// screen_reader.cpp - Direct NVDA + SAPI screen reader output
//
// No Tolk dependency. Two backends:
//
//   1. SAPI (primary): Initialized as a dedicated game speech channel.
//      Immune to NVDA's keypress cancellation. Always used for audio output.
//      Supports voice cycling (F6), rate up (F8), rate down (F7).
//
//   2. NVDA (braille + interrupt): nvdaControllerClient.dll is embedded
//      as a Win32 resource. DLL stays loaded even if NVDA isn't running
//      at startup. Used for braille output and interrupt announcements.
//
// v04.25 Speech routing:
//   - SAPI is primary for game dialog (immune to NVDA keypress cancellation).
//   - NVDA receives braille output when available.
//   - Voice cycling (F6), rate up (F8), rate down (F7) supported via SAPI.
//
// The public API (Speak, Output, Silence, etc.) is unchanged.

#include "ff8_accessibility.h"
#include "resources.h"
#include <objbase.h>
#include <sapi.h>
#include <mmsystem.h>   // WAVE_MAPPER for SpMMAudioOut

namespace ScreenReader {

// ============================================================================
// Backend selection
// ============================================================================

enum class Backend { NONE, NVDA_SAPI, SAPI_ONLY };
static Backend s_backend = Backend::NONE;
static bool s_initialized = false;

// ============================================================================
// NVDA controller client — dynamic loading
// ============================================================================

typedef unsigned long (__stdcall *nvdaTestIfRunning_t)(void);
typedef unsigned long (__stdcall *nvdaSpeakText_t)(const wchar_t*);
typedef unsigned long (__stdcall *nvdaCancelSpeech_t)(void);
typedef unsigned long (__stdcall *nvdaBrailleMessage_t)(const wchar_t*);

static HMODULE s_nvdaDll = nullptr;
static nvdaTestIfRunning_t  fn_testIfRunning = nullptr;
static nvdaSpeakText_t      fn_speakText = nullptr;
static nvdaCancelSpeech_t   fn_cancelSpeech = nullptr;
static nvdaBrailleMessage_t fn_brailleMessage = nullptr;

static char s_extractedDllPath[MAX_PATH] = {};

// ============================================================================
// SAPI COM — dedicated game speech voice
// ============================================================================

static ISpVoice* s_pVoice = nullptr;
static ISpVoice* s_pVoice2 = nullptr;   // v0.10.32: Second SAPI voice for battle event/damage channel

// v0.10.43: Separate SpMMAudioOut objects to bypass SAPI serialization.
// SAPI serializes all voices sharing an ISpAudio output. By giving each voice
// its own SpMMAudioOut COM object (each opens an independent waveOut handle),
// Windows mixes them at the WASAPI layer — enabling true simultaneous speech.
static ISpMMSysAudio* s_pAudio1 = nullptr;  // audio output for voice 1 (menu/commands)
static ISpMMSysAudio* s_pAudio2 = nullptr;  // audio output for voice 2 (damage/events)

// v04.25: SAPI voice enumeration and rate control
static IEnumSpObjectTokens* s_pVoiceEnum = nullptr;
static ULONG s_voiceCount = 0;
static ULONG s_currentVoiceIndex = 0;
static long s_currentRate = 3;  // SAPI rate: -10 to 10, default 3

// v05.13: SAPI volume control (0-100, default 100)
static USHORT s_currentVolume = 100;

// v04.25: Last spoken text for repeat feature
static std::wstring s_lastSpokenText;

// ============================================================================
// Helper: ensure COM is initialized on the calling thread
// ============================================================================

static void EnsureCOMInitialized()
{
    static thread_local bool comInitialized = false;
    if (!comInitialized) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
            comInitialized = true;
            Log::Write("ScreenReader: COM initialized on thread %lu (HRESULT: 0x%08X)",
                       GetCurrentThreadId(), hr);
        } else {
            Log::Write("ScreenReader: WARNING - COM init failed (HRESULT: 0x%08X)", hr);
        }
    }
}

