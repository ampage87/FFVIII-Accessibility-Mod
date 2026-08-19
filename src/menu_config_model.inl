// menu_config_model.inl -- v0.25.0 (#84)
//
// The Config screen's ANNOUNCEMENT LOGIC: pure functions of a ConfigView, no
// Win32, no SEH, no absolute memory. Same split as the Magic, Junction and Card
// models, driven offline by tests/menu_sim.cpp.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE, before menu_tts_config.inl.
//
// ---------------------------------------------------------------------------
// WHERE THIS COMES FROM
//
// Config module: dispatch index 8, creator 0x004EDD30, update 0x004EDE90, draw
// 0x004EE750, state table 0x004EE6D8 (states 0..13). Row table 0x00B88970,
// 9 rows of 16 bytes. Full write-up in docs/CONFIG_MENU_FINDINGS.md.
//
// **Every value on this screen is invisible to a screen reader by design.** The
// toggles show their current setting *only* by palette -- the active word is
// drawn bright and the other dimmed, with no marker, no brackets and no cursor
// on the value. The five bars have no numeric readout at all. So unlike the
// other menus, where the mod adds context to text that is at least present,
// here the mod is speaking the entire state of the screen.
// ---------------------------------------------------------------------------

#ifndef MENU_CONFIG_MODEL_INCLUDED
#define MENU_CONFIG_MODEL_INCLUDED

// The two states the player can rest in.
//
// State 2 is one frame and falls THROUGH into state 3's body, so the first
// interactive frame reports 2 -- which means gating on 3 alone would lose the
// arrival announcement about half the time, depending on where the poll landed.
// Both are treated as the list; the repeat suppression makes the overlap free.
static const int CFG_STATE_LIST_ENTER = 2;
static const int CFG_STATE_LIST       = 3;
static const int CFG_STATE_CUSTOMIZE  = 7;

static const int CFG_ROW_COUNT = 9;

// The rows, in the on-screen order the draw loop walks (table order, 16 px
// apart). Widget kinds:
//   TOGGLE  a flag bit in 0x01CFE73C; Left clears, Right sets
//   BAR5    a 0..4 byte where 0 is the FULL bar -- Right decreases it
//   VOLUME  a 0..100 byte
enum { CFGW_TOGGLE = 0, CFGW_BAR5 = 1, CFGW_VOLUME = 2 };

struct ConfigRow {
    const char* label;
    int         widget;
    unsigned    mask;        // TOGGLE: the flag bit
    int         byteIndex;   // BAR5/VOLUME: offset from 0x01CFE738
    const char* offWord;     // TOGGLE: value with the bit CLEAR
    const char* onWord;      // TOGGLE: value with the bit SET
    const char* help;        // the game's own help line for the row
};

// Value words are the game's own strings (section 1 bank 2, entries noted in
// the findings doc) -- not paraphrases, so a player reading a guide hears the
// same words the guide uses.
static const ConfigRow CFG_ROWS[CFG_ROW_COUNT] = {
    { "Controller",      CFGW_TOGGLE, 0x0020, -1, "Normal",  "Customize", "Change controller setting" },
    { "Cursor",          CFGW_TOGGLE, 0x0004, -1, "Initial", "Memory",    "Set cursor" },
    { "ATB",             CFGW_TOGGLE, 0x0001, -1, "Active",  "Wait",      "Set ATB" },
    { "Scan",            CFGW_TOGGLE, 0x0100, -1, "Once",    "Always",    "Set close-up for Scan" },
    { "Camera movement", CFGW_BAR5,   0,       6, 0, 0, "Set battle camera movement" },
    { "Battle speed",    CFGW_BAR5,   0,       0, 0, 0, "Set battle speed" },
    { "Battle message",  CFGW_BAR5,   0,       1, 0, 0, "Set battle message speed" },
    { "Field message",   CFGW_BAR5,   0,       2, 0, 0, "Set field message speed" },
    { "Sound",           CFGW_VOLUME, 0,       3, 0, 0, "Set sound" },
};

