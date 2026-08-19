# v0.23.0 — #82: finishing the Junction menu

Aaron: *"Let's now see if you can use your offline sim and everything we learned
from the Magic submenu to finish up the Junction menu... including the character
and party abilities list (command abilities are hooked already), junctioning
magic to specific stats, junctioning magic to elemental/status attack and
defense, and reading stat information... For some of these, like the list of
elements and status ailments, we'll need to think about how to communicate
pertinent information to the player without being overwhelming."*

---

## What now speaks

**The junction grid.** Every row: the slot, what is junctioned there, and the
consequence.

```
Strength, Curaga, 68
Strength, empty, 42
Speed, locked
Elemental defence 2 of 4, Blizzaga, Ice resists 50 percent
Status attack, Sleep, Sleep 60 percent
```

**The magic list, with the game's own preview.** The screen keeps a baseline
block and a live block and draws its up/down arrow by comparing them, so the mod
can say what a candidate *would* do before it is committed:

```
Choose magic for Strength. Curaga, quantity 47, Str 42 to 68
Blizzaga, quantity 12, Ice 0 to 50 percent
Cure, quantity 9, no change
Meltdown, quantity 3, no effect here
```

**The character ability list**, and the always-on party abilities, and the
equipped loadout.

**Number keys 0–9**, per-screen, matching the Status screen where they overlap.

---

## Not overwhelming: what the screen does NOT read

This was the part Aaron flagged, and it is the design, not an omission.

Eight elements and thirteen statuses on every cursor move would be unusable, and
**a zero is not news**. So:

- Resistance rows name only what is actually non-zero. A wholly neutral character
  gets `"Elemental defence 1 of 4, empty"` and nothing more.
- The preview reports only what **moves**, capped at four entries with `", and
  more"` as the honest tail — a real spell moves one or two.
- The full 21-entry picture lives behind key **9**, on demand, never automatic.
- The vocabulary carries the meaning without a number where it can: *immune*,
  *absorbs 30 percent*, *weak 20 percent*.

---

## Three things the disassembly corrected

**1. State 37 is a slide-in animation, not the grid.** The first draft of the
hook polled the grid at state 37. `0x004DB008` walks `+0x40` from 0 to `0x1000`
and only then hands over — a state the game passes *through*. Reading the grid
there would have announced a row at most once per visit, on whichever frame the
poll happened to land, and looked like a flaky hook rather than a wrong constant.
The steady grid state is **52** (`0x004DB29F`), the handler that actually reads
the D-pad against the row count.

This is the same shape as the v0.22.5 latch bug, so it gets a gate rather than
another BAT: `menu_junction_compile` walks all 74 states and asserts that exactly
two of them speak.

**2. The character-ability cursor moves with the list.** `0x004DAE12` addresses
it as `module[0x52 + kind]`: the command list is `+0x271` and the character list
is `+0x272`. The mod read `+0x271` unconditionally, which is precisely why *"command
abilities are hooked already"* and character abilities were not — it was
browsing a cursor that had stopped moving.

The list contents now come from the game's own arrays too (`0x01D8B258` /
`0x01D8B280`, built by `0x004E0110`). `BuildAbilityRightPanel` reconstructed the
same membership from the same GF bitmaps, which was a fair guess — but **a
reconstruction can agree on membership and still disagree on order, and order is
the only thing a cursor index means.** It is gone.

**3. The elemental and status rows are locked by the same mask as the stats.**
The first draft returned "unlocked" for every non-stat row. `0x004DE531` gates
them by `0x200` / `0x400` / `0x6800` / `0x19000`, which land on exactly ability
ids 10 / 11 / 12,14,15 / 13,16,17 once you subtract one — five independent
agreements, so the off-by-one in that mask is now known rather than assumed. A
character with no Elem-Def-J would otherwise have heard "Elemental defence 1,
empty" forever and never learned why nothing would go in it.

---

## Eligibility: the Magic screen's problem again

While a magic list is up, `pMenuStateA + 0x24A` holds a 32-bit mask the game
builds itself (`0x004DE485`) by asking `0x004C2E50(spellId, slot)` about each of
the 32 stock entries. Ineligible entries are drawn **dim**. Dim is invisible to a
screen reader, so the list says `"no effect here"` — distinct from `"no change"`,
which means the spell is legal and simply does not help.

---

## Elements and statuses are derived, not looked up

The on-screen labels are **sprites**. Unlike the Magic menu there is no in-game
string to read back, so a wrong ordering here would be undetectable in play —
the mod would confidently call Poison "Earth" forever.

Both orderings come out of kernel.bin's magic table, with every bit pinned by a
spell whose own name states it: Quake → Earth, Break → Petrify, and Pain →
Poison/Darkness/Silence/Curse, which is what fixes bit 10. `menu_tts_status.inl`
reached the same orders from live junctions, from a completely different
direction, and now shares these tables instead of keeping its own copy — two
copies of a name table is exactly what put Float/Drain/Pain at the wrong spell
ids for months.

---

## Architecture

Same split as the Magic submenu, for the same reason:

- `src/menu_junction_model.inl` — pure functions of a `JunctionView`. No Win32,
  no SEH, no absolute addresses. All the wording.
- `src/menu_tts_junction_stats.inl` — addresses only.
- `tests/menu_sim.cpp` — drives the model offline.
- `tests/menu_junction_compile.cpp` — new; compiles the memory-facing file and
  runs it against real mapped pages at the game's own addresses.

---

## Verified

- `menu_sim: OK (0 bad)` — the grid table (all 19 slots reachable exactly once,
  cell 15 blank), stat/locked/blank rows, elemental and status attack and
  defence, the locked elemental and status rows, defence row counts, the
  preview including "no effect here" vs "no change", both on-demand readouts, the
  percentage scale against the Status screen's validated anchors, the number
  keys, the four steady state numbers with 37 pinned as *not* the grid, and
  20,000 randomised views across four announcers at the buffer sizes the mod
  passes.
- `menu_junction_compile: OK (0 bad)` — 74-state gate, live grid and magic lines
  composed through the real offsets, repeat suppression, char id from `+0x261`
  with the cached fallback, the loadout and party-ability readouts, and the
  per-list cursor offsets.
- `menu_magic_compile: OK (0 bad)`; `lint_seh: OK (90 files)` — and it caught a
  C2712 in `JunctionNumberKeys` before the build did, the second time that check
  has paid for itself.
- `entryaim` / `trigwalk` / `trigseg` / `pathdecimate` / `vehsig` OK;
  `catalog_story` and `garden_aboard` 0 failures; all harnesses compile.

**Fixed en route: `tests/catalog_harness.cpp` has not compiled since v0.20.29.**
It keeps its own copy of `CapturedTriggerLine`, and `isCameraTransition` was
added to the real struct and not to the copy. A duplicated struct fails by not
building, which is a silent kind of failure when nobody runs that harness.

---

## What the offline gates cannot do

Prove an address is right. The fixtures are written at the addresses the mod
believes in, so a wrong belief is consistent with itself. What they prove is
that **given the right bytes**, the mod says the right words in every state at
every cursor position.

Addresses are what the BAT is for.
