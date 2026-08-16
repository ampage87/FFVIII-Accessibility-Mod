# World-map field entry: the polygon gate

**v0.21.6 (#79).** What actually decides whether a field loads when you walk
somewhere on the world map, why Edea's House refused for five builds, and what
else in the catalog has the same defect.

---

## The rule, in one line

A field entry fires only while the player stands on a **wmx.obj polygon whose
byte `0x0E` has bit 3 set**, inside a program's 8192-unit segment, with the
story, vehicle and UNK21 gates satisfied.

`sub_545EA0` — the walker that evaluates the 38 entry programs — opens with:

```
    mov  eax, [0x020409FC]         ; the polygon under the player
    test byte ptr [eax+0x0E], 8    ; is it an entry polygon?
    jne  walk
    xor  eax, eax
    ret                            ; not one: no program is evaluated at all
```

`[0x020409FC]` churns every frame because **the polygon under your feet changes
every frame.** v0.21.3 read that as a broken instrument and retracted it. It was
not broken; it was measuring exactly the right thing and the reading was correct
— bit 3 clear, everywhere Aaron walked.

## Two flags, not one

Being an entry polygon is necessary and not sufficient. The polygon must also be
one the player can stand on, and that is a **different byte**:

| byte | bit | meaning | source |
|------|-----|---------|--------|
| `0x0E` | 3 | field-entry trigger | `sub_545EA0`'s first test |
| `0x0F` | 7 | foot-walkable | move validator `0x54B860`, already used by the navmesh feed |

Map-wide there are **7,359** entry polygons and only **2,877** are foot-walkable.
The gap is where this bug lived.

## Why Edea's House refused

Segment 652 holds **103 entry polygons. Seven are foot-walkable.** The other 96
are the orphanage building — ground the move validator will not let Squall onto.

The seven cover `x[-29975,-29310] y[69632,70078]`: about 665 × 446 units, roughly
**a three-hundredth of the segment.**

The shipped marker was `(-28950, 70090)` — 234 units east of that patch. It came
from correct page-8 texture archaeology (v0.21.1 found the right 103-polygon
structure) followed by one plausible inference too many: *"the EAST face has the
most terrain-29 apron, and east is the side the player arrives from."* East is
where the building's wall is.

In the 2026-08-16 BAT Aaron's closest approach was `(-29144, 70004)` — **287
units short**, with every gate the exe names passing on every frame, because they
all genuinely did pass. He was never standing on a trigger polygon.

The v0.21.6 aim is `(-29459, 69772)`: the interior point of those seven triangles
farthest from their edge, 140 units of margin in every direction.

## Why the v0.21.5 interpreter said MATCH anyway

`world_map_trigwalk.inl` models the segment test and the clause coordinate
bounds. Programs 34 and 35 have **no coordinate clauses at all** — just a vehicle
predicate and a destination — so the model's finest positional unit is the whole
8192-unit square. It printed `MATCH, destination 18` at twenty-three positions
spanning 8 km of Centra while nothing loaded, and the log line said *"every
condition the exe names is satisfied and the refusal is elsewhere."*

That read as a finding. It was a blind spot with a confident sentence attached.

The half it could not see is the polygon gate, which is now reported on its own
`[ENTRYPATCH]` line beside every `[TRIGWALK]` verdict.

## The table was right and seven rows long

`world_map_trigger_data.inl` has carried this comment since **v0.18.3.206**:

> a field entry fires only while standing on a wmx poly with byte14 bit 3 set

Three sessions of Edea's House hunting then went looking for a story gate, a
master gate and a destination table. The knowledge was in the file the whole
time. The actual gap was that `s_entryAims` covered **7 of 40 destinations** and
Edea's House was not one of them.

**Method note for next time: when a decode is confidently wrong, check whether an
earlier decode in the same file already said the right thing. It is cheaper than
re-reading the exe, and it is where this answer was sitting.**

## The generator, and why it can be trusted

`offline/gen_entryaims.py`:

1. every polygon with byte `0x0E` bit 3 set → 7,359 map-wide
2. keep those also foot-walkable (byte `0x0F` bit 7) → 2,877
3. flood-fill into patches; take the patch nearest each catalog marker
4. aim = the interior sample farthest from the patch edge

Run it against the five aim points that were found **by hand, in the field, over
several BATs, before any of this scanning existed** and it returns:

| destination | hand-proven | generated | error |
|---|---|---|---|
| Timber | (-22580, -5291) | (-22529, -5216) | 91 u |
| Dollet | (-14513, -39119) | (-14551, -39080) | 54 u |
| Balamb Town | (12560, -26800) | (12544, -26756) | 47 u |
| Fire Cavern | (30239, -29528) | (30194, -29458) | 83 u |
| Galbadia Station | (-38914, -24767) | (-39291, -24810) | 379 u |

All five land inside their own proven bboxes. `tests/entryaim_test.cpp` asserts
that permanently.

Two further corroborations: Balamb Town's proven box holds **43 entry polygons of
which 7 are foot-walkable** — the same 103/7 shape as Edea's House, and its
proven aim sits in the walkable seven. And every generated aim falls in a segment
that carries an entry program in the independently-built `TRIG_PROGRAMS` table.

Balamb Garden and Galbadia Garden contain **zero** entry polygons in their boxes:
both are mobile objects whose entrances move with the story, so they come from a
different mechanism. Their rows are untouched.

## Shipped in v0.21.6

Rows added where the generated aim is within **800 units** of the existing
marker — unambiguous "the marker is just outside its own door":

| destination | aim | patch tris | edge margin | marker was |
|---|---|---|---|---|
| Edea's House | (-29459, 69772) | 7 | 140 | 600 u out |
| Tomb of the Unknown King | (-42011, -36843) | 64 | 204 | 539 u |
| Centra Ruins | (6143, 55295) | 291 | 259 | 744 u |
| Shumi Village | (12743, -84026) | 8 | 50 | 58 u |
| Chocobo Forest 1 | (11507, -64253) | 32 | 294 | 619 u |
| Chocobo Forest 2 | (10740, -80384) | 32 | 294 | 653 u |
| Chocobo Forest 4 | (97792, -48650) | 98 | 336 | 671 u |
| Chocobo Forest 5 | (17908, 22528) | 32 | 294 | 735 u |
| Chocobo Forest 6 | (44020, 75776) | 32 | 294 | 684 u |
| Chocobo Forest 7 | (-21004, 69632) | 32 | 294 | 728 u |

Chocobo Forest 7 is the **known-good control**: it opened on demand in an earlier
session from `(-20953, 68906)`, and that point is *outside* its patch. Every one
of these worked by luck of the arrival radius, not by aim. Retargeting makes them
deterministic — and it is the reason Forest 7 should be re-walked in the BAT.

## Held back in v0.21.6, resolved in v0.21.7

The five were held back because v0.21.6 bound patches to destinations by
**nearest centroid**, which is a guess. The authority is not distance:

> A patch sits in a **segment**. A segment has exactly **one** entry program.
> That program names its **destinations**, with clause coordinate bounds.

| destination | segment | program | dest | story | clause bounds | aim | margin |
|---|---|---|---|---|---|---|---|
| Deling City | 264 | 8 | 8 | ≥ 333 | none | (-61947, -30631) | 374 |
| D-District Prison | 361 | 16 | 10 | ≥ 350 | none | (-55308, -6296) | 195 |
| Trabia Garden | 149 | 3 | 19 | ≥ 750 | none | (48728, -59396) | 420 |
| Winhill | 393 | 24 | 14 / 15 | ≥ 750 | **split at Xoff 6144** | (-51071, 6326) | 126 |
| Great Salt Lake | 373 | 21 | 24 | ≥ 1600 | none, foot only | (48248, -2174) | 120 |

The first four markers sit **in the same segment as their own patch**, so the
binding is a fact rather than a nearest-neighbour guess: icon and door are in the
same 8192-unit square, and the square has one program. The 1–2 km is simply how
far a city icon sits from its gate.

### Winhill was the one worth holding back

Program 24 splits segment 393 at `Xoff 6144` — destination 14 high, 15 low. The
marker is at Xoff 7059 (high). The v0.21.6 whole-patch aim came out at **Xoff
5825: the low half, a different destination.** Shipping it would have produced a
confidently-wrong coordinate carrying a generated-therefore-trustworthy label.

The aim is now restricted to the high side and its **entire bbox** clears the
split (Xoff 6147 at the west edge), so a drive arriving at the near edge cannot
open the wrong door. The low half is left uncovered so the planner does not treat
it as a no-go zone for other routes.

### Great Salt Lake vs Chocobo Forest 3

Program 21 has **no chocobo clause** and a **story ≥ 1600** gate. Every chocobo
forest program has a vehicle-49 clause and no story gate at all — programs 1, 2,
5, 31, 35, 36, which are forests 2, 1, 4, 5, 7 and 6. Segment 373 is therefore
not a forest, and the patch is Great Salt Lake's, which fits a location that
opens after the Lunar Cry.

Great Salt Lake's gate means it **cannot be BAT-confirmed at story 912.** Shipped
regardless: strictly better than the marker it replaces, and it cannot regress
anything that already refuses.

### footOnly is derived, not judged

A program carrying a **vehicle-132 (FOOT_ALT)** clause is car-enterable. The rule
reproduces all seven hand-set flags — Dollet and Balamb Town have 132 and are
`false`; Timber, Fire Cavern and Galbadia Station do not and are `true`. Applied
to the new rows it makes Deling City and D-District Prison car-enterable, which
v0.21.6's blanket `true` would have got wrong.

## Chocobo Forest 3 is in the wrong place

Its marker `(51893, -3959)` is in **segment 374**, and segment 374 has no
walkable entry polygon at all. Its program (22, destination 25) is foot-only, so
it is not a forest entrance either.

Six of the seven forests are accounted for by programs 1, 2, 5, 31, 35 and 36.
The only unclaimed forest-shaped program — chocobo clause, no story gate — is
**program 14, segment 279**, which splits at `Xoff 2560` into destinations 22 and
23, with walkable patches at `(59348, -31753)` and `(61205, -30894)`.

That is **29 km** from where Chocobo Forest 3 is catalogued, so this is a
candidate and not a conclusion. Aaron's game knowledge or a captured entry should
settle it; guessing is what cost five builds at Edea's House.

## Every entry program, and what is bound to it

Twenty of the 38 programs now have a catalog destination bound to them. The
remaining eighteen, for reference:

| prog | seg | story | dests | markers in that segment |
|---|---|---|---|---|
| 4 | 150 | ≥750 | 19, 35 | — (Trabia's patch spills in here; dest 19 needs Yoff>4096) |
| 10 | 268 | 0 | 5 | — |
| 12 | 274 | 0 | 0 | — |
| 14 | 279 | 0 | 22, 23 | — (the unclaimed forest, above) |
| 15 | 327 | ≥350 | 11 | Galbadia Missile Base |
| 18, 19, 20 | 370 | ≥3900 / ≥636 | —, 13, 12 | Fisherman's Horizon |
| 22 | 374 | 0 | 25 | Great Salt Lake, Esthar City, Chocobo Forest 3 |
| 23 | 378 | ≥1750 | 28, 57 | — |
| 25, 26, 27, 28 | 406, 407, 438, 439 | ≥1750 | all include 26 | — |
| 29 | 441 | ≥1750 | 30, 69 | Sorceress Memorial |
| 30 | 443 | ≥1750 | 29, 47 | — |
| 32 | 506 | ≥1750 | 31, 48, 49 | Tears' Point |
| 37 | 705 | 0 | 21 | Deep Sea Research Center |

Three things stand out and are worth a later pass, all story-gated past where the
save currently is so none is urgent:

- **Four programs share destination 26** (segments 406, 407, 438, 439, all
  ≥ 1750). Four gates into one place, which is what Esthar City should look like.
  Esthar City's marker is in segment 374 with the others, so it is probably
  misplaced the same way Chocobo Forest 3 is.
- **Sorceress Memorial** (prog 29) and **Tears' Point** (prog 32) each have their
  marker in the right segment and a real patch — 248 and 24 triangles — so both
  are straightforward retargets whenever disc 4 arrives.
- **Galbadia Missile Base** (prog 15) has an entry program requiring story ≥ 350
  but **no entry polygons anywhere in its segment.** Combined with Aaron's *"the
  base blows up after the mission on disc two"*, the likely explanation is that
  the world map swaps terrain blocks by story state and `wmx.obj` holds the
  post-destruction version. Worth confirming before trusting any patch scan for
  a location whose terrain changes.

## `0xFF21` in the exe

The v0.21.6 BAT read UNK21 as a re-entry inhibit from behaviour. `sub_544630` at
`0x544702` says it in instructions — the world-map exit copies bit 3 of the
polygon you left from straight into the bit `0xFF21` tests:

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

## No entry polygons at all

Thirteen catalog destinations have no foot-walkable entry polygon anywhere near
them, which is correct — they are not walked into:

Fisherman's Horizon (entry polygon 6 u away but not foot-walkable — the platform),
Deep Sea Research Center (149 u, not foot-walkable — Ragnarok only), Esthar City,
Lunatic Pandora Lab, Lunar Gate, Sorceress Memorial, Tears' Point, Cactuar Island,
Island Closest to Hell, Island Closest to Heaven, Alien Ships 1–3.

**Galbadia Missile Base — closed, 2026-08-16.** It has no entry polygon within
17 km, and Aaron's answer is that there should not be one: *"the base blows up
after the mission on disc two, so there is no way to enter it at this point in
the game."* The scan is right; the destination is a landmark you walk *from*, not
into. It stays in the catalog as a navigation target.

## Side effect: fewer accidental entries

`world_map_planner2.inl` prices any route cell inside a **non-target** firing area
at +4096 (~32 cells). Going from 7 areas to 17 means ten more real doors the
planner now routes around instead of walking through. Accidental field entry
while passing a town has been a recurring hazard; this reduces it by exactly the
geometry the engine uses.

---

## BAT-CONFIRMED 2026-08-16

```
[00:39:49] [DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x01F9,
           fieldName='ehenter2', target=Edea's House, dist=171)
[00:40:16] [DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x0125,
           fieldName='cwwood7', target=Chocobo Forest 7, dist=34)
```

Edea's House opened after five builds of refusing, entered 171 units from the
generated aim. Chocobo Forest 7 — the control, whose aim moved 728 units —
entered at **34 units**: dead-on rather than lucky. Zero errors in the log; 51
`[ENTRYPATCH]` lines, all correct.

That is the generator validated against a destination nobody had ever reached,
which is the thing the five hand-proven cross-checks could only make plausible.

### `0xFF21` (UNK21) is a re-entry inhibit

Exactly one tick in the whole run refused program 34:

```
[00:39:53] [TRIGWALK] seg=652 prog 34 -> refused: UNK21 bit (unk21 bit=1 needs 0)
[00:39:53] [ENTRYPATCH] standing INSIDE Edea's House's entry-polygon patch at (-29431,69634)
```

That is the frame he came back **out** of `ehenter2`, still standing on the entry
polygon he had just used. A second later he had stepped off and the bit was 0.

So the opcode is not a world-state lock — it is the debounce that stops you
falling straight back through a door you just walked out of. v0.21.2 and v0.21.3
both treated "UNK21 might be holding Edea's House shut" as a live hypothesis. It
never was, and this settles it. It also shows the gate is implemented correctly:
it bit once, precisely where the engine needs it to.

The handler makes sense in that light:

```
if ([0x2040A34] != 0)                       pass          ; inhibit disabled
else require ((byte[[0x20403A4]+0x6D]) & 1) == operand     ; the just-exited flag
```

### Observed imprecision in `[ENTRYPATCH]`

At Chocobo Forest 7 the line read "standing INSIDE" for four ticks before the
field loaded. Two candidate causes, both benign: the bbox is a rectangle and
therefore a superset of the triangles, and the engine may want a step *onto* an
entry polygon rather than a standstill on one. The aim points carry margin and a
drive is always moving, so neither bites — but do not read a single "INSIDE" line
as "the door should have opened this instant".

---

## BAT-CONFIRMED 2026-08-16 (v0.21.7) and what it corrected (v0.21.8)

```
Winhill                   fieldId=0x029E             dist= 403
Deling City               fieldName='glrent1'        dist=  37
Tomb of the Unknown King  fieldName='glrent1'        dist=   2
Trabia Garden             fieldName='gnview1'        dist=1360
```

All four arrived. Zero real errors in 27,878 lines.

### Trabia's door is two segments wide

`dist=1360` was the tell. v0.21.7 gave Trabia only segment 149 — program 3,
destination 19, no coordinate bound — as the conservative choice. **Program 4
grants the same destination 19 in segment 150 whenever `Yoff > 4096`**, and the
patch runs across the seam: 194 walkable entry triangles in 149, 172 more in 150.

He entered at `(49966, -58834)` — Yoff 6702, segment 150 — 1,363 units from the
aim and **818 outside its bbox**, getting in only by passing through the eastern
half on the way to the western one. The corrected area covers both: aim
`(49096, -59393)`, bbox `x[48128,50176] y[-60416,-58368]`, edge margin **737**,
the largest in the table. The rectangle is Yoff 5120–7168 throughout, entirely
above the split.

Being conservative about a clause is right. Being conservative about which *half*
of a door to aim at is just aiming at the far side.

### The D-District Prison door never opens

Aaron: *"it is not possible to enter the D-District Prison. You can see it on the
world map but can never enter it from the world map. It is entered via a story
sequence triggered in Deling City."*

The geometry is real — program 16, destination 10, story ≥ 350, **260 walkable
entry polygons**, and the drive went onto them — and no field loads. The prison
is a mobile drilling rig that relocates and sinks during the story, so its
world-map presence is most likely vestigial by disc 3.

The row stays deliberately: the drive still delivers the player to the gate for
an equivalent experience, and the planner still routes other journeys around a
real firing area. **A future "arrived but nothing loaded" here is expected, not a
regression.**

### Winhill's two doors

Aaron: *"the one that auto-drive took me to now is the one in the north part of
town."* That is **destination 14**, the high side of the Xoff 6144 split, field
`0x029E` — the half v0.21.7 moved onto, and the half the v0.21.6 whole-patch aim
would have missed. Destination 15 is the other door and stays uncatalogued.

The bbox now comes from triangle vertices rather than the raster samples the aim
search uses, since the raster under-reports by up to one step. Its west edge is
Xoff 6145: exactly 6144 satisfies neither `> 6144` nor `< 6143`, so a bbox edge
there would promise ground that opens nothing.

### `lastPos` is not where the trigger fired

Checked against the raw triangles, **all four** entry positions are 59–166 units
outside any entry polygon. The arrival line's `lastPos` is the drive's last
sample, not the frame the engine ran its polygon test on, and at running speed
that is over 100 units per tick.

This matters for reading any future log: an entry that fires "just outside" the
patch is the instrument, not the geometry. `entryaim_test` bounds the four known
entries by 200 units rather than asserting containment — and still fails on the
v0.21.7 Trabia row, at 818.

### Not a bug

The 9 km prison drive produced four `[NAVBLK-PRUNE]` releases, two on learned
blocks at the Tomb 30 km away. That reads like a leak and is not: the prune picks
the block **nearest the start** and keeps anything `FootBlockedCached` calls
genuinely blocked. It released the two actually near the route first, then the
only others left, and the plan succeeded.

### Cosmetic loose end

The arrival line named `glrent1` for both Deling City (field `0x02EE`) and the
Tomb (`0x0318`), and gave an empty name for Winhill (`0x029E`). Two ids should
not resolve to one name. The ids are right and nothing depends on the string, but
the field-name lookup is worth a look.
