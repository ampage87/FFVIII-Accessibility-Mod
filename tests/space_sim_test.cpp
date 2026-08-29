// space_sim_test -- the disc-3 space rescue, flown offline by a simulated
// blind player who is given ONLY what the mod says.
//
// WHY THIS EXISTS
// ---------------
// Aaron: "this mini-game has a long story scene right before it so doing a BAT
// is time consuming... Do your best to reproduce the game in an offline sim,
// test the current mod support, identify and improve the problems noted."
//
// So the loop is closed here instead. The scene is reproduced from the model's
// own reading of the key handlers -- SrStepFrame IS rinoa::key{l,r,u,d} -- and
// the pilot is given nothing but the strings SrCall produces, on the module's
// real cadence, with a human reaction delay. If the pilot cannot land inside
// the game's own 180-unit box, the announcements are not steerable, and that is
// a failure of the mod and not of the pilot.
//
// THE ONE ASSUMPTION, AND HOW IT IS BOUNDED
// -----------------------------------------
// Nothing here moves the error except the player. That is read off Aaron's
// 2026-08-22 log rather than guessed: in his first attempt X sat at 2500 and
// then 2504 fifty-three seconds later, one unboosted step of change across the
// whole window, while Y travelled 628 units under the DOWN he was holding. The
// scene closes the DISTANCE to Rinoa on its own ("She will move toward you
// automatically"); it does not slide her sideways. DRIFT_PER_SEC below lets the
// test state that as a number, and the suite runs the whole matrix again with a
// pessimistic non-zero drift so a wrong assumption fails loudly rather than
// silently flattering the design.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#ifndef _WIN32
#define _stricmp strcasecmp
#include <strings.h>
#endif

#include "space_rescue_model.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}

// ---------------------------------------------------------------------------
// The cadence under test. These are the module's numbers; the test fails if
// they drift apart, which is what keeps this from becoming a second opinion.
// ---------------------------------------------------------------------------
static const int SIM_TICK_MS      = 1000 / SR_TICK_HZ;   // one script frame
static const int SIM_CALL_MS      = 3500;   // routine steering call
static const int SIM_NEAR_MS      = 1200;   // inside SIM_NEAR_BAND, faster
static const int SIM_NEAR_BAND    = 900;
static const int SIM_REACT_MS     = 400;    // hear it, then move
static const int SIM_APPROACH_MS  = 88000;  // the scene's own timer

struct Pilot {
    uint16_t held      = 0;
    uint16_t pending   = 0;
    int      actAt     = -1;
    void hear(const char* words, int nowMs) {
        const uint16_t want = SrMaskForCall(words);
        if (want == held && actAt < 0) return;
        pending = want;
        actAt   = nowMs + SIM_REACT_MS;
    }
    void tick(int nowMs) {
        if (actAt >= 0 && nowMs >= actAt) { held = pending; actAt = -1; }
    }
};

struct Result { int x, y; bool won; int calls; int silentMaxMs; int fuelUsed; int fuelAtWon; int wonAtMs; };

