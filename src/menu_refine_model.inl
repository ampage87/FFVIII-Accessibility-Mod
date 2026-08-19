// menu_refine_model.inl -- v0.33.0 (#91)
//
// The refine screen's shape, as its own state machine describes it. No memory
// reads live here: every function takes the bytes the caller already read, so
// tests/menu_ability_compile.cpp and tests/menu_sim.cpp can drive the whole
// decision surface without the game.
//
// ===========================================================================
// WHY THIS FILE EXISTS
// ---------------------------------------------------------------------------
// Through v0.32.1 the mod modelled the refine flow as ONE flow: browse an item
// list, pick a recipient, choose a quantity. That is sub-mode 0 and nothing
// else, and the v0.32.1 BAT caught both ways it fails:
//
//   * **Mid Mag-RF read the item inventory while the screen showed a list of
//     characters.** The log has the mod saying "Potion, 34", "Phoenix Down, 3",
//     "Elixir, 6" against a screen (f11_151824_110.png) whose only panel is
//     Squall/Zell/Irvine/Quistis/Rinoa/Selphie with their HP. Its own help line
//     said what it was: *"Refine Mid-Level Magic from other Magic."*
//   * **Tool-RF's quantity popup said nothing at all.** The screen
//     (f11_151810_436.png) shows "Force Armlet:1 will refine into 30 Shell
//     Stones", a source count, a "Number to refine" and a result count. The GCW
//     in the log holds every word of it and no [MenuTTS] line follows.
//
// One root cause. The old gate was `+0x53 == 0`, a byte the engine clears at
// state 0x1A -- the FIRST STATE OF THE RECIPIENT PICKER -- and 0x1A is only
// ever entered by sub-mode 0. Tool-RF (sub-mode 1) goes state 0x14 -> 0x28
// straight to the quantity popup and Mid Mag-RF (sub-mode 2) goes 0x14 -> 0x1D
// to a magic grid, so on both of them the byte stayed 0xFF, the gate never
// opened, and the item-list branch ran over a screen that was not an item list.
//
// ===========================================================================
// THE FIVE SUB-MODES  (module +0x45, from the ability descriptor's byte +5 --
// 0x004D71D4 calls 0x004E7620(id) = 0x01CF7F28 + id*8 and copies +5)
// ---------------------------------------------------------------------------
// Every one of these is a jump-table index in the engine, not a guess. The
// source list's base comes from the table at 0x004D8DA8 (read by 0x004D8CC0);
// the post-source destination from the table at 0x004D8C04 (state 0x14); the
// grant/consume calls from the table at 0x004D8B7C (state 0x2A).
//
//   | +0x45 | source rows                    | result   | after the source     |
//   |-------|--------------------------------|----------|----------------------|
//   |   0   | item inventory (0x01CFE79C)    | magic    | recipient picker     |
//   |   1   | item inventory                 | item     | quantity popup       |
//   |   2   | a CHARACTER'S MAGIC            | magic    | quantity popup       |
//   |   3   | item inventory                 | item     | quantity popup       |
//   |   4   | the CARD list (0x01D8B064)     | item     | quantity popup       |
//
// Sub-mode 2 is the odd one and the one that was wrong: 0x004D83C6 resolves the
// character from cursor +0x49 BEFORE the source list exists, then 0x004D8CDD
// points the source at 0x01CFE0F8 + charId*152 -- thirty-two {id, count} magic
// slots (the 0x20 loop at 0x004D7BEE proves the count).
//
// Sub-mode 4's list is BUILT by the creator at 0x004D7344: it walks card ids
// 0..0x6D, and for each one the player owns writes {cardId + 1, count} into
// 0x01D8B064. So Card Mod's rows can be named exactly -- v0.32.0 said
// "Card 1", "Card 2" because this file did not exist yet.
// ===========================================================================

#ifndef MENU_REFINE_MODEL_INCLUDED
#define MENU_REFINE_MODEL_INCLUDED

