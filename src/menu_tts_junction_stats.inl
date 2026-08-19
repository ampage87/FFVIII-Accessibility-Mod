// menu_tts_junction_stats.inl -- v0.23.3 (#82)
//
// The Junction screens the mod did not previously speak: the junction grid, the
// magic list with its live preview, the character-ability list, the always-on
// party abilities, and the number-key readouts.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE, after menu_junction_model.inl,
// menu_tts_junction.inl and menu_tts_magic.inl (it uses MagicCharName and
// MagicTextToGlyphs from the last of those). Do NOT compile standalone.
//
// This file is ONLY responsible for addresses. Every decision about wording is
// in menu_junction_model.inl, where tests/menu_sim.cpp drives it offline -- the
// same split that made the Magic submenu tractable.
//
// ---------------------------------------------------------------------------
// ADDRESSES -- and the correction that mattered
//
// The Junction module is dispatch index 17, update fn 0x004DA9B0, a 74-state
// machine with its jump table at 0x004DFC54. The mod has read that machine's
// state word as `pMenuStateA + 0x22E` ("focus") for two years and calls the
// right shots on char-select, the action row, the GF list and the Auto popup,
// so this file speaks the same word rather than re-deriving a module pointer.
//
// **The first draft of this file had the grid at state 37, and state 37 is a
// SLIDE-IN ANIMATION.** 0x004DB008 walks +0x40 from 0 to 0x1000 and only then
// hands over to 38. Reading the grid there would have announced a row at most
// once, on whichever frame the poll happened to land, and looked like a flaky
// hook rather than a wrong constant. The steady grid state is **52 (0x34)** --
// 0x004DB29F, the one that reads the D-pad against the row count -- reached
// through the 49 -> 50 -> 51 -> 52 fade-in chain.
//
// That is the same class of error as the Magic All-transfer latch: a state the
// game passes THROUGH is not a state the player is ever IN.
//
// Steady states, each identified by the handler that reads input:
//   52 (0x34)  the junction grid          0x004DB29F, cursor +0x276
//   59 (0x3B)  choosing a magic           0x004DB575, cursor +0x26E
//   24 (0x18)  the equipped ability slots 0x004DCB95, cursor +0x27C
//   28 (0x1C)  the available-ability list 0x004DAE08, cursor +0x270 + kind
// ---------------------------------------------------------------------------

// pMenuStateA-relative offsets (module field + 0x21E).
static const int JSO_STATE       = 0x22E;   // u16, the state machine's word
static const int JSO_HELP_PTR    = 0x23E;   // -> FF8 text: the highlighted description
static const int JSO_ELIG_MASK   = 0x24A;   // u32, stock slots that do something in this row
static const int JSO_CHAR_ID     = 0x261;   // the character being edited (module +0x43)
static const int JSO_MAGIC_CUR   = 0x26E;   // absolute 0..31
static const int JSO_ABIL_CUR    = 0x270;   // +kind: 0x270/0x271/0x272
static const int JSO_ABIL_KIND   = 0x274;   // 1 = command list, 2 = character list
static const int JSO_ABIL_COUNT  = 0x275;
static const int JSO_GRID_CUR    = 0x276;   // 0..19
static const int JSO_GRID_GROUP  = 0x277;   // usable rows in the cursor's column

// The state numbers themselves live in menu_junction_model.inl so the offline
// gate can pin them -- which state the poll may speak in is the one address-like
// constant a test without the game can still hold to account.
static const int JS_STATE_GRID       = JUNC_STATE_GRID;
static const int JS_STATE_MAGIC      = JUNC_STATE_MAGIC;
static const int JS_STATE_ABIL_SLOTS = JUNC_STATE_ABIL_SLOTS;
static const int JS_STATE_ABIL_LIST  = JUNC_STATE_ABIL_LIST;

// Game data.
static const uintptr_t JS_CHAR_BASE   = 0x01CFE0E8;  // savemap + 0x048C, stride 152
static const int       JS_CHAR_STRIDE = 152;
static const int       JS_JUNCTION_OFF = 0x5C;       // 19 bytes of junctioned spell ids
static const int       JS_MAGICS_OFF   = 0x10;       // 32 x {id, qty}
static const uintptr_t JS_STAT_PREVIEW = 0x01CFF000; // live / previewed 464-byte block
static const uintptr_t JS_STAT_BASE    = 0x01D8B3B0; // baseline snapshot of the same
static const uintptr_t JS_SCRATCH      = 0x01D8B6A8; // per-character, stride 28
static const int       JS_SCRATCH_STRIDE = 28;

