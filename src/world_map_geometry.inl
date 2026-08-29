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
// segments. BOTH axes carry a half-extent centering offset: per wmx.obj
// analysis, game_X = seg_col*8192 + 4096 - 131072 and game_Y = seg_row*8192 +
// 4096 - 98304. The +131072 (half width = 16*8192) and +98304 (half height =
// 12*8192) below are the inverses of those offsets.
//
// #67 (2026-06-20): the Y offset was MISSING here, and that was the all-
// continent navigation bug. The prior comment claimed "Y axis aligns naturally
// because the torus wrap absorbs any constant offset" -- that is WRONG. A
// constant offset is NOT absorbed by the wrap when binning into discrete
// cells; it shifts which cell every coordinate lands in. Without +98304,
// world-Y=0 mapped to the north edge instead of the vertical centre, so every
// western / Galbadia coordinate landed on an ocean cell and on-foot BFS reached
// nothing (Galbadia catalog came up empty). Proven 3 ways: 9/26 -> 26/26
// catalog locations land on land; region IDs cluster per-continent; the live
// player position maps OCEAN -> LAND. X (+131072) was the v0.11.16 fix; Y was
// simply overlooked, and Balamb kept working only because catalog coords and
// the live position shared the same (wrong) mapping. See `Plan & Research
// Documents/World Map Reachability Rework - offline wmx analysis findings.md`.
static int WorldXToSegCol(int32_t x)
{
    int32_t shifted = x + 131072;
    int32_t nx = ((shifted % 262144) + 262144) % 262144;
    return (nx / 8192) % WMX_SEG_COLS;
}

