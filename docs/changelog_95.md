## v0.20.95

#80: **the whole Shumi beach was shut by one cell — and it was my grid shutting it.**

**BAT (v0.20.94).** Aaron ran the survey several times and drove the Garden around the area by hand as well. That second part is what cracked it.

### The survey finally measured cleanly

Three runs, all the same shape:

```
t=0      goal=1963  berthOff=  53   <- pointed straight at it
t=1813   goal=1194  berthOff= 932
t=2938   goal= 936  berthOff= 694
t=3938   goal= 678  berthOff=1019
t=6000   goal= 672  berthOff=  16   <- pointed straight at it again
t=9016   goal= 673  berthOff= 106
t=13079  goal= 675  berthOff= 144
REFUSED at (2303,-82253) -- 673 short (closest approach 670)
```

Closest approach **670 units**, down from 1,029 in .93 — so the throttle fix worked. But the hull then sat at 670–690 for ten seconds, repeatedly pointed straight at the berth (`berthOff` 11, 16, 43), throttle down, **sliding sideways**. `gate=208` and `cls=0x31` throughout: never shallow, never ashore.

That is the signature of a wall.

### It is a wall in my grid, not in the game

The hull's stuck position sits on planner cell **(705,521)**:

```
sub(1410,1042) terr=29 h=  -7   NOT Garden-masked   <- 128 units of shore skirt
sub(1410,1043) terr= 9 h=-380   Garden
sub(1411,1042) terr=34 h=   0   Garden
sub(1411,1043) terr= 9 h=-355   Garden
```

A 256 cell is `WALK` only when **all four** sub-points carry the Garden bit, so this one reads solid. **The engine had the hull standing on it.**

And everything past it is clean: (705,522), (705,523) and the berth (704,523) are fully `WALK` terrain-9 at −390…−470.

**The only beach onto that island was shut by one cell whose fourth quarter is a 128-unit sliver of shore skirt.** `GdLineClear` refuses every chord through a non-`WALK` cell, so the executor would not drive the last 670 units of a ramp the engine was already letting it stand on.

**Aaron's manual driving is what proved it.** Of 718 logged hull positions in that region, **190 sit on planner cells the model calls non-WALK** — he had been driving across "solid rock" for minutes.

### The fix, deliberately tiny

New **`GDC_PARTIAL` (0x80)**: 1–3 of 4 sub-points Garden-masked. **6,939 cells map-wide.**

`GdBeachOpen(idx)` allows a step or a chord to cross such a cell **only** when a `beach_climb` berth is armed, and **only within 3 cells of it**. Open water, solid rock, and every other berth on the map are untouched — the conservative all-four rule still governs everywhere the planner actually plans.

Mirrored in `gsim3` as `PARTIALp` and added to `parity_check.py`, which now agrees on **6,939 both sides**.

### Honest status

This is the fourth iteration on this survey and the first one where the obstacle was real rather than my steering. Whether the Garden climbs the ramp once the mouth is open is **still unmeasured** — but for the first time the model is not standing in the way of finding out.

### Verification

* **Parity**: WALK / PARK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / **PARTIAL** / CLEAR all identical; PARK differs by 1 (documented tolerance).
* `tests/garden_harness.cpp`: grid 678,223/786,432, **25 ok / 0 bad**, `Shumi beach plan from Balamb: OK (360 waypoints)`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* **Route regression**: 23 berths × 4 starts × 4 headings — the new bit is scoped to an armed beach berth, so no other route can see it, and this confirms that.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