// The ability lists the game builds for this screen, at 0x004E0110: it unions
// the completeAbilities bitmaps of the character's junctioned GFs into
// 0x01D8B580[4], then walks ids 20..38 into one list and 39..82 into the other.
// Reading the game's own arrays instead of rebuilding them is what fixes the
// character list -- a reconstruction can be right about membership and still
// wrong about ORDER, and order is the only thing a cursor index means.
static const uintptr_t JS_ABIL_UNION      = 0x01D8B580;  // 128 bits
static const uintptr_t JS_ABIL_CMD_LIST   = 0x01D8B258;  // {id, nameIdx} pairs
static const uintptr_t JS_ABIL_CMD_COUNT  = 0x01D8B690;
static const uintptr_t JS_ABIL_CHAR_LIST  = 0x01D8B280;
static const uintptr_t JS_ABIL_CHAR_COUNT = 0x01D8B691;
static const int JS_ABIL_PARTY_FIRST = 83;   // first id the screen never lists
static const int JS_ABIL_PARTY_LAST  = 127;

// Stat-block offsets, in JUNC_STATS order. HP is the only u16.
static const int JS_STAT_OFF[9] = { 0x0174, 0x01BB, 0x01BC, 0x01BD, 0x01BE, 0x01BF, 0x01C1, 0x01C2, 0x01C0 };
static const int JS_ELEM_DEF_OFF = 0x0194;   // 8 x u16
static const int JS_ST_DEF_OFF   = 0x01A4;   // 13 x u8
static const int JS_EATK_MASK    = 0x01C4;   // u8 element bitmask
static const int JS_EATK_PCT     = 0x01C5;   // u8 percent, ABSOLUTE
// Status attack. The mask is split across two fields and has to be assembled
// exactly as 0x004E0FA0 does it -- see JuncAssembleStatusMask. The percentage
// is a u16 with 100 as "nothing" (0x004E0C7D: `sub eax, 0x64`).
static const int JS_STATK_LOW7   = 0x01B4;   // u8, statuses 0..6 in the low 7 bits
static const int JS_STATUS_WORD  = 0x018C;   // u32, carries statuses 7..12
static const int JS_STATK_RAW    = 0x01B6;   // u16, 100 = nothing

// Equipped slots on the character record.
static const int JS_CHR_COMMANDS = 0x50;   // 3
static const int JS_CHR_ABILITIES = 0x54;  // 4

// ---------------------------------------------------------------------------
static bool       s_jsActive   = false;
static int        s_jsState    = -1;
static int        s_jsLastChar = -1;
static char       s_jsLastSpoken[576] = {0};   // "state|grid|magic|line" -- see PollJunctionStats
static int        s_jsCharWatch = -1;          // last seen +0x261, for the L1/R1 switch

static void ResetJunctionStats()
{
    s_jsActive = false;
    s_jsState  = -1;
    s_jsLastChar = -1;
    s_jsCharWatch = -1;
    s_jsLastSpoken[0] = '\0';
}

