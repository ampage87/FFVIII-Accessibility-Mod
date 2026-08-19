// menu_ability_compile.cpp -- v0.29.0 (#88)
//
// Compile-and-run probe for src/menu_tts_ability.inl's ability-list reader.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_ability_compile tests/menu_ability_compile.cpp
//
// WHY THIS EXISTS
//
// The #88 audit found two defects in the same list, and they hid each other.
//
//   * ABIL_MENU_ID_LO was 97. The engine's own guard at 0x004C2B40 accepts
//     menu ids in [0x5C, 0x74) -- that is 92..115 -- so five real abilities at
//     the bottom of the range were rejected as "not an ability" and read out as
//     nothing.
//   * The list was parsed out of the GCW draw buffer, which holds only the
//     ELEVEN ROWS CURRENTLY DRAWN, while the cursor at +0x258 is an index into
//     the WHOLE list. On any GF with more than eleven abilities the mod named
//     the wrong ability from the twelfth row on -- and named it confidently.
//     0x004E770F reads the flat list from 0x01D8CB54 with its length at
//     0x01D8CB6C; that is what the mod now reads.
//
// Naming the wrong ability is worse than naming none: the player junctions or
// spends AP on something he did not choose, and nothing on screen contradicts
// him afterwards.

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
#undef __catch
#undef __throw_exception_again
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1

// Advances on every call so the 80 ms poll gate and the 400 ms settle dwell
// both open -- a stub pinned at 0 makes PollAbilityItemList() return before it
// does anything, which would make the end-to-end test below vacuous.
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
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) s += (char)(d[i] + 0x20);
        return s;
    }
    std::string Decode(const uint8_t* d, size_t n = 1024) {
        std::string s;
        for (size_t i = 0; i < n; i++) {
            if (d[i] == 0x00) break;
            s += (d[i] == 0x01 || d[i] == 0x02) ? ' ' : (char)d[i];
        }
        return s;
    }
}
namespace FieldDialog { int SnapshotGcwBuffer(uint8_t*, size_t) { return 0; } }

static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevCursor = 5;
// The real savemap addresses, not zeros. The v0.32.1 preview cut reads the
// item inventory to learn which source rows are drawn, so a probe that stubbed
// these to 0 would map page zero and silently exercise nothing -- the same
// mistake as stubbing FindItemModule() to null in the magazine probe.
static const uintptr_t SAVEMAP_BASE = 0x1CFDC5C;
static const int ITEM_INVENTORY_OFFSET = 0x0B40;

// Names and helpers the ability reader borrows from its host translation unit.
// This probe exercises the LIST, not the naming, so these are the thinnest
// stubs that let the file compile; the names themselves are pinned in
// tests/menu_sim.cpp against the game's own tables.
// The real spell table and the real party markers, not placeholders: the
// v0.32.1 preview cut matches the refine RESULT against these, so a three-name
// stub would have let a broken cut pass.
#include "menu_magic_model.inl"
static const char* HELP_END_MARKERS[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie",
    "Seifer", "Edea", "Laguna", "Kiros", "Ward",
    nullptr
};
static const std::vector<std::string>& GcwAbilityNames()
{ static const std::vector<std::string> v(24); return v; }
#include "ff8_item_names.h"
static const char* GetItemName(int id)
{
    if (id > 0 && id < FF8_ITEM_COUNT && FF8_ITEM_NAMES[id]) return FF8_ITEM_NAMES[id];
    return "";
}
static const char* GetAbilityName(int) { return "Ability"; }

#include "menu_refine_model.inl"
#include "menu_card_data.inl"
#include "menu_tts_ability.inl"

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

