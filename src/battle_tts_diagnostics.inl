// battle_tts_diagnostics.inl — Menu diagnostic, cursor hunter, limit toggle, enemy cache
// Included from battle_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

// Battle menu cursor diagnostic (v0.10.09)
// ============================================================================
// FFNx resolves battle_menu_state from battle_pause_window_sub_4CD350 + 0x29.
// sub_4BB840 + 0x13 gives battle_current_active_character_id (BYTE*).
// sub_4BB840 + 0x37 gives battle_new_active_character_id (BYTE*).
//
// We resolve these the same way FFNx does: read the 4-byte immediate
// from the instruction at the specified offset.

// Game function addresses (static, no ASLR)
static const uint32_t ADDR_BATTLE_PAUSE_WINDOW_SUB = 0x4CD350;
static const uint32_t ADDR_SUB_4BB840 = 0x4BB840;

// Resolved pointers (set at battle entry)
static uint8_t* s_pBattleMenuState = nullptr;      // battle_menu_state data pointer
static uint8_t* s_pActiveCharId = nullptr;          // battle_current_active_character_id
static uint8_t* s_pNewActiveCharId = nullptr;       // battle_new_active_character_id

// Snapshot buffer for change tracking
// v0.10.15: Expanded to 4096 bytes starting at 0x01D76800 to cover sub-menu cursors.
// Known offsets within scan: +0x43 = cmd cursor, +0xD0 = menu phase.
static const uint32_t MENU_SCAN_BASE = 0x01D76800;
static const int MENU_SNAP_SIZE = 4096;
static uint8_t s_menuSnap[MENU_SNAP_SIZE] = {};
static bool s_menuSnapValid = false;
static DWORD s_lastMenuDiagTick = 0;
static const DWORD MENU_DIAG_INTERVAL_MS = 100; // poll every 100ms
static uint8_t s_lastActiveCharId = 0xFF;
static uint8_t s_lastNewActiveCharId = 0xFF;

// v0.10.15: Event-triggered sub-menu diagnostic state
static uint8_t s_lastMenuPhase = 0xFF;   // last BATTLE_MENU_PHASE value
static uint8_t s_lastDiagCmdCursor = 0xFF; // last command cursor for event trigger
static uint8_t* s_pBattleMenuStateByte = nullptr;  // battle_menu_state from sub_4CD350+0x29

// v0.10.15: Second scan region for sub-menu data (may be outside primary range)
// Start at 0x01D76000 (2KB before primary scan) to catch sub-menu list state.
static const uint32_t MENU_SCAN2_BASE = 0x01D76000;
static const int MENU_SNAP2_SIZE = 2048;
static uint8_t s_menuSnap2[MENU_SNAP2_SIZE] = {};
static bool s_menuSnap2Valid = false;

