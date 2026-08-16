# The Magic submenu

**v0.22.0–v0.22.5 (#81).** What the screen is, how the mod reads it, and the one
structural fact that turned out to matter more than the screen itself.

The full disassembly write-up is in `MAGIC_MENU_DISASSEMBLY.md`; this is the
working reference.

---

## The structural fact

Everything the mod has called `pMenuStateA + 0x2xx` for two years is really a
field of a **menu module object**.

The game allocates modules from a pool at `0x01D76BC8` — stride `0x78`, 10 slots,
allocator `0x004BE540` — and threads them onto an MRU-first linked list whose
head is at `0x01D76B48`. Each module begins:

| off | meaning |
|---|---|
| +0x00 | next |
| +0x04 | prev |
| +0x08 | **update / state-machine function** |
| +0x0C | draw function |
| +0x10 | **state** |
| +0x12 | in-use flag |
| +0x20.. | per-module scratch, zeroed on allocation |

The main menu happens to land in slot 1 and the open submenu in slot 2. That is
why `pMenuStateA + 0x22E` has always worked: it is **slot 2 + 0x10**, the
module's state word. Two consequences:

- The thing this codebase calls "focus" is the **state word of a state machine**,
  not a focus indicator. It takes values like 3, 13, 20, 72 because those are
  jump-table indices.
- `pMenuStateA + 0x230`, which the comments call a "phase", is **slot 2's in-use
  flag**. It was never a phase.

Slot 2 is an *allocation-order coincidence*, so `menu_tts_magic.inl` does not
rely on it: it walks the list and matches the Magic module by its update
function, `0x004F02F0`. `tests/menu_sim.cpp` proves the walk finds Magic in all
ten slots; `tests/menu_magic_compile.cpp` runs it against a real mapped pool,
including out-of-pool, misaligned and cyclic lists. If the walk ever fails the
mod falls back to the historical address and logs why, once.

## Identifying Magic

Submenu dispatch table at `0x00B87ED8`, `{creator, id}` pairs indexed by the byte
at `pMenuStateA + 0x1E8`:

| idx | screen | creator | state machine |
|---|---|---|---|
| 2 | Item | 0x4F8010 | 0x4F81F0 |
| **3** | **Magic** | **0x004F00D0** | **0x004F02F0** |
| 5 | Status | 0x4CDFA0 | 0x4CE080 |
| 6 | Save | 0x4E6740 | 0x4E3090 |
| 10 | Switch | 0x4CB850 | 0x4CBA50 |
| 17 | Junction | 0x4E2DC0 | — |

Indices 3/5/6/10/17 match the `+0x1E8` values the mod had already observed by
watching the game, so the identification is confirmed from two directions.

Item and Magic are both 114-state machines, and **their state numbering is
completely different**. Item's known values do not transfer.

## The screen

| phase | state | what the mod reads |
|---|---|---|
| action row | 3 | cursor `+0x61`, enable mask `+0x67` |
| spell list | 13 | cursor `+0x38 + charId`, page `+0x42`, character `+0x64` |
| paging | 14/15 (left), 16/17 (right) | silent; re-announces on return to 13 |
| target select | 20 | cursor `+0x57`, count `+0x60`, mask `+0x36` |
| sort popup | 72 | cursor `+0x71` |
| discard confirm | any (overlay) | `+0x6E` non-zero, cursor `+0x70`, victim `+0x32`/`+0x33` |
| closing | 112/113 | silent |
| Exchange | 26 / 28 / 44 / 52 / 55 / 63 | see below |
| All | 97 / 99 / 105 / 106 | see below |

**Route on the state, never on the screen mode `+0x56`.** Both two-character
flows use mode 3 and mode 4, so mode alone cannot tell Exchange from All — that
was the v0.22.0 bug that left both of them unusable.

Offsets are module-relative. Add `0x21E` for the historical
`pMenuStateA`-relative form.

### The one formula that matters

The list is **1 column × 4 visible rows × 8 pages = 32**. Up/Down wrap inside the
page and never scroll; Left/Right change the page.

```
slot = (module[0x38 + charId] & 3) + module[0x42] * 4
```

The stored byte is resynced to the absolute index every frame but **lags by one
frame after a page change**, so only its low two bits can be trusted. Masking and
re-deriving from the page is exactly what the game's own draw path does at
`0x004F0597`.

The cursor is stored **per character**, so `+0x38` is an array of 8 and you must
index it by the character id at `+0x64`.

There is **no display remapping** — the index goes straight into
`savemap.char[charId].Magics[slot]`.

> **The trap.** `pMenuStateA + 0x272` is the *Item* submenu's list cursor. In the
> Magic module the same byte is written only in the post-sort redraw state, so it
> is stale or wrong on every other frame. Reading it here would have produced a
> plausible spell name that was quietly the wrong one.

## The labels, from the game's own data

Not guessed and not taken from the Item menu. Decoded from
`Data/lang-en/menu.fs` → `mngrp.bin`, section 1 bank 8 — the "group 8" the code
passes to the text getter at `0x004BD630`. Entry tables:

- action row `0x00B88A90` = `{0, 1, 11, 2}`
- sort popup `0x00B88A9C` = `{15..21}`

Those index **pairs** of `{label, help}` at string positions 2p and 2p+1:

| cursor | label | help |
|---|---|---|
| 0 | Use | Use magic |
| 1 | Exchg. | Exchange magic with members |
| 2 | All | Take all magic from other members |
| 3 | Rearrange | Organizes magic during battle |

The mod says "Exchange" rather than the screen's abbreviated "Exchg.", which is a
visual space constraint that means nothing to a listener.

Sort orders (cursor 0–6): Manual, then the six permutations of
Attack / Restore / Indirect. The game separates them with interpuncts; the mod
reads them as "Attack, then Restore, then Indirect", because a screen reader will
either say "dot" or skip the character entirely.

This also settles what static analysis could not: **action 1 is Exchange (with
one member) and action 2 is All (take from every member)**. Both open
two-character flows, which is why they look identical in the code.

### The enable mask `+0x67`

Built in the creator at `0x004F00D0`:

- bit 0 — **Use**: off when the character is Petrified (status 0x04), Silenced
  (0x10), unavailable, or the savemap menu-lock bit 0x02 is set
- bits 1, 2 — **Exchange** and **All**: only when ≥ 2 characters are available
- bit 3 — **Rearrange**: always on

**Observed 2026-08-16: a RESERVE character loses "Use".** Irvine and Selphie (in
the party) show all four actions; Zell (reserve) consistently shows three, and
the missing one is Use. That follows from the rule above -- bit 0 needs the
character to be *available*, while bits 1 and 2 depend only on `popcount(available)
>= 2`, which is a global condition. So a reserve member gets mask `0x0E`.

That is the game's own behaviour and the mod reports it faithfully. It does leave
a gap worth considering: a sighted player sees "Use" greyed out and infers why,
whereas a blind player just finds a three-entry row with no explanation. Saying
*why* Use is unavailable would need the status word at
`savemap + 0x522 + charId*0x98` (bit 0x04 Petrify, 0x10 Silence) plus the
available-character mask from `0x004AD030`. Not shipped -- it adds words to a
screen that currently reads well, and it should be a deliberate choice rather
than a silent one.

The cursor skips disabled entries, so a solo silenced character sees a two-entry
row. The mod speaks "Rearrange, 1 of 1" in that case and stays silent about
position when all four are live — a position qualifier on every keypress is
noise, and its absence is information.

## Field-castable magic

`mmagic.bin`, loaded to `*(void**)0x01D2BB10`, stride 4, **byte 0 bit 0**.
Extracted offline, exactly seven spells carry it:

**Cure (21), Cura (22), Curaga (23), Life (24), Full-Life (25), Esuna (27),
Dispel (28).**

Life and Full-Life additionally carry `0x20` — they target the fallen.

Unusable spells are **greyed, not hidden**: the draw path at `0x004F71DB` renders
every one of the 32 slots and picks colour 1 instead of colour 7. That is
invisible to a blind player and it is the difference between a spell that casts
and one that silently refuses, so the mod says "cannot be cast here" — but only
in the Use flow, where it decides something. In Exchange and Rearrange every
spell is fair game and the qualifier would be a lie.

There is one extra clause, and it is not academic: while savemap `+0xAE3` has bit
`0x40` set, spells whose target type (`0x01CF4064 + id*60 + 7`) is 5 or 6 grey
out *even though their base flag is set*.

## A bug this found on the way

`menu_tts_ability.inl` carried its own spell-name table that ran "Slow, Stop,
Float, Drain, Pain" at ids 36–40, with a note to extend it "once they're
confirmed". Ids 36 and 37 are right. **38, 39 and 40 are Blind, Confuse and
Sleep** — Float is 47, Drain 44, Pain 45.

So an Ability refine whose yield was Float, Drain or Pain looked up a *different
spell's* stock and spoke that number, on the one line whose entire job is "you
already have N of these". The canonical 57-entry table now lives in
`menu_magic_model.inl` and `tests/menu_sim.cpp` pins all three corrected ids.

The cross-check that makes the table trustworthy: `mmagic.bin`'s field-usable bit
lands on exactly the seven spells FF8 lets you cast from the menu. A table with
the wrong ids could not produce that set.

## Architecture, and why it is worth copying

The wording lives in `src/menu_magic_model.inl` as **pure functions of a
`MagicView` struct** — no Win32, no SEH, no absolute addresses.
`src/menu_tts_magic.inl` is responsible only for finding the module, reading the
bytes and speaking the result.

That split is the point. Every previous submenu put reads and wording in one
file, so the only way to test the wording was to play it — which is why Junction
is still "partially" done after two years. `tests/menu_sim.cpp` drives all six
phases offline, including 20,000 randomised states, and it caught a real defect
on its first run ("No target, 4 of 3").

`MenuSim` models the **module pool and a state machine**, not "the Magic screen",
so Ability, GF, Config and the rest can reuse it.

### What the offline gates cannot do

They cannot prove an **address** is right. If an offset is wrong, the model and
the simulator are wrong together and both pass happily. What they prove is that
*given the right bytes*, the mod says the right words in every phase at every
cursor position — including the ones a tester would never think to visit.
Addresses are what the BAT is for.

## The two-character flows (v0.22.1)

Both are gated on the **state number**. v0.22.0 gated on screen mode, and *both*
flows use modes 3 and 4 — so they were indistinguishable and neither could be
narrated. That was the root cause of both being reported unusable.

### Exchange (action 1)

| state | screen | mod says |
|---|---|---|
| 26 | your list | `Squall, Cure, quantity 47, slot 2 of 32` |
| 28 | choose the partner | `Zell` |
| 44 | the partner's list | `Zell, Fire, quantity 60, slot 7 of 32` |
| 52 / 55 | Give All · Take All · Split | `Split, 3 of 3` |
| 63 | the quantity split | `Cure, move 12, keep 35` |

Both columns share the one per-character cursor array at `+0x38`; column B pages
on `+0x46`, so the partner's slot is `(cursor[B] & 3) + pageB * 4`. The split
counters are `+0x58` (moving) and `+0x59` (staying), sum conserved, both bounded
0–100, with the spell id at `+0x5A`.

Each panel names **whose** list it is. Two lists on one screen with identical
wording is exactly the ambiguity that made this unusable without sight.

Also worth knowing: Square opens the discard confirmation from state 26 too, not
just from the Use list.

### All (action 2) — the direction

**State 97 picks the receiver. State 99 picks the giver.** This is the reverse of
how the screen was first described, so here is the evidence:

- State 105 calls `0x004F5FA0(arg1 = +0x62, arg2 = +0x64)`. Inside, `0x4C2C70`
  **adds** each of arg1's spells to arg2 and `0x4C2D50` **removes** them from
  arg1 — so the step-2 character loses and the step-1 character gains.
- Step 2's character mask is `available & ~(1 << +0x64)`: it excludes the step-1
  choice, which only makes sense if step 1 already fixed a role.
- The action's own label is *"Take all magic **from** other members"* — the
  character whose screen you opened is the taker.
- The game's own help bar says it outright: group 8 entry **12** on step 1 is
  *"Select member to receive magic"*, entry **13** on step 2 is *"Select member to
  transfer magic"*.

The mod speaks those two strings verbatim rather than paraphrasing, so a sighted
player reading the bar and a blind player hearing the mod are told the same thing
in the same words. There is no Yes/No confirmation: the second confirm runs a
pre-flight check and either commits (state 105) or opens a warning box (106).

## The help bar

`module +0x24` is a pointer to the exact string the bar is drawing — raw
NUL-terminated FF8-encoded bytes inside the loaded `mngrp.bin`, since the text
getter `0x004BD630` returns a pointer rather than copying. It is written with one
aligned dword store into long-lived data, which is what makes it safe to read
from the mod's thread.

`NULL`, `0x01D7714C` (the getter's own fallback) and `0x01CFF84C` (the
empty-string constant) all mean "no text".

**The bytes are text-stream, not glyph indices.** `FF8TextDecode::DecodeMenuText`
indexes its table with a glyph index, because its other caller hands it the GCW
buffer — which the renderer has already converted. A text byte is `glyph + 0x20`,
so the string must be shifted down before decoding. Passing it straight in reads
"Use magic" aloud as "AaI'UEIOE", which is what v0.22.1 shipped. Control codes
below 0x20 are dropped, along with the parameter byte of a `0x0A`–`0x0E`
variable.

`tests/menu_sim.cpp` gates this with byte sequences lifted verbatim out of
`mngrp.bin`, and asserts the un-shifted reading is *wrong* — a fixture that
decodes correctly either way would prove nothing.

The game does **not** refresh `+0x24` in target select (state 20) or in All's
second step (99). The string left there is still correct for those screens — it
just was not rewritten — so `/` reads slightly stale text, never wrong text.

## States you cannot poll

**The All transfer runs in state 105 and lasts one frame.** The chain is
99 → confirm → 104 → 105 → 96 → 97; everything but 99 and 97 is transient. A
polling reader will never see it, and the 2026-08-16 log proved it: an entire
32-slot list moved between characters and the mod announced nothing.

So the transfer is detected by its **effect**. While the giver step is up, latch
how much the giver is holding; when that total reaches zero, announce. A cancel
leaves it untouched. The latch re-snapshots every tick because the giver can be
changed with left/right, and it requires the same giver on both sides so a
cursor move cannot misattribute the transfer.

The latch must also **survive the transients**. 104 and 96 are `MP_NONE`, and
v0.22.4 disarmed on them — a frame or two before the transfer. It now clears only
when the player is demonstrably out of the flow: the action row, closing, or a
different giver.

**The general rule, learned twice: a state the game reaches only through a
sequence cannot be verified by a test that constructs it directly.** v0.22.1 unit-tested
the state-105 announcement and the test passed, because a test can enter state
105 whenever it likes. Where the effect is visible in the savemap, watch the
effect.

## Announcement bookkeeping

An announcement fires on **arrival**, and an arrival is a change of *either* the
phase *or* the character the phase is about.

The character part is not optional. L1/R1 swaps the character without leaving the
phase, so on the 2026-08-16 log the spell list went from Irvine to Selphie
announced only as "Cure, quantity 82, slot 1 of 32" — both lines individually
correct, and no way to tell whose magic you were reading. The list header
therefore names the character rather than saying "Magic list".

Which character a phase is *about*: the partner panel and the partner picker are
about `+0x62`; everything else is about `+0x64`. Changing the irrelevant one
stays quiet.

A character change forces the header **only where the line does not already name
the character**. On the All steps the line is "Rinoa, receives", so forcing it
there prefixed the step prompt to every left/right. The prompt belongs to the
step; the name belongs to the line.

The header name derives from the character **id**, never from a cached name —
they can desync, because a swap updates the id first.

## Open, and deliberately not guessed

- **Which split counter belongs to which panel on screen.** `+0x58` and `+0x59`
  are paired and sum-conserved, and the mod reads them as "move N, keep M". The
  two lists are stacked vertically, so whether Left visually means "toward the
  top panel" is not decidable from the code. A ten-second BAT settles it.
- **`module +0x68`** carries `0x80` / `0x81` / `0` and distinguishes the two
  non-Split popup entries. State-level gating narrates the flow without it.
- **The two-entry popup's first label** depends on whether the destination slot
  is occupied, which the mod cannot see. It says "Give All"; `/` always has the
  game's exact wording.
