// dialog_inject.h -- Mod-driven engine dialog injection (Phases 1 + 2a).
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
// Phase 2b (next ship -- v0.15.5.1 or v0.15.6) will layer on custom
// FF8 text encoding so we can pass a mod-composed prompt + options
// ("Manual / Auto / Original") and wire into chase_ask_overlay as
// the primary path.
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
//
// Hotkeys (per the F12 rule in userMemories: one diagnostic per
// physical key state):
//   - F12 alone   = Phase 1 MES test (slot 1, msg 0).
//   - Shift + F12 = Phase 2a ASK test (slot 2, msg 0, SP=6).

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

// Phase 2a test entry point (v0.15.5). Synthesizes a phantom
// script_context with SP=6 and calls opcode_ask(&ctx) targeting
// slot 2 with msg_id 0. The field's natural msg 0 must be an ASK
// for the choice cursor to render correctly; in doani1_2 (Dollet
// Comm Tower top, where Aaron BAT'd v0.15.4) this is the Selphie
// elevator ASK. Bound to Shift+F12.
void Phase2_TestAsk();

}  // namespace DialogInject

