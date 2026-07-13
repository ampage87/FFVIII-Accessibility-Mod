# BAT201 — v0.18.3.201 G-Garden→Dollet freeze: offline root-cause, fitted engine model, fix suite

Sources: `Logs/ff8_world.log` (21:41:10–21:44:30), `offline/ff8_walkmesh.py` oracle over
`wmx.obj`, `offline/nav_sim.py` (BAT201 section), results in
`outputs/ff8/bat201_results.json`. All simulation runs are offline; no live game involved.

## 1. Incident timeline (from the log)

| time | event |
|---|---|
| 21:42:45 | Dollet drive starts from ~(-36928,-25920), dist 25184 |
| 21:42:51 | GRID plan: 530 fine cells, cell(735,565)→(901,459) — heads WEST (horseshoe; goalDist legitimately rises to 32110) |
| 21:42:52–21:43:34 | encounter interrupts (world-map exit/re-entry), replan → 527 cells |
| 21:43:40–48 | drive west along gz=-25536/-25152; goalDist no-progress skip fires every ~1.5 s, cursor races +11 idx/1.5 s (5→16→27→37→48→59…75) |
| 21:43:48 | character FREEZES at (-44439,-25238): d+0 every frame, UP held, aims 3033/3043 (and later 2831/2941/2942/3012/3049), mh converged, **zero** movement for 886 frames. No DOWN (reverse-burst) frame anywhere in the log. |
| 21:44:04/16/27 | stuck→replan from cell(676,570): three IDENTICAL 472-cell plans (sterile loop) until manual cancel |

## 2. Fitted engine collision model

### 2.1 Why every 32u dest-attribute model fails

The four candidate models (A: first-containing bit7, B: walkable-preferred bit7,
C: terrain byte13∉{32,33,34}, D: step-gate only) were tested at 32u step scale against
545 observed walked steps (all drives) and the frozen state. **All four accept the frozen
step**: the 32u destination at aim 3033 lands on a poly with `b13=16, b15=0xd6` — the same
attribute tuple walked 109× elsewhere in the same drive. The freeze cannot be explained by
any property of the 32u destination.

### 2.2 The lookahead probe (the fitted model)

Marching west from the frozen point: walkable p69 (`16/0xd6`) ends ~40u out; a walkable
strip p100/p101 (`b13=27, b15=0xf0`, bit7=1) spans 44–92u; **mountain p9
(`b13=29, b15=0x20`, bit7=0) starts at 96u**. Sweeping probe distance × blocked-term over
all 545 walked steps (0 false rejections required) and all frozen headings (all must
reject) yields a unique fit:

> **A step along heading h is accepted iff**
> `dest = pos + dir(h)·32` has ground, its FIRST-CONTAINING poly has **byte15 bit7 = 1**, and |H(dest)−H(pos)| < 200, **and**
> `probe = pos + dir(h)·112` has ground and its FIRST-CONTAINING poly has **bit7 = 1**.
> **Probe failure is a HARD block — no wall slide.**

* Probe-distance window from the data: **[101, 126] u** (112 canonical). D≤100 fails to
  reject one frozen heading; D≥127 falsely rejects a walked step.
* The height gate never binds in this incident (max |dH| 17u at dest, 62u at probe).
* Blocked term: `bit7==0` and `byte13∈{29,32,33,34}` are indistinguishable on this data —
  globally they coincide for 99.6 % of polys (370,068 of 371,490 bit7==0 polys are
  mountain 29 or ocean 32/33/34). bit7 is the per-poly flag the validator reads
  (REQUIREMENTS §1.5); use it.
* "No slide" is itself an observation: at the freeze, walkable ground existed 32u ahead
  and to both sides, yet d+0 — the lookahead rejection bypasses the slide path entirely.
* This **supersedes** the NAV_SIM_FINDINGS claim that terrain-29 is walkable ground: the
  engine refuses to move when a bit7==0 poly is within ~112u ahead. (The old claim was
  fitted at 32u dest scale, where it *is* true that you never *stand* on 29.)

