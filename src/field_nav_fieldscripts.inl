// field_nav_fieldscripts.inl — HookedFieldScriptsInit — per-field setup
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// Hook: field_scripts_init
// ============================================================================

static int __cdecl HookedFieldScriptsInit(int unk1, int unk2, int unk3, int unk4)
{
    // Reset all per-field state BEFORE calling the original.
    // The original function calls set_current_triangle for every entity during
    // field load, which populates s_entityCenters via HookedSetCurrentTriangle.
    // If we reset AFTER the original call we wipe those freshly-seeded centers.
    memset(s_prevTriangles, 0, sizeof(s_prevTriangles));
    memset(s_changeScore,   0, sizeof(s_changeScore));
    memset(s_entityCenters, 0, sizeof(s_entityCenters));
    memset(s_triCenter,     0, sizeof(s_triCenter));
    memset(s_hookPrevTri,   0, sizeof(s_hookPrevTri));
    s_driveLastTriId    = 0xFFFF;
    s_driveStuckTicks  = 0;
    s_driveWiggleTicks = 0;
    s_driveWiggleDir   = DIR_LEFT;
    s_playerEntityIdx    = -1;
    s_symNameCount       = 0;
    s_symOthersOffset    = 0;
    s_jsmDoors           = 0;
    s_jsmLines           = 0;
    s_jsmBackgrounds     = 0;
    s_jsmOthers          = 0;
    s_gatewayCount       = 0;
    s_dedupGatewayCount  = 0;
    s_triggerCount       = 0;
    s_capturedLineCount  = 0;
    s_waypointCount      = 0;
    s_waypointIdx        = 0;
    s_usingFunnel        = false;  // v05.95
    // v0.15.9.2.6: reset cluster state (will be repopulated by the dead-end scanner below)
    s_deadClusterCount   = 0;
    // Free previous walkmesh before loading new one.
    FieldArchive::FreeWalkmesh(s_walkmesh);
    // Reset camera axes for new field.
    s_cameraAxes = {};
    // v0.12.02: Reset GPS guidance on field change.
    s_gpsActive = false;
    s_gpsCatalogIdx = -1;
    memset(s_symNames,   0, sizeof(s_symNames));
    memset(s_gateways,   0, sizeof(s_gateways));
    memset(s_triggers,   0, sizeof(s_triggers));
    memset(s_capturedLines, 0, sizeof(s_capturedLines));
    s_catalogCount       = 0;
    s_jsmEntityCount     = 0;
    s_playerTri          = 0xFFFF;
    s_setTriCallCount    = 0;
    s_structFallbackLogged = 0;
    // v05.58: ENTDIAG/BGDIAG disabled — keep flags true to skip dumps.
    s_entDiagDumped      = true;
    s_bgDiagDumped       = true;
    s_coordDiagDumped    = true;   // v0.12.11: DISABLED — coordinate diagnostic served its purpose
    s_partyDiagDumped    = false;  // v0.14.107: re-arm the [party-state] dump for this field load
    s_coordPrevPlayerTri = 0;       // v06.13: reset for shared-edge CoordSample
    // v06.14: Reset heading calibration for new field.
    //
    // v0.17.2: Reset BOTH axis pairs (manual nav + auto-drive private) and the
    // source tag. The CA-loaded block further down will overwrite both pairs
    // and set s_camAxesSource = "ca-file" when a .ca file is parsed.
    s_camRightX = 1.0f; s_camRightY = 0.0f;
    s_camDownX  = 0.0f; s_camDownY  = -1.0f;
    s_driveCamRightX = 1.0f; s_driveCamRightY = 0.0f;
    s_driveCamDownX  = 0.0f; s_driveCamDownY  = -1.0f;
    s_camAxesSource  = "identity";
    // v0.17.7.6: Reset closed-loop empirical calibration accumulator.
    // The buffer holds NAV-OBSERVE samples that drive the empirical
    // camera-axes correction on degenerate-CA fields. Clearing on every
    // field load means samples from one field never bleed into another's
    // consensus check, and the one-shot lock re-arms so the new field's
    // first valid sample run can apply a fresh correction if needed.
    memset(s_navObsBuffer, 0, sizeof(s_navObsBuffer));
    memset(s_navObsSampleCount, 0, sizeof(s_navObsSampleCount));
    s_camAxesEmpiricalApplied = false;
    s_camCalibrated = false;
    s_calibPhase = 0;
    s_calibPending = true;  // calibrate on first drive
    s_projDiagCount      = 0;       // v06.13: reset projection diagnostic
    s_projDiagPrevFpX    = 0;
    s_projDiagPrevFpY    = 0;
    s_cycleIdx           = 0;
    s_selectedCatalogIdx = 0;
    s_nonPlayerCount     = 0;
    // Stop auto-drive on field transition — releases held keys.
    if (s_driveActive) StopAutoDrive(nullptr);
    s_driveActive        = false;
    s_driveHeld          = 0;

    // v0.08.03: Enable SET3 capture so HookedSet3 records positions during init.
    // v0.08.26: PERSISTENT — stays true forever, no time window. Resets capture array
    // on field change so entity addresses from the old field don't linger.
    s_set3CaptureCount = 0;
    s_set3TotalCalls = 0;
    s_capturingSET3 = true;
    s_set3CaptureStartTime = GetTickCount();
    s_set3SummaryLogged = false;
    memset(s_set3Captures, 0, sizeof(s_set3Captures));
    s_pshmCaptureCount = 0;
    s_capturingPSHM = true;
    s_pshmCaptureStartTime = GetTickCount();
    s_pshmSummaryLogged = false;

    // v0.14.45: POPM varblock write capture reset block removed (F12 diagnostic retired).

    int ret = s_originalFieldScriptsInit(unk1, unk2, unk3, unk4);

    // v0.08.26: SET3 capture stays active PERMANENTLY (persistent hook).
    // Per-frame scripts (like dic on bghall_1) fire SET3 after init returns.
    // Note: s_capturingPSHM stays true — time-based window (5s) handles auto-off.
    // PSHM_W-using entities fire in per-frame scripts (method 1+), not during init.

    Log::Field("FieldNavigation: [PSHM_W-HOOK] Init done, %d PSHM_W during init. Capture window open for %ums.",
               s_pshmCaptureCount, PSHM_CAPTURE_DURATION_MS);

    // s_entityCenters now contains centers for every entity that fired
    // set_current_triangle during load — including stationary NPCs.

    __try {
        uint16_t    fieldId   = FF8Addresses::pCurrentFieldId
                                ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        const char* fieldName = FF8Addresses::pCurrentFieldName
                                ? FF8Addresses::pCurrentFieldName : "(unknown)";
        uint8_t     entCount  = FF8Addresses::pFieldStateOtherCount
                                ? *FF8Addresses::pFieldStateOtherCount : 0;

        Log::Field("FieldNavigation: [fieldload] id=%u name='%s' entities=%u",
                   (unsigned)fieldId, fieldName, (unsigned)entCount);

        // v05.41: Detect player entity only. Catalog is built on-demand
        // when the user presses -/= to cycle, via RefreshCatalog().
        // This avoids picking up placeholder entities from opening sequences.
        if (FF8Addresses::pFieldStateOthers && entCount > 0) {
            uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base) {
                uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;
                for (int i = 0; i < (int)lim; i++) {
                    uint8_t  setpc = *(base + ENTITY_STRIDE * i + 0x255);
                    // v0.17.8.17.1: Accept any valid character ID (0..10), not
                    // just `setpc == 0`. The old check matched only Squall (ID 0)
                    // and so failed on Laguna dream fields like gwgrass1 where
                    // the playable entity has setpc=8 (Laguna), 9 (Kiros), or
                    // 10 (Ward). The first entity with a character ID is the
                    // player; NPCs use the sentinel 254 (0xFE) and are excluded
                    // by the < 11 bound. Confirmed by the v0.17.8.17 BAT
                    // [LAGU-FLD] block on gwgrass1: ent0/1/2 had setpc=8/9/10
                    // (Laguna/Kiros/Ward), ent3/4 had setpc=254 (NPCs), and the
                    // old heuristic found nothing. Regular fields still work
                    // because Squall's ID is 0, which satisfies `< 11`.
                    if (setpc < 11) {
                        s_playerEntityIdx = i;
                        break;
                    }
                }
                Log::Field("FieldNavigation: [fieldload] player=ent%d", s_playerEntityIdx);
            }
        }

        // v05.48: Load SYM entity names, JSM counts, and INF gateways from archive.
        if (FieldArchive::IsReady() && fieldName && fieldName[0] != '(') {
            FieldArchive::LoadSYMNames(fieldName, s_symNames, MAX_SYM_NAMES, s_symNameCount);
            // v0.07.94: INF gateway loading RE-ENABLED with corrected Deling format.
            // INF gateways are the engine's native exit mechanism for many fields
            // (e.g. bghall_1 hallway exits) that don't use script-level MAPJUMP.
            FieldArchive::LoadINFGateways(fieldName, s_gateways, MAX_GATEWAYS, s_gatewayCount);

            // v05.49: SYM offset = 0.
            // CONFIRMED by ENTDIAG: the entity state array maps 1:1 to the
            // first N SYM names. SYM excludes doors, and the runtime entity
            // array corresponds directly to the non-door SYM entries in order.
            // Entity state index i = SYM[i].
            s_symOthersOffset = 0;

            // Still load JSM counts for diagnostic logging.
            FieldArchive::JSMCounts jsmCounts = {};
            FieldArchive::LoadJSMCounts(fieldName, jsmCounts);
            s_jsmDoors       = jsmCounts.doors;
            s_jsmLines       = jsmCounts.lines;
            s_jsmBackgrounds = jsmCounts.backgrounds;
            s_jsmOthers      = jsmCounts.others;

            // v05.54: Load INF trigger zones.
            FieldArchive::LoadINFTriggers(fieldName, s_triggers, MAX_TRIGGERS, s_triggerCount);

            // v05.62: Load walkmesh for A* pathfinding.
            FieldArchive::LoadWalkmesh(fieldName, s_walkmesh);

            // v0.12.01: Load camera axes for screen-space direction mapping.
            // The .ca file contains the camera orientation that defines how
            // 3D walkmesh coordinates project to 2D entity/screen space.
            s_cameraAxes = {};
            FieldArchive::LoadCameraAxes(fieldName, s_cameraAxes);

            // v0.17.0: Wire the .ca-derived camera axes into s_camRightX/Y/DownX/DownY
            // so all manual-nav direction code paths (GPS guided navigation,
            // FormatNavComponents-based Backspace announce, F9/F10 catalog cycling)
            // get screen-relative directions on EVERY field, not just chase fields
            // where the empirical chase-auto-pilot calibration happens to run.
            //
            // .ca format: 3 axis vectors stored as int16 fixed-point (/4096 to normalize).
            //   axis0 = screen-right direction in world XY basis
            //   axis1 = screen-down direction  in world XY basis
            //   axis2 = screen-forward (depth) — unused for direction labels
            // Confirmed by default-camera fields where axis0=(1.000,0.000,0.000) and
            // axis1=(0.000,-1.000,0.000) — exactly the identity defaults reset above.
            //
            // Walkmesh deltas are 3D but we treat them as 2D (dx, dy, 0) because the
            // floor is essentially flat for nav purposes. Projection becomes:
            //   screenRight = dx*axis0[0] + dy*axis0[1]
            //   screenDown  = dx*axis1[0] + dy*axis1[1]
            // matching how FormatNavComponents already uses s_camRight/Down.
            //
            // The chase auto-pilot's empirical calibration will overwrite these values
            // on chase fields when it runs (s_calibPending stays true). For non-chase
            // fields, the CA-derived values remain in place — which is exactly the
            // gap that caused 'left means right on some fields' for manual nav.
            if (s_cameraAxes.valid) {
                // v0.17.0.1: Normalize the 2D projection of axis0/axis1 to unit length.
                // The .ca file stores axes as 3D unit vectors (int16 fixed-point /4096).
                // For a tilted camera, axis1 has most of its magnitude in the Z
                // (depth) component — the XY projection is short. Walkmesh deltas have
                // Z=0, so dotting a short-XY-vector against (dx, dy) produces small
                // results, asymmetric with the camRight projection. atan2(sD, sR) then
                // biases toward east/west and the wrong cardinal comes out.
                //
                // The chase auto-pilot's empirical calibration produces NORMALIZED
                // values (it divides the measured walkmesh delta by its magnitude),
                // so the screen-projection is symmetric. We match that here by
                // normalizing axis0/axis1's 2D projections to unit length too.
                //
                // Why this works geometrically: when the engine reads analog input
                // (lX, lY) and converts to walkmesh movement, it follows the camera
                // axes' 2D projection as a *direction* (the speed is normalized to
                // the player's walking pace, not the axis magnitude). So the
                // walkmesh direction of "press right arrow" is the unit-length 2D
                // projection of axis0, not the raw 3D unit vector's XY components.
                // BAT v0.17.0 confirmed this for bghall_1 where the raw projection
                // produced `camDown=(0.044,-0.330)` (2D mag 0.333), breaking the
                // cardinal computation; normalized projection gives `camDown=
                // (0.132,-0.991)` (2D mag 1.0) and the cardinal binning works.
                float r2x = (float)s_cameraAxes.axis0[0] / 4096.0f;
                float r2y = (float)s_cameraAxes.axis0[1] / 4096.0f;
                float r2len = sqrtf(r2x*r2x + r2y*r2y);
                float d2x = (float)s_cameraAxes.axis1[0] / 4096.0f;
                float d2y = (float)s_cameraAxes.axis1[1] / 4096.0f;
                float d2len = sqrtf(d2x*d2x + d2y*d2y);
                if (r2len > 0.001f && d2len > 0.001f) {
                    s_camRightX = r2x / r2len;
                    s_camRightY = r2y / r2len;
                    s_camDownX  = d2x / d2len;
                    s_camDownY  = d2y / d2len;
                    // v0.17.4: Det convention check. The .ca format usually stores
                    // axes with det(camRight, camDown) = -1 (screen convention:
                    // camDown projects to world-down). Some fields (Aaron's BAT
                    // surfaced bg2f_2 / Balamb Garden classroom) have det = +1
                    // after 2D projection — axis1 ends up pointing world-UP after
                    // normalization. The engine treats those as left-handed and
                    // the predicted UP/DOWN cardinals come out exactly opposite
                    // of the world direction Aaron actually moved ("had to go
                    // opposite the instructions"). Negating camDown when det>0
                    // forces standard right-handed convention; passive calibration
                    // (v0.17.4 observer) then handles any residual rotation.
                    float det2d = s_camRightX * s_camDownY - s_camRightY * s_camDownX;
                    if (det2d > 0.0f) {
                        s_camDownX = -s_camDownX;
                        s_camDownY = -s_camDownY;
                        Log::Field("FieldNavigation: [NAV-PROJ-INIT] field='%s' det-correction: raw det=%+.3f (left-handed); negated camDown to (%.3f,%.3f). Engine now treats axis1 as screen-up convention; manual nav uses negated value.",
                                   fieldName, det2d, s_camDownX, s_camDownY);
                    }
                    // v0.17.5: Quantize camRight angle to its nearest 90-degree
                    // world-cardinal snap, then derive camDown by rotating 90
                    // degrees clockwise from camRight (which preserves the
                    // det=-1 screen convention regardless of where camRight
                    // happens to land).
                    //
                    // Rationale: v0.17.3 BAT data showed FF8's engine response
                    // to a held single arrow is world-axis-aligned on every
                    // tested field (bghall_1 measured exactly (1,0); bg2f_1
                    // exactly (0,1); bgroom_1 exactly (0,-1); bghall_4 exactly
                    // (0,1)). The CA-file values for those fields had axis
                    // angles of 7.8, 65.4, -62.5 and 23.8 degrees respectively,
                    // each rounding cleanly to a world cardinal. Only bg2f_2
                    // (classroom) had measured directions 5-11 degrees off-
                    // axis after the det fix; that residual is well within the
                    // 22.5-degree cardinal-sector tolerance and Aaron confirmed
                    // bg2f_2 navigates correctly with the det fix alone.
                    //
                    // The engine therefore appears to use a 90-deg-quantized
                    // version of its camera matrix when mapping DIJOYSTATE2
                    // lX/lY to walkmesh delta. Quantizing here makes the
                    // mod's cardinal prediction match the engine exactly
                    // (or within sector tolerance for bg2f_2), with zero
                    // observation-based correction and zero per-field state.
                    //
                    // Quantize camRight independently. Derive camDown from
                    // camRight via the rotation (x, y) -> (y, -x) which is
                    // R(-90deg) and exactly the screen-down convention with
                    // det = -1. (Quantizing camDown independently could break
                    // orthogonality if camRight happened to be near a 45-deg
                    // boundary, so we do not do that.)
                    {
                        float angleR = atan2f(s_camRightY, s_camRightX);
                        float quantum = (float)NAV_PI / 2.0f;  // 90 degrees in radians
                        float snappedR = roundf(angleR / quantum) * quantum;
                        float qrx = cosf(snappedR);
                        float qry = sinf(snappedR);
                        // Clean up floating-point residuals so logs read
                        // cleanly (cosf(pi/2) gives ~6e-8 instead of 0).
                        if (fabsf(qrx) < 1e-6f) qrx = 0.0f;
                        if (fabsf(qry) < 1e-6f) qry = 0.0f;
                        float preRx = s_camRightX, preRy = s_camRightY;
                        float preDx = s_camDownX,  preDy = s_camDownY;
                        s_camRightX = qrx;
                        s_camRightY = qry;
                        // camDown = R(-90deg) * camRight = (y, -x)
                        s_camDownX  = s_camRightY;
                        s_camDownY  = -s_camRightX;
                        float snappedDeg = snappedR * 180.0f / (float)NAV_PI;
                        float origAngleDeg = angleR * 180.0f / (float)NAV_PI;
                        Log::Field("FieldNavigation: [NAV-PROJ-INIT] field='%s' quantization: "
                                   "camRight pre=(%.3f,%.3f) angle=%+.1fdeg -> snap=%+.0fdeg -> "
                                   "camRight=(%.3f,%.3f) camDown=(%.3f,%.3f) (was camDown=(%.3f,%.3f))",
                                   fieldName, preRx, preRy, origAngleDeg, snappedDeg,
                                   s_camRightX, s_camRightY, s_camDownX, s_camDownY,
                                   preDx, preDy);
                    }
                    // v0.17.2: Mirror to the auto-drive private pair so auto-drive
                    // starts from CA-derived values on the first drive of this
                    // field. Phase 1/2 of empirical calibration will overwrite
                    // these as it runs.
                    s_driveCamRightX = s_camRightX;
                    s_driveCamRightY = s_camRightY;
                    s_driveCamDownX  = s_camDownX;
                    s_driveCamDownY  = s_camDownY;
                    s_camAxesSource  = "ca-quantized";
                } else {
                    // Both 2D projections are essentially zero — the camera is
                    // looking straight down a single world axis. Keep identity
                    // defaults; the field will navigate with world-bearing
                    // labels which on this geometry are effectively meaningless.
                    Log::Field("FieldNavigation: [NAV-PROJ-INIT] WARNING field='%s' camera 2D projections degenerate (r2len=%.3f d2len=%.3f); keeping identity defaults.",
                               fieldName, r2len, d2len);
                    s_camAxesSource = "identity";
                }
                // Determinant of the 2D projection (camRight x camDown). For a normal
                // camera this is ~1.0 (or -1.0 if axes are flipped). When near zero,
                // both axes point in nearly the same world direction — pathological
                // for the inverse problem (chase finding #4) but forward projection
                // still works for nav labels. Log so we can spot odd cameras.
                float det = s_camRightX * s_camDownY - s_camRightY * s_camDownX;
                // v0.17.7.6.1: log the actual s_camAxesSource value
                // instead of hardcoding "ca-quantized". On the degenerate-CA
                // branch (else clause above) the code correctly sets
                // s_camAxesSource to "identity" but the original log line
                // here always wrote "source=ca-quantized" regardless,
                // misleading anyone reading the BAT log. The code logic
                // was always right; only the log message was wrong.
                Log::Field("FieldNavigation: [NAV-PROJ-INIT] field='%s' camRight=(%.3f,%.3f) camDown=(%.3f,%.3f) det=%.3f source=%s",
                           fieldName, s_camRightX, s_camRightY, s_camDownX, s_camDownY, det, s_camAxesSource);
                // Pre-normalization 2D magnitudes are useful for diagnosing tilted vs
                // rolled cameras.
                Log::Field("FieldNavigation: [NAV-PROJ-INIT] field='%s' raw-2D r2len=%.3f d2len=%.3f (1.0 = flat camera; <1.0 = tilted)",
                           fieldName, r2len, d2len);
                if (fabsf(det) < 0.1f) {
                    Log::Field("FieldNavigation: [NAV-PROJ-INIT] WARNING field='%s' degenerate camera (|det|<0.1); direction labels may be unreliable.",
                               fieldName);
                }
            } else {
                // No .ca file or parse failed — keep identity defaults. Manual nav
                // will report world-bearing-as-screen-bearing, which is correct on
                // default-camera fields and wrong on rotated ones. Same as pre-v0.17.0.
                Log::Field("FieldNavigation: [NAV-PROJ-INIT] field='%s' camera axes unavailable; using identity defaults (world-bearing fallback).",
                           fieldName);
            }

            // v0.07.68: JSM script scan — classify all entities by opcode signatures.
            // Detects draw points, save points, shops, card games, ladders, and map exits.
            // v0.07.73: Results wired into catalog for TTS announcements.
            s_jsmEntityCount = 0;
            memset(s_jsmEntities, 0, sizeof(s_jsmEntities));
            FieldArchive::ScanJSMScripts(fieldName, s_jsmEntities, MAX_JSM_ENTITIES, s_jsmEntityCount);

            // v0.08.03: Match SET3 hook captures to JSM entities with hasPshmCoords.
            // During field_scripts_init, HookedSet3 captured entity addresses and
            // their resolved positions. Match these to JSM entities by computing
            // the expected entity address from the Others array base + index * stride.
            if (s_set3CaptureCount > 0 && FF8Addresses::pFieldStateOthers) {
                __try {
                    uint8_t* othersBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    if (othersBase) {
                        int othersStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
                        int bgStart = s_jsmDoors + s_jsmLines;
                        // v0.08.15: Also get background entity base for category 2 matching.
                        uint8_t* bgBase2 = nullptr;
                        if (FF8Addresses::HasFieldStateBackgrounds()) {
                            bgBase2 = reinterpret_cast<uint8_t*>(
                                *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                        }
                        int resolved = 0;
                        for (int j = 0; j < s_jsmEntityCount; j++) {
                            FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
                            if (!je.hasPshmCoords) continue;
                            // Compute expected entity address for this JSM entity.
                            // Works for ALL categories since field_scripts_init
                            // executes init scripts for doors, lines, bg, and others.
                            // Others (cat 3): othersBase + (jsmIndex - othersStart) * ENTITY_STRIDE
                            // Background (cat 2): bgBase + (jsmIndex - bgStart) * BG_STRIDE
                            uint32_t expectedAddr = 0;
                            int matchIdx = 0;  // v0.08.15: unified index for log (othersIdx or bgIdx)
                            if (je.jsmCategory == 3) {
                                matchIdx = je.jsmIndex - othersStart;
                                if (matchIdx < 0) continue;
                                expectedAddr = (uint32_t)(uintptr_t)(othersBase + ENTITY_STRIDE * matchIdx);
                            } else if (je.jsmCategory == 2 && bgBase2) {
                                matchIdx = je.jsmIndex - bgStart;
                                if (matchIdx < 0) continue;
                                expectedAddr = (uint32_t)(uintptr_t)(bgBase2 + BG_STRIDE * matchIdx);
                            } else {
                                continue;
                            }
                            // Search SET3 captures for this entity address.
                            for (int c = 0; c < s_set3CaptureCount; c++) {
                                if (s_set3Captures[c].entityAddr == expectedAddr) {
                                    je.posX = s_set3Captures[c].posX;
                                    je.posY = s_set3Captures[c].posY;
                                    je.posZ = s_set3Captures[c].posZ;
                                    je.posTriangle = s_set3Captures[c].triId;
                                    je.hasPosition = true;
                                    resolved++;
                                    Log::Field("FieldNavigation: [SET3-MATCH] ent%d '%s' type=%s "
                                               "cat=%d idx=%d pos=(%d,%d) tri=%u addr=0x%08X",
                                               je.jsmIndex, je.symName,
                                               FieldArchive::JSMEntityTypeName(je.type),
                                               je.jsmCategory, matchIdx, (int)je.posX, (int)je.posY,
                                               (unsigned)je.posTriangle, expectedAddr);
                                    break;
                                }
                            }
                        }
                        // v0.08.05: Fallback — direct struct read for unmatched PSHM entities.
                        // Some entities (e.g. dic) have SET3 in non-init methods, so the
                        // SET3 hook doesn't capture them. Try reading positions directly
                        // from their entity struct. At this point (right after field_scripts_init)
                        // positions may still be (0,0) — that's OK, RefreshCatalog will retry.
                        int directResolved = 0;
                        for (int j2 = 0; j2 < s_jsmEntityCount; j2++) {
                            FieldArchive::JSMEntityInfo& je2 = s_jsmEntities[j2];
                            if (!je2.hasPshmCoords || je2.hasPosition) continue;
                            // v0.08.15: Handle both Others (cat 3) and Background (cat 2) entities.
                            uint8_t* blk = nullptr;
                            int entIdx = 0;
                            if (je2.jsmCategory == 3) {
                                entIdx = je2.jsmIndex - othersStart;
                                if (entIdx < 0) continue;
                                blk = othersBase + ENTITY_STRIDE * entIdx;
                            } else if (je2.jsmCategory == 2 && bgBase2) {
                                entIdx = je2.jsmIndex - bgStart;
                                if (entIdx < 0) continue;
                                blk = bgBase2 + BG_STRIDE * entIdx;
                            } else {
                                continue;
                            }
                            int32_t fpX2 = *(int32_t*)(blk + 0x190);
                            int32_t fpY2 = *(int32_t*)(blk + 0x194);
                            // v0.08.15: triId at 0x1FA is only valid for Others (0x264 stride).
                            // Background structs (0x1B4 stride) don't extend to 0x1FA.
                            uint16_t tri2 = 0;
                            if (je2.jsmCategory == 3) {
                                tri2 = *(uint16_t*)(blk + 0x1FA);
                            }
                            if (fpX2 != 0 || fpY2 != 0) {
                                je2.posX = (int16_t)(fpX2 / 4096);
                                je2.posY = (int16_t)(fpY2 / 4096);
                                je2.posTriangle = tri2;
                                je2.hasPosition = true;
                                directResolved++;
                                Log::Field("FieldNavigation: [SET3-DIRECT] ent%d '%s' type=%s "
                                           "cat=%d idx=%d pos=(%d,%d) tri=%u fp=(%d,%d)",
                                           je2.jsmIndex, je2.symName,
                                           FieldArchive::JSMEntityTypeName(je2.type),
                                           je2.jsmCategory, entIdx,
                                           (int)je2.posX, (int)je2.posY,
                                           (unsigned)tri2, fpX2, fpY2);
                            }
                        }
                        Log::Field("FieldNavigation: [SET3-MATCH] %d captures, %d hook-matched, %d direct-read",
                                   s_set3CaptureCount, resolved, directResolved);

                        // v0.08.11: Varblock diagnostic — DISABLED v0.12.11 (served its purpose).
                        if (false)
                        {
                            // Use known varblock base. Dynamic resolution via opcode_pshm_w+0x1E
                            // fails because FFNx has replaced the dispatch table entry with its
                            // own hook function, so +0x1E reads FFNx code, not the game's embedded
                            // varblock reference. The correct base is 0x1CFE9B8 (Steam 2013 en-US),
                            // confirmed by FFNx source: field_vars_stack_1CFE9B8.
                            uint32_t varblockBase = 0x1CFE9B8;
                            Log::Field("FieldNavigation: [VARBLOCK-DIAG] base=0x%08X (hardcoded Steam 2013 en-US)",
                                       varblockBase);
                            // Read varblock at every PSHM address from JSM entities.
                            for (int vj = 0; vj < s_jsmEntityCount; vj++) {
                                const FieldArchive::JSMEntityInfo& vje = s_jsmEntities[vj];
                                if (!vje.hasPshmCoords) continue;
                                int16_t addrX = vje.pshmAddrX;
                                int16_t addrY = vje.pshmAddrY;
                                int16_t addrZ = vje.pshmAddrZ;
                                int16_t vbX = 0, vbY = 0, vbZ = 0;
                                bool readOk = true;
                                __try {
                                    vbX = *(int16_t*)(varblockBase + (uint16_t)addrX);
                                    vbY = *(int16_t*)(varblockBase + (uint16_t)addrY);
                                    vbZ = *(int16_t*)(varblockBase + (uint16_t)addrZ);
                                } __except(EXCEPTION_EXECUTE_HANDLER) {
                                    readOk = false;
                                }
                                // Find SET3-captured position for comparison.
                                const char* matchStr = "";
                                int16_t set3X = 0, set3Y = 0;
                                if (vje.hasPosition) {
                                    set3X = vje.posX;
                                    set3Y = vje.posY;
                                    if (vbX == set3X && vbY == set3Y)
                                        matchStr = " <<MATCH>>";
                                    else
                                        matchStr = " <<MISMATCH>>";
                                }
                                Log::Field("FieldNavigation: [VARBLOCK-DIAG] ent%d '%s' "
                                           "pshmAddr=(%d,%d,%d) varblock=(%d,%d,%d) "
                                           "set3pos=(%d,%d) %s%s",
                                           vje.jsmIndex, vje.symName,
                                           (int)addrX, (int)addrY, (int)addrZ,
                                           (int)vbX, (int)vbY, (int)vbZ,
                                           (int)set3X, (int)set3Y,
                                           readOk ? "" : "READ_FAIL",
                                           matchStr);
                            }
                        }

                        // v0.08.06: PSHM descriptor table probe — DISABLED v0.12.11 (served its purpose).
                        if (false)
                        {
                            static const uint32_t PSHM_TABLE_BASE = 0x01DCB340;
                            static const uint32_t PSHM_GLOBAL_FIELD_VAR = 0x01CE476A;
                            __try {
                                int16_t fieldVar = *(int16_t*)PSHM_GLOBAL_FIELD_VAR;
                                Log::Field("FieldNavigation: [PSHM-PROBE] global@0x%08X = %d",
                                           PSHM_GLOBAL_FIELD_VAR, (int)fieldVar);
                                // Scan descriptor table for all Others indices.
                                // Others start at jsmDoors+jsmLines+jsmBackgrounds in the JSM order.
                                // The table is indexed by a flat entity index that includes
                                // doors+lines+bg+others. Scan a range around where Others live.
                                int scanStart = 0;
                                int scanEnd = s_jsmDoors + s_jsmLines + s_jsmBackgrounds + s_jsmOthers;
                                if (scanEnd > 64) scanEnd = 64;  // safety
                                Log::Field("FieldNavigation: [PSHM-PROBE] scanning table 0x%08X indices %d..%d (others start at %d)",
                                           PSHM_TABLE_BASE, scanStart, scanEnd - 1,
                                           s_jsmDoors + s_jsmLines + s_jsmBackgrounds);
                                for (int ti = scanStart; ti < scanEnd; ti++) {
                                    __try {
                                        uint32_t* tableEntry = (uint32_t*)(PSHM_TABLE_BASE + ti * 4);
                                        uint32_t descPtr = *tableEntry;
                                        if (descPtr == 0) continue;  // null entry, skip silently
                                        // Check if descriptor is valid (first DWORD != -1)
                                        int32_t firstDword = *(int32_t*)descPtr;
                                        if (firstDword == -1) {
                                            Log::Field("FieldNavigation: [PSHM-PROBE] [%d] desc=0x%08X INVALID (first=-1)",
                                                       ti, descPtr);
                                            continue;
                                        }
                                        // Read key descriptor fields
                                        uint32_t dataArr = *(uint32_t*)(descPtr + 0x68);
                                        uint32_t secPtr  = *(uint32_t*)(descPtr + 0x6C);
                                        int16_t  lastAddr = *(int16_t*)(descPtr + 0x7E);
                                        int16_t  resX    = *(int16_t*)(descPtr + 0x0C);
                                        int16_t  resY    = *(int16_t*)(descPtr + 0x0E);
                                        int16_t  field50 = *(int16_t*)(descPtr + 0x50);
                                        int16_t  field52 = *(int16_t*)(descPtr + 0x52);
                                        // Get SYM name for this index
                                        const char* symName = "?";
                                        for (int sj = 0; sj < s_jsmEntityCount; sj++) {
                                            if (s_jsmEntities[sj].jsmIndex == ti) {
                                                symName = s_jsmEntities[sj].symName;
                                                break;
                                            }
                                        }
                                        Log::Field("FieldNavigation: [PSHM-PROBE] [%d] '%s' desc=0x%08X "
                                                   "data=0x%08X sec=0x%08X lastAddr=%d "
                                                   "res=(%d,%d) f50=(%d,%d)",
                                                   ti, symName, descPtr, dataArr, secPtr,
                                                   (int)lastAddr, (int)resX, (int)resY,
                                                   (int)field50, (int)field52);
                                        // For dic (ent24 = othersIdx 12), also dump the data array
                                        // to understand the parametric format.
                                        if (ti == 24 && dataArr > 0x10000 && dataArr < 0x20000000) {
                                            // Dump first 32 WORDs of data array
                                            int16_t* dw = (int16_t*)dataArr;
                                            Log::Field("FieldNavigation: [PSHM-PROBE] dic data[0..31]: "
                                                       "%d %d %d %d %d %d %d %d "
                                                       "%d %d %d %d %d %d %d %d "
                                                       "%d %d %d %d %d %d %d %d "
                                                       "%d %d %d %d %d %d %d %d",
                                                       dw[0],dw[1],dw[2],dw[3],dw[4],dw[5],dw[6],dw[7],
                                                       dw[8],dw[9],dw[10],dw[11],dw[12],dw[13],dw[14],dw[15],
                                                       dw[16],dw[17],dw[18],dw[19],dw[20],dw[21],dw[22],dw[23],
                                                       dw[24],dw[25],dw[26],dw[27],dw[28],dw[29],dw[30],dw[31]);
                                        }
                                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                                        Log::Field("FieldNavigation: [PSHM-PROBE] [%d] ACCESS VIOLATION", ti);
                                    }
                                }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                                Log::Field("FieldNavigation: [PSHM-PROBE] Exception in table scan");
                            }
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    Log::Field("FieldNavigation: [SET3-MATCH] Exception matching captures");
                }
            }

            // v0.07.82: Assign lineType to captured trigger lines from JSM classification.
            // SETLINE fires during field_scripts_init for each Line entity (and possibly
            // Door entities). Captured lines are ordered by lineOrder (SETLINE call order).
            // JSM Line entities are at indices [countDoors .. countDoors+countLines-1].
            // We match by assuming captured lines arrive in JSM entity order.
            // If captured count <= jsmLines, direct map. If more, excess are from doors.
            {
                // Sort captured lines by lineOrder to establish a stable mapping.
                // (They should already be in order, but be safe.)
                for (int a = 0; a < s_capturedLineCount - 1; a++) {
                    for (int b = a + 1; b < s_capturedLineCount; b++) {
                        if (s_capturedLines[b].lineOrder < s_capturedLines[a].lineOrder) {
                            CapturedTriggerLine tmp = s_capturedLines[a];
                            s_capturedLines[a] = s_capturedLines[b];
                            s_capturedLines[b] = tmp;
                        }
                    }
                }
                int linesMapped = 0;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    s_capturedLines[t].lineType = FieldArchive::JSM_ENT_UNKNOWN;
                    s_capturedLines[t].destFieldId = -1;
                    s_capturedLines[t].hasExtDispatch = false;
                    s_capturedLines[t].hasDialogReqTarget = false;  // v0.17.7.5.4
                    // Map captured line t to JSM Line entity at jsmDoors + t.
                    // Line entities in JSM: indices [jsmDoors .. jsmDoors+jsmLines-1].
                    int jsmIdx = s_jsmDoors + t;
                    if (jsmIdx < s_jsmEntityCount &&
                        s_jsmEntities[jsmIdx].jsmCategory == 1) {  // category 1 = Line
                        s_capturedLines[t].lineType = s_jsmEntities[jsmIdx].type;
                        s_capturedLines[t].hasExtDispatch = s_jsmEntities[jsmIdx].hasExtDispatch;
                        // v0.17.7.5.4: Copy the dialog-REQ-target signal too. This is
                        // what the catalog now uses to decide if a SCREEN_BOUND line
                        // is dual-purpose (exit-via-interaction) vs. a pure walk-across
                        // exit. hasExtDispatch alone is too noisy (fires on any 0x1C use).
                        s_capturedLines[t].hasDialogReqTarget = s_jsmEntities[jsmIdx].hasDialogReqTarget;
                        // v0.07.83 / v0.17.7.1.2: Capture MAPJUMP destination for screen boundary lines.
                        //
                        // The JSM scanner sets info.param to either:
                        //   * a literal field ID 0..981             (script pushed PSHN_L FieldID)
                        //   * a PSHM_W marker 0x80000000 | addr     (script pushed PSHM_W field-var)
                        //   * a negative literal e.g. -2 World Map  (PSHM_W passthrough, opcParam<0)
                        //   * a small negative on resolution failure
                        //
                        // v0.17.7.1.2 adds PSHM marker resolution: when bit 31
                        // is set, the low 16 bits encode the field-var address
                        // in the varblock. By the time this block runs, the
                        // engine has already executed s_originalFieldScriptsInit
                        // (above) so the varblock at 0x1CFE9B8 is populated.
                        // Read the 16-bit field ID from varblock[addr] and use
                        // that as the resolved destination.
                        //
                        // Why this matters for B-Garden hall fields: their exits
                        // (Cafeteria, Dormitories, Parking Lot) are emitted by
                        // the engine as MAPJUMP <PSHM_W varAddr>, with the
                        // varAddr indexing into a per-field destination table
                        // populated by the engine during init. Static JSM scan
                        // sees only the marker; INF gateways (v0.17.7.1.1's
                        // first attempted fix) hold vestigial PS1 placeholder
                        // destFieldIds for these fields; the runtime varblock
                        // is the only authoritative source.
                        //
                        // PSHSM_W (special memory, opcode 0x0C) also produces
                        // a marker via the same scanner branch but reads from
                        // a different base. Handled together here -- if the
                        // 0x1CFE9B8 read produces an out-of-range value, we
                        // leave info.param as-is and the catalog falls back to
                        // bare "Exit".
                        if (s_jsmEntities[jsmIdx].type == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                            int rawParam = s_jsmEntities[jsmIdx].param;
                            if ((unsigned)rawParam & 0x80000000u) {
                                uint16_t pshmAddr = (uint16_t)(rawParam & 0xFFFF);
                                // v0.17.7.5.3: addr-as-literal. Empirically across
                                // 8 BAT fires on B-Garden hall fields, SCREEN_BOUND
                                // lines whose static resolver returns VARBLOCK <addr>
                                // have engine destField == addr (in decimal):
                                //   bghall_2 squallsd  0x00A5 -> 165 (Hall 1)
                                //   bghall_2 zell      0x00B9 -> 185 (Quad 4)
                                //   bghall_2 zells     0x00E3 -> 227 (Hallway 4)
                                //   bghall_5 selphie   0x00E0 -> 224 (Hallway 1)
                                //   bghall_5 irvine    0x00AA -> 170 (Hall 6)
                                //   bghall_5 zell      0x00E4 -> 228 (Hallway 5)  [predicted]
                                //   bghall_5 zells     0x00E1 -> 225 (Hallway 2)  [predicted]
                                //   bgroad_1 squall    0x009A -> 154 (Cafeteria 1)
                                //
                                // Mechanism (best-current-understanding): the B-Garden
                                // script authors chose pshmAddr = destField for ease
                                // of reading; the varblock at byte-offset addr holds
                                // value=addr at method-7 execution time (some setup
                                // we haven't located populates it between field-load
                                // and the line's MAPJUMP3 firing). We can't read it
                                // at field-load time because at that lifecycle point
                                // the varblock isn't yet populated -- prior v0.17.7.x
                                // builds read varblock here and got either 0 (kept
                                // marker, line stayed bare) or wrong values that
                                // didn't match the engine's actual destField (e.g.
                                // bghall_2 zell varblock[0xB9]=255 at field load but
                                // engine destField=185, mismatching the v0.17.7.4 BAT).
                                //
                                // The addr-as-literal interpretation works whether the
                                // pattern is intentional self-documenting bytecode
                                // (most likely) or coincidental (engine populates
                                // varblock[X] = X for some init range we haven't
                                // identified). Either way the labeling comes out
                                // correct on every BAT'd traversal.
                                //
                                // Caveats:
                                //  * If a future field uses PSHM_W with addr that
                                //    ISN'T the destField (a truly dynamic varblock-
                                //    driven destination), this will mislabel it.
                                //    No such case is known but it can't be ruled
                                //    out for non-B-Garden fields.
                                //  * bghall_1 has 3 SCREEN_BOUND lines all picking
                                //    addr=0x00AF (=175=Hall 11). After this fix
                                //    they all label as "Exit to Hall 11". If that's
                                //    wrong, we'll catch it in catalog testing.
                                if (pshmAddr > 0 && pshmAddr < FIELD_DISPLAY_NAMES_COUNT) {
                                    Log::Field("FieldNavigation: [PSHM-DEST] line%d (jsm%d '%s') "
                                               "marker=0x%08X addr=0x%04X -> field %d (%s) [addr-as-literal]",
                                               t, jsmIdx, s_jsmEntities[jsmIdx].symName,
                                               (unsigned)rawParam, pshmAddr,
                                               (int)pshmAddr, FIELD_DISPLAY_NAMES[pshmAddr]);
                                    rawParam = (int)pshmAddr;
                                } else {
                                    Log::Field("FieldNavigation: [PSHM-DEST] line%d (jsm%d '%s') "
                                               "marker=0x%08X addr=0x%04X out of field-id range (0..%d), keeping marker",
                                               t, jsmIdx, s_jsmEntities[jsmIdx].symName,
                                               (unsigned)rawParam, pshmAddr,
                                               FIELD_DISPLAY_NAMES_COUNT - 1);
                                }
                            }
                            s_capturedLines[t].destFieldId = rawParam;
                        }
                        linesMapped++;
                    }
                }
                // Log the mapping results.
                int cameraPans = 0, screenBounds = 0, lineEvents = 0, lineUnknown = 0, lineInteractive = 0;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    switch (s_capturedLines[t].lineType) {
                        case FieldArchive::JSM_ENT_LINE_CAMERA_PAN:   cameraPans++; break;
                        case FieldArchive::JSM_ENT_LINE_SCREEN_BOUND: screenBounds++; break;
                        case FieldArchive::JSM_ENT_LINE_EVENT:        lineEvents++; break;
                        case FieldArchive::JSM_ENT_LINE_INTERACTIVE:  lineInteractive++; break;
                        default: lineUnknown++; break;
                    }
                }
                Log::Field("FieldNavigation: [fieldload] lineType assigned: %d captured, %d mapped "
                           "(camPan=%d screenBd=%d event=%d interact=%d unknown=%d)",
                           s_capturedLineCount, linesMapped,
                           cameraPans, screenBounds, lineEvents, lineInteractive, lineUnknown);

                // v0.17.7.2: MAPJUMP destination resolver DIAGNOSTIC (observation only).
                //
                // For each SCREEN_BOUND line whose param is an unresolved bit31
                // PSHM marker (the runtime varblock read above failed because the
                // varblock isn't populated at this lifecycle point), enumerate
                // every init-method POPM_W write across all entities targeting
                // the SAME varblock address. If exactly one entity writes a
                // sensible field-ID value there, v0.17.7.3 will adopt it as the
                // resolved destination.
                //
                // This block makes NO data changes -- it only logs. The goal is
                // to confirm (or rule out) the hypothesis that field-exit
                // destinations live in init-method literal-PUSH + POPM_W pairs
                // captured by s_initVarMaps[]. If the BAT log shows writers
                // matching the unresolved addresses, the resolver in v0.17.7.3
                // is a 5-line cross-reference. If it shows zero writers, the
                // destinations live in story-dispatch methods (m != 0) and the
                // scanner needs to be widened first.
                //
                // The summary [INITVARS-SUMMARY] block at the end of this
                // diagnostic shows the full landscape of init-var writes for
                // this field so we can spot patterns even when the per-line
                // lookup misses.
                {
                    int unresolvedLines = 0;
                    int linesWithWriters = 0;
                    for (int t = 0; t < s_capturedLineCount; t++) {
                        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND)
                            continue;
                        int dfi = s_capturedLines[t].destFieldId;
                        // Filter to UNRESOLVED markers only (bit31 set, low 16 bits = varblock addr).
                        if (((unsigned)dfi & 0x80000000u) == 0) continue;
                        unresolvedLines++;
                        uint16_t pshmAddr = (uint16_t)(dfi & 0xFFFF);
                        int jsmIdx = s_jsmDoors + t;
                        const char* sym = (jsmIdx < s_jsmEntityCount)
                                            ? s_jsmEntities[jsmIdx].symName : "?";
                        // Look up all init-method writers to this address.
                        FieldArchive::InitVarWriter writers[16] = {};
                        int totalWriters = FieldArchive::LookupInitVarWrites(
                            (int16_t)pshmAddr, writers, 16);
                        if (totalWriters == 0) {
                            Log::Field("FieldNavigation: [MAPJUMP-RESOLVE] line%d (jsm%d '%s') "
                                       "addr=0x%04X (%d): NO init writers found",
                                       t, jsmIdx, sym, (unsigned)pshmAddr, (int)pshmAddr);
                        } else {
                            linesWithWriters++;
                            int logged = totalWriters < 16 ? totalWriters : 16;
                            Log::Field("FieldNavigation: [MAPJUMP-RESOLVE] line%d (jsm%d '%s') "
                                       "addr=0x%04X (%d): %d init writers%s",
                                       t, jsmIdx, sym, (unsigned)pshmAddr, (int)pshmAddr,
                                       totalWriters, totalWriters > 16 ? " (capped to 16)" : "");
                            for (int w = 0; w < logged; w++) {
                                int wEnt = writers[w].entityIdx;
                                int32_t wVal = writers[w].value;
                                // Look up writer's sym name in the JSM table.
                                const char* wSym = "?";
                                for (int q = 0; q < s_jsmEntityCount; q++) {
                                    if (s_jsmEntities[q].jsmIndex == wEnt) {
                                        wSym = s_jsmEntities[q].symName;
                                        break;
                                    }
                                }
                                // If the value is a plausible field ID, also resolve its name.
                                const char* destName = "";
                                if (wVal > 0 && wVal < FIELD_DISPLAY_NAMES_COUNT)
                                    destName = FIELD_DISPLAY_NAMES[wVal];
                                Log::Field("FieldNavigation: [MAPJUMP-RESOLVE]   writer ent%d '%s' "
                                           "value=%d %s%s",
                                           wEnt, wSym, (int)wVal,
                                           destName[0] ? "-> " : "", destName);
                            }
                        }
                    }
                    Log::Field("FieldNavigation: [MAPJUMP-RESOLVE] summary: %d unresolved SCREEN_BOUND lines, "
                               "%d found writers, %d had no writers",
                               unresolvedLines, linesWithWriters,
                               unresolvedLines - linesWithWriters);

                    // [INITVARS-SUMMARY]: full landscape of init writes for this field.
                    // Useful for spotting the destination values when LookupInitVarWrites()
                    // misses (e.g. if addresses are stored shifted, masked, or under a
                    // different convention than the markers).
                    FieldArchive::InitVarTuple allWrites[256] = {};
                    int totalAllWrites = FieldArchive::EnumerateInitVars(allWrites, 256);
                    int loggedAll = totalAllWrites < 256 ? totalAllWrites : 256;
                    Log::Field("FieldNavigation: [INITVARS-SUMMARY] field has %d init-method POPM_W writes%s",
                               totalAllWrites, totalAllWrites > 256 ? " (capped to 256)" : "");
                    for (int w = 0; w < loggedAll; w++) {
                        int wEnt = allWrites[w].entityIdx;
                        int32_t wAddr = allWrites[w].addr;
                        int32_t wVal = allWrites[w].value;
                        const char* wSym = "?";
                        for (int q = 0; q < s_jsmEntityCount; q++) {
                            if (s_jsmEntities[q].jsmIndex == wEnt) {
                                wSym = s_jsmEntities[q].symName;
                                break;
                            }
                        }
                        // Annotate if the value resembles a field ID.
                        const char* destName = "";
                        if (wVal > 0 && wVal < FIELD_DISPLAY_NAMES_COUNT)
                            destName = FIELD_DISPLAY_NAMES[wVal];
                        Log::Field("FieldNavigation: [INITVARS-SUMMARY]   ent%d '%s' addr=0x%04X (%d) "
                                   "value=%d %s%s",
                                   wEnt, wSym, (unsigned)(wAddr & 0xFFFF), (int)wAddr, (int)wVal,
                                   destName[0] ? "-> " : "", destName);
                    }
                }

                // v0.12.23: Dump scripts of Event Trigger and Unknown-type Line entities.
                // These are interaction mediators on shared dormitory/classroom fields.
                // Their scripts contain REQ opcodes targeting Others entities — revealing
                // which interactive object (bed/desk/wardrobe) each SETLINE zone controls.
                //
                // v0.17.7.2: Gated behind FF8OPC_VERBOSE_JSM. The same per-Director
                // log-explosion problem affected this loop on dormitory fields where
                // Lines reference long Background scripts.
#ifdef FF8OPC_VERBOSE_JSM
                for (int ld = 0; ld < s_capturedLineCount; ld++) {
                    int ldJsmIdx = s_jsmDoors + ld;
                    if (ldJsmIdx >= s_jsmEntityCount) continue;
                    int ldType = s_jsmEntities[ldJsmIdx].type;
                    if (ldType != FieldArchive::JSM_ENT_LINE_CAMERA_PAN &&
                        ldType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        Log::Field("FieldNavigation: [LINE-SCRIPT] Dumping Line entity %d '%s' (type=%s)",
                                   ldJsmIdx, s_jsmEntities[ldJsmIdx].symName,
                                   FieldArchive::JSMEntityTypeName((FieldArchive::JSMEntityType)ldType));
                        FieldArchive::DumpEntityScript(fieldName, ldJsmIdx);
                    }
                }
#endif
            }

            // v0.12.17: Override JSM entity positions with INF trigger zone data.
            // INF trigger zones (parsed from offset 0x1E4 via x86 disassembly of 0x47B610)
            // provide the authoritative interaction positions for Background entities.
            // These are the exact line coordinates the engine checks for proximity.
            // For entities like the Directory (dic), this replaces the unreliable
            // PSHM_W shift-pattern approximation with the definitive position.
            if (s_triggerCount > 0) {
                int trigOverridden = 0;
                for (int t = 0; t < s_triggerCount; t++) {
                    const char* trigSym = s_triggers[t].symName;
                    if (trigSym[0] == '\0') continue;
                    // Find matching JSM entity by SYM name.
                    for (int j = 0; j < s_jsmEntityCount; j++) {
                        if (_stricmp(s_jsmEntities[j].symName, trigSym) == 0) {
                            int16_t oldX = s_jsmEntities[j].posX;
                            int16_t oldY = s_jsmEntities[j].posY;
                            int16_t newX = (int16_t)s_triggers[t].centerX;
                            int16_t newY = (int16_t)s_triggers[t].centerZ;
                            s_jsmEntities[j].posX = newX;
                            s_jsmEntities[j].posY = newY;
                            s_jsmEntities[j].hasPosition = true;
                            trigOverridden++;
                            Log::Field("FieldNavigation: [INF-TRIG-POS] ent%d '%s' type=%s "
                                       "pos (%d,%d)->(%d,%d) from trigger zone %d (type=%d)",
                                       s_jsmEntities[j].jsmIndex, trigSym,
                                       FieldArchive::JSMEntityTypeName(s_jsmEntities[j].type),
                                       (int)oldX, (int)oldY, (int)newX, (int)newY,
                                       s_triggers[t].triggerIdx, (int)s_triggers[t].interactionType);
                            break;
                        }
                    }
                }
                if (trigOverridden > 0)
                    Log::Field("FieldNavigation: [INF-TRIG-POS] %d JSM positions overridden from %d trigger zones",
                               trigOverridden, s_triggerCount);
            }

            // v0.12.17: SETLINE trigger line position override for PSHM_W entities.
            // Interactive objects (bed, desk, directory, signs, etc.) whose positions come
            // from PSHM_W shift-patterns have ~200-500 unit error. The engine's ACTUAL
            // interaction zone for these entities is defined by SETLINE trigger lines
            // placed by nearby Line entities. Matching each PSHM entity to its nearest
            // SETLINE center gives the authoritative interaction position.
            // This is a general solution that applies across all FF8 field maps.
            if (s_capturedLineCount > 0 && s_jsmEntityCount > 0) {
                int setlineOverridden = 0;
                for (int ei = 0; ei < s_jsmEntityCount; ei++) {
                    FieldArchive::JSMEntityInfo& je = s_jsmEntities[ei];
                    // Only override PSHM entities with shift-pattern positions.
                    // These have hasPshmCoords=true AND hasPosition=true (from shift-pattern).
                    if (!je.hasPshmCoords || !je.hasPosition) continue;
                    // Skip entities with precise positions (non-PSHM resolved)
                    // — their positions are already accurate.
                    
                    float bestDist = 999999.0f;
                    int bestLine = -1;
                    float bestCx = 0, bestCy = 0;
                    for (int li = 0; li < s_capturedLineCount; li++) {
                        float cx = (float)(s_capturedLines[li].x1 + s_capturedLines[li].x2) / 2.0f;
                        float cy = (float)(s_capturedLines[li].y1 + s_capturedLines[li].y2) / 2.0f;
                        float dx = cx - (float)je.posX;
                        float dy = cy - (float)je.posY;
                        float dist = sqrtf(dx*dx + dy*dy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestLine = li;
                            bestCx = cx;
                            bestCy = cy;
                        }
                    }
                    // Override if nearest SETLINE is within 2000 units of shift-pattern position.
                    // Typical shift-pattern error is 200-500 units; 2000 gives generous margin.
                    if (bestLine >= 0 && bestDist < 2000.0f) {
                        Log::Field("FieldNavigation: [SETLINE-POS] ent%d '%s' pos(%d,%d) -> SETLINE[%d] center(%.0f,%.0f) dist=%.0f",
                                   je.jsmIndex, je.symName,
                                   (int)je.posX, (int)je.posY,
                                   bestLine, bestCx, bestCy, bestDist);
                        je.posX = (int16_t)bestCx;
                        je.posY = (int16_t)bestCy;
                        setlineOverridden++;
                    }
                }
                if (setlineOverridden > 0)
                    Log::Field("FieldNavigation: [SETLINE-POS] %d PSHM entity positions overridden from %d SETLINE trigger lines",
                               setlineOverridden, s_capturedLineCount);
            }

            // v0.12.22: Walkmesh dead-end detection for Director-dispatched interactive objects.
            // Interactive background objects (beds, desks, wardrobes) are typically in walkmesh
            // alcoves/dead-ends. Find these and match to unpositioned interactive objects.
            if (s_walkmesh.valid && s_jsmEntityCount > 0) {
                // Step 1: Count neighbors per triangle
                static int s_neighborCount[4096];
                int numTri = s_walkmesh.numTriangles;
                if (numTri > 4096) numTri = 4096;
                for (int t = 0; t < numTri; t++) {
                    s_neighborCount[t] = 0;
                    for (int e = 0; e < 3; e++)
                        if (s_walkmesh.triangles[t].neighbor[e] != 0xFFFF)
                            s_neighborCount[t]++;
                }

                // Step 2: BFS cluster dead-ends (1-neighbor) through narrow passages (2-neighbor)
                static bool s_deadVisited[4096];
                memset(s_deadVisited, 0, numTri * sizeof(bool));

                // v0.15.9.2.6: cluster array promoted to file-scope state
                // (s_deadClusters / s_deadClusterCount in field_navigation.cpp)
                // so chase_auto_pilot can read it via GetLargestClusterCenter().
                // Reset before populating.
                s_deadClusterCount = 0;
                memset(s_deadClusters, 0, sizeof(s_deadClusters));
                int totalDeadEnds = 0, totalNarrow = 0;

                for (int t = 0; t < numTri; t++) {
                    if (s_neighborCount[t] == 1) totalDeadEnds++;
                    else if (s_neighborCount[t] == 2) totalNarrow++;
                }

                for (int t = 0; t < numTri && s_deadClusterCount < MAX_DEAD_CLUSTERS; t++) {
                    if (s_deadVisited[t] || s_neighborCount[t] != 1) continue;
                    // BFS from this dead-end through narrow (<=2 neighbor) triangles
                    float sumX = 0, sumY = 0;
                    int count = 0;
                    static uint16_t bfsQ[512];
                    int qH = 0, qT = 0;
                    bfsQ[qT++] = (uint16_t)t;
                    s_deadVisited[t] = true;
                    while (qH < qT) {
                        uint16_t cur = bfsQ[qH++];
                        sumX += s_walkmesh.triangles[cur].centerX;
                        sumY += s_walkmesh.triangles[cur].centerY;
                        count++;
                        for (int e = 0; e < 3; e++) {
                            uint16_t nb = s_walkmesh.triangles[cur].neighbor[e];
                            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
                            if (s_deadVisited[nb]) continue;
                            if (s_neighborCount[nb] <= 2) {
                                s_deadVisited[nb] = true;
                                if (qT < 512) bfsQ[qT++] = nb;
                            }
                        }
                    }
                    s_deadClusters[s_deadClusterCount].centerX = sumX / (float)count;
                    s_deadClusters[s_deadClusterCount].centerY = sumY / (float)count;
                    s_deadClusters[s_deadClusterCount].triCount = count;
                    s_deadClusters[s_deadClusterCount].seedTri = t;
                    s_deadClusterCount++;
                }

                Log::Field("FieldNavigation: [DEADEND] %s: %d tris, %d dead-ends, %d narrow, %d clusters",
                           fieldName, numTri, totalDeadEnds, totalNarrow, s_deadClusterCount);

                // Step 3: Log significant clusters and match to unpositioned interactive objects.
                static const int MIN_CLUSTER_TRIS = 2;
                int significantClusters = 0;
                for (int c = 0; c < s_deadClusterCount; c++) {
                    const char* tag = (s_deadClusters[c].triCount >= MIN_CLUSTER_TRIS) ? "*" : " ";
                    if (s_deadClusters[c].triCount >= MIN_CLUSTER_TRIS) significantClusters++;
                    Log::Field("FieldNavigation: [DEADEND]  %s cluster[%d] center=(%.0f,%.0f) tris=%d seed=%d",
                               tag, c, s_deadClusters[c].centerX, s_deadClusters[c].centerY,
                               s_deadClusters[c].triCount, s_deadClusters[c].seedTri);
                }
                Log::Field("FieldNavigation: [DEADEND] %d significant clusters (>=%d tris)",
                           significantClusters, MIN_CLUSTER_TRIS);

                int matched = 0;
                for (int ei = 0; ei < s_jsmEntityCount; ei++) {
                    FieldArchive::JSMEntityInfo& je = s_jsmEntities[ei];
                    if (je.type != FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) continue;
                    if (je.hasPosition && (je.posX != 0 || je.posY != 0)) continue;
                    Log::Field("FieldNavigation: [DEADEND]   unpositioned intobj: ent%d '%s' (%d sig. clusters)",
                               je.jsmIndex, je.symName, significantClusters);
                    matched++;
                }
                if (matched > 0) {
                    Log::Field("FieldNavigation: [DEADEND]   %d unpositioned objects, %d significant clusters",
                               matched, significantClusters);
                }
            }

            Log::Field("FieldNavigation: [fieldload] SYM: %d names, %d entities, offset=0, %d triggers, walkmesh=%s",
                       s_symNameCount, (int)entCount, s_triggerCount,
                       s_walkmesh.valid ? "OK" : "NONE");
            Log::Field("FieldNavigation: [fieldload] JSM: doors=%d lines=%d bg=%d others=%d",
                       s_jsmDoors, s_jsmLines, s_jsmBackgrounds, s_jsmOthers);

            // v06.08: NavLog field load
            NavLog::FieldLoad(fieldName, (int)fieldId,
                              s_walkmesh.valid ? s_walkmesh.numTriangles : 0,
                              (int)entCount, 0, 0);  // v0.07.83: gateway count always 0 (INF removed)
        }

        s_cachedFieldId = fieldId;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: Exception in field_scripts_init hook (0x%08X)",
                   GetExceptionCode());
    }

    return ret;
}

// ============================================================================
// Public API
// ============================================================================

