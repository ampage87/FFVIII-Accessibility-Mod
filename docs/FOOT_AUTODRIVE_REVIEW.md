# The on-foot world-map auto-drive, reviewed against what the Garden system taught us

*2026-08-16. Requested by Aaron: "we haven't reviewed or updated the world map
walking auto drive system since we implemented the B-Garden world map auto drive
system... revisit the whole world map walking auto drive system to see what can
be improved or what errors can be identified based on what we now know."*

Two independent read-throughs: one of the planner and world model, one of the
executor, each with the Garden subsystem beside it as the reference. Everything
below carries a file:line so it can be checked rather than believed.

**The headline is not a tuning gap. It is that large parts of the foot system
are dead code, and some of the parts that look like safety machinery compute a
value and then throw it away.**

---

## A. The five findings worth acting on first

### A1. The line-of-sight clamp and lookahead are computed every frame and discarded

`world_map_drive.inl:762-869` walks the whole route, applies a corner cap, then
walks the aim point back until the straight line is clear — producing `wi`.
Line **879** then does:

```c
wi = s_drivePathIdx + 1;
```

unconditionally, and line **884** steers at `s_drivePathWX[s_drivePathIdx]`
without consulting `wi` at all. **Roughly ninety lines of aim protection run and
are overwritten.**

So the foot executor has no lookahead: it always aims at the current waypoint,
128 units away. The Garden collapses its lookahead from 1536 to 256 *near land*
precisely because a long lookahead cuts corners the engine will not let it climb
(`world_garden.inl:921-946`); the foot system is permanently at the short end,
which means the bearing swings hard every frame as the waypoint is reached.

This is also the single biggest cause of the **camera dither** — and it connects
directly to the v0.21.0 bug. The false "Vehicle detected" verdicts came from
exactly these per-waypoint camera slews. Fixing the aim would reduce the slewing
that manufactured that evidence in the first place.

### A2. There is no dialog pause, and its absence writes permanent damage

The Garden freezes every watchdog while a dialog window is open
(`world_garden.inl:643-660`), added after three BATs wedged in the same spot
during the Lunar Cry cutscene — *"There is a spot on the world map that causes
the Garden to go haywire temporarily. Nida even comments on it."*

`FieldDialog::IsDialogOpen()` is declared in `world_map.cpp:61` and referenced
**only** by the Garden executor. The foot executor never checks it.

On foot the consequence is worse than a wedge, because the recovery is
*persistent*: the freeze detector fires at 0.65 s (`world_map_drive_exec.inl:237-247`),
`CamwLearnBlock` writes a nav block with **no walkability veto**
(`world_map_drive_helpers.inl:213-220`), and that block survives the drive and
the session (`world_map_planner2.inl:8-15`). **Any cutscene, encounter
transition or dialog on the world map permanently fences off real terrain.**

### A3. Learned nav blocks are permanent, unbounded and saturate silently

`s_navBlkX/Y[256]` (`world_map_planner2.inl:19-30`) is a hard reject in the A*
(`:417`) with a ±160 u radius. It has **no expiry, no per-drive reset, no
per-session reset, and no eviction** — once full, `AddNavBlock` returns false
forever, which quietly makes the "no new knowledge → inflate the fence" recovery
branch (`world_map_drive_exec.inl:345-353`) a no-op and every later recovery
replan sterile. The file's own comment already records a nine-cell learned fence
turning a region into a graph island (`:149-153`).

Combined with A2, this is a ratchet: every cutscene tightens it, nothing ever
loosens it.

### A4. The catalog promises destinations the planner cannot route to

The catalog reachability filter runs on the **1024 u fine grid**
(`world_catalog.inl:496-505`), which treats gentle terrain-29 as walkable
(`world_map_geometry.inl:253`) and applies no elevation edge guard. The planner
that actually routes uses `WorldFootBlockedAt`, which rejects **every** terrain-29
polygon (`world_map_navmesh.inl:1039`) and a 200-per-32 u gate.

So a destination behind a mountain pass is offered, accepted, and then
unroutable: the drive burns the 10 s planning budget, five prune attempts and the
384-cell margin retry, then silently drops to straight-line steering.

