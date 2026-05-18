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
    // Gate: skip during any auto-drive so injected keys can't pollute the sample.
    if (s_driveActive || s_chaseDriveActive) {
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
