// game_audio.h - Centralized game audio control for FF8 Accessibility Mod
//
// v0.09.22: Extracted from dinput8.cpp. Consolidates all audio control:
//   - BGM volume hook (FFNx set_music_volume_for_channel)
//   - F7/F8 BGM volume up/down (was F3/F4 pre-v0.14.45)
//   - Periodic volume re-application (fixes infirmary/classroom bug)
// v0.14.45: Added SFX volume hook (FFNx setSFXMasterVolume) and audio-ducking
//   toggle. Phase 1 of the audio mixing work — duck logic itself lands in
//   v0.14.46.
//
// The BGM volume bug (infirmary/classroom scenes) was caused by FFNx's
// set_music_volume_for_channel not forcing volume on already-playing tracks.
// Fix: periodic re-application of the scaled volume via the original function.

#pragma once

#include <windows.h>
#include <cstdint>

namespace GameAudio {

// Call once after MH_Initialize() and FF8Addresses::Resolve().
// Starts polling for FFNx's JMP patch on set_midi_volume and sfx_set_master_volume.
void Initialize();

// Call each frame from AccessibilityThread main loop.
// Handles deferred hook installation and periodic volume re-application
// for both BGM and SFX.
void Update();

// Call before MH_DisableHook(MH_ALL_HOOKS).
void Shutdown();

// User-facing BGM volume controls (F7/F8 in v0.14.45+).
// Steps volume by 10%, announces via TTS, persists to ff8_accessibility.ini.
void VolumeDown();
void VolumeUp();

// v0.14.45: User-facing SFX volume controls (F5/F6).
// Steps volume by 10%, announces via TTS, persists as `sfx_volume` (0-100).
// Note: FFNx's setSFXMasterVolume only stores the value; in-flight SFX keep
// their original volume. New SFX honor the new master.
void SfxVolumeDown();
void SfxVolumeUp();

// v0.14.45: TTS-during-SFX auto-duck toggle (F2).
// Phase 1: persists the bool and announces state. Auto-duck logic itself
// lands in v0.14.46 (Phase 2).
void ToggleDucking();
bool IsDuckingEnabled();

// v0.14.47: Float accessors for AudioDucker integration. Get returns the
// user's intended volume (0..1, untouched by ducking math); Apply writes
// the effective volume (user * envelope) to the audio backend via the
// existing direct-call paths. Both no-op if their hook isn't installed.
float GetUserBgmVolume();
float GetUserSfxVolume();
void  ApplyBgmVolume(float linearGain);
void  ApplySfxVolume(float linearGain);

}  // namespace GameAudio
