// laguna_switch_model.inl -- the PURE model of the Laguna junction-party screen.
//
// Included from menu_tts.cpp before menu_tts_laguna_switch.inl, and compiled
// standalone by tests/laguna_switch_compile.cpp. Nothing here touches game
// memory, Windows or the screen reader.
//
// ============================================================================
// WHAT THIS SCREEN IS, AND HOW IT WAS FOUND (v0.43.0, #104)
// ============================================================================
//
// Aaron: *"A specialized Switch screen appears where you select the party
// members whose junctions you want swapped to Laguna, Kiros, and Ward. Laguna
// is preset to Squall and can't be changed. This Switch screen currently reads
// nothing out."*
//
// It is **not** the Switch Member UI that `menu_tts_switch.inl` reads. It is its
// own menu module, and the chain from the field script to that module is short
// enough to walk end to end:
//
//   `bghoke_2` (B-Garden Infirmary, where the party sleeps), dwords 262-273:
//       0x08C(8) 0x08C(9) 0x08D(10)   ; flag Laguna, Kiros, Ward as present
//       PSHN_L 3 / OP_0x109           ; <- opens this screen
//       OP_0x0AA
//       PSHN_L 946 / 0 / MAPJUMPO     ; then jump to tvglen1, the dream
//
//   opcode 0x109's handler (`0x0051EFB0`) does the standard field->menu
//   handshake: `[0x01CE4760] = 5`, **`[0x01CE4762] = 0x16`** (the menu id) and
//   `[0x01CE490B] = gameObj[0x68] & 7`.
//
//   the mode 6/10 handler (`0x00470DA6`) copies those into `[0x01D2BB98]` and
//   `[0x00B87798]`, and `0x004A2378` hands them to the menu controller.
//
//   `0x004B3161` switches on the menu id: `lea ecx,[eax-1] / cmp ecx,0x1C /
//   mov dl,[ecx+0x4B32E8] / jmp [edx*4+0x4B32B0]`. Menu id 0x16 -> index 5 ->
//   `0x004B3190`, which stores the param at **`[0x01D75599]`** and pushes
//   **screen id 0x0D**.
//
//   `0x004BDB30` looks screen ids up in the pair table at `0x00B87ED8`
//   ({creator, layer}, 8 bytes each). Pair 13 = **creator `0x004E8A30`**.
//   (Pair 10 is `0x004CB850`, the ordinary Switch -- a different module, which
//   is why the existing reader sees nothing here.)
//
// ============================================================================
// THE MODULE
// ============================================================================
//
// `0x004E8A30` allocates through `0x004BE540(update, draw)` = the ten-slot menu
// module pool at `0x01D76BC8`, stride `0x78`, MRU head `0x01D76B48`, `+0x12`
// in-use, **`+0x08` the update function** -- the same pool `menu_tts_magic.inl`
// walks. This module's update function is `0x004E8B50`, and that is how the
// reader finds it: no game-mode gate, no window scan, the module is there
// exactly when the screen is.
//
// The creator, read straight out of `0x004E8A66..0x004E8B1D`:
//
//   * loops chars 0..7 at `savemap+0x48C + i*0x98 + 0x94`, bit 3 = "can be
//     picked", into a mask at `+0x24`;
//   * `+0x3B..+0x42` <- `0xFF`, then the ids of the set bits, packed;
//   * `+0x38 = 0` (**Squall**), `+0x39 = 0xFF`, `+0x3A = 0xFF`.
//
// That last line is Aaron's "Laguna is preset to Squall" -- it is a literal
// `mov byte ptr [esi+0x38], 0` in the creator.
//
// The state machine at `0x004E8B50` reads exactly two cursors:
//
//   `+0x32` focus  0 = the left character grid, 1 = the right slot column
//   `+0x34` grid cursor, **row*4 + column** (columns 0-3, rows 0-1)
//        -> `+0x3B[cursor]`, and `0xFF` there buzzes instead of selecting
//   `+0x35` slot cursor, a BIT INDEX walked through the slot mask by
//        `0x004ABCA0` / `0x004ABCD0` (next / previous set bit)
//        -> `+0x38[cursor]`
//
// **The slot mask is `[0x01D75599]`** -- the byte the field script's parameter
// became. `0x004B2DB0` is nothing but `mov al,[0x01D75599] / ret`, and both the
// state machine and the draw loop at `0x004E9FA1` use it to decide which of the
// three slots exist: `for (b = 0; b < 3; b++) if (mask & (1<<b)) draw slot b`.
// That is also how a dream with no Ward is expressed, so the reader takes the
// slot list from the mask rather than assuming three.
//
// **AND THE SECOND CURSOR IS `+0x31` (v0.45.0).** v0.44.0 fixed the wrong half
// of this. `+0x43` is a MODE, and it has three values:
//
//     1  browsing        -- `+0x32` focus, `+0x34` grid, `+0x35` slot
//     2  CHOOSING A SLOT -- the cursor is **`+0x31`**, a bit index in the slot
//                           mask, and the browse trio is frozen on the pick
//     3  the `+0x2e != 0` variant -- `+0x33`/`+0x36`/`+0x37` (see below)
//
// State 3's `0x40` branch (`0x004E8E96`) checks `+0x2e`: **zero -- which is what
// the creator writes and what this screen always has -- goes to state 6**, and
// state 6 (`0x004E9342`) sets `+0x43 = 2` and hands over to state 7, whose whole
// body walks `+0x31` up and down the slot mask with `0x004ABCA0` / `0x004ABCD0`
// and writes it back at `0x004E9378` / `0x004E9399`.
//
// The swap itself (`0x004E94E8`) is the proof of what the two ends are:
//
//     if ([esi+0x32] == 0)                       ; picked off the grid
//         slot  = [esi+0x31]                     ; <- the DESTINATION
//         who   = grid[[esi+0x34]]               ; <- the PICK, frozen
//         swap slots[slot] <-> grid[[esi+0x34]]
//     else                                       ; picked off the column
//         swap slots[[esi+0x31]] <-> slots[ordinal([esi+0x35])]
//
// so `+0x31` is where it is going and the browse trio is what is in hand.
//
// `+0x2e` is 0 on this screen (the creator writes it), so **mode 3 never
// happens here** -- but it is left in the model because the same module is used
// with `+0x2e != 0` elsewhere, and a reader that assumed mode 2 was the only
// one would go silent there in exactly the way v0.43.0 went silent here.
//
// (v0.44.0's note, kept because mode 3 is real:) `+0x43 == 3` means one end
// of a swap has been picked -- and from that moment the pair the player is
// moving is `+0x33/+0x36/+0x37`, while `+0x32/+0x34/+0x35` FREEZE on the pick.
// State 4 (`0x004E8F88`) is four instructions of proof:
//
//     cl = [esi+0x32] ; dl = [esi+0x34] ; al = [esi+0x35]
//     [esi+0x43] = 3
//     [esi+0x33] = cl ; [esi+0x36] = dl ; [esi+0x37] = al
//     [esi+0x10] = 5
//
// and state 5 (`0x004E8FA4`) then reads `[esi+0x33]` for the focus, moves
// `[esi+0x36]` with the same row*4+column arithmetic and `[esi+0x37]` through
// the slot mask, writing them back at `0x004E9066` and `0x004E9099`. The draw
// agrees: `0x004EA284` tests `+0x43 == 3` and takes the second cursor from
// those three bytes.
//
// v0.43.0 had the roles backwards -- it read the pick as live and the live
// cursor as the pick -- so **the entire second half of every placement was
// silent**, which is exactly what the first BAT reported.
//
// The three slots are Laguna, Kiros and Ward -- character ids 8, 9, 10, the
// three the field flagged with `0x08C`/`0x08D` immediately before opening this
// screen.
// ============================================================================

