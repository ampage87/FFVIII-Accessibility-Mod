// world_map.cpp - World map navigation TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.14.85.3 — Chapter 1 hotfix #3. Aaron clarified after the
//                v0.14.85.2 build: he doesn't have Ragnarok yet — the mode 4
//                seen in BAT was NOT a Ragnarok mount, it was a transient
//                locomotion-byte read at a Fire Cavern field-transition
//                moment. The v0.14.83 'mode 4 = Ragnarok' tag was an
//                assumption Claude made from an earlier BAT log, never
//                confirmed against actual gameplay. Per the research doc,
//                Ragnarok is mode 50. v0.14.85.3 drops the unvalidated mode
//                4 = Ragnarok mapping (now defaults to ON_FOOT) and tightens
//                the locomotion whitelist to an explicit list of canonical
//                modes {0, 3, 6, 31, 32-40, 48, 50} per the research doc.
//                The v0.14.85.2 type-change-triggered catalog rebuild stays;
//                its trigger is also refined to compare BFS rule class
//                (land-only / ocean-allowed / no-filter) instead of full
//                VehicleType, so foot↔car transitions don't trigger
//                gratuitous rebuilds.
//                Auto-drive (Chapter 2) still pending; `\` remains a placeholder.
//
//   Prior baseline:
//   v0.11.16 — Deferred catalog build (position validity check)
//   v0.14.31 — Update()/Shutdown() restored after v0.14.24 build damage
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
#include <cstdlib>
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
static const uint32_t WM_LOCOMOTION = 0x02040A5E;   // BYTE  - locomotion / vehicle mode. v0.14.83 whitelisted to canonical {0..4}; per `Plan & Research Documents/World Map Terrain and Locomotion Reference.md` legitimate values include 0=Squall foot, 6=Selphie foot, 10=train, 31=Chocobo, 32=invisible-car — our GetVehicleName uses 0/1/2/3/4 for foot/Car/Chocobo/Ship/Ragnarok which DISAGREES with the research-doc enum (research says Chocobo=31, we say 2). Empirical reconciliation needed; see v0.14.84 changelog.
static const uint32_t WM_SCENE_FLAG = 0x0203ED2C;   // WORD  - 0=worldmap, 1=field

// World map dimensions (wrapping torus)
static const double WM_WIDTH  = 262144.0;
static const double WM_HEIGHT = 196608.0;

// ============================================================================
// wmx.obj terrain grid constants (v0.14.85 restoration from v0.11.16 impl)
// ============================================================================
// world.fi entry 9 points at wmx.obj inside world.fs. The mesh contains 835
// segments. Of those, the first 768 form the playable 32x24 grid; the remainder
// are story variants. Each segment is exactly 36864 (0x9000) bytes and is
// structured as:
//   bytes 0..3   : group_id (uint32)
//   bytes 4..67  : 16 x uint32 block offsets (relative to segment base)
//   then 16 variable-length blocks at the offsets above
//   then padding to 36864.
// Each block starts with a 4-byte header:
//   byte 0: poly_count (1 byte)
//   byte 1: vert_count (1 byte)
//   byte 2: norm_count (1 byte)
//   byte 3: pad
// Followed by poly_count x 16-byte polygon records, vert_count x 8-byte
// vertices, norm_count x 8-byte normals, and a 4-byte end pad.
// Each polygon's terrain (ground type) byte lives at polygon offset 0x0D.
// Ocean values are 32 (shallow) / 33 (light) / 34 (dark); everything else
// is land. Source: `Plan & Research Documents/wmx.obj polygon format deep
// research findings.md`. Earlier reconstructed parsers that scanned a flat
// `segment[poly * 16 + 13]` stride from segment offset 0 (ignoring the
// per-block headers) read garbage bytes — v0.14.85 BAT classified all 768
// segments as land because the garbage-byte distribution rarely landed in
// the 32-34 range. The correct walker per the research doc is implemented
// in v0.14.85.1's LoadTerrainGrid.
static const int      WMX_FL_INDEX        = 9;
static const int      WMX_SEG_COLS        = 32;
static const int      WMX_SEG_ROWS        = 24;
static const int      WMX_PLAYABLE_SEGS   = 768;
static const int      WMX_TOTAL_SEGS      = 835;
static const uint32_t WMX_SEGMENT_SIZE    = 36864;
static const int      WMX_BLOCKS_PER_SEG  = 16;
static const int      WMX_SEG_HEADER_SIZE = 4 + 16 * 4;   // group_id + 16 block offsets = 68 bytes
static const int      WMX_BLOCK_HDR_SIZE  = 4;            // poly_count + vert_count + norm_count + pad
static const int      WMX_POLY_SIZE       = 16;
static const int      WMX_TERRAIN_OFFSET  = 0x0D;

