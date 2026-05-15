// chase_auto_pilot.cpp -- Dollet/X-ATM092 chase auto-drive
// See chase_auto_pilot.h for design notes.
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

#include "chase_auto_pilot.h"
#include "chase_ask_overlay.h"
#include "chase_detector.h"
#include "chase_keyboard.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_navigation.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ChaseAutoPilot {

// ============================================================================
// Per-field configuration
// ============================================================================
//
// Two drive modes per field, picked based on what the field's geometry
// allows:
//
//   MODE_DIRECTION : constant screen-relative analog direction held for
//                    the entire engagement. dirX/dirY are screen-relative
//                    direction signs in {-1, 0, +1} (DirectInput axis
//                    convention from field_nav_input_hooks.inl: dirX +1 =
//                    right, dirY +1 = south). Use for short, straight
//                    corridors where one walking step covers the field.
//                    Implementation: FieldNavigation::StartDirectionDrive.
//
//   MODE_TARGET    : path-finding drive toward an explicit (targetX,
//                    targetY) world coordinate. The F9 A*+funnel path-
//                    finder computes a walkmesh-aware route, handles
//                    narrow segments, dead-ends, and stuck recovery.
//                    Use for fields with bends, narrow corridors, or
//                    geometry where straight-line analog can't make it.
//                    Implementation: FieldNavigation::StartChaseDrive.
//
// walk = true holds W (FF8 PC default cancel = walk modifier on foot)
// for both modes -- needed on Aaron's AI-rule fields like domt5_1 where
// running shakes the mountain and the party gets caught.

enum FieldDriveMode {
    MODE_DIRECTION        = 0,
    MODE_TARGET           = 1,
    MODE_STAGED_DIRECTION = 2,  // v0.15.9.7
    MODE_BRIDGE_DANCE     = 3,  // v0.15.9.8.3 -- domt1_1 only
};

// v0.15.9.7: Stage descriptor for MODE_STAGED_DIRECTION. Stages let a single
// engagement switch direction part-way through the field based on the
// party's current Y position. Used for S-shaped trails like domt5_1 where
// one constant direction can't navigate the geometry (the trail bends
// from SW to S to SE, so the analog input has to follow).
//
// Stages are listed in DECREASING activeMinY order. PickStage walks the
// array and returns the first stage whose activeMinY is <= the party's
// current Y. The last stage MUST have activeMinY = INT32_MIN so it always
// matches as a fallback.
//
// Mid-engagement transitions are handled by FieldNavigation::StartDirectionDrive's
// "already running" branch (see field_nav_directiondrive.inl): when called
// with a new (dirX, dirY, walk), it updates s_analogDesiredLX/LY in place
// and fires diff-based KEYUP/KEYDOWN events only when the arrow bitmask
// actually changes. So changing s_engagedDirX/Y/Walk between Update ticks
// is enough -- the existing per-tick refresh call picks up the new values.
struct FieldStage {
    int8_t  dirX;        // screen-relative direction sign, {-1, 0, +1}
    int8_t  dirY;        // screen-relative direction sign, {-1, 0, +1}
    bool    walk;        // hold W (walk modifier) during this stage
    int32_t activeMinY;  // stage is active when party Y >= this value
};

struct FieldConfig {
    const char*     fieldName;
    FieldDriveMode  mode;
    int8_t          dirX;       // MODE_DIRECTION: screen-relative direction sign
    int8_t          dirY;       // MODE_DIRECTION: screen-relative direction sign
    int32_t         targetX;    // MODE_TARGET: walkmesh world X
    int32_t         targetY;    // MODE_TARGET: walkmesh world Y
    bool            walk;
    // v0.15.9.7: MODE_STAGED_DIRECTION fields. Null/0 for other modes.
    const FieldStage* stages;
    int               stageCount;
};

// Per-field auto-drive configuration. v0.15.9.2 adds MODE_TARGET to
// domt5_1 with a south-corridor target. v0.15.9.1 had only MODE_DIRECTION.
//
// v0.15.9.1.1 BAT empirical findings:
//   - domt4_1 (chase START, RUN LEFT): direction-drive cleared the field
//     in one walking step. Geometry is short. Keep MODE_DIRECTION.
//   - domt5_1 (west trail, WALK SOUTH): direction-drive hit a wall at
//     (-769, 2217) -- the corridor angles east of pure south. Need path-
//     finding to navigate. Switch to MODE_TARGET. Target chosen from the
//     walkmesh dead-end scanner: cluster[2] center=(382, 235) is the
//     largest cluster (14 narrow tris) in the south of the walkmesh,
//     likely the field-exit corridor.
//
// domt3_2 (between MH-6 and MH-7) is still unconfigured -- v0.15.9.2+
// will fill it in based on next BAT log evidence.
//
// v0.15.9.2.17 BAT empirical findings (from v0.15.9.2.16 BAT 15:21-15:24):
//   - dotown_2 (Town Square 8, post-FMV): generic INF-gateway fallback
//     picked the south-exit gateway center (-198, -600) as target, but
//     A* could not find a path -- the walkmesh is fragmented into
//     disconnected islands (player spawn on tri 45, gateway target on
//     tri 137, trigger-line bridge on tri 77, all separate). chase-drive
//     STARTED with waypoints=0; CALIB moved the player ~500 units, the
//     arrival check fired trivially (no waypoints to traverse), and the
//     v0.15.9.2.11 completion marker prevented re-engagement. Auto-pilot
//     permanently disengaged for this field. Aaron drove dotown_2
//     manually for 39 seconds.
//     Root cause: Dollet town chase fields use SETLINE-triggered scripted
//     animations to teleport the party between street segments. The
//     walkmesh polygons aren't connected; A*'s view of the field is
//     incomplete. Path-finding fundamentally can't solve this.
//     Fix: explicit MODE_DIRECTION config that pushes the party south.
//     Calibration data from the BAT: camRight=(0.957,0.291),
//     camDown=(0.291,-0.957). Screen-down (dirY=+1) maps to world south
//     in the +X/-Y quadrant, which is the direction from spawn
//     (-1642, 4518) toward the gateway (-198, -600). The party walks
//     into the next trigger line, the SETLINE script teleports them to
//     the next segment, repeat until field exit. Running (walk=false)
//     since no AI-rule slowdown applies to this field.
//
// v0.15.9.2.18 BAT empirical findings (from v0.15.9.2.17 BAT 16:11-16:14):
//   - dotown_2: MODE_DIRECTION south worked beautifully. Party moved
//     900 units/sec south, cleared the field in ~14 seconds (vs 41
//     seconds manually). v0.15.9.2.18 keeps this config unchanged.
//   - dotown_1 (Town Square 6, last chase field before chase climax
//     FMV disc00_07h.avi): same problem as dotown_2 but in a different
//     guise. A* found 65 valid waypoints (walkmesh connected), but
//     CALIB phase 1 FAILED on field entry (no movement during the
//     24-tick lX=+1000 test). Without proper camera axes, the
//     path-finding drive's analog steering was approximate. The party
//     moved ~3300 units in the correct general direction (south +
//     west from spawn) but then drifted off-course at ~(89, 2297)
//     and cycled velocity-stuck / wp-skipping for 24 seconds before
//     eventually drifting into the FMV trigger zone. Aaron's exact
//     observation: 'got right up to the trigger line to the beach
//     fmv, then stopped for some reason before actually triggering it.'
//     Same root cause as dotown_2 (chase scene scripts dominate;
//     path-finding can't track) but presents as 'stuck near trigger'
//     rather than 'never engages'. The CALIB failure is intrinsic to
//     dotown_1's first-second state -- not Aaron's manual input.
//     Fix: same approach as dotown_2 -- explicit MODE_DIRECTION south
//     bypasses CALIB entirely. The party walks continuously south;
//     SETLINE-triggered chase scripts route the actual movement;
//     party reaches the FMV trigger naturally.
// v0.15.9.7: Stage table for domt5_1 (west trail). S-shaped trail requires
// three sequenced directions. See the domt5_1 entry comment in kFieldConfigs
// below for the full rationale and threshold derivation.
static const FieldStage kStages_domt5_1[] = {
    // Stage 0: Y > 2200 -- WALK SOUTH-WEST. Spawn at Y~3300; trail bends
    // from the spawn cluster (Y~3500) down through the first bend toward
    // SETLINE call#13 center (-737, 1597). Aaron's recipe: "very southwest
    // initially" and "if you press down at first you won't move, you have
    // to press both down and left." The v0.15.9.1.1 BAT showed pure south
    // froze at Y=2217 -- SW must extend past that point.
    { -1, +1, true, 2200 },
    // Stage 1: 1100 < Y <= 2200 -- WALK SOUTH. Middle straight section
    // between the SW bend and the final SE bend. Pure south works through
    // this segment.
    {  0, +1, true, 1100 },
    // Stage 2: Y <= 1100 -- WALK SOUTH-EAST. Final approach to the south-
    // east exit. SETLINE call#14 center (-366, 1110) marks this transition;
    // the trail bends east toward the south exit cluster (Y~235).
    { +1, +1, true, INT32_MIN },
};
static const int kStages_domt5_1_count =
    (int)(sizeof(kStages_domt5_1) / sizeof(kStages_domt5_1[0]));

