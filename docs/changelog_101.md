## v0.20.101

#80: **the White SeeD Ship has a window too — and both its edges are
already-validated signals.**

> *"The White SeeD Ship — it only becomes available on Disc 3 and disappears from
> the World Map when the player receives the Ragnarok."* — Aaron

Until now it was offered unconditionally, so on discs 1 and 2 the mod would
happily drive a blind player to a ship that is not there. Same defect class as
the two Garden sites v0.20.99 retired.

### Both edges were already in hand

```
opening:  disc >= 3               savemap header +0x44, 0-indexed uint32
closing:  ragnarok_pos goes live  WORLDMAP struct +0x18
```

The opening edge is the same disc field the Galbadia Garden window uses, whose
0-indexing and disc-2 transition were pinned in the 41-save sweep.

**The closing edge needed no new discovery at all.** `ragnarok_pos` is the exact
analogue of `bgu_pos`, and `bgu_pos`'s behaviour — zero while the vehicle is
parked, live from the moment you have it — is *proven* across all 41 saves.
Ragnarok at +0x18 is empty in every one of them, which is correct: it is a disc-3
vehicle Aaron does not have yet. When he receives it, that slot fills exactly as
the Garden's did at `slot2_save28`.

So, unlike Mobile Galbadia Garden, **neither edge here is a guess.** Both are
fields whose semantics were established by measurement earlier in this same arc.
That is the whole return on having read the saves properly.

### One predicate, still

`WmStoryRetired` gains one more name. It remains the only test, asked by all
three catalog builders — the v0.20.88 rule.

`[STORY]` now carries both windows on one line, verdict beside evidence:

```
[STORY] disc=2  gg[0x0E8D]=2  alt[0x0B94]=439  alt[0x0FDC]=3
        -> Mobile Galbadia Garden PRESENT | Ragnarok not held -> White SeeD Ship absent
```

### Verification

* `tests/catalog_story_test.cpp`: **13 checks, 0 failures** — static / mobile /
  aboard, the four Galbadia window states, and five White SeeD states covering
  both edges (disc 1 and 2 too early; disc 3 without Ragnarok present; disc 3 and
  disc 4 with Ragnarok gone).
* `tests/garden_harness.cpp`: **26 ok / 0 bad**, `beach_climb berths: 0`, Shumi
  checks unchanged. `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
  `tests/world_map_harness.cpp`: WORLDMAPGUARD PASS.
* No berth, grid, route or planner change — this build only gates catalog
  membership.
* Every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd. Includes v0.20.100 (Mobile Galbadia Garden), which
was never built — one build covers both.**

### One thing worth knowing before trusting it

**The White SeeD Ship destination has never been exercised.** It is disc-3
content and Aaron is on disc 2, so its coordinate (4887, 51285) and berth
(2944, 53376) have never been driven to. They came from the original research
document, not from a BAT.

**Does it move? No** — Aaron: *"the White SeeD Ship is immobile for the time it
is on the World Map."* So a fixed marker is the right model and no live position
read is needed. That question is closed.

What is **not** closed is whether the marker is in the right place. A look at what
the map says about (4887, 51285):

* terrain 7 (grass), 479 units above sea, with water 58 cells inside 1,000 units
  — a coastal spot, which is at least the right kind of place
* texture page 128, **no page-8 feature anywhere near it**
* **no `wmsetus` arrival record within 4,472 units** — the nearest is record 16,
  which is Centra Ruins' own

That is the same profile as the two markers that turned out to be wrong this
week: Shumi's old coordinate and the page-8 candidate for Galbadia Garden. It is
not proof of anything — the Galbadia trigger had exactly this profile and was
correct — but it means the marker is unverified in precisely the way that has
already cost eleven builds. **First thing to check on reaching disc 3.**

### BAT

Unchanged from v0.20.100 — it needs two save loads, because the Galbadia window
has closed on the live playthrough:

1. **Load `slot2_save30`**: Mobile Galbadia Garden must be listed, and driving to
   it should start the Garden battle.
2. **Load the current post-battle save** (disc 3): it must be absent, and the
   `[STORY]` line gives the post-battle values of all three candidate bytes.

On any disc-2 save the White SeeD Ship must now be **absent** from the catalog —
that part is checkable immediately, and it is a change in behaviour you should
notice today.
