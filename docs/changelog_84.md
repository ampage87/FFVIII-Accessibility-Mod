## v0.20.84

#80: **`[0x203EE88]` is the Garden's altitude, and altitude follows the terrain class, not the mesh height. Balamb island was a one-way trap.**

**BAT (v0.20.83).** *"Very interesting results. Successfully made it to the Tomb. However, it wouldn't take me to Balamb Town and Fire Cavern did not appear in the catalog. I tried to go to Shumi Village and it got quite close supposedly, then advised me to hop off and walk, but when I hopped off Garden the Shumi Village did not appear in the catalog and the auto-drive wouldn't get me back to the Garden either."*

Four complaints. They are two bugs, and the first one is the beach rule.

### What the log actually says

```
[GARDEN] reachability from cell (479,591): 657082 cells      <- from Balamb
[GARDEN] reachability from cell (528,338): 655108 cells      <- from the Tomb
[GARDEN] plan FAILED (-44090,-37564)->(15232,-25216) after 655108 expansions
[GARDEN] plan FAILED (-44121,-37558)->(15232,-25216) after 655108 expansions
[GARDEN] plan FAILED (-42625,-37477)->(15232,-25216) after 655108 expansions
```

The two floods differ by exactly **1,974 cells**, and every one of them is on Balamb island — world x 3,712…32,640, y −33,152…−22,144. Nothing is reachable from the Tomb that is not reachable from Balamb; the difference is entirely one-way. **The Garden could leave the island the save starts it on and never get back.** Balamb Town and Fire Cavern both berth inside that pocket, which is the whole of Aaron's first two complaints.

### The rule that made it a trap

`0x53E3C1`, inside the movement validator, for vehicle `0x30` alone:

```
al = poly[0x0D]                     ; candidate terrain
cmp al,0x22 / ja  -> height test
cmp al,0x1E / jae -> ACCEPT         ; 30..34 always passes
mov eax,[0x203EE88] / test eax,eax
jg  reject                          ; land only while altitude <= 0
```

v0.20.73 read `[0x203EE88]` as a flag and modelled the branch as *"allowed terrain whose mesh height rises above sea level"* — a beach. **`0x54B49F` says it is not a flag:** it copies the active entity's position into `0x203EE80/84/88` as `(x, −z, y)`. The value tested is the **Garden's own altitude**.

296 `[GDTRACE]` samples from the .83 BAT, each cross-referenced against the polygon under the hull:

| terrain | samples | altitude |
|---|---|---|
| 32 (shallow shelf) | 46 | −254 … −58 — **always ≤ 0** |
| 33 | 2 | +208 |
| 34 (deep ocean) | 127 | +208 / +210 — 126 of 127 > 0 |

**The mesh height is 0 on all three.** What decides the altitude is the terrain *class*: the hull rides high over the shelf and sits down into the deep. So a beach is the shelf, and the height test was a proxy that happened to coincide at the Tomb of the Unknown King and nowhere on Balamb's coast — 1,285 cells passed it map-wide and **not one of them was on Balamb's shore**.

`GDC_BEACH` is now terrain 30..32 on any sub-point of a water cell: **9,504 cells**. The flood is symmetric again — **662,681 from Balamb and the identical 662,681 from the Tomb**, no pocket in either direction, and Balamb Town and Fire Cavern come back without their berths moving a unit.

### The catalog promised what the planner could not deliver

`Garden_CellReachable` returned true if **any of the nine cells around a berth** was in the flood. From the Tomb, Balamb Town's berth was outside it and the ocean cell beside it was inside — so the catalog offered the destination, Aaron chose it, and A* had to close the entire 655,108-cell component before it could say no. Four seconds of silence, three times over.

Two fixes: the test is now on the berth's **own** cell (the 3×3 widening survives only for a berth the Garden may not occupy at all), and `Garden_Plan` refuses an out-of-flood goal in O(1) instead of proving it exhaustively.

### Shumi Village: Aaron is right, there IS a beach — and it is not where a berth can go

I had this wrong twice over and the correction is worth writing down.

Aaron: *"You can reach Shumi Village with the Garden. I've told you this before — look it up online if you don't believe me."* The published guide says exactly that: *"If you are navigating around in the mobile Balamb Garden ship, use the nearby beach to take the ship up onto the land nearby and then head into the village."*

**The beach is in the data, exactly where he says.** On the SOUTH shore of the Shumi island, world (2368…3264, −81984…−83264): a wedge of **53 terrain-9 polygons carrying `b15 = 0xF7` — Garden walk, disembark and foot bits all set** — descending from −478 to −298. It is the **only** Garden-masked ground on the whole 7,034-cell island, and it is the island's one landing zone. Everything I said about "53 of 7,034" was true and I drew the wrong conclusion from it: those 53 are not noise, they are the beach.

