// world_map_segments.inl - Coordinate / archive / segment-math layer
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// Sits between state.inl (data) and the higher-level modules (catalog,
// planner, drive). Holds:
//   - Memory readers: GetWorldMapPosition[_Active], GetWorldMapHeading,
//     GetLocomotionMode, GetCurrentStoryFlag, IsOnWorldMap.
//   - Pure math: CalculateWrappedDistance, TorusBearing.
//   - Vehicle classifier: GetVehicleType, GetBfsRuleClass,
//     IsCanonicalLocomotion, IsSegmentTraversable.
//   - Coordinate conversion: WorldXToSegCol, WorldYToSegRow,
//     SegmentCenterToWorld.
//   - Archive I/O: WM_ReadFileToBuffer, WM_ReadFileChunk, WM_DecompressLZSS,
//     LoadTerrainGrid (wmx.obj), DumpTriggerSection / LoadTriggerZones
//     (wmsetus.obj, also populates s_segmentRegionMap[]).
//
// Note: IsLocationFootFriendly lives in world_map_planner.inl since it
// reads s_triggerPrograms[]. SegmentCenterToWorld is pulled here despite
// being adjacent to the planner in the original file -- it's pure inverse-
// of-WorldXToSegCol/Row math, used by both planner and drive.

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

// Forward declaration: GetVehicleType is defined later in this file (line ~1667)
// alongside the rest of the vehicle classification helpers; we need it here
// for GetWorldMapPosition_Active's vehicle dispatch.
static VehicleType GetVehicleType(uint8_t mode);

