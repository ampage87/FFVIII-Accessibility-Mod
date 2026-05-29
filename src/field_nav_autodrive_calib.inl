// field_nav_autodrive_calib.inl — Per-field heading calibration state machine
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.17.8.20: Extracted from field_nav_autodrive.inl for size relief (see that
// file's header and DEVNOTES). The CALIB phase 1/2 state machine used to live
// inline at the top of UpdateAutoDrive; it now lives in RunCalibration() below,
// called as `if (RunCalibration()) return;`. ZERO behavior change.
//
// Ordering: included AFTER field_nav_autodrive_helpers.inl (RunCalibration calls
// SetHeldDirections) and BEFORE field_nav_autodrive.inl (UpdateAutoDrive calls
// RunCalibration). The calib statics below were previously declared in this
// position inside field_nav_autodrive.inl, so the later includes that read them
// (directiondrive, handlekeys, fieldscripts) still see them unchanged.
// GetEntityPos lives in field_nav_helpers.inl (included earlier); s_driveCam*,
// s_camCalibrated, s_analog*, s_driveTotalTicks, s_driveStuck* are declared in
// field_navigation.cpp above the include block.

// v06.14: Per-field heading calibration.
// The game interprets analog stick input relative to the camera orientation.
// On each field, lX=+1000 moves the player along the camera's right vector
// in entity/world space, and lY=+1000 moves along the camera's down vector.
// We calibrate by injecting a known analog direction at drive start and
// measuring the resulting world-space movement direction.
//
// Until calibrated, we use the .ca camera axes (loaded at field load) as
// a best guess. The calibration refines this empirically.
// NOTE (v0.17.2): s_driveCamRightX/Y, s_driveCamDownX/Y, s_camCalibrated are
// declared in field_navigation.cpp. The drive-private pair starts mirrored
// to the manual-nav pair (CA-derived) at field load and is overwritten by
// phases 1/2 here. The manual-nav pair (s_camRight/Down) is read by GPS
// and FormatNavComponents and is NEVER touched here.

// v06.14: Heading calibration state machine.
// At drive start, we inject lX=+1000,lY=0 for a few ticks, measure the
// resulting movement direction, and use that as the camera right axis.
// Then inject lX=0,lY=+1000 for a few ticks to get the camera down axis.
// After both are measured, s_camCalibrated=true and we use the measured axes.
static int   s_calibPhase = 0;       // 0=not calibrating, 1=measuring right, 2=measuring down, 3=done
static int   s_calibTicks = 0;       // ticks in current calibration phase
static float s_calibStartX = 0;      // player position at calibration phase start
static float s_calibStartY = 0;
static const int CALIB_SETTLE_TICKS = 8;   // ticks to let the game start moving
static const int CALIB_MEASURE_TICKS = 16; // ticks to measure movement direction
static bool  s_calibPending = false;  // true if calibration should run at drive start

// v0.17.8.20: CALIB phase 1/2 state machine, extracted verbatim from the top of
// UpdateAutoDrive. Returns true while a calibration tick is being consumed
// (phase 1 or 2) — the caller does `if (RunCalibration()) return;`, exactly
// replicating the two inline `return;`s that used to end each phase block.
// Returns false when idle (phase 0) or done (phase 3), so the caller falls
// through to normal navigation. The only structural change from the original
// is that phase 2's `if` became an `else if` and the two per-phase
// `s_driveTotalTicks++; return;` pairs became a `s_driveTotalTicks++;` in each
// branch followed by a single trailing `return true;`. Logic and ordering are
// otherwise byte-for-byte identical.
static bool RunCalibration()
{
    // Not calibrating (phase 0 = idle, phase 3 = done): let the caller fall
    // through to normal navigation.
    if (s_calibPhase <= 0 || s_calibPhase >= 3) return false;

    float cpx = 0, cpy = 0;
    GetEntityPos(s_playerEntityIdx, cpx, cpy);
    s_calibTicks++;

    if (s_calibPhase == 1) {
        // Phase 1: inject lX=+1000, lY=0 (screen-right) and measure movement.
        s_analogOverrideActive = true;
        s_analogDesiredLX = 1000;
        s_analogDesiredLY = 0;
        SetHeldDirections(DIR_RIGHT);  // keyboard trigger for movement

        if (s_calibTicks == CALIB_SETTLE_TICKS) {
            // Record position after settling.
            s_calibStartX = cpx;
            s_calibStartY = cpy;
        } else if (s_calibTicks >= CALIB_SETTLE_TICKS + CALIB_MEASURE_TICKS) {
            // Measure displacement.
            float cdx = cpx - s_calibStartX;
            float cdy = cpy - s_calibStartY;
            float cdist = sqrtf(cdx*cdx + cdy*cdy);
            if (cdist > 5.0f) {
                // Normalize: this is the world-space direction of lX=+1000.
                // v0.17.2: Write to AUTO-DRIVE PRIVATE pair (s_driveCamRight)
                // so manual-nav's s_camRight stays at its CA-derived value.
                s_driveCamRightX = cdx / cdist;
                s_driveCamRightY = cdy / cdist;
                Log::Field("FieldNavigation: [CALIB] phase 1 done: lX=+1000 moved (%.1f,%.1f) dist=%.1f -> driveCamRight=(%.3f,%.3f)",
                           cdx, cdy, cdist, s_driveCamRightX, s_driveCamRightY);
            } else {
                Log::Field("FieldNavigation: [CALIB] phase 1 FAILED: no movement (dist=%.1f), keeping default driveCamRight", cdist);
            }
            // Transition to phase 2.
            s_calibPhase = 2;
            s_calibTicks = 0;
        }
        s_driveTotalTicks++;
    } else if (s_calibPhase == 2) {
        // Phase 2: inject lX=0, lY=+1000 (screen-down) and measure movement.
        s_analogOverrideActive = true;
        s_analogDesiredLX = 0;
        s_analogDesiredLY = 1000;
        SetHeldDirections(DIR_DOWN);  // keyboard trigger for movement

        if (s_calibTicks == CALIB_SETTLE_TICKS) {
            s_calibStartX = cpx;
            s_calibStartY = cpy;
        } else if (s_calibTicks >= CALIB_SETTLE_TICKS + CALIB_MEASURE_TICKS) {
            float cdx = cpx - s_calibStartX;
            float cdy = cpy - s_calibStartY;
            float cdist = sqrtf(cdx*cdx + cdy*cdy);
            if (cdist > 5.0f) {
                s_driveCamDownX = cdx / cdist;
                s_driveCamDownY = cdy / cdist;
                Log::Field("FieldNavigation: [CALIB] phase 2 done: lY=+1000 moved (%.1f,%.1f) dist=%.1f -> driveCamDown=(%.3f,%.3f)",
                           cdx, cdy, cdist, s_driveCamDownX, s_driveCamDownY);
            } else {
                // v06.17: Derive camDown from camRight by 90° clockwise rotation.
                // In screen space, rotating right vector 90° CW gives the down vector.
                // rotation: (x,y) -> (y, -x)
                s_driveCamDownX = s_driveCamRightY;
                s_driveCamDownY = -s_driveCamRightX;
                Log::Field("FieldNavigation: [CALIB] phase 2 FAILED: no movement (dist=%.1f), derived driveCamDown=(%.3f,%.3f) from driveCamRight perpendicular",
                           cdist, s_driveCamDownX, s_driveCamDownY);
            }
            // Calibration complete.
            s_calibPhase = 3;
            s_camCalibrated = true;
            s_calibPending = false;
            // Log the final calibration result.
            Log::Field("FieldNavigation: [CALIB] complete: driveCamRight=(%.3f,%.3f) driveCamDown=(%.3f,%.3f)",
                       s_driveCamRightX, s_driveCamRightY, s_driveCamDownX, s_driveCamDownY);
            // Reset stuck detection to account for calibration movement.
            s_driveStuckTicks = 0;
            GetEntityPos(s_playerEntityIdx, s_driveStuckPosX, s_driveStuckPosY);
        }
        s_driveTotalTicks++;
    }

    return true;  // a calibration tick was consumed; caller must return now
}
