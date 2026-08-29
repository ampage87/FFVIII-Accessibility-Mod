# Field catalog review — what the engine actually says

Written the night of 2026-08-22, in response to: *"we seem to be having more
phantoms lately"* and *"sometimes an NPC is misclassified as an Item and
vice-versa."* Both symptoms turned out to be downstream of the same thing: the
mod's model of the field-script file format disagreed with the engine in five
separate places. This document records what the engine does, how it was
established, and what each correction changed — measured across all 866 fields
on the disc rather than argued from one example.

Shipped as **v0.58.0** (the three ordering corrections + the identity join) and
**v0.59.0** (the instruction decode). They are separate versions on purpose:
v0.59.0 is much the larger behavioural change, and testing them in order makes a
regression easy to place.

---

## 1. Where the ground truth comes from

`field_scripts_init` at **FF8_EN.exe 0x0052BC00** builds a field's script objects
in four consecutive loops. Reading it settles three orderings this codebase had
been guessing at since v0.07.73:

```
0x0052BDAC   cl=[jsm+1] -> nLines    dl=[jsm+0] -> nDoors
             cl=[jsm+3] -> nOthers   dl=[jsm+2] -> nBackgrounds
             ecx = jsm+8             -> the group-word array

loop 1 @0x0052BF32  count=nLines        exec tag 0x20000000  stride 0x1A0
loop 2 @0x0052C02A  count=nDoors        exec tag 0x40000000  stride 0x18C
loop 3 @0x0052C13B  count=nBackgrounds  exec tag 0x80001000  stride 0x1B4
loop 4 @0x0052C270  count=nOthers       exec tag 0x10080002  stride 0x264
```

All four walk the **same** running group pointer (`0x01D9CDEC`) forward, and each
writes its object into the shared table at `0x01D9D020` at an index built from
the counts.

| | order |
|---|---|
| `.jsm` group-word array | **Lines, Doors, Backgrounds, Others** |
| runtime object table `0x01D9D020` | **Others, Lines, Backgrounds, Doors** |
| `.sym` leading bare-name list | **Others, Lines, Backgrounds** (doors omitted), each block in code order |

The SYM ordering was verified independently against all 849 fields that ship a
`.sym`: 848 agree exactly, and the single holdout (`cwwood6`) swaps two Others
names between its own two halves.

Other addresses used below:

| what | where |
|---|---|
| VM instruction decoder | `0x00530760` |
| VM dispatch | `0x0052A647`, table at `0x00B8DE94` |
| `REQ` / `REQSW` / `REQEW` | `0x0051CD60`, indexes the object table at `0x0051CD8D` |
| `SET3` (0x1E) | `0x0051D780` — pops z,y,x into `[0x198]/[0x194]/[0x190]` (<<12); **inline param is the walkmesh triangle** -> `[0x1FA]` |
| `SETLINE` (0x39) | `0x0051DC30` — pops six words into `[0x192]`..`[0x188]` |
| `MAPJUMP` (0x29) / `MAPJUMP3` (0x2A) | `0x00521A20` / `0x00521AC0` — pop 4 / 5; the **deepest** argument is the destination field (`0x01CE4762`); the inline param is the entrance id (`0x01CE476C`) |
| `MAPJUMPO` (0x5C) | `0x00521C30` — pops **two**, not four |
| `SETMODEL` (0x2B) | `0x0051D8F0` — model id is the inline param -> `[0x218]` |
| ladder-family (0x25-0x28) | `0x00525900` — writes a movement mode into `[0x23C]` **on the entity that runs it** |
| pop to savemap | `0x0B` `0x0051CCA0`, `0x0D` `0x0051CCD0`, `0x0F` `0x0051CD00` — store at `[param + 0x01CFE9B8]` |
| opcode `0x1C` | `0x0051D710` — `mov eax,1; ret`. A no-op. |

---

## 2. The five corrections

### 2.1 The group array is L, D, B, O — not D, L, B, O

On the 79 fields carrying both doors and lines, the first `min(nD, nL)` groups
had Door and Line swapped. **30 real trigger Lines carrying a MAPJUMP** were
therefore typed `JSM_ENT_DOOR` and never scanned as exits — `to_fhtown23`,
`jump0`, `jumpline0`, `winedoor`, `eventline0`, `swapline0`, `Jump1_3` and
friends, across 28 fields.

### 2.2 A REQ operand is a runtime slot, not a group index

