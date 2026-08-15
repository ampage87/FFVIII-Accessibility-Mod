## v0.20.100

#80: **Mobile Galbadia Garden, and it exists for a window.**

> *"That trigger is where Mobile Galbadia Garden is located on the World Map, so
> that is how it should be identified in the catalog. Also, once the battle of
> the Gardens is over, Galbadia Garden disappears from the world map forever."*
>
> *"...by the time the battle of the Gardens is over, you are on disc 3. You can
> use that as a signal to know when Galbadia Garden is 100% gone."*
>
> *"Let's leave the mod crossing the trigger when headed to Edea's House — you
> can't actually enter Edea's House until after the battle of the Gardens, and it
> makes for a more fluid experience."*

### The coordinate: (−24982, 65761)

Pinned from the .99 BAT, where auto-drive to Edea's House crossed it unasked:

```
[DRIVE] Manual field entry at (-24982,65761) -- nearest location Edea's House
        is 3437u away (> 3000), not capturing
[fieldload] id=253 name='bgsido_4'
FieldAnnounce: name='B-Garden - Headmaster's Office 5'
[TTS] Nida "Squall, take a look."
```

Driving onto it loads field 253, so **arrival needs no special handling** — the
drive already ends when a field loads. It is a `drive_in` like Fisherman's
Horizon: park *is* the marker, walk 0. Planner cell (127,414), WALK, clearance 5;
16 `gexec3` routes from four starts arrive with **zero replans**.

**Nothing in the map files could have found this.** The spot is terrain 7,
texture page 128, with nothing but more of the same for 1,000 units in every
direction, and the nearest page-8 settlement patch is **5,249 units away**. The
page-8 method that found Shumi Village finds places the map *draws*; a scripted
event zone is a pure coordinate test with no geometry behind it. The candidate
offered before the BAT — (−29632, 70108) — was **6,365 units wrong**. It was
flagged as a lead rather than a conclusion, and it was wrong.

The route crossing stays as it is, on Aaron's instruction. It is a trigger the
player wants.

### The gate is a two-sided window

```
before it appears near Edea's House  ->  not in the catalog
present near Edea's House            ->  the destination
after the battle of the Gardens      ->  gone FOREVER
```

**Closing edge — given outright.** The savemap header carries the disc at +0x44
as a 0-indexed uint32; the 41-save sweep shows the disc-2 transition at
`slot2_save11`. Disc 3 or later hides it unconditionally.

**Opening edge — narrowed, not confirmed, and shipped as a candidate.** The
filter was the whole save history rather than one diff: bytes **identical in all
40 saves before `slot2_save30` and changed in `slot2_save30`** — the only save
where the Garden is present, every earlier one being either "still at its
original site" or "vanished". That cut an 84-byte raw diff down to three fields:

```
savemap+0x0B94..0x0B95   0000 -> 01b7    an append-only list of uint16s
savemap+0x0E8D           00   -> 02      ISOLATED byte, static neighbours
savemap+0x0FDC           00   -> 03      inside a region that churns
```

`+0x0E8D` is the one shaped like a state enum: a single byte that stays zero
across 100,000 seconds of play and steps to 2 exactly when the Garden appears,
with its neighbourhood untouched throughout. That is what ships.

**The disc rule makes being wrong cheap.** If `+0x0E8D` is the wrong byte, the
destination can only appear at the wrong time *within disc 2* — never after. And
all three candidates are logged on every catalog build:

```
[STORY] disc=2  gg[0x0E8D]=2  alt[0x0B94]=439  alt[0x0FDC]=3
        -> Mobile Galbadia Garden PRESENT
```

so the next BAT either confirms the choice or names the right one. The verdict
and the evidence print on the same line, which is the .93 rule.

### A latent harness bug, found by adding a berth

`garden_harness.cpp` iterated `s_gardenParks` using the **catalog** count (39,
hardcoded) rather than `GARDEN_PARK_COUNT`. The two happened to match. Adding a
26th berth would have silently skipped one; it now uses the berth table's own
count.

### Verification

* `tests/catalog_story_test.cpp`: **8 checks, 0 failures** — static / mobile /
  aboard, plus all four window states (disc 2 flag 0, disc 2 flag 2, disc 3, disc
  4). Counts move consistently: 39 static, 38 mobile, 37 aboard against a
  40-entry `s_locations`.
* `tests/garden_harness.cpp`: **26 ok / 0 bad** (was 25 — the new berth),
  `beach_climb berths in the table: 0`, Shumi checks unchanged.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
  `tests/world_map_harness.cpp`: WORLDMAPGUARD PASS.
* Route regression not re-run: the only berth-table change is one `drive_in` row,
  which `offline/regress84.py` skips by construction, exactly as it skips
  Fisherman's Horizon. The other 24 berths are byte-identical.
* Every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**

**BAT — and it needs a save reload, because the window has closed on the live
playthrough.**

1. **Load `slot2_save30`** (Galbadia Garden present near Edea's House). Open the
   catalog: **Mobile Galbadia Garden must be listed**, and driving to it should
   start the Garden battle. Grep `[STORY]` for the disc and the three bytes.
2. **Load the current post-battle save** (disc 3). **Mobile Galbadia Garden must
   be absent**, and the `[STORY]` line gives the post-battle values of all three
   candidates — which is the data that confirms or corrects the opening edge.

Step 2 is the one that matters most; it is the missing measurement, and it costs
a save load.
