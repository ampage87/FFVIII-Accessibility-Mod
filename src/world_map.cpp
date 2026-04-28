// world_map.cpp - World map navigation TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.11.16 — Deferred catalog build (position validity check)
// ============================================================================
//
// Architecture mirrors FieldNavigation but simpler:
// - Hardcoded 37-entry location catalog (26 main + 7 chocobo + 4 alien)
// - On world map entry: compute distances, sort, freeze list
// - -/= cycle through sorted list with TTS announcement
// - Backspace announces bearing + distance to selected location
// - \ reserved for auto-drive (future)
// - Continuous polling: vehicle changes, world map entry/exit
//
// All addresses confirmed static (no pointer chains needed):
//   Position: 0x0203EE80/84/88 (X/Y/Z DWORDs)
//   Heading:  0x0203ED02 (WORD, 0-4095, 0=North CW)
//   Vehicle:  0x02040A5E (BYTE, locomotion mode)
//   Scene:    0x0203ED2C (WORD, 0=worldmap 1=field/battle)

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "world_map.h"

// Forward declarations for namespaces
namespace Log { void World(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); }

namespace WorldMap {

// ============================================================================
// Confirmed static addresses (from deep research + ff8-speedruns + v0.11.02 diag)
// ============================================================================
static const uint32_t WM_POS_X      = 0x0203EE80;  // DWORD - player X
static const uint32_t WM_POS_Y      = 0x0203EE84;  // DWORD - player Y
static const uint32_t WM_POS_Z      = 0x0203EE88;  // DWORD - player Z
static const uint32_t WM_HEADING    = 0x0203ED02;   // WORD  - 0=North, 0-4095 CW
static const uint32_t WM_LOCOMOTION = 0x02040A5E;   // BYTE  - vehicle mode
static const uint32_t WM_SCENE_FLAG = 0x0203ED2C;   // WORD  - 0=worldmap, 1=field

// World map dimensions (wrapping torus)
static const double WM_WIDTH  = 262144.0;
static const double WM_HEIGHT = 196608.0;

// ============================================================================
// Location catalog — 37 entries from ff8-speedruns/ff8-memory world-map.md
// ============================================================================
struct LocationEntry {
    const char* name;
    int32_t x;
    int32_t y;
};

// The location catalog from v0.11.01 — hardcoded for low maintenance.
// Names match the ff8-speedruns data minus confusing parentheticals.
// Coordinates checked manually with Cheat Engine at each location.
static const LocationEntry s_locations[] = {
    // Primary locations (26)
    {"Balamb Garden", 70784, 152832},
    {"Balamb Town", 73984, 151040},
    {"Fire Cavern", 81152, 146176},
    {"SeeD Graduation Ball", 73472, 148736},
    {"Timber", 53504, 133120},
    {"Timber Maniacs Building", 53248, 133120},
    {"SeeD on Train", 51456, 137216},
    {"Galbadia Garden", 51200, 108800},
    {"Deling City", 61184, 112128},
    {"Tomb of the Unknown King", 63744, 104448},
    {"Winhill", 40704, 110592},
    {"D-District Prison", 58368, 124672},
    {"Missile Base", 62976, 115712},
    {"Trabia Garden", 150016, 48896},
    {"Shumi Village", 139520, 45312},
    {"Balamb Garden MD Level", 72960, 152064},
    {"Fishermans Horizon", 19456, 130816},
    {"Edea House", 155648, 127744},
    {"Great Salt Lake", 196608, 85504},
    {"Centra Ruins", 199936, 97280},
    {"Esthar City", 204032, 57344},
    {"Dr. Odines Lab", 203520, 57344},
    {"Research Center", 207616, 69888},
    {"Sorceress Memorial", 199424, 45824},
    {"Lunar Gate", 204800, 72960},
    {"Crystal Pillar", 206336, 108032},
    // Chocobo Forests (7)
    {"Beginner Forest", 85248, 149248},
    {"Basics Forest", 51712, 100096},
    {"Roaming Forest", 155136, 47872},
    {"Enclosed Forest", 20736, 133120},
    {"Sanctuary", 206080, 92416},
    {"Pockets Forest", 83456, 116480},
    {"Sorc Forest", 208896, 124416},
    // Alien Ships (4) — confirmed by v0.11.02 BFS diagnostic
    {"Alien Ship Forest", 83456, 116224},
    {"Alien Ship Centra", 199936, 95744},
    {"Alien Ship Kashkabald", 206080, 92416},
    {"Alien Ship Esthar", 210176, 57600}
};
static const int LOCATION_COUNT = sizeof(s_locations) / sizeof(s_locations[0]);

// ============================================================================
// Navigation state
// ============================================================================
static struct LocationEntry s_catalog[LOCATION_COUNT];  // distance-sorted working copy
static bool s_catalogBuilt = false;
static int s_catalogIndex = 0;       // current selected location (0 = nearest)
static int s_lastVehicle = -1;       // last known vehicle state
static bool s_onWorldMap = false;    // true when on world map
static DWORD s_lastMovementTick = 0; // last time position changed significantly

// ============================================================================
// Coordinate utility functions
// ============================================================================
static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z)
{
    __try {
        *x = *(int32_t*)WM_POS_X;
        *y = *(int32_t*)WM_POS_Y;
        *z = *(int32_t*)WM_POS_Z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *x = *y = *z = 0;
    }
}

