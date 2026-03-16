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

#include "ff8_accessibility.h"
#include "minhook/include/MinHook.h"
#include "name_bypass.h"


// ============================================================================
// DirectInput8 Proxy
// ============================================================================

typedef HRESULT(WINAPI* DirectInput8Create_t)(
    HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static DirectInput8Create_t pDirectInput8Create = nullptr;
static HMODULE hOriginalDll = nullptr;
static HMODULE hOurModule = nullptr;  // Our DLL's HMODULE, for locating Audio Descriptions folder

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter)
{
    if (pDirectInput8Create == nullptr)
        return E_FAIL;
    return pDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

// ============================================================================
// Accessibility Mod Core
// ============================================================================

static volatile bool s_running = false;
static HANDLE s_thread = nullptr;

// ============================================================================
// Game audio volume control via FF8 dmusicperf_set_volume (FFNx hook)
//
// FFNx uses SoLoud for all game audio — NOT OpenAL. FFNx replaces the FF8
// function dmusicperf_set_volume_sub_46C6F0 (address 0x0046C6F0 in the EN
// Steam build) with its own set_music_volume(uint32_t volume) handler, which
// calls nxAudioEngine.setMusicMasterVolume(volume / 100.0f) via SoLoud.
//
// We call that address directly, passing volume as 0-100. FFNx reads the
// first argument as the volume value. cdecl calling convention; safe to call
// with one argument (caller cleans the stack regardless).
//
// If FFNx is not loaded, the original DirectMusic handler at 0x0046C6F0
// still runs and sets the DirectMusic performance volume — a graceful
// fallback for vanilla installs.
//
// F3 = volume down 10%, F4 = volume up 10%.
// ============================================================================

// Address of dmusicperf_set_volume_sub_46C6F0 in FF8_EN.exe (Steam EN build).
// FFNx replaces this function body at runtime with set_music_volume(vol 0-100).
#define FF8_SET_MUSIC_VOLUME_ADDR 0x0046C6F0

typedef void (__cdecl *FF8SetMusicVolume_t)(unsigned int volume);

static float s_gameVolume = 0.2f;

static void SetGameAudioVolume(float vol)
{
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    s_gameVolume = vol;

    // Convert 0.0-1.0 to 0-100 integer scale expected by set_music_volume.
    unsigned int intVol = (unsigned int)(vol * 100.0f + 0.5f);
    if (intVol > 100) intVol = 100;

    // Call FF8's volume function (patched by FFNx to route into SoLoud).
    FF8SetMusicVolume_t fn = (FF8SetMusicVolume_t)FF8_SET_MUSIC_VOLUME_ADDR;
    fn(intVol);

    Log::Write("GameVolume: set_music_volume(%u) called (%.0f%%).", intVol, vol * 100.0f);
}

static void GameVolumeDown()
{
    float newVol = s_gameVolume - 0.1f;
    if (newVol < 0.0f) newVol = 0.0f;
    SetGameAudioVolume(newVol);
    char msg[48];
    snprintf(msg, sizeof(msg), "Music volume %d percent", (int)(s_gameVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
    Log::Write("GameVolume: -> %.0f%%", s_gameVolume * 100.0f);
}

static void GameVolumeUp()
{
    float newVol = s_gameVolume + 0.1f;
    if (newVol > 1.0f) newVol = 1.0f;
    SetGameAudioVolume(newVol);
    char msg[48];
    snprintf(msg, sizeof(msg), "Music volume %d percent", (int)(s_gameVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
    Log::Write("GameVolume: -> %.0f%%", s_gameVolume * 100.0f);
}

// The main update loop runs in a background thread.
// It reads game state from memory and drives accessibility modules.
DWORD WINAPI AccessibilityThread(LPVOID lpParam)
{
    // Give the game a moment to initialize its memory structures.
    // FFNx needs to run ff8_find_externals() before the game's own
    // data addresses are populated.
    Sleep(500);
    
    Log::Write("AccessibilityThread: Starting main loop (v%s).", FF8OPC_VERSION);
    
    // Initialize screen reader (NVDA direct + SAPI fallback)
    if (!ScreenReader::Initialize(hOurModule)) {
        Log::Write("AccessibilityThread: Screen reader init failed. Continuing with logging only.");
    }

    // Apply default speech rate silently.
    ScreenReader::SetRate(3);
    Log::Write("AccessibilityThread: Default speech rate=3 applied.");

    // Resolve game addresses from the executable
    bool addressesValid = false;
    if (!FF8Addresses::Resolve()) {
        Log::Write("AccessibilityThread: WARNING - Address resolution failed!");
    } else {
        addressesValid = (FF8Addresses::pGameMode != nullptr);
        Log::Write("AccessibilityThread: Address resolution succeeded.");
        Log::Write("AccessibilityThread: pGameMode at 0x%08X, pTitleCursorPos at 0x%08X",
                   (uint32_t)(uintptr_t)FF8Addresses::pGameMode,
                   (uint32_t)(uintptr_t)FF8Addresses::pTitleCursorPos);
    }
    
    // Initialize MinHook (needed for FMV skip kernel32 hooks)
    MH_STATUS mhStatus = MH_Initialize();
    Log::Write("AccessibilityThread: MH_Initialize = %s", MH_StatusToString(mhStatus));
    
    // Initialize accessibility modules
    TitleScreen::Initialize();
    FmvSkip::Initialize();       // Creates kernel32 hooks (CreateFile/CloseHandle/ReadFile)
    FmvAudioDesc::Initialize(hOurModule);  // Loads VTT files from Audio Descriptions folder
    FieldDialog::Initialize();   // v04.00: Hooks opcode dispatch table for dialog text capture
    FieldNavigation::Initialize(); // v05.00: Field navigation assistance (scaffold)
    NameBypass::Initialize();    // v04.26: Auto-bypass character/GF naming screens
    
    // Enable all MinHook hooks
    mhStatus = MH_EnableHook(MH_ALL_HOOKS);
    Log::Write("AccessibilityThread: MH_EnableHook(ALL) = %s", MH_StatusToString(mhStatus));
    
    
    // Track previous state for edge detection
    bool wasTitleActive = false;

    // Apply default game volume on the first main loop tick, after SAPI is
    // fully stable and FF8's audio session has been registered with Windows.
    bool s_gameVolumeApplied = false;

    while (s_running) {
        // Apply default game volume once, on the first tick.
        if (!s_gameVolumeApplied) {
            SetGameAudioVolume(s_gameVolume);
            Log::Write("AccessibilityThread: Default game volume=80%% applied (first tick).");
            s_gameVolumeApplied = true;
        }

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
        
        // Field navigation update (v05.00) — runs before dialog poll so it
        // can check dialog-active state and suspend navigation if needed.
        FieldNavigation::Update();

        // Field dialog polling fallback (v04.13)
        // Catches dialogs that bypass hooked opcodes
        FieldDialog::PollWindows();

        // Naming screen bypass (v04.26)
        NameBypass::Update();
        
        // --- Accessibility keyboard shortcuts ---
        // `  = Repeat last dialog
        // F1 = Cycle SAPI voice
        // F3 = Game vol down,  F4 = Game vol up
        // F5 = Speech vol down, F6 = Speech vol up
        // F7 = Speech rate down, F8 = Speech rate up
        // Navigation (-/+/Backspace) handled inside FieldNavigation::Update()
        {
            static bool s_graveWas = false;
            static bool s_f1was = false;
            static bool s_f3was = false, s_f4was = false;
            static bool s_f5was = false, s_f6was = false;
            static bool s_f7was = false, s_f8was = false;
            bool grave = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0; // ` key
            bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
            bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
            bool f4 = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            bool f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
            bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;

            if (grave && !s_graveWas) FieldDialog::RepeatLastDialog();
            if (f1 && !s_f1was) ScreenReader::CycleVoice();
            if (f3 && !s_f3was) GameVolumeDown();
            if (f4 && !s_f4was) GameVolumeUp();
            if (f5 && !s_f5was) ScreenReader::DecreaseVolume();
            if (f6 && !s_f6was) ScreenReader::IncreaseVolume();
            if (f7 && !s_f7was) ScreenReader::DecreaseRate();
            if (f8 && !s_f8was) ScreenReader::IncreaseRate();

            s_graveWas = grave;
            s_f1was = f1;
            s_f3was = f3; s_f4was = f4;
            s_f5was = f5; s_f6was = f6;
            s_f7was = f7; s_f8was = f8;
        }
        
        // --- Sleep to avoid burning CPU ---
        // 16ms ≈ 60 polls/sec, fast enough for menu navigation
        Sleep(16);
    }
    
    // Cleanup
    NameBypass::Shutdown();      // v04.26: Remove naming screen hook
    FieldNavigation::Shutdown(); // v05.00: Field navigation cleanup
    FieldDialog::Shutdown();     // v04.00: Restore opcode table entries
    FmvAudioDesc::Shutdown();
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