static const FieldConfig kFieldConfigs[] = {
    // domt4_1 = Mountain Hideout 6 (chase start, Selphie cliff).
    // v0.15.9.4: RUN SOUTH-EAST (was RUN WEST per Jegged's guide, but
    // Jegged was wrong for our build, or wrong period). v0.15.9.3 BAT
    // diagnostic data showed three independent signals all pointing to
    // south-east as the correct flee direction:
    //   (1) Camera orientation is approximately default: camRight=(1,0),
    //       camDown=(0,-1). Clean analog-effect sample at engaged sec 9
    //       (between catches #2 and #3): pos went from (-366,1365) to
    //       (-427,1371) with analog=(-1000,0) -- delta=(-61,+6), pointing
    //       mostly west with negligible north component. Confirms default
    //       camera mapping with no significant rotation.
    //   (2) Kani approaches from the north-west. At engage moment kani
    //       was at (-1645,4154), party at (-657,3132) -- kani is at
    //       -988 X, +1022 Y relative to party (north-west). Direction
    //       to flee = +X, -Y = world east-south = SOUTH-EAST.
    //   (3) Field exit is south-east of spawn. Party ended at (-404,1117)
    //       at field-transition moment, having started near (-1230,3699).
    //       Net direction to exit: +826 east, -2582 south = mostly south,
    //       with east component. SOUTH-EAST again.
    //   Additionally: pre-engage data showed the chase script's own intro
    //   animation moves the party south-east during seconds 1-2 of chase
    //   activation (party walked from spawn to (-657,3132) -- a delta of
    //   roughly +573 east, -567 south). Our analog now COOPERATES with
    //   the script's natural choreography instead of fighting it.
    //
    // The previous WEST config (v0.15.9.1 through v0.15.9.3, Jegged-derived)
    // was the worst possible direction: toward the kani's column, away
    // from the field exit, and perpendicular to the script's natural flow.
    // v0.15.9.2.18 BAT recorded 3 catches on this field in 16 seconds.
    // v0.15.9.4 prediction: substantially fewer catches (target 0-1) and
    // faster field transit.
    { "domt4_1", MODE_DIRECTION,
      /*dirX=*/+1, /*dirY=*/+1,
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false,
      /*stages=*/nullptr, /*stageCount=*/0 },
    // domt3_2 = Mountain Hideout 3 (chase field 2, immediately after domt4_1).
    // v0.15.9.5: RUN EAST. Three-signal analysis on v0.15.9.4 BAT data:
    //   (1) Camera default: CALIB on this field had both phases fail (no
    //       movement during the calibration window), defaults kept --
    //       camRight=(1,0), camDown=(0,-1). dirX=+1 = world east.
    //   (2) Kani co-located with party at engage (kdist=93 at first engaged
    //       tick). The chase script teleports party from (-367,1139) to
    //       (-289,-3513) on field entry, spawning kani at the same place.
    //       No clean flee direction from this signal.
    //   (3) Field exit: east. Trigger line endpoints (-71,-3390) and
    //       (-139,-3562), nearly vertical, center (-105,-3476). Party at
    //       engage (-289,-3513) is well west; after post-catch teleport at
    //       (-1358,-3439) is even further west. East-only motion crosses
    //       the line from both states; north-south motion doesn't help.
    // Plus: v0.15.9.4 MODE_TARGET path on this field had CALIB phase 1 +
    // phase 2 BOTH fail with dist=0.0 -- wasted ~1 second on calibration
    // that produced no useful data. MODE_DIRECTION skips CALIB entirely.
    // The previous fallback path (BuildFallbackConfig -> trigger-line
    // target (-105,-3476)) was already aiming the path-finder at the same
    // direction; we're switching to a simpler implementation with the same
    // intent. Catch on this field may still fire (kani co-location is
    // outside our control) but transit should be faster.
    // v0.15.9.7.4: dirX flipped from +1 (east) to -1 (west). Aaron's
    // 2026-05-11 clarification on the chase route directions: domt3_2 should
    // run "west, northwest, west" -- not east as v0.15.9.5 had it. The
    // v0.15.9.5 BAT "success" on this field (5s / 1 catch) was actually a
    // direction-conflict catch firing in 5 seconds rather than the
    // "script-forced co-location" we attributed it to in the comments at
    // the time. Aaron's manual trace on this field (2026-05-11 20:14-20:20):
    // 2 seconds of travel, ~851 world-units west, no catches. He pressed
    // west; we were pressing east.
    //
    // For domt3_2 the world-coord camera is approximately default (the
    // v0.15.9.4 three-signal CALIB data showed camRight=(1,0) camDown=(0,-1)
    // for this field). So dirX=-1 (screen-left) maps to world-west.
    // Aaron's manual went from (-616,-3351) to (-1467,-3385), i.e.
    // world-west, matching the screen-left input.
    //
    // Aaron's full recipe is "west, northwest, west" -- a three-stage
    // pattern. For v0.15.9.7.4 we ship single MODE_DIRECTION west to fix
    // the gross direction error; if the NW middle stage matters for
    // efficiency or catch avoidance, v0.15.9.7.5 adds a staged direction
    // table here too. The field is short (~2s in Aaron's manual) so
    // single direction likely suffices.
    //
    // The previous v0.15.9.5 rationale (run east toward the trigger line
    // at (-105,-3476)) was based on the trigger-line position alone
    // without consulting Aaron's actual play. Lesson: the trigger line
    // tells us where the geometric exit is, but the chase script may
    // advance the field on a different condition (e.g., reaching a west-
    // side INF gateway, or a time-based script advance). The player's
    // actual recipe is the only ground truth. See Finding #30 in the
    // lessons doc.
    { "domt3_2", MODE_DIRECTION,
      /*dirX=*/-1, /*dirY=*/ 0,
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false,
      /*stages=*/nullptr, /*stageCount=*/0 },
    // domt5_1 = Mountain Hideout 7 (west trail). MODE_STAGED_DIRECTION:
    // southwest -> south -> southeast, walking throughout.
    //
    // v0.15.9.7.4: REVERT the v0.15.9.7.3 Y-axis-flip back to the v0.15.9.7.2
    // staged config. Aaron's 2026-05-11 clarification confirmed that the
    // west trail recipe is "generally southwest, south, southeast" -- exactly
    // what kStages_domt5_1[] encodes (SW=(-1,+1) -> S=(0,+1) -> SE=(+1,+1)).
    //
    // v0.15.9.7.3's interpretation of "LEFT and slightly UP" as screen-up-left
    // (dirY=-1) was a translation error. "Up" in that earlier message referred
    // to position-on-the-trail (toward the spawn end / upper part of the trail),
    // not screen-direction. The BAT result was definitive: dirY=-1 stuck the
    // party at spawn with the AD unable to make progress, while dirY=+1
    // (v0.15.9.7, .7.1, .7.2) reliably navigated the field every time.
    //
    // The v0.15.9.7.4 restoration of staged SW->S->SE means we go back to the
    // pre-fix problem: party walks correctly (period=1 W re-press from
    // v0.15.9.7.2 confirmed working) but the robot still triggers. That
    // remains to be solved. Hypothesis worth testing first: Aaron raised
    // "did you check if the prior field is carrying over" -- the v0.15.9.5
    // domt3_2 config was running east when Aaron presses west on that field.
    // If the engine carries some state (kani aggression timer, script chase
    // mode flag, etc.) from one chase field to the next, fighting the script
    // direction on domt3_2 might be priming the catch on domt5_1. v0.15.9.7.4
    // also fixes domt3_2 to go west (matching Aaron's recipe). If the
    // domt5_1 catch disappears once domt3_2 is fixed, carryover was the cause.
    //
    // === Previous attempts on this field ===
    //
    // v0.15.9.7.3 (MODE_DIRECTION -1,-1): stuck at spawn. The Y direction was
    // wrong; the analog opposed the script's down-trail flee force enough that
    // the walkmesh couldn't push the party past the spawn wall. Reverted here.
    //
    // v0.15.9.7 / .7.1 / .7.2 (MODE_STAGED_DIRECTION, SW->S->SE): geometry
    // navigated. Party reaches exit cleanly. v0.15.9.7.1 added a defensive
    // W re-press at period=30 to land the walk modifier despite chase-script
    // input swallow. v0.15.9.7.2 reduced period to 1 and Aaron confirmed
    // walking audio from the first frame. But the robot still triggered,
    // which we now think may be a carryover effect from domt3_2 going east.
    //
    // v0.15.9.6 (MODE_DIRECTION +1,+1): stuck on east wall at spawn. Both
    // axes wrong; party never moved more than ~30 units before freezing.
    //
    // v0.15.9.2 through .5 (MODE_TARGET path-finding to (382,235)): 28-
    // waypoint path with velocity-stuck recoveries and CALIB delay. 30+
    // seconds and 3 catches. Violated all of Aaron's recipe requirements
    // except walk.
    //
    // === What v0.15.9.7.4 expects ===
    //
    // Party walks SW->S->SE through the field. Aaron's manual transit on
    // this field at walking speed: 13 seconds, 0 catches. Target: same or
    // better. WALK_REPRESS_PERIOD=1 from v0.15.9.7.2 still defends against
    // any W swallow on field-load. The combined fix to domt3_2 (running west
    // instead of east) tests the carryover hypothesis: if catches on domt5_1
    // go to 0, carryover was the issue. If catches still fire, we have a
    // different problem to solve and the v0.15.9.7.5 investigation looks at
    // whether SendInput vs physical-key inputs are read differently by the
    // catch trigger.
    //
    // === Old v0.15.9.7.3 commentary kept below for context ===
    //
    // The pre-v0.15.9.7.4 reasoning thought Aaron's "LEFT and slightly UP"
    // was screen-relative-up, which made:
    // (dirX=-1, dirY=-1) = screen-up-left in the DirectInput convention from
    // field_nav_input_hooks.inl.
    //
    // The Y-axis interpretation was wrong (v0.15.9.7.3 BAT proved it: stuck).
    // Aaron's 2026-05-11 clarification: the west trail recipe is
    // southwest -> south -> southeast (down+left, then down, then down+right),
    // which is the v0.15.9.7 staged config. (Restored above.)
    //
    { "domt5_1", MODE_STAGED_DIRECTION,
      /*dirX=*/-1, /*dirY=*/+1,  // initial fallback direction (matches stage 0)
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/true,
      /*stages=*/kStages_domt5_1, /*stageCount=*/kStages_domt5_1_count },
    // domt1_1 = Dollet bridge. Chase route order: domt5_1 -> domt2_1 (fallback)
    // -> domt1_1 -> doopen2a. v0.15.9.8.3 introduces MODE_BRIDGE_DANCE with an
    // interactive EAST/WEST state machine driven by the kani's X-velocity.
    //
    // The mechanic (per Aaron, 2026-05-12): party starts east. Robot leaps
    // east, vaulting over the party and landing far ahead on the corridor.
    // Catch evaluator fires if party keeps running east into the blocking
    // robot (consistent X~2053 in v0.15.9.7.8 / .8 / .8.1 / .8.2 BATs). Party
    // must turn west after the robot LANDS. Robot then leaps again (presumed
    // westward); party must turn east the moment that second leap STARTS, so
    // we slip past while the robot is airborne.
    //
    // v0.15.9.8.2 BAT BridgeDiag (416 samples / 6.7 seconds, ChaseDetector
    // override to Others slot 3 SYM='laguna' shipped in v0.15.9.8.3)
    // characterized the signals:
    //   * Y is constant (-446) -- jumps are X-axis only in walkmesh coords.
    //   * Normal pursuit: ~106 units / 100ms tick.
    //   * Leap: ~372 units / 100ms tick (3.5x normal).
    //   * Landed/stationary: 0-1 units / tick.
    //
    // Thresholds (kBridgeLeapThreshold=200, kBridgeLandThreshold=50,
    // kBridgeLandConsec=2, kBridgeMinDwellTicks=60, kBridgeWestTimeoutTicks=300)
    // are documented inline above. The west-leg timeout is the safety net for
    // the empirically-unknown west-leg behavior: if no second leap fires, we
    // bail east after 5 seconds so the bridge progresses rather than getting
    // stuck retreating forever.
    //
    // The dirX/dirY fields below are the INITIAL direction-drive analog used
    // at engagement (east, running). They are subsequently overwritten by
    // UpdateBridgeDance() as the state machine fires.
    { "domt1_1", MODE_BRIDGE_DANCE,
      /*dirX=*/+1, /*dirY=*/0,
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false,
      /*stages=*/nullptr, /*stageCount=*/0 },
    // doopen2a = Dollet Town Square (immediately after the bridge domt1_1).
    // v0.15.9.8: Explicit MODE_TARGET south. v0.15.9.7.8 BAT showed the
    // generic fallback picked the WRONG trigger line on this field:
    // GetTriggerLineNearestCluster matched the NW trigger (idx=1, center
    // (-1390, 854)) because the walkmesh's largest dead-end cluster is at
    // (-1315, 503) -- an interior corner, not an exit. The actual exit is
    // the SOUTH trigger line (idx=257, center (-952, -3703)). With NW as
    // the target, the party briefly moved north, the catch evaluator fired
    // at 22:22:10 producing a post-catch script teleport south to
    // (-1042, -917), and the auto-pilot stuck there (A* path stale) until
    // the chase script forced a field transition at 22:22:21. Two catches
    // in 13 seconds of stuck movement.
    //
    // doopen2a has ZERO INF gateways (per `INF parsed: 0 active gateways
    // for 'doopen2a'`), so tier-1 of the BuildFallbackConfig fallback chain
    // is unavailable. Two SETLINE trigger lines exist on the field:
    //
    //   idx=1   line(-1432, 660) -> (-1349, 1048)  center=(-1390, 854)   NW
    //   idx=257 line(-814, -3717) -> (-1091, -3689) center=(-952, -3703)  S
    //
    // The cluster-direction heuristic picks the trigger nearest to the
    // walkmesh's largest dead-end. On this field the largest cluster is
    // an interior corner in the NW, not an exit -- so the heuristic
    // mis-selects the NW trigger. This is a general pattern: any field
    // where the biggest walkmesh feature isn't the exit will trip the
    // heuristic. The fix is per-field explicit config; the deeper fix
    // (improving the heuristic, e.g., scoring by distance-from-spawn or
    // by INF destination-field IDs) is deferred.
    //
    // Aaron's authoritative recipe (2026-05-12): "The Town Square does
    // require the party to keep running. You mostly head down from where
    // you enter the field in order to get to the next." Party spawns at
    // ~(-784, -474), must run south to cross the trigger line at
    // y ~= -3700. Running (walk=false) -- no AI rule restricts pace on
    // this field; the town square is at top speed like the bridge before
    // it and the streets after.
    //
    // Target chosen as the south trigger line's center (-952, -3703).
    // The path-finding drive's point-distance arrival radius is small
    // enough (~60 units per v0.15.9.2.13) that the party crosses the
    // line at y ~= -3700 during travel even if they stop short of the
    // target proper. If a future BAT shows the party arriving at the
    // target without crossing, bump the targetY south of the line
    // (e.g., -3800) to force crossing.
    //
    // Predicted v0.15.9.8 BAT outcome: 0 catches on doopen2a, transit
    // 5-8 seconds (similar to dotown_2 / dotown_1 south-running fields).
    // Field transitions cleanly to dotown_3 via south trigger crossing.
    //
    // Lesson (will be Finding #34 in the lessons doc): the
    // GetTriggerLineNearestCluster heuristic in chase auto-pilot's
    // BuildFallbackConfig assumes the walkmesh's largest dead-end cluster
    // correlates with the field exit direction. That assumption holds on
    // many fields (corridors, narrow trails) but breaks on fields with
    // interior architectural features (town squares, plazas, building
    // corners) where the biggest walkmesh dead-end is a non-exit. The
    // safety valve is per-field explicit configs; the heuristic itself
    // should ideally score by direction from spawn rather than nearest-
    // to-cluster, but that fix is deferred since explicit configs are
    // surgical enough for the remaining problematic fields.
    //
    // v0.15.9.8.1: targetY bumped from -3703 to -3800. v0.15.9.8 BAT showed
    // the south-target fix worked structurally -- party ran 3300+ units south
    // in 6 seconds with no NW detour -- but A* stopped 159 units short of the
    // trigger line at pos=(-958,-3541) and sat there for 8 seconds before the
    // chase script's catch evaluator fired once and forced a transition. By
    // targeting -3800 (97 units past the trigger line at y ~= -3700), A*'s
    // last reachable waypoint should fall closer to or past the line itself,
    // letting the party actually cross and trigger the script-side field
    // transition before the catch evaluator window expires.
    { "doopen2a", MODE_TARGET,
      /*dirX=*/0, /*dirY=*/0,
      /*targetX=*/-952, /*targetY=*/-3800,
      /*walk=*/false,
      /*stages=*/nullptr, /*stageCount=*/0 },
    // dotown_2 = Town Square 8 (Dollet streets, between dotown_3 and
    // dotown_1, post-FMV disc00_06h). v0.15.9.2.16 BAT proved
    // path-finding can't navigate this field -- the walkmesh has
    // disconnected islands and A* fails. Use MODE_DIRECTION south
    // (screen-down) at running speed. The party walks into the next
    // SETLINE trigger which scripts the segment-to-segment transition.
    // dirY=+1 maps to world south via the field's camera (per BAT
    // calibration camDown=(0.291,-0.957)). Running -- no AI rule
    // restricts pace on this field; the chase is at top speed.
    { "dotown_2", MODE_DIRECTION,
      /*dirX=*/0, /*dirY=*/+1,
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false,
      /*stages=*/nullptr, /*stageCount=*/0 },
    // dotown_1 = Town Square 6 (Dollet streets, last chase field before
    // disc00_07h.avi chase climax FMV). v0.15.9.2.17 BAT (2026-05-11
    // 16:13:40-16:14:13) showed CALIB phase 1 FAILED on field entry
    // (no movement during the 24-tick lX=+1000 test) -- same symptom
    // as v0.15.9.2.16 BAT but without Aaron's manual interference,
    // confirming the failure is intrinsic to dotown_1's first-second
    // state. Without proper camera axes, the path-finding drive's
    // analog steering was approximate; party moved ~3300 units in the
    // right direction (spawn (1629,6120) -> ~(89,2297)) then drifted
    // off-course and cycled velocity-stuck / wp-skipping for 24
    // seconds until eventually drifting into the FMV trigger zone
    // and firing disc00_07h.avi. Fix: same approach as dotown_2 --
    // explicit MODE_DIRECTION south (screen-down) bypasses CALIB
    // entirely. The party walks continuously south; SETLINE-triggered
    // chase scripts route the actual movement; eventually the party
    // crosses the FMV trigger. dirY=+1 alone (no horizontal
    // component) -- v0.15.9.2.17 BAT confirmed default-camera south
    // moves the party in the correct general direction (the
    // path-finding drive made progress in that direction before
    // getting stuck). If the v0.15.9.2.18 BAT shows the party walks
    // south but misses the trigger because it's slightly west,
    // add dirX=-1 in v0.15.9.2.19. Running -- no AI rule applies.
    { "dotown_1", MODE_DIRECTION,
      /*dirX=*/0, /*dirY=*/+1,
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false,
      /*stages=*/nullptr, /*stageCount=*/0 },
};
static const int kFieldConfigsCount =
    (int)(sizeof(kFieldConfigs) / sizeof(kFieldConfigs[0]));

