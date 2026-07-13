// world_map_arrival.inl - Deferred arrival state machine (with v0.16.0 Part B)
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// When the world map exits during an active auto-drive, capture the exit
// tick and wait briefly for the game mode register to settle. Then dispatch:
//   - MODE_FIELD (1)   -> ARRIVAL
//   - MODE_SWIRL/BATTLE/AFTER_BATTLE -> ENCOUNTER (paused, resume on world re-entry)
//   - otherwise (incl. lingering MODE_WORLDMAP) -> keep waiting until timeout
// On timeout, fall back to v0.14.95 segment-membership / distance heuristic.
//
// v0.16.0 Part B: when MODE_FIELD is detected, also check that the player's
// last-known world-map position was within DRIVE_ARRIVAL_MAX_DIST (2500) of
// s_driveTarget. If the drive accidentally steered into a different field
// far from the intended target (e.g. v0.15.13.2 Fire Cavern -> bggate_1
// poisoning where the planner's closest-active-region fallback routed the
// player into the wrong field), refuse to capture the refined coord and
// log the off-target stop. Same check applies in the timeout-fallback
// distance branch.
//
// v0.16.0.2: two-tier cap. Geometric-trigger destinations (Fire Cavern,
// Balamb Garden, etc. — anything with s_drivePlannerEligible == false)
// have their world-map icon catalog point intentionally offset from the
// actual terrain trigger by thousands of units. First-time arrivals will
// land at the trigger position with dist far above 2500 from the icon.
// Use DRIVE_ARRIVAL_MAX_DIST_GEOMETRIC (8000) for these so the first
// arrival succeeds and captures a refined coord; subsequent drives target
// the refined position and compute dist near zero, falling back inside
// the strict 2500 cap. Planner-eligible destinations keep the 2500 cap
// because their catalog points are already trigger-aligned via prior BAT.

static constexpr double DRIVE_ARRIVAL_MAX_DIST = 2500.0;           // v0.16.0 Part B (planner-eligible / refined)
static constexpr double DRIVE_ARRIVAL_MAX_DIST_GEOMETRIC = 8000.0; // v0.16.0.2 (planner-ineligible first arrival)

// ============================================================================
// v0.18.3.195: PERSIST captured refined entry coordinates to disk.
// The refined coord captured on first arrival at a location is its REAL field
// trigger (the walkable entrance / walled-town gate / open-town center), which
// is more accurate than the research icon. Keeping it only in memory meant it
// was lost on restart (hence the hard-coded Balamb/Fire-Cavern/Timber seeds).
// Persisting it means every location, once entered even once, is targeted
// directly forever after -- and the file accumulates toward a complete table
// of real entrances that ships with the mod so players never hit this.
// ============================================================================
static void GetRefinedEntriesPath(char* out, size_t n)
{
    char dllPath[MAX_PATH] = {0};
    HMODULE h = GetModuleHandleA("dinput8.dll");
    DWORD got = GetModuleFileNameA(h, dllPath, MAX_PATH);
    if (got == 0 || got >= MAX_PATH) { snprintf(out, n, "ff8opc_refined_entries.txt"); return; }
    char* slash = strrchr(dllPath, '\\');
    if (slash) *(slash + 1) = '\0'; else dllPath[0] = '\0';
    snprintf(out, n, "%sff8opc_refined_entries.txt", dllPath);
}

static void SaveRefinedEntries()
{
    char path[MAX_PATH]; GetRefinedEntriesPath(path, sizeof(path));
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# FF8 accessibility mod: captured world-map entry coordinates (name\\tX\\tY). Auto-generated; safe to share.\n");
    for (int i = 0; i < LOCATION_COUNT; i++)
        if (s_refinedHas[i])
            fprintf(f, "%s\t%d\t%d\n", s_locations[i].name, s_refinedX[i], s_refinedY[i]);
    fclose(f);
}

