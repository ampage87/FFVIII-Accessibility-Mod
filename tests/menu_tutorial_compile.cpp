// menu_tutorial_compile.cpp -- v0.26.0 (#85)
//
// Compile probe for src/menu_tts_tutorial.inl, and a live exercise of its reads.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_tutorial_compile tests/menu_tutorial_compile.cpp
//
// menu_sim.cpp proves the WORDING. This proves the PLUMBING, and on this screen
// the plumbing is the risky half.
//
// v0.26.0 got it wrong twice over and the probe did not catch either, because
// the probe was built around the same wrong belief the code was: that the text
// lived at module+0x20. It does not -- that field is the FOOTER HINT, so every
// message window in the exam announced "the Confirm button to quit". The text
// the game draws is the pre-processed buffer at 0x01D7DAB8 (0x004D4A80 writes
// it, 0x004D596C renders it), and this probe now drives THAT.

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

static WORD* pMenuStateA = nullptr;
static const int JUNC_ACTIVE_OFFSET    = 0x1E8;
static const int TUTORIAL_SUBSYSTEM_ID = 20;
static const int SEEDTEST_SUBSYSTEM_ID = 23;
static const int MAGAZINE_SUBSYSTEM_A  = 25;
static const int MAGAZINE_SUBSYSTEM_B  = 26;
static const int MAGAZINE_SUBSYSTEM_C  = 31;
static const int TIPS_SUBSYSTEM_ID     = 21;

static const uintptr_t MM_POOL_BASE = 0x01D76BC8;
static const uintptr_t MM_POOL_END  = 0x01D77078;
static const uintptr_t MM_LIST_HEAD = 0x01D76B48;

// v0.30.1: the Item menu's magazine states live in the Item module, which this
// probe does not own. menu_tts_item.inl provides the real walk in the mod.
// The Item module lives in menu_tts_item.inl, which this probe does not own.
// g_itemModule stands in for whatever the real identification returns, so the
// magazine reader below is exercised for real.
// GetItemName lives in menu_tts.cpp, which this probe does not own. Only the
// two ids the remodel test uses need real names.
static const char* GetItemName(uint8_t id)
{
    if (id == 155) return "Dragon Fin";
    if (id == 127) return "Spider Web";
    if (id == 0x6D) return "Screw";
    return "Item";
}

static uint8_t* g_itemModule = nullptr;
static uint8_t* FindItemModule() { return g_itemModule; }
static uint8_t* ItemModuleBaseInStates(int lo, int hi, const char** how)
{
    *how = "probe";
    if (!g_itemModule) return nullptr;
    const int st = (int)*(uint16_t*)(g_itemModule + 0x10);
    return (st >= lo && st <= hi) ? g_itemModule : nullptr;
}

#include "menu_magazine_art.inl"
#include "menu_tutorial_model.inl"
#include "menu_tts_tutorial.inl"

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

// ---------------------------------------------------------------------------
// A tiny FF8-text encoder, so the fixtures below are written as English and the
// test still exercises the real glyph table rather than a parallel copy of it.
// ---------------------------------------------------------------------------
static int Ff8Byte(char c)
{
    char one[2] = { c, 0 };
    for (int i = 0; i < TUT_GLYPH_COUNT; i++)
        if (strcmp(TUT_GLYPH[i], one) == 0) return 0x20 + i;
    return -1;
}
static int Ff8Encode(const char* s, unsigned char* out, int max)
{
    int n = 0;
    for (const char* p = s; *p && n < max - 1; p++) {
        const int b = Ff8Byte(*p);
        if (b >= 0) out[n++] = (unsigned char)b;
    }
    out[n] = 0x00;
    return n + 1;
}

// Stage the screen exactly as 0x004D4A80 leaves it: the expanded text in the
// draw buffer, and one 8-byte {x, y, slot} entry per answer slot. The labels are
// plain words on the end of the text -- the marker bytes never reach the buffer,
// which is the whole reason the cut has to come from the choice y.
static void StageScreen(const unsigned char* text, int len, int choices, int labelLine)
{
    memset((void*)SEED_TEXT_BUF, 0, 0x400);
    memcpy((void*)SEED_TEXT_BUF, text, (size_t)len);
    memset((void*)SEED_CHOICE_POS, 0, 64);
    *(uint16_t*)SEED_CHOICE_CNT = (uint16_t)choices;
    for (int c = 0; c < choices; c++) {
        uint16_t* e = (uint16_t*)(SEED_CHOICE_POS + c * 8);
        e[0] = (uint16_t)(c * 0x40);
        e[1] = (uint16_t)(labelLine * SEED_LINE_HEIGHT);
        e[2] = (uint16_t)c;
    }
}

