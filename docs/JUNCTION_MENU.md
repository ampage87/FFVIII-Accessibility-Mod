# The Junction submenu

Everything the mod knows about FF8's Junction screen, derived from `FF8_EN.exe`
rather than from play. Companion to `docs/MAGIC_MENU.md`, which established the
menu-module structure this file assumes.

---

## 1. The module

| | |
|---|---|
| Submenu dispatch index (`pMenuStateA + 0x1E8`) | **17** |
| Update function | **`0x004DA9B0`** |
| Jump table | **`0x004DFC54`**, 74 entries (states 0..0x49) |
| State word | module `+0x10` = `pMenuStateA + 0x22E` |
| Character being edited | module `+0x43` = `pMenuStateA + 0x261` |

**L1 / R1 swaps the character being edited without leaving the screen**, and the
only sign of it is `+0x261` changing. Confirming out of character select changes
the same byte, and the two are told apart by value rather than by timing: a
confirm lands on the character the char-select screen just named, a switch never
does.

**The swap is only accepted on the action row.** Confirmed in play, and in the
exe: `+0x43` is written at exactly two instructions — `0x004DBCF2` and
`0x004DBF68`, in the handlers for states 4 and 6 — and states 4 and 6 are
dispatched from one place only, state 3 (`0x004DABB5` and `0x004DAD11`). The
swap runs `4 → 5 → 6 → 7 → 3` as a slide, resets the ability left-panel cursor
`+0x5E`, and re-syncs the two stat blocks (`0x0049A7B0`, 0x1D0 bytes), so the
preview's baseline is correct for the new character immediately.

⚠ **The swap also calls `0x004BE790`** (`0x004DABA9`, `0x004DAD05`) — the same
auto-junction routine the mod's Auto-confirm hardware breakpoint watches. That
detector is only correct because it gates on the Auto submenu being focused
(`+0x22E == 11`); without the gate every character switch would announce
"Junctioned automatically for …", and only after an Auto submenu had been opened
at some point. Do not widen that gate.

The mod has read `pMenuStateA + 0x22E` as "focus" since v0.09, which the Magic
work identified as the state word of a state machine. The Junction hook speaks
that same word rather than re-deriving a module pointer, because the existing
Junction code already depends on it being the module at pool slot 2 and two
files disagreeing about where the module is would be worse than either being
wrong.

The savemap character record is at `0x01CFE0E8 + 152 * charId`.

---

## 2. Steady states, and the one that is not

The mod may only speak in a state the player can actually sit in. A state the
machine passes *through* on the way somewhere else is reached for a single frame,
so a poll either misses it entirely or catches it once at random.

| State | Handler | What it is |
|---|---|---|
| 24 (0x18) | `0x004DCB95` | equipped ability slots (cursor `+0x27C`) |
| 28 (0x1C) | `0x004DAE08` | available-ability list (cursor `+0x270 + kind`) |
| 38 (0x26) | `0x004DD5BA` | the GF / Magic sub-option (cursor `+0x268`) |
| 41 (0x29) | — | the GF list |
| **52 (0x34)** | **`0x004DB29F`** | **the junction grid** (cursor `+0x276`) |
| **59 (0x3B)** | **`0x004DB575`** | **choosing a magic** (cursor `+0x26E`) |

**37 (0x25) is not the grid.** `0x004DB008` walks `+0x40` from 0 to `0x1000` in
steps of `0x200` and only then sets state 38 — it is the slide-in animation. The
grid is reached through the fade chain 49 → 50 → 51 → 52.

`tests/menu_sim.cpp` pins all four steady numbers and pins 37 as *not* the grid;
`tests/menu_junction_compile.cpp` walks all 74 states and asserts that exactly
two of them speak.

**Paging the spell list is also a state trip: 59 -> 60 -> 59.** State 60
(`0x004DED18`) is page-left and 62 is page-right; both rewrite the cursor and
hand back. Anything that remembers "the last state seen" therefore sees a fresh
arrival on every page turn and re-announces the header. Remember the last state
*spoken in* instead. The compile probe replays the real chain seven times and
asserts zero repeated headers.