// Which rows the mod has taken over. Aaron: *"There are some items in the Config
// menu which we've essentially overridden or duplicated within the mod. These
// include the ones for volume and ATB... when a volume option is set the
// announcement can tell the player volume is controlled by the accessibility
// mod and the keys... When ATB is selected it should inform the player whether
// Enhanced Wait Mode is on/off and the key for it."*
//
// This matters more than it looks. A blind player who turns the game's Sound
// slider down and hears nothing change would reasonably conclude the setting is
// broken -- when in fact the mod's own mixer is what they are hearing. Saying so
// on the row converts a dead control into an explained one.
static const int CFG_ROW_CONTROLLER = 0;
static const int CFG_ROW_ATB       = 2;
static const int CFG_ROW_SOUND     = 8;

struct ConfigView
{
    unsigned short state;
    int  cursor;             // module +0x28, row 0..8
    unsigned flags;          // 0x01CFE73C
    unsigned char bytes[8];  // 0x01CFE738.., indexed by ConfigRow::byteIndex
    bool ewmEnabled;         // the mod's Enhanced Wait Mode, for the ATB row
    bool buttonsDefault;     // is FF8's own 12-button map untouched?
    int  customizePage;      // module +0x29, 0..2
    int  customizeRow;       // module +0x2A

    // FF8's 12-byte button map at 0x01CFE740, and whether it is even consulted.
    // Needed here rather than as a bool because the Customize screen has to name
    // the key sitting on each row, and the key that gets you OUT -- both of which
    // move when the map moves.
    unsigned char btnMap[12];
    bool          btnRemapActive;   // flags & 0x0020
};

static void CfgAppend(char* out, size_t n, const char* s)
{
    size_t l = strlen(out);
    if (l >= n - 1) return;
    snprintf(out + l, n - l, "%s", s);
}
static void CfgAppendInt(char* out, size_t n, long v)
{
    char t[24]; snprintf(t, sizeof(t), "%ld", v);
    CfgAppend(out, n, t);
}

// ===========================================================================
// The value of a row, in words.
//
// The five-step bars are the interesting case. The stored byte runs 0..4 with
// **0 meaning the FULL bar** -- Right decreases it -- so speaking the raw byte
// would tell the player that 0 is the most and 4 the least, which is the
// opposite of what the bar in front of them shows and the opposite of what a
// guide will say. The spoken number is therefore the BAR LENGTH, 1..5, and the
// two ends are named outright so the direction is never in doubt.
// ===========================================================================
static int CfgBarSteps(unsigned char raw) { return (raw <= 4) ? (5 - (int)raw) : 0; }

static void CfgRowValue(const ConfigView& v, int row, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (row < 0 || row >= CFG_ROW_COUNT) return;
    const ConfigRow& r = CFG_ROWS[row];

    if (r.widget == CFGW_TOGGLE) {
        CfgAppend(out, n, (v.flags & r.mask) ? r.onWord : r.offWord);
        return;
    }
    const unsigned char raw = (r.byteIndex >= 0 && r.byteIndex < 8)
                            ? v.bytes[r.byteIndex] : 0;
    if (r.widget == CFGW_VOLUME) {
        CfgAppendInt(out, n, (long)raw);
        CfgAppend(out, n, " percent");
        return;
    }
    const int steps = CfgBarSteps(raw);
    CfgAppendInt(out, n, steps);
    CfgAppend(out, n, " of 5");
    // Name the ends. "Fastest" is what a full battle-speed bar means; on the
    // camera row the same full bar means the most movement, not the fastest.
    if (steps == 5) CfgAppend(out, n, (row == 4) ? ", most" : ", fastest");
    else if (steps == 1) CfgAppend(out, n, (row == 4) ? ", least" : ", slowest");
}

