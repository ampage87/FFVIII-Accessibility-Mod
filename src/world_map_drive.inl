// world_map_drive.inl - Auto-drive lifecycle + key injection (with v0.16.0 Part C)
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// Held-key injection via keybd_event (PressKey/ReleaseKey/SetDriveKeys/
// ReleaseAllDriveKeys) plus the auto-drive lifecycle: StartAutoDrive,
// StopAutoDrive, StartSweep, UpdateAutoDrive.
//
// v0.16.0 Part C: StartAutoDrive checks s_destPlannerEligible[catIdx]
// before routing through PlanDrivePath. Ineligible destinations skip the
// planner entirely and use simple-coord steering (the v0.11.11-era design).
// This prevents the v0.14.95 closest-active-region fallback from misrouting
// drives toward unrelated destinations.

// keybd_event-based key injection. Arrow keys use scan codes 0x48 (UP),
// 0x4B (LEFT), 0x4D (RIGHT), all extended-key scancodes. v0.14.102 added
// the 'extended' parameter so A (gas pedal) and W (reverse) can be sent as
// non-extended keys per the v0.11.14 design.
static void PressKey(BYTE vk, BYTE scan, bool extended = true)
{
    keybd_event(vk, scan, extended ? KEYEVENTF_EXTENDEDKEY : 0, 0);
}

