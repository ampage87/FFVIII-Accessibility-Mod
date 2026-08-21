// menu_item_compile.cpp -- v0.30.0 (#89)
//
// Compile-and-run probe for the Item screen's USE-TARGET reader.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_item_compile tests/menu_item_compile.cpp
//
// WHY THIS EXISTS
//
// Aaron: *"The list of characters / party members / GFs doesn't always seem to
// be accurate. Most of the time it is, but sometimes not."*
//
// `0x004F8600..0x004F86BF` builds the target list as a 32-bit mask: the low half
// from `0x004AD030` (bit i = character i exists, narrowed to the battle
// formation when `[0x01CFE97A] & 1`), the high half from `0x004AD090` (bit 16+j
// = GF j obtained). The cursor at `+0x58` is **the bit index**, and the draw
// code positions each row from that index (`y = cur*13 + 0x42`) rather than from
// a packed position — **the screen leaves gaps.**
//
// The mod used to sort a roster into a packed list and index it with the cursor.
// That agrees with the engine **only while the set bits run 0,1,2,... with no
// gaps** — which is Aaron's current party exactly, and why it was right most of
// the time. Every case below with a gap in it is a case the old code got wrong,
// and the GF cases it could not express at all.

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

static DWORD GetTickCount() { return 0; }
namespace Log { void Menu(const char*, ...) {} }
namespace ScreenReader {
    char g_last[512]; int g_count = 0;
    bool Speak(const char* t, bool = false)
    { snprintf(g_last, sizeof(g_last), "%s", t ? t : ""); g_count++; return true; }
    bool IsSpeaking() { return false; }
}
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n)
    { std::string s; for (size_t i = 0; i < n; i++) s += (char)d[i]; return s; }
    std::string Decode(const uint8_t* d, size_t n = 1024)
    { std::string s; for (size_t i = 0; i < n && d[i]; i++) s += (char)d[i]; return s; }
}
namespace FieldDialog { int SnapshotGcwBuffer(uint8_t*, size_t) { return 0; } }

static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevCursor = 1;
static const uintptr_t SAVEMAP_BASE = 0x1CFDC5C;

// --- savemap layout, the same values src/menu_tts_diagnostics.inl uses -------
static const int HDR_CHAR1_MAX_HP  = 0x04;
static const int CHARS_OFFSET      = 0x48C;
static const int CHAR_STRUCT_SIZE  = 0x98;
static const int CHR_CURR_HP       = 0x00;
static const int CHR_MAX_HP        = 0x02;
static const int CHR_STATUS        = 0x96;
static const int ITEM_INVENTORY_OFFSET = 0x0B40;

// --- menu-state offsets the Item reader still uses for its other flows ------
static const int SUBMENU_LIST_CURSOR_OFFSET   = 0x272;
static const int SUBMENU_ACTION_CURSOR_OFFSET = 0x27F;
static const int SUBMENU_PHASE_OFFSET         = 0x230;
static const int ITEM_FOCUS_STATE_OFFSET      = 0x22E;
static const int ITEM_SUBPHASE_OFFSET         = 0x5DF;
static const int ITEM_TARGET_CURSOR_OFFSET    = 0x276;
static const int BATTLE_ITEM_CURSOR_OFFSET    = 0x285;
static const int PARTY_INDICES_OFFSET         = 0xAF1;

static const char* ITEM_ACTION_NAMES[] = { "Use", "Rearrange", "Sort", "Battle" };
static const int   ITEM_ACTION_COUNT   = 4;
static const char* MENU_ITEMS[] = { "Junction","Item","Magic","Status","GF",
                                    "Ability","Switch","Card","Config","Tutorial","Save" };
static const int   MENU_ITEMS_COUNT = 11;

// --- per-screen state owned by menu_tts.cpp ---------------------------------
static bool     s_itemSubmenuActive = false;
static uint8_t  s_prevItemCursor = 0xFF;
static uint8_t  s_prevActionCursor = 0xFF;
static uint8_t  s_prevFocusState = 0xFF;
static uint8_t  s_pendingActionCursor = 0xFF;
static DWORD    s_pendingActionTime = 0;
static uint8_t  s_prevTargetCursor = 0xFF;
static bool     s_prevTargetIsGF = false;
static uint8_t  s_prevTargetCharIdx = 0xFF;
static uint16_t s_prevTargetHP = 0xFFFF;
static bool     s_inUseTargetMode = false;
static bool     s_inRearrangeMode = false;
static uint8_t  s_rearrangePrevFocus = 0xFF;
static bool     s_inBattleMode = false;
static bool     s_inBattleDestMode = false;
static uint8_t  s_battleSwapSrcPos = 0xFF;
static uint8_t  s_prevBattleItemCursor = 0xFF;
static bool     s_rearDestDiagValid = false;
static bool     s_batDestDiagValid = false;

