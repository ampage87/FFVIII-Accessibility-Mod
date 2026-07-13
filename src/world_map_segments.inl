// world_map_segments.inl - Coordinate / archive / segment-math layer
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// WM_RUNTIME_WALK_DIAG is defined in world_map_state.inl (included first).
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

// GetVehicleType (and the rest of the coordinate / segment / BFS math) now
// lives in world_map_geometry.inl, included before this file -- visible here
// without a forward declaration.

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

    // v0.18.3.217: FOOT-MOTION OVERRIDE. The engine's per-frame integrator
    // updates the foot DWORDs ONLY while the player IS the foot character
    // (they freeze when a vehicle is mounted -- the documented basis of this
    // whole function). Therefore: if the foot DWORDs are actively changing,
    // the player is on foot RIGHT NOW and any non-foot s_lastVehicle byte is
    // stale/garbage. Observed live (BAT .216, 21:40:22): walking out of
    // Dollet committed locomotion=33 (VEH_CAR) on a foot-only save; every
    // _Active read then returned the savemap car_pos (26319,-30537) on the
    // Balamb continent -- poisoning the drive-start distance (55 km instead
    // of 35), the ROUTENET hop-on (declined: wrong continent), the resume
    // replan (A* burned its 10s budget ocean-routing), the learned trigger
    // circle, and the off-target field identification ("Balamb Garden").
    static int32_t s_fmPrevX = INT32_MIN, s_fmPrevY = 0;
    static DWORD   s_fmLastMoveTick = 0;
    DWORD fmNow = GetTickCount();
    if (s_fmPrevX == INT32_MIN) {                 // first sample this session
        s_fmPrevX = *x; s_fmPrevY = *y; s_fmLastMoveTick = fmNow;
    } else if (*x != s_fmPrevX || *y != s_fmPrevY) {
        s_fmPrevX = *x; s_fmPrevY = *y; s_fmLastMoveTick = fmNow;
    }
    bool footAlive = (fmNow - s_fmLastMoveTick) < 2000;

    // Determine current vehicle from the debounced state. Use s_lastVehicle
    // (the committed locomotion byte) rather than a fresh GetLocomotionMode
    // read, so transient byte cycling during AD doesn't flip us between
    // sources mid-drive. GetVehicleType maps the byte to VehicleType.
    if (s_lastVehicle < 0) return;   // not yet sampled → keep foot DWORDs
    VehicleType veh = GetVehicleType((uint8_t)s_lastVehicle);

    if (veh != VEH_ON_FOOT && footAlive) {
        // Foot DWORDs are integrating => player is the foot character.
        // Transition-only forensic log (same pattern as VEH-POS-FALLBACK).
        static int s_fmLastLoggedVehicle = -1;
        if (s_lastVehicle != s_fmLastLoggedVehicle) {
            Log::World("WorldMap: [VEH-POS-OVERRIDE] foot DWORDs are moving; "
                       "ignoring stale locomotion=%d and using foot pos (%d,%d)",
                       s_lastVehicle, *x, *y);
            s_fmLastLoggedVehicle = s_lastVehicle;
        }
        return;
    }

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

// CalculateWrappedDistance moved to world_map_geometry.inl (#67 test seam).

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
// Coordinate / segment / vehicle / traversability math -> world_map_geometry.inl
// ============================================================================
// WorldXToSegCol, WorldYToSegRow, SegmentCenterToWorld, TorusBearing,
// GetVehicleType, GetBfsRuleClass, IsCanonicalLocomotion and IsSegmentTraversable
// were moved verbatim to world_map_geometry.inl (included before this file),
// so the CI harness (tests/world_map_harness.cpp) can compile and guard the
// engine-coord -> segment mapping and BFS rules on a host compiler. #67/#65.

// ============================================================================
// #67 v0.18.3.84: ROAD overlay -- s_roadFine marks fine cells covered by a
// road/railroad polygon (wmx terrain 27/28). The road is the one mesh tag that
// is both reliably walkable AND runs exactly Timber->Dollet, so we use it to
// route through the mountains (bypassing cliff-vs-land misclassification) and
// as ground-truth to refine the terrain grid. Declared here for now; migrates
// to world_map_state.inl when the planner consumes it (build 2).
// ============================================================================
#define ROAD_MAP_DIAG 0
// v0.18.3.128 (#70): s_roadFine DECLARATION moved to world_map_state.inl (so the
// navmesh connection gate NmEngineStepBlocked can read it for the road-cell
// exemption). Still populated below by RasterizeTriRoad in the LoadTerrainGrid
// polygon loop, exactly as before.

// ============================================================================
// #67: RasterizeTriFine -- rasterize one wmx triangle into s_walkClassFine.
// ============================================================================
// Writes `cls` (and its `steep`ness) into every fine cell whose CENTRE lies
// inside the triangle and is still SEG_OCEAN (first non-ocean polygon
// containing the centre wins; a proper mesh puts each cell centre in exactly
// one triangle, so conflicts are edge-only and negligible). Coordinates are raw
// mesh world units; the cell index is clamped to the grid (the far-east /
// far-north wrap seam is ocean and loses nothing). Point-in-triangle uses int64
// cross-product signs.
static void RasterizeTriFine(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                             int32_t cx, int32_t cy, uint8_t cls, uint16_t steep,
                             int16_t elev)
{
    int32_t minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int32_t maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int32_t miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int32_t maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);

    int gx0 = (int)(minx / WM_FINE_CELL); if (gx0 < 0) gx0 = 0;
    int gx1 = (int)(maxx / WM_FINE_CELL); if (gx1 > WM_FINE_COLS - 1) gx1 = WM_FINE_COLS - 1;
    int gy0 = (int)(miny / WM_FINE_CELL); if (gy0 < 0) gy0 = 0;
    int gy1 = (int)(maxy / WM_FINE_CELL); if (gy1 > WM_FINE_ROWS - 1) gy1 = WM_FINE_ROWS - 1;

    for (int gy = gy0; gy <= gy1; gy++) {
        for (int gx = gx0; gx <= gx1; gx++) {
            if (s_walkClassFine[gy][gx] != SEG_OCEAN) continue;   // first non-ocean wins
            int32_t pcx = gx * WM_FINE_CELL + WM_FINE_CELL / 2;
            int32_t pcy = gy * WM_FINE_CELL + WM_FINE_CELL / 2;
            int64_t d1 = (int64_t)(pcx - bx) * (ay - by) - (int64_t)(ax - bx) * (pcy - by);
            int64_t d2 = (int64_t)(pcx - cx) * (by - cy) - (int64_t)(bx - cx) * (pcy - cy);
            int64_t d3 = (int64_t)(pcx - ax) * (cy - ay) - (int64_t)(cx - ax) * (pcy - ay);
            bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos)) {
                s_walkClassFine[gy][gx] = cls;
                s_steepFine[gy][gx]     = steep;
                s_elevFine[gy][gx]      = elev;   // #69 mechanism 2: per-cell floor height
            }
        }
    }
}

