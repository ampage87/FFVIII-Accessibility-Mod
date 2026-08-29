// drive_turn_test -- the stuck detector must not count a turn as a wedge.
#include <cstdio>
#include "drive_turn_pure.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }

int main()
{
    std::printf("drive_turn_test\n");

    // ---- the signed circle ------------------------------------------------
    check(DriveHeadingDelta(0, 0) == 0, "no turn is zero");
    check(DriveHeadingDelta(100, 0) == 100, "a small clockwise turn is positive");
    check(DriveHeadingDelta(0, 100) == -100, "and anticlockwise is negative");
    check(DriveHeadingDelta(10, 4090) == 16,
          "**a turn across the 4095/0 seam is 16 units, not 4080** -- the circle "
          "wraps and a subtraction that forgets it reports a full spin every time "
          "the ship passes north");
    check(DriveHeadingDelta(4090, 10) == -16, "and the same the other way");

    // ---- the excuse -------------------------------------------------------
    check(!DriveTurnExcusesStall(false, 2000, 0, 3),
          "**on foot and in the car nothing is excused** -- those thresholds are "
          "BAT-tuned over many builds and this rule does not touch them");
    check(DriveTurnExcusesStall(true, 300, 0, 3),
          "**a flying vehicle mid-turn is not stuck** -- tank controls do not "
          "translate while rotating, and the successful Sorceress Memorial drive "
          "spent one of its six lives on exactly this");
    check(!DriveTurnExcusesStall(true, 20, 0, 3),
          "**but a ship that is barely moving its heading IS stuck** -- otherwise "
          "the excuse covers the wedge it was meant to distinguish from");
    check(DriveTurnExcusesStall(true, -300, 0, 3), "either direction of turn");

    // ---- and the bound, which is the part that matters -------------------
    check(DriveTurnExcusesStall(true, 2000, 2, 3), "two excuses in, still allowed");
    check(!DriveTurnExcusesStall(true, 2000, 3, 3),
          "**the fourth is refused however hard it is turning** -- an un-wedge "
          "burst changes the heading too, so an unbounded excuse is a drive that "
          "spins in one place forever and never gives up. That is worse than "
          "giving up early: a give-up at least says so and hands control back");
    check(!DriveTurnExcusesStall(true, 2000, 99, 3), "and stays refused");

    std::printf(bad ? "drive_turn_test: FAILED (%d bad)\n" : "drive_turn_test: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
