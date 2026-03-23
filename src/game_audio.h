// game_audio.h - Centralized game audio control for FF8 Accessibility Mod
//
// v0.09.22: Extracted from dinput8.cpp. Consolidates all audio control:
//   - BGM volume hook (FFNx set_music_volume_for_channel)
//   - F3/F4 BGM volume up/down
//   - Periodic volume re-application (fixes infirmary/classroom bug)
//   - Future: SFX volume, default volume persistence
//
// The BGM volume bug (infirmary/classroom scenes) was caused by FFNx's
// set_music_volume_for_channel not forcing volume on already-playing tracks.
// Fix: periodic re-application of the scaled volume via the original function.

#pragma once

#include <windows.h>
#include <cstdint>

namespace GameAudio {

// Call once after MH_Initialize() and FF8Addresses::Resolve().
// Starts polling for FFNx's JMP patch on set_midi_volume.
void Initialize();

// Call each frame from AccessibilityThread main loop.
// Handles deferred hook installation and periodic volume re-application.
void Update();

// Call before MH_DisableHook(MH_ALL_HOOKS).
void Shutdown();

// User-facing BGM volume controls (F3/F4).
// Steps volume by 10%, announces via TTS.
void VolumeDown();
void VolumeUp();

}  // namespace GameAudio