// Vehicle classification used by the BFS reachability rules.
// Locomotion enum values per `Plan & Research Documents/World Map Terrain
// and Locomotion Reference.md`:
//   0  = Squall on foot       6  = Selphie on foot
//   3  = Ship (BAT-validated v0.14.83)
//   31 = Chocobo             32-40 = Cars
//   48 = Garden (mobile)     50 = Ragnarok
// Mode 4 is NOT in the canonical list. Earlier builds (v0.14.83-v0.14.85.2)
// tagged it 'Ragnarok' based on a v0.14.82 BAT log where Claude assumed the
// 'Ragnarok session' label, but Aaron's v0.14.85.2 BAT clarified he doesn't
// have Ragnarok in the current save — mode 4 was a transient byte read at
// a Fire Cavern field-transition moment, not a vehicle. v0.14.85.3 drops
// the assumption: mode 4 (and any other non-canonical value) defaults to
// VEH_ON_FOOT for BFS purposes and is silently ignored by CheckVehicleChange.
enum VehicleType {
    VEH_ON_FOOT,
    VEH_CHOCOBO,
    VEH_CAR,
    VEH_GARDEN,
    VEH_RAGNAROK
};

// ============================================================================
// Location catalog — v0.14.85.1: rebuilt from canonical research doc
// ============================================================================
// Source: `Plan & Research Documents/World Map Location Coordinates Research
// Findings.md`. The doc enumerates the 26 numbered world-map markers from
// FinalFantasyKingdom + 7 chocobo forests + 4 alien encounters with X/Y/Z
// coords from the ff8-speedruns/ff8-memory dataset — the SAME coordinate
// system the player position address (`FF8_EN.exe+1C3EE80`) reports at
// runtime, so distances and BFS segment-mapping work without conversion.
// Plus Fire Cavern from the v0.11.11 wmx.obj analysis (it was missing from
// the canonical 26).
//
// v0.14.85 BAT exposed two flaws in the prior catalog: (a) it used a
// completely different coordinate system (positive-Y, magnitudes mismatched
// with the runtime player-position address by ~50000 units) so BFS
// reachability was effectively random, and (b) it included interior /
// event-only locations like 'Timber Maniacs Building', 'SeeD Graduation
// Ball', 'SeeD on Train', 'Balamb Garden MD Level', and 'Dr. Odines Lab'
// that aren't world-map entry points at all.
//
// This rebuild matches the canonical FF8 world-map entry-point set: every
// location here is a place you can walk/drive/fly TO from the overworld
// (not an interior accessed from another field).
struct LocationEntry {
    const char* name;
    int32_t x;
    int32_t y;
};

static const LocationEntry s_locations[] = {
    // Numbered markers 1-26 (canonical FinalFantasyKingdom set)
    {"Balamb Garden",              24576,  -29406},
    {"Balamb Town",                13249,  -26779},   // canonical name 'Balamb'; kept 'Town' for clarity vs Garden
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
    {"Esthar City",                57011,   -2295},   // canonical 'Esthar'; kept 'City' for clarity
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

    // Chocobo Forests (7) — generic numbering; the in-game forests don't have
    // canon names. Geographic hints in the comments help during testing.
    {"Chocobo Forest 1",           11332,  -63659},   // Trabia Continent (south of Trabia Garden)
    {"Chocobo Forest 2",           10927,  -81010},   // Trabia Continent (north)
    {"Chocobo Forest 3",           51893,   -3959},   // Esthar coast / FH region
    {"Chocobo Forest 4",           97253,  -48250},   // Far East / Esthar mountains
    {"Chocobo Forest 5",           17383,   22013},   // South Galbadia
    {"Chocobo Forest 6",           44504,   76259},   // Centra Continent
    {"Chocobo Forest 7",          -20953,   68906},   // Centra / Edea's House region

    // Alien Encounter (UFO/PuPu) sites (4)
    {"Alien Ship 1",               79823,  -61212},   // Esthar / Trabia border
    {"Alien Ship 2",               40495,   54649},   // Centra Continent
    {"Alien Ship 3",              -12952,  -10202},   // Galbadia Continent
    {"Alien Ship 4",              -48806,    5808},   // West Galbadia

    // Fire Cavern — missing from the canonical 26 marker list because the
    // FinalFantasyKingdom set is Ragnarok-era; Fire Cavern is the early-game
    // dungeon on Balamb Island. Coords from the v0.11.11 wmx.obj polygon
    // analysis (segment(20,20), 6 terrain-29 polygons, small cave entrance).
    {"Fire Cavern",                36864,  -28672}
};
static const int LOCATION_COUNT = sizeof(s_locations) / sizeof(s_locations[0]);

