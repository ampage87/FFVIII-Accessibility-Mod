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

// ============================================================================
// v0.14.17 setup for PollLimitToggle screenshot (Aaron's diagnostic)
// ============================================================================
//
// Forward declaration from battle_tts_screenshot.inl (which is #included into
// battle_tts.cpp AFTER this file). Same precedent as battle_tts_sprite.inl.
// PollLimitToggle below uses CaptureScreenshot to validate whether FF8
// visually displays the character-specific limit name (e.g. 'Renzokuken',
// 'Blue Magic') or just generic 'Limit' on Attack→Limit Break toggle.
// CaptureScreenshot sets a deferred-capture flag; the actual glReadPixels
// happens inside HookedSwapBuffers on the next frame, then blocks up to
// 160ms for the render thread to consume.
static void CaptureScreenshot(const char* basePath);

// Hardcoded screenshot path (rather than reusing KIND4_SCREENSHOT_DIR from
// sprite.inl) because sprite.inl is #included AFTER this file; that
// constant is not in scope here. Path is the standard Logs/screenshots dir;
// the existing screenshot pipeline auto-creates it via CreateDirectoryA in
// other code paths so we don't need our own ensure step.
static const char* LIMIT_TOGGLE_SCREENSHOT_DIR =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\screenshots";
static const int LIMIT_TOGGLE_SCREENSHOT_MAX_PER_TURN = 6;  // 3 toggle pairs
static int s_limitToggleScreenshotCount = 0;

static void PollLimitToggle()
{
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (activeChar >= 3) {
        // v0.14.17: reset toggle screenshot counter at end of turn so each
        // turn gets its own budget. PollLimitDiag's turn-end reset block
        // already clears related state; we mirror that pattern here.
        s_limitToggleScreenshotCount = 0;
        return;  // no turn active
    }

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

            // v0.14.17: snap a screenshot the moment the toggle latches to
            // Limit Break (Aaron's diagnostic). Validates whether FF8 shows
            // the character-specific limit name on the Attack command
            // label, generic 'Limit', or something else. Capped at
            // LIMIT_TOGGLE_SCREENSHOT_MAX_PER_TURN per turn so a player
            // mashing Right/Left doesn't flood the screenshots dir.
            if (s_limitToggleScreenshotCount < LIMIT_TOGGLE_SCREENSHOT_MAX_PER_TURN) {
                s_limitToggleScreenshotCount++;
                SYSTEMTIME wt;
                GetLocalTime(&wt);
                char path[512];
                snprintf(path, sizeof(path),
                         "%s\\limit_toggle_ON_%02d%02d%02d_%03d_slot%u_n%d",
                         LIMIT_TOGGLE_SCREENSHOT_DIR,
                         wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds,
                         (unsigned)activeChar, s_limitToggleScreenshotCount);
                CaptureScreenshot(path);
                Log::Battle("BattleTTS: [LIMIT-CAP] Captured Attack->Limit toggle: %s", path);
            }
        } else if (!s_limitBreakActive && wasLimit) {
            BattleSpeak("Attack", PRIO_MENU, true);
            Log::Battle("BattleTTS: [LIMIT] Limit Break -> Attack (toggle=%u)", (unsigned)toggle);

            // v0.14.17: also capture the OFF transition for a complete
            // before/after pair. Same per-turn cap.
            if (s_limitToggleScreenshotCount < LIMIT_TOGGLE_SCREENSHOT_MAX_PER_TURN) {
                s_limitToggleScreenshotCount++;
                SYSTEMTIME wt;
                GetLocalTime(&wt);
                char path[512];
                snprintf(path, sizeof(path),
                         "%s\\limit_toggle_OFF_%02d%02d%02d_%03d_slot%u_n%d",
                         LIMIT_TOGGLE_SCREENSHOT_DIR,
                         wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds,
                         (unsigned)activeChar, s_limitToggleScreenshotCount);
                CaptureScreenshot(path);
                Log::Battle("BattleTTS: [LIMIT-CAP] Captured Limit->Attack toggle: %s", path);
            }
        }
    }
}

// Legacy stubs
static void PollLimitToggleFast() {}
static void PollLimitToggleDiag() {}

// ============================================================================
// v0.14.23: Auto-Building Blue Magic Scanner with DIAGNOSTIC LOGGING
// ============================================================================
// v0.14.23: Added extensive diagnostic logging to understand cursor→spell ID relationship
// that's causing backwards spell announcement. Previous versions assumed cursor 0 = 0x92,
// but Aaron's BAT results suggest cursor 0 = 0xAA. Need data to confirm and fix.

