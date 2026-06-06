// chase_auto_pilot.h -- Dollet/X-ATM092 chase auto-drive (MODE_AUTO)
//
// v0.15.9: New module. When ChaseDetector::GetChaseMode() == MODE_AUTO and
// the player is in a known chase field, this module drives the party
// toward the chase exit automatically.
//
// v0.15.9.1: Mechanism rewired. Original v0.15.9 used standalone SendInput
// to inject keyboard arrows + W. v0.15.9 BAT proved that wasn't enough --
// the engine reads movement direction from the gamepad analog stick
// (DIJOYSTATE2 lX/lY); keyboard is just a wake-up trigger. Per
// field_nav_autodrive.inl v05.85: "Keyboard injection is REQUIRED to
// activate the game's movement code path. Analog steering overrides the
// direction, but keyboard buttons are the trigger that makes the game
// process movement at all."
//
// v0.15.9.1 calls FieldNavigation::StartDirectionDrive(dirX, dirY, walk)
// which installs the same fake gamepad + analog override that F9 path-
// finding uses. chase_auto_pilot owns the per-field configuration table
// (which direction to drive on each chase field) and FieldNavigation owns
// the actual input-injection plumbing.
//
// Per-field configuration table maps chase field names to:
//   - dirX, dirY: screen-relative direction in {-1, 0, +1}
//                 dirX +1 = right, -1 = left
//                 dirY +1 = south, -1 = north
//   - walk:       true = walk (hold W); false = run
//
// Initial config in v0.15.9 (preserved in v0.15.9.1):
//   - domt4_1 ("Mountain Hideout 6", chase start / Selphie cliff): RUN LEFT
//     (per Jegged.com chase route research: "run immediately to the left
//     as quickly as possible. If you delay at all, you will have to fight
//     X-ATM092 again.")
//   - domt5_1 ("Mountain Hideout 7", west trail): WALK SOUTH
//     (per Jegged: "The next screen is a pathway leading south. Walk,
//     don't run, to the bottom of the screen." Also matches Aaron's AI
//     rule #1: running causes mountain shake / capture.)
//
// Other chase fields (domt1_1, domt2_1, domt3_2) are unconfigured in
// v0.15.9.1 -- chase_auto_pilot doesn't engage there, player drives
// manually. v0.15.9 BAT empirically captured the route domt4_1 ->
// domt3_2 -> domt5_1 -> ... -> doopen2a; v0.15.9.2+ will add the missing
// directions once we have BAT data confirming what works on each field.
// The bridge (doopen2a) gets its own state machine in v0.15.9.2 (PJUMPA
// hook detecting kani's two scripted leaps over the party, with reverse-
// direction state machine per Aaron's AI rule #2). v0.15.9.1 disengages
// on entering doopen2a so the player handles the bridge manually.
//
// Cap=0 in chase_battle_freeze (MODE_AUTO branch) is the safety net:
// any chase battle that does fire (e.g., from running on west trail
// before auto-pilot's W-press registers, or from any field we haven't
// configured) gets NO-OP'd, so the chase progresses regardless.
//
// Engagement gate (v0.15.9.1: THREE conditions, was four in v0.15.9):
//   1. ChaseDetector::IsInChaseField() -- in a known chase field
//   2. ChaseDetector::GetChaseMode() == MODE_AUTO
//   3. FF8Addresses::IsOnField() -- game mode 1 (not battle/menu/etc.)
//
// The v0.15.9 fourth condition (!ChaseAskOverlay::IsAskActive()) is
// REMOVED. v0.15.9 BAT showed it kept the auto-pilot off domt4_1 (the
// chase START field) for the entire ~37 seconds the chase ASK was open.
// FF8 already blocks input during ASK regardless, so the gate added no
// protection but did delay engagement. With the gate dropped,
// auto-pilot engages immediately on the chase START field; the engine
// queues our analog values until the ASK closes, then movement begins.
//
// On disengagement (any gate drops), all input held by direction-drive
// is released cleanly via FieldNavigation::StopDirectionDrive().
//
// DIAGNOSTIC LOGGING (v0.15.9.1)
//
// While engaged, chase_auto_pilot::Update logs a per-second status
// line so the BAT can verify movement is actually reaching the engine:
//
//   ChaseAutoPilot: tick=T field='X' dir=(dX,dY) walk=B pos=(pX,pY) lX=N lY=M
//
// Position is read from entity[0] (Squall, the lead in the chase) at
// offsets +0x190 (X*4096) and +0x194 (Y*4096). If pX/pY change tick to
// tick, movement IS reaching the engine; if they stay constant, the
// fake gamepad install isn't taking effect.

#pragma once

namespace ChaseAutoPilot {

void Initialize();
void Update();
void Shutdown();

// True while chase auto-pilot is currently engaged (driving the party
// via direction-drive).
bool IsEngaged();

}  // namespace ChaseAutoPilot
