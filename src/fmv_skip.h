// fmv_skip.h - FMV Skip Feature (v03.00)
//
// Allows the user to skip in-game FMVs by pressing Backspace.
// Hooks CreateFileA, CreateFileW, CloseHandle, AND ReadFile to intercept
// the movie decoder's file I/O. On skip, ReadFile returns 0 bytes (EOF)
// for tracked AVI handles, causing the decoder to end playback.
//
// FMV end is detected by CloseHandle hook (all AVI handles closed).
// Key detection uses GetAsyncKeyState polling (no InputHook dependency).
//
// Ported from FF8 Remastered Accessibility Mod (v31.31).
// Adapted for Original PC + FFNx: simplified input, uses ScreenReader for TTS.

#pragma once

#include <string>

namespace FmvSkip
{
    // Initialize the FMV skip system and install MinHook-based hooks.
    // Must be called AFTER MH_Initialize() and BEFORE MH_EnableHook(MH_ALL_HOOKS).
    void Initialize();

    // Shutdown and cleanup.
    void Shutdown();

    // Called every frame from the main accessibility loop.
    // Polls for Backspace key and handles skip TTS announcement.
    void OnFrame();

    // Returns true if we believe an FMV is currently playing
    // (based on AVI file handle tracking).
    bool IsMoviePlaying();

    // Returns the lowercase basename of the current AVI file being played.
    // Empty string if no AVI is active.
    // Used by FmvAudioDesc to match AVI files to VTT tracks.
    std::string GetCurrentAviName();
}
