// Feeds the real wmx.obj through the SAME polygon loop world_map_segments.inl
// uses, into the real Garden_RasterizeTri, then dumps the resulting grid so it
// can be diffed against the independently-written Python model. This is the
// check that the mesh-space transform (mz = 196608 - vwy) is right.
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <queue>

typedef unsigned long DWORD;
namespace Log { void World(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); } }
namespace FieldDialog { bool IsDialogOpen() { return false; } }   // v0.20.87
namespace ScreenReader { bool Speak(const char*, bool = false) { return true; } }
static DWORD GetTickCount() { return 0; }
static const double WM_WIDTH = 262144.0, WM_HEIGHT = 196608.0;
static const int MAX_LOCATIONS = 64;
struct LocationEntry { const char* name; int32_t x; int32_t y; };
static LocationEntry s_catalog[MAX_LOCATIONS];
static int s_catalogCount = 0, s_catalogIndex = 0;
static bool s_catalogBuilt = false;
static const int LOCATION_COUNT = 39;
static const int DRIVE_PATH_MAX = 768;
static uint16_t s_drivePath[DRIVE_PATH_MAX];
static int32_t s_drivePathWX[DRIVE_PATH_MAX], s_drivePathWY[DRIVE_PATH_MAX];
static bool s_drivePathWorld = false, s_drivePathPlanned = false;
static int s_drivePathLen = 0, s_drivePathIdx = 0;
static uint8_t g_fakeSavemap[64];
static const uintptr_t WM_BGU_POS_ADDR  = (uintptr_t)g_fakeSavemap;
static const uintptr_t WM_CHAR_POS_ADDR = (uintptr_t)(g_fakeSavemap + 16);
static bool WmSafeReadBytes(uintptr_t a, void* o, size_t n) { memcpy(o, (const void*)a, n); return true; }
static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z) { *x = *y = *z = 0; }
static bool s_catalogBuilt2 = false;
static const uint32_t WMS_VEHPOS_X_OFF = 0, WMS_VEHPOS_Y_OFF = 4, WMS_VEHPOS_Z_OFF = 8, WMS_VEHPOS_ROT_OFF = 10;
static inline uint16_t PackSeg(int r, int c) { return (uint16_t)(((r & 0xFF) << 8) | (c & 0xFF)); }
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = fabs((double)(x2 - x1)), dy = fabs((double)(y2 - y1));
    if (dx > WM_WIDTH / 2) dx = WM_WIDTH - dx;
    if (dy > WM_HEIGHT / 2) dy = WM_HEIGHT - dy;
    return sqrt(dx * dx + dy * dy);
}
static int TorusBearing(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    double r = atan2((double)(bx - ax), -(double)(by - ay));
    if (r < 0) r += 6.283185307179586;
    return (int)(r / 6.283185307179586 * 4096.0) & 0xFFF;
}
static void GetWorldMapPosition_Active(int32_t* x, int32_t* y, int32_t* z) { *x = *y = *z = 0; }
static uint16_t GetWorldMapHeading() { return 0; }
static int GetActiveVehicleId() { return -1; }
static void SetDriveKeys(bool, bool, bool, bool = false) {}
static void ReleaseAllDriveKeys() {}
struct GardenPark { const char* name; int32_t park_x, park_y, walk_units; bool reachable; bool drive_in; int32_t dock_x, dock_y; bool beach_climb; };
static const GardenPark* Garden_ParkFor(const char* name);
static void Garden_ComputeReach(int32_t, int32_t);
static bool Garden_CellReachable(int32_t, int32_t);
#include <cstdarg>
#include <chrono>
// v0.20.64: world_map_state.inl symbols the trigger-state diagnostic reads.
static const uint32_t WM_STORY_FLAG = 0x02036BDE;
static const int WMX_SEG_ROWS = 24, WMX_SEG_COLS = 32;
static uint8_t s_segmentRegionMap[WMX_SEG_ROWS][WMX_SEG_COLS];
static bool s_segmentRegionLoaded = false;
#include "wm_distance_pure.inl"
#include "world_garden_dump.inl"
#include "world_garden_grid.inl"
#include "world_garden_berths.inl"
#include "world_garden_inlets.inl"
#include "world_garden_plan.inl"
#include "world_garden_probe.inl"
#include "world_garden.inl"

