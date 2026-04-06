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
#include "menu_tts.h"
#include "battle_tts.h"
#include "field_archive.h"


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

    // Apply default speech rate silently.
    ScreenReader::SetRate(3);
    Log::Mod("AccessibilityThread: Default speech rate=3 applied.");

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
    FieldDialog::Initialize();   // v04.00: Hooks opcode dispatch table for dialog text capture
    FieldNavigation::Initialize(); // v05.00: Field navigation assistance
    NameBypass::Initialize();    // v04.26: Auto-bypass character/GF naming screens
    GameAudio::Initialize();      // v0.09.22: Centralized game audio control
    MenuTTS::Initialize();       // v07.00: In-game menu TTS diagnostic
    BattleTTS::Initialize();     // v0.10.01: Battle sequence TTS
    WorldMap::Initialize();       // v0.11.03: World map navigation
    
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
        
        FieldNavigation::Update();

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
            if (f3 && !s_f3was) GameAudio::VolumeDown();
            if (f4 && !s_f4was) GameAudio::VolumeUp();
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

        // ============================================================================
        // v0.12.17: F12 Entity Activation Monitor diagnostic
        // ============================================================================
        // Monitors the engine's entity pointer table (0x1D9D020) and position data
        // every frame to detect when entities like dic (Directory) get activated.
        // F12 toggles monitoring on/off. First press dumps full entity table state.
        // Subsequent frames log any changes in entity count, pointer validity, or
        // position values, along with player position at the moment of change.
        {
            static bool s_f12was = false;
            static bool s_entityMonitorActive = false;
            // Snapshot of entity table state from previous frame
            static const int ENT_TABLE_SIZE = 32;
            static uint32_t s_prevPtrs[ENT_TABLE_SIZE] = {};
            static int32_t  s_prevPosX[ENT_TABLE_SIZE] = {};
            static int32_t  s_prevPosY[ENT_TABLE_SIZE] = {};
            static uint16_t s_prevTri[ENT_TABLE_SIZE] = {};
            static uint16_t s_prevTalk[ENT_TABLE_SIZE] = {};
            static uint8_t  s_prevEntCount = 0;
            static uint8_t  s_prevBgCount = 0;

            bool f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
            bool f12Pressed = f12 && !s_f12was;
            s_f12was = f12;

            if (f12Pressed && addressesValid) {
                s_entityMonitorActive = !s_entityMonitorActive;
                if (s_entityMonitorActive) {
                    ScreenReader::Speak("Entity monitor on", true);
                    Log::Mod("[ENT-MON] === Entity Activation Monitor ON ===");

                    // Full dump of entity table on first activation
                    __try {
                        uint8_t entCount = *(uint8_t*)0x1D9D0E1;
                        Log::Mod("[ENT-MON] Active entity count (0x1D9D0E1): %u", (unsigned)entCount);
                        s_prevEntCount = entCount;

                        // Also read Others count if available
                        if (FF8Addresses::pFieldStateOtherCount) {
                            uint8_t othersCount = *FF8Addresses::pFieldStateOtherCount;
                            Log::Mod("[ENT-MON] Others count (pFieldStateOtherCount): %u", (unsigned)othersCount);
                        }

                        // Read player position
                        int playerIdx = FF8Addresses::GetPlayerEntityIndex();
                        int32_t playerX = 0, playerY = 0;
                        uint16_t playerTri = 0xFFFF;
                        if (playerIdx >= 0 && FF8Addresses::pFieldStateOthers) {
                            uint8_t* pEnt = FF8Addresses::pFieldStateOthers[playerIdx];
                            if (pEnt) {
                                playerX = *(int32_t*)(pEnt + 0x190) >> 12;
                                playerY = *(int32_t*)(pEnt + 0x194) >> 12;
                                playerTri = *(uint16_t*)(pEnt + 0x1FA);
                            }
                        }
                        Log::Mod("[ENT-MON] Player pos: (%d, %d) tri=%u", playerX, playerY, (unsigned)playerTri);

                        // Dump entity pointer table
                        for (int i = 0; i < ENT_TABLE_SIZE; i++) {
                            uint32_t ptr = *(uint32_t*)(0x1D9D020 + i * 4);
                            s_prevPtrs[i] = ptr;
                            if (ptr) {
                                uint8_t* ent = (uint8_t*)ptr;
                                int32_t px = *(int32_t*)(ent + 0x190) >> 12;
                                int32_t py = *(int32_t*)(ent + 0x194) >> 12;
                                uint16_t tri = *(uint16_t*)(ent + 0x1FA);
                                uint16_t talk = *(uint16_t*)(ent + 0x1F8);
                                uint16_t push = *(uint16_t*)(ent + 0x1F6);
                                uint8_t setpc = *(uint8_t*)(ent + 0x255);
                                int16_t modelId = *(int16_t*)(ent + 0x218);
                                s_prevPosX[i] = px;
                                s_prevPosY[i] = py;
                                s_prevTri[i] = tri;
                                s_prevTalk[i] = talk;
                                Log::Mod("[ENT-MON]   [%2d] ptr=0x%08X pos=(%d,%d) tri=%u talk=%u push=%u model=%d setpc=%u",
                                           i, ptr, px, py, (unsigned)tri, (unsigned)talk, (unsigned)push, (int)modelId, (unsigned)setpc);
                            } else {
                                s_prevPosX[i] = 0;
                                s_prevPosY[i] = 0;
                                s_prevTri[i] = 0;
                                s_prevTalk[i] = 0;
                                Log::Mod("[ENT-MON]   [%2d] NULL", i);
                            }
                        }

                        // Also dump Backgrounds table if available
                        if (FF8Addresses::pFieldStateBackgrounds && FF8Addresses::pFieldStateBackgroundCount) {
                            uint8_t bgCount = *FF8Addresses::pFieldStateBackgroundCount;
                            s_prevBgCount = bgCount;
                            Log::Mod("[ENT-MON] Backgrounds count: %u", (unsigned)bgCount);
                            // Dump first 20 background entity active flags
                            uint8_t* bgBase = *FF8Addresses::pFieldStateBackgrounds;
                            if (bgBase) {
                                for (int i = 0; i < 20 && i < bgCount; i++) {
                                    uint8_t* bg = bgBase + i * 0x18C; // stride from EXE disassembly (0x47B500)
                                    uint8_t active = *(uint8_t*)(bg + 0x188);
                                    uint8_t exec = *(uint8_t*)(bg + 0x18A);
                                    Log::Mod("[ENT-MON]   BG[%2d] active=%u exec=%u", i, (unsigned)active, (unsigned)exec);
                                }
                            }
                        }

                        Log::Mod("[ENT-MON] === Initial dump complete ===");

                        // v0.12.17: Dump JSM scripts for Background entities named dic/igyous
                        if (FF8Addresses::pCurrentFieldName) {
                            const char* fn = FF8Addresses::pCurrentFieldName;
                            char symN[128][32] = {};
                            int symC = 0;
                            FieldArchive::LoadSYMNames(fn, symN, 128, symC);
                            FieldArchive::JSMCounts jc = {};
                            FieldArchive::LoadJSMCounts(fn, jc);
                            for (int si = 0; si < symC; si++) {
                                if (strstr(symN[si], "dic") || strstr(symN[si], "igyous")) {
                                    int jsmIdx = si + jc.doors;  // SYM skips doors but not the rest
                                    // Actually: SYM ordering is Line, BG, Other (after skipping doors)
                                    // JSM entity ordering is Door[0..D-1], Line[D..], BG, Other
                                    // So SYM[i] = JSM entity[i + countDoors]
                                    Log::Mod("[ENT-MON] Dumping script for SYM[%d]='%s' -> JSM entity %d",
                                               si, symN[si], jsmIdx);
                                    FieldArchive::DumpEntityScript(fn, jsmIdx);
                                }
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        Log::Mod("[ENT-MON] EXCEPTION during initial dump");
                    }
                } else {
                    ScreenReader::Speak("Entity monitor off", true);
                    Log::Mod("[ENT-MON] === Entity Activation Monitor OFF ===");
                }
            }

            // Per-frame monitoring when active
            if (s_entityMonitorActive && addressesValid) {
                __try {
                    bool anyChange = false;

                    // Check entity count change
                    uint8_t entCount = *(uint8_t*)0x1D9D0E1;
                    if (entCount != s_prevEntCount) {
                        Log::Write("[ENT-MON] !!! Entity count changed: %u -> %u",
                                   (unsigned)s_prevEntCount, (unsigned)entCount);
                        s_prevEntCount = entCount;
                        anyChange = true;
                    }

                    // Check each entity table slot
                    for (int i = 0; i < ENT_TABLE_SIZE; i++) {
                        uint32_t ptr = *(uint32_t*)(0x1D9D020 + i * 4);

                        // Pointer appeared or disappeared
                        if (ptr != s_prevPtrs[i]) {
                            Log::Write("[ENT-MON] !!! Slot %d pointer changed: 0x%08X -> 0x%08X",
                                       i, s_prevPtrs[i], ptr);
                            s_prevPtrs[i] = ptr;
                            anyChange = true;
                        }

                        // If pointer is valid, check data changes
                        if (ptr) {
                            uint8_t* ent = (uint8_t*)ptr;
                            int32_t px = *(int32_t*)(ent + 0x190) >> 12;
                            int32_t py = *(int32_t*)(ent + 0x194) >> 12;
                            uint16_t tri = *(uint16_t*)(ent + 0x1FA);
                            uint16_t talk = *(uint16_t*)(ent + 0x1F8);

                            if (px != s_prevPosX[i] || py != s_prevPosY[i] ||
                                tri != s_prevTri[i] || talk != s_prevTalk[i]) {
                                Log::Write("[ENT-MON] !!! Slot %d data changed: pos(%d,%d)->(%d,%d) tri=%u->%u talk=%u->%u",
                                           i, s_prevPosX[i], s_prevPosY[i], px, py,
                                           (unsigned)s_prevTri[i], (unsigned)tri,
                                           (unsigned)s_prevTalk[i], (unsigned)talk);
                                s_prevPosX[i] = px;
                                s_prevPosY[i] = py;
                                s_prevTri[i] = tri;
                                s_prevTalk[i] = talk;
                                anyChange = true;
                            }
                        }
                    }

                    // If anything changed, log player position for correlation
                    if (anyChange) {
                        int playerIdx = FF8Addresses::GetPlayerEntityIndex();
                        if (playerIdx >= 0 && FF8Addresses::pFieldStateOthers) {
                            uint8_t* pEnt = FF8Addresses::pFieldStateOthers[playerIdx];
                            if (pEnt) {
                                int32_t px = *(int32_t*)(pEnt + 0x190) >> 12;
                                int32_t py = *(int32_t*)(pEnt + 0x194) >> 12;
                                uint16_t tri = *(uint16_t*)(pEnt + 0x1FA);
                                Log::Write("[ENT-MON] Player at change: pos=(%d,%d) tri=%u", px, py, (unsigned)tri);
                            }
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    // Silently continue — might be between field transitions
                }
            }

            // Auto-disable on field change
            if (s_entityMonitorActive) {
                static uint16_t s_monFieldId = 0xFFFF;
                if (FF8Addresses::pCurrentFieldId) {
                    uint16_t fid = *FF8Addresses::pCurrentFieldId;
                    if (s_monFieldId == 0xFFFF) {
                        s_monFieldId = fid;
                    } else if (fid != s_monFieldId) {
                        Log::Write("[ENT-MON] Field changed %u -> %u, auto-disabling", (unsigned)s_monFieldId, (unsigned)fid);
                        s_entityMonitorActive = false;
                        s_monFieldId = 0xFFFF;
                    }
                }
            }
        }
        
        // --- Sleep to avoid burning CPU ---
        // 16ms ≈ 60 polls/sec, fast enough for menu navigation
        Sleep(16);
    }
    
    // Cleanup
    BattleTTS::Shutdown();       // v0.10.01: Battle TTS cleanup
    WorldMap::Shutdown();         // v0.11.03: World map cleanup
    GameAudio::Shutdown();       // v0.09.22: Remove BGM volume hook
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
