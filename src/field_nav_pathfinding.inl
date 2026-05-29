// field_nav_pathfinding.inl — A* walkmesh pathfinding, funnel smoothing, BFS
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// A* Walkmesh Pathfinding (v05.62)
// ============================================================================
//
// Finds the shortest path through the walkmesh triangle graph from the
// player's current triangle to the triangle nearest the target position.
// Returns a sequence of triangle-center waypoints that UpdateAutoDrive
// follows instead of heading straight-line.
//
// The graph nodes are walkmesh triangles. Edges connect triangles that
// share an edge (neighbor[0..2]). Edge cost = Euclidean distance between
// triangle centers. Heuristic = straight-line distance to goal.

// Find the walkmesh triangle whose center is closest to (x, y).
// Returns triangle index, or -1 if walkmesh not loaded.
static int FindNearestTriangle(float x, float y)
{
    if (!s_walkmesh.valid || s_walkmesh.numTriangles == 0) return -1;
    int best = -1;
    float bestDist = 1e30f;
    for (int t = 0; t < s_walkmesh.numTriangles; t++) {
        float dx = s_walkmesh.triangles[t].centerX - x;
        float dy = s_walkmesh.triangles[t].centerY - y;
        float d = dx*dx + dy*dy;
        if (d < bestDist) { bestDist = d; best = t; }
    }
    return best;
}

// v0.17.7.1: Point-in-triangle test for the walkmesh exclusion filter in
// RefreshCatalog. Returns true if (x, y) lies inside any walkmesh triangle
// using the standard sign-of-cross-product 2D test.
//
// Rationale: light sources, scenery and other non-interactive props on FF8
// fields are commonly placed off the walkmesh (above an alcove ceiling, on
// a far wall, etc.). The walkmesh is the navigable surface, so an entity
// off-mesh is one the player can't reach. Combined with the
// no-TALKRADIUS/TALKON check at the catalog call site, this excludes them
// from the entity catalog while preserving talkable off-mesh entities like
// guards over a railing (player interacts from on-mesh).
//
// Returns false if the walkmesh isn't loaded -- conservative default that
// keeps every entity rather than dropping them all when data is unavailable.
//
// The walkmesh is already in memory for A* path-finding, so this is cheap
// (one cross-product triple per triangle, at most a few hundred per field,
// run at most ~16 times per RefreshCatalog).
static bool IsInsideWalkmesh(float x, float y)
{
    if (!s_walkmesh.valid || s_walkmesh.numTriangles == 0) return false;
    for (int t = 0; t < s_walkmesh.numTriangles; t++) {
        int vi0 = s_walkmesh.triangles[t].vertexIdx[0];
        int vi1 = s_walkmesh.triangles[t].vertexIdx[1];
        int vi2 = s_walkmesh.triangles[t].vertexIdx[2];
        if (vi0 < 0 || vi0 >= s_walkmesh.numVertices) continue;
        if (vi1 < 0 || vi1 >= s_walkmesh.numVertices) continue;
        if (vi2 < 0 || vi2 >= s_walkmesh.numVertices) continue;
        float ax = (float)s_walkmesh.vertices[vi0].x;
        float ay = (float)s_walkmesh.vertices[vi0].y;
        float bx = (float)s_walkmesh.vertices[vi1].x;
        float by = (float)s_walkmesh.vertices[vi1].y;
        float cx = (float)s_walkmesh.vertices[vi2].x;
        float cy = (float)s_walkmesh.vertices[vi2].y;
        // Sign-of-cross-product test. If all three signs match, the point is
        // inside; if mixed, it's outside. Points exactly on an edge get
        // counted as inside (zero is permissive in both directions here).
        float d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by);
        float d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy);
        float d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay);
        bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
        bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
        if (!(hasNeg && hasPos)) return true;
    }
    return false;
}

// A* open set node.
struct AStarNode {
    uint16_t triIdx;
    float    gCost;    // cost from start
    float    fCost;    // gCost + heuristic
    int16_t  cameFrom; // parent triangle index (-1 = start)
};

// v05.81: Compute the length of the shared edge between triangle triIdx and
// its neighbor on edge edgeIdx (0-2). The shared edge connects the two
// vertices that are NOT vertexIdx[edgeIdx]. Returns 0 if data is invalid.
static float GetSharedEdgeLength(int triIdx, int edgeIdx)
{
    if (!s_walkmesh.valid || triIdx < 0 || triIdx >= s_walkmesh.numTriangles) return 0;
    // Shared edge connects vertex[(edge+1)%3] and vertex[(edge+2)%3]
    int vi1 = s_walkmesh.triangles[triIdx].vertexIdx[(edgeIdx + 1) % 3];
    int vi2 = s_walkmesh.triangles[triIdx].vertexIdx[(edgeIdx + 2) % 3];
    if (vi1 >= s_walkmesh.numVertices || vi2 >= s_walkmesh.numVertices) return 0;
    float dx = (float)(s_walkmesh.vertices[vi1].x - s_walkmesh.vertices[vi2].x);
    float dy = (float)(s_walkmesh.vertices[vi1].y - s_walkmesh.vertices[vi2].y);
    return sqrtf(dx*dx + dy*dy);
}

// v05.81: Compute how well a movement direction aligns with the 8 arrow-key
// directions (N, NE, E, SE, S, SW, W, NW). Returns a penalty multiplier:
//   1.0 = perfectly aligned with one of the 8 directions
//   up to ~2.0 = worst case (22.5 degrees off from nearest direction)
// The 8 directions are spaced 45 degrees apart, so maximum misalignment is 22.5 degrees.
static float GetAngleAlignmentPenalty(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f) return 1.0f;
    float angle = atan2f(dy, dx);  // radians, -PI to PI
    // Snap to nearest 45-degree increment
    float sector = angle / (float)(NAV_PI / 4.0);  // -4 to 4
    float nearest = roundf(sector);
    float diff = fabsf(sector - nearest);  // 0 to 0.5 (0 = aligned, 0.5 = 22.5 deg off)
    // Scale: 0 deviation = 1.0x cost, 0.5 deviation = 2.0x cost
    return 1.0f + diff * 2.0f;
}

