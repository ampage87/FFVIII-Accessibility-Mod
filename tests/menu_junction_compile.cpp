// menu_junction_compile.cpp -- v0.23.2 (#82)
//
// Compile probe for src/menu_tts_junction_stats.inl, and a live exercise of the
// reads it makes. No host harness builds menu_tts.cpp, so this is the only
// pre-MSVC syntax and type check the Junction hook gets.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_junction_compile tests/menu_junction_compile.cpp
//
// tests/menu_sim.cpp proves the WORDING. This file proves the plumbing: that
// the poll gates on the right state, that FillJunctionView pulls each field
// from the offset it claims, that the character id comes from the state
// machine's own field, and that the ability readouts walk the game's arrays
// rather than a reconstruction. It cannot prove an address is right -- the
// fixtures are written at the addresses the mod believes in, so a wrong belief
// is consistent with itself here. That is what the BAT is for.
//
// Same 64-bit caveat as menu_magic_compile.cpp: pointer fields are wider on the
// host, so any planted pointer is written at full width.

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

// Number keys are driven explicitly rather than by the keyboard.
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
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) s += (char)(d[i] + 0x20);
        return s;
    }
}

// ---- the host translation unit's symbols the .inl expects ------------------
static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevCursor = 0;
static const int JUNC_ACTIVE_OFFSET = 0x1E8;

static uint8_t g_juncSelected = 0xFF;
static uint8_t GetJuncSelectedCharIdx() { return g_juncSelected; }
// menu_tts_junction.inl sets this when the char-select screen speaks a name; the
// character watcher consumes it to tell a confirm from an L1/R1 switch.
static uint8_t s_juncCharSelSpoke = 0xFF;

static const char* const PROBE_ABIL[] = {
    "None", "HP-J", "Str-J", "Vit-J", "Mag-J", "Spr-J", "Spd-J", "Eva-J", "Hit-J",
    "Luck-J", "Elem-Atk-J", "ST-Atk-J", "Elem-Def-J", "ST-Def-J"
};
static const char* GetAbilityName(uint8_t id)
{
    if (id < sizeof(PROBE_ABIL)/sizeof(PROBE_ABIL[0])) return PROBE_ABIL[id];
    if (id == 39) return "HP plus 20%";
    if (id == 58) return "Mug";
    if (id == 92) return "Haggle";
    if (id == 115) return "Card Mod";
    return "Unknown";
}

#include "menu_magic_model.inl"
#include "menu_junction_model.inl"

// menu_tts_magic.inl owns MagicCharName; the probe supplies its own so this
// file stays a check of the Junction hook alone.
static const char* MagicCharName(uint8_t idx)
{
    static const char* N[] = { "Squall","Zell","Irvine","Quistis","Rinoa","Selphie","Seifer","Edea" };
    return (idx < 8) ? N[idx] : "Member";
}