// ============================================================================
// #67 v0.18.3.84: RasterizeTriRoad -- flag fine cells covered by a road poly
// into s_roadFine (overlay; no first-wins gate, roads sit on top of land). In
// addition to the centre-in-triangle test, the three vertex cells and the
// centroid cell are flagged unconditionally, so a road ribbon narrower than a
// 1024-unit cell still registers instead of falling between cell centres.
// ============================================================================
static void RasterizeTriRoad(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                             int32_t cx, int32_t cy)
{
    int32_t vtx[4][2] = { {ax,ay}, {bx,by}, {cx,cy},
                          {(ax+bx+cx)/3, (ay+by+cy)/3} };
    for (int i = 0; i < 4; i++) {
        int gx = (int)(vtx[i][0] / WM_FINE_CELL);
        int gy = (int)(vtx[i][1] / WM_FINE_CELL);
        if (gx >= 0 && gx < WM_FINE_COLS && gy >= 0 && gy < WM_FINE_ROWS)
            s_roadFine[gy][gx] = 1;
    }
    int32_t minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int32_t maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int32_t miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int32_t maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    int gx0 = (int)(minx / WM_FINE_CELL); if (gx0 < 0) gx0 = 0;
    int gx1 = (int)(maxx / WM_FINE_CELL); if (gx1 > WM_FINE_COLS - 1) gx1 = WM_FINE_COLS - 1;
    int gy0 = (int)(miny / WM_FINE_CELL); if (gy0 < 0) gy0 = 0;
    int gy1 = (int)(maxy / WM_FINE_CELL); if (gy1 > WM_FINE_ROWS - 1) gy1 = WM_FINE_ROWS - 1;
    for (int gy = gy0; gy <= gy1; gy++) {
        for (int gx = gx0; gx <= gx1; gx++) {
            int32_t pcx = gx * WM_FINE_CELL + WM_FINE_CELL / 2;
            int32_t pcy = gy * WM_FINE_CELL + WM_FINE_CELL / 2;
            int64_t d1 = (int64_t)(pcx - bx) * (ay - by) - (int64_t)(ax - bx) * (pcy - by);
            int64_t d2 = (int64_t)(pcx - cx) * (by - cy) - (int64_t)(bx - cx) * (pcy - cy);
            int64_t d3 = (int64_t)(pcx - ax) * (cy - ay) - (int64_t)(cx - ax) * (pcy - ay);
            bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos)) s_roadFine[gy][gx] = 1;
        }
    }
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
    // #67: fine grid starts all-ocean; non-ocean polygons rasterize over it.
    memset(s_walkClassFine, SEG_OCEAN, sizeof(s_walkClassFine));
    memset(s_steepFine, 0, sizeof(s_steepFine));   // #67 BAT 2: 0 = flat = never blocks
    memset(s_elevFine, 0, sizeof(s_elevFine));     // #69 mechanism 2: 0 = baseline floor
    memset(s_roadFine, 0, sizeof(s_roadFine));     // #67 v0.18.3.84: road overlay
    int terrainHist[256] = {};                      // #67 v0.18.3.84: terrain-type tally
    int oceanSegs = 0, forestSegs = 0, landSegs = 0;
    int totalRealPolys = 0, totalOceanPolys = 0, totalForestPolys = 0;

#if NAVMESH_DIAG
    Navmesh_Reset();   // v0.18.3.104: accumulate non-ocean triangles for the true navmesh
