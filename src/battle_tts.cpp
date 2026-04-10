// battle_tts.cpp - Battle sequence TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.12.25 — V key version announce
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
// See: Plan & Research Documents/Battle TTS implementation plan.md
//      Plan & Research Documents/Battle system memory map deep research results.md

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "battle_tts.h"
#include "ff8_item_names.h"
#include "minhook/include/MinHook.h"

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
// Enemies may populate later than allies — second-pass catches them.
static const DWORD BATTLE_INIT_MIN_DELAY_MS = 2000;   // minimum wait before checking (swirl animation)
static const DWORD BATTLE_INIT_TIMEOUT_MS   = 10000;  // max wait before giving up
static bool s_initAnnounceDone = false;
static bool s_enemyAnnounceDone = false;  // second-pass: announce enemies when they appear

// ============================================================================
// Speech priority system
// ============================================================================

enum SpeechPriority {
    PRIO_CRITICAL = 0,  // KO / Game Over — always interrupt
    PRIO_TURN     = 1,  // "Squall's turn" / Limit Ready
    PRIO_MENU     = 2,  // Cursor navigation
    PRIO_ACTION   = 3,  // "Drew 3 Fire" / "Squall attacks!"
    PRIO_HP       = 4,  // Damage/heal amounts
    PRIO_STATUS   = 5,  // "Rinoa poisoned"
    PRIO_INFO     = 6,  // Battle log, misc
};

static int s_currentSpeakPriority = 99;  // higher = nothing speaking

// v0.10.30: Repeat buffer — stores last non-menu speech for backtick repeat key
static char s_repeatBuffer[256] = {};     // last non-PRIO_MENU text spoken
static bool s_repeatKeyWasDown = false;   // edge detection for backtick key

// v0.10.32: BattleSpeak — Channel 1 (menu/command voice)
// Uses the main ScreenReader::Speak with priority-based interruption.
// v0.10.45: Channel 1 is for turn identification + command menu navigation:
//   - Turn announcements ("Squall's turn. Attack.") — interrupt=true
//   - Command cursor movement (Attack/Magic/GF/Draw)
//   - Sub-menu cursor navigation (spell list, etc.)
//   - Limit Break toggle
// All battle EVENTS go through Channel 2 (BattleSpeakEvent).
static void BattleSpeak(const char* text, SpeechPriority prio, bool interrupt = false)
{
    if (!text || text[0] == '\0') return;

    // If interrupt requested or new speech has higher (lower number) priority, cancel current
    if (interrupt || (int)prio <= s_currentSpeakPriority) {
        ScreenReader::Speak(text, interrupt || ((int)prio < s_currentSpeakPriority));
        s_currentSpeakPriority = (int)prio;
    } else {
        // Queue: just speak without interrupting
        ScreenReader::Speak(text, false);
    }
}

// v0.10.32: BattleSpeakEvent — Channel 2 (event/status voice)
// Uses ScreenReader::SpeakChannel2 (independent SAPI ISpVoice instance).
// v0.10.43: Each voice has its own SpMMAudioOut, enabling true simultaneous audio.
// v0.10.47: Channel 2 carries all battle EVENTS (not turn ID or menu nav):
//   - Battle start ("Battle! 2 Bite Bugs.")
//   - Damage/healing ("Bite Bug takes 52 damage.")
//   - HP check keys (1/2/3/H)
// ALL events queue (interrupt=false) so nothing cuts off anything else.
// Only exception: backtick repeat key uses interrupt=true (user-initiated).
// Also stores text in the repeat buffer for backtick.
static void BattleSpeakEvent(const char* text, bool interrupt = false)
{
    if (!text || text[0] == '\0') return;

    // Always store in repeat buffer
    strncpy(s_repeatBuffer, text, sizeof(s_repeatBuffer) - 1);
    s_repeatBuffer[sizeof(s_repeatBuffer) - 1] = '\0';

    ScreenReader::SpeakChannel2(text, interrupt);
}

// ============================================================================
// Entity array reading helpers
// ============================================================================

// The entity array is at the static address 0x1D27B18.
// The pointer at 0x1D27B10 stays NULL (FFNx hooks the resolution function),
// but the data is populated directly at the static address.
static uint8_t* GetEntityBlock(int slot)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return nullptr;
    return (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
}

// Read current HP for a slot. Allies = uint16, enemies = uint32.
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

// Read max HP for a slot. Allies = uint16, enemies = uint32.
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

