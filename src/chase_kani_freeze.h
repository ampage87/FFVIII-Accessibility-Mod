// chase_kani_freeze.h -- Capture and freeze chase-agent post-battle wakeup
// transitions during the X-ATM092 chase.
//
// v0.15.3: SINGLE-PRONGED CLEANUP. The static kani+battleyarou pin was
// removed in v0.15.3 after v0.15.2.15 BAT confirmed the dynamic chase-agent
// pin handles every chase field that's not doopen2a (and chase_battle_freeze
// handles doopen2a via strcmp guard + BATTLE NO-OP). Three v0.15.2.x BATs in
// a row showed kani had 0-7 changed bytes and battleyarou had 0 across the
// chase fields tested -- both static pins were inert because the actual
// chase agents were rinoa-slot, director0, etc., not kani or battleyarou.
//
// The current design:
//
//   (1) Dynamic chase-agent pin: a public RegisterChaseAgent() entry point
//       called from chase_battle_freeze::Hook_opcode_battle on the first
//       PASS event in each chase field (except doopen2a). Resolves the
//       BATTLE caller's entityPtr to (arrayKind, slot, symIdx, symName),
//       logs a structured [CHASE-AGENT] line so per-field agent identity
//       is visible in any BAT, and arms a per-field full-state pin that
//       activates on the next StartCapture trigger. This is the entire
//       suppression mechanism in v0.15.3 (kani+battleyarou removed).
//
//   (2) Tightened deactivation (v0.15.2.14): the existing FREEZE DEACTIVATED
//       trigger keyed off the debounced field name, which left a ~2-second
//       window after fieldId flip during which the cached agent pointer was
//       still being written to even though the entity arrays had been freed
//       and the new field's entities were being allocated into the same
//       memory. v0.15.2.10 BAT crashed in dotown_3 ~16 seconds after the
//       doopen2a -> dotown_3 transition; v0.15.2.13 BAT recurred. v0.15.2.14
//       captures the raw fieldId at FREEZE ACTIVATED time and deactivates
//       IMMEDIATELY when pCurrentFieldId differs from that captured value,
//       before the name debounce settles.
//
//   (3) OTHERS-DIAG diagnostic scanner (v0.15.2.9, retained as diagnostic):
//       at StartCapture, snapshots all Others-array entities; at EndCapture,
//       logs per-slot byte-change counts. Useful for identifying chase
//       agents in fields where RegisterChaseAgent fails to resolve, and for
//       auditing entity-behavior drift in future BATs.
//
// v0.15.2.13: Caller-agnostic BATTLE NO-OP became the primary chase-
// suppression mechanism in chase_battle_freeze.cpp. v0.15.2.13 BAT confirmed
// it caps every chase field at one battle, but the experience was wrong:
// the actual chase agent was waking up and following Squall around silently
// while the BATTLE NO-OP suppressed combat. v0.15.2.14 pivoted to dynamic
// pinning as primary; BATTLE NO-OP is the safety net (the freeze# counter
// stays low when the pin is healthy).
//
// v0.15.2.4 - .12: Earlier byte-pin layers (anim ID trio, sub-state,
// position, full-state, dual-entity, all-Others scanner). All removed in
// v0.15.3 except the all-Others scanner (kept as diagnostic only). See
// chase_kani_freeze.cpp head for the v0.15.2.x narrative.

#pragma once

#include <cstdint>

namespace ChaseKaniFreeze {

// One-time setup. Resets state. Currently a no-op for hook installation;
// chase_battle_freeze owns the opcode_battle hook that drives this
// module via RegisterChaseAgent.
void Initialize();

// Cleanup. Resets state.
void Shutdown();

// Per-tick driver. Detects the battle-exit edge and runs the byte-diff
// capture loop, plus drives the per-frame ApplyFreezePin which holds the
// chase agent at "down on ground" until field exit. Cheap when no capture
// is in progress and no pin is active.
void Update();

// v0.15.2.14: Register the chase agent for the current field. Called from
// chase_battle_freeze::Hook_opcode_battle on the first PASS event per chase
// field (i.e. battleCount == 0 at hook entry, just before the engine runs
// the scripted opening encounter), EXCEPT in doopen2a where v0.15.2.15
// skips registration (the BATTLE NO-OP carries the load alone there because
// pinning the BATTLE caller would block the chase-end script).
//
// Resolves entityPtr to (arrayKind, slot, symIdx, symName) by walking
// pFieldStateOthers / pFieldStateBackgrounds with their respective strides
// (0x264 / 0x1B4) and the FieldArchive JSMCounts for the current field.
// On success, arms the chase-agent full-state pin: the next StartCapture
// (game mode 3 -> 1 transition) snapshots the agent's INITIAL state,
// ApplyFreezePin takes a full-state snapshot 1500ms in, and every
// subsequent frame writes the snapshot back over the agent's post-header
// region.
//
// Logs a structured [CHASE-AGENT] line with full identity on success, or
// [CHASE-AGENT-UNRESOLVED] on failure (entityPtr does not lie inside either
// array, or JSMCounts unavailable). On unresolved, the agent pin stays
// inactive for the field -- chase_battle_freeze's BATTLE NO-OP is the
// entire suppression layer in that case.
//
// Cleared on field change. Idempotent within a single field session
// (subsequent calls with the same entityPtr are no-ops).
//
// Called from the game thread; chase_kani_freeze::Update reads the
// resulting state from the mod thread. State writes are 32-bit aligned
// scalars on x86 (atomic) and the agent pointer is the last write, so no
// torn-state hazard.
void RegisterChaseAgent(uintptr_t entityPtr);

}  // namespace ChaseKaniFreeze