// ============================================================================
// Navigation state
// ============================================================================
static struct LocationEntry s_catalog[LOCATION_COUNT];  // distance-sorted working copy
static int  s_catalogCount = 0;        // v0.14.85: post-filter count (≤ LOCATION_COUNT)
static bool s_catalogBuilt = false;
static int s_catalogIndex = 0;       // current selected location (0 = nearest)
static int s_lastVehicle = -1;       // last known vehicle state
static bool s_onWorldMap = false;    // true when on world map
static DWORD s_lastMovementTick = 0; // last time position changed significantly

// ============================================================================
// Terrain grid + BFS reachability state (v0.14.85)
// ============================================================================
// s_terrainGrid[row][col]: 0 = LAND, 1 = OCEAN. Loaded once at module init from
// wmx.obj inside world.fs. s_reachable[row][col] is rebuilt per catalog build
// via BFS flood-fill from the player's current segment.
static uint8_t s_terrainGrid[WMX_SEG_ROWS][WMX_SEG_COLS];
static uint8_t s_reachable  [WMX_SEG_ROWS][WMX_SEG_COLS];
static bool    s_terrainLoaded = false;

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
// File I/O helpers (v0.14.85, restored from v0.11.12 impl)
// ============================================================================
// Mirrors the field_archive.cpp pattern but uses raw uint8_t* with malloc/free
// rather than std::vector to keep the world_map module independent of
// field_archive's internals. The duplicated LZSS function below has the same
// rationale; both could be lifted to a shared header in a future refactor.
static bool WM_ReadFileToBuffer(const char* path, uint8_t** outData, uint32_t* outSize)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    *outData = (uint8_t*)malloc((size_t)sz);
    if (!*outData) { fclose(f); return false; }
    size_t rd = fread(*outData, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(*outData); *outData = nullptr; return false; }
    *outSize = (uint32_t)sz;
    return true;
}

static bool WM_ReadFileChunk(const char* path, uint32_t offset, uint32_t size, uint8_t** outData)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return false; }
    *outData = (uint8_t*)malloc(size);
    if (!*outData) { fclose(f); return false; }
    size_t rd = fread(*outData, 1, size, f);
    fclose(f);
    if (rd != size) { free(*outData); *outData = nullptr; return false; }
    return true;
}

// ============================================================================
// LZSS decompression (v0.14.85, restored from v0.11.12 impl)
// ============================================================================
// Standard FF8/FF7 LZSS variant. 4096-byte ring buffer, ringPos starts at
// 0xFEE. Flag byte's bits read LSB-first: 1=literal byte, 0=back-reference
// (12-bit offset, 4-bit length+3). Identical to field_archive.cpp's
// DecompressLZSS but file-private here for module independence.
static bool WM_DecompressLZSS(const uint8_t* input, uint32_t inputSize,
                              uint8_t* output, uint32_t outputSize)
{
    uint8_t ring[4096];
    memset(ring, 0, sizeof(ring));
    int ringPos = 0xFEE;
    uint32_t inPos = 0, outPos = 0;

    while (outPos < outputSize && inPos < inputSize) {
        uint8_t flags = input[inPos++];
        for (int bit = 0; bit < 8 && outPos < outputSize; bit++) {
            if (flags & (1 << bit)) {
                if (inPos >= inputSize) return false;
                uint8_t b = input[inPos++];
                output[outPos++] = b;
                ring[ringPos] = b;
                ringPos = (ringPos + 1) & 0xFFF;
            } else {
                if (inPos + 1 >= inputSize) return false;
                uint8_t b1 = input[inPos++];
                uint8_t b2 = input[inPos++];
                int off = b1 | ((b2 & 0xF0) << 4);
                int len = (b2 & 0x0F) + 3;
                for (int i = 0; i < len && outPos < outputSize; i++) {
                    uint8_t b = ring[(off + i) & 0xFFF];
                    output[outPos++] = b;
                    ring[ringPos] = b;
                    ringPos = (ringPos + 1) & 0xFFF;
                }
            }
        }
    }
    return (outPos == outputSize);
}

