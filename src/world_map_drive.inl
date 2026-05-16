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
    // v0.14.102: also release the gas pedal (A key, NOT extended).
    if (s_keyGasHeld)   { ReleaseKey('A',      0x1E, false); s_keyGasHeld = false; }
}

// Idempotent press/release: only generates events on state changes.
static void SetDriveKeys(bool up, bool left, bool right)
{
    if (up    && !s_keyUpHeld)    { PressKey(VK_UP,    0x48); s_keyUpHeld    = true; }
    if (!up   &&  s_keyUpHeld)    { ReleaseKey(VK_UP,  0x48); s_keyUpHeld    = false; }
    if (left  && !s_keyLeftHeld)  { PressKey(VK_LEFT,  0x4B); s_keyLeftHeld  = true; }
    if (!left &&  s_keyLeftHeld)  { ReleaseKey(VK_LEFT, 0x4B); s_keyLeftHeld = false; }
    if (right && !s_keyRightHeld) { PressKey(VK_RIGHT, 0x4D); s_keyRightHeld = true; }
    if (!right&&  s_keyRightHeld) { ReleaseKey(VK_RIGHT,0x4D); s_keyRightHeld = false; }
    // v0.14.102: gas pedal mirrors UP arrow. A key (scan 0x1E, NOT extended).
    if (up    && !s_keyGasHeld)   { PressKey('A',      0x1E, false); s_keyGasHeld   = true; }
    if (!up   &&  s_keyGasHeld)   { ReleaseKey('A',    0x1E, false); s_keyGasHeld   = false; }
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
    s_driveApproachAnnounced = (dist < DRIVE_APPROACH_DIST);
    s_finalApproachEnterTick = 0;
    s_sweepActive            = false;
    s_sweepPhase             = 0;
    s_sweepTurning           = true;

    s_driveOnFootAtStart = (s_lastVehicle < 0) ||
                           (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

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
            if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount >= 2) {
                Log::World("WorldMap: [DRIVE] Stuck in final approach → sweep");
                StartSweep(px, py, now);
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

    // v0.14.94: waypoint advancement.
    if (s_drivePathPlanned && s_drivePathIdx < s_drivePathLen) {
        int playerRow = WorldYToSegRow(py);
        int playerCol = WorldXToSegCol(px);
        int wpRow = UnpackRow(s_drivePath[s_drivePathIdx]);
        int wpCol = UnpackCol(s_drivePath[s_drivePathIdx]);
        if (playerRow == wpRow && playerCol == wpCol) {
            s_drivePathIdx++;
            Log::World("WorldMap: [PLAN] Reached waypoint %d/%d at seg(%d,%d) \u2014 advancing",
                       s_drivePathIdx, s_drivePathLen, wpCol, wpRow);
        }
    }

    // Steering target selection.
    int32_t steerX = s_driveTargetX;
    int32_t steerY = s_driveTargetY;
    if (s_drivePathPlanned && s_drivePathIdx < s_drivePathLen) {
        int wpRow = UnpackRow(s_drivePath[s_drivePathIdx]);
        int wpCol = UnpackCol(s_drivePath[s_drivePathIdx]);
        SegmentCenterToWorld(wpCol, wpRow, &steerX, &steerY);
    }

    // Steering decision.
    int targetBearing = TorusBearing(px, py, steerX, steerY);
    int relBearing    = (targetBearing - (int)heading + 4096) & 0xFFF;

    bool wantUp = false, wantLeft = false, wantRight = false;

    if (dist < DRIVE_FINAL_APPROACH_DIST) {
        // v0.14.100: bearing-based steering even in final approach.
        if (dist < FINAL_APPROACH_FORWARD_DIST) {
            wantUp = true;
        } else {
            if (relBearing < 200 || relBearing > 3896) {
                wantUp = true;
            } else if (relBearing < 1800) {
                wantRight = true;
                if (relBearing < 512) wantUp = true;
            } else {
                wantLeft = true;
                if (relBearing > 3584) wantUp = true;
            }
        }
    } else if (relBearing < 200 || relBearing > 3896) {
        wantUp = true;
    } else if (relBearing < 1800) {
        wantRight = true;
        if (relBearing < 512) wantUp = true;
    } else {
        wantLeft = true;
        if (relBearing > 3584) wantUp = true;
    }

    SetDriveKeys(wantUp, wantLeft, wantRight);
}
