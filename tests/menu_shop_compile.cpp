// menu_shop_compile.cpp -- v0.34.0 (#92)
//
//   g++ -std=c++17 -O0 -Isrc -o menu_shop_compile tests/menu_shop_compile.cpp
//
// WHY THIS EXISTS, AND WHY IT DRIVES RATHER THAN CHECKS
//
// v0.33.2 shipped a reader whose arming line was missing from the file. It
// compiled, its pure decision function was asserted and correct, and the feature
// did nothing at all -- because nothing called it. So this probe does what that
// one could not: it maps the real module addresses, builds a shop in them, and
// calls PollShops(), asserting the string that was SPOKEN.
//
// Both shops are new ground, so both are driven: the item shop's top menu, its
// buy and sell lists (which have SEPARATE cursors), its quantity screen, and the
// Junk Shop's character picker and weapon list.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sys/mman.h>

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;

#undef __try
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1

// Advances, so the 60 ms poll gate in PollShops() actually opens. Pinned at 0
// the whole probe would be vacuous -- the v0.33.3 lesson.
static DWORD g_tick = 0;
static DWORD GetTickCount() { g_tick += 1000; return g_tick; }

namespace Log { void Menu(const char*, ...) {} }
namespace ScreenReader {
    char g_last[512];
    int  g_count = 0;
    bool Speak(const char* t, bool = false)
    { snprintf(g_last, sizeof(g_last), "%s", t ? t : ""); g_count++; return true; }
    bool IsSpeaking() { return false; }
}
// v0.35.0 (#93): **THE REAL DECODER, NOT A STUB.**
//
// This probe used to hand-write a Decode() that "mirrors the real one where it
// matters". It did not: it dropped every byte >= 0x80, which is what the real
// decoder did in v0.34.9 -- so the probe agreed with the bug and went green
// while item descriptions came out as "GF". A stub is a statement about an
// interface, not evidence about the implementation (lint_stub.py exists because
// of exactly this shape). The real ff8_text_decode.cpp is linked in instead,
// pointed at the real namedic.bin, and PutStr writes fixtures in the game's own
// encoding so what the probe asserts is what the game would produce.
#include "ff8_text_decode.h"

#include "ff8_item_names.h"
static const char* GetItemName(int id)
{
    if (id > 0 && id < FF8_ITEM_COUNT && FF8_ITEM_NAMES[id]) return FF8_ITEM_NAMES[id];
    return "";
}

// The two things the shop reader borrows from the ability file rather than
// duplicating: the party-mask picker and the character names.
static const char* REFINE_CHAR_NAMES[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa",
    "Selphie", "Seifer", "Edea", "Laguna", "Kiros", "Ward"
};
static int AbilCharAtPickerRow(int mask, int row)
{
    if (row < 0) return -1;
    for (int i = 0; i < 32; i++) {
        if (mask & (1 << i)) { if (row == 0) return i; row--; }
    }
    return -1;
}

// **The REAL shared yes/no window, not a stub.**
//
// v0.34.2 hand-wrote a MenuDialogCompose() stub with a signature the actual
// function does not have -- (raw, cursor) returning std::string, against the
// real (cursor, char*, size_t) returning bool. The probe passed and MSVC failed
// the build. A stub is a second implementation that nothing checks against the
// first, so this includes the real file and maps its three globals instead.
#include "menu_dialog.inl"

#include "menu_shop_model.inl"
#include "menu_tts_shop.inl"


