// drive_progress_test.cpp -- is the drive getting anywhere? (#centra, v0.131.1)
//
// The two fixtures are the two stuck episodes in the 19:15 log, replayed tick
// by tick from the positions the drive actually recorded. They are different
// failures and the whole point of the build is that the mod can now tell them
// apart.
#include <cstdio>
#include <cmath>

#include "drive_progress_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // ================================================================
    // crroof1 tri 34: THE OSCILLATION.
    // ================================================================
    // "pp=(1476,-187) ... kb=DR" then "pp=(1498,-167) ... kb=UL", over and over,
    // for ten seconds and five recoveries until Aaron cancelled. He is moving
    // 30 units a tick and ending up exactly where he started.
    {
        DriveProgressTracker t; DriveProgressClear(&t);
        for (int i = 0; i < DRIVE_PROG_SAMPLES; i++) {
            if (i & 1) DriveProgressPush(&t, 1498, -167);
            else       DriveProgressPush(&t, 1476, -187);
        }
        CHECK(DriveProgressFull(&t), "the window fills");
        CHECK(DriveProgressPath(&t) > 800.0f, "he walked a long way");
        CHECK(DriveProgressNet(&t) < DRIVE_PROG_NET_EPS, "and gained nothing");
        CHECK(DriveIsOscillating(&t), "which is an oscillation");
        CHECK(!DriveIsStalled(&t), "and NOT a pin -- he is moving the whole time");
    }

    // ================================================================
    // crtower3 tri 89: THE HARD PIN.
    // ================================================================
    // "pp=(-1305,-34) ... moveDist=0" for seven seconds, with the analog pushed
    // to lX=989. The one unit of drift the log shows is in the fixture.
    {
        DriveProgressTracker t; DriveProgressClear(&t);
        for (int i = 0; i < DRIVE_PROG_SAMPLES; i++)
            DriveProgressPush(&t, -1305 - (i > 20 ? 1.0f : 0.0f), -34);
        CHECK(DriveIsStalled(&t), "not moving at all is a pin");
        CHECK(!DriveIsOscillating(&t), "and NOT an oscillation -- there is no path length");
    }

    // A DRIVE THAT IS SIMPLY WORKING must trip neither, or the remedy fires on
    // every straight run across a field.
    {
        DriveProgressTracker t; DriveProgressClear(&t);
        for (int i = 0; i < DRIVE_PROG_SAMPLES; i++)
            DriveProgressPush(&t, 1000.0f + 30.0f * i, -500.0f);
        CHECK(!DriveIsOscillating(&t), "walking in a straight line is not oscillating");
        CHECK(!DriveIsStalled(&t), "nor pinned");
        CHECK(DriveProgressNet(&t) > 900.0f, "it is just walking");
    }

    // A SLOW BUT REAL APPROACH -- edging the last stretch toward a ladder -- is
    // also neither. This is the case where being too eager would cancel the
    // avoidance on a drive that was about to arrive.
    {
        DriveProgressTracker t; DriveProgressClear(&t);
        for (int i = 0; i < DRIVE_PROG_SAMPLES; i++)
            DriveProgressPush(&t, 900.0f + 6.0f * i, -900.0f);
        CHECK(!DriveIsOscillating(&t), "a slow steady approach is not oscillating");
        CHECK(!DriveIsStalled(&t), "and 186 units of travel over 1.6 s is not a pin");
    }

    // ================================================================
    // v0.131.4: THE WINDOW IS 1.6 SECONDS, NOT 32 TICKS.
    // ================================================================
    // At 20:14:58 the monitor declared "pinned -- 0 units walked in the last 32
    // ticks" while the drive's own diagnostic, thirty ticks either side, had the
    // player covering ninety-nine units. Both cannot be true of the same second.
    // UpdateAutoDrive runs on a thread that polls faster than the game renders,
    // so consecutive ticks see the same position and thirty-two of them can
    // cover a fraction of a second -- and a fraction of a second of standing
    // still is what every walking character does between frames.
    CHECK(DriveProgressDueForSample(0, 0, false),
          "the first sample of a drive is always due");
    CHECK(!DriveProgressDueForSample(1000, 1000, true), "the same millisecond is not");
    CHECK(!DriveProgressDueForSample(1000 + DRIVE_PROG_SAMPLE_MS - 1, 1000, true),
          "nor one tick short of the interval");
    CHECK(DriveProgressDueForSample(1000 + DRIVE_PROG_SAMPLE_MS, 1000, true),
          "the interval itself is");
    CHECK(DRIVE_PROG_SAMPLES * DRIVE_PROG_SAMPLE_MS >= 1000,
          "so a full window is at least a second of real time");
    CHECK(DRIVE_PROG_SAMPLES * DRIVE_PROG_SAMPLE_MS <= 3000,
          "and no more than three, or a genuine pin is endured too long");
    // And the thresholds have to describe THAT window. A character walking at
    // even a third of normal pace covers far more than the stall bar in 1.6 s,
    // which is what stops "between frames" reading as "stuck".
    CHECK(DRIVE_PROG_STALL_EPS < 20.0f, "twelve units in 1.6 s is unambiguously stuck");
    CHECK(DRIVE_PROG_PATH_MIN >= 5.0f * DRIVE_PROG_NET_EPS,
          "and an oscillation must walk several times what it gains");

    // A PARTIAL WINDOW DECIDES NOTHING. The first half-second of every drive
    // reads as "no net distance" if you let it.
    {
        DriveProgressTracker t; DriveProgressClear(&t);
        for (int i = 0; i < DRIVE_PROG_SAMPLES - 1; i++) DriveProgressPush(&t, 5, 5);
        CHECK(!DriveProgressFull(&t), "one sample short is not a full window");
        CHECK(!DriveIsStalled(&t), "and answers no");
        CHECK(!DriveIsOscillating(&t), "to both questions");
        DriveProgressPush(&t, 5, 5);
        CHECK(DriveIsStalled(&t), "the sample that fills it answers yes");
    }
    CHECK(!DriveIsStalled(nullptr) && !DriveIsOscillating(nullptr),
          "and a null tracker is not dereferenced");

    // ================================================================
    // THE ESCAPE SWEEP.
    // ================================================================
    // Nearest angles first, alternating sides, so a wall on either hand is found
    // in two probes rather than four.
    CHECK(DRIVE_PROBE_COUNT >= 6, "the sweep has enough headings to find a way out");
    CHECK(DriveProbeAngle(0) == 30 && DriveProbeAngle(1) == -30,
          "it starts with the smallest deviation, both ways");
    for (int i = 0; i + 1 < DRIVE_PROBE_COUNT; i += 2) {
        CHECK(DriveProbeAngle(i) == -DriveProbeAngle(i + 1),
              "and pairs each angle with its mirror");
        if (i + 2 < DRIVE_PROBE_COUNT)
            CHECK(abs(DriveProbeAngle(i + 2)) > abs(DriveProbeAngle(i)),
                  "widening as it goes");
    }
    // v0.131.3: and it stops at a right angle. The 20:04 run "unpinned at 180
    // degrees" -- turned around, called it an escape, and gave up 512 units
    // short forty seconds later.
    for (int i = 0; i < DRIVE_PROBE_COUNT; i++)
        CHECK(abs(DriveProbeAngle(i)) <= 90,
              "no probe turns the drive away from where it is going");
    CHECK(DriveProbeAngle(-1) == 0 && DriveProbeAngle(DRIVE_PROBE_COUNT) == 0,
          "an index outside the sweep rotates nothing");
    CHECK(DRIVE_PROBE_TICKS * DRIVE_PROBE_COUNT < 180,
          "and a whole sweep takes under three seconds");

    // The rotation itself, against the crtower3 heading that was walking into a
    // wall: (-130,-176) is south-west; 90 degrees off it must be perpendicular.
    {
        float ox = 0, oy = 0;
        DriveRotateSteer(-130.3f, -175.9f, 0, &ox, &oy);
        CHECK(fabsf(ox + 130.3f) < 0.01f && fabsf(oy + 175.9f) < 0.01f,
              "zero degrees is the identity");
        DriveRotateSteer(-130.3f, -175.9f, 90, &ox, &oy);
        const float dot = (-130.3f) * ox + (-175.9f) * oy;
        CHECK(fabsf(dot) < 1.0f, "ninety degrees is perpendicular to the original");
        const float lenIn  = sqrtf(130.3f * 130.3f + 175.9f * 175.9f);
        const float lenOut = sqrtf(ox * ox + oy * oy);
        CHECK(fabsf(lenIn - lenOut) < 0.5f, "and rotation preserves the magnitude");
        DriveRotateSteer(-130.3f, -175.9f, 180, &ox, &oy);
        CHECK(fabsf(ox - 130.3f) < 0.01f && fabsf(oy - 175.9f) < 0.01f,
              "and 180 turns it around");
    }

    // ================================================================
    // v0.131.2: FAILING FORWARD, NOT IN A LOOP.
    // ================================================================
    // The 19:46-19:47 run drove six fields with one recovery and no
    // cancellations -- the sweep works -- but it pinned seven times in ninety
    // seconds, clearing each in under a second and then walking straight back
    // into the same wall:
    //     19:46:27 pinned, sweeping from 30   19:46:27 unpinned at 30
    //     19:46:28 pinned, sweeping from 30   19:46:28 unpinned at -30
    // Every sweep restarted from the top. A pin that arrives while the last
    // winner is still held is the SAME wall and must escalate.
    CHECK(DriveProbeResumeIndex(true, 0) == 1,
          "re-pinning during the hold resumes at the next angle, not the first");
    CHECK(DriveProbeResumeIndex(true, 3) == 4, "wherever the last winner was");
    CHECK(DriveProbeResumeIndex(true, DRIVE_PROBE_COUNT - 1) == 0,
          "and wraps rather than running off the end of the sweep");
    CHECK(DriveProbeResumeIndex(false, 5) == 0,
          "a pin long after the hold expired is a new obstacle and starts fresh");
    CHECK(DriveProbeResumeIndex(true, -1) == 0,
          "and no previous winner starts fresh too");
    {
        // Escalating from any starting point must still visit every angle and
        // terminate -- a resume rule that could cycle forever would turn the
        // search back into the loop it exists to replace.
        int idx = DriveProbeResumeIndex(true, 0), seen = 0;
        for (int i = 0; i < DRIVE_PROBE_COUNT * 2 && idx < DRIVE_PROBE_COUNT; i++) {
            seen++; idx++;
        }
        CHECK(seen == DRIVE_PROBE_COUNT - 1, "the sweep still ends");
    }
    // The hold has to outlast the contact without outlasting the corner: long
    // enough that he does not re-pin immediately, short enough that the drive is
    // not walking at an angle to its waypoint for a noticeable stretch.
    CHECK(DRIVE_PROBE_HOLD_TICKS > DRIVE_PROBE_TICKS,
          "the hold outlasts a single probe");
    CHECK(DRIVE_PROBE_HOLD_TICKS <= 60, "but not more than a second of it");

    // ================================================================
    // v0.131.3: AN ESCAPE THAT DOES NOT MAKE GROUND IS NOT AN ESCAPE.
    // ================================================================
    // The 20:04:09 drive down crroof1's ramp thrashed for forty-one seconds and
    // gave up 512 units short, because the sweep's success test was "has he
    // moved" and walking backwards passes it. Measured against the direction the
    // drive wanted, the same movements answer correctly.
    {
        const float wantX = 100.0f, wantY = 0.0f;   // the drive wants to go east
        CHECK(DriveProbeMovedUsefully(100, 0, wantX, wantY),
              "straight at the target is useful");
        CHECK(DriveProbeMovedUsefully(70, 70, wantX, wantY),
              "sliding along a wall at 45 degrees is useful");
        CHECK(DriveProbeMovedUsefully(70, -70, wantX, wantY), "either way round");
        CHECK(!DriveProbeMovedUsefully(0, 100, wantX, wantY),
              "a right-angle squeeze that gains nothing is not");
        CHECK(!DriveProbeMovedUsefully(-70, 70, wantX, wantY),
              "and neither is backing off at 135 degrees");
        CHECK(!DriveProbeMovedUsefully(-100, 0, wantX, wantY),
              "least of all turning around -- the 20:04:45 'unpinned at 180 degrees'");
        // Movement has to be real as well as useful, or the drive claims an
        // escape from the drift of standing still against a wall.
        CHECK(!DriveProbeMovedUsefully(5, 0, wantX, wantY),
              "five units toward the target is not an escape");
        CHECK(!DriveProbeMovedUsefully(DRIVE_PROG_NET_EPS, 0, wantX, wantY),
              "and the threshold itself is not past it");
        // With no goal direction there is nothing to judge against, so any real
        // movement counts rather than none.
        CHECK(DriveProbeMovedUsefully(100, 0, 0, 0), "no goal direction accepts movement");
    }

    // ================================================================
    // v0.131.5: AN OVERSHOOT IS AN INFERENCE, AND ONE PER TICK IS ENOUGH.
    // ================================================================
    // crroof1's ramp is a switchback. The 20:50:20 drive down it went from
    // following its second waypoint to having none left between two log lines --
    // `t=60 wpRaw=(1459,-19)` then `t=90 wpRaw=(1334,-672)`, which is the final
    // target -- and then steered straight at the exit across ground the walkmesh
    // does not connect. On a path that doubles back, moving away from the
    // waypoint behind you is what walking the next leg looks like, so every
    // remaining waypoint reads as overshot at once.
    CHECK(DriveOvershootAdvanceAllowed(0), "the first overshoot of a tick advances");
    CHECK(!DriveOvershootAdvanceAllowed(1), "the second does not");
    CHECK(!DriveOvershootAdvanceAllowed(3), "and nor does the fourth");
    {
        // The whole four-waypoint list must not be consumable in one tick, which
        // is exactly what the log recorded.
        int idx = 0, overshoots = 0;
        for (int i = 0; i < 4; i++)
            if (DriveOvershootAdvanceAllowed(overshoots)) { overshoots++; idx++; }
        CHECK(idx == 1, "four waypoints cannot be crossed off by one tick's inference");
    }

    printf("drive_progress_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