The Garden system hit this and wrote the rule down — *"The catalog must promise
exactly what the planner can deliver"* — and built `Garden_BerthReachable` so
that **callers cannot get it wrong by forgetting** (`world_garden_plan.inl:93-141`).
The foot system has no equivalent.

### A5. There is no terminal watchdog on a planner-ineligible drive, and the
watchdogs that exist can all be suppressed at once

* W3 (4 s route stall) and W4 (40 s give-up) live inside
  `if (s_drivePathPlanned && s_drivePathLen > 0) { if (s_drivePathWorld) {`
  (`world_map_drive.inl:479-480, 554-614`). Simple-coordinate destinations have
  neither.
* W1 (the generic 3 s stuck check) is **reset every frame** while a CAMW recovery
  episode is running (`:263-265`).
* W3/W4 are suppressed while the firing-area escape is active (`:554-559`), and
  the comment's claim that they "reseed when the escape clears" is not true —
  nothing reseeds `s_rpT`/`s_rpGiveT`.

With an escape active during a recovery episode, **nothing can stop the drive**,
and it will grind with keys held. The Garden's three watchdogs each have their
own timer and none disables another (`world_garden.inl:1179-1196`).

---

## B. Mechanism-by-mechanism gap table

| Garden mechanism | Foot equivalent | Consequence |
|---|---|---|
| Off-grid recovery when the vehicle's own cell is unwalkable (`world_garden.inl:707-745`) | **none** — and G4 centring silently no-ops exactly there: both probes read blocked, `Lc+Rc = 32 < 288`, so `lat = 0` (`exec:384-403`) | on a mesh-fringe cell the fan learns blocks at genuinely walkable cells and poisons the permanent overlay |
| LOS-clamped aim, lookahead 1536→256 near land | computed then discarded (**A1**) | per-frame bearing swing; corner cutting |
| Adaptive probe length in tight corridors (`GdProbesFor`, `world_garden.inl:576-584`) | **none** — `SweptFootBlocked` fixed 8→112 u (`planner2.inl:101`) | Garden's own note: a probe longer than the corridor reads blocked on every heading. In a ~200 u corridor the wall-follow exit test `clearRun >= 8` can never be satisfied — grind until the 4.8 s timeout, every time |
| Wall-follow: side selection + limit-cycle brake flipping after 3 fruitless engagements | commitment distance only (64→512 u); **no side selection, no brake** — the follow bearing is whatever the fan escaped on (`exec:294, 317`) | nothing notices the guard re-engaging with no progress; the fan can keep choosing the same wrong lane |
| Pivot-deadlock breaker (rotation applies only as part of a move) | not needed on the foot law (UP is always held) but **missing on the vehicle law**, which has a pure-pivot band with no throttle (`exec:847-856`) | vehicle drives can reproduce Garden's .84/.85 deadlock verbatim; only the 40 s give-up breaks it |
| Guard-drift watchdog (`GD_GUARD_MAX_DRIFT = 1000`) | **none** | a wall-follow can run 4.8 s directly away from the goal with nothing measuring it |
| Stall timer re-arms after every throttle interruption | **none** | |
| Near-goal policy: inside 3000 u, release the guard but **keep the route** | **none** — recovery is distance-blind. Retreat pops up to 10 breadcrumbs (640 u backwards) regardless of proximity (`exec:305, 333`) | Garden's .81 failure verbatim: reach the doorstep, trip a recovery, get walked back out |
| Dialog pause freezing all watchdogs | **none** (**A2**) | |
| Validated berths: offline-generated, four constraints, 2–3 km standoff, same-landmass check (`world_garden_berths.inl:30-68`) | raw catalog coordinate, ring-snapped to any cell with ground within 16 cells (`planner2.inl:272-281`) — no same-component test, no final-approach line test | the planner can snap the goal onto an island across a strait and route to it |

---

## C. World-model and planner defects

Ranked by likelihood of causing a real failure.

1. **`PlanPathGridM` — the planner that actually runs — has no torus wrap at
   all.** The bbox is clamped to `[0,GCOLS-1]×[0,GROWS-1]`, and neither `inB`
   nor the neighbour loop wraps (`planner2.inl:249-257, 388-389`). A route that
   must cross the seam is impossible; if start and goal straddle it, the bbox
   expands to the full 2048×1536 map (3.1 M cells) and the search goes the long
   way until it hits the expansion cap or the 10 s clock. **Every other flood in
   the codebase wraps both axes.** The Garden system lost a whole build to
   exactly this (its clearance chamfer missed the row wrap; twelve polar samples
   read 27, 22, 17, 12, 6, 2, 0, 3, 11 — *"Those numbers ARE the row index"*,
   `world_garden_grid.inl:643-662`).