// namedic.bin -- main.fs entry 13, LZS-decompressed, byte for byte. This is the
// table the engine splices 0x0E words out of (0x004B8CB8); the probe points the
// decoder at it so the descriptions under test are the game's real sentences.
static const uint8_t NAMEDIC[408] = {
    0x20, 0x00, 0x42, 0x00, 0x4B, 0x00, 0x52, 0x00, 0x59, 0x00, 0x60, 0x00,
    0x67, 0x00, 0x6E, 0x00, 0x75, 0x00, 0x88, 0x00, 0x95, 0x00, 0xA3, 0x00,
    0xB1, 0x00, 0xBC, 0x00, 0xCA, 0x00, 0xD6, 0x00, 0xE4, 0x00, 0xF9, 0x00,
    0x08, 0x01, 0x1E, 0x01, 0x29, 0x01, 0x32, 0x01, 0x39, 0x01, 0x40, 0x01,
    0x48, 0x01, 0x4E, 0x01, 0x55, 0x01, 0x5F, 0x01, 0x66, 0x01, 0x6E, 0x01,
    0x77, 0x01, 0x88, 0x01, 0x8F, 0x01, 0x4B, 0x5F, 0x6A, 0x60, 0x5F, 0x62,
    0x67, 0x5F, 0x00, 0x49, 0x71, 0x72, 0x66, 0x5F, 0x70, 0x00, 0x46, 0x5F,
    0x6A, 0x5F, 0x6B, 0x60, 0x00, 0x48, 0x6D, 0x6A, 0x6A, 0x63, 0x72, 0x00,
    0x58, 0x67, 0x6B, 0x60, 0x63, 0x70, 0x00, 0x58, 0x70, 0x5F, 0x60, 0x67,
    0x5F, 0x00, 0x47, 0x63, 0x6C, 0x72, 0x70, 0x5F, 0x00, 0x4A, 0x67, 0x71,
    0x66, 0x63, 0x70, 0x6B, 0x5F, 0x6C, 0x71, 0x20, 0x4C, 0x6D, 0x70, 0x67,
    0x78, 0x6D, 0x6C, 0x00, 0x49, 0x5F, 0x71, 0x72, 0x20, 0x45, 0x61, 0x5F,
    0x62, 0x63, 0x6B, 0x77, 0x00, 0x48, 0x63, 0x71, 0x63, 0x70, 0x72, 0x20,
    0x54, 0x70, 0x67, 0x71, 0x6D, 0x6C, 0x00, 0x58, 0x70, 0x5F, 0x60, 0x67,
    0x5F, 0x20, 0x4B, 0x5F, 0x70, 0x62, 0x63, 0x6C, 0x00, 0x50, 0x73, 0x6C,
    0x5F, 0x70, 0x20, 0x46, 0x5F, 0x71, 0x63, 0x00, 0x57, 0x66, 0x73, 0x6B,
    0x67, 0x20, 0x5A, 0x67, 0x6A, 0x6A, 0x5F, 0x65, 0x63, 0x00, 0x48, 0x63,
    0x6A, 0x67, 0x6C, 0x65, 0x20, 0x47, 0x67, 0x72, 0x77, 0x00, 0x46, 0x5F,
    0x6A, 0x5F, 0x6B, 0x60, 0x20, 0x4B, 0x5F, 0x70, 0x62, 0x63, 0x6C, 0x00,
    0x49, 0x5F, 0x71, 0x72, 0x20, 0x45, 0x61, 0x5F, 0x62, 0x63, 0x6B, 0x77,
    0x20, 0x57, 0x72, 0x5F, 0x72, 0x67, 0x6D, 0x6C, 0x00, 0x48, 0x6D, 0x6A,
    0x6A, 0x63, 0x72, 0x20, 0x57, 0x72, 0x5F, 0x72, 0x67, 0x6D, 0x6C, 0x00,
    0x48, 0x63, 0x71, 0x63, 0x70, 0x72, 0x20, 0x54, 0x70, 0x67, 0x71, 0x6D,
    0x6C, 0x20, 0x57, 0x72, 0x5F, 0x72, 0x67, 0x6D, 0x6C, 0x00, 0x50, 0x73,
    0x6C, 0x5F, 0x70, 0x20, 0x4B, 0x5F, 0x72, 0x63, 0x00, 0x56, 0x63, 0x71,
    0x72, 0x6D, 0x70, 0x63, 0x71, 0x00, 0x71, 0x72, 0x5F, 0x72, 0x73, 0x71,
    0x00, 0x6A, 0x63, 0x5F, 0x70, 0x6C, 0x71, 0x00, 0x5F, 0x60, 0x67, 0x6A,
    0x67, 0x72, 0x77, 0x00, 0x51, 0x5F, 0x65, 0x67, 0x61, 0x00, 0x56, 0x63,
    0x64, 0x67, 0x6C, 0x63, 0x00, 0x4E, 0x73, 0x6C, 0x61, 0x72, 0x67, 0x6D,
    0x6C, 0x71, 0x00, 0x56, 0x5F, 0x67, 0x71, 0x63, 0x71, 0x00, 0x61, 0x6D,
    0x6B, 0x6B, 0x5F, 0x6C, 0x62, 0x00, 0x51, 0x5F, 0x65, 0x5F, 0x78, 0x67,
    0x6C, 0x63, 0x00, 0x59, 0x6A, 0x72, 0x67, 0x6B, 0x63, 0x61, 0x67, 0x5F,
    0x20, 0x47, 0x5F, 0x71, 0x72, 0x6A, 0x63, 0x00, 0x4B, 0x5F, 0x70, 0x62,
    0x63, 0x6C, 0x00, 0x48, 0x63, 0x6A, 0x67, 0x6C, 0x65, 0x00, 0x00, 0x00,
};

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { bad++; printf("  BAD: %s\n", what); } }

