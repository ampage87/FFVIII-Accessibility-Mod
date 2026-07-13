# BAT203 — v0.18.3.203 G-Garden→Dollet grinding loop: refit, replication, G-suite

Sources: `Logs/ff8_world.log` (18:01:26–18:03:19), `offline/ff8_walkmesh.py` over
`wmx.obj`, `offline/nav_sim.py` (BAT203 section), results in
`outputs/ff8/bat203_results.json` (+ parsed log artifacts in `outputs/ff8/bat203/`).
Supersedes the BAT201 fitted collision model where they disagree.

## 1. Incident timeline

| time | event |
|---|---|
| 18:01:32 | Dollet drive starts (-32742,-26874), dist 21221. [TRIGAVOID] holds (zero field entries — fixed) |
| 18:01:40 | 573-cell plan; 18:02:16 re-entry replan → 531 cells |
| 18:02:16–25 | clean drive ~8 km to the .201 neck, westbound on the z=-25024 route line |
| 18:02:25–18:03:01 | **grinding loop**: engine blocks #1–#9 at (-44444..-44499, -24959..-25030), tgt 3024–3432; each fan escape moves exactly ONE 31u step (N↔S ping-pong between z≈-24960 and z≈-24990), "waypoint bearing clear" fires, next block within 0.3–1 s |
| 18:02:34/49 | generic stuck-check replans DURING recovery (each live replan 10+ s) — wasted ~30 s |
| 18:03:01 | fan exhausted → retreat (clean, full speed ENE) → replan (overlay = **1** cell: oracle-veto refused the rest) |
| 18:03:13+ | 476-cell replan ≈ identical; log ends, drive never passed the neck |

## 2. Re-fit of the engine collision rule

### 2.1 Constraints

* **1217 ACCEPTED steps** (672 unique from .203 MFRAME deltas + 545 from .201).
* **53 REJECTED (pos,heading) states** (46 unique from .203 YAWDRIVE `d+0`/stable-yaw
  frames + the 7 frozen .201 headings).

### 2.2 Sweep of every static refinement (falseRejAcc / wronglyAcceptedRej)

| model | FR /1217 | FA /53 |
|---|---|---|
| M0 = BAT201 fit (dest32 + point probe 112, first-containing bit7) | 1 | **32** |
| (a) probe from dest (144u) | 3 | 31 |
| (b) longer probe 160u / 320u | 6 / 17 | 32 / 23 |
| (c) 3-ray width ±64u / ±96u | 30 / 66 | 21 / 17 |
| (d) walkable-preferred selector @112 | 1 | 32 |
| (e) swept 8u pitch 0–112 / 0–144 | **1** | **27** |
| (f) swept + \|dH\|≥200 at probe | 1 | 27 |

**No static gate fits.** The .203 evidence falsifies the BAT201 model: the engine
hard-blocked headings whose 112u point (and entire 512u ray, and ±200u lateral
corridor) are CLEAR — e.g. `(-44491,-24992) yaw=170` is open ≥432u yet rejected —
while ACCEPTING steps whose ray hits a wall at 40–112u (the fan escapes). Same
positions, minutes apart, flip verdicts. **The gate is stateful.**

### 2.3 The mechanism (fits both incidents)

`0x53EB80` find-poly keeps an **8-entry MRU triangle cache** (REQUIREMENTS §1.3)
that is hit-tested in **block-local coordinates** with the **block identity not
verified**. A recently-touched far/mountain triangle whose local footprint contains
the query's block-local point hijacks the answer ("capture"). Verified offline: the
neighbour-block mountain triangle **(block 41,59) poly 30 (b13=29,b15=0x00)**
contains — in local coordinates — the dest points of *every* blocked .203 bearing at
the cluster. Poisoning sources: the engine's per-frame **look-ahead ground query
~112u along the heading** (this is what BAT201's "112u probe" really was) and the
**camera's clamped ground probe** (fanning the aim sweeps the camera boom through
nearby walls — which is why N/NE fan bearings failed while the aim pointed away
from the mountain). Full scans are **walkable-preferred** (REQUIREMENTS §1.3);
that is why the live character could cross the paper-thin sliver and walk the
mountain-skirt "speckle" where overlay markers cover the real floor.

* Why the ORACLE said "clear" where the engine blocked: the oracle is stateless and
  point-samples the stored-order mesh; the engine's answer depends on which
  triangles its cache touched in the previous ~8 queries.
