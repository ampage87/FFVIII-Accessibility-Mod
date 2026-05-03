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

    // v0.14.69: Type label rendered at bottom-left of Scan UI (e.g.
    // 'Fly Monster', 'Earth Monster'). Captured asynchronously from
    // sub_47EC70 calls during the open Scan window — see
    // HandleBattleText below for the capture mechanism (v0.14.72: was
    // HookedGetBattleText prior to the hook-conflict resolution; the
    // capture logic itself is unchanged). Empty string means "not yet
    // captured" (will be filled within ~50ms of the Scan UI opening
    // visually). FormatLevel() appends to "Level N" announcement when
    // populated.
    char     typeLabel[64];

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
// v0.14.72: HandleBattleText — hook-forward entry point for sub_47EC70
// ============================================================================
//
// sub_47EC70 is FF8's canonical "fetch battle text by text_id" function
// per Aaron's user memory (used for victory text and other in-battle
// strings). First instructions confirm the signature is
//   const char* __cdecl get_battle_text(int text_id)
// with internal lookup `pos = u16 at [0x01CF8B50 + text_id*2]` and
// fallback ptr 0x01CFF84C if pos == 0xFFFF (not-present sentinel).
//
// HOOK ARCHITECTURE (v0.14.72):
//
// sub_47EC70 has exactly ONE hook installer in this codebase —
// InstallBattleTextHooks() in battle_tts_victory.inl, which has been
// installing the victory text hook since v0.13.14. v0.14.68-diag added
// a SECOND installer here in scan_tts.cpp for the type-label capture
// work; that second installer silently lost the MinHook race
// (MH_ERROR_ALREADY_CREATED for whichever ran second) and broke
// victory phase detection. v0.14.72 removes that second installer
// entirely — the victory hook now forwards every sub_47EC70 call into
// this HandleBattleText function via a one-line call after s_origBt1.
//
// Internally we gate on GetScanFlightSlot() so this is a near-no-op
// outside an active scan event (the victory hook itself runs on every
// sub_47EC70 call, ~hundreds per battle frame, so we have to be cheap
// in the hot path).
static const uint32_t TYPE_LABEL_MONSTER_TEXT_ID = 36;  // 0x24, returns 'Monster'

// v0.14.69: Type-label tracking state. The Scan UI renders the
// 'Fly Monster' / 'Earth Monster' / etc. label at the bottom-left
// via TWO consecutive sub_47EC70 calls: first the type prefix (e.g.
// text_id=99 returns 'Fly' for Glacial Eye), then text_id=36 which
// returns the universal 'Monster' suffix.
//
// We capture the type label by tracking the most recent NON-Monster
// call's returned bytes in s_lastTypePrefixBytes. When text_id=36
// fires during a Scan UI session, we decode the prior bytes via
// FF8TextDecode::Decode and compose '{prefix} Monster' into the
// active slot's typeLabel cache field.
//
// We snapshot the encoded bytes (not a decoded string) because the
// engine reuses its scan_text_data buffer between calls — by the
// time text_id=36 fires, the previous returned pointer may already
// be invalidated. The byte snapshot is stable for as long as we hold
// it, and the FF8 decoder is fast enough to run on the call edge.
static constexpr int  TYPE_PREFIX_MAX_BYTES = 32;
static uint8_t        s_lastTypePrefixBytes[TYPE_PREFIX_MAX_BYTES] = {};
static int            s_lastTypePrefixLen = 0;

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

// v0.14.73: Read the 8-element affinity block at `entity_base + 0x3C..0x4B`
// (BENT_ELEM_RESIST_BASE in battle_tts.h). 8 x u16 in element order:
//   elem[0]=Fire, [1]=Ice, [2]=Thunder, [3]=Earth,
//   [4]=Poison, [5]=Wind, [6]=Water, [7]=Holy.
// FF8 community-standard interpretation treats each value as i16:
//   v <  0  -> absorbs (heals)
//   v == 0  -> immune / nullifies
//   0 < v < 100 -> resists (partial reduction; not currently announced)
//   v == 100 -> normal damage (not announced)
//   v >  100 -> weak (extra damage)
// CaptureSnapshot logs the raw bytes under [SCAN-ELEM] so a BAT against
// a known-affinity monster (Glacial Eye absorbs Ice / weak to Fire,
// Bite Bug weak to Wind, etc.) can verify or correct this scale.
static bool ReadSlotElements(int slot, uint16_t outElem[8])
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (!outElem) return false;

    uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
    __try {
        // BENT_ELEM_RESIST_BASE is offset 0x3C, 16 bytes wide (8 u16),
        // safely inside the 0xD0-byte entity struct.
        memcpy(outElem, base + BENT_ELEM_RESIST_BASE, 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(outElem, 0, 16);
        return false;
    }
}

