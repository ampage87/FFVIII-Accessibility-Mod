// audio_ducker.h - Reusable per-bus ducking module for FF8 Accessibility Mod
//
// v0.14.47: New module. Drives BGM and SFX gain envelopes during TTS so
// game audio doesn't mask speech. Designed to be domain-agnostic — the FF8
// mod registers function pointers for read/write of each bus's user volume,
// and AudioDucker handles the reference count, hold timer, and one-pole
// envelope smoothing.
//
// Design points (from the v0.14.47 spec):
//   1. Per-bus configuration (depth/attack/release/hold), distinct values
//      for BGM (deep duck, ~-15 dB) and SFX (light duck, ~-6 dB).
//   2. Reference-counted trigger (BeginDuck/EndDuck). Stays active until
//      count returns to zero. Future-proofed for concurrent TTS channels.
//   3. Smoothed envelope: one-pole filter with separate attack/release
//      time constants. Recomputed coefficients per tick, frame-rate
//      independent.
//   4. Hold time: minimum duration the duck stays at full depth before
//      release can start. Bridges natural micro-pauses inside SAPI's
//      chunk boundaries that flip IsSpeaking() to false mid-sentence.
//   5. dB-based config, linear-applied math: depths configured in dB,
//      converted to linear gain internally via LinearFromDb.
//   6. Volume preservation: ducker reads CURRENT user volume from the
//      Get callback every tick, never snapshots. User can change volume
//      mid-duck (F5–F8) and the duck math tracks. Ducker writes via
//      userVolume * envelope, leaving the underlying user level untouched.
//   7. Update driver: piggybacks on the FF8 mod's AccessibilityThread via
//      GameAudio::Update (~62.5 Hz). No dedicated thread.
//
// Usage — sync (the spec's manual-control example):
//
//     AudioDucker::BeginDuck();
//     PlayLongAlertSound();
//     Sleep(2000);
//     AudioDucker::EndDuck();
//
// Usage — async (current FF8 mod, in GameAudio::Update each tick):
//
//     bool speaking = ScreenReader::IsSpeaking();
//     if (speaking && !s_wasSpeaking) AudioDucker::BeginDuck();
//     if (!speaking && s_wasSpeaking) AudioDucker::EndDuck();
//     s_wasSpeaking = speaking;
//     AudioDucker::Tick(dtMs);
//
// All envelope math runs in linear gain (0..1). dB depths in BusConfig are
// converted to linear once at Initialize and stored.

#pragma once

#include <cstdint>

namespace AudioDucker {

// Per-bus configuration. depthDb is the duck depth (negative dB; e.g., -15
// drops the bus to ~17.8% of its user level). Times in milliseconds.
struct BusConfig {
    float depthDb;     // Duck depth in dB (negative; 0 = no duck)
    float attackMs;    // One-pole time constant for going TO duck depth
    float releaseMs;   // One-pole time constant for returning to 0 dB
    float holdMs;      // Minimum hold at depth before release allowed
};

// Function-pointer interface for reading and writing a bus's volume.
// Both work in linear gain space (0.0 = mute, 1.0 = full). The Get callback
// returns the user's intended volume; the Set callback writes the EFFECTIVE
// volume (after duck scaling) to the audio backend.
typedef float (*GetVolumeFn)();
typedef void  (*SetVolumeFn)(float linearGain);

// Initialize the ducker. Call once after volume hooks are installed (so the
// Get/Set callbacks return real data). Safe to call before; Get/Set should
// be no-ops when their underlying mechanism isn't ready yet.
void Initialize(
    GetVolumeFn getBgm, SetVolumeFn setBgm, const BusConfig& bgmCfg,
    GetVolumeFn getSfx, SetVolumeFn setSfx, const BusConfig& sfxCfg);

// Stop ducking. Sets envelopes to 1.0 logically — caller is responsible for
// restoring final user volume via its own mechanism (FF8 mod's
// GameAudio::Shutdown intentionally writes 1.0 to release before unhooking,
// which conflicts with restoring user level — the ducker stays out of that).
void Shutdown();

// Master enable. When false, BeginDuck is a no-op, and any in-progress
// duck releases immediately at the configured release tau (audio recovers
// over ~600 ms by default rather than snapping).
void SetEnabled(bool enabled);
bool IsEnabled();

// Reference-counted trigger. Multiple BeginDuck calls without matching
// EndDuck stack; envelope releases only when count returns to zero AND
// the hold timer has expired.
void BeginDuck();
void EndDuck();

// True if either bus is below full level (active duck or mid-release) or
// the refcount is non-zero. Use to gate competing volume-write paths.
bool IsActive();

// Tick the envelope. Call once per update cycle. dtMs is the elapsed
// milliseconds since the previous Tick call. Internal clamp to [1, 250]
// guards against absurd values from stalls / first-frame bootstrap.
void Tick(float dtMs);

// Helper: convert dB to linear gain. dB <= 0; returns value in (0, 1].
//   0 dB -> 1.0
//  -6 dB -> ~0.501
// -15 dB -> ~0.178
// -60 dB -> ~0.001
float LinearFromDb(float dB);

}  // namespace AudioDucker
