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
#define ROUTE_MAP_DIAG 1
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
            // #70 v0.18.3.99: stepping ONTO a road cell is guard-EXEMPT from ANY
            // cell, not just road->road. The road is ground-truth walkable, so a
            // pocket sealed by the height-step guard (the Dollet coastal shelf, the
            // exit start) can now connect to the adjacent canyon road and route
            // normally SW to Timber, instead of dead-ending 5 cells in and falling
            // back to straight-line steering into the wall. Stepping OFF a road onto
            // a non-road cliff stays guarded (asymmetric -> no new cliff-climbing),
            // and road-less continents (Balamb) are unaffected (no road cells, so
            // this condition is identical to before there).
            if (!s_roadFine[nr][nc]) {
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
        // #70 v0.18.3.98: this IS a pocket -- BFS walled in (best cell is the start
        // itself) yet far from target. Expose the signal so PlanDrivePath bridges out
        // even though PlanPathFine returns false here.
        s_drivePlanExpanded = expanded;
        s_drivePlanDist     = bestDist;
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
    // #70 v0.18.3.97: expose the pocket signal to PlanDrivePath's bridge-out decision.
    s_drivePlanExpanded = expanded;
    s_drivePlanDist     = bestDist;
#if ROUTE_MAP_DIAG
    Log::World("WorldMap: [ELEVSTEP] #69 mechanism 2 height-step guard: blocked %d cliff edges during expansion (WM_CLIMB_STEP=%d)",
               elevBlockedEdges, WM_CLIMB_STEP);
    DumpRouteMap(startRow, startCol, goalR, goalC, tgtRow, tgtCol, veh);
#endif
    return true;
}

// #70 v0.18.3.97: nearest road cell (as a world coordinate) to a fine cell, via
// an outward ring search over s_roadFine. The road is the guard-exempt escape
// ramp out of an elevation-sealed pocket; bridging the character to it lets a
// fresh plan route normally (road-to-road edges bypass the height-step guard).
static bool FindNearestRoadWorld(int sc, int sr, int32_t* ox, int32_t* oy)
{
    for (int rad = 1; rad <= WM_BRIDGE_ROAD_RADIUS; ++rad) {
        for (int dr = -rad; dr <= rad; ++dr) {
            for (int dc = -rad; dc <= rad; ++dc) {
                if (dr > -rad && dr < rad && dc > -rad && dc < rad) continue; // ring perimeter only
                int r = sr + dr, c = sc + dc;
                if (r < 0 || r >= WM_FINE_ROWS || c < 0 || c >= WM_FINE_COLS) continue;
                if (s_roadFine[r][c]) {
                    FineCellCenterToWorld(c, r, ox, oy);
                    return true;
                }
            }
        }
    }
    return false;
}

#if NAVMESH_DIAG && NAVMESH_ROUTING
// === v0.18.3.123 ROAD-VERIFICATION DIAGNOSTIC (read-only; remove after diagnosis) ===
// The navmesh A* IGNORES the road overlay (s_roadFine) -- it routes on the
// triangle mesh + 200-step gate alone. So dump the FULL route (not the .112
// 40-cap) and flag each waypoint against the known-walkable Timber->Dollet road.
// If the route tracks the road, our walkability model matches reality (the
// executor is the only problem left); if it diverges into the deep canyon the
// road avoids, the model is still passing spurious terrain (same family as the
// retracted switchback). The road is NEVER consulted by the planner -- only by
// this diagnostic, so it is not a navigational crutch.
static void NavmeshDumpRouteRoad(const std::vector<int>& path,
                                 int32_t startX, int32_t startY,
                                 int32_t goalX, int32_t goalY,
                                 const char* label)
{
    int dn = (int)path.size();
    Log::World("WorldMap: [ROADV:%s] %d tris; start(%d,%d) fine(c%d,r%d) floorZ=%d -> goal(%d,%d) fine(c%d,r%d) floorZ=%d",
               label, dn, startX, startY, WorldXToFineCol(startX), WorldYToFineRow(startY),
               (dn > 0 ? (int)s_nmFloor[path[0]] : 0),
               goalX, goalY, WorldXToFineCol(goalX), WorldYToFineRow(goalY),
               (dn > 0 ? (int)s_nmFloor[path[dn-1]] : 0));
    int prevZ = 0, onRoad = 0, minZ = 0x3FFFFFFF, maxZ = -0x3FFFFFFF;
    for (int i = 0; i < dn; i++) {
        int32_t gx = 0, gy = 0; Navmesh_TriangleCentroidGame(path[i], &gx, &gy);
        int zz = (int)s_nmFloor[path[i]];
        int fc = WorldXToFineCol(gx), fr = WorldYToFineRow(gy);
        bool road = (fr >= 0 && fr < WM_FINE_ROWS && fc >= 0 && fc < WM_FINE_COLS)
                    && (s_roadFine[fr][fc] != 0);
        if (road) onRoad++;
        if (zz < minZ) minZ = zz;
        if (zz > maxZ) maxZ = zz;
        Log::World("WorldMap: [ROADV:%s]   [%d] tri#%d game(%d,%d) fine(c%d,r%d) floorZ=%d dZ=%+d %s",
                   label, i, path[i], gx, gy, fc, fr, zz, (i == 0 ? 0 : zz - prevZ),
                   road ? "ROAD" : "off");
        prevZ = zz;
    }
    Log::World("WorldMap: [ROADV:%s] SUMMARY %d/%d waypoints on road, floorZ [%d..%d]",
               label, onRoad, dn, (dn > 0 ? minZ : 0), (dn > 0 ? maxZ : 0));
}