// ============================================================================
// Coordinate conversion: game world coords → segment grid (v0.14.85)
// ============================================================================
// World map torus is 262144 x 196608, divided into a 32 x 24 grid of 8192-unit
// segments. The X axis has a non-zero origin offset: per wmx.obj analysis,
// game_X = seg_col * 8192 + 4096 - 131072. The +131072 below is the inverse
// of that offset — without it, BFS starts in an ocean cell and filters
// everything as unreachable. This was the v0.11.16 fix that finally made
// terrain BFS work end-to-end ("Driving worked as expected!" — Aaron's BAT).
// Y axis aligns naturally because the torus wrap absorbs any constant offset.
static int WorldXToSegCol(int32_t x)
{
    int32_t shifted = x + 131072;
    int32_t nx = ((shifted % 262144) + 262144) % 262144;
    return (nx / 8192) % WMX_SEG_COLS;
}

static int WorldYToSegRow(int32_t y)
{
    int32_t ny = ((y % 196608) + 196608) % 196608;
    return (ny / 8192) % WMX_SEG_ROWS;
}

// ============================================================================
// Vehicle classification (v0.14.85.3)
// ============================================================================
// Maps the raw locomotion byte to a coarse VehicleType used by reachability
// rules. Conservative: only modes from the canonical list get non-foot
// classifications. Unknown modes (including the transient mode 4 seen at
// field-transition moments) default to VEH_ON_FOOT — safest for filtering
// because the player IS likely on foot if they aren't in a known vehicle.
static VehicleType GetVehicleType(uint8_t mode)
{
    if (mode == 0 || mode == 6) return VEH_ON_FOOT;       // Squall / Selphie foot
    if (mode == 3)               return VEH_GARDEN;       // Ship: ocean access, BAT-validated v0.14.83
    if (mode == 31)              return VEH_CHOCOBO;
    if (mode >= 32 && mode <= 40) return VEH_CAR;
    if (mode == 48)              return VEH_GARDEN;       // Garden mobile (ocean access)
    if (mode == 50)              return VEH_RAGNAROK;     // No filter (flies anywhere)
    return VEH_ON_FOOT;                                    // safe default for unknown / transient values
}

// Three BFS rule classes used by the v0.14.85.3 type-change-triggered rebuild:
// 0 = land-only (foot, chocobo, car), 1 = ocean-allowed (Ship, Garden),
// 2 = no-filter (Ragnarok). A rebuild only fires when the rule class
// changes, so foot ↔ car ↔ chocobo transitions (all land-only) don't trigger
// gratuitous rebuilds.
static int GetBfsRuleClass(VehicleType v)
{
    if (v == VEH_RAGNAROK) return 2;
    if (v == VEH_GARDEN)   return 1;
    return 0;  // VEH_ON_FOOT, VEH_CHOCOBO, VEH_CAR all share land-only rules
}

// Whitelist of canonical locomotion-byte values. The byte at WM_LOCOMOTION
// drifts through transient values (animation phase counters, field-transition
// state, etc.) and announcing those would be noise. Only canonical values per
// the research doc are eligible for vehicle-change announcements.
static bool IsCanonicalLocomotion(uint8_t mode)
{
    return mode == 0 || mode == 3 || mode == 6 ||
           mode == 31 ||
           (mode >= 32 && mode <= 40) ||
           mode == 48 || mode == 50;
}

// True iff the segment is reachable for the given vehicle. For the binary
// land/ocean grid we have, foot/chocobo/car require land, Garden allows ocean
// too (and Ragnarok callers should skip the BFS entirely — it can fly
// anywhere, so calling this is moot).
static bool IsSegmentTraversable(int row, int col, VehicleType veh)
{
    if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) return false;
    uint8_t cell = s_terrainGrid[row][col];   // 0 = land, 1 = ocean
    if (veh == VEH_GARDEN || veh == VEH_RAGNAROK) return true;   // any segment
    return (cell == 0);                       // land-only for foot/chocobo/car
}