// ---- where it lives --------------------------------------------------------
static const uint32_t  LSW_UPDATE_FN     = 0x004E8B50;   // module +0x08 == this
static const uintptr_t LSW_POOL_BASE     = 0x01D76BC8;
static const uintptr_t LSW_POOL_END      = 0x01D77078;   // base + 10 * 0x78
static const int       LSW_POOL_STRIDE   = 0x78;
static const uintptr_t LSW_LIST_HEAD     = 0x01D76B48;
static const int       LSW_MOD_NEXT      = 0x00;
static const int       LSW_MOD_UPDATE_FN = 0x08;
static const uintptr_t LSW_SLOT_MASK_ADDR = 0x01D75599;  // 0x004B2DB0 returns it

// ---- module field offsets --------------------------------------------------
static const int LSWO_STATE       = 0x10;   // word
static const int LSWO_AVAIL_MASK  = 0x24;   // word
static const int LSWO_FOCUS       = 0x32;   // 0 = grid, 1 = slots
static const int LSWO_PEND_FOCUS  = 0x33;
static const int LSWO_GRID_CUR    = 0x34;   // row*4 + col
static const int LSWO_SLOT_CUR    = 0x35;   // bit index within the slot mask
static const int LSWO_PEND_GRID   = 0x36;
static const int LSWO_PEND_SLOT   = 0x37;
static const int LSWO_SLOTS       = 0x38;   // [3], 0xFF = empty
static const int LSWO_GRID        = 0x3B;   // [8], 0xFF = empty cell
static const int LSWO_DEST_SLOT   = 0x31;   // the SECOND cursor, mode 2
static const int LSWO_PAGE        = 0x2E;   // 0 on this screen
static const int LSWO_PENDING     = 0x43;   // 1 browse, 2 choosing a slot, 3 …

