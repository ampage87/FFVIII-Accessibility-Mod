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
#include "world_map.h"

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
    int32_t z;
};

static const LocationEntry LOCATION_CATALOG[] = {
    // 26 main locations (Ragnarok autopilot set)
    { "Balamb Garden",              24576,  -29406,  -658 },
    { "Balamb",                     13249,  -26779,  -304 },
    { "Fire Cavern",                36864,  -28672,  -400 },  // From wmx.obj terrain-29 analysis, seg(20,20)
    { "Dollet",                    -15639,  -39437,  -172 },
    { "Timber",                    -22564,   -4867,  -700 },
    { "Galbadia Garden",           -37471,  -25062,  -573 },
    { "Deling City",               -61806,  -28649,  -892 },
    { "Tomb of the Unknown King",  -42471,  -36562,  -228 },
    { "D-District Prison",         -55306,   -4841,  -199 },
    { "Galbadia Missile Base",     -71695,  -15591,  -364 },
    { "Fisherman's Horizon",        48811,   -1653,  -430 },
    { "Trabia Garden",              48893,  -57979,  -800 },
    { "Edea's House",              -23150,   62853,  -648 },
    { "White SeeD Ship",             4887,   51285,  -480 },
    { "Great Salt Lake",            49888,   -2683,  -333 },
    { "Esthar",                     57011,   -2295,  -297 },
    { "Lunatic Pandora Lab",        79521,   -9135,  -570 },
    { "Lunar Gate",                 88021,    7865,  -328 },
    { "Sorceress Memorial",         81521,   11865,  -460 },
    { "Shumi Village",              10362,  -76967,  -845 },
    { "Winhill",                   -50285,    6320,  -385 },
    { "Centra Ruins",                6887,   55285,  -582 },
    { "Deep Sea Research Center", -119138,   86000,   324 },
    { "Cactuar Island",             54806,   62040,  -618 },
    { "Tears Point",                83021,   31865,  -347 },
    { "Island Closest To Hell",   -105137,   -3802,  -483 },
    { "Island Closest To Heaven",  102251,  -53082,  -467 },
    // 7 Chocobo Forests
    { "Chocobo Forest 1",           11332,  -63659,  -632 },
    { "Chocobo Forest 2",           10927,  -81010,  -885 },
    { "Chocobo Forest 3",           51893,   -3959,  -795 },
    { "Chocobo Forest 4",           97253,  -48250,  -831 },
    { "Chocobo Forest 5",           17383,   22013,  -436 },
    { "Chocobo Forest 6",           44504,   76259,  -222 },
    { "Chocobo Forest 7",          -20953,   68906,  -435 },
    // 4 Alien Encounters
    { "Alien Encounter 1",          79823,  -61212,  -459 },
    { "Alien Encounter 2",          40495,   54649,  -494 },
    { "Alien Encounter 3",         -12952,  -10202,    -6 },
    { "Alien Encounter 4",         -48806,    5808,  -476 },
};
static const int CATALOG_SIZE = sizeof(LOCATION_CATALOG) / sizeof(LOCATION_CATALOG[0]);

// ============================================================================
// Sorted catalog (built on world map entry, frozen during stay)
// ============================================================================
struct SortedLocation {
    int catalogIdx;     // index into LOCATION_CATALOG
    float distance;     // distance at time of sorting
};

static SortedLocation s_sorted[CATALOG_SIZE] = {};
static int s_sortedCount = 0;
static int s_selectedIdx = 0;       // cursor into s_sorted[]
static bool s_catalogBuilt = false; // true once sorted for this world map visit

// ============================================================================
// State tracking
// ============================================================================
static bool s_initialized = false;
static bool s_onWorldMap = false;        // currently on world map
static uint8_t s_lastVehicle = 0xFF;     // for change detection
static bool s_minusWas = false;
static bool s_plusWas = false;
static bool s_bkspWas = false;
static bool s_driveWas = false;

// ============================================================================
// Auto-drive state
// ============================================================================
static bool s_driveActive = false;       // currently auto-driving
static int  s_driveTargetCatIdx = -1;    // index into LOCATION_CATALOG
static DWORD s_driveStartTime = 0;
static DWORD s_driveLastAnnounce = 0;    // time of last distance announcement
static float s_driveLastDist = 1e30f;    // for approach detection
static int32_t s_driveStuckX = 0;        // position at last stuck check
static int32_t s_driveStuckY = 0;
static DWORD s_driveStuckCheckTime = 0;
static int   s_driveStuckCount = 0;      // consecutive stuck checks

// Final approach sweep: when stuck near destination, try different headings
static int   s_sweepPhase = 0;           // 0=forward, 1-6=sweep directions
static DWORD s_sweepTurnEnd = 0;         // tick count when current turn phase ends
static bool  s_sweepTurning = false;     // currently in a turn sub-phase

// AD persistence across random encounters
static int   s_driveResumeTargetIdx = -1; // catalog index to resume after battle (-1=none)
static bool  s_driveResumeOnEntry = false;

// v0.11.13: Car detection and forest-stuck recovery
// The car (locomotion 32-40) can traverse all land EXCEPT forests.
// When stuck against a forest, we reverse out and try a different heading.
static bool  s_driveIsCar = false;        // true when driving a car

// Car recovery state machine:
//   NONE=0: normal driving (steering toward target)
//   REVERSE=1: backing up from forest obstacle (DOWN key)
//   TURN=2: turning to new heading (LEFT or RIGHT key)
//   FORWARD=3: trying forward on new heading, checking if we clear the forest
static int   s_carRecovPhase = 0;         // 0=none, 1=reverse, 2=turn, 3=forward-test
static DWORD s_carRecovEnd = 0;           // tick when current recovery sub-phase ends
static int   s_carRecovAttempt = 0;       // how many reverse-turn-forward cycles we've tried
static bool  s_carRecovTurnRight = true;  // alternates between attempts
static const int CAR_RECOV_MAX_ATTEMPTS = 8;  // give up after this many cycles
static const DWORD CAR_REVERSE_MS = 1500;     // how long to reverse
static const DWORD CAR_TURN_BASE_MS = 800;    // base turn duration
static const DWORD CAR_TURN_STEP_MS = 200;    // additional turn per attempt
static const DWORD CAR_FORWARD_TEST_MS = 3000; // forward test before re-checking stuck

