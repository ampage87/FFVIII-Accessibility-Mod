// battle_tts_victory.inl — Victory TTS: hooks, phase detection, GF/ability tables, thread
//
// Included by battle_tts.cpp AFTER battle_tts_screenshot.inl.
// Contains all 8 battle text hooks, victory phase state machine, GF fallback
// functions, ability name tables, and the victory screen background thread.
//
// Extracted from battle_tts.cpp v0.13.44 (session 63, purely mechanical split).

// ============================================================================
// Victory TTS state
// ============================================================================

// v0.13.24: Victory TTS announce state
static bool s_victoryTTSAnnounceDone = false;
static DWORD s_victoryTTSEntryTime = 0;
static const DWORD VICTORY_TTS_DELAY_MS = 3000;  // wait for data to populate

// v0.13.26: Victory phase state machine + EXP text capture (set by BTXT hook, read by victory thread)
enum VictoryPhase {
    VP_NONE = 0,
    VP_EXP,           // EXP screen active (textIDs 22/23 appeared)
    VP_ITEMS,         // Items screen (textID 21)
    VP_GF_AP,         // GF AP screen (textID 109)
    VP_GF_LEVELUP,    // GF level up (textID 121 "GF ")
    VP_ABILITY,       // Ability learned (textID 127)
};
static volatile int s_victoryPhase = VP_NONE;
static volatile int s_victoryPhasePrev = VP_NONE;

// EXP Phase 1/2 state + captured text
static bool s_expPhase1Announced = false;
static bool s_expPhase2Announced = false;
static DWORD s_expPhase1Time = 0;          // tick when Phase 1 announced
static DWORD s_expPollLastLog = 0;            // diagnostic: throttle EXP poll logging

// Captured EXP text for announcements
static char s_capturedExpText[3][128] = {};   // "Next LEVEL" text for each character
static bool s_expTextCaptured[3] = {};        // which characters have captured text

// Items phase state
static bool s_itemsPhaseAnnounced = false;
static bool s_victoryNoItems = false;  // v0.13.30: textID=28 "Couldn't find items" vs textID=21

// GF phase state
static bool s_gfAPAnnounced = false;

// v0.13.30: ABILITY + GF_LEVELUP phase state and entity name capture
static bool s_abilityAnnounced   = false;
static bool s_gfLevelupAnnounced = false;
static volatile bool s_entityNameCaptureActive = false;  // true = currently capturing names
static int           s_entityNameCaptureCount  = 0;      // how many names captured (0-2)
static char          s_entityNameCaptures[2][64] = {};   // [0]=GF name, [1]=ability name
static DWORD         s_entityPhaseDetectedTime = 0;      // GetTickCount when capture phase started

// v0.12.88: Background thread for victory screen monitoring
static HANDLE s_victoryThread = NULL;
static volatile bool s_victoryThreadStop = false;
// s_gdiplusToken declared in battle_tts.cpp (shared section)

// v0.13.14: Battle text candidate hooks for victory screen TTS
static bool s_battleTextHooksInstalled = false;
static volatile bool s_victoryTTSActive = false;

// v0.13.14: Hook type and counters
typedef uint32_t (__cdecl *BtTextFunc_t)(uint32_t, uint32_t, uint32_t, uint32_t,
                                         uint32_t, uint32_t, uint32_t, uint32_t);

static volatile long s_btCount1 = 0;  // sub_47EC70 (261 xrefs, battle text retrieval by ID)
static volatile long s_btCount2 = 0;  // sub_4B7210 (GPU glyph draw)
static volatile long s_btCount3 = 0;  // sub_4A3EE0 (victory per-frame loop)
static volatile long s_btCount4 = 0;  // sub_5348E0 (ctrl code expansion)
static volatile long s_btCount5 = 0;  // sub_47EA30 (entity name retrieval)
static volatile long s_btCount6 = 0;  // sub_47EA90 (sibling name retrieval)

static BtTextFunc_t s_origBt1 = nullptr;
static BtTextFunc_t s_origBt2 = nullptr;
static BtTextFunc_t s_origBt3 = nullptr;
static BtTextFunc_t s_origBt4 = nullptr;
static BtTextFunc_t s_origBt5 = nullptr;
static BtTextFunc_t s_origBt6 = nullptr;

static const uint32_t BT_ADDR1 = 0x0047EC70;  // 261 xrefs — get_battle_text(text_id) -> FF8 text ptr
static const uint32_t BT_ADDR2 = 0x004B7210;  // GPU glyph quad draw
static const uint32_t BT_ADDR3 = 0x004A3EE0;  // victory per-frame loop
static const uint32_t BT_ADDR4 = 0x005348E0;  // v0.13.25: control code 0x0A variable expansion
static const uint32_t BT_ADDR5 = 0x0047EA30;  // v0.13.30: entity name retrieval
static const uint32_t BT_ADDR6 = 0x0047EA90;  // v0.13.31: sibling name retrieval

// v0.13.40: Victory rendering deep hooks (from disassembly analysis session 61)
static const uint32_t BT_ADDR7 = 0x0047E970;  // GF name retrieval
static const uint32_t BT_ADDR8 = 0x0047E710;  // Ability name retrieval
static BtTextFunc_t  s_origBt7  = nullptr;
static BtTextFunc_t  s_origBt8  = nullptr;

// Captured GF/ability names from the rendering pipeline (set by hooks, read by victory thread)
static char s_renderedGFName[64] = {};       // from sub_47E970 during mode 4
static char s_renderedAbilityName[64] = {};  // from sub_47E710 during mode 4
static volatile bool s_gfNameCaptured = false;
static volatile bool s_abilityNameCaptured = false;
static char s_lastAnnouncedVictoryGFName[64] = {};  // tracks last GF name to prevent re-announce

// ============================================================================
// FF8 battle text decoder (standard FF8 menu encoding)
// ============================================================================

static void DecodeFF8TextPreview(const uint8_t* src, char* dst, int maxOut)
{
    int out = 0;
    for (int i = 0; i < 120 && out < maxOut - 1; i++) {
        uint8_t b = src[i];
        if (b == 0x00) break;
        // Standard FF8 menu encoding
        if (b == 0x20)                   dst[out++] = ' ';
        else if (b >= 0x45 && b <= 0x5E) dst[out++] = 'A' + (b - 0x45);  // A-Z
        else if (b >= 0x5F && b <= 0x78) dst[out++] = 'a' + (b - 0x5F);  // a-z
        else if (b >= 0x24 && b <= 0x2D) dst[out++] = '0' + (b - 0x24);  // 0-9
        else if (b == 0x02)              dst[out++] = '\n';              // newline
        else if (b == 0x06)              dst[out++] = '\'';
        else if (b == 0x2F)              dst[out++] = '-';
        else if (b == 0x32)              dst[out++] = '-';  // v0.13.34: confirmed hyphen
        else if (b == 0x43)              dst[out++] = ' ';  // v0.13.34: space variant
        else if (b == 0x2E)              dst[out++] = '.';
        else if (b == 0x0A) {
            // Control code: 0x0A = variable expansion (sub_5348E0)
            if (out < maxOut - 6) {
                uint8_t varId = src[i + 1];
                out += snprintf(dst + out, maxOut - out, "{var%02X}", varId);
                i++;  // skip the variable ID byte
            }
        }
        else if (b == 0x03) { i++; }     // color/special code — skip next byte
        else if (b == 0x01)              dst[out++] = '|';               // separator
        else if (b == 0x0E) {
            // v0.13.35: Icon code — next byte is icon ID
            if (i + 1 < 120) {
                uint8_t iconId = src[++i];
                const char* iconName = nullptr;
                switch (iconId) {
                    case 0x36: iconName = "Fire"; break;
                    case 0x37: iconName = "Magic"; break;
                }
                if (iconName) {
                    for (const char* p = iconName; *p && out < maxOut - 1; p++)
                        dst[out++] = *p;
                }
            }
        }
        else if (b >= 0x79) {
            // v0.13.35: Compressed token lookup
            static const struct { uint8_t tok; const char* s; } TOKENS[] = {
                { 0xE9, "e " }, { 0xEA, "ne" }, { 0xEB, "to" }, { 0xF6, " w" },
                { 0xF9, "i"  }, { 0xFB, "f"  },
            };
            bool found = false;
            for (int t = 0; t < 6; t++) {
                if (TOKENS[t].tok == b) {
                    for (const char* p = TOKENS[t].s; *p && out < maxOut - 1; p++)
                        dst[out++] = *p;
                    found = true;
                    break;
                }
            }
            if (!found) {
                dst[out++] = '{';
                int n = snprintf(dst + out, maxOut - out, "%02X", b);
                out += n;
                if (out < maxOut - 1) dst[out++] = '}';
            }
        }
        else { dst[out++] = '{'; int n = snprintf(dst+out, maxOut-out, "%02X", b); out += n; if (out < maxOut - 1) dst[out++] = '}'; }
    }
    dst[out] = '\0';
}

