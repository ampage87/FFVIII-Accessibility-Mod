// menu_magic_model.inl -- v0.22.0 (#81)
//
// The Magic submenu's ANNOUNCEMENT LOGIC, with no Win32, no SEH and no absolute
// memory reads. Everything here is a pure function of a MagicView struct that
// the caller fills in.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE. Included BEFORE menu_tts_ability.inl
// (which uses MAGIC_SPELL_NAMES) and before menu_tts_magic.inl (which fills the
// view from game memory). Also compiled directly by tests/menu_sim.cpp.
//
// WHY THE SPLIT. Every previous submenu put its memory reads and its wording in
// one file, so the only way to test the wording was to play the game. The Magic
// screen has six distinct phases, a 32-slot list paged 4 at a time, a cursor
// that is stored per character, and a destructive flow -- too much to validate
// one BAT at a time on a tester who is also the only user. With the logic in a
// pure function, tests/menu_sim.cpp drives all of it offline, and the file that
// touches the game only has to be right about ADDRESSES.
//
// ---------------------------------------------------------------------------
// WHERE THIS COMES FROM
//
// FF8_EN.exe's Magic module: creator 0x004F00D0, state machine 0x004F02F0
// (jump table 0x004F5C4C), draw 0x004F67C0. Reached from the submenu dispatch
// table at 0x00B87ED8 with index 3 -- which is exactly the value the mod already
// observed at pMenuStateA + 0x1E8 for Magic, so the identification is confirmed
// from two independent directions.
//
// The labels are NOT guessed. They are decoded from the game's own
// Data/lang-en/menu.fs -> mngrp.bin, section 1 bank 8 (the "group 8" the code
// passes to the text getter at 0x004BD630), whose entry tables are:
//
//     0x00B88A90  action row  {0, 1, 11, 2}   -> pairs 0,1,11,2
//     0x00B88A9C  sort popup  {15..21}        -> pairs 15..21
//
// Each "pair" is {label, help} at string indices 2p and 2p+1. Decoding those
// gives Use / Exchg. / All / Rearrange and the seven sort orders below, with
// their help text. See docs/MAGIC_MENU.md.
// ---------------------------------------------------------------------------

#ifndef MENU_MAGIC_MODEL_INCLUDED
#define MENU_MAGIC_MODEL_INCLUDED

// ===========================================================================
// Spell names -- THE canonical table for the whole mod.
//
// ids 0x00..0x38 (57 entries). This replaces the partial copy that used to live
// in menu_tts_ability.inl, which was correct to 37 and then WRONG: it listed
// Float/Drain/Pain at 38/39/40, where the real ids are Blind/Confuse/Sleep.
// Float is 47, Drain 44, Pain 45. That made the Ability screen's refine-preview
// stock line ("has N Floats") read a different spell's quantity. tests/menu_sim
// pins the ids that prove it.
//
// Cross-checked against the game's own mmagic.bin, whose field-usable bit is set
// on exactly ids 21,22,23,24,25,27,28 -- Cure, Cura, Curaga, Life, Full-Life,
// Esuna, Dispel, which is precisely the set FF8 lets you cast from the menu. A
// table with the wrong ids could not produce that.
// ===========================================================================
static const char* const MAGIC_SPELL_NAMES[] = {
    "(none)",                                                   // 0x00
    "Fire", "Fira", "Firaga",                                   // 0x01-03
    "Blizzard", "Blizzara", "Blizzaga",                         // 0x04-06
    "Thunder", "Thundara", "Thundaga",                          // 0x07-09
    "Water", "Aero", "Bio", "Demi", "Holy", "Flare",            // 0x0A-0F
    "Meteor", "Quake", "Tornado", "Ultima", "Apocalypse",       // 0x10-14
    "Cure", "Cura", "Curaga", "Life", "Full-Life",              // 0x15-19
    "Regen", "Esuna", "Dispel", "Protect", "Shell",             // 0x1A-1E
    "Reflect", "Aura", "Double", "Triple", "Haste",             // 0x1F-23
    "Slow", "Stop", "Blind", "Confuse", "Sleep",                // 0x24-28
    "Silence", "Break", "Death", "Drain", "Pain",               // 0x29-2D
    "Berserk", "Float", "Zombie", "Meltdown", "Scan",           // 0x2E-32
    "Full-Cure", "Wall", "Rapture", "Percent",                  // 0x33-36
    "Catastrophe", "The End",                                   // 0x37-38
};
static const int MAGIC_SPELL_NAME_COUNT =
    (int)(sizeof(MAGIC_SPELL_NAMES) / sizeof(MAGIC_SPELL_NAMES[0]));

