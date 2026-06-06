// field_nav_gps.inl — GPS guided navigation
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.
// v0.17.0: Camera-projected screen-relative directions. The old version
// computed direction as the world-space bearing atan2(dx, dy) from raw
// entity coordinates, which is correct ONLY on default-camera fields.
// On rotated cameras (any field where the camera looks at the walkmesh
// from a non-default angle) the walkmesh axes do not align with the
// screen, so the announced direction would not match the arrow key the
// player needs to press. We now project the walkmesh delta through the
// per-field camera axes derived from the .ca file (see field_nav_fieldscripts.inl
// for the s_camRightX/Y/DownX/DownY wiring at field load) and label the
// resulting screen-space angle with a cardinal direction that maps
// directly to the corresponding arrow key.
// v0.17.0.1: Normalize the 2D projection of axis0/axis1 to unit length in
// field_nav_fieldscripts.inl. v0.17.0 used the raw XY components after the
// /4096 normalization of the 3D unit vector, but on tilted cameras most of
// axis1's magnitude is in the Z component, so the 2D projection was short
// (e.g. bghall_1 had camDown 2D-magnitude 0.333). Asymmetric magnitudes
// biased atan2(sD, sR) toward east/west and produced the wrong cardinal.
// v0.17.1: Path-aware direction. v0.17.0/0.17.0.1 fixed the orientation
// layer (the cardinal is now genuinely screen-relative on any camera). But
// the announced direction was still the straight-line bearing from player
// to final destination, which is wrong on curved hallways — the bearing
// cuts through walls and points the player into geometry they can't walk
// through. v0.17.1 runs A* + funnel from the player's triangle to the
// target's triangle (reusing field_nav_pathfinding.inl) and announces the
// cardinal toward the NEXT waypoint, not the final destination. Waypoints
// advance as the player walks past them. On straight paths the funnel
// produces a single waypoint at the destination, so behavior is identical
// to v0.17.0.1; on curved paths the player gets corner-by-corner guidance.
// On fields where A* can't find a path (disconnected walkmesh islands,
// no walkmesh loaded, target not on walkmesh) we fall back to the
// straight-line v0.17.0.1 behavior.

// ============================================================================
// GPS Guidance (v0.12.02, projection rewritten v0.17.0, path-aware v0.17.1)
// ============================================================================

// v0.17.0: Track the last-announced cardinal sector and step count so we can
// re-announce on direction changes (sector crossings) and step-count changes
// rather than just time intervals. Reset by StartGPS().
static int s_gpsLastDirIdx   = -1;
static int s_gpsLastStepsAnn = -1;

// v0.17.5.1: Hysteresis on cardinal change. Aaron's v0.17.5 BAT showed two
// related issues:
//  1. "Direction / distance was constantly rattling off and spamming the TTS":
//     the v0.17.0 cadence fires on every cardinal sector boundary crossing and
//     every step-count change, which during a long walk produces back-to-back
//     announcements (one per cardinal flip near a sector boundary, plus one
//     each time the integer step count ticks down). The throttle
//     (GPS_ANNOUNCE_INTERVAL_FAR = 3s) only gates step changes, not direction
//     changes -- so the flood survives.
//  2. On bg2f_1 (the C-shaped classroom hallway), at one point the announcer
//     said "south" when Aaron needed "north". Looking at the geometry, the
//     L-shaped path from entry to door cannot have a south-pointing leg with
//     correctly-quantized axes (RIGHT->world-north, DOWN->world-east; pressing
//     DOWN would move the player away from the door), so this was almost
//     certainly a transient sector flip near a waypoint corner -- a single
//     bad announcement symptomatic of the same rattle.
//
// v0.17.5.1 rule: announce ONLY when the cardinal CHANGES and the new
// cardinal has held steady for GPS_DIR_HYSTERESIS_MS. Step changes never
// trigger an announcement on their own. Waypoint advances never trigger an
// announcement on their own (if the cardinal happens to match across two
// waypoint legs, the conceptual handoff is silent -- Aaron just keeps
// walking the same direction).
//
// `s_gpsPendingDirIdx` is the cardinal the player most recently computed,
// which differs from `s_gpsLastDirIdx` (the cardinal last spoken). When
// pending == last, nothing to do. When pending != last, we wait until
// pending has held the same value for GPS_DIR_HYSTERESIS_MS, then promote
// it to last and speak.
static int   s_gpsPendingDirIdx   = -1;
static DWORD s_gpsPendingDirSince = 0;
static const DWORD GPS_DIR_HYSTERESIS_MS = 500;

