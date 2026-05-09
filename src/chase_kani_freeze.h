// chase_kani_freeze.h — Capture and (eventually) freeze kani's post-battle
// wakeup transition during the X-ATM092 chase.
//
// v0.15.2.3 (DIAGNOSTIC ONLY): Watches the kani entity's full byte block
// for ~5 seconds after every chase-field battle exit (game-mode 3 -> non-3
// transition while in a chase field). Logs the timing of every byte's
// FIRST change during the window, plus a final initial-vs-final summary.
// This output reveals the byte/word that controls the "incapacitated ->
// standing -> chasing" transition Aaron wants pinned.
//
// v0.15.2.4 (PLANNED): Replace the diagnostic with a per-frame hook that
// holds the identified byte at its "incapacitated" value until the player
// crosses to a different field. Field-scoped (re-entering the same field
// resets to vanilla because entity state is reinitialized on field load).
//
// This module replaces the v0.15.1 chase_battle_freeze module which hooked
// opcode_battle. v0.15.2.1 BAT confirmed that approach worked in domt4_1
// but not in domt2_1/domt5_1 because those fields use a different battle-
// entry code path. Hooking the wakeup transition sidesteps the battle-
// entry mystery entirely: regardless of how a kani battle starts, the
// engine returns to the field with the same wakeup state machine, so a
// single hook covers every chase field uniformly.

#pragma once

namespace ChaseKaniFreeze {

// One-time setup. Currently a no-op — all state is in the per-frame Update
// path. Kept for symmetry with sibling chase modules and in case v0.15.2.4
// needs hook installation here.
void Initialize();

// Cleanup. Currently a no-op.
void Shutdown();

// Per-tick driver. Detects the battle-exit edge and runs the byte-diff
// capture loop. Cheap when no capture is in progress (one memory read of
// pGameMode, one comparison).
void Update();

}  // namespace ChaseKaniFreeze