static uint16_t GetWorldMapHeading()
{
    uint16_t heading = 0;
    __try {
        heading = *(uint16_t*)WM_HEADING;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        heading = 0;
    }
    return heading;
}

static uint8_t GetLocomotionMode()
{
    uint8_t mode = 0;
    __try {
        mode = *(uint8_t*)WM_LOCOMOTION;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode = 0;
    }
    return mode;
}

static bool IsOnWorldMap()
{
    uint16_t scene = 1;  // default to field (not worldmap)
    __try {
        scene = *(uint16_t*)WM_SCENE_FLAG;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        scene = 1;
    }
    return (scene == 0);
}

// Wrap-aware distance calculation on a torus
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = abs(x2 - x1);
    double dy = abs(y2 - y1);
    
    // Check if wrapping gives shorter distance
    if (dx > WM_WIDTH / 2)  dx = WM_WIDTH - dx;
    if (dy > WM_HEIGHT / 2) dy = WM_HEIGHT - dy;
    
    return sqrt(dx * dx + dy * dy);
}

// ============================================================================
// Catalog management
// ============================================================================
static void BuildDistanceCatalog()
{
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    // Copy all locations and compute distances
    for (int i = 0; i < LOCATION_COUNT; i++) {
        s_catalog[i] = s_locations[i];
        // Store distance in x field temporarily for sorting
        int32_t dist = (int32_t)CalculateWrappedDistance(px, py, s_locations[i].x, s_locations[i].y);
        s_catalog[i].x = dist;  // temporarily store distance here
    }
    
    // Sort by distance (ascending)
    std::sort(s_catalog, s_catalog + LOCATION_COUNT, [](const LocationEntry& a, const LocationEntry& b) {
        return a.x < b.x;  // x field contains distance
    });
    
    // Restore original coordinates
    for (int i = 0; i < LOCATION_COUNT; i++) {
        for (int j = 0; j < LOCATION_COUNT; j++) {
            if (strcmp(s_catalog[i].name, s_locations[j].name) == 0) {
                s_catalog[i].x = s_locations[j].x;
                s_catalog[i].y = s_locations[j].y;
                break;
            }
        }
    }
    
    s_catalogBuilt = true;
    s_catalogIndex = 0;
    Log::World("WorldMap: Catalog built, nearest location: %s", s_catalog[0].name);
}

// ============================================================================
// Navigation announcements
// ============================================================================
static void AnnounceLocation(int index)
{
    if (index < 0 || index >= LOCATION_COUNT || !s_catalogBuilt) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    double distance = CalculateWrappedDistance(px, py, s_catalog[index].x, s_catalog[index].y);
    int distanceKm = (int)(distance / 1000.0);  // rough conversion to kilometers
    
    char buf[256];
    if (distanceKm < 1) {
        snprintf(buf, sizeof(buf), "%s. Very close.", s_catalog[index].name);
    } else {
        snprintf(buf, sizeof(buf), "%s. %d kilometers away.", s_catalog[index].name, distanceKm);
    }
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [LOCATION] %s", buf);
}

static void AnnounceBearing()
{
    if (!s_catalogBuilt || s_catalogIndex >= LOCATION_COUNT) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    // Calculate bearing to selected location
    int32_t tx = s_catalog[s_catalogIndex].x;
    int32_t ty = s_catalog[s_catalogIndex].y;
    
    // Handle world wrapping for shortest path
    int32_t dx = tx - px;
    int32_t dy = ty - py;
    
    if (abs(dx) > WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else dy += (int32_t)WM_HEIGHT;
    }
    
    // Convert to bearing (0=North, clockwise)
    double radians = atan2(dx, -dy);  // -dy because FF8 Y increases downward
    double degrees = radians * 180.0 / 3.14159;
    if (degrees < 0) degrees += 360.0;
    
    const char* direction;
    if (degrees < 22.5 || degrees >= 337.5) direction = "North";
    else if (degrees < 67.5) direction = "Northeast";
    else if (degrees < 112.5) direction = "East";
    else if (degrees < 157.5) direction = "Southeast";
    else if (degrees < 202.5) direction = "South";
    else if (degrees < 247.5) direction = "Southwest";
    else if (degrees < 292.5) direction = "West";
    else direction = "Northwest";
    
    double distance = CalculateWrappedDistance(px, py, tx, ty);
    int distanceKm = (int)(distance / 1000.0);
    
    char buf[256];
    snprintf(buf, sizeof(buf), "%s. %s, %d kilometers.", 
             s_catalog[s_catalogIndex].name, direction, distanceKm);
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [BEARING] %s", buf);
}

