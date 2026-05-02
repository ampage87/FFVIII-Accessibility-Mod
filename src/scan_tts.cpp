// scan_tts.cpp - Scan spell TTS
//
// See scan_tts.h for the architecture summary and chapter plan.
//
// v0.14.60: Architectural fix — announce trigger moved from popup-spawn
// (kind=0x06 val=50 NEW edge in screenshot.inl) to the FIRST fire of
// the sub_B687C0 hook per Scan event. v0.14.59 BAT proved that the
// kind=0x06 val=50 popup spawns at action-commit time (≈9 seconds
// before the actual Scan UI window opens visually), causing the announce
// to fire too early. The sub_B687C0 hook fires when the engine actually
// renders the Scan window's text — that's the genuine "window open"
// signal. The action-layer cue (popup-hook in noeffect.inl, magicId==39
// in ewm.inl) still captures the snapshot silently and sets
// s_pendingScanSlot; the first sub_B687C0 fire consumes it and speaks.
// Per-scan reset of s_scanHookFireCount in OnScanCast(_, true) makes
// "first fire" mean "first fire of THIS scan," not the cumulative count
// across all scans this battle.
//
// v0.14.59 (now superseded above): UX redesign — silent action-layer +
// screen-open auto-announce + interactive number-key queries 1..0.
// The 30-second action-layer lock from v0.14.57 is retired (silent
// action-layer = no purpose), and the sub_84F860 dispatcher hook from
// v0.14.54 is retired (full-view-only — popup-spawn detection in
// screenshot.inl is universal across full and compacted views). The
// sub_B687C0 text-fetch hook from v0.14.52 is kept installed; v0.14.59
// made it vestigial, v0.14.60 promotes it to the announce trigger.
// Fields 5..0 (Stats / Weak / Absorb / Nullify / StatusRes /
// ActiveStatus) reply 'Not implemented yet.' and land in v0.14.61..64.
//
// Key data flow per cast (Magic-menu / Draw-Cast happy path):
//   action-commit
//     → sub_48D200 popup hook (battle_tts_sprite.inl) sees
//        text_id=0x06 value=0x32 → sets s_lastScanCastTick
//     → sub_48E830 hook (sprite_spawn.inl) calls
//        NoEffect_RecordSnapshot which checks s_lastScanCastTick,
//        skips its own snapshot, and calls OnScanCast(slot, true)
//     → OnScanCast captures ScanSnapshot{name, level, HP, monster_id,
//        description} into s_scanCache[slot]; sets s_pendingScanSlot;
//        resets s_scanHookFireCount=0 so the first sub_B687C0 fire
//        below counts as #1 of this scan.
//   ~9 seconds later, when the Scan UI window opens visually
//     → sub_B687C0 fires for the first time this scan event
//     → HookedScanGetText sees count==1, calls OnScanPopupSpawn
//     → OnScanPopupSpawn reads s_pendingScanSlot, speaks the
//        announce line, sets s_scanScreenActiveSlot
//   while screen open
//     → PollHPCheckKeys (battle_tts_hp.inl) sees IsScreenActive()
//        and routes number keys 1..0 to SpeakField
//     → sub_B687C0 may fire repeatedly (engine re-reads text per
//        frame); fires #2..N are no-ops.
//   end of cast
//     → PollPopupRecords detects DESPAWN of kind=0x06 val=50,
//        calls OnScanPopupDespawn
//     → OnScanPopupDespawn clears s_scanScreenActiveSlot. Cache stays.

#include "scan_tts.h"
#include "ff8_text_decode.h"
#include "battle_tts.h"  // BATTLE_ENTITY_ARRAY_BASE, BATTLE_ENTITY_STRIDE,
                         // BATTLE_ALLY_SLOTS, BATTLE_TOTAL_SLOTS,
                         // BENT_CUR_HP, BENT_MAX_HP, BENT_LEVEL
#include "minhook/include/MinHook.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Forward declarations for cross-module namespaces (matches the pattern
// used by gf_audio_desc.cpp). We avoid pulling in the full headers to
// keep this module's dependencies minimal.
namespace Log {
    void Mod(const char* format, ...);
    void Battle(const char* format, ...);
}
namespace ScreenReader {
    bool SpeakChannel2(const char* text, bool interrupt = false);
}
// v0.14.65: Forward decl for the non-blocking screenshot helper added
// alongside CancelNoEffectWatchdogForSlot in battle_tts.cpp. Used by the
// auto-capture in HookedScanGetText below to write the rendered Scan UI
// to disk for offline visual validation against the [SCAN-CACHE] memory
// reads (especially the 8-stat block whose RAM order STR/VIT/MAG/SPR/
// SPD/LCK/EVA/HIT differs from .dat-file order).
namespace BattleTTS {
    void RequestScreenshotAsync(const char* basePath);
}

