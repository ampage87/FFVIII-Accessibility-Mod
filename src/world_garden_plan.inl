// world_garden_plan.inl - Mobile Balamb Garden auto-drive (#80), PART 2a:
// reachability, the dock table, the aboard latch and the A* planner.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_garden_berths.inl and BEFORE world_garden.inl.
//
// v0.20.84 SPLIT: world_garden.inl reached 86,261 bytes against the 80 KB CI
// hard fail -- it was already 84,567 at v0.20.83, so that build could not have
// been pushed. DEVNOTES says to split BEFORE the edit that needs the room
// rather than shave comments to fit, so the cut is taken here, at the end of
// Garden_Plan: everything that decides WHERE TO GO lives in this file,
// everything that DRIVES lives in world_garden.inl. No code changed in the
// move; both garden harnesses reproduce the same grid totals, the same
// 24 ok / 0 bad and the same waypoint counts.
//
// The design rationale, the engine facts and the BAT history are in
// world_garden_grid.inl's header -- read that first.


// ============================================================================
// Reachability flood from the hull's current cell.
// ============================================================================
static int  s_gdReachRow = -1, s_gdReachCol = -1;

// v0.20.69: torus deltas. The map wraps in BOTH axes, so "how far apart are
// these two cells" is the shorter way round on each axis, not the array
// difference. Used by the A* heuristic and both endpoint-proximity tests.
static inline int GdRowDelta(int a, int b)
{
    int d = a - b; if (d < 0) d = -d;
    return (d > GD_ROWS / 2) ? (GD_ROWS - d) : d;
}
static inline int GdColDelta(int a, int b)
{
    int d = a - b; if (d < 0) d = -d;
    return (d > GD_COLS / 2) ? (GD_COLS - d) : d;
}

static bool GdSnapWalk(int* row, int* col, int radius)
{
    if (GdWalk(*row, *col)) return true;
    for (int k = 1; k <= radius; k++) {
        for (int dr = -k; dr <= k; dr++) {
            for (int dc = -k; dc <= k; dc++) {
                if (dr != -k && dr != k && dc != -k && dc != k) continue;
                const int r = ((*row + dr) % GD_ROWS + GD_ROWS) % GD_ROWS;   // v0.20.69 torus
                const int c = ((*col + dc) % GD_COLS + GD_COLS) % GD_COLS;
                if (GdWalk(r, c)) { *row = r; *col = c; return true; }
            }
        }
    }
    return false;
}

