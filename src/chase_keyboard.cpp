// chase_keyboard.cpp -- Synthetic keyboard buffer + GetDeviceState detour.
// See chase_keyboard.h for design notes.

#include "chase_keyboard.h"
#include "chase_wndproc.h"  // v0.15.9.11.3.6: lazy WndProc subclass install on Activate
#include "ff8_accessibility.h"
#include "mod_forward_decls.h"
#include "minhook/include/MinHook.h"

#include <dinput.h>
#include <cstring>

namespace ChaseKeyboard {

// ============================================================================
// State
// ============================================================================

// Synthetic 256-byte DirectInput keyboard state buffer. Each byte is a DIK
// code slot; bit 7 (0x80) set means "key down". Zero-initialized so all
// keys are released by default.
static uint8_t s_keyBuf[256] = {0};

// True iff the GetDeviceState detour should return s_keyBuf instead of
// the real device state. Set by Activate(); cleared by Deactivate().
//
// Read from the game main thread (inside the hook proc) and written from
// the accessibility worker thread (where chase_auto_pilot runs). bool is
// atomically readable/writable on x86 -- no lock needed. Worst case race
// (Activate fires between two consecutive GetDeviceState calls from the
// game): one frame of normal pass-through followed by buffer-substitution,
// which the engine sees as a one-frame keyup-then-keydown -- harmless.
static volatile bool s_active = false;

// Keyboard device pointer captured from dinput8.cpp's CreateDevice proxy.
// Used to (a) verify which device the hook fires on (we only hook the
// keyboard, not the gamepad/mouse) and (b) skip re-hooking if a future
// CreateDevice call for SysKeyboard returns the same device.
static IDirectInputDevice8A* s_keyboardDevice = nullptr;

// MinHook target + trampoline for IDirectInputDevice8A::GetDeviceState
// (vtable index 9). Type signature: HRESULT(__stdcall*)(IDID8A*, DWORD, LPVOID).
typedef HRESULT (__stdcall *GetDeviceState_t)(IDirectInputDevice8A*, DWORD, LPVOID);
static GetDeviceState_t s_origGetDeviceState = nullptr;

// True once the GetDeviceState detour is installed and enabled.
static bool s_hookInstalled = false;

// ============================================================================
// Hook proc
// ============================================================================

// v0.15.9.11.3.5: The v0.15.9.11.3.3 leak-probe was removed from this file.
// It did its job: its BAT (Outcome #1, [LEAK-PROBE] lines tagged PHYSICAL KEY
// MASKED) proved this GetDeviceState detour IS the path FFNx uses for FF8
// field input and DOES mask the user's physical arrows -- FF8 receives the
// synthetic buffer, never the physical keys. Keeping the probe past that
// point was pure liability: it polled the real device an extra time per
// frame and wrote a [LEAK-PROBE] log line on every physical-key state change
// -- during key-mashing that is ~20 file-I/O writes/sec on the field thread
// (and zero hands-off). That asymmetric logging load was itself a candidate
// disruptor of chase Auto timing, so it had to go before the next clean BAT.

// The detour. When s_active is true and the caller asks for a 256-byte
// keyboard buffer, we copy s_keyBuf into the caller's buffer and return
// DI_OK -- the real DirectInput device is NOT polled (for what FF8 sees).
// Outside chase Auto (or for any size other than 256, which would be a
// non-keyboard buffer type), we forward to the original.
static HRESULT __stdcall HookedGetDeviceState(IDirectInputDevice8A* dev,
                                              DWORD cbData,
                                              LPVOID lpvData)
{
    if (s_active && cbData == 256 && lpvData != nullptr) {
        // Substitute: copy our synthetic buffer to the caller. The real
        // DirectInput device is NOT polled -- physical keyboard state is
        // intentionally not consulted for what FF8 sees during chase Auto.
        // (v0.15.9.11.3.3's leak-probe confirmed this detour is the path
        // FFNx uses for FF8 field input, so the substitution here is the
        // complete and sufficient delivery path.)
        std::memcpy(lpvData, s_keyBuf, 256);
        return DI_OK;
    }
    // Pass-through. The original was captured by MinHook during install.
    return s_origGetDeviceState(dev, cbData, lpvData);
}

// ============================================================================
// Device-creation callback (from dinput8.cpp proxy)
// ============================================================================

void OnDeviceCreated(const GUID& rguid, IDirectInputDevice8A* device)
{
    if (device == nullptr) return;

    // Only act on the keyboard device. SysMouse, gamepads, etc. are
    // ignored -- our hook only makes sense for the keyboard state read.
    if (!IsEqualGUID(rguid, GUID_SysKeyboard)) return;

    // Already hooked the keyboard? FF8/FFNx typically creates the device
    // once at startup, but be defensive in case of re-acquire or other
    // re-creation paths.
    if (s_hookInstalled && device == s_keyboardDevice) return;

    s_keyboardDevice = device;

    // Resolve GetDeviceState from the device's vtable. IDirectInputDevice8A
    // vtable layout (from dinput.h):
    //   [0] QueryInterface  [1] AddRef           [2] Release
    //   [3] GetCapabilities [4] EnumObjects      [5] GetProperty
    //   [6] SetProperty     [7] Acquire          [8] Unacquire
    //   [9] GetDeviceState  ...
    void** vtable = *reinterpret_cast<void***>(device);
    void* targetGetDeviceState = vtable[9];

    MH_STATUS st = MH_CreateHook(targetGetDeviceState,
                                 reinterpret_cast<void*>(&HookedGetDeviceState),
                                 reinterpret_cast<void**>(&s_origGetDeviceState));
    if (st != MH_OK) {
        Log::Mod("ChaseKeyboard: MH_CreateHook(GetDeviceState) FAILED "
                 "(status=%d) -- chase Auto keyboard substitution DISABLED. "
                 "Chase may still work but user keypresses will reach the "
                 "engine. This is the v0.15.9.11.3 fallback (graceful "
                 "degradation rather than abort).",
                 (int)st);
        s_keyboardDevice = nullptr;
        return;
    }

    st = MH_EnableHook(targetGetDeviceState);
    if (st != MH_OK) {
        Log::Mod("ChaseKeyboard: MH_EnableHook(GetDeviceState) FAILED "
                 "(status=%d) -- chase Auto keyboard substitution DISABLED.",
                 (int)st);
        MH_RemoveHook(targetGetDeviceState);
        s_keyboardDevice = nullptr;
        return;
    }

    s_hookInstalled = true;
    Log::Mod("ChaseKeyboard: IDirectInputDevice8A::GetDeviceState hooked at "
             "0x%08X (vtable[9] on keyboard device 0x%08X). Synthetic "
             "buffer path armed -- will activate on chase Auto engage.",
             (uint32_t)(uintptr_t)targetGetDeviceState,
             (uint32_t)(uintptr_t)device);
}

void Shutdown()
{
    s_active = false;
    if (s_hookInstalled && s_keyboardDevice != nullptr) {
        void** vtable = *reinterpret_cast<void***>(s_keyboardDevice);
        void* targetGetDeviceState = vtable[9];
        MH_DisableHook(targetGetDeviceState);
        MH_RemoveHook(targetGetDeviceState);
    }
    s_hookInstalled = false;
    s_keyboardDevice = nullptr;
    s_origGetDeviceState = nullptr;
}

// ============================================================================
// Activate / Deactivate
// ============================================================================

void Activate()
{
    if (s_active) return;
    // Clear the buffer so we don't carry stale state across activations.
    // The auto-pilot will set the keys it wants over the next few ticks.
    std::memset(s_keyBuf, 0, sizeof(s_keyBuf));

    // v0.15.9.11.3.6: Install the WndProc subclass lazily on first Activate.
    // EnsureInstalled is idempotent -- safe to call on every Activate; only
    // the first one (per window) actually subclasses. We do it here rather
    // than at startup because EnumWindows needs FF8/FFNx to have created its
    // main game window first, and the first chase Auto activation always
    // happens many seconds after launch (the player has to reach the chase
    // scene), so the window is guaranteed to exist by now. Cross-thread
    // safe: SetWindowLongPtrW is documented to work from any thread, and
    // we're typically called from the chase auto-pilot worker thread.
    ChaseWndProc::EnsureInstalled();

    s_active = true;
    if (s_hookInstalled) {
        Log::Field("ChaseKeyboard: ACTIVATED -- synthetic keyboard buffer "
                   "now substituted for GetDeviceState reads. Physical "
                   "key presses will NOT reach the engine until Deactivate.");
    } else {
        Log::Field("ChaseKeyboard: ACTIVATED but hook NOT installed -- "
                   "synthetic buffer set but GetDeviceState detour absent; "
                   "physical key presses will continue to reach the engine. "
                   "This is the v0.15.9.11.3 graceful-degradation path.");
    }
}

void Deactivate()
{
    if (!s_active) return;
    s_active = false;
    // Clear the buffer so a future Activate starts clean.
    std::memset(s_keyBuf, 0, sizeof(s_keyBuf));
    Log::Field("ChaseKeyboard: DEACTIVATED -- GetDeviceState reads now "
               "pass through to the real DirectInput device.");
}

bool IsActive()
{
    return s_active;
}

// ============================================================================
// Buffer writes
// ============================================================================

void SetKeyDown(uint8_t dikCode)
{
    // Writes are unconditional (no IsActive() gate) for cheapness and so
    // the chase auto-pilot can pre-write keys before the first GetDeviceState
    // poll if its engage path is timing-sensitive. Outside chase Auto the
    // buffer isn't consulted by the hook, so the write is harmless.
    s_keyBuf[dikCode] = 0x80;
}

void SetKeyUp(uint8_t dikCode)
{
    s_keyBuf[dikCode] = 0x00;
}

void ClearAllKeys()
{
    std::memset(s_keyBuf, 0, sizeof(s_keyBuf));
}

}  // namespace ChaseKeyboard