// v0.11.08: Fake gamepad removed — world map uses keyboard injection (keybd_event)
// because worldmap_input_update_sub_559240 has its own input path that doesn't
// read from pDinputGamepadStatePtr.

// Auto-drive tuning constants
static const float DRIVE_APPROACH_DIST = 5000.0f; // "Approaching" announcement
static const DWORD DRIVE_ANNOUNCE_INTERVAL = 5000; // ms between distance announcements
static const DWORD DRIVE_STUCK_CHECK_INTERVAL = 3000; // ms between stuck checks
static const float DRIVE_STUCK_THRESHOLD = 50.0f;  // min movement per stuck interval
static const int   DRIVE_STUCK_MAX = 3;            // stuck checks before giving up

// ============================================================================
// Terrain grid — built from wmx.obj on first world map entry
// ============================================================================
// 32×24 segment grid. Each cell = LAND (0) or OCEAN (1).
// Used for BFS reachability filtering of location catalog.
// wmx.obj: 835 segments × 36864 bytes, first 768 are playable (32×24 grid).
// Each segment: 2304 polygons × 16 bytes. Terrain type at polygon byte 0x0D.
// Ocean = types 32-34. Everything else = land.
static uint8_t s_terrainGrid[24][32] = {};  // [row][col], 0=LAND, 1=OCEAN
static bool s_terrainLoaded = false;
static uint8_t s_reachable[24][32] = {};    // BFS result, 1=reachable from player

// wmx.obj constants
static const int WMX_FL_INDEX        = 9;      // index in world.fl
static const int WMX_SEGMENT_SIZE    = 36864;   // bytes per segment (0x9000)
static const int WMX_PLAYABLE_SEGS   = 768;     // 32×24 playable grid
static const int WMX_POLY_SIZE       = 16;      // bytes per polygon
static const int WMX_TERRAIN_OFFSET  = 0x0D;    // terrain type byte within polygon
static const int WMX_POLYS_PER_SEG   = WMX_SEGMENT_SIZE / WMX_POLY_SIZE; // 2304
static const int WMX_SEG_COLS        = 32;
static const int WMX_SEG_ROWS        = 24;

// ============================================================================
// File I/O helpers for terrain loading (same pattern as field_archive.cpp)
// ============================================================================
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

