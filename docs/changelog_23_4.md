# v0.23.4 — #120: the Save screen's overwrite confirmation

Aaron, 2026-08-17: *"After selecting a block, if the block already contains data
a dialog appears for the player to confirm overwriting the existing save. That
dialog is not supported by the mod."*

He is right, and it is the worst screen in the game to be unsupported on.
Choosing an occupied block opened a Yes/No box; the mod said nothing when it
opened, nothing when the cursor moved between the two options, and nothing when
it closed. The only way to learn which option was armed was to press confirm and
find out — on the one decision in the game that destroys a save file.

---

## The Save screen is a menu module, like everything else

`menu_tts_save.inl` has read `pMenuStateA + 0x1EE` (from the field) and
`+ 0x266` (from the in-game menu) since v0.07, calling them "phase". The two are
exactly `0x78` apart, which is the module-pool stride #81 uncovered — so they
are one field of one object, seen in two different pool slots:

| historical name | really |
|---|---|
| `+0x1EE` / `+0x266` "phase" | save module **+0x48**, a panel id (pool slot 1 / slot 2) |
| `+0x1F0` / `+0x268` block cursor | save module **+0x4A** |
| `+0x1FE` / `+0x276` slot cursor | save module **+0x58** |

The Save module is index **6** of the submenu dispatch table at `0x00B87ED8`:
creator `0x004E6740`, state machine `0x004E3090`, draw `0x004E5550`. The creator
registers the state machine through the pool allocator `0x004BE540`, so module
`+0x08 == 0x004E3090` identifies it uniquely — and identifying it that way is
what makes ONE reader cover both paths. The field save point and the menu Save
are the same module in different slots; the mod has been maintaining two sets of
constants for a distinction that does not exist below the surface.

(The comment at the top of `menu_tts_save.inl` claiming the save controller is
`0x004CB850` is wrong and has been for two years. `0x004CB850` is dispatch index
9/10 — the **Switch** screen.)

## The dialog, from the disassembly

The 84-state jump table is at `0x004E5294`. Block selection is state **0x21**;
on confirm it reads the per-block status table at `0x01D8CB30` (one byte per
block) and branches at `0x004E3880`:

| status | branch |
|---|---|
| `0` | state 0x38 — save straight into the empty block |
| `1` or `2` | state **0x36** — the overwrite confirmation |
| anything else | error beep |

State **0x36** (`0x004E3B0E`) opens the box in a single frame:

```
mov  byte [esi+0x48], 4            ; panel id = the confirmation
mov  byte [esi+0x4F], 1            ; cursor defaults to the SECOND option
call 0x004BD630 (1, 5, 5, 0)       ; "Data exists.  Overwrite?"
call 0x004C2B10 (text, 0x54)       ; open the generic yes/no query window
mov  word [esi+0x10], 0x37         ; -> the input loop
```

State **0x37** (`0x004E3B3D`) is the loop. It runs the two-entry cursor helper
`0x004C0A30(keys, 2, cursor)`, writes the result back to `+0x4F`, and on confirm
does this at `0x004E4C49`:

```
dl = [esi+0x4F] ; neg dl ; sbb edx,edx ; and edx,0xFFFFFFE8 ; add edx,0x38
```

`0x38` when the cursor is 0, `0x20` when it is anything else. **So cursor 0 is
Yes and cursor 1 is No, and the game arms it on No** — which is not a guess from
which option reads first on screen, it is the branch target. Cancel
(`0x004E4C5D`) also returns to `0x20`.

`0x004E3B37` is the **only** instruction in the entire state machine that writes
state `0x37`. That is what makes `state == 0x37` a sufficient gate on its own,
and it is the difference between this reader and one that has to guess from a
panel byte that other screens also use.

## The words are the game's, not ours

`0x004C2B10` tail-calls `0x004C2A20`, which parks three pointers in globals when
the window opens:

| address | contents |
|---|---|
| `0x01D77300` | the prompt |
| `0x01D772F0` | option 0 (null arg → `getter(0, 0, 0x2F, 0)`, "Yes") |
| `0x01D772E0` | option 1 (null arg → `getter(0, 0, 0x30, 0)`, "No") |

Each points straight into loaded menu text — the getter at `0x004BD630` returns
a pointer rather than copying — so they are long-lived bytes, safe to read from
the mod's thread for exactly the reason the Magic help bar at module `+0x24` is.
They are TEXT-STREAM bytes (`glyph + 0x20`) and are shifted down through
`MagicTextToGlyphs` before the decoder sees them; passing them straight in is
the v0.22.2 "AaI'UEIOE" bug and this file would have reproduced it.

Reading them rather than hardcoding matters beyond tidiness: a blind player and
a sighted player now hear and see the same sentence, and a localisation change
cannot silently desync the two. The literals in the file are a fallback for a
null or a fault, nothing more.

The fallback text is not invented either. `mngrp.bin` section 1 (file offset
`0x800`) bank 5 is the Save screen's own string bank:

```
entry  4  "Choose block to save"
entry  5  "Data exists.  Overwrite?"
entry  6  "Yes"
entry  7  "No"
entry  9  "Saving data"
entry 24  "No empty blocks.\nNeed 1 block to save."
entry 26  "Data saved"
entry 29  "Choose block to load"
```

Entries 4, 9, 24 and 29 are what the state machine's *other* `0x004BD630` call
sites fetch, at the states where those messages belong. That agreement is what
confirms the bank identification — decoding one string that looks right proves
nothing.

## What it says

Aaron's call on the wording: the game's words and the armed option, nothing
else.

```
Data exists. Overwrite? No.
```

then `Yes` / `No` alone as the cursor moves. The block's contents were spoken a
keypress earlier, when the cursor landed on it; repeating them here would put a
paragraph between the question and the answer the player is about to give.

The game's string carries two spaces ("Data exists.  Overwrite?") which is
typesetting, not meaning, so runs of whitespace collapse. The line-break control
`0x02` becomes a space before decoding — `MagicTextToGlyphs` drops control bytes,
so a two-line prompt would otherwise arrive with its words welded together. The
overwrite prompt is one line today; this costs nothing to be right about.

## The closing behaviour, which is where the real accessibility gap was

Cancelling on **No** returns to the block list, where the game redraws "Choose
block to save" and otherwise says nothing — so a player who chose No would hear
silence and have no way to know where they had landed. On close with a non-zero
cursor the reader clears `menu_tts_save.inl`'s `s_prevBlockCursor`, and that
file's existing, already-BAT'd block line re-reads itself. No new wording.

On **Yes** the latch is deliberately left alone: the screen moves on to the save
by itself, and re-announcing the block there would talk over it.

`s_saveQueryDialogOpen` holds the slot and block pollers down while the box is
up. Neither cursor byte moves during the dialog, so this is belt-and-braces
rather than a fix — but a deterministic silence is worth three lines when the
alternative is a hoped-for one, and this is the screen where two utterances
racing each other costs a save.

## Robustness

- The pool walk is bounded by the pool, rejects a misaligned pointer, and caps
  at 12 hops so a cycle cannot hang the game thread — the same shape as
  `FindMagicModule`, deliberately, so a fix to one is obviously a fix to both.
- The fallback to the historical bases is accepted **only** when `+0x08` there
  really is `0x004E3090`. A fallback that can be wrong silently is worse than no
  fallback, and it logs once when it engages.
- An out-of-range cursor names nothing rather than guessing an option.
- The whole SEH-protected read is POD-only in its own function, so MSVC's C2712
  cannot bite; `tests/lint_seh.py` agrees across all 91 files.

## Verified

`menu_save_query_compile` **OK (0 bad)** — a new offline gate that maps the real
pool and the real query-window globals and drives the reader the way the game
does: the wording, the `glyph + 0x20` shift, the double-space collapse, the line
break, both "no text" sentinels, the walk finding Save in all ten pool slots,
out-of-pool / misaligned / cyclic lists terminating, the fallback refusing a base
whose update fn disagrees, arrival speaking once, a stationary cursor staying
silent, a cursor move speaking the option alone, the latch cleared on cancel and
**not** cleared on confirm, and state `0x36` or the wrong panel refusing to
announce. Three deliberate mutations (gate on `0x36`, drop the space collapse,
drop the line-break translation) were each confirmed to fail it.

Also re-run unchanged: `menu_sim` OK (0 bad), `menu_magic_compile` OK (0 bad),
`menu_junction_compile` OK (0 bad), `lint_seh` OK (91 files).

`src/menu_tts_save_query.inl` is 15,057 bytes; `menu_tts_save.inl` moves 45,831
→ 46,455 and `menu_tts.cpp` 44,693 → 45,251. All far below the 60 KB soft limit.

**Not MSVC-compiled and not BAT'd.**

## Deliberately out of scope

The same query window drives the format-GAME-FOLDER confirm (states `0x28`/`0x29`,
same `+0x4F` cursor), and the panel byte `+0x48` already distinguishes "Saving
data" (7), "No empty blocks" (0xA) and the rest — so folding them in later is
cheap. One change per BAT cycle; Aaron chose overwrite only. Related: #104.

## BAT

1. Save from an in-game menu **onto a block that already has data**. Expect
   *"Data exists. Overwrite? No."* as the box opens.
2. Move the cursor. Expect just *"Yes"* and *"No"*, once per move, nothing else.
3. Choose **No**. Expect the block list to re-read the block you were standing
   on, rather than silence.
4. Choose **Yes**. Expect the save to proceed with nothing spoken over it.
5. Repeat all four **from a field save point** (game mode 1, not the menu) — that
   is the same module in a different pool slot, and it is the half of this that
   the offline gate cannot prove.
6. Grep `ff8_menu.log` for `[SaveQuery]`. It should be a handful of lines telling
   the whole story: opened / cursor / closed. **A `pool walk found no Save
   module` line means the walk failed and the historical base carried it** —
   worth reporting even though the screen still worked.