// v0.17.1: Path-aware direction state. Filled by BuildGpsPath() at StartGPS
// time using A* + funnel. UpdateGPS reads the current waypoint as the steering
// target and advances when the player gets close. When s_gpsUseWaypoints is
// false, behavior degrades to v0.17.0.1's straight-line direction.
//
// The buffer is separate from the shared s_waypoints[] used by UpdateAutoDrive
// so the two features don't trip over each other. BuildGpsPath save/restores
// the shared state around its A* call; see that function for the mechanism.
static const int   MAX_GPS_WAYPOINTS = 64;  // typical funnel paths are far smaller
static float       s_gpsWaypoints[MAX_GPS_WAYPOINTS][2] = {};
static int         s_gpsWaypointCount = 0;
static int         s_gpsWaypointIdx   = 0;
static bool        s_gpsUseWaypoints  = false;
// Distance at which we advance to the next waypoint. Auto-drive uses
// 60 units (FUNNEL_ARRIVE_DIST) because analog steering needs to hit
// precise turn points. The player walking themselves doesn't need that
// precision — generous threshold means the announced direction advances
// to the next leg BEFORE the player actually reaches the exact corner,
// giving them time to plan their next move.
static const float GPS_WP_ARRIVE_DIST    = 200.0f;
// Overshoot detection: when the player passes a waypoint at distance close
// to the arrive threshold without ever quite getting under it (common when
// they walk past at an angle), advance when distance starts increasing.
static const float GPS_WP_OVERSHOOT_CLOSE = 300.0f;  // must get within this distance first
static const float GPS_WP_OVERSHOOT_RATIO = 1.5f;
static const float GPS_WP_OVERSHOOT_MARGIN = 50.0f;
static float       s_gpsWpMinDist = 1e30f;  // min dist to current wp (reset on wp advance)

// Get the target position for a catalog entry.
// Returns true if a valid position was found.
static bool GetCatalogTargetPos(int catalogIdx, float& outX, float& outY)
{
    if (catalogIdx < 0 || catalogIdx >= s_catalogCount) return false;
    const EntityInfo& tgt = s_catalog[catalogIdx];

    if (tgt.entityIdx <= -400) {
        // INF gateway exit
        int gwIdx = -(tgt.entityIdx + 400);
        if (gwIdx < 0 || gwIdx >= s_dedupGatewayCount) return false;
        outX = s_dedupGateways[gwIdx].centerX;
        outY = s_dedupGateways[gwIdx].centerY;
        return true;
    } else if (tgt.entityIdx <= -300) {
        // JSM-injected entity
        int jsmIdx = -(tgt.entityIdx + 300);
        if (jsmIdx < 0 || jsmIdx >= s_jsmEntityCount || !s_jsmEntities[jsmIdx].hasPosition) return false;
        outX = (float)s_jsmEntities[jsmIdx].posX;
        outY = (float)s_jsmEntities[jsmIdx].posY;
        return true;
    } else if (tgt.entityIdx <= -200) {
        // Trigger line
        int trigIdx = -(tgt.entityIdx + 200);
        if (trigIdx < 0 || trigIdx >= s_capturedLineCount) return false;
        outX = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
        outY = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
        return true;
    } else if (tgt.entityIdx >= 0 && tgt.entityIdx != s_playerEntityIdx) {
        return GetEntityPos(tgt.entityIdx, outX, outY);
    }
    return false;
}

// v0.17.0: Compute 8-way cardinal direction (screen-relative).
//
// Walkmesh-space delta (dx, dy) is projected through the field's camera axes
// to get a screen-space delta (sR = screen-right component, sD = screen-down
// component), then classified into one of 8 cardinal sectors (45° each).
//
// Cardinals are screen-relative and map directly to arrow keys:
//   north = up arrow, east = right, south = down, west = left.
// Diagonals follow naturally: northeast = up+right, etc.
//
// Index ordering (used by GPS_DIR_NAMES):
//   0 = north, 1 = northeast, 2 = east,  3 = southeast,
//   4 = south, 5 = southwest, 6 = west,  7 = northwest.
//
// Math: atan2(sD, sR) yields an angle in [-π, π] with the convention
// sR>0,sD=0 → angle=0 → east. To get index 0 = north (sD<0, sR=0 → -π/2),
// shift by +π/2 so north=0, east=π/2, south=π, west=3π/2. Then snap to
// the nearest 45° sector using a +π/8 (half-sector) bias before dividing.
//
// Out-of-band sR/sD values (player and target at same location) return north
// as a benign default — callers should guard with a distance check.
static int ComputeScreenDirIndex(float dx, float dy)
{
    float sR = dx * s_camRightX + dy * s_camRightY;
    float sD = dx * s_camDownX  + dy * s_camDownY;
    if (sR == 0.0f && sD == 0.0f) return 0;  // benign default

    double angle = atan2((double)sD, (double)sR);
    double shifted = angle + NAV_PI * 0.5;
    if (shifted < 0) shifted += 2.0 * NAV_PI;
    int idx = (int)((shifted + NAV_PI / 8.0) / (NAV_PI / 4.0)) % 8;
    if (idx < 0) idx += 8;
    if (idx > 7) idx = 7;
    return idx;
}

