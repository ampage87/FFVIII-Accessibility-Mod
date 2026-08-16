# v0.22.4 — the All transfer ran silently

**#81.** The v0.22.3 character fix is confirmed. Completing an All transfer for
the first time exposed the last defect.

## The character fix works

The log shows three consecutive `"Use"` lines at `char=2`, `char=0`, `char=5`.
They only appear *because* the character changed — the string-equality check
suppresses an unchanged line — so the header fired each time. The spell list
does the same across `char=5` → `char=2`.

## The All transfer said nothing

Receiver Zell, giver Irvine. Afterwards, Zell's Exchange list reads:

```
Life, quantity 75, slot 1 of 32
Scan, quantity 88, slot 2 of 32
Cure, quantity 92, slot 3 of 32
```

which is **Irvine's exact loadout** from earlier in the same log. The transfer
happened. There are **zero** transfer announcements.

**Cause: state 105 performs the transfer and lasts one frame.** The sequence is
99 → confirm → 104 → 105 → 96 → 97, and every state in that chain except 99 and
97 is transient. A polling reader cannot see it. v0.22.1 implemented the
announcement on state 105 and unit-tested it, and the test passed, because the
test could hand it state 105 whenever it liked. The game will not.

## The fix: watch the effect, not the state

While the giver step is up, latch **how much the giver is holding**. When that
total reaches zero, the transfer has happened — announce it. A cancel leaves the
total untouched, so the two outcomes are distinguishable without ever observing
the state that did the work.

Three details the log itself dictated:

- The latch **re-snapshots every tick**, because the giver can be changed with
  left/right. The log shows Squall highlighted, then Irvine — so a latch taken
  once would have named Squall.
- It requires the same giver on both sides of the comparison.
- A giver holding nothing cannot produce a false positive.

State **107** is also now part of the pre-flight-warning phase: 106 opens the box
and 107 polls it, and only mapping 106 risked the same one-frame problem.

## Why this was worth catching

v0.22.3's notes said plainly that an All transfer had never completed in a log,
and that states 105 and 106 were "implemented and unit-tested but never observed
live". One BAT later, that exact gap contained a bug — and it was a silent one,
which is the kind a tester cannot report because nothing happens.

The general lesson, written into the file: **a state that only the game can enter
transiently cannot be verified by a test that constructs the state directly.**
Where the effect is observable in the savemap, watch the effect.

## Gates

`menu_sim` OK (0 bad) — new block replaying the log: the completed transfer, the
cancel, a giver changed mid-step, and an empty giver. `menu_magic_compile` OK
(0 bad) · `entryaim` · `trigwalk` · `trigseg` · `pathdecimate` · `vehsig` OK ·
`catalog_story` · `garden_aboard` 0 failures · `lint_seh` OK (89 files) · all
four harnesses compile.

## BAT

Complete one All transfer and confirm you hear "All magic moved from X to Y".
Then cancel out of another and confirm you hear nothing. That is the last
untested path in the Magic submenu.