static int WorldYToSegRow(int32_t y)
{
    int32_t shifted = y + 98304;   // #67: half-height centering, mirrors the X +131072
    int32_t ny = ((shifted % 196608) + 196608) % 196608;
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
    *outY = (int32_t)(row * 8192 + 4096) - 98304;   // #67: mirror WorldYToSegRow's +98304
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

// Foot and Selphie-foot. A foot claim is the one locomotion value that never
// needs corroborating, so world_map_locomotion.inl asks this first.
static inline bool IsFootLocomotion(uint8_t mode) { return mode == 0 || mode == 6; }

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

// ============================================================================
// #67: Fine-grid coordinate mapping + continuous-flood-fill reachability
// ============================================================================
// The fine grid is 256x192 cells of 1024 world units, centred the same way as
// the segment grid (+131072 X, +98304 Y) so a player coordinate and the
// rasterized wmx geometry share one frame. The grid arrays (s_walkClassFine,
// s_reachFine) and the WM_FINE_* constants live in world_map_state.inl (game)
// or the harness scaffolding (test). This replaces the 32x24 segment BFS for
// catalog reachability: continents that the coarse grid bridged across straits
// (or split at coastal cells) resolve correctly here. See the findings doc.
static int WorldXToFineCol(int32_t x)
{
    int32_t shifted = x + 131072;
    int32_t nx = ((shifted % 262144) + 262144) % 262144;
    return (nx / WM_FINE_CELL) % WM_FINE_COLS;
}

static int WorldYToFineRow(int32_t y)
{
    int32_t shifted = y + 98304;
    int32_t ny = ((shifted % 196608) + 196608) % 196608;
    return (ny / WM_FINE_CELL) % WM_FINE_ROWS;
}

// Inverse of WorldXToFineCol / WorldYToFineRow: the world coordinate of a fine
// cell's centre. The shifted-space centre is col*1024 + 512; subtract the
// half-map centering (131072 X, 98304 Y) to return to world space. The torus
// wrap is handled by the bearing math downstream, so one representative is fine.
// #67 stage 2: the AD planner now steers toward fine-cell centres (1024-unit)
// instead of segment centres (8192-unit), so its route hugs walkable ground.
static void FineCellCenterToWorld(int col, int row, int32_t* x, int32_t* y)
{
    *x = col * WM_FINE_CELL + WM_FINE_CELL / 2 - 131072;
    *y = row * WM_FINE_CELL + WM_FINE_CELL / 2 - 98304;
}

// True iff a fine cell of the given class + steepness is passable for the
// vehicle. #67 BAT 2 (slope-aware): a MOUNTAIN cell steeper than
// WM_MTN_STEEP_BLOCK is an impassable face for foot/chocobo/car; gentler
// mountain cells are passes/plateaus and stay walkable. The slope gate is kept
// to the MOUNTAIN class on purpose -- a type-agnostic slope block would wrongly
// seal steep roads/bridges (type 28 road, type 12 bridge both have steep
// individual polys). Garden/Ragnarok cross anything (hover/fly).
static bool IsFineTraversable(uint8_t cls, uint16_t steep, VehicleType veh)
{
    if (veh == VEH_GARDEN || veh == VEH_RAGNAROK) return true;
    if (cls == SEG_OCEAN) return false;
    if (cls == SEG_FOREST && veh == VEH_CAR) return false;   // cars can't enter forest
    if (cls == SEG_MOUNTAIN && steep > WM_MTN_STEEP_BLOCK) return false;  // steep face
    return true;   // LAND, gentle MOUNTAIN, and (foot/chocobo) FOREST
}

// True iff the straight (Bresenham) line of fine cells from (r0,c0) to (r1,c1)
// is entirely traversable for the vehicle. The AD drive's line-of-sight
// lookahead uses this so steering never aims across a blocked (ocean / steep-
// mountain) cell -- the corner-cut that would walk the player into a cliff.
// Non-wrapped: the drive only probes a few cells ahead, well inside a continent.
static bool FineLineWalkable(int r0, int c0, int r1, int c1, VehicleType veh)
{
    int dr = abs(r1 - r0), dc = abs(c1 - c0);
    int sr = (r1 > r0) ? 1 : -1, sc = (c1 > c0) ? 1 : -1;
    int err = dc - dr, r = r0, c = c0;
    for (;;) {
        if (r < 0 || r >= WM_FINE_ROWS || c < 0 || c >= WM_FINE_COLS) return false;
        if (!IsFineTraversable(s_walkClassFine[r][c], s_steepFine[r][c], veh)) return false;
        if (r == r1 && c == c1) break;
        int e2 = 2 * err;
        if (e2 > -dr) { err -= dr; c += sc; }
        if (e2 <  dc) { err += dc; r += sr; }
    }
    return true;
}

// 4-connected continuous flood-fill from the player's fine cell over passable
// cells, torus-wrapped on both axes. Fills s_reachFine. If the seed cell is
// not passable (player on a coastal cell whose centre fell in an ocean poly),
// snap to the nearest passable cell within SNAP_RADIUS before filling.
static void ComputeReachabilityFine(int startCol, int startRow, VehicleType veh)
{
    memset(s_reachFine, 0, sizeof(s_reachFine));
    if (startRow < 0 || startRow >= WM_FINE_ROWS ||
        startCol < 0 || startCol >= WM_FINE_COLS) return;

    if (!IsFineTraversable(s_walkClassFine[startRow][startCol],
                           s_steepFine[startRow][startCol], veh)) {
        const int SNAP_RADIUS = 4;
        bool snapped = false;
        for (int rad = 1; rad <= SNAP_RADIUS && !snapped; rad++) {
            for (int dr = -rad; dr <= rad && !snapped; dr++) {
                for (int dc = -rad; dc <= rad && !snapped; dc++) {
                    int nr = (((startRow + dr) % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
                    int nc = (((startCol + dc) % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
                    if (IsFineTraversable(s_walkClassFine[nr][nc],
                                          s_steepFine[nr][nc], veh)) {
                        startRow = nr; startCol = nc; snapped = true;
                    }
                }
            }
        }
        if (!snapped) return;   // nothing passable nearby
    }

    static int qIdx[WM_FINE_COLS * WM_FINE_ROWS];
    int qHead = 0, qTail = 0;
    s_reachFine[startRow][startCol] = 1;
    qIdx[qTail++] = startRow * WM_FINE_COLS + startCol;

    const int dc4[] = { 0, 0, -1, 1 };
    const int dr4[] = { -1, 1, 0, 0 };
    while (qHead < qTail) {
        int cur = qIdx[qHead++];
        int cr = cur / WM_FINE_COLS;
        int cc = cur % WM_FINE_COLS;
        for (int d = 0; d < 4; d++) {
            int nr = (((cr + dr4[d]) % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
            int nc = (((cc + dc4[d]) % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
            if (!s_reachFine[nr][nc] &&
                IsFineTraversable(s_walkClassFine[nr][nc], s_steepFine[nr][nc], veh)) {
                s_reachFine[nr][nc] = 1;
                if (qTail < WM_FINE_COLS * WM_FINE_ROWS)
                    qIdx[qTail++] = nr * WM_FINE_COLS + nc;
            }
        }
    }
}

// True iff the location at (x,y) is reachable: its fine cell or any of its 8
// neighbours is in the flood-filled set (1-cell tolerance for coastal/edge
// coordinates that sit just off the walkable centre).
static bool IsFineCellReachable(int32_t x, int32_t y)
{
    int col = WorldXToFineCol(x);
    int row = WorldYToFineRow(y);
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int nr = (((row + dr) % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
            int nc = (((col + dc) % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
            if (s_reachFine[nr][nc]) return true;
        }
    }
    return false;
}
