## v0.20.86

#80: **a stationary Garden cannot always turn, so withholding the throttle in order to turn can deadlock.**

**BAT (v0.20.85).** Centra Ruins, Winhill and Trabia Garden, all three arrived, `parkBit=1 canDisembark=1` on each.

### Replay came back clean — for the first time

```
replayed 309 [GDTRACE] samples

cell     309   0   100.00%
cls      309   0   100.00%
clear    309   0   100.00%
blk      309   0   100.00%
aim      309   0   100.00%

REPLAY OK: at every logged position the model computed what the game computed.
```

That is the first fully clean conformance run in the project. The v0.20.84 beach model and the v0.20.85 clearance fix are both confirmed against the engine rather than against a simulator that assumed them.

The polar fix shows up directly in the trace. On the Winhill → Trabia crossing:

| cell row | 765 | 4 | 10 | 15 | 20 | 26 | 31 | 36 |
|---|---|---|---|---|---|---|---|---|
| game `clear` | 32 | 32 | 32 | 32 | 32 | 32 | 32 | 32 |

In .84 those same rows reported their own row index. The seam is open ocean again.

### The one thing left, and it has now happened twice in the same place

The Trabia approach wedged at **(60976, −44518)** in .84 and **(60993, −44530)** in .85 — seventeen units apart. Fourteen seconds of identical trace both times:

```
pos=(60993,-44530) hd=3616 cell=(557,750) clear=2 steer=(61056,-44928)
off=582 blk=0 aim=1 guard=0/0/0 rev=0 mv=0/57 gate=-1038 cls=0x1F keys=--R-
```

Everything in that line matters:

* `off=582` against a `GD_STEER_FWD_CONE` of **576**. Six units outside the cone, so the executor pressed **RIGHT with no throttle** — `keys=--R-`.
* **`hd=3616` never changed.** Not once in fourteen seconds of holding RIGHT.
* `mv=0/57` — the engine moved the hull on zero of fifty-seven frames.
* `blk=0 aim=1 guard=0/0/0` — nothing was in the way and no wall-follow was running.

**The engine applies rotation as part of a move.** When the candidate step is refused the hull neither travels *nor turns*, so `off` never changes, so the throttle is never restored: turn → no move → no turn. Only the 15-second route-progress watchdog broke it, and it broke it twice.

The cone is the right rule for *steering* and the wrong rule as an absolute *veto*. With the bow clear, creeping forward while turning is exactly what the wall-follow already does to get around a headland — so after `GD_PIVOT_MS` (1,500 ms) of going nowhere the throttle comes back on. A healthy pivot finishes well inside that: 582 units of heading at the measured 9 u/frame is about 1.1 s.

### Why no offline run could ever have found this

`gexec3` turns the hull whether or not the move succeeds:

```python
if off > STEER_DEADZONE:
    hd = (hd + ...) & 0xFFF     # unconditional
```

The engine does not. **The loop that produced this wedge cannot close in the model**, which is why 356-run matrices kept coming back clean while the same 17-unit patch of the Trabia approach wedged in two consecutive BATs. The escape is mirrored so both sides carry the same behaviour, and the rotation coupling itself is now written down in `gexec3` as a known gap rather than left as an accident — this class of wedge stays invisible offline until it is modelled.

### Verification

* **Parity**: WALK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / CLEAR all identical between the C++ and `gsim3`; PARK differs by 1 (documented tolerance).
* **Replay** against the .85 BAT: **309/309 on every check.**
* **Route regression**: 23 berths × 4 starts × 4 headings, **356 runs, 0 failures**.
* `tests/garden_harness.cpp`: grid 678,223/786,432, reachability 662,681, **24 ok / 0 bad**. `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* Both harnesses clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
