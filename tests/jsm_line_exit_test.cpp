// jsm_line_exit_test.cpp -- when a trigger line is a door (#dsrc, v0.111.0).
//
// Every assertion was written against a mutant; the note on each says which
// change it kills. The type ids are passed in rather than named here because
// the model must not depend on field_archive.h -- so the test supplies the same
// four numbers the call site does.
#include <cstdio>

#include "jsm_line_exit_model.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

// The values from JSMEntityType, in the order the enum declares them. Only the
// relative identity matters to the model.
enum { T_UNKNOWN = 0, T_NPC, T_LINE_EVENT, T_LINE_CAMERA_PAN, T_LINE_INTERACTIVE,
       T_LINE_SCREEN_BOUND, T_SAVE_POINT, T_MAP_EXIT };

static bool Elig(int cat, int type, bool dlg = false, bool save = false)
{
    return JsmLineExitEligible(cat, type, T_LINE_EVENT, T_LINE_CAMERA_PAN, dlg, save);
}

static void TestEligibleTypes()
{
    // `Sitahe` on all four tower floors, and ddtower5's Lock1/Lock2.
    CHECK(Elig(1, T_LINE_EVENT),      "an event line is eligible");
    CHECK(Elig(1, T_LINE_CAMERA_PAN), "a camera-pan line is eligible");
    // Kills widening to every line type. An interactive line is a conversation
    // first -- the same argument the category-3 rule makes about NPCs.
    CHECK(!Elig(1, T_LINE_INTERACTIVE), "an interactive line is NOT eligible");
    // Kills re-promoting a line that already carries its own MAPJUMP.
    CHECK(!Elig(1, T_LINE_SCREEN_BOUND), "a line that is already an exit is NOT eligible");
    CHECK(!Elig(1, T_UNKNOWN),   "an unknown-typed line is NOT eligible");
    CHECK(!Elig(1, T_SAVE_POINT),"a save point is NOT eligible");
}

static void TestCategoryGate()
{
    // Kills dropping the category test. Category 3 has its own REQ-follow with
    // its own rules; running both over the same entity would promote it twice.
    CHECK(!Elig(3, T_LINE_EVENT), "a category-3 entity is NOT eligible here");
    CHECK(!Elig(0, T_LINE_EVENT), "a door entity is NOT eligible");
    CHECK(!Elig(2, T_LINE_EVENT), "a background entity is NOT eligible");
    CHECK(!Elig(-1, T_LINE_EVENT), "a nonsense category is NOT eligible");
}

static void TestLaterPassesWin()
{
    // THE ONE THE DISC-WIDE SCANNER DIFF CAUGHT. Folded into the category-3
    // loop this rule fired before the dialog-REQ pass and stole nine lines --
    // `Eventline`, `mapjumpline`, `Jumpline1` across three fields -- that the
    // pass would have made interactive.
    CHECK(!Elig(1, T_LINE_EVENT, /*dlg*/true, false),
          "a line that REQs a dialog entity is NOT a door");
    CHECK(!Elig(1, T_LINE_CAMERA_PAN, /*dlg*/true, false),
          "...and the same for a camera-pan line");
    // Kills dropping the save-line test: ddruins1 and ddruins6 both have one.
    CHECK(!Elig(1, T_LINE_EVENT, false, /*save*/true),
          "a save line is NOT a door");
    CHECK(!Elig(1, T_LINE_EVENT, true, true), "and neither is both at once");
}

static void TestPlausibleDest()
{
    CHECK(JsmDestIsPlausibleField(302), "ddtower2 is a plausible destination");
    CHECK(JsmDestIsPlausibleField(0),   "field 0 is plausible -- other rules judge it");
    CHECK(JsmDestIsPlausibleField(JSM_FIELD_ID_MAX - 1), "the last id is plausible");
    // Kills dropping the bound. The disc-wide diff threw up 0x8001FFFE more
    // than once from an unresolved variable marker, and an "Exit to field
    // -2147418114" is worse than no exit at all.
    CHECK(!JsmDestIsPlausibleField(-2147418114), "an unresolved marker is not a field");
    CHECK(!JsmDestIsPlausibleField(-1),  "a negative id is not a field");
    CHECK(!JsmDestIsPlausibleField(JSM_FIELD_ID_MAX), "the bound itself is not a field");
    CHECK(!JsmDestIsPlausibleField(65535), "0xFFFF is not a field");
}

// ---------------------------------------------------------------------------
// Whose destination wins
// ---------------------------------------------------------------------------
static void TestOwnerDestWins()
{
    // v0.62.1's rule, unchanged: the resolver's answer beats the raw literal.
    CHECK(JsmOwnerDestWins(870, 0),   "a resolved owner destination wins");
    CHECK(JsmOwnerDestWins(302, 302), "and agreeing is fine");
    CHECK(JsmOwnerDestWins(0, 0),     "zero over zero is still the owner's call");
    // THE ddtower1 CASE. `Director` and `Hantei` are both Map Exit with param 0
    // -- the resolver's answer for a destination it could not follow -- while
    // the MAPJUMP3 in the method says 847 in literals. Taking 0 sent the only
    // way back to the core through the world-map-staging filter.
    CHECK(!JsmOwnerDestWins(0, 847), "zero does NOT beat a real literal");
    CHECK(!JsmOwnerDestWins(0, 1),   "not even by one");
    // Kills dropping the plausibility test on the owner.
    CHECK(!JsmOwnerDestWins(-2147418114, 847), "an unresolved marker never wins");
    CHECK(!JsmOwnerDestWins(-1, 302),          "nor a negative");
    CHECK(!JsmOwnerDestWins(JSM_FIELD_ID_MAX, 302), "nor one past the bound");
}

int main()
{
    TestOwnerDestWins();
    TestEligibleTypes();
    TestCategoryGate();
    TestLaterPassesWin();
    TestPlausibleDest();
    printf("jsm_line_exit_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