static void ResolveBattleMenuAddresses()
{
    s_pBattleMenuState = nullptr;
    s_pActiveCharId = nullptr;
    s_pNewActiveCharId = nullptr;
    
    // Active char IDs: resolved from sub_4BB840 instruction immediates
    __try {
        uint32_t activeCharAddr = *(uint32_t*)(ADDR_SUB_4BB840 + 0x13);
        if (activeCharAddr > 0x00400000 && activeCharAddr < 0x7FFFFFFF) {
            s_pActiveCharId = (uint8_t*)(uintptr_t)activeCharAddr;
            Log::Battle("BattleTTS: [MENU-DIAG] active_char_id resolved: 0x%08X", activeCharAddr);
        } else {
            Log::Battle("BattleTTS: [MENU-DIAG] active_char_id bad addr: 0x%08X", activeCharAddr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [MENU-DIAG] EXCEPTION resolving active_char_id");
    }
    
    __try {
        uint32_t newCharAddr = *(uint32_t*)(ADDR_SUB_4BB840 + 0x37);
        if (newCharAddr > 0x00400000 && newCharAddr < 0x7FFFFFFF) {
            s_pNewActiveCharId = (uint8_t*)(uintptr_t)newCharAddr;
            Log::Battle("BattleTTS: [MENU-DIAG] new_active_char_id resolved: 0x%08X", newCharAddr);
        } else {
            Log::Battle("BattleTTS: [MENU-DIAG] new_active_char_id bad addr: 0x%08X", newCharAddr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [MENU-DIAG] EXCEPTION resolving new_active_char_id");
    }
    
    // v0.10.15: Resolve battle_menu_state from battle_pause_window_sub_4CD350 + 0x29
    __try {
        uint32_t menuStateAddr = *(uint32_t*)(ADDR_BATTLE_PAUSE_WINDOW_SUB + 0x29);
        if (menuStateAddr > 0x00400000 && menuStateAddr < 0x7FFFFFFF) {
            s_pBattleMenuStateByte = (uint8_t*)(uintptr_t)menuStateAddr;
            Log::Battle("BattleTTS: [MENU-DIAG] battle_menu_state resolved: 0x%08X", menuStateAddr);
        } else {
            Log::Battle("BattleTTS: [MENU-DIAG] battle_menu_state bad addr: 0x%08X", menuStateAddr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [MENU-DIAG] EXCEPTION resolving battle_menu_state");
    }

    Log::Battle("BattleTTS: [MENU-DIAG] Wide scan base: 0x%08X (%d bytes), scan2: 0x%08X (%d bytes)",
               MENU_SCAN_BASE, MENU_SNAP_SIZE, MENU_SCAN2_BASE, MENU_SNAP2_SIZE);
}

// v0.10.15: Event-triggered sub-menu diagnostic.
// Instead of logging every change every 100ms (extremely noisy with ATB timers),
// this version takes snapshots and logs diffs ONLY when key state transitions occur:
//   - BATTLE_MENU_PHASE changes (entering/leaving sub-menus)
//   - battle_menu_state changes (FFNx's resolved menu state)
//   - Command cursor changes within sub-menu context
// This dramatically reduces noise and makes sub-menu cursor bytes easy to identify.
static void PollMenuDiagnostic()
{
    DWORD now = GetTickCount();
    if (now - s_lastMenuDiagTick < MENU_DIAG_INTERVAL_MS) return;
    s_lastMenuDiagTick = now;
    
    // Read current key state values
    uint8_t curMenuPhase = 0xFF;
    uint8_t curCmdCursor = 0xFF;
    uint8_t curActiveChar = 0xFF;
    uint8_t curMenuState = 0xFF;
    __try { curMenuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // BATTLE_MENU_PHASE
    __try { curCmdCursor = *(uint8_t*)0x01D76843; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // BATTLE_CMD_CURSOR
    if (s_pActiveCharId) { __try { curActiveChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    if (s_pBattleMenuStateByte) { __try { curMenuState = *s_pBattleMenuStateByte; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    
    // Detect events that trigger a full diff log
    bool phaseChanged = (curMenuPhase != s_lastMenuPhase && s_lastMenuPhase != 0xFF);
    bool menuStateChanged = false;
    static uint8_t s_lastMenuStateByte = 0xFF;
    if (curMenuState != s_lastMenuStateByte && s_lastMenuStateByte != 0xFF) menuStateChanged = true;
    bool charChanged = (curActiveChar != s_lastActiveCharId && s_lastActiveCharId != 0xFF);
    
    // Always log state transitions (compact single line)
    if (phaseChanged || menuStateChanged || charChanged) {
        Log::Battle("BattleTTS: [SUBMENU-DIAG] === STATE CHANGE === "
                   "menuPhase: %u->%u  menuState: %u->%u  cmdCursor: %u  activeChar: %u->%u",
                   (unsigned)s_lastMenuPhase, (unsigned)curMenuPhase,
                   (unsigned)s_lastMenuStateByte, (unsigned)curMenuState,
                   (unsigned)curCmdCursor,
                   (unsigned)s_lastActiveCharId, (unsigned)curActiveChar);
    }
    
    // Take snapshots of both scan regions
    uint8_t newSnap[MENU_SNAP_SIZE];
    uint8_t newSnap2[MENU_SNAP2_SIZE];
    __try { memcpy(newSnap, (uint8_t*)MENU_SCAN_BASE, MENU_SNAP_SIZE); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    __try { memcpy(newSnap2, (uint8_t*)MENU_SCAN2_BASE, MENU_SNAP2_SIZE); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    if (!s_menuSnapValid) {
        memcpy(s_menuSnap, newSnap, MENU_SNAP_SIZE);
        memcpy(s_menuSnap2, newSnap2, MENU_SNAP2_SIZE);
        s_menuSnapValid = true;
        s_menuSnap2Valid = true;
        Log::Battle("BattleTTS: [SUBMENU-DIAG] First snapshot: scan1=0x%08X(%d) scan2=0x%08X(%d)",
                   MENU_SCAN_BASE, MENU_SNAP_SIZE, MENU_SCAN2_BASE, MENU_SNAP2_SIZE);
        s_lastMenuPhase = curMenuPhase;
        s_lastMenuStateByte = curMenuState;
        s_lastActiveCharId = curActiveChar;
        s_lastDiagCmdCursor = curCmdCursor;
        return;
    }
    
    // On STATE CHANGE events: log ALL changed bytes in both regions (up to 60)
    if (phaseChanged || menuStateChanged) {
        // Region 1
        int changeCount = 0;
        for (int i = 0; i < MENU_SNAP_SIZE && changeCount < 40; i++) {
            if (newSnap[i] != s_menuSnap[i]) {
                Log::Battle("BattleTTS: [SUBMENU-DIAG] R1 +0x%04X (0x%08X): %u -> %u",
                           i, MENU_SCAN_BASE + i,
                           (unsigned)s_menuSnap[i], (unsigned)newSnap[i]);
                changeCount++;
            }
        }
        // Region 2
        if (s_menuSnap2Valid) {
            int changeCount2 = 0;
            for (int i = 0; i < MENU_SNAP2_SIZE && changeCount2 < 20; i++) {
                if (newSnap2[i] != s_menuSnap2[i]) {
                    Log::Battle("BattleTTS: [SUBMENU-DIAG] R2 +0x%04X (0x%08X): %u -> %u",
                               i, MENU_SCAN2_BASE + i,
                               (unsigned)s_menuSnap2[i], (unsigned)newSnap2[i]);
                    changeCount2++;
                }
            }
        }
        if (changeCount == 0) {
            Log::Battle("BattleTTS: [SUBMENU-DIAG] (no byte changes in scanned regions)");
        }
    }
    
    // Update snapshots and tracking state
    memcpy(s_menuSnap, newSnap, MENU_SNAP_SIZE);
    if (s_menuSnap2Valid) memcpy(s_menuSnap2, newSnap2, MENU_SNAP2_SIZE);
    s_lastMenuPhase = curMenuPhase;
    s_lastMenuStateByte = curMenuState;
    s_lastActiveCharId = curActiveChar;
    s_lastDiagCmdCursor = curCmdCursor;
}

// ============================================================================
// v0.10.16: Sub-menu cursor hunter (continuous poll)
// ============================================================================
// Scans a focused region around the known command cursor every 100ms while
// a turn is active. Logs ONLY bytes whose change looks cursor-like:
//   - abs(delta) between 1 and 10
//   - new value < 64 (cursors are small indices, not animation/timer values)
//   - old value < 64 (same)
// This filters out ATB timers, animation state, and pointer churn.
// Scan region: 0x01D76800 to 0x01D76A00 (512 bytes) — covers cmd cursor at +0x43
// and extends to where sub-menu cursors likely live.

static const uint32_t HUNT_SCAN_BASE = 0x01D76800;
static const int HUNT_SCAN_SIZE = 512;
static uint8_t s_huntSnap[512] = {};
static bool s_huntSnapValid = false;
static DWORD s_lastHuntTick = 0;
static const DWORD HUNT_INTERVAL_MS = 100;

// Known noisy offsets to skip (relative to HUNT_SCAN_BASE):
// +0x42 = visual counter (always incrementing), +0x43 = cmd cursor (already tracked)
static bool IsHuntNoisy(int off) {
    if (off == 0x42) return true;  // visual counter
    if (off == 0x43) return true;  // cmd cursor (already handled by PollTurnAndCommands)
    if (off == 0x44) return true;  // active_char_id
    if (off == 0x45) return true;  // new_active_char_id
    return false;
}

static void PollCursorHunter()
{
    // Only run while a turn is active
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (activeChar >= 3) {
        // No turn active — reset snapshot so we get a fresh baseline when next turn starts
        if (s_huntSnapValid) {
            s_huntSnapValid = false;
        }
        return;
    }
    
    DWORD now = GetTickCount();
    if (now - s_lastHuntTick < HUNT_INTERVAL_MS) return;
    s_lastHuntTick = now;
    
    uint8_t newSnap[512];
    __try {
        memcpy(newSnap, (uint8_t*)HUNT_SCAN_BASE, HUNT_SCAN_SIZE);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    if (!s_huntSnapValid) {
        memcpy(s_huntSnap, newSnap, HUNT_SCAN_SIZE);
        s_huntSnapValid = true;
        return;
    }
    
    // Compare and log cursor-like changes
    for (int i = 0; i < HUNT_SCAN_SIZE; i++) {
        if (newSnap[i] == s_huntSnap[i]) continue;
        if (IsHuntNoisy(i)) { s_huntSnap[i] = newSnap[i]; continue; }
        
        uint8_t oldVal = s_huntSnap[i];
        uint8_t newVal = newSnap[i];
        int delta = (int)newVal - (int)oldVal;
        int absDelta = (delta < 0) ? -delta : delta;
        
        // Cursor-like: both values small (<64), small change (1-10)
        if (oldVal < 64 && newVal < 64 && absDelta >= 1 && absDelta <= 10) {
            Log::Battle("BattleTTS: [CURSOR-HUNT] +0x%03X (0x%08X): %u -> %u (delta=%+d)",
                       i, HUNT_SCAN_BASE + i,
                       (unsigned)oldVal, (unsigned)newVal, delta);
        }
    }
    memcpy(s_huntSnap, newSnap, HUNT_SCAN_SIZE);
}

// ============================================================================
// v0.10.22: Limit Break detection via toggle byte
// ============================================================================
// Confirmed by F12 snapshot diagnostic (v0.10.21):
//   0x01D7684A: 0 = Attack showing at cursor 0
//               64 (0x40) = Limit Break showing at cursor 0
// Player presses Right on Attack to toggle to Limit Break (and vice versa).
// We poll this single byte every frame while cursor=0 and turn is active.

static const uint32_t BATTLE_LIMIT_TOGGLE = 0x01D7684A; // BYTE: 0=Attack, 64=Limit Break
static bool s_limitBreakActive = false;     // true when toggle byte == 64
static uint8_t s_lastLimitToggle = 0;       // last value of toggle byte

static void PollLimitToggle()
{
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (activeChar >= 3) return;  // no turn active
    
    // Only poll while cursor=0
    uint8_t cmdCursor = 0xFF;
    __try { cmdCursor = *(uint8_t*)0x01D76843; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (cmdCursor != 0) return;
    
    uint8_t toggle = 0;
    __try { toggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    if (toggle != s_lastLimitToggle) {
        bool wasLimit = s_limitBreakActive;
        s_limitBreakActive = (toggle == 64);
        s_lastLimitToggle = toggle;
        
        if (s_limitBreakActive && !wasLimit) {
            BattleSpeak("Limit Break", PRIO_MENU, true);
            Log::Battle("BattleTTS: [LIMIT] Attack -> Limit Break (toggle=%u)", (unsigned)toggle);
        } else if (!s_limitBreakActive && wasLimit) {
            BattleSpeak("Attack", PRIO_MENU, true);
            Log::Battle("BattleTTS: [LIMIT] Limit Break -> Attack (toggle=%u)", (unsigned)toggle);
        }
    }
}

// Legacy stubs
static void PollLimitToggleFast() {}
static void PollLimitToggleDiag() {}

// ============================================================================
// v0.10.97: Target selection TTS
// ============================================================================
// Target bitmask at 0x01D76884: power-of-2 bits corresponding to entity slots.
//   bit 0 (0x01) = ally slot 0, bit 1 (0x02) = ally slot 1, bit 2 (0x04) = ally slot 2
//   bit 3 (0x08) = enemy slot 3, bit 4 (0x10) = enemy slot 4, etc.
// Multi-bit = all-target (e.g. 0x78 = all enemies, 0x07 = all allies).
// Discovered via v0.10.96 F12 diagnostic.

static const uint32_t BATTLE_TARGET_BITMASK = 0x01D76884;  // uint8 bitmask
static const uint32_t BATTLE_TARGET_SCOPE  = 0x01D76883;  // uint8: 3=single, 1=all (v0.10.99 discovery)
static uint8_t s_lastTargetBitmask = 0;
static uint8_t s_lastTargetScope = 0;    // track scope changes too
static bool s_inTargetSelect = false;
static DWORD s_targetLastAnnounceTick = 0;
static const DWORD TARGET_DEBOUNCE_MS = 150;  // min time between target announcements

static int BitmaskToSlot(uint8_t mask)
{
    for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
        if (mask & (1 << i)) return i;
    }
    return -1;
}

static int CountBits(uint8_t mask)
{
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (mask & (1 << i)) count++;
    }
    return count;
}

// PollTargetSelection() defined later (after GetSlotName)
static void PollTargetSelection();

// Forward declaration (defined in Turn/Command section below)
static const char* GetBattleCharName(uint8_t partySlot);

// ============================================================================
// v0.10.33: Enemy name cache
// ============================================================================
// Names are cached at battle start (after entities populate) and persist
// through KO. This prevents GetEnemyName from failing when HP=0.
// When multiple enemies share a base name, they're numbered:
//   "Bite Bug 1", "Bite Bug 2" etc.
// If only one enemy has a given name, no number is appended.

static char s_enemyNameCache[BATTLE_ENEMY_SLOTS][64] = {};  // cached display names for slots 3-6
static bool s_enemyNameCacheBuilt = false;

static void BuildEnemyNameCache()
{
    // Step 1: Read base names for all enemy slots that have maxHP > 0
    char baseNames[BATTLE_ENEMY_SLOTS][64] = {};
    for (int i = 0; i < BATTLE_ENEMY_SLOTS; i++) {
        int slot = BATTLE_ALLY_SLOTS + i;
        s_enemyNameCache[i][0] = '\0';
        baseNames[i][0] = '\0';
        if (GetEntityMaxHP(slot) > 0) {
            // GetEnemyName checks HP>0, but at cache build time enemies should be alive
            if (!GetEnemyName(slot, baseNames[i], sizeof(baseNames[i]))) {
                snprintf(baseNames[i], sizeof(baseNames[i]), "Enemy %d", i + 1);
            }
        }
    }
    
    // Step 2: Count occurrences of each base name
    int nameCount[BATTLE_ENEMY_SLOTS] = {};  // how many enemies share each name
    for (int i = 0; i < BATTLE_ENEMY_SLOTS; i++) {
        if (baseNames[i][0] == '\0') continue;
        for (int j = 0; j < BATTLE_ENEMY_SLOTS; j++) {
            if (baseNames[j][0] == '\0') continue;
            if (strcmp(baseNames[i], baseNames[j]) == 0) nameCount[i]++;
        }
    }
    
    // Step 3: Assign numbered names for duplicates, plain names for unique
    int assignedNum[BATTLE_ENEMY_SLOTS] = {};  // next number to assign per base name
    for (int i = 0; i < BATTLE_ENEMY_SLOTS; i++) {
        if (baseNames[i][0] == '\0') {
            snprintf(s_enemyNameCache[i], sizeof(s_enemyNameCache[i]), "Enemy %d", i + 1);
            continue;
        }
        if (nameCount[i] > 1) {
            // Find which number this is (count matching names before this index)
            int num = 1;
            for (int j = 0; j < i; j++) {
                if (strcmp(baseNames[j], baseNames[i]) == 0) num++;
            }
            snprintf(s_enemyNameCache[i], sizeof(s_enemyNameCache[i]), "%s %d", baseNames[i], num);
        } else {
            strncpy(s_enemyNameCache[i], baseNames[i], sizeof(s_enemyNameCache[i]) - 1);
            s_enemyNameCache[i][sizeof(s_enemyNameCache[i]) - 1] = '\0';
        }
    }
    
    s_enemyNameCacheBuilt = true;
    Log::Battle("BattleTTS: [NAME-CACHE] Enemy name cache built:");
    for (int i = 0; i < BATTLE_ENEMY_SLOTS; i++) {
        int slot = BATTLE_ALLY_SLOTS + i;
        if (s_enemyNameCache[i][0] != '\0') {
            Log::Battle("BattleTTS: [NAME-CACHE]   slot%d = \"%s\" (base=\"%s\")",
                       slot, s_enemyNameCache[i], baseNames[i]);
        }
    }
}

// ============================================================================