2. **Silent truncation of the emitted path.** `planner2.inl:502`:
   `for (i = rc-1; i >= 0 && n < DRIVE_PATH_MAX; i--)` — a route longer than 768
   waypoints is cut off mid-route, marked planned, with no decimation and no log
   line. `PlanPathFine` stride-samples correctly (`planner.inl:718-729`); the
   live planner does not. The 384-cell margin ladder exists to permit ~49 km
   detours, which is precisely the case that overflows.

3. **The A* heuristic is inadmissible and inconsistent.** `planner2.inl:318`:
   Manhattan × 128 on an 8-connected grid whose diagonal costs 181. A diagonal
   drops `h` by 256 while costing 181. Two consequences: paths up to ~41% longer
   than optimal, and — with the road discount making `h` overestimate true cost
   by >3× — A* goes greedy and **defeats the road preference the design depends
   on**. Re-expansions also count against the expansion cap, so the cap can abort
   a search that had a legal answer.

4. **Two step gates, both wrong, in opposite directions.**
   `WM_CLIMB_STEP = 400` is applied between **1024 u** cells
   (`planner.inl:667`) where the engine permits 200 per 32 u — i.e. up to 6400 —
   and its own comment concedes it is *"TUNABLE and not yet calibrated"*
   (`world_map_state.inl:891-898`). Meanwhile `EDGE_GATE = 200` in the live
   planner is documented as ~8× too permissive against 1927 logged moves, and was
   left that way (`planner2.inl:224-240`). The Garden derived both of its gates
   from the engine and wrote the derivation down (`world_garden_grid.inl:99-141`).

5. **RouteNet applies the 200 gate between 128 u cell centres with no sub-march**
   (`world_map_routenet.inl:466`) — the same 4× error the Garden diagnosed and
   corrected. RouteNet is tried **first** (`planner2.inl:542`).

6. **`eaAvoid` penalises the destination's own trigger area for 20 of 27 catalog
   entries.** `FindEntryAim` returns −1 for any destination outside the 7-entry
   table, so every firing area — including one the route may need to cross — is
   charged 4096 per cell (`planner2.inl:349-357, 458`), enough to detour ~16 km
   around a 4-cell box. The penalty ignores story and vehicle gates, so a trigger
   that *cannot fire* is still avoided.

7. **Dead code carrying live gates.** `PlanPath` (the coarse 32×24 A*,
   `planner.inl:310`) is never called, but its `MatchProgramForCatalog` goal-set
   machinery still runs and still gates the fallback chain
   (`planner2.inl:566-591`), including a `SEGMENT_DISTANCE_CAP = 5` that declines
   outright if no active region is within 40,960 u.

8. **Hand-tuned single-location hacks inside the shared world model.**
   `world_map_segments.inl:885-886` writes two hardcoded map cells inside
   `#if NAVMESH_DIAG` — **the terrain model changes with a diagnostic flag**.
   `:1017-1032` force-overrides every road cell map-wide to flat land to carve
   one corridor open. All are marked load-bearing.

9. **Silent heap drops** in both `PlanPathFine` and `PlanPath`
   (`planner.inl:596, 344`) — a full heap returns silently, the node vanishes,
   the flood dead-ends, and the pocket detector papers over it.

---

## D. Executor defects beyond the gaps

* **`s_drivePathWorld` is never reset between drives** — not in `StartAutoDrive`'s
  ineligible branch (`drive_helpers.inl:706-709`), not in `StopAutoDrive`
  (`:251-253`) — and it is also written by the **Garden** executor
  (`world_garden_plan.inl:295, 453`). It gates the reverse un-wedge and the whole
  W3/W4 block, so **whether a foot drive has route watchdogs can depend on what
  the previous drive did.**
