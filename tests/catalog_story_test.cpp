// v0.20.99 compile probe for world_catalog.inl's BuildDistanceCatalog edits.
// No host harness compiles this file (catalog_harness.cpp is the FIELD catalog),
// which is the same gap that let the v0.20.89 declaration-order bug reach MSVC.
#include <cstdio>
#include <string>
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
// v0.93.0: the REAL mapping, not a stub returning a constant. A stub that always
// says FOOT cannot tell a boarding from a steady state, so it was asserting
// nothing -- the boarding test below caught it on its first run. Mirrors
// world_map_geometry.inl.
static VehicleType GetVehicleType(uint8_t mode)
{
    if (mode == 0 || mode == 6)   return VEH_ON_FOOT;
    if (mode == 3)                return VEH_GARDEN;   // Ship: ocean access
    if (mode >= 32 && mode <= 40) return VEH_CAR;
    if (mode == 48)               return VEH_GARDEN;
    if (mode == 50)               return VEH_RAGNAROK;
    return VEH_ON_FOOT;
}
static int g_fakeVehId = 0;
static int GetActiveVehicleId() { return g_fakeVehId; }
// v0.93.0: CheckVehicleIdChange now stops a drive when the vehicle changes under
// it, so the harness has to provide the thing it calls -- and recording the call
// is what lets the behaviour be asserted rather than merely compiled.
static int         g_stopCalls = 0;
static std::string g_stopReason;
static void StopAutoDrive(const char* reason)
{ g_stopCalls++; g_stopReason = reason ? reason : ""; }
static int GetBfsRuleClass(VehicleType v)
{
    if (v == VEH_RAGNAROK) return 2;
    if (v == VEH_GARDEN)   return 1;
    return 0;
}
static int WorldXToFineCol(int32_t) { return 0; }
static int WorldYToFineRow(int32_t) { return 0; }
static void ComputeReachabilityFine(int, int, VehicleType) {}
static bool IsFineCellReachable(int32_t, int32_t) { return true; }
// Field names mirror the real struct in world_map_state.inl exactly -- the
// berth table below is included verbatim and read by name, not by position.
struct GardenPark { const char* name; int32_t park_x, park_y; int32_t walk_units; bool reachable, drive_in; int32_t dock_x, dock_y; bool beach_climb; };
static bool g_aboard = false;
static bool Garden_IsAboard() { return g_aboard; }
static void Garden_ComputeReach(int32_t, int32_t) {}
// v0.51.0 (#109): the REAL berth table, not a stub returning nullptr. It has no
// dependencies beyond the struct above and strcmp, and the White SeeD Ship
// checks at the bottom need the marker and the berth to be read from the two
// files that actually ship them -- a probe that invents either would agree with
// itself and prove nothing.
#include "world_garden_berths.inl"
static bool Garden_BerthReachable(const GardenPark*) { return true; }
static bool Garden_Active() { return false; }
static bool s_driveActive = false, s_onWorldMap = true;
static int  s_lastVehicle = 0;
static bool IsCanonicalLocomotion(uint8_t) { return true; }
static const uintptr_t WM_CAR_POS_ADDR      = (uintptr_t)(g_fakeSavemap + 32);
static const uintptr_t WM_RAGNAROK_POS_ADDR = (uintptr_t)(g_fakeSavemap + 64);
// v0.56.0 (#118): the REAL locomotion verdict, not a stub of it. world_catalog.inl
// now routes its entry-debounce snapshot and its vehicle-change commit through
// LocoCorroborated, and stubbing that here would be asserting the interface
// rather than compiling it -- the same reason the berth table above is included
// verbatim. The state block mirrors world_map_state.inl.
static inline bool IsFootLocomotion(uint8_t mode) { return mode == 0 || mode == 6; }   // world_map_geometry.inl provides this in the real build
enum LocoVerdictKind { LOCO_UNKNOWN = 0, LOCO_FOOT = 1, LOCO_VEHICLE = 2 };
static LocoVerdictKind s_locoVerdict      = LOCO_UNKNOWN;
static int32_t         s_locoFootX        = 0;
static int32_t         s_locoFootY        = 0;
static bool            s_locoHadFoot      = false;
static int             s_locoLastRejected = -999;
static const double    LOCO_MIRROR_AGREE_UNITS = 600.0;
#include "world_map_locomotion.inl"