---

## 3. The grid

The cursor at `pMenuStateA + 0x276` runs 0..19 and is **not** a slot number. It
is an index into a 20-byte table at **`0x00B88604`**:

```
group 0 (cells  0- 4)  10 15 16 17 18   ST-Atk, ST-Def x4
group 1 (cells  5- 9)   9 11 12 13 14   Elem-Atk, Elem-Def x4
group 2 (cells 10-14)   0  1  2  3  4   HP, Str, Vit, Mag, Spr
group 3 (cells 15-19)   0  5  6  7  8   (blank), Spd, Eva, Hit, Luck
```

`cursor = group * 5 + index`. Groups 2 and 3 are the two columns of the stat
page (drawn at x = 0x28 and x = 0xC8); groups 0 and 1 are the two columns of the
elemental/status page, and moving between the pages triggers the scroll states
53/54/55/56. The initial cursor is **10** — HP.

**Cell 15 duplicates slot 0 and is the blank cell.** The game skips it: at
`0x004DB314` and `0x004DB343` the wrap and the clamp both special-case group 3
so the index lands on 1, never 0. The mod reports it as "blank" rather than as a
second HP row.

`pMenuStateA + 0x277` holds the number of usable rows in the current column.

### The junction array

`char + 0x5C` is 19 bytes of junctioned spell ids —
`0x01CFE144 + 152 * charId + slot`:

```
0..8   HP Str Vit Mag Spr Spd Eva Hit Luck
9      Elem-Atk
10     ST-Atk
11..14 Elem-Def 1..4
15..18 ST-Def 1..4
```

ST-Atk comes **before** Elem-Def, which several public layout tables get wrong.
Proven at `0x00496A13`.

### Which rows are unlocked

`0x01D8B6A8 + 28 * charId` is a bitmap of the junction abilities the character's
GFs grant, and **bit N is ability id N+1** (HP-J is ability 1 and sits at bit 0).
From the gate at `0x004DE531`:

| Slot | Mask | Abilities |
|---|---|---|
| 0..8 | `1 << slot` | HP-J(1) .. Luck-J(9) |
| 9 | `0x200` | Elem-Atk-J(10) |
| 10 | `0x400` | ST-Atk-J(11) |
| 11..14 | `0x6800` | Elem-Def-J(12), x2(14), x4(15) |
| 15..18 | `0x19000` | ST-Def-J(13), x2(16), x4(17) |

All five constants land on exactly the right ability ids once you subtract one,
which is five independent agreements rather than one coincidence.

How many defence rows exist follows from the same mask: `-J` alone gives one,
`x2` gives two, `x4` gives four. The mod says "Elemental defence 2 of 4" because
nothing else tells a blind player where the column ends.

---

## 4. The preview

Two 464-byte stat blocks:

| | |
|---|---|
| `0x01CFF000` | live / previewed |
| `0x01D8B3B0` | baseline snapshot |

The game's own up/down arrow is a comparison of exactly these two, so the mod can
say what a candidate *would* do before it is committed — the one thing a blind
player otherwise cannot know.

**A junction is a trade.** Every row except the nine stat rows is a table, and
committing a spell rewrites the whole table: the outgoing spell's entries fall as
the incoming spell's rise, and the screen shows both arrows at once. The preview
therefore reports every entry that moves, in both directions, grouped by its
`(from, to)` pair — `"Stop 40 to 0 percent, Confuse 0 to 8 percent"`. The
outgoing side must be read from the **baseline** block and, for status attack,
assembled from *its own* `+0x1B4` and `+0x18C`; using the live block's status
word for both halves reports no drop at all.

Offsets within a block:

| Field | Offset | Type |
|---|---|---|
| HP | `0x0174` | u16 |
| Str / Vit / Mag / Spr / Spd | `0x01BB`..`0x01BF` | u8 |
| Luck | `0x01C0` | u8 |
| Eva / Hit | `0x01C1`, `0x01C2` | u8 |
| Elem-Def | `0x0194` | 8 x u16, 800 neutral |
| ST-Def | `0x01A4` | 13 x u8, 100 neutral |
| Elem-Atk mask / pct | `0x01C4` / `0x01C5` | u8, pct **absolute** |
| ST-Atk mask | `0x01B4` **and** `0x018C` | assembled — see below |
| ST-Atk pct | `0x01B6` | u16, **100 = nothing** |

