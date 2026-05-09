// chase_battle_freeze.h — chase BATTLE opcode safety net + agent identifier.
//
// v0.15.2.15: Skip the dynamic chase-agent pin in doopen2a only. The
// v0.15.2.14 BAT showed that the BATTLE caller in doopen2a (Others
// slot 4, 'director0') doubles as the chase-progress director --
// pinning its full state for the rest of the field session prevents
// the chase-end script from triggering the transition to dotown_3.
// Symptom: doopen2a's one chase battle resolves, then the field
// freezes (music keeps playing) and dotown_3 is never entered.
// Surgical fix: PASS branch now wraps the RegisterChaseAgent call in
// a strcmp against "doopen2a". The BATTLE NO-OP (cap at 1) still runs
// there and is sufficient because doopen2a has exactly one chase
// battle in the whole sequence. Other chase fields (domt1_1..domt5_1)
// keep the dynamic pin -- they don't share the "agent doubles as
// director" pattern and worked perfectly with the pin in v0.15.2.14.
// The static kani+battleyarou pin in chase_kani_freeze still runs in
// doopen2a but is inert there (both entities are dormant per the
// v0.15.2.14 OTHERS-DIAG: kani 7 changed bytes, battleyarou 0).
//
// v0.15.2.14: Repurposed from primary suppression (v0.15.2.13) to
// secondary safety net + chase-agent identifier. The primary chase
// suppression in v0.15.2.14 is the dynamic agent pin in chase_kani_freeze;
// this hook now (a) hands the BATTLE caller's entity pointer to
// chase_kani_freeze on the first PASS per chase field so the pin can
// target the right entity, and (b) keeps the v0.15.2.13 NO-OP behavior
// as a fallback for any chase BATTLE call that slips past the pin.
//
// In a healthy v0.15.2.14 run the pin holds the agent on the ground and
// the NO-OP fallback fires zero or rarely. The freeze# counter logged
// on each NO-OP becomes a real diagnostic: low = pin healthy, high =
// pin missing the agent and safety net carrying the load.
//
// Background — what the v0.15.2.13 BAT proved (kept here for the audit
// trail since v0.15.2.13 was never pushed):
//
//   The caller-agnostic NO-OP traversed the entire chase end-to-end
//   (six PASS + eight NO-OP across 14 BATTLE calls in 6 chase fields)
//   but the agent in domt5_1 (rinoa-slot wearing kani's robot model)
//   was waking up and following Squall around silently while combat
//   was suppressed. Aaron's design preference: pin the agent down so
//   it stays incapacitated; use the BATTLE NO-OP as a safety net.
//
//   The same BAT crashed in dotown_3 ~16 seconds after the doopen2a
//   handoff. v0.15.2.14 fixes that with tightened deactivation in
//   chase_kani_freeze (raw fieldId check, deactivates before the 2s
//   name-debounce settles).

#pragma once

namespace ChaseBattleFreeze {

// One-time setup. Resolves opcode_battle (pExecuteOpcodeTable[0x69])
// and installs a MinHook detour. Idempotent.
void Initialize();

// Cleanup. Disables the hook.
void Shutdown();

}  // namespace ChaseBattleFreeze