// Blue Magic spell signatures with KNOWN SPELL IDs from v0.14.20
static const struct AutoBlueMagicSignature {
    const char* name;
    uint8_t spellId;        // FIXED: Use established spell IDs
    uint8_t signature[7];
    size_t sigLen;
} AUTO_BLUE_MAGIC_SIGNATURES[] = {
    // v0.14.20 established these mappings - preserve them!
    { "Laser Eye",     0x92, {0x50,0x5F,0x71,0x63,0x70,0x20,0x49}, 7 },  // ID 0x92 = "Laser E"
    { "Ultra Waves",   0xAA, {0x59,0x6A,0x72,0x70,0x5F,0x20,0x5B}, 7 },  // ID 0xAA = "Ultra W"
    
    // Future spells - IDs will be discovered and logged, then hardcoded here
    { "Electrocute",   0x00, {0x49,0x6A,0x63,0x61,0x72,0x70,0x6D}, 7 },  // ID TBD
    { "Thunder Shot",  0x00, {0x58,0x66,0x73,0x6C,0x62,0x20,0x57}, 7 },  // ID TBD
    { "Scatter Shot",  0x00, {0x57,0x61,0x5F,0x72,0x72,0x63,0x70}, 7 },  // ID TBD
    { "Dark Shot",     0x00, {0x48,0x5F,0x70,0x69,0x20,0x57,0x66}, 7 },  // ID TBD
    { "Full Life",     0x00, {0x46,0x73,0x6A,0x6A,0x20,0x50,0x67}, 7 },  // ID TBD
    { "Bad Breath",    0x00, {0x46,0x5F,0x62,0x20,0x46,0x70,0x63}, 7 },  // ID TBD
    { "White Wind",    0x00, {0x5B,0x66,0x67,0x72,0x63,0x20,0x5B}, 7 },  // ID TBD
    { "Reflector",     0x00, {0x56,0x63,0x64,0x6A,0x63,0x61,0x72}, 7 },  // ID TBD
    { "Degenerator",   0x00, {0x48,0x63,0x65,0x63,0x6C,0x63,0x70}, 7 },  // ID TBD
    { "Aqua Breath",   0x00, {0x45,0x71,0x73,0x5F,0x20,0x46,0x70}, 7 },  // ID TBD
    { "Mighty Guard",  0x00, {0x51,0x67,0x65,0x66,0x72,0x77,0x20}, 7 },  // ID TBD
    { "LV5 Death",     0x00, {0x50,0x5A,0x26,0x20,0x48,0x63,0x5F}, 7 },  // ID TBD
    { "LV4 Death",     0x00, {0x50,0x5A,0x25,0x20,0x48,0x63,0x5F}, 7 },  // ID TBD
    { "LV3 Death",     0x00, {0x50,0x5A,0x24,0x20,0x48,0x63,0x5F}, 7 },  // ID TBD
};

static const size_t AUTO_BLUE_MAGIC_SIGNATURE_COUNT = 
    sizeof(AUTO_BLUE_MAGIC_SIGNATURES) / sizeof(AUTO_BLUE_MAGIC_SIGNATURES[0]);

// POD struct for found spells - NO C++ objects
static struct AutoBlueMagicFoundEntry {
    uint8_t spellId;        // Spell ID 
    uint32_t runtimeAddr;   // Memory address where spell name is found
    char name[32];          // Spell name (fixed-size ASCII buffer)
    bool isValid;           // True if address is confirmed working
} s_autoBuiltSpells[16];    // Fixed-size array instead of std::vector

static int s_autoBuiltSpellCount = 0;
static bool s_autoScanCompleted = false;

