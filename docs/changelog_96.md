## v0.20.96

#80: **the approach side was the whole problem, and the screenshots show it.**

**BAT (v0.20.95).** Aaron: *"Still failed to get on land near the Shumi Village... I also took multiple F11 screenshots."*

The screenshots are the evidence. They show **Balamb Garden sitting on the snow in a cove, with a cliff wall behind it** -- right up against the shore. His logged position there was **(3150, -81780)**: planner cell (703,524), reachable water, and **338 units from the berth.**

The automated survey, running straight at the berth with every model check off, sticks at **(2294, -82228)** -- cell (705,521), **572 units out on the south-west side**.

**Same berth. Two sides. Only one of them is open.**

### The cell map says exactly why

```
        519    520    521    522    523    524    525    526
 703  Ww.R.  ..P..  .....  .....  .....  Ww.R.# Ww.R.  Ww.R.
 704  Ww.R.  Ww.R.  ..P..  W....  W....* ..P..  Ww.R.  Ww.R.
 705  Ww.R.  Ww.R.  ..P..  W....  W....  ..P..  Ww.R.  Ww.R.
 706  Ww.R.  Ww.R.  ..P..  W....  W....  W....  ..P..  Ww.R.

 W=walk  w=water  P=partial  R=reachable    * berth    # where Aaron got to
```

The wedge -- (704...707, 522...524) -- is **walkable land the flood cannot reach**, ringed **entirely** by PARTIAL cells. The one mouth from reachable water is **(704,524), entered from (703,524) to its north** -- precisely where Aaron ended up by hand.

Everything the planner did approached from the wrong quarter. **That is why .90 through .95 kept measuring a wall: it is only a wall from one direction.** Four builds of survey iteration were measuring the closed side.

### The fix

`dock_x/dock_y` -- unused by anything but a `drive_in` until now -- carry the **approach point** for a `beach_climb` berth:

* `Garden_Plan` routes to **(3200, -81792)**, the water cell the mouth opens from, instead of at the berth.
* The beach push arms on proximity to **that** (`GD_BEACH_ARM_DIST` = 500) rather than on raw distance to the berth, so it starts from the open side.
* It then has **362 units** to cover instead of 572 through a closed one.

### Honest status

I have been iterating a survey against the wrong approach for four builds. Each fix in .90/.93/.94/.95 was a real defect -- bearing from the planner cursor, full throttle, the one-cell grid wall -- but none of them was going to work while the hull kept arriving on the sealed side. **The thing that broke the loop was a photograph**, and it took ten seconds to read. When a spatial question resists four rounds of instrumentation, ask for a picture.

### Also

`offline/regress84.py` now **skips `beach_climb` berths and says so out loud** -- `gexec3` has no `GdBeachOpen` and no approach-point routing, so such a berth is unrepresentable offline, and a silent drop would read as "everything passes".

### Verification

* `tests/garden_harness.cpp`: **25 ok / 0 bad**, `Shumi beach plan from Balamb: OK (360 waypoints)`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* **Parity**: all fields identical including PARTIAL and CLEAR; PARK differs by 1 (documented).
* **Route regression**: 23 berths (Shumi skipped, loudly) x 4 starts x 4 headings.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
