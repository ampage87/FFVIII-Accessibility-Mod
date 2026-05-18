**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.16.5.2** (commit `63dbbfac`, pushed 2026-05-17 20:59:00 UTC). **Local tree = v0.17.5.2, BAT'd clean** -- ready to push via `Utilities/push_to_github.ps1`.

v0.17.5.2 BAT result: prune pass worked exactly as designed. bg2f_2's 46-triangle A* corridor (longer this BAT than v0.17.5.1's 13-triangle path because Aaron started from a different spot in the hallway) produced 46 funnel waypoints pre-prune and **10 waypoints post-prune** -- 36 collinear-ish waypoints removed in 37 sweeps. bghall_1's 11-triangle corridor went 11 -> 5 (6 pruned in 7 sweeps). First announced cardinal on bg2f_2 was "north" matching Aaron's mental model (path's first leg was -X direction this run). Aaron's qualitative: "directions seemed good throughout" walking elevator -> directory -> classroom hallway -> elevator. No velocity-stuck events; no autodrive regressions. Quantization architecture (v0.17.5), announcement hysteresis (v0.17.5.1), and collinear pruning (v0.17.5.2) all working together cleanly.

With v0.17.5.2 BAT'd, the GPS announcement pipeline is in good shape. The open question is whether v0.17.5.3 (option B hybrid announcement) is still worth implementing. Aaron's BAT path this time didn't reproduce the original "east when I expected north" complaint, but that scenario depends on starting position -- if Aaron walks the corridor from a position where the first segment genuinely heads east before bending north, path-aware will still announce east first. Probably worth deferring B until/unless Aaron hits that scenario again in real play, and focusing next session on the v0.16.5.2 BAT triage backlog instead.

---

## v0.17.5.2: Funnel waypoint pruning (BAT'd clean, ready to push)

E from session discussion. Reduces SSFA micro-corner waypoint count without changing the path's macro shape or wall avoidance properties. BAT confirmed nearly 5x waypoint reduction on the test case and good qualitative behavior throughout the test path.

### BAT result

| Field | A* tris | Pre-prune wp | Post-prune wp | Sweeps |
|-------|---------|--------------|---------------|--------|
| bghall_1 | 11 | 11 | 5 | 7 |
| bg2f_2 | 46 | 46 | 10 | 37 |

No velocity-stuck, no autodrive regressions, no compile warnings.

### The change

In `field_nav_pathfinding.inl`, new function `PruneCollinearWaypoints` called at the end of `FunnelPath`:

1. For each interior waypoint B with neighbors A and C, compute perpendicular distance from B to segment AC.
2. If perpDist < `PRUNE_PERP_EPSILON = 50.0f`, remove B.
3. Sweep-to-stable (cap 100 sweeps as safety bound).

First and last waypoints preserved. Real corners (perp dist > 50) untouched.

50-unit epsilon is below typical FF8 wall thickness (~100+ units). With existing AGENT_RADIUS=30 portal shrinking, worst-case wall clearance after pruning is ~80 units -- safely inside walkable space.

### Files changed (v0.17.5.2)

- `src/ff8_accessibility.h` -- version bump
- `src/field_nav_pathfinding.inl` -- new `PruneCollinearWaypoints` function and FunnelPath call site

### What's NOT touched

v0.17.5 quantization (working). v0.17.5.1 announcement hysteresis (working). The SSFA funnel itself (output still optimal geometrically; pruning discards non-meaningful turns). EdgeMidpointPath fallback. v0.16.5.2 BAT triage backlog.

### Known remaining gap

Even with pruning, bg2f_2's first cardinal will be "east" (genuinely the first segment direction). Aaron's mental model wants "north" (final destination direction). Resolving fully will need hybrid announcement (B). Queued for v0.17.5.3.

### BAT verification

Repeat v0.17.5.1 path. Expect:

1. `[funnel] N triangles -> M waypoints (post-prune; pre-prune=K, was J centers)` showing K >> M on zigzag corridors. bg2f_2 should drop from 13 to ~4-5.
2. `[funnel-prune] removed N collinear waypoints (eps=50 units, K sweeps)` lines.
3. Aaron qualitative: fewer cardinal-change announcements per journey, each at a real bend.

---

## v0.17.5.1: GPS announcement hysteresis (BAT'd, hysteresis works)

Point release on top of v0.17.5. Quantization architecture untouched; only the announcement cadence changes.

### The change

In `field_nav_gps.inl::UpdateGPS`, replace the v0.17.0 sector-change + step-change + waypoint-force cadence with cardinal-change-only-with-hysteresis:

- Announce ONLY when `dirIdx != s_gpsLastDirIdx` AND the new value has held steady for 500ms (`GPS_DIR_HYSTERESIS_MS`).
- Step-count changes: log only, no speech.
- Waypoint advances: log only, no speech (if the cardinal happens to match across two legs, the handoff is silent).
- Nearby/in-range one-shots: unchanged.

Mechanism: two new statics `s_gpsPendingDirIdx` (candidate cardinal awaiting confirmation) and `s_gpsPendingDirSince` (when it became candidate). New cardinal -> candidate. Same candidate held 500ms -> promote and speak. Different candidate before 500ms -> reset timer.

### Why this addresses both bg2f_1 issues

1. **TTS rattle**: structurally eliminated. Brief sector flips reset the candidate timer rather than promote, so they never reach the screen reader.
2. **"South when needed north"**: the bg2f_1 geometry (per Aaron's description: enter at bottom of C, door at top opposite side) plus quantized axes (RIGHT->world-north, DOWN->world-east) make a real south-leg impossible -- DOWN would push the player away from the door. So the south was a transient sector flip during the rattle. With hysteresis it can't fire unless it persists 500ms, which on a 2-leg L-path it won't.

### Files changed (v0.17.5.1)

- `src/ff8_accessibility.h` -- version bump
- `src/field_nav_gps.inl` -- new hysteresis state + reset in StopGPS + prime in StartGPS + cadence block rewritten

### What's NOT touched

v0.17.5 quantization (working). v0.17.4 det fix. v0.17.1 path-aware path building (waypoints still steer; they just don't trigger speech anymore). v0.17.0 ComputeScreenDirIndex math. The diagnostic [NAV-OBSERVE] and [NAV-PROJ] log lines (still fire as before).

### BAT verification

Repeat v0.17.5 path (elevator -> classroom hall -> classroom -> dorm). Expect:

1. Drastically fewer `[GPS] Update` lines, all tagged `hysteresis=ok` and `dirChanged=1`.
2. No back-to-back announcements during straight walking.
3. bg2f_1 hallway: cardinals "north" then "west" (or northwest near the bend). No spurious south.
4. Aaron qualitative: silence while walking straight, announcement on each genuine direction change.

If a spurious cardinal still sneaks through (held >500ms but Aaron thinks it's wrong), that's a separate issue from rattle and we look at the .ca file's pre-quantization values in the `[NAV-PROJ-INIT] quantization` log line.

---

## v0.17.5: Load-time 90-degree quantization (BAT'd, architecture works)

Replaces the v0.17.4 passive calibration loop and the v0.17.5-pre filter tightening with a single load-time quantization step. Same end result on the v0.17.4 BAT fields, zero state machine, no observation-based correction.

### Architecture

1. At field load, parse .ca file into 2D-normalized camRight/camDown (v0.17.0.1 -- unchanged).
2. Det convention check: if det(camRight, camDown) > 0, negate camDown (v0.17.4 -- unchanged).
3. **Quantize camRight to nearest 90-degree world cardinal; derive camDown via (x,y) -> (y,-x) rotation (v0.17.5 -- new).**
4. Mirror to drive-private pair (unchanged).

The quantization is in field_nav_fieldscripts.inl after the det-fix block.

### Why it works

v0.17.3 BAT clean samples showed engine RIGHT direction = world cardinal on every tested field:
- bghall_1 CA angle 7.8 -> world east (0deg)
- bghall_4 CA angle 23.8 -> world east (0deg)
- bg2f_1 CA angle 65.4 -> world north (90deg)
- bg2f_2 CA angle 60.5 (det-fixed) -> world north (90deg) +5-11deg residual (within 22.5 sector tolerance)
- bgroom_1 CA angle -62.5 -> world south (-90deg)

FF8 engine appears to use 90-deg-quantized camera matrix for DIJOYSTATE2 -> walkmesh transform. Quantizing at load makes mod prediction match engine actual on every clean field.

### What's removed from v0.17.4 / v0.17.5-pre

- `ObsCalibrateAxes()` function in field_nav_observe.inl
- Its call from `ObserveArrowResponse()`
- `s_fieldCalibratedManual` static flag and its reset
- Observer hold-state reset at field load (no longer needed)
- Include order change (observe.inl back to its original position)
- v0.17.5-pre filter constants (`OBS_CALIB_AXIS_ALIGN_MAX`, `OBS_CALIB_MAX_ROT_DEG`, etc.)

The v0.17.3 observer log block stays as pure diagnostic.

### Files changed (v0.17.5)

- `src/ff8_accessibility.h` -- version bump
- `src/field_navigation.cpp` -- comment block above s_camRight/Down rewritten; s_fieldCalibratedManual removed; observe.inl include back at original position
- `src/field_nav_observe.inl` -- ObsCalibrateAxes function + call removed; v0.17.3 comment restored
- `src/field_nav_fieldscripts.inl` -- observer/lock reset removed; quantization block added after det fix; source tag now "ca-quantized"

### What's NOT touched

v0.17.4 det convention check (proven correct, necessary for bg2f_2 and any other left-handed CA fields). v0.17.0.1 2D normalization. v0.17.2 state separation. v0.17.1 path-aware GPS. v0.17.3 observer logging. Auto-drive empirical calibration (separate pair). v0.16.5.2 BAT triage backlog.

### BAT verification

Repeat v0.17.4 recipe (walk each cardinal in bghall_1, bghall_4, bg2f_1, bg2f_2, bgroom_1). Expect:

1. `[NAV-PROJ-INIT] det-correction` for bg2f_2 only.
2. `[NAV-PROJ-INIT] quantization` line for each field showing pre-angle -> snapped angle.
3. `[NAV-PROJ-INIT] source=ca-quantized` summary.
4. **No [NAV-CALIB-AUTO] lines.** That function is gone.
5. `[NAV-OBSERVE]` lines: DIVERGE near 0 on most fields, 5-11deg on bg2f_2 (acceptable).
6. Qualitative: all five fields navigate correctly from the first cardinal announcement. No "walk first" warmup. No TTS rattle.

---

## v0.17.4: Det fix + passive self-correcting calibration (BAT'd, calibration removed in v0.17.5)

Det fix worked perfectly (bg2f_2 classroom now navigable). Passive calibration was unstable -- accepted curve-walked samples that reversed correct cals, and slightly off-axis samples that drifted correct axes past optimum. Total of 8 calibration events across the BAT, 3 of which were either no-ops or actively harmful. Aaron's qualitative report mapped exactly onto the timeline (TTS rattle, bghall_1 Directory harder, bg2f_2 finally correct via det fix).

The v0.17.5-pre attempt to stabilize via filter tightening (one-shot + axis-aligned filter + rotation cap) would have worked but added complexity. Aaron's question "is calibration really necessary?" exposed that the engine appears to use 90-deg-quantized camera axes, and that quantization at load time gives the same result with no calibration loop at all. v0.17.5 ships the quantization approach.

The fix the v0.17.3 BAT diagnosed. Two paired changes that ship together.

### Fix 1: det convention check at CA load (fieldscripts.inl)

After the v0.17.0.1 2D normalization computes camRight/camDown, check `det = camRight.x*camDown.y - camRight.y*camDown.x`. If positive (left-handed projection — axis1 points world-up after normalization, opposite the screen-down convention), negate camDown to force det=-1. Logs `[NAV-PROJ-INIT] det-correction` when fired. Propagates to drive-private pair too (auto-drive's empirical calibration will overwrite those anyway on first drive).

### Fix 2: passive self-correcting calibration (observe.inl)

New `ObsCalibrateAxes()` called from `ObserveArrowResponse()`. Stricter gating than the diagnostic log:
- heldTicks >= 30 (~500ms, twice diagnostic threshold)
- delta magnitude >= 100 units (twice diagnostic threshold)
- dot(predicted, measured) >= 0.5 (60° cone, rejects wall-deflection / curve-noise samples)

When all pass: compute signed theta = atan2(cross, dot), rotate BOTH s_camRight and s_camDown by theta using standard 2D rotation matrix. Update s_camAxesSource to "calibrated". Log `[NAV-CALIB-AUTO]` with old/new axes and rotation magnitude.

Uniform rotation per field (Finding 1) means one clean observation fully calibrates that field. The diagnostic [NAV-OBSERVE] log from v0.17.3 stays at its lower 18-tick / 50-unit / no-cone thresholds.

### Files changed (v0.17.4)

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_fieldscripts.inl` — det check after CA 2D normalization
- `src/field_nav_observe.inl` — `ObsCalibrateAxes()` function and call from `ObserveArrowResponse()`; top-of-file comment updated to reflect that the observer now writes state
- `src/field_navigation.cpp` — comment block above `s_camRight/Down` updated to document the observer's writes

### What's NOT touched

Auto-drive empirical calibration in `field_nav_autodrive.inl` (proven correct, doesn't read s_camRight/Down). v0.17.2 state separation (preserved). Chase auto-pilot config. v0.17.1 path-aware logic. v0.17.0.1 2D normalization. v0.16.5.2 BAT triage backlog. Classroom entity catalog under-population.

### BAT verification

Repeat v0.17.3 BAT recipe (walk each cardinal direction in bghall_1, bghall_4, bg2f_1, bg2f_2, bgroom_1). Expect:
1. `[NAV-PROJ-INIT] det-correction` line for bg2f_2 only (the only det=+1 field).
2. `[NAV-CALIB-AUTO]` line per field within seconds of first eligible arrow hold.
3. Subsequent `[NAV-PROJ] start` lines show `axes=calibrated` with corrected camRight/camDown.
4. Qualitative: initial cardinal at GPS-start may briefly be wrong (CA-derived); after ~500ms of walking it self-corrects. End-to-end navigation no longer requires going opposite the instructions.

---

## v0.17.3: Passive arrow-response observer (BAT'd, diagnostic complete)

BAT result: observer captured 66 single-arrow observations across 5 fields. Per-field rotation analysis revealed:

- bghall_1: -7.8° (in 22.5° sector tolerance, Aaron didn't notice)
- bghall_4 (elevator): -23.8° (borderline, Aaron reports works perfectly)
- bg2f_1 (2nd-floor hall): +24.6° (wrong cardinals)
- bg2f_2 (classroom): det=+1.0 (left-handed, axis1 points world-up; UP/DOWN cardinals exactly opposite — "had to go opposite the instructions")
- bgroom_1 (dorm, not tested by Aaron but observer captured): -27.5°

Key learning: rotation is uniform across all four arrows within a field (stdev <0.5° on clean samples), so a single clean observation is enough to fully calibrate. v0.17.4 implements both the det fix and the passive calibration based on this data.

No behavior change. Pure diagnostic instrumentation. The observer fires every tick from `Update()`, gates on:
- No auto-drive active (synthetic injection would pollute the sample)
- No chase-drive active (same)
- Player entity detected
- No dialog open (engine ignores movement)
- Exactly one arrow held (diagonals are ambiguous between two single-axis predictions)

When all gates pass and the arrow has been held for >= 18 ticks (~300ms) with >= 50 world-unit measured delta, logs:

```
[NAV-OBSERVE] field='bg2f_1' axes=ca-file arrow=RIGHT held=22ticks delta=(120,-50)
              measured=(0.92,-0.38) predicted=(0.417,0.909)
              DIVERGE=68deg | camRight=(0.417,0.909) camDown=(0.909,-0.417)
```

Throttled to one sample per 1.5s so a continuous hold doesn't flood the log.

### Divergence interpretation

- **~0°**: CA values match engine projection on this field. No fix needed.
- **~180°**: Both camera-axis components signed wrong. Single-line fix: negate `s_camRight` AND `s_camDown` after CA load.
- **~90°**: Axes swapped or rotated 90°. Likely fix: swap which axis is treated as "right" vs "down" during CA load.
- **Other consistent angle**: More complex transformation needed. Data will narrow it down.
- **Variable within a field**: Camera state changes during play (e.g. multi-section fields). Fix needs runtime camera read, not the static .ca snapshot.

### Arrow-to-camera-axis mapping (used by the prediction)

- RIGHT arrow (lX=+1000) → +camRight world direction
- LEFT arrow → -camRight
- DOWN arrow (lY=+1000, DirectInput convention) → +camDown
- UP arrow → -camDown

Mirrors the chase auto-pilot's empirical calibration's arrow-axis mapping (chase calibration injects analog and measures; this observer measures the engine's response to Aaron's physical key presses).

### Files changed (v0.17.3)

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_observe.inl` — NEW file, ~150 lines. State (5 statics), thresholds (3 constants), 4 helpers (ObsBitCount, ObsReadArrows, ObsArrowName, ObsLogSample), and the main `ObserveArrowResponse()` function.
- `src/field_navigation.cpp` — `#include "field_nav_observe.inl"` after `field_nav_diagnostics.inl`; `ObserveArrowResponse();` call added to `Update()` right after `UpdateGPS();`.

### What's NOT touched

Projection math in `field_nav_gps.inl::ComputeScreenDirIndex` (still uses CA-derived axes; v0.17.4 fix targets here). Auto-drive's empirical calibration (proven correct). Chase auto-pilot config. v0.17.2 state separation (proven working). v0.17.1 path-aware buffer + advance logic. v0.17.0.1 CA 2D normalization. v0.16.5.2 BAT triage backlog. Classroom entity catalog under-population.

### BAT verification

Load save in `bghall_1`. Walk slowly in each cardinal direction (N/E/S/W) holding only one arrow for 2-3s each. Transition to `bg2f_1` and do the same. Transition into the classroom and do the same. Include the elevator-side field as a known-good baseline.

Expected log volume: ~12-16 `[NAV-OBSERVE]` lines plus incidental samples. Look for divergence patterns per (field, arrow). Whatever pattern emerges tells us the v0.17.4 fix.

---

## v0.17.2: Camera-axes state separation + diagnostic logging (BAT'd, state separation confirmed; cardinals on `bg2f_1`/classroom STILL WRONG)

BAT result: state separation works as designed. `[NAV-PROJ]` log line showed `axes=ca-file` and the camera-axis values matched the field-load `[NAV-PROJ-INIT]` for the corresponding field. Hypothesis A (calibration corruption) is definitively ruled out. But cardinals on `bg2f_1` and the classroom were still wrong — Aaron reported "had to go opposite the instructions provided by navigation". The field outside the elevator now works perfectly. Bug pattern shifted: hypothesis B (CA-vs-engine field-specific mismatch) is confirmed. Need v0.17.3 observer to gather divergence data before designing the fix.

### Original v0.17.2 design notes

v0.17.1 BAT log analysis:

- Path-aware logic CONFIRMED WORKING. Two GPS sessions on the test field: funnel produced 11 and 38 waypoints; advance routine fired; overshoot detection caught three sub-arrive-distance passes.
- BUT cardinals didn't match Aaron's walking direction. BAT log `[NAV-PROJ]` lines at GPS-start showed `camRight=(0.493,0.870) camDown=(-0.871,0.492)` — NOT the CA-derived axes for `bghall_1` first load (`(0.991,0.135)/(0.134,-0.991)`). Something overwrote `s_camRight/Down` between field load and GPS.
- A second field load happened at 17:31:44, ~5s before the first GPS session. Entity counts (13 entities, bg=6, others=22, 1 event trigger) differed from `bghall_1`'s first load (10 entities) — Aaron transitioned to a smaller indoor field between launch and GPS. The axes at GPS time MAY be the CA-derived values for that other field, OR they may be empirical-calibration overwrites; the log middle (~3 minute gap) was unloadable due to MCP size limit.

### Two competing hypotheses

**Hypothesis A (calibration corruption):** Auto-drive's `[CALIB]` phase 1/2 writes to `s_camRightX/Y, s_camDownX/Y` — the same statics manual nav reads. If Aaron triggered any auto-drive (chase or F9) between launch and GPS, calibration's writes leak into manual-nav's projection.

**Hypothesis B (CA-vs-engine divergence):** Chase doc "What chase-drive proved works" verifies empirical calibration produces correct axes; the CA-derived equivalent is unverified at the same rigor (Finding #10 mentions it as future work). If CA-values don't match what the engine actually uses on some fields, manual-nav cardinals will be wrong on those fields regardless of calibration.

### Approach

State separation kills hypothesis A definitively. Diagnostic logging distinguishes B from A in the next BAT.

**Manual-nav pair (`s_camRightX/Y, s_camDownX/Y`):** set once at field load by `HookedFieldScriptsInit` from CA values (via v0.17.0.1's 2D normalization) or identity defaults if CA absent / degenerate. Never written by auto-drive. Read by:
- `field_nav_gps.inl::ComputeScreenDirIndex`
- `field_nav_gps.inl` `[NAV-PROJ]` log lines
- `field_nav_helpers.inl::FormatNavComponents`

**Auto-drive private pair (`s_driveCamRightX/Y, s_driveCamDownX/Y`):** NEW. Mirrors manual-nav pair at field load (so first drive starts from CA values). Overwritten by `[CALIB]` phase 1/2 writes. Read only by `field_nav_autodrive.inl::SetAnalogFromVector`.

**Diagnostic tag (`s_camAxesSource`):** NEW `const char*`. Set to `"ca-file"` or `"identity"` at field load. Included in `[NAV-PROJ] start` log line. With this in the log, the next BAT can compare camera-axis values across multiple GPS sessions on the same field. Same axes across sessions = state separation worked; differing axes with `axes=ca-file` on both = CA-vs-engine divergence and v0.17.3 needs deeper work.

### Files changed (v0.17.2)

- `src/ff8_accessibility.h` — version bump
- `src/field_navigation.cpp` — new `s_driveCam*` state pair + `s_camAxesSource` tag declaration with explanatory comment
- `src/field_nav_fieldscripts.inl` — reset BOTH pairs at field load, CA-load writes to both pairs + sets source tag, fallback paths set tag to `identity`
- `src/field_nav_autodrive.inl` — calibration phase 1/2 writes renamed `s_cam*` → `s_driveCam*`; `SetAnalogFromVector` reads renamed; all `[CALIB]` log messages updated; doc comment block updated
- `src/field_nav_gps.inl` — `[NAV-PROJ] start` log adds `field='%s'` and `axes=%s` fields; manual-nav axis reads unchanged (still reads `s_camRight/Down` = the manual-nav pair)

### What's NOT touched

Chase auto-pilot config tables. `field_nav_pathfinding.inl` (A* + funnel reused unchanged). `field_nav_helpers.inl::FormatNavComponents` (reads the now-manual-nav-pinned pair without modification). v0.17.1 path-aware buffer + advance logic (proven working, no reason to risk regression). v0.16.5.2 BAT triage backlog. Classroom entity catalog under-population from v0.17.0.1.

### BAT verification

1. Load any field. New `[NAV-PROJ] start` log line will include `field='...'` and `axes=ca-file` (or `identity` on no-CA fields).
2. Walk to a few catalog targets. Verify cardinals match the direction Aaron actually walks.
3. Trigger at least one auto-drive (F9 list cycle then start drive, or chase if convenient).
4. After the auto-drive completes, run a manual GPS session on the same field. The `[NAV-PROJ]` line should show camera axes unchanged from the field-load values — calibration can no longer touch the manual-nav pair.

If cardinals are now correct end-to-end on `bghall_1`: hypothesis A was the cause and v0.17.2 is the complete fix.

If cardinals are still wrong with `axes=ca-file` and matching axes across sessions: hypothesis B is in play. v0.17.3 work options: (i) auto-drive-style empirical calibration on GPS start (slow, 24+ tick injection), (ii) read engine's runtime camera matrix from memory (via FFNx hook addresses or disassembly), (iii) read entity-coords' 2D screen projection from a different memory location (engine may store both world and screen coords).

### Open: classroom catalog under-population

From v0.17.0.1 BAT: classroom only listed 2 "interactions" with no exit, and auto-drive didn't work. Separate diagnosis track — need field name and F9 list contents to triage.

---

## v0.17.1: Path-aware GPS direction (BAT'd, path logic confirmed working)

v0.17.0.1 BAT confirmed orientation is correct: on `bghall_1` the normalized camera projection produced `camRight=(0.991,0.135) camDown=(0.134,-0.991)` det=-1.0 and the "Elevator" target on `bg2f_1` navigated cleanly. But Aaron reported: from classroom door → elevator corridor, directions were correct; from elevator corridor → classroom door, directions were wrong. The hallway is C-shaped. Going one way the straight-line bearing happens to align with the walkable direction at every step; going the other way the bearing cuts through the inside of the curve and aims through walls. This is the path-aware direction problem queued for v0.17.1.

v0.17.1 BAT (2026-05-17 17:32) RESULT: path-aware logic CONFIRMED WORKING. Funnel produced 11 and 38 waypoints across two GPS sessions; waypoint advance fired through the sequence; overshoot detection caught three sub-arrive-distance passes correctly. But manual-nav cardinals at the GPS sessions used corrupted camera axes (see v0.17.2 above). DO NOT TOUCH BuildGpsPath / AdvanceGpsWaypoint / save-restore of shared waypoint state — those work as designed.

### Approach

Run A* on the walkmesh from player triangle to target triangle (reusing `ComputeAStarPath` in `field_nav_pathfinding.inl`), funnel-smooth the corridor (`FunnelPath`), and announce the cardinal toward the NEXT waypoint rather than the final destination. The funnel produces a small number of turn-point waypoints; on a straight path it's a single waypoint at the destination (behavior identical to v0.17.0.1), on a curved path it's one waypoint per major bend.

### Key implementation choices

- **GPS-private waypoint buffer.** `s_gpsWaypoints[MAX_GPS_WAYPOINTS=64][2]` in `field_nav_gps.inl`. Separate from the shared `s_waypoints[]` used by `UpdateAutoDrive` so an active auto-drive and an active GPS session don't trip over each other.
- **Save/restore around the A* call.** `BuildGpsPath` snapshots shared `s_waypoints[]`, `s_waypointCount`, `s_waypointIdx`, `s_usingFunnel`, `s_corridor[]`, `s_corridorCount`, `s_wpMinDist` before A* runs and restores after copying the funnel result into the GPS buffer. Uses file-scope statics for the save buffers (corridor is 8 KB, too big for stack).
- **Waypoint advance threshold = 200 units** (`GPS_WP_ARRIVE_DIST`). Generous compared to auto-drive's 60-unit `FUNNEL_ARRIVE_DIST` because the player walking themselves doesn't need precise turn points — the threshold's job is to advance the announced direction BEFORE the player reaches the corner.
- **Overshoot detection** mirrors auto-drive: if the player got within `GPS_WP_OVERSHOOT_CLOSE=300` and distance is now growing, advance even if 200-unit threshold not hit (catches cutting corners).
- **Initial advance** in BuildGpsPath: skip past any initial waypoints already within `GPS_WP_ARRIVE_DIST` of the player. Without this, the first waypoint might be the player's own position (when player is at a corner), producing one tick of jittery direction before normal advance.
- **Cadence: waypoint advance forces announcement.** The v0.17.0 minimum-interval floor (3s) is broken by direction changes; v0.17.1 also breaks it on waypoint advance. Matters for L-shaped paths where two consecutive legs share a cardinal — the player needs the corner-handoff signal even when the cardinal text doesn't change.
- **Trigger-line targets** pass `skipTriggerIdx = -(entityIdx + 200)` to `ComputeAStarPath` so A* can enter the destination triangle. Runtime-entity targets pass `targetEntityIdx = entityIdx` so push-radius blackout doesn't block the goal.
- **Fallback unchanged from v0.17.0.1** when walkmesh isn't loaded, A* fails, or islands are disconnected. `BuildGpsPath` returns false, `s_gpsUseWaypoints` stays false, straight-line direction takes over.

### Diagnostic logging

- `[NAV-PATH]` log lines at BuildGpsPath: walkmesh availability, startTri/goalTri, A* success/failure, funnel wp count, first 6 wp positions.
- `[NAV-PATH] wp N/M reached` per advance with reason (arrived/overshoot).
- `[NAV-PROJ] start` and `update` now include `steer=(x,y)` (separate from `target=(x,y)`) and `wp=I/N`.

### Files changed (v0.17.1)

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_gps.inl` — path-aware state, BuildGpsPath, AdvanceGpsWaypoint, StartGPS/UpdateGPS/StopGPS integration, expanded diagnostics. File size 30 KB (under 60 KB warn).

### BAT verification

Load `bghall_1`, cycle F9 to a target on the opposite side of the curve, Backspace to start GPS. Initial announcement = cardinal toward FIRST turn point, not destination. Walking the corridor should produce `[NAV-PATH] wp 0/N reached` log lines and fresh direction announcements at each bend. The reverse direction — same field, opposite endpoints — should now produce correct directions, the v0.17.0.1 BAT failure case. Straight fields should behave indistinguishably from v0.17.0.1.

If path-aware breaks, the `[NAV-PATH]` log identifies which step failed (walkmesh missing, A* failure, disconnected islands, etc.). The straight-line fallback should always work, so v0.17.1 should never be worse than v0.17.0.1.

### Open: classroom catalog under-population

From v0.17.0.1 BAT: classroom only listed 2 "interactions" with no exit, and auto-drive didn't work. Separate diagnosis track — need field name and F9 list contents to triage. Hypotheses: classroom uses SETLINE Line-entity triggers (like dorm fields) that the catalog extractor doesn't surface, OR the classroom JSM uses non-standard SET3 PSHM patterns. Both produce sparse catalogs.

---

## v0.17.0.1: CA normalization fix (SHIPPED LOCAL, PROVEN BY BAT)

v0.17.0 BAT log surfaced the bug immediately: `bghall_1` `[NAV-PROJ-INIT]` showed `camRight=(0.991,0.135) camDown=(0.044,-0.330)`. camRight has 2D magnitude 1.0, camDown only 0.333 — asymmetric scale biases `atan2(sD, sR)` toward east/west.

`.ca` axes are stored as 3D unit vectors. On tilted cameras (most Balamb Garden interiors), axis1's 3D magnitude is dominated by its Z component (depth into floor); the XY projection is short. v0.17.0 divided by 4096 and used the raw XY components verbatim. Fix: normalize the 2D projection of axis0 and axis1 to unit length before writing `s_camRight/Down`.

The chase auto-pilot's empirical calibration in `field_nav_autodrive.inl` already does this correctly — it divides the measured walkmesh delta by `cdist` (the magnitude). So chase fields with the chase calibration always had correct normalized values; manual nav with CA-derived values didn't, on tilted-camera fields.

### Fix

In `field_nav_fieldscripts.inl`, after `LoadCameraAxes`:

```cpp
float r2x = (float)s_cameraAxes.axis0[0] / 4096.0f;
float r2y = (float)s_cameraAxes.axis0[1] / 4096.0f;
float r2len = sqrtf(r2x*r2x + r2y*r2y);
float d2x = (float)s_cameraAxes.axis1[0] / 4096.0f;
float d2y = (float)s_cameraAxes.axis1[1] / 4096.0f;
float d2len = sqrtf(d2x*d2x + d2y*d2y);
if (r2len > 0.001f && d2len > 0.001f) {
    s_camRightX = r2x / r2len; s_camRightY = r2y / r2len;
    s_camDownX  = d2x / d2len; s_camDownY  = d2y / d2len;
}
```

### Diagnostic additions

- `[NAV-PROJ-INIT]` log line now says `source=ca-file-normalized` (was `ca-file`) so v0.17.0 vs v0.17.0.1 builds are distinguishable from the log.
- New `raw-2D r2len=N d2len=M` line per field load. `d2len ~1.0` = flat camera, `d2len < 1.0` = tilted camera (the class of fields v0.17.0 mis-projected).
- Degenerate camera guard: if both `r2len` and `d2len` < 0.001, keep identity defaults and log warning (rather than divide by zero).

### BAT expectations

`bghall_1` should now log `camDown=(0.132,-0.991)` instead of `(0.044,-0.330)`. Cardinals announced during GPS guidance should match arrow keys on this field. Default-camera fields (`bgroom_1`, etc.) are unaffected — normalization is a no-op when axis1 already has unit 2D magnitude.

### Files changed (v0.17.0.1)

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_fieldscripts.inl` — CA wiring normalizes 2D projections; extra log lines

### Not touched

- `src/field_nav_gps.inl` (projection math was correct in v0.17.0; only the inputs to it were wrong)
- `src/field_navigation.cpp` `GPS_DIR_NAMES` (unchanged cardinals)
- Chase auto-pilot empirical calibration (already normalizes correctly)

---

## v0.17.0: Manual nav direction projection (SHIPPED, partially correct)

Bug 2 from the v0.16.5.2 BAT triage. Manual GPS direction announcements were correct on default-camera fields and inverted on fields with rotated cameras — Aaron's report was "the mod says to go left and it is correct, but on others it says to go left when I really need to go right." Root cause: the GPS direction code computed the world-space bearing `atan2(dx, dy)` from raw entity coordinates and labeled it with screen-relative names. On rotated cameras the walkmesh axes don't align with the screen, so the labeled direction did not match the arrow key the player needed to press.

### The two-system gap

The mod has two camera-axis storage variables that aren't reconciled:

- **`s_camRightX/Y, s_camDownX/Y`** (in `field_navigation.cpp` near line 580): used by the direction-computation code paths — `FormatNavComponents` for F9/Backspace component readout, and now (v0.17.0) `ComputeScreenDirIndex` for GPS guided nav. Reset to identity on every field load. Previously populated ONLY by the chase auto-pilot's empirical calibration, which runs lazily on first auto-drive — so on regular (non-chase) fields, these stayed at identity defaults and the direction code was lying.
- **`s_cameraAxes`** (`FieldArchive::CameraAxes` struct): populated at field load by `LoadCameraAxes(fieldName, s_cameraAxes)` in `field_nav_fieldscripts.inl` (line ~167). Holds the parsed `.ca` file content with three int16 fixed-point axis vectors. **Was being read into the struct but never consumed.**

v0.17.0 plugs the leak. After `LoadCameraAxes()` returns successfully, `s_camRightX/Y/DownX/DownY` are derived from `s_cameraAxes.axis0` and `axis1` (normalized by /4096) and written into the same statics the chase calibration would use. Default-camera fields produce identity values matching the existing defaults (`axis0=(1,0,0)`, `axis1=(0,-1,0)`); rotated cameras produce non-identity projections that make manual nav correct on the first announcement.

The chase auto-pilot's empirical calibration is untouched. `s_calibPending` stays true and chase calibration will overwrite the CA-derived values when a chase drive starts; the two methods converge for static cameras so this is fine.

### .ca file format

Per-setting layout (38 bytes), confirmed by the existing parser logging:
- bytes  0–5: axis0 (int16 x, y, z) — **screen-right vector in world XY basis**
- bytes  6–11: axis1 (int16 x, y, z) — **screen-down vector in world XY basis**
- bytes 12–17: axis2 (int16 x, y, z) — screen-forward (depth), unused for nav labels
- bytes 18–29: camera world position (3 × int32)
- bytes 30–31: zoom (int16)
- bytes 32–37: padding

Axis vectors are int16 fixed-point; divide by 4096 for normalized unit vectors. Walkmesh deltas are 3D `(dx, dy, dz)` but treated as 2D `(dx, dy, 0)` since the floor is essentially flat for nav purposes. Forward projection:
```
screenRight = dx*camRightX + dy*camRightY     // camRight = axis0[0..1]/4096
screenDown  = dx*camDownX  + dy*camDownY      // camDown  = axis1[0..1]/4096
```

Default-camera example from BAT logs (any non-rotated field):
```
[CA] axis0=(4096,0,0) norm=(1.000,0.000,0.000) mag=4096
[CA] axis1=(0,-4096,-1) norm=(0.000,-1.000,-0.000) mag=4096
```
Empirical chase calibration on a rotated field (`domt5_1`) produced `camRight ≈ (0.041, 0.999)` — the chase doc's textbook case. A BAT on the same field should produce a matching `[NAV-PROJ-INIT]` line if the CA parse is correct.

### GPS direction rewrite

`ComputeScreenDirIndex(dx, dy)` in `field_nav_gps.inl` now:
1. Projects the walkmesh delta through `s_camRight/Down` to get screen-space `(sR, sD)`.
2. Computes `atan2(sD, sR)` (yielding angle 0 = east, π/2 = south).
3. Shifts by +π/2 so index 0 = north, and bins into 8 cardinal sectors with a +π/8 half-sector bias.

Cardinal vocabulary `GPS_DIR_NAMES[]` in `field_navigation.cpp` is now `north / northeast / east / southeast / south / southwest / west / northwest`. Per Aaron, cardinals map directly to arrow keys (north = up, east = right, south = down, west = left). The diagonals follow.

### GPS cadence rewrite

Old cadence: time-interval-based, every 3 s / 1.5 s / 0.8 s depending on distance bucket. Produced bursts on long stretches with nothing meaningful changing, and continuous spam in the final approach.

New cadence:
- **In nearby zone** (dist ≤ `s_gpsNearbyDist`): GPS Updates are silent. `Nearby` and `In range` one-shots own messaging in this zone.
- **Outside nearby zone**: Update fires only when cardinal sector changes OR step count changes AND `GPS_ANNOUNCE_INTERVAL_FAR` (3 s) has elapsed. Direction changes break through the minimum-interval floor immediately; step-only changes wait it out to avoid "11 steps… 10 steps… 9 steps…" spam.

New statics in `field_nav_gps.inl`: `s_gpsLastDirIdx` and `s_gpsLastStepsAnn`. Reset by `StartGPS()` / `StopGPS()`.

### Diagnostic logging

- `[NAV-PROJ-INIT] field=X camRight=(a,b) camDown=(c,d) det=N source=ca-file` at every field load. `det = a*d - b*c` (determinant of the 2D projection). For non-degenerate cameras `|det| ≈ 1`. Warning logged if `|det| < 0.1`.
- `[NAV-PROJ] start ...` at every `StartGPS()` (initial announcement).
- `[NAV-PROJ] update ...` at every `UpdateGPS()` announcement. Records player pos, target pos, walkmesh delta, projected screen delta, and the chosen cardinal.

Triage rule for direction bugs: pull the `[NAV-PROJ]` line and verify the math step by step. If the cardinal disagrees with what Aaron observes, the log shows whether the camera axes, the projection, or the binning is at fault.

### Files changed in v0.17.0

- `src/ff8_accessibility.h` — `FF8OPC_VERSION "0.17.0"`
- `src/field_navigation.cpp` — `GPS_DIR_NAMES` array (cardinals) + comment update
- `src/field_nav_fieldscripts.inl` — CA → `s_camRight/Down` wiring after `LoadCameraAxes()`, with `[NAV-PROJ-INIT]` log
- `src/field_nav_gps.inl` — full rewrite of `ComputeScreenDirIndex` (projection through cam axes), new state `s_gpsLastDirIdx` / `s_gpsLastStepsAnn`, sector-change cadence in `UpdateGPS`, `[NAV-PROJ]` diagnostic at start + update

### Not touched, deliberately

- Chase auto-pilot calibration path (`s_calibPending`, `s_camCalibrated`). v0.17.2+ task is to retire the empirical calibration and have chase consume CA-derived axes too — but for v0.17.0 we keep both paths to avoid coupling the BAT signal.
- `FormatNavComponents` (F9 Backspace component readout). It already uses `s_camRight/Down`, so it inherits the fix automatically.
- The other five bugs from the v0.16.5.2 BAT triage (deferred; see Backlog).

### BAT verification recipe

1. Build succeeds. `Logs/build_latest.log` tail clean.
2. Field-load: `[NAV-PROJ-INIT]` line fires once per field. Values match expected per the .ca file dump.
3. Press F9 to cycle to a known entity, press Backspace twice (once to navigate-toward, once to start GPS guidance). Confirm initial cardinal matches the arrow key needed.
4. Walk; confirm direction announces only on sector changes (no spam at constant heading), one final announce on Nearby, one on In range.
5. Test specifically on a field where pre-v0.17.0 direction was inverted (e.g. anywhere on the Fire Cavern approach `bdin3` or any field in `Logs/ff8_field.log` where `dir=` and player-observed direction disagreed).

Direction wrong despite valid `[NAV-PROJ]` log? Inspect axes — possible axis-ordering bug (axis0 vs axis1 swap) or sign flip in the cardinal binning.

---

## Active backlog from v0.16.5.2 BAT triage (5 bugs, deferred)

The Bug 2 fix above is v0.17.0. The other five from the same triage stay deferred until BAT confirms (or refutes) v0.17.0:

1. **FMV STOP/PLAY race** — Quistis infirmary AD fired 22 s before engine resumed FMV playback. Engine STOP/PLAY transitions are visible in `ff8_mod.log`; fix is to pause/resume the AD cue timer on those transitions instead of free-running on wall clock.
2. **POLL tutorial garble** — `[POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."` after `[TUTO]` mode 10→1. Reject `[…]` tokens / unprintable garbage in POLL path, or suppress POLL win[0] for ~500 ms on tutorial-end.
3. **Party member announced as NPC** in 2-member parties on bdin2/bdin3. `party-filter` works on later fields but not earlier ones — likely keys on per-field model index instead of checking formation[] directly by character-ID.
4. **GF-BP diagnostic spam** at every GF cast (350+ `[GF-BP] #50 ACCESS` lines in a fraction of a second). Leftover diagnostic; gate behind `#define GF_BP_AUTOARM_DIAG 0`.
5. **Missing damage announce when GF substitutes for char** (GF-HP-SUB enabled, damage to Shiva's HP not tracked). HP-TRACK doesn't watch the GF HP address while GF-HP-SUB is active; subscribe it during the HP-SUB window.

Followup deferred to v0.17.x line:

- **v0.17.1**: path-aware direction (Bug 2 second half — `dir=up dist=4430` for 20 consecutive updates while player walked, then sudden flip on bdin3 because the bend wasn't represented). Reuse A*+funnel from chase auto-pilot; announce direction to next waypoint not destination. Findings #11 (geometric containment test) and #14 (advance waypoint on no-progress) from chase doc ride along.
- **v0.17.2+**: retire chase auto-pilot's empirical calibration, have it consume CA-derived axes via same `s_camRight/Down`. One projection implementation across manual and automatic nav (Finding #28 from chase doc: parallel implementations cost five wasted BAT cycles in v0.15.9).

---

## Pre-v0.17.0 carry-over backlog

1. **Ifrit / GF audio description miss diagnostic** (from v0.16.4 BAT): if it recurs in any future battle BAT, add 1-second `[GF-EFFECT-POLL] magicId=N prev=M` heartbeat to `PollBattleMagicId` in `src/battle_tts_ewm_gf_effect.inl` to capture engine writes to `0x01D99A68` during GF cast. **v0.16.5 BAT confirmed Ifrit AD fires correctly** so the v0.16.4 miss was intermittent engine timing, not refactor-related. Heartbeat stays parked.
2. **`menu_tts.cpp` T-handler `!shift` gate**. One-line cleanup.
3. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify Fire Cavern A mapping (fieldId 0x0088, engine `fieldName='bdview1'`, expected "Fire Cavern A") end-to-end.
4. **Field-name populate race** at Part B arrival check — diagnostic log only, audio fine.
5. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.

### Future (deferred)

- **Refined-coord persistence** (JSON or %APPDATA% store so BAT-captured coords survive sessions).
- **Other geometric-trigger destinations**: as v0.16.0.2's two-tier cap catches them on first arrival, add refined coords to `Initialize()`.
- **Engine-write hook for cleaner countdown freeze** (cosmetic ±1-s flicker; not urgent).

### Deferred (don't pick without Aaron's direction)

- SeeD rank bug #27
- Walk-and-talk dialog gap
- Refined-coord narrow-gate steering (#29)
- `chase_diag::OnAskOpcodeFired` snprintf bug

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.

---

## Recently shipped (compressed summary)

- **v0.16.5.2** (pushed 2026-05-17 `63dbbfac`): client-side mirror of CI safety checks in `Utilities/push_to_github.ps1` Step 7c. 60 KB warn / 80 KB hard fail mirroring `.github/workflows/safety-checks.yml`. DLL byte-identical to v0.16.5.1.
- **v0.16.5.1** (`7c462392`): 3-line fix in `battle_tts.cpp::Update()` wiring `PollDeferredTurnAnnounce` (latent since v0.13.52 — deferred-turn release path had no caller; stash worked but never released). Verification pattern: watch for `[TURN] Deferred fired after <ms> ms: ...` in future BAT logs.
- **v0.16.5**: pure mechanical split of `battle_tts_menu.inl` (81.89 KB → 1.05 KB slim shell + state/lists/helpers/poll). Largest sub-`.inl` is `_poll.inl` at 55.4 KB. v0.16.5 BAT confirmed clean across two battles.
- **v0.16.0 → v0.16.4**: completed size-split chapter. Pattern: parent `.cpp` (or shell `.inl`) → slim file with `#include` chain of `.inl` files, statics in `*_state.inl` included FIRST, no header guards in `.inl`. Split completion list: world_map, chase_auto_pilot, field_dialog, field_archive_jsm, battle_tts_ewm, battle_tts_menu. Every `src/*.cpp` / `*.inl` now under 80 KB hard fail.

Full narratives in `DEVNOTES_HISTORY.md`.

---

## Catalog of known fieldIds for geometric-trigger destinations

- **Fire Cavern A** (approach field, world-map trigger): `fieldId=0x0088`, engine `fieldName='bdview1'`. Trigger position ≈ (30260, -29221).
- **Balamb Town gate** (planner destination, not geometric): `fieldId=0x006A`, fieldName=`bcgate_1`. Trigger position ≈ (12894, -26776).

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at session start.
- Update both at every version bump AND after every BAT result.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes. The utility refuses if `CHANGELOG.md`'s top heading doesn't match `FF8OPC_VERSION`.
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- F12 reserved for per-session diagnostics.
- **NEVER re-enable SET3 hook (0x1E)** — CI guard.
- DEVNOTES under 10 KB. When this file approaches the limit, move completed-chapter material to `DEVNOTES_HISTORY.md`.
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1).
- **`.inl` textual-include pattern** for source splitting; no `deploy.bat` change needed (only the parent `.cpp` is compiled).
- **Inline-changelog accretion is dead** (retired v0.15.12.0). Canonical changelog is `CHANGELOG.md`.
- **F11 screenshots are gold for BAT context.**
- **Diagnostic-feature gating pattern**: gate behind `#define X 0` instead of deleting.
- **Source file size limits (v0.16.0 CI guard)**: 60 KB soft warning, 80 KB hard fail. Split before substantive edits cross the warning line.
- **Arrival detection needs VERIFICATION, not just signal-presence.**
- **Empirical-data capture (refined coords) needs the underlying decision VALIDATED before storage.**
- **Geometric-trigger vs script-trigger destinations need different navigation strategies.**
- **When "fixing" a planner decline, don't substitute a different region — that's the v0.14.95 mistake.**
- **Mid-drive replan must honor the same planner-eligibility gate as initial Start.**
- **Two-stage destination entry** (Fire Cavern, possibly other major dungeons): the world-map terrain trigger drops the player into an approach field, not the destination interior.
- **GitHub commit history is authoritative for "when did X change" questions.**
- **`ff8_nav_data.log` is the silent goldmine for spatial debugging.**
- **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.**
- **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller (`entityPtr=`) against the actual entity identity.
- **EWM is load-bearing.** Preserve "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Default to pure mechanical splits unless Aaron explicitly approves a refactor.
- **Battle menu TTS is also load-bearing** (v0.16.5). Every command, spell name, GF name, item with qty, target selection, all-target announce, Stock/Cast, cancel-restore is user-facing. Pure mechanical splits only.
- **Navigation direction announcements are screen-relative, not world-relative** (v0.17.0). Cardinals map to arrow keys (north=up, east=right, south=down, west=left). World-space `atan2(dx, dy)` is wrong on rotated cameras — always project through `s_camRightX/Y, s_camDownX/Y` first.
- **One change per BAT cycle.** v0.15.9 chase work taught this the hard way (five wasted cycles chasing W timing when the bug was in a different code path).
- **Verifying user-facing features after a refactor requires comparing against a known-working baseline log.** Absence of an expected log line doesn't automatically mean the refactor broke it — it might be intermittent. If something looks suspicious, look at the install/resolution path first; if that fired, the runtime path is structurally identical.
- Every Claude response starts with `## Claude Says`.

---