// SEH-protected memory scanner - NO C++ OBJECTS
static uint32_t ScanMemoryForBlueMagicSignature(const uint8_t* signature, size_t sigLen) {
    const uint32_t startAddr = 0x01CF0000;
    const uint32_t endAddr   = 0x01DF0000;
    
    __try {
        for (uint32_t addr = startAddr; addr <= endAddr - sigLen; addr++) {
            bool match = true;
            for (size_t i = 0; i < sigLen; i++) {
                if (*(uint8_t*)(addr + i) != signature[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return addr;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Memory access violation - continue scanning
    }
    return 0;
}

// SEH-protected text decoder - NO C++ OBJECTS
static bool DecodeFF8TextAtAddress(uint32_t addr, char* outBuf, size_t bufSize) {
    if (outBuf == nullptr || bufSize < 2) return false;
    outBuf[0] = '\0';
    
    __try {
        size_t len = 0;
        for (size_t i = 0; i < bufSize - 1 && i < 64; i++) {
            uint8_t b = *(uint8_t*)(addr + i);
            if (b == 0x00) break;  // null terminator
            
            // FF8 field encoding
            char c = 0;
            if (b >= 0x45 && b <= 0x5E) {
                c = 'A' + (b - 0x45);
            } else if (b >= 0x5F && b <= 0x78) {
                c = 'a' + (b - 0x5F);
            } else if (b >= 0x21 && b <= 0x2A) {
                c = '0' + (b - 0x21);
            } else if (b == 0x20) {
                c = ' ';
            } else if (b == 0x2E) {
                c = '!';
            } else if (b == 0x2F) {
                c = '?';
            } else if (b == 0x3B) {
                c = '.';
            } else {
                // Unknown encoding, stop here
                break;
            }
            outBuf[len++] = c;
        }
        outBuf[len] = '\0';
        return (len > 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
}

// Build spell table using POD-only functions - NO C++ OBJECTS
// v0.14.22: FIXED to preserve spell ID mappings from v0.14.20
static bool BuildAutoBlueMagicTable() {
    if (s_autoScanCompleted) return (s_autoBuiltSpellCount > 0);

    Log::Battle("AUTO-BLUE: Building Blue Magic table from memory scan...");
    Log::Battle("AUTO-BLUE: Scanning for %zu spell signatures (with FIXED spell ID mappings)...", AUTO_BLUE_MAGIC_SIGNATURE_COUNT);
    
    s_autoBuiltSpellCount = 0;

    // Scan for each spell signature
    for (size_t i = 0; i < AUTO_BLUE_MAGIC_SIGNATURE_COUNT && s_autoBuiltSpellCount < 16; i++) {
        const AutoBlueMagicSignature& sig = AUTO_BLUE_MAGIC_SIGNATURES[i];
        uint32_t foundAddr = ScanMemoryForBlueMagicSignature(sig.signature, sig.sigLen);
        
        if (foundAddr != 0) {
            char decoded[64] = "";
            bool decodeOk = DecodeFF8TextAtAddress(foundAddr, decoded, sizeof(decoded));
            
            if (decodeOk && strlen(decoded) >= 3) {
                // Verify the decoded name contains our expected spell name
                bool nameMatches = (strstr(decoded, sig.name) != nullptr);
                if (nameMatches) {
                    AutoBlueMagicFoundEntry& entry = s_autoBuiltSpells[s_autoBuiltSpellCount];
                    entry.spellId = sig.spellId;  // FIXED: Use known spell ID from signature
                    entry.runtimeAddr = foundAddr;
                    strncpy(entry.name, sig.name, sizeof(entry.name) - 1);
                    entry.name[sizeof(entry.name) - 1] = '\0';
                    entry.isValid = true;
                    s_autoBuiltSpellCount++;
                    
                    Log::Battle("AUTO-BLUE: Found '%s' at 0x%08X (ID=0x%02X, decoded: '%s')", 
                               sig.name, foundAddr, sig.spellId, decoded);
                } else {
                    Log::Battle("AUTO-BLUE: Address 0x%08X for '%s' failed verification (decoded: '%s')",
                               foundAddr, sig.name, decoded);
                }
            } else {
                Log::Battle("AUTO-BLUE: Address 0x%08X for '%s' failed decode", foundAddr, sig.name);
            }
        }
    }

    s_autoScanCompleted = true;
    
    Log::Battle("AUTO-BLUE: Scan complete - found %d/%zu spells with fixed ID mappings", s_autoBuiltSpellCount, AUTO_BLUE_MAGIC_SIGNATURE_COUNT);
    
    if (s_autoBuiltSpellCount > 0) {
        Log::Battle("AUTO-BLUE: Auto-building scanner ready - %d spells available", s_autoBuiltSpellCount);
        return true;
    } else {
        Log::Battle("AUTO-BLUE: No spells found - falling back to placeholder mode");
        return false;
    }
}

// Find spell name by ID using POD-only storage - NO C++ OBJECTS
// v0.14.22: Uses FIXED spell ID mappings - no more dynamic mapping
static bool FindBlueMagicSpellName(uint8_t spellId, char* outBuf, size_t bufSize) {
    if (outBuf == nullptr || bufSize < 2) return false;
    outBuf[0] = '\0';
    
    // Build table on first use
    if (!s_autoScanCompleted) {
        BuildAutoBlueMagicTable();
    }

    // Find by spell ID (fixed mapping)
    for (int i = 0; i < s_autoBuiltSpellCount; i++) {
        AutoBlueMagicFoundEntry& entry = s_autoBuiltSpells[i];
        if (entry.spellId == spellId && entry.isValid) {
            // Verify address still works
            char decoded[64] = "";
            bool decodeOk = DecodeFF8TextAtAddress(entry.runtimeAddr, decoded, sizeof(decoded));
            if (decodeOk && strstr(decoded, entry.name) != nullptr) {
                strncpy(outBuf, entry.name, bufSize - 1);
                outBuf[bufSize - 1] = '\0';
                return true;
            } else {
                // Address no longer valid, mark as invalid
                entry.isValid = false;
                Log::Battle("AUTO-BLUE: Invalidated address 0x%08X for '%s' ID=0x%02X (decode failed)",
                           entry.runtimeAddr, entry.name, entry.spellId);
            }
        }
    }

    // For unknown spell IDs, log them for future hardcoding
    if (spellId != 0xFF) {
        Log::Battle("AUTO-BLUE: UNKNOWN SPELL ID 0x%02X encountered - add to signature table for next version", spellId);
    }

    // No mapping found
    return false;
}

// Initialize auto-building Blue Magic scanner - ENTRY POINT
static void InitializeAutoBlueMagic() {
    // No need to reset mappings - they're now fixed from the signature table
    BuildAutoBlueMagicTable();
}

// ============================================================================
// v0.14.11 / v0.14.12 / v0.14.13: Limit Break menu diagnostic + submenu announce
// ============================================================================
// v0.14.11 confirmed v0.10.22 PollLimitToggle works — "Limit Break" announces
// when pressing Right at Attack with crisis available. Squall's Limit (no
// submenu) works fine.
//
// v0.14.12 BAT data:
//   - When Quistis selects Limit Break at slot 0, phaseDw at 0x01D768D0 is
//     0x004C7CD0 (function pointer = the engine's Limit Break command
//     callback). This stays set as long as Limit Break is selected.
//   - When the user confirms (presses X), submMode at 0x01D768EB transitions
//     0xFE → 0x00. That's the canonical "limit submenu opened" signal.
//   - The phase byte at 0x01D768D0 stays at 208 (= 0xD0, low byte of the
//     function pointer), which is NEITHER 32 nor 80 — so the existing
//     submenu fallback in PollTurnAndCommands does NOT fire for limits.
//   - The standard subCursor at 0x01D768EC is NOT updated by Blue Magic.
//     The spell list cursor lives somewhere near 0x01D768E4 area, but with
//     only one spell available we couldn't identify it cleanly.
//   - The v0.14.12 toggle==64 gate was too aggressive: when the user enters
//     the submenu the toggle byte drops below 64 (animation values), so the
//     diagnostic stopped logging right when it should have started.
//
// v0.14.13 changes:
//   1. Drop the toggle==64 gate. Latch s_inLimitMode for the whole turn
//      once toggle hits 64 — cleared on activeChar→0xFF.
//   2. Detect limit submenu open: submMode 0xFE→0x00 while s_inLimitMode
//      is latched. Announce "Blue Magic" for Quistis (charIdx==3), or
//      "Limit Break submenu" otherwise. Other character-specific titles
//      can be added incrementally as Aaron tests them.
//   3. Detect limit submenu close: submMode→0xFE while s_inLimitSubmenu
//      is true. Don't announce — the existing TARGET-EXIT and TARGET-ACTIVE
//      handlers cover what comes next.
//   4. Keep BASELINE/STATE/CHANGE diagnostic logging for future iterations
//      — when Quistis has multiple Blue Magic spells we'll see clean cursor
//      data and can wire spell-name announcements in v0.14.14+.
//
// User flow with v0.14.13 (Quistis, crisis active):
//   Right at Attack → "Limit Break"          [from PollLimitToggle, already works]
//   X to confirm    → "Blue Magic"            [NEW: this build]
//   X to confirm    → "All enemies"           [from PollTargetSelection, works]
//   X to confirm    → spell casts

static const uint32_t LIMIT_DIAG_WINDOW_BASE = 0x01D76800;
static const int      LIMIT_DIAG_WINDOW_SIZE = 64;
static uint8_t        s_limitDiagPrev[LIMIT_DIAG_WINDOW_SIZE] = {};
static bool           s_limitDiagPrevValid = false;
static uint8_t        s_limitDiagPrevActiveChar = 0xFF;
static DWORD          s_limitDiagLastTick = 0;
// State trackers across polls.
static uint8_t        s_limitDiagPrevCmdCursor = 0xFF;
static uint8_t        s_limitDiagPrevSubCursor = 0xFF;
static uint8_t        s_limitDiagPrevMenuPhase = 0xFF;
static uint32_t       s_limitDiagPrevMenuDword = 0;
static uint8_t        s_limitDiagPrevSubmenuMode = 0xFF;
// v0.14.13: Latched-mode + submenu-open tracking for announcement.
static bool           s_inLimitMode = false;     // sticky for the turn once toggle==64
static bool           s_inLimitSubmenu = false;  // true between submMode 0xFE→0x00 and 0x00→0xFE
// v0.14.14: Cursor + per-character struct tracking for Blue Magic spell-list announce + memory hunt.
static uint8_t        s_limitSubCursorPrev = 0xFF;          // last subCursor seen while in limit submenu
static const uint32_t LIMIT_PERCHAR_BASE = 0x01CFF032;       // = partySlot * 464 + 0x1CFF032; partySlot 0 (Quistis here) base
static const int      LIMIT_PERCHAR_SIZE = 256;             // size of per-char limit-data window we sample
static uint8_t        s_limitPerCharPrev[LIMIT_PERCHAR_SIZE] = {};
static bool           s_limitPerCharPrevValid = false;
static const int      LIMIT_DIAG_WIDE_SIZE = 256;           // 0x01D76800 + 256 bytes (vs the 64-byte primary window)
static uint8_t        s_limitDiagWidePrev[LIMIT_DIAG_WIDE_SIZE] = {};
static bool           s_limitDiagWidePrevValid = false;

// Bytes within the 64-byte window we already understand and don't want to
// flood the log with. Offsets are relative to 0x01D76800.
static bool LimitDiag_IsNoisy(int off) {
    if (off == 0x42) return true;  // visual counter (always increments)
    if (off == 0x44) return true;  // active_char_id (already tracked)
    if (off == 0x45) return true;  // new_active_char_id
    return false;
}

static void PollLimitDiag()
{
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (activeChar >= 3) {
        // Turn ended. Reset all state.
        if (s_inLimitMode || s_inLimitSubmenu) {
            Log::Battle("BattleTTS: [LIMIT-DIAG] Turn ended — clearing (mode=%d submenu=%d)",
                       (int)s_inLimitMode, (int)s_inLimitSubmenu);
        }
        s_inLimitMode = false;
        s_inLimitSubmenu = false;
        // v0.14.14: clear cursor + memory snapshot tracking too, so the next
        // limit submenu starts with a fresh baseline.
        s_limitSubCursorPrev = 0xFF;
        s_limitPerCharPrevValid = false;
        s_limitDiagWidePrevValid = false;
        if (s_limitDiagPrevValid) {
            s_limitDiagPrevValid = false;
            s_limitDiagPrevActiveChar = 0xFF;
        }
        return;
    }

    DWORD now = GetTickCount();
    if (now - s_limitDiagLastTick < 100) return;
    s_limitDiagLastTick = now;

    uint8_t window[LIMIT_DIAG_WINDOW_SIZE] = {};
    uint8_t menuPhaseB = 0xFF;
    uint8_t submenuMode = 0xFF;
    uint32_t menuPhaseDw = 0;
    uint8_t cmdCursor = 0xFF;
    uint8_t subCursor = 0xFF;
    uint8_t toggle = 0;
    uint8_t crisisLevel = 0xFF;

    __try { memcpy(window, (void*)LIMIT_DIAG_WINDOW_BASE, LIMIT_DIAG_WINDOW_SIZE); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    __try { menuPhaseB  = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { menuPhaseDw = *(uint32_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { submenuMode = *(uint8_t*)0x01D768EB; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { cmdCursor   = *(uint8_t*)0x01D76843; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { subCursor   = *(uint8_t*)0x01D768EC; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { toggle      = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try {
        uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + activeChar * BATTLE_ENTITY_STRIDE);
        crisisLevel = ent[0xC2];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // v0.14.13: Latch "in limit mode" for the duration of the turn once
    // toggle hits 64. The toggle byte is unstable (cycles through 0, 8, 48,
    // 64 during animations and on confirmation) so we need a sticky flag
    // rather than a moment-by-moment check.
    if (toggle == 64 && !s_inLimitMode) {
        s_inLimitMode = true;
        Log::Battle("BattleTTS: [LIMIT-DIAG] Limit mode latched (toggle=64) for turn slot=%u",
                   (unsigned)activeChar);
        
        // v0.14.23: Initialize auto-building Blue Magic scanner with DIAGNOSTIC LOGGING
        InitializeAutoBlueMagic();
    }

    // Lookup char name (used by both announcement and BASELINE log).
    const char* name = "???";
    uint8_t charIdx = 0xFF;
    uint8_t* charBase = nullptr;
    __try {
        charIdx = *(uint8_t*)(0x1CFE74C + activeChar);  // SAVEMAP_PARTY_FORMATION
        if (charIdx < 8) {
            static const char* CHAR_NAMES_LOCAL[8] = {
                "Squall", "Zell", "Irvine", "Quistis",
                "Rinoa", "Selphie", "Seifer", "Edea"
            };
            name = CHAR_NAMES_LOCAL[charIdx];
            charBase = (uint8_t*)(0x1CFE0E8 + charIdx * 0x98);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // v0.14.13/v0.14.15: Limit submenu OPEN detection. The v0.14.14 BAT log
    // proved the v0.14.13 condition (submMode 0xFE → 0x00) was wrong: when
    // Quistis confirms Limit Break, submMode actually transitions 0x01 → 0x00
    // (not 0xFE → 0x00), so OPEN never fired and the suppression flag never
    // latched, leaving the regular PollTurnAndCommands handler to announce
    // "Attack" on every Blue Magic cursor move.
    //
    // The reliable signal is phaseDw transitioning TO 0x004C7CD0 — that's
    // the engine's Limit Break command callback function pointer, set the
    // moment the user confirms Limit Break. Combined with s_inLimitMode
    // being latched, this is unambiguous.
    if (s_inLimitMode && !s_inLimitSubmenu &&
        menuPhaseDw == 0x004C7CD0 && s_limitDiagPrevMenuDword != 0x004C7CD0) {
        s_inLimitSubmenu = true;

        // Per-character submenu name. Quistis = Blue Magic; others get a
        // generic title until we have BAT data for them.
        const char* announceText = "Limit Break submenu";
        if (charIdx == 3) announceText = "Blue Magic";

        BattleSpeak(announceText, PRIO_MENU, true);
        Log::Battle("BattleTTS: [LIMIT-DIAG] Submenu OPENED for %s (charIdx=%u) — announcing: %s",
                   name, (unsigned)charIdx, announceText);

        // v0.14.14: Snapshot cursor + memory baselines for the new submenu
        // session. The cursor baseline keeps the first per-frame check from
        // immediately announcing 'Spell 1' on top of the OPEN announcement.
        // The memory baselines anchor the CHANGE-on-cursor-move diagnostic
        // that follows so we can find where the visible spell ID lives.
        s_limitSubCursorPrev = subCursor;
        __try { memcpy(s_limitPerCharPrev, (void*)LIMIT_PERCHAR_BASE, LIMIT_PERCHAR_SIZE); s_limitPerCharPrevValid = true; }
            __except(EXCEPTION_EXECUTE_HANDLER) { s_limitPerCharPrevValid = false; }
        __try { memcpy(s_limitDiagWidePrev, (void*)LIMIT_DIAG_WINDOW_BASE, LIMIT_DIAG_WIDE_SIZE); s_limitDiagWidePrevValid = true; }
            __except(EXCEPTION_EXECUTE_HANDLER) { s_limitDiagWidePrevValid = false; }
        Log::Battle("BattleTTS: [LIMIT-DIAG] Wide-snapshot baselines captured (perChar=0x%08X+%d, wide=0x%08X+%d)",
                    LIMIT_PERCHAR_BASE, LIMIT_PERCHAR_SIZE, LIMIT_DIAG_WINDOW_BASE, LIMIT_DIAG_WIDE_SIZE);
    }
    // v0.14.13/v0.14.15: Limit submenu CLOSE detection. Mirror of OPEN —
    // close is phaseDw transitioning AWAY from 0x004C7CD0. Don't announce;
    // existing TARGET-ACTIVE / TARGET-EXIT handlers cover the next state.
    // Clearing the flag lets re-entry fire again if the user cancels back
    // out and re-confirms.
    if (s_inLimitSubmenu &&
        menuPhaseDw != 0x004C7CD0 && s_limitDiagPrevMenuDword == 0x004C7CD0) {
        s_inLimitSubmenu = false;
        Log::Battle("BattleTTS: [LIMIT-DIAG] Submenu CLOSED for %s (phaseDw 0x%08X → 0x%08X)",
                    name, s_limitDiagPrevMenuDword, menuPhaseDw);
    }

    // v0.14.23: EXTENSIVE DIAGNOSTIC LOGGING for cursor→spell ID relationship
    // Need to understand why spells announce backwards despite fixed ID mappings
    if (s_inLimitSubmenu && subCursor != s_limitSubCursorPrev) {
        char announceBuf[64] = {};
        uint8_t spellId = 0xFF;

        // Read spell ID from the engine
        __try {
            spellId = *(uint8_t*)0x01D76860;
        } __except(EXCEPTION_EXECUTE_HANDLER) { spellId = 0xFF; }

        // v0.14.23: DIAGNOSTIC LOGGING to understand cursor→spell ID relationship
        Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR] === CURSOR MOVE DETECTED ===");
        Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR] subCursor: %u -> %u", 
                   (unsigned)s_limitSubCursorPrev, (unsigned)subCursor);
        Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR] spellId at 0x01D76860: 0x%02X", (unsigned)spellId);
        
        // Show what our current mapping table contains
        Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR] Known mappings in auto-builder:");
        for (int i = 0; i < s_autoBuiltSpellCount; i++) {
            AutoBlueMagicFoundEntry& entry = s_autoBuiltSpells[i];
            if (entry.isValid) {
                Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR]   ID 0x%02X -> '%s' at 0x%08X", 
                           entry.spellId, entry.name, entry.runtimeAddr);
            }
        }

        // Try auto-building scanner with detailed logging
        if (FindBlueMagicSpellName(spellId, announceBuf, sizeof(announceBuf))) {
            Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR] AUTO-BUILDER FOUND: spellId 0x%02X -> '%s'", 
                       (unsigned)spellId, announceBuf);
        } else {
            Log::Battle("BattleTTS: [LIMIT-DIAG-CURSOR] AUTO-BUILDER FAILED: spellId 0x%02X not found, using fallback", 
                       (unsigned)spellId);
            snprintf(announceBuf, sizeof(announceBuf),
                     "Spell %u", (unsigned)(subCursor + 1));
        }

        BattleSpeak(announceBuf, PRIO_MENU, true);

        Log::Battle("BattleTTS: [LIMIT-NAV] DIAGNOSTIC sub %u->%u id=0x%02X | announce='%s'",
                    (unsigned)s_limitSubCursorPrev, (unsigned)subCursor,
                    (unsigned)spellId, announceBuf);

        s_limitSubCursorPrev = subCursor;
    }

    // ===== Diagnostic logging from here on — BASELINE, STATE, CHANGE. =====
    // No toggle gate; we capture data for the whole turn once activeChar is
    // valid. Useful for future iterations when Quistis has multiple Blue
    // Magic spells — the cursor byte will reveal itself in the CHANGE log.

    // First sample for this turn — dump full BASELINE plus a one-time
    // savemap dump for offline analysis.
    if (!s_limitDiagPrevValid || activeChar != s_limitDiagPrevActiveChar) {
        char hex[256] = {};
        int hp = 0;
        for (int i = 0; i < LIMIT_DIAG_WINDOW_SIZE; i++)
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", window[i]);

        Log::Battle("BattleTTS: [LIMIT-DIAG] BASELINE turn=%s slot=%u charIdx=%u "
                    "cmdCursor=%u subCursor=%u phaseB=%u phaseDw=0x%08X submMode=0x%02X "
                    "crisis=%u toggle=%u",
                    name, (unsigned)activeChar, (unsigned)charIdx,
                    (unsigned)cmdCursor, (unsigned)subCursor,
                    (unsigned)menuPhaseB, menuPhaseDw, (unsigned)submenuMode,
                    (unsigned)crisisLevel, (unsigned)toggle);
        Log::Battle("BattleTTS: [LIMIT-DIAG]   window@0x%08X=[%s]", LIMIT_DIAG_WINDOW_BASE, hex);

        // One-time savemap dump (152 bytes = 0x98). Split across 5 log
        // lines of 32 bytes each so log entries stay readable.
        if (charBase) {
            for (int chunk = 0; chunk < 5; chunk++) {
                char shex[128] = {};
                int sp = 0;
                int base = chunk * 32;
                int end = base + 32;
                if (end > 0x98) end = 0x98;
                __try {
                    for (int i = base; i < end; i++)
                        sp += snprintf(shex + sp, sizeof(shex) - sp, "%02X ", charBase[i]);
                } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
                Log::Battle("BattleTTS: [LIMIT-DIAG]   savemap+0x%02X..0x%02X: %s",
                            base, end - 1, shex);
            }
        }

        memcpy(s_limitDiagPrev, window, sizeof(window));
        s_limitDiagPrevValid = true;
        s_limitDiagPrevActiveChar = activeChar;
        s_limitDiagPrevCmdCursor = cmdCursor;
        s_limitDiagPrevSubCursor = subCursor;
        s_limitDiagPrevMenuPhase = menuPhaseB;
        s_limitDiagPrevMenuDword = menuPhaseDw;
        s_limitDiagPrevSubmenuMode = submenuMode;
        return;
    }

    // STATE line on any high-level state change (cursor / phase / submenu mode).
    bool stateChanged =
        cmdCursor   != s_limitDiagPrevCmdCursor   ||
        subCursor   != s_limitDiagPrevSubCursor   ||
        menuPhaseB  != s_limitDiagPrevMenuPhase   ||
        menuPhaseDw != s_limitDiagPrevMenuDword   ||
        submenuMode != s_limitDiagPrevSubmenuMode;

    if (stateChanged) {
        Log::Battle("BattleTTS: [LIMIT-DIAG] STATE turn=%s cmdCursor=%u->%u subCursor=%u->%u "
                    "phaseB=%u->%u phaseDw=0x%08X->0x%08X submMode=0x%02X->0x%02X toggle=%u",
                    name,
                    (unsigned)s_limitDiagPrevCmdCursor, (unsigned)cmdCursor,
                    (unsigned)s_limitDiagPrevSubCursor, (unsigned)subCursor,
                    (unsigned)s_limitDiagPrevMenuPhase, (unsigned)menuPhaseB,
                    s_limitDiagPrevMenuDword, menuPhaseDw,
                    (unsigned)s_limitDiagPrevSubmenuMode, (unsigned)submenuMode,
                    (unsigned)toggle);
    }

    s_limitDiagPrevCmdCursor = cmdCursor;
    s_limitDiagPrevSubCursor = subCursor;
    s_limitDiagPrevMenuPhase = menuPhaseB;
    s_limitDiagPrevMenuDword = menuPhaseDw;
    s_limitDiagPrevSubmenuMode = submenuMode;

    // Per-byte CHANGE diff against previous sample. Only log while we're
    // in limit mode (latched) to keep volume sane during normal gameplay.
    if (!s_inLimitMode) {
        memcpy(s_limitDiagPrev, window, sizeof(window));
        return;
    }
    int changeCount = 0;
    for (int i = 0; i < LIMIT_DIAG_WINDOW_SIZE && changeCount < 16; i++) {
        if (window[i] == s_limitDiagPrev[i]) continue;
        if (LimitDiag_IsNoisy(i)) continue;
        uint32_t addr = LIMIT_DIAG_WINDOW_BASE + i;
        const char* tag = "";
        if (addr == 0x01D76843) tag = " (cmdCursor)";
        else if (addr == 0x01D7684A) tag = " (limitToggle)";
        Log::Battle("BattleTTS: [LIMIT-DIAG] CHANGE +0x%02X (0x%08X)%s: %u -> %u",
                    i, addr, tag,
                    (unsigned)s_limitDiagPrev[i], (unsigned)window[i]);
        changeCount++;
    }
    memcpy(s_limitDiagPrev, window, sizeof(window));
}

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
// v0.13.46: Mid-battle enemy name cache refresh
// ============================================================================
// Handles bosses that appear mid-battle (e.g. Elvoret after Biggs/Wedge die).
// Tracks previous HP for each enemy slot. When HP transitions from 0 to non-zero
// and the cached name is a generic fallback ("Enemy N"), re-read the real name.
static uint32_t s_enemyNameCachePrevHP[BATTLE_ENEMY_SLOTS] = {};

static void RefreshEnemyNameCache()
{
    if (!s_enemyNameCacheBuilt) return;
    
    bool anyRefreshed = false;
    for (int i = 0; i < BATTLE_ENEMY_SLOTS; i++) {
        int slot = BATTLE_ALLY_SLOTS + i;
        uint32_t hp = GetEntityHP(slot);
        uint32_t prevHP = s_enemyNameCachePrevHP[i];
        s_enemyNameCachePrevHP[i] = hp;
        
        if (prevHP == 0 && hp > 0) {
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "Enemy %d", i + 1);
            if (strcmp(s_enemyNameCache[i], prefix) == 0 || s_enemyNameCache[i][0] == '\0') {
                char newName[64];
                if (GetEnemyName(slot, newName, sizeof(newName)) && newName[0] != '\0') {
                    strncpy(s_enemyNameCache[i], newName, sizeof(s_enemyNameCache[i]) - 1);
                    s_enemyNameCache[i][sizeof(s_enemyNameCache[i]) - 1] = '\0';
                    Log::Battle("BattleTTS: [NAME-CACHE] Refreshed slot%d = \"%s\" (was \"%s\", HP 0->%u)",
                               slot, newName, prefix, hp);
                    anyRefreshed = true;
                }
            }
        }
    }
    
    if (anyRefreshed) {
        char enemyStr[200];
        BuildEnemyNameString(enemyStr, sizeof(enemyStr));
        char buf[256];
        snprintf(buf, sizeof(buf), "%s appeared.", enemyStr);
        BattleSpeakEvent(buf, false);
        Log::Battle("BattleTTS: [NAME-CACHE] Mid-battle refresh: %s", buf);
    }
}

// ============================================================================
