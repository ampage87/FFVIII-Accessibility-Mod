// menu_card_model.inl -- v0.24.0 (#83)
//
// The Card album's ANNOUNCEMENT LOGIC: pure functions of a CardView, no Win32,
// no SEH, no absolute memory. Same split as menu_magic_model.inl and
// menu_junction_model.inl, for the same reason -- the wording is testable
// without playing the game.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE, before menu_tts_card.inl.
// Also compiled directly by tests/menu_sim.cpp.
//
// ---------------------------------------------------------------------------
// WHERE THIS COMES FROM
//
// Card module: dispatch index 7, creator 0x004EF020, update 0x004EF6F0, draw
// 0x004EF750, jump table 0x004EF6BC (13 states). Full write-up in
// docs/CARD_MENU_FINDINGS.md.
//
// **The album is READ-ONLY.** There is no confirm handler at all -- you move the
// cursor and you back out. That makes it the simplest screen in the main menu
// and also the one where getting the NUMBERS right is the entire job.
// ---------------------------------------------------------------------------

#ifndef MENU_CARD_MODEL_INCLUDED
#define MENU_CARD_MODEL_INCLUDED

#include "menu_card_data.inl"   // generated: CARD_DEFS[110], CARD_ELEMENTS[8]

// The one steady state. Everything else in the 13-state machine is init, a
// fade, or one of the two page-slide animations -- and states 7 and 9 DO read
// the input word (they queue a left/right page flip mid-slide), which is exactly
// the trap that made state 37 look like the junction grid. Reading input is not
// the test; being somewhere the player can sit still is.
static const int CARD_STATE_LIST = 5;

// The album is 10 pages ("levels") of 11 rows, and the cursor IS the card id --
// no indirection table, unlike the junction grid's 0x00B88604.
static const int CARD_ROWS_PER_PAGE = 11;
static const int CARD_PAGES         = 10;

// Category boundaries, straight out of the row-label switch at 0x004EF7xx.
static const char* CardCategory(int id)
{
    if (id < 0)   return "";
    if (id < 55)  return "Monster";
    if (id < 77)  return "Boss";
    if (id < 99)  return "GF";
    if (id < CARD_COUNT) return "Player";
    return "";
}

// How many of this card the player holds.
//   >= 1  held, and how many
//    0    seen but not held -- the row is DIMMED on screen
//   -1    never seen -- the row is BLANK on screen
// The getter is 0x00534950; the two encodings behind it (a 0x80|count byte for
// the 77 common cards, an owner code plus a separate "known" bit for the 33
// rares) are the memory file's problem, not this one's.
enum { CARD_UNKNOWN = -1, CARD_NOT_HELD = 0 };

struct CardView
{
    unsigned short state;
    int  cursor;            // module +0x2E, 0..109, and the card id directly
    int  count;             // as above: -1 unknown, 0 seen-not-held, >0 held
    int  totalHeld;         // every card, duplicates counted -- what the screen totals
    int  uniqueHeld;        // distinct cards held at least once
    int  seen;              // distinct cards ever seen

    // The game's OWN summary panel, module +0x32..+0x3A. It is on screen the
    // whole time -- MONSTER / BOSS / GF / PLAYER / TOTAL down the right-hand
    // side -- and the mod was reading none of it. These count cards held, with
    // duplicates, and are the numbers a sighted player is actually looking at.
    int  gameMonster, gameBoss, gameGF, gamePlayer, gameTotal;
};

static int  CardLevel(int id) { return (id >= 0 && id < CARD_COUNT) ? id / CARD_ROWS_PER_PAGE : -1; }
static int  CardRow(int id)   { return (id >= 0 && id < CARD_COUNT) ? id % CARD_ROWS_PER_PAGE : -1; }

