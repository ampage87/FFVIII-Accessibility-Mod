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
#if FEPIC1_GATE_DIAG
    s_gateDiagPending    = false;  // re-armed below only if this field is fepic1
#endif
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
    // v0.18.3.227: Clear the per-entity talk-radius capture table for the new
    // field. TALKRADIUS opcodes in this field's init script will repopulate it.
    memset(s_entTalkRadius, 0, sizeof(s_entTalkRadius));
    // v0.18.3.235: Clear the sticky "seen talkable" table for the new field.
    memset(s_entSeenTalkable, 0, sizeof(s_entSeenTalkable));
    // v05.58: ENTDIAG/BGDIAG disabled — keep flags true to skip dumps.
    s_entDiagDumped      = true;
    s_bgDiagDumped       = true;   // v0.18.3.231: BGDIAG done — ruled out the bg array
    s_extScanDumped      = true;   // v0.18.3.234: EXTSCAN done — train staff found
    // v0.18.3.234: re-arm the catalog scan trace ONCE per field load. The [SCAN]/
    // [SCAN-KEEP]/[SCAN-DROP] lines are what made the missing-NPC bugs diagnosable
    // (the drop paths used to discard entities with no log line at all), so they
    // are kept — but RefreshCatalog runs about once a second, so tracing every
    // rebuild would flood the log. Once per field is enough to diagnose.
    s_scanTraced         = false;
    s_mapExitTraced      = false;  // v0.131.8: same rule for InjectMapExits' drops
    s_coordDiagDumped    = true;   // v0.12.11: DISABLED — coordinate diagnostic served its purpose
    s_puzzleDiagDumped   = false;  // v0.18.3.267: re-arm the [PUZZLE-DIAG] dump per field
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
    s_camAxesCorrectionCount  = 0;   // v0.18.3.305 (#109): re-arm the corrector
    s_caRawAngleDeg           = 999.0f;  // v0.18.3.305 (#110): no CA read yet
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

        // #minigame-bgbtl: install the REQ hook ONLY while the Garden-battle
        // mini-game is loaded, and drop it on any other field. REQ is far
        // hotter than SET3, which field_nav_mapjump_diag.inl records as
        // hanging the infirmary cutscene when hooked -- so it never stays
        // installed a moment longer than the one field that needs it.
        GardenBattle::OnFieldLoaded(fieldId);

        // v0.18.3.281 (#85): field-gated re-arm of EXTSCAN. Permanently
        // disabled since v0.18.3.234 ("done -- train staff found" on ggsta1),
        // but glwater1's sewer gates (sakua/sakub/seigyo) are suspected to sit
        // past the reported otherCount the same way (JSM declares O=11
        // "others", engine reports entities=5). Scoped to glwater1 only so it
        // doesn't flood every other field's log; remove this gate once #85 is
        // characterized either way.
        //
        // v0.18.3.285 (#85 correction): the 2026-07-19 short BAT (F11
        // screenshots of the two-gate/valve-wheel/ladder room) proves the
        // actual sewer gate puzzle Aaron plays is 'glwater3' (JSM entities
        // 'saku1'..'saku6', 'water', 'hasigo' -- not glwater1's
        // 'sakua'/'sakub'/'oku', a different, unrelated set of similarly-
        // named entities on a different field). Added glwater3 to the re-arm
        // gate. Kept glwater1 too since its own similarly-shaped gap is still
        // uncharacterized and costs nothing extra to keep watching.
        if (fieldName && (_stricmp(fieldName, "glwater1") == 0 ||
                           _stricmp(fieldName, "glwater3") == 0)) {
            s_extScanDumped = false;
        }

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
            // v0.20.7: precompute duplicated-room phantom-exit suppression for this field.
            ComputeDupRoomSuppression(fieldName);
            // v0.58.0: a REAL field change invalidates the post-battle trigger-line
            // backup. Returning from a battle re-inits the same field and keeps it,
            // which is the one case it exists for.
            if (strncmp(s_currentFieldName, fieldName, 63) != 0) {
                if (s_capBackupCount > 0)
                    Log::Field("FieldNavigation: [fieldload] dropped %d backed-up trigger lines "
                               "on the move from '%s' to '%s' [v0.58.0]",
                               s_capBackupCount, s_currentFieldName, fieldName);
                InvalidateCapturedLineBackup();
            }
            strncpy(s_currentFieldName, fieldName, 63); s_currentFieldName[63] = '\0';  // v0.20.9 diag

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
                        // v0.18.3.305 (#110): record the raw CA angle and log both
                        // snap candidates. LOG-ONLY -- the quantizer's choice below
                        // is unchanged. See field_nav_helpers.inl for why the 45
                        // degree threshold is under suspicion and what the offline
                        // survey of all 894 .ca files established.
                        s_caRawAngleDeg = origAngleDeg;
                        {
                            float nearDeg = 0.0f, altDeg = 0.0f;
                            CamSnapCandidates(origAngleDeg, nearDeg, altDeg);
                            float mod90 = fabsf(origAngleDeg);
                            while (mod90 >= 90.0f) mod90 -= 90.0f;
                            bool flipBand = (mod90 > 30.0f && mod90 < 45.0f);
                            Log::Field("FieldNavigation: [CAM-SNAP] field='%s' rawCA=%+.2fdeg "
                                       "|mod90|=%.2f quantizerChose=%+.0f alternative=%+.0f "
                                       "flipBand=%d (30..45 = would change under a lower "
                                       "threshold) [v0.18.3.305 #110]",
                                       fieldName, origAngleDeg, mod90, nearDeg, altDeg,
                                       flipBand ? 1 : 0);
                        }
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

            // v0.18.3.305 (#109): re-apply this field's previously LEARNED axes,
            // if it has any. Runs last so it overrides whatever the CA file just
            // produced -- a measurement of the engine's actual response beats a
            // derivation from the camera matrix, which is the whole premise of
            // the .304 correction.
            //
            // The observer stays armed afterwards (s_camAxesCorrectionCount is
            // reset to 0 by ResetFieldNavState, and "empirical-cached" is an
            // eligible source), so a cached value that turns out to be wrong is
            // overwritten by the same >= 45 degree contradiction rule that
            // produced it. That self-repair is load-bearing, not decorative:
            // the .304 BAT produced one correct correction and one 180-degree
            // wrong one, and without it the cache would preserve whichever
            // arrived first.
            {
                float cRx, cRy, cDx, cDy;
                if (CamAxesCacheLookup(fieldId, cRx, cRy, cDx, cDy)) {
                    s_camRightX = cRx; s_camRightY = cRy;
                    s_camDownX  = cDx; s_camDownY  = cDy;
                    s_driveCamRightX = cRx; s_driveCamRightY = cRy;
                    s_driveCamDownX  = cDx; s_driveCamDownY  = cDy;
                    s_camAxesSource = "empirical-cached";
                    Log::Field("FieldNavigation: [NAV-CAL] field='%s' (id=%u) cached axes re-applied "
                               "on field load: camRight=(%.3f,%.3f) camDown=(%.3f,%.3f) -- no relearn "
                               "needed, observer stays armed [v0.18.3.305 #109]",
                               fieldName, (unsigned)fieldId, cRx, cRy, cDx, cDy);
                }
            }

            // v0.07.68: JSM script scan — classify all entities by opcode signatures.
            // Detects draw points, save points, shops, card games, ladders, and map exits.
            // v0.07.73: Results wired into catalog for TTS announcements.
            s_jsmEntityCount = 0;
            memset(s_jsmEntities, 0, sizeof(s_jsmEntities));
            memset(s_jsmTriangleApprox, 0, sizeof(s_jsmTriangleApprox));  // v0.18.3.286 (#85)
            memset(s_jsmStateSuppressed, 0, sizeof(s_jsmStateSuppressed));  // v0.18.3.297 (#85)
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

            // v0.18.3.280: captured-line classification + exit-destination
            // resolution + [LINE-PAIR]/[LINEDIAG] diagnostics moved verbatim to
            // field_nav_fieldscripts_linetypes.inl. This file hit 81.7 KB and CI
            // hard-fails over 80 KB. Still executes exactly here, in this scope --
            // the include is a textual splice, not a function call, so it keeps
            // using this function's locals (fieldName, fieldId) and the file-scope
            // nav state. No logic changed in the move.
            #include "field_nav_fieldscripts_linetypes.inl"

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
                            s_jsmEntities[j].hasNearbyInteractionZone = true;  // v0.20.0 (#5):
                            // an INF trigger zone is bound to this entity BY NAME -- a definite
                            // walk-into interaction trigger. Keeps it through the junk-gate.
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

            // v0.101.0 (#derived-pos): the same three fields again, for entities
            // whose position is derivable from the field's OWN SCRIPT but is in
            // none of the files the mod parses. sdcore1's `BossBattle` -- the
            // trigger line that starts the Bahamut scene -- has no SETLINE, no
            // .inf trigger zone and no gateway, so the catalog had nothing to
            // steer Aaron to. The script does: all five party `hanno` methods
            // turn to face (-250, -1161, 550), which lands inside walkmesh
            // triangle 232. See field_nav_derived_pos.inl for the derivation.
            {
                int derivedApplied = 0;
                for (int j2 = 0; j2 < s_jsmEntityCount; j2++) {
                    const NavDerivedPos* row =
                        NavDerivedPosFor((uint16_t)fieldId, s_jsmEntities[j2].symName);
                    if (!NavDerivedShouldApply(s_jsmEntities[j2].hasPosition, row)) continue;
                    s_jsmEntities[j2].posX = row->x;
                    s_jsmEntities[j2].posY = row->y;
                    s_jsmEntities[j2].hasPosition = true;
                    // Same reasoning as the INF trigger zone above: this is a
                    // definite walk-into interaction target, so it keeps its
                    // place through the junk-gate.
                    s_jsmEntities[j2].hasNearbyInteractionZone = true;
                    derivedApplied++;
                    Log::Field("FieldNavigation: [DERIVED-POS] ent%d '%s' type=%s "
                               "pos -> (%d,%d) for field %u (derived from the field's own script)",
                               s_jsmEntities[j2].jsmIndex, s_jsmEntities[j2].symName,
                               FieldArchive::JSMEntityTypeName(s_jsmEntities[j2].type),
                               (int)row->x, (int)row->y, (unsigned)fieldId);
                }
                if (derivedApplied > 0)
                    Log::Field("FieldNavigation: [DERIVED-POS] %d JSM position(s) supplied for field %u",
                               derivedApplied, (unsigned)fieldId);
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
                        je.hasNearbyInteractionZone = true;  // v0.20.0 (#5): a real walk-into
                        // interaction trigger sits at this object (the physical interactable
                        // signal RE'd from the engine). The junk-gate keeps it; the inert
                        // lights (no SETLINE within range) get no flag and are dropped.
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

#if FEPIC1_GATE_DIAG
            // v0.17.9.12 / corrected v0.17.9.13: arm the one-shot push-through
            // gate dump for the B-Garden front gate. The Track A notes called
            // this field 'fepic1' but the live engine name is 'bggate_6'
            // (fieldId 0x00A3, display 'B-Garden - Front Gate 5'); the v0.17.9.12
            // build armed on the wrong name so the dump never fired. Key on the
            // authoritative fieldId. Walkmesh, INF gateways/triggers, JSM scan
            // and captured SETLINE trigger lines are all populated by this point;
            // the dump itself fires from Update() after GATEDIAG_DELAY_MS so the
            // player entity has settled at its spawn triangle.
            if (fieldId == 0x00A3 || _stricmp(fieldName, "bggate_6") == 0) {
                s_gateDiagPending = true;
                s_gateDiagArmTime = GetTickCount();
                Log::Field("FieldNavigation: [GATEDIAG] bggate_6 (front gate, id=0x%04X) detected "
                           "— gate diagnostic armed (fires ~%ums after load).",
                           (unsigned)fieldId, (unsigned)GATEDIAG_DELAY_MS);
            }
#endif

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
