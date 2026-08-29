// locomotion_compile.cpp -- the locomotion verdict (#118), compiled and RUN on
// the host against Aaron's own 2026-08-21 Esthar timeline.
//
//   g++ -std=c++17 -O0 -Isrc -o locomotion_compile tests/locomotion_compile.cpp
//
// WHAT THIS IS FOR
// ----------------
// Three drives failed on the Esthar continent and all three failed for one
// reason: a single noisy read of locomotion=3 (Ship) latched through the
// world-map entry-debounce snapshot, which was the one commit path that did not
// go through the v0.20.56 corroboration gate. From then on the mod believed
// Aaron was piloting a Ship, so it read his position from the Balamb Garden
// savemap mirror 70 km away and steered him with the vehicle law, which on foot
// walks in circles.
//
// The failure is a DECISION, so this probe tests the decision. It compiles the
// real world_map_locomotion.inl and drives it with the byte values, foot
// positions, engine vehicle ids and mirror contents taken from the log, and
// asserts what the verdict is at each moment Aaron pressed a key.
//
// The numbers below are LOG-SOURCED, not invented:
//
//   20:40:41  [WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=3 (was 0, ...)
//   20:40:56  [VEHDUMP] vehicleId[020409E0]=0 ... bgu_pos=(20360,-3850)
//   20:40:56  [BFS] Player at (86900,6861)
//   20:41:50  [GROUNDH] pos(89121,7419)          <- last movement
//   20:41:54  [DRIVE] Start -> Tears' Point ... dist=72125 units (72 km)
//   20:41:54  [VEHPOS32] bgu_pos int32=(20360,-3850,200)
//
// v0.56.0 (#118).

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>

typedef unsigned long DWORD;
#define __try       try
#define __except(x) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }

static std::vector<std::string> g_log;
namespace Log {
    void World(const char* fmt, ...)
    { char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap); g_log.push_back(b); }
}
static bool logged(const char* needle)
{ for (auto& l : g_log) if (l.find(needle) != std::string::npos) return true; return false; }

namespace WorldMap {

enum VehicleType { VEH_ON_FOOT, VEH_CHOCOBO, VEH_CAR, VEH_GARDEN, VEH_RAGNAROK };

// ---- the engine seams, as settable fixtures -------------------------------
static int32_t  g_footX = 0, g_footY = 0, g_footZ = 0;
static int      g_engineVehId = 0;                 // 0x020409E0, 0 when nothing moves
static int32_t  g_bguX = 0, g_bguY = 0;
static int32_t  g_carX = 0, g_carY = 0;
static int32_t  g_ragX = 0, g_ragY = 0;

static const uintptr_t WM_CAR_POS_ADDR      = 0x1000;
static const uintptr_t WM_BGU_POS_ADDR      = 0x2000;
static const uintptr_t WM_RAGNAROK_POS_ADDR = 0x3000;
static const uint32_t  WMS_VEHPOS_X_OFF = 0, WMS_VEHPOS_Y_OFF = 4;

static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z)
{ *x = g_footX; *y = g_footY; if (z) *z = g_footZ; }

static int GetActiveVehicleId() { return g_engineVehId; }

static bool WmSafeReadBytes(uintptr_t addr, void* out, size_t n)
{
    if (n != 4) return false;
    int32_t v = 0;
    if      (addr == WM_CAR_POS_ADDR      + WMS_VEHPOS_X_OFF) v = g_carX;
    else if (addr == WM_CAR_POS_ADDR      + WMS_VEHPOS_Y_OFF) v = g_carY;
    else if (addr == WM_BGU_POS_ADDR      + WMS_VEHPOS_X_OFF) v = g_bguX;
    else if (addr == WM_BGU_POS_ADDR      + WMS_VEHPOS_Y_OFF) v = g_bguY;
    else if (addr == WM_RAGNAROK_POS_ADDR + WMS_VEHPOS_X_OFF) v = g_ragX;
    else if (addr == WM_RAGNAROK_POS_ADDR + WMS_VEHPOS_Y_OFF) v = g_ragY;
    else return false;
    memcpy(out, &v, 4);
    return true;
}

