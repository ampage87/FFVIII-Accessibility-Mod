# Balamb Garden auto-drive — offline analysis (#80)

Session 2026-08-12. Primary sources only: `FF8_EN.exe` (disassembled with capstone)
and the raw `wmx.obj` walkmesh extracted from `world.fs`. Research documents were
not used as evidence.

Tooling persisted in `offline/`: `garden_grid.py`, `garden_park.py`,
`garden_sim.py`, `garden_exec.py`, `garden_matrix.py`.

---

## 1. The engine's per-vehicle walkability rule

The mod already knew that on-foot walkability is `wmx` polygon `byte15 & 0x80`,
but not where that came from or what the other bits meant. Disassembling
`FF8_EN.exe` produced the whole table from one small function.

`0x53E6B0` — `int IsPolyWalkable(uint32 typeTriple, int16 vehicleId)`:

```
typeTriple = poly[15]<<16 | poly[14]<<8 | poly[13]
if ([0xC75CF4] == 0) return 1                    ; collision globally disabled
if veh in 0x00..0x09 or veh == 0x80  -> (triple>>16) & 0x80   ; FOOT
if veh in 0x20..0x28 or veh == 0x84  -> (triple>>16) & 0x40   ; CAR
if veh == 0x30                       -> (triple>>16) & 0x20   ; GARDEN
if veh == 0x31                       -> (triple>>16) & 0x10   ; CHOCOBO
otherwise                            -> 1                     ; Ragnarok flies
```

The identical test is inlined in the step validator `0x53E7A0` (the Garden arm at
`0x53E92E`: `shr eax,0x10 / and eax,0x20` under `cmp si,0x30`). `0x30` is the same
constant the per-vehicle dispatcher at `0x546307-0x5463A2` compares the engine
vehicle id (`[0x020409E0]`) against for the Garden, so the two agree.

A sibling function `0x53E730`, reached only through `0x54B860`, applies a
*different* bit set:

```
if veh in 0x20..0x28 or veh == 0x84  -> (triple>>16) & 0x04   ; CAR
if veh == 0x30                       -> (triple>>16) & 0x02   ; GARDEN
if veh == 0x31                       -> (triple>>16) & 0x01   ; CHOCOBO
if veh == 0x32                       -> (triple>>8)  & 0x80   ; RAGNAROK
```

`0x54B860` gates on `|destH - curH| < 0xC8` and then requires BOTH this test for
the passed vehicle AND for the currently-ridden one. Note that Ragnarok is
*absent* from the walkability function (it flies) but *present* here — which is
the tell: this second set is **where the vehicle may be set down**, not where it
may move.

### Corroboration from the mesh itself

Tabulating both bit sets over all 473,193 polygons in `wmx.obj`, by terrain type
(`byte13`), reproduces the game's rules without being told them:

| mask | ocean 32/33/34 | mountain 29 | forest 0–5 | plains 7/6/9/14 |
|---|---|---|---|---|
| `0x80` foot | 0 % | 0 % | 100 % | 100 % |
| `0x40` car | 0 % | 0 % | **0–1.1 %** | 97–100 % |
| `0x20` **Garden** | **99.4 / 87.4 / 99.9 %** | **13.2 %** | 36–94 % | 10–89 % |
| `0x10` chocobo | 0–99 % | 0.4 % | 100 % | 92–100 % |
| `byte14 0x80` Ragnarok-land | **0 %** | **0 %** | **0 %** | **97–100 %** |

Three independent confirmations fall out:

* **car cannot enter forest** — a rule the mod had already established from live
  BATs, recovered here purely from the bit table.
* **the Garden sails and is stopped by mountains** — it is walkable on ~99 % of
  deep ocean and on only 13 % of terrain 29.
* **the Ragnarok lands only on open plains** — zero on ocean, mountain and
  forest, ~99 % on plains. Exactly an airship's landing rule, and the reason for
  reading the second bit set as "set down here".

Every "set down" bit is a near-perfect subset of foot-walkable (Garden 99.9 %,
car 100.0 %, chocobo 100.0 %, Ragnarok 100.0 %), which is what "the player ends
up standing here" requires.

**Step gate.** `|candH - curH| >= 200` rejects a move (`0x53E7A0:0x53E9C2`,
`0x54B860:0x54B87A`). It is not vehicle-dependent — the Garden obeys the same
200-unit rule as a walking character.

