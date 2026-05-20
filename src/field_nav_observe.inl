// field_nav_observe.inl - Passive observation of engine's actual response to arrow keys.
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.17.3: Diagnostic-only. NO behavior change. Logs the world-space delta
// the player moved when a single arrow key was held for long enough to produce
// measurable movement, alongside the .ca-derived camRight/camDown prediction
// for what direction that arrow SHOULD have moved them. The divergence angle
// shows directly whether the CA-file values match the engine's actual screen-
// to-world projection on the current field.
//
// Background: v0.17.1 / v0.17.2 BAT cycles ruled out hypothesis A (calibration
// corrupting manual nav). Hypothesis B is now in play: .ca-derived axes don't
// agree with the engine's actual analog-to-walkmesh transform on every field.
// The chase auto-pilot's empirical calibration proved that EMPIRICAL axes are
// always correct (it injects analog and measures walkmesh delta, same as what
// this observer measures from Aaron's actual keypresses). The open question is
// what transformation -- if any -- carries CA values to the engine's actual
// projection. This observer gathers the data to answer that.
//
// The observer self-gates: it samples only when no auto-drive is active (so
// auto-drive's synthetic injection can't pollute the sample), no chase drive
// is active, the player entity is detected, no dialog is open, and exactly
// one arrow is held (diagonals are ambiguous between the two single-axis
// predictions). It throttles to one log line per OBS_LOG_THROTTLE_MS so the
// log doesn't explode during a long walking session.

// Observer state (file-scope statics).
static uint8_t  s_obsPrevArrows    = 0;        // arrow bitmask from previous tick
static int      s_obsHoldTicks     = 0;        // ticks the current single arrow has been held
static float    s_obsStartPosX     = 0.0f;     // player position when the current single arrow started
static float    s_obsStartPosY     = 0.0f;
static DWORD    s_obsLastLogTick   = 0;        // GetTickCount() of last sample logged (throttle)
static const int   OBS_HOLD_THRESHOLD_TICKS = 18;     // ~300ms at 60fps; enough to settle
static const float OBS_MOVE_THRESHOLD       = 50.0f;  // world units; below this is noise
static const DWORD OBS_LOG_THROTTLE_MS      = 1500;   // 1.5s between consecutive samples

// Returns popcount of an 8-bit bitmask.
static int ObsBitCount(uint8_t v) {
    int n = 0;
    while (v) { n += (v & 1); v >>= 1; }
    return n;
}

// Read current physical arrow keys into a DIR_* bitmask. Uses GetAsyncKeyState
// rather than the engine's keyboard buffer because the observer only fires
// when auto-drive is inactive -- so synthetic injection isn't a confounder and
// GetAsyncKeyState directly reflects Aaron's physical key presses regardless
// of any in-engine remapping.
static uint8_t ObsReadArrows() {
    uint8_t m = 0;
    if (GetAsyncKeyState(VK_UP)    & 0x8000) m |= DIR_UP;
    if (GetAsyncKeyState(VK_DOWN)  & 0x8000) m |= DIR_DOWN;
    if (GetAsyncKeyState(VK_LEFT)  & 0x8000) m |= DIR_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) m |= DIR_RIGHT;
    return m;
}

static const char* ObsArrowName(uint8_t m) {
    switch (m) {
        case DIR_UP:    return "UP";
        case DIR_DOWN:  return "DOWN";
        case DIR_LEFT:  return "LEFT";
        case DIR_RIGHT: return "RIGHT";
        default:        return "?";
    }
}

