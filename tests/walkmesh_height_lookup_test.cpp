// walkmesh_height_lookup_test.cpp -- the 3D nearest-triangle lookup
// (#centra, v0.119.0).
//
// The numbers are read out of crtower1.id, not invented: five ladder rungs
// stacked 400 units apart in Z above the floor they hang over.
#include <cstdio>
#include <cmath>

#include "walkmesh_height_lookup_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

// crtower1, the left ladder. tris 21/23/25/27/29 are the rungs (islands 9-13,
// two triangles each); tri 88 is the floor beneath them, on the main island 3
// where the player is standing. The trigger line's own Z is 10385.
enum { N = 6 };
static const float CX[N] = { 1618, 1618, 1618, 1618, 1618, 1611 };
static const float CY[N] = { -466, -466, -466, -466, -466, -496 };
static const float CZ[N] = { 11413, 11813, 12213, 11013, 10613, 10378 };
static const int   FLOOR = 5;   // the entry that is tri 88, the walkable floor

int main()
{
    const float LX = 1617.0f, LY = -444.0f, LZ = 10385.0f;   // the ladder's line

    // THE BUG, reproduced. 2D scoring picks a rung -- it is 22 units away in
    // plan view and the floor is 52 -- and the drive then compares the player's
    // floor triangle against a triangle a thousand units in the air, correctly
    // finds them unconnected, and hunts for a bridge to somewhere it is already
    // standing next to.
    const int flat = WmPickNearest(CX, CY, CZ, N, LX, LY, LZ, false);
    CHECK(flat != FLOOR, "2D scoring picks a rung, not the floor (the bug)");
    CHECK(CZ[flat] > 10600.0f, "and the rung it picks is off the ground");

    // THE FIX. No tolerance, no weight: plain 3D distance. The rung scores
    // 22^2 + 1028^2 = 1,057,268 and the floor 52^2 + 7^2 = 2,753.
    const int solid = WmPickNearest(CX, CY, CZ, N, LX, LY, LZ, true);
    CHECK(solid == FLOOR, "3D scoring picks the floor under the ladder");

    // The margin is not marginal -- this is why no threshold is needed.
    const float rungScore  = WmSqDist3D(CX[0], CY[0], CZ[0], LX, LY, LZ);
    const float floorScore = WmSqDist3D(CX[FLOOR], CY[FLOOR], CZ[FLOOR], LX, LY, LZ);
    CHECK(floorScore * 100.0f < rungScore, "the floor beats the rung by two orders of magnitude");

    // Every rung is rejected, not just the nearest one. Kills a mutant that
    // only compares against the first candidate.
    for (int r = 0; r < FLOOR; r++) {
        CHECK(WmSqDist3D(CX[r], CY[r], CZ[r], LX, LY, LZ) > floorScore,
              "every rung scores worse than the floor");
    }

    // THE 2D FORM MUST NOT MOVE. Every caller without a height keeps it, so it
    // has to be the identical search it always was -- same metric, same
    // lowest-index tie rule.
    CHECK(WmSqDist2D(3.0f, 4.0f, 0.0f, 0.0f) == 25.0f, "2D distance is plain");
    CHECK(WmSqDist3D(3.0f, 4.0f, 12.0f, 0.0f, 0.0f, 0.0f) == 169.0f, "3D distance is plain");
    {
        // Two identical centres: the lower index wins, as the original loop's
        // strict `<` did.
        const float tx[2] = { 100, 100 }, ty[2] = { 100, 100 }, tz[2] = { 0, 0 };
        CHECK(WmPickNearest(tx, ty, tz, 2, 100, 100, 0, false) == 0, "2D ties go to the lower index");
        CHECK(WmPickNearest(tx, ty, tz, 2, 100, 100, 0, true)  == 0, "3D ties go to the lower index");
    }
    {
        // With every candidate at the same height, the 3D answer IS the 2D
        // answer -- so a flat field cannot change behaviour.
        const float fz[N] = { 0, 0, 0, 0, 0, 0 };
        CHECK(WmPickNearest(CX, CY, fz, N, LX, LY, 0.0f, true) ==
              WmPickNearest(CX, CY, fz, N, LX, LY, 0.0f, false),
              "on a flat mesh the two forms agree");
    }

    // Degenerate inputs answer "no triangle" rather than 0, which a caller
    // would use as a real triangle index.
    CHECK(WmPickNearest(CX, CY, CZ, 0, LX, LY, LZ, true) == -1, "no candidates -> -1");
    CHECK(WmPickNearest(nullptr, CY, CZ, N, LX, LY, LZ, true) == -1, "no X array -> -1");
    CHECK(WmPickNearest(CX, CY, nullptr, N, LX, LY, LZ, true) == -1, "3D without Z -> -1");
    CHECK(WmPickNearest(CX, CY, nullptr, N, LX, LY, LZ, false) >= 0, "2D without Z is fine");

    printf("walkmesh_height_lookup_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