// --- helpers from elsewhere in the translation unit --------------------------
static const char* CHAR_NAMES[] = { "Squall","Zell","Irvine","Quistis",
                                    "Rinoa","Selphie","Seifer","Edea" };
static const char* GetCharacterNameByPortrait(uint8_t id)
{ return (id < 8) ? CHAR_NAMES[id] : nullptr; }
static uint8_t ResolveDreamAwareCharId(uint8_t id) { return id; }
static const char* GetItemName(int) { return "Potion"; }
static const char* GetMenuItemName(int i)
{ return (i >= 0 && i < MENU_ITEMS_COUNT) ? MENU_ITEMS[i] : "?"; }
static char g_gfName[32] = "Quezacotl";
static void DecodeGFName(int idx, char* out, int outSize)
{ snprintf(out, outSize, "%s%d", g_gfName, idx); }

static void SubmonStart(uint8_t) {}
static void SubmonStop() {}
static void SubmonPoll() {}

#include "menu_item_swap_model.inl"
// v0.37.3 (#96): the learn notice goes through the SHARED dialog window, so
// the probe includes the real reader rather than stubbing it (the v0.34.2
// lesson). Its three globals are mapped in main() alongside the rest.
#include "menu_dialog.inl"
#include "menu_tts_item.inl"

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

static uint8_t* PlaceItem(int slot)
{
    uint8_t** head = (uint8_t**)ITEM_LIST_HEAD;
    for (int i = 0; i < 10; i++) {
        uint8_t* m = (uint8_t*)(ITEM_POOL_BASE + i * 0x78);
        memset(m, 0, 0x78);
        *(uint32_t*)(m + 0x08) = (i == slot) ? ITEM_STATE_FN : 0x004F02F0;
        *(uint8_t**)m = (i < 9) ? (uint8_t*)(ITEM_POOL_BASE + (i + 1) * 0x78) : nullptr;
    }
    *head = (uint8_t*)ITEM_POOL_BASE;
    return (uint8_t*)(ITEM_POOL_BASE + slot * 0x78);
}

static void SetSel(uint8_t* m, uint32_t mask, int cur, int kind)
{
    // The reader now requires the module's own state word to agree with the
    // state the caller is in -- that is what turns the slot-2 alias from an
    // assumption into a test -- so the fixture has to look like the real screen.
    *(uint16_t*)(m + 0x10) = 14;
    *(uint32_t*)(m + ITMO_TARGET_MASK) = mask;
    *(int16_t*)(m + ITMO_TARGET_CUR)   = (int16_t)cur;
    *(uint8_t*)(m + ITMO_TARGET_KIND)  = (uint8_t)kind;
}

