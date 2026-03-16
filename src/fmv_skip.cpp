// fmv_skip.cpp - FMV Skip Feature (v03.00)
//
// Ported from FF8 Remastered Accessibility Mod (v31.31).
//
// Hook summary (4 hooks via MinHook on kernel32):
//   - CreateFileA/W: Capture AVI file handles when opened.
//   - CloseHandle: Detect when AVI handles close (FMV ended).
//   - ReadFile: Return 0 bytes for AVI handles when skip is active.
//
// Architecture:
//   - On AVI open: register handle in tracked set, mark FMV playing.
//   - On Backspace (polled via GetAsyncKeyState): set g_skipActive flag.
//   - On ReadFile for tracked AVI handle while g_skipActive: return
//     TRUE with *lpNumberOfBytesRead = 0 (simulates EOF).
//   - On CloseHandle for tracked AVI handle: remove from set. When
//     set empties, FMV has truly ended.
//
// Adaptations from Remaster:
//   - Input: GetAsyncKeyState polling replaces InputHook callback
//   - TTS: ScreenReader::Speak replaces SpeechThread::QueueSpeak
//   - Logging: Log::Write replaces OutputDebug

#include "ff8_accessibility.h"
#include "fmv_skip.h"
#include "fmv_audio_desc.h"
#include "minhook/include/MinHook.h"
#include <windows.h>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>

namespace FmvSkip
{
    // ================================================================
    // Configuration
    // ================================================================
    static const int SKIP_KEY = VK_BACK;  // Backspace

    // ================================================================
    // State
    // ================================================================
    static bool g_initialized = false;
    static bool g_moviePlaying = false;
    static std::string g_currentAvi;  // lowercase basename of current AVI

    // Skip flag — when true, ReadFile hook returns 0 bytes for AVI handles
    static std::atomic<bool> g_skipActive{false};

    // Skip request flag, consumed by OnFrame for TTS announcement
    static std::atomic<bool> g_skipRequested{false};

    // Edge detection for Backspace key (polling-based)
    static bool g_backspaceWasDown = false;

    // ================================================================
    // AVI File Handle Tracking
    // ================================================================
    static std::set<HANDLE> g_aviHandles;
    static std::string g_aviFilePath;  // basename of current AVI
    static std::mutex g_aviMutex;

