# Auto-Drive Architecture Review & Recommendations

## Date: 2026-03-15
## Author: Claude (analysis for Aaron)

---

## 1. What We've Built (Current Architecture)

The auto-drive system has these main components:

1. **Pathfinding**: A* on walkmesh triangle graph → SSFA funnel algorithm for path smoothing
2. **Steering**: Fake gamepad injection (analog joystick) + keyboard arrow key suppression
3. **Waypoint following**: Sequential waypoint targeting with arrive distance + overshoot detection
4. **Stuck detection**: Velocity-based (DRIVE_STUCK_MIN_DIST=20 over 40-tick windows)
5. **Recovery**: Alternating edge-midpoint re-path (odd phases) and micro-nudge (even phases)
6. **Cancellation**: Arrow key detection at phase ≥ 4, MAX_RECOVERY_PHASES=12, 2400-tick timeout

## 2. Pattern Analysis: Why Drives Fail

From v06.09 test data across 9 fields:
- **Arrived**: 3+ drives (screen transitions, nearby NPCs)
- **Gave up (timeout)**: 3 drives (coordinate issue — now debunked, and micro-oscillation)
- **Cancelled (phase ≥ 4)**: 5+ drives (wall-stuck, corridor issues)
- **Field transition (unintended)**: 3 drives (trigger-line crossing)

From v06.12-v06.13 testing:
- Classroom NPCs: reachable (short distances, open space)
- Corridor NPC: fails consistently (stuck phase fires in <1 second)
- Screen transitions: mixed (some work, some trigger-line crossing)

**The dominant failure mode is: stuck detection fires too early, recovery disrupts a good path, phases escalate to cancel.**

Let me trace the bg2f_1 corridor NPC failure from the v06.13 log:

```
tick 0:    Drive starts, A*+funnel gives 3 waypoints (good path)
           wp0 pre-skipped (dist=104 < 120)
tick ~60:  Stuck phase 1 fires — player moved to tri 41 but stuck detector
           says "not enough movement". Edge-midpoint replaces funnel path.
tick ~80:  wp0 overshoot fires (dist=166, min=66). Phase 2: micro-nudge.
tick ~100: Re-path to funnel after nudge. Player at (267,-2761), target wp at (295,-3302).
tick 120:  Player moved 349 units! (moveDist=349) — that's GOOD progress.
           But moveAng=103° vs analogAng=177° — player moved SIDEWAYS, not toward target.
tick ~140: Phase 3: edge-midpoint again. wp0 reached immediately.
tick 240:  Player at (150,-2680), moved 142 units. wp1 overshoot.
           moveAng=-125° — player BACKING UP from target (dist went from 892 to 986!)
tick ~280: Phase 4: micro-nudge → arrow key cancel fires.
```

**The real problem**: The player IS moving (349 units, 142 units per window) but NOT toward the target. moveAng and analogAng diverge wildly. The analog stick says "go south" (177°) but the player actually moves east-southeast (103°). This means **the heading mapping is wrong for this section of the walkmesh.**

## 3. Root Cause Analysis

### The Heading Problem
Our heading mapping was determined empirically on bgroom_1 (flat field):
- UP arrow / +lY = +Y world direction
- RIGHT arrow / +lX = +X world direction

But this mapping was confirmed on a FLAT field. On angled fields, even though walkmesh X,Y
= entity X,Y (no transform needed for POSITIONS), the **movement direction mapping may be
different**. The arrow keys and analog stick map to SCREEN directions, which are determined
by the camera angle. Different camera angles on different fields mean different direction
mappings.

Evidence: bg2f_1's camera X-axis is (1706, 3723, -1) — a ~65° rotation from bgroom_1's
camera. When we push the analog stick "south" (lY=+998), the game moves the character
along the camera's forward vector, which on bg2f_1 points in a different world direction
than on bgroom_1.