static bool WM_ReadFileChunk(const char* path, uint32_t offset, uint32_t size,
                              uint8_t** outData)
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
// LZSS decompression (same as field_archive.cpp)
// ============================================================================
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
// Coordinate conversion: game world coords → segment grid
// ============================================================================
// World map torus: 262144 × 196608, divided into 32×24 segments of 8192 each.
static int WorldXToSegCol(int32_t x)
{
    // v0.11.16: World map X origin is offset from segment grid origin.
    // Formula from wmx.obj analysis: game_X = seg_col*8192 + 4096 - 131072
    // Inverse: seg_col = floor((game_X + 131072) / 8192) mod 32
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
// Load terrain grid from wmx.obj inside world.fs
// ============================================================================
static bool LoadTerrainGrid()
{
    if (s_terrainLoaded) return true;

    // Auto-detect game path (same pattern as FieldArchive)
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // Read world.fi (144 bytes = 12 entries × 12 bytes)
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

    // Parse wmx.obj's FI entry (index 9)
    const uint8_t* fiEntry = fiData + WMX_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);
    free(fiData);
    fiData = nullptr;

    Log::World("WorldMap: [TERRAIN] wmx.obj FI: uncomp=%u offset=%u comp=%u",
               uncompSize, fsOffset, compression);

    // Validate: 835 segments × 36864 = 30,801,540 bytes expected
    uint32_t expectedSize = (uint32_t)835 * WMX_SEGMENT_SIZE;
    if (uncompSize != expectedSize) {
        Log::World("WorldMap: [TERRAIN] WARNING: wmx.obj size %u != expected %u",
                   uncompSize, expectedSize);
    }

    // Read wmx.obj data from world.fs
    uint8_t* wmxData = nullptr;
    if (compression == 0) {
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmxData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read wmx.obj from world.fs");
            return false;
        }
    } else {
        // Compressed — need to find compressed size from next FI entry
        uint8_t* fiData2 = nullptr;
        uint32_t fiSize2 = 0;
        if (!WM_ReadFileToBuffer(fiPath, &fiData2, &fiSize2)) {
            Log::World("WorldMap: [TERRAIN] Failed to re-read world.fi for comp size");
            return false;
        }
        uint32_t compSize = uncompSize; // fallback
        for (int j = WMX_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize2; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData2 + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData2);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read compressed wmx.obj");
            return false;
        }
        // FF8 LZSS: skip 4-byte uncompressed-size header
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

    // DIAGNOSTIC: Scan different byte ranges to find where polygon terrain data lives.
    // Previous run showed bytes 0-11716 are header+vertex data (identical across segments).
    // Polygon data likely starts ~12000+ bytes into the segment.
    for (int diagSeg : {0, 200, 400}) {
        if ((uint32_t)(diagSeg + 1) * WMX_SEGMENT_SIZE > uncompSize) continue;
        const uint8_t* ds = wmxData + (uint32_t)diagSeg * WMX_SEGMENT_SIZE;
        // First byte of segment (differs between segments: 03, FF, etc.)
        Log::World("WorldMap: [TERRAIN-DIAG] seg%d firstByte=0x%02X", diagSeg, ds[0]);
        // Scan byte 0x0D at stride 16, starting from offset 12000 (skip header+vertices)
        int valHigh[256] = {};
        int highStart = 12000;
        int highCount = (WMX_SEGMENT_SIZE - highStart) / 16;
        for (int pp = 0; pp < highCount; pp++)
            valHigh[ds[highStart + pp * 16 + 0x0D]]++;
        char hex5[512] = {};
        int p5 = 0;
        for (int v = 0; v < 256; v++) {
            if (valHigh[v] > 0)
                p5 += snprintf(hex5 + p5, sizeof(hex5) - p5, "%d:%d ", v, valHigh[v]);
        }
        Log::World("WorldMap: [TERRAIN-DIAG] seg%d byte0x0D@stride16_from12000: %s", diagSeg, hex5);
        // Also try scanning ALL bytes for values 32-34 to see total ocean byte count
        int ocean32 = 0, ocean33 = 0, ocean34 = 0;
        for (uint32_t b = 0; b < WMX_SEGMENT_SIZE; b++) {
            if (ds[b] == 32) ocean32++;
            if (ds[b] == 33) ocean33++;
            if (ds[b] == 34) ocean34++;
        }
        Log::World("WorldMap: [TERRAIN-DIAG] seg%d raw_byte_counts: val32=%d val33=%d val34=%d",
                   diagSeg, ocean32, ocean33, ocean34);
        // Dump bytes at offsets 12000-12063 to see what polygon data looks like
        char hexPoly[200] = {};
        int pp2 = 0;
        for (int b = 0; b < 64; b++)
            pp2 += snprintf(hexPoly + pp2, sizeof(hexPoly) - pp2, "%02X ", ds[12000 + b]);
        Log::World("WorldMap: [TERRAIN-DIAG] seg%d bytes[12000..12063]: %s", diagSeg, hexPoly);
    }

    // Classify each of the 768 playable segments as LAND or OCEAN
    // v0.11.14: Block-aware parsing per wmx.obj deep research findings.
    // Each segment has a 68-byte header: 4-byte group ID + 16 uint32 block offsets.
    // Each block starts with a 4-byte header: poly_count, vert_count, norm_count, pad.
    // Ground type is at polygon offset 0x0D within each 16-byte polygon record.
    // Ocean ground types: 32 (shallow), 33 (light), 34 (dark).
    memset(s_terrainGrid, 0, sizeof(s_terrainGrid));
    int oceanSegs = 0, landSegs = 0;

    for (int seg = 0; seg < WMX_PLAYABLE_SEGS; seg++) {
        int row = seg / WMX_SEG_COLS;
        int col = seg % WMX_SEG_COLS;
        const uint8_t* segData = wmxData + (uint32_t)seg * WMX_SEGMENT_SIZE;

        // Read 16 block offsets from segment header (bytes 4..67)
        int totalPolys = 0;
        int oceanPolys = 0;
        for (int blk = 0; blk < 16; blk++) {
            uint32_t blockOfs = *(const uint32_t*)(segData + 4 + blk * 4);
            if (blockOfs == 0 || blockOfs >= WMX_SEGMENT_SIZE - 4) continue;

            // Block header: byte0=poly_count, byte1=vert_count, byte2=norm_count
            const uint8_t* blockPtr = segData + blockOfs;
            uint8_t polyCount = blockPtr[0];
            if (polyCount == 0) continue;

            // Sanity: polys must fit within segment
            uint32_t polyEnd = blockOfs + 4 + (uint32_t)polyCount * 16;
            if (polyEnd > WMX_SEGMENT_SIZE) continue;

            // Scan polygons for ground type at offset 0x0D
            for (int p = 0; p < polyCount; p++) {
                uint8_t groundType = blockPtr[4 + p * 16 + 0x0D];
                totalPolys++;
                if (groundType >= 32 && groundType <= 34) oceanPolys++;
            }
        }

        // Majority-ocean = OCEAN segment
        if (totalPolys > 0 && oceanPolys > totalPolys / 2) {
            s_terrainGrid[row][col] = 1; // OCEAN
            oceanSegs++;
        } else {
            s_terrainGrid[row][col] = 0; // LAND
            landSegs++;
        }
    }

    free(wmxData);
    s_terrainLoaded = true;

    Log::World("WorldMap: [TERRAIN] Grid built: %d land, %d ocean (of %d)",
               landSegs, oceanSegs, WMX_PLAYABLE_SEGS);

    // Log the grid visually (# = land, ~ = ocean)
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
// BFS flood-fill from player segment to find reachable land cells
// ============================================================================
static void ComputeReachability(int startCol, int startRow)
{
    memset(s_reachable, 0, sizeof(s_reachable));

    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) return;

    // Player's cell is always reachable (even if ocean — they're standing there)
    s_reachable[startRow][startCol] = 1;

    // BFS queue (max 768 cells)
    static int qCol[768], qRow[768];
    int qHead = 0, qTail = 0;
    qCol[qTail] = startCol;
    qRow[qTail] = startRow;
    qTail++;

    // 4-connected neighbors with torus wrapping
    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };

    while (qHead < qTail) {
        int cc = qCol[qHead];
        int cr = qRow[qHead];
        qHead++;

        for (int d = 0; d < 4; d++) {
            int nc = (cc + dx[d] + WMX_SEG_COLS) % WMX_SEG_COLS;
            int nr = (cr + dy[d] + WMX_SEG_ROWS) % WMX_SEG_ROWS;

            if (!s_reachable[nr][nc] && s_terrainGrid[nr][nc] == 0) {
                s_reachable[nr][nc] = 1;
                qCol[qTail] = nc;
                qRow[qTail] = nr;
                qTail++;
            }
        }
    }

    int reachCount = 0;
    for (int r = 0; r < WMX_SEG_ROWS; r++)
        for (int c = 0; c < WMX_SEG_COLS; c++)
            if (s_reachable[r][c]) reachCount++;

    Log::World("WorldMap: [BFS] From seg(%d,%d): %d/%d segments reachable",
               startCol, startRow, reachCount, WMX_PLAYABLE_SEGS);
}

// ============================================================================
// Compass directions
// ============================================================================
static const char* COMPASS_DIRS[] = { "north", "northeast", "east", "southeast",
                                       "south", "southwest", "west", "northwest" };

// Convert angle (0-4095, 0=N, CW) to compass index 0-7
static int AngleToCompassIdx(int angle) {
    int idx = ((angle + 256) % 4096) / 512;
    return (idx >= 0 && idx < 8) ? idx : 0;
}

