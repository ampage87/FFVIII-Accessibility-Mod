// dinput8.cpp - DLL proxy entry point for FF8 Accessibility Mod
// 
// Loads alongside FFNx as a companion DLL. Forwards DirectInput calls
// to the real system dinput8.dll while running accessibility features
// in a background thread.
//
// v03.00: FMV audio descriptions (WebVTT) and FMV skip (Backspace).
//         Ported from Remastered mod. Uses MinHook for kernel32 hooks.
// v02.00: First production build. Title screen TTS with direct memory
//         read of cursor position at pMenuStateA + 0x1F6.

#include <dinput.h>

#include "chase_diag.h"
#include "chase_detector.h"
#include "chase_ask_overlay.h"
#include "chase_auto_pilot.h"
#include "chase_battle_freeze.h"
#include "chase_keyboard.h"
#include "chase_kani_freeze.h"
#include "dialog_inject.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"
#include "minhook/include/MinHook.h"
#include "name_bypass.h"
#include "menu_tts.h"
#include "battle_tts.h"
#include "field_announce.h"
#include "field_archive.h"
#include "field_dialog.h"
#include "field_navigation.h"
#include "fmv_audio_desc.h"
#include "fmv_skip.h"
#include "gf_audio_desc.h"
#include "scan_tts.h"
#include "game_audio.h"
#include "world_map.h"

// Forward declarations for TitleScreen (no title_screen.h exists; defined in title_screen.cpp).
namespace TitleScreen {
    void Initialize();
    void Shutdown();
    void Activate();
    void Deactivate();
    void Update();
}


// ============================================================================
// DirectInput8 Proxy
// ============================================================================

typedef HRESULT(WINAPI* DirectInput8Create_t)(
    HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static DirectInput8Create_t pDirectInput8Create = nullptr;
static HMODULE hOriginalDll = nullptr;
static HMODULE hOurModule = nullptr;  // Our DLL's HMODULE, for locating Audio Descriptions folder

// ============================================================================
// v0.15.9.11.3: IDirectInput8A::CreateDevice vtable hook
// ============================================================================
//
// To install chase_keyboard's GetDeviceState detour on the keyboard device,
// we need a pointer to that device. FF8/FFNx obtains it via
// IDirectInput8::CreateDevice(GUID_SysKeyboard, ...). We hook CreateDevice
// on the IDirectInput8 instance returned by our DirectInput8Create proxy,
// then forward the device pointer to chase_keyboard::OnDeviceCreated when
// the keyboard is requested.
//
// Vtable layout for IDirectInput8A (from dinput.h):
//   [0] QueryInterface  [1] AddRef        [2] Release
//   [3] CreateDevice    [4] EnumDevices   [5] GetDeviceStatus
//   [6] RunControlPanel [7] Initialize    [8] FindDevice
//   [9] EnumDevicesBySemantics [10] ConfigureDevices
//
// CreateDevice is index 3. Standard COM vtable patching: read the slot,
// MH_CreateHook it, MH_EnableHook. We hook the IDirectInput8A vtable
// directly (not per-instance), so the hook persists for the life of the
// process and covers any subsequent CreateDevice calls regardless of
// which IDirectInput8A pointer makes them.

typedef HRESULT (__stdcall *CreateDevice_t)(IDirectInput8A*,
                                            REFGUID,
                                            LPDIRECTINPUTDEVICE8A*,
                                            LPUNKNOWN);
static CreateDevice_t s_origCreateDevice = nullptr;
static bool s_createDeviceHookInstalled = false;

static HRESULT __stdcall HookedCreateDevice(IDirectInput8A* di,
                                            REFGUID rguid,
                                            LPDIRECTINPUTDEVICE8A* lplpDevice,
                                            LPUNKNOWN pUnkOuter)
{
    HRESULT hr = s_origCreateDevice(di, rguid, lplpDevice, pUnkOuter);
    if (SUCCEEDED(hr) && lplpDevice != nullptr && *lplpDevice != nullptr) {
        // Hand off to chase_keyboard. It checks the GUID itself and only
        // installs the GetDeviceState detour for GUID_SysKeyboard.
        ChaseKeyboard::OnDeviceCreated(rguid, *lplpDevice);
    }
    return hr;
}

static void InstallCreateDeviceHook(IDirectInput8A* di)
{
    if (s_createDeviceHookInstalled || di == nullptr) return;

    void** vtable = *reinterpret_cast<void***>(di);
    void* targetCreateDevice = vtable[3];

    MH_STATUS st = MH_CreateHook(targetCreateDevice,
                                 reinterpret_cast<void*>(&HookedCreateDevice),
                                 reinterpret_cast<void**>(&s_origCreateDevice));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_CreateHook(IDirectInput8A::CreateDevice) FAILED "
                 "(status=%d) -- chase_keyboard cannot capture the keyboard "
                 "device pointer; chase Auto keyboard suppression DISABLED "
                 "(graceful degradation, chase still works without it).",
                 (int)st);
        return;
    }
    st = MH_EnableHook(targetCreateDevice);
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_EnableHook(IDirectInput8A::CreateDevice) FAILED "
                 "(status=%d) -- chase Auto keyboard suppression DISABLED.",
                 (int)st);
        MH_RemoveHook(targetCreateDevice);
        return;
    }
    s_createDeviceHookInstalled = true;
    Log::Mod("DllMain: IDirectInput8A::CreateDevice hooked at 0x%08X (vtable[3] "
             "on IDirectInput8A=0x%08X). chase_keyboard will receive device "
             "creation callbacks; GetDeviceState detour will install when "
             "GUID_SysKeyboard device is created.",
             (uint32_t)(uintptr_t)targetCreateDevice,
             (uint32_t)(uintptr_t)di);
}

