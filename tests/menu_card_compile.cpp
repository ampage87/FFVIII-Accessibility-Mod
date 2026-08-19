// menu_card_compile.cpp -- v0.24.0 (#83)
//
// Compile probe for src/menu_tts_card.inl, and a live exercise of its reads.
// No host harness builds menu_tts.cpp, so this is the only pre-MSVC check the
// Card hook gets.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_card_compile tests/menu_card_compile.cpp
//
// tests/menu_sim.cpp proves the WORDING. This proves the plumbing: the module
// walk, the two savemap encodings behind getCardCount, the state gate, and the
// page-turn header.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#include <sys/mman.h>

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;

#undef __try
#undef __catch
#undef __throw_exception_again
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1

static DWORD GetTickCount() { return 0; }
static int g_key = -1;
static short GetAsyncKeyState(int vk) { return (vk == g_key) ? (short)1 : (short)0; }

namespace Log { void Menu(const char*, ...) {} }
namespace ScreenReader {
    char g_last[1024];
    int  g_count = 0;
    bool Speak(const char* t, bool = false) {
        snprintf(g_last, sizeof(g_last), "%s", t ? t : ""); g_count++; return true;
    }
    bool IsSpeaking() { return false; }
}

// The card album's AREA line is FF8 glyph text out of the loaded areames bank,
// decoded through the same two helpers the Magic help bar uses.
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) s += (char)(d[i] + 0x20);
        return s;
    }
}
static size_t MagicTextToGlyphs(const unsigned char* in, size_t n,
                                unsigned char* out, size_t cap)
{
    size_t w = 0;
    for (size_t i = 0; i < n && w < cap; i++) {
        const unsigned char b = in[i];
        if (b == 0) break;
        if (b < 0x20) { if (b >= 0x0A && b <= 0x0E) i++; continue; }
        out[w++] = (unsigned char)(b - 0x20);
    }
    return w;
}

static WORD* pMenuStateA = nullptr;
static const int JUNC_ACTIVE_OFFSET = 0x1E8;
static const int CARD_SUBSYSTEM_ID  = 7;

// The module-pool constants menu_tts_magic.inl owns in the real build.
static const uintptr_t MM_POOL_BASE = 0x01D76BC8;
static const uintptr_t MM_POOL_END  = 0x01D77078;
static const uintptr_t MM_LIST_HEAD = 0x01D76B48;

