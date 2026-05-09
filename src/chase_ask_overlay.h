// chase_ask_overlay.h — Chase entry ASK overlay (manual / auto-drive choice)
//
// v0.15.1: New module. When the player enters the Dollet chase scene
// (detected by Squall's "Forget it!  Let's go!" MES firing in a chase
// field), this module presents an ASK choice between Auto-drive and
// Manual mode and persists the selection to ff8_accessibility.ini.
//
// Two-path hybrid implementation:
//
//   Primary path: engine-rendered window. Allocates a free slot in the
//     game's pWindowsArray (8 ff8_win_obj slots), populates it with
//     two options ("Auto-drive" / "Manual"), and registers callbacks
//     at the +0x34 / +0x38 offsets the engine reads. The engine's
//     normal frame loop renders the slot like any other ASK.
//
//   Belt-and-suspenders path: TTS + keyboard. ScreenReader speaks the
//     prompt and the highlighted option; up/down arrows cycle the
//     highlight (TTS announces the new highlighted option); Enter
//     confirms. This path works regardless of whether the engine
//     actually renders our slot, so the feature is functional on
//     first BAT even before we tune the engine-window template values.
//
// Auto-drive is not implemented in v0.15.1. Selecting "Auto-drive"
// announces "Auto-drive is not yet implemented, falling back to manual."
// then proceeds with manual mode.

#pragma once

namespace ChaseAskOverlay {

void Initialize();
void Shutdown();

// Per-tick driver. Polls keyboard for the ASK keys (up / down / Enter)
// when the ASK is open; otherwise a near-no-op.
void Update();

// Called by field_dialog's show_dialog hook for every captured field
// dialog text. Cheap strncmp filter — when the text matches Squall's
// chase-trigger MES AND we're currently in a chase field AND the ASK
// hasn't already fired this chase session, the ASK opens here.
void OnDialogText(const char* text);

// True while the chase ASK overlay is open (the player has not yet
// chosen). Used by other modules (e.g. field navigation) to defer
// any keypress they would otherwise consume.
bool IsAskActive();

}  // namespace ChaseAskOverlay