// ============================================================================
// LoadTerrainGrid — reads wmx.obj from world.fs once at module init,
// classifies each of 768 playable segments as LAND or OCEAN by polygon
// terrain types. Restored from v0.11.12 impl.
// ============================================================================
static bool LoadTerrainGrid()
{
    if (s_terrainLoaded) return true;

    // Auto-detect game install path via FF8_EN.exe location.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // ---- Read world.fi (small index file, 12 bytes per entry).
    uint8_t* fiData = nullptr;
    uint32_t fiSize = 0;
    if (!WM_ReadFileToBuffer(fiPath, &fiData, &fiSize)) {
        Log::World("WorldMap: [TERRAIN] Failed to read world.fi at '%s'", fiPath);
        return false;
    }
    if (fiSize < (uint32_t)(WMX_FL_INDEX + 1) * 12) {
        Log::World("WorldMap: [TERRAIN] world.fi too small (%u bytes)", fiSize);
        free(fiData);
        return false;
    }

    // ---- Parse wmx.obj's FI entry (index 9).
    const uint8_t* fiEntry = fiData + WMX_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);

    Log::World("WorldMap: [TERRAIN] wmx.obj FI entry: uncomp=%u offset=%u comp=%u",
               uncompSize, fsOffset, compression);

    // Validate expected size: 835 segments x 36864 = 30,801,540.
    uint32_t expectedSize = (uint32_t)835 * WMX_SEGMENT_SIZE;
    if (uncompSize != expectedSize) {
        Log::World("WorldMap: [TERRAIN] WARNING: wmx.obj size %u != expected %u",
                   uncompSize, expectedSize);
    }

    // ---- Read wmx.obj from world.fs (compressed or raw per FI compression flag).
    uint8_t* wmxData = nullptr;
    if (compression == 0) {
        free(fiData);
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmxData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read wmx.obj from world.fs");
            return false;
        }
    } else {
        // Compressed: derive compressed-size by reading the next FI entry's offset.
        uint32_t compSize = uncompSize; // fallback
        for (int j = WMX_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read compressed wmx.obj");
            return false;
        }
        // FF8 LZSS storage: 4-byte uncompressed-size header precedes the bitstream.
        wmxData = (uint8_t*)malloc(uncompSize);
        if (!wmxData) { free(compData); return false; }
        bool ok = WM_DecompressLZSS(compData + 4, compSize - 4, wmxData, uncompSize);
        free(compData);
        if (!ok) {
            Log::World("WorldMap: [TERRAIN] LZSS decompression failed");
            free(wmxData);
            return false;
        }
    }

    Log::World("WorldMap: [TERRAIN] wmx.obj loaded (%u bytes), classifying %d segments...",
               uncompSize, WMX_PLAYABLE_SEGS);

    // ---- Classify each of the 768 playable segments by walking its proper
    // 68-byte header + 16 block headers + per-block polygon arrays.
    // v0.14.85.1: rewritten from the v0.14.85 flat-stride bug that read
    // 2304 "polygons" per segment and saw 0 oceans because the garbage
    // bytes between block boundaries rarely landed in 32-34.
    memset(s_terrainGrid, 0, sizeof(s_terrainGrid));
    int oceanSegs = 0, landSegs = 0;
    int totalRealPolys = 0, totalOceanPolys = 0;

    for (int seg = 0; seg < WMX_PLAYABLE_SEGS; seg++) {
        int row = seg / WMX_SEG_COLS;
        int col = seg % WMX_SEG_COLS;
        const uint8_t* segData = wmxData + (uint32_t)seg * WMX_SEGMENT_SIZE;

        int segPolyCount = 0, segOceanCount = 0;

        // Walk the 16 block offsets in the segment header. Skip the first 4
        // bytes (group_id), then read each uint32 little-endian offset.
        for (int b = 0; b < WMX_BLOCKS_PER_SEG; b++) {
            uint32_t blockOffset = *(const uint32_t*)(segData + 4 + b * 4);
            if (blockOffset == 0) continue;                       // unused slot
            if (blockOffset + WMX_BLOCK_HDR_SIZE > WMX_SEGMENT_SIZE) continue;  // out of range

            const uint8_t* blockBase = segData + blockOffset;
            uint8_t polyCount = blockBase[0];
            // (vert_count = blockBase[1], norm_count = blockBase[2], pad = blockBase[3]
            //  — not needed for terrain classification but kept for documentation.)

            // Bounds-guard: polygon array must fit within the segment.
            uint32_t polyArrayEnd = blockOffset + WMX_BLOCK_HDR_SIZE +
                                    (uint32_t)polyCount * WMX_POLY_SIZE;
            if (polyArrayEnd > WMX_SEGMENT_SIZE) continue;

            for (int p = 0; p < polyCount; p++) {
                uint8_t terrain = blockBase[WMX_BLOCK_HDR_SIZE + p * WMX_POLY_SIZE
                                            + WMX_TERRAIN_OFFSET];
                if (terrain >= 32 && terrain <= 34) segOceanCount++;
                segPolyCount++;
            }
        }

        totalRealPolys  += segPolyCount;
        totalOceanPolys += segOceanCount;

        // Majority-ocean polygons => OCEAN segment. Empty/degenerate segments
        // (segPolyCount == 0) default to LAND — the playable grid shouldn't
        // contain any, but if one exists, defaulting to land is the
        // conservative choice for accessibility filtering.
        if (segPolyCount > 0 && segOceanCount * 2 > segPolyCount) {
            s_terrainGrid[row][col] = 1;  // OCEAN
            oceanSegs++;
        } else {
            s_terrainGrid[row][col] = 0;  // LAND
            landSegs++;
        }
    }
    free(wmxData);
    s_terrainLoaded = true;

    Log::World("WorldMap: [TERRAIN] Grid built: %d land, %d ocean (of %d). Total real polys=%d (oceans=%d).",
               landSegs, oceanSegs, WMX_PLAYABLE_SEGS, totalRealPolys, totalOceanPolys);

    // Visual grid dump (# = land, ~ = ocean) — valuable for diagnosing
    // coordinate-mapping issues; cheap (24 lines, once per process).
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        char rowStr[WMX_SEG_COLS + 1];
        for (int c = 0; c < WMX_SEG_COLS; c++)
            rowStr[c] = s_terrainGrid[r][c] ? '~' : '#';
        rowStr[WMX_SEG_COLS] = '\0';
        Log::World("WorldMap: [TERRAIN] row%02d: %s", r, rowStr);
    }

    return true;
}

