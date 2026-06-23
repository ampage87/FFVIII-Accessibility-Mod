// world_map_planner.inl - A* path planner + planner-eligibility (v0.16.0)
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// A* over the 32x24 segment grid with multi-target goal sets derived from
// s_triggerPrograms[] clauses. Reads s_segmentRegionMap (segments.inl),
// s_triggerPrograms (trigger_data.inl), s_terrainGrid (segments.inl).
//
// v0.16.0 addition: ComputePlannerEligibility() walks s_triggerPrograms[]
// at module init and marks each catalog destination as planner-eligible iff
// at least one foot-vehicle clause exists for that destination's region.
// Destinations without a foot clause are geometric-trigger destinations
// (entered via terrain-29 polygon trigger, not wmsetus script event) --
// they must use simple-coord steering, NOT the planner's closest-active-
// region fallback (which misroutes drives toward unrelated destinations).

// ============================================================================
// v0.14.103.7: IsLocationFootFriendly
// ============================================================================
// Returns TRUE if the given catalog (X, Y) is in a region where any Section
// 8 trigger program admits foot entry. The engine auto-dismounts cars onto
// foot triggers, so foot-friendly regions accept cars; non-foot-friendly
// regions (vehicle-only landing pads like region 0x0C = mobile B-Garden,
// vehicle=Garden only) reject cars permanently.
//
// Algorithm:
//   1. Map (X, Y) to (col, row) via the WorldXToSegCol / WorldYToSegRow
//      helpers.
//   2. Read the region byte from s_segmentRegionMap[row][col]. 0xFF means
//      "no region" (deep ocean cells without trigger zones); return false.
//   3. Walk s_triggerPrograms[]. A program contributes a "foot clause for
//      this region" when:
//        - Its top_vehicle is ANY (0x00), FOOT (0x80), or FOOT_ALT (0x84),
//          AND
//        - It has at least one clause whose region matches AND whose
//          vehicle is ANY, FOOT, or FOOT_ALT.
//      Return TRUE on first match; FALSE if all 38 programs are exhausted
//      without one.
//
// Defensive fallback: if s_segmentRegionLoaded is false (Section 2 didn't
// load), return TRUE so we keep the existing v0.14.103.6 behavior (longer
// retry threshold) rather than firing bounce-arrived prematurely. Same
// fallback for out-of-range segments.
static bool IsLocationFootFriendly(int32_t catX, int32_t catY)
{
    if (!s_segmentRegionLoaded) return true;   // graceful fallback

    int col = WorldXToSegCol(catX);
    int row = WorldYToSegRow(catY);
    if (col < 0 || col >= WMX_SEG_COLS ||
        row < 0 || row >= WMX_SEG_ROWS) return true;

    uint8_t destRegion = s_segmentRegionMap[row][col];
    if (destRegion == 0xFF) return false;      // no region → no trigger → not foot-friendly

    auto isFootClass = [](uint16_t v) {
        return v == TRIG_VEH_ANY || v == TRIG_VEH_FOOT || v == TRIG_VEH_FOOT_ALT;
    };

    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        const TriggerProgram& p = s_triggerPrograms[i];
        if (!isFootClass(p.top_vehicle)) continue;
        if (p.num_clauses == 0 || p.clauses == nullptr) continue;
        for (uint8_t k = 0; k < p.num_clauses; k++) {
            const TriggerClause& c = p.clauses[k];
            if (c.region == destRegion && isFootClass(c.vehicle)) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Path planner (v0.14.94)
// ============================================================================

// Story window predicate. A clause's [story_gte, story_lt) gates pass when
// the live story value is in that half-open range. gte=0 = no lower bound;
// lt=0 = +infinity.
static bool StoryWindowMatches(uint16_t gte, uint16_t lt, uint16_t story)
{
    bool lowerOk = (gte == 0) || (story >= gte);
    bool upperOk = (lt  == 0) || (story <  lt);
    return lowerOk && upperOk;
}

// Vehicle predicate. Cars are treated as foot for clause matching (cars
// auto-dismount onto foot triggers).
static bool VehicleClauseMatches(uint16_t clauseVeh, VehicleType playerVeh)
{
    if (clauseVeh == TRIG_VEH_ANY) return true;
    if (clauseVeh == TRIG_VEH_FOOT || clauseVeh == TRIG_VEH_FOOT_ALT)
        return playerVeh == VEH_ON_FOOT || playerVeh == VEH_CAR;
    if (clauseVeh == TRIG_VEH_CHOCOBO)  return playerVeh == VEH_CHOCOBO;
    if (clauseVeh == TRIG_VEH_GARDEN)   return playerVeh == VEH_GARDEN;
    if (clauseVeh == TRIG_VEH_RAGNAROK) return playerVeh == VEH_RAGNAROK;
    return false;
}

static bool ClauseMatches(const TriggerClause& c, VehicleType veh, uint16_t story)
{
    return VehicleClauseMatches(c.vehicle, veh) &&
           StoryWindowMatches(c.story_gte, c.story_lt, story);
}

// Forward decl for MatchProgramForCatalog's distance-cap logic.
static int WrapManhattan(int r1, int c1, int r2, int c2);

// v0.14.95 closest-active-region search; v0.14.97 PLAN-DEBUG tracing.
static int MatchProgramForCatalog(int32_t catX, int32_t catY,
                                  VehicleType veh, uint16_t story,
                                  uint8_t* outRegion)
{
    if (!s_segmentRegionLoaded) return -1;
    int catCol = WorldXToSegCol(catX);
    int catRow = WorldYToSegRow(catY);
    if (catCol < 0 || catCol >= WMX_SEG_COLS ||
        catRow < 0 || catRow >= WMX_SEG_ROWS) return -1;

    static uint8_t activeRegions[64];
    static int     activeProgIdx[64];
    static bool    activeIsClean[64];
    int activeCount = 0;

    auto addActive = [&](uint8_t region, int progIdx, bool isClean) {
        for (int j = 0; j < activeCount; j++) {
            if (activeRegions[j] == region) {
                if (isClean && !activeIsClean[j]) {
                    activeProgIdx[j] = progIdx;
                    activeIsClean[j] = true;
                }
                return;
            }
        }
        if (activeCount < (int)(sizeof(activeRegions)/sizeof(activeRegions[0]))) {
            activeRegions[activeCount] = region;
            activeProgIdx[activeCount] = progIdx;
            activeIsClean[activeCount] = isClean;
            activeCount++;
        }
    };

    int catRegByte = (catRow >= 0 && catRow < WMX_SEG_ROWS &&
                      catCol >= 0 && catCol < WMX_SEG_COLS)
                     ? s_segmentRegionMap[catRow][catCol] : 0xFF;
    Log::World("WorldMap: [PLAN-DEBUG] Walking %d programs for veh=%d story=%u catalog=(%d,%d) seg(%d,%d) catRegion=0x%02X",
               TRIGGER_PROGRAM_COUNT, (int)veh, (unsigned)story, catX, catY, catCol, catRow,
               (unsigned)catRegByte);

    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        const TriggerProgram& p = s_triggerPrograms[i];

        bool topVehOK = (p.top_vehicle == TRIG_VEH_ANY) ||
                        VehicleClauseMatches(p.top_vehicle, veh);
        if (!topVehOK) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X SKIP top_vehicle=0x%02X mismatch (player veh=%d)",
                       i, (unsigned)p.loc_id, (unsigned)p.top_vehicle, (int)veh);
            continue;
        }

        if (!StoryWindowMatches(p.top_story_gte, p.top_story_lt, story)) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X SKIP top_story=[%u,%u) story=%u out of window",
                       i, (unsigned)p.loc_id,
                       (unsigned)p.top_story_gte, (unsigned)p.top_story_lt,
                       (unsigned)story);
            continue;
        }

        if (p.num_clauses == 0 || p.clauses == nullptr) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X SKIP no clauses (top-level only)",
                       i, (unsigned)p.loc_id);
            continue;
        }

        int clausesPassed = 0;
        for (uint8_t k = 0; k < p.num_clauses; k++) {
            const TriggerClause& c = p.clauses[k];
            bool vehOK   = VehicleClauseMatches(c.vehicle, veh);
            bool storyOK = StoryWindowMatches(c.story_gte, c.story_lt, story);
            const char* reason;
            if (vehOK && storyOK) {
                reason = "PASS";
                addActive(c.region, i, c.unk_flags == 0);
                clausesPassed++;
            } else if (!vehOK && !storyOK) {
                reason = "FAIL veh+story";
            } else if (!vehOK) {
                reason = "FAIL veh";
            } else {
                reason = "FAIL story";
            }
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X clause %u: v=0x%02X r=0x%02X s=[%u,%u) unk=0x%04X => %s",
                       i, (unsigned)p.loc_id, (unsigned)k,
                       (unsigned)c.vehicle, (unsigned)c.region,
                       (unsigned)c.story_gte, (unsigned)c.story_lt,
                       (unsigned)c.unk_flags, reason);
        }
        if (clausesPassed == 0) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X => 0 clauses passed; nothing added",
                       i, (unsigned)p.loc_id);
        }
    }

    {
        char regBuf[256];
        int pos = 0;
        regBuf[0] = '\0';
        for (int j = 0; j < activeCount && pos < (int)sizeof(regBuf) - 8; j++) {
            int n = snprintf(regBuf + pos, sizeof(regBuf) - pos, "%s0x%02X",
                             j == 0 ? "" : ",", (unsigned)activeRegions[j]);
            if (n < 0) break;
            pos += n;
        }
        Log::World("WorldMap: [PLAN-DEBUG] Active region set after walk (%d): {%s}",
                   activeCount, regBuf);
    }

    if (activeCount == 0) {
        Log::World("WorldMap: [PLAN] No active regions for veh=%d story=%u \u2014 fallback",
                   (int)veh, (unsigned)story);
        return -1;
    }

    static const int SEGMENT_DISTANCE_CAP = 5;
    int bestDist = SEGMENT_DISTANCE_CAP + 1;
    int bestRow = -1, bestCol = -1;
    int bestActiveIdx = -1;
    for (int row = 0; row < WMX_SEG_ROWS; row++) {
        for (int col = 0; col < WMX_SEG_COLS; col++) {
            uint8_t r = s_segmentRegionMap[row][col];
            if (r == 0xFF) continue;
            int activeIdx = -1;
            for (int j = 0; j < activeCount; j++) {
                if (activeRegions[j] == r) { activeIdx = j; break; }
            }
            if (activeIdx < 0) continue;
            int d = WrapManhattan(row, col, catRow, catCol);
            if (d < bestDist) {
                bestDist  = d;
                bestRow   = row;
                bestCol   = col;
                bestActiveIdx = activeIdx;
            }
        }
    }

    if (bestActiveIdx < 0) {
        Log::World("WorldMap: [PLAN] %d active regions but none within %d segs of catalog (%d,%d) seg(%d,%d) catRegion=0x%02X \u2014 fallback",
                   activeCount, SEGMENT_DISTANCE_CAP, catX, catY, catCol, catRow,
                   (unsigned)s_segmentRegionMap[catRow][catCol]);
        return -1;
    }

    *outRegion = activeRegions[bestActiveIdx];
    int progIdx = activeProgIdx[bestActiveIdx];
    Log::World("WorldMap: [PLAN] Catalog (%d,%d) seg(%d,%d) \u2192 closest active region 0x%02X at seg(%d,%d) segDist=%d (program [%02d] locID=0x%04X, %s, %d active regions for veh=%d story=%u)",
               catX, catY, catCol, catRow,
               (unsigned)*outRegion, bestCol, bestRow, bestDist,
               progIdx, (unsigned)s_triggerPrograms[progIdx].loc_id,
               activeIsClean[bestActiveIdx] ? "clean" : "UNK-flagged",
               activeCount, (int)veh, (unsigned)story);
    return progIdx;
}

