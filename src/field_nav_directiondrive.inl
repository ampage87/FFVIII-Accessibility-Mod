// field_nav_directiondrive.inl - Direction-based auto-drive for chase scenes (v0.15.9.2.1)
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// PURPOSE
//
// Provides a public API (StartDirectionDrive / StopDirectionDrive /
// IsDirectionDriveActive) that lets external callers (chase_auto_pilot
// in v0.15.9.1, possibly other modules later) drive the party in a
// fixed screen-relative direction without engaging F9 path-finding
// auto-drive.
//
// WHY THIS EXISTS
//
// v0.15.9 BAT (2026-05-10) shipped chase_auto_pilot with standalone
// SendInput keyboard injection. The auto-pilot engaged but the party
// never moved -- per field_nav_autodrive.inl's v05.85 comment:
//
//   "Keyboard injection is REQUIRED to activate the game's movement
//    code path. Analog steering overrides the direction, but keyboard
//    buttons are the trigger that makes the game process movement at all."
//
// chase_auto_pilot's keyboard arrows + W weren't enough. The engine reads
// movement direction from the gamepad analog stick (DIJOYSTATE2 lX/lY);
// keyboard is just a wake-up trigger. F9 path-finding works because it
// installs a fake gamepad and writes lX/lY values; chase_auto_pilot did
// not.
//
// MECHANISM
//
// StartDirectionDrive(dirX, dirY, walk):
//   1. Refuses if F9 path-finding (s_driveActive) is already running.
//   2. Installs the fake gamepad (mirrors the install code in
//      field_nav_handlekeys.inl's drive branch).
//   3. Activates s_analogOverrideActive and writes screen-relative
//      analog values: lX = dirX * 1000, lY = dirY * 1000. NO camera
//      projection -- chase fields hand us the direction the engine
//      should see directly.
//   4. Holds one keyboard arrow as the wake-up trigger via
//      SetHeldDirections (the same diff-based holder that path-finding
//      uses). Direction matches dirX/dirY so the keyboard "vote"
//      agrees with the analog "vote".
//   5. If walk=true, holds W (scancode 0x11) via SendInput so the
//      party walks instead of runs. FF8 PC default keymap: cancel = W
//      = walk modifier on foot.
//
// StopDirectionDrive():
//   Releases W if held, releases all arrows, deactivates analog
//   override, removes the fake gamepad. Mirror of StartDirectionDrive.
//
// MUTUAL EXCLUSION WITH F9 PATH-FINDING
//
// Both paths share the analog override + fake gamepad infrastructure.
// They cannot run concurrently. Two enforcement points:
//   - StartDirectionDrive refuses if s_driveActive is true.
//   - field_nav_handlekeys.inl's F9 branch refuses if
//     s_directionDriveActive is true.
//
// The shared state (s_analogOverrideActive, s_analogDesiredLX/LY,
// s_fakeGamepadInstalled, s_savedDevicePtr/StatePtr, s_fakeDIJOYSTATE2,
// s_driveHeld via SetHeldDirections) is declared in
// field_navigation.cpp. This .inl is included AFTER field_nav_autodrive.inl
// (which defines SetHeldDirections / InjectKey / ReleaseAllDirections)
// and BEFORE field_nav_handlekeys.inl (so the F9 handler can see the
// s_directionDriveActive flag).
//
// COORDINATE CONVENTION (from field_nav_input_hooks.inl)
//
//   lX = +1000 -> screen right
//   lX = -1000 -> screen left
//   lY = +1000 -> screen down
//   lY = -1000 -> screen up
//
// chase_auto_pilot translates its DIR_UP/DOWN/LEFT/RIGHT mask to
// (dirX, dirY) accordingly, e.g. domt4_1 RUN LEFT = (-1, 0, false),
// domt5_1 WALK SOUTH = (0, +1, true).
//
// KEEP-ALIVE PULSE (v0.15.9.1.1)
//
// v0.15.9.1 BAT (2026-05-10 14:34-14:39) confirmed the analog override
// reaches the engine: party moved (-843,2482) -> (-769,2217) on domt5_1
// in the first second of engagement, then froze for 80+ seconds. Theory:
// the engine debounces "movement intent" after one walking cycle when
// keyboard input is constant. F9 path-finding sidesteps this because its
// heading vector wobbles tick-to-tick as the player walks toward dynamic
// waypoints -- SetHeldDirections fires fresh KEYUP/KEYDOWN events on
// arrow-bitmask flips, which the engine treats as new movement-intent.
// chase direction-drive's heading is fixed; arrow bitmask never changes;
// engine drops intent after one walking cycle (~60 ticks).
//
// v0.15.9.1.1 fix: in the "already running" branch of StartDirectionDrive,
// run a short cycle that releases the held arrow for one tick and re-presses
// it the next, every KEEPALIVE_PERIOD ticks. SetHeldDirections fires fresh
// KEYUP+KEYDOWN events; the engine sees "new" intent every ~0.5s, well
// inside one walking cycle. Direction is unchanged across the pulse, so
// the visible movement is continuous (the analog vote, which the engine
// reads each frame, never goes to zero -- only the discrete keyboard
// re-pulse generates the intent event).
//
// W (walk modifier) is NOT pulsed -- it is a held modifier the engine
// reads continuously to set walk-vs-run speed; toggling it would cause
// speed glitches.

