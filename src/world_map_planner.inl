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

    return PlanPath(startCol, startRow, veh);
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
