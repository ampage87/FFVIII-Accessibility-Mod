// field_dialog.h - Field dialog TTS via opcode_mes hooks
//
// Hooks the JSM script interpreter's MES opcode handler using MinHook.
// When a dialog message is triggered, we intercept the call, let the game
// set up the dialog normally, then extract and decode the text for TTS.
//
// v04.00-diag1: Diagnostic build — log dialog text, no TTS yet.

#pragma once

#include <cstdint>

namespace FieldDialog {

// Initialize the field dialog module.
// Resolves hook targets from FF8Addresses and creates MinHook detours.
// Call after FF8Addresses::Resolve() and MH_Initialize().
// Returns true if hooks were created successfully.
bool Initialize();

// Shutdown — disable hooks and clean up.
void Shutdown();

// Is the module active (hooks installed)?
bool IsActive();

// Polling fallback — call from accessibility thread every ~100ms.
// Catches dialogs that bypass hooked opcodes.
void PollWindows();

// v04.25: Repeat the last spoken dialog text (F5 hotkey).
void RepeatLastDialog();

// v05.37: Returns true if any dialog window is currently open (state != 0).
// Used by FieldNavigation to suspend auto-drive key injection during cutscenes.
bool IsDialogOpen();

// v0.18.3.15: Timber train-hijack guard mode (#58). Selectable via INI key
// `train_guard_mode` [Accessibility] and an in-engine ASK. Exposed so
// FieldNavigation's Manual-mode proximity cue can read the active mode and
// the ASK can set it.
//
// v0.18.3.22: relabeled to the user-facing scheme. NUMERIC VALUES UNCHANGED
// from the old 0=Original / 1=Manual / 2=Skip mapping -- only the names and
// the default changed (default is now Manual=0, was the frozen mode=1). A
// fully-vanilla option was dropped: without the code announce it strands a
// blind player.
//   TGM_MANUAL (0) = guards move; code announce + per-guard proximity cues
//                    (was "Original"). The DEFAULT.
//   TGM_FREEZE (1) = guards held; player just enters codes (was "Manual").
//   TGM_SKIP   (2) = bypass the train scene (not yet built).
enum TrainGuardModeVal { TGM_MANUAL = 0, TGM_FREEZE = 1, TGM_SKIP = 2 };
int  GetTrainGuardMode();          // cached; reads the INI on first call
void SetTrainGuardMode(int mode);  // updates the cache + persists to INI

// v07.09: Expose text rendering call counters for save screen diagnostic.
LONG GetMenuDrawTextCallCount();
LONG GetGetCharWidthCallCount();

// v07.10: Snapshot and reset the GCW accumulation buffer.
// Returns the number of bytes copied. Caller provides buffer and max size.
int SnapshotGcwBuffer(uint8_t* outBuf, int maxLen);

}  // namespace FieldDialog