static const int LSW_MODE_BROWSE = 1;
static const int LSW_MODE_DEST   = 2;   // the second cursor is +0x31
static const int LSW_MODE_CARRY  = 3;   // the second cursor is +0x33/36/37

static const int LSW_SLOT_COUNT = 3;
static const int LSW_GRID_CELLS = 8;
static const int LSW_GRID_COLS  = 4;
static const unsigned char LSW_EMPTY = 0xFF;

// ---- game data -------------------------------------------------------------
static const uintptr_t LSW_SAVEMAP      = 0x01CFDC5C;
static const int       LSW_CHAR_ARRAY   = 0x048C;
static const int       LSW_CHAR_STRIDE  = 0x98;
static const int       LSW_CHAR_EXISTS  = 0x94;   // bit 3 = pickable here
static const int       LSW_PICKABLE_BIT = 0x08;

// The three dream characters, in slot order. They are ids 8, 9 and 10 because
// `bghoke_2` flags exactly those three immediately before opening the screen.
static const int LSW_SLOT_CHAR_ID[LSW_SLOT_COUNT] = { 8, 9, 10 };

static const char* const LSW_CHAR_NAMES[11] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea",
    "Laguna", "Kiros", "Ward"
};

static const char* LswCharName(int id)
{
    if (id < 0 || id > 10) return 0;
    return LSW_CHAR_NAMES[id];
}

static const char* LswSlotLabel(int slot)
{
    if (slot < 0 || slot >= LSW_SLOT_COUNT) return "Unknown";
    return LSW_CHAR_NAMES[LSW_SLOT_CHAR_ID[slot]];
}

// ---- the view the reader fills --------------------------------------------
struct LagunaSwitchView
{
    int state;
    int availMask;
    int slotMask;
    int focus;
    int gridCursor;
    int slotCursor;
    int pending;         // +0x43 -- the MODE
    int destSlot;        // +0x31
    int page;            // +0x2E
    int pendFocus, pendGrid, pendSlot;
    unsigned char slots[LSW_SLOT_COUNT];
    unsigned char grid[LSW_GRID_CELLS];
};

// WHICH CURSOR IS LIVE. While a pick is held it is the second trio; otherwise
// the first. Everything the reader says about "where you are" goes through
// these, and everything it says about "what you are holding" goes through the
// Held* pair below.
static bool LswPickHeld(const LagunaSwitchView& v)
{ return v.pending == LSW_MODE_DEST || v.pending == LSW_MODE_CARRY; }

// Mode 2 is a slot chooser and nothing else, so its focus is always the column.
static int LswLiveFocus(const LagunaSwitchView& v)
{
    if (v.pending == LSW_MODE_DEST)  return 1;
    if (v.pending == LSW_MODE_CARRY) return v.pendFocus;
    return v.focus;
}
static int LswLiveGrid(const LagunaSwitchView& v)
{ return (v.pending == LSW_MODE_CARRY) ? v.pendGrid : v.gridCursor; }
static int LswLiveSlot(const LagunaSwitchView& v)
{
    if (v.pending == LSW_MODE_DEST)  return v.destSlot;
    if (v.pending == LSW_MODE_CARRY) return v.pendSlot;
    return v.slotCursor;
}

// The character in hand: whatever the FROZEN browse cursor is sitting on.
static int LswHeldChar(const LagunaSwitchView& v)
{
    if (!LswPickHeld(v)) return -1;
    if (v.focus == 0) {
        if (v.gridCursor < 0 || v.gridCursor >= LSW_GRID_CELLS) return -1;
        return (int)v.grid[v.gridCursor];
    }
    if (v.slotCursor < 0 || v.slotCursor >= LSW_SLOT_COUNT) return -1;
    return (int)v.slots[v.slotCursor];
}

static bool LswSlotSelectable(int slotMask, int slot)
{
    if (slot < 0 || slot >= LSW_SLOT_COUNT) return false;
    return (slotMask & (1 << slot)) != 0;
}

static int LswSlotCount(int slotMask)
{
    int n = 0;
    for (int i = 0; i < LSW_SLOT_COUNT; i++) if (LswSlotSelectable(slotMask, i)) n++;
    return n;
}

