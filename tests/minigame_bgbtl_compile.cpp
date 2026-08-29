// Compile probe for field_minigame_bgbtl.inl. No host harness compiles
// field_navigation.cpp, so this is the only pre-MSVC syntax check available.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
typedef unsigned long DWORD;
typedef unsigned char  BYTE;
typedef int            BOOL;
#define PAGE_READWRITE 4
#define PAGE_EXECUTE_READWRITE 0x40
#define VK_RETURN 0x0D
#define VK_F10 0x79
#define VK_F9 0x78
#define VK_SPACE 0x20
#define VK_TAB 0x09
#define VK_BACK 0x08
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_COMMA 0xBC
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_1 0xBA
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HOME 0x24
#define VK_END 0x23
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
// v0.20.121: confirm is ENTER now, not X -- X is mask 64, the kick, so the key
// that dismissed the Game Controls screen was one of the four it exists to
// teach. The release step matters too: .120 accepted a key that was already
// down when the briefing opened, and the retry briefings died in 93 ms.
static int g_fakeKey = 0;
static short GetAsyncKeyState(int vk) { return (short)((vk == g_fakeKey) ? 0x8000 : 0); }
// The module patches an absolute engine address and reads absolute variable
// addresses. Map real pages there so the probe exercises the REAL code path --
// signature check, byte patch, restore -- instead of a stubbed-out shadow.
#include <sys/mman.h>
static unsigned char g_fakeFieldCtx[0x200];
static bool MapEngineePages()
{
    void* code = mmap((void*)0x00471000, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    void* vars = mmap((void*)0x01CFE000, 0x2000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    // v0.20.119: the word the fight ACTUALLY reads -- opcode 109 at 0x0051DA50
    // loads [0x01CE48B0]. Mapped for real so the assist and the key learner run
    // against the same address the script does.
    void* btns = mmap((void*)0x01CE4000, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    // v0.20.123: the Game Controls box calls six real engine routines. Map an
    // EXECUTABLE page over them filled with `ret`, and give the text measurer a
    // `mov eax, imm32 ; ret` so OpenBriefDialog runs its real geometry maths on
    // a real measurement instead of being stubbed out at the C++ level.
    void* win = mmap((void*)0x0049F000, 0x3000, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    // v0.67.3: the WINDOW STATE ARRAY at 0x01D2B330 + winId*0x3C. The dialog
    // now reads every other window's rectangle out of it so the controls box can
    // avoid whatever the scene already has on screen -- so the probe has to have
    // one to read. Zeroed means "nothing else is open", which is the Garden
    // battle's own case.
    void* wins = mmap((void*)0x01D2B000, 0x2000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (wins != (void*)0x01D2B000) { std::printf("FATAL: window state mmap\n"); std::exit(2); }
    std::memset(wins, 0, 0x2000);

    void* gauge = mmap((void*)0x01D9C000, 0x2000, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    void* ctxp = mmap((void*)0x00B8E000, 0x2000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (code == MAP_FAILED || vars == MAP_FAILED || btns == MAP_FAILED ||
        win == MAP_FAILED || ctxp == MAP_FAILED || gauge == MAP_FAILED) return false;
    memset(win, 0xC3, 0x3000);                        // every stub is `ret`
    // measure_text -> 240 wide, 96 high  (0x0060_00F0)
    {
        unsigned char* m = (unsigned char*)0x004A0EC0;
        m[0] = 0xB8; *(unsigned*)(m + 1) = 0x006000F0u; m[5] = 0xC3;
    }
    *(void**)0x00B8EE90 = g_fakeFieldCtx;
    static const unsigned char sig[8] = { 0xA1, 0x64, 0x4A, 0xCE, 0x01, 0x53, 0x55, 0xBD };
    // Give both fighters 600/600 so PollHealth has something sane to read.
    *(short*)(0x01CFE9B8 + 350) = 600;   // Squall max
    *(short*)(0x01CFE9B8 + 354) = 600;   // Squall current
    *(short*)(0x01CFE9B8 + 352) = 600;   // Foe max
    *(short*)(0x01CFE9B8 + 356) = 600;   // Foe current
    return true;
}
#define g_fieldButtons (*(volatile uint32_t*)0x01CE48B0)
// Every standard header this probe needs must be included BEFORE the SEH
// macros below: libstdc++ writes `__try { } __catch(...) { }` in its own
// sources, and `#define __try if(1)` turns the `catch` that follows into a
// syntax error. <cmath> in particular pulls in headers that do exactly that.
#include <cstring>
#include <cmath>
#define EXCEPTION_EXECUTE_HANDLER 1
#define __try if(1)
#define __except(x) else
#define __cdecl
static DWORD g_tick = 0;
static DWORD GetTickCount() { return g_tick; }
static DWORD GetLastError() { return 0; }
static BOOL VirtualProtect(void*, size_t, DWORD, DWORD* old) { if (old) *old = 0; return 1; }
#define SND_MEMORY 4
#define SND_ASYNC 1
#define SND_NODEFAULT 2
typedef const char* LPCSTR;
static const void* g_lastSound = nullptr;
static int PlaySoundA(LPCSTR p, void*, DWORD) { g_lastSound = p; return 1; }
typedef void* HANDLE;
typedef void* LPVOID;
#define WINAPI
static HANDLE CreateThread(void*, size_t, DWORD (WINAPI *fn)(LPVOID), void*, DWORD, DWORD*) { (void)fn; return (HANDLE)1; }
static int CloseHandle(HANDLE) { return 1; }
typedef long LONG;
static void Sleep(DWORD) {}
static LONG InterlockedCompareExchange(volatile LONG* p, LONG v, LONG c)
{ LONG o = *p; if (o == c) *p = v; return o; }
static LONG InterlockedExchange(volatile LONG* p, LONG v) { LONG o = *p; *p = v; return o; }
namespace Log { void Field(const char*, ...) {} }
static int g_narrationSuppressed = -1;
namespace FmvAudioDesc { void SetSuppressed(bool on) { g_narrationSuppressed = on ? 1 : 0; } }
static std::string g_fakeAvi;
static int g_fmvSkipCalls = 0;
namespace FmvSkip { std::string GetCurrentAviName() { return g_fakeAvi; }
                    bool RequestSkip() { g_fmvSkipCalls++; g_fakeAvi.clear(); return true; }
                    bool IsMoviePlaying() { return !g_fakeAvi.empty(); } }
static int  g_speakCount = 0;
static char g_lastSpoken[256] = {0};
namespace ScreenReader {
    bool Speak(const char* t, bool = false) {
        g_speakCount++;
        if (t) { size_t i = 0; for (; t[i] && i + 1 < sizeof(g_lastSpoken); i++) g_lastSpoken[i] = t[i]; g_lastSpoken[i] = 0; }
        return true;
    }
    bool IsSpeaking() { return false; }
}
namespace FF8Addresses { static uint32_t table[512]; static uint32_t* pExecuteOpcodeTable = table;
                          static uint32_t btn = 0; static uint32_t* pEngineInputConfirmedButtons = &btn;
                          static uint32_t btn2 = 0; static uint32_t* pEngineInputValidButtons = &btn2;
                          static uint16_t curField = 144; static uint16_t* pCurrentFieldId = &curField; }
namespace FieldNavigation {
#include "field_minigame_bgbtl.inl"
}
int main()
{
    // Every entry point is compile-checked; the ones that touch absolute
    // engine addresses are not executed here.
    (void)&FieldNavigation::GardenBattle::OnFieldLoaded;
    (void)&FieldNavigation::GardenBattle::Update;
    (void)&FieldNavigation::GardenBattle::ToggleCueMode;
    (void)&FieldNavigation::GardenBattle::HookedReq;
    printf("game over screen? %d\n", (int)FieldNavigation::GardenBattle::OnGameOverScreen(95));
    // v0.20.104: the legend trigger is the real arm signal, so it gets exercised
    // rather than merely compiled -- three strstr calls and a filter decision.
    (void)&FieldNavigation::GardenBattleOnDialogText;
    printf("legend filter: legend=%d, ordinary line=%d\n",
           (int)(strstr("W  Punch\nA  Block\nX  Kick", "Punch") != nullptr),
           (int)(strstr("Squall \"Hey kid, you all right?\"", "Punch") != nullptr));
    // SkipToVictory writes absolute engine addresses -- compile-checked only,
    // never executed in the probe.
    (void)&FieldNavigation::GardenBattle::SkipToVictory;
    // v0.20.105: replay the REAL HP sequences the v0.20.103 trace recorded and
    // check the announcement policy against them. Max is 600 for both (from
    // bg2f_31's initialiser); these are the actual observed current values.
    {
        using FieldNavigation::GardenBattle::HpPercent;
        const int foe[]    = { 400, 345, 302, 273, 223, 172, 150, 66, 31, -18 };
        const int squall[] = { 251, 196, 116, 33 };
        int bad = 0, prev = 101;
        printf("foe   :");
        for (int i = 0; i < 10; i++) {
            int p = HpPercent(foe[i], 600);
            printf(" %d", p);
            if (p >= prev) { bad++; printf("  BAD: check 1\n"); }            // must be strictly falling: every
            prev = p;                        // recorded change must be audible
        }
        printf("\n");
        prev = 101;
        printf("squall:");
        for (int i = 0; i < 4; i++) {
            int p = HpPercent(squall[i], 600);
            printf(" %d", p);
            if (p >= prev) { bad++; printf("  BAD: check 2\n"); }
            prev = p;
        }
        printf("\n");
        // Negative HP must clamp, not wrap -- the read is signed on purpose.
        if (HpPercent(-18, 600) != 0) { bad++; printf("  BAD: check 3\n"); }
        if (HpPercent(600, 600) != 100) { bad++; printf("  BAD: check 4\n"); }
        if (HpPercent(0, 0) != -1) { bad++; printf("  BAD: check 5\n"); }    // unreadable max -> no announcement
        // v0.20.110: the v0.20.109 BAT announced "Foe 0" at 2/600 and then the
        // foe kept fighting. A live fighter must never round down to zero.
        if (HpPercent(2, 600) <= 0) { bad++; printf("  BAD: check 6\n"); }
        if (HpPercent(1, 800) <= 0) { bad++; printf("  BAD: check 7\n"); }
        if (HpPercent(-1, 600) != 0) { bad++; printf("  BAD: check 8\n"); }  // ...and a dead one must read zero
        printf("zero-floor: 2/600=%d  1/800=%d  -1/600=%d\n",
               HpPercent(2, 600), HpPercent(1, 800), HpPercent(-1, 600));
        printf("HP policy checks: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        if (bad) return 1;
    }
    // v0.20.107: a movie that is not the Garden battle must not arm the scene.
    {
        namespace GB = FieldNavigation::GardenBattle;
        g_fakeAvi = "disc01_99z.avi";
        GB::FreezeWatchdog();
        printf("unrelated FMV arms: %d (want 0)\n", (int)GB::s_briefing);
        if (GB::s_briefing) return 1;
    }
    // v0.20.106: the freeze must be released on EVERY exit path. A mod that
    // leaves field_main patched has not inconvenienced the player, it has
    // ended their session -- so this is checked, not reasoned about.
    {
        namespace GB = FieldNavigation::GardenBattle;
        int bad = 0;
        const bool mapped = MapEngineePages();
        printf("engine pages mapped: %d\n", (int)mapped);
        if (!mapped) { printf("cannot map -- skipping freeze test\n"); return 0; }
        // v0.20.107: the FMV arms the scene BEFORE the legend appears.
        g_fakeAvi = "disc01_32h.avi";
        GB::FreezeWatchdog();
        if (!GB::s_briefing) { bad++; printf("  BAD: check 9\n"); }              // FMV must open the briefing
        // Confirm and resume.
        FieldNavigation::GardenBattleOnDialogText("W  Punch\nA  Block\nX  Kick");
        GB::Update();                            // sees the key up, arms confirm
        g_fakeKey = VK_RETURN; GB::Update();      // press
        g_fakeKey = 0;         GB::Update();      // release -> resume
        if (GB::s_briefing) { bad++; printf("  BAD: check 11\n"); }               // must have ended
        // v0.20.107: the legend re-fires at every phase change and must NOT
        // re-open the briefing mid-fight.
        FieldNavigation::GardenBattleOnDialogText("W  Punch\nA  Block\nX  Kick");
        if (GB::s_briefing) { bad++; printf("  BAD: check 13\n"); }               // same attempt -> no re-brief
        // ...but a pass through the Game Over screen IS a fresh attempt -- and
        // v0.20.122: "Try again" drops the player back in the HALLWAY first, so
        // the module must LET GO there and wait for the battle movie again,
        // exactly as it did the first time. Aaron walked a hallway with the REQ
        // hook live and the assist pinning a guard flag in the .121 build.
        GB::OnFieldLoaded(95);
        GB::OnFieldLoaded(144);                  // the hallway
        if (GB::IsArmed()) { bad++; printf("  BAD: still armed in the hallway\n"); }
        g_fakeAvi = "";  GB::FreezeWatchdog();   // the movie name clears
        g_fakeAvi = "disc01_32h.avi";            // ...and the fight starts again
        GB::FreezeWatchdog();
        if (!GB::IsArmed()) { bad++; printf("  BAD: the movie did not re-arm\n"); }
        if (!GB::s_briefing) { bad++; printf("  BAD: check 14\n"); }              // retry -> briefing again
        // v0.20.119/.120: the guard flag is SQUALL'S and it is a DIFFERENT byte
        // in each field -- 1030 in bg2f_31, 1028 in bgbtl_1. Every build up to
        // .118 watched 1031, which is the SOLDIER'S, and told Aaron he had
        // blocked nothing for four BATs. The assist is the only thing that
        // writes the flag now, so THAT is what has to honour the per-field byte.
        {
            volatile unsigned char* sq144 = (unsigned char*)(0x01CFE9B8 + 1030);
            volatile unsigned char* sq152 = (unsigned char*)(0x01CFE9B8 + 1028);
            volatile unsigned char* foe   = (unsigned char*)(0x01CFE9B8 + 1031);
            if (GB::GuardVarFor(144) != 1030) { bad++; printf("  BAD: host guard var\n"); }
            if (GB::GuardVarFor(152) != 1028) { bad++; printf("  BAD: minigame guard var\n"); }
            *sq144 = 0; *sq152 = 0; *foe = 0;
            GB::s_assist = true; GB::s_blockSeen = false;
            g_fieldButtons = GB::BTN_BLOCK;
            GB::ApplyBlockAssist(144);
            if (*sq144 != 1) { bad++; printf("  BAD: assist missed 1030 in field 144\n"); }
            if (*foe   != 0) { bad++; printf("  BAD: assist wrote the SOLDIER's flag\n"); }
            if (*sq152 != 0) { bad++; printf("  BAD: assist wrote 1028 in field 144\n"); }
            *sq144 = 0;
            GB::ApplyBlockAssist(152);
            if (*sq152 != 1) { bad++; printf("  BAD: assist missed 1028 in field 152\n"); }
            if (*sq144 != 0) { bad++; printf("  BAD: assist wrote 1030 in field 152\n"); }
            if (*foe   != 0) { bad++; printf("  BAD: assist wrote the SOLDIER's flag\n"); }
            g_fieldButtons = 0; *sq144 = 0; *sq152 = 0;
            GB::ApplyBlockAssist(152);
            printf("guard flag: per-field byte honoured, soldier flag never written\n");
        }
        // v0.20.120: THE BLOCK COUNT COMES FROM VAR 340. The 2026-08-15 run
        // reported "2 BLOCKED" while var 340 walked 0 -> 6, and both were
        // honest -- with the assist pinning the flag there are only two rising
        // edges. One increment of var 340 is exactly one blocked hit.
        {
            volatile unsigned char* streak = (unsigned char*)(0x01CFE9B8 + 340);
            GB::s_streak = -1; GB::s_guards = 0; GB::s_bestStreak = 0;
            GB::s_heavyAnnounced = false;
            *streak = 0; GB::WatchStreak();
            for (int i = 1; i <= 6; i++) { *streak = (unsigned char)i; GB::WatchStreak(); }
            if (GB::s_guards != 6) {
                bad++; printf("  BAD: %d blocks counted, want 6\n", GB::s_guards);
            }
            if (GB::s_bestStreak != 6) { bad++; printf("  BAD: best streak not kept\n"); }
            *streak = 0; GB::WatchStreak();          // a hit gets through
            if (GB::s_guards != 6) { bad++; printf("  BAD: reset counted as a block\n"); }
            if (GB::s_bestStreak != 6) { bad++; printf("  BAD: best streak lost\n"); }
            printf("block count: %d blocks from var 340, best streak %d\n",
                   GB::s_guards, GB::s_bestStreak);
        }
        // v0.20.122: THE HEAVY-PUNCH GATE MUST SURVIVE THE HIT THAT OPENS IT.
        // In the 14:10 log the streak reached 3, Aaron released block to press
        // D, took an unguarded hit in that gap, `var340 3 -> 0` shut the
        // keyscan's `var340 >= 3` test, and the six seconds he then spent
        // holding D produced nothing. The gate is now held open until the punch
        // actually fires -- at 4, not 3, so the game's own hint (REQ'd on
        // `var340 == 3`) does not re-fire every tick.
        {
            volatile unsigned char* streak = (unsigned char*)(0x01CFE9B8 + 340);
            GB::s_heavyArmed = true;
            *streak = 0;
            GB::HoldHeavyArmed(152, 49);            // some other label
            if (*streak < 3) { bad++; printf("  BAD: heavy gate not held open\n"); }
            if (*streak == 3) { bad++; printf("  BAD: held at 3 -- re-fires the hint\n"); }
            // ...and it lets go the moment squ_punching1 runs.
            GB::HoldHeavyArmed(152, 30);            // bgbtl_1 squ_punching1
            if (GB::s_heavyArmed) { bad++; printf("  BAD: gate not released on the punch\n"); }
            *streak = 0;
            GB::HoldHeavyArmed(152, 49);
            if (*streak != 0) { bad++; printf("  BAD: gate still held after the punch\n"); }
            // the host field uses a different label for the same script
            GB::s_heavyArmed = true;
            GB::HoldHeavyArmed(144, 14);            // bg2f_31 squ_punching1
            if (GB::s_heavyArmed) { bad++; printf("  BAD: host heavy label wrong\n"); }
            printf("heavy gate: held open past a reset, released on the punch, "
                   "per-field label honoured\n");
        }
        // v0.20.122: while the skip is active the RING must carry the label the
        // engine really ran, not the one the script asked for -- otherwise every
        // cancelled attack still rings the block tone. 171 of them in the 14:11
        // run, and faster than a real fight because `push` returns instantly.
        {
            if (GB::IsSoldierAttack(152, GB::SkipVetoLabel(152, 49))) {
                bad++; printf("  BAD: a vetoed attack still reads as an attack\n");
            }
            if (GB::IsSoldierAttack(144, GB::SkipVetoLabel(144, 56))) {
                bad++; printf("  BAD: a vetoed host attack still reads as an attack\n");
            }
            printf("skip veto: cancelled attacks no longer cue\n");
        }
        // v0.20.130: **THE BRIEFING HAS TO VETO BOTH FIGHTERS.** The field is
        // not frozen behind the box, so squ_punchkeyscan0 is live and a tap on
        // punch or kick during the key-naming step reached gal_hpcalc0. Aaron's
        // 34.5 s briefing left the soldier on 431/600 before "Game start."
        {
            const struct { unsigned short f; short lbl; short want; const char* what; } CASES[] = {
                { 152, 29, 25, "squall::squ_punching0 (152)" },
                { 152, 30, 25, "squall::squ_punching1 (152)" },
                { 152, 31, 25, "squall::squ_kicking0  (152)" },
                { 144, 13,  3, "squall::squ_punching0 (144)" },
                { 144, 14,  3, "squall::squ_punching1 (144)" },
                { 144, 15,  3, "squall::squ_kicking0  (144)" },
                { 152, 49, 45, "the soldier is still vetoed too" },
                { 144, 55, 48, "...in the host field as well" },
                { 152, 32, 32, "squall::squ_guarding0 SURVIVES" },
                { 144, 16, 16, "...in the host field too" },
                { 152, 28, 28, "the keyscan itself is untouched" },
            };
            for (unsigned i = 0; i < sizeof(CASES)/sizeof(CASES[0]); i++) {
                const short got = GB::BriefingVetoLabel(CASES[i].f, CASES[i].lbl);
                if (got != CASES[i].want) {
                    bad++;
                    printf("  BAD: briefing veto %s -> %d, want %d\n",
                           CASES[i].what, (int)got, (int)CASES[i].want);
                }
            }
            // and outside the briefing the player keeps his fists
            if (GB::SkipVetoLabel(152, 29) != 29) {
                bad++; printf("  BAD: the skip veto swallowed Squall's own punch\n");
            }
            printf("briefing veto: both fighters held, the guard and the keyscan "
                   "left alone\n");
        }
        // v0.20.130: **ONE NAME PER PRESS, NOT ONE PER RISING EDGE.** A held key
        // auto-repeats, and Aaron's 17:57 briefing said "Punch, W." fifty-three
        // times -- eleven inside two seconds -- each interrupting the last.
        {
            const bool wasBrief = GB::s_briefing;
            FF8Addresses::curField = 152;
            GB::s_briefing = true; GB::s_needKeyUp = false; GB::s_awaitRelease = false;
            GB::s_briefStart = 0;
            for (int i = 0; i < 4; i++) GB::s_lastLearnSpoke[i] = 0;
            GB::s_btnPrev = 0;
            g_fakeKey = 'W'; g_tick = 100000;

            int spoke = 0;
            for (int rep = 0; rep < 6; rep++) {        // six auto-repeats, 100 ms apart
                g_fieldButtons = 0;             GB::Update();
                g_fieldButtons = GB::BTN_PUNCH;
                const int before = g_speakCount;
                GB::Update();
                if (g_speakCount > before && strstr(g_lastSpoken, "Punch")) spoke++;
                g_tick += 100;
            }
            if (spoke != 1) {
                bad++; printf("  BAD: a held key announced %d times in 600 ms\n", spoke);
            }
            // ...but a deliberate re-tap later still confirms the binding
            g_tick += 1500;
            g_fieldButtons = 0;             GB::Update();
            g_fieldButtons = GB::BTN_PUNCH;
            const int before = g_speakCount;
            GB::Update();
            if (g_speakCount == before) {
                bad++; printf("  BAD: a re-tap after the debounce said nothing\n");
            }
            g_fieldButtons = 0; g_fakeKey = 0;
            GB::s_briefing = wasBrief;
            printf("key learner: one name per press, silent through auto-repeat, "
                   "speaks again on a real re-tap\n");
        }
        // v0.20.123: without the field_main pause the round's clock keeps
        // running behind the briefing, so an unread box must hand the fight
        // back before the resolution at 580 rather than lose to a reading
        // speed.
        {
            FF8Addresses::curField = 152;
            GB::s_briefing = true; GB::s_briefStart = 0;
            *(unsigned*)(0x01CFE9B8 + 80) = 100;
            GB::Update();
            if (!GB::s_briefing) { bad++; printf("  BAD: briefing cut short early\n"); }
            *(unsigned*)(0x01CFE9B8 + 80) = 500;   // past CLOCK_BRIEF_LIMIT
            GB::Update();
            if (GB::s_briefing) { bad++; printf("  BAD: briefing outlived the round\n"); }
            printf("briefing clock guard: holds at 100, hands back at 500\n");

            FF8Addresses::curField = 144;
            *(unsigned*)(0x01CFE9B8 + 80) = 0;
            GB::s_briefing = true;              // put it back for the checks below
            GB::s_needKeyUp = false;
        }
        // v0.20.126: **THE BRIEFING MUST NOT TOUCH HP AT ALL.** The old hold
        // reverted `squ_out0`'s own `WRVARW 356 = 600` back to whatever the
        // previous attempt had left, and a retry then started against a soldier
        // on 248 of 600 -- one heavy punch won it. The briefing's protection is
        // the REQ veto; the scene's own writes must survive untouched.
        {
            volatile short* sCur = (short*)(0x01CFE9B8 + 354);
            volatile short* fCur = (short*)(0x01CFE9B8 + 356);
            GB::s_briefing = true;
            *sCur = 604; *fCur = 248;              // left over from a lost attempt
            GB::Update();
            *fCur = 600;                           // squ_out0 sets up the retry
            *sCur = 800;
            for (int i = 0; i < 5; i++) GB::Update();
            if (*fCur != 600) {
                bad++; printf("  BAD: the briefing reverted the foe to %d\n", (int)*fCur);
            }
            if (*sCur != 800) {
                bad++; printf("  BAD: the briefing reverted Squall to %d\n", (int)*sCur);
            }
            printf("briefing: the scene's own HP setup survives (foe %d, Squall %d)\n",
                   (int)*fCur, (int)*sCur);
            // ...and the protection that replaced it is the veto, which must
            // be live for the whole briefing.
            if (GB::SkipVetoLabel(152, 49) == 49) {
                bad++; printf("  BAD: attacks not vetoed during the briefing\n");
            }
        }
        // v0.20.123: THE GAME CONTROLS BOX. Sizing is the game's own -- the
        // measurer at 0x004A0EC0 walks the string with the real font metrics --
        // so what is asserted here is the encoding and the geometry rules that
        // decide whether all of that text ends up on screen.
        {
            uint8_t buf[256];
            const size_t n = GB::EncodeWrapped("AB c1. HELLO world!", 34, buf, sizeof(buf));
            // A->0x45, B->0x46, space 0x20, c->0x61, 1->0x22, '.'->0x3B
            if (buf[0] != 0x45 || buf[1] != 0x46 || buf[2] != 0x20 ||
                buf[3] != 0x61 || buf[4] != 0x22 || buf[5] != 0x3B) {
                bad++; printf("  BAD: FF8 encoding wrong\n");
            }
            if (buf[n] != 0x00) { bad++; printf("  BAD: text not terminated\n"); }
            // Wrapping must break lines with 0x02 and never exceed the column.
            uint8_t w[512];
            GB::EncodeWrapped("aaaa bbbb cccc dddd eeee ffff gggg hhhh", 10, w, sizeof(w));
            int col = 0, worst = 0, breaks = 0;
            for (int i = 0; w[i]; i++) {
                if (w[i] == 0x02) { breaks++; if (col > worst) worst = col; col = 0; }
                else col++;
            }
            if (col > worst) worst = col;
            if (breaks == 0) { bad++; printf("  BAD: no wrapping at all\n"); }
            if (worst > 10)  { bad++; printf("  BAD: line of %d exceeds 10 cols\n", worst); }
            // A newline in the source must survive as one.
            uint8_t nl[64];
            GB::EncodeWrapped("a\nb", 34, nl, sizeof(nl));
            if (nl[1] != 0x02) { bad++; printf("  BAD: newline not encoded\n"); }

            // ...and the box itself: opened, measured, clamped on screen.
            GB::s_dlgOpen = false;
            const bool shown = GB::OpenBriefDialog("Garden battle. Hold block.");
            if (!shown || !GB::s_dlgOpen) { bad++; printf("  BAD: box did not open\n"); }
            if (!(g_fakeFieldCtx[0xD3] & (1 << GB::BRIEF_WINDOW))) {
                bad++; printf("  BAD: window open bit not set\n");
            }
            GB::CloseBriefDialog();
            if (GB::s_dlgOpen) { bad++; printf("  BAD: box did not close\n"); }
            if (g_fakeFieldCtx[0xD3] & (1 << GB::BRIEF_WINDOW)) {
                bad++; printf("  BAD: window open bit not cleared\n");
            }
            printf("controls box: encoded %u bytes, wrapped to %d cols, "
                   "opened and closed cleanly\n", (unsigned)n, worst);
        }
        // v0.20.119: the key learner. The four masks are pad bits and the
        // keyboard mapping is not in FF8_EN.exe, so the mod correlates a rising
        // bit with whatever single key is down at that instant -- and refuses
        // to name anything when two keys are down, because a wrong key name is
        // worse than none.
        {
            for (int i = 0; i < 4; i++) { GB::s_learned[i].hits = 0;
                                          GB::s_learned[i].locked = false; }
            GB::SlotFor(GB::BTN_BLOCK)->vk = 0;      // unseed just this one
            GB::s_btnPrev = 0;
            g_fakeKey = 'A'; g_fieldButtons = GB::BTN_BLOCK; GB::LearnButtons();
            g_fieldButtons = 0;                              GB::LearnButtons();
            g_fieldButtons = GB::BTN_BLOCK;                  GB::LearnButtons();
            const char* blockKey = GB::NameForMask(GB::BTN_BLOCK);
            if (!blockKey || strcmp(blockKey, "A") != 0) {
                bad++; printf("  BAD: block key not learned\n");
            }
            if (!GB::SlotFor(GB::BTN_BLOCK)->locked) {
                bad++; printf("  BAD: block key not locked after two presses\n");
            }
            // v0.20.123: the table ships SEEDED with FF8 PC's stock bindings,
            // so the heavy punch can always be named -- but the seed must be
            // overwritable by one real press, not locked.
            const char* hv = GB::NameForMask(GB::BTN_HEAVY);
            if (!hv || strcmp(hv, "D") != 0) {
                bad++; printf("  BAD: heavy key seed missing (%s)\n", hv ? hv : "null");
            }
            if (GB::SlotFor(GB::BTN_HEAVY)->locked) {
                bad++; printf("  BAD: seed shipped locked -- cannot be corrected\n");
            }
            // Four names in one call must not all read as the last one -- the
            // summary line at disarm prints exactly that.
            GB::s_learned[0].vk = 'W'; GB::s_learned[1].vk = 'X';
            const char* n1 = GB::NameForMask(GB::BTN_PUNCH);
            const char* n2 = GB::NameForMask(GB::BTN_KICK);
            const char* n3 = GB::NameForMask(GB::BTN_BLOCK);
            if (strcmp(n1, "W") || strcmp(n2, "X") || strcmp(n3, "A")) {
                bad++; printf("  BAD: key names alias (%s/%s/%s)\n", n1, n2, n3);
            }
            printf("key learner: block='%s' locked; three names in one call: %s %s %s\n",
                   blockKey, n1, n2, n3);
            g_fakeKey = 0; g_fieldButtons = 0; GB::LearnButtons();
        }
        // v0.20.119: the heavy punch. var340 reaching 3 is the only route to a
        // win, so the announcement fires exactly once per streak and re-arms
        // when the streak is broken.
        {
            volatile unsigned char* streak = (unsigned char*)(0x01CFE9B8 + 340);
            const bool wasBriefing = GB::s_briefing;
            GB::s_briefing = false;          // cues are gated while briefing
            GB::s_streak = -1; GB::s_heavyAnnounced = false;
            GB::s_lastHeavyCue = 0; GB::s_priorityUntil = 0;
            *streak = 0; GB::WatchStreak();
            *streak = 1; GB::WatchStreak();
            *streak = 2; GB::WatchStreak();
            if (GB::s_heavyAnnounced) { bad++; printf("  BAD: heavy announced at 2\n"); }
            *streak = 3; GB::WatchStreak();
            if (!GB::s_heavyAnnounced) { bad++; printf("  BAD: heavy not announced at 3\n"); }
            *streak = 4; GB::WatchStreak();        // still armed, no second word
            *streak = 0; GB::WatchStreak();        // a hit gets through
            if (GB::s_heavyAnnounced) { bad++; printf("  BAD: heavy not re-armed\n"); }
            printf("heavy-punch streak: announced once at 3, re-armed at 0\n");
            GB::s_briefing = wasBriefing;
        }
        // v0.20.119: the block assist. While the block bit is held the guard
        // flag must be pinned for the CURRENT field, and released when it is
        // not -- and F9 must be able to turn the whole thing off.
        {
            volatile unsigned char* sq152 = (unsigned char*)(0x01CFE9B8 + 1028);
            g_fieldButtons = 0;   *sq152 = 0;
            GB::s_assist = true;  GB::s_assistPinned = false;
            GB::s_blockSeen = false; GB::s_blockHeldAt = 0;
            GB::ApplyBlockAssist(152);
            if (*sq152 != 0) { bad++; printf("  BAD: assist pinned with nothing held\n"); }
            g_fieldButtons = GB::BTN_BLOCK;
            GB::ApplyBlockAssist(152);
            if (*sq152 != 1) { bad++; printf("  BAD: assist did not pin the guard\n"); }
            *sq152 = 0; GB::s_assist = false;
            GB::ApplyBlockAssist(152);
            if (*sq152 != 0) { bad++; printf("  BAD: assist pinned while switched off\n"); }
            // ...and the grace window keeps the guard up for a moment after
            // the key comes up, because the scene REQUIRES letting go of block
            // to throw the heavy punch and the 14:10 log shows the very first
            // thing in that gap was an unguarded hit.
            GB::s_assist = true; g_fieldButtons = 0; *sq152 = 0;
            GB::ApplyBlockAssist(152);
            if (*sq152 != 1) { bad++; printf("  BAD: grace window did not hold the guard\n"); }
            g_tick = 5000;                               // well past the window
            *sq152 = 0; GB::ApplyBlockAssist(152);
            if (*sq152 != 0) { bad++; printf("  BAD: grace window never expires\n"); }
            g_tick = 0; GB::s_blockSeen = false;
            printf("block assist: pins while held plus a grace window, off when disabled\n");
        }
        // v0.20.108: THERE IS NO TIME CAP. However long the player takes, the
        // briefing must still be up and the field must still be frozen.
        GB::s_briefStart -= 600000;              // ten minutes of thinking
        GB::s_lastRemind -= 600000;
        GB::Update();
        if (!GB::s_briefing) { bad++; printf("  BAD: briefing timed out\n"); }
        // ...and a confirm that was ALREADY DOWN when the briefing opened must
        // be ignored until it has been released once.
        g_fakeKey = VK_RETURN;
        GB::s_needKeyUp = true;
        GB::Update(); GB::Update();
        if (!GB::s_briefing) { bad++; printf("  BAD: held confirm dismissed the briefing\n"); }
        g_fakeKey = 0;         GB::Update();      // release: now it counts
        g_fakeKey = VK_RETURN; GB::Update();
        g_fakeKey = 0;         GB::Update();
        if (GB::s_briefing) { bad++; printf("  BAD: fresh confirm did not end it\n"); }
        // v0.20.107: the AVI name outlives playback, so once the fight is over
        // the same name must NOT freeze the game all over again.
        GB::OnFieldLoaded(675);                  // ends this attempt
        g_fakeAvi = "disc01_32h.avi";            // name still reported
        GB::FreezeWatchdog();
        if (GB::s_briefing) { bad++; printf("  BAD: latched AVI re-armed the briefing\n"); }               // latched -- must not re-arm
        // v0.20.110: the WIN is called from HP, not from the field transition
        // sixty seconds later -- and it must not fire on a value carried over
        // from the previous attempt.
        {
            volatile short* sMax = (short*)(0x01CFE9B8 + 350);
            volatile short* sCur = (short*)(0x01CFE9B8 + 354);
            volatile short* fMax = (short*)(0x01CFE9B8 + 352);
            volatile short* fCur = (short*)(0x01CFE9B8 + 356);
            *sMax = 600; *fMax = 600;

            // A fresh attempt whose foe HP is STALE at -26 must stay quiet:
            // nothing has been seen alive yet.
            GB::s_foeDown = false; GB::s_sawFoeAlive = false;
            GB::s_playerDown = false; GB::s_sawPlayerAlive = false;
            *sCur = 600; *fCur = -26;
            GB::PollHealth(nullptr);
            if (GB::s_foeDown) { bad++; printf("  BAD: stale foe HP announced as a win\n"); }            // stale carry-over, not a win

            // Now the script re-initialises the foe and the fight runs down.
            *fCur = 600; GB::PollHealth(nullptr);
            *fCur = 2;   GB::PollHealth(nullptr);
            if (GB::s_foeDown) { bad++; printf("  BAD: check 20\n"); }            // 2 HP is not defeated
            *fCur = 0;   GB::PollHealth(nullptr);
            if (!GB::s_foeDown) { bad++; printf("  BAD: check 21\n"); }           // zero IS
            printf("outcome-from-HP: stale ignored, 2 alive, 0 wins -> %s\n",
                   bad ? "FAILED" : "OK");
        }
        // v0.20.111: labels are PER FIELD. Cueing bgbtl_1's numbers while the
        // fight runs in the host field is what left Aaron with no warning for
        // six attacks in a row, so the two tables are checked against each
        // other rather than assumed distinct.
        {
            using GB2 = FieldNavigation::GardenBattle::AttackSet;
            (void)sizeof(GB2);
            const bool host_ok = GB::IsSoldierAttack(144, 55) &&
                                 GB::IsSoldierAttack(144, 56);
            // 54 is g_hei0::g0_fall0, the driver; 57 is g0_guarding0, the
            // soldier reacting to a punch. Cueing either is a false alarm.
            const bool host_no = !GB::IsSoldierAttack(144, 54) &&
                                 !GB::IsSoldierAttack(144, 57) &&
                                 !GB::IsSoldierAttack(144, 49);
            const bool mg_ok   = GB::IsSoldierAttack(152, 49) &&
                                 GB::IsSoldierAttack(152, 50);
            // 48 is gal0::g0_fall0, the DRIVER, and 51 is g0_guarding0 -- the
            // soldier reacting to a punch of Squall's. Neither is an attack;
            // .118 cued the first of them and never cued the second correctly.
            const bool mg_no   = !GB::IsSoldierAttack(152, 48) &&
                                 !GB::IsSoldierAttack(152, 51) &&
                                 !GB::IsSoldierAttack(152, 55);
            // An unknown field must never cue blind.
            const bool unk_no  = !GB::IsSoldierAttack(999, 49) &&
                                 !GB::IsSoldierAttack(999, 55);
            if (!(host_ok && host_no && mg_ok && mg_no && unk_no)) { bad++; printf("  BAD: check 22\n"); }
            printf("per-field attack labels: host=%d/%d minigame=%d/%d unknown=%d\n",
                   (int)host_ok, (int)host_no, (int)mg_ok, (int)mg_no, (int)unk_no);
            // And the names must differ per field for the same number, or the
            // log would hide the next mismatch the way it hid this one.
            const char* n144 = GB::LabelName(144, 55);
            const char* n152 = GB::LabelName(152, 55);
            printf("label 55: field144='%s' field152='%s'\n",
                   n144 ? n144 : "?", n152 ? n152 : "?");
            if (n144 && n152 && strcmp(n144, n152) == 0) { bad++; printf("  BAD: check 23\n"); }
        }
        // v0.20.112: the skip must HOLD. One write is not enough -- the BAT
        // showed the foe attacking from 0 HP for seventy seconds while Squall
        // was ground down and lost. Simulate that: engage, then take damage
        // every tick, and check the hold puts it back.
        {
            volatile short* sMax = (short*)(0x01CFE9B8 + 350);
            volatile short* sCur = (short*)(0x01CFE9B8 + 354);
            volatile short* fCur = (short*)(0x01CFE9B8 + 356);
            *sMax = 1000; *sCur = 759; *fCur = 600;
            *(unsigned*)(0x01CFE9B8 + 80) = 42;      // early in the round
            FF8Addresses::curField = 152;            // in bgbtl_1
            GB::s_skipActive = false; GB::s_reached152 = true;
            GB::SkipToVictory();
            for (int i = 0; i < 40; i++) {
                *sCur = (short)(*sCur - 90);     // the foe keeps swinging
                *fCur = (short)(*fCur + 30);     // and the script re-inits him
                GB::SkipTick();
            }
            printf("skip hold after 40 hits: Squall %d/%d  Foe %d  clock=%u\n",
                   (int)*sCur, (int)*sMax, (int)*fCur,
                   *(unsigned*)(0x01CFE9B8 + 80));
            // .121: Squall is pinned at FULL and never at zero -- zero is a
            // lethal value on whichever tick it lands. The soldier is silenced
            // at the REQ instead (SkipVetoLabel).
            if (*sCur != 1000) { bad++; printf("  BAD: squall not pinned at full\n"); }
            if (*fCur != 0)    { bad++; printf("  BAD: foe not zeroed\n"); }
            // The hold must not have moved the clock. v0.20.114 through .119
            // pushed it to 579/580 here; see the block below for why that is
            // now the defect being guarded against rather than the feature.
            if (*(unsigned*)(0x01CFE9B8 + 80) != 42) {
                bad++; printf("  BAD: the hold moved the fight clock\n");
            }
        }
        // v0.20.121: the skip must NOT write the clock, must pin Squall at
        // FULL (v0.20.120 parked him at zero and the 13:46 log answers that
        // with a Game Over four seconds after field 152 loaded), and must
        // silence the soldier by rewriting the attack REQ's label to the
        // entity's own `push` script -- which in both fields is exactly
        // `PUSH8 n ; RET 8`.
        {
            GB::s_skipActive = true; GB::s_reached152 = true;
            *(unsigned*)(0x01CFE9B8 + 80) = 200;         // mid-fight
            *(short*)(0x01CFE9B8 + 350) = 1000;
            *(short*)(0x01CFE9B8 + 354) = 123;
            GB::SkipTick();
            if (*(unsigned*)(0x01CFE9B8 + 80) != 200) {
                bad++; printf("  BAD: skip wrote the clock\n");
            }
            if (*(short*)(0x01CFE9B8 + 354) != 1000) {
                bad++; printf("  BAD: Squall not pinned at full (%d)\n",
                              (int)*(short*)(0x01CFE9B8 + 354));
            }
            if (*(short*)(0x01CFE9B8 + 356) != 0) {
                bad++; printf("  BAD: foe not zeroed\n");
            }
            // Squall must NEVER be written as zero -- that is a lethal value
            // whichever tick it lands on.
            *(unsigned*)(0x01CFE9B8 + 80) = 570;
            GB::SkipTick();
            if (*(short*)(0x01CFE9B8 + 354) <= 0) {
                bad++; printf("  BAD: Squall written to a lethal value\n");
            }
            // The veto: an attack label becomes `push`, everything else is
            // passed through untouched.
            const int16_t v152 = GB::SkipVetoLabel(152, 49);
            const int16_t v144 = GB::SkipVetoLabel(144, 56);
            const int16_t keep = GB::SkipVetoLabel(152, 33);   // squ_punched_up0
            if (v152 != 45 || v144 != 48 || keep != 33) {
                bad++; printf("  BAD: veto mapping wrong (%d %d %d)\n",
                              (int)v152, (int)v144, (int)keep);
            }
            printf("skip: clock untouched, Squall pinned at %d, attacks vetoed to "
                   "push (%d/%d)\n", (int)*(short*)(0x01CFE9B8 + 354),
                   (int)v152, (int)v144);
        }
        // v0.20.124: the double-tap is gone, so nothing in this module may write
        // the fight clock at all any more. That is the whole assertion, and it
        // is what guards the 82-second dead scene of v0.20.119.
        {
            GB::s_skipActive = true; GB::s_reached152 = true;
            for (int c = 100; c <= 1000; c += 300) {
                *(unsigned*)(0x01CFE9B8 + 80) = (unsigned)c;
                for (int i = 0; i < 20; i++) GB::SkipTick();
                if (*(unsigned*)(0x01CFE9B8 + 80) != (unsigned)c) {
                    bad++; printf("  BAD: the skip wrote the clock at %d\n", c);
                    break;
                }
            }
            // v0.20.125: the DISPLAYED bars are a different thing from the HP
            // variables -- Aaron's post-F9 screenshot had the soldier's bar
            // still full red while the mod had just said "You win."
            *(short*)(0x01D9CF5C + 0) = 600;      // Squall's gauge
            *(short*)(0x01D9CF5C + 16) = 600;     // the soldier's
            GB::SkipTick();
            if (*(short*)(0x01D9CF5C + 16) != 0) {
                bad++; printf("  BAD: the soldier's bar was not emptied\n");
            }
            if (*(short*)(0x01D9CF5C + 0) != *(short*)(0x01CFE9B8 + 350)) {
                bad++; printf("  BAD: Squall's bar was not refilled\n");
            }
            printf("skip: the fight clock is never written, and both HP bars agree "
                   "with the announcement\n");
        }
        // v0.20.118: past the resolution the CLOCK must be left alone (the
        // ending runs on it) but THE HP HOLD MUST CONTINUE -- Squall was being
        // punched from 383 down to 147 through the whole rescue scene.
        {
            volatile short* sCur = (short*)(0x01CFE9B8 + 354);
            volatile short* sMax = (short*)(0x01CFE9B8 + 350);
            *sMax = 1000;
            GB::s_skipActive = true; GB::s_reached152 = true;
            *(unsigned*)(0x01CFE9B8 + 80) = 700;
            GB::SkipTick();
            *sCur = 123;
            GB::SkipTick();
            if (*sCur != 1000) { bad++; printf("  BAD: HP hold stopped too early\n"); }
            *(unsigned*)(0x01CFE9B8 + 80) = 900;
            GB::SkipTick();
            if (*(unsigned*)(0x01CFE9B8 + 80) != 900) {
                bad++; printf("  BAD: clock written after the resolution\n");
            }
            printf("skip after resolution: HP still held=%d, clock left alone=%d\n",
                   (int)(*sCur == 1000), (int)(*(unsigned*)(0x01CFE9B8+80) == 900));
            // v0.20.121: the briefing must NOT accept a confirm that was
            // already down when it opened. Aaron held X through the Game Over
            // menu and briefings 2 and 3 died at 500 ms and 93 ms.
            GB::s_needKeyUp = true;
            // And foe-at-zero must NOT be announced as a win any more. The
            // skip is released first: .120 silences health reports while it
            // holds the numbers, because it parks Squall at zero on purpose.
            GB::s_skipActive = false;
            GB::s_foeDown = false; GB::s_sawFoeAlive = true;
            GB::s_wonAnnounced = false;
            GB::s_playerDown = false; GB::s_sawPlayerAlive = true;
            GB::PollHealth(nullptr);
            if (!GB::s_foeDown) { bad++; printf("  BAD: foe-at-zero not reported\n"); }
            // v0.20.124: the win is announced HERE, not at field 675 -- and it
            // engages the same protection F9 uses so it cannot be taken back.
            // v0.20.111 announced it and then LOST the fight 69 seconds later.
            if (!GB::s_wonAnnounced) { bad++; printf("  BAD: win not called from HP\n"); }
            if (!GB::s_skipActive)   { bad++; printf("  BAD: win not protected\n"); }
            printf("win-from-HP: announced at foe 0, held with the veto\n");
            GB::s_skipActive = false;
        }
        // v0.20.117: in the HOST field the clock is frozen, so the skip must end
        // the movie instead -- and must ask exactly once, not every tick.
        {
            FF8Addresses::curField = 144;
            *(unsigned*)(0x01CFE9B8 + 80) = 140;   // frozen, as measured
            GB::s_skipActive = false; GB::s_fmvSkipAsked = false;
            GB::s_reached152 = false;
            g_fmvSkipCalls = 0; g_fakeAvi = "disc01_32h.avi";
            GB::SkipToVictory();
            for (int i = 0; i < 30; i++) GB::SkipTick();
            printf("host-phase skip: FmvSkip::RequestSkip called %d time(s)\n", g_fmvSkipCalls);
            if (g_fmvSkipCalls != 1) { bad++; printf("  BAD: expected exactly one skip request\n"); }
            if (*(unsigned*)(0x01CFE9B8 + 80) != 140) {
                bad++; printf("  BAD: host clock was written (it is frozen, leave it)\n");
            }
            FF8Addresses::curField = 152;
            GB::s_skipActive = false;
        }
        // The tone must be a real waveform, not silence.
        GB::BuildTone();
        {
            const int16_t* pcm = (const int16_t*)(GB::s_toneBuf + 44);
            int peak = 0;
            for (int i = 0; i < 64; i++) { int v = pcm[i]; if (v < 0) v = -v; if (v > peak) peak = v; }
            printf("tone peak in first 64 samples: %d\n", peak);
            if (peak < 1000) { bad++; printf("  BAD: check 24\n"); }              // taper is 4 ms; must ramp up fast
        }
        // v0.20.129: THE MOVIE'S DESCRIPTION TRACK MUST NOT TALK OVER THE FIGHT.
        // disc01_33h.avi plays behind the whole round and has its own audio
        // description; in the 2026-08-15 log a cue landed one second before
        // "Heavy punch ready" and both went out as interrupting speech. The
        // briefing suppressed it and EVERY exit resumed it, including the one
        // that starts the fight -- so the suppression has to survive EndBriefing
        // and be lifted only by the win, the loss, F9 or the disarm.
        {
            FF8Addresses::curField = 152;
            const bool wasBrief = GB::s_briefing;
            g_narrationSuppressed = -1;
            GB::s_briefing = true; GB::s_briefStart = 0;
            GB::EndBriefing("probe", false);
            if (g_narrationSuppressed == 0) {
                bad++; printf("  BAD: starting the fight resumed the movie narration\n");
            }
            // ...and the win gives it back, because the rescue scene follows.
            volatile short* foe = (short*)(0x01CFE9B8 + 356);
            GB::s_foeDown = false; GB::s_sawFoeAlive = true;
            GB::s_skipActive = false; GB::s_wonAnnounced = false;
            g_narrationSuppressed = 1;
            *foe = 0;
            GB::PollHealth(nullptr);
            if (!GB::s_foeDown) { bad++; printf("  BAD: win not called\n"); }
            if (g_narrationSuppressed != 0) {
                bad++; printf("  BAD: the win left the movie narration suppressed\n");
            }
            // ...and so does the disarm, whatever the reason.
            g_narrationSuppressed = 1;
            GB::s_inMinigame = true;
            GB::Disarm(152, "probe");
            if (g_narrationSuppressed != 0) {
                bad++; printf("  BAD: the disarm left the movie narration suppressed\n");
            }
            GB::s_briefing = wasBrief;
            GB::s_skipActive = false; GB::s_foeDown = false;
            printf("movie narration: held through the whole round, given back at "
                   "the win and at the disarm\n");
        }
        printf("briefing/freeze checks: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        if (bad) return 1;
    }
    printf("compile probe OK\n");
    return 0;
}