* One live observation to disambiguate: at a wedge, log the validator's candidate
  **poly index + block** (or dump the 8 cache entries). Capture predicts a poly
  whose block ≠ the dest's block (e.g. 41,59#30) and a candH from the wrong plane.

### 2.4 The refit model implemented in nav_sim (`EngineSimC`, gate="cache")

Per frame: Q0 camera probe (boom ≤500u behind the written yaw, clamped at first
terrain hit), Q1 standing, Q2 look-ahead 112u, Q3 validator dest 32u — all through
the walkable-preferred scan + 8-entry MRU capture cache; step accepted iff Q3
walkable ∧ |H(Q3)−H(Q1)| < 200 ∧ Q2 walkable; **hard block, no slide**; plus a
**sticky wall-foot zone** (non-walkable first-containing geometry within 90u of the
character ⇒ stateful wedge) with a deterministic escape hatch (every 23rd rejected
attempt flushes the cache and re-evaluates raw — log-calibrated escape statistics).
Reproduces: the .201 total freeze (0 moves at (-44439,-25238) aim 3033), the .203
grinding cluster, sliver crossings, and speckle walking.

## 3. Replication parity (`R203_replica` = cache engine + v.203 executor on the logged 531 route)

* Sim grinds from **(-44482,-25033) tgt 3026** — 38u from logged block #1
  (-44444,-25030) tgt 3048 — 32 freeze events in the cluster, ~95 % d+0 frames,
  4 near-identical replans, **no arrival** (log: 9+ blocks, 5 replans, no arrival).
* Divergence: the sim creeps slowly west via hatch steps; the live character
  ping-ponged N↔S inside an 80u cluster. Same failure mode, same location, same
  sterile-replan signature.

## 4. Neck verdict: PASSABLE, off the route line

Exact geometry (first-containing bit7, unquantized):

* A **1u-thick non-walkable sliver wall at z = −24936** spans x ∈ [−44530, −44000]
  (gone by −43500). It is invisible to 32u grids and to the 112u point probe
  (and walkable-preferred queries stand ON it), but it anchors the wall-foot zone.
* The corridor's north lane: x=−44490 width **440u** (z −25380..−24940), narrowing
  to **196u at x=−44608** (z −25184..−24988), then widening west (468–492u).
* The .203 route line z=−25024 runs **88u** from the sliver and **36u** from the
  −44608 south wall — inside the sticky zone; that is where it ground.
* **The passable line is the lane centerline: z ≈ −25086 ± 8 through
  x ∈ [−44700, −44540]**, clearance ≈ 98u/side; wider margins elsewhere
  (z −25100..−25200 for x ∈ [−44500, −44300]). Under the refit engine the full
  suite drives it (grinding but progressing); clearance ≥ ~90u is the threshold,
  ≥128u is comfortable.

## 5. Fix suite (all in `nav_sim.py`, switchable; drive_mission3/run_drive3)

* **G1 engine-truth learning**: EVERY engine-block event learns the 128u cell at
  112u (fallback 176/240u if quantization pulls the center within 96u of the
  character) along the blocked bearing — no oracle veto, dedupe, ≤12/leg.
  Learned cells have infinite planner cost, with **safety valves**: if planning
  fails at max margin, prune planner-oracle-walkable learned cells near the
  character, then all of them (a wrong fence must never sever a forced corridor —
  the 9-cell Timber-pass blob made Galbadia Garden a graph island).
* **G2 wall-follow hysteresis + commitment**: exit only after the desired bearing's
  probe stays clear ≥8 consecutive ticks AND ≥64u travelled since the follow began;
  re-block within 2 s of resuming ⇒ resume the SAME side without re-fanning and
  double the commitment (64→128→256→512), then retreat. Fan upgraded to
  **32 absolute bearings (128au pitch)** ordered by deviation from the course —
  the .203 ±1024 relative fan missed 20° exit cones behind the character.
  Freeze detector window 40 ticks (20 false-fires during zone crawls).
* **G3 replan discipline**: recovery replans only when ≥1 NEW cell was learned
  since the last plan (else the fence is inflated in place); recovery replans go
  straight to wide margin (24576→49152 ladder); the generic stuck-check NEVER
  replans while recovery is active; leg ends only on a long-horizon stall
  (route-remaining watermark not improved ≥128u across 1200 ticks).
* **G4 centerline discipline**: when left+right perpendicular clearance (16u-pitch
  probes to 160u, learned cells count as walls) < 2·112+64 = 288u, the waypoint aim
  is offset by half the imbalance (clamp ±96u).
