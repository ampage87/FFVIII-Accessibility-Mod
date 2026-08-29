// ladder_cue_test.cpp -- the ladder climb cue (#centra, v0.123.0).
//
// Every assertion was written against a mutant. The opcode-to-mode table is
// read out of FF8_EN.exe, not guessed, and the test says so by pinning it.
#include <cstdio>
#include <cmath>

#include "ladder_cue_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // THE THREE MODES A LADDER OPCODE LEAVES BEHIND. v0.131.8 removed the
    // opcode-to-mode table from the model -- since v0.130.0 nothing dispatches
    // on the opcode, so it is written down as evidence rather than compiled --
    // but the modes it sets are still what the cue watches, and they are still
    // pinned here. 0x024 (crtower3's `laddw0`, going down) sets 2; 0x025 and
    // 0x026 (crroof1's silent roof ladder) set 3; 0x027 (crtower1's
    // `p0_ladup0`, which Aaron says makes a noise) sets 4.
    CHECK(LadderModeIsClimb(2), "mode 2 -- what the way down sets -- is a climb");
    CHECK(LadderModeIsClimb(3), "mode 3 -- the roof ladder -- is a climb");
    CHECK(LadderModeIsClimb(4), "mode 4 is a climb");
    CHECK(!LadderModeIsClimb(0) && !LadderModeIsClimb(1),
          "standing and walking are not, so ordinary steps are left to the engine");

    // The step routine is no longer CALLED -- v0.131.0 moved the cue onto the
    // leaf sound player -- but it is still hooked to measure the game's own
    // cadence, and the address and prologue are still what that hook verifies.
    CHECK(LADDER_STEP_ADDR == 0x00520260u, "the routine's address");
    CHECK(LADDER_STEP_SIG_LEN == 15, "fifteen bytes of prologue are checked");
    CHECK(LADDER_STEP_SIG[0] == 0x83 && LADDER_STEP_SIG[1] == 0xEC &&
          LADDER_STEP_SIG[2] == 0x10, "starting with its stack frame");

    // ================================================================
    // v0.125.0: THE CADENCE IS LEARNED FROM THE GAME, NOT PICKED BY US.
    // ================================================================
    // Aaron: "the climbing sounds were very fast compared to the way they
    // usually sound on a ladder." The old constant was 260 ms and it was a
    // guess; the fix is that no number in this file is a guess any more.
    CHECK(LADDER_STEP_MS_DEFAULT > 260 * 1.5,
          "the default is no longer the 260 ms Aaron heard as very fast");
    // v0.126.0: and it is no longer the 700 ms placeholder either. The hook
    // measured fourteen consecutive gaps on crtower on 2026-08-28 and eleven of
    // them were 468-469 ms, so THAT is the starting value -- pinned here so the
    // next person to change it has to argue with the measurement.
    CHECK(LADDER_STEP_MS_DEFAULT == 469,
          "the default is the 469 ms the engine was measured at");
    CHECK(LadderGapIsSample(LADDER_STEP_MS_DEFAULT),
          "the default is itself a plausible sample, not an out-of-range placeholder");
    CHECK(LADDER_STEP_MS_MIN < LADDER_STEP_MS_DEFAULT &&
          LADDER_STEP_MS_DEFAULT < LADDER_STEP_MS_MAX,
          "and it sits inside the range learning is allowed to reach");
    CHECK(LadderClampInterval(10) == LADDER_STEP_MS_MIN, "clamped up from absurdly fast");
    CHECK(LadderClampInterval(99999) == LADDER_STEP_MS_MAX, "clamped down from a pause");
    CHECK(LadderClampInterval(600) == 600, "a plausible interval passes through");

    // A GAP ONLY COUNTS IF IT COULD BE A STRIDE. The engine plays two feet, and
    // when both cross on the same frame the gap is a handful of milliseconds --
    // learning from that would drive the cue back to a machine-gun rattle,
    // which is exactly the bug being fixed.
    CHECK(!LadderGapIsSample(8), "two feet on one frame is not a cadence sample");
    CHECK(!LadderGapIsSample(5000), "nor is a five-second pause between ladders");
    CHECK(LadderGapIsSample(LADDER_STEP_MS_MIN), "the boundary values are samples");
    CHECK(LadderGapIsSample(LADDER_STEP_MS_MAX), "at both ends");
    CHECK(LadderGapIsSample(650), "and an ordinary climbing gap is one");

    // ================================================================
    // v0.127.0: A GAP THE ENGINE SKIPPED IS NOT A SLOWER LADDER.
    // ================================================================
    // Folding, first. 953 and 1359 ms are the engine missing one and two
    // strides at ~469 ms, not the animation changing speed.
    CHECK(LadderFoldGap(469, 469) == 469, "a gap at the believed pace is itself");
    CHECK(LadderFoldGap(953, 469) == 476, "a doubled gap folds in half");
    CHECK(LadderFoldGap(1359, 452) == 453, "a tripled one folds in three");
    CHECK(LadderFoldGap(1344, 444) == 448, "and so does the other one in the log");
    // 672 IS THE ONE THAT MATTERS. It is the gap that must be REJECTED, and at a
    // +/-35% band its halving (336) squeaks in -- a fold that finds some
    // quotient in range for every gap is a licence to believe anything.
    CHECK(LadderFoldGap(672, 452) == 0,
          "672 ms folds to nothing plausible and is dropped, not halved");
    CHECK(LadderFoldGap(900, 600) == 0, "and so does any gap around 1.5x the pace");
    CHECK(LadderFoldGap(8, 469) == 0, "two feet on one frame folds to nothing");
    CHECK(LadderFoldGap(9000, 469) == 0, "and neither does a nine-second pause");
    CHECK(LadderFoldGap(469, 0) == 0, "a zero belief folds nothing");
    // Four times the pace is beyond what the fold will reach, on purpose: past
    // three skipped strides the gap is a pause, not a rhythm.
    CHECK(LadderFoldGap(469 * 4, 469) == 0, "a quadrupled gap is a pause, not a stride");

    // THE MEAN IS WEIGHTED TWO-TO-ONE toward what is already believed, and
    // v0.125.0's "first sample outright" rule is gone: since v0.126.0 the
    // starting value IS a measurement, so there is no guess to escape.
    CHECK(LadderLearnInterval(600, 700) == 633,
          "samples are weighted two-to-one toward what we already measured");
    // A gap around 1.5x the pace reads equally well as one slow stride or two
    // fast ones. Both readings fall outside the band, so it teaches nothing.
    CHECK(LadderLearnInterval(600, 900) == 600,
          "an ambiguous 1.5x gap is dropped rather than split the difference");
    CHECK(LadderLearnInterval(LADDER_STEP_MS_DEFAULT, 12) == LADDER_STEP_MS_DEFAULT,
          "a gap that folds to nothing changes nothing");
    CHECK(LadderLearnInterval(469, 953) == 471,
          "a doubled gap teaches the pace it doubled, not the double");
    {
        // Ten identical samples converge on the truth rather than drifting.
        unsigned iv = LADDER_STEP_MS_DEFAULT;
        for (int i = 0; i < 30; i++) iv = LadderLearnInterval(iv, 520);
        CHECK(iv >= 515 && iv <= 520, "a steady rhythm is converged on");
    }
    {
        // THE REGRESSION, REPLAYED. These are the nineteen gaps the hook
        // recorded on crtower3 and crroof1 between 16:39 and 16:54, in order.
        // v0.126.0's running mean reached 970 ms on them and handed 586 ms to
        // the silent roof ladder -- a quarter slower than the ladder it was
        // imitating. Folded, the same nineteen never leave the pace Aaron
        // called normal.
        static const unsigned RUN[] = { 469, 390, 469, 469, 406, 500, 953, 375,
                                        438, 531, 422, 453, 672, 407, 1359, 1344,
                                        422, 406, 438 };
        unsigned iv = LADDER_STEP_MS_DEFAULT, worst = 0;
        for (unsigned i = 0; i < sizeof(RUN) / sizeof(RUN[0]); i++) {
            iv = LadderLearnInterval(iv, RUN[i]);
            if (LadderAbsDiff(iv, LADDER_STEP_MS_DEFAULT) >
                LadderAbsDiff(worst, LADDER_STEP_MS_DEFAULT)) worst = iv;
        }
        CHECK(worst <= 560, "the run's own gaps never drag the cadence to 586 ms again");
        CHECK(iv >= 380 && iv <= 520, "and it ends where the game actually is");
    }

    // THE MOD STAYS QUIET WHILE THE ENGINE IS SOUNDING THE LADDER. This is the
    // second bug v0.125.0 closes: v0.124.0 stepped on EVERY ladder, doubling
    // the ones crtower3 and crtower1 already sound. Aaron's Centra log has only
    // the roof ladder in it, so he has not heard this yet.
    CHECK(!LadderEngineIsStepping(false, 10000, 0, 700),
          "an engine that has not stepped on this climb suppresses nothing");
    CHECK(LadderEngineIsStepping(true, 10000, 9800, 700),
          "a step 200 ms ago means the game is sounding this ladder itself");
    // v0.127.0 widened the slack from two intervals to four. At two, the 1359 ms
    // hole in crtower3's own rhythm at 16:51:22 let the mod cut in -- the log
    // says "2 mod steps, engine sounded it itself" on a ladder the mod was
    // supposed to leave alone.
    CHECK(LadderEngineIsStepping(true, 10000, 10000 - 1359, 452),
          "the longest hole the engine has ever left does NOT let the mod cut in");
    CHECK(LadderEngineIsStepping(true, 10000, 10000 + 1 - 700 * 4, 700),
          "slack runs to four intervals");
    CHECK(!LadderEngineIsStepping(true, 10000, 10000 - 700 * 4, 700),
          "and past that a real dropout hands the ladder back to the mod");

    // THE STEP SCHEDULE. A climb waits out the mount animation before its first
    // step -- which is also what keeps the mod from landing a step on top of the
    // engine's opening one on a ladder that turns out to be sounded.
    CHECK(!LadderStepDue(false, 10000, 9000, 0, false, 700),
          "not climbing, no step");
    CHECK(!LadderStepDue(true, 9000 + LADDER_STEP_GRACE_MS - 1, 9000, 0, false, 700),
          "the first step waits out the grace period");
    CHECK(LadderStepDue(true, 9000 + LADDER_STEP_GRACE_MS, 9000, 0, false, 700),
          "and arrives the moment it expires");
    CHECK(!LadderStepDue(true, 10699, 9000, 10000, true, 700),
          "later steps hold for the learned interval");
    CHECK(LadderStepDue(true, 10700, 9000, 10000, true, 700),
          "and land on it");
    // The interval is a parameter now, so a learned value actually reaches the
    // schedule. A mutant that kept using a constant would pass every test above.
    CHECK(!LadderStepDue(true, 10700, 9000, 10000, true, 1100),
          "a slower learned cadence really does space the steps further apart");
    CHECK(LadderStepDue(true, 11100, 9000, 10000, true, 1100), "landing later");

    // The 260 ms regression, stated as a scenario rather than a constant: a
    // five-second climb at the default must not produce anything like the
    // eighteen steps Aaron heard.
    {
        int steps = 0; unsigned last = 0; bool ever = false;
        for (unsigned t = 0; t <= 5000; t += 16) {
            if (LadderStepDue(true, t, 0, last, ever, LADDER_STEP_MS_DEFAULT)) {
                steps++; last = t; ever = true;
            }
        }
        // v0.126.0: at 469 ms after a 400 ms mount this is ten steps, which is
        // exactly what crroof1's silent ladder logged on the run Aaron called
        // the normal pace. Eighteen was the sound of the guess.
        CHECK(steps == 10, "a five-second climb is ten steps, as crroof1 logged");
        CHECK(steps <= 8 + 2, "a five-second climb is no longer eighteen steps");
        CHECK(steps >= 4, "but it is still audibly a climb");
    }

    // ================================================================
    // v0.131.8: WHAT THE MOD SOUNDS IS WIDER THAN WHAT THE ENGINE DOES.
    // ================================================================
    // sub_00520260 branches to the ladder sound on mode 3 and mode 4 only;
    // mode 2 -- which crtower3's ladder going DOWN sets -- falls through to the
    // terrain footstep table, which is the whole of why Aaron heard the way up
    // and not the way down.
    //
    // v0.126.0 answered that by presenting the byte as 3 across the call, and
    // v0.131.0 withdrew it: that meant writing the player's entity from the
    // mod's thread, and the game caught it half-written and stranded him. The
    // cue now plays sound 0x3F itself, so mode 2 is sounded WITHOUT the engine
    // agreeing that it is a ladder. That is the property to pin -- a mutant
    // that narrowed LadderModeIsClimb back to the engine's own 3-and-4 would
    // silence the descent again and pass every other test in this file.
    CHECK(LadderModeIsClimb(2), "mode 2 is a climb the mod sounds");
    CHECK(LADDER_SOUND_ID == 0x3F, "with the id read out of the branch at 0x005203BB");
    CHECK(LADDER_SOUND_PLAY_ADDR == 0x0046B2A0u, "played through the leaf sound routine");
    CHECK(LADDER_SOUND_PAN == 0x80, "centre pan, as all four engine call sites pass");
    CHECK(LADDER_SOUND_VOL == 0x77,
          "and never louder than the engine's own attenuation can produce");

    // ================================================================
    // v0.128.0: THE DESCENT IS NOT A LADDER MOVE, SO IT IS NOT FOUND
    //           BY LOOKING FOR ONE.
    // ================================================================
    // crroof1's party method 5 -- what "Ladder Down" runs -- plays the climbing
    // animation in place, waits, and SET3s the character to (923,-896,18403).
    // No ladder opcode, so no movement mode, so no engine step and, until this
    // build, no mod step either. What IS true for exactly the length of that
    // move is the script's own PREQEW wait -- see v0.130.0 below.

    // THE NAME GATE. Control is locked for every cutscene and every door in the
    // game; the cue arms only over a line the field calls a ladder.
    CHECK(LadderNameIsLadder("Ladder Down"), "the descent line is a ladder");
    CHECK(LadderNameIsLadder("Ladder Up"), "so is the ascent line");
    CHECK(LadderNameIsLadder("Left Ladder Up"), "and one with a qualifier in front");
    CHECK(LadderNameIsLadder("LADDER DOWN"), "case does not matter");
    CHECK(!LadderNameIsLadder("Eye Statue, not active"), "a statue is not");
    CHECK(!LadderNameIsLadder("Exit to Centra Ruins 10"), "nor is an exit");
    CHECK(!LadderNameIsLadder("Draw Point"), "nor a draw point");
    CHECK(!LadderNameIsLadder(""), "an empty name is not a ladder");
    CHECK(!LadderNameIsLadder(0), "and a null one is not dereferenced");

    // ================================================================
    // v0.129.0: THE NAME THE GATE READS HAS TO BE ONE THAT EXISTS.
    // ================================================================
    // v0.128.0 read s_capturedLines[].name, which the SETLINE hook sets to '\0'
    // and nothing ever fills. The gate was asking about an empty string on every
    // ladder in the game -- and an empty string is not a ladder, which the name
    // test above already says. So the identity now comes from the catalog, where
    // a trigger line is entityIdx = -200 - slot.
    {
        int slot = -1;
        CHECK(LadderCatalogIsTriggerLine(-201, 8, &slot) && slot == 1,
              "ent-201 is captured line 1 -- crroof1's 'Ladder Down'");
        CHECK(LadderCatalogIsTriggerLine(-200, 8, &slot) && slot == 0,
              "ent-200 is line 0, 'Ladder Up'");
        CHECK(!LadderCatalogIsTriggerLine(3, 8, &slot), "a real entity index is not a line");
        CHECK(!LadderCatalogIsTriggerLine(-1, 8, &slot), "nor is a gateway sentinel");
        CHECK(!LadderCatalogIsTriggerLine(-400, 8, &slot), "nor a JSM one");
        CHECK(!LadderCatalogIsTriggerLine(-300, 8, &slot), "the range is exclusive at -300");
        CHECK(!LadderCatalogIsTriggerLine(-209, 8, &slot),
              "and a slot past the captured lines is refused, not indexed");
    }

    // ================================================================
    // v0.130.0: ASK THE SCRIPT, NOT THE SYMPTOMS.
    // ================================================================
    // v0.129.0 armed on the button the line waits on, and the 18:15-18:21 run
    // armed three times with no climb behind any of them -- `grep -c "engine
    // movement mode"` over that log returns 0. The drive had parked the player
    // at t=1.02, fractionally past the line's end, so the press did nothing and
    // sixteen ladder steps played over eight seconds while NAV-OBSERVE recorded
    // him walking around. PREQEW is a WAIT: the interpreter calls it again every
    // frame the move is still running and stops the frame it ends, so the cue
    // needs no duration, no backstop, and cannot fire on a press that did
    // nothing at all.
    CHECK(!LadderPreqewIsRecent(1000, 0, false),
          "a heartbeat that has never been stamped is not running");
    CHECK(LadderPreqewIsRecent(1000, 1000, true), "the frame it is stamped, it is running");
    CHECK(LadderPreqewIsRecent(1000, 1000 - LADDER_PREQEW_HOLD_MS, true),
          "and it survives the hold, so a dropped frame does not stutter the cue");
    CHECK(!LadderPreqewIsRecent(1000, 1000 - LADDER_PREQEW_HOLD_MS - 1, true),
          "past that the script has stopped waiting and the move is over");
    // The hold is slack for dropped frames, NOT a duration: the cue must never
    // outlive the script by anything a listener would notice, which is the whole
    // difference between this and v0.129.0's eight-second backstop.
    CHECK(LADDER_PREQEW_HOLD_MS <= 300,
          "the hold is frames of slack, not a guessed duration");
    CHECK(LADDER_PREQEW_SLOTS >= 2,
          "and more than one wait can be in flight at a time");

    printf("ladder_cue_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
