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
    s_coordPrevPlayerTri = 0;       // v06.13: reset for shared-edge CoordSample
    // v06.14: Reset heading calibration for new field.
    s_camRightX = 1.0f; s_camRightY = 0.0f;
    s_camDownX = 0.0f; s_camDownY = -1.0f;  // default: -Y world = screen-down (matches common calibration result)
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
                    if (setpc == 0) {
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
                    s_capturedLines[t].destFieldId = -1;  // v0.07.83
                    // Map captured line t to JSM Line entity at jsmDoors + t.
                    // Line entities in JSM: indices [jsmDoors .. jsmDoors+jsmLines-1].
                    int jsmIdx = s_jsmDoors + t;
                    if (jsmIdx < s_jsmEntityCount &&
                        s_jsmEntities[jsmIdx].jsmCategory == 1) {  // category 1 = Line
                        s_capturedLines[t].lineType = s_jsmEntities[jsmIdx].type;
                        // v0.07.83: Capture MAPJUMP destination for screen boundary lines.
                        if (s_jsmEntities[jsmIdx].type == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                            s_capturedLines[t].destFieldId = s_jsmEntities[jsmIdx].param;
                        }
                        linesMapped++;
                    }
                }
                // Log the mapping results.
                int cameraPans = 0, screenBounds = 0, lineEvents = 0, lineUnknown = 0;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    switch (s_capturedLines[t].lineType) {
                        case FieldArchive::JSM_ENT_LINE_CAMERA_PAN:   cameraPans++; break;
                        case FieldArchive::JSM_ENT_LINE_SCREEN_BOUND: screenBounds++; break;
                        case FieldArchive::JSM_ENT_LINE_EVENT:        lineEvents++; break;
                        default: lineUnknown++; break;
                    }
                }
                Log::Field("FieldNavigation: [fieldload] lineType assigned: %d captured, %d mapped "
                           "(camPan=%d screenBd=%d event=%d unknown=%d)",
                           s_capturedLineCount, linesMapped,
                           cameraPans, screenBounds, lineEvents, lineUnknown);
                // Detailed per-line log for first few fields.
                for (int t = 0; t < s_capturedLineCount; t++) {
                    int jsmIdx = s_jsmDoors + t;
                    const char* typeName = (jsmIdx < s_jsmEntityCount)
                        ? FieldArchive::JSMEntityTypeName(s_jsmEntities[jsmIdx].type) : "(no JSM)";
                    Log::Field("FieldNavigation: [fieldload]   line%d order=%d type=%s center=(%.0f,%.0f)",
                               t, s_capturedLines[t].lineOrder, typeName,
                               (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f,
                               (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f);
                }
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