#endif

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
            uint8_t vertCount = blockBase[1];   // #67: needed for fine rasterization
            // (norm_count = blockBase[2], pad = blockBase[3] — unused here.)

            // Bounds-guard: polygon array must fit within the segment.
            uint32_t polyArrayEnd = blockOffset + WMX_BLOCK_HDR_SIZE +
                                    (uint32_t)polyCount * WMX_POLY_SIZE;
            if (polyArrayEnd > WMX_SEGMENT_SIZE) continue;

            // #67: read this block's vertices into world space for fine
            // rasterization. Vertices follow the polygon array; each is 8
            // bytes: int16 x at +0, int16 (-elevation) at +2, int16 y at +4,
            // uint16 pad at +6. World position adds the block origin -- a
            // segment is 8192 units, each of its 4x4 blocks is 2048.
            int bRow = b / 4, bCol = b % 4;
            int32_t ox = (int32_t)col * 8192 + bCol * 2048;
            int32_t oy = (int32_t)row * 8192 + bRow * 2048;
            const uint8_t* vertBase = blockBase + WMX_BLOCK_HDR_SIZE +
                                      (uint32_t)polyCount * WMX_POLY_SIZE;
            uint32_t vertArrayEnd = polyArrayEnd + (uint32_t)vertCount * 8;
            bool vertsOk = (vertArrayEnd <= WMX_SEGMENT_SIZE);
            static int32_t vwx[256], vwy[256];
            static int16_t vwz[256];   // #67 BAT 2: vertex elevation (-z) for steepness
            if (vertsOk) {
                for (int v = 0; v < vertCount; v++) {
                    int16_t lvx = *(const int16_t*)(vertBase + v * 8 + 0);
                    int16_t lvy = *(const int16_t*)(vertBase + v * 8 + 4);
                    vwz[v]      = *(const int16_t*)(vertBase + v * 8 + 2);
                    vwx[v] = ox + lvx;
                    // #70 fix: wmx vertex Y (word2) runs 0..-2048; the height-query mesh
                    // frame (NmGameToMeshY) runs the opposite way, so 'oy + lvy' placed
                    // every triangle ~one block off in Y and the query read the adjacent
                    // shallow coast (~-200) instead of the deep ground (~-595) -> the 200
                    // step gate severed the link (the Galbadia/Timber wedge). Negating lvy
                    // aligns the navmesh with the query. Proven vs 205 [GROUNDH] samples.
                    vwy[v] = oy - lvy;
                }
            }

            for (int p = 0; p < polyCount; p++) {
                const uint8_t* poly = blockBase + WMX_BLOCK_HDR_SIZE + p * WMX_POLY_SIZE;
                uint8_t terrain = poly[WMX_TERRAIN_OFFSET];
                terrainHist[terrain]++;   // #67 v0.18.3.84: terrain-type tally
                bool isOcean = (terrain >= 32 && terrain <= 34);
                if (isOcean) {
                    segOceanCount++;
                } else if (terrain <= 5) {
                    // Forest variants: 0=Galbadia, 1=Trabia, 2=Esthar,
                    // 3=Centra, 4=Balamb, 5=Esthar.
                    segForestCount++;
                }
                segPolyCount++;

                // #67: rasterize non-ocean polygons into the fine grid. Ocean
                // cells stay SEG_OCEAN from the memset. terrain 29 = mountain.
                if (vertsOk && !isOcean) {
                    uint8_t cls = (terrain == 29) ? SEG_MOUNTAIN
                                : (terrain <= 5)  ? SEG_FOREST
                                                  : SEG_LAND;
                    uint8_t i0 = poly[0], i1 = poly[1], i2 = poly[2];
                    if (i0 < vertCount && i1 < vertCount && i2 < vertCount) {
                        // #67 BAT 2: per-poly steepness = vertex elevation spread.
                        int16_t e0 = vwz[i0], e1 = vwz[i1], e2 = vwz[i2];
                        int16_t emax = e0 > e1 ? (e0 > e2 ? e0 : e2) : (e1 > e2 ? e1 : e2);
                        int16_t emin = e0 < e1 ? (e0 < e2 ? e0 : e2) : (e1 < e2 ? e1 : e2);
                        uint16_t steep = (uint16_t)(emax - emin);
                        // #69 mechanism 2: per-cell representative floor height
                        // = poly average vertex elevation (for the planner's
                        // height-step edge-adjacency guard).
                        int16_t elev = (int16_t)(((int32_t)e0 + e1 + e2) / 3);
                        RasterizeTriFine(vwx[i0], vwy[i0], vwx[i1], vwy[i1],
                                         vwx[i2], vwy[i2], cls, steep, elev);
#if NAVMESH_DIAG
                        // v0.18.3.104: feed the SAME triangle (identical verts
                        // + i0/i1/i2<vertCount guard) into the true navmesh so
                        // it sees exactly what the fine grid does.
                        // v0.18.3.107: feed ALL non-ocean triangles -- NO
                        // poly[0x0E] bit7 filter. The .105 bit7 filter was wrong:
                        // it deleted the gentle terr-27/29 ramps of the real
                        // canyon approach to Dollet, leaving only bit7-set cliffs
                        // (offline bottleneck to Dollet 1018u). On-foot
                        // walkability is now modeled by ELEVATION STEP in
                        // Navmesh_Build (z-aware bridging + z-gated proximity),
                        // matching the fine grid's steepness rule. See #70.
                        // v0.18.3.189: feed ONLY truly foot-walkable polys. The engine's on-foot
                        // walkable flag is poly[0x0F] bit7 -- NOT a terrain type, and NOT the .105
                        // poly[0x0E] (byte 14) that was tried and discarded as "wrong" (it was simply
                        // the wrong byte). Proven on wmx.obj: bit7 is SET on every land/road poly and
                        // CLEAR on all 315777 ocean polys AND all 54291 cliff-face (terrain-29) polys --
                        // exactly the surfaces the engine refuses on foot (the Dollet canyon descent,
                        // the high-ground region rims the .187 BAT froze on). With cliffs excluded,
                        // WorldGroundHeightLocal returns NO_GROUND there, so the GRID planner routes
                        // AROUND them instead of straight down a wall the character then wedges on. The
                        // gentle road/land ramps (bit7 set) stay, so legitimate routes are unaffected.
                        if ((poly[0x0F] & 0x80) != 0) {
                            Navmesh_AddTriangle(vwx[i0], vwy[i0], vwz[i0],
                                                vwx[i1], vwy[i1], vwz[i1],
                                                vwx[i2], vwy[i2], vwz[i2], terrain);
                        }
#endif
                    }
                }

                // #67 v0.18.3.84: ROAD overlay. Road polys (terrain 27/28 =
                // Road/Railroad) also rasterize as SEG_LAND above; here we ALSO
                // flag their fine cells in s_roadFine so the planner can treat
                // the road as ground-truth walkable and route along it.
                // v0.18.3.134 (#70): ALSO include terrain 12 = BRIDGE. Dollet (and
                // other coastal towns) connect to the mainland road across a short
                // raised BRIDGE deck, which is a distinct terrain type the overlay
                // previously excluded -- so the navmesh road-bridge, the gate's
                // road-road exemption, the .85 walkable override and the .127 floor
                // clamp all skipped it, leaving the bridge deck connected only to
                // the deep water beside it. A* then refused the severed bridge and
                // dived the coast (the #70 4km wedge). Tagging the bridge as road
                // folds it into the road network so all four mechanisms connect it.
                if (vertsOk && (terrain == 27 || terrain == 28 || terrain == 12)) {
                    uint8_t r0 = poly[0], r1 = poly[1], r2 = poly[2];
                    if (r0 < vertCount && r1 < vertCount && r2 < vertCount)
                        RasterizeTriRoad(vwx[r0], vwy[r0], vwx[r1], vwy[r1],
                                         vwx[r2], vwy[r2]);
                }
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

#if NAVMESH_DIAG
    // v0.18.3.104: build the true triangle navmesh from the accumulated
    // triangles (vertex dedup -> exact shared-edge adjacency -> axis-aligned
    // T-junction bridging -> CSR). NON-INVASIVE: nothing routes on it yet;
    // Navmesh_LogConnectivity() (from Initialize) is the BAT 1 probe.
    //
    // #70 v0.18.3.135: BRIDGE the road->Dollet gap BEFORE the build, so the gate
    // exemption + .127 clamp + .85 [ROADMAP] override all see it. CORRECTED from
    // the .130 tag, which tagged the WRONG cell. The .134 [DOLLETBRIDGE] corridor
    // dump revealed the real geography: a shallow ROAD CAUSEWAY runs along fine
    // row 55 from c107 east to c112 (already road = terr 28/6, floorZ ~-500), and
    // Dollet town is fine(c112,r57). The gap is the 2-cell column from the causeway
    // end c112,r55 -> c112,r56 -> Dollet c112,r57: c112,r56 is deep-coast non-road,
    // and the ~2-cell (2048u) jump exceeds the radius bridge's 1536u, so the navmesh
    // leaves Dollet connected to the causeway only via the deep coast and A* dives
    // it (the #70 4km wedge). The OLD .130 tag c113,r56 was a red herring -- terrain
    // 29 (mountain spur) at -172, ISOLATED (its one large triangle tri#38418
    // centroid-bins to a different cell, so the radius bridge never reached it), and
    // not on the causeway. FIX: tag the REAL gap cell c112,r56 AND the Dollet town
    // cell c112,r57 as road, turning the 2-cell jump into two 1-cell hops
    // (c112,r55 <-> c112,r56 <-> c112,r57) the radius bridge can span; unlike
    // c113,r56 these cells have many small triangles binned in-cell. The exemption
    // then keeps those connections, the clamp shallows them, and [ROADMAP] forces
    // them walkable -- completing an all-road path into Dollet that A* prefers over
    // the coast dive. s_roadFine is [row][col]. Hardcoded one-off like the .81/.85
    // Dollet patches; retire once #70 is BAT-confirmed + pushed.
    s_roadFine[56][112] = 1;   // gap cell between the r55 causeway end and Dollet town
    s_roadFine[57][112] = 1;   // Dollet town cell (catalog tri#38414) -> joins the road
    if (Navmesh_Build()) {
        int nmComp = 0, nmLargest = 0;
        Navmesh_ComponentStats(&nmComp, &nmLargest);
        Log::World("WorldMap: [NAVMESH] built %d triangles, %d components, largest=%d; gate dropped %d connections, road-cell exemption kept %d",
                   Navmesh_TriangleCount(), nmComp, nmLargest, s_nmStepBlocked, s_nmRoadExempt);

        // v0.18.3.127 (#70): correct the mean-of-corners floorZ artifact at ROAD
        // cells. A navmesh triangle straddling a canyon rim reads a deep MEAN
        // floorZ (e.g. -1341 at the Timber->Dollet road's r63 crossing) even
        // where the walkable road surface is shallow, which makes A* unable to
        // tell the shallow road from the deep canyon next to it -- so A* dives.
        // The road is ground-truth walkable (s_roadFine exists precisely to route
        // through the mountains past cliff-vs-land misclassification), so clamp
        // any anomalously-deep floorZ at a road cell up to a moderate road depth.
        // This is a model correction, not steering. It corrects A*'s COST only;
        // the .120 connection gate (corner heights, decided during the build
        // above) is unaffected, so the [ROADCON] severance walk on the next BAT
        // shows whether cost-correction alone is enough.
        {
            const int32_t ROAD_FLOOR_DEEP  = -1000;  // deeper than this at a road cell = artifact
            const int32_t ROAD_FLOOR_CLAMP = -500;   // clamp up to this moderate road depth
            int nClampRoad = 0, ntTri = Navmesh_TriangleCount();
            for (int t = 0; t < ntTri; t++) {
                if (s_nmFloor[t] >= ROAD_FLOOR_DEEP) continue;
                int32_t cgx, cgy;
                if (!Navmesh_TriangleCentroidGame(t, &cgx, &cgy)) continue;
                int fcc = WorldXToFineCol(cgx), fcr = WorldYToFineRow(cgy);
                if (fcr < 0 || fcr >= WM_FINE_ROWS || fcc < 0 || fcc >= WM_FINE_COLS) continue;
                if (!s_roadFine[fcr][fcc]) continue;
                s_nmFloor[t] = ROAD_FLOOR_CLAMP;
                nClampRoad++;
            }
            Log::World("WorldMap: [ROADFLOOR] clamped %d road-cell triangles deeper than %d up to %d (mean-of-corners artifact fix)", nClampRoad, ROAD_FLOOR_DEEP, ROAD_FLOOR_CLAMP);
        }

        // ---- [DOLLETBRIDGE] terrain-type map of the Dollet approach (v0.18.3.134, #70, READ-ONLY) ----
        // Confirms the bridge hypothesis. Dumps: (1) the global terrain-type
        // histogram (is terr 12=bridge present? how many polys?), (2) every
        // terr-12 bridge triangle map-wide with its cell, (3) every navmesh
        // triangle in the Dollet road-end -> town corridor with terrain type,
        // floor and road-status. After the .134 fix the terr-12 cells should now
        // read road=1 and join the road body. Pure logging; adds no connections.
        {
            char hist[768]; int hp = 0;
            for (int tt = 0; tt < 256; tt++)
                if (terrainHist[tt] && hp < (int)sizeof(hist) - 16)
                    hp += snprintf(hist + hp, sizeof(hist) - hp, "%d:%d ", tt, terrainHist[tt]);
            if (hp == 0) { hist[0] = '-'; hist[1] = '\0'; }
            Log::World("WorldMap: [DOLLETBRIDGE] terrain-type histogram (type:polys): %s", hist);

            const int ntTri = Navmesh_TriangleCount();
            int nBridge = 0;
            for (int t = 0; t < ntTri; t++) {
                if (s_nmTerr[t] != 12) continue;
                int32_t cgx, cgy;
                if (!Navmesh_TriangleCentroidGame(t, &cgx, &cgy)) continue;
                const int fc = WorldXToFineCol(cgx), fr = WorldYToFineRow(cgy);
                const bool inb = (fr >= 0 && fr < WM_FINE_ROWS && fc >= 0 && fc < WM_FINE_COLS);
                if (nBridge < 48)
                    Log::World("WorldMap: [DOLLETBRIDGE] BRIDGE(terr12) tri#%d fine(c%d,r%d) floorZ=%d road=%d",
                               t, fc, fr, s_nmFloor[t], (inb && s_roadFine[fr][fc]) ? 1 : 0);
                nBridge++;
            }
            Log::World("WorldMap: [DOLLETBRIDGE] total terr-12 bridge triangles map-wide: %d", nBridge);

            // Dollet road-end -> town corridor (fine rows 54..61, cols 107..114)
            const int DR0 = 54, DR1 = 61, DC0 = 107, DC1 = 114;
            for (int t = 0; t < ntTri; t++) {
                int32_t cgx, cgy;
                if (!Navmesh_TriangleCentroidGame(t, &cgx, &cgy)) continue;
                const int fc = WorldXToFineCol(cgx), fr = WorldYToFineRow(cgy);
                if (fr < DR0 || fr > DR1 || fc < DC0 || fc > DC1) continue;
                Log::World("WorldMap: [DOLLETBRIDGE] tri#%d fine(c%d,r%d) terr=%d floorZ=%d road=%d",
                           t, fc, fr, (int)s_nmTerr[t], s_nmFloor[t], (s_roadFine[fr][fc]) ? 1 : 0);
            }
        }
    } else {
        Log::World("WorldMap: [NAVMESH] build produced 0 triangles");
    }
#endif

    // #67 v0.18.3.81 (RESTORED v0.18.3.93): Dollet false-coast no-walk patch
    // (hardcoded; bounds in DOLLET_COAST_* in state.inl). Build 5 (.92) retired
    // this on the theory that mechanism 2 (the height-step edge guard) excluded
    // the ledge from geometry -- but the .92 BAT REGRESSED: with the patch gone
    // the clearance planner routed straight through the ex-ledge (cols 104-110)
    // and wedged ~15km out. The guard catches the cliff FACE (~1500-unit step)
    // but NOT the ledge's in-game impassability where its elevation is
    // continuous -- exactly the original #67 finding that no pure-geometry rule
    // separates this ledge from real land. So the patch is load-bearing and
    // STAYS. (The .85 road-walkable override + WM_OFFROAD_PENALTY were also
    // retired in .92 and STAY retired -- the .92 [ROUTEMAP] showed the west lane
    // walkable without the override, and the route avoids the ledge once this
    // patch re-blocks it.) Applied AFTER rasterization, BEFORE the clearance BFS
    // so clearance/reachability/planner all treat the ledge as a wall; marked
    // forced-steep MOUNTAIN so the foot/car block rule catches it with no change
    // to IsFineTraversable; Garden/Ragnarok bypass the fine grid.
    {
        int pc0 = WorldXToFineCol(DOLLET_COAST_X0), pc1 = WorldXToFineCol(DOLLET_COAST_X1);
        int pr0 = WorldYToFineRow(DOLLET_COAST_Y0), pr1 = WorldYToFineRow(DOLLET_COAST_Y1);
        if (pc0 > pc1) { int t = pc0; pc0 = pc1; pc1 = t; }
        if (pr0 > pr1) { int t = pr0; pr0 = pr1; pr1 = t; }
        int patched = 0;
        for (int r = pr0; r <= pr1; r++) {
            if (r < 0 || r >= WM_FINE_ROWS) continue;
            for (int c = pc0; c <= pc1; c++) {
                if (c < 0 || c >= WM_FINE_COLS) continue;
                s_walkClassFine[r][c] = SEG_MOUNTAIN;
                s_steepFine[r][c]     = 0xFFFF;   // > WM_MTN_STEEP_BLOCK => blocked for foot/car
                patched++;
            }
        }
        Log::World("WorldMap: [TERRAIN] Dollet false-coast patch: blocked %d fine cells cols[%d..%d] rows[%d..%d] (world X[%d..%d] Y[%d..%d])",
                   patched, pc0, pc1, pr0, pr1,
                   (int)DOLLET_COAST_X0, (int)DOLLET_COAST_X1, (int)DOLLET_COAST_Y0, (int)DOLLET_COAST_Y1);
    }

    // #67 v0.18.3.85 (RESTORED v0.18.3.94): ROAD is ground-truth walkable. After
    // rasterization AND the .81 false-coast patch, force every road cell
    // (s_roadFine) to a walkable class with steep=0, so the road OVERRIDES both
    // steep-mountain misclassification and the .81 patch wherever they fall on
    // the road. The Timber->Dollet road runs THROUGH the .81 box (cols 104-111
    // rows 59-69); the .81 patch alone walls it off and DISCONNECTS Dollet from
    // the start (proven: .93 retired this override and Dollet went unreachable,
    // 14-cell path to a dead-end 15 cells short). Non-road cells (incl. the rest
    // of the false ledge) keep their classification, so ONLY the road reconnects
    // -- this is the carve-back that makes the .81 box passable along the road.
    // Applied BEFORE the clearance BFS so clearance/reachability/planner all see
    // the road as the open corridor through the cliff pinch. LOAD-BEARING; do
    // NOT retire (the .92/.93 cleanup wrongly did and regressed).
    {
        int forcedRoad = 0, wasBlk = 0;
        for (int r = 0; r < WM_FINE_ROWS; r++)
            for (int c = 0; c < WM_FINE_COLS; c++) {
                if (!s_roadFine[r][c]) continue;
                uint8_t cl = s_walkClassFine[r][c];
                bool blk = (cl == SEG_OCEAN) ||
                           (cl == SEG_MOUNTAIN && s_steepFine[r][c] > WM_MTN_STEEP_BLOCK);
                s_walkClassFine[r][c] = SEG_LAND;
                s_steepFine[r][c]     = 0;
                forcedRoad++;
                if (blk) wasBlk++;
            }
        Log::World("WorldMap: [ROADMAP] road-walkable override: %d road cells forced walkable (%d had been blocked by steep-mtn or the .81 patch)",
                   forcedRoad, wasBlk);
    }

    // #67: tally the fine walkable grid and mark it loaded so the catalog
    // switches onto the continuous flood-fill reachability path.
    {
        int fLand = 0, fForest = 0, fMtn = 0, fOcean = 0, fMtnBlocked = 0;
        for (int r = 0; r < WM_FINE_ROWS; r++)
            for (int c = 0; c < WM_FINE_COLS; c++) {
                switch (s_walkClassFine[r][c]) {
                    case SEG_FOREST:   fForest++; break;
                    case SEG_MOUNTAIN: fMtn++;
                        if (s_steepFine[r][c] > WM_MTN_STEEP_BLOCK) fMtnBlocked++;
                        break;
                    case SEG_OCEAN:    fOcean++;  break;
                    default:           fLand++;   break;
                }
            }
        // #67 v0.18.3.59: clearance field -- Chebyshev distance (in cells) from
        // every walkable cell to the nearest BLOCKED cell (ocean or steep
        // mountain), via multi-source BFS seeded with all blocked cells at 0.
        // Each cell is enqueued exactly once (FIFO BFS is layer-monotonic), so
        // the queue never exceeds the cell count. The planner uses this to route
        // down corridor centres. Capped at 254 (255 = sentinel, never reached on
        // a map this size since ocean bounds every continent).
        int clrMax = 0, clrTight = 0;
        {
            static int clrQ[WM_FINE_COLS * WM_FINE_ROWS];
            int qh = 0, qt = 0;
            for (int r = 0; r < WM_FINE_ROWS; r++)
                for (int c = 0; c < WM_FINE_COLS; c++) {
                    uint8_t cl = s_walkClassFine[r][c];
                    bool blk = (cl == SEG_OCEAN) ||
                               (cl == SEG_MOUNTAIN && s_steepFine[r][c] > WM_MTN_STEEP_BLOCK);
                    if (blk) { s_clearFine[r][c] = 0; clrQ[qt++] = r * WM_FINE_COLS + c; }
                    else       s_clearFine[r][c] = 255;
                }
            while (qh < qt) {
                int idx = clrQ[qh++];
                int r = idx / WM_FINE_COLS, c = idx % WM_FINE_COLS;
                if (s_clearFine[r][c] >= 254) continue;   // don't propagate past the cap
                uint8_t nd = (uint8_t)(s_clearFine[r][c] + 1);
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = r + dr, nc = c + dc;
                        if (nr < 0 || nr >= WM_FINE_ROWS || nc < 0 || nc >= WM_FINE_COLS) continue;
                        if (s_clearFine[nr][nc] > nd) {
                            s_clearFine[nr][nc] = nd;
                            clrQ[qt++] = nr * WM_FINE_COLS + nc;
                        }
                    }
            }
            for (int r = 0; r < WM_FINE_ROWS; r++)
                for (int c = 0; c < WM_FINE_COLS; c++) {
                    uint8_t cl = s_walkClassFine[r][c];
                    bool walk = !((cl == SEG_OCEAN) ||
                                  (cl == SEG_MOUNTAIN && s_steepFine[r][c] > WM_MTN_STEEP_BLOCK));
                    if (!walk) continue;
                    if (s_clearFine[r][c] > clrMax && s_clearFine[r][c] < 255) clrMax = s_clearFine[r][c];
                    if (s_clearFine[r][c] <= 1) clrTight++;
                }
        }
        s_walkGridLoaded = true;
        Log::World("WorldMap: [WALKFINE] Fine grid (256x192) rasterized: %d land, %d forest, %d mountain (%d steep-blocked >%u), %d ocean (walkable = %d); clearance max=%d, wall-hugging cells(clr<=1)=%d",
                   fLand, fForest, fMtn, fMtnBlocked, (unsigned)WM_MTN_STEEP_BLOCK, fOcean, fLand + fForest + (fMtn - fMtnBlocked), clrMax, clrTight);
    }