// ============================================================================
// v0.15.9.11.3.1: IDirectInputA (DirectInput 7) CreateDevice hook chain
// ============================================================================
//
// v0.15.9.11.3 BAT (2026-05-13) showed the IDirectInput8A path captures no
// keyboard device -- FFNx logs at startup confirmed CreateDevice was hooked
// but no GUID_SysKeyboard callback ever arrived, and on chase Auto engage the
// graceful-degradation path fired ("ACTIVATED but hook NOT installed"). Aaron's
// physical arrows then disrupted the auto-pilot, proving keys still reached
// the engine.
//
// Root cause: FF8 (FF8_EN.exe) imports DirectInputCreateA from DINPUT.dll, NOT
// DirectInput8Create from dinput8.dll. FF8 is a 2013 Steam port of a 1999
// game using the original DirectInput 7 API. FFNx's input.cpp source confirms:
// `IDirectInputDeviceA* keyboard_device = *common_externals.keyboard_device`
// -- old-API interface type. FFNx's GetGameKeyState polls that device every
// frame; FFNx replaces FF8's get_keyboard_state with its own version, but the
// underlying device is FF8's IDirectInputDeviceA created via DirectInputCreateA.
//
// Our IDirectInput8A::CreateDevice hook only sees FFNx's own DirectInput 8
// devices (whatever FFNx creates for itself -- probably gamepad or overlay UI
// polling). The keyboard FF8 reads is on the v7 chain.
//
// Fix: install a parallel chain of hooks for the v7 API. Same logic:
// DirectInputCreateA returns IDirectInputA*; vtable[3] is CreateDevice;
// CreateDevice returns IDirectInputDeviceA*; vtable[9] on the device is
// GetDeviceState. The COM vtable layouts of IDirectInputDevice and
// IDirectInputDevice8 are compatible up through GetDeviceState (the v8
// interface ADDS methods at later vtable slots), so chase_keyboard's existing
// vtable[9] hook works for both kinds of device pointers without changes.
// We just reinterpret_cast the IDirectInputDeviceA* to IDirectInputDevice8A*
// when calling OnDeviceCreated -- a no-op at the binary level since both are
// just pointers to COM objects whose vtable[9] points to GetDeviceState.

typedef HRESULT (WINAPI *DirectInputCreateA_t)(HINSTANCE,
                                               DWORD,
                                               LPDIRECTINPUTA*,
                                               LPUNKNOWN);
static DirectInputCreateA_t s_origDirectInputCreateA = nullptr;

typedef HRESULT (__stdcall *CreateDeviceA_t)(IDirectInputA*,
                                             REFGUID,
                                             LPDIRECTINPUTDEVICEA*,
                                             LPUNKNOWN);
static CreateDeviceA_t s_origCreateDeviceA = nullptr;
static bool s_createDeviceAHookInstalled = false;

static HRESULT __stdcall HookedCreateDeviceA(IDirectInputA* di,
                                             REFGUID rguid,
                                             LPDIRECTINPUTDEVICEA* lplpDevice,
                                             LPUNKNOWN punkOuter)
{
    HRESULT hr = s_origCreateDeviceA(di, rguid, lplpDevice, punkOuter);
    if (SUCCEEDED(hr) && lplpDevice != nullptr && *lplpDevice != nullptr) {
        // Cast: chase_keyboard takes IDirectInputDevice8A* but only calls
        // vtable[9] (GetDeviceState), which exists at the same slot in both
        // IDirectInputDeviceA and IDirectInputDevice8A. The reinterpret_cast
        // is safe for our specific use; we are NOT calling any v8-only methods.
        ChaseKeyboard::OnDeviceCreated(
            rguid,
            reinterpret_cast<IDirectInputDevice8A*>(*lplpDevice));
    }
    return hr;
}

static void InstallCreateDeviceAHook(IDirectInputA* di)
{
    if (s_createDeviceAHookInstalled || di == nullptr) return;

    void** vtable = *reinterpret_cast<void***>(di);
    void* targetCreateDevice = vtable[3];

    MH_STATUS st = MH_CreateHook(targetCreateDevice,
                                 reinterpret_cast<void*>(&HookedCreateDeviceA),
                                 reinterpret_cast<void**>(&s_origCreateDeviceA));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_CreateHook(IDirectInputA::CreateDevice) FAILED "
                 "(status=%d) -- DirectInput 7 keyboard device cannot be "
                 "captured; chase Auto keyboard suppression DISABLED "
                 "(graceful degradation).",
                 (int)st);
        return;
    }
    st = MH_EnableHook(targetCreateDevice);
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_EnableHook(IDirectInputA::CreateDevice) FAILED "
                 "(status=%d) -- chase Auto keyboard suppression DISABLED.",
                 (int)st);
        MH_RemoveHook(targetCreateDevice);
        return;
    }
    s_createDeviceAHookInstalled = true;
    Log::Mod("DllMain: IDirectInputA::CreateDevice hooked at 0x%08X (vtable[3] "
             "on IDirectInputA=0x%08X). chase_keyboard will receive v7 device "
             "creation callbacks; GetDeviceState detour will install when "
             "GUID_SysKeyboard device is created via the v7 path.",
             (uint32_t)(uintptr_t)targetCreateDevice,
             (uint32_t)(uintptr_t)di);
}

