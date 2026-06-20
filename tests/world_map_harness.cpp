// world_map_harness.cpp - World-map reachability regression harness.
//
// Compiles the REAL src/world_map_geometry.inl (the engine-coord -> segment
// mapping, per-vehicle traversability, and BFS reachability) against a
// committed snapshot of the terrain grid, and asserts that the Balamb
// continent's on-foot reachable set never regresses.
//
// Like tests/chase_harness.cpp, the compile itself is protection: any
// incompatible change to the geometry core's signatures breaks the build.
// The run hard-gates the Balamb invariant.
//
// Build (host): g++ -std=c++17 -O2 -o world_map_harness tests/world_map_harness.cpp

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#include "world_map_fixtures.h"

// ---- Log stub (the real Log::World logs to ff8_world.log; here it is inert).
namespace Log { inline void World(const char*, ...) {} }

namespace WorldMap {

// ---- Scaffolding the game build gets from world_map_state.inl. Kept minimal
// and matched to state.inl. (Mirrors how chase_harness.cpp declares the nav
// statics before including the real pathfinding .inl.)
enum VehicleType { VEH_ON_FOOT, VEH_CHOCOBO, VEH_CAR, VEH_GARDEN, VEH_RAGNAROK };
enum SegTerrainClass : uint8_t { SEG_LAND = 0, SEG_FOREST = 1, SEG_OCEAN = 2 };

static const int    WMX_SEG_COLS      = 32;
static const int    WMX_SEG_ROWS      = 24;
static const int    WMX_PLAYABLE_SEGS = 768;
static const double WM_WIDTH          = 262144.0;
static const double WM_HEIGHT         = 196608.0;

static uint8_t s_terrainGrid[WMX_SEG_ROWS][WMX_SEG_COLS];
static uint8_t s_reachable  [WMX_SEG_ROWS][WMX_SEG_COLS];

// ---- The real code under test.
#include "../src/world_map_geometry.inl"

} // namespace WorldMap

using namespace WorldMap;

// ---- Catalog subset (mirrors src/world_map_catalog.inl s_locations[]). We
// carry the whole table for the reachability REPORT; the hard asserts key off
// the named Balamb-continent destinations only.
struct Loc { const char* name; int32_t x; int32_t y; };
static const Loc kLocs[] = {
    {"Balamb Garden",              24576,  -29406},
    {"Balamb Town",                13249,  -26779},
    {"Dollet",                    -15639,  -39437},
    {"Timber",                    -22564,   -4867},
    {"Galbadia Garden",           -37471,  -25062},
    {"Deling City",               -61806,  -28649},
    {"Tomb of the Unknown King",  -42471,  -36562},
    {"D-District Prison",         -55306,   -4841},
    {"Galbadia Missile Base",     -71695,  -15591},
    {"Fisherman's Horizon",        48811,   -1653},
    {"Trabia Garden",              48893,  -57979},
    {"Edea's House",              -23150,   62853},
    {"White SeeD Ship",             4887,   51285},
    {"Great Salt Lake",            49888,   -2683},
    {"Esthar City",                57011,   -2295},
    {"Lunatic Pandora Lab",        79521,   -9135},
    {"Lunar Gate",                 88021,    7865},
    {"Sorceress Memorial",         81521,   11865},
    {"Shumi Village",              10362,  -76967},
    {"Winhill",                   -50285,    6320},
    {"Centra Ruins",                6887,   55285},
    {"Deep Sea Research Center", -119138,   86000},
    {"Cactuar Island",             54806,   62040},
    {"Tears' Point",               83021,   31865},
    {"Island Closest to Hell",   -105137,   -3802},
    {"Island Closest to Heaven",  102251,  -53082},
    {"Fire Cavern",                36864,  -28672},
};
static const int kLocCount = (int)(sizeof(kLocs)/sizeof(kLocs[0]));

static void LoadFixtureGrid() {
    for (int r = 0; r < WorldMapFixtures::kRows; r++) {
        const char* row = WorldMapFixtures::kTerrainRows[r];
        for (int c = 0; c < WorldMapFixtures::kCols; c++) {
            char ch = row[c];
            s_terrainGrid[r][c] = (ch == '~') ? SEG_OCEAN : (ch == 'F') ? SEG_FOREST : SEG_LAND;
        }
    }
}

