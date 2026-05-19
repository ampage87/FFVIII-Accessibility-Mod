// field_nav_handlekeys.inl — Key dispatch (navigation, diagnostics, auto-drive toggle)
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

static void HandleKeys()
{
    // Only handle nav keys when on the field.
    if (!FF8Addresses::IsOnField()) return;

    bool minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus  = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    // Backspace for nav only when no FMV is playing (FmvSkip owns it during FMVs).
    bool bksp  = !FF8Addresses::IsMoviePlaying() &&
                 (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    // \ (backslash) toggles auto-drive to selected entity.
    bool drive = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;

    // v0.14.75: F11 VISDIAG dump removed. Was a v05.69 research diagnostic
    // for finding the entity SHOW/HIDE visibility-flag offset by dumping
    // candidate byte ranges (0x188, 0x1A0, 0x21A, 0x240) for every
    // model-bearing field entity. The investigation closed once the
    // catalog was built; F11 is now reserved globally in dinput8.cpp for
    // on-demand screenshot capture. The s_f11WasDown static and any
    // related state are no longer needed here.

    // v0.14.45: Removed v0.12.21 F2 = Director Varblock + Entity Struct Diagnostic.
    // F2 is now bound to GameAudio::ToggleDucking in dinput8.cpp. The Director
    // varblock investigation it served (interactive object positioning) was
    // resolved in session 43 -- SETLINE triggers are definitive, Director
    // entities are dead code.

    // v0.14.45: Removed F12 = On-demand POPM_W capture (10s window). The
    // capture diagnostic served the interactive object positioning
    // investigation, which is now closed. F12 is reserved for future
    // per-session diagnostic builds.

    // v0.12.13: Animation scan polling REMOVED — replaced by interaction range diagnostic.
    // Old ANIMSCAN code removed. Static variables (s_animScanActive etc.) still declared
    // above but no longer used.

    // v05.86: Arrow keys cancel auto-drive immediately.
    // The player pressing any direction key means they want manual control.
    // We must only cancel if the arrow key is NOT one we're currently injecting
    // via SetHeldDirections. Check which keys are held by us (s_driveHeld bitmask)
    // and only cancel on keys we're NOT injecting.
    //
    // v0.15.9.2: Suppress arrow-key cancel when chase-drive is active.
    // chase auto-pilot owns the drive on chase fields; player arrow taps
    // (or JAWS-injected ones) should not bump us out of chase mode. Player
    // can still cancel via the chase ASK Manual mode at chase trigger.
    if (s_driveActive && !s_chaseDriveActive) {
        bool arrowUp    = (GetAsyncKeyState(VK_UP)    & 0x8000) != 0;
        bool arrowDown  = (GetAsyncKeyState(VK_DOWN)  & 0x8000) != 0;
        bool arrowLeft  = (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0;
        bool arrowRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
        // v06.08: During recovery (stuck phases), any arrow key cancels immediately.
        // This ensures the player can always regain control when the drive is stuck.
        // During normal steering, mask out keys we're injecting ourselves.
        // v06.14: Arrow-key cancel REMOVED during recovery.
        // JAWS intercepts arrow keys in fullscreen DirectX, causing false
        // cancels at phase >= 4. The user can cancel via backslash toggle.
        // During normal steering (phase 0), still cancel on non-injected arrows
        // to allow manual override before recovery starts.
        if (s_driveWigglePhase == 0) {
            bool playerUp    = arrowUp    && !(s_driveHeld & DIR_UP);
            bool playerDown  = arrowDown  && !(s_driveHeld & DIR_DOWN);
            bool playerLeft  = arrowLeft  && !(s_driveHeld & DIR_LEFT);
            bool playerRight = arrowRight && !(s_driveHeld & DIR_RIGHT);
            if (playerUp || playerDown || playerLeft || playerRight) {
                StopAutoDrive("Cancelled.");
            }
        }
    }

    if (minus && !s_minusWasDown) { RefreshCatalog(); if (s_driveActive) StopAutoDrive("Cancelled."); CycleEntity(-1); if (s_gpsActive) { StopGPS(nullptr); StartGPS(s_selectedCatalogIdx); } }
    if (plus  && !s_plusWasDown)  { RefreshCatalog(); if (s_driveActive) StopAutoDrive("Cancelled."); CycleEntity(+1); if (s_gpsActive) { StopGPS(nullptr); StartGPS(s_selectedCatalogIdx); } }
    // v0.12.02: Backspace toggles GPS guided navigation.
    if (bksp && !s_bkspWasDown) {
        if (s_gpsActive) {
            StopGPS("Navigation off.");
        } else {
            RefreshCatalog();
            if (s_catalogCount > 0 && s_selectedCatalogIdx < s_catalogCount) {
                StartGPS(s_selectedCatalogIdx);
            }
        }
    }
    if (drive && !s_driveWasDown) {
        if (s_driveActive) {
            // v0.15.9.2: If chase-drive owns the drive, refuse the toggle
            // -- the player can't cancel chase auto-pilot via backslash.
            // Otherwise it's an F9 player drive; toggle cancels as before.
            if (s_chaseDriveActive) {
                ScreenReader::Speak("Auto-drive unavailable: chase auto-pilot is active.");
                Log::Field("FieldNavigation: F9 drive REFUSED "
                           "(chase-drive is active)");
            } else {
                StopAutoDrive("Cancelled.");
            }
        } else if (s_directionDriveActive) {
            // v0.15.9.1: Direction-drive (chase auto-pilot) owns the
            // analog/fake-gamepad path right now. F9 path-finding cannot
            // run alongside it.
            ScreenReader::Speak("Auto-drive unavailable: chase auto-pilot is active.");
            Log::Field("FieldNavigation: F9 drive REFUSED "
                       "(direction-drive is active)");
        } else if (FieldDialog::IsDialogOpen()) {
            ScreenReader::Speak("Auto-drive unavailable: dialog is open.");
        } else {
            // Validate we have a usable target before starting.
            const EntityInfo& drTgt = (s_selectedCatalogIdx < s_catalogCount)
                                      ? s_catalog[s_selectedCatalogIdx] : s_catalog[0];
            {
            float _px = 0, _pz = 0, _tx = 0, _tz = 0;
            bool drValid = false;
            if (drTgt.entityIdx != s_playerEntityIdx &&
                GetEntityPos(s_playerEntityIdx, _px, _pz)) {
                if (drTgt.entityIdx <= -400) {
                    // v0.07.94: INF gateway exit.
                    int gwIdx = -(drTgt.entityIdx + 400);
                    if (gwIdx >= 0 && gwIdx < s_dedupGatewayCount) {
                        _tx = s_dedupGateways[gwIdx].centerX;
                        _tz = s_dedupGateways[gwIdx].centerY;
                        drValid = true;
                    }
                } else if (drTgt.entityIdx <= -300) {
                    // v0.07.74: JSM-injected entity.
                    int jsmIdx = -(drTgt.entityIdx + 300);
                    if (jsmIdx >= 0 && jsmIdx < s_jsmEntityCount && s_jsmEntities[jsmIdx].hasPosition) {
                        _tx = (float)s_jsmEntities[jsmIdx].posX;
                        _tz = (float)s_jsmEntities[jsmIdx].posY;
                        drValid = true;
                    }
                } else if (drTgt.entityIdx <= -200) {
                    int trigIdx = -(drTgt.entityIdx + 200);
                    if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
                        _tx = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
                        _tz = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
                        drValid = true;
                    }
                } else if (drTgt.entityIdx >= 0 && GetEntityPos(drTgt.entityIdx, _tx, _tz)) {
                    drValid = true;
                }
            }
            if (drValid) {
                // Seed the triId from the player's current triangle so the
                // first tick doesn't see a "change" from the uninitialized value.
                uint16_t seedTri = 0xFFFF;
                {
                    uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    if (base)
                        seedTri = *(uint16_t*)(base + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
                }
                s_driveActive      = true;
                s_driveLastTriId   = seedTri;
                s_driveStuckTicks  = 0;
                s_driveWiggleTicks = 0;
                s_driveWiggleDir   = 0;
                s_driveWigglePhase = 0;  // v05.68: reset recovery rotation
                s_driveTotalTicks  = 0;
                s_driveLogTimer    = 0;
                s_driveStuckPosX   = _px;  // v05.90: velocity-based stuck detection
                s_driveStuckPosY   = _pz;
                s_driveProgressDist    = 1e30f;  // v06.10: reset progress tracking
                s_driveNoProgressCount = 0;
                s_lastRecoveryTri      = 0xFFFF;  // v0.17.6.1: clear so first recovery doesn't see a stale tri from a prior drive

                // v06.14: Start heading calibration if not yet calibrated for this field.
                // v0.17.6.0: F9 path-finding auto-drive NEVER runs CALIB. It uses the
                // .ca-file-derived, 90-degree-quantized axes that manual nav sets at
                // field load (s_camRight/Down). CALIB stays available for chase-drive
                // (StartChaseDrive sets s_calibPhase=1 conditionally on s_calibPending).
                // The phase-1-failed-on-wall-stuck class of bugs that the bghall_1 BAT
                // exhibited cannot occur for F9 because F9 never enters CALIB to begin
                // with. See SetAnalogFromVector's v0.17.6.0 block comment for the full
                // rationale and the chase-vs-F9 axis-source split.
                s_calibPhase = 3;  // skip calibration unconditionally for F9

                // v05.84: Install fake gamepad so the game processes our analog values.
                if (FF8Addresses::HasDinputGamepadPtrs() && !s_fakeGamepadInstalled) {
                    s_savedDevicePtr = *FF8Addresses::pDinputGamepadDevicePtr;
                    s_savedStatePtr  = *FF8Addresses::pDinputGamepadStatePtr;
                    // Zero the fake DIJOYSTATE2 (centered, no buttons).
                    memset(s_fakeDIJOYSTATE2, 0, sizeof(s_fakeDIJOYSTATE2));
                    // Point the game's state pointer at our fake struct.
                    *FF8Addresses::pDinputGamepadStatePtr = (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2;
                    // Set device to non-null sentinel so game thinks gamepad exists.
                    *FF8Addresses::pDinputGamepadDevicePtr = FAKE_DEVICE_SENTINEL;
                    s_fakeGamepadInstalled = true;
                    Log::Field("FieldNavigation: [drive] fake gamepad installed: device=0x%08X state=0x%08X (saved dev=0x%08X state=0x%08X)",
                               FAKE_DEVICE_SENTINEL, (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2,
                               s_savedDevicePtr, s_savedStatePtr);
                }

                // v05.80: Set per-drive arrive distance from entity talk radius.
                // For NPC entities, use talkRadius - 20 so we stop INSIDE the
                // interaction zone, allowing the player to press X to talk.
                // Minimum 30 to avoid overshoot into the NPC's collision body.
                // For gateways/triggers, use the default 300.
                // v05.90: Arrive distance strategy:
                // Drive aims to reach talkRadius distance from the NPC.
                // This is the distance at which the player can press X to talk.
                // We use talkRadius directly (not talkRadius-20) to give more
                // margin for collision bodies and narrow approaches.
                // For non-entity targets (gateways, triggers), use the default.
                //
                // v0.17.6.0: Save points and draw points split apart.
                //   - Save points stay walk-onto (30 units). The save crystal
                //     activates only when the player's model overlaps it;
                //     stopping "nearby" leaves the player unable to save.
                //   - Draw points get talkRadius treatment, matching how NPCs
                //     and interactive objects work (per Aaron's v0.17.6.0
                //     spec: "you have to walk up to draw points and interact
                //     with them just like an NPC or interactive object").
                //     Runtime-entity draw points (catalog reclassified an NPC
                //     slot as a draw point) read their engine-set talkRadius.
                //     JSM-injected draw points (no runtime entity, e.g. Fire
                //     Cavern 'drpoint') use a 120-unit default that matches
                //     GPS_ARRIVE_DIST.
                s_driveArriveDist = DRIVE_ARRIVE_DIST_DEFAULT;
                s_driveTargetEntityIdx = -1;
                s_driveOrigTalkRadius = 0;
                s_driveTalkRadExpanded = false;
                if (drTgt.type == ENT_SAVE_POINT) {
                    s_driveArriveDist = 30.0f;
                    Log::Field("FieldNavigation: [drive] Save Point target -> arriveDist=30 (walk-onto)");
                } else if (drTgt.entityIdx >= 0 && drTgt.entityIdx < MAX_ENTITIES) {
                    // NPC, Object, or runtime-entity Draw Point. Read the
                    // engine-set talkRadius and clamp to a 60-unit floor so
                    // very tight zones still leave room for the player's
                    // walking radius without overshoot.
                    uint16_t talkRad = GetEntityTalkRadius(drTgt.entityIdx);
                    if (talkRad > 0) {
                        s_driveArriveDist = (float)talkRad;
                        if (s_driveArriveDist < 60.0f) s_driveArriveDist = 60.0f;
                        // v06.21: Save original talk radius for potential expansion.
                        s_driveTargetEntityIdx = drTgt.entityIdx;
                        s_driveOrigTalkRadius = talkRad;
                        Log::Field("FieldNavigation: [drive] %s target talkRadius=%u -> arriveDist=%.0f",
                                   EntityTypeName(drTgt.type),
                                   (unsigned)talkRad, s_driveArriveDist);
                    }
                } else if (drTgt.type == ENT_DRAW_POINT) {
                    // v0.17.6.0: JSM-injected draw point (entityIdx <= -300,
                    // no runtime entity slot to query). Use a sensible default
                    // that lets the player press X to draw without first having
                    // to inch onto the exact marker.
                    s_driveArriveDist = 120.0f;
                    Log::Field("FieldNavigation: [drive] Draw Point (JSM-injected) -> arriveDist=120 (default talkRad-equivalent)");
                }

                // v05.76 / v0.17.6.0: Track trigger line and gateway crossing for arrival detection.
                // Trigger lines come from SETLINE (drTgt.entityIdx <= -200);
                // INF gateways come from the .inf gateway table (drTgt.entityIdx <= -400).
                // Both produce a line-crossing arrival condition rather than
                // point-distance, because the engine itself fires the screen transition
                // (or event) when the player physically crosses the line. Stopping
                // 300 units short of an exit means the player has to walk through
                // manually, defeating auto-drive on exits.
                //
                // The shared crossing-detection state (s_driveCrossLine*,
                // s_driveCrossLineActive, s_driveTrigCrossStart) is read by
                // UpdateAutoDrive's crossing block.
                s_driveTrigTarget = false;
                s_driveTrigCrossStart = 0.0f;
                s_driveCrossLineActive = false;
                s_driveCrossLineX1 = 0; s_driveCrossLineY1 = 0;
                s_driveCrossLineX2 = 0; s_driveCrossLineY2 = 0;
                if (drTgt.entityIdx <= -400) {
                    // v0.17.6.0: Exit gateway target. The dedup catalog entry covers
                    // 1..N raw INF gateways with the same destination field; pick the
                    // raw gateway nearest to the player and use its line endpoints for
                    // crossing detection. If the player ends up crossing a different
                    // raw gateway in the same group, the engine still fires the
                    // transition; auto-drive will stop with "Player position lost."
                    // when the field reloads, which is functionally equivalent for the
                    // user (they arrive at the next field). Picking the nearest gives
                    // the closest steer target and the most reliable cross-product
                    // sign-flip math.
                    int gwIdx = -(drTgt.entityIdx + 400);
                    if (gwIdx >= 0 && gwIdx < s_dedupGatewayCount) {
                        uint16_t destFieldId = s_dedupGateways[gwIdx].destFieldId;
                        int bestRawIdx = -1;
                        float bestDistSq = 1e30f;
                        for (int g = 0; g < s_gatewayCount; g++) {
                            if (s_gateways[g].destFieldId != destFieldId) continue;
                            // Skip degenerate (zero-length) gateway lines.
                            if (s_gateways[g].lineX1 == 0 && s_gateways[g].lineY1 == 0 &&
                                s_gateways[g].lineX2 == 0 && s_gateways[g].lineY2 == 0) continue;
                            float gcx = s_gateways[g].centerX;
                            float gcy = s_gateways[g].centerZ;  // centerZ = Y in our coords
                            float dx = gcx - _px;
                            float dy = gcy - _pz;
                            float distSq = dx*dx + dy*dy;
                            if (distSq < bestDistSq) {
                                bestDistSq = distSq;
                                bestRawIdx = g;
                            }
                        }
                        if (bestRawIdx >= 0) {
                            s_driveTrigTarget = true;
                            s_driveCrossLineX1 = s_gateways[bestRawIdx].lineX1;
                            s_driveCrossLineY1 = s_gateways[bestRawIdx].lineY1;
                            s_driveCrossLineX2 = s_gateways[bestRawIdx].lineX2;
                            s_driveCrossLineY2 = s_gateways[bestRawIdx].lineY2;
                            s_driveCrossLineActive = true;
                            float tdx = (float)(s_driveCrossLineX2 - s_driveCrossLineX1);
                            float tdy = (float)(s_driveCrossLineY2 - s_driveCrossLineY1);
                            s_driveTrigCrossStart = tdx * (_pz - (float)s_driveCrossLineY1)
                                                  - tdy * (_px - (float)s_driveCrossLineX1);
                            Log::Field("FieldNavigation: [drive] gateway target -> crossing line "
                                       "(%d,%d)->(%d,%d) crossStart=%.0f rawIdx=%d destFieldId=%u",
                                       (int)s_driveCrossLineX1, (int)s_driveCrossLineY1,
                                       (int)s_driveCrossLineX2, (int)s_driveCrossLineY2,
                                       s_driveTrigCrossStart, bestRawIdx, (unsigned)destFieldId);
                        } else {
                            Log::Field("FieldNavigation: [drive] gateway target gwIdx=%d destFieldId=%u: "
                                       "no raw gateway with line endpoints found, falling back to point-distance arrival",
                                       gwIdx, (unsigned)destFieldId);
                        }
                    }
                } else if (drTgt.entityIdx <= -200) {
                    int trigIdx = -(drTgt.entityIdx + 200);
                    if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
                        s_driveTrigTarget = true;
                        float tlx1 = (float)s_capturedLines[trigIdx].x1;
                        float tly1 = (float)s_capturedLines[trigIdx].y1;
                        float tlx2 = (float)s_capturedLines[trigIdx].x2;
                        float tly2 = (float)s_capturedLines[trigIdx].y2;
                        float tdx = tlx2 - tlx1;
                        float tdy = tly2 - tly1;
                        s_driveTrigCrossStart = tdx * (_pz - tly1) - tdy * (_px - tlx1);
                    }
                }

                // v05.93: Path computation — try line-of-sight first, fall back to A*.
                s_waypointCount = 0;
                s_waypointIdx   = 0;
                s_usingFunnel   = false;  // v05.95
                s_wpMinDist     = 1e30f;  // v06.08: reset overshoot tracker
                if (s_walkmesh.valid) {
                    int startTri = -1;
                    if (seedTri != 0xFFFF && seedTri < (uint16_t)s_walkmesh.numTriangles) {
                        startTri = (int)seedTri;
                    } else {
                        startTri = FindNearestTriangle(_px, _pz);
                    }
                    int goalTri  = FindNearestTriangle(_tx, _tz);

                    if (startTri >= 0 && goalTri >= 0) {
                        // v05.93: Check line-of-sight first. If the walkmesh has
                        // a clear, unobstructed path from player to target (no
                        // dead-end edges, no trigger line crossings), skip A*
                        // and just steer directly. This handles simple cases like
                        // open corridors and straight aisles perfectly.
                        // v06.01: Island connectivity check + A*+funnel pipeline.
                        // 47.5% of FF8 fields have disconnected walkmesh islands.
                        // If the target is on a different island, redirect to the
                        // nearest trigger line that bridges the gap.
                        if (!AreTrianglesConnected(startTri, goalTri)) {
                            // Target on different island. Find the nearest active
                            // trigger line (screen transition) and drive to it.
                            Log::Field("FieldNavigation: [drive] target on different walkmesh island "
                                       "(start tri %d, goal tri %d) — searching for bridge trigger",
                                       startTri, goalTri);
                            float bestTrigDist = 1e30f;
                            int bestTrigIdx = -1;
                            for (int tl = 0; tl < s_capturedLineCount; tl++) {
                                if (!s_capturedLines[tl].active) continue;
                                float tcx = (float)(s_capturedLines[tl].x1 + s_capturedLines[tl].x2) / 2.0f;
                                float tcy = (float)(s_capturedLines[tl].y1 + s_capturedLines[tl].y2) / 2.0f;
                                float tdx = tcx - _px;
                                float tdy = tcy - _pz;
                                float tdist = sqrtf(tdx*tdx + tdy*tdy);
                                // Must be reachable from player's island
                                int trigTri = FindNearestTriangle(tcx, tcy);
                                if (trigTri >= 0 && AreTrianglesConnected(startTri, trigTri)) {
                                    if (tdist < bestTrigDist) {
                                        bestTrigDist = tdist;
                                        bestTrigIdx = tl;
                                    }
                                }
                            }
                            if (bestTrigIdx >= 0) {
                                // Redirect to the trigger line center.
                                float bridgeX = (float)(s_capturedLines[bestTrigIdx].x1 + s_capturedLines[bestTrigIdx].x2) / 2.0f;
                                float bridgeY = (float)(s_capturedLines[bestTrigIdx].y1 + s_capturedLines[bestTrigIdx].y2) / 2.0f;
                                int bridgeTri = FindNearestTriangle(bridgeX, bridgeY);
                                Log::Field("FieldNavigation: [drive] redirecting to trigger line %d "
                                           "center=(%.0f,%.0f) tri=%d dist=%.0f",
                                           bestTrigIdx, bridgeX, bridgeY, bridgeTri, bestTrigDist);
                                // Set up trigger crossing detection.
                                s_driveTrigTarget = true;
                                float tlx1 = (float)s_capturedLines[bestTrigIdx].x1;
                                float tly1 = (float)s_capturedLines[bestTrigIdx].y1;
                                float tlx2 = (float)s_capturedLines[bestTrigIdx].x2;
                                float tly2 = (float)s_capturedLines[bestTrigIdx].y2;
                                float tdx2 = tlx2 - tlx1;
                                float tdy2 = tly2 - tly1;
                                s_driveTrigCrossStart = tdx2 * (_pz - tly1) - tdy2 * (_px - tlx1);
                                // Path to the trigger line.
                                // v06.02: Exempt the bridge trigger from A* avoidance.
                                if (bridgeTri >= 0 && ComputeAStarPath(startTri, bridgeTri, -1, bestTrigIdx)) {
                                    FunnelPath(_px, _pz, bridgeX, bridgeY);
                                }
                                _tx = bridgeX;
                                _tz = bridgeY;
                            } else {
                                Log::Field("FieldNavigation: [drive] no reachable trigger line found, using direct steering");
                                s_waypoints[0][0] = _tx;
                                s_waypoints[0][1] = _tz;
                                s_waypointCount = 1;
                                s_waypointIdx = 0;
                            }
                        } else {
                            // Same island — use A*+funnel.
                            // v06.02: When driving to a trigger line target, exempt
                            // that trigger line from A* avoidance so A* can path
                            // across it. Screen transitions and events are by definition
                            // on or near a trigger line.
                            // v06.04: Extended from ENT_EXIT only to all trigger targets.
                            int driveSkipTrigIdx = -1;
                            if (drTgt.entityIdx <= -200) {
                                driveSkipTrigIdx = -(drTgt.entityIdx + 200);
                                Log::Field("FieldNavigation: [drive] trigger target: exempting trigger line %d from A* avoidance",
                                           driveSkipTrigIdx);
                            }
                            s_driveSkipTrigIdx = driveSkipTrigIdx;  // v06.05: save for recovery
                            if (ComputeAStarPath(startTri, goalTri, drTgt.entityIdx, driveSkipTrigIdx)) {
                                FunnelPath(_px, _pz, _tx, _tz);
                                Log::Field("FieldNavigation: [drive] A*+funnel path: %d waypoints from tri %d to %d",
                                           s_waypointCount, startTri, goalTri);
                            }
                        }
                        // v05.97: Pre-skip waypoints we're already close to at drive start.
                        // The funnel may place the first waypoint near the player's current
                        // position. Without this, the tick-30 chain-advance delay causes
                        // the player to steer away from wp0 before skipping it, creating
                        // a circular orbit as the character tries to return to a passed waypoint.
                        if (s_waypointCount > 1 && s_usingFunnel) {
                            float wpSkipDist = FUNNEL_ARRIVE_DIST * 2.0f;  // generous initial skip
                            while (s_waypointIdx < s_waypointCount - 1) {
                                float wdx = s_waypoints[s_waypointIdx][0] - _px;
                                float wdy = s_waypoints[s_waypointIdx][1] - _pz;
                                float wd = sqrtf(wdx*wdx + wdy*wdy);
                                if (wd >= wpSkipDist) break;
                                Log::Field("FieldNavigation: [drive] pre-skip wp %d (dist=%.0f < %.0f)",
                                           s_waypointIdx, wd, wpSkipDist);
                                s_waypointIdx++;
                            }
                        }
                    } else {
                        Log::Field("FieldNavigation: [drive] A* skipped: start=%d goal=%d",
                                   startTri, goalTri);
                    }
                } else {
                    Log::Field("FieldNavigation: [drive] No walkmesh — straight-line mode");
                }

                // v06.08: Compute and store starting distance for NavLog
                {
                    float sdx = _tx - _px, sdz = _tz - _pz;
                    s_driveStartDist = sqrtf(sdx*sdx + sdz*sdz);
                }

                Log::Field("FieldNavigation: [drive] started toward ent%d gw%d waypoints=%d",
                           drTgt.entityIdx, drTgt.gatewayIdx, s_waypointCount);

                // v06.08: NavLog drive start
                {
                    const char* fld = FF8Addresses::pCurrentFieldName
                                      ? FF8Addresses::pCurrentFieldName : "?";
                    const char* tType = EntityTypeName(drTgt.type);
                    NavLog::DriveStart(fld, drTgt.name, tType,
                                       (int)seedTri, _px, _pz,
                                       -1, _tx, _tz, s_driveArriveDist,
                                       s_corridorCount, s_waypointCount, s_usingFunnel);
                }

                ScreenReader::Speak("Driving.");
            } else {
                // v0.17.5.3: Log the validation failure so the next BAT exposes
                // which target was unreachable and why. Most common cause:
                // target is an NPC whose init script has not yet placed it on
                // the walkmesh, so GetEntityPos returns false for entityIdx.
                // Other failure modes: catalog out-of-range (shouldn't happen
                // with the bounds-checked drTgt above), gateway/JSM index
                // off-by-one, or trigger line index out of range.
                const char* fld = FF8Addresses::pCurrentFieldName
                                  ? FF8Addresses::pCurrentFieldName : "?";
                const char* tType = EntityTypeName(drTgt.type);
                bool playerPosKnown = GetEntityPos(s_playerEntityIdx, _px, _pz);
                bool targetPosKnown = false;
                if (drTgt.entityIdx >= 0 && drTgt.entityIdx < MAX_ENTITIES) {
                    float tmpX, tmpY;
                    targetPosKnown = GetEntityPos(drTgt.entityIdx, tmpX, tmpY);
                }
                Log::Field("FieldNavigation: [drive] REFUSED -- target validation failed: "
                           "field='%s' catIdx=%d/%d entityIdx=%d gatewayIdx=%d "
                           "type=%s name='%s' player_pos_known=%d target_pos_known=%d "
                           "player_entityIdx=%d",
                           fld, s_selectedCatalogIdx, s_catalogCount,
                           drTgt.entityIdx, drTgt.gatewayIdx,
                           tType, drTgt.name,
                           (int)playerPosKnown, (int)targetPosKnown,
                           s_playerEntityIdx);
                ScreenReader::Speak("Target not yet located.");
            }
            }
        }
    }

    s_minusWasDown  = minus;
    s_plusWasDown   = plus;
    s_bkspWasDown   = bksp;
    s_driveWasDown  = drive;
}

// ============================================================================
// Hook: set_current_triangle_sub_45E160
// ============================================================================
//
// Called by the field engine whenever any entity moves to a new walkmesh
// triangle. The three arguments are DIRECT POINTERS to vertex structs (each
// int16_t[3] = x,y,z). We compute the triangle centre and store it for the
// entity whose triangle ID just changed.
//
// Threading note: runs on the game thread. s_entityCenters is read by the
// mod thread (Update/HandleKeys). On x86, 32-bit aligned float stores and
// bool stores are individually atomic, so the worst case is that Update()
// sees an old cx while cz is already updated — a cosmetically stale position
// for one 500ms polling cycle, which is acceptable for compass guidance.

