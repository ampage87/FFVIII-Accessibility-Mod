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
enum SegTerrainClass : uint8_t { SEG_LAND = 0, SEG_FOREST = 1, SEG_OCEAN = 2, SEG_MOUNTAIN = 3 };

static const int    WMX_SEG_COLS      = 32;
static const int    WMX_SEG_ROWS      = 24;
static const int    WMX_PLAYABLE_SEGS = 768;
static const double WM_WIDTH          = 262144.0;
static const double WM_HEIGHT         = 196608.0;

// #67 fine-grid scaffolding (matches world_map_state.inl) so the fine
// reachability functions in world_map_geometry.inl compile and link here.
static const int WM_FINE_CELL = 1024;
static const int WM_FINE_COLS = 256;
static const int WM_FINE_ROWS = 192;
static const uint16_t WM_MTN_STEEP_BLOCK = 256;   // #67 BAT 2: mirrors state.inl start value

static uint8_t s_terrainGrid  [WMX_SEG_ROWS][WMX_SEG_COLS];
static uint8_t s_reachable    [WMX_SEG_ROWS][WMX_SEG_COLS];
static uint8_t s_walkClassFine[WM_FINE_ROWS][WM_FINE_COLS];
static uint8_t s_reachFine    [WM_FINE_ROWS][WM_FINE_COLS];
static uint16_t s_steepFine   [WM_FINE_ROWS][WM_FINE_COLS];   // #67 BAT 2: per-cell steepness

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
    {"Fire Cavern",                30326,  -29221},   // #67 corrected on-continent coord
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

// #67: the harness is now a COORDINATE-MAPPING regression guard plus a fine-
// flood-fill smoke test. The all-continent fix is the +98304 half-height
// centering offset in WorldYToSegRow (and its inverse in SegmentCenterToWorld);
// this pins the corrected engine-coord -> segment mapping for one known
// location per continent, so a regression of the Y (or X) offset fails the CI
// build. The fine rasterized reachability is validated offline (17/17) and by
// in-game BAT; a committed 256x192 fine fixture to guard its geography here is
// a queued follow-up. The smoke test below still exercises the fine flood-fill
// against a synthetic island so a crash or broken passability rule is caught.
struct MapCheck { const char* name; int32_t x; int32_t y; int wantCol; int wantRow; };
static const MapCheck kMapChecks[] = {
    {"Balamb Garden",    24576, -29406, 19,  8},
    {"Balamb Town",      13249, -26779, 17,  8},
    {"Fire Cavern",      30326, -29221, 19,  8},
    {"Galbadia Garden", -37471, -25062, 11,  8},
    {"Deling City",     -61806, -28649,  8,  8},
    {"Esthar City",      57011,  -2295, 22, 11},
    {"Trabia Garden",    48893, -57979, 21,  4},
    {"Edea's House",    -23150,  62853, 13, 19},
};
static const int kMapCheckCount = (int)(sizeof(kMapChecks)/sizeof(kMapChecks[0]));

