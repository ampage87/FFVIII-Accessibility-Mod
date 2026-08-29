// drive_bridge_target_test.cpp -- the island-bridge redirect (#centra, v0.118.0).
//
// Each assertion was written against a mutant. The coordinates are the real
// ones from crtower2 in the 2026-08-28 log.
#include <cstdio>
#include <cmath>

#include "drive_bridge_target_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }
static void NEAR(float got, float want, const char* w)
{ if (fabsf(got - want) > 0.5f) { printf("FAIL: %s (got %.1f want %.1f)\n", w, got, want); g_fail++; } }

int main()
{
    // crtower2: player (1432,-404), bridge = "Left Ladder Up" at (1406,-384),
    // gateway = "Exit to Centra Ruins 10" at (1170,979) on the far island.
    const float BX = 1406.0f, BY = -384.0f;   // the ladder
    const float GX = 1170.0f, GY =  979.0f;   // the unreachable gateway

    float x = 0, y = 0;

    // THE BUG. Before .118 the tick loop re-derived the gateway and drove at it.
    BridgeDriveTarget(true, 1, BX, BY, GX, GY, &x, &y);
    NEAR(x, BX, "an active bridge overrides the per-tick target X");
    NEAR(y, BY, "an active bridge overrides the per-tick target Y");

    // EVERY ORDINARY DRIVE IS UNCHANGED. Kills a mutant that applies the
    // override unconditionally, which would send every drive in the game to
    // whatever stale bridge coordinates were last written.
    BridgeDriveTarget(false, -1, BX, BY, GX, GY, &x, &y);
    NEAR(x, GX, "no bridge -> the original target survives");
    NEAR(y, GY, "no bridge -> the original target survives");

    // Both halves of the guard are load-bearing. A flag set with no line index
    // (or an index with no flag) is a half-initialised state, and a drive must
    // not steer at (0,0) because of one.
    CHECK(BridgeRedirectApplies(true, 0), "line 0 is a valid bridge");
    CHECK(BridgeRedirectApplies(true, 1), "line 1 is the crtower2 ladder");
    CHECK(!BridgeRedirectApplies(true, -1), "flag without an index does not apply");
    CHECK(!BridgeRedirectApplies(false, 1), "index without the flag does not apply");
    CHECK(!BridgeRedirectApplies(false, -1), "neither does neither");
    BridgeDriveTarget(true, -1, BX, BY, GX, GY, &x, &y);
    NEAR(x, GX, "a half-initialised redirect falls through to the original");

    // A null out-pointer is a no-op rather than a fault -- the caller passes
    // the address of its own locals, but this is the kind of helper a later
    // caller passes something else to.
    BridgeDriveTarget(true, 1, BX, BY, GX, GY, nullptr, &y);
    BridgeDriveTarget(true, 1, BX, BY, GX, GY, &x, nullptr);

    // THE DEGENERATE PATH. The crtower2 bridge was 74 units away, in the
    // player's own triangle 37, and A*(37 -> 37) produced a funnelled route
    // whose first waypoint was 1149 units in the wrong direction.
    CHECK(BridgeNeedsNoPath(37, 37), "same triangle needs no path");
    CHECK(!BridgeNeedsNoPath(37, 73), "different triangles do need one");
    // Kills `startTri == bridgeTri` alone: FindNearestTriangle answers -1 when
    // it cannot place a point, and -1 == -1 must not read as "already there".
    CHECK(!BridgeNeedsNoPath(-1, -1), "two failed lookups are not the same triangle");
    CHECK(!BridgeNeedsNoPath(-1, 37), "a failed start lookup is not a match");
    CHECK(!BridgeNeedsNoPath(37, -1), "a failed bridge lookup is not a match");
    CHECK(BridgeNeedsNoPath(0, 0), "triangle 0 is a real triangle");

    printf("drive_bridge_target_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
