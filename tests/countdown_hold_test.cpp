// countdown_hold_test.cpp -- RUNS the real src/countdown_timer.cpp on the host.
//
//   g++ -std=c++17 -O0 -Isrc -Itests/winshim -Itests -o countdown_hold_test \
//       tests/countdown_hold_test.cpp
//
// WHY THIS EXISTS
// ---------------
// v0.63.1 gives the space rescue a way to stop the mission clock:
//
//     "The controls should essentially pause everything until the player hits
//      Enter to proceed."   -- Aaron, 2026-08-22
//
// CountdownTimer::SetHold is the mechanism, and it has one property that is
// easy to get wrong and impossible to see in a BAT until it has already cost a
// run: THE CLOCK DOES NOT EXIST YET WHEN THE HOLD IS PLACED. Aaron's log has
// the field loading at 16:10:53 and "ENTER ACTIVE: rawValue=88" at 16:10:56.
// A hold that only worked against an already-detected timer would silently do
// nothing for the only three seconds it was needed, and the module would go on
// to brief the player over a clock that never stopped -- which is exactly the
// v0.63.0 failure wearing a different hat.
//
// So this probe drives the REAL module -- its own detection state machine, its
// own SEH-guarded reads and writes, at the game's own addresses, which are
// mmap'd here -- rather than a stub of it. The countdown global is a real page
// at 0x01CFE92C and the probe watches what the module writes into it.
//
// It is also the first test of any kind over countdown_timer.cpp, whose
// detection/stall/dismissal machinery has been carrying the Dollet and Fire
// Cavern timers since v0.15.12 on BAT evidence alone.

#define WINSHIM_HOST_CLOCK 1
#define WINSHIM_HOST_INPUT 1

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sys/mman.h>

typedef unsigned long DWORD_HOST;   // documentation only; windows.h defines DWORD

// The clock and the keyboard the probe drives. Declared before <windows.h>
// pulls in anything, and the shim leaves both alone when the two macros above
// are defined.
static unsigned long g_tick = 100000;
extern "C" unsigned long GetTickCount() { return g_tick; }
static int g_keys = 0;              // bit0 = 'T', bit1 = Shift
extern "C" short GetAsyncKeyState(int vk)
{
    if (vk == 'T')      return (g_keys & 1) ? (short)0x8000 : 0;
    if (vk == 0x10)     return (g_keys & 2) ? (short)0x8000 : 0;   // VK_SHIFT
    return 0;
}

#include <windows.h>

// ---------------------------------------------------------------------------
// The mod seams countdown_timer.cpp reaches for.
// ---------------------------------------------------------------------------
static std::vector<std::string> g_said;
static std::vector<bool>        g_saidInterrupt;   // v0.65.4: WHICH kind of speak
static std::vector<std::string> g_logged;
namespace ScreenReader {
    bool Speak(const char* t, bool interrupt)
    {
        g_said.push_back(t ? t : "");
        g_saidInterrupt.push_back(interrupt);
        return true;
    }
}
namespace Log {
    void Mod(const char* fmt, ...)
    {
        char b[1024]; va_list ap; va_start(ap, fmt);
        vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        g_logged.push_back(b);
    }
}
namespace FF8Addresses { WORD* pGameMode = nullptr; }

// The two engine globals the module reads and writes, at their own addresses.
static const uintptr_t TIMER_PAGE = 0x01CF0000u;   // covers 0x01CFE92C
static const uintptr_t VISFLAG_PAGE = 0x01D2B000u; // covers 0x01D2B813
static const uintptr_t LIVE_TIMER = 0x01CFE92Cu;
static const uintptr_t VIS_FLAG   = 0x01D2B813u;
static WORD g_mode = 1;                            // FF8Addresses::MODE_FIELD

static void mapAt(uintptr_t base, size_t len)
{
    void* p = mmap((void*)base, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void*)base) { std::printf("FATAL: mmap 0x%lX\n", (unsigned long)base); std::exit(2); }
    std::memset(p, 0, len);
}
static void setRaw(uint16_t v) { *(volatile uint16_t*)LIVE_TIMER = v; }
static uint16_t getRaw()       { return *(volatile uint16_t*)LIVE_TIMER; }
static void setVis(uint8_t v)  { *(volatile uint8_t*)VIS_FLAG = v; }

// ---------------------------------------------------------------------------
#include "countdown_timer.cpp"
// ---------------------------------------------------------------------------

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }
static void clearSaid() { g_said.clear(); g_saidInterrupt.clear(); }
// Index of the first utterance containing `needle`, or -1.
static int saidIndex(const char* needle)
{
    for (size_t i = 0; i < g_said.size(); i++)
        if (g_said[i].find(needle) != std::string::npos) return (int)i;
    return -1;
}
static bool loggedContains(const char* needle)
{
    for (auto& l : g_logged) if (l.find(needle) != std::string::npos) return true;
    return false;
}
// One mod-thread tick. The real one runs far faster than 1 Hz; the module's
// own stall watch is in wall-clock ms, so the probe advances both.
static void tick(int ms = 16) { g_tick += (unsigned long)ms; CountdownTimer::Update(); }

