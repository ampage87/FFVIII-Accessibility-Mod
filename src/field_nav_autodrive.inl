// field_nav_autodrive.inl — Auto-drive state machine, steering, recovery
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// Auto-drive: inject arrow-key input to walk toward the selected entity
// ============================================================================

// Inject or release a direction key via SendInput using hardware scan codes.
// DirectInput reads raw hardware scan codes, so we must use KEYEVENTF_SCANCODE
// rather than KEYEVENTF_EXTENDEDKEY+VK.  Arrow keys have the E0 extended prefix,
// indicated by KEYEVENTF_EXTENDEDKEY alongside KEYEVENTF_SCANCODE.
static void InjectKey(WORD scanCode, bool down)
{
    INPUT inp      = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = 0;      // must be 0 when using KEYEVENTF_SCANCODE
    inp.ki.wScan   = scanCode;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY
                   | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
}

// Release all held direction keys and clear the held bitmask.
static void ReleaseAllDirections()
{
    if (s_driveHeld & DIR_UP)    InjectKey(SC_UP,    false);
    if (s_driveHeld & DIR_DOWN)  InjectKey(SC_DOWN,  false);
    if (s_driveHeld & DIR_LEFT)  InjectKey(SC_LEFT,  false);
    if (s_driveHeld & DIR_RIGHT) InjectKey(SC_RIGHT, false);
    s_driveHeld = 0;
}

// Apply a new desired direction bitmask: release keys no longer needed,
// press keys newly needed.
// v05.85: Keyboard injection is REQUIRED to activate the game's movement code
// path. Analog steering overrides the direction, but keyboard buttons are the
// trigger that makes the game process movement at all.
static void SetHeldDirections(uint8_t desired)
{
    uint8_t toRelease = s_driveHeld  & ~desired;
    uint8_t toPress   = desired & ~s_driveHeld;
    if (toRelease & DIR_UP)    InjectKey(SC_UP,    false);
    if (toRelease & DIR_DOWN)  InjectKey(SC_DOWN,  false);
    if (toRelease & DIR_LEFT)  InjectKey(SC_LEFT,  false);
    if (toRelease & DIR_RIGHT) InjectKey(SC_RIGHT, false);
    if (toPress   & DIR_UP)    InjectKey(SC_UP,    true);
    if (toPress   & DIR_DOWN)  InjectKey(SC_DOWN,  true);
    if (toPress   & DIR_LEFT)  InjectKey(SC_LEFT,  true);
    if (toPress   & DIR_RIGHT) InjectKey(SC_RIGHT, true);
    s_driveHeld = desired;
}

// v06.14: Per-field heading calibration.
// The game interprets analog stick input relative to the camera orientation.
// On each field, lX=+1000 moves the player along the camera's right vector
// in entity/world space, and lY=+1000 moves along the camera's down vector.
// We calibrate by injecting a known analog direction at drive start and
// measuring the resulting world-space movement direction.
//
// Until calibrated, we use the .ca camera axes (loaded at field load) as
// a best guess. The calibration refines this empirically.
// NOTE: s_camRightX/Y, s_camDownX/Y, s_camCalibrated are declared earlier
// (before FormatNavComponents) so compass directions can access them.

// v06.14: Heading calibration state machine.
// At drive start, we inject lX=+1000,lY=0 for a few ticks, measure the
// resulting movement direction, and use that as the camera right axis.
// Then inject lX=0,lY=+1000 for a few ticks to get the camera down axis.
// After both are measured, s_camCalibrated=true and we use the measured axes.
static int   s_calibPhase = 0;       // 0=not calibrating, 1=measuring right, 2=measuring down, 3=done
static int   s_calibTicks = 0;       // ticks in current calibration phase
static float s_calibStartX = 0;      // player position at calibration phase start
static float s_calibStartY = 0;
static const int CALIB_SETTLE_TICKS = 8;   // ticks to let the game start moving
static const int CALIB_MEASURE_TICKS = 16; // ticks to measure movement direction
static bool  s_calibPending = false;  // true if calibration should run at drive start

