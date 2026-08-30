// live_join_order_test.cpp -- the order tiebreak for script entities sharing one
// model id (#shumi, v0.132.3). The counts are tmkobo2's and tmsand1's.
#include <cstdio>

#include "live_join_order_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // tmkobo2: Shou, Otuki and Tukurite all run SETMODEL 6, none is a name-twin,
    // and the one at the statue has walked 407 units from his SET3 -- so all
    // three reach this pass unresolved, against three unresolved live entities.
    CHECK(LiveJoinOrderApplies(3, 3), "three live and three script entities pair in order");

    // tmsand1: the position tiebreak already resolved its three, so there is
    // nothing left for this pass to do. It must not fire on an empty set.
    CHECK(!LiveJoinOrderApplies(0, 3), "nothing unresolved means nothing to pair");
    CHECK(!LiveJoinOrderApplies(0, 0), "and neither list having anything is not a match");

    // A single leftover pair is the same argument with one element -- this is
    // what names a character whose model is shared with someone who did not
    // spawn into the scene at all.
    CHECK(LiveJoinOrderApplies(1, 1), "one and one still pair");

    // THE GUARD THAT MAKES IT SAFE. Unequal counts mean the two lists are not
    // the same set -- an entity did not spawn, or the script ran SETMODEL twice
    // and the runtime model no longer matches the one the scanner recorded
    // (tmkobo2's `Munba` runs 7 then 8). Pairing them in order would hand a live
    // entity somebody else's name, which is the exact failure the model key was
    // introduced to stop: Aaron spoke to "Quistis" and got a different NPC.
    CHECK(!LiveJoinOrderApplies(2, 3), "fewer live than script entities -- decline");
    CHECK(!LiveJoinOrderApplies(3, 2), "more live than script entities -- decline");
    CHECK(!LiveJoinOrderApplies(1, 0), "a live entity no script entity claims -- decline");
    CHECK(!LiveJoinOrderApplies(-1, -1), "and a degenerate count is never a match");

    if (g_fail == 0) printf("live_join_order_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