// v0.14.74: Read the 20-byte status resistance block at
// `entity_base + 0x4C` (BENT_STATUS_RESIST_BASE). Order per the deep
// research:
//   [0]=Death [1]=Poison [2]=Petrify [3]=Darkness [4]=Silence
//   [5]=Berserk [6]=Zombie [7]=Sleep [8]=Haste [9]=Slow
//   [10]=Stop [11]=Regen [12]=Reflect [13]=Doom [14]=Slow Petrify
//   [15]=Float [16]=Confuse [17]=Drain [18]=Expulsion [19]=???
// Each byte adds to a 100 baseline to form the StatusDefense value
// used in the inflict formula. `byte == 0` is the "Weak to" threshold
// per the Scan UI; `byte >= 100` is the "Strong vs" threshold per the
// deep research. SEH-guarded; mirrors ReadSlotElements pattern.
static bool ReadSlotStatusRes(int slot, uint8_t outStatusRes[20])
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (!outStatusRes) return false;

    uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
    __try {
        memcpy(outStatusRes, base + BENT_STATUS_RESIST_BASE, 20);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(outStatusRes, 0, 20);
        return false;
    }
}

// v0.14.74.1: Diagnostic dump of the entity struct from +0x3C through
// +0xB4 (121 bytes) to locate the actual runtime offset of the 20-byte
// status resistance block. v0.14.74 BAT proved BENT_STATUS_RESIST_BASE
// = 0x4C is wrong — Grat's read produced an alternating 169 / 251
// pattern that's clearly not status data. The deep research predicted
// that offset but explicitly flagged it as needing runtime validation;
// this diagnostic provides the validation.
//
// Range covers:
//   +0x3C..+0x4B  elements (16 bytes, CONFIRMED ANCHOR — must match
//                 the same u16 values [SCAN-ELEM] dumps so we can
//                 verify base+offset arithmetic is correct)
//   +0x4C..+0xB3  unknown gap (104 bytes — the 20-byte status block
//                 lives somewhere in here; PC port presumably inserted
//                 padding / other fields that broke the .dat-relative
//                 spacing assumption the deep research used)
//   +0xB4         level (CONFIRMED ANCHOR — should equal snap.level
//                 in [SCAN-CACHE] so we know dump alignment held all
//                 the way to the bottom)
//
// 121 bytes split across 8 log lines for greppability and visual
// alignment in the log diff. Test plan: BAT against three enemies
// with known status profiles (Grat, T-Rexaur, Tonberry) and diff the
// three [SCAN-STRUCT] sections. The 20-byte run where Grat shows 0x00
// at indices 4 (Silence), 5 (Berserk), 7 (Sleep) but Tonberry shows
// high values everywhere is the answer.
//
// Why a separate helper for the row body: MSVC /EHsc forbids non-
// trivial destructors inside __try (C2712). snprintf and Log::Battle
// are plain C-style calls so they compile fine inside __try, but
// keeping the row body in its own function is clearer and matches
// the Decode*ToBuf / Decode*Safe pattern used elsewhere in this
// module. SEH propagates from DumpRow up into LogStructDump's
// __except handler, so a faulting base[off] read still gets caught.

static void DumpRow(int slot, const uint8_t* base, int from, int to,
                    const char* tag)
{
    char hex[80];
    int pos = 0;
    for (int off = from; off <= to; off++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos,
                        (off == from ? "%02X" : " %02X"),
                        (unsigned)base[off]);
        if (pos >= (int)sizeof(hex)) break;
    }
    Log::Battle("BattleTTS: [SCAN-STRUCT] slot=%d +0x%02X..+0x%02X %s: %s",
                slot, from, to, tag, hex);
}

