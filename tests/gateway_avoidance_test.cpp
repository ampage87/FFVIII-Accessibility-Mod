// gateway_avoidance_test.cpp -- INF gateways as pathfinding walls (#shumi,
// v0.132.0). The case is tmmura1: the only walkable route from the player's
// triangle to the Moomba's crosses the Village 3 gateway, and A* used to take it.
#include <cstdio>

#include "gateway_avoidance_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // tmmura1 has three gateways: 0 -> Village 3, 1 -> Residence 1, 2 -> Village 1.
    // Driving to the Moomba is not driving to any of them, so all three are walls
    // and A* has to find a route that stays in Village 2 -- or report that there
    // isn't one, which is a thing Aaron can act on.
    CHECK(GatewayIsPathBarrier(0, -1), "with no gateway target, gateway 0 is a wall");
    CHECK(GatewayIsPathBarrier(1, -1), "and so is 1");
    CHECK(GatewayIsPathBarrier(2, -1), "and so is 2");

    // Driving TO the Village 3 gateway must still work. This is the assertion
    // that stops the fix breaking every exit in the game: without the exemption
    // the drive plans around its own destination and finds no path.
    CHECK(!GatewayIsPathBarrier(0, 0), "the gateway being driven to is not a wall to itself");
    CHECK( GatewayIsPathBarrier(1, 0), "but its neighbours still are");
    CHECK( GatewayIsPathBarrier(2, 0), "all of them");

    // A gateway with no resolvable destination is still a wall. Crossing it still
    // changes the field; not knowing where to is a reason for more care, not less.
    CHECK(GatewayIsPathBarrier(5, 3), "an unrelated gateway is a wall whatever its destination");
    CHECK(!GatewayIsPathBarrier(-1, -1), "and a non-gateway index is not a gateway");

    // DEGENERATE LINES MUST NOT BECOME WALLS. A zero-length segment at the origin
    // would fence off whatever part of the mesh lies across (0,0) -- a silent
    // "no path" on a field with nothing wrong with it.
    CHECK(!GatewayLineIsUsable(0, 0, 0, 0),       "an unfilled gateway line is not a wall");
    CHECK(!GatewayLineIsUsable(315, -2739, 315, -2739), "nor is a zero-length one");
    CHECK(GatewayLineIsUsable(36, -2401, 594, -3077),
          "tmmura1's Village 3 gateway is a real segment and IS a wall");
    CHECK(GatewayLineIsUsable(0, 0, 594, -3077),
          "a line that merely starts at the origin is still a real line");
    CHECK(GatewayLineIsUsable(-66, -459, 82, -459),
          "and so is tmmura1's Residence 1 gateway");

    if (g_fail == 0) printf("gateway_avoidance_test: all checks passed\n");
    return g_fail ? 1 : 0;
}