static void Garden_ComputeReach(int32_t px, int32_t py)
{
    if (!s_gdLoaded) return;
    int r = GdRow(py), c = GdCol(px);
    if (!GdSnapWalk(&r, &c, 24)) {
        memset(s_gdReach, 0, (size_t)GD_ROWS * GD_COLS);
        s_gdReachRow = s_gdReachCol = -1;
        Log::World("WorldMap: [GARDEN] hull at (%d,%d) is not on a traversable cell", px, py);
        return;
    }
    if (r == s_gdReachRow && c == s_gdReachCol) return;    // already flooded from here
    memset(s_gdReach, 0, (size_t)GD_ROWS * GD_COLS);
    static std::vector<int> q;
    q.clear();
    q.reserve(1 << 16);
    s_gdReach[GdIdx(r, c)] = 1;
    q.push_back(GdIdx(r, c));
    const int dR[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    const int dC[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    for (size_t head = 0; head < q.size(); head++) {
        const int cur = q[head];
        const int cr = cur / GD_COLS, cc = cur % GD_COLS;
        const int hh = s_gdH[cur];
        for (int d = 0; d < 8; d++) {
            const int nr = ((cr + dR[d]) % GD_ROWS + GD_ROWS) % GD_ROWS;   // v0.20.69 torus
            const int nc = (cc + dC[d] + GD_COLS) % GD_COLS;
            const int ni = GdIdx(nr, nc);
            if (s_gdReach[ni] || !(s_gdCls[ni] & GDC_WALK)) continue;
            if (abs((int)s_gdH[ni] - hh) >= GD_STEP_GATE) continue;
            if (!GdStepOpen(cr, cc, dR[d], dC[d])) continue;      // v0.20.61 cliff gate
            s_gdReach[ni] = 1;
            q.push_back(ni);
        }
    }
    s_gdReachRow = r; s_gdReachCol = c;
    Log::World("WorldMap: [GARDEN] reachability from cell (%d,%d): %d cells", r, c, (int)q.size());
}

// v0.20.89: the ONLY correct way to ask whether a berth can be reached.
//
// The exception a beach_climb berth needs is a property of THAT BERTH, not a
// mode the mod happens to be in. v0.20.88 armed it in Garden_StartDrive and
// nowhere else, so Garden_BuildCatalog -- which runs when you board, long
// before any destination is chosen -- asked about Shumi Village with the
// exception clear, got "unreachable", and hid it. Aaron: "Did a quick test just
// now and Shumi did not appear in the catalog."
//
// Arm, ask, restore. Callers cannot get it wrong by forgetting.
static bool Garden_BerthReachable(const GardenPark* gp)
{
    if (!gp || !gp->reachable) return false;
    const int saved = s_gdBeachGoalIdx;
    s_gdBeachGoalIdx = gp->beach_climb
                     ? GdIdx(GdRow(gp->park_y), GdCol(gp->park_x)) : -1;
    const bool ok = Garden_CellReachable(gp->park_x, gp->park_y);
    s_gdBeachGoalIdx = saved;
    return ok;
}

static bool Garden_CellReachable(int32_t gx, int32_t gy)
{
    if (!s_gdLoaded) return false;
    if (s_gdReachRow < 0) {
        // Never answer "unreachable" out of a map that was simply never built
        // -- that is exactly what made every destination announce "not
        // reachable from here" in the v0.20.54 BAT. Build it on demand.
        int32_t px = 0, py = 0, pz = 0;
        GetWorldMapPosition_Active(&px, &py, &pz);
        if (px == 0 && py == 0) return false;
        Garden_ComputeReach(px, py);
        if (s_gdReachRow < 0) return false;
    }
    const int r = GdRow(gy), c = GdCol(gx);
    // v0.20.84: THE BERTH'S OWN CELL, not "something near it".
    //
    // This answered yes whenever ANY of the nine cells around the berth was in
    // the flood, and the planner then had to reach the berth ITSELF. The .83
    // BAT shows what that costs: from the Tomb, Balamb Town's berth was outside
    // the flood but the ocean beside it was inside, so the catalog offered
    // Balamb Town, Aaron chose it, and the log says
    //
    //   [GARDEN] plan FAILED (-44090,-37564)->(15232,-25216) after 655108 expansions
    //
    // -- an exhaustive search of the whole component, four times over, ending in
    // nothing. The catalog must promise exactly what the planner can deliver.
    // The 3x3 widening survives only for a berth cell the Garden may not occupy
    // at all, where the planner snaps to a neighbour anyway.
    if (s_gdReach[GdIdx(r, c)]) return true;
    // v0.20.88: a beach_climb berth sits one cell inland of water the hull can
    // reach, across a shore the model refuses and the engine does not. Its own
    // cell will never be in the flood, so the flood is asked about the water
    // beside it instead. This is deliberately narrow -- see Garden_Plan, where
    // the same exception is made for the goal cell only.
    if (GdIdx(r, c) == s_gdBeachGoalIdx) {
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                const int nr = ((r + dr) % GD_ROWS + GD_ROWS) % GD_ROWS;
                const int nc = ((c + dc) % GD_COLS + GD_COLS) % GD_COLS;
                if (s_gdReach[GdIdx(nr, nc)]) return true;
            }
        return false;
    }
    if (s_gdCls[GdIdx(r, c)] & GDC_WALK) return false;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            const int nr = ((r + dr) % GD_ROWS + GD_ROWS) % GD_ROWS;   // v0.20.69 torus
            const int nc = ((c + dc) % GD_COLS + GD_COLS) % GD_COLS;
            if (s_gdReach[GdIdx(nr, nc)]) return true;
        }
    }
    return false;
}

// ============================================================================