// v0.17.1: Build a path-aware waypoint sequence from player to target.
//
// Runs A* on the walkmesh from the triangle nearest the player to the
// triangle nearest the target, then funnel-smooths the corridor into
// turn-point waypoints. Result is copied into the GPS-private buffer
// (s_gpsWaypoints[]) so subsequent path-aware steering reads it directly.
//
// The A* / funnel functions in field_nav_pathfinding.inl write to the
// shared s_waypoints[], s_waypointCount, s_waypointIdx, s_corridor[], and
// s_corridorCount variables, which are also used by UpdateAutoDrive. To
// avoid clobbering an active auto-drive's path, this function snapshots
// those variables before the A* call and restores them after copying the
// funnel result into the GPS buffer.
//
// Trigger-line target handling: if the target's catalog entry is a trigger
// line (entityIdx <= -200), pass its trigger index as skipTriggerIdx so A*
// is allowed to route through that specific trigger line (it's the goal).
// Runtime-entity targets (entityIdx >= 0) pass their entity index as
// targetEntityIdx so A*'s push-radius blackout doesn't block the target's
// own triangle.
//
// Returns true if a valid waypoint sequence was built. Returns false if
// the walkmesh isn't loaded, A* can't find a path, or the result is empty.
// On false, callers fall back to v0.17.0.1's straight-line direction.
static bool BuildGpsPath(float px, float py, float tx, float ty,
                         int targetEntityIdx, int skipTriggerIdx)
{
    s_gpsWaypointCount = 0;
    s_gpsWaypointIdx   = 0;
    s_gpsUseWaypoints  = false;
    s_gpsWpMinDist     = 1e30f;

    if (!s_walkmesh.valid || s_walkmesh.numTriangles == 0) {
        Log::Field("FieldNavigation: [NAV-PATH] No walkmesh for GPS path; straight-line fallback.");
        return false;
    }

    int startTri = FindNearestTriangle(px, py);
    int goalTri  = FindNearestTriangle(tx, ty);
    if (startTri < 0 || goalTri < 0) {
        Log::Field("FieldNavigation: [NAV-PATH] FindNearestTriangle returned -1 "
                   "(startTri=%d goalTri=%d player=(%.0f,%.0f) target=(%.0f,%.0f)); "
                   "straight-line fallback.",
                   startTri, goalTri, px, py, tx, ty);
        return false;
    }
    if (startTri == goalTri) {
        // Player and target are on the same walkmesh triangle. The straight-
        // line direction is already correct; no benefit to running A*. Single
        // waypoint at the target so the rest of the GPS code can run uniformly.
        s_gpsWaypoints[0][0] = tx;
        s_gpsWaypoints[0][1] = ty;
        s_gpsWaypointCount = 1;
        s_gpsWaypointIdx   = 0;
        s_gpsUseWaypoints  = true;
        Log::Field("FieldNavigation: [NAV-PATH] Player and target on same tri %d; "
                   "single-waypoint path (straight-line direction).", startTri);
        return true;
    }
    if (!AreTrianglesConnected(startTri, goalTri)) {
        Log::Field("FieldNavigation: [NAV-PATH] startTri %d and goalTri %d are on "
                   "disconnected walkmesh islands; straight-line fallback.",
                   startTri, goalTri);
        return false;
    }

    // Snapshot the shared state that A* + funnel will overwrite. We need
    // to restore exactly this much to leave UpdateAutoDrive unaffected:
    //   s_waypoints[][], s_waypointCount, s_waypointIdx, s_usingFunnel,
    //   s_corridor[], s_corridorCount, s_wpMinDist.
    //
    // The corridor array is large (MAX_CORRIDOR = 4096 uint16_t = 8 KB) so
    // we use file-scope statics to avoid blowing the stack. These statics
    // are only used inside BuildGpsPath, never read elsewhere.
    static float    s_savedWaypoints[MAX_WAYPOINTS][2];
    static uint16_t s_savedCorridor[MAX_CORRIDOR];
    int             savedWpCount   = s_waypointCount;
    int             savedWpIdx     = s_waypointIdx;
    bool            savedFunnel    = s_usingFunnel;
    int             savedCorrCount = s_corridorCount;
    float           savedWpMinDist = s_wpMinDist;
    if (savedWpCount > 0) {
        memcpy(s_savedWaypoints, s_waypoints, sizeof(float) * 2 * savedWpCount);
    }
    if (savedCorrCount > 0) {
        memcpy(s_savedCorridor, s_corridor, sizeof(uint16_t) * savedCorrCount);
    }

    bool ok = ComputeAStarPath(startTri, goalTri, targetEntityIdx, skipTriggerIdx);
    if (ok) {
        FunnelPath(px, py, tx, ty);
        // Copy the funnel result (which is now in s_waypoints[]) into the
        // GPS-private buffer.
        int copyCount = s_waypointCount;
        if (copyCount > MAX_GPS_WAYPOINTS) copyCount = MAX_GPS_WAYPOINTS;
        for (int i = 0; i < copyCount; ++i) {
            s_gpsWaypoints[i][0] = s_waypoints[i][0];
            s_gpsWaypoints[i][1] = s_waypoints[i][1];
        }
        s_gpsWaypointCount = copyCount;

        // Advance past any waypoints already within arrive distance of the
        // player. This handles the case where the funnel's first waypoint
        // is essentially the player's current position (e.g. when the player
        // is right at a corner). Without this, the GPS would announce a
        // direction toward a waypoint the player is effectively standing on,
        // jitter for one tick, then advance.
        while (s_gpsWaypointIdx < s_gpsWaypointCount - 1) {
            float wpDx = s_gpsWaypoints[s_gpsWaypointIdx][0] - px;
            float wpDy = s_gpsWaypoints[s_gpsWaypointIdx][1] - py;
            float wpDist = sqrtf(wpDx * wpDx + wpDy * wpDy);
            if (wpDist >= GPS_WP_ARRIVE_DIST) break;
            s_gpsWaypointIdx++;
        }

        s_gpsUseWaypoints = (s_gpsWaypointCount > 0);
        Log::Field("FieldNavigation: [NAV-PATH] A*+funnel produced %d waypoints, "
                   "startTri=%d goalTri=%d, starting at wp %d/%d.",
                   s_gpsWaypointCount, startTri, goalTri,
                   s_gpsWaypointIdx, s_gpsWaypointCount);
        // Log the first few waypoints so direction issues can be traced.
        int dumpN = (s_gpsWaypointCount < 6) ? s_gpsWaypointCount : 6;
        for (int i = 0; i < dumpN; ++i) {
            Log::Field("FieldNavigation: [NAV-PATH] wp %d=(%.0f,%.0f)",
                       i, s_gpsWaypoints[i][0], s_gpsWaypoints[i][1]);
        }
    } else {
        Log::Field("FieldNavigation: [NAV-PATH] A* failed (startTri=%d goalTri=%d); "
                   "straight-line fallback.", startTri, goalTri);
    }

    // Restore shared state so an active auto-drive keeps its path. If no
    // auto-drive is running (savedWpCount == 0), this is effectively a
    // reset of the shared state, which is harmless because UpdateAutoDrive
    // won't read it unless s_driveActive is true.
    s_waypointCount = savedWpCount;
    s_waypointIdx   = savedWpIdx;
    s_usingFunnel   = savedFunnel;
    s_corridorCount = savedCorrCount;
    s_wpMinDist     = savedWpMinDist;
    if (savedWpCount > 0) {
        memcpy(s_waypoints, s_savedWaypoints, sizeof(float) * 2 * savedWpCount);
    }
    if (savedCorrCount > 0) {
        memcpy(s_corridor, s_savedCorridor, sizeof(uint16_t) * savedCorrCount);
    }

    return s_gpsUseWaypoints;
}