**`pct = raw - neutral`, with no division.** `menu_tts_status.inl` reads the same
words and validated that scale in play; 0 is normal, 100 is immune, above 100
absorbs, below 0 is a weakness. The elemental *attack* percentage is the one
exception — `0x004E12C3` uses `+0x1C5` raw, while `0x004E0C7D` does
`sub eax, 0x64` on the status-attack percentage at `+0x1B6`.

### The status-attack mask is assembled, not stored

There is no 13-bit status field anywhere in the block. `0x004E0FA0` builds one:

```
mask  = block[0x1B4] & 0x7F                  statuses 0..6
if (block[0x18C] & 0x0001) mask |= 1 << 7    Sleep
if (block[0x18C] & 0x0004) mask |= 1 << 8    Slow
if (block[0x18C] & 0x0008) mask |= 1 << 9    Stop
if (block[0x18C] & 0x0200) mask |= 1 << 10   Curse
if (block[0x18C] & 0x4000) mask |= 1 << 11   Confuse
if (block[0x18C] & 0x8000) mask |= 1 << 12   Drain
```

That is FF8's real two-word status bitfield folded into the thirteen entries the
junction screen shows. **Six of the thirteen live entirely in the second word**,
Sleep among them — the likeliest ST-Atk junction there is — so reading `+0x1B4`
alone reports nothing for them. Silence, not a wrong answer, which is why it
presents as an unhooked row rather than as a bad number.

The junction draw calls it once per block and compares the two to pick its arrow
(`0x004E0C1D` baseline, `0x004E0C43` live).

### Eligibility

**The mask is rebuilt in state 58 and in state 52's input block, but not in the
page-scroll states 53..56**, so a grid line read on the frame after a column
change carries the previous column's mask. Nothing on the grid reads it, and the
"none of your magic affects this row" header is only composed in state 59 — but
do not start using it on the grid without re-checking that.

While a magic list is up, `pMenuStateA + 0x24A` is a 32-bit mask over the
character's stock: bit *s* is set when stock slot *s* holds a real spell **and**
`0x004C2E50(spellId, slot)` says it does something in the row being filled. The
loop that builds it is at `0x004DE485` (and again at `0x004DB511`).

On screen the ineligible entries are simply drawn dim. Dim is invisible to a
screen reader, so the mod says "no effect here" — the same problem, and the same
answer, as the Magic screen's greyed uncastable spells.

`0x004C2E50(spellId, slot)` reads the spell's kernel.bin entry
(`0x01CF4064 + 60 * id`) and returns, by slot:

| Slot | Field | Meaning |
|---|---|---|
| 0..8 | `entry + 0x17 + slot` | signed per-stat junction value |
| 9 Elem-Atk | `entry + 0x20` | element mask (value at `+0x21`) |
| 10 ST-Atk | `entry + 0x26` | status mask (value at `+0x24`) |
| 11..14 Elem-Def | `entry + 0x22` | element mask (value at `+0x23`) |
| 15..18 ST-Def | `entry + 0x28` | status mask (value at `+0x25`) |

**Only thirteen spells in the game have a non-zero `+0x26`** — exactly one per
junctionable status: Bio, Blind, Confuse, Sleep, Silence, Break, Death, Drain,
Pain, Berserk, Zombie, Slow, Stop. So on ST-Atk almost everything a player is
carrying genuinely does nothing, and "no effect here" spell after spell is
correct but reads like a broken hook. The mod says it once on arrival instead —
*"None of your magic affects this row"* — and keeps the per-spell qualifier for
the mixed case.

Twenty spells have a non-zero `+0x28`, so the defence side is far less sparse.

---

## 5. Elements and statuses

Both orderings are **derived from kernel.bin**, not taken from a wiki, because
the on-screen labels are sprites: unlike the Magic menu there is no in-game
string to read back, so a wrong table here would be undetectable in play.