// ---------------------------------------------------------------------------
// L1 / R1 (Q and E) swap the character being edited WITHOUT leaving the screen,
// and until v0.23.3 nothing said so. Aaron: *"Junction here doesn't announce the
// name of the new selected character when Q and E are pressed to switch between
// characters. Just like on the Magic submenu."*
//
// The switch is visible as a change in `pMenuStateA + 0x261`, which is the state
// machine's own field, so it can be watched from ANY state -- the action row and
// the sub-option screens included, which is where the BAT log caught it
// (17:40:25, `+0x261: 2 -> 0` with nothing spoken).
//
// **The action row is in fact the ONLY place the switch is possible**, which
// Aaron established in play and the exe confirms: `+0x43` is written at exactly
// two instructions, 0x004DBCF2 and 0x004DBF68, in the handlers for states 4 and
// 6, and states 4 and 6 are dispatched from ONE place -- state 3, the action row
// (0x004DABB5 and 0x004DAD11). Nothing else in the 74-state machine touches it.
// The watcher stays state-agnostic anyway: it costs nothing, and reading the
// field is a fact about the game where "only state 3 can do this" is a fact
// about the game's INPUT ROUTING, which is the more fragile of the two.
//
// **Confirming out of character select ALSO changes that byte** (17:39:22,
// `+0x261: 0 -> 2`), and there the char-select screen has just said "Irvine,
// Level 19, HP 522 of 1837" -- repeating the name would be noise. The two cases
// are two seconds apart either way, so recency cannot tell them apart. What CAN
// is the value: a confirm lands on the character char-select just named, and a
// switch never does. The marker is consumed on use, so switching away and back
// still announces both times.
//
// In the grid and the magic list the name goes in the HEADER instead, so the
// change and the new line are one utterance rather than two.
// ---------------------------------------------------------------------------
static void JsWatchCharacter(uint16_t state, uint8_t cur)
{
    if (s_jsCharWatch < 0) { s_jsCharWatch = (int)cur; return; }   // baseline, never speaks
    if ((int)cur == s_jsCharWatch) return;
    s_jsCharWatch = (int)cur;

    if (cur == s_juncCharSelSpoke) {          // a confirm, not a switch
        s_juncCharSelSpoke = 0xFF;            // consume, so a later switch back speaks
        return;
    }
    if (state == JS_STATE_GRID || state == JS_STATE_MAGIC) return;   // the header says it

    const char* nm = MagicCharName(cur);
    if (nm && nm[0]) {
        ScreenReader::Speak(nm, true);
        Log::Menu("[JuncStats] character switched to %s (+0x261=%u, state=%u)",
                  nm, (unsigned)cur, (unsigned)state);
    }
}

// Which character the Junction screen is editing. The state machine keeps it in
// its own field and indexes the savemap with `charId * 152` from there
// (0x004DE462), so this is the game's answer rather than the mod's -- but the
// cached char-select value stays as the fallback, because it is what every
// other line on this screen already uses and a disagreement should not make the
// two halves of the screen talk about different people.
static uint8_t JsCharId()
{
    uint8_t c = 0xFF;
    __try { c = *((uint8_t*)pMenuStateA + JSO_CHAR_ID); }
    __except(EXCEPTION_EXECUTE_HANDLER) { c = 0xFF; }
    if (c <= 7) return c;
    const uint8_t f = GetJuncSelectedCharIdx();
    return (f <= 7) ? f : 0;
}

