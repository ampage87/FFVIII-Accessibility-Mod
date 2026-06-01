// field_nav_diagnostics.inl — DumpPshmFunctions, PollDescriptorTable
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

static void DumpPshmFunctions()
{
    if (s_pshmFuncDumpDone) return;
    s_pshmFuncDumpDone = true;

    struct DumpTarget {
        const char* name;
        uint32_t addr;
        int size;
    };
    // v0.08.25: Primary target is the PSHM_W handler from the dispatch table.
    // This is the function that actually resolves shared memory reads.
    // Also dump the surrounding POPM_W/PSHM area for call context.
    DumpTarget targets[] = {
        { "pshm_w_handler",         0x0051C5C0, 1024 },  // THE KEY TARGET
        { "entity_scope_curve_sub", 0x00532890,  512 },
        { "type_clamp_dispatch",    0x0051C9C0,  512 },
    };

    for (int t = 0; t < 3; t++) {
        const char* name = targets[t].name;
        uint32_t addr = targets[t].addr;
        int size = targets[t].size;

        Log::Field("FieldNavigation: [FUNCDUMP] === %s @ 0x%08X (%d bytes) ===", name, addr, size);

        __try {
            const uint8_t* code = (const uint8_t*)addr;
            // Log in 32-byte lines: "ADDR: XX XX XX XX ... (32 bytes)"
            for (int offset = 0; offset < size; offset += 32) {
                int lineLen = size - offset;
                if (lineLen > 32) lineLen = 32;
                char hexBuf[200];
                int pos = 0;
                for (int b = 0; b < lineLen; b++) {
                    pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", code[offset + b]);
                }
                Log::Field("FieldNavigation: [FUNCDUMP] %08X: %s", addr + offset, hexBuf);
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Field("FieldNavigation: [FUNCDUMP] EXCEPTION reading %s @ 0x%08X", name, addr);
        }
    }

    // Also log what the dispatch table currently holds for PSHM_W (opcode 0x0C)
    // to confirm whether FFNx has replaced it.
    if (FF8Addresses::pExecuteOpcodeTable) {
        uint32_t tableEntry = FF8Addresses::pExecuteOpcodeTable[0x0C];
        Log::Field("FieldNavigation: [FUNCDUMP] dispatch_table[0x0C] (PSHM_W) = 0x%08X %s",
                   tableEntry,
                   (tableEntry == 0x0051C5C0) ? "(matches hardcoded addr)" :
                   (tableEntry > 0x00600000) ? "(likely FFNx replacement)" : "(game code range)");
        // If FFNx replaced it, also dump the original game code at nearby addresses.
        // The original PSHM_W handler should be near the other opcode handlers (~0x0051xxxx).
        if (tableEntry != 0x0051C5C0 && tableEntry > 0x00600000) {
            Log::Field("FieldNavigation: [FUNCDUMP] FFNx hook detected. Dumping FFNx handler too.");
            __try {
                const uint8_t* code = (const uint8_t*)tableEntry;
                Log::Field("FieldNavigation: [FUNCDUMP] === ffnx_pshm_w_hook @ 0x%08X (256 bytes) ===", tableEntry);
                for (int offset = 0; offset < 256; offset += 32) {
                    char hexBuf[200];
                    int pos = 0;
                    for (int b = 0; b < 32; b++)
                        pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", code[offset + b]);
                    Log::Field("FieldNavigation: [FUNCDUMP] %08X: %s", tableEntry + offset, hexBuf);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("FieldNavigation: [FUNCDUMP] EXCEPTION reading FFNx hook");
            }
        }
    }

    Log::Field("FieldNavigation: [FUNCDUMP] === dump complete ===");
}

// ============================================================================
// v0.08.23: Descriptor table polling probe
// ============================================================================
// Scans the per-entity descriptor pointer table at 0x01DCB340 for non-NULL
// entries. Each entry is a pointer to a ~0x90-byte descriptor struct that
// contains the entity-scope computed coordinates at +0x0C/+0x0E.
// Runs for 10 seconds after field load, checking every ~1 second.

static void PollDescriptorTable()
{
    if (!s_descriptorPollActive) return;
    DWORD now = GetTickCount();

    // Check if polling window has expired.
    if (now - s_descriptorPollStart > DESCRIPTOR_POLL_DURATION_MS) {
        if (!s_descriptorPollSummaryLogged) {
            s_descriptorPollSummaryLogged = true;
            Log::Field("FieldNavigation: [DESCPOLL] Polling complete after %d checks, %dms",
                       s_descriptorPollCount, (int)(now - s_descriptorPollStart));
        }
        s_descriptorPollActive = false;
        return;
    }

    // Throttle to DESCRIPTOR_POLL_INTERVAL_MS.
    if (now - s_descriptorPollLastCheck < DESCRIPTOR_POLL_INTERVAL_MS) return;
    s_descriptorPollLastCheck = now;
    s_descriptorPollCount++;

    int totalEnts = s_jsmDoors + s_jsmLines + s_jsmBackgrounds + s_jsmOthers;
    if (totalEnts <= 0 || totalEnts > MAX_DESCRIPTOR_SCAN) {
        Log::Field("FieldNavigation: [DESCPOLL] #%d totalEnts=%d (skipped, out of range 1-%d)",
                   s_descriptorPollCount, totalEnts, MAX_DESCRIPTOR_SCAN);
        return;
    }

    __try {
        uint32_t* table = (uint32_t*)DESCRIPTOR_TABLE_ADDR;
        int foundCount = 0;

        for (int i = 0; i < totalEnts; i++) {
            uint32_t descPtr = table[i];
            if (descPtr == 0) continue;

            // Non-NULL descriptor found — read key fields.
            foundCount++;
            __try {
                uint8_t* desc = (uint8_t*)descPtr;
                int32_t  validity = *(int32_t*)(desc + 0x00);
                int16_t  coordX   = *(int16_t*)(desc + 0x0C);
                int16_t  coordY   = *(int16_t*)(desc + 0x0E);
                uint32_t curvePtr = *(uint32_t*)(desc + 0x68);
                int16_t  cacheKey = *(int16_t*)(desc + 0x7E);

                // Dump first 16 bytes of descriptor for analysis.
                uint8_t hdr[16];
                memcpy(hdr, desc, 16);

                // Get SYM name for this flat entity index.
                const char* symName = (i < s_symNameCount) ? s_symNames[i] : "?";

                Log::Field("FieldNavigation: [DESCPOLL] #%d ent%d '%s' ptr=0x%08X "
                           "valid=%d coords=(%d,%d) curve=0x%08X cacheKey=%d "
                           "hdr=[%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X]",
                           s_descriptorPollCount, i, symName, descPtr,
                           validity, (int)coordX, (int)coordY, curvePtr, (int)cacheKey,
                           hdr[0], hdr[1], hdr[2], hdr[3],
                           hdr[4], hdr[5], hdr[6], hdr[7],
                           hdr[8], hdr[9], hdr[10], hdr[11],
                           hdr[12], hdr[13], hdr[14], hdr[15]);
            }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("FieldNavigation: [DESCPOLL] #%d ent%d ptr=0x%08X EXCEPTION reading descriptor",
                           s_descriptorPollCount, i, descPtr);
            }
        }

        if (foundCount == 0) {
            Log::Field("FieldNavigation: [DESCPOLL] #%d all %d descriptor ptrs NULL (elapsed %dms)",
                       s_descriptorPollCount, totalEnts, (int)(now - s_descriptorPollStart));
        } else {
            Log::Field("FieldNavigation: [DESCPOLL] #%d found %d/%d non-NULL descriptors",
                       s_descriptorPollCount, foundCount, totalEnts);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [DESCPOLL] #%d EXCEPTION reading descriptor table at 0x%08X",
                   s_descriptorPollCount, DESCRIPTOR_TABLE_ADDR);
        s_descriptorPollActive = false;
    }
}

// v0.12.22: Varblock poller — automatic polling of shared memory varblock
// after field load. Reads varblock[0..799] as int16 every 1s for 10s.
// When the Director entity activates (normal gameplay door transition),
// its POPM_W writes populate interaction zone positions. We capture them here.
// Confirmed by disassembly: PSHM_W(param) = read uint16 at 0x1CFE9B8 + param (byte offset).
static const uint32_t VARBLOCK_BASE_ADDR = 0x1CFE9B8;
static const int VARBLOCK_POLL_RANGE = 800;  // scan first 800 bytes as int16
static const DWORD VARBLOCK_POLL_DURATION_MS = 10000;
static const DWORD VARBLOCK_POLL_INTERVAL_MS = 1000;

static void PollVarblock()
{
    if (!s_varblockPollActive) return;

    DWORD now = GetTickCount();

    // Duration expired?
    if (now - s_varblockPollStart > VARBLOCK_POLL_DURATION_MS) {
        Log::Field("FieldNavigation: [VBPOLL] === COMPLETE === %d polls done",
                   s_varblockPollCount);
        s_varblockPollActive = false;
        return;
    }

    // Interval check
    if (now - s_varblockPollLastCheck < VARBLOCK_POLL_INTERVAL_MS) return;
    s_varblockPollLastCheck = now;
    s_varblockPollCount++;

    __try {
        const uint8_t* vbBase = (const uint8_t*)VARBLOCK_BASE_ADDR;
        int nonZero = 0;
        char summary[2048] = {};
        int sp = 0;

        for (int offset = 0; offset < VARBLOCK_POLL_RANGE; offset += 2) {
            int16_t val = *(const int16_t*)(vbBase + offset);
            if (val != 0) {
                nonZero++;
                if (sp < 1900)
                    sp += snprintf(summary + sp, 2048 - sp, "%d=%d ", offset, (int)val);
            }
        }

        Log::Field("FieldNavigation: [VBPOLL] poll#%d t=+%ds nonZero=%d%s",
                   s_varblockPollCount,
                   (int)((now - s_varblockPollStart) / 1000),
                   nonZero,
                   nonZero > 0 ? " FOUND VALUES" : "");
        if (nonZero > 0)
            Log::Field("FieldNavigation: [VBPOLL]   %s", summary);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [VBPOLL] Exception reading varblock");
        s_varblockPollActive = false;
    }
}

#if FEPIC1_GATE_DIAG
// ============================================================================
// v0.17.9.12: fepic1 push-through gate diagnostic (Track A).
// ============================================================================
//
// One-shot dump, armed in HookedFieldScriptsInit when field 'fepic1' loads,
// fired once from Update() after GATEDIAG_DELAY_MS. Behaviour-neutral.
//
// Decides why F9 auto-drive can't route through fepic1's scripted gate by
// distinguishing three hypotheses:
//   (a) TRUE WALL  -- the exit triangle is in a different connected component
//                     from the player's spawn triangle. A* genuinely can't get
//                     there; needs a scripted-teleport routing concept.
//   (b) MISSED/NARROW TRIANGLE -- connected, but the only portal is narrower
//                     than MIN_EDGE_WIDTH so A* refuses it (smaller fix).
//   (c) TRIGGER-LINE BLOCK -- connected, but a SCREEN_BOUND trigger line
//                     crosses the only portal so IsSeparatedByTriggerLine
//                     makes A* refuse it.
//
// The decisive datum is connected-component labeling of the walkmesh plus,
// per exit, AreTrianglesConnected(spawn, exit) and IsSeparatedByTriggerLine.
static void DumpGateDiagnostic()
{
    __try {
        if (!s_walkmesh.valid || s_walkmesh.numTriangles <= 0) {
            Log::Field("FieldNavigation: [GATEDIAG] walkmesh not valid; aborting dump.");
            return;
        }
        int numTri = s_walkmesh.numTriangles;
        if (numTri > 4096) numTri = 4096;

        // --- 1. Connected-component labeling (BFS flood-fill over neighbors) ---
        static int16_t  comp[4096];
        static uint16_t bfsQ[4096];
        for (int i = 0; i < numTri; i++) comp[i] = -1;
        int numComponents = 0;
        int compSize[64] = {};
        for (int seed = 0; seed < numTri; seed++) {
            if (comp[seed] != -1) continue;
            int cid = numComponents++;
            int qH = 0, qT = 0;
            bfsQ[qT++] = (uint16_t)seed;
            comp[seed] = (int16_t)cid;
            int sz = 0;
            while (qH < qT) {
                uint16_t cur = bfsQ[qH++];
                sz++;
                for (int e = 0; e < 3; e++) {
                    uint16_t nb = s_walkmesh.triangles[cur].neighbor[e];
                    if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
                    if (comp[nb] != -1) continue;
                    comp[nb] = (int16_t)cid;
                    if (qT < 4096) bfsQ[qT++] = nb;
                }
            }
            if (cid < 64) compSize[cid] = sz;
        }
        Log::Field("FieldNavigation: [GATEDIAG] === bggate_6 (B-Garden front gate) push-through gate diagnostic ===");
        Log::Field("FieldNavigation: [GATEDIAG] walkmesh: %d vertices, %d triangles, %d connected components",
                   s_walkmesh.numVertices, s_walkmesh.numTriangles, numComponents);
        {
            char buf[1024]; int p = 0;
            for (int c = 0; c < numComponents && c < 64; c++) {
                int w = snprintf(buf + p, sizeof(buf) - p, "comp%d=%d ", c, compSize[c]);
                if (w < 0 || w >= (int)sizeof(buf) - p) break;
                p += w;
            }
            Log::Field("FieldNavigation: [GATEDIAG] component sizes (first <=64): %s", buf);
        }

        // --- 2. Player spawn position / triangle / component ---
        float px = 0, py = 0; int spawnTri = -1; int spawnComp = -1;
        bool havePlayer = (s_playerEntityIdx >= 0) && GetEntityPos(s_playerEntityIdx, px, py);
        if (havePlayer) {
            spawnTri = FindNearestTriangle(px, py);
            if (spawnTri >= 0 && spawnTri < numTri) spawnComp = comp[spawnTri];
        }
        Log::Field("FieldNavigation: [GATEDIAG] player spawn: ent%d pos=(%.0f,%.0f) tri=%d comp=%d %s",
                   s_playerEntityIdx, px, py, spawnTri, spawnComp,
                   havePlayer ? "" : "(player pos UNAVAILABLE)");

        // --- 3. Exits: INF gateways (spawn-relative reachability + trigger separation) ---
        Log::Field("FieldNavigation: [GATEDIAG] --- INF gateways (%d) ---", s_gatewayCount);
        for (int g = 0; g < s_gatewayCount; g++) {
            float gx = s_gateways[g].centerX;
            float gy = s_gateways[g].centerZ;   // centerZ = screen-Y in our coords
            int gtri = FindNearestTriangle(gx, gy);
            int gcomp = (gtri >= 0 && gtri < numTri) ? comp[gtri] : -1;
            bool conn = (spawnTri >= 0 && gtri >= 0) ? AreTrianglesConnected(spawnTri, gtri) : false;
            bool sep  = havePlayer ? IsSeparatedByTriggerLine(px, py, gx, gy, -1) : false;
            Log::Field("FieldNavigation: [GATEDIAG]  gw[%d] dest=%u '%s' center=(%.0f,%.0f) "
                       "line=(%d,%d)->(%d,%d) tri=%d comp=%d | connected=%s triggerSep=%s",
                       g, (unsigned)s_gateways[g].destFieldId, s_gateways[g].destFieldName,
                       gx, gy,
                       (int)s_gateways[g].lineX1, (int)s_gateways[g].lineY1,
                       (int)s_gateways[g].lineX2, (int)s_gateways[g].lineY2,
                       gtri, gcomp,
                       conn ? "YES" : "NO", sep ? "YES(blocked)" : "no");
        }

        // --- 3b. Exits: captured SETLINE trigger lines (SCREEN_BOUND = real exits) ---
        Log::Field("FieldNavigation: [GATEDIAG] --- captured trigger lines (%d) ---", s_capturedLineCount);
        for (int t = 0; t < s_capturedLineCount; t++) {
            const CapturedTriggerLine& L = s_capturedLines[t];
            float cx = ((float)L.x1 + (float)L.x2) * 0.5f;
            float cy = ((float)L.y1 + (float)L.y2) * 0.5f;
            int ltri = FindNearestTriangle(cx, cy);
            int lcomp = (ltri >= 0 && ltri < numTri) ? comp[ltri] : -1;
            bool conn = (spawnTri >= 0 && ltri >= 0) ? AreTrianglesConnected(spawnTri, ltri) : false;
            Log::Field("FieldNavigation: [GATEDIAG]  line[%d] '%s' type=%s dest=%d active=%d "
                       "end=(%d,%d)->(%d,%d) center=(%.0f,%.0f) tri=%d comp=%d connected=%s",
                       t, L.name, FieldArchive::JSMEntityTypeName(L.lineType),
                       L.destFieldId, L.active ? 1 : 0,
                       (int)L.x1, (int)L.y1, (int)L.x2, (int)L.y2, cx, cy, ltri, lcomp,
                       conn ? "YES" : "NO");
        }

        // --- 4. INF trigger zones (push-through proximity-trigger candidates) ---
        Log::Field("FieldNavigation: [GATEDIAG] --- INF trigger zones (%d) ---", s_triggerCount);
        for (int t = 0; t < s_triggerCount; t++) {
            const FieldArchive::TriggerInfo& T = s_triggers[t];
            float cx = T.centerX, cy = T.centerZ;
            int ttri = FindNearestTriangle(cx, cy);
            int tcomp = (ttri >= 0 && ttri < numTri) ? comp[ttri] : -1;
            Log::Field("FieldNavigation: [GATEDIAG]  trig[%d] '%s' ent=%u type=%u "
                       "center=(%.0f,%.0f) p1=(%d,%d,%d) p2=(%d,%d,%d) tri=%d comp=%d",
                       T.triggerIdx, T.symName, (unsigned)T.entityIndex, (unsigned)T.interactionType,
                       cx, cy,
                       (int)T.x1, (int)T.y1, (int)T.z1, (int)T.x2, (int)T.y2, (int)T.z2,
                       ttri, tcomp);
        }

        // --- 5. Positioned / SETLINE JSM entities (identify the gate trigger entity) ---
        Log::Field("FieldNavigation: [GATEDIAG] --- JSM entities w/ position or setline (of %d) ---",
                   s_jsmEntityCount);
        for (int j = 0; j < s_jsmEntityCount; j++) {
            const FieldArchive::JSMEntityInfo& E = s_jsmEntities[j];
            if (!E.hasPosition && !E.hasSetline) continue;
            int etri = -1, ecomp = -1;
            if (E.hasPosition) {
                etri = FindNearestTriangle((float)E.posX, (float)E.posY);
                if (etri >= 0 && etri < numTri) ecomp = comp[etri];
            }
            Log::Field("FieldNavigation: [GATEDIAG]  jsm%d '%s' cat=%d type=%s "
                       "pos=%s(%d,%d) tri=%d comp=%d talk=%d setline=%s",
                       E.jsmIndex, E.symName, E.jsmCategory, FieldArchive::JSMEntityTypeName(E.type),
                       E.hasPosition ? "" : "none", (int)E.posX, (int)E.posY, etri, ecomp,
                       E.hasTalkSetup ? 1 : 0, E.hasSetline ? "yes" : "no");
            if (E.hasSetline) {
                Log::Field("FieldNavigation: [GATEDIAG]      setline end=(%d,%d)->(%d,%d)",
                           (int)E.setlineX1, (int)E.setlineY1, (int)E.setlineX2, (int)E.setlineY2);
            }
        }

        // --- 6. Full per-triangle dump (geometry + walls + component) ---
        int dumpCap = (numTri < 1200) ? numTri : 1200;
        Log::Field("FieldNavigation: [GATEDIAG] --- per-triangle walkmesh (showing %d of %d) ---",
                   dumpCap, s_walkmesh.numTriangles);
        for (int i = 0; i < dumpCap; i++) {
            const FieldArchive::WalkmeshTriangle& T = s_walkmesh.triangles[i];
            int vi0 = T.vertexIdx[0], vi1 = T.vertexIdx[1], vi2 = T.vertexIdx[2];
            int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0, vx2 = 0, vy2 = 0;
            if (vi0 >= 0 && vi0 < s_walkmesh.numVertices) { vx0 = s_walkmesh.vertices[vi0].x; vy0 = s_walkmesh.vertices[vi0].y; }
            if (vi1 >= 0 && vi1 < s_walkmesh.numVertices) { vx1 = s_walkmesh.vertices[vi1].x; vy1 = s_walkmesh.vertices[vi1].y; }
            if (vi2 >= 0 && vi2 < s_walkmesh.numVertices) { vx2 = s_walkmesh.vertices[vi2].x; vy2 = s_walkmesh.vertices[vi2].y; }
            char n0[8], n1[8], n2[8];
            uint16_t nb0 = T.neighbor[0], nb1 = T.neighbor[1], nb2 = T.neighbor[2];
            if (nb0 == 0xFFFF) snprintf(n0, sizeof(n0), "WALL"); else snprintf(n0, sizeof(n0), "%u", (unsigned)nb0);
            if (nb1 == 0xFFFF) snprintf(n1, sizeof(n1), "WALL"); else snprintf(n1, sizeof(n1), "%u", (unsigned)nb1);
            if (nb2 == 0xFFFF) snprintf(n2, sizeof(n2), "WALL"); else snprintf(n2, sizeof(n2), "%u", (unsigned)nb2);
            Log::Field("FieldNavigation: [GATEDIAG]  tri[%d] c=(%.0f,%.0f) v=(%d,%d)(%d,%d)(%d,%d) nb=[%s,%s,%s] comp=%d",
                       i, T.centerX, T.centerY,
                       vx0, vy0, vx1, vy1, vx2, vy2,
                       n0, n1, n2, comp[i]);
        }

        Log::Field("FieldNavigation: [GATEDIAG] === end bggate_6 gate diagnostic ===");
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [GATEDIAG] EXCEPTION during dump (0x%08X)", GetExceptionCode());
    }
}

// ============================================================================
// v0.17.9.16.1: Turnstile path tracer (Track A Step 3). LOCAL diagnostic.
// ============================================================================
//
// Captures the player's MANUAL walk-through path at the bggate_6 front-gate
// turnstiles so the auto-drive thread-the-needle fix can be built from real
// coordinates instead of guessing the slot geometry. Behaviour-neutral.
//
// Active ONLY in bggate_6 (field 0x00A3) AND only while auto-drive is OFF
// (s_driveActive / s_chaseDriveActive both false) -- we want the hand-walked
// path, not the AD attempts (already captured by the [drive] block). Called
// every Update tick from the FEPIC1_GATE_DIAG block in Update().
//
// Logs two things:
//   [TTRACE] pos=(x,y) tri=N        -- player world pos + nearest triangle,
//                                      throttled to ~10 Hz and only when moving
//                                      (>5 units) so standing still doesn't spam.
//   [TTRACE] CROSSED line[i] ...     -- fires the instant the player's side of
//                                      any captured trigger line flips sign
//                                      (segment cross-product), naming the line
//                                      index/type/endpoints. This is how we see
//                                      which interaction line ('squall'/'squalls')
//                                      the player threads for each turnstile and
//                                      where (so the fix can aim at it).
// Crossing detection runs every tick (not throttled) so a fast cross isn't
// missed; the position heartbeat is throttled.
static DWORD s_ttLastSample = 0;
static float s_ttLastX = 1e30f, s_ttLastY = 1e30f;
static float s_ttLineSide[MAX_CAPTURED_LINES] = {};
static bool  s_ttSideInit = false;
static const DWORD TT_SAMPLE_MS = 100;   // ~10 Hz position heartbeat

static void TurnstileTrace()
{
    // Gate: bggate_6 only, auto-drive OFF only.
    if (!FF8Addresses::pCurrentFieldId || *FF8Addresses::pCurrentFieldId != 0x00A3) {
        s_ttSideInit = false;      // reset crossing baseline + heartbeat on leave
        s_ttLastX = s_ttLastY = 1e30f;
        return;
    }
    if (s_driveActive || s_chaseDriveActive) return;
    if (s_playerEntityIdx < 0) return;

    float px = 0, py = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return;

    int nLines = (s_capturedLineCount < MAX_CAPTURED_LINES) ? s_capturedLineCount
                                                            : MAX_CAPTURED_LINES;

    // --- Crossing detection (every tick) ---
    // side = (lineEnd - lineStart) x (player - lineStart); sign = which side.
    if (!s_ttSideInit) {
        for (int t = 0; t < nLines; t++) {
            float ldx = (float)s_capturedLines[t].x2 - (float)s_capturedLines[t].x1;
            float ldy = (float)s_capturedLines[t].y2 - (float)s_capturedLines[t].y1;
            s_ttLineSide[t] = ldx * (py - (float)s_capturedLines[t].y1)
                            - ldy * (px - (float)s_capturedLines[t].x1);
        }
        s_ttSideInit = true;
    } else {
        for (int t = 0; t < nLines; t++) {
            float ldx = (float)s_capturedLines[t].x2 - (float)s_capturedLines[t].x1;
            float ldy = (float)s_capturedLines[t].y2 - (float)s_capturedLines[t].y1;
            float side = ldx * (py - (float)s_capturedLines[t].y1)
                       - ldy * (px - (float)s_capturedLines[t].x1);
            if (s_ttLineSide[t] != 0.0f && s_ttLineSide[t] * side < 0.0f) {
                int tri = FindNearestTriangle(px, py);
                Log::Field("FieldNavigation: [TTRACE] CROSSED line[%d] '%s' type=%s active=%d "
                           "at player=(%.0f,%.0f) tri=%d end=(%d,%d)->(%d,%d)",
                           t, s_capturedLines[t].name,
                           FieldArchive::JSMEntityTypeName(s_capturedLines[t].lineType),
                           s_capturedLines[t].active ? 1 : 0,
                           px, py, tri,
                           (int)s_capturedLines[t].x1, (int)s_capturedLines[t].y1,
                           (int)s_capturedLines[t].x2, (int)s_capturedLines[t].y2);
            }
            s_ttLineSide[t] = side;
        }
    }

    // --- Position heartbeat (~10 Hz, only when moving) ---
    DWORD now = GetTickCount();
    if (now - s_ttLastSample < TT_SAMPLE_MS) return;
    s_ttLastSample = now;
    float mdx = px - s_ttLastX, mdy = py - s_ttLastY;
    if (s_ttLastX < 1e29f && (mdx * mdx + mdy * mdy) < 25.0f) return;  // moved <5 units
    s_ttLastX = px; s_ttLastY = py;
    int tri = FindNearestTriangle(px, py);
    Log::Field("FieldNavigation: [TTRACE] pos=(%.0f,%.0f) tri=%d", px, py, tri);
}
#endif  // FEPIC1_GATE_DIAG