namespace ScanTTS {

// ============================================================================
// Engine address constants
// ============================================================================
//
// monster_id / description lookup chain (from deep research, see
// Plan & Research Documents/Scan spell deep research results.md).
// For an enemy at battle entity slot N, the Scan UI fetches its
// description like so:
//   monster_id = *(uint8_t*) (entity_base + 0xB3)
//   pos        = *(uint16_t*)(SCAN_TEXT_POSITIONS + monster_id * 2)
//   raw bytes  = (const uint8_t*)(SCAN_TEXT_DATA + pos)
//   string     = FF8TextDecode::Decode(raw, 256)
// FFNx names these tables `scan_text_positions` and `scan_text_data`.
// Allies don't have a meaningful entry (the byte at 0xB3 is whatever
// the engine has loaded there — usually 0 or stale data); the auto-
// announce omits the description for ally targets.
static const uint32_t SCAN_TEXT_POSITIONS = 0x01887474;
static const uint32_t SCAN_TEXT_DATA      = 0x018875B4;
static const uint32_t BENT_MONSTER_ID     = 0xB3;  // u8

// Engine name accessors (already in use by v0.14.50 first slice).
static const uint32_t BATTLE_GET_MONSTER_NAME_ADDR = 0x495100;  // sub_495100
static const uint32_t BATTLE_GET_ACTOR_NAME_ADDR   = 0x47EAF0;  // sub_47EAF0

typedef char* (__cdecl *GetMonsterNameFn)(int slotIndex);
typedef char* (__cdecl *GetActorNameFn)(int slotIndex);

// Hidden-HP soft fallback threshold (v0.14.50). Anything with a max HP
// over this value gets announced as "HP unknown" rather than a numeric
// value. v0.14.64 polish will replace this with the authoritative
// monster_id whitelist read out of sub_84F860's cmp-chain.
static const uint32_t HIDDEN_HP_SOFT_THRESHOLD = 99999;

// ============================================================================
// Snapshot struct + cache
// ============================================================================
//
// One ScanSnapshot per battle entity slot, populated by OnScanCast at
// action-commit time and read by OnScanPopupSpawn / SpeakField. Cleared
// on battle entry. Persists across the popup despawn so re-querying a
// previously-scanned target works.
//
// v0.14.59 captures only the fields needed for keys 1..4 (Name,
// Description, Level, HP). The struct already reserves space for the
// fields that v0.14.60..v0.14.63 will populate (stats, elem, statusRes,
// activeStatus) so adding the read code in those builds doesn't require
// touching the struct or the cache plumbing.
struct ScanSnapshot {
    bool     valid;
    int      slot;            // 0..6 (matches battle entity slot)
    bool     isAlly;          // slot < BATTLE_ALLY_SLOTS
    char     name[64];
    uint8_t  level;
    uint32_t curHP;
    uint32_t maxHP;
    bool     hpHidden;        // soft threshold or whitelist
    uint8_t  monsterId;
    char     description[256];
    bool     hasDescription;