static void LoadRefinedEntries()
{
    char path[MAX_PATH]; GetRefinedEntriesPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) { Log::World("WorldMap: [INIT] No persisted refined-entries file yet (%s)", path); return; }
    char line[256]; int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char* t1 = strchr(line, '\t'); if (!t1) continue; *t1 = '\0';
        int x = 0, y = 0;
        if (sscanf(t1 + 1, "%d\t%d", &x, &y) != 2) continue;
        for (int i = 0; i < LOCATION_COUNT; i++) {
            if (strcmp(s_locations[i].name, line) == 0) {
                s_refinedX[i] = x; s_refinedY[i] = y; s_refinedHas[i] = true; loaded++;
                break;
            }
        }
    }
    fclose(f);
    Log::World("WorldMap: [INIT] Loaded %d persisted refined entry coordinate(s) from %s", loaded, path);
}

// ============================================================================
// v0.18.3.198: MANUAL-entry capture.
// The drive-based capture in ResolveDeferredArrival only fires during an active
// auto-drive. But the player also enters locations on foot without auto-drive --
// or after AD gives up, as with Galbadia Garden's offset entrance (the .196/.197
// orbit). Record those too: when the world map exits into a FIELD and no drive is
// active, pin the last world-map position as the nearest catalog location's
// refined entry -- but ONLY for locations that don't already have one, so the
// hand-validated seeds (Timber gate, Balamb, Fire Cavern) and prior drive-captured
// coords are never overwritten by a looser manual guess. Catalog locations sit
// kilometres apart, so the nearest-within-3000u match is unambiguous; a genuine
// walk-in lands inside the town footprint right by its icon, while far-off cutscene
// fields (e.g. the forest ~4700u from the G-Garden icon) fall outside the cap and
// are ignored. This lets the real-entrance table fill in simply by playing.
// ============================================================================
static bool     s_manualArrivalPending = false;
static DWORD    s_manualArrivalTick    = 0;
static int32_t  s_lastWorldPosX        = 0;   // updated every world-map frame in Poll()
static int32_t  s_lastWorldPosY        = 0;
static int32_t  s_manualArrivalPosX    = 0;   // snapshot taken at world-map exit
static int32_t  s_manualArrivalPosY    = 0;
static constexpr double MANUAL_CAPTURE_MAX_DIST = 3000.0;

// v0.18.3.199: some catalog icons sit right next to a SEPARATE enterable field
// that distance-based capture cannot tell apart from the location itself.
// Galbadia Garden is the known case -- its world-map icon is beside the train/
// Galbadia station, and both the drive-arrival and manual-entry capture recorded
// the station's coordinate as G-Garden's "entrance" (so the mod then drove to
// G-Garden by going to the station). Exempt such locations from ALL automatic
// capture; their real entrance is hard-seeded from a confirmed walk-in instead.
static bool IsCaptureExempt(int locIdx)
{
    if (locIdx < 0 || locIdx >= LOCATION_COUNT) return false;
    return strcmp(s_locations[locIdx].name, "Galbadia Garden") == 0;
}