// v0.17.1: Per-tick waypoint advance. Tracks the player's progress along the
// path-aware waypoint sequence. Returns the (sx, sy) the cardinal should aim
// at (current waypoint position, or the final destination if no waypoints).
//
// Advance conditions, in priority order:
//   (a) Within GPS_WP_ARRIVE_DIST of the current waypoint AND we still have
//       waypoints ahead — advance to the next one.
//   (b) Overshoot: we got reasonably close (< GPS_WP_OVERSHOOT_CLOSE) and now
//       distance is growing again (passed the waypoint at an angle). Advance
//       even though we never hit the arrive threshold. This catches the
//       common case where the player cuts a corner.
//
// The final waypoint (== destination) is never advanced past; the existing
// distance-to-destination "Nearby" and "In range" checks handle arrival.
static void AdvanceGpsWaypoint(float px, float py, float& sx, float& sy)
{
    if (!s_gpsUseWaypoints || s_gpsWaypointCount == 0) return;
    if (s_gpsWaypointIdx >= s_gpsWaypointCount) {
        // Defensive: shouldn't happen, but if it does, just steer at the
        // last known waypoint.
        s_gpsWaypointIdx = s_gpsWaypointCount - 1;
    }
    // Advance through reachable waypoints.
    while (s_gpsWaypointIdx < s_gpsWaypointCount - 1) {
        float wx = s_gpsWaypoints[s_gpsWaypointIdx][0];
        float wy = s_gpsWaypoints[s_gpsWaypointIdx][1];
        float wpDx = wx - px;
        float wpDy = wy - py;
        float wpDist = sqrtf(wpDx * wpDx + wpDy * wpDy);

        bool reached = (wpDist < GPS_WP_ARRIVE_DIST);
        bool overshoot = (s_gpsWpMinDist < GPS_WP_OVERSHOOT_CLOSE &&
                          wpDist > s_gpsWpMinDist * GPS_WP_OVERSHOOT_RATIO + GPS_WP_OVERSHOOT_MARGIN);

        if (!reached && !overshoot) {
            // Still en route to this waypoint.
            if (wpDist < s_gpsWpMinDist) s_gpsWpMinDist = wpDist;
            break;
        }
        // Advance.
        Log::Field("FieldNavigation: [NAV-PATH] wp %d/%d reached (%s, dist=%.0f, minDist=%.0f), advancing",
                   s_gpsWaypointIdx, s_gpsWaypointCount,
                   reached ? "arrived" : "overshoot",
                   wpDist, s_gpsWpMinDist);
        s_gpsWaypointIdx++;
        s_gpsWpMinDist = 1e30f;
    }
    sx = s_gpsWaypoints[s_gpsWaypointIdx][0];
    sy = s_gpsWaypoints[s_gpsWaypointIdx][1];
}