#if WM_RUNTIME_WALK_DIAG
    // v0.18.3.101: defer -- the polygon array at 0x20426C0 is not populated
    // until the world map renders its first frame. Poll() checks polyCount
    // each tick and fires DumpRuntimeWalkability() once it reads > 0.
    s_rtWalkDumpPending = true;
    Log::World("WorldMap: [RTWALK] dump deferred -- will fire on first Poll() tick with polyCount > 0");
#endif

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

    (void)terrainHist;
#if ROAD_MAP_DIAG
    // #67 v0.18.3.84: terrain-type histogram + ROAD overlay map. Confirms which
    // wmx terrain types exist (and that road 27/28 is present), counts road
    // fine cells, and prints the road ribbon over the fine class grid for the
    // Galbadia/Dollet corridor -- so we can see the Timber->Dollet road
    // relative to the cliff the planner has been routing into. Retire (set
    // ROAD_MAP_DIAG 0) before the #67 push.
    {
        int roadCells = 0;
        for (int r = 0; r < WM_FINE_ROWS; r++)
            for (int c = 0; c < WM_FINE_COLS; c++)
                if (s_roadFine[r][c]) roadCells++;
        char hist[600]; int hp = 0;
        for (int t = 0; t < 256; t++)
            if (terrainHist[t] && hp < (int)sizeof(hist) - 16)
                hp += snprintf(hist + hp, sizeof(hist) - hp, "%d:%d ", t, terrainHist[t]);
        if (hp == 0) { hist[0] = '-'; hist[1] = '\0'; }
        Log::World("WorldMap: [ROADMAP] road fine cells=%d; terrain-type histogram (type:polys): %s",
                   roadCells, hist);
        const int R0 = 45, R1 = 76, C0 = 88, C1 = 121;
        int dCol = WorldXToFineCol(-15639), dRow = WorldYToFineRow(-39437); // Dollet catalog coord
        Log::World("WorldMap: [ROADMAP] rows %d..%d cols %d..%d  R=road !=road-but-blocked D=Dollet ~ocean .land f forest m mtn ^mtn-blocked",
                   R0, R1, C0, C1);
        for (int r = R0; r <= R1 && r < WM_FINE_ROWS; r++) {
            char line[80]; int lp = 0;
            for (int c = C0; c <= C1 && c < WM_FINE_COLS; c++) {
                uint8_t cl = s_walkClassFine[r][c];
                bool blk = (cl == SEG_OCEAN) ||
                           (cl == SEG_MOUNTAIN && s_steepFine[r][c] > WM_MTN_STEEP_BLOCK);
                char ch;
                if (s_roadFine[r][c])            ch = blk ? '!' : 'R';
                else if (r == dRow && c == dCol) ch = 'D';
                else if (cl == SEG_OCEAN)        ch = '~';
                else if (cl == SEG_FOREST)       ch = 'f';
                else if (cl == SEG_MOUNTAIN)     ch = blk ? '^' : 'm';
                else                             ch = '.';
                line[lp++] = ch;
            }
            line[lp] = '\0';
            Log::World("WorldMap: [ROADMAP] r%03d %s", r, line);
        }
    }
