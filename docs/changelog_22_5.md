# v0.22.5 — the latch was disarmed by the very frames it had to survive

**#81.** Two fixes, and the first one is the same mistake twice.

## The All transfer was still silent

v0.22.4 detected the transfer by watching the giver's total drop to zero, since
state 105 lasts one frame. That part was right. The clearing rule was not:

```c
if (phase != MP_ALL_RECEIVER || ...) { disarm }
```

The chain from the giver step to the transfer is **99 → 104 → 105 → 96 → 97**,
and 104 and 96 both map to `MP_NONE`. Any poll landing on one of them disarmed
the latch *one or two frames before the transfer it existed to detect*.

**And the v0.22.4 test passed** — because it stepped straight from 99 to 97 and
never visited the transient states. That is the second time in two builds that a
green test covered a path the game cannot actually take: v0.22.1 tested state 105
directly, which the poll can never observe, and v0.22.4 tested a state sequence
the game never produces.

The latch now survives until the player is demonstrably out of the flow — back on
the action row, closing, or a different giver selected. Nothing else empties a
character's entire list in place, and reaching Exchange to do it by hand means
passing through the action row, which disarms it first.

`menu_sim` now walks the **real** chain, 104 and 96 included, and asserts the
latch is still armed after a transient.

## The step prompt repeated with every character

v0.22.3 made a character change force the header. On the All steps the line
already says "Rinoa, receives", so that prefixed **"Select member to receive
magic"** to every single left/right.

The prompt belongs to the **step**; the name belongs to the **line**. A new
`MagicLineNamesCharacter()` suppresses the header on the two All steps and the
Exchange partner picker — and deliberately does *not* suppress it on the spell
list or the action row, whose lines name nobody and where v0.22.3's fix is still
exactly right.

Gated both ways: cycling five receivers speaks the prompt once, and switching
character in the spell list still re-announces whose magic it is.

## What I am taking from this

Both of my last two failures had the same shape: **a test that constructs a state
the game reaches only through a sequence, without walking the sequence.** The
tests were not wrong about the logic; they were wrong about the world. Where a
transition matters, the test now replays the transition rather than jumping to
its endpoints.

## Gates

`menu_sim` OK (0 bad) — the transfer block walks 99 → 104 → 105 → 96 → 97 and
asserts non-disarmament at 104; a new block asserts the prompt fires once per
step and still fires per character where the line does not name them.
`menu_magic_compile` OK (0 bad) · `entryaim` · `trigwalk` · `trigseg` ·
`pathdecimate` · `vehsig` OK · `catalog_story` · `garden_aboard` 0 failures ·
`lint_seh` OK (89 files) · all four harnesses compile.

## BAT

Complete one All transfer — you should hear "All magic moved from X to Y" — and
cycle a few characters on each step to confirm the prompt is spoken once rather
than before every name.