static void StopGPS(const char* reason)
{
    if (!s_gpsActive) return;
    s_gpsActive = false;
    s_gpsCatalogIdx = -1;
    s_gpsLastDirIdx = -1;     // v0.17.0
    s_gpsLastStepsAnn = -1;   // v0.17.0
    // v0.17.5.1: reset hysteresis state.
    s_gpsPendingDirIdx   = -1;
    s_gpsPendingDirSince = 0;
    // v0.17.1: reset path-aware state.
    s_gpsWaypointCount = 0;
    s_gpsWaypointIdx   = 0;
    s_gpsUseWaypoints  = false;
    s_gpsWpMinDist     = 1e30f;
    if (reason) {
        ScreenReader::Speak(reason, true);
    }
    Log::Field("FieldNavigation: [GPS] Stopped: %s", reason ? reason : "(silent)");
}

static void StartGPS(int catalogIdx)
{
    if (catalogIdx < 0 || catalogIdx >= s_catalogCount) return;
    const EntityInfo& tgt = s_catalog[catalogIdx];
    if (tgt.entityIdx == s_playerEntityIdx) return;

    float px = 0, py = 0, tx = 0, ty = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return;
    if (!GetCatalogTargetPos(catalogIdx, tx, ty)) {
        ScreenReader::Speak("Target not located.", true);
        return;
    }

    // v0.17.1: Build the A*+funnel path BEFORE computing the initial direction
    // so we can announce the cardinal toward the first waypoint, not the final
    // destination. Trigger-line targets need skipTriggerIdx so A* can route
    // through them; runtime-entity targets need targetEntityIdx so push-radius
    // blackout doesn't block the goal triangle.
    int targetEntityIdxForPath = -1;
    int skipTriggerIdxForPath  = -1;
    if (tgt.entityIdx >= 0 && tgt.entityIdx < MAX_ENTITIES) {
        targetEntityIdxForPath = tgt.entityIdx;
    } else if (tgt.entityIdx <= -200 && tgt.entityIdx > -300) {
        skipTriggerIdxForPath = -(tgt.entityIdx + 200);
    }
    bool pathBuilt = BuildGpsPath(px, py, tx, ty, targetEntityIdxForPath, skipTriggerIdxForPath);

    // Compute direction toward the steering target (first waypoint if path,
    // final destination otherwise). Distance is always to final destination
    // (steps and nearby/in-range use straight-line distance).
    float sx = tx, sy = ty;
    if (pathBuilt) {
        sx = s_gpsWaypoints[s_gpsWaypointIdx][0];
        sy = s_gpsWaypoints[s_gpsWaypointIdx][1];
    }
    float sdx = sx - px;
    float sdy = sy - py;
    float distToFinal = sqrtf((tx - px) * (tx - px) + (ty - py) * (ty - py));
    int dirIdx = ComputeScreenDirIndex(sdx, sdy);

    s_gpsActive = true;
    s_gpsCatalogIdx = catalogIdx;
    s_gpsLastDist = distToFinal;
    s_gpsLastAnnounceTime = GetTickCount();
    s_gpsArrivedAnnounced = false;
    s_gpsNearbyAnnounced = false;
    s_gpsLastDirIdx = dirIdx;  // v0.17.0: lock in initial direction so updates don't re-announce immediately
    // v0.17.5.1: prime hysteresis with the initial direction so the next
    // UpdateGPS tick doesn't spuriously fire a "changed" event.
    s_gpsPendingDirIdx   = dirIdx;
    s_gpsPendingDirSince = s_gpsLastAnnounceTime;

    // v0.12.07: Compute per-target arrival distance.
    // Use the entity's actual talk radius so "In range" fires when the player
    // can genuinely interact. Ceiling step rounding (below) provides the safety
    // margin — steps never undercount, so the player always arrives inside
    // the zone before the step count reaches zero. Min 60 to avoid overshoot.
    s_gpsArriveDist = GPS_ARRIVE_DIST;  // default fallback for non-entity targets
    if (tgt.entityIdx >= 0 && tgt.entityIdx < MAX_ENTITIES) {
        // Runtime entity — read talk radius from entity struct.
        uint16_t talkRad = GetEntityTalkRadius(tgt.entityIdx);
        if (talkRad > 0) {
            s_gpsArriveDist = (float)talkRad;
            if (s_gpsArriveDist < 60.0f) s_gpsArriveDist = 60.0f;
        } else {
            s_gpsArriveDist = GPS_ARRIVE_DIST;  // NPC without talk radius yet
        }
    } else if (tgt.type == ENT_SAVE_POINT || tgt.type == ENT_DRAW_POINT) {
        s_gpsArriveDist = 200.0f;  // walk-on interaction zone (generous)
    } else if (tgt.type == ENT_OBJECT || tgt.type == ENT_BG_OBJECT) {
        s_gpsArriveDist = 200.0f;  // default for model-less objects
    }
    // GPS_NEARBY_DIST is relative to arrive distance — use 2x arrive.
    s_gpsNearbyDist = s_gpsArriveDist * 2.0f;
    if (s_gpsNearbyDist < GPS_NEARBY_DIST) s_gpsNearbyDist = GPS_NEARBY_DIST;

    // Initialize movement tracking for step calibration diagnostic.
    s_gpsDiagLastX = px;
    s_gpsDiagLastY = py;
    s_gpsDiagLastTick = GetTickCount();
    s_gpsDiagAccumDist = 0;
    s_gpsDiagFrames = 0;

    // v0.12.07: Ceiling rounding — always round up so the player walks at least
    // as many steps as announced. e.g., 1300/320 = 4.06 → 5 steps.
    int steps = (int)ceilf(distToFinal / GPS_STEPS_DIVISOR);
    if (steps < 1) steps = 1;
    s_gpsLastStepsAnn = steps;  // v0.17.0

    char buf[256];
    snprintf(buf, sizeof(buf), "Navigating to %s. %s, %d steps.",
             tgt.name, GPS_DIR_NAMES[dirIdx], steps);
    ScreenReader::Speak(buf, true);
    Log::Field("FieldNavigation: [GPS] Started: %s dir=%s dist=%.0f steps=%d arriveDist=%.0f nearbyDist=%.0f catIdx=%d pathAware=%d wpCount=%d",
               tgt.name, GPS_DIR_NAMES[dirIdx], distToFinal, steps,
               s_gpsArriveDist, s_gpsNearbyDist, catalogIdx,
               s_gpsUseWaypoints ? 1 : 0, s_gpsWaypointCount);
    // v0.17.0: log the screen-projection math so direction issues can be diagnosed from logs.
    // v0.17.1: steering target is now the waypoint, not the final destination —
    // log both so the path-aware behavior is traceable.
    // v0.17.2: include field name + axis source tag so the BAT log makes
    // obvious which field the cardinal was computed for and where the axes
    // came from (ca-file vs identity-fallback). With state pairs now split,
    // any mismatch between consecutive sessions can only be a genuine
    // field-change (different .ca → different axes) since calibration can no
    // longer write to the manual-nav pair.
    const char* projFieldName = FF8Addresses::pCurrentFieldName
                                ? FF8Addresses::pCurrentFieldName : "(unknown)";
    float sR = sdx * s_camRightX + sdy * s_camRightY;
    float sD = sdx * s_camDownX  + sdy * s_camDownY;
    Log::Field("FieldNavigation: [NAV-PROJ] start field='%s' axes=%s cat=%d player=(%.0f,%.0f) "
               "steer=(%.0f,%.0f) target=(%.0f,%.0f) delta=(%.0f,%.0f) "
               "camRight=(%.3f,%.3f) camDown=(%.3f,%.3f) screen=(%.0f,%.0f) cardinal=%s wp=%d/%d",
               projFieldName, s_camAxesSource,
               catalogIdx, px, py, sx, sy, tx, ty, sdx, sdy,
               s_camRightX, s_camRightY, s_camDownX, s_camDownY,
               sR, sD, GPS_DIR_NAMES[dirIdx],
               s_gpsWaypointIdx, s_gpsWaypointCount);
}