#endif

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

// ============================================================================
// WM_RUNTIME_WALK_DIAG (v0.18.3.101)
// ============================================================================
#if WM_RUNTIME_WALK_DIAG
static void DumpRuntimeWalkability()
{
    // Read the polygon DESCRIPTOR ARRAY base. For polygon N:
    //   adjListPtr = *(uint32_t*)(descBase + N*WM_RT_DESC_STRIDE)
    // and the adjacency list is words at WM_RT_ADJ_STRIDE: 0xFFFF = end,
    // 0xFFFE = impassable edge, else neighbour polygon index. See the long
    // note in world_map_state.inl for how this was verified.
    //
    // NOTE: WM_RT_POLY_COUNT (0x02045C40) is UNRELIABLE here -- it is tied to
    // the separate geometry array and read 11 in a live BAT. We log it only as
    // an observation; the probe loop uses a fixed small range so a bad count
    // cannot truncate it. A full BFS (later) must derive the count differently
    // (e.g. the offline-confirmed 473193) or walk until descriptor entries go
    // null, NOT trust this address.
    uint32_t descBase = 0;
    uint32_t polyCountObs = 0;
    __try {
        descBase     = *(const uint32_t*)WM_RT_DESC_BASE;
        polyCountObs = *(const uint32_t*)WM_RT_POLY_COUNT;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::World("WorldMap: [RTWALK] SEH fault reading descBase/count -- aborting");
        return;
    }

    Log::World("WorldMap: [RTWALK] descBase=0x%08X  polyCount(obs,unreliable)=%u",
               descBase, polyCountObs);

    if (descBase == 0) {
        Log::World("WorldMap: [RTWALK] descBase is 0 -- mesh not loaded (should not reach here; Poll polls for != 0)");
        return;
    }

    // Probe the adjacency lists for the first few polygon indices to verify the
    // descriptor->adj-list->stride-32 structure prints sensible neighbours.
    Log::World("WorldMap: [RTWALK] Probing adjacency lists for polys 0..9 (descStride=%d adjStride=%d):",
               WM_RT_DESC_STRIDE, WM_RT_ADJ_STRIDE);
    for (uint32_t pi = 0; pi < 10; pi++) {
        uint32_t adjPtr = 0;
        __try { adjPtr = *(const uint32_t*)(descBase + pi * WM_RT_DESC_STRIDE); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::World("WorldMap: [RTWALK]   poly[%u]: FAULT reading descriptor", pi);
            break;
        }
        if (adjPtr == 0) {
            Log::World("WorldMap: [RTWALK]   poly[%u]: adjPtr=NULL", pi);
            continue;
        }
        // Walk up to 8 adjacency entries (word at +0 of each stride-32 entry).
        char buf[160]; int bp = 0;
        for (int k = 0; k < 8; k++) {
            uint16_t w = 0;
            __try { w = *(const uint16_t*)(adjPtr + k * WM_RT_ADJ_STRIDE); }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                bp += snprintf(buf + bp, sizeof(buf) - bp, "FAULT "); break;
            }
            if (w == WM_RT_SENTINEL_END)   { bp += snprintf(buf + bp, sizeof(buf) - bp, "END"); break; }
            if (w == WM_RT_SENTINEL_NOADJ) { bp += snprintf(buf + bp, sizeof(buf) - bp, "WALL "); continue; }
            bp += snprintf(buf + bp, sizeof(buf) - bp, "%u ", w);
        }
        Log::World("WorldMap: [RTWALK]   poly[%u]: adjPtr=0x%08X neighbours=[%s]", pi, adjPtr, buf);
    }

    // Sanity probe of the CURRENT-polygon adjacency pointer. NOTE: this address
    // (0x0203EE80) aliases the foot-position X DWORD on the live world map, so
    // while the player is moving this most likely reads as a coordinate, not a
    // pointer -- the value tells us which meaning is active this tick.
    uint32_t curAdjPtr = 0;
    __try { curAdjPtr = *(const uint32_t*)WM_RT_CUR_ADJ_PTR; } __except(EXCEPTION_EXECUTE_HANDLER){}
    Log::World("WorldMap: [RTWALK] [0203EE80] (aliases foot-X) = 0x%08X", curAdjPtr);
}
#endif  // WM_RUNTIME_WALK_DIAG