* Planner support: fitted 128u A* + **clearance-penalty ×4** for cells with
  non-walkable geometry within 96u of the center (routes prefer centerlines), and
  **refit lazy validation**: each planned polyline is swept at 8u; failing edges get
  a soft ×6 cost (hard bans sealed forced passes) and the route is re-planned.

### Acceptance results (cache-capture engine, one code version)

| scenario | arrived | replans | frames |
|---|---|---|---|
| R203 replica (.203 executor) | **NO** (grinds at the neck) | 4 sterile | 1,411 (cap) |
| (i) full suite, refit planner | **YES** | **0** | **4,448** |
| (ii) full suite, OLD 112u-point planner gate (robustness) | **YES** | 0 | 3,448 |
| G1 only (no G2/G3/G4, point probe) | YES | 0 | 3,601 |
| no G1 (G2+G3+G4 only) | YES | 0 | 4,448 |

(iii) **Validation matrix 20/20 arrived** (12 Galbadia directed pairs + 6 Balamb +
LG↔SM; Tears' Point excluded as established): all 0 replans; longest
G-Garden→Dollet 4,486 frames (~2.5 min at 30 Hz), Timber↔G-Garden crosses the 190u
pass by zone-crawling (grinding but monotonic).
(iv) No regression: Timber→Dollet 1,508 frames, Dollet→Timber 1,616, both clean.

The suite's key property: **the executor no longer needs the gate to be right.**
With the planner still using the WRONG point-probe gate (ii), G2's committed
wall-follow + G4 centering + G1 fences carry the drive through everything the
gate lied about.

## 6. EXACT v0.18.3.204 recommendations

1. **WorldFootBlockedAt / planner sub-march gate (refit envelope)**: keep
   first-containing byte15-bit7 + |dH|<200 at the 32u dest, and upgrade the 112u
   point probe to a **swept probe: samples every 8u along [8,112]** on the heading
   (first-containing bit7). Same false-rejection rate as the point probe on all
   1,217 walked steps, catches the 1u sliver at z=−24936 and 5 more logged blocks.
   Accept that ~half the .203 rejections are NOT statically predictable (cache
   statefulness) — that is what G1 is for.
2. **G1**: learn a blocked 128u cell on EVERY `[CAMW-REC] engine block` event —
   cell at 112u along the blocked bearing (176/240u fallback to stay ≥96u from the
   character), NO oracle veto, dedupe, ≤12/episode; infinite cost in PlanPathGrid;
   prune-valves as §5 (never let fences make the goal unplannable — prune cells the
   planner itself considers walkable, nearest-first).
3. **G2**: wall-follow exit = desired-bearing probe clear ≥8 consecutive frames AND
   ≥64u travelled since follow start; re-block ≤2 s after resume ⇒ same side, no
   re-fan, commitment ×2 up to 512u, then retreat. Fan = 32 absolute bearings at
   128au pitch ordered by |deviation|, hold 15 frames each, 2 full cycles before
   retreat. Freeze detector: net displacement <8u over 40 frames (not 20).
4. **G3**: recovery replans ONLY with ≥1 new learned cell (else inflate the fence
   ring); recovery replans start at 24576 margin (ladder to 49152); suppress the
   generic stuck-check replan while a CAMW-REC episode is active; end the leg only
   when route-remaining hasn't improved 128u in ~40 s.
5. **G4**: when perpendicular clearance L+R < 288u, offset the waypoint aim by
   (R−L)/2 (clamp ±96u), probes at 16u pitch to 160u including learned cells.
   Through the .203 neck this holds the passable line z≈−25086 at x=−44608.
6. **PlanPathGrid**: add the ×4 clearance cost (walls within 96u of cell center)
   and validate planned routes with the swept gate (soft ×6 edge penalty).
7. **Expected .204 BAT behavior (G-Garden→Dollet)**: one 480-cell plan, no field
   entries, slow visible "nibbling" progress along wall-adjacent stretches
   (zone crawling — expected, not a bug), 0–2 recovery replans, arrival in
   roughly 2.5–5 minutes; if a wedge occurs, at most one fan+wall-follow episode
   per spot, never a >3-replan loop at one location.

## 7. Repro

```
python3 offline/nav_sim.py <wmx.obj> --bat203 --grids <dir> [--budget 35]
        [--route203 outputs/ff8/bat203/route203_531.json]
```
Resumable (incremental JSON + per-mission state files). The refit evidence table
regenerates from `outputs/ff8/bat203/parsed.json` (see `refit_table` in the
results JSON).