// ===========================================================================
// The numbers.
//
// Aaron: *"Let's announce each card's values in the order of Top, Right, Down,
// Left. I think we should just announce each number with a clear space between
// rather than saying 'Top 5, Right 2, Bottom 3, Left 4' as that is verbose and
// will get old to the player."*
//
// So: "Geezard, 1 4 1 5". Clockwise from the top, four bare numbers. On a screen
// where the whole activity is comparing one card's four numbers against
// another's, the labels are pure overhead after the first card.
//
// **Ten is spoken as "A"**, because that is what the card shows and what every
// Triple Triad guide, opponent and rule discussion calls it. Saying "ten" would
// be a private vocabulary that matches nothing the player can look up.
// ===========================================================================
static void CardAppend(char* out, size_t n, const char* s)
{
    size_t l = strlen(out);
    if (l >= n - 1) return;
    snprintf(out + l, n - l, "%s", s);
}
static void CardAppendInt(char* out, size_t n, long v)
{
    char t[24]; snprintf(t, sizeof(t), "%ld", v);
    CardAppend(out, n, t);
}
static void CardAppendPower(char* out, size_t n, int v)
{
    if (v >= 10) CardAppend(out, n, "A");
    else         CardAppendInt(out, n, v);
}

static const char* CardElementName(unsigned char mask)
{
    if (!mask) return 0;
    for (int b = 0; b < 8; b++)
        if (mask & (1u << b)) return CARD_ELEMENTS[b];
    return 0;
}

// ===========================================================================
// The list line.
//
// "Geezard, 1 4 1 5, quantity 3"  -- held, three of them
// "Geezard, 1 4 1 5"               -- held exactly one; "1" adds nothing
// "Ifrit, 9 6 2 8, Fire, not held" -- seen, not held: the screen DIMS this row
// "Card 12, not seen"              -- never seen: the screen draws a BLANK row
//
// A never-seen row is blank on screen, so naming the card would tell the player
// something the game is deliberately withholding -- the album is a collection
// record and the blanks are the point of it. The position is still spoken so
// the list can be counted through.
// ===========================================================================
static void CardAnnounceLine(const CardView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int id = v.cursor;
    if (id < 0 || id >= CARD_COUNT) { CardAppend(out, n, "Empty"); return; }

    if (v.count == CARD_UNKNOWN) {
        CardAppend(out, n, "Card ");
        CardAppendInt(out, n, CardRow(id) + 1);
        CardAppend(out, n, ", not seen");
        return;
    }

    const CardDef& c = CARD_DEFS[id];
    CardAppend(out, n, c.name);
    CardAppend(out, n, ", ");
    CardAppendPower(out, n, c.top);    CardAppend(out, n, " ");
    CardAppendPower(out, n, c.right);  CardAppend(out, n, " ");
    CardAppendPower(out, n, c.bottom); CardAppend(out, n, " ");
    CardAppendPower(out, n, c.left);

    const char* el = CardElementName(c.elem);
    if (el) { CardAppend(out, n, ", "); CardAppend(out, n, el); }

    // **The word "quantity" is load-bearing.** Aaron: *"you hear the values
    // immediately followed by the quantity number without context. e.g. Bit Bug
    // 1 3 5 2 4 - that 4 at the end is actually the quantity, not one of the
    // values."* Four bare numbers are unambiguous precisely BECAUSE there are
    // always four of them; a fifth silently joins the set and the whole terse
    // form stops working. This is the one place on the line where a label costs
    // a word and buys back the format.
    //
    // It also matches what the Magic and Junction lists already say, so there is
    // one word for "how many of these you have" across the whole menu.
    if (v.count == CARD_NOT_HELD) CardAppend(out, n, ", not held");
    else if (v.count > 1)         { CardAppend(out, n, ", quantity "); CardAppendInt(out, n, v.count); }
}

// The header on arriving, and on every page turn -- the page IS the level, and
// the level is the only thing that tells you where you are in 110 cards.
static void CardAnnounceHeader(const CardView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int lvl = CardLevel(v.cursor);
    if (lvl < 0) return;
    CardAppend(out, n, "Level ");
    CardAppendInt(out, n, lvl + 1);
    const char* cat = CardCategory(v.cursor);
    if (cat[0]) { CardAppend(out, n, ", "); CardAppend(out, n, cat); }
}