static void ReleaseKey(BYTE vk, BYTE scan, bool extended = true)
{
    keybd_event(vk, scan, (extended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP, 0);
}

static void ReleaseAllDriveKeys()
{
    if (s_keyUpHeld)    { ReleaseKey(VK_UP,    0x48); s_keyUpHeld    = false; }
    if (s_keyLeftHeld)  { ReleaseKey(VK_LEFT,  0x4B); s_keyLeftHeld  = false; }
    if (s_keyRightHeld) { ReleaseKey(VK_RIGHT, 0x4D); s_keyRightHeld = false; }
    if (s_keyDownHeld)  { ReleaseKey(VK_DOWN,  0x50); s_keyDownHeld  = false; }
    // v0.14.102: also release the gas pedal (A key, NOT extended).
    if (s_keyGasHeld)   { ReleaseKey('A',      0x1E, false); s_keyGasHeld = false; }
}

// Idempotent press/release: only generates events on state changes.
static void SetDriveKeys(bool up, bool left, bool right, bool down = false)
{
    if (up    && !s_keyUpHeld)    { PressKey(VK_UP,    0x48); s_keyUpHeld    = true; }
    if (!up   &&  s_keyUpHeld)    { ReleaseKey(VK_UP,  0x48); s_keyUpHeld    = false; }
    if (left  && !s_keyLeftHeld)  { PressKey(VK_LEFT,  0x4B); s_keyLeftHeld  = true; }
    if (!left &&  s_keyLeftHeld)  { ReleaseKey(VK_LEFT, 0x4B); s_keyLeftHeld = false; }
    if (right && !s_keyRightHeld) { PressKey(VK_RIGHT, 0x4D); s_keyRightHeld = true; }
    if (!right&&  s_keyRightHeld) { ReleaseKey(VK_RIGHT,0x4D); s_keyRightHeld = false; }
    // #67 v0.18.3.68: DOWN arrow (reverse) for the wedge-recovery burst.
    if (down  && !s_keyDownHeld)  { PressKey(VK_DOWN,  0x50); s_keyDownHeld  = true; }
    if (!down &&  s_keyDownHeld)  { ReleaseKey(VK_DOWN,0x50); s_keyDownHeld  = false; }
    // v0.14.102: gas pedal mirrors UP arrow. A key (scan 0x1E, NOT extended).
    if (up    && !s_keyGasHeld)   { PressKey('A',      0x1E, false); s_keyGasHeld   = true; }
    if (!up   &&  s_keyGasHeld)   { ReleaseKey('A',    0x1E, false); s_keyGasHeld   = false; }
}

// #67 v0.18.3.74: wrap a world-space delta into the torus' shortest representative.
static void WrapWorldDelta(int32_t& dx, int32_t& dy)
{
    const int32_t W = (int32_t)WM_WIDTH, H = (int32_t)WM_HEIGHT;
    if (dx >  W / 2) dx -= W;
    if (dx < -W / 2) dx += W;
    if (dy >  H / 2) dy -= H;
    if (dy < -H / 2) dy += H;
}

// #67 v0.18.3.74: pull a measured basis axis toward a freshly observed unit
// motion (EMA), renormalized. Tracks the world-map camera as it swings mid-drive.
static void RefreshBasisAxis(double& ax, double& ay, double nx, double ny)
{
    ax = ax * (1.0 - DRIVE_BASIS_EMA) + nx * DRIVE_BASIS_EMA;
    ay = ay * (1.0 - DRIVE_BASIS_EMA) + ny * DRIVE_BASIS_EMA;
    double l = sqrt(ax * ax + ay * ay);
    if (l > 1e-6) { ax /= l; ay /= l; }
}

// #67 v0.18.3.75: re-perpendicularize unit axis (ax,ay) against unit (bx,by)
// (Gram-Schmidt), keeping the screen basis a valid rotating orthonormal frame as
// the camera swings. Without this, refreshing only the UP axis (the common case
// -- steering presses UP most of the time) would let uHat drift onto rHat and
// collapse the basis.
static void OrthonormalizeAgainst(double& ax, double& ay, double bx, double by)
{
    double d = ax * bx + ay * by;
    ax -= d * bx; ay -= d * by;
    double l = sqrt(ax * ax + ay * ay);
    if (l > 1e-6) { ax /= l; ay /= l; }
}

// #67 v0.18.3.82: is the straight line between two fine cells clear of cells
// that block FOOT/CAR travel (ocean, or steep mountain) -- the SAME block rule
// the clearance field and IsFineTraversable use for foot/car. Self-contained
// (reads only s_walkClassFine/s_steepFine + the SEG_*/WM_MTN_STEEP_BLOCK consts
// in state.inl) so it has no include-order dependency. Torus-aware; samples each
// interpolated cell between the endpoints. Used to clamp the drive's lookahead
// steer target so it is never aimed THROUGH a cliff corner the winding route
// goes AROUND. (The Dollet patch ledge is forced-steep MOUNTAIN, so it counts as
// blocked here too -- the target can't be aimed across it either.)
static bool FineLineClearFootCar(int c0, int r0, int c1, int r1)
{
    int dc = c1 - c0, dr = r1 - r0;
    if (dc >  WM_FINE_COLS / 2) dc -= WM_FINE_COLS;
    if (dc < -WM_FINE_COLS / 2) dc += WM_FINE_COLS;
    if (dr >  WM_FINE_ROWS / 2) dr -= WM_FINE_ROWS;
    if (dr < -WM_FINE_ROWS / 2) dr += WM_FINE_ROWS;
    int adc = dc < 0 ? -dc : dc;
    int adr = dr < 0 ? -dr : dr;
    int steps = adc > adr ? adc : adr;
    if (steps <= 0) return true;
    for (int i = 1; i <= steps; i++) {
        int cc = c0 + (int)((int64_t)dc * i / steps);
        int rr = r0 + (int)((int64_t)dr * i / steps);
        int wc = ((cc % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
        int wr = ((rr % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
        uint8_t cls = s_walkClassFine[wr][wc];
        if (cls == SEG_OCEAN ||
            (cls == SEG_MOUNTAIN && s_steepFine[wr][wc] > WM_MTN_STEEP_BLOCK))
            return false;
    }
    return true;
}

// ============================================================================
// Auto-drive lifecycle (v0.14.86)
// ============================================================================
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    ReleaseAllDriveKeys();
    s_driveActive = false;
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;
    s_driveAwaitingArrivalDecision = false;
    s_driveExitTick                = 0;
    s_destFootFriendly     = true;
    s_drivePlannerEligible = true;   // v0.16.0.2: reset to safe default
    // #67 v0.18.3.74: screen-relative steering teardown.
    s_driveCalPhase      = DCAL_DONE;
    s_camBasisValid      = false;
    s_driveSidestepUntil = 0;
    s_drivePrevHadKeys   = false;
    if (reason && *reason) {
        ScreenReader::Speak(reason, true);
        Log::World("WorldMap: [DRIVE] Stopped: %s", reason);
    } else {
        Log::World("WorldMap: [DRIVE] Stopped (silent)");
    }
}

static void StartAutoDrive(int catIdx)
{
    if (s_driveActive) return;
    if (!s_catalogBuilt || s_catalogCount == 0) {
        ScreenReader::Speak("No locations available.", true);
        return;
    }
    if (catIdx < 0 || catIdx >= s_catalogCount) {
        ScreenReader::Speak("Invalid destination.", true);
        return;
    }

    int32_t px, py, pz;
    GetWorldMapPosition_Active(&px, &py, &pz);
    if (px == 0 && py == 0) {
        ScreenReader::Speak("Position unavailable. Try again.", true);
        return;
    }

    s_sweepAbortCount = 0;

    const LocationEntry& dest = s_catalog[catIdx];

    // v0.14.89: prefer refined entry coord when available.
    int locIdx = FindLocationIndexByTargetCoords(dest.x, dest.y);
    if (locIdx >= 0 && s_refinedHas[locIdx]) {
        s_driveTargetX = s_refinedX[locIdx];
        s_driveTargetY = s_refinedY[locIdx];
        Log::World("WorldMap: [DRIVE] Using refined entry for %s: (%d,%d) instead of catalog (%d,%d)",
                   dest.name, s_refinedX[locIdx], s_refinedY[locIdx], dest.x, dest.y);
    } else {
        s_driveTargetX = dest.x;
        s_driveTargetY = dest.y;
    }
    strncpy(s_driveTargetName, dest.name, sizeof(s_driveTargetName) - 1);
    s_driveTargetName[sizeof(s_driveTargetName) - 1] = '\0';

    double dist = CalculateWrappedDistance(px, py, dest.x, dest.y);
    DWORD now = GetTickCount();

    s_driveActive            = true;
    s_driveStartTime         = now;
    s_driveLastAnnounce      = now;
    s_driveLastDist          = dist;
    s_driveStuckX            = px;
    s_driveStuckY            = py;
    s_driveStuckCheckTime    = now;
    s_driveStuckCount        = 0;
    s_driveReplanCount       = 0;   // #67 v0.18.3.59: fresh recovery budget per drive
    // #67 v0.18.3.62: reset motion-derived heading tracking for the new drive.
    s_driveMoveHeading    = -1;     // unknown until he moves
    s_driveHeadRefX       = px;
    s_driveHeadRefY       = py;
    s_driveTurnSign       = 1;      // initial guess; self-calibrates from observed rotation
    s_driveTurnedSinceRef = false;
    s_driveLastTurnRight  = false;
    s_driveLastMoveTime   = now;
    s_driveLastMovePosX   = px;
    s_driveLastMovePosY   = py;
    s_driveWedgeReverseUntil = 0;       // #67 v0.18.3.68: fresh reverse un-wedge state
    s_driveWedgeReverseCount = 0;
    s_driveWedgeProgressDist = dist;
    s_driveWedgeAnchorX      = px;       // #67 v0.18.3.69: net-displacement wedge anchor
    s_driveWedgeAnchorY      = py;
    s_driveWedgeAnchorTime   = now;
    s_driveApproachAnnounced = (dist < DRIVE_APPROACH_DIST);
    s_finalApproachEnterTick = 0;
    s_sweepActive            = false;
    s_sweepPhase             = 0;
    s_sweepTurning           = true;

    s_driveOnFootAtStart = (s_lastVehicle < 0) ||
                           (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    // #67 v0.18.3.74: screen-relative steering reset + calibration arm (foot only).
    // The basis (what UP / RIGHT do in world space) is measured at drive start so
    // we never assume a fixed camera orientation. A close start or a vehicle skips
    // the probe and relies on the default + live refresh.
    s_camBasisValid      = false;
    s_camUx = 0.0; s_camUy = -1.0;          // default North until measured
    s_camRx = 1.0; s_camRy =  0.0;          // default East  until measured
    s_drivePrevHadKeys   = false;
    s_driveSidestepUntil = 0;
    s_driveSidestepSign  = 1;
    s_driveProbeValid    = false;   // #67 v0.18.3.77: re-probe the steering arrow on a fresh drive
    s_driveCalTry        = 0;
    // #67 v0.18.3.77: the greedy arrow-probe needs no measured basis, so skip the
    // UP/RIGHT calibration wobble entirely -- it just trusts measured progress.
    s_driveCalPhase = DCAL_DONE;
    s_camBasisValid = true;

    int distKm = (int)(dist / 1000.0);
    char buf[160];
    if (distKm < 1) {
        snprintf(buf, sizeof(buf), "Driving to %s. Very close.", s_driveTargetName);
    } else {
        snprintf(buf, sizeof(buf), "Driving to %s. %d kilometers.", s_driveTargetName, distKm);
    }
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [DRIVE] Start \u2192 %s at (%d,%d), dist=%.0f units (%d km)",
               s_driveTargetName, s_driveTargetX, s_driveTargetY, dist, distKm);

    // v0.14.103.7: classify foot-friendliness for the sweep-abort threshold.
    s_destFootFriendly = IsLocationFootFriendly(s_driveTargetX, s_driveTargetY);
    {
        int destCol = WorldXToSegCol(s_driveTargetX);
        int destRow = WorldYToSegRow(s_driveTargetY);
        uint8_t destReg = (s_segmentRegionLoaded &&
                           destCol >= 0 && destCol < WMX_SEG_COLS &&
                           destRow >= 0 && destRow < WMX_SEG_ROWS)
                          ? s_segmentRegionMap[destRow][destCol]
                          : 0xFF;
        Log::World("WorldMap: [DRIVE] Destination foot-friendly=%s (target seg(%d,%d) region=0x%02X)",
                   s_destFootFriendly ? "YES" : "NO",
                   destCol, destRow, (unsigned)destReg);
    }

    // v0.14.94: run the path planner once. Sets s_drivePath[]/Len/Idx/Planned
    // and s_driveGoalSegs[]/Count. On failure (Ragnarok, region map not
    // loaded, no matching trigger program, no path), s_drivePathPlanned
    // stays false and AD falls back to catalog-center steering with the
    // v0.14.93 distance-based arrival heuristic. UpdateAutoDrive picks the
    // active mode from s_drivePathPlanned each tick.
    //
    // v0.16.0 Part C: gate on s_destPlannerEligible[locIdx]. Geometric-trigger
    // destinations (no foot clause in s_triggerPrograms[] for this region)
    // must skip the planner entirely -- the v0.14.95 closest-active-region
    // fallback misroutes them toward unrelated destinations. They use the
    // v0.11.11-era simple-coord steering (catalog-center, bearing-based)
    // implemented in UpdateAutoDrive's non-planner branch.
    //
    // v0.16.0.1: index by locIdx (s_locations master-table position) NOT
    // catIdx (s_catalog BFS-filtered/distance-sorted position). The v0.16.0
    // BAT showed Fire Cavern (master idx 37, catIdx 2) reading Dollet's
    // eligibility (master idx 2 = YES) and running the planner anyway,
    // hitting the closest-active-region fallback toward seg(18,20). Part B
    // caught the off-target arrival but the planner walk shouldn't have
    // fired at all. FindLocationIndexByTargetCoords resolves locIdx above.
    //
    // v0.16.0.2: persist the decision in s_drivePlannerEligible so the
    // replan-on-world-map-re-entry path in Poll() can honor it too. The
    // v0.16.0.1 BAT showed Fire Cavern starting correctly via simple-coord
    // (planned=0) but the replan after a random encounter called
    // PlanDrivePath unconditionally and converted the drive to planner-
    // routed toward the wrong destination. One decision, made once.
    bool plannerEligible = (locIdx >= 0 && locIdx < LOCATION_COUNT &&
                            s_destPlannerEligible[locIdx]);
    s_drivePlannerEligible = plannerEligible;
    if (plannerEligible) {
        PlanDrivePath(px, py);
    } else {
        Log::World("WorldMap: [DRIVE] Geometric-trigger destination %s (locIdx=%d, planner-ineligible) \u2014 using simple-coord steering",
                   s_driveTargetName, locIdx);
        s_drivePathLen      = 0;
        s_drivePathIdx      = 0;
        s_drivePathPlanned  = false;
        s_driveGoalSegCount = 0;
    }
}

static void StartSweep(int32_t px, int32_t py, DWORD now)
{
    s_sweepActive   = true;
    s_sweepPhase    = 1;
    s_sweepTurning  = true;
    s_sweepStateEnd = now + SWEEP_TURN_BASE_MS;
    s_driveStuckX = px;
    s_driveStuckY = py;
    s_driveStuckCheckTime = now;
    s_driveStuckCount = 0;
    ScreenReader::Speak("Searching for entrance.", true);
    Log::World("WorldMap: [DRIVE-SWEEP] Started (target=%s, phase 1 turning right %dms)",
               s_driveTargetName, SWEEP_TURN_BASE_MS);
}

static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    int32_t px, py, pz;
    GetWorldMapPosition_Active(&px, &py, &pz);
    uint16_t heading = GetWorldMapHeading();

    if (px == 0 && py == 0) return;

    double dist = CalculateWrappedDistance(px, py, s_driveTargetX, s_driveTargetY);
    DWORD now = GetTickCount();

    // #67 v0.18.3.68: refresh the reverse un-wedge budget on genuine progress
    // toward the target (got >= WEDGE_PROGRESS_EPS closer). Reversing is bounded
    // per wedge, but the budget renews once a reverse actually helps him round a
    // corner -- so a true no-progress jam still gives up instead of reversing
    // forever.
    if (dist < s_driveWedgeProgressDist - WEDGE_PROGRESS_EPS) {
        s_driveWedgeReverseCount = 0;
        s_driveWedgeProgressDist = dist;
    }

    bool isOnFoot = (s_lastVehicle < 0) ||
                    (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    // One-shot approach announcement (suppressed during sweep).
    if (!s_driveApproachAnnounced && dist < DRIVE_APPROACH_DIST && !s_sweepActive) {
        s_driveApproachAnnounced = true;
        int distKm = (int)(dist / 1000.0);
        char buf[128];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Approaching %s.", s_driveTargetName);
        } else {
            snprintf(buf, sizeof(buf), "Approaching %s. %d kilometers.", s_driveTargetName, distKm);
        }
        ScreenReader::Speak(buf, true);
        s_driveLastAnnounce = now;
    }
    s_driveLastDist = dist;
    s_driveLastPosX = px;
    s_driveLastPosY = py;

    if (!s_sweepActive && now - s_driveLastAnnounce >= DRIVE_ANNOUNCE_INTERVAL_MS) {
        s_driveLastAnnounce = now;
        int distKm = (int)(dist / 1000.0);
        char buf[64];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Less than 1 kilometer.");
        } else {
            snprintf(buf, sizeof(buf), "%d kilometers.", distKm);
        }
        ScreenReader::Speak(buf, true);
    }

    // v0.14.99: Sweep abort on drift. MUST run BEFORE the sweep state machine.
    if (s_sweepActive && dist > DRIVE_FINAL_APPROACH_DIST * 1.5) {
        s_sweepAbortCount++;
        int abortLimit = s_destFootFriendly ? DRIVE_BOUNCE_ABORT_THRESHOLD : 1;
        if (s_sweepAbortCount >= abortLimit) {
            char buf[200];
            snprintf(buf, sizeof(buf),
                     "Arrived near %s. You may need to enter on foot.",
                     s_driveTargetName);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: [DRIVE-BOUNCE] %s (sweep-abort %d/%d, dist=%.0f)",
                       buf, s_sweepAbortCount, DRIVE_BOUNCE_ABORT_THRESHOLD, dist);
            StopAutoDrive(nullptr);
            return;
        }
        Log::World("WorldMap: [DRIVE-SWEEP] Aborting (drifted out of final approach: dist=%.0f, threshold=%.0f) \u2014 retry %d/%d, returning to normal steering",
                   dist, DRIVE_FINAL_APPROACH_DIST * 1.5, s_sweepAbortCount, DRIVE_BOUNCE_ABORT_THRESHOLD);
        s_sweepActive            = false;
        s_sweepPhase             = 0;
        s_sweepTurning           = true;
        s_finalApproachEnterTick = 0;
    }

    // Sweep state machine.
    if (s_sweepActive) {
        if (now >= s_sweepStateEnd) {
            if (s_sweepTurning) {
                s_sweepTurning = false;
                s_sweepStateEnd = now + SWEEP_WALK_DURATION_MS;
                Log::World("WorldMap: [DRIVE-SWEEP] Phase %d walk start (%dms)",
                           s_sweepPhase, SWEEP_WALK_DURATION_MS);
            } else {
                s_sweepPhase++;
                if (s_sweepPhase > SWEEP_MAX_PHASES) {
                    StopAutoDrive("Could not find entrance.");
                    return;
                }
                s_sweepTurning = true;
                DWORD turnDur = SWEEP_TURN_BASE_MS + (DWORD)(s_sweepPhase - 1) * 200;
                s_sweepStateEnd = now + turnDur;
                const char* dir = (s_sweepPhase % 2 == 1) ? "right" : "left";
                Log::World("WorldMap: [DRIVE-SWEEP] Phase %d turn start (%s, %dms)",
                           s_sweepPhase, dir, turnDur);
            }
        }
        bool wantUp    = !s_sweepTurning;
        bool wantRight = s_sweepTurning && (s_sweepPhase % 2 == 1);
        bool wantLeft  = s_sweepTurning && (s_sweepPhase % 2 == 0);
        SetDriveKeys(wantUp, wantLeft, wantRight);
        return;
    }

    // Final-approach timeout (on-foot only).
    if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST) {
        if (s_finalApproachEnterTick == 0) {
            s_finalApproachEnterTick = now;
            Log::World("WorldMap: [DRIVE] Entered final approach zone (dist=%.0f)", dist);
        }
        if (now - s_finalApproachEnterTick > FINAL_APPROACH_TIMEOUT_MS) {
            Log::World("WorldMap: [DRIVE] Final-approach timeout (%dms in zone, no entry)",
                       (int)(now - s_finalApproachEnterTick));
            StartSweep(px, py, now);
            return;
        }
    } else {
        s_finalApproachEnterTick = 0;
    }

    // Stuck detection.
    if (now - s_driveStuckCheckTime >= DRIVE_STUCK_CHECK_INTERVAL_MS) {
        double moved = CalculateWrappedDistance(s_driveStuckX, s_driveStuckY, px, py);
        if (moved < DRIVE_STUCK_THRESHOLD) {
            s_driveStuckCount++;
            Log::World("WorldMap: [DRIVE] Stuck check %d/%d (moved %.0f units in %dms window)",
                       s_driveStuckCount, DRIVE_STUCK_MAX, moved, DRIVE_STUCK_CHECK_INTERVAL_MS);
            // #67 v0.18.3.72: REVERSE un-wedge on every mid-route stuck check,
            // bounded by the give-up counter -- NOT a separate 4-use budget. The
            // .71 BAT proved the separate MAX_WEDGE_REVERSE budget exhausted early
            // and NEVER reset across the encounter-fragmented drive (each random
            // encounter resumes him on the same neck), so after 4 bursts the
            // reverse stopped firing for the rest of the drive and the stuck check
            // fell through to a futile re-plan (identical route every time). Now:
            // while we still have give-up headroom (stuckCount < DRIVE_STUCK_MAX),
            // fire a DOWN-only reverse burst and let stuckCount keep climbing -- so
            // it fires on every check (~3s apart) and still gives up after
            // DRIVE_STUCK_MAX if reversing never helps. stuckCount resets on
            // resume, so each post-encounter segment gets a fresh set of attempts.
            // (Plan: DOWN backs him off the wall into open ground; once unblocked,
            // the normal PIVOT can rotate him to re-aim -- rotation appears to need
            // open space, which is why turning in place while pinned does nothing.)
            // #67 v0.18.3.74: on foot, the recurring "wedge" was an artifact of
            // steering against a frozen heading; screen-relative steering should
            // not re-create it. If he still stalls against geometry, SIDESTEP
            // (slide laterally past it, alternating sides) on the first stuck
            // window, then fall through to the mid-route re-plan. Vehicles keep
            // the DOWN reverse burst (they rotate-then-go; this rework is foot-only).
            // #67 v0.18.3.82: on-foot now uses the SAME reverse un-wedge as
            // vehicles. The .80 unify put on-foot STEERING on the vehicle
            // heading turn-then-go but left on-foot RECOVERY pointing at the
            // SIDESTEP, whose ONLY consumer lives in the now-disabled screen-
            // relative branch (if (false && isOnFoot)) -- so on foot had NO
            // working recovery at all: the .81 Dollet BAT logged "SIDESTEP
            // left" but pressed no key and stayed wedged at the cliff corner.
            // On the world map the controls are tank-style (UP walks the
            // facing; LEFT/RIGHT rotate, and rotation needs forward motion), so
            // a lateral sidestep can't free a nosed-in character anyway -- the
            // only move that works is REVERSE: DOWN backs him off the cliff into
            // open ground, where the heading PIVOT can finish rotating him to
            // the route bearing and walk him AROUND the corner. DOWN moves the
            // character on foot (Aaron-confirmed, .71). Same bound as vehicles:
            // fire each stuck check (~3s) while there's give-up headroom; the
            // progress watermark renews the budget once a reverse gets him
            // closer, else it escalates to re-plan / give-up below.
            if (dist >= DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount < DRIVE_STUCK_MAX) {
                s_driveWedgeReverseUntil = now + REVERSE_BURST_MS;
                Log::World("WorldMap: [DRIVE] Stuck -> reverse un-wedge burst (check %d/%d, dist=%.0f, DOWN-only off the wall%s)",
                           s_driveStuckCount, DRIVE_STUCK_MAX, dist, isOnFoot ? ", on foot" : "");
                s_driveStuckX         = px;
                s_driveStuckY         = py;
                s_driveStuckCheckTime = now;
                return;
            }
            if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount >= 2) {
                Log::World("WorldMap: [DRIVE] Stuck in final approach → sweep");
                StartSweep(px, py, now);
                return;
            }
            // #67 v0.18.3.59: mid-route stuck recovery. Before giving up, re-plan
            // from the player's CURRENT position -- a fresh clearance-weighted
            // route from where he actually is, which steers him out of a wall
            // pocket he drifted into rather than stranding him. Bounded by
            // DRIVE_MAX_REPLANS per drive so a true hard-jam still terminates.
            // Planner-routed mid-route drives only (final approach uses the sweep
            // above; simple-coord drives have no path to re-plan).
            if (s_drivePlannerEligible && s_drivePathPlanned &&
                dist >= DRIVE_FINAL_APPROACH_DIST &&
                s_driveStuckCount >= DRIVE_REPLAN_TRIGGER &&
                s_driveReplanCount < DRIVE_MAX_REPLANS) {
                s_driveReplanCount++;
                Log::World("WorldMap: [DRIVE] Stuck mid-route -- re-planning from current position (recovery %d/%d)",
                           s_driveReplanCount, DRIVE_MAX_REPLANS);
                PlanDrivePath(px, py);
                s_driveProbeValid     = false;   // #67 v0.18.3.77: re-probe on the new route
                s_driveStuckCount     = 0;
                s_driveStuckX         = px;
                s_driveStuckY         = py;
                s_driveStuckCheckTime = now;
                return;
            }
            if (s_driveStuckCount >= DRIVE_STUCK_MAX) {
                StopAutoDrive("Stuck. Cannot reach destination.");
                return;
            }
        } else {
            s_driveStuckCount = 0;
        }
        s_driveStuckX         = px;
        s_driveStuckY         = py;
        s_driveStuckCheckTime = now;
    }

    // #67 stage 2: the planner path is now FINE cells (1024-unit) routed around
    // mountains/ocean. Advance the path cursor forward to the cell nearest the
    // player (never backward), then steer toward a small lookahead further along
    // the path -- this hugs the walkable route around obstacles instead of
    // cutting a straight line at a far segment centre.
    if (s_drivePathPlanned && s_drivePathLen > 0) {
        int pfc = WorldXToFineCol(px);
        int pfr = WorldYToFineRow(py);
        while (s_drivePathIdx < s_drivePathLen - 1) {
            int cR = UnpackRow(s_drivePath[s_drivePathIdx]);
            int cC = UnpackCol(s_drivePath[s_drivePathIdx]);
            int nR = UnpackRow(s_drivePath[s_drivePathIdx + 1]);
            int nC = UnpackCol(s_drivePath[s_drivePathIdx + 1]);
            int dCur  = abs(pfr - cR) + abs(pfc - cC);
            int dNext = abs(pfr - nR) + abs(pfc - nC);
            if (dNext <= dCur) s_drivePathIdx++; else break;
        }
    }

    // #67 v0.18.3.63: steering target = a few cells AHEAD ALONG THE PATH, not the
    // first cell >=2400u away in a straight line. The straight-line target could
    // land across a canyon wall (the route winds), so the player jammed aiming at
    // it and the wall-slide spun him the wrong way (.62 BAT drove to Galbadia
    // Garden, away from Dollet). DRIVE_LOOKAHEAD_CELLS along the route keeps the
    // aim inside the walkable corridor; the path cursor above holds idx at the
    // cell nearest the player, so this target stays a few cells ahead of him and
    // re-points him back onto the route if he drifts. On final approach / path
    // end, steer to the real destination coordinate.
    // #67 v0.18.3.78: steering target = the first route cell at least
    // DRIVE_PLAN_LOOKAHEAD_DIST (~2400u) AHEAD along the path, not the immediately
    // adjacent cell. The .77 BAT proved the greedy probe steers correctly but
    // ORBITS: with a 1-cell (~1024u) target, every ~180u walk step swung the
    // target bearing ~15deg, so the probe re-decided every window and circled the
    // first waypoint on open grass (idx stuck at 0, tgtBrg swinging wildly, dist
    // pinned ~15.8km) instead of committing north and crossing into the open field
    // the F11 showed right in front of him. A ~2400u target swings only ~4deg per
    // step, so the bearing is stable and the probe locks onto the northward
    // cardinal and carries him up the corridor. This restores the OLD
    // DRIVE_PLAN_LOOKAHEAD_DIST rule (cut to 1 cell in .64 only because the THEN
    // heading-steering drove straight at a far target and jammed into walls -- the
    // probe doesn't, it rejects walled cardinals and staircases, so a far target
    // is now safe AND necessary to stop the orbit). On final approach / path end,
    // steer to the real destination coordinate.
    int32_t steerX = s_driveTargetX;
    int32_t steerY = s_driveTargetY;
    if (s_drivePathPlanned && s_drivePathLen > 0 &&
        s_drivePathIdx < s_drivePathLen - 1 &&
        dist >= DRIVE_FINAL_APPROACH_DIST) {
        int wi = s_drivePathIdx;
        for (int j = s_drivePathIdx; j <= s_drivePathLen - 1; ++j) {
            int32_t cx, cy;
            FineCellCenterToWorld(UnpackCol(s_drivePath[j]), UnpackRow(s_drivePath[j]), &cx, &cy);
            int32_t ddx = cx - px, ddy = cy - py;
            WrapWorldDelta(ddx, ddy);
            wi = j;
            if ((double)ddx * ddx + (double)ddy * ddy >=
                DRIVE_PLAN_LOOKAHEAD_DIST * DRIVE_PLAN_LOOKAHEAD_DIST)
                break;
        }
        // #67 v0.18.3.82: clamp the far lookahead target back to the farthest
        // route cell reachable from the player by a CLEAR straight line. The
        // route WINDS around cliffs, so a ~2400u-ahead target can sit on the far
        // side of a cliff corner the route goes around; aiming straight at it
        // drives the character INTO the cliff. The .81 Dollet BAT wedged exactly
        // this way at fine(103,64): the steer target was NNW across the blocked
        // fine(103,63) cliff, so he pressed north into the rock, and on the
        // world map's tank controls rotation needs forward motion -- nosed into
        // terrain he could neither advance nor pivot out (hdg frozen at 36).
        // Walking wi back to a clear-LOS cell makes him aim ALONG the open
        // corridor (here due WEST, around the cliff) and turn there in open
        // ground before reaching the rock. Engages ONLY near walls; on open
        // ground the far line is already clear so the far target stands
        // (stable bearing, no orbit -- preserves the .78 fix). The route cursor
        // holds s_drivePathIdx on the cell nearest the player, whose line is
        // trivially clear, so the clamp always terminates with a valid target.
        {
            int pfc = WorldXToFineCol(px), pfr = WorldYToFineRow(py);
            while (wi > s_drivePathIdx &&
                   !FineLineClearFootCar(pfc, pfr,
                                         UnpackCol(s_drivePath[wi]),
                                         UnpackRow(s_drivePath[wi]))) {
                wi--;
            }
        }
        FineCellCenterToWorld(UnpackCol(s_drivePath[wi]), UnpackRow(s_drivePath[wi]),
                              &steerX, &steerY);
    }

    // #67 v0.18.3.66: CLOSED-LOOP steering on the REAL facing. The .65 F12
    // heading scan PROVED WM_HEADING (0x0203ED02, what GetWorldMapHeading reads)
    // IS the live facing -- holding RIGHT raised it ~80/sample (clockwise), LEFT
    // lowered it, in the SAME compass units as TorusBearing (0 = North,
    // clockwise). The earlier "frozen" reads were the character not rotating
    // while jammed forward into terrain, NOT a dead sensor. So read the real
    // heading and TURN-THEN-GO: when off-aim, PIVOT with turn-only (no forward,
    // so the engine rotates him freely and the heading updates -- exactly the
    // scan's turn-only phase); once aligned (within STEER_DEADZONE), drive
    // forward. A true heading lets the pivot stop the instant we're aligned --
    // no oscillation, no wall-slide corruption, no motion-derivation, and no
    // arc-into-wall (holding UP into terrain is what locked the old rotation).
    int targetBearing = TorusBearing(px, py, steerX, steerY);
    int err = ((int)targetBearing - (int)heading) & 0xFFF;
    if (err > 2048) err -= 4096;             // signed [-2048,2048], 0 = dead ahead
    int off = err < 0 ? -err : err;

    // Wall-jam watchdog: if he stops moving while aimed (nosed into terrain),
    // force a pivot to find open ground.
    if (CalculateWrappedDistance(s_driveLastMovePosX, s_driveLastMovePosY, px, py) >= WALL_SLIDE_EPS) {
        s_driveLastMoveTime = now;
        s_driveLastMovePosX = px;
        s_driveLastMovePosY = py;
    }
    bool wallJam = (now - s_driveLastMoveTime) > WALL_SLIDE_MS;

    bool wantUp = false, wantLeft = false, wantRight = false, wantDown = false;
    bool fwdGuard = false;   // #67 v0.18.3.83: forward-collision guard fired this tick

    // #67 v0.18.3.68: REVERSE un-wedge. On the world map the character only
    // rotates WHILE moving forward, so once he noses into terrain he can't move,
    // can't turn, and his heading freezes (the recurring 15km coastal-corner
    // jam: hdg stuck at 501, pressing forward into the same wall forever). The
    // one move always available is BACKWARD -- reversing off the wall regains
    // open ground and motion, and turning toward the target while reversing
    // brings him out re-aimed. Fires on a HARD wedge (no real movement for
    // HARD_WEDGE_MS, longer than the WALL_SLIDE_MS slide window), as a bounded
    // burst; the progress watermark above renews the per-episode budget the
    // moment a reverse actually gets him closer, so a true hard-jam still falls
    // through to stuck detection / re-plan / give-up rather than reversing forever.
    // #67 v0.18.3.69: wedge = no NET travel, not no movement. The .68 BAT showed
    // the jam is a wall-VIBRATION -- the player bounces ~60u east-west every frame
    // (per-frame d+ 30-62) while netting ~zero progress, which kept resetting the
    // move-timer so the .68 hard-wedge (keyed on no-movement) NEVER fired and the
    // reverse never triggered. Key it on NET displacement from an anchor: the
    // anchor only resets when he genuinely relocates WEDGE_NET_EPS, so vibration-
    // in-place still counts as wedged once HARD_WEDGE_MS elapses.
    if (CalculateWrappedDistance(s_driveWedgeAnchorX, s_driveWedgeAnchorY, px, py) >= WEDGE_NET_EPS) {
        s_driveWedgeAnchorX    = px;
        s_driveWedgeAnchorY    = py;
        s_driveWedgeAnchorTime = now;
    }
    bool inReverseBurst = (s_driveWedgeReverseUntil != 0 && now < s_driveWedgeReverseUntil);
    if (!inReverseBurst && s_driveWedgeReverseUntil != 0) {
        s_driveWedgeReverseUntil = 0;        // burst ended; re-anchor for a fresh window
        s_driveWedgeAnchorX      = px;
        s_driveWedgeAnchorY      = py;
        s_driveWedgeAnchorTime   = now;
    }
    bool hardWedge = (now - s_driveWedgeAnchorTime) > HARD_WEDGE_MS;

    // #67 v0.18.3.80: on-foot UNIFIED onto the heading-based turn-then-go (the
    // vehicle branch below). The v0.18.3.79 [YAWPROBE] BAT proved 0x0203ED02 IS
    // the LIVE on-foot facing -- NOT frozen, NOT screen-relative: holding UP walks
    // a straight line whose COMPASS bearing == the yaw (matched within 1deg at two
    // different yaws in one run: yaw 316deg -> walked 315deg, yaw 355deg -> 356deg),
    // holding UP does NOT rotate the camera (yaw steady through each forward burst),
    // and RIGHT INCREASES the yaw (clockwise). So on foot desiredYaw == the target's
    // TorusBearing and the vehicle law err=TorusBearing-heading / turn-then-go
    // applies verbatim with ZERO offset. The .74-.78 screen-basis + greedy-probe
    // detour was built on the now-disproven "frozen heading" reading; it is bypassed
    // (gate forced false; left in place as dead code like the retired calibration)
    // so EVERYONE uses the heading steering below.
    if (false && isOnFoot) {
        // ===== #67 v0.18.3.74: SCREEN-RELATIVE self-calibrating steering =====
        // On foot, arrows WALK screen-relative and WM_HEADING is frozen (.73
        // diagnostic). Measure the screen->world basis from the character's own
        // motion, then press the arrow combo whose screen direction points at the
        // target. No heading, no pivot, no camera control.
        //
        // (A) Calibrate the basis once per drive: UP -> world delta = screen-up
        // vector; RIGHT -> screen-right vector. Returns each tick until done.
        if (s_driveCalPhase != DCAL_DONE) {
            DWORD el = now - s_driveCalStart;
            switch (s_driveCalPhase) {
                case DCAL_PROBE_UP:
                    SetDriveKeys(true, false, false, false);
                    if (el >= DRIVE_CAL_PROBE_MS) {
                        int32_t cdx = px - s_driveCalX, cdy = py - s_driveCalY;
                        WrapWorldDelta(cdx, cdy);
                        double cl = sqrt((double)cdx * cdx + (double)cdy * cdy);
                        if (cl >= DRIVE_CAL_MIN_MOVE) {
                            s_camUx = cdx / cl; s_camUy = cdy / cl;
                            Log::World("WorldMap: [SRDIAG] CAL UP -> uHat=(%.3f,%.3f) from d(%d,%d) len=%.0f",
                                       s_camUx, s_camUy, cdx, cdy, cl);
                            s_driveCalPhase = DCAL_SETTLE_UR; s_driveCalStart = now; s_driveCalTry = 0;
                            SetDriveKeys(false, false, false, false);
                        } else if (++s_driveCalTry >= DRIVE_CAL_MAX_TRY) {
                            Log::World("WorldMap: [SRDIAG] CAL UP no move (len=%.0f); keeping uHat default (%.3f,%.3f)",
                                       cl, s_camUx, s_camUy);
                            s_driveCalPhase = DCAL_SETTLE_UR; s_driveCalStart = now; s_driveCalTry = 0;
                            SetDriveKeys(false, false, false, false);
                        } else {
                            s_driveCalX = px; s_driveCalY = py; s_driveCalStart = now;
                        }
                    }
                    return;
                case DCAL_SETTLE_UR:
                    SetDriveKeys(false, false, false, false);
                    if (el >= DRIVE_CAL_SETTLE_MS) {
                        s_driveCalPhase = DCAL_PROBE_RIGHT; s_driveCalStart = now;
                        s_driveCalX = px; s_driveCalY = py; s_driveCalTry = 0;
                    }
                    return;
                case DCAL_PROBE_RIGHT:
                    SetDriveKeys(false, false, true, false);
                    if (el >= DRIVE_CAL_PROBE_MS) {
                        int32_t cdx = px - s_driveCalX, cdy = py - s_driveCalY;
                        WrapWorldDelta(cdx, cdy);
                        double cl = sqrt((double)cdx * cdx + (double)cdy * cdy);
                        if (cl >= DRIVE_CAL_MIN_MOVE) {
                            s_camRx = cdx / cl; s_camRy = cdy / cl;
                            Log::World("WorldMap: [SRDIAG] CAL RIGHT -> rHat=(%.3f,%.3f) from d(%d,%d) len=%.0f | basis ready",
                                       s_camRx, s_camRy, cdx, cdy, cl);
                            s_camBasisValid = true; s_driveCalPhase = DCAL_DONE;
                            SetDriveKeys(false, false, false, false);
                        } else if (++s_driveCalTry >= DRIVE_CAL_MAX_TRY) {
                            s_camRx = -s_camUy; s_camRy = s_camUx;   // perpendicular fallback; live-refresh fixes handedness
                            Log::World("WorldMap: [SRDIAG] CAL RIGHT no move (len=%.0f); derived rHat=(%.3f,%.3f) from uHat",
                                       cl, s_camRx, s_camRy);
                            s_camBasisValid = true; s_driveCalPhase = DCAL_DONE;
                            SetDriveKeys(false, false, false, false);
                        } else {
                            s_driveCalX = px; s_driveCalY = py; s_driveCalStart = now;
                        }
                    }
                    return;
                default:
                    s_driveCalPhase = DCAL_DONE;
                    break;
            }
        }

        // ===== #67 v0.18.3.77: GREEDY EMPIRICAL ARROW PROBE =====
        // PIVOT off the maintained screen->world basis. The .74/.75/.76 BATs all
        // sawed east-west at the route corner: a predicted basis is unreliable
        // exactly where the camera swings, and a 2-key diagonal (U-L-) drove him
        // WEST when the basis said NNE. This trusts ONLY measured progress: hold
        // ONE cardinal arrow for a window, measure whether it moved him toward the
        // steer target; KEEP it if the motion both happened (>=MIN_MOVE) and lined
        // up with the target (align>=ALIGN_MIN), else ROTATE to the next cardinal.
        // No basis, no trig, no diagonals -- can't be fooled by camera swing or
        // wall-slide. Single-arrow motion is clean; only the basis + 2-key combo
        // were the problem. Around the corner this naturally STAIRCASES: it
        // rejects the walled direction (<MIN_MOVE) and takes the open cardinal
        // that actually progresses toward the next route cell.
        bool inSidestep = (s_driveSidestepUntil != 0 && now < s_driveSidestepUntil);

        int32_t tdx = steerX - px, tdy = steerY - py;
        WrapWorldDelta(tdx, tdy);
        double tl = sqrt((double)tdx * tdx + (double)tdy * tdy);
        double thx = (tl >= 1.0) ? tdx / tl : 0.0;
        double thy = (tl >= 1.0) ? tdy / tl : 0.0;

        if (inSidestep) {
            // keep the probe window fresh so it re-evaluates cleanly once the
            // lateral slide ends
            s_driveProbeAnchorX = px; s_driveProbeAnchorY = py; s_driveProbeTime = now;
        } else if (!s_driveProbeValid) {
            s_driveProbeValid   = true;
            s_driveProbeArrow   = 0;                                // start by trying UP
            s_driveProbeAnchorX = px; s_driveProbeAnchorY = py;
            s_driveProbeTime    = now; s_driveProbeFails = 0;
        } else if (now - s_driveProbeTime >= DRIVE_PROBE_WINDOW_MS) {
            int32_t wdx = px - s_driveProbeAnchorX, wdy = py - s_driveProbeAnchorY;
            WrapWorldDelta(wdx, wdy);
            double disp  = sqrt((double)wdx * wdx + (double)wdy * wdy);
            double along = (disp >= 1.0) ? (wdx * thx + wdy * thy) / disp : 0.0;  // alignment with target dir
            bool good = (disp >= DRIVE_PROBE_MIN_MOVE) && (along >= DRIVE_PROBE_ALIGN_MIN);
            if (good) {
                s_driveProbeFails = 0;                                 // committed arrow is working -- keep it
            } else {
                s_driveProbeArrow = (s_driveProbeArrow + 1) & 3;       // rotate UP->RIGHT->DOWN->LEFT
                if (++s_driveProbeFails >= DRIVE_PROBE_MAX_FAILS) {     // no cardinal progresses: genuine pocket
                    s_driveProbeFails = 0;
                    if (s_drivePlannerEligible && s_drivePathPlanned &&
                        s_driveReplanCount < DRIVE_MAX_REPLANS) {
                        s_driveReplanCount++;
                        Log::World("WorldMap: [DRIVE] Probe found no progressing arrow -- re-planning (recovery %d/%d)",
                                   s_driveReplanCount, DRIVE_MAX_REPLANS);
                        PlanDrivePath(px, py);
                        s_driveProbeValid = false;
                        return;
                    }
                }
            }
            s_driveProbeAnchorX = px; s_driveProbeAnchorY = py; s_driveProbeTime = now;
        }

        if (inSidestep) {
            wantRight = (s_driveSidestepSign > 0);
            wantLeft  = (s_driveSidestepSign < 0);
        } else if (tl >= 1.0) {
            switch (s_driveProbeArrow) {
                case 0: wantUp    = true; break;
                case 1: wantRight = true; break;
                case 2: wantDown  = true; break;
                case 3: wantLeft  = true; break;
            }
        }

        if (DRIVE_STEER_DIAG) {
            static DWORD   s_srLast  = 0;
            static int32_t s_srLastX = 0, s_srLastY = 0;
            if (now - s_srLast >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
                double dmoved = CalculateWrappedDistance(s_srLastX, s_srLastY, px, py);
                const char* an = (s_driveProbeArrow == 0) ? "UP"
                               : (s_driveProbeArrow == 1) ? "RIGHT"
                               : (s_driveProbeArrow == 2) ? "DOWN" : "LEFT";
                Log::World("WorldMap: [SRDIAG] pos(%d,%d) d+%.0f | tgtBrg=%d arrow=%s fails=%d | dist=%.0f idx=%d/%d keys=%s%s%s%s%s",
                           px, py, dmoved, targetBearing, an, s_driveProbeFails,
                           dist, s_drivePathIdx, s_drivePathLen,
                           wantUp ? "U" : "-", wantDown ? "D" : "-",
                           wantLeft ? "L" : "-", wantRight ? "R" : "-",
                           inSidestep ? " SIDESTEP" : "");
                s_srLast = now; s_srLastX = px; s_srLastY = py;
            }
        }
    } else if (isOnFoot) {
        // ===== #67 v0.18.3.87: YAW-BASED SCREEN-RELATIVE 8-WAY STEERING =====
        // On foot, hdg (0x0203ED02) is the FIXED per-region CAMERA YAW, not a
        // steerable facing: .79's [YAWPROBE] proved holding UP walks a straight
        // line at the yaw bearing (within 1deg) and RIGHT walks screen-right
        // (yaw +90 CW). So the arrows are screen-relative WALK keys, and the
        // .80-.86 heading turn-then-go CANNOT steer here -- it presses pure UP
        // (= the yaw direction). On the .86 road BAT that was fatal: routing was
        // SOLVED (Squall stood ON the road, route correct up the road), but the
        // road ran due north while the yaw pointed NNW, so UP walked him ~28deg
        // off the thin road into the cliff beside it, every time (he could not
        // press the diagonal needed to track the road). FIX: steer in SCREEN
        // space. screenAngle = targetBearing - yaw is the target's direction
        // relative to screen-up (what UP walks); press the nearest of 8 arrow
        // combos (cardinals + diagonals) so he walks toward the target and
        // STAIRCASES along the road's bends. No turn-then-go, no pivot (a fixed
        // camera can't be rotated), no basis/probe. The reverse-burst un-wedge
        // (DOWN = screen-down, backs him off a wall) is kept for stuck recovery.
        if (inReverseBurst) {
            wantDown = true;
        } else {
            int screenAngle = ((int)targetBearing - (int)heading) & 0xFFF;
            // Nearest 8-way sector (each 512 wide, centred on k*512). RIGHT
            // walks screen-right = yaw +90 CW (screenAngle +1024) -- the ONE
            // handedness assumption; if the BAT shows he tracks bends the wrong
            // way (steers consistently to the wrong side at a turn), swap
            // RIGHT<->LEFT below (equivalently negate screenAngle). One line.
            int sector = ((screenAngle + 256) >> 9) & 7;
            switch (sector) {
                case 0: wantUp = true;                       break; // screen-up
                case 1: wantUp = true; wantRight = true;     break; // up-right
                case 2: wantRight = true;                    break; // right
                case 3: wantDown = true; wantRight = true;   break; // down-right
                case 4: wantDown = true;                     break; // down
                case 5: wantDown = true; wantLeft = true;    break; // down-left
                case 6: wantLeft = true;                     break; // left
                case 7: wantUp = true; wantLeft = true;      break; // up-left
            }
        }
        if (DRIVE_STEER_DIAG) {
            static DWORD   s_yawLast  = 0;
            static int32_t s_yawLastX = 0, s_yawLastY = 0;
            if (now - s_yawLast >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
                double dmoved = CalculateWrappedDistance(s_yawLastX, s_yawLastY, px, py);
                int scrAng = ((int)targetBearing - (int)heading) & 0xFFF;
                Log::World("WorldMap: [YAWDRIVE] pos(%d,%d) d+%.0f | yaw=%u tgtBrg=%d scrAng=%d | steer(%d,%d) dist=%.0f idx=%d/%d | keys=%s%s%s%s%s%s",
                           px, py, dmoved, (unsigned)heading, targetBearing, scrAng,
                           steerX, steerY, dist, s_drivePathIdx, s_drivePathLen,
                           wantUp ? "U" : "-", wantDown ? "D" : "-",
                           wantLeft ? "L" : "-", wantRight ? "R" : "-",
                           inReverseBurst ? " REVERSE" : "", wallJam ? " JAM" : "");
                s_yawLast = now; s_yawLastX = px; s_yawLastY = py;
            }
        }
    } else {
    if (inReverseBurst) {
        // #67 v0.18.3.71: DOWN-only reverse off the wall. Aaron confirmed DOWN
        // moves the character on the world map, so this is a clean backward burst
        // into open ground; normal steering re-aims him once he's free. No
        // simultaneous turn -- turning while wedged does nothing (.68/.69) and
        // only muddied the motion. The burst is now TRIGGERED from the stuck
        // check (proven to fire) rather than the hard-wedge vibration detector,
        // which never fired across the .68/.69/.70 BATs.
        wantDown = true;
    } else if (hardWedge && dist >= FINAL_APPROACH_FORWARD_DIST &&
               s_driveWedgeReverseCount < MAX_WEDGE_REVERSE) {
        // Vestigial fast-path: net-displacement hard-wedge (rarely trips).
        // DOWN-only, same as above; the stuck-check trigger is primary.
        s_driveWedgeReverseUntil = now + REVERSE_BURST_MS;
        s_driveWedgeReverseCount++;
        wantDown = true;
        Log::World("WorldMap: [DRIVE] Hard wedge -> reverse un-wedge burst %d/%d (off=%d, dist=%.0f)",
                   s_driveWedgeReverseCount, MAX_WEDGE_REVERSE, off, dist);
    } else if (dist < FINAL_APPROACH_FORWARD_DIST) {
        wantUp = true;                       // very close: walk straight onto the trigger
    } else if (off > STEER_DEADZONE) {
        // PIVOT, turn-only. RIGHT raises the heading (clockwise) toward a target
        // clockwise of us (err>=0); LEFT lowers it. NO forward -- holding UP into
        // terrain locks the rotation.
        if (err >= 0) wantRight = true;
        else          wantLeft  = true;
    } else {
        // #67 v0.18.3.83: FORWARD-COLLISION GUARD. off<=STEER_DEADZONE means
        // "roughly aimed" -- but roughly-aimed-into-a-cliff is still a cliff. The
        // .82 BAT wedged with off=318 just inside the ~320 deadzone, walking due
        // north into the blocked fine(103,63) the route goes WEST around (hdg
        // frozen at 36 the whole time -- he never pivoted because off never
        // exceeded the deadzone, and the reverse just backed him into the same
        // wall again). So before committing UP, look where he's about to step:
        // probe the fine cell ~1 cell ahead of the CURRENT facing/camera-up, and
        // if it's blocked for foot/car, do NOT go straight -- press toward the
        // target side (err sign) instead. On this region's screen-relative
        // controls that walks him sideways into the open corridor; on a
        // turn-then-go region it pivots him there. Either way he stops nosing into
        // the rock. The guard reads the grid in front of him, so it fires ONLY
        // when something is actually there -- open-road steering and the deadzone
        // are unchanged (no orbit/oscillation regression).
        double th   = (double)heading / 4096.0 * 6.283185307179586;
        double dirX = sin(th), dirY = -cos(th);   // heading 0=N(-Y), clockwise; +X=E
        int32_t aheadX = px + (int32_t)(dirX * 1024.0);   // one fine cell ahead
        int32_t aheadY = py + (int32_t)(dirY * 1024.0);
        int afc = WorldXToFineCol(aheadX), afr = WorldYToFineRow(aheadY);
        if (afc >= 0 && afc < WM_FINE_COLS && afr >= 0 && afr < WM_FINE_ROWS) {
            uint8_t acls = s_walkClassFine[afr][afc];
            if (acls == SEG_OCEAN ||
                (acls == SEG_MOUNTAIN && s_steepFine[afr][afc] > WM_MTN_STEEP_BLOCK))
                fwdGuard = true;
        }
        if (fwdGuard) {
            if (err >= 0) wantRight = true;      // toward the target / open route side
            else          wantLeft  = true;
        } else {
            wantUp = true;                       // aligned and clear -> drive straight
        }
    }

    // #67 v0.18.3.66/.68: heading-VERIFICATION trace. hdg should rotate toward
    // tgtBrg while PIVOTing/REVERSE, err should shrink, off small on STRAIGHT.
    // Set DRIVE_STEER_DIAG=false before the #67 push.
    if (DRIVE_STEER_DIAG) {
        static DWORD   s_diagLast  = 0;
        static int32_t s_diagLastX = 0;
        static int32_t s_diagLastY = 0;
        if (now - s_diagLast >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
            double dmoved = CalculateWrappedDistance(s_diagLastX, s_diagLastY, px, py);
            const char* band = wantDown ? "REVERSE"
                             : (dist < FINAL_APPROACH_FORWARD_DIST) ? "FINAL"
                             : wantUp  ? "STRAIGHT"
                             : "PIVOT";
            Log::World("WorldMap: [HDG-DIAG] pos(%d,%d) d+%.0f | hdg=%u tgtBrg=%d err=%+d off=%d | steer(%d,%d) dist=%.0f idx=%d/%d | keys=%s%s%s%s %s%s%s",
                       px, py, dmoved, (unsigned)heading,
                       targetBearing, err, off,
                       steerX, steerY, dist, s_drivePathIdx, s_drivePathLen,
                       wantUp ? "U" : "-", wantDown ? "D" : "-",
                       wantLeft ? "L" : "-", wantRight ? "R" : "-",
                       band, wallJam ? " JAM" : "", fwdGuard ? " GUARD" : "");
            s_diagLast  = now;
            s_diagLastX = px;
            s_diagLastY = py;
        }
    }
    }  // end vehicle (else) heading-based steering

    SetDriveKeys(wantUp, wantLeft, wantRight, wantDown);

    // #67 v0.18.3.74: record this tick's keys + position for the next-tick
    // on-foot basis refresh (the live screen->world calibration).
    s_drivePrevUp      = wantUp;
    s_drivePrevDown    = wantDown;
    s_drivePrevLeft    = wantLeft;
    s_drivePrevRight   = wantRight;
    s_drivePrevX       = px;
    s_drivePrevY       = py;
    s_drivePrevHadKeys = (wantUp || wantDown || wantLeft || wantRight);
}
