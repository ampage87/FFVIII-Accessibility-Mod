// dialog_inject.h -- Mod-driven engine dialog injection (Phases 1, 2a, 2b).
//
// v0.15.4: Phase 1. Synthesize a phantom script_context and call
// opcode_mes(&ctx) directly. Proven to work end-to-end (BAT 17:35
// 2026-05-09): dialog renders visually, slot transitions advance,
// engine state machine progresses, gameObj bitmasks update, and the
// existing show_dialog hook fires for the slot.
//
// v0.15.5: Phase 2a. Same recipe, different opcode -- call
// opcode_ask(&ctx) with SP=6 to render the field's natural ASK at
// msg 0 with a different slot. Confirms the recipe extends to ASK
// and gives empirical data on the SWO_ASK arg layout. Bound to
// Shift+F12 (Phase 1 stays on F12 alone).
//
// v0.15.6: Phase 2b. Custom text injection via field_get_dialog_string
// hook override. dialog_inject.cpp now ships a per-fire override flag
// that field_dialog.cpp's Hook_field_get_dialog_string consults. When
// the flag is set, the hook returns our FF8-encoded buffer instead of
// calling the original game function. v0.15.6 BAT failed: zero
// [GETSTR-RAW] log lines despite the hook's unconditional first-10-calls
// logging. Diagnosis: FFNx's replace_call pattern rewrote the engine's
// CALL field_get_dialog_string operand to point at FFNx's own function,
// so our hook on engine 0x00530750 is dead under FFNx.
//
// v0.15.6.1: Phase 2b fix -- post-ASK slot+0x08 patching. Don't rely on
// the bypassed get_dialog_string hook. Inside Hook_opcode_ask, after
// s_origAsk has populated slot+0x08 with the natural text pointer,
// overwrite slot+0x08 with our override buffer pointer before
// ScanAndSpeakChoiceWindows reads it. The TTS path then decodes our
// text; the engine reads slot+0x08 every frame for rendering and input
// too, so visually the dialog also displays our text. firstQ/lastQ at
// slot+0x29/+0x2A are already our values from the opcode_ask call
// (BAT log confirmed firstQ=1 lastQ=3), so cursor positions are correct.
//
// Future v0.15.7+ ships answer detection (poll slot+0x2B for cursor
// changes; detect commit when gameObj.D2 bit clears) and v0.15.8 wires
// this into chase_ask_overlay::OpenAsk as the primary chase ASK path.
//
// See "Plan & Research Documents/Field dialog system disassembly
// analysis.md" (the follow-up correction section) and "Plan & Research
// Documents/ASK render binding deep research results.md" for the
// research backing this approach.
//
// Approach -- Path A from the deep research:
//   - Synthesize a fake script_context buffer.
//   - Set [+0x184] = SP byte.
//   - Write opcode args to script-VM stack positions [+sp*4]..[+(sp-N)*4].
//   - Call the opcode at the dispatch-table entry. The engine does
//     the rest: set_window_object(_ASK), sub_4A0620 open transition,
//     sub_49FD50 foreground (MES only), gameObj bitmask updates,
//     and (critically) the per-slot callback registration that
//     triggers actual rendering.
//
// v0.15.6.1 layer on top of Path A: when Phase2_TestAsk fires, the engine
// (via FFNx's wrapped opcode_ask) populates slot+0x08 (text_data1) with
// the field's natural text pointer. Our existing Hook_opcode_ask in
// field_dialog.cpp runs after s_origAsk returns. Before it scans the
// slot for TTS, it consults DialogInject::IsOverrideActive() and patches
// slot+0x08 to our override buffer when active. This works because:
//   1. slot+0x08 is the canonical text pointer the engine reads every
//      frame for rendering and input handling. Overwriting it after
//      opcode_ask returns and before the next frame substitutes our
//      text into both display and TTS paths.
//   2. Our buffer is statically allocated so it persists for the dialog's
//      lifetime.
//   3. The override flag is set just before opcode_ask and cleared just
//      after, so post-ASK patching only happens for our injected calls,
//      not natural game ASKs.
//   4. The slot index is communicated via GetOverrideSlot() so the patch
//      targets the correct slot.
//
// Why post-ASK and not pre-fetch: FFNx's replace_call rewrote the engine's
// internal CALL field_get_dialog_string to point at FFNx's own function.
// Our hook on engine 0x00530750 is unreachable under FFNx (BAT log proves
// zero calls during a full minute of gameplay). Post-ASK patching attacks
// the slot at a single well-defined point (after FFNx fully populates it,
// before our TTS scan reads it) and is robust to FFNx version changes
// because it doesn't depend on FFNx's internal addresses.
//
// Why this is needed: v0.15.0 - v0.15.2.1 attempted to populate an
// ff8_win_obj slot directly with byte-perfect contents. The slot was
// never rendered because show_dialog is registered as a per-slot
// callback via sub_4B6210/sub_4B6230 inside sub_4A0880 (window-system
// init) at engine startup. Externally-populated slots are never part
// of that registry. Calling the opcode goes through the callback
// registration path correctly.
//
// Verification (all automated, no sighted help required):
//   - Log the opcode return code (MES: 3=advance/success, 5=busy.
//     ASK: 1=wait, 5=busy, 3=advance after answer).
//   - The existing v0.04.36 dialog hook fires on opcode entry; SAPI
//     speaks the dialog text when our injected call enters.
//   - Per-frame poll of pWindowsArray[slot]+0x1C for ~3 sec; should
//     advance from 0 to 0x1000 (fully open) if rendering is alive.
//   - SAPI announces "Dialog inject phase X, slot N, return code Y."
//   - v0.15.6: when override is active, [GETSTR-OVERRIDE] log line
//     confirms the hook saw the flag and returned our buffer.
//
// Hotkeys (per the F12 rule in userMemories: one diagnostic per
// physical key state):
//   - F12 alone   = Phase 1 MES test (slot 1, msg 0).
//   - Shift + F12 = Phase 2 ASK test (slot 2). v0.15.6 includes
//                   Phase 2b custom text via override; the field's
//                   natural msg 0 is not used.

