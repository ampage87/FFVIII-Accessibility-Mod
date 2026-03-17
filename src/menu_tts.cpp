// menu_tts.cpp - In-game menu TTS for FF8 Accessibility Mod
//
// ============================================================================
// CURRENT STATE: v0.07.21 — Fix: delayed volume re-apply after hook install
// ============================================================================
//
// Top-level menu cursor: BYTE at pMenuStateA + 0x1E6 (confirmed working)
//   0=Junction, 1=Item, 2=Magic, 3=Status, 4=GF, 5=Ability,
//   6=Switch, 7=Card, 8=Config, 9=Tutorial, 10=Save
//
// v07.06: Title Screen → Continue stays in mode 1. No mode change.
// v07.07: 2048-byte pMenuStateA scan showed only rendering noise.
//   The save/load cursor is NOT in the pMenuStateA region.
//
// v07.08: F12 scans the ff8_win_obj windows array (pWindowsArray).
//   Each window is 0x3C bytes. Key fields:
//     +0x18: win_id, +0x1A: mode1, +0x1C: open_close_transition,
//     +0x24: state, +0x29: first_question, +0x2A: last_question,
//     +0x2B: current_choice_question
//   Dumps all 8 windows every 500ms, looking for active windows
//   with changing current_choice_question during Continue flow.
//   Also scans a 4096-byte region starting 2048 bytes before pMenuStateA
//   to check broader memory for cursor candidates.

#include "ff8_accessibility.h"
#include "menu_tts.h"
#include "field_dialog.h"
#include "ff8_text_decode.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace FF8Addresses;

// ============================================================================
// Cursor offset from pMenuStateA
// ============================================================================
static const int CURSOR_OFFSET = 0x1E6;

// ============================================================================
// Menu item name table (full visual order, 0-based)
// ============================================================================
static const char* MENU_ITEMS[] = {
    "Junction",   // 0
    "Item",       // 1
    "Magic",      // 2
    "Status",     // 3
    "GF",         // 4
    "Ability",    // 5
    "Switch",     // 6
    "Card",       // 7
    "Config",     // 8
    "Tutorial",   // 9
    "Save",       // 10
};
static const int MENU_ITEMS_COUNT = 11;

static const char* GetMenuItemName(uint8_t idx)
{
    if (idx < MENU_ITEMS_COUNT) return MENU_ITEMS[idx];
    return nullptr;
}

// ============================================================================
// State tracking
// ============================================================================
static bool     s_initialized = false;
static bool     s_wasMenuMode = false;
static uint8_t  s_prevCursor = 0xFF;

// Global mode tracking
static uint16_t s_prevGameMode = 0xFFFF;

// F12 diagnostic: track menu_draw_text / get_character_width call counts
static bool     s_diagActive = false;
static DWORD    s_diagLastLogTime = 0;
static int      s_diagScanCount = 0;
static const int DIAG_SCAN_MAX = 60;  // 60 x 500ms = 30 seconds
static LONG     s_diagPrevMDT = 0;   // previous menu_draw_text count
static LONG     s_diagPrevGCW = 0;   // previous get_character_width count

// ============================================================================
// Save screen cursor detection (v07.12)
// ============================================================================
// The save/load screen (Title→Continue, or in-game Save) renders text via
// get_character_width. We poll the GCW buffer, decode it with DecodeMenuText,
// and parse for cursor position from the rendered text patterns.
//
// Text patterns observed:
//   "GAME FOLDER in use: Slot N" — N is the selected slot (1 or 2)
//   "Checking GAME FOLDER" — loading phase
//   "No save data" — empty slot
//   "Slot 1" / "Slot 2" + "FINAL FANTASY" — slot list screen
// ============================================================================

static bool     s_saveScreenActive = false;
static DWORD    s_saveLastPollTime = 0;
static int      s_saveCursorSlot = -1;     // -1 = unknown, 1 or 2
static std::string s_saveScreenPhase;      // "checking", "empty", "list", ""
static int      s_saveGcwZeroCount = 0;    // consecutive polls with no GCW data
static const int SAVE_POLL_INTERVAL = 300; // ms between polls
static const int SAVE_GCW_EXIT_THRESHOLD = 6; // ~1.8s of no data = left save screen

// v07.14 FINDING: GCW text is IDENTICAL regardless of cursor position.
// The cursor is a graphical element (hand icon), not encoded in text.
// v07.15 FINDING: 4KB memory scan around pMenuStateA found no cursor byte.
// Only noise (tick counter, rendering toggles). Cursor is elsewhere in memory.
//
// v07.16 APPROACH: Track arrow key presses to infer cursor position.
// The save slot screen has only 2 items (Slot 1, Slot 2) that wrap.
// We detect up/down presses via GetAsyncKeyState and toggle our own counter.

