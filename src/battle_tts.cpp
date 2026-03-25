// battle_tts.cpp - Battle sequence TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.10.14 — Fix command lookup: savemap uses GF ability IDs
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
// Scan 1024 bytes starting at 0x01D76800 — covers active_char_id (0x01D76844)
// and extends toward pMenuStateA (0x01D76A9A). Battle menu cursor is likely here.
static const uint32_t MENU_SCAN_BASE = 0x01D76800;
static const int MENU_SNAP_SIZE = 1024;
static uint8_t s_menuSnap[MENU_SNAP_SIZE] = {};
static bool s_menuSnapValid = false;
static DWORD s_lastMenuDiagTick = 0;
static const DWORD MENU_DIAG_INTERVAL_MS = 100; // poll every 100ms
static uint8_t s_lastActiveCharId = 0xFF;
static uint8_t s_lastNewActiveCharId = 0xFF;

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
    
    Log::Write("BattleTTS: [MENU-DIAG] Wide scan base: 0x%08X (%d bytes)", MENU_SCAN_BASE, MENU_SNAP_SIZE);
}

// Poll active char IDs + wide memory region for changes.
// Logs only bytes that changed since last snapshot.
static void PollMenuDiagnostic()
{
    DWORD now = GetTickCount();
    if (now - s_lastMenuDiagTick < MENU_DIAG_INTERVAL_MS) return;
    s_lastMenuDiagTick = now;
    
    // Track active char ID changes (always, independent of scan)
    if (s_pActiveCharId) {
        __try {
            uint8_t cur = *s_pActiveCharId;
            if (cur != s_lastActiveCharId) {
                Log::Write("BattleTTS: [MENU-DIAG] active_char_id: %u -> %u",
                           (unsigned)s_lastActiveCharId, (unsigned)cur);
                s_lastActiveCharId = cur;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (s_pNewActiveCharId) {
        __try {
            uint8_t cur = *s_pNewActiveCharId;
            if (cur != s_lastNewActiveCharId) {
                Log::Write("BattleTTS: [MENU-DIAG] new_active_char_id: %u -> %u",
                           (unsigned)s_lastNewActiveCharId, (unsigned)cur);
                s_lastNewActiveCharId = cur;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    
    // Snapshot wide region at fixed base
    uint8_t* base = (uint8_t*)MENU_SCAN_BASE;
    uint8_t newSnap[MENU_SNAP_SIZE];
    __try {
        memcpy(newSnap, base, MENU_SNAP_SIZE);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    
    if (!s_menuSnapValid) {
        // First snapshot — just store it
        memcpy(s_menuSnap, newSnap, MENU_SNAP_SIZE);
        s_menuSnapValid = true;
        Log::Write("BattleTTS: [MENU-DIAG] First snapshot taken (0x%08X, %d bytes)",
                   MENU_SCAN_BASE, MENU_SNAP_SIZE);
        return;
    }
    
    // Compare and log changes (only log up to 20 changes per poll to avoid flood)
    int changeCount = 0;
    for (int i = 0; i < MENU_SNAP_SIZE && changeCount < 20; i++) {
        if (newSnap[i] != s_menuSnap[i]) {
            Log::Write("BattleTTS: [MENU-DIAG] +0x%03X (abs 0x%08X): %u -> %u",
                       i, MENU_SCAN_BASE + i,
                       (unsigned)s_menuSnap[i], (unsigned)newSnap[i]);
            changeCount++;
        }
    }
    if (changeCount >= 20) {
        // Count remaining changes
        int extra = 0;
        for (int i = 0; i < MENU_SNAP_SIZE; i++) {
            if (newSnap[i] != s_menuSnap[i]) extra++;
        }
        Log::Write("BattleTTS: [MENU-DIAG] ... %d total changes (truncated)", extra);
    }
    memcpy(s_menuSnap, newSnap, MENU_SNAP_SIZE);
}

// ============================================================================
// Turn announcement + Command menu TTS (v0.10.11)
// ============================================================================

// Confirmed addresses from v0.10.10 diagnostic
static const uint32_t BATTLE_CMD_CURSOR  = 0x01D76843; // BYTE, 0-3 (command slot index)
static const uint32_t BATTLE_MENU_PHASE  = 0x01D768D0; // BYTE (1=open, 3=executing, 4=idle)

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
            
            // Announce "[Name]'s turn. [First command]."
            const char* name = GetBattleCharName(activeChar);
            const char* cmd = GetCommandName(s_turnCharCommands[0]);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s's turn. %s.", name, cmd);
            BattleSpeak(buf, PRIO_TURN, true);
            Log::Write("BattleTTS: [TURN] %s (slot %d)", buf, (int)activeChar);
            
            // Set cursor to 0 so we don't re-announce the initial command
            s_turnCmdCursor = 0;
        }
        else if (activeChar == 0xFF && s_turnActiveCharId != 0xFF) {
            // Turn ended
            s_turnActiveCharId = 0xFF;
            s_turnCmdCursor = 0xFF;
        }
        
        // Command cursor navigation (only while a turn is active)
        if (s_turnActiveCharId < 3) {
            uint8_t cursor = *(uint8_t*)BATTLE_CMD_CURSOR;
            if (cursor < 4 && cursor != s_turnCmdCursor) {
                s_turnCmdCursor = cursor;
                const char* cmd = GetCommandName(s_turnCharCommands[cursor]);
                BattleSpeak(cmd, PRIO_MENU, true);
                Log::Write("BattleTTS: [CMD-NAV] cursor=%d -> %s", (int)cursor, cmd);
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
    s_lastMenuDiagTick = 0;
    s_lastActiveCharId = 0xFF;
    s_lastNewActiveCharId = 0xFF;
    memset(s_menuSnap, 0, sizeof(s_menuSnap));
    
    // Reset turn/command tracking
    s_turnActiveCharId = 0xFF;
    s_turnCmdCursor = 0xFF;
    memset(s_turnCharCommands, 0, sizeof(s_turnCharCommands));
    
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
    Log::Write("BattleTTS: Initialized v0.10.14 — module skeleton + battle entry/exit detection.");
}

void Update()
{
    if (!s_initialized) return;
    if (!FF8Addresses::pGameMode) return;

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
    // TODO v0.10.19+: Target selection
    // TODO v0.10.25+: HP/status tracking
}

void Shutdown()
{
    if (!s_initialized) return;
    s_initialized = false;
    s_inBattle = false;
    Log::Write("BattleTTS: Shutdown.");
}

}  // namespace BattleTTS
