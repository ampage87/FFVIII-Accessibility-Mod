// Replays the vehicle-id / savemap sequence actually observed in the v0.20.54
// BAT (ff8_world.log, 2026-08-12 13:08-13:12) through Garden_UpdateAboard and
// asserts the latch now behaves. This is the regression guard for the .54
// failure: the engine id at 0x020409E0 names a vehicle only while one is
// MOVING, so a spot read at catalog-build time is always 0.
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
namespace Log { void World(const char* f, ...) { va_list a; va_start(a,f); vprintf(f,a); va_end(a); printf("\n"); } }
namespace FieldDialog { bool IsDialogOpen() { return false; } }   // v0.20.87
namespace ScreenReader { bool Speak(const char* t, bool = false) { printf("   SPEAK: %s\n", t); return true; } }
static DWORD g_now = 0;
static DWORD GetTickCount() { return g_now; }
static const double WM_WIDTH = 262144.0, WM_HEIGHT = 196608.0;
static const int MAX_LOCATIONS = 64;
struct LocationEntry { const char* name; int32_t x, y; };
static LocationEntry s_catalog[MAX_LOCATIONS];
static int s_catalogCount = 0, s_catalogIndex = 0;
static bool s_catalogBuilt = true;
static const int LOCATION_COUNT = 39, DRIVE_PATH_MAX = 768;
static uint16_t s_drivePath[DRIVE_PATH_MAX];
static int32_t s_drivePathWX[DRIVE_PATH_MAX], s_drivePathWY[DRIVE_PATH_MAX];
static bool s_drivePathWorld = false, s_drivePathPlanned = false;
static int s_drivePathLen = 0, s_drivePathIdx = 0;
static uint8_t g_sav[64];
static const uintptr_t WM_BGU_POS_ADDR  = (uintptr_t)g_sav;
static const uintptr_t WM_CHAR_POS_ADDR = (uintptr_t)(g_sav + 16);
static const uint32_t WMS_VEHPOS_X_OFF=0, WMS_VEHPOS_Y_OFF=4, WMS_VEHPOS_Z_OFF=8, WMS_VEHPOS_ROT_OFF=10;
// v0.20.64: the harness fakes the savemap. Addresses the test does not plant
// (the story word, for one) must fail cleanly rather than dereference.
static bool WmValidTestAddr(uintptr_t a)
{ return a >= (uintptr_t)g_sav && a + 16 <= (uintptr_t)g_sav + sizeof(g_sav); }
static bool WmSafeReadBytes(uintptr_t a, void* o, size_t n)
{ if (!WmValidTestAddr(a)) return false; memcpy(o,(const void*)a,n); return true; }
static inline uint16_t PackSeg(int r,int c){return (uint16_t)(((r&0xFF)<<8)|(c&0xFF));}
static double CalculateWrappedDistance(int32_t x1,int32_t y1,int32_t x2,int32_t y2){
    double dx=fabs((double)(x2-x1)),dy=fabs((double)(y2-y1));
    if(dx>WM_WIDTH/2)dx=WM_WIDTH-dx; if(dy>WM_HEIGHT/2)dy=WM_HEIGHT-dy; return sqrt(dx*dx+dy*dy);}
static int TorusBearing(int32_t ax,int32_t ay,int32_t bx,int32_t by){
    double r=atan2((double)(bx-ax),-(double)(by-ay)); if(r<0)r+=6.283185307179586;
    return (int)(r/6.283185307179586*4096.0)&0xFFF;}
static int32_t g_px=0,g_py=0;  static int g_id=0;
static void GetWorldMapPosition(int32_t*x,int32_t*y,int32_t*z){*x=g_px;*y=g_py;*z=0;}
static void GetWorldMapPosition_Active(int32_t*x,int32_t*y,int32_t*z){*x=g_px;*y=g_py;*z=0;}
static uint16_t GetWorldMapHeading(){return 0;}
static int GetActiveVehicleId(){return g_id;}
static void SetDriveKeys(bool,bool,bool,bool=false){}
static void ReleaseAllDriveKeys(){}
struct GardenPark{const char*name;int32_t park_x,park_y,walk_units;bool reachable;bool drive_in;int32_t dock_x,dock_y;bool beach_climb;};
static const GardenPark* Garden_ParkFor(const char*);
static void Garden_ComputeReach(int32_t,int32_t);
static bool Garden_CellReachable(int32_t,int32_t);
// v0.20.64: world_map_state.inl symbols the trigger-state diagnostic reads.
static const uint32_t WM_STORY_FLAG = 0x02036BDE;
static const int WMX_SEG_ROWS = 24, WMX_SEG_COLS = 32;
static uint8_t s_segmentRegionMap[WMX_SEG_ROWS][WMX_SEG_COLS];
static bool s_segmentRegionLoaded = false;
#include "wm_distance_pure.inl"
#include "world_garden_dump.inl"
#include "world_garden_grid.inl"
#include "world_garden_berths.inl"
#include "world_garden_inlets.inl"   // v0.55.0: struct GardenDock moved here at v0.53.0;
                                   // without it this test stopped compiling and nobody noticed.
