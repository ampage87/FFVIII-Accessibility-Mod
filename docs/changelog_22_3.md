# v0.22.3 — the one thing the log found that the player did not

**#81.** v0.22.2 BAT'd clean. This is the full-log review before calling the
Magic submenu production-ready.

## What the log says is right

1,448 lines, **zero errors**, and the parts that were hardest to get right are
demonstrably working:

- The help bar reads the game's own text, including per-spell descriptions the
  bar shows while you are on the list — `Revive from KO`, `Restore HP`,
  `Scan enemy's HP and weakness`, `Fire magic damage/one enemy`.
- Castability is correct **live**: `Scan, quantity 88, cannot be cast here` and
  `Thunder, quantity 90, cannot be cast here`, while `Life, quantity 75` and
  `Cure, quantity 92` carry no qualifier. That is the seven-spell `mmagic.bin`
  set behaving exactly as extracted.
- The Exchange split ran `Fire, move 0, keep 100` → `move 50, keep 50`, and the
  savemap agrees afterwards: Zell's slot 1 reads `Fire, quantity 50` and Rinoa's
  reads the same. The transfer arithmetic is right.
- The slot formula held through a page change with a stale cursor
  (`page=1 cur=3 -> slot 7`), which is the case the game's own draw path handles
  the same way.
- `Magic module at 0x01D76CB8 (pool slot 2)` — the walk confirms the slot-2
  inference rather than depending on it.

## The one defect

**L1/R1 changes the character without leaving the phase, so nothing was said
about it.**

```
char=2 ... "Fire, quantity 51, cannot be cast here, slot 7 of 32"
char=5 ... "Cure, quantity 82, slot 1 of 32"
```

That is the spell list moving from Irvine to Selphie. **You would believe you
were still looking at Irvine's magic.** On a screen whose entire purpose is
"whose spells are these and how many", that is the worst thing it could get
wrong — and it is invisible in play, because both lines are individually
correct.

The action row had the same shape: `char=1` → `char=5` was audible only as the
position qualifier quietly disappearing when the enable mask happened to change.

A character change now counts as an **arrival**, so the header fires. And the
list header **names the character** — "Selphie's magic" — instead of the generic
"Magic list", which said nothing precisely when it needed to.

Which character an announcement is *about* differs by phase: the partner panel is
about the partner, everything else about the screen's own character. So swapping
the partner re-announces in the partner panel, and changing it while on the
action row stays quiet.

One latent bug fell out of writing the test: the header used the cached
`charName` rather than deriving from `charId`. Those can desync, and a swap
updates `charId` first — so the fixture produced "Squall" where the character was
Selphie. It now derives from `charId` and cannot desync.

## Cosmetic, deliberately not fixed

The help bar renders the game's interpuncts as spaces:
`Rearrange in order of Restore Attack Indirect`. That is the game's own text and
it reads fine aloud. Special-casing the character inside the shared decoder would
risk every other help string for no real gain.

## Gates

`menu_sim` OK (0 bad), with a new block that replays the log's own transitions —
the spell-list swap, the action-row swap, the partner swap, and the case that
must *not* fire. `menu_magic_compile` OK (0 bad) · `entryaim` · `trigwalk` ·
`trigseg` · `pathdecimate` · `vehsig` OK · `catalog_story` · `garden_aboard`
0 failures · `lint_seh` OK (89 files) · all four harnesses compile.

## BAT

Short one: in the spell list, press L1/R1 to change character and confirm it now
says whose magic you are looking at. Same on the action row. Then it is done.

## Not exercised in any BAT yet

An **All transfer has never been completed** in a log — v0.22.2's run reached the
receiver step and backed out, and the "works as expected" confirmation came from
play rather than from a log line. States 105 (transfer) and 106 (pre-flight
warning) are therefore implemented and unit-tested but never observed live. Worth
one run whenever it is convenient; not a blocker.
