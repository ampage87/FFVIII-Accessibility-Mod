## v0.20.97

#80: **Shumi Village was never where the catalog said it was.**

Aaron, after nine builds of beach machinery:

> *"That is not correct. Remember you already confirmed this via the game guide —
> Shumi is reachable as soon as you get control of Garden. I am confused why this
> one location has been such an issue. Go back to the game files and exe … Keep
> going until you have pinned down a definitive answer on (a) where Shumi is
> located on the world map and (b) how to reach it using the Garden."*

He was right to push. The destination coordinate was wrong, and every build from
.88 onward was engineering a cliff climb to reach a place that is not there.

### (a) Where Shumi Village is

The world map labels its own settlements, and byte 14 of a `wmx` polygon is the
label. It is the texture page, and **every named place on the world map sits on
a patch of page 8**.

`wmsetus` section 8 is a 62-record table of `int32 X, int32 Z, int16 height,
int16 facing` — the position the game puts you at when you step out of a field.
The heights confirm the decode outright: record 0 says −658 and the walkmesh
under it is −652; record 1 says −304 and the mesh says −304.

Line the two up and a fixed relationship appears. **The arrival record always
sits 600–1,300 units OUTSIDE the page-8 patch**, which is exactly what you would
expect: if it were inside the entry trigger you would re-enter the town the
instant you left it.

| place | patch centre | patch cells | arrival record | offset |
|---|---|---|---|---|
| Balamb Town | (12220, −26381) | 135 | (13249, −26779) | 1,102 |
| Timber | (−22016, −5696) | 144 | (−22564, −4867) | 993 |
| Deling City | (−61519, −30619) | 169 | (−61794, −29369) | 1,280 |
| Winhill | (−51173, 6354) | 68 | (−50364, 6337) | 809 |
| Dollet | (−14603, −39156) | 39 | (−15639, −39437) | 1,072 |
| Trabia Garden | (49152, −59392) | 184 | (49129, −58171) | 1,221 |

Winter Island — the 19,401-cell landmass in the far north, regions 9 and 17 —
has **exactly two page-8 patches**:

```
32 cells, terrain 1,  centred (10752, -80384)
71 cells, terrain 29, centred (12274, -83958)
```

**32 cells of page 8 is the Chocobo Forest signature.** There are seven such
patches map-wide and the six we had already named are all of them plus one
unclaimed (see below). So the 32-cell patch is Chocobo Forest 2, and the 71-cell
patch — town-sized, on the scale where Winhill is 68 and Balamb Town is 135 — is
the only other structure on the island.

**Shumi Village is at (12274, −83958).**

The shipped coordinate was **(10362, −76967)**: 7,248 units north-west, on no
feature at all, on a *different foot landmass* — 7,034 cells of cliff plateau
with, as .88 through .96 measured in increasing detail, no Garden landing
anywhere on it.

### The .92 conclusion was wrong, and the null result is why

.91 sent Aaron on foot to `wmsetus` record 20, (13000, −83977). He reached it to
within seven units, stood there fifty seconds, and nothing loaded. .92 concluded
record 20 was not Shumi and closed the question.

**Record 20 is 725 units due east of the dome.** He was standing exactly where
the game stands you when you walk *out* of Shumi Village — deliberately outside
the trigger, the same 600–1,300 unit offset as every other town. The
measurement was good; the inference from it was not. *A null result at one point
is evidence about that point, not about the hypothesis* — and the offset that
makes it a null result was visible in the same table the point came from.

### (b) How to reach it with the Garden

Ordinarily. There is no beach to climb.

```
Shumi Village   berth (12672,-81536)  walk 2435  clear 6
```

That berth is on the **same 6,216-cell foot landmass** the Garden already parks
on for Chocobo Forest 2, **1,280 units from that berth** — ground Aaron has
already driven to and stood on. Sixteen `gexec3` routes (four starts × four
headings) arrive with **zero replans**, no beach exception, no approach point.

**No `beach_climb` berth remains in the table.** The machinery stays in the
source — `GdBeachOpen`, `PARTIAL`, the approach-point routing, the beach run —
because it is correct and the shelf rule it rests on is confirmed against the
engine at 590/590. But nothing arms it, and the harness now prints the count so
that stays visible rather than assumed.

### Byproduct: a Chocobo Forest we have never had

The seven 32-cell page-8 patches are at (44032, 75776), (−20992, 69632), (6080,
55296), (17920, 22528), (11520, −64256), (10752, −80384) and **(51968, −64256)**.
The last has no catalog entry and is 6,989 units from Trabia Garden — matching
the walkthroughs' *"North of Trabia Garden"* forest, with `wmsetus` record 35
(52145, −63615) as its arrival point at the usual offset. Meanwhile catalog
`Chocobo Forest 3` (51893, −3959) has no page-8 patch within 3,936 units and
`Chocobo Forest 4` (97253, −48250) sits by a 47-cell terrain-2 patch that is not
the forest signature. **Not changed in this build** — flagged for Aaron, because
two more wrong markers of exactly the class just fixed should be fixed
deliberately, not in the same commit that fixes the first.

### Verification

* **Parity**: WALK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / PARTIAL / CLEAR all
  identical between the C++ and `gsim3`; PARK differs by 1 (documented
  tolerance).
* **Berth generator** re-run against the corrected catalog: Shumi lands on
  (12672, −81536) walk 2,435 clear 6, inside the 2–3 km band, on the
  destination's own landmass. 24 of 39 destinations have a valid berth.
* **Route regression**: 24 berths × 4 starts × 4 headings, **372 runs, 0
  failures** — and **nothing is skipped**, where .96 had to skip Shumi as
  unrepresentable offline.
* `tests/garden_harness.cpp`: grid 678,223/786,432, 74,184 parkable,
  reachability 662,681, **25 ok / 0 bad**, `beach_climb berths in the table: 0`,
  `Shumi plan from Balamb: OK (381 waypoints)`, `Shumi berth to Chocobo Forest 2
  berth: 1280 units OK (same shore)`. `tests/garden_aboard_test.cpp`: ALL CHECKS
  PASSED.
* Both harnesses clean under `-Wall -Wextra`; every source file inside the 80 KB
  guard.

**NOT MSVC-built, NOT BAT'd.**

**BAT**: board the Garden, confirm the catalog still lists 25 destinations with
Shumi Village among them, and drive to Shumi Village. Expect an ordinary arrival
— `parkBit=1 canDisembark=1`, a walk of about 2.4 km — with no `beach` lines in
the log at all. Then step off and take the foot drive in: the village entrance
should load on the approach, from the east, before you reach the dome itself.
