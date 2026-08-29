// wm_story_test.cpp -- the three story rules that decide which destinations the
// world-map catalog offers, held to the words Aaron used and to the log that
// motivated them.
//
// Compiles src/wm_story_pure.inl directly, so this exercises the shipped rules
// rather than a restatement of them.
#include <cstdio>
#include <cstring>
#include "wm_story_pure.inl"

static int bad = 0;
static void chk(bool ok, const char* what)
{
    if (!ok) { printf("  BAD: %s\n", what); bad++; }
}

int main()
{
    printf("wm_story_test\n");

    // ---------------------------------------------------------------------
    // Q1 -- ownership. The bug this release exists to fix.
    // ---------------------------------------------------------------------
    // 23:03:38 in the 2026-08-25 log: aboard the ship, parked slot still zero.
    // v0.20.101 answered "not held" here and put the White SeeD Ship back on
    // the map. Being inside the Ragnarok is proof that he has it.
    chk(WmRagnarokHeldPure(true, false) == true,
        "**aboard the Ragnarok with an empty parked slot reads as NOT HELD** -- "
        "the slot holds the ship's parked position and is zero while he is "
        "inside it, which is every moment he is flying");

    // 23:05:09, ninety seconds later: on foot, slot live. This half always worked.
    chk(WmRagnarokHeldPure(false, true) == true,
        "on foot with a live parked slot no longer reads as held");

    chk(WmRagnarokHeldPure(true, true) == true, "both signals disagree with themselves");

    // The negative has to stay negative, or the ship vanishes for a player who
    // has not earned it yet.
    chk(WmRagnarokHeldPure(false, false) == false,
        "**neither signal present still reads as HELD** -- before he reaches the "
        "Ragnarok this must be false or the White SeeD Ship disappears early");

    // ---------------------------------------------------------------------
    // Q2 -- the White SeeD Ship's window. Both edges.
    // ---------------------------------------------------------------------
    chk(WmWhiteSeedPresentPure(1, false) == false, "disc 1 offers the White SeeD Ship");
    chk(WmWhiteSeedPresentPure(2, false) == false, "disc 2 offers the White SeeD Ship");
    chk(WmWhiteSeedPresentPure(3, false) == true,
        "**disc 3 without the Ragnarok hides the White SeeD Ship** -- that is the "
        "one window in which it is a real destination");

    chk(WmWhiteSeedPresentPure(3, true) == false,
        "**disc 3 WITH the Ragnarok still offers the White SeeD Ship** -- Aaron: "
        "\"no longer on the world map after you get Ragnarok\"");
    chk(WmWhiteSeedPresentPure(4, true) == false, "disc 4 with the Ragnarok offers it");

    // "regardless of vehicle" is the whole point: Q2 takes ownership as a fact,
    // never a vehicle, so there is no combination of ridings that brings it back
    // once Q1 says held.
    for (int disc = 3; disc <= 4; disc++)
        chk(WmWhiteSeedPresentPure(disc, WmRagnarokHeldPure(true,  false)) == false &&
            WmWhiteSeedPresentPure(disc, WmRagnarokHeldPure(false, true )) == false &&
            WmWhiteSeedPresentPure(disc, WmRagnarokHeldPure(true,  true )) == false,
            "**a vehicle he happens to be riding changes the answer** -- Aaron: "
            "\"should not appear on the world map catalog again regardless of vehicle\"");

    // ---------------------------------------------------------------------
    // Q3 -- the mobile Balamb Garden is not a Ragnarok destination.
    // ---------------------------------------------------------------------
    chk(WmOfferMobileGardenPure(true, true) == false,
        "**the mobile Garden is still offered while flying** -- Aaron: \"should not "
        "be an option in the catalog when flying Ragnarok. To get to Garden you "
        "land at FH where Garden is parked\"");

    chk(WmOfferMobileGardenPure(true, false) == true,
        "**on foot and in the car the mobile Garden is gone too** -- finding your "
        "ride again is the entire reason that entry exists");

    // No coordinate, nothing to offer -- and flight does not change that.
    chk(WmOfferMobileGardenPure(false, false) == false, "an implausible bgu_pos is offered");
    chk(WmOfferMobileGardenPure(false, true)  == false, "an implausible bgu_pos is offered in flight");

    printf("wm_story_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