// ============================================================================
// WM_CALIB_DIAG (#67) -- disambiguate the Galbadia "empty catalog" bug:
// (a) coordinate-mapping skew vs (b) terrain-classifier mislabel. Passive and
// fully blind-accessible -- no key, no sighted step. Two parts:
//   1. DumpRegionGridDiag(): dumps the wmsetus Section-2 region map as a
//      land/sea mask (same '#'/'~' format as the [TERRAIN] grid) plus raw
//      region IDs, so the game's own land oracle can be overlaid on our
//      classifier's grid. If they agree on where land is, the classifier is
//      fine and the bug is the coordinate mapping; if they disagree where the
//      player stands, the classifier is wrong.
//   2. PollWorldCalibDiag(): traces the live position -> segment -> terrain
//      class + region byte as the player walks (one line per new cell). Shows
//      exactly where the player's real position maps and whether that cell is
//      land per either oracle.
// Gate-don't-delete: flip WM_CALIB_DIAG to 0 to retire after #67 is diagnosed.
// ============================================================================
#define WM_CALIB_DIAG 0

#if WM_CALIB_DIAG
static void DumpRegionGridDiag()
{
    if (!s_segmentRegionLoaded) {
        Log::World("WorldMap: [WM-CALIB] region map NOT loaded -- cannot dump region grid");
        return;
    }
    // Land/sea mask from the game's own region map (0xFF = ocean / none).
    // Identical '#'/'~' format to the [TERRAIN] grid dump for direct overlay.
    Log::World("WorldMap: [WM-CALIB] region land/sea mask (# = region present, ~ = 0xFF/none) -- overlay on [TERRAIN] grid:");
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        char rowStr[WMX_SEG_COLS + 1];
        for (int c = 0; c < WMX_SEG_COLS; c++) {
            rowStr[c] = (s_segmentRegionMap[r][c] == 0xFF) ? '~' : '#';
        }
        rowStr[WMX_SEG_COLS] = '\0';
        Log::World("WorldMap: [WM-CALIB] regmask%02d: %s", r, rowStr);
    }
    // Raw region IDs (2-hex per cell) so we can identify WHICH region is which
    // continent and cross-reference the planner's region IDs.
    Log::World("WorldMap: [WM-CALIB] raw region IDs (00..FE = region, FF = none):");
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        char rowStr[WMX_SEG_COLS * 3 + 1];
        int pos = 0;
        for (int c = 0; c < WMX_SEG_COLS; c++) {
            pos += snprintf(rowStr + pos, sizeof(rowStr) - pos, "%02X ", s_segmentRegionMap[r][c]);
        }
        Log::World("WorldMap: [WM-CALIB] regid%02d: %s", r, rowStr);
    }
}

