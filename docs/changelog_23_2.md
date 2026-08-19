# v0.23.2 — #82: a junction is a trade, and only one side was spoken

Aaron's v0.23.1 BAT:

> Better. I now hear the values in ST-Atk as well. However, it is only announcing
> the one value when in fact two or more may change... the mod said Confuse 8% or
> similar, but neglected to mention the drop in the Stop status. A sighted player
> can see both effects. Make sure this fix applies to elements and other magic
> junctions as well where increasing one value decreases another.

---

## The cause was a design note I wrote and was wrong about

v0.23.1 added the attack-row preview with this comment:

> An attack row is a SET plus one percentage, not a per-entry table, so there is
> no useful delta to speak: what the player wants is what the row would BECOME.
> Naming the outgoing set as well would double the sentence for no gain, since
> the grid line already said it.

It is not no gain. Junctioning Confuse over Stop **raises Confuse and drops
Stop**, and on screen both arrows are visible at once. Saying only the rise
describes a trade as though it were a gift — and the grid line does not cover it,
because the grid line describes the row *before* you started choosing.

## The fix generalises rather than special-cases

An attack row is now turned into the same per-entry before/after table the
defence rows already were — an entry's percentage is `(mask has it) ? row
percentage : 0` — and **every** row type goes through one collector and one
emitter. There is no longer a code path that *can* report one side of a change.

```
Confuse, quantity 8, Stop 40 to 0 percent, Confuse 0 to 8 percent
Fire, quantity 22, Thunder 80 to 0 percent, Fire 0 to 50 percent
Blizzaga, quantity 12, Fire 70 to 0 percent, Ice 0 to 50 percent
```

The outgoing side has to come off the **baseline** block (`0x01D8B3B0`) and be
assembled from *its own* `+0x1B4` and `+0x18C`. Reading the live block for both
halves reports no drop at all, so the compile probe plants a different spell in
each block and asserts both clauses appear.

## The flat cap of four is gone

It could truncate exactly the drop that was missing. Deltas are **grouped by
their (from, to) pair** instead, which is the shape junction changes actually
have — a whole set moving `0 → N` or `M → 0`:

```
Poison, Petrify, Darkness, Silence, Berserk, Sleep, Slow, Stop 0 to 20 percent
```

Ten statuses arriving together are one clause naming ten statuses, not ten
clauses, and not four clauses and a shrug. The remaining caps exist only so a
pathological state cannot overflow the buffer; grouping means a real junction
never reaches them, and anything dropped is still admitted with `", and more"` —
a silent truncation would read as completeness, which is the failure this whole
change is about.

Order follows the table rather than the direction of the change, so the preview
and the on-demand readout agree about where an element sits in the list.

---

## Verified

- `menu_sim: OK (0 bad)` — Aaron's exact case (Stop 40 → 0 while Confuse 0 → 8),
  the same-set-lower-percentage case, a three-status spell displacing a
  one-status spell, the elemental-attack swap, the defence swap, the ten-status
  grouping with its admitted tail, and 20,000 randomised views that now
  randomise **both** halves of every attack and defence table — the worst case
  for the emitter is a before and an after sharing no entries at all.
- `menu_junction_compile: OK (0 bad)` — the trade read through the two real
  blocks, plus the existing state gate, paging replay and mask assembly.
- `menu_magic_compile: OK (0 bad)`; `lint_seh: OK (90 files)`; entryaim /
  trigwalk / trigseg / pathdecimate / vehsig OK; catalog_story and garden_aboard
  0 failures; all four harnesses compile.

## Method note

Three of the last four Junction defects were **a sentence I wrote asserting that
something did not need saying**. "The grid line already said it" was the third.
A comment that justifies an omission is a place to look first when the BAT says
something is missing.