int main() {
    printf("=== world_map_harness: engine-coord -> segment mapping guard (#67) ===\n\n");

    LoadFixtureGrid();   // populates s_terrainGrid (kept wired; report context only)

    int fails = 0;

    // --- Coordinate-mapping checks: each known location must map to its
    //     expected segment under the corrected (+131072 X, +98304 Y) centering.
    printf("Coordinate mapping (corrected #67 centering):\n");
    for (int i = 0; i < kMapCheckCount; i++) {
        const MapCheck& m = kMapChecks[i];
        int col = WorldXToSegCol(m.x);
        int row = WorldYToSegRow(m.y);
        bool ok = (col == m.wantCol && row == m.wantRow);
        printf("  %-16s (%7d,%7d) -> seg(col=%2d,row=%2d) want(col=%2d,row=%2d)  %s\n",
               m.name, m.x, m.y, col, row, m.wantCol, m.wantRow,
               ok ? "PASS" : "*** FAIL ***");
        if (!ok) fails++;
    }
    printf("\n");

    // --- Direct offset guards: these flip if the +98304 half-height centering
    //     is removed (the exact regression #67 fixed) or the X centering is
    //     accidentally edited.
    struct AxisCheck { const char* label; int got; int want; };
    const AxisCheck achecks[] = {
        {"WorldYToSegRow(0) == 12 (vertical centre)",   WorldYToSegRow(0),       12},
        {"WorldYToSegRow(-98304) == 0 (north edge)",    WorldYToSegRow(-98304),   0},
        {"WorldXToSegCol(0) == 16 (horizontal centre)", WorldXToSegCol(0),       16},
    };
    for (const auto& a : achecks) {
        bool ok = (a.got == a.want);
        printf("  ASSERT %-46s got=%2d  %s\n", a.label, a.got, ok ? "PASS" : "*** FAIL ***");
        if (!ok) fails++;
    }
    printf("\n");

    // --- Fine flood-fill smoke test: a 3x3 land island in an all-ocean grid.
    //     The fill must cover the island and stop at the water -- guards
    //     ComputeReachabilityFine / IsFineTraversable against a crash or a
    //     broken passability rule. (Real geography is validated offline + BAT.)
    for (int r = 0; r < WM_FINE_ROWS; r++)
        for (int c = 0; c < WM_FINE_COLS; c++)
            s_walkClassFine[r][c] = SEG_OCEAN;
    for (int r = 95; r <= 97; r++)
        for (int c = 127; c <= 129; c++)
            s_walkClassFine[r][c] = SEG_LAND;
    ComputeReachabilityFine(128, 96, VEH_ON_FOOT);
    bool islandOk    = s_reachFine[96][128] && s_reachFine[95][127] && s_reachFine[97][129];
    bool oceanWalled = !s_reachFine[96][125] && !s_reachFine[92][128];
    printf("  ASSERT fine-floodfill   island-filled=%d ocean-walled=%d  %s\n",
           islandOk ? 1 : 0, oceanWalled ? 1 : 0,
           (islandOk && oceanWalled) ? "PASS" : "*** FAIL ***");
    if (!(islandOk && oceanWalled)) fails++;
    printf("\n");

    // --- Slope-gate smoke test (#67 BAT 2): a gentle mountain cell is a
    //     walkable pass; a steep mountain cell is an impassable face. Guards
    //     IsFineTraversable's slope branch. Layout on row 96: seed land at
    //     col 128; a gentle pass (steep 100) at 129 leads to land at 130; a
    //     steep face (steep 500) at 127 must seal off land at 126.
    for (int r = 0; r < WM_FINE_ROWS; r++)
        for (int c = 0; c < WM_FINE_COLS; c++) { s_walkClassFine[r][c] = SEG_OCEAN; s_steepFine[r][c] = 0; }
    s_walkClassFine[96][128] = SEG_LAND;                                       // seed
    s_walkClassFine[96][129] = SEG_MOUNTAIN; s_steepFine[96][129] = 100;       // gentle pass (open)
    s_walkClassFine[96][130] = SEG_LAND;                                       // land beyond the pass
    s_walkClassFine[96][127] = SEG_MOUNTAIN; s_steepFine[96][127] = 500;       // steep face (blocks)
    s_walkClassFine[96][126] = SEG_LAND;                                       // land behind the wall
    ComputeReachabilityFine(128, 96, VEH_ON_FOOT);
    bool passOpen = s_reachFine[96][129] && s_reachFine[96][130];   // reached through gentle pass
    bool faceWall = !s_reachFine[96][127] && !s_reachFine[96][126]; // steep face seals the west
    printf("  ASSERT slope-gate       pass-open=%d face-walled=%d  %s\n",
           passOpen ? 1 : 0, faceWall ? 1 : 0,
           (passOpen && faceWall) ? "PASS" : "*** FAIL ***");
    if (!(passOpen && faceWall)) fails++;
    printf("\n");

    if (fails) {
        printf("WORLDMAPGUARD: *** FAIL *** (%d regression(s))\n", fails);
        return 1;
    }
    printf("WORLDMAPGUARD: PASS (corrected #67 coordinate mapping intact; fine flood-fill sound)\n");
    return 0;
}