static int CollectGoalSegments(uint8_t region)
{
    int count = 0;
    for (int row = 0; row < WMX_SEG_ROWS && count < DRIVE_PATH_MAX; row++) {
        for (int col = 0; col < WMX_SEG_COLS && count < DRIVE_PATH_MAX; col++) {
            if (s_segmentRegionMap[row][col] == region) {
                s_driveGoalSegs[count++] = PackSeg(row, col);
            }
        }
    }
    return count;
}

static bool IsGoalSegment(int row, int col)
{
    uint16_t target = PackSeg(row, col);
    for (int i = 0; i < s_driveGoalSegCount; i++) {
        if (s_driveGoalSegs[i] == target) return true;
    }
    return false;
}

static int WrapManhattan(int r1, int c1, int r2, int c2)
{
    int dr = abs(r1 - r2);
    int dc = abs(c1 - c2);
    if (dr > WMX_SEG_ROWS / 2) dr = WMX_SEG_ROWS - dr;
    if (dc > WMX_SEG_COLS / 2) dc = WMX_SEG_COLS - dc;
    return dr + dc;
}

static int HeuristicToGoals(int row, int col)
{
    int best = 9999;
    for (int i = 0; i < s_driveGoalSegCount; i++) {
        int gr = UnpackRow(s_driveGoalSegs[i]);
        int gc = UnpackCol(s_driveGoalSegs[i]);
        int h = WrapManhattan(row, col, gr, gc);
        if (h < best) best = h;
    }
    return best;
}

