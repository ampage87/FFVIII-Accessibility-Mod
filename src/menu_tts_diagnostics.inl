// menu_tts_diagnostics.inl — Menu diagnostics, memory monitor, auto-monitor (SUBMON)
// Included from menu_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

static void PollMenuCursor()
{
    uint8_t* base = (uint8_t*)pMenuStateA;
    
    __try {
        uint8_t cursor = *(base + CURSOR_OFFSET);
        
        if (cursor != s_prevCursor) {
            const char* name = GetMenuItemName(cursor);
            if (name) {
                ScreenReader::Speak(name, true);
                Log::Menu("[MenuTTS] Cursor %u -> %u: %s",
                           (unsigned)s_prevCursor, (unsigned)cursor, name);
            } else {
                Log::Menu("[MenuTTS] Cursor %u -> %u (unknown index)",
                           (unsigned)s_prevCursor, (unsigned)cursor);
            }
            
            s_prevCursor = cursor;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.08.22: Capture GCW text periodically in menu mode to find help text
static DWORD s_lastMenuGcwCapture = 0;
static std::string s_lastMenuGcwText;

static void CaptureMenuGcwText()
{
    DWORD now = GetTickCount();
    if (now - s_lastMenuGcwCapture < 500) return;  // every 500ms
    s_lastMenuGcwCapture = now;
    
    uint8_t gcwBuf[2048];
    int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
    if (gcwLen > 0) {
        std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
        if (!decoded.empty() && decoded != s_lastMenuGcwText) {
            Log::Menu("[MenuGCW] cursor=%u text(%d): \"%s\"",
                       (unsigned)s_prevCursor, gcwLen, decoded.c_str());
            s_lastMenuGcwText = decoded;
        }
    }
}

// SEH-protected diagnostic logger for save state during block selection
static void LogSaveDiagState()
{
    __try {
        uint8_t e8 = *((uint8_t*)pMenuStateA + 0x1E8);
        uint8_t c276 = *((uint8_t*)pMenuStateA + 0x276);
        Log::Menu("[MenuTTS] SaveDiag: +0x1E8=%u +0x276=%u prevSlot=%u",
                   (unsigned)e8, (unsigned)c276, (unsigned)s_prevSaveSlotCursor);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Log key offsets while save subsystem is active, to find block selection signal
static uint8_t s_saveDiagSnap[8] = {};
static bool    s_saveDiagSnapValid = false;

static void LogSaveSubsystemChanges()
{
    __try {
        // Candidate offsets that changed during save flow in v0.07.36 scan
        static const int offsets[] = { 0x1E8, 0x230, 0x24A, 0x24B, 0x266, 0x268, 0x276, 0x22E };
        static const int N = 8;
        uint8_t cur[8];
        for (int i = 0; i < N; i++)
            cur[i] = *((uint8_t*)pMenuStateA + offsets[i]);
        
        if (!s_saveDiagSnapValid) {
            memcpy(s_saveDiagSnap, cur, N);
            s_saveDiagSnapValid = true;
            Log::Menu("[SaveActive] init: 1E8=%u 230=%u 24A=%u 24B=%u 266=%u 268=%u 276=%u 22E=%u",
                       cur[0],cur[1],cur[2],cur[3],cur[4],cur[5],cur[6],cur[7]);
            return;
        }
        
        for (int i = 0; i < N; i++) {
            if (cur[i] != s_saveDiagSnap[i]) {
                Log::Menu("[SaveActive] +0x%03X: %u -> %u",
                           offsets[i], (unsigned)s_saveDiagSnap[i], (unsigned)cur[i]);
                s_saveDiagSnap[i] = cur[i];
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// v0.08.17: Menu data diagnostic — dump character stats, scan for Gil/time
// ============================================================================
// Known addresses from FFNx:
//   character_data_1CFE74C: base of 8 × savemap_ff8_character (152 bytes each)
//   field_vars_stack_1CFE9B8: varblock base (save offset 0xD10)
// Character struct layout (savemap_ff8_character from save_data.h):
//   +0x00: uint16 current_hp
//   +0x02: uint16 max_hp
//   +0x04: uint32 exp
//   +0x08: uint8 model_id (0=Squall,1=Zell,2=Irvine,3=Quistis,4=Rinoa,5=Selphie,6=Seifer,7=Edea)
//   +0x09: uint8 weapon_id
//   +0x0A-0x0F: str,vit,mag,spr,spd,lck
//   +0x94: uint8 exists (non-zero if character exists)
//   +0x96: uint8 status
static const uint32_t CHAR_DATA_BASE = 0x1CFE74C;  // hardcoded Steam 2013 en-US
static const int CHAR_STRUCT_SIZE = 0x98;  // 152 bytes per character
static const int CHAR_COUNT = 8;
static const char* CHAR_NAMES[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea"
};

// v0.08.18: Confirmed savemap base from v0.08.17 diagnostic.
// Gil=5000 at 0x1CFDC64, time=619 at 0x1CFDC68.
// savemap_ff8_header.gil is at header+0x08, so header starts at 0x1CFDC5C.
// Characters[0].current_hp=486 at 0x1CFE0E8, characters[0].exp=6500 at 0x1CFE0EC.
static const uint32_t SAVEMAP_BASE = 0x1CFDC5C;

// savemap_ff8_header offsets (from save_data.h)
static const int HDR_LOCATION_ID   = 0x00;  // uint16
static const int HDR_CHAR1_CURR_HP = 0x02;  // uint16
static const int HDR_CHAR1_MAX_HP  = 0x04;  // uint16
static const int HDR_SAVE_COUNT    = 0x06;  // uint16
static const int HDR_GIL           = 0x08;  // uint32
static const int HDR_PLAYED_TIME   = 0x0C;  // uint32 (seconds)
static const int HDR_CHAR1_LVL     = 0x10;  // uint8
static const int HDR_PORTRAITS     = 0x11;  // uint8[3] (char1, char2, char3)
static const int HDR_SQUALL_NAME   = 0x14;  // uint8[12] FF8-encoded
static const int HDR_RINOA_NAME    = 0x20;  // uint8[12]
static const int HDR_ANGELO_NAME   = 0x2C;  // uint8[12]
static const int HDR_BOKO_NAME     = 0x38;  // uint8[12]
static const int HDR_CURR_DISK     = 0x44;  // uint32
static const int HDR_CURR_SAVE     = 0x48;  // uint32
static const int HDR_SIZE          = 0x4C;

// After header: 16 GFs at 68 bytes each = 0x440 bytes
static const int GF_SECTION_SIZE   = 16 * 68;  // 0x440
// Characters start at header + HDR_SIZE + GF_SECTION_SIZE = 0x4C + 0x440 = 0x48C
static const int CHARS_OFFSET      = HDR_SIZE + GF_SECTION_SIZE;  // 0x48C from savemap base

// savemap_ff8_character offsets (from save_data.h)
static const int CHR_CURR_HP   = 0x00;  // uint16
static const int CHR_MAX_HP    = 0x02;  // uint16
static const int CHR_EXP       = 0x04;  // uint32
static const int CHR_MODEL_ID  = 0x08;  // uint8
static const int CHR_WEAPON_ID = 0x09;  // uint8
static const int CHR_STR       = 0x0A;  // uint8
static const int CHR_VIT       = 0x0B;  // uint8
static const int CHR_MAG       = 0x0C;  // uint8
static const int CHR_SPR       = 0x0D;  // uint8
static const int CHR_SPD       = 0x0E;  // uint8
static const int CHR_LCK       = 0x0F;  // uint8
static const int CHR_EXISTS    = 0x94;  // uint8
static const int CHR_STATUS    = 0x96;  // uint8

// ============================================================================
// v0.17.8.17.7: Dream-aware character identity resolver (SHARED)
// ----------------------------------------------------------------------------
// THE recurring Laguna-dream bug: a subsystem has a party FORMATION/character
// index and names the character by indexing a name table with it. During a
// dream the savemap party formation holds the STALE regular party (e.g.
// [5,0,1] = Selphie/Squall/Zell), so that naming is wrong -- even though the
// DATA is right, because the engine loads the dream character's struct into
// char-data[idx]. That struct's model_id (+0x08) reads 8/9/10 for
// Laguna/Kiros/Ward.
//
// This helper converts a formation/character index into the correct id to feed
// the existing id->name mappers: it returns the model_id when that identifies
// a dream member (8/9/10), else the original index (identical to normal play,
// where model_id == idx for the 8 main characters -> zero regression).
//
// Route EVERY "formation index -> name" lookup through this. New code that
// needs a party member's name from an index should call this first, then
// GetCharacterNameByPortrait() (which already maps 0..10).
// ============================================================================
static uint8_t ResolveDreamAwareCharId(uint8_t charIdx)
{
    if (charIdx > 10) return charIdx;
    __try {
        uint8_t modelId = *((uint8_t*)SAVEMAP_BASE + CHARS_OFFSET
                            + charIdx * CHAR_STRUCT_SIZE + CHR_MODEL_ID);
        if (modelId >= 8 && modelId <= 10) return modelId;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return charIdx;
}

// ============================================================================
// v0.18.3.34 (#65): Switch submenu discovery diagnostic. LOG-ONLY, no speech.
// ----------------------------------------------------------------------------
// Goal: find Switch's subsystem value (+0x1E8), its focus/phase byte, the
// two-phase source/destination cursor offset(s), the active/reserve grouping,
// and what a completed swap writes to the party arrays. We don't yet know the
// Switch cursor offset, so instead of watching a hand-picked set we delta-
// monitor two contiguous bands of the pMenuStateA menu-state region that
// contain every known menu cursor (roster +0x1DB, char cursor +0x1E9, top
// cursor +0x1E6, subsystem +0x1E8, focus +0x22E, the Rearrange slot cursors
// +0x1D6/+0x1D7, and the sub-list cursors used by Item/Junction/Status), plus
// both candidate savemap party arrays (mod's 3-byte +0xAF1 and the research
// 4-byte +0x0B04). Gated to the Switch command (top cursor == 6) by the caller,
// so other submenus never trigger it. No sighted step: Aaron just navigates
// Switch by the game's cursor-move sounds; every byte that moves is logged.
// Off for ship; gate, don't delete.
// ============================================================================
#define SWITCH_DISCOVERY_DIAG 0

#if SWITCH_DISCOVERY_DIAG
static const int  SWDIAG_A0 = 0x1D0; static const int SWDIAG_AN = 0x58;  // band A: 0x1D0..0x227
static const int  SWDIAG_B0 = 0x228; static const int SWDIAG_BN = 0x68;  // band B: 0x228..0x28F (contiguous with A)
static bool       s_swDiagArmed = false;
static uint8_t    s_swBandA[SWDIAG_AN];
static uint8_t    s_swBandB[SWDIAG_BN];
static uint8_t    s_swParty[7];
static DWORD      s_swLastDiag = 0;

static void ResetSwitchDiscoveryDiag() { s_swDiagArmed = false; }

static void PollSwitchDiscoveryDiag()
{
    DWORD now = GetTickCount();
    if (now - s_swLastDiag < 100) return;   // 10 Hz is plenty for cursor moves
    s_swLastDiag = now;
    __try {
        uint8_t* pmd = (uint8_t*)pMenuStateA;
        uint8_t* sm  = (uint8_t*)SAVEMAP_BASE;
        uint8_t party[7] = { sm[0xAF1], sm[0xAF2], sm[0xAF3],
                             sm[0xB04], sm[0xB05], sm[0xB06], sm[0xB07] };

        if (!s_swDiagArmed) {
            s_swDiagArmed = true;
            for (int i = 0; i < SWDIAG_AN; i++) s_swBandA[i] = pmd[SWDIAG_A0 + i];
            for (int i = 0; i < SWDIAG_BN; i++) s_swBandB[i] = pmd[SWDIAG_B0 + i];
            memcpy(s_swParty, party, 7);
            Log::Menu("[SwitchDiag] === ENTER Switch (baseline) ===");
            Log::Menu("[SwitchDiag] 1E6=%u 1E8=%u 1E9=%u 22E=%u 1B6=%u 1D6=%u 1D7=%u 1EC=%u",
                       pmd[0x1E6], pmd[0x1E8], pmd[0x1E9], pmd[0x22E],
                       pmd[0x1B6], pmd[0x1D6], pmd[0x1D7], pmd[0x1EC]);
            // Roster at +0x1DB (all members incl. reserves, 0xFF-terminated)
            char rb[200]; int p = 0;
            for (int i = 0; i < 12; i++) {
                uint8_t v = pmd[0x1DB + i];
                const char* nm = (v < 8) ? CHAR_NAMES[v] : ((v == 0xFF) ? "END" : "?");
                if (p < 180) p += sprintf(rb + p, "%u(%s) ", (unsigned)v, nm);
                if (v == 0xFF) break;
            }
            Log::Menu("[SwitchDiag] roster +0x1DB: %s", rb);
            Log::Menu("[SwitchDiag] party +0xAF1=(%u,%u,%u) +0x0B04=(%u,%u,%u,%u)",
                       party[0], party[1], party[2],
                       party[3], party[4], party[5], party[6]);
            return;
        }

        for (int i = 0; i < SWDIAG_AN; i++)
            if (pmd[SWDIAG_A0 + i] != s_swBandA[i]) {
                Log::Menu("[SwitchDiag] +0x%03X: %u -> %u",
                           SWDIAG_A0 + i, (unsigned)s_swBandA[i], (unsigned)pmd[SWDIAG_A0 + i]);
                s_swBandA[i] = pmd[SWDIAG_A0 + i];
            }
        for (int i = 0; i < SWDIAG_BN; i++)
            if (pmd[SWDIAG_B0 + i] != s_swBandB[i]) {
                Log::Menu("[SwitchDiag] +0x%03X: %u -> %u",
                           SWDIAG_B0 + i, (unsigned)s_swBandB[i], (unsigned)pmd[SWDIAG_B0 + i]);
                s_swBandB[i] = pmd[SWDIAG_B0 + i];
            }
        static const char* PN[7] = { "AF1","AF2","AF3","B04","B05","B06","B07" };
        for (int i = 0; i < 7; i++)
            if (party[i] != s_swParty[i]) {
                Log::Menu("[SwitchDiag] party[+0x%s]: %u -> %u",
                           PN[i], (unsigned)s_swParty[i], (unsigned)party[i]);
                s_swParty[i] = party[i];
            }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
#endif // SWITCH_DISCOVERY_DIAG

// ============================================================================
// v0.08.27: Savemap offset verification — compare current offsets vs deep research
// ============================================================================
static void VerifySavemapOffsets()
{
    Log::Menu("[OFFSET-VERIFY] === Savemap Offset Verification (v0.08.27) ===");
    Log::Menu("[OFFSET-VERIFY] SAVEMAP_BASE = 0x%08X", SAVEMAP_BASE);

    uint8_t* sm = (uint8_t*)SAVEMAP_BASE;

    // --- Gil comparison ---
    uint32_t gilOurs     = *(uint32_t*)(sm + 0x08);    // our current: header+0x08
    uint32_t gilResearch = *(uint32_t*)(sm + 0x0B1C);  // research: gameplay Gil
    Log::Menu("[OFFSET-VERIFY] GIL: ours(+0x08)=%u  research(+0x0B1C)=%u  %s",
               gilOurs, gilResearch,
               (gilOurs == gilResearch) ? "MATCH" : "DIFFER");

    // --- Play time comparison ---
    uint32_t timeOurs     = *(uint32_t*)(sm + 0x0C);    // our current: header+0x0C (stale)
    uint32_t timeResearch = *(uint32_t*)(sm + 0x0CE0);  // research: live game time
    Log::Menu("[OFFSET-VERIFY] TIME: ours(+0x0C)=%u sec  research(+0x0CE0)=%u sec  %s",
               timeOurs, timeResearch,
               (timeOurs == timeResearch) ? "MATCH" : "DIFFER");

    // --- Party composition ---
    uint8_t partyOurs[3], partyResearch[4];
    partyOurs[0] = *(sm + 0xAF1);
    partyOurs[1] = *(sm + 0xAF2);
    partyOurs[2] = *(sm + 0xAF3);
    partyResearch[0] = *(sm + 0x0B04);
    partyResearch[1] = *(sm + 0x0B05);
    partyResearch[2] = *(sm + 0x0B06);
    partyResearch[3] = *(sm + 0x0B07);
    Log::Menu("[OFFSET-VERIFY] PARTY: ours(+0xAF1)=(%u,%u,%u)  research(+0x0B04)=(%u,%u,%u,%u)",
               partyOurs[0], partyOurs[1], partyOurs[2],
               partyResearch[0], partyResearch[1], partyResearch[2], partyResearch[3]);

    // --- Character[0] (Squall) comparison ---
    // Our offset: savemap + 0x48C
    // Research offset: savemap + 0x4A0
    uint8_t* charOurs     = sm + 0x48C;
    uint8_t* charResearch = sm + 0x4A0;
    uint16_t hpOurs     = *(uint16_t*)(charOurs + 0x00);
    uint16_t maxHpOurs  = *(uint16_t*)(charOurs + 0x02);
    uint32_t expOurs    = *(uint32_t*)(charOurs + 0x04);
    uint8_t  modelOurs  = *(charOurs + 0x08);
    uint16_t hpRes      = *(uint16_t*)(charResearch + 0x00);
    uint16_t maxHpRes   = *(uint16_t*)(charResearch + 0x02);
    uint32_t expRes     = *(uint32_t*)(charResearch + 0x04);
    uint8_t  modelRes   = *(charResearch + 0x08);
    Log::Menu("[OFFSET-VERIFY] CHAR[0] OURS(+0x48C): hp=%u/%u exp=%u model=%u",
               hpOurs, maxHpOurs, expOurs, (unsigned)modelOurs);
    Log::Menu("[OFFSET-VERIFY] CHAR[0] RESEARCH(+0x4A0): hp=%u/%u exp=%u model=%u",
               hpRes, maxHpRes, expRes, (unsigned)modelRes);
    bool charMatch = (hpOurs == hpRes && expOurs == expRes && modelOurs == modelRes);
    Log::Menu("[OFFSET-VERIFY] CHAR[0]: %s", charMatch ? "MATCH" : "DIFFER");

    // --- GF[0] (Quetzalcoatl) name comparison ---
    // Our offset: savemap + 0x4C (header 0x4C)
    // Research offset: savemap + 0x60 (header 0x60)
    uint8_t* gfOurs     = sm + 0x4C;
    uint8_t* gfResearch = sm + 0x60;
    // GF name is first 12 bytes, FF8-encoded. Decode both.
    uint8_t gfNameOursAdj[12], gfNameResAdj[12];
    for (int i = 0; i < 12; i++) {
        gfNameOursAdj[i] = (gfOurs[i] >= 0x20) ? (gfOurs[i] - 0x20) : gfOurs[i];
        gfNameResAdj[i] = (gfResearch[i] >= 0x20) ? (gfResearch[i] - 0x20) : gfResearch[i];
    }
    char gfNameOursDec[32] = {}, gfNameResDec[32] = {};
    DecodeNameToBuffer(gfNameOursAdj, 12, gfNameOursDec, sizeof(gfNameOursDec));
    DecodeNameToBuffer(gfNameResAdj, 12, gfNameResDec, sizeof(gfNameResDec));
    // Also raw hex
    char gfHexOurs[64] = {}, gfHexRes[64] = {};
    for (int i = 0; i < 12; i++) {
        sprintf(gfHexOurs + i*3, "%02X ", gfOurs[i]);
        sprintf(gfHexRes + i*3, "%02X ", gfResearch[i]);
    }
    Log::Menu("[OFFSET-VERIFY] GF[0] OURS(+0x4C): raw=[%s] decoded='%s'",
               gfHexOurs, gfNameOursDec);
    Log::Menu("[OFFSET-VERIFY] GF[0] RESEARCH(+0x60): raw=[%s] decoded='%s'",
               gfHexRes, gfNameResDec);

    // --- Additional character scan: dump char[0] through char[2] at both offsets ---
    for (int c = 0; c < 3; c++) {
        uint8_t* cOurs = sm + 0x48C + 0x98 * c;
        uint8_t* cRes  = sm + 0x4A0 + 0x98 * c;
        Log::Menu("[OFFSET-VERIFY] CHAR[%d] ours(+0x%X): hp=%u/%u exp=%u model=%u exists=%u",
                   c, 0x48C + 0x98*c,
                   *(uint16_t*)(cOurs+0x00), *(uint16_t*)(cOurs+0x02),
                   *(uint32_t*)(cOurs+0x04), (unsigned)*(cOurs+0x08),
                   (unsigned)*(cOurs+0x94));
        Log::Menu("[OFFSET-VERIFY] CHAR[%d] research(+0x%X): hp=%u/%u exp=%u model=%u exists=%u",
                   c, 0x4A0 + 0x98*c,
                   *(uint16_t*)(cRes+0x00), *(uint16_t*)(cRes+0x02),
                   *(uint32_t*)(cRes+0x04), (unsigned)*(cRes+0x08),
                   (unsigned)*(cRes+0x94));
    }

    // --- Location ID at both header offsets ---
    uint16_t locOurs = *(uint16_t*)(sm + 0x00);  // our current
    uint16_t locRes  = *(uint16_t*)(sm + 0x04);  // research says +0x04
    Log::Menu("[OFFSET-VERIFY] LOCATION: ours(+0x00)=%u  research(+0x04)=%u",
               (unsigned)locOurs, (unsigned)locRes);

    // --- Current field ID (research says +0x0D52) ---
    uint16_t fieldId = *(uint16_t*)(sm + 0x0D52);
    Log::Menu("[OFFSET-VERIFY] FIELD_ID(+0x0D52)=%u", (unsigned)fieldId);

    // --- SeeD test level (research says +0x0D43) ---
    uint8_t seedTest = *(sm + 0x0D43);
    Log::Menu("[OFFSET-VERIFY] SEED_TEST_LVL(+0x0D43)=%u", (unsigned)seedTest);

    // --- Item inventory spot check (research says +0x0B54) ---
    uint8_t* items = sm + 0x0B54;
    int itemCount = 0;
    for (int i = 0; i < 198; i++) {
        if (items[i*2] != 0) itemCount++;
    }
    Log::Menu("[OFFSET-VERIFY] ITEMS(+0x0B54): %d non-empty slots out of 198", itemCount);
    // Log first 5 non-empty items
    int logged = 0;
    for (int i = 0; i < 198 && logged < 5; i++) {
        uint8_t id = items[i*2];
        uint8_t qty = items[i*2 + 1];
        if (id != 0) {
            Log::Menu("[OFFSET-VERIFY]   item[%d]: id=%u qty=%u", i, (unsigned)id, (unsigned)qty);
            logged++;
        }
    }

    // --- CORRECTED research offsets (subtract 0x14 from research values) ---
    Log::Menu("[OFFSET-VERIFY] === Corrected Research Offsets (research - 0x14) ===");

    // Gameplay Gil: research +0x0B1C -> corrected +0x0B08
    uint32_t gilCorrected = *(uint32_t*)(sm + 0x0B08);
    Log::Menu("[OFFSET-VERIFY] GIL CORRECTED(+0x0B08)=%u  ours(+0x08)=%u  %s",
               gilCorrected, gilOurs,
               (gilCorrected == gilOurs) ? "MATCH" : "DIFFER");

    // Live game time: research +0x0CE0 -> corrected +0x0CCC
    uint32_t timeCorrected = *(uint32_t*)(sm + 0x0CCC);
    Log::Menu("[OFFSET-VERIFY] TIME CORRECTED(+0x0CCC)=%u sec  ours(+0x0C)=%u sec  %s",
               timeCorrected, timeOurs,
               (timeCorrected == timeOurs) ? "MATCH" : "DIFFER");

    // Active party: research +0x0B04 -> corrected +0x0AF0
    uint8_t partyCorrected[4];
    partyCorrected[0] = *(sm + 0x0AF0);
    partyCorrected[1] = *(sm + 0x0AF1);
    partyCorrected[2] = *(sm + 0x0AF2);
    partyCorrected[3] = *(sm + 0x0AF3);
    Log::Menu("[OFFSET-VERIFY] PARTY CORRECTED(+0x0AF0)=(%u,%u,%u,%u)  ours(+0xAF1)=(%u,%u,%u)",
               partyCorrected[0], partyCorrected[1], partyCorrected[2], partyCorrected[3],
               partyOurs[0], partyOurs[1], partyOurs[2]);

    // Item inventory: research +0x0B54 -> corrected +0x0B40
    uint8_t* itemsCorrected = sm + 0x0B40;
    int itemCountCorr = 0;
    for (int i = 0; i < 198; i++) {
        if (itemsCorrected[i*2] != 0) itemCountCorr++;
    }
    Log::Menu("[OFFSET-VERIFY] ITEMS CORRECTED(+0x0B40): %d non-empty slots out of 198", itemCountCorr);
    int loggedCorr = 0;
    for (int i = 0; i < 198 && loggedCorr < 5; i++) {
        uint8_t id2 = itemsCorrected[i*2];
        uint8_t qty2 = itemsCorrected[i*2 + 1];
        if (id2 != 0) {
            Log::Menu("[OFFSET-VERIFY]   item[%d]: id=%u qty=%u", i, (unsigned)id2, (unsigned)qty2);
            loggedCorr++;
        }
    }

    // Current field ID: research +0x0D52 -> corrected +0x0D3E
    uint16_t fieldIdCorr = *(uint16_t*)(sm + 0x0D3E);
    Log::Menu("[OFFSET-VERIFY] FIELD_ID CORRECTED(+0x0D3E)=%u", (unsigned)fieldIdCorr);

    // SeeD test level: research +0x0D43 -> corrected +0x0D2F
    uint8_t seedTestCorr = *(sm + 0x0D2F);
    Log::Menu("[OFFSET-VERIFY] SEED_TEST CORRECTED(+0x0D2F)=%u", (unsigned)seedTestCorr);

    // Config: research +0x0AF0 -> corrected +0x0ADC
    Log::Menu("[OFFSET-VERIFY] CONFIG region CORRECTED(+0x0ADC) first 20 bytes:");
    {
        char cfgHex[120] = {};
        int hp = 0;
        for (int i = 0; i < 20 && hp < 100; i++)
            hp += sprintf(cfgHex + hp, "%02X ", *(sm + 0x0ADC + i));
        Log::Menu("[OFFSET-VERIFY]   %s", cfgHex);
    }

    Log::Menu("[OFFSET-VERIFY] === End Offset Verification ===");
}

static void DumpMenuScreenData()
{
    Log::Menu("[MENUDIAG] === Menu Screen Data Dump (v0.08.18) ===");

    // --- 1. Savemap header (confirmed base 0x1CFDC5C) ---
    {  // SEH removed — diagnostic function, crash is acceptable
        uint8_t* hdr = (uint8_t*)SAVEMAP_BASE;
        uint16_t locId    = *(uint16_t*)(hdr + HDR_LOCATION_ID);
        uint16_t hdrHp    = *(uint16_t*)(hdr + HDR_CHAR1_CURR_HP);
        uint16_t hdrMaxHp = *(uint16_t*)(hdr + HDR_CHAR1_MAX_HP);
        uint16_t saveCnt  = *(uint16_t*)(hdr + HDR_SAVE_COUNT);
        uint32_t gil      = *(uint32_t*)(hdr + HDR_GIL);
        uint32_t timeSec  = *(uint32_t*)(hdr + HDR_PLAYED_TIME);
        uint8_t  lvl      = *(hdr + HDR_CHAR1_LVL);
        uint8_t  port1    = *(hdr + HDR_PORTRAITS + 0);
        uint8_t  port2    = *(hdr + HDR_PORTRAITS + 1);
        uint8_t  port3    = *(hdr + HDR_PORTRAITS + 2);
        uint32_t disk     = *(uint32_t*)(hdr + HDR_CURR_DISK);
        uint32_t currSave = *(uint32_t*)(hdr + HDR_CURR_SAVE);
        Log::Menu("[MENUDIAG] HEADER @0x%08X:", SAVEMAP_BASE);
        Log::Menu("[MENUDIAG]   locId=%u hp=%u/%u saveCount=%u gil=%u",
                   (unsigned)locId, (unsigned)hdrHp, (unsigned)hdrMaxHp,
                   (unsigned)saveCnt, gil);
        Log::Menu("[MENUDIAG]   time=%u sec (%u:%02u:%02u) lvl=%u disk=%u currSave=%u",
                   timeSec, timeSec/3600, (timeSec%3600)/60, timeSec%60,
                   (unsigned)lvl, disk, currSave);
        Log::Menu("[MENUDIAG]   portraits=(%u,%u,%u)",
                   (unsigned)port1, (unsigned)port2, (unsigned)port3);
        // Decode character names (FF8 encoded)
        for (int n = 0; n < 4; n++) {
            int nameOff = HDR_SQUALL_NAME + n * 12;
            const char* label = (n==0)?"squall":(n==1)?"rinoa":(n==2)?"angelo":"boko";
            char raw[64] = {};
            int rp = 0;
            for (int i = 0; i < 12 && rp < 60; i++)
                rp += sprintf(raw + rp, "%02X ", hdr[nameOff + i]);
            // Live savemap names are +0x20 offset from menu encoding.
            // Subtract 0x20 from each non-zero byte before decoding.
            uint8_t adjusted[12];
            for (int i = 0; i < 12; i++) {
                uint8_t b = hdr[nameOff + i];
                adjusted[i] = (b >= 0x20) ? (b - 0x20) : b;
            }
            char decodedName[32] = {};
            DecodeNameToBuffer(adjusted, 12, decodedName, sizeof(decodedName));
            Log::Menu("[MENUDIAG]   name_%s: raw=[%s] decoded='%s'",
                       label, raw, decodedName);
        }
    }

    // --- 2. Character structs (8 chars starting at savemap+0x50C) ---
    {
        uint8_t* charBase = (uint8_t*)(SAVEMAP_BASE + CHARS_OFFSET);
        Log::Menu("[MENUDIAG] CHARACTER DATA @0x%08X (savemap+0x%X, 8 x %d bytes):",
                   (uint32_t)(SAVEMAP_BASE + CHARS_OFFSET), CHARS_OFFSET, CHAR_STRUCT_SIZE);
        for (int c = 0; c < CHAR_COUNT; c++) {
            uint8_t* ch = charBase + CHAR_STRUCT_SIZE * c;
            uint16_t curHp  = *(uint16_t*)(ch + CHR_CURR_HP);
            uint16_t maxHp  = *(uint16_t*)(ch + CHR_MAX_HP);
            uint32_t exp    = *(uint32_t*)(ch + CHR_EXP);
            uint8_t  modelId = *(ch + CHR_MODEL_ID);
            uint8_t  weapId  = *(ch + CHR_WEAPON_ID);
            uint8_t  str = *(ch + CHR_STR), vit = *(ch + CHR_VIT);
            uint8_t  mag = *(ch + CHR_MAG), spr = *(ch + CHR_SPR);
            uint8_t  spd = *(ch + CHR_SPD), lck = *(ch + CHR_LCK);
            uint8_t  exists  = *(ch + CHR_EXISTS);
            uint8_t  status  = *(ch + CHR_STATUS);
            const char* name = (modelId < 8) ? CHAR_NAMES[modelId] : "???";
            Log::Menu("[MENUDIAG]   char[%d] @0x%08X model=%u(%s) exists=%u HP=%u/%u EXP=%u "
                       "weapon=%u str=%u vit=%u mag=%u spr=%u spd=%u lck=%u status=%u",
                       c, (uint32_t)(uintptr_t)ch, (unsigned)modelId, name, (unsigned)exists,
                       (unsigned)curHp, (unsigned)maxHp, exp,
                       (unsigned)weapId, (unsigned)str, (unsigned)vit,
                       (unsigned)mag, (unsigned)spr, (unsigned)spd, (unsigned)lck,
                       (unsigned)status);
        }
    }

    // --- 3. Party composition + SeeD rank scan ---
    // savemap_ff8_field_h contains SeeD EXP at offset +0x08 (uint16).
    // Party order is stored as battle_order[32] in savemap_ff8_items.
    // These sections follow characters in the savemap.
    // Characters end at savemap + 0x50C + 8*0x98 = savemap + 0x98C
    // After chars: shops, limit breaks, items (with battle_order), then battle, field_h, field.
    // Let's dump 512 bytes after the character section to find party + SeeD.
    {
        uint32_t postCharStart = SAVEMAP_BASE + CHARS_OFFSET + CHAR_STRUCT_SIZE * CHAR_COUNT;
        Log::Menu("[MENUDIAG] POST-CHARACTER REGION @0x%08X (savemap+0x%X, 512 bytes hex):",
                   postCharStart, CHARS_OFFSET + CHAR_STRUCT_SIZE * CHAR_COUNT);
        for (int off = 0; off < 512; off += 16) {
            uint8_t* p = (uint8_t*)(postCharStart + off);
            char line[120] = {};
            int lpos = 0;
            lpos += sprintf(line + lpos, "+0x%04X ", CHARS_OFFSET + CHAR_STRUCT_SIZE * CHAR_COUNT + off);
            for (int b = 0; b < 16 && lpos < 100; b++)
                lpos += sprintf(line + lpos, "%02X ", p[b]);
            Log::Menu("[MENUDIAG]   %s", line);
        }
    }

    // --- 4. Scan for SeeD EXP (uint16, range 1-31000ish) in savemap ---
    // Also look for battle_order bytes (party composition: 3 bytes, each 0-7 or 0xFF)
    {
        // Scan from after header to end of savemap (~4KB after characters)
        uint32_t scanStart = SAVEMAP_BASE + HDR_SIZE;  // after header
        uint32_t scanEnd   = SAVEMAP_BASE + 0x1800;    // ~6KB total
        Log::Menu("[MENUDIAG] PARTY SCAN 0x%08X - 0x%08X:", scanStart, scanEnd);
        int partyHits = 0;
        for (uint32_t addr = scanStart; addr < scanEnd - 3 && partyHits < 15; addr++) {
            uint8_t b0 = *(uint8_t*)addr;
            uint8_t b1 = *(uint8_t*)(addr + 1);
            uint8_t b2 = *(uint8_t*)(addr + 2);
            // Only Squall at this point: (0, 0xFF, 0xFF)
            if (b0 == 0 && b1 == 0xFF && b2 == 0xFF) {
                uint32_t smOff = addr - SAVEMAP_BASE;
                Log::Menu("[MENUDIAG]   [PARTY] 0x%08X (savemap+0x%X) = (0, FF, FF)",
                           addr, smOff);
                partyHits++;
            }
        }
    }

    // --- 5. Hex dump of header raw bytes for verification ---
    {
        uint8_t* hdr = (uint8_t*)SAVEMAP_BASE;
        Log::Menu("[MENUDIAG] HEADER RAW (first 0x4C bytes):");
        for (int off = 0; off < 0x4C; off += 16) {
            char line[120] = {};
            int lpos = 0;
            lpos += sprintf(line + lpos, "+0x%02X ", off);
            for (int b = 0; b < 16 && off + b < 0x4C && lpos < 100; b++)
                lpos += sprintf(line + lpos, "%02X ", hdr[off + b]);
            Log::Menu("[MENUDIAG]   %s", line);
        }
    }

    Log::Menu("[MENUDIAG] === End Menu Screen Data Dump ===");
}

// ============================================================================
// v0.08.20: Menu open summary — announce party, Gil, time, location
// ============================================================================
static const int PARTY_OFFSET = 0xAF1;  // 3 bytes: active party member indices

static void AnnounceMenuSummary()
{
    uint8_t* sm = (uint8_t*)SAVEMAP_BASE;

    // Read header fields
    uint32_t gil      = *(uint32_t*)(sm + HDR_GIL);
    uint32_t timeSec  = *(uint32_t*)(sm + HDR_PLAYED_TIME);
    uint8_t  lvl      = *(sm + HDR_CHAR1_LVL);
    uint16_t locId    = *(uint16_t*)(sm + HDR_LOCATION_ID);

    // Location name from SETPLACE table
    const char* locName = GetLocationNameById(locId);
    char locBuf[64];
    if (!locName) {
        sprintf(locBuf, "Location %u", (unsigned)locId);
        locName = locBuf;
    }

    // Read party indices
    uint8_t party[3];
    party[0] = *(sm + PARTY_OFFSET + 0);
    party[1] = *(sm + PARTY_OFFSET + 1);
    party[2] = *(sm + PARTY_OFFSET + 2);

    // Build party string with HP for each member
    char partyBuf[512] = {};
    int pp = 0;
    uint8_t* charBase = sm + CHARS_OFFSET;
    for (int i = 0; i < 3; i++) {
        uint8_t idx = party[i];
        if (idx > 7) continue;  // 0xFF = empty slot
        uint8_t* ch = charBase + CHAR_STRUCT_SIZE * idx;
        uint16_t hp = *(uint16_t*)(ch + CHR_CURR_HP);
        // v0.17.8.17.7: Dream-aware name via the shared resolver (replaces the
        // .17.6 inline block). idx is the formation index (stale regular party
        // during a dream); ResolveDreamAwareCharId maps it to the dream member's
        // model_id when applicable, then GetCharacterNameByPortrait names 0..10.
        uint8_t modelId = *(ch + CHR_MODEL_ID);
        uint8_t nameId  = ResolveDreamAwareCharId(idx);
        const char* name = GetCharacterNameByPortrait(nameId);
        if (!name) name = "Unknown";
        Log::Menu("[MenuTTS] party slot %d: formIdx=%u modelId=%u -> %s",
                  i, (unsigned)idx, (unsigned)modelId, name);
        if (pp > 0) pp += sprintf(partyBuf + pp, ". ");
        uint32_t exp = *(uint32_t*)(ch + CHR_EXP);
        int charLvl;
        if (i == 0) {
            charLvl = lvl;  // header level is accurate for lead
        } else {
            charLvl = (int)(exp / 1000) + 1;
            if (charLvl > 100) charLvl = 100;
        }
        if (i == 0) {
            uint16_t maxHp = *(uint16_t*)(sm + HDR_CHAR1_MAX_HP);
            pp += sprintf(partyBuf + pp, "%s Level %d, HP %u of %u",
                          name, charLvl, (unsigned)hp, (unsigned)maxHp);
        } else {
            pp += sprintf(partyBuf + pp, "%s Level %d, HP %u",
                          name, charLvl, (unsigned)hp);
        }
    }

    // Format play time
    int hours = timeSec / 3600;
    int mins  = (timeSec % 3600) / 60;
    char timeBuf[32];
    if (hours > 0)
        sprintf(timeBuf, "%d hours %d minutes", hours, mins);
    else
        sprintf(timeBuf, "%d minutes", mins);

    // Build full announcement
    char announce[1024];
    sprintf(announce, "%s. %s. %u Gil. Play time %s.",
            locName, partyBuf, gil, timeBuf);

    ScreenReader::Speak(announce, true);
    Log::Menu("[MenuTTS] Menu summary: %s", announce);
}

// ============================================================================
// v0.08.22: Left-panel cursor diagnostic + help text capture
// ============================================================================
// When F11+Shift is pressed, starts a memory monitor that:
//   1. Snapshots 2KB around pMenuStateA every 200ms for 15 seconds
//   2. Logs any byte changes — these reveal the left-panel cursor address
//   3. Captures GCW buffer on each snapshot to see rendered help text
// Instructions: Press Shift+F11, then press LEFT arrow to move cursor to party
// member. The monitor will log which bytes changed.

static bool     s_memMonitorActive = false;
static DWORD    s_memMonitorStart = 0;
static const DWORD MEM_MONITOR_DURATION_MS = 15000;  // 15 seconds
static const int MEM_MONITOR_REGION_SIZE = 2048;  // bytes to monitor
static uint8_t  s_memMonitorSnap[2048] = {};
static bool     s_memMonitorSnapValid = false;
static DWORD    s_memMonitorLastPoll = 0;
static int      s_memMonitorChangeCount = 0;

static void StartMemoryMonitor()
{
    s_memMonitorActive = true;
    s_memMonitorStart = GetTickCount();
    s_memMonitorSnapValid = false;
    s_memMonitorChangeCount = 0;
    s_memMonitorLastPoll = 0;
    Log::Menu("[MEMMON] === Started memory monitor (15s, 2KB around pMenuStateA) ===");
    Log::Menu("[MEMMON] Press LEFT arrow to move cursor to party member panel.");
    ScreenReader::Speak("Memory monitor started. Press left to select party member.", true);
}

static void PollMemoryMonitor()
{
    if (!s_memMonitorActive) return;
    DWORD now = GetTickCount();

    // Check timeout
    if (now - s_memMonitorStart > MEM_MONITOR_DURATION_MS) {
        Log::Menu("[MEMMON] === Monitor stopped (%d changes detected) ===", s_memMonitorChangeCount);
        ScreenReader::Speak("Monitor done", true);
        s_memMonitorActive = false;
        return;
    }

    // Poll every 200ms
    if (now - s_memMonitorLastPoll < 200) return;
    s_memMonitorLastPoll = now;

    uint8_t* base = (uint8_t*)pMenuStateA;
    uint8_t cur[2048];
    memcpy(cur, base, MEM_MONITOR_REGION_SIZE);

    if (!s_memMonitorSnapValid) {
        memcpy(s_memMonitorSnap, cur, MEM_MONITOR_REGION_SIZE);
        s_memMonitorSnapValid = true;
        Log::Menu("[MEMMON] Initial snapshot taken. Waiting for changes...");
        return;
    }

    // Compare and log changes (skip known noise: rendering ticks, etc.)
    for (int i = 0; i < MEM_MONITOR_REGION_SIZE; i++) {
        if (cur[i] != s_memMonitorSnap[i]) {
            // Skip known noisy offsets: 0x1E6 (menu cursor we already track),
            // 0x1E4-0x1E5 (rendering state), 0x1FD (render tick)
            int off = i;
            if (off == 0x1E6 || off == 0x1E4 || off == 0x1E5 || off == 0x1FD) {
                s_memMonitorSnap[i] = cur[i];
                continue;
            }
            Log::Menu("[MEMMON] +0x%03X: %u -> %u  (elapsed=%ums)",
                       off, (unsigned)s_memMonitorSnap[i], (unsigned)cur[i],
                       now - s_memMonitorStart);
            s_memMonitorSnap[i] = cur[i];
            s_memMonitorChangeCount++;
        }
    }

    // Also capture GCW buffer to see help text
    static DWORD s_lastGcwCapture = 0;
    if (now - s_lastGcwCapture >= 1000) {  // once per second
        s_lastGcwCapture = now;
        uint8_t gcwBuf[1024];
        int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
        if (gcwLen > 0) {
            std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
            if (!decoded.empty()) {
                Log::Menu("[MEMMON] GCW text (%d chars): \"%s\"", gcwLen, decoded.c_str());
            }
        }
    }
}

// ============================================================================
// v0.08.28: Auto submenu cursor discovery monitor
// ============================================================================
// Automatically activates when the player enters a submenu (top-level cursor
// stable for 500ms in mode 6). Monitors 4KB around pMenuStateA for any byte
// changes. When the player exits the submenu (top-level cursor changes),
// logs a summary showing which offsets changed and their value ranges.
// Offsets with small-integer patterns (0-20, changing by 1) are flagged as
// likely cursor candidates.

static const int SUBMON_REGION_SIZE = 4096;
static bool     s_submonActive = false;
static DWORD    s_submonStableSince = 0;     // when top-level cursor last changed
static uint8_t  s_submonSubmenu = 0xFF;       // which submenu we're monitoring
static uint8_t  s_submonSnap[SUBMON_REGION_SIZE] = {};
static bool     s_submonSnapValid = false;
static DWORD    s_submonLastPoll = 0;

// Per-offset tracking: how many times each byte changed, and the min/max values seen
static uint16_t s_submonChangeCount[SUBMON_REGION_SIZE] = {};
static uint8_t  s_submonMinVal[SUBMON_REGION_SIZE] = {};
static uint8_t  s_submonMaxVal[SUBMON_REGION_SIZE] = {};
static uint8_t  s_submonFirstVal[SUBMON_REGION_SIZE] = {};
static int      s_submonTotalPolls = 0;

// Known noisy offsets to ignore (rendering ticks, counters, etc.)
static bool IsSubmonNoiseOffset(int off)
{
    // Top-level cursor (we already track this)
    if (off == 0x1E6) return true;
    // Known rendering noise from v0.08.22 memory monitor
    if (off == 0x1E4 || off == 0x1E5 || off == 0x1E9) return true;
    if (off == 0x1FD) return true;  // render tick
    if (off == 0x1CE || off == 0x1CA) return true;  // timer/countdown
    if (off == 0x1EA) return true;  // animation timer
    if (off >= 0x5E0 && off <= 0x5F8) return true;  // rendering state region
    return false;
}

// ============================================================================
// v0.18.3.39 (#66) — forced party-select discovery probe (mode-10 reframe).
// BAT of v0.18.3.38 found the forced select (Rinoa joining, after the Fake
// President battle) runs in GAME MODE 10 — not menu mode 6 — and the menu
// subsystem byte +0x1E8 is NOT 10 there, so it does not reuse the main-menu
// Switch's signal. This reframe runs at the TOP of MenuTTS::Update (before any
// game-mode gate) and, while game mode == 10, logs the GCW text plus a snapshot
// of pMenuStateA[+0x1C0..+0x2C0): the full region on entry, then only the bytes
// that change as the cursor moves — to locate this screen's cursor/roster.
// Log-only, no speech. Off for ship; gate, don't delete.
// ============================================================================
#define FORCED_PSEL_DIAG 1

#if FORCED_PSEL_DIAG
// Diagnostic capture window in pMenuStateA. Widened v0.18.3.46 from the original
// +0x1C0..+0x2C0 (256 B) to +0x100..+0x500 (1024 B) so the action-bar-vs-character
// FOCUS byte is caught even if it lives outside the menu-cursor cluster (the menu
// Switch focus byte +0x22E stays constant in mode 10, so the real one is unknown).
static const int   FPS_DIAG_OFF    = 0x100;
static const int   FPS_DIAG_LEN    = 0x400;
static uint16_t    s_fpsPrevMode    = 0xFFFF;
static uint8_t     s_fpsRegion[FPS_DIAG_LEN] = {};
static bool        s_fpsRegionValid = false;
static std::string s_fpsPrevGcw;

// POD + SEH (no std::string here): read game mode + snapshot the diag window.
static bool ForcedPselReadState(uint16_t& mode, uint8_t* region)
{
    bool ok = false;
    __try {
        mode = pGameMode ? *(uint16_t*)pGameMode : 0xFFFF;
        uint8_t* pmd = (uint8_t*)pMenuStateA;
        for (int i = 0; i < FPS_DIAG_LEN; i++) region[i] = pmd[FPS_DIAG_OFF + i];
        ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

// One-shot reference dump (SEH-guarded, no std::string): the data the eventual
// announce would read, so the implementation can be written from a single BAT.
// savemap party formation +0xAF0 / active +0xB04; menu roster +0x1DB; per-char
// level-source EXP (savemap +0x48C + id*0x98 + 0x04) and menu HP array
// (pMenuStateA +0x71E + id*0x20, cur +0 / max +2); computed-stats HP
// (0x1CFF000 + slot*0x1D0, cur +0x172 / max +0x174). Cross-check against the
// on-screen LV/HP (Rinoa 653/653, Squall 883/1044, Zell 799/994) to confirm
// which sources are live in game mode 10.
static void ForcedPselDumpRefs()
{
    __try {
        uint8_t* sm  = (uint8_t*)0x1CFDC5C;     // SAVEMAP_BASE
        uint8_t* pmd = (uint8_t*)pMenuStateA;
        Log::Menu("[ForcedPSel-REF] savemap party +0xAF0=[%u,%u,%u,%u]  active +0xB04=[%u,%u,%u,%u]",
                  (unsigned)sm[0xAF0],(unsigned)sm[0xAF1],(unsigned)sm[0xAF2],(unsigned)sm[0xAF3],
                  (unsigned)sm[0xB04],(unsigned)sm[0xB05],(unsigned)sm[0xB06],(unsigned)sm[0xB07]);
        Log::Menu("[ForcedPSel-REF] menu roster +0x1DB=[%u,%u,%u,%u,%u,%u,%u,%u]",
                  (unsigned)pmd[0x1DB],(unsigned)pmd[0x1DC],(unsigned)pmd[0x1DD],(unsigned)pmd[0x1DE],
                  (unsigned)pmd[0x1DF],(unsigned)pmd[0x1E0],(unsigned)pmd[0x1E1],(unsigned)pmd[0x1E2]);
        // Working party (live in mode 10, unlike savemap +0xAF0): 3 active slots
        // at +0x1EA, reserves from +0x1ED. +0x1EC == slot 3 confirmed by swap.
        Log::Menu("[ForcedPSel-REF] working +0x1EA active=[%u,%u,%u] reserves=[%u,%u,%u,%u,%u,%u,%u,%u]",
                  (unsigned)pmd[0x1EA],(unsigned)pmd[0x1EB],(unsigned)pmd[0x1EC],
                  (unsigned)pmd[0x1ED],(unsigned)pmd[0x1EE],(unsigned)pmd[0x1EF],
                  (unsigned)pmd[0x1F0],(unsigned)pmd[0x1F1],(unsigned)pmd[0x1F2],
                  (unsigned)pmd[0x1F3],(unsigned)pmd[0x1F4]);
        for (int id = 0; id < 8; id++) {
            uint32_t exp    = *(uint32_t*)(sm  + 0x48C + id*0x98 + 0x04);
            uint16_t mhpCur = *(uint16_t*)(pmd + 0x71E + id*0x20 + 0);
            uint16_t mhpMax = *(uint16_t*)(pmd + 0x71E + id*0x20 + 2);
            Log::Menu("[ForcedPSel-REF] id=%d  EXP=%u (lvl~%u)  menuHP=%u/%u",
                      id, (unsigned)exp, (unsigned)(exp/1000+1),
                      (unsigned)mhpCur, (unsigned)mhpMax);
        }
        for (int slot = 0; slot < 4; slot++) {
            uint8_t* cs  = (uint8_t*)0x1CFF000 + slot*0x1D0;
            uint16_t cur = *(uint16_t*)(cs + 0x172);
            uint16_t max = *(uint16_t*)(cs + 0x174);
            Log::Menu("[ForcedPSel-REF] compStats slot %d  HP=%u/%u",
                      slot, (unsigned)cur, (unsigned)max);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[ForcedPSel-REF] (read fault)");
    }
}

static void PollForcedPselDiag()
{
    if (!pGameMode || !pMenuStateA) return;
    uint16_t mode = 0xFFFF;
    uint8_t region[FPS_DIAG_LEN];
    if (!ForcedPselReadState(mode, region)) return;

    // Forced select = game mode 10. Outside it, reset and (once) log the exit.
    if (mode != 10) {
        if (s_fpsPrevMode == 10) { Log::Menu("[ForcedPSel] EXIT mode 10->%u", (unsigned)mode); ForcedPselDumpRefs(); }
        s_fpsPrevMode = mode;
        s_fpsRegionValid = false;
        s_fpsPrevGcw.clear();
        return;
    }

    // mode == 10: capture the on-screen text (GCW).
    std::string gcw;
    {
        uint8_t buf[2048];
        int len = FieldDialog::SnapshotGcwBuffer(buf, sizeof(buf));
        if (len > 0) gcw = FF8TextDecode::DecodeMenuText(buf, len);
    }

    if (s_fpsPrevMode != 10) {
        // Entry: dump the full window once, line-by-line (so a wide window can't
        // overrun a single log call), + the full GCW.
        Log::Menu("[ForcedPSel] ENTER mode=10  region[+0x%03X..+0x%03X):",
                  FPS_DIAG_OFF, FPS_DIAG_OFF + FPS_DIAG_LEN);
        for (int i = 0; i < FPS_DIAG_LEN; i += 16) {
            char line[96]; int p = 0;
            p += sprintf(line + p, "  +%03X:", FPS_DIAG_OFF + i);
            for (int j = 0; j < 16 && i + j < FPS_DIAG_LEN; j++)
                p += sprintf(line + p, " %02X", region[i + j]);
            Log::Menu("[ForcedPSel]%s", line);
        }
        Log::Menu("[ForcedPSel] ENTER gcw(%d)=\"%s\"", (int)gcw.size(), gcw.c_str());
        ForcedPselDumpRefs();
    } else {
        // While inside: log only the bytes that changed (cursor moves / focus) + GCW.
        if (s_fpsRegionValid) {
            char ch[1600]; int c = 0;
            for (int i = 0; i < FPS_DIAG_LEN && c < 1500; i++) {
                if (region[i] != s_fpsRegion[i])
                    c += sprintf(ch + c, " +%03X:%02X->%02X", FPS_DIAG_OFF + i,
                                 (unsigned)s_fpsRegion[i], (unsigned)region[i]);
            }
            if (c > 0) Log::Menu("[ForcedPSel] region change:%s", ch);
        }
        if (gcw != s_fpsPrevGcw)
            Log::Menu("[ForcedPSel] gcw(%d)=\"%s\"", (int)gcw.size(), gcw.c_str());
    }

    for (int i = 0; i < FPS_DIAG_LEN; i++) s_fpsRegion[i] = region[i];
    s_fpsRegionValid = true;
    s_fpsPrevGcw = gcw;
    s_fpsPrevMode = mode;
}
#endif