// v05.84/v06.14: Set analog override from a world-space direction vector.
// Converts (dx, dy) in entity/world space into DIJOYSTATE2 lX/lY values
// using the per-field camera axes to produce correct screen-relative input.
// DirectInput axis convention: lX +1000 = screen right, lY +1000 = screen down.
static void SetAnalogFromVector(float dx, float dy)
{
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) {
        s_analogDesiredLX = 0;
        s_analogDesiredLY = 0;
        return;
    }
    float nx = dx / len;
    float ny = dy / len;
    // v06.14: Project world-space direction onto camera axes.
    // lX = dot(worldDir, camRight) = how much of the desired direction
    //   aligns with the camera's rightward axis.
    // lY = dot(worldDir, camDown) = how much aligns with camera's downward axis.
    float lxF = (nx * s_camRightX + ny * s_camRightY) * 1000.0f;
    float lyF = (nx * s_camDownX  + ny * s_camDownY)  * 1000.0f;
    int lx = (int)lxF;
    int ly = (int)lyF;
    if (lx < -1000) lx = -1000; if (lx > 1000) lx = 1000;
    if (ly < -1000) ly = -1000; if (ly > 1000) ly = 1000;
    s_analogDesiredLX = lx;
    s_analogDesiredLY = ly;
}

// Stop auto-drive cleanly: release keys, clear state, optionally speak reason.
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    // v05.85: Release any held keyboard direction keys.
    ReleaseAllDirections();
    // v05.84: Deactivate analog override and remove fake gamepad.
    s_analogOverrideActive = false;
    s_analogDesiredLX = 0;
    s_analogDesiredLY = 0;
    // Restore original dinput pointers.
    if (s_fakeGamepadInstalled && FF8Addresses::HasDinputGamepadPtrs()) {
        *FF8Addresses::pDinputGamepadDevicePtr = s_savedDevicePtr;
        *FF8Addresses::pDinputGamepadStatePtr  = s_savedStatePtr;
        s_fakeGamepadInstalled = false;
        Log::Field("FieldNavigation: [drive] fake gamepad removed, original ptrs restored");
    }
    // v06.21: Do NOT restore talk radius here — the player needs the expanded
    // radius to persist so they can press X to interact after "Arrived".
    // The game's TALKRADIUS opcode resets it naturally on the next field load.
    if (s_driveTalkRadExpanded) {
        Log::Field("FieldNavigation: [drive] talkRadius stays expanded (%u -> %u) for ent%d — resets on field load",
                   (unsigned)s_driveOrigTalkRadius,
                   (unsigned)GetEntityTalkRadius(s_driveTargetEntityIdx),
                   s_driveTargetEntityIdx);
    }
    s_driveTalkRadExpanded = false;
    s_driveTargetEntityIdx = -1;
    s_driveOrigTalkRadius = 0;

    // v06.08: NavLog drive end
    NavLog::DriveEnd(reason ? reason : "unknown", s_driveTotalTicks, 0.0f,
                     s_driveWigglePhase, s_driveStartDist);

    s_driveActive = false;
    s_driveTrigTarget = false;
    s_driveTrigCrossStart = 0.0f;
    s_driveSkipTrigIdx = -1;
    Log::Field("FieldNavigation: [drive] stopped: %s", reason);
    if (reason) ScreenReader::Speak(reason);
}

