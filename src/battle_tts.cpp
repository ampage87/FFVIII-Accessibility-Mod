// battle_tts.cpp - Battle sequence TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.10.112 — Draw 3-bug fix (target/stock/name)
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

// ============================================================================
// Enemy name diagnostic (v0.10.07)
// ============================================================================

// Computed stats array: 7 x 0x1D0 structs at 0x1CFF000 (separate from entity array)
static const uint32_t BATTLE_COMP_STATS_BASE   = 0x1CFF000;
static const uint32_t BATTLE_COMP_STATS_STRIDE = 0x1D0;  // 464 bytes per slot

// battle_get_monster_name_sub_495100: original game function (FFNx JMP-hooked)
// Signature (guessed from FFNx): char* __cdecl battle_get_monster_name(int slot_index)
static const uint32_t BATTLE_GET_MONSTER_NAME_ADDR = 0x495100;

// battle_get_actor_name_sub_47EAF0: actor name function
static const uint32_t BATTLE_GET_ACTOR_NAME_ADDR = 0x47EAF0;

// Helper: check if a byte range contains printable ASCII (for name scanning)
static bool IsPrintableASCII(const uint8_t* p, int len) {
    if (len < 2) return false;
    for (int i = 0; i < len; i++) {
        if (p[i] == 0) return (i >= 2);  // null terminator, need at least 2 chars
        if (p[i] < 0x20 || p[i] > 0x7E) return false;
    }
    return true;
}

// ============================================================================
// FF8 text decoder (v0.10.08)
// ============================================================================
// FF8 uses proprietary character encoding, NOT ASCII.
// Confirmed mapping from diagnostic (v0.10.07):
//   0x00 = null terminator
//   0x02 = newline
//   0x20 = space (same as ASCII)
//   0x24-0x2D = '0'-'9' (digits: byte - 0x24 + '0') — estimated, not yet confirmed
//   0x45-0x5E = 'A'-'Z' (byte - 4)
//   0x5F-0x78 = 'a'-'z' (byte + 2)
//
// Diagnostic proof:
//   getMonsterName returned "Fgrc Fse" which decodes to "Bite Bug"
//   getActorName returned "Usgqrgq" = "Quistis", "Wos_jj" = "Squall"

static char DecodeFF8Char(uint8_t b)
{
    if (b == 0x00) return '\0';
    if (b == 0x20) return ' ';
    if (b >= 0x45 && b <= 0x5E) return (char)(b - 4);        // A-Z
    if (b >= 0x5F && b <= 0x78) return (char)(b + 2);        // a-z
    if (b >= 0x24 && b <= 0x2D) return (char)(b - 0x24 + '0'); // 0-9 (estimated)
    if (b == 0x2F) return '-';   // dash (estimated)
    if (b == 0x06) return '\'';  // apostrophe (estimated)
    // Unknown byte — return '?' rather than garbage
    return '?';
}

// Decode an FF8-encoded string into a regular C string.
// Returns the number of characters decoded (not counting null terminator).
static int DecodeFF8String(const uint8_t* src, char* dst, int maxLen)
{
    int i = 0;
    for (; i < maxLen - 1; i++) {
        if (src[i] == 0x00) break;
        dst[i] = DecodeFF8Char(src[i]);
    }
    dst[i] = '\0';
    return i;
}

