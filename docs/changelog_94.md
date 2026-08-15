## v0.20.94

#80: **the beach run was orbiting, and the turning circle is why.**

**BAT (v0.20.93).** The survey steered at the berth correctly this time and still could not converge. The trace is an orbit, and it says so twice:

```
run 1  t=0     goal=1963  berthOff=  53   <- pointed straight at it
       t=1813  goal=1194  berthOff= 932
       t=2813  goal=1502  berthOff=1350
       ...     ends at (1791,-80541), closest 1149

run 2  t=0     goal=1999  berthOff= 182   <- pointed straight at it
       t=2125  goal=1035  berthOff=1133
       t=4297  goal=1897  berthOff=1754
       ...     ends at (1791,-80541), closest 1029
```

**Two independent runs converge on the identical coordinate.** That is an attractor, not an obstruction.

### The geometry was already written down in this file

The Garden turns at most **9 units of heading per frame** and cruises at **32 units per frame** — a turning circle of roughly **1,300 units**. Driving flat out at a target 1,000–2,000 units away means the circle is wider than the approach. It cannot converge; it can only spiral past, which is exactly what `berthOff` blowing from 53 to 932 to 1350 records.

Throttle is now gated on the same forward cone the normal executor uses, with the v0.20.86 pivot escape still applying because a stationary hull cannot always turn. **The probes and the wall-follow stay off** — that is the entire point of the run; only the accelerator is disciplined.

### Three steering iterations, three real defects

Worth stating plainly rather than burying:

| build | defect the previous log exposed |
|---|---|
| .90 | took its bearing from the **planner cursor**, not the berth |
| .93 | fixed the bearing, kept **full throttle** — spiralled |
| .94 | disciplines the throttle |

Each was a genuine bug the log caught, and none of them was the engine. **`mv` has been high in every run** — 189/899 on the last one — so nothing observed so far is a refusal. Closest approach to date: **1,029 units**.

### The split

`world_garden.inl` hit **80,357 bytes**, 1.5 KB from the 81,920 hard fail, with this survey still iterating. Per DEVNOTES, split before the edit that needs the room: the collision probes move to **`world_garden_probe.inl`** (4.4 KB). They read only the grid and depend on no executor state, so nothing else moves. `world_garden.inl` is back to 76,769.

### Verification

* `tests/garden_harness.cpp`: grid 678,223/786,432, **25 ok / 0 bad**, `Shumi beach plan from Balamb: OK (360 waypoints)`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* **Parity** unchanged — no grid bit touched.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