// ----------------------------------------------------------------------------
// v0.20.62: every place a drive_in destination can actually be TOUCHED.
//
// Two BATs were spent betting on one site each, so stop betting. Take FH's foot
// landmass (the label containing the marker -- 1,842 cells, its own component,
// not Esthar's) and list every Garden-water cell in the world that is directly
// adjacent to it with no wall cell between. There are exactly EIGHT, in two
// clusters, and nothing else on the map touches FH at all:
//
//   the wall gap   (45632,576) (45632,704) (45760,320)   3.6-4.0 km from the
//                  marker, on the FAR side of the railroad bridge
//   the south tip  (48832,-7360) ... (49472,-8128)       5.7-6.5 km, on the
//                  Balamb side
//
// v0.20.61 pressed at (46850,-2970), which is in NEITHER cluster -- it is a
// terrain-29 cliff face, which is exactly what Aaron's screenshots show the
// hull grinding against. v0.20.60 picked the gap correctly and never reached
// it, because the cliff bug wedged the hull 50 km short; so the gap has never
// actually been tried. It goes first. The 120 km route to it is about seventy
// seconds at the hull's real speed (the .60 log clocks 30 km in 17 s), which
// makes the "detour" the .61 notes rejected it for a non-argument.
//
// If a site's nose-in sweep comes up empty the run moves to the next one rather
// than reporting failure, so one BAT settles the question instead of one site
// per BAT.
// struct GardenDock moved to world_garden_inlets.inl at v0.53.0 -- that file is
// included first and needs it for the Centra inlet sweep.
// v0.20.64: the ONE place the engine has a Garden field-entry clause.
// Program 20 (locID 0x0172, top vehicle GARDEN, story 636..3899) gates on
// region 0x0C, and Trabia Garden is the only catalog destination whose segment
// carries that region. The machinery below is kept and wired for it, but NOT
// enabled: Trabia Garden's berth already puts the hull on top of the place with
// zero units of walking, so park-and-walk works today and there is no reason to
// risk a working destination on an unproven drive-in. The new
// [GARDEN] trigger state log line reports whether program 20 is even live at
// the player's story point -- answer that first.
static const GardenDock s_gardenDocks[] = {
    { "Trabia Garden",  48832,  -57920,  48893,  -57979 },
};
static const int GARDEN_DOCK_COUNT = (int)(sizeof(s_gardenDocks) / sizeof(s_gardenDocks[0]));

// The n'th dock site for this destination, or null when there are no more.
static const GardenDock* Garden_DockSite(const char* name, int n)
{
    if (!name || n < 0) return nullptr;
    // v0.53.0 (#109): the White SeeD Ship has no known coordinate, so its
    // "dock sites" are the forty-one Centra inlets in world_garden_inlets.inl,
    // nearest to Edea's House first. The nose-in exhaustion path already walks
    // this list -- "another place the hull can touch? go there rather than
    // reporting failure" -- so the sweep is the machinery that is already here,
    // pointed at a list instead of at a guess.
    if (strcmp(name, "White SeeD Ship") == 0)
        return (n < WHITE_SEED_INLET_COUNT) ? &s_whiteSeedInlets[n] : nullptr;
    for (int i = 0; i < GARDEN_DOCK_COUNT; i++) {
        if (strcmp(s_gardenDocks[i].name, name) != 0) continue;
        if (n == 0) return &s_gardenDocks[i];
        n--;
    }
    return nullptr;
}

// ============================================================================
// Is the player currently piloting the mobile Garden?
// ============================================================================
// v0.20.55, from the v0.20.54 BAT (2026-08-12 13:08-13:12): the engine vehicle
// id at 0x020409E0 is NOT a "riding" register -- it is a "vehicle currently in
// MOTION" register. It read 0 at every world-map entry and at every catalog
// build in that session, and read 48 exactly once, four seconds into an active
// drive (13:08:51 [VEHID]), then fell back to 0 while the hull sat parked. The
// .54 build asked it the question at the two moments it is always 0, which is
// why the catalog never flipped, why every destination announced "not
// reachable", and why the post-park backslash started a FOOT drive on a player
// who was still aboard.
//
// So the id is used as a one-way CONFIRMATION, latched, and the latch is
// cleared only on positive evidence of being on foot. That evidence comes from
// the savemap mirrors, which the same BAT proved are fresh at a world-map
// transition: at 13:10:34 (on foot, just disembarked) |P - char_pos| = 0 and
// |P - bgu_pos| = 1015. Piloting should read the mirror image. The test is
// deliberately three-way -- clear / set / LEAVE ALONE -- so an ambiguous entry
// never flips the latch in either direction; the id corrects it the moment the
// hull moves.
static bool s_gdAboard       = false;
static bool s_gdEntryPending = false;
// v0.20.56: the hull's own position, tracked while we KNOW we are it. See
// Garden_UpdateAboard for why this exists.
static int32_t s_gdHullX = 0, s_gdHullY = 0;
static bool    s_gdHullKnown = false;
static DWORD   s_gdOffSince  = 0;

