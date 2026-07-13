# Camera-transform navigation simulator — validation results (2026-07-01)

Simulator: `offline/nav_sim.py` (pure stdlib, imports `ff8_walkmesh`).
Engine model implements CAMERA_EXE_ANALYSIS.md §8 verbatim: heading snap
(<=0x100) / turn (0x200/frame), spd=32 u/frame, camera velocity ramp +-8,
decay x3/4, half-damp near 180 deg, clamp +-0x80, `cy += cv >> 3`; per-triangle
bias hook (`bias//2`, 0 by default); region camera-lock mode (+-0x20/frame
toward forced yaw, snap within 0x20). Step gate: dest walkable + `|dH| < 200`;
slide approximated by projecting onto h+-0x100/0x200/0x300 (nearest first,
magnitude `spd*cos(offset)`; +-0x400 has zero projection).
Planner mirrors PlanPathGrid: 128u-cell A*, 8-neighbour, every edge
sub-marched at 32u against the oracle, bbox = endpoints + margin.
Results JSON: `outputs/ff8/sim_results.json` (sandbox).

## Validation matrix — executor (b) camera-write: **24/24 arrived**

Every route passed on the FIRST ladder config (routing gate 200, margin 6144);
the 160-gate / larger-margin / start-at-wp0 escalations were never needed.
arrive = within 400u of target. maxdh = max |dH| over accepted steps.

| Route | wps | plan ms | frames | arrived | slides | blocked | recov | b13-vs-b15 disagree |
|---|---|---|---|---|---|---|---|---|
| Timber -> Dollet | 273 | 2886 | 1192 | yes | 4 | 0 | 0 | 244 |
| Timber -> Galbadia Garden | 169 | 1242 | 840 | yes | 3 | 0 | 0 | 26 |
| Timber -> Galbadia Station | 158 | 64 | 806 | yes | 1 | 0 | 0 | 32 |
| Dollet -> Timber | 273 | 841 | 1481 | yes | 69 | 174 | 9 | 346 |
| Dollet -> Galbadia Garden | 173 | 1818 | 833 | yes | 0 | 0 | 0 | 67 |
| Dollet -> Galbadia Station | 180 | 264 | 880 | yes | 0 | 0 | 0 | 59 |
| Galbadia Garden -> Timber | 169 | 297 | 839 | yes | 0 | 0 | 0 | 23 |
| Galbadia Garden -> Dollet | 173 | 183 | 832 | yes | 0 | 0 | 0 | 105 |
| Galbadia Garden -> Galbadia Station | 13 | 13 | 44 | yes | 0 | 0 | 0 | 0 |
| Galbadia Station -> Timber | 158 | 43 | 806 | yes | 0 | 0 | 0 | 24 |
| Galbadia Station -> Dollet | 180 | 89 | 878 | yes | 0 | 0 | 0 | 112 |
| Galbadia Station -> Galbadia Garden | 13 | 0 | 44 | yes | 0 | 0 | 0 | 0 |
| Balamb Garden -> Fire Cavern | 47 | 88 | 177 | yes | 1 | 0 | 0 | 0 |
| Balamb Garden -> Balamb Town | 90 | 134 | 373 | yes | 0 | 0 | 0 | 0 |
| Fire Cavern -> Balamb Garden | 47 | 25 | 179 | yes | 3 | 0 | 0 | 0 |
| Fire Cavern -> Balamb Town | 135 | 446 | 551 | yes | 0 | 0 | 0 | 0 |
| Balamb Town -> Balamb Garden | 90 | 60 | 374 | yes | 0 | 0 | 0 | 0 |
| Balamb Town -> Fire Cavern | 135 | 80 | 551 | yes | 0 | 0 | 0 | 0 |
| Lunar Gate -> Sorceress Memorial | 53 | 45 | 242 | yes | 0 | 0 | 0 | 0 |
| Lunar Gate -> Tears' Point | 190 | 1311 | 801 | yes | 1 | 0 | 0 | 144 |
| Sorceress Memorial -> Lunar Gate | 53 | 42 | 239 | yes | 0 | 0 | 0 | 0 |
| Sorceress Memorial -> Tears' Point | 187 | 1020 | 803 | yes | 1 | 0 | 0 | 206 |
| Tears' Point -> Lunar Gate | 190 | 5 | 800 | yes | 0 | 0 | 0 | 20 |
| Tears' Point -> Sorceress Memorial | 187 | 607 | 804 | yes | 0 | 0 | 0 | 186 |

No wedges; max |dH| stayed < 200 everywhere (largest 198.9 on Dollet->Timber).

## Robustness — executor (b): 12/12 arrived

Run on Galbadia Garden->Dollet, Balamb Garden->Fire Cavern, Lunar
Gate->Tears' Point. All four adverse conditions arrived on all three routes
with frame counts within ~1% of nominal:

| Case | GG->Dollet | BG->Fire Cavern | LG->Tears' Point |
|---|---|---|---|
| initial cy AND h 180 deg wrong | 831 fr | 176 fr | 798 fr |
| region lock, forced yaw 3556 | 842 fr, 4 slides | 177 fr | 803 fr |
| bias = +64 (compensated write) | 832 fr | 177 fr | 801 fr |
| bias = -64 (compensated write) | 832 fr | 177 fr | 801 fr |

The camera-write driver rewrites cy every frame, so the lock controller's
+-0x20/frame pull produces at most a 2.8 deg transient — invisible in
practice. The 180-deg-wrong start costs only the <=4-frame engine turn.

## Executor (a) current-.200 vs (b) camera-write — live wedge conditions

Start (-37475,-26232), camera yaw AND heading forced to 3556 (the live
"anomaly" values):

| Route | exec | arrived | frames | turn frames | slides | wedge |
|---|---|---|---|---|---|---|
| GG -> Dollet | (a) 8-way keys | yes | 902 | **741 (82%)** | 3 | no |
| GG -> Dollet | (b) camera-write | yes | 833 | 18 (2%) | 3 | no |
| GG -> Timber | (a) 8-way keys | yes | 906 | **699 (77%)** | 1 | no |
| GG -> Timber | (b) camera-write | yes | 841 | 26 (3%) | 1 | no |

With the faithful camera physics, executor (a) does NOT hard-wedge on these
routes (the .200 PORD staircase + true register read escape it), but it spends
~80% of all frames inside the 0x200/frame turn loop — the heading
perpetually chases `camYaw + k*512` while the camera itself lags at
<=16 au/frame, which is exactly the ".152/.153 spin" signature and costs ~8%
travel time even in the good case. Executor (b) eliminates the loop entirely
(heading converges in <=4 frames and stays), and is also immune to the camera
lock/decay states that froze cy at 3556 live.

## Fixes that were REQUIRED to reach 24/24 (feed these into the mod)

The engine model itself passed nowhere near 24/24 at first; two executor
changes (not planner changes) were needed:

1. **Height-aware waypoint advance.** A 2D radius test can mark a waypoint
   "reached" while the character stands ~200u BELOW it: on Timber->Dollet the
   validated climb ran along the polyline edge (wp142->wp143, a legal 196u
   ramp) with a cliff flanking it; the character clipped the 48u radius of
   wp143 while still at the cliff base, retargeted wp144, and every heading
   within 90 deg was gate-blocked (slide cannot help; permanent wedge —
   reproduced the live "advanced then wedged" jam exactly). Advance now
   requires `|charH - wpH| < 100` in addition to the 48u radius. This ONE
   change took the matrix from wedging to 24/24.
2. **Breadcrumb recovery.** The walkability + |dH| gates are symmetric, so the
   character's own trail is always re-walkable. If no movement occurs for 20
   consecutive frames (or distance-to-waypoint stalls for 150), retrace
   breadcrumbs (dropped every 16u) until a direct 32u probe toward the current
   waypoint passes the gate, then resume. Needed 9 times on Dollet->Timber
   (69 slide frames along a canyon wall); zero times elsewhere. This is the
   sim equivalent of — and a cleaner replacement for — the mod's empirical
   unstick sweep.

No planner adjustment was needed: routing gate 200 + margin 6144 sufficed for
all 24 once the executor fixes were in (the 160-gate safety ladder remains
implemented and available).

## byte13 (terrain) vs byte15-bit7 walkability — they DO disagree

The step gate was evaluated under BOTH rules every candidate step. On 16 of
24 routes the rules disagreed at least once (up to 346 evaluations on
Dollet->Timber; all Balamb routes 0). Movement ground truth here is the
terrain-byte rule (consistent with routing and with the mod's navmesh, per
NAV_SIM_FINDINGS), and under it all routes complete. CAVEAT for the mod: the
engine's live validator checks bit7, so on the disagreeing cells (mountain-type
ground) the real game may block steps this sim allows. The mod's existing
OBAD/empirical fallback (0x0203FE30 disagreement detection) is the right
mitigation; routes through byte13-only ground should expect occasional live
slides the sim scores as clean.

## Known divergences from the mod / engine

- No road preference: the mod's `s_roadFine` cost layer has no offline data
  source; sim routes may cross open ground where the mod would prefer roads.
- Wall slide is an angular projection approximation (h +- 0x100/0x200/0x300),
  not the true edge-tangent projection of validator 0x53E7A0.
- Oracle lookups are quantized to an 8u grid (cache); heights are float vs the
  engine's sar-8 fixed point (known <6u disparity from VALIDATION.md).
- Speed fixed at the 32u validator step; live on-foot speed may differ by mode
  table — affects frame counts linearly, not outcomes.

## Files

- `offline/nav_sim.py` — engine model + planner + both executors;
  `python3 nav_sim.py <wmx.obj> --out results.json` runs everything
  (resumable: completed entries in an existing JSON are skipped).
- `outputs/ff8/sim_results.json` (sandbox) — full machine-readable results.
