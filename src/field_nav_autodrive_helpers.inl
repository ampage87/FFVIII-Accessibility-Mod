// field_nav_autodrive_helpers.inl — Auto-drive low-level input + lifecycle helpers
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.17.8.20: Extracted from field_nav_autodrive.inl (which had reached 79.83 KB,
// ~170 bytes under the 80 KB hard cap — see the Chapter 4 push hiccup in DEVNOTES).
// Pure file-organization split, ZERO behavior change. Holds the analog/keyboard
// injection primitives and the drive-stop lifecycle, all called from
// UpdateAutoDrive (field_nav_autodrive.inl) and from field_nav_directiondrive.inl.
// MUST be included BEFORE field_nav_autodrive_calib.inl and field_nav_autodrive.inl
// so SetHeldDirections / SetAnalogFromVector / StopAutoDrive are visible to both.
// All file-scope statics these touch (s_driveHeld, DIR_*/SC_*, s_analog*,
// s_drive*, s_cam*/s_driveCam*, s_chaseDrive*) are declared in field_navigation.cpp
// above the include block; textual .inl includes share one translation unit.

// ============================================================================
// Auto-drive: inject arrow-key input to walk toward the selected entity
// ============================================================================

// Inject or release a direction key via SendInput using hardware scan codes.
// DirectInput reads raw hardware scan codes, so we must use KEYEVENTF_SCANCODE
// rather than KEYEVENTF_EXTENDEDKEY+VK.  Arrow keys have the E0 extended prefix,
// indicated by KEYEVENTF_EXTENDEDKEY alongside KEYEVENTF_SCANCODE.
//
// v0.15.9.7.8: The 'extended' parameter (default true preserves backward
// compatibility for arrow-key callers) controls whether KEYEVENTF_EXTENDEDKEY
// is set. Arrow keys are extended (E0-prefixed scancodes); letter keys like
// W (the walk modifier for chase auto-pilot) are NOT extended. Setting the
// flag for a non-extended key causes the OS to inject a malformed scancode
// (E0 + the letter scancode) that the game's DirectInput keyboard reader
// doesn't recognize as the actual key. This bug was present since the
// function was first written but only manifested in v0.15.9.7+ when chase
// auto-pilot started using InjectKey for W. world_map.cpp's separate car-
// control PressKey/ReleaseKey was already fixed in v0.14.102 -- the same
// fix was just never applied to the field-navigation InjectKey.
//
// v0.15.9.7.8 also logs SendInput failures (return value != 1) so future
// dropped-input issues surface in the field log.
static void InjectKey(WORD scanCode, bool down, bool extended = true)
{
    // v0.15.9.11.3.4: During chase Auto, FF8's keyboard input comes
    // EXCLUSIVELY from chase_keyboard's synthetic buffer -- so we drive the
    // synthetic buffer directly and skip SendInput entirely.
    //
    // Disassembly of FF8_EN.exe's input pipeline (engine_eval_keyboard_gamepad_input
    // @0x00467D10, get_key_state @0x004685F0, ctrl_keyboard_actions @0x004A2E50)
    // confirmed FF8 reads keyboard state ONLY from the 256-byte DirectInput
    // device buffer whose pointer lives at *0x01CD02D8. engine_eval makes one
    // call per frame to get_keyboard_state (which FFNx replaces with
    // GetGameKeyState -> GetDeviceState, the call chase_keyboard hooks), stores
    // the returned buffer pointer, and every downstream reader -- engine_eval
    // itself, get_key_state, ctrl_keyboard_actions -- reads only that buffer.
    // There is NO path that reads OS-level key state for movement: no
    // GetAsyncKeyState, no GetKeyboardState, no WM_KEYDOWN consumption. (The
    // lone GetAsyncKeyState import is a Ctrl+Q WndProc hotkey, unrelated.)
    //
    // Consequence: while chase_keyboard is active, the SendInput() path below
    // reaches NOTHING in FF8 -- FF8 sees only the synthetic buffer. All
    // SendInput did was dump synthetic key events into the shared OS input
    // queue, where they interleaved with the user's physical key presses and
    // desynced SetHeldDirections' diff model. That is the root cause of the
    // v0.15.9.11.3.1/.2/.3 BAT failures (chase clean hands-off, caught when
    // keys pressed). Removing the SendInput call during chase Auto eliminates
    // the OS-queue collision by construction; the synthetic-buffer write below
    // is the complete and sufficient delivery path -- it produces the same
    // 0x80<->0x00 byte transitions the engine's edge detection needs, including
    // for the keep-alive pulse.
    //
    // Outside chase Auto (F9 path-finding, world-map AD): ChaseKeyboard::
    // IsActive() returns false; SendInput is the only delivery path and
    // behavior is unchanged.
    if (::ChaseKeyboard::IsActive()) {
        if (down) {
            ::ChaseKeyboard::SetScancodeDown((uint8_t)scanCode, extended);
        } else {
            ::ChaseKeyboard::SetScancodeUp((uint8_t)scanCode, extended);
        }
        return;
    }

    INPUT inp      = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = 0;      // must be 0 when using KEYEVENTF_SCANCODE
    inp.ki.wScan   = scanCode;
    DWORD flags    = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    if (extended) flags |= KEYEVENTF_EXTENDEDKEY;
    inp.ki.dwFlags = flags;
    UINT sent = SendInput(1, &inp, sizeof(INPUT));
    if (sent != 1) {
        Log::Field("FieldNavigation: [InjectKey] SendInput FAILED (sent=%u, expected=1) "
                   "scancode=0x%02X down=%d extended=%d -- input event dropped by OS",
                   sent, (unsigned)scanCode, down ? 1 : 0, extended ? 1 : 0);
    }
}

// Release all held direction keys and clear the held bitmask.
static void ReleaseAllDirections()
{
    if (s_driveHeld & DIR_UP)    InjectKey(SC_UP,    false);
    if (s_driveHeld & DIR_DOWN)  InjectKey(SC_DOWN,  false);
    if (s_driveHeld & DIR_LEFT)  InjectKey(SC_LEFT,  false);
    if (s_driveHeld & DIR_RIGHT) InjectKey(SC_RIGHT, false);
    s_driveHeld = 0;
}

// Apply a new desired direction bitmask: release keys no longer needed,
// press keys newly needed.
// v05.85: Keyboard injection is REQUIRED to activate the game's movement code
// path. Analog steering overrides the direction, but keyboard buttons are the
// trigger that makes the game process movement at all.
static void SetHeldDirections(uint8_t desired)
{
    uint8_t toRelease = s_driveHeld  & ~desired;
    uint8_t toPress   = desired & ~s_driveHeld;
    if (toRelease & DIR_UP)    InjectKey(SC_UP,    false);
    if (toRelease & DIR_DOWN)  InjectKey(SC_DOWN,  false);
    if (toRelease & DIR_LEFT)  InjectKey(SC_LEFT,  false);
    if (toRelease & DIR_RIGHT) InjectKey(SC_RIGHT, false);
    if (toPress   & DIR_UP)    InjectKey(SC_UP,    true);
    if (toPress   & DIR_DOWN)  InjectKey(SC_DOWN,  true);
    if (toPress   & DIR_LEFT)  InjectKey(SC_LEFT,  true);
    if (toPress   & DIR_RIGHT) InjectKey(SC_RIGHT, true);
    s_driveHeld = desired;
}

// v05.84/v06.14: Set analog override from a world-space direction vector.
// Converts (dx, dy) in entity/world space into DIJOYSTATE2 lX/lY values
// using the per-field camera axes to produce correct screen-relative input.
// DirectInput axis convention: lX +1000 = screen right, lY +1000 = screen down.
//
// v0.17.2: Reads the AUTO-DRIVE PRIVATE axis pair (s_driveCamRight/Down) so
// the empirical calibration's writes don't leak into manual-nav's projection.
//
// v0.17.6.0: Source of camera axes branches on which drive owns the wheel:
//   - Chase auto-pilot (s_chaseDriveActive == true): keep reading
//     s_driveCamRight/Down. The chase doc's "Auto-drive lessons" Finding #10
//     explicitly lists empirical calibration as the verified-working path for
//     rotated-camera chase fields (e.g. domt5_1 where camRight ~= (0,1)).
//     CALIB phase 1/2 still runs at chase-drive start (see UpdateAutoDrive)
//     and writes the empirical result into s_driveCam*.
//   - F9 path-finding auto-drive (s_chaseDriveActive == false): read the
//     MANUAL-NAV pair s_camRight/Down, which is set at field load by
//     HookedFieldScriptsInit from the .ca file with 2D normalization, det
//     correction, and 90-degree quantization (v0.17.5). Manual nav has been
//     BAT-proven correct on the first announcement across bghall_1, bghall_4,
//     bg2f_1, bg2f_2, bgroom_1; F9 auto-drive sharing those axes means F9
//     also gets correct steering on every field with a .ca file, with no
//     warmup phase and no CALIB-can-fail edge case (the bghall_1 bug from
//     NEXT_SESSION_PROMPT: phase 1 fails when the player is wedged against
//     geometry, leaves default driveCamRight=(1,0) untouched, and steering
//     uses wrong axes on rotated cameras).
//
// The two-source split also unblocks future unification (chase doc Finding
// #28: parallel implementations cost five wasted BAT cycles): once F9 with
// quantized axes proves stable in production, chase-drive can switch to the
// same source by adding it to the chase-drive doc's per-field verification
// matrix.
static void SetAnalogFromVector(float dx, float dy)
{
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) {
        s_analogDesiredLX = 0;
        s_analogDesiredLY = 0;
        return;
    }
    float nx = dx / len;
    float ny = dy / len;
    // v0.17.6.0: Select axis source per drive type. See block comment above.
    float camRX, camRY, camDX, camDY;
    if (s_chaseDriveActive) {
        camRX = s_driveCamRightX; camRY = s_driveCamRightY;
        camDX = s_driveCamDownX;  camDY = s_driveCamDownY;
    } else {
        camRX = s_camRightX; camRY = s_camRightY;
        camDX = s_camDownX;  camDY = s_camDownY;
    }
    // v06.14: Project world-space direction onto camera axes.
    // lX = dot(worldDir, camRight) = how much of the desired direction
    //   aligns with the camera's rightward axis.
    // lY = dot(worldDir, camDown) = how much aligns with camera's downward axis.
    float lxF = (nx * camRX + ny * camRY) * 1000.0f;
    float lyF = (nx * camDX + ny * camDY) * 1000.0f;
    int lx = (int)lxF;
    int ly = (int)lyF;
    if (lx < -1000) lx = -1000; if (lx > 1000) lx = 1000;
    if (ly < -1000) ly = -1000; if (ly > 1000) ly = 1000;
    s_analogDesiredLX = lx;
    s_analogDesiredLY = ly;
}

