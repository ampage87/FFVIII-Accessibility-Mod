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
#include "countdown_timer.h"
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
// See dinput8.cpp.history for the full v0.15.9.11.3.1 rationale block.
// Short version: FF8 imports DirectInputCreateA from DINPUT.dll (not
// DirectInput8Create from dinput8.dll), so the keyboard device FF8 reads
// is on the v7 chain. We install a parallel hook chain on the v7 API and
// reinterpret_cast IDirectInputDeviceA* to IDirectInputDevice8A* when
// calling chase_keyboard::OnDeviceCreated (binary-compatible through
// GetDeviceState which is the only vtable slot we touch).

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

// ============================================================================
// v0.15.9.11.3.2: GetAsyncKeyState hook for chase Auto arrow suppression
// ============================================================================
//
// Hooks user32!GetAsyncKeyState. During ChaseKeyboard::IsActive(), returns
// 0 for arrow VK queries plus the FF8 PC action/menu keys near the arrow
// cluster (Ctrl/Enter/Space/Tab/Escape and Lctrl/Rctrl explicitly). All
// other VKs pass through. See v0.15.9.11.3.8 CHANGELOG entry for the full
// mask-list rationale. VK_SHIFT and VK_MENU explicitly NOT masked (mod's
// own Shift+F3/F4 and Alt-gate hotkeys depend on them).

typedef SHORT (WINAPI *GetAsyncKeyState_t)(int);
static GetAsyncKeyState_t s_origGetAsyncKeyState = nullptr;