// A* over the 32x24 segment grid. 4-neighbor edges with torus wrap.
static bool PlanPath(int startCol, int startRow, VehicleType veh)
{
    s_drivePathLen     = 0;
    s_drivePathIdx     = 0;
    s_drivePathPlanned = false;

    if (s_driveGoalSegCount == 0) {
        Log::World("WorldMap: [PLAN] No goal segments \u2014 planner cannot run");
        return false;
    }
    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) {
        Log::World("WorldMap: [PLAN] Start segment (%d,%d) out of range", startCol, startRow);
        return false;
    }

    static const uint16_t INF_COST = 0xFFFF;
    static uint16_t gScore [WMX_SEG_ROWS][WMX_SEG_COLS];
    static uint16_t cameRow[WMX_SEG_ROWS][WMX_SEG_COLS];
    static uint16_t cameCol[WMX_SEG_ROWS][WMX_SEG_COLS];
    static uint8_t  closed [WMX_SEG_ROWS][WMX_SEG_COLS];
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        for (int c = 0; c < WMX_SEG_COLS; c++) {
            gScore[r][c]  = INF_COST;
            cameRow[r][c] = 0xFFFF;
            cameCol[r][c] = 0xFFFF;
            closed[r][c]  = 0;
        }
    }

    static uint32_t heap[4 * WMX_PLAYABLE_SEGS];
    int heapSize = 0;

    auto heapPush = [&](int f, int row, int col) {
        if (heapSize >= (int)(sizeof(heap)/sizeof(heap[0]))) return;
        uint32_t entry = ((uint32_t)f << 16) | (uint32_t)PackSeg(row, col);
        heap[heapSize] = entry;
        int i = heapSize++;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (heap[parent] <= heap[i]) break;
            uint32_t tmp = heap[parent]; heap[parent] = heap[i]; heap[i] = tmp;
            i = parent;
        }
    };
    auto heapPop = [&](int* outF, int* outRow, int* outCol) -> bool {
        if (heapSize == 0) return false;
        uint32_t top = heap[0];
        *outF   = (int)(top >> 16);
        uint16_t packed = (uint16_t)(top & 0xFFFF);
        *outRow = UnpackRow(packed);
        *outCol = UnpackCol(packed);
        heap[0] = heap[--heapSize];
        int i = 0;
        for (;;) {
            int l = 2*i + 1, r = 2*i + 2, smallest = i;
            if (l < heapSize && heap[l] < heap[smallest]) smallest = l;
            if (r < heapSize && heap[r] < heap[smallest]) smallest = r;
            if (smallest == i) break;
            uint32_t tmp = heap[i]; heap[i] = heap[smallest]; heap[smallest] = tmp;
            i = smallest;
        }
        return true;
    };

    gScore[startRow][startCol] = 0;
    heapPush(HeuristicToGoals(startRow, startCol), startRow, startCol);

    int goalRow = -1, goalCol = -1;
    int popsExpanded = 0;
    while (heapSize > 0) {
        int f, row, col;
        if (!heapPop(&f, &row, &col)) break;
        if (closed[row][col]) continue;
        closed[row][col] = 1;
        popsExpanded++;

        if (IsGoalSegment(row, col)) {
            goalRow = row;
            goalCol = col;
            break;
        }

        const int dx[] = { 0, 0, -1, 1 };
        const int dy[] = { -1, 1, 0, 0 };
        for (int d = 0; d < 4; d++) {
            int nr = (row + dy[d] + WMX_SEG_ROWS) % WMX_SEG_ROWS;
            int nc = (col + dx[d] + WMX_SEG_COLS) % WMX_SEG_COLS;
            if (closed[nr][nc]) continue;
            if (!IsSegmentTraversable(nr, nc, veh)) continue;
            int tentative = gScore[row][col] + 1;
            if (tentative < gScore[nr][nc]) {
                gScore[nr][nc]  = (uint16_t)tentative;
                cameRow[nr][nc] = (uint16_t)row;
                cameCol[nr][nc] = (uint16_t)col;
                int h = HeuristicToGoals(nr, nc);
                heapPush(tentative + h, nr, nc);
            }
        }
    }

    if (goalRow < 0) {
        Log::World("WorldMap: [PLAN] No path from seg(%d,%d) to any of %d goal cells (expanded %d nodes, veh=%d)",
                   startCol, startRow, s_driveGoalSegCount, popsExpanded, (int)veh);
        return false;
    }

    static uint16_t reverseBuf[DRIVE_PATH_MAX];
    int rcount = 0;
    int cr = goalRow, cc = goalCol;
    while ((cr != startRow || cc != startCol) && rcount < DRIVE_PATH_MAX) {
        reverseBuf[rcount++] = PackSeg(cr, cc);
        uint16_t pr = cameRow[cr][cc];
        uint16_t pc = cameCol[cr][cc];
        if (pr == 0xFFFF || pc == 0xFFFF) break;
        cr = pr;
        cc = pc;
    }
    for (int i = 0; i < rcount; i++) {
        s_drivePath[i] = reverseBuf[rcount - 1 - i];
    }
    s_drivePathLen     = rcount;
    s_drivePathIdx     = 0;
    s_drivePathPlanned = true;

    Log::World("WorldMap: [PLAN] Path found: %d waypoints from seg(%d,%d) to goal seg(%d,%d) (%d goal cells in zone, expanded %d nodes, veh=%d)",
               rcount, startCol, startRow, goalCol, goalRow,
               s_driveGoalSegCount, popsExpanded, (int)veh);
    return true;
}