// Position, on demand: which row of which page, and how many rows this page has.
// Every page is a full 11 rows -- 10 x 11 = 110 exactly -- so this is a constant,
// but saying it makes the list countable rather than something to feel around in.
static void CardAnnouncePosition(const CardView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int lvl = CardLevel(v.cursor), row = CardRow(v.cursor);
    if (lvl < 0) return;
    CardAppend(out, n, "Card ");
    CardAppendInt(out, n, row + 1);
    CardAppend(out, n, " of ");
    CardAppendInt(out, n, CARD_ROWS_PER_PAGE);
    CardAppend(out, n, ", level ");
    CardAppendInt(out, n, lvl + 1);
    CardAppend(out, n, " of ");
    CardAppendInt(out, n, CARD_PAGES);
    const char* cat = CardCategory(v.cursor);
    if (cat[0]) { CardAppend(out, n, ", "); CardAppend(out, n, cat); }
}

// The collection, on demand. The game's own summary counts cards HELD with
// duplicates; unique and seen are the two numbers a collector actually wants and
// the screen shows neither.
static void CardAnnounceTotals(const CardView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    // The screen's own five numbers first, in the screen's own order, because
    // that is what a sighted player is reading and what a guide will ask about.
    CardAppend(out, n, "Monster ");  CardAppendInt(out, n, v.gameMonster);
    CardAppend(out, n, ", boss ");   CardAppendInt(out, n, v.gameBoss);
    CardAppend(out, n, ", GF ");     CardAppendInt(out, n, v.gameGF);
    CardAppend(out, n, ", player "); CardAppendInt(out, n, v.gamePlayer);
    CardAppend(out, n, ", total ");  CardAppendInt(out, n, v.gameTotal);
    // Then the two the screen does NOT show, which are the ones a collector is
    // actually chasing: how much of the album is covered, and how much is even
    // known to exist.
    CardAppend(out, n, ". ");
    CardAppendInt(out, n, v.uniqueHeld);
    CardAppend(out, n, " different of ");
    CardAppendInt(out, n, CARD_COUNT);
    CardAppend(out, n, ", ");
    CardAppendInt(out, n, v.seen);
    CardAppend(out, n, " seen");
}

// The full detail for the card under the cursor, spoken with the labels -- the
// one place the verbose form earns its keep, because it is asked for explicitly
// rather than heard on every cursor move.
static void CardAnnounceDetail(const CardView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int id = v.cursor;
    if (id < 0 || id >= CARD_COUNT) return;
    if (v.count == CARD_UNKNOWN) { CardAppend(out, n, "Not seen yet"); return; }

    const CardDef& c = CARD_DEFS[id];
    CardAppend(out, n, c.name);
    CardAppend(out, n, ". Top ");    CardAppendPower(out, n, c.top);
    CardAppend(out, n, ", right ");  CardAppendPower(out, n, c.right);
    CardAppend(out, n, ", bottom "); CardAppendPower(out, n, c.bottom);
    CardAppend(out, n, ", left ");   CardAppendPower(out, n, c.left);
    const char* el = CardElementName(c.elem);
    CardAppend(out, n, ". Element ");
    CardAppend(out, n, el ? el : "none");
    CardAppend(out, n, ". ");
    if (v.count == CARD_NOT_HELD) CardAppend(out, n, "Not held");
    else { CardAppend(out, n, "Holding "); CardAppendInt(out, n, v.count); }
    CardAppend(out, n, ". ");
    CardAppend(out, n, CardCategory(id));
    CardAppend(out, n, " card, level ");
    CardAppendInt(out, n, CardLevel(id) + 1);
}

#endif // MENU_CARD_MODEL_INCLUDED