// ============================================================================
// ComputeReachability — BFS flood-fill from player segment, populates
// s_reachable[][] for the given vehicle's traversal rules. Restored from
// v0.11.12 impl. 4-connected with torus wrapping (the world map wraps both
// axes, so segment col 31 borders col 0, and row 23 borders row 0).
// ============================================================================
static void ComputeReachability(int startCol, int startRow, VehicleType veh)
{
    memset(s_reachable, 0, sizeof(s_reachable));

    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) return;

    // Player's current cell is always reachable (they're standing there)
    // even if the cell classifies as ocean — e.g. transition frames where
    // position briefly snaps to a coastline edge classified as ocean.
    s_reachable[startRow][startCol] = 1;

    // BFS queue sized for 768 cells (the entire playable grid).
    static int qCol[WMX_PLAYABLE_SEGS];
    static int qRow[WMX_PLAYABLE_SEGS];
    int qHead = 0, qTail = 0;
    qCol[qTail] = startCol;
    qRow[qTail] = startRow;
    qTail++;

    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };

    while (qHead < qTail) {
        int cc = qCol[qHead];
        int cr = qRow[qHead];
        qHead++;

        for (int d = 0; d < 4; d++) {
            int nc = (cc + dx[d] + WMX_SEG_COLS) % WMX_SEG_COLS;
            int nr = (cr + dy[d] + WMX_SEG_ROWS) % WMX_SEG_ROWS;

            if (!s_reachable[nr][nc] && IsSegmentTraversable(nr, nc, veh)) {
                s_reachable[nr][nc] = 1;
                if (qTail < WMX_PLAYABLE_SEGS) {
                    qCol[qTail] = nc;
                    qRow[qTail] = nr;
                    qTail++;
                }
            }
        }
    }

    int reachCount = 0;
    for (int r = 0; r < WMX_SEG_ROWS; r++)
        for (int c = 0; c < WMX_SEG_COLS; c++)
            if (s_reachable[r][c]) reachCount++;

    Log::World("WorldMap: [BFS] From seg(%d,%d) veh=%d: %d/%d segments reachable",
               startCol, startRow, (int)veh, reachCount, WMX_PLAYABLE_SEGS);
}

