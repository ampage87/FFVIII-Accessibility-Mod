# v0.21.6 — Edea's House: the door is seven triangles wide

**#79.** The refusal was never a story gate, a master gate or the destination
table. It was 234 units of ground.

## What decides a field entry

`sub_545EA0` opens with `mov eax,[0x020409FC]` / `test byte ptr [eax+0x0E], 8`.
That pointer is **the polygon under the player** — which is why it churned every
frame, and why v0.21.3 wrongly retracted it as a broken instrument. It was
reading exactly the right thing, and it was reading it correctly: bit 3 clear,
everywhere Aaron walked.

Being an entry polygon is not enough. It must also be foot-walkable, and that is
a different byte (`0x0F` bit 7, the move validator's flag the navmesh already
uses). Map-wide: **7,359 entry polygons, 2,877 foot-walkable.**

## Edea's House

Segment 652 holds **103 entry polygons. Seven are foot-walkable** — the other 96
are the building. The seven cover `x[-29975,-29310] y[69632,70078]`, about
665 × 446 units, roughly a three-hundredth of the segment.

The marker was `(-28950, 70090)`: **234 units east of that patch**, up against
the wall. v0.21.1's page-8 archaeology found the right structure and then aimed
at the wrong face of it — *"east is the side the player arrives from"* was a
reasonable inference and east is where the wall is.

In the 2026-08-16 BAT the closest you got was `(-29144, 70004)` — **287 units
short.** Every gate the exe names passed on every frame, because they all
genuinely did pass. You were never standing on a trigger polygon.

New aim: **(-29459, 69772)** — the interior point of those seven triangles
farthest from their edge, 140 units of margin in every direction.

## Why v0.21.5's interpreter said MATCH anyway

Programs 34 and 35 have no coordinate clauses at all, so `[TRIGWALK]`'s finest
positional unit is the 8192-unit segment. It printed `MATCH, destination 18` at
twenty-three positions spanning 8 km while nothing loaded — and told you *"every
condition the exe names is satisfied and the refusal is elsewhere."* That read
like a finding. It was a blind spot with a confident sentence attached, and the
sentence is gone.

The polygon gate now gets its own `[ENTRYPATCH]` line beside every `[TRIGWALK]`
verdict: whether you are standing on entry ground, or which patch is nearest and
by how much you are missing it.

## The uncomfortable part

`world_map_trigger_data.inl` has carried this since **v0.18.3.206**:

> a field entry fires only while standing on a wmx poly with byte14 bit 3 set

Three sessions then went hunting through the exe for a gate that was already
documented in the file. The table was never wrong — it was **7 rows long**, and
Edea's House was not one of them.

## The table is now generated

`offline/gen_entryaims.py` scans wmx.obj for entry polygons, keeps the
foot-walkable ones, flood-fills them into patches and takes the interior point
farthest from each patch edge.

Run it against the five aim points found **by hand, in the field, over several
BATs before any of this existed**: Timber 91 u, Dollet 54 u, Balamb Town 47 u,
Fire Cavern 83 u, Galbadia Station 379 u — all inside their own proven bboxes.
That agreement is the whole reason to trust the rows nobody has walked to yet,
and `tests/entryaim_test.cpp` keeps it honest.

Balamb Town's proven box turns out to hold 43 entry polygons of which 7 are
walkable — the same 103/7 shape as Edea's House, with its proven aim in the seven.

## Shipped

Ten new firing areas, all where the marker was within 800 units of its own door:
Edea's House, Tomb of the Unknown King, Centra Ruins, Shumi Village, and Chocobo
Forests 1, 2, 4, 5, 6 and 7. The seven hand-proven rows are untouched.

Five more have patches but need a retarget of 1.2–2.0 km — Deling City, Great
Salt Lake, D-District Prison, Trabia Garden, Winhill — and are held back for
their own BAT. Details and the open questions (Galbadia Missile Base has no entry
polygon within 17 km; Great Salt Lake and Chocobo Forest 3 resolve to the same
patch) are in `docs/WORLDMAP_ENTRY_POLYGONS.md`.

Side effect worth having: the planner prices non-target firing areas at +4096, so
going from 7 areas to 17 means ten more real doors it now routes *around*.
Accidental field entry while walking past a town should get rarer.

## Gates

`entryaim_test` OK (0 bad) — **new**: every aim inside its own bbox, names unique,
all five field-proven points still inside their areas, Edea's bbox asserted to be
the seven walkable triangles with all four historical positions outside it, the
Forest 7 control, and no two firing areas overlapping (they double as no-go zones,
so an overlap would make one destination permanently expensive).
`trigwalk_test` OK (0 bad) · `trigseg_test` OK (0 bad) · `pathdecimate_test` OK
(0 bad) · `vehsig_test` OK (0 bad) · `catalog_story_test` 0 failures ·
`garden_aboard_test` 0 failures · `lint_seh` OK (88 files) · `world_map_harness`,
`garden_harness`, `chase_harness`, `minigame_bgbtl_compile` all compile.

## BAT

**Walk to Edea's House.** That is the test.

Then re-walk **Chocobo Forest 7**, because it is the control and its aim moved
728 units — it worked before by luck of the arrival radius, and it should now work
by aim. If anything regressed, it will show there first.

If the door still refuses, the `[ENTRYPATCH]` line says whether you were standing
on entry ground when it happened, which splits the remaining possibilities cleanly
in two for the first time.

---

## BAT-CONFIRMED 2026-08-16

```
[00:39:49] [DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x01F9,
           fieldName='ehenter2', target=Edea's House, dist=171)
[00:40:16] [DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x0125,
           fieldName='cwwood7', target=Chocobo Forest 7, dist=34)
```

`ehenter2` is Edea's House - Entrance. The drive entered it 171 units from the
aim point; the marker it replaced was 600 units from the door and never opened
it in five builds. Chocobo Forest 7, the control, entered at **34 units** — its
aim moved 728 units and it is now dead-on rather than lucky.

Zero errors in the log. 51 `[ENTRYPATCH]` lines, all correct.

### The UNK21 opcode is a re-entry inhibit

One tick, and only one tick, in the whole run refused program 34:

```
[00:39:53] [TRIGWALK] seg=652 prog 34 -> refused: UNK21 bit (unk21 bit=1 needs 0)
[00:39:53] [ENTRYPATCH] standing INSIDE Edea's House's entry-polygon patch at (-29431,69634)
```

That is the frame he came back **out** of `ehenter2`, still standing on the entry
polygon. One second later he had stepped off and the bit was 0 again. So `0xFF21`
is not a world-state lock — it is the debounce that stops you falling straight
back through a door you just walked out of. v0.21.2 and v0.21.3 both spent effort
on the possibility that it was holding Edea's House shut. It never was, and this
run settles it.

It also means the gate is correctly implemented: it bit exactly once, exactly
where the engine needs it to.

### Known imprecision, observed

At Chocobo Forest 7 the `[ENTRYPATCH]` line said "standing INSIDE" for four ticks
before the field loaded. That is the documented bbox over-report — the rectangle
is a superset of the triangles — and/or the engine wanting a step *onto* an entry
polygon rather than a standstill on one. Harmless: the aim points carry margin
and a drive is always moving. Worth remembering before reading that line as
"the door should have opened this instant".

### Galbadia Missile Base: closed

Aaron: *"the base blows up after the mission on disc two, so there is no way to
enter it at this point in the game."* The absent entry polygons are correct, not
a gap in the scan. Removed from the open questions.
