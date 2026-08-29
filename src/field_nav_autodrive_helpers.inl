// DRIVE_VEC_LOG_FORMAT -- the [drive-vec] per-tick steering diagnostic, moved
// here in v0.131.1 to keep field_nav_autodrive.inl under the 80 KB gate.
    // Logs the intermediate values at each stage of the steering pipeline so
    // we can see WHICH STAGE produced the wrong direction when the drive gets
    // stuck. Fires every DRIVE_VEC_LOG_INTERVAL ticks (~0.5 s at 60 Hz) -- the
    // existing 120-tick [drive] tick log was too sparse to catch transient
    // steering inversions (e.g. the v0.17.6.0 BAT showed lX=-840/lY=-542 for
    // multiple consecutive log windows, but the per-tick values likely jumped
    // around between recovery cycles). Format:
    //   t  = total ticks since drive start
    //   tri = engine-reported walkmesh triangle (read from entity +0x1FA)
    //   pp = player world position
    //   wpRaw = chosen funnel waypoint or final target (pre-corridor override)
    //   corOverride/corSteer = corridor steering wrote new steer to this edge midpoint
    //   trigRedir/finalDelta = trigger-line proximity rewrote dx/dz parallel
    //   lX/lY = analog values written by SetAnalogFromVector (camera-projected)
    //   kb = heading bitmask derived from analog (post v0.17.6.0 unified logic)
    //   wig/phase = wiggle tick counter / recovery phase counter
    // To disable, raise DRIVE_VEC_LOG_INTERVAL; the per-tick cost otherwise
    // is one mod-by-constant and an int compare.

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
// ============================================================================
// v0.18.3.306 (#111): transient player-position dropout tolerance.
//
// Aaron, .305 BAT: "It attempted to auto-drive to the stairs but kept saying
// 'Player Position Lost'. I have noticed this happening more and more."
//
// His guess -- that it is tied to crossing some underlying line on the field --
// is essentially right, and the log shows the shape of it precisely. The
// position is readable on the tick BEFORE and the tick AFTER; only the one
// sample in between fails:
//
//   14:51:39  [drive-vec] t=150 tri=3 pp=(-2386,-372)   <- fine
//   14:51:39  [drive] stopped: Player position lost.    <- next tick
//   14:51:42  [A*] Path found ... start=2               <- fine again
//
//   14:49:21  [drive-vec] t=270 tri=55 pp=(1627,275)    <- fine
//   14:49:21  [drive] stopped: Player position lost.    <- next tick
//
// GetEntityPos() only fails in-range when the entity's triangle id at 0x1FA
// reads 0 ("not yet placed") or the Others base pointer is momentarily null.
// Both are things the engine does WHILE re-resolving the player's walkmesh
// placement -- which is exactly what happens as you cross into a new triangle
// or a line fires. At 14:51:39 the drive had just advanced three waypoints in
// one second (tri 56 -> 6 -> 3), i.e. it was crossing triangles rapidly. This
// is a race between our per-frame poll and the engine's placement update, so
// it gets MORE likely the more auto-drive is used and the faster the player is
// moving -- which matches "happening more and more".
//
// The fix does not depend on knowing which of the two reads dips. One failed
// sample is simply not evidence that the player is gone, and this log proves
// that directly. So a dropout now pauses the drive the same way an open dialog
// does -- release the keys, freeze the stuck counter, keep the drive alive --
// and only gives up if the position stays unreadable for a bounded run of
// ticks. 30 ticks is ~0.5 s: far longer than any observed dropout (all were a
// single tick), far shorter than a player would wait wondering what happened.
//
// If the position really has gone for good -- left the field, entity destroyed
// -- IsOnField() and the field-transition handler stop the drive on their own
// paths, so nothing depends on this one being trigger-happy.
static const int DRIVE_POS_LOST_TOLERANCE_TICKS = 30;
static int s_drivePosLostTicks = 0;

