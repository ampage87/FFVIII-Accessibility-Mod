# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

Older entries (pre-v0.15.12.0) are preserved in `CHANGELOG_HISTORY.md`.

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