// ============================================================================
// Torus-aware distance calculation
// ============================================================================
// The world wraps on both axes. Shortest distance accounts for wrapping.
static double TorusDist(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = (double)(x2 - x1);
    double dy = (double)(y2 - y1);

    // Wrap: if |dx| > half width, the shorter path goes the other way
    if (dx >  WM_WIDTH / 2.0) dx -= WM_WIDTH;
    if (dx < -WM_WIDTH / 2.0) dx += WM_WIDTH;
    if (dy >  WM_HEIGHT / 2.0) dy -= WM_HEIGHT;
    if (dy < -WM_HEIGHT / 2.0) dy += WM_HEIGHT;

    return sqrt(dx * dx + dy * dy);
}

// Compute bearing from (x1,y1) to (x2,y2) in 0-4095 range (0=N, CW)
// accounting for torus wrapping
static int TorusBearing(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = (double)(x2 - x1);
    double dy = (double)(y2 - y1);

    if (dx >  WM_WIDTH / 2.0) dx -= WM_WIDTH;
    if (dx < -WM_WIDTH / 2.0) dx += WM_WIDTH;
    if (dy >  WM_HEIGHT / 2.0) dy -= WM_HEIGHT;
    if (dy < -WM_HEIGHT / 2.0) dy += WM_HEIGHT;

    // atan2(dx, -dy) gives angle from north (negative Y = north on world map)
    // Result in radians, convert to 0-4095
    double rad = atan2(dx, -dy);
    int angle = (int)(rad * 4096.0 / (2.0 * 3.14159265358979)) % 4096;
    if (angle < 0) angle += 4096;
    return angle;
}