So why is Shumi still hidden this build? Two reasons, and neither is the terrain rule:

1. **The walk is 12,288 units.** The village is on the north of the island and the beach is on the south. `WALK_CAP` is 6,000. That alone rejects it — and 12 km is a real hike the game does ask of you, so the cap is the thing to revisit, not the berth.
2. **The shore step is 295 units and the gate is 200.** Between the water (terrain 34, height 0) and the terrain-9 plateau (−295 at its nearest vertex) sits a skirt of **terrain 29 polygons with `b15 = 0x00` — no masks at all**. In the model the hull cannot stand on the skirt, so it must step 295 units in one move, and `|dH| >= 200` refuses. The engine evidently manages it. **I cannot tell offline how**, and rather than guess a fourth time I would rather measure it.

Offering a berth on the strength of a guess is exactly what stranded him in .83 — parked 5 km out in open water, `[BFS] Filtered to 0 reachable locations`, no Shumi in the catalog and no way back to the hull. So Shumi stays hidden **for this build only**, with a concrete next step rather than a shrug: drive to the beach approach at **(3200, −81792)**, hold the throttle at the wedge, and log `mv=n/N` and `[0x203EE88]` per frame. That is the same instrument that settled the Centra freeze, pointed at the one shoreline the game guarantees is climbable — so it can only come back with an answer.

### Berths: regenerated where they had to be, and only there

`offline/gen_berths.py` now applies **all four constraints in one pass** — Garden-reachable, ashore with a real step-off, same foot landmass as the marker, 2–3 km standoff preferring open ground. Every previous regeneration applied a subset and re-broke whatever the missing one protected; .79 dropped the landmass check and told a blind player to walk 4,471 units across open water.

**17 of the 22 berths .83 shipped still satisfy all four and are kept verbatim** — including every one Aaron has driven: Tomb of the Unknown King, Trabia Garden, Centra Ruins, Deling City, Winhill, Timber, Balamb Town, Fire Cavern. Moving a BAT-proven berth for a marginally better score is a regression risk for no gain.

| moved | why |
|---|---|
| Galbadia Garden, Galbadia Station, Chocobo Forest 5 | walk was 4.5–6.0 km; re-picked |
| Shumi Village, Alien Ship 1 | berth was on a **different foot landmass** — hidden (see above for Shumi) |
| Dollet, Chocobo Forest 2, Chocobo Forest 6 | **new**: the shelf rule reaches them now |

24 of 39 destinations now have a berth.

### The split

`world_garden.inl` was **86,261 bytes** against the 80 KB CI hard fail — and **already 84,567 at v0.20.83, so that build could not have been pushed either**. Per DEVNOTES, split before the edit that needs the room: the cut is at the end of `Garden_Plan`, into `world_garden_plan.inl` (21.4 KB — reachability, the dock table, the aboard latch, A*) and `world_garden.inl` (65.6 KB — probes and executor). No code changed in the move.

### Verification

* **Parity** (`offline/parity_check.py`): WALK / FOOT / OPEN_E / OPEN_S / WATER / BEACH identical between the C++ and `gsim3`, PARK differs by 1 (the documented "seen" tolerance). BEACH 9,504 on both sides.
* **Replay** (`offline/replay.py`, 296 samples from the .83 log): with the **old** beach rule the model was 296/296 on *every* axis — cell, cls, clear, blk, aim. With the new rule, `cell`, `clear` and `aim` stay at 100.00%, and the only divergences are **44 `cls` (every one of them the BEACH bit alone, xor = 0x40)** and **7 `blk` downstream of it**. The change is isolated to exactly what it was meant to change; the next BAT re-validates it against the game.
* `tests/garden_harness.cpp`: grid 678,223/786,432, 74,184 parkable, reachability 662,681, **24 ok / 0 bad**. `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* **Route regression** (`offline/regress84.py`): 23 berths x 4 starts x 4 headings, **356 runs, 0 failures**. The Tomb start is the one that matters -- it is where .83 answered "plan FAILED" for anything on Balamb island.
* Both harnesses clean under `-Wall -Wextra`.
* Every source file inside the 80 KB guard: `world_garden.inl` 65,637, `world_garden_plan.inl` 21,406, `world_garden_grid.inl` 35,077, `world_garden_berths.inl` 10,594.

**NOT MSVC-built, NOT BAT'd.**
