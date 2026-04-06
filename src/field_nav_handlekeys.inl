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
    // v05.69: F11 = VISDIAG dump
    bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;

    if (f11 && !s_f11WasDown) {
        // Dump candidate visibility bytes for all model-bearing entities.
        __try {
            uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
            uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base && entCount > 0) {
                uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;
                Log::Field("FieldNavigation: [VISDIAG] === Visibility flag dump ===");
                for (int i = 0; i < (int)lim; i++) {
                    uint8_t* blk = base + ENTITY_STRIDE * i;
                    int16_t  mdl = *(int16_t*)(blk + 0x218);
                    // Dump a wide range of candidate offsets as hex.
                    // Flags area: 0x240-0x25F covers talk/push/through/setpc
                    // and likely the SHOW/HIDE visibility flag.
                    // Also dump 0x00-0x0F (early control bytes) and 0x160-0x16F (exec flags).
                    // Dump regions covering the gaps in ff8_field_state_other:
                    // gap1 (0x188-0x1F9): 114 bytes after ff8_field_state_common
                    // gap2 (0x21A-0x248): 47 bytes after model_id
                    // flags area (0x240-0x25F): talkon/pushon/throughon/setpc
                    char hexGap1a[80] = {}; // 0x188..0x19F (24 bytes, needs 24*3+1=73)
                    char hexGap1b[80] = {}; // 0x1A0..0x1B7 (24 bytes)
                    char hexGap2[80] = {};  // 0x21A..0x231 (24 bytes)
                    char hexFlags[80] = {}; // 0x240..0x257 (24 bytes)
                    for (int b = 0; b < 24; b++)
                        snprintf(hexGap1a + b*3, 4, "%02X ", blk[0x188 + b]);
                    for (int b = 0; b < 24; b++)
                        snprintf(hexGap1b + b*3, 4, "%02X ", blk[0x1A0 + b]);
                    for (int b = 0; b < 24; b++)
                        snprintf(hexGap2 + b*3, 4, "%02X ", blk[0x21A + b]);
                    for (int b = 0; b < 24; b++)
                        snprintf(hexFlags + b*3, 4, "%02X ", blk[0x240 + b]);
                    Log::Field("FieldNavigation: [VISDIAG] ent%d model=%d %s",
                               i, (int)mdl, (i == s_playerEntityIdx) ? "[PLAYER]" : "");
                    Log::Field("FieldNavigation: [VISDIAG]   @0x188: %s", hexGap1a);
                    Log::Field("FieldNavigation: [VISDIAG]   @0x1A0: %s", hexGap1b);
                    Log::Field("FieldNavigation: [VISDIAG]   @0x21A: %s", hexGap2);
                    Log::Field("FieldNavigation: [VISDIAG]   @0x240: %s", hexFlags);
                }
                Log::Field("FieldNavigation: [VISDIAG] === End dump ===");
                ScreenReader::Speak("Visibility diagnostic logged.");
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Field("FieldNavigation: [VISDIAG] Exception");
        }
    }
    s_f11WasDown = f11;

    // v0.12.13: F12 = Interaction Range Diagnostic.
    // Dumps RUNTIME talkRadius, pushRadius, position, and distance from player
    // for ALL entities on the field. This reveals the engine-set interaction
    // ranges that the static JSM scan can't see.
    static bool s_f12WasDown = false;
    bool f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    if (f12 && !s_f12WasDown) {
        if (s_playerEntityIdx >= 0 && FF8Addresses::pFieldStateOthers) {
            __try {
                uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
                uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                if (base && entCount > 0) {
                    uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;
                    float px = 0, py = 0;
                    GetEntityPos(s_playerEntityIdx, px, py);
                    const char* fieldName = FF8Addresses::pCurrentFieldName
                                            ? FF8Addresses::pCurrentFieldName : "?";
                    Log::Field("FieldNavigation: [INTERACT-DIAG] === %s === player=ent%d pos=(%.0f,%.0f)",
                               fieldName, s_playerEntityIdx, px, py);
                    for (int i = 0; i < (int)lim; i++) {
                        uint8_t* blk = base + ENTITY_STRIDE * i;
                        int16_t  modelId      = *(int16_t*)(blk + 0x218);
                        uint16_t triId        = *(uint16_t*)(blk + 0x1FA);
                        uint16_t talkRad      = *(uint16_t*)(blk + 0x1F8);
                        uint16_t pushRad      = *(uint16_t*)(blk + 0x1F6);
                        uint8_t  talkonoff    = *(blk + 0x24B);
                        uint8_t  pushonoff    = *(blk + 0x249);
                        uint8_t  throughonoff = *(blk + 0x24C);
                        uint8_t  setpc        = *(blk + 0x255);
                        int32_t  fpX          = *(int32_t*)(blk + 0x190);
                        int32_t  fpY          = *(int32_t*)(blk + 0x194);
                        float ex = (float)(fpX / 4096);
                        float ey = (float)(fpY / 4096);
                        float dx = ex - px;
                        float dy = ey - py;
                        float dist = sqrtf(dx*dx + dy*dy);
                        // SYM name
                        const char* sym = "";
                        int si = s_symOthersOffset + i;
                        if (si >= 0 && si < s_symNameCount) sym = s_symNames[si];
                        Log::Field("FieldNavigation: [INTERACT-DIAG] ent%d '%s' model=%d "
                                   "pos=(%.0f,%.0f) dist=%.0f tri=0x%04X "
                                   "talkRad=%u pushRad=%u talk=%d push=%d thru=%d setpc=%d%s",
                                   i, sym, (int)modelId, ex, ey, dist, (unsigned)triId,
                                   (unsigned)talkRad, (unsigned)pushRad,
                                   (int)talkonoff, (int)pushonoff, (int)throughonoff, (int)setpc,
                                   (i == s_playerEntityIdx) ? " [PLAYER]" : "");
                    }
                    Log::Field("FieldNavigation: [INTERACT-DIAG] === END Others ===");
                    // Also dump Backgrounds array.
                    if (FF8Addresses::HasFieldStateBackgrounds()) {
                        uint8_t bgCount = *FF8Addresses::pFieldStateBackgroundCount;
                        uint8_t* bgBase = reinterpret_cast<uint8_t*>(
                            *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                        if (bgBase && bgCount > 0) {
                            uint8_t bgLim = (bgCount < MAX_BG_ENTITIES) ? bgCount : (uint8_t)MAX_BG_ENTITIES;
                            Log::Field("FieldNavigation: [INTERACT-DIAG] === Backgrounds: %d ===", (int)bgLim);
                            for (int b = 0; b < (int)bgLim; b++) {
                                uint8_t* blk = bgBase + BG_STRIDE * b;
                                // BG struct is 0x1B4 — smaller than Others (0x264).
                                // Common offsets (from ff8_field_state_common) should be same:
                                // 0x190=fpX, 0x194=fpY, but interaction offsets may differ.
                                // Dump what we can safely read within 0x1B4.
                                int32_t fpX = *(int32_t*)(blk + 0x190);
                                int32_t fpY = *(int32_t*)(blk + 0x194);
                                float bx = (float)(fpX / 4096);
                                float by = (float)(fpY / 4096);
                                float bdx = bx - px;
                                float bdy = by - py;
                                float bdist = sqrtf(bdx*bdx + bdy*bdy);
                                uint32_t execFlags = *(uint32_t*)(blk + 0x160);
                                // BG struct ends at 0x1B4, so offsets like 0x1F8 (talkRad)
                                // and 0x218 (model) are OUT OF BOUNDS for BG structs.
                                // Dump the post-common region (0x188..0x1B3) as hex.
                                char hexTail[140] = {};
                                for (int h = 0; h < 44 && h < (int)(BG_STRIDE - 0x188); h++)
                                    snprintf(hexTail + h*3, 4, "%02X ", blk[0x188 + h]);
                                // SYM name: BG entities are at SYM[jsmLines + b]
                                const char* bgSym = "";
                                int bgSymIdx = s_jsmLines + b;
                                if (bgSymIdx >= 0 && bgSymIdx < s_symNameCount)
                                    bgSym = s_symNames[bgSymIdx];
                                Log::Field("FieldNavigation: [INTERACT-DIAG] bg%d '%s' "
                                           "pos=(%.0f,%.0f) dist=%.0f exec=0x%X",
                                           b, bgSym, bx, by, bdist, execFlags);
                                Log::Field("FieldNavigation: [INTERACT-DIAG]   @0x188: %s", hexTail);
                            }
                            Log::Field("FieldNavigation: [INTERACT-DIAG] === END Backgrounds ===");
                        }
                    }
                    ScreenReader::Speak("Interaction diagnostic logged.");
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("FieldNavigation: [INTERACT-DIAG] Exception");
            }
        }
    }
    s_f12WasDown = f12;

    // v0.12.13: Animation scan polling REMOVED — replaced by interaction range diagnostic.
    // Old ANIMSCAN code removed. Static variables (s_animScanActive etc.) still declared
    // above but no longer used.

    // v05.86: Arrow keys cancel auto-drive immediately.
    // The player pressing any direction key means they want manual control.
    // We must only cancel if the arrow key is NOT one we're currently injecting
    // via SetHeldDirections. Check which keys are held by us (s_driveHeld bitmask)
    // and only cancel on keys we're NOT injecting.
    if (s_driveActive) {
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
            StopAutoDrive("Cancelled.");
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

                // v06.14: Start heading calibration if not yet calibrated for this field.
                if (s_calibPending && !s_camCalibrated) {
                    s_calibPhase = 1;
                    s_calibTicks = 0;
                    Log::Field("FieldNavigation: [CALIB] starting heading calibration for field '%s'",
                               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?");
                } else {
                    s_calibPhase = 3;  // skip calibration, use existing axes
                }

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
                s_driveArriveDist = DRIVE_ARRIVE_DIST_DEFAULT;
                s_driveTargetEntityIdx = -1;
                s_driveOrigTalkRadius = 0;
                s_driveTalkRadExpanded = false;
                // v0.07.76: Save/draw points require walking INTO the spot, not stopping nearby.
                // These are walk-on triggers with no talk radius — arrive distance must be tiny.
                if (drTgt.type == ENT_SAVE_POINT || drTgt.type == ENT_DRAW_POINT) {
                    s_driveArriveDist = 30.0f;
                    Log::Field("FieldNavigation: [drive] %s target -> arriveDist=30 (walk-into)",
                               EntityTypeName(drTgt.type));
                } else if (drTgt.entityIdx >= 0 && drTgt.entityIdx < MAX_ENTITIES) {
                    uint16_t talkRad = GetEntityTalkRadius(drTgt.entityIdx);
                    if (talkRad > 0) {
                        s_driveArriveDist = (float)talkRad;
                        if (s_driveArriveDist < 60.0f) s_driveArriveDist = 60.0f;
                        // v06.21: Save original talk radius for potential expansion.
                        s_driveTargetEntityIdx = drTgt.entityIdx;
                        s_driveOrigTalkRadius = talkRad;
                        Log::Field("FieldNavigation: [drive] talkRadius=%u -> arriveDist=%.0f",
                                   (unsigned)talkRad, s_driveArriveDist);
                    }
                }

                // v05.76: Track trigger line crossing for arrival detection.
                s_driveTrigTarget = false;
                s_driveTrigCrossStart = 0.0f;
                if (drTgt.entityIdx <= -200) {
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

