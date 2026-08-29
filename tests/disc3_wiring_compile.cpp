// disc3_wiring_compile.cpp -- compiles AND RUNS the three disc-3 wiring layers
// (#110 Esthar, #111 space rescue, #112 Propagators) on the host.
//
//   g++ -std=c++17 -O0 -Isrc -o disc3_wiring_compile tests/disc3_wiring_compile.cpp
//
// WHY THIS EXISTS
// ---------------
// field_disc3*.inl are textual includes of field_navigation.cpp, which only
// MSVC ever compiles. Nothing on this side of the wire had ever *parsed* them.
// The first thing this probe found, before it ran a single assertion, was that
// `namespace Propagator` shadowed `struct Propagator`, so `const Propagator* p`
// inside it named a namespace -- an error MSVC would have raised too, and which
// lint_braces.py cannot see.
//
// The stubs below stand in for the mod's four seams (field pointers, speech,
// log, the bgbtl tone/key table). The FIELD VARIABLE BLOCK IS NOT STUBBED: the
// probe mmaps real pages at the game's own 0x01CFxxxx addresses, so the wiring
// reads and writes through the exact same D3ReadU8 / D3WriteI32 paths it uses
// in the game, at the exact same addresses the models name. A wrong address
// constant therefore shows up as a wrong answer here, not as silence.
//
// v0.55.0 (#110/#111/#112).

#include <cstdio>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Windows-isms the wiring uses.
// ---------------------------------------------------------------------------
typedef unsigned long  DWORD;
typedef unsigned short WORD;
#define __try       try
#define __except(x) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1
#define __cdecl
// v0.63.3.1: the same segmented-memory ghosts tests/winshim/windows.h now
// carries. This probe hand-rolls its Win32 surface rather than using the shim,
// so it needs them too -- v0.63.3 defined a local called `far` here, compiled
// green, and failed on MSVC with "error C2513: no variable declared before '='".
// Placed after every standard header, because the point is to poison the mod's
// identifiers and not libstdc++'s.
#define far
#define near
#define pascal
#define huge
#define VK_OEM_2 0xBF
#define VK_F9    0x78
#define VK_RETURN 0x0D

// v0.63.0 gave the space rescue a radar and this stub measured its pitch.
// v0.63.1 took the radar out -- Aaron, after flying the scene to the end on the
// words alone: "Let's get rid of the beep sound effect. It is extremely
// distracting and the TTS announcement I think is sufficient." The stub stays,
// with its counter, and the counter is now asserted to be ZERO across the whole
// scene. A removal that nothing checks grows back.
typedef const char* LPCSTR;
#define SND_MEMORY    0x0004
#define SND_ASYNC     0x0001
#define SND_NODEFAULT 0x0002
static int g_beeps = 0;
static bool PlaySoundA(LPCSTR, void*, unsigned) { g_beeps++; return true; }

static DWORD g_tick = 100000;
static DWORD GetTickCount() { return g_tick; }

// v0.69.0: the Confirm key, as the game sees it -- a bit in the field button
// word at 0x01CE48B0, which is where the module reads it from. Bit 5 (0x20) is
// Circle.
static bool g_confirmDown = false;

static int  g_keyDown = 0;              // bitmask: 1 = '/', 2 = F9, 4 = Enter
static short GetAsyncKeyState(int vk)
{
    if (vk == VK_OEM_2) return (g_keyDown & 1) ? (short)0x8000 : 0;
    if (vk == VK_F9)    return (g_keyDown & 2) ? (short)0x8000 : 0;
    if (vk == VK_RETURN) return (g_keyDown & 4) ? (short)0x8000 : 0;
    return 0;
}
static int _stricmp(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// ---------------------------------------------------------------------------
// Mod seams.
// ---------------------------------------------------------------------------
static char  g_fieldName[64] = "";
static WORD  g_fieldId = 0xFFFF;

// v0.64.0: field_main, and the page it lives on. The probe maps a real RWX page
// at the game's own 0x00471000 and puts field_main's real opening bytes at
// 0x00471F70, so FieldPause::Available() runs its actual signature check and
// Engage/Release write actual bytes that the assertions can read back. A stub
// that returned "yes, frozen" would be a statement that the freeze exists
// rather than evidence of it -- and this is the one feature in the mod where a
// byte that does not go back means a game that has to be closed.
static const uintptr_t FM_PAGE = 0x00471000u;
static const uintptr_t FM_ADDR = 0x00471F70u;
// v0.66.0 (#112): the live "Others" entity array, because the Propagator hold
// writes into it. A real array at a real address, so the probe exercises the
// same pointer arithmetic the mod does rather than a model of it.
static const int   MAX_ENTITIES  = 32;
static const DWORD ENTITY_STRIDE = 0x264;
static uint8_t*    g_others      = nullptr;   // MAX_ENTITIES * 0x264
static uint8_t*    g_othersPtr   = nullptr;   // the pointer the mod dereferences
static int         s_playerEntityIdx = -1;

namespace FF8Addresses {
    WORD* pCurrentFieldId  = &g_fieldId;
    char* pCurrentFieldName = g_fieldName;
    uint32_t field_main_fn = 0;          // set in main(), after the page is mapped
    uint8_t** pFieldStateOthers = nullptr;   // set in main()
    // v0.73.0: the live entity COUNT. PgResolveByModel walks the live array
    // looking for the block whose model id matches, and without a count it
    // either walks nothing or walks past the end.
    uint8_t  liveCount = (uint8_t)32;
    uint8_t* pFieldStateOtherCount = &liveCount;
    // v0.68.0: the field opcode table. The Propagator hold vetoes the two move
    // opcodes through it, the same way the Trabia dragon hooks ANIME, so the
    // probe supplies a real table and can watch the entries change.
    uint32_t opcodeTable[0x180] = {0};
    uint32_t* pExecuteOpcodeTable = opcodeTable;
}
// The two real opcodes, as the engine's would behave: both end up writing the
// script's requested speed into the entity. Whatever the veto does, it has to
// do it AFTER this has run, which is the whole point of chaining rather than
// replacing.
static int g_origSpeedCalls = 0;
static int __cdecl FakeSetSpeed(void* ctx, int param)
{
    g_origSpeedCalls++;
    if (ctx) {
        *(uint16_t*)((char*)ctx + 0x1FE) = (uint16_t)param;
        *(uint16_t*)((char*)ctx + 0x200) = (uint16_t)param;
    }
    return 0;
}
// v0.75.0: the engine's event-start opcode (0x04E). Its real job is to set the
// byte that takes the player's control away, so the probe records that it ran --
// a deferral that let it run has deferred nothing worth having.
static int g_origEventCalls = 0;
static int __cdecl FakeEvent(void* ctx, int param)
{
    (void)ctx; (void)param;
    g_origEventCalls++;
    return 2;                    // the engine's "advance to the next instruction"
}
static int __cdecl FakeTouch(void* ctx, int param)
{
    (void)param;
    if (ctx) *(int32_t*)((char*)ctx + 0x140) = 1;   // the engine's answer: touching
    return 0;
}
static int __cdecl FakeApproach(void* ctx, int param)
{
    (void)param;
    if (ctx) *(uint16_t*)((char*)ctx + 0x1FE) = *(uint16_t*)((char*)ctx + 0x200);
    return 0;
}
#ifndef PAGE_READWRITE
#define PAGE_READWRITE 0x04
#endif

static uint8_t* entBlock(int idx)
{ return g_others ? g_others + ENTITY_STRIDE * (size_t)idx : nullptr; }

// The two accessors field_disc3_propagator.inl reaches for, with the same
// contract the real ones have: a triangle of 0 means "not placed" and the
// caller must treat the position as unknown.
static bool GetEntityPos(int idx, float& cx, float& cy)
{
    if (idx < 0 || idx >= MAX_ENTITIES || !g_others) return false;
    uint8_t* b = entBlock(idx);
    if (*(uint16_t*)(b + 0x1FA) == 0) return false;
    int32_t fx = *(int32_t*)(b + 0x190), fy = *(int32_t*)(b + 0x194);
    if (fx == 0 && fy == 0) return false;
    cx = (float)(fx / 4096); cy = (float)(fy / 4096);
    return true;
}

// The live/script join. The probe answers "nothing proved it" by default, which
// is the case that makes the mod fall back to the field's own script order --
// the path most likely to be wrong and therefore the one worth exercising.
static char g_joinSym[MAX_ENTITIES][32] = {};
namespace FieldArchive {
    struct JSMEntityInfo { char symName[32]; };
}
static const FieldArchive::JSMEntityInfo* FindJSMByLiveEntity(int liveIdx)
{
    static FieldArchive::JSMEntityInfo info;
    if (liveIdx < 0 || liveIdx >= MAX_ENTITIES) return nullptr;
    if (!g_joinSym[liveIdx][0]) return nullptr;
    snprintf(info.symName, sizeof info.symName, "%s", g_joinSym[liveIdx]);
    return &info;
}
typedef void* LPVOID;
#define PAGE_EXECUTE_READWRITE 0x40
static int g_vprotCalls = 0;
static bool VirtualProtect(LPVOID, size_t, DWORD, DWORD* old)
{ g_vprotCalls++; if (old) *old = PAGE_EXECUTE_READWRITE; return true; }

struct Utterance { std::string text; bool interrupt; };
static std::vector<Utterance> g_said;
static std::vector<std::string> g_logged;

namespace ScreenReader {
    bool Speak(const char* t, bool interrupt = false)
    { g_said.push_back({ t ? t : "", interrupt }); return true; }
}
namespace Log {
    void Field(const char* fmt, ...)
    {
        char b[1024]; va_list ap; va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
        g_logged.push_back(b);
    }
}
static int g_tones = 0;
// v0.63.0: the space brief waits for the scene's own dialogue to finish.
static bool g_dialogOpen = false;
namespace FieldDialog { static bool IsDialogOpen() { return g_dialogOpen; } }

// v0.63.1: the mission clock. The real module is exercised for its own sake by
// tests/countdown_hold_test.cpp, which runs src/countdown_timer.cpp on the host
// against mmap'd engine globals; what THIS probe needs is the seam, and the one
// property of it the space module reasons about -- that a hold placed before
// the engine has written a countdown is a REQUEST and not yet a freeze. g_clock
// stands in for "the engine has started a countdown", so the probe can put the
// module on either side of that line deliberately.
static bool g_clockRunning = false;      // the engine has a detected countdown
static bool g_clockHeld    = false;      // ...and somebody asked us to stop it
static int  g_holdCalls    = 0;
namespace CountdownTimer {
    void SetHold(bool on, const char* reason) { (void)reason; g_clockHeld = on; g_holdCalls++; }
    bool IsHeldFrozen() { return g_clockHeld && g_clockRunning; }
    bool IsActive()     { return g_clockRunning; }
}

namespace GardenBattle {
    // FAITHFUL to field_minigame_bgbtl_input.inl, and it was not before.
    //
    // The first draft of this stub wrote an empty string for an unknown mask,
    // which is what the space brief was written against. The real one has FOUR
    // slots -- BTN_PUNCH 16, BTN_KICK 64, BTN_BLOCK 128, BTN_HEAVY 32 -- and
    // for anything else SlotFor() returns nullptr and CopyKeyName writes the
    // literal "?". The D-pad bits are not in that table, so the shipped brief
    // would have said "? and ? move you left and right" while this probe sat
    // there green. The stub is now the real function's contract, and the space
    // wiring no longer calls it at all.
    static const uint32_t BTN_PUNCH = 16, BTN_KICK = 64, BTN_BLOCK = 128, BTN_HEAVY = 32;
    static void CopyKeyName(char* dst, size_t n, uint32_t mask)
    {
        const char* p = (mask == BTN_PUNCH) ? "W"
                      : (mask == BTN_KICK)  ? "X"
                      : (mask == BTN_BLOCK) ? "A"
                      : (mask == BTN_HEAVY) ? "D"
                      : nullptr;
        if (!p) p = "?";                      // <-- never empty. That is the point.
        snprintf(dst, n, "%s", p);
    }
    static void PlayTone() { g_tones++; }
    // v0.63.2: the space brief names the boost key through the same learner.
    // The real LearnButtons() reads the field button word and refines the map;
    // here it only has to exist and be callable every tick, because what the
    // probe is checking is that the NAME reaches the screen.
    static int g_learnCalls = 0;
    static uint32_t LearnButtons() { g_learnCalls++; return 0; }

    // v0.63.0: the REAL Game Controls window code, not a stub of it.
    //
    // field_minigame_bgbtl_dialog.inl only ever calls the engine through
    // absolute-address function pointers, so it compiles here unchanged -- and
    // compiling it is the point. The space brief is the second caller of
    // OpenBriefDialog, and a stub would have been a statement that the function
    // exists rather than evidence of it, which is precisely the mistake the
    // CopyKeyName note above records. MapEngineWindowStubs() puts a `ret` at
    // each of those addresses so the calls really happen.
    static const int BRIEF_COLS = 34;   // field_minigame_bgbtl.inl:277
    #include "field_minigame_bgbtl_dialog.inl"
}

// ---------------------------------------------------------------------------
// The real field-variable block, at the real addresses.
// ---------------------------------------------------------------------------
// The engine's window entry points, as bare `ret`s. cdecl leaves the caller to
// clean the stack, so 0xC3 is a complete implementation of "do nothing".
static void MapEngineWindowStubs()
{
    const uintptr_t base = 0x0049F000u;
    const size_t    len  = 0x00003000u;      // covers 0x0049FBF0 .. 0x004A0EC0
    void* p = mmap((void*)base, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void*)base) {
        std::printf("FATAL: could not map the engine window stubs at 0x%lX\n",
                    (unsigned long)base);
        std::exit(2);
    }
    memset(p, 0xC3, len);
    // The field context pointer the open/close bits are written through.
    const uintptr_t ctxp = 0x00B8E000u;
    void* c = mmap((void*)ctxp, 0x2000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (c != (void*)ctxp) {
        std::printf("FATAL: could not map the field context at 0x%lX\n",
                    (unsigned long)ctxp);
        std::exit(2);
    }
    memset(c, 0, 0x2000);
    *(uintptr_t*)(ctxp + 0xE90) = ctxp + 0x1000;   // 0x00B8EE90 -> a real page

    // v0.67.0: the WINDOW STATE ARRAY, 0x01D2B330 + winId*0x3C, 8 slots. The
    // brief window's "message complete" byte at +0x28 is what the space rescue
    // now waits on before it stops the field, so the probe has to be able to
    // answer that question -- and answer it BOTH ways.
    const uintptr_t winp = 0x01D2B000u;
    void* w = mmap((void*)winp, 0x2000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (w != (void*)winp) {
        std::printf("FATAL: could not map the window state array at 0x%lX\n",
                    (unsigned long)winp);
        std::exit(2);
    }
    memset(w, 0, 0x2000);
}

// The engine's own "this window has finished typing" flag for window 7.
static const uintptr_t BRIEF_COMPLETE_ADDR = 0x01D2B330u + 0x3Cu * 7u + 0x28u;
static void setBoxComplete(bool on)
{ *(volatile uint8_t*)BRIEF_COMPLETE_ADDR = on ? 1 : 0; }

static const uintptr_t VARPAGE_BASE = 0x01CF0000u;
static const size_t    VARPAGE_LEN  = 0x00020000u;
// field_main's real opening: `mov eax, [0x01CE4A64]`, then push ebx / push ebp.
static void MapFieldMain()
{
    void* p = mmap((void*)FM_PAGE, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void*)FM_PAGE) {
        std::printf("FATAL: could not map field_main at 0x%lX\n", (unsigned long)FM_PAGE);
        std::exit(2);
    }
    memset(p, 0x90, 0x2000);
    // field_main's state machine byte, read by FieldPause::Engage for the log.
    void* st = mmap((void*)0x01CE4000u, 0x2000, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (st != (void*)0x01CE4000u) {
        std::printf("FATAL: could not map field_main's state\n"); std::exit(2);
    }
    memset(st, 0, 0x2000);
    *(volatile uint32_t*)0x01CE4A64u = 5;
    static const uint8_t opening[] = { 0xA1, 0x64, 0x4A, 0xCE, 0x01, 0x53, 0x55 };
    memcpy((void*)FM_ADDR, opening, sizeof opening);
    FF8Addresses::field_main_fn = (uint32_t)FM_ADDR;
}
static uint8_t FieldMainByte0() { return *(volatile uint8_t*)FM_ADDR; }

static void MapVarBlock()
{
    void* p = mmap((void*)VARPAGE_BASE, VARPAGE_LEN, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void*)VARPAGE_BASE) {
        std::printf("FATAL: could not map the field-variable page at 0x%lX\n",
                    (unsigned long)VARPAGE_BASE);
        std::exit(2);
    }
    std::memset(p, 0, VARPAGE_LEN);
}

// ---------------------------------------------------------------------------
// The code under test.
// ---------------------------------------------------------------------------
// The real display-name table, exactly as field_navigation.cpp includes it at
// line 48 -- field_disc3_esthar.inl formats exit names out of it, so stubbing
// it would be testing a stub.
// v0.65.0: the mod's own Game Controls box. The probe records what it was asked
// to show rather than drawing it -- the rasteriser is GDI and the blit is GL,
// and neither exists here. Its ARITHMETIC, which is the part that decides
// whether the box lands on the screen, is compiled and checked for real by
// tests/field_overlay_test.cpp.
static std::string g_overlayText;
static bool        g_overlayShown = false;
static int         g_overlayShows = 0;
// v0.66.1: the presented-frame counter. The probe drives it by hand, because
// the fact the space rescue now measures -- that a frozen field presents NO
// frames -- is exactly the kind of claim that must be checkable without a
// Ragnarok, and exactly the kind that was asserted in a comment for three
// builds while being false.
static unsigned g_swaps = 0;
namespace FieldOverlay {
    bool Show(const char* t)
    { g_overlayText = t ? t : ""; g_overlayShown = true; g_overlayShows++; return true; }
    void Hide()      { g_overlayShown = false; }
    bool IsShown()   { return g_overlayShown; }
    void Draw()      {}
    void NoteSwap()  { g_swaps++; }
    unsigned SwapCount() { return g_swaps; }
    void Shutdown()  { g_overlayShown = false; }
}

// The screenshot request the space rescue makes in the window before the
// freeze. Recorded, not taken.
static int         g_shotRequests = 0;
static std::string g_shotPath;
namespace BattleTTS {
    void RequestScreenshotAsync(const char* base, int = 0)
    { g_shotRequests++; g_shotPath = base ? base : ""; }
    const char* GetScreenshotDir() { return "SHOTDIR"; }
}

#include "field_pause.inl"
#include "field_display_names.h"
#include "esthar_pandora_model.inl"
#include "space_rescue_model.inl"
#include "propagator_model.inl"
#include "field_disc3.inl"

// ---------------------------------------------------------------------------
static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }

static bool loggedContains(const char* needle)
{
    for (auto& l : g_logged) if (l.find(needle) != std::string::npos) return true;
    return false;
}

static void setField(const char* name, int id)
{ snprintf(g_fieldName, sizeof(g_fieldName), "%s", name); g_fieldId = (WORD)id; }

static void wr8(int var, uint8_t v)  { *(uint8_t*)(uintptr_t)Disc3::D3VarAddr(var) = v; }
static void wr16(int var, uint16_t v){ *(uint16_t*)(uintptr_t)Disc3::D3VarAddr(var) = v; }
static void wrAddr32(uint32_t a, int32_t v) { *(int32_t*)(uintptr_t)a = v; }
static int32_t rdAddr32(uint32_t a) { return *(int32_t*)(uintptr_t)a; }

// Every utterance, ever, from the moment this probe starts. A "?" in any of
// them means something asked GardenBattle::CopyKeyName for a name it does not
// have and shipped the placeholder to the player.
static std::vector<std::string> g_allSaid;
static void recordAll() { for (auto& u : g_said) g_allSaid.push_back(u.text); }
static void clearSaid() { recordAll(); g_said.clear(); }
static bool saidContains(const char* needle)
{
    for (auto& u : g_said) if (u.text.find(needle) != std::string::npos) return true;
    return false;
}
static std::string lastSaid() { return g_said.empty() ? std::string() : g_said.back().text; }

static void tick(int ms = 16)
{
    g_tick += (DWORD)ms;
    // The Confirm bit goes into the real field button word, at the real address,
    // so the module reads it the same way the fight does.
    *(volatile uint16_t*)0x01CE48B0u = g_confirmDown ? (uint16_t)0x0020 : (uint16_t)0x0000;
    Disc3::Update();
}