int main()
{
    printf("menu_tts_tutorial.inl compiles\n");

    // One range for the savemap window, because the name array and the exam
    // counters share pages and MAP_FIXED_NOREPLACE refuses an overlap.
    if (!MapAt(MM_LIST_HEAD, (size_t)(MM_POOL_END - MM_LIST_HEAD)) ||
        !MapAt(0x01CFD000, 0x3000) ||
        !MapAt(SEED_RANK_GATE_A, 0x40) ||
        !MapAt(0x01D7D000, 0x3000) ||
        !MapAt(0x01D83000, 0x3000)) {
        printf("  (could not map the game address ranges -- skipping the live checks)\n");
        // =====================================================================
    // THE ITEM MENU'S MAGAZINE  (v0.30.2)
    // ---------------------------------------------------------------------
    // "Still nothing reads in the magazine" -- twice. v0.30.0 hoisted a gate on
    // a module that was never involved; v0.30.1 read the right data through an
    // identification that returns null. Neither version's decode was ever
    // executed by anything. This runs it.
    //
    // The layout, from 0x004FB60A / 0x004FCAA0 / 0x004FD746:
    //   [0x01D2BB2C] + magId*4    ->  {?, ?, firstPage, lastPage}
    //   module +0x65 magId, +0x52 page, +0x10 state (0x51..0x59, 0x55 steady)
    //   [0x01D2BB6C] + page*68    ->  the record; +0x34 = 4 x {u16,u8,u8 strIdx}
    //   [0x00B86D30] + 0x1F000    ->  u16 count, u16 offsets[], then the strings
    // =====================================================================
    {
        // 0x01D2B000 and 0x01D7D000/0x01D83000 are already mapped above; only
        // ask for what is not. Overlapping MAP_FIXED_NOREPLACE calls fail.
        const bool mapped = MapAt(0x00B86000, 0x2000) && MapAt(0x01E00000, 0x30000);
        if (!mapped) {
            printf("  (could not map the magazine globals -- skipped)\n");
        } else {
            uint8_t* arena = (uint8_t*)0x01E00000;
            memset(arena, 0, 0x30000);

            // The range table. These are the REAL records: Weapons Monthly
            // April is mmag.bin records 8..11, and record 9 is the Maverick --
            // the page in Aaron's v0.30.1 screenshot. Using the real numbers is
            // what lets the art lookup below be a test of the KEYING and not
            // just of the plumbing.
            uint8_t* range = arena;
            range[3 * 4 + 2] = 8;
            range[3 * 4 + 3] = 11;
            *(uint8_t**)0x01D2BB2C = range;

            // The string section: count, offsets, then NUL-terminated text.
            uint8_t* sec = arena + 0x1000;
            *(uint16_t*)sec = 3;
            const char* strs[3] = { "Weapons Monthly April Issue",
                                    "2/4",
                                    "With the Maverick, the combatant can deliver direct punching blows to the enemy." };
            uint16_t off = (uint16_t)(2 + 3 * 2);
            for (int i = 0; i < 3; i++) {
                *(uint16_t*)(sec + 2 + i * 2) = off;
                unsigned char enc[256];
                const int n = Ff8Encode(strs[i], enc, sizeof(enc));
                memcpy(sec + off, enc, (size_t)n);
                off = (uint16_t)(off + n);
            }
            uint8_t* archive = arena + 0x2000;
            *(uint8_t**)0x00B86D30 = archive;
            memcpy(archive + 0x1F000, sec, 0x400);

            // mwepon.bin: 33 records of 12 bytes, item pairs at +0x04. Weapon 8
            // is the Maverick and really does read {155 x1, 127 x1}.
            uint8_t* wep = arena + 0xC000;
            memset(wep, 0, 33 * 12);
            wep[8 * 12 + 4] = 155; wep[8 * 12 + 5] = 1;
            wep[8 * 12 + 6] = 127; wep[8 * 12 + 7] = 1;
            wep[1 * 12 + 4] = 0x6D; wep[1 * 12 + 5] = 6;   // one-item weapon
            *(uint8_t**)IMAG_WEPON_PTR = wep;

            // The records. Page 11 carries all three blocks; the list ends 0xFF.
            uint8_t* recs = arena + 0x8000;
            *(uint8_t**)0x01D2BB6C = recs;
            for (int r = 8; r <= 11; r++) {
                uint8_t* rec = recs + r * MAG_REC_SIZE;
                memset(rec + MAG_TEXTBLK, 0xFF, 16);
                rec[MAG_TEXTBLK + 0 * 4 + 3] = 0;
                rec[MAG_TEXTBLK + 1 * 4 + 3] = 1;
                rec[MAG_TEXTBLK + 2 * 4 + 3] = 2;
                rec[IMAG_REC_WEAPON] = 0xFF;            // no weapon by default
            }
            recs[9 * MAG_REC_SIZE + IMAG_REC_WEAPON] = 8;   // the Maverick
            recs[8 * MAG_REC_SIZE + IMAG_REC_WEAPON] = 1;   // a one-item weapon

            static uint8_t itemMod[0x78];
            memset(itemMod, 0, sizeof(itemMod));
            g_itemModule = itemMod;
            itemMod[IMAGO_ID]   = 3;
            itemMod[IMAGO_PAGE] = 9;

            // The opening states must NOT speak -- they are an animation, and a
            // page spoken while it slides is spoken again when it lands.
            s_imagActive = false; s_imagPage = -999;
            for (int st = 0x51; st <= 0x54; st++) {
                *(uint16_t*)(itemMod + IMAGO_STATE) = (uint16_t)st;
                ScreenReader::g_last[0] = '\0';
                check(PollItemMagazine(), "an opening state is still handled");
                check(ScreenReader::g_last[0] == '\0', "but must not speak");
            }

            // The reading state speaks the page, headline first.
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x55;
            ScreenReader::g_last[0] = '\0';
            check(PollItemMagazine(), "the reading state is handled");
            check(strncmp(ScreenReader::g_last, "Weapons Monthly April Issue", 27) == 0,
                  "the headline must be spoken first");
            check(strstr(ScreenReader::g_last, "punching blows") != nullptr,
                  "and the body prose with it -- the part the GCW buffer does not hold");
            check(strstr(ScreenReader::g_last, "2/4") != nullptr,
                  "the page counter is part of the stored text, not added");

            // **The remodeling list.** The page's own text stops at the heading;
            // v0.31.0 read the heading and then fell silent, which is the
            // v0.28.1 empty-page failure again -- and on a Weapons Monthly this
            // list is the practical point of the page.
            check(strstr(ScreenReader::g_last, "Dragon Fin, 1.") != nullptr,
                  "the first remodeling item must be read with its count");
            check(strstr(ScreenReader::g_last, "Spider Web, 1.") != nullptr,
                  "and so must the second");
            {
                const char* a = strstr(ScreenReader::g_last, "Dragon Fin");
                const char* b = strstr(ScreenReader::g_last, "Spider Web");
                check(a && b && a < b, "in the order the panel lists them");
            }
            check(strstr(ScreenReader::g_last, "Last page") == nullptr,
                  "page 2 of 4 is not the last page");

            // Holding still says nothing.
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(ScreenReader::g_last[0] == '\0', "sitting on a page must stay silent");

            // A page slide and back must re-speak the new page, once.
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x58;
            PollItemMagazine();
            itemMod[IMAGO_PAGE] = 10;
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x55;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(ScreenReader::g_last[0] != '\0', "a new page speaks");
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(ScreenReader::g_last[0] == '\0', "and only once");

            // A weapon needing one item lists one, not four.
            itemMod[IMAGO_PAGE] = 8;
            s_imagPage = -999;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(strstr(ScreenReader::g_last, "Screw, 6.") != nullptr,
                  "a one-item weapon reads its single requirement");
            check(strstr(ScreenReader::g_last, "Unknown item") == nullptr,
                  "and does not run past the end of its list");

            // A page with no weapon id must not borrow another weapon's list.
            itemMod[IMAGO_PAGE] = 10;
            s_imagPage = -999;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(strstr(ScreenReader::g_last, "Dragon Fin") == nullptr &&
                  strstr(ScreenReader::g_last, "Screw") == nullptr,
                  "a page with no weapon id gets no remodeling list");

            // The last page says so; nothing on screen does.
            itemMod[IMAGO_PAGE] = 11;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(strstr(ScreenReader::g_last, "Last page") != nullptr,
                  "the last page must say so");

            // A page outside the magazine's range is refused rather than read
            // out of a neighbouring magazine's records.
            itemMod[IMAGO_PAGE] = 20;
            ScreenReader::g_last[0] = '\0';
            check(!PollItemMagazine(), "a page outside the range is refused");

            // Leaving the magazine states hands the frame back.
            itemMod[IMAGO_PAGE] = 9;
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x0E;
            check(!PollItemMagazine(), "outside 0x51..0x59 the poll declines");
            g_itemModule = nullptr;
            check(!PollItemMagazine(), "with no Item module the poll declines");

            // --- "/" describes the picture (v0.31.0) --------------------
            // The table is hand-written from the art, so what a probe can hold
            // it to is that every entry EXISTS, is non-empty, is not a
            // duplicate of another, and is keyed to the right page -- and that
            // the key falls through on pages that have no weapon plate.
            {
                g_itemModule = itemMod;               // the block above cleared it
                itemMod[IMAGO_PAGE] = 9;
                *(uint16_t*)(itemMod + IMAGO_STATE) = 0x55;
                s_imagActive = false; s_imagPage = -999; s_imagArtRec = -1;
                PollItemMagazine();
                ScreenReader::g_last[0] = '\0';
                check(MagazineSpeakArt(), "the key is claimed on a weapon page");
                check(strncmp(ScreenReader::g_last, "Maverick.", 9) == 0,
                      "record 9 is the Maverick -- the page in Aaron's own screenshot");
                check(strstr(ScreenReader::g_last, "glove") != nullptr,
                      "and its description is of gloves, as its prose says");

                // Every entry present, non-trivial and distinct.
                for (int r = 0; r < MAG_ART_COUNT; r++) {
                    if (!MAG_ART[r].name || !MAG_ART[r].name[0]) {
                        bad++; printf("  BAD: art %d has no name\n", r); continue;
                    }
                    if (!MAG_ART[r].look || strlen(MAG_ART[r].look) < 40) {
                        bad++; printf("  BAD: art %d (%s) has no real description\n",
                                      r, MAG_ART[r].name);
                    }
                    for (int q = 0; q < r; q++)
                        if (strcmp(MAG_ART[r].look, MAG_ART[q].look) == 0) {
                            bad++;
                            printf("  BAD: art %d (%s) repeats art %d (%s)\n",
                                   r, MAG_ART[r].name, q, MAG_ART[q].name);
                        }
                }

                // The three weapons whose SHAPE would be misread without the
                // page prose -- Aaron's own example among them.
                check(strstr(MAG_ART[15].look, "whip") != nullptr,
                      "Slaying Tail must be called a whip, not a tail");
                check(strstr(MAG_ART[3].look, "whip") != nullptr,
                      "Strange Vision must be called a whip");
                check(strstr(MAG_ART[19].look, "whip") != nullptr,
                      "Red Scorpion must be called a whip");

                // A page with no weapon plate must not borrow one.
                s_imagActive = true; s_imagArtRec = 40;
                ScreenReader::g_last[0] = '\0';
                check(MagazineSpeakArt(), "the key is still claimed on other magazines");
                check(strstr(ScreenReader::g_last, "No picture description") != nullptr,
                      "but it says there is no plate rather than describing one");

                // Off the magazine entirely, the key must fall through.
                s_imagActive = false; s_imagArtRec = -1;
                check(!MagazineSpeakArt(), "off the magazine the key falls through");
                printf("magazine art: 28 distinct plates, keyed to the right page, "
                       "the three whips named as whips, and no plate invented\n");
            }

            printf("item magazine: the page decoded from the record and spoken once, "
                   "openers silent, slides re-speak, last page named, "
                   "out-of-range refused\n");
        }
    }

    printf("menu_tutorial_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }

    *(uint8_t*)SEED_RANK_GATE_A = 1;
    *(uint8_t*)SEED_RANK_GATE_B = 0;
    *(int16_t*)SEED_RANK_POINTS = 700;      // rank 7
    *(uint8_t*)SEED_TESTS_PASS  = 6;
    *(uint16_t*)SEED_CHOICE_CNT = 2;

    // Names, in the game's own encoding, at the two addresses 0x0047EB50
    // answers directly plus the 68-byte-stride GF array from 0x0047E970.
    unsigned char nm[32];
    Ff8Encode("Squall", nm, sizeof(nm));
    memcpy((void*)SEED_NAME_CHAR0, nm, SEED_NAME_BYTES);
    Ff8Encode("Rinoa", nm, sizeof(nm));
    memcpy((void*)SEED_NAME_CHAR4, nm, SEED_NAME_BYTES);
    for (int g = 0; g < 16; g++) {
        char t[16]; snprintf(t, sizeof(t), "GF%d", g);
        Ff8Encode(t, nm, sizeof(nm));
        memcpy((void*)(SEED_NAME_GF0 + g * SEED_NAME_GF_STRIDE), nm, SEED_NAME_BYTES);
    }
    Ff8Encode("Ifrit", nm, sizeof(nm));
    memcpy((void*)(SEED_NAME_GF0 + 2 * SEED_NAME_GF_STRIDE), nm, SEED_NAME_BYTES);

    static uint8_t menuState[0x400];
    memset(menuState, 0, sizeof(menuState));
    pMenuStateA = (WORD*)menuState;
    menuState[JUNC_ACTIVE_OFFSET] = TUTORIAL_SUBSYSTEM_ID;

    // ---- the module walk ----------------------------------------------------
    uint8_t** head = (uint8_t**)MM_LIST_HEAD;
    *head = nullptr;
    check(FindModuleByUpdateFn(TUT_UPDATE_FN) == nullptr, "an empty list yields no module");
    for (int place = 0; place < 10; place++) {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint32_t*)(m + 0x08) = (i == place) ? TUT_UPDATE_FN : 0x004F81F0;
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        *head = (uint8_t*)MM_POOL_BASE;
        if (FindModuleByUpdateFn(TUT_UPDATE_FN) != (uint8_t*)(MM_POOL_BASE + place * 0x78)) {
            bad++; printf("  BAD: Tutorial module not found in pool slot %d\n", place);
        }
    }
    printf("module walk: found in all 10 pool slots\n");

    // ---- the Tutorial list --------------------------------------------------
    for (int i = 0; i < 10; i++) {
        uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
        memset(m, 0, 0x78);
        *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
    }
    uint8_t* tut = (uint8_t*)(MM_POOL_BASE + 3 * 0x78);
    *(uint32_t*)(tut + 0x08) = TUT_UPDATE_FN;
    *head = (uint8_t*)MM_POOL_BASE;

    *(uint16_t*)(tut + TUTO_STATE)  = TUT_STATE_LIST;
    *(int8_t*)(tut + TUTO_CURSOR)   = 0;
    ResetTutorialMenu();
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    expect(ScreenReader::g_last, "Battle Operation. Battle Explanation", "the first row");

    *(int8_t*)(tut + TUTO_CURSOR) = TUT_ROW_TEST;
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "Next is test 7 of 30") != nullptr,
          "the Test row says which test comes next, which no screen states");

    // **The two gates the game enforces silently.** Confirming a gated row just
    // beeps, so a player who is not told is left pressing Confirm at a screen
    // that never answers.
    *(uint8_t*)SEED_TESTS_PASS = 0;
    *(int8_t*)(tut + TUTO_CURSOR) = TUT_ROW_REVIEW;
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "Not available") != nullptr,
          "Review with nothing passed says it is unavailable rather than beeping");
    *(uint8_t*)SEED_RANK_GATE_B = 1;                 // "not a SeeD yet"
    *(int8_t*)(tut + TUTO_CURSOR) = TUT_ROW_TEST;
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "until you are a SeeD") != nullptr,
          "and the Test row reports the rank gate, reproduced from 0x004C3090");
    *(uint8_t*)SEED_RANK_GATE_B = 0;
    *(uint8_t*)SEED_TESTS_PASS  = 6;
    printf("tutorial list: seven rows, and both gates the game enforces in silence\n");

    // ---- the review picker --------------------------------------------------
    *(uint16_t*)(tut + TUTO_STATE)   = TUT_STATE_TESTPICK;
    *(int8_t*)(tut + TUTO_TESTPICK)  = 2;
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    expect(ScreenReader::g_last, "Test 3 of 6", "the review picker names the test");
    *(int8_t*)(tut + TUTO_TESTPICK) = 9;
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "not yet passed") != nullptr,
          "and a row past the end says why Confirm will refuse it");
    printf("review picker: the flat 10-per-page cursor at +0x32\n");

    // ---- the exam -----------------------------------------------------------
    //
    // Build a real blob and put the exam module in the pool ALONGSIDE the
    // Tutorial one, because that is how the game leaves them: state 17 pushes
    // the exam and the Tutorial module stays behind in state 18.
    unsigned char q0[256], q1[256], q2[256];
    int l0 = Ff8Encode("The Draw command extracts magic from enemies.", q0, sizeof(q0));
    // Question with a symbol AND a renamed GF, which is the shape Aaron asked
    // about. 0x05 0x45 is the Junction Ability icon; 0x0C 0x62 is GF id 2.
    int n1 = 0;
    n1 += Ff8Encode("Test: ", q1, sizeof(q1)) - 1;
    q1[n1++] = 0x05; q1[n1++] = 0x45;
    n1 += Ff8Encode(" and ", q1 + n1, (int)sizeof(q1) - n1) - 1;
    q1[n1++] = 0x0C; q1[n1++] = 0x62;
    q1[n1++] = 0x02;                                   // line break
    n1 += Ff8Encode("go together.", q1 + n1, (int)sizeof(q1) - n1) - 1;
    q1[n1++] = 0x00;
    // Character-name substitution plus a button whose sprite depends on the
    // player's own map.
    int n2 = 0;
    q2[n2++] = 0x03; q2[n2++] = 0x30;
    n2 += Ff8Encode(" presses ", q2 + n2, (int)sizeof(q2) - n2) - 1;
    q2[n2++] = 0x05; q2[n2++] = 0x23;
    n2 += Ff8Encode(".", q2 + n2, (int)sizeof(q2) - n2) - 1;
    q2[n2++] = 0x00;

    uint8_t* exam = (uint8_t*)(MM_POOL_BASE + 6 * 0x78);
    *(uint32_t*)(exam + 0x08) = SEED_UPDATE_FN;
    *(uint16_t*)(exam + SEEDO_STATE)    = SEED_STATE_QUESTION;
    *(uint8_t*)(exam + SEEDO_TESTIDX)   = 6;
    *(uint8_t*)(exam + SEEDO_QUESTION)  = 0;
    *(int8_t*)(exam + SEEDO_CHOICE)     = 0;
    *(uint8_t*)(exam + SEEDO_REVIEW)    = 0;

    // The buffer as the game leaves it: question text, two blank lines, then the
    // labels. **The labels are ordinary words** -- 0x004D4A80 diverts the answer
    // markers to the position array and copies "YES     NO" straight through --
    // so the only thing that says where they start is the first choice's pen y.
    {
        unsigned char full[512];
        int f = 0;
        memcpy(full, q0, (size_t)l0 - 1); f = l0 - 1;
        full[f++] = 0x02; full[f++] = 0x02;
        f = Ff8Encode("YES     NO", full + f, (int)sizeof(full) - f) + f;
        StageScreen(full, f, 2, 2);   // two line breaks -> pen y 0x20 -> line 2
    }
    ResetTutorialMenu();
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "Question 1 of 10") != nullptr &&
          strstr(ScreenReader::g_last, "The Draw command extracts magic from enemies.") != nullptr &&
          strstr(ScreenReader::g_last, "Answer Yes") != nullptr,
          "**the exam wins over the Tutorial module when both are in the pool**");
    check(strstr(ScreenReader::g_last, "YES") == nullptr &&
          strstr(ScreenReader::g_last, "NO") == nullptr,
          "and the answer LABELS are cut at the choice line, not read as part of the question");

    // Moving the cursor says one word. Re-reading a forty-word stem on every
    // press would make the choice unusable.
    *(int8_t*)(exam + SEEDO_CHOICE) = 1;
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    expect(ScreenReader::g_last, "No", "moving between the answers says only the word");

    // ---- the symbols --------------------------------------------------------
    *(uint8_t*)(exam + SEEDO_QUESTION) = 1;
    *(int8_t*)(exam + SEEDO_CHOICE)    = 0;
    StageScreen(q1, n1, 2, 12);
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "the Junction Ability icon") != nullptr,
          "an ability icon is NAMED -- six tests are otherwise unpassable except by luck");
    check(strstr(ScreenReader::g_last, "Ifrit") != nullptr,
          "and a GF is spoken with the name the player gave it, read from the savemap");
    check(strstr(ScreenReader::g_last, "go together") != nullptr,
          "with the line break carried through rather than gluing two clauses together");

    *(uint8_t*)(exam + SEEDO_QUESTION) = 2;
    StageScreen(q2, n2, 2, 12);
    ScreenReader::g_last[0] = '\0';
    PollTutorialMenu();
    check(strstr(ScreenReader::g_last, "Squall") != nullptr,
          "a character name substitution uses the player's own name");
    check(strstr(ScreenReader::g_last, "the Trigger button") != nullptr,
          "and a remappable button is spoken as what it DOES, since the sprite moves "
          "when the player's map does");
    printf("exam: the question, the labels cut, the icons named, and both name "
           "substitutions read out of the savemap\n");

    // ---- the message windows ------------------------------------------------
    //
    // **This is the v0.26.0 regression, reproduced from Aaron's two screenshots.**
    // Both screens announced "the Confirm button to quit" -- the footer hint at
    // module+0x20 -- and nothing else. The offer screen and the result screen are
    // the only two places the exam tells the player anything, and the result one
    // is the only place the score appears at all.
    {
        unsigned char m[512];
        int f = 0;
        f = Ff8Encode("We will begin the SeeD written test.", m, sizeof(m)) - 1;
        m[f++] = 0x02;
        f += Ff8Encode("Your test level now is 1.", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x02; m[f++] = 0x02;
        f += Ff8Encode("Will you take the level 2 test?", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x02; m[f++] = 0x02; m[f++] = 0x02;
        f += Ff8Encode("YES    NO", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x00;
        StageScreen(m, f, 2, 6);

        *(uint16_t*)(exam + SEEDO_STATE) = SEED_STATE_MSG_E;   // 16, the offer
        *(int8_t*)(exam + SEEDO_CHOICE)  = 0;
        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        check(strstr(ScreenReader::g_last, "We will begin the SeeD written test.") != nullptr &&
              strstr(ScreenReader::g_last, "Will you take the level 2 test?") != nullptr,
              "the offer screen reads the MESSAGE, not the footer hint at +0x20");
        check(strstr(ScreenReader::g_last, "YES") == nullptr,
              "and its YES/NO labels are cut, since the mod says the answer itself");
        check(strstr(ScreenReader::g_last, "Yes") != nullptr,
              "which it does, because a two-choice window has an answer to report");

        // **The "Really?" confirmation lists NO FIRST** (mngrp section 95 string
        // 7: "  <slot0>NO      <slot1>YES"). Naming the cursor position from a
        // hard-coded Yes/No would tell the player the exact opposite on the one
        // screen whose whole job is to double-check them.
        f = Ff8Encode("Really?", m, sizeof(m)) - 1;
        m[f++] = 0x02; m[f++] = 0x02; m[f++] = 0x02;
        m[f++] = 0x02; m[f++] = 0x02; m[f++] = 0x02;
        f += Ff8Encode("  NO      YES", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x00;
        StageScreen(m, f, 2, 6);
        *(uint16_t*)(exam + SEEDO_STATE) = SEED_STATE_MSG_C;
        *(int8_t*)(exam + SEEDO_CHOICE)  = 0;
        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last, "Really?. No",
               "the Really? dialog reads NO for cursor 0, because that is what it shows");
        *(int8_t*)(exam + SEEDO_CHOICE) = 1;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last, "Really?. Yes", "and Yes for cursor 1");

        // A label that is two words stays one label -- FF8 separates answers with
        // a RUN of spaces, and a single space is part of the word.
        f = Ff8Encode("You are not allowed to take any more tests.", m, sizeof(m)) - 1;
        m[f++] = 0x02; m[f++] = 0x02;
        f += Ff8Encode("  GO BACK", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x00;
        StageScreen(m, f, 1, 2);
        *(uint16_t*)(exam + SEEDO_STATE) = SEED_STATE_MSG_A;
        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last, "You are not allowed to take any more tests.",
               "a one-answer window reads the message and does not name the button");

        // The result screen. **The score is only in this text**, put there by the
        // game's own variable substitution before it reached the buffer -- one of
        // the reasons to read what is drawn rather than the stored string.
        f = Ff8Encode("Your score was 80.", m, sizeof(m)) - 1;
        m[f++] = 0x02;
        f += Ff8Encode("You failed.", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x02;
        f += Ff8Encode("Better luck next time.", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x02; m[f++] = 0x02; m[f++] = 0x02;
        f += Ff8Encode("END", m + f, (int)sizeof(m) - f) - 1;
        m[f++] = 0x00;
        StageScreen(m, f, 1, 5);

        *(uint16_t*)(exam + SEEDO_STATE) = SEED_STATE_RESULT;
        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last,
               "Your score was 80. You failed. Better luck next time.",
               "the result screen reads the score and the verdict, and stops there");
        check(strstr(ScreenReader::g_last, "END") == nullptr,
              "the single END label is cut too -- it is a button, not the result");

        // And it must not repeat. A message window never changes, so a poll that
        // logged and spoke every frame wrote forty identical lines a second into
        // Aaron's BAT log.
        ScreenReader::g_count = 0;
        for (int i = 0; i < 20; i++) PollTutorialMenu();
        check(ScreenReader::g_count == 0,
              "and it is said once, not once per frame");
    }
    printf("message windows: the offer and the result read their own text, the "
           "labels are cut, and neither repeats\n");

    // ---- the state gate -----------------------------------------------------
    int spoke = 0;
    for (int st = 0; st < 28; st++) {
        if (st == SEED_STATE_QUESTION || SeedStateIsMessage(st)) continue;
        ResetTutorialMenu();
        *(uint16_t*)(exam + SEEDO_STATE) = (uint16_t)st;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        if (ScreenReader::g_last[0]) {
            spoke++; printf("  BAD: exam state %d spoke \"%s\"\n", st, ScreenReader::g_last);
        }
    }
    bad += spoke;
    check(spoke == 0, "no exam state outside the question and the message windows speaks");
    printf("state gate: of 28 exam states only 21 and the six message states speak -- "
           "19 and 25 are slides, and a slide that reads input is still a slide\n");

    // ---- the number keys ----------------------------------------------------
    *(uint16_t*)(exam + SEEDO_STATE)   = SEED_STATE_QUESTION;
    *(uint8_t*)(exam + SEEDO_QUESTION) = 0;
    ResetTutorialMenu();
    PollTutorialMenu();
    g_key = '1';
    ScreenReader::g_last[0] = '\0';
    TutorialNumberKeys();
    check(strstr(ScreenReader::g_last, "Question 1 of 10, test 7") != nullptr &&
          strstr(ScreenReader::g_last, "All ten must be correct") != nullptr,
          "key 1 locates the question and says the pass condition");
    // **The running score must never be spoken.** The game does not show it and
    // a sighted player cannot know it, so reporting it would be extra
    // information rather than equal access to the same information.
    *(uint16_t*)(exam + 0x26) = 4;
    ScreenReader::g_last[0] = '\0';
    TutorialNumberKeys();
    check(strstr(ScreenReader::g_last, "4") == nullptr,
          "and it never leaks the running score, which the screen does not show");
    g_key = -1;
    printf("number keys: the question again, where you are, and the two answers\n");

    // ---- the magazine viewer (Battle Operation / Card Rules / Icon Explanation)
    //
    // One module for all three. The record range is the ONLY thing that says
    // which of them you are in, so that is what the topic name comes from.
    {
        static unsigned char recs[69 * MAG_REC_SIZE];
        static unsigned char sec[4096];
        memset(recs, 0, sizeof(recs));
        memset(sec,  0, sizeof(sec));
        *(const unsigned char**)MAG_RECS_PTR = recs;

        // Three strings: heading, counter, body -- the real shape of a page.
        const char* S[3] = { "Status Window", "  Battle Tutorial 1/8",
                             " A status window is displayed." };
        *(uint16_t*)sec = 3;
        int wp = 2 + 3 * 2;
        for (int i = 0; i < 3; i++) {
            *(uint16_t*)(sec + 2 + i * 2) = (uint16_t)wp;
            wp += Ff8Encode(S[i], sec + wp, (int)sizeof(sec) - wp);
        }
        memcpy((void*)MAG_TEXT_SEC, sec, sizeof(sec) > 0x800 ? 0x800 : sizeof(sec));

        for (int b = 0; b < MAG_TEXTBLKS; b++)
            recs[43 * MAG_REC_SIZE + MAG_TEXTBLK + b * 4 + 3] = (b < 3) ? (unsigned char)b : 0xFF;
        // The last page of the topic, with only two blocks.
        recs[50 * MAG_REC_SIZE + MAG_TEXTBLK + 0 * 4 + 3] = 0;
        recs[50 * MAG_REC_SIZE + MAG_TEXTBLK + 1 * 4 + 3] = 2;
        recs[50 * MAG_REC_SIZE + MAG_TEXTBLK + 2 * 4 + 3] = 0xFF;

        *(uint8_t*)MAG_FIRST_REC = 43;
        *(uint8_t*)MAG_LAST_REC  = 50;

        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        uint8_t* tutm = (uint8_t*)(MM_POOL_BASE + 1 * 0x78);
        *(uint32_t*)(tutm + 0x08) = TUT_UPDATE_FN;
        uint8_t* magm = (uint8_t*)(MM_POOL_BASE + 5 * 0x78);
        *(uint32_t*)(magm + 0x08) = MAG_UPDATE_FN;
        *head = (uint8_t*)MM_POOL_BASE;
        menuState[JUNC_ACTIVE_OFFSET] = MAGAZINE_SUBSYSTEM_A;

        *(uint16_t*)(magm + MAGO_STATE)  = MAG_STATE_PAGE;
        *(uint32_t*)(magm + MAGO_RECORD) = 43;
        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last,
               "Battle Operation. Status Window. Battle Tutorial 1/8. "
               "A status window is displayed.",
               "page 1 names the topic and reads all three text blocks");
        check(strstr(ScreenReader::g_last, "Last page") == nullptr,
              "and does not claim to be the last page");

        // **The magazine module must win over the Tutorial module**, which is
        // still sitting in the pool underneath it in its own state.
        *(uint16_t*)(tutm + TUTO_STATE) = TUT_STATE_LIST;
        *(int8_t*)(tutm + TUTO_CURSOR)  = 0;
        ScreenReader::g_last[0] = '\0';
        *(uint32_t*)(magm + MAGO_RECORD) = 44;
        PollTutorialMenu();
        check(strstr(ScreenReader::g_last, "Battle Operation.") == nullptr &&
              strstr(ScreenReader::g_last, "Status Window") != nullptr,
              "later pages skip the topic name, and the Tutorial list does not "
              "speak over the page");

        // The last page says so, because Confirm stops turning pages there and
        // leaves instead, and nothing on screen mentions it.
        *(uint32_t*)(magm + MAGO_RECORD) = 50;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        check(strstr(ScreenReader::g_last, "Last page") != nullptr,
              "the last page warns that Confirm now leaves");
        check(strstr(ScreenReader::g_last, "Battle Tutorial 1/8") == nullptr,
              "and a page with a 0xFF-terminated short block list stops there");

        // Position on demand.
        g_key = '1';
        ScreenReader::g_last[0] = '\0';
        TutorialNumberKeys();
        check(strstr(ScreenReader::g_last, "Battle Operation, page 8 of 8") != nullptr,
              "key 1 gives the topic and the page count");
        g_key = -1;

        // Only state 9 speaks. Every other state is a fade or a wipe, and none
        // of them samples input at all in this module.
        int spoke = 0;
        for (int st = 0; st < 17; st++) {
            if (st == MAG_STATE_PAGE) continue;
            ResetTutorialMenu();
            *(uint16_t*)(magm + MAGO_STATE) = (uint16_t)st;
            ScreenReader::g_last[0] = '\0';
            PollTutorialMenu();
            if (ScreenReader::g_last[0]) {
                spoke++; printf("  BAD: magazine state %d spoke \"%s\"\n", st, ScreenReader::g_last);
            }
        }
        bad += spoke;
        check(spoke == 0, "no magazine state but 9 speaks");

        check(strcmp(MagTopicName(43), "Battle Operation") == 0 &&
              strcmp(MagTopicName(51), "Card Game Rules") == 0 &&
              strcmp(MagTopicName(64), "Icon Explanation") == 0 &&
              MagTopicName(0) == nullptr,
              "the record range names the topic, and the field magazines are not ours");
        menuState[JUNC_ACTIVE_OFFSET] = TUTORIAL_SUBSYSTEM_ID;
    }
    printf("magazine: one module for three topics, told apart by record range; "
           "all text blocks read; only state 9 speaks\n");

    // ---- Online Help -------------------------------------------------------
    //
    // **Not a module.** Row 1's action byte is 0xFF, which sets state 24 inside
    // the Tutorial module itself, so the list is a second panel with its own
    // cursor at +0x35 and its own length at +0x36. Its rows are filtered by
    // story progress, and the module writes the survivors' descriptor indices
    // into +0x39.. -- which is what the mod reads, rather than reproducing a
    // savemap flag bitmap for no gain.
    {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        uint8_t* t2 = (uint8_t*)(MM_POOL_BASE + 2 * 0x78);
        *(uint32_t*)(t2 + 0x08) = TUT_UPDATE_FN;
        *head = (uint8_t*)MM_POOL_BASE;
        menuState[JUNC_ACTIVE_OFFSET] = TUTORIAL_SUBSYSTEM_ID;

        // Three topics unlocked: descriptors 0, 4 and 5 -- deliberately NOT
        // 0,1,2, because the map is not the identity once the filter bites.
        *(uint16_t*)(t2 + TUTO_STATE)   = TUT_STATE_HELPLIST;
        *(uint8_t*)(t2 + TUTO_HELPCNT)  = 3;
        *(uint8_t*)(t2 + TUTO_HELPCUR)  = 0;
        *(uint8_t*)(t2 + TUTO_HELPMAP + 0) = 0;
        *(uint8_t*)(t2 + TUTO_HELPMAP + 1) = 4;
        *(uint8_t*)(t2 + TUTO_HELPMAP + 2) = 5;

        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        check(strstr(ScreenReader::g_last, "Online Help. 3 topics") != nullptr &&
              strstr(ScreenReader::g_last, "GF Junction") != nullptr,
              "arriving in Online Help says how many topics the story has unlocked");
        check(strstr(ScreenReader::g_last, "does not yet describe") != nullptr,
              "and says plainly that the demos behind it are not spoken yet");

        *(uint8_t*)(t2 + TUTO_HELPCUR) = 1;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last, "GF Tutorial. Explanation of GF. 2 of 3",
               "row 2 maps through +0x39 to descriptor 4, not to row 2's own index");

        // The row named after a party member uses the player's own name, read
        // from the same savemap slot the exam questions use.
        *(uint8_t*)(t2 + TUTO_HELPCUR) = 2;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        check(strstr(ScreenReader::g_last, "Squall's Status Screen") != nullptr,
              "and a character-named row uses the name the player gave them");

        // Only 4, 7 and 27 speak in this module.
        int spoke = 0;
        for (int st = 0; st < 34; st++) {
            if (st == TUT_STATE_LIST || st == TUT_STATE_TESTPICK ||
                st == TUT_STATE_HELPLIST) continue;
            ResetTutorialMenu();
            *(uint16_t*)(t2 + TUTO_STATE) = (uint16_t)st;
            ScreenReader::g_last[0] = '\0';
            PollTutorialMenu();
            if (ScreenReader::g_last[0]) {
                spoke++; printf("  BAD: tutorial state %d spoke \"%s\"\n", st, ScreenReader::g_last);
            }
        }
        bad += spoke;
        check(spoke == 0, "of 34 tutorial states only 4, 7 and 27 speak");
    }
    printf("online help: a second panel in the same module, its rows mapped "
           "through the progress filter, and the demos declared unspoken\n");

    // ---- Information -------------------------------------------------------
    //
    // The browser's links never reach the drawn body -- 0x004D6B20 diverts each
    // 0x0B into a position array and copies its LABEL through as ordinary text.
    // The pen advances 0x10 per line break, so penY / 0x10 is the label's line,
    // and **no page in the 425-record corpus puts two links on one line**, which
    // is what makes "the link's line IS its label" safe.
    {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        uint8_t* t3 = (uint8_t*)(MM_POOL_BASE + 4 * 0x78);
        *(uint32_t*)(t3 + 0x08) = TUT_UPDATE_FN;      // still in the pool underneath
        *(uint16_t*)(t3 + TUTO_STATE) = TUT_STATE_LIST;
        uint8_t* tp = (uint8_t*)(MM_POOL_BASE + 7 * 0x78);
        *(uint32_t*)(tp + 0x08) = TIPS_UPDATE_FN;
        *head = (uint8_t*)MM_POOL_BASE;
        menuState[JUNC_ACTIVE_OFFSET] = TIPS_SUBSYSTEM_ID;

        // A link page, the shape of record 0: one label per line, no prose.
        unsigned char t[512];
        Ff8Encode("Select term", t, sizeof(t));
        memcpy((void*)TIPS_TITLE_BUF, t, 64);
        int f = 0;
        unsigned char body[512];
        f  = Ff8Encode("Basic Terms", body, sizeof(body)) - 1; body[f++] = 0x02;
        f += Ff8Encode("Elemental",  body + f, (int)sizeof(body) - f) - 1; body[f++] = 0x02;
        f += Ff8Encode("Status",     body + f, (int)sizeof(body) - f) - 1; body[f++] = 0x02;
        f += Ff8Encode("Menu",       body + f, (int)sizeof(body) - f) - 1; body[f++] = 0x00;
        memset((void*)TIPS_BODY_BUF, 0, 0x400);
        memcpy((void*)TIPS_BODY_BUF, body, (size_t)f);
        *(uint16_t*)TIPS_LINK_CNT = 4;
        memset((void*)TIPS_LINK_POS, 0, 64);
        for (int k = 0; k < 4; k++)
            *(uint16_t*)(TIPS_LINK_POS + k * 8 + 2) = (uint16_t)(k * TIPS_LINE_H);
        *(uint16_t*)TIPS_PARENT   = 0xFFFF;   // the root
        *(uint16_t*)TIPS_PREVPAGE = 0xFFFF;
        *(uint16_t*)TIPS_NEXTPAGE = 0xFFFF;
        *(uint16_t*)(tp + MAGO_TIPS_RECORD) = 0;
        *(uint8_t*)(TIPS_CURSORS + 0) = 0;
        *(uint16_t*)(tp + MAGO_STATE) = TIPS_STATE_PAGE;

        ResetTutorialMenu();
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last, "Select term. 4 topics. Basic Terms, 1 of 4",
               "a link page: title, how many topics, and the one under the cursor");
        check(strstr(ScreenReader::g_last, "Elemental") == nullptr,
              "the other links are NOT read on arrival -- ten of them would be a wall");

        // Moving says the link alone. The cursor is stored PER RECORD by the
        // game, so this reads it back from that table rather than tracking it.
        *(uint8_t*)(TIPS_CURSORS + 0) = 2;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last, "Status, 3 of 4", "moving reads the link alone");

        g_key = '2';
        ScreenReader::g_last[0] = '\0';
        TutorialNumberKeys();
        expect(ScreenReader::g_last,
               "4 topics. Basic Terms, Elemental, Status, Menu",
               "key 2 lists every topic -- what a sighted player takes in at a glance");
        g_key = '1';
        ScreenReader::g_last[0] = '\0';
        TutorialNumberKeys();
        check(strstr(ScreenReader::g_last, "Topic 3 of 4") != nullptr &&
              strstr(ScreenReader::g_last, "Cancel leaves Information") != nullptr,
              "key 1 gives the position and, at the root, that Cancel leaves entirely");
        g_key = -1;

        // A prose page with no links at all, and siblings either side.
        Ff8Encode("Status/About Status", t, sizeof(t));
        memcpy((void*)TIPS_TITLE_BUF, t, 64);
        f  = Ff8Encode("Status signifies status effects,", body, sizeof(body)) - 1;
        body[f++] = 0x02;
        f += Ff8Encode("such as Poison and Petrify.", body + f, (int)sizeof(body) - f) - 1;
        body[f++] = 0x00;
        memset((void*)TIPS_BODY_BUF, 0, 0x400);
        memcpy((void*)TIPS_BODY_BUF, body, (size_t)f);
        *(uint16_t*)TIPS_LINK_CNT = 0;
        *(uint16_t*)TIPS_PARENT   = 273;
        *(uint16_t*)TIPS_NEXTPAGE = 275;
        *(uint16_t*)(tp + MAGO_TIPS_RECORD) = 274;
        ScreenReader::g_last[0] = '\0';
        PollTutorialMenu();
        expect(ScreenReader::g_last,
               "Status/About Status. Status signifies status effects, such as Poison "
               "and Petrify. More pages: left and right",
               "a prose page reads whole, and says its siblings exist without a "
               "doubled stop");

        g_key = '1';
        ScreenReader::g_last[0] = '\0';
        TutorialNumberKeys();
        check(strstr(ScreenReader::g_last, "Cancel goes up a level") != nullptr &&
              strstr(ScreenReader::g_last, "Right for the next page") != nullptr &&
              strstr(ScreenReader::g_last, "Topic") == nullptr,
              "and off the root Cancel climbs instead, with no topic count to give");
        g_key = -1;

        // Only state 7 speaks, of 22.
        int spoke = 0;
        for (int st = 0; st < 22; st++) {
            if (st == TIPS_STATE_PAGE) continue;
            ResetTutorialMenu();
            *(uint16_t*)(tp + MAGO_STATE) = (uint16_t)st;
            ScreenReader::g_last[0] = '\0';
            PollTutorialMenu();
            if (ScreenReader::g_last[0]) {
                spoke++; printf("  BAD: tips state %d spoke \"%s\"\n", st, ScreenReader::g_last);
            }
        }
        bad += spoke;
        check(spoke == 0, "no Information state but 7 speaks");
        check(strstr(ScreenReader::g_last, "Battle Operation") == nullptr,
              "and the Tutorial module underneath never speaks over the page");
        menuState[JUNC_ACTIVE_OFFSET] = TUTORIAL_SUBSYSTEM_ID;
    }
    printf("information: link pages, prose pages, the per-record cursor, and the "
           "whole topic list on one key\n");

    // =====================================================================
    // THE ITEM MENU'S MAGAZINE  (v0.30.2)
    // ---------------------------------------------------------------------
    // "Still nothing reads in the magazine" -- twice. v0.30.0 hoisted a gate on
    // a module that was never involved; v0.30.1 read the right data through an
    // identification that returns null. Neither version's decode was ever
    // executed by anything. This runs it.
    //
    // The layout, from 0x004FB60A / 0x004FCAA0 / 0x004FD746:
    //   [0x01D2BB2C] + magId*4    ->  {?, ?, firstPage, lastPage}
    //   module +0x65 magId, +0x52 page, +0x10 state (0x51..0x59, 0x55 steady)
    //   [0x01D2BB6C] + page*68    ->  the record; +0x34 = 4 x {u16,u8,u8 strIdx}
    //   [0x00B86D30] + 0x1F000    ->  u16 count, u16 offsets[], then the strings
    // =====================================================================
    {
        // 0x01D2B000 and 0x01D7D000/0x01D83000 are already mapped above; only
        // ask for what is not. Overlapping MAP_FIXED_NOREPLACE calls fail.
        const bool mapped = MapAt(0x00B86000, 0x2000) && MapAt(0x01E00000, 0x30000);
        if (!mapped) {
            printf("  (could not map the magazine globals -- skipped)\n");
        } else {
            uint8_t* arena = (uint8_t*)0x01E00000;
            memset(arena, 0, 0x30000);

            // The range table. These are the REAL records: Weapons Monthly
            // April is mmag.bin records 8..11, and record 9 is the Maverick --
            // the page in Aaron's v0.30.1 screenshot. Using the real numbers is
            // what lets the art lookup below be a test of the KEYING and not
            // just of the plumbing.
            uint8_t* range = arena;
            range[3 * 4 + 2] = 8;
            range[3 * 4 + 3] = 11;
            *(uint8_t**)0x01D2BB2C = range;

            // The string section: count, offsets, then NUL-terminated text.
            uint8_t* sec = arena + 0x1000;
            *(uint16_t*)sec = 3;
            const char* strs[3] = { "Weapons Monthly April Issue",
                                    "2/4",
                                    "With the Maverick, the combatant can deliver direct punching blows to the enemy." };
            uint16_t off = (uint16_t)(2 + 3 * 2);
            for (int i = 0; i < 3; i++) {
                *(uint16_t*)(sec + 2 + i * 2) = off;
                unsigned char enc[256];
                const int n = Ff8Encode(strs[i], enc, sizeof(enc));
                memcpy(sec + off, enc, (size_t)n);
                off = (uint16_t)(off + n);
            }
            uint8_t* archive = arena + 0x2000;
            *(uint8_t**)0x00B86D30 = archive;
            memcpy(archive + 0x1F000, sec, 0x400);

            // mwepon.bin: 33 records of 12 bytes, item pairs at +0x04. Weapon 8
            // is the Maverick and really does read {155 x1, 127 x1}.
            uint8_t* wep = arena + 0xC000;
            memset(wep, 0, 33 * 12);
            wep[8 * 12 + 4] = 155; wep[8 * 12 + 5] = 1;
            wep[8 * 12 + 6] = 127; wep[8 * 12 + 7] = 1;
            wep[1 * 12 + 4] = 0x6D; wep[1 * 12 + 5] = 6;   // one-item weapon
            *(uint8_t**)IMAG_WEPON_PTR = wep;

            // The records. Page 11 carries all three blocks; the list ends 0xFF.
            uint8_t* recs = arena + 0x8000;
            *(uint8_t**)0x01D2BB6C = recs;
            for (int r = 8; r <= 11; r++) {
                uint8_t* rec = recs + r * MAG_REC_SIZE;
                memset(rec + MAG_TEXTBLK, 0xFF, 16);
                rec[MAG_TEXTBLK + 0 * 4 + 3] = 0;
                rec[MAG_TEXTBLK + 1 * 4 + 3] = 1;
                rec[MAG_TEXTBLK + 2 * 4 + 3] = 2;
                rec[IMAG_REC_WEAPON] = 0xFF;            // no weapon by default
            }
            recs[9 * MAG_REC_SIZE + IMAG_REC_WEAPON] = 8;   // the Maverick
            recs[8 * MAG_REC_SIZE + IMAG_REC_WEAPON] = 1;   // a one-item weapon

            static uint8_t itemMod[0x78];
            memset(itemMod, 0, sizeof(itemMod));
            g_itemModule = itemMod;
            itemMod[IMAGO_ID]   = 3;
            itemMod[IMAGO_PAGE] = 9;

            // The opening states must NOT speak -- they are an animation, and a
            // page spoken while it slides is spoken again when it lands.
            s_imagActive = false; s_imagPage = -999;
            for (int st = 0x51; st <= 0x54; st++) {
                *(uint16_t*)(itemMod + IMAGO_STATE) = (uint16_t)st;
                ScreenReader::g_last[0] = '\0';
                check(PollItemMagazine(), "an opening state is still handled");
                check(ScreenReader::g_last[0] == '\0', "but must not speak");
            }

            // The reading state speaks the page, headline first.
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x55;
            ScreenReader::g_last[0] = '\0';
            check(PollItemMagazine(), "the reading state is handled");
            check(strncmp(ScreenReader::g_last, "Weapons Monthly April Issue", 27) == 0,
                  "the headline must be spoken first");
            check(strstr(ScreenReader::g_last, "punching blows") != nullptr,
                  "and the body prose with it -- the part the GCW buffer does not hold");
            check(strstr(ScreenReader::g_last, "2/4") != nullptr,
                  "the page counter is part of the stored text, not added");

            // **The remodeling list.** The page's own text stops at the heading;
            // v0.31.0 read the heading and then fell silent, which is the
            // v0.28.1 empty-page failure again -- and on a Weapons Monthly this
            // list is the practical point of the page.
            check(strstr(ScreenReader::g_last, "Dragon Fin, 1.") != nullptr,
                  "the first remodeling item must be read with its count");
            check(strstr(ScreenReader::g_last, "Spider Web, 1.") != nullptr,
                  "and so must the second");
            {
                const char* a = strstr(ScreenReader::g_last, "Dragon Fin");
                const char* b = strstr(ScreenReader::g_last, "Spider Web");
                check(a && b && a < b, "in the order the panel lists them");
            }
            check(strstr(ScreenReader::g_last, "Last page") == nullptr,
                  "page 2 of 4 is not the last page");

            // Holding still says nothing.
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(ScreenReader::g_last[0] == '\0', "sitting on a page must stay silent");

            // A page slide and back must re-speak the new page, once.
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x58;
            PollItemMagazine();
            itemMod[IMAGO_PAGE] = 10;
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x55;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(ScreenReader::g_last[0] != '\0', "a new page speaks");
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(ScreenReader::g_last[0] == '\0', "and only once");

            // A weapon needing one item lists one, not four.
            itemMod[IMAGO_PAGE] = 8;
            s_imagPage = -999;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(strstr(ScreenReader::g_last, "Screw, 6.") != nullptr,
                  "a one-item weapon reads its single requirement");
            check(strstr(ScreenReader::g_last, "Unknown item") == nullptr,
                  "and does not run past the end of its list");

            // A page with no weapon id must not borrow another weapon's list.
            itemMod[IMAGO_PAGE] = 10;
            s_imagPage = -999;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(strstr(ScreenReader::g_last, "Dragon Fin") == nullptr &&
                  strstr(ScreenReader::g_last, "Screw") == nullptr,
                  "a page with no weapon id gets no remodeling list");

            // The last page says so; nothing on screen does.
            itemMod[IMAGO_PAGE] = 11;
            ScreenReader::g_last[0] = '\0';
            PollItemMagazine();
            check(strstr(ScreenReader::g_last, "Last page") != nullptr,
                  "the last page must say so");

            // A page outside the magazine's range is refused rather than read
            // out of a neighbouring magazine's records.
            itemMod[IMAGO_PAGE] = 20;
            ScreenReader::g_last[0] = '\0';
            check(!PollItemMagazine(), "a page outside the range is refused");

            // Leaving the magazine states hands the frame back.
            itemMod[IMAGO_PAGE] = 9;
            *(uint16_t*)(itemMod + IMAGO_STATE) = 0x0E;
            check(!PollItemMagazine(), "outside 0x51..0x59 the poll declines");
            g_itemModule = nullptr;
            check(!PollItemMagazine(), "with no Item module the poll declines");

            // --- "/" describes the picture (v0.31.0) --------------------
            // The table is hand-written from the art, so what a probe can hold
            // it to is that every entry EXISTS, is non-empty, is not a
            // duplicate of another, and is keyed to the right page -- and that
            // the key falls through on pages that have no weapon plate.
            {
                g_itemModule = itemMod;               // the block above cleared it
                itemMod[IMAGO_PAGE] = 9;
                *(uint16_t*)(itemMod + IMAGO_STATE) = 0x55;
                s_imagActive = false; s_imagPage = -999; s_imagArtRec = -1;
                PollItemMagazine();
                ScreenReader::g_last[0] = '\0';
                check(MagazineSpeakArt(), "the key is claimed on a weapon page");
                check(strncmp(ScreenReader::g_last, "Maverick.", 9) == 0,
                      "record 9 is the Maverick -- the page in Aaron's own screenshot");
                check(strstr(ScreenReader::g_last, "glove") != nullptr,
                      "and its description is of gloves, as its prose says");

                // Every entry present, non-trivial and distinct.
                for (int r = 0; r < MAG_ART_COUNT; r++) {
                    if (!MAG_ART[r].name || !MAG_ART[r].name[0]) {
                        bad++; printf("  BAD: art %d has no name\n", r); continue;
                    }
                    if (!MAG_ART[r].look || strlen(MAG_ART[r].look) < 40) {
                        bad++; printf("  BAD: art %d (%s) has no real description\n",
                                      r, MAG_ART[r].name);
                    }
                    for (int q = 0; q < r; q++)
                        if (strcmp(MAG_ART[r].look, MAG_ART[q].look) == 0) {
                            bad++;
                            printf("  BAD: art %d (%s) repeats art %d (%s)\n",
                                   r, MAG_ART[r].name, q, MAG_ART[q].name);
                        }
                }

                // The three weapons whose SHAPE would be misread without the
                // page prose -- Aaron's own example among them.
                check(strstr(MAG_ART[15].look, "whip") != nullptr,
                      "Slaying Tail must be called a whip, not a tail");
                check(strstr(MAG_ART[3].look, "whip") != nullptr,
                      "Strange Vision must be called a whip");
                check(strstr(MAG_ART[19].look, "whip") != nullptr,
                      "Red Scorpion must be called a whip");

                // A page with no weapon plate must not borrow one.
                s_imagActive = true; s_imagArtRec = 40;
                ScreenReader::g_last[0] = '\0';
                check(MagazineSpeakArt(), "the key is still claimed on other magazines");
                check(strstr(ScreenReader::g_last, "No picture description") != nullptr,
                      "but it says there is no plate rather than describing one");

                // Off the magazine entirely, the key must fall through.
                s_imagActive = false; s_imagArtRec = -1;
                check(!MagazineSpeakArt(), "off the magazine the key falls through");
                printf("magazine art: 28 distinct plates, keyed to the right page, "
                       "the three whips named as whips, and no plate invented\n");
            }

            printf("item magazine: the page decoded from the record and spoken once, "
                   "openers silent, slides re-speak, last page named, "
                   "out-of-range refused\n");
        }
    }

    printf("menu_tutorial_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
