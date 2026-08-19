# v0.23.3 — #82: the character switch, and two things the log said

Aaron's v0.23.2 BAT:

> That worked well. Check the logs for any bugs or issues we may have missed. The
> only thing I noticed is that Junction here doesn't announce the name of the new
> selected character when Q and E are pressed to switch between characters.

---

## 1. L1 / R1 swaps the character and nothing said so

The switch shows up as a change in `pMenuStateA + 0x261` — the state machine's
own character field — so it can be watched from **any** state. That matters,
because the BAT log caught it at the action row, not the grid:

```
17:40:24  [JuncTTS] ActionMenu: Junction (cursor=0)
17:40:25  [SUBMON]  +0x261: 2 -> 0        <- Irvine to Squall, in silence
17:40:26  [JuncTTS] SubOption: GF (cursor=0 focus=37)
```

The complication is that **confirming out of character select changes the same
byte** (`17:39:22  +0x261: 0 -> 2`), and there the char-select screen has just
said *"Irvine, Level 19, HP 522 of 1837"* — repeating the name would be noise.

The two cases are about two seconds apart either way, so recency cannot separate
them. What can is the **value**: a confirm lands on the character char-select
just named; a switch never does. The marker is **consumed** on use, so switching
away and back announces both times — keying on recency would have swallowed the
second.

In the grid and the magic list the name goes into the header instead, so the
switch and the new line are one utterance rather than two:

```
action row   →  "Squall"
grid arrival →  "Junction, Squall. HP, Life, 2370"
switch on the grid →  "Squall. HP, Life, 2370"
```

The grid header names the character on arrival too. Every line on that screen
differs per character and none of them say whose it is.

## 2. The character select announced itself twice

Not noticed in play, but it is in the log twice over — two identical lines in the
same second, at `17:39:45` and again at `17:40:23`:

```
[JuncTTS] CharSelect: Irvine, Level 19, HP 522 of 1837 (cursor=0 roster[0]=2 ...)
[JuncTTS] CharSelect: Irvine, Level 19, HP 522 of 1837 (cursor=0 roster[0]=2 ...)
```

The "force a re-announce when returning from deeper" reset lived at the *end* of
the poll: the arrival frame announced with the stale cursor still matching, then
the reset fired and the next frame announced again. Doing the reset **before**
the announce block makes the arrival frame the only one that speaks.

## 3. The log claimed a solved screen was unhandled

`[JuncTTS] Unhandled focus=52` fourteen times, `=59` eleven times, plus 49, 54,
56, 57, 58, 61, 62, 63. Those are the grid, the magic list and their slide and
scroll states — all owned by `PollJunctionStats` since v0.23.0. The diagnostic
predates it and now skips 49..62, so the next BAT log does not read like a hole.

## 4. Noted, not a bug: the eligibility mask lags a column change

The mask at `+0x24A` is recomputed by the game in state 58 (entering the magic
list) and by state 52's own input block — but **not** by the page-scroll states
53..56. A grid line read on the frame after a column change therefore carries the
previous column's mask, which is why the log showed slot 9 as `003FFFFF` once and
`00196058` the next time.

Nothing on the grid uses it — `JuncAnnounceGrid` never looks at the mask, and the
"None of your magic affects this row" header is only ever composed in state 59,
where state 58 has just rebuilt it. So the readouts were correct throughout. But
a stale number in a log invites a bug hunt, so it is now printed only in state 59,
where it means something.

---

## Also confirmed by the log, working

- `"Thunder, quantity 90, Ice 46 to 0 percent, Thunder 0 to 45 percent"` — the
  v0.23.2 trade, on elemental attack.
- `"Berserk, quantity 11, Berserk 0 to 11 percent, Slow 41 to 0 percent"` — the
  same on status attack, which is the row that started this.
- `"Status attack, Slow, Slow 41 percent"` — v0.23.1's assembled mask.
- `"Status attack, locked"` / `"Elemental attack, locked"` on two different
  characters with different junction abilities.
- Zero errors, zero exceptions in 1,169 lines.

## Verified

- `menu_junction_compile: OK (0 bad)` — the switch named on the action row,
  folded into the header on the grid, silent on a char-select confirm, and
  speaking again after switching away and back (the consumed marker).
- `menu_sim: OK (0 bad)`; `menu_magic_compile: OK (0 bad)`;
  `lint_seh: OK (90 files)`; entryaim / trigwalk / trigseg / pathdecimate /
  vehsig OK; catalog_story and garden_aboard 0 failures; all harnesses compile.

---

## Confirmed in the BAT

> You can only press Q and E to switch between characters on the Junction screen
> when the Action Bar has focus. Tested that and announcements worked as expected.

Pinned in the exe as well: `+0x43` is written at exactly two instructions —
`0x004DBCF2` and `0x004DBF68`, the handlers for states 4 and 6 — and those two
states are dispatched from one place only, state 3 (`0x004DABB5`, `0x004DAD11`).
Nothing else in the 74-state machine touches it. The swap runs `4 → 5 → 6 → 7 → 3`
as a slide, resets the ability left-panel cursor `+0x5E`, and re-syncs the two
464-byte stat blocks, so the preview's baseline is correct for the new character
immediately.

The watcher stays state-agnostic anyway: reading the field costs nothing, and
"only state 3 can do this" is a fact about the game's *input routing*, which is
the more fragile of the two facts to depend on. The grid/magic header branch is
now marked as the defensive path it is, rather than left looking load-bearing.

### ⚠ The near-miss the confirmation turned up

The swap **also calls `0x004BE790`** (`0x004DABA9` and `0x004DAD05`, both on the
way into states 4 and 6). That is the same auto-junction routine the mod's
Auto-confirm hardware breakpoint watches.

The detector is only correct because it gates on the Auto submenu being focused
(`+0x22E == 11`), and during a swap the focus is 3. Without that gate every
character switch would announce *"Junctioned automatically for …"* — and only
after an Auto submenu had been opened at some point in the session, which is
exactly the kind of intermittent that costs a BAT cycle to pin down. Recorded
next to the gate itself so nobody widens it.
