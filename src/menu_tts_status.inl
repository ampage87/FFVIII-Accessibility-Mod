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