static bool IsReachableLoc(int32_t x, int32_t y) {
    int col = WorldXToSegCol(x);
    int row = WorldYToSegRow(y);
    return s_reachable[row][col] != 0;
}

int main() {
    printf("=== world_map_harness: real geometry core on committed terrain grid ===\n\n");

    LoadFixtureGrid();

    // --- Scenario: player standing on the Balamb continent, on foot.
    // Seed coordinate = Balamb Town's confirmed on-the-ground entry point
    // (the refined coord hardcoded in world_map.cpp Initialize). Any cell on
    // the Balamb landmass yields the same connected reachable set.
    const int32_t seedX = 12896, seedY = -26711;
    int pCol = WorldXToSegCol(seedX);
    int pRow = WorldYToSegRow(seedY);
    printf("Balamb on-foot seed (%d,%d) -> seg(col=%d,row=%d) terrain=%d\n\n",
           seedX, seedY, pCol, pRow, s_terrainGrid[pRow][pCol]);

    ComputeReachability(pCol, pRow, VEH_ON_FOOT);

    int reach = 0;
    for (int r = 0; r < WMX_SEG_ROWS; r++)
        for (int c = 0; c < WMX_SEG_COLS; c++)
            if (s_reachable[r][c]) reach++;
    printf("BFS reachable segments: %d / %d\n\n", reach, WMX_PLAYABLE_SEGS);

    printf("Per-location reachability (Balamb on-foot):\n");
    for (int i = 0; i < kLocCount; i++) {
        int col = WorldXToSegCol(kLocs[i].x);
        int row = WorldYToSegRow(kLocs[i].y);
        uint8_t cls = s_terrainGrid[row][col];
        bool rc = s_reachable[row][col] != 0;
        printf("  %-26s (%7d,%7d) seg(col=%2d,row=%2d) cls=%s %s\n",
               kLocs[i].name, kLocs[i].x, kLocs[i].y, col, row,
               cls == SEG_OCEAN ? "ocean " : cls == SEG_FOREST ? "forest" : "land  ",
               rc ? "REACHABLE" : "-");
    }
    printf("\n");

    // --- Hard asserts: the Balamb-continent on-foot set.
    struct Req { const char* name; int32_t x; int32_t y; bool wantReachable; };
    static const Req reqs[] = {
        {"Balamb Garden", 24576, -29406, true},
        {"Balamb Town",   13249, -26779, true},
        {"Fire Cavern",   36864, -28672, true},
        {"Dollet",       -15639, -39437, false},  // on the Galbadian continent
    };
    int fails = 0;
    for (const auto& q : reqs) {
        bool rc = IsReachableLoc(q.x, q.y);
        bool ok = (rc == q.wantReachable);
        printf("  ASSERT %-14s want=%s got=%s  %s\n",
               q.name, q.wantReachable ? "reachable" : "not-reach",
               rc ? "reachable" : "not-reach", ok ? "PASS" : "*** FAIL ***");
        if (!ok) fails++;
    }

    // Exact-set guard: among the numbered catalog + Fire Cavern, ONLY the three
    // Balamb-continent destinations may be reachable on foot. Catches both a
    // dropped Balamb location AND a non-Balamb location wrongly leaking onto the
    // on-foot set (e.g. a coordinate-mapping change that connects continents
    // across the ocean). Galbadia, Trabia, Centra, Esthar etc. are all across
    // water and must stay unreachable on foot.
    int reachableLocs = 0;
    for (int i = 0; i < kLocCount; i++)
        if (IsReachableLoc(kLocs[i].x, kLocs[i].y)) reachableLocs++;
    bool countOk = (reachableLocs == 3);
    printf("  ASSERT exact-set     want=3 reachable  got=%d  %s\n",
           reachableLocs, countOk ? "PASS" : "*** FAIL ***");
    if (!countOk) fails++;
    printf("\n");

    if (fails) {
        printf("WORLDMAPGUARD: *** FAIL *** (%d Balamb-continent invariant violation(s))\n", fails);
        return 1;
    }
    printf("WORLDMAPGUARD: PASS (Balamb continent on-foot reaches Garden, Town, Fire Cavern; Dollet excluded)\n");
    return 0;
}