#include "world_garden_plan.inl"
#include "world_garden_probe.inl"
#include "world_garden.inl"

static void setMirror(uintptr_t base, int32_t x, int32_t y)
{ *(int32_t*)(base+0)=x; *(int32_t*)(base+4)=y; }

static int fails = 0;
static void check(const char* what, bool got, bool want)
{ printf("%-58s got=%d want=%d %s\n", what, (int)got, (int)want, got==want?"OK":"** FAIL **"); if(got!=want) fails++; }

int main()
{
    Garden_BuildBegin();
    Garden_RasterizeTri(0,0,0, 4000,0,0, 0,4000,0, 33, 0xFF, 0xFF);   // v0.20.73: terrain byte (33 = water)
    Garden_BuildEnd();

    // 13:08:42 world-map entry, position not yet valid (P=(0,0,0) in the log)
    Garden_OnWorldMapEntry();
    g_id = 0; g_px = 0; g_py = 0;
    setMirror(WM_CHAR_POS_ADDR, 20914, -24944);
    setMirror(WM_BGU_POS_ADDR,  20271, -24355);
    Garden_UpdateAboard();
    check("entry with invalid position -> latch unchanged", Garden_IsAboard(), false);

    // 13:08:51 the hull moves; the engine finally names it
    g_px = 21273; g_py = -24666; g_id = 0x30;
    s_catalogBuilt = true;
    Garden_UpdateAboard();
    check("id reads 48 while moving -> aboard", Garden_IsAboard(), true);
    check("  and the catalog is forced to rebuild", s_catalogBuilt, false);

    // 13:09:48 parked at the Fire Cavern berth; the id falls back to 0.
    // THIS is the .54 bug: a spot read here started a FOOT drive.
    g_id = 0; g_px = 30762; g_py = -28717;
    Garden_UpdateAboard();
    check("id falls back to 0 while parked -> STILL aboard", Garden_IsAboard(), true);

    // 13:10:34 the player disembarks: a fresh entry where the live position
    // equals char_pos exactly and bgu_pos is 1015 units away (real log values)
    Garden_OnWorldMapEntry();
    g_px = 30195; g_py = -27881; g_id = 0;
    setMirror(WM_CHAR_POS_ADDR, 30195, -27881);
    setMirror(WM_BGU_POS_ADDR,  30764, -28722);
    s_catalogBuilt = true;
    Garden_UpdateAboard();
    check("entry, P==char_pos and bgu 1015 away -> on foot", Garden_IsAboard(), false);
    check("  and the catalog is forced to rebuild", s_catalogBuilt, false);

    // the mirror image: entry while piloting (P tracks bgu, char is stale)
    Garden_OnWorldMapEntry();
    g_px = 30764; g_py = -28722; g_id = 0;
    setMirror(WM_CHAR_POS_ADDR, 20914, -24944);
    setMirror(WM_BGU_POS_ADDR,  30764, -28722);
    Garden_UpdateAboard();
    check("entry, P==bgu_pos and char far -> aboard", Garden_IsAboard(), true);

    // ---- v0.20.55 BAT: stepping off the mobile Garden with NO world-map
    // entry at all. The hull parks at (-23366,-3987); the player walks off.
    g_id = 0x30; g_px = -23366; g_py = -3987;
    Garden_UpdateAboard();                      // hull position recorded here
    check("driving: aboard, hull position recorded", Garden_IsAboard(), true);
    g_id = 0;                                    // parked -> id falls back to 0
    Garden_UpdateAboard();
    check("parked, sitting in the hull -> still aboard", Garden_IsAboard(), true);
    g_px = -22697; g_py = -3813;                 // the real .55 coords, 816u away
    g_now = 1000;  Garden_UpdateAboard();
    check("just stepped off (816u, <700ms) -> still aboard", Garden_IsAboard(), true);
    g_now = 2000;  Garden_UpdateAboard();
    check("816u from the hull for >700ms -> ON FOOT, no entry needed", Garden_IsAboard(), false);
    check("  and the catalog is forced to rebuild", s_catalogBuilt, false);

    // a different vehicle in motion clears it immediately
    s_gdAboard = true;
    g_id = 0x21;
    Garden_UpdateAboard();
    check("a different vehicle id (car 0x21) -> not aboard", Garden_IsAboard(), false);

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL CHECKS PASSED", fails);
    return fails != 0;
}