// ---- the real mapping and distance, copied in from the modules this file
//      would otherwise have to pull whole. Both are tiny and both are asserted
//      against the shipped source by lint; keeping them here keeps the probe
//      from dragging in the entire world-map translation unit.
static VehicleType GetVehicleType(uint8_t mode)
{
    if (mode == 0 || mode == 6) return VEH_ON_FOOT;
    if (mode == 3)              return VEH_GARDEN;      // the Ship
    if (mode == 31)             return VEH_CHOCOBO;
    if (mode >= 32 && mode <= 40) return VEH_CAR;
    if (mode == 48)             return VEH_GARDEN;
    if (mode == 50)             return VEH_RAGNAROK;
    return VEH_ON_FOOT;
}
static inline bool IsFootLocomotion(uint8_t mode) { return mode == 0 || mode == 6; }
static const double WM_WIDTH = 262144.0, WM_HEIGHT = 196608.0;
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = (double)x1 - x2, dy = (double)y1 - y2;
    if (dx >  WM_WIDTH  / 2) dx -= WM_WIDTH;   if (dx < -WM_WIDTH  / 2) dx += WM_WIDTH;
    if (dy >  WM_HEIGHT / 2) dy -= WM_HEIGHT;  if (dy < -WM_HEIGHT / 2) dy += WM_HEIGHT;
    return std::sqrt(dx * dx + dy * dy);
}

static int s_lastVehicle = -1;

// ---- the state block, verbatim from world_map_state.inl -------------------
enum LocoVerdictKind { LOCO_UNKNOWN = 0, LOCO_FOOT = 1, LOCO_VEHICLE = 2 };
static LocoVerdictKind s_locoVerdict      = LOCO_UNKNOWN;
static int32_t         s_locoFootX        = 0;
static int32_t         s_locoFootY        = 0;
static bool            s_locoHadFoot      = false;
static int             s_locoLastRejected = -999;
static const double    LOCO_MIRROR_AGREE_UNITS = 600.0;

// ---- the code under test --------------------------------------------------
#include "world_map_locomotion.inl"

} // namespace WorldMap
using namespace WorldMap;

static void reset(int byteVal, int32_t fx, int32_t fy)
{
    LocoReset();
    s_lastVehicle = byteVal;
    g_footX = fx; g_footY = fy;
    g_engineVehId = 0;
    g_bguX = g_bguY = g_carX = g_carY = g_ragX = g_ragY = 0;
    g_log.clear();
}
static void step(int32_t fx, int32_t fy) { g_footX = fx; g_footY = fy; LocoTick(); }
static void hold(int n) { for (int i = 0; i < n; i++) LocoTick(); }

