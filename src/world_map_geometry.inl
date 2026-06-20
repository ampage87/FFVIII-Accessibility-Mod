// world_map_geometry.inl - Pure coordinate / segment / reachability math
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone in the
// game build. Included immediately AFTER world_map_state.inl (which declares
// the types, constants, and statics referenced here) and BEFORE
// world_map_segments.inl / world_map_catalog.inl, which rely on these
// functions.
//
// WHY THIS FILE EXISTS (v0.18.3.52): these functions are the load-bearing
// core of world-map navigation -- the engine-coord -> segment-grid mapping,
// the per-vehicle traversability rule, and the BFS reachability flood-fill.
// Issue #67 (all-continent navigation) changes exactly this math, so it is
// carved out here -- free of Win32, SEH, absolute-memory reads, and live
// game state -- so a host compiler (g++ on the CI runner) can compile and
// exercise it directly. tests/world_map_harness.cpp includes THIS file and
// asserts the Balamb-continent on-foot reachable set never regresses. The
// extraction is a pure move: every function below was previously defined
// verbatim in world_map_segments.inl (the coord/vehicle math) or
// world_map_catalog.inl (ComputeReachability), with no behavior change.
//
// Dependencies provided by the includer (state.inl in the game build, or the
// harness scaffolding in the test): the VehicleType / SegTerrainClass enums,
// the WMX_* and WM_WIDTH/WM_HEIGHT constants, the s_terrainGrid / s_reachable
// grids, and a Log::World(...) declaration.

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
// Coordinate conversion: game world coords -> segment grid (v0.14.85)
// ============================================================================
// World map torus is 262144 x 196608, divided into a 32 x 24 grid of 8192-unit
// segments. The X axis has a non-zero origin offset: per wmx.obj analysis,
// game_X = seg_col * 8192 + 4096 - 131072. The +131072 below is the inverse
// of that offset -- without it, BFS starts in an ocean cell and filters
// everything as unreachable. This was the v0.11.16 fix that finally made
// terrain BFS work end-to-end ("Driving worked as expected!" -- Aaron's BAT).
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
static void SegmentCenterToWorld(int col, int row, int32_t* outX, int32_t* outY)
{
    *outX = (int32_t)(col * 8192 + 4096) - 131072;
    *outY = (int32_t)(row * 8192 + 4096);
}

// ============================================================================
// Auto-drive bearing (v0.14.86)
// ============================================================================
// Bearing in native FF8 heading units (0-4095, 0=North, CW). Wrap-aware on
// the world torus.
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

static int GetBfsRuleClass(VehicleType v)
{
    if (v == VEH_RAGNAROK) return 2;
    if (v == VEH_GARDEN)   return 1;
    return 0;  // VEH_ON_FOOT, VEH_CHOCOBO, VEH_CAR all share land-only rules
}

static bool IsCanonicalLocomotion(uint8_t mode)
{
    return mode == 0 || mode == 3 || mode == 6 ||
           mode == 31 ||
           (mode >= 32 && mode <= 40) ||
           mode == 48 || mode == 50;
}

// True iff the segment is reachable for the given vehicle. 3-state terrain
// classifier (LAND/FOREST/OCEAN). Foot/Chocobo can cross forest; cars cannot.
// Garden/Ragnarok can cross any terrain.
static bool IsSegmentTraversable(int row, int col, VehicleType veh)
{
    if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) return false;
    uint8_t cell = s_terrainGrid[row][col];   // SegTerrainClass: 0=land, 1=forest, 2=ocean
    if (veh == VEH_GARDEN || veh == VEH_RAGNAROK) return true;          // any segment
    if (cell == SEG_OCEAN) return false;                                 // ocean blocks all non-Garden/Ragnarok
    if (cell == SEG_FOREST && veh == VEH_CAR) return false;              // cars can't enter forest
    return true;                                                          // foot/chocobo on land or forest; car on land
}

// ============================================================================
// ComputeReachability -- BFS flood-fill from player segment, populates
// s_reachable[][] for the given vehicle's traversal rules. 4-connected with
// torus wrapping.
// ============================================================================
static void ComputeReachability(int startCol, int startRow, VehicleType veh)
{
    memset(s_reachable, 0, sizeof(s_reachable));

    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) return;

    // Player's current cell is always reachable.
    s_reachable[startRow][startCol] = 1;

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
