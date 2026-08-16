# v0.21.8 — three things the BAT log said

**#79.** All four testable retargets arrived. The log then said three more
things, one of which is a real correction.

```
[01:04:59] Arrival via game-mode  fieldId=0x029E  target=Winhill                   dist= 403
[01:08:16] Arrival via game-mode  fieldName='glrent1'  target=Deling City          dist=  37
[01:08:59] Arrival via game-mode  fieldName='glrent1'  target=Tomb of the Unknown King  dist=   2
[01:16:46] Arrival via game-mode  fieldName='gnview1'  target=Trabia Garden        dist=1360
```

Deling City at 37 units and the Tomb at **2**. Zero real errors in 27,878 lines.

## Trabia's door is twice the size this table claimed

`dist=1360` is the tell. v0.21.7 restricted Trabia's aim to segment 149, where
program 3 grants destination 19 with no coordinate bound — the conservative
choice. But **program 4 grants the same destination 19 in segment 150 whenever
`Yoff > 4096`**, and the patch runs straight across the seam: 194 walkable entry
triangles in 149 and 172 more in 150.

The BAT entered at `(49966, -58834)` — Yoff 6702, well past the split, in segment
**150** — which is 1,363 units from the v0.21.7 aim and **818 units outside its
bbox**. The drive got in anyway, by passing through the eastern half on its way
to the western one.

Taking both halves puts the aim in the middle of the real 2048 × 2048 door and
raises its edge margin from 418 to **737**, the largest in the table. The whole
rectangle is safe to treat as one: its y range is Yoff 5120–7168, entirely above
the 4096 the segment-150 clause needs.

Being conservative about a clause is right. Being conservative about which *half*
of a door to aim at is just aiming at the far side.

## The D-District Prison door never opens

Aaron: *"it is not possible to enter the D-District Prison. You can see it on the
world map but can never enter it from the world map. It is entered via a story
sequence triggered in Deling City."*

The geometry is real and correct — program 16, 260 walkable entry polygons, story
≥ 350, and the BAT drove onto them — and no field loads. The prison is a mobile
drilling rig that relocates and sinks during the story, so its world-map presence
is most likely vestigial by disc 3.

**The row stays, deliberately.** The drive still delivers the player to the gate
for the equivalent experience Aaron asked for, and the planner still routes other
journeys around a real firing area. It is now written down so a future
"arrived but nothing loaded" here is not read as a regression and re-investigated
from scratch.

## Winhill has two doors and v0.21.7 picked the right one

Aaron: *"Winhill also has two entry points... the one that auto-drive took me to
now is the one in the north part of town."*

That is destination **14**, the high side of the Xoff 6144 split — the half
v0.21.7 moved the aim onto, and the half the v0.21.6 whole-patch aim would have
missed. Destination 15 is the other door and stays uncatalogued by his call. The
field is `0x029E`.

The bbox is now taken from the triangle **vertices** instead of the raster
samples the aim search uses, since the raster under-reports by up to one step.
Its west edge is Xoff 6145 — one unit inside the clause, because Xoff exactly
6144 satisfies neither `> 6144` nor `< 6143` and would open no door at all.

## A new gate, and an honest bound on it

`entryaim_test` now checks the four positions where the game actually let him
through — the only ground truth this table has.

It does **not** assert strict containment, because `lastPos` on the arrival line
is the drive's last sample, not the frame the engine ran its polygon test on. At
running speed that is over 100 units per tick, and checked against the raw
triangles all four entries are 59–166 units outside one. Asserting containment
would be asserting something the instrument cannot measure. The bound is the
sampling lag, 200 units.

It still catches the failure that matters: verified by running it against the
v0.21.7 row, where Trabia comes back **818 units outside** and the test fails.

## Not a bug, checked

The 9 km prison drive hit four `[NAVBLK-PRUNE]` releases before planning, two of
them on learned blocks at the Tomb, 30 km away. That reads like a leak and is
not: the prune picks the block **nearest the start** and keeps anything
`FootBlockedCached` calls genuinely blocked, so it released the two blocks
actually near the route first, then the only others left, and the plan succeeded.
Working as designed. Recorded so the next reader does not re-open it.

## Gates

`entryaim_test` OK (0 bad) — two new checks: the four BAT entry positions within
sampling lag of their own area (worst: Winhill, 11 units), and Trabia's bbox
entirely above the Yoff 4096 split. Plus the existing invariants at 22 rows.

`trigwalk_test` OK · `trigseg_test` OK · `pathdecimate_test` OK · `vehsig_test`
OK · `catalog_story_test` 0 failures · `garden_aboard_test` 0 failures ·
`lint_seh` OK (88 files) · all four harnesses compile.

## BAT

**Trabia Garden only** — it is the one coordinate that changed. Approaching from
the east should now enter at a much smaller `dist`. Nothing else in this build
alters a drive; Winhill's bbox moved by two units and the rest is comments.

## Loose end, minor

The arrival line named `glrent1` for both Deling City (field `0x02EE`) and the
Tomb (`0x0318`), and gave an empty name for Winhill (`0x029E`). Two different
field ids should not resolve to one name. It is cosmetic — the ids are right and
nothing depends on the string — but the field-name lookup is worth a look
sometime.
