// rag_flight_clamp_test.cpp -- the invariant that replaces a guard per subsystem.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

struct LocationEntry { const char* name; int32_t x; int32_t y; };
#include "rag_nofly_pure.inl"

// The two lines of world_rag_drive.inl that do not need the live engine. Copied
// rather than included because RagFlightOwnsDrive reads drive state that only
// exists inside world_map.cpp -- and the CLAMP is the part with the rule in it.
static void RagFlightClampAim(int32_t px, int32_t py,
                              int32_t tgtX, int32_t tgtY,
                              int32_t* aimX, int32_t* aimY,
                              const LocationEntry* tears,
                              int32_t* pathLen);
#define FF8OPC_RAG_CLAMP_TEST 1
#include "world_rag_drive_clamp_body.inl"

static int bad = 0;
static void chk(bool ok, const char* w) { if (!ok) { printf("  BAD: %s\n", w); bad++; } }

static const LocationEntry TEARS = { "Tears' Point", 83021, 31865 };

int main()
{
    printf("rag_flight_clamp_test\n");
    int32_t ax, ay, len;

    // The 18:58 BAT: the aim was a fine-grid waypoint 180 units ahead of a ship
    // 28 km from its destination, because a walking path had leaked in.
    ax = 81984; ay = 11712; len = 302;
    RagFlightClampAim(82117, 11779, 54791, 5650, &ax, &ay, &TEARS, &len);
    chk(ax == 54791 && ay == 5650,
        "**a leaked aim survives the clamp** -- the 18:58 BAT chased fine-grid "
        "waypoints 180 units ahead of itself for a minute and stalled 24 km short "
        "of Esthar, and the aim came from a 302-cell WALKING path");
    chk(len == 0,
        "**a flying drive keeps its path** -- the airship has nothing to route "
        "around, so a non-empty path can only be a walking route that leaked in");

    // Whatever moved it -- escape, bridge, LOS clamp, corner cap, sweep, or the
    // one nobody has found yet -- is undone by the same line.
    ax = -99999; ay = 42; len = 7;
    RagFlightClampAim(82117, 11779, 54791, 5650, &ax, &ay, nullptr, &len);
    chk(ax == 54791 && ay == 5650 && len == 0,
        "**an aim from an unknown subsystem survives** -- the whole point is that a "
        "subsystem nobody has found yet is covered by the same line as the four "
        "that have been");

    // The ONE override the airship keeps: the Lunatic Pandora, which reaches
    // higher than the ship can climb and so cannot be answered by going over it.
    ax = 0; ay = 0; len = 0;
    {
        const int32_t tgx = TEARS.x + 40000, tgy = TEARS.y;
        RagFlightClampAim((int32_t)(TEARS.x - 40000), TEARS.y, tgx, tgy,
                          &ax, &ay, &TEARS, &len);
        const double d = std::sqrt((double)(ax-TEARS.x)*(ax-TEARS.x) +
                                   (double)(ay-TEARS.y)*(ay-TEARS.y));
        // The aim must be the RIM, not the destination -- checking only that it is
        // "outside the radius" passes trivially, since the destination is 40 km
        // away. This assertion failed to kill its mutant on the first attempt for
        // exactly that reason.
        chk((ax != tgx || ay != tgy) && d > RAG_NOFLY_RADIUS && d < RAG_NOFLY_RADIUS * 2.0,
            "**the clamp drops the Pandora detour** -- it is the one aim the airship "
            "owns, because the Pandora reaches higher than the ship can climb and is "
            "the only thing on this map that cannot be answered by going over it");
    }

    // ...and it does not invent a detour when none is needed.
    ax = 0; ay = 0;
    RagFlightClampAim(82117, 11779, 54791, 5650, &ax, &ay, &TEARS, nullptr);
    chk(ax == 54791 && ay == 5650,
        "**a route nowhere near the Pandora is diverted** -- every flight past "
        "Esthar would take a detour it did not need");

    // Null-safe: a caller with no path to clear must not crash.
    ax = 1; ay = 2;
    RagFlightClampAim(0, 0, 100, 200, &ax, &ay, nullptr, nullptr);
    chk(ax == 100 && ay == 200, "a null pathLen breaks the aim clamp");

    printf("rag_flight_clamp_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