// v0.18.3.318 (#114): transition DELIVERY + completion detection (replaces the
// .317 hand-off, which fired on the tri==0 window but was the wrong layer). The
// .317 BAT showed the drive was ALREADY stopping ~350u SHORT of the descent
// trigger -- a plain dist<arriveDist "Arrived" at the edge -- so the player never
// reached the threshold and the hand-off just bolted a premature "Stairs down"
// onto a drive that had not delivered (6 hand-offs + 6 "position lost" + 21
// restarts to descend 2 floors). New model: for a TRANSITION target
// (s_driveTrigTarget -- trigger-line exit / stairs / ring-crossing gateway) the
// ONLY success is the floor or field ACTUALLY changing. The drive does NOT
// "Arrive" by proximity (see UpdateAutoDrive: both Arrived exits are gated to
// non-transition targets); it steers THROUGH the trigger until the transition
// fires -- bounded by the existing driveMaxTicks / stuck timeouts so it can't
// loop forever -- and survives the multi-second off-walkmesh window instead of
// dying at 30 ticks and forcing a restart. Completion is detected by comparing
// the live field id / shaft floor against the values captured at drive start.
static const int DRIVE_TRANSITION_TOLERANCE_TICKS = 480;  // ~8s: covers the whole descent/crossing off-walkmesh window; the floor/field-change success below fires well before this on a real transition
static uint16_t  s_driveTransStartFieldId = 0xFFFF;        // field id captured at transition-drive start
static int       s_driveTransStartFloor   = -999;          // shaft floor captured at start (-1 off-shaft)
static bool      s_driveTransCaptured     = false;         // start state captured for the current drive?
static char      s_driveTransName[48]     = {0};           // target name, announced on completion