static SHORT WINAPI HookedGetAsyncKeyState(int vKey)
{
    if (s_origGetAsyncKeyState == nullptr) {
        return 0;
    }
    if (ChaseKeyboard::IsActive()) {
        switch (vKey) {
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
            case VK_CONTROL:
            case VK_LCONTROL:
            case VK_RCONTROL:
            case VK_RETURN:
            case VK_SPACE:
            case VK_TAB:
            case VK_ESCAPE:
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
        Log::Mod("DllMain: MH_CreateHook(GetAsyncKeyState) FAILED (status=%d).",
                 (int)st);
        return;
    }
    st = MH_EnableHook(reinterpret_cast<void*>(pGetAsyncKeyState));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_EnableHook(GetAsyncKeyState) FAILED (status=%d).",
                 (int)st);
        MH_RemoveHook(reinterpret_cast<void*>(pGetAsyncKeyState));
        return;
    }
    Log::Mod("DllMain: GetAsyncKeyState hooked at 0x%08X (from user32.dll). "
             "During chase Auto, arrow + nearby action VK queries return 0; "
             "all other VKs pass through.",
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
        Log::Mod("DllMain: GetProcAddress(dinput.dll, \"DirectInputCreateA\") FAILED.");
        return;
    }
    MH_STATUS st = MH_CreateHook(reinterpret_cast<void*>(pDirectInputCreateA),
                                 reinterpret_cast<void*>(&HookedDirectInputCreateA),
                                 reinterpret_cast<void**>(&s_origDirectInputCreateA));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_CreateHook(DirectInputCreateA) FAILED (status=%d).",
                 (int)st);
        return;
    }
    st = MH_EnableHook(reinterpret_cast<void*>(pDirectInputCreateA));
    if (st != MH_OK) {
        Log::Mod("DllMain: MH_EnableHook(DirectInputCreateA) FAILED (status=%d).",
                 (int)st);
        MH_RemoveHook(reinterpret_cast<void*>(pDirectInputCreateA));
        return;
    }
    Log::Mod("DllMain: DirectInputCreateA hooked at 0x%08X (from dinput.dll).",
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

DWORD WINAPI AccessibilityThread(LPVOID lpParam)
{
    // Give the game a moment to initialize its memory structures.
    Sleep(500);
    
    Log::Mod("AccessibilityThread: Starting main loop (v%s).", FF8OPC_VERSION);
    
    if (!ScreenReader::Initialize(hOurModule)) {
        Log::Mod("AccessibilityThread: Screen reader init failed. Continuing with logging only.");
    }

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
    
    MH_STATUS mhStatus = MH_Initialize();
    Log::Mod("AccessibilityThread: MH_Initialize = %s", MH_StatusToString(mhStatus));
    
    // v0.17.8.15: FieldCharaOneParse::Initialize() removed -- the chara.one
    // cross-reference chain (v0.17.8.11-.14) was reverted. The catalog now
    // uses JSM behavior signals (jsmCategory + hasSetmodelInit) instead of
    // model-file classification to distinguish NPCs from interactions.
    
    // Initialize accessibility modules
    TitleScreen::Initialize();
    FmvSkip::Initialize();
    FmvAudioDesc::Initialize(hOurModule);
    GfAudioDesc::Initialize(hOurModule);
    ScanTTS::Initialize();
    FieldDialog::Initialize();
    FieldNavigation::Initialize();
    FieldAnnounce::Initialize();
    NameBypass::Initialize();
    GameAudio::Initialize();
    MenuTTS::Initialize();
    BattleTTS::Initialize();
    WorldMap::Initialize();
    ChaseDetector::Initialize();
    ChaseDiag::Initialize();
    ChaseAskOverlay::Initialize();
    ChaseAutoPilot::Initialize();
    ChaseKaniFreeze::Initialize();
    ChaseBattleFreeze::Initialize();
    DialogInject::Initialize();
    CountdownTimer::Initialize(); // v0.15.12.0: Mission countdown timer
                                  // accessibility. Reads field var 724
                                  // (0x01CFECCC, uint16) each frame. T
                                  // announces remaining, Shift+T toggles
                                  // experimental freeze. Polls its own
                                  // hotkeys in Update().
    
    mhStatus = MH_EnableHook(MH_ALL_HOOKS);
    Log::Mod("AccessibilityThread: MH_EnableHook(ALL) = %s", MH_StatusToString(mhStatus));
    
    bool wasTitleActive = false;

    while (s_running) {
        GameAudio::Update();
        FF8Addresses::TryResolveDeferredGameLoop();
        
        bool titleActive = FF8Addresses::IsTitleMenuActive();
        if (!titleActive && addressesValid) {
            uint16_t mode = FF8Addresses::GetCurrentMode();
            uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
            if (mode == 1 && fid == 0) {
                titleActive = true;
            }
        }
        
        if (titleActive && !wasTitleActive) {
            TitleScreen::Activate();
        } else if (!titleActive && wasTitleActive) {
            TitleScreen::Deactivate();
        }
        wasTitleActive = titleActive;
        
        TitleScreen::Update();
        FmvSkip::OnFrame();
        FmvAudioDesc::OnFrame();
        GfAudioDesc::OnFrame();
        
        FieldNavigation::Update();
        FieldAnnounce::Update();
        FieldDialog::PollWindows();
        NameBypass::Update();
        MenuTTS::Update();
        BattleTTS::Update();
        WorldMap::Update();

        ChaseDetector::Update();
        ChaseAskOverlay::Update();
        ChaseAutoPilot::Update();
        ChaseKaniFreeze::Update();
        ChaseDiag::Update();
        DialogInject::Update();

        // v0.15.12.0: Mission countdown timer accessibility. Reads field
        // var 724 (0x01CFECCC, uint16) each frame and fires scheduled TTS
        // at boundaries. Polls T (announce) / Shift+T (freeze) internally.
        CountdownTimer::Update();
        
        // --- Accessibility keyboard shortcuts (v0.14.45 layout) ---
        // `  = Repeat last dialog
        // V  = Announce mod version
        // F1 = Cycle SAPI voice  | F2 = Toggle audio ducking
        // F3/F4 = Speech rate -/+ | Shift+F3/F4 = Speech volume -/+
        // F5/F6 = SFX vol -/+ | F7/F8 = BGM vol -/+
        // F11 = On-demand screenshot
        // F12 reserved for per-session diagnostics (none active in this build)
        // Navigation (-/+/Backspace) handled inside FieldNavigation::Update()
        // T / Shift+T handled inside CountdownTimer::Update() (v0.15.12.0)
        {
            static bool s_graveWas = false;
            static bool s_f1was = false;
            static bool s_f2was = false;
            static bool s_f3was = false, s_f4was = false;
            static bool s_f5was = false, s_f6was = false;
            static bool s_f7was = false, s_f8was = false;
            static bool s_f11was = false;
            static bool s_vWas = false;

            bool grave = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0;
            bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
            bool f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
            bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
            bool f4 = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            bool f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
            bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
            bool vkey = (GetAsyncKeyState('V') & 0x8000) != 0;
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
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
            if (f7 && !s_f7was && !alt) GameAudio::VolumeDown();
            if (f8 && !s_f8was && !alt) GameAudio::VolumeUp();
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
            s_vWas = vkey;
        }
        
        Sleep(16);
    }
    
    // Cleanup
    BattleTTS::Shutdown();
    WorldMap::Shutdown();
    CountdownTimer::Shutdown();     // v0.15.12.0: countdown timer cleanup
    DialogInject::Shutdown();
    ChaseBattleFreeze::Shutdown();
    ChaseAutoPilot::Shutdown();
    ChaseKeyboard::Shutdown();
    ChaseKaniFreeze::Shutdown();
    ChaseAskOverlay::Shutdown();
    ChaseDiag::Shutdown();
    ChaseDetector::Shutdown();
    GameAudio::Shutdown();
    NameBypass::Shutdown();
    FieldAnnounce::Shutdown();
    FieldNavigation::Shutdown();
    FieldDialog::Shutdown();
    FmvAudioDesc::Shutdown();
    GfAudioDesc::Shutdown();
    FmvSkip::Shutdown();
    // v0.17.8.15: FieldCharaOneParse::Shutdown() removed with the rest of
    // the chara.one chain (see Initialize comment above).
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
        
        Log::Init("ff8_accessibility.log");
        NavLog::Init();
        NavLog::SessionStart();
        Log::Write("========================================");
        Log::Write("FF8 Original PC Accessibility Mod");
        Log::Write("Version: %s", FF8OPC_VERSION);
        Log::Write("Build:   " __DATE__ " " __TIME__);
        Log::Write("========================================");
        Log::Write("DllMain: DLL_PROCESS_ATTACH");
        
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

        {
            MH_STATUS mhInit = MH_Initialize();
            Log::Mod("DllMain: MH_Initialize for DirectInputCreateA hook = %d",
                     (int)mhInit);
            InstallDirectInputCreateAHook();
            InstallGetAsyncKeyStateHook();
        }

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
