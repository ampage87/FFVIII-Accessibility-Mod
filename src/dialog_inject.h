// dialog_inject.h -- Mod-driven engine dialog injection (Phase 1).
//
// v0.15.4: New module. The first concrete step toward replacing the
// chase ASK's TTS+keyboard-only path with the engine's actual rendered
// ASK dialog box. Phase 1 proves the recipe by synthesizing a phantom
// script_context and calling opcode_mes(&ctx) directly. If the dialog
// renders, the recipe is solid and Phase 2 (chase ASK via opcode_ask)
// is mechanical.
//
// See "Plan & Research Documents/Field dialog system disassembly
// analysis.md" (the follow-up correction section) and "Plan & Research
// Documents/ASK render binding deep research results.md" for the
// research backing this approach.
//
// Approach -- Path A from the deep research:
//   - Synthesize a fake script_context buffer.
//   - Set [+0x184] = SP byte (e.g. 2).
//   - Write opcode args to [+sp*4] and [+(sp-1)*4] (msg_id, slot index).
//   - Call opcode_mes(&ctx) at the dispatch-table entry. The engine
//     does the rest: set_window_object, sub_4A0620 open transition,
//     sub_49FD50 foreground, gameObj bitmask updates, and (critically)
//     the per-slot callback registration that triggers actual rendering.
//
// Why this is needed: v0.15.0 - v0.15.2.1 attempted to populate an
// ff8_win_obj slot directly with byte-perfect contents. The slot was
// never rendered because show_dialog is registered as a per-slot
// callback via sub_4B6210/sub_4B6230 inside sub_4A0880 (window-system
// init) at engine startup. Externally-populated slots are never part
// of that registry. Calling opcode_mes goes through the callback
// registration path correctly.
//
// Verification (all automated, no sighted help required):
//   - Log the opcode_mes return code (3 = advance/success, 5 = slot busy).
//   - The existing v0.04.36 dialog hook fires for opcode_mes; SAPI
//     speaks the dialog text when our injected call enters.
//   - Per-frame poll of pWindowsArray[slot]+0x1C for ~3 sec; should
//     advance from 0 to 0x1000 (fully open) if rendering is alive.
//   - SAPI announces "Dialog inject phase one, slot N, return code X."
//
// Hotkey: F12 (replaces v0.15.0's chase-diag F12 binding; chase chapter
// is complete, chase_diag module remains in source but no longer hot-
// keyed). Per the F12 rule in userMemories: only one diagnostic active
// on F12 at a time.

#pragma once

namespace DialogInject {

// One-time setup. Currently a no-op (no hooks; reads addresses at
// fire time so resolution failures are diagnosed at the call site
// rather than at startup).
void Initialize();

// Cleanup. Currently a no-op.
void Shutdown();

// Per-tick driver. When a Phase 1 test fires, this polls the target
// slot for ~3 seconds and logs the open-transition advance. Cheap
// no-op when no test is active.
void Update();

// Phase 1 test entry point. Synthesizes a phantom script_context and
// calls opcode_mes(&ctx) targeting slot 1 with msg_id 0. Logs the
// result and starts a 3-second slot poll. Safe no-op when not in
// field mode or when addresses are not yet resolved.
void Phase1_TestMes();

}  // namespace DialogInject
