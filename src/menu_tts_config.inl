// menu_tts_config.inl -- v0.25.0 (#84)
//
// The Config screen. PART OF menu_tts.cpp -- TEXTUAL INCLUDE, after
// menu_config_model.inl. Do NOT compile standalone.
//
// Addresses only; all wording is in the model.
//
// ---------------------------------------------------------------------------
// Module: dispatch index 8 (pMenuStateA + 0x1E8 == 8), creator 0x004EDD30,
// update 0x004EDE90, draw 0x004EE750, states 0..13. Found by walking the pool
// for the update fn, never by assuming a slot.
//
// Steady states: 3 (the option list) and 7 (Customize). State 2 is one frame and
// FALLS THROUGH into state 3's body, so the first interactive frame reports 2 --
// gating on 3 alone would drop the arrival line whenever the poll landed on that
// frame. States 5 and 11 are the list/Customize slides and, unlike the Card
// album's page slides, they do not sample input at all.
// ---------------------------------------------------------------------------

static const uint32_t CFG_UPDATE_FN = 0x004EDE90;

// Module field offsets.
static const int CFGO_STATE     = 0x10;   // u16
static const int CFGO_CURSOR    = 0x28;   // s8, row 0..8
static const int CFGO_CUST_PAGE = 0x29;   // s8, 0..2
static const int CFGO_CUST_ROW  = 0x2A;   // s8

// The settings block: 20 bytes saved with the save file.
//   +0 battle speed, +1 battle message, +2 field message  (0..4, 0 = fastest)
//   +3 sound volume 0..100
//   +4 flag word (u16) -- every toggle on the screen is a bit in here
//   +6 camera movement 0..4
static const uintptr_t CFG_SETTINGS = 0x01CFE738;
static const int       CFG_FLAGS_OFF = 4;

// The 12-byte button map, part of the same saved block (+8). One entry per raw
// slot, holding a 1-based logical button; the Customize screen SWAPS two entries
// (0x004EE560), the defaults action writes i+1 across (0x004EE01C), and every
// button read is translated through it by 0x004A2D60 -- but only while flag bit
// 0x0020 is set.
static const uintptr_t CFG_BTN_MAP = 0x01CFE740;

// ---------------------------------------------------------------------------
static bool s_cfgActive = false;
static int  s_cfgState  = -1;
static char s_cfgLastSpoken[512] = {0};

// v0.25.2: Customize is now three different sentences depending on what moved,
// so it needs its own memory. `s_cfgInCust` is "was the player already inside
// last frame" -- the arrival line, which carries the warning and the way out,
// must fire exactly once per visit and not again on every cursor move.
static bool s_cfgInCust   = false;
static int  s_cfgCustPage = -1;
static int  s_cfgCustRow  = -1;

static void ResetConfigMenu()
{
    s_cfgActive = false;
    s_cfgState  = -1;
    s_cfgLastSpoken[0] = '\0';
    s_cfgInCust   = false;
    s_cfgCustPage = -1;
    s_cfgCustRow  = -1;
}

static uint8_t* FindConfigModule()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)MM_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < MM_POOL_BASE || a >= MM_POOL_END) break;
            if ((a - MM_POOL_BASE) % 0x78 != 0) break;
            if (*(uint32_t*)(m + 0x08) == CFG_UPDATE_FN) return m;
            m = *(uint8_t* volatile*)m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

static bool FillConfigView(uint8_t* mod, ConfigView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        v.state         = *(uint16_t*)(mod + CFGO_STATE);
        v.cursor        = (int)*(int8_t*)(mod + CFGO_CURSOR);
        v.customizePage = (int)*(int8_t*)(mod + CFGO_CUST_PAGE);
        v.customizeRow  = (int)*(int8_t*)(mod + CFGO_CUST_ROW);

        const uint8_t* s = (const uint8_t*)CFG_SETTINGS;
        for (int i = 0; i < 8; i++) v.bytes[i] = s[i];
        v.flags = *(const uint16_t*)(s + CFG_FLAGS_OFF);

        // v0.25.2: the map itself, so the Customize screen can name the key on
        // each row and -- the part that actually matters -- the key that gets the
        // player back out. Both move when the map moves, which is exactly the
        // situation the player is in when they most need to be told.
        const uint8_t* m = (const uint8_t*)CFG_BTN_MAP;
        for (int i = 0; i < 12; i++) v.btnMap[i] = m[i];
        v.btnRemapActive = (v.flags & 0x0020) != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }

    // The mod's own Enhanced Wait Mode, which is what the ATB row's announcement
    // is actually about. Read from the INI key the EWM toggle persists rather
    // than from its static: that static lives in battle_tts.cpp, and one shared
    // accessor across two translation units is a dependency this screen does not
    // need for a value that is only ever read here.
    v.ewmEnabled = (Config::GetInt("ewm_enabled", 1) != 0);
    v.buttonsDefault = ButtonMapRescue::IsDefault();
    return true;
}