// ============================================================================
// v0.14.103: GetWorldMapPosition_Active
// ============================================================================
// Returns the player's active world-map position based on current vehicle.
// Foot DWORDs at WM_POS_X/Y/Z freeze when the player mounts a vehicle (the
// engine's per-frame integrator switches from updating foot DWORDs to
// updating the vehicle's position array in the savemap WORLDMAP struct).
// AD's distance/bearing/stuck-detection must read the active vehicle's
// position to track real motion; reading foot DWORDs while in a car gives
// the v0.14.101 BAT failure mode ("position frozen at (16031,-26948)").
//
// The savemap WORLDMAP arrays store positions as 6 uint16 values:
//   [0]=X, [1]=Z, [2]=Y, [3]=unk, [4]=unk, [5]=rotation
// per the v0.14.103 deep research. We multiply by WM_SAVEMAP_TO_DWORD_SCALE
// (4096) to align with the 20.12 fixed-point foot DWORD coordinate space.
// Sign-extending the uint16 to int32 first preserves negative coordinates
// (the world map's southern/western hemispheres have negative coords in
// the foot DWORD frame; whether savemap uint16 stores them as signed int16
// or as biased unsigned will be verified by the [VEH-VERIFY] block).
//
// Vehicle dispatch:
//   VEH_CAR      → read car_pos at WM_CAR_POS_ADDR
//   VEH_GARDEN   → read bgu_pos at WM_BGU_POS_ADDR  (mobile Balamb Garden)
//   VEH_RAGNAROK → read ragnarok_pos at WM_RAGNAROK_POS_ADDR
//   else (foot/Chocobo/Ship) → fall through to foot DWORDs (Chocobo and
//                              Ship piggyback on the foot character)
static void GetWorldMapPosition_Active(int32_t* x, int32_t* y, int32_t* z)
{
    // Default to foot DWORDs.
    GetWorldMapPosition(x, y, z);

    // Determine current vehicle from the debounced state. Use s_lastVehicle
    // (the committed locomotion byte) rather than a fresh GetLocomotionMode
    // read, so transient byte cycling during AD doesn't flip us between
    // sources mid-drive. GetVehicleType maps the byte to VehicleType.
    if (s_lastVehicle < 0) return;   // not yet sampled → keep foot DWORDs
    VehicleType veh = GetVehicleType((uint8_t)s_lastVehicle);

    uintptr_t addr = 0;
    const char* tag = nullptr;
    if (veh == VEH_CAR)            { addr = WM_CAR_POS_ADDR;      tag = "car_pos"; }
    else if (veh == VEH_GARDEN)    { addr = WM_BGU_POS_ADDR;      tag = "bgu_pos"; }
    else if (veh == VEH_RAGNAROK)  { addr = WM_RAGNAROK_POS_ADDR; tag = "ragnarok_pos"; }
    else                            return;   // foot/Chocobo/Ship use foot DWORDs

    __try {
        const int16_t* arr = (const int16_t*)addr;
        int32_t vx = (int32_t)arr[0] * WM_SAVEMAP_TO_DWORD_SCALE;
        int32_t vz = (int32_t)arr[1] * WM_SAVEMAP_TO_DWORD_SCALE;   // savemap [1] = Z (altitude)
        int32_t vy = (int32_t)arr[2] * WM_SAVEMAP_TO_DWORD_SCALE;   // savemap [2] = Y (north-south)
        // v0.16.0.1: only overwrite foot DWORDs when the vehicle pos looks
        // valid. (0,0) is a sentinel for "vehicle not owned / savemap not
        // maintained" -- the BAT failure mode where the WM-ENTRY-DEBOUNCE
        // committed locomotion=37 (Car) for a player who never owned a car,
        // and car_pos reads zero. In that situation the foot DWORDs are the
        // more reliable source. Foot DWORD layout uses X/Y for the 2D plane
        // the bearing math runs on, with Z as altitude.
        if (vx != 0 || vy != 0) {
            *x = vx;
            *y = vy;
            *z = vz;
        } else {
            // v0.16.0.3: log only on s_lastVehicle transition. The guard's
            // job (preserve foot DWORDs when vehicle savemap reads (0,0))
            // is silent and self-correcting; the log line exists only as
            // a forensic trail showing which vehicle byte triggered the
            // fallback. Without rate-limiting, the condition fired once
            // per world-map poll while s_lastVehicle stayed latched to a
            // non-foot mode, producing ~1800 identical lines in a 7-minute
            // v0.16.0.2 BAT. Transition-only logging gives one line per
            // distinct vehicle value reaching the fallback, which is what
            // forensic analysis actually needs.
            static int s_fbLastLoggedVehicle = -1;
            if (s_lastVehicle != s_fbLastLoggedVehicle) {
                Log::World("WorldMap: [VEH-POS-FALLBACK] %s reads (0,0) for s_lastVehicle=%d (%s) -- keeping foot DWORDs (%d,%d)",
                           tag, s_lastVehicle,
                           (veh == VEH_CAR) ? "VEH_CAR" :
                           (veh == VEH_GARDEN) ? "VEH_GARDEN" :
                           (veh == VEH_RAGNAROK) ? "VEH_RAGNAROK" : "?",
                           *x, *y);
                s_fbLastLoggedVehicle = s_lastVehicle;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Read fault — keep the foot DWORD fallback already in *x/*y/*z.
    }
}

// v0.14.94: read the savemap story-flag word at WM_STORY_FLAG. Used by the
// AD path planner's clause-evaluation logic to filter which s_triggerPrograms[]
// entries are currently satisfiable (each clause carries an optional
// [story_gte, story_lt) window; the program is gated only when the live
// story value falls inside that window). Returning 0 on access fault is safe
// — a story value of 0 satisfies any 'no lower bound' gate (story_gte=0)
// and fails any 'must be >=N' gate, which is the correct early-game behavior.
static uint16_t GetCurrentStoryFlag()
{
    uint16_t story = 0;
    __try {
        story = *(uint16_t*)WM_STORY_FLAG;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        story = 0;
    }
    return story;
}

static bool IsOnWorldMap()
{
    // v0.17.5.4: require BOTH the scene flag AND the game mode to agree.
    // The scene flag at WM_SCENE_FLAG can read 0 at boot (before any scene
    // has actually loaded -- it's just zero-initialized memory at that
    // point), which previously made this function return true at startup.
    // The downstream effect: world_map.cpp's Poll() declared "Entered world
    // map" before the player was on any scene, set s_onWorldMap=true, and
    // never reset because the exit detector only fires on a true->false
    // transition. PollKeys() then ran every tick on every screen, including
    // fields, and stole the `\` key with its own "No locations available"
    // announcement on top of FieldNavigation's "Driving."
    //
    // The pre-existing diagnostic warning in Poll() ("Warning - On world
    // map but game mode is N (expected 2)") was tracking this exact bug
    // but only logging it. v0.17.5.4 actually uses gameMode as part of the
    // decision.
    if (!FF8Addresses::pGameMode) return false;  // address not resolved yet
    uint32_t mode = 0;
    __try {
        mode = *FF8Addresses::pGameMode;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (mode != FF8Addresses::MODE_WORLDMAP) return false;

    // Game mode says world map; also confirm the scene flag agrees.
    uint16_t scene = 1;
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
// Segment-to-world coordinate conversion (v0.14.94)
// ============================================================================
// Inverse of WorldXToSegCol/Row. Used by planner and drive when steering
// toward a segment-center waypoint rather than a literal world coordinate.
// Pulled into segments.inl with its sibling forward-coord conversions.
// Convert a segment's center to world coordinates. Inverse of
// WorldXToSegCol/Row:  world_x = col*8192 + 4096 - 131072,
//                      world_y = row*8192 + 4096.
// World map wraps both axes; for steering targets we emit the canonical
// (un-wrapped) coordinate — TorusBearing handles wrap on its own.
static void SegmentCenterToWorld(int col, int row, int32_t* outX, int32_t* outY)
{
    *outX = (int32_t)(col * 8192 + 4096) - 131072;
    *outY = (int32_t)(row * 8192 + 4096);
}

// ============================================================================
// Auto-drive helpers (v0.14.86)
// ============================================================================
// Bearing in native FF8 heading units (0-4095, 0=North, CW). Wrap-aware on
// the world torus. Mirrors the math in AnnounceBearing but returns the raw
// angle for the steering decision; AnnounceBearing converts to compass
// directions for speech.
static int TorusBearing(int32_t fromX, int32_t fromY, int32_t toX, int32_t toY)
{
    int32_t dx = toX - fromX;
    int32_t dy = toY - fromY;
    if (abs(dx) > (int32_t)WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else        dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > (int32_t)WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else        dy += (int32_t)WM_HEIGHT;
    }
    // -dy because FF8 Y axis increases downward; atan2(dx, -dy) gives
    // angle from +Y (North) clockwise, matching the heading convention.
    double radians = atan2((double)dx, -(double)dy);
    if (radians < 0) radians += 2.0 * 3.14159265358979;
    int bearing = (int)(radians / (2.0 * 3.14159265358979) * 4096.0);
    return bearing & 0xFFF;  // wrap to 0-4095
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

// True iff the segment is reachable for the given vehicle. v0.14.103: 3-state
// terrain classifier (LAND/FOREST/OCEAN). Foot/Chocobo can cross forest;
// cars cannot (engine collision rejects forest entry from car locomotion
// values 32-40). Garden/Ragnarok can cross any terrain.
static bool IsSegmentTraversable(int row, int col, VehicleType veh)
{
    if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) return false;
    uint8_t cell = s_terrainGrid[row][col];   // SegTerrainClass: 0=land, 1=forest, 2=ocean
    if (veh == VEH_GARDEN || veh == VEH_RAGNAROK) return true;          // any segment
    if (cell == SEG_OCEAN) return false;                                 // ocean blocks all non-Garden/Ragnarok
    if (cell == SEG_FOREST && veh == VEH_CAR) return false;              // v0.14.103: cars can't enter forest
    return true;                                                          // foot/chocobo on land or forest; car on land
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
    // v0.14.103: extended to 3-state classification (land/forest/ocean) by
    // counting forest polygons (terrain values 0-5) alongside ocean polygons
    // (32-34). Majority-of-polygons rule with priority: ocean > forest > land.
    memset(s_terrainGrid, 0, sizeof(s_terrainGrid));
    int oceanSegs = 0, forestSegs = 0, landSegs = 0;
    int totalRealPolys = 0, totalOceanPolys = 0, totalForestPolys = 0;

    for (int seg = 0; seg < WMX_PLAYABLE_SEGS; seg++) {
        int row = seg / WMX_SEG_COLS;
        int col = seg % WMX_SEG_COLS;
        const uint8_t* segData = wmxData + (uint32_t)seg * WMX_SEGMENT_SIZE;

        int segPolyCount = 0, segOceanCount = 0, segForestCount = 0;

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
                if (terrain >= 32 && terrain <= 34) {
                    segOceanCount++;
                } else if (terrain <= 5) {
                    // Forest variants: 0=Galbadia, 1=Trabia, 2=Esthar,
                    // 3=Centra, 4=Balamb, 5=Esthar.
                    segForestCount++;
                }
                segPolyCount++;
            }
        }

        totalRealPolys   += segPolyCount;
        totalOceanPolys  += segOceanCount;
        totalForestPolys += segForestCount;

        // Majority classifier with priority: ocean > forest > land. A segment
        // is OCEAN if more than half its polygons are ocean. Otherwise, FOREST
        // if more than half are forest. Otherwise LAND. Empty/degenerate
        // segments default to LAND — the conservative choice for accessibility.
        if (segPolyCount > 0 && segOceanCount * 2 > segPolyCount) {
            s_terrainGrid[row][col] = SEG_OCEAN;
            oceanSegs++;
        } else if (segPolyCount > 0 && segForestCount * 2 > segPolyCount) {
            s_terrainGrid[row][col] = SEG_FOREST;
            forestSegs++;
        } else {
            s_terrainGrid[row][col] = SEG_LAND;
            landSegs++;
        }
    }
    free(wmxData);
    s_terrainLoaded = true;

    Log::World("WorldMap: [TERRAIN] Grid built: %d land, %d forest, %d ocean (of %d). Total polys=%d (ocean=%d, forest=%d).",
               landSegs, forestSegs, oceanSegs, WMX_PLAYABLE_SEGS,
               totalRealPolys, totalOceanPolys, totalForestPolys);

    // Visual grid dump (# = land, F = forest, ~ = ocean) — valuable for
    // diagnosing coordinate-mapping issues; cheap (24 lines, once per process).
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        char rowStr[WMX_SEG_COLS + 1];
        for (int c = 0; c < WMX_SEG_COLS; c++) {
            uint8_t cls = s_terrainGrid[r][c];
            rowStr[c] = (cls == SEG_OCEAN) ? '~' : (cls == SEG_FOREST) ? 'F' : '#';
        }
        rowStr[WMX_SEG_COLS] = '\0';
        Log::World("WorldMap: [TERRAIN] row%02d: %s", r, rowStr);
    }

    return true;
}

// ============================================================================
// LoadTriggerZones — hex-dumps a configurable list of wmsetus.obj sections
// (controlled by WMSETUS_DUMP_SECTIONS_1IDX) to ff8_world.log for trigger-
// system reverse engineering. Mirrors LoadTerrainGrid's archive-reader
// pattern (world.fi entry lookup + LZSS decompress) but reads world.fi entry
// 10 (wmsetus.obj) instead of entry 9 (wmx.obj). No game-side state is
// captured — the function is purely diagnostic; its output drives the
// decoder design in subsequent builds. v0.14.91 dumped Sections 17 and 18
// (deep research's leading hypothesis, since disproved). v0.14.92 dumps
// Sections 7 and 8 (the disassembly-confirmed field-entry bytecode plus
// its small adjacent section).
// ============================================================================

// Hex-dump a byte range to ff8_world.log under [TRIGGER-DUMP] with section
// label and a printable-ASCII gutter. Caller passes already-validated bounds.
static void DumpTriggerSection(const char* sectLabel, const uint8_t* base, uint32_t bytes)
{
    const uint32_t cap = (bytes < WMSETUS_DUMP_CAP_BYTES) ? bytes : WMSETUS_DUMP_CAP_BYTES;
    Log::World("WorldMap: [TRIGGER-DUMP] %s begin (size=%u, dumping %u)", sectLabel, bytes, cap);
    for (uint32_t off = 0; off < cap; off += 16) {
        char hexpart[16 * 3 + 1] = {};
        char asciipart[17]       = {};
        uint32_t row = (cap - off >= 16) ? 16 : (cap - off);
        for (uint32_t i = 0; i < row; i++) {
            uint8_t b = base[off + i];
            snprintf(hexpart + i * 3, sizeof(hexpart) - i * 3, "%02X ", b);
            asciipart[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        // Pad short final row's hex column to keep the gutter aligned.
        for (uint32_t i = row; i < 16; i++) {
            snprintf(hexpart + i * 3, sizeof(hexpart) - i * 3, "   ");
        }
        asciipart[row] = '\0';
        Log::World("WorldMap: [TRIGGER-DUMP] %s +%04X: %s %s", sectLabel, off, hexpart, asciipart);
    }
    if (cap < bytes) {
        Log::World("WorldMap: [TRIGGER-DUMP] %s truncated at %u (full size %u)", sectLabel, cap, bytes);
    }
    Log::World("WorldMap: [TRIGGER-DUMP] %s end", sectLabel);
}

static bool LoadTriggerZones()
{
    // ---- Build paths exactly the way LoadTerrainGrid does. Auto-detect
    // game install via FF8_EN.exe location.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // ---- Read world.fi (12 bytes per entry: uncompSize, fsOffset, compression).
    uint8_t* fiData = nullptr;
    uint32_t fiSize = 0;
    if (!WM_ReadFileToBuffer(fiPath, &fiData, &fiSize)) {
        Log::World("WorldMap: [TRIGGER-DUMP] Failed to read world.fi at '%s'", fiPath);
        return false;
    }
    if (fiSize < (uint32_t)(WMSETUS_FL_INDEX + 1) * 12) {
        Log::World("WorldMap: [TRIGGER-DUMP] world.fi too small (%u bytes) for entry %d",
                   fiSize, WMSETUS_FL_INDEX);
        free(fiData);
        return false;
    }

    const uint8_t* fiEntry = fiData + WMSETUS_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);
    Log::World("WorldMap: [TRIGGER-DUMP] wmsetus.obj FI entry %d: uncomp=%u offset=%u comp=%u",
               WMSETUS_FL_INDEX, uncompSize, fsOffset, compression);

    // ---- Read wmsetus.obj from world.fs (compressed or raw per FI compression flag).
    uint8_t* wmsData = nullptr;
    if (compression == 0) {
        free(fiData);
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmsData)) {
            Log::World("WorldMap: [TRIGGER-DUMP] Failed to read wmsetus.obj raw");
            return false;
        }
    } else {
        // Compressed: derive compressed-size by reading the next FI entry's offset.
        uint32_t compSize = uncompSize;   // fallback if no later entry exists
        for (int j = WMSETUS_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TRIGGER-DUMP] Failed to read compressed wmsetus.obj");
            return false;
        }
        // FF8 LZSS storage: 4-byte uncompressed-size header precedes the bitstream.
        wmsData = (uint8_t*)malloc(uncompSize);
        if (!wmsData) { free(compData); return false; }
        bool ok = WM_DecompressLZSS(compData + 4, compSize - 4, wmsData, uncompSize);
        free(compData);
        if (!ok) {
            Log::World("WorldMap: [TRIGGER-DUMP] LZSS decompression failed");
            free(wmsData);
            return false;
        }
    }

    // ---- Validate file size against the 48-entry header.
    if (uncompSize < (uint32_t)WMSETUS_HEADER_BYTES) {
        Log::World("WorldMap: [TRIGGER-DUMP] wmsetus.obj too small (%u bytes) for 48-entry header",
                   uncompSize);
        free(wmsData);
        return false;
    }

    // ---- Read the 48-entry section-offset table and log every offset for
    // diagnostic context. v0.14.93's decoder will reuse this header parse;
    // the full dump here makes the entire wmsetus layout visible at a glance
    // so we can sanity-check that section sizes match FF Inside wiki
    // annotations (region map at 2 = 772b, encounters at 1+4 = 392b+1348b,
    // textures at 38 = 257672b, etc.).
    uint32_t hdr[WMSETUS_SECTION_COUNT];
    for (int i = 0; i < WMSETUS_SECTION_COUNT; i++) {
        hdr[i] = *(const uint32_t*)(wmsData + i * 4);
    }
    for (int i = 0; i < WMSETUS_SECTION_COUNT; i++) {
        // Compute size as next-offset minus this-offset; for the last section,
        // size is uncompSize - hdr[last].
        uint32_t sectStart = hdr[i];
        uint32_t sectEnd   = (i + 1 < WMSETUS_SECTION_COUNT) ? hdr[i + 1] : uncompSize;
        uint32_t sectSize  = (sectEnd >= sectStart) ? (sectEnd - sectStart) : 0;
        Log::World("WorldMap: [TRIGGER-DUMP] section %02d (1-indexed): offset=0x%08X size=%u",
                   i + 1, sectStart, sectSize);
    }

    // ---- v0.14.94: extract Section 2 (the 32x24 segment-region byte map)
    // into s_segmentRegionMap[][] for AD path planning. Same buffer / same
    // header parse as the dump above — no duplicate I/O. Section 2 is at
    // 1-indexed position 2 (array index 1). Layout: 768 bytes of region
    // IDs (byte at offset row*32+col is the region ID for segment (col, row))
    // followed by a 4-byte '00 00 00 00' trailer. We bounds-check and skip
    // gracefully on size mismatch; AD's catalog-center fallback path still
    // works without the region map (just less precise arrival detection).
    {
        const int s2idx = 1;   // Section 2 = 1-indexed 2 = array index 1
        if (s2idx < WMSETUS_SECTION_COUNT) {
            uint32_t s2start = hdr[s2idx];
            uint32_t s2end   = (s2idx + 1 < WMSETUS_SECTION_COUNT)
                                ? hdr[s2idx + 1]
                                : uncompSize;
            uint32_t s2size  = (s2end >= s2start) ? (s2end - s2start) : 0;
            const uint32_t needed = (uint32_t)(WMX_SEG_ROWS * WMX_SEG_COLS);   // 768
            if (s2start < uncompSize && s2end <= uncompSize && s2size >= needed) {
                const uint8_t* s2 = wmsData + s2start;
                bool seenRegion[256] = {};
                int  uniqueRegions = 0;
                int  populatedCells = 0;
                for (int row = 0; row < WMX_SEG_ROWS; row++) {
                    for (int col = 0; col < WMX_SEG_COLS; col++) {
                        uint8_t b = s2[row * WMX_SEG_COLS + col];
                        s_segmentRegionMap[row][col] = b;
                        if (b != 0xFF) {
                            populatedCells++;
                            if (!seenRegion[b]) {
                                seenRegion[b] = true;
                                uniqueRegions++;
                            }
                        }
                    }
                }
                s_segmentRegionLoaded = true;
                Log::World("WorldMap: [REGION-MAP] Section 2 loaded into s_segmentRegionMap: %d populated cells (of %d), %d unique region IDs",
                           populatedCells, WMX_SEG_ROWS * WMX_SEG_COLS, uniqueRegions);
            } else {
                Log::World("WorldMap: [REGION-MAP] Section 2 size mismatch (start=0x%08X end=0x%08X size=%u, needed >=%u) \u2014 not loading",
                           s2start, s2end, s2size, needed);
            }
        }
    }

    // ---- Hex-dump each section in WMSETUS_DUMP_SECTIONS_1IDX (1-indexed).
    // Bounds-check before dumping; if any section's offset/end pair looks
    // out of range, log and skip rather than reading past the buffer.
    auto safeDump = [&](int sectArrayIdx, const char* label) {
        if (sectArrayIdx < 0 || sectArrayIdx >= WMSETUS_SECTION_COUNT) return;
        uint32_t sectStart = hdr[sectArrayIdx];
        uint32_t sectEnd   = (sectArrayIdx + 1 < WMSETUS_SECTION_COUNT)
                              ? hdr[sectArrayIdx + 1]
                              : uncompSize;
        if (sectStart >= uncompSize || sectEnd > uncompSize || sectEnd <= sectStart) {
            Log::World("WorldMap: [TRIGGER-DUMP] %s out-of-range (start=0x%08X end=0x%08X file=%u) — skipping",
                       label, sectStart, sectEnd, uncompSize);
            return;
        }
        DumpTriggerSection(label, wmsData + sectStart, sectEnd - sectStart);
    };
    for (int k = 0; k < WMSETUS_DUMP_COUNT; k++) {
        int sect1Idx = WMSETUS_DUMP_SECTIONS_1IDX[k];
        char label[16];
        snprintf(label, sizeof(label), "sect%02d", sect1Idx);
        safeDump(sect1Idx - 1, label);   // 1-indexed in user log, 0-indexed in array
    }

    free(wmsData);
    return true;
}