// One approach. `callMs` / `nearMs` are the cadence being tested; `dedupe`
// reproduces the v0.62.x behaviour where an unchanged phrase was not spoken.
//
// v0.63.2: `boost` holds the boost button for the whole flight. That is TWO
// changes at once and the model carries both: the step is SR_STEP_BOOST (16,
// because director1 writes 8 into var[1034] and rinoa::keyl then multiplies by
// 2 again), and the fuel drains at SR_STEP_VAR_BOOST (8) rather than SR_STEP
// per iteration -- director1 subtracts var[1034], not the distance moved. So
// boosting buys twice the travel per unit of fuel, which is why nothing in the
// scene punishes using it.
static Result Fly(int x0, int y0, int callMs, int nearMs, bool dedupe,
                  int driftPerSec, bool boxCueIsInstant, bool boost = false)
{
    Pilot p;
    const int step     = boost ? SR_STEP_BOOST : SR_STEP;
    const int fuelStep = boost ? SR_STEP_VAR_BOOST : SR_STEP;
    int fuel = SR_FUEL_FULL, wonAt = -1, fuelAtWon = 0;
    int x = x0, y = y0;
    char last[64] = ""; char words[64] = "";
    int nextCall = 0, calls = 0, lastSpoke = 0, silentMax = 0, driftAcc = 0;
    bool wasIn = SrCentred(x, y);
    for (int t = 0; t <= SIM_APPROACH_MS; t += SIM_TICK_MS) {
        // --- the scene ---
        if (p.held) { SrStepFrame(&x, &y, p.held, step); fuel -= fuelStep; }
        if (wonAt < 0 && SrCentred(x, y) && t > 0) { wonAt = t; fuelAtWon = SR_FUEL_FULL - fuel; }
        driftAcc += driftPerSec;
        while (driftAcc >= SR_TICK_HZ) { x += 1; driftAcc -= SR_TICK_HZ; }
        while (driftAcc <= -SR_TICK_HZ) { x -= 1; driftAcc += SR_TICK_HZ; }
        p.tick(t);

        // --- the module ---
        const bool in = SrHeld(x, y);
        bool speak = false;
        if (boxCueIsInstant && in != wasIn) { speak = true; }   // box entry/exit
        wasIn = in;
        const int worstAbs = (abs(x) > abs(y) * SR_CLAMP_X / SR_CLAMP_Y) ? abs(x) : abs(y);
        const int period = (worstAbs < SIM_NEAR_BAND) ? nearMs : callMs;
        if (t >= nextCall) { speak = true; nextCall = t + period; }
        if (speak) {
            SrCall(x, y, words, sizeof words);
            if (dedupe && strcmp(words, last) == 0) {
                // v0.62.x: an unchanged phrase was swallowed.
            } else {
                if (t - lastSpoke > silentMax) silentMax = t - lastSpoke;
                lastSpoke = t;
                calls++;
                p.hear(words, t);
            }
            strncpy(last, words, sizeof(last) - 1);
        }
    }
    Result r; r.x = x; r.y = y; r.won = SrCentred(x, y);
    r.calls = calls; r.silentMaxMs = silentMax;
    r.fuelUsed = SR_FUEL_FULL - fuel; r.fuelAtWon = fuelAtWon; r.wonAtMs = wonAt;
    return r;
}