#include "menu_card_model.inl"
#include "menu_tts_card.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}
static void expect(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) != 0) {
        bad++;
        printf("  BAD: %s\n       got  \"%s\"\n       want \"%s\"\n", what, got, want);
    }
}
static void* MapAt(uintptr_t addr, size_t len)
{
    const uintptr_t pg = addr & ~(uintptr_t)0xFFF;
    const size_t    sz = ((addr + len) - pg + 0xFFF) & ~(size_t)0xFFF;
    void* p = mmap((void*)pg, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
}

int main()
{
    printf("menu_tts_card.inl compiles\n");

    // The areames pointer and the owner->textid map have to be mapped too. On
    // the host __try is a no-op, so an unmapped read is a segfault rather than
    // the caught exception it is in the real build -- which means the probe must
    // provide the pages the mod is allowed to fault on, and then exercise the
    // NULL-bank path deliberately.
    if (!MapAt(MM_LIST_HEAD, (size_t)(MM_POOL_END - MM_LIST_HEAD)) ||
        !MapAt(CD_COUNTS, 0x100) ||
        !MapAt(CD_OWNER_TEXTID, 0x100) ||
        !MapAt(CD_AREAMES_PTR, 0x10)) {
        printf("  (could not map the game address ranges -- skipping the live checks)\n");
        printf("menu_card_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }

    static uint8_t menuState[0x400];
    memset(menuState, 0, sizeof(menuState));
    pMenuStateA = (WORD*)menuState;
    menuState[JUNC_ACTIVE_OFFSET] = CARD_SUBSYSTEM_ID;

    uint8_t** head = (uint8_t**)MM_LIST_HEAD;

    // ---- the module walk ---------------------------------------------------
    *head = nullptr;
    check(FindCardModule() == nullptr, "an empty list must yield no module");
    for (int place = 0; place < 10; place++) {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint32_t*)(m + 0x08) = (i == place) ? CD_UPDATE_FN : 0x004F81F0;
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        *head = (uint8_t*)MM_POOL_BASE;
        if (FindCardModule() != (uint8_t*)(MM_POOL_BASE + place * 0x78)) {
            bad++; printf("  BAD: Card module not found in pool slot %d\n", place);
        }
    }
    printf("module walk: the album is found in all 10 pool slots, never assumed to be slot 2\n");

    // Settle on one module for the rest.
    uint8_t* mod = (uint8_t*)MM_POOL_BASE;
    memset(mod, 0, 0x78);
    *(uint32_t*)(mod + 0x08) = CD_UPDATE_FN;
    *(uint8_t**)mod = nullptr;
    *head = mod;

    // ---- getCardCount, both encodings --------------------------------------
    uint8_t* counts = (uint8_t*)CD_COUNTS;
    uint8_t* rare   = (uint8_t*)CD_RARE_BITS;
    memset(counts, 0, CARD_COUNT);
    memset(rare, 0, 8);

    check(CardCountOf(0) == CARD_UNKNOWN, "a zero byte on a common card is NEVER SEEN, not zero held");
    counts[0] = 0x80;
    check(CardCountOf(0) == 0, "0x80 is seen with none held");
    counts[0] = 0x80 | 37;
    check(CardCountOf(0) == 37, "the low seven bits are the count");
    counts[0] = 0x80 | 100;
    check(CardCountOf(0) == 100, "and 100 is the game's own cap");

    // The rare half is a completely different encoding on the same byte.
    counts[90] = 0xF0;
    check(CardCountOf(90) == CARD_UNKNOWN,
          "a rare card is unknown until its BIT is set, whatever the byte says");
    rare[(90 - CD_RARE_FIRST) >> 3] |= (uint8_t)(1u << ((90 - CD_RARE_FIRST) & 7));
    check(CardCountOf(90) == 1, "0xF0 means you hold the rare card");
    counts[90] = 0x00;
    check(CardCountOf(90) == 0, "0x00 with the bit set is \"used up\", not unknown");
    counts[90] = 0x2A;
    check(CardCountOf(90) == 0, "any other value is an NPC owner -- seen, not held");
    check(CardCountOf(-1) == CARD_UNKNOWN && CardCountOf(CARD_COUNT) == CARD_UNKNOWN,
          "out of range is unknown, not a read past the array");
    printf("card counts: both savemap encodings, including the one where 0x00 means "
           "two different things depending on the card id\n");

    // ---- the state gate ----------------------------------------------------
    memset(counts, 0, CARD_COUNT);
    memset(rare, 0, 8);
    for (int i = 0; i < 77; i++) counts[i] = 0x80 | 1;      // one of every common card
    *(uint16_t*)(mod + CDO_CURSOR) = 0;
    for (int st = 0; st < 13; st++) {
        *(uint16_t*)(mod + CDO_STATE) = (uint16_t)st;
        ResetCardMenu();
        ScreenReader::g_count = 0;
        PollCardMenu();
        const bool spoke = (ScreenReader::g_count > 0);
        if (spoke != (st == CARD_STATE_LIST)) {
            bad++;
            printf("  BAD: state %d %s\n", st, spoke ? "spoke and should not have"
                                                     : "stayed silent and should have spoken");
        }
    }
    printf("state gate: of 13 states only 5 speaks -- 7 and 9 read the input word "
           "but are slide animations, which is the trap that caught Junction\n");

    // ---- the line, and the page-turn header --------------------------------
    *(uint16_t*)(mod + CDO_STATE) = CARD_STATE_LIST;
    ResetCardMenu();
    ScreenReader::g_last[0] = '\0';
    PollCardMenu();
    expect(ScreenReader::g_last, "Level 1, Monster. Geezard, 1 4 1 5",
           "arriving speaks the level and the first card");

    ScreenReader::g_count = 0;
    PollCardMenu();
    check(ScreenReader::g_count == 0, "an unchanged row must not repeat");

    *(uint16_t*)(mod + CDO_CURSOR) = 1;
    ScreenReader::g_last[0] = '\0';
    PollCardMenu();
    expect(ScreenReader::g_last, "Funguar, 5 1 1 3", "moving down speaks the row alone");

    // A duplicate count must be LABELLED: unlabelled it joins the four values
    // and the terse form reads as five numbers.
    counts[2] = 0x80 | 4;
    *(uint16_t*)(mod + CDO_CURSOR) = 2;
    ScreenReader::g_last[0] = '\0';
    PollCardMenu();
    expect(ScreenReader::g_last, "Bite Bug, 1 3 3 5, quantity 4",
           "the count is labelled, through the real offsets");
    counts[2] = 0x80 | 1;

    // Left/right jumps a whole level; the header comes back because the page did.
    *(uint16_t*)(mod + CDO_CURSOR) = 12;      // level 2, row 2
    ScreenReader::g_last[0] = '\0';
    PollCardMenu();
    check(strncmp(ScreenReader::g_last, "Level 2, Monster. ", 18) == 0,
          "a page turn re-announces the level");

    // A never-seen card is a BLANK row on screen, so the mod must not name it.
    *(uint16_t*)(mod + CDO_CURSOR) = 100;     // rare, bit not set
    ScreenReader::g_last[0] = '\0';
    PollCardMenu();
    expect(ScreenReader::g_last, "Level 10, Player. Card 2, not seen",
           "a never-seen card is a blank row, and naming it would leak the album");
    printf("live reads: the row, the page-turn header, repeat suppression and the "
           "blank row all compose through the real offsets\n");

    // ---- the number keys ---------------------------------------------------
    *(uint16_t*)(mod + CDO_CURSOR) = 0;
    counts[0] = 0x80 | 3;
    g_key = '2';
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last,
           "Geezard. Top 1, right 4, bottom 1, left 5. Element none. Holding 3. Monster card, level 1",
           "key 2 gives the labelled form -- the counterpart to the terse line");

    // The summary panel the screen shows down its right-hand side comes from the
    // game's OWN five counters, so the mod reads those rather than recomputing
    // numbers that would not match what a guide or a sighted player is looking at.
    *(uint16_t*)(mod + CDO_TOT_MON) = 9;
    *(uint16_t*)(mod + CDO_TOT_BOS) = 0;
    *(uint16_t*)(mod + CDO_TOT_GF)  = 2;
    *(uint16_t*)(mod + CDO_TOT_PLR) = 0;
    *(uint16_t*)(mod + CDO_TOT_ALL) = 11;
    g_key = '0';
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last,
           "Monster 9, boss 0, GF 2, player 0, total 11. 77 different of 110, 77 seen",
           "key 0 speaks the screen's own five numbers, then the two it does not show");

    g_key = '1';
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last, "Card 1 of 11, level 1 of 10, Monster", "key 1 locates the cursor");
    // ---- the bottom info line, which the screen always shows ---------------
    // "MONSTER: <the monster that carries the card>" for the 77 common cards,
    // "AREA: <who is holding it>" for the 33 rares. Neither was spoken, and for
    // a rare card the AREA line is the only thing in the game that tells you
    // where to go and find it.
    g_key = '3';
    *(uint16_t*)(mod + CDO_CURSOR) = 0;
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last, "Carried by Geezard", "a common card names its monster");

    *(uint16_t*)(mod + CDO_CURSOR) = 54;      // Wedge, Biggs -- a level 5 pair
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last, "Carried by Snow Lion, Funguar",
           "and a level 5+ card names the pair that play it");

    // Rares: the two fixed owner codes resolve without touching the areames bank
    // at all, which is what makes them safe to speak even mid-load.
    *(uint16_t*)(mod + CDO_CURSOR) = 90;
    rare[(90 - CD_RARE_FIRST) >> 3] |= (uint8_t)(1u << ((90 - CD_RARE_FIRST) & 7));
    counts[90] = 0xF0;
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last, "Area, you have it", "0xF0 is you");
    counts[90] = 0x00;
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last, "Area, used up", "0x00 with the seen bit set is used up");

    // An NPC owner needs the loaded areames bank; with no bank the reader must
    // stay SILENT rather than speak whatever a null pointer dereferences to.
    counts[90] = 0x2A;
    ScreenReader::g_count = 0;
    CardNumberKeys();
    check(ScreenReader::g_count == 0,
          "with no areames bank loaded the area line says nothing, rather than guessing");

    // An unseen card has no info line at all -- the screen draws none either.
    *(uint16_t*)(mod + CDO_CURSOR) = 100;
    ScreenReader::g_last[0] = '\0';
    CardNumberKeys();
    expect(ScreenReader::g_last, "Not seen yet", "an unseen card has no source to give");
    g_key = -1;
    printf("info line: the MONSTER source for common cards and the AREA owner for "
           "rares, both of which the screen shows and neither of which was spoken\n");
    printf("number keys: collection totals, position, and the labelled detail\n");

    printf("menu_card_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
