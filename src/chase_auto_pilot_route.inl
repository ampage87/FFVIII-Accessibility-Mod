// chase_auto_pilot_route.inl -- per-field auto-drive data tables
//
// Textual include from chase_auto_pilot.cpp, after _state.inl. Holds:
//   - kStages_domt5_1[] / kStages_domt5_1_count : MODE_STAGED_DIRECTION
//     table for the domt5_1 west trail (SW -> S -> SE by Y position).
//   - kFieldConfigs[] / kFieldConfigsCount      : per-chase-field drive
//     configuration with mode + direction + target.
//
// The inline rationale comments above each config record describe the
// empirical BAT findings that led to each entry's particular direction /
// target / mode choice. See chase_auto_pilot_history.h for the broader
// narrative of how the chase auto-pilot evolved through v0.15.9.x.

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

// v0.16.1.4: Stage table for doopen2a (Town Square 5). REVISED based
// on Aaron's manual chase BAT (2026-05-16 21:10:38-21:10:42), which
// successfully cleared the field in 4 seconds of movement, 0 catches.
// The ff8_nav_data.log COORD trace captured every triangle change of
// his run; the path is reproduced verbatim below.
//
// Aaron's manual route (timestamps abridged):
//   t=0       (-974, -166)  spawn          tri 52
//   t=0+      (-856, -450)                  tri 51
//   t=1       (-783, -669)                  tri 49
//   t=1.5     (-629, -891)  MAX EAST        tri 46
//   t=2       (-662, -1351)                 tri 97
//   ... (south, drifting west slightly) ...
//   t=4       (-1068, -3542) EXIT TRIGGER   tri 151
//
// The shape is clear: SOUTH-EAST briefly (~1.5 seconds) to a maximum
// east excursion of X=-629, then SOUTH for the rest of the field with
// natural west drift along the western corridor. Total 4 seconds.
//
// CRITICAL FINDING from the trace: at (-807, -2391) Aaron was 162 units
// from kani at (-685, -2284) -- well INSIDE the nominal TALKRAD=500 zone
// -- and was NOT caught. KANI HAS NO ACTIVE PROXIMITY CATCH ON DOOPEN2A.
// The pre-v0.16.1.3 commentary that attributed v0.16.1.2's t=3 catch to
// kani was wrong; that catch's caller was always battleyarou (entityPtr
// 0x0188CA04 in the [CBF] log), and we don't yet know why battleyarou
// fired BATTLE in v0.16.1.2 from 1447 units away -- probably velocity
// or motion-vector based rather than pure proximity. Whatever it is,
// Aaron's east-first / west-corridor route empirically avoids it.
//
// Battleyarou's TALKRAD does have a hard proximity catch at 500 units
// around its JSM-init position (0, -744), as confirmed by v0.16.1.3 BAT
// (auto-pilot at (-446, -821), 453 units, caught immediately). Aaron's
// closest approach was 646 units at his max-east excursion (-629, -891)
// -- a 146-unit margin. Also: TALKRAD expands from 500 to 700 at the
// 7-second mark on this field (per the [TALKRAD] CHANGED log at 21:10:45
// in Aaron's manual run, with context bytes shifting @21E 0->2 @244 0->3
// indicating a script state transition); the auto-pilot must clear the
// exit before that.
//
// === Threshold derivation for the auto-pilot ===
//
// Aaron's empirical SE rate was about 230 east + 480 south per second
// (walking-pace start). Auto-pilot SE rate measured in v0.16.1.3 BAT:
// 528 east + 716 south per second (faster, ratio more east-heavy).
//
// To replicate Aaron's max-east excursion X=-629 with the auto-pilot's
// faster SE: from spawn (-974, -166) at 528 east/sec, X=-629 is reached
// at (974-629)/528 = 0.65 seconds. In that same 0.65s, Y drops by
// 716*0.65 = 466, so Y=-631 at the X=-629 crossing. Threshold Y<-631
// terminates stage 0 at approximately (-629, -631).
//
// At stage-0 end (-629, -631) the distance to battleyarou (0, -744) is
// sqrt(629^2 + 113^2) = 639 -- 139 units outside TALKRAD=500. Closest
// approach during the entire SE leg occurs at the threshold itself
// (battleyarou is southeast of the trajectory line from spawn through
// the threshold point), with margin 139. Safe.
//
// Stage 1 (pure south) from (-629, -631) drifts west at -76/sec (camera
// camDown (-0.097, -0.995)) and south at +782/sec. The party reaches
// the south-edge area at approximately (-905, -3447) after ~3.6 more
// seconds. Total time on field: 4.25 seconds. Beats the 7-second
// TALKRAD expansion comfortably.
//
// Aaron's actual exit position was (-1068, -3542); our predicted
// arrival (-905, -3447) is ~163 east of his and ~95 north -- both in
// the SW corner of the field where some screen-boundary trigger fires.
// The exact trigger geometry isn't fully catalogued (the south SETLINE
// at center (-952, -3703) has unknown endpoints; the gateway crossing
// log from v0.16.1.2 showed `(-497,-3414)->(311,-3414)` but Aaron's
// exit at X=-1068 is well west of that), but both his exit point and
// our predicted exit are in the same SW region, so whatever wider
// trigger he hit, we should hit too.
//
// Walk vs run: walk=false on both stages. Town square is a top-speed
// field per Aaron's 2026-05-12 note. Aaron's manual run looked slower
// than the auto-pilot's top speed but he was still running; the slower
// observed rate is likely due to walkmesh constraints and his analog
// thumb angle, not a forced walk modifier.
//
// === Previous v0.16.1.3 derivation (kept for context) ===
//
// The v0.16.1.3 threshold of Y<-1500 was based on the false assumption
// that kani had an active TALKRAD=500 catch on doopen2a and that the
// SE leg therefore had to extend past kani's X boundary (X>=-185).
// That math required ~1.94 seconds of SE motion, by which point the
// auto-pilot had already crossed INTO battleyarou's 500-unit zone (the
// trajectory passed within 453 units of (0, -744) at t=1s). v0.16.1.3
// BAT confirmed the catch fired at exactly that moment. The kani-
// avoidance reasoning was wrong; with kani inert, we don't need to go
// far east -- we only need to clear the spawn area and the early west
// dead-end cluster, which Aaron's recipe of "several steps SE" achieves
// in well under a second.
static const FieldStage kStages_doopen2a[] = {
    // Stage 0: Y > -631 -- RUN SOUTH-EAST. "Several steps" of SE to
    // reach approximately (-629, -631), matching the X of Aaron's max-
    // east excursion at (-629, -891). 146-unit safety margin from
    // battleyarou (0, -744) at TALKRAD=500.
    { +1, +1, false, -631 },
    // Stage 1: Y <= -631 -- RUN SOUTH. Natural west drift (-76/sec)
    // from the camDown vector (-0.097, -0.995) walks the party along
    // the western corridor to the SW screen-boundary trigger.
    {  0, +1, false, INT32_MIN },
};
static const int kStages_doopen2a_count =
    (int)(sizeof(kStages_doopen2a) / sizeof(kStages_doopen2a[0]));

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
    //
    // v0.16.1.4: Same MODE_STAGED_DIRECTION (SE -> S) as v0.16.1.3 but
    // with the SE stage's threshold tightened from Y<-1500 to Y<-631.
    // See kStages_doopen2a[] above for the full derivation from Aaron's
    // manual chase BAT (2026-05-16) recorded in ff8_nav_data.log.
    //
    // The v0.16.1.3 threshold sent the party 1.94 seconds SE, walking
    // it INTO battleyarou's TALKRAD=500 zone around (0, -744) at t=1s
    // (auto-pilot ended up at (-446, -821), 453 units from (0,-744))
    // and the BATTLE catch fired immediately. The v0.16.1.4 threshold
    // ends SE at ~(-629, -631) -- same X as Aaron's max-east excursion,
    // 639 units from battleyarou's catch center (139-unit margin).
    //
    // Critical correction from the manual BAT trace: KANI HAS NO ACTIVE
    // PROXIMITY CATCH ON DOOPEN2A. Aaron passed within 162 units of
    // kani at (-685, -2284) and was not caught. The pre-v0.16.1.4
    // comments attributing v0.16.1.2's t=3 catch to kani were wrong.
    // That catch was battleyarou (entityPtr 0x0188CA04 in [CBF] log)
    // firing from 1447 units away -- probably velocity- or motion-
    // vector-based, not pure proximity. Aaron's east-first / west-
    // corridor route empirically avoids it.
    //
    // dirX/dirY here are the initial fallback direction-drive analog
    // used at engagement (SE, running). They are subsequently picked per
    // tick by PickStage() based on the party's current Y, so the field-
    // load value mostly only matters before the first stage check fires.
    { "doopen2a", MODE_STAGED_DIRECTION,
      /*dirX=*/+1, /*dirY=*/+1,  // initial fallback direction (matches stage 0)
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false,
      /*stages=*/kStages_doopen2a, /*stageCount=*/kStages_doopen2a_count },
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
