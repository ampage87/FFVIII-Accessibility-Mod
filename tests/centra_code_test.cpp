// centra_code_test.cpp -- the Centra Ruins five-digit code (#centra, v0.115.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills. The numbers come from crtower3's own script.
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdint>

#include "centra_code_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }
static void EQ(const char* got, const char* want, const char* w)
{
    if (strcmp(got, want) != 0) { printf("FAIL: %s (got \"%s\", want \"%s\")\n", w, got, want); g_fail++; }
}

static void TestAddresses()
{
    // The five entered digits are var[1028..1032] and the five the roof shows
    // are var[364..368]; the script reads exactly these and nothing else.
    CHECK(CC_VAR_BASE + CC_VAR_CURSOR  == 0x01CFEDBBu, "the cursor is at 0x01CFEDBB");
    CHECK(CC_VAR_BASE + CC_VAR_DIGIT0  == 0x01CFEDBCu, "digit 1 is at 0x01CFEDBC");
    CHECK(CC_VAR_BASE + CC_VAR_DIGIT0 + 4 == 0x01CFEDC0u, "digit 5 is at 0x01CFEDC0");
    CHECK(CC_VAR_BASE + CC_VAR_TARGET0 == 0x01CFEB24u, "the first target digit is at 0x01CFEB24");
    CHECK(CC_DIGITS == 5, "five digits, as no1..no5 in the field");
}

static void TestValidity()
{
    for (int v = 0; v <= 9; v++) CHECK(CcDigitValid(v), "0 through 9 are digits");
    // The script wraps at both ends, so nothing outside 0..9 ever reaches the
    // display. A read that produces one is garbage and must not be spoken.
    CHECK(!CcDigitValid(-1), "minus one is not a digit");
    CHECK(!CcDigitValid(10), "ten is not a digit");
    CHECK(!CcDigitValid(255),"an unread byte is not a digit");
    CHECK(CcCursorValid(0) && CcCursorValid(5), "0 and 5 are cursor values");
    CHECK(!CcCursorValid(6), "there is no sixth digit");
    CHECK(!CcCursorValid(-1),"nor a minus-first");
    // Kills confusing "idle" with "position zero".
    CHECK(!CcEntryActive(CC_CURSOR_IDLE), "cursor 0 means the panel is CLOSED");
    CHECK(CcEntryActive(1) && CcEntryActive(5), "1 and 5 are open positions");
    CHECK(!CcEntryActive(6), "6 is not");
}

static void TestWhenToSpeak()
{
    CHECK(CcShouldAnnounce(true, 1, 0, false, 0, 0), "the panel opening is announced");
    CHECK(CcShouldAnnounce(true, 2, 0, true, 1, 0),  "moving to the next digit is announced");
    CHECK(CcShouldAnnounce(true, 1, 4, true, 1, 3),  "changing the value is announced");
    // Kills dropping the dedup. The script's loop re-reads both every frame, so
    // without this the mod says the same digit thirty times a second and the
    // player hears one continuous stammer.
    CHECK(!CcShouldAnnounce(true, 3, 7, true, 3, 7), "an unchanged panel says nothing");
    CHECK(!CcShouldAnnounce(false, 1, 0, false, 0, 0), "a closed panel says nothing");
    CHECK(!CcShouldAnnounce(false, 3, 7, true, 3, 7),  "closing the panel says nothing");
}