static void UpdateGPS()
{
    if (!s_gpsActive) return;
    if (s_playerEntityIdx < 0) return;
    if (s_gpsCatalogIdx < 0 || s_gpsCatalogIdx >= s_catalogCount) {
        StopGPS("Navigation off.");
        return;
    }

    // Stop GPS if dialog opens.
    if (FieldDialog::IsDialogOpen()) return;

    float px = 0, py = 0, tx = 0, ty = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return;
    if (!GetCatalogTargetPos(s_gpsCatalogIdx, tx, ty)) return;

    // v0.17.1: Determine the steering target. Path-aware mode aims at the
    // current waypoint and advances as the player gets close. Straight-line
    // fallback aims at the final destination.
    float sx = tx, sy = ty;
    int prevWpIdx = s_gpsWaypointIdx;
    if (s_gpsUseWaypoints) {
        AdvanceGpsWaypoint(px, py, sx, sy);
    }
    bool wpAdvanced = (s_gpsWaypointIdx != prevWpIdx);

    // Distance is always to the final destination (for steps + nearby/in-range).
    float dxFinal = tx - px;
    float dyFinal = ty - py;
    float dist = sqrtf(dxFinal * dxFinal + dyFinal * dyFinal);

    // Direction is toward the steering target (waypoint or final destination).
    float sdx = sx - px;
    float sdy = sy - py;
    int dirIdx = ComputeScreenDirIndex(sdx, sdy);
    // v0.12.07: Ceiling rounding for steps.
    int steps = (int)ceilf(dist / GPS_STEPS_DIVISOR);
    if (steps < 1) steps = 1;

    // --- Movement rate tracking (for step calibration diagnostic) ---
    {
        float mdx = px - s_gpsDiagLastX;
        float mdy = py - s_gpsDiagLastY;
        float moveDist = sqrtf(mdx * mdx + mdy * mdy);
        if (moveDist > 0.5f) {  // only track meaningful movement
            s_gpsDiagAccumDist += moveDist;
            s_gpsDiagFrames++;
        }
        s_gpsDiagLastX = px;
        s_gpsDiagLastY = py;

        // Log movement rate every 3 seconds for calibration.
        DWORD diagNow = GetTickCount();
        if (diagNow - s_gpsDiagLastTick >= 3000 && s_gpsDiagAccumDist > 0) {
            float elapsed = (float)(diagNow - s_gpsDiagLastTick) / 1000.0f;
            float unitsPerSec = s_gpsDiagAccumDist / elapsed;
            float unitsPerFrame = (s_gpsDiagFrames > 0) ? s_gpsDiagAccumDist / (float)s_gpsDiagFrames : 0;
            Log::Field("FieldNavigation: [GPS-SPEED] %.1f units/sec, %.1f units/frame, %d frames in %.1fs, total=%.0f units",
                       unitsPerSec, unitsPerFrame, s_gpsDiagFrames, elapsed, s_gpsDiagAccumDist);
            s_gpsDiagLastTick = diagNow;
            s_gpsDiagAccumDist = 0;
            s_gpsDiagFrames = 0;
        }
    }

    // --- Arrival check (v0.12.04: per-target threshold from talk radius) ---
    if (dist <= s_gpsArriveDist && !s_gpsArrivedAnnounced) {
        s_gpsArrivedAnnounced = true;
        ScreenReader::Speak("In range.", true);
        Log::Field("FieldNavigation: [GPS] In range (dist=%.0f steps=%d)", dist, steps);
        StopGPS(nullptr);  // silent stop after arrival
        return;
    }

    // --- Nearby announcement (one-shot, v0.12.04: per-target threshold) ---
    if (dist <= s_gpsNearbyDist && !s_gpsNearbyAnnounced) {
        s_gpsNearbyAnnounced = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "Nearby. %s, %d steps.", GPS_DIR_NAMES[dirIdx], steps);
        ScreenReader::Speak(buf, true);
        s_gpsLastAnnounceTime = GetTickCount();
        s_gpsLastDist = dist;
        s_gpsLastDirIdx = dirIdx;
        s_gpsLastStepsAnn = steps;
        Log::Field("FieldNavigation: [GPS] Nearby: dir=%s dist=%.0f steps=%d", GPS_DIR_NAMES[dirIdx], dist, steps);
        return;
    }

    // --- v0.17.5.1: Cardinal-change-only cadence (replaces v0.17.0/.17.1 logic) ---
    //
    // Trigger rule: announce ONLY when the cardinal CHANGES, AND the new
    // cardinal has held steady for GPS_DIR_HYSTERESIS_MS. This eliminates the
    // flood Aaron reported in the v0.17.5 BAT ("direction / distance constantly
    // rattling off and spamming the TTS"):
    //   * Step-count changes do NOT announce (Aaron's spec: "it says to go
    //     north so I keep going north until it eventually says east then I go
    //     east" -- direction-only).
    //   * Waypoint advances do NOT announce on their own (if cardinal matches
    //     across two legs, the conceptual handoff is silent).
    //   * Cardinal changes still trigger, but only after the new cardinal has
    //     held GPS_DIR_HYSTERESIS_MS (500ms) of consistent samples. This filters
    //     sector-boundary jitter near waypoint corners -- the v0.17.5 BAT's one-
    //     off "south" on bg2f_1 (where the geometry can't produce a real south
    //     leg) was almost certainly such a transient.
    //   * Nearby/in-range still fire as one-shots (handled above).
    //
    // Note: the wpAdvanced log signal is retained for diagnostic [NAV-PROJ]
    // output but no longer drives speech.
    if (dist <= s_gpsNearbyDist) {
        // In nearby zone -- the one-shot announce above handled it. Re-prime
        // pending so we don't fire after leaving the nearby zone (which can
        // happen if the player wanders back out).
        s_gpsPendingDirIdx   = dirIdx;
        s_gpsPendingDirSince = GetTickCount();
        return;
    }

    DWORD now = GetTickCount();
    bool dirChanged   = (dirIdx != s_gpsLastDirIdx);
    bool stepsChanged = (steps  != s_gpsLastStepsAnn);  // tracked for log only

    if (!dirChanged) {
        // No direction change. Re-prime pending so a transient flip during a
        // long straight walk doesn't accidentally promote.
        s_gpsPendingDirIdx   = dirIdx;
        s_gpsPendingDirSince = now;
        return;
    }

    // Direction wants to change. Apply hysteresis: require the candidate
    // cardinal to hold steady for GPS_DIR_HYSTERESIS_MS before announcing.
    if (dirIdx != s_gpsPendingDirIdx) {
        // New candidate -- start its timer.
        s_gpsPendingDirIdx   = dirIdx;
        s_gpsPendingDirSince = now;
        return;
    }
    if (now - s_gpsPendingDirSince < GPS_DIR_HYSTERESIS_MS) {
        // Candidate not yet confirmed. Wait.
        return;
    }

    // Candidate confirmed. Announce and update state.
    s_gpsLastAnnounceTime = now;
    s_gpsLastDirIdx = dirIdx;
    s_gpsLastStepsAnn = steps;
    s_gpsLastDist = dist;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s, %d steps.", GPS_DIR_NAMES[dirIdx], steps);
    ScreenReader::Speak(buf, true);
    Log::Field("FieldNavigation: [GPS] Update: dir=%s dist=%.0f steps=%d dirChanged=1 stepsChanged=%d wpAdvanced=%d wp=%d/%d hysteresis=ok",
               GPS_DIR_NAMES[dirIdx], dist, steps,
               stepsChanged ? 1 : 0, wpAdvanced ? 1 : 0,
               s_gpsWaypointIdx, s_gpsWaypointCount);
    // v0.17.0: per-announcement projection diagnostic. Same field as [NAV-PROJ] at StartGPS
    // but lets us correlate every direction announcement with the exact projection inputs.
    // v0.17.1: log steer (current waypoint) AND final target so path-aware advance is traceable.
    float sR = sdx * s_camRightX + sdy * s_camRightY;
    float sD = sdx * s_camDownX  + sdy * s_camDownY;
    Log::Field("FieldNavigation: [NAV-PROJ] update player=(%.0f,%.0f) steer=(%.0f,%.0f) "
               "target=(%.0f,%.0f) delta=(%.0f,%.0f) screen=(%.0f,%.0f) cardinal=%s wp=%d/%d",
               px, py, sx, sy, tx, ty, sdx, sdy, sR, sD,
               GPS_DIR_NAMES[dirIdx], s_gpsWaypointIdx, s_gpsWaypointCount);
}