// ============================================================================
// Catalog management
// ============================================================================
static void BuildDistanceCatalog()
{
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);

    // v0.14.85 (restored from v0.11.15): the engine reports player position as
    // (0,0) for several frames at world-map entry before populating the real
    // value. Building the catalog from (0,0) lands BFS in an arbitrary cell
    // (segment col 16, row 0) that is almost always wrong, so defer until we
    // see a non-zero position. s_catalogBuilt stays false, Poll() retries us
    // each frame until success.
    if (px == 0 && py == 0) {
        Log::World("WorldMap: [DEFER] Position is (0,0), retrying catalog build next poll");
        return;
    }

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

    // v0.14.85: apply reachability filter. If terrain failed to load (e.g.
    // missing world.fs at runtime), fall back to no-filter mode so cycling
    // still works — graceful degradation rather than empty catalog. If the
    // player is in Ragnarok, also skip the BFS — it can fly anywhere.
    if (!s_terrainLoaded) {
        s_catalogCount = LOCATION_COUNT;
        Log::World("WorldMap: [BFS] Terrain not loaded — catalog unfiltered (%d entries)",
                   s_catalogCount);
    } else {
        VehicleType veh   = GetVehicleType(GetLocomotionMode());
        int         pCol  = WorldXToSegCol(px);
        int         pRow  = WorldYToSegRow(py);
        Log::World("WorldMap: [BFS] Player at (%d,%d) -> seg(%d,%d), vehicle type %d",
                   px, py, pCol, pRow, (int)veh);

        if (veh == VEH_RAGNAROK) {
            // Ragnarok flies over everything — keep all entries, no BFS.
            s_catalogCount = LOCATION_COUNT;
            Log::World("WorldMap: [BFS] Ragnarok mode — catalog unfiltered (%d entries)",
                       s_catalogCount);
        } else {
            ComputeReachability(pCol, pRow, veh);

            // Compact in place: keep only entries on reachable segments.
            int kept = 0;
            for (int i = 0; i < LOCATION_COUNT; i++) {
                int locCol = WorldXToSegCol(s_catalog[i].x);
                int locRow = WorldYToSegRow(s_catalog[i].y);
                if (s_reachable[locRow][locCol]) {
                    if (kept != i) s_catalog[kept] = s_catalog[i];
                    kept++;
                }
            }
            s_catalogCount = kept;
            Log::World("WorldMap: [BFS] Filtered to %d reachable locations (vehicle type %d)",
                       s_catalogCount, (int)veh);
        }
    }

    // Pathological case: nothing reachable. Better than crashing on cycle
    // math — just leave catalog empty and surface in the log; Aaron will
    // notice immediately and we can investigate.
    if (s_catalogCount == 0) {
        Log::World("WorldMap: [BFS] WARNING — no reachable locations from current position");
    }

    s_catalogBuilt = true;
    s_catalogIndex = 0;
    if (s_catalogCount > 0) {
        Log::World("WorldMap: Catalog built (%d entries), nearest: %s",
                   s_catalogCount, s_catalog[0].name);
    }
}