// ============================================================================
// State (file-scope statics)
// ============================================================================

// volatile because direction-drive can be started/stopped from the mod
// thread (chase_auto_pilot::Update) and the analog values are read by
// the game thread (HookedEngineEvalInput in field_nav_input_hooks.inl).
static volatile bool s_directionDriveActive = false;

// True while the W (cancel/walk) scancode is currently being held by
// direction-drive. Tracked separately from s_driveHeld (which only
// covers arrow keys) so we can release exactly what we pressed.
static bool s_directionDriveWalk = false;

// W scancode = 0x11. Same value chase_auto_pilot used in v0.15.9 and
// the same value field_nav_autodrive.inl uses for cancel detection.
// FF8 PC default keymap: cancel = W = walk modifier on foot.
static const WORD SC_W_CANCEL_DD = 0x11;

// v0.15.9.1.1: Keep-alive pulse cycle.
//
// Counter increments each tick the "already running" branch of
// StartDirectionDrive is hit. The cycle is:
//   ticks 1..(KEEPALIVE_PERIOD-1)  -> normal hold (SetHeldDirections(arrows))
//   tick KEEPALIVE_PERIOD          -> RELEASE arrow (SetHeldDirections(0))
//   tick KEEPALIVE_PERIOD+1        -> RE-PRESS arrow (SetHeldDirections(arrows)),
//                                     reset counter to 0
// Net result: 30 ticks of held + 1 tick released, then a fresh KEYDOWN.
// At 60 FPS that's a re-press every ~0.5 seconds.
//
// 30 was picked to be well below FF8's apparent ~60-tick walking-cycle
// debounce window observed in the v0.15.9.1 BAT (party walked one full
// cycle then froze).
static const int KEEPALIVE_PERIOD = 30;

// v0.15.9.1.1: Keep-alive counter. volatile because direction-drive's
// "already running" branch is called from the mod thread
// (chase_auto_pilot::Update) while the analog values it manages are
// read by the game thread (HookedEngineEvalInput).
static volatile int s_keepAliveCounter = 0;

// ============================================================================
// Internal helpers
// ============================================================================

