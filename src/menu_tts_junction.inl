// menu_tts_junction.inl — GCW decoder, junction TTS (char select, GF, ability)
// Included from menu_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

static void DecodeGcwToBuffer(const uint8_t* gcwBuf, int gcwLen, char* outBuf, int outBufSize)
{
    outBuf[0] = '\0';
    if (gcwLen <= 0) return;
    std::string tmp = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
    strncpy(outBuf, tmp.c_str(), outBufSize - 1);
    outBuf[outBufSize - 1] = '\0';
}

// Default GF names (fallback if savemap read fails)
static const char* GF_DEFAULT_NAMES[] = {
    "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
    "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
    "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
};
static const int GF_COUNT = 16;
static const int GF_STRUCT_SIZE = 68;  // 0x44 bytes per GF in savemap

// State tracking
static bool     s_juncActive = false;          // true while Junction subsystem is active (+0x1E8==17)
static uint8_t  s_juncPrevCharCursor = 0xFF;    // previous character select cursor
static uint8_t  s_juncPrevFocus = 0xFF;         // previous +0x22E focus state
static uint8_t  s_juncPrevActionCursor = 0xFF;  // previous action menu cursor (+0x26C)
static bool     s_juncCharSelectAnnounced = false; // true after first char select announce
static uint8_t  s_juncPrevSuboptCursor = 0xFF;  // previous sub-option cursor (+0x268)
static uint8_t  s_juncPrevGfListCursor = 0xFF;  // previous GF list cursor (+0x26D)
static uint8_t  s_juncPrevGfToggle = 0xFF;      // previous GF toggle state (+0x27F)

// v0.09.46: Ability screen state — CORRECTED via user testing
// Each panel has its own focus value and cursor offset:
//   LEFT (equipped slots):   focus=24, cursor at +0x27C
//   RIGHT (available list):  focus=28, cursor at +0x271
// v0.09.45 had these swapped. User test proved: focus=28 cursor goes to 10+
// (many GF abilities = RIGHT), focus=24 cursor stays 0-3 (few slots = LEFT).
// Intermediate focus values (21, 22, 26, 27) are transitions — ignore them.
static const int ABIL_LEFT_CURSOR_OFF  = 0x27C;  // left panel cursor (focus=24)
static const int ABIL_RIGHT_CURSOR_OFF = 0x271;  // right panel cursor (focus=28)
static const int ABIL_LEFT_FOCUS  = 24;
static const int ABIL_RIGHT_FOCUS = 28;
static uint8_t  s_juncPrevAbilLeftCursor  = 0xFF;
static uint8_t  s_juncPrevAbilRightCursor = 0xFF;
static uint8_t  s_juncPrevAbilFocus = 0xFF;  // tracks focus for panel transition detection
// v0.09.48: Right panel GF bitmap reconstruction
// Builds the available abilities list from junctioned GFs' completeAbilities bitmaps.
// Sorted in ascending unified ability ID order. Rebuilt when entering the right panel.
static const int ABIL_RIGHT_LIST_MAX = 128;
static uint8_t  s_abilRightList[ABIL_RIGHT_LIST_MAX] = {};  // ability IDs in display order
static int      s_abilRightListCount = 0;
static uint8_t  s_abilLastLeftCursor = 0;  // tracks which left slot was last active (0-2=cmd, 3-6=ability)
static uint16_t s_juncCachedGfMasks[8] = {}; // v0.09.49: GF bitmasks for ALL chars, cached at Junction entry (game zeroes them during editing)
static uint8_t  s_juncSelectedCharIdx = 0xFF;  // v0.09.49: cached charIdx from char select (formation array gets rewritten during editing)

// v0.18.2.1: J-Auto submenu. From the action menu (Junction/Off/Auto) confirming
// "Auto" opens a 3-option submenu that auto-junctions the character's magic to
// optimize a stat. Confirmed by SUBMON (BAT 2026-06-02): focus (+0x22E) settles
// at 11 and stays there while navigating; the option cursor is +0x26A (0/1/2) and
// the GCW help line tracked it exactly ("Junction magic to up Str/Mag/HP").
static const int JUNC_AUTO_FOCUS      = 11;     // +0x22E value on the Auto submenu
static const int JUNC_AUTO_CURSOR_OFF = 0x26A;  // option cursor (0=Atk,1=Mag,2=Def)
static const int JUNC_AUTO_OPT_COUNT  = 3;
static const char* const JUNC_AUTO_OPT_NAMES[] = { "Attack", "Magic", "Defense" };
// Help (read on "/", like the GF/Ability menus): the game's help-bar text per option.
static const char* const JUNC_AUTO_OPT_HELP[] = {
    "Junctions magic to raise Strength.",
    "Junctions magic to raise Magic.",
    "Junctions magic to raise HP."
};
static uint8_t s_juncPrevAutoCursor = 0xFF;     // previous Auto submenu cursor
static bool    s_juncAutoConfirmPending = false; // Auto option confirmed; speak confirmation when the action menu settles
static uint8_t s_juncAutoConfirmOpt     = 0xFF;  // which Auto option (0/1/2) was confirmed
// v0.18.2.6: apply-detection via the auto-junction routine itself. Confirm and
// cancel of the Auto submenu are byte-identical in the focus path (11 -> 8 -> 3),
// so apply/cancel can't be read from menu state. BAT (2026-06-02) proved the game
// routine at 0x004BE790 that rewrites the working junction array runs on a CONFIRM
// (even a no-op confirm that changes nothing) and does NOT run on a CANCEL. The HW
// write-BP below sets this flag from inside that routine while the Auto submenu is
// focused (+0x22E==11); the action-menu resolution reads it: set => confirm
// (announce), clear => cancel (silent). Replaces the v0.18.2.3 magic-changed
// snapshot, which was silent on no-op confirms (Aaron: "reads as broken").
static volatile bool s_juncAutoRoutineRan = false;