static HRESULT WINAPI HookedDirectInputCreateA(HINSTANCE hinst,
                                               DWORD dwVersion,
                                               LPDIRECTINPUTA* lplpDirectInput,
                                               LPUNKNOWN punkOuter)
{
    HRESULT hr = s_origDirectInputCreateA(hinst, dwVersion, lplpDirectInput, punkOuter);
    if (SUCCEEDED(hr) && lplpDirectInput != nullptr && *lplpDirectInput != nullptr) {
        InstallCreateDeviceAHook(*lplpDirectInput);
    }
    return hr;
}

// Install the DirectInputCreateA function-level hook. Called from DllMain
// DLL_PROCESS_ATTACH so the hook is in place before FF8 ever calls the
// function. FF8's static imports are resolved by the Windows loader before
// any DllMain runs, but FF8 doesn't actually CALL DirectInputCreateA until
// later in its init path (post-WinMain entry), so installing the hook in
// DllMain (which runs after FF8.exe's static load but before WinMain) puts
// us in place to catch FF8's first call.
// ============================================================================
// v0.15.9.11.3.2: GetAsyncKeyState hook for chase Auto arrow suppression
// ============================================================================
//
// v0.15.9.11.3.1 BAT (2026-05-13 19:39-19:40) showed the DirectInput 7
// keyboard hook IS installed correctly (startup logs confirm all three
// hooks: DirectInputCreateA, IDirectInputA::CreateDevice, and
// IDirectInputDevice::GetDeviceState). On chase Auto engage, the field log
// shows the GOOD activation message ("synthetic keyboard buffer now
// substituted for GetDeviceState reads. Physical key presses will NOT reach
// the engine"), CALIB succeeded (party moved 228 units on lX, 247 units on
// lY), and the auto-pilot drove through early fields with Aaron pressing
// keys -- suppression worked.
//
// BUT on doopen2a (the first MODE_TARGET / path-finding chase field),
// Aaron's arrow presses started reaching the engine and disrupted the
// auto-pilot, triggering the chase-progress director (entityPtr=0x0188CA04)
// to fire BATTLE at waypoint 6/28 of 28 -- the exact same failure pattern
// as v0.15.9.11.1 / .11.2 (which used WH_KEYBOARD_LL). Test 1 with no key
// presses completed cleanly through doopen2a; test 2 with key presses
// failed at doopen2a -- the failure correlates with Aaron's key presses,
// not with the field itself.
//
// Root cause hypothesis: FF8 has TWO keyboard read paths:
//   1. DirectInput 7 GetDeviceState (via FFNx's GetGameKeyState which
//      replace_function-replaces FF8's get_keyboard_state). This path is
//      now correctly suppressed by chase_keyboard's synthetic buffer hook.
//   2. GetAsyncKeyState from USER32.dll. FF8_EN.exe imports this directly
//      (FF8_EN_imports.txt line 227). GetAsyncKeyState reads OS-level
//      keyboard state, which is updated by the Windows kernel when any
//      keyboard event occurs (physical key press OR SendInput call). This
//      path completely bypasses DirectInput and any function-pointer
//      replacement done by FFNx.
//
// On doopen2a specifically, the chase-progress director's JSM script
// appears to poll GetAsyncKeyState for arrow keys to decide when to fire
// the catch BATTLE. Earlier chase fields (MODE_DIRECTION, MODE_STAGED,
// MODE_BRIDGE_DANCE) use simpler progression logic that doesn't poll
// keyboard via this path. doopen2a uses MODE_TARGET (path-finding) with
// a chase-progress director that's keyboard-sensitive.
//
// Fix: hook GetAsyncKeyState from USER32.dll. During ChaseKeyboard::
// IsActive(), return 0 for ARROW key queries (VK_UP / VK_DOWN / VK_LEFT /
// VK_RIGHT). For all other VKs, pass through to the real OS implementation
// so the mod's F-key handlers (F1-F12 voice cycling, F5/F6 SFX volume,
// F7/F8 BGM volume, F11 screenshot, F12 diagnostic), V/G/T/L/R info
// readout keys, /, =, -, \ navigation toggles, and Backspace GPS toggle
// continue to work normally during chase Auto.
//
// Why return 0 instead of routing through the synthetic buffer: the
// auto-pilot's own arrow presses are NOT consumed via GetAsyncKeyState by
// any path that matters (test 1 with no keys pressed completed the chase
// successfully, so the auto-pilot's keys reach FF8 via DirectInput only).
// Returning 0 for ALL arrow queries during chase Auto -- including the
// auto-pilot's own injected keys -- is safe and simple. If a future
// regression shows FF8 also needs to see the auto-pilot's arrows via
// GetAsyncKeyState, we can switch to a VK->DIK mapped read from
// ChaseKeyboard::s_keyBuf at that point.
//
// The mod's own arrow handler in field_nav_handlekeys.inl is already
// gated on `s_driveActive && !s_chaseDriveActive` (v0.15.9.2), so it
// doesn't query arrow VKs during chase Auto -- no mod-side regression.