// Called from Update() every tick while auto-drive is active.
// Computes direction to target, injects appropriate arrow keys.
static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    // Safety: must be on field and not in a menu/FMV.
    if (!FF8Addresses::IsOnField()) { StopAutoDrive("Left field."); return; }

    if (s_playerEntityIdx < 0) { StopAutoDrive("Player position lost."); return; }

    // v06.14: Heading calibration — runs at the start of the first drive on each field.
    // Injects known analog directions and measures the resulting world-space movement
    // to determine the camera-to-world rotation for this field.
    if (s_calibPhase > 0 && s_calibPhase < 3) {
        float cpx = 0, cpy = 0;
        GetEntityPos(s_playerEntityIdx, cpx, cpy);
        s_calibTicks++;

        if (s_calibPhase == 1) {
            // Phase 1: inject lX=+1000, lY=0 (screen-right) and measure movement.
            s_analogOverrideActive = true;
            s_analogDesiredLX = 1000;
            s_analogDesiredLY = 0;
            SetHeldDirections(DIR_RIGHT);  // keyboard trigger for movement

            if (s_calibTicks == CALIB_SETTLE_TICKS) {
                // Record position after settling.
                s_calibStartX = cpx;
                s_calibStartY = cpy;
            } else if (s_calibTicks >= CALIB_SETTLE_TICKS + CALIB_MEASURE_TICKS) {
                // Measure displacement.
                float cdx = cpx - s_calibStartX;
                float cdy = cpy - s_calibStartY;
                float cdist = sqrtf(cdx*cdx + cdy*cdy);
                if (cdist > 5.0f) {
                    // Normalize: this is the world-space direction of lX=+1000.
                    s_camRightX = cdx / cdist;
                    s_camRightY = cdy / cdist;
                    Log::Field("FieldNavigation: [CALIB] phase 1 done: lX=+1000 moved (%.1f,%.1f) dist=%.1f -> camRight=(%.3f,%.3f)",
                               cdx, cdy, cdist, s_camRightX, s_camRightY);
                } else {
                    Log::Field("FieldNavigation: [CALIB] phase 1 FAILED: no movement (dist=%.1f), keeping default camRight", cdist);
                }
                // Transition to phase 2.
                s_calibPhase = 2;
                s_calibTicks = 0;
            }
            s_driveTotalTicks++;
            return;  // don't run normal navigation during calibration
        }

        if (s_calibPhase == 2) {
            // Phase 2: inject lX=0, lY=+1000 (screen-down) and measure movement.
            s_analogOverrideActive = true;
            s_analogDesiredLX = 0;
            s_analogDesiredLY = 1000;
            SetHeldDirections(DIR_DOWN);  // keyboard trigger for movement

            if (s_calibTicks == CALIB_SETTLE_TICKS) {
                s_calibStartX = cpx;
                s_calibStartY = cpy;
            } else if (s_calibTicks >= CALIB_SETTLE_TICKS + CALIB_MEASURE_TICKS) {
                float cdx = cpx - s_calibStartX;
                float cdy = cpy - s_calibStartY;
                float cdist = sqrtf(cdx*cdx + cdy*cdy);
                if (cdist > 5.0f) {
                    s_camDownX = cdx / cdist;
                    s_camDownY = cdy / cdist;
                    Log::Field("FieldNavigation: [CALIB] phase 2 done: lY=+1000 moved (%.1f,%.1f) dist=%.1f -> camDown=(%.3f,%.3f)",
                               cdx, cdy, cdist, s_camDownX, s_camDownY);
                } else {
                    // v06.17: Derive camDown from camRight by 90° clockwise rotation.
                    // In screen space, rotating right vector 90° CW gives the down vector.
                    // rotation: (x,y) -> (y, -x)
                    s_camDownX = s_camRightY;
                    s_camDownY = -s_camRightX;
                    Log::Field("FieldNavigation: [CALIB] phase 2 FAILED: no movement (dist=%.1f), derived camDown=(%.3f,%.3f) from camRight perpendicular",
                               cdist, s_camDownX, s_camDownY);
                }
                // Calibration complete.
                s_calibPhase = 3;
                s_camCalibrated = true;
                s_calibPending = false;
                // Log the final calibration result.
                Log::Field("FieldNavigation: [CALIB] complete: camRight=(%.3f,%.3f) camDown=(%.3f,%.3f)",
                           s_camRightX, s_camRightY, s_camDownX, s_camDownY);
                // Reset stuck detection to account for calibration movement.
                s_driveStuckTicks = 0;
                GetEntityPos(s_playerEntityIdx, s_driveStuckPosX, s_driveStuckPosY);
            }
            s_driveTotalTicks++;
            return;  // don't run normal navigation during calibration
        }
    }

    const EntityInfo& catTarget = (s_selectedCatalogIdx < s_catalogCount)
                                   ? s_catalog[s_selectedCatalogIdx]
                                   : s_catalog[0]; // safety fallback
    int ei = catTarget.entityIdx;
    if (ei == s_playerEntityIdx) { StopAutoDrive("No target."); return; }
    // v0.07.94: Valid targets: >=0 (runtime entity), <=-200 (trigger line), <=-300 (JSM-injected), <=-400 (INF gateway).
    if (ei < 0 && ei > -200) { StopAutoDrive("Target lost."); return; }
    if (ei >= MAX_ENTITIES)                              { StopAutoDrive("Target lost."); return; }

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
        StopAutoDrive("Player position lost.");
        return;
    }
    // v0.07.74: JSM-injected entities use SET3 extraction positions.
    // v0.07.83: Trigger line exits use SETLINE center positions.
    // v0.07.94: INF gateway exits use deduplicated gateway center positions.
    bool gotTarget = false;
    if (ei <= -400) {
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
    float dx   = tx - px;
    float dz   = tz - pz;
    float dist = sqrtf(dx*dx + dz*dz);

    // v05.76: For trigger line targets, check if the player has crossed the line.
    // This is the primary arrival condition for screen transitions and events.
    if (s_driveTrigTarget && ei <= -200) {
        int trigIdx = -(ei + 200);
        if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
            float tlx1 = (float)s_capturedLines[trigIdx].x1;
            float tly1 = (float)s_capturedLines[trigIdx].y1;
            float tlx2 = (float)s_capturedLines[trigIdx].x2;
            float tly2 = (float)s_capturedLines[trigIdx].y2;
            float tdx = tlx2 - tlx1;
            float tdy = tly2 - tly1;
            float crossNow = tdx * (pz - tly1) - tdy * (px - tlx1);
            // Player has crossed if the sign flipped from start.
            if (s_driveTrigCrossStart != 0.0f && crossNow * s_driveTrigCrossStart < 0.0f) {
                StopAutoDrive("Arrived.");
                return;
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
    }
    if (dist < s_driveArriveDist) {
        StopAutoDrive("Arrived.");
        return;
    }

    // v05.66: If we have A* waypoints, steer toward the current waypoint
    // instead of the final target. Advance to the next waypoint when close.
    // Chain-advance is delayed until tick 30 (~0.5s) so we don't skip
    // nearby waypoints before the player has started moving.
    float steerX = tx, steerY = tz;  // default: straight to target
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
    }
    // v06.17: Corridor-level steering — steer toward the shared-edge midpoint
    // of the next corridor triangle instead of distant funnel waypoints.
    // This gives very local targets that are always close, preventing overshoot.
    // The corridor from A* tells us which triangle sequence leads to the goal.
    // Each tick, we find the player's current triangle in the corridor and target
    // the midpoint of the shared edge to the next corridor triangle.
    if (s_walkmesh.valid && s_corridorCount >= 2 && s_driveTotalTicks >= 30) {
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
                    int vi1 = tCur.vertexIdx[(sharedEdge + 1) % 3];
                    int vi2 = tCur.vertexIdx[(sharedEdge + 2) % 3];
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
                        }
                        // else: keep the funnel waypoint as steer target
                    }
                }
            } else if (corridorPos < 0) {
                // Player left the corridor — re-path needed (recovery will handle).
            }
        }
    }

    // Recompute dx/dz toward the steer target.
    dx = steerX - px;
    dz = steerY - pz;

    // v06.17: Wall-avoidance steering bias.
    // DISABLED in v06.20: Causes more harm than good. In narrow corridors
    // (bg2f_1), the bias pushes the player OUT of the corridor. In classrooms,
    // it interferes with short drives. The corridor-level steering + recovery
    // system handles wall-stuck better without active avoidance.
    // The code remains for potential re-enabling with better narrow-space logic.
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
                // Wall edge: vertices (e+1)%3 and (e+2)%3
                int wvi1 = tri.vertexIdx[(e + 1) % 3];
                int wvi2 = tri.vertexIdx[(e + 2) % 3];
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

    // v06.17: Trigger-line proximity check.
    // Per-tick: if the current steering direction would carry the player across
    // a non-target trigger line within the next ~200 units, redirect steering
    // to be parallel to the trigger line instead of crossing it.
    // Skip this check for trigger lines that the A* path legitimately crosses
    // (the target trigger line, exempted via s_driveSkipTrigIdx).
    // Also skip for NPC targets where the NPC is on the other side of a trigger
    // line — A* already routed through it, so crossing is intentional.
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
                    }
                    break;
                }
            }
        }
    }

    // v05.62: Max drive time safety cutoff.
    s_driveTotalTicks++;
    if (s_driveTotalTicks >= DRIVE_MAX_TICKS) {
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

    // v05.75: Heading computation. Map world-space delta to arrow keys.
    // Log analysis confirms: pressing UP moves player in +Y world direction.
    // For X axis: pressing RIGHT moves player in +X world direction (v05.74
    // confirmed back-to-front auto-drive worked with direct X mapping).
    // Y axis is inverted (UP=+Y but -Y=screen-up), X axis is NOT inverted.
    uint8_t heading = 0;
    if (dz >  DRIVE_AXIS_THRESH) heading |= DIR_UP;    // +Y world = press UP
    if (dz < -DRIVE_AXIS_THRESH) heading |= DIR_DOWN;  // -Y world = press DOWN
    if (dx >  DRIVE_AXIS_THRESH) heading |= DIR_RIGHT; // +X world = press RIGHT
    if (dx < -DRIVE_AXIS_THRESH) heading |= DIR_LEFT;  // -X world = press LEFT
    if (heading == 0) heading = DIR_UP;  // fallback: shouldn't happen (dist > arrive)

    // v05.83: Activate analog override and set direction from the computed vector.
    // This gives us true 360-degree steering via the gamepad analog path.
    // The keyboard injection (SetHeldDirections) is kept as a fallback
    // in case the analog path isn't read by the game engine.
    s_analogOverrideActive = true;
    SetAnalogFromVector(dx, dz);

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
                float rpTx = 0, rpTz = 0;
                bool rpGot = false;
                if (ei <= -400) {
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
                        int rpSkipTrig = -1;
                        if (catTarget.entityIdx <= -200 && catTarget.entityIdx > -300) {
                            rpSkipTrig = -(catTarget.entityIdx + 200);
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
    } else if (s_driveStuckTicks >= DRIVE_STUCK_THRESH) {
        // v06.16: Simplified recovery system.
        // No more odd/even phase alternation. Simple cycle:
        //   Odd phases:  re-run A* from current position → funnel path
        //   Even phases: single perpendicular nudge to break wall contact
        // After nudge completes, the wiggle-completion code above re-paths via funnel.
        s_driveStuckTicks = 0;
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
                float rpTx = tx, rpTz = tz;
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
                                int vi1 = tCur.vertexIdx[(sharedEdge + 1) % 3];
                                int vi2 = tCur.vertexIdx[(sharedEdge + 2) % 3];
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
                                        bool crossesTrig = (s_capturedLineCount > 0 &&
                                            WouldCrossTriggerLine(px, pz, ndx * 100.0f, ndy * 100.0f, s_driveSkipTrigIdx));
                                        if (crossesTrig) {
                                            ndx = altdx; ndy = altdy;
                                            crossesTrig = (s_capturedLineCount > 0 &&
                                                WouldCrossTriggerLine(px, pz, ndx * 100.0f, ndy * 100.0f, s_driveSkipTrigIdx));
                                        }
                                        if (!crossesTrig) {
                                            s_driveWiggleTicks = NUDGE_TICKS;
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
                                            Log::Field("FieldNavigation: [drive] recovery %d — nudge perpendicular "
                                                       "tri %d->%d dir=(%.2f,%.2f) %d ticks",
                                                       s_driveWigglePhase, (int)nowTri, (int)nextTri,
                                                       ndx, ndy, NUDGE_TICKS);
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
