# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

Older entries (pre-v0.15.12.0) are preserved in `CHANGELOG_HISTORY.md`.

## v0.17.6.2

Disables F9 path-finding auto-drive's corridor-level steering. The v0.17.6.1 BAT [drive-vec] log on bghall_1 Save Point exposed corridor steering directly fighting the drive-start pre-skip block, wedging the player against geometry for hundreds of ticks. Funnel waypoints alone (manual nav's BAT-proven primitive) are now F9 auto-drive's only steering source.

### What [drive-vec] showed

The Save Point drive started cleanly. Pre-skip correctly bumped past wp 0 (only 58 units from player; both wp 0 and the corridor edge midpoint between tri 358 and tri 71 are the funnel-collapsed point `(-626,-8215)`, which is essentially the player's current location). For one tick at t=30, the analog reflected the real target:

```
t=30  corOverride=0  corSteer=(-700,-8593)  finalDelta=(-132,-375)  lX=-332 lY=943  kb=DL
```

That's south-west toward the save point at `(-700,-8593)` -- the same direction manual nav uses for its "south, 2 steps" announcement at this position. But starting at t=60, the corridor-level steering block kicked in (its `s_driveTotalTicks >= 30` gate had just opened) and overrode `steerX/Y` to the shared-edge midpoint `(-626,-8215)`:

```
t=60   corOverride=1  corSteer=(-626,-8215)  finalDelta=(-57.8, 2.6)  lX=-999 lY=-44  kb=L
t=90,120,150,180,210,240...  pp=(-568,-8218)  lX=-999 lY=-44  kb=L  moveDist=0
```

The corridor steering re-introduced the exact point that pre-skip had discarded. The analog flipped from `lX=-332 lY=943` (south-west diagonal) to `lX=-999 lY=-44` (pure west). The keyboard collapsed from `kb=DL` (diagonal) to `kb=L` (single direction). The player pressed pure LEFT into a wall and didn't move for hundreds of ticks. Recovery fired, re-pathed, corridor steering picked the same point again, player wedged again. Repeat until `Gave up. Distance remaining: 555.`

### Why manual nav doesn't have this problem

Manual nav at the same position computes the analog directly from `(target - player) * camAxes` and presses arrow keys for the dominant axes. From `(-568,-8218)` toward save point `(-700,-8593)` the dominant axes are both LEFT and DOWN (the delta is `(-132,-375)`), so the keyboard fires `DL` diagonal. FF8's wall-sliding then handles the corridor turn -- the player walks south-west, slides along the west wall, and naturally tracks the corridor through tri 358 -> 71 -> 70 -> 8 to the save point.

Manual nav has been correct on the first announcement across bghall_1, bghall_4, bg2f_1, bg2f_2, bgroom_1 since v0.17.5 with no corridor steering. F9 auto-drive's separate corridor steering pipeline was the source of the failure, not the funnel or the analog projection (v0.17.6.0 confirmed those are correct).

### The fix

`field_nav_autodrive.inl` line ~635: the corridor-level steering condition is wrapped with `false &&`, matching the pattern v06.20 used to disable wall-avoidance. The entire block stays in place with the original v06.17/v0.15.9.2.3 rationale plus a new v0.17.6.2 block explaining why it's off and what to flip if a future field regresses without it.

Chase-drive is unaffected; it has skipped this block since v0.15.9.2.3.

Other things v0.17.6.1 added that stay in place because they're correct:
- Recovery counter reset on tri advance (worked exactly as designed -- the v0.17.6.1 BAT log shows `[drive] recovery counter reset: tri 358 -> 359 (player advanced along corridor; phase was 6)` firing at the right moment).
- `MAX_RECOVERY_PHASES` 12 -> 30 (safety net, didn't fire in v0.17.6.1 BAT; drive ended via DRIVE_MAX_TICKS instead).
- `[drive-vec]` per-tick diagnostic log (this is how we found the bug; staying on for v0.17.6.2 BAT in case a different failure pattern emerges).

### Files

- `src/ff8_accessibility.h` -- version 0.17.6.1 -> 0.17.6.2
- `src/field_nav_autodrive.inl` -- corridor-level steering block gated with `false &&` and new v0.17.6.2 rationale comment
- `CHANGELOG.md` -- this entry

## v0.17.6.1

Follow-up triage of the v0.17.6.0 BAT. The re-engineered F9 auto-drive proved mechanically correct on bghall_1 (no CALIB, .ca-quantized axes, mathematically correct analog projection, kb/analog agreement), but Aaron's BAT reported "failed on most entities I tried" -- three of four drive attempts ended in `Stuck. Distance remaining: <N>.` Only the JSM-injected Directory (closest target, in the main hallway) reached Arrived. Root-cause analysis traced the failures to the recovery counter, not the axis pipeline.

### Recovery counter inflates across triangle boundaries

The Save Point drive made genuine progress through five corridor triangles (367 -> 366 -> 363 -> 362 -> 359), but each triangle escape needed 2-3 recovery cycles (re-path + perpendicular nudge), and `s_driveWigglePhase` only resets when the player advances past funnel waypoint index 3 -- which never happened because the path kept re-pathing back to waypoint 0 after each recovery re-path. The global counter inflated to 12 and `MAX_RECOVERY_PHASES` killed the drive while the player was still making real progress toward the save point. The two long-range exit drives (Hall 8 at dist 4753 remaining, Front Gate 5 at dist 3291 remaining) hit the same wall earlier in their corridors.

v0.17.6.1 adds a new reset signal: when the recovery block fires and the player's walkmesh triangle has changed since the previous recovery cycle, that's genuine corridor progress and `s_driveWigglePhase` resets to 0. Each new triangle along the corridor earns a fresh recovery budget. The new `s_lastRecoveryTri` state variable is initialized to `0xFFFF` at drive start in `field_nav_handlekeys.inl` so the first recovery on a fresh drive doesn't see a stale tri from a prior drive on the same field.

`MAX_RECOVERY_PHASES` is also bumped from 12 to 30 as a safety net. With the tri-advance reset working, 30 is only reached when the player genuinely cannot escape a single triangle -- the v0.17.6.0 Save Point case would have run with phase max ~3 per triangle (the highest seen between resets on bghall_1) and never gotten anywhere near 30. The new ceiling is designed to fire only on "this triangle is permanently unreachable" cases, not on slow corridor traversals.

### Per-tick steering pipeline diagnostic ([drive-vec])

The v0.17.6.0 BAT log showed `lX=-840 lY=-542` for multiple consecutive 120-tick log windows even as the player oscillated between two positions, which made it hard to tell whether the analog projection itself was wrong or just stuck on a stale waypoint. The existing `[drive] tick=` line fires every 2 seconds and only shows post-projection state.

v0.17.6.1 adds a `[drive-vec]` log that fires every 30 ticks (~0.5 s) and shows the intermediate values at each stage of the steering pipeline:

```
[drive-vec] t=N tri=T pp=(px,pz) wpRaw=(wx,wy) corOverride=0|1 corSteer=(sx,sy) trigRedir=0|1 finalDelta=(dx,dz) lX=lx lY=ly kb=mask wig=W phase=P
```

- `wpRaw` is the chosen funnel waypoint (or final target) before corridor steering runs.
- `corOverride/corSteer` says whether corridor steering replaced the waypoint with a shared-edge midpoint, and what midpoint it picked.
- `trigRedir/finalDelta` says whether the trigger-line proximity check rewrote `dx/dz` parallel to a nearby line, and the final `dx/dz` going into `SetAnalogFromVector`.
- `lX/lY` are the analog values after camera projection.
- `kb` is the heading bitmask derived from `lX/lY` (post v0.17.6.0 unified logic).
- `wig/phase` are the recovery counters.

When v0.17.6.1 BAT data comes back and a drive still gets stuck, the per-tick log shows exactly which stage broke. Three new tracking variables (`vecWpRawX/Y`, `vecCorridorOverrode`, `vecTrigRedirected`) record stage outputs as the existing pipeline runs; they cost essentially nothing per tick and the log itself is gated by `s_driveTotalTicks % 30 == 0`. To turn the log off after triage is complete, raise `DRIVE_VEC_LOG_INTERVAL` to a large number.

### Files

- `src/ff8_accessibility.h` -- version 0.17.6.0 -> 0.17.6.1
- `src/field_navigation.cpp` -- `MAX_RECOVERY_PHASES` 12 -> 30, new `s_lastRecoveryTri` state
- `src/field_nav_handlekeys.inl` -- reset `s_lastRecoveryTri` at drive start
- `src/field_nav_autodrive.inl` -- recovery block tri-advance reset, three pipeline tracking flags, [drive-vec] log emit
- `CHANGELOG.md` -- this entry

## v0.17.6.0

Re-engineers F9 path-finding auto-drive to share manual nav's load-time-quantized camera axes, splits draw-point arrival from save-point arrival, and adds INF gateway crossing detection. First of a staged v0.17.6.x series that re-bases auto-drive on manual nav's BAT-proven primitives.

### Three changes, one BAT

**1. F9 auto-drive uses the .ca-quantized axes manual nav uses.**

Manual nav has been correct on the first announcement across bghall_1, bghall_4, bg2f_1, bg2f_2, and bgroom_1 since v0.17.5 thanks to load-time 90-degree quantization of the .ca-file axes. F9 auto-drive was still running the v06.14 empirical CALIB pipeline that injects `lX=+1000` for 24 ticks, then `lY=+1000` for 24 ticks, measures the resulting walkmesh delta, and writes `s_driveCamRight/Down`. That loop predates the quantization work and has a known failure mode (the bghall_1 BAT bug from NEXT_SESSION_PROMPT): when phase 1 fails because the player is wedged against geometry, the default `(1,0)` axes are kept and steering uses wrong axes on rotated cameras.

v0.17.6.0 wires `SetAnalogFromVector` to read `s_camRight/Down` (manual nav's quantized pair) when F9 owns the drive, and `s_driveCamRight/Down` (the empirical pair) when chase-drive owns it. F9's handlekeys block no longer initiates CALIB -- it sets `s_calibPhase = 3` (skip-state) unconditionally. The auto-drive starts moving the moment the player presses backslash, with no warmup phase and no CALIB-can-fail edge case.

Chase-drive is deliberately untouched: per its design doc Finding #10, empirical calibration is the verified-working axis source on rotated-camera chase fields (e.g., domt5_1 where `camRight ~= (0,1)`). Future unification can swap chase to the quantized axes once F9 with quantization proves stable in production (chase doc Finding #28: parallel implementations have already cost five wasted BAT cycles, so they shouldn't stay parallel forever -- but we don't risk regressing chase auto-pilot while validating F9).

**2. Draw points arrive within talk radius. Save points stay walk-onto.**

The handlekeys arrival-distance block previously conflated save points and draw points under a single 30-unit walk-onto rule. Per Aaron's spec, draw points should behave like NPCs and interactive objects -- arrive when the player is within interaction distance, not when they're standing on top of the marker.

The new split:
- `ENT_SAVE_POINT` -> arriveDist = 30.0f (walk-onto, unchanged). The save crystal only activates when the player's model overlaps it.
- Runtime-entity targets (`entityIdx >= 0`) including NPCs, Objects, and reclassified-NPC Draw Points -> read engine-set talkRadius, clamp to 60-unit floor. Logged target type for diagnostic clarity.
- `ENT_DRAW_POINT` with no runtime entity slot (JSM-injected, `entityIdx <= -300`, e.g., Fire Cavern 'drpoint') -> arriveDist = 120.0f. Matches GPS_ARRIVE_DIST's default for non-entity targets and gives the player room to press X without inching onto the exact marker.

**3. INF exit gateways auto-cross like trigger lines.**

Trigger-line targets (`entityIdx <= -200`) already had cross-product sign-flip arrival detection plus a 300-unit overshoot offset on the steer target -- when the player crosses the line, the drive announces Arrived and the engine fires the screen transition naturally. INF gateway targets (`entityIdx <= -400`) used plain `dist < arriveDist` arrival, which stopped the drive 300 units short of the gateway and left the player to walk through manually.

v0.17.6.0 wires gateway targets through the same crossing-detection state chase-drive uses (`s_driveCrossLine*`, `s_driveCrossLineActive`):

- At drive start, handlekeys finds the raw INF gateway in `s_gateways[]` whose `destFieldId` matches the dedup-catalog entry AND is nearest to the player, and seeds its line endpoints into `s_driveCrossLine*`.
- `UpdateAutoDrive`'s crossing block, which previously gated on `s_chaseDriveActive && s_driveCrossLineActive`, now gates on just `s_driveCrossLineActive`. Chase-drive and F9 gateway both flow through the same code path.
- F9 trigger-line targets keep using the existing `s_capturedLines[]` lookup branch; handlekeys doesn't seed `s_driveCrossLine*` for them.

The dedup catalog can cover 1..N raw gateways with the same destination field; we pick the nearest as the crossing line. If the player crosses a different raw gateway in the same group, the engine still fires the transition and `"Player position lost."` ends the drive when the field reloads -- functionally equivalent for the user.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.6.0)
- `src/field_nav_autodrive.inl`:
  - `SetAnalogFromVector` -- branch axis source on `s_chaseDriveActive`. Updated documentation block.
  - `UpdateAutoDrive` crossing block -- condition widened to `s_driveCrossLineActive` for both chase and F9 gateway.
- `src/field_nav_handlekeys.inl`:
  - F9 drive-start CALIB block -- replaced with unconditional `s_calibPhase = 3`.
  - Arrival-distance block -- split save points from draw points; runtime-entity draw points fall through to talkRadius path; JSM-injected draw points get a 120-unit default.
  - Trigger crossing block -- added gateway-crossing setup (find nearest raw gateway with matching destFieldId, seed `s_driveCrossLine*`, compute `s_driveTrigCrossStart`).
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### What's NOT touched

- Chase-drive (`StartChaseDrive`, `IsChaseDriveActive`, the v0.15.9.2.x logic) keeps its empirical CALIB pipeline. `s_driveCamRight/Down` still exist and are still written by CALIB phase 1/2 when chase-drive starts and `s_calibPending` is true.
- The `s_camCalibrated` flag and `s_calibPending` reset in `HookedFieldScriptsInit` -- chase-drive depends on them.
- Recovery / wiggle phase machine (deferred to v0.17.6.2).
- Engine triangle-ID corridor steering (deferred to v0.17.6.1; the stale-triId class of failures is the next big-ticket item).
- New per-tick `[drive-vec]` diagnostic (deferred to v0.17.6.3).
- Manual nav, GPS, funnel pruning, hysteresis -- all v0.17.5.x work stays as shipped.

### BAT verification

Load save in bghall_1. Cycle F9 to an exit (e.g., Hall 6). Press `\`.

Expect:
1. NO `[CALIB] phase 1` or `phase 2` log lines for F9. The first `[drive] tick=` line should show the correct screen-relative direction immediately.
2. `[drive] gateway target -> crossing line (...)->(...) crossStart=... rawIdx=N destFieldId=M` log line at drive start.
3. As the player approaches the exit, `[drive] stopped: Arrived.` fires when they physically cross the gateway line (cross-product sign-flip), not 300 units short.
4. Field reloads naturally to the destination.

Also BAT: cycle F9 to a Draw Point and `\`. The drive should stop within ~120 units (or talkRadius if a runtime entity), not walk on top of the marker. Save Points still walk on top (unchanged).

If cardinals or steering are still wrong on any rotated-camera field, the `[NAV-PROJ-INIT] quantization` line at field load tells us the camera axes the drive is using, and the per-tick `[drive] tick=` line shows the resulting lX/lY values. Both should match what manual nav uses for direction announcements on the same field.

## v0.17.5.4

Fixes the World Map polling stuck-at-startup bug exposed by v0.17.5.3's TTS audit trail.

### What v0.17.5.3 revealed

Aaron's BAT log showed two TTS messages firing in the same second when pressing `\` on bghall_1:

```
[15:59:37] [TTS] "Driving."
[15:59:37] [TTS] "No locations available." (interrupt)
```

The FieldNavigation autodrive started successfully ("Driving"). Immediately afterward, the World Map module's key handler also responded to `\`, found its catalog empty (correctly, since there's no world map data when on a field), and announced "No locations available" with interrupt=true -- clobbering the "Driving" announcement.

### Root cause

World Map's `IsOnWorldMap()` in `world_map_segments.inl` only checked `WM_SCENE_FLAG`. At application boot before any scene has loaded, that memory location reads 0 (zero-initialized memory), and `(scene == 0)` returned true. The `Poll()` function then declared "Entered world map" at boot, set `s_onWorldMap=true`, and never reset because the exit detector only fires on a true->false transition of `IsOnWorldMap()`. `PollKeys()` then ran every tick on every screen (including fields), responding to `\` with the "No locations available" announcement.

The world log carried a long-standing diagnostic warning that surfaced exactly this:

```
[WORLD] WorldMap: Entered world map
[WORLD] WorldMap: Warning - On world map but game mode is 0 (expected 2)
```

The warning was observing the disagreement between scene flag and game mode but only logging it. v0.17.5.4 uses game mode as part of the decision.

### The fix

`IsOnWorldMap()` now requires BOTH signals to agree:

1. `FF8Addresses::pGameMode` must be resolved AND its value must equal `MODE_WORLDMAP` (= 2).
2. THEN the scene flag at `WM_SCENE_FLAG` must read 0.

If either check fails, the function returns false. Both signals are wrapped in SEH (`__try`/`__except`) since they read raw process memory.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.4)
- `src/world_map_segments.inl` -- `IsOnWorldMap()` rewritten to require both scene flag AND game mode

### What's NOT touched

FieldNavigation autodrive's underlying steering issue on bghall_1 (separate bug, see below). Funnel pruning (v0.17.5.2). Quantization (v0.17.5). Hysteresis (v0.17.5.1). TTS audit logging (v0.17.5.3, retained and proving its worth).

### Separate bug exposed (deferred)

The BAT log also shows that even after the spurious "No locations available" TTS, FieldNavigation autodrive RAN but FAILED to reach its target. At drive start (15:59:37) dist=3899 from target. After 21 seconds of running (15:59:58) dist=3726 -- only 173 units of forward progress. Player position went from (137,-7634) to (134,-7461). Multiple recovery cycles and re-paths in the log. Drive stopped with `[drive] stopped: Stuck. Distance remaining: 3726`.

Analysis of the final tick shows the steering math is producing contradictory inputs: at player=(134,-7461), steer target=(452,-7722), delta is (+318 east, -261 south). With autodrive's calibrated axes `driveCamRight=(1,0) driveCamDown=(0,-1)`, the correct keyboard direction is RIGHT+DOWN. The log instead shows `kb=U lX=1000 lY=0` -- pressing UP on the keyboard and pushing analog right only. The vertical axis is inverted for autodrive on this field.

This is a separate, longstanding autodrive issue independent of v0.17.5.4's world-map fix. Manual GPS works fine on bghall_1 because manual nav and autodrive use SEPARATE axis pairs (`s_camRight/Down` vs `s_driveCamRight/Down`, v0.17.2 split). Tackling this needs a dedicated investigation of autodrive's steering pipeline and is queued for a future version.

### BAT recipe

Launch the game and reach a field (any field). Press `\` to start autodrive on a catalog entity.

Expect:
- `ff8_world.log` should NOT show "Entered world map" at boot. The world map module should only declare entry when the player actually reaches the world map.
- `ff8_mod.log` should show `[TTS] "Driving."` (or "Target not yet located.") in isolation -- no follow-up `[TTS] "No locations available."` interrupt.
- `\` on the world map (when the player is actually on it) should still work normally.

## v0.17.5.3

Diagnostic logging for autodrive validation failures and an audit trail of every TTS utterance. No behavior changes; this build is a step toward fixing the "Target not yet located" autodrive refusal Aaron hit on bghall_1.

### Motivation

After the v0.17.5.2 push, Aaron tried autodrive on bghall_1 by selecting an entity from the catalog and pressing `\`. The mod spoke something like "location not available" (Aaron's paraphrase) and refused to start driving. There was no record in any log of:

1. What was actually spoken (the message Aaron heard).
2. Which catalog target was selected at the moment of the refusal.
3. Why the validation failed (was the player position unknown? the target position? was the catalog index out of bounds?).

v0.17.5.3 adds both logs so the next BAT will expose the cause without needing further investigation.

### What ships

**1. `ScreenReader::Speak` -- TTS audit trail.** Every call to `Speak(text, interrupt)` now writes a `[TTS] "<text>"` line to `ff8_mod.log` before the text reaches SAPI/NVDA. Empty-string "silence" purge calls are skipped (they produce no audio). The narrow-ASCII transliteration is one WideCharToMultiByte hop and bounded at 511 chars; overhead is negligible at the speech cadence the player actually experiences. The same logging applies whether the call site uses `Speak`, `Output`, `RepeatLast`, or any other variant -- all funnel into this one wide-char path.

This is a permanent diagnostic. It pays for itself every time a player reports "the mod said something weird" -- we get the literal string without needing to reproduce the scenario.

**2. Autodrive validation-fail logging.** In `field_nav_handlekeys.inl`, the `else` branch that fires "Target not yet located" now logs a full context line before speaking. The line reports:

- Current field name
- Selected catalog index (and total catalog size)
- Target's `entityIdx` and `gatewayIdx`
- Target type and name
- Whether `GetEntityPos` succeeded for the player
- Whether `GetEntityPos` succeeded for the target (only meaningful when entityIdx >= 0)
- Player's own entity index

This tells us in one log line which of the validation gates failed.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.3)
- `src/screen_reader.cpp` -- `[TTS]` audit logging in `Speak(const wchar_t*, bool)`
- `src/field_nav_handlekeys.inl` -- `[drive] REFUSED` diagnostic in the validation-fail branch

### What's NOT touched

Funnel pruning (v0.17.5.2). Quantization (v0.17.5). Hysteresis (v0.17.5.1). The validation logic itself -- this is observation only, no fix. The actual fix for whatever the next BAT exposes will ship as v0.17.5.4 or v0.17.6.

### BAT recipe

Repeat the autodrive attempt that failed on bghall_1:
1. Load to bghall_1 (or any field).
2. Cycle to an entity in the F9/F10 catalog (especially an NPC, which is the suspected failure case).
3. Press `\` to start autodrive.
4. If you hear "Target not yet located" or anything else other than "Driving", note which entity was selected.

Check `ff8_mod.log` for `[TTS] "..."` -- that's the exact utterance you heard.
Check `ff8_field.log` for `[drive] REFUSED -- target validation failed: ...` -- that's the why.

### Likely cause (hypothesis, BAT will confirm)

GetEntityPos for NPCs returns false until the engine's `set_current_triangle` callback fires for them. That callback runs the first time the entity's init script puts it on a triangle. For NPCs whose init scripts haven't executed yet at autodrive-press time, `target_pos_known=0` is the expected log signal. Fix (deferred to v0.17.5.4): fall back to the JSM-extracted SETPOS coordinates already captured for those NPCs.

## v0.17.5.2

Funnel waypoint pruning. Reduces SSFA micro-corner waypoint noise on walkmesh corridors that have many small triangle turns. Quantization architecture (v0.17.5) and announcement hysteresis (v0.17.5.1) ship unchanged.

### Motivation

Aaron's v0.17.5.1 BAT on bg2f_2 (classroom hallway) revealed that the SSFA funnel was producing 13 waypoints for a 1600-unit path. Tracing the SSFA against the corridor's actual portal geometry showed that the algorithm was correctly identifying every walkmesh triangle's portal-vertex alternation as a turn point -- but most of those "turns" don't represent real bends from the player's perspective.

Perpendicular-distance check of each waypoint against the line through its neighbors:

| Waypoint | Off line | Real corner? |
|----------|----------|--------------|
| wp 1 | 220 units off wp 0->wp 4 | YES (real bend) |
| wp 2 | 6 units off wp 1->wp 4 | no (collinear-ish) |
| wp 3 | 24 units off wp 1->wp 4 | no |
| wp 4 | 30 units off wp 1->wp 5 | no |

With hysteresis filtering brief sector flips (v0.17.5.1), each of these waypoint advances still produced a cardinal-change announcement, since they each spanned >500ms of walking. So on this corridor Aaron heard "east, northeast, north, northeast, north, northwest, north..." instead of "east, north" (the two macroscopic legs).

### What ships

New function `PruneCollinearWaypoints` in `field_nav_pathfinding.inl`, called at the end of `FunnelPath` after the funnel produces its waypoint list. Algorithm:

1. For each interior waypoint B (with neighbors A and C), compute perpendicular distance from B to the segment AC.
2. If perpDist < `PRUNE_PERP_EPSILON` (50 units), remove B from the list.
3. Repeat the sweep until no more removable waypoints (sweep-to-stable).

First and last waypoints are preserved. Iteration is capped at 100 sweeps as a safety bound.

50-unit epsilon was chosen conservatively below typical FF8 wall thickness (~100+ units). Combined with the existing `AGENT_RADIUS = 30` portal shrinking (waypoints are already pulled 30 units inward from walls), worst-case post-prune wall clearance is ~80 units -- still well within walkable space. Real corners (like wp 1 above at 220 units off) are never touched.

### Properties

- **Reduces micro-corner cardinal changes.** Aaron hears one announcement per real corner, not one per walkmesh-triangle bend.
- **Preserves object avoidance.** Pruning eps is below wall thickness, so corners that actually route around walls/objects (where the perp distance is large) survive.
- **Affects both GPS and autodrive.** Both read the same `s_waypoints` array from FunnelPath. Autodrive benefits from fewer waypoints to navigate too, and its existing stuck-recovery handles any wall-grazing.
- **Reversible.** If a future field surfaces a real corner under 50 units of perp distance, lowering the constant or skipping the prune for that field is a one-line change.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.2)
- `src/field_nav_pathfinding.inl` -- `PruneCollinearWaypoints` function added; `FunnelPath` calls it before logging; log line now reports both pre- and post-prune counts as `[funnel] N triangles -> M waypoints (post-prune; pre-prune=K, was J centers)`

### What's NOT touched

v0.17.5 quantization (working). v0.17.5.1 announcement hysteresis (working). The SSFA funnel itself (its waypoint output is still optimal in the geometric sense; pruning just discards waypoints that don't represent meaningful turns). EdgeMidpointPath fallback (used by autodrive when funnel fails). v0.16.5.2 BAT triage backlog.

### Known remaining gap (for v0.17.5.3 or later)

Even with pruning, the FIRST cardinal on bg2f_2 will still be "east" because the path genuinely starts by going east before bending north (the corridor curves around the central pillar visible in Aaron's BAT screenshot). Aaron's mental model focuses on the final destination, which is north of the start. Resolving this fully will require hybrid announcement (option B from session discussion): announce both the immediate cardinal AND the final-target cardinal, e.g. "east, heading north, 6 steps". This is queued for v0.17.5.3 if Aaron's BAT shows the pruning alone isn't enough.

### BAT recipe

Repeat the v0.17.5.1 BAT path (elevator -> classroom hallway -> classroom -> dorm). Watch for:

1. `[funnel] N triangles -> M waypoints (post-prune; pre-prune=K, was J centers)` lines should show K >> M on corridors with zigzag triangles. bg2f_2's path that was 13 waypoints should drop to 4-5.
2. `[funnel-prune] removed N collinear waypoints (eps=50 units, K sweeps)` lines confirm pruning fired.
3. Aaron's qualitative: fewer cardinal-change announcements per journey. Each one should correspond to a real bend in the path.

## v0.17.5.1

GPS announcement hysteresis. Quantization architecture from v0.17.5 ships unchanged; this point release fixes the TTS rattle Aaron reported in the v0.17.5 BAT.

### Motivation

Aaron's v0.17.5 BAT: quantization worked on 4 of 5 fields (cardinals correct from the first announcement, no warmup needed). Two issues on bg2f_1 (the C-shaped classroom hallway):

1. **TTS rattle.** "The direction / distance was constantly rattling off and spamming the TTS." The v0.17.0 GPS cadence fires on every cardinal sector boundary crossing AND every step-count change. Near a sector boundary the cardinal can flip between two adjacent values for a tick or two; every flip fired an announcement. Step counts decrement frequently as the player walks. The existing 3-second throttle (`GPS_ANNOUNCE_INTERVAL_FAR`) only gated step-only changes -- direction changes always broke through. Result: 1-2 announcements per second on long walks.
2. **"South when I needed north" on bg2f_1.** Aaron's spatial description (enter at bottom point of a C-shape, door at top opposite side, considerably north and west of entry) combined with the quantized axes (RIGHT->world-north, DOWN->world-east) means a real "south" leg would push the player world-east -- away from the door. So this was almost certainly a transient sector flip during the rattle, not a sustained wrong cardinal.

Aaron's spec for the fix: "TTS only announces the direction when it changes, e.g. it says to go north so I keep going north until it eventually says east then I go east."

### What ships

In `field_nav_gps.inl::UpdateGPS`, the v0.17.0 sector-change-driven cadence and the v0.17.1 waypoint-force are replaced with **cardinal-change-only with hysteresis**:

- Announcements fire ONLY when the cardinal changes from `s_gpsLastDirIdx` to something different, AND the new value has held steady for `GPS_DIR_HYSTERESIS_MS = 500ms`.
- Step-count changes never fire on their own.
- Waypoint advances never fire on their own (if the cardinal happens to match across two waypoint legs, the conceptual handoff is silent -- Aaron just keeps walking the same direction).
- Nearby/in-range one-shot announcements are unchanged.

Mechanism: two new statics `s_gpsPendingDirIdx` and `s_gpsPendingDirSince`. When the computed cardinal differs from the last spoken one, it becomes a candidate. If a new candidate appears (different from previous candidate), its timer resets. Only when the candidate has held its value for 500ms is it promoted to `s_gpsLastDirIdx` and announced. Brief sector flips never get past the hysteresis.

### Properties

- **Eliminates the rattle structurally.** Sector boundary jitter can no longer fire because brief flips reset the candidate timer rather than promote.
- **Resolves the bg2f_1 transient.** Whatever caused the one-off "south" announcement (likely a corner-waypoint geometry quirk in the funnel) can no longer reach the screen reader unless it persists for half a second.
- **Matches Aaron's spec exactly.** Silence while walking in a constant direction, announce only when the direction genuinely changes.
- **No effect on the architecture.** v0.17.5 quantization is untouched. The fix is purely in the announcement gate.

### Edge cases the fix handles

- **Long straight walks.** Cardinal stays constant for minutes -> total silence (except Nearby/in-range at the end). Matches Aaron's spec.
- **Genuine corner turn.** Cardinal changes from A to B and B holds steady -> announced after 500ms. Adds ~half a step of delay to corner announcements; acceptable.
- **Wander back out of nearby zone.** Re-prime pending so the next direction change still requires hysteresis confirmation rather than firing immediately.
- **GPS started mid-flight on a stable bearing.** `StartGPS` primes both `s_gpsLastDirIdx` and `s_gpsPendingDirIdx` to the initial cardinal, so `UpdateGPS` doesn't spuriously fire on the first tick.

### Files changed

- `src/ff8_accessibility.h` -- version bump
- `src/field_nav_gps.inl` -- new statics `s_gpsPendingDirIdx`/`s_gpsPendingDirSince`/`GPS_DIR_HYSTERESIS_MS`; reset in `StopGPS`; prime in `StartGPS`; cadence block in `UpdateGPS` rewritten

### What's NOT touched

v0.17.5 quantization (working). v0.17.4 det convention check. v0.17.1 path-aware path building (waypoints still drive the steering target; they just no longer trigger announcements on their own). v0.17.0 ComputeScreenDirIndex math. v0.16.5.2 BAT triage backlog.

### BAT recipe

Walk the same path as v0.17.5 (elevator -> classroom hallway -> classroom -> dorm). Expect:

1. **Drastically fewer `[GPS] Update` lines in the log.** Each one's tagged `hysteresis=ok` and `dirChanged=1`.
2. **No back-to-back announcements** for step-count changes.
3. **bg2f_1 hallway**: cardinals should be "north" along the up-leg, "west" along the cross-leg (or "northwest" near the bend), no spurious "south". If a spurious south DOES sneak through, it means whatever produced it was stable for >500ms, which is a separate issue from rattle and we look at the .ca data.
4. **Aaron's qualitative report.** Silence while walking in a stable direction. Announcement at each genuine cardinal change. Same architecture-level correctness as v0.17.5.

## v0.17.5

Replaces v0.17.4's passive movement-driven calibration with a **load-time 90-degree quantization** of the CA-derived camera axes. Same end result as a perfect calibration on the four well-behaved fields, deterministic on field load, and zero state machine.

### Motivation

Aaron asked the right question after the v0.17.4 BAT: "is it really necessary to have this calibration? It seems like the ideal solution would be for the mod to automatically calibrate each field based on the field's unique data upon field load, and not when the character moves." Looking back at the v0.17.3 BAT clean samples through that lens, the engine's actual arrow -> world direction was world-axis-aligned on every tested field:

| Field | CA camRight angle | Engine RIGHT direction | World cardinal |
|-------|-------------------|------------------------|----------------|
| bghall_1 | 7.8 deg | (1, 0) | 0 deg (snap from 7.8) |
| bghall_4 | 23.8 deg | (1, 0) | 0 deg (snap from 23.8) |
| bg2f_1 | 65.4 deg | (0, 1) | 90 deg (snap from 65.4) |
| bg2f_2 (det-fixed) | 60.5 deg | ~(-0.19, 0.98) | 90 deg (5-11 deg residual) |
| bgroom_1 | -62.5 deg | (0, -1) | -90 deg (snap from -62.5) |

The CA value rounds to the engine's actual direction in every case. The engine appears to use a 90-degree-quantized form of its camera matrix when mapping DIJOYSTATE2 lX/lY to walkmesh delta. So we can do exactly that at load time and skip the entire observation-based calibration loop.

bg2f_2 (classroom) has a 5-11 deg residual after quantization. That's well within the 22.5 deg cardinal sector tolerance, and the v0.17.4 BAT proved bg2f_2 navigates correctly via the det fix alone with the residual baked in.

### What ships

The det convention check from v0.17.4 stays unchanged. v0.17.5 adds **one quantization block** in `HookedFieldScriptsInit` after the det fix:

1. Compute `angleR = atan2f(camRight.y, camRight.x)`.
2. Snap to nearest 90 deg: `snappedR = roundf(angleR / (PI/2)) * (PI/2)`.
3. Regenerate camRight as a unit vector at the snapped angle.
4. Derive camDown from camRight via the rotation `(x, y) -> (y, -x)` which is R(-90 deg) and exactly the det = -1 screen-down convention (independent quantization of camDown could break orthogonality near 45 deg boundaries, so we don't do that).
5. Mirror the quantized pair to `s_driveCam*` so the auto-drive private pair starts from the same baseline.
6. Source tag becomes `"ca-quantized"` and the load-time log includes both the original CA angle and the snapped angle.

The `[NAV-OBSERVE]` log from v0.17.3 stays in place as pure diagnostic. The observer now compares engine measured direction against the QUANTIZED prediction, so a future field where the engine doesn't match 90-deg snap will surface as DIVERGE > ~12 deg in the log.

### What got ripped out

From v0.17.4 and v0.17.5-pre:

- `ObsCalibrateAxes()` function and its call from `ObserveArrowResponse`.
- `s_fieldCalibratedManual` static flag and its reset at field load.
- Observer hold-state reset at field load (no longer needed without the cal logic).
- Include order change that put observe.inl before fieldscripts.inl (observe.inl is back at the end where it was in v0.17.3).
- The v0.17.5 filter constants (`OBS_CALIB_AXIS_ALIGN_MAX`, `OBS_CALIB_MAX_ROT_DEG`, etc.).

The v0.17.4 BAT-induced TTS rattle is structurally impossible now: cardinals are computed from axes that never change after field load.

### Properties

- **Deterministic.** Same .ca file -> same axes. No timing windows, no "walk for 500ms before cardinals are right."
- **No state machine.** No flag, no observer dependency for correctness.
- **No regressions.** Filter retest against the v0.17.4 BAT data shows: bghall_1 -> camRight=(1,0) (matches engine exactly), bg2f_1 -> camRight=(0,1) (matches), bg2f_2 -> camRight=(0,1) (matches engine within 11 deg), bghall_4 -> camRight=(1,0) (matches), bgroom_1 -> camRight=(0,-1) (matches).
- **Observer becomes pure diagnostic.** If a future field violates the 90-deg-quantized model, DIVERGE > ~12 deg in `[NAV-OBSERVE]` shows it and we revisit.

### Edge cases the math handles

- 45 deg boundary (e.g., camRight at exactly 45 deg): `roundf(0.5)` rounds away from zero, so the snap is consistent. camDown is derived from camRight rather than independently quantized, so the 90 deg relationship is always preserved.
- Floating-point residuals from `cosf(pi/2)` returning ~6e-8 instead of 0 are clamped to 0 so the log reads cleanly.
- Both det conventions (-1 raw + det-corrected, +1 -> negate camDown to get det = -1, then quantize) end up at the same standard right-handed quantized axes.

### Files changed

- `src/ff8_accessibility.h` -- version bump
- `src/field_navigation.cpp` -- comment block above `s_camRight/Down` rewritten to describe the v0.17.5 architecture; removed `s_fieldCalibratedManual` static; restored original observe.inl include position (after diagnostics.inl)
- `src/field_nav_observe.inl` -- removed `ObsCalibrateAxes` function, call site, and v0.17.4/.5 filter constants; restored v0.17.3 "purely observational" comment block
- `src/field_nav_fieldscripts.inl` -- removed observer/lock reset block; added quantization step inside the CA-init branch after the det fix; updated source tag to `"ca-quantized"`

### What's NOT touched

v0.17.4 det convention check (proven correct and necessary for bg2f_2 and any other left-handed CA fields). v0.17.0.1 2D normalization. v0.17.2 state separation between manual-nav and auto-drive axis pairs. v0.17.1 path-aware GPS. v0.17.3 observer logging (now diagnostic-only, same as it was originally). Auto-drive's empirical calibration (separate state pair, unaffected). v0.16.5.2 BAT triage backlog.

### BAT recipe

Repeat the v0.17.4 BAT pass. Expect:

1. `[NAV-PROJ-INIT] det-correction` line for bg2f_2 only.
2. `[NAV-PROJ-INIT] quantization: camRight pre=(...) angle=(...) -> snap=(...) -> camRight=(...) camDown=(...)` line for every field that successfully loads its .ca file.
3. `[NAV-PROJ-INIT] field=... source=ca-quantized` summary line.
4. **NO `[NAV-CALIB-AUTO]` lines anywhere.** That function is gone.
5. `[NAV-OBSERVE]` lines as before (still throttled to 1.5s), now showing DIVERGE against quantized prediction. Expect DIVERGE near 0 on every field except bg2f_2 where DIVERGE will be 5-11 deg (within sector tolerance, no action needed).
6. **Aaron's qualitative report.** All five fields should navigate correctly from the first cardinal announcement after entering. No "walk a bit before cardinals work," no TTS rattle.

## v0.17.4

The fix the v0.17.3 BAT diagnosed. v0.17.3's observer logged the world-space response to single-arrow key presses across `bghall_1`, `bghall_4` (elevator field), `bg2f_1` (2nd-floor hall), `bg2f_2` (classroom), and `bgroom_1` (dorm). Two findings:

**Finding 1 — the engine's screen-to-world transform is a uniform rotation per field, not a per-arrow thing.** On `bgroom_1`, all 14 axis-aligned clean samples agreed on -27.5° (CW), stdev 0.49°. On `bg2f_1`, all 8 clean samples agreed on +24.6° (CCW), stdev 0.0°. `bghall_1` had -7.8° (small enough Aaron didn't notice), `bghall_4` had -23.8° (Aaron reports works perfectly — borderline but the elevator corridor's geometry tolerates it). Meaning: a single clean observation per field is enough to determine the rotation matrix that maps CA-derived axes to the engine's actual axes. The chase auto-pilot's empirical calibration has been doing this all along; the manual-nav pair never adopted the technique.

**Finding 2 — `bg2f_2` (classroom) had `det(camRight, camDown) = +1.0`, left-handed CA axes.** All other fields in the BAT had `det = -1.0`. With det=+1, the 2D projection of `.ca` axis1 ends up pointing world-UP instead of the standard world-DOWN convention. The mod's prediction for UP/DOWN arrows comes out exactly opposite of the world direction Aaron actually moves — "had to go opposite the instructions" matches the math precisely. With axis1 negated to force `det=-1`, the residual rotation for `bg2f_2` becomes ~+30-40° (in line with the other tilted-camera fields, varying with noise from Aaron walking through curves).

The two paired fixes ship together because they target the same end-to-end outcome (cardinals match Aaron's walking direction) and the diagnostic distinguishes them in the log:

### Fix 1: det convention check at CA load

In `field_nav_fieldscripts.inl` after the v0.17.0.1 2D normalization, compute `det = camRight.x*camDown.y - camRight.y*camDown.x`. If positive, negate `camDown` to force `det=-1`. Logs a new `[NAV-PROJ-INIT] det-correction` line when this fires.

The negation propagates to the drive-private pair (auto-drive starts from CA values), but auto-drive's empirical calibration overwrites those on first run, so chase auto-pilot is unaffected. The only visible behavior change is that fields with det=+1 (rare, e.g. `bg2f_2`) now project to UP/DOWN cardinals using the corrected direction.

### Fix 2: passive self-correcting calibration

In `field_nav_observe.inl`, a new `ObsCalibrateAxes()` function fires from the per-tick observer when:

- Single arrow held for >= 30 ticks (~500ms; stricter than the diagnostic log's 18 ticks so the engine has settled into a stable motion direction).
- Measured movement delta >= 100 world units (twice the diagnostic threshold).
- `dot(predicted, measured) >= 0.5` (within a 60° cone of the current axes — rejects samples where Aaron walked through a wall or a curve and the engine's actual direction was deflected, which v0.17.3 BAT showed as 95-100° outliers).

When all three pass: compute `theta = atan2(cross(predicted, measured), dot(predicted, measured))` and rotate BOTH `s_camRightX/Y` and `s_camDownX/Y` by `theta` using the standard 2D rotation matrix. Update `s_camAxesSource` to `"calibrated"`. Log a `[NAV-CALIB-AUTO]` line with the old axes, new axes, and rotation magnitude.

Uniform rotation across the four arrows (Finding 1) means a single clean observation calibrates the field for all subsequent cardinal computations. If the camera changes mid-field (unlikely but possible on multi-section fields), the next clean observation re-calibrates. Wall-deflection samples are filtered out by the 60° cone, so axes don't wobble on noisy data.

The diagnostic `[NAV-OBSERVE]` log from v0.17.3 stays in place at the lower 18-tick / 50-unit / no-cone thresholds so the next BAT can still surface residual divergence patterns if they exist.

### What Aaron should experience

On entering a field, the first GPS cardinal still uses CA-derived axes (possibly off by 0-40°). When he holds an arrow to walk in that announced direction for ~500ms, calibration fires and rotates the axes to match the engine. The next GPS update announces a corrected cardinal. Total mismatch window: about half a second.

For `bghall_1` (small -7.8° rotation), Aaron likely won't notice anything different — cardinals stayed within the 22.5° sector tolerance even before calibration. For `bg2f_1`, `bg2f_2`, and `bgroom_1`, the mismatch window is the only time wrong cardinals appear; after that, cardinals are accurate. For `bghall_4` (elevator, -23.8° rotation, Aaron reported works perfectly), behavior should also improve slightly though Aaron was already comfortable with it.

### Files changed

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_fieldscripts.inl` — det convention check after CA 2D normalization
- `src/field_nav_observe.inl` — new `ObsCalibrateAxes()` function plus `ObsCalibrateAxes(...)` call from `ObserveArrowResponse()`; comment block at top updated to reflect that the observer now writes state
- `src/field_navigation.cpp` — comment block above `s_camRight/Down` updated to document v0.17.4 observer writes

### What's NOT touched

Auto-drive's empirical calibration code in `field_nav_autodrive.inl` (proven correct, never read s_camRight/Down anyway). v0.17.2 state separation (preserved verbatim — drive pair and manual pair stay independent except for the field-load mirror). Chase auto-pilot config tables. v0.17.1 path-aware GPS. v0.17.0.1 2D normalization. The v0.16.5.2 BAT triage backlog. Classroom entity catalog under-population (parallel track — still needs Aaron's field-name lookup and F9 list).

### BAT recipe

Repeat the v0.17.3 BAT pass: load save in `bghall_1`, walk through the same fields holding each cardinal for 2-3 seconds. Watch for new `[NAV-CALIB-AUTO]` log lines. Each field should produce one `[NAV-CALIB-AUTO]` line within seconds of the first eligible arrow hold; subsequent `[NAV-PROJ] start` lines for GPS sessions on that field should show `axes=calibrated` and updated `camRight`/`camDown` values.

Qualitative test: GPS-guide to a target on `bg2f_1` and `bg2f_2`. Initial cardinal announcement may briefly point the wrong way; after walking a step or two, subsequent announcements should be correct. End-to-end navigation should succeed without Aaron having to go opposite the instructions.

## v0.17.3

Diagnostic-only build. No behavior change. Adds a passive observer that logs the empirical world-space response to single-arrow key presses alongside the .ca-derived prediction, so the next BAT log shows directly whether the CA file values match the engine's actual screen-to-world projection on the fields where manual navigation has been giving wrong cardinals.

Background from the v0.17.2 BAT: Aaron loaded a save in `bghall_1` (Balamb Garden hallway), walked to the classroom. The `[NAV-PROJ]` line for the `bg2f_1` GPS session showed `axes=ca-file` (state separation working as designed: calibration can no longer leak into manual nav). But cardinals on `bg2f_1` and the classroom were still wrong, while the field outside the elevator -- previously the worst case -- now navigates correctly. That distribution rules out calibration corruption (hypothesis A) and confirms CA-vs-engine mismatch (hypothesis B): some fields' .ca data happens to align with the engine's actual projection, others don't, and which is which can't be derived from CA values alone.

The chase auto-pilot's empirical calibration in `field_nav_autodrive.inl` has always produced correct axes because it injects analog input and measures the resulting walkmesh delta -- it doesn't trust .ca, it observes reality. v0.17.3 does the same observation passively (no input injection) using Aaron's actual keypresses as the test signal. Each time he holds a single arrow for long enough to produce measurable movement (>= 18 ticks held, >= 50 world-unit delta), the observer logs a comparison line:

```
[NAV-OBSERVE] field='bg2f_1' axes=ca-file arrow=RIGHT held=22ticks delta=(120,-50)
              measured=(0.92,-0.38) predicted=(0.417,0.909)
              DIVERGE=68deg | camRight=(0.417,0.909) camDown=(0.909,-0.417)
```

The `DIVERGE` angle is the diagnostic core: 0 deg means CA values match the engine's actual projection for this arrow direction, 180 deg means exact opposite (both components signed wrong), 90 deg suggests axes swapped (axis0 and axis1 reversed in the .ca file's labeling convention), and any other angle is something more complex that needs the data to explain. Across multiple samples per field, the divergence pattern surfaces the transformation needed.