typedef SHORT (WINAPI *GetAsyncKeyState_t)(int);
static GetAsyncKeyState_t s_origGetAsyncKeyState = nullptr;

static SHORT WINAPI HookedGetAsyncKeyState(int vKey)
{
    // Defensive: if the hook is somehow called before s_origGetAsyncKeyState
    // is populated (impossible in normal flow but possible during teardown),
    // return 0. Better than dereferencing a null function pointer.
    if (s_origGetAsyncKeyState == nullptr) {
        return 0;
    }

    // During chase Auto, suppress arrow keys at the OS level so FF8's
    // GetAsyncKeyState-polling chase logic (e.g., the chase-progress
    // director on doopen2a) cannot see Aaron's physical key presses.
    // The auto-pilot's input still reaches FF8 via the DirectInput
    // synthetic-buffer path, which is independent of GetAsyncKeyState.
    if (ChaseKeyboard::IsActive()) {
        switch (vKey) {
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
                return 0;
            default:
                break;
        }
    }

    return s_origGetAsyncKeyState(vKey);
}

static void InstallGetAsyncKeyStateHook()
{
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32 == nullptr) {
        // user32.dll is part of every Win32 process; if this fails, the
        // process is in trouble already. Log and continue without the hook.
        Log::Mod("DllMain: GetModuleHandleA(\"user32.dll\") FAILED -- "
                 "GetAsyncKeyState chase Auto suppression unavailable.");
        return;
    }
    auto pGetAsyncKeyState = reinterpret_cast<GetAsyncKeyState_t>(
        GetProcAddress(hUser32, "GetAsyncKeyState"));
    if (pGetAsyncKeyState == nullptr) {
        Log::Mod("DllMain: GetProcAddress(user32, \"GetAsyncKeyState\") FAILED "
                 "-- GetAsyncKeyState chase Auto suppression unavailable.");
        return;
    }
    MH_STATUS st = MH_CreateHook(
        reinterpret_cast<void*>(pGetAsyncKeyState),
        reinterpret_cast<void*>(&HookedGetAsyncKeyState),
        reinterpret_cast<void**>(&s_origGetAsyncKeyState));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_CreateHook(GetAsyncKeyState) FAILED (status=%d) "
                 "-- chase Auto arrow suppression unavailable.",
                 (int)st);
        return;
    }
    st = MH_EnableHook(reinterpret_cast<void*>(pGetAsyncKeyState));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_EnableHook(GetAsyncKeyState) FAILED (status=%d) "
                 "-- chase Auto arrow suppression unavailable.",
                 (int)st);
        MH_RemoveHook(reinterpret_cast<void*>(pGetAsyncKeyState));
        return;
    }
    Log::Mod("DllMain: GetAsyncKeyState hooked at 0x%08X (from user32.dll). "
             "During chase Auto, arrow VK queries return 0; all other VKs "
             "pass through. Closes the non-DirectInput keyboard leak path "
             "that affected doopen2a in v0.15.9.11.3.1 BAT.",
             (uint32_t)(uintptr_t)pGetAsyncKeyState);
}

static void InstallDirectInputCreateAHook()
{
    HMODULE hDinput = LoadLibraryA("dinput.dll");
    if (hDinput == nullptr) {
        Log::Mod("DllMain: LoadLibraryA(\"dinput.dll\") FAILED -- DirectInput 7 "
                 "chain unavailable; chase Auto keyboard suppression DISABLED.");
        return;
    }
    auto pDirectInputCreateA = reinterpret_cast<DirectInputCreateA_t>(
        GetProcAddress(hDinput, "DirectInputCreateA"));
    if (pDirectInputCreateA == nullptr) {
        Log::Mod("DllMain: GetProcAddress(dinput.dll, \"DirectInputCreateA\") FAILED "
                 "-- DirectInput 7 chain unavailable; chase Auto keyboard suppression DISABLED.");
        return;
    }
    MH_STATUS st = MH_CreateHook(reinterpret_cast<void*>(pDirectInputCreateA),
                                 reinterpret_cast<void*>(&HookedDirectInputCreateA),
                                 reinterpret_cast<void**>(&s_origDirectInputCreateA));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_CreateHook(DirectInputCreateA) FAILED (status=%d) "
                 "-- chase Auto keyboard suppression DISABLED.",
                 (int)st);
        return;
    }
    st = MH_EnableHook(reinterpret_cast<void*>(pDirectInputCreateA));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_EnableHook(DirectInputCreateA) FAILED (status=%d) "
                 "-- chase Auto keyboard suppression DISABLED.",
                 (int)st);
        MH_RemoveHook(reinterpret_cast<void*>(pDirectInputCreateA));
        return;
    }
    Log::Mod("DllMain: DirectInputCreateA hooked at 0x%08X (from dinput.dll). "
             "FF8's keyboard creation via the DirectInput 7 path will now "
             "route through chase_keyboard's OnDeviceCreated.",
             (uint32_t)(uintptr_t)pDirectInputCreateA);
}

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter)
{
    if (pDirectInput8Create == nullptr)
        return E_FAIL;
    HRESULT hr = pDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);

    // v0.15.9.11.3: Hook CreateDevice on the returned IDirectInput8A so we
    // can capture the keyboard device pointer when FF8/FFNx creates it.
    // Only attempted once; subsequent DirectInput8Create calls (if any)
    // skip via s_createDeviceHookInstalled. The hook patches the vtable,
    // which is shared across all IDirectInput8A instances in this module,
    // so one install covers the lifetime of the process.
    //
    // v0.15.9.11.3 build fix: __declspec(dllexport) is INTENTIONALLY OMITTED
    // here. dinput.h (now included to get IDirectInput8A type) already
    // declares DirectInput8Create without dllexport; adding dllexport on
    // our implementation creates a C2375 linkage mismatch. Export is
    // handled instead by src/dinput8.def, which lists DirectInput8Create
    // explicitly. Behavior is identical (function is still exported); the
    // .def file is the standard mechanism for DLL-proxy projects.
    if (SUCCEEDED(hr) && ppvOut != nullptr && *ppvOut != nullptr &&
        IsEqualIID(riidltf, IID_IDirectInput8A))
    {
        InstallCreateDeviceHook(reinterpret_cast<IDirectInput8A*>(*ppvOut));
    }
    return hr;
}