`REQ` indexes `0x01D9D020` directly with its inline parameter, and that table is
ordered Others-first. The scanner was using the value as a group index, so on
essentially every field with any non-Other entity the "is this entity a REQ
target?" set was shifted — and the director gate keeps or drops an entity on
exactly that signal.

### 2.3 The SYM bare list is O, L, B — and the heuristic that hid it

The scanner assumed L, B, O. A v0.20.26 patch papered over the difference by
guessing each SYM name's category from whether it declared an `across` / `talk` /
`push` method, then remapping per category — and falling back to the broken
arithmetic whenever the per-category counts didn't line up.

On **ecenter1, ecmway1 and ecoway1** — three of the largest Esthar fields — the
guess failed. On `ecoway1` every one of its 35 entity names was on the wrong
entity, rotated by `nBackgrounds`:

```
group  cat  start   TRUE name          what the mod called it
0      B    219     road               Squall
10     O    0       Squall             Jiji
28     O    184     cardgamemaster     light2
```

Across the disc, **150 of 849 fields carried wrong entity names**, 41 of them
completely wrong. `JSMOrderMap` derives the mapping from the group words; the
heuristic is deleted.

### 2.4 NPC vs Item was decided by shared floor tiles

The relabel asked *"does any script entity share this walkmesh triangle and grant
an item?"* — a workaround adopted because names were unreliable. They were, from
2.3. But the workaround has a failure mode of its own: two entities standing on
one triangle swap identities. A person standing where a magazine lies is
announced as "Item", and the magazine, having been claimed, never surfaces at
all. That is the symptom, exactly.

The join is now an identity. Runtime Others slot *i* is JSM group
`nLines + nDoors + nBackgrounds + i`, recorded as `JSMEntityInfo::runtimeSlot`.

### 2.5 The instruction decode (v0.59.0)

```
w = code[ip]
if ((w & 0xFF000000) == 0)  opcode = w          ; NO parameter
else                        opcode = w >> 24    ; param = sign_extend24(w)
```

A word whose high byte is zero is an **opcode** — that is how everything above
0xFF is encoded (MENUSAVE `0x12E`, ADDITEM `0x125`, DRAWPOINT `0x137`, CARDGAME
`0x13A`, SETDRAWPOINT `0x155`). The scanner read those as literal pushes, which
is what forced the "opcode 0x1C is a prefix that pops the real opcode off the
stack" theory; `0x1C` is a no-op that appears in 92.8% of all entities.

Literals reach the stack only through a push opcode. Walking every handler in the
table and counting writes to the stack pointer at `[ctx+0x184]` gives the whole
push set:

| opcode | handler | what it pushes |
|---|---|---|
| `0x07` PSHN_L | `0x0051C990` | **the inline parameter** — a literal |
| `0x13` | `0x0051CD30` | the inline parameter — a literal |
| `0x08` PSHL | `0x0051CAB0` | a local, `[ctx + n*4 + 0x140]` |
| `0x0A` / `0x0C` / `0x0E` | | field variable bank `0x01CFE9B8` |
| `0x10` `0x11` `0x12` | | computed |
| `0x04` CALL | `0x0051C530` | the return IP — never an argument |
| `0x05` | `0x0051C570` | all eight locals — never an argument |

The old model had `0x07` as "PSHM_W, push a word from memory" and turned every
**non-negative** parameter into a fake "value comes from runtime memory" marker.
**8289 of the 8625 SET3 placements on the disc have at least one non-negative
coordinate**, so 96% of every entity position in the game was discarded as
unresolvable. That is what the triangle-centroid approximation, the late-resolve
pass and the struct-position scan have all been compensating for.

Arguments are now taken from the contiguous run of push opcodes immediately
before the consumer — the shape the compiler emits — instead of from a stack
simulated across a whole method. Measured run lengths confirm it:

```
SET3  3 args x8625     SET 2 x981      MAPJUMP 4 x412
MAPJUMP3 5 x1581       MAPJUMPO 2 x1037   SETDRAWPOINT 1 x130
```

**The independent check:** the SET3 triangle is the opcode's inline parameter and
the coordinates are popped values, so they agree only if both are read correctly.
Decoded correctly, **4539 of 4593 static positions (98.8%) land strictly inside
the triangle their own SET3 names.** The 29 misses are off-mesh cutscene
placements on `glstage2` (the FH concert) and `ewbrdg2`.

Three more mappings fell out of the same table:

- **The pop-to-savemap opcodes are `0x0B`/`0x0D`/`0x0F`, not `0x08`.** `0x08` is
  PSHL, a *push* of a local. Half the variable-write record was reading the wrong
  opcode and the other half was missing POPM_W and POPM_L entirely — and
  `foundNonInitVarWrite` feeds NPC-vs-Item.
