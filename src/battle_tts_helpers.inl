// battle_tts_helpers.inl — Enemy names, text decoder, entity reading helpers
// Included from battle_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

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
// FF8 text decoder (v0.10.08 → v0.15.10.0)
// ============================================================================
// v0.15.10.0: The v0.10.08 standalone decoder (DecodeFF8Char + DecodeFF8String)
// has been retired. Its character table was an "estimated, not yet confirmed"
// approximation with three known errors that surfaced across BAT iterations:
//   - Digit range 0x24-0x2D was off-by-0x03 (correct is 0x21-0x2A per the
//     canonical Ifrit textformat.ifr table). Caused "X-ATM092" to be spoken
//     as "X-ATM?6?" through every battle TTS path (NAME-CACHE, TARGET announce,
//     BuildEnemyNameString, GF name lookup).
//   - 0x06 was mapped to apostrophe; Ifrit says it's a color code that consumes
//     the next byte.
//   - 0x2F was mapped to '-'; Ifrit says it's '?'.
//   - Zero coverage of the 0xE8-0xFF two-character compression sequences
//     ("in", "to", "HP", "GF", "ar", etc.) that FF8 packs into kernel.bin to
//     save space; any monster name using a compressed pair was silently
//     mangled.
//
// DecodeFF8String now forwards to the canonical FF8TextDecode::Decode in
// ff8_text_decode.cpp (Ifrit-based; battle-tested via scan_tts.cpp on the same
// engine name accessor sub_495100 since v0.14.50). The signature is preserved
// so all five battle-module call sites (this file, hp.inl, menu.inl,
// victory.inl) keep working unchanged; only the implementation moved.
//
// MSVC /EHsc forbids __try in a function holding non-trivial destructors
// (C2712). std::string inside FF8TextDecode::Decode triggers that, so the
// work is split across DecodeFF8StringInner (std::string, no SEH) and the
// public DecodeFF8String (SEH wrapper). Pattern lifted from
// scan_tts.cpp::DecodeNameSafe / DecodeNameToBuf.
//
// Note: the third local decoder in this codebase — DecodeFF8TextPreview in
// battle_tts_victory.inl — has the same digit-range bug. It's left in place
// for this build because its call sites are mostly diagnostic logging plus
// item-name announcements (rarely contain digits). Unification is tracked
// as a follow-up item in the backlog.

static int DecodeFF8StringInner(const uint8_t* raw,
                                 char* outBuf,
                                 int outBufSize)
{
    if (!outBuf || outBufSize <= 0) return 0;
    outBuf[0] = '\0';
    if (!raw) return 0;
    std::string decoded = FF8TextDecode::Decode(raw, 64);
    if (decoded.empty()) return 0;
    size_t n = decoded.size();
    if ((int)n >= outBufSize) n = (size_t)(outBufSize - 1);
    memcpy(outBuf, decoded.data(), n);
    outBuf[n] = '\0';
    return (int)n;
}

