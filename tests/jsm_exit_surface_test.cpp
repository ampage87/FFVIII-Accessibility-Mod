// jsm_exit_surface_test.cpp -- which script-derived exits reach the catalog
// (#centra, v0.131.7).
//
// The fixture is crtower2's catalog as the 22:17:00 log printed it: one doorway
// listed four times, the fourth of which refused the drive four times over.
#include <cstdio>

#include "jsm_exit_surface_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // THE REAL DOORWAY. crtower2's exit to Centra Ruins 8 sits at (1451,-383) on
    // triangle 37. Whatever else is filtered, this must survive -- an exit the
    // player cannot see is worse than a duplicate.
    CHECK(JsmExitShouldSurface(false, 37, 1451, -383),
          "a placed, non-party exit is a real exit");

    // THE THREE DUPLICATES. squall, zell, irvine, rinoa, selphie and quistis all
    // carry an identical copy of the MAPJUMP, because that is the code that
    // moves whichever of them is walking. The ones in the active party are
    // already filtered; the ones who are absent were not, so a character who is
    // not even in the game contributed an exit.
    CHECK(!JsmExitShouldSurface(true, 37, 1451, -383),
          "a party character's own transition code is not a doorway");
    CHECK(!JsmExitShouldSurface(true, 0, 0, 0),
          "and it stays not-a-doorway when it has no position either");

    // THE FOURTH ENTRY. director0's MAPJUMP is real code and it is nowhere:
    // pos=(0,0), no triangle. The drive REFUSED it -- "target validation failed:
    // catIdx=8/11 entityIdx=-317" -- so listing it offers something that cannot
    // work, and the only way for a blind player to discover that is to try it.
    CHECK(!JsmExitShouldSurface(false, 0, 0, 0),
          "an exit with no placement at all is dropped, not offered");
    CHECK(!JsmExitHasPlacement(0, 0, 0), "no triangle and no coordinates is no placement");

    // BUT (0,0) IS A LEGAL COORDINATE. A field whose origin is walkable must not
    // lose its exit, so the triangle is what decides when there is one.
    CHECK(JsmExitHasPlacement(12, 0, 0), "a triangle at the origin is still a placement");
    CHECK(JsmExitShouldSurface(false, 12, 0, 0), "and that exit surfaces");
    // And an entity the walkmesh has not placed can still be somewhere the
    // catalog knows about, which is enough to walk to.
    CHECK(JsmExitHasPlacement(0, 1451, -383), "coordinates without a triangle are a placement");
    CHECK(JsmExitShouldSurface(false, 0, 1451, -383), "and that exit surfaces too");

    // The four-entry fixture in one assertion: of crtower2's four listings for
    // one doorway, exactly one survives.
    {
        struct Row { bool party; int tri, x, y; };
        const Row rows[] = {
            { true,  37, 1451, -383 },   // cat5  irvine
            { true,  37, 1451, -383 },   // cat6  selphie
            { true,  37, 1451, -383 },   // cat7  quistis
            { false,  0,    0,    0 },   // cat8  director0 -- the one that REFUSED
            { false, 37, 1451, -383 },   // the doorway itself
        };
        int kept = 0;
        for (int i = 0; i < 5; i++)
            if (JsmExitShouldSurface(rows[i].party, rows[i].tri, rows[i].x, rows[i].y)) kept++;
        CHECK(kept == 1, "one doorway, one catalog entry");
    }

    printf("jsm_exit_surface_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