- **MAPJUMPO takes two arguments, not four.**
- **`0x25`-`0x28` do not mark a ladder.** They set a movement mode on whoever
  *runs* them. 244 entities carry them and 113 are party characters; in
  `glwater3` and `glwater2`, where the mod's ladder support was developed, the
  ladder objects (`hasigo`, `hasigomodel`, `ladline0..7`) carry **none** and only
  the six party members do. With the decode corrected this would have started
  announcing Squall as a Ladder.

---

## 3. What changed, measured over all 866 fields

Old scanner vs new, same input, `tests/jsm_scan_harness`:

| | v0.58.0 | v0.59.0 |
|---|---|---|
| entities with a real position | — | **+2984** |
| entities that lost a fabricated position | — | 51 |
| Map Exit | 375 | **810** |
| Card Game | 0 | **98** |
| Draw Point | 114 | 227 |
| NPC | 11 | 134 |
| Interactive Object | 2104 | 454 |
| Save Point | 141 | 123 |
| flagged as an item pickup | 1315 | 89 |

**Every one of the 51 "lost" positions belongs to an entity that has no SET or
SET3 anywhere in its script** — they were coordinates read off a drifting
simulated stack. The names are the giveaway: `director0`, `moviedir`, `tmpdir`,
`director1..5`, `check`, `map`. Invisible script controllers, given a spot on the
player's map. That is the phantom class, named.

The 1650-entry drop in Interactive Object is the `hasExtDispatch` promotion
going away. It fired whenever opcode `0x1C` appeared — 92.8% of entities — and
every consumer used it as `hasDialogAny || hasExtDispatch`, a fallback for "this
might talk". With the decode corrected, MES/ASK/AMES/AASK are visible directly.

Card Game 0 -> 98 means **every Triple Triad opponent in the game was invisible
to the catalog.**

### Item pickups, second pass

"Wrote a savemap variable outside init" matched 876 entities across 400 fields —
Raijin, Fuujin, Cid, most of Balamb. Any NPC that remembers having been spoken to
writes such a flag. What actually marks a collectible is that it **gates
itself**: init READS a variable to learn whether it has already been taken, an
interaction method WRITES that same variable, and then it hides. ADDITEM
(`0x125`) is the other, now-exact path, and a collectible is never talked to.

89 entities match. Timber Maniacs (`Urakata`), the Weapons Monthlies (`Buki1`,
`Buki2`), `hon`, `book`, `huruzassi3` and the `itemkun` drops all survive; the
townsfolk do not.

---

## 4. A separate phantom source: the post-battle line backup

Unrelated to the file format. The v0.20.45 post-battle trigger-line preservation
was a **function-local `static`** inside `RefreshCatalog`, keyed only on the field
id and never invalidated. It exists for one case: the engine returning from a
battle re-runs field-scripts init (clearing the captured lines) but does not
re-fire SETLINE.

Because it outlived the visit that filled it, leaving a field and coming back in a
story state whose scripts run no SETLINE at all restored **the previous visit's
lines** as exits and interactions that no longer exist. It also made the catalog
harness emit a stray `Interaction 1` in every fixture after the first one that
declared a line — which is how it was found.

It now lives at file scope and is dropped by `HookedFieldScriptsInit` whenever the
field name changes. A battle return keeps it, which is the case it is for.

---

## 5. Test infrastructure

- **`tests/jsm_scan_harness`** runs the *real* `ScanJSMScripts` on the build host
  over the real field files and diffs all 866 fields against
  `tests/jsm_scan_golden.txt`. Nothing on this side of the build could execute
  the scanner at all before: `field_archive.cpp` is a Win32 translation unit and
  its archive reader wants the game's 294 MB `field.fs`. `tests/winshim` supplies
  a `windows.h`, `FF8OPC_ARCHIVE_TEST_SEAM` supplies the files, and
  `fieldsim/extract_fields.py` regenerates them from the disc. Every number in
  section 3 came from this harness.
- **`tests/jsm_order_test`** checks the order map against the two independent
  halves of the real `.sym` for 16 fields. 9 mutations applied, 9 killed.
- **`tests/jsm_decode_test`** checks the encoding against 59 real SET3 sequences
  plus the walkmesh geometry each one must produce, and asserts the *old* reading
  resolves none of them. 7 mutations applied, 7 killed.