// ============================================================================
// Helper: extract a resource to a temp file
// ============================================================================

static std::string ExtractResourceToTemp(HMODULE hModule, int resourceId, const char* filename)
{
    HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!hRes) {
        Log::Write("ScreenReader: FindResource failed for ID %d (error %lu)",
                   resourceId, GetLastError());
        return "";
    }

    HGLOBAL hData = LoadResource(hModule, hRes);
    if (!hData) {
        Log::Write("ScreenReader: LoadResource failed for ID %d", resourceId);
        return "";
    }

    DWORD size = SizeofResource(hModule, hRes);
    const void* pData = LockResource(hData);
    if (!pData || size == 0) {
        Log::Write("ScreenReader: LockResource failed or size=0 for ID %d", resourceId);
        return "";
    }

    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    std::string subDir = std::string(tempDir) + "ff8a11y";
    CreateDirectoryA(subDir.c_str(), NULL);

    std::string fullPath = subDir + "\\" + filename;

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(fullPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        if (fileInfo.nFileSizeLow == size && fileInfo.nFileSizeHigh == 0) {
            Log::Write("ScreenReader: %s already extracted (%lu bytes), reusing.", filename, size);
            return fullPath;
        }
    }

    HANDLE hFile = CreateFileA(fullPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log::Write("ScreenReader: Failed to create temp file %s (error %lu)",
                   fullPath.c_str(), GetLastError());
        return "";
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, pData, size, &written, NULL);
    CloseHandle(hFile);

    if (!ok || written != size) {
        Log::Write("ScreenReader: Write failed for %s (wrote %lu of %lu)",
                   fullPath.c_str(), written, size);
        DeleteFileA(fullPath.c_str());
        return "";
    }

    Log::Write("ScreenReader: Extracted %s (%lu bytes) to %s", filename, size, fullPath.c_str());
    return fullPath;
}

// ============================================================================
// NVDA backend initialization
// ============================================================================

static bool InitNVDA(HMODULE hModule)
{
    std::string dllPath = ExtractResourceToTemp(hModule, IDR_NVDA_CLIENT_DLL,
                                                 "nvdaControllerClient.dll");
    if (dllPath.empty()) {
        Log::Write("ScreenReader: NVDA client DLL extraction failed.");
        return false;
    }

    strncpy_s(s_extractedDllPath, dllPath.c_str(), MAX_PATH - 1);

    s_nvdaDll = LoadLibraryA(dllPath.c_str());
    if (!s_nvdaDll) {
        Log::Write("ScreenReader: LoadLibrary failed for %s (error %lu)",
                   dllPath.c_str(), GetLastError());
        return false;
    }
    Log::Write("ScreenReader: nvdaControllerClient.dll loaded from %s", dllPath.c_str());

    fn_testIfRunning  = (nvdaTestIfRunning_t)GetProcAddress(s_nvdaDll, "nvdaController_testIfRunning");
    fn_speakText      = (nvdaSpeakText_t)GetProcAddress(s_nvdaDll, "nvdaController_speakText");
    fn_cancelSpeech   = (nvdaCancelSpeech_t)GetProcAddress(s_nvdaDll, "nvdaController_cancelSpeech");
    fn_brailleMessage = (nvdaBrailleMessage_t)GetProcAddress(s_nvdaDll, "nvdaController_brailleMessage");

    if (!fn_testIfRunning || !fn_speakText || !fn_cancelSpeech) {
        Log::Write("ScreenReader: Failed to resolve NVDA controller functions.");
        FreeLibrary(s_nvdaDll);
        s_nvdaDll = nullptr;
        return false;
    }
    Log::Write("ScreenReader: NVDA controller functions resolved.");

    unsigned long result = fn_testIfRunning();
    if (result != 0) {
        Log::Write("ScreenReader: NVDA not running at startup (testIfRunning returned %lu).", result);
        Log::Write("ScreenReader: NVDA DLL kept loaded — will re-check when speaking.");
        // v04.24: Keep DLL loaded, return true so we get NVDA_SAPI backend.
        // We'll check testIfRunning at speak time and fall back to SAPI if needed.
        return true;
    }

    Log::Write("ScreenReader: NVDA is running and responsive.");
    return true;
}