int main()
{
    std::printf("locomotion_compile\n");

    // =======================================================================
    // 1. THE BUG, REPLAYED. Aaron's timeline, values from the log.
    // =======================================================================
    // The Ship byte is latched. He is at (86900,6861) in Esthar; the Balamb
    // Garden mirror is at (20360,-3850), ~70 km away; the engine vehicle id
    // reads 0 because nothing is moving.
    reset(3, 86900, 6861);
    g_bguX = 20360; g_bguY = -3850;
    g_engineVehId = 0;
    LocoTick();
    check(LocoIsFoot(), "**a latched Ship byte does not survive one tick on foot** -- with no corroboration the verdict is FOOT even before he moves");
    check(logged("NOT corroborated"), "and the log says the byte was rejected, with the distance");

    // He walks: 20:41:15 -> 20:41:50, the Sorceress Memorial attempt.
    step(88632, 7215); step(87733, 5293); step(86154, 6646); step(87753, 8270);
    check(LocoIsFoot(), "foot motion pins the verdict to FOOT");

    // 20:41:50 he cancels. 20:41:51 and :53 he presses the catalog key. He does
    // not move for four seconds. THIS IS THE MOMENT THE OLD CODE FAILED: the
    // 2000 ms footAlive window closed and the Garden mirror took over.
    step(89121, 7419);
    hold(240);                                  // ~4 s of standing still at 60 Hz
    check(LocoIsFoot(),
          "**standing still for four seconds does not turn him into a Ship** -- "
          "this is the Tears' Point start-position failure");

    // The consequence the old code produced, stated as an assertion: with the
    // verdict FOOT, the position source stays the foot DWORDs, so the drive
    // starts 25 km from Tears' Point and not 72 km.
    {
        const double realDist  = CalculateWrappedDistance(89121, 7419, 83021, 31865);
        const double bogusDist = CalculateWrappedDistance(20360, -3850, 83021, 31865);
        check(realDist < 26000, "the true distance to Tears' Point is ~25 km");
        check(bogusDist > 70000 && bogusDist < 74000,
              "and the Garden mirror gives the log's 72 km -- so the 72125 in the "
              "log identifies the mirror as the source beyond doubt");
    }

    // =======================================================================
    // 2. THE VERDICT STILL SAYS VEHICLE WHEN IT SHOULD.
    // =======================================================================
    // Genuinely aboard and moving: the engine names the vehicle family.
    reset(48, 24000, -30000);                   // byte says Garden
    g_bguX = 24000; g_bguY = -30000;            // and the hull is where he is
    g_engineVehId = 48;
    LocoTick();
    check(!LocoIsFoot(), "aboard the Garden and moving -> VEHICLE (engine id agrees)");
    check(logged("verdict VEHICLE"), "and it is logged with its evidence");

    // ...and the id ALONE has to be enough, because it is the only signal left
    // once the hull has sailed. Aboard and 30 km out from where he boarded: the
    // foot DWORDs are still frozen at the boarding point, so the mirror is
    // nowhere near them and CANNOT corroborate. If this case fell through to
    // FOOT the position source would snap back to the boarding point and every
    // long Garden voyage would break -- which is the mirror image of the bug
    // this module fixes, and it is why the id path is not decoration.
    // (Mutation-tested: disabling the id path passes without this case.)
    reset(48, 24000, -30000);                   // frozen at the berth
    g_bguX = 54000; g_bguY = -30000;            // hull 30 km east
    g_engineVehId = 48;                         // the engine names it while moving
    LocoTick();
    check(!LocoIsFoot(),
          "**aboard and 30 km from the berth -> VEHICLE on the engine id alone**");

    // Genuinely aboard and PARKED: the engine id reads 0, but the frozen foot
    // DWORDs sit at the boarding point beside the hull, so the mirror agrees.
    reset(48, 24100, -30050);
    g_bguX = 24000; g_bguY = -30000;            // 112 units away
    g_engineVehId = 0;
    LocoTick();
    check(!LocoIsFoot(), "aboard and parked -> VEHICLE (the mirror is right there)");

    // A car, the v0.20.56 case: byte says car, car is 50 km away at the Missile
    // Base, player is walking out of Dollet.
    reset(33, -15000, -39000);
    g_carX = -71612; g_carY = -14815;
    g_engineVehId = 0;
    LocoTick();
    check(LocoIsFoot(), "the v0.20.56 stale-car case still reads FOOT");

    // =======================================================================
    // 3. THE VERDICT HOLDS RATHER THAN GUESSING.
    // =======================================================================
    // Board a parked Garden, then stand still for a long time. The verdict must
    // NOT drift back to foot just because nothing is moving -- that is the same
    // mistake as the old window, pointing the other way.
    reset(48, 24100, -30050);
    g_bguX = 24000; g_bguY = -30000;
    g_engineVehId = 48;
    LocoTick();
    check(!LocoIsFoot(), "aboard");
    g_engineVehId = 0;                          // stops moving
    g_bguX = 40000; g_bguY = -30000;            // and the hull has since sailed off
    hold(600);                                  // ten seconds of nothing
    check(!LocoIsFoot(),
          "**a VEHICLE verdict survives a long standstill** -- holding is symmetric");

    // ...but real foot motion overturns it immediately, because that is the one
    // signal the engine cannot produce while mounted.
    step(24200, -30100);
    check(LocoIsFoot(), "and one step on foot overturns it at once");
    check(logged("verdict FOOT"), "with the transition logged");

    // =======================================================================
    // 4. BOOTSTRAP. Before any evidence, an uncorroborated vehicle claim must
    //    read as FOOT rather than being believed.
    // =======================================================================
    reset(3, 86900, 6861);
    g_bguX = 20360; g_bguY = -3850;
    check(LocoIsFoot(), "UNKNOWN + uncorroborated Ship byte -> FOOT (no tick yet)");

    reset(48, 24100, -30050);
    g_bguX = 24000; g_bguY = -30000;
    check(!LocoIsFoot(), "UNKNOWN + corroborated Garden byte -> VEHICLE (no tick yet)");

    reset(0, 1000, 1000);
    check(LocoIsFoot(), "UNKNOWN + foot byte -> FOOT");

    reset(-1, 1000, 1000);
    check(LocoIsFoot(), "UNKNOWN + unsampled byte -> FOOT");

    // =======================================================================
    // 5. THE CORROBORATION BOUNDARY. 600 units, and nothing real is near it.
    // =======================================================================
    reset(48, 40000, 40000); g_engineVehId = 0;
    g_bguX = 40599; g_bguY = 40000; LocoTick();
    check(!LocoIsFoot(), "599 units from the mirror corroborates");
    reset(48, 40000, 40000); g_engineVehId = 0;
    g_bguX = 40601; g_bguY = 40000; LocoTick();
    check(LocoIsFoot(), "601 does not");

    // Inherited from v0.20.56 and deliberately kept: a foot position of exactly
    // (0,0) is treated as "we do not have a position", so the mirror cannot
    // corroborate from it. (0,0) is mid-ocean south-west of Balamb -- the player
    // is never standing there on foot -- and the alternative, letting an
    // unreadable position corroborate a vehicle claim, is the failure mode this
    // whole module exists to stop. Pinned so it is a decision and not a
    // surprise.
    reset(48, 0, 0); g_engineVehId = 0;
    g_bguX = 0; g_bguY = 0; LocoTick();
    check(LocoIsFoot(), "a foot position of (0,0) cannot corroborate anything");
    {
        // The two real cases sit 70 km and 50 km outside it, so this boundary is
        // not doing delicate work -- worth pinning so a future tweak knows.
        const double aaron = CalculateWrappedDistance(86900, 6861, 20360, -3850);
        const double car   = CalculateWrappedDistance(-15000, -39000, -71612, -14815);
        check(aaron > 60000, "Aaron's stale Ship claim missed by more than 60 km");
        check(car   > 50000, "the v0.20.56 stale car claim missed by more than 50 km");
    }

    // =======================================================================
    // 6. A FOOT BYTE NEVER NEEDS CORROBORATION, AND NEVER FORCES ONE EITHER.
    // =======================================================================
    check(LocoCorroborated(0, 0, 0, nullptr, nullptr), "byte 0 is foot, always corroborated");
    check(LocoCorroborated(6, 0, 0, nullptr, nullptr), "byte 6 is foot, always corroborated");
    // Chocobo piggybacks on the foot character and has no mirror of its own, so
    // it can never corroborate -- and must therefore never be believed over a
    // standing FOOT verdict. GetVehicleType(31) is VEH_CHOCOBO, which the
    // position source already treats as foot.
    check(GetVehicleType(31) == VEH_CHOCOBO, "31 is the Chocobo");
    reset(31, 5000, 5000);
    LocoTick();
    check(LocoIsFoot(), "a Chocobo reads as foot, which is what the position source wants");

    std::printf("%s -- %d bad\n", bad ? "FAIL" : "OK", bad);
    return bad ? 1 : 0;
}