// ============================================================================
// v0.18.2.6 — auto-junction CONFIRM detector (HW write BP on the working byte)
// ============================================================================
// v0.18.2.4 BAT proved the menu module does NOT update pEngineInputConfirmedButtons
// (zero [JuncBtnDiag] lines across a full Auto session), so the engine button
// bitmask can't tell us a confirm happened. Instead we find the routine that
// rearranges the magic: on a confirm it writes the menu's working junction array
// at pMenuStateA+0x6C2 (BAT log: 0x6C2 0->18, 0x6C4 32->0). A 1-byte hardware
// WRITE breakpoint on that fixed address fires INSIDE the auto-junction routine.
// We log EIP + registers + FF8-.text stack return addresses; the return addresses
// sets s_juncAutoRoutineRan when it fires while the Auto submenu is focused. DR3
// is used (DR0/1/2 are the battle BPs); each VEH checks its own DR6 bit so they
// coexist. Armed on Junction-menu activation, dropped on Junction reset. This is
// now load-bearing (the apply/cancel signal), not a throwaway diagnostic.
static const int      JUNC_AUTO_WORK_OFF = 0x6C2;   // working junction byte the routine writes
static volatile bool  s_juncAutoBPArmed  = false;
static PVOID          s_juncAutoBPVEH    = nullptr;
static uint32_t       s_juncAutoBPTarget = 0;       // resolved pMenuStateA + 0x6C2