// ============================================================================
// SAPI backend initialization
// ============================================================================

static bool InitSAPI()
{
    EnsureCOMInitialized();

    HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL,
                                  IID_ISpVoice, (void**)&s_pVoice);
    if (FAILED(hr) || !s_pVoice) {
        Log::Write("ScreenReader: SAPI CoCreateInstance failed (HRESULT: 0x%08X)", hr);
        return false;
    }

    // Apply default rate immediately.
    s_pVoice->SetRate(s_currentRate);
    Log::Write("ScreenReader: SAPI voice 1 initialized (default rate=%ld).", s_currentRate);

    // v0.10.32: Create second SAPI voice for battle event channel
    hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL,
                          IID_ISpVoice, (void**)&s_pVoice2);
    if (SUCCEEDED(hr) && s_pVoice2) {
        s_pVoice2->SetRate(s_currentRate);
        s_pVoice2->SetVolume(s_currentVolume);
        Log::Write("ScreenReader: SAPI voice 2 (event channel) initialized.");
    } else {
        Log::Write("ScreenReader: WARNING - SAPI voice 2 creation failed, events will use voice 1.");
        s_pVoice2 = nullptr;
    }

    // v0.10.43: Create separate SpMMAudioOut objects to bypass SAPI serialization.
    // Without this, two ISpVoice instances sharing the default audio output are
    // serialized by SAPI's ISpAudio queue — only one can play at a time.
    // By giving each voice its own SpMMAudioOut (each opens a separate waveOut
    // handle), Windows mixes them independently at the WASAPI layer.
    hr = CoCreateInstance(CLSID_SpMMAudioOut, NULL, CLSCTX_ALL,
                          IID_ISpMMSysAudio, (void**)&s_pAudio1);
    if (SUCCEEDED(hr) && s_pAudio1) {
        s_pAudio1->SetDeviceId(WAVE_MAPPER);
        hr = s_pVoice->SetOutput(s_pAudio1, TRUE);
        Log::Write("ScreenReader: Voice 1 -> separate SpMMAudioOut (hr=0x%08X)", hr);
    } else {
        Log::Write("ScreenReader: SpMMAudioOut 1 creation failed (hr=0x%08X), using default output.", hr);
        s_pAudio1 = nullptr;
    }

    if (s_pVoice2) {
        hr = CoCreateInstance(CLSID_SpMMAudioOut, NULL, CLSCTX_ALL,
                              IID_ISpMMSysAudio, (void**)&s_pAudio2);
        if (SUCCEEDED(hr) && s_pAudio2) {
            s_pAudio2->SetDeviceId(WAVE_MAPPER);
            hr = s_pVoice2->SetOutput(s_pAudio2, TRUE);
            Log::Write("ScreenReader: Voice 2 -> separate SpMMAudioOut (hr=0x%08X)", hr);
        } else {
            Log::Write("ScreenReader: SpMMAudioOut 2 creation failed (hr=0x%08X), using default output.", hr);
            s_pAudio2 = nullptr;
        }
    }

    return true;
}

// ============================================================================
// Public interface
// ============================================================================