static const double GD_DISEMBARK_DIST = 450.0;   // the hull's own footprint is bigger than this
static const DWORD  GD_DISEMBARK_MS   = 700;

static void Garden_OnWorldMapEntry() { s_gdEntryPending = true; }

static bool GdReadMirror(uintptr_t base, int32_t* x, int32_t* y)
{
    return WmSafeReadBytes(base + WMS_VEHPOS_X_OFF, x, 4) &&
           WmSafeReadBytes(base + WMS_VEHPOS_Y_OFF, y, 4);
}

static bool Garden_IsAboard() { return s_gdLoaded && s_gdAboard; }

// ============================================================================
// Planner: 8-neighbour A* on the Garden grid, clearance-weighted.
// ============================================================================
static const int    GD_CLEAR_TARGET  = 7;        // 7 cells = 1792 units of sea room
static const double GD_LAND_PENALTY  = 2.5;    // v0.20.68: cost multiplier for foot-walkable (land) cells
static const double GD_CLEAR_PENALTY = 0.55;
static const int    GD_TIGHT_CLEAR_PLAN = 2;     // v0.20.81: cells the executor cannot thread
static const double GD_TIGHT_PENALTY    = 4.0;     // extra cost per missing cell
static const int    GD_PLAN_EXPAND_CAP = 1500000;   // v0.20.73: the terrain whitelist makes the graph much harder -- the hull must find one of only 4,921 beaches -- and 400k left Trabia and Centra unplannable

struct GdNode { double f; int idx; };
struct GdNodeCmp { bool operator()(const GdNode& a, const GdNode& b) const { return a.f > b.f; } };

