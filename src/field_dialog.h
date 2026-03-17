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

// v07.09: Expose text rendering call counters for save screen diagnostic.
LONG GetMenuDrawTextCallCount();
LONG GetGetCharWidthCallCount();

// v07.10: Snapshot and reset the GCW accumulation buffer.
// Returns the number of bytes copied. Caller provides buffer and max size.
int SnapshotGcwBuffer(uint8_t* outBuf, int maxLen);

}  // namespace FieldDialog