int main()
{
    MapVarBlock();
    FF8Addresses::opcodeTable[0x03D] = (uint32_t)(uintptr_t)&FakeSetSpeed;
    FF8Addresses::opcodeTable[0x079] = (uint32_t)(uintptr_t)&FakeApproach;
    FF8Addresses::opcodeTable[0x05B] = (uint32_t)(uintptr_t)&FakeTouch;
    FF8Addresses::opcodeTable[0x04E] = (uint32_t)(uintptr_t)&FakeEvent;
    g_others    = (uint8_t*)calloc((size_t)MAX_ENTITIES, ENTITY_STRIDE);
    g_othersPtr = g_others;
    FF8Addresses::pFieldStateOthers = &g_othersPtr;
    MapFieldMain();
    MapEngineWindowStubs();
    std::printf("disc3_wiring_compile\n");

    // =======================================================================
    // 0. Address arithmetic: the models name absolute addresses AND variable
    //    indices for the same cells. If those two ever disagree the feature
    //    reads garbage, so pin them against each other.
    // =======================================================================
    // v0.57.0: var[1024] is NOT the Esthar clock -- it is the HUD mirror the
    // field re-establishes every frame, and reading it is what produced the
    // 2026-08-22 flood. Its address is pinned here only so the test below can
    // deliberately fill it with rubbish at the place the module used to look.
    check(Disc3::D3VarAddr(1024) == 0x01CFEDB8u, "var[1024] is at 0x01CFEDB8");
    check(EP_TIMER_ADDR == 0x01CFE92Cu,
          "**the clock is GETTIMER's own address**, not a field variable");
    check(Disc3::D3VarAddr(PG_VAR_PENDFLAG) == PG_ADDR_PENDFLAG, "var[445] == PG_ADDR_PENDFLAG");
    check(Disc3::D3VarAddr(PG_VAR_DEAD)     == PG_ADDR_DEAD,     "var[446] == PG_ADDR_DEAD");
    check(Disc3::D3VarAddr(PG_VAR_PENDBIT)  == PG_ADDR_PENDBIT,  "var[447] == PG_ADDR_PENDBIT");
    check(Disc3::D3VarAddr(1040) == SR_ADDR_X, "var[1040] == SR_ADDR_X");
    check(Disc3::D3VarAddr(1044) == SR_ADDR_Y, "var[1044] == SR_ADDR_Y");
    check(Disc3::D3VarAddr(1052) == SR_ADDR_FUEL,
          "**var[1052] == SR_ADDR_FUEL** -- rinoa::default 16-17 sets it to 8000 "
          "and director1 drains it");
    check(Disc3::D3VarAddr(1034) == SR_ADDR_STEP,
          "**var[1034] == SR_ADDR_STEP** -- the live step, 8 boosted and 4 not");
    check(Disc3::D3VarAddr(EP_VAR_MISSION) == 0x01CFEC58u, "var[672] (mission byte)");

    // =======================================================================
    // 1. SILENCE EVERYWHERE ELSE. The mod spends the whole game outside these
    //    three scenes; every one of them must be a hard no-op. This is the
    //    single most important property in the file.
    // =======================================================================
    setField("bgbtl_1", 100);
    clearSaid(); g_logged.clear(); g_tones = 0;
    for (int i = 0; i < 200; i++) tick();
    check(g_said.empty(), "silent on an unrelated field");
    check(g_logged.empty(), "no log spam on an unrelated field");
    check(g_tones == 0, "no tone on an unrelated field");

    // An Esthar city field with the mission byte NOT set: ordinary city, silent.
    setField("ecmall1", 445);
    wr8(EP_VAR_MISSION, 0);
    clearSaid();
    for (int i = 0; i < 100; i++) tick();
    check(g_said.empty(), "silent in Esthar when the run is not live");

    // =======================================================================
    // 2. ESTHAR (#110) -- REWRITTEN for v0.57.0.
    //
    // The 2026-08-22 BAT: the mod said "you are here, wait" at eccway21 and
    // nothing happened, because eccway21 is a walk-through `touch` line and
    // waiting on one does nothing. And it said it six times a second, because
    // the clock it read alternated with zero.
    //
    // THE CLOCK IS WRITTEN TO THE ENGINE ADDRESS ONLY. var[1024] is deliberately
    // left holding rubbish in every case below: if the module ever reads it
    // again these tests go wrong immediately.
    // =======================================================================
    check(EP_MISSION_LIVE == 19, "the run-live value is 19");
    check(EpTablesConsistent(), "**the generated route and site tables close**");

    setField("ecmall1", 431);
    wr16(EP_VAR_MISSION, 18);                 // one off: must stay silent
    wrAddr32(EP_TIMER_ADDR, 1150);
    wr16(1024, 0);
    clearSaid();
    for (int i = 0; i < 40; i++) tick();
    check(g_said.empty(), "mission byte 18 is not the run and says nothing");

    // --- the flood, reproduced and refuted --------------------------------
    // var[1024] alternating 0 / 1150 is exactly what the BAT log shows. Under
    // v0.55.0 that flipped the target contact point every tick and fired the
    // "point has passed" and "window open" announcements over and over.
    wr16(EP_VAR_MISSION, 19);                 // literal 19, not the constant
    wrAddr32(EP_TIMER_ADDR, 1150);
    setField("ecmview1", 434);
    clearSaid();
    tick();
    check(saidContains("Lunatic Pandora run"), "Esthar greets once the run is live");
    {
        const size_t after = g_said.size();
        for (int i = 0; i < 600; i++) {       // ten seconds
            wr16(1024, (i & 1) ? 0 : 1150);   // the HUD mirror, flapping
            tick();
        }
        check(g_said.size() == after,
              "**ten seconds of var[1024] flapping produces NOT ONE utterance** -- "
              "the module no longer reads it");
    }

    // --- and a floor under interrupting speech, independently of the clock --
    //
    // Fixing the clock removes the cause of the 2026-08-22 flood, but "the
    // cause is gone" is not the same as "it cannot happen". Something else that
    // flaps -- a field id oscillating across a boundary, a future state -- must
    // not be able to produce six interrupting utterances a second again. So the
    // floor is tested by making the input flap on purpose.
    // (Mutation-tested: deleting the floor passes without this.)
    {
        Disc3::Esthar::Reset();
        wrAddr32(EP_TIMER_ADDR, 1150);
        setField("ecmview1", 434);
        clearSaid();
        tick();                                   // the greeting
        const size_t base = g_said.size();
        for (int i = 0; i < 600; i++) {           // ten seconds, field flapping
            setField((i & 1) ? "ecmway1" : "ecmview1", (i & 1) ? 437 : 434);
            tick();
        }
        const size_t spoke = g_said.size() - base;
        // Ten seconds at a 1500 ms floor allows at most seven interrupting
        // utterances. The BAT produced roughly sixty in that time.
        check(spoke <= 7,
              "**a flapping field cannot produce more than one interrupting "
              "utterance per 1.5 seconds** -- the BAT managed six in one second");
        check(spoke > 0, "and it is not silent either -- the floor throttles, it does not gag");
    }

    // --- the route names an exit, and it is the CATALOG's name for it ------
    clearSaid();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(saidContains("Contact point 1"), "slash names the live contact point");
    check(saidContains("Take the exit to"), "and tells him which exit to take");
    check(saidContains("Esthar - City"),
          "**named the way the catalog names it**, not by internal field name");
    check(!saidContains("ecmway1") && !saidContains("ecopen"),
          "no internal field name is ever spoken to the player");

    // --- eccway21: the exact place the BAT failed -------------------------
    //
    // v0.57.0 said "walk through the exit to Esthar - City 21", and the catalog
    // had no such entry -- the zone filter had dropped the line. Aaron bounced
    // between four fields for eight minutes and missed contact point 1.
    //
    // v0.57.1: the line is forced into the catalog as "Contact point 1", and
    // because crossing it early lands on eciway11 -- which boards you by itself
    // -- the instruction is GO, not "cross at the right moment".
    setField("eccway21", 408);
    wrAddr32(EP_TIMER_ADDR, 800);             // CP1 window OPEN
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("Contact point 1 in the catalog") != std::string::npos,
              "**names the catalog entry the player can select and walk to**");
        check(s.find("cross it") != std::string::npos, "and says to cross it");
        check(s.find("wait") == std::string::npos && s.find("Wait") == std::string::npos,
              "never says wait at a crossing");
    }

    // Early: still GO, and it says why that is safe.
    wrAddr32(EP_TIMER_ADDR, 1100);
    Disc3::Esthar::Reset();
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("you can go now") != std::string::npos,
              "**early at a CP1 line: GO NOW** -- Aaron's own finding, and the "
              "scripts agree: crossing early lands on the site that boards by itself");
        check(s.find("do not have to watch the clock") != std::string::npos,
              "and says the clock does not matter here");
    }

    // --- CP2 is the one that genuinely needs timing -----------------------
    // Its two lines miss to each other, and neither polls, so crossing early
    // just walks you back and forth. This is the ONLY point where the mod
    // should ask the player to wait for a window.
    check(EpMissIsAuto(EpSiteAt("eccway21", EP_CP1)), "the CP1 line misses to a polling site");
    check(EpMissIsAuto(EpSiteAt("ecmall1",  EP_CP3)), "the CP3 line misses to a polling site");
    check(!EpMissIsAuto(EpSiteAt("eccway12", EP_CP2)), "**the CP2 lines do not**");
    check(!EpMissIsAuto(EpSiteAt("eccway41", EP_CP2)), "either of them");

    setField("eccway12", 404);
    wrAddr32(EP_TIMER_ADDR, 700);             // CP2 shut, opens at 600
    Disc3::Esthar::Reset();
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("Wait for it to open") != std::string::npos,
              "**CP2 early: wait for the window** -- the one point where timing is real");
        check(s.find("cross straight back") != std::string::npos,
              "and says an early crossing is recoverable, not fatal");
    }
    wrAddr32(EP_TIMER_ADDR, 500);             // CP2 open
    Disc3::Esthar::Reset();
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(saidContains("cross it now"), "CP2 open: cross it now");

    // --- the catalog name the mod forces in --------------------------------
    // field_catalog.inl exempts these lines from the zone filter and renames
    // them; EpContactLineAt is what it asks. The midpoints below are the ones
    // the BAT's own [LINE-PAIR] capture reported, which is an independent
    // measurement of the values extracted offline from the LINE opcodes.
    {
        const EstharSite* a = EpContactLineAt("eccway21", 3679, 1248);
        const EstharSite* b = EpContactLineAt("eccway41", -5032, -4205);
        const EstharSite* c = EpContactLineAt("eccway41", -1713, -6966);
        check(a && a->cp == EP_CP1, "**the BAT's captured eccway21 line is contact point 1**");
        check(b && b->cp == EP_CP2, "and eccway41's line0 is contact point 2");
        check(c && c->cp == EP_CP3, "and eccway41's line1 is contact point 3");
        check(!EpContactLineAt("eccway21", 3679, 9999), "a line 8 km away does not match");
        check(!EpContactLineAt("bgbtl_1", 3679, 1248), "and neither does the right point in the wrong field");
        // The offline midpoints and the runtime capture agree to within a unit,
        // so the 64-unit tolerance is slack rather than fudge.
        check(EpSiteAt("eccway21", EP_CP1)->lineX == 3679 &&
              EpSiteAt("eccway21", EP_CP1)->lineY == 1248,
              "the model's midpoint IS the captured one");
    }

    // --- a stopped clock is not an expired one -----------------------------
    // The BAT opened with t=0 in ecmview1, before SETTIMER had run, and v0.57.0
    // announced contact point 3 -- the LAST one -- as the target.
    setField("ecmview1", 434);
    Disc3::Esthar::Reset();
    wrAddr32(EP_TIMER_ADDR, 0);
    clearSaid();
    for (int i = 0; i < 60; i++) tick();
    check(g_said.empty(), "**t=0 before the timer starts says nothing at all**");
    wrAddr32(EP_TIMER_ADDR, 1188);            // SETTIMER fires
    clearSaid();
    tick();
    check(saidContains("Lunatic Pandora run"), "and the run greets once the clock starts");
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("Contact point 1") != std::string::npos,
              "aiming at point 1, not point 3");
    }
    // ...and once it HAS started, a genuine zero is a genuine zero.
    wrAddr32(EP_TIMER_ADDR, 0);
    clearSaid();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(!g_said.empty(), "a real zero at the end of the run still reports");

    // --- eciway11: the automatic one. Here 'wait' is the correct answer. ---
    setField("eciway11", 425);
    wrAddr32(EP_TIMER_ADDR, 800);
    wr8(EP_VAR_USED, 0);
    Disc3::Esthar::Reset();
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("Stay exactly where you are") != std::string::npos,
              "**eciway11 with the window open: stay put**");
        check(s.find("on its own") != std::string::npos, "and says it is automatic");
    }
    wrAddr32(EP_TIMER_ADDR, 1100);
    Disc3::Esthar::Reset();
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(saidContains("Wait right here"), "eciway11 early: wait right here");
    check(saidContains("do not have to do anything"), "and nothing is required of him");

    // --- the used-bit: a point already spent must say so ------------------
    wr8(EP_VAR_USED, 0x01);
    Disc3::Esthar::Reset();
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(saidContains("already been used"),
          "a spent contact point says so instead of promising a boarding");
    wr8(EP_VAR_USED, 0);

    // --- the ladder, through the live feature -----------------------------
    // t counts DOWN, so CP1 (900..720) comes first. These are the boundaries
    // the game's own gates use.
    check(EpTargetPoint(1200) == EP_CP1, "before the run starts, CP1 is next");
    check(EpTargetPoint(901)  == EP_CP1 && !EpWindowOpen(EP_CP1, 901), "at 15:01 CP1 is shut");
    check(EpTargetPoint(900)  == EP_CP1 &&  EpWindowOpen(EP_CP1, 900), "at 15:00 CP1 opens");
    check(EpTargetPoint(721)  == EP_CP1 &&  EpWindowOpen(EP_CP1, 721), "at 12:01 CP1 still open");
    check(EpTargetPoint(720)  == EP_CP2, "at 12:00 CP1 has gone and CP2 is next");
    check(EpTargetPoint(601)  == EP_CP2 && !EpWindowOpen(EP_CP2, 601), "at 10:01 CP2 is shut");
    check(EpTargetPoint(600)  == EP_CP2 &&  EpWindowOpen(EP_CP2, 600), "at 10:00 CP2 opens");
    check(EpTargetPoint(300)  == EP_CP3, "at 5:00 CP2 has gone");
    check(EpTargetPoint(180)  == EP_CP3 &&  EpWindowOpen(EP_CP3, 180), "at 3:00 CP3 opens");
    check(EpTargetPoint(1)    == EP_CP3 &&  EpWindowOpen(EP_CP3, 1),   "at 0:01 CP3 still open");
    check(!EpWindowOpen(EP_CP3, 0), "at zero nothing is open");

    // ecmall1 is the one site with a strictly-less-than bound, so t == 179 is
    // open there and t == 180 is not, while every other CP3 site takes 180.
    {
        const EstharSite* mall = EpSiteAt("ecmall1", EP_CP3);
        const EstharSite* ow3  = EpSiteAt("ecoway3", EP_CP3);
        check(mall && ow3, "both CP3 sites are in the table");
        if (mall && ow3) {
            check(!EpSiteOpen(mall, 180) && EpSiteOpen(mall, 179),
                  "**ecmall1 uses LT: 180 is shut there, 179 is open**");
            check(EpSiteOpen(ow3, 180), "and ecoway3 takes 180");
        }
    }

    // --- leaving the city, and the run ending ------------------------------
    setField("bgbtl_1", 100);
    clearSaid(); tick();
    setField("ecmview1", 434);
    wrAddr32(EP_TIMER_ADDR, 1150);
    clearSaid(); tick();
    check(saidContains("Lunatic Pandora run"), "Esthar re-greets after leaving and returning");

    wr16(EP_VAR_MISSION, 0);
    clearSaid();
    for (int i = 0; i < 50; i++) tick();
    check(g_said.empty(), "Esthar goes silent the moment the mission word clears");
    wr16(EP_VAR_MISSION, 19);

    // An unreadable clock must produce silence, not a guess.
    wrAddr32(EP_TIMER_ADDR, 999999);
    Disc3::Esthar::Reset();
    clearSaid();
    for (int i = 0; i < 40; i++) tick();
    check(g_said.empty(), "an out-of-range clock says nothing at all");
    wr16(EP_VAR_MISSION, 0);

    // =======================================================================
    // 3. SPACE (#111)
    // =======================================================================
    //
    // v0.63.1. Aaron flew this scene to the end on v0.63.0 -- "Successfully
    // completed the mini-game using manual navigation" -- and asked for two
    // things on the way out: the beep gone, and the Game Controls screen turned
    // into a real pause. Both are asserted below, and the second is asserted
    // against the exact shape of the failure his log recorded rather than
    // against the idea of it.
    auto pressEnter = [&]() {
        g_keyDown |= 4; tick(); g_keyDown &= ~4;
    };
    // The probe drives BOTH strategies, and it chooses between them the same
    // way the module does: by what the bytes at field_main's entry say.
    auto breakSignature   = [&]() { *(volatile uint8_t*)FM_ADDR = 0x90; };
    auto restoreSignature = [&]() { *(volatile uint8_t*)FM_ADDR = 0xA1; };
    // Reset() is the module's LEAVING-THE-FIELD path, which the mod only ever
    // reaches after CloseScreen. Calling it straight out of a live screen the
    // way this probe does leaves the window flag set, so the probe closes it
    // the way the mod would.
    auto resetSpace = [&]() {
        GardenBattle::CloseBriefDialog();
        Disc3::Space::Reset();
    };

    // =======================================================================
    // 3a. THE REAL PAUSE (v0.64.0)
    // =======================================================================
    //
    // Aaron: "Is there no way we can suppress the actual scene behind the Game
    // Controls screen, that way when the player presses enter that is when
    // Rinoa begins to move?" There is: field_main_loop calls field_main only
    // when two globals are zero, everything after that call still runs, and the
    // call passes no arguments -- so one 0xC3 at its entry stops the field.
    restoreSignature();
    setField("ssspace3", SR_FIELD_ID);
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    wr8(1034, 4);
    resetSpace();
    g_clockRunning = true; g_clockHeld = false;
    clearSaid(); g_tones = 0; g_beeps = 0;
    g_dialogOpen = true;                       // Squall is mid-sentence
    check(FieldPause::Available(),
          "**the freeze is available when field_main reads A1 64 4A CE 01**");

    // v0.64.1: NOT BEFORE THE SCENE HAS STARTED. The field-variable block is
    // ZEROED on load, so (0,0) is inside the clamp and v0.64.0 briefed on the
    // first readable frame -- Aaron's 20:36:21 log has "game controls open at
    // x=0 y=0" on the same second as "entered ssspace3", which froze field_main
    // while field_main was still loading the field. rinoa::default writes
    // var[1052] = 8000 and then picks one of eight starts, none of them (0,0).
    wrAddr32(SR_ADDR_X, 0); wrAddr32(SR_ADDR_Y, 0);
    wrAddr32(SR_ADDR_FUEL, 0);
    for (int i = 0; i < 40; i++) tick();
    check(g_said.empty() && FieldMainByte0() == 0xA1,
          "**a zeroed variable block is not a scene** -- nothing said, and "
          "nothing frozen, while the field is still loading");
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    for (int i = 0; i < 40; i++) tick();
    check(g_said.empty(),
          "and fuel alone is not a scene either -- rinoa::default sets it "
          "BEFORE it picks where she starts");
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    tick();
    check(saidContains("Game controls"),
          "**it briefs at once, with the scene still talking** -- there is "
          "nothing left to wait out, because a frame that never happens cannot "
          "open a dialogue");
    check(saidContains("Nothing in the scene moves until you press Enter"),
          "**and says so** -- v0.63.3 had to say 'she drifts on' because she did");
    check(g_clockHeld,
          "**and stops the clock TOO, freeze or no freeze**. v0.64.0 made this "
          "conditional on the freeze being unavailable, reasoning that stopping "
          "field_main stops the countdown with it. It does not: Aaron's "
          "2026-08-23 22:05 log has [FIELDPAUSE] ENGAGED at 22:05:07 and the "
          "global going 90, 89, 88 ... one a second straight through it, "
          "reaching zero at 22:06:35 while the screen was still up. The moment "
          "Enter let the scripts run, timer0::start0 set var[1037] = 1, "
          "director1's loop exited, and no key moved Rinoa again");
    check(FieldMainByte0() == 0xA1,
          "**and does NOT freeze on the same tick it opened the box** -- window "
          "rendering hangs off field_main, so a box opened and frozen together "
          "is a box that was never drawn");

    // THE SELF-CAPTURE GOES IN FIRST, and that ordering is the whole point of
    // it. Aaron's 12:39 BAT: two F11 presses during the pause produced ONE file,
    // written at 12:39:51.996 -- the exact second Enter released the freeze --
    // because a frozen field presents no frames, and a capture needs a swap.
    // The only moment a shot of the box is possible is the gap this code
    // already leaves between opening the box and stopping the field.
    check(g_shotRequests == 0, "nothing is captured on the tick the box opens");
    for (int i = 0; i < 120 / 16; i++) tick();      // past SP_SHOT_MS
    check(g_shotRequests == 1,
          "**one shot is asked for while the engine is still presenting**");
    check(FieldMainByte0() == 0xA1,
          "**and it is asked for BEFORE the freeze** -- afterwards there are no "
          "frames to capture and the request sits unserviced until Enter");

    // v0.67.0: AND IT WAITS FOR THE GAME'S BOX TO FINISH TYPING. The window is
    // drawn by field_main, so whatever it has managed to type when the byte at
    // 0x00471F70 changes is what stays on screen for the whole pause -- which
    // is why every earlier screenshot caught it at "REACHING RIN".
    for (int i = 0; i < 300 / 16; i++) tick();  // past SP_DRAW_MS
    check(g_shotRequests == 1, "and exactly one, not one per tick");
    check(FieldMainByte0() == 0xA1,
          "**the field is still running while the box is mid-sentence** -- "
          "freezing now would photograph a half-typed window and leave it that "
          "way for the whole pause");
    setBoxComplete(true);                        // the engine's own +0x28 flag
    tick();
    check(FieldMainByte0() == 0xC3,
          "**and it stops the instant the box reports complete** -- one byte at "
          "0x00471F70, and the script VM, the 2700-frame approach, the countdown "
          "and Rinoa all stop with it");
    check(loggedContains("reports its text complete"),
          "and says that is why it froze, rather than that a timer elapsed");
    check(FieldPause::IsEngaged(), "the module knows it is holding it");
    check(!FieldOverlay::IsShown(),
          "**and the mod's own box is NOT up** -- Aaron: \"I don't think it looks "
          "very good if our injected dialogs / text doesn't look the same as the "
          "rest of the game.\" The game's own window is the one on screen; ours "
          "is the fallback for when that will not open, and showing both put our "
          "372x240 inside the game's 459x320 in the 13:12 frame");
    check(g_vprotCalls > 0, "and it went through VirtualProtect to get there");

    // Nothing steers while it is frozen, and her position is still pinned --
    // belt and braces, because the pin costs nothing and the freeze is new.
    wrAddr32(SR_ADDR_X, 4000); wrAddr32(SR_ADDR_Y, 3000);
    clearSaid();
    for (int i = 0; i < 40; i++) tick();
    check(g_said.empty(), "no steering call while the field is frozen");
    check(rdAddr32(SR_ADDR_X) == 2500 && rdAddr32(SR_ADDR_Y) == -800,
          "and her position is pinned as well");
    check(FieldMainByte0() == 0xC3, "and it stays frozen");

    // SLASH, WHILE FROZEN. v0.64.0's Available() looked for the UNPATCHED
    // signature, so between Engage and Release it failed on its own handiwork
    // and every wording chosen from it flipped to the fallback. Aaron's
    // 20:36:42 log is what that sounds like: he pressed slash while the field
    // was frozen and the repeat told him "the mission clock is held ... but the
    // scene itself is not", flatly contradicting the brief he had just heard.
    clearSaid();
    g_keyDown |= 1; tick(); g_keyDown &= ~1; tick();
    check(saidContains("Nothing in the scene moves until you press Enter"),
          "**the repeat says the same thing the brief did** -- Available() has "
          "to stay true against its own 0xC3");
    check(GardenBattle::BriefDialogOpen(),
          "and the game's box was rebuilt with it, so a key learned since the "
          "open reaches the screen too");

    // ---- NOTHING THE SCREEN SAYS MAY TURN INTO A SPACE ------------------
    // The 14:11 capture was the first picture of this box with all ten lines
    // in it, and it read "Boost  hold W." and " Centred  means let go." --
    // EncodeChar's `default: return 0x20` eating every punctuation mark that
    // was not in a hand-written switch. This is the assertion that would have
    // caught it: take the text the mod actually puts on the screen and require
    // that no character in it encodes to a space unless it IS one.
    {
        const char* t = Disc3::Space::s_screenText;
        check(t && t[0], "there is screen text to check");
        int lost = 0; char first = 0;
        for (const char* p = t; p && *p; p++) {
            if (*p == ' ' || *p == '\n') continue;
            if (GardenBattle::EncodeChar(*p) == 0x20) { lost++; if (!first) first = *p; }
        }
        if (lost)
            std::printf("  (first lost character: '%c')\n", first);
        check(lost == 0,
              "**every character of the Game Controls screen has a glyph** -- a "
              "colon that silently becomes a space is a colon nobody can see is "
              "missing, and it took a screenshot to notice");
        // The four the capture actually lost, named so a future edit to the
        // table cannot quietly drop them again.
        check(GardenBattle::EncodeChar(':') == 0x2D, "colon");
        check(GardenBattle::EncodeChar(';') == 0xB5, "semicolon");
        check(GardenBattle::EncodeChar('/') == 0x2C, "slash");
        check(GardenBattle::EncodeChar('"') == 0x3E, "double quote");
        // ...and one that must still be a space, because the font has no glyph.
        check(GardenBattle::EncodeChar('~') == 0x20,
              "a character the grid does not have is still a space");

        // COVERAGE, not just this one screen. Three mini-games share this
        // encoder now -- the space rescue, the Garden battle and the Trabia
        // dragon -- and a fourth will be written by somebody who assumes the
        // punctuation works. Require the whole ordinary printable set, so the
        // next Game Controls screen cannot lose a character nobody thought to
        // check.
        const char* need = "!\"#$%&'()*+,-./0123456789:;<=>?"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ[]_"
                           "abcdefghijklmnopqrstuvwxyz";
        int gaps = 0; char firstGap = 0;
        for (const char* p = need; *p; p++)
            if (GardenBattle::EncodeChar(*p) == 0x20) { gaps++; if (!firstGap) firstGap = *p; }
        if (gaps) std::printf("  (first uncovered character: '%c')\n", firstGap);
        check(gaps == 0,
              "**every ordinary printable character has a glyph code** -- this is "
              "the facility three mini-games draw their controls with, and the "
              "colon went missing in it for eleven months");
    }

    check(!saidContains("but the scene itself is not"),
          "and does not contradict it");
    check(FieldMainByte0() == 0xC3, "and slash does not thaw the field");

    // It does not time out, however long he takes.
    g_tick += 300000; tick();
    check(FieldMainByte0() == 0xC3 && GardenBattle::BriefDialogOpen(),
          "**five minutes in and still frozen** -- Enter, or nothing");

    // Enter gives the field back, and says nothing about a scene that ran on,
    // because none of it did.
    clearSaid();
    pressEnter();
    check(FieldMainByte0() == 0xA1,
          "**Enter puts the byte back** -- the original byte, read before the "
          "patch, not a constant");
    check(!FieldPause::IsEngaged(), "and the module lets go");
    check(!FieldOverlay::IsShown(), "and the overlay comes down with the freeze");
    check(!g_clockHeld, "and the clock is handed back with it");
    check(!saidContains("The scene ran on"),
          "**and does not apologise for time that was not spent**");
    g_dialogOpen = false;

    // F9 while frozen. The escape hatch cannot be the thing that strands him.
    resetSpace();
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    clearSaid();
    tick();
    for (int i = 0; i < 300 / 16; i++) tick();
    check(FieldMainByte0() == 0xC3, "frozen again");
    // THE CLAIM, MEASURED. The mod now counts the frames the engine actually
    // presented across the pause and says so on release, instead of asserting
    // in a comment -- which is what it did for three builds while being wrong.
    g_logged.clear();
    g_keyDown = 2; tick(); g_keyDown = 0;
    check(FieldMainByte0() == 0xA1, "**F9 unfreezes the field**");
    check(loggedContains("the engine presented 0 frames in the"),
          "**and reports how many frames the engine presented while it held it** "
          "-- the 13:12 BAT measured ONE across twenty-five seconds, which is why "
          "the last frame drawn before the freeze is the one that stays on screen "
          "and why a capture asked for during the pause waits for Enter");
    check(loggedContains("effectively stopped"),
          "and reads a handful of frames in half a minute as a stopped engine "
          "rather than tripping over a literal zero");
    check(!FieldOverlay::IsShown(),
          "**and F9 takes the overlay with it** -- an escape hatch that leaves a "
          "box painted over the game is not one");
    check(saidContains("Skip on"), "and still arms the skip");

    // Walking out while frozen.
    resetSpace();
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    tick();
    for (int i = 0; i < 300 / 16; i++) tick();
    check(FieldMainByte0() == 0xC3, "frozen once more");
    setField("bgbtl_1", 100); tick();
    check(FieldMainByte0() == 0xA1,
          "**leaving the field unfreezes it** -- and a field that is frozen "
          "cannot change on its own, so this is the case where the mod moved "
          "and the game did not");
    setField("ssspace3", SR_FIELD_ID);

    // ---- THE CAP. A box that never reports complete must not hold the scene
    // open for ever. The wait exists because the frozen frame is whatever the
    // window managed to type; it must not become a way for one wrong flag to
    // spend the player's ninety seconds.
    Disc3::Space::Reset();
    setBoxComplete(false);
    g_dialogOpen = true;
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    for (int i = 0; i < 10; i++) tick();
    g_dialogOpen = false;
    // Wait for the screen to actually open rather than assuming when it does --
    // the dialogue-quiet gate in front of it is timing this probe does not own.
    for (int i = 0; i < 600 && !GardenBattle::BriefDialogOpen(); i++) tick();
    check(GardenBattle::BriefDialogOpen(), "the box opened");
    g_logged.clear();
    for (int i = 0; i < 1500 / 16; i++) tick();
    check(FieldMainByte0() == 0xA1,
          "**a box that never finishes does not get frozen early**");
    for (int i = 0; i < 1200 / 16; i++) tick();
    check(FieldMainByte0() == 0xC3,
          "**but the cap fires and the scene goes on** -- two seconds, not for ever");
    check(loggedContains("never reported complete"),
          "**and the log says the box on screen may be part-typed** rather than "
          "letting a silent half-message pass for a working one");
    Disc3::Space::Reset();
    setBoxComplete(true);
    setField("ssspace3", SR_FIELD_ID);

    // THE WATCHDOG. The one guarantee that a bug in the caller cannot strand
    // him: it runs from PollBattlePauseResume, above every on-field early
    // return, and does not care what the owner believes.
    resetSpace();
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    tick();
    for (int i = 0; i < 300 / 16; i++) tick();
    check(FieldMainByte0() == 0xC3, "frozen for the watchdog test");
    g_tick += 601000;
    FieldPause::Watchdog();
    check(FieldMainByte0() == 0xA1,
          "**the watchdog force-releases past the ceiling** -- ten minutes is "
          "past any screen a player sits on and short of 'the game is broken "
          "and he does not know why'");
    check(!FieldPause::IsEngaged(), "and the module's own state agrees");
    resetSpace();

    // ...and if the clock is somehow gone by the time he starts, Enter says so
    // rather than reading bearings into a scene that has already ended -- four
    // minutes of "still well right and down" at a position that never moved is
    // what 22:06:54 onward actually sounded like.
    resetSpace();
    g_clockRunning = true; g_clockHeld = false;
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    tick();
    for (int i = 0; i < 300 / 16; i++) tick();
    g_clockRunning = false;              // the countdown ran out behind the box
    clearSaid();
    pressEnter();
    check(saidContains("the mission clock has already run out"),
          "**a dead clock at Enter is announced, not flown into**");

    resetSpace();
    g_clockRunning = true;
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    tick();
    for (int i = 0; i < 300 / 16; i++) tick();
    clearSaid();
    pressEnter();
    check(!saidContains("already run out"), "and a live one says nothing about it");
    resetSpace();

    // A build whose field_main does not read the way it should is never patched.
    breakSignature();
    check(!FieldPause::Available(),
          "**an unrecognised entry is not available** -- never patch what we "
          "did not recognise");
    check(!FieldPause::Engage("probe"), "and Engage refuses it outright");
    check(FieldMainByte0() == 0x90, "leaving the byte exactly as it found it");

    // =======================================================================
    // 3b. THE FALLBACK -- v0.63.3's clock hold, for a build that cannot freeze.
    // =======================================================================

    // -----------------------------------------------------------------------
    // THE WAIT. v0.63.0 asked "is a dialogue open?" on the frame the field
    // loaded -- before Squall had said anything -- decided the scene was quiet,
    // and briefed straight into his first line three seconds later. So the
    // probe reproduces THAT: no dialogue at arrival, dialogue afterwards.
    // -----------------------------------------------------------------------
    setField("ssspace3", SR_FIELD_ID);
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);      // rinoa::default 16-17
    wr8(1034, 4);                              // director1: not boosting
    Disc3::Space::Reset();
    g_clockRunning = false; g_clockHeld = false;
    clearSaid(); g_tones = 0; g_beeps = 0;
    g_dialogOpen = false;
    tick();
    check(g_clockHeld,
          "**the clock is asked to stop on the first frame of the scene** -- the "
          "engine does not detect its own countdown for another two seconds, so "
          "asking later is asking too late");
    check(g_said.empty() && !GardenBattle::BriefDialogOpen(),
          "**an empty first frame is not 'the scene has finished talking'** -- "
          "this is the v0.63.0 bug: it briefed at 16:10:54 and Squall started "
          "at 16:10:57");
    g_clockRunning = true;                       // the engine's countdown appears
    for (int i = 0; i < 3000 / 16; i++) tick();
    check(g_said.empty(), "still waiting three seconds in");
    g_dialogOpen = true;                         // "(Rinoa...... Where are you?)"
    for (int i = 0; i < 12000 / 16; i++) tick();
    check(g_said.empty(),
          "**and it waits out all of Squall's lines** -- twelve seconds of them, "
          "which only costs nothing because the clock is stopped");
    check(GardenBattle::BriefDialogOpen() == false, "the window waits with it");
    g_dialogOpen = false;
    for (int i = 0; i < 2000 / 16; i++) tick();
    check(saidContains("Game controls"), "then it briefs");
    check(saidContains("mission clock is held"),
          "and says the clock is held");
    check(saidContains("but the scene itself is not"),
          "**and does NOT claim the scene stops with it**. Aaron's 2026-08-23 "
          "attempt 1: the clock was held sixty-three seconds and the scene still "
          "ended with forty-seven showing on it -- KILLTIMER at 18:34:33 against "
          "a 60-second boundary announced at 18:34:20. The box has to be DRAWN "
          "and window rendering hangs off field_main, which is exactly why the "
          "Garden battle's RET-over-field_main pause is retired");

    // A scene that never talks at all must not be waited on forever.
    Disc3::Space::Reset();
    g_clockRunning = true; g_clockHeld = false;
    clearSaid();
    g_dialogOpen = false;
    for (int i = 0; i < 3000 / 16; i++) tick();
    check(g_said.empty(), "a silent scene is still given time to open its mouth");
    for (int i = 0; i < 5000 / 16; i++) tick();
    check(saidContains("Game controls"),
          "**but a scene with no dialogue at all briefs after the settle** -- the "
          "wait is for a dialogue that ends, not for one that never starts");

    // ...and a scene that never STOPS talking is capped. Which cap depends on
    // whether the clock actually stopped: waiting is free only if it did.
    Disc3::Space::Reset();
    g_clockRunning = false; g_clockHeld = false;      // the hold did not take
    clearSaid();
    g_dialogOpen = true;
    for (int i = 0; i < 21000 / 16; i++) tick();
    check(g_said.empty(), "an unstopped clock still waits its 22 seconds");
    for (int i = 0; i < 2000 / 16; i++) tick();
    check(saidContains("Game controls"),
          "**and briefs anyway** -- with the clock running, the approach is on a "
          "ninety-second budget and the wait cannot spend all of it");

    Disc3::Space::Reset();
    g_clockRunning = true; g_clockHeld = false;       // the hold took
    clearSaid();
    g_dialogOpen = true;
    for (int i = 0; i < 30000 / 16; i++) tick();
    check(g_said.empty(),
          "**a stopped clock waits longer** -- thirty seconds in and still "
          "listening, because thirty seconds of a stopped clock cost nothing");
    for (int i = 0; i < 16000 / 16; i++) tick();
    check(saidContains("Game controls"), "and it caps at forty-five all the same");
    g_dialogOpen = false;

    // -----------------------------------------------------------------------
    // THE SCREEN.
    // -----------------------------------------------------------------------
    Disc3::Space::Reset();
    g_clockRunning = true; g_clockHeld = false;
    // NOT (0,0): the field-variable block is zeroed on load, so (0,0) is the
    // scene BEFORE rinoa::default has picked a start, and the module refuses to
    // brief there. (2500,-800) is one of the eight the script can pick.
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    clearSaid(); g_tones = 0; g_beeps = 0;
    for (int i = 0; i < 7000 / 16; i++) tick();
    check(saidContains("Game controls"), "space opens the Game Controls screen");
    check(saidContains("Reaching Rinoa"), "and names the scene");
    check(saidContains("arrow keys"), "the screen names the arrow keys");
    check(saidContains("diagonal"),  "and tells him diagonals count -- director1 dispatches eight");
    check(saidContains("Enter"),     "and how to start");
    check(!saidContains("beep"),
          "**and nothing in it mentions a beep** -- there is no longer one to "
          "mention, and a brief that describes a cue the player cannot hear is "
          "worse than a brief that leaves it out");

    // v0.63.2, all three of Aaron's corrections, on the screen he actually gets.
    {
        std::string brief;
        for (auto& u : g_said) brief += u.text + " ";
        const std::string screen = Disc3::Space::s_screenText;

        // 1. THIRD PERSON, to match the Garden battle and the dragon fight.
        check(brief.find(" I ") == std::string::npos &&
              brief.find("I call") == std::string::npos &&
              brief.find("I say") == std::string::npos,
              "**the brief never says 'I'** -- Aaron: \"instead let's use "
              "third-person like 'when the mod' so it matches the other game "
              "controls screens\"");
        check(brief.find("the mod calls") != std::string::npos,
              "it says 'the mod calls' instead");

        // 2. THE BOOST, NAMED. Mask 0x0010, resolved through the learner -- the
        //    same one that measured W for it on Aaron's keyboard.
        check(SR_MASK_BOOST == GardenBattle::BTN_PUNCH,
              "**the boost mask is the learner's own slot 0x0010** -- so the key "
              "name comes from measurement rather than from a guess");
        check(screen.find("Boost: hold W") != std::string::npos,
              "**the screen names the boost key** -- W, not the X Aaron expected: "
              "X measured as 0x0040 in his own 2026-08-15 logs");
        check(brief.find("Hold W to boost") != std::string::npos,
              "and the spoken brief names it too");
        check(brief.find("four times the speed") != std::string::npos,
              "**and says four times, not twice** -- var[1034] goes to 8 AND the "
              "key handler multiplies by 2 again");
        check(GardenBattle::g_learnCalls > 0,
              "the learner is running during the scene, so a remap is caught");

        // 3. HOLD, DON'T TAP. BTN_HELD is a level test and the handler loops
        //    while it stays down; a tap moves her once.
        check(screen.find("HOLD an arrow down, don't tap.") != std::string::npos,
              "**the screen says HOLD the arrow, not tap it**");
        check(brief.find("hold the direction the mod calls DOWN") != std::string::npos,
              "and the spoken brief spells out why");
    }
    check(GardenBattle::BriefDialogOpen(),
          "**the game's own window is open** -- not just spoken");
    // v0.65.0: and the mod's own, which is the one that will actually be
    // visible. Aaron's F11 screenshot of the frozen field came back black but
    // for the mission timer -- the game's window stops being redrawn the moment
    // field_main stops, because that is where window drawing lives.
    // v0.67.0: ONE BOX, AND IT IS THE GAME'S. The mod's own overlay is the
    // fallback now -- it draws from the SwapBuffers hook, which does not run
    // while the field is frozen anyway (v0.66.1 measured one frame in
    // twenty-five seconds). What keeps the game's window on screen through the
    // pause is that the engine stops presenting AT ALL, so the last frame drawn
    // is the one that stays there -- and with the text speed at zero that frame
    // has the whole message in it.
    check(!FieldOverlay::IsShown(),
          "**and the mod's own overlay is NOT up beside it** -- two boxes, one "
          "inside the other, is what the 13:12 frame actually showed");
    check(g_clockHeld, "the clock is still held while he reads");
    {
        // A box that runs off the bottom of a 320x224 screen is what v0.20.125
        // had to go back and fix. The Garden battle's six lines measured 108 px,
        // so about 18 px a line and eleven lines is the ceiling; and any line
        // wider than BRIEF_COLS silently becomes two.
        int lines = 1, widest = 0, col = 0;
        for (const char* p2 = Disc3::Space::s_screenText; *p2; ++p2) {
            if (*p2 == '\n') { if (col > widest) widest = col; col = 0; lines++; }
            else col++;
        }
        if (col > widest) widest = col;
        check(lines <= 11, "**the Game Controls screen fits on the screen** (<= 11 lines)");
        check(widest <= GardenBattle::BRIEF_COLS,
              "and no line wraps behind our back (<= BRIEF_COLS)");
    }

    // NOTHING MOVES WHILE HE READS. v0.63.0 pinned nothing: an arrow pressed
    // while the box was up flew the ship, and the box went away by itself after
    // fifteen seconds whether or not anyone had read a word of it.
    wrAddr32(SR_ADDR_X, 4000); wrAddr32(SR_ADDR_Y, 3000);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(), "no steering call while the Game Controls screen is up");
    check(rdAddr32(SR_ADDR_X) == 2500 && rdAddr32(SR_ADDR_Y) == -800,
          "**and her position is pinned** -- reading the controls is not flying");

    // THE SCREEN DOES NOT TIME OUT. This is the assertion v0.63.0 failed in
    // Aaron's log: "[SPACE] game controls closed (timed out)" at 16:11:09,
    // fifteen seconds after it opened, with Enter never pressed.
    g_tick += 16000; tick();
    check(GardenBattle::BriefDialogOpen(),
          "**still up after sixteen seconds** -- v0.63.0 gave up here");
    clearSaid();
    g_tick += 120000; tick();
    check(GardenBattle::BriefDialogOpen(),
          "**and after two minutes** -- Aaron: \"pauses everything until the "
          "player hits Enter\"");
    check(!g_said.empty(), "and it reminds him what ends it while he waits");
    {
        bool nudged = false;
        for (auto& u : g_said) if (u.text.find("Press Enter") != std::string::npos) nudged = true;
        check(nudged, "the reminder names the key");
        for (auto& u : g_said)
            check(!u.interrupt, "**and never interrupts** -- it must not cut the brief in half");
    }

    // Slash reads the whole thing again, for a player who missed it.
    clearSaid();
    g_keyDown |= 1; tick(); g_keyDown &= ~1;
    check(saidContains("arrow keys"), "slash repeats the controls while the box is up");
    check(GardenBattle::BriefDialogOpen(), "and does not dismiss it");

    // Enter closes it, gives the clock back, and the game starts. Her position
    // is set AFTER the press, because until it lands she is pinned -- the probe
    // wrote 4000 into X a few lines above and the module put it straight back.
    clearSaid();
    pressEnter();
    wrAddr32(SR_ADDR_X, 4000); wrAddr32(SR_ADDR_Y, 0);
    check(!GardenBattle::BriefDialogOpen(), "Enter closes the window");
    check(!FieldOverlay::IsShown(), "and takes the overlay down with it");
    // The Enter tick produces the attempt's first bearing, off the pinned
    // position -- so it is also where the boost nudge belongs. Aaron lost
    // 2026-08-23 attempt 1 at 280 units out and still closing, from a start of
    // x=2500: 52 seconds of unboosted travel at the measured 48 a second.
    check(saidContains("Hold W to boost"),
          "**the first wide bearing names the boost, once, when it decides the "
          "scene**");
    check(saidContains("The scene ran on for those"),
          "**and Enter says what the pause actually cost**. It is the only "
          "honest thing left to say: the clock stopped and the approach did not, "
          "so the countdown's own \"one minute remaining\" is now over-reporting "
          "the scene by however long the box was up");
    {
        bool queued = true;
        for (auto& u : g_said)
            if (u.text.find("The scene ran on") != std::string::npos && u.interrupt)
                queued = false;
        check(queued, "and it is queued, not interrupted");
    }
    check(!g_clockHeld,
          "**and gives the clock back** -- a pause that outlives the box would "
          "hand him an approach with no deadline and a scene that never ends");
    check(SpaceRescueActive(),
          "**the catalog is suppressed while he is flying** -- Aaron's 14:54:22 log "
          "has auto-drive setting off toward \"Exit to Outer Space 5\", which is "
          "Rinoa catalogued as a door, in the middle of the attempt");

    // Steering: she is far right, so the call must say RIGHT.
    clearSaid();
    g_tick += 4000; Disc3::Update();
    check(saidContains("right"), "far-right error steers right");
    {
        clearSaid();
        for (int i = 0; i < 20 * 1000 / 16; i++) tick();
        bool again = false;
        for (auto& u : g_said) if (u.text.find("to boost") != std::string::npos) again = true;
        check(!again, "and only once -- it is a nudge, not a nag");
    }
    wrAddr32(SR_ADDR_X, 4000); wrAddr32(SR_ADDR_Y, 0);
    check(!saidContains("left"), "far-right error does not also say left");
    check(!saidContains("up") && !saidContains("down"),
          "and does not call an axis that is already inside the box");

    // THE CADENCE. Aaron: "The TTS should speak frequently throughout the game,
    // with maybe a 3-5 second pause in between." Held still on a wrong key, the
    // v0.62.x module said the same phrase once and then nothing for 53 seconds.
    {
        clearSaid();
        int spoke = 0;
        for (int i = 0; i < 20 * 1000 / 16; i++) { tick(); }   // twenty seconds at 16 ms
        spoke = (int)g_said.size();
        check(spoke >= 5, "**it keeps talking while he holds the wrong key** -- "
                          "at least one call every four seconds across twenty");
        bool sawStill = false;
        for (auto& u : g_said) if (u.text.find("still") != std::string::npos) sawStill = true;
        check(sawStill, "**and says 'still right' when the error has not closed** -- "
                        "the sentence that would have saved Aaron's first attempt");
    }

    // -----------------------------------------------------------------------
    // THE FUEL GAUGE (v0.63.2).
    // -----------------------------------------------------------------------
    // Aaron: "We need to inform the player when their boost is exhausted. I
    // believe a sighted player can tell by the movement but a blind player will
    // need to be explicitly informed."
    {
        wrAddr32(SR_ADDR_X, 3000); wrAddr32(SR_ADDR_Y, 0);
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
        clearSaid();
        for (int i = 0; i < 20; i++) tick();
        {
            bool spoke = false;
            for (auto& u : g_said) if (u.text.find("fuel") != std::string::npos ||
                                       u.text.find("Fuel") != std::string::npos) spoke = true;
            check(!spoke, "a full gauge says nothing about fuel");
        }

        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL / 2);        // 50%
        clearSaid();
        for (int i = 0; i < 5; i++) tick();
        check(saidContains("Boost fuel half"), "**half is announced**");
        for (auto& u : g_said)
            if (u.text.find("Boost fuel") != std::string::npos)
                check(!u.interrupt,
                      "**and it is queued, not interrupted** -- a bearing is what "
                      "he acts on and must never be cut in half by a fuel report");

        // Once only: the band has to DROP to be news.
        clearSaid();
        for (int i = 0; i < 60; i++) tick();
        check(!saidContains("Boost fuel half"), "and not again at the same band");

        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL / 5);        // 20%
        clearSaid();
        for (int i = 0; i < 5; i++) tick();
        check(saidContains("Boost fuel low"), "**low is announced**");

        wrAddr32(SR_ADDR_FUEL, 0);
        clearSaid();
        for (int i = 0; i < 5; i++) tick();
        check(saidContains("Boost fuel gauge empty"),
              "**empty is announced -- and it names the GAUGE**. Nothing in "
              "ssspace1/2/3 reads var[1052] and FF8_EN.exe holds no reference to "
              "its address, so the boost goes on working after the bar bottoms "
              "out. Saying \"boost gone\" would be inventing a rule");

        // THE RETRY. "Rinoa was lost in space...forever" -> Try again ->
        // MAPJUMPO 878, which is ssspace3 again, so Here() never goes false and
        // nothing resets. Aaron's 18:34:47 log caught both halves of that: the
        // gauge reads 0 for a moment while the scene tears down, which v0.63.2
        // announced as "Boost fuel gauge empty" over the Try again prompt --
        // and then latched there, so the refilled 8000 of attempt 2 could never
        // announce anything again. Neither is allowed now.
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL / 2);
        clearSaid();
        for (int i = 0; i < 5; i++) tick();          // resync to half quietly or not
        wrAddr32(SR_ADDR_FUEL, 0);                   // <- the teardown frame
        clearSaid();
        for (int i = 0; i < 5; i++) tick();
        check(!saidContains("empty"),
              "**a gauge that falls three bands between two ticks is a scene "
              "resetting, not a tank draining** -- the bands are 25 points apart "
              "and the gauge falls at most 8 of 8000 per script frame");
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);        // <- attempt 2 begins
        clearSaid();
        for (int i = 0; i < 5; i++) tick();
        check(g_said.empty() || !saidContains("fuel"), "the refill is silent");
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL / 2);
        clearSaid();
        for (int i = 0; i < 5; i++) tick();
        check(saidContains("Boost fuel half"),
              "**but the tracker followed it back up, so attempt 2 can still be "
              "told about its own fuel** -- v0.63.2 latched at empty and went "
              "silent for the rest of the scene");
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
        for (int i = 0; i < 5; i++) tick();

        // A refill (a retry) must not announce its way back up. The band word
        // for a full gauge is the empty string, so "nothing about fuel" is not
        // enough on its own -- speaking an empty utterance IS a bug, and the
        // check has to be able to see it.
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
        clearSaid();
        for (int i = 0; i < 60; i++) tick();
        {
            bool spoke = false;
            for (auto& u : g_said) if (u.text.find("fuel") != std::string::npos ||
                                       u.text.find("Fuel") != std::string::npos) spoke = true;
            check(!spoke, "and a gauge that goes back up says nothing at all");
            for (auto& u : g_said)
                check(!u.text.empty(),
                      "**and does not 'announce' the empty string** -- the band "
                      "word for a full tank is \"\", so a rise that reached the "
                      "speaker would be silent to a listener and invisible to a "
                      "test that only greps for the word fuel");
        }

        // Slash reads the boost state and the number, behind the bearing.
        wrAddr32(SR_ADDR_FUEL, (SR_FUEL_FULL * 62) / 100);
        wr8(1034, 8);                                    // the boost is engaged
        clearSaid();
        g_tick += 4000;
        g_keyDown |= 1; tick(); g_keyDown &= ~1;
        check(saidContains("right"), "slash still gives the bearing first");
        check(saidContains("Boost on. Fuel 62 percent."),
              "**and then the boost state and the gauge** -- read off var[1034] "
              "and var[1052], not off the keyboard");
        g_keyDown &= ~1; tick();        // the module edge-detects slash: release it
        wr8(1034, 4);
        clearSaid();
        g_tick += 4000;
        g_keyDown |= 1; tick(); g_keyDown &= ~1;
        check(saidContains("Boost off."), "and says so when it is not engaged");

        // An unreadable gauge is silence, not a report of zero. The state is
        // reset first so the band tracker starts at full -- otherwise a bogus
        // "empty" would be swallowed by the once-only rule and the check would
        // pass for the wrong reason.
        Disc3::Space::Reset();
        g_clockRunning = true;
        wrAddr32(SR_ADDR_X, 3000); wrAddr32(SR_ADDR_Y, 0);
        wrAddr32(SR_ADDR_FUEL, -999999);            // the scene has not started
        clearSaid();
        for (int i = 0; i < 9000 / 16; i++) tick();  // past the settle, through the brief
        clearSaid();                 // BEFORE the press: the first fuel read is on
        pressEnter();                // the very tick that closes the screen
        for (int i = 0; i < 30; i++) tick();
        {
            bool spoke = false;
            for (auto& u : g_said) if (u.text.find("uel") != std::string::npos) spoke = true;
            check(!spoke, "**a gauge outside its own range says nothing** -- the "
                          "same rule the position reading follows, and without it "
                          "the frame before the scene writes 8000 reads as empty");
        }
        // ...and with a gauge that unreadable the module never briefed at all,
        // because v0.64.1 will not open a screen over a scene that has not
        // started. Put the scene back and let it brief, so what follows is
        // testing steering rather than testing this.
        wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
        for (int i = 0; i < 9000 / 16; i++) tick();
        check(GardenBattle::BriefDialogOpen(),
              "**and briefs the moment the scene does start** -- the gate is 'not "
              "yet', not 'never'");
        clearSaid();
        pressEnter();
        wrAddr32(SR_ADDR_X, 4000); wrAddr32(SR_ADDR_Y, 0);
        for (int i = 0; i < 5; i++) tick();
    }

    // Far up-left, with the larger error first.
    wrAddr32(SR_ADDR_X, -6000); wrAddr32(SR_ADDR_Y, 1000);
    clearSaid();
    g_tick += 4000; Disc3::Update();
    {
        std::string s2 = lastSaid();
        size_t a = s2.find("left"), w = s2.find("up");
        check(a != std::string::npos && w != std::string::npos, "both axes are called");
        check(a < w, "the larger error is named first");
    }

    // Far down-right, the opposite corner, so a sign slip on either axis shows.
    wrAddr32(SR_ADDR_X, 5000); wrAddr32(SR_ADDR_Y, -6000);
    clearSaid();
    g_tick += 4000; Disc3::Update();
    {
        std::string s2 = lastSaid();
        check(s2.find("down") != std::string::npos, "negative Y calls DOWN");
        check(s2.find("right") != std::string::npos, "positive X calls RIGHT");
        check(s2.find("down") < s2.find("right"), "and the larger error leads");
    }

    // Entering the AIM box SAYS centred and RINGS NOTHING. v0.63.0 rang the
    // Garden battle's two-note cue on this exact transition; it duplicated a
    // sentence the module was already speaking and covered the next one.
    g_tones = 0;
    wrAddr32(SR_ADDR_X, 10); wrAddr32(SR_ADDR_Y, -20);
    clearSaid();
    g_tick += 4000; Disc3::Update();
    check(saidContains("centred"), "the box says centred");
    check(g_tones == 0,
          "**and rings nothing** -- Aaron: \"the TTS announcement I think is "
          "sufficient\"");

    // PARKED ON THE TARGET. Aaron's attempt 2 sat at one position for the last
    // sixty-three seconds of the scene and heard "centred" about fifty times.
    // It must still talk -- silence in a scene with no other feedback reads as
    // a mod that died -- but at a rhythm a person can sit inside.
    {
        wrAddr32(SR_ADDR_X, 10); wrAddr32(SR_ADDR_Y, -20);
        clearSaid();
        for (int i = 0; i < 30 * 1000 / 16; i++) tick();     // thirty seconds, still
        const int spoke = (int)g_said.size();
        check(spoke >= 6,
              "**it still speaks while he holds the target** -- going quiet in a "
              "scene with no other feedback reads as a mod that stopped working");
        check(spoke <= 14,
              "**but not once a second for a minute** -- thirty seconds at the "
              "near cadence would be twenty-five");
        bool sawStill = false;
        for (auto& u : g_said) if (u.text == "still centred") sawStill = true;
        check(sawStill, "and it says 'still centred' after the first, which is the "
                        "idiom the steering calls already use");
        // Keep her moving inside the box and the fast cadence comes straight
        // back: the slow rhythm is for a gap that is not moving, not for the box.
        clearSaid();
        for (int i = 0; i < 6000 / 16; i++) {
            wrAddr32(SR_ADDR_X, 10 + (i & 7));      // never still
            tick();
        }
        check((int)g_said.size() >= 4,
              "**and movement inside the box puts the near cadence back**");
        wrAddr32(SR_ADDR_X, 10);
        for (int i = 0; i < 5; i++) tick();
    }

    // 120 is inside the GAME's box but outside the aim box: it must still steer.
    wrAddr32(SR_ADDR_X, 120);
    clearSaid();
    g_tick += 4000; Disc3::Update();
    check(saidContains("right"), "leaving the aim box steers again");
    check(SrCentred(120, 0) && !SrHeld(120, 0),
          "**120 wins the game but is not 'centred'** -- the steering deadband is "
          "SR_AIM_BOX, the verdict is still the game's SR_WIN_BOX");

    // Out-of-clamp readings are treated as "the scene has not started".
    Disc3::Space::Reset();
    wrAddr32(SR_ADDR_X, 999999); wrAddr32(SR_ADDR_Y, 0);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(), "an out-of-range reading says nothing at all");
    check(!g_clockHeld,
          "**and does not stop the clock either** -- a reading the module will "
          "not act on is a scene it has not entered");

    // F9 skip pins BOTH error terms at zero, every tick, against a scene that
    // keeps writing them.
    // ...and it works while the scene is still talking, before the brief has
    // even opened: that is exactly when a player who does not want to fly this
    // reaches for it.
    Disc3::Space::Reset();
    wrAddr32(SR_ADDR_X, 3000); wrAddr32(SR_ADDR_Y, -2500);
    clearSaid();
    g_dialogOpen = true;
    tick();
    check(g_said.empty() && !GardenBattle::BriefDialogOpen(), "still waiting to brief");
    check(g_clockHeld, "with the clock stopped");
    g_keyDown = 2; tick(); g_keyDown = 0;
    g_dialogOpen = false;
    check(saidContains("Skip on"), "F9 announces the skip");
    check(!GardenBattle::BriefDialogOpen(), "and takes the screen down with it");
    check(!g_clockHeld,
          "**and gives the clock back** -- the skip flies the scene, it does not "
          "suspend it");
    check(rdAddr32(SR_ADDR_X) == 0 && rdAddr32(SR_ADDR_Y) == 0, "skip zeroes both axes");
    for (int i = 0; i < 30; i++) {
        wrAddr32(SR_ADDR_X, 2000); wrAddr32(SR_ADDR_Y, 2000);   // the scene fights back
        tick();
        check(rdAddr32(SR_ADDR_X) == 0 && rdAddr32(SR_ADDR_Y) == 0,
              "skip re-zeroes every single tick");
    }
    clearSaid();
    for (int i = 0; i < 30; i++) tick();
    check(g_said.empty(), "the skip stops steering calls");
    check(!SpaceRescueActive(),
          "and the catalog comes back once the skip is on -- there is nothing to "
          "fly any more");

    // Walking out of the field while the clock is held must release it: a hold
    // that outlives its scene freezes the next one.
    Disc3::Space::Reset();
    wrAddr32(SR_ADDR_X, 2000); wrAddr32(SR_ADDR_Y, 0);
    g_dialogOpen = true;
    tick();
    check(g_clockHeld, "held on the way in");
    g_dialogOpen = false;

    restoreSignature();

    // NOT ONE BEEP, ANYWHERE IN THE SCENE. Counted from the top of section 3.
    check(g_beeps == 0,
          "**nothing beeped at any point in the whole approach** -- Aaron: \"Let's "
          "get rid of the beep sound effect. It is extremely distracting\"");

    setField("bgbtl_1", 100); tick();

    // =======================================================================
    // 4. PROPAGATORS (#112)
    // =======================================================================
    check(PgTableConsistent(), "the pair table closes");
    wr8(PG_VAR_DEAD, 0); wr8(PG_VAR_PENDBIT, 0); wr8(PG_VAR_PENDFLAG, 0);

    // ---- ARRIVAL IS SILENT NOW ------------------------------------------
    // Aaron, 2026-08-24: "We don't want to simply announce when entering the
    // field. We also don't want to proactively inform the player where to find
    // it's pair." v0.55.0 did both, on every arrival. The colour now reaches him
    // through the catalog instead, which is where every other thing in a room
    // reaches him.
    setField("rgair1", 823);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(),
          "**walking into a Propagator's room says nothing at all** -- the "
          "catalog names it, the mod does not narrate it");

    // ---- BUT THE HELP KEY STILL ANSWERS EVERYTHING ----------------------
    clearSaid();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("yellow") != std::string::npos, "slash names the colour in front of him");
        check(s.find("passenger cabin") != std::string::npos,
              "**and WHERE its pair is -- asked for, not volunteered**");
    }

    // ---- AND A KILL SAYS NOTHING EITHER (v0.69.0) -----------------------
    // Aaron: "scrap the post-battle announcements... The whole point is the
    // player is supposed to listen to the terminal in the passenger compartment,
    // which tells them to kill them in matching color pairs. The current
    // post-battle help text essentially does this and makes the dialogue in the
    // passenger compartment redundant." The game teaches this puzzle itself, at
    // a terminal the player is meant to find; a mod that recites the rule after
    // every kill has replaced the part worth playing.
    const Propagator* air = PgForField("rgair1");
    check(air != nullptr && air->bit == 0x01, "rgair1 holds bit 0x01");
    wr8(PG_VAR_DEAD, 0x01);
    wr8(PG_VAR_PENDBIT, 0x01);
    wr8(PG_VAR_PENDFLAG, PG_PENDING_MASK);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(),
          "**a kill announces nothing at all** -- the terminal in the passenger "
          "compartment is where that rule comes from, and it is worth finding");

    // Walking into the partner's room: still silent.
    setField("rgguest2", 832);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(), "**and the partner's room is silent too**");

    // Closing a pair, and finishing the whole hunt: still nothing.
    wr8(PG_VAR_DEAD, 0x81); wr8(PG_VAR_PENDFLAG, 0); wr8(PG_VAR_PENDBIT, 0);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(), "closing a pair is silent");
    wr8(PG_VAR_DEAD, PG_ALL_DEAD);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(), "and so is the last one");

    // ...but the board is still TRACKED, and the help key still reports it all.
    wr8(PG_VAR_DEAD, 0x01); wr8(PG_VAR_PENDBIT, 0x01); wr8(PG_VAR_PENDFLAG, PG_PENDING_MASK);
    setField("rgair1", 823);
    for (int i = 0; i < 5; i++) tick();
    clearSaid();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("yellow") != std::string::npos,
              "**the help key still has the whole board** -- nothing was forgotten, "
              "it is simply not narrated");
        check(s.find("passenger cabin") != std::string::npos,
              "including where the match is, when he asks for it");
    }

    // =======================================================================
    // 4b. HOLDING THEM STILL, against a real entity array.
    // =======================================================================
    // Aaron: "We also need to prevent the Propagator from moving when the player
    // and the Propagator are on the same field, otherwise the player is going to
    // get caught while using navigation tools in the mod."
    {
        Disc3::Props::Reset();
        wr8(PG_VAR_DEAD, 0); wr8(PG_VAR_PENDBIT, 0); wr8(PG_VAR_PENDFLAG, 0);
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';

        // rgroad2: the player is entity 0, the Propagator is the script's slot 2.
        s_playerEntityIdx = 0;
        auto place = [&](int idx, int px, int py) {
            uint8_t* b = entBlock(idx);
            *(uint16_t*)(b + 0x1FA) = 7;                 // on the walkmesh
            *(int32_t*)(b + 0x190) = px * 4096;
            *(int32_t*)(b + 0x194) = py * 4096;
        };
        auto speedOf = [&](int idx, int off) {
            return (unsigned)*(uint16_t*)(entBlock(idx) + off);
        };
        const int PROP = 2;
        // The player is at x=50, not 0: GetEntityPos treats an exactly-zero
        // position as "not placed yet", in the probe exactly as in the mod.
        place(0, 50, 0);
        place(PROP, 50, 2000);                           // 2000 units away
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;     // the engine's patrol speed
        *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
        setField("rgroad2", 840);
        g_logged.clear();
        tick();
        if (getenv("PGDEBUG")) for (auto& l : g_logged) std::printf("LOG: %s\n", l.c_str());
        check(speedOf(PROP, 0x1FE) == 0 && speedOf(PROP, 0x200) == 0,
              "**a Propagator across the room has its move speed pinned to zero** -- "
              "the engine's step is unit(facing) * [ent+0x1FE] >> 8, so a zero there "
              "is a zero step and the thing does not move");
        check(loggedContains("holding ent2"), "and the log names the entity it pinned");

        // ---- THE VETO, which is what the per-tick pin was not ------------
        // Aaron's first run: "Suppression didn't seem to work as I stood still
        // and each one ran for me it seemed." The log had confident "holding"
        // lines and a Propagator crossing 2000 units in two seconds underneath
        // them -- the mod wrote a zero once a tick and the script wrote 8000
        // back in between. A tick is not a frame, and the loser of that race is
        // the player. So the two move opcodes are chained instead.
        check(FF8Addresses::opcodeTable[0x03D] != (uint32_t)(uintptr_t)&FakeSetSpeed,
              "**the set-speed opcode is hooked** -- 0x03D is where 8000 comes from");
        check(FF8Addresses::opcodeTable[0x079] != (uint32_t)(uintptr_t)&FakeApproach,
              "and so is MOVEAPPROACH, which re-copies it when a move starts");
        {
            // The probe cannot CALL through the table: it is uint32_t, because
            // the game is 32-bit, and a host function pointer does not survive
            // the truncation. What it can do is check that the entry was
            // replaced, that the original was saved so the chain has something
            // to call, and then drive the veto itself -- which is where all the
            // logic is.
            check(FF8Addresses::opcodeTable[0x03D] ==
                      (uint32_t)(uintptr_t)&Disc3::Props::PgHookedSpeed,
                  "the table entry now points at the mod's hook");
            check(Disc3::Props::s_origSpeedOp ==
                      (uint32_t)(uintptr_t)&FakeSetSpeed,
                  "**and the original was saved, not discarded** -- the mod chains, "
                  "it does not replace an engine opcode it only half understands");
            check(Disc3::Props::s_origApproachOp ==
                      (uint32_t)(uintptr_t)&FakeApproach,
                  "same for MOVEAPPROACH, which re-copies the speed when a move starts");

            void* held  = entBlock(PROP);
            void* other = entBlock(5);

            // The script has just asked for the charge speed and the original
            // opcode has written it. This is the instant the veto exists for.
            *(uint16_t*)((char*)held + 0x1FE) = 8000;
            *(uint16_t*)((char*)held + 0x200) = 8000;
            Disc3::Props::PgZeroIfHeld(held);
            check(speedOf(PROP, 0x1FE) == 0 && speedOf(PROP, 0x200) == 0,
                  "**and the speed is back to zero inside the same call** -- "
                  "frame-exact, so no amount of looping by the script can outrun "
                  "it the way it outran a once-a-tick write");

            // It touches NOBODY else. A veto that fired on every entity would
            // stop the party.
            *(uint16_t*)((char*)other + 0x1FE) = 8000;
            *(uint16_t*)((char*)held  + 0x1FE) = 8000;
            Disc3::Props::PgZeroIfHeld(other);
            check(*(uint16_t*)((char*)other + 0x1FE) == 8000,
                  "**another entity's speed is left exactly alone** -- the hook keys "
                  "on the block it was told to hold, not on the opcode");
            check(speedOf(PROP, 0x1FE) == 8000,
                  "**and a call about somebody else does NOTHING AT ALL** -- not even "
                  "to the entity we are holding. The first draft wrote through "
                  "s_heldBlock instead of the argument, so a loosened guard would "
                  "have gone on pinning the right entity for the wrong reason and "
                  "this probe would have stayed green");
            *(uint16_t*)((char*)held + 0x1FE) = 8000;
            Disc3::Props::PgZeroIfHeld(held);
            check(speedOf(PROP, 0x1FE) == 0, "and the held one is still vetoed");

            // ---- AND THE ONE THAT ACTUALLY STOPS THE FIGHT -------------
            // Aaron, after the 00:02 run: "Looks like the Propagator battles
            // trigger just by walking up to them, pressing Confirm isn't
            // required." The log shows the monster 213 units away, still held,
            // "speed reads 0/0, veto fired 464 times" -- and the battle starts.
            // Holding a monster still and then walking into it is still walking
            // into it. Its script asks "am I touching him?" two instructions
            // before it asks for a battle, and that is the question to answer.
            check(FF8Addresses::opcodeTable[0x05B] ==
                      (uint32_t)(uintptr_t)&Disc3::Props::PgHookedTouch,
                  "**the contact test is hooked too** -- 0x05B is what the BATTLE "
                  "two instructions later is waiting on");
            *(int32_t*)((char*)held + 0x140) = 1;      // the engine says: touching
            Disc3::Props::PgRefuseContactIfHeld(held);
            check(*(int32_t*)((char*)held + 0x140) == 0,
                  "**a held Propagator is told it is not touching him** -- which is "
                  "exactly true of a monster he has not said yes to, and sends the "
                  "script round its loop instead of into the fight");
            *(int32_t*)((char*)other + 0x140) = 1;
            Disc3::Props::PgRefuseContactIfHeld(other);
            check(*(int32_t*)((char*)other + 0x140) == 1,
                  "and nobody else's contact test is touched");

            // ...and it goes quiet the moment the hold is given back, or the
            // restore would be undone by the mod's own veto.
            Disc3::Props::GiveBackHold("probe");
            *(uint16_t*)((char*)held + 0x1FE) = 8000;
            Disc3::Props::PgZeroIfHeld(held);
            check(speedOf(PROP, 0x1FE) == 8000,
                  "**a released Propagator is not still being vetoed**");
            *(int32_t*)((char*)held + 0x140) = 1;
            Disc3::Props::PgRefuseContactIfHeld(held);
            check(*(int32_t*)((char*)held + 0x140) == 1,
                  "**and once he has said yes, contact means contact again** -- "
                  "a Propagator that can never be touched can never be killed");
        }

        // The script fights back the way `dic` does: it rewrites the speed.
        // Every tick must take it away again.
        for (int i = 0; i < 20; i++) {
            *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
            tick();
            check(speedOf(PROP, 0x1FE) == 0,
                  "**and it is taken away again every single tick** -- the pin is a "
                  "race with the script and the loser of that race is the player");
        }

        // HE WALKS UP TO IT AND IT STILL DOES NOT MOVE. This is the doorway
        // case from Aaron's 23:04 run: one was waiting where he came in, and a
        // distance rule cannot tell "he walked up to me" from "he walked in".
        clearSaid();
        place(0, 50, 1900);                              // 100 units away
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        tick();
        check(speedOf(PROP, 0x1FE) == 0,
              "**standing next to one does not free it** -- being close is where the "
              "navigation leaves him, not a decision to fight");
        for (int i = 0; i < 5; i++) tick();
        {
            std::string s;
            for (auto& u : g_said) s += u.text + " | ";
            check(s.find("within reach") != std::string::npos &&
                  s.find("Confirm") != std::string::npos,
                  "**but he is told Confirm is what starts it** -- a held monster "
                  "makes no sound and does not move, so without this he could stand "
                  "on one indefinitely");
        }
        // ...and it says it once, not once a tick.
        clearSaid();
        for (int i = 0; i < 40; i++) tick();
        check(g_said.empty(), "and says it once per approach, not once a tick");

        // Confirm, in reach: the fight is his.
        g_confirmDown = true; tick(); g_confirmDown = false;
        check(speedOf(PROP, 0x1FE) == 1500 && speedOf(PROP, 0x200) == 1500,
              "**Confirm within reach gives the speed back, exactly as it was** -- he "
              "walks up to the thing and starts the fight himself");
        check(loggedContains("released ent2"), "and the release is logged");

        // Nothing re-holds it afterwards, however far he walks away.
        place(0, 50, 800);                               // 1200 units clear
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        for (int i = 0; i < 10; i++) tick();
        check(speedOf(PROP, 0x1FE) == 1500,
              "**and it is never re-held** -- one that re-froze after being let go "
              "would be one that can never be fought");

        // ---- THE LIVE INDEX IS NOT THE SCRIPT SLOT ----------------------
        // Aaron, 11:52 BAT: "the Propagator in Aisle 6 still attacked without
        // pressing confirm." Aisle 6 is rgroad3, and it is the one Propagator
        // field whose model-less script object -- `dic` -- sits BEFORE the
        // monsters. The live array is compacted, so it takes no place in it:
        //
        //   script slot   0 squall  1 rinoa  2 dic   3 alien01  4 alien02  5 dp01
        //   live index    0 squall  1 rinoa  -       2 alien01  3 alien02  4 dp01
        //
        // The mod held live 4 and reported "speed reads 0/0" for the whole room.
        // Both true and worthless: it was reading back its own write to `dp01`,
        // a draw point, while the actual Propagator charged.
        {
            Disc3::Props::Reset();
            memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
            for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
            const int MODEL_OFF = 0x218;
            // The live array as the engine compacts it, with each block carrying
            // the SETMODEL parameter its script assigned.
            const int models[5] = { 0, 1, 2, 3, 4 };   // squall rinoa alien01 alien02 dp01
            for (int i = 0; i < 5; i++)
                *(int16_t*)(entBlock(i) + MODEL_OFF) = (int16_t)models[i];
            FF8Addresses::liveCount = 5;
            const int ALIEN02 = 3, DP01 = 4;

            place(0, 50, 0);
            place(ALIEN02, 50, 2000);
            place(DP01,   -326, 1398);
            *(uint16_t*)(entBlock(ALIEN02) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(ALIEN02) + 0x200) = 1500;
            *(uint16_t*)(entBlock(DP01)    + 0x1FE) = 1500;   // whatever was there
            setField("rgroad3", 844);
            g_logged.clear();
            tick();

            check(speedOf(ALIEN02, 0x1FE) == 0 && speedOf(ALIEN02, 0x200) == 0,
                  "**the Propagator on rgroad3 is the one that gets held** -- its "
                  "live index is 3 because the compacted array skips the model-less "
                  "`dic`, and the script slot the table used to trust says 4");
            check(speedOf(DP01, 0x1FE) == 1500,
                  "**and the draw point at live 4 is left completely alone** -- this "
                  "is the entity the mod spent the whole of Aisle 6 pinning to zero, "
                  "reading its own zeros back, and reporting as a working hold");
            check(loggedContains("live entity 3 by model id"),
                  "and the log says which key answered, so the next wrong row is "
                  "one line rather than one BAT");
            check(loggedContains("the script slot would have been wrong here"),
                  "**and it says out loud that the fallback would have been wrong** "
                  "-- the disagreement is the finding, not a detail");

            // PAST THE LIVE COUNT IS THE LAST FIELD'S RUBBISH. The array is not
            // cleared between fields, it is refilled and the count is moved, so
            // block 7 in a five-entity room still holds whatever the previous
            // room put there -- including, sooner or later, a model 3. Reading
            // it would make the key look ambiguous and quietly demote the mod
            // back to the guess that broke Aisle 6.
            *(int16_t*)(entBlock(7) + MODEL_OFF) = (int16_t)3;   // stale, out of range
            Disc3::Props::Reset();
            g_logged.clear();
            place(0, 50, 0); place(ALIEN02, 50, 2000);
            *(uint16_t*)(entBlock(ALIEN02) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(ALIEN02) + 0x200) = 1500;
            setField("rgroad3", 844);
            tick();
            check(speedOf(ALIEN02, 0x1FE) == 0 && loggedContains("live entity 3 by model id"),
                  "**the search stops at the live count** -- a stale block past the "
                  "end is the previous field's, and letting it vote turns a unique "
                  "key into a refused one");
            *(int16_t*)(entBlock(7) + MODEL_OFF) = (int16_t)0;

            // Every OTHER Propagator field must be unmoved by this: in seven of
            // the eight the model-less entity comes after the monster, so slot
            // and live index agree and always did.
            Disc3::Props::Reset();
            g_logged.clear();
            place(0, 50, 0);
            place(2, 50, 2000);
            *(int16_t*)(entBlock(2) + MODEL_OFF) = (int16_t)2;   // rgroad2 alien01
            *(uint16_t*)(entBlock(2) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(2) + 0x200) = 1500;
            setField("rgroad2", 840);
            tick();
            check(speedOf(2, 0x1FE) == 0,
                  "**and rgroad2, where slot and live index agree, still holds "
                  "entity 2** -- the model key must not fix one field by breaking "
                  "the seven that were right");

            // A MODEL ID THAT MATCHES TWICE IS NOT AN ANSWER. Picking either one
            // would be a coin toss with a whole scene riding on it.
            Disc3::Props::Reset();
            g_logged.clear();
            *(int16_t*)(entBlock(5) + MODEL_OFF) = (int16_t)2;   // a second model 2
            FF8Addresses::liveCount = 6;
            place(5, 50, 2500);
            setField("rgroad2", 840);
            tick();
            check(!loggedContains("by model id"),
                  "**an ambiguous model id is refused, not guessed** -- two blocks "
                  "carrying the same key is evidence the key does not identify "
                  "anybody in this field");
            *(int16_t*)(entBlock(5) + MODEL_OFF) = (int16_t)0;

            // THE ANSWER CAN ARRIVE LATE, AND THE LOG MUST SHOW THE CORRECTION.
            // The model key needs the live array populated and the catalog join
            // needs the catalog refresh to have run; on the tick this module
            // first asks, neither may be true. The old code logged once and
            // latched, so the guess was printed and the correction was not --
            // which is why "script order says 4" is the only line rgroad3 ever
            // produced. It now prints once per DISTINCT answer.
            Disc3::Props::Reset();
            memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
            for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
            FF8Addresses::liveCount = 0;                 // nothing instantiated yet
            place(0, 50, 0); place(3, 50, 2000); place(4, -326, 1398);
            setField("rgroad3", 844);
            g_logged.clear();
            tick();
            check(loggedContains("script slot -- a guess"),
                  "**with nothing to resolve against, the guess is named a guess**");
            for (int i = 0; i < 5; i++)
                *(int16_t*)(entBlock(i) + MODEL_OFF) = (int16_t)i;
            FF8Addresses::liveCount = 5;                 // the array populates
            tick();
            check(loggedContains("live entity 3 by model id"),
                  "**and the correction is logged when the evidence arrives** -- the "
                  "old code latched on the first answer, so the guess was printed and "
                  "the correction never was");

            FF8Addresses::liveCount = (uint8_t)32;
        }

        // ---- AND A HOLD THAT NEVER SEES A SCRIPT SAYS SO -----------------
        // The number that told the truth about rgroad3 was the veto count: 0,
        // in a room where every other field ran into the hundreds. Nothing in
        // the mod read it, so it sat in the log through two builds. Now it
        // reads itself.
        {
            Disc3::Props::Reset();
            memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
            for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
            place(0, 50, 0); place(2, 50, 2000);
            *(uint16_t*)(entBlock(2) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(2) + 0x200) = 1500;
            setField("rgroad2", 840);
            tick();
            check(speedOf(2, 0x1FE) == 0, "holding something");
            g_logged.clear();
            // Four seconds of holding, and its script never runs an opcode.
            for (int i = 0; i < 5; i++) tick(1000);
            check(loggedContains("HAS RUN NO SCRIPT"),
                  "**a hold whose entity runs no script for seconds, with every "
                  "hook live, reports itself as probably the wrong entity** -- "
                  "'speed reads 0/0' cannot tell a held monster from a lamp post, "
                  "and that ambiguity is what hid Aisle 6");
            // ...and it says it once, not once a second, or the log it has to be
            // found in becomes the thing burying it.
            g_logged.clear();
            for (int i = 0; i < 5; i++) tick(1000);
            check(!loggedContains("HAS RUN NO SCRIPT"), "and it says it once per hold");

            // A hold whose entity IS running its script never trips it.
            Disc3::Props::Reset();
            place(0, 50, 0); place(2, 50, 2000);
            *(uint16_t*)(entBlock(2) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(2) + 0x200) = 1500;
            setField("rgroad2", 840);
            tick();
            g_logged.clear();
            for (int i = 0; i < 5; i++) {
                *(uint16_t*)(entBlock(2) + 0x1FE) = 8000;
                Disc3::Props::PgZeroIfHeld(entBlock(2));   // its script ran
                tick(1000);
            }
            check(!loggedContains("HAS RUN NO SCRIPT"),
                  "**and a Propagator whose script is being vetoed is never accused** "
                  "-- the test is silence from the entity, not distance or time");
        }

        // ---- AND THE HOOKS MUST SURVIVE A BATTLE -------------------------
        // Aaron, after the 11:27 run: "after I entered the passenger compartment
        // and the forced encounter with the Propagator in there, it seemed like
        // the freeze stopped working on all the rest ... I got attacked twice
        // after the passenger compartment without pressing confirm as expected."
        // The log says exactly why. Before that fight: "veto fired 27 times ...
        // contact refused 12 times". After it, every hold: "veto fired 0 times
        // ... contact refused 0 times", and the battle starts on walk-up. Zero
        // is not a race being lost -- a hold that loses a race still fires. Zero
        // is a hook that is not in the table any more: the battle -> field return
        // rebuilds pExecuteOpcodeTable, and the old s_hooksInstalled latch had
        // been true since the first field, so nothing ever put them back.
        {
            Disc3::Props::Reset();
            memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
            setField("rgroad2", 840);
            place(0, 50, 0); place(PROP, 50, 2000);
            *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
            tick();
            check(speedOf(PROP, 0x1FE) == 0, "held, with the hooks in place");

            // The engine takes its table back, which is all a battle does to us.
            FF8Addresses::opcodeTable[0x03D] = (uint32_t)(uintptr_t)&FakeSetSpeed;
            FF8Addresses::opcodeTable[0x079] = (uint32_t)(uintptr_t)&FakeApproach;
            FF8Addresses::opcodeTable[0x05B] = (uint32_t)(uintptr_t)&FakeTouch;
            g_logged.clear();
            tick();

            check(FF8Addresses::opcodeTable[0x03D] ==
                      (uint32_t)(uintptr_t)&Disc3::Props::PgHookedSpeed &&
                  FF8Addresses::opcodeTable[0x079] ==
                      (uint32_t)(uintptr_t)&Disc3::Props::PgHookedApproach &&
                  FF8Addresses::opcodeTable[0x05B] ==
                      (uint32_t)(uintptr_t)&Disc3::Props::PgHookedTouch,
                  "**a table the engine restored is re-hooked on the very next tick** -- "
                  "this is the whole of the bug he hit: three entries quietly went back "
                  "to the engine's handlers and a latch said the job was done");
            check(loggedContains("RE-INSTALLED"),
                  "and the log says RE-INSTALLED, not installed, so a BAT can see it "
                  "happen rather than infer it from counters that read zero");

            // THE PART THAT WOULD BE UNSURVIVABLE. Re-patching without checking
            // whose address is in the entry would save OUR hook as the original,
            // and the hook would then call itself: a stack overflow on the first
            // move opcode, in a build whose whole point is that it does not crash.
            check(Disc3::Props::s_origSpeedOp == (uint32_t)(uintptr_t)&FakeSetSpeed &&
                  Disc3::Props::s_origApproachOp == (uint32_t)(uintptr_t)&FakeApproach &&
                  Disc3::Props::s_origTouchOp == (uint32_t)(uintptr_t)&FakeTouch,
                  "**and the saved original is the ENGINE's handler, never our own** -- "
                  "the compare is what makes a per-tick re-patch safe; without it the "
                  "second install chains the hook to itself");

            // A tick that changed nothing must not keep re-patching or re-logging.
            g_logged.clear();
            for (int i = 0; i < 10; i++) tick();
            check(!loggedContains("RE-INSTALLED"),
                  "and an untouched table is left alone -- three compares a tick, no writes");

            // And the veto works again, which is the thing he actually lost.
            *(uint16_t*)(entBlock(PROP) + 0x1FE) = 8000;
            Disc3::Props::PgZeroIfHeld(entBlock(PROP));
            check(speedOf(PROP, 0x1FE) == 0, "the movement veto is live again after the battle");
            *(int32_t*)(entBlock(PROP) + 0x140) = 1;
            Disc3::Props::PgRefuseContactIfHeld(entBlock(PROP));
            check(*(int32_t*)(entBlock(PROP) + 0x140) == 0,
                  "**and so is the contact veto** -- which is the one that decides "
                  "whether he gets to press Confirm or just gets attacked");
        }

        // LEAVING THE FIELD MUST NOT WRITE ANYTHING. The live array is rebuilt
        // on every field load, so slot 2 in the next room is somebody else
        // entirely: a "restore" fired after the transition does not give a
        // Propagator its speed back, it hands a stranger a number.
        // A fresh approach, because the one above has been engaged and nothing
        // re-holds an engaged Propagator.
        Disc3::Props::Reset();
        setField("rgroad2", 840);
        place(0, 50, 0); place(PROP, 50, 2000);
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
        tick();
        check(speedOf(PROP, 0x1FE) == 0, "held again before the transition");
        g_logged.clear();
        setField("bgbtl_1", 100);
        tick();
        check(speedOf(PROP, 0x1FE) == 0 && speedOf(PROP, 0x200) == 0,
              "**walking out writes nothing at all** -- the entity we were holding "
              "does not exist any more, and slot 2 in the next field is a stranger");
        check(loggedContains("forgetting the hold"), "and the log says it forgot rather than restored");
        // ...and the raw field id, not the debounced name, is what drops it: the
        // name arrives seconds late, and every write in between lands on the new
        // field's entities.
        Disc3::Props::Reset();
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        place(0, 50, 0); place(PROP, 50, 2000);
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
        setField("rgroad2", 840);
        tick();
        check(speedOf(PROP, 0x1FE) == 0, "holding");
        g_fieldId = 841;                                 // the id moves, the name has not
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        tick();
        check(speedOf(PROP, 0x1FE) == 1500,
              "**the raw id moving drops the hold immediately** -- waiting for the "
              "debounced name would mean seconds of pinning the next field's "
              "entities to zero");

        // ---- THE TWO THAT DO NOT MOVE ARE GATED, NOT FROZEN --------------
        // Aaron, 12:30 BAT: "Found another one that activated without me
        // pressing X." The battle was on rgroad1. Freezing and consent are
        // different problems and PG_LIST had one column for both, so a
        // Propagator with nothing to freeze was read as one with nothing to do
        // -- while it ran `0x013 0; 0x05B; PSHL 0; JMPZ` into BATTLE 815 sixty
        // times a second, standing perfectly still, waiting to be walked into.
        Disc3::Props::Reset();
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        place(0, 50, 0); place(PROP, 50, 3000);
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
        setField("rgair1", 823);
        for (int i = 0; i < 10; i++) tick();
        check(speedOf(PROP, 0x1FE) == 1500,
              "**rgair1's Propagator still has its speed left alone** -- its script "
              "has no move opcode at all, so pinning one would be a write with "
              "nothing on the other end of it");
        {
            // ...and yet its contact test is refused, which is the whole point.
            void* prop = entBlock(PROP);
            *(int32_t*)((char*)prop + 0x140) = 1;      // the engine says: touching
            Disc3::Props::PgRefuseContactIfHeld(prop);
            check(*(int32_t*)((char*)prop + 0x140) == 0,
                  "**but it is told it is NOT touching him** -- not moving does not "
                  "make it harmless, and this is the instruction the BATTLE two "
                  "later is waiting on");
            check(loggedContains("gating ent"),
                  "and the log says gating, not holding, so the two are not confused "
                  "again the next time one of them misbehaves");
        }
        // AND IT MUST NOT BE ACCUSED OF BEING THE WRONG ENTITY. The
        // silent-veto warning asks "has this thing's script run at all?", and a
        // gated-but-not-frozen Propagator NEVER trips the movement veto -- it
        // never asks for a speed. Testing only that counter would print the
        // warning on every hold in rgair1 and rgroad1, which is the fastest way
        // to teach a reader that the loudest line in the log means nothing.
        g_logged.clear();
        for (int i = 0; i < 5; i++) {
            *(int32_t*)(entBlock(PROP) + 0x140) = 1;
            Disc3::Props::PgRefuseContactIfHeld(entBlock(PROP));   // its script ran
            tick(1000);
        }
        check(!loggedContains("HAS RUN NO SCRIPT"),
              "**a Propagator that never moves is not accused of being the wrong "
              "entity** -- what proves it is alive is the contact test, which is "
              "the one thing every Propagator runs whether it walks or not");
        // ...but a genuinely silent one still is. A FRESH hold, because the
        // counters are cumulative for the life of one: an entity that has run a
        // script even once during this hold has answered the question for good.
        Disc3::Props::Reset();
        place(0, 50, 0); place(PROP, 50, 3000);
        setField("rgair1", 823);
        tick();
        g_logged.clear();
        for (int i = 0; i < 5; i++) tick(1000);
        check(loggedContains("HAS RUN NO SCRIPT"),
              "**and one that runs neither opcode for seconds still is** -- the "
              "warning is weaker for these two, not switched off");
        Disc3::Props::Reset();
        place(0, 50, 0); place(PROP, 50, 3000);
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
        setField("rgair1", 823);
        tick();
        // Walk up and press Confirm: the fight is his, exactly as with a mover.
        place(PROP, 50, 300);                            // within two steps
        tick();
        g_confirmDown = true; tick(); g_confirmDown = false;
        {
            void* prop = entBlock(PROP);
            *(int32_t*)((char*)prop + 0x140) = 1;
            Disc3::Props::PgRefuseContactIfHeld(prop);
            check(*(int32_t*)((char*)prop + 0x140) == 1,
                  "**and once he presses Confirm, contact means contact** -- a "
                  "Propagator that can never be touched can never be killed, and "
                  "all eight have to die for the lift to open");
            check(saidContains("Released"), "and he is told it is his fight now");
        }
        // The release still gets a line even though nothing was written: with a
        // log that only reports writes, "nothing to give back" and "nothing
        // happened" are the same silence.
        check(loggedContains("nothing to give back"),
              "**a gate-only release is logged** -- it has no speed to restore and "
              "that must not make it invisible");

        // rgroad1 is the field the BAT actually caught, so it gets its own row.
        Disc3::Props::Reset();
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        place(0, 50, 0); place(3, 50, 3000);             // rgroad1's alien01 is slot 3
        setField("rgroad1", 834);
        for (int i = 0; i < 3; i++) tick();
        {
            void* prop = entBlock(3);
            *(int32_t*)((char*)prop + 0x140) = 1;
            Disc3::Props::PgRefuseContactIfHeld(prop);
            check(*(int32_t*)((char*)prop + 0x140) == 0,
                  "**rgroad1's is gated too** -- this is the one that took him in "
                  "the lift corridor at 12:31:41 while standing still");
        }

        // ---- ZERO IS NOT A DISTANCE ---------------------------------------
        // Every gated room in the 20:25 log opens with "gating entN at 0 units",
        // because on the first tick the rebuilt entity array can still have the
        // party and the monster reading the same point. One second after that
        // line, rgroad1 announced "Green Propagator within reach. Press Confirm
        // to fight it." with the thing across the corridor.
        //
        // The announcement is the harmless half. The same reading satisfies the
        // release test, so a Confirm pressed on the frame he walks in -- which
        // is exactly when a player is still mashing through the last room's
        // dialog -- would un-gate a monster he has never seen.
        {
            Disc3::Props::Reset();
            memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
            for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
            wr8(PG_VAR_DEAD, 0);
            place(0, 50, 900); place(PROP, 50, 900);      // the same point, exactly
            *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
            *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
            setField("rgroad2", 842);
            clearSaid();
            for (int i = 0; i < 30; i++) tick();
            check(g_said.empty(),
                  "**a Propagator reading zero units away says nothing** -- that is "
                  "not a monster he is standing on, it is a room that has not "
                  "finished loading");
            check(speedOf(PROP, 0x1FE) == 0, "and it is held, not released");

            // And Confirm on that reading does NOT hand him the fight.
            g_confirmDown = true; tick(); g_confirmDown = false;
            check(speedOf(PROP, 0x1FE) == 0,
                  "**and Confirm on a zero reading does not release it** -- this is "
                  "the frame he walks in on, still pressing the key that closed the "
                  "last room's dialog");

            // A real reading, and everything works exactly as before.
            place(PROP, 50, 1200);                        // 300 units: within reach
            tick();
            check(saidContains("within reach"),
                  "**and a real reading still cues him** -- the rule refuses one "
                  "impossible measurement, it does not narrow the bubble");
            g_confirmDown = true; tick(); g_confirmDown = false;
            check(speedOf(PROP, 0x1FE) == 1500, "and Confirm still starts the fight");
        }

        // ---- AND THE IDENTITY LINE REPORTS THE KEY, NOT ONLY THE ANSWER ----
        // rgroad1's script slot happens to equal its model rank, so the 20:25
        // log's first tick said "**script slot -- a guess**" and the correction
        // a tick later was silent -- leaving a reader to conclude the whole room
        // ran on a guess. Which key replied is the finding.
        {
            Disc3::Props::Reset();
            memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
            for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
            FF8Addresses::liveCount = 0;                  // nothing instantiated yet
            place(0, 50, 0); place(PROP, 50, 2000);
            setField("rgroad2", 842);
            g_logged.clear();
            tick();
            check(loggedContains("script slot -- a guess"), "the first tick guesses");
            // The model key arrives and points at the SAME entity the guess did.
            for (int i = 0; i < 3; i++) *(int16_t*)(entBlock(i) + 0x218) = (int16_t)i;
            FF8Addresses::liveCount = 3;
            g_logged.clear();
            tick();
            check(loggedContains("by model id"),
                  "**and the upgrade from guess to proof is logged even though the "
                  "answer did not move** -- an index that stays put is not evidence "
                  "that the guess was ever checked");
            FF8Addresses::liveCount = (uint8_t)32;
        }

        // ---- THE PASSENGER COMPARTMENT, GATED AT ITS FIRST INSTRUCTION ---
        // Aaron: "Let's try to freeze the one in the passenger compartment as
        // well. I know that one is a bit different but it is jarring the way it
        // works right now." It IS different: no ISTOUCHING anywhere in its
        // script, and its post-charge wait is an eight-FRAME count rather than a
        // wait for the move, so neither of the other two mechanisms reaches it.
        // The hold goes on the cutscene's own first instruction instead, using
        // the engine's idiom for "not yet": return 1 and the executor re-runs
        // the same instruction next frame (exec loop 0x005235A7 / 0x0052363C).
        Disc3::Props::Reset();
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        wr8(PG_VAR_DEAD, 0);
        place(0, 50, 0); place(PROP, 50, 3000);
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        *(uint16_t*)(entBlock(PROP) + 0x200) = 1500;
        *(int16_t*)(entBlock(PROP) + 0x218) = 2;      // rgguest2's alien01, model 2
        setField("rgguest2", 832);
        for (int i = 0; i < 10; i++) tick();
        check(speedOf(PROP, 0x1FE) == 1500,
              "**rgguest2's is still not FROZEN** -- its script waits eight frames "
              "after the charge rather than waiting for the move, so a pinned speed "
              "would stop the walk and the battle would arrive anyway");
        {
            // The hook chains through a table entry the probe cannot hold -- it
            // is uint32_t, because the game is 32-bit, and a host function
            // pointer does not survive the truncation. So the DECISION is the
            // testable part, and the deferral is tested through the hook because
            // that path returns without chaining at all.
            g_origEventCalls = 0;
            check(Disc3::Props::PgDeferGuestCutscene(entBlock(PROP)),
                  "**the passenger compartment's cutscene is deferred** -- alive, "
                  "in that field, and he has not said yes");
            const int r = Disc3::Props::PgHookedEvent(entBlock(PROP), 0);
            check(r == 1,
                  "**and the answer is \"not yet\"** -- 1 is the engine's own re-run "
                  "code, the same one MOVEWAIT returns; the script has not started, "
                  "it simply has not got there");
            check(g_origEventCalls == 0,
                  "**with the engine's handler not called at all** -- 0x04E's whole job "
                  "is to set the byte that takes his control away, and a deferral that "
                  "takes his control away first has deferred nothing worth having");
        }
        // Walk up, press Confirm: the scene plays exactly as it was written.
        place(PROP, 50, 300);
        tick();
        g_confirmDown = true; tick(); g_confirmDown = false;
        check(!Disc3::Props::PgDeferGuestCutscene(entBlock(PROP)),
              "**and after Confirm nothing is deferred any more** -- nothing was faked "
              "and nothing was skipped, so the fade, the charge, BATTLE 816 and the "
              "pair bookkeeping all happen in their own order");
        check(loggedContains("cutscene released"), "and the release is logged");

        // NOBODY ELSE'S EVENT IS EVER DEFERRED. 0x04E is a global opcode that
        // every scripted scene in the game opens with, so a deferral aimed one
        // entity wide is a room that never finishes loading.
        {
            Disc3::Props::Reset();
            wr8(PG_VAR_DEAD, 0);
            place(0, 50, 0); place(PROP, 50, 3000);
            *(int16_t*)(entBlock(PROP) + 0x218) = 2;
            setField("rgroad2", 842);                 // a different field entirely
            tick();
            check(!Disc3::Props::PgDeferGuestCutscene(entBlock(PROP)),
                  "**the same model id in another field is not deferred** -- the raw "
                  "field id is part of the test, and it is the RAW one because the "
                  "debounced name arrives seconds after this script has already run");
            Disc3::Props::Reset();
            setField("rgguest2", 832);
            tick();
            *(int16_t*)(entBlock(5) + 0x218) = 7;     // somebody else in the same room
            check(!Disc3::Props::PgDeferGuestCutscene(entBlock(5)),
                  "**and another entity in the same room is not deferred** -- `comp` "
                  "and `dic` open scenes in there too");
            check(Disc3::Props::PgDeferGuestCutscene(entBlock(PROP)),
                  "while the Propagator in that same room still is");
            // A dead one has nothing to consent to: its script skips the cutscene
            // branch entirely, and deferring it would be a room that never loads.
            Disc3::Props::Reset();
            wr8(PG_VAR_DEAD, 0x80);
            setField("rgguest2", 832);
            tick();
            check(!Disc3::Props::PgDeferGuestCutscene(entBlock(PROP)),
                  "**and a dead one is never deferred** -- var[446] bit 0x80 is the "
                  "script's own skip test, and holding a scene that was never going "
                  "to run is a room that never finishes loading");
            wr8(PG_VAR_DEAD, 0);
        }

        // The table has to keep agreeing with itself: freezing something the mod
        // has not gated would hold a monster it never intends to let go of.
        for (int i = 0; i < PG_COUNT; i++)
            check(!PG_LIST[i].freezable || PG_LIST[i].gate,
                  "**every frozen Propagator is also a gated one** -- a hold with no "
                  "way to end it is a puzzle that cannot be finished");

        // THE JOIN OUTRANKS THE TABLE. The model id is the primary key, but it
        // needs the live array populated; the catalog's join is the second
        // opinion and it still outranks the script slot, which is a guess.
        Disc3::Props::Reset();
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
        snprintf(g_joinSym[5], 32, "alien01");
        place(0, 50, 0); place(5, 50, 2000);
        *(uint16_t*)(entBlock(5) + 0x1FE) = 1500;
        *(uint16_t*)(entBlock(5) + 0x200) = 1500;
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;
        setField("rgroad2", 840);
        tick();
        check(speedOf(5, 0x1FE) == 0,
              "**the join's answer is the one that gets pinned**");
        check(speedOf(PROP, 0x1FE) == 1500,
              "and the table's guess is left alone when the join disagrees");
        check(loggedContains("live entity 5 by catalog join"),
              "**and the log names the key that answered** -- a silently wrong "
              "slot would pin some other entity's speed to zero for the whole scene");

        // An unplaced entity is an unmeasurable one, and unmeasurable means
        // hands off: a hold we cannot end is a puzzle that cannot be finished.
        Disc3::Props::Reset();
        memset(g_others, 0, (size_t)MAX_ENTITIES * ENTITY_STRIDE);
        for (int i = 0; i < MAX_ENTITIES; i++) g_joinSym[i][0] = '\0';
        place(0, 50, 0);
        *(uint16_t*)(entBlock(PROP) + 0x1FE) = 1500;    // speed but no position
        setField("rgroad2", 840);
        for (int i = 0; i < 10; i++) tick();
        check(speedOf(PROP, 0x1FE) == 0,
              "**an entity we cannot measure is held, not freed** -- there is no "
              "distance rule left for it to fall through, and a monster we cannot "
              "see the distance to is the last one that should be moving");

        setField("bgbtl_1", 100); tick();
        s_playerEntityIdx = -1;
        Disc3::Props::Reset();
        wr8(PG_VAR_DEAD, 0); wr8(PG_VAR_PENDBIT, 0); wr8(PG_VAR_PENDFLAG, 0);
    }

    // Off the Ragnarok: silent.
    setField("bgbtl_1", 100);
    clearSaid();
    for (int i = 0; i < 50; i++) tick();
    check(g_said.empty(), "silent off the Ragnarok");

    // =======================================================================
    // 5. THE THREE CANNOT COLLIDE. "/" is shared; only the module whose scene
    //    is live may answer it.
    // =======================================================================
    setField("rgair1", 823);
    wr8(PG_VAR_DEAD, 0); wr8(PG_VAR_PENDBIT, 0); wr8(PG_VAR_PENDFLAG, 0);
    wr8(EP_VAR_MISSION, 19);                   // Esthar's byte set but not in Esthar
    wrAddr32(SR_ADDR_X, 0); wrAddr32(SR_ADDR_Y, 0);
    clearSaid();
    tick();
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    {
        std::string s;
        for (auto& u : g_said) s += u.text + " | ";
        check(s.find("Contact point") == std::string::npos,
              "Esthar stays quiet aboard the Ragnarok even with its mission byte set");
        check(s.find("Rinoa") == std::string::npos, "space stays quiet aboard the Ragnarok");
    }

    // =======================================================================
    // 5b. DETECTION POLICY. The field ids for all three scenes are DERIVED, not
    //     observed, so each module's choice of id-vs-name is load-bearing and
    //     is tested here rather than trusted.
    // =======================================================================
    Disc3::Esthar::Reset(); Disc3::Props::Reset(); Disc3::Space::Reset();
    wr16(EP_VAR_MISSION, 19);
    wr8(PG_VAR_DEAD, 0); wr8(PG_VAR_PENDBIT, 0); wr8(PG_VAR_PENDFLAG, 0);
    wrAddr32(EP_TIMER_ADDR, 800);

    // An id that lands inside a derived range but a name that is NOT one of
    // the scene's fields: both the Esthar and Propagator modules must stay
    // silent. If the derivation is wrong this is the case that happens, and
    // the cost of getting it wrong is chatter in an unrelated scene forever.
    setField("dosea1", 830);              // inside the derived rg range
    clearSaid();
    for (int i = 0; i < 40; i++) tick();
    // ...and it must STAY silent while those variables move underneath it.
    // var[445..447] are ordinary field variables and belong to whatever scene
    // is running; a foreign field writing them is the normal case, not a freak
    // one. Without this half the test passes on an id-keyed build too, because
    // the announcements happen to be gated on PgForField() finding a match.
    for (uint8_t d = 1; d; d = (uint8_t)(d << 1)) {
        wr8(PG_VAR_DEAD, d);
        wr8(PG_VAR_PENDBIT, (uint8_t)(d << 1));
        wr8(PG_VAR_PENDFLAG, (uint8_t)((d & 1) ? PG_PENDING_MASK : 0));
        for (int i = 0; i < 4; i++) tick();
    }
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(g_said.empty(), "an rg-range id with a foreign name says nothing, "
                          "even while var[445..447] churn and slash is pressed");
    wr8(PG_VAR_DEAD, 0); wr8(PG_VAR_PENDBIT, 0); wr8(PG_VAR_PENDFLAG, 0);

    setField("dosea1", 430);              // an id in the old derived ec range
    clearSaid();
    for (int i = 0; i < 40; i++) tick();
    check(g_said.empty(), "an ec-range id with a foreign name says nothing");

    // The mirror case: the right NAME with an id nowhere near the derived
    // range. The feature must still work -- that is the whole point of keying
    // on the name -- and it must log that the derivation is wrong.
    Disc3::Props::Reset();
    g_logged.clear();
    setField("rgair1", 60000);
    clearSaid();
    tick();
    // v0.66.0: arrival is silent, so the proof that the name still drives the
    // feature is the help key answering -- which it can only do inside the
    // Ragnarok.
    g_keyDown = 1; tick(); g_keyDown = 0; tick();
    check(saidContains("yellow"), "the right name works even when the derived id is wrong");
    {
        bool warned = false;
        for (auto& l : g_logged) if (l.find("OUTSIDE the derived") != std::string::npos) warned = true;
        check(warned, "and a wrong derived id is logged so one BAT settles it");
    }

    // Space is the exception: it keys on id OR name, because the skip and the
    // steering cannot wait out pCurrentFieldName's 2-5 second lag. With a stale
    // name and the right id it must still run.
    Disc3::Space::Reset();
    setField("ssspace2", SR_FIELD_ID);     // name still lagging on the previous field
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    clearSaid();
    g_dialogOpen = false;
    for (int i = 0; i < 8000 / 16; i++) tick();   // past the settle: the brief opens
    check(saidContains("Reaching Rinoa"), "space runs on the id alone while the name lags");

    // ...and on the name alone if the id turns out to be derived wrong.
    Disc3::Space::Reset();
    setField("ssspace3", 60000);
    wrAddr32(SR_ADDR_X, 2500); wrAddr32(SR_ADDR_Y, -800);
    wrAddr32(SR_ADDR_FUEL, SR_FUEL_FULL);
    clearSaid();
    for (int i = 0; i < 8000 / 16; i++) tick();
    check(saidContains("Reaching Rinoa"), "space also runs on the name alone");

    setField("bgbtl_1", 100);
    Disc3::Esthar::Reset(); Disc3::Props::Reset(); Disc3::Space::Reset();
    wr8(EP_VAR_MISSION, 0);
    clearSaid();
    for (int i = 0; i < 20; i++) tick();
    check(g_said.empty(), "and everything is quiet again afterwards");

    // =======================================================================
    // 6. NOTHING EVER SPOKE A PLACEHOLDER.
    // =======================================================================
    recordAll();
    for (auto& t : g_allSaid)
        if (t.find('?') != std::string::npos) {
            std::printf("  BAD: an utterance contained '?': \"%s\"\n", t.c_str());
            bad++;
        }
    check(!g_allSaid.empty(), "the probe actually collected utterances");

    // ---- AND IT WILL NOT TAKE A WINDOW THE GAME IS USING ----------------
    // This started as one screen in one fight, where slot 7 had been observed
    // free. It is now the house style for every Game Controls screen, and the
    // next scene to use it is one nobody has checked. Opening a window the game
    // already owns replaces its text with ours and says nothing about it --
    // worse than not drawing, because the player loses something he was meant
    // to have.
    {
        GardenBattle::CloseBriefDialog();
        uint8_t* ctx = *(uint8_t**)0x00B8EE90u;
        check(ctx != nullptr, "the field context is mapped");
        ctx[0xD3] |= (uint8_t)(1u << 7);              // somebody else owns slot 7
        g_logged.clear();
        check(!GardenBattle::OpenBriefDialog("HELLO"),
              "**an occupied window is refused** -- the briefing is still spoken, "
              "and the log says why the box is missing");
        check(loggedContains("is already open in this scene"),
              "and it names the reason rather than failing silently");
        ctx[0xD3] &= (uint8_t)~(1u << 7);
        check(GardenBattle::OpenBriefDialog("HELLO"),
              "and it opens again once the slot is free");
        GardenBattle::CloseBriefDialog();
    }

    // ---- AND IT GETS OUT OF THE GAME'S WAY ------------------------------
    // Aaron's 15:05 shot of the Trabia dragon: the box opened in the game's own
    // window exactly as intended, and the scene's OWN legends -- "A to defend!"
    // and "W to attack!" -- sat on top of its left-hand third and hid the first
    // three lines. Slot 7 really was free; the collision was positional, and no
    // amount of checking ctx+0xD3 would have caught it. A fixed corner cannot
    // work either: the Garden battle wants the top because ITS legend is at the
    // bottom, and Trabia's is at the top.
    {
        typedef GardenBattle::WinRect R;
        int x = 0, y = 0, cost = 0, choice = 0;

        // The predicate itself, first. A placement search built on an overlap
        // test that never reports an overlap is a search that always answers
        // "the top is fine" -- which is precisely the answer the 15:05 shot
        // disproves, and it would look identical in every other assertion here.
        {
            const R a = { 0, 0, 10, 10 }, b = { 5, 5, 10, 10 };
            const R touching = { 10, 0, 10, 10 }, apart = { 40, 40, 10, 10 };
            check(GardenBattle::RectsOverlap(a, b), "overlapping rectangles overlap");
            check(!GardenBattle::RectsOverlap(a, touching),
                  "**and rectangles that merely touch do not** -- an off-by-one here "
                  "would push every box away from a window it was not covering");
            check(!GardenBattle::RectsOverlap(a, apart), "and distant ones do not");
            check(GardenBattle::OverlapArea(a, b) == 25, "the covered area is measured");
            check(GardenBattle::OverlapArea(a, apart) == 0, "and is zero when clear");
        }

        // Nothing else on screen: the box keeps the placement it has always had.
        GardenBattle::PlaceBriefBox(193, 133, nullptr, 0, &x, &y, &cost, &choice);
        check(choice == 0 && y == 8 && cost == 0,
              "**an empty screen still puts it centred at the top** -- this must "
              "not move the two scenes where it was already right");

        // Trabia's legends, in FF8's own 320x224 space: two stacked boxes down
        // the left, which is exactly what the shot shows.
        const R trabia[2] = { { 8, 14, 105, 25 }, { 8, 42, 105, 25 } };
        GardenBattle::PlaceBriefBox(193, 133, trabia, 2, &x, &y, &cost, &choice);
        check(cost == 0,
              "**and it finds somewhere clear when the scene has its own legends "
              "up** -- the 15:05 shot is what the alternative looks like");
        {
            const R box = { x, y, 193, 133 };
            for (int i = 0; i < 2; i++)
                check(!GardenBattle::RectsOverlap(box, trabia[i]),
                      "the chosen rectangle really does miss them");
        }

        // The Garden battle's own case: legend at the LOWER left, bars along the
        // bottom. The top must still win there.
        const R garden[2] = { { 8, 150, 120, 30 }, { 8, 190, 290, 20 } };
        GardenBattle::PlaceBriefBox(193, 133, garden, 2, &x, &y, &cost, &choice);
        check(cost == 0 && y == 8,
              "**and the Garden battle keeps the top**, which is why it is first "
              "in the list rather than merely one of five");

        // Every corner taken: it must still choose, and say it is covered.
        const R everywhere[5] = { { 0, 0, 304, 224 }, { 0, 0, 8, 8 },
                                  { 0, 0, 8, 8 }, { 0, 0, 8, 8 }, { 0, 0, 8, 8 } };
        GardenBattle::PlaceBriefBox(193, 133, everywhere, 5, &x, &y, &cost, &choice);
        check(cost > 0, "a screen with no clear space reports that it is covered");
        check(x >= 8 && y >= 8 && x + 193 < 0x130 && y + 133 < 0xE0,
              "**and still lands inside the screen** -- a partly covered box beats "
              "no box, a box off the edge does not");
    }

    std::printf("%s -- %d bad\n", bad ? "FAIL" : "OK", bad);
    return bad ? 1 : 0;
}
