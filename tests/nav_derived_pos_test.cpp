// nav_derived_pos_test.cpp -- positions derived from a field's own script
// (#derived-pos, v0.101.0).
//
// Compiles src/field_nav_derived_pos.inl standalone. Every assertion was written
// against a mutant; the note on each says which change it kills.
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "field_nav_derived_pos.inl"

static int g_fail = 0;
static void CHECK(bool cond, const char* what)
{
    if (!cond) { printf("FAIL: %s\n", what); g_fail++; }
}

static void TestLookup()
{
    const NavDerivedPos* r = NavDerivedPosFor(846, "BossBattle");
    CHECK(r != nullptr, "sdcore1's BossBattle has a derived position");
    if (r) {
        // The coordinate all five `hanno` methods turn to face.
        CHECK(r->x == -250 && r->y == -1161, "the light is at (-250,-1161)");
    }

    // The .sym's case is not something to depend on.
    CHECK(NavDerivedPosFor(846, "bossbattle") != nullptr, "the sym match is case-insensitive");
    CHECK(NavDerivedPosFor(846, "BOSSBATTLE") != nullptr, "in both directions");

    // THE FIELD ID IS PART OF THE KEY. "BossBattle" is a plausible symbol in any
    // field with a boss in it, and a coordinate from the wrong room would put a
    // catalog entry somewhere the player cannot walk. Kills the mutant that
    // matches on the name alone.
    CHECK(NavDerivedPosFor(847, "BossBattle") == nullptr, "a different field does not match");
    CHECK(NavDerivedPosFor(0, "BossBattle") == nullptr, "and neither does field 0");

    CHECK(NavDerivedPosFor(846, "Koe") == nullptr, "an unlisted entity does not match");
    CHECK(NavDerivedPosFor(846, "") == nullptr, "an empty sym does not match");
    CHECK(NavDerivedPosFor(846, nullptr) == nullptr, "a null sym does not match");

    // A prefix is not a match, in either direction -- `NavDerivedSymEq` has to
    // check that BOTH strings ended.
    CHECK(NavDerivedPosFor(846, "Boss") == nullptr, "a prefix of the sym does not match");
    CHECK(NavDerivedPosFor(846, "BossBattle2") == nullptr, "a longer sym does not match");
}

static void TestApply()
{
    const NavDerivedPos* r = NavDerivedPosFor(846, "BossBattle");

    // THE FILE ALWAYS WINS. SET3 and the .inf are the engine's own numbers; this
    // table stands in for numbers that are missing. Kills the mutant that drops
    // the guard and lets a derived row overwrite a real position.
    CHECK(!NavDerivedShouldApply(true, r), "an entity that already has a position is left alone");
    CHECK(NavDerivedShouldApply(false, r), "an entity with no position gets the derived one");

    // Nothing to apply is nothing to apply.
    CHECK(!NavDerivedShouldApply(false, nullptr), "no row means no write");
    CHECK(!NavDerivedShouldApply(true, nullptr), "no row means no write, positioned or not");
}

static void TestTable()
{
    CHECK(NAV_DERIVED_POS_COUNT >= 1, "the table is not empty");
    for (int i = 0; i < NAV_DERIVED_POS_COUNT; i++) {
        CHECK(NAV_DERIVED_POS[i].sym != nullptr && NAV_DERIVED_POS[i].sym[0] != '\0',
              "every row names an entity");
        CHECK(NAV_DERIVED_POS[i].fieldId != 0, "every row names a field");
        // (0,0) is what an unpositioned entity already reads as, so a row that
        // says (0,0) is a row that does nothing while looking like it does.
        CHECK(!(NAV_DERIVED_POS[i].x == 0 && NAV_DERIVED_POS[i].y == 0),
              "no row is the origin");
    }
    // Two rows for the same (field, sym) would make the lookup order-dependent.
    for (int i = 0; i < NAV_DERIVED_POS_COUNT; i++)
        for (int j = i + 1; j < NAV_DERIVED_POS_COUNT; j++)
            CHECK(!(NAV_DERIVED_POS[i].fieldId == NAV_DERIVED_POS[j].fieldId &&
                    NavDerivedSymEq(NAV_DERIVED_POS[i].sym, NAV_DERIVED_POS[j].sym)),
                  "no two rows share a (field, entity) key");
}

int main()
{
    TestLookup();
    TestApply();
    TestTable();
    if (g_fail == 0) printf("nav_derived_pos_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