static const char* MagicSpellName(unsigned id)
{
    if (id > 0 && (int)id < MAGIC_SPELL_NAME_COUNT) return MAGIC_SPELL_NAMES[id];
    return 0;
}

// ===========================================================================
// The action row and the sort popup, decoded from mngrp.bin group 8.
// ===========================================================================
struct MagicLabel { const char* label; const char* help; };

static const MagicLabel MAGIC_ACTIONS[4] = {
    { "Use",       "Use magic" },                              // pair 0
    { "Exchange",  "Exchange magic with members" },            // pair 1  (shown as "Exchg.")
    { "All",       "Take all magic from other members" },      // pair 11
    { "Rearrange", "Organizes magic during battle" },          // pair 2
};

// Seven entries, cursor 0..6, from table 0x00B88A9C. The interpuncts in the
// game's labels are read aloud as "then", which is what the order actually
// means and is far clearer than a punctuation character a screen reader may
// pronounce as "dot" or skip entirely.
static const MagicLabel MAGIC_SORT_ORDERS[7] = {
    { "Manual",                        "Rearrange manually" },
    { "Attack, then Restore, then Indirect", "" },
    { "Attack, then Indirect, then Restore", "" },
    { "Restore, then Attack, then Indirect", "" },
    { "Restore, then Indirect, then Attack", "" },
    { "Indirect, then Attack, then Restore", "" },
    { "Indirect, then Restore, then Attack", "" },
};

// ===========================================================================
// The view: everything the announcer needs, already read out of memory.
// ===========================================================================
struct MagicSlotView { unsigned char id, qty; };

struct MagicView
{
    unsigned short state;        // module +0x10  (pMenuStateA +0x22E)
    unsigned char  screenMode;   // module +0x56  (+0x274)
    unsigned char  charId;       // module +0x64  (+0x282)  savemap character id
    unsigned char  page;         // module +0x42  (+0x260)  0..7
    unsigned char  cursorRaw;    // module +0x38 + charId    (+0x256 + charId)
    unsigned char  actionCursor; // module +0x61  (+0x27F)   0..3
    unsigned char  actionMask;   // module +0x67  (+0x285)   bit n = action n enabled
    unsigned char  targetCursor; // module +0x57  (+0x275)
    unsigned char  targetCount;  // module +0x60  (+0x27E)
    unsigned short targetMask;   // module +0x36  (+0x254)   bit n = character n
    unsigned char  sortCursor;   // module +0x71  (+0x28F)   0..6
    unsigned short dialogOpen;   // module +0x6E  (+0x28C)   non-zero = yes/no up
    unsigned char  dialogCursor; // module +0x70  (+0x28E)
    unsigned char  dialogChar;   // module +0x32  (+0x250)
    unsigned char  dialogSlot;   // module +0x33  (+0x251)
    unsigned char  secondChar;   // module +0x62  (+0x280)
    // --- v0.22.1: the Exchange flow ---
    unsigned char  pageB;        // module +0x46  (+0x264)  partner's page
    unsigned char  popupCursor;  // module +0x5F  (+0x27D)
    unsigned char  popupKind;    // module +0x5E  (+0x27C)  bit0 = 2-entry, bit1 = 3-entry
    unsigned char  splitTake;    // module +0x58  (+0x276)  amount moving
    unsigned char  splitLeave;   // module +0x59  (+0x277)  amount staying
    unsigned char  splitSpell;   // module +0x5A  (+0x278)  the spell being split
    unsigned char  cursorRawB;   // module +0x38 + secondChar -- the partner's cursor

