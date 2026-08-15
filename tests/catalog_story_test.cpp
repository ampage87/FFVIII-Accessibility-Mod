// v0.20.99 compile probe for world_catalog.inl's BuildDistanceCatalog edits.
// No host harness compiles this file (catalog_harness.cpp is the FIELD catalog),
// which is the same gap that let the v0.20.89 declaration-order bug reach MSVC.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>
typedef unsigned long DWORD;
namespace Log { void World(const char*, ...) {} void Menu(const char*, ...) {} }
namespace ScreenReader { bool Speak(const char*, bool = false) { return true; } }
static DWORD GetTickCount() { return 0; }
static const int MAX_LOCATIONS = 64;
struct LocationEntry { const char* name; int32_t x; int32_t y; };
static LocationEntry s_catalog[MAX_LOCATIONS];
static int s_catalogCount = 0, s_catalogIndex = 0;
static bool s_catalogBuilt = false, s_walkGridLoaded = true;
static bool s_refinedHas[MAX_LOCATIONS]; static int32_t s_refinedX[MAX_LOCATIONS], s_refinedY[MAX_LOCATIONS];
static uint8_t g_fakeSavemap[256];
static uint8_t g_fakeSm[0x1200];                       // the savemap the story reads
static const uintptr_t WM_SAVEMAP_BASE = (uintptr_t)g_fakeSm;
static const uintptr_t WM_BGU_POS_ADDR = (uintptr_t)g_fakeSavemap;
static const uint32_t WMS_VEHPOS_X_OFF = 0, WMS_VEHPOS_Y_OFF = 4;
static bool WmSafeReadBytes(uintptr_t a, void* o, size_t n) { memcpy(o, (const void*)a, n); return true; }
static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z) { *x = 20271; *y = -24355; *z = 0; }
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    double dx = x2 - x1, dy = y2 - y1; return std::sqrt(dx*dx + dy*dy);
}
enum VehicleType { VEH_ON_FOOT = 0, VEH_CAR, VEH_GARDEN, VEH_RAGNAROK };
static uint8_t GetLocomotionMode() { return 0; }
static VehicleType GetVehicleType(uint8_t) { return VEH_ON_FOOT; }
static int GetActiveVehicleId() { return 0; }
static int GetBfsRuleClass(VehicleType) { return 0; }
static int WorldXToFineCol(int32_t) { return 0; }
static int WorldYToFineRow(int32_t) { return 0; }
static void ComputeReachabilityFine(int, int, VehicleType) {}
static bool IsFineCellReachable(int32_t, int32_t) { return true; }
struct GardenPark { const char* name; int32_t park_x, park_y; int32_t walk; bool reachable, drive_in; int32_t dock_x, dock_y; bool beach_climb; };
static bool g_aboard = false;
static bool Garden_IsAboard() { return g_aboard; }
static void Garden_ComputeReach(int32_t, int32_t) {}
static const GardenPark* Garden_ParkFor(const char*) { return nullptr; }
static bool Garden_BerthReachable(const GardenPark*) { return true; }
static bool Garden_Active() { return false; }
static bool s_driveActive = false, s_onWorldMap = true;
static int  s_lastVehicle = 0;
static bool IsCanonicalLocomotion(uint8_t) { return true; }
static const uintptr_t WM_CAR_POS_ADDR      = (uintptr_t)(g_fakeSavemap + 32);
static const uintptr_t WM_RAGNAROK_POS_ADDR = (uintptr_t)(g_fakeSavemap + 64);
#include "world_catalog.inl"
int main() {
    // static: Garden parked -> both fixed Garden sites must stay.
    memset(g_fakeSavemap, 0, sizeof(g_fakeSavemap));
    BuildDistanceCatalog();
    int staticCount = s_catalogCount;
    bool bg = false, gg = false;
    for (int i = 0; i < s_catalogCount; i++) {
        if (!strcmp(s_catalog[i].name, "Balamb Garden"))   bg = true;
        if (!strcmp(s_catalog[i].name, "Galbadia Garden")) gg = true;
    }
    printf("Garden STATIC : %d entries, Balamb Garden=%d Galbadia Garden=%d  %s\n",
           staticCount, (int)bg, (int)gg, (bg && gg) ? "OK" : "*** FAIL ***");
    int fails = (bg && gg) ? 0 : 1;

    // mobile: bgu_pos live -> both must be gone, and exactly two fewer entries.
    *(int32_t*)(g_fakeSavemap + 0) = 20271;
    *(int32_t*)(g_fakeSavemap + 4) = -24355;
    s_catalogBuilt = false;
    BuildDistanceCatalog();
    bg = gg = false;
    for (int i = 0; i < s_catalogCount; i++) {
        if (!strcmp(s_catalog[i].name, "Balamb Garden"))   bg = true;
        if (!strcmp(s_catalog[i].name, "Galbadia Garden")) gg = true;
    }
    // -2 retired, +1 for the 'Mobile Balamb Garden' entry the mobile branch appends
    const bool countOk = (s_catalogCount == staticCount - 2 + 1);
    printf("Garden MOBILE : %d entries (want %d), Balamb Garden=%d Galbadia Garden=%d  %s\n",
           s_catalogCount, staticCount - 2 + 1, (int)bg, (int)gg,
           (!bg && !gg && countOk) ? "OK" : "*** FAIL ***");
    if (bg || gg || !countOk) fails++;

    // and the mobile ride itself is still offered
    bool mob = false;
    for (int i = 0; i < s_catalogCount; i++)
        if (!strcmp(s_catalog[i].name, "Mobile Balamb Garden")) mob = true;
    printf("Mobile Balamb Garden present: %d  %s\n", (int)mob, mob ? "OK" : "*** FAIL ***");
    if (!mob) fails++;
    // ABOARD rebuilds the catalog from s_locations rather than from the
    // compacted s_catalog, so it asks WmStoryRetired a second time. That is the
    // shape of the v0.20.88 bug -- one condition, three askers -- so it gets its
    // own check rather than being assumed to follow.
    g_aboard = true;
    s_catalogBuilt = false;
    BuildDistanceCatalog();
    bg = gg = false;
    for (int i = 0; i < s_catalogCount; i++) {
        if (!strcmp(s_catalog[i].name, "Balamb Garden"))   bg = true;
        if (!strcmp(s_catalog[i].name, "Galbadia Garden")) gg = true;
    }
    printf("Garden ABOARD : %d entries, Balamb Garden=%d Galbadia Garden=%d  %s\n",
           s_catalogCount, (int)bg, (int)gg, (!bg && !gg) ? "OK" : "*** FAIL ***");
    if (bg || gg) fails++;

    // v0.20.100: the Mobile Galbadia Garden WINDOW. Three states, and the
    // closing edge (disc 3) has to hold even when the opening flag is set --
    // Aaron: "once the battle of the Gardens is over, Galbadia Garden
    // disappears from the world map forever."
    g_aboard = false;
    struct { const char* label; uint32_t disc; uint8_t gg; bool want; } W[] = {
        { "disc 2, flag 0  (not yet)",      1, 0, false },
        { "disc 2, flag 2  (present)",      1, 2, true  },
        { "disc 3, flag 2  (gone forever)", 2, 2, false },
        { "disc 4, flag 2  (gone forever)", 3, 2, false },
    };
    for (unsigned k = 0; k < sizeof(W) / sizeof(W[0]); k++) {
        memcpy(g_fakeSm + 0x0044, &W[k].disc, 4);
        g_fakeSm[0x0E8D] = W[k].gg;
        s_catalogBuilt = false;
        BuildDistanceCatalog();
        bool gal = false;
        for (int i = 0; i < s_catalogCount; i++)
            if (!strcmp(s_catalog[i].name, "Mobile Galbadia Garden")) gal = true;
        printf("%-34s Mobile Galbadia Garden=%d want=%d  %s\n",
               W[k].label, (int)gal, (int)W[k].want,
               (gal == W[k].want) ? "OK" : "*** FAIL ***");
        if (gal != W[k].want) fails++;
    }

    // v0.20.101: the WHITE SEED SHIP window. Both edges are already-validated
    // signals -- disc >= 3 opens it, ragnarok_pos going live closes it -- so
    // unlike Galbadia Garden neither of these is a guess.
    memset(g_fakeSm + 0x0044, 0, 4);
    g_fakeSm[0x0E8D] = 0;
    struct { const char* label; uint32_t disc; bool rag; bool want; } S[] = {
        { "disc 1, no Ragnarok  (too early)", 0, false, false },
        { "disc 2, no Ragnarok  (too early)", 1, false, false },
        { "disc 3, no Ragnarok  (present)",   2, false, true  },
        { "disc 3, has Ragnarok (gone)",      2, true,  false },
        { "disc 4, has Ragnarok (gone)",      3, true,  false },
    };
    for (unsigned k = 0; k < sizeof(S) / sizeof(S[0]); k++) {
        memcpy(g_fakeSm + 0x0044, &S[k].disc, 4);
        const int32_t rx = S[k].rag ? 12345 : 0, ry = S[k].rag ? -6789 : 0;
        memcpy(g_fakeSavemap + 64 + 0, &rx, 4);
        memcpy(g_fakeSavemap + 64 + 4, &ry, 4);
        s_catalogBuilt = false;
        BuildDistanceCatalog();
        bool ws = false;
        for (int i = 0; i < s_catalogCount; i++)
            if (!strcmp(s_catalog[i].name, "White SeeD Ship")) ws = true;
        printf("%-34s White SeeD Ship=%d want=%d  %s\n",
               S[k].label, (int)ws, (int)S[k].want,
               (ws == S[k].want) ? "OK" : "*** FAIL ***");
        if (ws != S[k].want) fails++;
    }

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL CHECKS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
