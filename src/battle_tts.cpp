// battle_tts.cpp - Battle sequence TTS for blind players
//
// ============================================================================
// CURRENT STATE: See FF8OPC_VERSION in ff8_accessibility.h
// ============================================================================
//
// Phase 1 (v0.10.01-05): Skeleton, mode detection, enemy announcement
// Phase 2 (v0.10.06-10): Turn announcements + ATB tracking
// Phase 3 (v0.10.11-18): Command menu TTS (includes diagnostics)
// Phase 4 (v0.10.19-24): Target selection TTS
// Phase 5 (v0.10.25-32): HP + status tracking
// Phase 6 (v0.10.33-37): Battle results
// Phase 7 (v0.10.38-42): Draw system
// Phase 8 (v0.10.43-50): Events + limit breaks
//
// v0.14.24 build recovery: restored from GitHub HEAD v0.13.61 base, plus the
// v0.13.62-v0.14.x .inl additions wired into the include chain in dependency
// order. See DEVNOTES.md session 65 for the recovery narrative.

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cerrno>
#include <intrin.h>
#include <gdiplus.h>
#include <gl/GL.h>
#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "opengl32.lib")
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "battle_tts.h"
#include "ff8_item_names.h"
#include "minhook/include/MinHook.h"

// Forward declarations for namespaces used in .inl files
namespace Log { void Battle(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); bool SpeakChannel2(const char* text, bool interrupt = false); bool IsSpeaking(); }
namespace Config { void Load(); int GetInt(const char* key, int defaultValue); void SetInt(const char* key, int value); const char* GetPath(); }
// v0.14.44: GF summon AD trigger fired from PollBattleMagicId in battle_tts_ewm.inl.
namespace GfAudioDesc { void OnGFAnimationStart(int effectId); }
// v0.14.50: Scan spell TTS trigger fired from the same PollBattleMagicId. Scan
// has its own effect ID (39) but reuses the GF detection plumbing.
// v0.14.57: fromActionLayer parameter added — defaults to false; the
// magicId==39 polling path in ewm.inl and the popup path in noeffect.inl
// pass true to mark the call as the authoritative cast-time signal that
// owns the 30 s hook-suppression window.
// v0.14.59: Public API extended for the UX redesign — OnScanPopupSpawn /
// OnScanPopupDespawn fire from the [SPRITE-POLL] emitter in screenshot.inl;
// IsScreenActive / GetActiveSlot / SpeakField are consumed by the keyboard
// router in PollHPCheckKeys (battle_tts_hp.inl); OnBattleEnter resets the
// per-battle snapshot cache and screen state. Forward-declared HERE at
// file scope (BEFORE `namespace BattleTTS {` opens) so the declarations
// land in the GLOBAL `::ScanTTS` namespace where the linker can find
// scan_tts.cpp's definitions. Repeating any of these inside an .inl file
// would create `BattleTTS::ScanTTS::Foo` (a different symbol) and trigger
// LNK2019 — see the v0.14.55 BAT FAIL note in DEVNOTES.md.
namespace ScanTTS {
    void OnScanCast(int targetSlot, bool fromActionLayer = false);
    void OnScanPopupSpawn();
    void OnScanPopupDespawn();
    bool IsScreenActive();
    int  GetActiveSlot();
    void SpeakField(int fieldId);
    void OnBattleEnter();
}

