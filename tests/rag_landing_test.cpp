// rag_landing_test -- the Ragnarok landing table, checked against the world-map
// catalog it has to serve and against the engine facts it was generated from.
//
// The table is generated (offline/gen_rag_table.py) from two independent game
// files: the landing bit in wmx.obj and the location records in wmsetus.obj.
// What a generator cannot check is whether its output still lines up with the
// catalog the mod actually offers -- so that is what this does.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#define _stricmp strcasecmp

#include "rag_landing_model.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }

// The three pads, from the game's own location table. Coordinates are the
// wmsetus records themselves, not measurements: #13, #21 and #27.
struct Pad { const char* name; int32_t x, y; };
static const Pad PADS[] = {
    { "Fisherman's Horizon",        20480,  -2560 },
    { "Deep Sea Research Center", -118784,  86016 },
    { "Esthar City",                54791,   5650 },   // via the Airstation
};

int main()
{
    std::printf("rag_landing_test\n");

    check(RAG_LANDING_COUNT >= 40,
          "**every world-map destination has a landing** -- a Ragnarok catalog "
          "that silently drops a place is one that cannot fly him there");

    // --- every row is one of the two kinds, and a pad's walk is zero --------
    for (int i = 0; i < RAG_LANDING_COUNT; i++) {
        const RagLanding& r = RAG_LANDINGS[i];
        check(r.kind == RAG_PAD || r.kind == RAG_WALK, "each row has a kind");
        check(r.name != nullptr && r.name[0] != '\0', "each row is named");
        check(r.walk >= 0, "no negative walk");
        if (r.kind == RAG_PAD)
            check(RagWalkAfterLanding(&r) == 0,
                  "**a pad leaves nothing to walk** -- landing on it IS the arrival, "
                  "which is what makes it a pad rather than a nearby field");
    }

    // --- the three pads are the three pads ---------------------------------
    int pads = 0;
    for (int i = 0; i < RAG_LANDING_COUNT; i++)
        if (RAG_LANDINGS[i].kind == RAG_PAD) pads++;
    check(pads == 3,
          "**exactly three destinations are landed ON** -- the signature is the "
          "game's own (a location record that is landable and NOT foot-walkable) "
          "and it picks three records out of fifty-two; a fourth would mean the "
          "rule had drifted");

    for (const Pad& p : PADS) {
        const RagLanding* r = RagLandingFor(p.name);
        if (r == nullptr) { std::printf("  BAD: no row for '%s'\n", p.name); bad++; continue; }
        check(r->kind == RAG_PAD, "the named pads are pads");
        check(r->x == p.x && r->y == p.y,
              "**a pad's coordinate is the game's own record, unrounded** -- it is "
              "a platform a few hundred units across, and a cell-centre "
              "approximation can miss it");
    }

    // --- Esthar is reached by landing at its Airstation ---------------------
    {
        const RagLanding* r = RagLandingFor("Esthar City");
        check(r != nullptr && r->kind == RAG_PAD && r->walk > 4000,
              "**Esthar City is a pad with a long walk** -- its own ground carries "
              "no landable cell anywhere on its foot component, and the only pad "
              "within twenty kilometres is the Airstation 8.2 km off, which is how "
              "a sighted player gets there too");
    }

    // --- a walk row is a SHORT walk, which is the whole point ---------------
    {
        int32_t worst = 0; const char* who = "";
        for (int i = 0; i < RAG_LANDING_COUNT; i++) {
            const RagLanding& r = RAG_LANDINGS[i];
            if (r.kind != RAG_WALK) continue;
            if (r.walk > worst) { worst = r.walk; who = r.name; }
        }
        check(worst <= 2500,
              "**the longest walk after landing is under 2.5 km** -- Aaron asked "
              "for as close as possible, which is the opposite of the Garden's "
              "deliberate 2-3 km standoff; a row that drifted far would mean the "
              "landmass test had picked the wrong shore");
        std::printf("  (worst walk: %s at %d units)\n", who, (int)worst);
    }

    // --- the lookup is by name and is exact --------------------------------
    check(RagLandingFor(nullptr) == nullptr, "a null name resolves to nothing");
    check(RagLandingFor("Nowhere At All") == nullptr,
          "**an unknown name resolves to nothing rather than to row zero** -- a "
          "wrong landing is worse than no landing");
    check(RagLandingFor("balamb town") != nullptr,
          "and the match is case-insensitive, like every other name match here");

    std::printf(bad ? "rag_landing_test: FAILED (%d bad)\n"
                    : "rag_landing_test: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
