## v0.20.99

#80: **the catalog was not story-aware, and the signal was already in the file.**

Aaron:

> *"The original location for Balamb Garden on the Balamb continent is no longer
> accessible since Garden is now mobile. This entry shouldn't be in the catalog
> anymore at this point in the game."*
>
> *"Galbadia Garden is also no longer reachable at this point in the game at its
> original location... it is essentially vanished from the world map at this
> point."*

Both were still offered as fixed coordinates. Choosing either would have driven a
blind player sixty kilometres to empty ground.

### The signal needed no new machinery

`bgu_pos` — WORLDMAP struct +0x24, **the same read that already builds the
"Mobile Balamb Garden" entry** — is zero while the Garden is parked and live from
the moment it moves. That was the assumption. It is now measurement.

**FF8 Steam saves read offline.** An `.ff8` is a 4-byte length then LZS,
decompressing to exactly 8,192 bytes, with the savemap at **+0x184**. Anchored on
locID, HP, level, gil, save count and played time, and cross-checked on two saves
taken sixteen minutes apart (save count 90→92, played 40,780→41,718 s).

So all **39 of Aaron's saves** were read directly — no diagnostic build, no BAT:

```
slot1_save01 .. slot2_save27    bgu = (0,0)             37 saves, every one empty
slot2_save28, slot2_save29      bgu = (20271,-24355)    mobile
```

`(20271,-24355)` is the exact `GARDEN_START` the offline berth generator has used
since v0.20.84 — an independent second confirmation that fell out for free.

Galbadia Garden rides the same signal, on Aaron's testimony: *"G-Garden is
already mobile by the time B-Garden becomes mobile."* There is no window where
the Garden is mobile and the Galbadia site is still occupied, so no second flag
is needed to retire it.

### Also settled — negatively, and cheaply

The hypothesis going in was that one of the four unexamined WORLDMAP slots
(+0x0C, +0x3C, +0x48, +0x54) held Galbadia Garden, making one slot both the flag
and the coordinate.

**It is empty in every one of the 39 saves. All four are.** Galbadia Garden has
no position record in that struct, so its reappearance near Edea's House will
need a coordinate and a flag of its own.

That hypothesis died before a line of code was written for it, which is the point
of reading the data first. The struct is also now pinned as **eight 12-byte
slots, 0x00–0x5F** — `+0x60` is not a position record.

### One predicate, asked by every builder

`WmStoryRetired(name)` is the only test, and the catalog assembles in three
places: the unfiltered path, the flood-filtered path, and the aboard path — which
rebuilds from `s_locations` rather than from the compacted `s_catalog` and so
asks the question a second time.

**That is the exact shape of the v0.20.88 bug** — a condition armed in one place
and asked about in three, which silently vanished Shumi Village from the catalog
it had just been added to. So the aboard path gets its own check in the test
rather than being assumed to follow.

### A harness gap worth naming

**No host harness compiled `world_catalog.inl` at all.** `catalog_harness.cpp` is
the *field* catalog; the world catalog has only ever been compiled by MSVC. That
is precisely the gap that let the v0.20.89 declaration-order error reach a build.

New **`tests/catalog_story_test.cpp`** compiles the real file against stubs and
checks all three states:

```
Garden STATIC : 39 entries, Balamb Garden=1 Galbadia Garden=1  OK
Garden MOBILE : 38 entries (want 38), Balamb Garden=0 Galbadia Garden=0  OK
Mobile Balamb Garden present: 1  OK
Garden ABOARD : 37 entries, Balamb Garden=0 Galbadia Garden=0  OK

ALL CHECKS PASSED (0 failures)
```

38 rather than 37 in the mobile case because the mobile branch also *appends*
"Mobile Balamb Garden" — the fixed sites go, the ride you can actually reach
stays.

### Verification

* `tests/catalog_story_test.cpp`: **4 checks, 0 failures**, clean under
  `-Wall -Wextra`.
* `tests/world_map_harness.cpp`: WORLDMAPGUARD PASS.
* `tests/garden_harness.cpp` and `tests/garden_aboard_test.cpp` unchanged and
  still passing — this build touches no grid bit, no berth and no route.
* Every source file inside the 80 KB guard (`world_catalog.inl` 29,144).

**NOT MSVC-built, NOT BAT'd.**

**BAT**: board nothing, just stand on the world map and open the catalog.
**Balamb Garden and Galbadia Garden must both be gone**, and "Mobile Balamb
Garden" must still be there pointing at wherever you parked. Then board the
Garden and check the list again — same two absent. Grep `[STORY]` for the
one-line count.

### Still open

* **Mobile Galbadia Garden near Edea's House** needs a coordinate and a story
  flag. Aaron is close to that point; a save immediately before and immediately
  after it appears gives both, via `Utilities/diff_ff8_saves.py` for the flag and
  the page-8 patch method for the coordinate.
* **Chocobo Forest 3** points at nothing; the unclaimed forest at (51968, −64256)
  with arrival record 35 (52145, −63615) is almost certainly it — berth
  (53632, −61568), walk 2,485, corridor clear.
* **Chocobo Forest 4** sits by a 47-cell terrain-2 patch that is not the forest
  signature.
* **Deling City's marker** is not foot-walkable and gets snapped 128 units.