namespace BattleTTS {

// ============================================================================
// Module state
// ============================================================================

static bool s_initialized = false;
static bool s_inBattle    = false;    // true while game mode == 999
static bool s_battleJustStarted = false;  // edge trigger: true for one frame on entry
static DWORD s_battleEntryTime  = 0;      // GetTickCount() when battle was entered

// The engine needs time to populate the entity array after mode transitions to 3.
// Mode 3 starts during the swirl animation, before entity data is ready.
// We enforce a 2s minimum delay, then poll until ally slot 0 maxHP > 0.
// Enemies may populate later than allies -- second-pass catches them.
static const DWORD BATTLE_INIT_MIN_DELAY_MS = 2000;   // minimum wait before checking (swirl animation)
static const DWORD BATTLE_INIT_TIMEOUT_MS   = 10000;  // max wait before giving up
static bool s_initAnnounceDone = false;
static bool s_enemyAnnounceDone = false;  // second-pass: announce enemies when they appear

// ============================================================================
// Speech priority system
// ============================================================================

enum SpeechPriority {
    PRIO_CRITICAL = 0,  // KO / Game Over -- always interrupt
    PRIO_TURN     = 1,  // "Squall's turn" / Limit Ready
    PRIO_MENU     = 2,  // Cursor navigation
    PRIO_ACTION   = 3,  // "Drew 3 Fire" / "Squall attacks!"
    PRIO_HP       = 4,  // Damage/heal amounts
    PRIO_STATUS   = 5,  // "Rinoa poisoned"
    PRIO_INFO     = 6,  // Battle log, misc
};

static int s_currentSpeakPriority = 99;  // higher = nothing speaking

// v0.10.30: Repeat buffer -- stores last non-menu speech for backtick repeat key
static char s_repeatBuffer[256] = {};     // last non-PRIO_MENU text spoken
static bool s_repeatKeyWasDown = false;   // edge detection for backtick key

// v0.10.32: BattleSpeak -- Channel 1 (menu/command voice)
static void BattleSpeak(const char* text, SpeechPriority prio, bool interrupt = false)
{
    if (!text || text[0] == '\0') return;

    if (interrupt || (int)prio <= s_currentSpeakPriority) {
        ScreenReader::Speak(text, interrupt || ((int)prio < s_currentSpeakPriority));
        s_currentSpeakPriority = (int)prio;
    } else {
        ScreenReader::Speak(text, false);
    }
}

// v0.10.32: BattleSpeakEvent -- Channel 2 (event/status voice)
static void BattleSpeakEvent(const char* text, bool interrupt = false)
{
    if (!text || text[0] == '\0') return;

    strncpy(s_repeatBuffer, text, sizeof(s_repeatBuffer) - 1);
    s_repeatBuffer[sizeof(s_repeatBuffer) - 1] = '\0';

    ScreenReader::SpeakChannel2(text, interrupt);
}

// ============================================================================
// Entity array reading helpers
// ============================================================================

static uint8_t* GetEntityBlock(int slot)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return nullptr;
    return (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
}

static uint32_t GetEntityHP(int slot)
{
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return 0;
    __try {
        if (slot < BATTLE_ALLY_SLOTS) {
            return (uint32_t)(*(uint16_t*)(blk + BENT_CUR_HP));
        } else {
            return *(uint32_t*)(blk + BENT_CUR_HP);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static uint32_t GetEntityMaxHP(int slot)
{
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return 0;
    __try {
        if (slot < BATTLE_ALLY_SLOTS) {
            return (uint32_t)(*(uint16_t*)(blk + BENT_MAX_HP));
        } else {
            return *(uint32_t*)(blk + BENT_MAX_HP);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool IsEntityKO(int slot)
{
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return true;
    __try {
        return (*(blk + BENT_PERSIST_STATUS) & 0x01) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

static int CountActiveEnemies()
{
    int count = 0;
    for (int i = BATTLE_ALLY_SLOTS; i < BATTLE_TOTAL_SLOTS; i++) {
        if (GetEntityHP(i) > 0) count++;
    }
    return count;
}

// ============================================================================
// Cross-.inl forward declarations (v0.14.24 build recovery)
// ============================================================================
// These functions are defined in later .inl files but called from earlier ones.
// Forward declaring them here keeps the existing include order intact.

// Defined in battle_tts_validate.inl. Called from hp.inl, sprite.inl,
// battle_status.inl, noeffect.inl.
static void Validate_AnnounceEvent(const char* kind,
                                    int slot,
                                    int claimedValue,
                                    const char* claimedText,
                                    const char* trigger);

// Defined in battle_tts_noeffect.inl. Called from sprite.inl.
static void NoEffect_QueueAnnouncement(int slot, int value,
                                        const char* text,
                                        const char* validateKind);

// Defined in battle_tts_noeffect.inl. Called from sprite_spawn.inl.
static void NoEffect_RecordSnapshot(uint32_t targetMask);


// --- Enemy names, text decoder, entity helpers (extracted v0.12.18) ---
#include "battle_tts_helpers.inl"

// --- Menu diagnostic, cursor hunter, limit toggle, enemy cache (extracted v0.12.18) ---
#include "battle_tts_diagnostics.inl"

// --- HP tracking, damage, target selection, HP check (extracted v0.12.18) ---
#include "battle_tts_hp.inl"

// --- EWM, GF fire prevention, ATB hook, FFNx hook (extracted v0.12.18) ---
#include "battle_tts_ewm.inl"

// --- Turn/command menu, magic/GF/item/draw sub-menus (extracted v0.12.18) ---
#include "battle_tts_menu.inl"

// --- Action announcements via sprite data (added between menu and noeffect) ---
#include "battle_tts_sprite.inl"

// --- Status ailment / buff transition TTS (v0.13.62-63; needs hp.inl + menu.inl) ---
#include "battle_status.inl"

// --- "No effect!" detection (v0.13.83; needs sprite.inl + battle_status.inl) ---
#include "battle_tts_noeffect.inl"

// --- Popup table polling, SpriteRec, KIND4_SCREENSHOT_DIR, DrainDeferredTextSpriteLog ---
#include "battle_tts_sprite_spawn.inl"

// --- TTS announcement breadcrumb logging (defines Validate_AnnounceEvent) ---
#include "battle_tts_validate.inl"

// --- Hardware write BP on damage display (v0.14.2) ---
#include "battle_tts_dmgbp.inl"

// --- MinHook on sub_48EF80 popup creator (v0.14.4) ---
#include "battle_tts_dmg_popup_hook.inl"

// --- Hardware read BP on damage display (v0.14.6) ---
#include "battle_tts_dmg_read_bp.inl"

// --- MinHook on sub_5068B0 impact-time renderer (v0.14.8) ---
#include "battle_tts_dmg_render_hook.inl"

// --- Per-frame slot-pool + anim-flag-region poll (v0.14.0) ---
#include "battle_tts_spritepool.inl"

// --- ROI calibration auto-capture (v0.13.95; defines RoiCalib_OnSwapBuffers) ---
#include "battle_tts_roi_calib.inl"

// ============================================================================
// Shared victory state -- used by both screenshot.inl and victory.inl
// ============================================================================

// v0.14.45: F12 victory step-capture diagnostic state removed. The shared
// section previously held s_victoryDumpDone, s_victoryScreenActive,
// s_victoryEntryTime, s_victoryStepCount, s_victoryF12WasDown — all of which
// were only used by the F12 step capture path that has been retired.
static uint16_t s_prevGameMode = 0;

// v0.13.36: Pre-battle GF struct snapshots for FindChangedGF fallback.
static uint8_t s_preBattleGFStructs[16][0x44] = {};
static bool s_preBattleGFSnapValid = false;

// Known victory data addresses
static const uint32_t VICTORY_EXP_BASE = 0x01CFF574;       // 3x u16: EXP earned per party slot
static const uint32_t VICTORY_AP_BASE  = 0x01CFF5C2;       // 3x u16: AP earned per party slot
static const uint32_t VICTORY_PARTY_ADDR = 0x01CFE74C;     // 4 bytes: party composition (char IDs)

// CHAR_NAMES[] already defined in battle_tts_menu.inl
static const char* GetCharNameById(uint8_t id)
{
    if (id < 8) return CHAR_NAMES[id];
    if (id == 8) return "Laguna";
    if (id == 9) return "Kiros";
    if (id == 10) return "Ward";
    return "Unknown";
}

static int s_victoryAutoCapture = 0;

// v0.13.28: Pre-battle EXP snapshots for level-up detection
static uint32_t s_preBattleExpAll[11] = {};
static bool s_preBattleExpSnapValid = false;

// v0.13.45: GDI+ token shared between Initialize/Shutdown (here) and the PNG
// encoding paths in screenshot.inl / roi_calib.inl. Lives in this shared
// section so all consumers see the same value regardless of include order.
static ULONG_PTR s_gdiplusToken = 0;

// --- GL screenshot capture, memory diff, victory step diagnostics (extracted v0.13.45) ---
#include "battle_tts_screenshot.inl"

// --- Victory TTS: hooks, phase detection, GF/ability tables, thread (extracted v0.13.45) ---
#include "battle_tts_victory.inl"

// ============================================================================
// Battle enter/exit
// ============================================================================

static void OnBattleEnter()
{
    s_inBattle = true;
    s_battleJustStarted = true;
    s_battleEntryTime = GetTickCount();
    s_initAnnounceDone = false;
    s_enemyAnnounceDone = false;
    s_currentSpeakPriority = 99;
    
    // Reset menu diagnostic state
    s_menuSnapValid = false;
    s_menuSnap2Valid = false;
    s_lastMenuDiagTick = 0;
    s_lastActiveCharId = 0xFF;
    s_lastNewActiveCharId = 0xFF;
    s_lastMenuPhase = 0xFF;
    s_lastDiagCmdCursor = 0xFF;
    memset(s_menuSnap, 0, sizeof(s_menuSnap));
    memset(s_menuSnap2, 0, sizeof(s_menuSnap2));
    s_huntSnapValid = false;
    s_lastHuntTick = 0;
    memset(s_huntSnap, 0, sizeof(s_huntSnap));
    
    // Reset turn/command tracking
    s_turnActiveCharId = 0xFF;
    s_turnCmdCursor = 0xFF;
    memset(s_turnCharCommands, 0, sizeof(s_turnCharCommands));
    
    // Reset sub-menu state
    s_inSubmenu = false;
    s_turnSubmenuCursor = 0xFF;
    s_submenuCommandId = 0;
    s_magicListBuilt = false;
    s_turnMagicCount = 0;
    s_gfListBuilt = false;
    s_turnGFCount = 0;
    s_itemListBuilt = false;
    s_turnItemCount = 0;
    s_drawListBuilt = false;
    s_turnDrawCount = 0;
    s_drawTargetSlot = -1;
    s_drawCursorPrev = 0xFF;
    s_drawStockCastPrev = 0xFF;
    s_lastDrawerPartySlot = 0xFF;
    s_drawLastMenuPhase = 0xFF;
    s_pendingSubmenuEntry = false;
    s_pendingSubmenuTick = 0;
    s_submenuDebouncing = false;
    s_submenuDebounceTick = 0;
    
    // Reset Limit Break state
    s_limitBreakActive = false;
    s_lastLimitToggle = 0;
    
    // v0.12.46: Reset GF HP substitution tracking
    memset(s_gfHpSubstitutionActive, 0, sizeof(s_gfHpSubstitutionActive));
    
    // Reset repeat buffer for new battle
    s_repeatBuffer[0] = '\0';
    s_repeatKeyWasDown = false;

    // Reset HP check key states
    s_hpKey1WasDown = false;
    s_hpKey2WasDown = false;
    s_hpKey3WasDown = false;
    s_hpKey4WasDown = false;
    s_hpKey5WasDown = false;
    s_hpKey6WasDown = false;
    s_hpKey7WasDown = false;
    s_hpKey8WasDown = false;
    s_hpKey9WasDown = false;
    s_hpKey0WasDown = false;
    s_hpKeyHWasDown = false;

    // Reset enemy name cache for new battle
    s_enemyNameCacheBuilt = false;
    memset(s_enemyNameCache, 0, sizeof(s_enemyNameCache));

    // Reset HP tracking for new battle
    s_hpTrackingReady = false;
    memset(s_hpPrev, 0, sizeof(s_hpPrev));
    memset(s_hpMaxPrev, 0, sizeof(s_hpMaxPrev));
    memset(s_hpAccumDelta, 0, sizeof(s_hpAccumDelta));
    memset(s_hpAccumPending, 0, sizeof(s_hpAccumPending));
    s_anyHpPending = false;
    s_hpFirstPendingTime = 0;
    s_damageAnimWasActive = false;
    s_damageAnimStartTime = 0;
    s_hpTrackLastActiveChar = 0xFF;

    // v0.14.32: Reset v0.14.x damage-popup signal state (popup-create + impact-render).
    // Both pairs were inadvertently dropped from OnBattleEnter during the v0.14.24
    // build recovery: the recovery rebuilt battle_tts.cpp from GitHub HEAD v0.13.61
    // (which predates these hooks), wired the new .inl files into the include chain,
    // but never called the install/reset functions. Without these calls,
    // s_lastDmgRenderTick stays 0, the impact-time render trigger never fires, and
    // PollHPChanges falls back to the v0.13.90 anim-flag-fall path — which is the
    // OLD pre-v0.14.10 "announce at animation end" timing. Restoring these calls
    // re-enables the impact-time trigger documented in battle_tts_dmg_render_hook.inl.
    DmgPopupHook_Reset();
    DmgRenderHook_Reset();

    // v0.14.33: Reset no-effect watchdog state and hook counters. Same
    // regression class as v0.14.32: the v0.14.24 build recovery wired
    // battle_tts_noeffect.inl into the include chain but never called its
    // ResetNoEffectState() in OnBattleEnter, and the sub_48E830 hook it
    // depends on was an empty stub in sprite_spawn.inl after the v0.13.93
    // architectural pivot. Without the reset, stale s_pendingSpellNoEffect
    // state from a prior battle could fire spurious 'No effect on X' lines
    // in the new battle.
    ResetNoEffectState();
    Sub48E830Hook_Reset();

    // v0.14.34: Reset battle_tts_sprite.inl per-battle dedup state. Same
    // regression class — the v0.14.24 build recovery wired this .inl into
    // the include chain but never called ResetSpriteSpawnState() in
    // OnBattleEnter. Without the reset, stale (slot, text_id) and
    // (slot, kind) dedup entries from prior battles silenced legitimate
    // events in the new battle. Reset clears all four dedup tables
    // (sub_483400, sub_4877F0, sub_48D200, kind=4 screenshot state).
    ResetSpriteSpawnState();

    // v0.14.59: Reset Scan snapshot cache + screen-state flags. Cache
    // entries from a previous battle are stale (different entity-array
    // contents) and would speak garbage if the player pressed a number
    // key during a Scan window in the new battle.
    ::ScanTTS::OnBattleEnter();
    
    // Reset EWM cap state for new battle
    s_ewmFreezing = false;
    s_ewmShouldCap = false;
    s_ewmCapExcludeSlot = 0xFF;
    s_ewmCapGF = false;
    s_ewmLastActiveChar = 0xFF;
    s_ewmNewTurnGrace = false;
    s_gfSnapValid = false;
    s_gfSnapLastTick = 0;
    s_gfHookLastLogTick = 0;
    s_gfState68Clamped = false;
    s_gfSavedState68 = 0xFF;
    memset(s_gfMaxInflated, 0, sizeof(s_gfMaxInflated));
    memset(s_gfRealMax, 0, sizeof(s_gfRealMax));
    s_gfFlagHidden = false;
    s_gfSavedSlot = 0xFF;
    s_gfStickyHidden = false;
    s_gfAutoArmLastActive = 0;
    s_gfAutoArmDone = false;
    s_gfScanValid = false;
    s_gfScanLogCount = 0;
    memset(s_gfScanSnap, 0, sizeof(s_gfScanSnap));
    s_tgtDiagStage = 0;
    s_lastTargetBitmask = 0;
    s_lastTargetScope = 0;
    s_inTargetSelect = false;
    s_targetLastAnnounceTick = 0;
    if (s_gfFirePatched && s_gfFirePatchReady) {
        *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_FIRE_VALUE;
        s_gfFirePatched = false;
    }
    EWM_LoadConfig();
    
    if (!s_pBattleMenuState) {
        ResolveBattleMenuAddresses();
    }

    if (!s_ffnxGFHookInstalled) {
        EWM_InstallFFNxGFHook();
        Log::Battle("BattleTTS: [FFNx-GF] Deferred install result: %s", s_ffnxGFHookInstalled ? "OK" : "FAIL");
    }

    if (!s_battleEffectHookInstalled) {
        EWM_InstallBattleEffectHook();
    }

    // v0.14.32: Install the v0.14.x damage-popup hooks. Each install function
    // self-guards on its own *Installed flag so calling every battle is safe.
    // The popup-create hook (sub_48EF80) is currently diagnostic-only — its
    // signal is not wired into PollHPChanges as of v0.14.10 (see comment in
    // hp.inl about why v0.14.4 reverted it). The render hook (sub_5068B0) IS
    // the production impact-time trigger. Both are still installed because the
    // diagnostic from the popup-create hook helps us cross-reference timing.
    DmgPopupHook_Install();
    DmgRenderHook_Install();

    // v0.14.33: Install the sub_48E830 action-announce hook. Required by
    // battle_tts_noeffect.inl to start its no-effect watchdog when the
    // player casts a status spell (retaddr=0x0048594E, actionId=0x16).
    // Self-guards on s_sub48E830HookInstalled so calling every battle is safe.
    InstallSub48E830Hook();

    // v0.14.34: Install the three battle_tts_sprite.inl event hooks —
    // critical for spell-miss / no-effect / Miss announcements:
    //
    //   sub_483400 (InstallSpriteSpawnHook)   — item event sprite spawner;
    //                                            diagnostic only as of v0.13.66.
    //   sub_4877F0 (InstallSpellResultHook)   — spell result dispatcher;
    //                                            kind=4 a3=0x9 path fires
    //                                            'No effect on X' via
    //                                            NoEffect_QueueAnnouncement.
    //                                            THIS is the missing piece
    //                                            for status-spell miss/no-effect.
    //   sub_48D200 (InstallPopupSpriteHook)   — central popup dispatcher;
    //                                            text_id=0xED fires 'Miss on X'
    //                                            for physical attack misses.
    //
    // Same regression class — the v0.14.24 build recovery never re-added
    // these install calls. Each Install function self-guards on its own
    // *Installed flag so calling every battle is safe.
    InstallSpriteSpawnHook();
    InstallSpellResultHook();
    InstallPopupSpriteHook();
    
    memset((void*)s_gfAnimFired, 0, sizeof(s_gfAnimFired));
    s_prevBattleMagicId = -1;

    // v0.14.45: F12 victory diagnostic state resets removed.
    ResetVictoryTTS();

    // Snapshot savemap EXP for all 11 characters at battle entry
    s_preBattleExpSnapValid = false;
    __try {
        for (int c = 0; c < 11; c++) {
            uint8_t* ch = (uint8_t*)(0x1CFE0E8 + c * 0x98);
            s_preBattleExpAll[c] = *(uint32_t*)(ch + 0x04);
        }
        s_preBattleExpSnapValid = true;
        Log::Battle("BattleTTS: [VICTORY-SNAP] Pre-battle EXP snapshot: [%u,%u,%u,%u,%u,%u]",
                   s_preBattleExpAll[0], s_preBattleExpAll[1], s_preBattleExpAll[2],
                   s_preBattleExpAll[3], s_preBattleExpAll[4], s_preBattleExpAll[5]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-SNAP] Pre-battle EXP snapshot FAILED");
    }

    // Snapshot all 16 GF structs for level-up/ability identification
    s_preBattleGFSnapValid = false;
    __try {
        for (int g = 0; g < 16; g++) {
            memcpy(s_preBattleGFStructs[g],
                   (void*)(SAVEMAP_GF_BASE + g * SAVEMAP_GF_STRIDE),
                   SAVEMAP_GF_STRIDE);
        }
        s_preBattleGFSnapValid = true;
        for (int g = 0; g < 16; g++) {
            if (s_preBattleGFStructs[g][0x11]) {
                uint32_t gfExp = *(uint32_t*)(s_preBattleGFStructs[g] + 0x0C);
                uint8_t learnIdx = s_preBattleGFStructs[g][0x41];
                Log::Battle("BattleTTS: [VICTORY-SNAP] GF%d: EXP=%u learnIdx=%u", g, gfExp, learnIdx);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-SNAP] Pre-battle GF snapshot FAILED");
    }

    // Install battle text hooks on first battle entry
    if (!s_battleTextHooksInstalled) {
        InstallBattleTextHooks();
    }
    InterlockedExchange(&s_btCount1, 0);
    InterlockedExchange(&s_btCount2, 0);
    InterlockedExchange(&s_btCount3, 0);
    InterlockedExchange(&s_btCount4, 0);

    Log::Battle("BattleTTS: === BATTLE ENTERED === (encounter ID: %u)",
               (unsigned)(*(uint16_t*)BATTLE_ENCOUNTER_ID));
}

static void OnBattleExit()
{
    Log::Battle("BattleTTS: === BATTLE EXITED ===");
    
    s_inBattle = false;
    s_battleJustStarted = false;
    s_initAnnounceDone = false;
}

// ============================================================================
// Battle start announcement
// ============================================================================

static void AnnounceBattleStart()
{
    if (s_initAnnounceDone) return;

    DWORD elapsed = GetTickCount() - s_battleEntryTime;
    if (elapsed < BATTLE_INIT_MIN_DELAY_MS) return;
    
    __try {
        uint16_t allyMaxHP = *(uint16_t*)(BATTLE_ENTITY_ARRAY_BASE + BENT_MAX_HP);
        if (allyMaxHP == 0) {
            if (elapsed < BATTLE_INIT_TIMEOUT_MS) return;
            Log::Battle("BattleTTS: Entity array not populated after %ums", BATTLE_INIT_TIMEOUT_MS);
        } else {
            Log::Battle("BattleTTS: Entity array ready after %ums (ally0 maxHP=%u)",
                       elapsed, (unsigned)allyMaxHP);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: Exception polling entity array");
    }

    s_initAnnounceDone = true;

    for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
        uint32_t hp = GetEntityHP(i);
        uint32_t maxHp = GetEntityMaxHP(i);
        uint8_t* blk = GetEntityBlock(i);
        uint8_t lvl = 0, sts = 0;
        if (blk) { __try { lvl = *(blk + BENT_LEVEL); sts = *(blk + BENT_PERSIST_STATUS); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
        Log::Battle("BattleTTS: slot%d %s HP=%u/%u Lv=%u status=0x%02X",
                   i, (i < BATTLE_ALLY_SLOTS) ? "ALLY" : "ENEMY",
                   hp, maxHp, (unsigned)lvl, (unsigned)sts);
    }

    int enemyCount = CountActiveEnemies();

    char buf[256];
    if (enemyCount == 0) {
        snprintf(buf, sizeof(buf), "Battle!");
    } else {
        char enemyStr[200];
        BuildEnemyNameString(enemyStr, sizeof(enemyStr));
        snprintf(buf, sizeof(buf), "Battle! %s.", enemyStr);
        s_enemyAnnounceDone = true;
        if (!s_enemyNameCacheBuilt) BuildEnemyNameCache();
    }

    BattleSpeakEvent(buf);
    Log::Battle("BattleTTS: %s", buf);
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;

    s_inBattle = false;
    s_battleJustStarted = false;
    s_initAnnounceDone = false;
    s_enemyAnnounceDone = false;

    s_initialized = true;
    EWM_LoadConfig();

    {
        DWORD oldProtect = 0;
        BOOL ok = VirtualProtect((LPVOID)GF_FIRE_PATCH_ADDR, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
        s_gfFirePatchReady = (ok != FALSE);
        if (s_gfFirePatchReady) {
            uint8_t curByte = *(uint8_t*)GF_FIRE_PATCH_ADDR;
            if (curByte != GF_FIRE_VALUE) {
                Log::Battle("BattleTTS: [GF-PATCH] WARNING: byte at 0x%08X is 0x%02X, expected 0x%02X",
                           GF_FIRE_PATCH_ADDR, (unsigned)curByte, (unsigned)GF_FIRE_VALUE);
                s_gfFirePatchReady = false;
            } else {
                Log::Battle("BattleTTS: [GF-PATCH] Code page writable, fire byte verified at 0x%08X",
                           GF_FIRE_PATCH_ADDR);
            }
        } else {
            Log::Battle("BattleTTS: [GF-PATCH] VirtualProtect FAILED (err=%u)", GetLastError());
        }
    }
    
    EWM_InstallHook();
    EWM_InstallGFHook();

    s_gfVEHHandle = AddVectoredExceptionHandler(1, GF_BP_VectoredHandler);
    Log::Battle("BattleTTS: [GF-BP] VEH registered: handle=0x%08X", (uint32_t)(uintptr_t)s_gfVEHHandle);

    Log::Battle("BattleTTS: Initialized v%s (EWM=%s, ATB=%s, GF=%s, FFNx=%s, PATCH=%s, BT=%s).",
               FF8OPC_VERSION,
               s_ewmEnabled ? "ON" : "OFF",
               s_ewmHookInstalled ? "OK" : "FAIL",
               s_gfTimerHookInstalled ? "OK" : "FAIL",
               s_ffnxGFHookInstalled ? "OK" : "FAIL",
               s_gfFirePatchReady ? "OK" : "FAIL",
               s_battleTextHooksInstalled ? "OK" : "deferred");

    // Initialize GDI+ for PNG screenshots
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, NULL);
    Log::Battle("BattleTTS: [GDI+] Initialized (token=%lu)", (unsigned long)s_gdiplusToken);

    // Hook SwapBuffers for OpenGL screenshot capture
    InstallSwapBuffersHook();

    // Start victory screen monitor thread
    s_victoryThreadStop = false;
    s_victoryThread = CreateThread(NULL, 0, VictoryScreenThreadFunc, NULL, 0, NULL);
    Log::Battle("BattleTTS: [VICTORY-THREAD] Created: handle=0x%08X",
               (uint32_t)(uintptr_t)s_victoryThread);
}

void Update()
{
    if (!s_initialized) return;
    if (!FF8Addresses::pGameMode) return;

    EWM_PollToggle();

    uint16_t mode = *FF8Addresses::pGameMode;
    bool isBattle = (mode == 3);

    s_prevGameMode = mode;

    if (isBattle && !s_inBattle) {
        OnBattleEnter();
    } else if (!isBattle && s_inBattle) {
        OnBattleExit();
    }

    if (!s_inBattle) return;

    if (s_battleJustStarted) {
        s_battleJustStarted = false;
    }

    if (!s_initAnnounceDone) {
        AnnounceBattleStart();
    }

    if (s_initAnnounceDone && !s_enemyAnnounceDone) {
        int enemyCount = CountActiveEnemies();
        if (enemyCount > 0) {
            s_enemyAnnounceDone = true;
            if (!s_enemyNameCacheBuilt) BuildEnemyNameCache();
            char enemyStr[200];
            BuildEnemyNameString(enemyStr, sizeof(enemyStr));
            char buf[256];
            snprintf(buf, sizeof(buf), "%s.", enemyStr);
            BattleSpeakEvent(buf, false);
            Log::Battle("BattleTTS: [second-pass] %s (enemies appeared after initial announce)", buf);
            for (int i = BATTLE_ALLY_SLOTS; i < BATTLE_TOTAL_SLOTS; i++) {
                char name[64];
                GetEnemyName(i, name, sizeof(name));
                uint32_t hp = GetEntityHP(i);
                if (hp > 0) {
                    uint32_t maxHp = GetEntityMaxHP(i);
                    uint8_t* blk = GetEntityBlock(i);
                    uint8_t lvl = 0;
                    if (blk) { __try { lvl = *(blk + BENT_LEVEL); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
                    Log::Battle("BattleTTS: Enemy slot %d \"%s\": HP %u/%u Lv=%u", i, name, hp, maxHp, (unsigned)lvl);
                }
            }
        } else if (GetTickCount() - s_battleEntryTime > BATTLE_INIT_TIMEOUT_MS) {
            s_enemyAnnounceDone = true;
            Log::Battle("BattleTTS: No enemies detected after %ums timeout", BATTLE_INIT_TIMEOUT_MS);
        }
    }

    // v0.13.46: Mid-battle enemy detection (e.g. Elvoret after Biggs/Wedge die)
    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        RefreshEnemyNameCache();
    }

    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollMenuDiagnostic();
    }

    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollTurnAndCommands();
    }

    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollCursorHunter();
    }

    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollLimitToggle();
    }

    // v0.14.x: Limit Break submenu diagnostic (Quistis Blue Magic spell list)
    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollLimitDiag();
    }

    if (s_inBattle && s_initAnnounceDone) {
        EWM_UpdateBattle();
    }
    if (s_inBattle && s_ewmCapGF) {
        __try {
            for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
                uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
                uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
                if (gfFlag != 0) {
                    uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                    uint16_t* pMax = (uint16_t*)(cs + 0x16);
                    uint16_t curMax = *pMax;
                    if (curMax != 0xFFFF && curMax > 0) {
                        if (!s_gfMaxInflated[gs]) {
                            s_gfRealMax[gs] = curMax;
                            s_gfMaxInflated[gs] = true;
                        }
                        *pMax = 0xFFFF;
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (s_inBattle) {
        GF_LogHookStats();
        GF_PollStateChanges();
        PollBattleMagicId();
    }

    if (s_inBattle) {
        GF_BP_AutoArm();
    }

    // v0.14.45: F12 victory step-capture polling removed (diagnostic complete).

    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollHPChanges();
    }

    // v0.13.62-63: Status ailment/buff transition TTS (must come after PollHPChanges)
    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollStatusChanges();
    }

    // v0.13.83: No-effect detection watchdog. Per noeffect.inl comment, this
    // must run AFTER PollHPChanges and PollStatus so any transient signals
    // they observe are visible to the watchdog this frame.
    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollPendingSpellNoEffect();
        PollPendingNoEffectAnnouncements();
    }

    // v0.14.34: kind=4 auto-screenshot capture. Diagnostic subsystem from
    // v0.13.69 — schedules a screenshot ~400ms after each kind=4 event
    // (HookedSpellResultDispatch in battle_tts_sprite.inl) so we can visually
    // verify what the engine renders for resist/no-effect cases. Capped at
    // 10 captures per battle. Was missing from Update() since the v0.14.24
    // build recovery; restoring it lets us audit the new sub_4877F0 hook's
    // a3 patterns visually.
    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollKind4Capture();
    }

    if (s_inBattle && s_initAnnounceDone) {
        PollHPCheckKeys();
    }

    if (s_inBattle) {
        bool backtickDown = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0;
        bool backtickPressed = backtickDown && !s_repeatKeyWasDown;
        s_repeatKeyWasDown = backtickDown;
        if (backtickPressed && s_repeatBuffer[0] != '\0') {
            ScreenReader::SpeakChannel2(s_repeatBuffer, true);
            Log::Battle("BattleTTS: [REPEAT] '%s'", s_repeatBuffer);
        }
    }
    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollTargetSelection();
    }
}

const char* GetLastDrawerName()
{
    if (s_lastValidatedDrawSlot < BATTLE_ALLY_SLOTS)
        return GetBattleCharName(s_lastValidatedDrawSlot);
    return nullptr;
}

uint8_t GetDrawExecutingSlot()
{
    uint8_t execSlot = 0xFF;
    __try { execSlot = *(uint8_t*)DRAW_EXEC_SLOT_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return execSlot;
}

void ValidateDrawCharacter(uint8_t claimedSlot)
{
    s_lastValidatedDrawSlot = DiffMagicInventories(claimedSlot);
}

// v0.14.51: Public wrapper around the noeffect.inl static helper. Allows
// other modules (currently ScanTTS) to suppress the spurious 'No effect
// on <target>' watchdog announcement after they've produced their own
// authoritative TTS for the same target.
void CancelNoEffectWatchdogForSlot(int slot)
{
    NoEffect_CancelForSlot(slot);
}

// v0.14.65: Public non-blocking wrapper around the screenshot.inl static
// flag pair (s_captureBasePath + s_captureRequested). The internal
// CaptureScreenshot() in screenshot.inl polls for up to 160 ms via Sleep,
// which would freeze the game thread if called from a MinHook callback.
// This variant just sets the flag and returns; the next SwapBuffers tick
// (within ~16 ms at 60 fps) picks it up and writes the .bmp/.png pair.
// Aaron uploads the PNG to validate against in-memory state we just read.
void RequestScreenshotAsync(const char* basePath, int frameDelay)
{
    if (!basePath || !basePath[0]) return;
    strncpy(s_captureBasePath, basePath, sizeof(s_captureBasePath) - 1);
    s_captureBasePath[sizeof(s_captureBasePath) - 1] = '\0';
    // v0.14.65.3: capture deferred by frameDelay swap frames. 0 = next swap
    // (preserves v0.14.65 behavior). HookedSwapBuffers decrements this each
    // call and only fires DoGLCapture when it reaches 0.
    s_captureFrameDelay = frameDelay < 0 ? 0 : frameDelay;
    s_captureRequested = true;
    // Caller does NOT wait — HookedSwapBuffers picks up the flag on the
    // next frame (or after frameDelay frames) and runs DoGLCapture()
    // inline (GL context current). If multiple requests stack up between
    // SwapBuffers calls, the last one wins (s_captureBasePath +
    // s_captureFrameDelay both get overwritten); that's fine for our
    // throughput needs.
}

// v0.14.65.2: Public accessor for KIND4_SCREENSHOT_DIR. The constant lives
// inside battle_tts_sprite.inl as a file-static, so it's only visible
// within this translation unit (battle_tts.cpp + its included .inl files).
// Other compilation units (scan_tts.cpp, etc.) need this accessor to
// compose absolute paths that land in the same diagnostic directory as
// the kind4_*, poll_NEW_*, popup_time_* captures. Without this, modules
// using relative paths like 'Screenshots\\foo.png' resolve against
// FF8.exe's CWD (the Steam install dir), scattering captures outside the
// project tree. v0.14.65.1 hit exactly this issue — the scan_*.png file
// landed in the FF8 install dir's Screenshots folder, where Claude has
// no read access.
const char* GetScreenshotDir()
{
    return KIND4_SCREENSHOT_DIR;
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_victoryThread) {
        s_victoryThreadStop = true;
        WaitForSingleObject(s_victoryThread, 2000);
        CloseHandle(s_victoryThread);
        s_victoryThread = NULL;
    }
    if (s_gdiplusToken) {
        Gdiplus::GdiplusShutdown(s_gdiplusToken);
        s_gdiplusToken = 0;
    }
    if (s_gfFirePatched && s_gfFirePatchReady) {
        *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_FIRE_VALUE;
        s_gfFirePatched = false;
    }
    if (s_gfVEHHandle) {
        RemoveVectoredExceptionHandler(s_gfVEHHandle);
        s_gfVEHHandle = nullptr;
    }
    s_initialized = false;
    s_inBattle = false;
    Log::Battle("BattleTTS: Shutdown.");
}

}  // namespace BattleTTS