bool Initialize(HMODULE hModule)
{
    Log::Write("ScreenReader: Initializing (NVDA + SAPI dual-output)...");

    EnsureCOMInitialized();

    bool nvdaOk = InitNVDA(hModule);
    bool sapiOk = InitSAPI();

    if (nvdaOk && sapiOk) {
        s_backend = Backend::NVDA_SAPI;
        Log::Write("ScreenReader: Backend = NVDA+SAPI (NVDA for braille, SAPI for game audio)");
    } else if (sapiOk) {
        s_backend = Backend::SAPI_ONLY;
        Log::Write("ScreenReader: Backend = SAPI only (NVDA not available)");
    } else {
        s_backend = Backend::NONE;
        Log::Write("ScreenReader: WARNING - No speech backend available.");
    }

    s_initialized = (s_backend != Backend::NONE);

    if (s_initialized) {
        wchar_t announcement[128];
        wsprintfW(announcement, L"FF8 Accessibility Mod version %hs loaded.", FF8OPC_VERSION);
        Output(announcement, true);
    }

    return s_initialized;
}

void Shutdown()
{
    // Release voices first (they hold references to audio outputs)
    if (s_pVoice2) {
        s_pVoice2->Release();
        s_pVoice2 = nullptr;
    }

    if (s_pVoice) {
        s_pVoice->Release();
        s_pVoice = nullptr;
    }

    // v0.10.43: Release separate audio output objects
    if (s_pAudio2) {
        s_pAudio2->Release();
        s_pAudio2 = nullptr;
    }
    if (s_pAudio1) {
        s_pAudio1->Release();
        s_pAudio1 = nullptr;
    }

    if (s_nvdaDll) {
        FreeLibrary(s_nvdaDll);
        s_nvdaDll = nullptr;
    }
    fn_testIfRunning = nullptr;
    fn_speakText = nullptr;
    fn_cancelSpeech = nullptr;
    fn_brailleMessage = nullptr;

    CoUninitialize();

    s_backend = Backend::NONE;
    s_initialized = false;
    Log::Write("ScreenReader: Shutdown complete.");
}

bool IsAvailable()
{
    return s_initialized && s_backend != Backend::NONE;
}

// ============================================================================
// Speak - Core speech output
//
// v04.25: SAPI is primary (immune to NVDA keypress cancellation).
//
// When interrupt=true:
//   - Cancel SAPI queue + Cancel NVDA speech
//   - Speak via SAPI (audio) + NVDA speakText + braille
//
// When interrupt=false (game dialog):
//   - Queue on SAPI only (audio survives keypresses)
//   - Send to NVDA braille only (no NVDA speech)
// ============================================================================

bool Speak(const wchar_t* text, bool interrupt)
{
    if (!s_initialized || !text) return false;

    // v04.25: Track last spoken text for repeat feature
    if (wcslen(text) > 0) s_lastSpokenText = text;

    switch (s_backend) {
    case Backend::NVDA_SAPI:
    {
        EnsureCOMInitialized();

        if (interrupt) {
            // Cancel everything, then speak on both channels
            if (fn_cancelSpeech) fn_cancelSpeech();
            if (s_pVoice) s_pVoice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);

            // SAPI for audio
            bool sapiOk = false;
            if (s_pVoice)
                sapiOk = SUCCEEDED(s_pVoice->Speak(text, SPF_ASYNC, NULL));

            // NVDA for speech + braille
            if (fn_speakText) fn_speakText(text);
            if (fn_brailleMessage) fn_brailleMessage(text);

            return sapiOk;
        } else {
            // Queue mode: SAPI for audio (survives keypresses), NVDA braille only
            bool sapiOk = false;
            if (s_pVoice)
                sapiOk = SUCCEEDED(s_pVoice->Speak(text, SPF_ASYNC, NULL));

            if (fn_brailleMessage) fn_brailleMessage(text);

            return sapiOk;
        }
    }

    case Backend::SAPI_ONLY:
    {
        if (s_pVoice) {
            EnsureCOMInitialized();
            DWORD flags = SPF_ASYNC;
            if (interrupt) flags |= SPF_PURGEBEFORESPEAK;
            return SUCCEEDED(s_pVoice->Speak(text, flags, NULL));
        }
        return false;
    }

    default:
        return false;
    }
}

// ============================================================================
// v04.25: SAPI voice cycling
// ============================================================================

