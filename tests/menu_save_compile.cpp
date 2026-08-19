// menu_save_compile.cpp -- v0.29.0 (#88)
//
// Compile-and-run probe for src/menu_tts_save.inl's confirmation-dialog reader
// and for src/menu_dialog.inl, the shared yes/no window it reads through.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_save_compile tests/menu_save_compile.cpp
//
// WHY THIS EXISTS
//
// The #88 audit found that **"Data exists.  Overwrite?" was announced as
// nothing at all**, on both the title-screen path and the in-game Save path.
// The engine presets that dialog's cursor to the SECOND option, so a blind
// player who pressed Confirm on an occupied block, heard silence, and pressed
// Confirm again did not overwrite -- he was returned to the block list
// believing he had saved. There is no later symptom to notice.
//
// Two things therefore have to hold, and both are asserted here rather than
// left to the BAT:
//
//   1. The module is found by WALKING THE POOL for its update function, not by
//      assuming a slot. Every other Save reader still uses the fixed
//      pMenuStateA+0x1A6 / +0x21E aliases; for a dialog that decides whether
//      the save happens, a wrong base could speak "Overwrite?" over a screen
//      that has no dialog on it.
//   2. The option word is read OFF THE WINDOW, never assumed. This mod has
//      already shipped one screen that assumed Yes-then-No and met a dialog
//      that lists NO first (v0.26.2, the SeeD exam's "Really?"). The cost of
//      getting it wrong here is telling the player the opposite of what he is
//      about to confirm, which is worse than saying nothing.
//
// The 64-bit host caveat from menu_magic_compile.cpp applies unchanged: plant
// every pointer at full width.

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

static DWORD s_tick = 0;
static DWORD GetTickCount() { return s_tick; }

// Minimal Win32 surface. menu_tts_save.inl's LZSS/header scanner sweeps the
// process with VirtualQuery; it is not what this probe exercises, but it has to
// compile, and a scan that finds nothing is the correct host behaviour.
typedef size_t SIZE_T;
struct SYSTEM_INFO { void* lpMinimumApplicationAddress; void* lpMaximumApplicationAddress; };
static void GetSystemInfo(SYSTEM_INFO* si)
{ si->lpMinimumApplicationAddress = (void*)0x1000; si->lpMaximumApplicationAddress = (void*)0x1000; }
struct MEMORY_BASIC_INFORMATION { void* BaseAddress; DWORD State; DWORD Protect; SIZE_T RegionSize; };
static const DWORD MEM_COMMIT = 0x1000, PAGE_READWRITE = 4, PAGE_READONLY = 2,
                   PAGE_EXECUTE_READ = 0x20, PAGE_EXECUTE_READWRITE = 0x40;
static SIZE_T VirtualQuery(const void*, MEMORY_BASIC_INFORMATION*, SIZE_T) { return 0; }

namespace Log { void Menu(const char*, ...) {} }
namespace ScreenReader {
    char g_last[512];
    int  g_count = 0;
    bool Speak(const char* t, bool = false)
    { snprintf(g_last, sizeof(g_last), "%s", t ? t : ""); g_count++; return true; }
    bool IsSpeaking() { return false; }
}

// Same inverse-of-the-glyph-shift stub the other probes use: planting ASCII
// round-trips, so this stays a check of control flow. The encoding itself is
// tested in tests/menu_sim.cpp against bytes lifted out of the real mngrp.bin.
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) s += (char)(d[i] + 0x20);
        return s;
    }
    // The real Decode (src/ff8_text_decode.cpp) takes RAW STREAM BYTES -- text
    // byte = glyph index + 0x20 -- so on the printable range it is the identity,
    // and 0x01/0x02 come out as a space. That is what menu_dialog.inl hands it.
    std::string Decode(const uint8_t* d, size_t n = 1024) {
        std::string s;
        for (size_t i = 0; i < n; i++) {
            if (d[i] == 0x00) break;
            s += (d[i] == 0x01 || d[i] == 0x02) ? ' ' : (char)d[i];
        }
        return s;
    }
}

namespace FieldDialog {
    int SnapshotGcwBuffer(uint8_t*, size_t) { return 0; }
}

static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevCursor = 10;
static bool    s_saveDiagSnapValid = false;

#include "menu_dialog.inl"
#include "menu_tts_save.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

