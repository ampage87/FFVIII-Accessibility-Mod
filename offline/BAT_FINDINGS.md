# BAT findings log (offline diagnoses)

## .143 BAT (2026-06-27) — height/guard good; two NEW issues isolated

The .143 step-guard worked: `[STEPGUARD]` fired 287x (vs DEEPGUARD 2378x), heights stayed
faithful (`[GROUNDH]` mean |diff| 1.5 over 837 samples). Squall auto-drove **to Dollet and
entered it**, and **drove to Timber**, but (a) would not ENTER Timber, and (b) the
Timber→Galbadia-Garden drive stalled ~7 km out. Both are now isolated:

### 1. Towns not entering = the target coordinate isn't the real entry trigger
- At Timber the character reaches the target **exactly** (YAWDRIVE `dist` 3–10, jittering in
  place with JAM) but **no field loads** — two `Final-approach timeout (no entry)` then
  `Stopped (silent)`.
- Dollet entered because it hit its trigger *en route* at `dist=293` (`mode=1 MODE_FIELD
  fieldId=0x013D 'dogate_2'`), i.e. Dollet's table coord happened to sit ~within reach of
  the real trigger. Timber's table coord (−22564,−4867) does not — the world-map entry
  trigger is elsewhere, so standing on the marker does nothing.
- This is the research-table coordinate caveat (flagged "verify-in-BAT"). The auto-drive
  `[DRIVE] Start` targets the table coord; there is no refined Timber entry.
- **Fix options (next BAT):** (a) on `Final-approach timeout`, run the existing `StartSweep`
  spiral to find the nearby trigger instead of `Stopped (silent)` — generic, needs no per-
  town data; or (b) capture/seed correct entry coords (the mod already `Captured refined
  entry` on success — seed Timber's from the engine trigger zone / a one-time manual entry).
  Recommended: (a) sweep-on-timeout (contained, helps every town whose marker is off).

### 2. Galbadia-Garden stall = the mod's NAVMESH ROUTER plans a physically-impossible route
- The stall point was the character pinned ~(−30480,−24850) pressing toward route
  **waypoint 0 = (−32256,−27136)**, never advancing past leg 0/10, reverse-bursting.
- That waypoint is at height **−1305** and, on the faithful walkmesh, is in a **different,
  disconnected component (35)** from the character and from G-Garden (both component 0).
  The straight line to it crosses a **239-unit cliff** (`validate_route` flag) descending
  into a deep pocket — the engine's 200 step gate makes it unreachable, so STEPGUARD
  (correctly) refuses it and the drive wedges.
- The mod's navmesh A* routed there because its build **fakes connectivity** into deep road
  pockets via two hacks: the `[ROADFLOOR]` clamp (raises road-triangle floors deeper than
  −1000 up to −500, so the gate sees −500 not −1305) and the **road-cell step-gate
  exemption** in `NmEngineStepBlocked` (keeps |dH|≥200 connections when both cells are
  road). Either alone re-connects the −1305 pocket; both must go to cut it.
- A **faithful** router (uniform 200 gate, no road exemption, no floor clamp) finds a clean
  Timber→G-Garden route: **343 waypoints, all in component 0, per-step max dH < 200.** So
  connectivity is fine — only the mod's *router* is wrong.
- A height floor would be WRONG: the faithful route's deepest waypoint is −1246, i.e.
  reachable land also goes deep; the only valid discriminator is the step-gate connectivity.
- **Fix (its own BAT):** retire the two road-connectivity hacks in `Navmesh_Build` /
  `NmEngineStepBlocked` so the router obeys the true gate everywhere. Offline this is safe
  for the Galbadia trio — the trio passes with pure step-gate connectivity and **no hacks**
  (VALIDATION.md §C). Keep the T-junction (step 3) and step-distance (3.5) seam bridges;
  remove only the ROADFLOOR clamp and the `bothRoad` exemption.

## Offline model refinement
Added `validate_route(w, waypoints)` to `ff8_walkmesh.py`: samples each route segment at the
engine's per-frame (40u) scale and flags ocean/no-ground or |dH|≥200 — i.e. it predicts
exactly the executor wedge above (flags the mod's leg 0, passes the faithful route). Use it
to vet any planned route before trusting it, and to regression-test the navmesh after the
hacks are retired.
