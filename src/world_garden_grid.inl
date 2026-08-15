// world_garden_grid.inl - Mobile Balamb Garden auto-drive (#80), PART 1: the
// traversability grid and its build.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included immediately BEFORE world_garden.inl, which holds the reachability
// flood, the planner, the berth/dock tables and the executor.
//
// v0.20.63: split out of world_garden.inl, which reached 81 KB against the
// 80 KB CI hard fail. The cut is at the end of Garden_BuildEnd: everything
// that turns wmx polygons into the grid lives here, everything that reads the
// grid lives next door. No code changed in the move.
// world_garden.inl - Mobile Balamb Garden auto-drive (#80)
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_map_drive_helpers.inl (uses SetDriveKeys /
// ReleaseAllDriveKeys / WriteMemDword32) and BEFORE world_map_drive.inl.
// The forward declarations consumed by earlier files (segments.inl's terrain
// loader, world_catalog.inl's catalog builder) live in world_map_state.inl.
//
// ============================================================================
// WHY THIS IS A SEPARATE SUBSYSTEM
// ============================================================================
// Everything in world_map_planner*.inl / world_map_routenet.inl /
// world_map_navmesh.inl models FOOT walkability: WorldFootBlockedAt() is
// literally `terr == 29 || (terr >= 32 && terr <= 34)`, the route network's
// 13 nodes are all land nodes, and open ocean returns WGH_NO_GROUND (which
// every consumer reads as "blocked"). A vehicle whose entire purpose is to
// cross the ocean cannot borrow any of it. Rather than thread a VehicleType
// through five files and risk regressing the BAT-proven foot drive, the
// Garden gets its own grid, its own planner and its own executor, gated on
// the engine's own vehicle id. When the id is not 0x30 not one line of this
// file runs.
//
// ============================================================================
// ENGINE FACTS (FF8_EN.exe, disassembled 2026-08-12 -- primary source)
// ============================================================================
// 0x53E6B0  IsPolyWalkable(uint32 typeTriple, int16 vehicleId)
//     typeTriple = poly[15]<<16 | poly[14]<<8 | poly[13]
//     if ([0xC75CF4] == 0) return 1;                  // collision disabled
//     veh 0x00..0x09, 0x80  -> (triple>>16) & 0x80    // FOOT
//     veh 0x20..0x28, 0x84  -> (triple>>16) & 0x40    // CAR
//     veh 0x30              -> (triple>>16) & 0x20    // *** GARDEN ***
//     veh 0x31              -> (triple>>16) & 0x10    // CHOCOBO
//     otherwise             -> 1                      // Ragnarok flies
//     The same test is inlined in the step validator 0x53E7A0 at 0x53E92E
//     (`shr eax,0x10 / and eax,0x20` under `cmp si,0x30`).
//
// 0x53E730  IsPolyParkable(uint32 typeTriple, int16 vehicleId)   [via 0x54B860]
//     veh 0x20..0x28, 0x84  -> (triple>>16) & 0x04
//     veh 0x30              -> (triple>>16) & 0x02    // *** GARDEN ***
//     veh 0x31              -> (triple>>16) & 0x01
//     veh 0x32              -> (triple>>8)  & 0x80    // Ragnarok LANDING
//
// STEP GATE: |candH - curH| >= 0xC8 (200) rejects the move. 0x53E7A0:0x53E9C2
//     and 0x54B860:0x54B87A. It is NOT vehicle-dependent.
//
// PER-VEHICLE PROBE DISTANCE (0x53E7A0:0x53E84D-0x53E87C): foot 0x20,
//     car/Garden 0x40, Ragnarok 0x100 -- the Garden moves at roughly twice
//     walking pace, which is why DRIVE constants below are scaled up.
//
// OFFLINE CORROBORATION (wmx.obj, 473,193 polygons). Per-terrain percentages
// of each mask, which independently confirm the bit assignment:
//     bit 0x40 ("car") is 0.0-1.1% on every FOREST terrain 0..5 -- cars cannot
//         enter forest, a rule the mod already had from live BATs.
//     bit 0x20 ("Garden") is 99.4% / 87.4% / 99.9% on the three OCEAN terrains
//         32/33/34 and 13.2% on terrain 29 (mountain) -- the Garden sails and
//         is stopped by mountains.
//     byte14 bit 0x80 ("Ragnarok landing") is 0% on every ocean, mountain and
//         forest terrain and 97-100% on the open plains -- exactly where an
//         airship may set down.
//     Every "park" bit is a near-perfect subset of foot-walkable (Garden
//         99.9%, car 100.0%, chocobo 100.0%, ragnarok 100.0%), which is what
//         "the player ends up standing here" requires.
//
// ============================================================================
// WHAT THIS GIVES THE PLAYER
// ============================================================================
// There is exactly ONE trigger program in wmsetus Section 8 with a Garden
// clause (program 20, locID 0x0172, region 0x0C). No destination in the
// catalog can be ENTERED by driving the Garden into it. So the Garden drive
// is park-and-walk by construction, not by preference: it routes the hull to
// the nearest cell that is (a) Garden-traversable, (b) Garden-parkable, and
// (c) on the same foot landmass as the destination, stops there, and hands
// the player back to the existing BAT-proven on-foot auto-drive.
// ============================================================================

// ----------------------------------------------------------------------------
// Grid geometry. 256 world units per cell, 1024 x 768 cells.
// ----------------------------------------------------------------------------
// 256 was chosen empirically, not by taste: the offline reachability analysis
// was run at 128 (the engine's own resolution for our purposes), 256 and 512.
// 128 and 256 produce an IDENTICAL reachable-destination set (23 of 39); 512
// loses Galbadia Garden, Galbadia Station and Chocobo Forest 5 by sealing the
// straits/passes that reach them. 256 is therefore the coarsest grid that is
// still faithful, and it costs 2.3 MB against 9.4 MB for a 128 grid.
static const int GD_CELL = 256;
static const int GD_COLS = 1024;                 // 262144 / 256
static const int GD_ROWS = 768;                  // 196608 / 256
// v0.20.57: the engine gate is |dH| >= 200 between the current polygon and the
// one ONE MOVE STEP away, and the Garden's move step is 0x40 = 64 units
// (0x53E7A0:0x53E84D). A 256-unit planner cell is FOUR such steps, so the
// faithful budget between adjacent cells is 4 x 200 = 800. Applying the raw
// 200 at cell scale is 4x too strict and manufactures a wall along every gentle
// slope. The v0.20.56 BAT wedged on exactly that: at the Centra continental
// shelf the per-cell steps were 0/107/166/85/48/62/32/17 -- every one legal --
// while the shelf as a whole descends 573 units, so the hull read open ocean as
// a wall and wall-followed the depth contour for 60 seconds. Measured over the
// whole map, the 200 rule blocks 2,955 cell edges and 3,990 cells; the faithful
// 800 blocks ZERO edges and one degenerate cell. Which is the real answer: the
// Garden's permission is the per-vehicle MASK (byte15 bit 0x20), not a slope
// rule -- the 200 gate is a cliff test a hovering hull never meets at sea.
//
// v0.20.61: THAT LAST SENTENCE IS WRONG AND THE .60 BAT PROVED IT. The hull
// drove out of Balamb and jammed against the cliff coastline of the Galbadia
// continent at (-5127,-5196); the F11 screenshots show it pressed into a
// two-tier cliff with the rail line along the top, and the log shows it holding
// UP for ninety seconds without moving. Those coastal polygons DO carry the
// Garden mask bit -- what stops the hull is the 200 gate, climbing 227 units
// from sea level in one step.
//
// The 800 derivation is right for a RAMP and wrong for a CLIFF. The engine
// tests |dH| >= 200 between the polygon under the hull and the polygon one
// 64-unit move step away. On a ramp that budget genuinely accumulates across a
// 256-unit cell, so 800 is correct. At a cliff the entire drop happens in ONE
// step however the grid is quantised -- and averaging four sub-heights into a
// single cell height smears the cliff out of existence, which is exactly how
// the planner came to route a course straight into a coastline.
//
// So the gate is applied where the cliff is still visible: between the 128-unit
// SUB-POINTS the rasterizer already samples, at the engine's own 200. A cell is
// traversable only if none of its four internal sub-pairs is a cliff, and an
// edge between two cells is open only if at least one of the two sub-point
// crossings is climbable (half an open boundary is enough for a hull).
//
// Both BATs are satisfied by this and neither is by the other two candidates:
// at the Centra shelf the sub-pairs are all well under 200 so the shelf stays
// open (the .56 wedge does not return), and at the Galbadia cliff they are
// 227/249/251 so the edge closes. Map-wide it shuts 14,401 of 5,466,034
// sub-edges -- 0.26%, which is what a real cliff set should look like.
static const int GD_STEP_GATE = 800;             // ramp budget, 4 move steps per 256u cell
static const int GD_CLIFF_GATE = 200;            // engine value, applied sub-point to sub-point

// GD_CLS bits
static const uint8_t GDC_WALK = 0x01;            // Garden may traverse  (byte15 & 0x20)
static const uint8_t GDC_PARK = 0x02;            // player may disembark (byte15 & 0x02)
static const uint8_t GDC_FOOT = 0x04;            // foot-walkable        (byte15 & 0x80)
static const uint8_t GDC_OPEN_E = 0x08;          // edge to (row, col+1) is climbable
static const uint8_t GDC_OPEN_S = 0x10;          // edge to (row+1, col) is climbable
// v0.20.73: THE GARDEN'S TERRAIN WHITELIST. FF8_EN.exe 0x53E3C1, inside the
// movement validator 0x53E2A0, and it applies to vehicle 0x30 ALONE:
//
//     cmp al, 0x22 / ja  +  cmp al, 0x1E / jae   ->  30 <= terrain <= 34 passes
//     otherwise: test [0x203EE88] and reject if positive
//
// Terrain 32/33/34 is water sitting at height 0; 6/7/9/29 is land at -200 to
// -1100. So the mask bit and the 200-unit step gate are NECESSARY BUT NOT
// SUFFICIENT: from the sea the hull may only enter water, and the only places
// allowed terrain actually CLIMBS are 4,921 cells map-wide -- 0.18% of the
// Garden's masked area. Those are the beaches Aaron has been describing since
// v0.20.67, and modelling them by height step instead of terrain class is what
// made ten builds route confidently at a shore the engine will never open.
//
// v0.20.84: AND THE TEST IS ON TERRAIN CLASS, NOT ON HEIGHT.
//
// [0x203EE88] is not a flag. 0x54B49F copies the active entity's world position
// into 0x203EE80/84/88 as (x, -z, y), so the value the validator tests is the
// GARDEN'S OWN ALTITUDE, and the branch reads "refuse land while you are below
// sea level". v0.20.73 modelled that as "allowed terrain whose MESH HEIGHT
// rises above sea level" and it is measurably not what the engine does. 296
// [GDTRACE] samples out of the v0.20.83 BAT, cross-referenced against the
// polygon under the hull:
//
//     terrain 32    46 samples   altitude -254 .. -58   ALWAYS <= 0
//     terrain 33     2 samples   altitude +208          always  > 0
//     terrain 34   127 samples   altitude +208 / +210   126 of 127 > 0
//
// The mesh height is 0 on all three. What decides the altitude is the terrain
// CLASS -- 32 is the shallow shelf the hull rides high over, 33/34 the deep
// ocean it sits down into. A beach IS the shelf, and the height test was a
// proxy that happened to coincide at the Tomb of the Unknown King and nowhere
// on Balamb's coast.
//
// The cost of that was exact, and Aaron drove into it: 1,285 cells passed the
// height rule and NOT ONE was on Balamb's shore, so the island the game starts
// you on was a one-way trap -- 1,974 cells the Garden could leave and never
// re-enter. That is why v0.20.83 hid Fire Cavern from the catalog and answered
// "plan FAILED ... ->(15232,-25216)" for Balamb Town. On terrain the flood is
// symmetric: 662,681 cells from Balamb and the identical 662,681 from the
// Tomb, no pocket in either direction.
static const uint8_t GDC_WATER = 0x20;           // terrain 30..34, enterable at sea level
static const uint8_t GDC_BEACH = 0x40;           // shelf terrain 30..32 -- the only way ashore
// v0.20.95: SOME BUT NOT ALL SUB-POINTS ARE GARDEN-MASKED.
//
// A 256 cell is WALK only when all four of its 128-unit sub-points carry the
// Garden bit, which is right for planning and wrong at exactly one place: the
// mouth of a beach. The .94 Shumi survey walked into it. Three runs converged
// to 670 units of the berth, pointed straight at it (berthOff 11, 16, 43),
// throttle down, sliding sideways -- and the cell the hull was standing on,
// planner (705,521), reads WALK=false:
//
//     sub(1410,1042) terr=29 h=  -7  NOT Garden-masked   <- 128 units of skirt
//     sub(1410,1043) terr= 9 h=-380  Garden
//     sub(1411,1042) terr=34 h=   0  Garden
//     sub(1411,1043) terr= 9 h=-355  Garden
//
// THE ENGINE HAD PUT THE HULL THERE. Everything beyond it -- (705,522),
// (705,523) and the berth itself -- is fully WALK terrain-9 at -390..-470. The
// whole beach was shut by ONE cell whose fourth quarter is a sliver of unmasked
// shore skirt, and it was MY grid shutting it: GdLineClear refuses every chord
// through a non-WALK cell, so the executor would not drive the last 670 units
// of a ramp the engine was already letting it stand on.
//
// This bit records the distinction so a beach approach can use it without
// loosening WALK anywhere else. See GdBeachOpen.
static const uint8_t GDC_PARTIAL = 0x80;         // 1..3 of 4 sub-points Garden-masked

static uint8_t* s_gdCls   = nullptr;             // GD_ROWS*GD_COLS
static int16_t* s_gdH     = nullptr;             // GD_ROWS*GD_COLS
static uint8_t* s_gdReach = nullptr;             // flood mask from the hull
static uint8_t* s_gdClear = nullptr;             // Chebyshev cells to nearest non-WALK
// v0.20.77: WHICH SUB-CELL, not just whether the cell has one.
//
// .74 required the disembark bit on all four 128-unit sub-points and told Aaron
// "there is nowhere here to leave the Garden" at Centra Ruins and Trabia Garden,
// where he then stepped off. .75 swung to ANY sub-point -- and at Shumi Village
// the hull parked in open water (terrain 33) and the mod announced a step-off at
// the CELL CENTRE, which is terrain 29, no disembark bit, no foot. One of the
// four sub-cells did qualify; the coordinate handed back was not it.
//
// So keep the sub-cell resolution instead of throwing it away at build time:
// 4 bits per cell marking the sub-points that are BOTH disembark-flagged and
// foot-walkable, so the answer to "can the player get off, and exactly where"
// is a real place rather than an average of four.
static uint8_t* s_gdParkSub = nullptr;           // bit s set => sub-cell s is a true step-off
static bool     s_gdLoaded = false;

// Build-time scratch (freed in Garden_BuildEnd). Each 256 cell is sampled at
// the four 128-unit sub-centres, so the cell is only declared traversable when
// the WHOLE cell is -- a coarse cell that is half cliff must not be planned
// through, because the executor's collision probe reads the real geometry.
static uint8_t* s_gdSubSeen = nullptr;           // bitmask of the 4 sub-points filled
static uint8_t* s_gdSubWalk = nullptr;           // bitmask of sub-points Garden-walkable
static uint8_t* s_gdSubPark = nullptr;
static uint8_t* s_gdSubFoot = nullptr;
static uint8_t* s_gdSubWater = nullptr;          // v0.20.73: terrain 30..34
static uint8_t* s_gdSubShelf = nullptr;          // v0.20.84: terrain 30..32 (the shallow shelf)
static int16_t* s_gdSubMinH = nullptr;
static int16_t* s_gdSubMaxH = nullptr;
// v0.20.61: the four sub-point heights themselves, not just their envelope --
// a cliff INSIDE a cell is invisible once they are averaged, and that is the
// defect the .60 BAT drove into.
static int16_t* s_gdSubH    = nullptr;           // 4 per cell, sub order s = (z<<1)|x

// ----------------------------------------------------------------------------
// Coordinate helpers. The Garden grid is indexed in MESH space, the same frame
// the wmx vertices are rasterized in, so no half-map centring constant is
// involved and the torus seam is a plain modulo.
//     mx = (gx + 0x60000) mod 0x40000        gx = wrap(mx - 0x60000)
//     mz = (0x48000 - gy) mod 0x30000        gy = wrap(0x48000 - mz)
// ----------------------------------------------------------------------------
static inline int GdCol(int32_t gx) { return (int)((((int64_t)gx + 0x60000) % 0x40000 + 0x40000) % 0x40000 / GD_CELL); }
static inline int GdRow(int32_t gy) { return (int)((((int64_t)0x48000 - gy) % 0x30000 + 0x30000) % 0x30000 / GD_CELL); }
// v0.20.77: the centre of one 128-unit SUB-cell, s = (z<<1)|x.
static inline void GdSubCellToWorld(int row, int col, int s, int32_t* gx, int32_t* gy)
{
    const int32_t mx = col * GD_CELL + ((s & 1) ? 192 : 64);
    const int32_t mz = row * GD_CELL + ((s & 2) ? 192 : 64);
    // v0.20.78: the double-modulo and the int64 widening are BOTH load-bearing.
    // Written without them this returned stepOff=(-251584,-75712) in the .77
    // BAT -- an X outside the 262144-wide world -- because C's % keeps the sign
    // of a negative dividend, so `(mx - 0x60000 + 0x20000) % 0x40000` goes
    // negative and the final -0x20000 drives it further out. GdCellToWorld
    // right above has always had this guard; the copy did not.
    *gx = (int32_t)((((int64_t)mx - 0x60000 + 0x20000) % 0x40000 + 0x40000) % 0x40000 - 0x20000);
    *gy = (int32_t)((((int64_t)0x48000 - mz + 0x18000) % 0x30000 + 0x30000) % 0x30000 - 0x18000);
}

static inline void GdCellToWorld(int row, int col, int32_t* gx, int32_t* gy)
{
    int32_t mx = col * GD_CELL + GD_CELL / 2;
    int32_t mz = row * GD_CELL + GD_CELL / 2;
    *gx = (int32_t)((((int64_t)mx - 0x60000 + 0x20000) % 0x40000 + 0x40000) % 0x40000 - 0x20000);
    *gy = (int32_t)((((int64_t)0x48000 - mz + 0x18000) % 0x30000 + 0x30000) % 0x30000 - 0x18000);
}
static inline int GdIdx(int row, int col) { return row * GD_COLS + col; }
static inline bool GdWalk(int row, int col)
{
    row = (row % GD_ROWS + GD_ROWS) % GD_ROWS;      // v0.20.69: torus in y too
    return s_gdLoaded && (s_gdCls[GdIdx(row, (col % GD_COLS + GD_COLS) % GD_COLS)] & GDC_WALK) != 0;
}

// v0.20.88: THE ONE CELL THE SHORE RULES MAY BE BYPASSED FOR.
//
// Set from the berth table when a beach_climb destination is chosen (Shumi
// Village, and nothing else today), cleared otherwise. -1 = no exception.
//
// Aaron: "You go up the beach like any other." The model disagrees at Shumi and
// the model is the thing that has been wrong there twice, but the beach rule as
// a whole is measured and it is what fixed Balamb island in .84 -- so it is
// bypassed for a single named goal cell rather than loosened map-wide. Both the
// water->land test and the 200-unit cliff gate are waived for entry INTO this
// cell; everything about getting to the water beside it is unchanged. If the
// engine refuses the climb, the drive fails at a known coordinate with the
// trace running, which is the survey.
static int s_gdBeachGoalIdx = -1;

// v0.20.95: is this cell inside the mouth of the armed beach approach?
//
// Deliberately tiny: only when a beach_climb berth is armed, only within three
// cells of it, and only for a cell that has SOME Garden-masked ground in it.
// Open water and solid rock are unaffected; so is every other berth on the map.
static const int GD_BEACH_MOUTH = 3;             // cells either side of the berth
static inline bool GdBeachOpen(int idx)
{
    if (s_gdBeachGoalIdx < 0 || !s_gdLoaded) return false;
    const int gr = s_gdBeachGoalIdx / GD_COLS, gc = s_gdBeachGoalIdx % GD_COLS;
    const int r = idx / GD_COLS, c = idx % GD_COLS;
    int dr = r - gr; if (dr < 0) dr = -dr;
    int dc = c - gc; if (dc < 0) dc = -dc;
    if (dc > GD_COLS / 2) dc = GD_COLS - dc;
    if (dr > GD_BEACH_MOUTH || dc > GD_BEACH_MOUTH) return false;
    return (s_gdCls[idx] & (GDC_WALK | GDC_PARTIAL)) != 0;
}

// v0.20.61 cliff gate. GdEdgeOpen answers for a 4-neighbour; GdStepOpen adds
// the diagonal, which is legal only if one of its two L-shaped routes is -- a
// hull cannot cut the corner of a headland the engine will not let it climb.
// v0.20.69: the world map is a torus in BOTH axes. wdist/TorusBearing have
// always wrapped in y; the GRID did not, so the last row's south edge was
// hard-wired shut and no route could go over a pole. Aaron: "make sure the
// route planner understands that you can go around the world -- so it may be
// shorter to go south from Centra Ruins to get to Shumi Village... you go down
// around the southern pole." Rows now wrap exactly like columns.
static inline bool GdEdgeOpen(int r, int c, int nr, int nc)
{
    r  = (r  % GD_ROWS + GD_ROWS) % GD_ROWS;
    nr = (nr % GD_ROWS + GD_ROWS) % GD_ROWS;
    c  = (c  % GD_COLS + GD_COLS) % GD_COLS;
    nc = (nc % GD_COLS + GD_COLS) % GD_COLS;
    if (nr == r) {
        if (nc == (c + 1) % GD_COLS)  return (s_gdCls[GdIdx(r, c)]  & GDC_OPEN_E) != 0;
        if (c  == (nc + 1) % GD_COLS) return (s_gdCls[GdIdx(r, nc)] & GDC_OPEN_E) != 0;
        return false;
    }
    if (nc == c) {
        if (nr == (r + 1) % GD_ROWS) return (s_gdCls[GdIdx(r,  c)] & GDC_OPEN_S) != 0;
        if (r  == (nr + 1) % GD_ROWS) return (s_gdCls[GdIdx(nr, c)] & GDC_OPEN_S) != 0;
    }
    return false;
}
// v0.20.73: THE WATER->LAND TRANSITION IS THE WHOLE GAME.
//
// Per FF8_EN.exe 0x53E3C1 the Garden may only enter terrain 30..34 while the
// condition at [0x203EE88] holds. Land-to-land and land-to-water are free --
// the hull drives overland perfectly well once ashore, which is why Deling City
// and Trabia Garden have always worked. What it cannot do is climb out of the
// sea anywhere except where allowed terrain rises above sea level: a BEACH.
// There are 4,921 such cells on the entire map, 0.18% of the Garden's masked
// area, so a planner that does not model this will never route through one by
// chance -- which is exactly what every build up to .72 did.

static inline bool GdTransitionOk(int fromIdx, int toIdx)
{
    if (toIdx == s_gdBeachGoalIdx) return true;          // v0.20.88
    const uint8_t f = s_gdCls[fromIdx], t = s_gdCls[toIdx];
    if (!(f & GDC_WATER)) return true;          // already ashore: unrestricted
    if (t & GDC_WATER)    return true;          // staying at sea
    return (f & GDC_BEACH) != 0;                // leaving the sea: beaches only
}

static inline bool GdStepOpen(int r, int c, int dr, int dc)
{
    r = (r % GD_ROWS + GD_ROWS) % GD_ROWS;
    c = (c % GD_COLS + GD_COLS) % GD_COLS;
    const int nr = ((r + dr) % GD_ROWS + GD_ROWS) % GD_ROWS;
    const int nc = ((c + dc) % GD_COLS + GD_COLS) % GD_COLS;
    if (!(s_gdCls[GdIdx(nr, nc)] & GDC_WALK) && !GdBeachOpen(GdIdx(nr, nc))) return false;
    if (!GdTransitionOk(GdIdx(r, c), GdIdx(nr, nc))) return false;
    if (GdBeachOpen(GdIdx(nr, nc))) return true;      // v0.20.95: the mouth is open
    // v0.20.88: the shore skirt below the Shumi beach is a strip of terrain-29
    // polygons carrying no masks at all, so the 128-unit samples either side of
    // it read a 295-unit step against a 200 gate. The engine climbs it. Waive
    // the cliff gate for entry into the beach goal cell alone.
    if (GdIdx(nr, nc) == s_gdBeachGoalIdx) return true;
    if (dr == 0 || dc == 0) return GdEdgeOpen(r, c, nr, nc);
    if ((s_gdCls[GdIdx(r, nc)] & GDC_WALK) &&
        GdTransitionOk(GdIdx(r, c), GdIdx(r, nc)) &&
        GdTransitionOk(GdIdx(r, nc), GdIdx(nr, nc)) &&
        GdEdgeOpen(r, c, r, nc) && GdEdgeOpen(r, nc, nr, nc)) return true;
    if ((s_gdCls[GdIdx(nr, c)] & GDC_WALK) &&
        GdTransitionOk(GdIdx(r, c), GdIdx(nr, c)) &&
        GdTransitionOk(GdIdx(nr, c), GdIdx(nr, nc)) &&
        GdEdgeOpen(r, c, nr, c) && GdEdgeOpen(nr, c, nr, nc)) return true;
    return false;
}

// ============================================================================
// Build. Driven from LoadTerrainGrid's polygon loop in world_map_segments.inl.
// ============================================================================
static void Garden_BuildBegin()
{
    s_gdLoaded = false;
    Garden_DumpBegin();                                 // v0.20.75 diagnostic
    const size_t n = (size_t)GD_ROWS * GD_COLS;
    if (!s_gdCls)   s_gdCls   = (uint8_t*)malloc(n);
    if (!s_gdH)     s_gdH     = (int16_t*)malloc(n * sizeof(int16_t));
    if (!s_gdReach) s_gdReach = (uint8_t*)malloc(n);
    if (!s_gdClear) s_gdClear = (uint8_t*)malloc(n);
    if (!s_gdParkSub) s_gdParkSub = (uint8_t*)malloc(n);
    s_gdSubSeen = (uint8_t*)malloc(n);
    s_gdSubWalk = (uint8_t*)malloc(n);
    s_gdSubPark = (uint8_t*)malloc(n);
    s_gdSubFoot = (uint8_t*)malloc(n);
    s_gdSubWater = (uint8_t*)malloc(n);
    s_gdSubShelf = (uint8_t*)malloc(n);
    s_gdSubMinH = (int16_t*)malloc(n * sizeof(int16_t));
    s_gdSubMaxH = (int16_t*)malloc(n * sizeof(int16_t));
    s_gdSubH    = (int16_t*)malloc(n * 4 * sizeof(int16_t));
    if (!s_gdCls || !s_gdH || !s_gdReach || !s_gdClear || !s_gdParkSub || !s_gdSubSeen ||
        !s_gdSubWalk || !s_gdSubPark || !s_gdSubFoot || !s_gdSubWater || !s_gdSubShelf ||
        !s_gdSubMinH ||
        !s_gdSubMaxH || !s_gdSubH) {
        Log::World("WorldMap: [GARDEN] allocation failed -- Garden drive disabled");
        return;
    }
    memset(s_gdCls, 0, n);
    memset(s_gdH, 0, n * sizeof(int16_t));
    memset(s_gdReach, 0, n);
    memset(s_gdClear, 0, n);
    memset(s_gdParkSub, 0, n);
    memset(s_gdSubSeen, 0, n);
    memset(s_gdSubWalk, 0, n);
    memset(s_gdSubPark, 0, n);
    memset(s_gdSubFoot, 0, n);
    memset(s_gdSubWater, 0, n);
    memset(s_gdSubShelf, 0, n);
    for (size_t i = 0; i < n; i++) { s_gdSubMinH[i] = 32767; s_gdSubMaxH[i] = -32768; }
    memset(s_gdSubH, 0, n * 4 * sizeof(int16_t));
}

// One wmx triangle, in MESH coordinates (mx, mz) with the vertex height h.
// b14/b15 are poly[14]/poly[15]. First-writer-wins per sub-point, matching the
// engine's first-containing-polygon selection.
static void Garden_RasterizeTri(int32_t ax, int32_t az, int16_t ah,
                                int32_t bx, int32_t bz, int16_t bh,
                                int32_t cx, int32_t cz, int16_t ch,
                                uint8_t b13, uint8_t b14, uint8_t b15)
{
    (void)b14;
    if (!s_gdSubSeen) return;
    const bool walk = (b15 & 0x20) != 0;
    const bool park = (b15 & 0x02) != 0;
    const bool foot = (b15 & 0x80) != 0;
    const bool watr = (b13 >= 30 && b13 <= 34);   // v0.20.73: the engine's whitelist
    const bool shlf = (b13 >= 30 && b13 <= 32);   // v0.20.84: the shallow shelf

    int32_t minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int32_t maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int32_t minz = az < bz ? (az < cz ? az : cz) : (bz < cz ? bz : cz);
    int32_t maxz = az > bz ? (az > cz ? az : cz) : (bz > cz ? bz : cz);

    int c0 = (int)(minx / GD_CELL), c1 = (int)(maxx / GD_CELL);
    int r0 = (int)(minz / GD_CELL), r1 = (int)(maxz / GD_CELL);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > GD_COLS - 1) c1 = GD_COLS - 1;
    if (r1 > GD_ROWS - 1) r1 = GD_ROWS - 1;
    if (c1 < c0 || r1 < r0) return;

    const int64_t den = (int64_t)(bz - cz) * (ax - cx) + (int64_t)(cx - bx) * (az - cz);
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            const int ci = GdIdx(r, c);
            for (int s = 0; s < 4; s++) {
                const uint8_t bit = (uint8_t)(1 << s);
                if (s_gdSubSeen[ci] & bit) continue;          // first poly wins
                const int32_t px = c * GD_CELL + ((s & 1) ? 192 : 64);
                const int32_t pz = r * GD_CELL + ((s & 2) ? 192 : 64);
                const int64_t d0 = (int64_t)(bx - ax) * (pz - az) - (int64_t)(bz - az) * (px - ax);
                const int64_t d1 = (int64_t)(cx - bx) * (pz - bz) - (int64_t)(cz - bz) * (px - bx);
                const int64_t d2 = (int64_t)(ax - cx) * (pz - cz) - (int64_t)(az - cz) * (px - cx);
                const bool neg = (d0 < 0) || (d1 < 0) || (d2 < 0);
                const bool pos = (d0 > 0) || (d1 > 0) || (d2 > 0);
                if (neg && pos) continue;                      // outside
                s_gdSubSeen[ci] |= bit;
                if (walk) s_gdSubWalk[ci] |= bit;
                if (park) s_gdSubPark[ci] |= bit;
                if (foot) s_gdSubFoot[ci] |= bit;
                if (watr) s_gdSubWater[ci] |= bit;
                if (shlf) s_gdSubShelf[ci] |= bit;
                int32_t h;
                if (den == 0) {
                    h = ((int32_t)ah + bh + ch) / 3;
                } else {
                    const double w0 = (double)((int64_t)(bz - cz) * (px - cx) + (int64_t)(cx - bx) * (pz - cz)) / (double)den;
                    const double w1 = (double)((int64_t)(cz - az) * (px - cx) + (int64_t)(ax - cx) * (pz - cz)) / (double)den;
                    h = (int32_t)(w0 * ah + w1 * bh + (1.0 - w0 - w1) * ch);
                }
                if (h < -32768) h = -32768;
                if (h >  32767) h =  32767;
                if (h < s_gdSubMinH[ci]) s_gdSubMinH[ci] = (int16_t)h;
                if (h > s_gdSubMaxH[ci]) s_gdSubMaxH[ci] = (int16_t)h;
                s_gdSubH[(size_t)ci * 4 + s] = (int16_t)h;
            }
        }
    }
}

