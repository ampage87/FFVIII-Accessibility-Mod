# Character limit breaks in battle — how the menus work

*v0.36.0 (#94). Read out of `FF8_EN.exe` and the shipped `kernel.bin`. Nothing
here is inferred from watching the screen.*

---

## 1. There are only three shapes, not six

A battle command's kernel record (section 0, 39 entries × 8 bytes) carries a
**menu-kind byte at +5**. `0x004BC7EA` masks it with `0x1F`, and when bit `0x20`
is clear it jumps through the 9-entry table at `0x004BCA58`.

| +5 | command | idx | handler | opens |
|---|---|---|---|---|
| `0xA0` | Attack, **Renzokuken (5)**, **Duel (11)**, Mug, Cast, Stock, MiniMog | — | bit `0x20` set | **no submenu** |
| `0x80` | Magic (2) | 0 | `0x004C8840` | magic list |
| `0x81` | GF (3) | 1 | `0x004C8280` | GF list |
| `0x82` | Item (4) | 2 | `0x004C87A0` / `0x004C8550` | item list |
| `0x83` | Draw (6) | 3 | `0x004ADD10` | draw list |
| `0x84` | **Shot (14)** | 4 | `0x004C8220` → kind 3 | **Irvine**, ammunition |
| `0x85` | **Slot (16)** | 5 | `0x004C7920` | **Selphie**, its own UI |
| `0x86` | **Blue Magic (15)** | 6 | `0x004C81C0` → kind 1 | **Quistis** |
| `0x87` | Fire Cross / Sorcery / Limit (20–22) | 7 | `0x004C8190` → kind 0 | Seifer, Edea, Laguna, Kiros, Ward |
| `0x88` | **Combine (19)** | 8 | `0x004C81F0` → kind 2 | **Rinoa** |

**Squall and Zell have no submenu at all.** Renzokuken and Duel both carry
`0xA0`, so choosing the command goes straight to target selection. That is why
"just let me pick it from the command menu" is the entire job for those two —
there is no list to read, and the v0.10.22 toggle announce already names it.

So four of the six characters share **one list menu**, and Selphie has her own.

---

## 2. The shared list menu

`0x004C7D00(kind, charIdx, cmdId, 4, count, 0x004C7CD0)` sets it up and calls
`0x004FF0C0`, which fills the scratch block at `0x01D768D0`:

| address | what |
|---|---|
| `0x01D768D0` | dword — the base-address callback, **`0x004C7CD0`** |
| `0x01D768D4` | dword — name resolver for this kind |
| `0x01D768D8` | dword — description resolver |
| `0x01D768E5` | byte — visible rows, `min(count, 4)` |
| `0x01D768E6` | byte — the kind, 0–3 |
| `0x01D768EB` | byte — charIdx |
| `0x01D768EC + [0x01D768F6]` | byte — **the cursor** |
| `0x01D768F0` | byte — last usable row |
| `0x01D768F2` | byte — columns (4 for every limit list) |
| `0x01D768F4` | byte — **row count** |
| `0x01D768F5` | byte — the battle command id |
| `0x01D76904 + row` | byte — how many of that row are already queued this turn |

Rows live at `0x004C7CD0(charIdx)` = `charIdx * 464 + 0x01CFF032`, **stride 5**:
`+0` id, `+1` stock, `+4` flags. `0x004FE2A5` computes what is left as
`stock − queuedThisTurn` (skipped when the menu is 1 or 3 columns wide), and
`0x004FE2BD` refuses the row when `flags & 2`.

### The four kinds

| kind | name fn | desc fn | source | contents |
|---|---|---|---|---|
| 0 | `0x0047E6B0` | `0x0047E6E0` | kernel sec 18, stride 24, 5 rows | No Mercy · Ice Strike · Desperado · Blood Pain · Massive Anchor |
| 1 | `0x0047E650` | `0x0047E680` | kernel sec 19, stride 16, 16 rows | Laser Eye … Shockwave Pulsar — **Quistis** |
| 2 | `0x0047E4F0` | `0x004952D0` | kernel sec 24, stride 8, **2 rows** | *\<character name\>* · Angel Wing — **Rinoa** |
| 3 | `0x0047EA30` | `0x0047EA90` | item names/descriptions | ammunition — **Irvine** |

Kind 2 row 0 has **no kernel name**: `0x0047E4F0` special-cases index 0 and
returns `0x01CFDC88`, the savemap pointer, so the row reads as the dog's name.
Its description is "Fight together with *\<name\>*". This is one reason the mod
calls the engine's resolvers instead of indexing the sections itself — it gets
that behaviour for free, without knowing about it.

---

## 3. Selphie's Slot (`0x004C7920`)

Not a list. One rolled spell, a cast count, and a two-row menu.

| address | what |
|---|---|
| `0x01D768D0` | dword — **the list base itself**, `charIdx*464 + 0x01CFF032` (not a callback — this is what distinguishes the two menus) |
| `0x01D768D8` | byte — the option cursor, 0 or 1 |
| `0x01D768D9` | byte — charIdx |
| `0x01D768DB` | byte — phase, 0–0x0A (`0x004C7454`) |
| `0x01D768DC` | byte — **the rolled magic id** |
| `0x01D768DD` | byte — **how many times it will cast** |
| `0x01D768E0` | byte — the battle command id |

Phase 0 rolls (`0x004C7491` calls `0x0048CBB0(&0x01D768DC, &0x01D768DD)` — the
**only** call site, so the spell changes on a deliberate re-roll and never on an
animation frame). Phase 2 is the input state; Up/Down at `0x004C74E9` /
`0x004C7513` walks the cursor between 0 and 1 with wraparound. Confirm sets the
next phase to 6 for Cast and 8 for Do over (`0x004C758D`).

The two labels are the game's own strings, and **there are two call sites one
step apart** (v0.36.2 — v0.36.1 used the wrong one):

```
id      = listBase[(cursor + 1) * 5]
0x004C7A92   row drawer:  label = 0x0047EC70(id)          <- no offset
0x004C7538   help line:   help  = 0x0047EC70(id + 2)   -> 0x01D76860
```

Kernel section 30 stores the pair two apart on purpose: **66 "Cast" / 68 "Use
indicated magic"**, **67 "Do over" / 69 "Turn the slot again"**. Entry **65 is
"Times"** — the word beside the count.

The list holds **67 at entry 1 and 66 at entry 2**, so **cursor 0 is Do over and
cursor 1 is Cast** — confirmed by the BAT, where picking cursor 1 produced a
popup carrying `value=0x33` (Full-Cure).

---

## 3a. The index everywhere here is a PARTY SLOT (v0.36.1)

`0x01D768EB` (list) and `0x01D768D9` (Slot) hold the **battle party slot, 0–2**,
not a savemap character index, and `0x004C7CD0` takes that slot. The 2026-08-19
BAT proved it twice over: Irvine, character 2, logged `char=0` from party slot
0; and Selphie in slot 2 got the pointer `0x01CFF3D2`, which is
`0x01CFF032 + 2 × 464` exactly.

v0.36.0 compared that byte against Selphie's character index (5) and so refused
her Slot window every time it opened. Map the slot through
`0x01CFE74C[slot]` when a character index is what you want.

## 4. How the mod identifies each window

The scratch block is a **union**. `0x01D768D4` is the executing party slot
during a Draw, a signed cursor in the Slot, and a function pointer in a limit
list. So nothing is read until two creator-written fields agree — the standard
the module-pool work arrived at after `+0x46` (v0.34.0) and `+0x47` (v0.34.5)
were each keyed on a byte the engine had not written yet.

**Shared list:** `[0x01D768D0] == 0x004C7CD0` **and** `[0x01D768D4]` is the name
resolver for `[0x01D768E6]`'s kind. The first test is not merely plausible, it is
**unique**: the only four dword references to `0x004C7CD0` anywhere in the
executable are the four limit cases of the command dispatch.

**Slot:** `[0x01D768D0] != 0x004C7CD0`; the stored pointer equals
`0x01CFF032 + slot * 464` for the slot byte at `0x01D768D9` beside it; and
`0x01D768E0` holds command **16** (Slot). The third test matters because that
byte is shared with another menu — eleven writers in `0x004AExxx` — so on its
own it proves nothing; together with the pointer it means the Slot creator ran.

**And nothing else may speak while one of these is up.** Opening a limit
submenu looks exactly like *leaving* a submenu to the generic command handler —
the engine drops `submenuMode` to `0xFE` — so it announced the command it
thought it was returning to, with `interrupt=true`, ten milliseconds after the
limit line. `LimitMenuIsOpenNow()` gates both its entry and exit paths. It asks
the engine rather than reading a flag, because `s_inLimitSubmenu` is set by
`PollLimitDiag`, which runs *after* `PollTurnAndCommands` — **a flag written
later in the same frame cannot gate something that runs earlier in it.**

---

## 4a. The list is paged four rows at a time (v0.37.2)

`0x004FF170` computes `[0x01D768F1] = (lastUsable + 4) / 4` — the page count —
and hands the renderer `[0x01D768E5] = min(count, 4)` visible rows. The row
drawers (`0x004C8000` for kinds 0-2, `0x004C8090` for kind 3) index the list by
the **absolute** row, `listBase + row * 5`, and use `row & 3` only to compute
the Y position. So `[0x01D768EC + colSet]` is the absolute entry index across
every page, not a position within one.

`0x01D768E8` is **not** a scroll offset despite looking like one: `0x004C7F93`
puts it into the window's width field, so it is the open/close animation.

**An out-of-range cursor is not a failed identification.** The 2026-08-20 log
caught `0x01D768EC` reading 5 with five rows as the window closed; refusing the
whole view there tore the session down, so the next readable frame re-announced
the title instead of the row. The view survives now and the row is simply not
named.

## 5. What gets spoken

* **On open** — the command's own name from `0x0047EBD0` plus the row already
  under the cursor, in one utterance. A separate title line gets interrupted by
  the row that follows it (the v0.33.3 lesson), and battle has an ATB clock.
* **On cursor move** — the row.
* **`/`** — the description of the row under the cursor, the same key that reads
  the help bar everywhere else. Claimed only while one of these windows is open.
* **Ammunition reads a count** ("Normal Ammo, 30 left"). It is the only kind
  whose stock is a quantity the player spends; Blue Magic and Rinoa's two
  options carry 1 and would just gain a word.
* **A row that cannot be chosen says so** rather than reading like the others.
* **Selphie** — "Slot. Full-Cure, 3 times. Cast", then the option on a cursor
  move, and the whole line again after a re-roll (the cursor has not moved, the
  spell has).
* **A name the engine will not give up is not invented.** The row reads as
  "Row N" — the Card Mod rule: a confident wrong name is worse than none.

---

## 6. What is still open

* **Squall and Zell still hear "Limit Break", not "Renzokuken" / "Duel".** The
  command id is not in reach at the point `PollLimitToggle` fires, and a
  hardcoded character→limit table is exactly the sort of thing this project has
  been burned by. Naming it properly means finding where the engine swaps
  command slot 0 when the crisis flag is set.
* **Irvine's firing phase** is deliberately not covered — Aaron's call, on the
  grounds that the timer is short and any announcement would be interrupted by
  the firing itself.
* **Whether Irvine's list shows all eight ammo types or only the ones held** is
  not settled statically. The reader handles both: rows with nothing left are
  announced as unavailable rather than skipped or hidden.
* **Kind 0** (Seifer, Edea, Laguna, Kiros, Ward) is wired because it is the same
  code path, but no BAT has reached it.

---

## 7. Where the code lives

| file | what |
|---|---|
| `src/battle_limit_model.inl` | the pure model — kinds, resolver tables, the stock arithmetic, the Slot's index math |
| `src/battle_tts_limit.inl` | the reader: identification, the engine calls, the announcements, `/` |
| `tests/battle_limit_compile.cpp` | drives it at the engine's real addresses, with **executable stubs written at the resolver addresses** so the reader has to actually call them |

---

## Kind 0 — Seifer, Edea, Laguna, Kiros, Ward (v0.38.3, #99)

Settled from the shipped files without a battle, because Seifer is playable for
a handful of fights near the start and Edea only much later.

**The commands.** Kernel section 0 is 312 bytes = 39 records of 8. The menu-kind
byte at +5 reads `0x87` for exactly five of them:

| id | command | character |
|---|---|---|
| 17 | Fire Cross | Seifer |
| 18 | Sorcery | Edea |
| 20 | Limit | Laguna |
| 21 | Limit | Kiros |
| 22 | Limit | Ward |

Five commands, five characters. 19, between Sorcery and the first Limit, is
Rinoa's Combine (`0x88`).

**The dispatch.** `0x87 & 0x1F` = 7, bit `0x20` clear, so `0x004BC7EA` jumps
through `[0x004BCA58 + 7*4]` to `0x004C8190`, which is six pushes and
`call 0x004C7D00` with `push 0` for the kind. `0x004C7D00` installs
`[0x01D768D4] = 0x0047E6B0` and `[0x01D768D8] = 0x0047E6E0`.

**The rows.** `0x0047E6B0(i)` = `ax = word[0x01CF82C8 + i*24]` — kernel section
18, stride 24, five records; its text is section 48 at file offset `0x89C0`.
`0x0047E6E0` is the same record +2.

| i | name offset | name | description |
|---|---|---|---|
| 0 | 0x0000 | No Mercy | Damage all enemies |
| 1 | 0x0018 | Ice Strike | Damage one enemy |
| 2 | 0x002F | Desperado | Damage all enemies |
| 3 | 0x0048 | Blood Pain | Damage one enemy |
| 4 | 0x005F | Massive Anchor | Damage all enemies |

Searching `kernel.bin` for the encoded bytes of each name lands on
`0x89C0 / 0x89D8 / 0x89EF / 0x8A08 / 0x8A1F` — the same offsets reached from the
other direction, which is what turns a plausible layout into a settled one.

Each of these characters has **one** row. A one-row list is the shape that broke
v0.14.13 (the reader announced the command and stopped, because one row cannot
reveal a cursor), so it is the first case `tests/battle_limit_compile.cpp`
checks for kind 0, with section 18's real bytes planted in the resolver banks.

## The limit command's own name

When the toggle at `0x01D7684A` is set, row 0 of the command menu is not
"Attack" — the game scrolls the character's limit command there:

```
0x004BCE80  mov edx, [0x01D76834]     ; the four ordinary commands
            mov al,  [0x01D7684A]     ; the limit toggle
            test al, al      / je normal
            test [edi+3], 4  / je normal
            ...
0x004BCEE9  mov ebx, [0x01D76838]
0x004BCEFC  mov al,  [ebx]            ; the LIMIT command id
0x004BCEFF  call 0x0047EBD0           ; the game's own name for it
```

and `0x004BB77E` sets `[0x01D76838] = slot*464 + 0x01CFF02E`. So the limit
command is **one byte at `0x01CFF02E + slot*464`**, named by the engine's own
resolver. `LimitCommandNameForSlot()` reads it, and the turn line says
"Renzokuken", "Duel", "Fire Cross", "Sorcery" instead of a generic "Limit
Break".

This is also what makes Seifer and Edea testable through characters that are
reachable: the byte, the resolver and the sentence are the same three steps for
every character, so Squall's turn line exercises Seifer's path.
