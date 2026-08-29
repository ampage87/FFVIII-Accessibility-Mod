// naming_bypass_test.cpp -- which mode the GF-naming-screen bypass returns to
// (#naming-bypass, v0.99.0).
//
// Compiles src/naming_bypass_model.inl standalone, in the same (global) scope
// battle_tts.cpp includes it in. Every assertion was written against a mutant;
// the note on each says which change it kills.
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "naming_bypass_model.inl"

using namespace NamingBypassModel;

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

// ---------------------------------------------------------------------------
// The engine's own test, copied from 0x00470AA2 and 0x00470C8D
// ---------------------------------------------------------------------------
static void TestEngineTest()
{
    CHECK(NbModeFromReturnKind(1) == NB_MODE_FIELD, "returnKind 1 is the field");
    // The engine tests `== 1` and takes the world-map branch for EVERYTHING
    // else -- it is not a two-value flag. 0, 3 and 6 all appear in this same
    // function (0x00470A7D writes 0, 0x00470AA9 writes 3, 0x00470CC7 writes 6).
    CHECK(NbModeFromReturnKind(0) == NB_MODE_WORLDMAP, "returnKind 0 is the world map");
    CHECK(NbModeFromReturnKind(3) == NB_MODE_WORLDMAP, "returnKind 3 is the world map");
    CHECK(NbModeFromReturnKind(6) == NB_MODE_WORLDMAP, "returnKind 6 is the world map");
    CHECK(NbModeFromReturnKind(2) == NB_MODE_WORLDMAP, "returnKind 2 is the world map");
}

// ---------------------------------------------------------------------------
// What counts as a place a battle can return to
// ---------------------------------------------------------------------------
static void TestHostMode()
{
    CHECK(NbIsHostMode(1), "field is a host mode");
    CHECK(NbIsHostMode(2), "world map is a host mode");

    // THE MENU IS NOT. The 2026-08-25 log goes 2 -> 6 -> 2 -> 3 into the Cactuar
    // fight, so a sampler that accepted mode 6 could latch the menu and hand the
    // bypass a 6 to write into the immediate.
    CHECK(!NbIsHostMode(6), "the menu is not a host mode");
    CHECK(!NbIsHostMode(3), "the battle swirl is not a host mode");
    CHECK(!NbIsHostMode(4), "the victory screen is not a host mode");
    CHECK(!NbIsHostMode(11), "the naming screen is not a host mode");
    CHECK(!NbIsHostMode(0), "the title screen is not a host mode");
    CHECK(!NbIsHostMode(-1), "no observation is not a host mode");
}