    MagicSlotView  slots[32];    // savemap char[charId].Magics[32]
    MagicSlotView  slotsB[32];   // savemap char[secondChar].Magics[32]
    unsigned char  menuLock;     // savemap +0xAE3
    unsigned char  mmagicFlag[MAGIC_SPELL_NAME_COUNT];  // mmagic.bin[id*4], byte 0
    unsigned char  targetType[MAGIC_SPELL_NAME_COUNT];  // magic data[id] +0x07

    const char*    charName;         // resolved by the caller
    const char*    memberName[8];    // roster names, for target select
};

enum MagicPhase {
    MP_NONE = 0,     // not on a Magic screen we speak
    MP_ACTION,       // state 3    -- the four-command row
    MP_LIST,         // state 13   -- the 32-slot spell list
    MP_TARGET,       // state 20   -- choosing who to cast on
    MP_SORT,         // state 72   -- the seven-order popup
    MP_DISCARD,      // yes/no confirmation (overrides the state)
    MP_CLOSING,      // states 112/113
    // --- v0.22.1: the two-character flows, which v0.22.0 left silent ---
    MP_XCHG_MINE,    // state 26   -- Exchange: browsing YOUR list
    MP_XCHG_PARTNER, // state 28   -- Exchange: choosing the partner character
    MP_XCHG_THEIRS,  // state 44   -- Exchange: browsing the PARTNER's list
    MP_XCHG_POPUP,   // states 52 / 55 -- Give All / Take All / Split
    MP_XCHG_SPLIT,   // state 63   -- the quantity picker
    MP_ALL_RECEIVER, // state 97   -- All: choose who RECEIVES
    MP_ALL_GIVER,    // state 99   -- All: choose who is emptied
    MP_ALL_DONE,     // state 105  -- the transfer ran
    MP_ALL_WARN      // state 106  -- pre-flight warning instead
};

// The yes/no dialog is drawn OVER whatever state is running, so it is tested
// first. Everything else keys off the module's state word.
//
// v0.22.1: **gate on the STATE, never on the screen mode.** v0.22.0 used
// screenMode == 3 as "choosing the second character", which is true for both
// Exchange and All -- so the two flows were indistinguishable and the mod said
// something vague or nothing at all. The Exchange and All state numbers are
// reached from nowhere else in the machine, which makes them exact.
static MagicPhase MagicPhaseOf(const MagicView& v)
{
    if (v.dialogOpen != 0) return MP_DISCARD;
    switch (v.state) {
        case 3:   return MP_ACTION;
        case 13:  return MP_LIST;
        case 20:  return MP_TARGET;
        case 72:  return MP_SORT;
        case 112:
        case 113: return MP_CLOSING;
        // Exchange (action 1)
        case 26:  return MP_XCHG_MINE;
        case 28:  return MP_XCHG_PARTNER;
        case 44:  return MP_XCHG_THEIRS;
        case 52:
        case 55:  return MP_XCHG_POPUP;
        case 63:  return MP_XCHG_SPLIT;
        // All (action 2). Step 1 picks the RECEIVER and step 2 the GIVER --
        // see the note on MagicAnnounce's MP_ALL_* cases.
        case 97:  return MP_ALL_RECEIVER;
        case 99:  return MP_ALL_GIVER;
        case 105: return MP_ALL_DONE;
        case 106:
        case 107: return MP_ALL_WARN;   // 107 is the box's own poll state
        default:  break;
    }
    return MP_NONE;
}

