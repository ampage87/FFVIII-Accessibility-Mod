// chase_ask_overlay.h — Chase entry ASK overlay (manual / auto / original)
//
// v0.15.1: New module. When the player enters the Dollet chase scene
// (detected by Squall's "Forget it!  Let's go!" MES firing in a chase
// field), this module presents an ASK choice for the chase mode.
//
// v0.15.2.2: After several iterations of attempting to render an
// engine-allocated proxy slot, that approach was abandoned. The
// engine doesn't render slots populated from outside the script-VM;
// rendering is bound to script-VM context. v0.15.2.2 shipped TTS-only
// (no visible in-game dialog).
//
// v0.15.8: Wired into DialogInject's OpenAsk pipeline. The chase ASK
// now renders as a real engine dialog (via the v0.15.6.2 + v0.15.7.1
// engine-rendered + answer-detected pipeline), with the player using
// FF8's natural arrow + X (confirm) keys to select. chase_ask_overlay
// owns the trigger detection and the chase-mode dispatch; DialogInject
// owns rendering, cursor input, and answer detection.
//
// Three options: Manual / Auto / Original. All three currently route to
// MODE_MANUAL since Auto (v0.15.9) and Original (v0.15.10) aren't
// implemented yet. Mode-specific announcements make the chosen option
// clear and indicate fallback.

#pragma once

namespace ChaseAskOverlay {

void Initialize();
void Shutdown();

// Per-tick driver. Polls DialogInject::GetLastAnswer() while the ASK is
// open; otherwise a near-no-op. Also handles the deferred-open timer
// (3-second delay so Squall's chase-trigger line plays first).
void Update();

// Called by field_dialog's show_dialog hook for every captured field
// dialog text. Cheap strstr filter — when the text matches Squall's
// chase-trigger MES AND we're currently in a chase field AND the ASK
// hasn't already fired this chase session, we defer-open the ASK.
void OnDialogText(const char* text);

// True while the chase ASK overlay is open (DialogInject::OpenAsk
// returned true, GetLastAnswer hasn't returned a non--1 yet).
// Reserved for future input-gating consumers.
bool IsAskActive();

}  // namespace ChaseAskOverlay