// Fill the view. Returns false on any bad read: the caller then says nothing,
// because silence is recoverable and a wrong stat is not.
static bool FillJunctionView(JunctionView& v)
{
    memset(&v, 0, sizeof(v));
    __try {
        const uint8_t* pm = (const uint8_t*)pMenuStateA;
        v.state        = *(const uint16_t*)(pm + JSO_STATE);
        v.gridCursor   = pm[JSO_GRID_CUR];
        v.groupCount   = pm[JSO_GRID_GROUP];
        v.magicCursor  = pm[JSO_MAGIC_CUR];
        v.eligibleMask = *(const uint32_t*)(pm + JSO_ELIG_MASK);
        v.charId       = JsCharId();

        const uint8_t* ch = (const uint8_t*)(JS_CHAR_BASE + v.charId * JS_CHAR_STRIDE);
        for (int i = 0; i < JSLOT_COUNT; i++) v.junction[i] = ch[JS_JUNCTION_OFF + i];
        const uint8_t* mg = ch + JS_MAGICS_OFF;
        for (int i = 0; i < 32; i++) { v.magics[i].id = mg[i*2]; v.magics[i].qty = mg[i*2+1]; }

        v.unlockMask = *(const uint32_t*)(JS_SCRATCH + v.charId * JS_SCRATCH_STRIDE);

        const uint8_t* after  = (const uint8_t*)JS_STAT_PREVIEW;
        const uint8_t* before = (const uint8_t*)JS_STAT_BASE;
        for (int i = 0; i < 9; i++) {
            const int off = JS_STAT_OFF[i];
            if (i == 0) {   // HP is the only u16 row
                v.statAfter[i]  = *(const uint16_t*)(after  + off);
                v.statBefore[i] = *(const uint16_t*)(before + off);
            } else {
                v.statAfter[i]  = after[off];
                v.statBefore[i] = before[off];
            }
        }
        for (int e = 0; e < 8; e++) {
            v.elemDefAfter[e]  = *(const uint16_t*)(after  + JS_ELEM_DEF_OFF + e*2);
            v.elemDefBefore[e] = *(const uint16_t*)(before + JS_ELEM_DEF_OFF + e*2);
        }
        for (int s = 0; s < 13; s++) {
            v.stDefAfter[s]  = after[JS_ST_DEF_OFF + s];
            v.stDefBefore[s] = before[JS_ST_DEF_OFF + s];
        }
        v.elemAtkMask       = after [JS_EATK_MASK];
        v.elemAtkPct        = after [JS_EATK_PCT];
        v.elemAtkMaskBefore = before[JS_EATK_MASK];
        v.elemAtkPctBefore  = before[JS_EATK_PCT];

        v.stAtkMask = JuncAssembleStatusMask(after[JS_STATK_LOW7],
                                             *(const uint32_t*)(after + JS_STATUS_WORD));
        v.stAtkRaw  = *(const uint16_t*)(after + JS_STATK_RAW);
        v.stAtkMaskBefore = JuncAssembleStatusMask(before[JS_STATK_LOW7],
                                                   *(const uint32_t*)(before + JS_STATUS_WORD));
        v.stAtkRawBefore  = *(const uint16_t*)(before + JS_STATK_RAW);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    v.charName = MagicCharName(v.charId);   // same dream-party rule as everywhere else
    return true;
}

// ---------------------------------------------------------------------------
// Party abilities.
//
// ids >= 83 are not equippable: they are simply in force while the granting GF
// is junctioned, so 0x004E0110 never puts them in a list and nothing on screen
// enumerates them either. That makes this the one readout here with no sighted
// equivalent -- a sighted player has to go and read the GF's ability page too.
// ---------------------------------------------------------------------------
static int JsReadAbilityUnion(uint32_t* bitsOut)
{
    __try {
        const uint32_t* u = (const uint32_t*)JS_ABIL_UNION;
        for (int i = 0; i < 4; i++) bitsOut[i] = u[i];
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return 0;
}

static void JsSpeakPartyAbilities()
{
    uint32_t bits[4] = {0,0,0,0};
    if (JsReadAbilityUnion(bits) != 0) return;

    char buf[768];
    buf[0] = '\0';
    int said = 0;
    for (int id = JS_ABIL_PARTY_FIRST; id <= JS_ABIL_PARTY_LAST; id++) {
        if (!(bits[id >> 5] & (1u << (id & 31)))) continue;
        JuncAppend(buf, sizeof(buf), said++ ? ", " : "Party abilities. ");
        JuncAppend(buf, sizeof(buf), GetAbilityName((uint8_t)id));
    }
    if (!said) snprintf(buf, sizeof(buf), "No party abilities from the junctioned GFs");
    ScreenReader::Speak(buf, true);
    Log::Menu("[JuncStats] party abilities (%d): %s", said, buf);
}

// The equipped loadout, read straight off the character record -- three command
// slots then four ability slots, which is the same order the left panel walks.
static void JsSpeakEquipped(uint8_t charId)
{
    uint8_t cmds[3] = {0,0,0}, abils[4] = {0,0,0,0};
    __try {
        const uint8_t* ch = (const uint8_t*)(JS_CHAR_BASE + charId * JS_CHAR_STRIDE);
        for (int i = 0; i < 3; i++) cmds[i]  = ch[JS_CHR_COMMANDS + i];
        for (int i = 0; i < 4; i++) abils[i] = ch[JS_CHR_ABILITIES + i];
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    char buf[512];
    buf[0] = '\0';
    JuncAppend(buf, sizeof(buf), "Commands. ");
    for (int i = 0; i < 3; i++) {
        if (i) JuncAppend(buf, sizeof(buf), ", ");
        JuncAppend(buf, sizeof(buf), cmds[i] ? GetAbilityName(cmds[i]) : "empty");
    }
    JuncAppend(buf, sizeof(buf), ". Abilities. ");
    for (int i = 0; i < 4; i++) {
        if (i) JuncAppend(buf, sizeof(buf), ", ");
        JuncAppend(buf, sizeof(buf), abils[i] ? GetAbilityName(abils[i]) : "empty");
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[JuncStats] equipped: %s", buf);
}

// ---------------------------------------------------------------------------
// The help line for the highlighted row. Same mechanism as the Magic help bar:
// 0x004BD630 returns a pointer into loaded mngrp.bin text, and the byte stream
// is glyph+0x20 (which is what made the first Magic attempt read "AaI'UEIOE").
//
// SEH and std::string cannot share a frame under MSVC (C2712), so the raw copy
// is its own function -- lint_seh caught exactly this in the Magic work.
// ---------------------------------------------------------------------------
static int JsHelpRawCopy(uint8_t* out, size_t cap)
{
    __try {
        const uint8_t* txt = *(const uint8_t* volatile*)((uint8_t*)pMenuStateA + JSO_HELP_PTR);
        if (!txt) return 0;
        size_t len = 0;
        while (len < cap && txt[len] != 0) { out[len] = txt[len]; len++; }
        return (int)len;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static bool JsDecodedHelp(std::string& out)
{
    uint8_t raw[256];
    const int len = JsHelpRawCopy(raw, sizeof(raw));
    if (len <= 0) return false;
    uint8_t glyphs[256];
    const size_t gn = MagicTextToGlyphs(raw, (size_t)len, glyphs, sizeof(glyphs));
    if (gn == 0) return false;
    out = FF8TextDecode::DecodeMenuText(glyphs, gn);
    return !out.empty();
}

// ---------------------------------------------------------------------------
// The poll.
//
// Change detection is on the composed sentence plus the character, exactly as
// the Magic submenu settled on: the grid line carries the row name and the
// stat, so every distinct cursor position produces a distinct sentence, and a
// character swap forces a re-announcement because it is news the line does not
// otherwise carry.
// ---------------------------------------------------------------------------
static void PollJunctionStats()
{
    if (!pMenuStateA) return;

    uint8_t sub = 0xFF;
    uint16_t state = 0xFFFF;
    __try {
        sub   = *((uint8_t*)pMenuStateA + JUNC_ACTIVE_OFFSET);
        state = *(uint16_t*)((uint8_t*)pMenuStateA + JSO_STATE);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (s_prevCursor != 0 || sub != 17) {
        if (s_jsActive) ResetJunctionStats();
        return;
    }
    if (!s_jsActive) {
        s_jsActive = true;
        s_jsState = -1; s_jsLastChar = -1; s_jsCharWatch = -1; s_jsLastSpoken[0] = '\0';
    }

    // Runs in EVERY state, not just the two that speak lines: the switch is most
    // often made from the action row, which this file otherwise never touches.
    JsWatchCharacter(state, JsCharId());

    // Only the steady states. Every other value is a slide, a popup or a
    // one-frame action, and reading a half-built frame is how state 37 nearly
    // shipped as "the grid".
    //
    // **v0.23.1: this used to record the transient state, and that is what made
    // every page turn re-announce the header.** Paging the spell list is
    // 59 -> 60 (page left, 0x004DED18) or 62 (page right) -> 59, so a poll
    // landing on 60 rewrote s_jsState, and coming back to 59 looked like a fresh
    // arrival. **The Magic submenu had this exact bug in v0.22.1 -- "Magic list"
    // on every page turn -- and the fix there was the same: remember the last
    // state SPOKEN IN, never the last state seen.** Passing through 60 now
    // leaves every remembered value alone.
    if (state != JS_STATE_GRID && state != JS_STATE_MAGIC) return;

    JunctionView v;
    if (!FillJunctionView(v)) return;

    char line[512];
    line[0] = '\0';
    char header[192];
    header[0] = '\0';

    const bool charChangedHere = (s_jsLastChar >= 0 && (int)v.charId != s_jsLastChar);
    if (v.state == JS_STATE_GRID) {
        JuncAnnounceGrid(v, line, sizeof(line));
        JuncGridHeader(v, header, sizeof(header));
    } else {
        JuncAnnounceMagicChoice(v, line, sizeof(line));
        JuncMagicHeader(v, header, sizeof(header));
    }
    // A switch made while the line is already up is news about the CHARACTER, not
    // about arriving somewhere -- so the name replaces the arrival header rather
    // than being buried at the end of it.
    //
    // DEFENSIVE, and known to be: the game only accepts the switch on the action
    // row (see JsWatchCharacter), so reaching the grid after one is always a
    // STATE change, and the arrival header names the character anyway. This
    // branch is kept because it is two lines and it is what should happen if the
    // routing ever differs -- not because the game can currently reach it.
    if (charChangedHere && v.charName && v.charName[0])
        snprintf(header, sizeof(header), "%s", v.charName);

    if (line[0] == '\0') return;

    // The dedup key carries the cursor positions as well as the words, so a page
    // turn onto an identically-worded entry ("Empty", or the same spell held in
    // two slots) still speaks. Only the words are spoken.
    char sig[576];
    snprintf(sig, sizeof(sig), "%u|%u|%u|%s",
             (unsigned)v.state, (unsigned)v.gridCursor, (unsigned)v.magicCursor, line);

    const bool stateChanged = ((int)v.state != s_jsState);
    const bool charChanged  = charChangedHere;
    if (!stateChanged && !charChanged && strcmp(sig, s_jsLastSpoken) == 0) return;

    if ((stateChanged || charChanged) && header[0]) {
        char full[768];
        snprintf(full, sizeof(full), "%s. %s", header, line);
        ScreenReader::Speak(full, true);
    } else {
        ScreenReader::Speak(line, true);
    }
    snprintf(s_jsLastSpoken, sizeof(s_jsLastSpoken), "%s", sig);
    s_jsState = (int)v.state;
    s_jsLastChar = (int)v.charId;

    // The eligibility mask is only trustworthy in state 59: the game recomputes
    // it in state 58 on the way in, but the page-scroll states 53..56 do not, so
    // a grid line read on the frame after a column change carries the PREVIOUS
    // column's mask. Nothing on the grid uses it -- JuncAnnounceGrid never looks
    // at it -- but a stale number in the log invites a bug hunt, so it is only
    // printed where it means something.
    if (v.state == JS_STATE_MAGIC)
        Log::Menu("[JuncStats] state=%u char=%u grid=%u->slot %d rows=%u magic=%u elig=%08X : \"%s\"",
                  (unsigned)v.state, (unsigned)v.charId, (unsigned)v.gridCursor,
                  JuncSlotAtCursor(v), (unsigned)v.groupCount, (unsigned)v.magicCursor,
                  (unsigned)v.eligibleMask, line);
    else
        Log::Menu("[JuncStats] state=%u char=%u grid=%u->slot %d rows=%u : \"%s\"",
                  (unsigned)v.state, (unsigned)v.charId, (unsigned)v.gridCursor,
                  JuncSlotAtCursor(v), (unsigned)v.groupCount, line);
}

// ---------------------------------------------------------------------------
// Number keys, mirroring the Status screen. Aaron: *"Let's use number keys 0-9
// for various shortcuts/announcements, like we are doing on the Status screen.
// Both S and E are used by FF8 itself."*
//
// The meanings are per-screen, the way the Status screen's already are:
//
//   grid and magic list          ability screens
//   -------------------          ---------------
//   0  character and HP          0  equipped commands and abilities
//   1  elemental + status attack 1  party abilities (always active)
//   2..7 Str Vit Mag Spr Spd Luck 2 what the highlighted ability does
//   8  Evade and Hit
//   9  elemental + status defence
//
// Gated to the Junction subsystem so the digits stay free everywhere else.
// ---------------------------------------------------------------------------
// The state read and the help-text speak are their own functions because MSVC
// refuses __try in any function that also needs object unwinding (C2712), and
// the rule is PER FUNCTION, not per block -- lint_seh.py flagged this exact
// pairing before it reached a build, which is the second time that check has
// paid for itself.
static uint16_t JsReadState()
{
    __try { return *(uint16_t*)((uint8_t*)pMenuStateA + JSO_STATE); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0xFFFF; }
}

static void JsSpeakAbilityHelp()
{
    std::string d;
    if (JsDecodedHelp(d) && !d.empty()) {
        ScreenReader::Speak(d.c_str(), true);
        Log::Menu("[JuncStats] ability help: %s", d.c_str());
    }
}

static void JunctionNumberKeys()
{
    if (!s_jsActive || !pMenuStateA) return;

    int key = -1;
    for (int k = 0; k <= 9; k++)
        if (GetAsyncKeyState('0' + k) & 1) { key = k; break; }
    if (key < 0) return;

    const uint16_t state = JsReadState();
    if (state == 0xFFFF) return;

    if (state == JS_STATE_ABIL_SLOTS || state == JS_STATE_ABIL_LIST) {
        if (key == 0)      JsSpeakEquipped(JsCharId());
        else if (key == 1) JsSpeakPartyAbilities();
        else if (key == 2) JsSpeakAbilityHelp();
        return;
    }

    JunctionView v;
    if (!FillJunctionView(v)) return;

    char buf[800];
    buf[0] = '\0';
    if (key == 1)      JuncAnnounceAttack(v, buf, sizeof(buf));
    else if (key == 9) JuncAnnounceResistances(v, buf, sizeof(buf));
    else               JuncAnnounceStatKey(v, key, buf, sizeof(buf));

    if (buf[0] == '\0') return;
    ScreenReader::Speak(buf, true);
    Log::Menu("[JuncStats] key=%d char=%u: %s", key, (unsigned)v.charId, buf);
}
