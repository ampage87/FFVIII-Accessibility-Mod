// gf_audio_desc.h - GF Summon Audio Description System (v0.14.44)
//
// Provides text-to-speech narration of GF summon animations using WebVTT
// cue files. Mirrors fmv_audio_desc.cpp/h: loads VTT files embedded as
// Win32 resources, parses cue timings, and fires the next cue when its
// start time is reached.
//
// Trigger flow:
//   battle_tts_ewm.inl::PollBattleMagicId()  detects battle_magic_id
//   transitioning to a known GF effect ID. It then calls
//   GfAudioDesc::OnGFAnimationStart(effectId), which looks up the matching
//   VTT track and starts cue playback on Channel 2 (event voice).
//
// Stop conditions are handled inside OnFrame():
//   - battle_magic_id reverts to non-GF (animation ended OR player skipped)
//   - All cues finished (we stay alive until magic_id reverts)
//   - Battle exited (caller invokes StopPlayback)
//
// VTTs are hand-authored from a reference video and embedded via
// resources.rc using a per-GF resource ID = IDR_VTT_GF_BASE + N.

#pragma once

namespace GfAudioDesc
{
    // Initialize: load all GF VTT resources and set up effect-id -> track map.
    // dllModule: HMODULE of our DLL, used to call FindResource/LoadResource.
    void Initialize(HMODULE dllModule);

    // Shut down and release resources.
    void Shutdown();

    // Called every frame from the main accessibility loop.
    // Detects battle_magic_id reverting to non-GF (= summon ended/skipped)
    // and fires the next cue when its start time is reached.
    void OnFrame();

    // Called from PollBattleMagicId() when battle_magic_id changes to a GF
    // effect ID. Looks up the matching VTT and starts playback.
    // No-op if no VTT exists for this effect ID, if a summon is already
    // playing (re-entrancy guard), or if the system is uninitialized.
    void OnGFAnimationStart(int effectId);

    // Force stop any current playback (e.g. battle ended).
    void StopPlayback();
}