static void LogStructDump(int slot, const uint8_t* base)
{
    __try {
        DumpRow(slot, base, 0x3C, 0x4B, "(elements anchor)");
        DumpRow(slot, base, 0x4C, 0x5B, "                 ");
        DumpRow(slot, base, 0x5C, 0x6B, "                 ");
        DumpRow(slot, base, 0x6C, 0x7B, "                 ");
        DumpRow(slot, base, 0x7C, 0x8B, "                 ");
        DumpRow(slot, base, 0x8C, 0x9B, "                 ");
        DumpRow(slot, base, 0x9C, 0xAB, "                 ");
        DumpRow(slot, base, 0xAC, 0xB4, "(level anchor)   ");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [SCAN-STRUCT] slot=%d SEH fault during dump",
                    slot);
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

    // v0.14.73: 8-element affinity block at 0x3C..0x4B (8 x u16). Same
    // dual-side rationale — the engine populates this for both allies
    // and enemies during battle setup. v0.14.73.1 corrected the
    // interpretation in FormatWeak / FormatResistances to the
    // 800-anchored u16 scale documented in the deep research.
    ReadSlotElements(slot, snap.elem);

    // v0.14.74: 20-byte status resistance block at 0x4C..0x5F. Used by
    // FormatStatusResistances (key 9) and FormatStatusWeaknesses
    // (key 0); both filter to 13 offensive ailments for the announce.
    ReadSlotStatusRes(slot, snap.statusRes);

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

    // v0.14.73: Raw element-affinity bytes log line. If FormatWeak /
    // FormatAbsorb / FormatNullify announce surprising results in BAT,
    // these values reveal whether the i16 interpretation is correct.
    // Order: Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy.
    Log::Battle("BattleTTS: [SCAN-ELEM] slot=%d raw u16=[Fi=%u Ic=%u Th=%u Ea=%u "
                "Po=%u Wi=%u Wa=%u Ho=%u] as i16=[Fi=%d Ic=%d Th=%d Ea=%d "
                "Po=%d Wi=%d Wa=%d Ho=%d]",
                slot,
                (unsigned)snap.elem[0], (unsigned)snap.elem[1],
                (unsigned)snap.elem[2], (unsigned)snap.elem[3],
                (unsigned)snap.elem[4], (unsigned)snap.elem[5],
                (unsigned)snap.elem[6], (unsigned)snap.elem[7],
                (int)(int16_t)snap.elem[0], (int)(int16_t)snap.elem[1],
                (int)(int16_t)snap.elem[2], (int)(int16_t)snap.elem[3],
                (int)(int16_t)snap.elem[4], (int)(int16_t)snap.elem[5],
                (int)(int16_t)snap.elem[6], (int)(int16_t)snap.elem[7]);

    // v0.14.74: Raw status resistance bytes per the 20-byte block at
    // entity+0x4C, in deep research order. Threshold reference:
    // byte == 0 -> "Weak to" candidate; byte >= 100 -> "Strong vs" /
    // immune. T-Rexaur is the canonical anchor for calibration
    // (famously vulnerable to Sleep, Darkness, Death, Poison).
    Log::Battle("BattleTTS: [SCAN-STAT] slot=%d raw u8=[Death=%u Poison=%u Petrify=%u "
                "Darkness=%u Silence=%u Berserk=%u Zombie=%u Sleep=%u Haste=%u Slow=%u "
                "Stop=%u Regen=%u Reflect=%u Doom=%u SlowPetrify=%u Float=%u Confuse=%u "
                "Drain=%u Expulsion=%u Unknown=%u]",
                slot,
                (unsigned)snap.statusRes[0],  (unsigned)snap.statusRes[1],
                (unsigned)snap.statusRes[2],  (unsigned)snap.statusRes[3],
                (unsigned)snap.statusRes[4],  (unsigned)snap.statusRes[5],
                (unsigned)snap.statusRes[6],  (unsigned)snap.statusRes[7],
                (unsigned)snap.statusRes[8],  (unsigned)snap.statusRes[9],
                (unsigned)snap.statusRes[10], (unsigned)snap.statusRes[11],
                (unsigned)snap.statusRes[12], (unsigned)snap.statusRes[13],
                (unsigned)snap.statusRes[14], (unsigned)snap.statusRes[15],
                (unsigned)snap.statusRes[16], (unsigned)snap.statusRes[17],
                (unsigned)snap.statusRes[18], (unsigned)snap.statusRes[19]);

    // v0.14.74.1: 121-byte hex dump of the entity struct from +0x3C
    // through +0xB4. v0.14.74 BAT proved BENT_STATUS_RESIST_BASE = 0x4C
    // is wrong; the alternating 169/251 [SCAN-STAT] pattern is clearly
    // not status data and contradicts canon (Grat reportedly fully
    // resisted Sleep, but Grat is the in-game source of Sleep). Range
    // covers element-block anchor, unknown gap, and level anchor; see
    // LogStructDump for the full diagnostic plan.
    {
        const uint8_t* base = (const uint8_t*)(BATTLE_ENTITY_ARRAY_BASE +
                                               slot * BATTLE_ENTITY_STRIDE);
        LogStructDump(slot, base);
    }
}

// ============================================================================
// Speech composition
// ============================================================================
//
// Build the auto-announce line. Format:
//   Enemy: "<Name>. <Description>. Press numbers 0 through 9 for details."
//   Enemy without description: "<Name>. Press numbers 0 through 9 for details."
//   Ally:  "<Name>. Press numbers 0 through 9 for details."
//
// v0.14.73: phrasing changed from "Press number keys 1 through 0" to
// "Press numbers 0 through 9". v0.14.74: keys 5..0 reorganized — see
// the SpeakField switch below for the bindings (1=Name, 2=Description,
// 3=Level, 4=HP, 5=Offensive Stats, 6=Defensive Stats, 7=Elemental
// Resistances, 8=Elemental Weaknesses, 9=Status Resistances, 0=Status
// Weaknesses). The spoken phrase is unchanged in v0.14.74 — still says
// "Press numbers 0 through 9 for details."
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
                    " Press numbers 0 through 9 for details.");
    (void)pos;
}

