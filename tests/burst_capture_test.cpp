// burst_capture_test.cpp -- the screenshot burst schedule (#megaflare, v0.107.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills.
#include <cstdio>

#include <cstring>
#include <cstddef>
#include "battle_burst_capture_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

// Step a burst `frames` times and return how many shots it authorised,
// recording the frame index of each into `at`.
static int RunFrames(BurstCapture* b, int frames, int* at, int atMax)
{
    int shots = 0;
    for (int f = 0; f < frames; f++) {
        if (BurstStep(b)) {
            if (shots < atMax) at[shots] = f;
            shots++;
        }
    }
    return shots;
}

static void TestFiresOnTheFirstFrame()
{
    BurstCapture b; BurstReset(&b);
    CHECK(BurstArm(&b, 3, 10), "arming succeeds");
    // Kills a lead-in: the trigger is already late, the first shot is now.
    CHECK(BurstStep(&b), "the first step captures");
    CHECK(BurstShotNumber(&b) == 0, "the first capture is shot 0");
}

static void TestCadence()
{
    BurstCapture b; BurstReset(&b);
    BurstArm(&b, 4, 5);
    int at[8] = {0};
    int shots = RunFrames(&b, 100, at, 8);
    CHECK(shots == 4, "exactly the requested number of shots is taken");
    // Kills interval off-by-one in either direction.
    CHECK(at[0] == 0,  "shot 0 on frame 0");
    CHECK(at[1] == 5,  "shot 1 five frames later");
    CHECK(at[2] == 10, "shot 2 ten frames in");
    CHECK(at[3] == 15, "shot 3 fifteen frames in");
}

static void TestIntervalOne()
{
    // interval 1 means every frame -- the degenerate case the countdown
    // arithmetic (interval - 1) has to survive.
    BurstCapture b; BurstReset(&b);
    BurstArm(&b, 3, 1);
    int at[4] = {0};
    CHECK(RunFrames(&b, 10, at, 4) == 3, "interval 1 takes every frame");
    CHECK(at[0] == 0 && at[1] == 1 && at[2] == 2, "and takes them consecutively");
}

static void TestStopsWhenDone()
{
    BurstCapture b; BurstReset(&b);
    BurstArm(&b, 2, 3);
    int at[4] = {0};
    RunFrames(&b, 100, at, 4);
    CHECK(!BurstIsRunning(&b), "the burst is over when the shots are spent");
    // Kills a missing remaining-- : this would run forever.
    CHECK(!BurstStep(&b), "a spent burst captures nothing");
    CHECK(RunFrames(&b, 500, at, 4) == 0, "and captures nothing for a long time after");
}

static void TestRearmRefusedWhileRunning()
{
    BurstCapture b; BurstReset(&b);
    BurstArm(&b, 6, 20);
    BurstStep(&b);                       // shot 0 taken, 5 remain
    // The ability name lands in the window more than once. If a second arm
    // reset the run, the tail of the animation -- the part being hunted --
    // would be the part that got dropped.
    CHECK(!BurstArm(&b, 6, 20), "re-arming while running is refused");
    CHECK(b.remaining == 5, "and the running burst is untouched");
    CHECK(b.index == 1,     "including its shot counter");
}

static void TestRearmAllowedWhenDone()
{
    BurstCapture b; BurstReset(&b);
    BurstArm(&b, 1, 4);
    BurstStep(&b);
    CHECK(!BurstIsRunning(&b), "one-shot burst is done after one step");
    CHECK(BurstArm(&b, 2, 4),  "a second Mega Flare can arm a second burst");
    CHECK(b.index == 0,        "and the new burst numbers its shots from 0");
}

static void TestBadArguments()
{
    BurstCapture b; BurstReset(&b);
    CHECK(!BurstArm(&b, 0, 10),  "zero shots is a no-op");
    CHECK(!BurstArm(&b, -1, 10), "negative shots is a no-op");
    // Kills dropping the interval guard: interval 0 would make countdown -1
    // and fire on every frame until remaining ran out -- survivable -- but
    // interval < 0 would push countdown negative and never recover.
    CHECK(!BurstArm(&b, 4, 0),   "zero interval is refused");
    CHECK(!BurstArm(&b, 4, -3),  "negative interval is refused");
    CHECK(!BurstIsRunning(&b),   "none of those armed anything");
    CHECK(!BurstArm(nullptr, 4, 4), "a null burst arms nothing");
    CHECK(!BurstStep(nullptr),      "a null burst steps nothing");
    CHECK(!BurstIsRunning(nullptr), "a null burst is not running");
}

static void TestResetClears()
{
    BurstCapture b; BurstReset(&b);
    BurstArm(&b, 9, 9);
    BurstStep(&b);
    BurstReset(&b);
    CHECK(!BurstIsRunning(&b), "reset stops a running burst");
    CHECK(b.index == 0,        "reset clears the shot counter");
    CHECK(BurstArm(&b, 2, 2),  "and leaves it armable");
}

// ---------------------------------------------------------------------------
// What arms it (v0.110.0)
// ---------------------------------------------------------------------------
static void TestTextTrigger()
{
    CHECK(BurstTextTriggers("Mega Flare", "Mega Flare"), "the ability-name box arms it");
    // FF8 pads its window strings; that padding must not stop the trigger.
    CHECK(BurstTextTriggers("Mega Flare  ", "Mega Flare"), "trailing padding is trimmed");
    CHECK(BurstTextTriggers("  Mega Flare", "Mega Flare"), "leading padding is trimmed");
    CHECK(BurstTextTriggers(" Mega Flare ", "Mega Flare"), "both ends are trimmed");
    // THE ONE THAT COST A RUN. This is the scan description, verbatim from the
    // 2026-08-27 log, and the substring form armed on it.
    CHECK(!BurstTextTriggers("Called the King of GF; its Mega Flare ignores all "
                             "defense. Gives assistance freely to those who show "
                             "their power", "Mega Flare"),
          "the scan description does NOT arm it");
    // Kills relaxing back to a prefix or suffix match.
    CHECK(!BurstTextTriggers("Mega Flares", "Mega Flare"), "a longer name does not match");
    CHECK(!BurstTextTriggers("Mega Flar", "Mega Flare"),   "a shorter name does not match");
    CHECK(!BurstTextTriggers("mega flare", "Mega Flare"),  "the match is case sensitive");
    CHECK(!BurstTextTriggers("Diamond Dust", "Mega Flare"), "another ability does not match");
    CHECK(!BurstTextTriggers("", "Mega Flare"),   "an empty box does not match");
    CHECK(!BurstTextTriggers("   ", "Mega Flare"),"a box of spaces does not match");
    CHECK(!BurstTextTriggers(nullptr, "Mega Flare"), "null text does not match");
    CHECK(!BurstTextTriggers("Mega Flare", nullptr), "a null name matches nothing");
    CHECK(!BurstTextTriggers("Mega Flare", ""),     "an empty name matches nothing");
}

int main()
{
    TestTextTrigger();
    TestFiresOnTheFirstFrame();
    TestCadence();
    TestIntervalOne();
    TestStopsWhenDone();
    TestRearmRefusedWhileRunning();
    TestRearmAllowedWhenDone();
    TestBadArguments();
    TestResetClears();
    printf("burst_capture_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
