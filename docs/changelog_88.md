## v0.20.88

#80: **two things — the Trabia "wedge" was a cutscene, and Shumi Village is re-opened as a beach climb.**

---

## Part one: it was never a wedge

**BAT (v0.20.86).** Winhill and Trabia Garden, both arrived. The pivot escape from .86 fired exactly as designed:

```
[GARDEN] pivot deadlock at (-16825,-1627) hd=567 off=1539
         -- turning in place is going nowhere, adding throttle
```

and the Winhill drive carried on. Replay came back clean again: **194 samples, `cell` / `cls` / `clear` / `blk` / `aim` all 100.00%.**

### The Trabia stall, three BATs running, and what it actually was

The hull froze within twenty units of the same point every time — (60976,−44518), (60993,−44530), (60993,−44513). In .86 the throttle *was* on (`keys=U-R-`, `off=558` inside the cone) and it still went nowhere, so the refusal sweep fired. Its output looked like a hard engine block:

```
 1/16 at (60993,-44513) hd=3636 -> ENGINE MOVED
 2/16 at (60993,-44513) hd=3636 -> ENGINE refused
 ...
13/16 at (60993,-44513) hd=3636 -> ENGINE refused
14/16 at (60788,-44753) hd=3636 -> ENGINE MOVED        <- after the reverse
```

Thirteen samples over nine seconds and **`hd=3636` on every one**. The sweep is built to rotate the hull through sixteen headings; it could not rotate at all. I was one edit away from shipping a "pinned hull" heuristic built on that.

Aaron:

> *"There is a spot on the world map that causes the Garden to go haywire temporarily. Nida even comments on it when you hit that spot. It is part of the game's lore as that spot is where a Lunar Cry happened in the past."*

The dialog log has it, one second before the hull stopped:

```
[00:12:56] [SHOW_DIALOG-TEXT] win[0] mode=2 text=""Huh!?""
[00:13:12] [SHOW_DIALOG-TEXT] win[0] mode=2 text="Nida "The gauge is going berserk!?""
```

**A field-dialog window was open for the whole fifteen seconds.** The engine ignores movement input while one is up — which is precisely `mv=0/57` with a frozen heading. Nothing was refusing terrain. Nothing was pinned. The Garden was doing what the game told it to do, and the mod reversed and replanned in the middle of a cutscene, three BATs in a row.

### The fix already existed, on the other drive

The FIELD auto-drive has done this since v05.37:

```c
// v05.37: Suspend key injection during dialog (scripted cutscenes lock movement).
// Don't stop the drive — just pause until dialog clears.
if (FieldDialog::IsDialogOpen()) { ReleaseAllDirections(); s_driveStuckTicks = 0; return; }
```

The Garden drive never got it. Same predicate, same treatment: release the keys, hold **every** watchdog at the current instant — stall, throttle, route-progress, probe, announce — so none of them fires on time that was never ours, and resume where the scene left off. New log lines: `[GARDEN] cutscene at ...` and `cutscene over at ...`.

The .86 pivot escape stays; it fired legitimately at Winhill and is a separate, real defect. The "pinned hull" heuristic drafted for the wrong cause is **dropped rather than left in as a second-guesser** — a rule built on a misdiagnosis will misfire somewhere else.

### The lesson, written down

I spent this session deriving a terrain explanation for a place that has a *name* and a *cutscene*. Aaron plays this game; I do not. The refusal sweep, the region-byte lookup and the raw-polygon query were all pointed at the right coordinate and all of them were answering the wrong question. **Ask what a place is before modelling why it refuses.** That is the second time this week the answer came from him rather than the disassembly — Shumi's beach was the first.

### Verification

* **Replay** against the .86 BAT: 194 samples, **all five checks 100.00%.**
* **Parity**: WALK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / CLEAR identical; PARK differs by 1 (documented tolerance).
* **Route regression**: 23 berths × 4 starts × 4 headings, **356 runs, 0 failures**.
* `tests/garden_harness.cpp`: grid 678,223/786,432, reachability 662,681, **24 ok / 0 bad**. `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED. Both carry a `FieldDialog::IsDialogOpen` stub returning false.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

---

## Part two: Shumi Village, re-opened as a beach climb

> *"You go up the beach like any other. Let's have the build try to drive up the beach and get as close as possible to the Shumi Village. If it runs into walls or something like that so be it, but we can at least use that data to survey the area around the village."*

### Where the beach is, confirmed at polygon level

The island's landing zone is **53 terrain-9 polygons with `b15 = 0xF7`** — Garden walk, disembark and foot bits all set — on the SOUTH shore at world (2368…3264, −81984…−83264). That is the **only** Garden-masked ground among the island's 7,034 foot cells.

I checked whether the 128-grid was hiding more of it. The grid keeps the *first* polygon covering each sub-point, so an overlapped masked polygon would vanish. Re-rasterising all **473,193 polygons** with an any-covering-polygon rule gains **208 sub-points map-wide and not one of them is on this island.** So 53 is the real number, the wedge is the whole landing zone, and the Garden cannot drive up to the village — it lands and you walk.

### Why every generator before this one rejected it

**The walk is 12,288 units.** The village is on the north shore and the beach on the south; `WALK_CAP` is 6,000. The game asks for that hike — the walkthrough says *"take the ship up onto the land nearby and then head into the village"* — so **the cap was the thing that was wrong, not the berth.**

### The shore step, and why the rule is bypassed rather than loosened

Between the water and the terrain-9 plateau sits a skirt of terrain-29 polygons carrying **no masks at all**, so the 128-unit samples either side read a **295-unit step against a 200 gate**. The engine climbs it; the model refuses. That beach rule is measured and it is what fixed Balamb island in .84, so loosening it map-wide on a guess is exactly the wrong move.

Instead: a new `beach_climb` flag on the berth waives **both** the water→land test and the 200 cliff gate **for that one goal cell**, and nothing else. Everything about getting to the water beside it is unchanged. `Garden_CellReachable` asks the flood about the water next to the berth rather than the berth itself, for that cell only, and `Garden_Plan`'s O(1) out-of-flood refusal skips it.

**A\* routes it: 360 waypoints from Balamb.**

If the engine refuses the climb after all, the drive stops **at a known coordinate with `[GDTRACE]` running** — which is the survey Aaron asked for rather than a mystery.

### Also

Arrivals past 5 km now announce **kilometres**. "Shumi Village is 12.3 kilometres north — a long walk" beats "one hundred and twenty-two hundred units".

### Verification

* **Harness**: 25 ok / 0 bad (Shumi included, with its exception armed as `Garden_Start` arms it), and a new check that A\* can actually *route* to the beach, not merely that reachability says yes.
* **Parity** unchanged — the exception is runtime-only and touches no grid bit.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* Every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