// Call site for world_map_segments.inl's polygon loop. Kept here rather than
// inline there for two reasons: segments.inl sits at 79 KB against an 80 KB CI
// hard fail, and the coordinate reflection belongs with the grid that needs it.
//
// The loop hands us vertices already in the mod's game frame, where
// vwy = fileRow*2048 - lvy spans [0, 196608]. The Garden grid is indexed in MESH
// space, so mz = 196608 - vwy: a pure reflection, no wrap needed (the top row
// lands exactly on the boundary and the rasterizer clamps it). EVERY polygon is
// fed, ocean included -- the foot grids skip ocean, and crossing it is the
// Garden's whole purpose.
static void Garden_FeedPoly(const uint8_t* poly, const int32_t* vwx,
                            const int32_t* vwy, const int16_t* vwz, int vertCount)
{
    Garden_DumpPoly(poly, vwx, vwy, vwz, vertCount);   // v0.20.75 diagnostic
    const uint8_t i0 = poly[0], i1 = poly[1], i2 = poly[2];
    if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) return;
    Garden_RasterizeTri(vwx[i0], 196608 - vwy[i0], vwz[i0],
                        vwx[i1], 196608 - vwy[i1], vwz[i1],
                        vwx[i2], 196608 - vwy[i2], vwz[i2],
                        poly[0x0D], poly[0x0E], poly[0x0F]);
}