static void* MapAt(uintptr_t addr, size_t len)
{
    const uintptr_t pg = addr & ~(uintptr_t)0xFFF;
    const size_t    sz = ((addr + len) - pg + 0xFFF) & ~(size_t)0xFFF;
    void* p = mmap((void*)pg, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
}

// Plant a text stream the way the game stores one. On the printable range an
// FF8 text byte IS the ASCII byte (glyph index + 0x20), so this is a copy with
// the engine's 0x00 terminator on the end.
static void PlantText(uint8_t* dst, const char* ascii)
{
    size_t i = 0;
    for (; ascii[i]; i++) dst[i] = (uint8_t)ascii[i];
    dst[i] = 0x00;
}

static uint8_t g_body[512], g_opt1[64], g_opt2[64];

static void OpenWindow(const char* body, const char* o1, const char* o2)
{
    PlantText(g_body, body);
    PlantText(g_opt1, o1);
    PlantText(g_opt2, o2);
    *(uint8_t* volatile*)MDLG_BODY_PTR = g_body;
    *(uint8_t* volatile*)MDLG_OPT1_PTR = g_opt1;
    *(uint8_t* volatile*)MDLG_OPT2_PTR = g_opt2;
}

// Put the Save module in a chosen pool slot with a chosen phase and cursor.
static uint8_t* PlaceSave(int slot, int phase, int cursor)
{
    uint8_t** head = (uint8_t**)SAVE_LIST_HEAD;
    for (int i = 0; i < 10; i++) {
        uint8_t* m = (uint8_t*)(SAVE_POOL_BASE + i * 0x78);
        memset(m, 0, 0x78);
        *(uint32_t*)(m + 0x08) = (i == slot) ? SAVE_STATE_FN : 0x004F02F0;
        *(uint8_t**)m = (i < 9) ? (uint8_t*)(SAVE_POOL_BASE + (i + 1) * 0x78) : nullptr;
    }
    *head = (uint8_t*)SAVE_POOL_BASE;
    uint8_t* m = (uint8_t*)(SAVE_POOL_BASE + slot * 0x78);
    m[SDO_PHASE]      = (uint8_t)phase;
    m[SDO_DLG_CURSOR] = (uint8_t)cursor;
    return m;
}

int main()
{
    printf("menu_tts_save.inl + menu_dialog.inl compile\n");

    // The pool, the list head and the three window globals all sit in the same
    // few pages; map one combined range (overlapping MAP_FIXED_NOREPLACE calls
    // fail, which is how v0.28.0's probe host broke).
    if (!MapAt(SAVE_LIST_HEAD, (size_t)(MDLG_BODY_PTR + 0x100 - SAVE_LIST_HEAD))) {
        printf("  (could not map the pool address range -- skipping the live checks)\n");
        printf("menu_save_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }
    uint8_t** head = (uint8_t**)SAVE_LIST_HEAD;

    // ---- 1. the module walk ------------------------------------------------
    *head = nullptr;
    check(FindSaveModule() == nullptr, "an empty list must yield no module");

    for (int place = 0; place < 10; place++) {
        uint8_t* want = PlaceSave(place, 0, 0);
        uint8_t* got  = FindSaveModule();
        if (got != want) {
            bad++;
            printf("  BAD: Save in slot %d, walk returned %p (want %p)\n",
                   place, (void*)got, (void*)want);
        }
    }
    printf("module walk: found in all 10 slots against a real mapped pool\n");

    *head = (uint8_t*)0x00401000;
    check(FindSaveModule() == nullptr, "an out-of-pool head must not be followed");
    *head = (uint8_t*)(SAVE_POOL_BASE + 4);
    check(FindSaveModule() == nullptr, "a misaligned pool pointer must be rejected");
    {
        uint8_t* a = (uint8_t*)SAVE_POOL_BASE;
        uint8_t* b = (uint8_t*)(SAVE_POOL_BASE + 0x78);
        memset(a, 0, 0x78); memset(b, 0, 0x78);
        *(uint32_t*)(a + 0x08) = 0x004F02F0;
        *(uint32_t*)(b + 0x08) = 0x004F02F0;
        *(uint8_t**)a = b; *(uint8_t**)b = a;
        *head = a;
        check(FindSaveModule() == nullptr, "a cyclic list must terminate and find nothing");
    }
    printf("robustness: out-of-pool, misaligned and cyclic lists all terminate\n");

    // ---- 2. THE OVERWRITE PROMPT ------------------------------------------
    // The defect this file exists for. Phase 4, cursor preset to 1.
    OpenWindow("Data exists.   Overwrite?", "Yes", "No");

    PlaceSave(3, 4, 1);
    s_saveDlgLast[0] = '\0';
    ScreenReader::g_last[0] = '\0'; ScreenReader::g_count = 0;
    PollSaveConfirmDialog();
    check(strcmp(ScreenReader::g_last, "Data exists. Overwrite?. No") == 0,
          "phase 4 must speak the question and the option the cursor is on");
    check(ScreenReader::g_count == 1, "phase 4 must speak exactly once");

    // Sitting still says nothing. A dialog that repeats every frame is the
    // v0.22.x Magic defect, and it makes the screen unusable rather than merely
    // silent.
    PollSaveConfirmDialog();
    PollSaveConfirmDialog();
    check(ScreenReader::g_count == 1, "holding still on the dialog must stay silent");

    // Moving the cursor re-speaks, with the OTHER option.
    ((uint8_t*)(SAVE_POOL_BASE + 3 * 0x78))[SDO_DLG_CURSOR] = 0;
    PollSaveConfirmDialog();
    check(strcmp(ScreenReader::g_last, "Data exists. Overwrite?. Yes") == 0,
          "moving the cursor must re-speak with the other option");
    check(ScreenReader::g_count == 2, "moving the cursor must speak once more");

    // The padding the game lays down between columns must not be spoken as a
    // stall: three spaces in, one out.
    check(strstr(ScreenReader::g_last, "  ") == nullptr,
          "runs of padding must collapse to a single space");

    // ---- 3. THE OPTION WORDS ARE READ, NOT ASSUMED ------------------------
    // v0.26.2 shipped a screen that assumed Yes-then-No and met a dialog that
    // lists NO first. Same window, reversed labels: the reader must follow the
    // window, not its own habit.
    OpenWindow("Really?", "No", "Yes");
    PlaceSave(3, 4, 0);
    s_saveDlgLast[0] = '\0';
    PollSaveConfirmDialog();
    check(strcmp(ScreenReader::g_last, "Really?. No") == 0,
          "cursor 0 must speak the window's FIRST option whatever it says");
    ((uint8_t*)(SAVE_POOL_BASE + 3 * 0x78))[SDO_DLG_CURSOR] = 1;
    PollSaveConfirmDialog();
    check(strcmp(ScreenReader::g_last, "Really?. Yes") == 0,
          "cursor 1 must speak the window's SECOND option whatever it says");
    printf("overwrite prompt: spoken once, follows the cursor, follows the window's own labels\n");

    // ---- 4. the unformatted-folder prompt (phase 8) ------------------------
    OpenWindow("Format this GAME FOLDER?", "Yes", "No");
    PlaceSave(0, 8, 1);
    s_saveDlgLast[0] = '\0';
    ScreenReader::g_count = 0;
    PollSaveConfirmDialog();
    check(strcmp(ScreenReader::g_last, "Format this GAME FOLDER?. No") == 0,
          "phase 8 (the second 0x004C2B10 call) must be read too");

    // ---- 5. silence where there is no dialog -------------------------------
    // The window globals are never cleared by the game, so a phase gate that
    // let anything through would read a dialog that closed minutes ago.
    ScreenReader::g_count = 0;
    for (int phase = 0; phase < 18; phase++) {
        if (phase == SAVE_PHASE_OVERWRITE || phase == SAVE_PHASE_FORMAT) continue;
        PlaceSave(5, phase, 1);
        PollSaveConfirmDialog();
    }
    check(ScreenReader::g_count == 0, "no other phase may speak a dialog");

    *head = nullptr;
    PollSaveConfirmDialog();
    check(ScreenReader::g_count == 0, "with no Save module in the pool the reader must be silent");

    // Leaving and re-entering the same dialog must speak again -- the dedup key
    // is cleared when the phase leaves, not left to shadow the next visit.
    PlaceSave(5, 4, 1);
    OpenWindow("Data exists.   Overwrite?", "Yes", "No");
    ScreenReader::g_count = 0;
    PollSaveConfirmDialog();
    check(ScreenReader::g_count == 1, "re-entering the dialog must speak again");
    printf("gating: silent on every other phase, silent with no module, re-speaks on re-entry\n");

    // ---- 6. a torn window pointer must not be spoken -----------------------
    *(uint8_t* volatile*)MDLG_BODY_PTR = nullptr;
    s_saveDlgLast[0] = '\0';
    ScreenReader::g_count = 0;
    PollSaveConfirmDialog();
    check(ScreenReader::g_count == 0, "a null body pointer must produce silence, not a crash");

    // An empty body is not a dialog either.
    static uint8_t empty[4] = { 0x00, 0, 0, 0 };
    *(uint8_t* volatile*)MDLG_BODY_PTR = empty;
    PollSaveConfirmDialog();
    check(ScreenReader::g_count == 0, "an empty body must produce silence");
    printf("window: null and empty bodies produce silence\n");

    printf("menu_save_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
