// rag_approach_test.cpp -- the braked approach, and the strike an orbit cannot fake.
#include <cstdio>
#include "rag_ground_pure.inl"
#include "rag_approach_pure.inl"

static int bad = 0;
static void chk(bool ok, const char* w) { if (!ok) { printf("  BAD: %s\n", w); bad++; } }

int main()
{
    printf("rag_approach_test\n");

    // --- altitude: climb, and never come down --------------------------------
    //
    // Aaron: "You do not have to descend in order to land. Pressing X to
    // land/disembark descends the ship automatically." v0.89.0's approach
    // descent and v0.91.0's vertical settle were both built on the assumption
    // that the ship had to be LOW to be set down, and it does not.
    chk(RagAltitudeWant(true) == RAG_ALT_CLIMB,
        "**the airship does not climb** -- Aaron: \"make sure the airship ascends to "
        "max altitude when doing auto-drive so it doesn't get stuck against things "
        "on land\", and flying the last stretch low is what put it into the "
        "Fisherman's Horizon towers at 15:15");
    chk(RagAltitudeWant(false) == RAG_ALT_HOLD,
        "on foot or in the car this commands an altitude change");
    chk(RagAltitudeWant(true) != RAG_ALT_DESCEND && RagAltitudeWant(false) != RAG_ALT_DESCEND,
        "**something still commands a descent** -- every unit of altitude given away "
        "on the approach buys nothing, because X flies the descent itself, and the "
        "altitude it costs is what keeps the ship clear of the towers");

    // --- forward is its own axis now ---------------------------------------
    chk(RagForwardWant(true, 0, 96) == true,   "a ship dead on the bearing does not go forward");
    chk(RagForwardWant(true, 96, 96) == true,  "a ship at the edge of the cone does not go forward");
    chk(RagForwardWant(true, 97, 96) == false,
        "**it flies forward while still turning** -- turning under power is what a "
        "wide arc is made of, and an arc is what put the ship in orbit at v0.85.0");
    chk(RagForwardWant(false, 0, 96) == false,
        "**the flight throttle fires for the car** -- A is the car's gas pedal too, "
        "and its own path already presses it");

    // --- the aim cone -------------------------------------------------------
    chk(RagSteerCone(true, 1000.0, 576) == RAG_FINAL_CONE &&
        RAG_FINAL_CONE < 576,
        "**the braked approach uses the car's ~50-degree cone** -- at this speed "
        "fifty degrees of bearing error is a mile of arc");
    chk(RagSteerCone(true, 50000.0, 576) == 576, "the long haul uses the tight cone");
    chk(RagSteerCone(false, 1000.0, 576) == 576, "on foot the cone changed");

    // --- the strike ---------------------------------------------------------
    // The BAT: moving 3000 units a window while orbiting at a constant distance.
    chk(RagStuckStrike(true, 3043.0, 100.0, 1618.0, 1645.0, 100.0) == true,
        "**three thousand units of orbit counts as progress** -- that is the 12:59 "
        "trace exactly, and it is why the drive ran fifty-eight seconds without ever "
        "reaching Stuck check 6/6");
    chk(RagStuckStrike(true, 3043.0, 100.0, 958.0, 1645.0, 100.0) == false,
        "687 units closer in a window scores a strike");
    chk(RagStuckStrike(true, 0.0, 100.0, 500.0, -1.0, 100.0) == false,
        "**the first check of a flight is a strike** -- there is no previous reading, "
        "and starting a drive one strike down is wrong");

    // On foot and in the car the rule is untouched -- movement, not progress.
    chk(RagStuckStrike(false, 0.0, 100.0, 1618.0, 1645.0, 100.0) == true,
        "on foot, not moving no longer scores a strike");
    chk(RagStuckStrike(false, 3043.0, 100.0, 1618.0, 1645.0, 100.0) == false,
        "**on foot, moving without closing distance now scores a strike** -- the "
        "reverse-burst recovery #67 tuned depends on those strikes being spent slowly");

    // --- blocked: commanded forward, and did not move -----------------------
    //
    // The 15:15 BAT, sixteen identical lines: dist=1806 hdg=2255 off=65 cone=96
    // gas=1 keys=U---. Asked to go, did not go, for fifteen seconds.
    chk(RagBlockedStrike(true, 5, 0.0, 100.0) == true,
        "**forward commanded on five ticks with zero movement is not read as "
        "blocked** -- that is the 15:15 log exactly, 1,806 units short of the "
        "Fisherman's Horizon pad with UP held");

    chk(RagBlockedStrike(true, 0, 0.0, 100.0) == false,
        "**a ship that was never asked to go reads as blocked** -- pivoting in place "
        "is what the tight cone asks for, and calling that an obstruction would end "
        "every drive that has to turn");

    chk(RagBlockedStrike(true, 5, 3000.0, 100.0) == false,
        "a ship moving 3000 units reads as blocked");
    chk(RagBlockedStrike(false, 5, 0.0, 100.0) == false,
        "**on foot or in the car this fires** -- a walker genuinely wedges on terrain "
        "and #67's reverse-burst recovery is what handles that; this rule is about a "
        "hull no polygon can stop");

    // Two strikes, not six: the signature is unambiguous and eighteen seconds of
    // pressing into a wall helps nobody.
    chk(RAG_BLOCKED_MAX == 2,
        "**the blocked bound is not two** -- six is the ordinary give-up, and it is six "
        "because a car's stall is ambiguous; this one is not");

    printf("rag_approach_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