// Does this phase's announcement already name the character it is about? For
// those, a character change must NOT re-speak the step prompt: the line itself
// carries the news. v0.22.3 forced the header on every character change, which
// on the All steps meant "Select member to receive magic" ahead of every single
// left/right -- the repetition Aaron reported.
static bool MagicLineNamesCharacter(MagicPhase phase)
{
    return phase == MP_ALL_RECEIVER || phase == MP_ALL_GIVER ||
           phase == MP_XCHG_PARTNER;
}

// Total magic a character is holding. v0.22.4 uses this to detect that an All
// transfer HAPPENED, because the state that performs it cannot be observed --
// see the note on MP_ALL_DONE in menu_tts_magic.inl.
static long MagicTotalHeld(const MagicSlotView* slots)
{
    long t = 0;
    for (int i = 0; i < 32; i++) if (slots[i].id != 0) t += slots[i].qty;
    return t;
}

// Column B's highlighted slot, for the Exchange screen. Same shape as
// MagicSlotIndex but keyed on the partner's character id and page +0x46 --
// both columns share the one per-character cursor array at +0x38.
static int MagicSlotIndexB(const MagicView& v)
{
    return (int)(v.cursorRawB & 3) + (int)v.pageB * 4;
}

// **THE ONE FORMULA THAT MATTERS.** The list is 1 column x 4 visible rows x 8
// pages. Up/Down wrap inside the page and never scroll; Left/Right change the
// page. The stored cursor byte is resynced to the absolute index every frame,
// but it lags by one frame right after a page change, so the low two bits are
// the only part of it that can be trusted -- masking and re-deriving from the
// page is what the game's own draw path does at 0x004F0597.
//
// Do NOT use pMenuStateA + 0x272 here. That is the Item submenu's list cursor;
// in the Magic module the same byte is written only in the post-sort redraw
// state, so it is stale or wrong on every other frame.
static int MagicSlotIndex(const MagicView& v)
{
    return (int)(v.cursorRaw & 3) + (int)v.page * 4;
}

// Field-castable, replicating the draw path at 0x004F71DB that decides whether
// to render the name in colour 7 (normal) or colour 1 (greyed).
//
// mmagic.bin bit 0 is the base flag -- set on exactly seven spells. The extra
// clause matters in one situation the player WILL hit: while savemap +0xAE3 has
// bit 0x40 set (a story lock), spells whose target type is 5 or 6 are greyed
// even though their base flag is set. That is why this is not just a lookup.
static bool MagicCastable(const MagicView& v, unsigned id)
{
    if (id == 0 || (int)id >= MAGIC_SPELL_NAME_COUNT) return false;
    if ((v.mmagicFlag[id] & 1) == 0) return false;
    if (v.menuLock & 0x40) {
        const unsigned char tt = v.targetType[id];
        if (tt == 5 || tt == 6) return false;
    }
    return true;
}

// Target select walks the SET BITS of the character mask, so cursor 2 is the
// third available character, not character 2. Returns -1 if the cursor is past
// the end of the mask.
static int MagicTargetChar(const MagicView& v)
{
    int seen = 0;
    for (int i = 0; i < 8; i++) {
        if (!(v.targetMask & (1u << i))) continue;
        if (seen == (int)v.targetCursor) return i;
        seen++;
    }
    return -1;
}

// How many actions are actually selectable. The game's cursor skips disabled
// entries, so "1 of 4" would be a lie when only two are usable.
static int MagicEnabledActionCount(const MagicView& v)
{
    int n = 0;
    for (int i = 0; i < 4; i++) if (v.actionMask & (1u << i)) n++;
    return n;
}

// Position of actionCursor among the ENABLED actions, 1-based; 0 if the cursor
// sits on a disabled entry (which the game should never allow).
static int MagicEnabledActionPos(const MagicView& v)
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (!(v.actionMask & (1u << i))) continue;
        n++;
        if (i == (int)v.actionCursor) return n;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Small formatting helpers -- no <cstdio> dependency beyond snprintf, which the