// Run the engine's own decrement: it writes the global once a second, and the
// whole point of a freeze is that our write lands after the engine's.
static void engineSecond()
{
    uint16_t r = getRaw();
    if (r) setRaw((uint16_t)(r - 1));
    for (int i = 0; i < 1000 / 16; i++) tick();
}

int main()
{
    mapAt(TIMER_PAGE, 0x00020000);
    mapAt(VISFLAG_PAGE, 0x00002000);
    FF8Addresses::pGameMode = &g_mode;
    setVis(1);
    std::printf("countdown_hold_test\n");

    // =======================================================================
    // 1. THE HOLD SURVIVES DETECTION ORDER -- the property Aaron's log demands.
    // =======================================================================
    // The space rescue asks for the clock on the frame the field loads. The
    // engine has not written a classifiable value yet, so there is nothing to
    // freeze; the request has to be remembered.
    setRaw(0);
    for (int i = 0; i < 5; i++) tick();
    check(!CountdownTimer::IsActive(), "no timer before the engine writes one");

    CountdownTimer::SetHold(true, "probe: before detection");
    check(!CountdownTimer::IsHeldFrozen(),
          "**a hold on a timer that does not exist yet is not a freeze** -- the "
          "caller must be able to tell, because how long it can afford to wait "
          "depends on the answer");
    check(loggedContains("no timer yet"), "and it says so in the log");

    // Now the engine starts the ninety-second clock, exactly as it does two
    // seconds into ssspace3. Detection is DECREMENT-GATED (#59) -- a static
    // leftover value must never start a timer -- so it costs two seconds of
    // real countdown before there is anything to freeze. That is precisely
    // what Aaron's log shows: raw=90 at 16:10:56 and "ENTER ACTIVE:
    // rawValue=88" the moment after. Two seconds of ninety is the whole price
    // of the pause, and it is the engine's price, not the mod's.
    g_logged.clear(); clearSaid();
    setRaw(90);
    tick();
    check(!CountdownTimer::IsActive(), "one sighting is not a countdown");
    setRaw(89); for (int i = 0; i < 1000 / 16; i++) tick();
    setRaw(88); for (int i = 0; i < 1000 / 16; i++) tick();
    check(CountdownTimer::IsActive(), "the engine's write is detected");
    check(CountdownTimer::IsHeldFrozen(),
          "**and the remembered hold is applied the instant it is** -- this is "
          "the whole reason SetHold takes a request rather than an order");
    check(loggedContains("HOLD applied at detection"), "the log records the moment");

    // ...AND IT DOES NOT SAY SO OUT LOUD. Aaron, 2026-08-24: "the game timer
    // interrupts the announcement of the game controls when the controls
    // initially appear." It did, and with an INTERRUPT, so the brief the whole
    // scene depends on was cut off two sentences in. A clock detected under a
    // hold is a clock somebody has deliberately stopped in order to talk over
    // it; the announcement waits for the release.
    check(saidIndex("Timer detected") < 0,
          "**a timer detected under a hold does not announce itself** -- the "
          "module that placed the hold is mid-sentence, and the number would be "
          "wrong anyway: the clock was frozen on the line above");
    check(loggedContains("announcement DEFERRED"), "and the log says it is only deferred");

    // The clock is stopped: the engine decrements and the module writes it back.
    {
        bool everMoved = false;
        for (int s = 0; s < 20; s++) {
            engineSecond();
            if (getRaw() != 88) everMoved = true;
        }
        check(!everMoved,
              "**twenty seconds of engine decrements leave the clock at 88** -- "
              "a screen the player reads at his own pace costs him nothing");
    }

    // ...and it does not fill the log with a hundred and thirty lines saying
    // the freeze is working. The global really does oscillate -- the engine
    // decrements, this module writes it back -- and v0.63.2 logged every edge.
    {
        g_logged.clear();
        for (int s2 = 0; s2 < 10; s2++) engineSecond();
        int rawLines = 0;
        for (auto& l : g_logged) if (l.find("live raw=") != std::string::npos) rawLines++;
        check(rawLines == 0,
              "**a frozen timer does not log its own oscillation** -- the value "
              "is pinned by definition and the FROZEN transition is already in "
              "the log");
    }

    // ...and the stall watch does not mistake a held clock for a dead sequence.
    check(CountdownTimer::IsActive(), "a held timer is still an active timer");
    check(!loggedContains("timer stalled"),
          "**the eight-second stall watch does not fire on a clock we are "
          "holding on purpose**");

    // =======================================================================
    // 2. THE RELEASE GIVES IT BACK, AND DOES NOT IMMEDIATELY KILL IT.
    // =======================================================================
    g_logged.clear(); clearSaid();
    CountdownTimer::SetHold(false, "probe: Enter");
    check(!CountdownTimer::IsHeldFrozen(), "the hold is off");
    // THE DEFERRED ANNOUNCEMENT COMES DUE HERE. Deferred, not dropped: Aaron
    // asked for the clock to stop, not to be kept secret, and the release is
    // the first moment the number is true.
    {
        const int i = saidIndex("Timer detected");
        check(i >= 0,
              "**the deferred announcement is spoken when the hold releases** -- "
              "the player still has to be told how long he has");
        if (i >= 0) {
            check(!g_saidInterrupt[(size_t)i],
                  "**and it is QUEUED, not an interrupt** -- the module that just "
                  "released the clock is usually mid-sentence about having done "
                  "so, and cutting THAT in half is the same bug facing the other "
                  "way");
            check(g_said[(size_t)i].find("1 minutes 28 seconds") != std::string::npos,
                  "and it reports what is actually left (88 s), not a stale figure");
        }
    }
    check(CountdownTimer::IsActive(), "the timer is still running");
    // The mod thread ticks about sixty times a second and the engine writes
    // once, so between the release and the engine's next decrement there is
    // most of a second in which the value has not changed -- against a
    // stall clock that has been standing still for the whole hold. Every one
    // of those ticks is a chance to declare the sequence dead. THIS is the
    // assertion that the reset in SetHold is load-bearing; without it the very
    // first Update after Enter throws the timer away and Aaron flies the
    // approach with no clock and no announcements.
    for (int i = 0; i < 900 / 16; i++) tick();
    check(CountdownTimer::IsActive(),
          "**and survives the gap before the engine's next write** -- a stall "
          "clock frozen along with the timer must be given back too");
    check(!loggedContains("timer stalled"), "nothing called it a dead sequence");
    engineSecond();
    check(getRaw() == 87, "the engine's decrement now sticks");
    for (int s = 0; s < 5; s++) engineSecond();
    check(getRaw() == 82, "and keeps sticking");
    check(!loggedContains("timer stalled"),
          "**releasing does not trip the stall watch** -- the value had not "
          "changed for the whole hold, and without resetting the clock the very "
          "next Update would call the sequence dead");

    // Announcements still work after a hold: the boundaries are what a blind
    // player has instead of the HUD.
    clearSaid();
    while (getRaw() > 58) engineSecond();
    engineSecond();
    {
        bool sawMinute = false;
        for (auto& s : g_said) if (s.find("1 minute") != std::string::npos) sawMinute = true;
        check(sawMinute, "the 60-second boundary still announces after a hold");
    }

    // =======================================================================
    // 3. IT IS SILENT. Shift+T talks because Aaron pressed it; this fires
    //    underneath a Game Controls screen that is already speaking.
    // =======================================================================
    clearSaid();
    CountdownTimer::SetHold(true, "probe: silence");
    tick();
    CountdownTimer::SetHold(false, "probe: silence");
    tick();
    check(g_said.empty(), "**the programmatic hold never speaks**");

    // Idempotent: the module calls Hold(true) every frame until it briefs.
    g_logged.clear();
    CountdownTimer::SetHold(true, "probe: repeat");
    const size_t n = g_logged.size();
    for (int i = 0; i < 50; i++) CountdownTimer::SetHold(true, "probe: repeat");
    check(g_logged.size() == n, "repeating the same request logs once, not fifty times");
    CountdownTimer::SetHold(false, "probe: repeat");

    // =======================================================================
    // 4. THE PLAYER'S OWN FREEZE OUTRANKS OURS.
    // =======================================================================
    // Shift+T is Aaron's key. If he freezes the clock by hand while a hold is
    // up, releasing the hold must not hand the clock back underneath him...
    CountdownTimer::SetHold(true, "probe: hold then shift+T");
    check(CountdownTimer::IsHeldFrozen(), "held");
    g_keys = 3; tick(); g_keys = 0; tick();       // Shift+T: releases the freeze
    check(!CountdownTimer::IsHeldFrozen(),
          "**Shift+T takes the freeze away from the hold** -- it is his key");
    const uint16_t before = getRaw();
    CountdownTimer::SetHold(false, "probe: hold then shift+T");
    engineSecond();
    check(getRaw() == (uint16_t)(before - 1),
          "and releasing a hold that is no longer holding anything changes nothing");

    // =======================================================================
    // 5. LEAVING THE SCENE. The module's own dismissal path must still work
    //    while held, or a hold would outlive the field that placed it.
    // =======================================================================
    CountdownTimer::SetHold(true, "probe: dismissal");
    check(CountdownTimer::IsHeldFrozen(), "held again");
    setVis(0);
    for (int i = 0; i < 6000 / 16; i++) tick();   // sustained, past the debounce
    check(!CountdownTimer::IsActive(),
          "**the HUD dismissal still ends the session while held** -- the scene "
          "is over and pinning a dead global forever is not a feature");
    check(!CountdownTimer::IsHeldFrozen(), "and nothing is being pinned any more");
    CountdownTimer::SetHold(false, "probe: dismissal");
    setVis(1);

    std::printf(bad ? "FAIL: %d\n" : "OK (%d failures)\n", bad);
    return bad ? 1 : 0;
}