void CycleVoice()
{
    if (!s_pVoice) return;
    EnsureCOMInitialized();

    // Enumerate available voices via ISpObjectTokenCategory
    ISpObjectTokenCategory* pCat = nullptr;
    IEnumSpObjectTokens* pEnum = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                                   IID_ISpObjectTokenCategory, (void**)&pCat);
    if (FAILED(hr) || !pCat) return;

    hr = pCat->SetId(SPCAT_VOICES, FALSE);
    if (FAILED(hr)) { pCat->Release(); return; }

    hr = pCat->EnumTokens(NULL, NULL, &pEnum);
    pCat->Release();
    if (FAILED(hr) || !pEnum) return;

    ULONG count = 0;
    pEnum->GetCount(&count);
    if (count == 0) { pEnum->Release(); return; }

    s_currentVoiceIndex = (s_currentVoiceIndex + 1) % count;

    ISpObjectToken* pToken = nullptr;
    hr = pEnum->Item(s_currentVoiceIndex, &pToken);
    if (SUCCEEDED(hr) && pToken) {
        s_pVoice->SetVoice(pToken);

        // Get voice name for announcement
        WCHAR* name = nullptr;
        pToken->GetStringValue(NULL, &name);
        if (name) {
            wchar_t msg[256];
            wsprintfW(msg, L"Voice: %s", name);
            Log::Write("ScreenReader: Switched to voice %lu/%lu: %ls",
                       s_currentVoiceIndex + 1, count, name);
            Speak(msg, true);
            CoTaskMemFree(name);
        } else {
            wchar_t msg[64];
            wsprintfW(msg, L"Voice %lu of %lu", s_currentVoiceIndex + 1, count);
            Speak(msg, true);
        }
        pToken->Release();
    }
    pEnum->Release();
}

// ============================================================================
// v04.25: SAPI speech rate control (-10 to +10)
// ============================================================================

void IncreaseRate()
{
    if (!s_pVoice) return;
    EnsureCOMInitialized();
    if (s_currentRate < 10) s_currentRate++;
    s_pVoice->SetRate(s_currentRate);
    if (s_pVoice2) s_pVoice2->SetRate(s_currentRate);
    wchar_t msg[64];
    wsprintfW(msg, L"Rate %ld", s_currentRate);
    Log::Write("ScreenReader: Speech rate -> %ld", s_currentRate);
    Speak(msg, true);
}

// Set rate silently (no announcement) — used for applying defaults at startup.
void SetRate(long rate)
{
    if (rate < -10) rate = -10;
    if (rate >  10) rate =  10;
    s_currentRate = rate;
    if (s_pVoice) {
        EnsureCOMInitialized();
        s_pVoice->SetRate(s_currentRate);
    }
    if (s_pVoice2) s_pVoice2->SetRate(s_currentRate);
    Log::Write("ScreenReader: Speech rate set to %ld (silent).", s_currentRate);
}

void DecreaseRate()
{
    if (!s_pVoice) return;
    EnsureCOMInitialized();
    if (s_currentRate > -10) s_currentRate--;
    s_pVoice->SetRate(s_currentRate);
    if (s_pVoice2) s_pVoice2->SetRate(s_currentRate);
    wchar_t msg[64];
    wsprintfW(msg, L"Rate %ld", s_currentRate);
    Log::Write("ScreenReader: Speech rate -> %ld", s_currentRate);
    Speak(msg, true);
}

// ============================================================================
// v05.13: SAPI volume control (0-100, step 10)
// F5 = volume down, F6 = volume up (mapped in dinput8.cpp)
// ============================================================================

void IncreaseVolume()
{
    if (!s_pVoice) return;
    EnsureCOMInitialized();
    if (s_currentVolume < 100) {
        s_currentVolume = (USHORT)min(100, (int)s_currentVolume + 10);
    }
    s_pVoice->SetVolume(s_currentVolume);
    if (s_pVoice2) s_pVoice2->SetVolume(s_currentVolume);
    wchar_t msg[64];
    wsprintfW(msg, L"Volume %u percent", (unsigned)s_currentVolume);
    Log::Write("ScreenReader: Speech volume -> %u", (unsigned)s_currentVolume);
    Speak(msg, true);
}

