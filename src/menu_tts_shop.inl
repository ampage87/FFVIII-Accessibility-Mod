// menu_tts_shop.inl -- v0.34.0 (#92)  Item shops and Junk Shops.
//
// Aaron: *"let's go ahead and implement support for junk shops and item shops
// in the mod."* Both screens were entirely silent -- the mod has never spoken a
// word inside a shop, so buying, selling and every weapon remodel in the game
// were done blind.
//
// Provenance for every address here is in src/menu_shop_model.inl. The rule
// this file follows throughout: **read what the engine already resolved rather
// than re-deriving it.** The item shop puts the highlighted item's description
// in +0x20 and the current message in +0x30; the Junk Shop puts the weapon's
// text in +0x20 and its message in +0x2C. Those are the game's own strings, so
// the mod speaks the game's own words instead of a translation of them.

// ---------------------------------------------------------------------------
// Module identification -- the same pool walk the Save and refine screens use.
// ---------------------------------------------------------------------------
static const uintptr_t SHOP_INVENTORY = 0x01CFE79C;   // savemap items, {id, qty}
static const uintptr_t SHOP_POOL_BASE = 0x01D76BC8;

// ---------------------------------------------------------------------------
// WHICH SHOP IS OPEN -- without needing the module at all  (v0.34.5)
//
// Three builds have now failed to read the item shop and each diagnosis has been
// a guess. This is not a guess. 0x004B2D70, which BOTH creators call to decide
// what they are, is two instructions:
//
//     004B2D70  mov eax, [0x01D75450]
//     004B2D75  and eax, 0xFF
//
// and the type is byte [0x00B88918 + shopNumber], a 52-entry table (0..51; the
// next byte is the "shop.bin" string, which is how its length is known). 0..20
// map to themselves, 21..31 to 0, and 32..42 to 21 -- the Junk Shops.
//
// So the mod can say "an item shop is open" with no module, no pool and no list
// walk. That makes the difference between "the reader is broken" and "no shop
// was open" answerable from the log instead of from the screenshots.
// ---------------------------------------------------------------------------
static const uintptr_t SHOP_NUMBER_ADDR = 0x01D75450;
static const uintptr_t SHOP_TYPE_TABLE  = 0x00B88918;
static const int       SHOP_TYPE_COUNT  = 52;
static const int       SHOP_TYPE_JUNK   = 0x15;