static LONG CALLBACK JuncAutoBP_VEH(PEXCEPTION_POINTERS pEx)
{
    if (pEx->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    if (!((DWORD)pEx->ContextRecord->Dr6 & 0x08))  // DR3 condition bit — not our hit
        return EXCEPTION_CONTINUE_SEARCH;
    pEx->ContextRecord->Dr6 &= ~0x0F;  // acknowledge

    // The write fires from inside the game's auto-junction routine (0x004BE790),
    // which runs on a CONFIRM of the Auto submenu (including a no-op confirm) and
    // NOT on a cancel. Gate on the Auto submenu being focused (+0x22E==11) so the
    // same byte's menu-load / manual-junction writes don't trip the flag.
    __try {
        if (pMenuStateA && ((uint8_t*)pMenuStateA)[0x22E] == 11)
            s_juncAutoRoutineRan = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void JuncAutoBP_SetAllThreads(bool arm)
{
    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    int done = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                FALSE, te.th32ThreadID);
            if (!h) continue;
            bool isSelf = (te.th32ThreadID == myTid);
            if (!isSelf) SuspendThread(h);
            CONTEXT ctx; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                if (arm) {
                    ctx.Dr3 = s_juncAutoBPTarget;
                    ctx.Dr7 &= ~((DWORD)0x40 | ((DWORD)0x0F << 28)); // clear L3 + RW3 + LEN3
                    ctx.Dr7 |= (DWORD)0x40;                          // L3 local enable (bit 6)
                    ctx.Dr7 |= ((DWORD)0x01 << 28);                  // RW3 = 01 (write-only)
                    // LEN3 = 00 (1 byte)
                } else {
                    ctx.Dr3 = 0;
                    ctx.Dr7 &= ~((DWORD)0x40 | ((DWORD)0x0F << 28));
                }
                if (SetThreadContext(h, &ctx)) done++;
            }
            if (!isSelf) ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    Log::Menu("[JuncAutoBP] %s DR3 on 0x%08X (1-byte write) — threads=%d",
               arm ? "armed" : "disarmed", s_juncAutoBPTarget, done);
}

static void JuncAutoBP_Arm()
{
    if (s_juncAutoBPArmed) return;
    if (!pMenuStateA) return;
    if (!s_juncAutoBPVEH) {
        s_juncAutoBPVEH = AddVectoredExceptionHandler(1, JuncAutoBP_VEH);
        Log::Menu("[JuncAutoBP] VEH registered: 0x%08X", (uint32_t)(uintptr_t)s_juncAutoBPVEH);
    }
    s_juncAutoBPTarget = (uint32_t)(uintptr_t)((uint8_t*)pMenuStateA + JUNC_AUTO_WORK_OFF);
    JuncAutoBP_SetAllThreads(true);
    s_juncAutoBPArmed = true;
}

static void JuncAutoBP_Disarm()
{
    if (!s_juncAutoBPArmed) return;
    JuncAutoBP_SetAllThreads(false);
    s_juncAutoBPArmed = false;
}

static void ResetJunctionState()
{
    JuncAutoBP_Disarm();  // v0.18.2.6: drop the HW write BP when the Junction state resets
    s_juncActive = false;
    s_juncPrevCharCursor = 0xFF;
    s_juncPrevFocus = 0xFF;
    s_juncPrevActionCursor = 0xFF;
    s_juncCharSelectAnnounced = false;
    s_juncPrevSuboptCursor = 0xFF;
    s_juncPrevGfListCursor = 0xFF;
    s_juncPrevGfToggle = 0xFF;
    s_juncPrevAbilLeftCursor = 0xFF;
    s_juncPrevAbilRightCursor = 0xFF;
    s_juncPrevAbilFocus = 0xFF;
    s_abilRightListCount = 0;
    s_abilLastLeftCursor = 0;
    memset(s_juncCachedGfMasks, 0, sizeof(s_juncCachedGfMasks));
    s_juncSelectedCharIdx = 0xFF;
    s_juncPrevAutoCursor = 0xFF;
    s_juncAutoConfirmPending = false;
    s_juncAutoConfirmOpt     = 0xFF;
    s_juncAutoRoutineRan     = false;
}

// Compute character level from EXP. FF8: each level needs 1000 EXP flat.
static int ComputeCharLevel(uint32_t exp)
{
    int lvl = (int)(exp / 1000) + 1;
    if (lvl > 100) lvl = 100;
    return lvl;
}

// SEH-safe: Announce a party member for Junction character select.
// Uses inline literal addresses to avoid forward reference issues.
// savemap=0x1CFDC5C, chars at +0x48C (8×0x98), compStats at 0x1CFF000 (3×0x1D0)
//
// v0.09.41: The cursor at +0x1E9 indexes directly into the formation array
// at savemap+0xAF0. The engine places characters in the visual slot positions:
//   1 member  → formation=[FF, charIdx, FF, FF] (middle slot)
//   2 members → formation=[charA, charB, FF, FF] (top + middle)
//   3 members → formation=[charA, charB, charC, FF] (all three)
// So formation[cursorPos] gives the character at the cursor's visual slot,
// or 0xFF for an empty slot. No compaction or centering formula needed.
static void AnnounceJuncCharSelect(uint8_t cursorPos)
{
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;  // SAVEMAP_BASE
        // Party formation at +0xAF0: 4 bytes, char index 0-7 or 0xFF.
        // Cursor indexes directly into this array — engine handles centering.
        uint8_t* party = sm + 0xAF0;
        
        if (cursorPos > 2) {
            Log::Menu("[JuncTTS] CharSelect cursor %u out of range", (unsigned)cursorPos);
            return;
        }
        
        uint8_t charIdx = party[cursorPos];
        if (charIdx == 0xFF || charIdx > 10) {
            ScreenReader::Speak("Empty", true);
            Log::Menu("[JuncTTS] CharSelect cursor %u -> empty (formation[%u]=0x%02X)",
                       (unsigned)cursorPos, (unsigned)cursorPos, (unsigned)charIdx);
            return;
        }
        // Inline name lookup (GetCharacterNameByPortrait defined later in file)
        static const char* JUNC_CHAR_NAMES[] = {
            "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea",
            "Laguna", "Kiros", "Ward"
        };
        // v0.17.8.17.7: Dream-party name fix (same model as victory screen). The
        // formation index (charIdx, e.g. [5,0,1] = Selphie/Squall/Zell) is the
        // STALE regular party during a Laguna dream; the dream character's data
        // is loaded into char-data[charIdx], whose model_id (+0x08) reads 8/9/10
        // for Laguna/Kiros/Ward. Prefer model_id when it names a dream member,
        // else fall back to the formation index (unchanged for normal play, where
        // model_id == charIdx for the 8 mains). The modelId is added to the log
        // line below so a dream BAT confirms the value without a separate diag.
        uint8_t modelId = *(sm + 0x48C + charIdx * 0x98 + 0x08);
        const char* name;
        if (modelId >= 8 && modelId <= 10) name = JUNC_CHAR_NAMES[modelId];
        else                               name = (charIdx < 11) ? JUNC_CHAR_NAMES[charIdx] : "Unknown";
        
        // Get HP from computed stats (0x1CFF000, stride 0x1D0, curHP +0x172, maxHP +0x174)
        // Map charIdx to party slot via party array
        uint16_t curHP = 0, maxHP = 0;
        for (int s = 0; s < 3; s++) {
            if (party[s] == charIdx) {
                uint8_t* cs = (uint8_t*)0x1CFF000 + s * 0x1D0;
                uint16_t csMax = *(uint16_t*)(cs + 0x174);
                if (csMax > 0 && csMax < 10000) {
                    curHP = *(uint16_t*)(cs + 0x172);
                    maxHP = csMax;
                }
                break;
            }
        }
        // Fallback: read from savemap character struct
        if (maxHP == 0 && charIdx < 8) {
            uint8_t* smChar = sm + 0x48C + charIdx * 0x98;
            curHP = *(uint16_t*)(smChar + 0x00);
            maxHP = *(uint16_t*)(smChar + 0x02);
        }
        
        // Get level from EXP (FF8: level = EXP/1000 + 1)
        uint8_t* charData = sm + 0x48C + charIdx * 0x98;
        uint32_t exp = *(uint32_t*)(charData + 0x04);
        int level = ComputeCharLevel(exp);
        
        char buf[256];
        if (maxHP > 0)
            sprintf(buf, "%s, Level %d, HP %u of %u", name, level, (unsigned)curHP, (unsigned)maxHP);
        else
            sprintf(buf, "%s, Level %d, HP %u", name, level, (unsigned)curHP);
        
        ScreenReader::Speak(buf, true);
        Log::Menu("[JuncTTS] CharSelect: %s (cursor=%u formation[%u]=%u modelId=%u)",
                   buf, (unsigned)cursorPos, (unsigned)cursorPos, (unsigned)charIdx, (unsigned)modelId);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[JuncTTS] Exception in AnnounceJuncCharSelect");
    }
}

// SEH-safe: Get GF name by index (0-15). Reads from savemap GF struct.
// GFs at savemap +0x4C, 16 × 68 bytes. Name at +0x00 (12 bytes, FF8-encoded).
// Exists flag at +0x11.
static const char* GetGfName(uint8_t gfIdx, char* nameBuf, int bufSize)
{
    if (gfIdx >= GF_COUNT) return "Unknown GF";
    
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;  // SAVEMAP_BASE
        uint8_t* gf = sm + 0x4C + gfIdx * GF_STRUCT_SIZE;
        uint8_t exists = gf[0x11];
        
        if (!exists) return GF_DEFAULT_NAMES[gfIdx];  // shouldn't appear in list, but fallback
        
        // Decode GF name from FF8 encoding.
        // GF names in live savemap use +0x20 offset (same as save files).
        // Subtract 0x20 from each non-zero byte before decoding.
        int pos = 0;
        for (int i = 0; i < 12 && pos < bufSize - 1; i++) {
            uint8_t raw = gf[i];
            uint8_t c = (raw >= 0x20) ? (raw - 0x20) : raw;
            if (c == 0x00) { nameBuf[pos++] = ' '; }
            else if (c >= 0x01 && c <= 0x0A) { nameBuf[pos++] = '0' + (c - 0x01); }
            else if (c >= 0x25 && c <= 0x3E) { nameBuf[pos++] = 'A' + (c - 0x25); }
            else if (c >= 0x3F && c <= 0x58) { nameBuf[pos++] = 'a' + (c - 0x3F); }
            else break;  // terminator or unknown
        }
        while (pos > 0 && nameBuf[pos-1] == ' ') pos--;
        nameBuf[pos] = '\0';
        
        if (pos > 0) return nameBuf;
        return GF_DEFAULT_NAMES[gfIdx];  // empty name, use default
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return GF_DEFAULT_NAMES[gfIdx];
    }
}

