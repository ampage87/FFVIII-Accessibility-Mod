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
// See: Plan & Research Documents/Battle TTS implementation plan.md
//      Plan & Research Documents/Battle system memory map deep research results.md

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

// v0.10.32: BattleSpeakEvent — Channel 2 (event/status voice)
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

// ============================================================================
// Shared victory state — used by both screenshot.inl and victory.inl
// ============================================================================

// v0.12.85: Victory screen state (forward declarations, used by OnBattleEnter)
static bool s_victoryDumpDone = false;
static bool s_victoryScreenActive = false;
static DWORD s_victoryEntryTime = 0;
static uint16_t s_prevGameMode = 0;
static int s_victoryStepCount = 0;
static bool s_victoryF12WasDown = false;

// v0.13.36: Pre-battle GF struct snapshots for FindChangedGF fallback.
static uint8_t s_preBattleGFStructs[16][0x44] = {};
static bool s_preBattleGFSnapValid = false;

// Known victory data addresses
static const uint32_t VICTORY_EXP_BASE = 0x01CFF574;       // 3×u16: EXP earned per party slot
static const uint32_t VICTORY_AP_BASE  = 0x01CFF5C2;       // 3×u16: AP earned per party slot
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

// v0.12.96: GDI+ for PNG screenshots (used by screenshot.inl and victory.inl)
static ULONG_PTR s_gdiplusToken = 0;

// v0.13.28: Pre-battle EXP snapshots for level-up detection
static uint32_t s_preBattleExpAll[11] = {};
static bool s_preBattleExpSnapValid = false;

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
    
    memset((void*)s_gfAnimFired, 0, sizeof(s_gfAnimFired));
    s_prevBattleMagicId = -1;

    // Reset victory screen diagnostic state
    s_victoryDumpDone = false;
    s_victoryScreenActive = false;
    s_victoryEntryTime = 0;
    s_victoryStepCount = 0;
    s_victoryF12WasDown = false;
    s_diffSnapValid = false;
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

    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        bool allEnemiesDead = true;
        for (int i = BATTLE_ALLY_SLOTS; i < BATTLE_TOTAL_SLOTS; i++) {
            if (GetEntityMaxHP(i) > 0 && GetEntityHP(i) > 0) {
                allEnemiesDead = false;
                break;
            }
        }
        if (allEnemiesDead && !s_victoryScreenActive) {
            s_victoryScreenActive = true;
            s_victoryEntryTime = GetTickCount();
            s_victoryStepCount = 0;
            s_victoryF12WasDown = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
            Log::Battle("BattleTTS: [VICTORY] All enemies dead — victory capture enabled");
            DumpVictoryStep(0);
        }
        if (s_victoryScreenActive) {
            bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
            bool f12Pressed = f12Down && !s_victoryF12WasDown;
            s_victoryF12WasDown = f12Down;
            if (f12Pressed) {
                s_victoryStepCount++;
                Log::Battle("BattleTTS: [VICTORY] F12 — step %d", s_victoryStepCount);
                DumpVictoryStep(s_victoryStepCount);
                char buf[64];
                snprintf(buf, sizeof(buf), "Step %d captured.", s_victoryStepCount);
                ScreenReader::Speak(buf, true);
            }
        }
    }

    if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
        PollHPChanges();
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