// Decode an FF8-encoded byte string into a regular C string.
// Returns the number of characters decoded (not counting null terminator).
// SEH-guarded so a faulting read on "raw" cannot crash the caller.
static int DecodeFF8String(const uint8_t* src, char* dst, int maxLen)
{
    int n = 0;
    __try {
        n = DecodeFF8StringInner(src, dst, maxLen);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (dst && maxLen > 0) dst[0] = '\0';
        n = 0;
    }
    return n;
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
    Log::Battle("BattleTTS: [NAME-DIAG] === Enemy name diagnostic ===");
    
    // --- Approach 1: Scan computed stats struct (0x1CFF000 + slot*0x1D0) ---
    // Look for printable ASCII strings in each enemy slot's 0x1D0 block
    Log::Battle("BattleTTS: [NAME-DIAG] --- Approach 1: Computed stats scan (0x1CFF000) ---");
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
                        Log::Battle("BattleTTS: [NAME-DIAG] slot%d comp+0x%03X: \"%s\" (len=%d)",
                                   slot, off, name, len);
                    }
                    off += len;  // skip past this string
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [NAME-DIAG] slot%d comp stats EXCEPTION", slot);
        }
    }
    
    // --- Approach 2: Check specific known offsets in computed stats ---
    // The HP values are at +0x172/+0x174 per deep research. Names might be nearby.
    Log::Battle("BattleTTS: [NAME-DIAG] --- Approach 2: Comp stats HP check ---");
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
            Log::Battle("BattleTTS: [NAME-DIAG] enemy slot%d comp[0x00]: %s", slot, hex1);
            Log::Battle("BattleTTS: [NAME-DIAG] enemy slot%d comp[0x168]: %s hp172=%u hp174=%u",
                       slot, hex2, (unsigned)hp172, (unsigned)hp174);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [NAME-DIAG] slot%d comp EXCEPTION", slot);
        }
    }
    
    // --- Approach 3: Try calling battle_get_monster_name via FFNx JMP ---
    Log::Battle("BattleTTS: [NAME-DIAG] --- Approach 3: Call 0x495100 (FFNx hooked) ---");
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
                        Log::Battle("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X -> \"%s\"",
                                   slot, (uint32_t)(uintptr_t)name, safe);
                    } else {
                        Log::Battle("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X (non-ASCII first byte 0x%02X)",
                                   slot, (uint32_t)(uintptr_t)name, (unsigned)(uint8_t)name[0]);
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log::Battle("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X (access fault)",
                               slot, (uint32_t)(uintptr_t)name);
                }
            } else {
                Log::Battle("BattleTTS: [NAME-DIAG] getMonsterName(%d) = 0x%08X (bad ptr)",
                           slot, name ? (uint32_t)(uintptr_t)name : 0);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [NAME-DIAG] getMonsterName(%d) EXCEPTION calling function", slot);
        }
    }
    
    // --- Approach 4: Try calling battle_get_actor_name ---
    Log::Battle("BattleTTS: [NAME-DIAG] --- Approach 4: Call 0x47EAF0 (actor name) ---");
    // Check if this function is hooked (starts with E9 JMP)
    __try {
        uint8_t firstByte = *(uint8_t*)BATTLE_GET_ACTOR_NAME_ADDR;
        Log::Battle("BattleTTS: [NAME-DIAG] 0x47EAF0 first byte: 0x%02X (%s)",
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
                        Log::Battle("BattleTTS: [NAME-DIAG] getActorName(%d) = 0x%08X -> \"%s\"",
                                   slot, (uint32_t)(uintptr_t)name, safe);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        Log::Battle("BattleTTS: [NAME-DIAG] getActorName(%d) = 0x%08X (access fault)",
                                   slot, (uint32_t)(uintptr_t)name);
                    }
                } else {
                    Log::Battle("BattleTTS: [NAME-DIAG] getActorName(%d) = 0x%08X (bad/null ptr)",
                               slot, name ? (uint32_t)(uintptr_t)name : 0);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log::Battle("BattleTTS: [NAME-DIAG] getActorName(%d) EXCEPTION calling function", slot);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [NAME-DIAG] 0x47EAF0 EXCEPTION reading function");
    }
    
    // --- Approach 5: Scan entity struct 0xD0 block for name-like data ---
    Log::Battle("BattleTTS: [NAME-DIAG] --- Approach 5: Entity struct 0xD0 block scan ---");
    for (int slot = BATTLE_ALLY_SLOTS; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (GetEntityHP(slot) == 0) continue;
        uint8_t* blk = GetEntityBlock(slot);
        __try {
            // Dump bytes 0x00-0x07 (pre-HP area), 0x78-0x80 (status area), 0xA0-0xD0 (level/stats area)
            char hex[200];
            int p = 0;
            for (int b = 0xA0; b < (int)BATTLE_ENTITY_STRIDE; b++)
                p += snprintf(hex + p, sizeof(hex) - p, "%02X ", blk[b]);
            Log::Battle("BattleTTS: [NAME-DIAG] slot%d entity[0xA0..0xCF]: %s", slot, hex);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [NAME-DIAG] slot%d entity scan EXCEPTION", slot);
        }
    }
    
    // --- Approach 6: Check memory near entity array for name table ---
    // The entity array is at 0x1D27B18, maybe names are stored nearby
    Log::Battle("BattleTTS: [NAME-DIAG] --- Approach 6: Name table scan near entity array ---");
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
                    Log::Battle("BattleTTS: [NAME-DIAG] @0x%08X: \"%s\" (len=%d)",
                               addr, name, len);
                }
                addr += len;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [NAME-DIAG] post-entity scan EXCEPTION");
    }
    
    Log::Battle("BattleTTS: [NAME-DIAG] === End diagnostic ===");
}

// ============================================================================
