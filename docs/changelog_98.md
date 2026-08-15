## v0.20.98

#80: **the marker was the building; it had to be the door.**

**BAT (v0.20.97).** Aaron: *"Garden landed on the land mass and said it was close
by, but when I got off the Garden and tried to use auto-drive to walk to the
Shumi Village it seemed to run into a wall, and ended up going into the Chocobo
Forest nearby while trying to correct."*

Half of .97 was right. The Garden crossed the world, landed on Winter Island and
parked at **(12952, −81477)** — `parkBit=1 canDisembark=1`, walk 2,572, **zero
replans** — which is the first time any build has put the hull within walking
distance of Shumi Village. **(b) is solved.** The walk is not.

### What the log says

```
[DRIVE] Start -> Shumi Village at (12274,-83958), dist=2525 units (2 km)
[DRIVE] Entered final approach zone (dist=994)
[DRIVE] Hard wedge -> reverse un-wedge burst 1/4 (off=439,  dist=778)
[DRIVE] Hard wedge -> reverse un-wedge burst 2/4 (off=1830, dist=1305)
[DRIVE] Hard wedge -> reverse un-wedge burst 3/4 (off=1903, dist=1901)
[DRIVE] Hard wedge -> reverse un-wedge burst 4/4 (off=1939, dist=2478)
```

It closed to **778 units** and then every un-wedge burst pushed it further out,
until it drifted south-west and tripped the Chocobo Forest's field trigger at
(10729, −80800).

The frames before the wedge name the wall exactly. The walker is at
**(12137,−83179) → (11962,−83245)** — moving *west* while the bearing to target
reads 139 — on tri #503, ground rising −977 → −1013 as it slides. That is a hull
pressed against a face, not a hull that lost its way.

### The dome has one door and it is on the east

The route's own last waypoint was **(11712, −83392)**, 798 units short. The
planner could not do better, because the goal it was given is not walkable
ground at all: **(12274, −83958) is the centre of the building footprint.**

Flood the foot mask from the Garden's own step-off, using the engine's step gate
— the navmesh logs it at load, `66900 engine-gate-blocked connections (>=200
over ~190u)` — and exactly **five cells of the structure come back reachable**:

```
(12736,-83776) (12736,-83904) (12736,-83968) (12736,-84032) (12736,-84288)
                     all height -878, terrain 9, flat snow
```

One 640-unit strip on the **east face**. Everything else around the dome clears
the 200-unit gate and is a wall. And **wmsetus record 20, (13000, −83977), sits
264 units due east of that strip's middle** — you step out of Shumi, cross the
apron, and you are on the world map. The apron *is* the entrance.

So the destination is now **(12736, −83968)**, the middle of the strip, directly
west of the arrival record.

### The berth moves too, for the reason .96 already taught

.97's berth (12672, −81536) parks perfectly well — but it sits **west** of the
dome, so the walk in ran at the blind face. That is the walking version of the
beach: the same destination, open from one side and sealed from the other.

New berth **(13184, −81536)**, walk 2,472 — 512 units east of the one that
already parked cleanly, chosen so the last leg comes up the open apron side.
Sixteen `gexec3` routes arrive with zero replans.

### Two new constraints in the generator, and only one of them gets a vote

`offline/gen_berths.py` is where the berth rules live, so both went there.

**Constraint 5 — the marker must be foot-walkable under the engine's gate — is
real and is the .97 defect.** The generator has always snapped an unwalkable
marker to the nearest walkable cell *silently*; that snap is exactly how a
building footprint became a destination. It now prints, loudly, every time it
fires. It fires on Deling City too (128 units) — worth a look later.

**Constraint 6 — a clear final-approach corridor — is ADVISORY, and the audit is
why.** Applied as a scoring rule it reshuffled seven berths, and when checked
against the *shipped* table it flags **Balamb Town, Timber and Chocobo Forest 2**
— all of which Aaron has arrived at without trouble. A normal town's field
trigger fires well before you touch the building, so a blocked last segment is
harmless there. It is reported, never scored; Shumi's berth was picked with it by
hand, because here the BAT is the evidence that the approach side matters.

This is deliberately the opposite of .90–.95: **a model that disagrees with a
BAT does not get to steer.**

### Verification

* **Parity**: WALK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / PARTIAL / CLEAR all
  identical; PARK differs by 1 (documented tolerance).
* **Berth generator** re-run: 24 of 39 valid, and with constraint 6 advisory the
  other 23 berths are **unchanged from .97** — no churn on ground Aaron has
  already driven.
* **Route regression**: 24 berths × 4 starts × 4 headings, **372 runs, 0
  failures**, nothing skipped.
* `tests/garden_harness.cpp`: **25 ok / 0 bad**, `beach_climb berths in the
  table: 0`, `Shumi plan from Balamb: OK (381 waypoints)`, `Shumi berth to
  Chocobo Forest 2 berth: 1055 units OK (same shore)`.
  `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* Both harnesses clean under `-Wall -Wextra`; every source file inside the 80 KB
  guard.

### BAT (v0.20.98): CONFIRMED

Aaron: *"Finally made it to Shumi Village!"*

```
[GARDEN] parked (arrived) at (13371,-81750) parkBit=1 canDisembark=1
         walk=2307 to Shumi Village, 0 replans
[DRIVE]  Start -> Shumi Village at (12736,-83968), dist=2123 units (2 km)
[DRIVE]  Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x03A5,
         fieldName='tmdome1', target=Shumi Village, dist=171,
         lastPos=(12774,-83801), elapsed=563ms)
[DRIVE]  Stopped: Arrived at Shumi Village.
```

**`fieldName='tmdome1'`.** The game named it. Sixty-seven seconds for the Garden
leg, three for the walk, zero replans on either.

And the trigger geometry closes the .91/.92 loop exactly. The field fired at
**(12774, −83801) — 524 units from the dome centre.** Record 20 is **726** out.
The entry trigger lives between those two numbers, which is why standing on the
arrival point for fifty seconds did nothing and why a walk that crosses the apron
does: **the arrival point is outside the trigger by design, and the apron is
inside it.**

One correction for the record: Shumi's field range starts at **933** (0x3A5,
`tmdome1`), not 934 — the earlier note had the dome one off.

**MSVC-built and BAT'd. This closes #80's Shumi arc, eleven builds after it
opened.**

**Still open, all of the same class and none urgent:**

* **Chocobo Forest 3** (51893, −3959) has no page-8 patch within 3,936 units,
  and there is an unclaimed 32-cell terrain-1 forest patch at **(51968, −64256)**
  with arrival record 35 at (52145, −63615) — the walkthroughs' *"North of Trabia
  Garden"* forest. Pointing CF3 at that record gives berth (53632, −61568), walk
  2,485, corridor clear.
* **Chocobo Forest 4** (97253, −48250) sits by a 47-cell terrain-2 patch, which
  is not the forest signature.
* **Deling City's marker** is not foot-walkable and gets snapped 128 units by the
  generator. It works today; it is the same defect as Shumi's, two orders of
  magnitude smaller.