static const uint32_t WMX_SEGMENT_SIZE = 36864;

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "/root/work/wmx.obj";
    FILE* f = fopen(path, "rb");
    if (!f) { printf("no wmx\n"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* wmx = (uint8_t*)malloc(sz);
    if (fread(wmx, 1, sz, f) != (size_t)sz) { printf("short read\n"); return 1; }
    fclose(f);

    Garden_BuildBegin();
    for (int seg = 0; seg < 768; seg++) {
        int row = seg / 32, col = seg % 32;
        const uint8_t* segData = wmx + (uint32_t)seg * WMX_SEGMENT_SIZE;
        for (int b = 0; b < 16; b++) {
            uint32_t blockOffset = *(const uint32_t*)(segData + 4 + b * 4);
            if (blockOffset == 0 || blockOffset + 4 > WMX_SEGMENT_SIZE) continue;
            const uint8_t* blockBase = segData + blockOffset;
            uint8_t polyCount = blockBase[0], vertCount = blockBase[1];
            uint32_t polyArrayEnd = blockOffset + 4 + (uint32_t)polyCount * 16;
            if (polyArrayEnd > WMX_SEGMENT_SIZE) continue;
            int bRow = b / 4, bCol = b % 4;
            int32_t ox = (int32_t)col * 8192 + bCol * 2048;
            int32_t oy = (int32_t)row * 8192 + bRow * 2048;
            const uint8_t* vertBase = blockBase + 4 + (uint32_t)polyCount * 16;
            if (polyArrayEnd + (uint32_t)vertCount * 8 > WMX_SEGMENT_SIZE) continue;
            static int32_t vwx[256], vwy[256];
            static int16_t vwz[256];
            for (int v = 0; v < vertCount; v++) {
                int16_t lvx = *(const int16_t*)(vertBase + v * 8 + 0);
                int16_t lvy = *(const int16_t*)(vertBase + v * 8 + 4);
                vwz[v] = *(const int16_t*)(vertBase + v * 8 + 2);
                vwx[v] = ox + lvx;
                vwy[v] = oy - lvy;
            }
            for (int p = 0; p < polyCount; p++) {
                const uint8_t* poly = blockBase + 4 + p * 16;
                Garden_FeedPoly(poly, vwx, vwy, vwz, vertCount);
            }
        }
    }
    Garden_BuildEnd();

    FILE* o = fopen("/root/work/gcheck/gdcls.bin", "wb");
    fwrite(s_gdCls, 1, (size_t)GD_ROWS * GD_COLS, o);
    fclose(o);
    o = fopen("/root/work/gcheck/gdh.bin", "wb");
    fwrite(s_gdH, 2, (size_t)GD_ROWS * GD_COLS, o);
    fclose(o);
    // v0.20.85: the clearance field too. The .84 replay found the C++ and the
    // offline model disagreeing on it at the pole and neither side was guarded.
    o = fopen("/root/work/gcheck/gdclear.bin", "wb");
    fwrite(s_gdClear, 1, (size_t)GD_ROWS * GD_COLS, o);
    fclose(o);

    // sanity: reachability from Balamb Garden, and every park point
    Garden_OnWorldMapEntry();
    Garden_UpdateAboard();
    printf("aboard after entry classify = %d\n", (int)Garden_IsAboard());
    Garden_ComputeReach(24576, -29406);
    int ok = 0, bad = 0;
    for (int i = 0; i < GARDEN_PARK_COUNT; i++) {
        const GardenPark& g = s_gardenParks[i];
        if (!g.reachable) continue;
        // v0.20.89: the helper arms the beach exception itself, so this asks the
        // question exactly the way the catalog and the drive now ask it.
        if (Garden_BerthReachable(&g)) ok++;
        else { bad++; printf("  UNREACHABLE from Balamb: %s (%d,%d)\n", g.name, g.park_x, g.park_y); }
    }
    printf("park points reachable from Balamb Garden: %d ok, %d bad\n", ok, bad);

    // v0.20.89 REGRESSION GUARD. The .88 bug was one call site asking the raw
    // question instead of the berth question: the catalog called
    // Garden_CellReachable directly, the beach exception was not in force, and
    // Shumi Village was hidden from the destination list it had just been added
    // to. Pin the semantics so the difference is visible rather than incidental.
    {
        int climbers = 0;
        for (int i = 0; i < GARDEN_PARK_COUNT; ++i) if (s_gardenParks[i].beach_climb) climbers++;
        printf("beach_climb berths in the table: %d\n", climbers);
        for (int i = 0; i < GARDEN_PARK_COUNT; ++i) {
            const GardenPark& g = s_gardenParks[i];
            if (!g.beach_climb) continue;
            s_gdBeachGoalIdx = -1;
            const bool raw   = Garden_CellReachable(g.park_x, g.park_y);
            const bool berth = Garden_BerthReachable(&g);
            printf("beach_climb semantics: %s raw=%d berth=%d  %s\n", g.name, (int)raw, (int)berth,
                   (!raw && berth) ? "OK (helper is required -- use Garden_BerthReachable)"
                                   : "*** UNEXPECTED ***");
            if (raw || !berth) bad++;
        }
    }

    // v0.51.0 (#109): THE WHITE SEED SHIP IS A DRIVE-IN ON OPEN WATER.
    //
    // Its berth is the marker, wmsetus record 17, and the marker is deep ocean.
    // Three things have to hold and all three are asked of the REAL grid built
    // above from the real wmx.obj, not of the offline model:
    //   1. the cell is Garden-traversable at all (it is water the hull may
    //      occupy: byte 0x0F bit 5 set, bit 7 clear),
    //   2. it is in the flood from the places Aaron actually starts -- his
    //      Garden's position in the current save, and both starts from the
    //      2026-08-21 BAT that failed,
    //   3. the planner returns a route, not just a reachability yes.
    // The old berth (2944,53376) satisfied 1-3 too, which is the point: it was
    // reachable and it was the wrong place. See world_catalog.inl.
    {
        const GardenPark* ws = Garden_ParkFor("White SeeD Ship");
        if (!ws || !ws->reachable || !ws->drive_in || ws->walk_units != 0) {
            printf("White SeeD Ship berth is not a reachable drive_in *** UNEXPECTED ***\n");
            bad++;
        } else {
            static const struct { int32_t x, y; const char* label; } WS_START[] = {
                { -20798,  64150, "his Garden now (slot2_save05)" },
                { -21020,  63113, "BAT 23:10 start" },
                {  17890,  40048, "BAT 23:11 start" },
                {  20271, -24355, "Balamb" },
            };
            for (unsigned k = 0; k < sizeof(WS_START) / sizeof(WS_START[0]); k++) {
                s_gdBeachGoalIdx = -1;
                Garden_ComputeReach(WS_START[k].x, WS_START[k].y);
                const bool reach = Garden_BerthReachable(ws);
                const bool okPlan = reach && Garden_Plan(WS_START[k].x, WS_START[k].y,
                                                         ws->park_x, ws->park_y);
                printf("White SeeD Ship from %-30s reach=%d plan=%s (%d waypoints)  %s\n",
                       WS_START[k].label, (int)reach, okPlan ? "OK" : "FAILED",
                       s_drivePathLen, (reach && okPlan) ? "OK" : "*** FAIL ***");
                if (!reach || !okPlan) bad++;
            }
        }
    }

    // v0.52.0 (#109): A SHORELINE PATROL NEEDS A SHORELINE.
    //
    // The 2026-08-21 BAT drove the hull onto the White SeeD Ship's marker and
    // then "searched the shore" of a bay it was floating in the middle of --
    // ninety seconds of `dist to White SeeD Ship 5` with the heading spinning.
    // GdDockIsAfloat is what stops that, and it has to say yes for the ship and
    // no for the one other drive_in, which docks against land.
    {
        const GardenPark* ws = Garden_ParkFor("White SeeD Ship");
        const bool offWhenNotDriveIn = GdDockIsAfloat(false, ws->park_x, ws->park_y);
        printf("GdDockIsAfloat is a drive_in question only: %d  %s\n",
               (int)offWhenNotDriveIn, !offWhenNotDriveIn ? "OK" : "*** FAIL ***");
        if (offWhenNotDriveIn) bad++;

        const bool shipAfloat = GdDockIsAfloat(true, ws->park_x, ws->park_y);
        printf("White SeeD Ship dock (%d,%d) is open water: %d  %s\n",
               ws->park_x, ws->park_y, (int)shipAfloat, shipAfloat ? "OK" : "*** FAIL ***");
        if (!shipAfloat) bad++;

        // The other drive_in presses at a coordinate on LAND -- Mobile Galbadia
        // Garden, terrain 7 -- and must NOT be called afloat, so nothing about
        // the shoreline patrol changes for it.
        const GardenPark* mg = Garden_ParkFor("Mobile Galbadia Garden");
        const bool mgAfloat = GdDockIsAfloat(true, mg->park_x, mg->park_y);
        printf("Mobile Galbadia Garden dock (%d,%d) is open water: %d  %s\n",
               mg->park_x, mg->park_y, (int)mgAfloat, !mgAfloat ? "OK (still patrols)" : "*** FAIL ***");
        if (mgAfloat) bad++;

        // And the failure says what the player can do about it.
        const char* hint = GdMissingHint("White SeeD Ship");
        const bool hintOk = hint && strstr(hint, "Edea") != nullptr;
        printf("missing hint names the prerequisite: \"%s\"  %s\n",
               hint ? hint : "(null)", hintOk ? "OK" : "*** FAIL ***");
        if (!hintOk) bad++;
    }

    // v0.53.0 (#109): EVERY INLET IN THE SWEEP MUST BE REACHABLE AND PLANNABLE.
    //
    // A sweep site the hull cannot get to is a stop that fails for the wrong
    // reason, and forty-one of them is too many to eyeball. Asked of the REAL
    // grid, from the Garden's position in the current save.
    {
        s_gdBeachGoalIdx = -1;
        Garden_ComputeReach(-20798, 64150);
        int okI = 0, badI = 0;
        for (int i = 0; i < WHITE_SEED_INLET_COUNT; i++) {   // includes site 0
            const GardenDock& d = s_whiteSeedInlets[i];
            const bool reach = Garden_CellReachable(d.approach_x, d.approach_y);
            const bool plan  = reach && Garden_Plan(-20798, 64150, d.approach_x, d.approach_y);
            if (reach && plan) okI++;
            else { badI++; printf("  inlet %d (%d,%d) reach=%d plan=%d *** FAIL ***\n",
                                  i, d.approach_x, d.approach_y, (int)reach, (int)plan); }
        }
        printf("Centra inlet sweep: %d of %d reachable and plannable  %s\n",
               okI, WHITE_SEED_INLET_COUNT, badI ? "*** FAIL ***" : "OK");
        if (badI) bad++;

        // And the sweep is walked by the machinery that already exists: site 0
        // must be the first row, and site N-1 the last, with nothing after it.
        const GardenDock* s0 = Garden_DockSite("White SeeD Ship", 0);
        const GardenDock* sN = Garden_DockSite("White SeeD Ship", WHITE_SEED_INLET_COUNT - 1);
        const GardenDock* sOver = Garden_DockSite("White SeeD Ship", WHITE_SEED_INLET_COUNT);
        const bool walkOk = s0 == &s_whiteSeedInlets[0] &&
                            sN == &s_whiteSeedInlets[WHITE_SEED_INLET_COUNT - 1] &&
                            sOver == nullptr;
        printf("Garden_DockSite walks the inlet list and ends: %s\n", walkOk ? "OK" : "*** FAIL ***");
        if (!walkOk) bad++;

        // Trabia Garden's own dock row must still be found -- the White SeeD
        // branch returns early and must not shadow the real table.
        const GardenDock* tg = Garden_DockSite("Trabia Garden", 0);
        printf("Trabia Garden dock row still resolves: %s\n", tg ? "OK" : "*** FAIL ***");
        if (!tg) bad++;

        // Site 0 is the known boarding point and gets a full press; a search
        // stop gets the short one; every other destination is unaffected.
        const bool phaseOk = GdNosePhases("White SeeD Ship", 0) == GD_NOSE_PHASES &&
                             GdNosePhases("White SeeD Ship", 1) == GD_NOSE_SWEEP_PHASES &&
                             GdNosePhases("Fisherman's Horizon", 0) == GD_NOSE_PHASES &&
                             GD_NOSE_SWEEP_PHASES < GD_NOSE_PHASES;
        printf("phases: ship site0=%d, sweep site1=%d, FH=%d  %s\n",
               GdNosePhases("White SeeD Ship", 0), GdNosePhases("White SeeD Ship", 1),
               GdNosePhases("Fisherman's Horizon", 0), phaseOk ? "OK" : "*** FAIL ***");
        if (!phaseOk) bad++;

        // ------------------------------------------------------------------
        // v0.54.0: THE BOARDING POINT, AND THE THING THAT MADE 773 UNITS FATAL.
        //
        // The BAT captured (-17974,47006) as the hull's position on the frame
        // field 853 loaded. Site 0 must aim there, from an approach that is a
        // DIFFERENT point -- when park and dock are the same coordinate the
        // hull arrives on the mark, the bearing to the dock is noise, and the
        // nose-in's lateral sweep goes nowhere. That is exactly how v0.51.0
        // and v0.52.0 pressed 773 units away from a ship that was there.
        // ------------------------------------------------------------------
        const GardenDock* site0 = Garden_DockSite("White SeeD Ship", 0);
        const bool dockOk = site0 && site0->dock_x == -17974 && site0->dock_y == 47006;
        printf("site 0 presses at the BAT-captured boarding point (%d,%d)  %s\n",
               site0 ? site0->dock_x : 0, site0 ? site0->dock_y : 0,
               dockOk ? "OK" : "*** FAIL ***");
        if (!dockOk) bad++;

        const double sep = site0 ? CalculateWrappedDistance(site0->approach_x, site0->approach_y,
                                                            site0->dock_x, site0->dock_y) : 0.0;
        const bool sepOk = sep >= 600.0;
        printf("approach stands off the dock by %.0f units  %s\n", sep,
               sepOk ? "OK (the hull drives through it)" : "*** FAIL: park==dock is the v0.51 bug ***");
        if (!sepOk) bad++;

        // Both ends Garden-navigable, and the line between them too -- the
        // nose-in drives it with the wall guard off and must not be steering
        // into anything.
        bool lineOk = site0 && Garden_CellReachable(site0->approach_x, site0->approach_y);
        if (site0) {
            for (int t = 0; t <= 10 && lineOk; t++) {
                const int32_t lx = site0->approach_x + (site0->dock_x - site0->approach_x) * t / 10;
                const int32_t ly = site0->approach_y + (site0->dock_y - site0->approach_y) * t / 10;
                if (!GdWalk(GdRow(ly), GdCol(lx))) lineOk = false;
            }
        }
        printf("approach -> boarding point is Garden water end to end  %s\n",
               lineOk ? "OK" : "*** FAIL ***");
        if (!lineOk) bad++;

        // (The catalog marker is checked in tests/catalog_story_test.cpp, which
        // is the probe that compiles world_catalog.inl.)
    }

    // v0.20.97: Shumi Village is an ORDINARY berth now -- the marker was moved
    // from the island's foot-isolated west plateau to the village itself, which
    // sits on the same landmass the Garden already parks on for Chocobo Forest
    // 2. Both must plan from Balamb with no beach exception armed, and their
    // berths must be close enough together to be the same shore.
    {
        const GardenPark* sh = Garden_ParkFor("Shumi Village");
        const GardenPark* cf = Garden_ParkFor("Chocobo Forest 2");
        s_gdBeachGoalIdx = -1;
        Garden_ComputeReach(20271, -24355);
        if (sh && sh->reachable) {
            const bool okPlan = Garden_Plan(20271, -24355, sh->park_x, sh->park_y);
            printf("Shumi plan from Balamb: %s (%d waypoints)\n",
                   okPlan ? "OK" : "FAILED", s_drivePathLen);
            if (!okPlan) bad++;
        } else { printf("Shumi Village has no berth *** UNEXPECTED ***\n"); bad++; }
        if (sh && cf) {
            const double d = CalculateWrappedDistance(sh->park_x, sh->park_y,
                                                      cf->park_x, cf->park_y);
            printf("Shumi berth to Chocobo Forest 2 berth: %d units %s\n",
                   (int)d, (d < 4000.0) ? "OK (same shore)" : "*** UNEXPECTED ***");
            if (d >= 4000.0) bad++;
        }
    }

    // plan a couple of real routes
    const int32_t tests[][4] = {
        { 24576, -29406, -62080, -28544 },   // Balamb -> Deling City
        { 24576, -29406,  48512, -57984 },   // Balamb -> Trabia
        { 24576, -29406,  44160,  65920 },   // Balamb -> Alien Ship 2 (far south)
        { 24576, -29406,  10624, -81536 },   // Balamb -> Chocobo Forest 2
    };
    for (int i = 0; i < 4; i++) {
        auto t0 = std::chrono::steady_clock::now();
        bool r = Garden_Plan(tests[i][0], tests[i][1], tests[i][2], tests[i][3]);
        auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
        double len = 0;
        for (int k = 0; k + 1 < s_drivePathLen; k++)
            len += CalculateWrappedDistance(s_drivePathWX[k], s_drivePathWY[k],
                                            s_drivePathWX[k + 1], s_drivePathWY[k + 1]);
        printf("plan %d -> %d  waypoints=%d  length=%.0f  %.1f ms\n", i, (int)r, s_drivePathLen, len, ms);
    }
    return 0;
}