* **Ten function-local statics survive across drives**, including
  `s_orbitAng` — so a second sweep starts at the previous sweep's residual angle
  (`drive.inl:155-157`) — and `s_rpStallIdx`/`s_rpStallN`, which can trip a
  waypoint skip on the first stall of a new drive (`:585`).
* **Two watchdogs are computed and never consumed.** `wallJam`
  (`drive.inl:910-915`) appears only inside log format strings; `hardWedge`
  (`:938-950`) is read only by the vehicle branch.
* **The uncapped final-approach teleport** (`drive.inl:325-343`) writes the
  character onto the target with no attempt counter and no walkability check on
  the destination.
* **ENTRYMOW destroys the planned route irrecoverably** (`:238-241`) — if the mow
  fails and the sweep aborts back to "normal steering", the executor follows the
  mow box, not the route.
* **Silent camera-read fallbacks**: `GetWorldMapCameraYaw()` returns −1 on fault
  and the sweep coerces that to `base = 0` (`drive.inl:180`), rotating the entire
  key basis by an unknown amount rather than refusing to steer.
* **Diagnostics are shipped on**: `DRIVE_STEER_DIAG = true` at 50 ms — its own
  comment says *"set false before push"* — and `WM_MOTION_DIAG` emits one
  **unthrottled** `[MFRAME]` line per poll tick (`world_map_state.inl:527-528, 38`).
  `Log::World` flushes synchronously, so that is 120–180 formatted file writes a
  second on the poll thread for the whole of every drive.
* **Stale comments misstating the hardware**: `drive.inl:891-893` and
  `exec:173-174` both claim `WM_HEADING` is `0x0203ED02`; it is `0x0203FE52`,
  and `0x0203ED02` is the camera yaw.

---

## E. The validation gap, which is why none of this was caught

`tests/world_map_harness.cpp` compiles **only** `world_map_geometry.inl`
(line 54) and asserts a coordinate mapping, a 3×3 synthetic island flood and a
synthetic slope gate. **Nothing in `world_map_planner.inl`,
`world_map_planner2.inl`, `world_map_routenet.inl` or `world_map_segments.inl`
is exercised at all.** The live planner, its cost function, its heuristic, its
caps and its path emission have zero offline coverage.

Against that, the Garden has: a harness that feeds the real `wmx.obj` through the
real rasteriser and asserts berth reachability and plan quality; `parity_check.py`
comparing the C++ grid dump cell-for-cell against the offline model; `replay.py`
checking the model against real `[GDTRACE]` lines from actual play; and a 372-run
route matrix at 0 failures.

And the Garden's own hard-won lesson about why that matters
(`offline/replay.py:6-13`):

> *"Every rule in this system was derived by me, encoded in BOTH the C++ and the
> simulator, and then 'validated' by running the simulator — which embeds the
> same derived rule and therefore can never falsify it."*

---

## F. Suggested order of work

Cheap and safe, no behaviour risk:

1. Turn the shipped diagnostics off (D: `DRIVE_STEER_DIAG`, `WM_MOTION_DIAG`).
2. Delete the ~400 lines of unreachable steering code and the dead `PlanPath`
   chain, or gate them behind a flag that is off — right now they make the file
   read as though it has protections it does not have.
3. Fix the stale hardware comments.

Small, high-value, individually testable:

4. **A1** — use `wi`. One line, and it restores the lookahead the design intends.
5. **A2** — add the dialog pause. One call, copied from the Garden.
6. **A3** — give nav blocks a per-drive or per-session reset and an eviction
   policy.
7. **D** — reset `s_drivePathWorld` and the ten drifting statics in
   `StartAutoDrive`.

Needs design and a harness first:

8. **C1** — torus wrap in `PlanPathGridM`.
9. **C2** — decimate rather than truncate.
10. **C3** — an admissible heuristic (octile, wrap-aware), which the Garden
    already has (`world_garden_plan.inl:326-332`).
11. **A4** — one reachability predicate shared by the catalog and the planner.
12. **A5** — watchdogs that cannot suppress each other, plus a guard-drift bound
    and a near-goal keep-the-route rule.

And before any of 8–12, the thing that makes them checkable:

13. **A foot equivalent of `garden_harness` + `parity_check`** — a host test that
    compiles the real planner against the real `wmx.obj` and asserts a route
    matrix. Every Garden defect above was found by that apparatus rather than by
    a BAT.