int main()
{
    // Aaron's three real starting errors, straight out of the BAT log, plus a
    // spread of corners and edges.
    struct { int x, y; const char* what; } starts[] = {
        {  2500,  -800, "attempt 1 (log 14:52:59)" },
        {   900,   -43, "attempt 2 (log 14:55:27)" },
        {  3000,  -350, "attempt 3 (log 14:57:15)" },
        {  8100,  7300, "far corner" },
        { -8100, -7300, "opposite corner" },
        {     0,  7300, "straight up" },
        { -8100,     0, "hard left" },
        {   181,   181, "one unit outside the box" },
        {  -600,   250, "close, both axes" },
    };
    const int n = (int)(sizeof(starts)/sizeof(starts[0]));

    // WHAT THE CLOCK CAN ACTUALLY BUY.
    //
    // v0.63.0 flew this matrix at SR_TICK_HZ=30 -- 120 units a second -- and
    // every start won, corners included. Aaron's 2026-08-22 log then measured
    // the real thing at FORTY-EIGHT (x = 896, 848, 800, 752 on consecutive
    // seconds under one held arrow), and at 48 units/s the far corner is
    // 8100/48 = 169 seconds away with ninety on the clock. Those starts are not
    // hard, they are ARITHMETICALLY IMPOSSIBLE, and a suite that reported them
    // as passing was flattering the design with a number nobody had checked.
    //
    // So the matrix is split by the budget rather than trimmed to fit it. A
    // start inside the budget must be WON. A start outside it must still be
    // flown perfectly -- full closing rate on both axes for the whole approach,
    // which is the only thing the announcements can be held responsible for --
    // and is expected to run out of clock.
    // The rate itself, pinned to the measurement rather than to a belief. If
    // somebody "fixes" SR_TICK_HZ back to the frame rate, this is the line that
    // makes them look at the log first.
    check(SR_UNITS_PER_SEC == 48,
          "**48 units a second, measured** -- Aaron's 2026-08-22 log, x = 896, "
          "848, 800, 752 on consecutive seconds under one held arrow. The 120 "
          "v0.63.0 assumed came from 'scripts run once a frame', which is a "
          "reasonable belief and not what the game does");
    const double SIM_T_SEC = SIM_APPROACH_MS / 1000.0;
    auto budget = [&](int drift) {
        return (double)(SR_UNITS_PER_SEC - (drift < 0 ? -drift : drift)) * SIM_T_SEC;
    };
    std::printf("space_sim: %d ms / %d ms near; %d units/s, %.0f s => %.0f units "
                "of closing available per axis\n",
                SIM_CALL_MS, SIM_NEAR_MS, SR_UNITS_PER_SEC, SIM_T_SEC, budget(0));

    int worstSilence = 0;
    for (int i = 0; i < n; i++) {
        const int ax = starts[i].x < 0 ? -starts[i].x : starts[i].x;
        const int ay = starts[i].y < 0 ? -starts[i].y : starts[i].y;
        const bool reachable = ax <= budget(0) && ay <= budget(0);
        Result r = Fly(starts[i].x, starts[i].y, SIM_CALL_MS, SIM_NEAR_MS,
                       /*dedupe=*/false, /*drift=*/0, /*boxCue=*/true);
        std::printf("  %-28s -> (%5d,%5d) %s  %-12s calls=%3d  longest silence %d ms\n",
                    starts[i].what, r.x, r.y, r.won ? "WON " : "LOST",
                    reachable ? "(in budget)" : "(out of reach)", r.calls,
                    r.silentMaxMs);
        if (reachable) {
            check(r.won, "**every start the clock can reach is flyable on the "
                         "words alone**");
        } else {
            // Not won -- but the steering must still have spent every unit of
            // closing the clock allowed, on BOTH axes. If the words were wrong
            // the pilot would have wandered and closed less.
            const int closedX = ax - (r.x < 0 ? -r.x : r.x);
            const int closedY = ay - (r.y < 0 ? -r.y : r.y);
            const int need = (int)(budget(0) * 0.97);
            check((ax < need || closedX >= need) && (ay < need || closedY >= need),
                  "**an unreachable start still closes at the full rate the "
                  "whole way** -- it runs out of clock, not out of instructions");
        }
        if (r.silentMaxMs > worstSilence) worstSilence = r.silentMaxMs;
    }
    check(worstSilence <= 4000,
          "**the mod is never silent for more than four seconds** -- Aaron: "
          "\"The TTS should speak frequently throughout the game, with maybe a "
          "3-5 second pause in between\"");

    // Aaron's three real starts are the ones that matter: the scene puts him
    // around x=2500, not in a corner.
    check(2500 <= budget(0) && 3000 <= budget(0),
          "**the scene's own starting errors are inside the budget** -- which is "
          "why he could win it by hand");

    // The same matrix with a pessimistic sideways drift the log does not show,
    // so the design does not rest on the no-drift reading. The drift eats into
    // the budget, so the budget is recomputed with it.
    int driftWins = 0, driftReach = 0;
    for (int i = 0; i < n; i++) {
        const int ax = starts[i].x < 0 ? -starts[i].x : starts[i].x;
        const int ay = starts[i].y < 0 ? -starts[i].y : starts[i].y;
        const bool reachable = ax <= budget(20) && ay <= budget(0);
        Result r = Fly(starts[i].x, starts[i].y, SIM_CALL_MS, SIM_NEAR_MS,
                       false, /*drift=*/20, true);
        if (!reachable) continue;
        driftReach++;
        if (r.won) driftWins++;
        else std::printf("    drift LOST from %-24s -> (%d,%d)\n",
                         starts[i].what, r.x, r.y);
    }
    std::printf("  with a 20 units/s sideways drift: %d/%d of the reachable starts\n",
                driftWins, driftReach);
    check(driftReach > 0 && driftWins == driftReach,
          "and still flyable if the scene does drift her sideways");

    // ---- THE BOOST (v0.63.2) -----------------------------------------------
    //
    // Aaron: "There is an option to boost for a limited duration... We need to
    // include this control on the Game Controls screen."
    //
    // It changes the shape of the scene, which is why it belongs on the screen
    // and not in a footnote: sixteen units a frame instead of four puts the far
    // corners INSIDE the clock, and they were unreachable without it. The fuel
    // is checked at the same time, because "limited duration" is the reason to
    // hesitate and the arithmetic says not to.
    {
        std::printf("  boosted: %d units/s, %d fuel, %d units of travel per unit "
                    "of fuel\n",
                    SR_STEP_BOOST * SR_TICK_HZ, SR_FUEL_FULL,
                    SR_STEP_BOOST / SR_STEP_VAR_BOOST);
        int bWins = 0, worstArrive = 0, worstWhole = 0, slowest = 0;
        for (int i = 0; i < n; i++) {
            Result r = Fly(starts[i].x, starts[i].y, SIM_CALL_MS, SIM_NEAR_MS,
                           false, 0, true, /*boost=*/true);
            if (r.won) bWins++;
            else std::printf("    boosted LOST from %-24s -> (%d,%d)\n",
                             starts[i].what, r.x, r.y);
            if (r.fuelAtWon > worstArrive) worstArrive = r.fuelAtWon;
            if (r.fuelUsed  > worstWhole)  worstWhole  = r.fuelUsed;
            if (r.wonAtMs   > slowest)     slowest     = r.wonAtMs;
        }
        std::printf("    %d/%d won; fuel to first centring at worst %d of %d; "
                    "over the whole approach %d; slowest centring %d ms\n",
                    bWins, n, worstArrive, SR_FUEL_FULL, worstWhole, slowest);
        check(bWins == n,
              "**with the boost held, EVERY start lands -- the far corners "
              "included**. 8100 units at 16 a frame is 507 frames, about 42 "
              "seconds of a ninety-second clock; at 4 it is 169 seconds and the "
              "scene is over first");
        check(worstArrive < SR_FUEL_FULL / 2,
              "**and GETTING there never costs half the gauge** -- boosting spends "
              "8 fuel a frame and travels 16, so 8000 units buy 16000 of travel "
              "across a board 8100 wide. There is no reason to hoard it");
        check(worstWhole > SR_FUEL_FULL,
              "**but holding it for the whole ninety seconds DOES empty the "
              "gauge** -- the pilot keeps correcting after it arrives, which is "
              "what a real player does, and that is the case the fuel "
              "announcement exists for");
    }

    // ---- the regression this release exists to fix -------------------------
    // v0.62.x spoke once a second but swallowed an unchanged phrase, so a
    // player holding the wrong key heard nothing at all. Aaron's attempt 1:
    // "right and down" at 14:52:59, then fifty-three seconds of silence.
    {
        Result old = Fly(2500, -800, 1000, 1000, /*dedupe=*/true, 0, true);
        std::printf("  v0.62.x dedupe from attempt 1: %s, longest silence %d ms\n",
                    old.won ? "WON" : "LOST", old.silentMaxMs);
        check(old.silentMaxMs > 4000,
              "**the old scheme really did go silent past what Aaron asked for** -- "
              "and this is the optimistic case, with a pilot who reacts to every "
              "call. In the field, holding one arrow of a two-arrow instruction, "
              "he got fifty-three seconds of it");
    }

    // ---- the bands and the normalised error ---------------------------------
    check(strcmp(SrBand(0,   SR_CLAMP_X), "")            == 0, "centred has no band word");
    check(strcmp(SrBand(100, SR_CLAMP_X), "a hair ")     == 0, "100 out still asks for a hair -- the aim box is 60, not 180");
    check(strcmp(SrBand(300, SR_CLAMP_X), "a hair ")     == 0, "300 out is a hair");
    check(strcmp(SrBand(800, SR_CLAMP_X), "just ")       == 0, "800 out is just");
    check(strcmp(SrBand(2000,SR_CLAMP_X), "")            == 0, "2000 out is a plain direction");
    check(strcmp(SrBand(8000,SR_CLAMP_X), "a long way ") == 0, "8000 out is a long way");
    check(SrRadarT(0, 0, SR_CLAMP_X, SR_CLAMP_Y) == 1000, "dead centre is 1000");
    check(SrRadarT(SR_CLAMP_X, 0, SR_CLAMP_X, SR_CLAMP_Y) == 0, "the edge is 0");
    check(SrRadarT(4050, 0, SR_CLAMP_X, SR_CLAMP_Y) == 500, "half out is half way");
    // The worse axis drives it: a big Y error must not be masked by a small X.
    check(SrRadarT(0, 7300, SR_CLAMP_X, SR_CLAMP_Y) == 0,
          "**the worse axis drives the measure** -- the verdict fails on either one");
    check(SrTrend(1000, 800) == 1 && SrTrend(800, 1000) == -1 && SrTrend(800, 799) == 0,
          "the trend needs more than one step of change to count");

    // ---- the call itself ---------------------------------------------------
    char b[64];
    SrCall(0, 0, b, sizeof b);        check(strcmp(b, "centred") == 0, "inside the box: centred");
    SrCall(2500, -800, b, sizeof b);  check(strcmp(b, "well right and down") == 0,
                                            "Aaron's attempt 1 reads well right and down");
    SrCall(-300, 0, b, sizeof b);     check(strcmp(b, "a hair left") == 0, "a hair left");
    SrCall(0, 4000, b, sizeof b);     check(strcmp(b, "well up") == 0, "well up");
    SrCall(0, 6000, b, sizeof b);     check(strcmp(b, "a long way up") == 0, "a long way up");
    check(SrMaskForCall("a hair right and down") == (SR_MASK_RIGHT | SR_MASK_DOWN),
          "the pilot reads both directions back out of the phrase");
    check(SrMaskForCall("centred") == 0, "and centred means hands off");

    std::printf("space_sim_test: %s (%d bad)\n", bad ? "*** FAIL ***" : "OK", bad);
    return bad ? 1 : 0;
}