// ============================================================================
// State
// ============================================================================

static bool s_initialized      = false;
static bool s_engaged          = false;
static char s_engagedField[32] = {0};

// v0.15.9.2.10: ASK gate state. v0.15.9.1 dropped the original
// !IsAskActive() gate on the theory that FF8 blocks input during the
// ASK anyway, so the gate added no protection but delayed engagement.
// v0.15.9.2.9 BAT proved that theory wrong: on domt4_1 Aaron reported
// "unable to move" with NO footsteps the whole BAT. The auto-pilot
// engaged at field-entry time (between chase_detector debounce settling
// and the chase-trigger MES + deferred-open ASK firing), installed the
// fake gamepad and held arrows + analog through the entire scripted
// intro and ASK. Position changes in the log were the chase script's
// own scripted entity motion -- the auto-pilot's input wasn't reaching
// the engine at all, AND it wedged the input pipeline so that after
// the ASK closed Aaron couldn't move via any path.
//
// Aaron's clarification: "auto-drive for the chase scene should not
// start or be affecting navigation until the ASK dialog fires and an
// option is selected." Just !IsAskActive() isn't enough -- the ASK is
// deferred-opened 3 seconds after Squall's chase-trigger MES fires,
// which itself fires after some script intro time. There's a 5-12
// second window between field-entry and ASK-open where the ASK isn't
// yet active but engaging would still wedge the engine.
//
// Solution: a two-step state machine that tracks "has the chase ASK
// been observed open AND then closed?" Once both happen we know the
// scripted intro is done and the player has handed control to the
// auto-pilot. Reset on chase activation transitions (chase-end
// followed by a new chase session re-arms the gate).
static bool s_prevChaseActive  = false;  // tracks IsChaseActive transitions
static bool s_askWasActive     = false;  // ASK has been seen open
static bool s_askAnswered      = false;  // ASK was open and is now closed

// v0.15.9.2.14: Trigger-line index for the current fallback config, if any.
// chase-drive uses this for cross-product sign-flip line crossing detection
// instead of stopping at a fixed point-distance from the target. -1 means
// the fallback is a plain point target (largest cluster center).
static int s_fallbackTriggerLineIdx = -1;

// v0.15.9.2.15: INF gateway crossing-line endpoints for the current fallback
// config. When the fallback finds a gateway via GetGatewayNearestCluster,
// these hold the gateway's full line segment so StartChaseDrive can install
// cross-product detection. All four zero means no gateway -- fall back to
// trigger-line index (s_fallbackTriggerLineIdx) or plain point target.
static int32_t s_fallbackGwLineX1 = 0;
static int32_t s_fallbackGwLineY1 = 0;
static int32_t s_fallbackGwLineX2 = 0;
static int32_t s_fallbackGwLineY2 = 0;

// v0.15.9.2.11: Completed-field gate. v0.15.9.2.10 BAT on domt2_1 showed
// an infinite engagement loop: player spawned at (444,-500), fallback
// target was (276,-727), startDist=282 -- already inside the default
// arrive distance. Chase-drive engaged, immediately called Arrived.,
// disengaged. Next Update tick saw s_engaged=false, rebuilt the fallback,
// re-engaged, arrived, disengaged. Loop at ~60Hz: fake gamepad installed
// and uninstalled hundreds of times per second, log flooded with
// "fallback config built" lines, no actual driving happens, and the
// input pipeline thrash probably explains the no-footsteps symptom on
// fallback fields too.
//
// Fix: when chase-drive completes (Arrived or Stuck) we mark the field
// as "auto-pilot done" and refuse to re-engage on that field until the
// field actually changes. Player can drive manually for the rest of
// that field if there's more ground to cover (e.g., the target was the
// largest dead-end cluster but the actual screen-transition trigger is
// further out). On field change, the marker clears.
static char s_completedField[32] = {0};

// v0.15.9.1: Per-second diagnostic tick counter. Increments every Update()
// call while engaged; logs status line and resets at 60. Reset to 0 at
// Engage() so each fresh engagement gets a clean count.
static int s_diagTickCounter = 0;

// v0.15.9.3: Pre-engage chase-active diagnostic state.
//
// s_chaseActiveTickCounter: separate counter for the once-per-second
// pre-engage log. Increments every Update() call while chaseActive is true,
// regardless of engagement state. Lets us log party + kani positions during
// the chase-ASK window (when the existing s_diagTickCounter is dormant
// because chase_auto_pilot hasn't engaged yet).
//
// s_prevPosX, s_prevPosY, s_prevPosValid: party position from the previous
// per-second snapshot. Used to compute `delta=(dX,dY)` -- the party motion
// since the last log line. This is the v0.15.9.3 key diagnostic: it tells
// us whether the analog input we're injecting is actually moving the party,
// or whether the chase script is overriding it. On engaged fields, dX/dY
// should correlate (modulo camera rotation) with the analog direction; on
// fields where the script overrides, dX/dY will be inconsistent with analog.
// s_prevPosValid is false on the first tick of a chase (no previous sample)
// and is reset when the chase deactivates.
static int     s_chaseActiveTickCounter = 0;
static int32_t s_prevPosX                = 0;
static int32_t s_prevPosY                = 0;
static bool    s_prevPosValid            = false;

// v0.15.9.8.2: Bridge (domt1_1) all-slots diagnostic tick counter. Fires
// LogBridgeDiagnostic every 6 Update ticks (10Hz) when engaged on the
// bridge field. v0.15.9.8.1 BAT found ChaseDetector resolves kani symIdx=12
// to Others slot 6 on domt1_1, but that slot reads pos=(0,0) on every tick
// throughout the bridge traversal -- so the actually-pursuing kani is
// either in a different slot or in the Backgrounds array. The diagnostic
// enumerates every non-zero slot of both arrays and dumps slot index +
// SYM name + pos + Y-delta-vs-party so v0.15.9.8.3 can route the dance
// logic to the right entity.
static int     s_bridgeDiagTick = 0;

// v0.15.9.8.3: Bridge dance state machine. The bridge (domt1_1) has an
// interactive feedback loop with X-ATM092:
//
//   1. Party starts heading east (default).
//   2. Robot eventually leaps east, vaulting over the party and landing
//      ahead of them on the corridor (X far east of party).
//   3. With robot blocking east, party must turn west to retreat;
//      otherwise the chase script's catch evaluator fires at X~2053.
//   4. After turning west, robot eventually leaps again (presumably west).
//      As soon as the leap STARTS (mid-air, before landing), turn east
//      immediately -- robot can't course-correct mid-jump, so party slips
//      past while it's airborne.
//   5. Resume east, cross the natural east-edge SETLINE into doopen2a.
//
// Detection signals confirmed by v0.15.9.8.2 BAT (laguna at Others slot 3):
//   * Robot Y is constant on the bridge (-446) -- jumps are X-axis only
//     in walkmesh coords (the visual "jump" is the 3D Z-axis, not Y).
//   * Normal pursuit speed: ~106 units / 100ms tick (slightly faster than
//     party at ~93).
//   * Leap speed: ~372 units / 100ms tick (3.5x normal).
//   * Landed: 0-1 units / tick (stationary).
//
// State variables here track previous-sample kani.X for delta computation,
// the current dance state (EAST / WEST leg), a "was leaping" latch (so we
// only treat "landed" as significant after observing a leap), and dwell-
// time counters for transition debouncing and west-leg timeout fallback.
enum BridgeDanceState {
    BRIDGE_DANCE_EAST = 0,  // initial: drive east, watch for landed-in-front
    BRIDGE_DANCE_WEST = 1,  // drive west, watch for next leap start
};