#include "menu_tts_junction_stats.inl"

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
    printf("menu_tts_junction_stats.inl compiles\n");

    // The character array and the live stat block share a region; the scratch,
    // the ability union and the two lists share another.
    if (!MapAt(JS_CHAR_BASE, (size_t)(JS_STAT_PREVIEW + 0x200 - JS_CHAR_BASE)) ||
        !MapAt(JS_ABIL_CMD_LIST, (size_t)(JS_SCRATCH + 28 * 8 - JS_ABIL_CMD_LIST))) {
        printf("  (could not map the game address ranges -- skipping the live checks)\n");
        printf("menu_junction_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }

    static uint8_t menuState[0x800];
    memset(menuState, 0, sizeof(menuState));
    pMenuStateA = (WORD*)menuState;

    uint8_t* pm = menuState;
    pm[JUNC_ACTIVE_OFFSET] = 17;
    s_prevCursor = 0;

    // A character: id 3 (Quistis), Curaga on Str-J, a stock of magic.
    const uint8_t CHAR_ID = 3;
    pm[JSO_CHAR_ID] = CHAR_ID;
    uint8_t* ch = (uint8_t*)(JS_CHAR_BASE + CHAR_ID * JS_CHAR_STRIDE);
    memset(ch, 0, JS_CHAR_STRIDE);
    ch[JS_JUNCTION_OFF + JSLOT_STR] = 23;                 // Curaga
    ch[JS_MAGICS_OFF + 0] = 23; ch[JS_MAGICS_OFF + 1] = 47;
    ch[JS_MAGICS_OFF + 2] = 21; ch[JS_MAGICS_OFF + 3] = 9;
    ch[JS_CHR_COMMANDS + 0] = 1;  ch[JS_CHR_COMMANDS + 1] = 0; ch[JS_CHR_COMMANDS + 2] = 2;
    ch[JS_CHR_ABILITIES + 0] = 39; ch[JS_CHR_ABILITIES + 1] = 0;
    ch[JS_CHR_ABILITIES + 2] = 0;  ch[JS_CHR_ABILITIES + 3] = 58;

    uint8_t* after  = (uint8_t*)JS_STAT_PREVIEW;
    uint8_t* before = (uint8_t*)JS_STAT_BASE;
    memset(after, 0, 0x200); memset(before, 0, 0x200);
    *(uint16_t*)(after  + JS_STAT_OFF[0]) = 1837;
    *(uint16_t*)(before + JS_STAT_OFF[0]) = 1837;
    after[JS_STAT_OFF[1]] = 68; before[JS_STAT_OFF[1]] = 42;   // Str 42 -> 68
    for (int e = 0; e < 8; e++) {
        *(uint16_t*)(after  + JS_ELEM_DEF_OFF + e*2) = 800;
        *(uint16_t*)(before + JS_ELEM_DEF_OFF + e*2) = 800;
    }
    for (int s = 0; s < 13; s++) { after[JS_ST_DEF_OFF + s] = 100; before[JS_ST_DEF_OFF + s] = 100; }

    *(uint32_t*)(JS_SCRATCH + CHAR_ID * JS_SCRATCH_STRIDE) = 0x1FFFF;   // every row unlocked
    *(uint32_t*)(menuState + JSO_ELIG_MASK) = 0xFFFFFFFFu;

    // ---- the character id comes from the state machine's own field ---------
    g_juncSelected = 6;                       // a deliberately different fallback
    check(JsCharId() == CHAR_ID, "the char id must come from +0x261, not the cached fallback");
    pm[JSO_CHAR_ID] = 0xFF;
    check(JsCharId() == 6, "an out-of-range +0x261 falls back to the cached char-select value");
    pm[JSO_CHAR_ID] = CHAR_ID;

    // ---- the poll speaks only in the steady states -------------------------
    pm[JSO_GRID_CUR] = 11;                    // Strength
    for (int st = 0; st < 74; st++) {
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)st;
        ResetJunctionStats();
        ScreenReader::g_count = 0;
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        const bool spoke = (ScreenReader::g_count > 0);
        const bool allowed = (st == JUNC_STATE_GRID || st == JUNC_STATE_MAGIC);
        if (spoke != allowed) {
            bad++;
            printf("  BAD: state %d %s\n", st, spoke ? "spoke and should not have"
                                                     : "stayed silent and should have spoken");
        }
    }
    check(true, "");
    printf("state gate: of all 74 states, only 52 and 59 speak -- 37, the "
           "slide-in the first draft used, is silent\n");

    // ---- the grid line, end to end -----------------------------------------
    *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_GRID;
    ResetJunctionStats();
    ScreenReader::g_last[0] = '\0';
    PollJunctionStats();
    expect(ScreenReader::g_last, "Junction, Quistis. Strength, Curaga, 68",
           "the grid line, read through the real offsets, named for its character");

    // The same cursor twice must not repeat; a move must.
    ScreenReader::g_count = 0;
    PollJunctionStats();
    check(ScreenReader::g_count == 0, "an unchanged line must not be repeated");
    pm[JSO_GRID_CUR] = 12;                    // Vitality
    ScreenReader::g_last[0] = '\0';
    PollJunctionStats();
    expect(ScreenReader::g_last, "Vitality, empty, 0", "a cursor move re-announces");

    // ---- the magic list, with the game's eligibility mask -------------------
    pm[JSO_GRID_CUR] = 11;
    pm[JSO_MAGIC_CUR] = 0;
    *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_MAGIC;
    ScreenReader::g_last[0] = '\0';
    PollJunctionStats();
    expect(ScreenReader::g_last, "Choose magic for Strength. Curaga, quantity 47, Str 42 to 68",
           "the magic list names the row it is filling");

    *(uint32_t*)(menuState + JSO_ELIG_MASK) = 0;       // nothing helps this row
    pm[JSO_MAGIC_CUR] = 1;
    ScreenReader::g_last[0] = '\0';
    PollJunctionStats();
    expect(ScreenReader::g_last, "Cure, quantity 9, no effect here",
           "an ineligible candidate is called out rather than read as usable");
    *(uint32_t*)(menuState + JSO_ELIG_MASK) = 0xFFFFFFFFu;
    pm[JSO_MAGIC_CUR] = 0;
    printf("live reads: the grid and the magic list compose correctly from the "
           "mod's own offsets, and repeats are suppressed\n");

    // ---- PAGING MUST NOT RE-ANNOUNCE THE HEADER ---------------------------
    // Aaron: *"As I moved through the page of spells to junction I heard
    // repetitive announcement on each page."* Paging the list is 59 -> 60
    // (0x004DED18, page left) or 62 (page right) -> 59, and recording the
    // transient state made the return look like a fresh arrival. **The state
    // must be REPLAYED, not constructed** -- a test that sets 59 twice in a row
    // passes while the game is doing 59, 60, 59.
    {
        ResetJunctionStats();
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_MAGIC;
        pm[JSO_MAGIC_CUR] = 0;
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();                             // arrival: header expected
        check(strstr(ScreenReader::g_last, "Choose magic") != nullptr,
              "arriving in the list speaks the header once");

        int headers = 0, lines = 0;
        for (int page = 1; page < 8; page++) {
            *(uint16_t*)(menuState + JSO_STATE) = 60;    // page-left transient
            PollJunctionStats();
            *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_MAGIC;
            pm[JSO_MAGIC_CUR] = (uint8_t)(page * 4);
            ScreenReader::g_last[0] = '\0';
            ScreenReader::g_count = 0;
            PollJunctionStats();
            if (ScreenReader::g_count) {
                lines++;
                if (strstr(ScreenReader::g_last, "Choose magic")) headers++;
            }
        }
        check(headers == 0, "no page turn may repeat the header");
        check(lines == 7, "every page turn must still speak its line, even an identical one");
        printf("paging: the header is spoken on arrival and never again -- seven "
               "page turns through the real 59->60->59 chain, %d lines, %d headers\n",
               lines, headers);
        ResetJunctionStats();
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_MAGIC;
        pm[JSO_MAGIC_CUR] = 0;
    }

    // ---- STATUS ATTACK, ASSEMBLED THE WAY THE GAME ASSEMBLES IT -----------
    // Aaron: *"Junctioning to ST-Atk doesn't seem to announce any value."* The
    // mask is seven bits at +0x1B4 plus six in the status word at +0x18C, and
    // Sleep -- the likeliest ST-Atk junction there is -- lives entirely in the
    // second one, so the old u16-at-+0x1B4 read returned nothing at all.
    {
        ch[JS_JUNCTION_OFF + JSLOT_ST_ATK] = 40;         // Sleep junctioned
        after[JS_STATK_LOW7] = 0x00;
        *(uint32_t*)(after + JS_STATUS_WORD) = 0x0001;   // Sleep
        *(uint16_t*)(after + JS_STATK_RAW)   = 130;      // 30 percent
        pm[JSO_GRID_CUR] = 0;                            // the ST-Atk cell
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_GRID;
        ResetJunctionStats();
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        expect(ScreenReader::g_last, "Junction, Quistis. Status attack, Sleep, Sleep 30 percent",
               "a Sleep status-attack junction must speak its value");

        after[JS_STATK_LOW7] = 0x20;                     // Berserk, in the low byte
        *(uint32_t*)(after + JS_STATUS_WORD) = 0;
        *(uint16_t*)(after + JS_STATK_RAW)   = 166;
        ch[JS_JUNCTION_OFF + JSLOT_ST_ATK] = 46;
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        check(strstr(ScreenReader::g_last, "Berserk 66 percent") != nullptr,
              "and so must one that does live in the low byte");
        printf("status attack: both halves of the assembled mask reach the "
               "announcement, and the percentage is offset by 100\n");

        // ---- THE DROP MUST COME OFF THE BASELINE BLOCK, NOT BE INFERRED ----
        // Aaron: *"the mod said Confuse 8% or similar, but neglected to mention
        // the drop in the Stop status."* The outgoing side lives in the baseline
        // block at 0x01D8B3B0, and it is assembled from ITS OWN two fields --
        // reading the live block's +0x18C for both would report no drop at all.
        before[JS_STATK_LOW7] = 0x00;
        *(uint32_t*)(before + JS_STATUS_WORD) = 0x0008;   // Stop was junctioned
        *(uint16_t*)(before + JS_STATK_RAW)   = 140;      // at 40 percent
        after [JS_STATK_LOW7] = 0x00;
        *(uint32_t*)(after  + JS_STATUS_WORD) = 0x4000;   // Confuse would replace it
        *(uint16_t*)(after  + JS_STATK_RAW)   = 108;      // at 8 percent
        pm[JSO_GRID_CUR] = 0;                             // the ST-Atk cell
        pm[JSO_MAGIC_CUR] = 0;
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_MAGIC;
        ResetJunctionStats();
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        check(strstr(ScreenReader::g_last, "Stop 40 to 0 percent") != nullptr,
              "the displaced status must be read off the BASELINE block");
        check(strstr(ScreenReader::g_last, "Confuse 0 to 8 percent") != nullptr,
              "and the incoming one off the live block");
        printf("the trade: both sides of a status-attack swap come from their own "
               "block, so a drop cannot go missing\n");
        *(uint32_t*)(before + JS_STATUS_WORD) = 0;
        *(uint16_t*)(before + JS_STATK_RAW)   = 0;
        *(uint32_t*)(after  + JS_STATUS_WORD) = 0;
        *(uint16_t*)(after  + JS_STATK_RAW)   = 0;
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_GRID;
        ch[JS_JUNCTION_OFF + JSLOT_ST_ATK] = 0;
        after[JS_STATK_LOW7] = 0;
        *(uint16_t*)(after + JS_STATK_RAW) = 0;
        pm[JSO_GRID_CUR] = 11;
    }

    // ---- L1 / R1 SWAPS THE CHARACTER, AND NOTHING SAID SO ------------------
    // Aaron: *"Junction here doesn't announce the name of the new selected
    // character when Q and E are pressed."* The BAT log has the switch as a bare
    // `+0x261: 2 -> 0` at the action row with no announcement anywhere near it.
    {
        // (a) On the ACTION ROW -- a state this file otherwise never speaks in --
        //     the watcher names the new character on its own.
        ResetJunctionStats();
        *(uint16_t*)(menuState + JSO_STATE) = 3;      // the action row
        pm[JSO_CHAR_ID] = 2;
        PollJunctionStats();                          // baseline, must be silent
        ScreenReader::g_count = 0; ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        check(ScreenReader::g_count == 0, "the first sample of the character is a baseline, not news");

        pm[JSO_CHAR_ID] = 0;                          // L1/R1 -> Squall
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        expect(ScreenReader::g_last, "Squall", "the switch must name the new character");

        // (b) Confirming out of char select changes the SAME byte, and there the
        //     char-select screen has just said the name in full. Silent.
        pm[JSO_CHAR_ID] = 0; s_juncCharSelSpoke = 4;  // char select just said "Rinoa"
        ScreenReader::g_count = 0;
        pm[JSO_CHAR_ID] = 4;
        PollJunctionStats();
        check(ScreenReader::g_count == 0, "confirming the character just announced must stay silent");

        // ...and the marker is CONSUMED, so switching away and back speaks both
        // times. Keying on recency instead would have swallowed the second.
        ScreenReader::g_last[0] = '\0';
        pm[JSO_CHAR_ID] = 0;
        PollJunctionStats();
        expect(ScreenReader::g_last, "Squall", "switching away speaks");
        ScreenReader::g_last[0] = '\0';
        pm[JSO_CHAR_ID] = 4;
        PollJunctionStats();
        expect(ScreenReader::g_last, "Rinoa", "and switching back speaks again -- the marker was consumed");

        // (c) On the grid the name replaces the arrival header, so the switch and
        //     the new row are ONE utterance rather than two.
        *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_GRID;
        pm[JSO_CHAR_ID] = CHAR_ID;
        pm[JSO_GRID_CUR] = 11;
        ResetJunctionStats();
        ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        expect(ScreenReader::g_last, "Junction, Quistis. Strength, Curaga, 68",
               "arriving at the grid names the character");
        pm[JSO_CHAR_ID] = 0;                          // switch while the row is up
        ScreenReader::g_count = 0; ScreenReader::g_last[0] = '\0';
        PollJunctionStats();
        check(ScreenReader::g_count == 1, "a switch on the grid is ONE utterance, not two");
        // Squall's fixture is empty, so his Str row is locked -- which is the
        // point: the row belongs to the CHARACTER, and the switch changed it.
        expect(ScreenReader::g_last, "Squall. Strength, locked",
               "and the name leads it instead of the arrival header");
        printf("character switch: named on the action row, folded into the header on "
               "the grid, and silent when it is only a char-select confirm\n");
        pm[JSO_CHAR_ID] = CHAR_ID;
        s_juncCharSelSpoke = 0xFF;
        ResetJunctionStats();
        PollJunctionStats();          // re-arm: the number keys are gated on s_jsActive
    }

    // ---- the ability readouts ----------------------------------------------
    *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_ABIL_LIST;
    g_key = '0';
    ScreenReader::g_last[0] = '\0';
    JunctionNumberKeys();
    expect(ScreenReader::g_last,
           "Commands. HP-J, empty, Str-J. Abilities. HP plus 20%, empty, empty, Mug",
           "key 0 reads the equipped loadout, empties included");

    // Party abilities: ids >= 83 in the union, which no list on screen carries.
    uint32_t* uni = (uint32_t*)JS_ABIL_UNION;
    memset(uni, 0, 16);
    uni[92 >> 5]  |= (1u << (92 & 31));       // Haggle
    uni[115 >> 5] |= (1u << (115 & 31));      // Card Mod
    uni[58 >> 5]  |= (1u << (58 & 31));       // Mug -- equippable, must NOT appear
    g_key = '1';
    ScreenReader::g_last[0] = '\0';
    JunctionNumberKeys();
    expect(ScreenReader::g_last, "Party abilities. Haggle, Card Mod",
           "party abilities list only the ids the screen never shows");

    memset(uni, 0, 16);
    ScreenReader::g_last[0] = '\0';
    JunctionNumberKeys();
    expect(ScreenReader::g_last, "No party abilities from the junctioned GFs",
           "an empty union gets an answer, not silence");

    // On the grid the same digit means something else entirely.
    *(uint16_t*)(menuState + JSO_STATE) = (uint16_t)JUNC_STATE_GRID;
    g_key = '0';
    ScreenReader::g_last[0] = '\0';
    JunctionNumberKeys();
    expect(ScreenReader::g_last, "Quistis, HP 1837",
           "the digits are per-screen, exactly as they are on the Status screen");
    g_key = -1;
    printf("number keys: 0 and 1 mean loadout and party abilities on the ability "
           "screens, and character and stats on the grid\n");

    // ---- the ability list arrays -------------------------------------------
    // The cursor the game moves is module[0x52 + kind]: +0x271 for commands and
    // +0x272 for character abilities. The mod read only the first, which is
    // exactly why the character list never spoke.
    check(JSO_ABIL_CUR + 1 == 0x271, "the command list cursor is +0x271");
    check(JSO_ABIL_CUR + 2 == 0x272, "the character list cursor is +0x272");
    {
        uint8_t* cmd = (uint8_t*)JS_ABIL_CMD_LIST;
        uint8_t* chr = (uint8_t*)JS_ABIL_CHAR_LIST;
        memset(cmd, 0, 32); memset(chr, 0, 32);
        cmd[0] = 20; cmd[2] = 22;
        chr[0] = 39; chr[2] = 58;
        *(uint32_t*)JS_ABIL_CMD_COUNT  = 2;
        *(uint8_t*) JS_ABIL_CHAR_COUNT = 2;
        check(cmd[1 * 2] == 22, "the lists are {id, nameIdx} pairs, so the stride is two");
        check(chr[1 * 2] == 58, "and the character list has the same shape");
    }
    printf("ability lists: the cursor is per-list and the entries are the game's "
           "own {id, nameIdx} pairs\n");

    printf("menu_junction_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
