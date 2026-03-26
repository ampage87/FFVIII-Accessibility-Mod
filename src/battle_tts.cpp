// battle_tts.cpp - Battle sequence TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.10.27 — EWM blacklist executing phases
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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "battle_tts.h"

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
// v0.10.25: Enhanced Wait Mode (EWM)
// ============================================================================
// Freezes ALL ATBs as soon as the command menu appears (active_char_id 0-2),
// not just when sub-menus open like standard Wait Mode.
// ATB stays frozen until the character's turn ends (active_char_id returns to 255).
//
// Implementation: snapshot all entity ATB values when turn starts, write them
// back every frame while turn is active. No engine flags needed.
//
// Toggle: "O" key (works in all game modes, not just battle).
// Persistence: ewm_config.txt in mod root ("1"=on, "0"=off). Default: on.

static bool s_ewmEnabled = true;          // Enhanced Wait Mode toggle
static bool s_ewmFreezing = false;        // currently writing back ATB values
static bool s_ewmConfigLoaded = false;    // config file has been read
static bool s_ewmOKeyWasDown = false;     // edge detection for O key

// ATB snapshot: 4 bytes per slot (covers both uint16 ally and uint32 enemy ATB)
static uint32_t s_ewmAtbSnapshot[BATTLE_TOTAL_SLOTS] = {};
static bool s_ewmSnapshotValid = false;

// Config file path (built at init time)
static char s_ewmConfigPath[512] = {};

static void EWM_BuildConfigPath()
{
    // Place ewm_config.txt in the same directory as the DLL (mod root)
    char dllPath[512];
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)EWM_BuildConfigPath, &hMod);
    GetModuleFileNameA(hMod, dllPath, sizeof(dllPath));
    // Strip filename, keep directory
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
        if (buf[0] == '0') {
            s_ewmEnabled = false;
        } else {
            s_ewmEnabled = true;  // default to on for any non-zero or missing value
        }
        Log::Write("BattleTTS: [EWM] Config loaded: %s (enabled=%d)", s_ewmConfigPath, (int)s_ewmEnabled);
    } else {
        // No config file — default to enabled, create it
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
    if (f) {
        fputs(s_ewmEnabled ? "1" : "0", f);
        fclose(f);
    }
}

// Called every frame from Update() — works in ALL game modes
static void EWM_PollToggle()
{
    bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
    bool oPressed = oDown && !s_ewmOKeyWasDown;
    s_ewmOKeyWasDown = oDown;
    
    if (!oPressed) return;
    
    s_ewmEnabled = !s_ewmEnabled;
    EWM_SaveConfig();
    
    const char* msg = s_ewmEnabled ? "Enhanced Wait Mode on" : "Enhanced Wait Mode off";
    ScreenReader::Speak(msg, true);
    Log::Write("BattleTTS: [EWM] Toggled: %s", msg);
}

// Snapshot all entity ATB current values
static void EWM_SnapshotATB()
{
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint8_t* blk = GetEntityBlock(slot);
        if (!blk) { s_ewmAtbSnapshot[slot] = 0; continue; }
        __try {
            if (slot < BATTLE_ALLY_SLOTS) {
                s_ewmAtbSnapshot[slot] = (uint32_t)(*(uint16_t*)(blk + 0x0C));
            } else {
                s_ewmAtbSnapshot[slot] = *(uint32_t*)(blk + 0x0C);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            s_ewmAtbSnapshot[slot] = 0;
        }
    }
    s_ewmSnapshotValid = true;
}