// Position of a slot among the slots that exist -- "2 of 3", not "slot 1".
static int LswSlotOrdinal(int slotMask, int slot)
{
    int n = 0;
    for (int i = 0; i < LSW_SLOT_COUNT; i++) {
        if (!LswSlotSelectable(slotMask, i)) continue;
        n++;
        if (i == slot) return n;
    }
    return 0;
}

// Which slot a character is already lending to, or -1.
static int LswSlotOfChar(const LagunaSwitchView& v, int charId)
{
    if (charId < 0 || charId == LSW_EMPTY) return -1;
    for (int i = 0; i < LSW_SLOT_COUNT; i++)
        if ((int)v.slots[i] == charId) return i;
    return -1;
}

static void LswAppend(char* out, size_t n, const char* s)
{
    size_t len = strlen(out);
    if (len + 1 >= n) return;
    snprintf(out + len, n - len, "%s", s);
}

// One slot, as it should be read: "Kiros, Zell." / "Ward, nobody yet."
static void LswSlotLine(const LagunaSwitchView& v, int slot, char* out, size_t n)
{
    out[0] = '\0';
    if (slot < 0 || slot >= LSW_SLOT_COUNT) return;
    const int id = (int)v.slots[slot];
    const char* who = (id == LSW_EMPTY) ? 0 : LswCharName(id);
    snprintf(out, n, "%s, %s.", LswSlotLabel(slot), who ? who : "nobody yet");
}

// All the slots that exist, in order.
static void LswAllSlotsLine(const LagunaSwitchView& v, char* out, size_t n)
{
    out[0] = '\0';
    for (int i = 0; i < LSW_SLOT_COUNT; i++) {
        if (!LswSlotSelectable(v.slotMask, i)) continue;
        char one[64];
        LswSlotLine(v, i, one, sizeof(one));
        if (out[0]) LswAppend(out, n, " ");
        LswAppend(out, n, one);
    }
}

// What the cursor is sitting on.
//
// On the grid this is a party member; saying who they are already lending to is
// the point, because the screen's whole job is to divide eight people between
// two or three slots and the only way to see a conflict is to remember it.
static void LswCursorLine(const LagunaSwitchView& v, char* out, size_t n)
{
    out[0] = '\0';
    if (LswLiveFocus(v) != 0) {
        const int slot = LswLiveSlot(v);
        char one[64];
        LswSlotLine(v, slot, one, sizeof(one));
        const int total = LswSlotCount(v.slotMask);
        const int ord   = LswSlotOrdinal(v.slotMask, slot);
        if (ord > 0 && total > 1)
            snprintf(out, n, "%s %d of %d.", one, ord, total);
        else
            snprintf(out, n, "%s", one);
        return;
    }

    const int gc = LswLiveGrid(v);
    if (gc < 0 || gc >= LSW_GRID_CELLS) return;
    const int id = (int)v.grid[gc];
    if (id == LSW_EMPTY) { snprintf(out, n, "Empty."); return; }
    const char* who = LswCharName(id);
    if (!who) { snprintf(out, n, "Unknown character."); return; }

    const int slot = LswSlotOfChar(v, id);
    if (slot >= 0) snprintf(out, n, "%s, lending to %s.", who, LswSlotLabel(slot));
    else           snprintf(out, n, "%s.", who);
}

// Where a held pick would land: the slot under the second cursor, and -- because
// this is a SWAP, not a placement -- who would come back the other way.
static void LswDestinationLine(const LagunaSwitchView& v, char* out, size_t n)
{
    out[0] = '\0';
    if (!LswPickHeld(v)) return;
    const int slot = LswLiveSlot(v);
    if (slot < 0 || slot >= LSW_SLOT_COUNT) return;
    char one[64];
    LswSlotLine(v, slot, one, sizeof(one));
    const int total = LswSlotCount(v.slotMask);
    const int ord   = LswSlotOrdinal(v.slotMask, slot);
    const int held  = LswHeldChar(v);
    const char* who = (held >= 0 && held != LSW_EMPTY) ? LswCharName(held) : 0;
    if (who && ord > 0 && total > 1)
        snprintf(out, n, "%s Put %s here. %d of %d.", one, who, ord, total);
    else if (who)
        snprintf(out, n, "%s Put %s here.", one, who);
    else
        snprintf(out, n, "%s", one);
}

// The line spoken when a first pick is held and the player is choosing where to
// put it. The game shows this as a lifted card; there is nothing to hear.
static void LswPendingLine(const LagunaSwitchView& v, char* out, size_t n)
{
    out[0] = '\0';
    const int id = LswHeldChar(v);
    const char* who = (id >= 0 && id != LSW_EMPTY) ? LswCharName(id) : 0;
    if (!who) return;
    snprintf(out, n, "%s picked up. Choose a slot.", who);
}
