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
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "Auto-drive stopped. Entered field but %.0f units from target; not arrival.",
                     s_driveLastDist);
            Log::World("WorldMap: [DRIVE] OFF-TARGET stop (mode=%u MODE_FIELD, target=%s, dist=%.0f > %.0f max [%s], lastPos=(%d,%d), elapsed=%lums) -- not capturing refined coord, not declaring arrival",
                       (unsigned)mode, s_driveTargetName, s_driveLastDist,
                       arrivalCap,
                       s_drivePlannerEligible ? "planner-eligible" : "geometric-trigger",
                       s_driveLastPosX, s_driveLastPosY,
                       (unsigned long)elapsed);
            s_driveAwaitingArrivalDecision = false;
            StopAutoDrive(buf);
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
        if (locIdx >= 0 && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
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
        if (locIdx >= 0 && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
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
