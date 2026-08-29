// rag_ground_test.cpp -- the three rules the 12:37 BAT bought.
#include <cstdio>
#include "rag_ground_pure.inl"

static int bad = 0;
static void chk(bool ok, const char* what)
{ if (!ok) { printf("  BAD: %s\n", what); bad++; } }

int main()
{
    printf("rag_ground_test\n");

    // --- 1. the Ragnarok is never wedged -----------------------------------
    chk(RagWedgeAllowed(true) == false,
        "**the un-wedge burst still fires for the Ragnarok** -- 0x53E6B0 falls "
        "through to return 1 for vehicle 0x32, so no polygon is closed to it and "
        "it cannot have driven into anything; the 12:37 BAT fired thirteen bursts "
        "at a ship that was never stuck");
    chk(RagWedgeAllowed(false) == true,
        "**on foot and in the car the un-wedge burst is gone too** -- those are "
        "vehicles that genuinely wedge, and #67 spent many builds tuning this");

    // --- 2. progress, not motion, refunds the turn budget -------------------
    // The BAT's own numbers: the distance went 26502 -> 26266 -> 26028 while the
    // ship was shoved back and forth, so it MOVED hundreds of units per window.
    chk(RagDriveMadeProgress(26266.0, 26502.0, 100.0) == true,
        "236 units closer in a window does not count as progress");
    chk(RagDriveMadeProgress(26502.0, 26502.0, 100.0) == false,
        "**standing still at the same distance counts as progress** -- that is the "
        "first two bursts of the BAT, dist unchanged at 26502");
    chk(RagDriveMadeProgress(24445.0, 24108.0, 100.0) == false,
        "**moving AWAY from the target counts as progress** -- the BAT went 24108 "
        "then 24445 then 24504; a refund there is what made the bound meaningless");
    chk(RagDriveMadeProgress(26480.0, 26502.0, 100.0) == false,
        "22 units of drift counts as progress against a 100-unit floor");
    chk(RagDriveMadeProgress(500.0, -1.0, 100.0) == true,
        "**the first check of a drive is punished** -- there is no previous reading "
        "to compare against, and starting a drive already one strike down is wrong");

    // --- 3. aboard is not airborne -----------------------------------------
    // 12:37:08, on the ground: char Z = -544, engine ground height = -544.
    chk(RagHeightState(true, true, -544, -544) == RAG_ON_GROUND,
        "**altitude equal to the ground does not read as ON THE GROUND** -- that is "
        "the slot dump and the engine's own height agreeing exactly");
    chk(RagHeightState(true, true, -543, -544) == RAG_ON_GROUND, "a 1-unit gap is not on the ground");
    // The boundary is the engine's, and it belongs on the engine's side: the
    // set-down predicate at 0x54B860 refuses at gap >= 200, so a gap of 199 is
    // still close enough to the ground for the game to put the ship down, and
    // this must agree with it rather than round the other way.
    chk(RagHeightState(true, true, -743, -544) == RAG_ON_GROUND,
        "**a 199-unit gap reads as airborne** -- the engine would still set the ship "
        "down at that height, so calling it flight disagrees with the game");
    chk(RagHeightState(true, true, -744, -544) == RAG_AIRBORNE,
        "**a 200-unit gap reads as on the ground** -- that is the exact height the "
        "engine starts refusing to set down at");
    chk(RagHeightState(true, true, -5000, -544) == RAG_AIRBORNE, "cruising is not airborne");
    // sign-independent: world-map heights are negative upward, so never assume
    // which side is bigger.
    chk(RagHeightState(true, true, -344, -544) == RAG_AIRBORNE &&
        RagHeightState(true, true, -544, -344) == RAG_AIRBORNE,
        "**the gap is measured signed** -- it is a distance, and up is negative here");

    chk(RagHeightState(false, true, -5000, -544) == RAG_HEIGHT_UNKNOWN,
        "the height state is claimed when not aboard the Ragnarok");
    chk(RagHeightState(true, false, -5000, -544) == RAG_HEIGHT_UNKNOWN,
        "**an unreadable altitude is reported as a real state** -- guessing here "
        "would ground a working feature on a number nobody has validated in flight");

    // --- 4. and only a POSITIVE measurement refuses a drive -----------------
    chk(RagDriveMayStart(RAG_ON_GROUND) == false,
        "**a drive starts with the ship on the ground** -- 46 times slower than "
        "flight, and every landing row assumes a ship that flies");
    chk(RagDriveMayStart(RAG_AIRBORNE) == true, "an airborne drive is refused");
    chk(RagDriveMayStart(RAG_HEIGHT_UNKNOWN) == true,
        "**an unreadable altitude refuses the drive** -- unknown must behave exactly "
        "as v0.83.0 did, or a bad read takes auto-drive away entirely");

    // --- v0.96.0: when the ground is not ground -----------------------------
    //
    // The 19:38 BAT ended with the ship PARKED ON THE FISHERMAN'S HORIZON PAD --
    // the screenshot shows it on the platform, bridge either side, ocean all
    // round -- and the check read "shipZ=-200 groundH=0 gap=200 -> AIRBORNE".
    // 0 is the engine's NO GROUND value: the pad is a man-made platform over
    // water, so there is no terrain height to compare against.
    chk(RagGroundReadable(0) == false,
        "**zero is treated as a real ground height** -- it is the engine's NO "
        "GROUND value, and the 19:38 stall measured a parked ship against it");
    chk(RagGroundReadable(-91) == true && RagGroundReadable(-1349) == true,
        "a real negative ground height reads as unreadable");

    chk(RagGroundHeight(-115, -91, true) == -115,
        "**our own reader overrides a ground the engine HAS** -- the engine is the "
        "authority wherever it has an answer");
    chk(RagGroundHeight(0, -91, true) == -91,
        "**with no engine ground, our own reader is ignored** -- it has a triangle "
        "where the engine has none and sits within 14 units of it across 3,852 "
        "ground-truth samples");
    chk(RagGroundHeight(0, 0, false) == 0,
        "**a third source is invented when both fail** -- there isn't one, and 0 "
        "must stand so the UNKNOWN path can see it");

    // With the pad's real height the same reading becomes the truth: a ship at
    // -200 over ground at -91 is 109 up, which is inside the set-down gate.
    chk(RagHeightState(true, true, -200, -91) == RAG_ON_GROUND,
        "**the parked ship still reads AIRBORNE once the ground is real** -- that "
        "is the whole of the 19:38 failure");
    chk(RagDriveMayStart(RAG_ON_GROUND) == false, "and the drive still starts on it");

    // --- and the advice depends on why it is not moving ----------------------
    chk(RagWhyNotMoving(true, 109, 200) == RAG_STILL_GROUNDED,
        "**a ship sitting on the ground is told to gain altitude** -- the opposite "
        "of what it needs, and the 19:38 BAT is exactly that case");
    chk(RagWhyNotMoving(true, 2472, 200) == RAG_STILL_BLOCKED,
        "**a ship 2,472 units up is told to take off** -- it already has; something "
        "is in the way");
    chk(RagWhyNotMoving(false, 0, 200) == RAG_STILL_UNKNOWN,
        "**it guesses with no ground reading** -- telling him confidently to do the "
        "wrong one of two opposite things is worse than saying both");

    printf("rag_ground_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