// ============================================================================
// Battle text hook functions (BT1-BT8)
// ============================================================================

// v0.13.22: Hook for sub_47EC70 — battle text retrieval by ID
static int s_bt1LogCount = 0;

static const int BT1_MAX_TRACKED_IDS = 40;
static struct {
    int textId;
    char lastDecoded[80];
    int callCount;
    bool seen;
} s_bt1Tracked[BT1_MAX_TRACKED_IDS] = {};
static int s_bt1TrackedCount = 0;

static uint32_t __cdecl HookedBtCandidate1(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    InterlockedIncrement(&s_btCount1);
    
    // Call original first to get the returned text pointer
    uint32_t result = s_origBt1(a1, a2, a3, a4, a5, a6, a7, a8);
    
    // Smart logging during victory (mode 4/5/100)
    {
        uint16_t mode = 0;
        if (FF8Addresses::pGameMode) {
            __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (mode == 4 || mode == 5 || mode == 100) {
            // v0.13.39: FIRST textID=121 per phase transition only.
            if ((int)a1 == 121 && mode == 4) {
                if (s_victoryPhase < VP_GF_LEVELUP) {
                    s_victoryPhase = VP_GF_LEVELUP;
                    Log::Battle("BattleTTS: [VPHASE] -> GF_LEVELUP (textID=121)");
                    s_entityPhaseDetectedTime = GetTickCount();
                }
            }
            
            // Decode the returned FF8 text
            char decoded[80] = {};
            if (result >= 0x00400000 && result < 0x02800000) {
                __try {
                    DecodeFF8TextPreview((const uint8_t*)result, decoded, sizeof(decoded));
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    snprintf(decoded, sizeof(decoded), "<decode failed>");
                }
            }
            
            // Find or create tracking entry for this text ID
            int tIdx = -1;
            for (int i = 0; i < s_bt1TrackedCount; i++) {
                if (s_bt1Tracked[i].textId == (int)a1) { tIdx = i; break; }
            }
            
            bool isNew = false;
            bool contentChanged = false;
            
            if (tIdx < 0 && s_bt1TrackedCount < BT1_MAX_TRACKED_IDS) {
                tIdx = s_bt1TrackedCount++;
                s_bt1Tracked[tIdx].textId = (int)a1;
                s_bt1Tracked[tIdx].callCount = 0;
                s_bt1Tracked[tIdx].seen = true;
                strncpy(s_bt1Tracked[tIdx].lastDecoded, decoded, 79);
                s_bt1Tracked[tIdx].lastDecoded[79] = '\0';
                isNew = true;
            } else if (tIdx >= 0) {
                if (strcmp(s_bt1Tracked[tIdx].lastDecoded, decoded) != 0) {
                    contentChanged = true;
                    strncpy(s_bt1Tracked[tIdx].lastDecoded, decoded, 79);
                    s_bt1Tracked[tIdx].lastDecoded[79] = '\0';
                }
                s_bt1Tracked[tIdx].callCount++;
            }
            
            if (isNew || contentChanged) {
                s_bt1LogCount++;
                uint32_t retAddr = (uint32_t)_ReturnAddress();
                DWORD tick = GetTickCount();
                
                Log::Battle("BattleTTS: [BTXT] #%d t=%u %s textID=%d (0x%X) ret=0x%08X -> \"%s\"",
                           s_bt1LogCount, tick,
                           isNew ? "NEW" : "CHANGED",
                           a1, a1, retAddr, decoded);
            }
            
            // v0.13.26: Phase detection from text IDs + EXP text capture
            if (isNew && mode == 4) {
                int tid = (int)a1;
                if (tid == 22 || tid == 23) {
                    if (s_victoryPhase < VP_EXP) {
                        s_victoryPhase = VP_EXP;
                        Log::Battle("BattleTTS: [VPHASE] -> EXP (textID=%d)", tid);
                        if (!s_expPhase1Announced) {
                            s_expPhase1Announced = true;
                            s_expPhase1Time = GetTickCount();
                            __try {
                                uint8_t party[4] = {};
                                memcpy(party, (void*)VICTORY_PARTY_ADDR, 4);
                                uint16_t expEarned[3] = {};
                                memcpy(expEarned, (void*)VICTORY_EXP_BASE, 6);
                                char buf[512];
                                int pos = 0;
                                int partyCount = 0;
                                for (int i = 0; i < 3; i++) if (party[i] != 0xFF) partyCount++;
                                bool allSame = partyCount < 2 ||
                                    (expEarned[0] == expEarned[1] && expEarned[1] == expEarned[2]);
                                if (allSame) {
                                    for (int i = 0; i < partyCount; i++) {
                                        if (i > 0 && i == partyCount - 1)
                                            pos += snprintf(buf + pos, sizeof(buf) - pos, " and ");
                                        else if (i > 0)
                                            pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
                                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                                                       GetCharNameById(party[i]));
                                    }
                                    pos += snprintf(buf + pos, sizeof(buf) - pos,
                                                   " received %u EXP.", expEarned[0]);
                                } else {
                                    bool announced[3] = {};
                                    bool first = true;
                                    for (int i = 0; i < partyCount; i++) {
                                        if (announced[i]) continue;
                                        if (!first) pos += snprintf(buf + pos, sizeof(buf) - pos, " ");
                                        first = false;
                                        int groupCount = 0;
                                        for (int j = i; j < partyCount; j++) {
                                            if (expEarned[j] == expEarned[i] && !announced[j]) {
                                                if (groupCount > 0)
                                                    pos += snprintf(buf + pos, sizeof(buf) - pos, " and ");
                                                pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                                                               GetCharNameById(party[j]));
                                                announced[j] = true;
                                                groupCount++;
                                            }
                                        }
                                        pos += snprintf(buf + pos, sizeof(buf) - pos,
                                                       " received %u EXP.", expEarned[i]);
                                    }
                                }
                                if (pos > 0) {
                                    ScreenReader::Speak(buf, false);
                                    Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 1: %s", buf);
                                }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                                Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 1 EXCEPTION (BTXT hook)");
                            }
                        }
                    }
                } else if (tid == 21) {
                    if (s_victoryPhase < VP_ITEMS) {
                        s_victoryPhase = VP_ITEMS;
                        Log::Battle("BattleTTS: [VPHASE] -> ITEMS (textID=%d)", tid);
                    }
                } else if (tid == 28) {
                    if (s_victoryPhase < VP_ITEMS) {
                        s_victoryPhase = VP_ITEMS;
                        s_victoryNoItems = true;
                        s_itemsPhaseAnnounced = true;
                        Log::Battle("BattleTTS: [VPHASE] -> ITEMS (no items, textID=28)");
                        ScreenReader::Speak("No items received.", false);
                        Log::Battle("BattleTTS: [VICTORY-TTS] No items received.");
                    }
                } else if (tid == 109) {
                    if (s_victoryPhase < VP_GF_AP) {
                        s_victoryPhase = VP_GF_AP;
                        Log::Battle("BattleTTS: [VPHASE] -> GF_AP (textID=%d)", tid);
                    }
                } else if (tid == 121) {
                    if (s_victoryPhase < VP_GF_LEVELUP) {
                        s_victoryPhase = VP_GF_LEVELUP;
                        Log::Battle("BattleTTS: [VPHASE] -> GF_LEVELUP (textID=%d)", tid);
                        s_entityNameCaptureActive = true;
                        s_entityNameCaptureCount = 0;
                        s_entityPhaseDetectedTime = GetTickCount();
                        memset((void*)s_entityNameCaptures, 0, sizeof(s_entityNameCaptures));
                    }
                } else if (tid == 127) {
                    if (s_victoryPhase < VP_ABILITY) {
                        s_victoryPhase = VP_ABILITY;
                        Log::Battle("BattleTTS: [VPHASE] -> ABILITY (textID=%d)", tid);
                        s_entityNameCaptureActive = true;
                        s_entityNameCaptureCount = 0;
                        s_entityPhaseDetectedTime = GetTickCount();
                        memset((void*)s_entityNameCaptures, 0, sizeof(s_entityNameCaptures));
                    }
                }
            }
            
            // v0.13.27: Capture EXP-to-next-level text during EXP phase
            if (mode == 4 && s_victoryPhase == VP_EXP && decoded[0] != '\0') {
                if (strstr(decoded, "Next") || strstr(decoded, "LEVEL") || 
                    (strstr(decoded, "EXP") && (strstr(decoded, "to") || strstr(decoded, "reach")))) {
                    int slot = -1;
                    for (int i = 0; i < 3; i++) {
                        if (!s_expTextCaptured[i]) {
                            slot = i;
                            break;
                        }
                    }
                    if (slot >= 0) {
                        strncpy(s_capturedExpText[slot], decoded, 127);
                        s_capturedExpText[slot][127] = '\0';
                        s_expTextCaptured[slot] = true;
                        Log::Battle("BattleTTS: [EXP-CAPTURE] Slot %d: \"%s\"", slot, decoded);
                    }
                }
            }
        }
    }
    
    return result;
}