void DecreaseVolume()
{
    if (!s_pVoice) return;
    EnsureCOMInitialized();
    if (s_currentVolume > 0) {
        s_currentVolume = (USHORT)max(0, (int)s_currentVolume - 10);
    }
    s_pVoice->SetVolume(s_currentVolume);
    if (s_pVoice2) s_pVoice2->SetVolume(s_currentVolume);
    wchar_t msg[64];
    wsprintfW(msg, L"Volume %u percent", (unsigned)s_currentVolume);
    Log::Write("ScreenReader: Speech volume -> %u", (unsigned)s_currentVolume);
    Speak(msg, true);
}

// ============================================================================
// v04.25: Repeat last spoken dialog (F1)
// ============================================================================

void RepeatLast()
{
    if (!s_initialized || s_lastSpokenText.empty()) {
        Speak(L"Nothing to repeat.", true);
        return;
    }
    Log::Write("ScreenReader: Repeating last dialog");
    Speak(s_lastSpokenText.c_str(), true);
}

bool Speak(const char* text, bool interrupt)
{
    if (!s_initialized || !text) return false;
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0) return false;
    wchar_t* wide = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, len);
    bool result = Speak(wide, interrupt);
    delete[] wide;
    return result;
}

bool Output(const wchar_t* text, bool interrupt)
{
    if (!s_initialized || !text) return false;
    return Speak(text, interrupt);
}

bool Output(const char* text, bool interrupt)
{
    if (!s_initialized || !text) return false;
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0) return false;
    wchar_t* wide = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, len);
    bool result = Output(wide, interrupt);
    delete[] wide;
    return result;
}

bool Silence()
{
    if (!s_initialized) return false;

    bool ok = false;

    // Cancel SAPI (both channels)
    if (s_pVoice) {
        EnsureCOMInitialized();
        ok = SUCCEEDED(s_pVoice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL));
    }
    if (s_pVoice2) {
        s_pVoice2->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    }

    // Cancel NVDA
    if (fn_cancelSpeech)
        fn_cancelSpeech();

    return ok;
}

// ============================================================================
// v0.10.32: Channel 2 — independent SAPI voice for battle events/damage
// ============================================================================
// Uses s_pVoice2 for audio output. Falls back to s_pVoice if voice 2 unavailable.
// Does NOT touch NVDA speech or braille — this is a parallel audio-only channel.

bool SpeakChannel2(const wchar_t* text, bool interrupt)
{
    if (!s_initialized || !text) return false;

    bool usingVoice2 = (s_pVoice2 != nullptr);
    ISpVoice* voice = usingVoice2 ? s_pVoice2 : s_pVoice;
    if (!voice) return false;

    EnsureCOMInitialized();
    DWORD flags = SPF_ASYNC;
    if (interrupt) flags |= SPF_PURGEBEFORESPEAK;
    HRESULT hr = voice->Speak(text, flags, NULL);
    // v0.10.33: Diagnostic — confirm channel 2 is actually producing audio
    if (FAILED(hr)) {
        Log::Write("ScreenReader: SpeakChannel2 FAILED hr=0x%08X voice2=%d", hr, (int)usingVoice2);
    }
    return SUCCEEDED(hr);
}

bool SpeakChannel2(const char* text, bool interrupt)
{
    if (!s_initialized || !text) return false;
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0) return false;
    wchar_t* wide = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, len);
    bool result = SpeakChannel2(wide, interrupt);
    delete[] wide;
    return result;
}

std::string GetScreenReaderName()
{
    switch (s_backend) {
    case Backend::NVDA_SAPI: return "NVDA+SAPI";
    case Backend::SAPI_ONLY: return "SAPI";
    default: return "";
    }
}

}  // namespace ScreenReader