static void TestSpeech()
{
    char b[128];
    CcPositionText(b, sizeof(b), 3, 7);
    EQ(b, "Digit 3 of 5. 7.", "position first, then the value");
    CcPositionText(b, sizeof(b), 1, 0);
    EQ(b, "Digit 1 of 5. 0.", "zero is a value, not an absence");
    // A garbage read still names the position rather than reading a wrong number.
    CcPositionText(b, sizeof(b), 2, 200);
    EQ(b, "Digit 2 of 5.", "an out-of-range value is not spoken as a number");
    // Kills speaking a position when the panel is shut.
    CcPositionText(b, sizeof(b), 0, 4);
    EQ(b, "", "a closed panel produces no position line");
    CcPositionText(nullptr, 16, 3, 7);          // must not crash
    char guard[4] = { 'x','x','x','x' };
    CcPositionText(guard, 0, 3, 7);
    CHECK(guard[0] == 'x', "a zero-length buffer is left alone");

    const int good[5] = { 4, 7, 1, 9, 2 };
    CcSequenceText(b, sizeof(b), "Code:", good);
    EQ(b, "Code: 4, 7, 1, 9, 2.", "the whole code reads as five separate numbers");
    CcSequenceText(b, sizeof(b), "Entered:", good);
    EQ(b, "Entered: 4, 7, 1, 9, 2.", "and so does the entry");
    // Kills reading a partially-garbage row aloud: better silence than a wrong
    // code, because the player will write this down and act on it.
    const int bad[5] = { 4, 7, 200, 9, 2 };
    CcSequenceText(b, sizeof(b), "Code:", bad);
    EQ(b, "", "a row with any bad digit is not spoken at all");
    CcSequenceText(b, sizeof(b), "Code:", nullptr);
    EQ(b, "", "and neither is a null row");
}

// v0.115.0: the opening utterance is ONE utterance. Every line this module
// speaks interrupts, so controls-then-position as two calls loses the controls.
static void TestOpening()
{
    char buf[512];
    CcOpeningText(buf, sizeof(buf), 1, 0);
    // The controls survive in full...
    CHECK(strstr(buf, "Up and Down move between the five digits") != nullptr,
          "the opening utterance carries the controls");
    CHECK(strstr(buf, "Press C") != nullptr,
          "the opening utterance names the repeat key");
    // ...AND the starting position arrives with them. Kills a mutant that
    // speaks only CC_CONTROLS_TEXT, which would leave the player on digit 1
    // with no idea which digit that was.
    CHECK(strstr(buf, "Digit 1 of 5. 0.") != nullptr,
          "the opening utterance carries the starting position");

    // A cursor that is not on a digit yields the controls alone rather than a
    // dangling "Digit 0 of 5". CcPositionText already returns empty for it;
    // this pins that the join does not paste an empty string on with a space.
    CcOpeningText(buf, sizeof(buf), 0, 3);
    CHECK(strcmp(buf, CC_CONTROLS_TEXT) == 0,
          "an idle cursor yields the controls with nothing appended");

    // A short buffer stays terminated rather than running off the end.
    char tiny[8];
    CcOpeningText(tiny, sizeof(tiny), 2, 4);
    CHECK(strlen(tiny) < sizeof(tiny), "a short opening buffer stays terminated");

    // The box is a different shape from the speech on purpose -- columns for
    // the eye, a sentence for the ear -- but both must name the same four
    // controls. If one is edited without the other a sighted tester and a
    // blind player are looking at different games.
    CHECK(strstr(CC_CONTROLS_BOX, "Up / Down") != nullptr, "the box names Up/Down");
    CHECK(strstr(CC_CONTROLS_BOX, "Left / Right") != nullptr, "the box names Left/Right");
    CHECK(strstr(CC_CONTROLS_BOX, "Triangle") != nullptr, "the box names Triangle");
    CHECK(strstr(CC_CONTROLS_BOX, "C ") != nullptr, "the box names the C key");
}