// host tests and the game build both have.
// ---------------------------------------------------------------------------
static void MagicAppend(char* out, size_t n, const char* s)
{
    size_t l = strlen(out);
    if (l >= n - 1) return;
    snprintf(out + l, n - l, "%s", s);
}

static void MagicAppendInt(char* out, size_t n, long v)
{
    char t[24];
    snprintf(t, sizeof(t), "%ld", v);
    MagicAppend(out, n, t);
}

// ===========================================================================
// v0.22.2: TEXT-STREAM BYTES -> GLYPH INDICES.
//
// The help bar came out garbled -- "Use magic" was read aloud as "AaI'UEIOE".
// Cause: FF8TextDecode::DecodeMenuText indexes its 224-entry table with a GLYPH
// INDEX, because its existing caller feeds it the GCW buffer, which the renderer
// has already converted. The strings in mngrp.bin are one stage earlier: they
// are TEXT-STREAM bytes, and a text byte is `glyph + 0x20`.
//
// So every character came out 32 glyph slots too high. Proof, from the real
// file (group 8 string 1, bytes 59 71 63 20 6B 5F 65 67 61):
//
//     indexed raw   -> "AaI'UEIOE"      <- what Aaron heard
//     minus 0x20    -> "Use magic"
//
// The v0.22.1 comment claiming the bar and the scrape "were always reading the
// same encoding, just at different points in the pipeline" was half right and
// therefore wrong: same TABLE, different OFFSET, and the offset is the whole
// bug. DecodeMenuText itself is left alone -- its other callers are correct.
//
// Bytes below 0x20 are control codes. 0x0A..0x0E introduce a variable (the
// game's own strings include things like "Can't carry {0A}5"), so their
// parameter byte is skipped too rather than being read as a letter.
// ===========================================================================
static size_t MagicTextToGlyphs(const unsigned char* in, size_t n,
                                unsigned char* out, size_t cap)
{
    size_t w = 0;
    for (size_t i = 0; i < n && w < cap; i++) {
        const unsigned char b = in[i];
        if (b == 0) break;
        if (b < 0x20) {
            if (b >= 0x0A && b <= 0x0E) i++;   // skip the variable's parameter
            continue;
        }
        out[w++] = (unsigned char)(b - 0x20);
    }
    return w;
}

// One spell slot, described. Shared by the Use list and by both columns of the
// Exchange screen so they cannot drift apart in wording.
//
// v0.22.2: `owner` is now spoken by the PHASE HEADER, not on every line. Saying
// "Zell" before each of the 32 slots is the repetition Aaron reported -- the
// panel does not change while you move within it, so the name is news exactly
// once, when you arrive.
static void MagicDescribeSlot(const MagicView& v, const MagicSlotView* slots,
                              int slot, char* out, size_t n)
{
    if (slot < 0 || slot >= 32) { MagicAppend(out, n, "Empty"); return; }
    const unsigned id  = slots[slot].id;
    const unsigned qty = slots[slot].qty;
    if (id == 0 || qty == 0) {
        MagicAppend(out, n, "Empty");
    } else {
        const char* nm = MagicSpellName(id);
        if (nm) MagicAppend(out, n, nm);
        else { MagicAppend(out, n, "Magic "); MagicAppendInt(out, n, (long)id); }
        MagicAppend(out, n, ", quantity ");
        MagicAppendInt(out, n, (long)qty);
    }
    MagicAppend(out, n, ", slot ");
    MagicAppendInt(out, n, (long)(slot + 1));
    MagicAppend(out, n, " of 32");
}