static int ShopNumber()
{
    __try { return (int)(*(volatile uint32_t*)SHOP_NUMBER_ADDR & 0xFF); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static int ShopTypeNow()
{
    const int n = ShopNumber();
    if (n < 0 || n >= SHOP_TYPE_COUNT) return -1;
    __try { return (int)((const uint8_t*)SHOP_TYPE_TABLE)[n]; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static const uintptr_t SHOP_POOL_END  = 0x01D77078;
static const uintptr_t SHOP_LIST_HEAD = 0x01D76B48;
static const uint32_t  SHOP_ITEM_FN   = 0x004EBE40;   // creator 0x004EBBA0
static const uint32_t  SHOP_JUNK_FN   = 0x004EA890;   // creator 0x004EA4D0
// v0.34.6: **the DRAW fn is the reliable identity for the item shop.** The
// v0.34.5 pool dump is unambiguous:
//
//   slot 1 @01D76C40 inUse=1 state=0x03 upd=605D8130 draw=004ED1B0 +2C=01CFE79C
//
// +0x08 has been overwritten with a heap pointer -- the same thing SUBMENU_AUDIT
// §9 records for the Item MENU module (0x605D8200, and note how close the two
// values are). +0x0C survives. Three builds looked for a value that is not
// there while the one next to it was correct the whole time.
static const uint32_t  SHOP_ITEM_DRAW = 0x004ED1B0;
static const uint32_t  SHOP_JUNK_DRAW = 0x004EAFF0;

// v0.34.2: **scan the pool, do not walk the list.**
//
// v0.34.1 walked the MRU list from 0x01D76B48 and found the Junk Shop every
// time -- and never once found the item shop, through two BATs, though the
// screenshot shows it plainly ("Balamb Shop", Buy / Sell / Exit, a full stock
// list). The walk stops at the first entry outside the pool, which is correct
// for the tail sentinel and wrong for anything the engine relinks: 0x004BE5B0
// is a SECOND allocator that threads modules onto a different list head
// (0x01D76ACC), and a module reachable from that one is invisible from this one.
//
// The pool itself is ten fixed slots. Reading all ten cannot miss a module that
// exists, does not depend on which list it is currently on, and costs ten
// compares. The list walk bought nothing that mattered.
static uint8_t* ShopFindModule(uint32_t updateFn, uint32_t drawFn)
{
    __try {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(SHOP_POOL_BASE + i * 0x78);
            if (!m[0x12]) continue;                       // slot not in use
            if (*(volatile uint32_t*)(m + 0x08) == updateFn) return m;
            if (drawFn && *(volatile uint32_t*)(m + 0x0C) == drawFn) return m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

// v0.34.7: OFF by default. It did its job -- the dump that produced
//     slot 1 ... upd=605D8130 draw=004ED1B0 +2C=01CFE79C ... +47=0
// is what identified both the clobbered +0x08 and the unwritten +0x47. Gate it,
// do not delete it: the same convention as ABIL_DIAG and REFINE_FLOW_DIAG.
//
// It also could not stay on. **Game mode 10 is not shop-only** -- the v0.34.6
// log shows the party-SWITCH screen (upd=004CBA50) running in it -- and the
// shop-number global at 0x01D75450 goes stale between shops, so it reported
// "type 0 = ITEM" on a screen that was not a shop and dumped eighty lines per
// visit. The module's own +0x45 is the authoritative type; the global is only
// trustworthy while a shop is actually being opened.
//
// (History: v0.34.3's dump fired ONCE, on the first poll after the mode change,
// before the modules exist -- the junk-shop visit proved it, an empty pool at
// 18:43:07 and the module found at 18:43:08. v0.34.4 made it sample the steady
// state every two seconds, up to eight times a visit, which is what caught it.)
#define SHOP_POOL_DIAG 0
#if SHOP_POOL_DIAG
static int   s_shopPoolDumps = 0;
static DWORD s_shopPoolLast  = 0;
static const int   SHOP_POOL_DUMP_MAX = 8;
static const DWORD SHOP_POOL_DUMP_MS  = 2000;

static void ShopDumpPool()
{
    const DWORD now = GetTickCount();
    if (s_shopPoolDumps >= SHOP_POOL_DUMP_MAX) return;
    if (s_shopPoolDumps > 0 && now - s_shopPoolLast < SHOP_POOL_DUMP_MS) return;
    s_shopPoolDumps++;
    s_shopPoolLast = now;

    uint32_t head1 = 0, head2 = 0;
    __try {
        head1 = *(volatile uint32_t*)SHOP_LIST_HEAD;      // 0x01D76B48
        head2 = *(volatile uint32_t*)0x01D76ACC;          // the other allocator's
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    // The line that makes the rest of the dump mean something: WHICH SHOP the
    // engine thinks is open, read from its own two-instruction accessor.
    const int num = ShopNumber(), typ = ShopTypeNow();
    Log::Menu("[SHOP-POOL] #%d shop=%d type=%d (%s) heads: %08X / %08X",
              s_shopPoolDumps, num, typ,
              (typ == SHOP_TYPE_JUNK) ? "junk" : (typ >= 0 ? "ITEM" : "unknown"),
              (unsigned)head1, (unsigned)head2);
    for (int i = 0; i < 10; i++) {
        uint32_t upd = 0, drw = 0; int use = -1, st = -1;
        __try {
            uint8_t* m = (uint8_t*)(SHOP_POOL_BASE + i * 0x78);
            use = (int)m[0x12];
            st  = (int)*(volatile uint16_t*)(m + 0x10);
            upd = *(volatile uint32_t*)(m + 0x08);
            drw = *(volatile uint32_t*)(m + 0x0C);
        } __except(EXCEPTION_EXECUTE_HANDLER) { }
        uint32_t f2C = 0; int f45 = -1, f46 = -1, f47 = -1;
        __try {
            uint8_t* m = (uint8_t*)(SHOP_POOL_BASE + i * 0x78);
            f2C = *(volatile uint32_t*)(m + 0x2C);
            f45 = (int)m[0x45]; f46 = (int)m[0x46]; f47 = (int)m[0x47];
        } __except(EXCEPTION_EXECUTE_HANDLER) { }
        Log::Menu("[SHOP-POOL] slot %d @%08X inUse=%d state=0x%02X upd=%08X draw=%08X "
                  "+2C=%08X +45=%d +46=%d +47=%d",
                  i, (unsigned)(SHOP_POOL_BASE + i * 0x78), use, st,
                  (unsigned)upd, (unsigned)drw, (unsigned)f2C, f45, f46, f47);
    }
}
#endif // SHOP_POOL_DIAG

// v0.34.4: a second way to recognise the item shop, for the case where +0x08 is
// not what it should be -- which is not hypothetical. SUBMENU_AUDIT.md §9
// records the Item MENU module carrying `upd=0x605D8200` in that field, so
// identity-by-update-fn provably cannot work everywhere in this engine.
//
// Two things must agree, both set by the creator and by nothing else:
//   +0x2C == 0x01CFE79C   the inventory base   (0x004EDAE8, set by the creator)
//   +0x45 <  52           the shop type, from the same 52-entry table
// plus a state inside the machine's range.
//
// v0.34.5 ALSO required +0x47 to be 2 or 25 -- and that is written by the
// TOP-MENU CONFIRM (0x004EC2F3 / 0x004EC306), not by the creator. So the test
// could not pass until the player had already chosen Buy or Sell, which is
// exactly the symptom Aaron reported: *"when the item shop first appears, the
// action bar does not read out. Once I pick an option ... it works."* The log
// shows +0x47=0 at state 3 in every dump.
//
// **That is the third time in this file an identification has been keyed on a
// field the engine had not written yet** (+0x46 in v0.34.0, +0x47 here). The
// rule that survives: only test fields the CREATOR sets.
static uint8_t* ShopFindByFields()
{
    __try {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(SHOP_POOL_BASE + i * 0x78);
            if (!m[0x12]) continue;
            if (*(volatile uint32_t*)(m + 0x2C) != SHOP_INVENTORY) continue;
            if ((int)m[0x45] >= SHOP_TYPE_COUNT) continue;
            const int st = (int)*(volatile uint16_t*)(m + 0x10);
            if (st < 0 || st > SHOP_ST_MAX) continue;
            return m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The game's own text, resolved exactly as 0x004BD630 resolves it.
//
//   0x004C2590(id) == 0x004BD630(1, 3, id, 0):
//       base = [0x00B86D30] + 0x2E000
//       base += u16 at base + 3*2 + 2          (the group's offset)
//       base += u16 at base + (0 + id*2)*2 + 2 (the string's offset)
//
// A zero offset at either step means "no string", and the engine substitutes a
// blank; so does this. Same heap pointer the magazine reader already uses, at a
// different section offset.
// ---------------------------------------------------------------------------
static const uintptr_t SHOP_TEXT_HEAP = 0x00B86D30;
static const uintptr_t SHOP_TEXT_SECT = 0x2E000;
static const int       SHOP_TEXT_GROUP = 3;

static const uint8_t* ShopGroupString(int id)
{
    __try {
        uint8_t* base = *(uint8_t* volatile*)SHOP_TEXT_HEAP;
        if (!base) return nullptr;
        base += SHOP_TEXT_SECT;
        const uint16_t g = *(volatile uint16_t*)(base + SHOP_TEXT_GROUP * 2 + 2);
        if (!g) return nullptr;
        base += g;
        const uint16_t s = *(volatile uint16_t*)(base + (id * 2) * 2 + 2);
        if (!s) return nullptr;
        return base + s;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// SEH-isolated raw copy of an FF8 string so the decode (std::string) can happen
// outside the __try -- C2712 forbids the two sharing a function.
static bool ShopCopyString(const uint8_t* p, uint8_t* out, int n)
{
    __try {
        if (!p) return false;
        int i = 0;
        for (; i < n - 1; i++) { out[i] = p[i]; if (!p[i]) break; }
        out[n - 1] = 0;
        return out[0] != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// v0.34.8: **the decoder silently drops what it does not recognise**, and that
// turns a wrong table into a plausible sentence. From the BAT:
//
//     detail (/): " HP to all GF"      (Pet House)
//     detail (/): "GF"                 (Magic Scroll)
//     detail (/): " for dog lovers"    (Pet Pals Vol. 4)
//
// while Phoenix Down's "Removes KO" came out whole. Those are not truncations --
// they are the fragments the decoder happened to recognise, with every unknown
// byte thrown away. A player hearing "GF" has no way to know he was given two
// characters of a sentence.
//
// The shop's own strings (labels, messages) decode perfectly through the same
// path -- "What do you want to buy?", "Come back soon!" -- so the encoding is
// right for THOSE. Item descriptions come from somewhere else entirely:
// 0x0047EA90 resolves them out of 0x01CF3E48 + [0x01CF3EE4] + offset, and it
// uses TWO different tables split at item id 0x21 -- which is exactly where the
// good and bad cases split (Phoenix Down is 7; Pet House 34, Magic Scroll 55,
// Pet Pals Vol. 4 192).
//
// Rather than guess at the encoding a fourth time, this logs the raw bytes
// beside the decode whenever the result is suspiciously short for its input.
// One BAT settles it; guessing has cost four builds on this screen already.
// v0.34.9: count what the decoder THROWS AWAY, rather than guessing from the
// length. The v0.34.8 ratio test only caught the worst case ("GF" out of ten
// bytes) and passed the dangerous ones -- " 1000 HP to GF" and "Makes GF forget
// an" sound like whole sentences and are not. The raw bytes said why:
//
//     F0 20 0E 35 20 0E 37 20 0E 36     ->  "GF"
//
// v0.35.0 (#93) FINISHED IT: `0E xx` is a word substituted from **namedic.bin**,
// and those ten bytes are "GF learns Magic ability". `FF8TextDecode::Decode`
// expands them now, and the byte-counting loop that used to live here is gone
// -- it hard-coded a copy of "what the decoder throws away", which stopped
// being true the moment the decoder stopped throwing it away. The decoder
// reports its own losses through `Decode`'s `droppedOut`, so the two can no
// longer disagree.
static int  s_shopDecodeWarned = 0;
static void ShopWarnIfLossy(const uint8_t* raw, int rawLen,
                            const std::string& out, int dropped)
{
    if (rawLen <= 0 || dropped <= 0 || s_shopDecodeWarned >= 12) return;
    s_shopDecodeWarned++;
    char hex[3 * 64 + 1];
    int n = 0;
    for (int i = 0; i < rawLen && i < 64 && n < (int)sizeof(hex) - 3; i++)
        n += snprintf(hex + n, sizeof(hex) - n, "%02X ", raw[i]);
    Log::Menu("[SHOP-TEXT] lossy decode: %d raw bytes, %d dropped -> %d chars \"%s\" | %s",
              rawLen, dropped, (int)out.size(), out.c_str(), hex);
}

// Decode a shop string and report whether anything was lost doing it.
static std::string ShopDecodeAt(const uint8_t* p, int* droppedOut)
{
    if (droppedOut) *droppedOut = 0;
    uint8_t raw[256];
    if (!ShopCopyString(p, raw, sizeof(raw))) return std::string();
    int rawLen = 0;
    while (rawLen < (int)sizeof(raw) && raw[rawLen]) rawLen++;
    int dropped = 0;
    std::string s = FF8TextDecode::Decode(raw, sizeof(raw), &dropped);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) s.pop_back();
    ShopWarnIfLossy(raw, rawLen, s, dropped);
    if (droppedOut) *droppedOut = dropped;
    return s;
}

static std::string ShopDecodeAt(const uint8_t* p)
{
    return ShopDecodeAt(p, nullptr);
}

static std::string ShopMenuLabel(int index)
{
    if (index < 0 || index >= SHOP_TOPMENU_COUNT) return std::string();
    return ShopDecodeAt(ShopGroupString(SHOP_TOPMENU_STR0 + index));
}

// ===========================================================================
// THE ITEM SHOP
// ===========================================================================
static const uintptr_t SHOP_STOCK      = 0x01D8D038;   // 16 x {u8 id, u8 avail}
static const uintptr_t SHOP_OWNED_BY_ID = 0x01D8D058;  // qty indexed by item id
static const uintptr_t SHOP_BUY_PRICE  = 0x01D8CD18;   // 200 x u32
static const uintptr_t SHOP_SELL_PRICE = 0x01D8D120;   // 200 x u32
static const int       SHOP_STOCK_ROWS = 16;
static const int       SHOP_ITEM_SLOTS = 198;

static const int SHO_STATE   = 0x10;   // u16
static const int SHO_DESC    = 0x20;   // the highlighted item's description
static const int SHO_GIL     = 0x28;
static const int SHO_INV     = 0x2C;   // 0x01CFE79C
static const int SHO_MSG     = 0x30;   // the current message
static const int SHO_TOPCUR  = 0x42;   // Buy / Sell / Quit
static const int SHO_TYPE    = 0x45;
static const int SHO_MODE    = 0x46;   // 0 buy, 1 sell
static const int SHO_PAGES   = 0x47;
static const int SHO_QTY     = 0x48;
static const int SHO_QTYMAX  = 0x49;

struct ShopView
{
    bool ok;
    int  state, topCur, mode, pages, qty, qtyMax, shopType, cursor;
    long gil;
    uint32_t desc, msg, invBase;
};

static bool s_shopFoundByFields = false;

static bool ShopReadView(ShopView* v)
{
    uint8_t* b = ShopFindModule(SHOP_ITEM_FN, SHOP_ITEM_DRAW);
    if (!b) {
        b = ShopFindByFields();
        if (b && !s_shopFoundByFields) {
            s_shopFoundByFields = true;
            Log::Menu("[SHOP] item shop found by FIELDS, not by update fn "
                      "(+0x08=%08X) -- identity-by-update-fn does not hold here",
                      (unsigned)*(volatile uint32_t*)(b + 0x08));
        }
    }
    if (!b) { v->ok = false; return false; }
    __try {
        v->state    = (int)*(volatile uint16_t*)(b + SHO_STATE);
        v->topCur   = (int)(int8_t)b[SHO_TOPCUR];
        v->mode     = (int)b[SHO_MODE];
        v->pages    = (int)b[SHO_PAGES];
        v->qty      = (int)b[SHO_QTY];
        v->qtyMax   = (int)b[SHO_QTYMAX];
        v->shopType = (int)b[SHO_TYPE];
        v->gil      = (long)*(volatile uint32_t*)(b + SHO_GIL);
        v->desc     = *(volatile uint32_t*)(b + SHO_DESC);
        v->msg      = *(volatile uint32_t*)(b + SHO_MSG);
        v->invBase  = *(volatile uint32_t*)(b + SHO_INV);
        // v0.34.1: +0x46 is written by the TOP-MENU CONFIRM (0x004EC2E1) and by
        // nothing else -- the creator never touches it. So on entry, and for the
        // whole of states 0..3, it holds whatever the last shop left there.
        // v0.34.0 required it to be 0 or 1 before it would accept the view at
        // all, which declined the module for the entire opening of every shop
        // and, if the leftover was out of range, forever. It is CLAMPED now, and
        // only the list and quantity screens are read through it -- they are the
        // screens the engine has written it for.
        if (v->mode != SHOP_MODE_SELL) v->mode = SHOP_MODE_BUY;
        const int off = ShopCursorOffsetFor(v->mode);
        v->cursor   = (int)*(volatile int16_t*)(b + off);
        v->ok = (v->state >= 0 && v->state <= SHOP_ST_MAX);
        return v->ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { v->ok = false; return false; }
}

// The item id under the list cursor, resolved the way 0x004ED0D0 resolves it:
// the compacted stock for a buy, the raw inventory slot for a sell.
static bool ShopRowItem(const ShopView& v, int row, int* id, int* owned, long* price)
{
    __try {
        if (row < 0) return false;
        int iid = 0;
        if (v.mode == SHOP_MODE_BUY) {
            if (row >= SHOP_STOCK_ROWS) return false;
            iid = (int)((uint8_t*)SHOP_STOCK)[row * 2];
        } else {
            if (row >= SHOP_ITEM_SLOTS) return false;
            uint8_t* inv = (uint8_t*)(uintptr_t)v.invBase;
            if (!inv) return false;
            iid = (int)inv[row * 2];
        }
        if (iid <= 0 || iid > SHOP_ITEM_SLOTS) { *id = 0; *owned = 0; *price = -1; return true; }
        *id    = iid;
        *owned = (int)((uint8_t*)SHOP_OWNED_BY_ID)[iid];
        *price = (long)*(volatile uint32_t*)((v.mode == SHOP_MODE_BUY ? SHOP_BUY_PRICE
                                                                      : SHOP_SELL_PRICE) + iid * 4);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool  s_shopActive     = false;
static int   s_shopLastTop    = -1;
static int   s_shopLastRow    = -1;
static int   s_shopLastMode   = -1;
static int   s_shopLastQty    = -1;
static uint32_t s_shopLastMsg = 0;
static DWORD s_shopPoll       = 0;
static char  s_shopDesc[256]  = {};
static bool  s_shopLeadPending = false;

static void ShopResetState()
{
    s_shopActive   = false;
    s_shopLastTop  = -1;
    s_shopLastRow  = -1;
    s_shopLastMode = -1;
    s_shopLastQty  = -1;
    s_shopLastMsg  = 0;
    s_shopDesc[0]  = '\0';
    s_shopLeadPending = false;
    s_shopDecodeWarned = 0;
}

static void PollItemShop(const ShopView& v)
{
    // The gil is PREPENDED to the first thing spoken rather than announced on
    // its own. A separate entry line is immediately interrupted by the row the
    // cursor is already sitting on -- the same way the refine outcome was
    // stepped on in v0.33.3 -- so it becomes "Shop, 1000 gil. Buy" instead.
    // v0.34.6: the entry line has to SURVIVE until something actually speaks.
    // A shop opens in states 0..2, where there is no row to attach it to, and
    // v0.34.5 built the prefix into a local that those polls then threw away --
    // so "Shop, N gil" was lost whenever the first poll landed before the top
    // menu, which is every time.
    char lead[48];
    lead[0] = '\0';
    if (!s_shopActive) {
        s_shopActive = true;
        s_shopLeadPending = true;
        Log::Menu("[SHOP] entered item shop: shop=%d type=%d (+0x45=%d) gil=%ld",
                  ShopNumber(), ShopTypeNow(), v.shopType, v.gil);
    }
    if (s_shopLeadPending) snprintf(lead, sizeof(lead), "Shop, %ld gil. ", v.gil);

    const ShopPhase phase = ShopPhaseOf(v.state);

    // A message the engine put up -- "not enough gil" (string 0x49), "you can't
    // carry any more" (0x48). These are the game's own words out of +0x30, so
    // there is nothing here to keep in step with a translation.
    if (phase == SHOP_PHASE_OTHER || v.state == SHOP_ST_MSG) {
        if (v.msg && v.msg != s_shopLastMsg) {
            s_shopLastMsg = v.msg;
            std::string m = ShopDecodeAt((const uint8_t*)(uintptr_t)v.msg);
            if (!m.empty()) {
                ScreenReader::Speak(m.c_str(), true);
                Log::Menu("[SHOP] message: \"%s\"", m.c_str());
            }
        }
        if (v.state == SHOP_ST_MSG) return;
    }

    if (phase == SHOP_PHASE_TOPMENU) {
        s_shopLastRow = -1;
        s_shopLastQty = -1;
        if (v.topCur != s_shopLastTop) {
            s_shopLastTop = v.topCur;
            std::string lbl = ShopMenuLabel(v.topCur);
            char out[96];
            if (!lbl.empty()) snprintf(out, sizeof(out), "%s%s", lead, lbl.c_str());
            else              snprintf(out, sizeof(out), "%sOption %d", lead, v.topCur + 1);
            s_shopLeadPending = false;
            ScreenReader::Speak(out, true);
            Log::Menu("[SHOP] top menu %d -> \"%s\"", v.topCur, out);
        }
        return;
    }
    s_shopLastTop = -1;

    if (phase == SHOP_PHASE_QUANTITY) {
        int id = 0, owned = 0; long unit = -1;
        if (!ShopRowItem(v, v.cursor, &id, &owned, &unit)) return;
        if (v.qty < 1 || v.qtyMax < 1) return;      // the engine writes both in 0x0B
        if (v.qty == s_shopLastQty) return;
        s_shopLastQty = v.qty;
        s_shopLastRow = -1;

        const long total = ShopQuantityTotal(unit, v.qty);
        const long after = ShopGilAfter(v.gil, total, v.mode);
        const char* nm = (id > 0) ? GetItemName(id) : nullptr;

        char out[224];
        int n = snprintf(out, sizeof(out), "%s%d of %d", lead, v.qty, v.qtyMax);
        if (nm) n += snprintf(out + n, sizeof(out) - n, " %s", nm);
        if (total >= 0) {
            n += snprintf(out + n, sizeof(out) - n, ", %ld gil", total);
            if (after >= 0)
                n += snprintf(out + n, sizeof(out) - n, ", %ld left", after);
        }
        s_shopLeadPending = false;
        ScreenReader::Speak(out, true);
        Log::Menu("[SHOP] quantity: mode=%d qty=%d max=%d id=%d unit=%ld total=%ld -> \"%s\"",
                  v.mode, v.qty, v.qtyMax, id, unit, total, out);
        return;
    }
    s_shopLastQty = -1;

    if (phase != SHOP_PHASE_LIST) return;

    // Changing Buy to Sell keeps its own cursor (+0x3C vs +0x3E), so the row
    // must re-announce on a MODE change even when the number did not move.
    if (v.cursor == s_shopLastRow && v.mode == s_shopLastMode) return;
    s_shopLastRow  = v.cursor;
    s_shopLastMode = v.mode;

    int id = 0, owned = 0; long price = -1;
    if (!ShopRowItem(v, v.cursor, &id, &owned, &price)) return;

    // Stash the game's own description for the "/" reader.
    //
    // v0.34.8: when the decode came back lossy, say so. "GF" is not a shorter
    // way of saying whatever the Magic Scroll's description actually is -- it is
    // two characters out of a sentence, and a player has no way to tell. The
    // fragment is still worth having, so it is spoken with a word in front of it
    // that stops it being mistaken for the whole thing. Same principle as
    // suppressing Card Mod's row names rather than guessing them.
    int descDropped = 0;
    std::string desc = ShopDecodeAt((const uint8_t*)(uintptr_t)v.desc, &descDropped);
    const bool lossy = (descDropped > 0);
    if (desc.empty())    s_shopDesc[0] = '\0';
    else if (lossy)      snprintf(s_shopDesc, sizeof(s_shopDesc), "Partial description: %s", desc.c_str());
    else                 snprintf(s_shopDesc, sizeof(s_shopDesc), "%s", desc.c_str());

    char out[224];
    if (id <= 0) {
        snprintf(out, sizeof(out), "%sEmpty", lead);
    } else {
        const char* nm = GetItemName(id);
        int n = snprintf(out, sizeof(out), "%s%s", lead, nm ? nm : "Unknown item");
        if (price >= 0)
            n += snprintf(out + n, sizeof(out) - n, ", %ld gil", price);
        // Buying: what you already hold decides whether you need any. Selling:
        // how many you are about to be able to part with.
        n += snprintf(out + n, sizeof(out) - n, ", have %d", owned);
    }
    s_shopLeadPending = false;
    ScreenReader::Speak(out, true);
    Log::Menu("[SHOP] %s row %d/%d pages=%d: id=%d price=%ld owned=%d -> \"%s\"",
              v.mode == SHOP_MODE_BUY ? "buy" : "sell",
              v.cursor, v.cursor / SHOP_ROWS_PER_PAGE + 1, v.pages, id, price, owned, out);
}

// ===========================================================================
// THE JUNK SHOP
// ===========================================================================
static const uintptr_t JUNK_ROWS      = 0x01D8CC08;   // built by 0x004EA770
static const uintptr_t JUNK_MWEPON_P  = 0x01D2BB50;   // -> mwepon.bin
static const uintptr_t JUNK_MSG_P     = 0x01D2BB28;   // -> mwepon.msg
static const uintptr_t JUNK_OWNER_TAB = 0x01CF7404;   // + recIdx*12 = character
static const int       JUNK_REC_COUNT = 28;
static const int       JUNK_REC_SIZE  = 12;

// v0.34.1: **the weapon's name is not in mwepon.msg.** The Junk Shop puts
// `mwepon.msg base + record[0]` into +0x20 (0x004EAB3E) and v0.34.0 read the
// name from there -- but that file is 68 bytes of nothing but spaces and
// terminators in this release, so every row came back "Unknown weapon". The
// draw function does not use it either: 0x004EB590 calls 0x0047EBA0 with the
// record index, which is
//     u16 off = word [0x01CF7400 + idx*12]     ; 0xFFFF = no name
//     return 0x01CF3E48 + [0x01CF3ED8] + off
// and that is what this reproduces. The record index doubles as the weapon
// index -- the engine's own "already equipped" test compares them directly
// (0x004EAB94).
static const uintptr_t JUNK_WEAPON_TAB  = 0x01CF7400;   // stride 12, +0 u16 name
static const uintptr_t JUNK_NAME_BANK   = 0x01CF3E48;
static const uintptr_t JUNK_NAME_OFF_P  = 0x01CF3ED8;

static const uint8_t* JunkWeaponName(int recIdx)
{
    __try {
        if (recIdx < 0 || recIdx >= JUNK_REC_COUNT) return nullptr;
        const uint16_t off = *(volatile uint16_t*)(JUNK_WEAPON_TAB + recIdx * JUNK_REC_SIZE);
        if (off == 0xFFFF) return nullptr;
        const uint32_t bank = *(volatile uint32_t*)JUNK_NAME_OFF_P;
        return (const uint8_t*)(JUNK_NAME_BANK + bank + off);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static const int JKO_STATE   = 0x10;
static const int JKO_TEXT    = 0x20;   // the weapon's mwepon.msg text
static const int JKO_GIL     = 0x28;
static const int JKO_MSG     = 0x2C;
static const int JKO_MASK    = 0x38;   // u16: characters with a remodel
static const int JKO_NCHARS  = 0x3E;
static const int JKO_NWEAPS  = 0x3F;
static const int JKO_CHARCUR = 0x40;
static const int JKO_WEAPCUR = 0x41;
static const int JKO_EQUIP   = 0x44;   // + charCursor = that character's weapon
static const int JKO_PRICEMUL = 0x30;  // 1000, or 750 with Haggle (0x004EA71D)
static const int JKO_YESNO    = 0x42;  // 0 = Yes, 1 = No, 0xFF = none (0x004EADBB)

// The bottom panel's two comparisons, from the same two tables the draw code
// reads at 0x004EB843/0x004EB850:
//     Str  = [0x01D8CB84 + weaponIdx*4]   (the creator computes one per weapon
//            for its owner, 0x004EA534)
//     Hit% = byte [0x01CF7407 + weaponIdx*12]
// The screen shows current and new side by side with an arrow; without them a
// player cannot tell whether an upgrade is worth 200 gil.
static const uintptr_t JUNK_STR_TAB = 0x01D8CB84;
static const uintptr_t JUNK_HIT_TAB = 0x01CF7407;

struct JunkView
{
    bool ok;
    int  state, charCur, weapCur, nChars, nWeaps, mask, equipped;
    long gil;
    uint32_t text, msg;
    // the row under the weapon cursor
    bool haveRow;
    int  recIdx, rowFlags, priceByte, priceMul, yesNo;
    uint8_t matId[4], matQty[4];
    int  matHave[4];
    int  strNow, strNew, hitNow, hitNew;
};

static bool JunkReadView(JunkView* v)
{
    uint8_t* b = ShopFindModule(SHOP_JUNK_FN, SHOP_JUNK_DRAW);
    if (!b) { v->ok = false; return false; }
    __try {
        v->state   = (int)*(volatile uint16_t*)(b + JKO_STATE);
        v->charCur = (int)(int8_t)b[JKO_CHARCUR];
        v->weapCur = (int)(int8_t)b[JKO_WEAPCUR];
        v->nChars  = (int)b[JKO_NCHARS];
        v->nWeaps  = (int)b[JKO_NWEAPS];
        v->mask    = (int)*(volatile uint16_t*)(b + JKO_MASK);
        v->gil     = (long)*(volatile uint32_t*)(b + JKO_GIL);
        v->text    = *(volatile uint32_t*)(b + JKO_TEXT);
        v->msg     = *(volatile uint32_t*)(b + JKO_MSG);
        v->equipped = (v->charCur >= 0 && v->charCur < 8)
                      ? (int)(int8_t)b[JKO_EQUIP + v->charCur] : -1;

        v->priceMul = (int)*(volatile uint16_t*)(b + JKO_PRICEMUL);
        v->yesNo    = (int)(int8_t)b[JKO_YESNO];
        v->strNow = v->strNew = v->hitNow = v->hitNew = -1;
        v->haveRow = false;
        if (v->weapCur >= 0 && v->weapCur < v->nWeaps && v->nWeaps <= 64) {
            const int raw = (int)((uint8_t*)JUNK_ROWS)[v->weapCur];
            v->recIdx    = raw & JUNK_ROW_INDEX_MASK;
            v->rowFlags  = raw & (JUNK_ROW_SEEN | JUNK_ROW_BUILDABLE);
            if (v->recIdx < JUNK_REC_COUNT) {
                uint8_t* rec = *(uint8_t* volatile*)JUNK_MWEPON_P;
                if (rec) {
                    rec += v->recIdx * JUNK_REC_SIZE;
                    v->priceByte = (int)rec[3];
                    for (int i = 0; i < 4; i++) {
                        v->matId[i]  = rec[4 + i * 2];
                        v->matQty[i] = rec[5 + i * 2];
                        v->matHave[i] = 0;
                    }
                    // How many of each material the player actually holds. The
                    // screen puts this in a second column next to the required
                    // count and v0.34.1 said only the requirement -- so the one
                    // number that decides whether the remodel is possible was
                    // the number missing. Counted from the inventory itself
                    // rather than from 0x01D8D058, which only the ITEM shop's
                    // creator fills and would be stale here.
                    uint8_t* inv = (uint8_t*)SHOP_INVENTORY;
                    for (int slot = 0; slot < 198; slot++) {
                        const int iid = inv[slot * 2];
                        if (!iid) continue;
                        for (int i = 0; i < 4; i++)
                            if (v->matId[i] && iid == (int)v->matId[i])
                                v->matHave[i] = inv[slot * 2 + 1];
                    }
                    v->strNew = (int)*(volatile uint32_t*)(JUNK_STR_TAB + v->recIdx * 4);
                    v->hitNew = (int)*(volatile uint8_t*)(JUNK_HIT_TAB + v->recIdx * JUNK_REC_SIZE);
                    if (v->equipped >= 0 && v->equipped < JUNK_REC_COUNT) {
                        v->strNow = (int)*(volatile uint32_t*)(JUNK_STR_TAB + v->equipped * 4);
                        v->hitNow = (int)*(volatile uint8_t*)(JUNK_HIT_TAB + v->equipped * JUNK_REC_SIZE);
                    }
                    v->haveRow = true;
                }
            }
        }
        v->ok = (v->state >= 0 && v->state <= JUNK_ST_MAX);
        return v->ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { v->ok = false; return false; }
}

static bool  s_junkActive   = false;
static int   s_junkLastChar = -1;
static int   s_junkLastWeap = -1;
static int   s_junkLastYesNo = -2;
static uint32_t s_junkLastMsg = 0;
static char  s_junkDetail[256] = {};

static void JunkResetState()
{
    s_junkActive   = false;
    s_junkLastChar = -1;
    s_junkLastWeap = -1;
    s_junkLastYesNo = -2;
    s_junkLastMsg  = 0;
    s_junkDetail[0] = '\0';
}

// The four material pairs, named. A zero item id ends the list (the engine's
// own loop at 0x004EA80D treats a zero id as "nothing required").
static void JunkFormatMaterials(const JunkView& v, char* out, int n)
{
    int w = 0;
    out[0] = '\0';
    for (int i = 0; i < 4 && w < n - 1; i++) {
        if (v.matId[i] == 0) continue;
        const char* nm = GetItemName((int)v.matId[i]);
        // "2 M-Stone Piece, have 100" -- the screen draws the requirement and
        // what you hold as two columns, and the second one is what decides
        // whether the remodel can happen at all.
        w += snprintf(out + w, n - w, "%s%d %s, have %d", w ? "; " : "",
                      (int)v.matQty[i], nm ? nm : "unknown item", v.matHave[i]);
        if (w < n - 1 && JunkMaterialShort((int)v.matQty[i], v.matHave[i]))
            w += snprintf(out + w, n - w, " (short)");
    }
}

static void PollJunkShop(const JunkView& v)
{
    char lead[48];
    lead[0] = '\0';
    if (!s_junkActive) {
        s_junkActive = true;
        snprintf(lead, sizeof(lead), "Junk Shop, %ld gil. ", v.gil);
        Log::Menu("[SHOP] entered junk shop: gil=%ld mask=0x%04X chars=%d",
                  v.gil, (unsigned)v.mask, v.nChars);
    }

    if (v.msg && v.msg != s_junkLastMsg) {
        s_junkLastMsg = v.msg;
        std::string m = ShopDecodeAt((const uint8_t*)(uintptr_t)v.msg);
        if (!m.empty()) {
            ScreenReader::Speak(m.c_str(), true);
            Log::Menu("[SHOP] junk message: \"%s\"", m.c_str());
        }
    }

    // v0.34.2: **the confirmation dialog.** Aaron: *"A confirmation dialog
    // appears that is not announced too."* State 0x0B (0x004EADA6) is the shared
    // yes/no window -- "Remodel Rinoa's weapon to 'Valkyrie' OK?" -- built at
    // 0x004EACDD from template string 0x3B with the character and weapon names
    // substituted, then opened through the same 0x004C2B10 every other
    // confirmation in the game uses. So menu_dialog.inl already reads it; it
    // just had no caller here. Cursor is +0x42: 0 = Yes, 1 = No (0x004EADBB).
    if (v.state == JUNK_ST_CONFIRM) {
        if (v.yesNo != s_junkLastYesNo) {
            s_junkLastYesNo = v.yesNo;
            char dlg[320];
            if (MenuDialogCompose(v.yesNo, dlg, sizeof(dlg))) {
                ScreenReader::Speak(dlg, true);
                Log::Menu("[SHOP] junk confirm (cursor=%d): \"%s\"", v.yesNo, dlg);
            }
        }
        s_junkLastWeap = -1;      // re-announce the row if the player backs out
        return;
    }
    s_junkLastYesNo = -2;

    const JunkPhase phase = JunkPhaseOf(v.state);

    if (phase == JUNK_PHASE_CHARS) {
        s_junkLastWeap = -1;
        if (v.charCur == s_junkLastChar) return;
        s_junkLastChar = v.charCur;
        // The picker indexes SET BITS of the mask, not character ids
        // (0x004EA990 calls 0x004ABC40) -- the same shape as the refine screen's
        // picker, and the same way to get it wrong.
        const int cid = AbilCharAtPickerRow(v.mask, v.charCur);
        const int nNames = (int)(sizeof(REFINE_CHAR_NAMES) / sizeof(REFINE_CHAR_NAMES[0]));
        char out[96];
        if (cid >= 0 && cid < nNames) snprintf(out, sizeof(out), "%s%s", lead, REFINE_CHAR_NAMES[cid]);
        else                          snprintf(out, sizeof(out), "%sCharacter %d", lead, v.charCur + 1);
        ScreenReader::Speak(out, true);
        Log::Menu("[SHOP] junk character %d/%d: id=%d -> \"%s\"",
                  v.charCur, v.nChars, cid, out);
        return;
    }
    s_junkLastChar = -1;

    if (phase != JUNK_PHASE_WEAPONS) return;
    if (v.weapCur == s_junkLastWeap) return;
    s_junkLastWeap = v.weapCur;

    // The weapon's NAME, from the same accessor the draw function uses. Still
    // the game's own text -- this file carries no weapon table -- just not the
    // empty file the module happened to point +0x20 at.
    std::string name;
    if (v.haveRow) name = ShopDecodeAt(JunkWeaponName(v.recIdx));
    if (name.empty()) name = ShopDecodeAt((const uint8_t*)(uintptr_t)v.text);

    char mats[160];
    mats[0] = '\0';
    if (v.haveRow) JunkFormatMaterials(v, mats, sizeof(mats));

    char out[256];
    int n = snprintf(out, sizeof(out), "%s%s", lead,
                     name.empty() ? "Unknown weapon" : name.c_str());
    if (v.haveRow) {
        n += snprintf(out + n, sizeof(out) - n, ", %ld gil",
                      JunkWeaponPrice(v.priceByte, v.priceMul));
        // 0x80 means the engine has already checked gil AND all four materials
        // (0x004EA808..0x004EA839). Saying so is not a second opinion -- it is
        // the same bit the screen greys the row with.
        if (!(v.rowFlags & JUNK_ROW_BUILDABLE))
            n += snprintf(out + n, sizeof(out) - n, ", not available yet");
        else if (v.recIdx == v.equipped)
            n += snprintf(out + n, sizeof(out) - n, ", already equipped");
    }
    // The stat comparison the bottom panel draws (Str 28 -> 31, Hit 99% -> 101%).
    // Without it the player cannot tell whether an upgrade is worth paying for.
    if (v.haveRow && v.strNew >= 0) {
        if (v.strNow >= 0 && v.recIdx != v.equipped)
            n += snprintf(out + n, sizeof(out) - n, ", strength %d to %d", v.strNow, v.strNew);
        else
            n += snprintf(out + n, sizeof(out) - n, ", strength %d", v.strNew);
        if (v.hitNew >= 0) {
            if (v.hitNow >= 0 && v.recIdx != v.equipped)
                n += snprintf(out + n, sizeof(out) - n, ", hit %d to %d percent", v.hitNow, v.hitNew);
            else
                n += snprintf(out + n, sizeof(out) - n, ", hit %d percent", v.hitNew);
        }
    }

    ScreenReader::Speak(out, true);

    // The materials are a sentence, so they go on "/" rather than into the row
    // line -- the same split the card album uses for its info line.
    snprintf(s_junkDetail, sizeof(s_junkDetail), "%s%s",
             mats[0] ? "Needs " : "No materials listed",
             mats[0] ? mats : "");
    Log::Menu("[SHOP] junk weapon %d/%d: rec=%d flags=0x%02X price=%ld equip=%d "
              "-> \"%s\" | %s",
              v.weapCur, v.nWeaps, v.haveRow ? v.recIdx : -1,
              (unsigned)(v.haveRow ? v.rowFlags : 0),
              v.haveRow ? JunkWeaponPrice(v.priceByte, v.priceMul) : -1, v.equipped,
              out, s_junkDetail);
}

// ---------------------------------------------------------------------------
// The one entry point. Called from MenuTTS::Update OUTSIDE the menu-mode gate:
// a shop is opened from the FIELD, not from the main menu, so gating it on
// mode 6 would have made it silent in exactly the place it is used.
// ---------------------------------------------------------------------------
static const int SHOP_GAME_MODE = 10;   // the mode both shops run in

static void PollShops(int gameMode)
{
    DWORD now = GetTickCount();
    if (now - s_shopPoll < 60) return;
    s_shopPoll = now;

    ShopView sv;
    const bool haveItem = ShopReadView(&sv);
    if (haveItem)          { PollItemShop(sv); }
    else if (s_shopActive) { ShopResetState(); Log::Menu("[SHOP] item shop closed"); }

    JunkView jv;
    const bool haveJunk = JunkReadView(&jv);
    if (haveJunk)          { PollJunkShop(jv); }
    else if (s_junkActive) { JunkResetState(); Log::Menu("[SHOP] junk shop closed"); }

    // v0.34.2: if the game is in shop mode and NEITHER module was found, dump
    // the pool once. Two BATs have now shown an item shop on screen with not a
    // line of output, and reasoning from the disassembly has been wrong about
    // why twice. This is the evidence that settles it.
#if SHOP_POOL_DIAG
    if (gameMode == SHOP_GAME_MODE && !haveItem && !haveJunk &&
        ShopTypeNow() >= 0 && ShopTypeNow() != SHOP_TYPE_JUNK)
        ShopDumpPool();
#endif
    if (gameMode != SHOP_GAME_MODE) {
#if SHOP_POOL_DIAG
        s_shopPoolDumps = 0;
#endif
        s_shopFoundByFields = false;
    }
}

// (/) On-demand detail: the item's description in an item shop, the weapon's
// material list in a Junk Shop. Returns true when it spoke, so every other
// screen's "/" still works.
static bool ShopSpeakDetail()
{
    if (s_junkActive && s_junkDetail[0]) {
        ScreenReader::Speak(s_junkDetail, true);
        Log::Menu("[SHOP] detail (/): \"%s\"", s_junkDetail);
        return true;
    }
    if (s_shopActive && s_shopDesc[0]) {
        ScreenReader::Speak(s_shopDesc, true);
        Log::Menu("[SHOP] detail (/): \"%s\"", s_shopDesc);
        return true;
    }
    return false;
}
