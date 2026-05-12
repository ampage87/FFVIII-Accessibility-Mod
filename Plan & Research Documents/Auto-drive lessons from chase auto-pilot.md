# Auto-Drive Lessons from Chase Auto-Pilot

**Date:** 2026-05-10 (initial), updated 2026-05-11/12
**Source versions:** v0.15.9 -- v0.15.9.7.4
**Author of notes:** Claude (with Aaron's testing data)

## Aaron's cardinal-direction terminology (the ground truth)

Aaron describes chase-field directions in cardinal terms. Always translate his natural-language descriptions to this table BEFORE making direction-code changes. Phrases like "left and slightly up" are ambiguous (could mean screen-direction or position-on-the-trail); cardinal terms are unambiguous and map directly to `(dirX, dirY)` via `DD_DirsToArrowMask`.

| Aaron's word | Arrow keys | (dirX, dirY) |
|---|---|---|
| southwest | Down + Left | (-1, +1) |
| south | Down | (0, +1) |
| southeast | Down + Right | (+1, +1) |
| west | Left | (-1, 0) |
| northwest | Up + Left | (-1, -1) |
| north | Up | (0, -1) |
| northeast | Up + Right | (+1, -1) |
| east | Right | (+1, 0) |

If Aaron describes a recipe in non-cardinal terms, ASK him to translate to cardinals before coding. See Finding #31 for the v0.15.9.7.3 BAT failure that this rule prevents.

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

### 15. INF gateways are the engine's actual screen-transition mechanism on chase fields

**Source:** v0.15.9.2.14 BAT diagnosis, v0.15.9.2.15 implementation.

Until v0.15.9.2.13, chase-drive used point-distance arrival ("within N units of target") as its completion signal. Each chase field had either an explicit `MODE_TARGET` config or v0.15.9.2.6's cluster-center fallback. Both are wrong: **chase fields exit via screen-transition line crossings, not point-distance arrivals.**

The engine fires the field transition when the player walks across a specific line segment. Those segments live in the field's `.inf` file as INF gateways. The INF parser already runs at field load and produces, for `domt2_1`:

```
gw[0] line=(-497,-3414)->(311,-3414) center=(-93,-3414) dest='bgmon_5'  (south exit)
gw[1] line=(482,1825)->(118,1351)   center=(300,1588)  dest='bgryo1_8' (entry-back)
```

The auto-pilot wasn't reading the line endpoints. v0.15.9.2.15 added `lineX1, lineY1, lineX2, lineY2` to `FieldArchive::GatewayInfo`; `LoadINFGateways` stores them. `StartChaseDrive` accepts the four endpoints and writes them to new state vars `s_driveCrossLineX1/Y1/X2/Y2`. `UpdateAutoDrive`'s crossing block reads from those state vars for chase-drive (decoupled from `s_capturedLines`).

**Implication:** chase fields without explicit per-field config get a valid line-crossing target from the generic INF-gateway fallback. No per-field code required for most chase fields. v0.15.9.2.15 BAT traversed five chase fields end-to-end on this mechanism alone.

### 16. Direction-aligned gateway selection beats nearest-gateway selection

**Source:** v0.15.9.2.15 design analysis.

When a field has multiple INF gateways (typical: forward exit + entry-back), the entry-back gateway is often **geometrically closer to the cluster centroid** than the forward exit. On `domt2_1`:

```
player (520, 1391) -> cluster (276, -728):     direction (-244, -2119)
player -> gw[0] (-93, -3414):                   distance ~2711
player -> gw[1] (300, 1588):                    distance ~2316  (closer)
```

Distance-only would pick gw[1] -- the entry-back. Wrong. **Cross-product alignment** (dot product of `player->gateway` and `player->cluster` > 0) correctly selects the forward-progress gateway:

```
dot(player->gw[0], player->cluster) = +10,331,967   SELECTED
dot(player->gw[1], player->cluster) =    -363,763   rejected
```

The cluster centroid represents "where the corridor leads"; a gateway aligned positively with that vector is on the path forward.

**For F9 re-engineering:** when picking among multiple candidate exit points (gateways, trigger lines, or otherwise), use direction alignment with the long-range target, not distance. Distance-only heuristics fail predictably on fields where the player enters from one side and exits through another.

### 17. Cluster-center fallback can land inside the spawn-arrival radius

**Source:** v0.15.9.2.6 BAT, v0.15.9.2.11 fix.

v0.15.9.2.6's generic fallback used the largest dead-end cluster center as target with point-distance arrival. On `domt2_1`: player spawns at `(444, -500)`, cluster center is `(276, -727)`, distance 282, default arrive distance 300. Chase-drive reports "Arrived" on engagement; chase_auto_pilot disengages and re-engages next tick. Loop runs at ~60 Hz, flooding the log with ~9 KB/sec of identical engage/arrive lines.

**v0.15.9.2.11 fix:** per-field completion marker. After Disengage with reason "chase-drive completed", mark `s_completedField = engaged field`. Refuse re-engagement on the same field until field change.

**For F9 re-engineering:** always check `startDist > arrive_distance` before engaging. If not, the engage is a no-op and should report as a configuration error, not silently loop. Per-field completion markers prevent re-engagement on already-completed drives.

### 18. Point-distance arrival is the wrong mechanism for chase fields

**Source:** v0.15.9.2.12 and v0.15.9.2.13 BATs.

v0.15.9.2.13 tightened arrive distance from 300 to 60 units. BAT result: party reached one more waypoint than v0.15.9.2.12 (wp 16 instead of wp 15), but still "Arrived" while ~300 units from the actual screen-transition exit. Tightening the threshold gets a few more waypoints but doesn't fix the architecture.

The fundamental issue: chase fields exit via line-crossing (Finding #15), not point-distance. Arrival semantics depend on what kind of target you have.

**For F9 re-engineering:** every drive target should carry its arrival semantics with it. NPC-target drives use point-distance (NPC is a point). Field-exit drives use line-crossing (gateway is a line segment). The drive infrastructure should support both via a tagged target type, not implicitly assume point-distance.

### 19. SETLINE Line entities have semantic types; filter accordingly

**Source:** v0.15.9.2.14 BAT diagnosis.

`field_navigation.cpp` classifies SETLINE Line entities into: `camPan`, `screenBd`, `event`, `interact`, `unknown`. v0.15.9.2.14 tried trigger-line crossing detection as the chase-drive arrival mechanism, filtering on `SCREEN_BOUND` or `UNKNOWN`. Field log on `domt2_1`:

```
[fieldload] lineType assigned: 2 captured, 2 mapped
            (camPan=0 screenBd=0 event=2 interact=0 unknown=0)
ent0 cat=1 type=Event Trigger sym='squall' param=-1
ent1 cat=1 type=Event Trigger sym='zell'   param=-1
```

Both Line entities on this chase field are Event Triggers -- they're kani (X-ATM092) battle-summon triggers, not screen transitions. The `SCREEN_BOUND or UNKNOWN` filter correctly rejected them, but that meant `GetTriggerLineNearestCluster` returned false on chase fields.

**Implication:** on chase fields specifically, SETLINE Line entities are reserved for battle/event triggers, not screen-transition geometry. INF gateways are the alternative.

**For F9 re-engineering:** know the semantic of each Line entity. When looking for screen-transition geometry, use INF gateways first; `SCREEN_BOUND` SETLINE lines second; `UNKNOWN` SETLINE lines third (lowest confidence). Don't conflate trigger-line types with screen-boundary types.

### 20. Three-tier fallback handles every chase field encountered so far

**Source:** v0.15.9.2.15 implementation, BAT.

`BuildFallbackConfig` orders crossing-target sources by reliability:

```
Tier 1: GetGatewayNearestCluster     -> INF gateway, line-crossing arrival.
Tier 2: GetTriggerLineNearestCluster -> SCREEN_BOUND/UNKNOWN SETLINE, line-crossing.
Tier 3: GetLargestClusterCenter      -> cluster center, point-distance arrival.
```

v0.15.9.2.15's BAT traversed five chase fields beyond `domt5_1` (which has an explicit config). All used the generic fallback, primarily Tier 1 (INF gateway). No per-field code beyond the two pre-existing explicit configs (`domt4_1` direction RUN LEFT, `domt5_1` target with walk=true).

**For F9 re-engineering:** the three-tier preference generalizes beyond chase auto-pilot. Whenever a drive target must be inferred from field geometry alone (no NPC, no explicit destination), the three-tier pattern applies. The arrival semantic varies by tier; the drive infrastructure must handle both line-crossing and point-distance.

### 21. Steering pace is the next deferred problem

**Source:** v0.15.9.2.15 BAT (`battles_suppressed=11`).

X-ATM092 catches the party whenever the scripted distance closes. A skilled player completes the chase with zero or very few catches by moving efficiently. v0.15.9.2.15's BAT reported `battles_suppressed=11` -- eleven catches NO-OP'd by cap=0. The chase progressed because catches don't end the chase, but each catch is the robot reaching the party. **The auto-pilot is too slow.**

Possible contributors, not yet measured:

- **Calibration overhead.** First drive on each field eats ~48 ticks before navigation starts. Aggregated across the chase route, meaningful.
- **Reactive waypoint advance.** Funnel waypoints advance only on arrival proximity or no-progress detection. Tight bends could be cut more aggressively with anticipatory advance.
- **Walking when running would suffice.** v0.15.9.2.12 set fallback walk default to false (running). `domt5_1`'s explicit config still says walk=true per Aaron's AI rule #1 (mountain-shake battle on running). On fields without that rule, walk=true unnecessarily throttles pace.
- **Recovery / advance-on-stuck fires during normal traversal.** Each fire interrupts main steering for several ticks. Sensitive thresholds fire on healthy progress.
- **Conservative analog values.** `SetAnalogFromVector` saturates at ±1000. Whether real walking pace requires sustained ±1000 or is throttled lower hasn't been measured directly.

**Deferred until full-chase completion works** (Finding #15-20 enabled multi-field traversal; finishing the chase requires extending the chase-fields list to include 2-3 more fields after dotown_3 up to the Lapin Beach FMV). Once the full chase reliably completes, address steering pace and target zero catches.

### 22. MODE_DIRECTION is the right escape hatch for chase town fields

**Source:** v0.15.9.2.16, .17, .18 BATs (2026-05-11).

The Dollet town chase fields (`dotown_3`, `dotown_2`, `dotown_1`) behave differently from the mountain trail. They use SETLINE-triggered scripted animations to teleport the party between street segments. Two failure modes hit chase-drive on these fields:

- **Fragmented walkmesh (dotown_2).** A* finds no path because spawn, exit gateway, and trigger-line bridge are on disconnected walkmesh islands. v0.15.9.2.16 BAT: `[A*] No path from tri 45 to tri 77 (48 iterations)`. chase-drive STARTED with `waypoints=0`; calibration's ~500-unit movement triggered the arrival check trivially (no waypoints means "all waypoints reached" is vacuously true); v0.15.9.2.11's completion marker locked the field out permanently.
- **Field-entry input block (dotown_1).** Walkmesh IS connected (65 valid waypoints), but `[CALIB] phase 1 FAILED: no movement (dist=0.0)` -- the engine doesn't respond to the lX=+1000 test input during the first second of the field. Without proper camera axes, subsequent path-finding analog steering is approximate; party moved 3300 units in the correct general direction then drifted off-course and cycled velocity-stuck / wp-skipping for 24 seconds before drifting into the FMV trigger by chance.

Neither failure is reliably fixable in chase-drive itself. Both are fixed by switching the field to `MODE_DIRECTION dirY=+1 walk=false` (screen-down, running) in `chase_auto_pilot`'s `kFieldConfigs`. Direction-mode bypasses path-finding entirely (no A* dependency), bypasses calibration (screen-relative direction signs don't need camera axes), and never reports arrival (no target), so the party walks continuously south until the field changes. The chase-scene SETLINE scripts handle the segment-to-segment transitions; the auto-pilot just provides directional pressure.

v0.15.9.2.18 BAT result: dotown_2 cleared in 14s (vs 41s manual baseline), dotown_1 cleared in 9s (vs 24s of stuck cycling in v0.15.9.2.17), zero catches on either town field. The chase scene now completes end-to-end.

**For F9 re-engineering:** the principle generalizes. When path-finding or calibration is unreliable on a particular field, falling back to direction-mode (with an explicit per-field direction config) is often more robust than trying to fix the underlying machinery. Direction-mode has fewer failure modes by construction. The right architecture is to make MODE_DIRECTION a first-class fallback that any drive can degrade into, rather than only the explicit-config path.

### 23. cap=0 is the wrong success metric; catches per chase is the right metric

**Source:** v0.15.9.2.18 BAT (2026-05-11).

The `chase_battle_freeze` cap=0 mechanism (which NO-OPs the engine's `BATTLE` opcode during AUTO mode chases) prevents catches from triggering battles. It's how the chase "completes" reliably -- catches happen but don't end the chase or kill the party. However, **it masks navigation quality.** A perfectly-driven chase has zero catches: X-ATM092 never reaches the party, the kani-freeze module never has work to do, the BATTLE opcode never fires. A poorly-driven chase has many catches but still "succeeds" thanks to cap=0.

v0.15.9.2.18 BAT recorded 11 NO-OPed catches across the chase (2:35 total). Catches concentrated on the mountain trail (catches #1-#9) and the bridge (catches #10-#11), with zero catches on the post-bridge town fields. Aaron's framing: "our current battle nope system is a band-aid for poor navigation. We need to streamline navigation in each field."

**Implications for future work:**

- Keep cap=0 as a safety net through refinement. Without it, every navigation regression breaks the chase. Remove it only when multi-BAT testing shows reliably zero catches.
- Track catches per field as the metric of navigation quality, not total chase time alone. A long chase with zero catches is better than a short one with five.
- Aaron's AI rules become the highest-impact targets: rule #1 (don't run on domt5_1) is enforced by `walk=true` in the config and seems sufficient; rule #2 (reverse direction on doopen2a bridge leaps) is unimplemented and produces multiple catches per chase. The bridge trick is the single biggest quality improvement remaining.

### 24. Three-signal direction selection for chase fields with default camera

**Source:** v0.15.9.3 BAT (2026-05-11) + v0.15.9.4 implementation.

When the chase script and the auto-pilot's analog input both contribute to party motion, the analog direction matters even if the script dominates. The v0.15.9.1 config for `domt4_1` shipped with `dirX=-1, dirY=0` (RUN WEST) based on Jegged's strategy guide. v0.15.9.3 diagnostic data showed this was the worst possible direction — the previous config had survived 17 versions only because cap=0 NO-OPed the resulting catches.

**Three independent signals identify the correct direction:**

1. **Camera orientation** (derived from a clean analog-effect sample). On `domt4_1`, sec 9 of engaged drive (between catches #2 and #3) showed `analog=(-1000, 0)` producing world `delta=(-61, +6)` over one second. `camRight ≈ (1, 0)`, `camDown ≈ (0, -1)` — default. So `dirX=+1` maps to world east; `dirY=+1` maps to world south.
2. **Kani approach direction** (read from per-second `kani=(kX,kY)` log at engage moment). Flee direction is the *opposite* of the kani-to-party vector, with a sign convention adjustment for screen vs world axes.
3. **Field exit direction** (read from the party's final position before the field transition, compared to engage-moment position). The script's catches generally teleport the party toward the exit, so the net trajectory across the chase indicates exit direction.

When all three signals agree, the answer is unambiguous. On `domt4_1`: kani is north-west of party at engage, field exit is south-east of spawn, default camera maps `(+1, +1)` to south-east. Agreement → `dirX=+1, dirY=+1` is the right config.

**A fourth check is a sanity test:** does the chase script's *own* intro animation point in the same direction? Pre-engage `ChaseActiveDiag` lines show party motion before the auto-pilot engages — driven entirely by the script. On `domt4_1`, the script moves the party from spawn to `(-657, 3132)` over 2 seconds, a delta of `(+573, -567)` — also south-east. Confirming that our analog now cooperates with rather than fights the script's natural flow.

**For F9 re-engineering:** the principle is general. When deciding what direction to drive a player in a script-influenced scenario, use the script's own movement during the pre-control phase as a hint. The script knows where the player is supposed to go; reading its choreography is faster than guessing or trying directions empirically.

**Inverse principle for diagnostic instrumentation:** the data collection that exposed this (Finding #24) is itself a generalizable pattern. Adding pre-engage / chase-active logging that runs from the moment a script-driven scenario activates (not from the moment our controller engages) gives us visibility into the script's setup phase. Combined with movement-delta computation that's independent of analog-direction assumptions, we can derive camera orientation, script behavior, and correct direction from the same dataset. This pattern (pre-engage diagnostic + delta logging) should be the default first move for any future script-influenced refinement work.

### 25. Three-signal direction selection: empirical validation and the analog-as-force-multiplier insight

**Source:** v0.15.9.4 BAT (2026-05-11) on `domt4_1` and v0.15.9.5 application to `domt3_2`.

Finding #24 proposed three-signal direction selection. v0.15.9.4 BAT validated it empirically: `domt4_1` went from 3 catches / 16 seconds (WEST, all three signals pointing south-east) to 0 catches / 3 seconds (SOUTH-EAST, signals satisfied). Cascade benefit on `domt5_1` (44s → 30s) and `domt2_1` (lost 1 catch) from auto-pilot arriving in better shape. Total chase: 2:35/11 catches → 2:06/7 catches.

More importantly, the BAT exposed a quantitative insight about analog effectiveness on script-driven chase fields:

**Analog is a ~15x force multiplier when cooperating with the script's flow.** On `domt4_1` with WEST (fighting the script), the cleanest between-catches analog sample was `dmag=61` per second. With SOUTH-EAST (cooperating with the script), the first engaged second produced `dmag=905`. Second second: `dmag=888`. **Sustained running speed**, in roughly the analog direction, throughout the engaged phase. Not subtle.

The implication is that pre-BAT pessimism about "the chase script overrides our analog" was wrong. The script doesn't override; the script and analog *combine vectorially*. When they agree, the resulting motion is the sum (running speed in the agreed direction). When they disagree, the resulting motion is the difference (near-zero motion as the script pulls one way and our analog pulls another).

This raises the stakes on direction selection significantly. Picking the wrong direction isn't "suboptimal" — it's actively pulling the party against the script and producing near-zero motion. Picking the right direction unlocks the full running speed of cooperation.

**For the lessons stack:** the three-signal pattern should be applied to every chase field before defaulting to MODE_TARGET. The savings are real — fewer catches AND faster transit. The diagnostic instrumentation (Finding #24's `ChaseActiveDiag` log) provides everything needed: camera derived from clean analog samples, kani approach from engage-moment positions, exit direction from net trajectory across the chase.

**On the kani-co-location case (domt3_2 in v0.15.9.5):** when the chase script teleports party and kani to the same position at field entry, the kani signal is null — there's no "flee direction" because they're co-located. In this case, two-of-three is still enough to make a decision (camera + exit). The catch is likely unavoidable (script-forced) but the transit can still be optimized for speed by skipping CALIB and using direction-mode.

**Inverse principle: CALIB-on-script-frozen-fields is wasted overhead.** When the chase script freezes the party at field entry (e.g., for an ASK, or because of co-location), CALIB's analog probe produces no movement and the calibration fails. The defaults are kept anyway, so we can detect this case (CALIB failure with default fallback) and prefer `MODE_DIRECTION` over `MODE_TARGET` on such fields. The path-finder isn't producing value when the geometry is trivial and the camera is default.

### 26. Time-based catches and the four-requirement diagnostic for script-driven chase fields

**Source:** v0.15.9.5 BAT analysis on `domt5_1` (2026-05-11) + Aaron's recipe + v0.15.9.6 application.

Not all chase-field catches are distance-based. On `domt5_1` (Mountain Hideout 7, the west trail), catches fire roughly every 6 seconds while party is on the field, regardless of party position or kani distance. The kani entity reads as a static `(0, 0)` -- the kani's position isn't tracked because the catch isn't triggered by proximity. The chase script just runs a timer.

**Implication for time-based fields:** the catch budget is `field_time / catch_interval`. With v0.15.9.5's 32s on domt5_1 and 6s interval, that's 5.3 catches' worth of opportunity; 3 actual catches fired (some interval slack). To get to 0 catches, transit must be under one catch interval.

This is fundamentally different from distance-based catches (e.g., `domt4_1`) where you can keep the kani at bay by moving fast enough that it can't close the gap. Time-based catches don't care about kani-party distance; they care about how long you spend on the field.

**Aaron's recipe for time-based chase fields** (formalized from his 2026-05-11 description of `domt5_1`):

1. **Walk** if the field has a no-running constraint (AI rule like mountain shake).
2. **Head directly for the next field exit** -- sustained motion toward where the exit lives, not waypoint-by-waypoint navigation.
3. **No hangups** -- nothing that causes velocity-stuck pauses, waypoint re-evaluation, or steering adjustments.
4. **No delay** -- no startup overhead like CALIB.

The four requirements are diagnostic. For any chase field, score each mode (MODE_DIRECTION vs MODE_TARGET) against the four:

- MODE_TARGET path-finding **fails 3 and 4 always** (waypoint state machine has velocity-stuck recoveries; CALIB consumes the first second). It satisfies 2 only when the path-finder's route is a straight line.
- MODE_DIRECTION **satisfies 3 and 4 always** (no state machine, no CALIB). It satisfies 2 when the chosen direction aligns with the exit. It satisfies 1 via the `walk` flag.

**MODE_DIRECTION is the default choice for time-based catch fields.** The CALIB delay alone is a meaningful fraction of the catch interval; the velocity-stuck pauses can easily double the field time. The price is direction choice: you must pick a screen direction that walks straight through the geometry.

**Detecting time-based catches** from BAT diagnostic data:

- Catches fire at roughly even intervals (~6 seconds in `domt5_1`'s case)
- Kani entity reads as static `(0, 0)` or a fixed position -- not tracking party
- Catch interval is independent of party speed or distance to kani

Vs distance-based catches:
- Catches fire when kani enters proximity range (varies)
- Kani entity is dynamic, tracking party position
- Catches accelerate when party stops or slows

**For F9 re-engineering:** if F9 encounters a comparable time-based mechanic (e.g., a stealth section with a patrol timer), the same recipe applies. Direct, no-hangup, no-delay analog steering beats a waypoint-state-machine path-finder when the failure mode is a timer rather than collision.

**Inverse principle: don't use MODE_TARGET when the path is straight enough for direction-mode.** The path-finder's overhead is only worth paying when the geometry genuinely requires multi-segment navigation. For trails, corridors, and any field with a roughly linear exit-direction, MODE_DIRECTION is faster and more reliable.

## Finding #27: S-curve trails require staged directions; single-direction MODE_DIRECTION freezes at the first wall that contradicts the held direction

**Source:** v0.15.9.6 BAT failure on `domt5_1` (2026-05-11 18:31–18:34) + Aaron's keyboard recipe + v0.15.9.7 application.

Finding #26 said MODE_DIRECTION is the default choice for time-based catch fields. v0.15.9.6 BAT'd that on `domt5_1` with `dirX=+1 dirY=+1` (south-east, the world-direction from spawn `(-1057, 3301)` to exit `(382, 235)`). The party drove into the east wall at `(-1026, 3301)` within one second of engagement, then froze for ~30 seconds while the script forced brief bursts of motion ending at `(-751, 2325)`. Party never reached the south exit.

Aaron's keyboard recipe explained why:

> "very southwest initially, then go south, then south east at the end."
>
> "if you press down at first you won't move, you have to press both down and left."

The trail is **S-shaped**, not straight. From spawn the trail runs south-west to bypass a wall on the east side. After the first bend the trail runs pure south through the middle. After the second bend it runs south-east to the exit. Three segments, three different directions.

Single-direction MODE_DIRECTION fails on this geometry by definition: whatever direction is held is wrong for at least one of the three segments. The party hits a wall in that segment, the walking-cycle debounce expires (Finding from v0.15.9.1.1), and motion stops. The script's bursts can budge the party slightly but not navigate.

**The exit-direction heuristic from Finding #25 is a necessary but not sufficient condition.** It works when the trail is roughly straight from spawn to exit (`domt4_1`, `domt3_2`, `dotown_2`, `dotown_1`). It fails when the trail bends, even if the bends average out to the exit direction.

**v0.15.9.7 solution: MODE_STAGED_DIRECTION.** A new drive mode that switches direction part-way through a field based on the party's current Y position (or X, or any other readable position component). Each staged field declares an array of `FieldStage` entries:

```cpp
struct FieldStage {
    int8_t  dirX;        // screen-relative direction sign, {-1, 0, +1}
    int8_t  dirY;        // screen-relative direction sign, {-1, 0, +1}
    bool    walk;        // hold W (walk modifier) during this stage
    int32_t activeMinY;  // stage is active when party Y >= this value
};
```

Stages are listed in DECREASING `activeMinY` order. `PickStageIdx` walks the array and returns the first stage whose `activeMinY` is `<= party Y`. The last stage has `activeMinY = INT32_MIN` so it always matches as a fallback.

At every Update tick the per-tick refresh re-picks the stage by current Y. If the picked index differs from the previously-active one, `s_engagedDirX/Y/Walk` are updated and the existing `StartDirectionDrive` call on the next line picks up the new analog/arrow values via its idempotent already-running branch (diff-based `SendInput`, see `field_nav_directiondrive.inl`). No stop/restart needed; the keep-alive pulse cycle continues uninterrupted across transitions.

**For `domt5_1`:**

```cpp
static const FieldStage kStages_domt5_1[] = {
    { -1, +1, true, 2200 },        // SW until Y < 2200
    {  0, +1, true, 1100 },        // S until Y < 1100
    { +1, +1, true, INT32_MIN },   // SE for the rest
};
```

Thresholds derived from BAT evidence:

- v0.15.9.1.1 BAT pure-south froze at Y=2217 (trail bends out of south-only corridor there). SW must extend past 2217; picked 2200 for SW→S.
- SETLINE call#14 center `(-366, 1110)` marks the final bend toward south-east exit. South-only would fail past this point; picked 1100 for S→SE.
- Spawn cluster center Y~3500; exit cluster center Y~235.

Aaron uses keyboard arrow keys (not analog). His "very southwest" is literally Down+Left pressed together, which maps directly to `(dirX=-1, dirY=+1)` via `DD_DirsToArrowMask`. The keyboard convention generalizes cleanly to stage parameters.

**Why this works mechanically:** `StartDirectionDrive`'s "already running" branch (added in v0.15.9.1.1) updates `s_analogDesiredLX/LY` in place and uses diff-based `SetHeldDirections` that only fires `SendInput` KEYUP/KEYDOWN when the arrow bitmask actually changes. So mid-engagement direction changes work cleanly: when we change `s_engagedDirX/Y/Walk` between ticks and the next `StartDirectionDrive(...)` is called, the analog values jump to the new direction, the arrow bitmask diff fires fresh KEYUP/KEYDOWN events, and the engine sees "new movement intent" without the walking-cycle debounce kicking in (because intent changed, not just sustained).

**Generalization to F9 path-finding:** the staged-direction pattern is a coarse approximation of waypoint-based steering. F9 has the full machinery (A*+funnel waypoint sequence + per-tick steering toward current waypoint) and doesn't need staged direction. But the **stage-transition mechanism** itself — update the heading via shared state and let the next per-tick refresh pick up the change — is the same idea as advancing a waypoint in F9's `UpdateAutoDrive`. The diff-based `SetHeldDirections` ensures the keyboard doesn't have to be released across transitions, which is the same property F9 needs when transitioning between waypoints on a curved path.

**Detecting S-curve geometry from BAT data:**

- Single-direction MODE_DIRECTION freezes the party within seconds of engagement at a wall that's perpendicular to the held direction.
- The freeze position is NOT at the exit; it's somewhere along the trail.
- Aaron's manual play confirms multiple direction changes are required to traverse.
- Walkmesh dump shows SETLINE triggers at intermediate Y positions (marking the bend points).

When these conditions are present, prefer MODE_STAGED_DIRECTION with thresholds derived from the SETLINE positions or empirical BAT freeze positions.

**Failure modes to watch for in v0.15.9.7 BAT:**

- Stage transition fires at wrong Y (threshold needs adjustment): symptom is brief progress in the new stage's direction followed by a freeze, with the transition log line showing position different from the intended bend.
- A stage's direction is wrong for its segment: symptom is freeze during that stage, with `STAGED stage transition` log lines showing the party never reaching the next stage's threshold.
- Y-only stage selection misses the geometry: if a field has X-axis bends too, stage selection on Y alone is insufficient. The `FieldStage` struct could be extended to support X-axis stages or 2D stage selection.

## Finding #28: Chase script intro choreography can swallow SendInput events; defensive re-press is required for modifier keys held across the swallow window

**Source:** v0.15.9.7 BAT partial success on `domt5_1` (2026-05-11 19:35) + v0.15.9.7.1 fix.

v0.15.9.7's `MODE_STAGED_DIRECTION` navigated the S-curve geometry correctly — the BAT progressed through the rest of the chase. But Aaron heard running footsteps on `domt5_1` instead of walking, and the kani arrived on its 6-second time-based catch timer. The W (walk modifier) press never reached the engine, even though the config and engage path were correct.

**The mechanism:** `StartDirectionDrive` in `field_nav_directiondrive.inl` fires `InjectKey(SC_W_CANCEL_DD, true)` exactly once in the fresh-start branch when a field with `walk=true` engages. Subsequent per-tick refresh calls hit the "already running" branch where the W diff check `if (walk != s_directionDriveWalk)` is a no-op because both sides stay `true`. If the chase script's intro choreography on field entry (camera pan, mountain shake setup, kani teleport) absorbs keyboard events from the SendInput queue before the engine processes them, the engine sees W as never-pressed forever while our state insists it's held.

**The arrow key keep-alive pulse (Finding from v0.15.9.1.1) already exists** for exactly this kind of failure mode — it re-fires arrow KEYDOWNs every 30 ticks. But the keep-alive header comment explicitly notes that W is NOT pulsed because release+re-press would cause one frame of run-speed motion between them. So the arrow pulse covers "new movement intent every ~0.5s" but doesn't cover "modifier key got swallowed."

**The fix:** add a press-only re-press for W. KEYDOWN events on a held key are idempotent at the OS level (Windows treats them as auto-repeat at the input layer; engines re-acknowledge the modifier press intent). Since we never release between presses, the speed-glitch concern doesn't apply.

v0.15.9.7.1 implementation:

```cpp
static const int WALK_REPRESS_PERIOD = 30;  // ~0.5s at 60fps
static volatile int s_walkRepressCounter = 0;
static volatile bool s_walkRepressLogged = false;

// In StartDirectionDrive's "already running" branch:
if (walk != s_directionDriveWalk) {
    InjectKey(SC_W_CANCEL_DD, walk);
    s_directionDriveWalk = walk;
    s_walkRepressCounter = 0;
    s_walkRepressLogged = false;
} else if (s_directionDriveWalk) {
    s_walkRepressCounter++;
    if (s_walkRepressCounter >= WALK_REPRESS_PERIOD) {
        InjectKey(SC_W_CANCEL_DD, true);  // press-only, no release
        s_walkRepressCounter = 0;
        // log first re-press at INFO, suppress after
    }
}
```

Reset on fresh-start and Stop. The walk-state-flip branch also resets so future MODE_STAGED_DIRECTION fields with mixed walk/run stages work cleanly.

**Generalization to other held inputs:** any modifier-style key held across a chase-script intro is vulnerable to the same swallow. Cancel/walk (W) is the only such modifier this mod uses today, but if future work needs to hold confirm (X / Enter) or menu (Esc) during chase scripts, the same defensive re-press pattern applies. The press-only-no-release rule is critical — toggling a modifier causes one frame of the un-modified behavior.

**Generalization to F9 path-finding:** F9 already moves between waypoints continuously, so arrow inputs change tick-to-tick and the engine sees "fresh movement intent" naturally. F9 doesn't currently hold any modifier keys (always runs). If F9 ever needs a walk modifier for slow-zones or stealth, the same defensive re-press applies.

**Detecting swallow from BAT data:**

- The `STARTED dir=(X,Y) walk=1` log line confirms our state sent `walk=1` to `StartDirectionDrive`.
- The audio says running.
- The `s_directionDriveWalk` state stays `true` for the entire field (no flips logged).
- The party's speed corresponds to running, not walking.

With the v0.15.9.7.1 fix in place, the `[direction-drive] walk modifier RE-PRESSED (defensive, period=30 ticks)` log line proves the re-press is firing at our level. If the audio still says running with that log line present, the failure is deeper than SendInput swallow — the engine is actively filtering or ignoring the W key during chase scripts, which would require either direct keyboard-state buffer writes or a chase-script-specific walk mechanism.

**Why the dotown_2/dotown_1 direction-drive engagements worked without this fix:** those fields have `walk=false`. The walk modifier was never pressed in the first place, so the swallow concern doesn't apply. The arrow KEYDOWNs are kept fresh by the existing keep-alive pulse. The fix only matters for `walk=true` fields.

## Finding #29: The engine commits run/walk decision on the first frame of field-load; defensive re-press must fire within that frame, OR the modifier key must be held across the field transition

**Source:** v0.15.9.7.1 BAT partial success (walking audio but robot still triggered) + Aaron's manual playthrough trace (2026-05-11 20:14-20:20) + v0.15.9.7.2 fix to period=1.

Finding #28 added a defensive re-press path firing every 30 ticks (~500ms at 60fps) for the W modifier in `StartDirectionDrive`'s "already running" branch. The v0.15.9.7.1 BAT confirmed that re-press worked at the audio level: Aaron heard walking footsteps on `domt5_1`. But the robot still triggered, indicating a catch fired despite the walking-audio confirmation.

Aaron then ran the chase manually (Original mode, no auto-pilot engagement), and the `ChaseActiveDiag PRE-ENGAGE` per-second log captured ground truth:

```
20:19:05  pos=(-1004,3253)  START domt5_1
20:19:06  pos=(-988,2965)   delta=(16,-288)   dmag=288   walking
20:19:07  pos=(-949,2682)   delta=(39,-283)   dmag=285
...
20:19:18  pos=(655,123)     EXIT     13s total, 0 catches
```

Every dmag is 267-303 — walking speed from the very first measured frame. Aaron's physical W press is held BEFORE the engine evaluates running-vs-walking on field-load. He never has any running-speed motion at all on this field.

**Implication:** the engine commits the running-vs-walking decision very early on field-load — within the first frame or two after `Engage` fires. That decision is sticky: once "player is running" is committed for the field, subsequent walking does not undo it for catch purposes. The catch fires on the time-based timer (~6 seconds on this field per v0.15.9.6 analysis) and the robot appears.

v0.15.9.7.1's `InjectKey` at fresh-start was either queued behind the engine's first read OR the script's intro reset keyboard state before the press could land. The re-press at t~500ms came too late — the catch flag was already set.

**Two fix paths, in order of complexity:**

### Fix path A (v0.15.9.7.2): Aggressive re-press cadence

Reduce `WALK_REPRESS_PERIOD` from 30 to 1. Re-press every tick (~17ms at 60fps). Worst-case latency from field-entry to engine-sees-W-down drops from ~500ms to ~17ms. If the engine's catch-eval window is wider than one frame, this lands W press inside it.

The `else if (s_directionDriveWalk)` branch already exists; just the constant changes. One-line patch.

Cost: 60 SendInput calls per second while walking. SendInput is microsecond-cheap. Repeated KEYDOWN on a held key has no semantic effect at the engine layer.

### Fix path B (v0.15.9.7.3, if needed): Keep W held across chase-field transitions

If path A's 17ms is still too slow — the engine reads keyboard at the exact load frame, BEFORE any SendInput dispatch can land — the only solution is to never let W go up between fields. Then there's no "W up" state for the engine to read on field-load; W has been continuously held since some earlier moment.

Implementation:

1. Add `bool keepWalkModifier = false` param to `StopDirectionDrive`. When `true`, skip the `InjectKey(W, false)` call and leave `s_directionDriveWalk` unchanged. `s_walkRepressCounter` / `s_walkRepressLogged` stay unchanged too.
2. `ChaseAutoPilot::Disengage` already accepts a reason. For field-change reasons (chase still active, just moving to next field), pass `keepWalkModifier=true`. For chase-ended reasons, pass `keepWalkModifier=false` (default).
3. `StartDirectionDrive`'s fresh-start branch must handle the case where `s_directionDriveActive=false` but `s_directionDriveWalk=true` (already held from previous field). Existing diff check `if (walk != s_directionDriveWalk)` already does this correctly — if the new field's `walk` matches what's already held, no SendInput; if it differs, fire the flip.

This means:

- Transition from a `walk=false` field to `domt5_1` (`walk=true`): the new field's `Engage` sees `s_directionDriveWalk=false` (held over from previous field's state) and `walk=true`, fires `InjectKey(W, true)`. Same race condition as before — doesn't help this case. **Need to pre-press W during the transition.**
- Transition from `domt5_1` (`walk=true`) to a `walk=false` field: with `keepWalkModifier=true` in `Disengage`, W stays held going into the new field. The new field's `Engage` sees `s_directionDriveWalk=true` and `walk=false`, fires `InjectKey(W, false)`. One frame of walking at the start of the new field, then back to running. Cosmetic; doesn't cause issues.

So path B alone doesn't solve the entry-to-walk-field problem. Combined with path A (period=1), the entry-to-walk-field still races, but path B avoids the W-up-on-transition for transitions FROM a walk field.

### Fix path C (v0.15.9.7.4 or later, if both A and B insufficient): Pre-press W during the previous field's lifetime

The only way to guarantee W is held before the engine reads keyboard on field-load is to have W pressed BEFORE the field-change fires. That means pressing W during the PREVIOUS field's last few ticks, before the fieldId change.

This requires knowing what field is coming next. `ChaseDetector` fires on `fieldId` change but doesn't know the new field's `walk` config until name debounce settles (2 seconds later). Workarounds:

- Hardcode "if fieldId changes to 0x0148 (domt5_1), pre-press W immediately on the fieldId change, before debounce."
- Or: maintain a static mapping from fieldId to walk config, queried at ChaseDetector level.
- Or: have `ChaseAutoPilot` watch for fieldId changes and pre-press W when the next field will be a walk field.

All of these introduce coupling between `ChaseDetector` and walk-config. Path A or B should be tried first.

**Generalization:** any input state that must be "already true" when the engine reads keyboard on field-load is vulnerable to the same race. The arrow KEYDOWNs work because their keep-alive pulse re-presses every 30 ticks regardless of state — but arrows are press-release cycle-resilient (the engine just sees "new movement intent" each cycle). Modifier keys like W don't have a cycle; they're "held continuously" or not. The engine's first-frame read of a modifier is the entire decision.

For any future modifier-key behavior (Confirm key held to talk to NPC, Esc held to skip cutscene, etc.), the same pattern applies: must be held BEFORE field-load.

## Finding #30: On some chase fields, the catch trigger is the analog-vs-script direction conflict, NOT the W (walk) modifier state. The walk modifier is a red herring or supporting condition; the primary predicate is whether the analog cooperates with the script's natural flee direction

**Source:** v0.15.9.7.2 BAT (period=1 W re-press confirmed walking from first frame, robot still triggered) + Aaron's pushback on direction ("mostly LEFT and slightly UP", inverse of our `dirY=+1` config) + v0.15.9.7.3 fix to flip dirY.

Findings #28 (defensive re-press path) and #29 (re-press cadence) were aimed at making W reach the engine reliably so the party walks instead of runs. v0.15.9.7.2 achieved that goal completely: Aaron confirmed audible walking-speed footsteps from the very first frame of `domt5_1`. The W press reaches the engine. But the robot still triggered.

That empirically rules out W as the cause of the catch. Something else triggers it.

Aaron's pushback surfaced the actual signal:

> "You mentioned the mod is pushing east on that field, but when I played through manually I went mostly LEFT and slightly UP."

Mapping to analog: Aaron's recipe is `(dirX=-1, dirY=-1)` (screen-up-left). Our v0.15.9.7-.7.2 config had `(dirX=-1, dirY=+1)` (screen-down-left). The Y axis was inverse of his manual recipe.

**Why this triggers a catch even with walking:**

The chase script forces the party to flee down the trail (world-Y-decreasing direction). With Aaron's analog cooperating (screen-up = world-Y-decreasing because the camera is Y-inverted on this field), the script and analog AGREE -- the party moves smoothly down the trail.

With our `dirY=+1` analog, we tell the engine "the player wants to go screen-down" which on this Y-inverted camera maps to world-Y-INCREASING (back up the trail, AWAY from the exit). The script overrides our intent and still forces the party down the trail (via walkmesh narrowness and script-forced motion bursts), but the engine sees the analog-vs-script DISAGREEMENT.

Apparently the engine interprets that disagreement as "the player is trying to go the wrong way" or "the player is panicking and pulling back from the chase." Either way, that's the predicate that gates the time-based catch (~6s on this field). When analog cooperates, predicate FALSE, timer doesn't tick, no catch. When analog opposes, predicate TRUE, timer ticks, catch at ~6s.

**Empirical evidence supporting this theory:**

- **Aaron's manual run:** 13 seconds on domt5_1, 0 catches. Analog (LEFT+UP) cooperates with script direction.
- **v0.15.9.6 BAT:** stuck on east wall at field-entry, never moved. Analog `(+1, +1)` opposed BOTH axes; party never escaped wall, never timed out for catch eval.
- **v0.15.9.7 BAT:** geometry navigated, but caught. Analog `(-1, +1)` agreed on X (LEFT correct) but opposed on Y. Script forced motion happened; catch triggered.
- **v0.15.9.7.2 BAT:** walking audible from first frame, but caught. Same `(-1, +1)` analog as v0.15.9.7; W modifier orthogonal to catch trigger.
- **v0.15.9.7.3 expectation:** analog `(-1, -1)` matches manual. No direction disagreement. Predicate FALSE. No catch.

**Generalization:**

For chase fields with this signature (time-based catch + camera rotated relative to default), the auto-pilot must match the player's manual recipe in BOTH screen axes, not just X. Walkmesh navigation success is NOT sufficient evidence that direction is correct -- the walkmesh forces world-coord paths regardless of analog, so observing "party reached the exit" doesn't validate the analog input.

The correct validation is:

1. **The player's screen-relative input (which arrow keys they press).** This is the analog direction the auto-pilot should match.
2. **Audio cues** (footstep cadence, presence/absence of robot announcements). These confirm walking vs running and catch firing.
3. **The chase event extract log** (catches and timing per field).

World-coord position deltas tell you what HAPPENED, not what the input WAS. Don't pattern-match "world goes south-east" to "analog should be SE" without checking the camera mapping.

**Three-signal direction analysis (Finding #25) still applies as a starting heuristic** for fields without a manual recipe, but if the BAT result is "navigated but caught" with the heuristic-chosen direction, the next step is to ask Aaron to play the field manually and capture his keyboard input directly. The ground-truth direction is what he physically presses, not what the world-coord delta suggests.

**Process improvement:**

For every chase field where the auto-pilot needs to set an analog direction, before the BAT, ask Aaron: "On this field, which arrow keys do you press, and roughly how long do you hold each?" His answer maps directly to `(dirX, dirY)` values via `DD_DirsToArrowMask`. The empirical world-coord deltas are confirmation, not the primary signal.

## Finding #31: ALWAYS translate the player's natural-language direction descriptions to cardinal terms BEFORE coding. Non-cardinal phrasing is ambiguous and produces shipping-quality errors

**Source:** v0.15.9.7.3 BAT FAILED (AD stuck at spawn on `domt5_1`) + Aaron's clarified cardinal-direction recipe.

After the v0.15.9.7.2 BAT, Aaron pushed back with: "You mentioned that the mod is pushing east on that field, but when I played through manually I went mostly LEFT and slightly UP."

I interpreted "LEFT and slightly UP" as screen-direction (dirX=-1, dirY=-1 = screen-up-left) and shipped v0.15.9.7.3 with that config. The BAT failed: AD stuck at spawn, party couldn't reach the exit.

Aaron's correction the next turn:

> "It should be heading generally southwest, south, southeast."

In his cardinal terminology, the west trail recipe is SW->S->SE, which is `(-1,+1) -> (0,+1) -> (+1,+1)`. The dirY is `+1` (screen-down), NOT `-1` (screen-up). "Up" in his earlier message referred to position-on-the-trail (toward the upper/spawn end of the trail), not screen-direction.

**The mistake's root cause:**

Non-cardinal direction phrases are ambiguous. "Up" can mean:
- Screen-up (dirY=-1 in our analog convention)
- Visually-up-the-screen (which on a rotated camera might be a totally different world direction)
- Position-on-the-trail (the upper part of the trail; a *location* not a *direction*)
- World-Y-increasing or world-Y-decreasing (depends on field-specific camera mapping)

Without asking which one Aaron meant, I picked the wrong interpretation. The cardinal-direction terminology Aaron uses (SW, S, SE, W, NW, N, NE, E) is unambiguous because it always means "the screen-direction the same as that compass direction on a screen with north at top" and translates directly to `(dirX, dirY)` via `DD_DirsToArrowMask`.

**Rule:**

For any chase-field direction discussion with Aaron:

1. If Aaron uses cardinal terms (SW, S, SE, etc.), translate using the table at the top of this doc.
2. If Aaron uses non-cardinal phrasing ("left and slightly up", "down the trail", "toward the exit"), ASK him to restate in cardinals before changing any code. Specifically ask: "In cardinal terms, that's (e.g.) southwest, or south, or something else?"
3. NEVER guess the cardinal interpretation of an ambiguous phrase. The cost of one extra clarifying message is far less than a wasted BAT cycle.

**The terminology table is now documented at the top of this lessons doc** so it's easy to reference during future direction discussions without scrolling.

**Related: re-examine prior direction-conflict diagnoses with the cardinal-direction lens.**

The v0.15.9.5 BAT "success" on `domt3_2` (5s, 1 catch) was attributed in the v0.15.9.5 comments to "script-forced kani co-location." That diagnosis was wrong: the catch was a direction-conflict catch (we were pushing east while Aaron presses west). v0.15.9.7.4 fixes domt3_2 to go west; if catches drop to 0, the original v0.15.9.5 diagnosis was wrong on every BAT since.

This also resurrects the carryover hypothesis Aaron raised after v0.15.9.7.2: fighting Aaron's actual direction on `domt3_2` may have been priming some script state (kani aggression timer, chase-fighting flag) that carried into `domt5_1` and triggered catches even with correct direction and walking on the west trail. v0.15.9.7.4 tests this by fixing both fields' directions in the same build.

**Generalization to F9 and future direction features:**

Any code path that asks the player for a direction (manual chase-mode hints, F9 destination cardinals, navigation announcements) should use the cardinal table as its vocabulary. Other phrasings invite the same kind of ambiguity that caused the v0.15.9.7.3 failure. Announcements in particular should output cardinals consistently ("the exit is to the south-east") rather than relative terms ("the exit is down and to the right") which assume the player has a screen-relative mental model.

## Finding #32: FF8 PC's analog input path determines walk vs run by deflection MAGNITUDE, not the W modifier. The keyboard input path uses W; these are independent code paths and the chase script's catch evaluator reads the analog path

**Source:** v0.15.9.7.4 BAT (walking audio confirmed but `domt5_1` catch still fired) + Aaron's pinpointing question + v0.15.9.7.5 magnitude-reduction fix.

After five successive BATs (v0.15.9.7 through v0.15.9.7.4) chasing W-modifier timing, re-press cadence, direction, and carryover on `domt5_1` -- all with walking audio confirmed reaching the engine but the robot still triggering -- Aaron asked:

> "Are we emulating directional keys or the analog stick? Could it be that the way we are emulating the navigation is not respecting the W key being held down to walk? I know the directional keys respect that, but not sure if the analog stick does."

This was the right question. FF8 PC has two parallel input paths that the engine reads:

1. **Keyboard arrows + W modifier**
   - Arrows set movement direction.
   - W (cancel scancode, mapped to walk modifier on foot) asserts walking speed regardless of arrow press magnitude (arrows are binary anyway).
   - This is the path Aaron uses in manual play and that drives the walking animation + footstep audio.

2. **Analog stick (DIJOYSTATE2 lX/lY)**
   - Magnitude of deflection determines speed: full = run, partial = walk. The threshold is around 50/127 in PSX values (~394 in our `±1000` scaled convention).
   - The W modifier does NOT (or barely) apply to the analog path. This makes sense historically: PSX FF8 used analog magnitude exclusively (no W key); the PC port added keyboard support as a parallel path but kept the analog code unchanged.

Our direction-drive emulates BOTH simultaneously. It writes fake-gamepad analog at full deflection (`lX = dirX * 1000`) AND injects keyboard arrows + W via SendInput. The engine reads both, but specific subsystems read different paths:

- **Walking animation + footstep audio**: reads keyboard + W. Worked correctly in v0.15.9.7.2 onward (Aaron heard walking).
- **Chase script's catch evaluator**: reads analog magnitude. Saw full deflection = running, fired the catch regardless of W state.

That's why the catch fired in every BAT from v0.15.9.7 to v0.15.9.7.4 despite walking audio.

**The fix (v0.15.9.7.5):**

When `walk=true`, write analog at `WALK_ANALOG_MAGNITUDE = 350` (~35% deflection per axis) instead of `RUN_ANALOG_MAGNITUDE = 1000`. The catch evaluator now sees walking magnitude.

The W press is kept as belt-and-suspenders: covers any walk-modifier analog consultation FF8 might do, and preserves walking-animation/footstep audio for Aaron's accessibility.

**Diagnostic implication for future BAT debugging:**

When "walking audio confirmed but catch still fires" appears in a BAT report, the FIRST question is: what magnitude is the analog written at? Audio is downstream of W on the keyboard path; catch eval is downstream of analog magnitude. They diverge in our emulation because we write analog at full deflection by default.

**Generalization to F9 path-finding:**

F9's analog write is identical to direction-drive's (both share `s_analogDesiredLX/LY` from `field_navigation.cpp`). F9 always writes full deflection because F9 always wants the party to run to its target. If any future feature wants F9 to walk (e.g., navigating slowly through a crowded room for screen reader audio cue parsing), the same magnitude-reduction approach applies.

**Generalization to chase fields generally:**

The catch evaluator on `domt5_1` reads analog magnitude. We don't know yet whether the catch evaluators on OTHER chase fields (`domt4_1`, `domt3_2`, etc.) also read analog magnitude or rely on different signals. The v0.15.9.7.5 fix only changes `domt5_1`'s behavior (it's the only walk=true field). If future BATs surface catch issues on other fields, the same analog-magnitude question applies before assuming the cause is direction or some other signal.

**Lesson on debugging methodology:**

We spent five BATs investigating W-modifier timing, re-press cadence, direction, and carryover before recognizing that the catch was on a parallel code path. The pattern -- "feedback signal X confirms our intent but result Y still fails" -- is a strong indication that the signal we're reading and the signal driving the result are on different code paths. Always ask "what code path drives X?" and "what code path drives Y?" before assuming they're the same.

**Update after v0.15.9.7.5 BAT (2026-05-12) -- partial-deflection theory failed:**

The v0.15.9.7.5 fix shipped `WALK_ANALOG_MAGNITUDE = 350` (~35% deflection) hoping the analog magnitude would land below the engine's run/walk threshold. **It didn't.** Party still ran and the catch still fired.

Possible explanations:

1. **The walk threshold is much lower than the PSX-50/127 estimate.** Maybe ~25/127 in PSX values, scaling to ~200 in our convention. 350 would still be above it.
2. **The catch evaluator reads a different signal than speed.** Maybe "is analog stick deflected at all" (boolean) rather than "how fast is movement". In that case, any non-zero magnitude triggers the catch regardless of speed.
3. **There's a kani-state predicate we don't see.** The catch script might condition on something orthogonal to player input -- e.g., a per-tick check that increments while "chase mode is active and player is doing something".

**Aaron's v0.15.9.7.6 pivot:** stop searching for the right magnitude. Set `WALK_ANALOG_MAGNITUDE = 0`. The engine sees a centered gamepad + keyboard arrows + W modifier -- exactly the input shape Aaron's manual play uses with no joystick. Manual play has 0 catches every time; mirroring the input shape should produce the same result.

Generalization: when you don't know whether the threshold you're trying to hit is at value X or below value X, and you have an existing known-working configuration, **just match that configuration exactly**. Don't search the parameter space; copy the known-working point.

**v0.15.9.7.6 expectation:** matches manual play, 0 catches. If even this fails, the catch script reads something else entirely and we need disassembly to find it.

## Finding #33: SendInput's KEYEVENTF_EXTENDEDKEY flag must match the scancode -- arrow keys are extended, letter keys are not. Setting it unconditionally silently drops letter-key injections through DirectInput readers.

**Source:** v0.15.9.7.7 BAT (pure keyboard, fake gamepad uninstalled, party STILL ran at dmag 750-855 and got caught) + Aaron's two-part diagnosis ("the W key press is not making its way to the game ... we should be holding W down continuously, not re-pressing every tick") + direct read of `InjectKey` in `field_nav_autodrive.inl`.

After Finding #32 ruled out analog magnitude, v0.15.9.7.7 shipped pure-keyboard input (no fake gamepad install, no analog override). The build ran exactly as designed -- the field log at engagement showed `FRESH START walk=1 -- keyboard-only path`, `override=0 gamepad=0`, and `walk modifier RE-PRESSED` firing every tick all the way to tick #1321. But the party still moved at running speed (dmag 750-855 vs the manual play's 270-303), and `[CBF] NO-OP chase BATTLE call` fired three times on `domt5_1` between 21:59:00 and 21:59:13.

The input was being delivered to the OS, but FF8 wasn't honoring W. Reading `InjectKey` in `field_nav_autodrive.inl` revealed the root cause:

```cpp
static void InjectKey(WORD scanCode, bool down)
{
    INPUT inp      = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = 0;
    inp.ki.wScan   = scanCode;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY  // <-- unconditional
                   | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
}
```

`KEYEVENTF_EXTENDEDKEY` was set for every call. **Arrow keys ARE extended** -- the `0xE0` hardware prefix is correct for them. **Letter keys like W are NOT extended.** When we sent W with the flag set, the OS injected scancode `0xE0 0x11` instead of `0x11`. FF8's DirectInput keyboard reader, which reads raw hardware scancodes, never saw a W press. Arrows worked the whole time (correctly extended); W silently dropped from the very first BAT that tried to use it.

The smoking gun: `world_map.cpp` v0.14.102 had ALREADY documented this exact distinction in a comment for the car-driving code:

> "the rental car uses the A KEY (VK=0x41, scan=0x1E) as the gas pedal that must be HELD continuously, and W (VK=0x57, scan=0x11) for reverse. CRUCIAL DETAIL: A and W are NOT extended keys (per v0.11.14: 'NOT extended keys (no KEYEVENTF_EXTENDEDKEY)'); arrow keys ARE extended. The key-injection helpers must handle both flag types."

The car-control module had its own `PressKey`/`ReleaseKey` helpers with an `extended` parameter, fixed in v0.14.102. But the field-navigation `InjectKey` (a parallel implementation of the same SendInput dance, in a different file) was never updated. **The same bug had two parallel implementations in the codebase, and we fixed one but not the other.** When chase auto-pilot started injecting W in v0.15.9.7, the un-fixed `InjectKey` silently dropped every press.

### The fix

v0.15.9.7.8 adds an `extended` parameter to `InjectKey` (default `true` preserves backward compat for every existing arrow-key call site). When `false`, `KEYEVENTF_EXTENDEDKEY` is omitted. The three W call sites in `field_nav_directiondrive.inl` pass `extended=false`. v0.15.9.7.8 BAT confirmed success: full chase auto-pilot route completed end-to-end, west trail walked without triggering the robot.

### Aaron's hold-vs-tap point

Aaron's second observation in the same diagnosis was: "we should be holding W down continuously, not re-pressing every tick." Correct. The defensive re-press path added in v0.15.9.7.1 was based on an unverified theory that the chase intro choreography swallowed the initial KEYDOWN. The actual reason W wasn't reaching the engine was the extended-key bug -- timing had nothing to do with it. With the bug fixed, one KEYDOWN at engagement is enough; the OS holds the key state until our explicit KEYUP at Disengage.

Re-pressing 60 times per second was also potentially counterproductive: DirectInput's buffered mode treats each KEYDOWN as a discrete event, so the engine could be seeing 60 "tap" events per second instead of one continuous hold. v0.15.9.7.8 removes the re-press path entirely.

### Methodology lesson: parallel implementations drift

The codebase had two functions doing the same low-level SendInput dance in different files for different purposes (`PressKey`/`ReleaseKey` in `world_map.cpp` for car driving; `InjectKey` in `field_nav_autodrive.inl` for arrow keys + chase walking). They were originally written at different times, didn't share code, and drifted. When one developer (a past Claude session) discovered the extended-key trap and fixed the car module, the field module was left vulnerable to the same bug.

**The rule:** when adding a new use case for a low-level OS API (here, injecting a non-arrow key through SendInput), consult sibling modules first. If another part of the codebase already uses the same API for a similar purpose, its quirks and fixes are likely relevant. Don't trust that two parallel implementations are equivalent just because they look similar.

**The consolidation suggestion:** unify `world_map.cpp`'s `PressKey`/`ReleaseKey` with field-nav's `InjectKey` into a single helper module (e.g., `src/key_injection.cpp`) exporting one function with the right parameter set. Both callers use it. Future scancode-quirk discoveries land in one place. This is item #3 in DEVNOTES backlog after v0.15.9.7.8.

### Diagnostic recipe for SendInput drops

When a SendInput-based injection appears to do nothing despite all visible signals saying it should work:

1. Verify the build is running (unique log markers).
2. Verify the call site is firing (logs at the injection site, scancode visible).
3. Verify SendInput's return value -- it should be `1`. If `0`, the OS rejected the input (UIPI / focus / lock-screen / Ctrl+Alt+Del).
4. **Check the `KEYEVENTF_EXTENDEDKEY` flag for the specific scancode.** Arrows, function keys, number-pad keys with NumLock off, and right-side modifiers are extended. Most letter keys and digits are not.
5. Compare to a physical keyboard event for the same key via a scancode logger to confirm the flag set the OS produces naturally.

v0.15.9.7.8 added the SendInput return-value check (logs failure on return != 1). Future drops of any kind will surface in the field log immediately.

### Generalization

Any mod feature that uses SendInput to inject non-arrow keys (future controller-emulation hotkeys, accessibility key remapping, scripted button presses for cutscene skips) must use the corrected `InjectKey` signature with `extended=false` for non-extended scancodes. Future Claude sessions should treat "injecting a new scancode through SendInput" as a gate that requires verifying the extended-key flag for that specific scancode.

## Suggested F9 re-engineering priorities

Order by impact (updated after v0.15.9.2.15 BAT). The list is scoped to **F9 path-finding** (NPC-target drives). Findings #15-20 are chase-specific patterns for **drive-target inference** when the target must be derived from field geometry alone, and aren't directly applicable to F9's NPC-as-target model. Findings #13, #14, #21 cross-apply.

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

## Drive-target inference patterns (chase auto-pilot and future)

For any drive where the target must be inferred from field geometry rather than supplied as a known coordinate (e.g., chase auto-pilot, future "walk to field exit" features, generic auto-traversal):

1. **Three-tier preference: gateway -> trigger line -> cluster center** (finding #20). Each tier has its own arrival semantic (line-crossing for the first two, point-distance for the third).
2. **Direction-aligned candidate selection, not nearest** (finding #16). Cross-product alignment of `player->candidate` and `player->cluster` selects forward-progress candidates over entry-back ones.
3. **Tagged target types: line-crossing vs point-distance arrival** (finding #18). The drive infrastructure must support both. Implicitly assuming point-distance was the v0.15.9.2.6-.13 bug.
4. **Per-field completion markers** (finding #17). Prevent re-engage/arrive loops when the target lands inside the spawn-arrival radius. Mark fields as completed after Disengage; clear on field change.
5. **Know the semantic of each Line entity** (finding #19). SETLINE Line entities can be `screenBd`, `event`, `interact`, etc. Don't conflate trigger-line types when searching for screen-transition geometry.

## What chase-drive proved works

- Path-finding (A*+funnel) computes correct routes end-to-end.
- Calibration produces correct camera axes (consistent across v0.15.9.2.1 and v0.15.9.2.2 BATs on the same field).
- Fake gamepad install/remove is reliable.
- SetAnalogFromVector projection is correct.
- Walking modifier (W key) holds correctly across long drives.
- chase_auto_pilot's per-field config + mode routing is a clean pattern.
- Updates to keyboard bitmask DO take effect tick-by-tick (v0.15.9.2.2's Fix A produced consistent kb=DR throughout BAT).
- INF gateway parsing produces correct line endpoints and destination field IDs (v0.15.9.2.15 BAT).
- Direction-aligned gateway selection (cross-product alignment) reliably picks forward-progress exits over entry-back gateways (v0.15.9.2.15 BAT, every chase field tried).
- Three-tier fallback (gateway -> trigger line -> cluster) handles all chase fields encountered without per-field code beyond two pre-existing explicit configs (v0.15.9.2.15 BAT).
- chase-drive can complete a multi-field auto route end-to-end (5+ chase fields, ~106 seconds, 0 player input from chase ASK commit onward).

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
- v0.15.9.2.6 implementation: `src/chase_auto_pilot.cpp` BuildFallbackConfig + `src/field_navigation.cpp` GetLargestClusterCenter (cluster-center fallback, superseded by INF gateway).
- v0.15.9.2.11 implementation: per-field completion marker in chase_auto_pilot.cpp (stops engage/arrive/disengage loop).
- v0.15.9.2.14 BAT field log: 10:44:09 on domt2_1 (trigger-line filter rejected Event Triggers; INF gateway already had correct endpoints).
- v0.15.9.2.15 implementation: `src/field_archive.{h,cpp}` GatewayInfo line endpoints; `src/field_navigation.{h,cpp}` GetGatewayNearestCluster API + s_driveCrossLine state; `src/field_nav_directiondrive.inl` StartChaseDrive crossing setup; `src/field_nav_autodrive.inl` crossing-check unified read; `src/chase_auto_pilot.cpp` three-tier fallback.
- v0.15.9.2.15 BAT field log: 14:00-14:05 (full chase auto-pilot through dotown_3, 5 chase fields, 11 battles suppressed by cap=0, 0 player input).
- v06.17 corridor steering: same file.
- Camera calibration: same file, `s_calibPhase` state machine.
- `Plan & Research Documents/Auto-Drive Architecture Review.md` -- existing review of the architecture, may need refresh post-v0.15.9.2.x.
- `Plan & Research Documents/Field Navigation Improvements Plan.md` -- existing plan, may overlap.

## Finding #34: `GetTriggerLineNearestCluster` mis-selects on fields where the walkmesh's largest dead-end isn't the exit

**Discovered:** v0.15.9.8 (post-mortem on v0.15.9.7.8 BAT, 2026-05-12)

**Field affected:** `doopen2a` (Dollet Town Square, immediately after the bridge `domt1_1`)

### The bug

`BuildFallbackConfig`'s tier 2 (TRIGGER-LINE) calls `GetTriggerLineNearestCluster` to pick which SETLINE trigger to target when INF gateways are unavailable. The heuristic finds the walkmesh's largest dead-end cluster, then selects the trigger line nearest that cluster's center.

The underlying assumption -- that the largest walkmesh dead-end correlates with the field's exit direction -- holds for corridors and narrow trails where the biggest walkmesh feature IS the exit funnel. It breaks on fields with interior architectural features (town squares, plazas, building corners) where the biggest dead-end is a non-exit.

### Concrete trace from v0.15.9.7.8 BAT log

```
[22:22:07] FieldArchive: INF parsed: 0 active gateways for 'doopen2a'
[22:22:07] FieldNavigation: [SETLINE] call#21 line(-1432,660)->(-1349,1048) idx=1 center=(-1390,854)
[22:22:07] FieldNavigation: [SETLINE] call#22 line(-814,-3717)->(-1091,-3689) idx=257 center=(-952,-3703)
[22:22:08] FieldNavigation: GetTriggerLineNearestCluster matched cluster(-1315,503)
                            -> trigger[0] center=(-1390,854) distFromCluster=359
[22:22:08] ChaseAutoPilot: fallback config built for field='doopen2a' mode=TARGET
                            tgt=(-1390,854) walk=0 running TRIGGER-LINE idx=0
```

`doopen2a` has two trigger lines: one NW at `(-1390, 854)`, one SOUTH at `(-952, -3703)`. The walkmesh's largest dead-end cluster is at `(-1315, 503)` -- an interior corner in the NW. The heuristic picks the NW trigger (359 units from cluster) over the SOUTH trigger (~4200 units from cluster). But the actual exit is SOUTH.

Aaron's recipe for this field: *"The Town Square does require the party to keep running. You mostly head down from where you enter the field in order to get to the next."*

### Downstream consequence

With the wrong target, the party briefly moved NW, the catch evaluator fired at 22:22:10 during the mis-directed run, the post-catch script knockback teleported the party south to `(-1042, -917)`, the A* path was now stale, and the party sat stuck for ~9 seconds while the chase script eventually forced a field transition at 22:22:21. Two `[CBF]` catches fired during the wait.

### The fix (v0.15.9.8)

Per-field explicit MODE_TARGET config for `doopen2a` in `kFieldConfigs[]` pointing at the south trigger center `(-952, -3703)`. Bypasses the heuristic entirely. Pattern matches the existing explicit configs for `domt4_1`, `domt3_2`, `domt5_1`, `dotown_2`, `dotown_1`.

### Deeper fix (deferred)

The heuristic could be improved by re-scoring trigger lines using one or more of:

1. **Direction-from-spawn alignment.** Score each trigger line by the dot product of `(trigger_center - player_spawn)` and `(player_facing_direction)` or the chase script's preferred direction. Whichever trigger is in the player's run direction wins. This matches what `GetGatewayNearestCluster` already does for INF gateways via cross-product detection.
2. **INF destination-field IDs (when available).** INF gateways encode `destFieldId`. If the auto-pilot knows the chase order's next field (`dotown_3` after `doopen2a`), it could match against that. Doesn't help when INF=0 like `doopen2a`, but could help on other fields.
3. **Distance from player spawn.** The exit trigger is typically further from spawn than interior-architecture clusters. Score by distance-from-spawn, prefer the farther trigger.

None of these is ironclad on its own -- options 1 and 3 can mis-score on U-shaped fields where the exit is BACK in the spawn direction; option 2 only helps when INF data is present. Per-field explicit configs are the safety valve and remain the right answer for any field that trips the heuristic.

### Methodology takeaways

1. **When a field has 0 INF gateways AND multiple SETLINE trigger lines, the cluster heuristic is unreliable.** Always verify against Aaron's authoritative direction recipe before assuming the auto-pilot picked correctly.
2. **The 1187 dmag was NOT script-controlled motion** (my earlier wrong theory). It was the post-catch script knockback after the auto-pilot steered into a catch. Lesson: don't conflate "single anomalous tick" with "pervasive script control." Look at the full picture -- did the party have a chance to move before the script intervened?
3. **Per-field explicit configs are cheap and surgical.** Adding one config entry takes minutes; the heuristic improvement project takes days and may not generalize. Use explicit configs liberally on problem fields.

### References

- v0.15.9.8 implementation: `src/chase_auto_pilot.cpp` -- new `doopen2a` entry in `kFieldConfigs[]`.
- v0.15.9.7.8 BAT field log: `Logs/ff8_field.log` line 4499 (INF parsed: 0 for doopen2a), 4488-4489 (SETLINE call#21, call#22), 5936 (GetTriggerLineNearestCluster output).
- Existing heuristic implementation: `src/field_navigation.cpp` `GetTriggerLineNearestCluster` and `GetGatewayNearestCluster` (these are parallel implementations -- INF gateway version uses cross-product detection, trigger-line version uses nearest-to-cluster; the cross-product approach is more robust and should ideally be ported to the trigger-line picker too).