// Format helpers for SpeakField. Tiny one-liners but keep the switch
// readable.
static void FormatLevel(const ScanSnapshot& snap, char* out, int outSize)
{
    // v0.14.69: If the type label was captured during the Scan UI
    // session (e.g. 'Fly Monster'), append it to the level announce
    // so key 3 says 'Level 14, Fly Monster.' instead of just
    // 'Level 14.'. Type label may be empty if the user pressed key 3
    // before the Scan UI fully opened, or for ally targets where the
    // engine doesn't fetch a type prefix — in those cases we fall
    // back to the plain level announce.
    bool hasType = (snap.typeLabel[0] != '\0');
    if (snap.level > 0) {
        if (hasType) {
            snprintf(out, outSize, "Level %u, %s.",
                     (unsigned)snap.level, snap.typeLabel);
        } else {
            snprintf(out, outSize, "Level %u.", (unsigned)snap.level);
        }
    } else {
        if (hasType) {
            snprintf(out, outSize, "Level unknown, %s.", snap.typeLabel);
        } else {
            snprintf(out, outSize, "Level unknown.");
        }
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

// v0.14.74: 8 stats split into two announcements (was a single 8-stat
// readout in v0.14.65..v0.14.73.1). Hearing all 8 numbers in one breath
// was overwhelming; splitting into 4-and-4 by combat role makes each
// readout digestible.
//
// snap.stats[8] RAM order (validated; matches BENT_* in battle_tts.h):
//   stats[0] = STR (0xB5)   stats[4] = SPD (0xB9)
//   stats[1] = VIT (0xB6)   stats[5] = LCK (0xBA)
//   stats[2] = MAG (0xB7)   stats[6] = EVA (0xBB)
//   stats[3] = SPR (0xB8)   stats[7] = HIT (0xBC)
//
// Aaron's grouping (v0.14.74 keys 5 / 6):
//   key 5 (offensive): Strength, Magic, Speed, Hit
//   key 6 (defensive): Vitality, Spirit, Evasion, Luck
//
// Luck is grouped defensive even though it affects critical-hit rate
// (offensive) too. The 4-and-4 balance and the fact that Luck applies
// to both sides motivated putting it on defensive. The all-zero
// fallback message changes per stat type so neither readout silently
// disappears on empty slots.

static void FormatOffensiveStats(const ScanSnapshot& snap, char* out, int outSize)
{
    bool anyNonZero = (snap.stats[0] != 0) || (snap.stats[2] != 0) ||
                      (snap.stats[4] != 0) || (snap.stats[7] != 0);
    if (!anyNonZero) {
        snprintf(out, outSize, "Offensive stats unavailable.");
        return;
    }
    snprintf(out, outSize,
             "Strength %u. Magic %u. Speed %u. Hit %u.",
             (unsigned)snap.stats[0],   // STR
             (unsigned)snap.stats[2],   // MAG
             (unsigned)snap.stats[4],   // SPD
             (unsigned)snap.stats[7]);  // HIT
}

static void FormatDefensiveStats(const ScanSnapshot& snap, char* out, int outSize)
{
    bool anyNonZero = (snap.stats[1] != 0) || (snap.stats[3] != 0) ||
                      (snap.stats[5] != 0) || (snap.stats[6] != 0);
    if (!anyNonZero) {
        snprintf(out, outSize, "Defensive stats unavailable.");
        return;
    }
    snprintf(out, outSize,
             "Vitality %u. Spirit %u. Evasion %u. Luck %u.",
             (unsigned)snap.stats[1],   // VIT
             (unsigned)snap.stats[3],   // SPR
             (unsigned)snap.stats[6],   // EVA
             (unsigned)snap.stats[5]);  // LCK
}

// ============================================================================
// v0.14.74: Element affinity + status resistance formatters
// (keys 7 / 8 elemental, keys 9 / 0 status)
// ============================================================================
//
// snap.elem[8] holds the 8 elemental affinity values populated from
// `entity_base + 0x3C` (BENT_ELEM_RESIST_BASE). Element order:
//   [0]=Fire [1]=Ice [2]=Thunder [3]=Earth
//   [4]=Poison [5]=Wind [6]=Water [7]=Holy
//
// snap.statusRes[20] holds the 20 status resistance bytes populated
// from `entity_base + 0x4C` (BENT_STATUS_RESIST_BASE). Status order
// per the deep research:
//   [0]=Death [1]=Poison [2]=Petrify [3]=Darkness [4]=Silence
//   [5]=Berserk [6]=Zombie [7]=Sleep [8]=Haste [9]=Slow
//   [10]=Stop [11]=Regen [12]=Reflect [13]=Doom [14]=Slow Petrify
//   [15]=Float [16]=Confuse [17]=Drain [18]=Expulsion [19]=???
//
// v0.14.74 KEY LAYOUT (matches the SpeakField switch below):
//   key 7 (Resistances) = combined Halves / Strongly resists / Nullifies / Absorbs
//   key 8 (Weaknesses)  = elements where v < 800
//   key 9 (Status Resistances) = statusRes byte >= 100, filtered to ailments
//   key 0 (Status Weaknesses)  = statusRes byte == 0, filtered to ailments
//
// ELEMENT SCALE (from v0.14.73.1 deep research; 800-anchored u16):
//   v <  800            -> Weak to                  (FormatWeak, key 8)
//   v == 800            -> Normal damage (silent)
//   800 <  v <  900     -> Halves                   (key 7 -> "Halves X")
//   v == 900            -> Nullifies                (key 7 -> "Nullifies X")
//   900 <  v <  1000    -> Strongly resists         (key 7 -> "Strongly resists X")
//   v >= 1000           -> Absorbs                  (key 7 -> "Absorbs X")
//
// STATUS SCALE (from deep research + Final Fantasy Wiki StatusDefense
// formula; byte adds to 100 baseline to form StatusDefense):
//   byte == 0  -> Weak to (fully vulnerable)        (FormatStatusWeaknesses, key 0)
//   1..99      -> Partial resistance (silent in UI; not announced)
//   byte >= 100 -> Strongly resists / immune         (FormatStatusResistances, key 9)
//
// The status "Weak to" threshold (byte == 0) is conservative and
// inferred from the Wiki StatusDefense formula. T-Rexaur is the
// canonical anchor for calibration: famously vulnerable to Sleep,
// Darkness, Death, Poison — v0.14.74 should announce those as
// weaknesses if the standard FF8 data has them at 0. The [SCAN-STAT]
// diagnostic log line in CaptureSnapshot dumps all 20 raw bytes per
// scan so any threshold adjustment is data-driven, not guessed.
//
// STATUS FILTER (keys 9 + 0 — same filter applied to both):
// We announce only the 13 offensive ailments players cast or junction.
// Buffs (Haste, Regen, Reflect, Float) and non-offensive effects
// (Drain, Expulsion, ???) are excluded — knowing an enemy "resists
// Haste" or "is weak to Reflect" is rarely actionable. If desired in
// a future build, a separate verbose mode could include all 20.
//
// HISTORY:
// - v0.14.73: shipped wrong i16 element interpretation (100-anchored).
//   The [SCAN-ELEM] diagnostic log saved us; deep research had the
//   correct 800-anchored u16 scale documented since 2026-04-29.
// - v0.14.73.1: corrected element scale.
// - v0.14.74: split stats key 5 -> 5 (offensive) + 6 (defensive);
//   consolidated key 7 into four-bucket Resistances (was Absorbs);
//   moved Weak from 6 to 8 (was Nullify); added Status Resistances on
//   9 and Status Weaknesses on 0 (both were 'Not implemented yet.'
//   previously). The active-statuses readout that used to live on
//   key 0 is removed because the same info is announced via the
//   target-cursor flow in BuildStatusString already. Memory edit #30
//   records: deep research docs first, guess second.

static const char* const ELEMENT_NAMES[8] = {
    "Fire", "Ice", "Thunder", "Earth",
    "Poison", "Wind", "Water", "Holy"
};

// All 20 status names in deep-research order. The diagnostic log in
// CaptureSnapshot uses these via STATUS_NAMES_20[idx]; the offensive
// ailment table below selects the 13 we announce on keys 9 / 0.
static const char* const STATUS_NAMES_20[20] = {
    "Death",        "Poison",       "Petrify",      "Darkness",
    "Silence",      "Berserk",      "Zombie",       "Sleep",
    "Haste",        "Slow",         "Stop",         "Regen",
    "Reflect",      "Doom",         "Slow Petrify", "Float",
    "Confuse",      "Drain",        "Expulsion",    "Unknown"
};

// Indices (into snap.statusRes[20]) of the 13 offensive ailments we
// announce on keys 9 (resistances) and 0 (weaknesses). Buffs and
// non-offensive effects are excluded — see comment above.
static const int OFFENSIVE_AILMENT_INDICES[13] = {
    0,   // Death
    1,   // Poison
    2,   // Petrify
    3,   // Darkness
    4,   // Silence
    5,   // Berserk
    6,   // Zombie
    7,   // Sleep
    9,   // Slow
    10,  // Stop
    13,  // Doom
    14,  // Slow Petrify
    16   // Confuse
};
static const int OFFENSIVE_AILMENT_COUNT =
    (int)(sizeof(OFFENSIVE_AILMENT_INDICES) / sizeof(OFFENSIVE_AILMENT_INDICES[0]));

// Build a natural-language list with Oxford commas: "Fire", "Fire and
// Ice", "Fire, Ice, and Thunder". Generic over a name table so the
// element and status formatters share the same join logic.
static void JoinNameList(const int* indices, int count,
                         const char* const* names,
                         char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (count <= 0 || !indices || !names) return;

    if (count == 1) {
        snprintf(out, outSize, "%s", names[indices[0]]);
        return;
    }
    if (count == 2) {
        snprintf(out, outSize, "%s and %s",
                 names[indices[0]], names[indices[1]]);
        return;
    }
    int pos = 0;
    for (int i = 0; i < count; i++) {
        const char* name = names[indices[i]];
        if (i == 0) {
            pos += snprintf(out + pos, outSize - pos, "%s", name);
        } else if (i == count - 1) {
            pos += snprintf(out + pos, outSize - pos, ", and %s", name);
        } else {
            pos += snprintf(out + pos, outSize - pos, ", %s", name);
        }
    }
}

static void FormatWeak(const ScanSnapshot& snap, char* out, int outSize)
{
    int matches[8];
    int count = 0;
    for (int i = 0; i < 8; i++) {
        // v0.14.73.1: u16 < 800 means weak (the .dat-file negative
        // bytes were widened to u16 and added to the 800 anchor).
        if (snap.elem[i] < 800) matches[count++] = i;
    }
    if (count == 0) {
        snprintf(out, outSize, "No elemental weaknesses.");
        return;
    }
    char list[256];
    JoinNameList(matches, count, ELEMENT_NAMES, list, sizeof(list));
    snprintf(out, outSize, "Weak against %s.", list);
}

// v0.14.74: Combined elemental resistances on key 7. Replaces the
// separate FormatAbsorb (key 7 in v0.14.73.1) and FormatNullify
// (key 8 in v0.14.73.1), and folds in Halves and Strongly resists
// which were silent in v0.14.73.1. Each non-empty bucket announces
// as its own sentence so the categories stay clear:
//   "Halves Fire and Earth. Strongly resists Ice. Nullifies Thunder.
//    Absorbs Water and Holy."
// Empty case: "No elemental resistances."
static void FormatResistances(const ScanSnapshot& snap, char* out, int outSize)
{
    int halves[8];        int hCount = 0;
    int strongResist[8];  int sCount = 0;
    int nullifies[8];     int nCount = 0;
    int absorbs[8];       int aCount = 0;

    for (int i = 0; i < 8; i++) {
        uint16_t v = snap.elem[i];
        if      (v >= 1000) absorbs[aCount++] = i;
        else if (v == 900)  nullifies[nCount++] = i;
        else if (v > 900)   strongResist[sCount++] = i;
        else if (v > 800)   halves[hCount++] = i;
        // v == 800 normal (silent), v < 800 weak (handled by FormatWeak)
    }

    if (hCount + sCount + nCount + aCount == 0) {
        snprintf(out, outSize, "No elemental resistances.");
        return;
    }

    int pos = 0;
    char list[256];

    if (hCount > 0) {
        JoinNameList(halves, hCount, ELEMENT_NAMES, list, sizeof(list));
        pos += snprintf(out + pos, outSize - pos, "Halves %s.", list);
    }
    if (sCount > 0) {
        JoinNameList(strongResist, sCount, ELEMENT_NAMES, list, sizeof(list));
        pos += snprintf(out + pos, outSize - pos, "%sStrongly resists %s.",
                        (pos > 0 ? " " : ""), list);
    }
    if (nCount > 0) {
        JoinNameList(nullifies, nCount, ELEMENT_NAMES, list, sizeof(list));
        pos += snprintf(out + pos, outSize - pos, "%sNullifies %s.",
                        (pos > 0 ? " " : ""), list);
    }
    if (aCount > 0) {
        JoinNameList(absorbs, aCount, ELEMENT_NAMES, list, sizeof(list));
        pos += snprintf(out + pos, outSize - pos, "%sAbsorbs %s.",
                        (pos > 0 ? " " : ""), list);
    }
}

// v0.14.74: Status resistances on key 9. Iterates the 13 offensive
// ailments (per OFFENSIVE_AILMENT_INDICES) and announces those whose
// statusRes byte >= 100 (the deep research's "Strong vs" threshold).
// Empty case: "No status resistances."
static void FormatStatusResistances(const ScanSnapshot& snap, char* out, int outSize)
{
    int matches[20];
    int count = 0;
    for (int i = 0; i < OFFENSIVE_AILMENT_COUNT; i++) {
        int idx = OFFENSIVE_AILMENT_INDICES[i];
        if (snap.statusRes[idx] >= 100) matches[count++] = idx;
    }
    if (count == 0) {
        snprintf(out, outSize, "No status resistances.");
        return;
    }
    char list[384];
    JoinNameList(matches, count, STATUS_NAMES_20, list, sizeof(list));
    snprintf(out, outSize, "Resists %s.", list);
}

// v0.14.74: Status weaknesses on key 0. Iterates the 13 offensive
// ailments and announces those whose statusRes byte == 0 (fully
// vulnerable per the FF Wiki StatusDefense formula). Empty case:
// "No status weaknesses." T-Rexaur is the canonical calibration
// anchor — should announce Death, Poison, Darkness, Sleep based on
// the standard FF8 data.
static void FormatStatusWeaknesses(const ScanSnapshot& snap, char* out, int outSize)
{
    int matches[20];
    int count = 0;
    for (int i = 0; i < OFFENSIVE_AILMENT_COUNT; i++) {
        int idx = OFFENSIVE_AILMENT_INDICES[i];
        if (snap.statusRes[idx] == 0) matches[count++] = idx;
    }
    if (count == 0) {
        snprintf(out, outSize, "No status weaknesses.");
        return;
    }
    char list[384];
    JoinNameList(matches, count, STATUS_NAMES_20, list, sizeof(list));
    snprintf(out, outSize, "Weak to %s.", list);
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

    // v0.14.70-diag: Per-scan reset of the type-label prefix tracking
    // buffer. v0.14.69 only reset on OnBattleEnter — if a prior scan in
    // the same battle left stale prefix bytes (e.g. last call was the
    // 'Fly' for the previous scan), the next scan's text_id=36 fire
    // could compose a wrong label using the stale bytes. By clearing
    // here we guarantee each scan starts with a clean buffer.
    // (CaptureSnapshot above already memset the slot's typeLabel via
    // its own memset(&snap, 0, sizeof(snap)) so we don't need to clear
    // that here.)
    memset(s_lastTypePrefixBytes, 0, sizeof(s_lastTypePrefixBytes));
    s_lastTypePrefixLen = 0;

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
    case 5:  // Offensive stats (v0.14.74; was full 8-stat readout in v0.14.65..v0.14.73.1)
        FormatOffensiveStats(snap, msg, sizeof(msg));
        break;
    case 6:  // Defensive stats (v0.14.74; was Weak in v0.14.73)
        FormatDefensiveStats(snap, msg, sizeof(msg));
        break;
    case 7:  // Elemental Resistances — combined halves/strong/nullify/absorb (v0.14.74)
        FormatResistances(snap, msg, sizeof(msg));
        break;
    case 8:  // Elemental Weaknesses (v0.14.74; was Nullify in v0.14.73)
        FormatWeak(snap, msg, sizeof(msg));
        break;
    case 9:  // Status Resistances — reverted to stub in v0.14.74.1
        // v0.14.74 shipped this as FormatStatusResistances reading from
        // entity+0x4C, but the BAT proved that offset is not the status
        // resistance block (Grat announced as resisting all 13 ailments
        // due to alternating 169/251 byte pattern at +0x4C). Reverting
        // to the stub so misleading status info doesn't ship to Aaron
        // during play. The [SCAN-STRUCT] diagnostic in CaptureSnapshot
        // dumps 121 bytes per scan; one BAT round across Grat /
        // T-Rexaur / Tonberry will reveal the actual offset, after
        // which v0.14.74.2 re-enables this case with the corrected
        // BENT_STATUS_RESIST_BASE.
        snprintf(msg, sizeof(msg), "Not implemented yet.");
        break;
    case 0:  // Status Weaknesses — reverted to stub in v0.14.74.1 (same reason)
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
    // v0.14.69: Reset the type-label prefix tracking buffer so a stale
    // prefix from a prior battle can't leak into this battle's first scan.
    memset(s_lastTypePrefixBytes, 0, sizeof(s_lastTypePrefixBytes));
    s_lastTypePrefixLen = 0;
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

// v0.14.70-diag: Returns the slot whose Scan event is currently in
// flight, covering BOTH the action-layer phase (s_pendingScanSlot >= 0,
// set by OnScanCast at action-commit) AND the visible Scan UI phase
// (s_scanScreenActiveSlot >= 0, set by OnScanPopupSpawn at fire #1).
// Returns -1 if no scan is in flight.
//
// The active slot takes priority over pending: in normal flow, fire #1
// consumes s_pendingScanSlot (clears to -1) and sets
// s_scanScreenActiveSlot in the same OnScanPopupSpawn call, so they're
// not both >= 0 simultaneously. But in the brief window between those
// two writes (single function, ~microseconds), reading the active slot
// first is safe.
//
// This widens the v0.14.69 capture gate. v0.14.69 only captured during
// the visible Scan UI phase (IsScreenActive()); for some monster types
// the engine renders the type label DURING the cast animation, before
// fire #1, so v0.14.69 silently skipped those bytes. The wider gate
// covers the entire action-layer-through-screen-close window so the
// engine's render-order timing doesn't matter.
static int GetScanFlightSlot()
{
    int active = (int)InterlockedCompareExchange(&s_scanScreenActiveSlot, -1, -1);
    if (active >= 0) return active;
    int pending = (int)InterlockedCompareExchange(&s_pendingScanSlot, -1, -1);
    return pending;
}

// v0.14.71: HandleBattleText — captures the Scan UI's type label
// (e.g. 'Fly Monster') by observing two consecutive sub_47EC70 calls
// during scan flight. See the s_lastTypePrefixBytes comment above for
// the capture mechanism.
//
// v0.14.72: Converted from a standalone MinHook callback
// (HookedGetBattleText) into a public function called from the victory
// module's existing hook on sub_47EC70 (HookedBtCandidate1 in
// battle_tts_victory.inl). The caller has already invoked the original
// engine function; we receive the textId and the returned char* and
// process them. See the architecture comment above the
// TYPE_LABEL_MONSTER_TEXT_ID constant for why the dual-hook design was
// abandoned.
//
// For monsters with a type label (Fly-type Bite Bug / Glacial Eye /
// Buel etc.), the engine fetches the type prefix string via
// sub_47EC70(prefixId) where prefixId varies per monster type (e.g.
// text_id=99 returns 'Fly'), then immediately fetches sub_47EC70(36)
// which returns 'Monster'. We snapshot the previous call's bytes and
// compose '{prefix} Monster' when text_id=36 fires.
//
// For monsters without a type label (Fastitocalon and many others),
// the engine never calls sub_47EC70(36) at all during the scan UI
// render — confirmed via v0.14.70-diag's BATTLE-TEXT-LITE log + visual
// screenshot proof. In that case we silently leave typeLabel empty and
// FormatLevel falls back to plain 'Level N.' on key 3, which correctly
// mirrors the on-screen UI.
//
// Gate (v0.14.70-diag onward): GetScanFlightSlot() covers BOTH the
// action-layer phase (s_pendingScanSlot >= 0) AND the visible Scan UI
// phase (s_scanScreenActiveSlot >= 0). Strict superset of v0.14.69's
// IsScreenActive()-only gate — robustness improvement against any
// future case where the engine might fetch the type label during cast
// animation. Outside scan flight this function is a passthrough
// no-op — critical because the victory hook calls us on every
// sub_47EC70 invocation (~hundreds per battle frame).

static int SnapshotPrefixBytesSafe(const char* result, uint8_t* outBuf, int outBufSize)
{
    if (!result || outBufSize <= 0) return 0;
    int copied = 0;
    __try {
        for (int i = 0; i < outBufSize; i++) {
            uint8_t bv = *(const uint8_t*)(result + i);
            outBuf[i] = bv;
            copied++;
            if (bv == 0) break;  // null terminator — stop
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        copied = 0;
    }
    return copied;
}

static void ComposeTypeLabelToBuf(const uint8_t* prefixBytes, int prefixLen,
                                  char* outBuf, int outBufSize)
{
    if (!outBuf || outBufSize <= 0) return;
    outBuf[0] = '\0';
    if (!prefixBytes || prefixLen <= 0) return;

    // FF8TextDecode::Decode handles the encoding rules (uppercase
    // encoded = decoded + 4, lowercase encoded = decoded - 2, 0x20 =
    // space, 0x02 = section separator, 0x00 = terminator).
    std::string prefix = FF8TextDecode::Decode(prefixBytes, prefixLen);
    if (prefix.empty()) return;

    // Trim any trailing nulls / whitespace the decoder might emit.
    while (!prefix.empty() &&
           (prefix.back() == ' ' || prefix.back() == '\t' ||
            prefix.back() == '\n' || prefix.back() == '\0')) {
        prefix.pop_back();
    }
    if (prefix.empty()) return;

    snprintf(outBuf, outBufSize, "%s Monster", prefix.c_str());
}

void HandleBattleText(int textId, const char* result)
{
    // Hot path: outside any scan flight, do nothing. The victory hook
    // calls us on every sub_47EC70 invocation (~hundreds per battle
    // frame), so this early-return matters for performance.
    int slot = GetScanFlightSlot();
    if (slot < 0 || result == nullptr) {
        return;
    }

    if ((uint32_t)textId == TYPE_LABEL_MONSTER_TEXT_ID) {
        // text_id=36 ('Monster' suffix) just fired during scan flight.
        // The previous call's bytes (held in s_lastTypePrefixBytes) are
        // the type prefix — decode and compose the full label. Single-
        // write per scan event (we don't overwrite if text_id=36 ever
        // refetches for some other UI element).
        if (slot >= 0 && slot < BATTLE_TOTAL_SLOTS &&
            s_lastTypePrefixLen > 0 &&
            s_scanCache[slot].typeLabel[0] == '\0') {
            char composed[64] = {};
            ComposeTypeLabelToBuf(s_lastTypePrefixBytes,
                                   s_lastTypePrefixLen,
                                   composed, sizeof(composed));
            if (composed[0] != '\0') {
                strncpy(s_scanCache[slot].typeLabel, composed,
                        sizeof(s_scanCache[slot].typeLabel) - 1);
                s_scanCache[slot].typeLabel[
                    sizeof(s_scanCache[slot].typeLabel) - 1] = '\0';
                Log::Battle("BattleTTS: [SCAN-TTS] Type label captured slot=%d typeLabel='%s'",
                            slot, s_scanCache[slot].typeLabel);
            }
        }
    } else {
        // Any other text_id during scan flight — snapshot its bytes as
        // a candidate prefix. The next text_id=36 will use whichever
        // bytes are most recent.
        s_lastTypePrefixLen = SnapshotPrefixBytesSafe(
            result, s_lastTypePrefixBytes, TYPE_PREFIX_MAX_BYTES);
    }
}

void Initialize()
{
    Log::Mod("[SCAN-TTS] Initialized (v0.14.72: sub_47EC70 forwarded from victory hook, sub_B687C0 owned here).");
    InstallScanGetTextHook();
    // v0.14.72: No InstallGetBattleTextHook() call — sub_47EC70 is now
    // owned by InstallBattleTextHooks() in battle_tts_victory.inl and
    // forwards into HandleBattleText. See architecture comment above
    // HandleBattleText for why the v0.14.68-diag dual-hook design was
    // abandoned.
    InterlockedExchange(&s_pendingScanSlot, -1);
    InterlockedExchange(&s_scanScreenActiveSlot, -1);
    InterlockedExchange(&s_scanHookFireCount, 0);
    memset(s_scanCache, 0, sizeof(s_scanCache));
}

}  // namespace ScanTTS

