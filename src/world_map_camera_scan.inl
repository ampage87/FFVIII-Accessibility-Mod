// world_map_camera_scan.inl - #67 world-map CAMERA-YAW confirmation + walk-basis probe
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_map_drive.inl (uses SetDriveKeys/ReleaseAllDriveKeys/
// WrapWorldDelta) and the segment readers (GetWorldMapPosition/GetWorldMapHeading).
//
// WHY THIS EXISTS (supersedes the .73 G/H camera-control scan, which was negative)
// --------------------------------------------------------------------------------
// Offline disassembly of FF8_EN.exe (2026-06-22) identified the world-map CAMERA:
//   * 0x0203ED02 = camera YAW (compass rotation), 0..4096 = 360deg. The mod has
//     been reading this all along as GetWorldMapHeading() and (wrongly) calling it
//     a frozen, useless "character heading". It is the camera yaw -- frozen on
//     foot because the on-foot world-map camera holds a fixed yaw PER REGION
//     (set on region transitions at .text 0x548155/0x54A1C8/0x54D030, eased by a
//     follow routine at 0x558590 at <=16 units/frame). That is exactly why
//     holding an arrow walks a STRAIGHT line whose WORLD direction differs by
//     region (Aaron, confirmed). The view-matrix builder at 0x552D20 feeds this
//     yaw (and the pitch below) into the PSX RotMatrix/ApplyMatrix library.
//   * 0x0203FE52 = camera PITCH (~45deg downward tilt) -- the second angle 0x552D20
//     reads; it foreshortens the vertical screen axis (why UP maps to a DIAGONAL
//     world direction).
//
// If this holds, navigation needs NO empirical basis and NO orbit-prone probe:
// read the yaw each frame, rotate the world->target bearing by -yaw, press the
// nearest arrow. This diagnostic CONFIRMS the yaw drives movement and MEASURES
// the one constant the steering rewrite needs -- the fixed offset between the
// camera yaw and the world direction the UP arrow actually walks, plus the turn
// sense (does RIGHT sit +90 or -90 from UP in world space).
//
// THE PROBE (one F12 press, fully automated while the player holds still, ~6s,
// TTS on start + completion, pause/resume around random encounters):
//   1. Settle, snapshot yaw + pitch + pos.
//   2. HOLD UP ~1.2s, sampling pos+yaw every 250ms -> net world vector =
//      screen-UP direction at this camera yaw. offsetUp = bearing(UP) - yawDeg.
//   3. Settle. HOLD RIGHT ~1.2s -> bearing(RIGHT). sense = bearing(RIGHT)-bearing(UP).
//   4. Settle. HOLD UP again -> reproduce bearing(UP) + confirm yaw unchanged.
//   5. Log a summary: yaw, pitch, offsetUp(x2), offsetRight, sense.
//
// HOW TO READ [YAWPROBE] in ff8_world.log, and what to do with it:
//   * Run the probe once where you start, then walk into the NEXT region and run
//     it again. Across the two runs the YAW should DIFFER (per-region camera) but
//     offsetUp should be the SAME constant -> proves the yaw drives movement and
//     gives the steering rewrite its one constant. (Within a run, the two UP
//     holds should match and the yaw should not change.)
//   * sense ~ +90 or -90 fixes whether RIGHT is clockwise or counter-clockwise
//     of UP in world space.
//
// Gate-don't-delete: set CAMERA_SCAN_DIAG 0 to retire once the offset+sense are
// confirmed. Keeps the external symbols (TriggerCameraScan / UpdateCameraScan /
// CamScanPause / CamScanResume) so dinput8.cpp (F12) and Poll() need no rewiring.

#define CAMERA_SCAN_DIAG 0

#if CAMERA_SCAN_DIAG

static const double YP_RAD2DEG  = 57.295779513082320;
static const double YP_YAW2DEG  = 360.0 / 4096.0;   // camera-yaw units -> degrees

static const DWORD YP_SETTLE_MS = 400;
static const DWORD YP_HOLD_MS   = 1200;
static const DWORD YP_SAMPLE_MS = 250;

enum YawProbeState {
    YP_OFF = 0,
    YP_SETTLE_PRE,
    YP_HOLD_UP1,
    YP_SETTLE_1,
    YP_HOLD_RIGHT,
    YP_SETTLE_2,
    YP_HOLD_UP2,
    YP_DONE
};

static YawProbeState s_ypState     = YP_OFF;
static bool          s_ypSuspended = false;
static DWORD         s_ypStateStart = 0;
static DWORD         s_ypSampleTick = 0;
static int           s_ypSampleIdx  = 0;

// Per-hold capture
static int32_t s_ypHoldStartX = 0, s_ypHoldStartY = 0;
static int32_t s_ypHoldLastX  = 0, s_ypHoldLastY  = 0;

// Measured world bearings (degrees, math convention: +X=0, CCW positive)
static double s_ypBearUp1   = 0.0; static bool s_ypHaveUp1   = false;
static double s_ypBearRight = 0.0; static bool s_ypHaveRight = false;
static double s_ypBearUp2   = 0.0; static bool s_ypHaveUp2   = false;
static int    s_ypYawAtStart = -1;

static int YpReadPitch()
{
    __try { return (int)(*(volatile int16_t*)0x0203FE52u); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static double YpWrap180(double a)
{
    while (a >  180.0) a -= 360.0;
    while (a <= -180.0) a += 360.0;
    return a;
}

static void YpReleaseAll() { ReleaseAllDriveKeys(); }

static void YpGoto(YawProbeState next, DWORD now) { s_ypState = next; s_ypStateStart = now; }

// Begin a sustained hold of ONE direction; remember the start pos + yaw.
static void YpHoldBegin(const char* label, bool up, bool right)
{
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    s_ypHoldStartX = px; s_ypHoldStartY = py;
    s_ypHoldLastX  = px; s_ypHoldLastY  = py;
    s_ypSampleIdx  = 0;
    s_ypSampleTick = GetTickCount();
    int yaw = (int)GetWorldMapHeading();
    Log::World("WorldMap: [YAWPROBE] HOLD %s begin: pos(%d,%d) yaw=%d (%.1fdeg) pitch=%d",
               label, px, py, yaw, yaw * YP_YAW2DEG, YpReadPitch());
    YpReleaseAll();
    SetDriveKeys(up, false, right, false);   // up also presses the A gas pedal
}

// Sample mid-hold: log the per-window step (is the line straight?) + cumulative.
static void YpHoldSample(const char* label)
{
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    int32_t sdx = px - s_ypHoldLastX, sdy = py - s_ypHoldLastY; WrapWorldDelta(sdx, sdy);
    int32_t cdx = px - s_ypHoldStartX, cdy = py - s_ypHoldStartY; WrapWorldDelta(cdx, cdy);
    double  stepBrg = (sdx || sdy) ? YpWrap180(atan2((double)sdy, (double)sdx) * YP_RAD2DEG) : 0.0;
    int yaw = (int)GetWorldMapHeading();
    Log::World("WorldMap: [YAWPROBE]   %s s%d: pos(%d,%d) step(%+d,%+d brg %.1f) cum(%+d,%+d) yaw=%d",
               label, s_ypSampleIdx, px, py, sdx, sdy, stepBrg, cdx, cdy, yaw);
    s_ypHoldLastX = px; s_ypHoldLastY = py; s_ypSampleIdx++;
}

// End a hold: compute the net world vector + its bearing; stash it.
static double YpHoldEnd(const char* label)
{
    YpReleaseAll();
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    int32_t dx = px - s_ypHoldStartX, dy = py - s_ypHoldStartY; WrapWorldDelta(dx, dy);
    double mag = sqrt((double)dx * dx + (double)dy * dy);
    double brg = (mag > 1.0) ? YpWrap180(atan2((double)dy, (double)dx) * YP_RAD2DEG) : 0.0;
    int yaw = (int)GetWorldMapHeading();
    Log::World("WorldMap: [YAWPROBE] HOLD %s end: net(%+d,%+d) mag=%.0f worldBrg=%.1f yaw=%d (%.1fdeg) offset(brg-yaw)=%.1f",
               label, dx, dy, mag, brg, yaw, yaw * YP_YAW2DEG, YpWrap180(brg - yaw * YP_YAW2DEG));
    if (mag <= 1.0)
        Log::World("WorldMap: [YAWPROBE]   (WARNING: %s produced no movement -- blocked/encounter; rerun on open ground)", label);
    return brg;
}

// ---- Public trigger (F12, from dinput8.cpp) --------------------------------
void TriggerCameraScan()
{
    if (s_ypState != YP_OFF) {
        YpReleaseAll();
        s_ypState = YP_OFF; s_ypSuspended = false;
        ScreenReader::Speak("Yaw probe cancelled.", true);
        Log::World("WorldMap: [YAWPROBE] Cancelled by F12.");
        return;
    }
    if (!s_onWorldMap) {
        ScreenReader::Speak("Yaw probe only works on the world map.", true);
        return;
    }

    s_ypState      = YP_SETTLE_PRE;
    s_ypSuspended  = false;
    s_ypStateStart = GetTickCount();
    s_ypHaveUp1 = s_ypHaveRight = s_ypHaveUp2 = false;
    YpReleaseAll();

    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    s_ypYawAtStart = (int)GetWorldMapHeading();
    Log::World("WorldMap: [YAWPROBE] === START pos(%d,%d) yaw=%d (%.1fdeg) pitch=%d ===",
               px, py, s_ypYawAtStart, s_ypYawAtStart * YP_YAW2DEG, YpReadPitch());
    Log::World("WorldMap: [YAWPROBE] plan: HOLD UP ~1.2s | HOLD RIGHT ~1.2s | HOLD UP ~1.2s. "
               "Confirms 0x203ED02=camera yaw and measures offset(UP-walk vs yaw) + turn sense. "
               "Run here, then walk to the NEXT region and run again: yaw should DIFFER, offset should MATCH.");
    ScreenReader::Speak("Yaw probe started. Hold still for about six seconds.", true);
}

// ---- Pause / resume (called from Poll on world-map exit/entry) -------------
static void CamScanPause()
{
    if (s_ypState == YP_OFF || s_ypSuspended) return;
    YpReleaseAll();
    s_ypSuspended = true;
    Log::World("WorldMap: [YAWPROBE] Paused (left world map -- likely random encounter).");
    ScreenReader::Speak("Yaw probe paused.", true);
}

static void CamScanResume()
{
    if (s_ypState == YP_OFF || !s_ypSuspended) return;
    s_ypSuspended  = false;
    s_ypState      = YP_SETTLE_PRE;       // an encounter may have moved/re-yawed; restart clean
    s_ypStateStart = GetTickCount();
    s_ypHaveUp1 = s_ypHaveRight = s_ypHaveUp2 = false;
    YpReleaseAll();
    Log::World("WorldMap: [YAWPROBE] Resumed -- restarting the probe from the top.");
    ScreenReader::Speak("Resuming yaw probe.", true);
}

// ---- State machine (called each frame from Poll while on the world map) -----
static void UpdateCameraScan()
{
    if (s_ypState == YP_OFF || s_ypSuspended) return;

    DWORD now     = GetTickCount();
    DWORD inState = now - s_ypStateStart;

    // Drive the per-window sampler during any HOLD phase.
    if (s_ypState == YP_HOLD_UP1 || s_ypState == YP_HOLD_RIGHT || s_ypState == YP_HOLD_UP2) {
        if (now - s_ypSampleTick >= YP_SAMPLE_MS) {
            const char* lbl = (s_ypState == YP_HOLD_RIGHT) ? "RIGHT" : "UP";
            YpHoldSample(lbl);
            s_ypSampleTick = now;
        }
    }

    switch (s_ypState) {
        case YP_SETTLE_PRE:
            YpReleaseAll();
            if (inState >= YP_SETTLE_MS) { YpHoldBegin("UP1", true, false); YpGoto(YP_HOLD_UP1, now); }
            break;

        case YP_HOLD_UP1:
            if (inState >= YP_HOLD_MS) {
                s_ypBearUp1 = YpHoldEnd("UP1"); s_ypHaveUp1 = true;
                YpGoto(YP_SETTLE_1, now);
            }
            break;

        case YP_SETTLE_1:
            YpReleaseAll();
            if (inState >= YP_SETTLE_MS) { YpHoldBegin("RIGHT", false, true); YpGoto(YP_HOLD_RIGHT, now); }
            break;

        case YP_HOLD_RIGHT:
            if (inState >= YP_HOLD_MS) {
                s_ypBearRight = YpHoldEnd("RIGHT"); s_ypHaveRight = true;
                YpGoto(YP_SETTLE_2, now);
            }
            break;

        case YP_SETTLE_2:
            YpReleaseAll();
            if (inState >= YP_SETTLE_MS) { YpHoldBegin("UP2", true, false); YpGoto(YP_HOLD_UP2, now); }
            break;

        case YP_HOLD_UP2:
            if (inState >= YP_HOLD_MS) {
                s_ypBearUp2 = YpHoldEnd("UP2"); s_ypHaveUp2 = true;
                YpGoto(YP_DONE, now);
            }
            break;

        case YP_DONE: {
            YpReleaseAll();
            int yawNow = (int)GetWorldMapHeading();
            double yawDeg = yawNow * YP_YAW2DEG;
            if (s_ypHaveUp1 && s_ypHaveRight && s_ypHaveUp2) {
                double offUp1 = YpWrap180(s_ypBearUp1   - yawDeg);
                double offUp2 = YpWrap180(s_ypBearUp2   - yawDeg);
                double offRt  = YpWrap180(s_ypBearRight - yawDeg);
                double sense  = YpWrap180(s_ypBearRight - s_ypBearUp1);
                Log::World("WorldMap: [YAWPROBE] === SUMMARY yaw=%d (%.1fdeg) pitch=%d | "
                           "bearUP1=%.1f bearUP2=%.1f bearRIGHT=%.1f | offsetUP1=%.1f offsetUP2=%.1f offsetRIGHT=%.1f | "
                           "sense(RIGHT-UP)=%.1f (expect ~+/-90) | UP-reproduce delta=%.1f (expect ~0) ===",
                           yawNow, yawDeg, YpReadPitch(),
                           s_ypBearUp1, s_ypBearUp2, s_ypBearRight,
                           offUp1, offUp2, offRt, sense, YpWrap180(s_ypBearUp2 - s_ypBearUp1));
            } else {
                Log::World("WorldMap: [YAWPROBE] === SUMMARY incomplete (a hold produced no movement). Rerun on open ground. ===");
            }
            ScreenReader::Speak("Yaw probe complete.", true);
            s_ypState = YP_OFF;
            break;
        }

        default:
            s_ypState = YP_OFF;
            break;
    }
}

#else  // CAMERA_SCAN_DIAG == 0

void TriggerCameraScan()
{
    ScreenReader::Speak("Yaw probe is not enabled in this build.", true);
}

#endif // CAMERA_SCAN_DIAG
