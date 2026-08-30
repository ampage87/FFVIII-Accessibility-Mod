// field_nav_handlekeys.inl — Key dispatch (navigation, diagnostics, auto-drive toggle)
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

// v0.119.0 (#centra): the mid-height of a captured trigger line -- the Z its
// centre actually sits at. Lives here rather than beside FindNearestTriangle3D
// because the captured-line array does; the chase harness includes the
// pathfinding header without it. See walkmesh_height_lookup_model.inl.
static float CapturedLineMidZ(int t)
{
    if (t < 0 || t >= s_capturedLineCount) return 0.0f;
    return (float)(s_capturedLines[t].z1 + s_capturedLines[t].z2) / 2.0f;
}

// v0.120.0 (#centra): what captured line `t` waits on, 0 if nothing. The scan
// records it per JSM entity; lines are cat 1, group index t (v0.62.2).
static uint16_t LineTouchButtonMask(int t)
{
    for (int j = 0; j < s_jsmEntityCount; j++) {
        if (s_jsmEntities[j].jsmCategory == 1 && s_jsmEntities[j].jsmIndex == t)
            return s_jsmEntities[j].touchButtonMask;
    }
    return 0;
}

// The name the catalog is already using for that line, so the arrival says
// "Left Ladder Up" and not a second, differently-worded label.
static const char* CatalogNameForLine(int t)
{
    for (int c = 0; c < s_catalogCount; c++) {
        if (s_catalog[c].entityIdx == (-200 - t) && s_catalog[c].name[0])
            return s_catalog[c].name;
    }
    return nullptr;
}

