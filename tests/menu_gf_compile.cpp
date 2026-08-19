// menu_gf_compile.cpp -- v0.29.1 (#88)
//
// Compile-and-run probe for the GF Learn-list parse and its help slice.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_gf_compile tests/menu_gf_compile.cpp
//
// WHY THIS EXISTS
//
// The v0.29.0 BAT's "/" key on Leviathan's Learn list read:
//
//     "Raises Spr by 20%Mag-JSpr-JElem-Defx2"
//
// One cause, two symptoms. **The game drops "-J" in front of a multiplier** --
// its own draw buffer in that BAT reads
// "...SaveRaises Spr by 20%Mag-JSpr-JElem-Defx2MagicGFDrawItem..." -- so id 14
// renders as "Elem-Defx2" and the right-to-left list parse could not match it.
// The parse stopped three rows short, and the help description, which is
// everything between "Save" and the first row the parse DID match, then
// swallowed the three rows it had missed and spoke them as part of the help.
//
// Both fixtures below are the real decoded GCW text out of that log, pasted
// verbatim. That is the point: the earlier version of this parse passed every
// hand-written fixture it was given.

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
    char g_last[512];
    bool Speak(const char* t, bool = false)
    { snprintf(g_last, sizeof(g_last), "%s", t ? t : ""); return true; }
    bool IsSpeaking() { return false; }
}
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s; for (size_t i = 0; i < n; i++) s += (char)d[i]; return s;
    }
    std::string Decode(const uint8_t* d, size_t n = 1024) {
        std::string s;
        for (size_t i = 0; i < n; i++) { if (!d[i]) break; s += (char)d[i]; }
        return s;
    }
}
namespace FieldDialog { int SnapshotGcwBuffer(uint8_t*, size_t) { return 0; } }

static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevCursor = 4;
static const uintptr_t SAVEMAP_BASE = 0;

// Host-translation-unit symbols the GF reader borrows. This probe exercises the
// LIST PARSE and the HELP SLICE, so these are the thinnest stubs that compile;
// the savemap layout itself is pinned elsewhere.
static const int CHAR_COUNT = 8, CHARS_OFFSET = 0x48C, CHAR_STRUCT_SIZE = 0x98;
static const int CHR_EXISTS = 0x94, CHR_MODEL_ID = 0x08;
static const char* CHAR_NAMES[] = { "Squall","Zell","Irvine","Quistis",
                                    "Rinoa","Selphie","Seifer","Edea" };
static bool DecodeNameToBuffer(const uint8_t*, int, char* out, size_t n)
{ if (out && n) out[0] = '\0'; return false; }
static short GetAsyncKeyState(int) { return 0; }

#include "menu_ability_names.inl"

static const char* GetAbilityName(uint8_t id)
{ return (id < ABILITY_NAME_COUNT) ? ABILITY_NAMES[id] : "Unknown"; }

#include "menu_tts_gf.inl"

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

// The two Learn screens from the v0.29.0 BAT, decoded exactly as the mod
// decoded them (Logs/ff8_menu.log, [MenuGCW] at 23:15:51 and 23:15:18).
static const char* LEVIATHAN =
    "StatusGFAbilitySwitchCardConfigTutorialSave"
    "Raises Spr by 20%"
    "Mag-JSpr-JElem-Defx2MagicGFDrawItemRecoverSpr+20%Auto-PotionSumMag+10%"
    "LeviathanLVHP/CompatibilitySquallZellIrvineQuistisRinoaSelphieLearningGFHP+30%";

static const char* CERBERUS =
    "StatusGFAbilitySwitchCardConfigTutorialSave"
    "Prevents Back Attack"
    "Str-JMag-JSpr-JSpd-JHit-JAbilityx3MagicGFDrawItemAlert"
    "CerberusLVHP/CompatibilitySquallZellIrvineQuistisRinoaSelphieLearningGFHP+20%";

