// menu_shop_model.inl -- v0.34.0 (#92)
//
// The two shop modules' shape, read out of FF8_EN.exe. No memory access lives
// here: every function takes bytes the caller already read, so the probe can
// drive the whole decision surface without the game.
//
// ===========================================================================
// THERE ARE TWO MODULES, NOT ONE
// ---------------------------------------------------------------------------
// 0x004EDA40 is the entry point. It asks 0x004B2D70 which shop this is, looks
// the answer up in the table at 0x00B88918, and branches:
//
//     004EDA53  call 0x4B2D70                  ; shop number
//     004EDA58  cmp  byte [eax + 0xB88918], 0x15
//     004EDA5F  jne  0x4EDA73                  ; -> the ITEM shop
//     004EDA66  call 0x4EA4D0                  ; -> the JUNK shop
//
// That table maps shop numbers 0..20 to themselves, 21..31 to 0, and **32..39
// to 21** -- so type 0x15 is the Junk Shop and there are eight of them. The
// item shop is created at 0x004EBBA0 (update 0x004EBE40, draw 0x004ED1B0); the
// Junk Shop at 0x004EA4D0 (update 0x004EA890, draw 0x004EAFF0). They share no
// state machine, no field layout and no cursor.
//
// ===========================================================================
// THE ITEM SHOP
// ---------------------------------------------------------------------------
// The creator loads three files (0x004EBBB5..0x004EBBF5):
//     shop.bin  -> [0x01D2BB08]   20 shops x 32 bytes = 16 x {u8 item, u8 ?}
//     price.bin -> [0x01D2BB64]   200 x {u16 base, u8 sellFactor, u8 ?}
//     mitem.bin -> [0x01D2BB60]
//
// and builds three runtime tables the reader can use directly:
//
//   0x01D8D038  the SHOP STOCK actually on offer: 16 x {u8 itemId, u8 avail}.
//               Item ids come from shop.bin; the availability byte comes from
//               the SAVEMAP at 0x01CFE5A7 + shopType*20 + row (0x004EBCC9), and
//               rows with no item or no availability are COMPACTED OUT
//               (0x004EBCED..0x004EBD53). So every row present is buyable.
//   0x01D8D058  owned quantity BY ITEM ID, rebuilt from the inventory on entry
//               and again on every menu confirm (0x004EBC45, 0x004EC359).
//   0x01D8CD18  buy price, 200 x u32.   base * 10, or base * 15/2 with the
//               "Haggle" flag from 0x004C2B30 bit 0 (0x004EBD82..0x004EBDA2).
//   0x01D8D120  sell price, 200 x u32.  (base * sellFactor) / 2, or * 3/4 of
//               that with "Sell-High", bit 1 (0x004EBDA9..0x004EBDE9).
//               Both are clamped to a minimum of 1.
//
// ===========================================================================
// THE JUNK SHOP
// ---------------------------------------------------------------------------
// Loads mwepon.bin -> [0x01D2BB50] and mwepon.msg -> [0x01D2BB28]. A mwepon
// record is 12 bytes and 0x004EA7A6..0x004EA864 reads it as:
//     +0x00 u16   offset into mwepon.msg -- the weapon's own text
//     +0x03 u8    price / 10        (0x004EA7FD: cost = byte * 10)
//     +0x04..0x0B four {u8 itemId, u8 count} -- the materials
// and the weapon's owner is a separate table: 0x01CF7404 + recIdx*12.
//
// 0x004EA770(charId) builds the visible list at 0x01D8CC08, one byte per row:
//     low 6 bits  the mwepon record index
//     0x40        this weapon has been SEEN before ([0x01CFE750] bit set)
//     0x80        it can be built RIGHT NOW -- gil >= price and all four
//                 materials in hand (0x004EA808..0x004EA839)
// Rows that are neither seen nor buildable are not listed at all.
// ===========================================================================

#ifndef MENU_SHOP_MODEL_INCLUDED
#define MENU_SHOP_MODEL_INCLUDED

// --- the item shop's state machine (jump table 0x004ED080, 18 entries) ------
static const int SHOP_ST_TOPMENU  = 0x03;  // 0x004EBE82: Buy / Sell / Quit
static const int SHOP_ST_LIST     = 0x06;  // 0x004EBF63: the item list
static const int SHOP_ST_QTY_ARM  = 0x0B;  // 0x004EC468: computes the maximum
static const int SHOP_ST_QTY      = 0x0C;  // 0x004EC5E1: the quantity screen
static const int SHOP_ST_MSG      = 0x0F;  // 0x004EC5A7: a timed message
static const int SHOP_ST_MAX      = 0x11;