// Get the decoded enemy name for a battle slot (3-6).
// Calls battle_get_monster_name through FFNx's JMP hook.
// Returns empty string on failure.
static bool GetEnemyName(int slot, char* outName, int maxLen)
{
    outName[0] = '\0';
    if (slot < BATTLE_ALLY_SLOTS || slot >= BATTLE_TOTAL_SLOTS) return false;
    if (GetEntityHP(slot) == 0) return false;
    
    typedef char* (__cdecl *GetMonsterNameFn)(int);
    GetMonsterNameFn fn = (GetMonsterNameFn)BATTLE_GET_MONSTER_NAME_ADDR;
    
    __try {
        uint8_t* raw = (uint8_t*)fn(slot);
        if (!raw || (uintptr_t)raw < 0x10000 || (uintptr_t)raw > 0x7FFFFFFF) return false;
        
        __try {
            DecodeFF8String(raw, outName, maxLen);
            return (outName[0] != '\0');
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Build a grouped enemy name string like "2 Bite Bugs" or "Bite Bug, Glacial Eye"
static void BuildEnemyNameString(char* buf, int bufSize)
{
    // Collect names for each active enemy slot
    struct EnemyInfo { char name[64]; int count; };
    EnemyInfo enemies[BATTLE_ENEMY_SLOTS] = {};
    int uniqueCount = 0;
    
    for (int slot = BATTLE_ALLY_SLOTS; slot < BATTLE_TOTAL_SLOTS; slot++) {
        char name[64];
        if (!GetEnemyName(slot, name, sizeof(name))) continue;
        
        // Check if we already have this name
        bool found = false;
        for (int j = 0; j < uniqueCount; j++) {
            if (strcmp(enemies[j].name, name) == 0) {
                enemies[j].count++;
                found = true;
                break;
            }
        }
        if (!found && uniqueCount < BATTLE_ENEMY_SLOTS) {
            strncpy(enemies[uniqueCount].name, name, 63);
            enemies[uniqueCount].name[63] = '\0';
            enemies[uniqueCount].count = 1;
            uniqueCount++;
        }
    }
    
    if (uniqueCount == 0) {
        // Fallback: no names retrieved
        int total = CountActiveEnemies();
        if (total == 1) snprintf(buf, bufSize, "1 enemy");
        else snprintf(buf, bufSize, "%d enemies", total);
        return;
    }
    
    // Build the string
    int pos = 0;
    for (int i = 0; i < uniqueCount; i++) {
        if (i > 0) {
            pos += snprintf(buf + pos, bufSize - pos, ", ");
        }
        if (enemies[i].count > 1) {
            pos += snprintf(buf + pos, bufSize - pos, "%d %ss", enemies[i].count, enemies[i].name);
        } else {
            pos += snprintf(buf + pos, bufSize - pos, "%s", enemies[i].name);
        }
    }
}

// Diagnostic: try all known approaches to read enemy names
static void DiagEnemyNames()
{
    Log::Write("BattleTTS: [NAME-DIAG] === Enemy name diagnostic ===");
    
    // --- Approach 1: Scan computed stats struct (0x1CFF000 + slot*0x1D0) ---
    // Look for printable ASCII strings in each enemy slot's 0x1D0 block
    Log::Write("BattleTTS: [NAME-DIAG] --- Approach 1: Computed stats scan (0x1CFF000) ---");
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint8_t* base = (uint8_t*)(BATTLE_COMP_STATS_BASE + slot * BATTLE_COMP_STATS_STRIDE);
        __try {
            // Scan for ASCII strings at every offset in the struct
            for (uint32_t off = 0; off < BATTLE_COMP_STATS_STRIDE - 4; off++) {
                if (IsPrintableASCII(base + off, 4)) {
                    // Found potential string — read up to 24 chars
                    char name[25] = {};
                    int len = 0;
                    for (int j = 0; j < 24 && base[off + j] >= 0x20 && base[off + j] <= 0x7E; j++) {
                        name[j] = (char)base[off + j];
                        len++;
                    }
                    name[len] = 0;
                    if (len >= 3) {  // only log strings of 3+ chars
                        Log::Write("BattleTTS: [NAME-DIAG] slot%d comp+0x%03X: \"%s\" (len=%d)",
                                   slot, off, name, len);
                    }
                    off += len;  // skip past this string
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [NAME-DIAG] slot%d comp stats EXCEPTION", slot);
        }
    }
    
    // --- Approach 2: Check specific known offsets in computed stats ---
    // The HP values are at +0x172/+0x174 per deep research. Names might be nearby.
    Log::Write("BattleTTS: [NAME-DIAG] --- Approach 2: Comp stats HP check ---");
    for (int slot = BATTLE_ALLY_SLOTS; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint8_t* base = (uint8_t*)(BATTLE_COMP_STATS_BASE + slot * BATTLE_COMP_STATS_STRIDE);
        __try {
            uint16_t hp172 = *(uint16_t*)(base + 0x172);
            uint16_t hp174 = *(uint16_t*)(base + 0x174);
            // Dump first 32 bytes and bytes around 0x170
            char hex1[100] = {}, hex2[100] = {};
            int p1 = 0, p2 = 0;
            for (int b = 0; b < 32; b++)
                p1 += snprintf(hex1 + p1, sizeof(hex1) - p1, "%02X ", base[b]);
            for (int b = 0x168; b < 0x188 && b < (int)BATTLE_COMP_STATS_STRIDE; b++)
                p2 += snprintf(hex2 + p2, sizeof(hex2) - p2, "%02X ", base[b]);
            Log::Write("BattleTTS: [NAME-DIAG] enemy slot%d comp[0x00]: %s", slot, hex1);
            Log::Write("BattleTTS: [NAME-DIAG] enemy slot%d comp[0x168]: %s hp172=%u hp174=%u",
                       slot, hex2, (unsigned)hp172, (unsigned)hp174);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [NAME-DIAG] slot%d comp EXCEPTION", slot);
        }
    }
    
    // --- Approach 3: Try calling battle_get_monster_name via FFNx JMP ---
    Log::Write("BattleTTS: [NAME-DIAG] --- Approach 3: Call 0x495100 (FFNx hooked) ---");
    typedef char* (__cdecl *GetMonsterNameFn)(int);
    GetMonsterNameFn getMonsterName = (GetMonsterNameFn)BATTLE_GET_MONSTER_NAME_ADDR;
    for (int slot = BATTLE_ALLY_SLOTS; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (GetEntityHP(slot) == 0) continue;
        __try {
            char* name = getMonsterName(slot);
            if (name && (uintptr_t)name > 0x10000 && (uintptr_t)name < 0x7FFFFFFF) {
                // Validate it looks like a string
                __try {
                    if (name[0] >= 0x20 && name[0] <= 0x7E) {
                        char safe[64] = {};
                        for (int j = 0; j < 63 && name[j] != 0 && name[j] >= 0x20 && name[j] <= 0x7E; j++)
                            safe[j] = name[j];
                        Log::Write("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X -> \"%s\"",
                                   slot, (uint32_t)(uintptr_t)name, safe);
                    } else {
                        Log::Write("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X (non-ASCII first byte 0x%02X)",
                                   slot, (uint32_t)(uintptr_t)name, (unsigned)(uint8_t)name[0]);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log::Write("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X (access fault)",
                               slot, (uint32_t)(uintptr_t)name);
                }
            } else {
                Log::Write("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X (bad ptr)",
                           slot, name ? (uint32_t)(uintptr_t)name : 0);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [NAME-DIAG] getMonsterName(%d) EXCEPTION calling function", slot);
        }
    }
    
    // --- Approach 4: Try calling battle_get_actor_name ---
    Log::Write("BattleTTS: [NAME-DIAG] --- Approach 4: Call 0x47EAF0 (actor name) ---");
    // Check if this function is hooked (starts with E9 JMP)
    __try {
        uint8_t firstByte = *(uint8_t*)BATTLE_GET_ACTOR_NAME_ADDR;
        Log::Write("BattleTTS: [NAME-DIAG] 0x47EAF0 first byte: 0x%02X (%s)",
                   (unsigned)firstByte, (firstByte == 0xE9) ? "JMP=hooked" : "not hooked");
        
        typedef char* (__cdecl *GetActorNameFn)(int);
        GetActorNameFn getActorName = (GetActorNameFn)BATTLE_GET_ACTOR_NAME_ADDR;
        for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
            if (GetEntityMaxHP(slot) == 0) continue;
            __try {
                char* name = getActorName(slot);
                if (name && (uintptr_t)name > 0x10000 && (uintptr_t)name < 0x7FFFFFFF) {
                    __try {
                        char safe[64] = {};
                        for (int j = 0; j < 63 && name[j] != 0 && name[j] >= 0x20 && name[j] <= 0x7E; j++)
                            safe[j] = name[j];
                        Log::Write("BattleTTS: [NAME-DIAG] getActorName(%d) = 0x%08X -> \"%s\"",
                                   slot, (uint32_t)(uintptr_t)name, safe);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        Log::Write("BattleTTS: [NAME-DIAG] getActorName(%d) = 0x%08X (access fault)",
                                   slot, (uint32_t)(uintptr_t)name);
                    }
                } else {
                    Log::Write("BattleTTS: [NAME-DIAG] getActorName(%d) = 0x%08X (bad/null ptr)",
                               slot, name ? (uint32_t)(uintptr_t)name : 0);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log::Write("BattleTTS: [NAME-DIAG] getActorName(%d) EXCEPTION calling function", slot);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [NAME-DIAG] 0x47EAF0 EXCEPTION reading function");
    }
    
    // --- Approach 5: Scan entity struct 0xD0 block for name-like data ---
    Log::Write("BattleTTS: [NAME-DIAG] --- Approach 5: Entity struct 0xD0 block scan ---");
    for (int slot = BATTLE_ALLY_SLOTS; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (GetEntityHP(slot) == 0) continue;
        uint8_t* blk = GetEntityBlock(slot);
        __try {
            // Dump bytes 0x00-0x07 (pre-HP area), 0x78-0x80 (status area), 0xA0-0xD0 (level/stats area)
            char hex[200];
            int p = 0;
            for (int b = 0xA0; b < (int)BATTLE_ENTITY_STRIDE; b++)
                p += snprintf(hex + p, sizeof(hex) - p, "%02X ", blk[b]);
            Log::Write("BattleTTS: [NAME-DIAG] slot%d entity[0xA0..0xCF]: %s", slot, hex);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [NAME-DIAG] slot%d entity scan EXCEPTION", slot);
        }
    }
    
    // --- Approach 6: Check memory near entity array for name table ---
    // The entity array is at 0x1D27B18, maybe names are stored nearby
    Log::Write("BattleTTS: [NAME-DIAG] --- Approach 6: Name table scan near entity array ---");
    // Scan from entity array end (0x1D27B18 + 7*0xD0 = 0x1D280C8) through +0x400
    uint32_t scanStart = BATTLE_ENTITY_ARRAY_BASE + BATTLE_TOTAL_SLOTS * BATTLE_ENTITY_STRIDE;
    __try {
        for (uint32_t addr = scanStart; addr < scanStart + 0x400; addr++) {
            uint8_t* p = (uint8_t*)addr;
            if (IsPrintableASCII(p, 4)) {
                char name[33] = {};
                int len = 0;
                for (int j = 0; j < 32 && p[j] >= 0x20 && p[j] <= 0x7E; j++) {
                    name[j] = (char)p[j];
                    len++;
                }
                if (len >= 3) {
                    Log::Write("BattleTTS: [NAME-DIAG] @0x%08X: \"%s\" (len=%d)",
                               addr, name, len);
                }
                addr += len;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [NAME-DIAG] post-entity scan EXCEPTION");
    }
    
    Log::Write("BattleTTS: [NAME-DIAG] === End diagnostic ===");
}

// ============================================================================
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
            Log::Write("BattleTTS: [MENU-DIAG] active_char_id resolved: 0x%08X", activeCharAddr);
        } else {
            Log::Write("BattleTTS: [MENU-DIAG] active_char_id bad addr: 0x%08X", activeCharAddr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [MENU-DIAG] EXCEPTION resolving active_char_id");
    }
    
    __try {
        uint32_t newCharAddr = *(uint32_t*)(ADDR_SUB_4BB840 + 0x37);
        if (newCharAddr > 0x00400000 && newCharAddr < 0x7FFFFFFF) {
            s_pNewActiveCharId = (uint8_t*)(uintptr_t)newCharAddr;
            Log::Write("BattleTTS: [MENU-DIAG] new_active_char_id resolved: 0x%08X", newCharAddr);
        } else {
            Log::Write("BattleTTS: [MENU-DIAG] new_active_char_id bad addr: 0x%08X", newCharAddr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [MENU-DIAG] EXCEPTION resolving new_active_char_id");
    }
    
    // v0.10.15: Resolve battle_menu_state from battle_pause_window_sub_4CD350 + 0x29
    __try {
        uint32_t menuStateAddr = *(uint32_t*)(ADDR_BATTLE_PAUSE_WINDOW_SUB + 0x29);
        if (menuStateAddr > 0x00400000 && menuStateAddr < 0x7FFFFFFF) {
            s_pBattleMenuStateByte = (uint8_t*)(uintptr_t)menuStateAddr;
            Log::Write("BattleTTS: [MENU-DIAG] battle_menu_state resolved: 0x%08X", menuStateAddr);
        } else {
            Log::Write("BattleTTS: [MENU-DIAG] battle_menu_state bad addr: 0x%08X", menuStateAddr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [MENU-DIAG] EXCEPTION resolving battle_menu_state");
    }

    Log::Write("BattleTTS: [MENU-DIAG] Wide scan base: 0x%08X (%d bytes), scan2: 0x%08X (%d bytes)",
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
        Log::Write("BattleTTS: [SUBMENU-DIAG] === STATE CHANGE === "
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
        Log::Write("BattleTTS: [SUBMENU-DIAG] First snapshot: scan1=0x%08X(%d) scan2=0x%08X(%d)",
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
                Log::Write("BattleTTS: [SUBMENU-DIAG] R1 +0x%04X (0x%08X): %u -> %u",
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
                    Log::Write("BattleTTS: [SUBMENU-DIAG] R2 +0x%04X (0x%08X): %u -> %u",
                               i, MENU_SCAN2_BASE + i,
                               (unsigned)s_menuSnap2[i], (unsigned)newSnap2[i]);
                    changeCount2++;
                }
            }
        }
        if (changeCount == 0) {
            Log::Write("BattleTTS: [SUBMENU-DIAG] (no byte changes in scanned regions)");
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
            Log::Write("BattleTTS: [CURSOR-HUNT] +0x%03X (0x%08X): %u -> %u (delta=%+d)",
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
            Log::Write("BattleTTS: [LIMIT] Attack -> Limit Break (toggle=%u)", (unsigned)toggle);
        } else if (!s_limitBreakActive && wasLimit) {
            BattleSpeak("Attack", PRIO_MENU, true);
            Log::Write("BattleTTS: [LIMIT] Limit Break -> Attack (toggle=%u)", (unsigned)toggle);
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
    Log::Write("BattleTTS: [NAME-CACHE] Enemy name cache built:");
    for (int i = 0; i < BATTLE_ENEMY_SLOTS; i++) {
        int slot = BATTLE_ALLY_SLOTS + i;
        if (s_enemyNameCache[i][0] != '\0') {
            Log::Write("BattleTTS: [NAME-CACHE]   slot%d = \"%s\" (base=\"%s\")",
                       slot, s_enemyNameCache[i], baseNames[i]);
        }
    }
}

// ============================================================================
// v0.10.34: Damage/healing TTS — display-triggered announcements
// ============================================================================
// HP changes are tracked silently. Damage announcements are triggered when
// the damage display value (0x01D2834A) changes to a new non-zero number,
// meaning the engine is now showing the damage text on screen. This syncs
// TTS with the visual damage display instead of with the early HP computation.
//
// For healing (HP increase, no damage display), a fallback timeout fires.
//
// Address 0x01D2834A (uint16) holds displayed damage value (including overkill).
// Confirmed via v0.10.28 diagnostic.

static const uint32_t BATTLE_DAMAGE_DISPLAY_ADDR = 0x01D2834A; // uint16: last displayed damage value (holds permanently, not a timer)

// v0.10.47: Damage animation active flag at 0x01D280C0.
// Discovered via DSCAN diagnostic (v0.10.46): this byte is 01 when the engine
// is displaying damage numbers on screen, and transitions to 00 when the
// animation completes (~1.4 seconds after HP change). This is our trigger.
static const uint32_t BATTLE_DAMAGE_ANIM_FLAG = 0x01D280C0; // BYTE: 01=animating, 00=done

static uint32_t s_hpPrev[BATTLE_TOTAL_SLOTS] = {};      // previous HP per slot
static uint32_t s_hpMaxPrev[BATTLE_TOTAL_SLOTS] = {};   // previous maxHP (detect population)
static bool s_hpTrackingReady = false;                   // true after first frame of valid HP
static int32_t s_hpAccumDelta[BATTLE_TOTAL_SLOTS] = {};  // accumulated HP delta pending announce
static bool s_hpAccumPending[BATTLE_TOTAL_SLOTS] = {};   // true if accumulated delta awaits announce
static DWORD s_hpFirstPendingTime = 0;                   // GetTickCount when first HP change recorded
static bool s_anyHpPending = false;                      // any slot has pending HP changes
static const DWORD HP_HEAL_TIMEOUT_MS = 1500;            // v0.10.51: shorter heal timeout (no anim flag available)

// v0.10.47: Animation flag trigger — replaces display-to-zero approach.
// We watch BATTLE_DAMAGE_ANIM_FLAG: when it transitions from non-zero to zero,
// the damage number has finished displaying on screen. Flush announcements.
static bool s_damageAnimWasActive = false;               // true while anim flag is non-zero
static DWORD s_damageAnimStartTime = 0;                  // GetTickCount when anim flag first went non-zero
static const DWORD HP_ANIM_TIMEOUT_MS = 4000;            // safety net: max wait for anim flag to clear

// v0.10.42: New-turn flush — when active_char_id changes, the previous action's
// animation is guaranteed complete (engine wouldn't advance otherwise).
// Immediately flush any pending HP announcements on this edge.
static uint8_t s_hpTurnFlushLastChar = 0xFF;             // last active_char_id seen by HP tracker

// v0.10.42: New-turn flush — when active_char_id transitions, the previous
// action animation is guaranteed complete (engine wouldn't hand control to the
// next character otherwise). Immediately flush any pending HP announcements.
// This solves the EWM display-timer-frozen problem: the display countdown can't
// reach zero because EWM freezes the ATB function that drives it, but by the
// time the next turn starts the animation is visually done.
static uint8_t s_hpTrackLastActiveChar = 0xFF;           // track turn transitions for flush

// Get a name for any battle slot (allies 0-2, enemies 3-6)
// Uses the persistent name cache for enemies (survives KO).
static const char* GetSlotName(int slot, char* nameBuf, int bufSize)
{
    if (slot < BATTLE_ALLY_SLOTS) {
        return GetBattleCharName((uint8_t)slot);
    } else {
        int idx = slot - BATTLE_ALLY_SLOTS;
        if (idx >= 0 && idx < BATTLE_ENEMY_SLOTS && s_enemyNameCacheBuilt && s_enemyNameCache[idx][0] != '\0') {
            return s_enemyNameCache[idx];
        }
        // Fallback: try live read (might fail for KO'd enemies)
        if (GetEnemyName(slot, nameBuf, bufSize)) return nameBuf;
        snprintf(nameBuf, bufSize, "Enemy %d", slot - BATTLE_ALLY_SLOTS + 1);
        return nameBuf;
    }
}

// ============================================================================
// v0.10.35: Party HP & Status check keys (1/2/3 = individual, H = full party)
// ============================================================================

static bool s_hpKey1WasDown = false;
static bool s_hpKey2WasDown = false;
static bool s_hpKey3WasDown = false;
static bool s_hpKeyHWasDown = false;

// Build a status effect string from the entity's persistent + timed status bytes.
// Returns the number of effects found. Empty string if none.
#define APPEND_STATUS(name_str) do { \
    if (count > 0 && pos < bufSize - 2) pos += snprintf(buf + pos, bufSize - pos, ", "); \
    pos += snprintf(buf + pos, bufSize - pos, "%s", name_str); \
    count++; \
} while(0)

static int BuildStatusString(int slot, char* buf, int bufSize)
{
    buf[0] = '\0';
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return 0;
    
    int count = 0;
    int pos = 0;
    
    __try {
        uint8_t persist = *(blk + BENT_PERSIST_STATUS);
        uint8_t timed0  = *(blk + BENT_TIMED_STATUS_0);
        uint8_t timed1  = *(blk + BENT_TIMED_STATUS_1);
        uint8_t timed2  = *(blk + BENT_TIMED_STATUS_2);
        uint8_t timed3  = *(blk + BENT_TIMED_STATUS_3);
        
        // Persistent statuses (0x78)
        if (persist & 0x01) APPEND_STATUS("KO");
        if (persist & 0x02) APPEND_STATUS("Poison");
        if (persist & 0x04) APPEND_STATUS("Petrify");
        if (persist & 0x08) APPEND_STATUS("Blind");
        if (persist & 0x10) APPEND_STATUS("Silence");
        if (persist & 0x20) APPEND_STATUS("Berserk");
        if (persist & 0x40) APPEND_STATUS("Zombie");
        
        // Timed statuses (0x00)
        if (timed0 & 0x01) APPEND_STATUS("Sleep");
        if (timed0 & 0x02) APPEND_STATUS("Haste");
        if (timed0 & 0x04) APPEND_STATUS("Slow");
        if (timed0 & 0x08) APPEND_STATUS("Stop");
        if (timed0 & 0x10) APPEND_STATUS("Regen");
        if (timed0 & 0x20) APPEND_STATUS("Protect");
        if (timed0 & 0x40) APPEND_STATUS("Shell");
        if (timed0 & 0x80) APPEND_STATUS("Reflect");
        
        // Timed statuses (0x01)
        if (timed1 & 0x01) APPEND_STATUS("Aura");
        if (timed1 & 0x02) APPEND_STATUS("Curse");
        if (timed1 & 0x04) APPEND_STATUS("Doom");
        if (timed1 & 0x10) APPEND_STATUS("Gradual Petrify");
        if (timed1 & 0x20) APPEND_STATUS("Float");
        if (timed1 & 0x40) APPEND_STATUS("Confuse");
        
        // Timed statuses (0x02)
        if (timed2 & 0x04) APPEND_STATUS("Double");
        if (timed2 & 0x08) APPEND_STATUS("Triple");
        
        // Timed statuses (0x03)
        if (timed3 & 0x02) APPEND_STATUS("Angel Wing");
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    return count;
}

#undef APPEND_STATUS

// ============================================================================
// v0.10.93: GF summoning state helpers
// ============================================================================
// When a GF is being summoned, it takes damage in place of the character.
// The GF's HP is displayed overlaid on the character's HP bar.
// Address 0x01D76998 holds a pointer to the active GF's savemap struct.
// GF savemap struct: name at +0x00 (12 bytes FF8-encoded), HP at +0x12 (uint16).

static const uint32_t GF_ACTIVE_SAVEMAP_PTR = 0x01D76998;  // uint32 pointer to GF savemap struct
static const uint32_t GF_SLOT_ADDR          = 0x01D76970;  // int8: which party slot summoned
static const uint32_t GF_ACTIVE_FLAG_ADDR   = 0x01D76971;  // uint8: 1 during loading+animation
static const uint32_t GF_DISPLAY_TIMER_ADDR = 0x01D769D6;  // uint8: countdown, 0=animation phase

// Check if a party slot is currently summoning a GF.
// v0.10.95: Uses per-character entity+0x7C flag instead of global gfSlot.
static bool IsSlotSummoningGF(int partySlot)
{
    if (partySlot < 0 || partySlot >= BATTLE_ALLY_SLOTS) return false;
    __try {
        uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + partySlot * BATTLE_ENTITY_STRIDE);
        uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
        return (gfFlag != 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Check if a GF animation is playing (active flag set, timer at 0).
// v0.10.93 fix: If our code patch is blocking the fire, the animation ISN'T
// playing — the GF is loaded but waiting to fire. Only return true when the
// fire has actually happened (patch restored) and the animation is running.
// We check the actual byte at 0x004B04B4 rather than s_gfFirePatched (declared later).
static bool IsGFAnimationPlaying(void)
{
    // If our EWM code patch is active (byte == 0xC3 RET), fire is blocked — no animation yet
    __try {
        uint8_t patchByte = *(uint8_t*)0x004B04B4;
        if (patchByte == 0xC3) return false;  // fire blocked by our patch
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    __try {
        uint8_t gfActive = *(uint8_t*)GF_ACTIVE_FLAG_ADDR;
        if (gfActive != 1) return false;
        uint8_t timer = *(uint8_t*)GF_DISPLAY_TIMER_ADDR;
        return (timer == 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Savemap GF section: 16 GFs, each 0x44 bytes, starting at 0x1CFDCA8.
// Name at +0x00 (12 bytes, FF8-encoded). HP at +0x12 (uint16). Exists at +0x11.
static const uint32_t SAVEMAP_GF_BASE   = 0x1CFDCA8;
static const uint32_t SAVEMAP_GF_STRIDE = 0x44;

// Read the summoning GF's name and HP for a specific party slot.
// v0.10.95: Takes partySlot parameter instead of reading global gfSlot.
// Approach 1: Runtime pointer (only valid when this slot matches gfSlot during fire/animation)
// Approach 2: Character junction lookup — find which GFs are junctioned to this character,
//             then pick the first one with HP > 0. Works reliably during loading phase.
static bool GetActiveGFInfo(int partySlot, char* nameOut, int nameMax, uint16_t* hpOut)
{
    nameOut[0] = '\0';
    *hpOut = 0;
    if (partySlot < 0 || partySlot >= BATTLE_ALLY_SLOTS) return false;
    
    // Approach 1: Runtime pointer (only valid during fire/animation phase AND for gfSlot)
    __try {
        int8_t gfSlot = *(int8_t*)GF_SLOT_ADDR;
        if (gfSlot == partySlot) {
            uint32_t gfPtr = *(uint32_t*)GF_ACTIVE_SAVEMAP_PTR;
            if (gfPtr >= 0x01000000 && gfPtr <= 0x7FFFFFFF) {
                uint8_t* gfBase = (uint8_t*)(uintptr_t)gfPtr;
                __try {
                    DecodeFF8String(gfBase + 0x00, nameOut, nameMax);
                    *hpOut = *(uint16_t*)(gfBase + 0x12);
                    if (nameOut[0] != '\0') return true;
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    // Approach 2: Character junction lookup using the specified partySlot
    __try {
        uint8_t charIdx = *(uint8_t*)(0x1CFE74C + partySlot);  // SAVEMAP_PARTY_FORMATION
        if (charIdx >= 8) return false;
        
        uint8_t* charBase = (uint8_t*)(0x1CFE0E8 + charIdx * 0x98);  // SAVEMAP_CHAR_DATA_BASE
        uint16_t gfMask = *(uint16_t*)(charBase + 0x58);
        
        for (int gfIdx = 0; gfIdx < 16; gfIdx++) {
            if (!(gfMask & (1 << gfIdx))) continue;
            
            uint8_t* gfBase = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
            uint16_t hp = *(uint16_t*)(gfBase + 0x12);
            if (hp > 0) {
                DecodeFF8String(gfBase, nameOut, nameMax);
                *hpOut = hp;
                Log::Write("BattleTTS: [HP-CHECK] GF junction lookup: slot=%d charIdx=%d gfIdx=%d name='%s' hp=%u mask=0x%04X",
                           partySlot, (int)charIdx, gfIdx, nameOut, (unsigned)hp, (unsigned)gfMask);
                return (nameOut[0] != '\0');
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    return false;
}

// Announce HP + status for a single party slot (0-2). Uses Channel 1 (interrupts menu speech).
// v0.10.93: If the slot is summoning a GF, announce GF name + HP instead.
static void AnnouncePartyMemberHP(int partySlot)
{
    if (partySlot < 0 || partySlot >= BATTLE_ALLY_SLOTS) return;
    
    // v0.10.95: Check if this slot is summoning a GF (per-character flag)
    if (IsSlotSummoningGF(partySlot)) {
        char gfName[64];
        uint16_t gfHP = 0;
        if (GetActiveGFInfo(partySlot, gfName, sizeof(gfName), &gfHP)) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s: %u HP.", gfName, (unsigned)gfHP);
            BattleSpeakEvent(buf);
            Log::Write("BattleTTS: [HP-CHECK] GF substitution: slot%d -> %s", partySlot, buf);
            return;
        }
    }
    
    uint32_t maxHP = GetEntityMaxHP(partySlot);
    if (maxHP == 0) {
        // Slot not populated
        char buf[64];
        snprintf(buf, sizeof(buf), "Party slot %d: empty.", partySlot + 1);
        BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued
        return;
    }
    
    const char* name = GetBattleCharName((uint8_t)partySlot);
    uint32_t curHP = GetEntityHP(partySlot);
    
    char statusBuf[256];
    int statusCount = BuildStatusString(partySlot, statusBuf, sizeof(statusBuf));
    
    char buf[384];
    if (statusCount > 0) {
        snprintf(buf, sizeof(buf), "%s: %u of %u HP. %s.", name, curHP, maxHP, statusBuf);
    } else {
        snprintf(buf, sizeof(buf), "%s: %u of %u HP.", name, curHP, maxHP);
    }
    BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued
    Log::Write("BattleTTS: [HP-CHECK] %s", buf);
}

// Announce HP for all active party members. v0.10.44: Ch2 event.
static void AnnounceFullPartyHP()
{
    char buf[512];
    int pos = 0;
    int memberCount = 0;
    
    for (int slot = 0; slot < BATTLE_ALLY_SLOTS; slot++) {
        uint32_t maxHP = GetEntityMaxHP(slot);
        if (maxHP == 0) continue;
        
        const char* name = GetBattleCharName((uint8_t)slot);
        uint32_t curHP = GetEntityHP(slot);
        
        if (memberCount > 0 && pos < (int)sizeof(buf) - 2)
            pos += snprintf(buf + pos, sizeof(buf) - pos, ". ");
        
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s %u of %u", name, curHP, maxHP);
        memberCount++;
    }
    
    if (memberCount == 0) {
        BattleSpeakEvent("No party members.");
        return;
    }
    
    if (pos < (int)sizeof(buf) - 1) buf[pos++] = '.';
    buf[pos] = '\0';
    
    BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued
    Log::Write("BattleTTS: [HP-CHECK] Party: %s", buf);
}

static void PollHPCheckKeys()
{
    bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
    bool key2 = (GetAsyncKeyState('2') & 0x8000) != 0;
    bool key3 = (GetAsyncKeyState('3') & 0x8000) != 0;
    bool keyH = (GetAsyncKeyState('H') & 0x8000) != 0;
    
    if (key1 && !s_hpKey1WasDown) AnnouncePartyMemberHP(0);
    if (key2 && !s_hpKey2WasDown) AnnouncePartyMemberHP(1);
    if (key3 && !s_hpKey3WasDown) AnnouncePartyMemberHP(2);
    if (keyH && !s_hpKeyHWasDown) AnnounceFullPartyHP();
    
    s_hpKey1WasDown = key1;
    s_hpKey2WasDown = key2;
    s_hpKey3WasDown = key3;
    s_hpKeyHWasDown = keyH;
}

// ============================================================================
// v0.10.97: Target selection TTS — function body (after GetSlotName)
// ============================================================================
static void PollTargetSelection()
{
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (activeChar >= 3) {
        // No turn active — reset
        if (s_inTargetSelect) {
            s_inTargetSelect = false;
            Log::Write("BattleTTS: [TARGET] Exited target select (turn ended)");
        }
        s_lastTargetBitmask = 0;
        return;
    }
    
    uint8_t tgtMask = 0;
    uint8_t tgtScope = 0;
    __try { tgtMask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    __try { tgtScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    // Re-announce if either the bitmask OR the scope changed
    // (scope change = player toggled single/all targeting on same cursor position)
    if (tgtMask == s_lastTargetBitmask && tgtScope == s_lastTargetScope) return;
    
    if (tgtMask == 0) {
        s_lastTargetBitmask = tgtMask;
        s_lastTargetScope = tgtScope;
        if (s_inTargetSelect) {
            s_inTargetSelect = false;
            Log::Write("BattleTTS: [TARGET] Exited target select (mask=0)");
        }
        return;
    }
    
    // Debounce rapid changes — do NOT update tracking vars here.
    DWORD now = GetTickCount();
    if (now - s_targetLastAnnounceTick < TARGET_DEBOUNCE_MS) return;
    s_targetLastAnnounceTick = now;
    s_lastTargetBitmask = tgtMask;
    s_lastTargetScope = tgtScope;
    
    // v0.10.100: Use scope byte at 0x01D76883 to detect all-target vs single-target.
    // Observed values: 3 = single target (Attack), 1 = all enemies (GF).
    // When scope != 3, the action targets all entities on that side.
    // v0.10.112: Also require multi-bit mask for all-target. Draw uses scope=1
    // with a single-bit mask, which should announce the single enemy name.
    bool isAllTarget = (tgtScope != 3 && tgtScope != 0 && CountBits(tgtMask) > 1);
    
    char buf[128];
    
    if (isAllTarget) {
        // All-target: determine which side based on the cursor bitmask
        int slot = BitmaskToSlot(tgtMask);
        if (slot >= BATTLE_ALLY_SLOTS) {
            snprintf(buf, sizeof(buf), "All enemies");
        } else {
            snprintf(buf, sizeof(buf), "All allies");
        }
    } else if (CountBits(tgtMask) == 1) {
        // Single target
        int slot = BitmaskToSlot(tgtMask);
        if (slot < 0) return;
        char nameBuf[64];
        const char* name = GetSlotName(slot, nameBuf, sizeof(nameBuf));
        snprintf(buf, sizeof(buf), "%s", name);
    } else {
        // Multi-bit bitmask (fallback for multi-target with scope=3)
        bool hasAllies = (tgtMask & 0x07) != 0;
        bool hasEnemies = (tgtMask & 0x78) != 0;
        if (hasEnemies && !hasAllies) {
            snprintf(buf, sizeof(buf), "All enemies");
        } else if (hasAllies && !hasEnemies) {
            snprintf(buf, sizeof(buf), "All allies");
        } else {
            snprintf(buf, sizeof(buf), "All targets");
        }
    }
    
    BattleSpeak(buf, PRIO_MENU, true);
    
    if (!s_inTargetSelect) {
        s_inTargetSelect = true;
        Log::Write("BattleTTS: [TARGET] Entered target select: mask=0x%02X scope=%u -> %s", (unsigned)tgtMask, (unsigned)tgtScope, buf);
    } else {
        Log::Write("BattleTTS: [TARGET] Target changed: mask=0x%02X scope=%u -> %s", (unsigned)tgtMask, (unsigned)tgtScope, buf);
    }
}

// Flush all pending HP change announcements (damage and healing).
// v0.10.40: Back to Channel 2 (independent SAPI voice for damage/events).
// Each slot with accumulated HP changes gets its own individual announcement.
// Per-slot deltas ensure multi-target attacks announce each target separately.
static void FlushHPAnnouncements(const char* trigger)
{
    // v0.10.47: Read the engine's displayed damage value for overkill correction.
    // 0x01D2834A holds the actual damage number shown on screen, which includes
    // overkill (damage exceeding remaining HP). Our HP delta is capped at remaining HP.
    // Use max(abs(delta), displayValue) to announce the correct number.
    uint16_t displayVal = 0;
    __try { displayVal = *(uint16_t*)BATTLE_DAMAGE_DISPLAY_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (!s_hpAccumPending[slot]) continue;
        
        int32_t accum = s_hpAccumDelta[slot];
        s_hpAccumDelta[slot] = 0;
        s_hpAccumPending[slot] = false;
        
        if (accum == 0) continue;  // net zero
        
        char nameBuf[64];
        const char* name = GetSlotName(slot, nameBuf, sizeof(nameBuf));
        char buf[256];
        
        if (accum < 0) {
            // Damage — use max(abs(delta), displayValue) for overkill correction
            uint32_t deltaDmg = (uint32_t)(-accum);
            uint32_t dmg = deltaDmg;
            if ((uint32_t)displayVal > dmg) {
                dmg = (uint32_t)displayVal;
            }
            uint32_t nowHP = GetEntityHP(slot);
            bool ko = (nowHP == 0);
            
            if (ko) {
                snprintf(buf, sizeof(buf), "%s takes %u damage. Defeated.", name, dmg);
            } else {
                snprintf(buf, sizeof(buf), "%s takes %u damage.", name, dmg);
            }
            BattleSpeakEvent(buf);
            Log::Write("BattleTTS: [HP-TRACK] %s (slot%d, hpDelta=%d, display=%u, used=%u, hp=%u/%u, trigger=%s)",
                       buf, slot, accum, (unsigned)displayVal, dmg,
                       GetEntityHP(slot), GetEntityMaxHP(slot), trigger);
        } else {
            // Healing — use max(delta, displayValue) for overcapped heals
            uint32_t healAmt = (uint32_t)accum;
            if ((uint32_t)displayVal > healAmt) {
                healAmt = (uint32_t)displayVal;
            }
            snprintf(buf, sizeof(buf), "%s recovers %u HP.", name, healAmt);
            BattleSpeakEvent(buf);
            Log::Write("BattleTTS: [HP-TRACK] %s (slot%d, delta=+%d, display=%u, used=%u, hp=%u/%u, trigger=%s)",
                       buf, slot, accum, (unsigned)displayVal, healAmt,
                       GetEntityHP(slot), GetEntityMaxHP(slot), trigger);
        }
    }
    s_anyHpPending = false;
}

static void PollHPChanges()
{
    if (!s_initAnnounceDone || !s_enemyAnnounceDone) return;
    
    // First call: populate baseline HP values, don't announce
    if (!s_hpTrackingReady) {
        bool anyValid = false;
        for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
            s_hpPrev[i] = GetEntityHP(i);
            s_hpMaxPrev[i] = GetEntityMaxHP(i);
            s_hpAccumDelta[i] = 0;
            s_hpAccumPending[i] = false;
            if (s_hpMaxPrev[i] > 0) anyValid = true;
        }
        if (anyValid) {
            s_hpTrackingReady = true;
            s_anyHpPending = false;
            s_damageAnimWasActive = false;
            Log::Write("BattleTTS: [HP-TRACK] Baseline captured");
        }
        return;
    }
    
    DWORD now = GetTickCount();
    
    // --- Step 1: Detect HP changes silently (don't announce yet) ---
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint32_t curHP = GetEntityHP(slot);
        uint32_t curMaxHP = GetEntityMaxHP(slot);
        uint32_t prevHP = s_hpPrev[slot];
        
        if (s_hpMaxPrev[slot] == 0 && curMaxHP > 0) {
            s_hpPrev[slot] = curHP;
            s_hpMaxPrev[slot] = curMaxHP;
            continue;
        }
        if (curMaxHP == 0) {
            s_hpPrev[slot] = 0;
            s_hpMaxPrev[slot] = 0;
            continue;
        }
        
        if (curHP != prevHP) {
            int32_t delta = (int32_t)curHP - (int32_t)prevHP;
            s_hpAccumDelta[slot] += delta;
            s_hpAccumPending[slot] = true;
            if (!s_anyHpPending) {
                s_anyHpPending = true;
                s_hpFirstPendingTime = now;
            }
            Log::Write("BattleTTS: [HP-TRACK] slot%d HP %u->%u delta=%d (silent, awaiting display)",
                       slot, prevHP, curHP, delta);
            s_hpPrev[slot] = curHP;
        }
        s_hpMaxPrev[slot] = curMaxHP;
    }
    
    // --- Step 1b: New-turn flush (v0.10.42, fixed v0.10.43) ---
    // When active_char_id transitions TO a valid player slot (0-2), the previous
    // action's animation is guaranteed complete — the engine wouldn't hand control
    // to a new character while an animation is still playing.
    //
    // CRITICAL: Do NOT flush when transitioning TO 0xFF. That transition means
    // the player just confirmed a command — HP changes were computed instantly
    // but the attack animation is ABOUT TO START, not finished.
    //
    // Valid flush transitions:
    //   0xFF → 0/1/2  (enemy attacks done, next player turn starting)
    //   0/1/2 → different 0/1/2  (immediate turn handoff, previous anim done)
    // Invalid (do not flush):
    //   0/1/2 → 0xFF  (command confirmed, animation starting)
    {
        uint8_t curActiveChar = 0xFF;
        if (s_pActiveCharId) {
            __try { curActiveChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (curActiveChar != s_hpTrackLastActiveChar) {
            // Only flush when transitioning TO a valid player slot
            bool flushNow = (curActiveChar < 3 && s_hpTrackLastActiveChar != curActiveChar);
            if (flushNow && s_anyHpPending) {
                Log::Write("BattleTTS: [HP-TRACK] New turn flush (char %u->%u)",
                           (unsigned)s_hpTrackLastActiveChar, (unsigned)curActiveChar);
                s_damageAnimWasActive = false;
                FlushHPAnnouncements("new-turn");
            }
            s_hpTrackLastActiveChar = curActiveChar;
        }
    }
    
    // --- Step 2: Animation flag trigger (v0.10.47) ---
    // Watch 0x01D280C0: the engine sets this to 01 when damage numbers are being
    // displayed on screen, and clears it to 00 when the animation completes.
    // When it transitions from non-zero to zero, flush pending announcements.
    // This replaces the broken display-to-zero approach (0x01D2834A never clears).
    if (s_anyHpPending) {
        uint8_t animFlag = 0;
        __try { animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        bool healTimeout = (now - s_hpFirstPendingTime >= HP_HEAL_TIMEOUT_MS);
        
        if (animFlag != 0 && !s_damageAnimWasActive) {
            // Flag just went active — damage number appearing on screen
            s_damageAnimWasActive = true;
            s_damageAnimStartTime = now;
            Log::Write("BattleTTS: [HP-TRACK] Anim flag active (0x%08X=%u)",
                       BATTLE_DAMAGE_ANIM_FLAG, (unsigned)animFlag);
        }
        
        if (animFlag == 0 && s_damageAnimWasActive) {
            // Flag cleared — damage number animation finished, announce now!
            s_damageAnimWasActive = false;
            Log::Write("BattleTTS: [HP-TRACK] Anim flag cleared after %ums",
                       (unsigned)(now - s_damageAnimStartTime));
            FlushHPAnnouncements("anim-done");
        } else if (s_damageAnimWasActive && (now - s_damageAnimStartTime >= HP_ANIM_TIMEOUT_MS)) {
            // Safety net: flag stuck non-zero too long
            s_damageAnimWasActive = false;
            FlushHPAnnouncements("anim-timeout");
        } else if (!s_damageAnimWasActive && healTimeout) {
            // Fallback for healing (no damage display) or if flag never activated
            FlushHPAnnouncements("heal-timeout");
        }
    } else {
        // No pending HP changes — reset tracking
        s_damageAnimWasActive = false;
    }
}

// ============================================================================
// v0.10.25/38/55: Enhanced Wait Mode (EWM) — ATB capping
// ============================================================================
// Prevents actions from triggering while the player is choosing a command,
// without altering the Speed-based turn economy.
//
// v0.10.55: ATB CAPPING replaces the previous full-freeze approach.
// Instead of skipping the ATB update function entirely (which gave players
// extra turns by removing decision-time costs), the hook now:
//   1. Always calls the original ATB update function (gauges fill normally)
//   2. After it returns, caps every entity's ATB at max-1 (except the
//      active deciding character, whose ATB is already at max)
// This means:
//   - Nobody can trigger a new action while the player reads TTS output
//   - ATB fills at correct Speed-based rates → turn ratios are preserved
//   - When the cap lifts, the entity closest to max triggers next (within 1 tick)
//   - No gameplay advantage from slow decision-making
//
// Hook target: ATB update function at 0x004842B0 (MinHook intercept).
// Discovery: v0.10.36-37 via hardware write BP on entity ATB.
// The function handles status timer decrements AND ATB increments
// for all 7 entity slots (loop with add esi,0xD0 stride).
//
// The hook runs on the GAME THREAD (called from the battle main loop).
// s_ewmShouldCap / s_ewmCapExcludeSlot are set by our mod thread
// (Update/PollToggle) and read by the hook.
//
// Toggle: "O" key (works in all game modes, not just battle).
// Persistence: ewm_config.txt in mod root ("1"=on, "0"=off). Default: on.

static const uint32_t ATB_UPDATE_FUNC_ADDR = 0x004842B0;  // confirmed function entry

typedef void (__cdecl *ATBUpdateFn)(void);
static ATBUpdateFn s_originalATBUpdate = nullptr;  // trampoline to original function
static bool s_ewmHookInstalled = false;            // true after MinHook setup

// Volatile flags: set by mod thread, read by game thread hook.
// When s_ewmShouldCap is true, ATB runs normally but all entities (except
// the deciding character) are capped at ATB_max - 1, preventing any new
// turn from triggering. This preserves Speed-based turn ratios while
// giving the player time to read TTS output without being attacked.
static volatile bool s_ewmShouldCap = false;
static volatile uint8_t s_ewmCapExcludeSlot = 0xFF;  // slot to exclude from capping (the deciding character)
// ============================================================================
// v0.10.64: GF timer function hook (MinHook sandwich)
// ============================================================================
// GF loading countdown at 0x01D769D6 is driven by a dedicated function at
// 0x004B0500. Discovery: v0.10.63 hardware write BP on 0x01D769D6 → write
// instruction at 0x004B063B (EIP=0x004B063F), function entry scan found
// SUB ESP,14 / PUSH EBX/EBP/ESI/EDI / MOV EBP,0x01D76971 at 0x004B0500.
//
// When EWM cap is active (player is deciding), we skip the GF timer function
// entirely. The countdown freezes in place and can't reach 0 (fire).
// When the cap releases, the function runs normally and the timer resumes.
// This prevents risk-free GF loading during decision windows.

static const uint32_t GF_TIMER_FUNC_ADDR = 0x004B0500;  // confirmed function entry

// v0.10.78-80: 0x1D0 stride constants REMOVED (v0.10.88).
// Deep research round 2 proved: 0x01D27D94 = Enemy 1 ATB (3*0xD0+0x0C), NOT a GF timer.
// The GF timer is a separate SpeedMod-only countdown in the 0x01D768xx region.

typedef void (__cdecl *GFTimerFn)(void);
static GFTimerFn s_originalGFTimerUpdate = nullptr;  // trampoline to original
static bool s_gfTimerHookInstalled = false;

static volatile bool s_ewmCapGF = false;  // true while player is deciding

// Forward declarations (defined in FFNx hook section below)
static bool s_ffnxGFHookInstalled;
static volatile LONG s_ffnxHookCallCount;

// v0.10.95: Per-slot GF max inflation state (used by HookedATBUpdate on game thread)
// Indexed by party slot (0-2). Each slot independently tracks whether its
// compStats+0x16 has been inflated to 0xFFFF to prevent GF fire.
static bool s_gfMaxInflated[BATTLE_ALLY_SLOTS] = {};    // true while +0x16 is set to 0xFFFF
static uint16_t s_gfRealMax[BATTLE_ALLY_SLOTS] = {};     // saved real max value to restore later

// v0.10.81: GF active flag hiding
static bool s_gfFlagHidden = false;
static uint8_t s_gfSavedSlot = 0xFF;
// v0.10.86: Sticky hide — once set, stays true until player executes a command.
// This prevents the flickering that broke ATB in v0.10.81.
static bool s_gfStickyHidden = false;

// v0.10.85: CODE PATCH — RET at the fire handler entry.
// v0.10.84 proved: patching the immediate value (5→3) still fires because the
// fire setup code at 0x004B04BE+ executes regardless of what state68 is set to.
// Fix: patch the OPCODE at 0x004B04B4 from C7 (MOV) to C3 (RET). This makes
// the state machine handler return immediately before any fire setup code runs.
// The state machine stays in state 3 (loading) until we unpatch.
// When cap releases, restore C7 and the GF fires on the next state machine tick.
static const uint32_t GF_FIRE_PATCH_ADDR = 0x004B04B4; // opcode byte of MOV [state68], 5
static const uint8_t  GF_FIRE_VALUE     = 0xC7;        // original: C7 = MOV opcode
static const uint8_t  GF_SAFE_VALUE     = 0xC3;        // patched: C3 = RET (skip fire setup)
static bool s_gfFirePatched = false;                    // true while byte is patched to RET
static bool s_gfFirePatchReady = false;                 // true after VirtualProtect succeeded

// v0.10.89: GF effect function pointer table.
// Deep research says 0xC81774 holds a table of function pointers, one per GF.
// When the engine is about to fire a GF summon, it reads table[gfId] to get
// the effect function, then calls it. A hardware READ BP on the relevant entry
// will catch the exact code path that dispatches the fire.
// Standard GF ordering: Quezacotl=0, Shiva=1, Ifrit=2, Siren=3, ...
static const uint32_t GF_EFFECT_TABLE_BASE = 0xC81774;
static const int GF_EFFECT_TABLE_ENTRIES = 16;
static const int GF_EFFECT_ENTRY_SIZE = 4;  // uint32 function pointers

// v0.10.88: Per-frame GF timer scan.
// Runs inside HookedATBUpdate (game thread) to catch the GF countdown decrement.
// Scans 0x01D76860-0x01D769DF as 2-byte values, logging any that decrement
// by 1-4 per call (matches SpeedMod 1-3 at ~60 calls/sec = ~15 game ticks/sec).
static const uint32_t GF_SCAN_BASE = 0x01D76860;
static const int GF_SCAN_BYTES = 384;  // covers 0x01D76860-0x01D769DF
static uint8_t s_gfScanSnap[384] = {};  // previous frame's bytes
static bool s_gfScanValid = false;      // true after first snapshot
static int s_gfScanLogCount = 0;        // limit logging
static const int GF_SCAN_LOG_MAX = 200; // max log lines per battle

// v0.10.65: Diagnostic counters for GF timer hook
static volatile LONG s_gfHookCallCount = 0;      // total calls to hooked function
static volatile LONG s_gfHookSkipCount = 0;      // calls skipped (cap active)
static volatile LONG s_gfHookPassCount = 0;      // calls passed through (cap inactive)
static DWORD s_gfHookLastLogTick = 0;

// GF state machine byte at 0x01D76868 controls GF readiness.
// Value 5 = "GF ready to fire". The battle loop reads this independently
// of the hooked function at 0x004B0500 and triggers the GF animation.
// We must clamp state68 to a non-fire value while capped.
static const uint32_t GF_STATE68_ADDR = 0x01D76868;
static const uint8_t  GF_STATE_FIRE   = 5;   // state value that triggers fire
static const uint8_t  GF_STATE_SAFE   = 3;   // safe "loading" state value
static uint8_t s_gfSavedState68 = 0xFF;       // real state68 value (saved when we clamp)
static bool s_gfState68Clamped = false;        // true while we're holding state68 at safe value

static void __cdecl HookedGFTimerUpdate(void)
{
    InterlockedIncrement(&s_gfHookCallCount);
    
    if (s_ewmCapGF) {
        InterlockedIncrement(&s_gfHookSkipCount);
        // Skip the function entirely while capped.
        // GF state clamping is handled by EWM_ClampGFState() in the mod thread.
        return;
    }
    
    // Not capped — state68 clamp is released by the ATB hook.
    // Do NOT restore state68 here — let the function set it naturally
    // when the timer reaches 0.
    
    InterlockedIncrement(&s_gfHookPassCount);
    s_originalGFTimerUpdate();
}

// Called from EWM_UpdateBattle() on the mod thread every frame while capped.
// v0.10.91: CONFIRMED fire path is in vanilla engine at 0x004B0400-0x004B0640.
// Three-layer prevention:
//   1. Code patch: 0x004B04B4 MOV→RET prevents state machine case-5 handler
//   2. State68 clamp: prevents state68==5 from being seen by other systems
//   3. Timer function skip: HookedGFTimerUpdate skips display countdown
static void EWM_ClampGFState(void)
{
    // Layer 1: Code patch — prevent state machine from executing fire handler
    // This is the PRIMARY prevention. Patching the opcode at 0x004B04B4 from
    // C7 (MOV [state68],5) to C3 (RET) makes the state machine handler return
    // immediately before any fire setup code runs.
    if (s_gfFirePatchReady && !s_gfFirePatched) {
        __try {
            *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_SAFE_VALUE;  // C7 → C3 (MOV → RET)
            s_gfFirePatched = true;
            Log::Write("BattleTTS: [GF-PATCH] APPLIED: 0x%08X = 0x%02X (RET) — fire blocked",
                       GF_FIRE_PATCH_ADDR, (unsigned)GF_SAFE_VALUE);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    
    // Layer 2: Clamp state68 to prevent battle loop from seeing "GF ready"
    __try {
        uint8_t state = *(uint8_t*)GF_STATE68_ADDR;
        if (state == GF_STATE_FIRE) {
            if (!s_gfState68Clamped) {
                s_gfSavedState68 = state;
                s_gfState68Clamped = true;
            }
            *(uint8_t*)GF_STATE68_ADDR = GF_STATE_SAFE;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Called when EWM cap releases — restore the fire path so GF fires naturally.
static void EWM_RestoreGFPatch(void)
{
    // Restore code patch: C3 → C7 (RET → MOV) — re-enable fire handler
    if (s_gfFirePatched && s_gfFirePatchReady) {
        __try {
            *(uint8_t*)GF_FIRE_PATCH_ADDR = GF_FIRE_VALUE;  // C3 → C7
            s_gfFirePatched = false;
            Log::Write("BattleTTS: [GF-PATCH] RESTORED: 0x%08X = 0x%02X (MOV) — fire enabled",
                       GF_FIRE_PATCH_ADDR, (unsigned)GF_FIRE_VALUE);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    
    // Release state68 clamp
    s_gfState68Clamped = false;
    s_gfSavedState68 = 0xFF;
}

// Called from Update() to periodically log GF hook stats
static void GF_LogHookStats(void)
{
    DWORD now = GetTickCount();
    if (now - s_gfHookLastLogTick < 1000) return;  // every 1 second
    s_gfHookLastLogTick = now;
    
    LONG calls = InterlockedExchange(&s_gfHookCallCount, 0);
    LONG skips = InterlockedExchange(&s_gfHookSkipCount, 0);
    LONG pass  = InterlockedExchange(&s_gfHookPassCount, 0);
    
    // Read GF-related state bytes
    uint8_t timerVal = 0, gfState = 0xFF, gfActive = 0xFF;
    __try { timerVal = *(uint8_t*)0x01D769D6; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { gfState  = *(uint8_t*)0x01D76868; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // state machine var (jump table dispatch)
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // GF active flag (checked at 0x004B0514)
    
    if (calls > 0 || timerVal > 0 || gfState != 0 || gfActive != 0 || s_gfFirePatched || s_gfScanValid) {
        uint16_t csLoad = 0, csMax = 0;
        int8_t gfSlot = -1;
        __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        // v0.10.81: Also check s_gfFlagHidden — if we've hidden the active flag,
        // gfActive reads as 0 but we still want to log the real timer values.
        // v0.10.88: Removed BATTLE_REAL_ENTITY_STRIDE references (was wrong 0x1D0)
        // Read compStats for display gauge diagnostic instead
        bool gfDiagActive = (gfActive != 0) || s_gfFirePatched;
        if (gfDiagActive && gfSlot >= 0 && gfSlot < BATTLE_ALLY_SLOTS) {
            uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gfSlot * BATTLE_COMP_STATS_STRIDE);
            __try { csLoad = *(uint16_t*)(cs + 0x14); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { csMax = *(uint16_t*)(cs + 0x16); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        
        // v0.10.85: Also read actual byte at patch address for verification
        uint8_t patchByte = 0;
        __try { patchByte = *(uint8_t*)GF_FIRE_PATCH_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        Log::Write("BattleTTS: [GF-HOOK] calls=%ld skip=%ld pass=%ld timer=%u st68=%u act71=%u capGF=%d patch=%d(0x%02X) cs=%u/%u scan=%d",
                   calls, skips, pass, (unsigned)timerVal, (unsigned)gfState, (unsigned)gfActive, (int)s_ewmCapGF, (int)s_gfFirePatched, (unsigned)patchByte,
                   (unsigned)csLoad, (unsigned)csMax, s_gfScanLogCount);
    }
}

// v0.10.65: GF state snapshot diagnostic — watches the GF state machine region
// to find what triggers the actual GF fire (since timer freeze doesn't prevent it).
// Polls every 200ms and logs any changes in the GF state struct area.
static uint8_t s_gfStateSnap[128] = {};  // snapshot of 0x01D76860-0x01D768DF (state machine area)
static uint8_t s_gfStructSnap[128] = {}; // snapshot of 0x01D76960-0x01D769DF (GF struct area)
static bool s_gfSnapValid = false;
static DWORD s_gfSnapLastTick = 0;

static void GF_PollStateChanges(void)
{
    DWORD now = GetTickCount();
    if (now - s_gfSnapLastTick < 200) return;
    s_gfSnapLastTick = now;
    
    uint8_t newState[128], newStruct[128];
    __try { memcpy(newState, (uint8_t*)0x01D76860, 128); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    __try { memcpy(newStruct, (uint8_t*)0x01D76960, 128); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    if (!s_gfSnapValid) {
        memcpy(s_gfStateSnap, newState, 128);
        memcpy(s_gfStructSnap, newStruct, 128);
        s_gfSnapValid = true;
        return;
    }
    
    // Log changes in state machine region (0x01D76860-0x01D768DF)
    for (int i = 0; i < 128; i++) {
        if (newState[i] != s_gfStateSnap[i]) {
            uint32_t addr = 0x01D76860 + i;
            // Filter out known noisy addresses
            if (addr == 0x01D76862 || addr == 0x01D76870 || addr == 0x01D7686E) continue;  // animation counters
            Log::Write("BattleTTS: [GF-STATE] 0x%08X: %u -> %u", addr,
                       (unsigned)s_gfStateSnap[i], (unsigned)newState[i]);
        }
    }
    
    // Log changes in GF struct region (0x01D76960-0x01D769DF)
    for (int i = 0; i < 128; i++) {
        if (newStruct[i] != s_gfStructSnap[i]) {
            uint32_t addr = 0x01D76960 + i;
            Log::Write("BattleTTS: [GF-STRUCT] 0x%08X: %u -> %u", addr,
                       (unsigned)s_gfStructSnap[i], (unsigned)newStruct[i]);
        }
    }
    
    memcpy(s_gfStateSnap, newState, 128);
    memcpy(s_gfStructSnap, newStruct, 128);
}

static void EWM_InstallGFHook()
{
    if (s_gfTimerHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)GF_TIMER_FUNC_ADDR,
        (LPVOID)HookedGFTimerUpdate,
        (LPVOID*)&s_originalGFTimerUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)GF_TIMER_FUNC_ADDR);
    }
    s_gfTimerHookInstalled = (st == MH_OK);
    Log::Write("BattleTTS: [EWM] GF timer hook @ 0x%08X — %s (trampoline=0x%08X)",
               GF_TIMER_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalGFTimerUpdate);
}

// ============================================================================
// v0.10.63/70: Hardware write breakpoint diagnostic for GF fire trigger hunt
// ============================================================================
// v0.10.63: Originally targeted 0x01D769D6 (GF loading timer) — found function 0x004B0500.
// v0.10.70: RETARGETED to 0x01D769D0 — bytes that transition to 0 when GF actually fires.
//           This address is written by an UNKNOWN function (not 0x004B0500).
//           Goal: find the instruction that writes to 0x01D769D0, then its function entry.
//
// Sets a hardware write BP using x86 debug registers (DR0).
// When the CPU writes to that address, EXCEPTION_SINGLE_STEP fires and our
// Vectored Exception Handler (VEH) captures the EIP (instruction pointer).
// EIP points to the instruction AFTER the write. We log it and can then
// scan backward to find the function entry for MinHook.
//
// Arm: F12 key during battle while GF is loading.
// Auto-disables after 20 captures to avoid flooding.

static volatile bool s_gfBPArmed = false;       // true while hardware BP is active
static volatile bool s_gfBPWantArm = false;     // set by mod thread, armed by game thread hook
static volatile int  s_gfBPHitCount = 0;        // number of VEH captures so far
static const int     GF_BP_MAX_HITS = 50;  // v0.10.91: increased from 20 to catch fire dispatch amid timer noise
static PVOID         s_gfVEHHandle = nullptr;    // VEH registration handle
static bool          s_gfBPF12WasDown = false;   // edge detection
static DWORD s_accessibilityTID = 0;             // v0.10.91: our thread ID, skip in BP arming

// VEH handler: catches EXCEPTION_SINGLE_STEP from our DR0 hardware breakpoint.
// v0.10.89: Handles READ/WRITE BPs on the GF effect table.
// Runs on whatever thread triggered the access (should be the game thread).
static LONG CALLBACK GF_BP_VectoredHandler(PEXCEPTION_POINTERS pExInfo)
{
    if (pExInfo->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    
    // Check DR6 to confirm this is our DR0 breakpoint (bit 0)
    DWORD dr6 = (DWORD)pExInfo->ContextRecord->Dr6;
    if (!(dr6 & 0x01))
        return EXCEPTION_CONTINUE_SEARCH;  // not our BP
    
    // Clear DR6 bit 0 to acknowledge
    pExInfo->ContextRecord->Dr6 &= ~0x0F;
    
    // v0.10.91: Skip captures from our accessibility thread (self-capture noise)
    if (s_accessibilityTID != 0 && GetCurrentThreadId() == s_accessibilityTID) {
        pExInfo->ContextRecord->Dr6 &= ~0x0F;
        return EXCEPTION_CONTINUE_EXECUTION;  // silently skip, don't count
    }
    
    // v0.10.71: After max captures, silently disarm on whatever thread fires
    if (s_gfBPHitCount >= GF_BP_MAX_HITS) {
        pExInfo->ContextRecord->Dr0 = 0;
        pExInfo->ContextRecord->Dr7 &= ~(0x03 | (0x0F << 16));
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    
    {
        s_gfBPHitCount++;
        DWORD eip = (DWORD)pExInfo->ContextRecord->Eip;
        DWORD dr0 = (DWORD)pExInfo->ContextRecord->Dr0;
        DWORD tid = GetCurrentThreadId();
        
        // Read code bytes around EIP for disassembly context
        // The triggering instruction is BEFORE eip (EIP points to next instruction)
        uint8_t codeBuf[32] = {};
        DWORD codeStart = (eip >= 16) ? eip - 16 : 0;
        __try {
            memcpy(codeBuf, (uint8_t*)codeStart, 32);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // v0.10.89: Capture ALL general-purpose registers.
        // For READ BPs, the read value is typically in one of these registers.
        DWORD regEax = (DWORD)pExInfo->ContextRecord->Eax;
        DWORD regEbx = (DWORD)pExInfo->ContextRecord->Ebx;
        DWORD regEcx = (DWORD)pExInfo->ContextRecord->Ecx;
        DWORD regEdx = (DWORD)pExInfo->ContextRecord->Edx;
        DWORD regEsi = (DWORD)pExInfo->ContextRecord->Esi;
        DWORD regEdi = (DWORD)pExInfo->ContextRecord->Edi;
        DWORD regEbp = (DWORD)pExInfo->ContextRecord->Ebp;
        DWORD regEsp = (DWORD)pExInfo->ContextRecord->Esp;
        
        // Read the value at the BP address (e.g. the function pointer in the effect table)
        uint32_t bpValue = 0;
        __try { bpValue = *(uint32_t*)dr0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // Read GF loading context
        uint16_t gfLoadCur = 0, gfLoadMax = 0;
        uint8_t timerVal = 0, state68 = 0, gfActive = 0;
        int8_t gfSlot = -1;
        __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (gfSlot >= 0 && gfSlot < 3) {
            uint8_t* cs = (uint8_t*)(0x01CFF000 + gfSlot * 0x1D0);
            __try { gfLoadCur = *(uint16_t*)(cs + 0x14); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { gfLoadMax = *(uint16_t*)(cs + 0x16); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        __try { timerVal = *(uint8_t*)0x01D769D6; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { state68 = *(uint8_t*)0x01D76868; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // Format code hex — 32 bytes centered on EIP
        char hexBuf[128] = {};
        int p = 0;
        for (int i = 0; i < 32; i++)
            p += snprintf(hexBuf + p, sizeof(hexBuf) - p, "%02X ", codeBuf[i]);
        
        Log::Write("BattleTTS: [GF-BP] #%d ACCESS 0x%08X! TID=%u EIP=0x%08X val@BP=0x%08X",
                   s_gfBPHitCount, dr0, tid, eip, bpValue);
        Log::Write("BattleTTS: [GF-BP]   regs: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X",
                   regEax, regEbx, regEcx, regEdx);
        Log::Write("BattleTTS: [GF-BP]   regs: ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X",
                   regEsi, regEdi, regEbp, regEsp);
        Log::Write("BattleTTS: [GF-BP]   GF: load=%u/%u timer=%u st68=%u act=%u slot=%d",
                   (unsigned)gfLoadCur, (unsigned)gfLoadMax,
                   (unsigned)timerVal, (unsigned)state68, (unsigned)gfActive, (int)gfSlot);
        Log::Write("BattleTTS: [GF-BP]   code[@0x%08X]: %s", codeStart, hexBuf);
        
        // v0.10.89: Capture call stack (return addresses from stack)
        // Walk ESP upward looking for addresses in executable range
        __try {
            uint32_t* stack = (uint32_t*)regEsp;
            char stackBuf[512] = {};
            int sp = 0;
            sp += snprintf(stackBuf + sp, sizeof(stackBuf) - sp, "stack:");
            int found = 0;
            for (int si = 0; si < 32 && found < 8; si++) {
                uint32_t val = stack[si];
                // Check if it looks like a code address (0x004xxxxx or 0x6Exxxxxx for FFNx)
                if ((val >= 0x00401000 && val < 0x00800000) ||
                    (val >= 0x60000000 && val < 0x80000000)) {
                    sp += snprintf(stackBuf + sp, sizeof(stackBuf) - sp,
                                   " [ESP+%02X]=0x%08X", si * 4, val);
                    found++;
                }
            }
            if (found > 0)
                Log::Write("BattleTTS: [GF-BP]   %s", stackBuf);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // After enough captures, disable the BP
        if (s_gfBPHitCount >= GF_BP_MAX_HITS) {
            pExInfo->ContextRecord->Dr0 = 0;
            pExInfo->ContextRecord->Dr7 &= ~(0x03 | (0x0F << 16));
            s_gfBPArmed = false;
            Log::Write("BattleTTS: [GF-BP] Max captures reached, BP disabled.");
        }
    }
    
    return EXCEPTION_CONTINUE_EXECUTION;
}

// v0.10.71/76/89: Arm hardware BP on ALL threads in the process.
// Enumerates threads via ToolHelp32, suspends each, sets DR0, resumes.
// v0.10.76: Takes target address as parameter.
// v0.10.89: Takes condition and length bits for DR7.
//   condition: 0x01 = write-only, 0x03 = read/write
//   length:    0x00 = 1 byte, 0x01 = 2 bytes, 0x03 = 4 bytes
static void GF_BP_ArmAllThreads(uint32_t targetAddr, uint8_t condition = 0x01, uint8_t length = 0x00)
{
    if (s_gfBPArmed) return;
    
    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        Log::Write("BattleTTS: [GF-BP] CreateToolhelp32Snapshot FAILED (err=%u)", GetLastError());
        return;
    }
    
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    int armed = 0, failed = 0;
    
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            
            HANDLE hThread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                FALSE, te.th32ThreadID);
            if (!hThread) { failed++; continue; }
            
            bool isSelf = (te.th32ThreadID == myTid);
            // v0.10.91: Skip our accessibility thread to avoid self-capture
            bool isOurThread = (s_accessibilityTID != 0 && te.th32ThreadID == s_accessibilityTID);
            if (isOurThread) { CloseHandle(hThread); continue; }
            if (!isSelf) SuspendThread(hThread);
            
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(hThread, &ctx)) {
                ctx.Dr0 = targetAddr;
                ctx.Dr7 &= ~(0x03 | (0x0F << 16));  // clear DR0 enable + condition + length
                ctx.Dr7 |= 0x01;                      // local enable DR0
                ctx.Dr7 |= ((DWORD)(condition & 0x03) << 16);  // condition bits
                ctx.Dr7 |= ((DWORD)(length & 0x03) << 18);     // length bits
                
                if (SetThreadContext(hThread, &ctx)) {
                    armed++;
                } else {
                    failed++;
                }
            } else {
                failed++;
            }
            
            if (!isSelf) ResumeThread(hThread);
            CloseHandle(hThread);
        } while (Thread32Next(snap, &te));
    }
    
    CloseHandle(snap);
    s_gfBPArmed = true;
    s_gfBPHitCount = 0;
    const char* condStr = (condition == 0x03) ? "READ/WRITE" : (condition == 0x01) ? "WRITE" : "EXEC";
    int lenBytes = (length == 0x03) ? 4 : (length == 0x01) ? 2 : 1;
    Log::Write("BattleTTS: [GF-BP] Hardware %s BP armed on 0x%08X (%d-byte) — ALL THREADS (%d armed, %d failed)",
               condStr, targetAddr, lenBytes, armed, failed);
}

// v0.10.91: Auto-arm READ BP on display timer 0x01D769D6 when GF loading
// starts. compStats+0x14/+0x16 are ALWAYS ZERO (confirmed v0.10.90) — they
// are NOT the GF loading counter. The real countdown is the display timer
// at 0x01D769D6 (counts ~48→0). Something reads this value and triggers
// the fire when it hits 0. A hardware READ BP will catch that instruction.
//
// CRITICAL FIX: Skip arming on our own accessibility thread. In v0.10.90,
// ALL VEH captures were from our thread (TID that polls memory), producing
// only FFNx SEH handler EIPs. By excluding our thread, the BP only fires
// on the game thread where the actual fire dispatch code runs.
static uint8_t s_gfAutoArmLastActive = 0;  // previous value of 0x01D76971
static bool s_gfAutoArmDone = false;        // only auto-arm once per battle

static void GF_BP_AutoArm(void)
{
    if (!s_inBattle || s_gfBPArmed || s_gfAutoArmDone) return;
    
    // Record our TID on first call so we can skip it when arming
    if (s_accessibilityTID == 0) s_accessibilityTID = GetCurrentThreadId();
    
    uint8_t gfActive = 0;
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    // v0.10.91 fix: Arm as soon as we see gfActive==1, not just on 0→1 edge.
    // The GF may already be active when we first poll (command issued before
    // our thread starts checking). s_gfAutoArmDone ensures we only arm once.
    s_gfAutoArmLastActive = gfActive;
    
    if (gfActive == 1) {
        // v0.10.91: Target the display timer at 0x01D769D6 (uint16).
        // CRITICAL: Don't arm immediately! The timer is read/written ~60x/sec by
        // the hooked timer function. If we arm now, we'll burn 20 captures in <1sec,
        // all from the timer decrement code, and never see the fire dispatch.
        // Instead, wait until timer <= 3 so we catch the fire dispatch read when
        // the timer hits 0, not all the decrement reads during loading.
        uint32_t targetAddr = 0x01D769D6;
        uint16_t timerVal = 0;
        __try { timerVal = *(uint16_t*)targetAddr; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        if (timerVal == 0 || timerVal > 3) {
            // timerVal==0: timer hasn't started yet (goes 0→47→0)
            // timerVal>3: still counting down, wait until closer to fire
            return;
        }
        
        // Timer is 1, 2, or 3 — about to fire, arm now!
        Log::Write("BattleTTS: [GF-BP] AUTO-ARM: timer at %u, arming READ/WRITE BP on 0x%08X",
                   (unsigned)timerVal, targetAddr);
        Log::Write("BattleTTS: [GF-BP] AUTO-ARM: skipping our TID=%u to avoid self-capture",
                   (unsigned)s_accessibilityTID);
        GF_BP_ArmAllThreads(targetAddr, 0x03, 0x01);  // read/write, 2-byte
        s_gfAutoArmDone = true;
        ScreenReader::Speak("Breakpoint armed on display timer", true);
    }
}

// ============================================================================
// v0.10.96: Target selection diagnostic (F12 key)
// ============================================================================
// Takes 2 snapshots of the battle menu state region (0x01D76800-0x01D76C00,
// 1024 bytes) while the player moves the target cursor between enemies/allies.
// F12 press cycle:
//   Stage 0 → 1: Snapshot with cursor on target A
//   Stage 1 → 2: Move cursor to target B, press F12 → diff + log
//   Stage 2 + F12: Reset for another round
//
// Focuses on cursor-like byte changes: small values (<32), small deltas (1-6).
// Also logs menuPhase, activeChar, cmdCursor to identify the targeting phase.

static const uint32_t TGTDIAG_SCAN_BASE = 0x01D76800;
static const int TGTDIAG_SCAN_SIZE = 1024;  // 0x01D76800-0x01D76BFF

struct TargetDiagSnapshot {
    uint8_t region[1024];
    uint8_t menuPhase;      // 0x01D768D0
    uint8_t activeChar;     // battle_current_active_character_id
    uint8_t cmdCursor;      // 0x01D76843
    uint8_t subCursor;      // 0x01D76844 (known sub-menu cursor)
};

static TargetDiagSnapshot s_tgtDiagSnaps[2] = {};
static int s_tgtDiagStage = 0;  // 0=ready, 1=first snap, 2=diffed

static void TgtDiag_TakeSnapshot(int idx)
{
    TargetDiagSnapshot& snap = s_tgtDiagSnaps[idx];
    memset(&snap, 0, sizeof(snap));
    __try { memcpy(snap.region, (uint8_t*)TGTDIAG_SCAN_BASE, TGTDIAG_SCAN_SIZE); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { snap.menuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (s_pActiveCharId) { __try { snap.activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    __try { snap.cmdCursor = *(uint8_t*)0x01D76843; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { snap.subCursor = *(uint8_t*)0x01D76844; } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.10.107: F12 Draw sub-menu diagnostic.
// Dumps target bitmask, cursor state, and draw spell slots for all enemies.
// Draw spell slots: 0x1D28F18 base, 4 slots/enemy, 0x47 stride between enemies.
// Each slot is 4 bytes (format TBD — this diagnostic will reveal it).
static const uint32_t DRAW_SPELL_BASE = 0x1D28F18;   // Enemy 1 slot 0
static const int      DRAW_SLOTS_PER_ENEMY = 4;
static const int      DRAW_SLOT_SIZE = 4;             // bytes per slot
static const int      DRAW_ENEMY_STRIDE = 0x47;       // bytes between enemy 1 and enemy 2
static const int      DRAW_ENEMY_COUNT = 4;            // enemies 1-4 (slots 3-6)

static void GF_BP_PollKey(void)
{
    bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    bool f12Pressed = f12Down && !s_gfBPF12WasDown;
    s_gfBPF12WasDown = f12Down;
    
    if (!f12Pressed || !s_inBattle) return;
    
    Log::Write("BattleTTS: [F12-DRAW] === Draw Diagnostic Snapshot ===");
    
    // 1. Core state
    uint8_t menuPhase = 0, cmdCursor = 0xFF, subCursor = 0xFF;
    uint8_t activeChar = 0xFF;
    uint8_t tgtMask = 0, tgtScope = 0;
    __try { menuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { cmdCursor = *(uint8_t*)0x01D76843; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { subCursor = *(uint8_t*)0x01D768EC; } __except(EXCEPTION_EXECUTE_HANDLER) {}  // BATTLE_SUBMENU_CURSOR
    if (s_pActiveCharId) { __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    __try { tgtMask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { tgtScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    Log::Write("BattleTTS: [F12-DRAW] menuPhase=%u activeChar=%u cmdCursor=%u subCursor=%u",
               (unsigned)menuPhase, (unsigned)activeChar, (unsigned)cmdCursor, (unsigned)subCursor);
    // Note: s_inSubmenu and s_submenuCommandId are defined later in the file (MSVC single-pass).
    // Read the raw submenu state from memory instead.
    uint8_t menuPhaseVal = 0;
    __try { menuPhaseVal = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    Log::Write("BattleTTS: [F12-DRAW] tgtMask=0x%02X tgtScope=%u menuPhase2=%u",
               (unsigned)tgtMask, (unsigned)tgtScope, (unsigned)menuPhaseVal);
    
    // 2. Draw spell slots for all 4 enemies — raw hex + decoded magic names
    for (int e = 0; e < DRAW_ENEMY_COUNT; e++) {
        uint32_t enemyBase = DRAW_SPELL_BASE + e * DRAW_ENEMY_STRIDE;
        int slot = BATTLE_ALLY_SLOTS + e;  // entity slot 3-6
        uint32_t hp = GetEntityHP(slot);
        
        // Read all 4 draw slots (16 bytes) as raw data
        uint8_t raw[16] = {};
        __try { memcpy(raw, (uint8_t*)enemyBase, 16); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // Format raw hex
        char hexBuf[80] = {};
        int p = 0;
        for (int b = 0; b < 16; b++)
            p += snprintf(hexBuf + p, sizeof(hexBuf) - p, "%02X ", raw[b]);
        
        Log::Write("BattleTTS: [F12-DRAW] Enemy%d (slot%d hp=%u) @0x%08X: %s",
                   e + 1, slot, hp, enemyBase, hexBuf);
        
        // Interpret each 4-byte slot in multiple ways
        for (int s = 0; s < DRAW_SLOTS_PER_ENEMY; s++) {
            uint8_t* slotPtr = raw + s * DRAW_SLOT_SIZE;
            uint8_t b0 = slotPtr[0], b1 = slotPtr[1], b2 = slotPtr[2], b3 = slotPtr[3];
            uint16_t w0 = *(uint16_t*)slotPtr;
            uint16_t w1 = *(uint16_t*)(slotPtr + 2);
            
            // Log raw values — magic name lookup is defined later in file (MSVC single-pass).
            // Decode magic IDs from the log manually (1=Fire, 4=Blizzard, 7=Thunder, etc.)
            Log::Write("BattleTTS: [F12-DRAW]   slot[%d] bytes=%02X %02X %02X %02X  "
                       "w0=%u w1=%u  b0asId=%u w0asId=%u",
                       s, b0, b1, b2, b3, (unsigned)w0, (unsigned)w1, (unsigned)b0, (unsigned)w0);
        }
    }
    
    // 3. Also dump the wider region around draw data (48 bytes before, 16 after last enemy)
    // to see if there's a "current target enemy index" or draw list pointer nearby
    uint32_t contextStart = DRAW_SPELL_BASE - 48;
    Log::Write("BattleTTS: [F12-DRAW] --- Context dump around draw spell region ---");
    __try {
        for (uint32_t addr = contextStart; addr < DRAW_SPELL_BASE + DRAW_ENEMY_COUNT * DRAW_ENEMY_STRIDE + 16; addr += 16) {
            uint8_t buf[16] = {};
            memcpy(buf, (uint8_t*)addr, 16);
            char hex[80] = {};
            int hp2 = 0;
            for (int b = 0; b < 16; b++)
                hp2 += snprintf(hex + hp2, sizeof(hex) - hp2, "%02X ", buf[b]);
            Log::Write("BattleTTS: [F12-DRAW] 0x%08X: %s", addr, hex);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [F12-DRAW] EXCEPTION reading context region");
    }
    
    // 4. v0.10.110: EXPANDED — dump full 0xC0-0xFF range (64 bytes) to cover
    //    the gap at D8-DF that was missing in v0.10.109. The Stock/Cast cursor
    //    is likely in this gap (draw cursor confirmed at D8, Stock/Cast may be nearby).
    Log::Write("BattleTTS: [F12-DRAW] --- Full menu state region 0xC0-0xFF (64 bytes) ---");
    __try {
        uint8_t fullBuf[64] = {};
        memcpy(fullBuf, (uint8_t*)0x01D768C0, 64);
        // Log as 4 rows of 16 bytes each, with individual byte labels
        for (int row = 0; row < 4; row++) {
            char hex[120] = {};
            int hp4 = 0;
            for (int b = 0; b < 16; b++)
                hp4 += snprintf(hex + hp4, sizeof(hex) - hp4, "%02X ", fullBuf[row * 16 + b]);
            Log::Write("BattleTTS: [F12-DRAW] 0x%02X-0x%02X: %s",
                       0xC0 + row * 16, 0xCF + row * 16, hex);
        }
        // Also log key individual bytes with labels for easy comparison
        Log::Write("BattleTTS: [F12-DRAW] KEY BYTES: D0(menuPhase)=%u D4=%u D8(drawCur)=%u D9=%u DA=%u DB=%u DC=%u DD=%u DE=%u DF=%u",
                   fullBuf[0x10], fullBuf[0x14], fullBuf[0x18], fullBuf[0x19],
                   fullBuf[0x1A], fullBuf[0x1B], fullBuf[0x1C], fullBuf[0x1D],
                   fullBuf[0x1E], fullBuf[0x1F]);
        Log::Write("BattleTTS: [F12-DRAW] KEY BYTES: EC(subCur)=%u ED=%u EE=%u EF=%u F0=%u F1=%u F2=%u F3=%u",
                   fullBuf[0x2C], fullBuf[0x2D], fullBuf[0x2E], fullBuf[0x2F],
                   fullBuf[0x30], fullBuf[0x31], fullBuf[0x32], fullBuf[0x33]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // 5. Also scan the UI task pool for a Draw controller node
    // The pool at 0x1D76BC8 (10 x 0x78) may have a Draw handler with a Stock/Cast cursor
    Log::Write("BattleTTS: [F12-DRAW] --- UI task pool scan ---");
    __try {
        for (int pi = 0; pi < 10; pi++) {
            uint8_t* node = (uint8_t*)(0x1D76BC8 + pi * 0x78);
            uint32_t h08 = *(uint32_t*)(node + 0x08);
            uint32_t h0C = *(uint32_t*)(node + 0x0C);
            uint16_t inUse = *(uint16_t*)(node + 0x12);
            if (h08 != 0 || h0C != 0 || inUse != 0) {
                // Dump first 32 bytes of active nodes
                char nhex[100] = {};
                int np = 0;
                for (int nb = 0; nb < 32; nb++)
                    np += snprintf(nhex + np, sizeof(nhex) - np, "%02X ", node[nb]);
                Log::Write("BattleTTS: [F12-DRAW] pool[%d] h08=0x%08X h0C=0x%08X inUse=%u: %s",
                           pi, h08, h0C, (unsigned)inUse, nhex);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    Log::Write("BattleTTS: [F12-DRAW] === End Draw Diagnostic ===");
    ScreenReader::Speak("Draw diagnostic captured.", true);
}

// v0.10.63: Function entry scan — read code bytes backward from the write instruction
// to find the GF timer function's entry point for MinHook.
static bool s_gfFuncScanDone = false;

static void GF_ScanForFunctionEntry(void)
{
    if (s_gfFuncScanDone) return;
    if (s_gfBPHitCount < 1) return;  // need at least one BP hit first
    s_gfFuncScanDone = true;
    
    // The write instruction is at 0x004B063B (EIP after = 0x004B063F).
    // Scan backward looking for function boundaries:
    //   - CC (INT3 padding)
    //   - C3 (RET) or C2 xx xx (RET imm16) followed by our code
    //   - 90 (NOP padding)
    //   - 55 8B EC (PUSH EBP; MOV EBP, ESP — standard prologue)
    //   - 83 EC xx (SUB ESP, xx — FPO prologue without PUSH EBP)
    
    const uint32_t writeAddr = 0x004B063B;
    const uint32_t scanStart = writeAddr - 0x200;  // scan 512 bytes back
    
    Log::Write("BattleTTS: [GF-FUNC] === Function entry scan from write @ 0x%08X ===", writeAddr);
    
    // Dump code in 32-byte chunks from scanStart to writeAddr+16
    __try {
        for (uint32_t addr = scanStart; addr <= writeAddr + 16; addr += 32) {
            uint8_t* p = (uint8_t*)addr;
            char hex[200] = {};
            int pos = 0;
            for (int i = 0; i < 32 && addr + i <= writeAddr + 16; i++) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", p[i]);
            }
            Log::Write("BattleTTS: [GF-FUNC] 0x%08X: %s", addr, hex);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [GF-FUNC] EXCEPTION reading code");
    }
    
    // Also scan for specific patterns
    __try {
        uint8_t* base = (uint8_t*)scanStart;
        int scanLen = (int)(writeAddr - scanStart);
        
        for (int i = scanLen - 1; i >= 0; i--) {
            uint32_t addr = scanStart + i;
            uint8_t b = base[i];
            
            // Look for RET (C3) or INT3 (CC) — potential function boundary
            if (b == 0xC3 || b == 0xCC) {
                // The next byte after RET/INT3 could be the function entry
                uint32_t candidate = addr + 1;
                uint8_t next1 = base[i + 1];
                uint8_t next2 = (i + 2 < scanLen) ? base[i + 2] : 0;
                uint8_t next3 = (i + 3 < scanLen) ? base[i + 3] : 0;
                
                Log::Write("BattleTTS: [GF-FUNC] Boundary @ 0x%08X: %02X | next: %02X %02X %02X (candidate entry: 0x%08X)",
                           addr, b, next1, next2, next3, candidate);
            }
            
            // Look for PUSH EBP (55) followed by MOV EBP,ESP (8B EC)
            if (b == 0x55 && i + 2 < scanLen && base[i+1] == 0x8B && base[i+2] == 0xEC) {
                Log::Write("BattleTTS: [GF-FUNC] PROLOGUE (push ebp; mov ebp,esp) @ 0x%08X",
                           addr);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [GF-FUNC] EXCEPTION in pattern scan");
    }
    
    Log::Write("BattleTTS: [GF-FUNC] === End scan ===");
}

static void __cdecl HookedATBUpdate(void)
{
    // v0.10.88: Initialize GF timer scan snapshot when GF loading starts
    if (!s_gfScanValid) {
        uint8_t gfAct = 0;
        __try { gfAct = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (gfAct == 1) {
            memcpy(s_gfScanSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
            s_gfScanValid = true;
            Log::Write("BattleTTS: [GF-SCAN] Snapshot initialized (GF active)");
        }
    }
    
    // v0.10.85: RET code patch (belt-and-suspenders, kept alongside sticky hide).
    if (s_gfFirePatchReady) {
        if (s_ewmCapGF && !s_gfFirePatched) {
            uint8_t* p = (uint8_t*)GF_FIRE_PATCH_ADDR;
            if (*p == GF_FIRE_VALUE) {
                *p = GF_SAFE_VALUE;
                s_gfFirePatched = true;
            }
        } else if (!s_ewmCapGF && s_gfFirePatched) {
            uint8_t* p = (uint8_t*)GF_FIRE_PATCH_ADDR;
            *p = GF_FIRE_VALUE;
            s_gfFirePatched = false;
        }
    }
    
    // v0.10.87: Sticky/sandwich gfActive hide REMOVED (v0.10.88).
    // All flag-hiding approaches break ATB or menus. Need to find the
    // GF timer decrement function instead.
    
    // v0.10.69: Clamp GF state68 on the GAME THREAD (before battle loop reads it).
    // The mod thread clamp was too late — race condition with the game loop.
    if (s_ewmCapGF) {
        uint8_t st = *(uint8_t*)GF_STATE68_ADDR;
        if (st == GF_STATE_FIRE) {
            if (!s_gfState68Clamped) {
                s_gfSavedState68 = st;
                s_gfState68Clamped = true;
            }
            *(uint8_t*)GF_STATE68_ADDR = GF_STATE_SAFE;
        }
    } else if (s_gfState68Clamped) {
        // DON'T restore state68 to 5 (fire) — let the GF timer function
        // set it naturally when the timer reaches 0 after uncapping.
        // Just clear our tracking flag.
        s_gfState68Clamped = false;
    }
    
    // v0.10.79-82: Real GF timer pre-cap REMOVED (v0.10.88).
    // 0x01D27D94 was Enemy 1's ATB, not a GF timer. Stride is 0xD0, not 0x1D0.
    
    if (!s_ewmShouldCap) {
        s_originalATBUpdate();
        
        // v0.10.88: Per-frame GF timer scan on early-return path
        if (s_gfScanValid && s_gfScanLogCount < GF_SCAN_LOG_MAX) {
            uint8_t newSnap[GF_SCAN_BYTES];
            memcpy(newSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
            for (int i = 0; i + 1 < GF_SCAN_BYTES; i += 2) {
                int16_t oldVal = *(int16_t*)(s_gfScanSnap + i);
                int16_t newVal = *(int16_t*)(newSnap + i);
                int16_t delta = newVal - oldVal;
                // Look for decrementing values (delta -1 to -4) with values in timer range
                if (delta >= -4 && delta <= -1 && oldVal > 0 && oldVal <= 500) {
                    Log::Write("BattleTTS: [GF-SCAN] +0x%03X (0x%08X): %d -> %d (delta=%d)",
                               i, GF_SCAN_BASE + i, (int)oldVal, (int)newVal, (int)delta);
                    s_gfScanLogCount++;
                }
            }
            memcpy(s_gfScanSnap, newSnap, GF_SCAN_BYTES);
        }
        
        return;
    }
    
    uint8_t excludeSlot = s_ewmCapExcludeSlot;
    
    // --- PRE-CAP: save real ATB values, set to 0 ---
    uint32_t savedATB[BATTLE_TOTAL_SLOTS] = {};
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        
        if (slot < BATTLE_ALLY_SLOTS) {
            uint16_t* pCurATB = (uint16_t*)(base + BENT_CUR_ATB);
            savedATB[slot] = *pCurATB;
            *pCurATB = 0;
        } else {
            uint32_t* pCurATB = (uint32_t*)(base + BENT_CUR_ATB);
            savedATB[slot] = *pCurATB;
            *pCurATB = 0;
        }
    }
    
    // v0.10.95: PRE-CAP for GF loading counter — per-slot via entity+0x7C.
    // The ATB function also increments compStats[slot]+0x14 (GF loading gauge).
    // Same sandwich: save → zero → call → measure → restore+cap.
    // Now iterates all ally slots instead of just gfSlot.
    uint16_t savedGFLoad[BATTLE_ALLY_SLOTS] = {};
    uint16_t gfLoadMax[BATTLE_ALLY_SLOTS] = {};
    bool gfLoadActive[BATTLE_ALLY_SLOTS] = {};
    if (s_ewmCapGF) {
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
            uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
            if (gfFlag != 0) {
                uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
                savedGFLoad[gs] = *pGFLoad;
                gfLoadMax[gs] = *(uint16_t*)(cs + 0x16);
                *pGFLoad = 0;  // zero so original function increments from 0
                gfLoadActive[gs] = true;
            }
        }
    }
    
    // --- CALL ORIGINAL: ATB increments from 0, status timers run normally ---
    s_originalATBUpdate();
    
    // v0.10.95: POST-CAP for GF loading counter — per-slot.
    for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
        if (!gfLoadActive[gs]) continue;
        uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
        uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
        uint16_t gfIncrement = *pGFLoad;  // increment from 0
        uint32_t realGFLoad = (uint32_t)savedGFLoad[gs] + gfIncrement;
        if (gfLoadMax[gs] > 1 && realGFLoad >= gfLoadMax[gs]) {
            realGFLoad = gfLoadMax[gs] - 1;  // cap at max-1: prevents fire
        }
        *pGFLoad = (uint16_t)realGFLoad;
    }
    
    // v0.10.82: Real GF timer post-cap REMOVED (v0.10.88) — was Enemy 1's ATB.
    
    // --- POST-CAP: compute increment, restore + cap ---
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        
        if (slot < BATTLE_ALLY_SLOTS) {
            uint16_t maxATB = *(uint16_t*)(base + BENT_MAX_ATB);
            uint16_t* pCurATB = (uint16_t*)(base + BENT_CUR_ATB);
            uint32_t increment = *pCurATB;  // new value after function ran (started from 0)
            uint32_t realATB = savedATB[slot] + increment;
            if (maxATB > 1 && realATB >= maxATB) realATB = maxATB - 1;
            *pCurATB = (uint16_t)realATB;
        } else {
            uint32_t maxATB = *(uint32_t*)(base + BENT_MAX_ATB);
            uint32_t* pCurATB = (uint32_t*)(base + BENT_CUR_ATB);
            uint32_t increment = *pCurATB;  // new value after function ran (started from 0)
            uint64_t realATB = (uint64_t)savedATB[slot] + increment;
            if (maxATB > 1 && realATB >= maxATB) realATB = maxATB - 1;
            *pCurATB = (uint32_t)realATB;
        }
    }

    // v0.10.95: Per-slot GF max inflation on the GAME THREAD.
    // Uses entity+0x7C per-character flag instead of global gfSlot.
    // Iterates all ally slots: any slot with entity+0x7C != 0 gets its
    // compStats+0x16 inflated to 0xFFFF to prevent the fire check from passing.
    if (s_ewmCapGF) {
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
            uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
            if (gfFlag != 0) {
                uint8_t* cs2 = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pMax = (uint16_t*)(cs2 + 0x16);
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
    } else {
        // Cap released — restore real max for all inflated slots
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            if (s_gfMaxInflated[gs]) {
                uint8_t* cs3 = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pMax2 = (uint16_t*)(cs3 + 0x16);
                if (*pMax2 == 0xFFFF && s_gfRealMax[gs] > 0) {
                    *pMax2 = s_gfRealMax[gs];
                }
                s_gfMaxInflated[gs] = false;
                s_gfRealMax[gs] = 0;
            }
        }
    }
    
    // v0.10.87: Sticky gfActive zero REMOVED (v0.10.88).
    
    // v0.10.88: Per-frame GF timer scan on main sandwich path
    if (s_gfScanValid && s_gfScanLogCount < GF_SCAN_LOG_MAX) {
        uint8_t newSnap[GF_SCAN_BYTES];
        memcpy(newSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
        for (int i = 0; i + 1 < GF_SCAN_BYTES; i += 2) {
            int16_t oldVal = *(int16_t*)(s_gfScanSnap + i);
            int16_t newVal = *(int16_t*)(newSnap + i);
            int16_t delta = newVal - oldVal;
            if (delta >= -4 && delta <= -1 && oldVal > 0 && oldVal <= 500) {
                Log::Write("BattleTTS: [GF-SCAN] +0x%03X (0x%08X): %d -> %d (delta=%d)",
                           i, GF_SCAN_BASE + i, (int)oldVal, (int)newVal, (int)delta);
                s_gfScanLogCount++;
            }
        }
        memcpy(s_gfScanSnap, newSnap, GF_SCAN_BYTES);
    }
}

static bool s_ewmEnabled = true;          // Enhanced Wait Mode toggle
static bool s_ewmFreezing = false;        // currently requesting freeze
static bool s_ewmConfigLoaded = false;    // config file has been read
static bool s_ewmOKeyWasDown = false;     // edge detection for O key

static char s_ewmConfigPath[512] = {};

static void EWM_BuildConfigPath()
{
    char dllPath[512];
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)EWM_BuildConfigPath, &hMod);
    GetModuleFileNameA(hMod, dllPath, sizeof(dllPath));
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    snprintf(s_ewmConfigPath, sizeof(s_ewmConfigPath), "%sewm_config.txt", dllPath);
}

static void EWM_LoadConfig()
{
    if (s_ewmConfigLoaded) return;
    s_ewmConfigLoaded = true;
    if (s_ewmConfigPath[0] == '\0') EWM_BuildConfigPath();
    FILE* f = fopen(s_ewmConfigPath, "r");
    if (f) {
        char buf[16] = {};
        fgets(buf, sizeof(buf), f);
        fclose(f);
        s_ewmEnabled = (buf[0] != '0');
        Log::Write("BattleTTS: [EWM] Config loaded: %s (enabled=%d)", s_ewmConfigPath, (int)s_ewmEnabled);
    } else {
        s_ewmEnabled = true;
        FILE* fw = fopen(s_ewmConfigPath, "w");
        if (fw) { fputs("1", fw); fclose(fw); }
        Log::Write("BattleTTS: [EWM] No config file, created with default ON: %s", s_ewmConfigPath);
    }
}

static void EWM_SaveConfig()
{
    if (s_ewmConfigPath[0] == '\0') EWM_BuildConfigPath();
    FILE* f = fopen(s_ewmConfigPath, "w");
    if (f) { fputs(s_ewmEnabled ? "1" : "0", f); fclose(f); }
}

static void EWM_PollToggle()
{
    bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
    bool oPressed = oDown && !s_ewmOKeyWasDown;
    s_ewmOKeyWasDown = oDown;
    if (!oPressed) return;
    s_ewmEnabled = !s_ewmEnabled;
    EWM_SaveConfig();
    // If disabling, immediately release cap
    if (!s_ewmEnabled) {
        s_ewmShouldCap = false;
        s_ewmFreezing = false;
        s_ewmCapExcludeSlot = 0xFF;
        s_ewmCapGF = false;
    }
    const char* msg = s_ewmEnabled ? "Enhanced Wait Mode on" : "Enhanced Wait Mode off";
    ScreenReader::Speak(msg, true);
    Log::Write("BattleTTS: [EWM] Toggled: %s", msg);
}

// Install the MinHook on the ATB update function.
// Called once from Initialize().
static void EWM_InstallHook()
{
    if (s_ewmHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)ATB_UPDATE_FUNC_ADDR,
        (LPVOID)HookedATBUpdate,
        (LPVOID*)&s_originalATBUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)ATB_UPDATE_FUNC_ADDR);
    }
    s_ewmHookInstalled = (st == MH_OK);
    Log::Write("BattleTTS: [EWM] ATB hook @ 0x%08X — %s (trampoline=0x%08X)",
               ATB_UPDATE_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalATBUpdate);
}

// ============================================================================
// v0.10.77: FFNx GF loading counter hook
// ============================================================================
// FFNx (not the vanilla engine) writes to compStats[slot]+0x14 (master GF loading
// counter). Confirmed v0.10.76 via hardware write BP: all writes come from FFNx
// DLL space. Our ATB hook sandwich on +0x14 had no effect because FFNx overwrites
// the value on a separate code path.
//
// Strategy: find FFNx's module at runtime via the JMP at set_midi_volume (0x0046BB40),
// scan for the signature B9 16 F0 CF 01 66 89 06, walk backward to find the
// function entry, and MinHook it with the same cap-at-max-1 sandwich.
// ============================================================================

// s_ffnxGFHookInstalled defined earlier (forward declaration near GF timer hook section)
typedef void (__cdecl *FFNxBattleUpdateFn)(void);
static FFNxBattleUpdateFn s_originalFFNxBattleUpdate = nullptr;
static uint32_t s_ffnxGFFuncAddr = 0;  // resolved address of FFNx function

// s_ffnxHookCallCount forward-declared earlier near GF timer hook section

static void __cdecl HookedFFNxBattleUpdate(void)
{
    InterlockedIncrement(&s_ffnxHookCallCount);

    // If not capping GF, or no GF is loading, just call through
    if (!s_ewmCapGF) {
        s_originalFFNxBattleUpdate();
        return;
    }

    // Check if a GF is actively loading
    uint8_t gfActive = 0;
    int8_t gfSlot = -1;
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (gfActive != 1 || gfSlot < 0 || gfSlot >= BATTLE_ALLY_SLOTS) {
        // No GF loading — call through unmodified
        s_originalFFNxBattleUpdate();
        return;
    }

    // Sandwich: save compStats[gfSlot]+0x14, call original, restore+cap at max-1
    uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gfSlot * BATTLE_COMP_STATS_STRIDE);
    uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
    uint16_t savedLoad = *pGFLoad;
    uint16_t gfMax = *(uint16_t*)(cs + 0x16);

    s_originalFFNxBattleUpdate();

    // After the call, FFNx may have incremented +0x14.
    // Compute the new value and cap at max-1.
    uint16_t newLoad = *pGFLoad;
    if (gfMax > 1 && newLoad >= gfMax) {
        *pGFLoad = gfMax - 1;  // cap: prevent GF from firing
    }
}

// Find FFNx module base by following the E9 JMP at set_midi_volume (0x0046BB40).
// Returns 0 on failure.
static uint32_t FindFFNxModuleBase(void)
{
    __try {
        uint8_t* pSetMidi = (uint8_t*)0x0046BB40;
        if (*pSetMidi != 0xE9) {
            Log::Write("BattleTTS: [FFNx-GF] set_midi_volume @0x0046BB40 is not a JMP (byte=0x%02X)",
                       (unsigned)*pSetMidi);
            return 0;
        }
        // E9 rel32: target = addr + 5 + *(int32_t*)(addr+1)
        int32_t rel = *(int32_t*)(pSetMidi + 1);
        uint32_t target = 0x0046BB40 + 5 + rel;
        Log::Write("BattleTTS: [FFNx-GF] set_midi_volume JMP target = 0x%08X", target);

        // Use VirtualQuery to find the allocation base (= module base)
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) == 0) {
            Log::Write("BattleTTS: [FFNx-GF] VirtualQuery failed for 0x%08X", target);
            return 0;
        }
        uint32_t moduleBase = (uint32_t)(uintptr_t)mbi.AllocationBase;
        Log::Write("BattleTTS: [FFNx-GF] FFNx module base = 0x%08X", moduleBase);
        return moduleBase;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [FFNx-GF] EXCEPTION resolving FFNx module base");
        return 0;
    }
}

// Scan a single module for the GF loading writer signature.
// Signature: B9 16 F0 CF 01 66 89 06 = MOV ECX,0x01CFF016; MOV [ESI],AX
// Returns the address of the first byte of the match, or 0.
static uint32_t ScanModuleForSignature(uint32_t moduleBase)
{
    __try {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)moduleBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(moduleBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        uint32_t moduleSize = nt->OptionalHeader.SizeOfImage;

        static const uint8_t sig[] = { 0xB9, 0x16, 0xF0, 0xCF, 0x01, 0x66, 0x89, 0x06 };
        static const int sigLen = sizeof(sig);

        uint8_t* base = (uint8_t*)moduleBase;
        for (uint32_t i = 0; i + sigLen <= moduleSize; i++) {
            bool match = true;
            for (int j = 0; j < sigLen; j++) {
                if (base[i + j] != sig[j]) { match = false; break; }
            }
            if (match) {
                uint32_t addr = moduleBase + i;
                Log::Write("BattleTTS: [FFNx-GF] Signature found at 0x%08X in module 0x%08X (size=0x%X)",
                           addr, moduleBase, moduleSize);
                return addr;
            }
        }
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Scan ALL loaded modules in the process for the signature.
// The GF loading writer may be in a DLL loaded by FFNx, not FFNx.dll itself.
static uint32_t ScanAllModulesForSignature(void)
{
    HANDLE hProc = GetCurrentProcess();
    HMODULE modules[512];
    DWORD cbNeeded = 0;
    if (!EnumProcessModules(hProc, modules, sizeof(modules), &cbNeeded)) {
        Log::Write("BattleTTS: [FFNx-GF] EnumProcessModules failed (err=%u)", GetLastError());
        return 0;
    }
    int count = cbNeeded / sizeof(HMODULE);
    Log::Write("BattleTTS: [FFNx-GF] Scanning %d loaded modules for signature...", count);

    for (int i = 0; i < count; i++) {
        uint32_t base = (uint32_t)(uintptr_t)modules[i];
        uint32_t result = ScanModuleForSignature(base);
        if (result != 0) return result;
    }
    Log::Write("BattleTTS: [FFNx-GF] Signature not found in any loaded module");
    return 0;
}

// Walk backward from sigAddr to find the function entry point.
// Looks for CC/90 inter-function padding (MSVC pattern).
static uint32_t FindFunctionEntry(uint32_t sigAddr)
{
    __try {
        uint8_t* p = (uint8_t*)sigAddr;
        // Scan backward up to 0x400 bytes
        for (int i = 1; i < 0x400; i++) {
            uint8_t b = p[-i];
            if (b == 0xCC || b == 0x90) {
                // Found padding — the function entry is the first non-padding byte after this
                // Continue backward through the padding
                int padStart = i;
                while (padStart < 0x400 && (p[-padStart] == 0xCC || p[-padStart] == 0x90))
                    padStart++;
                // Now p[-padStart] is non-padding (end of previous function).
                // The entry is at p[-(padStart-1)] = first padding byte... no.
                // Actually: p[-i] is the first padding byte we found (closest to sig).
                // Walk backward through padding. The function entry is the byte AFTER
                // the last padding byte (closest to our code).
                uint32_t entry = sigAddr - i + 1;
                // But we need to continue backward past ALL padding
                int j = i;
                while (j < 0x400) {
                    uint8_t prev = p[-j];
                    if (prev != 0xCC && prev != 0x90) break;
                    j++;
                }
                entry = sigAddr - j + 1;
                Log::Write("BattleTTS: [FFNx-GF] Function entry at 0x%08X (sig-0x%X, padding at sig-0x%X)",
                           entry, (sigAddr - entry), i);
                return entry;
            }
            // Also check for RET (C3) which ends the previous function
            if (b == 0xC3) {
                uint32_t entry = sigAddr - i + 1;
                Log::Write("BattleTTS: [FFNx-GF] Function entry at 0x%08X (after RET at sig-0x%X)",
                           entry, i);
                return entry;
            }
        }
        Log::Write("BattleTTS: [FFNx-GF] Could not find function entry (no padding/RET in 0x400 bytes)");
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [FFNx-GF] EXCEPTION scanning for function entry");
        return 0;
    }
}

static void EWM_InstallFFNxGFHook(void)
{
    if (s_ffnxGFHookInstalled) return;

    // Step 1+2: Scan all loaded modules for signature
    // The writer may be in a DLL loaded by FFNx, not FFNx.dll itself.
    uint32_t sigAddr = ScanAllModulesForSignature();
    if (sigAddr == 0) {
        Log::Write("BattleTTS: [FFNx-GF] Signature scan failed — GF timer hook skipped");
        return;
    }

    // Step 3: Find function entry
    uint32_t funcAddr = FindFunctionEntry(sigAddr);
    if (funcAddr == 0) {
        Log::Write("BattleTTS: [FFNx-GF] Function entry not found — GF timer hook skipped");
        return;
    }
    s_ffnxGFFuncAddr = funcAddr;

    // Dump first 32 bytes of the function for diagnostic
    __try {
        uint8_t* code = (uint8_t*)funcAddr;
        char hex[200] = {};
        int p = 0;
        for (int b = 0; b < 32; b++)
            p += snprintf(hex + p, sizeof(hex) - p, "%02X ", code[b]);
        Log::Write("BattleTTS: [FFNx-GF] Function code[0..31]: %s", hex);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Step 4: MinHook it
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)funcAddr,
        (LPVOID)HookedFFNxBattleUpdate,
        (LPVOID*)&s_originalFFNxBattleUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)funcAddr);
    }
    s_ffnxGFHookInstalled = (st == MH_OK);
    Log::Write("BattleTTS: [FFNx-GF] MinHook @ 0x%08X — %s (trampoline=0x%08X)",
               funcAddr, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalFFNxBattleUpdate);
}

// Menu phase lifecycle for a typical command:
//   0 or 32 = command menu / sub-menu open (DECIDING)
//   64 = Limit Break showing (DECIDING)
//   3 = brief transition after command select (DECIDING — loading target UI)
//   11 = target selection (DECIDING)
//   14 = target confirmed, action committed (EXECUTING — release here)
//   21, 23, 33, 34 = animation/cleanup (EXECUTING)
//   1, 4 = turn setup transitions (still DECIDING — keep freeze)
//
// BUG FIX (v0.10.38): When active_char_id changes (new turn starts), menuPhase
// may still be 34 from the PREVIOUS character's execution. We must always freeze
// on a new turn edge regardless of menuPhase. Only use menuPhase for RELEASE.

static uint8_t s_ewmLastActiveChar = 0xFF;  // track active_char_id changes for turn edge
static bool s_ewmNewTurnGrace = false;       // v0.10.41: suppress phase-based release until non-executing phase seen

static bool EWM_IsExecutingPhase(uint8_t phase)
{
    return (phase == 14 || phase == 21 || phase == 23 || phase == 33 || phase == 34);
}

// GF loading diagnostic (v0.10.57): logs ATB values for all slots while cap
// is active. Tells us whether GF charge gauge uses entity+0x0C (same as ATB)
// or a separate counter. Run from mod thread, reads memory directly.
static DWORD s_ewmDiagLastTick = 0;
static int s_ewmDiagCount = 0;
static const int EWM_DIAG_MAX = 40;  // max samples per cap session

static void EWM_DiagLogATB(const char* label)
{
    DWORD now = GetTickCount();
    if (now - s_ewmDiagLastTick < 500) return;  // every 500ms
    if (s_ewmDiagCount >= EWM_DIAG_MAX) return;
    s_ewmDiagLastTick = now;
    s_ewmDiagCount++;
    
    char buf[512];
    int pos = 0;
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        __try {
            if (slot < BATTLE_ALLY_SLOTS) {
                uint16_t cur = *(uint16_t*)(base + BENT_CUR_ATB);
                uint16_t max = *(uint16_t*)(base + BENT_MAX_ATB);
                uint16_t hp  = *(uint16_t*)(base + BENT_CUR_HP);
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                               "s%d=%u/%u(hp%u) ", slot, (unsigned)cur, (unsigned)max, (unsigned)hp);
            } else {
                uint32_t cur = *(uint32_t*)(base + BENT_CUR_ATB);
                uint32_t max = *(uint32_t*)(base + BENT_MAX_ATB);
                uint32_t hp  = *(uint32_t*)(base + BENT_CUR_HP);
                if (max > 0) {  // only log active enemies
                    pos += snprintf(buf + pos, sizeof(buf) - pos,
                                   "s%d=%u/%u(hp%u) ", slot, cur, max, hp);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    Log::Write("BattleTTS: [EWM-DIAG] #%d %s ATB: %s", s_ewmDiagCount, label, buf);
}

// Called every frame during battle from Update() (mod thread).
// Sets s_ewmShouldCap + s_ewmCapExcludeSlot which the game-thread hook reads.
//
// ATB CAPPING (v0.10.55): Instead of freezing ATB entirely, we let ATB fill
// at normal Speed-based rates but cap all entities at ATB_max - 1. This means:
//   - Nobody can trigger a new turn while the player is deciding
//   - Speed-based turn ratios are fully preserved (no extra player turns)
//   - When the cap lifts, the fastest entity triggers next (within 1 tick)
// The deciding character is excluded from capping (their ATB is already at max).
static void EWM_UpdateBattle()
{
    if (!s_ewmEnabled || !s_ewmHookInstalled) {
        if (s_ewmFreezing) {
            s_ewmShouldCap = false;
            s_ewmCapExcludeSlot = 0xFF;
            s_ewmCapGF = false;
            s_ewmFreezing = false;
            Log::Write("BattleTTS: [EWM] ATB cap released (EWM disabled or hook missing)");
        }
        return;
    }
    
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    // v0.10.75: GF cap stays active during turn transitions (activeChar==0xFF)
    // as long as a GF is loading. Only release when an action is executing.
    // This closes the gap where the GF loading counter crossed max during
    // the brief uncapped frames between turns.
    // v0.10.88: Removed sticky hide accounting (flag-hiding abandoned)
    bool gfIsLoading = false;
    __try { gfIsLoading = (*(uint8_t*)0x01D76971 == 1); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    if (activeChar < 3) {
        s_ewmCapGF = true;  // player is deciding — cap GF timer
        EWM_ClampGFState();   // v0.10.68: clamp state68 to prevent GF fire
        
        // Detect new turn edge: active_char_id changed to a valid player slot
        bool newTurnEdge = (activeChar != s_ewmLastActiveChar);
        s_ewmLastActiveChar = activeChar;
        
        uint8_t menuPhase = 0;
        __try { menuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        if (newTurnEdge) {
            // New turn — ALWAYS cap, regardless of menuPhase.
            // menuPhase may still be 34 from the previous character's execution.
            s_ewmFreezing = true;
            s_ewmCapExcludeSlot = activeChar;
            s_ewmShouldCap = true;
            s_ewmNewTurnGrace = true;  // suppress phase-based release until non-executing phase seen
            s_ewmDiagCount = 0;  // reset diagnostic counter for new cap session
            s_ewmDiagLastTick = 0;
            Log::Write("BattleTTS: [EWM] ATB capped (new turn, char=%d, phase=%u)",
                       (int)activeChar, (unsigned)menuPhase);
        } else {
            // Same turn continuing — use menuPhase to decide cap vs release
            if (!EWM_IsExecutingPhase(menuPhase)) {
                // Player is deciding (command menu, sub-menu, target select)
                if (s_ewmNewTurnGrace) {
                    // Grace period satisfied — we've seen a non-executing phase,
                    // meaning the engine has transitioned to the command menu.
                    s_ewmNewTurnGrace = false;
                    Log::Write("BattleTTS: [EWM] Grace cleared (deciding phase=%u)", (unsigned)menuPhase);
                }
                if (!s_ewmFreezing) {
                    s_ewmFreezing = true;
                    Log::Write("BattleTTS: [EWM] ATB capped (deciding, char=%d, phase=%u)",
                               (int)activeChar, (unsigned)menuPhase);
                }
                s_ewmCapExcludeSlot = activeChar;
                s_ewmShouldCap = true;
                EWM_DiagLogATB("cap");  // v0.10.57: log ATB values while capped
            } else {
                // Phase says executing — but check grace period first
                if (s_ewmNewTurnGrace) {
                    // Still in grace period: phase=34 is STALE from previous character.
                    // Keep cap active until we see a non-executing phase.
                    s_ewmShouldCap = true;
                } else {
                    // Action executing (no grace) — release cap
                    if (s_ewmFreezing) {
                        s_ewmFreezing = false;
                        s_ewmShouldCap = false;
                        s_ewmCapExcludeSlot = 0xFF;
                        s_ewmCapGF = false;
                        EWM_RestoreGFPatch();
                        Log::Write("BattleTTS: [EWM] ATB cap released (executing, phase=%u)",
                                   (unsigned)menuPhase);
                    }
                }
            }
        }
    } else {
        s_ewmLastActiveChar = 0xFF;
        s_ewmNewTurnGrace = false;
        // Keep GF cap active during turn transitions if a GF is loading.
        // Only release when no GF is loading.
        if (gfIsLoading) {
            s_ewmCapGF = true;
            EWM_ClampGFState();
        } else {
            if (s_ewmCapGF) {
                s_ewmCapGF = false;
                EWM_RestoreGFPatch();
            }
        }
        if (s_ewmFreezing) {
            s_ewmFreezing = false;
            s_ewmShouldCap = false;
            s_ewmCapExcludeSlot = 0xFF;
            Log::Write("BattleTTS: [EWM] ATB cap released (no turn active, gfLoading=%d)",
                       (int)gfIsLoading);
        }
    }
}

// ============================================================================
// Turn announcement + Command menu TTS (v0.10.11)
// ============================================================================

// Confirmed addresses from v0.10.10 diagnostic
static const uint32_t BATTLE_CMD_CURSOR     = 0x01D76843; // BYTE, 0-3 (command slot index)
static const uint32_t BATTLE_MENU_PHASE     = 0x01D768D0; // BYTE (32=cmd menu, 3=executing, etc.)
static const uint32_t BATTLE_SUBMENU_CURSOR = 0x01D768EC; // BYTE, 0-N sub-menu list cursor (v0.10.16 confirmed)

// Savemap addresses
static const uint32_t SAVEMAP_PARTY_FORMATION = 0x1CFE74C; // 3 bytes: slot→charIdx (confirmed)
static const uint32_t SAVEMAP_CHAR_DATA_BASE  = 0x1CFE0E8; // char structs (UNCORRECTED — savemap stores ability IDs, not battle cmd IDs)
static const uint32_t SAVEMAP_CHAR_STRIDE     = 0x98;      // 152 bytes per character
static const uint32_t SAVEMAP_CHAR_EQUIP_CMD  = 0x50;      // 3 equipped command IDs within char struct

// Character names by savemap index (0=Squall through 7=Edea)
static const char* CHAR_NAMES[8] = {
    "Squall", "Zell", "Irvine", "Quistis",
    "Rinoa", "Selphie", "Seifer", "Edea"
};

// Savemap stores GF ABILITY IDs for equipped commands, not battle command IDs.
// Ability IDs = battle command IDs + 0x12.
// Slot 0 is always Attack (hardcoded, not stored in savemap).
static const char* GetCommandName(uint8_t abilityId) {
    switch (abilityId) {
        // Special: Attack is slot 0, hardcoded as 0x01
        case 0x01: return "Attack";
        // GF ability IDs for junctioned commands (ability = battle_cmd + 0x12)
        case 0x14: return "Magic";
        case 0x15: return "GF";
        case 0x16: return "Draw";
        case 0x17: return "Item";
        case 0x18: return "Card";
        case 0x19: return "Devour";
        case 0x21: return "MiniMog";
        case 0x22: return "Defend";
        case 0x23: return "Darkside";
        case 0x24: return "Recover";
        case 0x25: return "Absorb";
        case 0x26: return "Revive";
        case 0x27: return "LV Down";
        case 0x28: return "LV Up";
        case 0x29: return "Kamikaze";
        case 0x2A: return "Expendx2-1";
        case 0x2B: return "Expendx3-1";
        case 0x2C: return "Mad Rush";
        case 0x2D: return "Doom";
        case 0x36: return "Mug";
        case 0x38: return "Treatment";
        default:   return "???";
    }
}

// ============================================================================
// v0.10.17: Magic name table (kernel.bin IDs 0-56)
// ============================================================================
// IDs from Doomtrain wiki / kernel.bin Section 4. Verified against game at runtime.
static const char* MAGIC_NAMES[] = {
    "(none)",      // 0x00
    "Fire",        // 0x01
    "Fira",        // 0x02
    "Firaga",      // 0x03
    "Blizzard",    // 0x04
    "Blizzara",    // 0x05
    "Blizzaga",    // 0x06
    "Thunder",     // 0x07
    "Thundara",    // 0x08
    "Thundaga",    // 0x09
    "Water",       // 0x0A
    "Aero",        // 0x0B
    "Bio",         // 0x0C
    "Demi",        // 0x0D
    "Holy",        // 0x0E
    "Flare",       // 0x0F
    "Meteor",      // 0x10
    "Quake",       // 0x11
    "Tornado",     // 0x12
    "Ultima",      // 0x13
    "Apocalypse",  // 0x14
    "Cure",        // 0x15
    "Cura",        // 0x16
    "Curaga",      // 0x17
    "Life",        // 0x18
    "Full-Life",   // 0x19
    "Regen",       // 0x1A
    "Esuna",       // 0x1B
    "Dispel",      // 0x1C
    "Protect",     // 0x1D
    "Shell",       // 0x1E
    "Reflect",     // 0x1F
    "Aura",        // 0x20
    "Double",      // 0x21
    "Triple",      // 0x22
    "Haste",       // 0x23
    "Slow",        // 0x24
    "Stop",        // 0x25
    "Blind",       // 0x26
    "Confuse",     // 0x27
    "Sleep",       // 0x28
    "Silence",     // 0x29
    "Break",       // 0x2A
    "Death",       // 0x2B
    "Drain",       // 0x2C
    "Pain",        // 0x2D
    "Berserk",     // 0x2E
    "Float",       // 0x2F
    "Zombie",      // 0x30
    "Meltdown",    // 0x31
    "Scan",        // 0x32
    "Full-Cure",   // 0x33
    "Wall",        // 0x34
    "Rapture",     // 0x35
    "Percent",     // 0x36
    "Catastrophe", // 0x37
    "The End",     // 0x38
};
static const int MAGIC_NAMES_COUNT = sizeof(MAGIC_NAMES) / sizeof(MAGIC_NAMES[0]);

static const char* GetMagicName(uint8_t id) {
    if (id < MAGIC_NAMES_COUNT) return MAGIC_NAMES[id];
    return "???";
}

// ============================================================================
// v0.10.17: Sub-menu tracking state
// ============================================================================

struct MagicEntry { uint8_t id; uint8_t qty; };
static MagicEntry s_turnMagicList[32] = {};   // filtered list of spells with qty>0
static int s_turnMagicCount = 0;               // number of entries in filtered list
static uint8_t s_turnSubmenuCursor = 0xFF;     // last sub-menu cursor value
static bool s_inSubmenu = false;               // true when sub-menu is open
static uint8_t s_submenuCommandId = 0;         // ability ID of the command that opened the sub-menu
static bool s_magicListBuilt = false;          // true after we build the magic list for current turn
static DWORD s_submenuDebounceTick = 0;        // GetTickCount() when debounce started
static bool s_submenuDebouncing = false;        // true for 300ms after turn start (ignores sub-menu cursor)
static bool s_pendingSubmenuEntry = false;       // v0.10.112: delayed submenu entry after command scroll
static DWORD s_pendingSubmenuTick = 0;           // v0.10.112: GetTickCount() when pending entry was scheduled

// Build the filtered magic list for the active character.
// Reads savemap char struct +0x10 (32 slots × 2 bytes: magic_id, qty).
// Only includes slots with qty > 0, preserving savemap order (ascending magic_id).
static void BuildMagicList(uint8_t partySlot)
{
    s_turnMagicCount = 0;
    s_magicListBuilt = false;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        uint8_t* magicBase = charBase + 0x10;  // 32 slots × 2 bytes
        
        for (int i = 0; i < 32 && s_turnMagicCount < 32; i++) {
            uint8_t magicId = magicBase[i * 2];
            uint8_t qty = magicBase[i * 2 + 1];
            if (magicId == 0 || qty == 0) continue;
            s_turnMagicList[s_turnMagicCount].id = magicId;
            s_turnMagicList[s_turnMagicCount].qty = qty;
            s_turnMagicCount++;
        }
        
        s_magicListBuilt = true;
        Log::Write("BattleTTS: [MAGIC-LIST] charIdx=%d has %d spells:", (int)charIdx, s_turnMagicCount);
        for (int i = 0; i < s_turnMagicCount; i++) {
            Log::Write("BattleTTS: [MAGIC-LIST]   [%d] id=0x%02X (%s) x%d",
                       i, s_turnMagicList[i].id,
                       GetMagicName(s_turnMagicList[i].id),
                       (int)s_turnMagicList[i].qty);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [MAGIC-LIST] EXCEPTION reading magic for partySlot=%d", (int)partySlot);
    }
}

// ============================================================================
// v0.10.98: GF sub-menu list (junctioned GFs for active character)
// ============================================================================
struct GFEntry { uint8_t gfIdx; char name[32]; };
static GFEntry s_turnGFList[16] = {};          // filtered list of junctioned GFs
static int s_turnGFCount = 0;                  // number of entries in filtered list
static bool s_gfListBuilt = false;             // true after we build the GF list for current turn

// Build the filtered GF list for the active character.
// Reads savemap char struct +0x58 (uint16 bitmask of junctioned GFs).
// Only includes GFs that are junctioned. Order follows bit index (0-15).
static void BuildGFList(uint8_t partySlot)
{
    s_turnGFCount = 0;
    s_gfListBuilt = false;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        uint16_t gfMask = *(uint16_t*)(charBase + 0x58);
        
        for (int gfIdx = 0; gfIdx < 16 && s_turnGFCount < 16; gfIdx++) {
            if (!(gfMask & (1 << gfIdx))) continue;
            
            uint8_t* gfBase = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
            s_turnGFList[s_turnGFCount].gfIdx = (uint8_t)gfIdx;
            DecodeFF8String(gfBase, s_turnGFList[s_turnGFCount].name, sizeof(s_turnGFList[s_turnGFCount].name));
            s_turnGFCount++;
        }
        
        s_gfListBuilt = true;
        Log::Write("BattleTTS: [GF-LIST] charIdx=%d has %d junctioned GFs:", (int)charIdx, s_turnGFCount);
        for (int i = 0; i < s_turnGFCount; i++) {
            Log::Write("BattleTTS: [GF-LIST]   [%d] gfIdx=%d name='%s'",
                       i, (int)s_turnGFList[i].gfIdx, s_turnGFList[i].name);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [GF-LIST] EXCEPTION reading GFs for partySlot=%d", (int)partySlot);
    }
}

// ============================================================================
// v0.10.101: Item sub-menu list (battle items from display struct)
// ============================================================================
// Battle items: The display struct at 0x1D8DFF4 IS the authoritative visual layout.
// v0.10.105 diagnostic proved it persists from field menu into battle.
// Format: 32 x {uint8 id, uint8 qty} in the visual order set by Items > Battle.
// Engine renders only entries with qty>0, so cursor position maps to Nth non-zero entry.
static const uint32_t BATTLE_ORDER_ADDR = 0x1CFE77C;   // uint8[32] inventory slot indices
static const uint32_t ITEM_INVENTORY_ADDR = 0x1CFE79C;  // 198 x {id, qty} byte pairs
static const int BATTLE_ITEM_MAX = 32;

struct BattleItemEntry { uint8_t id; uint8_t qty; };
static BattleItemEntry s_turnItemList[BATTLE_ITEM_MAX] = {};
static int s_turnItemCount = 0;
static bool s_itemListBuilt = false;

// v0.10.103: MinHook on 0x4F81F0 REMOVED (v0.10.102 proved ESI calling convention
// mismatch — controller struct passed via ESI register, not cdecl stack argument).
// v0.10.104: POOL-SCAN approach — deep research revealed the Item controller is a
// pool node at 0x1D76BC8 (10 slots × 0x78 bytes). Scan for [node+0x0C]==0x4F81F0
// to find the active Item controller, then read [node+0x54] for absolute inventory
// index. No hooking or list-building needed.

// ============================================================================
// v0.10.104: Battle UI task pool scanner
// ============================================================================
// The engine uses a 10-slot pool at 0x1D76BC8 for battle UI controllers.
// Each node is 0x78 bytes. The Item handler address (0x4F81F0) is stored at
// +0x0C (or possibly +0x08 — we check both). When the Item sub-menu is open,
// the node contains the controller state including:
//   +0x20: inventory pointer (should be 0x01CFE79C)
//   +0x24: battle_order pointer (should be 0x01CFE77C)
//   +0x54: absolute inventory index (int16) — the currently highlighted item
static const uint32_t POOL_BASE     = 0x1D76BC8;
static const int      POOL_SLOTS    = 10;
static const int      POOL_STRIDE   = 0x78;
static const uint32_t ITEM_HANDLER  = 0x4F81F0;
static const int      HANDLER_OFF_A = 0x0C;  // primary handler offset
static const int      HANDLER_OFF_B = 0x08;  // fallback handler offset
static const int      POOL_INUSE    = 0x12;  // uint16 in-use flag
static const int      POOL_INV_PTR  = 0x20;  // uint32 inventory pointer
static const int      POOL_CURSOR   = 0x54;  // int16 absolute inventory index

// Find the active Item controller node in the pool.
// Returns pointer to the node, or NULL if not found.
static uint8_t* FindItemControllerNode()
{
    __try {
        for (int i = 0; i < POOL_SLOTS; i++) {
            uint8_t* node = (uint8_t*)(POOL_BASE + i * POOL_STRIDE);
            uint32_t handler = *(uint32_t*)(node + HANDLER_OFF_A);
            if (handler == ITEM_HANDLER) return node;
        }
        // Fallback: check +0x08 in case handler is stored there
        for (int i = 0; i < POOL_SLOTS; i++) {
            uint8_t* node = (uint8_t*)(POOL_BASE + i * POOL_STRIDE);
            uint32_t handler = *(uint32_t*)(node + HANDLER_OFF_B);
            if (handler == ITEM_HANDLER) return node;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// Read the currently highlighted item from the pool node.
// Returns true if successful and populates outId/outQty.
static bool ReadItemFromPoolNode(uint8_t* node, uint8_t* outId, uint8_t* outQty, int16_t* outAbsIdx)
{
    if (!node) return false;
    __try {
        int16_t absIdx = *(int16_t*)(node + POOL_CURSOR);
        if (absIdx < 0 || absIdx >= 198) return false;
        uint8_t* inv = (uint8_t*)ITEM_INVENTORY_ADDR;
        *outAbsIdx = absIdx;
        *outId = inv[absIdx * 2];
        *outQty = inv[absIdx * 2 + 1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// v0.10.105: Display struct at 0x1D8DFF4 is the authoritative visual layout.
// Confirmed by F12 diagnostic: it IS populated during battle (persists from
// field menu Items > Battle arrangement). Contains 32 x {id, qty} pairs in
// visual order. Engine renders only entries with qty>0, skipping empties.
// We filter the same way to build a cursor-indexed list matching the screen.
static const uint32_t ITEM_DISPLAY_STRUCT = 0x1D8DFF4;  // 32 x {uint8 id, uint8 qty}

static void BuildItemList()
{
    s_turnItemCount = 0;
    s_itemListBuilt = false;
    __try {
        uint8_t* battleOrder = (uint8_t*)BATTLE_ORDER_ADDR;
        uint8_t* inventory   = (uint8_t*)ITEM_INVENTORY_ADDR;
        
        // v0.10.105: Two modes depending on display struct state.
        // When display struct at 0x1D8DFF4 is populated (after visiting field
        // menu Items > Battle), cursor indexes into ALL 32 positions (with gaps).
        // When zeroed (normal gameplay), cursor indexes into filtered battle items.
        uint8_t* ds = (uint8_t*)ITEM_DISPLAY_STRUCT;
        bool dsPopulated = false;
        for (int i = 0; i < 64; i++) {
            if (ds[i] != 0) { dsPopulated = true; break; }
        }
        
        int visCount = 0;
        if (dsPopulated) {
            // Display struct mode: cursor indexes ALL 32 positions including empties
            for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
                s_turnItemList[i].id  = ds[i * 2];
                s_turnItemList[i].qty = ds[i * 2 + 1];
                if (s_turnItemList[i].id >= 1 && s_turnItemList[i].id < 33 && s_turnItemList[i].qty > 0)
                    visCount++;
            }
            s_turnItemCount = BATTLE_ITEM_MAX;
            Log::Write("BattleTTS: [ITEM-LIST] Display struct mode: %d visible of %d", visCount, s_turnItemCount);
        } else {
            // Filtered mode: cursor indexes only valid battle items (compacted)
            for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
                uint8_t invIdx = battleOrder[i];
                if (invIdx >= 198) continue;
                uint8_t id  = inventory[invIdx * 2];
                uint8_t qty = inventory[invIdx * 2 + 1];
                if (id >= 1 && id < 33 && qty > 0) {
                    s_turnItemList[s_turnItemCount].id  = id;
                    s_turnItemList[s_turnItemCount].qty = qty;
                    s_turnItemCount++;
                    visCount++;
                }
            }
            Log::Write("BattleTTS: [ITEM-LIST] Filtered mode: %d battle items", s_turnItemCount);
        }
        
        s_itemListBuilt = true;
        Log::Write("BattleTTS: [ITEM-LIST] battle_order loaded: %d battle items of %d total", visCount, s_turnItemCount);
        for (int i = 0; i < s_turnItemCount; i++) {
            uint8_t id = s_turnItemList[i].id;
            uint8_t qty = s_turnItemList[i].qty;
            if (id >= 1 && id < 33 && qty > 0) {
                Log::Write("BattleTTS: [ITEM-LIST]   [%d] bo->inv id=%u qty=%u -> %s",
                           i, (unsigned)id, (unsigned)qty, GetBattleItemName(id));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: [ITEM-LIST] EXCEPTION reading battle_order");
    }
}

// ============================================================================
// v0.10.109: Draw sub-menu list (drawable spells from target enemy)
// ============================================================================
// Draw spell slots at 0x1D28F18 + enemyIdx * 0x47, 4 slots per enemy.
// Each slot is uint32, byte 0 = magic_id (0 = empty). Confirmed v0.10.107 diagnostic.
// Target enemy from target bitmask at 0x01D76884 (persists into Draw phase).
struct DrawEntry { uint8_t magicId; };
static DrawEntry s_turnDrawList[4] = {};        // all 4 slots (including empties)
static int s_turnDrawCount = 0;                 // number of non-empty entries
static bool s_drawListBuilt = false;
static int s_drawTargetSlot = -1;               // entity slot (3-6) of draw target

// v0.10.109 fix: Draw uses a DIFFERENT cursor byte from Magic/GF/Item.
// 0x01D768EC only fires during phase transitions (engine init), NOT during
// active up/down navigation of the draw spell list.
// 0x01D768D8 is the real draw cursor (confirmed by CURSOR-HUNT diagnostic).
static const uint32_t DRAW_CURSOR_ADDR = 0x01D768D8;
static const uint32_t DRAW_STOCK_CAST_ADDR = 0x01D768D9; // v0.10.111: 0=Stock, 1=Cast
static uint8_t s_drawCursorPrev = 0xFF;         // previous draw cursor value for change detection
static uint8_t s_drawStockCastPrev = 0xFF;      // previous Stock/Cast cursor value
static uint8_t s_lastDrawerPartySlot = 0xFF;    // v0.10.112: party slot of character who last used Draw
static uint8_t s_drawLastMenuPhase = 0xFF;      // v0.10.112: track menuPhase for phase-transition resets

static void BuildDrawList()
{
    s_turnDrawCount = 0;
    s_drawListBuilt = false;
    s_drawTargetSlot = -1;
    memset(s_turnDrawList, 0, sizeof(s_turnDrawList));
    
    // Determine which enemy from target bitmask
    uint8_t tgtMask = 0;
    __try { tgtMask = *(uint8_t*)0x01D76884; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (tgtMask == 0) return;
    
    int slot = BitmaskToSlot(tgtMask);
    if (slot < BATTLE_ALLY_SLOTS || slot >= BATTLE_TOTAL_SLOTS) return;
    int enemyIdx = slot - BATTLE_ALLY_SLOTS;
    s_drawTargetSlot = slot;
    
    // Read draw spell slots for this enemy
    uint32_t enemyBase = DRAW_SPELL_BASE + enemyIdx * DRAW_ENEMY_STRIDE;
    __try {
        for (int i = 0; i < DRAW_SLOTS_PER_ENEMY; i++) {
            uint8_t magicId = *(uint8_t*)(enemyBase + i * DRAW_SLOT_SIZE);
            s_turnDrawList[i].magicId = magicId;
            if (magicId != 0) s_turnDrawCount++;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    s_drawListBuilt = true;
    
    // Log the draw list
    char nameBuf[64];
    const char* enemyName = GetSlotName(slot, nameBuf, sizeof(nameBuf));
    Log::Write("BattleTTS: [DRAW-LIST] target=%s (slot%d) %d drawable spells:",
               enemyName, slot, s_turnDrawCount);
    for (int i = 0; i < DRAW_SLOTS_PER_ENEMY; i++) {
        uint8_t mid = s_turnDrawList[i].magicId;
        if (mid != 0) {
            Log::Write("BattleTTS: [DRAW-LIST]   [%d] id=%u (%s)",
                       i, (unsigned)mid, GetMagicName(mid));
        }
    }
}

// Turn/command tracking state
static uint8_t s_turnActiveCharId = 0xFF;    // last active_char_id we announced
static uint8_t s_turnCmdCursor = 0xFF;       // last command cursor position
static uint8_t s_turnCharCommands[4] = {};    // command IDs for current turn's 4 slots

static const char* GetBattleCharName(uint8_t partySlot) {
    if (partySlot >= 3) return "???";
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx < 8) return CHAR_NAMES[charIdx];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return "???";
}

static void BuildCharCommandList(uint8_t partySlot) {
    s_turnCharCommands[0] = 0x01; // Attack always first
    s_turnCharCommands[1] = 0x00;
    s_turnCharCommands[2] = 0x00;
    s_turnCharCommands[3] = 0x00;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        __try {
            s_turnCharCommands[1] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 0];
            s_turnCharCommands[2] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 1];
            s_turnCharCommands[3] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 2];
            Log::Write("BattleTTS: [CMD] charIdx=%d cmds=[0x%02X,0x%02X,0x%02X] = [%s,%s,%s]",
                       (int)charIdx, s_turnCharCommands[1], s_turnCharCommands[2], s_turnCharCommands[3],
                       GetCommandName(s_turnCharCommands[1]),
                       GetCommandName(s_turnCharCommands[2]),
                       GetCommandName(s_turnCharCommands[3]));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [CMD] EXCEPTION reading cmds for charIdx=%d", (int)charIdx);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void PollTurnAndCommands()
{
    if (!s_pActiveCharId) return;
    
    __try {
        uint8_t activeChar = *s_pActiveCharId;
        
        // Turn start: active_char_id transitions to a valid slot
        if (activeChar < 3 && activeChar != s_turnActiveCharId) {
            s_turnActiveCharId = activeChar;
            BuildCharCommandList(activeChar);
            
            // Reset sub-menu state for new turn
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
            s_drawLastMenuPhase = 0xFF;
            s_pendingSubmenuEntry = false;
            s_pendingSubmenuTick = 0;
            s_submenuDebouncing = true;
            s_submenuDebounceTick = GetTickCount();
            
            // v0.10.97 fix: Snapshot current target bitmask+scope on new turn so
            // PollTargetSelection doesn't see a false "change" from 0 to stale value.
            __try { s_lastTargetBitmask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { s_lastTargetScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_inTargetSelect = false;
            
            // v0.10.22: Check limit toggle byte for initial announcement
            uint8_t initToggle = 0;
            __try { initToggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_limitBreakActive = (initToggle == 64);
            s_lastLimitToggle = initToggle;
            
            // Announce "[Name]'s turn. [First command]." (or Limit Break if toggle=64)
            const char* name = GetBattleCharName(activeChar);
            const char* cmd = s_limitBreakActive ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s's turn. %s.", name, cmd);
            BattleSpeak(buf, PRIO_TURN, true);
            Log::Write("BattleTTS: [TURN] %s (slot %d, limitToggle=%u)", buf, (int)activeChar, (unsigned)initToggle);
            
            // Set cursor to 0 so we don't re-announce the initial command
            s_turnCmdCursor = 0;
        }
        else if (activeChar == 0xFF && s_turnActiveCharId != 0xFF) {
            // Turn ended
            s_turnActiveCharId = 0xFF;
            s_turnCmdCursor = 0xFF;
            s_inSubmenu = false;
            s_turnSubmenuCursor = 0xFF;
        }
        
        // Command cursor navigation (only while a turn is active)
        if (s_turnActiveCharId < 3) {
            bool cmdCursorChangedThisFrame = false;  // v0.10.112: suppress false submenu entry
            uint8_t cursor = *(uint8_t*)BATTLE_CMD_CURSOR;
            if (cursor < 4 && cursor != s_turnCmdCursor) {
                s_turnCmdCursor = cursor;
                // Returning to command menu from sub-menu
                if (s_inSubmenu) {
                    s_inSubmenu = false;
                    s_turnSubmenuCursor = 0xFF;
                    // v0.10.112: Reset draw tracking so re-entry announces initial items
                    s_drawCursorPrev = 0xFF;
                    s_drawStockCastPrev = 0xFF;
                    s_drawListBuilt = false;
                    s_drawLastMenuPhase = 0xFF;
                    Log::Write("BattleTTS: [SUBMENU] Exited sub-menu, back to command menu");
                }
                // v0.10.112: Suppress false submenu entry on this frame AND capture
                // baseline. Then schedule a delayed forced entry after 150ms so the
                // command name has time to speak before the submenu item queues.
                cmdCursorChangedThisFrame = true;
                __try { s_turnSubmenuCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                s_pendingSubmenuEntry = true;
                s_pendingSubmenuTick = GetTickCount();
                // v0.10.22: cursor=0 may be Attack or Limit Break depending on toggle byte
                const char* cmd;
                if (cursor == 0) {
                    uint8_t toggle = 0;
                    __try { toggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    s_limitBreakActive = (toggle == 64);
                    s_lastLimitToggle = toggle;
                    cmd = s_limitBreakActive ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
                } else {
                    cmd = GetCommandName(s_turnCharCommands[cursor]);
                }
                BattleSpeak(cmd, PRIO_MENU, true);
                Log::Write("BattleTTS: [CMD-NAV] cursor=%d -> %s", (int)cursor, cmd);
            }
            
            // v0.10.19/20: Limit Break toggle detection moved to PollLimitToggleFast()
            
            // v0.10.17: Sub-menu cursor tracking
            // Debounce: ignore sub-menu cursor for 300ms after turn start.
            // The engine resets this byte during turn transitions, causing false
            // sub-menu entry detection (v0.10.17 glitch: "Fire" spoken on cmd menu).
            if (s_submenuDebouncing) {
                if (GetTickCount() - s_submenuDebounceTick > 300) {
                    s_submenuDebouncing = false;
                    // Capture current value as baseline after debounce expires
                    s_turnSubmenuCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR;
                }
            }
            
            uint8_t subCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR;
            if (!s_submenuDebouncing && !cmdCursorChangedThisFrame && subCursor != s_turnSubmenuCursor) {
                if (!s_inSubmenu && s_turnCmdCursor < 4) {
                    // Entering sub-menu — record which command opened it
                    s_submenuCommandId = s_turnCharCommands[s_turnCmdCursor];
                    s_inSubmenu = true;
                    s_pendingSubmenuEntry = false;  // v0.10.112: cancel delayed entry
                    Log::Write("BattleTTS: [SUBMENU] Entered sub-menu for cmd 0x%02X (%s) at cursor %d",
                               (unsigned)s_submenuCommandId,
                               GetCommandName(s_submenuCommandId),
                               (int)s_turnCmdCursor);
                    
                    // Build spell list if Magic sub-menu
                    if (s_submenuCommandId == 0x14 && !s_magicListBuilt) { // 0x14 = Magic ability ID
                        BuildMagicList(s_turnActiveCharId);
                    }
                    // v0.10.98: Build GF list if GF sub-menu
                    if (s_submenuCommandId == 0x15 && !s_gfListBuilt) { // 0x15 = GF ability ID
                        BuildGFList(s_turnActiveCharId);
                    }
                    // v0.10.104: Build item list if Item sub-menu
                    if (s_submenuCommandId == 0x17 && !s_itemListBuilt) {
                        BuildItemList();
                    }
                    // v0.10.109: Build draw list if Draw sub-menu
                    if (s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                        s_lastDrawerPartySlot = s_turnActiveCharId;  // v0.10.112: track who is drawing
                        BuildDrawList();
                    }

                }
                
                s_turnSubmenuCursor = subCursor;
                
                // Announce the current sub-menu item
                if (s_inSubmenu) {
                    if (s_submenuCommandId == 0x14 && s_magicListBuilt) {
                        // Magic sub-menu: read spell at cursor position
                        // Cursor 0-3 is visible position. With <=4 spells, maps directly.
                        // TODO: handle scroll offset for >4 spells (need to find page offset byte)
                        if ((int)subCursor < s_turnMagicCount) {
                            const char* spellName = GetMagicName(s_turnMagicList[subCursor].id);
                            int qty = (int)s_turnMagicList[subCursor].qty;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, %d", spellName, qty);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Write("BattleTTS: [SUBMENU-NAV] Magic cursor=%d -> %s x%d (id=0x%02X)",
                                       (int)subCursor, spellName, qty,
                                       (unsigned)s_turnMagicList[subCursor].id);
                        } else {
                            Log::Write("BattleTTS: [SUBMENU-NAV] Magic cursor=%d out of range (count=%d)",
                                       (int)subCursor, s_turnMagicCount);
                        }
                    } else if (s_submenuCommandId == 0x15 && s_gfListBuilt) {
                        // v0.10.98: GF sub-menu — announce junctioned GF at cursor position
                        if ((int)subCursor < s_turnGFCount) {
                            const char* gfName = s_turnGFList[subCursor].name;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s", gfName);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Write("BattleTTS: [SUBMENU-NAV] GF cursor=%d -> %s (gfIdx=%d)",
                                       (int)subCursor, gfName, (int)s_turnGFList[subCursor].gfIdx);
                        } else {
                            Log::Write("BattleTTS: [SUBMENU-NAV] GF cursor=%d out of range (count=%d)",
                                       (int)subCursor, s_turnGFCount);
                        }
                    } else if (s_submenuCommandId == 0x17) {
                        // v0.10.106: Item sub-menu — dual-source announce (cleaned up from v0.10.105 diagnostic).
                        // Display struct at 0x1D8DFF4 when populated (after Items > Battle), else inv[cursor].
                        int sc = (int)subCursor;
                        uint8_t annId = 0, annQty = 0;
                        
                        // Check if display struct is populated
                        bool dsActive = false;
                        __try {
                            uint8_t* ds = (uint8_t*)ITEM_DISPLAY_STRUCT;
                            for (int q = 0; q < 64; q++) {
                                if (ds[q] != 0) { dsActive = true; break; }
                            }
                            if (dsActive && sc < 32) {
                                annId = ds[sc * 2];
                                annQty = ds[sc * 2 + 1];
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        
                        if (!dsActive) {
                            // Fallback: direct inventory at cursor position
                            __try {
                                uint8_t* inv = (uint8_t*)ITEM_INVENTORY_ADDR;
                                if (sc < 198) { annId = inv[sc * 2]; annQty = inv[sc * 2 + 1]; }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        
                        int page = (sc / 4) + 1;
                        int itemNum = (sc % 4) + 1;
                        
                        if (annId >= 1 && annId < 33 && annQty > 0) {
                            const char* itemName = GetBattleItemName(annId);
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, quantity %d, page %d, item %d",
                                     itemName, (int)annQty, page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Write("BattleTTS: [ITEM] cursor=%d -> %s x%d page%d item%d",
                                       sc, itemName, (int)annQty, page, itemNum);
                        } else {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Empty, page %d, item %d", page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Write("BattleTTS: [ITEM] cursor=%d -> Empty page%d item%d", sc, page, itemNum);
                        }
                    } else if (s_submenuCommandId == 0x16 && s_drawListBuilt) {
                        // v0.10.112: Draw sub-menu — generic subCursor fires on phase transitions,
                        // NOT during active navigation. All draw spell announces go through the
                        // draw-specific cursor poll at 0x01D768D8 below. Log only here.
                        Log::Write("BattleTTS: [DRAW-NAV] generic subCursor=%d (ignored, handled by draw poll)",
                                   (int)subCursor);
                    } else {
                        // Other sub-menus — log for diagnostic
                        Log::Write("BattleTTS: [SUBMENU-NAV] cmd=0x%02X cursor=%d (unhandled)",
                                   (unsigned)s_submenuCommandId, (int)subCursor);
                    }
                }
            }
            
            // v0.10.112: Delayed submenu entry after command scroll.
            // 150ms after scrolling to a new command, force-enter the submenu and
            // announce the current item with interrupt=false (queued after command name).
            if (s_pendingSubmenuEntry && !s_inSubmenu && 
                GetTickCount() - s_pendingSubmenuTick > 150) {
                s_pendingSubmenuEntry = false;
                if (s_turnCmdCursor < 4) {
                    uint8_t sc = 0;
                    __try { sc = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    s_submenuCommandId = s_turnCharCommands[s_turnCmdCursor];
                    s_inSubmenu = true;
                    s_turnSubmenuCursor = sc;
                    
                    Log::Write("BattleTTS: [SUBMENU] Delayed entry for cmd 0x%02X (%s) cursor %d",
                               (unsigned)s_submenuCommandId,
                               GetCommandName(s_submenuCommandId), (int)sc);
                    
                    // Build lists
                    if (s_submenuCommandId == 0x14 && !s_magicListBuilt)
                        BuildMagicList(s_turnActiveCharId);
                    if (s_submenuCommandId == 0x15 && !s_gfListBuilt)
                        BuildGFList(s_turnActiveCharId);
                    if (s_submenuCommandId == 0x17 && !s_itemListBuilt)
                        BuildItemList();
                    if (s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                        s_lastDrawerPartySlot = s_turnActiveCharId;
                        BuildDrawList();
                    }
                    
                    // Announce current item (queued, not interrupting command name)
                    if (s_submenuCommandId == 0x14 && s_magicListBuilt) {
                        if ((int)sc < s_turnMagicCount) {
                            const char* spellName = GetMagicName(s_turnMagicList[sc].id);
                            int qty = (int)s_turnMagicList[sc].qty;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, %d", spellName, qty);
                            BattleSpeak(buf, PRIO_MENU, false);
                            Log::Write("BattleTTS: [SUBMENU-DELAYED] Magic cursor=%d -> %s x%d",
                                       (int)sc, spellName, qty);
                        }
                    } else if (s_submenuCommandId == 0x15 && s_gfListBuilt) {
                        if ((int)sc < s_turnGFCount) {
                            BattleSpeak(s_turnGFList[sc].name, PRIO_MENU, false);
                            Log::Write("BattleTTS: [SUBMENU-DELAYED] GF cursor=%d -> %s",
                                       (int)sc, s_turnGFList[sc].name);
                        }
                    } else if (s_submenuCommandId == 0x17 && s_itemListBuilt) {
                        // Item: read from display struct or inventory
                        uint8_t annId = 0, annQty = 0;
                        __try {
                            uint8_t* ds = (uint8_t*)0x1D8DFF4;
                            bool dsActive = false;
                            for (int q = 0; q < 64; q++) { if (ds[q] != 0) { dsActive = true; break; } }
                            if (dsActive && (int)sc < 32) { annId = ds[sc * 2]; annQty = ds[sc * 2 + 1]; }
                            if (!dsActive && (int)sc < 198) {
                                uint8_t* inv = (uint8_t*)0x1CFE79C;
                                annId = inv[sc * 2]; annQty = inv[sc * 2 + 1];
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        if (annId >= 1 && annId < 33 && annQty > 0) {
                            const char* itemName = GetBattleItemName(annId);
                            char buf[128];
                            int page = ((int)sc / 4) + 1;
                            int itemNum = ((int)sc % 4) + 1;
                            snprintf(buf, sizeof(buf), "%s, quantity %d, page %d, item %d",
                                     itemName, (int)annQty, page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, false);
                            Log::Write("BattleTTS: [SUBMENU-DELAYED] Item cursor=%d -> %s x%d",
                                       (int)sc, itemName, (int)annQty);
                        }
                    }
                    // Draw and Item are handled by their dedicated poll blocks below
                }
            }
            
            // v0.10.109: Draw-specific cursor poll.
            // Draw uses a SEPARATE cursor byte (0x01D768D8) from other sub-menus.
            // 0x01D768EC only fires during engine init/phase transitions, NOT during
            // active up/down navigation. We poll 0x01D768D8 independently here.
            // NOTE: Also retry BuildDrawList if it hasn't succeeded yet — the target
            // bitmask at 0x01D76884 may not be set until after target confirmation,
            // which happens AFTER the submenu entry event fires.
            if (s_inSubmenu && s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                BuildDrawList();  // retry until target bitmask is populated
            }
            if (s_inSubmenu && s_submenuCommandId == 0x16 && s_drawListBuilt) {
                // v0.10.112: Keep drawer slot updated every frame while draw submenu is open
                if (s_turnActiveCharId < 3)
                    s_lastDrawerPartySlot = s_turnActiveCharId;
                
                // v0.10.112: Detect menuPhase transitions to reset cursor tracking.
                // When canceling from Stock/Cast (phase 23) back to spell list (phase 14),
                // reset draw cursor prev so the current spell re-announces.
                // When canceling from spell list back to target select, reset everything.
                uint8_t drawPhaseNow = 0;
                __try { drawPhaseNow = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (s_drawLastMenuPhase != 0xFF && drawPhaseNow != s_drawLastMenuPhase) {
                    if (s_drawLastMenuPhase == 23 && drawPhaseNow < 23) {
                        // Left Stock/Cast prompt backward → back to spell list
                        s_drawCursorPrev = 0xFF;
                        s_drawStockCastPrev = 0xFF;
                        Log::Write("BattleTTS: [DRAW] Phase %u->%u: reset cursor tracking (back to spell list)",
                                   (unsigned)s_drawLastMenuPhase, (unsigned)drawPhaseNow);
                    }
                    if (s_drawLastMenuPhase == 14 && drawPhaseNow < 14) {
                        // Left spell list backward → back to target selection
                        s_drawCursorPrev = 0xFF;
                        s_drawStockCastPrev = 0xFF;
                        s_drawListBuilt = false;  // force rebuild with potentially new target
                        s_lastTargetBitmask = 0;  // force target re-announce
                        s_lastTargetScope = 0;
                        Log::Write("BattleTTS: [DRAW] Phase %u->%u: reset target+draw tracking (back to target select)",
                                   (unsigned)s_drawLastMenuPhase, (unsigned)drawPhaseNow);
                    }
                }
                s_drawLastMenuPhase = drawPhaseNow;
                
                uint8_t drawCur = 0xFF;
                __try { drawCur = *(uint8_t*)DRAW_CURSOR_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (drawCur != s_drawCursorPrev && drawCur < DRAW_SLOTS_PER_ENEMY) {
                    s_drawCursorPrev = drawCur;
                    uint8_t mid = s_turnDrawList[drawCur].magicId;
                    if (mid != 0) {
                        const char* spellName = GetMagicName(mid);
                        char buf[128];
                        snprintf(buf, sizeof(buf), "%s", spellName);
                        BattleSpeak(buf, PRIO_MENU, true);
                        Log::Write("BattleTTS: [DRAW-CUR] draw_cursor=%d -> %s (id=%u)",
                                   (int)drawCur, spellName, (unsigned)mid);
                    } else {
                        BattleSpeak("Empty", PRIO_MENU, true);
                        Log::Write("BattleTTS: [DRAW-CUR] draw_cursor=%d -> Empty", (int)drawCur);
                    }
                } else if (drawCur != s_drawCursorPrev && drawCur != 0xFF) {
                    s_drawCursorPrev = drawCur;  // out of range, track but don't announce
                }
                // v0.10.111: Stock/Cast cursor at 0x01D768D9 (0=Stock, 1=Cast)
                // v0.10.112: Only poll during Stock/Cast phase (menuPhase == 23).
                // menuPhase=14 is the spell list; Stock/Cast prompt is specifically at 23.
                // Without this guard, D9=0 (stale) triggers false "Stock" during spell list.
                uint8_t drawMenuPhase = 0;
                __try { drawMenuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (drawMenuPhase == 23) {
                    uint8_t stockCast = 0xFF;
                    __try { stockCast = *(uint8_t*)DRAW_STOCK_CAST_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (stockCast != s_drawStockCastPrev && stockCast <= 1) {
                        s_drawStockCastPrev = stockCast;
                        const char* actionName = (stockCast == 0) ? "Stock" : "Cast";
                        BattleSpeak(actionName, PRIO_MENU, true);
                        Log::Write("BattleTTS: [DRAW-ACTION] Stock/Cast cursor=%u -> %s (phase=%u)",
                                   (unsigned)stockCast, actionName, (unsigned)drawMenuPhase);
                    }
                }
            }
        }

    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// Battle entry/exit detection
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
        Log::Write("BattleTTS: [FFNx-GF] Deferred install result: %s", s_ffnxGFHookInstalled ? "OK" : "FAIL");
    }

    Log::Write("BattleTTS: === BATTLE ENTERED === (encounter ID: %u)",
               (unsigned)(*(uint16_t*)BATTLE_ENCOUNTER_ID));
}

static void OnBattleExit()
{
    Log::Write("BattleTTS: === BATTLE EXITED ===");
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
            Log::Write("BattleTTS: Entity array not populated after %ums", BATTLE_INIT_TIMEOUT_MS);
        } else {
            Log::Write("BattleTTS: Entity array ready after %ums (ally0 maxHP=%u)",
                       elapsed, (unsigned)allyMaxHP);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("BattleTTS: Exception polling entity array");
    }

    s_initAnnounceDone = true;

    // Log all slot data for diagnostics
    for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
        uint32_t hp = GetEntityHP(i);
        uint32_t maxHp = GetEntityMaxHP(i);
        uint8_t* blk = GetEntityBlock(i);
        uint8_t lvl = 0, sts = 0;
        if (blk) { __try { lvl = *(blk + BENT_LEVEL); sts = *(blk + BENT_PERSIST_STATUS); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
        Log::Write("BattleTTS: slot%d %s HP=%u/%u Lv=%u status=0x%02X",
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
    Log::Write("BattleTTS: %s", buf);
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
        Log::Write("BattleTTS: [GF-DISASM] === GF timer function code dump (PRE-HOOK) ===");
        Log::Write("BattleTTS: [GF-DISASM] 0x%08X through 0x%08X (%d bytes)",
                   funcStart, funcStart + dumpLen, dumpLen);
        __try {
            uint8_t* code = (uint8_t*)funcStart;
            for (int off = 0; off < dumpLen; off += 16) {
                char hex[80] = {};
                int p = 0;
                for (int b = 0; b < 16 && off + b < dumpLen; b++)
                    p += snprintf(hex + p, sizeof(hex) - p, "%02X ", code[off + b]);
                Log::Write("BattleTTS: [GF-DISASM] %08X: %s", funcStart + off, hex);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [GF-DISASM] EXCEPTION reading function code");
        }
        Log::Write("BattleTTS: [GF-DISASM] === End dump ===");
    }

    // v0.10.88: Dump code at 0x004B0400-0x004B0500 to find the GF timer check.
    // The state machine handlers are at 0x004B0440-04FF. The CALLER of the
    // state=5 handler at 0x004B04B4 contains the timer comparison.
    {
        Log::Write("BattleTTS: [GF-DISASM] === Code dump 0x004B0400-0x004B0500 ===");
        __try {
            for (uint32_t addr = 0x004B0400; addr < 0x004B0500; addr += 16) {
                uint8_t* p = (uint8_t*)addr;
                char hex[100] = {};
                int pos = 0;
                for (int i = 0; i < 16; i++)
                    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", p[i]);
                Log::Write("BattleTTS: [GF-DISASM] %08X: %s", addr, hex);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("BattleTTS: [GF-DISASM] EXCEPTION reading code");
        }
        Log::Write("BattleTTS: [GF-DISASM] === End dump ===");
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
                Log::Write("BattleTTS: [GF-PATCH] WARNING: byte at 0x%08X is 0x%02X, expected 0x%02X",
                           GF_FIRE_PATCH_ADDR, (unsigned)curByte, (unsigned)GF_FIRE_VALUE);
                s_gfFirePatchReady = false;  // don't patch unknown code
            } else {
                Log::Write("BattleTTS: [GF-PATCH] Code page writable, fire byte verified at 0x%08X",
                           GF_FIRE_PATCH_ADDR);
            }
        } else {
            Log::Write("BattleTTS: [GF-PATCH] VirtualProtect FAILED (err=%u)", GetLastError());
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
    Log::Write("BattleTTS: [GF-BP] VEH registered: handle=0x%08X", (uint32_t)(uintptr_t)s_gfVEHHandle);

    Log::Write("BattleTTS: Initialized v0.10.112 — Draw 3-bug fix (EWM=%s, ATB=%s, GF=%s, FFNx=%s, PATCH=%s).",
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
            Log::Write("BattleTTS: [second-pass] %s (enemies appeared after initial announce)", buf);
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
                    Log::Write("BattleTTS: Enemy slot %d \"%s\": HP %u/%u Lv=%u", i, name, hp, maxHp, (unsigned)lvl);
                }
            }
        } else if (GetTickCount() - s_battleEntryTime > BATTLE_INIT_TIMEOUT_MS) {
            s_enemyAnnounceDone = true;  // give up on enemy detection
            Log::Write("BattleTTS: No enemies detected after %ums timeout", BATTLE_INIT_TIMEOUT_MS);
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
            Log::Write("BattleTTS: [REPEAT] '%s'", s_repeatBuffer);
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
    Log::Write("BattleTTS: Shutdown.");
}

}  // namespace BattleTTS