### 2.3 Replication parity

* **Open loop** (logged CAMW `wrote` sequence fed into the fitted engine from the 21:43:40
  anchor at (-37270,-25904)): freezes at **(-44435,-25231) — 8.1u** from the logged freeze
  point, with hard-blocked d+0 frames. 237 ticks replayed.
* **Closed loop** (v.201 executor replica: 192u pursuit advance + goalDist no-progress
  skip): skip cadence **+11 idx / 1.5 s — exactly** the logged 5→16→27→37→48→59 stride;
  engine freeze occurs (position differs — the real character's ~80u north drift from
  camera-trim error is not modelled); stuck→replan produces **identical consecutive
  replans with zero movement** (sim: 124-cell twins; log: 472-cell triplets).
* Divergence: the offline legacy planner has no road-preference layer, so replan route
  *shapes* differ from the mod's; the failure mode is identical.

## 3. Blocking geometry at the neck

The route runs an E-W corridor at gz≈-25100, **~190–250u wide**, between two bit7==0
mountain ranges. The corridor itself IS passable — dead-center. The racing cursor made the
character beeline to skipped-ahead waypoints; the pursuit line grazed the north wall's
foot (86u north of the route line). From there every west-ish heading has mountain within
112u → hard freeze, no slide, and the mod had no reverse/perpendicular recovery.

**The planner was not wrong about walkability at 32u**: the whole 530-cell route passes a
plain bit7@32u sub-march. It was wrong about **clearance**: under the fitted 112u probe the
first rejected edge is **idx 85: (-47808,-25152)→(-47936,-25024)** (diagonal turn probing
into the north wall). The freeze happened earlier (near idx 59) only because the cursor
race pulled the character off the polyline.

## 4. Reachability under the fitted model