// Per-tick live-position trace. Logs only when the player's mapped segment
// changes (one line per new cell as they walk -- no spam). Pure read; never
// touches s_reachable, so it cannot perturb the live catalog's BFS state.
static void PollWorldCalibDiag(int32_t px, int32_t py)
{
    // #67: dedup on the FINE cell so the trace has per-fine-cell granularity
    // for mountain calibration (one line per new 1024-unit cell the player
    // occupies), not just per coarse segment.
    static int s_lastFineCol = -999, s_lastFineRow = -999;
    int fcol = WorldXToFineCol(px);
    int frow = WorldYToFineRow(py);
    if (fcol == s_lastFineCol && frow == s_lastFineRow) return;
    s_lastFineCol = fcol;
    s_lastFineRow = frow;

    int col = WorldXToSegCol(px);
    int row = WorldYToSegRow(py);
    bool inBounds = (row >= 0 && row < WMX_SEG_ROWS && col >= 0 && col < WMX_SEG_COLS);
    uint8_t terrain = inBounds ? s_terrainGrid[row][col] : 0xEE;
    uint8_t region  = inBounds ? s_segmentRegionMap[row][col] : 0xEE;
    const char* tc = (terrain == SEG_OCEAN) ? "ocean" : (terrain == SEG_FOREST) ? "forest" : "land";
    uint8_t fcls = s_walkClassFine[frow][fcol];
    uint16_t fsteep = s_steepFine[frow][fcol];
    const char* fc = (fcls == SEG_OCEAN) ? "ocean" : (fcls == SEG_MOUNTAIN) ? "MOUNTAIN"
                   : (fcls == SEG_FOREST) ? "forest" : "land";
    // #67 BAT 2: would this cell block foot travel under the current slope
    // threshold? If the player is ever logged standing on a BLOCKED cell, the
    // threshold (WM_MTN_STEEP_BLOCK) is too low and is sealing walkable ground.
    bool fblocked = (fcls == SEG_MOUNTAIN && fsteep > WM_MTN_STEEP_BLOCK);
    uint8_t loco = GetLocomotionMode();
    Log::World("WorldMap: [WM-CALIB] pos(%d,%d) -> seg(col=%d,row=%d) terrain=%s region=0x%02X loco=%u | fine(col=%d,row=%d)=%s steep=%u%s",
               px, py, col, row, tc, region, loco, fcol, frow, fc, (unsigned)fsteep, fblocked ? " BLOCKED" : "");
}
#endif
