// fmv_audio_desc.h - FMV Audio Description System (v03.00)
//
// Provides text-to-speech narration of FMV cutscenes using WebVTT cue files.
// Loads .vtt files from the Audio Descriptions folder and maps AVI filenames
// to description tracks via mapping.txt.
//
// Ported from FF8 Remastered Accessibility Mod (v31.30).
// Adapted for Original PC + FFNx: uses ScreenReader for TTS output,
// FmvSkip for AVI filename detection, and FF8Addresses for movie state.

#pragma once

namespace FmvAudioDesc
{
    // Initialize the FMV audio description system.
    // Loads VTT files from the Audio Descriptions folder and sets up AVI-to-VTT mappings.
    // dllModule: HMODULE of our DLL, used to locate the Audio Descriptions folder.
    void Initialize(HMODULE dllModule);

    // Shut down and release resources.
    void Shutdown();

    // Called every frame from the main accessibility loop.
    // Monitors for new AVI playback (via FmvSkip) and fires TTS cues at the right times.
    void OnFrame();

    // Force stop any current playback (e.g., when user skips an FMV or movie ends).
    void StopPlayback();
}