// ===========================================================================
// The line spoken when the cursor lands on a row.
//
// "ATB, Wait. Enhanced Wait Mode is on. Press O in battle to toggle it."
// "Sound, 100 percent. Volume is controlled by the mod: F7 and F8 for music,
//  F5 and F6 for effects."
//
// The two override notes are spoken EVERY time the row is selected, not once
// per visit. That is what Aaron asked for, and it is right: the whole point is
// that the row does not do what its name says, and a player who hears the
// warning once and then comes back an hour later needs it again.
// ===========================================================================
static void CfgAnnounceRow(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int row = v.cursor;
    if (row < 0 || row >= CFG_ROW_COUNT) return;

    CfgAppend(out, n, CFG_ROWS[row].label);
    char val[64];
    CfgRowValue(v, row, val, sizeof(val));
    if (val[0]) { CfgAppend(out, n, ", "); CfgAppend(out, n, val); }

    if (row == CFG_ROW_ATB) {
        // The game's ATB bit only freezes the clock while a command or target
        // menu is open. The mod's Enhanced Wait Mode is a much broader thing,
        // and it is the one actually governing whether the player gets time to
        // read -- so which of them is on is the useful fact, not the bit.
        CfgAppend(out, n, ". Enhanced Wait Mode is ");
        CfgAppend(out, n, v.ewmEnabled ? "on" : "off");
        CfgAppend(out, n, ". Press O in battle to toggle it");
    } else if (row == CFG_ROW_CONTROLLER) {
        // **This row is a trap and the mod put the player next to it.** Setting
        // it to Customize opens a screen where Cancel is not handled at all and
        // the Steam port's "press the button you want" rebinder is listening, so
        // a blind player feeling for the way out remaps a control with every
        // press. Aaron walked into exactly that. The row now says how to get out
        // before you go in, and reports whether the map is currently stock.
        //
        // v0.25.2: the hotkey moved from Alt+K to Shift+F9 -- see button_map_rescue.inl
        // for why Alt+K never fired -- and Aaron asked for the conflict to be
        // named outright: *"changing the game's defaults could conflict with keys
        // used by the mod."* That warning is now unconditional on this row, not
        // just when the setting already reads Customize, because the damage is
        // done by walking in, and the row is the last place to say so.
        if (!v.buttonsDefault)
            CfgAppend(out, n, ". Buttons have been remapped, which can break the mod's own "
                              "shortcut keys. Press Shift F9 to restore defaults");
        else
            CfgAppend(out, n, ". Leave this on Normal: Customize opens button assignment, "
                              "where Cancel does not work and every key you press is "
                              "reassigned, and the mod's shortcuts assume the default "
                              "layout. Shift F9 restores defaults at any time");
    } else if (row == CFG_ROW_SOUND) {
        CfgAppend(out, n, ". Volume is controlled by the mod: "
                          "F7 and F8 for music, F5 and F6 for effects");
    }
}

// The help line the game shows at the top for the focused row. On its own key,
// because it is the game's wording and worth having, but it is a second
// sentence about a row the player has just been told the state of.
static void CfgAnnounceHelp(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int row = v.cursor;
    if (row < 0 || row >= CFG_ROW_COUNT) return;
    CfgAppend(out, n, CFG_ROWS[row].help);
}

// Every setting at once. Nine rows is short enough to be worth having in one
// utterance -- it is the only way to answer "what is my configuration" without
// walking the list and listening to nine separate lines.
static void CfgAnnounceAll(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    ConfigView t = v;
    for (int i = 0; i < CFG_ROW_COUNT; i++) {
        if (i) CfgAppend(out, n, ", ");
        CfgAppend(out, n, CFG_ROWS[i].label);
        CfgAppend(out, n, " ");
        t.cursor = i;
        char val[64];
        CfgRowValue(t, i, val, sizeof(val));
        CfgAppend(out, n, val);
    }
}

// Position in the list.
static void CfgAnnouncePosition(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.cursor < 0 || v.cursor >= CFG_ROW_COUNT) return;
    CfgAppend(out, n, "Setting ");
    CfgAppendInt(out, n, v.cursor + 1);
    CfgAppend(out, n, " of ");
    CfgAppendInt(out, n, CFG_ROW_COUNT);
}

// ===========================================================================
// The Customize sub-screen.
//
// **Cancel does not work in there.** The state-7 handler tests only the
// 0x0800 edge bit to leave; the Cancel bit is not read at all, so a player who
// presses Cancel gets nothing and has every reason to think the game has hung.
// Sighted players see the on-screen button legend. This is the single most
// useful thing the mod can say about that screen, so it leads.
// ===========================================================================
// v0.25.2: the screen's own titles, from mngrp section 1 bank 2 strings 100,
// 102, 104. The page does not change WHICH key each row is -- it changes what
// that key DOES, which is why the page name has to lead every row line.
static const char* const CFG_CUSTOMIZE_PAGES[3] = {
    "Field Map Controls", "Battle Controls", "World Map Controls"
};

