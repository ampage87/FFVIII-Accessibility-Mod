# v0.21.7 — the five held-back retargets, resolved offline

**#79.** Not by measuring distances more carefully. By using the thing that
actually decides it.

## The authority

v0.21.6 assigned entry-polygon patches to catalog destinations by **nearest
centroid**, which is a guess, so anything needing a 1–2 km move was held back.

The authority is not distance:

> A patch sits in a **segment**. A segment has exactly **one** entry program. That
> program names its **destinations**, with clause coordinate bounds.

| destination | segment | program | dest | story gate | clause bounds |
|---|---|---|---|---|---|
| Deling City | 264 | 8 | 8 | ≥ 333 | none |
| D-District Prison | 361 | 16 | 10 | ≥ 350 | none |
| Trabia Garden | 149 | 3 | 19 | ≥ 750 | none |
| Winhill | 393 | 24 | 14 / 15 | ≥ 750 | **split at Xoff 6144** |
| Great Salt Lake | 373 | 21 | 24 | ≥ 1600 | none, foot only |

The first four markers are **in the same segment as their own patch**. That makes
the binding a fact, not an inference — the icon and the door are in the same
8192-unit square and the square has one program. The 1–2 km is just how far a
city icon sits from its gate.

## Winhill was the one worth holding back

Program 24 splits segment 393 at `Xoff 6144`: **destination 14 on the high side,
15 on the low.** The marker sits at Xoff 7059 — the high side. The v0.21.6
whole-patch aim came out at **Xoff 5825**, the low half, *a different destination*.

That would have been a confidently-wrong coordinate of exactly the kind this
table exists to prevent, shipped with a generated-therefore-trustworthy label on
it. The new aim is restricted to the high side, and the **whole bbox** clears the
split (Xoff 6147 at its west edge) so a drive arriving at the near edge cannot
open the wrong door. `entryaim_test` asserts both.

The low half is deliberately left uncovered, so the planner does not treat it as
a no-go zone for routes to other places.

## Great Salt Lake vs Chocobo Forest 3: settled

Program 21 has **no chocobo clause** and a **story ≥ 1600** gate. Every chocobo
forest's program has a vehicle-49 clause and no story gate whatsoever (programs
1, 2, 5, 31, 35, 36 — forests 2, 1, 4, 5, 7, 6). So segment 373 is not a forest,
and its patch is Great Salt Lake's — which fits a place that opens after the
Lunar Cry.

**Chocobo Forest 3's marker is in segment 374, and segment 374 has no walkable
entry polygon at all.** Its program (22) is foot-only too. So that marker is
wrong, and where it belongs is an open question rather than something to guess —
see the doc. Not touched.

## footOnly is now derived, not judged

A program with a **vehicle-132 (FOOT_ALT)** clause can be entered by car. That
rule reproduces all seven hand-set flags: Dollet and Balamb Town have 132 and are
`false`; Timber, Fire Cavern and Galbadia Station don't and are `true`. Applied
to the new rows it makes Deling City and D-District Prison car-enterable, which
the previous blanket `true` would have got wrong.

## A confirmation picked up on the way

`sub_544630` at `0x544702`, the world-map exit:

```
mov  ecx, [0x20409fc]      ; the polygon you left from
mov  eax, [0x20403a4]
mov  cl,  byte [ecx + 0xe] ; its flags
shr  cl,  3
and  cl,  1                ; bit 3 -- was it an entry polygon?
and  dl,  0xfe
or   cl,  dl
mov  byte [eax + 0x6d], cl ; ...into the UNK21 bit
```

The engine copies bit 3 of the departure polygon straight into the bit `0xFF21`
tests. That is the v0.21.6 BAT reading — UNK21 as a re-entry inhibit — as
instructions rather than as inference.

## Shipped

Five rows, `ENTRY_AIM_COUNT` 17 → 22.

| destination | aim | tris | margin | footOnly |
|---|---|---|---|---|
| Deling City | (-61947, -30631) | 48 | 374 | false |
| D-District Prison | (-55308, -6296) | 260 | 195 | false |
| Trabia Garden | (48728, -59396) | 194 | 420 | true |
| Winhill | (-51071, 6326) | 10 | 126 | true |
| Great Salt Lake | (48248, -2174) | 46 | 120 | true |

Trabia's patch spills into segment 150, where destination 19 additionally
requires `Yoff > 4096`; the aim is kept inside segment 149 where the program has
no bound at all.

## Gates

`entryaim_test` OK (0 bad), with three new assertions: Winhill's aim **and its
whole bbox** clear the Xoff 6144 split and are not the v0.21.6 coordinate; the
four same-segment bindings still aim inside their own marker's segment; and the
existing invariants (aim inside bbox, unique names, five field-proven points
still inside their areas, Edea's seven triangles, the Forest 7 control, no two
areas overlapping) all hold at 22 rows.

`trigwalk_test` OK · `trigseg_test` OK · `pathdecimate_test` OK · `vehsig_test`
OK · `catalog_story_test` 0 failures · `garden_aboard_test` 0 failures ·
`lint_seh` OK (88 files) · all four harnesses compile.

## BAT

Four of the five are testable at story 912:

- **Deling City** (gate ≥ 333)
- **D-District Prison** (≥ 350)
- **Trabia Garden** (≥ 750)
- **Winhill** (≥ 750) — the interesting one, since the aim moved to the other
  half of a split square

**Great Salt Lake is gated at story ≥ 1600** and cannot be confirmed until much
later. It ships because it is strictly better than the marker it replaces and
cannot regress anything that already refuses.

Whatever the `[DRIVE] Arrival via game-mode` line names as `fieldName` for
Winhill is worth reading — it will say which of destinations 14 and 15 the high
side actually is.