// ============================================================================
// #67 v0.18.3.67: route-map dump (diagnostic). Prints the REAL fine terrain
// grid spanning the planned route, route overlaid, so a BAT can SHOW whether
// the path threads an inland corridor or scrapes the coast -- and whether the
// grid's walkable/blocked classification is even right. Runs at plan time, so
// it logs the instant a drive starts (no need to drive into a jam). Legend:
// '.'=land 'f'=forest 'm'=gentle-mountain '~'=ocean (traversable for this
// vehicle); UPPERCASE 'X'/'F'/'^' = the same class but BLOCKED for it. Overlay
// (wins over terrain): 'S'=route start (post-snap) 'D'=Dollet/target cell
// 'G'=goal cell reached 'o'=route cell. Row 0 = north (top), col 0 = west.
#define ROUTE_MAP_DIAG 0
#if ROUTE_MAP_DIAG
static void DumpRouteMap(int startR, int startC, int goalR, int goalC,
                         int tgtR, int tgtC, VehicleType veh)
{
    int minR = startR, maxR = startR, minC = startC, maxC = startC;
    auto bump = [&](int r, int c){ if(r<minR)minR=r; if(r>maxR)maxR=r; if(c<minC)minC=c; if(c>maxC)maxC=c; };
    bump(goalR, goalC); bump(tgtR, tgtC);
    for (int i = 0; i < s_drivePathLen; i++)
        bump(UnpackRow(s_drivePath[i]), UnpackCol(s_drivePath[i]));
    const int PAD = 8;
    minR -= PAD; maxR += PAD; minC -= PAD; maxC += PAD;
    if (minR < 0) minR = 0;
    if (minC < 0) minC = 0;
    if (maxR >= WM_FINE_ROWS) maxR = WM_FINE_ROWS - 1;
    if (maxC >= WM_FINE_COLS) maxC = WM_FINE_COLS - 1;
    if (maxR - minR > 50) maxR = minR + 50;
    if (maxC - minC > 90) maxC = minC + 90;

    Log::World("WorldMap: [ROUTEMAP] rows %d..%d cols %d..%d  .=land f=forest m=mtn ~=ocean (CAPS/X/^=blocked) S=start D=dollet G=goal o=route",
               minR, maxR, minC, maxC);
    char line[112];
    for (int r = minR; r <= maxR; r++) {
        int n = 0;
        for (int c = minC; c <= maxC && n < (int)sizeof(line) - 1; c++) {
            uint8_t cls = s_walkClassFine[r][c];
            bool trav = IsFineTraversable(cls, s_steepFine[r][c], veh);
            char ch;
            switch (cls) {
                case SEG_LAND:     ch = trav ? '.' : 'X'; break;
                case SEG_FOREST:   ch = trav ? 'f' : 'F'; break;
                case SEG_MOUNTAIN: ch = trav ? 'm' : '^'; break;
                case SEG_OCEAN:    ch = '~'; break;
                default:           ch = trav ? ':' : '?'; break;
            }
            bool isPath = false;
            for (int i = 0; i < s_drivePathLen; i++)
                if (UnpackRow(s_drivePath[i]) == r && UnpackCol(s_drivePath[i]) == c) { isPath = true; break; }
            if      (r == startR && c == startC) ch = 'S';
            else if (r == tgtR   && c == tgtC)   ch = 'D';
            else if (r == goalR  && c == goalC)  ch = 'G';
            else if (isPath)                     ch = 'o';
            line[n++] = ch;
        }
        line[n] = '\0';
        Log::World("WorldMap: [ROUTEMAP] r%03d %s", r, line);
    }
    // #69 mechanism 2: report the largest inter-cell elevation step in the
    // dumped window + where, so WM_CLIMB_STEP can be calibrated against the
    // real false-coast cliff height (the Dollet corridor is in this window).
    int maxStep = 0, msr = minR, msc = minC;
    for (int rr = minR; rr <= maxR; rr++)
        for (int cc2 = minC; cc2 <= maxC; cc2++) {
            if (cc2 + 1 <= maxC) { int s = (int)s_elevFine[rr][cc2] - (int)s_elevFine[rr][cc2+1]; if (s < 0) s = -s; if (s > maxStep) { maxStep = s; msr = rr; msc = cc2; } }
            if (rr + 1 <= maxR) { int s = (int)s_elevFine[rr][cc2] - (int)s_elevFine[rr+1][cc2]; if (s < 0) s = -s; if (s > maxStep) { maxStep = s; msr = rr; msc = cc2; } }
        }
    Log::World("WorldMap: [ELEVMAP] window max adjacent elev-step=%d at fine(%d,%d) (WM_CLIMB_STEP=%d); elev S=%d D=%d",
               maxStep, msc, msr, WM_CLIMB_STEP, (int)s_elevFine[startR][startC], (int)s_elevFine[tgtR][tgtC]);
}
#endif

