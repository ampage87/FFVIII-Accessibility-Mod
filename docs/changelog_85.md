## v0.20.85

#80: **the clearance field never wrapped at the pole — and it took a completely clean BAT to find it.**

**BAT (v0.20.84).** *"That worked great! It successfully drove to all the locations I tried… arrived at 2-3km from each destination."*

### The run

Nine destinations, nine arrivals, two replans in the whole session:

| destination | walk | replans |
|---|---|---|
| Balamb Town | 2,366 | 0 |
| Fire Cavern | 2,998 | 1 |
| Centra Ruins | 2,887 | 0 |
| Winhill | 2,637 | 0 |
| Trabia Garden | 2,886 | 1 |
| Timber | 2,929 | 0 |
| Edea's House | 2,666 | 0 |
| Tomb of the Unknown King | 2,049 | 0 |
| Fisherman's Horizon | docked | 0 |

`parkBit=1 canDisembark=1` on every one, every walk between 2.0 and 3.0 km, **zero `plan FAILED`**, and reachability **662,681 on all twelve floods** — including cell **(528,338)**, the Tomb, which read 655,108 and sat outside the pocket in .83. The catalog held at **24 destinations at every location**, so Fire Cavern and Balamb Town stayed visible after the hull left Balamb island. Both replans were recovery machinery working as designed: Fire Cavern's guard drift abort fired at 1,063 units (`GD_GUARD_MAX_DRIFT` is 1,000) and re-planned in 91 expansions.

### What replay found in it

Against those 590 `[GDTRACE]` samples:

```
cell     590   0   100.00%
cls      590   0   100.00%
blk      590   0   100.00%
aim      590   0   100.00%
clear    578  12    97.97%   <-- DIVERGENCE
```

**`cls` at 590/590 is the result that matters** — the shelf-based beach model of v0.20.84 is now confirmed against the engine on a real drive, bit for bit, rather than against a simulator that assumed it.

The twelve `clear` divergences are all on the polar crossing from Winhill to Trabia Garden, and the numbers give it away immediately:

| cell row | 27 | 22 | 17 | 12 | 6 | 2 | 0 | 764 | 756 | 749 | 741 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| game `clear` | 27 | 22 | 17 | 12 | 6 | 2 | 0 | 3 | 11 | 18 | 26 |

**The clearance value IS the row index.** v0.20.69 made the world a torus in both axes — edges, flood, heuristic, A\* and the line probe all take rows mod `GD_ROWS` — and missed the clearance chamfer, which went on treating row 0 and row 767 as solid wall. The hull was being told its distance to the array boundary over completely open ocean.

**The cost was silent and precisely backwards.** Below `GD_TIGHT_CLEAR` the executor switches to short bow probes and the planner adds `GD_TIGHT_PENALTY` per cell, so a 32-cell band of empty sea at the pole was priced and driven as a narrow channel — on the one route where crossing the pole is the entire point. **59,456 cells** had the wrong clearance; **12,288** of them were below the planner's clear target and are not, and **4,096** were below the tight threshold and are not.

Rows now wrap. A chamfer converges in two passes only on an open grid — on a torus the seam has to be re-propagated — so the forward/backward pair repeats until nothing changes. It settles in three or four iterations, once, at load.

### Two model errors that were cancelling each other out

`gsim3` had the *opposite* bug: scipy's `distance_transform_cdt` treats the array edge as background, so the offline model called the polar band open while the C++ called it walled. Neither side was guarded, and nothing offline could see the disagreement — it took a real drive over the pole and the replay harness to separate them. `parity_check.py` now compares `CLEAR` too, and the harness dumps `gcheck/gdclear.bin` so it can.

### Also

The version string still read `0.20.83` in the .84 build. Bumped, with the note.

### Verification

* **Parity**: WALK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / **CLEAR** all identical between the C++ and `gsim3`; PARK differs by 1 (the documented "seen" tolerance). CLEAR was previously unguarded and now agrees on all 786,432 cells.
* **Replay** against the .84 BAT: cell, cls, blk, aim **590/590 exact**. The 12 `clear` samples still differ because that log came from the build with the un-wrapped chamfer — the next BAT is what closes them.
* **Route regression**: 23 berths × 4 starts × 4 headings, **356 runs, 0 failures** — the clearance change alters planner costs across 59,456 cells, so this was the check that mattered.
* `tests/garden_harness.cpp`: grid 678,223/786,432, 74,184 parkable, reachability 662,681, **24 ok / 0 bad**. `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* Both harnesses clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