// Read the raw fields GetEntityPos() rejects on, for the dropout diagnostic.
// Deliberately its OWN function: MSVC C2712 forbids __try in a function that
// needs C++ object unwinding, and UpdateAutoDrive() is large and has none
// today -- putting the SEH here keeps it that way. Same pattern as
// GetEntityPos() in field_nav_helpers.inl.
static void ReadPlayerPlacementRaw(int entityIdx, bool& baseOk, unsigned& triOut)
{
    baseOk = false; triOut = 0xFFFF;
    if (entityIdx < 0 || entityIdx >= MAX_ENTITIES) return;
    if (!FF8Addresses::pFieldStateOthers) return;
    __try {
        uint8_t* b = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (b) {
            baseOk = true;
            triOut = *(uint16_t*)(b + ENTITY_STRIDE * entityIdx + 0x1FA);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.18.3.307 (#100): ring of the last few triangles a recovery cycle has
// fired in during the current drive. The v0.17.6.1 tri-advance reset treats
// ANY triangle change at a recovery cycle as corridor progress and refills
// the whole MAX_RECOVERY_PHASES budget. When steering oscillates between two
// or three triangles -- the .306 gpbig1a stairs drive ping-ponged across
// tri 8 <-> 9 <-> 10 for 40 seconds -- every hop re-earned a full budget, so
// the "Stuck." announcement was unreachable and the drive bounced silently
// until the global timeout. Revisiting a triangle we were recently stuck in
// is NOT progress; only genuinely new territory resets the counter now.
// Genuine corridor traversal is unaffected: each fresh triangle is absent
// from the ring (so it still resets), and waypoint advances reset the phase
// through their own independent path in UpdateAutoDrive(). Ring size 4
// covers the observed 2- and 3-triangle oscillations; a corridor longer
// than 4 triangles keeps earning resets as entries age out.
// Cleared at drive start (field_nav_handlekeys.inl).
static const int RECOVERY_TRI_RING_SIZE = 4;
static uint16_t s_recoveryTriRing[RECOVERY_TRI_RING_SIZE] =
    { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
static int s_recoveryTriRingPos = 0;

static void RecoveryRingClear()
{
    for (int i = 0; i < RECOVERY_TRI_RING_SIZE; i++)
        s_recoveryTriRing[i] = 0xFFFF;
    s_recoveryTriRingPos = 0;
}

static bool RecoveryRingContains(uint16_t tri)
{
    for (int i = 0; i < RECOVERY_TRI_RING_SIZE; i++)
        if (s_recoveryTriRing[i] == tri) return true;
    return false;
}

static void RecoveryRingPush(uint16_t tri)
{
    if (tri == 0xFFFF || RecoveryRingContains(tri)) return;
    s_recoveryTriRing[s_recoveryTriRingPos] = tri;
    s_recoveryTriRingPos = (s_recoveryTriRingPos + 1) % RECOVERY_TRI_RING_SIZE;
}

// v0.18.3.309 (#114): pinned-nudge escape variation state.
// The even-phase recovery nudge (field_nav_autodrive.inl) presses perpendicular
// to the shared edge with the next corridor triangle. .308 fixed WHICH edge it
// uses, but on the D-District shaft the player still pins at moveDist=0 because
// the stair rails and the knee-wall around the central hole are COLLISION
// geometry the walkmesh does not contain (verified offline: the player->waypoint
// segment crosses no walkmesh wall, yet the engine refuses the move). A fixed
// nudge that happens to press into an invisible rail fails every time, and
// repeating it cannot succeed. These track how many consecutive nudges have
// left the player essentially stationary; the recovery rotates its escape
// direction and lengthens the nudge as the count climbs, then resets the
// instant the player actually moves. Cleared at drive start.
static int   s_drivePinnedCount = 0;
static float s_drivePinnedPosX  = 0.0f;
static float s_drivePinnedPosY  = 0.0f;
static const float DRIVE_PIN_MOVE_EPS = 45.0f;   // v0.18.3.320 (#114): 12->45. The .319 stairs BAT wedged with a ~38u E-W wiggle that kept resetting the pin count (moved>12) so the escape never escalated though dist was frozen at 663. 45 treats an in-place wiggle as pinned; a genuinely advancing player still covers far more per nudge.

// v0.120.0 (#centra): end the drive with the sentence the player needs. On an
// ordinary target that is still "Arrived."; on a line the script gates behind a
// BTNTEST it names the key they have to press, resolved through the engine's own
// keymap so a keyboard player hears "Enter" and not "Cross". The mask usually
// names two buttons because the game takes either -- the first one the engine
// can put a key to wins, and the pad name is the fallback when it cannot put a
// key to any of them. button_mask_model.inl.
static void StopAutoDrive(const char* reason);
static void DriveArriveAtButtonLine(float alongT = -99.0f)
{
    if (!BtnMaskIsActionable(s_driveButtonMask)) { StopAutoDrive("Arrived."); return; }
    char key[32];  key[0] = '\0';
    const char* keyName = nullptr;
    const char* padName = nullptr;
    for (int i = 0; i < 16; i++) {
        const int b = BtnMaskNthButton(s_driveButtonMask, i);
        if (b < 0) break;
        if (padName == nullptr) padName = BtnPadName(b);
        if (FF8TextDecode::ButtonKeyName(b, key, sizeof key) && key[0]) { keyName = key; break; }
    }
    char press[160];
    BtnPressText(press, sizeof press, s_driveButtonLabel, keyName, padName,
                 s_driveButtonLeadsTo);
    Log::Field("FieldNavigation: [drive] arrived at a BTNTEST line 0x%04X alongside t=%.2f "
               "-- \"%s\" [v0.122.0]",
               (unsigned)s_driveButtonMask, alongT, press);
    StopAutoDrive(press[0] ? press : "Arrived.");
}

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
    s_drivePosLostTicks = 0;   // v0.18.3.306 (#111)
    // v0.18.3.318 (#114): clear transition-completion capture for the next drive.
    s_driveTransCaptured     = false;
    s_driveTransStartFieldId = 0xFFFF;
    s_driveTransStartFloor   = -999;
    s_driveTransName[0]      = '\0';
    s_driveTrigTarget = false;
    s_driveTrigCrossStart = 0.0f;
    s_driveSkipTrigIdx = -1;
    // v0.15.9.2.15: clear chase-drive crossing line state.
    s_driveCrossLineActive = false;
    s_driveCrossLineX1 = 0; s_driveCrossLineY1 = 0;
    s_driveCrossLineX2 = 0; s_driveCrossLineY2 = 0;
    // v0.118.0 (#centra): the island-bridge redirect ends with the drive.
    s_driveBridgeActive  = false;
    s_driveBridgeLineIdx = -1;
    s_driveBridgeX = 0.0f; s_driveBridgeY = 0.0f;
    s_driveGoalZ = 0.0f; s_driveGoalZValid = false;   // v0.119.0
    s_driveButtonMask = 0; s_driveButtonLabel[0] = '\0';   // v0.120.0
    s_driveButtonLeadsTo[0] = '\0';                        // v0.121.0
    Log::Field("FieldNavigation: [drive] stopped: %s",
               reason ? reason : "(silent -- field change or handoff)");
    // v0.15.9.2.1: Suppress SAPI announce when chase-drive owns the drive.
    // Internal stops during chase-drive (e.g., "No target." from stale
    // catalog state, "Stuck." from a wall, "Arrived." when path-finding
    // completes) are confusing to the player -- chase auto-pilot is supposed
    // to be silent. chase_auto_pilot detects the disengage via IsChaseDriveActive()
    // returning false (gated on s_driveActive) and disengages cleanly on
    // the next Update tick.
    if (reason && !s_chaseDriveActive) ScreenReader::Speak(reason);
}

// ============================================================================
// v0.130.0 (#centra): A SIDE-FLIP IS NOT AN ARRIVAL, AND NEITHER IS PAST AN END
// ============================================================================
// Aaron: "auto-drive is unreliable and doesn't always take me to the ladder."
// Two log lines say why.
//
// 17:58:25 -- "arrived at a BTNTEST line 0x00C0 alongside t=0.76 -- Ladder Down.
// Press X to use it." The player was at (175,-329); crroof1's `lad1` runs
// (444,-509) to (505,-385). That is **320 units away**. The arrival came from
// the CROSSING path, where the player's side of the line's INFINITE extension
// flips, and v0.18.3.304 bounded that with the projection parameter alone. t was
// 0.76 -- perfectly inside the segment -- because t says WHERE ALONG the line
// the foot falls and nothing whatever about how far off to the side the player
// is. Walking parallel to a line three hundred units away sweeps t from 0 to 1
// without ever approaching it.
//
// 18:15:04 and 18:21:22 -- "alongside t=1.02" and "t=1.03", both inside the
// ±15% end margin, both announced, and both times the X press did nothing at
// all: sixteen mod ladder steps played over eight seconds while NAV-OBSERVE
// recorded him walking around with the arrow keys. The margin exists so a drive
// that is nearly there is not told to keep walking forever. It was never a
// statement about where the engine accepts the button.
//
// So both arrival paths now ask both questions -- inside the segment, and near
// it. Being too strict is the safe failure: the aim point is the clamped point
// ON the segment, so a drive that is not yet inside simply keeps walking and
// arrives a moment later. Being too loose is the silent one, and it has now cost
// four BATs.
static bool DriveButtonLineArrival(float px, float pz, float t,
                                   float x1, float y1, float x2, float y2,
                                   float arriveDist, bool logWhenFar)
{
    const float dSq = BtnPointSegDistSq(px, pz, x1, y1, x2, y2);
    const bool  ok  = BtnLineArrivalOk(t, dSq, arriveDist);
    if (!ok && logWhenFar) {
        static int s_flipFarLogged = 0;
        if (s_flipFarLogged < 8) {
            s_flipFarLogged++;
            Log::Field("FieldNavigation: [drive] side-flip at t=%.2f, %.0f units from "
                       "the segment -- NOT arrived, still walking [v0.130.0]",
                       t, sqrtf(dSq));
        }
    }
    return ok;
}

// ============================================================================
// v0.131.1 (#centra): THE PROGRESS MONITOR, WIRED
// ============================================================================
// One call per drive tick decides which of the two failures is happening and
// applies the matching remedy. drive_progress_model.inl carries the reasoning
// and the thresholds; this is the state and the log.
static DriveProgressTracker s_driveProg;
static unsigned s_driveProgLastMs   = 0;    // v0.131.4: the window is 1.6 s, not 32 ticks
static bool     s_driveProgSampled  = false;
static int   s_driveRedirectSuppress = 0;   // ticks left with avoidance off
static int   s_driveProbeIdx         = -1;  // -1 = not probing
static int   s_driveProbeTicks       = 0;
static float s_driveProbeStartX      = 0.0f;
static float s_driveProbeStartY      = 0.0f;
static int   s_driveProbeHold        = 0;   // v0.131.2: ticks left holding a winner
static int   s_driveProbeHoldAngle   = 0;
static int   s_driveProbeLastGoodIdx = -1;

static void DriveProgressResetForNewDrive(float px, float pz)
{
    DriveProgressClear(&s_driveProg);
    s_driveProgSampled      = false;
    s_driveRedirectSuppress = 0;
    s_driveProbeIdx         = -1;
    s_driveProbeTicks       = 0;
    s_driveProbeStartX      = px;
    s_driveProbeStartY      = pz;
    s_driveProbeHold        = 0;
    s_driveProbeHoldAngle   = 0;
    s_driveProbeLastGoodIdx = -1;
}

// Returns true while the trigger-line avoidance should stay out of the way.
static bool DriveProgressTick(float px, float pz)
{
    if (s_driveRedirectSuppress > 0) s_driveRedirectSuppress--;

    // v0.131.4: sample on the clock. A drive tick is not a game frame -- this
    // loop outruns the renderer, so consecutive ticks routinely see the same
    // position and thirty-two of them covered no time at all.
    const unsigned nowMs = (unsigned)GetTickCount();
    if (!DriveProgressDueForSample(nowMs, s_driveProgLastMs, s_driveProgSampled))
        return s_driveRedirectSuppress > 0;
    s_driveProgLastMs  = nowMs;
    s_driveProgSampled = true;
    DriveProgressPush(&s_driveProg, px, pz);

    // OSCILLATION: moving hard, going nowhere. crroof1 tri 34, where the
    // avoidance flipped the steer parallel to a line every other tick.
    if (s_driveRedirectSuppress == 0 && s_driveProbeIdx < 0 &&
        DriveIsOscillating(&s_driveProg)) {
        s_driveRedirectSuppress = DRIVE_REDIRECT_SUPPRESS_TICKS;
        Log::Field("FieldNavigation: [drive] oscillating -- %.0f units walked, %.0f "
                   "units gained. Trigger-line avoidance off for %d ticks, steering "
                   "straight at the waypoint [v0.131.1]",
                   DriveProgressPath(&s_driveProg), DriveProgressNet(&s_driveProg),
                   DRIVE_REDIRECT_SUPPRESS_TICKS);
        DriveProgressClear(&s_driveProg);
    }

    // HARD PIN: not moving at all. crtower3 tri 89, seven seconds of moveDist=0.
    if (s_driveProbeIdx < 0 && DriveIsStalled(&s_driveProg)) {
        // v0.131.2: a pin that lands while the last winner is still being held is
        // the same wall, so resume at the NEXT angle rather than re-trying one
        // that already bought only half a second.
        s_driveProbeIdx    = DriveProbeResumeIndex(s_driveProbeHold > 0,
                                                   s_driveProbeLastGoodIdx);
        s_driveProbeTicks  = 0;
        s_driveProbeStartX = px;
        s_driveProbeStartY = pz;
        s_driveProbeHold   = 0;
        Log::Field("FieldNavigation: [drive] pinned -- %.0f units walked in the last "
                   "%d ms. Sweeping the heading from %d degrees [v0.131.4]",
                   DriveProgressPath(&s_driveProg),
                   DRIVE_PROG_SAMPLES * DRIVE_PROG_SAMPLE_MS,
                   DriveProbeAngle(s_driveProbeIdx));
        DriveProgressClear(&s_driveProg);
    }
    return s_driveRedirectSuppress > 0;
}

// Rotates the steering while a sweep is running, and ends the sweep the moment
// the player actually moves -- keeping the angle that worked, so he slides along
// the wall instead of turning straight back into it.
static void DriveProbeApply(float px, float pz, float* dx, float* dz)
{
    if (dx == nullptr || dz == nullptr) return;

    // v0.131.2: still riding the angle that got him moving. Dropping it the
    // instant he moved is what made the 19:46 run pin seven times in ninety
    // seconds -- he cleared the contact and steered straight back into the wall.
    if (s_driveProbeIdx < 0) {
        if (s_driveProbeHold > 0) {
            s_driveProbeHold--;
            float hx = 0.0f, hy = 0.0f;
            DriveRotateSteer(*dx, *dz, s_driveProbeHoldAngle, &hx, &hy);
            *dx = hx; *dz = hy;
        }
        return;
    }

    // v0.131.3: measured against the direction the drive WANTED, so a reversal
    // can no longer report itself as an escape. *dx/*dz on entry is the
    // un-rotated steer, which is exactly that direction.
    const float mdx = px - s_driveProbeStartX, mdy = pz - s_driveProbeStartY;
    if (DriveProbeMovedUsefully(mdx, mdy, *dx, *dz)) {
        s_driveProbeHoldAngle   = DriveProbeAngle(s_driveProbeIdx);
        s_driveProbeLastGoodIdx = s_driveProbeIdx;
        s_driveProbeHold        = DRIVE_PROBE_HOLD_TICKS;
        Log::Field("FieldNavigation: [drive] unpinned at %d degrees, %.0f units toward "
                   "the target -- holding it for %d ticks [v0.131.3]",
                   s_driveProbeHoldAngle, sqrtf(mdx * mdx + mdy * mdy),
                   DRIVE_PROBE_HOLD_TICKS);
        s_driveProbeIdx = -1;
        return;
    }

    if (++s_driveProbeTicks >= DRIVE_PROBE_TICKS) {
        s_driveProbeTicks  = 0;
        s_driveProbeStartX = px;
        s_driveProbeStartY = pz;
        if (++s_driveProbeIdx >= DRIVE_PROBE_COUNT) {
            Log::Field("FieldNavigation: [drive] heading sweep found nothing -- this is "
                       "a pathing problem, handing back to recovery [v0.131.2]");
            s_driveProbeIdx         = -1;
            s_driveProbeLastGoodIdx = -1;
            return;
        }
    }
    float rx = 0.0f, ry = 0.0f;
    DriveRotateSteer(*dx, *dz, DriveProbeAngle(s_driveProbeIdx), &rx, &ry);
    *dx = rx; *dz = ry;
}