// Check if a GF is currently junctioned to the selected character.
// Reads character's GF bitmask from savemap character struct +0x58 (uint16).
static bool IsGfJunctioned(uint8_t gfIdx, uint8_t charIdx)
{
    if (gfIdx >= GF_COUNT || charIdx > 7) return false;
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;
        uint8_t* chr = sm + 0x48C + charIdx * 0x98;
        uint16_t gfMask = *(uint16_t*)(chr + 0x58);
        return (gfMask & (1 << gfIdx)) != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Find which character (if any) has a GF junctioned. Returns charIdx 0-7 or 0xFF if none.
static uint8_t FindGfOwner(uint8_t gfIdx)
{
    if (gfIdx >= GF_COUNT) return 0xFF;
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;
        for (int c = 0; c < 8; c++) {
            uint8_t* chr = sm + 0x48C + c * 0x98;
            uint16_t gfMask = *(uint16_t*)(chr + 0x58);
            if (gfMask & (1 << gfIdx)) return (uint8_t)c;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0xFF;
}

// Get the currently selected character index from the Junction char cursor.
// v0.09.41: Read formation[cursor] directly — cursor indexes the formation array.
static uint8_t GetJuncSelectedCharIdx()
{
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;
        uint8_t* party = sm + 0xAF0;
        uint8_t cursor = *((uint8_t*)pMenuStateA + JUNC_CHARSEL_CURSOR_OFF);
        if (cursor <= 2) {
            uint8_t charIdx = party[cursor];
            if (charIdx != 0xFF && charIdx <= 10) return charIdx;
        }
        // v0.09.49: Engine rewrites formation during Junction editing.
        // Fall back to cached charIdx from char select.
        if (s_juncSelectedCharIdx != 0xFF) return s_juncSelectedCharIdx;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0xFF;
}

// v0.09.48: Build the right panel available abilities list from GF bitmaps.
// Reads all junctioned GFs' completeAbilities[16] bitmaps, unions them,
// and filters by the relevant ability ID range based on the left panel slot type.
// Result is stored in s_abilRightList[] in ascending ID order.
//
// Left cursor 0-2 = command slot selected → show command abilities (IDs 20-38, skip 24)
// Left cursor 3-6 = ability slot selected → show character/party abilities (IDs 39-82)
static void BuildAbilityRightPanel(uint8_t charIdx, uint8_t leftCursor)
{
    s_abilRightListCount = 0;
    if (charIdx > 7) return;
    
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;
        uint8_t* chr = sm + 0x48C + charIdx * 0x98;
        uint16_t gfMask = *(uint16_t*)(chr + 0x58);  // junctioned GF bitmask (live)
        // v0.09.49: Game zeroes gfMask during Junction editing. Use per-char cached value.
        if (gfMask == 0 && charIdx < 8 && s_juncCachedGfMasks[charIdx] != 0) {
            gfMask = s_juncCachedGfMasks[charIdx];
        }
        // Ultimate fallback — if both are 0, use ALL existing GFs.
        if (gfMask == 0) {
            for (int g = 0; g < 16; g++) {
                uint8_t* gf = sm + 0x4C + g * 0x44;
                if (gf[0x11]) gfMask |= (1 << g);
            }
        }
        
        // Union all junctioned GFs' completeAbilities bitmaps (16 bytes = 128 bits)
        uint8_t unionBitmap[16] = {};
        for (int g = 0; g < 16; g++) {
            if (!(gfMask & (1 << g))) continue;  // GF not junctioned
            uint8_t* gf = sm + 0x4C + g * 0x44;  // GF struct base
            if (!gf[0x11]) continue;  // GF doesn't exist
            uint8_t* completeAbil = gf + 0x14;  // completeAbilities[16]
            for (int b = 0; b < 16; b++)
                unionBitmap[b] |= completeAbil[b];
        }
        
        // Determine ID range based on left panel slot type
        int idMin, idMax;
        if (leftCursor <= 2) {
            // Command slot selected: show command abilities (IDs 20-38)
            idMin = 20;
            idMax = 38;
        } else {
            // Ability slot selected: show character/party abilities (IDs 39-82)
            idMin = 39;
            idMax = 82;
        }
        
        // Collect all learned abilities in the relevant range, ascending ID order
        for (int id = idMin; id <= idMax; id++) {
            if (id == 24) continue;  // ID 24 = "Empty" placeholder, skip
            // Check bit 'id' in the union bitmap
            int byteIdx = id / 8;
            int bitIdx = id % 8;
            if (unionBitmap[byteIdx] & (1 << bitIdx)) {
                if (s_abilRightListCount < ABIL_RIGHT_LIST_MAX)
                    s_abilRightList[s_abilRightListCount++] = (uint8_t)id;
            }
        }
        
        Log::Menu("[AbilTTS] Built right panel list: %d abilities (leftCursor=%u range=%d-%d)",
                   s_abilRightListCount, (unsigned)leftCursor, idMin, idMax);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[AbilTTS] Exception in BuildAbilityRightPanel");
    }
}

// Main Junction submenu poller — called every frame while top-level cursor == 0
// Gate: +0x1E8==17 means Junction subsystem is active. This is set when the
// player confirms Junction from the top menu. Character select happens while
// +0x1E8==17 AND focus==0. The subsystem flag is the reliable entry indicator.
static void PollJunctionSubmenu()
{
    if (!pMenuStateA) return;
    
    __try {
        uint8_t* base = (uint8_t*)pMenuStateA;
        uint8_t juncActiveFlag = base[JUNC_ACTIVE_OFFSET];
        uint8_t focus = base[JUNC_FOCUS_OFFSET];
        
        // Detect Junction subsystem activation (+0x1E8 transitions to 17)
        if (juncActiveFlag == 17 && !s_juncActive) {
            s_juncActive = true;
            s_juncPrevCharCursor = 0xFF;  // force announce on first poll
            s_juncPrevFocus = 0xFF;
            s_juncPrevActionCursor = 0xFF;
            s_juncCharSelectAnnounced = false;
            // v0.09.49: Cache GF bitmasks for ALL characters NOW,
            // before the engine zeroes them during Junction editing.
            // The user can switch characters without leaving Junction,
            // so we need all masks upfront.
            {
                uint8_t* sm2 = (uint8_t*)0x1CFDC5C;
                for (int ci = 0; ci < 8; ci++) {
                    s_juncCachedGfMasks[ci] = *(uint16_t*)(sm2 + 0x48C + ci * 0x98 + 0x58);
                }
                Log::Menu("[JuncTTS] Cached GF bitmasks: [%04X %04X %04X %04X %04X %04X %04X %04X]",
                           (unsigned)s_juncCachedGfMasks[0], (unsigned)s_juncCachedGfMasks[1],
                           (unsigned)s_juncCachedGfMasks[2], (unsigned)s_juncCachedGfMasks[3],
                           (unsigned)s_juncCachedGfMasks[4], (unsigned)s_juncCachedGfMasks[5],
                           (unsigned)s_juncCachedGfMasks[6], (unsigned)s_juncCachedGfMasks[7]);
            }
            Log::Menu("[JuncTTS] Junction subsystem activated (+0x1E8=17)");
            JuncAutoBP_Arm();  // v0.18.2.6: arm the HW write BP that detects Auto-submenu confirms
        }
        
        // Detect Junction subsystem deactivation
        // Back-out from Junction returns to the top-level menu (Junction/Item/etc),
        // so the top-level cursor handler will announce the menu item.
        if (juncActiveFlag != 17 && s_juncActive) {
            Log::Menu("[JuncTTS] Junction subsystem deactivated (+0x1E8=%u)",
                       (unsigned)juncActiveFlag);
            ResetJunctionState();
            return;
        }
        
        if (!s_juncActive) return;
        
        // DEBUG: Log unhandled focus states
        if (focus != 0 && focus != 3 && focus != 8 && focus != 11 && focus != 37 && focus != 38 && focus != 41 &&
            !(focus >= 20 && focus <= 28)) {
            static uint8_t s_lastLoggedFocus = 0xFF;
            if (focus != s_lastLoggedFocus) {
                s_lastLoggedFocus = focus;
                Log::Menu("[JuncTTS] Unhandled focus=%u +271=%u +272=%u +275=%u",
                           (unsigned)focus, (unsigned)base[0x271], (unsigned)base[0x272], (unsigned)base[0x275]);
            }
        }
        
        // ---- Auto submenu CLOSED — resolve apply vs cancel at the action menu ----
        // Confirm and cancel both leave the Auto submenu (focus 11) via the same
        // path (11 -> char-select 8 -> action menu 3), so the focus path can't tell
        // them apart. Mark "pending" on the way out and decide at the action menu
        // (focus==3 block) from whether the auto-junction routine ran (the HW
        // write-BP flag). While pending, the
        // char-select re-announce is muted so it can't precede the confirmation.
        if (s_juncPrevFocus == JUNC_AUTO_FOCUS && (focus == 0 || focus == 8) &&
            s_juncPrevAutoCursor < JUNC_AUTO_OPT_COUNT) {
            s_juncAutoConfirmPending = true;
            s_juncAutoConfirmOpt     = s_juncPrevAutoCursor;
            Log::Menu("[JuncTTS] AutoMenu closed: opt=%u (focus %u->%u), awaiting apply check",
                       (unsigned)s_juncAutoConfirmOpt, (unsigned)s_juncPrevFocus, (unsigned)focus);
        }

        // ---- Ability Screen (focus 20-28 range) ----
        // v0.09.45: Confirmed via v0.09.44 diagnostic:
        //   LEFT panel:  focus=28, cursor at +0x271 (equipped command/ability slots)
        //   RIGHT panel: focus=24, cursor at +0x27C (available abilities from GFs)
        //   Other focus values (21, 22, 26, 27) are transitions — ignore.
        if (focus >= 20 && focus <= 28) {
            // Detect panel transitions (focus changes between 24 and 28)
            // Only re-announce when switching BETWEEN panels (24↔ 28), not on
            // first entry from outside the 20-28 range (avoids transition artifact
            // where focus passes through 28 briefly when entering from action menu).
            if (focus != s_juncPrevAbilFocus) {
                bool fromOtherPanel = (s_juncPrevAbilFocus == ABIL_LEFT_FOCUS ||
                                       s_juncPrevAbilFocus == ABIL_RIGHT_FOCUS);
                if (focus == ABIL_LEFT_FOCUS && fromOtherPanel) {
                    s_juncPrevAbilLeftCursor = 0xFF;  // force re-announce on left entry
                    Log::Menu("[AbilTTS] -> LEFT panel (focus=%u)", (unsigned)focus);
                }
                if (focus == ABIL_RIGHT_FOCUS && fromOtherPanel) {
                    s_juncPrevAbilRightCursor = 0xFF;  // force re-announce on right entry
                    // v0.09.48: Rebuild available abilities list when entering right panel
                    uint8_t charIdx = GetJuncSelectedCharIdx();
                    BuildAbilityRightPanel(charIdx, s_abilLastLeftCursor);
                    Log::Menu("[AbilTTS] -> RIGHT panel (focus=%u, leftCursor=%u)",
                               (unsigned)focus, (unsigned)s_abilLastLeftCursor);
                }
                s_juncPrevAbilFocus = focus;
            }

            // LEFT PANEL (focus=24): read equipped ability from savemap
            if (focus == ABIL_LEFT_FOCUS) {
                uint8_t cursor = base[ABIL_LEFT_CURSOR_OFF];
                if (cursor != s_juncPrevAbilLeftCursor) {
                    s_juncPrevAbilLeftCursor = cursor;
                    s_abilLastLeftCursor = cursor;  // v0.09.48: track for right panel filtering
                    __try {
                        uint8_t charIdx = GetJuncSelectedCharIdx();
                        if (charIdx <= 7) {
                            uint8_t* sm = (uint8_t*)0x1CFDC5C;
                            uint8_t* chr = sm + 0x48C + charIdx * 0x98;
                            uint8_t abilId = 0;
                            // Slot layout: cursor 0-2 = commands[0-2], cursor 3-6 = abilities[0-3]
                            if (cursor <= 2) {
                                abilId = chr[0x50 + cursor];
                            } else if (cursor >= 3 && cursor <= 6) {
                                abilId = chr[0x54 + (cursor - 3)];
                            }
                            const char* name = (abilId == 0) ? "Empty" : GetAbilityName(abilId);
                            ScreenReader::Speak(name, true);
                            Log::Menu("[AbilTTS] LEFT slot %u: id=%u -> %s",
                                       (unsigned)cursor, (unsigned)abilId, name);
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // RIGHT PANEL (focus=28): available abilities from junctioned GFs
            // v0.09.48: Index into reconstructed list built from GF completeAbilities bitmaps.
            // The list is rebuilt each time the user enters the right panel.
            if (focus == ABIL_RIGHT_FOCUS) {
                uint8_t cursor = base[ABIL_RIGHT_CURSOR_OFF];
                if (cursor != s_juncPrevAbilRightCursor) {
                    s_juncPrevAbilRightCursor = cursor;
                    // v0.09.48: Build list on demand if not yet built
                    // (handles first entry without a left→right transition)
                    if (s_abilRightListCount == 0) {
                        uint8_t charIdx = GetJuncSelectedCharIdx();
                        BuildAbilityRightPanel(charIdx, s_abilLastLeftCursor);
                    }
                    // Look up ability ID from our reconstructed list
                    if (cursor < s_abilRightListCount) {
                        uint8_t abilId = s_abilRightList[cursor];
                        const char* name = GetAbilityName(abilId);
                        ScreenReader::Speak(name, true);
                        Log::Menu("[AbilTTS] RIGHT cursor %u: id=%u -> %s",
                                   (unsigned)cursor, (unsigned)abilId, name);
                    } else {
                        // Cursor past end of list = empty slot
                        ScreenReader::Speak("Empty", true);
                        Log::Menu("[AbilTTS] RIGHT cursor %u: past list end (%d items) -> Empty",
                                   (unsigned)cursor, s_abilRightListCount);
                    }
                }
            }
        }
        
        // ---- Character Select (focus == 0 or 8) ----
        // v0.09.41: focus=8 is char select after Switch rearrangement.
        // focus=0 is the normal char select on first entry.
        if (focus == 0 || focus == 8) {
            uint8_t charCursor = base[JUNC_CHARSEL_CURSOR_OFF];
            if (s_juncAutoConfirmPending) {
                // Auto submenu just closed; this char-select hop is transient and
                // resolves at the action menu. Stay silent and keep prev in sync.
                s_juncPrevCharCursor = charCursor;
            } else if (charCursor <= 2 && charCursor != s_juncPrevCharCursor) {
                AnnounceJuncCharSelect(charCursor);
                // v0.09.49: Cache the resolved charIdx NOW, while formation is still intact.
                // The engine rewrites the formation array once Junction editing starts.
                {
                    uint8_t ci = GetJuncSelectedCharIdx();
                    if (ci != 0xFF) s_juncSelectedCharIdx = ci;
                }
                s_juncPrevCharCursor = charCursor;
                s_juncCharSelectAnnounced = true;
            }
            // Reset action menu state when back on char select
            s_juncPrevActionCursor = 0xFF;
        }
        
        // ---- Action Menu (focus == 3) ----
        if (focus == 3) {
            uint8_t actionCursor = base[JUNC_ACTION_CURSOR_OFF];
            bool spokeConfirm = false;

            // Resolve a just-closed Auto submenu. The HW write-BP set
            // s_juncAutoRoutineRan iff the game's auto-junction routine ran while
            // the submenu was focused, which happens on a CONFIRM (even a no-op
            // confirm) and never on a cancel. Set => speak the confirmation (and
            // suppress the action re-announce). Clear => it was a cancel; say
            // nothing here and let the normal action announce run below.
            if (s_juncAutoConfirmPending) {
                bool applied = s_juncAutoRoutineRan;
                if (applied) {
                    const char* opt = (s_juncAutoConfirmOpt < JUNC_AUTO_OPT_COUNT)
                                      ? JUNC_AUTO_OPT_NAMES[s_juncAutoConfirmOpt] : "";
                    char abuf[64];
                    sprintf(abuf, "Junctioned automatically for %s", opt);
                    ScreenReader::Speak(abuf, true);
                    Log::Menu("[JuncTTS] AutoApplied: %s (confirm detected)", opt);
                    s_juncPrevActionCursor = actionCursor;  // suppress the action re-announce
                    spokeConfirm = true;
                } else {
                    Log::Menu("[JuncTTS] AutoMenu cancelled (auto-junction routine "
                              "did not run) opt=%u",
                              (unsigned)s_juncAutoConfirmOpt);
                }
                s_juncAutoConfirmPending = false;
                s_juncAutoConfirmOpt     = 0xFF;
                s_juncAutoRoutineRan     = false;
            }

            // Announce on entry to action menu OR cursor change
            if (!spokeConfirm && actionCursor < JUNC_ACTION_COUNT) {
                if (s_juncPrevFocus != 3 || actionCursor != s_juncPrevActionCursor) {
                    const char* actionName = JUNC_ACTION_NAMES[actionCursor];
                    ScreenReader::Speak(actionName, true);
                    Log::Menu("[JuncTTS] ActionMenu: %s (cursor=%u)",
                               actionName, (unsigned)actionCursor);
                    s_juncPrevActionCursor = actionCursor;
                }
            }
            // Reset other cursors when in action menu
            s_juncPrevCharCursor = 0xFF;
            s_juncPrevSuboptCursor = 0xFF;
            s_juncPrevGfListCursor = 0xFF;
            // v0.09.48: Reset Ability screen state so re-entry announces correctly
            s_juncPrevAbilLeftCursor = 0xFF;
            s_juncPrevAbilRightCursor = 0xFF;
            s_juncPrevAbilFocus = 0xFF;
            s_abilRightListCount = 0;
        }

        // ---- Auto-junction submenu (focus == 11) ----
        // Confirming "Auto" from the action menu opens a 3-option submenu
        // (Atk/Mag/Def) selected by +0x26A; focus stays 11 throughout. Announce
        // the option + what it optimizes on entry and on cursor change.
        if (focus == JUNC_AUTO_FOCUS) {
            // On entry, clear the apply flag so the action-menu resolution can
            // tell this Auto session's confirm (write-BP sets it) from a cancel.
            if (s_juncPrevFocus != JUNC_AUTO_FOCUS) {
                s_juncAutoRoutineRan = false;  // fresh apply-window; the write-BP sets it on a confirm
            }
            uint8_t autoCursor = base[JUNC_AUTO_CURSOR_OFF];
            if (autoCursor < JUNC_AUTO_OPT_COUNT &&
                (s_juncPrevFocus != JUNC_AUTO_FOCUS || autoCursor != s_juncPrevAutoCursor)) {
                ScreenReader::Speak(JUNC_AUTO_OPT_NAMES[autoCursor], true);
                Log::Menu("[JuncTTS] AutoMenu: %s (cursor=%u)",
                           JUNC_AUTO_OPT_NAMES[autoCursor], (unsigned)autoCursor);
                s_juncPrevAutoCursor = autoCursor;
            }
        }
        
        // ---- Junction Sub-option (focus == 37 or 38) ----
        // Player chose "Junction" from action menu, now picks GF or Magic
        if (focus == 37 || focus == 38) {
            uint8_t suboptCursor = base[JUNC_SUBOPTION_CURSOR_OFF];
            if (suboptCursor <= 1 && suboptCursor != s_juncPrevSuboptCursor) {
                ScreenReader::Speak(JUNC_SUBOPTION_NAMES[suboptCursor], true);
                Log::Menu("[JuncTTS] SubOption: %s (cursor=%u focus=%u)",
                           JUNC_SUBOPTION_NAMES[suboptCursor], (unsigned)suboptCursor, (unsigned)focus);
                s_juncPrevSuboptCursor = suboptCursor;
            }
            // Reset GF list cursor for fresh entry
            s_juncPrevGfListCursor = 0xFF;
            s_juncPrevGfToggle = 0xFF;
        }
        
        // ---- GF List (focus == 41) ----
        // Player is browsing the GF list to junction/unjunction.
        // +0x26D cursor indexes into OBTAINED GFs only (not all 16).
        // Must build obtained-GF list and map cursor to real GF index.
        if (focus == 41) {
            uint8_t gfCursor = base[JUNC_GF_LIST_CURSOR_OFF];
            uint8_t gfToggle = base[JUNC_GF_TOGGLE_OFF];
            
            // Build list of obtained GF indices
            // Diagnostic: dump exists flags for all 16 GFs on first entry
            static bool s_gfDiagDone = false;
            uint8_t obtainedGfs[GF_COUNT];
            int obtainedCount = 0;
            __try {
                uint8_t* sm = (uint8_t*)0x1CFDC5C;
                if (!s_gfDiagDone) {
                    s_gfDiagDone = true;
                    char diag[512] = {};
                    int dp = 0;
                    dp += sprintf(diag + dp, "[JuncTTS] GF exists dump: ");
                    for (int g = 0; g < GF_COUNT && dp < 480; g++) {
                        uint8_t* gfs = sm + 0x4C + g * GF_STRUCT_SIZE;
                        dp += sprintf(diag + dp, "%d:[%02X,%02X,%02X,%02X] ",
                                      g, gfs[0x10], gfs[0x11], gfs[0x12], gfs[0x13]);
                    }
                    Log::Menu("%s", diag);
                }
                for (int g = 0; g < GF_COUNT; g++) {
                    uint8_t* gfStruct = sm + 0x4C + g * GF_STRUCT_SIZE;
                    if (gfStruct[0x11] != 0)  // exists flag
                        obtainedGfs[obtainedCount++] = (uint8_t)g;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            
            // Map cursor to real GF index
            uint8_t realGfIdx = (gfCursor < obtainedCount) ? obtainedGfs[gfCursor] : 0xFF;
            
            if (realGfIdx < GF_COUNT && gfCursor != s_juncPrevGfListCursor) {
                char nameBuf[32];
                const char* gfName = GetGfName(realGfIdx, nameBuf, sizeof(nameBuf));
                
                // Check who owns this GF
                static const char* JUNC_CHAR_NAMES2[] = {
                    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea",
                    "Laguna", "Kiros", "Ward"
                };
                uint8_t owner = FindGfOwner(realGfIdx);
                uint8_t selChar = GetJuncSelectedCharIdx();
                // v0.17.8.17.7: dream-aware owner name. owner is a regular char
                // index (0-7) from FindGfOwner; during a dream the dream member's
                // struct is loaded into char-data[owner] and its model_id (+0x08)
                // reads 8/9/10. Map through that so "on <name>" says Laguna/Kiros/
                // Ward, not the stale regular name. (Literal savemap addr used
                // because the shared resolver is defined in a later include.)
                uint8_t ownerName = owner;
                if (owner < 11) {
                    __try {
                        uint8_t m = *((uint8_t*)0x1CFDC5C + 0x48C + owner * 0x98 + 0x08);
                        if (m >= 8 && m <= 10) ownerName = m;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
                
                char buf[128];
                if (owner != 0xFF && owner == selChar) {
                    sprintf(buf, "%s, junctioned", gfName);
                } else if (owner != 0xFF && owner < 11) {
                    sprintf(buf, "%s, on %s", gfName, JUNC_CHAR_NAMES2[ownerName]);
                } else {
                    sprintf(buf, "%s", gfName);
                }
                
                ScreenReader::Speak(buf, true);
                Log::Menu("[JuncTTS] GFList: %s (cursor=%u realIdx=%u owner=%u selChar=%u)",
                           buf, (unsigned)gfCursor, (unsigned)realGfIdx, (unsigned)owner, (unsigned)selChar);
                s_juncPrevGfListCursor = gfCursor;
                s_juncPrevGfToggle = gfToggle;
            }
            // Detect toggle change (junction/unjunction) on same GF
            else if (realGfIdx < GF_COUNT && gfToggle != s_juncPrevGfToggle && s_juncPrevGfToggle != 0xFF) {
                char nameBuf[32];
                const char* gfName = GetGfName(realGfIdx, nameBuf, sizeof(nameBuf));
                const char* action = (gfToggle == 1) ? "junctioned" : "removed";
                
                char buf[128];
                sprintf(buf, "%s %s", gfName, action);
                ScreenReader::Speak(buf, true);
                Log::Menu("[JuncTTS] GFToggle: %s (toggle %u->%u)",
                           buf, (unsigned)s_juncPrevGfToggle, (unsigned)gfToggle);
                s_juncPrevGfToggle = gfToggle;
            }
        }
        
        // When returning to char select (focus==0 or 8) after being deeper,
        // force re-announce the current character
        if ((focus == 0 || focus == 8) && s_juncPrevFocus != 0 && s_juncPrevFocus != 8 && s_juncPrevFocus != 0xFF) {
            if (!s_juncAutoConfirmPending) s_juncPrevCharCursor = 0xFF;
        }
        
        // Reset sub-phase cursors when leaving those phases
        if (focus != 37 && focus != 38 && s_juncPrevFocus >= 37 && s_juncPrevFocus <= 38) {
            s_juncPrevSuboptCursor = 0xFF;
        }
        if (focus != 41 && s_juncPrevFocus == 41) {
            s_juncPrevGfListCursor = 0xFF;
            s_juncPrevGfToggle = 0xFF;
        }
        if (focus != JUNC_AUTO_FOCUS && s_juncPrevFocus == JUNC_AUTO_FOCUS) {
            s_juncPrevAutoCursor = 0xFF;
        }
        
        // Track focus changes for transition detection
        s_juncPrevFocus = focus;
        
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[JuncTTS] Exception in PollJunctionSubmenu");
    }
}

// (/) On-demand help for the Junction Auto submenu option under the cursor.
// Returns true (and speaks the help) ONLY while the Auto submenu is up, so the
// "/" dispatch chain in MenuTTS::Update() falls through to the normal help bar
// everywhere else. Mirrors GFSpeakSelectedAbilityHelp()/AbilitySpeakSelectedHelp().
// Reads focus/cursor live (the / hotkey fires earlier in the frame than
// PollJunctionSubmenu, so the stashed prev-cursor could lag). SEH-safe.
static bool JunctionAutoSpeakHelp()
{
    if (!s_juncActive || !pMenuStateA) return false;
    __try {
        uint8_t* base = (uint8_t*)pMenuStateA;
        if (base[JUNC_ACTIVE_OFFSET] != 17) return false;
        if (base[JUNC_FOCUS_OFFSET] != JUNC_AUTO_FOCUS) return false;
        uint8_t cur = base[JUNC_AUTO_CURSOR_OFF];
        if (cur >= JUNC_AUTO_OPT_COUNT) return false;
        ScreenReader::Speak(JUNC_AUTO_OPT_HELP[cur], true);
        Log::Menu("[JuncTTS] AutoHelp (/): cursor=%u \"%s\"", (unsigned)cur, JUNC_AUTO_OPT_HELP[cur]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// F12 diagnostic: track menu_draw_text / get_character_width call counts
static bool     s_diagActive = false;
static DWORD    s_diagLastLogTime = 0;
static int      s_diagScanCount = 0;
static const int DIAG_SCAN_MAX = 60;  // 60 x 500ms = 30 seconds
static LONG     s_diagPrevMDT = 0;   // previous menu_draw_text count
static LONG     s_diagPrevGCW = 0;   // previous get_character_width count

// ============================================================================
