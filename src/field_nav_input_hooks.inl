// field_nav_input_hooks.inl — DinputUpdateGamepad, EngineEvalInput, GetKeyState hooks
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// v05.82: engine_eval_keyboard_gamepad_input post-call diagnostic hook
// ============================================================================
// Fires AFTER the original function processes keyboard/gamepad input.
// Dumps the gamepad_states entry and engine button state once per second
// while on a field, so we can see what values the keyboard path produces.
// This tells us whether analog_lx/ly are populated by keyboard input
// or whether we need a different injection point.

// v05.84: Hook dinput_update_gamepad_status to prevent it from calling
// IDirectInputDevice8::Poll()/GetDeviceState() on our fake device sentinel.
// When fake gamepad is installed, we skip the real poll and just return
// our fake DIJOYSTATE2 pointer (the game expects the return value to be
// the DIJOYSTATE2 pointer or null).
typedef void* (__cdecl *DinputUpdateGamepad_t)();
static DinputUpdateGamepad_t s_originalDinputUpdateGamepad = nullptr;

// v05.89: Hook get_key_state to zero arrow keys when auto-drive is active.
// This runs INSIDE engine_eval_keyboard_gamepad_input, after the keyboard buffer
// is filled but before ctrl_keyboard_actions reads direction from it.
// The arrow keys were injected via SendInput to trigger "player wants to move"
// abstract buttons, but we don't want them to determine the movement DIRECTION.
// The gamepad analog path (via FFNx's ff8_get_analog_value) provides the direction.
static int __cdecl HookedGetKeyState()
{
    // Call the original (or FFNx's replacement) to fill the keyboard buffer.
    int result = 0;
    if (s_originalGetKeyState)
        result = s_originalGetKeyState();

    // If auto-drive analog override is active, zero arrow key scancodes
    // so ctrl_keyboard_actions sees no keyboard direction.
    if (s_analogOverrideActive && FF8Addresses::HasKeyboardState()) {
        __try {
            uint8_t* kbBuf = *FF8Addresses::pKeyboardState;
            if (kbBuf) {
                static bool s_kbSuppressLogged = false;
                if (!s_kbSuppressLogged) {
                    s_kbSuppressLogged = true;
                    Log::Field("FieldNavigation: [v05.89] get_key_state hook: zeroing arrows "
                               "(buf=0x%08X, up=%02X dn=%02X lt=%02X rt=%02X)",
                               (uint32_t)(uintptr_t)kbBuf,
                               kbBuf[0x48], kbBuf[0x50], kbBuf[0x4B], kbBuf[0x4D]);
                }
                kbBuf[0x48] = 0;  // Up
                kbBuf[0x50] = 0;  // Down
                kbBuf[0x4B] = 0;  // Left
                kbBuf[0x4D] = 0;  // Right
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    return result;
}

static void* __cdecl HookedDinputUpdateGamepad()
{
    if (s_fakeGamepadInstalled) {
        // Don't poll the fake device — just return our fake state.
        // The original function returns LPDIJOYSTATE2 (or null on failure).
        return (void*)s_fakeDIJOYSTATE2;
    }
    // v0.11.07: Also skip if another module (e.g. WorldMap) installed a fake device.
    // Any device pointer in the sentinel range (>= 0xDEAD0000) is synthetic.
    if (FF8Addresses::HasDinputGamepadPtrs()) {
        uint32_t devPtr = *FF8Addresses::pDinputGamepadDevicePtr;
        if (devPtr >= 0xDEAD0000) {
            // Another module's fake gamepad — return their state pointer.
            uint32_t statePtr = *FF8Addresses::pDinputGamepadStatePtr;
            return (statePtr != 0) ? (void*)(uintptr_t)statePtr : nullptr;
        }
    }
    // No fake installed — call the real poll function.
    if (s_originalDinputUpdateGamepad)
        return s_originalDinputUpdateGamepad();
    return nullptr;
}

static void __cdecl HookedEngineEvalInput()
{
    // v05.84: If analog override is active, write our desired lX/lY into the
    // fake DIJOYSTATE2 BEFORE calling the original. The original function will
    // see dinput_gamepad_device as non-null (our sentinel) and the hooked
    // dinput_update_gamepad_status will return our fake DIJOYSTATE2.
    if (s_analogOverrideActive && s_fakeGamepadInstalled) {
        // DIJOYSTATE2: lX at offset 0, lY at offset 4 (both LONG/int32_t)
        // Range: -1000 to +1000 (DirectInput axis range for FF8)
        *(int32_t*)(s_fakeDIJOYSTATE2 + 0) = s_analogDesiredLX;
        *(int32_t*)(s_fakeDIJOYSTATE2 + 4) = s_analogDesiredLY;
    }

    // v06.13: Approach C — snapshot player 2D position BEFORE engine_eval.
    // If the position changes after the call, the engine performed a 3D→2D
    // projection during this frame. We log the stack to find the function.
    int32_t projSnapX = 0, projSnapY = 0;
    bool projSnapValid = false;
    if (s_projDiagCount < PROJ_DIAG_MAX && s_playerEntityIdx >= 0 &&
        FF8Addresses::pFieldStateOthers && FF8Addresses::IsOnField()) {
        __try {
            uint8_t* pBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (pBase) {
                uint8_t* pBlk = pBase + ENTITY_STRIDE * s_playerEntityIdx;
                projSnapX = *(int32_t*)(pBlk + 0x190);
                projSnapY = *(int32_t*)(pBlk + 0x194);
                projSnapValid = true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Call the original — it will now process our fake gamepad data.
    if (s_originalEngineEvalInput)
        s_originalEngineEvalInput();

    // v06.13: Approach C — check if position changed during engine_eval.
    if (projSnapValid && s_projDiagCount < PROJ_DIAG_MAX) {
        __try {
            uint8_t* pBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (pBase) {
                uint8_t* pBlk = pBase + ENTITY_STRIDE * s_playerEntityIdx;
                int32_t afterX = *(int32_t*)(pBlk + 0x190);
                int32_t afterY = *(int32_t*)(pBlk + 0x194);
                // Only log if the position actually changed (player moved this frame).
                if (afterX != projSnapX || afterY != projSnapY) {
                    // Only log on first detection, then periodically.
                    if (s_projDiagCount == 0 || s_projDiagCount == 5) {
                        // Walk the stack to find return addresses.
                        // On x86, EBP chain gives us the call stack.
                        uint32_t stackAddrs[8] = {};
                        int stackDepth = 0;
                        __try {
                            uint32_t* ebp;
                            __asm { mov ebp, ebp }
                            // CaptureStackBackTrace is safer than manual EBP walking.
                            stackDepth = (int)CaptureStackBackTrace(0, 8, (PVOID*)stackAddrs, NULL);
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        Log::Field("FieldNavigation: [PROJDIAG] #%d position changed during engine_eval: "
                                   "before=(%d,%d)/4096=(%d,%d) after=(%d,%d)/4096=(%d,%d) "
                                   "delta=(%d,%d)",
                                   s_projDiagCount,
                                   projSnapX, projSnapY, projSnapX/4096, projSnapY/4096,
                                   afterX, afterY, afterX/4096, afterY/4096,
                                   afterX - projSnapX, afterY - projSnapY);
                        if (stackDepth > 0) {
                            char stackBuf[256] = {};
                            int pos = 0;
                            for (int s = 0; s < stackDepth && pos < 240; s++) {
                                pos += snprintf(stackBuf + pos, 256 - pos, "0x%08X ", stackAddrs[s]);
                            }
                            Log::Field("FieldNavigation: [PROJDIAG]   stack(%d): %s", stackDepth, stackBuf);
                        }
                        // Also log the engine_eval address for reference.
                        Log::Field("FieldNavigation: [PROJDIAG]   engine_eval=0x%08X set_tri=0x%08X",
                                   FF8Addresses::engine_eval_keyboard_gamepad_input_addr,
                                   FF8Addresses::set_current_triangle_addr);
                    }
                    s_projDiagCount++;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // v05.88 Approach D post-call suppression removed in v05.89.
    // Arrow key suppression now happens inside HookedGetKeyState, which runs
    // between get_key_state (buffer fill) and ctrl_keyboard_actions (direction read).

    // Diagnostic: dump state once per second while on field (first 10 field-mode dumps only).
    if (!s_gpDiagEnabled) return;
    if (!FF8Addresses::IsOnField()) return;

    DWORD now = GetTickCount();
    if ((now - s_gpDiagLastDump) < 1000) return;
    s_gpDiagLastDump = now;

    static int s_gpDiagCount = 0;
    if (s_gpDiagCount >= 10) {
        s_gpDiagEnabled = false;
        Log::Field("FieldNavigation: [GPDIAG2] 10 field dumps complete, diagnostic disabled.");
        return;
    }
    s_gpDiagCount++;

    __try {
        uint8_t* gp = FF8Addresses::pGamepadStates;
        if (!gp) return;
        uint8_t entries_offset = gp[0x18];
        uint8_t gamepad_options = gp[0xC3];

        Log::Field("FieldNavigation: [GPDIAG2] #%d entries_offset=%u gamepad_options=0x%02X override=%s fakeGP=%s lX=%d lY=%d",
                   s_gpDiagCount, (unsigned)entries_offset, (unsigned)gamepad_options,
                   s_analogOverrideActive ? "ON" : "off",
                   s_fakeGamepadInstalled ? "YES" : "no",
                   (int)s_analogDesiredLX, (int)s_analogDesiredLY);

        // Dump the active entry.
        if (entries_offset < 8) {
            uint8_t* entry = gp + 0x1C + entries_offset * 20;
            Log::Field("FieldNavigation: [GPDIAG2]   entry[%u] adis=%u aflg=0x%02X kscan=0x%04X "
                       "rx=%u ry=%u lx=%u ly=%u kon=0x%04X kinv=0x%04X",
                       (unsigned)entries_offset,
                       entry[0], entry[1], *(uint16_t*)(entry + 2),
                       entry[4], entry[5], entry[6], entry[7],
                       *(uint16_t*)(entry + 16), *(uint16_t*)(entry + 18));
        }

        if (FF8Addresses::pEngineInputValidButtons && FF8Addresses::pEngineInputConfirmedButtons) {
            Log::Field("FieldNavigation: [GPDIAG2]   validButtons=0x%08X confirmedButtons=0x%08X",
                       *FF8Addresses::pEngineInputValidButtons,
                       *FF8Addresses::pEngineInputConfirmedButtons);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [GPDIAG2] Exception reading gamepad state");
        s_gpDiagEnabled = false;
    }
}