// ---------------------------------------------------------------------------
// Choosing the mode
// ---------------------------------------------------------------------------
static void TestWanted()
{
    int src = -1;

    // THE BUG, AS A TEST. Jumbo Cactuar: the party was on the world map
    // (observed 2) and v0.13.46 wrote 1. Kills any mutant that reinstates a
    // constant, and the one that swaps the two modes.
    CHECK(NbWantedMode(true, 2, true, 3, &src) == NB_MODE_WORLDMAP &&
          src == (int)NB_SRC_OBSERVED,
          "a world-map battle returns to the world map");

    // And the case that must not regress: a GF drawn in a dungeon.
    CHECK(NbWantedMode(true, 1, true, 1, &src) == NB_MODE_FIELD &&
          src == (int)NB_SRC_OBSERVED,
          "a field battle still returns to the field");

    // THE OBSERVATION WINS. It is a direct measurement of where the party was
    // standing; the engine test is a reading of one branch. When they disagree
    // the measurement is what the mod acts on. Kills the mutant that reorders
    // the two sources -- which every agreeing case above would still pass.
    CHECK(NbWantedMode(true, 2, true, 1, &src) == NB_MODE_WORLDMAP &&
          src == (int)NB_SRC_OBSERVED,
          "the observed mode beats a disagreeing engine test");
    CHECK(NbWantedMode(true, 1, true, 3, &src) == NB_MODE_FIELD &&
          src == (int)NB_SRC_OBSERVED,
          "and beats it the other way round too");

    // The fallback: no observation (the mod loaded mid-battle).
    CHECK(NbWantedMode(false, -1, true, 1, &src) == NB_MODE_FIELD &&
          src == (int)NB_SRC_ENGINE,
          "with no observation the engine test decides -- field");
    CHECK(NbWantedMode(false, -1, true, 3, &src) == NB_MODE_WORLDMAP &&
          src == (int)NB_SRC_ENGINE,
          "with no observation the engine test decides -- world map");

    // An observation that is not a host mode is not an observation. Kills the
    // mutant that drops the NbIsHostMode guard and writes a 6 into the immediate.
    CHECK(NbWantedMode(true, 6, true, 1, &src) == NB_MODE_FIELD &&
          src == (int)NB_SRC_ENGINE,
          "a menu-mode observation falls through to the engine test");

    // NOTHING TO GO ON MEANS DO NOT PATCH. A naming screen Aaron must navigate
    // is bad; a save file that believes it is in a room the party never entered
    // is worse, and guessing is how you get the second.
    CHECK(NbWantedMode(false, -1, false, 0, &src) == 0 &&
          src == (int)NB_SRC_NONE,
          "no observation and no engine read -> do not patch");
    CHECK(NbWantedMode(true, 6, false, 0, &src) == 0 &&
          src == (int)NB_SRC_NONE,
          "an unusable observation and no engine read -> do not patch");

    // src is reported on every path, including the refusal -- the caller keys
    // its "do not patch" branch on it.
    src = -1; NbWantedMode(true, 1, true, 1, &src);
    CHECK(src != -1, "the source is always reported");
}

// ---------------------------------------------------------------------------
// The disagreement report
// ---------------------------------------------------------------------------
static void TestDisagree()
{
    CHECK(NbSourcesDisagree(true, 2, true, 1), "world-map observation vs field engine test");
    CHECK(NbSourcesDisagree(true, 1, true, 3), "field observation vs world-map engine test");
    CHECK(!NbSourcesDisagree(true, 2, true, 3), "agreement on the world map is not reported");
    CHECK(!NbSourcesDisagree(true, 1, true, 1), "agreement on the field is not reported");
    // Only a real disagreement counts: one source missing is not two sources
    // differing, and reporting it would cry wolf on every mid-battle injection.
    CHECK(!NbSourcesDisagree(false, -1, true, 1), "no observation is not a disagreement");
    CHECK(!NbSourcesDisagree(true, 2, false, 0), "no engine read is not a disagreement");
    CHECK(!NbSourcesDisagree(true, 6, true, 1), "an unusable observation is not a disagreement");
}

// ---------------------------------------------------------------------------
// The patch site
// ---------------------------------------------------------------------------
static void TestPatchSite()
{
    // 0x00470AB2: 66 C7 05 [C6 8F CD 01] [0B] 00
    //             ^instruction            ^the immediate's low byte, +7
    CHECK(NB_PATCH_ADDR == NB_PATCH_INSN_ADDR + 7,
          "the patched byte is the immediate, seven bytes into the instruction");
    CHECK(NB_ORIGINAL_IMM == 0x0B, "the original immediate is MODE_11, the naming screen");

    // These must match FF8Addresses::MODE_FIELD / MODE_WORLDMAP. The model
    // spells them out so it compiles alone, which is exactly how they could
    // drift apart unnoticed.
    CHECK(NB_MODE_FIELD == 1, "MODE_FIELD is 1");
    CHECK(NB_MODE_WORLDMAP == 2, "MODE_WORLDMAP is 2");
    CHECK(NB_MODE_FIELD != NB_ORIGINAL_IMM && NB_MODE_WORLDMAP != NB_ORIGINAL_IMM,
          "neither replacement is the naming mode itself");
    CHECK(NB_RETURN_KIND_ADDR == 0x01CE0758u, "the tested word is 0x01CE0758");
}

int main()
{
    TestEngineTest();
    TestHostMode();
    TestWanted();
    TestDisagree();
    TestPatchSite();
    if (g_fail == 0) printf("naming_bypass_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