### How the observer self-gates

The sample is invalid if any of these is true, so the observer skips logging:

- An auto-drive (F9 path-finding or chase auto-pilot) is running -- synthetic key injection would pollute the measurement.
- The player entity hasn't been detected yet.
- A dialog is open -- engine ignores movement input.
- More than one arrow is held -- a diagonal press averages two camera-axis directions, ambiguous for clean per-axis comparison.
- Less than 18 ticks (~300ms at 60fps) of held time -- engine hasn't settled into movement.
- Less than 50 world units of measured delta -- noise rather than signal.
- Less than 1.5 seconds since the last sample -- throttle so a continuous hold doesn't flood the log.

With those gates, Aaron walking around naturally produces one log line per arrow direction per second or so. A test pass that walks a couple of seconds in each cardinal direction on each field of interest fills the log with the data needed to design v0.17.4.

### Why GetAsyncKeyState instead of the engine's keyboard buffer

The observer reads arrow state via `GetAsyncKeyState(VK_UP/DOWN/LEFT/RIGHT)` rather than the engine's keyboard buffer at `*0x01CD02D8`. Two reasons. First, GetAsyncKeyState reflects Aaron's physical key presses regardless of any in-engine remapping (FF8 lets the player remap keys; Aaron almost certainly hasn't, but the diagnostic shouldn't depend on that assumption). Second, the engine's keyboard buffer is written to by chase_keyboard during chase Auto and by FFNx during normal play; reading from the OS layer instead of the engine layer keeps the observer self-contained and removes a dependency on hook timing that doesn't matter here (the observer gates on auto-drive being inactive anyway, so synthetic injection isn't a confounder).

### Files changed

- `src/ff8_accessibility.h` -- version bump
- `src/field_nav_observe.inl` -- NEW file, ~150 lines, contains the observer state, helpers, and `ObserveArrowResponse()` function called from `Update()`.
- `src/field_navigation.cpp` -- include the new .inl after `field_nav_diagnostics.inl` (so it can see all helper functions from earlier includes) and add `ObserveArrowResponse();` to `Update()` right after `UpdateGPS();`.

### Not touched, deliberately

Projection math in `field_nav_gps.inl::ComputeScreenDirIndex` (still uses CA-derived `s_camRight/Down` -- v0.17.4 will know how to transform those once we have the BAT data). `field_nav_autodrive.inl` empirical calibration (proven correct, untouched). Chase auto-pilot config. v0.17.2 state separation (proven working). v0.17.1 path-aware buffer + advance logic. v0.17.0.1 CA 2D normalization. The v0.16.5.2 BAT triage backlog. The classroom entity catalog under-population (separate track).

### BAT recipe

Load a save in `bghall_1`, walk slowly in each cardinal direction (north / east / south / west) for at least 2-3 seconds, holding only one arrow at a time. Transition to `bg2f_1` and do the same. Transition into the classroom (the field that's been giving the worst cardinals) and do the same. The field log will contain `[NAV-OBSERVE]` lines with field name, arrow direction, measured vs predicted world direction, and divergence angle. Aaron can also include the elevator-side field that v0.17.2 BAT confirmed works correctly -- that gives a known-good baseline for what the divergence looks like when CA is correct.

Ideal sample count: 4 cardinals per field, across 3-4 fields, total ~12-16 lines. Plus whatever incidental samples happen during normal walking. Throttling ensures the log doesn't explode even on long sessions.

What the v0.17.4 fix looks like depends on what the data shows. If divergence is consistent per field (e.g. always 0 deg on field A, always 180 deg on field B, always 90 deg on field C), the fix is a per-field transformation table or a single geometric flip applied conditionally on a tractable .ca header value. If divergence varies within a single field (different per camera section), the fix needs to be runtime: read the engine's current camera state at the moment of projection rather than the static .ca snapshot. The observer data resolves the ambiguity in one BAT cycle.

## v0.17.2

Follow-up to v0.17.1 BAT triage. v0.17.1 added path-aware GPS (A* + funnel waypoints) and the BAT log confirmed the path-aware logic works correctly — two GPS sessions on the test field showed the funnel producing 11 and 38 waypoints respectively, the waypoint advance routine fired through the sequence, and the overshoot-detection branch caught three sub-arrive-distance passes. The new logic is sound. But the announced cardinals didn't match the direction Aaron actually had to walk, and the BAT log surfaced the cause: the `[NAV-PROJ]` lines at GPS-start time recorded camera axes `(0.493,0.870)`/`(-0.871,0.492)`, which are NOT the CA-derived axes that fieldscripts.inl logged for that same session at field load (`bghall_1` first load: `(0.991,0.135)`/`(0.134,-0.991)`). Something had overwritten the manual-nav camera axes between field load and the GPS test.

The most likely culprit is the auto-drive empirical calibration in `field_nav_autodrive.inl`. Phases 1 and 2 of that calibration inject `lX=+1000`/`lY=+1000`, measure the resulting walkmesh-delta direction, and write the normalized result back into `s_camRightX/Y` and `s_camDownX/Y` — the same statics that GPS and `FormatNavComponents` read for screen-relative projection. The calibration was originally added in v06.14 to give the chase auto-pilot's analog steering correct screen-to-world conversion, but it shares state with manual-nav by virtue of writing to the same module-level statics. That shared state never mattered before v0.17.0 because manual-nav didn't actually consume `s_camRight/Down` for cardinal labels — only the chase code did. v0.17.0 changed that: it wired CA-derived values into the same statics and pointed manual-nav at them. The wiring works at field load, but if calibration runs at any point afterward — even on a different field, even briefly — its writes leak into manual-nav's projection for the rest of the session or until the next field load.

A secondary possibility, raised by the chase auto-pilot lessons document, is that empirical calibration and `.ca`-derived axes don't produce identical results on every field. The chase doc's "What chase-drive proved works" list is explicit: empirical calibration produces correct axes (verified across multiple BATs on multiple rotated-camera chase fields). The CA-derived equivalent is unverified at that level of rigor — Finding #10 mentions it as a future option, not a proven equivalent. If CA-axes diverge from what the engine actually uses for screen projection on some fields, manual-nav cardinals will be wrong on those fields regardless of whether calibration runs.

v0.17.2 distinguishes the two hypotheses by splitting the camera-axes state pair, so manual-nav and auto-drive consume axes from independent sources that can no longer cross-contaminate:

- **`s_camRightX/Y, s_camDownX/Y`** is now the MANUAL-NAV pair. Set once by `HookedFieldScriptsInit` at field load — from the `.ca` file via v0.17.0.1's 2D-normalization path, or identity defaults if the `.ca` is absent / degenerate. Never written by auto-drive. Read by `field_nav_gps.inl::ComputeScreenDirIndex`, `field_nav_gps.inl`'s `[NAV-PROJ]` log lines, and `field_nav_helpers.inl::FormatNavComponents`.
- **`s_driveCamRightX/Y, s_driveCamDownX/Y`** is a new AUTO-DRIVE PRIVATE pair. Mirrors the manual-nav pair at field load (so auto-drive starts from CA-derived values on the first drive of each field), then overwritten by the calibration's phase 1 / phase 2 writes. Read only by `field_nav_autodrive.inl::SetAnalogFromVector` (and through it by chase auto-pilot + F9 path-finding's analog steering).

With this split, the next BAT log answers the question definitively. If manual-nav cardinals are now correct on `bghall_1` end-to-end, hypothesis A (calibration corrupting manual-nav) was the cause and the fix is complete. If the cardinals are still wrong on the same field, hypothesis B (CA-derived axes diverge from the engine's actual projection) is in play and v0.17.3 will need a deeper fix — either auto-drive-style empirical calibration on GPS start, or reading the engine's runtime camera matrix from memory rather than the file-load `.ca` snapshot.

Diagnostic logging added so the BAT log tells us which case we're in without needing further log forensics. The `[NAV-PROJ] start` line at `StartGPS` now includes the current field name and a new `axes=` tag that reads either `ca-file` (CA loaded and 2D-normalized successfully) or `identity` (CA absent, degenerate, or fell through to fallback). New static `s_camAxesSource` in `field_navigation.cpp` tracks this; `field_nav_fieldscripts.inl` sets it at every field load. With the source tag in every NAV-PROJ line, a v0.17.2 BAT log showing two GPS sessions on `bghall_1` will show the same `axes=ca-file` and the same camera-axis values on both — confirming the manual-nav pair is stable through the session. If the values still differ, the difference is now provably a real `.ca`-vs-engine mismatch and points to v0.17.3 work; calibration is no longer a possible culprit.

Chase auto-pilot is deliberately untouched. The empirical-calibration code path in `field_nav_autodrive.inl` still runs (phases 1, 2, fallback-perpendicular, and the `[CALIB]` log lines all retained); the only change is that it writes into the `s_driveCam*` pair instead of `s_cam*`. The chase doc's verified-working behavior on `domt4_1`, `domt3_2`, `domt5_1`, `dotown_2`, `dotown_1`, etc. is preserved bit-for-bit, since auto-drive's `SetAnalogFromVector` now reads from the same drive-private pair that calibration writes. The mirror-at-field-load step ensures the first drive on each field starts from CA-derived values rather than the previous field's calibration residue — a small improvement, but a side effect of the state split, not its purpose.

v0.17.1's path-aware logic is also untouched. `BuildGpsPath` and `AdvanceGpsWaypoint` still work the way the v0.17.1 BAT confirmed; the only thing changing in `field_nav_gps.inl` is the `[NAV-PROJ] start` log format (added two fields: `field='%s'` and `axes=%s`). Behavior outside the diagnostic logging is byte-for-byte identical.

Files changed: `src/ff8_accessibility.h` (version), `src/field_navigation.cpp` (new state pair + source tag), `src/field_nav_fieldscripts.inl` (reset + CA-load both pairs, set source tag), `src/field_nav_autodrive.inl` (calibration phases + `SetAnalogFromVector` rename `s_cam*` → `s_driveCam*`), `src/field_nav_gps.inl` (NAV-PROJ start log adds field + axes fields).

Not touched, deliberately: chase auto-pilot config tables, `field_nav_pathfinding.inl` (A* + funnel reused unchanged), `field_nav_helpers.inl::FormatNavComponents` (already reads `s_camRight/Down` which is now manual-nav-pinned), the v0.17.1 path-aware buffer + advance logic (proven working last BAT, no reason to risk regression), the v0.16.5.2 BAT triage backlog (FMV STOP/PLAY race, POLL tutorial garble, formation party-member filter, GF-BP diagnostic gating, HP-TRACK during GF-HP-SUB), and the classroom entity catalog under-population reported in v0.17.0.1.

BAT recipe: load `bghall_1` again (or any tilted-camera field). The new `[NAV-PROJ] start` log line will include `field='bghall_1'` and `axes=ca-file`. Walk around the field exercising both manual nav (Backspace/F9) and at least one auto-drive (F9 list cycle then start drive, or chase-trigger if convenient). If manual-nav GPS produces correct cardinals throughout AND a subsequent BAT shows the camera-axis values matching the field's CA load every time GPS announces, the state-separation fix is sufficient. If cardinals are still wrong with `axes=ca-file` in the NAV-PROJ logs, the BAT log will show the same axis values across multiple announces — proving the issue is CA-vs-engine divergence, not calibration corruption, and v0.17.3 needs to address the projection itself.

## v0.17.1

Path-aware GPS direction. v0.17.0/0.17.0.1 made the cardinal genuinely screen-relative on any camera, but the announced cardinal was still the straight-line bearing from player to final destination. On a curved hallway, that bearing cuts through walls. v0.17.0.1 BAT confirmed this on `bghall_1` (Balamb Garden hallway, C-shaped): going classroom → elevator corridor the bearing happened to align with the walkable direction at every step, so the cardinal was correct; going the other way, the bearing pointed through the inside of the curve and the announced cardinal was wrong because the player needed to follow the bend, not aim through it.

v0.17.1 runs A* on the walkmesh from the player's triangle to the target's triangle, smooths the corridor with the existing funnel algorithm (`FunnelPath` in `field_nav_pathfinding.inl`), and announces the cardinal toward the NEXT waypoint rather than the final destination. The funnel produces a small number of turn-point waypoints; on a straight path it produces a single waypoint at the destination, so behavior degrades cleanly to v0.17.0.1's straight-line direction. On a curved path it produces one waypoint per major bend, and the GPS announces the leg-by-leg direction the player needs to walk.

The A* + funnel infrastructure already exists in `field_nav_pathfinding.inl` — chase auto-pilot has been using it since v0.15.9, and F9 path-finding auto-drive uses it from `UpdateAutoDrive`. v0.17.1 calls it from `StartGPS` and copies the result into a GPS-private buffer (`s_gpsWaypoints[]`) so an active auto-drive's path isn't disturbed. The save/restore mechanism in `BuildGpsPath` snapshots the shared `s_waypoints[]`, `s_waypointCount`, `s_waypointIdx`, `s_usingFunnel`, `s_corridor[]`, `s_corridorCount`, and `s_wpMinDist` before A* runs and restores them after the funnel result is copied. If no auto-drive is running, the save/restore is effectively a state-clear and has no observable effect; if auto-drive IS running, its waypoint sequence and progress index are preserved across the GPS path build.

Waypoint advance uses two conditions, evaluated each `UpdateGPS` tick: (a) the player gets within `GPS_WP_ARRIVE_DIST` (200 units) of the current waypoint, or (b) the player overshoots — they got reasonably close (under `GPS_WP_OVERSHOOT_CLOSE` = 300 units) and the distance is now growing again, indicating they passed the waypoint at an angle. Either condition advances to the next waypoint. The 200-unit threshold is generous compared to auto-drive's 60-unit `FUNNEL_ARRIVE_DIST` because the player walking themselves doesn't need precise turn points — the threshold's job is to advance the announced direction BEFORE the player reaches the corner, so they have time to plan their turn. The overshoot detection mirrors auto-drive's same-named feature for the same reason.

The announcement cadence from v0.17.0 still applies: silent in the nearby/in-range zone, fires on cardinal sector change or step-count change outside it, with a 3-second minimum interval that direction changes break through. v0.17.1 adds one new break-through condition: a waypoint advance forces an announcement even when the cardinal happens to match the previous one. This matters for L-shaped paths where two legs both run, say, east — without the forced announcement, the player would never get a corner-handoff signal. The forced announcement uses the same cardinal text but fires immediately on advance regardless of the time-interval floor.

Fallback behavior is unchanged from v0.17.0.1 whenever A* can't run: walkmesh not loaded, target not on the walkmesh, player and target on disconnected walkmesh islands, or A* iteration limit exhausted. In all those cases `BuildGpsPath` returns false, `s_gpsUseWaypoints` stays false, and `UpdateGPS` aims straight at the final destination as before. The `[NAV-PATH]` log lines record which fallback path fired, so a future BAT where path-aware misbehaves can be diagnosed by reading the log without re-running.

Diagnostic logging additions: `[NAV-PATH]` lines at `BuildGpsPath` covering walkmesh availability, start/goal triangle indices, A* success/failure, funnel waypoint count, and the first 6 waypoint positions; `[NAV-PATH] wp N/M reached` on each advance with the reached/overshoot reason; the existing `[NAV-PROJ] start` / `[NAV-PROJ] update` lines now include `wp=I/N` and a `steer=(x,y)` field separate from `target=(x,y)` so the path-aware behavior is fully traceable.

Trigger-line targets get special handling: when the GPS target's catalog entry is a trigger line (`entityIdx <= -200` and `> -300`), `BuildGpsPath` passes the trigger index as `skipTriggerIdx` to `ComputeAStarPath` so A* can route through that specific trigger line (it's the goal). Without this, A* would refuse to enter the destination triangle because crossing the trigger line is forbidden by default. Runtime-entity targets (`entityIdx >= 0`) pass their entity index as `targetEntityIdx` so A*'s push-radius blackout doesn't block the goal triangle.

Files changed: `src/ff8_accessibility.h` (version), `src/field_nav_gps.inl` (path-aware state, `BuildGpsPath` helper, `AdvanceGpsWaypoint` helper, `StartGPS`/`UpdateGPS`/`StopGPS` integration with path-aware mode, expanded diagnostics).

Not touched, deliberately: `field_nav_pathfinding.inl` (A* + funnel implementation is reused as-is), `field_nav_autodrive.inl` (auto-drive's waypoint state save/restore is handled at the GPS-call boundary, not by changing auto-drive), `field_navigation.cpp` GPS_DIR_NAMES (the cardinal vocabulary is the same), the v0.16.5.2 BAT triage's other backlog bugs (FMV STOP/PLAY race, POLL tutorial garble, formation-based party-as-NPC filter, GF-BP diagnostic spam, missing damage announce during GF-HP-SUB), and the classroom entity catalog under-population reported in the v0.17.0.1 BAT (separate diagnosis track once we have the field name and the F9 list).

BAT recipe: load `bghall_1` (or any curved-corridor field). Cycle F9 to a target on the opposite side of the curve, press Backspace to start GPS guidance. The initial announcement should be the cardinal toward the first turn point, not the final destination. Walking along the corridor should produce a `[NAV-PATH] wp 0/N reached` log line and a fresh direction announcement at each bend. Going the reverse direction — same field, opposite endpoints — should now produce correct directions; that was the v0.17.0.1 BAT failure case. On straight fields (`bgroom_1`, default-camera open areas), behavior should be indistinguishable from v0.17.0.1.

## v0.17.0.1

Follow-up to v0.17.0 BAT triage. v0.17.0 confirmed the orientation infrastructure in principle — most fields improved — but two specific fields (`bghall_1` Balamb Garden hallway and the classroom outside it) still produced wrong cardinals. The BAT logs surfaced the cause immediately: `bghall_1`'s `[NAV-PROJ-INIT]` line showed `camRight=(0.991,0.135) camDown=(0.044,-0.330)`. camRight's 2D magnitude is 1.0; camDown's is only 0.333. The asymmetry biased every `atan2(sD, sR)` toward east/west and produced the wrong cardinal even when the screen-direction was unambiguously north or south.

Root cause was a missing normalization step in the v0.17.0 CA-to-`s_camRight/Down` wiring. The `.ca` file stores camera axes as **3D unit vectors** (int16 fixed-point /4096). For a tilted camera — where the rendering camera looks forward+down at the floor instead of straight down — most of axis1's magnitude lies in the Z (depth) component; the XY projection is short. v0.17.0 divided by 4096 and used the raw XY components, which left `s_camDown` at sub-unit-length on tilted-camera fields. Walkmesh deltas have Z=0, so dotting them against a short-XY-vector produced screen-space deltas with asymmetric scale: the screen-right projection was at full scale while screen-down was at fractional scale. `atan2(sD, sR)` reads ratios, so the asymmetry rotates the apparent angle toward the larger axis's direction.

The chase auto-pilot's empirical calibration in `field_nav_autodrive.inl` doesn't have this problem because it measures the resulting walkmesh-delta direction and normalizes by `cdist` (the magnitude of the delta). The result is always a unit-length 2D vector matching the engine's actual analog-to-walkmesh transform. v0.17.0.1 mirrors that: it normalizes axis0's and axis1's 2D projections to unit length before writing into `s_camRight/Down`.

The geometric justification matches the chase calibration's empirical observation. When the engine reads analog input `(lX, lY)` and converts it to walkmesh movement, it follows the camera axes' 2D projection as a *direction* — the player's walking speed is constrained to the engine's pace, not the axis vector's magnitude. So the walkmesh direction of "press right arrow" is the unit-length 2D projection of axis0, regardless of how much of axis0's 3D magnitude lies along Z. Same for camDown and axis1.

Default-camera fields produced correct cardinals on v0.17.0 because their axis0 and axis1 already have near-zero Z components (the 2D magnitude was ~1.0 already, so normalization is a no-op). The bug only surfaced on tilted-camera fields where axis1's Z component is large — a class of fields that includes the Balamb Garden interiors (`bghall_1`, `bgcls_1`/classroom, and likely most of the indoor environments with non-default camera framings).

Two extra `[NAV-PROJ-INIT]` log fields: `source=ca-file-normalized` (was `ca-file`) so a v0.17.0 build vs v0.17.0.1 build is unambiguous from the log; and a per-field `raw-2D r2len=N d2len=M` line showing the pre-normalization 2D magnitudes. `d2len` near 1.0 means a flat (default) camera; `d2len` significantly less than 1.0 means a tilted camera. This makes "is this a tilted-camera field?" a one-line lookup in the field log.

Also adds a degenerate-projection safety check: if both `r2len` and `d2len` are essentially zero, the camera is looking straight down a single world axis and the screen-projection is undefined for direction labels. v0.17.0.1 keeps identity defaults in that case and logs a warning rather than dividing by ~0 and producing NaN.

Files changed: `src/ff8_accessibility.h` (version), `src/field_nav_fieldscripts.inl` (CA wiring now normalizes 2D projections, extra diagnostic log lines, degenerate-camera guard).

Not touched: `src/field_nav_gps.inl` (the projection math was correct in v0.17.0 — only the inputs to it were wrong), `src/field_navigation.cpp` `GPS_DIR_NAMES` cardinals (unchanged from v0.17.0), the chase auto-pilot's empirical calibration path (also unchanged — it normalizes correctly already).

BAT recipe: load any tilted-camera field (`bghall_1` is the canonical example). The `[NAV-PROJ-INIT]` line should now show `camDown` with 2D magnitude ~1.0 (e.g. `(0.132,-0.991)` for `bghall_1`) instead of `(0.044,-0.330)`. Cardinals announced during GPS guidance should match arrow keys. The `raw-2D` line records the pre-normalization magnitudes so the tilt-or-flat status is logged independently.

## v0.17.0

Field navigation, Bug 2 from the v0.16.5.2 BAT triage: manual GPS direction announcements were correct on some fields and inverted on others ("left" when the player actually needed to press right). Root cause was a coordinate-system mismatch: the GPS direction code computed the world-space bearing from raw entity coordinates and labeled it with screen-relative names, which works only on default-camera fields. On any field with a rotated camera — e.g. the Mountain Hideout chase fields, where the chase auto-pilot's empirical calibration found `camRight ≈ (0.04, 0.99)` instead of identity — the labeled direction did not match the arrow key the player needed to press.

The fix has two parts. First, the `.ca` (camera) file is already parsed at field load into the `CameraAxes` struct, but the parsed result was never being wired into the projection axes (`s_camRightX/Y`, `s_camDownX/Y`) that the direction code actually reads. Those projection axes were only ever populated by the chase auto-pilot's empirical calibration, which runs lazily on the first auto-drive of a field — i.e. on chase fields and nowhere else. The rest of the time they sat at their identity defaults. `field_nav_fieldscripts.inl` now derives the screen-right and screen-down vectors from `s_cameraAxes.axis0` and `axis1` (int16 fixed-point, normalized by /4096) and writes them into `s_camRightX/Y/DownX/DownY` immediately after `LoadCameraAxes()` returns. A `[NAV-PROJ-INIT]` log line records the derived axes and a 2D determinant per field load; degenerate cameras (|det| < 0.1) get a warning line. The chase auto-pilot's empirical calibration is untouched and will overwrite the CA-derived values on chase fields when it runs, which is fine because the two methods converge on the same axes for static cameras.

Second, `field_nav_gps.inl` no longer uses the world-space `atan2(dx, dy)`. The new `ComputeScreenDirIndex` projects the walkmesh delta through the now-correctly-populated camera axes (`screenRight = dx*camRightX + dy*camRightY`, `screenDown = dx*camDownX + dy*camDownY`) before classifying into one of eight 45° cardinal sectors. The cardinal vocabulary (north, northeast, east, southeast, south, southwest, west, northwest) replaces the old `up / up right / right / ...` labels; cardinals were chosen because they map unambiguously to arrow keys (north = up arrow, east = right, south = down, west = left) and are the canonical terminology already used by the chase auto-pilot. Aaron confirmed this convention before the rewrite.

GPS announcement cadence also changes. The old loop spoke a direction every 3 seconds while distance > 500 units, every 1.5 seconds while distance was in 200–500, and every 0.8 seconds while distance was under 200 — producing announcement bursts on long stretches where nothing meaningful was changing, and continuous spam in the final approach where the `Nearby` and `In range` one-shots already cover the player. New rule: in the nearby/in-range zone (distance ≤ `s_gpsNearbyDist`) GPS Updates are silent entirely, leaving messaging to the existing one-shots. Outside that zone, an Update fires only when (a) the cardinal sector changes — i.e. the player crosses into a new 45° wedge — or (b) the step count changes AND at least `GPS_ANNOUNCE_INTERVAL_FAR` (3 s) has elapsed since the last announcement. Direction changes break through the minimum-interval floor immediately; step-count-only changes wait it out so the player doesn't hear "11 steps… 10 steps… 9 steps…" in rapid succession on a long approach. State for this is two new statics in `field_nav_gps.inl`: `s_gpsLastDirIdx` (last announced cardinal index) and `s_gpsLastStepsAnn` (last announced step count), both reset by `StartGPS()` / `StopGPS()`.

A `[NAV-PROJ]` log line is added to both `StartGPS` (initial announcement) and the `UpdateGPS` periodic announce path. Each line records player position, target position, walkmesh delta, projected screen delta, and the chosen cardinal label. Combined with the `[NAV-PROJ-INIT]` line at field load, this gives full traceability for any direction the mod announces — if a future BAT exposes a direction that feels wrong, the log line shows exactly which step disagrees with reality. The diagnostic also serves the Bug 2 verification recipe: walk to a known target on a field where pre-v0.17.0 direction was inverted, confirm the announced cardinal matches the arrow key needed, and verify the `[NAV-PROJ]` math against what the player observes.

Scope of this version is the orientation layer only — GPS direction announcements (Backspace-triggered guided nav, F9-list-cycle-then-Backspace, F10 player-position-and-named-destination). Path-aware direction (the second half of Bug 2: "dir=up for 4000 units then suddenly up-left in the final 6 seconds" on bdin3, where the destination is correct but the player needs to follow a bend in the corridor) is queued for v0.17.1 and will reuse the existing A*+funnel infrastructure to target the next funnel waypoint instead of the final destination. Auto-drive integration (replacing the chase auto-pilot's parallel empirical calibration with the same CA-derived axes) is queued for v0.17.2+. v0.17.0 ships orientation alone so a single BAT cycle confirms or refutes the camera-projection approach in isolation before path-aware complexity goes on top — the v0.15.9 chase auto-pilot work hammered home that one-change-per-BAT cycle is the only way to attribute regressions cleanly.

Files changed: `src/ff8_accessibility.h` (version), `src/field_navigation.cpp` (`GPS_DIR_NAMES` array — cardinal vocabulary + comment update), `src/field_nav_fieldscripts.inl` (CA → `s_camRight/Down` wiring at field load, `[NAV-PROJ-INIT]` log), `src/field_nav_gps.inl` (full rewrite of `ComputeScreenDirIndex`, new sector-change cadence in `UpdateGPS`, `[NAV-PROJ]` diagnostic, new state `s_gpsLastDirIdx` / `s_gpsLastStepsAnn`).

Not touched, deliberately: the chase auto-pilot's empirical calibration path (`s_calibPending`, `s_camCalibrated`); the F9/F10 component-readout `FormatNavComponents` (it already uses `s_camRight/Down`, so it inherits the fix automatically); the post-v0.16.5.2 BAT triage's other five bugs (FMV STOP/PLAY race, POLL tutorial garble, formation-based party-member-as-NPC filter, GF-BP diagnostic gating, HP-TRACK during GF-HP-SUB) which remain backlog.

## v0.16.5.2

Defense-in-depth utility change — no mod code change. The DLL behavior is byte-for-byte identical to v0.16.5.1 except `Initialize()` will log `Initialized v0.16.5.2 ...` instead of `v0.16.5.1`.

Mirror the two checks in `.github/workflows/safety-checks.yml` locally in `Utilities/push_to_github.ps1` as new Step 7c, between the duplicate-commit refusal (Step 7b) and the session-header / cmd.exe invocation (Step 8). The CI workflow runs server-side AFTER a push lands — if it fails, the offending commit is already on `main` with a red X next to it, and the only notifications are an email to the committer and (next session) a Claude `github:list_commits` check. The push utility's own success dialog never reflects CI results, so a screen-reader user could come away believing a push succeeded when in fact CI was about to flag it. Step 7c closes that gap by running the same two checks locally and refusing the push (via the existing `Show-ErrorDialog` flow) if either would fail on the server.

### Checks added

- **SET3 hook marker** (mirrors CI job `check-set3-hook`): greps `src/field_navigation.cpp` for `SET3.*PERMANENTLY DISABLED`. The marker is a comment near the disabled SET3 hook block that documents the v0.09.32–v0.09.40 diagnosis showing ANY interception of the SET3 opcode handler hangs the infirmary scene (Dr. Kadowaki walk-to-phone freeze). If the marker is missing, the utility refuses with a screen-reader-readable error dialog naming the file and explaining the consequence.
- **Source file size** (mirrors CI job `source-file-size-check`): walks `src/*.{cpp,inl}` at depth 1 (matches the CI's `find src -maxdepth 1 \( -name '*.cpp' -o -name '*.inl' \)`), checks each file's byte size. Files > 60 KB log a WARN line to `Logs/push_diagnostic.log` as informational (matching CI's warn-but-don't-fail behavior). Files > 80 KB cause refusal with a dialog listing every offending filename and KB size, plus a pointer to the v0.16.0–v0.16.5 splits in CHANGELOG.md as the template for splitting.

### Thresholds (constants in both files)

- `WARN_BYTES = 60 * 1024 = 61440`
- `FAIL_BYTES = 80 * 1024 = 81920`

The duplication between `safety-checks.yml` and `push_to_github.ps1` is intentional and acceptable. The check needs to be fast and offline (no GitHub API round-trip), and the thresholds have been stable since v0.16.0. A header comment in each file points at the other so future maintenance keeps both in sync.

### What this catches vs. what it doesn't

Catches:
- A future source edit that crosses 80 KB without anyone noticing during the edit session.
- Accidentally removing the SET3 marker (e.g. during a refactor that touches `field_navigation.cpp`).
- Cases where Claude or another tool grew a file but didn't trigger a split.

Doesn't catch:
- Files in subdirectories of `src/` (CI also doesn't — only depth 1).
- `.h` files growing large (CI also doesn't — documented exception for `ff8_accessibility_history.h` and `field_display_names.h`).
- Server-side checks added in the future to `safety-checks.yml` that aren't also mirrored here (manual sync required).
- Bypass via direct `git push` from a terminal (the .ps1 is the only path that runs this check; CI is still the authoritative server-side backstop).

### Watch zone

Files in the 60–80 KB range log to `Logs/push_diagnostic.log` as `[Step 7c] Watch zone (60-80 KB, informational): ...`. This gives a passive trail of which files are creeping toward the limit, useful for spotting growth trends across multiple pushes without having to actively monitor. Current watch zone after v0.16.5: `field_archive_jsm_scan.inl` (63 KB, accepted exception), plus several `battle_tts_*.inl` files near the line.

### Future maintenance

If new safety checks are added to `safety-checks.yml` (e.g. a guard against inline-changelog accretion in source headers, or a check for forbidden imports), mirror them as additional sub-blocks under Step 7c. Each check should:
1. Run its detection logic.
2. Append a descriptive failure message to `$ciFailReasons` on failure (don't `exit 1` immediately — collect all reasons so the user sees them in one dialog).
3. Log PASS/FAIL/WARN to `Write-Diag` for the diagnostic trail.

The push utility now treats itself as the canonical client-side enforcement point; CI remains the authoritative server-side backstop in case the utility is ever bypassed.

## v0.16.5.1

Three-line fix wiring `PollDeferredTurnAnnounce()` into `battle_tts.cpp::Update()` after the existing `PollHPChanges()` call. Latent dead-code bug since v0.13.52 (2026-02 timeframe): the deferred turn-announce release function was defined in what is now `battle_tts_menu_poll.inl` but was never invoked anywhere. Whenever a character's ATB filled on the exact frame an enemy attack landed (or a teammate's GF / spell animation was still resolving), `PollTurnAndCommands` would correctly identify the collision, stash "X's turn. <Cmd>." in `s_deferredTurnBuf`, log `[TURN] Deferred (damage in flight): ...`, and set `s_deferredTurnPending = true`. The release path that was supposed to drain that buffer once the damage TTS cleared (or hit the 5-second safety timeout, or cancel on stale activeChar) was simply never called per-frame. The stashed line sat in the buffer until battle end, then got silently wiped when the next battle's `OnBattleEnter` reset state.

Discovered in the v0.16.5 BAT log triage: Selphie's third turn in battle 2 (timestamp 13:24:18, log line 2942) started on the exact frame Zell's Ifrit cast began animating. The defer line `[TURN] Deferred (damage in flight): Selphie's turn. Attack. (tts=0 hp=0 anim=0 engAnim=1)` appeared correctly, Ifrit's GF audio description played end-to-end (~23 s, all 6 cues), the battle continued, and ended ~78 s later — with no `[TURN] Deferred fired ...` or `[TURN] Deferred cancelled ...` log line ever appearing. Grep + dryRun probes across `battle_tts.cpp`, every `battle_tts_*.inl`, `battle_tts_hp.inl`, and `battle_tts_helpers.inl` confirmed no caller existed.

The v0.16.5 split was pure mechanical, so both the function body and the absent call site are byte-for-byte from v0.16.4 and back through v0.13.52. The split did not introduce the bug — it exposed it by giving the BAT triage a clear marker to look for.

### The fix

`src/battle_tts.cpp`, right after the existing `PollHPChanges()` block:

```cpp
if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
    PollDeferredTurnAnnounce();
}
```

Guards match the surrounding poll calls (battle active, init done, enemies announced). Placement after `PollHPChanges` matches the function's own header comment so `s_ewmHoldForDamageTTS` reflects this frame's HP signals before the release-decision is made. No change to the function body in `battle_tts_menu_poll.inl`.

### Reproducibility / verification

The trigger window is one frame wide — a teammate's ATB has to fill on the exact frame an attack lands or a GF animation kicks off. Not reliably reproducible on demand. Future battle log review will look for `[TURN] Deferred (damage in flight): ...` followed by `[TURN] Deferred fired after <ms> ms: ...` (success) or `[TURN] Deferred cancelled (char N -> M, stale): ...` (turn already advanced). Pre-fix, only the first line would appear; post-fix, one of the latter two should always follow within ~5 seconds.

### Impact

From the player's perspective: whenever this collision happened (probably a handful of times per dungeon run with junctioned GFs in play), the spoken "<Character>'s turn. <Command>." line was silently dropped, requiring the player to figure out whose menu was open via cursor probing or HP key inspection. With this fix, the line speaks within milliseconds of the damage window clearing, with `PRIO_TURN` interrupting anything lower-priority that might be queued.

## v0.16.5

Pure mechanical split of `src/battle_tts_menu.inl` (81.89 KB monolith — over the 80 KB CI hard-fail line) into a slim 1.05 KB shell plus four sub-`.inl` modules. No behavioral change; turn announcements, command-menu navigation, target selection, all four submenus (Magic / GF / Item / Draw), Stock/Cast prompts, all-target entry/cancel, deferred GF cancel, and the v0.13.52 deferred-turn TTS are byte-for-byte identical to v0.16.4.

This was the FINAL size-split task. With v0.16.5 shipped, every `src/*.cpp` and `src/*.inl` file is under the 80 KB CI hard-fail line. The CI allowlist in `.github/workflows/safety-checks.yml` is now empty. `field_archive_jsm_scan.inl` remains in the watch zone at 63 KB as an accepted exception (will warn but not fail).

Battle menu TTS is user-facing and load-bearing for accessibility (every command, every spell, every submenu cursor move must announce correctly), so this split deliberately did NOT touch the `PollTurnAndCommands` function body. Internal blocks of that ~52 KB function share local variables (`cmdCursorChangedThisFrame`, `subCursor`) and live inside one outer SEH guard — splitting them into separate helper functions would have required scope restructuring that risks regressing menu announcements. The block stays whole in `_poll.inl`.

### New files

- `src/battle_tts_menu_state.inl` (16.2 KB) — all constants (`BATTLE_CMD_CURSOR`, `BATTLE_MENU_PHASE`, `BATTLE_SUBMENU_CURSOR`, `SAVEMAP_*`, `BATTLE_ITEM_*`, `DRAW_*`), name tables (`CHAR_NAMES[8]`, `MAGIC_NAMES[57]`, GF fallback names), free lookups (`GetCommandName`, `GetMagicName`, `GetDrawEntryName`), struct definitions (`MagicEntry`, `GFEntry`, `BattleItemEntry`, `DrawEntry`, `MagicSlot`), all module statics (sub-menu state, deferred-turn TTS, Magic/GF/Item/Draw list state, magic-snapshot state, turn-tracking state). Hoisted to the head of the chain so every subsequent sub-`.inl` sees these declarations.
- `src/battle_tts_menu_lists.inl` (9.6 KB) — the per-turn list builders: `BuildMagicList`, `BuildGFList`, `BuildItemList` (plus `ReadBattleItemEntry` and `GetItemVisualPos` helpers), `BuildDrawList`, and the v0.12.52 Draw-validation pair `SnapshotAllMagicInventories` / `DiffMagicInventories`. All read state from `_state.inl`; consumed by `EnterSubmenu`, `PollTurnAndCommands`, and (for `DiffMagicInventories`) the dialog-injection "Received" path.
- `src/battle_tts_menu_helpers.inl` (3.0 KB) — the per-frame helpers: `EnterSubmenu` (the v0.13.49 shared entry helper called from all three detection paths — submenu mode, dword detection, subCursor change), `GetBattleCharName` (savemap-driven char-name lookup, distinct from `GetSlotName` for the battle entity array), `BuildCharCommandList` (reads savemap `+0x50` equipped-command IDs into `s_turnCharCommands[]`).
- `src/battle_tts_menu_poll.inl` (55.4 KB) — `PollTurnAndCommands` (the per-frame menu state machine: turn-start/end detection, char→char GF HP substitution arming, command cursor navigation, submenu debounce, target-phase entry/exit via `BATTLE_MENU_PHASE`, submenu entry via `0x01D768EB` mode byte, submenu entry via `BATTLE_MENU_PHASE` dword function-pointer detection, subCursor announcement routing per submenu type, v0.10.112 delayed submenu entry, Draw-specific cursor + Stock/Cast polling, v0.12.72 deferred GF cancel, v0.12.66 all-target entry/cancel via `0x01D7689D`) plus `PollDeferredTurnAnnounce` (the v0.13.52-53 deferred turn TTS release path).

### Include chain

Dependency-ordered, included textually from the slim `battle_tts_menu.inl` parent (which is itself included from `battle_tts.cpp` inside `namespace BattleTTS`):

```
state → lists → helpers → poll
```

`state.inl` must come first (declares every static and every struct). `lists.inl` reads state, defines builders. `helpers.inl` reads state, calls list builders. `poll.inl` consumes everything above.

### Statics also referenced by `battle_tts.cpp`

The `OnBattleEnter` reset block in `battle_tts.cpp` writes to many statics that now live in `battle_tts_menu_state.inl`: `s_turnActiveCharId`, `s_turnCmdCursor`, `s_turnCharCommands[]`, `s_inSubmenu` (declared in `battle_tts_hp.inl` since v0.13.51, modified from menu code), `s_turnSubmenuCursor`, `s_submenuCommandId`, `s_magicListBuilt`, `s_turnMagicCount`, `s_gfListBuilt`, `s_turnGFCount`, `s_itemListBuilt`, `s_turnItemCount`, `s_drawListBuilt`, `s_turnDrawCount`, `s_drawTargetSlot`, `s_drawCursorPrev`, `s_drawStockCastPrev`, `s_lastDrawerPartySlot`, `s_drawLastMenuPhase`, `s_pendingSubmenuEntry`, `s_pendingSubmenuTick`, `s_submenuDebouncing`, `s_submenuDebounceTick`, `s_limitBreakActive` (declared elsewhere), `s_lastLimitToggle` (declared elsewhere). `CHAR_NAMES[]` is read by `GetCharNameById` in the shared-victory section. All of these work because the `.inl` chain is textually included inside `battle_tts.cpp` BEFORE `OnBattleEnter` — file-scope `static` visibility carries across the include boundary, identical to the v0.16.4 pattern.

### CI status

Largest sub-file is `_poll.inl` at 55.4 KB — under the 60 KB warn line. The other three sub-files (state 16.2, lists 9.6, helpers 3.0) are comfortably small. Slim parent is 1.05 KB. Total split footprint is 85.3 KB versus the original 81.9 KB; the ~3 KB overhead is per-file orientation comment headers explaining each module's purpose and dependency position.

`battle_tts.cpp` is unchanged — it still `#include`s `battle_tts_menu.inl` exactly as before; the content beneath that include simply expanded into the four sub-files. `deploy.bat` is unchanged (`.inl` files are textually included, only the parent `.cpp` compiles).

### Allowlist emptied

The `.github/workflows/safety-checks.yml` allowlist that protected the queued v0.16.x splits is now empty. The four entries (`field_dialog.cpp` for v0.16.2, `field_archive_jsm.inl` for v0.16.3, `battle_tts_ewm.inl` for v0.16.4, `battle_tts_menu.inl` for v0.16.5) were all stale — the corresponding files have all been carved into slim 1–3 KB shells. Removing them closes the refactor chapter that began with v0.16.0's `world_map.cpp` split.

## v0.16.4

Pure mechanical split of `src/battle_tts_ewm.inl` (91.79 KB monolith — over the 80 KB CI hard-fail line) into a slim 2.17 KB shell plus nine sub-`.inl` modules. No behavioral change; the EWM freeze, GF fire prevention, dispatch hooks, FFNx hook, and per-frame state machine are byte-for-byte identical to v0.16.3. Pattern matches the v0.16.0 (`world_map.cpp`), v0.16.1 (`chase_auto_pilot.cpp`), v0.16.2 (`field_dialog.cpp`), and v0.16.3 (`field_archive_jsm.inl`) splits.

EWM is the load-bearing core of the turn-based retrofit ("first-to-fill acts first, no skipped turns, natural ally/enemy ratio"), so this split deliberately did NOT touch the v0.13.57 ATB-restore semantics or any of the dispatch / cooldown / grace-period logic. Every static, comment, and `__try` block is preserved verbatim; only locations moved.

### New files

- `src/battle_tts_ewm_state.inl` (8.4 KB) — all module statics, typedefs, structs, constants. Hoisted to file head so every sub-`.inl` can reference them. Includes the `TargetDiagSnapshot` struct, all `s_ewm*` lifecycle flags, dispatch hook counters (`s_processReadyCalls/Blocks/Passes`, `s_actionExecuteCalls/Blocks/Passes`), diagnostic state (`s_diagPrevDamageAnim`, `s_prevSlotATB[]`, `s_slotTurnCount[]`), and the two function-pointer typedefs (`ProcessReadyFn`, `ActionExecuteFn`, `FFNxBattleUpdateFn`, `BattleEffectFn`).
- `src/battle_tts_ewm_gf_patch.inl` (8.9 KB) — GF fire prevention layer: `HookedGFTimerUpdate`, `EWM_ClampGFState` (the three-layer prevention: 0x004B04B4 MOV→RET code patch + state68 clamp + timer function skip), `EWM_RestoreGFPatch`, `GF_LogHookStats`, `GF_PollStateChanges`, `EWM_InstallGFHook`.
- `src/battle_tts_ewm_gf_effect.inl` (6.9 KB) — battle_magic_id polling (v0.12.48-49) for GF animation fire detection and Scan effect handling: `IsGFEffectId`, `GFEffectIdToIndex`, `FindPartySlotForGF`, `PollBattleMagicId` (handles GF anim fire AND Scan effect ID 39 with target bitmask resolution), `EWM_InstallBattleEffectHook`.
- `src/battle_tts_ewm_bp_diag.inl` (17.1 KB) — hardware breakpoint diagnostic (DR0 VEH + ToolHelp32 thread enumeration), target-selection diff diagnostic, and function entry scanner: `GF_BP_VectoredHandler`, `GF_BP_ArmAllThreads`, `GF_BP_AutoArm` (display-timer ≤3 arm window), `TgtDiag_TakeSnapshot`, `GF_BP_PollKey` (no-op since v0.11.01), `GF_ScanForFunctionEntry`.
- `src/battle_tts_ewm_atb_hook.inl` (12.3 KB) — `HookedATBUpdate` (the core ATB freeze sandwich with v0.13.57 exact-value restore semantics) plus EWM lifecycle/toggle: `EWM_LoadConfig`, `EWM_SaveConfig`, `EWM_PollToggle` (O-key toggle), `EWM_InstallHook`. The lifecycle functions live with the ATB hook because EWM_InstallHook is the installer for HookedATBUpdate and the toggle gates its behavior.
- `src/battle_tts_ewm_dispatch.inl` (5.7 KB) — v0.13.55/56 dispatch-layer hooks (sub_483470 + sub_482F80): `HookedProcessReady`, `HookedActionExecute`, `EWM_InstallProcessReadyHook`, `EWM_InstallActionExecuteHook`, `EWM_LogDispatchStats`.
- `src/battle_tts_ewm_ffnx.inl` (9.7 KB) — v0.10.77 FFNx GF loading counter hook: `HookedFFNxBattleUpdate` (the GF loading counter cap-at-max-1 sandwich), `FindFFNxModuleBase` (via the E9 JMP at set_midi_volume 0x0046BB40), `ScanModuleForSignature` (sig: `B9 16 F0 CF 01 66 89 06`), `ScanAllModulesForSignature`, `FindFunctionEntry` (backward CC/90/C3 padding scan), `EWM_InstallFFNxGFHook`.
- `src/battle_tts_ewm_diag.inl` (12.0 KB) — diagnostic helpers: `EWM_IsExecutingPhase` (phases 14/21/23/33/34), `EWM_FormatATBSnapshot`, `EWM_PollDiagnostics` (v0.13.57 transition logger for [0x01D280C0]/[0x01D27B00]/s_ewmShouldCap + post-release trace window), `EWM_ResetTurnCount`, `EWM_LogTurnCountSummary`, `EWM_TrackTurnCount` (v0.13.58-60 per-slot ATB high→low turn counter), `EWM_DiagLogATB`.
- `src/battle_tts_ewm_update.inl` (13.8 KB) — `EWM_UpdateBattle`, the per-frame freeze state machine. Calls helpers from `gf_patch.inl` (`EWM_ClampGFState`, `EWM_RestoreGFPatch`) and `diag.inl` (`EWM_PollDiagnostics`, `EWM_IsExecutingPhase`, `EWM_DiagLogATB`), so must come last in the include chain.

### Include chain

Dependency-ordered, included textually from the slim parent (which is itself included from `battle_tts.cpp` inside `namespace BattleTTS`):

```
state → gf_patch → gf_effect → bp_diag → atb_hook → dispatch → ffnx → diag → update
```

`state.inl` must come first (declares every static). `update.inl` must come last (calls helpers from `gf_patch` and `diag`). Everything between is independent and could be reordered; the chosen order groups related concerns (GF prevention → diagnostics → core hook → dispatch → FFNx → diag helpers → state machine).

### Statics also referenced by `battle_tts.cpp`

A few statics declared in `state.inl` are also referenced by `battle_tts.cpp` itself (e.g. `OnBattleEnter` resets `s_gfSnapValid`, `s_gfSnapLastTick`, `s_gfAutoArmLastActive`, `s_gfAutoArmDone`, `s_tgtDiagStage`; `Initialize` and `Shutdown` reference `s_gfVEHHandle` for the AddVectoredExceptionHandler / RemoveVectoredExceptionHandler pair). These work because the `.inl` chain is textually included inside `battle_tts.cpp` BEFORE the functions that use them — file-scope `static` visibility carries across the include boundary.

### CI status

Largest sub-file is `bp_diag.inl` at 17.1 KB — comfortably under the 60 KB warn line. Smallest is the slim parent at 2.17 KB. Total split is 96.83 KB versus the original 91.79 KB; the ~5 KB overhead is per-file orientation comment headers explaining each module's purpose.

`battle_tts.cpp` is unchanged — it still `#include`s `battle_tts_ewm.inl` exactly as before; the `.inl` content beneath that include simply expanded into the nine sub-files. `deploy.bat` is unchanged (`.inl` files are textually included, only the parent `.cpp` compiles).

### Remaining size-split work

After v0.16.4 ships, the size-split sequence is one file from done: v0.16.5 splits `src/battle_tts_menu.inl` (82 KB). With that, every source file in the project is under the 80 KB CI hard fail, and the allowlist in `.github/workflows/safety-checks.yml` can be emptied.

## v0.16.3

Split `src/field_archive_jsm.inl` (91 KB monolith, over the 80 KB CI hard-fail line) into a slim 2 KB shell plus seven sub-`.inl` modules. The JSM scanner pipeline was the last source file over the size limit; with this split, the field-archive subsystem stays under the 80 KB cap.

### Strategy

This was a small-refactor split rather than a pure mechanical one. Two changes:

1. **State hoist.** The cross-pass `static` arrays inside `ScanJSMScripts()` (`s_methodMapjumps`, `s_entityReqs`, `s_entityPopms`, `s_initVarMaps`, `s_reqOpcodeCount`, `s_hasSetmodelInit`, `s_hasDialogAny`, `s_hasExtDispatchArr`) and their containing struct definitions (`MethodMapjump`, `ReqCallInfo`, `EntityReqs`, `EntityPopms`, `VarWrite`, `EntityVarMap`) were promoted from function-local to namespace scope so the Director post-pass can share them. Function-local `static` already has program lifetime; the move is visibility-only. The explicit `memset` block at scan entry remains and preserves the zero-on-entry contract identically.
2. **Director helper extraction.** The DIAGNOSTIC + Director-detection-post-pass blocks (originally a v0.12.20 addition with its own bounded scope) were extracted verbatim into a new `RunDirectorDetection()` helper. `ScanJSMScripts()` now calls it as a single line after the draw-point trigger cross-reference completes.

Behavior is byte-for-byte identical to v0.16.2.

### New files

- `src/field_archive_jsm_state.inl` (4.4 KB) — hoisted struct decls, size constants (`MAX_METHOD_MAPJUMPS`, `MAX_PSHM_PER_METHOD`, etc.), the eight cross-pass `static` arrays, and the `RunDirectorDetection` forward declaration.
- `src/field_archive_jsm_constants.inl` (6.5 KB) — `JSM_OP_*` opcode ID constants and `JSMEntityTypeName()` lookup.
- `src/field_archive_jsm_helpers.inl` (2.1 KB) — `GetFieldIdByInternalName`, `SwapBE32`, `DecodeJSMInstruction`.
- `src/field_archive_jsm_opnames.inl` (2.6 KB) — `GetOpcodeName()` lookup for the script-dump diagnostic.
- `src/field_archive_jsm_director.inl` (10.4 KB) — `RunDirectorDetection()` post-pass: the `[DIR-DIAG]` log emitter and the Director identification + dispatch-target promotion logic, including the party-character SYM filter.
- `src/field_archive_jsm_scan.inl` (63.3 KB) — `ScanJSMScripts()` main body with the Director block replaced by a single helper call and the consolidated memset block referencing the namespace-scope arrays.
- `src/field_archive_jsm_dump.inl` (7.1 KB) — `DumpEntityScript()` diagnostic.

### Include chain

Dependency-ordered, included textually from the slim parent inside `namespace FieldArchive` (which is itself included from `field_archive.cpp`):

```
state → constants → helpers → opnames → director → scan → dump
```

`state.inl` must come first because it declares the cross-pass arrays and the helper forward decl; everything else depends on those. `director.inl` precedes `scan.inl` so the helper body acts as its own declaration when `scan.inl` calls it.

### CI status

`scan.inl` lands at 63.3 KB — just over the 60 KB warn line but well under the 80 KB hard fail. The 91 KB parent monolith is gone; the largest single piece of the JSM scanner is now ~70% of the CI hard limit. The dense per-entity opcode-scan loop is what keeps `scan.inl` chunky; further splitting would require breaking the loop into sub-helpers, which crosses the line from mechanical extraction into behavior-touching refactor. Holding off until there's a functional reason to revisit.

`field_archive.cpp` is unchanged — it still `#include`s `field_archive_jsm.inl` exactly as before; the `.inl` content beneath that include simply expanded into the seven sub-files. Public-API surface (`JSMEntityTypeName`, `ScanJSMScripts`, `DumpEntityScript` declared in `field_archive.h`) is identical.

### Why

v0.15-era debugging on the X-ATM092 chase repeatedly touched both the JSM scanner (for Background-entity classification fixes) and the Director detection logic (for dormitory-field interactive-object promotion). With both living in a 91 KB monolith, surgical edits to either side required scrolling through the other. The split lets future Director-detection work happen in a 10 KB file and leaves `scan.inl` focused on the per-entity opcode pass.

## v0.16.2

Pure mechanical split of `src/field_dialog.cpp` (88 KB monolith → 3 KB slim parent + 8 `.inl` files). No functional change. Pattern matches the v0.16.0 (`world_map.cpp`) and v0.16.1 (`chase_auto_pilot.cpp`) splits.

### New files

- `src/field_dialog_state.inl` — typedefs, all module-static state, struct definitions (`WindowState`, `PendingText`), window-object layout constants, FMV-poll state, show_dialog dedup state, and the `MarkPendingAsSpoken` forward declaration.
- `src/field_dialog_helpers.inl` — pointer validation (`IsValidTextPointer`, `ProbePointer`, `ProbeGetstrResult`), window accessors (`GetWindowObj`, `GetWinText1/2`, `GetWinOpenCloseTransition`), text helpers (`TrimDecoded`, `IsSuffixOrSubstring`, `fnv1a_prefix`), `CreateDetourHook`.
- `src/field_dialog_scan.inl` — the central TTS-speak path: `ScanAndSpeakAllWindows`, `ScanAndSpeakChoiceWindows`, `MarkPendingAsSpoken`, `CheckPendingTexts`.
- `src/field_dialog_show_dialog.inl` — `Hook_show_dialog` with OOR diagnostic, FNV-1a hash dedup, scan-active suppression, chase overlay forward, battle drawer-name decoration.
- `src/field_dialog_opcodes.inl` — opcode hooks (mes/mesw/ask/ames/aask/amesw), diagnostic opcode hooks (tuto/mesmode/ramesw), `Hook_field_get_dialog_string` with the DialogInject override path, and `RepeatLastDialog`.
- `src/field_dialog_diag.inl` — dispatch instrumentation (`DispatchStub`, `DispatchStub_EDX`, `PatchDispatchSite`, `UnpatchDispatchSite`), naked counter hooks, `Hook_get_character_width` + `CheckGcwBuffer`, `DiagRawWindowDump`.
- `src/field_dialog_menuname.inl` — `Hook_opcode_menuname` with GF-diff-on-acquire detection and naming-screen UI suppression.
- `src/field_dialog_lifecycle.inl` — `Initialize`, `Shutdown`, `PollWindows` (FMV-aware polling fallback).

### Include chain

Dependency-ordered, included textually from the slim parent inside `namespace FieldDialog`:

```
state → helpers → scan → show_dialog → opcodes → diag → menuname → lifecycle
```

The parent retains the tiny public-API tail (`IsActive`, `IsDialogOpen`, `GetMenuDrawTextCallCount`, `GetGetCharWidthCallCount`, `SnapshotGcwBuffer`) for visibility — everything else is in the `.inl` chain. Build script (`src/deploy.bat`) unchanged: `.inl` files are textually included, only the parent `.cpp` compiles.

### CI guard

60 KB warn / 80 KB hard-fail thresholds (`.github/workflows/safety-checks.yml`) respected. Largest new file is `field_dialog_lifecycle.inl` at ~12 KB; all others under 25 KB. The 88 KB monolith no longer trips the limit.

### Why

Readability + future-proofing. The v0.16.x refactor sequence is carving every source file over 60 KB into focused `.inl` modules so single-area edits stop touching half the dialog system. `field_dialog.cpp` was the second-largest remaining offender after the v0.16.1 chase split.

## v0.16.1.4

Corrects the doopen2a auto-pilot route based on Aaron's manual chase BAT (2026-05-16 21:10:38-21:10:47), which successfully cleared Town Square 5 in 9 seconds total with 0 catches. The `ff8_nav_data.log` COORD trace captured every triangle change of the manual run and is the source of truth for the new threshold.

### Manual run trace

```
t=0       (-974, -166)   spawn          tri 52
t=0+      (-856, -450)                  tri 51
t=1       (-783, -669)                  tri 49
t=1.5     (-629, -891)   MAX EAST       tri 46
t=2       (-662, -1351)                 tri 97
t=2       (-725, -1559)                 tri 96
t=2       (-750, -1805)                 tri 94
t=2       (-753, -1836)                 tri 89
t=2       (-777, -2082)                 tri 88
t=2       (-780, -2113)                 tri 85
t=3       (-807, -2391)                 tri 37
t=3       (-825, -2576)                 tri 33
t=3       (-871, -3038)                 tri 14
t=3       (-874, -3069)                 tri 79
t=4       (-940, -3293)                 tri 148
t=4       (-964, -3313)                 tri 23
t=4       (-1068, -3542) EXIT TRIGGER   tri 151
```

Shape: SE briefly (~1.5s, max east X=-629), then SOUTH along the western corridor with natural west drift. Exit triggered in the SW corner around `(-1068, -3542)`. Aaron's TALKRAD log also showed battleyarou's catch radius expanded from 500 to 700 at the 7-second mark (`[TALKRAD] CHANGED @0x1F8: 500 -> 700` at 21:10:45, context `@21E 0->2 @244 0->3`) -- the chase mechanic is "outrun an expanding catch radius", not "avoid a fixed circle."

### Critical correction: kani has no active proximity catch on doopen2a

Aaron's path passes within **162 units** of kani at `(-685, -2284)` (at position `(-807, -2391)`) and is not caught. The pre-v0.16.1.4 commentary that attributed v0.16.1.2's t=3 catch to kani was wrong. That catch's caller in the `[CBF] PASS` log was always `entityPtr=0x0188CA04` (battleyarou). Battleyarou fired BATTLE in v0.16.1.2 from 1447 units away -- probably velocity- or motion-vector-based rather than pure proximity. The exact mechanism remains unidentified, but the empirical fact is that Aaron's east-first / west-corridor route avoids it while a direct west-wall A* path triggers it.

Battleyarou's *static* TALKRAD=500 around its JSM init position `(0, -744)` is a real proximity catch, confirmed by v0.16.1.3 BAT (auto-pilot at `(-446, -821)`, 453 units from `(0, -744)`, caught at t=1). Aaron's max-east excursion to `(-629, -891)` was 646 units from `(0, -744)` -- 146-unit margin.

### Fix

`src/chase_auto_pilot_route.inl`: SE-stage threshold in `kStages_doopen2a[]` tightened from `Y < -1500` to `Y < -631`. The new threshold ends stage 0 at approximately `(-629, -631)` -- same X as Aaron's max-east excursion, 639 units from battleyarou's catch center (139-unit margin). Stage 1 (pure south) then drifts west naturally at -76/sec from the camDown vector `(-0.097, -0.995)`, walking the party along the western corridor to the SW screen-boundary trigger at approximately `(-905, -3447)` after ~3.6 more seconds. Total field time: ~4.25 seconds, well under the 7-second TALKRAD expansion.

### Walk vs run

Unchanged: `walk=false` on both stages. Aaron's manual run was at running pace; the slower observed rate compared to the auto-pilot's top speed is from walkmesh constraints and analog thumb angle, not a forced walk modifier.

### Expected v0.16.1.4 BAT signature

- Auto-pilot ENGAGED on doopen2a, MODE_STAGED_DIRECTION, stage 0/2 SE.
- `tick=60` log line approximately at `pos=(-629, -631)` or thereabouts, with `bydist >= 639`.
- Stage transition to S after Y crosses -631.
- Field transition to dotown_3 at approximately t=4-5 seconds.
- No `[CBF] PASS` BATTLE call on doopen2a.

### v0.16.1.3 reverted in spirit

The MODE_STAGED_DIRECTION mode is kept; only the SE-stage threshold changes. v0.16.1.3's threshold of `Y < -1500` was based on the false kani-proximity model and is replaced with the empirically-derived `Y < -631`.

## v0.16.1.3

Functional fix for the X-ATM092 chase catch on doopen2a (Town Square 5). The v0.16.1.1 diagnostic and v0.16.1.2 funnel-collapse BATs both reached BATTLE at ~3 seconds on doopen2a regardless of upstream timing, which finally clicked into place after Aaron's 2026-05-16 confirmation that the catch IS proximity-based ("that is how the chase scene works -- when the robot catches you then you end up in a fight") and that the field is not difficult for a sighted player following his recipe: "first have to go southeast (down and right) several steps, then due south to the exit gateway."

### Root cause

The v0.15.9.8 doopen2a config used `MODE_TARGET (-952, -3800)`, aiming at a SETLINE south trigger center the v0.15.9.7.8 fallback-mis-selection comment described as the chase exit. The A* + funnel pipeline routed the party along the WEST wall of the field (per v0.16.1.2 BAT portal data: portal 0 `L=(-1022,-99) R=(-769,53)` through portal 19 `L=(-1171,-2693) R=(-1180,-2525)`, X range ~-769 to -1180). The party's actual path held X around -800 to -900 throughout the south leg.

Kani sits at `(-685, -2284)` mid-field with TALKRAD set to 500 on field load (per the `[TALKRAD] CHANGED @0x1F8: 128 -> 500` log line that fires on every doopen2a engage). The west-wall path crossed within 165 units of kani's X column when the party reached kani's Y band -- well inside the 500-unit catch radius. The chase script's proximity check fired BATTLE at ~3 seconds reliably across multiple BATs. The v0.16.1.1 diagnostic captured the closing pattern in the new `kdist` per-tick log: 1837 -> 1061 -> caught, with `bydist` (party-to-origin distance for the UNUSE'd battleyarou entity) increasing in lockstep as the party moved away from world origin -- confirming kani, not battleyarou, as the proximity source.

### Why the v0.16.1.2 funnel-COLLAPSE didn't help

The v0.16.1.2 BAT confirmed Fix B fired correctly on domt2_1 (`[funnel] COLLAPSE wall-parallel portal 23 ... -> wp=(-13,-1508) tri 26->27`) and the party reached the new collapsed waypoint cleanly. But the 5-second stuck on domt2_1 at `(8, -1602)` persisted unchanged. That stuck is the scripted X-ATM092 landing animation Aaron confirmed plays on domt2_1 ("the field with the robot-jump-down animation, right before the bridge"). It is immutable game cinematic, not a pathing bug, and doesn't affect doopen2a timing because the robot's position resets at each field boundary. v0.16.1.2 was a clean swing-and-miss against the actual problem; the funnel-collapse code stays in for other walkmesh cases (no regression risk shown), but it's not what fixes the chase.

### Fix

New `MODE_STAGED_DIRECTION` config for doopen2a in `src/chase_auto_pilot_route.inl`, modelled on the domt5_1 stage table. Two stages match Aaron's recipe:

- Stage 0: `dirX=+1, dirY=+1` (south-east), running, active while `Y > -1500`. Several steps of SE motion push the party east of `X = -185` (kani's TALKRAD radius east boundary) before reaching kani's Y line. At doopen2a's calibrated camera (`camRight=(0.927,-0.376)`, `camDown=(-0.097,-0.995)`), SE input produces approximately `+407` east and `-672` south per second of world motion. Clearing `dX = 789` east (from spawn `X=-974` to safe `X=-185`) takes ~1.94 seconds, by which point `dY = -1303` south -- the threshold is set to `Y < -1500` with a ~90-unit safety margin.
- Stage 1: `dirX=0, dirY=+1` (pure south), running, active while `Y <= -1500`. Pure south to cross the exit gateway at `Y=-3414` (Screen Boundary line, X range `[-497, 311]` per the v0.16.1.2 BAT `gateway crossing line (-497,-3414)->(311,-3414)` log). Party X is held at the value reached during stage 0 (~-185 or further east), keeping kdist >= 500 throughout the south leg.

The exit-gateway target also corrects a long-standing misidentification of the chase exit. The v0.15.9.8 comment treated the south SETLINE at `Y=-3703` (center `(-952, -3703)`, X range `-1091` to `-814`) as the chase exit. That line is in the west of the field and was the target the A* path was aimed at. The actual chase exit per the BAT-logged `gateway crossing line` is at `Y=-3414` with `X` range `[-497, 311]`, in the center-east of the field -- exactly where the SE -> S route ends up.

### Diagnostic logging from v0.16.1.1 remains in place

- `ReadBattleyarouPosition` helper + ` by=(X,Y) bydist=N` per-tick log suffix in `chase_auto_pilot_update.inl`.
- `_ReturnAddress()` capture on the `[CBF] PASS` line in `chase_battle_freeze.cpp`.

Future chase regressions will surface in these logs without re-shipping diagnostic code.

### Expected v0.16.1.3 BAT signature

- doopen2a transit: ~5 seconds (1.94s SE + ~3s S), 0 catches.
- Per-tick log shows party moving south-east during stage 0 with `pos.X` increasing from ~-974 toward 0 (or close), then southbound with `pos.X` held near -100 to -200.
- `kdist` minimum ~545 (party at X=-185, kani at X=-685, same Y -- never closer).
- No `[CBF] PASS` line on doopen2a; field transitions to dotown_3 via the gateway crossing line at Y=-3414.

If the chase still catches:
- Check the BAT log for the per-tick `pos.X` trajectory during stage 0. If party X stays under -400 (didn't move east enough), the camera-mapping math is off and the threshold needs lowering (more negative Y to allow more SE travel time).
- If party moves east correctly but kdist still drops below 500 in stage 1, kani's TALKRAD is wider than 500 or her tracked position differs from the BAT-logged value. Re-derive thresholds.
- If party reaches the exit but the chase doesn't transition out, the gateway crossing line is on a different Screen Boundary than expected. Dump the `squall` / `zell` SETLINE entries to see which one corresponds to the south gateway.

## v0.16.1.2

Functional fix for the deterministic doopen2a catch identified by the v0.16.1.1 diagnostic BAT. The catch was not proximity-based (battleyarou reads as static at `(0,0)` across all chase fields per the new `by=(X,Y) bydist=N` per-tick log) and not a non-script controller (no FFNx hook detour; the chase script just runs out of session-budget). Total chase time domt5_1->BATTLE = 51 seconds, of which 5 seconds are eaten by a stuck on domt2_1 at `(3, -1603)`. Removing that 5 seconds gives doopen2a enough headroom for the south trigger to fire before the chase script does.

### Root cause

The SSFA (Simple Stupid Funnel Algorithm) in `src/field_nav_pathfinding.inl::FunnelPath` includes a wall-parallel portal optimization added in v06.01: portals whose endpoints lie on the same vertical line (`absDX < WALL_PARALLEL_EPSILON && absDY > 10 * epsilon`) are skipped via `continue` before being added to the portal list. The justification, validated against all 894 game walkmeshes offline, is that these portals "run ALONG a wall, not across the walkable corridor."

That reasoning holds for the bg2f_1 case the heuristic was tuned against (a long open corridor whose left/right inner walls happen to be exposed as wall-parallel portals between adjacent corridor triangles). It fails for tight chase fields like domt2_1, where the wall-parallel portal IS the corridor: tri 26 -> tri 27 has exactly one shared edge, a vertical doorway at `x=-42` spanning `y=-1638` to `y=-1360`. Skipping the portal removed the only aim point inside the doorway. The player at `(3, -1603)` saw a steer vector pointing to wp 23 at `(-64, -1658)` -- mostly south, slightly west -- which the camera projection (`camRight=(0.860,-0.510)`, `camDown=(-0.619,-0.785)`) converted to analog dominated by south. Player walked south into the `x=-42` wall, slid east-west along it for 2 seconds (`moveDist=160` with zero net displacement), then froze entirely (`moveDist=0`) for another 2 seconds before velocity-stuck recovery advanced wp 23 -> 24 -> 25 -> 26 over a total of 5 seconds. Each waypoint skip took ~1 second because each new waypoint also lived on the far side of the same wall.

### Fix B (default behavior)

When a wall-parallel portal is detected, emit a single "forced waypoint" at the portal midpoint shrunk inward by `AGENT_RADIUS` (30 units) toward triB's center, rather than `continue`-ing past it. The SSFA treats `L == R` as a pass-through constraint, so the funnel produces a waypoint exactly at the doorway and the player aims through it. New log line: `[funnel] COLLAPSE wall-parallel portal N dX=... dY=... L=(...) R=(...) -> wp=(...) tri A->B`. Summary log on field load: `[funnel] N wall-parallel portals processed (SKIP if SKIP_WALL_PARALLEL_LEGACY else COLLAPSE; v0.16.1.2 default = COLLAPSE)`.

### Fix A (fallback toggle)

A `static const bool SKIP_WALL_PARALLEL_LEGACY = false` inside the wall-parallel branch restores the v0.16.1.1 `continue` behavior globally when flipped to `true`. Intended as a one-line + rebuild mitigation if Fix B turns out to regress on bg2f_1 or other long-corridor fields where the original SKIP was correct. If a per-field toggle becomes necessary, we lift the constant to a route-config field instead. The toggle's legacy path emits `[funnel] SKIP wall-parallel portal N (LEGACY)` so BAT logs distinguish the modes.

### Diagnostic logging retained from v0.16.1.1

- `ReadBattleyarouPosition` and the ` by=(X,Y) bydist=N` suffix on ChaseAutoPilot per-tick logs stay in place. Useful for confirming battleyarou continues to read as `(0,0)` on chase fields (proximity catch falsified) and for diagnosing future chase regressions.
- `_ReturnAddress()` capture on the `[CBF] PASS` line stays. Useful for tracing future BATTLE invocations through the FFNx hook chain.

### Expected v0.16.1.2 BAT signature

**Chase clears cleanly**:
- `[funnel] COLLAPSE wall-parallel portal N` appears once during the domt2_1 chase-drive (between A* and the chase-drive STARTED log).
- domt2_1 transit time drops from ~14s to ~9s (the 5s stuck at `(3, -1603)` is gone).
- Total chase time domt5_1 -> doopen2a south trigger arrives well under 51s.
- No `[CBF] PASS` line on doopen2a; the chase ends with a field transition to dotown_3 (or whatever follows).

If chase still catches:
- Check the BAT log for `[funnel] COLLAPSE` lines to confirm Fix B fired.
- If COLLAPSE fired but domt2_1 transit is still 14s, the wall-parallel portal wasn't the bottleneck and we need to revisit (memory scan for chase timer or other approaches).
- If COLLAPSE did NOT fire (the wall-parallel detection missed the portal), the threshold values need tightening.

If bg2f_1 or other fields regress:
- Flip `SKIP_WALL_PARALLEL_LEGACY` to `true` in `field_nav_pathfinding.inl::FunnelPath` and rebuild. This restores the v0.16.1.1 behavior pending a per-field toggle.

## v0.16.1.1

Diagnostic build investigating the reproducible X-ATM092 catch on doopen2a (Town Square 5) discovered in the v0.16.1 BAT. The catch fires ~4 seconds after entering doopen2a regardless of party progress -- party position at BATTLE time (-853, -1266) is still ~2500 units short of the target trigger at (-952, -3800). Two consecutive BATs (same save state) both caught the party in the square; the regression is deterministic, not marginal.

Three small additions, all pure diagnostic logging -- no behavior changes:

### (1) `ReadBattleyarouPosition` in `src/chase_auto_pilot_io.inl`

New SEH-guarded helper mirroring `ReadKaniPosition` but targeting `ChaseDetector::GetBattleyarouEntityPtr()`. battleyarou (Others slot 6 on doopen2a) is the BATTLE caller per the v0.16.1 `[CBF]` log line (`entityPtr=0x0188CA04 caller=other`). Its method[4] is a 51-instruction movement loop (SET3 at dword 990 with PSHM_W params 7, -744, 0 -- the spawn position -- and a chain of waypoint constants 442/765/500/724/1494/756) and its TALKRAD jumps from 128 to 500 at field load. The question this read answers: is battleyarou actively closing on the party (proximity catch within TALKRAD=500), or is its position roughly static while a session-timer fires BATTLE regardless of geometry?

### (2) Per-tick log adds ` by=(X,Y) bydist=N` in `src/chase_auto_pilot_update.inl`

All four ChaseAutoPilot tick log paths (DIRECTION-with-pos, DIRECTION-without-pos, TARGET-with-pos, TARGET-without-pos) gain the battleyarou suffix alongside the existing kani suffix. Format mirrors the kani fragment exactly: ` by=(X,Y) bydist=N` when both reads succeed, ` by=(X,Y) bydist=?` when only battleyarou resolves, ` by=UNRESOLVED` when battleyarou's slot pointer is null on this field. Slots into the v0.15.9.11.3.7 delta-zero suppression cleanly because the suffix is appended after `kaniBuf` in the same `Log::Field` call -- no new log gate.

### (3) `_ReturnAddress()` capture in `src/chase_battle_freeze.cpp`

`Hook_opcode_battle` reads MSVC's `_ReturnAddress()` intrinsic on its very first line (before any other code so no inlining shuffles the captured value) and appends ` retAddr=0x%08X` to the existing `[CBF] PASS` log line. The captured address is the engine instruction immediately after the call site that invoked opcode_battle (0x69). With this we can map the BATTLE invocation back to the engine function that fired it -- battleyarou's script body, an EXT_DISPATCH handler, or some other dispatch path the v0.16.1 BAT's opcode histogram (which topped out at 0x35 with no 0x66 BATTLE opcode visible) didn't surface. New include: `<intrin.h>`.

### Expected v0.16.1.1 BAT signatures

**Proximity hypothesis confirmed:** on doopen2a, `bydist` starts ~1165 (battleyarou spawn at (0,-744), party at (-974,-105)) and decreases each tick as battleyarou's script moves it south, dropping below 500 right before the `[CBF] PASS` line fires. The `retAddr` lands inside battleyarou's script execution -- whatever engine function actually runs the JSM bytecode.

**Timer hypothesis confirmed:** `bydist` stays large (>500) throughout the engagement; `[CBF] PASS` fires with `bydist=800+`. The `retAddr` lands in a non-script engine function (e.g. a scene-state controller) that fires BATTLE on its own schedule.

Either result narrows the next fix substantially: proximity wants a faster auto-pilot path through doopen2a (MODE_DIRECTION south for instant engagement, no CALIB delay) and possibly a battleyarou-position-aware steering bias; timer wants us to either save time on earlier fields (bridge transit was 14s in the v0.16.1 BAT -- on the high end) or to intercept the timer-arming opcode on chase entry.

## v0.16.1

Pure-refactor split of `src/chase_auto_pilot.cpp` (108 KB, 1402 lines — second-largest non-history source file after `ff8_accessibility_history.h`). No behavioral changes. Removes `chase_auto_pilot.cpp` from the CI source-file-size-check allowlist.

The v0.15.9.x narrative comment header (every chase auto-pilot iteration from v0.15.9 through v0.15.9.11.3.7, walking through MODE_DIRECTION, MODE_TARGET, MODE_STAGED_DIRECTION, MODE_BRIDGE_DANCE, per-field configs, BAT findings, and the v0.15.9.11.3 synthetic-keyboard hookup) is pulled into a new `chase_auto_pilot_history.h` with an `#if 0` wrapper, mirroring the `world_map_history.h` archive pattern.

File layout after the split:

- `chase_auto_pilot.cpp` (slim parent) — system includes, namespace forward decls, namespace block, `.inl` chain in dependency order, public API: `Initialize`, `Shutdown`, `IsEngaged`. The big `Update` function lives in `chase_auto_pilot_update.inl` and is wired in via the textual include.
- `chase_auto_pilot_history.h` — pulled-out v0.15.9.x narrative, NOT in build path.
- `chase_auto_pilot_state.inl` — enums (`FieldDriveMode`, `BridgeDanceState`), structs (`FieldStage`, `FieldConfig`), all `s_*` module-static state, bridge-dance `kBridge*` thresholds, `ENTITY_STRIDE_OTHERS`. First in include chain.
- `chase_auto_pilot_route.inl` — `kStages_domt5_1[]` and `kFieldConfigs[]` with their rationale comments.
- `chase_auto_pilot_io.inl` — `ReadSquallPosition`, `ReadKaniPosition` (both SEH-guarded), `DistSquared`, `IntSqrt`.
- `chase_auto_pilot_helpers.inl` — `IsDirectionLikeMode`, `PickStageIdx`, `LookupConfig`, `BuildFallbackConfig`, `DirectionName`.
- `chase_auto_pilot_diag.inl` — `LogChaseActiveDiagnostic` (currently retired/early-returns; preserved for future camera-orientation research).
- `chase_auto_pilot_bridge.inl` — `UpdateBridgeDance` state machine (domt1_1 EAST/WEST kani-leap dance).
- `chase_auto_pilot_engage.inl` — `Engage`, `Disengage`.
- `chase_auto_pilot_update.inl` — the big per-tick `Update` function with the per-second diagnostic and delta-zero suppression.

Include order in the slim parent: state → route → io → helpers → diag → bridge → engage → update. State first per the v0.16.0 rule; each later file's functions reference only definitions from earlier files.

Largest .inl after the split is `chase_auto_pilot_update.inl` at roughly 22 KB — well clear of the 60 KB soft warning and 80 KB hard fail thresholds enforced by `.github/workflows/safety-checks.yml`. The allowlist entry for `src/chase_auto_pilot.cpp` is removed in the same diff.

BAT plan: load a Dollet save just before/during the X-ATM092 chase. Verify the auto-pilot still engages at the chase-ASK answer, drives the party across `domt4_1 / domt3_2 / domt5_1 / domt1_1 / doopen2a / dotown_2 / dotown_1` per their respective configs, and disengages cleanly at field exits. No log lines or behavior should differ from v0.16.0.3.

## v0.16.0.3

Log-spam cleanup follow-up from v0.16.0.2 BAT. The `[VEH-POS-FALLBACK]` diagnostic added in v0.16.0.1 (when `GetWorldMapPosition_Active` declines to overwrite foot DWORDs with a (0,0) vehicle read) was firing on every world-map poll while `s_lastVehicle` stayed latched to a non-foot value. In Aaron's v0.16.0.2 BAT, `s_lastVehicle=33 (VEH_CAR)` latched mid-session and the fallback log line fired roughly 1800 times in a 7-minute session, dominating `Logs/ff8_world.log` and making post-BAT analysis painful.

**Fix.** `world_map_segments.inl` — the fallback log branch now uses a function-local `s_fbLastLoggedVehicle` static and logs only when the current `s_lastVehicle` differs from the last-logged value. The functional guard (only overwrite foot DWORDs when `vx != 0 || vy != 0`) is unchanged.

Rationale for transition-only (no time-based heartbeat): the guard is silent and self-correcting; the diagnostic exists only as a forensic trail of which vehicle byte values reached the fallback. Once a given vehicle value has been logged once, additional heartbeats add noise without adding forensic value. A future bug that depends on the fallback firing repeatedly without a vehicle change would need its own targeted diagnostic.

No functional change to AD behavior; only diagnostic log frequency reduced.

BAT plan: any session that exercises the world map. Expect at most one `[VEH-POS-FALLBACK]` line per distinct `s_lastVehicle` value that triggers the fallback (typically 0–3 total per session), instead of the per-poll flood.

## v0.16.0.2

Three-part fix from the v0.16.0.1 BAT, which revealed that Fire Cavern is a two-stage entry: the world-map trigger drops the player into the "Fire Cavern A" approach field (a path field leading to the cavern interior), not directly into the cavern. The trigger geometry for this approach field sits ~6.5k units southwest of the icon at (36864,-28672), well outside the 2500-unit Part B cap. Aaron correctly observed in the BAT that landing in Fire Cavern A is a success; the mod was wrongly treating it as off-target.

### Fix 1 — Poll() replan-path now honors planner-eligibility

**Symptom.** Fire Cavern drive at 14:37:46 in the v0.16.0.1 BAT log started correctly with `planned=0` (simple-coord steering, per Part C). A random encounter at 14:37:51 paused it. On world-map re-entry at 14:39:00, the log shows `[DRIVE] Resumed after world-map re-entry`, and immediately after, the next `Awaiting arrival decision` line reports `planned=1`. The Poll()'s replan path had called `PlanDrivePath(rx, ry)` unconditionally, the closest-active-region fallback fired (Fire Cavern's segment (20,20) region=0x0C has no foot clause, so the fallback walked active regions and picked seg(18,20) which belongs to Balamb Town's region 0x07), and the simple-coord drive was converted into a misrouted planner drive.

**Root cause.** Part C correctly gates `StartAutoDrive` on `s_destPlannerEligible[locIdx]`, but `Poll()`'s mid-drive replan code at re-entry was added in v0.14.88 (well before Part C existed) and calls `PlanDrivePath` without the same gate.

**Fix.** `world_map_state.inl` adds `static bool s_drivePlannerEligible = true;`. `world_map_drive.inl`'s `StartAutoDrive` sets it from the same locIdx-based decision Part C uses, and `StopAutoDrive` resets it to `true`. `world_map.cpp`'s Poll() replan block now wraps `PlanDrivePath(rx, ry)` with `if (s_drivePlannerEligible) { ... } else { log + keep simple-coord }`. Planner-ineligible drives now stay simple-coord through encounter-resume cycles.

### Fix 2 — Part B two-tier distance cap

**Symptom.** Same BAT, 14:39:11: the misrouted-then-corrected drive exits the world map at lastPos=(30326,-29221), MODE_FIELD fires, Part B refuses arrival because `dist=6561 > 2500 max`. But that position is exactly where the Fire Cavern A approach-field trigger sits — the off-target stop was actually a successful arrival.

**Root cause.** Part B's 2500-unit cap assumes the destination's catalog point is trigger-aligned (true for refined coords captured from prior BAT, true for planner-eligible destinations whose icons sit at script-event positions). Geometric-trigger destinations (Fire Cavern, early-game Balamb Garden, likely Centra Ruins / Tomb / Cactuar Island / Shumi / Edea's House) have icons placed for visual centering, with terrain triggers thousands of units away. The 2500 cap is correct for them once a refined coord is captured but wrong on first arrival.

**Fix.** `world_map_arrival.inl` adds `DRIVE_ARRIVAL_MAX_DIST_GEOMETRIC = 8000.0`. The MODE_FIELD branch and the timeout-fallback distance branch both choose between the two caps via `s_drivePlannerEligible ? 2500 : 8000`. OFF-TARGET log lines now include the tier label (`planner-eligible` or `geometric-trigger`) for diagnostic clarity. The refined-coord capture in the success branch already exists; it now runs for geometric-trigger arrivals in the 2500–8000 zone, capturing the actual trigger position. Subsequent drives target the refined coord and dist drops to near zero, falling back inside the strict 2500 cap.

This is self-correcting and data-driven: every new geometric-trigger destination Aaron visits will be refined on first arrival, with no per-destination hardcode required. The wider cap is a safety net only for unrefined destinations, not a permanent relaxation.

### Fix 3 — Hardcoded Fire Cavern refined-coord baseline

In `world_map.cpp`'s `Initialize()`, the `s_refinedHas[i]` default-population loop now sets Fire Cavern's refined position to `(30326, -29221)` alongside the existing Balamb Town hardcode at `(12896, -26711)`. This eliminates the first-drive 4-second round-trip through the wider-cap arrival path for Fire Cavern specifically. On a fresh install or after savedata reset, the first Fire Cavern drive will compute dist near zero at arrival and use the strict cap immediately.

The loop's `break;` after Balamb Town was removed so both names are checked on a single pass; the else-if chain ensures only one match per location.

### BAT plan for v0.16.0.2

1. Build v0.16.0.2, restart FF8.
2. Stand on world map on foot. Select Fire Cavern, press `\`. Expect `[INIT] Refined entry default: Fire Cavern (30326,-29221)` already logged at module init.
3. Expected drive log: `[DRIVE] Geometric-trigger destination Fire Cavern (locIdx=37, planner-ineligible) -- using simple-coord steering`. **NO `[PLAN-DEBUG]` walk.**
4. After arrival in Fire Cavern A, expect `[DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x????, fieldName='?????', target=Fire Cavern, dist=<low>, ...)` — `dist` should be small because the refined coord is now the target.
5. If a random encounter interrupts mid-drive: on resume, expect `[DRIVE] Planner-ineligible destination -- keeping simple-coord steering, not replanning`. The previous bug would have shown `[PLAN-DEBUG]` here.
6. Select Balamb Town, press `\`. Expect normal `[PLAN-DEBUG]` walk and planner arrival (unchanged behavior).
7. Pull `Logs/ff8_world.log` + `Logs/ff8_mod.log` and the field's fieldName/fieldId from the arrival line so the DEVNOTES catalog of geometric-trigger destinations can grow.

## v0.16.0.1

Two follow-up fixes from the v0.16.0 BAT. Both surfaced in `Logs/ff8_world.log` from Aaron's first run; both have known repros and small surgical patches.

### Fix 1 — "Position unavailable" after exiting a field (the bug Aaron hit)

**Symptom.** After exiting a location back to the world map, pressing `\` to start auto-drive spoke "Position unavailable. Try again." After a random encounter the announcement disappeared and AD worked normally.

**Root cause.** In the BAT log at 14:09:55:
```
[WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=37 (was 0, suppressed 3000ms of byte noise)
```
The 3-second WM-ENTRY-DEBOUNCE committed `s_lastVehicle = 37` (mode 0x25, in the 32-40 `VEH_CAR` range) for a player who never owned a car. `GetWorldMapPosition_Active` saw `VEH_CAR`, dispatched to `WM_CAR_POS_ADDR`, read the savemap `car_pos` struct which holds `(0,0)` (vehicle never owned, never maintained), and **unconditionally overwrote the perfectly valid foot DWORD position** with `(0,0)`. `StartAutoDrive` then aborted via the `if (px == 0 && py == 0)` guard with "Position unavailable. Try again." The random encounter cycle eventually settled `s_lastVehicle` to mode 0 (foot), and AD started working.

**Fix.** `world_map_segments.inl` — inside the `__try` block in `GetWorldMapPosition_Active`, guard the vehicle-pos overwrite with `if (vx != 0 || vy != 0)`. `(0,0)` from a vehicle savemap struct is a sentinel meaning "vehicle not owned / not maintained," and the foot DWORDs (already populated by the initial `GetWorldMapPosition` call) are the more reliable fallback. The `else` branch logs `[VEH-POS-FALLBACK]` with the tag, `s_lastVehicle`, vehicle-type name, and the retained foot coords so any recurrence is visible in `ff8_world.log` without needing a fresh diagnostic build.

### Fix 2 — Part C indexed the wrong eligibility array (uncovered while diagnosing Fix 1)

The same BAT log showed the Fire Cavern drive at 14:07:15 walking all 38 planner programs and producing the closest-active-region fallback toward seg(18,20), exactly the case Part C was meant to short-circuit. Part B caught the off-target arrival at 14:07:23, but the planner walk shouldn't have fired at all.

**Root cause.** `world_map_drive.inl`'s Part C gate read `s_destPlannerEligible[catIdx]` where `catIdx` is into `s_catalog[]` (the BFS-filtered, distance-sorted, vehicle-aware catalog — 4 entries during the failing drive), but `s_destPlannerEligible[]` is indexed by `s_locations[]` (the 38-entry master table populated by `ComputePlannerEligibility`). For Fire Cavern at catIdx=2, the gate read `s_destPlannerEligible[2]` = **Dollet's** eligibility (master idx 2 = YES) and ran the planner anyway.

**Fix.** `world_map_drive.inl` — `StartAutoDrive` already calls `FindLocationIndexByTargetCoords(dest.x, dest.y)` to look up `locIdx` (master-table position) for the refined-coord check a few lines earlier. Reuse that variable: `s_destPlannerEligible[locIdx]` is the right index. The fallback log now reports `locIdx` for direct correlation with the `[INIT] Planner-eligibility:` lines.

### Verification path for the next BAT

1. **"Position unavailable" gone.** Exit any field on foot, immediately press `\` on the world map. Should announce the destination and start driving. `[VEH-POS-FALLBACK]` lines in `ff8_world.log` confirm the new guard catching the stale-vehicle case; their absence means the locomotion byte stayed clean this run.
2. **Fire Cavern uses simple-coord steering.** Stand on the world map on foot, select Fire Cavern, press `\`. Expect a new log line: `[DRIVE] Geometric-trigger destination Fire Cavern (locIdx=37, planner-ineligible) -- using simple-coord steering`. **No** `[PLAN-DEBUG]` walk follows. UpdateAutoDrive steers by bearing toward the catalog center until either arrival (capped by Part B at 2500 units) or sweep-abort.
3. **Balamb Town still uses the planner.** Select Balamb Town, press `\`. Expect the existing `[PLAN-DEBUG]` walk to run and produce a real path. Part B and the new locIdx gate together should keep planner-eligible destinations working exactly as in v0.16.0.

Fire Cavern refined-coord capture is on the BAT punch list for v0.16.0.1: stand on the world map on foot, drive into Fire Cavern via simple-coord steering, the on-arrival log line `[DRIVE] Captured refined entry for Fire Cavern at (X,Y)` is what we want.

## v0.16.0

Refactor + safety net for the world-map auto-drive system. The 222 KB / 4452-line `src/world_map.cpp` monolith has been split into 10 focused files, two new behavioral safety nets were added (Part B and Part C), and a CI guard was added to keep source-file size bounded going forward. No new features for the user beyond the AD safety improvements; the bulk of the diff is structural.

### What v0.15.13.2 BAT exposed (the bug behind Parts B / C)

A Fire Cavern auto-drive routed the player into Balamb Garden's gate field (`bggate_1`) instead. The v0.14.95 closest-active-region fallback in `MatchProgramForCatalog` was the culprit: Fire Cavern's catalog at (36864, -28672) maps to segment region 0x0C, and the only program that names 0x0C is program 20 with `top_vehicle=Garden`. On foot with no Garden owned, that clause filters out, the catalog's own region falls out of the active set, and the closest-active-region search picked an unrelated active region — routing the player toward Balamb Garden's gate. Worse, the v0.14.96 deferred-arrival path then captured the misrouted entry coord into `s_refinedX/Y[bggate_1]`, poisoning subsequent drives to Balamb Garden until a fresh session cleared the in-memory table.

Root diagnosis: some world-map destinations are **planner destinations** (entered via a wmsetus.obj Section 8 trigger zone, well represented by the A* planner) and some are **geometric-trigger destinations** (entered via a terrain-29 polygon trigger on the world map mesh, no wmsetus script event at all). Fire Cavern is the canonical geometric-trigger destination. The A* planner cannot represent these — there's no foot clause to match — so its closest-active-region fallback misroutes drives toward unrelated destinations. Pre-Sonnet builds solved this with v0.11.11-era simple-coord steering (catalog-center, bearing-based) which is bounded and predictable.

### Part B — off-target distance cap on arrival

`world_map_arrival.inl` adds `DRIVE_ARRIVAL_MAX_DIST = 2500.0` and applies it at two anchor points in `ResolveDeferredArrival`:

1. Top of the `MODE_FIELD` branch: when the game settles into a field but the player's last-known world-map position is more than 2500 units from `s_driveTarget`, refuse to capture a refined coord, refuse to declare arrival, log `[DRIVE] OFF-TARGET stop (dist=X.X > 2500.0 max ...)`, and stop AD with a spoken "Entered field but X units from target; not arrival." The Fire-Cavern-into-bggate_1 case fails this check on every retry — it would have stopped cleanly instead of poisoning the refined table.
2. Inside the timeout-fallback exit-distance branch: same check, defensive. `DRIVE_ARRIVED_ON_EXIT_DIST` (1500) is already below `DRIVE_ARRIVAL_MAX_DIST` (2500), so the check is structurally redundant today, but the explicit guard preserves the contract if a future build raises `DRIVE_ARRIVED_ON_EXIT_DIST`.

### Part C — planner-eligibility gate in `StartAutoDrive`

`world_map_drive.inl`'s `StartAutoDrive` no longer calls `PlanDrivePath` unconditionally. It now checks `s_destPlannerEligible[catIdx]` first:

- **Eligible destination**: call `PlanDrivePath(px, py)` exactly as before. A* runs, planner takes over.
- **Ineligible destination**: skip the planner entirely. Log `[DRIVE] Geometric-trigger destination (planner-ineligible), using simple-coord steering`. Clear `s_drivePathLen / Idx / Planned / GoalSegCount`. `UpdateAutoDrive`'s non-planner branch (catalog-center steering with bearing-based final approach) handles the rest.

### `ComputePlannerEligibility` — the helper that decides which is which

`world_map_planner.inl` gains `ComputePlannerEligibility()`, called once near the end of `Initialize` (after `LoadTriggerZones` so `s_segmentRegionMap` is populated, after the catalog is registered so `s_locations[]` is valid). It walks every catalog entry, maps `(x, y)` to a segment region byte, and scans `s_triggerPrograms[]` looking for at least one clause that names that region with a foot vehicle code (`TRIG_VEH_FOOT = 0x80` or `TRIG_VEH_FOOT_ALT = 0x84`). Result lands in `s_destPlannerEligible[LOCATION_COUNT]`. Logs one `[INIT] Planner-eligibility:` line per catalog entry plus a count summary. Defaults all flags to false if `s_segmentRegionLoaded` is false — safer than over-marking.

Predicted classifications (verify in v0.16.0 BAT init log):

- Balamb Town (region 0x07): **YES** (program 9 clause 1: foot, 0x07, story [0..3900)).
- Balamb Garden (region 0x0C): **NO** (only program 20 names 0x0C; top_vehicle=Garden).
- Fire Cavern (region 0x0C): **NO** (same as Balamb Garden — both at seg(20-ish, 19-ish), both region 0x0C, no foot clause).
- Most named-town destinations should be YES.
- Most chocobo forests should be YES.
- Alien Ship sites are likely NO (they're terrain triggers).

### File split — what moved where

| File | Size | Contains |
|---|---|---|
| `src/world_map_history.h` | 17.76 KB | Narrative archive of v0.14.31 through v0.15.13.2. Pulled out of the build. v0.14.102 narrative preserved verbatim; older blocks condensed with a `git show v0.15.13.2:src/world_map.cpp \| head -609` pointer. |
| `src/world_map_state.inl` | 29.78 KB | Enums (`VehicleType`, `SegTerrainClass`), structs (`LocationEntry`, `TriggerClause`, `TriggerProgram`), all `static` state arrays sized to `MAX_LOCATIONS = 64`, including the new `s_destPlannerEligible[MAX_LOCATIONS]`. Constants for the world torus, wmx.obj, wmsetus.obj, AD lifecycle. |
| `src/world_map_segments.inl` | 34.35 KB | Coord readers, pure math, vehicle classifier, archive I/O, `LoadTerrainGrid`, `DumpTriggerSection`, `LoadTriggerZones`. |
| `src/world_map_trigger_data.inl` | 17.72 KB | 38 decoded wmsetus.obj Section 8 trigger programs, `s_triggerPrograms[]`, `TRIGGER_PROGRAM_COUNT`, `LogTriggerPrograms`. |
| `src/world_map_catalog.inl` | 13.57 KB | `s_locations[]` data with `LOCATION_COUNT`, `static_assert(LOCATION_COUNT <= MAX_LOCATIONS)`, BFS reachability, distance-sorted catalog builder, vehicle-state tracker. |
| `src/world_map_announce.inl` | 2.81 KB | `AnnounceLocation`, `AnnounceBearing`. |
| `src/world_map_planner.inl` | ~30 KB | `IsLocationFootFriendly`, story/vehicle predicates, `MatchProgramForCatalog`, `CollectGoalSegments`, `IsGoalSegment`, `WrapManhattan`, `HeuristicToGoals`, `PlanPath`, `PlanDrivePath`, **new** `ComputePlannerEligibility`. |
| `src/world_map_arrival.inl` | ~11 KB | `ResolveDeferredArrival` with `DRIVE_ARRIVAL_MAX_DIST` and the two Part B distance checks. |
| `src/world_map_drive.inl` | ~28 KB | `PressKey`, `ReleaseKey`, `ReleaseAllDriveKeys`, `SetDriveKeys`, `StopAutoDrive`, `StartAutoDrive` (with Part C eligibility gate), `StartSweep`, `UpdateAutoDrive`. |
| `src/world_map_keys.inl` | 2.59 KB | `PollKeys` (catalog cycle, bearing, AD toggle). |
| `src/world_map.cpp` | ~10 KB | Slim parent. Headers, namespace forward decls, the 9-deep `.inl` include chain inside `namespace WorldMap { ... }`, plus `Initialize` (with the new `ComputePlannerEligibility()` call), `Update`, `Shutdown`, `Poll`. |

`.inl` includes are textual — no header guards inside, all `static` declarations preserved, `state.inl` is always included first so types/state are visible to every later file. The `LocationEntry` struct moved into `state.inl` so state arrays can reference it; `s_locations[]` data and `LOCATION_COUNT` stay in `catalog.inl`. `MAX_LOCATIONS = 64` in `state.inl` decouples state-array sizing from the catalog size; a `static_assert` in `catalog.inl` keeps them honest.

### CI guard — source-file-size check

`.github/workflows/safety-checks.yml` gains a `source-file-size-check` job that scans `src/*.cpp` and `src/*.inl` at push time. Soft warning at 60 KB, hard fail at 80 KB. The check is needed because the 222 KB world_map.cpp got that way precisely because there was no enforcement — every "just add one more changelog block" was locally cheap and globally ruinous.

Existing oversized files are temporarily allowlisted so this build can push without already requiring follow-up refactors: `chase_auto_pilot.cpp` (108 KB), `field_archive_jsm.inl` (91 KB), `battle_tts_ewm.inl` (90 KB), `field_dialog.cpp` (88 KB), `battle_tts_menu.inl` (82 KB). Each of these is queued for its own v0.16.x split — the world_map split is the template.

### BAT plan

1. Build, restart FF8 to clear in-memory poisoned Fire Cavern refined-coord.
2. Check `Logs/ff8_world.log` for `[INIT] Planner-eligibility:` block. Expect Fire Cavern=NO, Balamb Garden=NO, Balamb Town=YES, plus a count summary.
3. **Fire Cavern AD test**: select Fire Cavern, press `\`. Log should show `[DRIVE] Geometric-trigger destination (planner-ineligible), using simple-coord steering`. Character moves east toward Fire Cavern. Arrives. Capture refined coord from `[DRIVE] Captured refined entry for Fire Cavern at (X,Y)` for v0.16.0.1 hardcoding.
4. **Balamb Town AD regression**: planner-eligible path executes normally, drive arrives.
5. **Balamb Garden AD**: geometric-trigger steering instead of accidental terrain crossing.
6. **Off-target stop test (Part B safety net)**: if a drive ever enters the wrong field >2500 units from target, look for `[DRIVE] OFF-TARGET stop`. Should fire on the wrong-field scenarios that v0.15.13.2 silently passed.
7. World-map keyboard nav regression: `+`, `-`, Backspace, `\` all still work.

Upload `Logs/ff8_world.log` + `Logs/ff8_mod.log`.

### Files

- DELETED (effectively, by overwrite): old monolithic `src/world_map.cpp` (222 KB) — content migrated to the .inl files.
- NEW: `src/world_map_history.h`, `src/world_map_state.inl`, `src/world_map_segments.inl`, `src/world_map_trigger_data.inl`, `src/world_map_catalog.inl`, `src/world_map_announce.inl`, `src/world_map_planner.inl`, `src/world_map_arrival.inl`, `src/world_map_drive.inl`, `src/world_map_keys.inl`.
- REPLACED: `src/world_map.cpp` (slim parent, ~10 KB).
- MODIFIED: `src/ff8_accessibility.h` (version 0.16.0).
- MODIFIED: `.github/workflows/safety-checks.yml` (source-file-size CI guard, allowlist for already-oversized files pending later splits).
- MODIFIED: `CHANGELOG.md` (this entry).
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.

### Deferred

- v0.16.1: split `src/chase_auto_pilot.cpp` (108 KB) using the same `.inl` pattern.
- v0.16.2: split `src/field_dialog.cpp` (88 KB).
- v0.16.3: split `src/field_archive_jsm.inl` (91 KB).
- v0.16.4: split `src/battle_tts_ewm.inl` (90 KB).
- v0.16.5: split `src/battle_tts_menu.inl` (82 KB).
- v0.16.0.1 hardcoded refined-coord baseline for Fire Cavern and Balamb Garden, captured from the v0.16.0 BAT logs (Part D from the planning conversation — was always intended to follow the BAT result, not ship blind).

## v0.15.13.2

Live timer reads now point at the address the v0.15.13.1 scanner discovered. The scanner has served its purpose and is disabled in this build, freeing ~6 MB of static memory and the per-frame snapshot/analyze CPU cost.

### v0.15.13.1 BAT — scanner found the timer

Cycle 11 of the v0.15.13.1 BAT (21:50:40) surfaced a single, unmistakable candidate in the R1 u32 list:

```
[CountdownScan] R1 u32 #0 addr=0x01CFE92C u32 cur=1711 old=1715 dec=4 rate=1.00/s
```

Perfect 1.00/second monotonic decrement, value 1711 = 28 minutes 31 seconds remaining — squarely consistent with a Dollet chase save loaded mid-run (chase starts at 1800 sec; 89 seconds elapsed by cycle 11). The address `0x01CFE92C` is `0x8C` bytes BELOW the game-object struct base `0x01CFE9B8`, in an adjacent engine-globals allocation. That's why v0.15.13.0's old Region 1 (8 KB starting AT the game object) missed it — the v0.15.13.1 expansion to `0x01CD0000 + 192 KB` was what surfaced it.

The candidate only appeared in cycle 11 because the top-16 cap pushed it out of most other cycles where 16+ faster-changing candidates ranked higher (entity-state churn at rate ~25/s during gameplay dominated the rankings). Cycle 11 was unusually calm — only 1 R1 u32 entry made it through the value-range and rate filters — letting our slow timer (dec=4, rate=1.00/s) take that lone slot.

This is the kind of find that justifies wider scan regions and accepting more noise in the ranked output: a low-rate, single-instance, perfectly-monotonic candidate in an otherwise quiet region is exactly the signature of a real countdown timer.

### Changes in `src/countdown_timer.cpp`

- New constant `LIVE_TIMER_ADDR = 0x01CFE92C` (the discovered address). Read as uint16 — value fits comfortably in 16 bits since the max representable timer is 65535 seconds = ~18 hours, well above any chase duration, and reading uint16 means Shift+T freeze writes won't clobber any unknown high-byte engine state.
- Old `TIMER_VAR724_ADDR = 0x01CFEC8C` renamed to `VAR724_SNAPSHOT_ADDR`, kept as a documented constant but no longer read. The script-side snapshot stays at 0 during the chase because the chase script doesn't call GETTIMER routinely; only `LIVE_TIMER_ADDR` updates.
- `ReadVar724Raw` / `WriteVar724Raw` renamed to `ReadLiveTimerRaw` / `WriteLiveTimerRaw`. All call sites updated.
- Log tag updated from `var724 raw=N` to `live raw=N` to make the new source obvious in the log.
- Initial announcement reworded "Timer started" → "Timer detected" since the player may be loading mid-chase rather than at the SETTIMER moment.
- Comment block rewritten to capture the v0.15.13.0/.1/.2 history and the rationale for picking uint16 over uint32.

### Changes in `src/countdown_scan.inl`

Scanner gated behind `#define COUNTDOWN_SCAN_ENABLED 0` at the top of the file. When disabled:

- The large static buffers (`s_region1Buf`, `s_region2Buf` — ~6 MB total) are not declared.
- `Initialize` becomes a one-line log saying "DISABLED (v0.15.13.2). Set `COUNTDOWN_SCAN_ENABLED=1` to re-enable."
- `Update` is an empty no-op.
- Full scanner implementation preserved inside the `#if` block so a future session can flip the flag to re-hunt for a different engine global without rewriting from scratch.

This is a deliberate pattern: when a diagnostic feature has served its purpose, gate the heavy work behind a flag rather than deleting the code. The file keeps documenting how scanning was done, and the next time we need to find an engine global, the only change is the flag and (optionally) the region addresses.

### What the next BAT verifies

Aaron loads the Dollet comm-tower save. The mod log should now show:

- `[CountdownTimer] Initialize v0.15.13.2: reading live engine timer at 0x01CFE92C ...`
- `[CountdownScan] DISABLED (v0.15.13.2). ...` (and nothing else from the scanner).
- Shortly after fieldload: `[CountdownTimer] live raw=NNNN (prev=-1) state=0 tickMs=...` (the first observation).
- Then `[CountdownTimer] ENTER ACTIVE: rawValue=NNNN units=SECONDS initialSec=NNNN (NNmNNs) ...`
- TTS announcement: "Timer detected. NN minutes NN seconds remaining."
- As the chase progresses: `[CountdownTimer] BOUNDARY 1500 seconds reached ...` etc. at 25:00, 20:00, 15:00, 10:00, 5:00, 1:00, 0:30.
- Pressing T at any point: "NN minutes NN seconds remaining."
- Pressing Shift+T: "Timer frozen." Then on-screen timer stops advancing (or flickers between current and frozen value at HUD refresh rate). Pressing Shift+T again: "Timer resumed."

Static memory should drop by ~6 MB (verifiable indirectly via taskmgr if Aaron cares to check). No `[CountdownScan]` lines beyond the disabled announcement.

### Failure modes to watch for

- **No `[CountdownTimer] live raw=NNNN` after fieldload**: read may have faulted on 0x01CFE92C. SEH should catch this gracefully; log would be empty rather than crashing. Could mean the address isn't always mapped before fieldload finishes initializing the engine state. Mitigation: read attempts run every frame, so it'd start working once the page maps. If it never maps, the scanner finding was a false positive (unlikely given the exact 1.00/s signature).
- **`live raw=0` throughout**: the address holds zero. Could mean the timer hasn't started yet for this save, or 0x01CFE92C is actually a per-save-slot offset rather than a global. Aaron would confirm by watching the on-screen HUD.
- **Units misclassified**: if the live address holds a value outside our three ranges (5-60, 500-3000, 15000-60000), the classifier returns UNKNOWN and the state machine stays INACTIVE. The "Observed nonzero value N but units UNKNOWN" log line will tell us which range to add. (Aaron's BAT had value 1711 which is in SECONDS range — should be fine.)
- **Shift+T freeze doesn't visually freeze the timer**: the engine writes to 0x01CFE92C more aggressively than our mod thread can rewrite. If this happens, we have the read working but freeze remains unreliable; that's an acceptable trade-off — read-and-announce is the primary feature. Could be addressed in v0.15.14 by hooking the engine's write instead of polling.

### Files

- MODIFIED: `src/countdown_timer.cpp` (live timer address, renames, log tags, comments)
- MODIFIED: `src/countdown_scan.inl` (compile flag gating heavy work; full implementation preserved)
- MODIFIED: `src/ff8_accessibility.h` (version 0.15.13.2)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Deferred to later builds

- `menu_tts.cpp` T-handler `!shift` gate. Theoretical conflict only.
- Deep-research doc comment fix (still says `0x01CFECCC`).
- `field_display_names.h` audit (fieldIds 0x0134 / 0x0136 mislabeled).
- v0.15.14.0 candidate work: hook the engine write to 0x01CFE92C for more reliable freeze; or add value-range "spotlight" pass to the scanner so future address hunts surface slow timers even amid faster-changing neighbors.

## v0.15.13.1

Region expansion for the in-mod timer scanner, after the v0.15.13.0 BAT showed the scanner working mechanically but failing to surface a candidate matching the visibly-active Dollet 30-minute countdown.

### v0.15.13.0 BAT findings (why this build exists)

Aaron loaded a save in the Dollet comm tower (post-Elvoret, timer actively counting down) and captured an F11 screenshot at 21:24:47 showing `28:19` on the timer HUD. The mod log confirms the scanner ran correctly: `Initialize: armed`, `First snapshot done at slot 0: Region 1 2/2 pages mapped, Region 2 256/256 pages mapped`, 11 analysis cycles. No SEH faults; both regions fully mapped.

But none of the candidates surfaced over those 11 cycles match any plausible encoding of "28:19 remaining":

- SECONDS encoding expected cur ≈ 1699 — no candidate near that value
- MINUTES encoding expected cur ≈ 28 or 29 — no candidate in that range
- FRAMES@30Hz encoding expected cur ≈ 50970 — no candidate near that
- MS encoding expected cur ≈ 1,699,000 — filtered out by v0.15.13.0's `MAX_PLAUSIBLE_VAL = 200,000`

The actual candidates were either (a) very-fast-changing animation counters during field load (cycle 7's 16 entries at `0x01DC67xx-0x01DC68xx` with rate ~115/s and cur values 60-75 — entity state during the comm-tower-interior load), (b) menu-state byte-boundary artifacts (uint16 reads spanning a byte where the low byte changed, looking like dec=256 in uint16), or (c) the recurring `0x01D2B106 dec=32 rate=8/s` counter (constant pattern, probably an audio/input system tick).

Diagnosis: the chase timer global lives in a region the v0.15.13.0 scanner did not cover. The address-resolution log lists many engine globals — `pCurrentFieldId = 0x01CD2FC0`, `pCurrentFieldName = 0x01CD2DB0`, `pMode0Phase = 0x01CE4760`, `pMode0InitFlag = 0x01CE0758`, `pMasterSfxVolume = 0x01CD1794`, `_mode = 0x01CD8FC6`, `pKeyboardState = 0x01CD02D8`, `pEngineInputValidButtons = 0x01CD01F8` — all in the `0x01CD0000-0x01D00000` range, which is exactly the 192 KB gap between v0.15.13.0's Region 1 (8 KB at the game-object struct base `0x01CFE9B8`) and Region 2 (`0x01D00000-0x01E00000`). The chase timer is almost certainly in that neighborhood.

### Changes

**Region 1 expanded**: from 8 KB at `0x01CFE9B8` to **192 KB at `0x01CD0000`** (covers the broader engine-globals zone). The game-object struct at `0x01CFE9B8` is now at offset `0x2E9B8` inside this expanded region. Page count grows from 2 to 48. Static buffer grows from 40 KB to 960 KB.

**`MAX_PLAUSIBLE_VAL` raised** from 200,000 to **2,000,000**. Admits ms-encoded 30-minute timers (1,800,000 ms at chase start).

**`MAX_RATE_PER_SEC` raised** from 200 to **2000**. Admits ms-encoded decrements at ~1000/sec.

**Bug fix: "Ring is now full" log spam.** v0.15.13.0 fired this line every snapshot tick (every second) after `s_snapshotsTaken` saturated at `SNAPSHOT_COUNT`. v0.15.13.1 adds `s_ringFullLogged` boolean so the line fires exactly once on the transition from 4→5 snapshots.

### Memory cost

Static buffers grow from 5.04 MB to ~5.96 MB total. Per-snapshot CPU cost grows proportionally with region 1 size (now scans 48 pages instead of 2, but region 2's 256 pages dominate anyway). Per-analyze CPU cost grows: 192 KB has ~98k uint16 + ~49k uint32 candidates, plus region 2's ~786k. Inner loop is still tight; should land in the 50-100 ms range at 5-second cadence.

### What the next BAT will tell us

Aaron loads the same comm-tower save and plays for ~10-15 seconds (enough for the ring to fill plus one analyze cycle). The `[CountdownScan]` log shows the candidate dumps. Expected outcomes:

- **Clean find in R1**: the timer global is in the newly-scanned engine-globals area. Look for a candidate whose `cur` value matches the on-screen timer value at the screenshot moment (1699 for seconds, 50970 for frames@30Hz, 28 or 29 for minutes, ~1.7M for ms). Then v0.15.13.2 hardcodes that address.
- **Multiple plausible candidates**: several addresses look timer-shaped. v0.15.13.2 adds a value-range "spotlight" pass that filters per encoding so the right one is easier to pick out.
- **Still no candidate**: the timer is either outside both regions or encoded in a way our filters miss. v0.15.14.0 pivots to Path B — hook the DISPTIMER opcode (`0x09D`) at JSM dispatch and read whatever memory address the engine reads to render the HUD value.

### Files

- MODIFIED: `src/countdown_scan.inl` (region 1 expansion, bound widening, log-spam fix)
- MODIFIED: `src/ff8_accessibility.h` (version bump to 0.15.13.1)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

No other source changes. `src/countdown_timer.cpp` is unchanged from v0.15.13.0.

## v0.15.13.0

In-mod memory scanner for the live FF8 mission timer global. After Aaron clarified that his v0.15.12.0 BAT save was post-Elvoret in the Dollet comm tower — i.e. the 30-minute escape countdown was actively running and visibly decrementing on screen — we know the script-side snapshot at `0x01CFEC8C` does not mirror the live timer. The `[CountdownTimer]` log lines from that BAT showed `var724 raw=0` throughout, conclusively. v0.15.13.0 adds a scanner that hunts for the address that actually does decrement, so v0.15.13.1 can repoint reads to it.

### New module: `src/countdown_scan.inl`

A textually-included `.inl` (no `deploy.bat` change needed; the .inl reaches the build through the existing `countdown_timer.cpp` compilation unit) that:

- Snapshots two regions of process memory into its own buffers every 1 second:
  - **Region 1** at `0x01CFE9B8 + 8 KB`. Covers the game-object struct in case the field-var stack lives at some offset inside it rather than at offset 0. Cheap (2 pages).
  - **Region 2** at `0x01D00000 .. 0x01E00000`. The 1 MB engine-globals zone the deep-research doc identified as the most plausible home of the live engine countdown global. 256 pages.
- Maintains a 5-snapshot ring → 4-second time window of history.
- Each `SEH-wrapped` per-page read; pages that fault on first read are marked invalid and skipped from then on.
- Analyses every aligned `uint16` and `uint32` inside the regions whose values across the 5 snapshots are: all nonzero, all not `0xFFFF` / `0xFFFFFFFF` (FF8 unset sentinels), monotonically non-increasing, with total decrement > 0, with current value < 200000 (filters out pointer-like values), and with per-second rate in [0.10, 200.0] (admits seconds-level, frames@30Hz-level, and even minutes-level timers that happen to tick during the window, while rejecting random data noise and large counters).
- Logs the top-16 candidates per region per width every 5 seconds to `ff8_mod.log` under tag `[CountdownScan]`, sorted by total decrement. Each line includes address, current value, oldest value, total decrement, and per-second rate. From these Aaron can identify the Dollet timer by matching expected values (~1800 if SECONDS, ~30 if MINUTES, ~54000 if FRAMES_30HZ) at the start of his BAT session and seeing them drop steadily.
- Always-on for v0.15.13.0 — no field-id gating. Aaron loads any save with an active timer, plays for ~5 seconds to fill the ring, then for ~5 more seconds to see the first analyse cycle. v0.15.13.1+ may add gating once the address is known.

Memory cost: ~5 MB static (snapshot ring) + ~40 KB (region 1 ring). Per-frame cost: dominated by `Scan::Update` which mostly short-circuits on the snapshot/log-interval checks; per-snapshot is ~256 SEH-wrapped 4 KB memcpys (~1-2 ms total); per-analyse is the inner loop over ~786k candidate addresses (~50-100 ms). The analyse cost lands in a 5-second cadence so the per-second amortised cost is small.

### Wired into `src/countdown_timer.cpp`

Three changes there:

1. `#include <cstring>` added at the top (the `.inl` uses `memcpy` and `memset`).
2. Forward declaration of the `Scan` sub-namespace's `Initialize` and `Update(DWORD)` so the calls from `CountdownTimer::Initialize` and `CountdownTimer::Update` resolve before the `.inl` definition.
3. `Scan::Initialize()` added at end of `CountdownTimer::Initialize()`; `Scan::Update(GetTickCount())` added unconditionally at end of `CountdownTimer::Update()` (runs regardless of whether the var724 read faulted, since the scanner has its own per-page fault handling). The `#include "countdown_scan.inl"` sits at the bottom of the `CountdownTimer` namespace block so the definitions land in `CountdownTimer::Scan::*`.

Existing var724 logic is unchanged in behavior — still reads `0x01CFEC8C`, still runs the state machine, still polls T and Shift+T. The scanner is purely additive diagnostic for this build.

### Cosmetic cleanup also in this commit

The deep-research doc and the original `countdown_timer.cpp` comments both said "0x01CFE9B8 + 724 = 0x01CFECCC" — that's wrong math. 724 decimal = 0x2D4 hex, and 0xE9B8 + 0x2D4 = `0x01CFEC8C`. The C++ code computed the correct value at compile time (via `FIELD_VAR_STACK_BASE + 724`), so the binary was right; only the comments were misleading. Corrected throughout `src/countdown_timer.cpp` (header block + the BAT-result comment that explains what we learned in v0.15.12.0). The deep-research doc still has the original wrong math; that's a documentation cleanup task tracked in backlog.

### Expected BAT outcome

Aaron loads a save with the Dollet timer active (the same save shape he used for the v0.15.12.0 BAT). The mod log shows the existing `[CountdownTimer]` lines (`Initialize`, `var724 raw=0` once, no further changes because the snapshot doesn't update — same as v0.15.12.0). New: `[CountdownScan] Initialize: armed.` near startup; `[CountdownScan] First snapshot done at slot 0: Region 1 N/2 pages mapped, Region 2 N/256 pages mapped.` after ~1 second; `[CountdownScan] Ring is now full (5 snapshots). Analysis will begin on the next scheduled log tick.` after ~5 seconds; then every 5 seconds a `=== Scan cycle #N ===` block listing the top candidates per region/width.

Three possible BAT outcomes (in increasing severity of follow-up needed):

- **Clean find.** The Dollet timer appears at the top of `R2 u16` or `R2 u32` (or `R1` if the field-var stack really is inside the game-object struct) with the expected value (~1800 / ~30 / ~54000 at fresh chase start, decreasing). v0.15.13.1 hardcodes that address and the timer reads work. Aaron reports which address + width + initial value.
- **Multiple plausible candidates.** Several addresses show timer-like behavior but it's not obvious which is the real Dollet timer. v0.15.13.1 adds a tighter filter (e.g. value must match a known starting duration within tolerance at chase start) or adds a longer ring window (covers more time so minute-level timers stand out more).
- **No clean candidate.** The scanner finds many addresses but none match expected values, OR finds nothing because Region 2 is mostly unmapped. v0.15.14 falls back to Path B (SETTIMER opcode hook at JSM dispatch slot 0x9C): hook the script-VM dispatch table at the SETTIMER index, capture the duration parameter when the chase script calls it, simulate locally off `GetTickCount`. Doesn't give freeze, but gives reliable read-and-announce.

### Files

- NEW: `src/countdown_scan.inl`
- MODIFIED: `src/countdown_timer.cpp` (scanner wiring, comment corrections)
- MODIFIED: `src/ff8_accessibility.h` (version bump to 0.15.13.0)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Deferred (post-scanner-find or post-Path-B)

- `menu_tts.cpp` T-handler `!shift` gate. Theoretical conflict only.
- `Plan & Research Documents/Dollet timer countdown deep research results.md` comment fix (still has the 0x01CFECCC typo).
- `src/field_display_names.h` audit (wrong mappings for fieldIds 0x0134 / 0x0136 in the Dollet comm tower area, surfaced by the v0.15.12.0 BAT interpretation cycle).

## v0.15.12.0

First implementation of mission countdown timer accessibility, plus a structural cleanup that retired two project files which had grown past the size at which Claude (or any editor with a bounded buffer) could safely round-trip them. The two changes ship together because the cleanup is what made the version bump for the countdown work even possible.

### Countdown timer module (NEW)

New `src/countdown_timer.h` / `src/countdown_timer.cpp` targeting the Dollet 30-minute mission timer and, by virtue of FF8 having a single generic countdown system shared across all timed events, also Fire Cavern (10/20/30/40 min), Missile Base, Centra Ruins Odin, and Rinoa-in-space.

Reads field var 724 ("Dollet mission time", uint16) at `0x01CFECCC` each frame, SEH-wrapped. State machine: INACTIVE / ACTIVE / FROZEN. Units classifier (FRAMES_30HZ 15000-60000, SECONDS 500-3000, MINUTES 5-60) — rejects values outside these ranges as noise so the classifier can't latch onto an unrelated word at the same address. Scheduler fires TTS at 25:00 / 20:00 / 15:00 / 10:00 / 5:00 / 1:00 / 0:30 boundaries; boundaries above the session's initial value are pre-flagged so Fire Cavern's shorter durations don't fire stale "25 minutes remaining" announcements. T key (gated on `IsActive() && !shift && !alt`) announces remaining time on demand. Shift+T (gated on `shift && !alt`) toggles an experimental freeze that rewrites `0x01CFECCC` each frame to the captured value. Comprehensive `Log::Mod` diagnostic logging on every value change (rate-limited 50 ms), state transition, hotkey press, and units-detection decision.

Wired into `src/dinput8.cpp` (`#include "countdown_timer.h"` plus `Initialize` / `Update` / `Shutdown` calls in the existing module-init / main-loop / cleanup sections) and `src/deploy.bat` (added `countdown_timer.cpp` to the cl.exe compile list).

Research saved at `Plan & Research Documents/Dollet timer countdown deep research results.md`. Key findings: timer opcode family is SETTIMER 0x09C, DISPTIMER 0x09D, SHADETIMER 0x09E, GETTIMER 0x0A4, KILLTIMER 0x0B9 (STIM / WAIT_TIMER / TIMER do NOT exist in FF8 — those names come from FF7's opcode set and contaminated some wiki references). Field-var-stack base on Steam 2013 is `0x01CFE9B8`, and var 724 lands at `+0x2D4 = 0x01CFECCC`. The 0x14 savemap correction does NOT apply to the field-var stack — those are two separate memory regions. The script-side snapshot at `0x01CFECCC` is updated by GETTIMER (opcode 0x0A4) when the field script calls it; the actual per-frame engine timer lives at a separate address in the `0x01D00000-0x01E00000` range that is not in any public source.

### BAT result: Case C — snapshot never observed positive

Aaron triggered the Dollet chase. T key did not announce, and Shift+T spoke "No timer to freeze," which means `IsActive()` returned false the whole time — the countdown module never saw a value in `0x01CFECCC` that the classifier accepted. Either the snapshot stays at zero during the chase (which would mean the field script never calls GETTIMER to refresh it), or it holds a value outside our classifier ranges. The `[CountdownTimer]` log lines in `ff8_mod.log` from a chase session will disambiguate; that diagnostic data is what v0.15.13 needs to design the next attempt.

This is the worst-case branch of the BAT decision tree documented in DEVNOTES, and it's a clear signal that v0.15.13 has to find the live engine global rather than relying on the script-side snapshot. Since Aaron is blind and can't use Cheat Engine or x64dbg to find the address externally, the v0.15.13 path is one of:

- In-mod memory scanner that runs during the chase: snapshot a candidate region every second, diff against previous snapshot, surface addresses whose uint16 / uint32 values decrement monotonically at ~30 Hz or ~1 Hz.
- SETTIMER opcode hook (0x09C in the JSM dispatch table) that captures the duration parameter at chase start, then simulates the countdown locally in the mod off a GetTickCount baseline.

The current snapshot read + Shift+T rewrite path stays in place either way as a complementary diagnostic.

### Structural cleanup — file slimming

Two files had grown past the size at which Claude could safely round-trip them through a single full-file rewrite (the only edit mode available with the filesystem MCP toolset in the current session — no `edit_file`):

- `src/ff8_accessibility.h` was **421.80 KB**, almost all of it a single line-12 comment that contained the inline-changelog chain accreted across roughly 80 versions of the project. The header itself only needed to provide `#pragma once`, three system includes, and the `FF8OPC_VERSION` macro — everything else was historical accretion.
- `CHANGELOG.md` was **488.25 KB**, with entries since project start prepended one by one. The push utility only reads the top entry, so the size was load-bearing nowhere.

Cleanup:

- `src/ff8_accessibility.h` moved to `src/ff8_accessibility_history.h` (NOT included by the build — nothing references it; the rename preserves the full inline-changelog history off the build path). New slim `src/ff8_accessibility.h` written with the header guard, the three system includes, a pointer comment to the history file and to CHANGELOG.md, and the `FF8OPC_VERSION` macro at v0.15.12.0 with no trailing comment.
- `CHANGELOG.md` moved to `CHANGELOG_HISTORY.md` (preserves all pre-v0.15.12.0 entries). New slim `CHANGELOG.md` written with the file header explaining the format + push-utility contract, this v0.15.12.0 entry on top, and a pointer to `CHANGELOG_HISTORY.md` for older content. Future versions get prepended here as normal.

`deploy.bat`'s version-extract regex (`findstr /B /C:"#define FF8OPC_VERSION "`) still resolves cleanly to "0.15.12.0" since the new header has exactly one matching line at column 0 with no historical mentions to compete with. (The history file is not in the build's includepath traversal, but even if it were, the regex pattern starts at column 0 and all the historical mentions inside it are inside comment lines starting with `// `, which the `/B` anchor correctly excludes.)

### Files

- NEW: `src/countdown_timer.h`
- NEW: `src/countdown_timer.cpp`
- NEW: `src/ff8_accessibility_history.h` (renamed from `src/ff8_accessibility.h`, preserved off the build path)
- NEW: `CHANGELOG_HISTORY.md` (renamed from `CHANGELOG.md`)
- MODIFIED: `src/dinput8.cpp` (countdown_timer wiring; some pre-existing v0.15.9.11.3.x historical-rationale comment blocks compressed to short summaries during the rewrite to fit within Claude's response budget for a 720-line file — the full historical comments are preserved at GitHub HEAD `8d29ee61` if a future session needs to restore them)
- MODIFIED: `src/deploy.bat` (added `countdown_timer.cpp` to the cl.exe compile list)
- REPLACED: `src/ff8_accessibility.h` (slim version, v0.15.12.0 macro only)
- REPLACED: `CHANGELOG.md` (slim version, this entry on top)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`, `Plan & Research Documents/Dollet timer countdown deep research results.md`

### Deferred to v0.15.13

- In-mod scanner for the live engine timer global, OR SETTIMER opcode hook for start-event simulation (Case C remediation per BAT result above).
- `menu_tts.cpp` T-handler `!shift` gate so Shift+T doesn't fire both `AnnouncePlayTime` and `CountdownTimer::ToggleFreeze` in menu mode 6. Theoretical conflict only — player can't realistically open the menu during the Dollet chase — but worth fixing.