// Per-sample tick budget. Update() runs at ~60 Hz; we sample kani motion
// every kBridgeSamplePeriodTicks ticks, matching the existing BridgeDiag
// 10 Hz cadence so the X-delta values line up with the v0.15.9.8.2 data
// that calibrated the thresholds below.
static const int     kBridgeSamplePeriodTicks = 6;

// X-speed thresholds (units per 100 ms sample) classifying robot motion.
// From v0.15.9.8.2 BAT: normal pursuit 70-140, leap 350-500, stationary
// 0-36. Two-tier classification with a deliberate gap so a borderline
// sample doesn't ambiguously read as both.
static const int32_t kBridgeLeapThreshold  = 200;
static const int32_t kBridgeLandThreshold  = 50;

// East-leg "landed in front" debounce: require this many consecutive
// samples meeting the landed criteria before committing the turn. Cheap
// protection against a one-sample noise spike masquerading as a landing.
static const int     kBridgeLandConsec     = 2;

// Minimum dwell time per state (in 60 Hz Update ticks) before any
// transition can fire. Prevents oscillation if the kani's X delta briefly
// flickers during a landing.
static const int     kBridgeMinDwellTicks  = 60;     // 1 second

// West-leg safety timeout. If no leap is detected for this long after
// turning west, force a transition back to east. Without this, a bridge
// where the robot never re-leaps would leave the party walking west
// forever (we'd never reach the east-edge SETLINE exit). Generous value
// because the natural pre-leap chase phase took ~3 seconds in the
// v0.15.9.8.2 BAT (ticks 13-47), and we want to give the west leg the
// same room to develop before bailing.
static const int     kBridgeWestTimeoutTicks = 300;  // 5 seconds

// State.
static BridgeDanceState s_bridgeDanceState     = BRIDGE_DANCE_EAST;
static int32_t          s_bridgeLastKaniX      = 0;
static bool             s_bridgeLastKaniValid  = false;
static int              s_bridgeSampleCounter  = 0;  // counts 60Hz ticks toward next sample
static int              s_bridgeConsecLandSamples = 0;
static bool             s_bridgeWasLeaping     = false;
static int              s_bridgeTicksSinceXition  = 0;  // 60Hz ticks since last state transition (or engage)
static int              s_bridgeLeapCount      = 0;  // total leaps observed this engagement (diagnostic)

// Cached config for the engaged field (so the per-second log can read
// dir/walk values without re-running LookupConfig).
static FieldDriveMode s_engagedMode = MODE_DIRECTION;
static int8_t  s_engagedDirX    = 0;
static int8_t  s_engagedDirY    = 0;
static int32_t s_engagedTargetX = 0;
static int32_t s_engagedTargetY = 0;
static bool    s_engagedWalk    = false;

// v0.15.9.7: MODE_STAGED_DIRECTION state.
//
// s_engagedStages / s_engagedStageCount: pointer + count copied from the
// FieldConfig at engagement time. Per-tick refresh uses this without
// having to re-look-up the config.
//
// s_currentStageIdx: index of the currently-active stage. -1 means
// uninitialized (set in Engage()); each tick the refresh code re-picks
// the stage by current Y position, and logs a transition message when
// the index changes.
static const FieldStage* s_engagedStages     = nullptr;
static int               s_engagedStageCount = 0;
static int               s_currentStageIdx   = -1;

// v0.15.9.7: Helper -- does this mode use direction-drive plumbing?
// MODE_DIRECTION, MODE_STAGED_DIRECTION, and (v0.15.9.8.3) MODE_BRIDGE_DANCE
// all call StartDirectionDrive / StopDirectionDrive. MODE_TARGET uses
// StartChaseDrive instead.
static inline bool IsDirectionLikeMode(FieldDriveMode mode)
{
    return mode == MODE_DIRECTION
        || mode == MODE_STAGED_DIRECTION
        || mode == MODE_BRIDGE_DANCE;
}

// v0.15.9.7: Helper -- find the active stage for a given Y position.
// Walks the stage array (DECREASING activeMinY order) and returns the
// index of the first stage whose activeMinY is <= posY. The last stage
// must have activeMinY = INT32_MIN so this always returns a valid index
// (count - 1 worst case).
static int PickStageIdx(const FieldStage* stages, int count, int32_t posY)
{
    if (stages == nullptr || count <= 0) return -1;
    for (int i = 0; i < count; ++i) {
        if (posY >= stages[i].activeMinY) return i;
    }
    return count - 1;  // fallback (last stage matches anything if activeMinY=INT32_MIN)
}

// FF8 entity-array stride for "others" (party slots). Same value
// field_navigation.cpp uses internally. Squall in the chase scene is
// always entity[0] -- the lead member. Position bytes at +0x190 (X * 4096)
// and +0x194 (Y * 4096), matching field_nav_helpers.inl::GetEntityPos.
static const uint32_t ENTITY_STRIDE_OTHERS = 0x264;

// ============================================================================
// Config lookup
// ============================================================================

static const FieldConfig* LookupConfig(const char* fieldName)
{
    if (fieldName == nullptr || *fieldName == '\0') return nullptr;
    for (int i = 0; i < kFieldConfigsCount; ++i) {
        if (std::strcmp(fieldName, kFieldConfigs[i].fieldName) == 0)
            return &kFieldConfigs[i];
    }
    return nullptr;
}

// v0.15.9.2.6: Per-field fallback config buffer. When LookupConfig() returns
// null on a chase field, BuildFallbackConfig() asks FieldNavigation for the
// largest cluster center and synthesizes a MODE_TARGET FieldConfig pointing
// at it. Returns true on success (and *outCfg is populated) or false if the
// walkmesh didn't load or the scanner found no clusters.
//
// The fallback buffer is module-static so the returned pointer stays valid
// for the duration of the engagement. Engage() copies the fieldName into
// it before each fallback engagement so its lifetime matches the engaged
// field. fieldName must remain valid for the engagement (chase_detector's
// debounced name is static, so this is fine).
static char        s_fallbackFieldName[32] = {0};
static FieldConfig s_fallbackConfig = {};

static const FieldConfig* BuildFallbackConfig(const char* fieldName)
{
    if (fieldName == nullptr || *fieldName == '\0') return nullptr;
    int32_t tgtX = 0, tgtY = 0;
    // v0.15.9.2.15: Three-tier target preference for chase fallback:
    //   1. INF gateway crossing line  -- the engine's actual screen-transition
    //      exit, with explicit destination field ID. Picked by direction-
    //      alignment with the cluster (not nearest-to-cluster, because the
    //      entry-back gateway can be geometrically closer; see header).
    //   2. SETLINE trigger nearest cluster -- works on fields whose JSM Line
    //      entities are SCREEN_BOUND/UNKNOWN, not on Event Trigger chase fields
    //      like domt2_1. v0.15.9.2.14's primary mechanism. Still useful for
    //      non-chase fallback uses of this path.
    //   3. Cluster center only -- plain point-distance arrival as last resort.
    //      Chase fields hit this path only when both gateway and SETLINE
    //      lookups fail (rare; should never happen in practice).
    s_fallbackTriggerLineIdx = -1;
    s_fallbackGwLineX1 = 0; s_fallbackGwLineY1 = 0;
    s_fallbackGwLineX2 = 0; s_fallbackGwLineY2 = 0;

    int32_t gwX1 = 0, gwY1 = 0, gwX2 = 0, gwY2 = 0;
    bool gotGw = FieldNavigation::GetGatewayNearestCluster(&tgtX, &tgtY,
                                                           &gwX1, &gwY1,
                                                           &gwX2, &gwY2);
    int trigIdx = -1;
    if (gotGw) {
        s_fallbackGwLineX1 = gwX1; s_fallbackGwLineY1 = gwY1;
        s_fallbackGwLineX2 = gwX2; s_fallbackGwLineY2 = gwY2;
    } else {
        bool gotTrig = FieldNavigation::GetTriggerLineNearestCluster(&tgtX, &tgtY, &trigIdx);
        if (gotTrig) {
            s_fallbackTriggerLineIdx = trigIdx;
        } else {
            if (!FieldNavigation::GetLargestClusterCenter(&tgtX, &tgtY)) {
                Log::Field("ChaseAutoPilot: fallback for field='%s' UNAVAILABLE "
                           "(walkmesh not loaded; no gateways, no triggers, no clusters)",
                           fieldName);
                return nullptr;
            }
        }
    }
    std::strncpy(s_fallbackFieldName, fieldName, sizeof(s_fallbackFieldName) - 1);
    s_fallbackFieldName[sizeof(s_fallbackFieldName) - 1] = '\0';
    s_fallbackConfig.fieldName = s_fallbackFieldName;
    s_fallbackConfig.mode      = MODE_TARGET;
    s_fallbackConfig.dirX      = 0;
    s_fallbackConfig.dirY      = 0;
    s_fallbackConfig.targetX   = tgtX;
    s_fallbackConfig.targetY   = tgtY;
    // v0.15.9.2.12: Default to RUNNING (walk=false). The chase as a whole is
    // Squall fleeing X-ATM092 at top speed; running is the right default for
    // any chase field we don't have an explicit config for. Aaron confirmed
    // after the v0.15.9.2.11 BAT: "the party should be running on this field
    // not walking" (re: domt2_1). The walking-mode exception (Aaron's AI rule
    // #1) applies only to domt5_1 where running shakes the cliff path and
    // the party gets caught -- that field has an explicit config with
    // walk=true. Previous default of walk=true was a misreading.
    s_fallbackConfig.walk      = false;
    if (gotGw) {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running INF-GATEWAY line(%d,%d)->(%d,%d) (cross-product detection)",
                   fieldName, (int)tgtX, (int)tgtY,
                   (int)gwX1, (int)gwY1, (int)gwX2, (int)gwY2);
    } else if (trigIdx >= 0) {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running TRIGGER-LINE idx=%d (cross-product detection)",
                   fieldName, (int)tgtX, (int)tgtY, trigIdx);
    } else {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running (cluster-center fallback, no gateway/trigger found)",
                   fieldName, (int)tgtX, (int)tgtY);
    }
    return &s_fallbackConfig;
}

// Compass name for log messages. Returns a short descriptive string for
// the (dirX, dirY) pair so the FREEZE/ENGAGED log lines are readable.
static const char* DirectionName(int8_t dirX, int8_t dirY)
{
    if (dirX == 0 && dirY == 0) return "target";
    if (dirX == 0 && dirY <  0) return "north";
    if (dirX == 0 && dirY >  0) return "south";
    if (dirX <  0 && dirY == 0) return "west";
    if (dirX >  0 && dirY == 0) return "east";
    if (dirX <  0 && dirY <  0) return "northwest";
    if (dirX >  0 && dirY <  0) return "northeast";
    if (dirX <  0 && dirY >  0) return "southwest";
    if (dirX >  0 && dirY >  0) return "southeast";
    return "?";
}

// ============================================================================
// Entity[0] (Squall) position read for diagnostic logging
// ============================================================================