// v05.81: Minimum shared edge width for A* to consider a passage navigable.
// Edges shorter than this are treated as too narrow for 8-directional steering.
// Typical character width is roughly 30-50 world units.
// v05.93: Reverted to v05.89 values that successfully navigated bg2f_1.
// The v05.92 increase to 80/200 was too aggressive and blocked walkable aisles.
static const float MIN_EDGE_WIDTH = 40.0f;
static const float NARROW_EDGE_THRESHOLD = 100.0f;
static const float NARROW_EDGE_PENALTY = 3.0f;

// v05.92: Forward declarations for trigger-line avoidance in A*.
// These are defined later in the file but needed by ComputeAStarPath.
// v06.02: skipTriggerIdx allows exempting one trigger line (used when
// driving TO a screen transition — we need to cross that specific line).
static bool IsSeparatedByTriggerLine(float px, float py, float ex, float ey, int skipTriggerIdx = -1);
// v06.05: Check if moving from (px,py) in direction (dx,dy) by RECOVERY_CHECK_DIST
// would cross any non-target active trigger line. Used to prevent recovery
// wiggle from accidentally pushing the player through screen transitions.
static const float RECOVERY_CHECK_DIST = 400.0f;  // how far ahead to check
static bool WouldCrossTriggerLine(float px, float py, float dx, float dy, int skipTriggerIdx);

// v05.92: Trigger line data moved here (was in SETLINE section) so A* can access it.
struct CapturedTriggerLine {
    uint32_t entityAddr;
    int      lineOrder;
    int16_t  x1, y1, z1;
    int16_t  x2, y2, z2;
    bool     active;
    char     name[48];
    // v0.07.82: JSM-classified line type for screen filtering.
    // Only JSM_ENT_LINE_SCREEN_BOUND lines act as screen boundaries.
    // Camera pans and event triggers are transparent.
    FieldArchive::JSMEntityType lineType;
    // v0.07.83: Destination field ID for LINE_SCREEN_BOUND (from MAPJUMP param).
    // -1 = unknown, -2 = world map, >= 0 = field ID.
    int destFieldId;
    // v0.12.24: True if the JSM entity also has foundExtDispatch (runtime 0x1C dispatch).
    // Used to identify dual-purpose Lines (exit + interaction via PSHM_W-dispatched dialog).
    bool hasExtDispatch;
    // v0.17.7.5.4: True if REQ-following found this Line REQs a target with
    // dialog opcodes or extended dispatch. The genuine dual-purpose signal
    // (distinct from hasExtDispatch which also fires on non-dialog 0x1C usage).
    // See JSMEntityInfo::hasDialogReqTarget for full rationale.
    bool hasDialogReqTarget;
};
static const int MAX_CAPTURED_LINES = 32;
static CapturedTriggerLine s_capturedLines[MAX_CAPTURED_LINES] = {};
static int s_capturedLineCount = 0;
static int s_setlineCallCount = 0;

// v05.93: Walkmesh line-of-sight check.
// Walks through walkmesh triangles from startTri toward (goalX, goalY).
// At each triangle, picks the neighbor whose shared edge is closest to the
// goal direction. If we reach the goal triangle (or a triangle within
// arriveDist of the goal), returns true — meaning A* can be skipped and
// the player can steer directly to the target.
// Also checks that the path doesn't cross any active trigger lines.
static bool HasLineOfSight(int startTri, float goalX, float goalY, float arriveDist)
{
    if (!s_walkmesh.valid || startTri < 0) return false;
    int numTri = s_walkmesh.numTriangles;
    if (startTri >= numTri) return false;

    // Walk up to 200 triangles (safety limit for large walkmeshes).
    int curTri = startTri;
    bool visited[4096] = {};
    if (numTri > 4096) return false;

    float startCX = s_walkmesh.triangles[startTri].centerX;
    float startCY = s_walkmesh.triangles[startTri].centerY;

    for (int step = 0; step < 200; step++) {
        if (curTri < 0 || curTri >= numTri) return false;
        visited[curTri] = true;

        float cx = s_walkmesh.triangles[curTri].centerX;
        float cy = s_walkmesh.triangles[curTri].centerY;

        // Check if we've reached the goal.
        float dx = goalX - cx;
        float dy = goalY - cy;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < arriveDist + 100.0f) return true;  // close enough

        // Check if this triangle is on the other side of a trigger line from start.
        if (s_capturedLineCount > 0) {
            if (IsSeparatedByTriggerLine(startCX, startCY, cx, cy)) return false;
        }

        // Find the neighbor that makes the most progress toward the goal.
        // Use dot product of (edge midpoint - current center) with (goal direction).
        int bestNeighbor = -1;
        float bestProgress = -1e30f;
        float dirLen = sqrtf(dx*dx + dy*dy);
        float ndx = (dirLen > 0.001f) ? dx / dirLen : 0;
        float ndy = (dirLen > 0.001f) ? dy / dirLen : 0;

        for (int e = 0; e < 3; e++) {
            uint16_t nb = s_walkmesh.triangles[curTri].neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
            if (visited[nb]) continue;

            // Check edge width — skip too-narrow passages.
            float edgeWidth = GetSharedEdgeLength(curTri, e);
            if (edgeWidth > 0 && edgeWidth < MIN_EDGE_WIDTH) continue;

            float nbCX = s_walkmesh.triangles[nb].centerX;
            float nbCY = s_walkmesh.triangles[nb].centerY;
            // Progress = how much closer this neighbor gets us to the goal.
            float progress = (nbCX - cx) * ndx + (nbCY - cy) * ndy;
            if (progress > bestProgress) {
                bestProgress = progress;
                bestNeighbor = (int)nb;
            }
        }

        if (bestNeighbor < 0) return false;  // dead end
        curTri = bestNeighbor;
    }
    return false;  // exceeded step limit
}

