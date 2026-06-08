// menu_tts_status.inl — Status screen LIMIT-BREAK page (page 4) TTS (#49)
// Included from menu_tts.cpp AFTER menu_tts_diagnostics.inl / menu_tts_gf.inl so
// it can use SAVEMAP_BASE, FieldDialog::SnapshotGcwBuffer, FF8TextDecode::
// DecodeMenuText, Log::Menu, ScreenReader::Speak, pMenuStateA. Do not compile
// independently. No header guards / namespaces (textual include).
//
// Announces, on the per-character Status DETAIL view, LIMIT page (Status
// subsystem +0x1E8==5; detail focus +0x22E==3; page index +0x257==3):
//
//   1. TOGGLES (the headline win — these let a blind player land Squall's
//      Renzokuken and Zell's Duel, which otherwise need unseeable timing):
//        - Gunblade Auto         savemap+0x0D1C & 0x01  (1 = ON)
//        - Zell Duel-Auto        savemap+0x0D1C & 0x02  (set = ON)  [shared field]
//        - Renzokuken Indicator  savemap+0x0D1D & 0x80  (0 = ON, INVERTED)
//      Gunblade Auto / Duel-Auto are detected from the GCW HELP text (no cursor
//      dependence). Renzokuken Indicator is DISABLED while Gunblade Auto is on —
//      the game then stops drawing its help text, so we detect that row by cursor
//      (Squall row +0x25F==2) and announce "disabled" (v0.18.2.28, BAT #1).
//
//   2. READ-ONLY LIST NAMES — limit moves/abilities parsed from the GCW by
//      longest-match against the per-character name table, indexed by the row
//      cursor (band +0x25F..+0x264, whichever byte moved). Unlearned rows past
//      the learned ones announce "Empty slot" (v0.18.2.28). The "/" key re-reads
//      the focused move's GCW help description (StatusLimitSpeakSelectedHelp).
//
// Selphie's Slot limit has no learnable list (omitted from the table). Rinoa's
// Angelo learn-% gauges are #50 (deferred).

// --- savemap toggle offsets (post-0x14-correction) ---
static const int ST_LIMIT_AUTOFLAGS = 0x0D1C;  // bit0x01 Gunblade Auto, bit0x02 Duel-Auto
static const int ST_LIMIT_RENZIND   = 0x0D1D;  // bit0x80 Renzokuken Indicator (0 = ON)

// --- per-character read-only limit lists (display order; spellings TBC on BAT) ---
static const char* ST_RENZ_FINISHERS[] = {
    "Rough Divide", "Fated Circle", "Blasting Zone", "Lion Heart"
};
static const char* ST_ZELL_DUEL[] = {
    "Punch Rush", "Booya", "Heel Drop", "Mach Kick", "Dolphin Blow",
    "Meteor Strike", "Burning Rave", "Meteor Barret", "Different Beat",
    "My Final Heaven"
};
static const char* ST_QUISTIS_BLUE[] = {
    "Laser Eye", "Ultra Waves", "Electrocute", "LV? Death", "Degenerator",
    "Aqua Breath", "Micro Missiles", "Acid", "Gatling Gun", "Fire Breath",
    "Bad Breath", "White Wind", "Homing Laser", "Mighty Guard", "Ray-Bomb",
    "Shockwave Pulse"
};
static const char* ST_RINOA_ANGELO[] = {
    "Angelo Berserk", "Angelo Rush", "Angelo Recover", "Angelo Reverse",
    "Angelo Search", "Angelo Cannon", "Angelo Strike", "Invincible Moon",
    "Wishing Star"
};

// Baked target descriptions for the two TOGGLE-bearing pages (Squall, Zell).
// On those pages the GCW help text lags the cursor by one row and never catches
// up while the cursor is still (confirmed BAT #3: Burning Rave read "Damage one
// enemy" the whole time it was focused, flipping to "Damage all enemies" only
// after the cursor moved off it). Rinoa/Quistis pages have no toggle and read the
// GCW help correctly, so they keep descs=nullptr and use the live help text.
// Strings mirror the game's wording; Burning Rave + Fated Circle are the only
// all-enemy moves among these.
static const char* ST_RENZ_DESC[] = {
    "Damage one enemy",    // Rough Divide
    "Damage all enemies",  // Fated Circle
    "Damage one enemy",    // Blasting Zone
    "Damage one enemy"     // Lion Heart
};
static const char* ST_ZELL_DESC[] = {
    "Damage one enemy",    // Punch Rush
    "Damage one enemy",    // Booya
    "Damage one enemy",    // Heel Drop
    "Damage one enemy",    // Mach Kick
    "Damage one enemy",    // Dolphin Blow
    "Damage one enemy",    // Meteor Strike
    "Damage all enemies",  // Burning Rave
    "Damage one enemy",    // Meteor Barret
    "Damage one enemy",    // Different Beat
    "Damage one enemy"     // My Final Heaven
};

struct StatusLimitChar {
    const char*        header;
    const char* const* moves;
    const char* const* descs;   // baked descriptions, or nullptr to use GCW help
    int                moveCount;
    int                step;
    int                leadingToggles;
    int                cursorByte; // band index 0..5 (+0x25F..+0x264); -1 = use whichever byte moved
};
static const StatusLimitChar ST_LIMIT_CHARS[] = {
    //                                                  cnt step lead curByte
    { "Squall",  ST_RENZ_FINISHERS, ST_RENZ_DESC, 4,  1, 4,  0 },  // +0x25F; cur 0-3 = 2 toggles x ON/OFF, 4+ = finishers (step 1)
    { "Zell",    ST_ZELL_DUEL,      ST_ZELL_DESC, 10, 1, 0,  1 },  // +0x260 (band[1])
    { "Quistis", ST_QUISTIS_BLUE,   nullptr,      16, 1, 0, -1 },  // +0x262 (unconfirmed; auto-detect)
    { "Rinoa",   ST_RINOA_ANGELO,   nullptr,      9,  1, 0,  4 },  // +0x263 (band[4])
};
static const int ST_LIMIT_CHAR_COUNT = 4;

// Tokens skipped while forward-parsing the list run: the toggle labels, the
// ON/OFF state words, and Rinoa's bare "Angelo" command label that precedes the
// Angelo ability names (without this the parse stopped before her list — BAT #1).
static const char* ST_SKIP_TOKENS[] = {
    "Gunblade Auto", "Renzokuken Indicator", "Duel-Auto", "Angelo", "ON", "OFF"
};
static const int ST_SKIP_TOKEN_COUNT = 6;

// Diagnostics for mapping a new character's limit page (the cursor-band dump and
// the per-move cursor/help line). Off in production; flip to true when mapping a
// new page (e.g. Irvine's Shot limit) so [STBAND]/[STLIMIT] resume logging.
static const bool ST_LIMIT_DIAG = false;

// --- state ---
static bool    s_stActive       = false;
static DWORD   s_stPoll         = 0;
static int     s_stToggleFocus  = -1;   // 0 Gunblade / 1 Duel / 2 Renz / -1 none
static int     s_stToggleState  = -1;   // last spoken on/off (1/0/-1 unknown)
static int     s_stLastRow      = -1;   // dedupe row announce (see key scheme below)
static uint8_t s_stBand[6]      = {};   // +0x25F..+0x264 snapshot
static bool    s_stBandValid    = false;
// stored focused row for the "/" help re-read
static bool    s_stSelValid     = false;
static char    s_stSelName[64]  = {};
static char    s_stSelDesc[160] = {};

// Row-dedupe keys: a learned move = its index 0..; an empty slot = 100+index; the
// disabled Renzokuken Indicator = 200.
static const int ST_KEY_EMPTY_BASE = 100;
static const int ST_KEY_RENZ_DISABLED = 200;
static const int ST_KEY_SELPHIE = 300;

static void ResetStatusLimit()
{
    s_stActive      = false;
    s_stPoll        = 0;
    s_stToggleFocus = -1;
    s_stToggleState = -1;
    s_stLastRow     = -1;
    s_stBandValid   = false;
    s_stSelValid    = false;
    s_stSelName[0]  = '\0';
    s_stSelDesc[0]  = '\0';
}

// --- SEH-isolated raw reads (scalars only, C2712-safe) ---