// ---------------------------------------------------------------------------
// The poll.
//
// Two speakable states and a line per row. **Left/Right change the value in
// place without moving the cursor or the state**, so the dedup key has to be the
// whole composed line -- if it were the row number, adjusting a slider would be
// silent, which is the one thing this screen must never be. Nothing else on it
// tells you a value changed: the bars have no readout and the toggles show their
// setting only by which word is drawn bright.
// ---------------------------------------------------------------------------
static void PollConfigMenu()
{
    if (!pMenuStateA) return;

    uint8_t sub = 0xFF;
    __try { sub = *((uint8_t*)pMenuStateA + JUNC_ACTIVE_OFFSET); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (sub != CONFIG_SUBSYSTEM_ID) { if (s_cfgActive) ResetConfigMenu(); return; }

    uint8_t* mod = FindConfigModule();
    if (!mod) { if (s_cfgActive) ResetConfigMenu(); return; }
    if (!s_cfgActive) {
        s_cfgActive = true;
        s_cfgState = -1; s_cfgLastSpoken[0] = '\0';
        Log::Menu("[ConfigTTS] module at 0x%08X (pool slot %d)",
                  (unsigned)(uintptr_t)mod, (int)(((uintptr_t)mod - MM_POOL_BASE) / 0x78));
    }

    ConfigView v;
    if (!FillConfigView(mod, v)) return;

    const bool isList = (v.state == CFG_STATE_LIST || v.state == CFG_STATE_LIST_ENTER);
    const bool isCust = (v.state == CFG_STATE_CUSTOMIZE);
    if (!isList && !isCust) {
        // Leaving Customize by any route -- including the slide out -- must clear
        // the arrival latch, or a second visit is silent about the way out.
        if (s_cfgInCust) { s_cfgInCust = false; s_cfgCustPage = -1; s_cfgCustRow = -1; }
        return;
    }

    char line[512];
    if (isCust) {
        // Arrival first, then the page, then the row -- in that order, because a
        // page change moves the cursor's meaning and a row change does not.
        if (!s_cfgInCust) {
            s_cfgInCust = true;
            CfgAnnounceCustomize(v, line, sizeof(line));
        } else if (v.customizePage != s_cfgCustPage) {
            CfgAnnounceCustomizePage(v, line, sizeof(line));
        } else if (v.customizeRow != s_cfgCustRow) {
            CfgAnnounceCustomizeRow(v, line, sizeof(line));
        } else {
            return;
        }
        s_cfgCustPage = v.customizePage;
        s_cfgCustRow  = v.customizeRow;

        // **Speak here and return, deliberately skipping the whole-line dedup
        // below.** Four of this page's rows read "not used", so an identical-text
        // guard would go silent exactly where the player most needs the feedback
        // that the cursor moved at all. (page, row) is already the correct key.
        if (line[0] == '\0') return;
        ScreenReader::Speak(line, true);
        snprintf(s_cfgLastSpoken, sizeof(s_cfgLastSpoken), "%s", line);
        s_cfgState = (int)v.state;
        Log::Menu("[ConfigTTS] customize page=%d row=%d flags=%04X : \"%s\"",
                  v.customizePage, v.customizeRow, v.flags, line);
        return;
    }

    if (s_cfgInCust) { s_cfgInCust = false; s_cfgCustPage = -1; s_cfgCustRow = -1; }
    CfgAnnounceRow(v, line, sizeof(line));
    if (line[0] == '\0') return;

    if (strcmp(line, s_cfgLastSpoken) == 0) return;
    ScreenReader::Speak(line, true);
    snprintf(s_cfgLastSpoken, sizeof(s_cfgLastSpoken), "%s", line);
    s_cfgState = (int)v.state;

    Log::Menu("[ConfigTTS] state=%u row=%d flags=%04X : \"%s\"",
              (unsigned)v.state, v.cursor, v.flags, line);
}

// ---------------------------------------------------------------------------
// Number keys.
//   0  every setting at once
//   1  where am I in the list
//   2  the game's own help line for this row
// ---------------------------------------------------------------------------
static void ConfigNumberKeys()
{
    if (!s_cfgActive) return;

    int key = -1;
    for (int k = 0; k <= 2; k++)
        if (GetAsyncKeyState('0' + k) & 1) { key = k; break; }
    if (key < 0) return;

    uint8_t* mod = FindConfigModule();
    if (!mod) return;
    ConfigView v;
    if (!FillConfigView(mod, v)) return;

    char buf[640];
    buf[0] = '\0';
    if (v.state == CFG_STATE_CUSTOMIZE) {
        // Same three keys, re-pointed at what is in front of the player. Key 2
        // is the way out, and it is on a key rather than only on arrival because
        // a player who has stopped listening or who tabbed away must be able to
        // ask again without pressing anything the screen will reassign.
        if      (key == 0) CfgAnnounceCustomizeAll(v, buf, sizeof(buf));
        else if (key == 1) CfgAnnounceCustomizeRow(v, buf, sizeof(buf));
        else               CfgAnnounceCustomizeEscape(v, buf, sizeof(buf));
    }
    else if (key == 0) CfgAnnounceAll(v, buf, sizeof(buf));
    else if (key == 1) CfgAnnouncePosition(v, buf, sizeof(buf));
    else               CfgAnnounceHelp(v, buf, sizeof(buf));

    if (buf[0] == '\0') return;
    ScreenReader::Speak(buf, true);
    Log::Menu("[ConfigTTS] key=%d row=%d: %s", key, v.cursor, buf);
}