// v06.01: Check if two triangles are on the same walkmesh island.
// Uses BFS from startTri. Returns true if goalTri is reachable.
// This is fast (<1ms for 512 triangles) and detects disconnected islands
// that make A* pathfinding impossible (47.5% of FF8 fields have these).
static bool AreTrianglesConnected(int startTri, int goalTri)
{
    if (!s_walkmesh.valid) return false;
    if (startTri < 0 || goalTri < 0) return false;
    if (startTri == goalTri) return true;
    int numTri = s_walkmesh.numTriangles;
    if (startTri >= numTri || goalTri >= numTri) return false;

    // BFS from startTri.
    static bool visited[4096];
    if (numTri > 4096) return false;
    memset(visited, 0, sizeof(bool) * numTri);

    static uint16_t queue[4096];
    int qHead = 0, qTail = 0;
    queue[qTail++] = (uint16_t)startTri;
    visited[startTri] = true;

    while (qHead < qTail) {
        uint16_t cur = queue[qHead++];
        if (cur == (uint16_t)goalTri) return true;
        for (int e = 0; e < 3; e++) {
            uint16_t nb = s_walkmesh.triangles[cur].neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
            if (visited[nb]) continue;
            visited[nb] = true;
            queue[qTail++] = nb;
        }
    }
    return false;
}

// Run A* from startTri to goalTri. Writes waypoint centers into
// s_waypoints[] and sets s_waypointCount. Returns true if path found.
// v05.80: targetEntityIdx is used for push-radius blackout — triangles
// within any NPC's push radius (except the target) are treated as blocked.
// v06.02: skipTriggerIdx exempts one trigger line from A* avoidance
// (used when driving to a screen transition exit).
static bool ComputeAStarPath(int startTri, int goalTri, int targetEntityIdx = -1, int skipTriggerIdx = -1)
{
    s_waypointCount = 0;
    s_waypointIdx   = 0;
    s_usingFunnel   = false;  // v05.95: reset until FunnelPath sets it
    if (!s_walkmesh.valid) return false;
    if (startTri < 0 || goalTri < 0) return false;
    if (startTri == goalTri) {
        // Already on the goal triangle.
        s_waypoints[0][0] = s_walkmesh.triangles[goalTri].centerX;
        s_waypoints[0][1] = s_walkmesh.triangles[goalTri].centerY;
        s_waypointCount = 1;
        return true;
    }

    int numTri = s_walkmesh.numTriangles;
    float goalX = s_walkmesh.triangles[goalTri].centerX;
    float goalY = s_walkmesh.triangles[goalTri].centerY;

    // Per-triangle best known gCost and parent.
    // Using static arrays to avoid heap allocation in game thread.
    static float bestG[4096];
    static int16_t parent[4096];
    static bool closed[4096];
    if (numTri > 4096) return false;  // safety

    for (int i = 0; i < numTri; i++) {
        bestG[i] = 1e30f;
        parent[i] = -1;
        closed[i] = false;
    }

    // Simple open list (not a heap — walkmesh is small enough).
    // Max 4096 triangles, so linear scan is fine.
    static uint16_t openList[4096];
    int openCount = 0;

    bestG[startTri] = 0.0f;
    openList[openCount++] = (uint16_t)startTri;

    bool found = false;
    int iterations = 0;
    static const int MAX_ITERATIONS = 20000;  // safety limit

    while (openCount > 0 && iterations < MAX_ITERATIONS) {
        iterations++;

        // Find node in open list with lowest fCost.
        int bestIdx = 0;
        float bestF = 1e30f;
        for (int i = 0; i < openCount; i++) {
            uint16_t ti = openList[i];
            float dx = s_walkmesh.triangles[ti].centerX - goalX;
            float dy = s_walkmesh.triangles[ti].centerY - goalY;
            float h = sqrtf(dx*dx + dy*dy);
            float f = bestG[ti] + h;
            if (f < bestF) { bestF = f; bestIdx = i; }
        }

        uint16_t current = openList[bestIdx];
        // Remove from open list (swap with last).
        openList[bestIdx] = openList[--openCount];
        closed[current] = true;

        if (current == (uint16_t)goalTri) { found = true; break; }

        // Expand neighbors.
        float curX = s_walkmesh.triangles[current].centerX;
        float curY = s_walkmesh.triangles[current].centerY;

        for (int e = 0; e < 3; e++) {
            uint16_t nb = s_walkmesh.triangles[current].neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
            if (closed[nb]) continue;

            float nbX = s_walkmesh.triangles[nb].centerX;
            float nbY = s_walkmesh.triangles[nb].centerY;

            // v05.80: Skip triangles blocked by NPC push radius.
            // Don't block the goal triangle itself (we need to reach it).
            if (targetEntityIdx >= 0 && nb != (uint16_t)goalTri) {
                if (IsTriangleBlockedByNPC(nbX, nbY, targetEntityIdx)) continue;
            }

            // v05.91: Skip triangles on the other side of active trigger lines.
            // This prevents the A* path from routing through screen transition
            // zones, which would cause the player to accidentally leave the field.
            // We check if the neighbor triangle's center is separated from the
            // start triangle's center by any active trigger line.
            if (s_capturedLineCount > 0 && nb != (uint16_t)goalTri) {
                float startCX = s_walkmesh.triangles[startTri].centerX;
                float startCY = s_walkmesh.triangles[startTri].centerY;
                if (IsSeparatedByTriggerLine(startCX, startCY, nbX, nbY, skipTriggerIdx)) continue;
            }

            // v05.81: Check shared edge width. Block edges too narrow to navigate.
            float edgeWidth = GetSharedEdgeLength((int)current, e);
            if (edgeWidth > 0 && edgeWidth < MIN_EDGE_WIDTH && nb != (uint16_t)goalTri) {
                continue;  // too narrow, skip entirely
            }

            float edgeCost = sqrtf((nbX - curX)*(nbX - curX) + (nbY - curY)*(nbY - curY));

            // v05.81: Penalize narrow edges — prefer wider corridors.
            // v05.92: Penalize narrow edges to strongly prefer wide aisles.
            // Even with analog steering, the character's collision body can't
            // fit through desk gaps. Use full penalty to route around.
            if (edgeWidth > 0 && edgeWidth < NARROW_EDGE_THRESHOLD) {
                edgeCost *= NARROW_EDGE_PENALTY;
            }

            // v05.81 angle alignment penalty REMOVED in v05.91.
            // With analog steering (v05.89), the player can move in any direction,
            // so penalizing non-cardinal movement creates unnecessarily roundabout paths.
            float tentG = bestG[current] + edgeCost;

            if (tentG < bestG[nb]) {
                bestG[nb] = tentG;
                parent[nb] = (int16_t)current;
                // Add to open list if not already there.
                bool inOpen = false;
                for (int i = 0; i < openCount; i++) {
                    if (openList[i] == nb) { inOpen = true; break; }
                }
                if (!inOpen && openCount < 4096) {
                    openList[openCount++] = nb;
                }
            }
        }
    }

    if (!found) {
        Log::Field("FieldNavigation: [A*] No path from tri %d to tri %d (%d iterations)",
                   startTri, goalTri, iterations);
        return false;
    }

    // Reconstruct path: trace parent[] from goal back to start.
    static uint16_t pathReverse[4096];
    int pathLen = 0;
    uint16_t cur = (uint16_t)goalTri;
    while (cur != (uint16_t)startTri && pathLen < 4096) {
        pathReverse[pathLen++] = cur;
        int16_t p = parent[cur];
        if (p < 0) break;
        cur = (uint16_t)p;
    }
    // v05.90: Store triangle corridor (start..goal order) for funnel algorithm.
    // Also store triangle-center waypoints as fallback.
    s_corridorCount = 0;
    s_waypointCount = 0;
    // Add start triangle first (not in pathReverse).
    if (s_corridorCount < MAX_CORRIDOR) s_corridor[s_corridorCount++] = (uint16_t)startTri;
    for (int i = pathLen - 1; i >= 0; i--) {
        if (s_corridorCount < MAX_CORRIDOR) s_corridor[s_corridorCount++] = pathReverse[i];
        if (s_waypointCount < MAX_WAYPOINTS) {
            s_waypoints[s_waypointCount][0] = s_walkmesh.triangles[pathReverse[i]].centerX;
            s_waypoints[s_waypointCount][1] = s_walkmesh.triangles[pathReverse[i]].centerY;
            s_waypointCount++;
        }
    }

    Log::Field("FieldNavigation: [A*] Path found: %d triangles, %d waypoints, %d iterations, start=%d goal=%d",
               s_corridorCount, s_waypointCount, iterations, startTri, goalTri);
    return (s_waypointCount > 0);
}