static void ResolveManualArrival()
{
    DWORD now = GetTickCount();
    DWORD elapsed = now - s_manualArrivalTick;

    uint16_t mode = 0xFFFF;
    __try {
        if (FF8Addresses::pGameMode) mode = *FF8Addresses::pGameMode;
    } __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0xFFFF; }

    bool isFieldArrival = (mode == FF8Addresses::MODE_FIELD);
    bool isEncounter    = (mode == FF8Addresses::MODE_SWIRL ||
                           mode == FF8Addresses::MODE_BATTLE ||
                           mode == FF8Addresses::MODE_AFTER_BATTLE);

    if (isEncounter) { s_manualArrivalPending = false; return; }  // random battle, not an arrival

    if (isFieldArrival) {
        s_manualArrivalPending = false;
        if (s_manualArrivalPosX == 0 && s_manualArrivalPosY == 0) return;

        // Nearest catalog location to where we left the world map.
        int    best = -1;
        double bestD = 1e18;
        for (int i = 0; i < LOCATION_COUNT; i++) {
            double d = CalculateWrappedDistance(s_manualArrivalPosX, s_manualArrivalPosY,
                                                s_locations[i].x, s_locations[i].y);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best < 0 || bestD > MANUAL_CAPTURE_MAX_DIST) {
            Log::World("WorldMap: [DRIVE] Manual field entry at (%d,%d) -- nearest location %s is %.0fu away (> %.0f), not capturing",
                       s_manualArrivalPosX, s_manualArrivalPosY,
                       best >= 0 ? s_locations[best].name : "<none>", bestD, MANUAL_CAPTURE_MAX_DIST);
            return;
        }
        // v0.18.3.199: log the field identity so an unrecognized adjacent field
        // (e.g. the station beside G-Garden) can be told apart from the real
        // location field, and so a confirmed real entrance can be hard-seeded.
        uint16_t fieldId = 0xFFFF; const char* fieldName = "<unknown>";
        __try { if (FF8Addresses::pCurrentFieldId)   fieldId   = *FF8Addresses::pCurrentFieldId; }   __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { if (FF8Addresses::pCurrentFieldName) fieldName = FF8Addresses::pCurrentFieldName; } __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (IsCaptureExempt(best)) {
            Log::World("WorldMap: [DRIVE] Manual field entry near %s (%.0fu, fieldId=0x%04X '%s', pos=(%d,%d)) -- capture-exempt (adjacent-field ambiguity); NOT recording. If this is the real entrance, hard-seed this position.",
                       s_locations[best].name, bestD, (unsigned)fieldId, fieldName,
                       s_manualArrivalPosX, s_manualArrivalPosY);
            return;
        }
        if (s_refinedHas[best]) {
            Log::World("WorldMap: [DRIVE] Manual field entry near %s (%.0fu) -- already has a refined entry, keeping it",
                       s_locations[best].name, bestD);
            return;
        }
        Log::World("WorldMap: [DRIVE] Manual entry field identity: fieldId=0x%04X '%s' at (%d,%d)",
                   (unsigned)fieldId, fieldName, s_manualArrivalPosX, s_manualArrivalPosY);
        s_refinedX[best]   = s_manualArrivalPosX;
        s_refinedY[best]   = s_manualArrivalPosY;
        s_refinedHas[best] = true;
        Log::World("WorldMap: [DRIVE] Captured refined entry for %s at (%d,%d) via MANUAL walk-in (nearest icon %.0fu)",
                   s_locations[best].name, s_manualArrivalPosX, s_manualArrivalPosY, bestD);
        SaveRefinedEntries();   // persist so it survives restarts + accumulates toward the full table
        return;
    }

    // Mode hasn't settled to a field yet -- keep waiting until the timeout.
    if (elapsed < ARRIVAL_DECISION_TIMEOUT_MS) return;
    s_manualArrivalPending = false;   // gave up; not a clean field arrival
}

