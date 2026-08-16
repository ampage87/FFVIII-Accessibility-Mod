# v0.22.0 — the Magic submenu

**#81.** The Magic screen is hooked: the action row, the 32-slot spell list, the
cast target, the sort popup and the discard confirmation. Plus an offline
simulator for the main menu, and a bug it found in the Ability screen.

## The thing worth knowing first

Everything this mod has called `pMenuStateA + 0x2xx` for two years is really a
field of a **menu module object**. The game allocates modules from a pool at
`0x01D76BC8` (stride `0x78`, 10 slots) and threads them onto an MRU linked list
at `0x01D76B48`. The main menu lands in slot 1 and the open submenu in slot 2.

So `+0x22E` — which this codebase calls "focus" — is **slot 2 + 0x10, the state
word of a state machine**. That is why it takes values like 3, 13, 20 and 72:
they are jump-table indices. And `+0x230`, which the comments call a "phase", is
slot 2's **in-use flag**. It was never a phase.

Slot 2 is an allocation-order coincidence, so the Magic hook does not depend on
it. It walks the list and matches the module by its update function,
`0x004F02F0`, taken from the submenu dispatch table at `0x00B87ED8` index 3 —
whose indices for Status, Save, Switch and Junction match the `+0x1E8` values the
mod already observed, confirming the identification from two directions.

## What it announces

| phase | state | line |
|---|---|---|
| action row | 3 | `Use` · `Exchange` · `All` · `Rearrange` |
| spell list | 13 | `Cure, quantity 47, slot 2 of 32` |
| — uncastable | | `Meltdown, quantity 3, cannot be cast here, slot 4 of 32` |
| — empty | | `Empty, slot 6 of 32` |
| target | 20 | `Quistis, 3 of 3` |
| sort popup | 72 | `Attack, then Restore, then Indirect, 2 of 7` |
| discard | overlay | `Discard all of Zell's Cure? Yes` |

**The labels are not guesses.** They are decoded from the game's own
`Data/lang-en/menu.fs` → `mngrp.bin` group 8, using the entry tables the code
itself indexes (`0x00B88A90` for the action row, `0x00B88A9C` for the sort
popup). That also settles what the disassembly could not: action 1 is *Exchange*
(with one member) and action 2 is *All* (take from everyone) — they look
identical in code because both open two-character flows.

The screen abbreviates the second one to "Exchg."; the mod says "Exchange",
because the abbreviation is a pixel-width constraint that means nothing aloud.
The sort orders' interpuncts are read as "then" for the same reason — a screen
reader will either say "dot" or skip the character.

## Two things that had to be right

**The slot formula.** The list is 1 column × 4 rows × 8 pages. The cursor is
stored **per character** and holds an absolute index that lags one frame after a
page change, so only its low two bits are trustworthy:

```
slot = (module[0x38 + charId] & 3) + module[0x42] * 4
```

The trap: `pMenuStateA + 0x272` is the *Item* menu's list cursor. In the Magic
module that byte is written only during the post-sort redraw, so reading it here
would have produced a plausible spell name that was quietly the wrong one.

**The greyed-out state.** Unusable spells are greyed, not hidden — the draw path
renders all 32 and picks colour 1 instead of 7. That is invisible to a blind
player and it is the difference between a spell that casts and one that silently
refuses, so the list says "cannot be cast here". Only in the Use flow, though;
in Exchange and Rearrange every spell is fair game and the qualifier would be a
lie.

Field-castable is `mmagic.bin` byte 0 bit 0, and extracting it offline gives
exactly seven spells: Cure, Cura, Curaga, Life, Full-Life, Esuna, Dispel. Which
is exactly what FF8 allows.

## The simulator

`tests/menu_sim.cpp`. The wording lives in `src/menu_magic_model.inl` as pure
functions of a `MagicView` struct — no Win32, no SEH, no absolute addresses — and
`src/menu_tts_magic.inl` is responsible only for finding the module and reading
bytes.

Every previous submenu put reads and wording in one file, so the only way to test
the wording was to play it. That is why Junction is still "partially" done after
two years. The simulator drives all six phases offline, and **it caught a real
defect on its first run**: with the target cursor past the end of the character
mask the mod said "No target, 4 of 3".

`MenuSim` models the module pool and a state machine rather than "the Magic
screen", so Ability, GF, Config and the rest can reuse it.

`tests/menu_magic_compile.cpp` is the other half: no host harness builds
`menu_tts.cpp`, so this is the only pre-MSVC syntax check the hook gets. It also
runs the module walk against a real mapped copy of the pool — all ten slots, plus
out-of-pool, misaligned and cyclic lists.

## A bug found on the way

`menu_tts_ability.inl` carried its own spell-name table running "Slow, Stop,
Float, Drain, Pain" at ids 36–40, with a note to extend it "once they're
confirmed". 36 and 37 are right. **38, 39 and 40 are Blind, Confuse and Sleep** —
Float is 47, Drain 44, Pain 45.

So an Ability refine yielding Float, Drain or Pain looked up a *different spell's*
stock and spoke that number, on the one line whose whole job is "you already have
N of these". The canonical 57-entry table now lives in `menu_magic_model.inl`.

The cross-check that makes it trustworthy: `mmagic.bin`'s field-usable bit lands
on exactly the seven spells FF8 lets you cast from the menu. A table with the
wrong ids could not produce that set.

## Gates

`menu_sim` OK (0 bad) — **new**: 16 spell-id anchors including the three the old
table had wrong; the slot formula over all 32 slots with a deliberately stale
cursor; list wording for names, quantities, empties and the greyed qualifier;
the castable set against `mmagic.bin` plus the `0x40` lock; the action row under
four enable masks; target select over a sparse party mask; the sort popup; the
discard confirmation and its precedence over the underlying screen; phase routing
for 11 states; the module walk in all 10 pool slots; and 20,000 randomised states
with no overflow and no silent speaking phase.

`menu_magic_compile` OK (0 bad) — **new**. `entryaim_test` OK · `trigwalk_test`
OK · `trigseg_test` OK · `pathdecimate_test` OK · `vehsig_test` OK ·
`catalog_story_test` 0 failures · `garden_aboard_test` 0 failures · `lint_seh` OK
(89 files) · all four harnesses compile.

## BAT

Open **Magic**, pick a character, and walk it:

1. The **action row** — left/right across Use / Exchange / All / Rearrange.
2. **Use**, then the spell list. Up/down inside a page, left/right to change
   page. Check a Cure-type spell reads normally and something like Meltdown says
   "cannot be cast here".
3. Confirm a castable spell to reach **target select**.
4. **Rearrange** for the seven sort orders.
5. If you are willing: trigger a **discard** and listen to the confirmation
   before answering. It should name the character and the spell before Yes/No.

The offline gates prove the words are right given the right bytes. What they
cannot prove is that the *addresses* are right, so the log line is what matters:
`[MagicTTS] phase=… state=… char=… page=… cur=… -> slot N : "…"`. If a spell name
is wrong, that line says which slot the mod thought it was reading.

There is also one line worth grepping for on the first run:
`[MagicTTS] Magic module at 0x… (pool slot N)`. If N is not 2, the module walk
just earned its keep.

## Not done, deliberately

Exchange and All open two-column, two-character screens with their own sub-flows
("Give All", "Split", "Take All"). v0.22.0 announces which member is being
chosen and then goes quiet on the two-column screen rather than read a layout it
has not verified. That is the obvious next piece, and `docs/MAGIC_MENU.md` names
the offsets to check first.