// v05.90: Simple Stupid Funnel Algorithm (SSFA) for path smoothing.
// Takes the triangle corridor from A* and produces the shortest path through
// it by "string-pulling" — finding the tightest rope through the portal edges.
// This replaces the old cumulative-angle SimplifyPath (v05.66) which just
// removed collinear waypoints from triangle-center paths.
//
// The algorithm walks through the portals (shared edges between consecutive
// corridor triangles) maintaining a funnel defined by left and right boundaries.
// When the funnel crosses itself, a turn point is emitted.
//
// Reference: "Simple Stupid Funnel Algorithm" by Mikko Mononen.

// Helper: 2D cross product of (OA x OB).
static float Cross2D(float ox, float oy, float ax, float ay, float bx, float by)
{
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

// Find the shared edge (portal) between two adjacent corridor triangles.
// Returns the two vertex positions of the shared edge.
// The left/right ordering is from the perspective of walking from triA to triB.
// v05.94: Fixed left/right determination. Use direction of travel (triA center
// → triB center) instead of opposite vertex. The cross product of the travel
// direction with the edge vector determines which side is left and right from
// the traveler's perspective.
static bool FindPortal(uint16_t triA, uint16_t triB,
                       float& leftX, float& leftY, float& rightX, float& rightY)
{
    if (!s_walkmesh.valid) return false;
    const auto& tA = s_walkmesh.triangles[triA];
    const auto& tB = s_walkmesh.triangles[triB];
    // Find which edge of triA connects to triB.
    int edgeIdx = -1;
    for (int e = 0; e < 3; e++) {
        if (tA.neighbor[e] == triB) { edgeIdx = e; break; }
    }
    if (edgeIdx < 0) return false;
    // Shared edge connects vertex[(edge+1)%3] and vertex[(edge+2)%3].
    int vi1 = tA.vertexIdx[(edgeIdx + 1) % 3];
    int vi2 = tA.vertexIdx[(edgeIdx + 2) % 3];
    if (vi1 >= s_walkmesh.numVertices || vi2 >= s_walkmesh.numVertices) return false;
    float x1 = (float)s_walkmesh.vertices[vi1].x;
    float y1 = (float)s_walkmesh.vertices[vi1].y;
    float x2 = (float)s_walkmesh.vertices[vi2].x;
    float y2 = (float)s_walkmesh.vertices[vi2].y;
    // v05.96: Determine left/right using the center of triB (the "far" triangle).
    // We need left/right as seen by a traveler standing in triA looking through
    // the portal toward triB. The center of triB is always unambiguously on one
    // side of the shared edge — unlike the travel direction, which can be nearly
    // parallel to the edge in long corridors, causing near-zero cross products.
    //
    // Cross product of (v1→v2) × (v1→triB_center) tells us which side triB is on.
    // The SSFA convention: looking from apex through the funnel, LEFT is the
    // side that triB is NOT on (the wall side of triA), and RIGHT is the other.
    // Actually the standard: LEFT boundary = left side of corridor from traveler.
    // triB center is on the "forward" side. We pick left/right so that the
    // corridor interior (triB side) is between them.
    //
    // If (v1→v2) × (v1→Bcenter) > 0, Bcenter is LEFT of v1→v2.
    //   So v1 is the RIGHT boundary, v2 is the LEFT boundary.
    // If < 0, Bcenter is RIGHT of v1→v2.
    //   So v1 is the LEFT boundary, v2 is the RIGHT boundary.
    float toBX = tB.centerX - x1;
    float toBY = tB.centerY - y1;
    float edgeDX = x2 - x1;
    float edgeDY = y2 - y1;
    float cross = edgeDX * toBY - edgeDY * toBX;
    if (cross > 0) {
        // triB center is to the LEFT of v1→v2.
        // v2 = left boundary, v1 = right boundary.
        leftX = x2; leftY = y2;
        rightX = x1; rightY = y1;
    } else {
        // triB center is to the RIGHT of v1→v2.
        // v1 = left boundary, v2 = right boundary.
        leftX = x1; leftY = y1;
        rightX = x2; rightY = y2;
    }
    return true;
}

// v0.17.5.2: Iterative collinear waypoint pruning.
//
// The SSFA funnel can emit many micro-corner waypoints when the walkmesh
// corridor has multiple small triangle turns. v0.17.5.1 BAT on bg2f_2
// (classroom hallway) showed a 1600-unit path produce 13 waypoints; checking
// each one's perpendicular distance from the line through its neighbors:
//
//   wp 1 = 220 units off line wp 0->wp 4   <- real corner (keep)
//   wp 2 =   6 units off line wp 1->wp 4   <- nearly collinear (remove)
//   wp 3 =  24 units off line wp 1->wp 4   <- nearly collinear (remove)
//   wp 4 =  30 units off line wp 1->wp 5   <- nearly collinear (remove)
//   ...
//
// Aaron's perception: TTS rattling through micro-cardinal changes as each
// waypoint advances. Each micro-correction is a real walkmesh triangle
// portal corner, but doesn't represent a turn the player needs to make.
//
// This pass repeatedly scans the waypoint list and removes any waypoint B
// whose perpendicular distance from the segment connecting its neighbors A
// and C is below PRUNE_PERP_EPSILON. It's a simplified Ramer-Douglas-
// Peucker (no recursion; just sweep-until-stable).
//
// PRUNE_PERP_EPSILON is set conservatively below typical FF8 wall thickness
// (~100+ units) so post-prune paths cannot route through walls. Combined
// with the existing AGENT_RADIUS=30 portal shrinking that pre-pulls
// waypoints inward from walls, worst-case wall clearance after pruning is
// ~80 units -- still well within walkable space.
//
// First and last waypoints are preserved (loop runs i = 1 to count-2).
static const float PRUNE_PERP_EPSILON = 50.0f;

// v0.17.8.19.1: Protected waypoint positions populated by FunnelPath's
// wall-parallel-portal COLLAPSE branch. Those waypoints are constraint-
// forced -- the path MUST aim at them so the player threads a narrow
// doorway rather than sliding along its parallel wall. Without protection
// they are geometrically near-collinear with their neighbors (the whole
// point of a doorway through a wall is that it sits ON the corridor line)
// and would be deleted by PruneCollinearWaypoints. Removing them regresses
// v0.16.1.2's COLLAPSE fix and reproduces the v0.16.1.1 stuck-at-wall
// behavior on domt2_1 (chase catch by battleyarou at the south exit) and
// any other field with a wall-parallel exit portal.
//
// Cleared at the top of every FunnelPath call so the protection list is
// fresh per re-path; populated in the COLLAPSE branch with the post-
// AGENT_RADIUS-shift midpoint that the funnel emits as a forced waypoint;
// consulted in PruneCollinearWaypoints which skips any candidate-for-prune
// whose position lies within PROTECTED_WP_EPSILON of a recorded entry.
//
// Sized to MAX_CORRIDOR because that's the upper bound on portals per
// path; in practice <=5 protected waypoints per field is typical, well
// under the threshold where preserving them would resurrect the TTS
// micro-corner spam that motivated PruneCollinearWaypoints' original
// design (hundreds of waypoints on bg2f_2's classroom hallway).
static float s_protectedWaypointPos[MAX_CORRIDOR][2];
static int   s_protectedWaypointCount = 0;
static const float PROTECTED_WP_EPSILON = 1.0f;

static int PruneCollinearWaypoints(float wp[][2], int count)
{
    if (count < 3) return count;
    bool changed = true;
    int iterations = 0;
    int totalRemoved = 0;
    int totalSkipped = 0;
    while (changed && iterations < 100) {  // safety bound
        changed = false;
        iterations++;
        for (int i = 1; i + 1 < count; i++) {
            float ax = wp[i-1][0], ay = wp[i-1][1];
            float bx = wp[i][0],   by = wp[i][1];
            float cx = wp[i+1][0], cy = wp[i+1][1];
            float abx = bx - ax, aby = by - ay;
            float acx = cx - ax, acy = cy - ay;
            float aclen = sqrtf(acx * acx + acy * acy);
            float perpDist;
            if (aclen < 0.001f) {
                // A and C are essentially the same point; B is redundant.
                perpDist = 0.0f;
            } else {
                float cross = abx * acy - aby * acx;
                perpDist = fabsf(cross) / aclen;
            }
            if (perpDist < PRUNE_PERP_EPSILON) {
                // v0.17.8.19.1: Skip if this waypoint is a protected
                // wall-parallel-portal COLLAPSE midpoint. See the
                // s_protectedWaypointPos declaration above for the full
                // rationale.
                bool isProtected = false;
                for (int p = 0; p < s_protectedWaypointCount; p++) {
                    float pdx = bx - s_protectedWaypointPos[p][0];
                    float pdy = by - s_protectedWaypointPos[p][1];
                    if (pdx*pdx + pdy*pdy < PROTECTED_WP_EPSILON*PROTECTED_WP_EPSILON) {
                        isProtected = true;
                        break;
                    }
                }
                if (isProtected) {
                    totalSkipped++;
                    continue;  // leave protected waypoint in place
                }
                // Remove B by shifting subsequent waypoints left.
                for (int k = i; k + 1 < count; k++) {
                    wp[k][0] = wp[k+1][0];
                    wp[k][1] = wp[k+1][1];
                }
                count--;
                totalRemoved++;
                changed = true;
                break;  // restart scan with the smaller list
            }
        }
    }
    if (totalRemoved > 0 || totalSkipped > 0) {
        Log::Field("FieldNavigation: [funnel-prune] removed %d collinear waypoints "
                   "(eps=%.0f units, %d sweeps, %d protected wall-parallel midpoints preserved)",
                   totalRemoved, PRUNE_PERP_EPSILON, iterations, totalSkipped);
    }
    return count;
}

static void FunnelPath(float startX, float startY, float goalX, float goalY)
{
    // v0.17.8.19.1: Fresh protected-waypoint list per call. The COLLAPSE
    // branch below populates it with wall-parallel doorway midpoints so
    // PruneCollinearWaypoints (called at the bottom of this function)
    // won't delete them.
    s_protectedWaypointCount = 0;

    // Build portal list from corridor.
    // Each portal is the shared edge between corridor[i] and corridor[i+1].
    struct Portal { float lx, ly, rx, ry; };
    static Portal portals[MAX_CORRIDOR];
    int numPortals = 0;

    // v06.01: New portal pipeline — wall-parallel skip + agent-radius shrinking.
    // Validated offline against all 894 game walkmeshes.
    //
    // Step 1: Skip wall-parallel portals. A portal is wall-parallel when both
    // endpoints lie on the same wall line — one axis has near-zero span AND the
    // other has significant span. These portals run ALONG a wall, not across
    // the walkable corridor. The funnel can't form proper L/R bounds from them.
    //   - bg2f_1 corridor: portals at X=165 and X=325 (dX=0, dY=300+) are wall-parallel
    //   - bgroom_1 classroom: diagonal edges with dX=1-2 are NOT wall-parallel
    //   - Epsilon=1.0 with 10x ratio test cleanly separates these cases
    //
    // Step 2: Shrink surviving portals inward by AGENT_RADIUS on each end.
    // This keeps the SSFA path away from walls by the character's collision
    // radius. Turn points land agent_radius units from the wall instead of
    // exactly on wall corners. Replaces the old wall-margin post-processing.
    static const float WALL_PARALLEL_EPSILON = 1.0f;
    static const float AGENT_RADIUS = 30.0f;  // character collision half-width
    int degenerateSkipped = 0;

    for (int i = 0; i + 1 < s_corridorCount && numPortals < MAX_CORRIDOR; i++) {
        float lx, ly, rx, ry;
        if (FindPortal(s_corridor[i], s_corridor[i+1], lx, ly, rx, ry)) {
            // Wall-parallel check: one axis < epsilon AND other > 10*epsilon.
            float absDX = fabsf(lx - rx);
            float absDY = fabsf(ly - ry);
            // v06.02: Only check vertical wall-parallel (dX near zero).
            // Horizontal portals (dY near zero, dX large) span ACROSS the
            // corridor and are always valid. The old dY<epsilon check caused
            // false positives on bg2f_1 (dX=209 dY=0 portal at Y=-2342).
            bool wallParallel = (absDX < WALL_PARALLEL_EPSILON && absDY > WALL_PARALLEL_EPSILON * 10.0f);
            if (wallParallel) {
                // v0.16.1.2 Fix B: COLLAPSE wall-parallel portals to a single
                // waypoint instead of SKIPPING them. The v0.16.1.1 diagnostic
                // BAT on the X-ATM092 chase identified the SKIP behavior as
                // the root cause of the deterministic doopen2a catch: on
                // domt2_1, the wall-parallel portal between tri 26 and tri 27
                // at x=-42 (L=(-42,-1638), R=(-42,-1360)) is the ONLY exit
                // from tri 26 going south. Skipping it left the player with
                // no aim point inside the doorway, so they slid along the
                // x=-42 wall accumulating moveDist=160 over 2s with zero net
                // displacement, then froze entirely (moveDist=0) for another
                // 2s before velocity-stuck recovery advanced wp 23 -> 24 ->
                // 25 -> 26 over 5 seconds total. Without that 5s, the chase
                // session timer (~51s total budget) has enough headroom for
                // the party to reach doopen2a's south trigger before BATTLE.
                //
                // The new behavior emits a single "forced waypoint" at the
                // portal midpoint, offset toward triB's center by
                // AGENT_RADIUS. The funnel algorithm treats L == R as a
                // pass-through constraint, which forces the player to aim
                // into the doorway and cross the wall.
                //
                // SKIP_WALL_PARALLEL_LEGACY toggle (Fix A fallback): if Fix B
                // regresses on bg2f_1 or other fields where the wall-parallel
                // skip was the right call (e.g. long open corridors with
                // inner-wall edges that the v0.15-era SKIP heuristic was
                // tuned against), flip this constant to true to restore the
                // v0.16.1.1 "continue" behavior globally. The toggle is
                // intentionally static const so a one-line flip + rebuild
                // is the quickest mitigation; if a per-field toggle becomes
                // necessary we can lift it to a route-config field later.
                static const bool SKIP_WALL_PARALLEL_LEGACY = false;
                if (SKIP_WALL_PARALLEL_LEGACY) {
                    degenerateSkipped++;
                    Log::Field("FieldNavigation: [funnel] SKIP wall-parallel portal %d (LEGACY) "
                               "dX=%.1f dY=%.1f L=(%.0f,%.0f) R=(%.0f,%.0f) tri %d->%d",
                               i, absDX, absDY, lx, ly, rx, ry,
                               (int)s_corridor[i], (int)s_corridor[i+1]);
                    continue;
                }
                // Fix B: collapse to a single waypoint offset toward triB.
                float mx = (lx + rx) / 2.0f;
                float my = (ly + ry) / 2.0f;
                uint16_t triB = s_corridor[i+1];
                if (triB < (uint16_t)s_walkmesh.numTriangles) {
                    float toBx = s_walkmesh.triangles[triB].centerX - mx;
                    float toBy = s_walkmesh.triangles[triB].centerY - my;
                    float toBlen = sqrtf(toBx*toBx + toBy*toBy);
                    if (toBlen > 0.001f) {
                        mx += (toBx / toBlen) * AGENT_RADIUS;
                        my += (toBy / toBlen) * AGENT_RADIUS;
                    }
                }
                portals[numPortals].lx = mx;
                portals[numPortals].ly = my;
                portals[numPortals].rx = mx;
                portals[numPortals].ry = my;
                // v0.17.8.19.1: Record this midpoint as a protected
                // waypoint position so PruneCollinearWaypoints doesn't
                // delete it later. Without this, the funnel emits the
                // collapsed point correctly but the prune pass (added in
                // v0.17.5.2) removes it because it's near-collinear with
                // its neighbors in the path -- which is the entire reason
                // it's a useful constraint, but defeats v0.16.1.2's fix.
                if (s_protectedWaypointCount < MAX_CORRIDOR) {
                    s_protectedWaypointPos[s_protectedWaypointCount][0] = mx;
                    s_protectedWaypointPos[s_protectedWaypointCount][1] = my;
                    s_protectedWaypointCount++;
                }
                numPortals++;
                degenerateSkipped++;  // reused as "encountered" counter for the summary log
                Log::Field("FieldNavigation: [funnel] COLLAPSE wall-parallel portal %d "
                           "dX=%.1f dY=%.1f L=(%.0f,%.0f) R=(%.0f,%.0f) -> wp=(%.0f,%.0f) tri %d->%d",
                           i, absDX, absDY, lx, ly, rx, ry, mx, my,
                           (int)s_corridor[i], (int)s_corridor[i+1]);
                continue;
            }
            // Shrink portal inward by AGENT_RADIUS on each end.
            // Direction from left to right endpoint.
            float edgeLen = sqrtf(absDX*absDX + absDY*absDY);
            if (edgeLen <= AGENT_RADIUS * 2.0f) {
                // Portal too narrow — collapse to midpoint.
                float mx = (lx + rx) / 2.0f;
                float my = (ly + ry) / 2.0f;
                portals[numPortals].lx = mx;
                portals[numPortals].ly = my;
                portals[numPortals].rx = mx;
                portals[numPortals].ry = my;
            } else {
                float nx = (rx - lx) / edgeLen;
                float ny = (ry - ly) / edgeLen;
                portals[numPortals].lx = lx + nx * AGENT_RADIUS;
                portals[numPortals].ly = ly + ny * AGENT_RADIUS;
                portals[numPortals].rx = rx - nx * AGENT_RADIUS;
                portals[numPortals].ry = ry - ny * AGENT_RADIUS;
            }
            numPortals++;
        }
    }
    if (degenerateSkipped > 0) {
        Log::Field("FieldNavigation: [funnel] %d wall-parallel portals processed "
                   "(SKIP if SKIP_WALL_PARALLEL_LEGACY else COLLAPSE; v0.16.1.2 default = COLLAPSE)",
                   degenerateSkipped);
    }
    // Add a degenerate portal at the goal (both sides = goal point).
    if (numPortals < MAX_CORRIDOR) {
        portals[numPortals].lx = goalX;
        portals[numPortals].ly = goalY;
        portals[numPortals].rx = goalX;
        portals[numPortals].ry = goalY;
        numPortals++;
    }

    if (numPortals == 0) return;  // no portals, keep triangle-center path

    // v05.94: Diagnostic — log first 20 portals to verify left/right ordering.
    for (int d = 0; d < numPortals && d < 20; d++) {
        Log::Field("FieldNavigation: [funnel] portal %d/%d L=(%.0f,%.0f) R=(%.0f,%.0f) tri %d->%d",
                   d, numPortals,
                   portals[d].lx, portals[d].ly,
                   portals[d].rx, portals[d].ry,
                   (d < s_corridorCount) ? (int)s_corridor[d] : -1,
                   (d+1 < s_corridorCount) ? (int)s_corridor[d+1] : -1);
    }

    // SSFA: walk the portals maintaining a funnel.
    float apexX = startX, apexY = startY;
    float funnelLX = startX, funnelLY = startY;
    float funnelRX = startX, funnelRY = startY;
    int apexIdx = 0, leftIdx = 0, rightIdx = 0;

    float result[MAX_WAYPOINTS][2];
    int resultCount = 0;

    for (int i = 0; i < numPortals; i++) {
        float pLX = portals[i].lx, pLY = portals[i].ly;
        float pRX = portals[i].rx, pRY = portals[i].ry;

        // Update right vertex.
        if (Cross2D(apexX, apexY, funnelRX, funnelRY, pRX, pRY) <= 0.0f) {
            if ((apexX == funnelRX && apexY == funnelRY) ||
                Cross2D(apexX, apexY, funnelLX, funnelLY, pRX, pRY) > 0.0f) {
                // Tighten the funnel.
                funnelRX = pRX; funnelRY = pRY;
                rightIdx = i;
            } else {
                // Right crosses left — left becomes new apex.
                if (resultCount < MAX_WAYPOINTS) {
                    result[resultCount][0] = funnelLX;
                    result[resultCount][1] = funnelLY;
                    resultCount++;
                }
                apexX = funnelLX; apexY = funnelLY;
                apexIdx = leftIdx;
                funnelRX = apexX; funnelRY = apexY;
                rightIdx = apexIdx;
                // Restart scan from apex portal.
                i = apexIdx;
                continue;
            }
        }

        // Update left vertex.
        if (Cross2D(apexX, apexY, funnelLX, funnelLY, pLX, pLY) >= 0.0f) {
            if ((apexX == funnelLX && apexY == funnelLY) ||
                Cross2D(apexX, apexY, funnelRX, funnelRY, pLX, pLY) < 0.0f) {
                // Tighten the funnel.
                funnelLX = pLX; funnelLY = pLY;
                leftIdx = i;
            } else {
                // Left crosses right — right becomes new apex.
                if (resultCount < MAX_WAYPOINTS) {
                    result[resultCount][0] = funnelRX;
                    result[resultCount][1] = funnelRY;
                    resultCount++;
                }
                apexX = funnelRX; apexY = funnelRY;
                apexIdx = rightIdx;
                funnelLX = apexX; funnelLY = apexY;
                leftIdx = apexIdx;
                // Restart scan from apex portal.
                i = apexIdx;
                continue;
            }
        }
    }

    // Add goal as final waypoint.
    if (resultCount < MAX_WAYPOINTS) {
        result[resultCount][0] = goalX;
        result[resultCount][1] = goalY;
        resultCount++;
    }

    // v06.01: Wall-margin post-processing REMOVED. Portal shrinking (AGENT_RADIUS)
    // handles wall clearance at the portal level, so waypoints are already
    // agent_radius units from walls. No need for expensive per-waypoint
    // wall-distance scanning.

    // Replace the A* triangle-center waypoints with the funnel result.
    int oldCount = s_waypointCount;
    memcpy(s_waypoints, result, sizeof(float) * 2 * resultCount);
    s_waypointCount = resultCount;
    // v0.17.8.19.2: Skip the prune for chase-drive. v0.17.5.2's prune
    // motivation (TTS micro-corner spam on bg2f_2 classroom) doesn't apply
    // to chase auto-pilot, which runs silent. Chase needs the dense funnel
    // output to navigate long Dollet corridors like domt2_1 -- v0.17.8.19.1
    // protected the COLLAPSE'd doorway midpoint but the prune still deleted
    // 24 of 32 OTHER intermediate waypoints, leaving the path so sparse
    // that the party wedged on tri 25 geometry between the doorway exit
    // (-13,-1508) and the next surviving waypoint (-306,-1919). v0.16.1.4
    // had 0 chase catches without any prune; restore that by gating the
    // call on !s_chaseDriveActive.
    //
    // Manual nav (F9 path-finding to NPCs) still runs the prune and keeps
    // the v0.17.5.2 TTS fix. The s_protectedWaypointPos mechanism added in
    // v0.17.8.19.1 stays in place for F9 path-finding that happens to cross
    // a wall-parallel-portal field (same scenario applies there if a player
    // F9s to an NPC behind a doorway).
    int prePruneCount = s_waypointCount;
    if (!s_chaseDriveActive) {
        s_waypointCount = PruneCollinearWaypoints(s_waypoints, s_waypointCount);
    } else {
        Log::Field("FieldNavigation: [funnel-prune] skipped for chase-drive "
                   "(%d waypoints kept; PRUNE_PERP_EPSILON=%.0f only applies "
                   "to F9 nav to avoid TTS micro-corner spam)",
                   s_waypointCount, PRUNE_PERP_EPSILON);
    }
    s_waypointIdx = 0;
    s_usingFunnel = true;  // v05.95: use tighter arrive distance for funnel waypoints

    Log::Field("FieldNavigation: [funnel] %d triangles -> %d waypoints "
               "(post-prune; pre-prune=%d, was %d centers)",
               s_corridorCount, s_waypointCount, prePruneCount, oldCount);
}

// v06.06: Edge-midpoint path generation — the reliable fallback for when funnel
// paths get the player stuck. Instead of the SSFA funnel (which produces optimal
// but sometimes tight waypoints near wall corners), this generates waypoints at
// the midpoints of shared edges between consecutive corridor triangles.
//
// Each midpoint is guaranteed to be on a walkable edge boundary, so steering
// toward it always aims the player through the "doorway" between triangles.
// The path is less smooth than funnel but never gets stuck on wall corners.
//
// Edge midpoints are shrunk inward by AGENT_RADIUS (same as funnel portal
// shrinking) to keep the player away from walls.
static const float EDGE_MIDPOINT_ARRIVE_DIST = 50.0f;  // tight arrive for precise waypoints

static void EdgeMidpointPath(float startX, float startY, float goalX, float goalY)
{
    if (!s_walkmesh.valid || s_corridorCount < 2) return;

    static const float AGENT_RADIUS_EM = 30.0f;
    float result[MAX_WAYPOINTS][2];
    int resultCount = 0;

    // For each pair of consecutive corridor triangles, find the shared edge
    // and place a waypoint at its midpoint (shrunk inward by agent radius).
    for (int i = 0; i + 1 < s_corridorCount && resultCount < MAX_WAYPOINTS - 1; i++) {
        uint16_t triA = s_corridor[i];
        uint16_t triB = s_corridor[i + 1];
        if (triA >= (uint16_t)s_walkmesh.numTriangles || triB >= (uint16_t)s_walkmesh.numTriangles)
            continue;

        const auto& tA = s_walkmesh.triangles[triA];
        // Find which edge of triA connects to triB.
        int edgeIdx = -1;
        for (int e = 0; e < 3; e++) {
            if (tA.neighbor[e] == triB) { edgeIdx = e; break; }
        }
        if (edgeIdx < 0) continue;

        // Shared edge connects vertex[(edge+1)%3] and vertex[(edge+2)%3].
        int vi1 = tA.vertexIdx[(edgeIdx + 1) % 3];
        int vi2 = tA.vertexIdx[(edgeIdx + 2) % 3];
        if (vi1 >= s_walkmesh.numVertices || vi2 >= s_walkmesh.numVertices) continue;

        float x1 = (float)s_walkmesh.vertices[vi1].x;
        float y1 = (float)s_walkmesh.vertices[vi1].y;
        float x2 = (float)s_walkmesh.vertices[vi2].x;
        float y2 = (float)s_walkmesh.vertices[vi2].y;

        // Midpoint of the shared edge.
        float mx = (x1 + x2) / 2.0f;
        float my = (y1 + y2) / 2.0f;

        // Shrink toward the corridor center (triB center) by AGENT_RADIUS.
        // This keeps the waypoint away from walls.
        float toCenterX = s_walkmesh.triangles[triB].centerX - mx;
        float toCenterY = s_walkmesh.triangles[triB].centerY - my;
        float toCenterLen = sqrtf(toCenterX * toCenterX + toCenterY * toCenterY);
        if (toCenterLen > 0.001f) {
            mx += (toCenterX / toCenterLen) * AGENT_RADIUS_EM;
            my += (toCenterY / toCenterLen) * AGENT_RADIUS_EM;
        }

        result[resultCount][0] = mx;
        result[resultCount][1] = my;
        resultCount++;
    }

    // Add goal as final waypoint.
    if (resultCount < MAX_WAYPOINTS) {
        result[resultCount][0] = goalX;
        result[resultCount][1] = goalY;
        resultCount++;
    }

    if (resultCount == 0) return;  // shouldn't happen

    // Replace waypoints.
    int oldCount = s_waypointCount;
    memcpy(s_waypoints, result, sizeof(float) * 2 * resultCount);
    s_waypointCount = resultCount;
    s_waypointIdx = 0;
    s_usingFunnel = true;  // use tight arrive distance (FUNNEL_ARRIVE_DIST)

    Log::Field("FieldNavigation: [edge-midpoint] %d triangles -> %d edge-midpoint waypoints (was %d)",
               s_corridorCount, resultCount, oldCount);
}