static bool     s_savePrevUp = false;
static bool     s_savePrevDown = false;
static bool     s_savePrevConfirm = false;
static bool     s_saveAnnouncedInitial = false;

// Announce the current save slot
static void AnnounceSaveSlot()
{
    const char* slotName = (s_saveCursorSlot == 1) ? "Slot 1" : "Slot 2";
    ScreenReader::Speak(slotName, true);
    Log::Write("[MenuTTS] Save slot: %s", slotName);
}

// Poll GCW buffer for save screen text + track arrow keys for cursor
static void PollSaveScreen()
{
    DWORD now = GetTickCount();
    if (now - s_saveLastPollTime < SAVE_POLL_INTERVAL) return;
    s_saveLastPollTime = now;
    
    // Snapshot the GCW buffer
    uint8_t gcwBuf[1024];
    int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
    
    if (gcwLen > 0) {
        s_saveGcwZeroCount = 0;
        
        // Decode and check for save screen markers
        std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
        
        bool isSaveScreen = (decoded.find("GAME FOLDER") != std::string::npos ||
                             (decoded.find("Slot") != std::string::npos &&
                              decoded.find("FINAL FANTASY") != std::string::npos));
        
        if (isSaveScreen) {
            if (!s_saveScreenActive) {
                s_saveScreenActive = true;
                s_saveCursorSlot = 1;  // Default to slot 1
                s_saveAnnouncedInitial = false;
                s_savePrevUp = false;
                s_savePrevDown = false;
                s_savePrevConfirm = false;
                Log::Write("[MenuTTS] Save screen detected");
            }
            
            // Announce initial slot after a brief delay (let screen stabilize)
            if (!s_saveAnnouncedInitial) {
                s_saveAnnouncedInitial = true;
                AnnounceSaveSlot();
            }
            
            // Track arrow keys for cursor movement
            // FF8 uses VK_UP/VK_DOWN for menu navigation
            bool upNow   = (GetAsyncKeyState(VK_UP)   & 0x8000) != 0;
            bool downNow  = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
            
            // Detect rising edge (key just pressed)
            bool upPressed   = upNow && !s_savePrevUp;
            bool downPressed = downNow && !s_savePrevDown;
            
            if (upPressed || downPressed) {
                // Toggle between slot 1 and slot 2 (only 2 slots, they wrap)
                s_saveCursorSlot = (s_saveCursorSlot == 1) ? 2 : 1;
                AnnounceSaveSlot();
            }
            
            s_savePrevUp = upNow;
            s_savePrevDown = downNow;
        }
    } else {
        // No GCW data this poll
        if (s_saveScreenActive) {
            s_saveGcwZeroCount++;
            if (s_saveGcwZeroCount >= SAVE_GCW_EXIT_THRESHOLD) {
                Log::Write("[MenuTTS] Save screen exited (no GCW data for %d polls)",
                           s_saveGcwZeroCount);
                s_saveScreenActive = false;
                s_saveCursorSlot = -1;
                s_saveScreenPhase.clear();
                s_saveGcwZeroCount = 0;
                s_saveAnnouncedInitial = false;
            }
        }
    }
}

// ============================================================================
// Initialize
// ============================================================================
void MenuTTS::Initialize()
{
    Log::Write("[MenuTTS] Initialize() — v0.07.21 delayed volume re-apply");
    
    if (pMenuStateA == nullptr) {
        Log::Write("[MenuTTS] WARNING: pMenuStateA not resolved, menu TTS disabled");
        return;
    }
    
    Log::Write("[MenuTTS] Cursor byte at pMenuStateA + 0x%X = absolute 0x%08X",
               CURSOR_OFFSET, (uint32_t)(uintptr_t)pMenuStateA + CURSOR_OFFSET);
    
    s_initialized = true;
    Log::Write("[MenuTTS] Initialize() complete");
}

