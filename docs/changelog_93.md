## v0.20.93

#80: **the .92 beach run said REFUSED, and it was not entitled to.**

**BAT (v0.20.92).** The survey fired twice at Shumi. Both runs ended `REFUSED`. Both readings are wrong, and the trace says so in the first column:

```
t=0ms     pos=(3182,-84004) goal=1970 hd=2071 mv=23/45   gate=208 afloat
t=1516ms  pos=(3328,-83275) goal=1286 hd=1337 mv=46/128  gate=208 afloat
t=4453ms  pos=(3438,-82372) goal= 591 hd=2261 mv=78/294  gate=208 afloat
t=5453ms  pos=(3176,-81527) goal= 570 hd=2541 mv=92/351  gate=208 afloat
t=7891ms  pos=(3332,-81224) goal= 911 hd=1329 mv=110/489 gate=208 afloat
t=8891ms  pos=(5015,-81102) goal=2277 hd=865  mv=140/547 gate=208 afloat
REFUSED at (5259,-81178) after 9016 ms -- 2473 units short
```

**`mv` is 23/45, 46/128, 78/294, 92/351, 110/489, 140/547.** The hull moved on roughly half of every frame for the whole nine seconds. **The engine refused nothing.** Meanwhile `hd` swung 2071 → 1337 → 2261 → 2541 → 1329 → 865, and the distance went 1970 → 1286 → **591** → 570 → 911 → 2277.

It closed to **591 units** of the berth and then drove away from it.

### The cause is mine

The beach-run block reused `off` and `err` — which are measured against `steerX/steerY`, **the planner cursor**. So "drive straight at it" drove straight at whatever waypoint the cursor happened to hold, while the cursor kept advancing and replanning underneath it. The last four seconds of that run contain three `OFF-GRID` recoveries, each one re-planning and moving the cursor again.

It now computes the bearing to **`s_gdTargetX/Y`, the berth itself**, and logs `berthOff` plus the closest approach so the next log cannot be misread the same way. The window goes 9,000 → 14,000 ms, because honest steering has to cover ~2 km rather than wander it away.

### The wording is fixed too, and that matters more than the steering

`REFUSED ... That is the engine's answer, not the model's` was about to write a **false fact into the record**. A hull that is moving is not being refused. The line now reads *"did not get there"*, with the closest approach, and says explicitly that the hull moved throughout.

This is the same error as reading the Lunar Cry cutscene as an engine block — an instrument reporting a confident conclusion its data does not support. Twice in one week is a pattern worth naming: **when a diagnostic prints a verdict, the verdict has to be entailed by the numbers beside it.**

### What the data does say

* The hull reaches **591 units** of the beach berth under its own steering, on water, with `gate=208` throughout — it never got shallow.
* Nothing in two nine-second runs looks like an engine refusal.

Whether the Garden can climb that shore is **still unanswered**. This build is the first one whose survey can actually ask.

### Verification

* `tests/garden_harness.cpp`: **25 ok / 0 bad**, `beach_climb semantics: raw=0 berth=1`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard (`world_garden.inl` 78,892 — 3 KB of headroom, split before the next edit that needs room).

**NOT MSVC-built, NOT BAT'd.**