- **`tests/catalog_harness` runs again.** It has been compile-only since v0.20.48,
  dying on the seventh of 33 fixtures because the draw-point presence gate reads
  three hardcoded engine addresses and g++'s `__try` is a no-op. The pages are
  mapped, all 35 fixtures execute (two new: the sparkle gate, and NPC-vs-Item
  identity), and `catalog_golden.txt` is re-baselined — after verifying the new
  code is byte-identical to v0.57.1 on all 33 original fixtures.
- **`tests/run_gates.sh`** is the whole gate in one command: five linters, the
  80 KB size gate, a host syntax check of the Win32 translation units, every
  compile-test and harness, then the scanner diff. It must print `overall fail=0`.

---

## 6. Offline field simulator (`fieldsim/`)

| file | what it does |
|---|---|
| `extract_fields.py`, `lzs.py` | pull every field's files out of `field.fs` |
| `ffield.py` | `.jsm` / `.sym` / `.id` / `.inf` / `.msd` readers, engine-accurate ordering |
| `scan.py` | the entity scanner, engine-accurate decode |
| `fieldids.py` | field id <-> internal/display name, extracted from `field_display_names.h` |
| `simfield.py` | text report + SVG for one field |
| `fieldmap.py` | PNG of the navigable geometry: walkmesh, entity placements, gateway exit lines, SETLINE zones |
| `gen_jsm_order_fixtures.py`, `gen_jsm_decode_fixtures.py` | regenerate the test fixtures from the disc |

---

## 7. Exit audit

Exits come from two complementary sources — INF gateways (the engine's pedestrian
doors) and script MAPJUMPs — and both are needed: 574 fields have a gateway
destination with no script jump, 347 have the reverse.

**76 fields have no statically derivable exit at all.** Broken down by why:

| how the field is actually left | count |
|---|---|
| a MAPJUMP whose destination is computed at runtime | 50 |
| `WORLDMAPJUMP` only — no field destination exists | 12 |
| no transition opcode at all | 13 |

The WORLDMAPJUMP group is the Chocobo Forest woods (`cwwood1`-`cwwood7`),
`bvcar_1`, `bvtr_2`, `gdtrain1`, `ggview2`, `tgcourt2`. These leave to the world
map, so there is no destination field to name — the catalog should be offering
"Exit to the world map" for them, and that is worth a look.

The 13 with no transition opcode include cutscene and test fields (`ending`,
`gover`, `test9`) but also `bgryo1_3`, `bgryo1_8`, `elview1`, `gfvill22`,
`gfvill23`, `gpcont2`, `ssspace1`. `gfvill22` is instructive: it has no
gateways and no script jump, but two Door entities and two INF *triggers*
referencing them by door id. Only three fields depend on doors this way
(`bg2f_4`, `esfreez1`, `gfvill22`), so it is a small hole, but it is a hole.

There are 539 one-way transitions (A reaches B, B has no listed way back). Most
are legitimate — story gates, one-way drops — but the list is a good place to
look for a missing return exit.

---

## 8. Where the painted background stands

Aaron asked for a look at how each field *appears*. The geometric render
(`fieldmap.py`) is done and is what the exit audit above was read off. The
painted background is not: the `.mim` pixel format did not fall out in the time
available. What is established, so resuming is cheap:

- Every Esthar `.mim` is exactly `0x6B000` bytes regardless of how many texture
  pages its `.map` references (3 to 14 observed), so it is a fixed-size dump, not
  a packed one.
- The `.map` tile is 16 bytes and decodes cleanly:
  `int16 x, int16 y, uint16 z, uint16 texBits, uint16 palBits, uint8 srcX,
  uint8 srcY, uint8 layer, uint8 blend, uint8 animId, uint8 animState`, with
  `x == 0x7FFF` terminating. Laying tiles out by `x`/`y` reproduces the 336x240
  field screen exactly, so the geometry half is certainly right.
- `texBits` and `palBits` read as PSX GPU attributes: every field background is
  8bpp (`(texBits>>7)&3 == 1`), pages sit at tpage y=256 and x = 64*n, and CLUTs
  sit at x=0, y=240+palette index.
- The palettes are at the start of the file — rendering the first few KB as
  RGB555 shows the colour bars plainly.
- What does **not** work is addressing the texel data: no row pitch between 32
  and 1200 produces a coherent image at 8bpp, 4bpp or 16bpp, scored by either
  smoothness or run-length. So the texel region is not a flat linear bitmap at
  the obvious strides, and the next step is to read the engine's own texture
  upload path rather than keep guessing.