| group | verdict |
|---|---|
| Galbadia (Timber, Dollet, G-Garden, G-Station) | **Mutually reachable** — 12/12 directed pairs arrive. |
| G-Garden→Dollet corridor | **West horseshoe is forced** (no NE/coastal path). Fitted route (553 cells): west through the gz≈-25100 corridor center → around the massif at (-52864,-19328) → south loop to z≈-6400 → east at x≈-37376 → NE across the plain to Dollet. Requires planner bbox **margin ≈ 20k+** (the mod's 12288 is too small — use a margin ladder). |
| Balamb (B-Garden, Fire Cavern, Balamb Town) | **Mutually reachable** — 6/6. Fire Cavern sits flush against the mountain face: needs **goal-relaxed planning** + the 400u arrive radius; west approach only. |
| Esthar | LG↔SM arrive. **Tears' Point is genuinely UNREACHABLE on foot**: its pocket (2,348 cells, x[77440,86272] z[29568,37888]) is disconnected from the Lunar-Gate component (27,836 cells). The only link is a zigzag 1–2-cell gap at ≈(83800,29700); a 32u, 16-heading, engine-faithful BFS cannot cross it (the 112u probe always hits mountain). This is a product-level finding, not an executor bug. Recommend one live BAT to confirm, and vehicle/alternate handling for Tears' Point. |

## 5. Fix suite (implemented in nav_sim.py, all switchable)

* **F1 — planner gate** (`Planner2(mode="fitted")`): 128u A*, **directed** edges
  (the gate is anisotropic), 32u sub-march with first-containing-bit7 + |dH|<200 **plus
  the 112u probe along the edge direction from every sub-point**; goal-relaxation
  (nearest reachable cell within 8 cells); margin ladder; learned-overlay support.
* **F2 — watchdog** (`run_drive(f2=True)`): goalDist skip **deleted**. Route progress
  `remaining = distToWp + 128·(len−1−idx)`; no ≥64u improvement in ~4 s → BLOCKED
  (hand to F3 / end leg for replan). The cursor is never skipped past unreached waypoints.
* **F3 — engine-block recovery** (`run_drive(f3=True)`): UP held with net displacement
  <8u over 20 ticks → bearing fan-out ±256/512/768/1024 (hold ≤15 ticks each); on escape,
  **wall-follow** the fan bearing until the waypoint bearing's own probe passes; if the fan
  exhausts (or >8 freezes/leg), retreat ≤10 breadcrumbs (64u spacing, stall-guarded — the
  trail is NOT guaranteed re-walkable under an anisotropic gate) and record the **probe
  cell** (the obstacle, never the step destination, and only if its cell center is
  non-walkable) into a LEARNED ENGINE-BLOCKED overlay; replans give learned cells infinite
  cost → each replan is genuinely different.

### Results (G-Garden→Dollet, fitted engine)

| scenario | planner | executor | arrived | replans | frames |
|---|---|---|---|---|---|
| S0 baseline v.201 | legacy | skip watchdog | **NO** | 26 | 5,580 |
| S1 F1 only | fitted | skip watchdog | **NO** (cursor race still corner-cuts) | 26 | 5,940 |
| S2 F2 only | legacy | F2 | **NO** (sterile replans, no learning) | 101 | 12,315 |
| S3 F3 only | legacy | skip + F3 | **NO** (race defeats recovery) | 301 | 51,741 |
| **S4 F2+F3 (no F1)** | legacy (wrong gate) | F2+F3 | **YES** | 163 | 81,274 |
| **S5 F1+F2+F3** | fitted | F2+F3 | **YES** | **0** | **2,688** |

**S4 is the robustness/scalability proof**: with the planner still using the wrong v.201
gate, the executor alone learns 100+ obstacle cells and arrives. **S5 is the product
path**: one plan, zero recoveries.

### 24-pair validation matrix (F1+F2+F3): **20/24 arrived**

All 12 Galbadia + all 6 Balamb + LG↔SM. The 4 failures are exactly the Tears' Point pairs
(genuine unreachability, §4). No other pair needed more than 2 replans (G-Station→Timber:
a ~190u mountain pass at (-41100,-6400) needed two learned-overlay replans).

## 6. Recommendations for the mod (v0.18.3.202)

1. **PlanPathGrid gate**: first-containing **byte15 bit7** at 32u sub-march **plus a 112u
   bit7 probe along the edge direction from every sub-point** (directed edges). The mod's
   navmesh export therefore needs the per-triangle byte15-bit7 flag (terrain byte13 alone
   is equivalent in practice — bit7==0 ≈ {29,32,33,34} — but bit7 is the engine's flag).
2. **Delete the goalDist no-progress cursor skip.** goalDist legitimately rises on
   horseshoe routes; the skip converts a correct plan into corner-cutting suicide. Replace
   with the F2 route-progress watchdog (distToWp + 128·cellsLeft, ≥64u/4 s).
3. **Reduce foot pursuit advance radius 192→64** (keep the height gate). 192 permits ~90u
   corner cuts — fatal in ~200u corridors under a 112u lookahead engine.
4. **Add F3**: freeze detector (<8u net over 20 ticks with UP held) → camera-write fan-out
   ±256/512/768/1024 with wall-follow until the desired bearing's probe clears → breadcrumb
   retreat (stall-guarded) → learned engine-blocked overlay (probe cell, non-walkable
   centers only) consulted by every replan. This also kills the sterile replan loop.
5. **Margin ladder** for PlanPathGrid (12288 → 24576 → 49152): the correct G-Garden→Dollet
   route needs ~20k margin.
6. **Goal relaxation**: if the goal cell is unreachable, path to the nearest reachable
   cell within ~1km and rely on the 400u arrival radius (Fire Cavern pattern).
7. **Tears' Point**: exclude from on-foot validation or route via vehicle; verify live.

## 7. Repro / rerun

```
python3 offline/nav_sim.py <wmx.obj> --bat201 --grids <dir> [--budget 35]
```
Resumable (incremental JSON + per-mission state files). Grids are optional but ~100×
faster; they rebuild from wmx.obj automatically (resumable with --budget).
