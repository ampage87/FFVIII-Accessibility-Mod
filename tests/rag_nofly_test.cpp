// rag_nofly_test.cpp -- flying round the one thing altitude cannot answer.
#include <cstdio>
#include <cmath>
#include "rag_nofly_pure.inl"

static int bad = 0;
static void chk(bool ok, const char* w) { if (!ok) { printf("  BAD: %s\n", w); bad++; } }

// The catalog's own Tears' Point marker.
static const double TX = 83021.0, TY = 31865.0, R = RAG_NOFLY_RADIUS;

int main()
{
    printf("rag_nofly_test\n");
    double ax = 0, ay = 0;

    // A route straight through the cylinder must be diverted.
    chk(RagNoFlyDetour(TX - 40000, TY, TX + 40000, TY, TX, TY, R, &ax, &ay) == true,
        "**a route straight through the Pandora is flown straight through** -- Aaron: "
        "it \"reaches up into the sky higher than Ragnarok can go\", so climbing, "
        "which answers every other obstruction on this map, cannot answer this one");
    chk(std::sqrt((ax-TX)*(ax-TX) + (ay-TY)*(ay-TY)) > R,
        "**the detour aims INSIDE the cylinder** -- aiming at a circle you are trying "
        "to miss is not a plan");

    // The side it picks is the side the route already favours.
    {
        double n1x, n1y, s1x, s1y;
        RagNoFlyDetour(TX - 40000, TY + 500, TX + 40000, TY + 500, TX, TY, R, &n1x, &n1y);
        RagNoFlyDetour(TX - 40000, TY - 500, TX + 40000, TY - 500, TX, TY, R, &s1x, &s1y);
        chk((n1y - TY) * (s1y - TY) < 0.0,
            "**both offsets divert to the same side** -- a ship already north of the "
            "Pandora should go north of it, not be sent across its face");
    }

    // Clear routes are left alone.
    chk(RagNoFlyDetour(TX - 40000, TY + 20000, TX + 40000, TY + 20000, TX, TY, R, &ax, &ay) == false,
        "**a route that misses by 20 km is diverted** -- every flight that passed "
        "anywhere near Esthar would take a detour it did not need");

    // A SEGMENT, not an infinite line: the cylinder behind you is not in the way.
    chk(RagNoFlyDetour(TX + 10000, TY, TX + 40000, TY, TX, TY, R, &ax, &ay) == false,
        "**a cylinder BEHIND the ship diverts it** -- the route is a segment; what is "
        "astern cannot be flown into");
    chk(RagNoFlyDetour(TX - 40000, TY, TX - 10000, TY, TX, TY, R, &ax, &ay) == false,
        "a cylinder beyond the destination diverts the ship");

    // Tears' Point ITSELF is a catalog destination. Flying to it must still work.
    chk(RagNoFlyDetour(TX - 40000, TY, TX, TY, TX, TY, R, &ax, &ay) == false,
        "**flying TO Tears' Point is diverted away from Tears' Point** -- the "
        "destination sits under the Pandora, and orbiting it forever is worse than "
        "flying at it");

    // Degenerate inputs must not divert or crash.
    chk(RagNoFlyDetour(TX + 5000, TY, TX + 5000, TY, TX, TY, R, &ax, &ay) == false,
        "a zero-length route diverts");
    chk(RagNoFlyDetour(0, 0, 1000, 0, TX, TY, R, nullptr, nullptr) == false,
        "a null aim pointer is written through");

    // Dead astern -- the ship exactly on the line through the centre -- must still
    // pick a side rather than dividing by nothing.
    chk(RagNoFlyDetour(TX - 40000, TY, TX + 40000, TY, TX, TY, R, &ax, &ay) == true &&
        std::sqrt((ax-TX)*(ax-TX) + (ay-TY)*(ay-TY)) > R,
        "**a route dead through the centre fails to pick a side** -- the perpendicular "
        "offset is exactly zero there and the sign has to be chosen, not derived");

    printf("rag_nofly_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