// --- sub-modes (module +0x45) ---------------------------------------------
static const int RF_SUB_ITEM_TO_MAGIC = 0;
static const int RF_SUB_ITEM_TO_ITEM  = 1;
static const int RF_SUB_MAGIC_TO_MAGIC = 2;
static const int RF_SUB_ITEM_TO_MED   = 3;
static const int RF_SUB_CARD_TO_ITEM  = 4;
static const int RF_SUB_MAX           = 4;

// --- what the rows under the source cursor ARE -----------------------------
enum RefineSourceKind { RF_SRC_ITEMS = 0, RF_SRC_MAGIC = 1, RF_SRC_CARDS = 2 };

static RefineSourceKind RefineSourceKindOf(int submode)
{
    if (submode == RF_SUB_MAGIC_TO_MAGIC) return RF_SRC_MAGIC;
    if (submode == RF_SUB_CARD_TO_ITEM)   return RF_SRC_CARDS;
    return RF_SRC_ITEMS;
}

// --- what the recipe produces (entry +7 is an id in one namespace or the
//     other; naming it out of the wrong table is a confident wrong name) -----
static bool RefineResultIsMagic(int submode)
{
    return submode == RF_SUB_ITEM_TO_MAGIC || submode == RF_SUB_MAGIC_TO_MAGIC;
}

// --- which screen is up ----------------------------------------------------
enum RefinePhase {
    RF_PHASE_SOURCE = 0,     // the source list -- cursor +0x49
    RF_PHASE_CHARSRC,        // sub-mode 2 only: WHOSE magic -- cursor +0x49
    RF_PHASE_MAGICGRID,      // sub-mode 2 only: that character's spells -- +0x4A
    RF_PHASE_RECIPIENT,      // sub-mode 0 only: who receives -- cursor +0x4A
    RF_PHASE_QUANTITY        // the number-to-refine popup -- +0x4F of +0x4C
};

// Engine states worth naming (jump table 0x004D8A8C, 45 entries, 0x00..0x2C):
static const int RF_ST_QTY_ARM   = 0x28;  // 0x004D8959: sets +0x4C/+0x4F/+0x51
static const int RF_ST_QTY       = 0x29;  // 0x004D78A8: the popup's own loop
static const int RF_ST_EXECUTE   = 0x2A;  // 0x004D7A37: grants and consumes
static const int RF_ST_GRID_LO   = 0x1D;  // 0x004D8584: sub-mode 2 enters here
static const int RF_ST_GRID_HI   = 0x24;
static const int RF_ST_RECIP_LO  = 0x1A;  // 0x004D7540: sub-mode 0 enters here
static const int RF_ST_RECIP_HI  = 0x1C;
static const int RF_ST_BACK_LO   = 0x25;  // 0x004D88B0: cancel, routed by +0x45
static const int RF_ST_BACK_HI   = 0x27;
static const int RF_ST_MAX       = 0x2C;

// qtyOpen is the engine's OWN flag (+0x51): set to 1 at state 0x28
// (0x004D89F8) and cleared on both ways out of the popup -- cancel at
// 0x004D7946, confirm at 0x004D7A29. Preferring it over a state range means the
// popup cannot be missed while the engine animates between states.
static RefinePhase RefinePhaseOf(int state, int submode, int qtyOpen)
{
    if (qtyOpen == 1 || state == RF_ST_QTY || state == RF_ST_QTY_ARM)
        return RF_PHASE_QUANTITY;
    // NOTE: state 0x28 counts as the popup so the source branch cannot fire
    // over it -- but 0x28 is also where +0x4C and +0x4F are WRITTEN
    // (0x004D89F1/0x004D89F4). Reading them during it caught the pre-write
    // zeroes once in the v0.33.0 BAT ("Number to refine 0, makes 0 Death
    // Stone"), so the caller must also wait for RefineQtyReady().

    if (submode == RF_SUB_MAGIC_TO_MAGIC) {
        if (state >= RF_ST_GRID_LO && state <= RF_ST_GRID_HI) return RF_PHASE_MAGICGRID;
        if (state >= RF_ST_BACK_LO && state <= RF_ST_BACK_HI) return RF_PHASE_MAGICGRID;
        return RF_PHASE_CHARSRC;
    }

    if (submode == RF_SUB_ITEM_TO_MAGIC) {
        if (state >= RF_ST_RECIP_LO && state <= RF_ST_RECIP_HI) return RF_PHASE_RECIPIENT;
        if (state >= RF_ST_BACK_LO && state <= RF_ST_BACK_HI) return RF_PHASE_RECIPIENT;
    }
    return RF_PHASE_SOURCE;
}