// Mirrors the fake-gamepad install block in field_nav_handlekeys.inl's
// F9 drive branch so direction-drive shares the same dinput injection
// path. Idempotent -- safe to call when the gamepad is already
// installed (returns immediately). Does nothing if FF8Addresses haven't
// resolved the dinput pointers; in that case the analog write path is
// dormant and the keyboard wake-up is the only input the game sees,
// matching v0.15.9's behavior.
static void DD_InstallFakeGamepad()
{
    if (s_fakeGamepadInstalled) return;
    if (!FF8Addresses::HasDinputGamepadPtrs()) {
        Log::Field("FieldNavigation: [direction-drive] WARNING - "
                   "dinput gamepad ptrs not resolved, fake gamepad NOT installed "
                   "(analog path will be dormant)");
        return;
    }
    s_savedDevicePtr = *FF8Addresses::pDinputGamepadDevicePtr;
    s_savedStatePtr  = *FF8Addresses::pDinputGamepadStatePtr;
    memset(s_fakeDIJOYSTATE2, 0, sizeof(s_fakeDIJOYSTATE2));
    *FF8Addresses::pDinputGamepadStatePtr  = (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2;
    *FF8Addresses::pDinputGamepadDevicePtr = FAKE_DEVICE_SENTINEL;
    s_fakeGamepadInstalled = true;
    Log::Field("FieldNavigation: [direction-drive] fake gamepad installed: "
               "device=0x%08X state=0x%08X (saved dev=0x%08X state=0x%08X)",
               FAKE_DEVICE_SENTINEL, (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2,
               s_savedDevicePtr, s_savedStatePtr);
}

// Mirrors the fake-gamepad uninstall block in field_nav_autodrive.inl's
// StopAutoDrive. Idempotent.
static void DD_UninstallFakeGamepad()
{
    if (!s_fakeGamepadInstalled) return;
    if (!FF8Addresses::HasDinputGamepadPtrs()) return;
    *FF8Addresses::pDinputGamepadDevicePtr = s_savedDevicePtr;
    *FF8Addresses::pDinputGamepadStatePtr  = s_savedStatePtr;
    s_fakeGamepadInstalled = false;
    Log::Field("FieldNavigation: [direction-drive] fake gamepad removed, "
               "original ptrs restored");
}

// Convert a (dirX, dirY) tuple to an arrow-key bitmask suitable for
// SetHeldDirections. The keyboard wake-up just needs SOME direction
// pressed; we pick one that aligns with the analog vote so the game's
// keyboard direction reading and its analog reading agree.
//
// Caller passes (0, 0) at their peril -- we map that to DIR_UP as a
// safety so SetHeldDirections sees something. chase_auto_pilot never
// passes (0, 0) since unconfigured fields don't engage at all.
static uint8_t DD_DirsToArrowMask(int8_t dirX, int8_t dirY)
{
    uint8_t mask = 0;
    if (dirY < 0) mask |= DIR_UP;
    if (dirY > 0) mask |= DIR_DOWN;
    if (dirX < 0) mask |= DIR_LEFT;
    if (dirX > 0) mask |= DIR_RIGHT;
    if (mask == 0) mask = DIR_UP;  // safety
    return mask;
}

// ============================================================================
// Public API
// ============================================================================
//
// These are at namespace scope (NOT static) because they are visible
// outside this translation unit via field_navigation.h. The .inl file
// is included inside `namespace FieldNavigation { ... }` in
// field_navigation.cpp, so the names resolve as
// FieldNavigation::StartDirectionDrive etc.

void StartDirectionDrive(int8_t dirX, int8_t dirY, bool walk)
{
    // Mutex with F9 path-finding auto-drive. Both paths use the
    // analog override and fake gamepad; they cannot coexist.
    if (s_driveActive) {
        Log::Field("FieldNavigation: [direction-drive] REFUSED to start "
                   "(F9 path-finding auto-drive is active). Stop it first.");
        return;
    }

    if (s_directionDriveActive) {
        // Already running. Update direction values + walk state in place.
        // chase_auto_pilot::Update may call this every tick on an engaged
        // field; the diff-based SetHeldDirections handles arrow changes
        // cheaply and we only fire SendInput for W when its state actually
        // flips.
        s_analogDesiredLX = (int32_t)dirX * 1000;
        s_analogDesiredLY = (int32_t)dirY * 1000;
        uint8_t arrows = DD_DirsToArrowMask(dirX, dirY);

        // v0.15.9.1.1: Keep-alive pulse cycle. See header comment block.
        // Releases the held arrow for one tick every KEEPALIVE_PERIOD
        // ticks, then re-presses it. SetHeldDirections is diff-based so
        // the actual SendInput KEYUP/KEYDOWN events only fire on the two
        // boundary ticks per cycle -- the rest are no-ops.
        s_keepAliveCounter++;
        if (s_keepAliveCounter == KEEPALIVE_PERIOD) {
            // Release the arrow for one tick to generate KEYUP.
            SetHeldDirections(0);
        } else if (s_keepAliveCounter > KEEPALIVE_PERIOD) {
            // Re-press the arrow to generate fresh KEYDOWN, reset cycle.
            SetHeldDirections(arrows);
            s_keepAliveCounter = 0;
        } else {
            // Normal hold (ticks 1 .. KEEPALIVE_PERIOD-1).
            SetHeldDirections(arrows);
        }

        if (walk != s_directionDriveWalk) {
            InjectKey(SC_W_CANCEL_DD, walk);
            s_directionDriveWalk = walk;
            Log::Field("FieldNavigation: [direction-drive] walk modifier %s",
                       walk ? "PRESSED" : "released");
        }
        return;
    }

    // Fresh start.
    DD_InstallFakeGamepad();

    s_analogOverrideActive = true;
    s_analogDesiredLX = (int32_t)dirX * 1000;
    s_analogDesiredLY = (int32_t)dirY * 1000;

    uint8_t arrows = DD_DirsToArrowMask(dirX, dirY);
    SetHeldDirections(arrows);

    if (walk) {
        InjectKey(SC_W_CANCEL_DD, true);
        s_directionDriveWalk = true;
    } else {
        s_directionDriveWalk = false;
    }

    // v0.15.9.1.1: Reset keep-alive counter on fresh engagement so each
    // direction-drive session gets a clean cycle. Otherwise a stop+restart
    // could land mid-cycle and immediately fire a release pulse.
    s_keepAliveCounter = 0;

    s_directionDriveActive = true;
    Log::Field("FieldNavigation: [direction-drive] STARTED dir=(%d,%d) walk=%d "
               "lX=%d lY=%d arrows=0x%X",
               (int)dirX, (int)dirY, (int)walk,
               (int)s_analogDesiredLX, (int)s_analogDesiredLY,
               (unsigned)arrows);
}

void StopDirectionDrive()
{
    if (!s_directionDriveActive) return;

    if (s_directionDriveWalk) {
        InjectKey(SC_W_CANCEL_DD, false);
        s_directionDriveWalk = false;
    }
    ReleaseAllDirections();
    s_analogOverrideActive = false;
    s_analogDesiredLX = 0;
    s_analogDesiredLY = 0;
    DD_UninstallFakeGamepad();

    s_directionDriveActive = false;
    s_keepAliveCounter = 0;  // v0.15.9.1.1
    Log::Field("FieldNavigation: [direction-drive] STOPPED");
}

bool IsDirectionDriveActive()
{
    return s_directionDriveActive;
}

// ============================================================================
// v0.15.9.2: Chase-drive (path-finding flavour for chase auto-pilot)
// ============================================================================
//
// Wraps the F9 path-finding auto-drive (defined in field_nav_autodrive.inl)
// with an entry point that takes raw target coordinates instead of an entity
// catalog index. Used by chase_auto_pilot to navigate chase fields with
// full walkmesh-aware A*+funnel pathing, stuck-detection, and wiggle
// recovery -- all the things the v0.15.9.1.1 direction-drive lacked when
// it ran into the wall at (-769, 2217) in domt5_1.
//
// Why a separate entry point and not just calling F9's existing flow:
//
//   F9's start logic (in field_nav_handlekeys.inl) is gated on the entity
//   catalog: it pulls a target entity, looks up its position, applies
//   entity-type-specific arrive distance (talkRadius, save/draw point,
//   trigger line crossing detection). chase_auto_pilot doesn't have an
//   entity to target -- it has a hardcoded (X, Y) per chase field. So we
//   skip the entity-specific setup and feed raw coords into the same
//   path-computation pipeline (line-of-sight test, A*, FunnelPath).
//
//   The drive STATE machine (UpdateAutoDrive in field_nav_autodrive.inl)
//   is shared verbatim. Once chase-drive sets s_driveActive=true and
//   populates the waypoint array, the existing per-tick steering, stuck
//   detection, and recovery wiggle all run unmodified.
//
// Walk modifier: chase fields like domt5_1 require walking instead of
// running (Aaron's AI rule #1: the mountain shakes when running and the
// party gets caught). chase-drive optionally holds W (cancel scancode
// 0x11) for the duration of the drive. F9 path-finding doesn't support
// this because F9 always runs to its target as fast as possible.
//
// Mutex with F9: F9's backslash handler in field_nav_handlekeys.inl
// refuses to start a new drive if s_chaseDriveActive is true, and arrow-
// key cancel is suppressed for chase-drive (the player can't accidentally
// stop chase auto-pilot by tapping a direction). Direction-drive
// (StartDirectionDrive) is also mutually exclusive -- chase_auto_pilot
// picks one or the other per-field via its config table.
//
// Cleanup on stop: StopChaseDrive releases W (if held), then calls
// StopAutoDrive(nullptr) to release arrows, deactivate analog, and
// remove the fake gamepad. Same teardown F9's StopAutoDrive does, plus W.

// ============================================================================
// Chase-drive state (file-scope statics)
// ============================================================================

// v0.15.9.2.1: Chase-drive state moved to field_navigation.cpp (declared
// before field_nav_autodrive.inl is included) so the autodrive .inl can
// reference s_chaseDriveActive / s_chaseDriveTargetX/Y for the bypass
// branches that fix the v0.15.9.2 "No target" SAPI bug. The variables
// declared in field_navigation.cpp:
//   static volatile bool s_chaseDriveActive;
//   static bool          s_chaseDriveWalk;
//   static int32_t       s_chaseDriveTargetX;
//   static int32_t       s_chaseDriveTargetY;
// We use them here from StartChaseDrive / StopChaseDrive / IsChaseDriveActive
// below as if they were declared in this .inl. File-scope statics in textual
// includes are visible to the whole translation unit regardless of which .inl
// file declares them.

// ============================================================================
// Public API
// ============================================================================

bool StartChaseDrive(int32_t targetX, int32_t targetY,
                     int triggerLineIdx,
                     int32_t crossLineX1, int32_t crossLineY1,
                     int32_t crossLineX2, int32_t crossLineY2,
                     bool walk)
{
    // Mutex: refuse if any auto-drive is already active and it's not us.
    if (s_driveActive && !s_chaseDriveActive) {
        Log::Field("FieldNavigation: [chase-drive] REFUSED to start "
                   "(F9 path-finding auto-drive is active). Stop it first.");
        return false;
    }
    if (s_directionDriveActive) {
        Log::Field("FieldNavigation: [chase-drive] REFUSED to start "
                   "(direction-drive is active). Stop it first.");
        return false;
    }

    // Already running? chase_auto_pilot::Update calls StartChaseDrive at most
    // once per engagement (unlike direction-drive which is called every tick),
    // so this branch is mostly a defensive no-op. Returning true preserves the
    // "call is idempotent if already engaged" pattern that direction-drive set.
    if (s_chaseDriveActive) {
        return true;
    }

    // Prerequisites.
    if (FieldDialog::IsDialogOpen()) {
        Log::Field("FieldNavigation: [chase-drive] REFUSED: dialog is open");
        return false;
    }
    if (!FF8Addresses::IsOnField()) {
        Log::Field("FieldNavigation: [chase-drive] REFUSED: not on field");
        return false;
    }
    if (s_playerEntityIdx < 0) {
        Log::Field("FieldNavigation: [chase-drive] REFUSED: player entity unresolved");
        return false;
    }

    // Read player's current position.
    float _px = 0, _pz = 0;
    if (!GetEntityPos(s_playerEntityIdx, _px, _pz)) {
        Log::Field("FieldNavigation: [chase-drive] REFUSED: player position read failed");
        return false;
    }

    // Seed the triangle ID from the player's current triangle so the first
    // tick doesn't see a "change" from the uninitialized value. Mirrors
    // F9's start logic in field_nav_handlekeys.inl.
    uint16_t seedTri = 0xFFFF;
    {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base)
            seedTri = *(uint16_t*)(base + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
    }

    // Drive state initialization (mirrors F9's start block).
    s_driveActive          = true;
    s_driveLastTriId       = seedTri;
    s_driveStuckTicks      = 0;
    s_driveWiggleTicks     = 0;
    s_driveWiggleDir       = 0;
    s_driveWigglePhase     = 0;
    s_driveTotalTicks      = 0;
    s_driveLogTimer        = 0;
    s_driveStuckPosX       = _px;
    s_driveStuckPosY       = _pz;
    s_driveProgressDist    = 1e30f;
    s_driveNoProgressCount = 0;

    // Calibration setup. F9 calibrates the camera-to-world heading on first
    // drive per field; chase-drive piggybacks on the same flag so a single
    // calibration run covers both paths.
    if (s_calibPending && !s_camCalibrated) {
        s_calibPhase = 1;
        s_calibTicks = 0;
        Log::Field("FieldNavigation: [chase-drive] starting heading calibration for field '%s'",
                   FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?");
    } else {
        s_calibPhase = 3;
    }

    // Fake gamepad install.
    if (FF8Addresses::HasDinputGamepadPtrs() && !s_fakeGamepadInstalled) {
        s_savedDevicePtr = *FF8Addresses::pDinputGamepadDevicePtr;
        s_savedStatePtr  = *FF8Addresses::pDinputGamepadStatePtr;
        memset(s_fakeDIJOYSTATE2, 0, sizeof(s_fakeDIJOYSTATE2));
        *FF8Addresses::pDinputGamepadStatePtr  = (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2;
        *FF8Addresses::pDinputGamepadDevicePtr = FAKE_DEVICE_SENTINEL;
        s_fakeGamepadInstalled = true;
        Log::Field("FieldNavigation: [chase-drive] fake gamepad installed: "
                   "device=0x%08X state=0x%08X (saved dev=0x%08X state=0x%08X)",
                   FAKE_DEVICE_SENTINEL, (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2,
                   s_savedDevicePtr, s_savedStatePtr);
    }

    // v0.15.9.2.14/15: Enable cross-product sign-flip detection for the
    // chase-drive target. The crossing line can come from either a SETLINE
    // trigger (triggerLineIdx >= 0) or an INF gateway (crossLineX1/Y1/X2/Y2
    // non-zero). UpdateAutoDrive's crossing block reads s_driveCrossLine*
    // for chase-drive to (a) detect when the player crosses the line and
    // Arrive immediately, (b) offset the heading 300 units past the line
    // center so player momentum carries them through. Trigger lines also
    // set s_driveSkipTrigIdx for A* avoidance exemption; INF gateways are
    // off-mesh and don't need A* exemption (s_driveSkipTrigIdx stays -1).
    //
    // Without crossing detection (both inputs unset), chase-drive falls back
    // to plain point-distance arrival -- v0.15.9.2.13's 60-unit threshold.
    s_driveArriveDist      = 60.0f;
    s_driveTargetEntityIdx = -1;
    s_driveOrigTalkRadius  = 0;
    s_driveTalkRadExpanded = false;
    s_driveCrossLineActive = false;
    if (triggerLineIdx >= 0 && triggerLineIdx < s_capturedLineCount &&
        s_capturedLines[triggerLineIdx].active) {
        s_driveTrigTarget      = true;
        s_driveCrossLineX1     = s_capturedLines[triggerLineIdx].x1;
        s_driveCrossLineY1     = s_capturedLines[triggerLineIdx].y1;
        s_driveCrossLineX2     = s_capturedLines[triggerLineIdx].x2;
        s_driveCrossLineY2     = s_capturedLines[triggerLineIdx].y2;
        s_driveCrossLineActive = true;
        s_driveSkipTrigIdx     = triggerLineIdx;
        float tdx = (float)(s_driveCrossLineX2 - s_driveCrossLineX1);
        float tdy = (float)(s_driveCrossLineY2 - s_driveCrossLineY1);
        s_driveTrigCrossStart = tdx * (_pz - (float)s_driveCrossLineY1)
                              - tdy * (_px - (float)s_driveCrossLineX1);
        Log::Field("FieldNavigation: [chase-drive] trigger-line target idx=%d "
                   "line(%d,%d)->(%d,%d) crossStart=%.0f",
                   triggerLineIdx,
                   (int)s_driveCrossLineX1, (int)s_driveCrossLineY1,
                   (int)s_driveCrossLineX2, (int)s_driveCrossLineY2,
                   s_driveTrigCrossStart);
    } else if (crossLineX1 != 0 || crossLineY1 != 0 ||
               crossLineX2 != 0 || crossLineY2 != 0) {
        // INF gateway crossing line (or any caller-supplied line, e.g. a
        // future custom override). No A* avoidance -- gateways are off-mesh.
        s_driveTrigTarget      = true;
        s_driveCrossLineX1     = (int16_t)crossLineX1;
        s_driveCrossLineY1     = (int16_t)crossLineY1;
        s_driveCrossLineX2     = (int16_t)crossLineX2;
        s_driveCrossLineY2     = (int16_t)crossLineY2;
        s_driveCrossLineActive = true;
        s_driveSkipTrigIdx     = -1;
        float tdx = (float)(s_driveCrossLineX2 - s_driveCrossLineX1);
        float tdy = (float)(s_driveCrossLineY2 - s_driveCrossLineY1);
        s_driveTrigCrossStart = tdx * (_pz - (float)s_driveCrossLineY1)
                              - tdy * (_px - (float)s_driveCrossLineX1);
        Log::Field("FieldNavigation: [chase-drive] gateway crossing line "
                   "(%d,%d)->(%d,%d) crossStart=%.0f",
                   (int)s_driveCrossLineX1, (int)s_driveCrossLineY1,
                   (int)s_driveCrossLineX2, (int)s_driveCrossLineY2,
                   s_driveTrigCrossStart);
    } else {
        s_driveTrigTarget     = false;
        s_driveTrigCrossStart = 0.0f;
        s_driveSkipTrigIdx    = -1;
    }

    // Path computation. Same A*+funnel pipeline F9 uses, minus the line-of-
    // sight first-pass (chase fields are hand-picked corridors that benefit
    // from full A* path resolution).
    s_waypointCount = 0;
    s_waypointIdx   = 0;
    s_usingFunnel   = false;
    s_wpMinDist     = 1e30f;

    float _tx = (float)targetX;
    float _tz = (float)targetY;

    if (s_walkmesh.valid) {
        int startTri = -1;
        if (seedTri != 0xFFFF && seedTri < (uint16_t)s_walkmesh.numTriangles) {
            startTri = (int)seedTri;
        } else {
            startTri = FindNearestTriangle(_px, _pz);
        }
        int goalTri = FindNearestTriangle(_tx, _tz);

        if (startTri >= 0 && goalTri >= 0) {
            if (!AreTrianglesConnected(startTri, goalTri)) {
                // Different walkmesh islands. Chase fields shouldn't normally
                // hit this -- the chase exit and the player should always be
                // on the same island. Log loudly and try a trigger-line bridge
                // (mirrors F9's island-bridging logic).
                Log::Field("FieldNavigation: [chase-drive] target on different walkmesh island "
                           "(start tri %d, goal tri %d) -- searching for bridge trigger",
                           startTri, goalTri);
                float bestTrigDist = 1e30f;
                int bestTrigIdx = -1;
                for (int tl = 0; tl < s_capturedLineCount; tl++) {
                    if (!s_capturedLines[tl].active) continue;
                    float tcx = (float)(s_capturedLines[tl].x1 + s_capturedLines[tl].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[tl].y1 + s_capturedLines[tl].y2) / 2.0f;
                    float tdx = tcx - _px;
                    float tdy = tcy - _pz;
                    float tdist = sqrtf(tdx*tdx + tdy*tdy);
                    int trigTri = FindNearestTriangle(tcx, tcy);
                    if (trigTri >= 0 && AreTrianglesConnected(startTri, trigTri)) {
                        if (tdist < bestTrigDist) {
                            bestTrigDist = tdist;
                            bestTrigIdx = tl;
                        }
                    }
                }
                if (bestTrigIdx >= 0) {
                    float bridgeX = (float)(s_capturedLines[bestTrigIdx].x1 + s_capturedLines[bestTrigIdx].x2) / 2.0f;
                    float bridgeY = (float)(s_capturedLines[bestTrigIdx].y1 + s_capturedLines[bestTrigIdx].y2) / 2.0f;
                    int bridgeTri = FindNearestTriangle(bridgeX, bridgeY);
                    Log::Field("FieldNavigation: [chase-drive] redirecting to trigger line %d "
                               "center=(%.0f,%.0f) tri=%d dist=%.0f",
                               bestTrigIdx, bridgeX, bridgeY, bridgeTri, bestTrigDist);
                    s_driveTrigTarget = true;
                    float tlx1 = (float)s_capturedLines[bestTrigIdx].x1;
                    float tly1 = (float)s_capturedLines[bestTrigIdx].y1;
                    float tlx2 = (float)s_capturedLines[bestTrigIdx].x2;
                    float tly2 = (float)s_capturedLines[bestTrigIdx].y2;
                    float tdx2 = tlx2 - tlx1;
                    float tdy2 = tly2 - tly1;
                    s_driveTrigCrossStart = tdx2 * (_pz - tly1) - tdy2 * (_px - tlx1);
                    if (bridgeTri >= 0 && ComputeAStarPath(startTri, bridgeTri, -1, bestTrigIdx)) {
                        FunnelPath(_px, _pz, bridgeX, bridgeY);
                    }
                    _tx = bridgeX;
                    _tz = bridgeY;
                } else {
                    Log::Field("FieldNavigation: [chase-drive] no reachable trigger line found, using direct steering");
                    s_waypoints[0][0] = _tx;
                    s_waypoints[0][1] = _tz;
                    s_waypointCount = 1;
                    s_waypointIdx = 0;
                }
            } else {
                // Same island -- A*+funnel. No entity to exempt from avoidance.
                if (ComputeAStarPath(startTri, goalTri, -1, -1)) {
                    FunnelPath(_px, _pz, _tx, _tz);
                    Log::Field("FieldNavigation: [chase-drive] A*+funnel: %d waypoints from tri %d to %d",
                               s_waypointCount, startTri, goalTri);
                }
            }
            // Pre-skip waypoints we're already close to at drive start.
            if (s_waypointCount > 1 && s_usingFunnel) {
                float wpSkipDist = FUNNEL_ARRIVE_DIST * 2.0f;
                while (s_waypointIdx < s_waypointCount - 1) {
                    float wdx = s_waypoints[s_waypointIdx][0] - _px;
                    float wdy = s_waypoints[s_waypointIdx][1] - _pz;
                    float wd = sqrtf(wdx*wdx + wdy*wdy);
                    if (wd >= wpSkipDist) break;
                    Log::Field("FieldNavigation: [chase-drive] pre-skip wp %d (dist=%.0f < %.0f)",
                               s_waypointIdx, wd, wpSkipDist);
                    s_waypointIdx++;
                }
            }
        } else {
            Log::Field("FieldNavigation: [chase-drive] A* skipped: start=%d goal=%d",
                       startTri, goalTri);
        }
    } else {
        Log::Field("FieldNavigation: [chase-drive] no walkmesh -- straight-line steering only");
    }

    // Start distance for telemetry.
    {
        float sdx = _tx - _px, sdz = _tz - _pz;
        s_driveStartDist = sqrtf(sdx*sdx + sdz*sdz);
    }

    // Walk modifier (independent of F9 -- F9 always runs).
    if (walk) {
        InjectKey(SC_W_CANCEL_DD, true);
        s_chaseDriveWalk = true;
    } else {
        s_chaseDriveWalk = false;
    }

    // v0.15.9.2.1: Cache target coords for UpdateAutoDrive to read.
    s_chaseDriveTargetX = targetX;
    s_chaseDriveTargetY = targetY;

    s_chaseDriveActive = true;

    Log::Field("FieldNavigation: [chase-drive] STARTED tgt=(%d,%d) walk=%d "
               "player=(%.0f,%.0f) waypoints=%d startDist=%.0f trigIdx=%d "
               "crossLine=%s",
               targetX, targetY, (int)walk, _px, _pz,
               s_waypointCount, s_driveStartDist, triggerLineIdx,
               s_driveCrossLineActive ? "yes" : "no");
    return true;
}

void StopChaseDrive()
{
    if (!s_chaseDriveActive) return;

    if (s_chaseDriveWalk) {
        InjectKey(SC_W_CANCEL_DD, false);
        s_chaseDriveWalk = false;
    }

    // Reuse F9's StopAutoDrive: releases arrows, deactivates analog override,
    // removes the fake gamepad, clears drive state. nullptr reason suppresses
    // the "Cancelled." SAPI announce -- chase-drive should be silent.
    StopAutoDrive(nullptr);

    s_chaseDriveActive = false;
    Log::Field("FieldNavigation: [chase-drive] STOPPED");
}

bool IsChaseDriveActive()
{
    // v0.15.9.2.1: Gate on s_driveActive too. F9's StopAutoDrive can fire
    // internally during chase-drive (rare, but the BAT log shows it happens
    // if the entity-catalog check trips before we can bypass it). When that
    // happens, s_driveActive flips to false but s_chaseDriveActive stays
    // true (StopAutoDrive doesn't know about chase-drive's flag). Without
    // this gate, chase_auto_pilot's per-tick IsChaseDriveActive() check
    // would keep returning true forever, and the diagnostic would log a
    // zombie engagement while the actual drive is dead. With the gate,
    // chase_auto_pilot's Update sees IsChaseDriveActive=false on the
    // next tick after an internal stop and calls Disengage cleanly.
    return s_chaseDriveActive && s_driveActive;
}