// v0.18.3.123: one-shot road oracle, fired once at world-load. Logs (1) the
// road's bounding box (where s_roadFine actually runs) and (2) an A* along the
// road's own endpoints, Timber->Dollet, dumped + road-flagged. If THAT route
// tracks the road the model is sound; if even it dives into a canyon, the model
// -- not the executor -- is what is broken.
static void RoadVerifyTimberDollet()
{
    int minC = 9999, maxC = -1, minR = 9999, maxR = -1, cells = 0;
    for (int r = 0; r < WM_FINE_ROWS; r++)
        for (int c = 0; c < WM_FINE_COLS; c++)
            if (s_roadFine[r][c]) {
                cells++;
                if (c < minC) minC = c;
                if (c > maxC) maxC = c;
                if (r < minR) minR = r;
                if (r > maxR) maxR = r;
            }
    if (cells > 0) {
        int32_t x0, y0, x1, y1;
        FineCellCenterToWorld(minC, minR, &x0, &y0);
        FineCellCenterToWorld(maxC, maxR, &x1, &y1);
        Log::World("WorldMap: [ROADV] road extent: %d cells, fine col[%d..%d] row[%d..%d], game X[%d..%d] Y[%d..%d]",
                   cells, minC, maxC, minR, maxR, x0, x1, y0, y1);
    } else {
        Log::World("WorldMap: [ROADV] road extent: 0 cells (s_roadFine empty)");
    }

    const int32_t TIMBER_X = -22564, TIMBER_Y = -4867;
    const int32_t DOLLET_X = -15639, DOLLET_Y = -39437;
    int tT = Navmesh_FindTriangleGame(TIMBER_X, TIMBER_Y);
    int tD = Navmesh_FindTriangleGame(DOLLET_X, DOLLET_Y);
    if (tT < 0 || tD < 0) {
        Log::World("WorldMap: [ROADV:TIMBER-DOLLET] start/goal tri not found (Timber #%d, Dollet #%d)", tT, tD);
        return;
    }
    std::vector<int> path;
    double L = Navmesh_AStar(tT, tD, -1, &path);
    if (L < 0.0 || path.size() < 2) {
        Log::World("WorldMap: [ROADV:TIMBER-DOLLET] A* NO path (Timber #%d -> Dollet #%d)", tT, tD);
        return;
    }
    NavmeshDumpRouteRoad(path, TIMBER_X, TIMBER_Y, DOLLET_X, DOLLET_Y, "TIMBER-DOLLET");
}

