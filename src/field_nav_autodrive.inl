// field_nav_autodrive.inl — Auto-drive state machine, steering, recovery
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.17.8.20: Low-level helpers (InjectKey, ReleaseAllDirections,
//   SetHeldDirections, SetAnalogFromVector, StopAutoDrive) moved to
//   field_nav_autodrive_helpers.inl, and the CALIB phase 1/2 state machine
//   moved to field_nav_autodrive_calib.inl::RunCalibration(), to drop this
//   file below the 80 KB hard cap. This file now holds only UpdateAutoDrive.
//   Both new files are included immediately before this one in
//   field_navigation.cpp. ZERO behavior change.

// Called from Update() every tick while auto-drive is active.
// Computes direction to target, injects appropriate arrow keys.
static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    // Safety: must be on field and not in a menu/FMV.
    if (!FF8Addresses::IsOnField()) { StopAutoDrive("Left field."); return; }

    // v0.18.3.306 (#111): distinct message. This branch and the
    // GetEntityPos() failure below both used to say "Player position lost.",
    // so the .305 log could not say which had fired and I had to reason it
    // out from surrounding lines. They are different faults: this one means
    // we never identified the player entity, the other means we momentarily
    // could not read a known entity.
    if (s_playerEntityIdx < 0) { StopAutoDrive("Player entity unknown."); return; }

    // v0.17.8.20: CALIB phase 1/2 state machine extracted to
    // field_nav_autodrive_calib.inl. RunCalibration() returns true while a
    // calibration tick is consumed (phases 1-2) — caller returns immediately,
    // exactly as the two inline `return;`s here used to; false once done
    // (phase 3) or idle (phase 0), so we fall through to normal navigation.
    if (RunCalibration()) return;

    // v0.15.9.2.1: Chase-drive bypasses the entity catalog. chase_auto_pilot
    // doesn't have an entity to target -- it uses raw (X, Y) coords stored
    // in s_chaseDriveTargetX/Y by StartChaseDrive. Setting ei=-1 here is a
    // sentinel that skips entity-specific branches in the rest of this
    // function. Without this gate, v0.15.9.2 BAT showed UpdateAutoDrive
    // reading s_catalog[s_selectedCatalogIdx] after calibration, matching
    // its stale entityIdx against the player, and firing the loud
    // StopAutoDrive("No target.") SAPI announce.
    const EntityInfo& catTarget = (s_selectedCatalogIdx < s_catalogCount)
                                   ? s_catalog[s_selectedCatalogIdx]
                                   : s_catalog[0]; // safety fallback
    int ei;
    if (s_chaseDriveActive) {
        ei = -1;  // sentinel: chase-drive has no entity target
    } else {
        ei = catTarget.entityIdx;
        if (ei == s_playerEntityIdx) { StopAutoDrive("No target."); return; }
        // Valid targets: >=0 entity, <=-200 trigline, <=-300 JSM, <=-400 INF-gw.
        if (ei < 0 && ei > -200) { StopAutoDrive("Target lost."); return; }
        if (ei >= MAX_ENTITIES)                              { StopAutoDrive("Target lost."); return; }
    }

    // v0.18.3.318 (#114): transition COMPLETION detection. For a transition target
    // (trigger-line exit / stairs / ring-crossing gateway) the only clean success
    // is the floor or field ACTUALLY changing. Capture the start state on the
    // first tick, then each tick compare -- this fires the instant the
    // descent/crossing completes, even while the player is still off the walkmesh
    // during the animation (field id / shaft floor are separate globals that stay
    // readable), so the drive ends with an accurate announcement instead of
    // "Arrived" at the edge or a premature hand-off. Runs BEFORE the dialog and
    // position gates so a scripted-cutscene descent can never hide it. Non-chase
    // only (chase-drive is silent and has no catalog target).
    if (!s_chaseDriveActive && s_driveTrigTarget) {
        uint16_t curFid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        int curFloor = ReadShaftFloor();   // -1 when not on a prison-shaft field
        if (!s_driveTransCaptured) {
            if (curFid != 0xFFFF && curFid != 0) {   // wait for a valid field id before capturing
                s_driveTransStartFieldId = curFid;
                s_driveTransStartFloor   = curFloor;
                const char* nm = (catTarget.name[0] != '\0') ? catTarget.name : "Through.";
                snprintf(s_driveTransName, sizeof(s_driveTransName), "%s", nm);
                s_driveTransCaptured = true;
            }
        } else if ((curFid != 0xFFFF && curFid != 0 && curFid != s_driveTransStartFieldId) ||
                   (curFloor >= 1 && curFloor != s_driveTransStartFloor)) {
            Log::Field("FieldNavigation: [drive] transition COMPLETE -- %s changed "
                       "(field 0x%04X->0x%04X, floor %d->%d); '%s' reached [v0.18.3.318 #114]",
                       (curFid != s_driveTransStartFieldId) ? "field" : "floor",
                       (unsigned)s_driveTransStartFieldId, (unsigned)curFid,
                       s_driveTransStartFloor, curFloor, s_driveTransName);
            StopAutoDrive(s_driveTransName);
            return;
        }
    }

    // v05.37: Suspend key injection during dialog (scripted cutscenes lock movement).
    // Don't stop the drive — just pause until dialog clears.
    if (FieldDialog::IsDialogOpen()) {
        // Release any held keys so the game doesn't see stuck inputs.
        ReleaseAllDirections();
        // Freeze stuck counter so dialog pauses don't count as being stuck.
        s_driveStuckTicks = 0;
        return;
    }

    float px = 0, pz = 0, tx = 0, tz = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, pz)) {
        // v0.18.3.306 (#111): a single failed read is a transient, not a loss.
        // See the rationale on DRIVE_POS_LOST_TOLERANCE_TICKS in
        // field_nav_autodrive_helpers.inl -- the .305 log has the position
        // readable on the tick either side of every one of these.
        s_drivePosLostTicks++;
        if (s_drivePosLostTicks == 1) {
            // Log WHY once per dropout so the next BAT confirms the mechanism
            // rather than leaving it inferred. Reads the same fields
            // GetEntityPos() rejects on.
            unsigned triId = 0xFFFF; bool gotBase = false;
            ReadPlayerPlacementRaw(s_playerEntityIdx, gotBase, triId);
            Log::Field("FieldNavigation: [drive] player position unreadable — PAUSING, not stopping "
                       "(ent%d, othersBase=%s, tri=%u; tri==0 means the engine has the player "
                       "temporarily off the walkmesh). Tolerance %d ticks. [v0.18.3.306 #111]",
                       s_playerEntityIdx, gotBase ? "ok" : "NULL", triId,
                       (s_driveTrigTarget && !s_chaseDriveActive)
                           ? DRIVE_TRANSITION_TOLERANCE_TICKS : DRIVE_POS_LOST_TOLERANCE_TICKS);
        }
        // v0.18.3.318 (#114): survive the transition's off-walkmesh window -- for a
        // transition target the tri==0 run IS the descent/crossing (seconds long), and
        // completion-detection (top of fn) ends the drive on the floor/field change.
        int posLostTol = (s_driveTrigTarget && !s_chaseDriveActive)
                         ? DRIVE_TRANSITION_TOLERANCE_TICKS
                         : DRIVE_POS_LOST_TOLERANCE_TICKS;
        if (s_drivePosLostTicks >= posLostTol) {
            StopAutoDrive("Player position lost.");
            return;
        }
        // v0.18.3.322 (#114): a TRANSITION target KEEPS the last heading held through the
        // tri==0 window -- the .321 BAT parked the player one tap short of the descent
        // because releasing the movement keys stopped his final push; completion-detection
        // still ends it on the floor change. A non-transition dropout releases as before.
        if (!(s_driveTrigTarget && !s_chaseDriveActive))
            ReleaseAllDirections();
        s_driveStuckTicks = 0;
        return;
    }
    if (s_drivePosLostTicks > 0) {
        Log::Field("FieldNavigation: [drive] player position recovered after %d tick(s) — "
                   "drive continues [v0.18.3.306 #111]", s_drivePosLostTicks);
        s_drivePosLostTicks = 0;
    }
    // v0.07.74: JSM-injected entities use SET3 extraction positions.
    // v0.07.83: Trigger line exits use SETLINE center positions.
    // v0.07.94: INF gateway exits use deduplicated gateway center positions.
    // v0.15.9.2.1: Chase-drive uses raw target coords from s_chaseDriveTargetX/Y.
    bool gotTarget = false;
    if (s_chaseDriveActive) {
        tx = (float)s_chaseDriveTargetX;
        tz = (float)s_chaseDriveTargetY;
        gotTarget = true;
    } else if (ei <= -400) {
        int gwIdx = -(ei + 400);
        if (gwIdx >= 0 && gwIdx < s_dedupGatewayCount) {
            tx = s_dedupGateways[gwIdx].centerX;
            tz = s_dedupGateways[gwIdx].centerY;
            gotTarget = true;
        }
    } else if (ei <= -300) {
        int jsmIdx = -(ei + 300);
        if (jsmIdx >= 0 && jsmIdx < s_jsmEntityCount && s_jsmEntities[jsmIdx].hasPosition) {
            tx = (float)s_jsmEntities[jsmIdx].posX;
            tz = (float)s_jsmEntities[jsmIdx].posY;
            gotTarget = true;
        }
    } else if (ei <= -200) {
        int trigIdx = -(ei + 200);
        if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
            tx = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
            tz = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
            gotTarget = true;
        }
    } else if (ei >= 0) {
        gotTarget = GetEntityPos(ei, tx, tz);
    }
    if (!gotTarget) {
        StopAutoDrive("Target lost.");
        return;
    }
    // v0.18.3.307 (#112): capture the target BEFORE the trigger-line branch
    // below offsets tx/tz 300 units "past the line". That offset is applied
    // along the direction from the player's CURRENT position, so it moves as
    // the player moves. The odd-phase/nudge-fallback recovery re-paths were
    // seeding FindNearestTriangle() with the offset point, which made the A*
    // goal triangle a function of where the player happened to be standing:
    // the .306 BAT log shows FOUR different goals inside single drives
    // (48/20/1/22 -- drive start and the wiggle-completion re-path use the
    // raw center and correctly agree on 48), with the goal-1 route pulling
    // the player west and the goal-22 route pulling him back north-east.
    // Successive recoveries steered in opposite directions and the final
    // gpbig1a stairs drive ping-ponged between tri 8/9/10 for 40 seconds.
    // Every recovery re-path now aims at THIS stable point, the same goal
    // the drive started with.
    float rawTx = tx, rawTz = tz;
    float dx   = tx - px;
    float dz   = tz - pz;
    float dist = sqrtf(dx*dx + dz*dz);

    // v0.15.9.2.2: Save original target direction for keyboard heading bitmask.
    // The corridor-level steering below overwrites dx/dz to point at nearby
    // shared-edge midpoints. When that target is close (< 150 units in either
    // axis), the heading-bitmask threshold check fails for ALL four bits, and
    // the fallback fires DIR_UP. That presses the UP arrow while the analog
    // says SW (toward the actual target) -- the engine sees fighting inputs
    // and the party crawls instead of walking properly.
    //
    // Fix: heading bitmask uses the LONG-RANGE target direction (always passes
    // the 150 threshold), while the analog continues to use the corridor-tuned
    // dx/dz for fine steering. Keyboard "wake-up trigger" doesn't need to be
    // pixel-precise; it just needs to push in the right broad direction.
    float origDx = dx;
    float origDz = dz;

    // v05.76: For trigger line targets, check if the player has crossed the line.
    // This is the primary arrival condition for screen transitions and events.
    //
    // v0.15.9.2.14: Extended to chase-drive. Chase-drive's `ei` is the -1
    // sentinel (no entity target), so the original `ei <= -200` gate excluded
    // it. Now we read the trigger index from s_driveSkipTrigIdx (chase-drive
    // sets it when StartChaseDrive is given a trigger line) and use the same
    // cross-product sign-flip detection as F9.
    //
    // v0.15.9.2.15: Chase-drive now uses explicit endpoint state
    // (s_driveCrossLine*) instead of indexing into s_capturedLines. This lets
    // INF gateways drive the same crossing logic -- gateways aren't in
    // s_capturedLines (which only holds SETLINE-defined trigger lines).
    //
    // v0.17.6.0: F9 auto-drive now ALSO populates s_driveCrossLine* when its
    // target is an INF gateway exit (entityIdx <= -400). The first branch's
    // condition was widened from `chase-drive + crossing-line-active` to just
    // `crossing-line-active` so it covers both paths uniformly. F9 trigger
    // lines (entityIdx <= -200) still take the second branch via the
    // s_capturedLines lookup -- handlekeys doesn't seed s_driveCrossLine* for
    // them (point-distance plus per-line geometry is the proven path).
    bool gotCrossLine = false;
    float tlx1 = 0, tly1 = 0, tlx2 = 0, tly2 = 0;
    // v0.18.3.319 (#114): segment-aim override for the final approach to a
    // transition trigger line (see the compute block below and the apply site
    // just before dx/dz). Fixes the .318 stairs failure where the funnel routed
    // the player across the line SOUTH of its active segment so the descent
    // never fired.
    float segAimX = 0.0f, segAimY = 0.0f;
    bool  useSegAim = false;
    const float SEG_AIM_ENGAGE_DIST = 450.0f;  // take over the funnel within this range of the segment middle
    const float SEG_AIM_PUSH        = 200.0f;  // aim this far past the segment (far side) so the crossing goes through
    if (s_driveTrigTarget) {
        if (s_driveCrossLineActive) {
            tlx1 = (float)s_driveCrossLineX1;
            tly1 = (float)s_driveCrossLineY1;
            tlx2 = (float)s_driveCrossLineX2;
            tly2 = (float)s_driveCrossLineY2;
            gotCrossLine = true;
        } else if (!s_chaseDriveActive && ei <= -200) {
            int trigCrossIdx = -(ei + 200);
            if (trigCrossIdx >= 0 && trigCrossIdx < s_capturedLineCount) {
                tlx1 = (float)s_capturedLines[trigCrossIdx].x1;
                tly1 = (float)s_capturedLines[trigCrossIdx].y1;
                tlx2 = (float)s_capturedLines[trigCrossIdx].x2;
                tly2 = (float)s_capturedLines[trigCrossIdx].y2;
                gotCrossLine = true;
            }
        }
    }
    if (gotCrossLine) {
        float tdx = tlx2 - tlx1;
        float tdy = tly2 - tly1;
        float crossNow = tdx * (pz - tly1) - tdy * (px - tlx1);
        // v0.18.3.319 (#114): within SEG_AIM_ENGAGE_DIST of the active segment,
        // steer at its middle half (not the funnel heading, which routed the .318
        // player SOUTH of the segment) and push SEG_AIM_PUSH past the far side so
        // the crossing goes through. Applied at the steer site below.
        {
            float sl2 = tdx * tdx + tdy * tdy;
            if (sl2 > 1.0f) {
                float tp = ((px - tlx1) * tdx + (pz - tly1) * tdy) / sl2;
                if (tp < 0.25f) tp = 0.25f; else if (tp > 0.75f) tp = 0.75f;
                float ax = tlx1 + tp * tdx, ay = tly1 + tp * tdy;
                float tX = ax - px, tY = ay - pz;
                if (sqrtf(tX * tX + tY * tY) < SEG_AIM_ENGAGE_DIST) {
                    float pX = -tdy, pY = tdx;
                    float pl = sqrtf(pX * pX + pY * pY);
                    if (pl > 0.001f) { pX /= pl; pY /= pl; }
                    if (pX * (px - ax) + pY * (pz - ay) > 0.0f) { pX = -pX; pY = -pY; }
                    segAimX = ax + pX * SEG_AIM_PUSH;
                    segAimY = ay + pY * SEG_AIM_PUSH;
                    useSegAim = true;
                }
            }
        }
        // Player has crossed if the sign flipped from start.
        // v0.18.3.304 (#107): ...AND the player must be ALONGSIDE the segment
        // when it flips. gpbig1a line1 (-2141,-89)->(-2158,-305) is nearly
        // Y-parallel, so its infinite extension flips the sign for any point
        // past X=-2150 -- the .303 BAT announced "Arrived" ~960u short of the
        // stairs. Bound it: project the player onto the line and require the
        // foot within the segment (15% end margin). Failing is the SAFE failure
        // (keep walking); a false "Arrived" is unrecoverable for a blind player.
        bool sideFlipped = (s_driveTrigCrossStart != 0.0f &&
                            crossNow * s_driveTrigCrossStart < 0.0f);
        if (sideFlipped) {
            float segLen2 = tdx * tdx + tdy * tdy;
            float tParam  = 0.5f;
            if (segLen2 > 1.0f)
                tParam = ((px - tlx1) * tdx + (pz - tly1) * tdy) / segLen2;
            const float TRIG_CROSS_END_MARGIN = 0.15f;
            if (tParam >= -TRIG_CROSS_END_MARGIN &&
                tParam <= 1.0f + TRIG_CROSS_END_MARGIN) {
                // v0.18.3.318 (#114): for a transition target, crossing the line is
                // NOT the end -- the descent/crossing must actually fire. Keep
                // steering through; the completion detection (floor/field change,
                // top of UpdateAutoDrive) ends the drive with the real outcome.
                if (!s_driveTrigTarget) {
                    StopAutoDrive("Arrived.");
                    return;
                }
            }
            static int s_boundedRejectLogged = 0;
            if (s_boundedRejectLogged < 8) {
                s_boundedRejectLogged++;
                Log::Field("FieldNavigation: [drive] side-flip on INF extension, "
                           "not alongside segment (t=%.2f) -- NOT arrived. "
                           "player=(%.0f,%.0f) line=(%.0f,%.0f)->(%.0f,%.0f) [#107]",
                           tParam, px, pz, tlx1, tly1, tlx2, tly2);
            }
        }
        // Also offset the target 300 units past the line center
        // so the heading aims through the line, not just to its center.
        float dirLen = sqrtf(dx*dx + dz*dz);
        if (dirLen > 1.0f) {
            tx += (dx / dirLen) * 300.0f;
            tz += (dz / dirLen) * 300.0f;
            dx = tx - px;
            dz = tz - pz;
            dist = sqrtf(dx*dx + dz*dz);
        }
    }
    // v0.18.3.318 (#114): a transition target must NOT "Arrive" by mere proximity
    // to the target point -- that is exactly what stopped the player ~350u short
    // of the stairs threshold in the .317 BAT. Transition drives end only via the
    // completion detection (floor/field change) at the top of UpdateAutoDrive; up
    // to then they steer through the trigger (bounded by driveMaxTicks / stuck).
    if (dist < s_driveArriveDist && !s_driveTrigTarget) {
        StopAutoDrive("Arrived.");
        return;
    }

    // v05.66: If we have A* waypoints, steer toward the current waypoint
    // instead of the final target. Advance to the next waypoint when close.
    // Chain-advance is delayed until tick 30 (~0.5s) so we don't skip
    // nearby waypoints before the player has started moving.
    float steerX = tx, steerY = tz;  // default: straight to target
    // v0.17.6.1: [drive-vec] pipeline tracking. Records what each steering
    // stage produced so the per-tick diagnostic log can show WHICH stage
    // changed the heading. Initialized to the default (straight to target)
    // and updated as the waypoint, corridor, and trigger-line stages run.
    float vecWpRawX = tx, vecWpRawY = tz;  // chosen waypoint (or final target if none)
    bool  vecCorridorOverrode = false;     // true if corridor steering rewrote steerX/Y
    bool  vecTrigRedirected   = false;     // true if trigger-line proximity check rewrote dx/dz
    if (s_waypointCount > 0 && s_waypointIdx < s_waypointCount) {
        // v05.66: Only chain-advance after the player has had time to move.
        // On the first few ticks, nearby waypoints shouldn't be skipped
        // because they represent the initial steering direction.
        if (s_driveTotalTicks >= 30) {
            // Chain-advance: skip past all waypoints we're already close to.
            float wpArriveDist = s_usingFunnel ? FUNNEL_ARRIVE_DIST : WAYPOINT_ARRIVE_DIST;
            int prevWpIdx = s_waypointIdx;
            while (s_waypointIdx < s_waypointCount - 1) {
                float wpDx = s_waypoints[s_waypointIdx][0] - px;
                float wpDy = s_waypoints[s_waypointIdx][1] - pz;
                float wpDist = sqrtf(wpDx*wpDx + wpDy*wpDy);
                if (wpDist >= wpArriveDist) {
                    // v06.08: Overshoot detection — if we got close and are now
                    // moving away, advance even though we didn't hit the exact threshold.
                    // This catches the corridor oscillation where the player passes
                    // through the waypoint zone at dist~192 but FUNNEL_ARRIVE_DIST=60
                    // never triggers.
                    if (s_usingFunnel && s_wpMinDist < WP_OVERSHOOT_CLOSE &&
                        wpDist > s_wpMinDist * WP_OVERSHOOT_RATIO + WP_OVERSHOOT_MARGIN) {
                        Log::Field("FieldNavigation: [drive] wp %d/%d overshoot (dist=%.0f, minDist=%.0f), advancing",
                                   s_waypointIdx, s_waypointCount, wpDist, s_wpMinDist);
                        NavLog::DriveWaypoint(s_waypointIdx, s_waypointCount, px, pz, dist, s_driveTotalTicks);
                        s_wpMinDist = 1e30f;  // reset for next wp
                        s_waypointIdx++;
                        continue;  // check the next waypoint too
                    }
                    // Update min distance tracker.
                    if (wpDist < s_wpMinDist) s_wpMinDist = wpDist;
                    break;  // not close enough yet and no overshoot
                }
                Log::Field("FieldNavigation: [drive] wp %d/%d reached (dist=%.0f), advancing",
                           s_waypointIdx, s_waypointCount, wpDist);
                NavLog::DriveWaypoint(s_waypointIdx, s_waypointCount, px, pz, dist, s_driveTotalTicks);
                s_wpMinDist = 1e30f;  // reset for next wp
                s_waypointIdx++;
            }
            // Reset min dist tracker and recovery phase if we changed waypoints (progress made).
            // v06.11: Don't reset recovery phase when a recovery re-path places wp0/wp1
            // near the player and they're instantly "reached." Only count as progress if
            // we advance to wpIdx >= 3 during recovery (past the trivial near-player wps),
            // OR if we were already past wp 2 before recovery started (prevWpIdx >= 3).
            if (s_waypointIdx != prevWpIdx) {
                s_wpMinDist = 1e30f;
                bool genuineProgress = (s_driveWigglePhase == 0) ||
                                       (s_waypointIdx >= 3) ||
                                       (prevWpIdx >= 3);
                if (genuineProgress) {
                    s_driveWigglePhase = 0;  // v06.08: genuine progress resets recovery counter
                    s_driveNoProgressCount = 0;  // v06.10: waypoint progress resets no-progress counter
                    s_driveProgressDist = dist;  // v06.10: re-baseline from new position
                }
            }
        }
        steerX = s_waypoints[s_waypointIdx][0];
        steerY = s_waypoints[s_waypointIdx][1];
        vecWpRawX = steerX;  // v0.17.6.1: capture pre-corridor waypoint for [drive-vec]
        vecWpRawY = steerY;

        // v0.17.8.19.4: Chase-drive gateway pass-through. Override final-wp
        // steerX/Y to 300 units past the gateway so the party walks THROUGH
        // instead of oscillating at it (chain-advance can't pass the last wp;
        // v0.15.9.2.15 tx/tz offset gets overwritten here). See CHANGELOG.
        if (s_chaseDriveActive && s_driveCrossLineActive &&
            s_waypointIdx == s_waypointCount - 1) {
            float toGwX = (float)s_chaseDriveTargetX - px;
            float toGwY = (float)s_chaseDriveTargetY - pz;
            float toGwLen = sqrtf(toGwX*toGwX + toGwY*toGwY);
            if (toGwLen > 1.0f) {
                steerX = (float)s_chaseDriveTargetX + (toGwX / toGwLen) * 300.0f;
                steerY = (float)s_chaseDriveTargetY + (toGwY / toGwLen) * 300.0f;
                Log::Field("FieldNavigation: [drive] chase-drive gateway pass-through: "
                           "player=(%.0f,%.0f) gw=(%d,%d) toGwLen=%.0f -> steer=(%.0f,%.0f)",
                           px, pz, (int)s_chaseDriveTargetX, (int)s_chaseDriveTargetY,
                           toGwLen, steerX, steerY);
                // vecWpRawX/Y left as raw funnel wp for [drive-vec] aim-vs-override.
            }
        }
    }
    // v06.17: Corridor-level steering — steer toward the shared-edge midpoint
    // of the next corridor triangle instead of distant funnel waypoints.
    // This gives very local targets that are always close, preventing overshoot.
    // The corridor from A* tells us which triangle sequence leads to the goal.
    // Each tick, we find the player's current triangle in the corridor and target
    // the midpoint of the shared edge to the next corridor triangle.
    //
    // v0.15.9.2.3: SKIPPED for chase-drive. v0.15.9.2.2 BAT (party frozen at
    // (-989,3195) on domt5_1 for 60+ seconds) revealed that corridor steering
    // depends on the engine's reported triangle ID (read from entity +0x1FA),
    // which can be STALE -- particularly when the player has been frozen, the
    // engine never reclassifies. Player was geometrically on tri 13 but engine
    // kept reporting tri 51 (the previous triangle). Corridor steering found
    // tri 51 at corridor[0], targeted the 51-13 portal midpoint at (-1135,3293)
    // which is NORTHWEST of player. With v0.15.9.2.2 Fix A, keyboard reflects
    // long-range target SE while analog reflects backward corridor NW --
    // PERFECT 2D opposition, total freeze. For chase-drive, use the funnel
    // waypoint instead (computed at A* time from portal positions, independent
    // of per-tick tri ID reads). Waypoints can be slightly less precise at
    // edge crossings but don't suffer from stale tri ID feedback loops.
    //
    // v0.17.6.2: DISABLED for F9 auto-drive as well (via `if (false && ...)`).
    // The v0.17.6.1 BAT [drive-vec] log on bghall_1 Save Point exposed a hard
    // failure mode: the handlekeys drive-start "pre-skip" block bumps
    // s_waypointIdx past any wp closer than PRE_SKIP_DIST (120) so the drive
    // targets the next meaningful waypoint instead of a trivial near-player
    // one. Corridor steering then re-introduces the SAME point that pre-skip
    // just discarded -- the corridor edge midpoint between the player's
    // current triangle and the next is, by construction, the same location
    // the funnel emitted as that near-player wp. The result is that the
    // analog flips from "steer toward the real target wp 1" (lX=-332 lY=943,
    // kb=DL, south-west toward Save Point) to "steer toward the corridor
    // edge" (lX=-999 lY=-44, kb=L, pure west into a wall), the player
    // wedges against geometry and moveDist=0 for hundreds of ticks. Manual
    // nav uses the same funnel waypoints WITHOUT corridor steering and has
    // been BAT-proven across bghall_1, bghall_4, bg2f_1, bg2f_2, bgroom_1
    // since v0.17.5 -- FF8's built-in wall-sliding handles narrow corridor
    // turns naturally when the analog points at a far-enough waypoint to
    // produce a non-trivial diagonal heading. F9 inherits that correctness
    // by removing the override and trusting the funnel output, matching the
    // v0.17.6.x design theme (re-base F9 auto-drive on manual nav primitives).
    //
    // The block is preserved with the existing v06.17/v0.15.9.2.3 rationale
    // because the geometry, shrink-by-agent-radius, and trigger-line edge-
    // crossing avoidance might still be useful for elongated-corridor maze
    // fields (Fire Cavern etc.) if v0.17.6.2 BAT regresses on those. To
    // re-enable, flip `false &&` to `true &&` -- and consider gating on
    // `currentWpDist > 200.0f` so the override only fires when the current
    // waypoint is far enough that an intermediate edge midpoint adds value.
    if (false && s_walkmesh.valid && s_corridorCount >= 2 && s_driveTotalTicks >= 30 && !s_chaseDriveActive) {
        uint16_t nowTri = 0xFFFF;
        {
            uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base2)
                nowTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
        }
        if (nowTri != 0xFFFF && nowTri < (uint16_t)s_walkmesh.numTriangles) {
            // Find player's position in the corridor.
            int corridorPos = -1;
            for (int ci = 0; ci < s_corridorCount; ci++) {
                if (s_corridor[ci] == nowTri) { corridorPos = ci; break; }
            }
            if (corridorPos >= 0 && corridorPos + 1 < s_corridorCount) {
                // Target = midpoint of shared edge to next corridor triangle.
                uint16_t nextTri = s_corridor[corridorPos + 1];
                const auto& tCur = s_walkmesh.triangles[nowTri];
                int sharedEdge = -1;
                for (int e = 0; e < 3; e++) {
                    if (tCur.neighbor[e] == nextTri) { sharedEdge = e; break; }
                }
                if (sharedEdge >= 0) {
                    // v0.18.3.308 (#113): corrected to the v0.17.9.14 edge convention
                    // (neighbor[e] <-> vertices e, e+1) while this block was DISABLED,
                    // so re-enabling it doesn't resurrect the wrong-edge bug.
                    int vi1 = tCur.vertexIdx[sharedEdge];
                    int vi2 = tCur.vertexIdx[(sharedEdge + 1) % 3];
                    if (vi1 < s_walkmesh.numVertices && vi2 < s_walkmesh.numVertices) {
                        float emx = ((float)s_walkmesh.vertices[vi1].x + (float)s_walkmesh.vertices[vi2].x) / 2.0f;
                        float emy = ((float)s_walkmesh.vertices[vi1].y + (float)s_walkmesh.vertices[vi2].y) / 2.0f;
                        // Shrink toward next triangle center by agent radius.
                        float toCX = s_walkmesh.triangles[nextTri].centerX - emx;
                        float toCY = s_walkmesh.triangles[nextTri].centerY - emy;
                        float toCLen = sqrtf(toCX*toCX + toCY*toCY);
                        if (toCLen > 0.001f) {
                            emx += (toCX / toCLen) * 30.0f;
                            emy += (toCY / toCLen) * 30.0f;
                        }
                        // v06.22: Don't use corridor steering if the edge midpoint
                        // is across a non-target trigger line from the player.
                        // This prevents the corridor from routing through trigger zones.
                        bool edgeCrossesTrig = false;
                        if (s_capturedLineCount > 0) {
                            edgeCrossesTrig = IsSeparatedByTriggerLine(px, pz, emx, emy, s_driveSkipTrigIdx);
                        }
                        if (!edgeCrossesTrig) {
                            steerX = emx;
                            steerY = emy;
                            vecCorridorOverrode = true;  // v0.17.6.1: flag for [drive-vec]
                        }
                        // else: keep the funnel waypoint as steer target
                    }
                }
            } else if (corridorPos < 0) {
                // Player left the corridor — re-path needed (recovery will handle).
            }
        }
    }

    // v0.18.3.321 (#114): seg-aim is the FINAL-approach push, so engage it ONLY on the
    // last waypoint. The .320 dot-gate failed at the stairs (funnel + seg-aim both point
    // north, dot>0) so seg-aim yanked the player EAST off the westward A* detour into the
    // tri-6 pin -- but the .320 BAT proved the descent fires WEST at tri 3, mid-path.
    if (useSegAim) {
        bool onLastWp = (s_waypointCount == 0) || (s_waypointIdx >= s_waypointCount - 1);
        if (onLastWp) {
            steerX = segAimX; steerY = segAimY;
            vecTrigRedirected = true;
        } else {
            useSegAim = false;  // mid-detour: follow the path (trigRedir stays 0)
        }
    }
    // Recompute dx/dz toward the steer target.
    dx = steerX - px;
    dz = steerY - pz;

    // v06.17: Wall-avoidance steering bias. DISABLED in v06.20 (pushed players
    // OUT of narrow corridors; corridor steering + recovery handles it better).
    // Retained for potential re-enabling with better narrow-space logic.
    if (false && s_walkmesh.valid) {
        uint16_t nowTri2 = 0xFFFF;
        {
            uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base2)
                nowTri2 = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
        }
        if (nowTri2 != 0xFFFF && nowTri2 < (uint16_t)s_walkmesh.numTriangles) {
            const auto& tri = s_walkmesh.triangles[nowTri2];
            static const float WALL_BIAS_DIST = 40.0f;   // activate when within this distance
            static const float WALL_BIAS_STRENGTH = 0.25f; // blend factor (0=no bias, 1=full perpendicular)
            // v06.19: Check if corridor is narrow (walls on multiple edges).
            // If so, reduce bias to avoid ping-ponging between walls.
            int wallEdgeCount = 0;
            for (int ec = 0; ec < 3; ec++)
                if (tri.neighbor[ec] == 0xFFFF) wallEdgeCount++;
            float effectiveStrength = WALL_BIAS_STRENGTH;
            if (wallEdgeCount >= 2) effectiveStrength *= 0.3f; // very narrow, minimal bias
            for (int e = 0; e < 3; e++) {
                if (tri.neighbor[e] != 0xFFFF) continue; // not a wall edge
                // Wall edge e spans vertices e and (e+1)%3.
                // v0.18.3.308 (#113): corrected to the v0.17.9.14 convention while
                // this block was DISABLED, so re-enabling doesn't resurrect the bug.
                int wvi1 = tri.vertexIdx[e];
                int wvi2 = tri.vertexIdx[(e + 1) % 3];
                if (wvi1 >= s_walkmesh.numVertices || wvi2 >= s_walkmesh.numVertices) continue;
                float wx1 = (float)s_walkmesh.vertices[wvi1].x;
                float wy1 = (float)s_walkmesh.vertices[wvi1].y;
                float wx2 = (float)s_walkmesh.vertices[wvi2].x;
                float wy2 = (float)s_walkmesh.vertices[wvi2].y;
                // Distance from player to this edge (point-to-line-segment).
                float edx = wx2 - wx1, edy = wy2 - wy1;
                float edLenSq = edx*edx + edy*edy;
                if (edLenSq < 1.0f) continue;
                float t = ((px - wx1)*edx + (pz - wy1)*edy) / edLenSq;
                if (t < 0) t = 0; if (t > 1) t = 1;
                float closestX = wx1 + t * edx;
                float closestY = wy1 + t * edy;
                float wallDx = px - closestX;
                float wallDy = pz - closestY;
                float wallDist = sqrtf(wallDx*wallDx + wallDy*wallDy);
                if (wallDist < WALL_BIAS_DIST && wallDist > 0.1f) {
                    // Blend steering away from wall. Stronger when closer.
                    float factor = effectiveStrength * (1.0f - wallDist / WALL_BIAS_DIST);
                    float awayX = wallDx / wallDist; // unit vector away from wall
                    float awayY = wallDy / wallDist;
                    float steerMag = sqrtf(dx*dx + dz*dz);
                    dx = dx * (1.0f - factor) + awayX * factor * steerMag;
                    dz = dz * (1.0f - factor) + awayY * factor * steerMag;
                }
            }
        }
    }

    // v06.17: Trigger-line proximity check. If the current heading would carry
    // the player across a NON-target trigger line within ~200u, redirect steering
    // parallel to it. Skips the target line (s_driveSkipTrigIdx) and NPC targets
    // the A* path legitimately routes across.
    if (s_capturedLineCount > 0) {
        float steerLen = sqrtf(dx*dx + dz*dz);
        if (steerLen > 1.0f) {
            float projDist = 200.0f;
            float projX = px + (dx / steerLen) * projDist;
            float projY = pz + (dz / steerLen) * projDist;
            for (int t = 0; t < s_capturedLineCount; t++) {
                if (!s_capturedLines[t].active) continue;
                if (t == s_driveSkipTrigIdx) continue;
                float lx1 = (float)s_capturedLines[t].x1;
                float ly1 = (float)s_capturedLines[t].y1;
                float lx2 = (float)s_capturedLines[t].x2;
                float ly2 = (float)s_capturedLines[t].y2;
                float ldx = lx2 - lx1, ldy = ly2 - ly1;
                // v06.18: Skip trigger lines where the target is on the other side.
                // If the target is across this trigger line from the player, A*
                // already planned to cross it, so we must allow the crossing.
                float crossPlayer = ldx * (pz - ly1) - ldy * (px - lx1);
                float crossTarget = ldx * (tz - ly1) - ldy * (tx - lx1);
                if (crossPlayer * crossTarget < -1.0f) continue; // target is across, allow crossing
                float crossProj = ldx * (projY - ly1) - ldy * (projX - lx1);
                if (crossPlayer * crossProj < -1.0f) {
                    // Projected endpoint crosses trigger line — redirect parallel.
                    float trigLen = sqrtf(ldx*ldx + ldy*ldy);
                    if (trigLen > 0.001f) {
                        float trigNx = ldx / trigLen, trigNy = ldy / trigLen;
                        float dot = dx * trigNx + dz * trigNy;
                        dx = trigNx * dot;
                        dz = trigNy * dot;
                        vecTrigRedirected = true;  // v0.17.6.1: flag for [drive-vec]
                    }
                    break;
                }
            }
        }
    }

    // v05.62: Max drive time safety cutoff.
    // v0.15.9.2.2: chase-drive uses an extended timeout because chase
    // corridors are long (domt5_1 is ~3300 world units start-to-finish) and
    // walking pace stays well below F9's run-to-NPC pace. 12000 ticks = 200 s
    // gives enough slack to traverse multi-screen corridors without the
    // outer chase auto-pilot loop having to disengage and re-engage.
    s_driveTotalTicks++;
    int driveMaxTicks = s_chaseDriveActive ? 12000 : DRIVE_MAX_TICKS;
    if (s_driveTotalTicks >= driveMaxTicks) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Gave up. Distance remaining: %.0f.", dist);
        StopAutoDrive(msg);
        return;
    }

    // v05.65: Periodic position log every ~2s with waypoint progress.
    // v05.86: Also log analog lX/lY and movement delta to verify analog steering.
    s_driveLogTimer++;
    if (s_driveLogTimer >= 120) {
        s_driveLogTimer = 0;
        // Compute actual movement delta from last logged position.
        static float s_lastLogPX = 0, s_lastLogPZ = 0;
        float moveDx = px - s_lastLogPX;
        float moveDz = pz - s_lastLogPZ;
        float moveDist = sqrtf(moveDx*moveDx + moveDz*moveDz);
        // Compute angle between analog vector and actual movement.
        // If analog is working, these should be similar.
        float analogAngle = atan2f((float)s_analogDesiredLX, (float)-s_analogDesiredLY) * (180.0f / (float)NAV_PI);
        float moveAngle = (moveDist > 5.0f) ? atan2f(moveDx, -moveDz) * (180.0f / (float)NAV_PI) : 0.0f;
        Log::Field("FieldNavigation: [drive] tick=%d dist=%.0f player=(%.0f,%.0f) "
                   "steer=(%.0f,%.0f) wp=%d/%d kb=%s%s%s%s "
                   "lX=%d lY=%d analogAng=%.0f moveAng=%.0f moveDist=%.0f",
                   s_driveTotalTicks, dist, px, pz,
                   steerX, steerY, s_waypointIdx, s_waypointCount,
                   (s_driveHeld & DIR_UP) ? "U" : "", (s_driveHeld & DIR_DOWN) ? "D" : "",
                   (s_driveHeld & DIR_LEFT) ? "L" : "", (s_driveHeld & DIR_RIGHT) ? "R" : "",
                   (int)s_analogDesiredLX, (int)s_analogDesiredLY,
                   analogAngle, moveAngle, moveDist);
        s_lastLogPX = px;
        s_lastLogPZ = pz;

        // v06.08: NavLog periodic sample
        {
            uint16_t sampTri = 0xFFFF;
            uint8_t* base3 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base3)
                sampTri = *(uint16_t*)(base3 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            NavLog::DriveSample(px, pz, (int)sampTri, dist, s_waypointIdx, s_waypointCount, s_driveTotalTicks);
        }
    }

    // v05.90: Velocity-based stuck detection.
    // Every DRIVE_STUCK_THRESH ticks, check if the player has moved at least
    // DRIVE_STUCK_MIN_DIST world units from the position recorded at the
    // start of the window. This catches both triId-stuck (player on one
    // triangle) and oscillation (bouncing between two triangles without
    // making progress). Much more responsive than the old triId-only check.
    s_driveStuckTicks++;
    // v06.08: Grace period — don't check for stuck until the player has had
    // time to start moving. The game needs ~60 ticks to engage movement after
    // we install the fake gamepad and start injecting keys.
    if (s_driveStuckTicks >= DRIVE_STUCK_THRESH && s_driveTotalTicks >= 60) {
        float sdx = px - s_driveStuckPosX;
        float sdy = pz - s_driveStuckPosY;
        float stuckDist = sqrtf(sdx*sdx + sdy*sdy);
        if (stuckDist < DRIVE_STUCK_MIN_DIST) {
            // Player hasn't moved enough — trigger recovery.
            // (stuckTicks stays >= thresh so the recovery block below fires)
            //
            // v0.15.9.2.9: For chase-drive, the regular recovery branch is
            // gated off (v0.15.9.2.2 Fix B), and v0.15.9.2.5's chase-drive
            // advance-on-stuck only fires in the no-progress branch BELOW --
            // which requires the player to actually be MOVING (stuckDist >=
            // MIN). v0.15.9.2.8 BAT confirmed wp-13 on domt2_1 hangs in this
            // gap: player at (683,8) with moveDist=0 for 27+ seconds, no
            // logs, no advance, no recovery. Velocity-stuck on chase-drive
            // was completely unhandled.
            //
            // Fix: when stuckDist < MIN AND chase-drive is active AND we have
            // waypoints remaining, advance the funnel waypoint. Same logic as
            // v0.15.9.2.5's no-progress advance, applied to the parallel
            // velocity-stuck case (player completely stationary instead of
            // oscillating). Reset stuckTicks so the next advance waits another
            // full window (~1.3s at 60Hz), preventing chain-skip through all
            // remaining waypoints in one tick.
            if (s_chaseDriveActive && s_waypointIdx < s_waypointCount - 1) {
                Log::Field("FieldNavigation: [drive] chase-drive: velocity-stuck "
                           "(stuckDist=%.0f < %d), skipping wp %d/%d, "
                           "advancing to wp %d/%d",
                           stuckDist, (int)DRIVE_STUCK_MIN_DIST,
                           s_waypointIdx, s_waypointCount,
                           s_waypointIdx + 1, s_waypointCount);
                s_waypointIdx++;
                s_wpMinDist = 1e30f;
                s_driveStuckTicks = 0;  // wait another full window before next advance
            }
        } else {
            // v06.10: Player moved, but check if they're making progress
            // toward the target. Micro-oscillation (e.g. bggate_2 tri 126<->127)
            // produces enough movement to pass the velocity check but zero
            // progress toward the target.
            if (s_driveProgressDist > 1e29f) {
                // First window — seed the progress baseline.
                s_driveProgressDist = dist;
                s_driveNoProgressCount = 0;
            } else {
                float closed = s_driveProgressDist - dist;  // positive = closer
                if (closed < DRIVE_PROGRESS_MIN) {
                    s_driveNoProgressCount++;
                    if (s_driveNoProgressCount >= DRIVE_NO_PROGRESS_MAX) {
                        // Not making progress toward target — force stuck recovery.
                        Log::Field("FieldNavigation: [drive] no-progress stuck: "
                                   "dist=%.0f progressBaseline=%.0f closed=%.0f "
                                   "noProgressCount=%d — forcing recovery",
                                   dist, s_driveProgressDist, closed,
                                   s_driveNoProgressCount);
                        // v0.15.9.2.5: For chase-drive, the regular recovery branch
                        // is gated off (v0.15.9.2.2 Fix B) because its perp-nudge
                        // logic doesn't work on rotated cameras. But no-progress IS
                        // a real signal that we're stuck on the current funnel
                        // waypoint -- typically because the camera projection makes
                        // it unreachable (e.g., on domt5_1 wp 7 sits east of the
                        // player but camRight is mostly +Y world, so the analog
                        // can barely move the player east; player oscillates around
                        // the wp's Y-line at dist ~100 forever). Advance the funnel
                        // waypoint instead: we've gotten as close as the camera will
                        // allow, move on. The next waypoint may be camera-reachable.
                        // If no more waypoints, fall through and let the main drive
                        // timeout handle it.
                        if (s_chaseDriveActive && s_waypointIdx < s_waypointCount - 1) {
                            Log::Field("FieldNavigation: [drive] chase-drive: "
                                       "skipping wp %d/%d (camera-unreachable?), "
                                       "advancing to wp %d/%d",
                                       s_waypointIdx, s_waypointCount,
                                       s_waypointIdx + 1, s_waypointCount);
                            s_waypointIdx++;
                            s_wpMinDist = 1e30f;  // reset for new wp
                        }
                        // Leave stuckTicks >= thresh so recovery block fires.
                        s_driveProgressDist = dist;
                        s_driveNoProgressCount = 0;
                        // Don't reset stuckTicks — fall through to recovery.
                        s_driveStuckPosX = px;
                        s_driveStuckPosY = pz;
                        goto stuck_check_done;  // skip the normal reset
                    }
                } else {
                    // Genuine progress — reset the no-progress counter.
                    s_driveNoProgressCount = 0;
                }
                s_driveProgressDist = dist;
            }
            // Player is making progress — reset the window.
            s_driveStuckTicks = 0;
            s_driveWiggleTicks = 0;
        }
        // Always update the reference position at window boundary.
        s_driveStuckPosX = px;
        s_driveStuckPosY = pz;
    }
    stuck_check_done:

    // v05.83: Activate analog override and set direction from the computed vector.
    // This gives us true 360-degree steering via the gamepad analog path.
    // The keyboard injection (SetHeldDirections) is kept as a fallback
    // in case the analog path isn't read by the game engine.
    //
    // v0.15.9.2.4: Moved BEFORE heading bitmask computation. The chase-drive
    // heading is now derived from the analog values (set just below), not from
    // the long-range target direction. See heading-bitmask comment.
    s_analogOverrideActive = true;
    SetAnalogFromVector(dx, dz);

    // v05.75: Heading computation. Map world-space delta to arrow keys.
    // Log analysis confirms: pressing UP moves player in +Y world direction.
    // For X axis: pressing RIGHT moves player in +X world direction (v05.74
    // confirmed back-to-front auto-drive worked with direct X mapping).
    // Y axis is inverted (UP=+Y but -Y=screen-up), X axis is NOT inverted.
    //
    // v0.15.9.2.2: Used origDx/origDz (long-range target direction) to fix
    // the heading-fallback bug. F9 path-finding still uses this branch.
    //
    // v0.15.9.2.4: For chase-drive, DERIVE THE HEADING FROM THE ANALOG VALUES
    // (s_analogDesiredLX/LY, set just above by SetAnalogFromVector). The
    // keyboard's job is to wake up FF8's movement code; it doesn't direct
    // movement, the analog does. When keyboard and analog conflict on ANY
    // axis, FF8 reads inconsistent input and movement stalls or freezes.
    // v0.15.9.2.3 BAT confirmed this: steer was correct (south-west toward
    // funnel waypoint), analog correct (lX=-886 lY=852 = SW), but kb=DR
    // (south-east per origDx/origDz from long-range SE target) conflicted on
    // the X axis (E vs W) and the party stayed frozen for 60+ seconds. Y
    // axis agreement wasn't enough; ANY axis-level conflict locks movement.
    // Fix: in chase-drive, heading reflects the screen direction the analog
    // is currently requesting (screen-relative, lX > thresh → RIGHT, etc.).
    // F9 path-finding keeps the v0.15.9.2.2 origDx/origDz behavior unchanged.
    //
    // v0.17.6.0: F9 now uses the same analog-derived heading logic as chase-drive.
    // Reason: F9's analog values now flow through the .ca-quantized camera
    // projection (s_camRight/Down), so on rotated-camera fields the analog
    // direction is screen-relative while origDx/origDz remained world-relative.
    // On a 90-degree rotated field (camRight=(0,1), camDown=(-1,0)), a world
    // +Y target produces lX=+positive (screen right) but origDz>0 triggers
    // DIR_UP -- the same kb-vs-analog axis conflict the chase doc warned
    // about. Sharing the analog-derived heading logic guarantees keyboard and
    // analog agree on every field regardless of camera orientation. The
    // chase doc Finding from v0.15.9.2.3 ("If F9 ever hits the same freeze
    // pattern, replace this branch with the chase-drive logic above") was
    // explicit foreshadowing; v0.17.6.0 acts on it.
    uint8_t heading = 0;
    {
        // Derive heading from analog values (screen-relative).
        // DirectInput convention: lX +1000 = screen right, lY +1000 = screen down.
        // Arrow keys are screen-relative: DIR_RIGHT = screen right, DIR_DOWN = screen down.
        const int kAnalogHeadingThresh = 100;
        int lx = s_analogDesiredLX;
        int ly = s_analogDesiredLY;
        if (lx >  kAnalogHeadingThresh) heading |= DIR_RIGHT;
        if (lx < -kAnalogHeadingThresh) heading |= DIR_LEFT;
        if (ly >  kAnalogHeadingThresh) heading |= DIR_DOWN;
        if (ly < -kAnalogHeadingThresh) heading |= DIR_UP;
        if (heading == 0) {
            // Analog is in deadzone. Pick the dominant axis so we still
            // wake up FF8's movement code with something coherent.
            int absLx = (lx < 0) ? -lx : lx;
            int absLy = (ly < 0) ? -ly : ly;
            if (absLx >= absLy && absLx > 0)      heading = (lx >= 0) ? DIR_RIGHT : DIR_LEFT;
            else if (absLy > 0)                   heading = (ly >= 0) ? DIR_DOWN : DIR_UP;
            else                                  heading = DIR_UP;  // pure deadzone
        }
    }

    // v0.17.6.1: [drive-vec] per-tick steering pipeline diagnostic.
    // Logs the intermediate values at each stage of the steering pipeline so
    // we can see WHICH STAGE produced the wrong direction when the drive gets
    // stuck. Fires every DRIVE_VEC_LOG_INTERVAL ticks (~0.5 s at 60 Hz) -- the
    // existing 120-tick [drive] tick log was too sparse to catch transient
    // steering inversions (e.g. the v0.17.6.0 BAT showed lX=-840/lY=-542 for
    // multiple consecutive log windows, but the per-tick values likely jumped
    // around between recovery cycles). Format:
    //   t  = total ticks since drive start
    //   tri = engine-reported walkmesh triangle (read from entity +0x1FA)
    //   pp = player world position
    //   wpRaw = chosen funnel waypoint or final target (pre-corridor override)
    //   corOverride/corSteer = corridor steering wrote new steer to this edge midpoint
    //   trigRedir/finalDelta = trigger-line proximity rewrote dx/dz parallel
    //   lX/lY = analog values written by SetAnalogFromVector (camera-projected)
    //   kb = heading bitmask derived from analog (post v0.17.6.0 unified logic)
    //   wig/phase = wiggle tick counter / recovery phase counter
    // To disable, raise DRIVE_VEC_LOG_INTERVAL; the per-tick cost otherwise
    // is one mod-by-constant and an int compare.
    static const int DRIVE_VEC_LOG_INTERVAL = 30;
    if ((s_driveTotalTicks % DRIVE_VEC_LOG_INTERVAL) == 0) {
        uint16_t vecTri = 0xFFFF;
        {
            uint8_t* baseVec = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (baseVec)
                vecTri = *(uint16_t*)(baseVec + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
        }
        Log::Field("FieldNavigation: [drive-vec] t=%d tri=%u pp=(%.0f,%.0f) "
                   "wpRaw=(%.0f,%.0f) corOverride=%d corSteer=(%.0f,%.0f) "
                   "trigRedir=%d finalDelta=(%.1f,%.1f) "
                   "lX=%d lY=%d kb=%s%s%s%s wig=%d phase=%d",
                   s_driveTotalTicks, (unsigned)vecTri, px, pz,
                   vecWpRawX, vecWpRawY, (int)vecCorridorOverrode, steerX, steerY,
                   (int)vecTrigRedirected, dx, dz,
                   (int)s_analogDesiredLX, (int)s_analogDesiredLY,
                   (heading & DIR_UP) ? "U" : "", (heading & DIR_DOWN) ? "D" : "",
                   (heading & DIR_LEFT) ? "L" : "", (heading & DIR_RIGHT) ? "R" : "",
                   s_driveWiggleTicks, s_driveWigglePhase);
    }

    if (s_driveWiggleTicks > 0) {
        // v05.68/85: Wall recovery using analog steering.
        // Convert the 8-dir bitmask to a vector for analog injection.
        float wdx = 0, wdy = 0;
        if (s_driveWiggleDir & DIR_RIGHT) wdx += 1.0f;
        if (s_driveWiggleDir & DIR_LEFT)  wdx -= 1.0f;
        if (s_driveWiggleDir & DIR_UP)    wdy += 1.0f;
        if (s_driveWiggleDir & DIR_DOWN)  wdy -= 1.0f;
        // v06.05: Abort wiggle if the direction would now cross a trigger line
        // (player may have drifted during the wiggle).
        if (s_capturedLineCount > 0 &&
            WouldCrossTriggerLine(px, pz, wdx * 1000.0f, wdy * 1000.0f, s_driveSkipTrigIdx)) {
            s_driveWiggleTicks = 0;  // abort this wiggle
            Log::Field("FieldNavigation: [drive] wiggle aborted — would cross trigger line");
        } else {
            // Scale to a large magnitude so SetAnalogFromVector normalizes it.
            SetAnalogFromVector(wdx * 1000.0f, wdy * 1000.0f);
            SetHeldDirections(s_driveWiggleDir);  // kept for bitmask tracking/logging only
            s_driveWiggleTicks--;
        }
        // v05.80: When recovery finishes and we moved to a new triangle,
        // recompute A* from current position. This prevents the cascading
        // failure where recovery flings the player far away and the old
        // waypoints become unreachable.
        if (s_driveWiggleTicks == 0 && s_walkmesh.valid && s_waypointCount > 0) {
            uint16_t nowTri = 0xFFFF;
            {
                uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                if (base2)
                    nowTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            }
            if (nowTri != 0xFFFF && nowTri < (uint16_t)s_walkmesh.numTriangles) {
                // Find goal triangle from target position
                // v0.15.9.2.1: Chase-drive uses stored target coords.
                float rpTx = 0, rpTz = 0;
                bool rpGot = false;
                if (s_chaseDriveActive) {
                    rpTx = (float)s_chaseDriveTargetX;
                    rpTz = (float)s_chaseDriveTargetY;
                    rpGot = true;
                } else if (ei <= -400) {
                    int gwIdx = -(ei + 400);
                    if (gwIdx >= 0 && gwIdx < s_dedupGatewayCount) {
                        rpTx = s_dedupGateways[gwIdx].centerX;
                        rpTz = s_dedupGateways[gwIdx].centerY;
                        rpGot = true;
                    }
                } else if (ei <= -300) {
                    int jsmIdx = -(ei + 300);
                    if (jsmIdx >= 0 && jsmIdx < s_jsmEntityCount && s_jsmEntities[jsmIdx].hasPosition) {
                        rpTx = (float)s_jsmEntities[jsmIdx].posX;
                        rpTz = (float)s_jsmEntities[jsmIdx].posY;
                        rpGot = true;
                    }
                } else if (ei <= -200) {
                    int trigIdx2 = -(ei + 200);
                    if (trigIdx2 >= 0 && trigIdx2 < s_capturedLineCount) {
                        rpTx = (float)(s_capturedLines[trigIdx2].x1 + s_capturedLines[trigIdx2].x2) / 2.0f;
                        rpTz = (float)(s_capturedLines[trigIdx2].y1 + s_capturedLines[trigIdx2].y2) / 2.0f;
                        rpGot = true;
                    }
                } else if (ei >= 0) {
                    rpGot = GetEntityPos(ei, rpTx, rpTz);
                }
                if (rpGot) {
                    int rpGoal = FindNearestTriangle(rpTx, rpTz);
                    if (rpGoal >= 0 && (int)nowTri != rpGoal) {
                        int oldWp = s_waypointIdx;
                        int oldTotal = s_waypointCount;
                        // v06.02: Exempt target trigger line from A* avoidance during re-path.
                        // v06.04: Also exempt for event triggers (not just exits).
                        // v0.07.94: Only for trigger-line entities (-200 to -299).
                        // v0.15.9.2.1: Use ei (matches catTarget.entityIdx in
                        // the F9 path, -1 sentinel in chase-drive). Original
                        // v06.02/v06.04 used catTarget.entityIdx; replaced for
                        // chase-drive safety -- catTarget can be stale catalog
                        // state when chase-drive owns the drive.
                        int rpSkipTrig = -1;
                        if (ei <= -200 && ei > -300) {
                            rpSkipTrig = -(ei + 200);
                        }
                        // v06.04: Save old waypoints before A* overwrites them.
                        // If re-path fails (player on disconnected island), we
                        // restore the old waypoints so the drive can keep trying.
                        float savedWp[MAX_WAYPOINTS][2];
                        int savedWpCount = s_waypointCount;
                        int savedWpIdx = s_waypointIdx;
                        bool savedFunnel = s_usingFunnel;
                        memcpy(savedWp, s_waypoints, sizeof(float) * 2 * savedWpCount);
                        if (ComputeAStarPath((int)nowTri, rpGoal, ei, rpSkipTrig)) {
                            // v05.94: Funnel re-enabled after FindPortal fix.
                            FunnelPath(px, pz, rpTx, rpTz);
                            s_wpMinDist = 1e30f;  // v06.08
                            Log::Field("FieldNavigation: [drive] re-pathed after recovery: %d wp (was wp %d/%d)",
                                       s_waypointCount, oldWp, oldTotal);
                        } else {
                            // v06.04: Re-path failed — restore old waypoints.
                            memcpy(s_waypoints, savedWp, sizeof(float) * 2 * savedWpCount);
                            s_waypointCount = savedWpCount;
                            s_waypointIdx = savedWpIdx;
                            s_usingFunnel = savedFunnel;
                            Log::Field("FieldNavigation: [drive] re-path FAILED from tri %d — restored old %d wp",
                                       (int)nowTri, savedWpCount);
                        }
                    }
                }
            }
        }
    } else if (s_driveStuckTicks >= DRIVE_STUCK_THRESH && !s_chaseDriveActive) {
        // v06.16: Simplified recovery system.
        // No more odd/even phase alternation. Simple cycle:
        //   Odd phases:  re-run A* from current position → funnel path
        //   Even phases: single perpendicular nudge to break wall contact
        // After nudge completes, the wiggle-completion code above re-paths via funnel.
        //
        // v0.15.9.2.2: Skipped for chase-drive. Recovery's perpendicular-nudge
        // logic picks the perp direction whose dot-product with (next-tri-center
        // − player) is larger. On elongated triangles, the centroid can be on
        // the OPPOSITE side of the player from the shared edge, so the chosen
        // perp pushes AWAY from the edge the corridor wants to cross. On rotated-
        // camera fields like domt5_1 (camRight ≈ (0,1), camDown ≈ (0,-1)), the
        // chosen perp also projects through the camera to nearly-zero analog,
        // producing no useful movement. With the v0.15.9.2.2 heading-bitmask
        // fix above, main steering can hold a coherent direction without
        // recovery thrashing it. Chase corridors are hand-picked; if main
        // steering can't progress, recovery's misdirected nudges won't help
        // either, and the outer chase auto-pilot's per-tick IsChaseDriveActive
        // check provides the only sensible cancel path.
        s_driveStuckTicks = 0;

        // v0.17.6.1: Reset recovery counter when the player's walkmesh triangle
        // has changed since the previous recovery cycle. Each new triangle
        // along the corridor counts as genuine progress and earns a fresh
        // MAX_RECOVERY_PHASES budget. Without this, narrow-corridor traversals
        // burn the global counter across many triangles even when each
        // individual triangle escape works -- the v0.17.6.0 Save Point BAT
        // got 5 corridor advances (tri 367 -> 366 -> 363 -> 362 -> 359) and
        // gave up at recovery 12 in tri 362 because the counter never reset.
        {
            uint16_t curRecoveryTri = 0xFFFF;
            uint8_t* baseRT = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (baseRT)
                curRecoveryTri = *(uint16_t*)(baseRT + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            if (curRecoveryTri != 0xFFFF && s_lastRecoveryTri != 0xFFFF &&
                curRecoveryTri != s_lastRecoveryTri) {
                // v0.18.3.307 (#100): a triangle CHANGE only refills the
                // recovery budget when it is genuinely new territory. The
                // .306 final drive reset the phase on every hop of a
                // tri 8 <-> 9 <-> 10 ping-pong, so MAX_RECOVERY_PHASES was
                // unreachable and "Stuck." could never fire -- exactly the
                // #100 "wedges silently" report. Real corridor progress
                // still resets (a fresh triangle is not in the ring), and
                // waypoint advances reset the phase independently above.
                if (!RecoveryRingContains(curRecoveryTri)) {
                    Log::Field("FieldNavigation: [drive] recovery counter reset: tri %u -> %u "
                               "(player advanced along corridor; phase was %d)",
                               (unsigned)s_lastRecoveryTri, (unsigned)curRecoveryTri,
                               s_driveWigglePhase);
                    s_driveWigglePhase = 0;
                } else {
                    Log::Field("FieldNavigation: [drive] recovery counter NOT reset: tri %u -> %u "
                               "revisits a recent stuck triangle (phase stays %d) [v0.18.3.307 #100]",
                               (unsigned)s_lastRecoveryTri, (unsigned)curRecoveryTri,
                               s_driveWigglePhase);
                }
            }
            if (curRecoveryTri != 0xFFFF)
                RecoveryRingPush(curRecoveryTri);
            s_lastRecoveryTri = curRecoveryTri;
        }

        s_driveWigglePhase++;

        // Auto-cancel after too many recovery phases without progress.
        if (s_driveWigglePhase > MAX_RECOVERY_PHASES) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Stuck. Distance remaining: %.0f.", dist);
            StopAutoDrive(msg);
            return;
        }

        // NavLog recovery event
        {
            uint16_t recTri = 0xFFFF;
            uint8_t* base4 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base4)
                recTri = *(uint16_t*)(base4 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            NavLog::DriveRecovery(s_driveWigglePhase, (int)recTri, px, pz, dist);
        }

        // v06.21: Expand talk radius when stuck near NPC target.
        // If recovery is firing and we're close to the target, expand the
        // game's talk radius so the player can interact from further away.
        // This is the "meet in the middle" strategy.
        if (!s_driveTalkRadExpanded && s_driveTargetEntityIdx >= 0 &&
            s_driveOrigTalkRadius > 0 && dist < TALK_RAD_EXPAND_DIST) {
            float expanded = (float)s_driveOrigTalkRadius * TALK_RAD_EXPAND_FACTOR;
            if (expanded > TALK_RAD_EXPAND_MAX) expanded = TALK_RAD_EXPAND_MAX;
            uint16_t newRad = (uint16_t)expanded;
            if (newRad > s_driveOrigTalkRadius) {
                SetEntityTalkRadius(s_driveTargetEntityIdx, newRad);
                s_driveTalkRadExpanded = true;
                // Also expand our arriveDist to match.
                s_driveArriveDist = expanded;
                Log::Field("FieldNavigation: [drive] expanded talkRadius %u -> %u for ent%d (dist=%.0f)",
                           (unsigned)s_driveOrigTalkRadius, (unsigned)newRad,
                           s_driveTargetEntityIdx, dist);
            }
        }

        if (s_walkmesh.valid) {
            uint16_t nowTri = 0xFFFF;
            {
                uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                if (base2)
                    nowTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            }
            if (nowTri != 0xFFFF && nowTri < (uint16_t)s_walkmesh.numTriangles) {
                // v0.18.3.307 (#112): use the raw (pre-offset) target so every
                // recovery re-path shares the drive's original goal triangle.
                // Was `tx, tz`, which carries the per-tick "+300 past the
                // line" offset -- see the rawTx capture comment above.
                float rpTx = rawTx, rpTz = rawTz;
                int rpGoal = FindNearestTriangle(rpTx, rpTz);

                if ((s_driveWigglePhase % 2) == 0) {
                    // Even phase: perpendicular nudge to break wall contact.
                    // Compute nudge perpendicular to the shared edge between
                    // current triangle and the next corridor triangle.
                    bool nudged = false;
                    if (s_corridorCount >= 2) {
                        int corridorPos = -1;
                        for (int ci = 0; ci < s_corridorCount; ci++) {
                            if (s_corridor[ci] == nowTri) { corridorPos = ci; break; }
                        }
                        int neighborCorridorIdx = -1;
                        if (corridorPos >= 0 && corridorPos + 1 < s_corridorCount)
                            neighborCorridorIdx = corridorPos + 1;
                        else if (corridorPos > 0)
                            neighborCorridorIdx = corridorPos - 1;

                        if (neighborCorridorIdx >= 0) {
                            uint16_t nextTri = s_corridor[neighborCorridorIdx];
                            const auto& tCur = s_walkmesh.triangles[nowTri];
                            int sharedEdge = -1;
                            for (int e = 0; e < 3; e++) {
                                if (tCur.neighbor[e] == nextTri) { sharedEdge = e; break; }
                            }
                            if (sharedEdge >= 0) {
                                // v0.18.3.308 (#113): neighbor[e] is across edge
                                // (vertex[e], vertex[(e+1)%3]) -- the SAME convention
                                // v0.17.9.14 already fixed in FindPortal. This nudge
                                // still used the old (e+1, e+2) pair, so its
                                // "perpendicular to the shared edge" was perpendicular
                                // to the WRONG SEGMENT. Proof from the .306 BAT log:
                                // the wedged drive's 20 identical nudges logged
                                // dir=(0.90,-0.44), which is exactly the old-rule
                                // output for tri 72->71 on gpbig1a; the true shared
                                // edge (-1433,-1590)-(-781,-1402) gives (-0.28,0.96)
                                // -- pointing at tri 71 instead of along the wall.
                                // Verified against the extract: the old pair mismatches
                                // the neighbor's vertex set on 184/186 links across
                                // gpbig1a/gpbig2a; the fixed pair mismatches 0.
                                int vi1 = tCur.vertexIdx[sharedEdge];
                                int vi2 = tCur.vertexIdx[(sharedEdge + 1) % 3];
                                if (vi1 < s_walkmesh.numVertices && vi2 < s_walkmesh.numVertices) {
                                    float ex1 = (float)s_walkmesh.vertices[vi1].x;
                                    float ey1 = (float)s_walkmesh.vertices[vi1].y;
                                    float ex2 = (float)s_walkmesh.vertices[vi2].x;
                                    float ey2 = (float)s_walkmesh.vertices[vi2].y;
                                    float edx = ex2 - ex1;
                                    float edy = ey2 - ey1;
                                    float edLen = sqrtf(edx*edx + edy*edy);
                                    if (edLen > 0.001f) {
                                        float perp1x = -edy / edLen, perp1y =  edx / edLen;
                                        float perp2x =  edy / edLen, perp2y = -edx / edLen;
                                        float nextCX = s_walkmesh.triangles[nextTri].centerX;
                                        float nextCY = s_walkmesh.triangles[nextTri].centerY;
                                        float toNextX = nextCX - px, toNextY = nextCY - pz;
                                        float dot1 = perp1x * toNextX + perp1y * toNextY;
                                        float dot2 = perp2x * toNextX + perp2y * toNextY;
                                        float ndx = (dot1 >= dot2) ? perp1x : perp2x;
                                        float ndy = (dot1 >= dot2) ? perp1y : perp2y;
                                        float altdx = (dot1 >= dot2) ? perp2x : perp1x;
                                        float altdy = (dot1 >= dot2) ? perp2y : perp1y;

                                        // v0.18.3.309 (#114): if the player has NOT
                                        // moved since the last nudge, the default
                                        // perpendicular is pressing into an invisible
                                        // rail (see the pin-state comment in
                                        // field_nav_autodrive_helpers.inl). Rotate the
                                        // escape direction so we stop repeating a nudge
                                        // that provably does nothing, and lengthen it so
                                        // a real escape has time to register. The walkmesh
                                        // can't tell us which way is clear, so we probe:
                                        //   mode 0: perpendicular toward the next tri (default)
                                        //   mode 1: opposite perpendicular (rail may be on the near side)
                                        //   mode 2: toward this triangle's centroid (retreat off an edge rail into open interior)
                                        //   mode 3: back along the corridor (un-wedge, then the odd-phase re-path re-approaches)
                                        {
                                            float pinDx = px - s_drivePinnedPosX;
                                            float pinDy = pz - s_drivePinnedPosY;
                                            float pinMoved = sqrtf(pinDx*pinDx + pinDy*pinDy);
                                            if (pinMoved > DRIVE_PIN_MOVE_EPS) s_drivePinnedCount = 0;
                                            else                               s_drivePinnedCount++;
                                            s_drivePinnedPosX = px; s_drivePinnedPosY = pz;

                                            int escapeMode = s_drivePinnedCount & 3;   // 0..3
                                            if (escapeMode == 1) {
                                                ndx = altdx; ndy = altdy;
                                            } else if (escapeMode == 2) {
                                                float toCX = tCur.centerX - px, toCY = tCur.centerY - pz;
                                                float l = sqrtf(toCX*toCX + toCY*toCY);
                                                if (l > 1.0f) { ndx = toCX / l; ndy = toCY / l; }
                                            } else if (escapeMode == 3) {
                                                int backIdx = (corridorPos > 0) ? corridorPos - 1 : corridorPos;
                                                uint16_t backTri = s_corridor[backIdx];
                                                if (backTri < (uint16_t)s_walkmesh.numTriangles) {
                                                    float bX = s_walkmesh.triangles[backTri].centerX - px;
                                                    float bY = s_walkmesh.triangles[backTri].centerY - pz;
                                                    float l = sqrtf(bX*bX + bY*bY);
                                                    if (l > 1.0f) { ndx = bX / l; ndy = bY / l; }
                                                }
                                            }
                                        }
                                        // Escalate the nudge duration with the pin count
                                        // (8 -> 32 ticks) so a longer press can break contact.
                                        int escNudgeTicks = NUDGE_TICKS +
                                            (s_drivePinnedCount > 4 ? 4 : s_drivePinnedCount) * 6;

                                        bool crossesTrig = (s_capturedLineCount > 0 &&
                                            WouldCrossTriggerLine(px, pz, ndx * 100.0f, ndy * 100.0f, s_driveSkipTrigIdx));
                                        if (crossesTrig) {
                                            ndx = altdx; ndy = altdy;
                                            crossesTrig = (s_capturedLineCount > 0 &&
                                                WouldCrossTriggerLine(px, pz, ndx * 100.0f, ndy * 100.0f, s_driveSkipTrigIdx));
                                        }
                                        if (!crossesTrig) {
                                            s_driveWiggleTicks = escNudgeTicks;
                                            uint8_t nudgeDir = 0;
                                            if (ndy >  0.3f) nudgeDir |= DIR_UP;
                                            if (ndy < -0.3f) nudgeDir |= DIR_DOWN;
                                            if (ndx >  0.3f) nudgeDir |= DIR_RIGHT;
                                            if (ndx < -0.3f) nudgeDir |= DIR_LEFT;
                                            if (nudgeDir == 0) nudgeDir = DIR_UP;
                                            s_driveWiggleDir = nudgeDir;
                                            SetAnalogFromVector(ndx * 1000.0f, ndy * 1000.0f);
                                            SetHeldDirections(nudgeDir);
                                            nudged = true;
                                            Log::Field("FieldNavigation: [drive] recovery %d — nudge tri %d->%d "
                                                       "dir=(%.2f,%.2f) %d ticks escapeMode=%d pinned=%d [v0.18.3.309 #114]",
                                                       s_driveWigglePhase, (int)nowTri, (int)nextTri,
                                                       ndx, ndy, escNudgeTicks,
                                                       s_drivePinnedCount & 3, s_drivePinnedCount);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (!nudged) {
                        // Couldn't compute nudge — fall back to re-path.
                        Log::Field("FieldNavigation: [drive] recovery %d — nudge failed, falling back to re-path",
                                   s_driveWigglePhase);
                        if (rpGoal >= 0 && (int)nowTri != rpGoal) {
                            float savedWp[MAX_WAYPOINTS][2];
                            int savedWpCount = s_waypointCount;
                            int savedWpIdx = s_waypointIdx;
                            bool savedFunnel = s_usingFunnel;
                            memcpy(savedWp, s_waypoints, sizeof(float) * 2 * savedWpCount);
                            if (ComputeAStarPath((int)nowTri, rpGoal, ei, s_driveSkipTrigIdx)) {
                                FunnelPath(px, pz, rpTx, rpTz);
                                s_wpMinDist = 1e30f;
                                Log::Field("FieldNavigation: [drive] recovery %d — re-pathed (nudge fallback): %d wp",
                                           s_driveWigglePhase, s_waypointCount);
                            } else {
                                memcpy(s_waypoints, savedWp, sizeof(float) * 2 * savedWpCount);
                                s_waypointCount = savedWpCount;
                                s_waypointIdx = savedWpIdx;
                                s_usingFunnel = savedFunnel;
                            }
                        }
                    }
                } else {
                    // Odd phase: re-run A* and generate fresh funnel path.
                    if (rpGoal >= 0 && (int)nowTri != rpGoal) {
                        float savedWp[MAX_WAYPOINTS][2];
                        int savedWpCount = s_waypointCount;
                        int savedWpIdx = s_waypointIdx;
                        bool savedFunnel = s_usingFunnel;
                        memcpy(savedWp, s_waypoints, sizeof(float) * 2 * savedWpCount);

                        if (ComputeAStarPath((int)nowTri, rpGoal, ei, s_driveSkipTrigIdx)) {
                            FunnelPath(px, pz, rpTx, rpTz);
                            s_wpMinDist = 1e30f;
                            Log::Field("FieldNavigation: [drive] recovery %d — re-pathed: %d wp from tri %d",
                                       s_driveWigglePhase, s_waypointCount, (int)nowTri);
                        } else {
                            memcpy(s_waypoints, savedWp, sizeof(float) * 2 * savedWpCount);
                            s_waypointCount = savedWpCount;
                            s_waypointIdx = savedWpIdx;
                            s_usingFunnel = savedFunnel;
                            Log::Field("FieldNavigation: [drive] recovery %d — A* failed from tri %d, keeping old %d wp",
                                       s_driveWigglePhase, (int)nowTri, savedWpCount);
                        }
                    } else {
                        Log::Field("FieldNavigation: [drive] recovery %d — already on goal tri or no goal",
                                   s_driveWigglePhase);
                    }
                }
            }
        }
        // Reset stuck position for fresh window.
        s_driveStuckPosX = px;
        s_driveStuckPosY = pz;
        if (s_driveWiggleTicks == 0)
            SetHeldDirections(heading);
    } else {
        // Normal heading toward waypoint/target.
        SetHeldDirections(heading);
    }
}

// Called every Update() tick (16ms cadence, unthrottled).
// Keys: VK_OEM_MINUS (-) = previous, VK_OEM_PLUS (+/=) = next,
//       VK_BACK (Backspace) = repeat current (gated: only on field, not during FMV).