    // v0.14.60..v0.14.63 fields (declared but not populated by v0.14.59)
    uint8_t  stats[8];        // STR..HIT at 0xB5..0xBC
    uint16_t elem[8];         // Fire..Holy at 0x3C..0x4B (8 x u16)
    uint8_t  statusRes[20];   // 0x4C..0x5F (20 x u8)
    uint32_t activeStatus;    // composite of TIMED_STATUS_0..3 + PERSIST_STATUS
};

static ScanSnapshot s_scanCache[BATTLE_TOTAL_SLOTS] = {};

// Cross-thread state. The action-layer and popup-spawn detectors live
// in the mod thread (BattleTTS::Update); the sub_B687C0 hook fires on
// the game thread. Atomic primitives keep the cross-thread reads safe
// without locking.
//
// s_pendingScanSlot is set by OnScanCast(_, true) at action-commit and
// consumed (read + cleared) by OnScanPopupSpawn one frame later when
// the popup record appears in the table. Value is the battle-entity
// slot 0..6, or -1 when no Scan is pending.
//
// s_scanScreenActiveSlot is set by OnScanPopupSpawn when it announces,
// and cleared by OnScanPopupDespawn. -1 means no Scan window is open;
// 0..6 means the keyboard router should redirect 1..0.
static volatile LONG s_pendingScanSlot       = -1;
static volatile LONG s_scanScreenActiveSlot  = -1;

// ============================================================================
// sub_B687C0 hook — the announce trigger (v0.14.60+)
// ============================================================================
//
// Fires when the engine reads scan text for the Scan UI window. This is
// the genuine "window opened visually" signal — it lags the
// kind=0x06 val=50 popup-spawn by ~9 seconds (the cast animation), but
// matches what the player actually sees on screen.
//
// Per-scan reset of s_scanHookFireCount in OnScanCast(_, true) makes
// fire #1 mean "first fire of THIS scan event," not the cumulative
// count across all scans this battle. On count==1 we call
// OnScanPopupSpawn which consumes s_pendingScanSlot and speaks the
// pre-captured snapshot. Subsequent fires (the engine re-reads text
// per frame while the window is open) are silent no-ops.
//
// v0.14.54 added a parallel hook on sub_84F860 (the Scan UI phase
// dispatcher) which v0.14.55 BAT proved was full-view-only (compacted
// view skips it). v0.14.59 retired that hook entirely. The popup-spawn
// trigger from v0.14.59 is also retired in v0.14.60 — it fired too
// early and caused the duplicate announcement Aaron heard in BAT.
static const uint32_t SCAN_GET_TEXT_ADDR = 0x00B687C0;
typedef const char* (__cdecl *ScanGetTextFn)(int slotIndex);
static ScanGetTextFn s_originalScanGetText = nullptr;
static bool s_scanGetTextHookInstalled = false;
static volatile LONG s_scanHookFireCount = 0;

// ============================================================================
// Memory read helpers (SEH-guarded)
// ============================================================================
//
// MSVC /EHsc forbids __try in functions that contain non-trivial
// destructors (C2712). std::string in the decode path triggers that,
// so the work is split across helpers as in v0.14.50.

static uint8_t* CallEngineNameAccessor(int slot)
{
    uint8_t* raw = nullptr;
    __try {
        if (slot < BATTLE_ALLY_SLOTS) {
            GetActorNameFn fn = (GetActorNameFn)BATTLE_GET_ACTOR_NAME_ADDR;
            raw = (uint8_t*)fn(slot);
        } else {
            GetMonsterNameFn fn = (GetMonsterNameFn)BATTLE_GET_MONSTER_NAME_ADDR;
            raw = (uint8_t*)fn(slot);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw = nullptr;
    }
    return raw;
}

static bool DecodeNameToBuf(uint8_t* raw, char* outBuf, int outBufSize)
{
    if (!raw || !outBuf || outBufSize <= 0) return false;
    std::string decoded = FF8TextDecode::Decode(raw, 64);
    if (decoded.empty()) return false;
    size_t n = decoded.size();
    if ((int)n >= outBufSize) n = (size_t)(outBufSize - 1);
    memcpy(outBuf, decoded.data(), n);
    outBuf[n] = '\0';
    return outBuf[0] != '\0';
}

static bool DecodeNameSafe(uint8_t* raw, char* outBuf, int outBufSize)
{
    bool ok = false;
    __try {
        ok = DecodeNameToBuf(raw, outBuf, outBufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

static bool ReadSlotName(int slot, char* outName, int outNameSize)
{
    if (!outName || outNameSize <= 0) return false;
    outName[0] = '\0';
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;

    uint8_t* raw = CallEngineNameAccessor(slot);
    if (!raw) return false;
    if ((uintptr_t)raw < 0x10000 || (uintptr_t)raw > 0x7FFFFFFF) return false;

    return DecodeNameSafe(raw, outName, outNameSize);
}

static bool ReadSlotHP(int slot, uint32_t* outCur, uint32_t* outMax)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (!outCur || !outMax) return false;

    uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
    __try {
        if (slot < BATTLE_ALLY_SLOTS) {
            *outCur = *(uint16_t*)(base + BENT_CUR_HP);
            *outMax = *(uint16_t*)(base + BENT_MAX_HP);
        } else {
            *outCur = *(uint32_t*)(base + BENT_CUR_HP);
            *outMax = *(uint32_t*)(base + BENT_MAX_HP);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadSlotLevel(int slot, uint8_t* outLevel)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (!outLevel) return false;

    uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
    __try {
        *outLevel = *(uint8_t*)(base + BENT_LEVEL);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadSlotMonsterId(int slot, uint8_t* outId)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (!outId) return false;

    uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
    __try {
        *outId = *(uint8_t*)(base + BENT_MONSTER_ID);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.14.65: Read the 8-byte stat block at `entity_base + 0xB5..0xBC`. Per
// the validated BENT_* constants in battle_tts.h, the RAM order is:
//   stats[0] = STR (0xB5)   stats[4] = SPD (0xB9)
//   stats[1] = VIT (0xB6)   stats[5] = LCK (0xBA)
//   stats[2] = MAG (0xB7)   stats[6] = EVA (0xBB)
//   stats[3] = SPR (0xB8)   stats[7] = HIT (0xBC)
// Note this differs from the FFRTT Section-7 .dat file order (which has
// LUCK at the end). The runtime-RAM order has LCK at index 5, between
// SPD and EVA — these are all already-validated offsets in battle_tts.h
// (Aaron's prior testing confirmed them via HP / level / draw reads).
// FormatStats below prints them in this RAM order so the indices match
// the BENT_* names if anyone cross-references later.
static bool ReadSlotStats(int slot, uint8_t outStats[8])
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (!outStats) return false;

    uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
    __try {
        // BENT_STR through BENT_HIT are 8 contiguous bytes (0xB5..0xBC),
        // safely inside the 0xD0-byte entity struct. memcpy is fine; we
        // already read HP/level out of the same struct above without issue.
        memcpy(outStats, base + BENT_STR, 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(outStats, 0, 8);
        return false;
    }
}

// ============================================================================
// Description lookup
// ============================================================================
//
// The split-helper trick again: ResolveDescriptionToBuf uses std::string
// and cannot live inside an __try, so we wrap it in a scalar-only
// ResolveDescriptionSafe.

static bool ResolveDescriptionToBuf(uint8_t monsterId, char* outBuf, int outBufSize)
{
    if (!outBuf || outBufSize <= 0) return false;
    outBuf[0] = '\0';

    // monster_id 0xFF is the engine's "no monster" sentinel; bail.
    if (monsterId == 0xFF) return false;

    uint16_t pos = *(uint16_t*)(SCAN_TEXT_POSITIONS + monsterId * 2);
    // Offsets >= 0x4000 are almost certainly garbage — the description
    // pool isn't that large. Defensive bail.
    if (pos == 0xFFFF || pos >= 0x4000) return false;

    const uint8_t* raw = (const uint8_t*)(SCAN_TEXT_DATA + pos);
    std::string decoded = FF8TextDecode::Decode(raw, 256);
    if (decoded.empty()) return false;

    size_t n = decoded.size();
    if ((int)n >= outBufSize) n = (size_t)(outBufSize - 1);
    memcpy(outBuf, decoded.data(), n);
    outBuf[n] = '\0';
    return outBuf[0] != '\0';
}

static bool ResolveDescriptionSafe(uint8_t monsterId, char* outBuf, int outBufSize)
{
    bool ok = false;
    __try {
        ok = ResolveDescriptionToBuf(monsterId, outBuf, outBufSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// ============================================================================
// Snapshot capture
// ============================================================================
//
// Populates s_scanCache[slot] from the live entity struct. Idempotent.
// All field reads are SEH-guarded; missing fields land as defaults.
static void CaptureSnapshot(int slot)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return;

    ScanSnapshot& snap = s_scanCache[slot];
    memset(&snap, 0, sizeof(snap));
    snap.slot   = slot;
    snap.isAlly = (slot < BATTLE_ALLY_SLOTS);

    // Name. Fall back to a generic label so SpeakField always has
    // something rather than going silent.
    if (!ReadSlotName(slot, snap.name, sizeof(snap.name))) {
        snprintf(snap.name, sizeof(snap.name),
                 snap.isAlly ? "Ally %d" : "Enemy %d", slot);
    }

    // Level
    ReadSlotLevel(slot, &snap.level);

    // HP
    if (ReadSlotHP(slot, &snap.curHP, &snap.maxHP)) {
        snap.hpHidden = (snap.maxHP > HIDDEN_HP_SOFT_THRESHOLD);
    }

    // monster_id + description (enemies only — ally byte at 0xB3 is
    // whatever the engine has loaded; description lookup would index
    // into the wrong table and produce garbage).
    if (!snap.isAlly && ReadSlotMonsterId(slot, &snap.monsterId)) {
        snap.hasDescription = ResolveDescriptionSafe(
            snap.monsterId, snap.description, sizeof(snap.description));
    }

    // v0.14.65: Stats block at 0xB5..0xBC. Read all 8 bytes regardless of
    // ally/enemy — allies have stat values too (junction-modified character
    // stats), and the runtime values are valid for both sides. The fields
    // were already declared in ScanSnapshot but unpopulated through
    // v0.14.59..v0.14.64.
    ReadSlotStats(slot, snap.stats);

    snap.valid = true;

    Log::Battle("BattleTTS: [SCAN-CACHE] Captured slot=%d name='%s' level=%u "
                "curHP=%u maxHP=%u hpHidden=%d monsterId=0x%02X hasDesc=%d "
                "stats=[STR=%u VIT=%u MAG=%u SPR=%u SPD=%u LCK=%u EVA=%u HIT=%u]",
                slot, snap.name, (unsigned)snap.level,
                (unsigned)snap.curHP, (unsigned)snap.maxHP,
                (int)snap.hpHidden, (unsigned)snap.monsterId,
                (int)snap.hasDescription,
                (unsigned)snap.stats[0], (unsigned)snap.stats[1],
                (unsigned)snap.stats[2], (unsigned)snap.stats[3],
                (unsigned)snap.stats[4], (unsigned)snap.stats[5],
                (unsigned)snap.stats[6], (unsigned)snap.stats[7]);
}

// ============================================================================
// Speech composition
// ============================================================================
//
// Build the auto-announce line. Format:
//   Enemy: "<Name>. <Description>. Press number keys 1 through 0 for details."
//   Enemy without description: "<Name>. Press number keys 1 through 0 for details."
//   Ally:  "<Name>. Press number keys 1 through 0 for details."
static void BuildAutoAnnounce(const ScanSnapshot& snap, char* out, int outSize)
{
    int pos = 0;
    pos += snprintf(out + pos, outSize - pos, "%s.", snap.name);
    if (!snap.isAlly && snap.hasDescription) {
        // v0.14.60: Strip trailing period from description before composing.
        // The decoded scan text from the game data ends in '.' and our
        // template appends another '.' — producing "...may be a shark..
        // Press number keys..." with a double period in v0.14.59 BAT. Trim
        // any trailing period (and trailing whitespace just in case) so the
        // composed announce reads naturally.
        const char* desc = snap.description;
        size_t descLen = strlen(desc);
        while (descLen > 0 &&
               (desc[descLen - 1] == '.' || desc[descLen - 1] == ' ' ||
                desc[descLen - 1] == '\t' || desc[descLen - 1] == '\n')) {
            descLen--;
        }
        if (descLen > 0) {
            char trimmed[256];
            size_t copyLen = (descLen >= sizeof(trimmed)) ? sizeof(trimmed) - 1 : descLen;
            memcpy(trimmed, desc, copyLen);
            trimmed[copyLen] = '\0';
            pos += snprintf(out + pos, outSize - pos, " %s.", trimmed);
        }
    }
    pos += snprintf(out + pos, outSize - pos,
                    " Press number keys 1 through 0 for details.");
    (void)pos;
}

// Format helpers for SpeakField. Tiny one-liners but keep the switch
// readable.
static void FormatLevel(const ScanSnapshot& snap, char* out, int outSize)
{
    if (snap.level > 0) {
        snprintf(out, outSize, "Level %u.", (unsigned)snap.level);
    } else {
        snprintf(out, outSize, "Level unknown.");
    }
}

static void FormatHP(const ScanSnapshot& snap, char* out, int outSize)
{
    if (snap.maxHP == 0) {
        snprintf(out, outSize, "HP unknown.");
    } else if (snap.hpHidden) {
        snprintf(out, outSize, "HP unknown.");
    } else {
        snprintf(out, outSize, "HP %u of %u.",
                 (unsigned)snap.curHP, (unsigned)snap.maxHP);
    }
}

// v0.14.65: Format the 8-stat block for SpeakField(5). Speaks all 8 stats
// in RAM order using familiar FF8 stat names. Order: Strength, Vitality,
// Magic, Spirit, Speed, Luck, Evasion, Hit — matches the BENT_* offset
// constants in battle_tts.h. If all 8 read as zero (likely an empty slot
// or read failure), fall back to a neutral message instead of speaking
// 'Strength 0. Vitality 0...'
static void FormatStats(const ScanSnapshot& snap, char* out, int outSize)
{
    bool anyNonZero = false;
    for (int i = 0; i < 8; i++) {
        if (snap.stats[i] != 0) { anyNonZero = true; break; }
    }
    if (!anyNonZero) {
        snprintf(out, outSize, "Stats unavailable.");
        return;
    }

    snprintf(out, outSize,
             "Strength %u. Vitality %u. Magic %u. Spirit %u. "
             "Speed %u. Luck %u. Evasion %u. Hit %u.",
             (unsigned)snap.stats[0],   // STR
             (unsigned)snap.stats[1],   // VIT
             (unsigned)snap.stats[2],   // MAG
             (unsigned)snap.stats[3],   // SPR
             (unsigned)snap.stats[4],   // SPD
             (unsigned)snap.stats[5],   // LCK
             (unsigned)snap.stats[6],   // EVA
             (unsigned)snap.stats[7]);  // HIT
}

// ============================================================================
// Public API
// ============================================================================

void OnScanCast(int targetSlot, bool fromActionLayer)
{
    if (targetSlot < 0 || targetSlot >= BATTLE_TOTAL_SLOTS) {
        Log::Battle("BattleTTS: [SCAN-TTS] OnScanCast skipped: invalid targetSlot=%d "
                    "(bitmask was zero, multi-bit, or out of range)", targetSlot);
        return;
    }

    if (!fromActionLayer) {
        // v0.14.60: No code path currently calls OnScanCast with
        // fromActionLayer=false (the sub_B687C0 hook now calls
        // OnScanPopupSpawn directly on first fire). Kept as a defensive
        // guard in case a future build re-routes through OnScanCast.
        return;
    }

    // Capture the snapshot silently. The first sub_B687C0 fire of this
    // Scan event will pick this up and announce.
    CaptureSnapshot(targetSlot);
    InterlockedExchange(&s_pendingScanSlot, (LONG)targetSlot);

    // v0.14.60: Reset the sub_B687C0 fire counter so the next fire is
    // counted as #1 of THIS scan, not the cumulative count across all
    // scans this battle. HookedScanGetText keys its OnScanPopupSpawn
    // call on count==1.
    InterlockedExchange(&s_scanHookFireCount, 0);

    Log::Battle("BattleTTS: [SCAN-TTS] Action-layer fire slot=%d (silent; pending announce on first sub_B687C0 fire)",
                targetSlot);

    // v0.14.51: Cancel the no-effect watchdog. Same rationale as before:
    // Scan goes through the player-magic action-announce path which
    // records a watchdog snapshot, but Scan deals no HP/status/display
    // change. Without cancellation the watchdog would queue 'No effect
    // on <target>' ~6 s later.
    BattleTTS::CancelNoEffectWatchdogForSlot(targetSlot);
}

void OnScanPopupSpawn()
{
    LONG pending = InterlockedExchange(&s_pendingScanSlot, -1);
    if (pending < 0 || pending >= BATTLE_TOTAL_SLOTS) {
        Log::Battle("BattleTTS: [SCAN-TTS] OnScanPopupSpawn: no pending slot "
                    "(action-layer didn't fire; possible Doomtrain edge case)");
        return;
    }

    int slot = (int)pending;
    const ScanSnapshot& snap = s_scanCache[slot];
    if (!snap.valid) {
        Log::Battle("BattleTTS: [SCAN-TTS] OnScanPopupSpawn: pending slot %d has invalid "
                    "snapshot — cache may have been reset between action-layer and popup",
                    slot);
        return;
    }

    InterlockedExchange(&s_scanScreenActiveSlot, (LONG)slot);

    char msg[512] = {};
    BuildAutoAnnounce(snap, msg, sizeof(msg));

    Log::Battle("BattleTTS: [SCAN-TTS] Auto-announce slot=%d msg='%s'", slot, msg);
    ScreenReader::SpeakChannel2(msg, true);
}

void OnScanPopupDespawn()
{
    LONG prev = InterlockedExchange(&s_scanScreenActiveSlot, -1);
    if (prev >= 0) {
        Log::Battle("BattleTTS: [SCAN-TTS] Screen closed (slot=%ld); number keys revert to ally HP",
                    (long)prev);
    }
    // Snapshot cache is intentionally retained — supports re-scan within
    // the same battle and any future "speak last scan's data" feature.
}

bool IsScreenActive()
{
    return InterlockedCompareExchange(&s_scanScreenActiveSlot, -1, -1) >= 0;
}

int GetActiveSlot()
{
    return (int)InterlockedCompareExchange(&s_scanScreenActiveSlot, -1, -1);
}

void SpeakField(int fieldId)
{
    int slot = GetActiveSlot();
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) {
        // Should never happen if the keyboard router checks IsScreenActive
        // first, but defensive.
        return;
    }

    const ScanSnapshot& snap = s_scanCache[slot];
    if (!snap.valid) {
        ScreenReader::SpeakChannel2("No scan data available.", true);
        return;
    }

    char msg[512] = {};
    switch (fieldId) {
    case 1:  // Name
        snprintf(msg, sizeof(msg), "%s.", snap.name);
        break;
    case 2:  // Description
        if (snap.isAlly) {
            snprintf(msg, sizeof(msg), "No description available.");
        } else if (snap.hasDescription) {
            snprintf(msg, sizeof(msg), "%s", snap.description);
        } else {
            snprintf(msg, sizeof(msg), "No description available.");
        }
        break;
    case 3:  // Level
        FormatLevel(snap, msg, sizeof(msg));
        break;
    case 4:  // HP
        FormatHP(snap, msg, sizeof(msg));
        break;
    case 5:  // Stats (v0.14.65)
        FormatStats(snap, msg, sizeof(msg));
        break;
    case 6:  // Weaknesses (v0.14.66)
    case 7:  // Absorbs (v0.14.66)
    case 8:  // Nullifies (v0.14.66)
    case 9:  // Status Resistances (v0.14.67)
    case 0:  // Active Statuses (v0.14.68)
        snprintf(msg, sizeof(msg), "Not implemented yet.");
        break;
    default:
        snprintf(msg, sizeof(msg), "Unknown field.");
        break;
    }

    Log::Battle("BattleTTS: [SCAN-TTS] SpeakField slot=%d fieldId=%d msg='%s'",
                slot, fieldId, msg);
    ScreenReader::SpeakChannel2(msg, true);
}

void OnBattleEnter()
{
    memset(s_scanCache, 0, sizeof(s_scanCache));
    InterlockedExchange(&s_pendingScanSlot, -1);
    InterlockedExchange(&s_scanScreenActiveSlot, -1);
    // v0.14.60: Reset the sub_B687C0 fire counter on battle enter so the
    // first scan in this battle starts clean. Subsequent scans within the
    // same battle reset it again via OnScanCast(_, true).
    InterlockedExchange(&s_scanHookFireCount, 0);
    Log::Battle("BattleTTS: [SCAN-TTS] OnBattleEnter — cache + screen state + hook count reset");
}

// ============================================================================
// sub_B687C0 hook (announce trigger as of v0.14.60)
// ============================================================================

static const char* __cdecl HookedScanGetText(int slotIndex)
{
    int slot = slotIndex & 0xFF;  // v0.14.58 mask: engine call site only sets CL
    LONG count = InterlockedIncrement(&s_scanHookFireCount);

    if (count == 1) {
        // v0.14.60: First fire of THIS scan event — the moment the engine
        // begins rendering the Scan UI window's text on screen. This is the
        // genuine "window opened" signal that we want the announce to ride
        // on. OnScanPopupSpawn consumes s_pendingScanSlot (set earlier by
        // the action-layer) and speaks.
        Log::Battle("BattleTTS: [SCAN-HOOK] sub_B687C0 fire #1 slot=%d (window-open trigger — announcing now)",
                    slot);
        OnScanPopupSpawn();

        // v0.14.65.1: Auto-capture for stat validation. v0.14.65 BAT proved
        // the original count==30 trigger was wrong — sub_B687C0 fires ONCE
        // per scan event when the engine fetches the text (then the
        // framebuffer is reused), NOT once per frame as the v0.14.60
        // architecture comment speculated. So count never reached 30 and
        // the screenshot never fired. Fix: trigger immediately here at
        // fire #1, alongside OnScanPopupSpawn. The async flag is set;
        // HookedSwapBuffers picks it up on the next swap (~16ms at 60fps)
        // and captures the framebuffer, which by then contains the
        // fully-rendered scan UI (fire #1 happens DURING the engine's
        // UI-render frame; SwapBuffers at end of that frame presents the
        // rendered result, so our capture catches it).
        //
        // Aaron is blind — he can't see the rendered UI to spot-check the
        // 8-stat block ordering (the RAM-order STR/VIT/MAG/SPR/SPD/LCK/EVA/
        // HIT differs from .dat-file order). The screenshot lets Claude
        // read the on-screen UI offline and confirm correctness.
        //
        // One capture per scan event. Path is relative to the FF8 working
        // directory; the file lands as
        // Screenshots\\scan_<HHMMSS>_<MS>_slot<N>_<safeName>.{bmp,png}.
        if (slot >= 0 && slot < BATTLE_TOTAL_SLOTS && s_scanCache[slot].valid) {
            // Sanitize name for filename: ASCII alnum + underscore only.
            // FF8 names are usually clean but defensive against rare cases
            // (apostrophes in 'Sorceress's' boss names, dashes in
            // 'Fastitocalon-F', etc.).
            char safeName[64] = {};
            const char* src = s_scanCache[slot].name;
            int j = 0;
            for (int i = 0; src[i] != '\0' && j < 63; i++) {
                char c = src[i];
                bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9');
                safeName[j++] = keep ? c : '_';
            }
            safeName[j] = '\0';
            if (j == 0) { strcpy(safeName, "unknown"); }

            // v0.14.65.2: Build absolute path under the project's diagnostic
            // screenshots dir (same place as kind4_*, poll_NEW_*, popup_time_*
            // captures). v0.14.65.1 used a relative "Screenshots\\..." path
            // which resolved against FF8.exe's CWD (Steam install dir),
            // putting the file outside the project tree where Claude can't
            // read it. The directory itself is created on demand by the
            // existing kind4 / sprite-poll capture mechanisms; we don't
            // need a CreateDirectoryA call here as long as some other
            // capture has fired in this battle (and during a Scan event
            // the poll_NEW kind=06 val=50 capture always fires first).
            SYSTEMTIME wt;
            GetLocalTime(&wt);
            char path[512];
            snprintf(path, sizeof(path),
                     "%s\\scan_%02d%02d%02d_%03d_slot%d_%s",
                     BattleTTS::GetScreenshotDir(),
                     wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds,
                     slot, safeName);
            BattleTTS::RequestScreenshotAsync(path, 90);
            Log::Battle("BattleTTS: [SCAN-CAPTURE] Auto-screenshot requested at fire #1 slot=%d path='%s.png' (90-frame delay ≈ 1.5 s)",
                        slot, path);
        } else {
            Log::Battle("BattleTTS: [SCAN-CAPTURE] Skipping fire #1 capture: slot=%d invalid or snapshot not valid",
                        slot);
        }
    } else if ((count % 60) == 0) {
        // Subsequent fires are silent no-ops (engine re-reads text per
        // frame while the window is open IF it does that at all — v0.14.65
        // BAT showed it actually fires exactly once per scan event, but
        // we keep the throttled diagnostic here in case a different game
        // path or future engine version behaves differently).
        Log::Battle("BattleTTS: [SCAN-HOOK] sub_B687C0 fire #%ld slot=%d (subsequent; no-op)",
                    (long)count, slot);
    }

    if (s_originalScanGetText) {
        return s_originalScanGetText(slotIndex);
    }
    return "";
}

static void InstallScanGetTextHook()
{
    if (s_scanGetTextHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)SCAN_GET_TEXT_ADDR,
        (LPVOID)HookedScanGetText,
        (LPVOID*)&s_originalScanGetText);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)SCAN_GET_TEXT_ADDR);
    }
    s_scanGetTextHookInstalled = (st == MH_OK);
    Log::Mod("[SCAN-TTS] sub_B687C0 hook @ 0x%08X — %s (trampoline=0x%08X)",
             SCAN_GET_TEXT_ADDR,
             (st == MH_OK) ? "OK" : "FAIL",
             (uint32_t)(uintptr_t)s_originalScanGetText);
}

void Initialize()
{
    Log::Mod("[SCAN-TTS] Initialized (v0.14.60: action-layer captures snapshot, sub_B687C0 first-fire announces; keys 1..4).");
    InstallScanGetTextHook();
    InterlockedExchange(&s_pendingScanSlot, -1);
    InterlockedExchange(&s_scanScreenActiveSlot, -1);
    InterlockedExchange(&s_scanHookFireCount, 0);
    memset(s_scanCache, 0, sizeof(s_scanCache));
}

}  // namespace ScanTTS