**Speed.** The 8-direction probe at `0x53E7A0:0x53E84D` picks its march distance
per vehicle: foot `0x20`, car and Garden `0x40`, Ragnarok `0x100`. The Garden
moves at roughly twice walking pace.

---

## 2. Where the Garden can go

A grid was rasterized from `wmx.obj` at 128 units per cell (the finest useful
resolution), storing the first-containing polygon's masks and interpolated
height, then flood-filled 8-connected under the Garden mask plus the 200-unit
step gate.

* Garden-traversable cells: **87.2 %** of the world.
* Reachable in one component from Balamb Garden's berth: **98.3 %** of those.

The important result is what is *not* in that component. Cross-referencing each
of the 39 catalog destinations against (a) its own foot-walkable landmass and
(b) the Garden's reachable set gives **23 reachable, 16 not**:

**Cannot be reached (16).** Fisherman's Horizon, Great Salt Lake, Esthar City,
Lunatic Pandora Lab, Lunar Gate, Sorceress Memorial, Shumi Village, Tears' Point,
Chocobo Forest 3, Chocobo Forest 4, Alien Ship 1 — every one of these is on the
Esthar continent or its bridge, which is ringed by mountains the Garden cannot
cross. Plus Edea's House, Cactuar Island, Island Closest to Hell, Island Closest
to Heaven — islands with no Garden-parkable ground — and the Deep Sea Research
Center, which has no foot-walkable ground on the world map at all.

This is the engine's own encoding of "you need the Ragnarok for these", derived
rather than assumed, and it matches the game.

**Grid resolution.** The reachable-destination set was computed at 128, 256 and
512 units per cell. 128 and 256 agree exactly (23/39). 512 loses Galbadia Garden,
Galbadia Station and Chocobo Forest 5 by sealing the passes that reach them. So
**256 is the coarsest faithful resolution**, and that is what ships — 2.3 MB
instead of 9.4 MB.

---

## 3. Park points

Only ONE trigger program in `wmsetus` Section 8 carries a Garden clause
(program 20, locID `0x0172`, region `0x0C`). **No catalog destination can be
entered by driving the Garden into it.** Park-and-walk is therefore forced by the
engine, not chosen.

A park point is the cell nearest the destination that is simultaneously:

1. Garden-traversable and inside the hull's reachable component,
2. Garden-parkable (`byte15 & 0x02` — the disembark bit), and
3. on the **same foot-walkable landmass** as the destination, so the existing
   on-foot auto-drive can finish the trip.

It must also be a valid cell on the conservative 256-unit planner grid, or the
planner would return a route that ends somewhere the hull cannot stop.

| destination | park point | walk |
|---|---|---|
| Balamb Garden | (24704, −29312) | 0 |
| Balamb Town | (13952, −26752) | 768 |
| Fire Cavern | (30592, −29056) | 362 |
| Dollet | (−19072, −40320) | 3415 |
| Timber | (−23168, −4480) | 724 |
| Galbadia Garden | (−34432, −21632) | 4529 |
| Galbadia Station | (−34944, −21120) | 4891 |
| Deling City | (−62080, −28544) | 256 |
| Tomb of the Unknown King | (−43136, −36992) | 923 |
| D-District Prison | (−55424, −4480) | 256 |
| Galbadia Missile Base | (−71808, −15488) | 0 |
| Winhill | (−49536, 6272) | 768 |
| Trabia Garden | (48512, −57984) | 256 |
| White SeeD Ship | (4992, 51328) | 0 |
| Centra Ruins | (7296, 54656) | 724 |
| Chocobo Forest 1 | (11392, −62848) | 768 |
| Chocobo Forest 2 | (10624, −81536) | 572 |
| Chocobo Forest 5 | (14208, 22912) | 3238 |
| Chocobo Forest 6 | (45184, 70784) | 5431 |
| Chocobo Forest 7 | (−20352, 69248) | 572 |
| Alien Ship 2 | (44160, 65920) | 11820 |
| Alien Ship 3 | (−13440, −10112) | 512 |
| Alien Ship 4 | (−48768, 5760) | 0 |

The long walks are real, not defects: Galbadia Garden and Galbadia Station sit
inland behind ground the hull cannot hover over, and Alien Ship 2 is most of a
continent from the nearest anchorage. The distance is announced so the player
knows before committing.

---

## 4. Planner