// The slice UpdateGFLearnPhase performs, lifted so the probe tests the real
// thing rather than a paraphrase of it.
static std::string HelpSlice(const std::string& dec, const char* gfName,
                             int& outCount)
{
    uint8_t ids[64];
    size_t  listStart = std::string::npos;
    outCount = ParseLearnList(dec, std::string(gfName), ids, 64, &listStart);
    std::string desc;
    if (listStart != std::string::npos && listStart > 0) {
        size_t sp = dec.rfind("Save", listStart);
        if (sp != std::string::npos) {
            size_t ds = sp + 4;
            if (listStart > ds) desc = dec.substr(ds, listStart - ds);
        }
    }
    if (desc == "Select ability to learn" || desc.size() > 64) desc.clear();
    if (!desc.empty() && !GFHelpSliceIsClean(desc)) desc.clear();
    return desc;
}

int main()
{
    printf("menu_tts_gf.inl compiles\n");

    // 1. The substitution the game makes, stated as itself.
    check(NormalizeAbilityToGcw("Elem-Def-J x2") == "Elem-Defx2",
          "a multiplier drops the preceding \"-J\" (witnessed in the BAT buffer)");
    check(NormalizeAbilityToGcw("Elem-Def-J x4") == "Elem-Defx4", "same for x4");
    check(NormalizeAbilityToGcw("ST-Def-J x2")   == "ST-Defx2",   "same for ST-Def x2");
    check(NormalizeAbilityToGcw("ST-Def-J x4")   == "ST-Defx4",   "same for ST-Def x4");
    // The rule must not touch a "-J" that has no multiplier after it, or the
    // eight junction rows every GF shows would stop matching.
    check(NormalizeAbilityToGcw("Elem-Def-J") == "Elem-Def-J", "a bare -J is untouched");
    check(NormalizeAbilityToGcw("Mag-J")      == "Mag-J",      "Mag-J is untouched");
    check(NormalizeAbilityToGcw("Elem-Atk-J") == "Elem-Atk-J", "Elem-Atk-J is untouched");
    // ...and must not confuse "Ability x3", which has no -J at all.
    check(NormalizeAbilityToGcw("Ability x3") == "Abilityx3", "Ability x3 -> Abilityx3");
    check(NormalizeAbilityToGcw("SumMag plus 10%") == "SumMag+10%", "\" plus \" -> \"+\"");

    // 2. **The screen that broke.** All eleven rows, and a clean help line.
    {
        int count = 0;
        std::string desc = HelpSlice(LEVIATHAN, "Leviathan", count);
        if (count != 11) { bad++; printf("  BAD: Leviathan parsed %d rows, want 11\n", count); }
        if (desc != "Raises Spr by 20%") {
            bad++;
            printf("  BAD: Leviathan help was \"%s\", want \"Raises Spr by 20%%\"\n",
                   desc.c_str());
        }
    }

    // 3. The screen that already worked must keep working.
    {
        int count = 0;
        std::string desc = HelpSlice(CERBERUS, "Cerberus", count);
        if (count != 11) { bad++; printf("  BAD: Cerberus parsed %d rows, want 11\n", count); }
        if (desc != "Prevents Back Attack") {
            bad++;
            printf("  BAD: Cerberus help was \"%s\"\n", desc.c_str());
        }
    }
    printf("learn list: both BAT screens parse all 11 rows with a clean help line\n");

    // 4. **The guard, which is the part that has to survive the next wrong name.**
    //    Pretend the parse still cannot match "Elem-Defx2": the slice then holds
    //    list rows glued to their neighbours and must be dropped rather than
    //    spoken.
    check(!GFHelpSliceIsClean("Raises Spr by 20%Mag-JSpr-JElem-Defx2"),
          "a help slice with a glued ability name must be rejected");
    check(!GFHelpSliceIsClean("Mag-JSpr-JElem-Defx2"),
          "a slice that is nothing BUT list rows must be rejected");

    // 5. ...and a real help line that legitimately ENDS in an ability word must
    //    survive, because the game puts a space there. Boost's help is "Boost GF".
    check(GFHelpSliceIsClean("Boost GF"),
          "\"Boost GF\" must survive -- the ability word is space-preceded");
    check(GFHelpSliceIsClean("Prevents Back Attack"), "ordinary prose survives");
    check(GFHelpSliceIsClean("Raises Spr by 20%"), "the fixed Leviathan line survives");

    printf("help slice: rejects a glued list head, keeps a space-preceded ability word\n");

    // =====================================================================
    // 6. THE ROW THE CURSOR IS ON  (v0.29.2)
    // ---------------------------------------------------------------------
    // Aaron: *"It did not seem to consistently announce abilities as I moved
    // through the list on later GFs like Leviathan, Cerberus, and Pandemona."*
    // 0x004D35ED says how the engine resolves it:
    //     abs = [esi+0x36]*11 + [esi + 0x39 + [esi+0x36]]
    //     if (abs < [0x01D7DAA0]) id = [0x01D7D9F0 + abs*8]
    // The old reader indexed a list parsed out of the DRAW BUFFER -- which holds
    // one page -- with whichever of two bytes had changed, and had no page byte
    // at all. Everything below is that formula, exercised against real mapped
    // memory at the game's own addresses.
    // =====================================================================
    {
        const bool mapped =
            MapAt(GF_LIST_HEAD, (size_t)(GF_POOL_END - GF_LIST_HEAD)) &&
            MapAt(GF_LEARN_LIST, (size_t)(GF_LEARN_COUNT + 8 - GF_LEARN_LIST));
        if (!mapped) {
            printf("  (could not map the pool / list -- skipping the engine checks)\n");
        } else {
            uint8_t** head = (uint8_t**)GF_LIST_HEAD;

            // --- the module walk, same three hazards as Magic and Save --------
            *head = nullptr;
            check(FindGFModule() == nullptr, "an empty list must yield no module");
            for (int place = 0; place < 10; place++) {
                for (int i = 0; i < 10; i++) {
                    uint8_t* m = (uint8_t*)(GF_POOL_BASE + i * 0x78);
                    memset(m, 0, 0x78);
                    *(uint32_t*)(m + 0x08) = (i == place) ? GF_STATE_FN : 0x004F02F0;
                    *(uint8_t**)m = (i < 9) ? (uint8_t*)(GF_POOL_BASE + (i + 1) * 0x78)
                                            : nullptr;
                }
                *head = (uint8_t*)GF_POOL_BASE;
                if (FindGFModule() != (uint8_t*)(GF_POOL_BASE + place * 0x78)) {
                    bad++; printf("  BAD: GF module not found in slot %d\n", place);
                }
            }
            *head = (uint8_t*)0x00401000;
            check(FindGFModule() == nullptr, "an out-of-pool head must not be followed");
            *head = (uint8_t*)(GF_POOL_BASE + 4);
            check(FindGFModule() == nullptr, "a misaligned pool pointer must be rejected");
            {
                uint8_t* a = (uint8_t*)GF_POOL_BASE;
                uint8_t* b = (uint8_t*)(GF_POOL_BASE + 0x78);
                memset(a, 0, 0x78); memset(b, 0, 0x78);
                *(uint32_t*)(a + 0x08) = 0x004F02F0;
                *(uint32_t*)(b + 0x08) = 0x004F02F0;
                *(uint8_t**)a = b; *(uint8_t**)b = a;
                *head = a;
                check(FindGFModule() == nullptr, "a cyclic list must terminate");
            }
            printf("module walk: GF found in all 10 slots; bad lists all terminate\n");

            // --- the selection formula ---------------------------------------
            uint8_t* m = (uint8_t*)GF_POOL_BASE;
            memset(m, 0, 0x78);
            *(uint32_t*)(m + 0x08) = GF_STATE_FN;
            *(uint8_t**)m = nullptr;
            *head = m;

            uint8_t* list = (uint8_t*)GF_LEARN_LIST;
            uint32_t* cnt = (uint32_t*)GF_LEARN_COUNT;

            // A 16-ability GF: eleven rows on page 1, five on page 2. Ids are
            // distinct so a wrong index cannot accidentally read right.
            memset(list, 0, 24 * 8);
            for (int i = 0; i < 16; i++) list[i * 8] = (uint8_t)(20 + i);
            *cnt = 16;
            m[GFO_PAGE_ROWS + 0] = 11;
            m[GFO_PAGE_ROWS + 1] = 5;

            GFLearnSel sel;

            // Page 1, every row.
            m[GFO_PAGE] = 0;
            for (int r = 0; r < 11; r++) {
                m[GFO_PAGE_CURSOR + 0] = (uint8_t)r;
                if (!GFReadLearnSel(sel)) { bad++; printf("  BAD: read failed p0 r%d\n", r); continue; }
                if (sel.absIdx != r || sel.abilityId != 20 + r) {
                    bad++;
                    printf("  BAD: page 0 row %d -> abs %d id %d (want abs %d id %d)\n",
                           r, sel.absIdx, sel.abilityId, r, 20 + r);
                }
            }

            // **Page 2. This is the part the old reader could not reach at all**
            // -- its list came from the draw buffer, which only ever holds the
            // page on screen, and it had no page byte to offset by.
            m[GFO_PAGE] = 1;
            for (int r = 0; r < 5; r++) {
                m[GFO_PAGE_CURSOR + 1] = (uint8_t)r;
                if (!GFReadLearnSel(sel)) { bad++; printf("  BAD: read failed p1 r%d\n", r); continue; }
                if (sel.absIdx != 11 + r || sel.abilityId != 31 + r) {
                    bad++;
                    printf("  BAD: page 1 row %d -> abs %d id %d (want abs %d id %d)\n",
                           r, sel.absIdx, sel.abilityId, 11 + r, 31 + r);
                }
            }

            // The two cursors are INDEPENDENT. Page 1's cursor must not leak
            // into page 2's answer -- that is precisely what "whichever byte
            // changed" got wrong.
            m[GFO_PAGE_CURSOR + 0] = 9;
            m[GFO_PAGE_CURSOR + 1] = 2;
            m[GFO_PAGE] = 1;
            GFReadLearnSel(sel);
            check(sel.absIdx == 13 && sel.abilityId == 33,
                  "page 2 must use cursor[1], not cursor[0]");
            m[GFO_PAGE] = 0;
            GFReadLearnSel(sel);
            check(sel.absIdx == 9 && sel.abilityId == 29,
                  "page 1 must use cursor[0], not cursor[1]");

            // Past the end of the list is an EMPTY SLOT, and the engine's own
            // bound says so (0x004D3602 -> 0x004D361D writes 0). The old code
            // only reached this conclusion when the help text happened to be
            // blank, so on a screen with help text it said nothing at all.
            m[GFO_PAGE] = 1;
            m[GFO_PAGE_CURSOR + 1] = 7;          // abs 18, past count 16
            check(GFReadLearnSel(sel), "an over-the-end row must still read");
            check(sel.abilityId == -1, "past the count is an empty slot, not an ability");

            // Exactly at the boundary.
            m[GFO_PAGE_CURSOR + 1] = 5;          // abs 16 == count
            GFReadLearnSel(sel);
            check(sel.abilityId == -1, "abs == count is already past the end");
            m[GFO_PAGE_CURSOR + 1] = 4;          // abs 15, the last real ability
            GFReadLearnSel(sel);
            check(sel.abilityId == 35, "abs == count-1 is the last real ability");

            // A short list that fits on one page -- the common case, which must
            // not regress.
            *cnt = 4;
            m[GFO_PAGE] = 0;
            for (int r = 0; r < 4; r++) {
                m[GFO_PAGE_CURSOR + 0] = (uint8_t)r;
                GFReadLearnSel(sel);
                if (sel.abilityId != 20 + r) {
                    bad++; printf("  BAD: short list row %d -> id %d\n", r, sel.abilityId);
                }
            }
            m[GFO_PAGE_CURSOR + 0] = 6;
            GFReadLearnSel(sel);
            check(sel.abilityId == -1, "a short list's padded rows are empty slots");

            // Values it must refuse rather than guess at.
            m[GFO_PAGE] = 2;                     // only pages 0 and 1 exist
            check(!GFReadLearnSel(sel), "a page outside 0..1 must be refused");
            m[GFO_PAGE] = 0;
            m[GFO_PAGE_CURSOR + 0] = 12;         // > 11 rows per page
            check(!GFReadLearnSel(sel), "a row past the page must be refused");
            m[GFO_PAGE_CURSOR + 0] = 0;
            *cnt = 25;                           // a GF has at most 24 abilities
            check(!GFReadLearnSel(sel), "an impossible ability count must be refused");
            *cnt = 16;
            *head = nullptr;
            check(!GFReadLearnSel(sel), "with no GF module in the pool, no answer");

            printf("learn cursor: page*11 + cursor[page] over both pages, "
                   "independent cursors, empty slots past the count, "
                   "and refusals on every out-of-range field\n");
        }
    }

    printf("menu_gf_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
