// menu_config_compile.cpp -- v0.25.0 (#84)
//
// Compile probe for src/menu_tts_config.inl, and a live exercise of its reads.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_config_compile tests/menu_config_compile.cpp
//
// menu_sim.cpp proves the WORDING; this proves the plumbing -- the module walk,
// the settings block, the state gate (including the state-2 fall-through), and
// the fact that changing a value with the cursor stationary still speaks.

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
// The ATB row reports the mod's Enhanced Wait Mode, which is persisted in the
// INI by the O-key toggle in battle_tts.cpp.
static int g_ewm = 1;
namespace Config { int GetInt(const char*, int) { return g_ewm; } }

static WORD* pMenuStateA = nullptr;
static const int JUNC_ACTIVE_OFFSET  = 0x1E8;
static const int CONFIG_SUBSYSTEM_ID = 8;

static const uintptr_t MM_POOL_BASE = 0x01D76BC8;
static const uintptr_t MM_POOL_END  = 0x01D77078;
static const uintptr_t MM_LIST_HEAD = 0x01D76B48;

// The Controller row reports whether FF8's own button map is stock.
namespace ButtonMapRescue { static bool g_default = true; static bool IsDefault() { return g_default; } }

#include "menu_config_model.inl"
#include "menu_tts_config.inl"

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
    printf("menu_tts_config.inl compiles\n");

    if (!MapAt(MM_LIST_HEAD, (size_t)(MM_POOL_END - MM_LIST_HEAD)) ||
        !MapAt(CFG_SETTINGS, 0x40)) {
        printf("  (could not map the game address ranges -- skipping the live checks)\n");
        printf("menu_config_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }

    static uint8_t menuState[0x400];
    memset(menuState, 0, sizeof(menuState));
    pMenuStateA = (WORD*)menuState;
    menuState[JUNC_ACTIVE_OFFSET] = CONFIG_SUBSYSTEM_ID;

    uint8_t** head = (uint8_t**)MM_LIST_HEAD;
    *head = nullptr;
    check(FindConfigModule() == nullptr, "an empty list must yield no module");
    for (int place = 0; place < 10; place++) {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint32_t*)(m + 0x08) = (i == place) ? CFG_UPDATE_FN : 0x004F81F0;
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        *head = (uint8_t*)MM_POOL_BASE;
        if (FindConfigModule() != (uint8_t*)(MM_POOL_BASE + place * 0x78)) {
            bad++; printf("  BAD: Config module not found in pool slot %d\n", place);
        }
    }
    printf("module walk: found in all 10 pool slots\n");

    uint8_t* mod = (uint8_t*)MM_POOL_BASE;
    memset(mod, 0, 0x78);
    *(uint32_t*)(mod + 0x08) = CFG_UPDATE_FN;
    *(uint8_t**)mod = nullptr;
    *head = mod;

    uint8_t* set = (uint8_t*)CFG_SETTINGS;
    memset(set, 0, 0x20);
    set[3] = 100;                              // volume
    *(uint16_t*)(set + CFG_FLAGS_OFF) = 0;     // every toggle at its default

    // ---- the state gate, including the fall-through -------------------------
    // State 2 is one frame and falls THROUGH into state 3's body, so the first
    // interactive frame reports 2. Gating on 3 alone would silently drop the
    // arrival line whenever the poll landed on that frame -- an intermittent,
    // which is the worst kind.
    *(int8_t*)(mod + CFGO_CURSOR) = 0;
    for (int st = 0; st < 14; st++) {
        *(uint16_t*)(mod + CFGO_STATE) = (uint16_t)st;
        ResetConfigMenu();
        ScreenReader::g_count = 0;
        PollConfigMenu();
        const bool spoke = (ScreenReader::g_count > 0);
        const bool allowed = (st == CFG_STATE_LIST || st == CFG_STATE_LIST_ENTER ||
                              st == CFG_STATE_CUSTOMIZE);
        if (spoke != allowed) {
            bad++;
            printf("  BAD: state %d %s\n", st, spoke ? "spoke and should not have"
                                                     : "stayed silent and should have spoken");
        }
    }
    printf("state gate: of 14 states only 2, 3 and 7 speak -- 2 because it falls "
           "through into the list body, 5 and 11 because they are silent slides\n");

    // ---- the rows, read through the real settings block ---------------------
    *(uint16_t*)(mod + CFGO_STATE) = CFG_STATE_LIST;
    ResetConfigMenu();
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    // v0.25.2: this row no longer ends at its value. Aaron asked for a standing
    // warning here -- *"changing the game's defaults could conflict with keys
    // used by the mod"* -- so the assertion is the value AND the warning.
    check(strncmp(ScreenReader::g_last, "Controller, Normal", 18) == 0 &&
          strstr(ScreenReader::g_last, "Leave this on Normal") != nullptr,
          "row 0 with the bit clear, plus the standing warning about remapping");

    *(int8_t*)(mod + CFGO_CURSOR) = 1;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "Cursor, Initial", "row 1");

    // **A value change with the cursor stationary must still speak.** Left/Right
    // move nothing but the setting, and nothing on screen reports it -- the two
    // words are drawn side by side and only the palette says which is active.
    *(uint16_t*)(set + CFG_FLAGS_OFF) = 0x0004;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "Cursor, Memory",
           "toggling a value without moving the cursor must speak");

    // ---- the two rows the mod has taken over --------------------------------
    *(int8_t*)(mod + CFGO_CURSOR) = CFG_ROW_ATB;
    *(uint16_t*)(set + CFG_FLAGS_OFF) = 0x0000;
    g_ewm = 1;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last,
           "ATB, Active. Enhanced Wait Mode is on. Press O in battle to toggle it",
           "the ATB row reports the mod's own wait mode, which is the one that matters");

    g_ewm = 0;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    check(strstr(ScreenReader::g_last, "Enhanced Wait Mode is off") != nullptr,
          "and it tracks the mod's setting, not the game's bit");

    *(int8_t*)(mod + CFGO_CURSOR) = CFG_ROW_SOUND;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last,
           "Sound, 100 percent. Volume is controlled by the mod: "
           "F7 and F8 for music, F5 and F6 for effects",
           "the Sound row says the slider is not the one you are hearing");

    // ---- the bars, whose stored byte runs the opposite way to the bar --------
    *(int8_t*)(mod + CFGO_CURSOR) = 5;         // Battle speed
    set[0] = 0;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "Battle speed, 5 of 5, fastest",
           "a stored 0 is the FULL bar -- speaking the raw byte would invert the scale");
    set[0] = 4;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "Battle speed, 1 of 5, slowest", "and 4 is the shortest bar");
    set[0] = 2;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "Battle speed, 3 of 5", "the middle needs no adjective");
    printf("rows: toggles, the two overridden rows, and the bars whose stored byte "
           "runs the opposite way to the bar the player sees\n");

    // ---- the Customize screen ----------------------------------------------
    //
    // **This is the screen that trapped Aaron twice.** The footer that gets you
    // out -- "S to end, F to default" -- was on screen the whole time and the mod
    // never read it. So the arrival line is asserted to carry the warning, the way
    // out, the page, AND the row, in that order.
    *(uint16_t*)(mod + CFGO_STATE) = CFG_STATE_CUSTOMIZE;
    *(int8_t*)(mod + CFGO_CUST_PAGE) = 1;
    *(int8_t*)(mod + CFGO_CUST_ROW)  = 0;
    ResetConfigMenu();
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    check(strstr(ScreenReader::g_last, "Button assignment")   != nullptr &&
          strstr(ScreenReader::g_last, "Do not change these") != nullptr &&
          strstr(ScreenReader::g_last, "restore every button to default") != nullptr &&
          strstr(ScreenReader::g_last, "Cancel does nothing here") != nullptr &&
          strstr(ScreenReader::g_last, "Battle Controls")     != nullptr &&
          strstr(ScreenReader::g_last, "Confirm")             != nullptr,
          "arriving in Customize: the warning, the way out, the page, then the row");

    // Moving the cursor says the ROW ONLY. Repeating the warning on every press
    // would bury the one thing that changed.
    *(int8_t*)(mod + CFGO_CUST_ROW) = 2;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    check(strstr(ScreenReader::g_last, "View status")       != nullptr &&
          strstr(ScreenReader::g_last, "Do not change")     == nullptr &&
          strstr(ScreenReader::g_last, "Battle Controls")   == nullptr,
          "moving inside Customize speaks the row and nothing else");

    // **Two adjacent rows that read the same must BOTH speak.** Four rows on the
    // Field page are "not used"; a dedup keyed on the text would go silent
    // exactly where the player most needs to know the cursor moved at all. This
    // is why the Customize branch bypasses the whole-line guard.
    *(int8_t*)(mod + CFGO_CUST_PAGE) = 0;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();                              // page change: page + row
    *(int8_t*)(mod + CFGO_CUST_ROW) = 4;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "not used", "row 4 of the Field page is unbound");
    *(int8_t*)(mod + CFGO_CUST_ROW) = 5;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    expect(ScreenReader::g_last, "not used", "and row 5 says so again rather than going silent");

    // A page change renames every row without moving the cursor, so it has to
    // speak the page and then re-speak the row under it.
    *(int8_t*)(mod + CFGO_CUST_ROW) = 0;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    *(int8_t*)(mod + CFGO_CUST_PAGE) = 2;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    check(strstr(ScreenReader::g_last, "World Map Controls") != nullptr &&
          strstr(ScreenReader::g_last, "On, off, or examine") != nullptr,
          "changing page renames the rows, so the page and the row are both spoken");

    // The way out, on its own key, so it can be asked for again without pressing
    // anything this screen will reassign.
    g_key = '2';
    ScreenReader::g_last[0] = '\0';
    ConfigNumberKeys();
    check(strstr(ScreenReader::g_last, "restore every button to default") != nullptr &&
          strstr(ScreenReader::g_last, "Cancel does nothing here") != nullptr,
          "key 2 inside Customize repeats the way out");
    g_key = -1;
    printf("customize: the warning, the way out, per-row labels that change with the "
           "page, and two identical rows that both speak\n");

    // ---- the number keys ----------------------------------------------------
    *(uint16_t*)(mod + CFGO_STATE) = CFG_STATE_LIST;
    *(int8_t*)(mod + CFGO_CURSOR) = 2;
    g_key = '1';
    ScreenReader::g_last[0] = '\0';
    ConfigNumberKeys();
    expect(ScreenReader::g_last, "Setting 3 of 9", "key 1 locates the cursor");
    g_key = '2';
    ScreenReader::g_last[0] = '\0';
    ConfigNumberKeys();
    expect(ScreenReader::g_last, "Set ATB", "key 2 gives the game's own help line");
    g_key = '0';
    ScreenReader::g_last[0] = '\0';
    ConfigNumberKeys();
    check(strstr(ScreenReader::g_last, "Controller Normal") != nullptr &&
          strstr(ScreenReader::g_last, "Sound 100 percent") != nullptr &&
          strstr(ScreenReader::g_last, "Battle speed 3 of 5") != nullptr,
          "key 0 reads all nine settings in one pass");
    g_key = -1;
    printf("number keys: every setting, the position, and the game's help line\n");

    // ---- the Controller row warns about the trap it is --------------------
    *(uint16_t*)(mod + CFGO_STATE) = CFG_STATE_LIST;
    *(int8_t*)(mod + CFGO_CURSOR) = CFG_ROW_CONTROLLER;
    *(uint16_t*)(set + CFG_FLAGS_OFF) = 0;
    ButtonMapRescue::g_default = false;
    ResetConfigMenu();
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    check(strstr(ScreenReader::g_last, "Buttons have been remapped") != nullptr &&
          strstr(ScreenReader::g_last, "Shift F9") != nullptr,
          "a non-stock button map is reported on the Controller row with the way out");

    // **v0.25.1 asserted the opposite of this and v0.25.1 was wrong.** It held
    // that a stock map on Normal should say nothing extra, on the grounds that a
    // warning which fires when nothing is broken is noise. Aaron, after being
    // trapped a second time: *"We should also add a warning against changing the
    // controller layout, since changing the game's defaults could conflict with
    // keys used by the mod."* The warning is not about the current state, it is
    // about the state one keypress away -- so it belongs on the healthy case too,
    // which is the only case where it can still prevent anything.
    ButtonMapRescue::g_default = true;
    ScreenReader::g_last[0] = '\0';
    PollConfigMenu();
    check(strncmp(ScreenReader::g_last, "Controller, Normal", 18) == 0 &&
          strstr(ScreenReader::g_last, "Leave this on Normal") != nullptr &&
          strstr(ScreenReader::g_last, "Shift F9") != nullptr,
          "and a stock map still warns, because the damage is done by walking in");
    printf("controller row: reports a remapped pad, and warns before the trap as "
           "well as after\n");

    printf("menu_config_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
