// chase_keyboard.h -- Synthetic keyboard buffer substitution for chase Auto.
//
// v0.15.9.11.3: Replaces the failed WH_KEYBOARD_LL approach. During chase
// Auto we own a 256-byte DirectInput keyboard state buffer; a vtable hook
// on IDirectInputDevice8::GetDeviceState (installed when FF8/FFNx creates
// the keyboard device) returns OUR buffer instead of the real one. The
// chase auto-pilot writes the keys it wants pressed (arrows + W) into
// this buffer via SetKeyDown/SetKeyUp; the user's physical key presses
// never reach the engine because we don't read from the real DirectInput
// device while Active.
//
// SCOPE: chase Auto only. Outside chase Auto the hook is a pure pass-
// through to the original GetDeviceState. F9 path-finding, world-map AD,
// and all normal field gameplay are unaffected.
//
// DESIGN NOTES (per the v0.15.9.11 / .11.1 / .11.2 failure analysis):
//
//   v0.15.9.11 attempted to ZERO FF8's engine kbBuf via HookedGetKeyState.
//   That kbBuf is FFNx's `keys[256]` global in input.cpp; pKeyboardState
//   resolves but its dereferenced value is always nullptr because FFNx
//   replaces FF8's get_keyboard_state via replace_function(). The zeroing
//   was dead code.
//
//   v0.15.9.11.1 / .11.2 installed a WH_KEYBOARD_LL hook to swallow
//   physical arrows + W/A/D/X. The hook fired correctly but broke the
//   chase auto-pilot on doopen2a -- caught at waypoint 5-6 of 26, ~2s
//   after engagement. Best theory: SendInput from the worker thread
//   becomes a synchronous round-trip through the main thread's message
//   pump (where LL hooks fire), adding enough latency per arrow inject
//   that the auto-pilot's per-tick path-finding falls behind the chase-
//   progress director's catch evaluator.
//
//   v0.15.9.11.3 solves both problems at once: synthetic buffer means
//   user keys are never read during chase Auto (no LL hook needed) AND
//   the auto-pilot's "presses" don't require SendInput latency (we write
//   them straight to memory; the hook reads our buffer on demand).
//
// FUTURE: the same mechanism can be activated for any scripted-sequence
// accessibility feature (cutscene playback, walk-and-talk, timed events,
// etc.). The gate is just Activate() / Deactivate() and a buffer write
// API; chase Auto is the first user.

#pragma once

#include <windows.h>
#include <cstdint>

// Forward-declared so the header doesn't need to include dinput.h.
struct IDirectInputDevice8A;

namespace ChaseKeyboard {

// ============================================================================
// Lifecycle
// ============================================================================

// Called from the dinput8.cpp proxy whenever IDirectInput8::CreateDevice
// returns successfully. The caller passes the GUID requested (so we can
// identify the keyboard device) plus the returned device pointer. If the
// GUID is GUID_SysKeyboard, we vtable-hook IDirectInputDevice8::GetDeviceState
// on it via MinHook. Subsequent CreateDevice calls for other devices
// (gamepad, mouse, etc.) are ignored.
void OnDeviceCreated(const GUID& rguid, IDirectInputDevice8A* device);

// Called from mod shutdown. Disables the GetDeviceState detour so any
// final keyboard polls during teardown go straight to DirectInput.
void Shutdown();

// ============================================================================
// Activate / Deactivate
// ============================================================================

// Called by chase_auto_pilot at engagement time when mode == MODE_AUTO.
// Once active, our GetDeviceState detour returns the synthetic buffer
// instead of the real DirectInput device state. Calling Activate while
// already active is a no-op.
//
// Activate clears the synthetic buffer so we don't carry stale "pressed"
// state across activations.
void Activate();

// Called by chase_auto_pilot at disengage time, OR if chase mode changes
// away from MODE_AUTO mid-chase. Restores normal pass-through behavior;
// the synthetic buffer is no longer consulted. Idempotent.
void Deactivate();

// True iff the synthetic-buffer path is currently in effect. Used by
// InjectKey in field_nav_autodrive.inl to decide whether to also update
// the synthetic buffer when injecting a key event.
bool IsActive();

// ============================================================================
// Synthetic buffer write API
// ============================================================================

// Set or clear a key in the synthetic buffer. The dikCode is a DirectInput
// scancode (DIK_*); set bit 7 (0x80) signals "key down" to FF8. Helper
// callers (e.g. InjectKey in field_nav_autodrive.inl) translate the raw
// PS/2 scancode + extended-prefix flag into a DIK code before calling.
//
// These functions are safe to call from any thread without locks: each
// byte write is atomic on x86 and the buffer is read byte-by-byte by the
// hook proc. Transient inconsistencies (e.g. reading between Up clear and
// Down set) last at most one frame and self-correct on the next read.
//
// Calling SetKeyDown/SetKeyUp while !IsActive() is a no-op -- the buffer
// is only consulted by the hook when active, so writes outside that
// window are harmless but pointless.
void SetKeyDown(uint8_t dikCode);
void SetKeyUp(uint8_t dikCode);

// Clear all keys in the synthetic buffer. Called automatically by
// Activate(); also available to callers who want a clean slate (e.g. at
// chase-field transition without disengaging).
void ClearAllKeys();

// ============================================================================
// v0.18.3.215: Autotest key OVERLAY (independent of chase Auto)
// ============================================================================
//
// Unlike the chase synthetic buffer (which REPLACES the keyboard state and
// only while chase Auto is active), the overlay is OR-ed into every 256-byte
// GetDeviceState result the game sees, active or not. This is the delivery
// path for the automated-BAT command channel (autotest_cmd.inl): OS-level
// SendInput injection reaches GetAsyncKeyState readers (mod hotkeys) but NOT
// FF8's DirectInput buffer for direction keys, so the overlay writes the DIK
// bytes at the exact point FF8 reads them. Zero-cost when no overlay key is
// held (single counter check per read).
void SetOverlayKey(uint8_t dikCode, bool down);
void ClearOverlay();

// ============================================================================
// Convenience: PS/2 scancode + extended-flag -> DIK conversion
// ============================================================================
//
// FF8/SendInput uses raw PS/2 hardware scancodes (0x48 = Up arrow,
// 0x11 = W, etc.) with the KEYEVENTF_EXTENDEDKEY flag indicating the E0
// prefix. DirectInput's GetDeviceState returns a 256-byte buffer indexed
// by DIK_* constants, which are the raw scancodes with bit 7 set for
// extended keys (DIK_UP = 0xC8 = 0x48 | 0x80).
//
// Helper for the rare caller that has (scancode, extended) but wants to
// poke the synthetic buffer directly.
inline uint8_t ScancodeToDik(uint8_t scancode, bool extended)
{
    return extended ? (uint8_t)(scancode | 0x80) : scancode;
}

// Combined helper: translate + write.
inline void SetScancodeDown(uint8_t scancode, bool extended)
{
    SetKeyDown(ScancodeToDik(scancode, extended));
}
inline void SetScancodeUp(uint8_t scancode, bool extended)
{
    SetKeyUp(ScancodeToDik(scancode, extended));
}

}  // namespace ChaseKeyboard