// #80 diagnostic: log the int32 reading of a savemap vehicle mirror next to the
// value the old six-int16 reading would have produced. On the Balamb continent
// they agree; anywhere with |X| > 32767 they will not, and that line is the
// confirmation the fix was needed. Transition-only, so it cannot spam.
static void Garden_LogVehPos(uintptr_t addr, const char* tag,
                             int32_t vx, int32_t vy, int32_t vz)
{
    static uintptr_t s_lastAddr = 0;
    static int32_t   s_lastX = INT32_MIN;
    if (addr == s_lastAddr && vx == s_lastX) return;
    s_lastAddr = addr; s_lastX = vx;
    const int16_t* w = (const int16_t*)addr;
    Log::World("WorldMap: [VEHPOS32] %s int32=(%d,%d,%d) rot=%d -- old int16 reading would have been (%d,%d)",
               tag, vx, vy, vz, (int)*(const int16_t*)(addr + WMS_VEHPOS_ROT_OFF),
               (int)w[0], (int)w[2]);
}

static void Garden_BuildEnd()
{
    Garden_DumpEnd();                                   // v0.20.75 diagnostic
    if (!s_gdSubSeen || !s_gdCls) return;
    const size_t n = (size_t)GD_ROWS * GD_COLS;
    int walkCells = 0, parkCells = 0;
    // Pass 1: per-cell class. The internal cliff test is what the .60 BAT was
    // missing -- a cell that straddles a 200-unit drop is not somewhere a hull
    // may be planned through, however flat its average looks.
    for (size_t i = 0; i < n; i++) {
        const bool full = (s_gdSubSeen[i] == 0x0F);
        const int spread = (int)s_gdSubMaxH[i] - (int)s_gdSubMinH[i];
        const int16_t* sh = s_gdSubH + i * 4;     // s = (z<<1)|x
        const bool flat =
            abs((int)sh[0] - (int)sh[1]) < GD_CLIFF_GATE &&    // north pair, west-east
            abs((int)sh[2] - (int)sh[3]) < GD_CLIFF_GATE &&    // south pair, west-east
            abs((int)sh[0] - (int)sh[2]) < GD_CLIFF_GATE &&    // west pair, north-south
            abs((int)sh[1] - (int)sh[3]) < GD_CLIFF_GATE;      // east pair, north-south
        uint8_t cls = 0;
        if (full && s_gdSubWalk[i] == 0x0F && spread < GD_STEP_GATE && flat) cls |= GDC_WALK;
        // v0.20.95: partial coverage -- passable to a beach approach, nothing else.
        if (s_gdSubWalk[i] != 0 && s_gdSubWalk[i] != 0x0F) cls |= GDC_PARTIAL;
        // v0.20.75: ANY sub-point, not all four.
        //
        // GDC_PARK demanded the disembark bit on all four 128-unit sub-points of
        // a 256 cell while GDC_FOOT has always accepted any one of them -- an
        // inconsistency that quietly made every coastal berth "unparkable". The
        // .74 BAT caught it red-handed: the hull parked at Centra Ruins and at
        // Trabia Garden, the mod announced "there is nowhere here to leave the
        // Garden" (canDisembark=0), and Aaron then stepped off at both. The
        // engine's own set-down search (0x53E7A0) tries FIVE candidate spots in a
        // fan and takes the first that passes, so a single qualifying sub-point
        // is the honest model of it. This is also the most likely reason Shumi
        // Village fell out of the reachable set.
        // v0.20.77: a sub-point is a true step-off only if it carries the
        // disembark bit AND is foot-walkable -- somewhere the player can stand.
        s_gdParkSub[i] = (uint8_t)(s_gdSubPark[i] & s_gdSubFoot[i]);
        if (s_gdParkSub[i] != 0) cls |= GDC_PARK;
        if (s_gdSubFoot[i] != 0)            cls |= GDC_FOOT;
        // v0.20.73: a cell counts as WATER only if every sampled sub-point is
        // allowed terrain -- the whitelist is per polygon, so a cell that is
        // half shore is not somewhere the hull may cross from the sea.
        if (s_gdSubSeen[i] && s_gdSubWater[i] == s_gdSubSeen[i]) cls |= GDC_WATER;
        s_gdCls[i] = cls;
        s_gdH[i] = full ? (int16_t)(((int)s_gdSubMinH[i] + (int)s_gdSubMaxH[i]) / 2) : 0;
        // v0.20.84: BEACH is the shallow shelf (terrain 30..32), not "water that
        // happens to sit above zero". See the GDC_BEACH note above for the 296
        // altitude samples this comes from. ANY shelf sub-point is enough: the
        // move step is 64 units and a sub-point is 128, so a quarter of a cell
        // is still a whole hull-length of shelf to be over when the step lands.
        if ((cls & GDC_WATER) && s_gdSubShelf[i] != 0) s_gdCls[i] |= GDC_BEACH;
        if (cls & GDC_WALK) walkCells++;
        if (cls & GDC_PARK) parkCells++;
    }
    // Pass 2: edge openness, which needs the neighbours' sub-heights and so
    // cannot be folded into pass 1. An edge is open when EITHER of its two
    // sub-point crossings is climbable -- a boundary half blocked by a headland
    // is still a boundary a hull can round.
    int openE = 0, openS = 0;
    for (int r = 0; r < GD_ROWS; r++) {
        for (int c = 0; c < GD_COLS; c++) {
            const int i = GdIdx(r, c);
            if (!(s_gdCls[i] & GDC_WALK)) continue;
            const int16_t* a = s_gdSubH + (size_t)i * 4;
            const int ie = GdIdx(r, (c + 1) % GD_COLS);
            if (s_gdCls[ie] & GDC_WALK) {
                const int16_t* b = s_gdSubH + (size_t)ie * 4;
                if (abs((int)a[1] - (int)b[0]) < GD_CLIFF_GATE ||
                    abs((int)a[3] - (int)b[2]) < GD_CLIFF_GATE) { s_gdCls[i] |= GDC_OPEN_E; openE++; }
            }
            {
                const int is = GdIdx((r + 1) % GD_ROWS, c);
                if (s_gdCls[is] & GDC_WALK) {
                    const int16_t* b = s_gdSubH + (size_t)is * 4;
                    if (abs((int)a[2] - (int)b[0]) < GD_CLIFF_GATE ||
                        abs((int)a[3] - (int)b[1]) < GD_CLIFF_GATE) { s_gdCls[i] |= GDC_OPEN_S; openS++; }
                }
            }
        }
    }
    Log::World("WorldMap: [GARDEN] cliff gate: %d east edges and %d south edges open", openE, openS);
    free(s_gdSubSeen); free(s_gdSubWalk); free(s_gdSubPark); free(s_gdSubFoot);
    free(s_gdSubWater);
    free(s_gdSubShelf);
    free(s_gdSubMinH); free(s_gdSubMaxH); free(s_gdSubH);
    s_gdSubSeen = s_gdSubWalk = s_gdSubPark = s_gdSubFoot = s_gdSubWater = nullptr;
    s_gdSubShelf = nullptr;
    s_gdSubMinH = s_gdSubMaxH = s_gdSubH = nullptr;

    // Clearance field: Chebyshev cell distance to the nearest non-traversable
    // cell, two-pass chamfer, capped at 32. The planner pays for running close
    // to land because a hull with a turning radius of roughly 1300 units
    // cannot follow a 256-unit-resolution coastline -- offline this single
    // term took the validation matrix from mostly-wedged to clean.
    //
    // v0.20.85: THE ROWS WRAP HERE TOO, AND UNTIL NOW THEY DID NOT.
    //
    // v0.20.69 made the world a torus in both axes -- edges, flood, heuristic,
    // A* and the line probe all take rows mod GD_ROWS -- and missed this one
    // loop, which kept treating row 0 and row GD_ROWS-1 as solid wall. The
    // v0.20.84 BAT replay caught it: twelve samples on the polar crossing from
    // Winhill to Trabia Garden where the game said clearance 27, 22, 17, 12, 6,
    // 2, 0, 3, 11, 18, 26 and the offline model said 32 every time. Those
    // numbers ARE the row index -- the hull was being told its distance to the
    // array boundary over completely open ocean.
    //
    // The cost was silent and exactly wrong: below GD_TIGHT_CLEAR the executor
    // switches to short bow probes and the planner adds GD_TIGHT_PENALTY per
    // cell, so a 32-cell-wide stretch of empty sea at the pole was priced and
    // driven as a narrow channel -- on the one route where going over the pole
    // is the whole point.
    //
    // A chamfer converges in two passes only on an open grid; on a torus the
    // seam has to be re-propagated, so the pair repeats until nothing changes.
    // It settles in three or four iterations and runs once, at load.
    for (size_t i = 0; i < n; i++) s_gdClear[i] = (s_gdCls[i] & GDC_WALK) ? 32 : 0;
    for (int sweep = 0; sweep < 8; sweep++) {
        bool changed = false;
        for (int r = 0; r < GD_ROWS; r++) {
            for (int c = 0; c < GD_COLS; c++) {
                uint8_t& v = s_gdClear[GdIdx(r, c)];
                if (!v) continue;
                int best = v;
                for (int dr = -1; dr <= 0; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc >= 0) continue;
                        const int nr = (r + dr + GD_ROWS) % GD_ROWS;
                        const int nc = (c + dc + GD_COLS) % GD_COLS;
                        const int cand = s_gdClear[GdIdx(nr, nc)] + 1;
                        if (cand < best) best = cand;
                    }
                }
                if (best < v) { v = (uint8_t)best; changed = true; }
            }
        }
        for (int r = GD_ROWS - 1; r >= 0; r--) {
            for (int c = GD_COLS - 1; c >= 0; c--) {
                uint8_t& v = s_gdClear[GdIdx(r, c)];
                if (!v) continue;
                int best = v;
                for (int dr = 0; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc <= 0) continue;
                        const int nr = (r + dr) % GD_ROWS;
                        const int nc = (c + dc + GD_COLS) % GD_COLS;
                        const int cand = s_gdClear[GdIdx(nr, nc)] + 1;
                        if (cand < best) best = cand;
                    }
                }
                if (best < v) { v = (uint8_t)best; changed = true; }
            }
        }
        if (!changed) break;
    }
    s_gdLoaded = true;
    Log::World("WorldMap: [GARDEN] grid built: %d/%d traversable cells (%.1f%%), %d parkable",
               walkCells, (int)n, 100.0 * walkCells / (double)n, parkCells);
}
