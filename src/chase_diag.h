// chase_diag.h - Dollet / X-ATM092 chase scene diagnostic logger
//
// v0.15.0: New module. F12-toggleable diagnostic instrumentation that captures
// engine state during the Dollet escape and X-ATM092 chase. The data feeds the
// upcoming v0.15.1 manual-mode build (in-engine ASK proxy window + opcode_battle
// hook to cap chase battles at one per field).
//
// The deep research (Plan & Research Documents/X-ATM092 chase accessibility deep
// research results.md) confirmed:
//   - Var 530 at absolute address 0x01CFEB7E is the Dollet state bitmap
//     (bit 0x10 = "xatm first knock out" = chase-active signal).
//   - Var 84 at 0x01CFE9C0 is the place ID (Dollet places: 99 Comm Tower,
//     100 Mountain Hideout, 93 Town Square, 94 Lapin Beach).
//   - X-ATM092's field entity is named "kani" in SYM data.
//   - Field short names come from FF8Addresses::pCurrentFieldName.
//
// The diag captures, on F12 toggle, six log streams:
//   [CHASE-DIAG-FIELD]   on every field transition - id, name, place, var530,
//                        full entity dump, SYM names with kani slot flagged
//   [CHASE-DIAG-VAR530]  per-frame poll, log only on change with bit deltas
//   [CHASE-DIAG-PLACE]   per-frame poll of var 84, log only on change
//   [CHASE-DIAG-BATTLE]  enriches existing battle logs with chase-diag tag
//   [CHASE-DIAG-KANI]    when kani is in the field, log its state changes
//   [CHASE-DIAG-FRAME]   heartbeat every 5 seconds with player/world summary
//
// All logging goes to ff8_field.log (and ff8_battle.log for the BATTLE stream).
//
// Usage:
//   ChaseDiag::Initialize();          // once, at AccessibilityThread start
//   ChaseDiag::Update();              // once per tick from AccessibilityThread
//   ChaseDiag::Toggle();              // called from dinput8.cpp F12 handler
//   bool on = ChaseDiag::IsEnabled(); // for other modules to check (unused
//                                     // in v0.15.0; reserved for v0.15.x)
//   ChaseDiag::Shutdown();            // at thread exit

#pragma once

#include <cstdint>

namespace ChaseDiag {

// One-time setup. Resolves the few pointers the diag needs (savemap base via
// FF8Addresses convenience accessors that already exist) and snapshots the
// initial state so the first Update() doesn't burst-log spurious "changes."
void Initialize();

// Cleanup. Currently a no-op; kept for symmetry with other modules.
void Shutdown();

// Per-tick driver. Cheap no-op when disabled. When enabled, performs the
// per-frame polls (var 530, var 84, kani state, dialog window changes) and
// emits log streams as appropriate.
void Update();

// F12 handler. Flips the enable bit, snapshots fresh baselines, announces
// "Chase diagnostic enabled" / "Chase diagnostic disabled" via TTS so the
// developer knows the toggle landed.
void Toggle();

// Whether chase-diag is currently active. Cheap inline accessor.
bool IsEnabled();

// v0.15.1: Called from field_dialog's Hook_opcode_ask / Hook_opcode_aask
// every time an ASK or AASK opcode fires. When chase-diag is enabled, we
// snapshot all 8 ff8_win_obj slots so chase_ask_overlay can mine real
// engine-set template values for its proxy ASK in v0.15.2. No-op when
// chase-diag is disabled (keeps the call-site cost negligible).
void OnAskOpcodeFired(const char* opcodeLabel);

}  // namespace ChaseDiag
