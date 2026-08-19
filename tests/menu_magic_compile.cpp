// menu_magic_compile.cpp -- v0.22.0 (#81)
//
// Compile probe for src/menu_tts_magic.inl. No host harness builds menu_tts.cpp,
// so this is the only pre-MSVC syntax and type check the Magic hook gets, and it
// is the difference between Aaron waking up to a build or to a compiler error.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_magic_compile tests/menu_magic_compile.cpp
//
// It also RUNS the module walk against a real, mapped copy of the module pool at
// the game's own address, so FindMagicModule's pointer arithmetic, bounds check
// and stride check are exercised rather than merely parsed. What it cannot check
// is whether 0x004F02F0 is really the Magic state machine -- that is what the
// BAT is for.
//
// One honest limitation: the host is 64-bit and the game is 32-bit, so a pointer
// field written here occupies eight bytes where the real module has four. That
// makes this a check of CONTROL FLOW and of the sentinel/bounds handling, not of
// the module's byte layout. Every pointer the probe plants is therefore written
// at full width; writing only the low half splices two addresses together and
// segfaults in a way that looks like a mod bug and is not one.

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

// The mod builds with MSVC SEH. On the host, __try/__except degrade to plain
// blocks -- the same trick minigame_bgbtl_compile.cpp uses. Standard headers
// must come BEFORE these macros: libstdc++ has its own `__try { } __catch(...)`
// and redefining __try turns it into a syntax error.
// libstdc++'s <string> pulls in exception_defines.h, which defines __try/__catch
// for its own use. Undefine before redefining, or the redefinition warns and the
// two meanings sit on top of each other.
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
    bool Speak(const char* t, bool = false) { snprintf(g_last, sizeof(g_last), "%s", t ? t : ""); return true; }
    bool IsSpeaking() { return false; }
}

// v0.22.2: the help-bar reader converts text-stream bytes to GLYPH INDICES
// (subtracting 0x20) and then hands them to the mod's glyph-table decoder. This
// stub is that decoder's inverse, so planting ASCII round-trips and the probe
// stays a check of CONTROL FLOW -- the pointer read, the sentinels, the fault
// path, declining off-screen.
//
// **The encoding itself is NOT tested here.** It is tested in tests/menu_sim.cpp
// against byte sequences lifted verbatim out of the real mngrp.bin, which is the
// only fixture that can actually catch the shift being wrong. A stub cannot
// falsify its own convention.
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) s += (char)(d[i] + 0x20);
        return s;
    }
    // v0.29.0: menu_dialog.inl decodes the shared window's text with this one.
    // Unlike DecodeMenuText above, the real Decode takes RAW STREAM BYTES, so on
    // the printable range it is the identity. Getting that backwards here would
    // pin a convention the mod does not use; menu_save_compile.cpp is where the
    // window reader is actually exercised.
    std::string Decode(const uint8_t* d, size_t n = 1024) {
        std::string s;
        for (size_t i = 0; i < n; i++) {
            if (d[i] == 0x00) break;
            s += (d[i] == 0x01 || d[i] == 0x02) ? ' ' : (char)d[i];
        }
        return s;
    }
}

// menu_tts_magic.inl reads these from its host translation unit.
static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevCursor = 2;

#include "menu_dialog.inl"
#include "menu_magic_model.inl"
#include "menu_tts_magic.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