// ============================================================================
// PlanPathFine (#67 stage 2) -- fine-grid clearance-weighted route planner.
// ============================================================================
// Replaces the coarse 32x24 A* (PlanPath) for the actual routing. Clearance-
// weighted Dijkstra over the 256x192 fine grid using the SAME slope-aware
// traversability rule as catalog reachability (IsFineTraversable on
// s_walkClassFine + s_steepFine), so the route goes AROUND mountains/ocean
// instead of through them, and the clearance penalty (s_clearFine) pulls it
// toward corridor CENTRES rather than wall edges. 4-connected, torus-wrapped. Routes to the reachable fine cell NEAREST the destination
// coordinate (s_driveTargetX/Y) -- the target itself when reachable, else the
// closest walkable approach. (Routing to "any cell of the goal region" stopped
// short at a near edge of a large region and steered into a cliff.) The
// path is stored as packed fine cells in s_drivePath (PackSeg works unchanged:
// fine col 0-255 / row 0-191 fit the row<<8|col layout); the drive follows it
// with a small lookahead and steers toward fine-cell centres (1024-unit), which
// also dissolves the segment-centre overshoot. Decimated by stride if it would
// exceed DRIVE_PATH_MAX (real intra-continent paths are far shorter).
//
// #69 v0.18.3.92: WM_OFFROAD_PENALTY (the .86 flat road/non-road cost split)
// is RETIRED -- it was already 0 since build 4 (.91), so removing it is a
// no-op. The route is shaped by clearance + steepness + the height-step guard;
// nothing prefers road cells anymore. (Rationale history is in git / CHANGELOG.)

// #69 v0.18.3.88: STEEPNESS PENALTY. The route should follow the flat valley
// floor (steep~=0) because the terrain IS a corridor, not because a road runs
// through it -- the road was only ever a proxy for the flat floor. Charge a
// per-step cost proportional to the cell's steepness so the clearance-weighted
// Dijkstra prefers flatter ground. Divisor scales s_steepFine (walkable cells
// span ~0..400; blocked walls are already excluded) into the existing cost
// band (offroad=40, clearance<=100). Tunable; SMALLER = stronger floor-pull.
static const uint32_t WM_STEEP_PENALTY_DIV = 8;