// Record the button a drive target waits on, with the label to say beside it.
static void DriveSetButtonTarget(int lineIdx, const char* fallbackName,
                                 const char* leadsTo)
{
    s_driveButtonMask = 0;
    s_driveButtonLabel[0] = '\0';
    s_driveButtonLeadsTo[0] = '\0';
    if (lineIdx < 0) return;
    const uint16_t mask = LineTouchButtonMask(lineIdx);
    if (!BtnMaskIsActionable(mask)) return;
    s_driveButtonMask = mask;
    const char* nm = CatalogNameForLine(lineIdx);
    if (!nm || !nm[0]) nm = fallbackName;
    if (nm && nm[0]) {
        strncpy(s_driveButtonLabel, nm, sizeof(s_driveButtonLabel) - 1);
        s_driveButtonLabel[sizeof(s_driveButtonLabel) - 1] = '\0';
    }
    if (leadsTo && leadsTo[0]) {
        strncpy(s_driveButtonLeadsTo, leadsTo, sizeof(s_driveButtonLeadsTo) - 1);
        s_driveButtonLeadsTo[sizeof(s_driveButtonLeadsTo) - 1] = '\0';
    }
    // A line the player has to press something on is one to stop ON, not 300
    // units past -- and 300 was the non-entity default.
    //
    // v0.121.0 (#centra): 60, not 30. The 2026-08-28 log has the drive parked at
    // **dist=31** against the ladder base with moveDist=0, burning four
    // recoveries and five seconds to close a single unit it could not close --
    // the player had already stopped moving. 60 is the figure this codebase
    // already uses for "close enough to interact" (the talk-radius floor in this
    // same function, and the direction-drive default), and he climbed the ladder
    // from 42 units away in that very log.
    s_driveArriveDist = 60.0f;
    Log::Field("FieldNavigation: [drive] target line%d waits on BTNTEST 0x%04X "
               "('%s') -- arriving on it, not through it [v0.120.0]",
               lineIdx, (unsigned)mask, s_driveButtonLabel);
}

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

    // #minigame-bgbtl: two keys that only do anything around the Garden-battle
    // fight, so they cost nothing anywhere else.
    //
    //   F9   during the briefing -- repeat it. During the fight -- swap the
    //        Block cue between speech and a tone. The reaction window is 700 ms
    //        (measured in the v0.20.102 BAT), which a one-word cue only just
    //        fits, so which cue works is Aaron's call.
    //   F9   SKIP -- during the fight OR on the Game Over screen. Writes exactly
    //        the transition the winning script writes (field 675 ggback1). Same
    //        idea as the Dollet Chase and Timber Train skips. v0.20.107 made it
    //        available mid-fight on Aaron's request; the briefing announces it.
    //
    // v0.20.103: WAS F7, WHICH IS GameAudio::VolumeDown IN dinput8.cpp -- the
    // v0.20.102 BAT log shows both firing on one press ("Block cue: tone." then
    // "Music volume 30 percent"). F9 and F10 were the only unbound function
    // keys and both are now spoken for.
    {
        // v0.20.121, at Aaron's request and for two good reasons:
        //   * the SKIP moved F10 -> F9. "F10 is used by the Windows system
        //     menu", and in a windowed build that is a real conflict.
        //   * REPEAT/TOGGLE moved F9 -> Space, and CONFIRM moved X -> Enter
        //     (handled inside GardenBattle::Update), because X is mask 64 --
        //     the kick. The key that dismissed the Game Controls screen was one
        //     of the four keys the screen exists to teach.
        static bool s_f9Was = false, s_spaceWas = false;
        const bool f9    = (GetAsyncKeyState(VK_F9)    & 0x8000) != 0;
        const bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        const uint16_t fid = FF8Addresses::pCurrentFieldId
                           ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        if (space && !s_spaceWas) {
            if (fid == GardenBattle::FIELD_MINIGAME || GardenBattle::IsArmed())
                GardenBattle::ToggleCueMode();
        }
        if (f9 && !s_f9Was) {
            if (GardenBattle::OnGameOverScreen(fid) || GardenBattle::IsArmed())
                GardenBattle::SkipToVictory();
        }
        s_f9Was = f9; s_spaceWas = space;
    }

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
    // v0.18.3.236 (#72): consume a one-shot post-battle resume request as a
    // synthetic backslash press. PollBattlePauseResume (field_navigation.cpp)
    // arms it only after the field is back, the player position has settled,
    // and the paused catalog target was re-located, so the normal start path
    // below (validation included) runs unchanged.
    bool driveResume = s_driveResumeRequest;
    s_driveResumeRequest = false;

    if ((drive && !s_driveWasDown) || driveResume) {
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
        } else if (strcmp(s_camAxesSource, "identity") == 0 && !s_camAxesEmpiricalApplied) {
            // v0.17.7.6.2: Camera axes haven't been calibrated yet on this
            // field. The CA file's 2D projection was degenerate (e.g.
            // bgroad_5's axis1=(0,0,-4096), entirely in depth) so the mod
            // fell back to identity defaults that don't match the engine's
            // actual screen-to-world mapping on this field.
            //
            // The previous v0.17.7.6.1 design let AD start and tried to
            // seed calibration from AD's own injected keys. That works only
            // if AD's wrong-direction injection produces measurable
            // movement -- BUT bgroad_5's wrong direction (east) pushed the
            // player straight into a wall, producing moveDist=0 for the
            // entire drive duration. With no movement, the observer's
            // 50-unit gate filters out all samples, calibration never
            // fires, AD thrashes through recovery phases and gives up.
            //
            // v0.17.7.6.2's fix: refuse AD start with a TTS message that
            // tells Aaron what to do. He walks an arrow briefly (which
            // does produce movement, since he's pressing keys he KNOWS
            // align with the field's visible geometry), the observer
            // samples it, calibration fires after 2 samples, and a TTS
            // confirmation announces "Camera calibrated." Aaron then
            // retries AD and it works with correct axes.
            //
            // This gate ONLY affects degenerate-CA fields with pending
            // calibration. On CA-valid fields (source="ca-quantized") the
            // strcmp returns non-zero and AD starts as before. After
            // calibration applies on a degenerate field, the lock flag
            // (s_camAxesEmpiricalApplied) becomes true and the gate stops
            // firing for the remainder of that field load.
            ScreenReader::Speak("Camera not yet calibrated. Press an arrow key briefly to calibrate, then try again.");
            Log::Field("FieldNavigation: F9 drive REFUSED (camera axes not yet calibrated: source=identity, pending empirical correction)");
        } else {
            // Validate we have a usable target before starting.
            const EntityInfo& drTgt = (s_selectedCatalogIdx < s_catalogCount)
                                      ? s_catalog[s_selectedCatalogIdx] : s_catalog[0];
            {
            float _px = 0, _pz = 0, _tx = 0, _tz = 0;
            // v0.119.0 (#centra): the target's HEIGHT, when the target is a kind
            // that has one. Only trigger lines and INF gateways do.
            float _goalZ = 0.0f; bool _haveGoalZ = false;
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
                RecoveryRingClear();             // v0.18.3.307 (#100): forget the previous drive's stuck-triangle ring
                s_drivePinnedCount = 0;          // v0.18.3.309 (#114): fresh pin-escape state per drive
                s_drivePinnedPosX  = _px;
                s_drivePinnedPosY  = _pz;
                DriveProgressResetForNewDrive(_px, _pz);  // v0.131.1 (#centra)

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
                } else if (drTgt.type == ENT_EXIT && drTgt.gatewayIdx < 0 &&
                           drTgt.entityIdx <= -300) {
                    // v0.62.1 (#123): a SCRIPTED exit -- a lift platform, a
                    // trapdoor, a bus. It has no gateway line to cross and no
                    // talk radius to stop at, so it fell to the 300-unit
                    // non-entity default and auto-drive announced "Arrived" a
                    // room away from it. You get onto a lift the same way you
                    // get onto a save point: by standing on it.
                    s_driveArriveDist = 30.0f;
                    Log::Field("FieldNavigation: [drive] scripted exit '%s' -> arriveDist=30 "
                               "(walk-onto) [v0.62.1]", drTgt.name);
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
                // v0.120.0 (#centra): and with no button to press.
                s_driveButtonMask = 0; s_driveButtonLabel[0] = '\0';
                s_driveButtonLeadsTo[0] = '\0';
                // v0.118.0 (#centra): every drive starts with no bridge redirect.
                s_driveBridgeActive  = false;
                s_driveBridgeLineIdx = -1;
                s_driveBridgeX = 0.0f; s_driveBridgeY = 0.0f;
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
                            // v0.119.0 (#centra): the gateway's own height. The 2D
                            // lookup put crtower2's exit to crtower3 on the right-
                            // ladder platform (tri 73, z=14060) instead of the floor
                            // under it (tri 91, z=14984) and invented an island
                            // crossing. walkmesh_height_lookup_model.inl.
                            _goalZ = (float)(s_gateways[bestRawIdx].lineZ1 +
                                             s_gateways[bestRawIdx].lineZ2) / 2.0f;
                            _haveGoalZ = true;
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
                        // v0.119.0 (#centra): SETLINE has carried z1/z2 since v05.58.
                        _goalZ = CapturedLineMidZ(trigIdx);
                        _haveGoalZ = true;
                        s_driveTrigTarget = true;
                        DriveSetButtonTarget(trigIdx, drTgt.name, nullptr);
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
                    // v0.131.6 (#centra): AN ENTITY KNOWS WHICH TRIANGLE IT IS
                    // STANDING ON -- ASK IT INSTEAD OF SEARCHING FOR ONE.
                    //
                    // Aaron: "an earlier field where you have to walk onto an
                    // automated lift, but the lift was identified as an NPC and
                    // auto-drive wouldn't actually trigger it." crsphi1, the
                    // 21:01:06 drive, and the log names the failure exactly:
                    // "startTri 71 and goalTri 122 are on disconnected walkmesh
                    // islands; straight-line fallback", then "A* No path from
                    // tri 64 to tri 122", and it parked him 400 units short.
                    //
                    // THE LIFT IS FOUR TRIANGLES. crsphi1 has five walkmesh
                    // islands and four of them share one XY footprint at
                    // different heights -- tri 131 at z 942, tri 149 at 1561,
                    // tri 122 at 2231, tri 140 at 2949. That is the lift drawn
                    // once per stop. A coordinate search over (-4,-206) can
                    // return any of them, and it returned the one two thousand
                    // units up, on an island the player cannot reach. tri 131
                    // is on the player's own island and was always walkable.
                    //
                    // The mod already had the right answer and threw it away:
                    // the catalog reads the entity's live triangle every refresh
                    // and logged it in the same second -- "[dedup] ent3 tri=131".
                    // The engine's own statement of where a thing is standing
                    // beats any search over its coordinates, and it costs a read.
                    int goalTri = -1;
                    if (DriveGoalTriFromEntity(drTgt.entityIdx, drTgt.triangleId,
                                               s_walkmesh.numTriangles, &goalTri)) {
                        Log::Field("FieldNavigation: [drive] goal tri %d from the "
                                   "entity's own placement, not a coordinate search "
                                   "[v0.131.6]", goalTri);
                    } else {
                        goalTri = _haveGoalZ ? FindNearestTriangle3D(_tx, _tz, _goalZ)
                                             : FindNearestTriangle(_tx, _tz);
                    }

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
                                int trigTri = FindNearestTriangle3D(tcx, tcy,
                                                                    CapturedLineMidZ(tl));
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
                                int bridgeTri = FindNearestTriangle3D(bridgeX, bridgeY,
                                                    CapturedLineMidZ(bestTrigIdx));
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
                                // v0.118.0 (#centra): the crossing line has to be the
                                // BRIDGE's, not the gateway's. A gateway target seeded
                                // s_driveCrossLine* from the gateway's own endpoints a
                                // few dozen lines above; if the drive is actually
                                // heading for a ladder 1400 units away from that line,
                                // "have we crossed yet" was asking about the wrong line
                                // -- and so was the 300-unit push applied to the target.
                                s_driveCrossLineX1 = s_capturedLines[bestTrigIdx].x1;
                                s_driveCrossLineY1 = s_capturedLines[bestTrigIdx].y1;
                                s_driveCrossLineX2 = s_capturedLines[bestTrigIdx].x2;
                                s_driveCrossLineY2 = s_capturedLines[bestTrigIdx].y2;
                                s_driveCrossLineActive = true;
                                // v0.118.0 (#centra): AND THE TICK LOOP HAS TO KNOW.
                                // v06.01 wrote the bridge into _tx/_tz, which are locals
                                // here; UpdateAutoDrive re-derives its target from the
                                // catalog index every tick and so drove at the gateway
                                // it could not reach. See drive_bridge_target_model.inl.
                                s_driveBridgeActive  = true;
                                s_driveBridgeLineIdx = bestTrigIdx;
                                s_driveBridgeX = bridgeX;
                                s_driveBridgeY = bridgeY;
                                // v0.120.0 (#centra): the bridge is usually a ladder,
                                // and a ladder is a button press. The drive walks to
                                // it and hands over rather than pushing through it.
                                DriveSetButtonTarget(bestTrigIdx, nullptr, drTgt.name);
                                // Path to the trigger line.
                                // v06.02: Exempt the bridge trigger from A* avoidance.
                                // v0.118.0: a bridge inside the player's OWN triangle
                                // needs no path -- A* from a triangle to itself is a
                                // degenerate query, and on crtower2 (player tri 37,
                                // bridge tri 37) its funnelled output steered 1149 units
                                // the wrong way. One waypoint, straight at it.
                                if (BridgeNeedsNoPath(startTri, bridgeTri)) {
                                    s_waypoints[0][0] = bridgeX;
                                    s_waypoints[0][1] = bridgeY;
                                    s_waypointCount = 1;
                                    s_waypointIdx = 0;
                                    s_usingFunnel = false;
                                    Log::Field("FieldNavigation: [drive] bridge is in the player's own "
                                               "triangle (%d) -- steering straight at it [v0.118.0]",
                                               startTri);
                                } else if (bridgeTri >= 0 &&
                                           ComputeAStarPath(startTri, bridgeTri, -1, bestTrigIdx)) {
                                    FunnelPath(_px, _pz, bridgeX, bridgeY);
                                } else {
                                    s_waypoints[0][0] = bridgeX;
                                    s_waypoints[0][1] = bridgeY;
                                    s_waypointCount = 1;
                                    s_waypointIdx = 0;
                                    s_usingFunnel = false;
                                    Log::Field("FieldNavigation: [drive] no A* route to the bridge "
                                               "(tri %d -> %d) -- steering straight at it [v0.118.0]",
                                               startTri, bridgeTri);
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

                            // v0.132.0 (#shumi): the same exemption for INF
                            // gateways, which A* now treats as walls. Without it a
                            // drive TO a doorway would plan a route that avoids
                            // the doorway and report no path. drTgt.gatewayIdx is
                            // already the catalog's own index into s_gateways[].
                            s_driveSkipGatewayIdx = drTgt.gatewayIdx;
                            if (s_driveSkipGatewayIdx >= 0)
                                Log::Field("FieldNavigation: [drive] gateway target: exempting "
                                           "gateway %d from A* avoidance -- every OTHER gateway "
                                           "on this field is a wall [v0.132.0]",
                                           s_driveSkipGatewayIdx);

                            // v0.17.9.16.2: bggate_6 front-gate TURNSTILE slot
                            // selection. The gate is a closed walkmesh loop with
                            // two offset one-way lanes -- WEST lane = IN/up to the
                            // B-Garden Hall, EAST lane = OUT/down the gate path --
                            // and the turnstile collision separating them is not in
                            // the walkmesh, so plain A* picks the shorter/wrong lane
                            // and the party wedges (confirmed by the [TTRACE] manual
                            // walk: IN goes straight up X=-1312, OUT goes right then
                            // down X=-1093). When the route crosses the turnstile
                            // band (between the two interaction-line "bars" at
                            // Y=-428 and Y=-907), force a via triangle in the correct
                            // lane's mid-band, chosen by travel direction. F9-only,
                            // bggate_6-only -- every other field/drive is unchanged.
                            int viaTri = -1;
                            if (FF8Addresses::pCurrentFieldId &&
                                *FF8Addresses::pCurrentFieldId == 0x00A3) {
                                const float TURNSTILE_MID_Y = -667.0f;  // midway between the two bars
                                bool startSouth = (_pz < TURNSTILE_MID_Y);
                                bool goalSouth  = (_tz < TURNSTILE_MID_Y);
                                if (startSouth != goalSouth) {
                                    if (!goalSouth) {
                                        // goal north of start -> IN -> WEST lane
                                        viaTri = FindNearestTriangle(-1312.0f, -532.0f);
                                    } else {
                                        // goal south of start -> OUT -> EAST lane
                                        viaTri = FindNearestTriangle(-1093.0f, -586.0f);
                                    }
                                    Log::Field("FieldNavigation: [drive] bggate_6 turnstile: "
                                               "start=(%.0f,%.0f) goal=(%.0f,%.0f) dir=%s -> via lane tri %d",
                                               _px, _pz, _tx, _tz,
                                               goalSouth ? "OUT/east" : "IN/west", viaTri);
                                }
                            }

                            bool pathOk = false;
                            if (viaTri >= 0 && viaTri != startTri && viaTri != goalTri) {
                                pathOk = ComputeAStarPathVia(startTri, viaTri, goalTri,
                                                             drTgt.entityIdx, driveSkipTrigIdx);
                                if (!pathOk) {
                                    Log::Field("FieldNavigation: [drive] bggate_6 via-lane path failed; "
                                               "falling back to direct A*");
                                }
                            }
                            if (!pathOk) {
                                pathOk = ComputeAStarPath(startTri, goalTri, drTgt.entityIdx, driveSkipTrigIdx);
                            }
                            if (pathOk) {
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

                // v0.119.0 (#centra): the recovery re-paths inside UpdateAutoDrive
                // re-derive the goal triangle too, and must use the same height.
                s_driveGoalZ      = _goalZ;
                s_driveGoalZValid = _haveGoalZ;

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