// The ten rows, in the order the game draws them (table 0x00B88A10), and what
// each does on each page. Bank 2 strings 40..58 / 60..78 / 80..98: the draw code
// asks for entry `row + 10 * (page + 2)` and every entry is a pair, so the
// string index is twice that. Verified against Aaron's screenshot of the Field
// page, which reads Talk/Confirm, Walk/Cancel, Talk, Menu, N/A, N/A, N/A, N/A.
//
// "N/A" is spoken as "not used" -- the game's abbreviation is a visual shorthand
// and a screen reader saying "en slash ay" eight times is noise.
static const int CFG_CUSTOMIZE_ROWS = 10;
static const char* const CFG_CUST_LABEL[3][CFG_CUSTOMIZE_ROWS] = {
    { "Talk or Confirm", "Walk or Cancel", "Talk", "Menu",
      "not used", "not used", "not used", "not used",
      "Walk", "not used" },
    { "Confirm", "Cancel", "View status", "Change character",
      "Change select window", "Trigger",
      "Escape, hold with the other Escape key",
      "Escape, hold with the other Escape key",
      "Move cursor", "not used" },
    { "On, off, or examine", "Move back", "Move forward", "Menu",
      "Look left", "Look right", "not used", "Switch point of view",
      "Walk, or steer a vehicle", "Vehicle forward and back" }
};

// Row position -> the LOGICAL button it stands for, byte 0 of each 8-byte entry
// at 0x00B88A10. Rows 8 and 9 carry flag byte 1 and are the two analog sticks;
// they are drawn without a key glyph, so they have no letter to speak.
static const int CFG_CUST_LOGICAL[CFG_CUSTOMIZE_ROWS] = { 6, 4, 7, 5, 2, 3, 0, 1, 9, 10 };
static const int CFG_CUST_LAST_KEYED_ROW = 7;

// The two bits the Customize state handler actually tests, at 0x004EE007 and
// 0x004EE03E: 0x0100 restores every button to default, 0x0800 leaves. **Cancel
// is not among them** -- that is the whole trap.
static const int CFG_LOGICAL_DEFAULT = 8;    // 0x0100
static const int CFG_LOGICAL_END     = 11;   // 0x0800

// ---------------------------------------------------------------------------
// Key letters by RAW slot 0..11.
//
// The game draws these as FONT GLYPHS -- the draw path is
// `char = 0x004A2DF0(logical) + 0x80` -- so the letters are pixels in the PC
// font, not text anywhere in the exe or in mngrp. There is nothing to decode.
// They were read off a screenshot of the screen at a known map and they are a
// property of the build, not of the player's settings, so once right they stay
// right.
//
// A null entry means "not established". Every caller degrades to naming the mod
// hotkey instead of guessing, because a WRONG key on this screen is worse than
// no key: acting on it presses a button that reassigns something.
// ---------------------------------------------------------------------------
static const char* const CFG_KEY_LETTER[12] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// 0x004A2DF0 reproduced: logical button -> the raw slot currently driving it.
// With the remap flag clear the game returns the logical index untouched, so
// this must too -- a stock pad is not "unmapped", it is mapped by identity.
static int CfgRawForLogical(const ConfigView& v, int logical)
{
    if (logical < 0 || logical >= 12) return -1;
    if (!v.btnRemapActive) return logical;
    for (int i = 0; i < 12; i++)
        if ((int)v.btnMap[i] == logical + 1) return i;
    return -1;
}

static const char* CfgKeyForLogical(const ConfigView& v, int logical)
{
    const int raw = CfgRawForLogical(v, logical);
    if (raw < 0 || raw >= 12) return 0;
    return CFG_KEY_LETTER[raw];
}

