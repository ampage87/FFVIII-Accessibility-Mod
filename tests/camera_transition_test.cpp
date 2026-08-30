// camera_transition_test.cpp -- the "jump"-named line rule (#shumi, v0.132.0).
//
// Every assertion is a row read out of the disc by tests_out/jsm_scan_harness
// over all 900 fields, not a guess about what FF8 probably does.
#include <cstdio>

#include "camera_transition_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // ================================================================
    // THE NAME TEST, which is where the old rule both began and ended.
    // ================================================================
    CHECK(CamXlineNameHasJump("Jump"),        "'Jump' -- tmgate1, tmkobo2, fhmin1, sdcore1");
    CHECK(CamXlineNameHasJump("Jump2"),       "'Jump2' -- tmkobo1");
    CHECK(CamXlineNameHasJump("jumpline0"),   "'jumpline0' -- 12 fields");
    CHECK(CamXlineNameHasJump("Linejump1"),   "'Linejump1' -- the Esthar convention");
    CHECK(CamXlineNameHasJump("mapjump_FW"),  "'mapjump_FW' -- the gnroad convention");
    CHECK(CamXlineNameHasJump("JumptoMD2_3L"),"'JumptoMD2_3L' -- bgmd2_8");
    CHECK(CamXlineNameHasJump("bgroom_1_jump01"), "and the ones that really are camera pans");
    CHECK(!CamXlineNameHasJump("Kako"),       "'Kako' does not match");
    CHECK(!CamXlineNameHasJump("Mitukeru"),   "nor 'Mitukeru'");
    CHECK(!CamXlineNameHasJump(""),           "nor an empty name");
    CHECK(!CamXlineNameHasJump(0),            "nor a null one");

    // ================================================================
    // A DESTINATION IN ANY OF ITS THREE FORMS IS STILL A DESTINATION.
    // ================================================================
    CHECK(CamXlineHasDestination(936), "a literal field id -- tmgate1's Jump goes to the Elevator");
    CHECK(CamXlineHasDestination(0),   "field 0 is a real field, not an absence");
    CHECK(CamXlineHasDestination(981), "and so is the last one");
    CHECK(CamXlineHasDestination(-2),  "-2 is WORLDMAPJUMP, which v0.60.0 already established is an exit");
    CHECK(CamXlineHasDestination((int)0x8000FFFEu),
          "a PSHM marker is a destination read at run time -- tmele1's own Jump");
    CHECK(CamXlineHasDestination((int)0x800003B1u),
          "and so is the marker form the elevator resolves through");
    CHECK(!CamXlineHasDestination(-1), "-1 is the scanner saying it found none");

    // ================================================================
    // THE THREE FIELDS AARON WAS STANDING IN.
    // ================================================================
    // tmgate1 (937) 'Jump' -> 936, the Elevator. This is the entry his log
    // called 'Camera transition, 1 of 3' -- the way out of Shumi Village.
    CHECK(!CamXlineIsCameraTransition(true, 936, 937),
          "tmgate1's Jump is the ELEVATOR, not a camera angle");
    // tmkobo2 (941) 'Jump' -> 940, Residence 2. The way back out of the
    // workshop, and the other half of what Aaron reported.
    CHECK(!CamXlineIsCameraTransition(true, 940, 941),
          "tmkobo2's Jump is the way back to Residence 2");
    // tmkobo1 (940) 'Jump2' -> 941, Residence 3.
    CHECK(!CamXlineIsCameraTransition(true, 941, 940),
          "tmkobo1's Jump2 is the way in to Residence 3");
    // tmele1 (936) 'Jump' -> PSHM marker; resolves to 945 going down and 937
    // going up. The elevator car's own door, and the reason the car had no
    // catalog entry at all.
    CHECK(!CamXlineIsCameraTransition(true, (int)0x8000FFFEu, 936),
          "the elevator car's own door is an exit even before its destination is known");

    // ================================================================
    // THE 14 THAT REALLY ARE CAMERA TRANSITIONS KEEP THE LABEL.
    // ================================================================
    // bgroom_1's four, feclock2's pair, cwwood4's in-field Jump, ecpway1a,
    // ecmall1a, ecoway3a, doani1_1/2, cdfield2, bgmd2_6 -- all param=-1.
    CHECK(CamXlineIsCameraTransition(true, -1, 700),
          "a jump-named line that goes nowhere is exactly what the rule was for");
    CHECK(CamXlineIsCameraTransition(true, -1, -1),
          "and it does not need to know its own field to say so");

    // A line the rule does not apply to at all is never a camera transition,
    // whatever its destination. 'Kako' on tmmura2 carries dest=940 and must
    // reach the catalog as an exit.
    CHECK(!CamXlineIsCameraTransition(false, -1, 944),  "a non-jump name is not this rule's business");
    CHECK(!CamXlineIsCameraTransition(false, 940, 944), "even when it does carry a destination");

    // ================================================================
    // THE WORLD-MAP CARVE-OUT v0.60.0 ADDED IS SUBSUMED, NOT DROPPED.
    // ================================================================
    // The seven Chocobo Forest woods, edview1b's four, eeview1/2/3, etsta1,
    // bdview1, fhparar1, sdisle1. A regression here is a field whose only way
    // out disappears -- which is the bug v0.60.0 existed to fix.
    CHECK(!CamXlineIsCameraTransition(true, -2, 728),
          "cwwood1's Jump still goes to the world map, not to a camera");

    // AND THE ONE CASE THE DISC DOES NOT CONTAIN. Zero of the 148 jump-named
    // lines target their own field. If one ever appears it is a screen change,
    // and offering it as an exit would send a blind player to where he already
    // is -- so it is named honestly instead.
    CHECK(CamXlineIsCameraTransition(true, 937, 937),
          "a jump to the field you are standing in is a screen change, not a journey");
    CHECK(!CamXlineIsCameraTransition(true, 937, -1),
          "but with no field id known, the destination alone decides");

    // ================================================================
    // THE MUTANT THAT MATTERS: keying off the TYPE instead of the destination.
    // ================================================================
    // 93 of the 95 literal-destination lines are Screen Boundary, so a rule
    // written as "Screen Boundary means exit" passes almost everything -- and
    // then loses bgsido_3's mappjump1 (dest=254) and tiyane1's Jumpline1
    // (dest=931), both typed Interactive Line. The destination is the property
    // that is actually true of all of them.
    CHECK(!CamXlineIsCameraTransition(true, 254, 253),
          "bgsido_3's mappjump1 is an Interactive Line AND a real exit");
    CHECK(!CamXlineIsCameraTransition(true, 931, 930),
          "so is tiyane1's Jumpline1");

    if (g_fail == 0) printf("camera_transition_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