// ============================================================================
// Accessibility Mod Core
// ============================================================================

static volatile bool s_running = false;
static HANDLE s_thread = nullptr;

// The main update loop runs in a background thread.
// It reads game state from memory and drives accessibility modules.
DWORD WINAPI AccessibilityThread(LPVOID lpParam)
{
    // Give the game a moment to initialize its memory structures.
    // FFNx needs to run ff8_find_externals() before the game's own
    // data addresses are populated.
    Sleep(500);
    
    Log::Mod("AccessibilityThread: Starting main loop (v%s).", FF8OPC_VERSION);
    
    // Initialize screen reader (NVDA direct + SAPI fallback)
    if (!ScreenReader::Initialize(hOurModule)) {
        Log::Mod("AccessibilityThread: Screen reader init failed. Continuing with logging only.");
    }

    // v0.13.51: Default speech rate is now loaded from ff8_accessibility.ini
    // inside ScreenReader::Initialize. The prior unconditional SetRate(3) call
    // here was overwriting the persisted value on every launch — removed.

    // Resolve game addresses from the executable
    bool addressesValid = false;
    if (!FF8Addresses::Resolve()) {
        Log::Mod("AccessibilityThread: WARNING - Address resolution failed!");
    } else {
        addressesValid = (FF8Addresses::pGameMode != nullptr);
        Log::Mod("AccessibilityThread: Address resolution succeeded.");
        Log::Mod("AccessibilityThread: pGameMode at 0x%08X, pTitleCursorPos at 0x%08X",
                   (uint32_t)(uintptr_t)FF8Addresses::pGameMode,
                   (uint32_t)(uintptr_t)FF8Addresses::pTitleCursorPos);
    }
    
    // Initialize MinHook (needed for FMV skip kernel32 hooks)
    MH_STATUS mhStatus = MH_Initialize();
    Log::Mod("AccessibilityThread: MH_Initialize = %s", MH_StatusToString(mhStatus));
    
    // Initialize accessibility modules
    TitleScreen::Initialize();
    FmvSkip::Initialize();       // Creates kernel32 hooks (CreateFile/CloseHandle/ReadFile)
    FmvAudioDesc::Initialize(hOurModule);  // Loads VTT files from Audio Descriptions folder
    GfAudioDesc::Initialize(hOurModule);   // v0.14.44: GF summon audio descriptions
    ScanTTS::Initialize();                  // v0.14.50: Scan spell TTS first slice
    FieldDialog::Initialize();   // v04.00: Hooks opcode dispatch table for dialog text capture
    FieldNavigation::Initialize(); // v05.00: Field navigation assistance
    FieldAnnounce::Initialize(); // v0.15.2.14: auto-announce field display name on field load
    NameBypass::Initialize();    // v04.26: Auto-bypass character/GF naming screens
    GameAudio::Initialize();      // v0.09.22: Centralized game audio control
    MenuTTS::Initialize();       // v07.00: In-game menu TTS diagnostic
    BattleTTS::Initialize();     // v0.10.01: Battle sequence TTS
    WorldMap::Initialize();       // v0.11.03: World map navigation
    ChaseDetector::Initialize();  // v0.15.1: Chase state authority (must be
                                  //          before ChaseDiag/Overlay/KaniFreeze
                                  //          since they query it)
    ChaseDiag::Initialize();      // v0.15.0: Dollet/X-ATM092 chase diagnostic
    ChaseAskOverlay::Initialize();// v0.15.1: Chase entry ASK overlay
    ChaseAutoPilot::Initialize(); // v0.15.9: Chase auto-drive (MODE_AUTO).
                                  //   Per-field config: domt4_1 run-west,
                                  //   domt5_1 walk-south. Engages only when
                                  //   chase mode is AUTO and player is on a
                                  //   configured chase field. Other chase
                                  //   fields and the bridge (doopen2a) are
                                  //   left to the player in v0.15.9; bridge
                                  //   state machine ships in v0.15.9.1.
    ChaseKaniFreeze::Initialize();// v0.15.2.14: kani+battleyarou static pin +
                                  //   DYNAMIC chase-agent pin (resolves the
                                  //   actual entity calling BATTLE in each
                                  //   chase field via RegisterChaseAgent;
                                  //   pins it post-battle to keep the robot
                                  //   on the ground). Tightened deactivation:
                                  //   raw fieldId check fires before the 2s
                                  //   name-debounce, fixing the doopen2a ->
                                  //   dotown_3 handoff crash that recurred
                                  //   in v0.15.2.10 and v0.15.2.13 BATs.
    ChaseBattleFreeze::Initialize();// v0.15.2.14: BATTLE NO-OP safety net +
                                  //   chase-agent identifier. On the first
                                  //   PASS in each chase field, hands the
                                  //   caller's entityPtr to ChaseKaniFreeze
                                  //   so the pin can target the correct
                                  //   entity. Subsequent BATTLE calls in
                                  //   the same field NO-OP'd as fallback
                                  //   (freeze# stays low when the pin is
                                  //   healthy; high freeze# = pin missing
                                  //   the agent).
    DialogInject::Initialize();   // v0.15.4: Phase 1 engine-dialog injection
                                  //   (mod-driven opcode_mes via synthesized
                                  //   script_context). F12 fires a one-shot
                                  //   test; multi-layered verification (log,
                                  //   SAPI, per-frame slot poll). See
                                  //   dialog_inject.h for design notes.
    
    // Enable all MinHook hooks
    mhStatus = MH_EnableHook(MH_ALL_HOOKS);
    Log::Mod("AccessibilityThread: MH_EnableHook(ALL) = %s", MH_StatusToString(mhStatus));
    
    
    // Track previous state for edge detection
    bool wasTitleActive = false;

    while (s_running) {
        // Game audio: deferred hook install + periodic volume re-application
        GameAudio::Update();

        // Deferred game loop resolution (needed for title screen detection)
        FF8Addresses::TryResolveDeferredGameLoop();
        
        // --- Detect current game state ---
        bool titleActive = FF8Addresses::IsTitleMenuActive();
        
        // Also check field-based detection as fallback:
        // mode==1 && field_id==0 means title screen in field mode
        if (!titleActive && addressesValid) {
            uint16_t mode = FF8Addresses::GetCurrentMode();
            uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
            if (mode == 1 && fid == 0) {
                titleActive = true;
            }
        }
        
        // --- Module dispatch ---
        
        // Title screen
        if (titleActive && !wasTitleActive) {
            TitleScreen::Activate();
        } else if (!titleActive && wasTitleActive) {
            TitleScreen::Deactivate();
        }
        wasTitleActive = titleActive;
        
        TitleScreen::Update();
        
        // FMV modules (active in all game states)
        FmvSkip::OnFrame();
        FmvAudioDesc::OnFrame();
        GfAudioDesc::OnFrame();  // v0.14.44: poll for GF summon end + fire cues
        
        FieldNavigation::Update();

        // v0.15.2.14: Auto-announce field display name on field load.
        FieldAnnounce::Update();

        // Field dialog polling fallback (v04.13)
        // Catches dialogs that bypass hooked opcodes
        FieldDialog::PollWindows();

        // Naming screen bypass (v04.26)
        NameBypass::Update();

        // In-game menu TTS (v07.00)
        MenuTTS::Update();

        // Battle sequence TTS (v0.10.01)
        BattleTTS::Update();

        // World map navigation TTS (v0.11.03)
        WorldMap::Update();

        // v0.15.1: Chase scene state authority + ASK overlay polling.
        // ChaseDetector polls field/game-mode and resolves kani slot;
        // ChaseAskOverlay polls keyboard for the chase ASK choice.
        // v0.15.2.3: ChaseKaniFreeze diagnoses/freezes kani's post-battle
        // wakeup transition so the robot stays incapacitated until field
        // exit — replaces the v0.15.1 ChaseBattleFreeze opcode_battle hook.
        ChaseDetector::Update();
        ChaseAskOverlay::Update();
        ChaseAutoPilot::Update();   // v0.15.9: drives party on configured chase fields
        ChaseKaniFreeze::Update();

        // Chase scene diagnostic (v0.15.0) -- F12 toggle, no-op when disabled.
        // v0.15.4: F12 binding moved to DialogInject; ChaseDiag still polls
        // when previously enabled but cannot be toggled at runtime now.
        ChaseDiag::Update();

        // Dialog injection (v0.15.4) -- per-frame slot poll after a Phase 1
        // F12 fire; cheap no-op otherwise.
        DialogInject::Update();
        
        // --- Accessibility keyboard shortcuts (v0.14.45 layout) ---
        // `  = Repeat last dialog
        // V  = Announce mod version
        // F1 = Cycle SAPI voice
        // F2 = Toggle audio ducking
        // F3 / F4              = Speech rate down / up
        // Shift+F3 / Shift+F4  = Speech volume down / up
        // F5 / F6              = SFX volume down / up
        // F7 / F8              = BGM volume down / up
        // F11                  = On-demand screenshot
        // F12                  = Toggle chase scene diagnostic (v0.15.0)
        // Navigation (-/+/Backspace) handled inside FieldNavigation::Update()
        {
            static bool s_graveWas = false;
            static bool s_f1was = false;
            static bool s_f2was = false;
            static bool s_f3was = false, s_f4was = false;
            static bool s_f5was = false, s_f6was = false;
            static bool s_f7was = false, s_f8was = false;
            static bool s_f11was = false;
            static bool s_f12was = false;
            static bool s_vWas = false;

            bool grave = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0; // ` key
            bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
            bool f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
            bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
            bool f4 = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            bool f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
            bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
            bool f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
            bool vkey = (GetAsyncKeyState('V') & 0x8000) != 0;
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            // v0.14.105: Suppress accessibility hotkeys when Alt is held.
            // Without this, Alt+F4 (close window) fires our IncreaseRate()
            // on the way out — Aaron's speech rate was creeping up by 1 per
            // session because of this. Apply the gate uniformly to every
            // function-key handler (F1-F8, F11) so no Alt+combo accidentally
            // triggers an accessibility action; if a real Alt+key shortcut
            // is ever wanted, it will need an explicit "Alt is held" branch.
            bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

            if (grave && !s_graveWas) FieldDialog::RepeatLastDialog();
            if (f1 && !s_f1was && !alt)       ScreenReader::CycleVoice();
            if (f2 && !s_f2was && !alt)       GameAudio::ToggleDucking();
            if (f3 && !s_f3was && !alt) {
                if (shift) ScreenReader::DecreaseVolume();
                else       ScreenReader::DecreaseRate();
            }
            if (f4 && !s_f4was && !alt) {
                if (shift) ScreenReader::IncreaseVolume();
                else       ScreenReader::IncreaseRate();
            }
            if (f5 && !s_f5was && !alt) GameAudio::SfxVolumeDown();
            if (f6 && !s_f6was && !alt) GameAudio::SfxVolumeUp();
            if (f7 && !s_f7was && !alt) GameAudio::VolumeDown();   // BGM
            if (f8 && !s_f8was && !alt) GameAudio::VolumeUp();     // BGM
            // v0.14.75: F11 = on-demand screenshot capture (was F12 in
            // v0.14.74.4-diag). Promoted to a permanent feature after the
            // diagnostic build proved its value (capturing the FF8 Config
            // menu to find the Scan: Once/Always toggle, which closed the
            // compacted-view chapter without writing fallback code). Moved
            // off F12 so F12 stays free as the per-session diagnostic key
            // per the F12 rule in userMemories. The previous F11 owners
            // (FieldNavigation VISDIAG dump, MenuTTS Shift+F11
            // StartMemoryMonitor, MenuTTS Ctrl+F11 DumpMenuScreenData)
            // were research diagnostics for closed investigations — all
            // removed in v0.14.75. The MenuTTS plain F11 user feature
            // (AnnounceMenuSummary) was relocated to the M key. Builds
            // an absolute path under the project's diagnostic screenshots
            // dir, calls BattleTTS::RequestScreenshotAsync (sets the GL
            // capture flag, returns immediately; the next SwapBuffers
            // writes <path>.bmp + <path>.png), logs the path, speaks
            // 'Screenshot captured.' so Aaron knows the keypress
            // registered. Works in any game state because the SwapBuffers
            // hook installed by BattleTTS::Initialize is global.
            if (f11 && !s_f11was && !alt) {
                SYSTEMTIME wt;
                GetLocalTime(&wt);
                char path[512];
                snprintf(path, sizeof(path),
                         "%s\\f11_%02d%02d%02d_%03d",
                         BattleTTS::GetScreenshotDir(),
                         wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds);
                BattleTTS::RequestScreenshotAsync(path);
                Log::Mod("[F11-SCREENSHOT] Capture requested: '%s.png'", path);
                ScreenReader::Speak(L"Screenshot captured.", true);
            }
            // F12 = Dialog inject Phase 1 test fire (v0.15.4).
            // Shift+F12 = Phase 2a ASK test (v0.15.5).
            // Replaces v0.15.0's chase-diag toggle. Per the F12 rule, only
            // one diagnostic active per physical key state; the chase
            // chapter is complete (v0.15.3 shipped end-to-end) so the
            // chase-diag binding is retired. ChaseDiag module remains in
            // source but is no longer hot-keyed; if needed for future
            // chase work it can be re-bound in a session-specific build.
            if (f12 && !s_f12was && !alt) {
                if (shift) DialogInject::Phase2_TestAsk();
                else       DialogInject::Phase1_TestMes();
            }
            if (vkey && !s_vWas) {
                wchar_t verMsg[128];
                wsprintfW(verMsg, L"Version %hs", FF8OPC_VERSION);
                ScreenReader::Speak(verMsg, true);
            }

            s_graveWas = grave;
            s_f1was = f1;
            s_f2was = f2;
            s_f3was = f3; s_f4was = f4;
            s_f5was = f5; s_f6was = f6;
            s_f7was = f7; s_f8was = f8;
            s_f11was = f11;
            s_f12was = f12;
            s_vWas = vkey;
        }

        // v0.15.4: F12 = DialogInject::Phase1_TestMes (engine-dialog
        // injection diagnostic). v0.15.5: Shift+F12 = Phase2_TestAsk
        // (experimental ASK call). See F12 handler above. Replaces
        // v0.15.0's ChaseDiag::Toggle binding; per the F12 rule only one
        // diagnostic active per physical key state, and the chase chapter
        // is complete.
        // ENT-MON code removed in v0.12.23.
        
        // --- Sleep to avoid burning CPU ---
        // 16ms ≈ 60 polls/sec, fast enough for menu navigation
        Sleep(16);
    }
    
    // Cleanup
    BattleTTS::Shutdown();       // v0.10.01: Battle TTS cleanup
    WorldMap::Shutdown();         // v0.11.03: World map cleanup
    DialogInject::Shutdown();     // v0.15.4: Dialog inject Phase 1 cleanup
    ChaseBattleFreeze::Shutdown();// v0.15.2.13: active opcode_battle freeze
    ChaseAutoPilot::Shutdown();   // v0.15.9: release any held auto-drive keys
    ChaseKeyboard::Shutdown();    // v0.15.9.11.3: remove GetDeviceState detour
    ChaseKaniFreeze::Shutdown();  // v0.15.2.3: kani-wakeup diagnostic cleanup
    ChaseAskOverlay::Shutdown();  // v0.15.1: close ASK if open + reset state
    ChaseDiag::Shutdown();        // v0.15.0: Chase diagnostic cleanup
    ChaseDetector::Shutdown();    // v0.15.1: Chase state cleanup
    GameAudio::Shutdown();       // v0.09.22: Remove BGM volume hook
    NameBypass::Shutdown();      // v04.26: Remove naming screen hook
    FieldAnnounce::Shutdown();   // v0.15.2.14: field-name auto-announce cleanup
    FieldNavigation::Shutdown(); // v05.00: Field navigation cleanup
    FieldDialog::Shutdown();     // v04.00: Restore opcode table entries
    FmvAudioDesc::Shutdown();
    GfAudioDesc::Shutdown();  // v0.14.44
    FmvSkip::Shutdown();
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    TitleScreen::Shutdown();
    ScreenReader::Shutdown();
    
    Log::Write("AccessibilityThread: Exited main loop.");
    return 0;
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        
        // Initialize logging first
        Log::Init("ff8_accessibility.log");
        NavLog::Init();
        NavLog::SessionStart();
        Log::Write("========================================");
        Log::Write("FF8 Original PC Accessibility Mod");
        Log::Write("Version: %s (%s)", FF8OPC_VERSION, FF8OPC_VERSION_DATE);
        Log::Write("Build:   " __DATE__ " " __TIME__);
        Log::Write("========================================");
        Log::Write("DllMain: DLL_PROCESS_ATTACH");
        
        // Load real dinput8.dll from system directory
        char systemPath[MAX_PATH];
        GetSystemDirectoryA(systemPath, MAX_PATH);
        strcat_s(systemPath, "\\dinput8.dll");
        
        hOurModule = hModule;
        hOriginalDll = LoadLibraryA(systemPath);
        if (hOriginalDll == nullptr) {
            Log::Write("DllMain: ERROR - Failed to load system dinput8.dll");
            return FALSE;
        }
        
        pDirectInput8Create = (DirectInput8Create_t)
            GetProcAddress(hOriginalDll, "DirectInput8Create");
        if (pDirectInput8Create == nullptr) {
            Log::Write("DllMain: ERROR - DirectInput8Create not found");
            FreeLibrary(hOriginalDll);
            return FALSE;
        }
        
        Log::Write("DllMain: System dinput8.dll loaded, proxy ready.");

        // v0.15.9.11.3.1: Install the DirectInput 7 (DirectInputCreateA) hook
        // chain BEFORE the accessibility thread starts. FF8 uses the DirectInput 7
        // API for its keyboard (proven by FF8_EN.exe import table showing
        // DirectInputCreateA from DINPUT.dll, no DirectInput8Create import).
        //
        // MinHook is normally initialized inside the AccessibilityThread, which
        // runs concurrently with FF8's WinMain. We initialize MinHook HERE in
        // DllMain instead so the hook is installed synchronously before any
        // user code runs. MH_Initialize is idempotent: the later call in
        // AccessibilityThread returns MH_ERROR_ALREADY_INITIALIZED, which is
        // logged as a non-fatal status and execution continues normally.
        //
        // FF8 has not yet called DirectInputCreateA when DllMain runs --
        // FF8.exe's static imports are resolved by the Windows loader, but
        // the actual call to DirectInputCreateA happens later during FF8's
        // init sequence, well after DllMain has finished. The hook lands in
        // time to capture FF8's first keyboard device creation.
        //
        // The IDirectInput8A path is set up separately when FFNx calls
        // DirectInput8Create through our proxy.
        {
            MH_STATUS mhInit = MH_Initialize();
            Log::Mod("DllMain: MH_Initialize for DirectInputCreateA hook = %d",
                     (int)mhInit);
            InstallDirectInputCreateAHook();
            // v0.15.9.11.3.2: GetAsyncKeyState hook -- closes the non-
            // DirectInput keyboard leak path that affected doopen2a in the
            // v0.15.9.11.3.1 BAT. See block comment above for full rationale.
            // Order doesn't matter relative to InstallDirectInputCreateAHook;
            // both hooks share MinHook's global state, both are independent.
            InstallGetAsyncKeyStateHook();
        }

        // Start accessibility thread
        s_running = true;
        s_thread = CreateThread(nullptr, 0, AccessibilityThread, nullptr, 0, nullptr);
        if (s_thread == nullptr) {
            Log::Write("DllMain: ERROR - Failed to create accessibility thread");
        } else {
            Log::Write("DllMain: Accessibility thread started.");
        }
        
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        Log::Write("DllMain: DLL_PROCESS_DETACH");
        
        // Signal thread to stop
        s_running = false;
        if (s_thread != nullptr) {
            WaitForSingleObject(s_thread, 3000);
            CloseHandle(s_thread);
        }
        
        if (hOriginalDll != nullptr) {
            FreeLibrary(hOriginalDll);
        }
        
        NavLog::Close();
        Log::Close();
        break;
    }
    }
    
    return TRUE;
}