#pragma once

namespace DialogInject {

// One-time setup. Currently a no-op (no hooks; reads addresses at
// fire time so resolution failures are diagnosed at the call site
// rather than at startup).
void Initialize();

// Cleanup. Currently a no-op.
void Shutdown();

// Per-tick driver. When a Phase 1/2a test fires, this polls the
// target slot for ~3 seconds and logs the open-transition advance.
// Cheap no-op when no test is active.
void Update();

// Phase 1 test entry point. Synthesizes a phantom script_context and
// calls opcode_mes(&ctx) targeting slot 1 with msg_id 0. Logs the
// result and starts a 3-second slot poll. Safe no-op when not in
// field mode or when addresses are not yet resolved.
void Phase1_TestMes();

// Phase 2 test entry point. Synthesizes a phantom script_context
// with SP=6 and calls opcode_ask(&ctx) targeting slot 2.
//
// v0.15.5 (Phase 2a): used the field's natural msg 0 -- in doani1_2
// (Aaron's BAT field) that's the Selphie elevator ASK.
//
// v0.15.6 (Phase 2b): sets a field_get_dialog_string override before
// the opcode_ask call so the dialog renders our hardcoded
// "Mode? / Manual / Auto / Original" prompt regardless of which
// field msg 0 is. The override is cleared immediately after the
// opcode returns. Wire-format: 4 lines separated by 0x02 with 0x00
// terminator; firstQ=1, lastQ=3, curQ=1 (Manual selected).
//
// Bound to Shift+F12.
void Phase2_TestAsk();

// ============================================================================
// v0.15.6.1 Phase 2b: text override coordination
//
// field_dialog.cpp's Hook_opcode_ask consults these inside the post-ASK
// patch block to decide whether to overwrite slot+0x08 with our override
// buffer.
//
// IsOverrideActive(), GetOverrideText(), and GetOverrideSlot() must be
// safe to call from the game thread (the hook fires there). All three
// reads are atomic on x86 -- the active flag is a volatile LONG, the text
// pointer is 32-bit aligned, and the slot int is 32-bit aligned -- so we
// don't need a lock. The window between SetOverride() and ClearOverride()
// is a single function call (opcode_ask) on the same thread, so there's
// no race.
//
// v0.15.6.2 adds GetOverrideBufferStart/Size: expose the static buffer's
// address range so field_dialog.cpp's IsValidTextPointer check can
// whitelist our buffer. v0.15.6.1 BAT confirmed our pointer-swap landed
// (POST-ASK-OVERRIDE log line fired, slot+0x08 holds our address) but
// IsValidTextPointer rejected the pointer because our DLL's data section
// lives above 0x30000000, the upper bound of the existing FF8-heap-range
// heuristic. ScanAndSpeakChoiceWindows silently skipped the slot,
// Hook_show_dialog fell back to text_data2 (which still held the
// engine's natural prompt), and Aaron heard the natural text. The
// buffer's location is stable for the DLL's lifetime, so whitelisting
// by exact range is safe.
// ============================================================================
bool        IsOverrideActive();
const char* GetOverrideText();
int         GetOverrideSlot();   // v0.15.6.1: which slot Hook_opcode_ask should patch

// v0.15.6.2: stable bounds of the override buffer for IsValidTextPointer
// whitelisting. These return s_overrideBuffer's static-storage range and
// do NOT depend on the override flag being active -- show_dialog can fire
// after ClearOverride and still need to validate the buffer.
const unsigned char* GetOverrideBufferStart();
unsigned int         GetOverrideBufferSize();

}  // namespace DialogInject