static void* MapAt(uintptr_t addr, size_t len)
{
    const uintptr_t pg = addr & ~(uintptr_t)0xFFF;
    const size_t    sz = ((addr + len) - pg + 0xFFF) & ~(size_t)0xFFF;
    void* p = mmap((void*)pg, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
}

// Write a string in the game's raw encoding, so the real decoder reads it back.
static uint8_t Enc(char c)
{
    if (c == ' ')             return 0x20;
    if (c >= '0' && c <= '9') return (uint8_t)(0x21 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x45 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0x5F + (c - 'a'));
    switch (c) {
        case '%': return 0x2B; case '/': return 0x2C; case ':': return 0x2D;
        case '!': return 0x2E; case '?': return 0x2F; case '+': return 0x31;
        case '-': return 0x32; case '=': return 0x33; case '*': return 0x34;
        case '&': return 0x35; case '(': return 0x38; case ')': return 0x39;
        case '.': return 0x3B; case ',': return 0x3C; case '~': return 0x3D;
        case '\'': return 0x3A; case '"': return 0x3E; case '#': return 0x41;
        case '$': return 0x42; case '_': return 0x44;
        default:  return 0x20;
    }
}
static void PutStr(uint8_t* p, const char* s)
{ size_t i = 0; for (; s[i]; i++) p[i] = Enc(s[i]); p[i] = 0; }

int main()
{
    printf("menu_tts_shop.inl compiles\n");

    FF8TextDecode::SetWordTableBase(NAMEDIC);

    // --- the pure model ---------------------------------------------------
    check(ShopPhaseOf(SHOP_ST_TOPMENU) == SHOP_PHASE_TOPMENU, "state 3 is the top menu");
    check(ShopPhaseOf(SHOP_ST_LIST)    == SHOP_PHASE_LIST,    "state 6 is the item list");
    check(ShopPhaseOf(SHOP_ST_QTY)     == SHOP_PHASE_QUANTITY, "state 0x0C is the quantity screen");
    check(ShopPhaseOf(SHOP_ST_QTY_ARM) == SHOP_PHASE_QUANTITY,
          "and so is 0x0B, where the engine writes the maximum");
    check(ShopPhaseOf(SHOP_ST_MSG)     == SHOP_PHASE_OTHER,   "a timed message is not a screen");

    // **The buy and sell lists keep separate cursors** (0x004EBF9D indexes
    // +0x3C by mode). Reading one while in the other names a row from the wrong
    // list -- the defect this project has now found on four screens.
    check(ShopCursorOffsetFor(SHOP_MODE_BUY)  == 0x3C, "buy's cursor is +0x3C");
    check(ShopCursorOffsetFor(SHOP_MODE_SELL) == 0x3E, "sell's is +0x3E, not the same byte");

    check(JunkPhaseOf(JUNK_ST_CHARS)   == JUNK_PHASE_CHARS,   "junk state 3 picks the character");
    check(JunkPhaseOf(JUNK_ST_WEAPONS) == JUNK_PHASE_WEAPONS, "junk state 7 picks the weapon");
    check(JunkWeaponPrice(30, 1000) == 300, "mwepon +3 is the price in tens (0x004EA7FD)");
    check(JunkWeaponPrice(30, 750) == 225,
          "and +0x30 scales it -- 750 with Haggle, so v0.34.1's figure was 25% high");
    check(JunkWeaponPrice(30, 0) == 300, "an unread multiplier falls back to the base price");
    check(JunkMaterialShort(2, 1) && !JunkMaterialShort(2, 2) && !JunkMaterialShort(2, 5),
          "a material is short only when you hold fewer than the recipe wants");

    check(ShopQuantityTotal(200, 3) == 600, "three at two hundred is six hundred");
    check(ShopGilAfter(1000, 600, SHOP_MODE_BUY)  == 400,  "buying subtracts");
    check(ShopGilAfter(1000, 600, SHOP_MODE_SELL) == 1600, "selling adds");
    check(ShopGilAfter(100, 600, SHOP_MODE_BUY) == 0,
          "and a transient over-spend clamps at zero rather than going negative");
    printf("model: two state machines, two cursors, and the price in tens\n");

    // --- which shop is open, with no module involved ----------------------
    // 0x004B2D70 is two instructions and the type table is 52 bytes; both
    // creators branch on exactly this. Reading it the same way means the log can
    // say "an ITEM shop is open and I could not read it", which is the one thing
    // three builds of guesswork could not establish.
    if (MapAt(SHOP_NUMBER_ADDR, 0x10) && MapAt(SHOP_TYPE_TABLE, 0x80)) {
        uint8_t* tab = (uint8_t*)SHOP_TYPE_TABLE;
        for (int i = 0; i < SHOP_TYPE_COUNT; i++)
            tab[i] = (i <= 20) ? (uint8_t)i : (i >= 32 && i <= 42) ? 21 : 0;

        *(uint32_t*)SHOP_NUMBER_ADDR = 6;
        check(ShopNumber() == 6 && ShopTypeNow() == 6, "shop 6 maps to itself");
        check(ShopTypeNow() != SHOP_TYPE_JUNK, "and is an item shop");

        *(uint32_t*)SHOP_NUMBER_ADDR = 34;
        check(ShopTypeNow() == SHOP_TYPE_JUNK, "shops 32..42 are Junk Shops");

        *(uint32_t*)SHOP_NUMBER_ADDR = 25;
        check(ShopTypeNow() == 0, "21..31 fold to shop 0");

        // The accessor masks to a byte; a stale high word must not be read as a
        // shop number past the table.
        *(uint32_t*)SHOP_NUMBER_ADDR = 0xDEAD0006;
        check(ShopNumber() == 6, "only the low byte is the shop number");
        *(uint32_t*)SHOP_NUMBER_ADDR = 200;
        check(ShopTypeNow() == -1, "a number past the 52-entry table yields no type");
        printf("shop identity: the engine's own accessor, so 'no shop was open' and "
               "'the reader is broken' are now different lines in the log\n");
    }

    // =====================================================================
    // THE ITEM SHOP, DRIVEN THROUGH PollShops()
    // =====================================================================
    const uintptr_t TEXT_BLOB = 0x02000000;   // stands in for the menu text heap
    const bool havePool = MapAt(SHOP_LIST_HEAD, (size_t)(SHOP_POOL_END - SHOP_LIST_HEAD)) != nullptr;
    // One map for everything from the junk-shop row list up through the sell
    // price table: they share pages, and MAP_FIXED_NOREPLACE refuses a second
    // overlapping call (a recurring hazard in these probes).
    const bool haveTabs = MapAt(JUNK_ROWS, (size_t)(SHOP_SELL_PRICE + 0x400 - JUNK_ROWS)) != nullptr;
    const bool haveHeap = MapAt(SHOP_TEXT_HEAP, 0x40) != nullptr;
    const bool haveBlob = MapAt(TEXT_BLOB, 0x4000) != nullptr;
    const bool haveInv  = MapAt(SHOP_INVENTORY, 0x400) != nullptr;
    const bool haveJunk = MapAt(JUNK_MWEPON_P, 0x40) != nullptr &&
                          MapAt(0x02100000, 0x1000) != nullptr &&
                          MapAt(JUNK_NAME_BANK, 0x5000) != nullptr;   // must reach 0x01CF7400 + 28*12

    if (!(havePool && haveTabs && haveHeap && haveBlob && haveInv && haveJunk))
        printf("  [map] pool=%d tabs=%d heap=%d blob=%d inv=%d junk=%d\n",
               havePool, haveTabs, haveHeap, haveBlob, haveInv, haveJunk);
    if (havePool && haveTabs && haveHeap && haveBlob && haveInv && haveJunk) {
        uint8_t** head = (uint8_t**)SHOP_LIST_HEAD;
        uint8_t*  slot = (uint8_t*)(SHOP_POOL_BASE + 4 * 0x78);
        memset(slot, 0, 0x78);
        slot[0x12] = 1;
        *(uint32_t*)(slot + 0x08) = SHOP_ITEM_FN;
        *(uint8_t**)slot = nullptr;
        *head = slot;

        // The text heap, laid out the way 0x004BD630 walks it: a pointer at
        // 0x00B86D30, +0x2E000, a group offset at [+3*2+2], then per-string
        // offsets at [+id*4+2].
        *(uint32_t*)SHOP_TEXT_HEAP = (uint32_t)(TEXT_BLOB - SHOP_TEXT_SECT);
        uint8_t* blob = (uint8_t*)TEXT_BLOB;
        memset(blob, 0, 0x4000);
        const int GROUP_OFF = 0x100;
        *(uint16_t*)(blob + SHOP_TEXT_GROUP * 2 + 2) = (uint16_t)GROUP_OFF;
        uint8_t* grp = blob + GROUP_OFF;
        // strings 52, 53, 54 -- the three the table at 0x00B88910 names.
        const char* tops[3] = { "Buy", "Sell", "Exit" };
        for (int i = 0; i < 3; i++) {
            const int soff = 0x400 + i * 0x40;
            *(uint16_t*)(grp + (52 + i) * 4 + 2) = (uint16_t)soff;
            PutStr(grp + soff, tops[i]);
        }

        uint8_t* stock = (uint8_t*)SHOP_STOCK;
        uint8_t* owned = (uint8_t*)SHOP_OWNED_BY_ID;
        uint32_t* buyP = (uint32_t*)SHOP_BUY_PRICE;
        uint32_t* selP = (uint32_t*)SHOP_SELL_PRICE;
        uint8_t* inv   = (uint8_t*)SHOP_INVENTORY;
        memset(stock, 0, 64); memset(owned, 0, 256); memset(inv, 0, 512);
        stock[0] = 1;  stock[1] = 1;      // Potion
        stock[2] = 7;  stock[3] = 1;      // Phoenix Down
        owned[1] = 34; owned[7] = 3;
        buyP[1] = 100; selP[1] = 50;
        buyP[7] = 500; selP[7] = 250;
        inv[0] = 7; inv[1] = 3;           // sell row 0 = Phoenix Down x3

        *(uint32_t*)(slot + SHO_GIL) = 1000;
        *(uint32_t*)(slot + SHO_INV) = 0x01CFE79C;
        slot[SHO_TYPE] = 3;

        // --- the fallback identification ----------------------------------
        // Two BATs and a pool dump say the item shop is not findable by its
        // update fn. SUBMENU_AUDIT.md §9 already records one module in this
        // engine whose +0x08 is not its update fn at all, so the reader must not
        // depend on that field alone.
        {
            // **The real thing, from the v0.34.5 pool dump:**
            //   slot 1 inUse=1 state=0x03 upd=605D8130 draw=004ED1B0
            //          +2C=01CFE79C +45=1 +46=0 +47=0
            // +0x08 overwritten with a heap pointer, +0x0C intact, and +0x47
            // still ZERO because the top-menu confirm has not run yet.
            *(uint32_t*)(slot + 0x08) = 0x605D8130;
            *(uint32_t*)(slot + 0x0C) = SHOP_ITEM_DRAW;
            *(uint16_t*)(slot + SHO_STATE) = SHOP_ST_TOPMENU;
            slot[SHO_PAGES] = 0;
            slot[SHO_TYPE]  = 1;
            check(ShopFindModule(SHOP_ITEM_FN, SHOP_ITEM_DRAW) == slot,
                  "**the DRAW fn identifies the shop when +0x08 has been clobbered**");
            ShopView probe;
            check(ShopReadView(&probe), "and the view reads at state 3, before any choice");

            // The field fallback must not require +0x47 either -- requiring it
            // is what silenced the action bar until the player picked an option.
            *(uint32_t*)(slot + 0x0C) = 0;
            check(ShopFindByFields() == slot,
                  "the field fallback accepts a page count of 0 -- +0x47 is written "
                  "by the top-menu CONFIRM, not by the creator");
            *(uint32_t*)(slot + SHO_INV) = 0x11111111;
            check(ShopFindByFields() == nullptr,
                  "but the wrong inventory base is still refused");
            *(uint32_t*)(slot + SHO_INV) = SHOP_INVENTORY;
            *(uint32_t*)(slot + 0x08) = SHOP_ITEM_FN;   // put it back
            *(uint32_t*)(slot + 0x0C) = SHOP_ITEM_DRAW;
        }

        // --- the top menu -------------------------------------------------
        // **+0x46 is written by the top-menu CONFIRM (0x004EC2E1) and by nothing
        // else** -- the creator never touches it, so on entry it holds whatever
        // the last shop left there. v0.34.0 required it to be 0 or 1 before it
        // would accept the view, which declined the module for the whole opening
        // of every shop; the v0.34.0 BAT entered an item shop and the log has not
        // one [SHOP] line for it. A stale byte must not silence the reader.
        slot[SHO_TOPCUR] = 0;
        slot[SHO_MODE]   = 0xB7;              // leftover garbage
        ShopResetState();

        // **A shop opens in states 0..2, not on the top menu.** v0.34.5 built
        // the "Shop, N gil" prefix into a local, so the polls that ran before
        // the top menu appeared threw it away -- which was every time.
        *(uint16_t*)(slot + SHO_STATE) = 0x01;
        *(uint32_t*)(slot + SHO_MSG) = 0;
        ScreenReader::g_last[0] = '\0';
        PollShops(10);
        check(ScreenReader::g_last[0] == '\0',
              "the opening states have nothing to say yet");

        *(uint16_t*)(slot + SHO_STATE) = SHOP_ST_TOPMENU;
        ScreenReader::g_last[0] = '\0';
        PollShops(10);
        // The gil rides on the FIRST utterance rather than being its own line:
        // a separate entry line is interrupted by the row the cursor is already
        // on, which is how the refine outcome got stepped on in v0.33.3.
        check(strcmp(ScreenReader::g_last, "Shop, 1000 gil. Buy") == 0,
              "and the entry line SURVIVES to the first thing actually spoken");
        {
            ShopView probe;
            slot[SHO_MODE] = 0xB7;
            check(ShopReadView(&probe) && probe.mode == SHOP_MODE_BUY,
                  "an unwritten mode byte is clamped, not treated as an unreadable module");
        }
        slot[SHO_TOPCUR] = 1;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Sell") == 0, "and the second one");
        slot[SHO_TOPCUR] = 2;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Exit") == 0, "and the third");

        // --- the buy list --------------------------------------------------
        *(uint16_t*)(slot + SHO_STATE) = SHOP_ST_LIST;
        slot[SHO_MODE] = SHOP_MODE_BUY;
        *(int16_t*)(slot + 0x3C) = 0;
        *(int16_t*)(slot + 0x3E) = 0;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Potion, 100 gil, have 34") == 0,
              "a buy row is name, price and what you already hold");
        *(int16_t*)(slot + 0x3C) = 1;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Phoenix Down, 500 gil, have 3") == 0,
              "and the next row");

        // **The sell list has its own cursor.** Switching mode with both
        // cursors at different values must name the SELL row, and must
        // re-announce even though +0x3E did not move.
        slot[SHO_MODE] = SHOP_MODE_SELL;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Phoenix Down, 250 gil, have 3") == 0,
              "switching to Sell reads +0x3E and the SELL price, not the buy one");

        // --- the quantity screen -------------------------------------------
        *(uint16_t*)(slot + SHO_STATE) = SHOP_ST_QTY;
        slot[SHO_MODE] = SHOP_MODE_BUY;
        *(int16_t*)(slot + 0x3C) = 0;                  // Potion
        slot[SHO_QTY] = 3; slot[SHO_QTYMAX] = 10;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "3 of 10 Potion, 300 gil, 700 left") == 0,
              "a buy quantity says the total and what gil is left after it");
        slot[SHO_MODE] = SHOP_MODE_SELL;
        *(int16_t*)(slot + 0x3E) = 0;                  // Phoenix Down
        slot[SHO_QTY] = 2; slot[SHO_QTYMAX] = 3;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "2 of 3 Phoenix Down, 500 gil, 1500 left") == 0,
              "a sell quantity ADDS to the gil rather than subtracting");

        // The engine writes +0x48/+0x49 inside state 0x0B, so a zero pair is a
        // frame that has not been filled in -- exactly the v0.33.1 defect.
        slot[SHO_QTY] = 0; slot[SHO_QTYMAX] = 0;
        const int before = ScreenReader::g_count;
        PollShops(10);
        check(ScreenReader::g_count == before, "the pre-write quantity frame stays silent");

        // --- a message ------------------------------------------------------
        const int MSG_OFF = 0x800;
        PutStr(grp + MSG_OFF, "You don't have enough gil.");
        *(uint16_t*)(slot + SHO_STATE) = SHOP_ST_MSG;
        *(uint32_t*)(slot + SHO_MSG) = (uint32_t)(uintptr_t)(grp + MSG_OFF);
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "You don't have enough gil.") == 0,
              "a shop message is spoken in the game's own words");

        // --- "/" reads the description --------------------------------------
        const int DESC_OFF = 0x900;
        PutStr(grp + DESC_OFF, "Restores 200 HP.");
        *(uint16_t*)(slot + SHO_STATE) = SHOP_ST_LIST;
        slot[SHO_MODE] = SHOP_MODE_BUY;
        *(int16_t*)(slot + 0x3C) = 1;
        *(uint32_t*)(slot + SHO_DESC) = (uint32_t)(uintptr_t)(grp + DESC_OFF);
        s_shopLastRow = -1;
        PollShops(10);
        ScreenReader::g_last[0] = '\0';
        check(ShopSpeakDetail() && strcmp(ScreenReader::g_last, "Restores 200 HP.") == 0,
              "\"/\" reads the item's own description");

        // **v0.35.0: THE FRAGMENTS ARE WHOLE SENTENCES NOW.** The BAT read the
        // Magic Scroll's description as "GF"; the same ten bytes, through the
        // real decoder with the real namedic.bin behind it, are the sentence.
        {
            uint8_t* d = grp + DESC_OFF;
            const uint8_t magicScroll[] = { 0xF0,0x20,0x0E,0x35,0x20,0x0E,0x37,0x20,0x0E,0x36,0x00 };
            memcpy(d, magicScroll, sizeof(magicScroll));
            s_shopLastRow = -1;
            PollShops(10);
            ScreenReader::g_last[0] = '\0';
            ShopSpeakDetail();
            check(strcmp(ScreenReader::g_last, "GF learns Magic ability") == 0,
                  "the Magic Scroll reads as its whole description");

            // The dangerous case from v0.34.8 -- a sentence that SOUNDS complete
            // because it lost only its first word. 0x0E 0x33 is "Restores".
            const uint8_t soundsFine[] = { 0x0E,0x33,0x20,0x22,0x21,0x21,0x21,0x20,0xED,0x20,0x72,0x6D,0x20,0xF0,0x00 };
            memcpy(d, soundsFine, sizeof(soundsFine));
            s_shopLastRow = -1;
            PollShops(10);
            ScreenReader::g_last[0] = '\0';
            ShopSpeakDetail();
            check(strcmp(ScreenReader::g_last, "Restores 1000 HP to GF") == 0,
                  "and so does the one that used to lose its first word silently");

            // **THE MARKER STILL WORKS.** Take the table away and the words
            // cannot be resolved -- which is what the engine itself does with a
            // null table. What must NOT happen is the fragment passing for the
            // sentence, so the reader still says it is partial.
            FF8TextDecode::SetWordTableBase(nullptr);
            memcpy(d, magicScroll, sizeof(magicScroll));
            s_shopLastRow = -1;
            PollShops(10);
            ScreenReader::g_last[0] = '\0';
            ShopSpeakDetail();
            check(strncmp(ScreenReader::g_last, "Partial description:", 20) == 0,
                  "with no word table, a fragment is announced AS a fragment");
            FF8TextDecode::SetWordTableBase(NAMEDIC);

            // A clean string must NOT be flagged.
            PutStr(grp + DESC_OFF, "Restores 200 HP.");
            s_shopLastRow = -1;
            PollShops(10);
            ScreenReader::g_last[0] = '\0';
            ShopSpeakDetail();
            check(strcmp(ScreenReader::g_last, "Restores 200 HP.") == 0,
                  "and a fully understood description reads plainly, with no marker");
        }
        printf("item shop: top menu, both lists with their own cursors, quantity, "
               "messages and \"/\" all driven through PollShops()\n");

        // =====================================================================
        // THE JUNK SHOP
        // =====================================================================
        memset(slot, 0, 0x78);              // the item shop is gone
        uint8_t* jslot = (uint8_t*)(SHOP_POOL_BASE + 5 * 0x78);
        memset(jslot, 0, 0x78);
        jslot[0x12] = 1;
        *(uint32_t*)(jslot + 0x08) = SHOP_JUNK_FN;
        *(uint8_t**)jslot = nullptr;
        *head = jslot;

        // v0.34.1: the weapon NAME comes from 0x0047EBA0's tables, not from
        // mwepon.msg -- which is 68 bytes of spaces in this release.
        uint16_t* wtab = (uint16_t*)JUNK_WEAPON_TAB;
        uint8_t*  wbank = (uint8_t*)(JUNK_NAME_BANK + 0x2000);
        memset((void*)JUNK_WEAPON_TAB, 0xFF, JUNK_REC_COUNT * JUNK_REC_SIZE);
        *(uint32_t*)JUNK_NAME_OFF_P = 0x2000;
        wtab[5 * 6] = 0x40;                       // record 5's name offset
        PutStr(wbank + 0x40, "Flail");
        wtab[6 * 6] = 0x60;
        PutStr(wbank + 0x60, "Morning Star");

        uint8_t* mwep = (uint8_t*)0x02100000;
        memset(mwep, 0, 0x400);
        *(uint32_t*)JUNK_MWEPON_P = (uint32_t)(uintptr_t)mwep;
        // record 5: 300 gil, needs 1 Screw and 2 Steel Pipes (ids from the real
        // table), text at a known offset.
        uint8_t* r5 = mwep + 5 * JUNK_REC_SIZE;
        r5[3] = 30;
        r5[4] = 128; r5[5] = 1;
        r5[6] = 123; r5[7] = 2;
        // the player's bag: 3 of material 128, none of 123
        memset(inv, 0, 512);
        inv[0] = 128; inv[1] = 3;
        uint8_t* rows = (uint8_t*)JUNK_ROWS;
        memset(rows, 0, 64);
        rows[0] = 5 | JUNK_ROW_BUILDABLE;
        rows[1] = 6 | JUNK_ROW_SEEN;          // known, but not affordable

        *(uint32_t*)(jslot + JKO_GIL)  = 5000;
        *(uint32_t*)(jslot + JKO_TEXT) = 0;      // as it effectively is in the game
        *(uint16_t*)(jslot + JKO_MASK) = (1 << 0) | (1 << 3);   // Squall, Quistis
        jslot[JKO_NCHARS] = 2;
        jslot[JKO_NWEAPS] = 2;
        jslot[JKO_CHARCUR] = 0;
        jslot[JKO_WEAPCUR] = 0;
        jslot[JKO_EQUIP + 0] = 9;
        *(uint16_t*)(jslot + JKO_PRICEMUL) = 1000;   // no Haggle
        *(uint16_t*)(jslot + JKO_STATE) = JUNK_ST_CHARS;

        JunkResetState();
        ScreenReader::g_last[0] = '\0';
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Junk Shop, 5000 gil. Squall") == 0,
              "the junk shop announces itself and row 0 of the picker together");
        jslot[JKO_CHARCUR] = 1;
        PollShops(10);
        check(strcmp(ScreenReader::g_last, "Quistis") == 0,
              "**row 1 is QUISTIS, not Zell** -- the picker indexes set bits of the mask");

        *(uint16_t*)(jslot + JKO_STATE) = JUNK_ST_WEAPONS;
        PollShops(10);
        check(strstr(ScreenReader::g_last, "Flail, 300 gil") != nullptr,
              "a buildable weapon reads its own name and its price in tens");
        // **Both columns.** Aaron: "I do not hear how many of the item I have in
        // inventory along with how many I need to upgrade." The screen draws the
        // requirement and the holding side by side, and v0.34.1 said only the
        // first -- the number that does not decide anything on its own.
        char want[192];
        snprintf(want, sizeof(want), "Needs 1 %s, have 3; 2 %s, have 0 (short)",
                 GetItemName(128), GetItemName(123));
        ScreenReader::g_last[0] = '\0';
        check(ShopSpeakDetail() && strcmp(ScreenReader::g_last, want) == 0,
              "and \"/\" lists what each material needs AND what is held");

        jslot[JKO_WEAPCUR] = 1;
        PollShops(10);
        check(strstr(ScreenReader::g_last, "not available yet") != nullptr,
              "a row without the 0x80 bit is not offered as buildable");
        check(strstr(ScreenReader::g_last, "Morning Star") != nullptr,
              "and it is still NAMED -- v0.34.0 read mwepon.msg and got "
              "\"Unknown weapon\" on every row in the BAT");

        // Standing on the weapon the character already has must say so rather
        // than letting the player pay for a no-op.
        jslot[JKO_WEAPCUR] = 0;
        jslot[JKO_CHARCUR] = 1;
        jslot[JKO_EQUIP + 1] = 5;
        s_junkLastWeap = -1;
        PollShops(10);
        check(strstr(ScreenReader::g_last, "already equipped") != nullptr,
              "the weapon already equipped is called out");

        // --- the stat comparison the bottom panel draws --------------------
        // "Str 28 -> 31, Hit 99% -> 101%". Without it the player cannot tell
        // whether an upgrade is worth paying for.
        *(uint32_t*)(JUNK_STR_TAB + 5 * 4) = 31;
        *(uint32_t*)(JUNK_STR_TAB + 9 * 4) = 28;
        *(uint8_t*)(JUNK_HIT_TAB + 5 * JUNK_REC_SIZE) = 101;
        *(uint8_t*)(JUNK_HIT_TAB + 9 * JUNK_REC_SIZE) = 99;
        jslot[JKO_CHARCUR] = 0;
        jslot[JKO_EQUIP + 0] = 9;              // currently carrying record 9
        jslot[JKO_WEAPCUR] = 0;                // offering record 5
        s_junkLastWeap = -1;
        PollShops(10);
        check(strstr(ScreenReader::g_last, "strength 28 to 31") != nullptr &&
              strstr(ScreenReader::g_last, "hit 99 to 101 percent") != nullptr,
              "an upgrade reads the before-and-after the bottom panel shows");

        // On the weapon already carried there is no "before", so it must not
        // read as a change from itself to itself.
        jslot[JKO_EQUIP + 0] = 5;
        s_junkLastWeap = -1;
        PollShops(10);
        check(strstr(ScreenReader::g_last, "strength 31") != nullptr &&
              strstr(ScreenReader::g_last, " to ") == nullptr,
              "the weapon already equipped states its stats rather than a change");

        // --- the confirmation dialog ---------------------------------------
        // Aaron: "A confirmation dialog appears that is not announced too."
        // Fill the window's three globals the way 0x004C2A20 fills them, and let
        // the REAL MenuDialogCompose() read them.
        const int DLG_BODY = 0xB00, DLG_YES = 0xB80, DLG_NO = 0xBC0;
        PutStr(grp + DLG_BODY, "Remodel Rinoa's weapon to 'Valkyrie'  OK?");
        PutStr(grp + DLG_YES, "Yes");
        PutStr(grp + DLG_NO,  "No");
        *(uint32_t*)MDLG_BODY_PTR = (uint32_t)(uintptr_t)(grp + DLG_BODY);
        *(uint32_t*)MDLG_OPT1_PTR = (uint32_t)(uintptr_t)(grp + DLG_YES);
        *(uint32_t*)MDLG_OPT2_PTR = (uint32_t)(uintptr_t)(grp + DLG_NO);
        *(uint16_t*)(jslot + JKO_STATE) = JUNK_ST_CONFIRM;
        jslot[JKO_YESNO] = 0;
        ScreenReader::g_last[0] = '\0';
        PollShops(10);
        check(strcmp(ScreenReader::g_last,
                     "Remodel Rinoa's weapon to 'Valkyrie' OK?. Yes") == 0,
              "the remodel confirmation is read through the REAL shared-window "
              "reader, with the option under the cursor and the padding collapsed");
        jslot[JKO_YESNO] = 1;
        PollShops(10);
        check(strstr(ScreenReader::g_last, ". No") != nullptr,
              "and moving to No says so -- the default is not assumed");

        printf("junk shop: set-bit picker, the weapon's own text, both material "
               "columns, the stat comparison and the confirmation dialog\n");
    } else {
        printf("  (could not map the shop addresses -- skipping the driven checks)\n");
    }

    printf("menu_shop_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