// ============================================================================
// Navigation announcements
// ============================================================================
static void AnnounceLocation(int index)
{
    if (index < 0 || index >= s_catalogCount || !s_catalogBuilt) return;
    
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
    if (!s_catalogBuilt || s_catalogIndex >= s_catalogCount) return;
    
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
// Vehicle state tracking (v0.14.85.3)
// ============================================================================
// Names match the canonical research-doc enum. Modes outside the canonical
// set never reach this function in normal operation — CheckVehicleChange's
// whitelist filters them — but the default returns 'Unknown vehicle' for
// safety. Cars are identified as a single 'Car' name regardless of the
// specific mode 32-40 value.
static const char* GetVehicleName(uint8_t mode)
{
    if (mode == 0 || mode == 6) return "On foot";        // Squall / Selphie
    if (mode == 3)               return "Ship";          // BAT-validated v0.14.83
    if (mode == 31)              return "Chocobo";
    if (mode >= 32 && mode <= 40) return "Car";
    if (mode == 48)              return "Garden";
    if (mode == 50)              return "Ragnarok";
    return "Unknown vehicle";
}

static void CheckVehicleChange()
{
    uint8_t vehicle = GetLocomotionMode();

    // v0.14.85.3 whitelist: only canonical locomotion values per the research
    // doc are eligible to trigger vehicle-change announcements or catalog
    // rebuilds. Transient bytes (animation phase counters, field-transition
    // state values like the mode 4 we observed at the Fire Cavern boundary in
    // the v0.14.85.2 BAT) are silently ignored — s_lastVehicle is not updated,
    // nothing is announced, no rebuild fires. Real vehicle changes still
    // register because the byte passes through a canonical value at the
    // mount/dismount moment, and that is the transition we capture.
    if (!IsCanonicalLocomotion(vehicle)) return;

    if (vehicle != s_lastVehicle) {
        if (s_lastVehicle != -1) {  // skip initial announcement
            const char* newVehicle = GetVehicleName(vehicle);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.", newVehicle);
            ScreenReader::Speak(buf, true);  // interrupt previous speech
            Log::World("WorldMap: Vehicle change: %s (mode %u)", newVehicle, vehicle);

            // v0.14.85.2 + .3: rebuild the catalog when the vehicle's BFS rule
            // CLASS changes (not just the VehicleType). Three rule classes:
            //   0 = land-only  (foot, chocobo, car)
            //   1 = ocean-allowed (Ship, Garden)
            //   2 = no-filter  (Ragnarok)
            // Foot ↔ car ↔ chocobo transitions stay within class 0, so they
            // don't trigger gratuitous rebuilds (the BFS result would be
            // identical). Class crossings (foot ↔ Ragnarok, car ↔ Ship)
            // do rebuild because the reachability set genuinely differs.
            VehicleType oldType = (s_lastVehicle >= 0)
                                  ? GetVehicleType((uint8_t)s_lastVehicle)
                                  : VEH_ON_FOOT;
            VehicleType newType = GetVehicleType(vehicle);
            int oldClass = GetBfsRuleClass(oldType);
            int newClass = GetBfsRuleClass(newType);
            if (oldClass != newClass && s_onWorldMap) {
                s_catalogBuilt = false;
                Log::World("WorldMap: [BFS] Vehicle rule class changed (%d -> %d), forcing catalog rebuild",
                           oldClass, newClass);
            }
        }
        s_lastVehicle = vehicle;
    }
}

// ============================================================================
// Keyboard input polling (v0.14.83)
// ============================================================================
// Replaces the orphaned v0.11.x HandleKeyPress dispatch path. HandleKeyPress
// was defined in this file but never declared in world_map.h and never
// invoked from anywhere — leftover collateral from the v0.14.24 build damage
// / v0.14.31 partial recovery (Update() and Shutdown() were restored, the
// keyboard dispatch was not). Result: world map nav keys had been silently
// dead since the recovery.
//
// New design mirrors FieldNavigation::HandleKeys: PollKeys() is called from
// inside Poll() and uses GetAsyncKeyState with edge-detected statics.
// Implicitly gated on s_onWorldMap because Poll() early-returns when off
// world map. No collision with FieldNavigation: it gates its identical key
// set on FF8Addresses::IsOnField() which is mutually exclusive with the
// world-map scene flag (0x0203ED2C == 0).
static void PollKeys()
{
    static bool s_minusWas = false;
    static bool s_plusWas  = false;
    static bool s_bkspWas  = false;
    static bool s_bslashWas = false;

    bool minus  = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus   = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    bool bksp   = (GetAsyncKeyState(VK_BACK)      & 0x8000) != 0;
    bool bslash = (GetAsyncKeyState(VK_OEM_5)     & 0x8000) != 0;  // '\' key

    if (minus && !s_minusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex - 1 + s_catalogCount) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] minus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (plus && !s_plusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex + 1) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] plus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (bksp && !s_bkspWas) {
        AnnounceBearing();
        Log::World("WorldMap: [KEY] backspace bearing");
    }
    if (bslash && !s_bslashWas) {
        // v0.14.84: Placeholder. World map auto-drive (and BFS terrain
        // filtering for the catalog) were lost in the v0.14.24 build damage
        // and have never been restored or pushed to GitHub. Research is
        // present in `Plan & Research Documents/`; full restoration is
        // Priority 1 in NEXT_SESSION_PROMPT. This handler exists so the
        // keypress is not a silent failure.
        ScreenReader::Speak("World map auto-drive is not yet implemented in this build.", true);
        Log::World("WorldMap: [KEY] backslash placeholder (auto-drive not yet implemented)");
    }

    s_minusWas  = minus;
    s_plusWas   = plus;
    s_bkspWas   = bksp;
    s_bslashWas = bslash;
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

    // v0.14.83: Poll nav keys (-, =, Backspace) at the end of the world-map
    // poll cycle. Catalog is guaranteed built by this point; s_onWorldMap is
    // true (we early-returned above otherwise). See PollKeys comment for
    // ownership notes.
    PollKeys();
}

// ============================================================================
// Module initialization
// ============================================================================
void Initialize()
{
    s_onWorldMap = false;
    s_catalogBuilt = false;
    s_catalogCount = 0;
    s_catalogIndex = 0;
    s_lastVehicle = -1;
    s_lastMovementTick = 0;

    // v0.14.85: load terrain grid once at module init. Idempotent
    // (s_terrainLoaded short-circuits subsequent calls). If the load fails
    // — e.g. world.fs missing or corrupt — we log and continue; catalog
    // building will fall back to no-filter mode rather than blocking.
    if (LoadTerrainGrid()) {
        Log::World("WorldMap: [INIT] Terrain grid loaded successfully");
    } else {
        Log::World("WorldMap: [INIT] Terrain grid load failed — catalog will be unfiltered");
    }

    Log::World("WorldMap: Module initialized (v%s)", FF8OPC_VERSION);
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
    s_catalogCount = 0;
    s_catalogIndex = 0;
    s_lastVehicle = -1;
    s_lastMovementTick = 0;
    Log::World("WorldMap: Shutdown complete.");
}

} // namespace WorldMap