// chase_auto_pilot_history.h -- pulled-out narrative archive
//
// This file is NOT part of the build. It exists as a historical reference
// for the v0.15.9 -> v0.15.9.11.3.7 evolution of the X-ATM092 chase auto-
// pilot. The narrative was originally the top-of-file comment block in
// `chase_auto_pilot.cpp`; it grew large enough that the file as a whole
// crossed the 80 KB CI hard-fail threshold, so v0.16.1 split the source
// into `_state.inl` / `_route.inl` / `_io.inl` / `_helpers.inl` /
// `_diag.inl` / `_bridge.inl` / `_engage.inl` / `_update.inl` and moved
// the narrative here.
//
// Wrap in `#if 0 ... #endif` so even an accidental textual include in a
// translation unit doesn't pull any of it into the build. Read it as
// documentation only.

#pragma once

#if 0

// ============================================================================
// v0.15.9.x narrative archive (chronological -- newest at top)
// ============================================================================
//
// v0.15.9.6: Switch domt5_1 (west trail) from MODE_TARGET path-finding to
// MODE_DIRECTION dirX=+1, dirY=+1, walk=1 (WALK SOUTH-EAST). The previous
// MODE_TARGET path-finding with 28 waypoints, velocity-stuck recoveries,
// and CALIB overhead violated Aaron's recipe for this field. Aaron's
// recipe for domt5_1 (per 2026-05-11): walk straight through, head
// directly for next field exit, no hangups, no delay. The robot doesn't
// catch the party if walked straight through; running or delays cause
// mountain shake / catches.
//
// v0.15.9.5 BAT result (2026-05-11 18:11-18:14) summary:
//   domt4_1: 0 catches/3s (reproducible from v0.15.9.4)
//   domt3_2: 1 catch/5s (-1s from CALIB skip, catch is script-forced)
//   domt5_1: 3 catches/32s (UNCHANGED, biggest remaining problem)
//   Total chase: 2:09/7 catches
//   Three-signal pattern reproducible. Time to apply to domt5_1.
//
// Analysis on domt5_1 catches (from v0.15.9.5 BAT field log):
//   Catch #2 at 12s after entry, #3 at 18s, #4 at 25s -- catches fire
//   roughly every 6 seconds while party is on the field, regardless of
//   position. Kani entity reads as (0,0) static. This is a TIME-BASED
//   catch trigger, not a distance-based one. To get to 0 catches we
//   need field transit time under ~6 seconds. Current 32s = 3 catches
//   (each catch ~4s freeze + 6s timer interval).
//
// The previous MODE_TARGET approach (v0.15.9.2 through .5) violated all
// four of Aaron's requirements except walk: 28 waypoints (not direct),
// velocity-stuck pauses (hangups), CALIB delay (delay). Producing the
// observed 30+ seconds and 3 catches.
//
// MODE_DIRECTION walking south-east satisfies all four:
//   1. walk=true -- AI rule #1 (running shakes mountain) satisfied.
//   2. Direct: sustained analog toward south-east where the exit lives.
//   3. No hangups: no path-finder state machine.
//   4. No delay: skips CALIB.
//
// Risk: party may freeze at a wall. v0.15.9.1.1 BAT tried pure-south
// direction-drive and froze at (-769, 2217) -- the trail bends east at
// that point. South-east adds an east component to push around the bend.
// The CALIB-derived camera basis on this field is unreliable (script
// motion contaminates the measurement); south-east is the best heuristic.
// If south-east also freezes, v0.15.9.7 tries alternate directions
// empirically.
//
// Prediction: domt5_1 transit drops from 32s to under 18s. 0-1 catches.
// If under 6s, 0 catches. If 6-12s, 1 catch. Best case (Aaron's manual
// target): under 6s with 0 catches. The chase script's natural flow on
// this field plus our analog should produce running-speed-equivalent
// motion while still walking (per Finding #25's analog-as-force-multiplier
// observation), comfortably under the 6s threshold.
//
// v0.15.9.5: Explicit MODE_DIRECTION config for domt3_2 (RUN EAST).
// **BAT SUCCESSFUL 2026-05-11 18:11-18:14.** domt3_2 went from 6s/1 catch
// to 5s/1 catch (-1s from CALIB skip). Catch is script-forced co-location
// (cannot be eliminated without structural change). v0.15.9.4 result on
// domt4_1 reproduced: 0 catches/3s. Pre-BAT rationale follows:
// v0.15.9.4 BAT was a major success on domt4_1 (3 catches/16s -> 0 catches/3s)
// validated the three-signal direction-selection pattern. Refinement
// continues to the next chase field in chronological order: domt3_2.
//
// v0.15.9.4 BAT result (2026-05-11 17:27-17:30) summary:
//   domt4_1: 3 catches/16s -> 0 catches/3s (PRIMARY FIX)
//   domt5_1: 44s -> 30s (cascade: arriving in better shape)
//   domt2_1: ~1-2 catches -> 0 catches (cascade)
//   Total chase: 2:35/11 catches -> 2:06/7 catches
//   On domt4_1's first engaged second, party motion was dmag=905 in the
//   analog direction. Compare v0.15.9.3 (WEST) where the cleanest between-
//   catches sample was dmag=61. Pressing the right direction is ~15x faster
//   than pressing the wrong direction. The script's flow on these fields is
//   strong; cooperating multiplies our analog effectiveness dramatically.
//
// Three-signal analysis on domt3_2 from v0.15.9.4 BAT data:
//
//   (1) Camera orientation: default. CALIB on this field at 17:28:09 had
//       both phases fail ("no movement (dist=0.0)") but defaults were kept:
//       camRight=(1.000,0.000) camDown=(0.000,-1.000). With these defaults,
//       dirX=+1 = world east, dirY=+1 = world south.
//
//   (2) Kani approach: co-located with party at engage. First engaged tick
//       showed kani=(-289,-3513), party=(-211,-3461), kdist=93. The chase
//       script teleports both party and kani to the south end of the field
//       on entry, putting them in catch range immediately. No clean flee
//       direction from this signal -- kani is essentially on top of party.
//
//   (3) Field exit: east. Trigger line endpoints (-71,-3390) and
//       (-139,-3562), nearly vertical, center (-105,-3476). Party engage
//       position (-289,-3513) is well west of the line. After the post-
//       catch teleport, party position (-1358,-3439) is even further west.
//       Both states need east-only motion to cross the line. The trigger
//       line's verticality means north-south motion doesn't progress toward
//       crossing -- only east matters.
//
//   Sanity check (script choreography): on field entry the script teleports
//   party from (-367,1139) to (-289,-3513) -- mostly south, with negligible
//   east-west bias (+78 east). The script doesn't impose a preferred east-
//   west direction; we're free to push east without fighting it.
//
// Two of three signals point east (camera + exit); kani is null. Plus the
// CALIB phase 1 and 2 BOTH failed on this field in v0.15.9.4 -- MODE_TARGET
// wasted ~1 second on a calibration that produced no useful data. MODE_
// DIRECTION skips CALIB entirely, saving that overhead.
//
// v0.15.9.5 prediction: at minimum, ~1 second faster transit (CALIB removed).
// Possibly 0 catches instead of 1 if the party crosses the trigger line
// before the script forces the co-location catch. If catch still fires, the
// post-catch teleport puts party further west; direction-drive continues
// pushing east and party crosses the trigger line. Either way no slower
// than current behavior.
//
// Risk: very low. One new config-array entry. domt3_2's previous behavior
// (BuildFallbackConfig + INF-gateway/trigger-line fallback) was already
// doing essentially the same thing in MODE_TARGET; we're switching to a
// simpler implementation with the same intent.
//
// v0.15.9.4: domt4_1 direction config flipped from RUN WEST to RUN SOUTH-EAST.
// **BAT SUCCESSFUL 2026-05-11 17:27-17:30.** domt4_1 went from 3 catches/16s
// to 0 catches/3s. Three-signal direction-selection pattern validated. Total
// chase 2:06/7 catches (was 2:35/11 catches). Pre-BAT rationale:
// v0.15.9.3 BAT diagnostic data showed our previous WEST direction (inherited
// from Jegged's strategy guide at v0.15.9.1) was actively wrong for our
// build: WEST points toward the kani's approach column, away from the field
// exit, and perpendicular to the chase script's natural choreography.
//
// v0.15.9.3 BAT findings on domt4_1:
//   - Camera orientation is approximately default. Clean analog sample
//     between catches #2 and #3 (sec 9 post-engage) showed analog=(-1000,0)
//     producing world delta=(-61,+6) -- camRight ≈ (1,0), camDown ≈ (0,-1).
//     So dirX=+1 = world east, dirY=+1 = world south.
//   - Kani approaches from the north-west of the party at engage moment
//     (kani at (-1645,4154), party at (-657,3132); kani is -988 X, +1022 Y
//     relative). Flee direction is SOUTH-EAST.
//   - Field exit is south-east of spawn. Party ended at (-404,1117) having
//     started near (-1230,3699); net direction is SOUTH-EAST.
//   - The chase script's own intro animation moves the party south-east
//     during the first 2 seconds of chase activation. Our analog now
//     reinforces the script's flow instead of fighting it.
//
// v0.15.9.4 changes one config entry: domt4_1's dirX from -1 to +1, dirY
// from 0 to +1. No other changes. v0.15.9.3's pre-engage diagnostic and
// delta logging are retained -- they'll keep capturing data on this and
// subsequent fields as refinement continues down the chase route.
//
// Prediction: substantially fewer catches on domt4_1 (target 0-1 vs the
// 3 catches recorded in v0.15.9.2.18 and v0.15.9.3 BATs). Faster transit
// to domt3_2 (target <10s vs the 16s recorded).
//
// Risk: very low. One config-array entry edited. Behavior change scoped
// to one field. Easy to revert to WEST or try other directions if the BAT
// surprises us.
//
// v0.15.9.3: Diagnostic-only build for refinement work, starting with
// domt4_1 (chase start field). v0.15.9.2.18 BAT completed the full chase
// end-to-end with 11 NO-OPed catches (cap=0 band-aid). Aaron's framing:
// "our current battle nope system is a band-aid for poor navigation. We
// need to streamline navigation in each field." Refinement starts with
// domt4_1 since the chase starts there.
//
// v0.15.9.2.18 BAT analysis of domt4_1 (16 seconds total, 3 catches):
//   - At engage moment (16:26:41) the kani has already been running its
//     chase script for 17 seconds (since chase ACTIVATED at 16:26:25
//     while the player was reading/answering the ASK). By tick=60
//     (16:26:42) kani is at kdist=401 from party -- already within catch
//     range.
//   - First catch fires 1 second after engage. Each catch teleports the
//     party slightly south and freezes them for 4-5 seconds while the
//     kani repositions, then another catch fires.
//   - Despite analog=lX=-1000 (pure screen-left) the party drifts net
//     +710 east, -2157 south. The chase script appears to be overriding
//     our analog input -- our "RUN LEFT" config produces motion the
//     script chose, not motion we requested.
//
// Two open questions before designing a fix:
//   (Q1) Is the analog input doing ANYTHING on domt4_1, or is the chase
//        script the sole motion source? -> Look at delta-vs-analog. If
//        the party moves consistently in (a rotated version of) the
//        analog direction, input is being applied. If movement is
//        completely independent of analog (or zero), script overrides.
//   (Q2) What's the camera orientation on this field? -> Derive from the
//        delta. With lX=-1000 lY=0 known, the actual world delta gives
//        us camRight (its negative). Tells us whether "RUN LEFT" actually
//        points toward the field exit or somewhere else.
//
// v0.15.9.3 adds two diagnostics:
//   1. Pre-engage / chase-active per-second log. Fires once per second
//      while chase_active is true, regardless of whether chase_auto_pilot
//      is engaged. Captures party + kani positions and kdist from the
//      moment chase activates (entering the chase field) through the
//      ASK and into the engaged state. Lets us see where the kani is
//      and how it's moving DURING the ASK -- data we don't currently
//      have because the existing per-second log only fires when engaged.
//   2. Movement delta computation. Both the new pre-engage log and the
//      existing engaged tick log now include `delta=(dX,dY) dmag=N` --
//      the party position change since the last per-second snapshot.
//      This is what lets us answer Q1 above: if the party isn't moving
//      (dmag=0) while we're injecting analog, the chase script is in
//      control. If dmag is positive and consistent with the analog
//      direction (modulo camera rotation), input is being applied.
//
// Risk: very low. Pure additive logging. No behavior change. Two new
// module-static state variables (s_prevPosX/Y, s_prevPosValid) plus a
// counter (s_chaseActiveTickCounter). One new static helper function
// (LogChaseActiveDiagnostic). The existing per-second log in the engaged
// branch is left intact; the new pre-engage log adds delta computation
// to its output and the engaged log gets the same delta info appended.
//
// v0.15.9.2.8: Diagnostic-only -- log kani position and squall-kani distance
// every second to test Aaron's collision-push hypothesis (raised after the
// v0.15.9.2.7 BAT). v0.15.9.2.7 BAT showed: clean logs (spam gone), but the
// pattern at wp 13 on domt2_1 was IDENTICAL to v0.15.9.2.6 -- party walked
// 12 of 18 waypoints in ~2 seconds (19:54:41-19:54:43, ~700 units/sec, faster
// than calibration's measured walking pace), then froze at (683,8) with
// moveDist=0 for the entire rest of the BAT (~16 sec, ticks 408-1128). One
// transient moveDist=120 at tick=288, then dead silence. Crucially, NO
// "no-progress stuck" or "chase-drive: skipping wp" log lines appeared --
// the v0.15.9.2.5 advance-on-stuck mechanism is NOT firing on wp 13 on
// domt2_1, even though the conditions for it (sustained zero progress on
// a chase-drive target) clearly hold.
//
// Aaron's hypothesis after hearing the BAT: the kani's (X-ATM092 spider's)
// collision is what pushes Squall through chase fields. The auto-pilot's
// analog/keyboard input may have been doing nothing all along. No footsteps
// audible during the "working" fields supports this. The chase route would
// appear to make progress because the kani shoves Squall through screen
// transitions, not because the auto-pilot moves him.
//
// If true, this invalidates the entire chase-drive premise: every prior
// "success" (v0.15.9.2.5 west trail end-to-end, v0.15.9.2.4 wp 0-6 etc.)
// might have been collision-push, not input injection. The wp-13 stuck
// on domt2_1 would then be explained as: kani is somewhere out of pushing
// range (still on a previous field, or stuck behind a wall), so the only
// movement force is gone.
//
// v0.15.9.2.8 fix: add a ReadKaniPosition() helper that reads the kani's
// world position via ChaseDetector::GetKaniEntityPtr() (which already
// resolves the kani's runtime block at field-change time). The per-second
// diagnostic log now appends kani=(KX,KY) kdist=<squall-kani distance>
// to every status line. Three patterns to look for in the v0.15.9.2.8 BAT:
//   (A) kdist is consistently SMALL (<100) while wp progress happens,
//       and LARGE (>500) or kani=UNRESOLVED when stuck. -> Collision-push
//       confirmed. Auto-pilot input doing nothing. Rethink the approach
//       (perhaps script-pin kani's velocity to chase the party).
//   (B) kdist is roughly constant or varies independent of wp progress,
//       OR the party moves while kdist is large. -> Input injection works
//       at least sometimes. The wp-13 stuck has a different cause (likely
//       camera-unreachable funnel waypoint + advance-on-stuck guard bug).
//   (C) kani=UNRESOLVED on chase fields. -> ChaseDetector's slot resolution
//       broke, OR the kani is genuinely not a tracked entity on this field
//       (background entity vs others). v0.15.9.2.8 then refines kani slot
//       resolution.
//
// Risk: very low. Pure additive diagnostic. SEH-wrapped pointer read with
// null-check guard. No behavior change to engage/disengage/refresh paths.
//
// v0.15.9.2.7: Fix v0.15.9.2.6 log spam. v0.15.9.2.6 BAT result was an
// architectural success: the generic fallback fired on domt2_1 (chaseField=1,
// no explicit config), the path-finder computed 18 waypoints to (276,-727),
// and the party walked through 12 of them before getting stuck at wp 13.
// But BuildFallbackConfig() was called at the top of every Update() tick
// (~60Hz) because the per-tick refresh path ran AFTER the config lookup.
// Every call logged "fallback config built for field=...", flooding the
// field log with ~960 messages over 16 seconds of stuck-at-wp-13. Critical
// signal lost: any v0.15.9.2.5 advance-on-stuck "no-progress stuck" /
// "chase-drive: skipping wp" log lines were buried or possibly dropped.
//
// Fix: reorder Update() so the per-tick refresh path runs FIRST, using the
// cached s_engagedX state set by Engage() at fresh engagement. LookupConfig
// and BuildFallbackConfig only run on fresh engagement (new field or first
// engagement after disengage). The diagnostic log also reads s_engagedX
// instead of cfg->X so it doesn't need the config pointer. Net effect:
// BuildFallbackConfig's log line fires once per fresh engagement, not
// once per tick. After v0.15.9.2.7 the field log on domt2_1 should show
// one fallback message, then clean [drive] periodic logs interleaved with
// whatever stuck/skip/advance events v0.15.9.2.5's mechanism actually fires.
//
// v0.15.9.2.6: Generic chase-field engagement fallback. v0.15.9.2.5 BAT
// proved the west trail (domt5_1) walks end-to-end with the new chase-drive
// pipeline (kb-from-analog + advance-on-stuck). On field transition to
// domt2_1, the party stood still because chase_auto_pilot's per-field config
// table only knows about domt4_1 and domt5_1. v0.15.9.2.6 adds a fallback:
// when the engagement gate opens (chase field + auto mode + on field) but
// LookupConfig returns null, ask FieldNavigation::GetLargestClusterCenter()
// for the center of the largest dead-end cluster found at field load (the
// main corridor / south exit), then start MODE_TARGET chase-drive with
// walk=true and that target. The path-finder figures out the route.
//
// v0.15.9.2:  Per-field drive mode (DIRECTION vs TARGET). Adds path-finding
// support for fields where direction-drive hits walkmesh walls. v0.15.9.1.1
// BAT showed domt5_1 freezes the party at (-769, 2217) regardless of how
// fresh the keyboard injection is -- the corridor angles east of pure
// south, and constant-direction analog can't navigate it. domt5_1 now uses
// MODE_TARGET with a south-corridor target (cluster[2] center from the
// walkmesh dead-end scanner). Direction-mode path is preserved verbatim
// for fields that work with it (domt4_1's RUN LEFT cleared the field in
// one walking step in v0.15.9.1's BAT and doesn't need path-finding).
//
// v0.15.9.1.1: Cosmetic fix to per-second diagnostic logging. v0.15.9.1
// reset s_diagTickCounter to 0 BEFORE the Log::Field call, so every log
// line printed "tick=0" instead of the trigger value (60). The reset is
// now AFTER the log so the printed value matches the per-second cycle.
// Functional behavior unchanged.
//
// v0.15.9.1: Rewired to use FieldNavigation::StartDirectionDrive instead
// of standalone SendInput. v0.15.9 BAT proved keyboard alone wasn't
// enough -- the engine reads movement direction from the gamepad analog
// stick. FieldNavigation owns the fake-gamepad/analog-override plumbing
// (same path F9 path-finding uses); chase_auto_pilot owns the per-field
// config table.

#endif