    // Hook typedefs
    typedef HANDLE(WINAPI* CreateFileA_t)(
        LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileA_t g_originalCreateFileA = nullptr;

    typedef HANDLE(WINAPI* CreateFileW_t)(
        LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileW_t g_originalCreateFileW = nullptr;

    typedef BOOL(WINAPI* CloseHandle_t)(HANDLE);
    static CloseHandle_t g_originalCloseHandle = nullptr;

    typedef BOOL(WINAPI* ReadFile_t)(
        HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    static ReadFile_t g_originalReadFile = nullptr;

    // ================================================================
    // Utility
    // ================================================================
    static std::string ToLower(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    static std::string BaseName(const std::string& path)
    {
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos)
            return path.substr(pos + 1);
        return path;
    }

    static bool EndsWithAvi(const std::string& s)
    {
        std::string lower = ToLower(s);
        return lower.size() > 4 && lower.substr(lower.size() - 4) == ".avi";
    }

    static std::string WideToNarrow(const wchar_t* wide)
    {
        if (!wide) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], len, nullptr, nullptr);
        return result;
    }

    // ================================================================
    // Handle registration / FMV state management
    // ================================================================
    static void RegisterAviHandle(HANDLE handle, const std::string& filename, const char* source)
    {
        std::lock_guard<std::mutex> lock(g_aviMutex);

        std::string base = BaseName(filename);
        std::string baseLower = ToLower(base);

        // If this is a different AVI, reset state
        if (!g_aviFilePath.empty() && ToLower(g_aviFilePath) != baseLower)
        {
            Log::Write("[FmvSkip] New AVI detected (%s), clearing previous handles for %s",
                base.c_str(), g_aviFilePath.c_str());
            g_aviHandles.clear();
            g_skipActive.store(false);
        }

        g_aviHandles.insert(handle);
        g_aviFilePath = base;

        // Mark FMV as playing
        if (!g_moviePlaying)
        {
            g_moviePlaying = true;
            g_currentAvi = baseLower;
            g_skipActive.store(false);
            g_skipRequested.store(false);
            Log::Write("[FmvSkip] FMV started: %s", base.c_str());
        }

        Log::Write("[FmvSkip] AVI file opened via %s: %s (handle=0x%p, total=%zu)",
            source, base.c_str(), handle, g_aviHandles.size());
    }

    static void OnAviHandleClosed(HANDLE handle)
    {
        std::lock_guard<std::mutex> lock(g_aviMutex);

        auto it = g_aviHandles.find(handle);
        if (it == g_aviHandles.end())
            return;

        g_aviHandles.erase(it);
        Log::Write("[FmvSkip] AVI handle closed: 0x%p (remaining=%zu) file=%s",
            handle, g_aviHandles.size(), g_aviFilePath.c_str());

        if (g_aviHandles.empty() && g_moviePlaying)
        {
            bool wasSkipped = g_skipActive.load();
            Log::Write("[FmvSkip] All AVI handles closed - FMV ended: %s%s",
                g_aviFilePath.c_str(),
                wasSkipped ? " [SKIPPED BY USER]" : " [NATURAL END]");

            g_moviePlaying = false;
            g_skipActive.store(false);
            g_currentAvi.clear();
            g_aviFilePath.clear();
            g_skipRequested.store(false);

            // Notify audio descriptions that the FMV ended
            FmvAudioDesc::StopPlayback();
        }
    }

    // ================================================================
    // CreateFileA Hook
    // ================================================================
    static HANDLE WINAPI HookedCreateFileA(
        LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
        LPSECURITY_ATTRIBUTES lpSA, DWORD dwCreationDisposition,
        DWORD dwFlags, HANDLE hTemplate)
    {
        HANDLE result = g_originalCreateFileA(
            lpFileName, dwDesiredAccess, dwShareMode,
            lpSA, dwCreationDisposition, dwFlags, hTemplate);

        if (lpFileName && result != INVALID_HANDLE_VALUE)
        {
            std::string filename(lpFileName);
            if (EndsWithAvi(filename))
                RegisterAviHandle(result, filename, "CreateFileA");
        }

        return result;
    }

    // ================================================================
    // CreateFileW Hook
    // ================================================================
    static HANDLE WINAPI HookedCreateFileW(
        LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
        LPSECURITY_ATTRIBUTES lpSA, DWORD dwCreationDisposition,
        DWORD dwFlags, HANDLE hTemplate)
    {
        HANDLE result = g_originalCreateFileW(
            lpFileName, dwDesiredAccess, dwShareMode,
            lpSA, dwCreationDisposition, dwFlags, hTemplate);

        if (lpFileName && result != INVALID_HANDLE_VALUE)
        {
            std::string filename = WideToNarrow(lpFileName);
            if (EndsWithAvi(filename))
                RegisterAviHandle(result, filename, "CreateFileW");
        }

        return result;
    }

    // ================================================================
    // CloseHandle Hook
    // ================================================================
    static BOOL WINAPI HookedCloseHandle(HANDLE hObject)
    {
        OnAviHandleClosed(hObject);
        return g_originalCloseHandle(hObject);
    }

    // ================================================================
    // ReadFile Hook
    //
    // The key skip mechanism. When g_skipActive is true and the handle
    // is a tracked AVI handle, returns TRUE with *lpNumberOfBytesRead = 0.
    // This simulates EOF, causing the movie decoder to end playback.
    // ================================================================
    static BOOL WINAPI HookedReadFile(
        HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
        LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
    {
        // Fast path: if skip isn't active, pass through immediately
        if (g_skipActive.load(std::memory_order_relaxed))
        {
            std::lock_guard<std::mutex> lock(g_aviMutex);
            if (g_aviHandles.count(hFile) > 0)
            {
                // Return 0 bytes read = EOF
                if (lpNumberOfBytesRead)
                    *lpNumberOfBytesRead = 0;

                // Log first occurrence only (avoid spam)
                static HANDLE lastLoggedHandle = INVALID_HANDLE_VALUE;
                if (hFile != lastLoggedHandle)
                {
                    Log::Write("[FmvSkip] ReadFile intercepted: returning EOF for handle 0x%p (requested %u bytes)",
                        hFile, nNumberOfBytesToRead);
                    lastLoggedHandle = hFile;
                }

                return TRUE;  // "Successful" read of 0 bytes = EOF
            }
        }

        return g_originalReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
            lpNumberOfBytesRead, lpOverlapped);
    }

    // ================================================================
    // Public API
    // ================================================================

    void Initialize()
    {
        if (g_initialized) return;
        g_initialized = true;

        // Install MinHook hooks on kernel32 file I/O functions.
        // MH_Initialize() must have been called before this.
        int hookCount = 0;

        MH_STATUS statusA = MH_CreateHookApi(
            L"kernel32", "CreateFileA",
            (LPVOID)HookedCreateFileA,
            (LPVOID*)&g_originalCreateFileA);
        if (statusA == MH_OK) hookCount++;

        MH_STATUS statusW = MH_CreateHookApi(
            L"kernel32", "CreateFileW",
            (LPVOID)HookedCreateFileW,
            (LPVOID*)&g_originalCreateFileW);
        if (statusW == MH_OK) hookCount++;

        MH_STATUS statusC = MH_CreateHookApi(
            L"kernel32", "CloseHandle",
            (LPVOID)HookedCloseHandle,
            (LPVOID*)&g_originalCloseHandle);
        if (statusC == MH_OK) hookCount++;

        MH_STATUS statusR = MH_CreateHookApi(
            L"kernel32", "ReadFile",
            (LPVOID)HookedReadFile,
            (LPVOID*)&g_originalReadFile);
        if (statusR == MH_OK) hookCount++;

        Log::Write("[FmvSkip] Hooks: CreateFileA=%s CreateFileW=%s CloseHandle=%s ReadFile=%s (%d/4)",
            statusA == MH_OK ? "OK" : "FAIL",
            statusW == MH_OK ? "OK" : "FAIL",
            statusC == MH_OK ? "OK" : "FAIL",
            statusR == MH_OK ? "OK" : "FAIL",
            hookCount);

        Log::Write("[FmvSkip] FMV skip initialized (Backspace to skip).");
    }

    void Shutdown()
    {
        g_initialized = false;

        std::lock_guard<std::mutex> lock(g_aviMutex);
        g_aviHandles.clear();
        g_aviFilePath.clear();
        g_moviePlaying = false;
        g_skipActive.store(false);
        g_currentAvi.clear();

        Log::Write("[FmvSkip] Shutdown.");
    }

    bool IsMoviePlaying()
    {
        return g_moviePlaying;
    }

    std::string GetCurrentAviName()
    {
        // Return the lowercase basename (e.g., "disc00_30h.avi")
        return g_currentAvi;
    }

    void OnFrame()
    {
        if (!g_initialized) return;

        // --- Poll for Backspace key (edge-triggered) ---
        bool backspaceDown = (GetAsyncKeyState(SKIP_KEY) & 0x8000) != 0;

        if (backspaceDown && !g_backspaceWasDown)
        {
            // Rising edge: Backspace just pressed
            bool playing = g_moviePlaying;
            bool alreadySkipping = g_skipActive.load();

            Log::Write("[FmvSkip] Backspace pressed (moviePlaying=%d, skipActive=%d, handles=%zu)",
                playing ? 1 : 0, alreadySkipping ? 1 : 0, g_aviHandles.size());

            if (playing && !alreadySkipping)
            {
                g_skipActive.store(true);
                g_skipRequested.store(true);

                // Stop audio descriptions immediately
                FmvAudioDesc::StopPlayback();

                Log::Write("[FmvSkip] Skip activated - ReadFile will return EOF for AVI handles.");
            }
        }
        g_backspaceWasDown = backspaceDown;

        // --- Announce skip via TTS ---
        bool justRequested = g_skipRequested.exchange(false);
        if (justRequested)
        {
            ScreenReader::Speak("Skipping video", true);
        }

        // --- Cross-reference with FF8Addresses for state consistency ---
        // If our handle tracking says movie is playing but the game disagrees,
        // log it for debugging (don't force-correct, as timing may differ).
        if (g_moviePlaying && FF8Addresses::HasMovieObject())
        {
            bool gameThinks = FF8Addresses::IsMoviePlaying();
            // Only log transitions, not every frame
            static bool s_lastGameState = false;
            if (gameThinks != s_lastGameState)
            {
                Log::Write("[FmvSkip] Game movie state changed: %s (our state: playing=%d)",
                    gameThinks ? "PLAYING" : "STOPPED", g_moviePlaying ? 1 : 0);
                s_lastGameState = gameThinks;
            }
        }
    }

}  // namespace FmvSkip