// Write back snapshot ATB values to all entities (prevents ATB advancement)
static void EWM_FreezeATB()
{
    if (!s_ewmSnapshotValid) return;
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint8_t* blk = GetEntityBlock(slot);
        if (!blk) continue;
        __try {
            if (slot < BATTLE_ALLY_SLOTS) {
                *(uint16_t*)(blk + 0x0C) = (uint16_t)s_ewmAtbSnapshot[slot];
            } else {
                *(uint32_t*)(blk + 0x0C) = s_ewmAtbSnapshot[slot];
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Called every frame during battle from Update()
// Freeze ATB while the player is actively deciding (command menu, sub-menu, target select).
// Release when the action starts executing (phase >= 14) or when active_char_id goes to 255.
//
// Menu phase lifecycle for a typical command:
//   0 or 32 = command menu / sub-menu open (DECIDING)
//   64 = Limit Break showing (DECIDING)
//   3 = brief transition after command select (DECIDING — loading target UI)
//   11 = target selection (DECIDING)
//   14 = target confirmed, action committed (EXECUTING — release here)
//   21, 23, 33, 34 = animation/cleanup (EXECUTING)
//   1, 4 = turn setup transitions (still DECIDING — keep freeze)

static bool EWM_IsExecutingPhase(uint8_t phase)
{
    // Phases where the player's action has been committed and is executing/animating.
    // ATB should resume during these.
    // All other phases (0, 1, 3, 4, 11, 32, 64, etc.) occur during turn setup,
    // command menu, sub-menu, or target selection — ATB should stay frozen.
    return (phase == 14 || phase == 21 || phase == 23 || phase == 33 || phase == 34);
}

static void EWM_UpdateBattle()
{
    if (!s_ewmEnabled) {
        if (s_ewmFreezing) {
            s_ewmFreezing = false;
            s_ewmSnapshotValid = false;
            Log::Write("BattleTTS: [EWM] Freeze released (EWM disabled)");
        }
        return;
    }
    
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    if (activeChar < 3) {
        // A player turn is active — check if we should be freezing
        uint8_t menuPhase = 0;
        __try { menuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        if (!EWM_IsExecutingPhase(menuPhase)) {
            // Player is deciding (any non-execution phase) — freeze ATB
            if (!s_ewmFreezing) {
                EWM_SnapshotATB();
                s_ewmFreezing = true;
                Log::Write("BattleTTS: [EWM] ATB frozen (deciding, char=%d, phase=%u)",
                           (int)activeChar, (unsigned)menuPhase);
            }
            EWM_FreezeATB();
        } else {
            // Action is executing (phase 14, 21, 23, 33, 34) — release freeze
            if (s_ewmFreezing) {
                s_ewmFreezing = false;
                s_ewmSnapshotValid = false;
                Log::Write("BattleTTS: [EWM] ATB released (executing, phase=%u)",
                           (unsigned)menuPhase);
            }
        }
    } else {
        // No turn active — release freeze
        if (s_ewmFreezing) {
            s_ewmFreezing = false;
            s_ewmSnapshotValid = false;
            Log::Write("BattleTTS: [EWM] ATB released (no turn active)");
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
            s_submenuDebouncing = true;
            s_submenuDebounceTick = GetTickCount();
            
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
            uint8_t cursor = *(uint8_t*)BATTLE_CMD_CURSOR;
            if (cursor < 4 && cursor != s_turnCmdCursor) {
                s_turnCmdCursor = cursor;
                // Returning to command menu from sub-menu
                if (s_inSubmenu) {
                    s_inSubmenu = false;
                    s_turnSubmenuCursor = 0xFF;
                    Log::Write("BattleTTS: [SUBMENU] Exited sub-menu, back to command menu");
                }
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
            if (!s_submenuDebouncing && subCursor != s_turnSubmenuCursor) {
                if (!s_inSubmenu && s_turnCmdCursor < 4) {
                    // Entering sub-menu — record which command opened it
                    s_submenuCommandId = s_turnCharCommands[s_turnCmdCursor];
                    s_inSubmenu = true;
                    Log::Write("BattleTTS: [SUBMENU] Entered sub-menu for cmd 0x%02X (%s) at cursor %d",
                               (unsigned)s_submenuCommandId,
                               GetCommandName(s_submenuCommandId),
                               (int)s_turnCmdCursor);
                    
                    // Build spell list if Magic sub-menu
                    if (s_submenuCommandId == 0x14 && !s_magicListBuilt) { // 0x14 = Magic ability ID
                        BuildMagicList(s_turnActiveCharId);
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
                    } else {
                        // Other sub-menus (GF, Draw, Item) — log for now, implement later
                        Log::Write("BattleTTS: [SUBMENU-NAV] cmd=0x%02X cursor=%d (not yet implemented)",
                                   (unsigned)s_submenuCommandId, (int)subCursor);
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
    s_submenuDebouncing = false;
    s_submenuDebounceTick = 0;
    
    // Reset Limit Break state
    s_limitBreakActive = false;
    s_lastLimitToggle = 0;
    
    // Reset EWM freeze state for new battle
    s_ewmFreezing = false;
    s_ewmSnapshotValid = false;
    EWM_LoadConfig();  // ensure config is loaded on first battle
    
    // Resolve battle menu addresses on first battle entry
    if (!s_pBattleMenuState) {
        ResolveBattleMenuAddresses();
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
    }

    BattleSpeak(buf, PRIO_TURN, true);
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
    Log::Write("BattleTTS: Initialized v0.10.27 — Enhanced Wait Mode (EWM=%s).",
               s_ewmEnabled ? "ON" : "OFF");
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
            char enemyStr[200];
            BuildEnemyNameString(enemyStr, sizeof(enemyStr));
            char buf[256];
            snprintf(buf, sizeof(buf), "%s.", enemyStr);
            BattleSpeak(buf, PRIO_TURN, false);
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

    // v0.10.25: Enhanced Wait Mode — freeze ATB while player is deciding
    if (s_inBattle && s_initAnnounceDone) {
        EWM_UpdateBattle();
    }
    // TODO: Target selection, HP/status tracking
}

void Shutdown()
{
    if (!s_initialized) return;
    s_initialized = false;
    s_inBattle = false;
    Log::Write("BattleTTS: Shutdown.");
}

}  // namespace BattleTTS