// v0.116.0 (#centra): C must not hand the player a code they have not earned,
// and must not stop working the moment they take the eyes back out.
static void TestReveal()
{
    // The engine's own condition: both ROOF sockets, bits 2 and 3.
    CHECK(CC_VAR_STATUS == 359, "the eye-socket flags are var[359]");
    CHECK(CC_ROOF_EYES_MASK == 12, "the roof sockets are bits 2 and 3");

    CHECK(CcRoofStatueShowsCode(12), "both roof eyes -> the code is on screen");
    CHECK(CcRoofStatueShowsCode(12 | 32), "unrelated bits do not matter");
    CHECK(!CcRoofStatueShowsCode(4),  "one roof eye is not enough");
    CHECK(!CcRoofStatueShowsCode(8),  "the other roof eye alone is not enough");
    CHECK(!CcRoofStatueShowsCode(0),  "no eyes, no code");
    // THE ONE THAT MATTERS. Bits 0 and 1 are the LOWER statue, by the door.
    // A mutant using mask 3, or `(status & 12) != 0`, would unlock C for a
    // player who walked straight to the door and never went to the roof --
    // which is precisely the case Aaron asked about.
    CHECK(!CcRoofStatueShowsCode(3),  "both eyes in the LOWER statue is not the roof");
    CHECK(!CcRoofStatueShowsCode(1),  "left eye in the lower statue is not the roof");
    CHECK(!CcRoofStatueShowsCode(2),  "right eye in the lower statue is not the roof");

    // The latch is what keeps C working after the eyes come back out to be
    // carried down. Kills `return CcRoofStatueShowsCode(status);` -- that
    // mutant refuses at the code panel, the one place the repeat is needed.
    CHECK(CcCodeRevealed(0, true),   "once seen, the code stays available");
    CHECK(CcCodeRevealed(3, true),   "still available with the eyes in the lower statue");
    CHECK(CcCodeRevealed(12, false), "available while it is on screen, before any latch");
    CHECK(!CcCodeRevealed(0, false), "not seen and not showing -> refuse");
    CHECK(!CcCodeRevealed(3, false), "eyes in the lower statue alone -> refuse");

    // A refusal has to say what to do next; "no" on its own is the same dead
    // end as the silence this project keeps having to fix.
    CHECK(strstr(CC_CODE_UNKNOWN_TEXT, "roof") != nullptr,
          "the refusal names where the code comes from");
}

// v0.116.0: the one-frame 0xFF between `digit--` and the script's wrap to 9.
static void TestWrapTransient()
{
    // Observed in the 2026-08-27 log as a bare "Digit 5 of 5." between 0 and 9.
    CHECK(!CcShouldAnnounce(true, 5, 255, true, 5, 0),
          "a mid-wrap 0xFF is not announced");
    CHECK(!CcShouldAnnounce(true, 5, -1, true, 5, 0),
          "a negative read is not announced");
    // ...but the wrapped value that follows IS, because lastDigit was never
    // moved to the garbage. Kills a mutant that suppresses the whole wrap.
    CHECK(CcShouldAnnounce(true, 5, 9, true, 5, 0),
          "the wrapped value announces on the next poll");
    // A real change still announces, and an unchanged digit still does not.
    CHECK(CcShouldAnnounce(true, 2, 4, true, 1, 4), "a cursor move announces");
    CHECK(!CcShouldAnnounce(true, 2, 4, true, 2, 4), "an unchanged position is silent");
}

static void TestMatch()
{
    const int a[5] = { 4, 7, 1, 9, 2 };
    const int same[5] = { 4, 7, 1, 9, 2 };
    const int diff[5] = { 4, 7, 1, 9, 3 };
    CHECK(CcCodeMatches(a, same), "an exact match is a match");
    // Kills stopping the comparison early -- the script checks all five, and the
    // last one is the one a player is most likely to fumble.
    CHECK(!CcCodeMatches(a, diff), "a difference in the FIFTH digit is a mismatch");
    const int diff1[5] = { 5, 7, 1, 9, 2 };
    CHECK(!CcCodeMatches(a, diff1), "and in the first");
    const int bad[5] = { 4, 7, 1, 9, 99 };
    CHECK(!CcCodeMatches(a, bad),  "a garbage target never matches");
    CHECK(!CcCodeMatches(nullptr, same), "null never matches");
    CHECK(!CcCodeMatches(a, nullptr),    "in either direction");
}

int main()
{
    TestAddresses();
    TestValidity();
    TestWhenToSpeak();
    TestSpeech();
    TestMatch();
    TestOpening();
    TestReveal();
    TestWrapTransient();
    printf("centra_code_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