static const int SHOP_MODE_BUY  = 0;
static const int SHOP_MODE_SELL = 1;

// The top row's three labels are group-3 strings 52, 53 and 54, terminated by
// 0xFFFF at index 3 in the table at 0x00B88910 -- so there are exactly three,
// in that order, and the mod reads the game's own words rather than guessing.
static const int SHOP_TOPMENU_STR0  = 52;
static const int SHOP_TOPMENU_COUNT = 3;

enum ShopPhase {
    SHOP_PHASE_OTHER = 0,
    SHOP_PHASE_TOPMENU,
    SHOP_PHASE_LIST,
    SHOP_PHASE_QUANTITY
};

static ShopPhase ShopPhaseOf(int state)
{
    if (state == SHOP_ST_TOPMENU) return SHOP_PHASE_TOPMENU;
    if (state == SHOP_ST_LIST)    return SHOP_PHASE_LIST;
    if (state == SHOP_ST_QTY || state == SHOP_ST_QTY_ARM) return SHOP_PHASE_QUANTITY;
    return SHOP_PHASE_OTHER;
}

// The list cursor is per-mode: +0x3C is buy's, +0x3E is sell's, and 0x004EBF9D
// indexes them as `word [esi + mode*2 + 0x3C]`. Reading the wrong one names a
// row from the other list -- the same shape as every cursor defect in
// SUBMENU_AUDIT.md.
static int ShopCursorOffsetFor(int mode) { return 0x3C + (mode ? 2 : 0); }

// 8 rows to a page (0x004EBFB8 pushes 8 to the cursor mover), and the engine
// keeps the page in +0x40 as cursor/8 (0x004EC31A).
static const int SHOP_ROWS_PER_PAGE = 8;

// --- the junk shop's state machine (jump table 0x004EAFA8, 17 entries) ------
static const int JUNK_ST_CHARS   = 0x03;   // 0x004EA92C: whose weapon
static const int JUNK_ST_WEAPONS = 0x07;   // 0x004EAAFC: which weapon
static const int JUNK_ST_MSG     = 0x09;   // 0x004EAC66: a timed message
static const int JUNK_ST_BUILD   = 0x0A;   // 0x004EACDD: builds the prompt text
static const int JUNK_ST_CONFIRM = 0x0B;   // 0x004EADA6: the shared yes/no window
static const int JUNK_ST_MAX     = 0x10;

static const int JUNK_ROW_INDEX_MASK = 0x3F;
static const int JUNK_ROW_SEEN       = 0x40;
static const int JUNK_ROW_BUILDABLE  = 0x80;

enum JunkPhase { JUNK_PHASE_OTHER = 0, JUNK_PHASE_CHARS, JUNK_PHASE_WEAPONS };

static JunkPhase JunkPhaseOf(int state)
{
    if (state == JUNK_ST_CHARS)   return JUNK_PHASE_CHARS;
    if (state == JUNK_ST_WEAPONS) return JUNK_PHASE_WEAPONS;
    return JUNK_PHASE_OTHER;
}

// mwepon +0x03 is the price in TENS (0x004EA7FD/0x004EAC10 both multiply by 10)
// -- and the amount actually DEDUCTED at 0x004EAE2B scales it by the module's
// +0x30, which is 1000 normally and 750 with Haggle (0x004EA71D). v0.34.1 spoke
// the unscaled figure, which is 25% high for a player who has the ability.
static long JunkWeaponPrice(int priceByte, int priceMul)
{
    const long base = (long)priceByte * 10;
    if (priceMul <= 0) return base;
    return base * priceMul / 1000;
}

// Both columns of the material panel: what the recipe wants and what is in the
// bag. Aaron: *"I do not hear how many of the item I have in inventory along
// with how many I need to upgrade."*
static bool JunkMaterialShort(int need, int have) { return have < need; }

// --- what a quantity screen costs ------------------------------------------
// The engine caps a buy at gil/price and at 100 held (0x004EC53A..0x004EC556),
// and a sell at what you own (0x004EC56D). The reader does not recompute the
// cap -- it reads +0x49 -- but it does need the running total.
static long ShopQuantityTotal(long unitPrice, int count)
{
    if (unitPrice < 0 || count < 0) return -1;
    return unitPrice * count;
}

// Gil after the transaction, for the line the screen does not spell out.
static long ShopGilAfter(long gil, long total, int mode)
{
    if (gil < 0 || total < 0) return -1;
    long g = (mode == SHOP_MODE_SELL) ? gil + total : gil - total;
    if (g < 0) g = 0;
    return g;
}

#endif // MENU_SHOP_MODEL_INCLUDED