static bool PlanPathFine(int startCol, int startRow, VehicleType veh)
{
    s_drivePathLen = 0; s_drivePathIdx = 0; s_drivePathPlanned = false;

    if (!s_walkGridLoaded) {
        Log::World("WorldMap: [PLAN] Fine grid not loaded \u2014 fine planner cannot run");
        return false;
    }
    if (s_driveGoalSegCount == 0) {
        Log::World("WorldMap: [PLAN] No goal segments \u2014 fine planner cannot run");
        return false;
    }
    if (startRow < 0 || startRow >= WM_FINE_ROWS ||
        startCol < 0 || startCol >= WM_FINE_COLS) {
        Log::World("WorldMap: [PLAN] Fine start (%d,%d) out of range", startCol, startRow);
        return false;
    }

    // Snap the start to the nearest traversable fine cell (the player may stand
    // on a coastal/edge cell whose centre fell in an ocean or blocked polygon).
    if (!IsFineTraversable(s_walkClassFine[startRow][startCol],
                           s_steepFine[startRow][startCol], veh)) {
        const int SNAP = 4; bool snapped = false;
        for (int rad = 1; rad <= SNAP && !snapped; rad++)
            for (int dr = -rad; dr <= rad && !snapped; dr++)
                for (int dc = -rad; dc <= rad && !snapped; dc++) {
                    int nr = (((startRow + dr) % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
                    int nc = (((startCol + dc) % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
                    if (IsFineTraversable(s_walkClassFine[nr][nc], s_steepFine[nr][nc], veh)) {
                        startRow = nr; startCol = nc; snapped = true;
                    }
                }
        if (!snapped) {
            Log::World("WorldMap: [PLAN] Fine start not traversable, no walkable cell nearby");
            return false;
        }
    }

    static uint32_t dist  [WM_FINE_ROWS][WM_FINE_COLS];
    static int      parent[WM_FINE_ROWS][WM_FINE_COLS];
    for (int r = 0; r < WM_FINE_ROWS; r++)
        for (int c = 0; c < WM_FINE_COLS; c++) dist[r][c] = 0xFFFFFFFFu;

    // #67 v0.18.3.59: clearance-weighted Dijkstra (min-heap of cost<<32|cell,
    // lazy deletion) instead of a plain BFS. Step cost = 1 + WM_CLEAR_PENALTY *
    // (cells of clearance below WM_CLEAR_TARGET), so the route is pulled toward
    // the CENTRE of walkable corridors -- threading the canyon instead of
    // scraping its walls, which is what made the wall-hugging shortest path jam
    // the drive against cliffs the 1024-unit grid mislabels as walkable.
    static uint64_t heap[2 * WM_FINE_COLS * WM_FINE_ROWS];
    int heapSize = 0;
    auto hpush = [&](uint32_t cost, int idx) {
        if (heapSize >= (int)(sizeof(heap) / sizeof(heap[0]))) return;
        heap[heapSize] = ((uint64_t)cost << 32) | (uint32_t)idx;
        int i = heapSize++;
        while (i > 0) { int p = (i - 1) / 2; if (heap[p] <= heap[i]) break;
            uint64_t t = heap[p]; heap[p] = heap[i]; heap[i] = t; i = p; }
    };
    auto hpop = [&](uint32_t* oc, int* oidx) -> bool {
        if (heapSize == 0) return false;
        uint64_t top = heap[0];
        *oc = (uint32_t)(top >> 32); *oidx = (int)(uint32_t)(top & 0xFFFFFFFFu);
        heap[0] = heap[--heapSize]; int i = 0;
        for (;;) { int l = 2*i+1, r = 2*i+2, s = i;
            if (l < heapSize && heap[l] < heap[s]) s = l;
            if (r < heapSize && heap[r] < heap[s]) s = r;
            if (s == i) break; uint64_t t = heap[i]; heap[i] = heap[s]; heap[s] = t; i = s; }
        return true;
    };

    // Routing goal = the destination coordinate's fine cell. Flood the reachable
    // component from the player (clearance-weighted) and route to the cell
    // nearest the target -- the target itself when reachable (the common case;
    // the catalog already vetted it), else the closest walkable approach.
    // Routing to "any goal-region cell" was wrong: region 0x01 (Dollet's) is
    // large and reaches right next to the player, so the flood stopped 3 cells
    // north and the drive steered into the cliff (v0.18.3.56 BAT).
    int tgtCol = WorldXToFineCol(s_driveTargetX);
    int tgtRow = WorldYToFineRow(s_driveTargetY);

    dist[startRow][startCol] = 0; parent[startRow][startCol] = -1;
    hpush(0, startRow * WM_FINE_COLS + startCol);

    int bestR = startRow, bestC = startCol;
    int bestDist = abs(startRow - tgtRow) + abs(startCol - tgtCol);
    int expanded = 0;
    [[maybe_unused]] int elevBlockedEdges = 0;   // #69 mechanism 2: cliff edges the height-step guard skipped (read only under ROUTE_MAP_DIAG)
    const int dc4[] = { 0, 0, -1, 1 };
    const int dr4[] = { -1, 1, 0, 0 };
    uint32_t curCost; int curIdx;
    while (hpop(&curCost, &curIdx)) {
        int cr = curIdx / WM_FINE_COLS, cc = curIdx % WM_FINE_COLS;
        if (curCost > dist[cr][cc]) continue;   // stale heap entry
        expanded++;
        int dToTgt = abs(cr - tgtRow) + abs(cc - tgtCol);
        if (dToTgt < bestDist) { bestDist = dToTgt; bestR = cr; bestC = cc; }
        if (bestDist == 0) break;   // reached the target cell; can't improve
        for (int d = 0; d < 4; d++) {
            int nr = (((cr + dr4[d]) % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
            int nc = (((cc + dc4[d]) % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
            if (!IsFineTraversable(s_walkClassFine[nr][nc], s_steepFine[nr][nc], veh)) continue;
            // #69 v0.18.3.90 (mechanism 2): HEIGHT-STEP EDGE GUARD. A false
            // coast reads as walkable LAND per-cell but is a cliff at the EDGE
            // between this cell and the neighbour -- the grid is NODE-based, the
            // cliff is an EDGE. Block the step when the absolute floor-height
            // difference exceeds WM_CLIMB_STEP, so the cliff self-excludes even
            // though both cells are "land". Road-to-road steps are EXEMPT (the
            // road is ground-truth walkable across rendered height changes like
            // ramps/bridges; stepping OFF the road onto a non-road cliff is
            // still guarded). The road exemption also keeps the Dollet ribbon
            // connected while WM_CLIMB_STEP is being calibrated.
            if (!(s_roadFine[cr][cc] && s_roadFine[nr][nc])) {
                int elevStep = (int)s_elevFine[cr][cc] - (int)s_elevFine[nr][nc];
                if (elevStep < 0) elevStep = -elevStep;
                if (elevStep > WM_CLIMB_STEP) { elevBlockedEdges++; continue; }
            }
            // #69 v0.18.3.91 (build 4): ROAD COST-PREFERENCE RETIRED. The road
            // branch (road step = cost 1, clearance-exempt) is gone: EVERY cell
            // now pays the same clearance + steepness cost, so the route is
            // shaped by terrain GEOMETRY -- the height-step guard above excludes
            // the false-coast cliffs, the clearance penalty centres the corridor,
            // and the steepness penalty pulls to the flat floor. The road no
            // longer gets a free pass; mechanism 2 (+ clearance + steepness)
            // carries the route. WM_OFFROAD_PENALTY is now 0 (the road/non-road
            // distinction it encoded is retired); the term is kept one build so
            // the diff is isolated, removed in build 5. (The .85 road-walkable
            // override + .81 Dollet AABB also stay this build -- dropped as
            // cleanup in build 5 once this BAT proves the guard holds alone.)
            int clrPen = WM_CLEAR_TARGET - (int)s_clearFine[nr][nc];
            if (clrPen < 0) clrPen = 0;
            uint32_t steepPen = (uint32_t)s_steepFine[nr][nc] / WM_STEEP_PENALTY_DIV;
            uint32_t stepCost = 1u + (uint32_t)(WM_CLEAR_PENALTY * clrPen)
                                   + steepPen;
            uint32_t nd = curCost + stepCost;
            if (nd < dist[nr][nc]) {
                dist[nr][nc]   = nd;
                parent[nr][nc] = curIdx;
                hpush(nd, nr * WM_FINE_COLS + nc);
            }
        }
    }

    int goalR = bestR, goalC = bestC;
    if (goalR == startRow && goalC == startCol) {
        Log::World("WorldMap: [PLAN] Fine BFS: start already the closest reachable cell to target fine(%d,%d) (expanded %d, veh=%d) -- direct steer",
                   tgtCol, tgtRow, expanded, (int)veh);
        return false;
    }

    // Reconstruct goal->start via parents, then store start->goal.
    static uint16_t rev[WM_FINE_COLS * WM_FINE_ROWS];
    int rc = 0, curR = goalR, curC = goalC;
    while (!(curR == startRow && curC == startCol)) {
        rev[rc++] = PackSeg(curR, curC);
        int pr = parent[curR][curC];
        if (pr < 0) break;
        curR = pr / WM_FINE_COLS; curC = pr % WM_FINE_COLS;
        if (rc >= (int)(sizeof(rev) / sizeof(rev[0]))) break;
    }
    rev[rc++] = PackSeg(startRow, startCol);
    int total = rc;

    if (total <= DRIVE_PATH_MAX) {
        for (int i = 0; i < total; i++) s_drivePath[i] = rev[total - 1 - i];
        s_drivePathLen = total;
    } else {
        // Stride-sample to fit, preserving both endpoints.
        for (int i = 0; i < DRIVE_PATH_MAX; i++) {
            int srcRev = (int)((int64_t)i * (total - 1) / (DRIVE_PATH_MAX - 1));
            s_drivePath[i] = rev[total - 1 - srcRev];
        }
        s_drivePathLen = DRIVE_PATH_MAX;
    }
    s_drivePathIdx     = 0;
    s_drivePathPlanned = true;
    Log::World("WorldMap: [PLAN] Fine path: %d cells from fine(%d,%d) to fine(%d,%d) (nearest reachable to target fine(%d,%d), dist=%d, expanded %d, veh=%d, clearance-weighted)",
               s_drivePathLen, startCol, startRow, goalC, goalR, tgtCol, tgtRow, bestDist, expanded, (int)veh);
#if ROUTE_MAP_DIAG
    Log::World("WorldMap: [ELEVSTEP] #69 mechanism 2 height-step guard: blocked %d cliff edges during expansion (WM_CLIMB_STEP=%d)",
               elevBlockedEdges, WM_CLIMB_STEP);
    DumpRouteMap(startRow, startCol, goalR, goalC, tgtRow, tgtCol, veh);
#endif
    return true;
}

static bool PlanDrivePath(int32_t startX, int32_t startY)
{
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;

    if (!s_segmentRegionLoaded) {
        Log::World("WorldMap: [PLAN] Region map not loaded \u2014 fallback to catalog-center steering");
        return false;
    }

    VehicleType veh   = (s_lastVehicle < 0) ? VEH_ON_FOOT
                                            : GetVehicleType((uint8_t)s_lastVehicle);
    uint16_t    story = GetCurrentStoryFlag();

    if (veh == VEH_RAGNAROK) {
        Log::World("WorldMap: [PLAN] Ragnarok mode \u2014 skipping planner (catalog-center steering)");
        return false;
    }

    uint8_t region = 0;
    int progIdx = MatchProgramForCatalog(s_driveTargetX, s_driveTargetY,
                                         veh, story, &region);
    if (progIdx < 0) {
        return false;
    }

    s_driveGoalSegCount = CollectGoalSegments(region);
    if (s_driveGoalSegCount == 0) {
        Log::World("WorldMap: [PLAN] Region 0x%02X has zero cells in s_segmentRegionMap \u2014 fallback",
                   (unsigned)region);
        return false;
    }

    int startCol = WorldXToSegCol(startX);
    int startRow = WorldYToSegRow(startY);

    if (IsGoalSegment(startRow, startCol)) {
        s_drivePathLen     = 0;
        s_drivePathIdx     = 0;
        s_drivePathPlanned = true;
        Log::World("WorldMap: [PLAN] Player already in goal segment seg(%d,%d) region=0x%02X \u2014 empty path",
                   startCol, startRow, (unsigned)region);
        return true;
    }

    // #67 stage 2: route on the FINE slope-aware grid (around mountains/ocean),
    // not the coarse 32x24 segment grid that has no mountain class.
    int startFineCol = WorldXToFineCol(startX);
    int startFineRow = WorldYToFineRow(startY);
    return PlanPathFine(startFineCol, startFineRow, veh);
}

// ============================================================================
// ComputePlannerEligibility (v0.16.0 -- Part C)
// ============================================================================
// Called once at Initialize() time, AFTER LoadTriggerZones (so s_segmentRegionMap
// is populated) and AFTER the catalog is registered (so s_locations[] is valid).
// Walks every catalog entry, finds its region byte from s_segmentRegionMap,
// and scans s_triggerPrograms[] looking for at least one clause that names
// that region with a foot vehicle code (TRIG_VEH_FOOT 0x80 or TRIG_VEH_FOOT_ALT
// 0x84). If such a clause exists, the destination is "planner-eligible" and
// StartAutoDrive will route via PlanDrivePath / A* + closest-active-region.
// If no foot clause matches, the destination is a geometric-trigger destination
// (entered via terrain-29 polygon trigger, no wmsetus script event); the A*
// planner cannot represent it and its closest-active-region fallback will
// misroute the drive toward unrelated destinations (e.g. Fire Cavern catalog
// at region 0x0C maps only to program 20's Garden clause, no foot clause; the
// v0.14.95 fallback picks an unrelated active region and routes the player
// across the map).
//
// Geometric-trigger destinations include: Fire Cavern, early-game Balamb Garden
// (before mobile-Garden phase), several chocobo forests on small islands. AD
// for these falls back to v0.11.11-era simple-coord steering toward catalog
// coordinates -- not perfect but bounded and predictable.
//
// Logs each catalog entry's classification at init for diagnostic clarity.
// Defaults all flags to false; if s_segmentRegionLoaded is false (e.g.
// LoadTriggerZones failed), no entry is marked eligible -- safer than
// over-marking and routing into the wrong destination.
static void ComputePlannerEligibility()
{
    memset(s_destPlannerEligible, 0, sizeof(s_destPlannerEligible));

    if (!s_segmentRegionLoaded) {
        Log::World("WorldMap: [INIT] Planner-eligibility: s_segmentRegionMap not loaded, no destinations marked eligible");
        return;
    }

    int eligibleCount = 0;
    for (int catIdx = 0; catIdx < LOCATION_COUNT; catIdx++) {
        int col = WorldXToSegCol(s_locations[catIdx].x);
        int row = WorldYToSegRow(s_locations[catIdx].y);
        if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) {
            Log::World("WorldMap: [INIT] Planner-eligibility: %s (%d,%d) -> seg out of range -> NO",
                       s_locations[catIdx].name, s_locations[catIdx].x, s_locations[catIdx].y);
            continue;
        }
        uint8_t region = s_segmentRegionMap[row][col];
        if (region == 0xFF) {
            Log::World("WorldMap: [INIT] Planner-eligibility: %s seg(%d,%d) region=0xFF (no region) -> NO",
                       s_locations[catIdx].name, row, col);
            continue;
        }

        // Walk s_triggerPrograms[] looking for any clause that names this
        // region with a foot vehicle code.
        bool footClauseFound = false;
        for (int p = 0; p < TRIGGER_PROGRAM_COUNT && !footClauseFound; p++) {
            const TriggerProgram& prog = s_triggerPrograms[p];
            for (uint32_t c = 0; c < prog.num_clauses && !footClauseFound; c++) {
                const TriggerClause& cl = prog.clauses[c];
                if (cl.region != region) continue;
                if (cl.vehicle == TRIG_VEH_FOOT || cl.vehicle == TRIG_VEH_FOOT_ALT) {
                    footClauseFound = true;
                }
            }
        }

        s_destPlannerEligible[catIdx] = footClauseFound;
        if (footClauseFound) {
            eligibleCount++;
            Log::World("WorldMap: [INIT] Planner-eligibility: %s seg(%d,%d) region=0x%02X -> YES",
                       s_locations[catIdx].name, row, col, region);
        } else {
            Log::World("WorldMap: [INIT] Planner-eligibility: %s seg(%d,%d) region=0x%02X -> NO (no foot clause; will use simple-coord steering)",
                       s_locations[catIdx].name, row, col, region);
        }
    }
    Log::World("WorldMap: [INIT] Planner-eligibility: %d of %d catalog entries are planner-eligible",
               eligibleCount, LOCATION_COUNT);
}