int main()
{
    printf("menu_tts_item.inl compiles\n");

    // GetCharacterHP reads the savemap and the menu's HP mirror. Give both real
    // pages so the composed line is exercised end to end rather than short-
    // circuited -- the point of this probe is the whole sentence, not the slot
    // number on its own.
    static uint8_t menuState[0x800];
    memset(menuState, 0, sizeof(menuState));
    pMenuStateA = (WORD*)menuState;
    // One combined range: the savemap through the computed-stats array at
    // 0x01CFF000, which GetCharacterHP falls back to. Overlapping
    // MAP_FIXED_NOREPLACE calls fail, which is how an earlier probe host broke.
    if (MapAt(SAVEMAP_BASE, (size_t)(COMP_STATS_BASE + 0x600 - SAVEMAP_BASE))) {
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE;
        memset(sm, 0, 0x1000);
        memset((void*)(uintptr_t)COMP_STATS_BASE, 0, 0x600);
        // No battle formation: leave it 0xFF so the computed-stats fallback is
        // not entered for every character.
        memset(sm + 0xAF0, 0xFF, 4);
        for (int i = 0; i < 8; i++) {
            uint8_t* c = sm + CHARS_OFFSET + i * CHAR_STRUCT_SIZE;
            *(uint16_t*)(c + CHR_CURR_HP) = (uint16_t)(100 + i);
            *(uint16_t*)(c + CHR_MAX_HP)  = (uint16_t)(200 + i);
        }
    }

    // --- the arrange decision (v0.29.x), unchanged and still pinned ----------
    s_swapSrcIdAtArm = ITEM_SWAP_NO_ID;
    check(!ItemSwapHappened(0x21), "nothing armed means nothing happened");
    s_swapSrcIdAtArm = 0x21;
    check(ItemSwapHappened(0x35), "a different id at the source slot is a swap");
    check(s_swapSrcIdAtArm == ITEM_SWAP_NO_ID, "the arm must be consumed");
    s_swapSrcIdAtArm = 0x21;
    check(!ItemSwapHappened(0x21), "the same id at the source slot is a cancel");

    if (!MapAt(ITEM_LIST_HEAD, (size_t)(ITEM_POOL_END - ITEM_LIST_HEAD))) {
        printf("  (could not map the pool -- skipping the engine checks)\n");
        printf("menu_item_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }
    uint8_t** head = (uint8_t**)ITEM_LIST_HEAD;

    // --- the module walk ----------------------------------------------------
    *head = nullptr;
    check(FindItemModule() == nullptr, "an empty list must yield no module");
    for (int place = 0; place < 10; place++) {
        uint8_t* want = PlaceItem(place);
        if (FindItemModule() != want) {
            bad++; printf("  BAD: Item module not found in slot %d\n", place);
        }
    }
    *head = (uint8_t*)0x00401000;
    check(FindItemModule() == nullptr, "an out-of-pool head must not be followed");
    *head = (uint8_t*)(ITEM_POOL_BASE + 4);
    check(FindItemModule() == nullptr, "a misaligned pool pointer must be rejected");
    {
        uint8_t* a = (uint8_t*)ITEM_POOL_BASE;
        uint8_t* b = (uint8_t*)(ITEM_POOL_BASE + 0x78);
        memset(a, 0, 0x78); memset(b, 0, 0x78);
        *(uint32_t*)(a + 0x08) = 0x004F02F0;
        *(uint32_t*)(b + 0x08) = 0x004F02F0;
        *(uint8_t**)a = b; *(uint8_t**)b = a;
        *head = a;
        check(FindItemModule() == nullptr, "a cyclic list must terminate");
    }
    printf("module walk: Item found in all 10 slots; bad lists all terminate\n");

    uint8_t* m = PlaceItem(2);
    ItemTargetSel sel;
    char buf[256];

    // --- 1. THE CASE THAT WAS ALWAYS RIGHT, which must stay right -----------
    // Squall..Selphie, ids 0..5, no gaps. This is Aaron's party, and it is why
    // the packed-list version passed every test it was ever given.
    for (int i = 0; i < 6; i++) {
        SetSel(m, 0x003F, i, 0);
        check(ItemReadTargetSel(14, sel), "read must succeed");
        check(sel.slot == i && !sel.isGF && sel.available, "contiguous party: slot == id");
        ItemFormatTarget(sel, false, buf, sizeof(buf));
        if (strncmp(buf, CHAR_NAMES[i], strlen(CHAR_NAMES[i])) != 0) {
            bad++; printf("  BAD: contiguous slot %d said \"%s\"\n", i, buf);
        }
    }

    // --- 2. **A GAP.** v0.30.0 read the cursor as a BIT INDEX here. -------
    // v0.37.3 (#96): it is a PACKED POSITION for characters. `0x004F88F5`
    // calls `0x004ABC40(mask, cursor)` -- the Nth-set-bit helper -- and stores
    // the answer in +0x62, which `0x004F8920` then shifts into the action's
    // target mask. GFs keep the bit-index shape (`0x004F88CF`: cursor + 0x10,
    // shift, test), so the two halves genuinely differ.
    //
    // These numbers come from the exe and from Aaron's 2026-08-20 screenshot,
    // not from what this file used to do: mask 0x08 (one bit, Quistis) with the
    // cursor at 0 drew a NAME panel containing exactly one row, hers, cursor on
    // it. Position 0 of a compacted list.
    SetSel(m, (1u<<0)|(1u<<2)|(1u<<5), 1, 0);
    ItemReadTargetSel(14, sel);
    check(sel.slot == 2 && sel.available,
          "**position 1 of {0,2,5} is Irvine**, not Zell -- the list is compacted");
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strncmp(buf, "Irvine", 6) == 0, "and it is named as Irvine");

    SetSel(m, (1u<<0)|(1u<<2)|(1u<<5), 2, 0);
    ItemReadTargetSel(14, sel);
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strncmp(buf, "Selphie", 7) == 0, "position 2 is the last of the three");

    SetSel(m, (1u<<0)|(1u<<2)|(1u<<5), 3, 0);
    ItemReadTargetSel(14, sel);
    check(!sel.available, "position 3 is past the end of a three-target list");

    // **THE BAT CASE.** A Blue Magic item masks the targets down to bit 3 alone
    // (`0x004F86A2` is literally `and ebp, 8`), and the engine leaves the cursor
    // at 0 because the init at `0x004F8710` is gated on a flag this item lacks.
    // v0.30.0 said "Squall, not available" about a row that is not drawn.
    SetSel(m, (1u<<3), 0, 0);
    ItemReadTargetSel(14, sel);
    check(sel.slot == 3 && sel.available,
          "**a single-target Blue Magic item resolves to Quistis at cursor 0**");
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strncmp(buf, "Quistis", 7) == 0, "and says Quistis, not Squall");

    // A mask that starts above zero -- position 0 is the first SET bit.
    SetSel(m, (1u<<6)|(1u<<7), 0, 0);
    ItemReadTargetSel(14, sel);
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strncmp(buf, "Seifer", 6) == 0, "a mask of {6,7} names Seifer at position 0");
    SetSel(m, (1u<<6)|(1u<<7), 1, 0);
    ItemReadTargetSel(14, sel);
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strncmp(buf, "Edea", 4) == 0, "and Edea at position 1");

    // The model on its own, both halves.
    check(ItemResolveTargetId(0x0000000Cu, 0, false) == 2, "Nth set bit: position 0 of {2,3}");
    check(ItemResolveTargetId(0x0000000Cu, 1, false) == 3, "position 1 of {2,3}");
    check(ItemResolveTargetId(0x0000000Cu, 2, false) == -1, "position 2 of {2,3} is nothing");
    check(ItemResolveTargetId(0x00080000u, 3, true) == 3, "GFs keep the bit-index shape");
    check(ItemResolveTargetId(0x00080000u, 2, true) == -1, "a clear GF bit is nothing");

    // --- 3. **GFs**, which the old reader could not express at all ----------
    // High half of the mask, kind byte 1. Every one of these used to be read out
    // as a party member.
    SetSel(m, 0x00050000u, 0, 1);          // GFs 0 and 2 obtained
    check(ItemReadTargetSel(14, sel), "GF read must succeed");
    check(sel.isGF && sel.slot == 0 && sel.available, "GF slot 0 is obtained");
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strcmp(buf, "Quezacotl0") == 0, "a GF is named from the savemap");

    SetSel(m, 0x00050000u, 1, 1);
    ItemReadTargetSel(14, sel);
    check(!sel.available, "GF slot 1 is not obtained");
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strcmp(buf, "Quezacotl1, not available") == 0,
          "an unobtained GF keeps its slot and is marked, not skipped");

    SetSel(m, 0x00050000u, 15, 1);
    ItemReadTargetSel(14, sel);
    check(sel.slot == 15 && sel.isGF, "GF slot 15 is in range");

    // The two halves must not be confused: the same bit position means a
    // character in one and a GF in the other.
    SetSel(m, 0x00000001u, 0, 1);
    ItemReadTargetSel(14, sel);
    check(!sel.available, "a character bit must not make a GF available");
    SetSel(m, 0x00010000u, 0, 0);
    ItemReadTargetSel(14, sel);
    check(!sel.available, "a GF bit must not make a character available");

    // --- 4. "Use on " prefix only on arrival --------------------------------
    SetSel(m, 0x003F, 0, 0);
    ItemReadTargetSel(14, sel);
    ItemFormatTarget(sel, true, buf, sizeof(buf));
    check(strncmp(buf, "Use on Squall", 13) == 0, "the entry line says Use on");
    ItemFormatTarget(sel, false, buf, sizeof(buf));
    check(strncmp(buf, "Squall", 6) == 0, "the move line does not repeat it");

    // --- 5. refusals --------------------------------------------------------
    SetSel(m, 0x003F, 16, 0);
    check(!ItemReadTargetSel(14, sel), "a slot past 15 must be refused");
    SetSel(m, 0x003F, -1, 0);
    check(!ItemReadTargetSel(14, sel), "a negative cursor must be refused");
    SetSel(m, 0x003F, 0, 0);
    *head = nullptr;
    check(!ItemReadTargetSel(14, sel), "with no Item module in the pool, no answer");

    // --- 6. **THE IDENTIFICATION ITSELF** (v0.30.1) -------------------------
    // v0.30.0 asked the pool walk and nothing else, and the BAT came back with
    // "engine read failed" on every entry while the screenshot showed eleven GFs
    // drawn and the cursor on Alexander. The module was there. So the reader now
    // accepts either identification and demands the same evidence from both: the
    // module's own state word must be the state the caller is in.
    {
        uint8_t* alias = (uint8_t*)pMenuStateA + 0x21E;

        // (a) The walk answers when it finds a module whose state agrees.
        uint8_t* mm = PlaceItem(2);
        SetSel(mm, 0x003F, 3, 0);
        check(ItemReadTargetSel(14, sel), "the walk answers when the state agrees");
        check(sel.slot == 3, "and it reads the right slot");
        check(strncmp(sel.how, "walk", 4) == 0, "and the log says it was the walk");

        // (b) **The walk finds nothing.** The verified slot-2 alias must answer.
        *head = nullptr;
        *(uint16_t*)(alias + 0x10) = 14;
        *(uint32_t*)(alias + ITMO_TARGET_MASK) = 0x003F;
        *(int16_t*)(alias + ITMO_TARGET_CUR)   = 4;
        *(uint8_t*)(alias + ITMO_TARGET_KIND)  = 0;
        check(ItemReadTargetSel(14, sel), "with no walk result the slot-2 alias answers");
        check(sel.slot == 4 && !sel.isGF, "and it reads the alias correctly");
        check(strncmp(sel.how, "slot2", 5) == 0, "and the log says which one answered");
        ItemFormatTarget(sel, false, buf, sizeof(buf));
        check(strncmp(buf, "Rinoa", 5) == 0, "the alias path names the same way");

        // (c) The alias is a TEST, not an assumption: a state that disagrees is
        //     refused. Without this the fallback would be the fixed-slot guess
        //     the rest of this audit exists to remove.
        *(uint16_t*)(alias + 0x10) = 5;          // still on the item list
        check(!ItemReadTargetSel(14, sel), "a slot-2 state that disagrees is refused");
        *(uint16_t*)(alias + 0x10) = 14;

        // (d) A walk hit whose state disagrees must not win over a good alias.
        uint8_t* wrong = PlaceItem(5);
        *(uint16_t*)(wrong + 0x10) = 99;
        *(int16_t*)(wrong + ITMO_TARGET_CUR) = 12;
        check(ItemReadTargetSel(14, sel), "a stale walk hit falls through to the alias");
        check(sel.slot == 4, "and the alias's cursor is the one used");

        *head = nullptr;
        *(uint16_t*)(alias + 0x10) = 0;
        printf("identification: walk first, slot-2 alias second, both required to "
               "agree with the live state\n");
    }

    // --- 5. THE LEARN NOTICE, off the drawn-text buffer --------------------
    // The two strings below are the 2026-08-20 BAT's own GCW frames, verbatim.
    // The first is the trap: the game has drawn the template but not yet
    // substituted the spell, so "Quistis learned !!!" appears BEFORE the real
    // sentence in the same buffer.
    {
        char nb[192];
        struct { const char* in; const char* want; } CASES[] = {
            { "Quistis learned !!!JunctionItemMagicStatusGFAbilitySwitchCardConfig"
              "TutorialSaveQuistis learned Electrocute!!!Quistis can learn Blue Magic",
              "Quistis learned Electrocute!!!" },
            { "JunctionItemMagicStatusGFAbilitySwitchCardConfigTutorialSave"
              "Quistis learned Electrocute!!!Quistis can learn",
              "Quistis learned Electrocute!!!" },
            { "SaveZell learned Mighty Guard!!!Zell can", "Zell learned Mighty Guard!!!" },
        };
        for (size_t i = 0; i < sizeof(CASES)/sizeof(CASES[0]); i++) {
            const bool ok = ItemExtractLearnNotice(CASES[i].in, nb, sizeof(nb));
            if (!ok || strcmp(nb, CASES[i].want) != 0) {
                bad++;
                printf("  BAD: notice case %d\n        got  \"%s\"\n        want \"%s\"\n",
                       (int)i, ok ? nb : "(none)", CASES[i].want);
            }
        }
        // **The pre-substitution frame on its own must produce nothing.** The
        // same shape that made the refine quantity screen read "0" (v0.33.1)
        // and the Slot announce a spell of id 0 (v0.36.0).
        check(!ItemExtractLearnNotice("Quistis learned !!!", nb, sizeof(nb)),
              "a template with nothing substituted into it is refused");
        check(!ItemExtractLearnNotice("nothing here at all", nb, sizeof(nb)),
              "and text with no notice in it is refused");
    }

    printf("use target: characters resolve by PACKED POSITION and GFs by SLOT, both halves of the "
           "mask, unavailable rows marked, refusals on out-of-range input, and the learn "
           "notice pulled out of the BAT's own drawn-text frames\n");

    printf("menu_item_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