// v0.17.7.6: Closed-loop empirical calibration thresholds.
//
// EMPIRICAL_MIN_SAMPLES = require this many consecutive in-buffer samples
// for the same arrow before considering consensus.
//
// v0.17.7.6.1: lowered from 3 to 2. Background: v0.17.7.6 BAT on bgroad_5
// showed the user-facing wrong-direction window was driven by the gap
// between field load and first calibration. Each NAV-OBSERVE sample takes
// ~1.5 seconds (throttle) plus the 18-tick hold floor plus 100-unit
// movement floor; 3 samples means ~5 seconds minimum even under ideal
// conditions. With the strict per-sample filters (single-arrow only,
// ~6deg residual noise floor empirically, 100-unit movement) two samples
// already give high consensus confidence -- bgroad_5 samples landed at
// exactly (1.000, 0.000) both times. The remaining risk (a single bad
// sample applying wrong correction) is bounded by the consensus check
// itself: two random samples both within 10 degrees of their mean is
// rare. Two samples is enough to filter the rarer of those failure modes
// (player-on-platform, scripted-nudge) while halving time-to-correction.
static const int   EMPIRICAL_MIN_SAMPLES   = 2;
//
// EMPIRICAL_AGREEMENT_RAD = max angular deviation between the mean
// direction and any individual sample. 10 degrees is wider than the
// ~1-2 degree noise floor observed in v0.17.3 NAV-OBSERVE logs but
// narrow enough to reject genuine non-cardinal motion.
//
// OBS_EMPIRICAL_MIN_MAG = minimum measured-delta magnitude (in world
// units) before a sample is admitted to the buffer. Defense-in-depth
// against stuck-on-wall noise; the upstream OBS_MOVE_THRESHOLD of 50
// already filters most of this but the calibration is permanent so
// the bar is higher.
static const float EMPIRICAL_AGREEMENT_RAD = 10.0f * (float)NAV_PI / 180.0f;
static const float OBS_EMPIRICAL_MIN_MAG   = 100.0f;

// Map an arrow bitmask to a buffer index 0..3. Returns -1 for non-single-
// arrow inputs. The caller in ObsLogSample has already gated on single-
// arrow, but this defense lets us reuse the helper safely.
static int ObsArrowToIdx(uint8_t arrow) {
    switch (arrow) {
        case DIR_UP:    return 0;
        case DIR_DOWN:  return 1;
        case DIR_LEFT:  return 2;
        case DIR_RIGHT: return 3;
        default:        return -1;
    }
}

// Push a normalized world-direction sample into the matching arrow's
// ring buffer. When the buffer is full, the oldest entry is dropped
// (shift-left) so the consensus check always operates on the most
// recent OBS_MAX_SAMPLES measurements.
static void ObsPushSample(int arrowIdx, float wx, float wy) {
    int n = s_navObsSampleCount[arrowIdx];
    if (n >= OBS_MAX_SAMPLES) {
        for (int i = 1; i < OBS_MAX_SAMPLES; i++) {
            s_navObsBuffer[arrowIdx][i - 1] = s_navObsBuffer[arrowIdx][i];
        }
        n = OBS_MAX_SAMPLES - 1;
    }
    s_navObsBuffer[arrowIdx][n].wx = wx;
    s_navObsBuffer[arrowIdx][n].wy = wy;
    s_navObsSampleCount[arrowIdx] = n + 1;
}

// Check whether the last EMPIRICAL_MIN_SAMPLES entries in this arrow's
// buffer agree on a common direction within EMPIRICAL_AGREEMENT_RAD.
// Returns true and writes the consensus unit vector to (outX, outY) on
// success; returns false otherwise.
//
// Method: sum the unit vectors, normalize -> mean direction. Then verify
// every sample's dot product with the mean exceeds cos(threshold). If the
// samples are ~180-degree opposite (e.g. player walked back and forth),
// the mean magnitude approaches zero and we bail.
static bool ObsCheckConsensus(int arrowIdx, float& outX, float& outY) {
    int n = s_navObsSampleCount[arrowIdx];
    if (n < EMPIRICAL_MIN_SAMPLES) return false;
    int firstIdx = n - EMPIRICAL_MIN_SAMPLES;
    float sx = 0.0f, sy = 0.0f;
    for (int i = firstIdx; i < n; i++) {
        sx += s_navObsBuffer[arrowIdx][i].wx;
        sy += s_navObsBuffer[arrowIdx][i].wy;
    }
    float meanLen = sqrtf(sx * sx + sy * sy);
    if (meanLen < 0.001f) return false;
    float mnx = sx / meanLen;
    float mny = sy / meanLen;
    float cosThresh = cosf(EMPIRICAL_AGREEMENT_RAD);
    for (int i = firstIdx; i < n; i++) {
        float dot = s_navObsBuffer[arrowIdx][i].wx * mnx
                  + s_navObsBuffer[arrowIdx][i].wy * mny;
        if (dot < cosThresh) return false;
    }
    outX = mnx;
    outY = mny;
    return true;
}