static bool StRawOnLimitPage()
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        return (ms[0x22E] == 3) && (ms[0x257] == 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Packed toggle bits: bit0 Gunblade ON, bit1 Duel ON, bit2 Renz ON (de-inverted).
static int StRawToggleBits()
{
    __try {
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE;
        int v = 0;
        if (sm[ST_LIMIT_AUTOFLAGS] & 0x01) v |= 1;
        if (sm[ST_LIMIT_AUTOFLAGS] & 0x02) v |= 2;
        if ((sm[ST_LIMIT_RENZIND] & 0x80) == 0) v |= 4;   // 0 = ON
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static bool StRawBand(uint8_t out[6])
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        for (int i = 0; i < 6; i++) out[i] = ms[0x25F + i];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Which toggle's "Set ..." HELP text is showing (focused + enabled), or -1.
static int StFocusedToggle(const std::string& dec)
{
    if (dec.find("Set Gunblade")   != std::string::npos) return 0;
    if (dec.find("Set Duel")       != std::string::npos) return 1;
    if (dec.find("Set Renzokuken") != std::string::npos) return 2;
    return -1;
}

// Trim ASCII spaces from both ends, in place.
static void StTrim(std::string& s)
{
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) { s.clear(); return; }
    size_t b = s.find_last_not_of(' ');
    s = s.substr(a, b - a + 1);
}

// "/" on-demand: re-read the focused move's help description (falls back to the
// row name when no description was captured, e.g. an empty slot). Returns true
// while on the limit page with a stored row so the caller's normal help-bar
// reader runs everywhere else.
static bool StatusLimitSpeakSelectedHelp()
{
    if (!s_stActive || !s_stSelValid) return false;
    char out[192];
    if (s_stSelDesc[0]) snprintf(out, sizeof(out), "%s", s_stSelDesc);
    else                snprintf(out, sizeof(out), "%s", s_stSelName);
    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Status limit help (/): \"%s\"", out);
    return true;
}

// --- production poll ---
static void PollStatusLimit()
{
    s_stActive = true;
    DWORD now = GetTickCount();
    if (now - s_stPoll < 100) return;
    s_stPoll = now;

    if (!StRawOnLimitPage()) {            // detail panel but not the limit page
        s_stToggleFocus = -1;
        s_stToggleState = -1;
        s_stLastRow     = -1;
        s_stBandValid   = false;
        s_stSelValid    = false;          // "/" falls back to the normal help bar
        return;
    }

    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    if (len <= 0) return;
    std::string dec = FF8TextDecode::DecodeMenuText(gcw, len);
    if (dec.empty()) return;

    int bits = StRawToggleBits();

    // --- 1. TOGGLE row, help-keyed (Gunblade / Duel / enabled Renz) ---
    int tog = StFocusedToggle(dec);
    if (tog >= 0) {
        int st = (bits < 0) ? -1
               : (tog == 0) ? ((bits & 1) ? 1 : 0)
               : (tog == 1) ? ((bits & 2) ? 1 : 0)
                            : ((bits & 4) ? 1 : 0);
        if (tog != s_stToggleFocus || st != s_stToggleState) {
            s_stToggleFocus = tog;
            s_stToggleState = st;
            s_stLastRow     = -1;
            const char* nm = (tog == 0) ? "Gunblade Auto"
                           : (tog == 1) ? "Duel Auto"
                                        : "Renzokuken Indicator";
            const char* state = (st == 1) ? "on" : (st == 0) ? "off" : "unknown";
            // The Renzokuken Indicator row still shows its "Set Renzokuken
            // Indicator" help (so it lands here) but is greyed out while Gunblade
            // Auto is on. Announce "disabled" on focus rather than a meaningless
            // on/off (confirmed from the F11 screenshot — the row is greyed).
            bool renzDisabled = (tog == 2 && (bits & 1));
            char buf[96];
            if (renzDisabled)
                snprintf(buf, sizeof(buf), "Renzokuken Indicator, disabled");
            else
                snprintf(buf, sizeof(buf), "%s, %s", nm, state);
            ScreenReader::Speak(buf, true);
            Log::Menu("[MenuTTS] Status limit toggle: %s", buf);
            // store for "/"
            s_stSelValid = true;
            snprintf(s_stSelName, sizeof(s_stSelName), "%s", nm);
            if (renzDisabled)
                snprintf(s_stSelDesc, sizeof(s_stSelDesc),
                         "Disabled while Gunblade Auto is on");
            else
                s_stSelDesc[0] = '\0';
        }
        return;
    }
    // If we were on a toggle last poll, the band snapshot wasn't maintained
    // (the toggle path returns early), so the move onto the adjacent list row
    // registers no change and gets swallowed (BAT #4: the first ability after
    // Duel-Auto was silent until you moved again). Remember it and force the
    // arriving row to (re-)announce below.
    bool justLeftToggle = (s_stToggleFocus >= 0);
    s_stToggleFocus = -1;
    s_stToggleState = -1;
    if (justLeftToggle) s_stLastRow = -1;

    // --- 2. LIST / empty / disabled-Renz row (name parsed, indexed by cursor) ---
    const StatusLimitChar* cc = nullptr;
    for (int i = 0; i < ST_LIMIT_CHAR_COUNT; i++) {
        if (dec.find(std::string(ST_LIMIT_CHARS[i].header) + "LV") != std::string::npos) {
            cc = &ST_LIMIT_CHARS[i];
            break;
        }
    }
    if (cc == nullptr) {                   // Selphie's Slot has no learnable list
        if (dec.find("SelphieLV") != std::string::npos &&
            s_stLastRow != ST_KEY_SELPHIE) {
            s_stLastRow  = ST_KEY_SELPHIE;
            ScreenReader::Speak("Slot limit. No options to adjust.", true);
            Log::Menu("[MenuTTS] Status limit: Selphie Slot (no options)");
            s_stSelValid = true;
            snprintf(s_stSelName, sizeof(s_stSelName), "Slot limit. No options to adjust.");
            s_stSelDesc[0] = '\0';
        }
        return;
    }

    // Forward-parse the list run from just after the freshest menu-bar "Save" up
    // to "<header>LV", longest-matching move names (kept) vs skip tokens (dropped).
    size_t start = dec.rfind("Save");
    if (start == std::string::npos) return;
    start += 4;
    size_t bound = dec.find(std::string(cc->header) + "LV", start);
    size_t limit = (bound == std::string::npos) ? dec.size() : bound;

    const char* parsed[24];
    int parsedIdx[24];                     // full-list index of each parsed move
    int parsedCount = 0;
    size_t p = start;
    while (p < limit && parsedCount < 24) {
        const char* bestMove = nullptr;
        int    bestIdx  = -1;
        size_t bestLen  = 0;
        bool   bestSkip = false;
        for (int i = 0; i < cc->moveCount; i++) {
            size_t l = strlen(cc->moves[i]);
            if (l > bestLen && p + l <= dec.size() &&
                dec.compare(p, l, cc->moves[i]) == 0) {
                bestLen = l; bestMove = cc->moves[i]; bestIdx = i; bestSkip = false;
            }
        }
        for (int i = 0; i < ST_SKIP_TOKEN_COUNT; i++) {
            size_t l = strlen(ST_SKIP_TOKENS[i]);
            if (l > bestLen && p + l <= dec.size() &&
                dec.compare(p, l, ST_SKIP_TOKENS[i]) == 0) {
                bestLen = l; bestMove = nullptr; bestIdx = -1; bestSkip = true;
            }
        }
        if (bestLen == 0) break;
        if (!bestSkip && bestMove) {
            parsed[parsedCount]    = bestMove;
            parsedIdx[parsedCount] = bestIdx;
            parsedCount++;
        }
        p += bestLen;
    }

    // Resolve the focused row from the cursor band (use whichever byte moved).
    uint8_t band[6];
    if (!StRawBand(band)) return;
    if (ST_LIMIT_DIAG && s_stBandValid) {   // full band dump (mapping a new page)
        bool any = false;
        for (int i = 0; i < 6; i++) if (band[i] != s_stBand[i]) any = true;
        if (any)
            Log::Menu("[STBAND] %s %02X %02X %02X %02X %02X %02X <- %02X %02X %02X %02X %02X %02X",
                      cc->header, band[0], band[1], band[2], band[3], band[4], band[5],
                      s_stBand[0], s_stBand[1], s_stBand[2], s_stBand[3], s_stBand[4], s_stBand[5]);
    }
    int changedVal = -1;
    if (s_stBandValid) {
        if (cc->cursorByte >= 0) {          // deterministic byte (robust vs flicker)
            int b = cc->cursorByte;
            if (band[b] != s_stBand[b]) changedVal = band[b];
        } else {
            for (int i = 0; i < 6; i++)
                if (band[i] != s_stBand[i]) changedVal = band[i];
        }
    }
    if (changedVal < 0 && justLeftToggle && cc->cursorByte >= 0)
        changedVal = band[cc->cursorByte];  // arriving from a toggle: announce this row
    memcpy(s_stBand, band, 6);
    s_stBandValid = true;
    if (changedVal < 0) return;            // nothing moved this poll

    if (ST_LIMIT_DIAG) {                    // per-move cursor + help-region dump
        size_t hs = (p > start + 10) ? p - 10 : start;
        size_t he = (bound != std::string::npos) ? bound : dec.size();
        if (he > hs + 100) he = hs + 100;
        Log::Menu("[STLIMIT] %s cur=%d off=%d help=\"%.*s\"",
                  cc->header, changedVal, (int)(p - start),
                  (int)(he - hs), dec.c_str() + hs);
    }

    // Squall row +0x25F==2 is the Renzokuken Indicator. The enabled case is
    // help-caught above; reaching here means Gunblade Auto is on and the row is
    // disabled (its help isn't drawn), so announce that.
    if (cc == &ST_LIMIT_CHARS[0] && changedVal == 2 && (bits & 1)) {
        if (s_stLastRow != ST_KEY_RENZ_DISABLED) {
            s_stLastRow = ST_KEY_RENZ_DISABLED;
            ScreenReader::Speak("Renzokuken Indicator, disabled", true);
            Log::Menu("[MenuTTS] Status limit: Renzokuken Indicator disabled (Gunblade Auto on)");
            s_stSelValid = true;
            snprintf(s_stSelName, sizeof(s_stSelName), "Renzokuken Indicator");
            snprintf(s_stSelDesc, sizeof(s_stSelDesc),
                     "Disabled while Gunblade Auto is on");
        }
        return;
    }

    int moveIdx = changedVal / cc->step - cc->leadingToggles;
    if (moveIdx < 0) return;

    // Focused item's help/description = the GCW run between the parsed list and
    // the stat-panel header.
    std::string desc;
    if (p < limit) desc = dec.substr(p, limit - p);
    StTrim(desc);
    if (desc.size() > 120) desc.clear();   // sanity: not a help line

    if (moveIdx < parsedCount) {           // learned move
        if (moveIdx == s_stLastRow) return;
        s_stLastRow = moveIdx;
        // Prefer the baked description on toggle pages (the GCW help lags there);
        // otherwise use the live GCW help (correct on Rinoa/Quistis).
        const char* useDesc = desc.c_str();
        if (cc->descs && parsedIdx[moveIdx] >= 0 && parsedIdx[moveIdx] < cc->moveCount)
            useDesc = cc->descs[parsedIdx[moveIdx]];   // map learned-row -> full-list desc
        ScreenReader::Speak(parsed[moveIdx], true);
        Log::Menu("[MenuTTS] Status limit row %d/%d (cur=%d): \"%s\" desc=\"%s\" gcwHelp=\"%s\"",
                  moveIdx, parsedCount, changedVal, parsed[moveIdx], useDesc, desc.c_str());
        s_stSelValid = true;
        snprintf(s_stSelName, sizeof(s_stSelName), "%s", parsed[moveIdx]);
        snprintf(s_stSelDesc, sizeof(s_stSelDesc), "%s", useDesc);
    } else if (moveIdx < cc->moveCount) {  // unlearned slot
        int key = ST_KEY_EMPTY_BASE + moveIdx;
        if (key == s_stLastRow) return;
        s_stLastRow = key;
        ScreenReader::Speak("Empty slot", true);
        Log::Menu("[MenuTTS] Status limit row %d/%d (cur=%d): empty slot",
                  moveIdx, parsedCount, changedVal);
        s_stSelValid = true;
        snprintf(s_stSelName, sizeof(s_stSelName), "Empty slot");
        s_stSelDesc[0] = '\0';
    }
}

// ============================================================================
// #54 detail-page discovery diagnostic (pages 1-3: stats / resistances / GF compat)
// ----------------------------------------------------------------------------
// Fires on the Status DETAIL view for the three non-limit pages (focus
// +0x22E==3, page +0x257 != 3). Log-only; no speech, no writes, all reads SEH-
// isolated. No sighted step: Aaron cycles pages with L1/R1 and arrows through
// fields, and the log captures each state. Two questions for the BAT:
//   1. Which +0x257 value is which page (P1 stats / P2 resist / P3 GF compat)?
//   2. Do the font-rendered stat numbers / percentages reach the GCW buffer at
//      all (then we parse them like the limit page), or are they drawn by a
//      separate number routine that bypasses get_character_width (then we hunt
//      the computed-stats render buffer next)?
// Per Aaron, pages 1-3 have NO cursor — they are static information displays;
// L1/R1 page-flip is the only navigation. The band (+0x25F..+0x264, shared with
// the limit page) is logged only to confirm it stays put here (negative check).
// The page byte + GCW text are the real signal, and are what the existing
// every-500ms [MenuGCW] line omits. Flip to false once #54 ships.
static const bool ST_DETAIL_DIAG = false;  // per-session discovery (#54) — OFF: Page 1/3 confirmed v0.18.2.37 (Squall + Zell BAT)

static DWORD       s_stdPoll       = 0;
static int         s_stdLastPage   = -1;
static uint8_t     s_stdBand[6]    = {};
static bool        s_stdBandValid  = false;
static std::string s_stdLastText;

// Read page (+0x257) and focus (+0x22E) bytes under SEH (scalars only).
static bool StRawDetailState(uint8_t& page, uint8_t& focus)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        focus = ms[0x22E];
        page  = ms[0x257];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void PollStatusDetailDiag()
{
    if (!ST_DETAIL_DIAG) return;
    DWORD now = GetTickCount();
    if (now - s_stdPoll < 150) return;
    s_stdPoll = now;

    uint8_t page = 0, focus = 0;
    if (!StRawDetailState(page, focus)) return;
    // Detail view only, and NOT the limit page (PollStatusLimit owns +0x257==3
    // and is the only consumer that drains the GCW buffer there).
    if (focus != 3 || page == 3) {
        s_stdLastPage  = -1;
        s_stdBandValid = false;
        s_stdLastText.clear();
        return;
    }

    uint8_t band[6];
    bool haveBand = StRawBand(band);

    uint8_t gcw[2048];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    std::string dec = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();

    bool pageChanged = ((int)page != s_stdLastPage);
    bool bandChanged = false;
    if (haveBand) {
        if (!s_stdBandValid) bandChanged = true;
        else for (int i = 0; i < 6; i++) if (band[i] != s_stdBand[i]) bandChanged = true;
    }
    bool textChanged = (!dec.empty() && dec != s_stdLastText);

    if (!pageChanged && !bandChanged && !textChanged) return;

    char bandbuf[32] = "??";
    if (haveBand)
        snprintf(bandbuf, sizeof(bandbuf), "%02X %02X %02X %02X %02X %02X",
                 band[0], band[1], band[2], band[3], band[4], band[5]);

    Log::Menu("[STDETAIL] page(+0x257)=%u focus=%u band=[%s] gcwLen=%d text=\"%s\"",
              (unsigned)page, (unsigned)focus, bandbuf, len, dec.c_str());

    s_stdLastPage = (int)page;
    if (haveBand) { memcpy(s_stdBand, band, 6); s_stdBandValid = true; }
    if (!dec.empty()) s_stdLastText = dec;
}

// ============================================================================
// #54 computed-stats buffer hunt (STCALC)
// ----------------------------------------------------------------------------
// FFNx: char_comp_stats_1CFF000 = ff8_char_computed_stats[3] (stride 0x1D0),
//   curr_hp @ +0x172, max_hp @ +0x174, stat_multiplier @ +0x1B8.  This is the
// battle/active computed-stats buffer (3 slots; benched chars not covered, per
// v0.18.2.12).  character_data 0x1CFE74C is the BASE/working records, NOT the
// computed buffer.  GO/NO-GO for this BAT: while a Status detail page is open,
// does any 0x1CFF000 slot hold the VIEWED character's computed HP (matching the
// savemap values logged below), and which slot maps to the viewed character?
// If yes, page-1 stats for active members come straight from this buffer and a
// follow-up build maps the stat-field offsets via a junction-diff.  Log-only,
// SEH-isolated, no speech/writes, NO GCW snapshot (cannot interfere with
// PollStatusLimit).  Emits once per page/character change (signature-deduped).
static int s_stcSig = -1;

static void PollStatusCalcDiag()
{
    if (!ST_DETAIL_DIAG) return;

    uint8_t page = 0, focus = 0;
    if (!StRawDetailState(page, focus)) return;
    if (focus != 3) return;            // any detail page (incl. limit) is fine

    const uint32_t CMP  = 0x1CFF000;   // ff8_char_computed_stats[3]
    const int      CSTR = 0x1D0;       // stride
    const int      CHP  = 0x172, CMHP = 0x174, CMUL = 0x1B8;
    const uint32_t SMB  = 0x1CFDC5C;   // savemap base
    const uint32_t CHRS = 0x48C;       // 0x4C header + 0x440 GF block (76B-header)
    const int      ESZ  = 0x98;        // char struct stride

    // Signature = page + the 3 computed slots' cur HP -> emit on change only.
    int sig = (int)page;
    __try {
        for (int s = 0; s < 3; s++)
            sig = sig * 131 + *(uint16_t*)(CMP + s * CSTR + CHP);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (sig == s_stcSig) return;
    s_stcSig = sig;

    // (a) savemap base records: stored HP/maxHP (maxHP already includes HP-J)
    //     plus the pre-junction base stats -> ground truth to match against.
    __try {
        for (int c = 0; c < 8; c++) {
            uint8_t* ch = (uint8_t*)(SMB + CHRS + c * ESZ);
            Log::Menu("[STCALC] base[%d] model=%u HP=%u/%u exp=%u STR=%u VIT=%u MAG=%u SPR=%u SPD=%u LCK=%u",
                      c, (unsigned)ch[0x08],
                      (unsigned)*(uint16_t*)(ch + 0x00), (unsigned)*(uint16_t*)(ch + 0x02),
                      *(uint32_t*)(ch + 0x04),
                      (unsigned)ch[0x0A], (unsigned)ch[0x0B], (unsigned)ch[0x0C],
                      (unsigned)ch[0x0D], (unsigned)ch[0x0E], (unsigned)ch[0x0F]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // (b) computed buffer 0x1CFF000 slots 0-2: is it live in-menu? which slot?
    for (int s = 0; s < 3; s++) {
        uint32_t b = CMP + s * CSTR;
        unsigned cur = 0xFFFF, mx = 0xFFFF, mul = 0xFF;
        __try {
            cur = *(uint16_t*)(b + CHP); mx = *(uint16_t*)(b + CMHP); mul = *(uint8_t*)(b + CMUL);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        Log::Menu("[STCALC] comp[%d] @%08X curHP=%u maxHP=%u mult=%u", s, b, cur, mx, mul);
    }

    // (c) viewed-char hint: char-select cursor (+0x1E9) + roster slot 0 (+0x1DB).
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        Log::Menu("[STCALC] page=%u curSel(+0x1E9)=%u roster0(+0x1DB)=%u",
                  (unsigned)page, (unsigned)ms[0x1E9], (unsigned)ms[0x1DB]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// #54 Status detail PAGE 1 (Character Statistics) + PAGE 3 (GF Compatibility)
// ----------------------------------------------------------------------------
// Production reads. The page numbers bypass the GCW buffer, so we read memory,
// not text. Page-1 stats come from the computed-stats buffer
// ff8_char_computed_stats (0x1CFF000, stride 0x1D0); the screen recomputes the
// VIEWED character into slot 0 (confirmed Squall; benched re-verify). Field
// offsets were resolved by disassembling compute_char_stats_sub_495960 (#54):
//   curHP w+0x172, maxHP w+0x174, level b+0x1B8, STR b+0x1BB, VIT +0x1BC,
//   MAG +0x1BD, SPR +0x1BE, SPD +0x1BF, LUCK +0x1C0, EVA +0x1C1, HIT +0x1C2.
// Page-3 reads the savemap directly (reusing the GF module's verified model):
//   GF obtained = GF rec[+0x11]!=0; name via DecodeGFName; compatibility =
//   (6000 - char[+0x70 + gf*2]) / 5; junctioned-to-this-char = char[+0x58] bit
//   gf. Per #44 every value is spoken with its label. Pages are static (no
//   cursor), so per-field reads are mod-side number-key hotkeys gated to the
//   page. Equipment (Page-1 key 9) is deferred to a later build.
static const uint32_t ST_COMP_BASE   = 0x1CFF000;
static const int      ST_COMP_STRIDE = 0x1D0;
static const int      ST_C_CURHP = 0x172, ST_C_MAXHP = 0x174, ST_C_LEVEL = 0x1B8;
static const int      ST_C_STR = 0x1BB, ST_C_VIT = 0x1BC, ST_C_MAG = 0x1BD, ST_C_SPR = 0x1BE,
                      ST_C_SPD = 0x1BF, ST_C_LCK = 0x1C0, ST_C_EVA = 0x1C1, ST_C_HIT = 0x1C2;
static const int      ST_GF_COMPAT_OFF = 0x70;   // char+0x70 u16 per GF
static const int      ST_CHR_GF_JUNC   = 0x58;   // char+0x58 u16 junctioned-GF bitmask

// --- Page 2 (Elemental & Status) computed-slot offsets (confirmed #54 BATs
// v0.18.2.43-.44): elem-def 8 LE words +0x194 (% = word - 800, neutral 800;
// Ice 824 = 24% verified); status-def 13 bytes +0x1A4 (% = byte - 100, neutral
// 100; 0/4/66% verified); elem-atk element bitmask +0x1C4 + % +0x1C5 (Thunder
// 0x04/43, Water 0x40/100, Ice 0x02/80 verified). Status-atk % is NOT cached;
// its source magic id is the savemap char+0x66. ---
static const int      ST_C_ELEM_DEF   = 0x194;   // 8x u16 (element order below)
static const int      ST_C_STATUS_DEF = 0x1A4;   // 13x u8
static const int      ST_C_EATK_MASK  = 0x1C4;   // u8 element bitmask
static const int      ST_C_EATK_PCT   = 0x1C5;   // u8 percent
static const int      ST_ELEM_DEF_NEUTRAL   = 800;
static const int      ST_STATUS_DEF_NEUTRAL = 100;
static const int      ST_STATK_MAGIC_OFF    = 0x66;  // savemap char+0x66 ST-Atk magic id

// Element order of BOTH the +0x194 defense words and the +0x1C4 attack bitmask
// (bit i = element i): confirmed via attack bitmask (Ice 0x02 / Thunder 0x04 /
// Water 0x40) and defense (Ice = index 1, word +0x196).
static const char* ST_ELEM_NAMES[8] = {
    "Fire", "Ice", "Thunder", "Earth", "Poison", "Wind", "Water", "Holy"
};
// Status-defense order of the 13-byte +0x1A4 array (index 5 = Berserk confirmed
// via magic 46 def junction = 66% at index 5).
static const char* ST_STATUS_NAMES[13] = {
    "Death", "Poison", "Petrify", "Darkness", "Silence", "Berserk", "Zombie",
    "Sleep", "Slow", "Stop", "Curse", "Confuse", "Drain"
};

static const char* ST_DREAM_NAMES[3] = { "Laguna", "Kiros", "Ward" };

// FF8 magic id -> spell name (kernel magic order; ids 0-56). Validated against
// our own data: id 7 = Thunder (Elem-Atk) and id 46 = Berserk (ST-Atk, char+0x66)
// both match, and every id seen in the magic-inventory dump maps to a real spell.
// Used to name the junctioned Status-Attack spell (key 3). Naming the SPELL the
// player junctioned (not the inflicted status) keeps it exact and self-verifiable;
// status-icon names (Blind->Darkness, Break->Petrify, Pain->multi) are a possible
// later refinement, deliberately avoided here to not ship an unsourced mapping.
static const char* ST_MAGIC_NAMES[] = {
    "None", "Fire", "Fira", "Firaga", "Blizzard", "Blizzara", "Blizzaga",
    "Thunder", "Thundara", "Thundaga", "Water", "Aero", "Bio", "Demi", "Holy",
    "Flare", "Meteor", "Quake", "Tornado", "Ultima", "Apocalypse", "Cure",
    "Cura", "Curaga", "Life", "Full-Life", "Regen", "Esuna", "Dispel",
    "Protect", "Shell", "Reflect", "Aura", "Double", "Triple", "Haste", "Slow",
    "Stop", "Blind", "Confuse", "Sleep", "Silence", "Break", "Death", "Drain",
    "Pain", "Berserk", "Float", "Zombie", "Meltdown", "Scan", "Full-Cure",
    "Wall", "Rapture", "Percent", "Catastrophe", "The End"
};
static const int ST_MAGIC_NAME_COUNT = (int)(sizeof(ST_MAGIC_NAMES) / sizeof(ST_MAGIC_NAMES[0]));

static int   s_stpPage = -1;     // current detail page: 0 stats / 2 GF compat / -1 none
static int   s_stpView = -1;     // resolved savemap char index of the viewed character
static DWORD s_stpPoll = 0;

static void ResetStatusDetailPages() { s_stpPage = -1; s_stpView = -1; s_stpPoll = 0; }

// --- SEH-isolated scalar reads (C2712-safe; no C++ objects in frame) ---
static int StCompByte(int slot, int off) {
    __try { return *(uint8_t*)(ST_COMP_BASE + slot * ST_COMP_STRIDE + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int StCompWord(int slot, int off) {
    __try { return *(uint16_t*)(ST_COMP_BASE + slot * ST_COMP_STRIDE + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int StCharModelSEH(int c) {
    __try {
        if (c < 0 || c >= CHAR_COUNT) return -1;
        return *(uint8_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + CHR_MODEL_ID);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int StCharExpSEH(int c) {
    __try {
        if (c < 0 || c >= CHAR_COUNT) return -1;
        return (int)*(uint32_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + CHR_EXP);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// Roster-based viewed-char index: char-select cursor (+0x1E9) indexes the menu
// roster (+0x1DB). Returns savemap char index 0..7, or -1.
static int StViewedCharRoster() {
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (!ms) return -1;
        uint8_t idx = ms[0x1DB + ms[0x1E9]];
        return (idx < CHAR_COUNT) ? (int)idx : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// Cross-check: savemap char whose current HP equals computed slot-0 curHP (the
// screen computes the viewed char into slot 0). Independent of the roster.
static int StViewedCharByHP() {
    __try {
        uint16_t s0 = *(uint16_t*)(ST_COMP_BASE + ST_C_CURHP);
        if (!s0) return -1;
        for (int c = 0; c < CHAR_COUNT; c++) {
            uint8_t* ch = (uint8_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE);
            if (ch[CHR_EXISTS] && *(uint16_t*)(ch + CHR_CURR_HP) == s0) return c;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}
// Pick the computed slot holding the viewed char (match its savemap curHP); fall
// back to slot 0 (the confirmed viewed-char slot).
static int StFindCompSlot(int c) {
    __try {
        if (c >= 0 && c < CHAR_COUNT) {
            uint16_t hp = *(uint16_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + CHR_CURR_HP);
            if (hp) for (int s = 0; s < 3; s++)
                if (*(uint16_t*)(ST_COMP_BASE + s * ST_COMP_STRIDE + ST_C_CURHP) == hp) return s;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
static int StGFObtained(int g) {
    __try {
        if (g < 0 || g >= GF_REC_COUNT) return 0;
        return ((uint8_t*)(SAVEMAP_BASE + GF_BLOCK_OFFSET + g * GF_RECORD_SIZE))[0x11] ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int StGFCompatDisp(int c, int g) {
    __try {
        uint16_t raw = *(uint16_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + ST_GF_COMPAT_OFF + g * 2);
        return (6000 - (int)raw) / 5;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int StGFJunctioned(int c, int g) {
    __try {
        uint16_t bm = *(uint16_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + ST_CHR_GF_JUNC);
        return (bm >> g) & 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Page-2 key 3: the ST-Atk magic id junctioned on this char (savemap +0x66; 0 = none).
static int StStatAtkMagic(int c) {
    __try {
        if (c < 0 || c >= CHAR_COUNT) return -1;
        return *(uint8_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + ST_STATK_MAGIC_OFF);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Stock count of magic id m for character c, read from the per-character magic
// inventory (FFNx savemap_ff8_character.magics[32] at char+0x10): 32 u16 slots,
// each packing id in the low byte and quantity in the high byte (matches the
// magic-inventory layout seen in the computed-slot dump: Berserk record id 0x2E,
// qty 0x42). The inventory is slot-ordered (not id-indexed), so scan all 32.
// For ST-Atk-J the displayed infliction percent IS this count (1% per spell,
// capped at 100 — confirmed via Fandom/Neoseeker/GameFAQs). Returns 0..100, or
// -1 on error. (Byte packing confirmed by the key-3 log on BAT.)
static int StMagicStock(int c, int m) {
    __try {
        if (c < 0 || c >= CHAR_COUNT || m <= 0) return -1;
        uint16_t* mag = (uint16_t*)(SAVEMAP_BASE + CHARS_OFFSET + c * CHAR_STRUCT_SIZE + 0x10);
        for (int i = 0; i < 32; i++) {
            if ((mag[i] & 0xFF) == (unsigned)m) {
                int cnt = (mag[i] >> 8) & 0xFF;
                return (cnt > 100) ? 100 : cnt;
            }
        }
        return 0;   // junctioned but none in stock
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Name of the viewed character (default names; renamed Squall/Rinoa fall back to
// the default -- a v2 refinement). No std::string / __try in this frame.
static void StViewedCharName(char* out, int outSize, int c) {
    int m = StCharModelSEH(c);
    if (m >= 0 && m < 8)        snprintf(out, outSize, "%s", CHAR_NAMES[m]);
    else if (m >= 8 && m <= 10) snprintf(out, outSize, "%s", ST_DREAM_NAMES[m - 8]);
    else                        snprintf(out, outSize, "Character");
}

// Page-1 number key: speak one labeled field from the computed buffer.
static void StSpeakStat(int key, int view) {
    int slot = StFindCompSlot(view);
    char buf[160];
    if (key == 0) {
        char nm[32]; StViewedCharName(nm, sizeof(nm), view);
        int lvl = StCompByte(slot, ST_C_LEVEL);
        int hp = StCompWord(slot, ST_C_CURHP), mhp = StCompWord(slot, ST_C_MAXHP);
        snprintf(buf, sizeof(buf), "%s, Level %d, HP %d of %d", nm, lvl, hp, mhp);
    } else if (key == 1) {
        int exp = StCharExpSEH(view);
        int lvl = StCompByte(slot, ST_C_LEVEL);
        if (exp < 0) {
            snprintf(buf, sizeof(buf), "Experience unavailable");
        } else if (lvl >= 1 && lvl < 100) {
            // FF8 characters take a flat 1000 EXP per level, so the next-level
            // threshold is lvl*1000 (verified on-screen: Squall L20 / 19690 EXP
            // shows "Next LEVEL 310" = 20*1000 - 19690).
            int toNext = lvl * 1000 - exp;
            if (toNext < 0) toNext = 0;
            snprintf(buf, sizeof(buf), "Experience %d. %d to next level.", exp, toNext);
        } else if (lvl >= 100) {
            snprintf(buf, sizeof(buf), "Experience %d. Maximum level.", exp);
        } else {
            snprintf(buf, sizeof(buf), "Experience %d", exp);
        }
    } else if (key == 8) {
        snprintf(buf, sizeof(buf), "Evade %d, Hit %d",
                 StCompByte(slot, ST_C_EVA), StCompByte(slot, ST_C_HIT));
    } else {
        const char* lbl; int off;
        switch (key) {
            case 2: lbl = "Strength"; off = ST_C_STR; break;
            case 3: lbl = "Vitality"; off = ST_C_VIT; break;
            case 4: lbl = "Magic";    off = ST_C_MAG; break;
            case 5: lbl = "Spirit";   off = ST_C_SPR; break;
            case 6: lbl = "Speed";    off = ST_C_SPD; break;
            case 7: lbl = "Luck";     off = ST_C_LCK; break;
            default: return;
        }
        snprintf(buf, sizeof(buf), "%s %d", lbl, StCompByte(slot, off));
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE1] key=%d view=%d slot=%d: %s", key, view, slot, buf);
}

// Page-3 key 0: enumerate obtained GFs with this character's compatibility and a
// junctioned marker. char[] only (no __try here); DecodeGFName reads the savemap
// directly per the GF module's established pattern.
static void StSpeakGFCompat(int view) {
    if (view < 0 || view >= CHAR_COUNT) { ScreenReader::Speak("No character selected", true); return; }
    char out[1024]; int p = 0, n = 0;
    for (int g = 0; g < GF_REC_COUNT; g++) {
        if (!StGFObtained(g)) continue;
        char nm[32] = {}; DecodeGFName(g, nm, sizeof(nm));
        if (nm[0] == '\0') continue;
        p += snprintf(out + p, sizeof(out) - p, "%s%s %d%s",
                      (n ? ". " : ""), nm, StGFCompatDisp(view, g),
                      StGFJunctioned(view, g) ? ", junctioned" : "");
        n++;
        if (p > (int)sizeof(out) - 64) break;
    }
    if (!n) snprintf(out, sizeof(out), "No GFs obtained");
    ScreenReader::Speak(out, true);
    Log::Menu("[STPAGE3] view=%d gfs=%d: %s", view, n, out);
}

// The complete set of FF8 command abilities (verified against the Final Fantasy
// VIII command-ability list) plus the innate "Attack". The status COMMAND box
// holds Attack plus up to three equipped commands, so the weapon always follows
// this run. There are exactly 18 command abilities: Absorb, Card, Darkside,
// Defend, Devour, Doom, Draw, GF, Item, Kamikaze, LV Down, LV Up, Mad Rush,
// Magic, MiniMog, Recover, Revive, Treatment. (Mug / Med Data / Junk Shop are
// NOT commands: Mug isn't in FF8; the latter two are Menu abilities.) Both
// spacing/hyphen variants are listed for the few multi-word names whose exact
// in-game rendering is uncertain; extra never-match variants are harmless and
// longest-match in the loop handles overlaps.
static const char* ST_CMD_TOKENS[] = {
    "Attack",
    "Absorb", "Card", "Darkside", "Defend", "Devour", "Doom", "Draw", "GF",
    "Item", "Kamikaze", "Magic", "Recover", "Revive", "Treatment",
    "Mad Rush", "Mad-Rush", "LV Up", "LV-Up", "LV Down", "LV-Down",
    "MiniMog", "Mini-Mog"
};
static const int ST_CMD_TOKEN_COUNT = (int)(sizeof(ST_CMD_TOKENS) / sizeof(ST_CMD_TOKENS[0]));

// Page-1 key 9: equipped weapon (FF8 has no armor/accessories, so the weapon is
// the whole of "equipment"). The page-1 panel renders as
//   <menubar><Name>LVHP/<commands><Weapon>
// and the command list is junction-dependent, so we isolate one panel between
// two menu-bar repeats, skip the command tokens after the innate "Attack", and
// take the trailing run as the weapon name. Reads the game's own rendered name,
// so it always matches the screen. Uses std::string (no SEH frame here).
static void StSpeakEquipment(int view) {
    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    std::string dec = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();
    static const std::string BAR = "MagicStatusGFAbilitySwitchCardConfigTutorialSave";
    char buf[160];

    size_t a = dec.find(BAR);
    if (a == std::string::npos) {
        snprintf(buf, sizeof(buf), "Weapon unavailable");
        ScreenReader::Speak(buf, true);
        Log::Menu("[STPAGE1] key=9 view=%d: no panel (gcwLen=%d)", view, len);
        return;
    }
    size_t ps = a + BAR.size();
    size_t b  = dec.find(BAR, ps);
    std::string panel = dec.substr(ps, (b == std::string::npos ? dec.size() : b) - ps);

    std::string weapon;
    size_t atk = panel.find("Attack");      // innate first command; weapon follows the run
    if (atk != std::string::npos) {
        size_t p = atk;
        while (p < panel.size()) {
            size_t bestLen = 0;
            for (int i = 0; i < ST_CMD_TOKEN_COUNT; i++) {
                size_t l = strlen(ST_CMD_TOKENS[i]);
                if (l > bestLen && p + l <= panel.size() &&
                    panel.compare(p, l, ST_CMD_TOKENS[i]) == 0)
                    bestLen = l;
            }
            if (bestLen == 0) break;        // not a command token -> weapon starts here
            p += bestLen;
        }
        weapon = panel.substr(p);
        size_t s0 = weapon.find_first_not_of(' ');
        size_t s1 = weapon.find_last_not_of(' ');
        if (s0 == std::string::npos) weapon.clear();
        else weapon = weapon.substr(s0, s1 - s0 + 1);
    }

    if (weapon.empty() || weapon.size() > 40)
        snprintf(buf, sizeof(buf), "Weapon unavailable");
    else
        snprintf(buf, sizeof(buf), "Weapon, %s", weapon.c_str());
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE1] key=9 view=%d: %s (panel=\"%.60s\")", view, buf, panel.c_str());
}

// ----------------------------------------------------------------------------
// #54 Status detail PAGE 2 (Elemental & Status) production reads.
// Key 1 Elemental Attack (+0x1C4 bitmask / +0x1C5 %); Key 2 Elemental Resistance
// (8 words +0x194, % = word - 800); Key 4 Status Resistance (13 bytes +0x1A4,
// % = byte - 100). Key 3 Status Attack reads the junctioned magic id (char+0x66);
// its status name + % aren't cached in the computed buffer (a magic->status
// table is the pending refinement), so v1 reports presence only. Per #44 every
// value is spoken with its label. Slot resolved via StFindCompSlot.
static void StSpeakElemAtk(int view) {
    int slot = StFindCompSlot(view);
    int mask = StCompByte(slot, ST_C_EATK_MASK);
    int pct  = StCompByte(slot, ST_C_EATK_PCT);
    if (pct < 0) pct = 0;
    char buf[192];
    if (mask <= 0) {
        snprintf(buf, sizeof(buf), "No elemental attack junctioned");
    } else {
        char els[120]; int p = 0; int n = 0;
        for (int i = 0; i < 8; i++) if (mask & (1 << i)) {
            p += snprintf(els + p, sizeof(els) - p, "%s%s", (n ? ", " : ""), ST_ELEM_NAMES[i]);
            n++;
        }
        snprintf(buf, sizeof(buf), "Elemental attack, %s, %d percent", els, pct);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE2] key=1 view=%d slot=%d: %s", view, slot, buf);
}
static void StSpeakElemDef(int view) {
    int slot = StFindCompSlot(view);
    char list[480]; int p = 0; int n = 0;
    for (int i = 0; i < 8; i++) {
        int w = StCompWord(slot, ST_C_ELEM_DEF + i * 2);
        if (w < 400 || w > 1400) continue;            // unread / implausible
        int pct = w - ST_ELEM_DEF_NEUTRAL;            // FF8 elem-def: 0 normal, 100 immune, >100 absorb
        if (pct == 0) continue;                       // neutral: normal damage, nothing to announce
        if (pct == 100) {                             // exactly 100 = full immunity (no damage taken)
            p += snprintf(list + p, sizeof(list) - p, "%s%s immune",
                          (n ? ", " : ""), ST_ELEM_NAMES[i]);
        } else if (pct > 100) {                       // >100 = absorb the overflow: 120 -> "absorbs 20 percent"
            p += snprintf(list + p, sizeof(list) - p, "%s%s absorbs %d percent",
                          (n ? ", " : ""), ST_ELEM_NAMES[i], pct - 100);
        } else {                                      // <100: positive = resist, negative = weak
            const char* kind = (pct > 0) ? "resists" : "weak";
            int mag = (pct > 0) ? pct : -pct;
            p += snprintf(list + p, sizeof(list) - p, "%s%s %s %d percent",
                          (n ? ", " : ""), ST_ELEM_NAMES[i], kind, mag);
        }
        n++;
    }
    char buf[560];
    if (!n) snprintf(buf, sizeof(buf), "No elemental resistances or weaknesses");
    else    snprintf(buf, sizeof(buf), "Elemental: %s", list);
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE2] key=2 view=%d slot=%d: %s", view, slot, buf);
}
static void StSpeakStatusAtk(int view) {
    int mag = StStatAtkMagic(view);
    char buf[192];
    int stock = -1;
    if (mag <= 0) {
        snprintf(buf, sizeof(buf), "No status attack junctioned");
    } else {
        const char* nm = (mag >= 0 && mag < ST_MAGIC_NAME_COUNT) ? ST_MAGIC_NAMES[mag] : "Unknown";
        stock = StMagicStock(view, mag);
        int pct = (stock < 0) ? 0 : stock;   // ST-Atk infliction % = spells in stock
        snprintf(buf, sizeof(buf), "Status attack, %s, %d percent", nm, pct);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE2] key=3 view=%d: magicId=%d stock=%d %s", view, mag, stock, buf);
}
static void StSpeakStatusDef(int view) {
    int slot = StFindCompSlot(view);
    char list[720]; int p = 0; int n = 0;
    for (int i = 0; i < 13; i++) {
        int b = StCompByte(slot, ST_C_STATUS_DEF + i);
        if (b < 0) continue;
        int pct = b - ST_STATUS_DEF_NEUTRAL;
        if (pct == 0) continue;                       // neutral
        if (pct >= 100) {                             // status caps at immune (no absorb for status)
            p += snprintf(list + p, sizeof(list) - p, "%s%s immune",
                          (n ? ", " : ""), ST_STATUS_NAMES[i]);
        } else {
            const char* kind = (pct > 0) ? "resist" : "weak";
            int mag = (pct > 0) ? pct : -pct;
            p += snprintf(list + p, sizeof(list) - p, "%s%s %s %d percent",
                          (n ? ", " : ""), ST_STATUS_NAMES[i], kind, mag);
        }
        n++;
    }
    char buf[800];
    if (!n) snprintf(buf, sizeof(buf), "No status resistances or weaknesses");
    else    snprintf(buf, sizeof(buf), "Status: %s", list);
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE2] key=4 view=%d slot=%d: %s", view, slot, buf);
}

// Number-key dispatch for the status detail pages (gated to pages 0 / 1 / 2).
static void StatusDetailHotkeys() {
    if (s_stpPage != 0 && s_stpPage != 1 && s_stpPage != 2) return;
    int view = (s_stpView >= 0) ? s_stpView : StViewedCharRoster();
    if (s_stpPage == 0) {
        for (int k = 0; k <= 8; k++)
            if (GetAsyncKeyState('0' + k) & 1) StSpeakStat(k, view);
        if (GetAsyncKeyState('9') & 1) StSpeakEquipment(view);
    } else if (s_stpPage == 1) {
        if (GetAsyncKeyState('1') & 1) StSpeakElemAtk(view);
        if (GetAsyncKeyState('2') & 1) StSpeakElemDef(view);
        if (GetAsyncKeyState('3') & 1) StSpeakStatusAtk(view);
        if (GetAsyncKeyState('4') & 1) StSpeakStatusDef(view);
    } else {
        if (GetAsyncKeyState('0') & 1) StSpeakGFCompat(view);
    }
}

// Detect entry onto Page 1 (stats) or Page 3 (GF compat) and announce the page +
// viewed character + key hint. Resolves the viewed char (roster primary; logs an
// HP-match cross-check for the slot/char verification BAT). Throttled.
static void PollStatusDetailPages() {
    DWORD now = GetTickCount();
    if (now - s_stpPoll < 120) return;
    s_stpPoll = now;

    uint8_t page = 0, focus = 0;
    if (!StRawDetailState(page, focus) || focus != 3 || (page != 0 && page != 1 && page != 2)) {
        s_stpPage = -1; s_stpView = -1;
        return;
    }
    int view   = StViewedCharRoster();
    int viewHP = StViewedCharByHP();
    if (view < 0) view = viewHP;

    bool entered = ((int)page != s_stpPage) || (view != s_stpView);
    s_stpPage = (int)page;
    s_stpView = view;
    if (!entered) return;

    char nm[32]; StViewedCharName(nm, sizeof(nm), view);
    char buf[192];
    if (page == 0) {
        int slot = StFindCompSlot(view);
        int lvl  = StCompByte(slot, ST_C_LEVEL);
        if (lvl > 0)
            snprintf(buf, sizeof(buf),
                     "Character Statistics. %s, Level %d. Keys 0 overview, 1 experience, 2 to 7 stats, 8 evade and hit, 9 weapon.",
                     nm, lvl);
        else
            snprintf(buf, sizeof(buf),
                     "Character Statistics. %s. Keys 0 overview, 1 experience, 2 to 7 stats, 8 evade and hit, 9 weapon.", nm);
    } else if (page == 1) {
        snprintf(buf, sizeof(buf),
                 "Elemental and Status. %s. Keys 1 elemental attack, 2 elemental resistance, 3 status attack, 4 status resistance.", nm);
    } else {
        snprintf(buf, sizeof(buf), "GF Compatibility. %s. Press 0 to list compatibility.", nm);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[STPAGE] page=%u view=%d(roster) hpMatch=%d name=%s", (unsigned)page, view, viewHP, nm);
}

// ============================================================================
// #54 Status detail PAGE 2 (Elemental & Status) discovery diagnostic
// ----------------------------------------------------------------------------
// Page 2 (+0x257==1) shows an elemental-attack element + 8 elemental-defense %s
// and a status-attack status + ~13 status-defense %s. The displayed numbers
// bypass the GCW buffer (confirmed v0.18.2.35), and FFNx leaves the elem/status
// arrays unnamed inside ff8_char_computed_stats.unk1[370] (struct offsets
// 0x000..0x171 of the 0x1CFF000 buffer). This dump pairs the savemap junction
// INPUTS (char+0x65..0x6E magic IDs) with a hex dump of the viewed character's
// computed-stats slot so that a Page-2 F11 screenshot can be correlated against
// it to locate the per-element / per-status arrays and their scale. If the
// arrays AREN'T in this buffer, the dump shows only the known stat region
// populated -> fall back to replicating from kernel Magic Data. Log-only,
// SEH-isolated, no speech / writes, NO GCW snapshot. Emits once per
// page/character/junction change (signature-deduped).
static const bool ST_PAGE2_DIAG = false;  // discovery COMPLETE (v0.18.2.50): status-ATTACK solved. % = stock count of the junctioned ST-Atk spell (char+0x66 id, count from magics[32] @char+0x10); name from ST_MAGIC_NAMES. Production read live in StSpeakStatusAtk; [STPAGE2] key=3 log shows magicId+stock for verification.
static int s_st2Sig = -1;

// Read n bytes from an absolute address under SEH (scalars only, C2712-safe).
static bool StReadBytesSEH(uint32_t addr, uint8_t* out, int n) {
    __try { for (int i = 0; i < n; i++) out[i] = *(uint8_t*)(addr + i); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void PollStatusPage2Diag() {
    if (!ST_PAGE2_DIAG) return;
    uint8_t page = 0, focus = 0;
    if (!StRawDetailState(page, focus)) return;
    if (focus != 3 || page != 1) return;          // Page 2 only

    int view = StViewedCharRoster();
    if (view < 0) view = StViewedCharByHP();
    if (view < 0 || view >= CHAR_COUNT) return;
    int slot = StFindCompSlot(view);

    // Junction INPUTS from the savemap char struct (+0x65..0x6E): Elem-Atk,
    // ST-Atk, 4x Elem-Def, 4x ST-Def (all magic IDs; 0 = none).
    uint32_t chBase = SAVEMAP_BASE + CHARS_OFFSET + view * CHAR_STRUCT_SIZE;
    uint8_t junc[10] = {};
    StReadBytesSEH(chBase + 0x65, junc, 10);

    int sig = view * 131 + slot;
    for (int i = 0; i < 10; i++) sig = sig * 31 + junc[i];
    if (sig == s_st2Sig) return;
    s_st2Sig = sig;

    char nm[32]; StViewedCharName(nm, sizeof(nm), view);
    Log::Menu("[ST2DIAG] page=1 view=%d slot=%d name=%s", view, slot, nm);
    Log::Menu("[ST2DIAG] junc atk_ele=%u atk_st=%u def_ele=[%u %u %u %u] def_st=[%u %u %u %u]",
              junc[0], junc[1], junc[2], junc[3], junc[4], junc[5], junc[6], junc[7], junc[8], junc[9]);

    // Hex-dump the viewed char's computed-stats slot, non-zero 16-byte rows only
    // (a fully-zero row holds no array entry, so skipping them loses nothing).
    uint32_t base = ST_COMP_BASE + slot * ST_COMP_STRIDE;
    Log::Menu("[ST2DIAG] comp slot %d @%08X (non-zero rows):", slot, base);
    for (int off = 0; off < ST_COMP_STRIDE; off += 16) {
        int n = (off + 16 <= ST_COMP_STRIDE) ? 16 : (ST_COMP_STRIDE - off);
        uint8_t row[16] = {};
        if (!StReadBytesSEH(base + off, row, n)) continue;
        bool any = false;
        for (int i = 0; i < n; i++) if (row[i]) { any = true; break; }
        if (!any) continue;
        char hex[64]; int p = 0;
        for (int i = 0; i < n; i++) p += snprintf(hex + p, sizeof(hex) - p, "%02X ", row[i]);
        Log::Menu("[ST2DIAG]  +0x%03X: %s", off, hex);
    }
}

// ============================================================================
// #54 key-3 Status-Attack: kernel Magic-Data base discovery (STMAGSCAN)
// ----------------------------------------------------------------------------
// Key 3 needs each spell's status name + status-attack % from the kernel.bin
// Magic table. That table isn't cached in the computed buffer, and FFNx names
// only the battle EFFECT loaders (\Data\Magic\ / dav_aoy), not this stat table.
// This ONE-SHOT scan locates the table by signature, anchored on values we
// already confirmed this chapter:
//   - magic 46 = Berserk, status-attack 66 (0x42)
//   - magic 7  = Thunder, elem-atk-J 43 (0x2B)
// FF8 magic entries are 0x3C bytes and the array is indexed by magic id. We look
// for a 0x3C-stride run whose per-entry magic-id field counts up (0,1,2,... or
// 1,2,3,...) OR whose leading name-offset u16 rises monotonically, AND where
// entry[46] contains 0x42 (content anchor). On a hit it logs the base + dumps
// entries 0/1/7/27/46 in full so we can READ the true field offsets (the
// status-attack byte + the status bitmask) rather than assume them. Scans only
// committed/readable regions via VirtualQuery (ascending, stops at first hit);
// SEH-guarded; log-only, no speech/writes; gated to Page 2 so it can't hitch
// elsewhere. Flip ST_MAGSCAN false once the base is found.
static const bool ST_MAGSCAN = false;  // SHELVED: 3 scan variants each false-matched or found nothing — the in-memory magic table isn't a flat id-indexed 0x3C array. Pivoted to the computed-slot dump (ST_PAGE2_DIAG) to locate the status-atk fields, the method that cracked the rest of Page 2.
static bool s_magScanDone = false;
static const int ST_MAG_STRIDE = 0x3C;

// Strong structural test: the u16 magic-id field at +0x04 must equal the entry
// index across a long run (48 entries). Tries 0-based (id==i) then 1-based
// (id==i+1). A single coincidental byte is common; 48 consecutive u16==index
// values at 0x3C stride is unique to the real Magic table. Reads run inside the
// caller's per-region __try. Returns 1 (0-based), 2 (1-based), or 0.
// (v3: v2's u8@+0x04 + two-entry shortcut matched a curve/lookup table at
// 0x00D497D4; this full u16 run rejects it on entry 0.)
static int StMagSeqMatch(uint32_t b) {
    const int N = 48;
    bool ok0 = true, ok1 = true;
    for (int i = 0; i < N; i++) {
        uint16_t v = *(uint16_t*)(b + i * ST_MAG_STRIDE + 4);
        if (v != (uint16_t)i)       ok0 = false;
        if (v != (uint16_t)(i + 1)) ok1 = false;
        if (!ok0 && !ok1) return 0;
    }
    return ok0 ? 1 : (ok1 ? 2 : 0);
}

// Content anchor: does entry[id] contain byte val anywhere in its 0x3C bytes?
static bool StMagEntryHas(uint32_t base, int id, uint8_t val) {
    uint32_t e = base + id * ST_MAG_STRIDE;
    for (int i = 0; i < ST_MAG_STRIDE; i++) if (*(uint8_t*)(e + i) == val) return true;
    return false;
}

static void PollStatusMagScan() {
    if (!ST_MAGSCAN || s_magScanDone) return;
    uint8_t page = 0, focus = 0;
    if (!StRawDetailState(page, focus)) return;
    if (focus != 3 || page != 1) return;          // Page 2 only
    s_magScanDone = true;                          // one-shot regardless of outcome

    DWORD t0 = GetTickCount();
    Log::Menu("[STMAGSCAN] begin (anchors: magic46 has 0x42, magic7 has 0x2B; stride 0x3C)");

    const uint32_t SCAN_BEG = 0x00400000u, SCAN_END = 0x40000000u;
    const uint32_t NEED = 48u * ST_MAG_STRIDE;     // must cover the 48-entry run
    uint32_t foundBase = 0; int foundSeq = -1;

    MEMORY_BASIC_INFORMATION mbi;

    // PASS 1 (strict): u16 id field at +0x04 == entry index across 48 entries.
    uint32_t addr = SCAN_BEG;
    while (addr < SCAN_END && !foundBase) {
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) break;
        uint32_t regBase = (uint32_t)mbi.BaseAddress;
        uint32_t regSize = (uint32_t)mbi.RegionSize;
        uint32_t next = regBase + regSize;
        if (next <= regBase) break;
        bool readable = (mbi.State == MEM_COMMIT) &&
                        ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0) &&
                        ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                         PAGE_EXECUTE_WRITECOPY)) != 0);
        if (readable && regSize >= NEED) {
            uint32_t s = (regBase < addr ? addr : regBase);
            s = (s + 3u) & ~3u;
            uint32_t e = next - NEED;
            __try {
                for (uint32_t b = s; b <= e; b += 4) {
                    int seq = StMagSeqMatch(b);
                    if (!seq) continue;
                    foundBase = b; foundSeq = seq;
                    break;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) { /* skip faulting region */ }
        }
        addr = next;
    }

    unsigned dt = (unsigned)(GetTickCount() - t0);

    if (foundBase) {
        // value-byte anchors are informational only (the 48-entry run already
        // identifies the table); they confirm Thunder/Berserk land where expected.
        uint8_t a7[ST_MAG_STRIDE] = {}, a46[ST_MAG_STRIDE] = {};
        int has7 = 0, has46 = 0;
        if (StReadBytesSEH(foundBase + 7 * ST_MAG_STRIDE, a7, ST_MAG_STRIDE))
            for (int i = 0; i < ST_MAG_STRIDE; i++) if (a7[i] == 0x2B) { has7 = 1; break; }
        if (StReadBytesSEH(foundBase + 46 * ST_MAG_STRIDE, a46, ST_MAG_STRIDE))
            for (int i = 0; i < ST_MAG_STRIDE; i++) if (a46[i] == 0x42) { has46 = 1; break; }
        Log::Menu("[STMAGSCAN] STRONG base=0x%08X seq=%d e7has0x2B=%d e46has0x42=%d (%u ms)",
                  foundBase, foundSeq, has7, has46, dt);
        int ids[5] = { 0, 1, 7, 27, 46 };
        for (int k = 0; k < 5; k++) {
            int id = ids[k];
            uint8_t row[ST_MAG_STRIDE] = {};
            if (StReadBytesSEH(foundBase + id * ST_MAG_STRIDE, row, ST_MAG_STRIDE)) {
                char hex[ST_MAG_STRIDE * 3 + 1]; int p = 0;
                for (int i = 0; i < ST_MAG_STRIDE; i++)
                    p += snprintf(hex + p, sizeof(hex) - p, "%02X ", row[i]);
                Log::Menu("[STMAGSCAN] magic %2d @%08X: %s", id, foundBase + id * ST_MAG_STRIDE, hex);
            }
        }
        return;
    }

    // PASS 2 (fallback): no strict hit. Collect up to 6 bases that merely carry
    // both value bytes at the anchor entries (stride 0x3C, sane entry-0 name
    // offset), then dump them OUTSIDE the __try so the real table can be picked
    // out by inspection. Records bases only inside SEH (no logging in the frame).
    uint32_t cands[6]; int found = 0;
    addr = SCAN_BEG;
    while (addr < SCAN_END && found < 6) {
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) break;
        uint32_t regBase = (uint32_t)mbi.BaseAddress;
        uint32_t regSize = (uint32_t)mbi.RegionSize;
        uint32_t next = regBase + regSize;
        if (next <= regBase) break;
        bool readable = (mbi.State == MEM_COMMIT) &&
                        ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0) &&
                        ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                         PAGE_EXECUTE_WRITECOPY)) != 0);
        if (readable && regSize >= NEED) {
            uint32_t s = (regBase < addr ? addr : regBase);
            s = (s + 3u) & ~3u;
            uint32_t e = next - NEED;
            __try {
                for (uint32_t b = s; b <= e && found < 6; b += 4) {
                    uint16_t n0 = *(uint16_t*)b;
                    if (n0 == 0 || n0 > 0x4000) continue;
                    if (!StMagEntryHas(b, 7, 0x2B)) continue;
                    if (!StMagEntryHas(b, 46, 0x42)) continue;
                    cands[found++] = b;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) { /* skip faulting region */ }
        }
        addr = next;
    }

    if (found == 0) { Log::Menu("[STMAGSCAN] no STRONG hit and no fallback candidates (%u ms)", dt); return; }
    Log::Menu("[STMAGSCAN] no STRONG hit; %d fallback candidate(s) (%u ms):", found, dt);
    for (int c = 0; c < found; c++) {
        uint32_t b = cands[c];
        uint8_t r7[ST_MAG_STRIDE] = {}, r46[ST_MAG_STRIDE] = {};
        bool ok7  = StReadBytesSEH(b + 7  * ST_MAG_STRIDE, r7,  ST_MAG_STRIDE);
        bool ok46 = StReadBytesSEH(b + 46 * ST_MAG_STRIDE, r46, ST_MAG_STRIDE);
        unsigned id7  = ok7  ? (unsigned)(r7[4]  | (r7[5]  << 8)) : 0u;
        unsigned id46 = ok46 ? (unsigned)(r46[4] | (r46[5] << 8)) : 0u;
        Log::Menu("[STMAGSCAN] cand[%d] base=0x%08X id7@+4=%u id46@+4=%u", c, b, id7, id46);
        if (ok7) {
            char h[ST_MAG_STRIDE * 3 + 1]; int p = 0;
            for (int i = 0; i < ST_MAG_STRIDE; i++) p += snprintf(h + p, sizeof(h) - p, "%02X ", r7[i]);
            Log::Menu("[STMAGSCAN]   e7 : %s", h);
        }
        if (ok46) {
            char h[ST_MAG_STRIDE * 3 + 1]; int p = 0;
            for (int i = 0; i < ST_MAG_STRIDE; i++) p += snprintf(h + p, sizeof(h) - p, "%02X ", r46[i]);
            Log::Menu("[STMAGSCAN]   e46: %s", h);
        }
    }
}