// ============================================================================
// Vehicle state tracking
// ============================================================================
static const char* GetVehicleName(uint8_t mode)
{
    switch (mode) {
        case 0: return "On foot";
        case 1: return "Car";
        case 2: return "Chocobo";
        case 3: return "Ship";
        case 4: return "Ragnarok";
        default: return "Unknown vehicle";
    }
}

static void CheckVehicleChange()
{
    uint8_t vehicle = GetLocomotionMode();
    if (vehicle != s_lastVehicle) {
        if (s_lastVehicle != -1) {  // skip initial announcement
            const char* newVehicle = GetVehicleName(vehicle);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.", newVehicle);
            ScreenReader::Speak(buf, true);  // interrupt previous speech
            Log::World("WorldMap: Vehicle change: %s (mode %u)", newVehicle, vehicle);
        }
        s_lastVehicle = vehicle;
    }
}

// ============================================================================
// Keyboard input handling
// ============================================================================
void HandleKeyPress(UINT vkCode)
{
    if (!s_onWorldMap) return;
    
    if (vkCode == VK_OEM_MINUS) {  // '-' key - previous location
        if (s_catalogBuilt) {
            s_catalogIndex = (s_catalogIndex - 1 + LOCATION_COUNT) % LOCATION_COUNT;
            AnnounceLocation(s_catalogIndex);
        }
    }
    else if (vkCode == VK_OEM_PLUS) {  // '=' key - next location  
        if (s_catalogBuilt) {
            s_catalogIndex = (s_catalogIndex + 1) % LOCATION_COUNT;
            AnnounceLocation(s_catalogIndex);
        }
    }
    else if (vkCode == VK_BACK) {  // Backspace - announce bearing
        AnnounceBearing();
    }
}

// ============================================================================
// Main polling loop
// ============================================================================
void Poll()
{
    bool nowOnWorldMap = IsOnWorldMap();
    
    // Detect world map entry
    if (nowOnWorldMap && !s_onWorldMap) {
        s_onWorldMap = true;
        s_catalogBuilt = false;  // force rebuild on next poll
        Log::World("WorldMap: Entered world map");
        
        // Announce entering world map
        ScreenReader::Speak("World map.", true);
        
        // Check if we're in the right game mode for world map functionality
        __try {
            if (FF8Addresses::pGameMode && *FF8Addresses::pGameMode != FF8Addresses::MODE_WORLDMAP) {
                Log::World("WorldMap: Warning - On world map but game mode is %u (expected %u)", 
                          *FF8Addresses::pGameMode, FF8Addresses::MODE_WORLDMAP);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::World("WorldMap: Exception checking game mode");
        }
    }
    
    // Detect world map exit
    if (!nowOnWorldMap && s_onWorldMap) {
        s_onWorldMap = false;
        s_catalogBuilt = false;
        Log::World("WorldMap: Exited world map");
    }
    
    if (!s_onWorldMap) return;
    
    // Build catalog on first poll while on world map
    if (!s_catalogBuilt) {
        BuildDistanceCatalog();
    }
    
    // Check for vehicle changes
    CheckVehicleChange();
    
    // Track significant position changes for future auto-drive features
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    static int32_t lastX = 0, lastY = 0;
    
    double movement = CalculateWrappedDistance(px, py, lastX, lastY);
    if (movement > 1000) {  // moved more than 1km
        s_lastMovementTick = GetTickCount();
        lastX = px;
        lastY = py;
    }
}

// ============================================================================
// Module initialization
// ============================================================================
void Initialize()
{
    s_onWorldMap = false;
    s_catalogBuilt = false;
    s_catalogIndex = 0;
    s_lastVehicle = -1;
    s_lastMovementTick = 0;
    
    Log::World("WorldMap: Module initialized (v0.11.16)");
}

// ============================================================================
// Public Update entry — called every ~16ms from the accessibility thread.
// Restored v0.14.31 (was deleted from world_map.cpp during build damage).
// Thin wrapper: defers all polling/announcement work to internal Poll().
// ============================================================================
void Update()
{
    Poll();
}

// ============================================================================
// Public Shutdown entry — restored v0.14.31.
// Resets module state. No hooks installed by this module so nothing to unhook.
// ============================================================================
void Shutdown()
{
    s_onWorldMap = false;
    s_catalogBuilt = false;
    s_catalogIndex = 0;
    s_lastVehicle = -1;
    s_lastMovementTick = 0;
    Log::World("WorldMap: Shutdown complete.");
}

} // namespace WorldMap