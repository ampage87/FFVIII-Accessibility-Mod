# The Save screen

**v0.23.4 (#120).** What the screen actually is, where every byte the mod reads
comes from, and the one structural correction that matters more than the dialog
this cycle was about.

---

## The structural fact

**The Save screen is a menu module**, and `menu_tts_save.inl` has been reading
its fields through two unrelated-looking address sets since v0.07:

| the mod called it | it is |
|---|---|
| `pMenuStateA + 0x1EE` / `+ 0x266` — "phase" | save module **+0x48**, a panel id |
| `+ 0x1F0` / `+ 0x268` — block cursor | save module **+0x4A** |
| `+ 0x1FE` / `+ 0x276` — slot cursor | save module **+0x58** |

The two sets are exactly **0x78** apart, which is the module-pool stride #81
uncovered. They are one object seen in two pool slots: the **field save point**
lands in slot 1, the **in-game menu Save** in slot 2 (the main menu takes slot 1
there). Everything downstream follows from that — one reader covers both paths,
and the "mode 1 vs mode 6" distinction the mod maintains is a distinction below
the surface only.

The state machine's own state word is **module +0x10**, which no part of the
save code read before this cycle.

> **Correction on record.** The comment at the top of `menu_tts_save.inl`
> claiming the save controller is `0x004CB850` was wrong and stood for two years.
> `0x004CB850` is dispatch index 9/10 — the **Switch** screen.

## Identity

Submenu dispatch table at `0x00B87ED8`, `{creator, id}` pairs indexed by the byte
at `pMenuStateA + 0x1E8`:

| idx | screen | creator | state machine |
|---|---|---|---|
| 2 | Item | 0x4F8010 | 0x4F81F0 |
| 3 | Magic | 0x4F00D0 | 0x4F02F0 |
| 5 | Status | 0x4CDFA0 | 0x4CE080 |
| **6** | **Save / Load** | **0x004E6740** | **0x004E3090** |
| 9 / 10 | Switch | 0x4CB850 | 0x4CBA50 |
| 17 | Junction | 0x4E2DC0 | — |

The creator registers the state machine and the draw function (`0x004E5550`)
through the pool allocator `0x004BE540`, so **module `+0x08 == 0x004E3090`
identifies the Save module uniquely** — the same identification
`menu_tts_magic.inl` uses. `0x004E6788` also sets `+0x49 = 0` and `+0x48 = 1` at
creation, which is where the mod's long-standing "phase 1 = slot list" comes
from. **`+0x49` is the mode: 0 = save, 1 = load.**

## The state machine

84 states, jump table at **`0x004E5294`**, dispatched at `0x004E30E0` on the word
at module `+0x10`. Two transition idioms:

- `mov word [esi+0x10], N` — take state N next frame.
- `mov eax, N ; jmp 0x004E410C` — re-dispatch into state N **this** frame.
  (`0x004E410C` is the bounds check and tail-loop back into the table.)

States worth knowing:

| state | what it is |
|---|---|
| 0x15 (21) | slot list |
| **0x20 (32)** | enters block selection; sets `+0x48 = 5` and the header message (entry 4 save / 29 load) |
| **0x21 (33)** | **the block cursor loop** — 3 columns × 10 rows |
| **0x36 (54)** | **opens the overwrite confirmation** |
| **0x37 (55)** | **the confirmation's input loop** |
| 0x38 (56) | commit — checks free space, then the write chain 0x3A → 0x3B → … |
| 0x28/0x29 (40/41) | the format-GAME-FOLDER confirm — same query window, same `+0x4F` cursor |
| 0x46 (70) | "No empty blocks. Need 1 block to save." |
| 0x48 (72) | the load path |

### Block selection, and the status table

State 0x21 drives the cursor with `0x004C0A30(keys, 3, column)` over a 3-wide
grid (`idiv 3` for the column, `imul 0x55555556` for the row), then on confirm
reads **`0x01D8CB30`** — a 30-byte table, one entry per block — and branches at
`0x004E3880`:

| `+0x49` (mode) | status at `0x01D8CB30[block]` | branch |
|---|---|---|
| 1 (load) | `1` | state 0x48 — load |
| 1 (load) | anything else | error beep |
| 0 (save) | `0` (empty) | state 0x38 — save straight in |
| 0 (save) | `1` or `2` | **state 0x36 — the overwrite confirmation** |
| 0 (save) | anything else | error beep |

State 0x22 (34) fills the whole table with `0x04` before a re-scan, so **4 means
"not yet known"**, not a save state.

### The overwrite confirmation

State **0x36** (`0x004E3B0E`) opens the box in a single frame:

```
mov  byte [esi+0x48], 4            ; panel id = the confirmation
mov  byte [esi+0x4F], 1            ; cursor defaults to the SECOND option
call 0x004BD630 (1, 5, 5, 0)       ; "Data exists.  Overwrite?"
call 0x004C2B10 (text, 0x54)       ; open the generic yes/no query window
mov  word [esi+0x10], 0x37         ; -> the input loop
```

State **0x37** (`0x004E3B3D`) runs `0x004C0A30(keys, 2, cursor)`, writes the
result back to `+0x4F`, moves the hand icon with `0x004C2900(cursor)`, and on
confirm does this at `0x004E4C49`:

```
dl = [esi+0x4F] ; neg dl ; sbb edx,edx ; and edx,0xFFFFFFE8 ; add edx,0x38
```

`0x38` when the cursor is 0, `0x20` when it is anything else. **Cursor 0 is Yes
and cursor 1 is No, and the game arms it on No.** That is the branch target, not
an inference from which option is drawn first. Cancel (`0x004E4C5D`) also returns
to `0x20`.

**`0x004E3B37` is the only instruction in the whole state machine that writes
state `0x37`**, so `state == 0x37` is a sufficient gate on its own — which is
what lets the reader avoid depending on the panel byte, a value other screens
also use.

### A trap in state 0x38

Reading it quickly suggests the opposite of what it does:

```
cl = [0x1d8cb30 + block] ; test cl,cl ; jne 0x004E4C86   ; occupied -> "error"?
... free-space check ... ; cmp ecx,eax ; jge 0x004E4C86  ; enough room -> same target
mov eax, 0x46 ; jmp 0x004E410C                            ; -> "No empty blocks"
```

`0x004E4C86` is the **normal** path — it sets `+0x48 = 7` and fetches message
entry 9, *"Saving data"*. State `0x46` is the failure. Both branches into
`0x004E4C86` are correct: overwriting an occupied block does not need a free
block, and having enough free space does not either.

## The query window is generic

`0x004C2B10(text, pos)` tail-calls `0x004C2A20(text, opt0, opt1, pos)`, which
parks three pointers in globals when the window opens:

| address | contents |
|---|---|
| **0x01D77300** | the prompt |
| **0x01D772F0** | option 0 — null arg → `getter(0, 0, 0x2F, 0)`, "Yes" |
| **0x01D772E0** | option 1 — null arg → `getter(0, 0, 0x30, 0)`, "No" |
| 0x01D772E8 / EA | window x / y |
| 0x01D772F8 / FA | cursor origin x / y |

The Save screen passes `0` for both options, so the box always uses the global
Yes/No pair rather than bank 5's own entries 6 and 7.

Each pointer goes **straight into loaded menu text** — the getter at
`0x004BD630` returns a pointer rather than copying — so they are long-lived
bytes, safe to read from the mod's thread for the same reason the Magic help bar
at module `+0x24` is. They are **text-stream bytes** (`glyph + 0x20`) and must be
shifted down before `FF8TextDecode::DecodeMenuText` sees them; passing them
straight in is the v0.22.2 "AaI'UEIOE" bug.

Because this window is generic, one reader gated on a state number can cover any
screen that opens it — the format confirm at states 0x28/0x29 is the same
machinery with the same `+0x4F` cursor.

## The text getter

```
0x004BD630(a, group, entry, sub):
    base = a ? *(0x00B86D30) + 0x2E000      // the menu-text section
             : *(0x01D2BB78)                // the global bank (Yes/No live here)
    base += *(u16*)(base + group*2 + 2)     // -> the bank
    return base + *(u16*)(base + (sub + entry*2)*2 + 2)
    // 0x01D7714C on any miss -- the "no string" sentinel
```

Entries are **pairs** — `{label, help}` at sub 0 and 1 — the same shape
`MAGIC_MENU.md` documents for group 8.

## The Save screen's own strings

Decoded offline from `Data/lang-en/menu.fs` → `mngrp.bin`, **section 1 (file
offset 0x800), bank 5**. This is the bank `getter(1, 5, …)` reaches, confirmed
rather than assumed: entries 4, 9, 24 and 29 are exactly what the state machine's
other fetch sites ask for, at the states where those messages belong.

```
 0  Save menu                    20  NEW GAME
 1  GAME FOLDER                  21  Continue
 2  Slot 1                       22  Failed to format GAME FOLDER
 3  Slot 2                       23  Saved data is damaged
 4  Choose block to save         24  No empty blocks.\nNeed 1 block to save.
 5  Data exists.  Overwrite?     25  Checking GAME FOLDER
 6  Yes                          26  Data saved
 7  No                           27  Can't load damaged data
 8  Loading data                 28  No save data
 9  Saving data                  29  Choose block to load
10  No MEMORY CARD               30  DISC
11  The MEMORY CARD in slot 1…   31  FINAL FANTASY VIII
12  The GAME FOLDER in slot 2…   32  Chocobo World
13  Formatting                   33  Data from another title
14  Formatting complete          34  GAME FOLDER in use: Slot 1
15  Do not remove the controller… 35  GAME FOLDER in use: Slot 2
16  Checking GAME FOLDER         36  Data is damaged
17  unused block                 37  No 'Chocobo World'
18  Failed to save file          38  Failed to check GAME FOLDER
19  Failed to load file          41  Not formatted
```

Entries 11 and 12 are the format prompt, fetched as `0x0B + slotCursor` at
`0x004E4FC8` — which is what identifies states 0x28/0x29 as the format confirm.

## What v0.23.4 ships

`src/menu_tts_save_query.inl`: find the module by its update function, gate on
`state == 0x37 && panel == 4`, speak the game's own prompt and the armed option,
then the option alone on each move. Aaron's call on the wording — the game's
words and nothing else, because the block's contents were spoken a keypress
earlier when the cursor landed on it.

The closing behaviour is where the real gap was. Cancelling on No returns to a
block list that redraws silently, so the reader clears
`menu_tts_save.inl`'s `s_prevBlockCursor` and that file's existing, already-BAT'd
block line re-reads itself. On Yes the latch is left alone — the screen moves on
by itself and re-announcing would talk over the save.

Gated by `tests/menu_save_query_compile.cpp`, which maps the real pool and the
real query-window globals. **What it cannot prove is that any of these addresses
are right** — an offline fixture agrees with whatever the file believes. That is
what the BAT is for, and specifically the field-save-point half of it: the pool
slot differs there and nothing offline can tell.

## Still silent, and cheap to fix

All of these are the same module, distinguished by the panel byte `+0x48` the
mod already reads:

| `+0x48` | screen |
|---|---|
| 4 | the overwrite confirm (**done**) |
| 7 | "Saving data" |
| 8 | the format confirm (states 0x28/0x29, same `+0x4F` cursor) |
| 0xA | "No empty blocks. Need 1 block to save." |

Plus "Data saved" (entry 26) and "Failed to save file" (entry 18) on the write
chain. Related: **#104**, no save confirmation.
