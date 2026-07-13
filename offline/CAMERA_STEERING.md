# World-map camera + steering model (offline findings, .146/.147)

## The big finding: movement is HEADING-controlled (tank-style); the camera is cosmetic

Disassembled from `FF8_EN.exe`:
- The character walks along its **heading** `0x0203FE52` (0=North, full circle=4096):
  `dX = sin(h*2pi/4096)*spd`, `dY = cos(h*2pi/4096)*spd` (PSX RotMatrix Y-axis; cos/sin at
  `0x56D130`/`0x56D100`, scale `2pi/4096` at `0xB6B980`, magnitude 4096.0 at `0xB69540`).
  Move-vector builders: `0x53D8A0` / `0x53DA20` (read heading, add to player X/Y at
  `0x203EE80/84`). LEFT/RIGHT add to the heading (turn); UP walks forward along it.
- The **camera yaw** `0x0203ED02` only **lerps toward the heading** each frame
  (`0x558592`+: `yaw += clamp(wrap(heading - yaw), +-0x80) >> 3`). It is purely visual.
- **The mod has been steering off the wrong global.** `GetWorldMapHeading()` reads the
  **camera yaw** `0x203ED02`, which LAGS the true heading by up to ~66 deg right after a
  region/warp (the Galbadia exit). That lag is the "press UP, character goes ~90 deg off /
  X-flipped" wedge from brief 5b -- the steering's reference angle is the lagging camera, not
  the actual move heading.

## The fix direction (for the mod)
For a **blind** auto-drive the camera is irrelevant -- only the character's movement matters.
So **control the heading directly**: each frame set `0x0203FE52` = bearing to the next
waypoint and hold UP. The character walks straight at the waypoint; the camera lerps to
follow (cosmetic). To snap the camera too, also write `0x0203ED02` = the same value once
(mimics the warp routines `0x548155/0x54815c`). Writing heading persists (the turn logic
only adds to it on L/R input, which the auto-drive won't press). This removes the
camera-lag dependency entirely and makes steering deterministic. Confirm the heading->move
convention has no constant offset by a one-frame closed-loop check (observe actual dPos vs
written heading); the disasm suggests the convention matches the mod's `TorusBearing`.

## Offline simulator now models this (`ff8_walkmesh.py`)
- `bearing(ax,ay,bx,by)`, `camera_lerp(yaw,heading)` -- the heading/camera transform.
- `navigate(w, route, ...)` -- simulates a heading-controlled point following a route at the
  engine's ~40u/frame scale, applying the real `|dH|<200` step gate, advancing waypoints.
  This faithfully reproduces the in-game behavior (it reproduced the exact wedges).

## What navigation simulation revealed (all three trios, both directions)
Plan with a **fine 40u-sub-march edge check** (not just 128u cell endpoints) + a routing
safety margin (gate ~160), and **follow waypoints tightly** (aim at the next waypoint,
advance only when reached -- NOT the mod's 2400u lookahead, which cuts corners into cliffs):
- **Esthar trio: all 6 directions ARRIVE.**
- **Most Galbadia/Balamb routes ARRIVE** (e.g. Timber<->Galbadia Garden, ...->Balamb town).
- A few still graze **marginal ~210u barriers** (e.g. Balamb Garden<->Fire Cavern, some
  Timber<->Dollet samples). These are right at the 200 gate: the grid planner cuts
  cross-country through marginal terrain, whereas the reliable corridor is the **road** (the
  old navmesh/road planner followed it -- which is why Dollet/Timber worked in .143).

## Required planner/executor changes (next implementation)
1. **Steering:** replace the camera-yaw 8-way sector steering with **direct heading control**
   (write `0x0203FE52` = bearing to waypoint; press UP). Read heading, not camera yaw.
2. **Executor:** follow waypoints tightly (small lookahead ~1 cell), don't cut corners with
   the 2400u lookahead.
3. **Planner:** validate edges at the engine's ~40u per-frame scale with a small safety
   margin (gate ~150-160), and **prefer road cells** (`s_roadFine`, lower cost) so routes
   follow the genuinely-reliable corridors instead of marginal cross-country terrain.
With (1)-(3), the offline sim navigates the trios; the few remaining marginal spots are
expected to clear once routing prefers roads.
