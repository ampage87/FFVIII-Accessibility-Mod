// field_pause_test.cpp -- the field freeze, on its own terms.
//
//   g++ -std=c++17 -O0 -Isrc -Itests -o field_pause_test tests/field_pause_test.cpp
//
// WHY THIS EXISTS SEPARATELY FROM disc3_wiring_compile
// ----------------------------------------------------
// That probe checks the space rescue's USE of the freeze. This one checks the
// freeze's own contract, because a byte written over the game's main loop is
// the most dangerous thing in this mod: a restore that does not happen leaves
// the field frozen with no way out but closing the game.
//
// So the five rules in field_pause.inl each get an assertion here, against a
// real page mapped at the game's own address with field_main's real opening
// bytes in it -- the patch and the restore are actual writes this file reads
// back, not a flag a stub set.

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/mman.h>

typedef unsigned long DWORD;
typedef void*         LPVOID;
#define PAGE_EXECUTE_READWRITE 0x40
#define __try       try
#define __except(x) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1

static DWORD g_tick = 100000;
static DWORD GetTickCount() { return g_tick; }

static int  g_vprotCalls = 0;
static bool g_vprotFails = false;
static bool VirtualProtect(LPVOID, size_t, DWORD, DWORD* old)
{
    g_vprotCalls++;
    if (old) *old = PAGE_EXECUTE_READWRITE;
    return !g_vprotFails;
}

static std::vector<std::string> g_logged;
namespace Log {
    void Field(const char* fmt, ...)
    {
        char b[1024]; va_list ap; va_start(ap, fmt);
        vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        g_logged.push_back(b);
    }
}

static const uintptr_t FM_PAGE = 0x00471000u;
static const uintptr_t FM_ADDR = 0x00471F70u;
namespace FF8Addresses { uint32_t field_main_fn = 0; }

#include "field_pause.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }
static uint8_t b0() { return *(volatile uint8_t*)FM_ADDR; }
static bool logged(const char* needle)
{ for (auto& l : g_logged) if (l.find(needle) != std::string::npos) return true; return false; }

int main()
{
    void* p = mmap((void*)FM_PAGE, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void*)FM_PAGE) { std::printf("FATAL: mmap\n"); return 2; }
    memset(p, 0x90, 0x2000);
    // field_main's own state machine byte, which Engage logs. Mapped because a
    // faulting read here is a real segfault on the host: the winshim's __except
    // is a C++ catch, and catch(...) does not catch a bad address.
    void* st = mmap((void*)0x01CE4000u, 0x2000, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (st != (void*)0x01CE4000u) { std::printf("FATAL: mmap state\n"); return 2; }
    memset(st, 0, 0x2000);
    *(volatile uint32_t*)0x01CE4A64u = 5;
    // field_main's real opening: mov eax, [0x01CE4A64] ; push ebx ; push ebp
    static const uint8_t opening[] = { 0xA1, 0x64, 0x4A, 0xCE, 0x01, 0x53, 0x55 };
    memcpy((void*)FM_ADDR, opening, sizeof opening);
    std::printf("field_pause_test\n");

    // RULE 2: never patch blind. With no address resolved there is nothing to
    // be available, and nothing to write.
    FF8Addresses::field_main_fn = 0;
    check(!FieldPause::Available(), "an unresolved field_main is not available");
    check(!FieldPause::Engage("probe"), "and Engage refuses");
    check(b0() == 0xA1, "and wrote nothing");

    FF8Addresses::field_main_fn = (uint32_t)FM_ADDR;

    // RULE 1: never patch what we did not recognise.
    for (int i = 0; i < 5; i++) {
        uint8_t save = ((volatile uint8_t*)FM_ADDR)[i];
        ((volatile uint8_t*)FM_ADDR)[i] = (uint8_t)(save ^ 0xFF);
        check(!FieldPause::Available(),
              "**one wrong byte anywhere in the signature and it is not available**");
        check(!FieldPause::Engage("probe"), "and Engage refuses");
        ((volatile uint8_t*)FM_ADDR)[i] = save;
    }
    check(FieldPause::Available(), "the real opening IS available");

    // The patch, and the read-back.
    g_logged.clear();
    check(FieldPause::Engage("probe: first"), "Engage takes");
    check(b0() == 0xC3, "**one byte, 0xC3, at field_main's entry**");
    check(FieldPause::IsEngaged(), "and it knows");
    check(g_vprotCalls > 0, "through VirtualProtect");
    check(logged("ENGAGED"), "and it says so in the log");

    // Idempotent: the space rescue calls Engage on every tick while its screen
    // is up, and a second patch would record 0xC3 as the byte to restore.
    const int before = g_vprotCalls;
    for (int i = 0; i < 20; i++) FieldPause::Engage("probe: repeat");
    check(g_vprotCalls == before, "**a second Engage writes nothing**");

    // RULE 5: restore the ORIGINAL byte, not a constant.
    FieldPause::Release("probe: first");
    check(b0() == 0xA1, "**Release puts back what was there**");
    check(!FieldPause::IsEngaged(), "and lets go");
    for (int i = 0; i < 5; i++)
        check(((volatile uint8_t*)FM_ADDR)[i] == opening[i],
              "and the whole signature reads as it did before");

    // Release is idempotent too -- every exit path calls it, and several of
    // them can fire on the same tick.
    const int before2 = g_vprotCalls;
    for (int i = 0; i < 20; i++) FieldPause::Release("probe: repeat");
    check(g_vprotCalls == before2, "**a Release with nothing held writes nothing**");

    // RULE 4: the watchdog does not need the caller.
    check(FieldPause::Engage("probe: watchdog"), "engaged for the watchdog");
    g_tick += 1000;
    FieldPause::Watchdog();
    check(b0() == 0xC3, "**the watchdog leaves a young freeze alone**");
    g_tick += 601000;
    g_logged.clear();
    FieldPause::Watchdog();
    check(b0() == 0xA1,
          "**and force-releases one that has outlived the ceiling** -- this is "
          "the guarantee that a bug in the owner cannot strand the player in a "
          "field that never advances");
    check(logged("WATCHDOG"), "and names itself when it does");
    check(!FieldPause::IsEngaged(), "state agrees with the byte");

    // A refused page protection must not leave the module thinking it holds
    // something it does not.
    g_vprotFails = true;
    g_logged.clear();
    check(!FieldPause::Engage("probe: no protect"),
          "**a VirtualProtect that fails is an Engage that fails**");
    check(!FieldPause::IsEngaged(), "and nothing is held");
    check(b0() == 0xA1, "and nothing was written");
    check(logged("the write failed"), "and it says why");
    g_vprotFails = false;

    // ...and after all of that the byte is still the game's own.
    check(b0() == 0xA1, "the entry survived every path through this file");

    std::printf(bad ? "FAIL: %d\n" : "OK (%d failures)\n", bad);
    return bad ? 1 : 0;
}