// ===========================================================================
// THE ANNOUNCEMENT.
//
// House style, matching the Item submenu: "<name>, quantity <n>, ..." with the
// position last, "Empty" for an empty slot, and no trailing punctuation.
// ===========================================================================
static void MagicAnnounce(const MagicView& v, MagicPhase phase, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';

    switch (phase) {

    case MP_ACTION: {
        const int idx = (v.actionCursor < 4) ? v.actionCursor : 0;
        MagicAppend(out, n, MAGIC_ACTIONS[idx].label);
        // Only qualify the position when some entries are unavailable -- with
        // all four live it is just noise on every left/right press.
        const int total = MagicEnabledActionCount(v);
        if (total > 0 && total < 4) {
            const int pos = MagicEnabledActionPos(v);
            if (pos > 0) {
                MagicAppend(out, n, ", ");
                MagicAppendInt(out, n, pos);
                MagicAppend(out, n, " of ");
                MagicAppendInt(out, n, total);
            }
        }
        break;
    }

    case MP_LIST: {
        const int slot = MagicSlotIndex(v);
        if (slot < 0 || slot >= 32) { MagicAppend(out, n, "Empty"); break; }
        const unsigned id  = v.slots[slot].id;
        const unsigned qty = v.slots[slot].qty;
        if (id == 0 || qty == 0) {
            MagicAppend(out, n, "Empty");
        } else {
            const char* nm = MagicSpellName(id);
            if (nm) MagicAppend(out, n, nm);
            else { MagicAppend(out, n, "Magic "); MagicAppendInt(out, n, (long)id); }
            MagicAppend(out, n, ", quantity ");
            MagicAppendInt(out, n, (long)qty);
            // **The greyed-out state is invisible to a blind player**, and it is
            // the difference between a spell that will cast and one that will
            // silently refuse. Only say it in the "Use" flow, where it decides
            // something; in Exchange/Rearrange every spell is fair game.
            if (v.screenMode == 1 && !MagicCastable(v, id))
                MagicAppend(out, n, ", cannot be cast here");
        }
        MagicAppend(out, n, ", slot ");
        MagicAppendInt(out, n, (long)(slot + 1));
        MagicAppend(out, n, " of 32");
        break;
    }

    case MP_TARGET: {
        const int ch = MagicTargetChar(v);
        const char* nm = (ch >= 0 && ch < 8) ? v.memberName[ch] : 0;
        if (nm && nm[0]) MagicAppend(out, n, nm);
        else if (ch >= 0) { MagicAppend(out, n, "Member "); MagicAppendInt(out, n, (long)(ch + 1)); }
        else {
            // The cursor is past the end of the mask, which the game should not
            // allow. Say so and stop -- appending "4 of 3" after it, as the
            // first draft did, is worse than useless to someone who cannot see
            // that nothing is highlighted.
            MagicAppend(out, n, "No target");
            break;
        }
        if (v.targetCount > 1) {
            MagicAppend(out, n, ", ");
            MagicAppendInt(out, n, (long)(v.targetCursor + 1));
            MagicAppend(out, n, " of ");
            MagicAppendInt(out, n, (long)v.targetCount);
        }
        break;
    }

    case MP_SORT: {
        const int idx = (v.sortCursor < 7) ? v.sortCursor : 0;
        MagicAppend(out, n, MAGIC_SORT_ORDERS[idx].label);
        MagicAppend(out, n, ", ");
        MagicAppendInt(out, n, (long)(idx + 1));
        MagicAppend(out, n, " of 7");
        break;
    }

    case MP_DISCARD: {
        // The game's own wording is "Discarding all of <who>'s <spell>. OK?".
        // Lead with the consequence, then the choice under the cursor -- a
        // player who tabs onto this mid-sentence still hears what it destroys.
        MagicAppend(out, n, "Discard all of ");
        const char* who = (v.dialogChar < 8) ? v.memberName[v.dialogChar] : 0;
        if (who && who[0]) { MagicAppend(out, n, who); MagicAppend(out, n, "'s "); }
        const unsigned sid = (v.dialogSlot < 32) ? v.slots[v.dialogSlot].id : 0;
        const char* sn = MagicSpellName(sid);
        MagicAppend(out, n, sn ? sn : "magic");
        MagicAppend(out, n, "? ");
        // FF8's yes/no window puts Yes first; cursor 0 = Yes.
        MagicAppend(out, n, v.dialogCursor == 0 ? "Yes" : "No");
        break;
    }

    case MP_XCHG_PARTNER: {
        const char* nm = (v.secondChar < 8) ? v.memberName[v.secondChar] : 0;
        if (nm && nm[0]) MagicAppend(out, n, nm);
        else { MagicAppend(out, n, "Member "); MagicAppendInt(out, n, (long)(v.secondChar + 1)); }
        break;
    }

    // -- Exchange -----------------------------------------------------------
    // Both lists read exactly like the Use list, but each names WHOSE list it
    // is. On a two-panel screen with no visual reference, "Cure, quantity 47"
    // twice over is the ambiguity that made this flow unusable.
    case MP_XCHG_MINE:
        MagicDescribeSlot(v, v.slots, MagicSlotIndex(v), out, n);
        break;

    case MP_XCHG_THEIRS:
        MagicDescribeSlot(v, v.slotsB, MagicSlotIndexB(v), out, n);
        break;

    case MP_XCHG_POPUP: {
        // The draw function picks the entries from module +0x5E: bit 1 means the
        // three-entry popup, bit 0 the two-entry one. Entry text is group 8, and
        // the two-entry popup's first entry depends on whether the destination
        // slot is occupied -- which the mod cannot see, so it names both only
        // when it must. (The help bar, on "/", always has the exact wording.)
        const bool three = (v.popupKind & 2) != 0;
        const int  count = three ? 3 : 2;
        int idx = v.popupCursor;
        if (idx >= count) idx = 0;
        static const char* THREE[3] = { "Give All", "Take All", "Split" };
        static const char* TWO[2]   = { "Give All", "Split" };
        MagicAppend(out, n, three ? THREE[idx] : TWO[idx]);
        MagicAppend(out, n, ", ");
        MagicAppendInt(out, n, (long)(idx + 1));
        MagicAppend(out, n, " of ");
        MagicAppendInt(out, n, (long)count);
        break;
    }

    case MP_XCHG_SPLIT: {
        // Two paired counters whose sum is conserved. Saying both every time is
        // the whole point -- the screen shows two numbers and a blind player
        // needs to know which way the last keypress moved them.
        const char* sn = MagicSpellName(v.splitSpell);
        if (sn) { MagicAppend(out, n, sn); MagicAppend(out, n, ", "); }
        MagicAppend(out, n, "move ");
        MagicAppendInt(out, n, (long)v.splitTake);
        MagicAppend(out, n, ", keep ");
        MagicAppendInt(out, n, (long)v.splitLeave);
        break;
    }

    // -- All ----------------------------------------------------------------
    // **THE DIRECTION IS RECEIVER FIRST.** The tester remembered it the other
    // way round, and the code is unambiguous: state 105 calls 0x004F5FA0 with
    // arg1 = module +0x62 (the step-2 choice) and arg2 = +0x64 (the step-1
    // choice); inside, arg1 loses every spell and arg2 gains them. Step 2's
    // character mask is also built as `available & ~(1 << +0x64)`, which only
    // makes sense if step 1 already fixed a role.
    //
    // The game's own help bar says the same thing -- group 8 entry 12 on step 1
    // is "Select member to receive magic" and entry 13 on step 2 is "Select
    // member to transfer magic" -- so these lines are the game's wording, not an
    // interpretation of it. Announcing "choose who gives" on the receiver step
    // would have been an accessibility bug dressed as a feature.
    case MP_ALL_RECEIVER: {
        const char* nm = (v.charId < 8) ? v.memberName[v.charId] : 0;
        MagicAppend(out, n, nm && nm[0] ? nm : "Member");
        MagicAppend(out, n, ", receives");
        break;
    }

    case MP_ALL_GIVER: {
        const char* nm = (v.secondChar < 8) ? v.memberName[v.secondChar] : 0;
        MagicAppend(out, n, nm && nm[0] ? nm : "Member");
        MagicAppend(out, n, ", gives all magic");
        break;
    }

    case MP_ALL_DONE: {
        const char* from = (v.secondChar < 8) ? v.memberName[v.secondChar] : "Member";
        const char* to   = (v.charId     < 8) ? v.memberName[v.charId]     : "Member";
        MagicAppend(out, n, "All magic moved from ");
        MagicAppend(out, n, from);
        MagicAppend(out, n, " to ");
        MagicAppend(out, n, to);
        break;
    }

    case MP_ALL_WARN:
        MagicAppend(out, n, "Cannot take all magic. See the message on screen");
        break;

    case MP_CLOSING:
    case MP_NONE:
    default:
        break;
    }
}