// Apply the empirical correction.
//
// Mapping rules (mirror of the predicted-direction logic in ObsLogSample):
//   arrow=UP    measured -> world-direction of screen-up.    camDown = -measured.
//   arrow=DOWN  measured -> world-direction of screen-down.  camDown = +measured.
//   arrow=LEFT  measured -> world-direction of screen-left.  camRight = -measured.
//   arrow=RIGHT measured -> world-direction of screen-right. camRight = +measured.
//
// Quantize the derived axis to its nearest 90-degree world cardinal
// (matches the v0.17.5 CA-quantization path so the empirical and
// CA-derived paths produce identical axis values when both succeed on the
// same field). Then derive the orthogonal axis via the existing v0.17.5
// rotation rule: camDown = (camRight.y, -camRight.x), which keeps det = -1.
//
// Overwrites BOTH the manual-nav pair (s_cam*X/Y) and the auto-drive
// private pair (s_driveCam*X/Y). Sets s_camAxesSource to a distinct tag
// ("empirical-corrected") so logs make obvious which path applied. Marks
// s_camAxesEmpiricalApplied so the correction is one-shot per field load.
static void ObsApplyEmpirical(uint8_t arrow, float wx, float wy) {
    float preRx = s_camRightX, preRy = s_camRightY;
    float preDx = s_camDownX,  preDy = s_camDownY;

    // Step 1: derive the axis directly informed by this measurement.
    float axisX = 0.0f, axisY = 0.0f;
    bool  isDown = false;   // true -> measurement maps to camDown; false -> camRight
    switch (arrow) {
        case DIR_UP:    axisX = -wx; axisY = -wy; isDown = true;  break;
        case DIR_DOWN:  axisX =  wx; axisY =  wy; isDown = true;  break;
        case DIR_LEFT:  axisX = -wx; axisY = -wy; isDown = false; break;
        case DIR_RIGHT: axisX =  wx; axisY =  wy; isDown = false; break;
        default: return;
    }

    // Step 2: quantize to nearest 90-degree cardinal.
    float angle = atan2f(axisY, axisX);
    float quantum = (float)NAV_PI / 2.0f;
    float snappedAngle = roundf(angle / quantum) * quantum;
    float qx = cosf(snappedAngle);
    float qy = sinf(snappedAngle);
    if (fabsf(qx) < 1e-6f) qx = 0.0f;
    if (fabsf(qy) < 1e-6f) qy = 0.0f;

    // Step 3: derive the orthogonal axis with det = -1 (v0.17.5 convention:
    //   camDown = R(-90deg) * camRight = (rY, -rX)
    //   Inverting that:
    //   camRight = (-dY, dX)
    // when camDown is the known axis.
    float rx, ry, dx, dy;
    if (isDown) {
        dx = qx; dy = qy;
        rx = -dy; ry = dx;
    } else {
        rx = qx; ry = qy;
        dx = ry; dy = -rx;
    }

    s_camRightX = rx; s_camRightY = ry;
    s_camDownX  = dx; s_camDownY  = dy;
    s_driveCamRightX = rx; s_driveCamRightY = ry;
    s_driveCamDownX  = dx; s_driveCamDownY  = dy;
    s_camAxesSource = "empirical-corrected";
    s_camAxesEmpiricalApplied = true;

    const char* fieldName = FF8Addresses::pCurrentFieldName
                            ? FF8Addresses::pCurrentFieldName : "(unknown)";
    float det = s_camRightX * s_camDownY - s_camRightY * s_camDownX;
    Log::Field("FieldNavigation: [NAV-CAL] field='%s' arrow=%s samples=%d "
               "consensus=(%.3f,%.3f) EMPIRICAL CORRECTION APPLIED: "
               "camRight (%.3f,%.3f)->(%.3f,%.3f) camDown (%.3f,%.3f)->(%.3f,%.3f) "
               "det=%.3f source=empirical-corrected",
               fieldName, ObsArrowName(arrow), EMPIRICAL_MIN_SAMPLES, wx, wy,
               preRx, preRy, s_camRightX, s_camRightY,
               preDx, preDy, s_camDownX, s_camDownY, det);

    // v0.17.7.6.2: TTS confirmation so Aaron knows the calibration
    // completed. This pairs with the v0.17.7.6.2 AD-refusal message in
    // handlekeys.inl: "Camera not yet calibrated. Press an arrow key
    // briefly to calibrate, then try again." Once this announcement
    // fires, Aaron knows AD will work on his next attempt.
    //
    // Kept terse on purpose -- frequent enough on first entry to
    // degenerate-CA fields that a longer message would be intrusive,
    // but rare enough overall (degenerate CA is uncommon) that the
    // confirmation doesn't add log noise during normal play.
    ScreenReader::Speak("Camera calibrated.");
}