// v0.13.16: Hook for sub_4B7210 (GPU glyph draw) — counter only
static uint32_t __cdecl HookedBtCandidate2(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    InterlockedIncrement(&s_btCount2);
    return s_origBt2(a1, a2, a3, a4, a5, a6, a7, a8);
}

// v0.13.18: Hook for sub_4A3EE0 (victory per-frame loop) — counter only
static uint32_t __cdecl HookedBtCandidate3(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    InterlockedIncrement(&s_btCount3);
    return s_origBt3(a1, a2, a3, a4, a5, a6, a7, a8);
}

// v0.13.25: Hook for sub_5348E0 — control code 0x0A variable expansion
static int s_bt4LogCount = 0;

static uint32_t __cdecl HookedBtCandidate4(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    InterlockedIncrement(&s_btCount4);
    
    uint32_t result = s_origBt4(a1, a2, a3, a4, a5, a6, a7, a8);
    
    if (s_bt4LogCount < 50) {
        uint16_t mode = 0;
        if (FF8Addresses::pGameMode) {
            __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (mode == 4 || mode == 5 || mode == 100) {
            s_bt4LogCount++;
            uint32_t retAddr = (uint32_t)_ReturnAddress();
            DWORD tick = GetTickCount();
            
            Log::Battle("BattleTTS: [VAREXP] #%d t=%u ret=0x%08X a1=%u(0x%X) a2=%u(0x%X) a3=0x%08X a4=0x%08X result=%u(0x%X)",
                       s_bt4LogCount, tick, retAddr,
                       a1, a1, a2, a2, a3, a4, result, result);
            
            uint32_t vals[] = {a1, a2, a3, a4, result};
            const char* labels[] = {"a1", "a2", "a3", "a4", "ret"};
            for (int vi = 0; vi < 5; vi++) {
                if (vals[vi] >= 0x00400000 && vals[vi] < 0x02800000) {
                    __try {
                        char decoded[80] = {};
                        DecodeFF8TextPreview((const uint8_t*)vals[vi], decoded, sizeof(decoded));
                        if (decoded[0] != '\0' && decoded[0] != '{')
                            Log::Battle("BattleTTS: [VAREXP]   %s ff8txt: \"%s\"", labels[vi], decoded);
                        uint8_t* p = (uint8_t*)vals[vi];
                        if (p[0] >= 0x20 && p[0] <= 0x7E) {
                            char ascii[40] = {};
                            for (int j = 0; j < 39 && p[j] >= 0x20 && p[j] <= 0x7E; j++)
                                ascii[j] = (char)p[j];
                            if (ascii[0])
                                Log::Battle("BattleTTS: [VAREXP]   %s ascii: \"%s\"", labels[vi], ascii);
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }
    }
    
    return result;
}

// v0.13.34: Strip {XX} compression tokens from a decoded description string.
static void StripDescriptionTokens(const char* src, char* dst, int maxOut)
{
    int out = 0;
    bool lastWasSpace = true;  // suppress leading spaces
    for (int i = 0; src[i] && out < maxOut - 1; i++) {
        if (src[i] == '{') {
            while (src[i] && src[i] != '}') i++;
        } else if (src[i] == '\n') {
            if (!lastWasSpace && out < maxOut - 1) {
                dst[out++] = ' ';
                lastWasSpace = true;
            }
        } else if (src[i] == ' ') {
            if (!lastWasSpace && out < maxOut - 1) {
                dst[out++] = ' ';
                lastWasSpace = true;
            }
        } else {
            dst[out++] = src[i];
            lastWasSpace = false;
        }
    }
    while (out > 0 && dst[out - 1] == ' ') out--;
    dst[out] = '\0';
}

// v0.13.30: Hook for sub_47EA30 — entity name retrieval
static uint32_t __cdecl HookedBtCandidate5(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    InterlockedIncrement(&s_btCount5);
    uint32_t result = s_origBt5(a1, a2, a3, a4, a5, a6, a7, a8);
    uint16_t mode = 0;
    if (FF8Addresses::pGameMode)
        __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if ((mode == 4 || mode == 5 || mode == 100) &&
        result >= 0x00400000 && result < 0x02800000) {
        __try {
            char decoded[64] = {};
            DecodeFF8String((uint8_t*)result, decoded, sizeof(decoded));
            if (decoded[0] == '\0')
                DecodeFF8TextPreview((const uint8_t*)result, decoded, sizeof(decoded));
            if (decoded[0] != '\0' && decoded[0] != '{') {
                Log::Battle("BattleTTS: [BT5-EA30] mode=%u a1=%u -> \"%s\"", mode, a1, decoded);
                if (mode == 4) {
                    // v0.13.32: Announce item name immediately when VP_ITEMS first fires.
                    if (s_victoryPhase == VP_ITEMS && !s_itemsPhaseAnnounced) {
                    s_itemsPhaseAnnounced = true;

                    // v0.13.40: Read item quantity from victory state drop list.
                    int itemQty = 0;
                    __try {
                        uint8_t* dropList = *(uint8_t**)(0x01A78C88 + 0x50);
                        if (dropList) {
                            for (int di = 0; di < 4; di++) {
                                uint16_t entryWord = *(uint16_t*)(dropList + di * 4);
                                uint8_t entryQty = *(uint8_t*)(dropList + di * 4 + 2);
                                if (entryWord != 0)
                                    Log::Battle("BattleTTS: [ITEM-DROP] entry[%d]: word=0x%04X qty=%u", di, entryWord, entryQty);
                            }
                            for (int di = 0; di < 8; di++) {
                                uint16_t entryWord = *(uint16_t*)(dropList + di * 4);
                                uint8_t entryQty = *(uint8_t*)(dropList + di * 4 + 2);
                                if (entryWord == 0) break;
                                int entryId = (entryWord >= 0x100) ? (entryWord & 0xFF) : entryWord;
                                if (entryId == (int)a1 && entryQty > 0) {
                                    itemQty = entryQty;
                                    break;
                                }
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}

                    // Fetch description from sub_47EA90 using same item_id (a1)
                    char descRaw[128] = {};
                        char descClean[128] = {};
                    if (s_origBt6) {
                        __try {
                            uint32_t descResult = s_origBt6(a1, 0, 0, 0, 0, 0, 0, 0);
                            if (descResult >= 0x00400000 && descResult < 0x02800000) {
                                DecodeFF8TextPreview((const uint8_t*)descResult, descRaw, sizeof(descRaw));
                                StripDescriptionTokens(descRaw, descClean, sizeof(descClean));
                                Log::Battle("BattleTTS: [VICTORY-TTS] Item desc raw: \"%s\"", descRaw);
                                Log::Battle("BattleTTS: [VICTORY-TTS] Item desc clean: \"%s\"", descClean);
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            Log::Battle("BattleTTS: [VICTORY-TTS] Item desc EXCEPTION");
                        }
                    }

                    char buf[256];
                    int qty = (itemQty > 0) ? itemQty : 1;
                    if (descClean[0])
                        snprintf(buf, sizeof(buf), "Received %d %s. Description: %s", qty, decoded, descClean);
                    else
                        snprintf(buf, sizeof(buf), "Received %d %s.", qty, decoded);
                    ScreenReader::Speak(buf, false);
                    Log::Battle("BattleTTS: [VICTORY-TTS] Item (EA30): %s", buf);
                }
                    // Capture during ABILITY/GF_LEVELUP phase
                    if (s_entityNameCaptureActive && s_entityNameCaptureCount < 2) {
                        int captureIdx = s_entityNameCaptureCount;
                        strncpy(s_entityNameCaptures[captureIdx], decoded, 63);
                        s_entityNameCaptures[captureIdx][63] = '\0';
                        InterlockedIncrement((volatile long*)&s_entityNameCaptureCount);
                        Log::Battle("BattleTTS: [ENTITY-NAME] #%d (EA30) -> \"%s\"",
                                   captureIdx + 1, decoded);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

// v0.13.31: Hook for sub_47EA90 — sibling name retrieval
static uint32_t __cdecl HookedBtCandidate6(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    InterlockedIncrement(&s_btCount6);
    uint32_t result = s_origBt6(a1, a2, a3, a4, a5, a6, a7, a8);
    uint16_t mode = 0;
    if (FF8Addresses::pGameMode)
        __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if ((mode == 4 || mode == 5 || mode == 100) &&
        result >= 0x00400000 && result < 0x02800000) {
        __try {
            char decoded[64] = {};
            DecodeFF8TextPreview((const uint8_t*)result, decoded, sizeof(decoded));
            if (decoded[0] != '\0' && decoded[0] != '{') {
                Log::Battle("BattleTTS: [BT6-EA90] mode=%u a1=%u -> \"%s\"", mode, a1, decoded);
                if (mode == 4 && s_entityNameCaptureActive && s_entityNameCaptureCount < 2) {
                    int captureIdx = s_entityNameCaptureCount;
                    strncpy(s_entityNameCaptures[captureIdx], decoded, 63);
                    s_entityNameCaptures[captureIdx][63] = '\0';
                    InterlockedIncrement((volatile long*)&s_entityNameCaptureCount);
                    Log::Battle("BattleTTS: [ENTITY-NAME] #%d (EA90) -> \"%s\"",
                               captureIdx + 1, decoded);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

// v0.13.40: Hook for sub_47E970 — GF name retrieval during victory rendering
static uint32_t __cdecl HookedBtCandidate7(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    uint32_t result = s_origBt7(a1, a2, a3, a4, a5, a6, a7, a8);
    uint16_t mode = 0;
    if (FF8Addresses::pGameMode)
        __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (mode == 4 && result >= 0x00400000 && result < 0x02800000) {
        __try {
            char decoded[64] = {};
            DecodeFF8String((uint8_t*)result, decoded, sizeof(decoded));
            if (decoded[0] == '\0')
                DecodeFF8TextPreview((const uint8_t*)result, decoded, sizeof(decoded));
            if (decoded[0] != '\0' && decoded[0] != '{') {
                if (!s_gfNameCaptured || strcmp(s_renderedGFName, decoded) != 0) {
                    strncpy(s_renderedGFName, decoded, 63);
                    s_renderedGFName[63] = '\0';
                    s_gfNameCaptured = true;
                    Log::Battle("BattleTTS: [GF-NAME-HOOK] sub_47E970: a1=0x%X -> \"%s\"", a1, decoded);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

// v0.13.40: Hook for sub_47E710 — Ability name retrieval during victory rendering
static uint32_t __cdecl HookedBtCandidate8(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    uint32_t result = s_origBt8(a1, a2, a3, a4, a5, a6, a7, a8);
    uint16_t mode = 0;
    if (FF8Addresses::pGameMode)
        __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (mode == 4 && result >= 0x00400000 && result < 0x02800000) {
        __try {
            char decoded[64] = {};
            // Ability-specific decoder (digits at 0x21-0x2A, not 0x24-0x2D)
            int out = 0;
            const uint8_t* src = (const uint8_t*)result;
            for (int i = 0; i < 60 && out < 62; i++) {
                uint8_t b = src[i];
                if (b == 0x00) break;
                if (b == 0x20)                   decoded[out++] = ' ';
                else if (b >= 0x45 && b <= 0x5E) decoded[out++] = 'A' + (b - 0x45);
                else if (b >= 0x5F && b <= 0x78) decoded[out++] = 'a' + (b - 0x5F);
                else if (b >= 0x21 && b <= 0x2A) decoded[out++] = '0' + (b - 0x21);
                else if (b == 0x2B)              decoded[out++] = '%';
                else if (b == 0x31)              decoded[out++] = '+';
                else if (b == 0x2F)              decoded[out++] = '-';
                else if (b == 0x2E)              decoded[out++] = '.';
                else if (b == 0x30)              decoded[out++] = '=';
            }
            decoded[out] = '\0';
            if (decoded[0] != '\0' && decoded[0] != '{') {
                if (!s_abilityNameCaptured || strcmp(s_renderedAbilityName, decoded) != 0) {
                    strncpy(s_renderedAbilityName, decoded, 63);
                    s_renderedAbilityName[63] = '\0';
                    s_abilityNameCaptured = true;
                    char hx[80] = {};
                    int hp = 0;
                    const uint8_t* rp = (const uint8_t*)result;
                    for (int b = 0; b < 20 && rp[b] != 0 && hp < 70; b++)
                        hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", rp[b]);
                    Log::Battle("BattleTTS: [ABILITY-NAME-HOOK] sub_47E710: a1=%u(0x%X) -> \"%s\" hex=[%s]",
                               a1, a1, decoded, hx);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

// ============================================================================
// Install MinHook on battle text hooks (8/8)
// ============================================================================

static void InstallBattleTextHooks()
{
    if (s_battleTextHooksInstalled) return;
    
    struct { uint32_t addr; void* hook; void** orig; const char* name; } targets[] = {
        { BT_ADDR1, (void*)&HookedBtCandidate1, (void**)&s_origBt1, "sub_47EC70" },
        { BT_ADDR2, (void*)&HookedBtCandidate2, (void**)&s_origBt2, "sub_4B7210" },
        { BT_ADDR3, (void*)&HookedBtCandidate3, (void**)&s_origBt3, "sub_4A3EE0" },
        { BT_ADDR4, (void*)&HookedBtCandidate4, (void**)&s_origBt4, "sub_5348E0" },
        { BT_ADDR5, (void*)&HookedBtCandidate5, (void**)&s_origBt5, "sub_47EA30" },
        { BT_ADDR6, (void*)&HookedBtCandidate6, (void**)&s_origBt6, "sub_47EA90" },
        { BT_ADDR7, (void*)&HookedBtCandidate7, (void**)&s_origBt7, "sub_47E970" },
        { BT_ADDR8, (void*)&HookedBtCandidate8, (void**)&s_origBt8, "sub_47E710" },
    };
    int installed = 0;
    for (int i = 0; i < 8; i++) {
        __try {
            uint8_t* p = (uint8_t*)targets[i].addr;
            char hx[50] = {};
            int hp = 0;
            for (int b = 0; b < 8; b++)
                hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", p[b]);
            Log::Battle("BattleTTS: [BT-HOOK] %s @ 0x%08X: %s", targets[i].name, targets[i].addr, hx);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        MH_STATUS st = MH_CreateHook((void*)targets[i].addr, targets[i].hook, targets[i].orig);
        if (st != MH_OK) {
            Log::Battle("BattleTTS: [BT-HOOK] %s MH_CreateHook FAILED: %d", targets[i].name, (int)st);
            continue;
        }
        st = MH_EnableHook((void*)targets[i].addr);
        if (st != MH_OK) {
            Log::Battle("BattleTTS: [BT-HOOK] %s MH_EnableHook FAILED: %d", targets[i].name, (int)st);
            continue;
        }
        installed++;
        Log::Battle("BattleTTS: [BT-HOOK] %s hooked OK", targets[i].name);
    }
    s_battleTextHooksInstalled = (installed > 0);
    Log::Battle("BattleTTS: [BT-HOOK] %d/8 hooks installed", installed);
}

// ============================================================================
// GF fallback functions
// ============================================================================

// v0.13.30: Read name of first junctioned GF for any active party member.
static void GetAnyJunctionedGFName(char* outName, int maxName)
{
    static const char* GF_FALLBACK_NAMES[] = {
        "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
        "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
        "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
    };
    outName[0] = '\0';
    __try {
        uint8_t party[4] = {};
        memcpy(party, (void*)VICTORY_PARTY_ADDR, 4);
        for (int ps = 0; ps < 3 && outName[0] == '\0'; ps++) {
            if (party[ps] >= 11) continue;
            uint8_t* ch = (uint8_t*)(0x1CFE0E8 + party[ps] * 0x98);
            uint16_t gfMask = *(uint16_t*)(ch + 0x58);
            for (int gfIdx = 0; gfIdx < 16 && outName[0] == '\0'; gfIdx++) {
                if (!(gfMask & (1 << gfIdx))) continue;
                uint8_t* gf = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
                if (*(gf + 0x11) == 0) continue;
                DecodeFF8String(gf, outName, maxName);
                if (outName[0] == '\0')
                    strncpy(outName, GF_FALLBACK_NAMES[gfIdx], maxName - 1);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.13.36: Find which junctioned GF's savemap data changed vs pre-battle snapshot.
static int FindChangedGF()
{
    if (!s_preBattleGFSnapValid) {
        Log::Battle("BattleTTS: [GF-DIFF] No pre-battle GF snapshot available");
        return -1;
    }
    
    int bestGF = -1;
    int bestChanges = 0;
    
    __try {
        uint8_t party[4] = {};
        memcpy(party, (void*)VICTORY_PARTY_ADDR, 4);
        
        bool checked[16] = {};
        for (int ps = 0; ps < 3; ps++) {
            if (party[ps] >= 11) continue;
            uint8_t* ch = (uint8_t*)(0x1CFE0E8 + party[ps] * 0x98);
            uint16_t gfMask = *(uint16_t*)(ch + 0x58);
            
            for (int gi = 0; gi < 16; gi++) {
                if (checked[gi]) continue;
                if (!(gfMask & (1 << gi))) continue;
                checked[gi] = true;
                
                uint8_t* gf = (uint8_t*)(SAVEMAP_GF_BASE + gi * SAVEMAP_GF_STRIDE);
                if (*(gf + 0x11) == 0) continue;
                
                int changes = 0;
                for (int b = 0; b < (int)SAVEMAP_GF_STRIDE; b++) {
                    if (gf[b] != s_preBattleGFStructs[gi][b]) changes++;
                }
                
                if (changes > 0) {
                    Log::Battle("BattleTTS: [GF-DIFF] GF%d: %d bytes changed vs pre-battle", gi, changes);
                    for (int b = 0; b < (int)SAVEMAP_GF_STRIDE; b++) {
                        if (gf[b] != s_preBattleGFStructs[gi][b]) {
                            Log::Battle("BattleTTS: [GF-DIFF]   +0x%02X: 0x%02X -> 0x%02X",
                                       b, s_preBattleGFStructs[gi][b], gf[b]);
                        }
                    }
                }
                
                if (changes > bestChanges) {
                    bestChanges = changes;
                    bestGF = gi;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [GF-DIFF] EXCEPTION in FindChangedGF");
    }
    
    Log::Battle("BattleTTS: [GF-DIFF] Best match: GF%d (%d byte changes)", bestGF, bestChanges);
    return bestGF;
}

// v0.13.36: Read the savemap name for a given GF index.
static void GetGFNameByIndex(int gfIdx, char* outName, int maxName)
{
    static const char* GF_FALLBACK[] = {
        "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
        "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
        "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
    };
    outName[0] = '\0';
    if (gfIdx < 0 || gfIdx >= 16) return;
    __try {
        uint8_t* gf = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
        DecodeFF8String(gf, outName, maxName);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (outName[0] == '\0')
        strncpy(outName, GF_FALLBACK[gfIdx], maxName - 1);
}

// ============================================================================
// GF ability tables + lookup
// ============================================================================

// v0.13.35: Session-level ability announcement dedup.
static bool s_gfAbilityAnnounced[16][22] = {};

// v0.13.32: Hardcoded GF ability name tables (kernel.bin order).
static const char* const GF_ABILITY_NAMES[16][22] = {
    // 0: Quezacotl (20 abilities)
    {"T Mag-RF", "I Mag-RF", "Card", "Card Mod", "Mid Mag-RF", "Magic RF",
     "Magic+20%", "Magic+40%", "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%",
     "SumMag+40%", "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Mug", "Recover", nullptr, nullptr},
    // 1: Shiva (18 abilities)
    {"I Mag-RF", "Doom", "Spd+20%", "Spd+40%", "Eva+30%", "Luck+50%",
     "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%", "GFHP+10%",
     "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0", "Mug", "Recover", nullptr, nullptr, nullptr, nullptr},
    // 2: Ifrit (21 abilities)
    {"F Mag-RF", "Ammo-RF", "Str+20%", "Str+40%", "Str+60%", "Str Bonus",
     "Boost", "Mad Rush", "Counter", "SumMag+10%", "SumMag+20%", "SumMag+30%",
     "SumMag+40%", "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Mug", "Recover", "Haggle", nullptr},
    // 3: Siren (20 abilities)
    {"L Mag-RF", "ST Mag-RF", "Tool-RF", "Forbidden Med-RF", "Move-Find", "Blind Jnction",
     "Blind Attack", "Blind Def", "Blind Def x2", "Boost", "SumMag+10%", "SumMag+20%",
     "SumMag+30%", "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Mug", "Recover", nullptr, nullptr},
    // 4: Brothers (14 abilities)
    {"High Mag-RF", "Revive", "HP+20%", "HP+40%", "HP+80%", "HP Bonus",
     "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%",
     "GFHP+10%", "GFHP+20%", "Vit 0", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 5: Diablos (17 abilities)
    {"Time Mag-RF", "ST Mag-RF", "Demi", "Darkside", "Mug", "Travel",
     "Enc-Half", "Enc-None", "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%",
     "SumMag+40%", "GFHP+10%", "GFHP+20%", "GFHP+30%", "Vit 0", nullptr, nullptr, nullptr, nullptr, nullptr},
    // 6: Carbuncle (16 abilities)
    {"Recov Med-RF", "Recov Med-RF x2", "Spd+20%", "Spd+40%", "Eva+30%", "Luck+50%",
     "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%",
     "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 7: Leviathan (20 abilities)
    {"Supt Mag-RF", "GFRecov Med-RF", "Spr+20%", "Spr+40%", "Spr+60%", "Spr Bonus",
     "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%",
     "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Mug", "Recover", "Luck+50%", "Eva+30%", nullptr, nullptr},
    // 8: Pandemona (13 abilities)
    {"Spd+20%", "Spd+40%", "Spd+60%", "Spd Bonus", "Boost", "SumMag+10%",
     "SumMag+20%", "SumMag+30%", "SumMag+40%", "GFHP+10%", "GFHP+20%",
     "GFHP+30%", "Vit 0", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 9: Cerberus (16 abilities)
    {"Spd+20%", "Spd+40%", "Eva+30%", "Luck+50%", "Boost", "SumMag+10%",
     "SumMag+20%", "SumMag+30%", "SumMag+40%", "GFHP+10%", "GFHP+20%",
     "GFHP+30%", "GFHP+40%", "Vit 0", "Counter", "Initiative", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 10: Alexander (20 abilities)
    {"Med LV Up", "Rev Med-RF", "Holy War", "Spr+20%", "Spr+40%", "Spr+60%",
     "Spr Bonus", "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%",
     "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Mug", "Recover", "Auto-Shell", nullptr, nullptr},
    // 11: Doomtrain (16 abilities)
    {"Forbid Med-RF", "Junk Shop", "CALL Shop", "Rare Item", "Boost", "SumMag+10%",
     "SumMag+20%", "SumMag+30%", "SumMag+40%", "GFHP+10%", "GFHP+20%",
     "GFHP+30%", "GFHP+40%", "Vit 0", "Auto-Protect", "Auto-Shell", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 12: Bahamut (18 abilities)
    {"Forbid Mag-RF", "Rare Item", "Str+20%", "Str+40%", "Str+60%", "Str Bonus",
     "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%",
     "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Auto-Protect", "Auto-Shell", nullptr, nullptr, nullptr, nullptr},
    // 13: Cactuar (12 abilities)
    {"Mag+20%", "Mag+40%", "Spd+20%", "Spd+40%", "Luck+50%", "Boost",
     "SumMag+10%", "SumMag+20%", "SumMag+30%", "GFHP+10%", "GFHP+20%", "Vit 0",
     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 14: Tonberry (16 abilities)
    {"Haggle", "Sell-High", "Familiar", "Call Shop", "LV Down", "LV Up",
     "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%", "SumMag+40%",
     "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    // 15: Eden (19 abilities)
    {"Str+20%", "Str+40%", "Str+60%", "Str Bonus", "Mag+20%", "Mag+40%",
     "Spr+20%", "Spr+40%", "Boost", "SumMag+10%", "SumMag+20%", "SumMag+30%",
     "SumMag+40%", "GFHP+10%", "GFHP+20%", "GFHP+30%", "GFHP+40%", "Vit 0",
     "Expendx3-1", nullptr, nullptr, nullptr},
};
static const int GF_ABILITY_COUNTS[16] = {
    20, 18, 21, 20, 14, 17, 16, 20, 13, 16, 20, 16, 18, 12, 16, 19
};

// v0.13.32: Look up ability name from savemap for the GF that just learned.
static void GetJustLearnedAbilityName(char* outName, int maxName, int gfIdx)
{
    outName[0] = '\0';
    if (gfIdx < 0 || gfIdx >= 16) return;
    __try {
        uint8_t* gf = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
        uint8_t learningIdx = *(gf + 0x41);
        int count = GF_ABILITY_COUNTS[gfIdx];
        int slotA = (learningIdx > 0) ? (int)learningIdx - 1 : -1;
        int slotB = (int)learningIdx;
        Log::Battle("BattleTTS: [ABILITY-LOOKUP] GF=%d learningIdx=%u slotA=%d slotB=%d count=%d",
                   gfIdx, learningIdx, slotA, slotB, count);
        {
            char hx[256] = {};
            int hp = 0;
            for (int b = 0; b < (int)SAVEMAP_GF_STRIDE && hp < 240; b++)
                hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", gf[b]);
            Log::Battle("BattleTTS: [GF-STRUCT] GF%d full (%d bytes): %s", gfIdx, SAVEMAP_GF_STRIDE, hx);
        }
        const char* name = nullptr;
        if (slotB >= 0 && slotB < count && GF_ABILITY_NAMES[gfIdx][slotB])
            name = GF_ABILITY_NAMES[gfIdx][slotB];
        else if (slotA >= 0 && slotA < count && GF_ABILITY_NAMES[gfIdx][slotA])
            name = GF_ABILITY_NAMES[gfIdx][slotA];
        if (name)
            strncpy(outName, name, maxName - 1);
        Log::Battle("BattleTTS: [ABILITY-LOOKUP] Result: \"%s\"", outName[0] ? outName : "(not found)");
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// Victory cross-reference data (diagnostic log, not TTS)
// ============================================================================

static void LogVictoryCrossRefData()
{
    Log::Battle("BattleTTS: [VICTORY-XREF] === Cross-reference data from memory ===");
    
    uint8_t party[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    __try { memcpy(party, (void*)VICTORY_PARTY_ADDR, 4); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    uint16_t expEarned[3] = {};
    __try { memcpy(expEarned, (void*)VICTORY_EXP_BASE, 6); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    uint32_t expEarned32[3] = {};
    __try { memcpy(expEarned32, (void*)VICTORY_EXP_BASE, 12); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    uint16_t apEarned[3] = {};
    __try { memcpy(apEarned, (void*)VICTORY_AP_BASE, 6); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    Log::Battle("BattleTTS: [VICTORY-XREF] Party=[%u,%u,%u] EXP16=[%u,%u,%u] EXP32=[%u,%u,%u] AP=[%u,%u,%u]",
               party[0], party[1], party[2],
               expEarned[0], expEarned[1], expEarned[2],
               expEarned32[0], expEarned32[1], expEarned32[2],
               apEarned[0], apEarned[1], apEarned[2]);
    
    __try {
        char hx[200];
        int hp;
        hp = 0;
        uint8_t* expBase = (uint8_t*)VICTORY_EXP_BASE;
        for (int b = 0; b < 32 && hp < 190; b++)
            hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", expBase[b]);
        Log::Battle("BattleTTS: [VICTORY-XREF] EXP region (0x%08X+32): %s", VICTORY_EXP_BASE, hx);
        
        hp = 0;
        uint8_t* apBase = (uint8_t*)VICTORY_AP_BASE;
        for (int b = 0; b < 16 && hp < 190; b++)
            hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", apBase[b]);
        Log::Battle("BattleTTS: [VICTORY-XREF] AP region (0x%08X+16): %s", VICTORY_AP_BASE, hx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    Log::Battle("BattleTTS: [VICTORY-XREF] === Done ===");
}

// ============================================================================
// Victory TTS reset
// ============================================================================

static void ResetVictoryTTS()
{
    s_victoryTTSActive = false;
    s_victoryAutoCapture = 0;
    s_bt1LogCount = 0;
    s_bt4LogCount = 0;
    s_victoryTTSAnnounceDone = false;
    s_victoryTTSEntryTime = 0;
    s_bt1TrackedCount = 0;
    memset(s_bt1Tracked, 0, sizeof(s_bt1Tracked));
    s_victoryPhase = VP_NONE;
    s_victoryPhasePrev = VP_NONE;
    s_expPhase1Announced = false;
    s_expPhase2Announced = false;
    s_expPhase1Time = 0;
    s_expPollLastLog = 0;
    s_itemsPhaseAnnounced = false;
    s_gfAPAnnounced = false;
    s_victoryNoItems = false;
    s_abilityAnnounced = false;
    s_gfLevelupAnnounced = false;
    s_entityNameCaptureActive = false;
    s_entityNameCaptureCount = 0;
    s_entityPhaseDetectedTime = 0;
    memset(s_entityNameCaptures, 0, sizeof(s_entityNameCaptures));
    s_renderedGFName[0] = '\0';
    s_renderedAbilityName[0] = '\0';
    s_gfNameCaptured = false;
    s_abilityNameCaptured = false;
    s_lastAnnouncedVictoryGFName[0] = '\0';
    for (int i = 0; i < 3; i++) {
        s_capturedExpText[i][0] = '\0';
        s_expTextCaptured[i] = false;
    }
}

// ============================================================================
// Victory auto-capture (screenshot on phase transitions)
// ============================================================================

static void VictoryAutoCapture(const char* label)
{
    s_victoryAutoCapture++;
    char path[512];
    snprintf(path, sizeof(path),
             "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
             "\\FF8_OriginalPC_mod\\Logs\\victory_auto_%d_%s",
             s_victoryAutoCapture, label);
    CaptureScreenshot(path);
    Log::Battle("BattleTTS: [VICTORY-TTS] Auto-capture %d: %s", s_victoryAutoCapture, label);
}

// ============================================================================
// Victory screen background thread (~30Hz polling)
// ============================================================================

static DWORD WINAPI VictoryScreenThreadFunc(LPVOID)
{
    uint16_t prevMode = 0;
    bool f12WasDown = false;
    int stepCount = 0;
    
    Log::Battle("BattleTTS: [VICTORY-THREAD] Started");
    
    while (!s_victoryThreadStop) {
        Sleep(33);  // ~30Hz polling
        
        if (!FF8Addresses::pGameMode) continue;
        
        uint16_t mode = 0;
        __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        
        // Log mode transitions
        if (mode != prevMode) {
            Log::Battle("BattleTTS: [VICTORY-THREAD] Mode: %u -> %u", prevMode, mode);
            if (mode == 3 && prevMode != 3) {
                stepCount = 0;
                f12WasDown = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
                ResetVictoryTTS();
            }
            if (mode == 4 && prevMode != 4) {
                ScreenReader::Speak("Victory!", true);
                Log::Battle("BattleTTS: [VICTORY-TTS] Victory! (mode 4 entered)");
                s_victoryTTSActive = true;
                s_victoryTTSEntryTime = GetTickCount();
                VictoryAutoCapture("pose");
                
                s_bt1TrackedCount = 0;
                memset(s_bt1Tracked, 0, sizeof(s_bt1Tracked));
                s_bt1LogCount = 0;
                Log::Battle("BattleTTS: [VICTORY-THREAD] BTXT tracker reset at mode 4 entry");
                if (s_preBattleExpSnapValid) {
                    __try {
                        uint8_t partySnap[4] = {};
                        memcpy(partySnap, (void*)VICTORY_PARTY_ADDR, 4);
                        for (int ps = 0; ps < 3; ps++) {
                            if (partySnap[ps] < 11) {
                                uint8_t* ch = (uint8_t*)(0x1CFE0E8 + partySnap[ps] * 0x98);
                                uint32_t curExp = *(uint32_t*)(ch + 0x04);
                                uint32_t preExp = s_preBattleExpAll[partySnap[ps]];
                                int preLv = (int)(preExp / 1000) + 1;
                                if (preLv > 100) preLv = 100;
                                Log::Battle("BattleTTS: [VICTORY-TTS] %s: pre-battle=%u(Lv%d) mode4-entry=%u delta=%d",
                                           GetCharNameById(partySnap[ps]), preExp, preLv,
                                           curExp, (int)(curExp - preExp));
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
            if (prevMode == 4 && mode != 4) {
                ResetVictoryTTS();
            }
        }
        
        // F12 capture
        bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
        bool f12Pressed = f12Down && !f12WasDown;
        f12WasDown = f12Down;
        
        if (f12Pressed) {
            stepCount++;
            Log::Battle("BattleTTS: [VICTORY-THREAD] F12 pressed (mode %u) — step %d", mode, stepCount);
            DumpVictoryStep(stepCount);
            
            char buf[64];
            snprintf(buf, sizeof(buf), "Step %d captured.", stepCount);
            ScreenReader::Speak(buf, true);
        }
        
        // Phase-based victory TTS (driven by BTXT hook phase detection)
        if (mode == 4) {
            int curPhase = s_victoryPhase;
            
            // EXP Phase 1: fallback (normally fires from BTXT hook)
            if (curPhase >= VP_EXP && !s_expPhase1Announced) {
                s_expPhase1Announced = true;
                __try {
                    uint8_t party[4] = {};
                    memcpy(party, (void*)VICTORY_PARTY_ADDR, 4);
                    uint16_t expEarned[3] = {};
                    memcpy(expEarned, (void*)VICTORY_EXP_BASE, 6);
                    
                    char buf[512];
                    int pos = 0;
                    bool allSame = (expEarned[0] == expEarned[1] && expEarned[1] == expEarned[2]);
                    int partyCount = 0;
                    for (int i = 0; i < 3; i++) if (party[i] != 0xFF) partyCount++;
                    
                    s_expPhase1Time = GetTickCount();
                    if (allSame || partyCount <= 1) {
                        for (int i = 0; i < partyCount; i++) {
                            if (i > 0 && i == partyCount - 1) pos += snprintf(buf + pos, sizeof(buf) - pos, " and ");
                            else if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
                            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", GetCharNameById(party[i]));
                        }
                        pos += snprintf(buf + pos, sizeof(buf) - pos, " received %u EXP.", expEarned[0]);
                    } else {
                        bool announced[3] = {};
                        bool first = true;
                        for (int i = 0; i < partyCount; i++) {
                            if (announced[i]) continue;
                            if (!first) pos += snprintf(buf + pos, sizeof(buf) - pos, " ");
                            first = false;
                            int groupCount = 0;
                            for (int j = i; j < partyCount; j++) {
                                if (expEarned[j] == expEarned[i] && !announced[j]) {
                                    if (groupCount > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, " and ");
                                    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", GetCharNameById(party[j]));
                                    announced[j] = true;
                                    groupCount++;
                                }
                            }
                            pos += snprintf(buf + pos, sizeof(buf) - pos, " received %u EXP.", expEarned[i]);
                        }
                    }
                    ScreenReader::Speak(buf, false);
                    Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 1: %s", buf);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 1 EXCEPTION");
                }
            }
            
            // EXP Phase 2: level-ups + EXP-to-next
            if (curPhase == VP_EXP && s_expPhase1Announced && !s_expPhase2Announced && s_preBattleExpSnapValid) {
                __try {
                    uint8_t party[4] = {};
                    memcpy(party, (void*)VICTORY_PARTY_ADDR, 4);
                    
                    bool savemapUpdated = false;
                    for (int ps = 0; ps < 3; ps++) {
                        if (party[ps] >= 11) continue;
                        uint8_t* ch = (uint8_t*)(0x1CFE0E8 + party[ps] * 0x98);
                        uint32_t curExp = *(uint32_t*)(ch + 0x04);
                        if (curExp != s_preBattleExpAll[party[ps]]) { savemapUpdated = true; break; }
                    }
                    
                    DWORD now = GetTickCount();
                    if (now - s_expPollLastLog >= 2000) {
                        s_expPollLastLog = now;
                        for (int ps = 0; ps < 3; ps++) {
                            if (party[ps] >= 11) continue;
                            uint8_t* ch = (uint8_t*)(0x1CFE0E8 + party[ps] * 0x98);
                            uint32_t curExp = *(uint32_t*)(ch + 0x04);
                            Log::Battle("BattleTTS: [EXP-POLL] %s: savemap=%u pre=%u %s",
                                       GetCharNameById(party[ps]), curExp,
                                       s_preBattleExpAll[party[ps]],
                                       curExp != s_preBattleExpAll[party[ps]] ? "CHANGED" : "same");
                        }
                    }
                    
                    if (savemapUpdated) {
                        s_expPhase2Announced = true;
                        char buf[512];
                        int pos = 0;
                        
                        for (int ps = 0; ps < 3; ps++) {
                            if (party[ps] >= 11) continue;
                            
                            uint8_t* ch = (uint8_t*)(0x1CFE0E8 + party[ps] * 0x98);
                            uint32_t postExp = *(uint32_t*)(ch + 0x04);
                            
                            int curLevel = (int)(postExp / 1000) + 1;
                            if (curLevel > 100) curLevel = 100;
                            int preBattleLevel = (int)(s_preBattleExpAll[party[ps]] / 1000) + 1;
                            if (preBattleLevel > 100) preBattleLevel = 100;
                            bool leveledUp = (curLevel > preBattleLevel);
                            
                            if (curLevel >= 100) {
                                if (leveledUp) {
                                    pos += snprintf(buf + pos, sizeof(buf) - pos,
                                        "%s reached level 100. ", GetCharNameById(party[ps]));
                                }
                            } else {
                                uint32_t nextThreshold = (uint32_t)curLevel * 1000;
                                uint32_t expToNext = nextThreshold - postExp;
                                int nextLevel = curLevel + 1;
                                
                                if (leveledUp) {
                                    pos += snprintf(buf + pos, sizeof(buf) - pos,
                                        "%s reached level %d and has %u EXP to reach level %d. ",
                                        GetCharNameById(party[ps]), curLevel, expToNext, nextLevel);
                                } else {
                                    pos += snprintf(buf + pos, sizeof(buf) - pos,
                                        "%s has %u EXP to reach level %d. ",
                                        GetCharNameById(party[ps]), expToNext, nextLevel);
                                }
                            }
                        }
                        
                        if (pos > 0) {
                            ScreenReader::Speak(buf, false);
                            Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 2: %s", buf);
                        }
                        Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 2 fired (savemap change detected)");
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log::Battle("BattleTTS: [VICTORY-TTS] EXP Phase 2 EXCEPTION");
                }
            }
            
            // GF AP phase
            if (curPhase >= VP_GF_AP && !s_gfAPAnnounced) {
                s_gfAPAnnounced = true;
                __try {
                    uint16_t ap = *(uint16_t*)VICTORY_AP_BASE;
                    if (ap > 0) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "GF received %u AP.", ap);
                        ScreenReader::Speak(buf, false);
                        Log::Battle("BattleTTS: [VICTORY-TTS] %s", buf);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }

            // GF Level Up TTS
            if (curPhase == VP_GF_LEVELUP) {
                if (s_gfNameCaptured && s_renderedGFName[0] != '\0' &&
                    strcmp(s_renderedGFName, s_lastAnnouncedVictoryGFName) != 0) {
                    strncpy(s_lastAnnouncedVictoryGFName, s_renderedGFName, 63);
                    s_gfLevelupAnnounced = true;
                    s_entityNameCaptureActive = false;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "GF %s leveled up.", s_renderedGFName);
                    ScreenReader::Speak(buf, false);
                    Log::Battle("BattleTTS: [VICTORY-TTS] GF Level Up (deep hook): %s", buf);
                } else if (!s_gfLevelupAnnounced &&
                           s_entityPhaseDetectedTime > 0 && GetTickCount() - s_entityPhaseDetectedTime >= 500) {
                    s_gfLevelupAnnounced = true;
                    s_entityNameCaptureActive = false;
                    int gfIdx = FindChangedGF();
                    char gfName[64] = {};
                    if (gfIdx >= 0) GetGFNameByIndex(gfIdx, gfName, sizeof(gfName));
                    else GetAnyJunctionedGFName(gfName, sizeof(gfName));
                    char buf[128];
                    if (gfName[0]) snprintf(buf, sizeof(buf), "GF %s leveled up.", gfName);
                    else snprintf(buf, sizeof(buf), "GF leveled up.");
                    ScreenReader::Speak(buf, false);
                    Log::Battle("BattleTTS: [VICTORY-TTS] GF Level Up (fallback): %s", buf);
                }
            }

            // Ability Learned TTS
            if (curPhase >= VP_ABILITY && !s_abilityAnnounced) {
                if (s_entityPhaseDetectedTime > 0 && GetTickCount() - s_entityPhaseDetectedTime >= 200) {
                    s_abilityAnnounced = true;
                    s_entityNameCaptureActive = false;
                    __try {
                        char gfName[64] = {};
                        char abilityName[64] = {};
                        if (s_gfNameCaptured && s_renderedGFName[0] != '\0')
                            strncpy(gfName, s_renderedGFName, 63);
                        if (s_abilityNameCaptured && s_renderedAbilityName[0] != '\0')
                            strncpy(abilityName, s_renderedAbilityName, 63);
                        if (gfName[0] == '\0') {
                            int gfIdx = FindChangedGF();
                            if (gfIdx >= 0) GetGFNameByIndex(gfIdx, gfName, sizeof(gfName));
                            else GetAnyJunctionedGFName(gfName, sizeof(gfName));
                        }
                        if (abilityName[0] == '\0') {
                            int gfIdx = FindChangedGF();
                            if (gfIdx >= 0)
                                GetJustLearnedAbilityName(abilityName, sizeof(abilityName), gfIdx);
                        }
                        char buf[256];
                        if (gfName[0] && abilityName[0])
                            snprintf(buf, sizeof(buf), "GF %s learned %s.", gfName, abilityName);
                        else if (gfName[0])
                            snprintf(buf, sizeof(buf), "GF %s learned a new ability.", gfName);
                        else
                            snprintf(buf, sizeof(buf), "GF learned a new ability.");
                        ScreenReader::Speak(buf, false);
                        Log::Battle("BattleTTS: [VICTORY-TTS] Ability: %s", buf);
                        strncpy(s_lastAnnouncedVictoryGFName, gfName, 63);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // Post-ability level-ups
            if (curPhase >= VP_ABILITY && s_abilityAnnounced && s_gfNameCaptured) {
                if (s_renderedGFName[0] != '\0' &&
                    strcmp(s_renderedGFName, s_lastAnnouncedVictoryGFName) != 0) {
                    strncpy(s_lastAnnouncedVictoryGFName, s_renderedGFName, 63);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "GF %s leveled up.", s_renderedGFName);
                    ScreenReader::Speak(buf, false);
                    Log::Battle("BattleTTS: [VICTORY-TTS] GF Level Up (post-ability): %s", buf);
                }
            }
        }
        
        // Delayed cross-reference data dump (log only, no TTS)
        if (mode == 4 && !s_victoryTTSAnnounceDone && s_victoryTTSEntryTime > 0) {
            DWORD elapsed = GetTickCount() - s_victoryTTSEntryTime;
            if (elapsed >= VICTORY_TTS_DELAY_MS) {
                s_victoryTTSAnnounceDone = true;
                LogVictoryCrossRefData();
            }
        }
        
        prevMode = mode;
    }
    
    Log::Battle("BattleTTS: [VICTORY-THREAD] Stopped");
    return 0;
}