// ============================================================================
// Helper: poll menu cursor with SEH protection (separate function to avoid
// C2712 — __try can't coexist with C++ object unwinding in same function)
// ============================================================================
static void PollMenuCursor()
{
    uint8_t* base = (uint8_t*)pMenuStateA;
    
    __try {
        uint8_t cursor = *(base + CURSOR_OFFSET);
        
        if (cursor != s_prevCursor) {
            const char* name = GetMenuItemName(cursor);
            if (name) {
                ScreenReader::Speak(name, true);
                Log::Write("[MenuTTS] Cursor %u -> %u: %s",
                           (unsigned)s_prevCursor, (unsigned)cursor, name);
            } else {
                Log::Write("[MenuTTS] Cursor %u -> %u (unknown index)",
                           (unsigned)s_prevCursor, (unsigned)cursor);
            }
            
            s_prevCursor = cursor;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// Update — called every frame from AccessibilityThread
// ============================================================================
void MenuTTS::Update()
{
    if (!s_initialized) return;
    if (pGameMode == nullptr || pMenuStateA == nullptr) return;
    
    uint16_t mode = *pGameMode;
    bool isMenuMode = (mode == 6);
    
    // ========================================================================
    // GLOBAL MODE TRACKING — fires every frame regardless of isMenuMode
    // ========================================================================
    if (mode != s_prevGameMode) {
        Log::Write("[MenuTTS] === GAME MODE CHANGE: %u -> %u ===",
                   (unsigned)s_prevGameMode, (unsigned)mode);
        s_prevGameMode = mode;
    }
    
    // ========================================================================
    // F12 DIAGNOSTIC: Capture GCW (get_character_width) text during save screen
    // Snapshots the accumulated character codes every 500ms, decodes them,
    // and logs the resulting text. This shows exactly what the save screen renders.
    // ========================================================================
    if (GetAsyncKeyState(VK_F12) & 1) {
        // F12: start GCW text capture diagnostic
        s_diagPrevMDT = FieldDialog::GetMenuDrawTextCallCount();
        s_diagPrevGCW = FieldDialog::GetGetCharWidthCallCount();
        
        uint8_t flushBuf[1024];
        FieldDialog::SnapshotGcwBuffer(flushBuf, sizeof(flushBuf));
        
        Log::Write("[MenuTTS] === F12 GCW TEXT CAPTURE STARTED (mode=%u) ===", (unsigned)mode);
        
        s_diagActive = true;
        s_diagLastLogTime = GetTickCount();
        s_diagScanCount = 0;
        
        ScreenReader::Speak("Text capture started", true);
    }
    
    // Periodic GCW text capture
    if (s_diagActive) {
        DWORD now = GetTickCount();
        
        if (now - s_diagLastLogTime >= 500 && s_diagScanCount < DIAG_SCAN_MAX) {
            s_diagLastLogTime = now;
            s_diagScanCount++;
            
            // Snapshot call counts
            LONG curMDT = FieldDialog::GetMenuDrawTextCallCount();
            LONG curGCW = FieldDialog::GetGetCharWidthCallCount();
            LONG deltaMDT = curMDT - s_diagPrevMDT;
            LONG deltaGCW = curGCW - s_diagPrevGCW;
            s_diagPrevMDT = curMDT;
            s_diagPrevGCW = curGCW;
            
            // Snapshot and decode the GCW buffer
            uint8_t gcwBuf[1024];
            int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
            
            if (gcwLen > 0) {
                // Decode the MENU font glyph indices to UTF-8 (not field dialog encoding)
                std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
                
                // Also log raw hex of first 64 bytes for analysis
                char hexDump[200] = {};
                int hexPos = 0;
                int hexMax = (gcwLen < 64) ? gcwLen : 64;
                for (int i = 0; i < hexMax && hexPos < 190; i++) {
                    hexPos += sprintf(hexDump + hexPos, "%02X ", gcwBuf[i]);
                }
                
                Log::Write("[MenuTTS] Scan %d: MDT+%ld GCW+%ld gcwLen=%d",
                           s_diagScanCount, deltaMDT, deltaGCW, gcwLen);
                Log::Write("[MenuTTS]   hex: %s%s",
                           hexDump, (gcwLen > 64) ? "..." : "");
                Log::Write("[MenuTTS]   text: \"%s\"", decoded.c_str());
            } else if (s_diagScanCount <= 3) {
                Log::Write("[MenuTTS] Scan %d: MDT+%ld GCW+%ld gcwLen=0 (no chars)",
                           s_diagScanCount, deltaMDT, deltaGCW);
            }
            
            if (s_diagScanCount >= DIAG_SCAN_MAX) {
                Log::Write("[MenuTTS] GCW text capture complete (%d scans)",
                           s_diagScanCount);
                s_diagActive = false;
                ScreenReader::Speak("Text capture complete", true);
            }
        }
    }
    
    // ========================================================================
    // MENU MODE TTS — only active during mode 6
    // ========================================================================
    
    // Detect entering menu mode
    if (isMenuMode && !s_wasMenuMode) {
        s_prevCursor = 0xFF;
        Log::Write("[MenuTTS] Menu opened (mode 6)");
    }
    
    // Detect exiting menu mode
    if (!isMenuMode && s_wasMenuMode) {
        Log::Write("[MenuTTS] Menu closed (left mode 6)");
    }
    
    // While in menu mode: poll cursor and announce changes
    if (isMenuMode) {
        PollMenuCursor();
    }
    
    // ========================================================================
    // SAVE SCREEN DETECTION — runs outside mode 6 (save screen is mode 1)
    // ========================================================================
    if (!isMenuMode) {
        PollSaveScreen();
    }
    
    s_wasMenuMode = isMenuMode;
}