// Log one observation sample. Compares the empirically-measured world-space
// movement direction against the CA-derived prediction for the held arrow.
//
// Arrow-to-camera-axis mapping (DirectInput convention, mirrored by the chase
// auto-pilot's empirical calibration):
//   RIGHT arrow (lX = +1000) -> +camRight   (screen-right in world)
//   LEFT  arrow (lX = -1000) -> -camRight
//   DOWN  arrow (lY = +1000) -> +camDown    (screen-down in world)
//   UP    arrow (lY = -1000) -> -camDown
//
// If the measured and predicted directions agree (divergence ~0 deg), the CA
// values are correct on this field. If divergence is ~180 deg, the predicted
// direction is exactly opposite -- the camera axis pair has both components
// signed the wrong way. If divergence is ~90 deg, the axes are perpendicular
// to what the engine actually uses -- a possible swap of axis0 and axis1.
static void ObsLogSample(uint8_t arrow, float dx, float dy, int heldTicks) {
    float measLen = sqrtf(dx*dx + dy*dy);
    if (measLen < 0.001f) return;
    float mnx = dx / measLen;
    float mny = dy / measLen;

    // Predicted direction = world direction the engine SHOULD move the player
    // in when this arrow is pressed, under the v0.17.0.1 CA-derived axes.
    float pnx = 0.0f, pny = 0.0f;
    switch (arrow) {
        case DIR_UP:    pnx = -s_camDownX;  pny = -s_camDownY;  break;
        case DIR_DOWN:  pnx =  s_camDownX;  pny =  s_camDownY;  break;
        case DIR_LEFT:  pnx = -s_camRightX; pny = -s_camRightY; break;
        case DIR_RIGHT: pnx =  s_camRightX; pny =  s_camRightY; break;
    }
    float predLen = sqrtf(pnx*pnx + pny*pny);
    if (predLen < 0.001f) return;
    pnx /= predLen; pny /= predLen;

    // Divergence angle = arccos(dot of unit vectors).
    float dot = mnx * pnx + mny * pny;
    if (dot >  1.0f) dot =  1.0f;
    if (dot < -1.0f) dot = -1.0f;
    float divDeg = acosf(dot) * 180.0f / (float)NAV_PI;

    const char* fieldName = FF8Addresses::pCurrentFieldName
                            ? FF8Addresses::pCurrentFieldName : "(unknown)";
    Log::Field("FieldNavigation: [NAV-OBSERVE] field='%s' axes=%s arrow=%s held=%dticks "
               "delta=(%.0f,%.0f) measured=(%.3f,%.3f) predicted=(%.3f,%.3f) "
               "DIVERGE=%.0fdeg | camRight=(%.3f,%.3f) camDown=(%.3f,%.3f)",
               fieldName, s_camAxesSource, ObsArrowName(arrow), heldTicks,
               dx, dy, mnx, mny, pnx, pny, divDeg,
               s_camRightX, s_camRightY, s_camDownX, s_camDownY);

    // v0.17.7.6: Closed-loop empirical calibration feed.
    //
    // GATES (all must hold):
    //   1. s_camAxesSource == "identity"    -> CA was missing or degenerate
    //                                          at field load; the v0.17.7.5.5
    //                                          identity defaults are wrong on
    //                                          this field and need replacing.
    //   2. !s_camAxesEmpiricalApplied        -> correction is one-shot per
    //                                          field load; once applied, lock.
    //   3. arrowIdx >= 0                     -> single cardinal arrow (caller
    //                                          already gates, defensive here).
    //   4. measLen >= OBS_EMPIRICAL_MIN_MAG  -> sample is well above the
    //                                          OBS_MOVE_THRESHOLD floor;
    //                                          calibration is permanent so
    //                                          we use a stricter bar.
    //
    // REGRESSION SAFETY: when s_camAxesSource is anything other than
    // "identity" (i.e. "ca-quantized" on healthy CA fields, or
    // "empirical-corrected" after a prior correction), this entire block is
    // skipped. Auto-drive behavior on those fields is byte-for-byte
    // identical to v0.17.7.5.5.
    if (strcmp(s_camAxesSource, "identity") == 0 && !s_camAxesEmpiricalApplied) {
        int arrowIdx = ObsArrowToIdx(arrow);
        if (arrowIdx >= 0 && measLen >= OBS_EMPIRICAL_MIN_MAG) {
            ObsPushSample(arrowIdx, mnx, mny);
            float cx = 0.0f, cy = 0.0f;
            if (ObsCheckConsensus(arrowIdx, cx, cy)) {
                ObsApplyEmpirical(arrow, cx, cy);
            }
        }
    }
}