// SEH-guarded read of Squall's screen-space position. Returns true and
// fills (x, y) if successful; returns false if the address chain isn't
// resolved or the read faults (e.g. mid-field-load transition).
//
// Coordinates are integer divisions of the fixed-point bytes -- matching
// field_nav_helpers.inl::GetEntityPos so log values line up with anything
// FieldNavigation logs about player position. We don't fall back to the
// 0x20/0x24 simple-int16 path here because in chase fields Squall is
// always actively moving (or being driven by us) and the fixed-point
// path at 0x190/0x194 is always populated.
static bool ReadSquallPosition(int32_t& outX, int32_t& outY)
{
    if (!FF8Addresses::pFieldStateOthers) return false;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return false;
        uint8_t* block = base + ENTITY_STRIDE_OTHERS * 0;  // entity[0] = Squall
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.15.9.2.8: SEH-guarded read of the kani's (X-ATM092 spider's) world
// position via ChaseDetector::GetKaniEntityPtr(). Returns true and fills
// (x, y) on success; returns false if the kani slot is unresolved in this
// field, the runtime block pointer is null, or the read faults.
//
// Used by the per-second diagnostic to test Aaron's hypothesis (raised
// after v0.15.9.2.7 BAT) that the kani's collision is what pushes Squall
// through chase fields -- not the auto-pilot's analog/keyboard input.
// If the kani sits right behind Squall (small distance) every time wp
// progress occurs, and stays far away when the party is stuck, that
// confirms the collision-push hypothesis and means the entire chase
// auto-pilot premise (input injection) needs rethinking.
//
// Reads at +0x190 (X*4096) / +0x194 (Y*4096), divided by 4096 -- same
// layout as Squall (entity blocks are uniform). The kani entity block
// pointer is owned by ChaseDetector which caches it at field-change time.
static bool ReadKaniPosition(int32_t& outX, int32_t& outY)
{
    uintptr_t kani = ChaseDetector::GetKaniEntityPtr();
    if (kani == 0) return false;
    __try {
        uint8_t* block = reinterpret_cast<uint8_t*>(kani);
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Squared distance helper (avoids sqrt for log threshold checks).
static int32_t DistSquared(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int64_t dx = (int64_t)ax - (int64_t)bx;
    int64_t dy = (int64_t)ay - (int64_t)by;
    int64_t sq = dx*dx + dy*dy;
    if (sq > 0x7FFFFFFFLL) sq = 0x7FFFFFFFLL;
    return (int32_t)sq;
}

// Integer sqrt for distance display (returns 0 for negative input).
// v0.15.9.2.9: Newton's method from x=v doesn't converge in 6 iterations for
// large squared values (e.g. v=796850 converges to 12471 after 6 iter, real
// answer is 892). v0.15.9.2.8 BAT showed kdist values inflated 14x. Switch to
// std::sqrt via <cmath>; the cast back to int32_t truncates fractional pixels
// which is fine for distance display.
static int32_t IntSqrt(int32_t v)
{
    if (v <= 0) return 0;
    return (int32_t)std::sqrt((double)v);
}

// ============================================================================
// v0.15.9.3: Chase-active diagnostic helper
// ============================================================================
//
// Pre-engage logging: runs once per second from the moment ChaseDetector
// reports chase_active = true (typically on entry to a chase field) through
// chase deactivation. Independent of chase_auto_pilot engagement state, so
// it covers the ASK window (where the existing engaged tick log is silent
// because chase_auto_pilot hasn't engaged yet).
//
// Output format:
//   ChaseActiveDiag: field='X' state=PRE-ENGAGE|ENGAGED-DIR|ENGAGED-TGT
//     pos=(pX,pY) delta=(dX,dY) dmag=N kani=(kX,kY) kdist=K [analog=(lX,lY)]
//
// `delta` is the difference between this tick's pos and the previous tick's
// pos (s_prevPosX/Y). `dmag` is the magnitude of that delta. `kdist` is the
// squall-kani distance. When the auto-pilot is engaged, `analog=(lX,lY)`
// reports the analog values we're injecting (so the post-BAT analysis can
// correlate analog -> world delta).
static void LogChaseActiveDiagnostic(const char* fieldName)
{
    int32_t pX = 0, pY = 0;
    bool gotPos = ReadSquallPosition(pX, pY);

    int32_t kX = 0, kY = 0;
    bool gotKani = ReadKaniPosition(kX, kY);

    // Compose the delta substring from previous-tick pos. First tick of
    // chase: s_prevPosValid is false, delta is "N/A".
    char deltaBuf[64];
    if (gotPos && s_prevPosValid) {
        int32_t dX = pX - s_prevPosX;
        int32_t dY = pY - s_prevPosY;
        int32_t dMag = IntSqrt(DistSquared(0, 0, dX, dY));
        std::snprintf(deltaBuf, sizeof(deltaBuf),
                      " delta=(%d,%d) dmag=%d", (int)dX, (int)dY, (int)dMag);
    } else {
        std::snprintf(deltaBuf, sizeof(deltaBuf), " delta=N/A");
    }

    // Compose the kani substring.
    char kaniBuf[96];
    if (gotKani && gotPos) {
        int32_t kdist = IntSqrt(DistSquared(kX, kY, pX, pY));
        std::snprintf(kaniBuf, sizeof(kaniBuf),
                      " kani=(%d,%d) kdist=%d", (int)kX, (int)kY, (int)kdist);
    } else if (gotKani) {
        std::snprintf(kaniBuf, sizeof(kaniBuf),
                      " kani=(%d,%d) kdist=?", (int)kX, (int)kY);
    } else {
        std::snprintf(kaniBuf, sizeof(kaniBuf), " kani=UNRESOLVED");
    }

    // Compose the state and (if engaged) analog substring.
    const char* stateStr;
    char analogBuf[64];
    analogBuf[0] = '\0';
    if (s_engaged) {
        if (IsDirectionLikeMode(s_engagedMode)) {
            stateStr = (s_engagedMode == MODE_STAGED_DIRECTION) ? "ENGAGED-STG" : "ENGAGED-DIR";
            int32_t lX = (int32_t)s_engagedDirX * 1000;
            int32_t lY = (int32_t)s_engagedDirY * 1000;
            std::snprintf(analogBuf, sizeof(analogBuf),
                          " analog=(%d,%d)", (int)lX, (int)lY);
        } else {
            stateStr = "ENGAGED-TGT";
            // MODE_TARGET doesn't have a stable analog -- the path-finder
            // changes it per tick to steer toward the current waypoint.
            // The [drive] log line elsewhere captures per-tick analog.
            std::snprintf(analogBuf, sizeof(analogBuf),
                          " tgt=(%d,%d)",
                          (int)s_engagedTargetX, (int)s_engagedTargetY);
        }
    } else {
        stateStr = "PRE-ENGAGE";
    }

    if (gotPos) {
        Log::Field("ChaseActiveDiag: field='%s' state=%s pos=(%d,%d)%s%s%s",
                   fieldName, stateStr, (int)pX, (int)pY,
                   deltaBuf, kaniBuf, analogBuf);
    } else {
        Log::Field("ChaseActiveDiag: field='%s' state=%s pos=READ_FAILED%s%s%s",
                   fieldName, stateStr, deltaBuf, kaniBuf, analogBuf);
    }

    // Update prev-pos for the NEXT tick's delta computation. Done last so
    // any future logging on this same tick (e.g. the engaged-branch tick log)
    // sees the pre-update value of s_prevPos.
    if (gotPos) {
        s_prevPosX = pX;
        s_prevPosY = pY;
        s_prevPosValid = true;
    }
}

// ============================================================================
// v0.15.9.8.2: Bridge (domt1_1) all-slots diagnostic
// ============================================================================
//
// Per-tick (10Hz when called every 6 ticks) dump of every non-zero entity
// slot in both pFieldStateOthers and pFieldStateBackgrounds when engaged on
// the bridge. v0.15.9.8.1 BAT confirmed ChaseDetector resolved kani symIdx=12
// to Others slot 6 but that slot reads (0, 0) the entire bridge traversal --
// the real moving kani entity is somewhere else. Output identifies which
// slot is actually tracking the chase so v0.15.9.8.3 can route the bridge
// dance logic to it.
//
// Output format (one line per non-zero slot):
//   BridgeDiag: field='X' party=(pX,pY) array=Others|Backgrounds
//     slot=I sym='Y' pos=(eX,eY) ydelta=N
//
// Hard-gated to fieldName == "domt1_1". Skips slots whose computed position
// is (0, 0) so the log is concise -- the absence of a slot in the output
// indicates it's empty or unset. SEH-guarded per array.
//
// SYM-name lookup: ChaseDetector caches the field's SYM names at field-
// transition time. The map from (arrayKind, slot) to symIdx is:
//   Backgrounds slot I -> symIdx = doorsLines + I
//   Others      slot I -> symIdx = doorsLines + backgrounds + I
// where doorsLines = doors + lines. For domt1_1 the v0.15.9.8.1 BAT logged
// doors=0, lines=4, bgs=2, others=17, so:
//   kBridgeBgStart     = 4   (doors(0) + lines(4))
//   kBridgeOthersStart = 6   (doors(0) + lines(4) + bgs(2))
// These are hard-coded here because the diagnostic only runs on domt1_1
// and avoiding FieldArchive::LoadJSMCounts keeps the include surface small.
// If the counts ever differ in another game version the sym names will be
// off-by-N -- not a correctness problem (we'd still see which SLOT is moving)
// but it would mislabel the SYM string in the log.
static void LogBridgeDiagnostic(const char* fieldName)
{
    if (fieldName == nullptr || std::strcmp(fieldName, "domt1_1") != 0) return;

    int32_t pX = 0, pY = 0;
    bool gotPos = ReadSquallPosition(pX, pY);

    const int kBridgeBgStart     = 4;
    const int kBridgeOthersStart = 6;
    const uint32_t STRIDE_BG     = 0x1B4;

    // --- Others array ---
    if (FF8Addresses::pFieldStateOthers != nullptr &&
        FF8Addresses::pFieldStateOtherCount != nullptr)
    {
        __try {
            uint8_t* base  = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            int      count = (int)*FF8Addresses::pFieldStateOtherCount;
            if (base != nullptr && count > 0) {
                if (count > 32) count = 32;
                for (int i = 0; i < count; ++i) {
                    uint8_t* block = base + (uintptr_t)i * ENTITY_STRIDE_OTHERS;
                    int32_t fpX = *(int32_t*)(block + 0x190);
                    int32_t fpY = *(int32_t*)(block + 0x194);
                    int32_t x   = fpX / 4096;
                    int32_t y   = fpY / 4096;
                    if (x == 0 && y == 0) continue;
                    int32_t ydelta = gotPos ? (y - pY) : 0;
                    const char* sym = ChaseDetector::GetSymName(kBridgeOthersStart + i);
                    Log::Field("BridgeDiag: field='%s' party=(%d,%d) array=Others "
                               "slot=%d sym='%s' pos=(%d,%d) ydelta=%d",
                               fieldName, (int)pX, (int)pY,
                               i, (sym && sym[0]) ? sym : "?",
                               (int)x, (int)y, (int)ydelta);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // --- Backgrounds array ---
    if (FF8Addresses::pFieldStateBackgrounds != nullptr &&
        FF8Addresses::pFieldStateBackgroundCount != nullptr)
    {
        __try {
            uint8_t* base  = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateBackgrounds);
            int      count = (int)*FF8Addresses::pFieldStateBackgroundCount;
            if (base != nullptr && count > 0) {
                if (count > 32) count = 32;
                for (int i = 0; i < count; ++i) {
                    uint8_t* block = base + (uintptr_t)i * STRIDE_BG;
                    int32_t fpX = *(int32_t*)(block + 0x190);
                    int32_t fpY = *(int32_t*)(block + 0x194);
                    int32_t x   = fpX / 4096;
                    int32_t y   = fpY / 4096;
                    if (x == 0 && y == 0) continue;
                    int32_t ydelta = gotPos ? (y - pY) : 0;
                    const char* sym = ChaseDetector::GetSymName(kBridgeBgStart + i);
                    Log::Field("BridgeDiag: field='%s' party=(%d,%d) array=Backgrounds "
                               "slot=%d sym='%s' pos=(%d,%d) ydelta=%d",
                               fieldName, (int)pX, (int)pY,
                               i, (sym && sym[0]) ? sym : "?",
                               (int)x, (int)y, (int)ydelta);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// ============================================================================
// v0.15.9.8.3: Bridge dance per-tick update
// ============================================================================
//
// Called once per Update() tick when MODE_BRIDGE_DANCE is the engaged mode
// and the field is domt1_1. Samples the kani's X-position every
// kBridgeSamplePeriodTicks ticks (10 Hz), computes the X-delta vs the
// previous sample, and classifies it as LEAPING / LANDED / CHASING. Drives
// the EAST <-> WEST state machine; on transition, updates s_engagedDirX/Y
// so the per-tick StartDirectionDrive refresh below picks up the new
// direction cleanly.
//
// All thresholds and counters are static-file constants documented above.
// Side effects are limited to module-static state and Log::Field output.
static void UpdateBridgeDance(const char* fieldName)
{
    // 60 Hz dwell counters tick every Update() call regardless of sample
    // cadence -- they govern transition gating, not motion classification.
    s_bridgeTicksSinceXition++;

    // 10 Hz sample gate.
    s_bridgeSampleCounter++;
    if (s_bridgeSampleCounter < kBridgeSamplePeriodTicks) return;
    s_bridgeSampleCounter = 0;

    // Read kani position via ChaseDetector (with v0.15.9.8.3 per-field
    // override applied). A read failure here means the kani entity isn't
    // resolvable yet -- hold state and try again next sample.
    int32_t kX = 0, kY = 0;
    if (!ReadKaniPosition(kX, kY)) {
        Log::Field("BridgeDance: kani read FAILED -- holding state=%s dwell=%d",
                   s_bridgeDanceState == BRIDGE_DANCE_EAST ? "EAST" : "WEST",
                   (int)s_bridgeTicksSinceXition);
        return;
    }

    // Read party position for the "kani ahead of party" predicate.
    int32_t pX = 0, pY = 0;
    bool gotParty = ReadSquallPosition(pX, pY);

    // X-delta vs previous sample. On the first sample of an engagement,
    // there's no previous sample -- delta defaults to 0 (classified as
    // landed but the kBridgeLandConsec debounce + wasLeaping latch prevent
    // any spurious transition).
    int32_t dX = s_bridgeLastKaniValid ? (kX - s_bridgeLastKaniX) : 0;
    int32_t absDx = (dX < 0) ? -dX : dX;

    bool isLeaping = (absDx > kBridgeLeapThreshold);
    bool isLanded  = (absDx < kBridgeLandThreshold);
    bool minDwellMet = (s_bridgeTicksSinceXition >= kBridgeMinDwellTicks);

    // The "was leaping" latch: only set true after observing a leap. Used
    // on the east leg so a landed sample without a preceding leap doesn't
    // trigger the turn-west (the kani is initially stationary west of the
    // party for ~1 second before the chase starts; that's not the
    // landing-in-front signal we're after).
    bool justStartedLeaping = (isLeaping && !s_bridgeWasLeaping);
    if (isLeaping) {
        if (!s_bridgeWasLeaping) {
            s_bridgeLeapCount++;
            Log::Field("BridgeDance: leap #%d STARTED state=%s kani=(%d,%d) "
                       "party=(%d,%d) kdx=%d",
                       s_bridgeLeapCount,
                       s_bridgeDanceState == BRIDGE_DANCE_EAST ? "EAST" : "WEST",
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX);
        }
        s_bridgeWasLeaping = true;
    }

    // ===== State machine =====

    if (s_bridgeDanceState == BRIDGE_DANCE_EAST) {
        // East leg: turn west when kani lands IN FRONT (east of party) AFTER
        // observing a leap. The "after a leap" gate is essential -- the
        // kani spends the first ~12 samples stationary at the far-west spawn
        // position, which would otherwise trigger an immediate turn-west on
        // the very first sample.
        bool kaniAhead = gotParty && (kX > pX);
        if (isLanded && kaniAhead && s_bridgeWasLeaping) {
            s_bridgeConsecLandSamples++;
        } else {
            s_bridgeConsecLandSamples = 0;
        }

        if (minDwellMet &&
            s_bridgeConsecLandSamples >= kBridgeLandConsec) {
            Log::Field("BridgeDance: EAST->WEST transition kani=(%d,%d) party=(%d,%d) "
                       "kdx=%d (landed_in_front for %d samples, leapCount=%d, dwell=%d)",
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX, (int)s_bridgeConsecLandSamples,
                       (int)s_bridgeLeapCount, (int)s_bridgeTicksSinceXition);
            s_engagedDirX            = -1;
            s_engagedDirY            =  0;
            s_bridgeDanceState       = BRIDGE_DANCE_WEST;
            s_bridgeTicksSinceXition = 0;
            s_bridgeConsecLandSamples = 0;
            s_bridgeWasLeaping       = false;
        }
    } else /* BRIDGE_DANCE_WEST */ {
        // West leg: turn east the instant we detect a leap START. The robot
        // is mid-air during a leap and can't course-correct, so this is the
        // safest window to reverse direction and slip past.
        if (minDwellMet && justStartedLeaping) {
            Log::Field("BridgeDance: WEST->EAST transition kani=(%d,%d) party=(%d,%d) "
                       "kdx=%d (leap_start, leapCount=%d, dwell=%d)",
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX, (int)s_bridgeLeapCount, (int)s_bridgeTicksSinceXition);
            s_engagedDirX            = +1;
            s_engagedDirY            =  0;
            s_bridgeDanceState       = BRIDGE_DANCE_EAST;
            s_bridgeTicksSinceXition = 0;
            s_bridgeConsecLandSamples = 0;
            s_bridgeWasLeaping       = false;
        } else if (s_bridgeTicksSinceXition >= kBridgeWestTimeoutTicks) {
            // West-leg safety timeout. If we've been retreating for too long
            // without any leap firing, force a transition back to east. The
            // party then continues toward the east-edge SETLINE; in the worst
            // case we get caught at X~2053 like we did before this dance
            // existed, but at least the bridge progresses.
            Log::Field("BridgeDance: WEST->EAST TIMEOUT (no leap detected after %d ticks) "
                       "kani=(%d,%d) party=(%d,%d) kdx=%d",
                       (int)s_bridgeTicksSinceXition,
                       (int)kX, (int)kY,
                       gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
                       (int)dX);
            s_engagedDirX            = +1;
            s_engagedDirY            =  0;
            s_bridgeDanceState       = BRIDGE_DANCE_EAST;
            s_bridgeTicksSinceXition = 0;
            s_bridgeConsecLandSamples = 0;
            s_bridgeWasLeaping       = false;
        }
    }

    // Per-sample diagnostic. Concise format; ~10 lines/sec while engaged.
    const char* stateStr = (s_bridgeDanceState == BRIDGE_DANCE_EAST) ? "EAST" : "WEST";
    const char* motionStr = isLeaping ? "LEAPING" : (isLanded ? "LANDED" : "CHASING");
    Log::Field("BridgeDance: sample state=%s motion=%s kani=(%d,%d) party=(%d,%d) "
               "kdx=%d consecLanded=%d wasLeaping=%d dwell=%d",
               stateStr, motionStr,
               (int)kX, (int)kY,
               gotParty ? (int)pX : 0, gotParty ? (int)pY : 0,
               (int)dX, (int)s_bridgeConsecLandSamples,
               (int)s_bridgeWasLeaping, (int)s_bridgeTicksSinceXition);

    s_bridgeLastKaniX     = kX;
    s_bridgeLastKaniValid = true;
}

// ============================================================================
// Engage / disengage
// ============================================================================

static void Engage(const FieldConfig* cfg)
{
    if (cfg == nullptr) return;

    // v0.15.9.11.3: Activate the synthetic keyboard buffer BEFORE installing
    // the analog override + injecting keys. Once active, our GetDeviceState
    // detour returns the synthetic buffer instead of real DirectInput state,
    // so the user's physical key presses no longer reach the engine. The
    // auto-pilot's InjectKey calls below ALSO update the synthetic buffer
    // (gated by ChaseKeyboard::IsActive() inside InjectKey itself), so the
    // engine sees exactly the keys the auto-pilot wants pressed -- no more,
    // no less. Activate clears the buffer to known-empty state.
    ChaseKeyboard::Activate();

    bool ok = false;
    if (cfg->mode == MODE_DIRECTION) {
        FieldNavigation::StartDirectionDrive(cfg->dirX, cfg->dirY, cfg->walk);
        // StartDirectionDrive doesn't return a status; assume success unless
        // log evidence proves otherwise. F9 mutex is the only known refusal
        // cause and chase auto-pilot doesn't engage while F9 runs.
        ok = true;
    } else if (cfg->mode == MODE_STAGED_DIRECTION) {
        // v0.15.9.7: Multi-stage direction drive. Pick the initial stage by
        // current Y position and start direction-drive with that stage's
        // params. Per-tick refresh in Update() re-picks the stage each tick
        // and updates s_engagedDirX/Y/Walk when the active stage changes.
        if (cfg->stages != nullptr && cfg->stageCount > 0) {
            int32_t pX = 0, pY = 0;
            bool gotPos = ReadSquallPosition(pX, pY);
            if (!gotPos) {
                Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' pos read failed at "
                           "engage; defaulting to first stage", cfg->fieldName);
                pY = INT32_MAX;
            }
            int idx = PickStageIdx(cfg->stages, cfg->stageCount, pY);
            if (idx >= 0 && idx < cfg->stageCount) {
                const FieldStage* stg = &cfg->stages[idx];
                FieldNavigation::StartDirectionDrive(stg->dirX, stg->dirY, stg->walk);
                s_currentStageIdx = idx;
                Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' initial pos=(%d,%d) -> "
                           "stage %d/%d dir=(%d,%d) walk=%d (activeMinY=%d)",
                           cfg->fieldName, (int)pX, (int)pY,
                           idx, (int)cfg->stageCount,
                           (int)stg->dirX, (int)stg->dirY, (int)stg->walk,
                           (int)stg->activeMinY);
                ok = true;
            } else {
                Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' PickStageIdx "
                           "failed for posY=%d (stageCount=%d)",
                           cfg->fieldName, (int)pY, (int)cfg->stageCount);
            }
        } else {
            Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' has no stages defined",
                       cfg->fieldName);
        }
    } else if (cfg->mode == MODE_BRIDGE_DANCE) {
        // v0.15.9.8.3: Bridge dance starts in EAST_LEG state, driving east
        // at running speed via the existing direction-drive plumbing.
        // UpdateBridgeDance() flips s_engagedDirX/Y when the state machine
        // transitions, and the per-tick StartDirectionDrive refresh below
        // picks up the new values via its already-running diff branch.
        FieldNavigation::StartDirectionDrive(+1, 0, /*walk=*/false);
        s_bridgeDanceState        = BRIDGE_DANCE_EAST;
        s_bridgeLastKaniValid     = false;
        s_bridgeSampleCounter     = 0;
        s_bridgeConsecLandSamples = 0;
        s_bridgeWasLeaping        = false;
        s_bridgeTicksSinceXition  = 0;
        s_bridgeLeapCount         = 0;
        ok = true;
    } else if (cfg->mode == MODE_TARGET) {
        // v0.15.9.2: path-finding drive. StartChaseDrive validates state
        // (no F9 active, no dialog open, on field) and returns false on
        // failure. We don't retry -- if it fails on this Update tick, the
        // gate must have flipped (e.g., dialog opened) and we'll try again
        // on the next tick when the gate re-evaluates.
        //
        // v0.15.9.2.14: Pass the trigger-line index. Only the fallback path
        // (BuildFallbackConfig) sets s_fallbackTriggerLineIdx >= 0 currently.
        // Explicit per-field configs use point targets (pass -1). When the
        // index is >= 0, chase-drive enables cross-product sign-flip line-
        // crossing detection -- the player walks ONTO the line and the drive
        // stops the instant they cross, which is what fires FF8's screen
        // transition.
        //
        // v0.15.9.2.15: Also pass INF gateway crossing-line endpoints. Set
        // by BuildFallbackConfig when GetGatewayNearestCluster succeeded.
        // Mutually exclusive with the trigger-line index (only one wins per
        // engagement). Explicit per-field configs pass all zeros = no
        // crossing line, plain point arrival.
        int trigIdx = -1;
        int32_t gwX1 = 0, gwY1 = 0, gwX2 = 0, gwY2 = 0;
        if (cfg == &s_fallbackConfig) {
            trigIdx = s_fallbackTriggerLineIdx;
            gwX1 = s_fallbackGwLineX1; gwY1 = s_fallbackGwLineY1;
            gwX2 = s_fallbackGwLineX2; gwY2 = s_fallbackGwLineY2;
        }
        ok = FieldNavigation::StartChaseDrive(cfg->targetX, cfg->targetY,
                                              trigIdx,
                                              gwX1, gwY1, gwX2, gwY2,
                                              cfg->walk);
    }

    if (!ok) {
        Log::Field("ChaseAutoPilot: failed to engage on field='%s' mode=%d",
                   cfg->fieldName, (int)cfg->mode);
        return;
    }

    std::strncpy(s_engagedField, cfg->fieldName, sizeof(s_engagedField) - 1);
    s_engagedField[sizeof(s_engagedField) - 1] = '\0';
    s_engaged          = true;
    s_engagedMode      = cfg->mode;
    s_engagedTargetX   = cfg->targetX;
    s_engagedTargetY   = cfg->targetY;
    s_diagTickCounter  = 0;

    if (cfg->mode == MODE_STAGED_DIRECTION) {
        // v0.15.9.7: For STAGED mode, s_engagedDirX/Y/Walk track the active
        // stage (set by the engagement branch above), not cfg->dirX/Y/Walk.
        // s_currentStageIdx was set by the staged branch in the if/else.
        const FieldStage* stg = &cfg->stages[s_currentStageIdx];
        s_engagedDirX       = stg->dirX;
        s_engagedDirY       = stg->dirY;
        s_engagedWalk       = stg->walk;
        s_engagedStages     = cfg->stages;
        s_engagedStageCount = cfg->stageCount;
    } else {
        s_engagedDirX       = cfg->dirX;
        s_engagedDirY       = cfg->dirY;
        s_engagedWalk       = cfg->walk;
        s_engagedStages     = nullptr;
        s_engagedStageCount = 0;
        s_currentStageIdx   = -1;
    }

    if (cfg->mode == MODE_DIRECTION) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=DIRECTION direction=%s "
                   "%s (dirX=%d dirY=%d walk=%d)",
                   cfg->fieldName, DirectionName(cfg->dirX, cfg->dirY),
                   cfg->walk ? "WALKING" : "running",
                   (int)cfg->dirX, (int)cfg->dirY, (int)cfg->walk);
    } else if (cfg->mode == MODE_STAGED_DIRECTION) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=STAGED_DIRECTION "
                   "starting stage %d/%d direction=%s %s (dirX=%d dirY=%d walk=%d)",
                   cfg->fieldName, (int)s_currentStageIdx, (int)cfg->stageCount,
                   DirectionName(s_engagedDirX, s_engagedDirY),
                   s_engagedWalk ? "WALKING" : "running",
                   (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk);
    } else if (cfg->mode == MODE_BRIDGE_DANCE) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=BRIDGE_DANCE "
                   "initial state=EAST_LEG direction=east running (dirX=+1 dirY=0 walk=0). "
                   "Will turn west when kani lands in front, east when kani leaps; "
                   "west-leg timeout %d ticks (~5s) for safety.",
                   cfg->fieldName, (int)kBridgeWestTimeoutTicks);
    } else {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=TARGET tgt=(%d,%d) "
                   "%s (walk=%d)",
                   cfg->fieldName, (int)cfg->targetX, (int)cfg->targetY,
                   cfg->walk ? "WALKING" : "running",
                   (int)cfg->walk);
    }
}