// ============================================================================
// Build sorted catalog (called on world map entry)
// ============================================================================
static void BuildSortedCatalog()
{
    int32_t px = 0, py = 0;
    __try {
        px = *(int32_t*)WM_POS_X;
        py = *(int32_t*)WM_POS_Y;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    // v0.11.15: Guard against building catalog before engine populates position.
    // At world map entry, position reads (0,0) for several frames.
    // Building BFS from seg(0,0) (ocean) filters ALL locations as unreachable.
    if (px == 0 && py == 0) {
        Log::World("WorldMap: [CATALOG] Position is (0,0) — deferring catalog build");
        return;  // s_catalogBuilt stays false, will retry next frame
    }

    // BFS reachability from player's current segment (on-foot)
    int playerCol = WorldXToSegCol(px);
    int playerRow = WorldYToSegRow(py);
    bool hasTerrainFilter = s_terrainLoaded;
    if (hasTerrainFilter) {
        ComputeReachability(playerCol, playerRow);
    }

    s_sortedCount = 0;
    int skippedCount = 0;
    for (int i = 0; i < CATALOG_SIZE; i++) {
        // Filter by terrain reachability if grid is loaded
        if (hasTerrainFilter) {
            int locCol = WorldXToSegCol(LOCATION_CATALOG[i].x);
            int locRow = WorldYToSegRow(LOCATION_CATALOG[i].y);
            if (!s_reachable[locRow][locCol]) {
                skippedCount++;
                continue;  // Unreachable — skip
            }
        }
        s_sorted[s_sortedCount].catalogIdx = i;
        s_sorted[s_sortedCount].distance = (float)TorusDist(px, py,
            LOCATION_CATALOG[i].x, LOCATION_CATALOG[i].y);
        s_sortedCount++;
    }

    // Sort by distance (ascending)
    for (int a = 0; a < s_sortedCount - 1; a++) {
        for (int b = a + 1; b < s_sortedCount; b++) {
            if (s_sorted[b].distance < s_sorted[a].distance) {
                SortedLocation tmp = s_sorted[a];
                s_sorted[a] = s_sorted[b];
                s_sorted[b] = tmp;
            }
        }
    }

    s_selectedIdx = 0;
    s_catalogBuilt = true;

    Log::World("WorldMap: Catalog built (%d locations, %d filtered, player at %d,%d seg=%d,%d)",
               s_sortedCount, skippedCount, px, py, playerCol, playerRow);
    for (int i = 0; i < s_sortedCount && i < 5; i++) {
        const LocationEntry& loc = LOCATION_CATALOG[s_sorted[i].catalogIdx];
        Log::World("WorldMap:   [%d] %s — %.0f units", i, loc.name, s_sorted[i].distance);
    }
}

// ============================================================================
// Announce current selection (used by cycling and backspace)
// ============================================================================
static void AnnounceSelection(bool withBearing)
{
    if (s_sortedCount == 0 || s_selectedIdx < 0 || s_selectedIdx >= s_sortedCount) return;

    const SortedLocation& sel = s_sorted[s_selectedIdx];
    const LocationEntry& loc = LOCATION_CATALOG[sel.catalogIdx];

    int32_t px = 0, py = 0;
    uint16_t heading = 0;
    __try {
        px = *(int32_t*)WM_POS_X;
        py = *(int32_t*)WM_POS_Y;
        heading = *(uint16_t*)WM_HEADING;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    // Compute live distance and bearing
    double dist = TorusDist(px, py, loc.x, loc.y);
    int bearing = TorusBearing(px, py, loc.x, loc.y);
    int compassIdx = AngleToCompassIdx(bearing);

    char buf[256];
    if (withBearing) {
        // Backspace mode: full navigation info with relative direction
        // Compute relative bearing (bearing - heading)
        int relBearing = (bearing - heading + 4096) % 4096;
        int relCompass = AngleToCompassIdx(relBearing);

        // Also compute "turn" instruction
        const char* turnDir = "";
        if (relBearing > 256 && relBearing < 2048)
            turnDir = "Turn right. ";
        else if (relBearing >= 2048 && relBearing < 3840)
            turnDir = "Turn left. ";
        // else roughly ahead or behind

        snprintf(buf, sizeof(buf), "%s. %.0f units %s. %s",
                 loc.name, dist, COMPASS_DIRS[compassIdx], turnDir);
    } else {
        // Cycling mode: name + compass direction + distance
        snprintf(buf, sizeof(buf), "%s. %s, %.0f units.",
                 loc.name, COMPASS_DIRS[compassIdx], dist);
    }

    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [NAV] %s (idx=%d/%d dist=%.0f bearing=%d heading=%d)",
               buf, s_selectedIdx, s_sortedCount, dist, bearing, (int)heading);
}

// ============================================================================
// Auto-drive input injection — keyboard-based (world map uses different
// input path than fields, so fake gamepad doesn't work here)
// ============================================================================
static bool s_keyUpHeld = false;
static bool s_keyDownHeld = false;
static bool s_keyLeftHeld = false;
static bool s_keyRightHeld = false;
static bool s_keyGasHeld = false;     // A key = Cross = Car Forward (user-confirmed)
static bool s_keyReverseHeld = false;  // W key = Triangle = Car Reverse (user-confirmed)

static void PressKey(BYTE vk, WORD scan, bool extended = true)
{
    DWORD flags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    keybd_event(vk, scan, flags, 0);
}

static void ReleaseKey(BYTE vk, WORD scan, bool extended = true)
{
    DWORD flags = (extended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;
    keybd_event(vk, scan, flags, 0);
}

static void ReleaseAllDriveKeys()
{
    if (s_keyUpHeld)      { ReleaseKey(VK_UP, 0x48);    s_keyUpHeld = false; }
    if (s_keyDownHeld)    { ReleaseKey(VK_DOWN, 0x50);  s_keyDownHeld = false; }
    if (s_keyLeftHeld)    { ReleaseKey(VK_LEFT, 0x4B);  s_keyLeftHeld = false; }
    if (s_keyRightHeld)   { ReleaseKey(VK_RIGHT, 0x4D); s_keyRightHeld = false; }
    // Car gas/reverse: A and W are NOT extended keys (no KEYEVENTF_EXTENDEDKEY)
    if (s_keyGasHeld)     { ReleaseKey('A', 0x1E, false);  s_keyGasHeld = false; }
    if (s_keyReverseHeld) { ReleaseKey('W', 0x11, false);  s_keyReverseHeld = false; }
}

// Set which keys should be held this frame
// For car mode: gas (A key) and reverse (W key) are injected alongside direction keys
static void SetDriveKeys(bool up, bool down, bool left, bool right,
                         bool gas = false, bool reverse = false)
{
    // Press/release changes only — direction keys (extended)
    if (up && !s_keyUpHeld)       { PressKey(VK_UP, 0x48);    s_keyUpHeld = true; }
    if (!up && s_keyUpHeld)       { ReleaseKey(VK_UP, 0x48);  s_keyUpHeld = false; }
    if (down && !s_keyDownHeld)   { PressKey(VK_DOWN, 0x50);  s_keyDownHeld = true; }
    if (!down && s_keyDownHeld)   { ReleaseKey(VK_DOWN, 0x50); s_keyDownHeld = false; }
    if (left && !s_keyLeftHeld)   { PressKey(VK_LEFT, 0x4B);  s_keyLeftHeld = true; }
    if (!left && s_keyLeftHeld)   { ReleaseKey(VK_LEFT, 0x4B); s_keyLeftHeld = false; }
    if (right && !s_keyRightHeld) { PressKey(VK_RIGHT, 0x4D); s_keyRightHeld = true; }
    if (!right && s_keyRightHeld) { ReleaseKey(VK_RIGHT, 0x4D); s_keyRightHeld = false; }
    // Car gas/reverse — NOT extended keys (user-confirmed: A=forward, W=reverse)
    if (gas && !s_keyGasHeld)         { PressKey('A', 0x1E, false);  s_keyGasHeld = true; }
    if (!gas && s_keyGasHeld)         { ReleaseKey('A', 0x1E, false); s_keyGasHeld = false; }
    if (reverse && !s_keyReverseHeld) { PressKey('W', 0x11, false);  s_keyReverseHeld = true; }
    if (!reverse && s_keyReverseHeld) { ReleaseKey('W', 0x11, false); s_keyReverseHeld = false; }
}

// ============================================================================
// Vehicle detection helper
// ============================================================================
// The locomotion byte cycles through animation sub-states while moving.
// Car values are 32-40. We sample it and check if ANY recent value was in car range.
static bool IsInCar()
{
    uint8_t loco = 0;
    __try { loco = *(uint8_t*)WM_LOCOMOTION; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return (loco >= 32 && loco <= 40);
}

// ============================================================================
// Auto-drive control
// ============================================================================
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    s_driveActive = false;
    ReleaseAllDriveKeys();
    if (reason) {
        ScreenReader::Speak(reason, true);
    }
    Log::World("WorldMap: [DRIVE] Stopped: %s", reason ? reason : "(silent)");
}

static void StartAutoDrive()
{
    if (s_sortedCount == 0 || s_selectedIdx < 0 || s_selectedIdx >= s_sortedCount) return;

    int catIdx = s_sorted[s_selectedIdx].catalogIdx;
    const LocationEntry& loc = LOCATION_CATALOG[catIdx];

    int32_t px = 0, py = 0;
    __try {
        px = *(int32_t*)WM_POS_X;
        py = *(int32_t*)WM_POS_Y;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    float dist = (float)TorusDist(px, py, loc.x, loc.y);

    // Keyboard injection doesn't need fake gamepad setup
    s_driveActive = true;
    s_driveTargetCatIdx = catIdx;
    s_driveStartTime = GetTickCount();
    s_driveLastAnnounce = GetTickCount();
    s_driveLastDist = dist;
    s_driveStuckX = px;
    s_driveStuckY = py;
    s_driveStuckCheckTime = GetTickCount();
    s_driveStuckCount = 0;
    s_sweepPhase = 0;
    s_sweepTurning = false;
    s_driveResumeOnEntry = false;  // clear resume since we're starting fresh

    // v0.11.13: Car detection and recovery reset
    s_driveIsCar = IsInCar();
    s_carRecovPhase = 0;
    s_carRecovAttempt = 0;
    s_carRecovTurnRight = true;

    char buf[128];
    snprintf(buf, sizeof(buf), "Driving to %s. %.0f units.%s",
             loc.name, dist, s_driveIsCar ? " Car mode." : "");
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [DRIVE] Started toward %s (dist=%.0f car=%d)", loc.name, dist, (int)s_driveIsCar);
}

// Per-frame steering + announcements
static void UpdateAutoDrive()
{
    if (!s_driveActive) return;
    if (s_driveTargetCatIdx < 0 || s_driveTargetCatIdx >= CATALOG_SIZE) {
        StopAutoDrive("Cancelled.");
        return;
    }

    const LocationEntry& loc = LOCATION_CATALOG[s_driveTargetCatIdx];

    int32_t px = 0, py = 0;
    uint16_t heading = 0;
    __try {
        px = *(int32_t*)WM_POS_X;
        py = *(int32_t*)WM_POS_Y;
        heading = *(uint16_t*)WM_HEADING;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        StopAutoDrive("Position read failed.");
        return;
    }

    float dist = (float)TorusDist(px, py, loc.x, loc.y);

    // v0.11.14: Continuous car detection — locomotion byte cycles through
    // animation states, so we might miss car range (32-40) at drive start.
    // Once we see ANY car value, latch s_driveIsCar=true for this drive.
    if (!s_driveIsCar && IsInCar()) {
        s_driveIsCar = true;
        Log::World("WorldMap: [DRIVE] Car detected mid-drive (loco now in 32-40)");
    }

    // --- Arrival is detected by world map exit (mode change), not distance ---
    // The game auto-enters locations when you walk into the trigger zone.
    // We keep driving until the game transitions us to a field.

    // --- Approach announcement (one-shot) ---
    if (dist < DRIVE_APPROACH_DIST && s_driveLastDist >= DRIVE_APPROACH_DIST) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Approaching %s. %.0f units.", loc.name, dist);
        ScreenReader::Speak(buf, true);
        s_driveLastAnnounce = GetTickCount();
    }
    s_driveLastDist = dist;

    // --- Periodic distance announcement ---
    DWORD now = GetTickCount();
    if (now - s_driveLastAnnounce >= DRIVE_ANNOUNCE_INTERVAL) {
        s_driveLastAnnounce = now;
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f units.", dist);
        ScreenReader::Speak(buf, true);
    }

    // --- Stuck detection ---
    if (now - s_driveStuckCheckTime >= DRIVE_STUCK_CHECK_INTERVAL) {
        float moved = (float)TorusDist(s_driveStuckX, s_driveStuckY, px, py);
        if (moved < DRIVE_STUCK_THRESHOLD) {
            s_driveStuckCount++;
            Log::World("WorldMap: [DRIVE] Stuck check %d/%d (moved %.0f, dist=%.0f, car=%d, recov=%d, sweep=%d)",
                       s_driveStuckCount, DRIVE_STUCK_MAX, moved, dist,
                       (int)s_driveIsCar, s_carRecovPhase, s_sweepPhase);
            if (s_driveStuckCount >= DRIVE_STUCK_MAX) {
                if (s_driveIsCar) {
                    // --- CAR RECOVERY: reverse out of forest, try new heading ---
                    if (s_carRecovAttempt >= CAR_RECOV_MAX_ATTEMPTS) {
                        StopAutoDrive("Stuck. Cannot reach destination by car.");
                        return;
                    }
                    s_carRecovPhase = 1;  // start reversing
                    s_carRecovEnd = now + CAR_REVERSE_MS;
                    s_driveStuckCount = 0;
                    s_carRecovAttempt++;
                    s_carRecovTurnRight = !s_carRecovTurnRight; // alternate direction
                    Log::World("WorldMap: [DRIVE] Car recovery attempt %d/%d: reverse %ums then turn %s",
                               s_carRecovAttempt, CAR_RECOV_MAX_ATTEMPTS,
                               CAR_REVERSE_MS, s_carRecovTurnRight ? "right" : "left");
                    ScreenReader::Speak("Rerouting.", true);
                } else {
                    // --- WALKING RECOVERY: sweep search (existing behavior) ---
                    if (dist < 1500.0f && s_sweepPhase < 6) {
                        s_sweepPhase++;
                        s_sweepTurning = true;
                        int turnMs = 600 + s_sweepPhase * 200;
                        s_sweepTurnEnd = now + turnMs;
                        s_driveStuckCount = 0;
                        Log::World("WorldMap: [DRIVE] Sweep phase %d: turn %s for %dms",
                                   s_sweepPhase, (s_sweepPhase % 2 == 1) ? "right" : "left", turnMs);
                        ScreenReader::Speak("Searching.", true);
                    } else {
                        StopAutoDrive("Stuck. Cannot reach destination.");
                        return;
                    }
                }
            }
        } else {
            s_driveStuckCount = 0;
            // Clear car recovery if we're moving again
            if (s_carRecovPhase == 3) {
                s_carRecovPhase = 0;
                Log::World("WorldMap: [DRIVE] Car recovery cleared (moving, attempt %d)",
                           s_carRecovAttempt);
            }
        }
        s_driveStuckX = px;
        s_driveStuckY = py;
        s_driveStuckCheckTime = now;
    }

    // --- Steering (keyboard injection) ---
    int targetBearing = TorusBearing(px, py, loc.x, loc.y);
    int relBearing = (targetBearing - (int)heading + 4096) % 4096;

    bool wantUp = false, wantDown = false, wantLeft = false, wantRight = false;

    // v0.11.13: Car recovery state machine takes priority over normal steering
    if (s_carRecovPhase == 1) {
        // REVERSE phase: back up from forest
        if (now < s_carRecovEnd) {
            wantDown = true;
        } else {
            // Transition to TURN phase
            s_carRecovPhase = 2;
            DWORD turnMs = CAR_TURN_BASE_MS + s_carRecovAttempt * CAR_TURN_STEP_MS;
            s_carRecovEnd = now + turnMs;
            Log::World("WorldMap: [DRIVE] Car recovery -> turn %s for %ums",
                       s_carRecovTurnRight ? "right" : "left", turnMs);
        }
    }
    else if (s_carRecovPhase == 2) {
        // TURN phase: rotate to new heading
        if (now < s_carRecovEnd) {
            if (s_carRecovTurnRight) wantRight = true; else wantLeft = true;
            // Car needs gas held to turn (it steers while moving forward)
            wantUp = true;
        } else {
            // Transition to FORWARD-TEST phase
            s_carRecovPhase = 3;
            s_carRecovEnd = now + CAR_FORWARD_TEST_MS;
            s_driveStuckCount = 0;
            s_driveStuckCheckTime = now;
            s_driveStuckX = px;
            s_driveStuckY = py;
            Log::World("WorldMap: [DRIVE] Car recovery -> forward test for %ums", CAR_FORWARD_TEST_MS);
        }
    }
    else if (s_carRecovPhase == 3) {
        // FORWARD-TEST phase: drive forward, stuck detection will handle re-entry
        // Use normal steering toward target (fall through below)
        s_carRecovPhase = 0; // exit recovery, normal steering resumes
    }

    // Normal steering (when not in a recovery sub-phase that set keys above)
    if (!wantDown && s_carRecovPhase == 0) {
        if (s_sweepTurning) {
            // Sweep turn sub-phase (walking only): turn while moving forward.
            // v0.11.14: Added wantUp during turn so car can steer (car needs
            // gas to turn — can't rotate in place like walking).
            if (now < s_sweepTurnEnd) {
                if (s_sweepPhase % 2 == 1) wantRight = true; else wantLeft = true;
                wantUp = true;  // drive forward while turning (needed for car steering)
            } else {
                // Turn done — resume walking forward
                s_sweepTurning = false;
                s_driveStuckCount = 0;
                s_driveStuckCheckTime = now;
                s_driveStuckX = px;
                s_driveStuckY = py;
                wantUp = true;
            }
        } else if (dist < 1000.0f) {
            // Final approach: just drive forward
            wantUp = true;
        } else if (relBearing < 200 || relBearing > 3896) {
            wantUp = true;
        } else if (relBearing < 1800) {
            wantRight = true;
            if (relBearing < 512) wantUp = true;
        } else {
            wantLeft = true;
            if (relBearing > 3584) wantUp = true;
        }
    }

    // v0.11.14: Always inject gas (A key = Cross = Confirm) when going forward.
    // On foot: A is the pause button — but during auto-drive, the game may handle
    //   it differently. User confirmed A=forward for the car.
    // In car: A is the accelerator — required to make the car move.
    // Reverse (W key = Triangle) is only injected during stuck recovery.
    bool wantGas = wantUp;
    bool wantReverse = wantDown;  // only true during car recovery phase
    SetDriveKeys(wantUp, wantDown, wantLeft, wantRight, wantGas, wantReverse);
}

// ============================================================================
// Key handling
// ============================================================================
static bool s_f12Was = false;  // F12 diagnostic key state

static void HandleKeys()
{
    bool minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus  = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    bool bksp  = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    bool drive = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;  // backslash
    bool f12   = (GetAsyncKeyState(VK_F12)  & 0x8000) != 0;

    // F12: Vehicle state diagnostic — dump keyboard state + vehicle bytes
    if (f12 && !s_f12Was) {
        Log::World("WorldMap: [F12-DIAG] === Vehicle & Key diagnostic ===");
        // Dump all currently pressed keys
        int pressed = 0;
        for (int vk = 0; vk < 256; vk++) {
            if (GetAsyncKeyState(vk) & 0x8000) {
                UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
                Log::World("WorldMap: [F12-DIAG] KEY VK=0x%02X scan=0x%02X", vk, scan);
                pressed++;
            }
        }
        // Dump locomotion byte and surrounding 32 bytes for vehicle detection research
        __try {
            uint8_t loco = *(uint8_t*)WM_LOCOMOTION;
            // Scan wider range around locomotion for vehicle state candidates
            char hex1[100] = {}, hex2[100] = {};
            for (int b = 0; b < 16; b++)
                snprintf(hex1 + b*3, 4, "%02X ", *(uint8_t*)(WM_LOCOMOTION - 16 + b));
            for (int b = 0; b < 16; b++)
                snprintf(hex2 + b*3, 4, "%02X ", *(uint8_t*)(WM_LOCOMOTION + b));
            Log::World("WorldMap: [F12-DIAG] loco=%u [-16]: %s", (unsigned)loco, hex1);
            Log::World("WorldMap: [F12-DIAG] loco=%u [+0]:  %s", (unsigned)loco, hex2);
            // Also read heading and position for context
            int32_t px = *(int32_t*)WM_POS_X;
            int32_t py = *(int32_t*)WM_POS_Y;
            uint16_t hd = *(uint16_t*)WM_HEADING;
            Log::World("WorldMap: [F12-DIAG] pos=(%d,%d) heading=%u", px, py, (unsigned)hd);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::World("WorldMap: [F12-DIAG] Exception reading addresses");
        }
        Log::World("WorldMap: [F12-DIAG] %d keys pressed, driveActive=%d, isCar=%d",
                   pressed, (int)s_driveActive, (int)s_driveIsCar);
        ScreenReader::Speak("Vehicle diagnostic logged.", true);
    }
    s_f12Was = f12;

    // Minus: previous location in list
    if (minus && !s_minusWas) {
        if (s_driveActive) StopAutoDrive("Cancelled.");
        if (!s_catalogBuilt) BuildSortedCatalog();
        if (s_sortedCount > 0) {
            s_selectedIdx = (s_selectedIdx - 1 + s_sortedCount) % s_sortedCount;
            AnnounceSelection(false);
        }
    }

    // Plus/Equals: next location in list
    if (plus && !s_plusWas) {
        if (s_driveActive) StopAutoDrive("Cancelled.");
        if (!s_catalogBuilt) BuildSortedCatalog();
        if (s_sortedCount > 0) {
            s_selectedIdx = (s_selectedIdx + 1) % s_sortedCount;
            AnnounceSelection(false);
        }
    }

    // Backspace: announce bearing + distance to selected location
    if (bksp && !s_bkspWas) {
        if (!s_catalogBuilt) BuildSortedCatalog();
        if (s_sortedCount > 0) {
            AnnounceSelection(true);
        }
    }

    // Backslash: toggle auto-drive to selected location
    if (drive && !s_driveWas) {
        if (s_driveActive) {
            StopAutoDrive("Cancelled.");
        } else {
            StartAutoDrive();
        }
    }

    s_minusWas = minus;
    s_plusWas = plus;
    s_bkspWas = bksp;
    s_driveWas = drive;
}

// ============================================================================
// Vehicle change detection
// ============================================================================
static void PollVehicleChange()
{
    uint8_t vehicle = 0;
    __try { vehicle = *(uint8_t*)WM_LOCOMOTION; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (vehicle != s_lastVehicle && s_lastVehicle != 0xFF) {
        // v0.11.04: Log only, don't speak. The locomotion byte cycles rapidly
        // through animation/movement states while walking (0->3->7->10->14->0).
        // Need locomotion.md from ff8-speedruns to identify actual vehicle changes
        // vs walking animation phases. Will add TTS once enum is decoded.
        Log::World("WorldMap: [VEHICLE] locomotion %u -> %u", (unsigned)s_lastVehicle, (unsigned)vehicle);
    }
    s_lastVehicle = vehicle;
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized = true;
    s_onWorldMap = false;
    s_catalogBuilt = false;
    s_lastVehicle = 0xFF;
    s_selectedIdx = 0;
    s_sortedCount = 0;

    // Load terrain grid from wmx.obj (deferred to first world map entry
    // to avoid blocking game startup with a 30MB file read)
    // LoadTerrainGrid() is called from Update() on first world map entry.

    Log::World("WorldMap: Initialized v0.11.16 — %d locations, terrain=DEFERRED, deferred catalog build.",
               CATALOG_SIZE);
}

void Update()
{
    if (!s_initialized) return;
    if (!FF8Addresses::pGameMode) return;

    uint16_t mode = *FF8Addresses::pGameMode;
    bool isWorldMap = (mode == FF8Addresses::MODE_WORLDMAP);

    // World map entry detection
    if (isWorldMap && !s_onWorldMap) {
        s_onWorldMap = true;
        s_catalogBuilt = false;  // Force rebuild on next key press
        s_lastVehicle = 0xFF;    // Reset vehicle tracking
        Log::World("WorldMap: === ENTERED WORLD MAP ===");

        // Deferred terrain loading: parse wmx.obj on first world map entry
        if (!s_terrainLoaded) {
            LoadTerrainGrid();
        }

        // v0.11.15: Don't call BuildSortedCatalog() here — position is (0,0)
        // at entry. Deferred build happens in the polling section below.
    }
    // World map exit detection
    else if (!isWorldMap && s_onWorldMap) {
        s_onWorldMap = false;
        s_catalogBuilt = false;

        // Log player's world map coordinates at exit for coordinate discovery
        int32_t exitX = 0, exitY = 0, exitZ = 0;
        __try {
            exitX = *(int32_t*)WM_POS_X;
            exitY = *(int32_t*)WM_POS_Y;
            exitZ = *(int32_t*)WM_POS_Z;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        Log::World("WorldMap: [EXIT-COORD] Position at exit: X=%d Y=%d Z=%d (mode=%u)",
                   exitX, exitY, exitZ, (unsigned)mode);
        if (s_driveActive) {
            uint16_t newMode = mode;
            if (newMode == FF8Addresses::MODE_FIELD || newMode == FF8Addresses::MODE_AFTER_BATTLE) {
                // Entered a location — arrival!
                const char* destName = (s_driveTargetCatIdx >= 0 && s_driveTargetCatIdx < CATALOG_SIZE)
                    ? LOCATION_CATALOG[s_driveTargetCatIdx].name : "destination";
                char buf[128];
                snprintf(buf, sizeof(buf), "Arrived at %s.", destName);
                StopAutoDrive(buf);
                s_driveResumeOnEntry = false;  // arrived, don't resume
                s_driveResumeTargetIdx = -1;
            } else {
                // Battle or other interruption — save target for resume
                s_driveResumeTargetIdx = s_driveTargetCatIdx;
                s_driveResumeOnEntry = true;
                StopAutoDrive(nullptr);  // silent stop
                Log::World("WorldMap: [DRIVE] Battle interrupted, will resume target %d on return",
                           s_driveResumeTargetIdx);
            }
        }
        Log::World("WorldMap: === LEFT WORLD MAP (new mode=%u) ===", (unsigned)mode);
    }

    if (!s_onWorldMap) return;

    // v0.11.15: Deferred catalog build — retry each frame until position is valid.
    // At world map entry, the engine takes several frames to populate the player
    // position. BuildSortedCatalog() will bail if position is still (0,0).
    if (!s_catalogBuilt) {
        BuildSortedCatalog();

        // If catalog was just built successfully, check for pending auto-drive resume
        if (s_catalogBuilt && s_driveResumeOnEntry &&
            s_driveResumeTargetIdx >= 0 && s_driveResumeTargetIdx < CATALOG_SIZE) {
            for (int i = 0; i < s_sortedCount; i++) {
                if (s_sorted[i].catalogIdx == s_driveResumeTargetIdx) {
                    s_selectedIdx = i;
                    break;
                }
            }
            s_driveResumeOnEntry = false;
            s_driveResumeTargetIdx = -1;
            StartAutoDrive();
            Log::Write("WorldMap: [DRIVE] Resumed auto-drive after battle");
        }
    }

    // Handle navigation keys
    HandleKeys();

    // Auto-drive steering (runs every frame when active)
    UpdateAutoDrive();

    // Poll for vehicle changes
    PollVehicleChange();
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_driveActive) StopAutoDrive(nullptr);
    ReleaseAllDriveKeys();  // safety
    s_initialized = false;
    s_onWorldMap = false;
    Log::Write("WorldMap: Shutdown.");
}

}  // namespace WorldMap