// Per-tick observer. Called from Update() each frame.
//
// v0.17.5 (post quantization architecture): Pure diagnostic again. The cal
// logic that briefly lived here in v0.17.4/.5-pre is gone; cardinal correction
// is now handled at field-load time by quantizing the CA-derived axes to the
// nearest 90-degree world cardinal in field_nav_fieldscripts.inl. The
// observer continues to log [NAV-OBSERVE] samples so any field whose engine
// arrow-response doesn't match quantized prediction (DIVERGE > ~12 degrees)
// surfaces in the log for follow-up.
//
// Behavior is purely observational -- no game-state writes, no input injection,
// no interaction with any other subsystem. The function ONLY reads state and
// optionally writes a log line. If it returns early (any of the gates), no
// effect; if it logs, the only side-effect is the log line and an update to
// the observer's own private statics.
static void ObserveArrowResponse() {
    // v0.17.7.6.1: Two-tier gating for auto-drive activity.
    //
    // CHASE drive always suppresses observer sampling. The chase auto-pilot
    // runs its own empirical calibration in a tighter loop than the manual
    // observer can match (it injects analog and reads walkmesh delta on a
    // per-tick basis), so the observer should stay out of its way.
    //
    // REGULAR auto-drive normally suppresses observer sampling too, for
    // diagnostic cleanliness -- so the NAV-OBSERVE log shows the engine's
    // response to Aaron's hand, not AD's synthetic injection. BUT on a
    // degenerate-CA field with empirical correction still pending, that
    // suppression creates a catch-22: AD uses the wrong identity axes,
    // goes wrong direction, doesn't produce useful progress, the observer
    // can't sample, calibration can't fire, AD continues wrong forever.
    // v0.17.7.6 BAT on bgroad_5 hit this exact failure: 53 seconds of AD
    // with wrong-direction movement before Aaron disengaged and walked
    // manually to seed the calibration.
    //
    // The fix: when degenerate-CA + pending calibration, allow observer
    // sampling DURING AD. AD's keyboard injection produces the same key
    // state visible to GetAsyncKeyState as Aaron's hand would; the
    // calibration math (input arrow -> world delta direction) is identical
    // regardless of who pressed the key. After enough samples accumulate
    // (~3 seconds with the reduced 2-sample threshold), the calibration
    // fires, both manual-nav and auto-drive axes get overwritten, and
    // AD's next tick re-projects through the corrected axes -- self-
    // correcting within a few seconds of first AD activation.
    //
    // Regression safety: when s_camAxesSource is anything other than
    // "identity" (e.g. "ca-quantized" on healthy CA fields), or after the
    // calibration has already applied ("empirical-corrected"), the gate
    // behaves identically to v0.17.7.6 and earlier. AD on those fields
    // continues to suppress observation.
    if (s_chaseDriveActive) {
        s_obsHoldTicks  = 0;
        s_obsPrevArrows = 0;
        return;
    }
    bool degenerateCaPendingCal = (strcmp(s_camAxesSource, "identity") == 0 &&
                                   !s_camAxesEmpiricalApplied);
    if (s_driveActive && !degenerateCaPendingCal) {
        s_obsHoldTicks  = 0;
        s_obsPrevArrows = 0;
        return;
    }
    // Gate: need a known player entity to read position from.
    if (s_playerEntityIdx < 0) {
        s_obsHoldTicks  = 0;
        s_obsPrevArrows = 0;
        return;
    }
    // Gate: dialog open means engine ignores movement; sample would be zero-delta noise.
    if (FieldDialog::IsDialogOpen()) {
        s_obsHoldTicks  = 0;
        s_obsPrevArrows = 0;
        return;
    }

    uint8_t arrows = ObsReadArrows();

    // Gate: exactly one arrow held. Diagonals could match either single-axis
    // prediction (UP+RIGHT moves at average of -camDown and +camRight, with
    // measured direction ambiguous between the two CA values), so we skip
    // them for clean per-axis data.
    if (ObsBitCount(arrows) != 1) {
        s_obsHoldTicks  = 0;
        s_obsPrevArrows = arrows;
        return;
    }

    // Arrow combination changed since last tick -- start a fresh hold window
    // and record the start position. Need a separate tick to settle before
    // the first position read (engine doesn't move the player instantly).
    if (arrows != s_obsPrevArrows) {
        s_obsHoldTicks  = 0;
        s_obsPrevArrows = arrows;
        GetEntityPos(s_playerEntityIdx, s_obsStartPosX, s_obsStartPosY);
        return;
    }

    // Same single arrow as last tick -- accumulate hold ticks.
    s_obsHoldTicks++;
    if (s_obsHoldTicks < OBS_HOLD_THRESHOLD_TICKS) return;

    // Hold has lasted long enough. Read current position and measure delta
    // from the position recorded when the hold started.
    float px = 0.0f, py = 0.0f;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return;
    float dx = px - s_obsStartPosX;
    float dy = py - s_obsStartPosY;
    float moveLen = sqrtf(dx*dx + dy*dy);
    if (moveLen < OBS_MOVE_THRESHOLD) return;  // not enough movement to be sure of direction

    // Throttle: at most one sample per OBS_LOG_THROTTLE_MS so a long hold
    // doesn't flood the log. Aaron walking around naturally produces a sample
    // every couple of seconds per arrow, which is plenty for diagnosis.
    DWORD now = GetTickCount();
    if (now - s_obsLastLogTick < OBS_LOG_THROTTLE_MS) return;

    ObsLogSample(arrows, dx, dy, s_obsHoldTicks);
    s_obsLastLogTick = now;

    // Re-anchor the start position so the next sample (when the throttle
    // expires) is measured from the current position rather than the original
    // hold start. This keeps successive samples independent and gives a
    // cleaner picture of any direction change as the player continues holding.
    s_obsHoldTicks  = 0;
    s_obsStartPosX  = px;
    s_obsStartPosY  = py;
}
