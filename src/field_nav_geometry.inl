// field_nav_geometry.inl -- Orient2D, SegmentsCross and EdgeCrossesScreenBound.
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Also included by tests/chase_harness.cpp.
//
// EXTRACTED v0.55.0, and the reason is a dead gate. `EdgeCrossesScreenBound` is
// FORWARD-DECLARED in field_nav_pathfinding.inl and was DEFINED only here in
// field_navigation.cpp, which no host probe can compile -- so chase_harness,
// the offline A* harness built for #113, had stopped linking:
//
//     undefined reference to `FieldNavigation::EdgeCrossesScreenBound(...)'
//
// and the whole harness had been silently out of the gate set. Copying the
// function into the harness would have fixed the link and created a second
// copy of the A* barrier rule that can drift away from the shipped one, which
// is worse than no harness. So the three functions move into a file both sides
// include, and the harness tests the code the game runs.
//
// They belong together: SegmentsCross is Orient2D four times, and
// EdgeCrossesScreenBound is SegmentsCross over the captured-line table.


// v0.17.8.10: Proper bounded segment-vs-segment intersection. Returns true only
// when segment AB actually crosses segment CD (orientation test). Unlike the
// infinite-line side test in IsSeparatedByTriggerLine, a short line that lies
// off to one side does NOT count as a crossing. Used by the INF-gateway screen
// filter so a far-edge screen-boundary doorway no longer falsely "separates" a
// gateway on the opposite edge (the bug behind the missing bghall_5 -> Hall 4
// exit). Collinear/endpoint-touch cases return false: a gateway grazing a
// boundary endpoint is not "behind" it.
static int Orient2D(float px, float py, float qx, float qy, float rx, float ry)
{
    float v = (qx - px) * (ry - py) - (qy - py) * (rx - px);
    if (v >  1.0f) return  1;
    if (v < -1.0f) return -1;
    return 0;
}
static bool SegmentsCross(float ax, float ay, float bx, float by,
                          float cx, float cy, float dx, float dy)
{
    int o1 = Orient2D(ax, ay, bx, by, cx, cy);
    int o2 = Orient2D(ax, ay, bx, by, dx, dy);
    int o3 = Orient2D(cx, cy, dx, dy, ax, ay);
    int o4 = Orient2D(cx, cy, dx, dy, bx, by);
    return (o1 != o2 && o3 != o4 && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0);
}

// v0.17.9.15: Local bounded screen-bound crossing test for A* avoidance.
// Forward-declared in field_nav_pathfinding.inl. Unlike IsSeparatedByTriggerLine
// (which side-splits an INFINITE line from the start centre, so a short
// screen-bound segment near the spawn fences off the whole far side of the
// field via its extension), this returns true only when the actual traversal
// edge (ax,ay)->(bx,by) crosses a screen-bound line's FINITE segment, via the
// bounded SegmentsCross. ComputeAStarPath calls it per current->neighbor
// expansion so A* routes AROUND a boundary instead of being globally walled
// off by the boundary's infinite extension. Same filter as
// IsSeparatedByTriggerLine (only SCREEN_BOUND / UNKNOWN lines are barriers;
// camera-pans and interactive lines are transparent) and the same
// skipTriggerIdx exemption (used when driving TO a screen transition).
// Validated on bcsaka_1 (Balamb Hotel): the global test gives No path
// tri 13->196; this gives the 65-triangle route. nav-core shared with the
// Dollet chase -> gated on a chase-first BAT. See DEVNOTES "Track A Step 2".
static bool EdgeCrossesScreenBound(float ax, float ay, float bx, float by, int skipTriggerIdx)
{
    return EdgeCrossesScreenBoundEx(ax, ay, bx, by, skipTriggerIdx, s_driveSkipGatewayIdx);
}

// v0.132.0 (#shumi): the same test, plus the field's INF gateways.
//
// An INF gateway is a doorway the engine watches for itself -- it is not a
// SETLINE line and has never been in s_capturedLines, so until now A* could plan
// a route straight through one. That is how a drive to a Moomba in Shumi Village
// 2 delivered Aaron to Village 3: the only walkable route to triangle 3 crosses
// the Village 3 gateway, and nothing stopped it. See gateway_avoidance_model.inl.
static bool EdgeCrossesScreenBoundEx(float ax, float ay, float bx, float by,
                                     int skipTriggerIdx, int skipGatewayIdx)
{
    for (int g = 0; g < s_gatewayCount && g < MAX_GATEWAYS; g++) {
        if (!GatewayIsPathBarrier(g, skipGatewayIdx)) continue;
        const int x1 = (int)s_gateways[g].lineX1, y1 = (int)s_gateways[g].lineY1;
        const int x2 = (int)s_gateways[g].lineX2, y2 = (int)s_gateways[g].lineY2;
        if (!GatewayLineIsUsable(x1, y1, x2, y2)) continue;
        if (SegmentsCross(ax, ay, bx, by, (float)x1, (float)y1, (float)x2, (float)y2))
            return true;
    }
    for (int t = 0; t < s_capturedLineCount; t++) {
        if (!s_capturedLines[t].active) continue;
        if (s_capturedLines[t].gateClosed) continue;   // v0.62.3: inert, not a wall
        if (t == skipTriggerIdx) continue;
        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
            s_capturedLines[t].lineType != FieldArchive::JSM_ENT_UNKNOWN)
            continue;
        if (SegmentsCross(ax, ay, bx, by,
                          (float)s_capturedLines[t].x1, (float)s_capturedLines[t].y1,
                          (float)s_capturedLines[t].x2, (float)s_capturedLines[t].y2))
            return true;
    }
    return false;
}