static void Disengage(const char* reason)
{
    if (!s_engaged) return;
    if (IsDirectionLikeMode(s_engagedMode)) {
        FieldNavigation::StopDirectionDrive();
    } else {
        FieldNavigation::StopChaseDrive();
    }

    // v0.15.9.11.3: Deactivate the synthetic keyboard buffer. From here on,
    // GetDeviceState pass-through is restored; any physical key presses from
    // the user reach FF8 again. Note that the auto-pilot's StopDirectionDrive
    // / StopChaseDrive above already released arrow keys via SendInput, so
    // the engine sees a clean key-up transition for anything that was held.
    ChaseKeyboard::Deactivate();

    Log::Field("ChaseAutoPilot: DISENGAGED (%s) was on field='%s' mode=%d",
               reason ? reason : "?", s_engagedField, (int)s_engagedMode);

    // v0.15.9.2.11: When chase-drive completes (Arrived or Stuck) mark
    // this field as auto-pilot-done so we don't re-engage in a loop. The
    // reason string from the per-tick refresh path is
    // "chase-drive completed (target reached or stuck)". Match a stable
    // substring rather than the whole string in case the reason text is
    // ever refined.
    if (reason && std::strstr(reason, "chase-drive completed") != nullptr &&
        s_engagedField[0] != '\0') {
        std::strncpy(s_completedField, s_engagedField, sizeof(s_completedField) - 1);
        s_completedField[sizeof(s_completedField) - 1] = '\0';
        Log::Field("ChaseAutoPilot: field '%s' marked auto-pilot complete; "
                   "won't re-engage until field changes", s_completedField);
    }

    s_engagedField[0]  = '\0';
    s_engaged          = false;
    s_engagedMode      = MODE_DIRECTION;
    s_engagedDirX      = 0;
    s_engagedDirY      = 0;
    s_engagedTargetX   = 0;
    s_engagedTargetY   = 0;
    s_engagedWalk      = false;
    s_diagTickCounter  = 0;
    s_bridgeDiagTick   = 0;  // v0.15.9.8.2
    // v0.15.9.8.3: Reset bridge dance state.
    s_bridgeDanceState        = BRIDGE_DANCE_EAST;
    s_bridgeLastKaniValid     = false;
    s_bridgeSampleCounter     = 0;
    s_bridgeConsecLandSamples = 0;
    s_bridgeWasLeaping        = false;
    s_bridgeTicksSinceXition  = 0;
    s_bridgeLeapCount         = 0;
    // v0.15.9.7: Reset staged-direction state.
    s_engagedStages     = nullptr;
    s_engagedStageCount = 0;
    s_currentStageIdx   = -1;
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized      = true;
    s_engaged          = false;
    s_engagedField[0]  = '\0';
    s_engagedMode      = MODE_DIRECTION;
    s_engagedDirX      = 0;
    s_engagedDirY      = 0;
    s_engagedTargetX   = 0;
    s_engagedTargetY   = 0;
    s_engagedWalk      = false;
    s_diagTickCounter  = 0;
    s_prevChaseActive  = false;  // v0.15.9.2.10
    s_askWasActive     = false;  // v0.15.9.2.10
    s_askAnswered      = false;  // v0.15.9.2.10
    s_completedField[0] = '\0';  // v0.15.9.2.11
    s_chaseActiveTickCounter = 0;  // v0.15.9.3
    s_prevPosX            = 0;     // v0.15.9.3
    s_prevPosY            = 0;     // v0.15.9.3
    s_prevPosValid        = false; // v0.15.9.3
    s_bridgeDiagTick      = 0;     // v0.15.9.8.2
    s_bridgeDanceState        = BRIDGE_DANCE_EAST;  // v0.15.9.8.3
    s_bridgeLastKaniValid     = false;              // v0.15.9.8.3
    s_bridgeSampleCounter     = 0;                  // v0.15.9.8.3
    s_bridgeConsecLandSamples = 0;                  // v0.15.9.8.3
    s_bridgeWasLeaping        = false;              // v0.15.9.8.3
    s_bridgeTicksSinceXition  = 0;                  // v0.15.9.8.3
    s_bridgeLeapCount         = 0;                  // v0.15.9.8.3
    s_engagedStages       = nullptr;  // v0.15.9.7
    s_engagedStageCount   = 0;        // v0.15.9.7
    s_currentStageIdx     = -1;       // v0.15.9.7
    Log::Mod("ChaseAutoPilot: Initialized v%s. %d field configs ready: "
             "domt4_1 (DIRECTION run south-east, v0.15.9.4), "
             "domt3_2 (DIRECTION run east, v0.15.9.5), "
             "domt5_1 (STAGED_DIRECTION walk SW->S->SE by Y, v0.15.9.7), "
             "domt1_1 (BRIDGE_DANCE east/west by kani X-velocity, v0.15.9.8.3), "
             "doopen2a (TARGET south, v0.15.9.8), "
             "dotown_2/_1 (DIRECTION run south). "
             "Unknown chase fields fall back to MODE_TARGET via largest-cluster scan. "
             "Engagement gated on chase ASK being answered (v0.15.9.2.10). "
             "Per-field completed marker prevents re-engagement loop (v0.15.9.2.11). "
             "v0.15.9.8.3: kani-slot override on domt1_1 -> Others slot 3 (SYM 'laguna'); "
             "BridgeDiag still active for empirical confirmation.",
             FF8OPC_VERSION, kFieldConfigsCount);
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_engaged) Disengage("Shutdown");
    s_initialized = false;
    Log::Mod("ChaseAutoPilot: Shutdown.");
}