**This is the fundamental issue**: We compute the analog stick direction from the world-space
delta to the target, but the game interprets analog stick values relative to the CAMERA's
orientation. On bgroom_1 (camera ~0° or aligned with world axes), this works. On bg2f_1
(camera ~65° rotated), our world-space direction maps to the wrong screen direction.

### Why This Wasn't Caught Earlier
- bgroom_1 (where most testing happened) has a camera roughly aligned with world axes
- The heading was "confirmed" by back-to-front navigation in bgroom_1 (v05.74-76)
- On angled fields, the initial movement goes in the wrong direction, stuck detection fires,
  recovery generates new paths that also go wrong, and the drive cascades to failure
- We attributed the failures to coordinate transform issues, wall collisions, stuck detection
  sensitivity, and recovery mechanics — when the actual cause was incorrect steering direction

### The Camera Direction Theory
If the analog stick maps to camera-relative directions, then:
- lX = movement along camera's RIGHT axis (in world space)
- lY = movement along camera's FORWARD/DOWN axis (in world space)

For bgroom_1 with camera right=(1892,-3633) and forward=(3610,1881):
- These map to roughly (0.46, -0.89) and (0.88, 0.46) normalized
- The camera is rotated ~62° from world axes

But on bgroom_1, the heading worked with direct world-space mapping. This contradicts
the camera-relative theory... UNLESS bgroom_1's heading "working" was actually because
the paths were short enough that stuck recovery compensated for slightly wrong directions.

Actually, let me reconsider. The DEVNOTES say:
- v05.67: "Inverted heading mapping. Walkmesh +X = screen-left, walkmesh -Y = screen-down."
- v05.74: "Heading fix attempt: removed v05.67 inversion. Back-to-front auto-drive worked by accident."
- v05.76: "Heading: hybrid (Y inverted, X direct). UP=+Y, RIGHT=+X."

The heading was determined by trial and error on one field. It was never derived from the
camera orientation. On fields with a different camera angle, the mapping is wrong.

## 4. Recommended Approach

### Option A: Camera-Relative Steering (Recommended)

Instead of mapping world-space deltas directly to analog stick values, we should:

1. Load the camera's right and forward axis vectors (from .ca or from runtime globals)
2. Project the world-space delta-to-target onto the camera axes
3. Use the projected components as lX (along camera right) and lY (along camera forward)

Formula:
```c
// delta = target - player (in world/entity space)
float dx = targetX - playerX;
float dy = targetY - playerY;
// Project onto camera axes (normalized to unit length)
float camRightX = cam_right.x / 4096.0f;
float camRightY = cam_right.y / 4096.0f;
float camFwdX = cam_forward.x / 4096.0f;
float camFwdY = cam_forward.y / 4096.0f;
// Analog stick values
float stickX = dx * camRightX + dy * camRightY;  // movement along camera right
float stickY = dx * camFwdX + dy * camFwdY;      // movement along camera forward
```

This would make steering correct on ALL fields regardless of camera orientation.

**However**: We proved the .ca camera data doesn't directly correspond to entity positions.
The camera axes are for rendering. We need to determine whether the game's movement system
uses the .ca axes or something else for interpreting analog input direction.

**Alternative**: Determine the heading mapping empirically per field. On field load, briefly
inject a known analog direction and measure which world direction the player moves. This
"calibration" step would give us the exact mapping without needing to understand the camera
data format.

### Option B: Simplified Architecture (Start Fresh)

Given everything we've learned, a cleaner architecture would be:

1. **Keep**: A* pathfinding + walkmesh data loading (this works correctly)
2. **Keep**: Fake gamepad injection + keyboard suppression (the steering mechanism works)
3. **Replace**: The entire waypoint-following + stuck detection + recovery system
4. **Add**: Per-field heading calibration

The replacement approach:

**Reactive Steering with Triangle-Level Goals**

Instead of pre-computing a full waypoint path and then trying to follow it with
stuck detection/recovery:

1. A* gives us a triangle corridor from start to goal
2. Each tick, the drive target is the NEXT triangle in the corridor (not a distant waypoint)
3. Steer toward the midpoint of the shared edge between current triangle and next corridor triangle
4. When the player enters the next triangle, advance the corridor pointer
5. If the player enters a non-corridor triangle (pushed off course), re-run A* from current position

Benefits:
- No waypoint overshoot (target is always the next edge crossing, very close)
- No funnel algorithm needed (edge midpoints are inherently walkable)
- No stuck detection timers needed — if the player hasn't changed triangles after N ticks,
  they're stuck, and we know exactly WHERE (shared edge) they're trying to cross
- Recovery is simple: nudge perpendicular to the shared edge, then retry
- No overshoot/oscillation — the target changes every time the player crosses an edge
- Path quality is still good because the A* corridor is optimal

Costs:
- Slightly less smooth paths (edge-midpoint stepping instead of funnel curves)
- Player may zig-zag slightly in wide open areas
- Need to handle the "last mile" to the actual target (NPC, trigger line) separately

### Option C: Hybrid (Recommended Starting Point)

Keep the current architecture but make these specific changes:

1. **Fix the heading**: Load .ca camera axes at field load. Use them to transform
   world-space deltas to camera-relative analog stick values. Test on bg2f_1 first.

2. **Remove the complex recovery system**: Replace the multi-phase edge-midpoint/micro-nudge
   recovery with simple "re-run A* from current position, generate edge-midpoint waypoints."
   No phases, no nudges, no oscillation between strategies.

3. **Increase stuck tolerance**: DRIVE_STUCK_THRESH from 40 to 80 ticks. Give the player
   more time to actually follow the path before declaring stuck. The 40-tick threshold was
   tuned for keyboard steering; analog steering needs more time to settle.

4. **Remove arrow-key cancel during recovery**: The phase ≥ 4 cancel is causing more
   problems than it solves. JAWS key interception triggers false cancels. Instead, only
   cancel on deliberate user input (e.g., pressing backslash again, or pressing escape).

5. **Add per-tick trigger-line check**: Simple cross-product test each tick. If the player
   is about to cross a non-target trigger line, either stop the drive or redirect.

## 5. Recommended Priority Order

1. **Fix the heading mapping** (biggest impact, explains most failures on non-classroom fields)
2. **Increase stuck threshold and remove phase-4 cancel** (reduces false cancellations)
3. **Simplify recovery to just "re-run A* + edge midpoints"** (reduces complexity)
4. **Add trigger-line proximity check** (prevents unintended field transitions)
5. **Test extensively across fields** (validate the camera-relative steering on multiple fields)

## 6. Questions to Investigate

1. Does the game interpret analog stick input relative to the camera orientation?
   (Test: inject lX=1000,lY=0 on bg2f_1 and see which world direction the player moves.
   Compare with the .ca camera right axis.)

2. Does the .ca camera RIGHT axis match the direction the player moves when lX is positive?
   (This would confirm or deny the camera-relative steering theory.)

3. Are the 38-byte and 40-byte .ca formats actually storing the same data with different
   padding? (38-byte files might have a 2-byte header or different padding.)

4. Could we read the camera orientation from runtime memory instead of the .ca file?
   (FFNx or the game may store the active camera matrix in a global we can read.)

## 7. Conclusion

**We should NOT start completely fresh.** The fundamental infrastructure (walkmesh loading,
A* pathfinding, analog steering injection, entity detection, catalog system) is solid and
battle-tested. But the steering direction mapping and the stuck/recovery system need
significant rework.

The single highest-impact change is fixing the heading mapping to be camera-relative.
If the analog stick is indeed interpreted in camera space (which the movement angle
divergence on bg2f_1 strongly suggests), then fixing this one thing would likely make
most drives succeed on the first attempt, eliminating the need for aggressive stuck
detection and complex recovery.

The second priority is simplifying the recovery system. The current multi-phase
edge-midpoint/micro-nudge/re-path system has become too complex and fragile. Each fix
introduces new edge cases. A simpler "re-path from current position" approach would be
more robust.