// Check if entity is KO'd (persistent status bit 0).
static bool IsEntityKO(int slot)
{
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return true;
    __try {
        return (*(blk + BENT_PERSIST_STATUS) & 0x01) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

// Count active enemies (HP > 0 in slots 3-6).
static int CountActiveEnemies()
{
    int count = 0;
    for (int i = BATTLE_ALLY_SLOTS; i < BATTLE_TOTAL_SLOTS; i++) {
        if (GetEntityHP(i) > 0) count++;
    }
    return count;
}


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
    
    // Reset repeat buffer for new battle
    s_repeatBuffer[0] = '\0';
    s_repeatKeyWasDown = false;

    // Reset HP check key states
    s_hpKey1WasDown = false;
    s_hpKey2WasDown = false;
    s_hpKey3WasDown = false;
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
    
    // Reset EWM cap state for new battle
    s_ewmFreezing = false;
    s_ewmShouldCap = false;
    s_ewmCapExcludeSlot = 0xFF;
    s_ewmCapGF = false;
    s_ewmLastActiveChar = 0xFF;
    s_ewmNewTurnGrace = false;
    s_gfSnapValid = false;  // reset GF state snapshot
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
    // v0.10.88: Reset GF timer scan state
    s_gfScanValid = false;
    s_gfScanLogCount = 0;
    memset(s_gfScanSnap, 0, sizeof(s_gfScanSnap));
    // v0.10.96: Reset target selection diagnostic
    s_tgtDiagStage = 0;
    // v0.10.97: Reset target selection TTS state
    s_lastTargetBitmask = 0;
    s_lastTargetScope = 0;
    s_inTargetSelect = false;
    s_targetLastAnnounceTick = 0;
    // v0.10.84: Ensure fire byte is restored at battle start
    if (s_gfFirePatched && s_gfFirePatchReady) {
        *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_FIRE_VALUE;
        s_gfFirePatched = false;
    }
    EWM_LoadConfig();  // ensure config is loaded on first battle
    
    // Resolve battle menu addresses on first battle entry
    if (!s_pBattleMenuState) {
        ResolveBattleMenuAddresses();
    }

    // v0.10.77: Install FFNx GF hook on first battle entry (deferred from Initialize)
    if (!s_ffnxGFHookInstalled) {
        EWM_InstallFFNxGFHook();
        Log::Battle("BattleTTS: [FFNx-GF] Deferred install result: %s", s_ffnxGFHookInstalled ? "OK" : "FAIL");
    }

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

    // Wait for entity data to populate at the static address.
    // Enforce minimum delay (swirl animation), then poll ally slot 0 maxHP > 0.
    DWORD elapsed = GetTickCount() - s_battleEntryTime;
    if (elapsed < BATTLE_INIT_MIN_DELAY_MS) return;  // still in swirl
    
    __try {
        uint16_t allyMaxHP = *(uint16_t*)(BATTLE_ENTITY_ARRAY_BASE + BENT_MAX_HP);
        if (allyMaxHP == 0) {
            if (elapsed < BATTLE_INIT_TIMEOUT_MS) return;  // keep polling
            Log::Battle("BattleTTS: Entity array not populated after %ums", BATTLE_INIT_TIMEOUT_MS);
        } else {
            Log::Battle("BattleTTS: Entity array ready after %ums (ally0 maxHP=%u)",
                       elapsed, (unsigned)allyMaxHP);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: Exception polling entity array");
    }

    s_initAnnounceDone = true;

    // Log all slot data for diagnostics
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

    // Count active enemies and build name string
    int enemyCount = CountActiveEnemies();

    // Announce
    char buf[256];
    if (enemyCount == 0) {
        // Enemies may not be populated yet — second-pass will catch them
        snprintf(buf, sizeof(buf), "Battle!");
    } else {
        char enemyStr[200];
        BuildEnemyNameString(enemyStr, sizeof(enemyStr));
        snprintf(buf, sizeof(buf), "Battle! %s.", enemyStr);
        s_enemyAnnounceDone = true;
        if (!s_enemyNameCacheBuilt) BuildEnemyNameCache();
    }

    BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued (no interrupting other events)
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

    // v0.10.73: Dump GF timer function code BEFORE hooks overwrite the entry.
    // Function at 0x004B0500, write instruction at 0x004B063B.
    // Dump 0x004B0500 through 0x004B0680 (384 bytes) to cover the full function.
    // This reveals what addresses the function READS to compute the visual timer,
    // which should point us to the master GF countdown the fire logic also reads.
    {
        const uint32_t funcStart = 0x004B0500;
        const int dumpLen = 384;
        Log::Battle("BattleTTS: [GF-DISASM] === GF timer function code dump (PRE-HOOK) ===");
        Log::Battle("BattleTTS: [GF-DISASM] 0x%08X through 0x%08X (%d bytes)",
                   funcStart, funcStart + dumpLen, dumpLen);
        __try {
            uint8_t* code = (uint8_t*)funcStart;
            for (int off = 0; off < dumpLen; off += 16) {
                char hex[80] = {};
                int p = 0;
                for (int b = 0; b < 16 && off + b < dumpLen; b++)
                    p += snprintf(hex + p, sizeof(hex) - p, "%02X ", code[off + b]);
                Log::Battle("BattleTTS: [GF-DISASM] %08X: %s", funcStart + off, hex);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [GF-DISASM] EXCEPTION reading function code");
        }
        Log::Battle("BattleTTS: [GF-DISASM] === End dump ===");
    }

    // v0.10.88: Dump code at 0x004B0400-0x004B0500 to find the GF timer check.
    // The state machine handlers are at 0x004B0440-04FF. The CALLER of the
    // state=5 handler at 0x004B04B4 contains the timer comparison.
    {
        Log::Battle("BattleTTS: [GF-DISASM] === Code dump 0x004B0400-0x004B0500 ===");
        __try {
            for (uint32_t addr = 0x004B0400; addr < 0x004B0500; addr += 16) {
                uint8_t* p = (uint8_t*)addr;
                char hex[100] = {};
                int pos = 0;
                for (int i = 0; i < 16; i++)
                    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", p[i]);
                Log::Battle("BattleTTS: [GF-DISASM] %08X: %s", addr, hex);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [GF-DISASM] EXCEPTION reading code");
        }
        Log::Battle("BattleTTS: [GF-DISASM] === End dump ===");
    }
    
    // v0.10.84: Make the fire instruction byte writable for code patching.
    // VirtualProtect the page containing 0x004B04BA to PAGE_EXECUTE_READWRITE.
    {
        DWORD oldProtect = 0;
        BOOL ok = VirtualProtect((LPVOID)GF_FIRE_PATCH_ADDR, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
        s_gfFirePatchReady = (ok != FALSE);
        if (s_gfFirePatchReady) {
            // Verify the byte is what we expect (0x05)
            uint8_t curByte = *(uint8_t*)GF_FIRE_PATCH_ADDR;
            if (curByte != GF_FIRE_VALUE) {
                Log::Battle("BattleTTS: [GF-PATCH] WARNING: byte at 0x%08X is 0x%02X, expected 0x%02X",
                           GF_FIRE_PATCH_ADDR, (unsigned)curByte, (unsigned)GF_FIRE_VALUE);
                s_gfFirePatchReady = false;  // don't patch unknown code
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
    // v0.10.103: InstallBattleItemHook() REMOVED — ESI calling convention mismatch.
    // Item sub-menu uses direct cursor→list mapping via BuildItemList() instead.
    // v0.10.77: FFNx GF hook deferred to first battle entry.
    // At Initialize() time, MH_EnableHook(ALL) hasn't run yet, so FFNx's JMP
    // at set_midi_volume isn't active and we can't find the FFNx module.

    // v0.10.70/91: Register VEH for hardware BP (v0.10.91: READ on display timer 0x01D769D6)
    s_gfVEHHandle = AddVectoredExceptionHandler(1, GF_BP_VectoredHandler);
    Log::Battle("BattleTTS: [GF-BP] VEH registered: handle=0x%08X", (uint32_t)(uintptr_t)s_gfVEHHandle);

    Log::Battle("BattleTTS: Initialized v0.12.25 — SETLINE interactive object catalog (EWM=%s, ATB=%s, GF=%s, FFNx=%s, PATCH=%s).",
               s_ewmEnabled ? "ON" : "OFF",
               s_ewmHookInstalled ? "OK" : "FAIL",
               s_gfTimerHookInstalled ? "OK" : "FAIL",
               s_ffnxGFHookInstalled ? "OK" : "FAIL",
               s_gfFirePatchReady ? "OK" : "FAIL");
}

void Update()
{
    if (!s_initialized) return;
    if (!FF8Addresses::pGameMode) return;

    // EWM toggle: "O" key works in ALL game modes (field, worldmap, battle, menu)
    EWM_PollToggle();

    uint16_t mode = *FF8Addresses::pGameMode;
    // Battle mode is 3 (NOT 999 — FFNx's FF8_MODE_BATTLE=999 is an internal enum,
    // not the raw game mode value). Confirmed from log: battle dialog fires at mode 3.
    // Mode sequence: field(1) -> worldmap(2) -> battle(3) -> 5 -> 100 -> after_battle(4) -> worldmap(2)
    bool isBattle = (mode == 3);

    // Edge detection: battle entry/exit
    if (isBattle && !s_inBattle) {
        OnBattleEnter();
    } else if (!isBattle && s_inBattle) {
        OnBattleExit();
    }

    // Not in battle — nothing to do
    if (!s_inBattle) return;

    // Clear edge trigger after first frame
    if (s_battleJustStarted) {
        s_battleJustStarted = false;
    }

    // Battle start announcement (delayed for engine init)
    if (!s_initAnnounceDone) {
        AnnounceBattleStart();
    }

    // Second-pass: announce enemies if they weren't ready at initial announcement
    if (s_initAnnounceDone && !s_enemyAnnounceDone) {
        int enemyCount = CountActiveEnemies();
        if (enemyCount > 0) {
            s_enemyAnnounceDone = true;
            if (!s_enemyNameCacheBuilt) BuildEnemyNameCache();
            char enemyStr[200];
            BuildEnemyNameString(enemyStr, sizeof(enemyStr));
            char buf[256];
            snprintf(buf, sizeof(buf), "%s.", enemyStr);
            BattleSpeakEvent(buf, false);  // v0.10.44: Ch2 event (queue after battle start)
            Log::Battle("BattleTTS: [second-pass] %s (enemies appeared after initial announce)", buf);
            // Log enemy data
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
            s_enemyAnnounceDone = true;  // give up on enemy detection
            Log::Battle("BattleTTS: No enemies detected after %ums timeout", BATTLE_INIT_TIMEOUT_MS);
        }
    }

    // Menu cursor diagnostic — runs every 100ms during battle
    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollMenuDiagnostic();
    }

    // Turn announcements + command menu cursor TTS
    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollTurnAndCommands();
    }

    // v0.10.16: Sub-menu cursor hunter (continuous poll during active turns)
    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollCursorHunter();
    }

    // v0.10.22: Limit Break toggle detection (polls 0x01D7684A while cursor=0)
    if (s_initAnnounceDone && s_enemyAnnounceDone) {
        PollLimitToggle();
    }

    // v0.10.38: Enhanced Wait Mode — MinHook-based ATB freeze
    if (s_inBattle && s_initAnnounceDone) {
        EWM_UpdateBattle();
    }
    // v0.10.95: Mod-thread GF max inflation backup — per-slot via entity+0x7C.
    // Primary inflation runs on game thread (HookedATBUpdate). This is a
    // secondary safety net from the mod thread.
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
    // v0.10.65: GF timer hook diagnostic stats + state change monitor
    if (s_inBattle) {
        GF_LogHookStats();
        GF_PollStateChanges();
    }

    // v0.10.83: Auto-arm HW BP on state68 when GF loading starts
    if (s_inBattle) {
        GF_BP_AutoArm();
    }
    
    // F12 manual fallback
    if (s_inBattle) {
        GF_BP_PollKey();
    }

    // HP tracking: damage/healing announcements (v0.10.29)
    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollHPChanges();
    }

    // v0.10.35: HP check keys (1/2/3 = individual, H = full party)
    if (s_inBattle && s_initAnnounceDone) {
        PollHPCheckKeys();
    }

    // v0.10.30: Backtick repeat key — re-speak last non-menu announcement
    if (s_inBattle) {
        bool backtickDown = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0;  // ` / ~ key
        bool backtickPressed = backtickDown && !s_repeatKeyWasDown;
        s_repeatKeyWasDown = backtickDown;
        if (backtickPressed && s_repeatBuffer[0] != '\0') {
            ScreenReader::SpeakChannel2(s_repeatBuffer, true);  // Interrupt channel 2 to repeat
            Log::Battle("BattleTTS: [REPEAT] '%s'", s_repeatBuffer);
        }
    }
    // v0.10.97: Target selection TTS
    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollTargetSelection();
    }
}

// v0.10.112: Public accessor for the drawer's character name.
// Called by FieldDialog to prepend name to "Received X spells!" text.
const char* GetLastDrawerName()
{
    if (s_lastDrawerPartySlot < BATTLE_ALLY_SLOTS)
        return GetBattleCharName(s_lastDrawerPartySlot);
    return nullptr;
}

void Shutdown()
{
    if (!s_initialized) return;
    // v0.10.86: Sticky hide restore REMOVED (v0.10.88). Flag-hiding abandoned.
    // v0.10.84: Restore fire byte before shutdown
    if (s_gfFirePatched && s_gfFirePatchReady) {
        *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_FIRE_VALUE;
        s_gfFirePatched = false;
    }
    // v0.10.70: Remove VEH on shutdown
    if (s_gfVEHHandle) {
        RemoveVectoredExceptionHandler(s_gfVEHHandle);
        s_gfVEHHandle = nullptr;
    }
    s_initialized = false;
    s_inBattle = false;
    Log::Battle("BattleTTS: Shutdown.");
}

}  // namespace BattleTTS
