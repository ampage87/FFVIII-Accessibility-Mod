## v0.21.4

#70: **the on-foot auto-drive, reviewed against everything the Garden system
taught us — and five defects it had been carrying.**

> *"We haven't reviewed or updated the world map walking auto drive system since
> we implemented the B-Garden world map auto drive system. I suspect the walking
> auto drive system could be improved based on the findings from the B-Garden
> system research and implementation."* — Aaron

Two full read-throughs, planner and executor, each with the Garden subsystem
beside it. The findings are written up with file:line evidence in
**`docs/FOOT_AUTODRIVE_REVIEW.md`**. Shipped here are the five that are
unambiguous; the rest are ranked there with a suggested order.

---

### 1. A cutscene is not a stall — and on foot it was doing permanent damage

The Garden executor has paused on an open dialog window since v0.20.87, after
three BATs wedged in the same spot during the Lunar Cry scene. `IsDialogOpen()`
was declared for that fix. **The foot executor never called it.**

While a dialog holds the world map the engine ignores movement. The foot freeze
detector fires after 40 ticks — about 0.65 s — and its first act is
`CamwLearnBlock`, which writes into the learned-obstacle overlay. That overlay is
**deliberately persistent across drives**. So every world-map cutscene taught the
planner that a piece of open ground was a wall, permanently, then pressed keys
into a locked engine and retreated through the terrain it had just libelled.

`UpdateAutoDrive` now pauses on the same predicate, releases the keys, holds the
stuck clock, and on resume bumps `s_driveWatchdogGen` — the mechanism v0.18.3.216
already built to reseed the route clocks after a battle. A cutscene is the same
event with a different cause.

### 2. The learned-obstacle overlay went silently sterile at 256

`AddNavBlock` returned `false` when full — and that return value is not just
"dropped". It is the signal the recovery ladder reads as *no new knowledge*,
which routes it to the fence-inflation branch, whose entire job is to add blocks.
So at 256 entries the executor entered a state where **every recovery was a no-op
and every replan was sterile**, with nothing logged.

It now evicts oldest-first and says so. The overlay records where the engine
refused to walk; the oldest entries are the least likely to still matter, and
keeping `AddNavBlock` truthful keeps the ladder moving.

### 3. The grid planner truncated long routes and called them planned

```c
for (i = rc-1; i >= 0 && n < DRIVE_PATH_MAX; i--)   // 768 waypoints
```

A 1,500-cell route kept 768 waypoints and **ended 732 cells short, in open
country**, marked `s_drivePathPlanned = true`, with no log line. The 384-cell
margin ladder exists to permit ~49 km horseshoe detours, which is exactly the
case that overflows. `PlanPathFine` has stride-sampled for this reason since it
was written; the live planner never did.

It now decimates, keeps the goal, and logs the resolution it dropped to.

**`tests/pathdecimate_test.cpp` earned its place immediately** — the first
version of this fix picked stride 2 for a 1,536-cell route, every stride-aligned
index came out odd, and the goal was never emitted. The test caught it on the
first run, before the build left this machine.

### 4. `s_drivePathWorld` survived between drives

It gates the reverse un-wedge and the **whole** route-progress and give-up
watchdog block. It was cleared on neither the planner-ineligible path in
`StartAutoDrive` nor in `StopAutoDrive` — and it is also written by the **Garden**
executor. So whether a foot drive had route watchdogs at all could depend on what
the previous drive did, including a drive by a different vehicle. Reset per drive.

### 5. Diagnostics were shipped on

`WM_MOTION_DIAG` emitted one **unthrottled** `[MFRAME]` line per poll tick, and
`Log::World` flushes synchronously — 120–180 formatted file writes a second on
the poll thread, for the whole of every drive. `DRIVE_STEER_DIAG` added another
every 50 ms; its own comment read *"set false before push"*. Both off.

---

### What is deliberately NOT done, because the review was wrong about it

The review's highest-ranked item was "the LOS clamp and lookahead are computed
and discarded — one line to fix". The discard is real:

```c
wi = s_drivePathIdx + 1;   // world_map_drive.inl
```

**But it is deliberate, and documented.** v0.18.3.155 records the far selector
oscillating between adjacent corridor cells with the character limit-cycling and
never advancing, and the offline sim carrying it 20 km with sequential follow
versus ~700 units with the selector.

So the fix is not to restore the discarded value — that reverts a hard-won
correction. It is to add what the Garden pairs with its lookahead and the foot
system never had: **commitment**, plus a lookahead that collapses near obstacles.
That is a design change, it changes how every drive feels, and it should be built
against a harness rather than shipped on an argument. It is item 4 in the review's
order of work, behind the harness itself.

This is worth recording as a method note: **an audit that reads only the code
will recommend reverting fixes whose reasons live in the comments.** Both agents
that produced the review missed that comment; the check that caught it was
reading the site before editing it.

### Verification

* `pathdecimate_test` **OK (0 bad)** — new gate. Asserts the old emission
  reproduces the truncation, then over every route length from 2 to 1,539: never
  overflows the buffer, starts at the start, **ends at the goal**, stays
  monotonic, and leaves routes that fit at full 128-unit resolution.
* `trigseg_test`, `vehsig_test`, `catalog_story_test`, `garden_harness`
  (26 ok / 0 bad), `garden_aboard_test`, `world_map_harness`,
  `minigame_bgbtl_compile` (0 errors / 0 bad), `lint_seh` OK (88 files).

**NOT MSVC-built.**

### BAT

Drive somewhere long on foot.

1. **The log should be much quieter** — no `[MFRAME]`, no `[YAWDRIVE]`. If a
   drive still feels wrong, say so and I will turn them back on for a capture.
2. **Walk into a world-map cutscene if you can find one** (the spot where Nida
   comments is the known one). The drive should say *"cutscene … pausing; this is
   not a stall"* and resume, instead of learning a phantom obstacle.
3. Everything else should behave exactly as before. Items 2–5 are all
   fault-path changes: on a clean drive none of them executes.