// v0.18.3.124 ROAD-CONNECTIVITY diagnostic (read-only): is the player-walkable
// Timber->Dollet road actually CONNECTED in the navmesh, or did the .120
// 200-step gate sever it (forcing A* into the canyon detour the .123 dumps
// showed -- 80/232 on road, dive to floorZ -1567)? (1) 8-connected BFS over the
// fine-grid road cells (s_roadFine) from Timber toward Dollet -> the ordered
// road ribbon + a fine-grid continuity check. (2) Walk the ribbon (decimated):
// log each checkpoint's navmesh floorZ (does the ROAD itself stay shallow, or
// is the navmesh deep under it?) and run a short-range A* to the previous
// checkpoint -- a few shallow tris = road navmesh-connected there; no path, or
// a sub-path that dives below floorZ -1000 = the gate SEVERED the road there
// and A* must leave it. Pinpoints the break. The road is never used to STEER --
// only to probe the mesh, so this stays inside the verify-not-crutch line.
static void RoadConnectivityDiag()
{
    const int32_t TIMBER_X = -22564, TIMBER_Y = -4867;
    const int32_t DOLLET_X = -15639, DOLLET_Y = -39437;
    int tc = WorldXToFineCol(TIMBER_X), tr = WorldYToFineRow(TIMBER_Y);
    int dc = WorldXToFineCol(DOLLET_X), dr = WorldYToFineRow(DOLLET_Y);

    auto snapRoad = [&](int& c, int& r) -> bool {
        if (r >= 0 && r < WM_FINE_ROWS && c >= 0 && c < WM_FINE_COLS && s_roadFine[r][c]) return true;
        for (int rad = 1; rad <= 10; rad++)
            for (int dd = -rad; dd <= rad; dd++)
                for (int ee = -rad; ee <= rad; ee++) {
                    int nr = r + dd, nc = c + ee;
                    if (nr < 0 || nr >= WM_FINE_ROWS || nc < 0 || nc >= WM_FINE_COLS) continue;
                    if (s_roadFine[nr][nc]) { r = nr; c = nc; return true; }
                }
        return false;
    };
    if (!snapRoad(tc, tr) || !snapRoad(dc, dr)) {
        Log::World("WorldMap: [ROADCON] no road cell near Timber/Dollet -- cannot trace ribbon");
        return;
    }

    static bool seen[WM_FINE_ROWS][WM_FINE_COLS];
    static int  par [WM_FINE_ROWS][WM_FINE_COLS];
    static int  q   [WM_FINE_ROWS * WM_FINE_COLS];
    for (int zr = 0; zr < WM_FINE_ROWS; zr++)
        for (int zc = 0; zc < WM_FINE_COLS; zc++) seen[zr][zc] = false;
    int qh = 0, qt = 0;
    seen[tr][tc] = true; par[tr][tc] = -1; q[qt++] = tr * WM_FINE_COLS + tc;
    const int d8r[8] = {-1,-1,-1, 0, 0, 1, 1, 1};
    const int d8c[8] = {-1, 0, 1,-1, 1,-1, 0, 1};
    bool reached = false;
    while (qh < qt) {
        int cur = q[qh++]; int cr = cur / WM_FINE_COLS, cc = cur % WM_FINE_COLS;
        if (cr == dr && cc == dc) { reached = true; break; }
        for (int k = 0; k < 8; k++) {
            int nr = cr + d8r[k], nc = cc + d8c[k];
            if (nr < 0 || nr >= WM_FINE_ROWS || nc < 0 || nc >= WM_FINE_COLS) continue;
            if (seen[nr][nc] || !s_roadFine[nr][nc]) continue;
            seen[nr][nc] = true; par[nr][nc] = cur; q[qt++] = nr * WM_FINE_COLS + nc;
        }
    }
    // v0.18.3.125: the road overlay (s_roadFine = wmx terrain 27/28 only)
    // fragments where the road climbs through terrain-29 MOUNTAINS, so the BFS
    // usually cannot reach Dollet's own road cell. Find the reached road cell
    // CLOSEST to Dollet (the far end of Timber's main road component), report
    // how far short of Dollet it stops, and dump the terrain types in the gap
    // toward Dollet. If the gap is terrain 29 (SEG_MOUNTAIN), that IS the
    // mountain-pass road the 27/28 overlay misses (the #70 root cause) -- not a
    // real break in the road.
    int bestCell = tr * WM_FINE_COLS + tc; double bestD = 1e18;
    for (int rr = 0; rr < WM_FINE_ROWS; rr++)
        for (int cc2 = 0; cc2 < WM_FINE_COLS; cc2++) {
            if (!seen[rr][cc2]) continue;
            double dd = (double)(rr - dr) * (rr - dr) + (double)(cc2 - dc) * (cc2 - dc);
            if (dd < bestD) { bestD = dd; bestCell = rr * WM_FINE_COLS + cc2; }
        }
    int bcr = bestCell / WM_FINE_COLS, bcc = bestCell % WM_FINE_COLS;
    int rlen = 0; { int c2 = bestCell; while (c2 != -1) { rlen++; c2 = par[c2 / WM_FINE_COLS][c2 % WM_FINE_COLS]; } }
    Log::World("WorldMap: [ROADCON] Timber road component = %d cells, ribbon Timber->road-end = %d cells; %s; road-end fine(c%d,r%d) is %d cells short of Dollet fine(c%d,r%d)",
               qt, rlen, reached ? "REACHES Dollet road cell" : "does NOT reach Dollet road cell",
               bcc, bcr, (int)(sqrt(bestD) + 0.5), dc, dr);

    Log::World("WorldMap: [ROADCON] gap profile road-end->Dollet (terrain: 27/28=road 29=MOUNTAIN 32-34=ocean <=5=forest else=land):");
    int adr = (dr > bcr) ? (dr - bcr) : (bcr - dr);
    int adc = (dc > bcc) ? (dc - bcc) : (bcc - dc);
    int nsteps = (adr > adc) ? adr : adc;
    if (nsteps < 1) nsteps = 1;
    for (int s = 0; s <= nsteps; s++) {
        double t = (double)s / nsteps;
        int rr  = (int)(bcr + (dr - bcr) * t + 0.5);
        int cc2 = (int)(bcc + (dc - bcc) * t + 0.5);
        if (rr < 0 || rr >= WM_FINE_ROWS || cc2 < 0 || cc2 >= WM_FINE_COLS) continue;
        int32_t wx, wy; FineCellCenterToWorld(cc2, rr, &wx, &wy);
        int tri = Navmesh_FindTriangleGame(wx, wy);
        int terr = (tri >= 0) ? (int)s_nmTerr[tri] : -1;
        int fz   = (tri >= 0) ? (int)s_nmFloor[tri] : 99999;
        Log::World("WorldMap: [ROADCON]   gap[%d] fine(c%d,r%d) road=%d terrain=%d floorZ=%d tri#%d",
                   s, cc2, rr, (int)s_roadFine[rr][cc2], terr, fz, tri);
    }

    // v0.18.3.126: now that we can trace a continuous road, run the original
    // SEVERANCE test on it -- walk the road ribbon Timber->road-end and check
    // navmesh connectivity along it. Does the navmesh connect the road's
    // triangles (so A* COULD follow the shallow road) or sever them (forcing
    // the canyon detour the .123 dumps showed)? Log each checkpoint's navmesh
    // floorZ + terrain + a short-range A* to the previous checkpoint; flag
    // SEVERED on no-path or a sub-path diving below floorZ -1000.
    static int ribbon[WM_FINE_ROWS * WM_FINE_COLS];
    int rn = 0; { int c2 = bestCell; while (c2 != -1) { ribbon[rn++] = c2; c2 = par[c2 / WM_FINE_COLS][c2 % WM_FINE_COLS]; } }
    Log::World("WorldMap: [ROADCON] severance walk Timber->road-end (%d-cell ribbon, road-end %d cells from Dollet):", rn, (int)(sqrt(bestD) + 0.5));
    int step = rn / 16 + 1;
    int prevTri = -1; int breaks = 0;
    for (int i = rn - 1; i >= 0; i -= step) {
        int cell = ribbon[i]; int cr = cell / WM_FINE_COLS, cc = cell % WM_FINE_COLS;
        int32_t wx, wy; FineCellCenterToWorld(cc, cr, &wx, &wy);
        int tri = Navmesh_FindTriangleGame(wx, wy);
        int fz   = (tri >= 0) ? (int)s_nmFloor[tri] : 99999;
        int terr = (tri >= 0) ? (int)s_nmTerr[tri] : -1;
        if (prevTri >= 0 && tri >= 0) {
            std::vector<int> seg;
            double segL = Navmesh_AStar(prevTri, tri, -1, &seg);
            int minZ = 0x3FFFFFFF, nt = (int)seg.size();
            for (int s = 0; s < nt; s++) { int z = (int)s_nmFloor[seg[s]]; if (z < minZ) minZ = z; }
            bool brk = (segL < 0.0) || (nt > 0 && minZ < -1000);
            if (brk) breaks++;
            Log::World("WorldMap: [ROADCON]   road[%d] fine(c%d,r%d) terr=%d floorZ=%d | navA*=%d %dtris minZ=%d%s",
                       i, cc, cr, terr, fz, (int)segL, nt, (nt > 0 ? minZ : 0),
                       brk ? "  <== SEVERED" : "  ok");
        } else {
            Log::World("WorldMap: [ROADCON]   road[%d] fine(c%d,r%d) terr=%d floorZ=%d (Timber end)", i, cc, cr, terr, fz);
        }
        prevTri = tri;
    }
    Log::World("WorldMap: [ROADCON] SEVERANCE SUMMARY %d of ~%d road segments SEVERED (0 = road navmesh-connected; >0 = the gate cut the walkable road)", breaks, (rn - 1) / step);
}