// ============================================================================
// v0.14.96: Deferred arrival decision
// ============================================================================
static void ResolveDeferredArrival()
{
    DWORD now = GetTickCount();
    DWORD elapsed = now - s_driveExitTick;

    uint16_t mode = 0xFFFF;
    __try {
        if (FF8Addresses::pGameMode) mode = *FF8Addresses::pGameMode;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode = 0xFFFF;
    }

    bool isFieldArrival = (mode == FF8Addresses::MODE_FIELD);
    bool isEncounter   = (mode == FF8Addresses::MODE_SWIRL ||
                          mode == FF8Addresses::MODE_BATTLE ||
                          mode == FF8Addresses::MODE_AFTER_BATTLE);

    if (isFieldArrival) {
        // v0.16.0 Part B: refuse to capture refined coord / declare arrival when
        // the player's last-known world-map position is far from the intended
        // target. Drives can steer into wrong fields (e.g. v0.15.13.2 Fire
        // Cavern -> bggate_1 where the v0.14.95 closest-active-region fallback
        // misrouted the player). Off-target arrivals would poison s_refined*
        // with garbage and announce false arrivals.
        //
        // v0.16.0.2: cap is tier-dependent. Geometric-trigger destinations
        // (planner-ineligible) use the wider 8000-unit cap on first arrival to
        // accommodate icon-vs-trigger offset; refined coords get captured and
        // subsequent drives fall back inside the strict 2500 cap.
        const double arrivalCap = s_drivePlannerEligible
            ? DRIVE_ARRIVAL_MAX_DIST
            : DRIVE_ARRIVAL_MAX_DIST_GEOMETRIC;
        if (s_driveLastDist > arrivalCap) {
            // v0.18.3.203: off-target field entry = LEARN + PAUSE, not stop. The .202 BAT:
            // the Dollet route clipped the Galbadia Garden 'ggview1' trigger REGION (fired
            // 1815u from its entrance coord -- the whole plateau triggers) and the drive
            // died with "not arrival", twice. Now: (1) attribute the entry to the nearest
            // catalog location and record its observed trigger radius (planner soft-avoids
            // it for every future non-target route); (2) keep the drive alive, paused --
            // the world-map re-entry path already replans and resumes, and the new plan
            // both knows the circle and starts INSIDE it (trigger disarmed), so it walks
            // out cleanly and continues. This is also how through-travel past view-fields
            // is supposed to work: enter, cross, exit, continue.
            int bestLoc = -1; double bestLD = 1e30;
            for (int i = 0; i < LOCATION_COUNT; i++) {
                double d = CalculateWrappedDistance(s_driveLastPosX, s_driveLastPosY,
                                                    s_locations[i].x, s_locations[i].y);
                if (d < bestLD) { bestLD = d; bestLoc = i; }
            }
            const char* locName = "a field";
            if (bestLoc >= 0 && bestLD <= 5000.0) {
                locName = s_locations[bestLoc].name;
                int32_t lx = s_locations[bestLoc].x, ly = s_locations[bestLoc].y;
                int r = (int)bestLD + 192; if (r < 512) r = 512; if (r > 4096) r = 4096;
                int slot = -1;
                for (int i = 0; i < s_trigAvoidN; i++) {
                    if (s_trigAvoidX[i] == lx && s_trigAvoidY[i] == ly) { slot = i; break; }
                }
                if (slot < 0 && s_trigAvoidN < TRIG_AVOID_MAX) {
                    slot = s_trigAvoidN++;
                    s_trigAvoidX[slot] = lx; s_trigAvoidY[slot] = ly; s_trigAvoidR[slot] = 0;
                }
                if (slot >= 0 && r > s_trigAvoidR[slot]) {
                    s_trigAvoidR[slot] = r;
                    Log::World("WorldMap: [TRIGAVOID] learned %s trigger radius %d (entry at (%d,%d), %.0fu from center; %d circles known)",
                               locName, r, s_driveLastPosX, s_driveLastPosY, bestLD, s_trigAvoidN);
                }
            }
            Log::World("WorldMap: [DRIVE] OFF-TARGET field entry (mode=%u MODE_FIELD, target=%s, dist=%.0f > %.0f max [%s], lastPos=(%d,%d), elapsed=%lums) -- PAUSING drive, will resume on world-map re-entry",
                       (unsigned)mode, s_driveTargetName, s_driveLastDist,
                       arrivalCap,
                       s_drivePlannerEligible ? "planner-eligible" : "geometric-trigger",
                       s_driveLastPosX, s_driveLastPosY,
                       (unsigned long)elapsed);
            s_driveAwaitingArrivalDecision = false;
            s_drivePausedInField = true;
            s_drivePauseTick     = now;
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "Entered %s, not the destination. Return to the world map and I'll continue to %s.",
                     locName, s_driveTargetName);
            ScreenReader::Speak(buf, true);
            return;
        }

        // Read field info for diagnostic logging.
        uint16_t fieldId = 0xFFFF;
        const char* fieldName = "<unknown>";
        __try {
            if (FF8Addresses::pCurrentFieldId) fieldId = *FF8Addresses::pCurrentFieldId;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try {
            if (FF8Addresses::pCurrentFieldName) fieldName = FF8Addresses::pCurrentFieldName;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Refined-coord capture.
        int locIdx = FindLocationIndexByTargetCoords(s_driveTargetX, s_driveTargetY);
        if (locIdx >= 0 && IsCaptureExempt(locIdx)) {
            // v0.18.3.199: G-Garden-class location -- the drive can land in an
            // adjacent field (the station) that distance can't distinguish from
            // the real entrance, so recording here poisons the table. Skip it.
            Log::World("WorldMap: [DRIVE] Arrival at %s (fieldId=0x%04X '%s', lastPos=(%d,%d)) -- capture-exempt (adjacent-field ambiguity); NOT recording refined coord. Hard-seed from a confirmed walk-in instead.",
                       s_locations[locIdx].name, (unsigned)fieldId, fieldName,
                       s_driveLastPosX, s_driveLastPosY);
        } else if (locIdx >= 0 && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
            bool wasRefined = s_refinedHas[locIdx];
            s_refinedX[locIdx]   = s_driveLastPosX;
            s_refinedY[locIdx]   = s_driveLastPosY;
            s_refinedHas[locIdx] = true;
            Log::World("WorldMap: [DRIVE] %s refined entry for %s at (%d,%d) (was target=(%d,%d))",
                       wasRefined ? "Updated" : "Captured",
                       s_locations[locIdx].name,
                       s_driveLastPosX, s_driveLastPosY,
                       s_driveTargetX, s_driveTargetY);
            SaveRefinedEntries();   // v0.18.3.195: persist so it survives restarts + accumulates
        }

        char buf[160];
        snprintf(buf, sizeof(buf), "Arrived at %s.", s_driveTargetName);
        Log::World("WorldMap: [DRIVE] Arrival via game-mode (mode=%u MODE_FIELD, fieldId=0x%04X, fieldName='%s', target=%s, dist=%.0f, lastPos=(%d,%d), elapsed=%lums)",
                   (unsigned)mode, (unsigned)fieldId, fieldName,
                   s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   (unsigned long)elapsed);
        s_driveAwaitingArrivalDecision = false;
        StopAutoDrive(buf);
        return;
    }

    if (isEncounter) {
        int pausedCol = WorldXToSegCol(s_driveLastPosX);
        int pausedRow = WorldYToSegRow(s_driveLastPosY);
        uint8_t pausedRegion = 0xFF;
        if (s_segmentRegionLoaded &&
            pausedRow >= 0 && pausedRow < WMX_SEG_ROWS &&
            pausedCol >= 0 && pausedCol < WMX_SEG_COLS) {
            pausedRegion = s_segmentRegionMap[pausedRow][pausedCol];
        }
        const char* modeLabel = (mode == FF8Addresses::MODE_SWIRL)        ? "MODE_SWIRL" :
                                (mode == FF8Addresses::MODE_BATTLE)       ? "MODE_BATTLE" :
                                                                            "MODE_AFTER_BATTLE";
        Log::World("WorldMap: [DRIVE] Paused via game-mode (mode=%u %s, target=%s, dist=%.0f, lastPos=(%d,%d), seg=(%d,%d), region=0x%02X, planned=%d, elapsed=%lums) \u2014 will resume on re-entry",
                   (unsigned)mode, modeLabel,
                   s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   pausedCol, pausedRow, (unsigned)pausedRegion,
                   s_drivePathPlanned ? 1 : 0,
                   (unsigned long)elapsed);
        s_driveAwaitingArrivalDecision = false;
        return;
    }

    // Mode hasn't settled yet — keep waiting.
    if (elapsed < ARRIVAL_DECISION_TIMEOUT_MS) {
        return;
    }

    // Timeout — fall back to segment-membership / distance heuristic.
    Log::World("WorldMap: [DRIVE] Arrival decision timeout (mode=%u after %lums) \u2014 falling back to segment/distance heuristic",
               (unsigned)mode, (unsigned long)elapsed);

    bool arrived = false;
    const char* arrivalReason = "";
    int  arrivedSegRow = -1, arrivedSegCol = -1;

    if (s_drivePathPlanned && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
        arrivedSegRow = WorldYToSegRow(s_driveLastPosY);
        arrivedSegCol = WorldXToSegCol(s_driveLastPosX);
        arrived       = IsGoalSegment(arrivedSegRow, arrivedSegCol);
        arrivalReason = arrived ? "segment-membership (timeout-fallback)" : "";
    } else if (s_driveLastDist > 0 && s_driveLastDist < DRIVE_ARRIVED_ON_EXIT_DIST) {
        // v0.16.0 Part B: same off-target safety check as the MODE_FIELD branch
        // above. DRIVE_ARRIVED_ON_EXIT_DIST (1500) is below
        // DRIVE_ARRIVAL_MAX_DIST (2500), so this branch should always pass;
        // the explicit check preserves the safety contract if anyone later
        // changes the DRIVE_ARRIVED_ON_EXIT_DIST constant.
        //
        // v0.16.0.2: honor the same two-tier cap as the MODE_FIELD branch so
        // geometric-trigger destinations don't get a stricter test here than
        // in the primary path.
        const double timeoutCap = s_drivePlannerEligible
            ? DRIVE_ARRIVAL_MAX_DIST
            : DRIVE_ARRIVAL_MAX_DIST_GEOMETRIC;
        if (s_driveLastDist <= timeoutCap) {
            arrived       = true;
            arrivalReason = "exit-distance (timeout-fallback)";
        } else {
            Log::World("WorldMap: [DRIVE] OFF-TARGET timeout-fallback stop (target=%s, dist=%.0f > %.0f max [%s], lastPos=(%d,%d)) -- not declaring arrival",
                       s_driveTargetName, s_driveLastDist,
                       timeoutCap,
                       s_drivePlannerEligible ? "planner-eligible" : "geometric-trigger",
                       s_driveLastPosX, s_driveLastPosY);
        }
    }

    s_driveAwaitingArrivalDecision = false;

    if (arrived) {
        int locIdx = FindLocationIndexByTargetCoords(s_driveTargetX, s_driveTargetY);
        if (locIdx >= 0 && !IsCaptureExempt(locIdx) && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
            bool wasRefined = s_refinedHas[locIdx];
            s_refinedX[locIdx]   = s_driveLastPosX;
            s_refinedY[locIdx]   = s_driveLastPosY;
            s_refinedHas[locIdx] = true;
            Log::World("WorldMap: [DRIVE] %s refined entry for %s at (%d,%d) (was target=(%d,%d))",
                       wasRefined ? "Updated" : "Captured",
                       s_locations[locIdx].name,
                       s_driveLastPosX, s_driveLastPosY,
                       s_driveTargetX, s_driveTargetY);
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "Arrived at %s.", s_driveTargetName);
        Log::World("WorldMap: [DRIVE] Arrival via %s (target=%s, dist=%.0f, lastPos=(%d,%d), seg=(%d,%d))",
                   arrivalReason, s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   arrivedSegCol, arrivedSegRow);
        StopAutoDrive(buf);
    } else {
        Log::World("WorldMap: [DRIVE] Paused via timeout-fallback (target=%s, dist=%.0f, lastPos=(%d,%d), planned=%d) \u2014 will resume on re-entry",
                   s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   s_drivePathPlanned ? 1 : 0);
    }
}