// Map real pages over the pool and the list head so the walk runs for real.
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
    printf("menu_tts_magic.inl compiles\n");

    // The pool (0x01D76BC8..0x01D77078) and the list head (0x01D76B48) sit in
    // the same 4 KB page region; map from the head through the pool end.
    if (!MapAt(MM_LIST_HEAD, (size_t)(MM_POOL_END - MM_LIST_HEAD))) {
        printf("  (could not map the pool address range -- skipping the live walk)\n");
        printf("menu_magic_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }

    uint8_t** head = (uint8_t**)MM_LIST_HEAD;

    // 1. Empty list -> no module, no crash.
    *head = nullptr;
    check(FindMagicModule() == nullptr, "an empty list must yield no module");

    // 2. Magic in each of the ten slots, reached through a chain of decoys.
    for (int place = 0; place < 10; place++) {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(MM_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint32_t*)(m + 0x08) = (i == place) ? MM_MAGIC_STATE_FN : 0x004F81F0;
            // next -> the following slot, terminating after the last
            *(uint8_t**)m = (i < 9) ? (uint8_t*)(MM_POOL_BASE + (i + 1) * 0x78) : nullptr;
        }
        *head = (uint8_t*)MM_POOL_BASE;
        uint8_t* got = FindMagicModule();
        uint8_t* want = (uint8_t*)(MM_POOL_BASE + place * 0x78);
        if (got != want) {
            bad++;
            printf("  BAD: Magic in slot %d, walk returned %p (want %p)\n",
                   place, (void*)got, (void*)want);
        }
    }
    printf("module walk: found in all 10 slots against a real mapped pool\n");

    // 3. A pointer outside the pool must terminate the walk, not follow it.
    *head = (uint8_t*)0x00401000;
    check(FindMagicModule() == nullptr, "an out-of-pool head must not be followed");

    // 4. A misaligned pointer inside the pool must be rejected -- this is the
    //    check that stops a half-torn pointer being read as a module.
    *head = (uint8_t*)(MM_POOL_BASE + 4);
    check(FindMagicModule() == nullptr, "a misaligned pool pointer must be rejected");

    // 5. A cycle must terminate. Without the hop cap this hangs the game thread.
    {
        uint8_t* a = (uint8_t*)MM_POOL_BASE;
        uint8_t* b = (uint8_t*)(MM_POOL_BASE + 0x78);
        memset(a, 0, 0x78); memset(b, 0, 0x78);
        *(uint32_t*)(a + 0x08) = 0x004F81F0;
        *(uint32_t*)(b + 0x08) = 0x004F81F0;
        *(uint8_t**)a = b; *(uint8_t**)b = a;
        *head = a;
        check(FindMagicModule() == nullptr, "a cyclic list must terminate and find nothing");
        printf("robustness: out-of-pool, misaligned and cyclic lists all "
               "terminate without a module\n");
    }

    // 6. **THE HELP BAR.** Aaron pressed "/" on the Magic screen and got
    //    nothing. The reader must speak the module's cached string, and must
    //    treat the game's two "no text" sentinels as no text rather than
    //    dereferencing them.
    {
        uint8_t* m = (uint8_t*)MM_POOL_BASE;
        memset(m, 0, 0x78);
        *(uint32_t*)(m + 0x08) = MM_MAGIC_STATE_FN;
        *(uint8_t**)m = nullptr;
        *head = m;

        static const char* TXT = "Use magic";
        *(const char**)(m + 0x24) = TXT;
        ScreenReader::g_last[0] = '\0';
        check(AnnounceMagicHelpText(), "the reader must claim the key on the Magic screen");
        check(strcmp(ScreenReader::g_last, "Use magic") == 0,
              "the help bar should speak the module's cached string");

        // Write the FULL pointer width. Writing only the low 32 bits leaves the
        // high half of the previous pointer in place, and the reader then
        // dereferences a spliced address -- which is a host artefact, not a mod
        // bug, but it segfaults the probe and looks like one.
        *(const uint8_t**)(m + 0x24) = nullptr;
        ScreenReader::g_last[0] = '\0';
        AnnounceMagicHelpText();
        check(strcmp(ScreenReader::g_last, "No help text") == 0, "NULL means no text");

        *(const uint8_t**)(m + 0x24) = (const uint8_t*)(uintptr_t)0x01D7714C;  // getter fallback
        ScreenReader::g_last[0] = '\0';
        AnnounceMagicHelpText();
        check(strcmp(ScreenReader::g_last, "No help text") == 0,
              "the 0x01D7714C sentinel must not be dereferenced");

        *(const uint8_t**)(m + 0x24) = (const uint8_t*)(uintptr_t)0x01CFF84C;  // empty-string constant
        ScreenReader::g_last[0] = '\0';
        AnnounceMagicHelpText();
        check(strcmp(ScreenReader::g_last, "No help text") == 0,
              "the 0x01CFF84C sentinel must not be dereferenced");

        // Off the Magic screen the reader must decline, so the existing GCW
        // scrape still runs for every other menu.
        *head = nullptr;
        check(!AnnounceMagicHelpText(), "off the Magic screen the reader must decline");
        printf("help bar: speaks the module's string, handles both \"no text\" "
               "sentinels, and declines elsewhere\n");
    }

    printf("menu_magic_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
