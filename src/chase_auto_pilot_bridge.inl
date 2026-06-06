// chase_auto_pilot_bridge.inl -- domt1_1 EAST/WEST kani-leap dance
//
// Textual include from chase_auto_pilot.cpp, after _io.inl and _helpers.inl
// (uses ReadSquallPosition, ReadKaniPosition, and the cached s_bridge*
// state declared in _state.inl). Called from chase_auto_pilot_update.inl
// when the engaged mode is MODE_BRIDGE_DANCE.

// ============================================================================
// v0.15.9.8.3: Bridge dance per-tick update
// ============================================================================
//
// Called once per Update() tick when MODE_BRIDGE_DANCE is the engaged mode
// and the field is domt1_1. Samples the kani's X-position every
// kBridgeSamplePeriodTicks ticks (10 Hz), computes the X-delta vs the
// previous sample, and classifies it as LEAPING / LANDED / CHASING. Drives
// the EAST <-> WEST state machine; on transition, updates s_engagedDirX/Y
// so the per-tick StartDirectionDrive refresh below picks up the new
// direction cleanly.
//
// All thresholds and counters are static-file constants documented above.
// Side effects are limited to module-static state and Log::Field output.
static void UpdateBridgeDance(const char* fieldName)
{
    // 60 Hz dwell counters tick every Update() call regardless of sample
    // cadence -- they govern transition gating, not motion classification.
    s_bridgeTicksSinceXition++;

    // 10 Hz sample gate.
    s_bridgeSampleCounter++;
    if (s_bridgeSampleCounter < kBridgeSamplePeriodTicks) return;
    s_bridgeSampleCounter = 0;

    // Read kani position via ChaseDetector (with v0.15.9.8.3 per-field
    // override applied). A read failure here means the kani entity isn't
    // resolvable yet -- hold state and try again next sample.
    int32_t kX = 0, kY = 0;
    if (!ReadKaniPosition(kX, kY)) {
        Log::Field("BridgeDance: kani read FAILED -- holding state=%s dwell=%d",
                   s_bridgeDanceState == BRIDGE_DANCE_EAST ? "EAST" : "WEST",
                   (int)s_bridgeTicksSinceXition);
        return;
    }

    // Read party position for the "kani ahead of party" predicate.
    int32_t pX = 0, pY = 0;
    bool gotParty = ReadSquallPosition(pX, pY);

    // X-delta vs previous sample. On the first sample of an engagement,
    // there's no previous sample -- delta defaults to 0 (classified as
    // landed but the kBridgeLandConsec debounce + wasLeaping latch prevent
    // any spurious transition).
    int32_t dX = s_bridgeLastKaniValid ? (kX - s_bridgeLastKaniX) : 0;
    int32_t absDx = (dX < 0) ? -dX : dX;

    bool isLeaping = (absDx > kBridgeLeapThreshold);
    bool isLanded  = (absDx < kBridgeLandThreshold);
    bool minDwellMet = (s_bridgeTicksSinceXition >= kBridgeMinDwellTicks);

    // The "was leaping" latch: only set true after observing a leap. Used
    // on the east leg so a landed sample without a preceding leap doesn't
    // trigger the turn-west (the kani is initially stationary west of the
    // party for ~1 second before the chase starts; that's not the
    // landing-in-front signal we're after).
    bool justStartedLeaping = (isLeaping && !s_bridgeWasLeaping);
    if (isLeaping) {
        if (!s_bridgeWasLeaping) {
            s_bridgeLeapCount++;
            Log::Field("BridgeDance: leap #%d STARTED state=%s kani=(%d,%d) "
                       "party=(%d,%d) kdx=%d",
                       s_bridgeLeapCount,
                       s_bridgeDanceState == BRIDGE_DANCE_EAST ? "EAST" : "WEST",
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX);
        }
        s_bridgeWasLeaping = true;
    }

    // ===== State machine =====

    if (s_bridgeDanceState == BRIDGE_DANCE_EAST) {
        // East leg: turn west when kani lands IN FRONT (east of party) AFTER
        // observing a leap. The "after a leap" gate is essential -- the
        // kani spends the first ~12 samples stationary at the far-west spawn
        // position, which would otherwise trigger an immediate turn-west on
        // the very first sample.
        bool kaniAhead = gotParty && (kX > pX);
        if (isLanded && kaniAhead && s_bridgeWasLeaping) {
            s_bridgeConsecLandSamples++;
        } else {
            s_bridgeConsecLandSamples = 0;
        }

        if (minDwellMet &&
            s_bridgeConsecLandSamples >= kBridgeLandConsec) {
            Log::Field("BridgeDance: EAST->WEST transition kani=(%d,%d) party=(%d,%d) "
                       "kdx=%d (landed_in_front for %d samples, leapCount=%d, dwell=%d)",
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX, (int)s_bridgeConsecLandSamples,
                       (int)s_bridgeLeapCount, (int)s_bridgeTicksSinceXition);
            s_engagedDirX            = -1;
            s_engagedDirY            =  0;
            s_bridgeDanceState       = BRIDGE_DANCE_WEST;
            s_bridgeTicksSinceXition = 0;
            s_bridgeConsecLandSamples = 0;
            s_bridgeWasLeaping       = false;
        }
    } else /* BRIDGE_DANCE_WEST */ {
        // West leg: turn east the instant we detect a leap START. The robot
        // is mid-air during a leap and can't course-correct, so this is the
        // safest window to reverse direction and slip past.
        if (minDwellMet && justStartedLeaping) {
            Log::Field("BridgeDance: WEST->EAST transition kani=(%d,%d) party=(%d,%d) "
                       "kdx=%d (leap_start, leapCount=%d, dwell=%d)",
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX, (int)s_bridgeLeapCount, (int)s_bridgeTicksSinceXition);
            s_engagedDirX            = +1;
            s_engagedDirY            =  0;
            s_bridgeDanceState       = BRIDGE_DANCE_EAST;
            s_bridgeTicksSinceXition = 0;
            s_bridgeConsecLandSamples = 0;
            s_bridgeWasLeaping       = false;
        } else if (s_bridgeTicksSinceXition >= kBridgeWestTimeoutTicks) {
            // West-leg safety timeout. If we've been retreating for too long
            // without any leap firing, force a transition back to east. The
            // party then continues toward the east-edge SETLINE; in the worst
            // case we get caught at X~2053 like we did before this dance
            // existed, but at least the bridge progresses.
            Log::Field("BridgeDance: WEST->EAST TIMEOUT (no leap detected after %d ticks) "
                       "kani=(%d,%d) party=(%d,%d) kdx=%d",
                       (int)s_bridgeTicksSinceXition,
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX);
            s_engagedDirX            = +1;
            s_engagedDirY            =  0;
            s_bridgeDanceState       = BRIDGE_DANCE_EAST;
            s_bridgeTicksSinceXition = 0;
            s_bridgeConsecLandSamples = 0;
            s_bridgeWasLeaping       = false;
        }
    }

    s_bridgeLastKaniX     = kX;
    s_bridgeLastKaniValid = true;
}