int main()
{
    printf("menu_tts_ability.inl compiles\n");

    // The id window the engine itself enforces at 0x004C2B40: cmp eax, 0x5C /
    // jb reject ; cmp eax, 0x74 / jae reject.
    check(ABIL_MENU_ID_LO == 92, "the low id must match the engine's 0x5C, not 97");
    check(ABIL_MENU_ID_HI >= 115, "the high id must reach the engine's 0x73");

    if (!MapAt(ABIL_ENGINE_IDS, (size_t)(ABIL_ENGINE_COUNT + 8 - ABIL_ENGINE_IDS))) {
        printf("  (could not map the engine list -- skipping the live checks)\n");
        printf("menu_ability_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }
    // Map once. MAP_FIXED_NOREPLACE refuses a second, overlapping call, so every
    // later block reuses these rather than mapping its own window.
    const bool havePool    = MapAt(ABIL_LIST_HEAD, (size_t)(ABIL_POOL_END - ABIL_LIST_HEAD)) != nullptr;
    const bool haveSavemap = MapAt(SAVEMAP_BASE, 0x2000) != nullptr;   // savemap + character records
    const bool haveCards   = MapAt(RF_CARD_LIST, 0x200) != nullptr;
    // The recipe table must live at a 32-bit address: the module holds it in a
    // u32 field, so a static array in this 64-bit host would be truncated to
    // garbage the moment the view read it back.
    const uintptr_t RECIPE_FIXTURE = 0x01D90000;
    const bool haveRecipeMem = MapAt(RECIPE_FIXTURE, 0x200) != nullptr;

    uint8_t* ids = (uint8_t*)ABIL_ENGINE_IDS;
    uint8_t* cnt = (uint8_t*)ABIL_ENGINE_COUNT;

    uint8_t out[64]; int n = 0;

    // A GF with more abilities than the eleven drawn rows -- the case the GCW
    // parse got wrong. Every entry must come back, in order.
    *cnt = 20;
    for (int i = 0; i < 20; i++) ids[i] = (uint8_t)(92 + i);
    check(AbilReadEngineList(out, 64, n), "a 20-entry list must be read");
    check(n == 20, "all 20 entries must be returned, not the 11 drawn rows");
    bool ordered = true;
    for (int i = 0; i < n; i++) if (out[i] != 92 + i) ordered = false;
    check(ordered, "the engine list must come back in engine order");

    // The bottom five ids the old 97 floor threw away.
    *cnt = 5;
    for (int i = 0; i < 5; i++) ids[i] = (uint8_t)(92 + i);
    check(AbilReadEngineList(out, 64, n) && n == 5,
          "ids 92..96 must be accepted, not rejected as non-abilities");

    // Rejections. A list the reader cannot trust must be declined outright so
    // the caller falls back, rather than half-read and announced as fact.
    *cnt = 0;
    check(!AbilReadEngineList(out, 64, n) && n == 0, "an empty list must be declined");
    *cnt = 25;
    check(!AbilReadEngineList(out, 64, n), "a count past the 24-slot cap must be declined");
    *cnt = 3; ids[0] = 92; ids[1] = 200; ids[2] = 94;
    check(!AbilReadEngineList(out, 64, n),
          "an out-of-range id must decline the whole list, not be skipped");
    *cnt = 3; ids[0] = 92; ids[1] = 91; ids[2] = 94;
    check(!AbilReadEngineList(out, 64, n), "an id below 92 must decline the list");
    *cnt = 3; ids[0] = 92; ids[1] = 116; ids[2] = 94;
    check(!AbilReadEngineList(out, 64, n), "an id above 115 must decline the list");

    // The output cap must clamp rather than overrun the caller's array.
    *cnt = 20;
    for (int i = 0; i < 20; i++) ids[i] = (uint8_t)(92 + i);
    uint8_t small[8];
    memset(small, 0xEE, sizeof(small));
    check(AbilReadEngineList(small, 8, n) && n == 8, "the output cap must clamp the count");
    printf("engine list: full list past the drawn rows, ids 92..115, "
           "declines anything it cannot trust\n");

    // =====================================================================
    // THE REFINE SCREEN'S IDENTIFICATION AND ITS SOURCE LIST  (v0.32.0)
    // ---------------------------------------------------------------------
    // The refine screen is a MODULE OF ITS OWN (dispatch 19, update fn
    // 0x004D7410) that the Ability screen pushes -- 0x004E7990 branches on the
    // ability's type byte and only the "else" case comes here. So this file has
    // always been driving a module it does not own, through pool slot 3.
    // =====================================================================
    {
        if (!havePool) {
            printf("  (could not map the pool -- skipping the refine checks)\n");
        } else {
            uint8_t** head = (uint8_t**)ABIL_LIST_HEAD;
            uint8_t* slot3 = (uint8_t*)ABIL_SLOT3_ALIAS;

            // Nothing in the pool and slot 3 not in use -> no base, no reading.
            *head = nullptr;
            memset(slot3, 0, 0x78);
            check(AbilRefineBase() == nullptr, "an empty pool yields no refine base");

            // The walk wins when it finds the refine module.
            for (int place = 0; place < 10; place++) {
                for (int i = 0; i < 10; i++) {
                    uint8_t* m = (uint8_t*)(ABIL_POOL_BASE + i * 0x78);
                    memset(m, 0, 0x78);
                    *(uint32_t*)(m + 0x08) = (i == place) ? ABIL_REFINE_FN : 0x004F02F0;
                    *(uint8_t**)m = (i < 9) ? (uint8_t*)(ABIL_POOL_BASE + (i + 1) * 0x78) : nullptr;
                }
                *head = (uint8_t*)ABIL_POOL_BASE;
                if (AbilRefineBase() != (uint8_t*)(ABIL_POOL_BASE + place * 0x78)) {
                    bad++; printf("  BAD: refine module not found in slot %d\n", place);
                }
            }

            // **The walk failing is not hypothetical** -- on the Item module it
            // provably cannot succeed, so the alias has to work, and it has to
            // be a test rather than an assumption.
            *head = nullptr;
            memset(slot3, 0, 0x78);
            slot3[0x12] = 1;                       // in use
            *(uint16_t*)(slot3 + 0x10) = 0x1C;     // a real refine state
            check(AbilRefineBase() == slot3, "with no walk result the in-use slot 3 answers");
            *(uint16_t*)(slot3 + 0x10) = 0x2D;     // past the state machine's max
            check(AbilRefineBase() == nullptr, "a state past 0x2C is refused");
            *(uint16_t*)(slot3 + 0x10) = 0x1C;
            slot3[0x12] = 0;                       // not in use
            check(AbilRefineBase() == nullptr, "a free slot 3 is refused");

            // Cyclic and misaligned lists must still terminate.
            {
                uint8_t* a = (uint8_t*)ABIL_POOL_BASE;
                uint8_t* b = (uint8_t*)(ABIL_POOL_BASE + 0x78);
                memset(a, 0, 0x78); memset(b, 0, 0x78);
                *(uint32_t*)(a + 0x08) = 0x004F02F0;
                *(uint32_t*)(b + 0x08) = 0x004F02F0;
                *(uint8_t**)a = b; *(uint8_t**)b = a;
                *head = a;
                memset(slot3, 0, 0x78);
                check(AbilRefineBase() == nullptr, "a cyclic list terminates and finds nothing");
            }
            printf("refine base: found by walk in all 10 slots, and the in-use "
                   "state-checked slot 3 answers when the walk cannot\n");
        }
    }

    // --- the chosen ability id -------------------------------------------
    // v0.32.0 used this to suppress Card Mod's row names. v0.33.0 names them
    // from the engine's own list instead, so what is left to assert is that an
    // id outside the menu block is refused rather than claimed.
    {
        static uint8_t menuState[0x400];
        memset(menuState, 0, sizeof(menuState));
        pMenuStateA = (WORD*)menuState;

        for (int id = ABIL_MENU_ID_LO; id <= ABIL_MENU_ID_HI; id++) {
            menuState[ABIL_CHOSEN_ID_OFF] = (uint8_t)id;
            if (AbilChosenId() != id) {
                bad++;
                printf("  BAD: ability %d not claimed\n", id);
            }
        }
        menuState[ABIL_CHOSEN_ID_OFF] = 200;      // out of the menu block
        check(AbilChosenId() == -1, "an id outside 92..115 is not claimed");
        menuState[ABIL_CHOSEN_ID_OFF] = 91;
        check(AbilChosenId() == -1, "and neither is one below it");
        printf("chosen ability: ids 92..115 claimed, anything else refused\n");
    }

    // =====================================================================
    // THE "/" REFINE PREVIEW  (v0.32.1)
    // ---------------------------------------------------------------------
    // Both fixtures are the decoded GCW out of the v0.32.0 BAT, verbatim.
    // A magic-refine puts the party panel after the sentence, so the old
    // party-name cut worked. **An item-refine has no recipient and therefore no
    // party panel**, so nothing bounded the slice and the whole drawn source
    // list came out glued to the end of the preview.
    // =====================================================================
    if (haveSavemap) {
        uint8_t* inv = (uint8_t*)(SAVEMAP_BASE + ITEM_INVENTORY_OFFSET);
        memset(inv, 0, 512);
        // The page of source rows drawn under the Tool-RF preview, in order.
        // Coral Fragment, Betrayal Sword, Dead Spirit, Hero, Force Armlet --
        // real item ids out of src/ff8_item_names.h, not invented ones.
        const uint8_t page[] = { 128, 123, 158, 19, 84 };
        for (size_t i = 0; i < sizeof(page); i++) { inv[i*2] = page[i]; inv[i*2+1] = 1; }

        const std::string tool =
            "GFAbilitySwitchCardConfigTutorialSaveTool-RF"
            "1 will refine into 30 Shell Stones"
            "Coral FragmentBetrayal SwordDead SpiritHeroForce Armlet";
        std::string got = ParseRefinePreview(tool, 0);
        if (got != "1 will refine into 30 Shell Stones") {
            bad++;
            printf("  BAD: Tool-RF preview was \"%s\"\n", got.c_str());
        }

        // The quantity popup draws fewer rows and appends its own label; the
        // same cut has to hold.
        const std::string toolQty =
            "Tool-RF1 will refine into 30 Shell StonesForce ArmletNumber to r";
        got = ParseRefinePreview(toolQty, 0);
        if (got != "1 will refine into 30 Shell Stones") {
            bad++;
            printf("  BAD: Tool-RF quantity preview was \"%s\"\n", got.c_str());
        }

        // A result whose own name begins with a drawn row's name must survive:
        // the cut is required to leave the sentence non-empty.
        memset(inv, 0, 512);
        inv[0] = 128; inv[1] = 1;                     // Coral Fragment
        const std::string tight = "1 will refine into 3 Coral Fragments" "Coral Fragment";
        got = ParseRefinePreview(tight, 0);
        check(got == "1 will refine into 3 Coral Fragments",
              "a result that starts with a row name is not cut short");

        // The magic case, which already worked, must not regress. The party
        // panel still bounds it and the source rows are further along.
        memset(inv, 0, 512);
        inv[0] = 1; inv[1] = 34;                      // Potion
        const std::string mag =
            "GFAbilitySwitchCardConfigTutorialSaveL Mag-RF"
            "1 will refine into 10 Curagas"
            "SquallZellIrvineQuistisRinoaSelphiePotionPhoenix Down";
        got = ParseRefinePreview(mag, 0);
        if (got != "1 will refine into 10 Curagas") {
            bad++;
            printf("  BAD: L Mag-RF preview was \"%s\"\n", got.c_str());
        }

        // Ammo-RF, the second item-output recipe the v0.32.0 BAT caught. Its
        // result name is two words and the row that follows starts a new one,
        // so this is the case a naive "cut at the next capital" would miss.
        memset(inv, 0, 512);
        {
            const uint8_t rows[] = { 115, 190, 163 };   // Zombie Powder,
            // Pet Pals Vol. 2, Girl Next Door
            for (size_t i = 0; i < sizeof(rows); i++)
            { inv[i*2] = rows[i]; inv[i*2+1] = 1; }
        }
        got = ParseRefinePreview(
            "Ammo-RF100 will refine into 1 Dark Matter"
            "Zombie PowderPet Pals Vol. 2Girl Next Door", 0);
        if (got != "100 will refine into 1 Dark Matter") {
            bad++;
            printf("  BAD: Ammo-RF preview was \"%s\"\n", got.c_str());
        }

        // A result the tables do not know falls back to the layout bound
        // rather than to nothing -- the fallback path has to stay live.
        memset(inv, 0, 512);
        inv[0] = 7; inv[1] = 3;                       // Phoenix Down
        got = ParseRefinePreview(
            "1 will refine into 5 Widgetsprocket" "Phoenix Down", 0);
        check(got == "1 will refine into 5 Widgetsprocket",
              "an unknown result name still yields a preview via the layout bound");

        // No preview at all when the item cannot be refined.
        check(ParseRefinePreview("GFAbilitySwitchCardConfigTutorialSaveTool-RF"
                                 "PotionPhoenix Down", 0).empty(),
              "no sentence means no preview");
        printf("refine preview: cut at the end of the result's own name on both "
               "item and magic recipes, with the party panel and the drawn source "
               "rows as the fallback bound\n");
    }

    // =====================================================================
    // THE FIVE SUB-MODES AND WHICH SCREEN IS UP  (v0.33.0)
    // ---------------------------------------------------------------------
    // The v0.32.1 BAT produced two failures with one cause: the flow was
    // modelled as sub-mode 0's flow. Tool-RF (1) reaches the quantity popup
    // without a recipient and Mid Mag-RF (2) reaches a magic grid without an
    // item list, so the marker that gated everything -- +0x53, cleared at state
    // 0x1A, which only sub-mode 0 enters -- never opened.
    // =====================================================================
    {
        check(RefineSourceKindOf(RF_SUB_ITEM_TO_MAGIC)  == RF_SRC_ITEMS, "sub-mode 0 sources items");
        check(RefineSourceKindOf(RF_SUB_ITEM_TO_ITEM)   == RF_SRC_ITEMS, "sub-mode 1 sources items");
        check(RefineSourceKindOf(RF_SUB_MAGIC_TO_MAGIC) == RF_SRC_MAGIC,
              "sub-mode 2 sources a CHARACTER'S MAGIC -- 0x004D8CDD, not the inventory");
        check(RefineSourceKindOf(RF_SUB_ITEM_TO_MED)    == RF_SRC_ITEMS, "sub-mode 3 sources items");
        check(RefineSourceKindOf(RF_SUB_CARD_TO_ITEM)   == RF_SRC_CARDS,
              "sub-mode 4 sources the card list the creator builds at 0x004D7344");

        check(RefineResultIsMagic(RF_SUB_ITEM_TO_MAGIC),  "sub-mode 0 grants magic (0x004C2D20)");
        check(!RefineResultIsMagic(RF_SUB_ITEM_TO_ITEM),  "sub-mode 1 grants an item (0x0047ED00)");
        check(RefineResultIsMagic(RF_SUB_MAGIC_TO_MAGIC), "sub-mode 2 grants magic");
        check(!RefineResultIsMagic(RF_SUB_ITEM_TO_MED),   "sub-mode 3 grants an item");
        check(!RefineResultIsMagic(RF_SUB_CARD_TO_ITEM),  "sub-mode 4 grants an item");

        // **Tool-RF: the popup that said nothing.** Sub-mode 1 never enters
        // state 0x1A, so nothing about a recipient may gate this.
        check(RefinePhaseOf(RF_ST_QTY, RF_SUB_ITEM_TO_ITEM, 1) == RF_PHASE_QUANTITY,
              "Tool-RF's quantity popup is the quantity popup");
        check(RefinePhaseOf(RF_ST_QTY_ARM, RF_SUB_ITEM_TO_ITEM, 0) == RF_PHASE_QUANTITY,
              "the arming state counts as the popup, so the first frame is not missed");
        check(RefinePhaseOf(0x1C, RF_SUB_ITEM_TO_ITEM, 1) == RF_PHASE_QUANTITY,
              "the engine's own +0x51 flag wins while the states animate");
        for (int st = 0; st <= RF_ST_MAX; st++)
            if (st != RF_ST_QTY && st != RF_ST_QTY_ARM)
                check(RefinePhaseOf(st, RF_SUB_ITEM_TO_ITEM, 0) == RF_PHASE_SOURCE,
                      "sub-mode 1 has exactly two screens: the item list and the popup");

        // **Mid Mag-RF: the character list read out as items.** The picker comes
        // FIRST here (0x004D83C6 resolves the character before the source list
        // exists), and its cursor is +0x49, not +0x4A.
        check(RefinePhaseOf(9, RF_SUB_MAGIC_TO_MAGIC, 0) == RF_PHASE_CHARSRC,
              "sub-mode 2 opens on 'whose magic', not on an item list");
        check(RefineCursorOffsetFor(RF_PHASE_CHARSRC) == 0x49,
              "that picker runs on +0x49 (0x004D7F0C), the SOURCE cursor byte");
        for (int st = RF_ST_GRID_LO; st <= RF_ST_GRID_HI; st++)
            check(RefinePhaseOf(st, RF_SUB_MAGIC_TO_MAGIC, 0) == RF_PHASE_MAGICGRID,
                  "states 0x1D..0x24 are that character's spell grid");
        check(RefineCursorOffsetFor(RF_PHASE_MAGICGRID) == 0x4A,
              "the grid runs on +0x4A (0x004D766A), the OTHER cursor byte");
        check(RefinePhaseOf(RF_ST_BACK_LO, RF_SUB_MAGIC_TO_MAGIC, 0) == RF_PHASE_MAGICGRID,
              "cancelling inside the grid stays in the grid (0x004D8914 -> state 0x20)");

        // Sub-mode 0 is unchanged: the picker is the RECIPIENT and comes last.
        for (int st = RF_ST_RECIP_LO; st <= RF_ST_RECIP_HI; st++)
            check(RefinePhaseOf(st, RF_SUB_ITEM_TO_MAGIC, 0) == RF_PHASE_RECIPIENT,
                  "sub-mode 0's picker is states 0x1A..0x1C");
        check(RefineCursorOffsetFor(RF_PHASE_RECIPIENT) == 0x4A, "and it runs on +0x4A");
        check(RefinePhaseOf(0x0D, RF_SUB_ITEM_TO_MAGIC, 0) == RF_PHASE_SOURCE,
              "before that it is the item list");
        // The states sub-mode 2 uses for its grid must NOT read as a picker on
        // sub-mode 0, and vice versa -- that swap is the whole defect.
        check(RefinePhaseOf(RF_ST_GRID_LO, RF_SUB_ITEM_TO_MAGIC, 0) != RF_PHASE_MAGICGRID,
              "sub-mode 0 has no magic grid");
        check(RefinePhaseOf(RF_ST_RECIP_LO, RF_SUB_MAGIC_TO_MAGIC, 0) != RF_PHASE_RECIPIENT,
              "sub-mode 2 has no recipient picker");

        // Card Mod's rows are cards, and now they have names.
        check(RefinePhaseOf(0x0D, RF_SUB_CARD_TO_ITEM, 0) == RF_PHASE_SOURCE,
              "Card Mod browses a source list like any other");
        printf("sub-modes: five source/result pairs, and the screen up is decided by "
               "the engine's state + its own +0x51, not by sub-mode 0's marker\n");
    }

    // =====================================================================
    // THE QUANTITY POPUP'S ARITHMETIC  (v0.33.0)
    // ---------------------------------------------------------------------
    // Numbers straight from the recipe entry the engine multiplies at state
    // 0x2A (0x004D7A4C..0x004D7A74), so the three rows the screen draws are the
    // three numbers spoken. The screenshot's own case is the first fixture.
    // =====================================================================
    {
        // f11_151810_436.png: Force Armlet:1 will refine into 30 Shell Stones,
        // Force Armlet 0 / Number to refine 1 / Shell Stone 30. Aaron owns 1.
        RefineRecipe toolRf = { 30, 0, 84, 1, 22 };   // 30 Shell Stone per 1 Force Armlet
        RefineQtyLine q = RefineQtyMath(toolRf, 1, 1, 0);
        check(q.produced == 30,  "one Force Armlet makes thirty Shell Stones");
        check(q.consumed == 1,   "and consumes one");
        check(q.remaining == 0,  "leaving none -- the screen's own 0");

        q = RefineQtyMath(toolRf, 3, 5, 0);
        check(q.produced == 90 && q.consumed == 3 && q.remaining == 2,
              "three of five leaves two and makes ninety");

        // A recipe that eats more than one per unit must not be assumed 1:1.
        RefineRecipe midMag = { 1, 0, 21, 5, 22 };    // 5 Cure -> 1 Cura (the BAT's own)
        q = RefineQtyMath(midMag, 16, 84, 71);
        check(q.produced == 16 && q.consumed == 80 && q.remaining == 4,
              "the per-unit source count is multiplied too, not assumed to be one");

        // **The screen's third row is the resulting STOCK.** Aaron's screenshot
        // reads "Cure 4 / Number to refine 16 / Cura 87" -- 87, not 16, because
        // he already held 71. Saying only the produced count hides the cap.
        check(q.total == 87, "the third row is stock + produced, exactly as the screenshot reads");
        q = RefineQtyMath(midMag, 16, 84, 90);
        check(q.total == RF_STOCK_CAP,
              "and it clamps at 100 -- 0x004C2CCC for magic, 0x0047ED54 for items");
        q = RefineQtyMath(toolRf, 2, 2, -1);
        check(q.total == -1, "an unreadable stock says nothing rather than guessing zero");

        // Never speak a negative remainder if the count and the owned amount
        // disagree for a frame while the popup opens.
        q = RefineQtyMath(toolRf, 9, 1, 0);
        check(q.remaining == 0, "a transient over-count clamps at zero rather than going negative");

        // **The pre-write frame.** State 0x28 is where the engine writes +0x4C
        // and +0x4F, and the v0.33.0 BAT caught the mod reading them inside it:
        //   "Number to refine 0, makes 0 Death Stone, 2 Dead Spirit left"
        // a beat before the real "1 of 2". The popup must stay silent until the
        // engine has filled it in.
        check(!RefineQtyReady(0, 0), "the pre-write frame is not spoken");
        check(!RefineQtyReady(0, 2), "nor a zero count with a real max");
        check(!RefineQtyReady(1, 0), "nor a real count with a zero max");
        check(RefineQtyReady(1, 2),  "the first real frame is");
        check(RefinePhaseOf(RF_ST_QTY_ARM, RF_SUB_ITEM_TO_ITEM, 0) == RF_PHASE_QUANTITY,
              "and 0x28 still counts as the popup, so no source row is announced over it");
        printf("quantity: produced, consumed, remaining and the resulting stock all come "
               "from the recipe, and the pre-write frame stays silent\n");
    }

    // =====================================================================
    // DID THE REFINE ACTUALLY HAPPEN?  (v0.33.2)
    // ---------------------------------------------------------------------
    // The v0.33.1 BAT walked every screen and backed out of every refine, so
    // the one action that changes the save was never exercised -- and it was
    // silent. State 0x2A does the work in a single frame and jumps out
    // (0x004D7DEA); cancel and confirm both clear +0x51 and both land back on a
    // list with the cursor unmoved. The source's own count is what separates
    // them, and it is a measurement rather than an inference.
    // =====================================================================
    {
        // 5 Tents -> 50 Curaga, the BAT's own staged refine.
        check(RefineOutcomeOf(5, 0, 5) == RF_OUT_DONE,
              "the source dropping by exactly the consumed amount is a completed refine");
        check(RefineOutcomeOf(5, 5, 5) == RF_OUT_CANCELLED,
              "the source unchanged is a cancel");
        check(RefineOutcomeOf(84, 4, 80) == RF_OUT_DONE, "and the same holds for a magic source");
        check(RefineOutcomeOf(84, 84, 80) == RF_OUT_CANCELLED, "backing out of Mid Mag-RF is a cancel");

        // Anything that does not add up says nothing. Announcing "Refined" when
        // the numbers disagree is the failure this whole thread has been about.
        check(RefineOutcomeOf(5, 3, 5) == RF_OUT_UNKNOWN, "a partial drop is not claimed as done");
        check(RefineOutcomeOf(5, 7, 5) == RF_OUT_UNKNOWN, "nor is the count going UP");
        check(RefineOutcomeOf(-1, 0, 5) == RF_OUT_UNKNOWN, "an unreadable before says nothing");
        check(RefineOutcomeOf(5, -1, 5) == RF_OUT_UNKNOWN, "an unreadable after says nothing");
        check(RefineOutcomeOf(5, 5, 0) == RF_OUT_UNKNOWN,
              "and a zero consumed cannot distinguish the two, so it says nothing");
        printf("outcome: a confirmed refine and a cancel are told apart by the source's "
               "own count, and an inconsistent pair stays silent\n");
    }

    // =====================================================================
    // THE ROWS THEMSELVES, THROUGH THE REAL ADDRESSES  (v0.33.0)
    // ---------------------------------------------------------------------
    // The model above decides WHICH list; this decides whether the right bytes
    // come back out of it. Both failures in the BAT were a right-looking number
    // read from the wrong array, so the arrays are what get asserted.
    // =====================================================================
    if (havePool && haveSavemap && haveCards && haveRecipeMem) {
        uint8_t** head  = (uint8_t**)ABIL_LIST_HEAD;
        uint8_t*  slot3 = (uint8_t*)ABIL_SLOT3_ALIAS;
        *head = nullptr;
        memset(slot3, 0, 0x78);
        slot3[0x12] = 1;                                   // in use
        *(uint16_t*)(slot3 + 0x10) = 0x0D;                 // a source-list state

        // A recipe table the view has to decode exactly as state 0x2A does.
        uint8_t* recipes = (uint8_t*)RECIPE_FIXTURE;
        memset(recipes, 0, 64);
        //  entry 1: 1 Force Armlet (84) -> 30 Shell Stone (22)
        *(uint16_t*)(recipes + 8 + 2) = 30;   // result per unit
        recipes[8 + 4] = 0;                   // required level
        recipes[8 + 5] = 84;                  // source id
        recipes[8 + 6] = 1;                   // source per unit
        recipes[8 + 7] = 22;                  // result id
        *(uint32_t*)(slot3 + RFO_RECIPES) = (uint32_t)(uintptr_t)recipes;
        slot3[RFO_RECIPE_N]   = 4;
        slot3[RFO_RECIPE_IDX] = 1;
        slot3[RFO_SUBMODE]    = RF_SUB_ITEM_TO_ITEM;
        slot3[RFO_ABILITY]    = 108;                       // Tool-RF
        slot3[RFO_SRCCUR]     = 3;
        slot3[RFO_PICKCUR]    = 2;
        slot3[RFO_QTY_MAX]    = 5;
        slot3[RFO_QTY]        = 2;
        slot3[RFO_QTY_OPEN]   = 1;

        AbilRefineView v;
        check(AbilReadRefineView(&v), "the view reads off the module base");
        check(v.submode == RF_SUB_ITEM_TO_ITEM && v.abilityId == 108, "sub-mode and ability come back");
        check(v.qty == 2 && v.qtyMax == 5 && v.qtyOpen == 1, "the quantity fields come back");
        check(v.haveRecipe && v.recipe.sourceId == 84 && v.recipe.sourcePer == 1 &&
              v.recipe.resultId == 22 && v.recipe.resultPer == 30,
              "the recipe decodes as {+2 result per, +5 source id, +6 source per, +7 result id}");
        check(RefinePhaseOf(v.state, v.submode, v.qtyOpen) == RF_PHASE_QUANTITY,
              "and with +0x51 set the popup is what is up");

        // A recipe index past the table's own count must not be decoded.
        slot3[RFO_RECIPE_IDX] = 9;
        check(AbilReadRefineView(&v) && !v.haveRecipe,
              "a recipe index past +0x46 is refused rather than read");
        slot3[RFO_RECIPE_IDX] = 1;
        // A state past the machine's max means this is not the refine module.
        *(uint16_t*)(slot3 + 0x10) = 0x40;
        check(!AbilReadRefineView(&v), "a state past 0x2C is not accepted as a refine view");
        *(uint16_t*)(slot3 + 0x10) = 0x0D;

        // --- the three source lists ---------------------------------------
        uint8_t* inv = (uint8_t*)(SAVEMAP_BASE + ITEM_INVENTORY_OFFSET);
        memset(inv, 0, 512);
        inv[6] = 84; inv[7] = 2;                       // row 3 = Force Armlet x2
        int id = -1, qty = -1;
        slot3[RFO_SUBMODE] = RF_SUB_ITEM_TO_ITEM;
        AbilReadRefineView(&v);
        check(AbilReadSourceRow(v, 3, &id, &qty) && id == 84 && qty == 2,
              "an item sub-mode reads the savemap inventory");

        // Sub-mode 2: a CHARACTER'S magic, 32 slots at 0x01CFE0F8 + id*152.
        slot3[RFO_SUBMODE] = RF_SUB_MAGIC_TO_MAGIC;
        slot3[RFO_CHARID]  = 3;                        // Quistis, as in the screenshot
        uint8_t* mag = (uint8_t*)(RF_CHAR_MAGIC_BASE + 3 * RF_CHAR_MAGIC_STRIDE);
        memset(mag, 0, RF_CHAR_MAGIC_STRIDE);
        mag[0] = 21; mag[1] = 42;                      // slot 0 = Cure x42
        mag[62] = 23; mag[63] = 7;                     // slot 31 = Curaga x7
        AbilReadRefineView(&v);
        check(AbilReadSourceRow(v, 0, &id, &qty) && id == 21 && qty == 42,
              "sub-mode 2 reads the character's magic, NOT the item inventory");
        check(AbilReadSourceRow(v, 31, &id, &qty) && id == 23 && qty == 7,
              "all thirty-two slots are reachable (the 0x20 loop at 0x004D7BEE)");
        check(!AbilReadSourceRow(v, 32, &id, &qty), "and the thirty-third is not");
        // The regression in one line. Put a real item in inventory row 0 and
        // ask sub-mode 2 for row 0: the BAT's answer was "Potion, 34" off this
        // very array while the screen showed a list of characters.
        inv[0] = 84; inv[1] = 34;
        check(AbilReadSourceRow(v, 0, &id, &qty) && id == 21 && qty == 42,
              "sub-mode 2 must not fall through to the item inventory");

        // Sub-mode 4: the list the creator builds, storing cardId + 1.
        slot3[RFO_SUBMODE] = RF_SUB_CARD_TO_ITEM;
        uint8_t* cards = (uint8_t*)RF_CARD_LIST;
        memset(cards, 0, 0x100);
        cards[0] = 1;  cards[1] = 3;                   // card 0 (Geezard) x3
        cards[4] = 11; cards[5] = 1;                   // card 10 x1
        AbilReadRefineView(&v);
        check(AbilReadSourceRow(v, 0, &id, &qty) && id == 0 && qty == 3,
              "a card row is cardId + 1 on disk and cardId in hand");
        check(AbilReadSourceRow(v, 2, &id, &qty) && id == 10 && qty == 1, "and so is the third row");
        check(AbilReadSourceRow(v, 1, &id, &qty) && id == -1,
              "an unwritten card row reports no card rather than card 255");
        check(id < CARD_COUNT, "every card id the list can yield indexes the 110-card table");

        // --- the resulting stock the popup's third row shows ---------------
        // Magic: the destination character's own 32 slots. Aaron's screenshot
        // is the fixture -- 71 held, 16 made, "Cura 87" on screen.
        slot3[RFO_SUBMODE] = RF_SUB_MAGIC_TO_MAGIC;
        slot3[RFO_CHARID]  = 0;
        uint8_t* sq = (uint8_t*)(RF_CHAR_MAGIC_BASE + 0 * RF_CHAR_MAGIC_STRIDE);
        memset(sq, 0, RF_CHAR_MAGIC_STRIDE);
        sq[0] = 21; sq[1] = 84;                        // Cure x84 (the source)
        sq[8] = 22; sq[9] = 71;                        // Cura x71 (the result)
        *(uint16_t*)(recipes + 8 + 2) = 1;             // 5 Cure -> 1 Cura
        recipes[8 + 5] = 21;
        recipes[8 + 6] = 5;
        recipes[8 + 7] = 22;
        AbilReadRefineView(&v);
        check(AbilResultStock(v) == 71, "the result's current stock is found by id, not by slot");
        check(RefineQtyMath(v.recipe, 16, 84, AbilResultStock(v)).total == 87,
              "so the spoken total is the screenshot's 87");
        sq[8] = 0; sq[9] = 0;
        AbilReadRefineView(&v);
        check(AbilResultStock(v) == 0,
              "a result not held yet reads zero, not 'unreadable' -- Death Stone from none");

        // Items: the 198 inventory slots, scanned the same way (0x0047ED18).
        slot3[RFO_SUBMODE] = RF_SUB_ITEM_TO_ITEM;
        recipes[8 + 7] = 25;                           // Death Stone
        memset(inv, 0, 512);
        inv[40] = 25; inv[41] = 12;
        AbilReadRefineView(&v);
        check(AbilResultStock(v) == 12, "an item result is found in the inventory by id");
        // Restore the Tool-RF recipe for anything that follows.
        *(uint16_t*)(recipes + 8 + 2) = 30;
        recipes[8 + 5] = 84; recipes[8 + 6] = 1; recipes[8 + 7] = 22;

        // --- the character picker's mask ----------------------------------
        // Rebuilt exactly as 0x004AD030 builds it, because the picker's cursor
        // is a position in the SET BITS, not a character id (0x004ABC40).
        for (int i = 0; i < 8; i++) ((uint8_t*)0x01CFE17C)[i * 0x98] = 0;
        ((uint8_t*)0x01CFE97A)[0] = 0;                 // no formation narrowing
        ((uint8_t*)0x01CFE17C)[0 * 0x98] = 1;          // Squall
        ((uint8_t*)0x01CFE17C)[2 * 0x98] = 1;          // Irvine
        ((uint8_t*)0x01CFE17C)[5 * 0x98] = 1;          // Selphie
        int mask = AbilCharMask();
        check(mask == ((1 << 0) | (1 << 2) | (1 << 5)), "the mask is the 'exists' bytes");
        check(AbilCharAtPickerRow(mask, 0) == 0, "row 0 is Squall");
        check(AbilCharAtPickerRow(mask, 1) == 2, "row 1 is IRVINE, not Zell -- the list has gaps");
        check(AbilCharAtPickerRow(mask, 2) == 5, "row 2 is Selphie");
        check(AbilCharAtPickerRow(mask, 3) == -1, "a row past the end names nobody");

        // With [0x01CFE97A] & 1 the mask narrows to the battle formation.
        ((uint8_t*)0x01CFE97A)[0] = 1;
        ((uint8_t*)0x01CFE74C)[0] = 2;
        ((uint8_t*)0x01CFE74C)[1] = 5;
        ((uint8_t*)0x01CFE74C)[2] = 0xFF;
        mask = AbilCharMask();
        check(mask == ((1 << 2) | (1 << 5)), "a live formation narrows the picker");
        check(AbilCharAtPickerRow(mask, 0) == 2, "and row 0 shifts with it");
        ((uint8_t*)0x01CFE97A)[0] = 0;

        printf("source rows: items, one character's thirty-two magic slots, and the "
               "card list read from their own arrays; the picker indexes set bits\n");
    } else {
        printf("  (could not map the row arrays -- skipping the live row checks)\n");
    }

    // =====================================================================
    // THE WHOLE PATH, DRIVEN  (v0.33.3)
    // ---------------------------------------------------------------------
    // v0.33.2 shipped PollRefineOutcome() and the pure RefineOutcomeOf() it
    // calls, and the probe asserted the latter -- while the line that ARMS it
    // was missing from the file entirely. Everything compiled, every test
    // passed, and the BAT log had not one `Refine outcome` line in it.
    //
    // Testing a pure function is not testing that anything calls it. This block
    // drives PollAbilityItemList() through the real module memory and asserts
    // what was SPOKEN, so the wiring cannot go missing again in silence.
    // =====================================================================
    if (havePool && haveSavemap && haveCards && haveRecipeMem) {
        uint8_t** head  = (uint8_t**)ABIL_LIST_HEAD;
        uint8_t*  slot3 = (uint8_t*)ABIL_SLOT3_ALIAS;
        uint8_t*  inv2  = (uint8_t*)(SAVEMAP_BASE + ITEM_INVENTORY_OFFSET);
        uint8_t*  recipes = (uint8_t*)RECIPE_FIXTURE;

        // Tool-RF on Dead Spirit: 1 Dead Spirit (158) -> 2 Death Stone (25),
        // holding two, refining one. The v0.33.2 BAT's own case.
        memset(recipes, 0, 64);
        *(uint16_t*)(recipes + 2) = 2;      // result per unit
        recipes[5] = 158;                   // source id
        recipes[6] = 1;                     // source per unit
        recipes[7] = 25;                    // result id
        memset(inv2, 0, 512);
        inv2[0] = 158; inv2[1] = 2;         // Dead Spirit x2
        *head = nullptr;
        memset(slot3, 0, 0x78);
        slot3[0x12] = 1;
        *(uint32_t*)(slot3 + RFO_RECIPES) = (uint32_t)(uintptr_t)recipes;
        slot3[RFO_RECIPE_N]   = 4;
        slot3[RFO_RECIPE_IDX] = 0;
        slot3[RFO_SUBMODE]    = RF_SUB_ITEM_TO_ITEM;
        slot3[RFO_ABILITY]    = 108;
        slot3[RFO_QTY_MAX]    = 2;
        slot3[RFO_QTY]        = 1;
        slot3[RFO_QTY_OPEN]   = 1;
        *(uint16_t*)(slot3 + RFO_STATE) = RF_ST_QTY;

        ResetAbilitySubmenuState();
        ScreenReader::g_last[0] = '\0';
        PollAbilityItemList();
        check(strcmp(ScreenReader::g_last,
                     "Number to refine 1 of 2, makes 2 Death Stone for 2 total, 1 Dead Spirit left") == 0,
              "the popup speaks the BAT's own line");

        // Confirm: the engine consumed one Dead Spirit and granted two Death
        // Stones, cleared +0x51, and dropped back to the source list.
        inv2[1] = 1;                        // one Dead Spirit consumed
        inv2[2] = 25; inv2[3] = 2;          // two Death Stones gained
        slot3[RFO_QTY_OPEN] = 0;
        *(uint16_t*)(slot3 + RFO_STATE) = 0x0D;
        ScreenReader::g_last[0] = '\0';
        PollAbilityItemList();
        check(strcmp(ScreenReader::g_last, "Refined 1 Dead Spirit into 2 Death Stone, 2 total") == 0,
              "**and closing it announces the refine** -- the line v0.33.2 never armed");
        // It must also be the LAST thing said that poll. Falling through to the
        // source row would cut the outcome off mid-sentence: both utterances
        // interrupt, so the player would hear only the row he was already on.
        check(ScreenReader::g_count == 2,
              "the outcome is the only thing spoken that poll, not the row on top of it");

        // Cancel: same close, source untouched.
        slot3[RFO_QTY_OPEN] = 1;
        *(uint16_t*)(slot3 + RFO_STATE) = RF_ST_QTY;
        slot3[RFO_QTY] = 1;
        ResetAbilitySubmenuState();
        PollAbilityItemList();               // arm again
        slot3[RFO_QTY_OPEN] = 0;
        *(uint16_t*)(slot3 + RFO_STATE) = 0x0D;
        ScreenReader::g_last[0] = '\0';
        PollAbilityItemList();
        check(strcmp(ScreenReader::g_last, "Cancelled") == 0,
              "an untouched source announces a cancel, not a refine");

        // It must fire ONCE. A second poll on the same list is a source row.
        ScreenReader::g_last[0] = '\0';
        PollAbilityItemList();
        check(strcmp(ScreenReader::g_last, "Refined 1 Dead Spirit into 2 Death Stone, 2 total") != 0 &&
              strcmp(ScreenReader::g_last, "Cancelled") != 0,
              "the outcome is announced once, not on every poll afterwards");

        // **Sub-mode 0 lands back on the RECIPIENT PICKER, not on a list.**
        // v0.33.3 returned from the poll that announced the outcome -- and the
        // NEXT poll spoke the character's name over it, because the popup had
        // re-armed that announcer on its way past. The BAT heard "Refined 3 Tent
        // into 30 Curaga, 30 total" followed immediately by "Squall".
        memset(recipes, 0, 64);
        *(uint16_t*)(recipes + 2) = 10;     // 1 Tent -> 10 Curaga
        recipes[5] = 33;                    // Tent
        recipes[6] = 1;
        recipes[7] = 23;                    // Curaga
        memset(inv2, 0, 512);
        inv2[0] = 33; inv2[1] = 5;          // Tent x5
        slot3[RFO_SUBMODE]  = RF_SUB_ITEM_TO_MAGIC;
        slot3[RFO_ABILITY]  = 100;
        slot3[RFO_CHARID]   = 0;
        slot3[RFO_QTY_MAX]  = 5;
        slot3[RFO_QTY]      = 3;
        slot3[RFO_QTY_OPEN] = 1;
        *(uint16_t*)(slot3 + RFO_STATE) = RF_ST_QTY;
        ResetAbilitySubmenuState();
        PollAbilityItemList();                       // the popup, and the arm

        inv2[1] = 2;                                 // three Tents consumed
        slot3[RFO_QTY_OPEN] = 0;
        *(uint16_t*)(slot3 + RFO_STATE) = RF_ST_RECIP_LO;   // back to the picker
        ScreenReader::g_last[0] = '\0';
        PollAbilityItemList();
        check(strcmp(ScreenReader::g_last, "Refined 3 Tent into 30 Curaga, 30 total") == 0,
              "a sub-mode 0 refine announces from the recipient picker too");

        // The poll AFTER it must stay quiet. This is the one v0.33.3 missed:
        // returning early only protects the poll the outcome happened in.
        const int before = ScreenReader::g_count;
        PollAbilityItemList();
        PollAbilityItemList();
        check(ScreenReader::g_count == before,
              "**and the picker does not speak over it on the next poll**");

        // Moving to a different character must still announce, though -- the
        // dedupe is seeded, not disabled.
        // (State 0x1C rewrites +0x48 from the cursor every frame at 0x004D7589,
        // so the engine moves both -- the fixture must too.)
        slot3[RFO_PICKCUR] = 1;
        slot3[RFO_CHARID]  = 1;
        ScreenReader::g_last[0] = '\0';
        PollAbilityItemList();
        check(strcmp(ScreenReader::g_last, "Zell") == 0,
              "moving the picker still announces -- the dedupe is seeded, not disabled");

        printf("wiring: the popup arms the outcome, closing it speaks, and nothing "
               "speaks over it on the poll after -- driven through PollAbilityItemList()\n");
    }

    printf("menu_ability_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