// Stop auto-drive cleanly: release keys, clear state, optionally speak reason.
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    // v05.85: Release any held keyboard direction keys.
    ReleaseAllDirections();
    // v05.84: Deactivate analog override and remove fake gamepad.
    s_analogOverrideActive = false;
    s_analogDesiredLX = 0;
    s_analogDesiredLY = 0;
    // Restore original dinput pointers.
    if (s_fakeGamepadInstalled && FF8Addresses::HasDinputGamepadPtrs()) {
        *FF8Addresses::pDinputGamepadDevicePtr = s_savedDevicePtr;
        *FF8Addresses::pDinputGamepadStatePtr  = s_savedStatePtr;
        s_fakeGamepadInstalled = false;
        Log::Field("FieldNavigation: [drive] fake gamepad removed, original ptrs restored");
    }
    // v06.21: Do NOT restore talk radius here — the player needs the expanded
    // radius to persist so they can press X to interact after "Arrived".
    // The game's TALKRADIUS opcode resets it naturally on the next field load.
    if (s_driveTalkRadExpanded) {
        Log::Field("FieldNavigation: [drive] talkRadius stays expanded (%u -> %u) for ent%d — resets on field load",
                   (unsigned)s_driveOrigTalkRadius,
                   (unsigned)GetEntityTalkRadius(s_driveTargetEntityIdx),
                   s_driveTargetEntityIdx);
    }
    s_driveTalkRadExpanded = false;
    s_driveTargetEntityIdx = -1;
    s_driveOrigTalkRadius = 0;

    // v06.08: NavLog drive end
    NavLog::DriveEnd(reason ? reason : "unknown", s_driveTotalTicks, 0.0f,
                     s_driveWigglePhase, s_driveStartDist);

    s_driveActive = false;
    s_driveTrigTarget = false;
    s_driveTrigCrossStart = 0.0f;
    s_driveSkipTrigIdx = -1;
    // v0.15.9.2.15: clear chase-drive crossing line state.
    s_driveCrossLineActive = false;
    s_driveCrossLineX1 = 0; s_driveCrossLineY1 = 0;
    s_driveCrossLineX2 = 0; s_driveCrossLineY2 = 0;
    Log::Field("FieldNavigation: [drive] stopped: %s", reason);
    // v0.15.9.2.1: Suppress SAPI announce when chase-drive owns the drive.
    // Internal stops during chase-drive (e.g., "No target." from stale
    // catalog state, "Stuck." from a wall, "Arrived." when path-finding
    // completes) are confusing to the player -- chase auto-pilot is supposed
    // to be silent. chase_auto_pilot detects the disengage via IsChaseDriveActive()
    // returning false (gated on s_driveActive) and disengages cleanly on
    // the next Update tick.
    if (reason && !s_chaseDriveActive) ScreenReader::Speak(reason);
}