// The header line spoken once on entering a phase, before the per-item line.
// Empty string means "say nothing extra".
// v0.22.1: the two All headers are the game's own help-bar strings (group 8
// entries 12 and 13), so a sighted player reading the screen and a blind player
// hearing the mod are told the same thing in the same words.
// v0.22.2: the two Exchange list headers name WHOSE list you have landed in, so
// the per-slot lines no longer have to. Written into a caller-supplied buffer
// because those two are the only dynamic ones.
static void MagicPhaseHeaderBuf(const MagicView& v, MagicPhase phase, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    // v0.22.3: derive the name from charId rather than trusting the cached
    // charName. They cannot desync if only one of them is consulted, and a
    // character swap updates charId first.
    const char* self = (v.charId < 8 && v.memberName[v.charId] && v.memberName[v.charId][0])
                     ? v.memberName[v.charId]
                     : (v.charName ? v.charName : 0);
    const char* fixed = 0;
    switch (phase) {
        case MP_ACTION:        fixed = self ? self : "Magic"; break;
        case MP_TARGET:        fixed = "Choose target"; break;
        case MP_SORT:          fixed = "Rearrange order"; break;
        case MP_XCHG_PARTNER:  fixed = "Exchange with which member"; break;
        case MP_XCHG_POPUP:    fixed = "Choose how much"; break;
        case MP_XCHG_SPLIT:    fixed = "Split"; break;
        case MP_ALL_RECEIVER:  fixed = "Select member to receive magic"; break;
        case MP_ALL_GIVER:     fixed = "Select member to transfer magic"; break;
        // v0.22.3: the list header NAMES THE CHARACTER. L1/R1 changes character
        // without leaving the phase, and "Magic list" said nothing about whose
        // list you had just landed in -- the 2026-08-16 log shows a switch from
        // Irvine to Selphie announced only as "Cure, quantity 82, slot 1 of 32".
        case MP_LIST:
        case MP_XCHG_MINE: {
            MagicAppend(out, n, self ? self : "Your");
            MagicAppend(out, n, "'s magic");
            return;
        }
        case MP_XCHG_THEIRS: {
            const char* who = (v.secondChar < 8) ? v.memberName[v.secondChar] : 0;
            MagicAppend(out, n, (who && who[0]) ? who : "Partner");
            MagicAppend(out, n, "'s magic");
            return;
        }
        default: return;
    }
    if (fixed) MagicAppend(out, n, fixed);
}

// Compatibility wrapper for the fixed-text phases; returns "" for the two that
// need the buffer form.
static const char* MagicPhaseHeader(const MagicView& v, MagicPhase phase)
{
    static char buf[96];
    MagicPhaseHeaderBuf(v, phase, buf, sizeof(buf));
    return buf;
}

#endif // MENU_MAGIC_MODEL_INCLUDED