// v0.18.3.106 (#70 routing swap): route on the walkability-filtered triangle
// navmesh. A* (ungated -- poly[0x0E] walkability is the passability test) from
// the start triangle to the destination triangle, then convert each triangle
// CENTROID to a fine cell so the existing #68 executor (which consumes
// s_drivePath as packed fine cells) follows it UNCHANGED. Consecutive duplicate
// cells are dropped. Returns false if the navmesh has no path (caller falls
// back to the fine-grid planner) so a transient pre-build drive still routes.
static bool PlanDrivePathNavmesh(int32_t startX, int32_t startY)
{
    int startTri = Navmesh_FindTriangleGame(startX, startY);
    int goalTri  = Navmesh_FindTriangleGame(s_driveTargetX, s_driveTargetY);
    if (startTri < 0 || goalTri < 0) return false;
    // v0.18.3.111 (#70): REVERT the .108 funnel string-pull -- it cut chords
    // STRAIGHT ACROSS non-walkable cliff terrain. The SSF's degenerate centroid
    // portals (at the bridge / proximity links that share no edge) give the
    // string-pull no real left/right walls, so it pulls taut THROUGH a box-
    // canyon wall: the F11 at the 17km wedge shows Squall steered into a vertical
    // rock face, steer target locked NW (-31232,-31232) ACROSS the cliff while
    // Dollet is NE. The LOS clamp can't catch it (the coarse 1024u grid reads the
    // cliff as walkable land -- which is why .109 clamp-OFF == .110 clamp-ON, byte
    // for byte). Use the raw A* triangle CENTROIDS as waypoints instead: each
    // centroid is strictly inside a walkable triangle, and consecutive centroids
    // are joined by a shared edge or a short (<=400u, z-gated <=300u) bridge/prox
    // neck, so the path stays ON walkable ground and the chords are too short to
    // span a cliff. Rasterized dense below (same as the funnel) so the executor
    // still reads adjacent legs. (The executor's far-lookahead can still cut a
    // corner across a cliff the coarse clamp misses -- the .107 ~12km wedge --
    // which is the next, separate step: a navmesh-resolution walkability clamp.)
    std::vector<int32_t> cornGX, cornGY;
    std::vector<int> tris;
    // v0.18.3.119 (#70): A* UNGATED on floor-step. The .118 floor-step gate at
    // 200 FAILED -- its [NAVPATH] still dived into the box canyon (floorZ -566
    // -> -1601) because A* just found a GENTLER centroid descent (every step
    // <=197) that the 200 gate permits: s_nmFloor is the MEAN of 3 corner
    // heights, so a cliff face reads as a gentle staircase and NO floor-step
    // threshold separates the canyon from the corridor (176u neck). The fix is
    // upstream -- Navmesh_AddTriangle now drops cliff-face tris by true per-tri
    // SLOPE (the engine's actual collision quantity), so the canyon walls are
    // gone from the graph and A* must take the corridor; no edge gate needed.
    double navL = Navmesh_AStar(startTri, goalTri, -1, &tris);
    int triCount = (int)tris.size();
    if (navL < 0.0 || tris.size() < 2) {
        Log::World("WorldMap: [PLAN] navmesh A* NO path: start tri#%d -> goal tri#%d -- fine-grid fallback",
                   startTri, goalTri);
        return false;
    }
    for (size_t i = 0; i < tris.size(); i++) {
        int32_t cgx, cgy;
        if (Navmesh_TriangleCentroidGame(tris[i], &cgx, &cgy)) {
            cornGX.push_back(cgx); cornGY.push_back(cgy);
        }
    }
    if (cornGX.size() < 2) return false;

    // v0.18.3.123 ROAD-VERIFICATION: full route dump + per-waypoint road flag
    // (replaces the .112 40-cap [NAVPATH] dump). See NavmeshDumpRouteRoad above.
    NavmeshDumpRouteRoad(tris, startX, startY, s_driveTargetX, s_driveTargetY, "DRIVE");

    // rasterize the corner polyline into dense fine cells (~512u steps, shortest
    // torus delta, dedup consecutive) so consecutive cells are adjacent and the
    // executor's corner-cap reads each funnel leg as a sustained run.
    int n = 0; int last = -1;
    auto emitCell = [&](int32_t wx, int32_t wy) {
        int fc = WorldXToFineCol(wx), fr = WorldYToFineRow(wy);
        if (fr < 0 || fr >= WM_FINE_ROWS || fc < 0 || fc >= WM_FINE_COLS) return;
        uint16_t packed = PackSeg(fr, fc);
        if ((int)packed == last) return;
        if (n < DRIVE_PATH_MAX) { s_drivePath[n++] = packed; last = (int)packed; }
    };
    for (size_t i = 0; i + 1 < cornGX.size() && n < DRIVE_PATH_MAX; i++) {
        int32_t x0 = cornGX[i],   y0 = cornGY[i];
        int32_t x1 = cornGX[i+1], y1 = cornGY[i+1];
        int32_t dx = x1 - x0, dy = y1 - y0;
        if      (dx >  NM_WX / 2) dx -= NM_WX;   // shortest torus delta (game coords)
        else if (dx < -NM_WX / 2) dx += NM_WX;
        if      (dy >  NM_WY / 2) dy -= NM_WY;
        else if (dy < -NM_WY / 2) dy += NM_WY;
        int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        int span = adx > ady ? adx : ady;
        int steps = span / 512 + 1;
        for (int s = 0; s <= steps && n < DRIVE_PATH_MAX; s++) {
            int32_t wx = x0 + (int32_t)((int64_t)dx * s / steps);
            int32_t wy = y0 + (int32_t)((int64_t)dy * s / steps);
            emitCell(wx, wy);
        }
    }
    if (n < 2) return false;
    s_drivePathLen      = n;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = true;
    s_drivePlanExpanded = 9999;   // not a pocket -> the bridge-out stays off
    s_drivePlanDist     = 0;
    // s_driveNavmeshPath stays FALSE: v0.18.3.110 REVERTED the .109 executor coarse-grid bypass -- it REGRESSED. The funnel's dense/degenerate corners NEED the executor's LOS clamp to keep the steer target on a clear straight line; with the clamp bypassed the steer target locked onto a WRONG-WAY funnel corner (steer(-31232,-31232) = WEST, away from Dollet) and the drive walked into a wall at 15-17km, vs .108's clean 4km WITH the clamp active. The FineLineClearFootCar LOS clamp + #83 fwd-guard stay ACTIVE on the navmesh path (= the .108 executor). (The clamp's .81-box over-clamp near Dollet is a separate, narrower final-approach problem.)
    Log::World("WorldMap: [PLAN] navmesh centroids: start tri#%d -> goal tri#%d = %d tris -> %d centroids -> %d fine cells",
               startTri, goalTri, triCount, (int)cornGX.size(), n);
    return true;
}
#endif

// ===== v0.18.3.145 (#70): FAITHFUL GRID PLANNER =====
// Plans on the ACTUAL walkable surface using the engine-faithful block-local height query
// (WorldGroundHeightLocal) + the engine's 200 step gate -- the SAME rule the executor's
// STEPGUARD applies -- instead of the triangle-navmesh A*, which kept routing through
// faithfully-unreachable deep pockets (the Timber->G-Garden stall chose a waypoint at -1305
// behind a 239u cliff). 8-neighbour A* on a 128u grid within a bbox around start+goal; emits
// deduped fine cells into s_drivePath (same format as the other planners). Goal is the global
// s_driveTargetX/Y. Offline this routes all three acceptance trios correctly. No exotic STL
// (this .inl is inside namespace WorldMap, so no headers added) -- std::vector + a manual heap.