// v0.81.0: RagIsFlying is a two-line vehicle read that world_rag_arrival.inl
// owns and the real build compiles; what this probe is for is the CATALOG'S
// RESPONSE to it, so it is driven from here rather than read from a fake
// locomotion byte. The three rules it feeds are the real ones, included below.
static bool g_fakeFlyingRag = false;
static bool RagIsFlying() { return g_fakeFlyingRag; }
#include "wm_vehicle_resolve_pure.inl"
#include "wm_story_pure.inl"
#include "wm_catalog_refresh_pure.inl"
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


    // ========================================================================
    // v0.81.0 -- FLYING THE RAGNAROK. Two rules, one cause.
    //
    // The 2026-08-25 log had two catalog builds ninety seconds apart:
    //   23:03:38  aboard (vehicleId=50), ragnarok_pos ZERO -> "Ragnarok not held"
    //   23:05:09  on foot (vehicleId=0),  ragnarok_pos live -> "Ragnarok held"
    // The slot is the ship's PARKED position, so v0.20.101's ownership test read
    // false for the whole of every flight and the White SeeD Ship came back onto
    // the map each time he took off. Aaron: "It should not appear on the world
    // map catalog again REGARDLESS OF VEHICLE at this point."
    //
    // And the mobile Balamb Garden is not somewhere an airship can land: "To get
    // to Garden you land at FH where Garden is parked."
    // ========================================================================
    {
        memset(g_fakeSm + 0x0044, 0, 4);
        g_fakeSm[0x0E8D] = 0;
        const uint32_t disc3 = 2;                       // 0-indexed
        memcpy(g_fakeSm + 0x0044, &disc3, 4);
        // the Garden is mobile and parked, exactly as it was in the log
        *(int32_t*)(g_fakeSavemap + 0) = 20360;
        *(int32_t*)(g_fakeSavemap + 4) = -3850;

        // wantRag (v0.82.0): the parked ship is a destination exactly when its
        // slot is live, which is exactly when he is not inside it.
        struct { const char* label; bool flying; int32_t ragx;
                 bool wantWS; bool wantMob; bool wantRag; } F[] = {
            // the 23:03:38 state: in the ship, slot not yet written. THE BUG.
            { "flying, parked slot still zero",  true,      0, false, false, false },
            // the 23:05:09 state: landed and out, slot written. HE NEEDS HIS RIDE.
            { "on foot, parked slot live",      false,  12345, false, true,  true  },
            // flying later in the game, slot written: no ship, no Garden, and no
            // point offering him the thing he is sitting in.
            { "flying, parked slot live",        true,  12345, false, false, false },
            // disc 3 before he ever reaches the Ragnarok: no ship to walk to.
            { "on foot, no Ragnarok at all",    false,      0, true,  true,  false },
        };
        for (unsigned k = 0; k < sizeof(F) / sizeof(F[0]); k++) {
            g_fakeFlyingRag = F[k].flying;
            const int32_t ry = F[k].ragx ? -6789 : 0;
            memcpy(g_fakeSavemap + 64 + 0, &F[k].ragx, 4);
            memcpy(g_fakeSavemap + 64 + 4, &ry, 4);
            s_catalogBuilt = false;
            BuildDistanceCatalog();
            bool ws = false, mob = false, rag = false;
            for (int i = 0; i < s_catalogCount; i++) {
                if (!strcmp(s_catalog[i].name, "White SeeD Ship"))      ws  = true;
                if (!strcmp(s_catalog[i].name, "Mobile Balamb Garden")) mob = true;
                if (!strcmp(s_catalog[i].name, "Ragnarok"))             rag = true;
            }
            const bool ok = (ws == F[k].wantWS) && (mob == F[k].wantMob) && (rag == F[k].wantRag);
            printf("%-34s WhiteSeeD=%d(%d) MobileGarden=%d(%d) Ragnarok=%d(%d)  %s\n",
                   F[k].label, (int)ws, (int)F[k].wantWS, (int)mob, (int)F[k].wantMob,
                   (int)rag, (int)F[k].wantRag, ok ? "OK" : "*** FAIL ***");
            if (!ok) fails++;
        }
        g_fakeFlyingRag = false;
        memset(g_fakeSavemap + 64, 0, 8);
    }

    // ========================================================================
    // v0.93.0 -- BOARDING MID-DRIVE STOPS THE DRIVE.
    //
    // The 18:18 BAT, twice: Aaron auto-drove ON FOOT to the parked Ragnarok and
    // pressed X when he got there. The engine vehicle id read 50 immediately --
    // "[VEHID] engine vehicleId=50 -> steering as VEHICLE from drive start" --
    // and the watcher was forbidden to look at it while a drive was active, so
    // the drive flew on toward the coordinate where the ship used to be parked
    // until he cancelled it by hand.
    // ========================================================================
    {
        g_fakeFlyingRag = false;
        memset(g_fakeSavemap + 64, 0, 8);
        s_catalogBuilt = false;
        g_fakeVehId    = 0;
        BuildDistanceCatalog();                 // a catalog built ON FOOT
        const int builtFor = s_catalogVehIdClass;

        s_driveActive = true;                   // ...and a drive running
        g_stopCalls = 0;
        g_fakeVehId = 50;                       // he presses X and boards
        for (int i = 0; i < 8; i++) CheckVehicleIdChange();

        const bool ok = (g_stopCalls == 1) && !s_catalogBuilt;
        printf("boarding mid-drive: stops=%d catalogInvalidated=%d  %s\n",
               g_stopCalls, (int)!s_catalogBuilt, ok ? "OK" : "*** FAIL ***");
        if (!ok) fails++;
        const bool said = g_stopReason.find("Ragnarok") != std::string::npos &&
                          g_stopReason.find("catalog") != std::string::npos;
        printf("  and it says what he is in and what to do next: \"%s\"  %s\n",
               g_stopReason.c_str(), said ? "OK" : "*** FAIL ***");
        if (!said) fails++;

        // Steady state must not keep stopping the drive, or no flight survives
        // its first second.
        s_driveActive = true;
        s_catalogBuilt = false;
        BuildDistanceCatalog();                 // rebuild, now aboard
        g_stopCalls = 0;
        for (int i = 0; i < 8; i++) CheckVehicleIdChange();
        printf("  steady state aboard: stops=%d (want 0)  %s\n",
               g_stopCalls, g_stopCalls == 0 ? "OK" : "*** FAIL ***");
        if (g_stopCalls != 0) fails++;
        (void)builtFor;

        s_driveActive = false;
        g_fakeVehId   = 0;
        s_catalogBuilt = false;
        BuildDistanceCatalog();
    }

    // ========================================================================
    // v0.51.0 (#109) -- THE WHITE SEED SHIP'S COORDINATE, READ OUT OF THE GAME
    //
    // The shipped marker was (4887,51285): the Centra Ruins marker minus
    // exactly (2000,4000), never measured, never driven to. The 2026-08-21 BAT
    // parked the Garden 2.7 km from it and told a blind player to walk to bare
    // ground on a landmass he cannot reach.
    //
    // The real one is wmsetus.obj section 8 record 17. The bytes below are that
    // table verbatim, records 14..21, straight out of the game file. Records 14,
    // 16, 18 and 20 are Winhill, the Centra Ruins, Edea's House and Shumi
    // Village -- four coordinates this catalog already carries and Aaron has
    // driven to -- so the base offset and the 12-byte stride are PINNED by the
    // fixture rather than asserted about it. Get either wrong and the anchors
    // fail before the ship is ever read.
    // ========================================================================
    {
        // wmsetus.obj, 12-byte records at file offset 5580: int32 X, int32 Y,
        // int16 Z, int16 flags. This block starts at record 14 (offset 5748).
        static const unsigned char WMSETUS_REC14_21[] = {
            0x44,0x3B,0xFF,0xFF, 0xC1,0x18,0x00,0x00, 0x7F,0xFE, 0x20,0x40,  // 14
            0x60,0x35,0xFF,0xFF, 0xBF,0x18,0x00,0x00, 0x7F,0xFE, 0xE0,0xC0,  // 15
            0xE7,0x1A,0x00,0x00, 0xF5,0xD7,0x00,0x00, 0xBA,0xFD, 0xEB,0x40,  // 16
            0x3A,0xBC,0xFF,0xFF, 0xD6,0xB5,0x00,0x00, 0x00,0x00, 0x21,0x26,  // 17
            0x0F,0x8D,0xFF,0xFF, 0x52,0x0F,0x01,0x00, 0xE3,0xFE, 0x90,0x00,  // 18
            0xE9,0xBF,0x00,0x00, 0xC5,0x1C,0xFF,0xFF, 0xE0,0xFC, 0xE0,0x7F,  // 19
            0xC8,0x32,0x00,0x00, 0xF7,0xB7,0xFE,0xFF, 0x98,0xFC, 0xCC,0x40,  // 20
            0x00,0x30,0xFE,0xFF, 0x00,0x50,0x01,0x00, 0x9C,0xFC, 0x01,0x08,  // 21
        };
        struct Rec { int32_t x, y; int16_t z; };
        auto rec = [&](int n) {
            Rec r;
            const unsigned char* p = WMSETUS_REC14_21 + (n - 14) * 12;
            memcpy(&r.x, p + 0, 4); memcpy(&r.y, p + 4, 4); memcpy(&r.z, p + 8, 2);
            return r;
        };
        auto marker = [&](const char* nm, int32_t* x, int32_t* y) {
            for (int i = 0; i < LOCATION_COUNT; i++)
                if (!strcmp(s_locations[i].name, nm)) { *x = s_locations[i].x; *y = s_locations[i].y; return true; }
            return false;
        };

        // --- the anchors pin the table's base and stride ---------------------
        static const struct { int n; const char* name; } ANCHOR[] = {
            { 14, "Winhill" }, { 16, "Centra Ruins" },
            { 18, "Edea's House" }, { 20, "Shumi Village" },
        };
        for (unsigned k = 0; k < sizeof(ANCHOR) / sizeof(ANCHOR[0]); k++) {
            int32_t cx = 0, cy = 0;
            const bool got = marker(ANCHOR[k].name, &cx, &cy);
            const Rec r = rec(ANCHOR[k].n);
            const double d = got ? std::sqrt((double)(cx - r.x) * (cx - r.x) +
                                             (double)(cy - r.y) * (cy - r.y)) : 1e9;
            const bool ok = got && d < 2000.0;
            printf("wmsetus rec %2d -> %-16s (%7d,%7d) vs catalog (%7d,%7d) d=%5.0f  %s\n",
                   ANCHOR[k].n, ANCHOR[k].name, r.x, r.y, cx, cy, d, ok ? "OK" : "*** FAIL ***");
            if (!ok) fails++;
        }

        // --- v0.53.0: RECORD 17 IS *NOT* THE SHIP -----------------------------
        //
        // v0.51.0 shipped it as the marker on the strength of being the only
        // sea-level ocean record in the table, in a bay matching every
        // walkthrough. Two BATs killed it: the hull was driven onto it twice,
        // the second time with Edea's letter in hand, `dist to press point 11`,
        // and the F11 screenshot shows an empty inlet. The record stays worth
        // knowing -- it is still the only sea-level ocean marker, so it is
        // something -- but it is not a coordinate to drive a blind player to.
        const Rec r17 = rec(17);
        printf("wmsetus rec 17 = (%d,%d) Z=%d -- the table's only sea-level "
               "ocean record, and NOT the ship (BAT 2026-08-21)\n",
               r17.x, r17.y, (int)r17.z);

        int32_t wx = 0, wy = 0;
        const bool gotShip = marker("White SeeD Ship", &wx, &wy);
        const bool notR17 = gotShip && !(wx == r17.x && wy == r17.y);
        printf("White SeeD Ship marker has been retired from rec 17: (%d,%d)  %s\n",
               wx, wy, notR17 ? "OK" : "*** FAIL ***");
        if (!notR17) fails++;

        const bool notOld = gotShip && !(wx == 4887 && wy == 51285);
        printf("and is not the v0.50.0 fabrication either (Centra Ruins - 2000,4000)  %s\n",
               notOld ? "OK" : "*** FAIL ***");
        if (!notOld) fails++;

        // --- v0.54.0: THE MARKER IS THE BOARDING POINT ------------------------
        //
        // (-17974,47006) is the hull's position on the frame the world map
        // handed off to field 853 `se\sefront1`, captured by the v0.53.1 inlet
        // sweep. Two consequences ride on the marker being that exact point and
        // not something near it: the distance the mod announces is the distance
        // to the ship, and a future entry capture lands inside its 3,000-unit
        // window. The v0.53.1 log refused to capture the coordinate it had just
        // found -- `nearest location White SeeD Ship is 19182u away (> 3000),
        // not capturing` -- because the marker was still the first sweep inlet.
        const bool atShip = gotShip && wx == -17974 && wy == 47006;
        printf("marker is the BAT-captured boarding point (%d,%d)  %s\n",
               wx, wy, atShip ? "OK" : "*** FAIL ***");
        if (!atShip) fails++;

        // And it is 773 units from wmsetus record 17 -- close enough that the
        // record really was the ship's marker, far enough that pressing on the
        // record missed. Both facts matter; the assertion pins the number so a
        // future edit cannot quietly drift back onto the record.
        const double toR17 = gotShip ? std::sqrt((double)(wx - r17.x) * (wx - r17.x) +
                                                 (double)(wy - r17.y) * (wy - r17.y)) : 0.0;
        const bool gapOk = toR17 > 600.0 && toR17 < 1000.0;
        printf("boarding point is %.0f units from wmsetus rec 17  %s\n", toR17,
               gapOk ? "OK (the record was the marker, not the door)" : "*** FAIL ***");
        if (!gapOk) fails++;

        // --- the berth is a drive-in with no walk -----------------------------
        // Whatever the marker is, the White SeeD Ship is never park-and-walk:
        // it is on water, and a walk figure here is the 2026-08-21 defect
        // coming back.
        const GardenPark* gp = Garden_ParkFor("White SeeD Ship");
        // park is the APPROACH now, deliberately not the marker: park == dock is
        // what made the nose-in sweep go nowhere. dock is the marker.
        const bool berthOk = gp && gp->reachable && gp->drive_in &&
                             gp->walk_units == 0 &&
                             gp->dock_x == wx && gp->dock_y == wy &&
                             (gp->park_x != wx || gp->park_y != wy);
        printf("berth: park=(%d,%d) dock=(%d,%d) walk=%d drive_in=%d  %s\n",
               gp ? gp->park_x : 0, gp ? gp->park_y : 0,
               gp ? gp->dock_x : 0, gp ? gp->dock_y : 0,
               gp ? gp->walk_units : -1, gp ? (int)gp->drive_in : -1,
               berthOk ? "OK" : "*** FAIL ***");
        if (!berthOk) fails++;

        // Fisherman's Horizon is the other drive_in and it is a STAND-OFF, so
        // "park == marker" is a property of this berth, not of drive_ins.
        const GardenPark* fh = Garden_ParkFor("Fisherman's Horizon");
        int32_t fx = 0, fy = 0; marker("Fisherman's Horizon", &fx, &fy);
        const bool fhOk = fh && fh->drive_in && (fh->park_x != fx || fh->park_y != fy);
        printf("Fisherman's Horizon still stands off its marker  %s\n", fhOk ? "OK" : "*** FAIL ***");
        if (!fhOk) fails++;
    }

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL CHECKS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