// ===========================================================================
// The way out.
//
// Aaron, after v0.25.1 left him remapped and Alt+K did not fire: *"It is still
// not back to the original controls."* The answer was on screen the entire
// time -- the footer reads **"S to end, F to default"** -- and the mod never
// read it to him. A sighted player solves this in two seconds.
//
// So the escape line is now built from the game's own two bits and spoken on
// arrival, on every page change, and on demand. When the letters are known it
// names them; when they are not, it names Shift+F9, which the mod controls and which
// works from anywhere. It never guesses a letter.
// ===========================================================================
static void CfgAppendEscape(const ConfigView& v, char* out, size_t n)
{
    const char* kEnd = CfgKeyForLogical(v, CFG_LOGICAL_END);
    const char* kDef = CfgKeyForLogical(v, CFG_LOGICAL_DEFAULT);

    if (kDef) { CfgAppend(out, n, "Press "); CfgAppend(out, n, kDef);
                CfgAppend(out, n, " to restore every button to default"); }
    else      { CfgAppend(out, n, "Press Shift F9 to restore every button to default"); }

    if (kEnd) { CfgAppend(out, n, ", "); CfgAppend(out, n, kEnd);
                CfgAppend(out, n, " to leave this screen"); }
    else      { CfgAppend(out, n, ". The key that leaves is shown at the bottom of the "
                                  "screen; Shift F9 works from anywhere"); }
    CfgAppend(out, n, ". Cancel does nothing here");
}

// The line for the row under the cursor: what it does on this page, and which
// key does it. Rows 8 and 9 are the analog sticks and carry no key.
static void CfgAppendCustomizeRow(const ConfigView& v, char* out, size_t n)
{
    const int row = v.customizeRow;
    const int pg  = v.customizePage;
    if (row < 0 || row >= CFG_CUSTOMIZE_ROWS || pg < 0 || pg >= 3) return;

    CfgAppend(out, n, CFG_CUST_LABEL[pg][row]);
    if (row > CFG_CUST_LAST_KEYED_ROW) return;

    const char* key = CfgKeyForLogical(v, CFG_CUST_LOGICAL[row]);
    if (key) { CfgAppend(out, n, ", "); CfgAppend(out, n, key); }
}

// ===========================================================================
// The Customize sub-screen.
//
// Aaron: *"We should also add a warning against changing the controller
// layout, since changing the game's defaults could conflict with keys used by
// the mod."* He is right, and it is worse than a conflict: the mod's own
// shortcuts are letter keys, and every letter the player presses in here is a
// reassignment. **The warning therefore leads**, ahead of even the page name --
// by the time it is heard the player is already inside.
// ===========================================================================
static void CfgAnnounceCustomize(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    CfgAppend(out, n, "Button assignment. Do not change these: the mod's own keys "
                      "assume the default layout, and any key you press in here is "
                      "reassigned. ");
    CfgAppendEscape(v, out, n);
    CfgAppend(out, n, ". ");
    CfgAppend(out, n, (v.customizePage >= 0 && v.customizePage < 3)
                      ? CFG_CUSTOMIZE_PAGES[v.customizePage] : "page");
    CfgAppend(out, n, ". ");
    CfgAppendCustomizeRow(v, out, n);
}

// Moving the cursor inside Customize: the row, and nothing else. The warning is
// on arrival and on demand; repeating it on every keypress would bury the one
// piece of information that changes.
static void CfgAnnounceCustomizeRow(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    CfgAppendCustomizeRow(v, out, n);
}

// The page banner, when Left/Right swings to another set of meanings. The same
// keys do different things per page, so the row line follows the page name.
static void CfgAnnounceCustomizePage(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    CfgAppend(out, n, (v.customizePage >= 0 && v.customizePage < 3)
                      ? CFG_CUSTOMIZE_PAGES[v.customizePage] : "page");
    CfgAppend(out, n, ". ");
    CfgAppendCustomizeRow(v, out, n);
}

// Every row of the current page, on demand -- the whole assignment sheet, which
// is the thing a sighted player takes in at a glance and a blind one otherwise
// has to arrow through one row at a time.
static void CfgAnnounceCustomizeAll(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int pg = v.customizePage;
    if (pg < 0 || pg >= 3) return;
    CfgAppend(out, n, CFG_CUSTOMIZE_PAGES[pg]);
    CfgAppend(out, n, ". ");
    ConfigView t = v;
    for (int r = 0; r < CFG_CUSTOMIZE_ROWS; r++) {
        t.customizeRow = r;
        CfgAppendCustomizeRow(t, out, n);
        CfgAppend(out, n, r + 1 < CFG_CUSTOMIZE_ROWS ? ". " : "");
    }
}

// The escape line on its own key, so a player who has stopped listening or who
// arrived mid-sentence can ask for it again without leaving the screen.
static void CfgAnnounceCustomizeEscape(const ConfigView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    CfgAppendEscape(v, out, n);
}

#endif // MENU_CONFIG_MODEL_INCLUDED