// v0.20.67: SEA-FIRST. The .66 BAT's Centra route was 66 of 67 waypoints
// overland, and the hull stalled at the water's edge because getting UP onto
// that land needs a step the 200 gate forbids. Land is now excluded from the
// search outright, with a permissive retry if no sea route exists at all.
//
// A cost multiplier was tried first and rejected on measurement: at x12 it
// wrecks A*'s heuristic, the search degenerates toward Dijkstra, and a single
// plan took minutes offline -- which would be a frame-rate hazard in the game,
// not just slow in the harness. A hard mask keeps every edge cost at 1.0, so
// the search stays exactly as fast as it was, and it states the constraint
// exactly rather than approximately.
static bool Garden_Plan(int32_t sx, int32_t sy, int32_t tx, int32_t ty, int clearTarget = GD_CLEAR_TARGET)
{
    s_drivePathLen = 0; s_drivePathIdx = 0;
    s_drivePathPlanned = false; s_drivePathWorld = false;
    if (!s_gdLoaded) return false;

    int sr = GdRow(sy), sc = GdCol(sx);
    int gr = GdRow(ty), gc = GdCol(tx);
    if (!GdSnapWalk(&sr, &sc, 24)) return false;
    if (!GdSnapWalk(&gr, &gc, 24)) return false;
    // v0.20.84: refuse an unreachable goal in O(1) instead of proving it in
    // 655,108 expansions. The flood is already built for the hull's position,
    // and A* on an unreachable goal cannot terminate early -- it has to close
    // the entire component before it can say no. That is the four-second stall
    // the .83 BAT hit three times running on Balamb Town. When the flood is not
    // current for this start, fall through and let A* answer.
    const bool beachGoal = (GdIdx(gr, gc) == s_gdBeachGoalIdx);
    if (!beachGoal && s_gdReachRow >= 0 && s_gdReach[GdIdx(sr, sc)] && !s_gdReach[GdIdx(gr, gc)]) {
        Log::World("WorldMap: [GARDEN] plan refused (%d,%d)->(%d,%d): goal cell (%d,%d) "
                   "is not in the hull's reachable set", sx, sy, tx, ty, gr, gc);
        return false;
    }

    static std::vector<float>  gScore;
    static std::vector<int>    came;
    static std::vector<uint8_t> closed;
    const size_t n = (size_t)GD_ROWS * GD_COLS;
    gScore.assign(n, 1e30f);
    came.assign(n, -1);
    closed.assign(n, 0);

    const int startIdx = GdIdx(sr, sc), goalIdx = GdIdx(gr, gc);
    // Column distance is wrap-aware; row is not (the map does not wrap N/S in
    // any playable way, and the poles are ocean nobody routes through).
    auto heur = [&](int idx) -> double {
        const int r = idx / GD_COLS, c = idx % GD_COLS;
        int dr = GdRowDelta(r, gr);      // v0.20.69: wraps over the poles
        int dc = GdColDelta(c, gc);
        const int mn = dr < dc ? dr : dc;
        return (dr + dc) + (1.4142135 - 2.0) * mn;
    };
    std::priority_queue<GdNode, std::vector<GdNode>, GdNodeCmp> open;
    gScore[startIdx] = 0.0f;
    open.push({ heur(startIdx), startIdx });

    const int dR[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    const int dC[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    const double dW[8] = { 1, 1, 1, 1, 1.4142135, 1.4142135, 1.4142135, 1.4142135 };
    int expanded = 0;
    bool found = false;
    while (!open.empty()) {
        const GdNode cur = open.top(); open.pop();
        if (closed[cur.idx]) continue;
        closed[cur.idx] = 1;
        if (cur.idx == goalIdx) { found = true; break; }
        if (++expanded > GD_PLAN_EXPAND_CAP) break;
        const int cr = cur.idx / GD_COLS, cc = cur.idx % GD_COLS;
        // Clearance is not charged within 10 cells of either endpoint: the
        // park point is on a coast by definition, and a hull that started in a
        // tight inlet must be allowed to leave it.
        // v0.20.69: endpoint proximity measured on the torus, not on the array.
        const bool nearEnd =
            (GdRowDelta(cr, gr) <= 10 && GdColDelta(cc, gc) <= 10) ||
            (GdRowDelta(cr, sr) <= 10 && GdColDelta(cc, sc) <= 10);
        for (int d = 0; d < 8; d++) {
            const int nr = ((cr + dR[d]) % GD_ROWS + GD_ROWS) % GD_ROWS;   // v0.20.69 torus
            const int nc = (cc + dC[d] + GD_COLS) % GD_COLS;
            const int ni = GdIdx(nr, nc);
            if (closed[ni] || !(s_gdCls[ni] & GDC_WALK)) continue;
            if (abs((int)s_gdH[ni] - (int)s_gdH[cur.idx]) >= GD_STEP_GATE) continue;
            if (!GdStepOpen(cr, cc, dR[d], dC[d])) continue;      // v0.20.61 cliff gate
            double w = dW[d];
            if (!nearEnd) {
                const int miss = clearTarget - (int)s_gdClear[ni];
                double pen = (miss > 0) ? GD_CLEAR_PENALTY * miss : 0.0;
                // v0.20.81: A SURCHARGE ON CORRIDORS THE EXECUTOR CANNOT DRIVE.
                //
                // The .79 Tomb route had 30 of 340 waypoints at clearance <= 2,
                // and its three longest tight stretches -- (-31360,-55424),
                // (-50304,-37504), (-45184,-35456) -- are exactly where the game
                // logged its wall-follows. A quarter of that drive was spent
                // inside the guard, which is a local heuristic with a 256-unit
                // horizon being asked to thread a one-cell channel.
                //
                // Aaron: "It does not have to take the most direct route, it is
                // perfectly fine to take a bit longer to take a cleaner / more
                // open path." A flat surcharge of 4 on clearance <= 2 drops the
                // tight waypoints from 30 to 1 and costs 108 km -> 134 km;
                // larger values buy nothing measurable. The one survivor is the
                // destination itself -- the Tomb sits on the TIP OF A PENINSULA,
                // narrow by construction, which is what the nearGoal exemption
                // above is for.
                //
                // Measured on the replay-conforming model: the Tomb goes from
                // wedged at 3,884 units out to ARRIVED at the berth (gd ~285)
                // from all four starts.
                // Scoped to WATER: every wall-follow in the .79 Tomb log was a
                // narrow SEA channel being coast-followed. Fire Cavern's route
                // is 16 of 41 waypoints tight and is a short INLAND run that
                // drives fine today -- the hull has no alternative there, so
                // charging for it risks a regression for no gain.
                if ((int)s_gdClear[ni] <= GD_TIGHT_CLEAR_PLAN) pen += GD_TIGHT_PENALTY;
                if (pen > 0.0) w *= (1.0 + pen);
            }
            // v0.20.68: land is BIASED AGAINST, not banned.
            //
            // Aaron: "in order to get up on land masses you must pilot the
            // Garden up a beach coastline... once you go up the beach you pilot
            // the Garden overland to get close to the location." Counting them
            // confirms it: there are only 8,565 cells on the whole map where a
            // Garden-navigable sea cell meets Garden-navigable land with a step
            // under the 200 gate. Everywhere else the coast is a 200-400 unit
            // escarpment and the engine refuses the climb.
            //
            // So land must be reachable, and 20 of the 26 berths are land cells.
            // v0.20.67 banned land beyond six cells of an endpoint, and the
            // beach Centra Ruins needs is 4,574 units out -- it could not get
            // ashore at all, which is why that build still failed. A x12
            // multiplier wrecks A*'s heuristic. x2.5 keeps the search healthy
            // and still prefers open water for the crossing itself.
            const bool nearEndLand =
                (GdRowDelta(cr, gr) <= 6 && GdColDelta(cc, gc) <= 6) ||
                (GdRowDelta(cr, sr) <= 6 && GdColDelta(cc, sc) <= 6);
            if (!nearEndLand && (s_gdCls[ni] & GDC_FOOT)) w *= GD_LAND_PENALTY;
            const float ng = (float)(gScore[cur.idx] + w);
            if (ng < gScore[ni]) {
                gScore[ni] = ng;
                came[ni] = cur.idx;
                open.push({ ng + heur(ni), ni });
            }
        }
    }
    if (!found) {
        Log::World("WorldMap: [GARDEN] plan FAILED (%d,%d)->(%d,%d) after %d expansions",
                   sx, sy, tx, ty, expanded);
        return false;
    }
    static std::vector<int> rev;
    rev.clear();
    for (int i = goalIdx; i >= 0; i = came[i]) {
        rev.push_back(i);
        if (i == startIdx) break;
    }
    const int total = (int)rev.size();
    int stride = 1;
    while (total / stride > DRIVE_PATH_MAX - 2) stride++;
    int outN = 0;
    for (int i = total - 1; i >= 0 && outN < DRIVE_PATH_MAX - 1; i -= stride) {
        int32_t wx, wy;
        GdCellToWorld(rev[i] / GD_COLS, rev[i] % GD_COLS, &wx, &wy);
        s_drivePathWX[outN] = wx;
        s_drivePathWY[outN] = wy;
        s_drivePath[outN] = PackSeg(rev[i] / GD_COLS % 256, rev[i] % GD_COLS % 256);
        outN++;
    }
    // Always finish on the exact park coordinate, not the cell centre.
    s_drivePathWX[outN] = tx; s_drivePathWY[outN] = ty;
    s_drivePath[outN] = PackSeg(gr % 256, gc % 256);
    outN++;
    s_drivePathLen = outN;
    s_drivePathIdx = 0;
    s_drivePathWorld = true;
    s_drivePathPlanned = true;
    Log::World("WorldMap: [GARDEN] planned %d waypoints (%d cells, stride %d, %d expansions)",
               outN, total, stride, expanded);
    return true;
}