kernel.bin's magic table (section 1, 57 entries of 60 bytes, header = u32 count
then u32 offsets) carries an element bitfield at `entry+0x22` and a status mask
at `entry+0x28`, and every bit is
pinned by a spell whose own name states it:

```
Fire->0  Blizzard->1  Thunder->2  Quake->3  Bio->4  Aero->5  Water->6  Holy->7

Death->0  Bio->1 (Poison)  Break->2 (Petrify)  Blind->3  Silence->4
Berserk->5  Zombie->6  Sleep->7  Slow->8  Stop->9
Pain->1,3,4,10  (Poison / Darkness / Silence / Curse — exactly what Pain does,
                 which is what fixes bit 10)
Confuse->11  Drain->12
```

`menu_tts_status.inl` reached the same orders from a completely different
direction (live junctions on the Status screen), and now shares these tables.

A **third** confirmation arrived with the v0.23.1 status-attack work: the bit
numbering that `0x004E0FA0` produces is the same one, and kernel.bin's ST-Atk
masks give exactly one spell per bit, each one the spell of that name — Death→0,
Bio→1, Break→2, Blind→3, Silence→4, Berserk→5, Zombie→6, Sleep→7, Slow→8,
Stop→9, Pain→1,3,4, Confuse→11, Drain→12.

---

## 6. The ability screens

State 24 is the equipped panel: cursor `pMenuStateA + 0x27C`, 0..2 = the three
command slots (`char + 0x50`), 3..6 = the four ability slots (`char + 0x54`).

State 28 is the available list, and **its cursor moves with the list**:
`0x004DAE12` addresses it as `module[0x52 + kind]`, where kind lives at
`pMenuStateA + 0x274`.

| kind | cursor | list | count | ids |
|---|---|---|---|---|
| 1 | `+0x271` | `0x01D8B258` | `0x01D8B690` | 20..38, commands |
| 2 | `+0x272` | `0x01D8B280` | `0x01D8B691` | 39..82, character abilities |

Both lists are `{id, nameIdx}` pairs, built by `0x004E0110`, which first unions
the `completeAbilities` bitmaps of the character's junctioned GFs into
`0x01D8B580[4]`.

Before v0.23.0 the mod read `+0x271` unconditionally, which is why command
abilities worked and character abilities did not.

**Party abilities are ids ≥ 83.** They are not equippable — they are simply in
force while the granting GF is junctioned — so `0x004E0110` never puts them in a
list and nothing on screen enumerates them. They are read out of the union
bitmap on demand.

---

## 7. Keys

Digits are per-screen, the way the Status screen's already are.

**Grid and magic list**

| | |
|---|---|
| 0 | character and HP |
| 1 | elemental and status attack |
| 2..7 | Str, Vit, Mag, Spr, Spd, Luck |
| 8 | Evade and Hit |
| 9 | elemental and status defence |

0 and 2..8 speak exactly what the Status screen speaks, so there is one mapping
to learn rather than two.

**Ability screens**

| | |
|---|---|
| 0 | equipped commands and abilities |
| 1 | party abilities (always active) |
| 2 | the game's description of the highlighted ability |

`S` and `E` are FF8's own keys and are left alone.

---

## 8. Open

- **State 38's two options** are read as "GF" and "Magic" by code that predates
  this investigation. The entries come from `0x00B8866C` (text group 0xB, entries
  24 and 25) and the count is 2, which is consistent, but the labels have not
  been decoded from mngrp.bin the way the Magic action row was.
- **Commit and unjunction are not announced.** Confirming on the grid goes to
  state 57 → 58 → 59; confirming in the magic list goes to state 60
  (`0x004DED18`); the remove path is button `0x40` at `0x004DE4F1`. The grid line
  re-announces on return, so the result is audible, but not as a distinct event.
- **Ability category names.** ids 83..91 are the GF abilities (SumMag, GFHP,
  Boost) and 92..115 the menu/refine ones; the mod speaks them as one "party
  abilities" list rather than inventing category headings the game does not show
  on this screen.