// Which module byte holds the live cursor for a phase. Getting this wrong is
// how v0.29.x read the GF list and the item targets: the cursor was real, it
// just belonged to a different list than the one being named.
static int RefineCursorOffsetFor(RefinePhase p)
{
    switch (p) {
        case RF_PHASE_MAGICGRID:
        case RF_PHASE_RECIPIENT: return 0x4A;
        default:                 return 0x49;
    }
}

// --- the quantity popup's arithmetic --------------------------------------
// Recipe entry (module +0x24, stride 8), read exactly as state 0x2A does at
// 0x004D7A4C..0x004D7A74:
//     +0x00 u16  result name string offset
//     +0x02 u16  result count PER UNIT   (multiplied by the count at 0x004D7A74)
//     +0x04 u8   required refine level   (compared against +0x47, 0x004D8D4A)
//     +0x05 u8   source id               (matched at 0x004D8D2D)
//     +0x06 u8   source count PER UNIT   (multiplied at 0x004D7A71)
//     +0x07 u8   result id
struct RefineRecipe { int resultPer, level, sourceId, sourcePer, resultId; };

struct RefineQtyLine { long produced; long consumed; long remaining; long total; };

// v0.33.1: the popup's THIRD ROW is the resulting STOCK, not the amount
// produced. Aaron's screenshots settle it -- "Cura 87" with 16 being made (he
// held 71), "Fira 51" with 13 being made, "Death Stone 4" with 4 being made from
// none. Both grant paths clamp at 0x64: magic at 0x004C2CCC and items at
// 0x004D7D54/0x004D7D70, so the number the screen will land on is capped too.
static const int RF_STOCK_CAP = 100;

// The popup is not worth speaking until the engine has filled it in.
static bool RefineQtyReady(int count, int qtyMax) { return count >= 1 && qtyMax >= 1; }

static RefineQtyLine RefineQtyMath(const RefineRecipe& r, int count, int owned, int stock)
{
    RefineQtyLine q;
    q.produced  = (long)r.resultPer * count;
    q.consumed  = (long)r.sourcePer * count;
    q.remaining = (long)owned - q.consumed;
    if (q.remaining < 0) q.remaining = 0;
    q.total = (stock >= 0) ? (long)stock + q.produced : -1;
    if (q.total > RF_STOCK_CAP) q.total = RF_STOCK_CAP;
    return q;
}

// --- did the refine actually happen? --------------------------------------
// State 0x2A performs the refine in a single frame and then jumps to 0x25 or
// 0x17 (0x004D7DEA), so a poller cannot count on ever seeing it. Cancel and
// confirm both clear +0x51 and both land the player back on a list, with the
// cursor where it was -- so nothing is announced either way and the screen goes
// silent on the one action that changes the save.
//
// What DOES separate them is observable after the fact: a confirm consumes
// exactly sourcePer * count of the source, a cancel consumes nothing. Comparing
// the source's own count across the popup closing is a measurement rather than
// an inference, and it works the same for an item source and a magic one.
enum RefineOutcome { RF_OUT_UNKNOWN = 0, RF_OUT_DONE, RF_OUT_CANCELLED };

static RefineOutcome RefineOutcomeOf(int ownedBefore, int ownedAfter, long consumed)
{
    if (ownedBefore < 0 || ownedAfter < 0 || consumed <= 0) return RF_OUT_UNKNOWN;
    if (ownedAfter == ownedBefore)                          return RF_OUT_CANCELLED;
    if ((long)ownedBefore - (long)ownedAfter == consumed)   return RF_OUT_DONE;
    return RF_OUT_UNKNOWN;   // something else moved -- say nothing rather than guess
}

#endif // MENU_REFINE_MODEL_INCLUDED