8-neighbour A* on the 256-unit grid, step-gated at 200, with one extra term:
a **clearance penalty**. Clearance is the Chebyshev cell distance to the nearest
non-traversable cell; edges into cells with less than 7 cells (1792 units) of
clearance are surcharged 0.55 per missing cell. It is waived within 10 cells of
either endpoint, because a park point is on a coast by definition and a hull that
starts in a tight inlet has to be allowed out.

That single term is what made the system work. A hull with a turning radius of
roughly 1300 units cannot follow a route that hugs a 256-unit-resolution
coastline; before the clearance term the validation matrix was mostly wedged,
after it most routes complete with zero replans and zero reverses.

Measured planning cost, real grid, `-O2`: **10–33 ms** for routes of 66–187 km.

---

## 5. Executor

The vehicle steering law the mod already uses for the car — deadzone 320,
forward cone 576, pivot outside the cone — plus four behaviours, each added to
kill a wedge mode reproduced in simulation:

1. **Line-of-sight-clamped lookahead.** The aim point is the furthest waypoint
   inside the lookahead arc whose straight line from the hull is Garden-clear.
   Without it the lookahead cuts corners across headlands into the coast.
2. **Wall-follow guard.** When the bow probe is blocked the drive commits to the
   side whose heading fan clears first, keeps turning that way, and **creeps
   forward on every frame the bow happens to be clear**. The current rule in
   `world_map_drive_exec.inl` turns toward the target side and never moves, which
   limit-cycles at ±1 turn step against a wall — that is the `moveDist=0` for
   18 s with 11 identical re-paths signature reported as issue **#100 / H2**.
3. **Bug2 leave condition.** The wall is left when the aim is line-of-sight clear
   again **or** simply when the hull is measurably closer to the goal than it was
   where it hit. Without the second clause a wall-follow orbits an island
   forever, because in cluttered terrain the aim is almost never LOS-clear. This
   was the last remaining wedge mode in the matrix.
4. **Stall → reverse → replan, with escalating clearance.** Stall frames are
   counted only where forward motion was *wanted and denied*; pivoting and
   wall-turning are legitimate no-motion frames. After two failed replans the
   clearance target rises to 12 cells, after five to 18 — trading route length
   for a corridor the hull can turn in.

---

## 6. Validation

Two independent implementations were compared cell for cell. The C++
`Garden_RasterizeTri` that ships in the mod was fed the real `wmx.obj` through
the same polygon loop `world_map_segments.inl` uses, and its grid diffed against
the Python model written separately from the same spec:

```
WALK  cpp=677934  py=677934  disagree=0
PARK  cpp=65035   py=65035   disagree=0
```

Zero disagreements, which validates the mesh-space transform (`mz = 196608 − vwy`)
that the whole system rests on. All 23 park points are reachable from Balamb
Garden's berth in the shipping C++ flood fill.

The executor was then simulated against the **128-unit** grid — finer than the
planner's own — so the simulation genuinely tests whether a coarse route can be
followed through real geometry, from 8 start positions spread across the map and
under 4 speed/turn-rate combinations spanning turning radii from roughly 650 to
2600 units (the Garden's true rate is unknown; the sweep is the honest way to
cover it).

Results are in `offline/GARDEN_MATRIX.md`.

---

## 7. Open questions for the BAT

1. **Is `[0x020409E0] == 0x30` true exactly while piloting the Garden?** This is
   the single gate for the whole subsystem. It is the constant the engine's own
   dispatcher uses, and the mod already trusts this address vehicle-positively
   for steering, but it has never been observed reading `0x30` live. Every
   transition is logged as `[GARDEN] aboard`.
2. **The Garden's real speed and turn rate.** The probe table says it marches at
   `0x40` per step against foot's `0x20`; the turn rate is not known. The
   simulation covers a 4× spread, but the live numbers would let the lookahead
   and arrival radius be tuned rather than bracketed.
3. **Does the hull actually stop where it is told?** `GD_ARRIVE_DIST` is 384
   units. If the Garden coasts, the park point may be overshot onto ground where
   the player cannot disembark; the arrival log records whether the final cell
   carried the park bit.
4. **The `int32` savemap fix.** `[VEHPOS32]` logs the new reading alongside what
   the old `int16` reading would have produced. On the Galbadia continent they
   will differ by tens of thousands of units; that line is the confirmation.
