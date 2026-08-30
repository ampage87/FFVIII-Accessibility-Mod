// scan_keep_test.cpp -- who survives the runtime entity scan (#shumi, v0.132.1).
//
// The numbers are tmkobo2's, read out of the field archive and the exe: Shou
// runs TALKRADIUS 130 then PUSHOFF, Otuki and Tukurite run TALKRADIUS 100 then
// PUSHOFF, and PUSHOFF writes 1 to +0x249.
#include <cstdio>

#include "scan_keep_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // ================================================================
    // A TALK RADIUS IN THE ENTITY'S OWN BLOCK IS ENOUGH.
    // ================================================================
    // This is the whole of the Sculptor bug. Shou's flags read talk=0 (which
    // under the engine's inverted polarity actually means talk ENABLED, but the
    // scan reads it raw and calls it false), the static scan finds no talk
    // setup, and the cached radius was filed three slots away on the Draw Point.
    // The radius sitting at +0x1F8 in his own block is 130.
    CHECK(ScanEntityIsTalkable(false, false, 130, false, 0),
          "Shou's own talk radius of 130 makes him talkable on its own");
    CHECK(!ScanEntityIsTalkable(false, false, 0, false, 0),
          "and with no signal at all he is not");

    // Every older signal still works by itself -- this is a widening, never a
    // narrowing, so nothing that used to be kept can start being dropped.
    CHECK(ScanEntityIsTalkable(true,  false, 0, false, 0), "the raw flag alone still counts");
    CHECK(ScanEntityIsTalkable(false, true,  0, false, 0), "the static JSM talk setup alone still counts");
    CHECK(ScanEntityIsTalkable(false, false, 0, true,  0), "the sticky latch alone still counts");
    CHECK(ScanEntityIsTalkable(false, false, 0, false, 250), "the cached radius alone still counts");

    // ================================================================
    // THE PUSH-ONLY DROP MUST NOT TAKE A PERSON WITH IT.
    // ================================================================
    // PUSHOFF writes 1, so `pushFlagSet` is true for all three Shumi. Model 6 is
    // below the >=10 generic-character shortcut, so before v0.132.1 every one of
    // them was discarded here.
    CHECK(!ScanDropAsPushOnly(true, 6, 130), "Shou is not push-only -- he asked for a talk radius");
    CHECK(!ScanDropAsPushOnly(true, 6, 100), "nor are Otuki and Tukurite");
    CHECK(ScanDropAsPushOnly(true, 6, 0),
          "a visible model-6 entity with the push flag and NO talk radius still drops");
    CHECK(!ScanDropAsPushOnly(false, 6, 0), "no push flag, no drop");
    CHECK(!ScanDropAsPushOnly(true, -1, 0), "an invisible entity belongs to a different branch");
    CHECK(!ScanDropAsPushOnly(true, 12, 0), "and a generic character model is an NPC (v0.07.97)");

    // ================================================================
    // SCENERY: THE GUARDS MATTER MORE THAN THE LIST.
    // ================================================================
    // The four fish: a real curated name, no talk radius, no talk flag, typed as
    // an ordinary NPC.
    CHECK(ScanDropAsScenery(true, true, false, 0, true), "Fish is curated scenery and goes");

    // AND THE MUTANT THAT WOULD DELETE THE WHOLE SHUMI WORKSHOP. On this path a
    // failed model join leaves the symbol empty, and IsBgControllerName() answers
    // TRUE for an empty name. If the emptiness guard were dropped, every entity
    // whose name could not be resolved would be deleted -- which in tmkobo2 is
    // all three Shumi, the two Moombas and the draw point.
    CHECK(!ScanDropAsScenery(false, true, false, 0, true),
          "an unresolved name proves nothing and must never be grounds for deletion");

    // A talk radius or a talk flag rescues a skip-list name. bcmin2_1's
    // 'Urakata' reads as a talkable NPC; gfcross2's is a Draw Point.
    CHECK(!ScanDropAsScenery(true, true, true,  0, true),   "a talkable skip-list name stays");
    CHECK(!ScanDropAsScenery(true, true, false, 100, true), "so does one with a live talk radius");
    CHECK(!ScanDropAsScenery(true, true, false, 0, false),
          "and one typed as an exit, draw point or save point is never scenery");
    CHECK(!ScanDropAsScenery(true, false, false, 0, true),
          "a name that is not in the curated list is not touched at all");

    if (g_fail == 0) printf("scan_keep_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
