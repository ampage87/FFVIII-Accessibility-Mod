// gated_exit_test.cpp -- following an if/else-if chain to the guard that
// actually governs a MAPJUMP (#shumi, v0.132.0).
//
// The numbers are tmkobo2 'Munbamini' method 1, read word by word out of the
// field archive; see gated_exit_model.inl for the dump.
#include <cstdio>

#include "gated_exit_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // Munbamini's chain. Link 0 at rel 0 skips to 19; link 1 at 19 skips to 37;
    // link 2 at 37 skips to 119. The MAPJUMPO sits at rel 117.
    const int MJ = 117;
    CHECK(!GatedExitJumpIsInsideGuard(0,  19, MJ), "link 0 ends at 19 -- it does not guard the jump");
    CHECK(!GatedExitJumpIsInsideGuard(19, 18, MJ), "nor does link 1, which ends at 37");
    CHECK( GatedExitJumpIsInsideGuard(37, 82, MJ), "link 2 skips to 119 and the jump is inside it");

    // THE BUG THIS FILE EXISTS FOR: under the leading-guard-only rule the answer
    // was link 0 alone, and link 0 says no. That is how a story-gated exit came
    // out ungated and reached Aaron's catalog as 'Exit to Shumi Village -
    // Elevator' in a room where walking onto it does nothing.
    CHECK(!GatedExitJumpIsInsideGuard(0, 19, MJ),
          "the leading guard alone cannot see a jump behind the third link");

    // A guard that starts AFTER the jump is not guarding it, however long its
    // skip. Without this a trailing else-branch would claim the exit.
    CHECK(!GatedExitJumpIsInsideGuard(200, 400, MJ), "a guard past the jump guards nothing");
    CHECK(!GatedExitJumpIsInsideGuard(MJ, 50, MJ),   "nor one that starts exactly on it");

    // Degenerate inputs cannot produce a gate. A zero or negative skip is a
    // decode that went wrong, and gating on it would hide a real door.
    CHECK(!GatedExitJumpIsInsideGuard(-1, 100, MJ), "a negative offset is not a guard");
    CHECK(!GatedExitJumpIsInsideGuard(0,   0,  MJ), "nor is a zero skip");
    CHECK(!GatedExitJumpIsInsideGuard(0,  -5,  MJ), "nor a negative one");

    // The walk stops at the jump, so a chain cannot run off the end of a method.
    CHECK(GatedExitScanLimit(117, 122) == 117, "the search stops at the MAPJUMP");
    CHECK(GatedExitScanLimit(500, 122) == 122, "and never past the method's own words");
    CHECK(GatedExitScanLimit(-1,  122) == 0,   "no MAPJUMP means nothing to search");
    CHECK(GATED_EXIT_MAX_CHAIN >= 3, "Munbamini's chain is three links; the cap must clear it");

    if (g_fail == 0) printf("gated_exit_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
