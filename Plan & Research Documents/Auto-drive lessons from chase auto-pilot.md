# Auto-Drive Lessons from Chase Auto-Pilot

**Date:** 2026-05-10
**Source versions:** v0.15.9 -- v0.15.9.2.2
**Author of notes:** Claude (with Aaron's testing data)

## Scope

This document captures findings from building the chase auto-pilot (v0.15.9 - v0.15.9.2.2). Several bugs we hit affect the general F9 path-finding auto-drive too. F9 has known reliability issues; these learnings are inputs for the eventual F9 re-engineering.

The F9 auto-drive shipped layered over multiple iterations (v05.x onwards). It works often enough to be useful but has a high "stuck thrashing" rate, especially in non-trivial geometries. Chase auto-pilot exercised the same code path on the most hostile field we have (domt5_1, rotated camera, elongated corridor triangles, walking-only) and exposed concrete bugs.

## Findings

### 1. The heading-bitmask fallback is dangerous

**Location:** `field_nav_autodrive.inl`, the heading-bitmask computation block.

**Current code:**

```cpp
uint8_t heading = 0;
if (dz >  DRIVE_AXIS_THRESH) heading |= DIR_UP;
if (dz < -DRIVE_AXIS_THRESH) heading |= DIR_DOWN;
if (dx >  DRIVE_AXIS_THRESH) heading |= DIR_RIGHT;
if (dx < -DRIVE_AXIS_THRESH) heading |= DIR_LEFT;
if (heading == 0) heading = DIR_UP;  // fallback
```

`DRIVE_AXIS_THRESH = 150`. When all four bits fail (both |dx| and |dz| below 150), the fallback fires `DIR_UP`. This happens whenever the steer target is close to the player.

The corridor-level steering (v06.17) deliberately picks a target at the **midpoint of the shared edge to the next corridor triangle**, shrunk 30 units toward the next triangle's center. On angled / narrow corridors this midpoint can be tens of units from the player, well below the 150 threshold. Result: keyboard pushes UP arrow indefinitely while analog points at the actual long-range target. The two inputs fight.

**Fix shipped in v0.15.9.2.2:** save `origDx = dx; origDz = dz;` from the long-range target right after the initial `dx = tx - px;` `dz = tz - pz;` computation, and use origDx/origDz for the heading bitmask. The analog continues to use the corridor-tuned dx/dz for fine steering. Keyboard's job is to be a wake-up trigger that doesn't fight the analog -- it doesn't need to be pixel-precise.

**Generalization for F9:** apply the same fix unconditionally (not gated on chase-drive). Origin-direction heading is the right behavior whenever the long-range target exists, which is always. F9 NPCs are usually far away, so this looks like a no-op for the common case, but it removes a known failure mode when corridor steering picks a nearby edge.

### 2. FF8 reads keyboard and analog as voting inputs

When keyboard says one direction and analog says another, FF8 doesn't pick one -- it averages or otherwise blends them, and the result is throttled movement. v0.15.9.2.1 BAT measured 15 units/sec walking when kb=UP fought analog=SW. Calibration with kb and analog aligned measured 864 units/sec on the same field.

**Implication:** the two inputs MUST agree. The current "keyboard = wake-up trigger, analog = direction" design (v05.85) is correct in intent, but only works if both signals point in the same direction. Any divergence costs huge throughput.

**For F9 re-engineering:** ensure ANY code path that touches s_driveHeld considers what direction the analog is currently pointing. If they can disagree, the analog wins -- which means the keyboard wake-up trigger should follow the analog, not the corridor or waypoint target.

### 3. Recovery's perpendicular-nudge logic is wrong for elongated triangles

**Location:** `field_nav_autodrive.inl`, the even-phase recovery block.

The nudge picks the perpendicular direction whose dot-product with `(next-tri-center - player)` is larger. The intent is "push the player toward the next triangle's centroid." This works when triangles are roughly equilateral, but fails on elongated triangles where the centroid sits on the opposite side of the shared edge from the player.

Example from domt5_1: tri 51 has vertices roughly at (-1313, 3361), (-1311, 3590), and (-980, 3316). Centroid ≈ (-1201, 3422). Player at (-1244, 3576) is on tri 32's side of the shared edge at X=-1312. Tri 51's centroid is EAST of the player. The dot-product check picks perpendicular = (+1, 0) (east) -- but to enter tri 51 the player must cross the X=-1312 edge going WEST.

**For F9 re-engineering:** the perpendicular nudge should aim at the **shared edge** to cross, not at the next triangle's centroid. A better heuristic: pick the side of the perpendicular that puts the player closer to the midpoint of the shared edge. The current heuristic is degenerate when the triangle's centroid and edge midpoint diverge.

### 4. Rotated cameras break perpendicular nudges

**Location:** same recovery block.

The nudge direction is computed in world coordinates and then projected through `SetAnalogFromVector` to analog (lX, lY). On fields where the camera is rotated such that both `camRight` and `camDown` are nearly parallel to world ±Y (small cross-product determinant), the X-axis of world space is unreachable from the analog.

domt5_1 has camRight = (0.041, 0.999) and camDown = (0, -1). Determinant ≈ -0.041. World direction (1, -0.01) projects to analog (lX=31, lY=10) -- both deadzone values. The engine ignores them. Net result: nudge produces no useful movement, and the small lY=10 alone projects back to slight +Y world, drifting the player north.

**Implication:** on rotated cameras, you can only move in the direction(s) that the camera axes span. World-X is BARELY accessible (a few units per tick per 1000 lX).

**For F9 re-engineering:**
- Detect "degenerate" cameras (|det(camRight, camDown)| < 0.1) and warn / fall back to a different strategy.
- Don't generate steering vectors that the camera can't express. Project the desired world direction onto the **achievable subspace** before computing analog values. If the achievable subspace is ~1D (det near zero), any orthogonal component is just noise.

### 5. Corridor-level steering can pick the wrong target

**Location:** v06.17 corridor-level steering block.

The corridor steering overrides the funnel waypoint when the player has a valid current triangle in the corridor sequence. It targets the **midpoint of the shared edge to the next corridor triangle**. The intent is fine-grained local steering toward a known-safe transition point.

Problem: when the player drifts to a triangle that's **earlier** in the corridor than intended (e.g., backward), the corridor steering targets the shared edge to advance, even if that edge is in the wrong direction relative to the long-range target.

Example: chase-drive's A* path was `[32, 51, 13, 36, ...]`. Player drifted to tri 29 (NORTH of tri 32, off the planned path). Recovery re-paths from tri 29, gives `[29, 32, 51, 13, ...]`. Corridor steering on tri 29 targets the shared edge with tri 32, which is at Y≈3615 (north of player at Y=3578). So the corridor wants the player to go NORTH to reach tri 32, even though the long-range target Y=235 is far south.

**For F9 re-engineering:**
- Corridor steering should be aware of the long-range target direction. If the next corridor edge is in the opposite direction from the target, suspect that the path got contorted and re-path.
- Or: keep the funnel waypoint as the primary steer target and only use corridor steering when the funnel waypoint is far away.

### 6. Drive timeout too short for slow paths

**Location:** `DRIVE_MAX_TICKS = 2400` (40 s).

F9's typical use case is short walks to nearby NPCs at running pace. 40 s is plenty. But for walking, fields with hostile geometry, or long corridors, 40 s is not enough. v0.15.9.2.2 raised this to 12000 ticks (200 s) for chase-drive only.

**For F9 re-engineering:** make the timeout adaptive. Base it on `startDist` and an estimated pace, with a generous safety margin. Walking pace ≈ 250 units/sec; running ≈ 500. `maxTicks = (startDist / paceUnitsPerSec) * 60 * safetyMargin`.

### 7. Recovery's wiggle interrupts main steering's progress

When DRIVE_STUCK_THRESH=80 ticks (~1.3 s) and DRIVE_STUCK_MIN_DIST=20 units fire stuck detection, recovery launches a wiggle nudge for 8 ticks AND re-paths A*. The wiggle ticks throw away the analog vote that was actually making progress.

In v0.15.9.2.1 BAT, the player was making steady ~15 units/sec south progress, but every 1.5 s a recovery fired an east nudge that drifted the player north. The misdirected nudges undid the slow southward progress, leading to net-zero displacement over 20 seconds.

**For F9 re-engineering:**
- Don't fire recovery just because progress is slow. Only fire when truly stuck (e.g., `moveDist` over a 5+ second window < some threshold). Slow progress IS progress.
- Recovery's wiggle should be informed by the velocity history -- if the player has been moving in direction X for the last second, don't nudge in direction -X.

### 8. DEFAULT-FORWARD when heading bitmask is empty was a bug

Related to finding #1. The fallback `if (heading == 0) heading = DIR_UP;` is also wrong because there's no reason to assume UP is "forward." The "right" fallback when steer is too close to player is to use the **last non-zero heading** (the previous direction), or to derive from the long-range target. The default-to-UP is essentially random.

### 9. Walking speed mechanic to investigate

Calibration measured 864 units/sec along the camDown axis (pure analog, no diagonal). Main steering in real navigation was 15 units/sec with kb/analog conflict. After removing the conflict (v0.15.9.2.2 Fix A), the BAT should reveal whether walking is genuinely slower than calibration's pure-axis measurement, or whether 864 was an artifact of pure-axis input.

If walking IS substantially slower than running on the same field, the F9 re-engineering should account for that in timeouts and stuck-detection thresholds.

### 10. Calibration is per-field but reused across drives

`s_camCalibrated` is set true after the first drive on a field calibrates. Subsequent drives reuse the values. This is correct in concept but means the **first drive on a field eats two phases of artificial movement** (24 + 24 = 48 ticks of injected input that doesn't head toward the target). On short drives, that's a significant chunk of the total drive time.

**For F9 re-engineering:** consider pre-calibrating on field entry rather than at first-drive time. The mod could fire a brief calibration burst when the player walks into a field for the first time (low input, just enough to measure axes) and store the result keyed by field name. Or: derive camera axes from the .ca file directly and skip empirical calibration.

### 11. Engine's reported triangle ID can be STALE

**Source:** v0.15.9.2.2 BAT.

`field_nav_autodrive.inl` reads the player's current triangle from entity[player] + 0x1FA each tick. This is the engine's classifier. In v0.15.9.2.2 BAT, the player position was geometrically inside triangle 13 (south of the 51-13 portal at Y=3316-3330) but the engine kept reporting tri 51 (the previous triangle) for 60+ seconds.

Hypothesis: the engine only reclassifies the tri ID when the entity actively moves. The player was frozen post-calibration (due to kb/analog conflict), so the engine never updated the tri ID. This created a chicken-and-egg lockup:

- Stale tri ID → corridor steering targets backward portal
- Analog points toward backward portal → fights keyboard which points toward long-range target
- Inputs fight → zero movement
- Zero movement → engine doesn't reclassify → stale tri ID persists

**For F9 re-engineering:**
- Don't depend on +0x1FA exclusively. Compute the actual geometric containing triangle from player position each tick (point-in-triangle test against the local triangle and its neighbors first, falling back to a broader search if needed).
- If geometric tri differs from engine tri, prefer the geometric tri for steering decisions.
- Alternative: forcibly nudge the player's position by a few units when stuck to make the engine reclassify. Less elegant.

This finding may affect many F9 stuck-thrashing scenarios that look like "navigation got confused" -- the engine's tri ID may have lagged behind the actual position.

### 12. Funnel waypoints are more reliable than corridor-level steering

**Source:** v0.15.9.2.2 BAT, hypothesis for v0.15.9.2.3 fix.

The v06.17 corridor-level steering picks a target at the shared-edge midpoint between the player's current corridor triangle and the next one. The target is **derived from the engine's per-tick tri ID** (see Finding 11). This makes corridor steering vulnerable to stale tri ID reads.

Funnel waypoints, in contrast, are **position-based**: `FunnelPath` computes them at A* time from the portal sequence, storing (X, Y) coordinates. The mod tracks which waypoint is current via `s_waypointIdx`, advancing as the player gets close (distance threshold + overshoot detection). No per-tick tri ID dependency.

**For F9 re-engineering:**
- Prefer funnel waypoints for primary steering. Use corridor steering only as an opportunistic precision enhancement (e.g., when the funnel waypoint is far away and the player is close to a portal edge).
- If corridor steering is used, validate that the engine tri ID is consistent with the player's actual position (e.g., is the player geometrically inside the reported tri?). If not, fall back to funnel waypoints.
- Consider running the corridor steering only when the player's position-to-tri relationship is fresh (e.g., reset a freshness flag whenever the engine tri ID changes, expire it after N ticks of no movement).

### 13. ANY axis-level keyboard/analog conflict freezes movement (not just full opposition)

**Source:** v0.15.9.2.2 and v0.15.9.2.3 BAT confirmed empirically.

Finding 2 (keyboard and analog vote) implied conflict throttles movement. Initial hypothesis (v0.15.9.2.2): perfect 2D opposition causes total freeze. v0.15.9.2.3 BAT refined this: **even SINGLE-AXIS conflict freezes movement, not just full 2D opposition**.

v0.15.9.2.1 had partial opposition: kb=UP (north only, no X bit) vs analog=SW. Y axis fully opposed (U vs S), X axis unopposed (no kb X bit, analog says W). Player drifted slowly west.

v0.15.9.2.2 made keyboard fully consistent with long-range target (kb=DR = south-east) vs analog=NW (corridor backward). Both X and Y opposed. Player totally frozen.

v0.15.9.2.3 fixed the corridor backward issue (funnel waypoint correctly SW of player, analog SW). Keyboard still kb=DR (long-range SE target). Y agrees (both south). X disagrees (kb east, analog west). **Player still totally frozen.**

So the lockup isn't "both axes oppose" -- it's "any axis opposes." When FF8 sees `keyboard wants right + analog wants left`, the movement vote for that axis is presumably zero or near-zero, and the small remaining vote on the other axis can't drive movement either (perhaps because some validation gate requires both axes to be coherent).

**Counter-intuitively, making the keyboard MORE aggressive in pointing toward the target WORSENS movement when the analog points elsewhere.** The keyboard's job is to wake up FF8's movement code, not to direct movement. If the wake-up direction conflicts with the analog direction on ANY axis, FF8 fights itself.

**For F9 re-engineering:**
- The keyboard heading bitmask must approximate **the same direction as the analog**, not the long-range target. (Inverse of v0.15.9.2.2's Fix A.)
- Specifically: derive keyboard from the analog values that `SetAnalogFromVector` is about to write. If analog lX is positive, set DIR_RIGHT; if negative, DIR_LEFT; etc. (DirectInput convention: lX +1000 = screen right, lY +1000 = screen down. Arrow keys are screen-relative: DIR_RIGHT = screen right, DIR_DOWN = screen down.)
- Apply a threshold (~100) so small analog wobbles don't flap keys.
- Dominant-axis fallback if both lX and lY are below threshold.
- This makes keyboard a pure consequence of the analog steering choice, not an independent voice.

v0.15.9.2.4 implements this for chase-drive. If the BAT succeeds, the same fix should be applied to F9 path-finding (removing the chase-drive gate).

**Hypothesis to be confirmed by v0.15.9.2.4 BAT:** with kb derived from analog (so both inputs point SW), party will walk SW toward funnel wp 0 at full walking pace. If party STILL doesn't move, the lockup is more fundamental than kb/analog conflict and we need to investigate engine-side movement validation (e.g., the engine refusing to leave a stale tri ID).

**v0.15.9.2.4 BAT confirmed this fix.** Party walked 1,560 units through 7 waypoints in ~3 seconds with kb-from-analog. The kb/analog conflict was indeed the freeze cause.

### 14. Funnel waypoints can be camera-unreachable when one world axis is squashed

**Source:** v0.15.9.2.4 BAT.

The funnel algorithm picks waypoints at portal vertices that minimize the path length around bends. On rotated cameras where one world axis is squashed (Finding #4), a waypoint displaced primarily along the squashed axis is effectively unreachable -- the player oscillates around the wp's perpendicular line at a constant distance equal to the squashed-axis displacement.

Example from v0.15.9.2.4 BAT on domt5_1:
- Player at `(-978, 1741)`. wp 7 at `(-878, 1752)`. Displacement: `+100 east, +11 north`.
- Camera squashes X-world axis (`det(camRight, camDown) ≈ -0.07`).
- Player can move Y (north/south) easily, but barely move X (east).
- Player oscillates between `Y=1741` and `Y=1770` (overshoots `Y=1752` every cycle), stays at `X=-978`. Distance to wp 7 stays at ~100 forever. FUNNEL_ARRIVE_DIST=60 never triggers.

The overshoot detection requires getting within WP_OVERSHOOT_CLOSE first (likely < 100), so it also doesn't fire.

**For F9 re-engineering:**
- When stuck at a waypoint (no-progress detected), advance the waypoint instead of trying to recover in place. The current waypoint may be camera-unreachable; the next one might be reachable.
- Consider "camera-aware" waypoint selection: at funnel time, check each candidate waypoint against the camera projection. If a waypoint requires significant movement along a squashed axis, prefer the alternative portal vertex (L vs R).
- Or: relax FUNNEL_ARRIVE_DIST when the displacement to the waypoint is primarily along the squashed axis. E.g., if player's `dx_world > 5 * dy_world` and camera squashes X, increase the arrive threshold accordingly.
- A simpler heuristic: track `s_wpMinDist`. If it stabilizes (oscillates) for N ticks without decreasing, advance the waypoint.

v0.15.9.2.5 implements the simplest version: on no-progress detection (existing infrastructure), advance the waypoint. Pragmatic workaround until the camera-aware approach is built.

## Suggested F9 re-engineering priorities

Order by impact (updated after v0.15.9.2.4 BAT):

1. **Derive keyboard from analog values, not from target direction** (finding #13). CONFIRMED: eliminates the kb/analog conflict that throttles or freezes movement. v0.15.9.2.4 BAT walked 1560 units through 7 waypoints with this fix.

2. **Advance waypoint on no-progress stuck** (finding #14, NEW). When player is oscillating around a camera-unreachable waypoint, advance to the next one instead of trying to recover in place. v0.15.9.2.5 implements this.

3. **Compute geometric containing triangle each tick** (finding #11). Replace +0x1FA reads with a local point-in-triangle test. Removes the stale-tri-ID failure mode.

4. **Prefer funnel waypoints over corridor-level steering** (finding #12). Use corridor steering as opportunistic enhancement only when tri ID is verified fresh.

5. **Fix recovery's perp direction** (finding #3). Target the shared edge midpoint, not the next-tri centroid.

6. **Adaptive timeout** (finding #6). `maxTicks` based on startDist / pace.

7. **Velocity-aware recovery** (finding #7). Don't fire recovery while making progress.

8. **Camera-aware waypoint selection** (finding #14 advanced). At funnel time, prefer waypoints reachable through the current camera projection. Specifically, prefer portal vertices whose displacement from the player projects strongly through the camera axes.

9. **Rotated-camera detection and fallback** (finding #4). When cross-product determinant is near zero, expect 1D movement; don't generate orthogonal steering.

10. **Pre-calibration on field entry** (finding #10). Removes the "first drive eats calibration time" penalty.

## What chase-drive proved works

- Path-finding (A*+funnel) computes correct routes end-to-end.
- Calibration produces correct camera axes (consistent across v0.15.9.2.1 and v0.15.9.2.2 BATs on the same field).
- Fake gamepad install/remove is reliable.
- SetAnalogFromVector projection is correct.
- Walking modifier (W key) holds correctly across long drives.
- chase_auto_pilot's per-field config + mode routing is a clean pattern.
- Updates to keyboard bitmask DO take effect tick-by-tick (v0.15.9.2.2's Fix A produced consistent kb=DR throughout BAT).

The **routing** layer is fine. The **execution** layer (steering / recovery / heading / input coordination) is where the bugs cluster. F9 re-engineering should focus on the steering pipeline.

## Lower priority

- Investigate FF8's walking-vs-running speed mechanics (finding #9).
- Consider removing the heading-fallback entirely (finding #8). If origDx/origDz can fall through to zero (e.g., player is already on top of target), the drive should arrive, not press UP.

## Test strategy for F9 re-engineering

For each F9 fix:

1. Identify the offending failure mode (which finding above).
2. Build a test on a known-stuck field. Aaron has reported F9 issues on multiple fields; pick one to use as a canary.
3. Add periodic [drive] logging that surfaces the failure-mode signal (e.g., for finding #1: log kb-vs-analog direction agreement on every periodic log line).
4. Fix one thing at a time. Each BAT confirms a single hypothesis.

## References

- v0.15.9.2.1 BAT field log: 16:32:42 - 16:33:02.
- v0.15.9.2.2 BAT field log: 17:05:02 - 17:06:00 (party frozen at (-989, 3195)).
- v0.15.9.2.3 BAT field log: 17:25:16 - 17:26:18 (party frozen at (-989, 3195), steer correctly at funnel wp).
- v0.15.9.2.4 BAT field log: 17:50:16 - 17:51:38 (MAJOR PROGRESS: walked 7 wps, 1560 units; stuck at wp 7 due to camera-unreachable target).
- v0.15.9.2.5 implementation: `src/field_nav_autodrive.inl` (advance wp on no-progress for chase-drive).
- v0.15.9.2.4 implementation: `src/field_nav_autodrive.inl` (SetAnalogFromVector reorder, kb-from-analog for chase-drive).
- v0.15.9.2.3 implementation: corridor-steering gate.
- v06.17 corridor steering: same file.
- Camera calibration: same file, `s_calibPhase` state machine.
- `Plan & Research Documents/Auto-Drive Architecture Review.md` -- existing review of the architecture, may need refresh post-v0.15.9.2.x.
- `Plan & Research Documents/Field Navigation Improvements Plan.md` -- existing plan, may overlap.