void Update()
{
    if (!s_initialized) return;

    // v0.15.9.2.10: ASK gate state machine. Track chase-activation
    // transitions to reset the gate, and watch IsAskActive transitions to
    // detect when the ASK has been answered. See the state-declaration
    // comment block above for the full rationale.
    bool chaseActive = ChaseDetector::IsChaseActive();
    if (chaseActive && !s_prevChaseActive) {
        // Chase just started. Re-arm the gate -- a fresh ASK answer is
        // required for this chase session.
        s_askWasActive = false;
        s_askAnswered  = false;
        // v0.15.9.3: Reset diagnostic state. First tick of chase has no
        // previous-position sample (delta will print "N/A").
        s_chaseActiveTickCounter = 0;
        s_prevPosValid           = false;
        Log::Field("ChaseAutoPilot: chase activated, waiting for ASK to fire and be answered before engaging");
    } else if (!chaseActive && s_prevChaseActive) {
        // Chase just ended. Clear the gate state for next session.
        s_askWasActive = false;
        s_askAnswered  = false;
        // v0.15.9.3: Reset diagnostic state for next chase session.
        s_chaseActiveTickCounter = 0;
        s_prevPosValid           = false;
    }
    s_prevChaseActive = chaseActive;

    // v0.15.9.3: Pre-engage chase-active diagnostic. Fires once per second
    // (60 Update ticks) while chaseActive is true, regardless of engagement
    // state. Captures the ASK window plus the engaged-state ticks. Field
    // name from ChaseDetector's debounced name; uses "(name not settled)"
    // during the 2-second post-transition debounce so the log line still
    // appears even before the name resolves.
    if (chaseActive) {
        s_chaseActiveTickCounter++;
        if (s_chaseActiveTickCounter >= 60) {
            const char* fnameForDiag = ChaseDetector::GetDebouncedFieldName();
            if (fnameForDiag == nullptr || *fnameForDiag == '\0') {
                fnameForDiag = "(name not settled)";
            }
            LogChaseActiveDiagnostic(fnameForDiag);
            s_chaseActiveTickCounter = 0;
        }
    }

    if (chaseActive) {
        bool askActiveNow = ChaseAskOverlay::IsAskActive();
        if (askActiveNow) {
            if (!s_askWasActive) {
                s_askWasActive = true;
                Log::Field("ChaseAutoPilot: chase ASK observed open (auto-pilot stays disengaged)");
            }
        } else if (s_askWasActive && !s_askAnswered) {
            // ASK was open and is now closed -- user must have selected.
            s_askAnswered = true;
            Log::Field("ChaseAutoPilot: chase ASK answered, engagement gate is now open");
        }
    }

    // Engagement gate: chase field, auto mode, on field, AND chase ASK has
    // been answered for this chase session.
    bool inChaseField = ChaseDetector::IsInChaseField();
    bool autoMode     = (ChaseDetector::GetChaseMode() == ChaseDetector::MODE_AUTO);
    bool onField      = FF8Addresses::IsOnField();

    bool wantEngage = inChaseField && autoMode && onField && s_askAnswered;

    if (!wantEngage) {
        if (s_engaged) {
            const char* reason =
                !inChaseField ? "left chase field"             :
                !autoMode     ? "mode != auto"                  :
                !onField      ? "off-field (battle/menu/etc.)" :
                                "ASK not yet answered";
            Disengage(reason);
        }
        return;
    }

    // Engagement window is open. Get the debounced field name.
    const char* fieldName = ChaseDetector::GetDebouncedFieldName();
    if (fieldName == nullptr || *fieldName == '\0') {
        // Field name not yet settled (during the 2s name-debounce after
        // a transition). Don't engage yet; release any prior held state.
        if (s_engaged) Disengage("field name not settled");
        return;
    }

    // Field changed since last engagement? Disengage cleanly so the
    // new field starts with fresh direction-drive state.
    if (s_engaged && std::strcmp(s_engagedField, fieldName) != 0) {
        Disengage("field changed");
    }

    // v0.15.9.2.11: Field changed since last completion? Clear the
    // completion marker so the new field can be auto-piloted from scratch.
    if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) != 0) {
        Log::Field("ChaseAutoPilot: field changed from completed field '%s' to '%s'; "
                   "clearing completion marker",
                   s_completedField, fieldName);
        s_completedField[0] = '\0';
    }

    // v0.15.9.2.11: If we've already completed auto-pilot on this field,
    // refuse to re-engage. Prevents the engage/arrive/disengage loop
    // discovered in the v0.15.9.2.10 BAT on domt2_1.
    if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) == 0) {
        return;
    }

    // v0.15.9.2.7: Per-tick refresh path FIRST, before any config lookup.
    // v0.15.9.2.6 ran LookupConfig() and BuildFallbackConfig() at the top
    // of every Update tick. BuildFallbackConfig() always logs when it
    // succeeds, so on a fallback-engaged field it flooded the log with
    // hundreds of "fallback config built" lines per second (one per tick,
    // ~60Hz) — confirmed in the v0.15.9.2.6 BAT field log on domt2_1:
    // 16 seconds of stuck-at-wp-13 buried under ~960 spam messages, with
    // any v0.15.9.2.5 advance-on-stuck logs presumably drowned out.
    //
    // Fix: when we're already engaged on the same field, run the per-tick
    // refresh and diagnostic using the cached s_engagedX state (set by
    // Engage() once at fresh engagement). The config pointer is only
    // needed for fresh engagement; the engaged-state cache is sufficient
    // for everything else.
    if (s_engaged && std::strcmp(s_engagedField, fieldName) == 0) {
        // Already engaged on this field. Per-tick refresh depends on mode:
        //
        // MODE_DIRECTION: re-call StartDirectionDrive every tick. The API's
        // "already running" branch is idempotent and runs the keep-alive
        // pulse cycle (see field_nav_directiondrive.inl) so the engine
        // sees fresh KEYDOWN events periodically.
        //
        // MODE_TARGET: path-finding runs autonomously inside the F9 update
        // state machine (UpdateAutoDrive in field_nav_autodrive.inl). We
        // just verify it's still active. If chase-drive completed (arrived
        // at target or stuck-detection gave up), disengage so the player
        // knows we're done. The field-change branch above handles the
        // happy case where reaching the target triggered a field exit.
        if (IsDirectionLikeMode(s_engagedMode)) {
            // v0.15.9.7: For MODE_STAGED_DIRECTION, re-pick the active stage
            // based on current Y position. If it differs from the previously-
            // active stage, update s_engagedDirX/Y/Walk to the new stage's
            // values and log the transition. StartDirectionDrive's already-
            // running branch (see field_nav_directiondrive.inl) then picks up
            // the new analog/arrow values cleanly on the call below.
            if (s_engagedMode == MODE_STAGED_DIRECTION &&
                s_engagedStages != nullptr && s_engagedStageCount > 0) {
                int32_t pX = 0, pY = 0;
                if (ReadSquallPosition(pX, pY)) {
                    int newIdx = PickStageIdx(s_engagedStages, s_engagedStageCount, pY);
                    if (newIdx >= 0 && newIdx < s_engagedStageCount &&
                        newIdx != s_currentStageIdx) {
                        const FieldStage* stg = &s_engagedStages[newIdx];
                        Log::Field("ChaseAutoPilot: STAGED stage transition %d->%d at "
                                   "pos=(%d,%d) new dir=(%d,%d) walk=%d (activeMinY=%d)",
                                   (int)s_currentStageIdx, (int)newIdx,
                                   (int)pX, (int)pY,
                                   (int)stg->dirX, (int)stg->dirY, (int)stg->walk,
                                   (int)stg->activeMinY);
                        s_engagedDirX     = stg->dirX;
                        s_engagedDirY     = stg->dirY;
                        s_engagedWalk     = stg->walk;
                        s_currentStageIdx = newIdx;
                    }
                }
            }
            // v0.15.9.8.3: Bridge dance per-tick update. Owns the EAST/WEST
            // state machine on domt1_1; updates s_engagedDirX/Y when it
            // decides to flip direction, which the StartDirectionDrive
            // refresh below picks up on the same tick.
            if (s_engagedMode == MODE_BRIDGE_DANCE) {
                UpdateBridgeDance(fieldName);
            }
            FieldNavigation::StartDirectionDrive(s_engagedDirX, s_engagedDirY, s_engagedWalk);
        } else {
            if (!FieldNavigation::IsChaseDriveActive()) {
                Disengage("chase-drive completed (target reached or stuck)");
                return;
            }
        }

        // v0.15.9.8.2: Bridge diagnostic. Per-tick log every 6 Update ticks
        // (10Hz) on domt1_1 to find the actually-pursuing kani entity (and
        // characterize Y-axis excursions during jumps for the v0.15.9.8.3
        // dance thresholds). No-op on all other fields. The 10Hz cadence is
        // chosen so a brief jump (estimated ~0.5s) reliably lands in at least
        // 4-5 samples even if it's quick, while keeping log volume bounded
        // (~17 active slots * 10Hz * ~10s of bridge transit = ~1700 lines max
        // per BAT, filtered to non-zero positions).
        if (std::strcmp(fieldName, "domt1_1") == 0) {
            s_bridgeDiagTick++;
            if (s_bridgeDiagTick >= 6) {
                LogBridgeDiagnostic(fieldName);
                s_bridgeDiagTick = 0;
            }
        }

        // Per-second diagnostic, using cached engaged state (no cfg lookup).
        s_diagTickCounter++;
        if (s_diagTickCounter >= 60) {
            // v0.15.9.1.1: log first, THEN reset, so the printed tick
            // value matches the trigger (60) rather than always reading 0.
            int32_t pX = 0, pY = 0;
            bool gotPos = ReadSquallPosition(pX, pY);

            // v0.15.9.2.8: also read the kani's position and compute the
            // squall-kani distance. This is purely diagnostic -- no behavior
            // change. Goal: test Aaron's collision-push hypothesis. If kdist
            // is consistently small when movement happens (party advances
            // through waypoints) and large when movement stops (stuck), the
            // kani's collision is the actual movement source and the auto-
            // pilot's analog/kb input is doing nothing. Conversely, if the
            // party moves while kani is far away, input injection works at
            // least sometimes and the wp-13 stuck has a different cause.
            int32_t kX = 0, kY = 0;
            bool gotKani = ReadKaniPosition(kX, kY);
            char kaniBuf[96];
            if (gotKani && gotPos) {
                int32_t kdist = IntSqrt(DistSquared(kX, kY, pX, pY));
                std::snprintf(kaniBuf, sizeof(kaniBuf),
                              " kani=(%d,%d) kdist=%d", (int)kX, (int)kY, (int)kdist);
            } else if (gotKani) {
                std::snprintf(kaniBuf, sizeof(kaniBuf),
                              " kani=(%d,%d) kdist=?", (int)kX, (int)kY);
            } else {
                std::snprintf(kaniBuf, sizeof(kaniBuf), " kani=UNRESOLVED");
            }

            if (IsDirectionLikeMode(s_engagedMode)) {
                int32_t lX = (int32_t)s_engagedDirX * 1000;
                int32_t lY = (int32_t)s_engagedDirY * 1000;
                const char* modeStr =
                    (s_engagedMode == MODE_STAGED_DIRECTION) ? "STAGED" :
                    (s_engagedMode == MODE_BRIDGE_DANCE)     ? "BRIDGE_DANCE" :
                                                               "DIRECTION";
                if (gotPos) {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=%s "
                               "dir=(%d,%d) walk=%d stage=%d pos=(%d,%d) lX=%d lY=%d%s",
                               s_diagTickCounter, fieldName, modeStr,
                               (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk,
                               (int)s_currentStageIdx,
                               pX, pY, lX, lY, kaniBuf);
                } else {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=%s "
                               "dir=(%d,%d) walk=%d stage=%d pos=READ_FAILED lX=%d lY=%d%s",
                               s_diagTickCounter, fieldName, modeStr,
                               (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk,
                               (int)s_currentStageIdx,
                               lX, lY, kaniBuf);
                }
            } else {
                if (gotPos) {
                    int32_t dx = s_engagedTargetX - pX;
                    int32_t dy = s_engagedTargetY - pY;
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=TARGET "
                               "tgt=(%d,%d) walk=%d pos=(%d,%d) dist=(%d,%d)%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedTargetX, (int)s_engagedTargetY, (int)s_engagedWalk,
                               pX, pY, dx, dy, kaniBuf);
                } else {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=TARGET "
                               "tgt=(%d,%d) walk=%d pos=READ_FAILED%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedTargetX, (int)s_engagedTargetY, (int)s_engagedWalk,
                               kaniBuf);
                }
            }
            s_diagTickCounter = 0;
        }
        return;
    }

    // Not engaged on this field yet -- look up config (explicit first,
    // then fallback). BuildFallbackConfig's log line fires at most once
    // per fresh engagement now, instead of once per tick.
    const FieldConfig* cfg = LookupConfig(fieldName);
    if (cfg == nullptr) {
        // v0.15.9.2.6: No per-field config -- try the generic fallback.
        // BuildFallbackConfig() asks FieldNavigation for the largest cluster
        // center (typically the main corridor / exit area) and returns a
        // synthesized MODE_TARGET config. If the walkmesh didn't load or no
        // clusters were found, fallback returns null and we behave as before
        // (player drives manually).
        cfg = BuildFallbackConfig(fieldName);
        if (cfg == nullptr) {
            // The strcmp above already disengaged us if we were previously
            // engaged on a different field.
            return;
        }
    }

    // Fresh engagement on this field.
    Engage(cfg);
}

bool IsEngaged()
{
    return s_initialized && s_engaged;
}

}  // namespace ChaseAutoPilot
