# Shops: what the exe says (#92, v0.34.0)

Aaron: *"let's go ahead and implement support for junk shops and item shops in
the mod. Look into the exe and game files, disassemble the functionality, code,
interface, etc."*

Both screens were entirely silent before v0.34.0. The mod had never spoken a word
inside a shop, so every purchase, every sale and every weapon remodel in the game
was done blind.

---

## 1. There are two modules, and one of them is not a shop at all

`0x004EDA40` is the entry point:

```
004EDA53  call 0x4B2D70                    ; which shop is this
004EDA58  cmp  byte [eax + 0xB88918], 0x15
004EDA5F  jne  0x4EDA73                    ; -> the ITEM shop
004EDA66  call 0x4EA4D0                    ; -> the JUNK shop
```

The table at `0x00B88918` maps shop numbers 0..20 to themselves, 21..31 to 0, and
**32..39 to 21**. Type `0x15` is the Junk Shop; there are eight of them.

| | creator | update | draw |
|---|---|---|---|
| item shop | `0x004EBBA0` | `0x004EBE40` | `0x004ED1B0` |
| Junk Shop | `0x004EA4D0` | `0x004EA890` | `0x004EAFF0` |

They share no state machine, no field layout and no cursor. The creator of the
item shop even *skips its whole stock build* when the type is `0x15`
(`0x004EBC75`) — a leftover that made it look, briefly, like one module with a
special case. It is not.

---

## 2. The item shop

### Files and the tables built from them

`shop.bin → [0x01D2BB08]`, `price.bin → [0x01D2BB64]`, `mitem.bin → [0x01D2BB60]`
(`0x004EBBB5..0x004EBBF5`). shop.bin is 20 shops × 32 bytes = 16 × `{u8 item, u8 ?}`.

| address | what | built at |
|---|---|---|
| `0x01D8D038` | the stock on offer: 16 × `{u8 itemId, u8 avail}` | `0x004EBCC1` |
| `0x01D8D058` | owned quantity **by item id** | `0x004EBC45`, again at `0x004EC359` |
| `0x01D8CD18` | buy price, 200 × u32 | `0x004EBD82` |
| `0x01D8D120` | sell price, 200 × u32 | `0x004EBDA9` |

Two things worth stating plainly:

* **The availability byte does not come from shop.bin.** It comes from the
  savemap at `0x01CFE5A7 + shopType*20 + row` (`0x004EBCC9`), and rows with no
  item or no availability are **compacted out** (`0x004EBCED..0x004EBD53`). Every
  row the player can see is buyable, so the reader does not need to say
  "sold out".
* **The prices are pre-computed by the engine**, including the ability
  modifiers: buy is `base × 10` or `× 15/2` with Haggle; sell is
  `(base × sellFactor) / 2`, or `× 3/4` of that with Sell-High, both clamped to a
  minimum of 1. The mod reads the tables rather than reimplementing the formula,
  so a mistake in either can never disagree with the screen.

### Fields

| offset | what |
|---|---|
| `+0x10` | state (0..0x11, jump table `0x004ED080`) |
| `+0x20` | the highlighted item's **description** string |
| `+0x28` | gil |
| `+0x2C` | inventory base (`0x01CFE79C`) |
| `+0x30` | the current message string |
| `+0x3C` / `+0x3E` | **buy cursor / sell cursor** |
| `+0x40` | page (cursor / 8) |
| `+0x42` | top-row cursor |
| `+0x45` | shop type |
| `+0x46` | mode: 0 buy, 1 sell |
| `+0x47` | page count (2 buying, 25 selling) |
| `+0x48` / `+0x49` | quantity / its maximum |

**The two cursors are the trap.** `0x004EBF9D` reads
`word [esi + mode*2 + 0x3C]`, so buy and sell each remember their own row.
Reading one while in the other names a row from the wrong list — the same shape
as the GF Learn list, the Item target list, the Save dialog and the refine
source list. The probe switches modes with the two cursors deliberately
different and asserts the sell row and the sell price.

### States

| state | screen |
|---|---|
| `0x03` | the top row — Buy / Sell / Quit |
| `0x06` | the item list |
| `0x0B` | computes the quantity maximum (`+0x48`/`+0x49` are **written here**) |
| `0x0C` | the quantity screen |
| `0x0F` | a timed message |

`0x0B` writing the two bytes the quantity line reads is exactly the v0.33.1
refine defect, so the reader waits for a real count and a real maximum before it
says anything.

---

## 3. The Junk Shop

`mwepon.bin → [0x01D2BB50]`, `mwepon.msg → [0x01D2BB28]`. A record is 12 bytes,
read at `0x004EA7A6..0x004EA864` and `0x004EAB3E`:

| offset | what |
|---|---|
| `+0x00` u16 | offset into `mwepon.msg` — the weapon's own text |
| `+0x03` u8 | **price ÷ 10** (`0x004EA7FD` multiplies it by ten) |
| `+0x04..0x0B` | four `{u8 itemId, u8 count}` — the materials |

The weapon's owner is a separate table: `0x01CF7404 + recIdx*12`.

`0x004EA770(charId)` builds the visible list at `0x01D8CC08`, one byte per row:

* low 6 bits — the mwepon record index
* `0x40` — **seen before** (`[0x01CFE750]` bit set)
* `0x80` — **buildable right now**: gil ≥ price *and* all four materials in hand
  (`0x004EA808..0x004EA839`)

Rows that are neither are not listed. So "not available yet" in the mod is not a
second opinion about the player's inventory — it is the absence of the same bit
the screen greys the row with.

### Fields

| offset | what |
|---|---|
| `+0x10` | state (0..0x10, jump table `0x004EAFA8`) |
| `+0x20` | the weapon's `mwepon.msg` text |
| `+0x28` | gil |
| `+0x2C` | the current message |
| `+0x38` | mask of characters who have a remodel available |
| `+0x3E` / `+0x3F` | character count / weapon count |
| `+0x40` / `+0x41` | character cursor / weapon cursor |
| `+0x44 + n` | each character's currently equipped weapon index |

**The character picker indexes the set bits of `+0x38`**, through `0x004ABC40` —
the same shape as the refine screen's picker, and the same way to get it wrong.
It reuses `AbilCharAtPickerRow()` rather than growing a second copy.

---

## 4. Nothing the mod says here is a translation

`0x004C2590(id)` is `0x004BD630(1, 3, id, 0)`, and that walk is two u16 offsets:

```
base = [0x00B86D30] + 0x2E000
base += u16 at base + 3*2 + 2            ; the group
base += u16 at base + (id*2)*2 + 2       ; the string
```

So the mod resolves the game's own strings. The top row's three labels are
group-3 strings **52, 53 and 54**, terminated by `0xFFFF` at index 3 in the table
at `0x00B88910` — the count and the order come from the exe, the words come from
the game. Item descriptions arrive already resolved in `+0x20`, messages in
`+0x30` (`+0x2C` for the Junk Shop), and weapon names in `+0x20`. There is no
weapon table in this mod at all.

---

## 5. Open

* **Call Shop** — Aaron does not have the ability yet, so the path that opens a
  shop from the main menu (ability type `0x80`, `0x004E79EC`) is unexercised.
  It creates the same module, so it should work; it is untested.
* The `0x004ED110` pass over the stock, called twice at `0x004EBCDE`, is the
  Familiar ability's effect on what is offered. The reader does not need it —
  it reads the finished list — but it is not documented here.
* The item shop's "Quit" row and the confirm/decline dialogs at the end of a
  purchase go through the shared yes/no window, which is still read at only 3 of
  its 20 call sites (`SUBMENU_AUDIT.md`).


---

## 6. What the first BAT settled (v0.34.1)

The Junk Shop worked on the first build. Its messages, prices, materials,
"already equipped" and "not available yet" were all correct, and its character
picker resolved mask `0x002B` to Squall / Zell / Quistis / Selphie — bits 0, 1, 3
and 5, so **a packed list would have named Quistis "Irvine"**. That is the whole
reason §3 insists on the set-bit read.

Two things were wrong.

### `+0x46` is not written until the top menu is confirmed

`0x004EC2E1` is the only write. The creator never touches it. So for the entire
opening of a shop — states 0 through 3 — it holds whatever the previous shop left
there, and v0.34.0 refused to accept the module unless it was already 0 or 1.
The result was no `[SHOP]` line at all for an item shop.

**A byte the engine has not written yet is not evidence the module is
unreadable.** It is clamped now and read only on the list and quantity screens,
which are the screens `0x004EC2E1` has run before.

### The weapon name is not in `mwepon.msg`

The module resolves `mwepon.msg base + record[0]` into `+0x20` (`0x004EAB3E`),
which looks exactly like a name pointer and is not one: **`mwepon.msg` is 68
bytes of spaces and terminators in this release** — 34 empty strings. Every row
came back "Unknown weapon".

The draw function does not read `+0x20` either. `0x004EB590` calls
`0x0047EBA0(recIdx)`:

```
u16 off = word [0x01CF7400 + idx*12]     ; 0xFFFF = no name
return  0x01CF3E48 + [0x01CF3ED8] + off
```

The record index doubles as the weapon index — the engine's own "already
equipped" test compares the two directly at `0x004EAB94`, which is what settles
it rather than an assumption that two 12-byte tables must line up.

The lesson worth keeping: **a pointer the module hands you is not automatically
the pointer the screen draws from.** Checking what the *draw* function reads
would have caught this before the BAT did.


---

## 7. What the second BAT and the screenshots settled (v0.34.2)

Weapon names read correctly. Three things the screenshots showed the mod was not
saying, and one it still cannot reach.

### The material panel has two columns

```
M-Stone Piece
        2   100      <- required, held
Bomb Fragment
        1     0
```

v0.34.1 spoke only the requirement. The held count is what decides whether the
remodel is possible at all, and it is taken from the inventory directly —
`0x01D8D058` is filled only by the **item shop's** creator, so in a Junk Shop it
holds whatever the last item shop left there.

### The confirmation is the shared window again

State `0x0B` (`0x004EADA6`). Its text is assembled at `0x004EACDD` from template
string `0x3B` with the character name (`0x0047EB50`) and the weapon name
(`0x0047EBA0`) substituted, then handed to **`0x004C2B10`** — the same opener as
the Save overwrite prompt, the Magic transfer, the GF "Don't learn anything?".
`menu_dialog.inl` has read that window since v0.29.0; it simply had no caller
here. Cursor is `+0x42`: 0 = Yes, 1 = No.

**Four of that window's twenty call sites are now wired.**

### The bottom panel is a comparison

`Str 28 ▲ 31`, `Hit 99% ▲ 101%` — read at `0x004EB843` from

* `[0x01D8CB84 + weaponIdx*4]` — Str, computed per weapon by the creator
  (`0x004EA534` sets the character's weapon to each index in turn and asks
  `0x004BFC90` for the result)
* `byte [0x01CF7407 + weaponIdx*12]` — Hit%

And the price actually deducted is scaled by module `+0x30` (`0x004EAE2B`): 1000
normally, **750 with Haggle**.

### The item shop, take three

Two builds, a screenshot of "Balamb Shop" with Buy / Sell / Exit and a full stock
list, and no output at all.

The MRU-list walk stopped at the first entry outside the pool — correct for the
tail sentinel, wrong for anything the engine relinks. `0x004BE5B0` is a **second
allocator** that threads modules onto `0x01D76ACC` instead of `0x01D76B48`, and a
module on that list is invisible from this one. The pool, though, is ten fixed
slots at `0x01D76BC8`: reading all ten cannot miss a module that exists.

Twice now the disassembly has said this should work and the game has said
otherwise. `[SHOP-POOL]` dumps all ten slots once per entry into shop mode, so if
the scan fails too the next log answers the question instead of another guess.


---

## 8. The exe, re-verified before a fourth BAT (v0.34.5)

Aaron asked for the disassembly to be re-checked before running another test.
The model is airtight, and that is the finding: **the problem has never been the
model.**

### There is exactly one module pool

Only `0x004BE540` and `0x004BE5B0` reference `0x01D76BC8` anywhere in `.text`.
Both walk ten slots of `0x78`; both store arg0 at `+0x08` and set `+0x12`; the
free routine `0x004BE610` clears both. The two list heads — `0x01D76B48` and
`0x01D76ACC` — thread the **same array**.

A scan of ten slots therefore cannot miss a module that exists, and there is no
second pool for a shop to be hiding in.

### The dispatch table, correctly located this time

`0x004BDB30` reads `[idx*8 + 0x00B87ED8]`, pairs of `{creator, music kind}`:

| index | creator | |
|---|---|---|
| 11 | `0x004EDA40` | item shop |
| 12 | `0x004EA4D0` | Junk Shop |

§1 quoted this table with a base of `0xB87F00`, which was a guess — **nothing in
`.text` references that address**. The entries and conclusions were right; the
base was off by one entry and should have been checked when it was written.

`shop.ovl` in the string table is a dead PSX overlay name; `menushop.ovl` and its
siblings sit unreferenced in the same block. Ruled out.

### Which shop is open, without any module

`0x004B2D70` — the function both creators call to decide what they are — is two
instructions:

```
004B2D70  mov eax, [0x01D75450]
004B2D75  and eax, 0xFF
```

and the type is `byte [0x00B88918 + n]` across a 52-entry table: 0..20 map to
themselves, 21..31 to 0, **32..42 to 21** (the Junk Shops). The table's length is
exact rather than assumed — the byte after index 51 begins the string
`"shop.bin"`.

Reading this needs no module, no pool and no list walk. It is the piece that was
missing, and it was missing from the *diagnostics*, not from the analysis: with
it, the log can distinguish

* "an item shop is open and the reader could not find its module", from
* "no item shop was open during that window".

**None of the three previous BATs could tell those apart.** Reviewing them with
that in mind: the only *confirmed* item-shop visit — screenshot, "Balamb Shop",
full stock list — was at 18:09 on **v0.34.1, which still used the MRU-list walk**.
The pool scan arrived in v0.34.2 and has never demonstrably been inside one. The
22-second mode-10 window in the v0.34.3 log has no screenshot attached and may
not have been an item shop at all.

The lesson, and it is the same one as §7: **when a diagnostic cannot distinguish
two explanations, it is not evidence.** Three builds were spent on a question the
logs were never able to answer.


---

## 9. The answer: `+0x08` is clobbered, `+0x0C` is not (v0.34.6)

The dump that §8's shop-number line made interpretable:

```
slot 1 @01D76C40 inUse=1 state=0x03 upd=605D8130 draw=004ED1B0 +2C=01CFE79C +45=1 +46=0 +47=0
```

**`+0x08` holds a heap pointer, not the update fn.** `SUBMENU_AUDIT.md` §9
records the same thing for the Item *menu* module — `0x605D8200`, within a
hundred and thirty bytes of this one, so whatever writes there writes for both.
**`+0x0C`, the draw fn, is intact.**

So the identity to use is the draw fn: `0x004ED1B0` for the item shop,
`0x004EAFF0` for the Junk Shop. The reader now matches either field.

Four builds searched for a value that is not in the module while the field
beside it was correct throughout. §1 asserted "`+0x08` should hold `0x4EBE40`"
from the allocator's code and never checked it against the running game — and
the allocator *does* write it; something else overwrites it later.

### And the same dump explains the last symptom

*"When the item shop first appears, the action bar does not read out. Once I pick
an option it works."*

v0.34.5's field fallback required `+0x47 ∈ {2, 25}`. `+0x47` is written by the
**top-menu confirm** (`0x004EC2F3` / `0x004EC306`), not by the creator — the dump
shows `+0x47=0` at state 3 every time. So the fallback could not fire until the
player had already chosen Buy or Sell.

That is the **third** identification in this work keyed on a field the engine had
not written yet: `+0x46` in v0.34.0, `+0x47` in v0.34.5, and the refine screen's
`+0x4C`/`+0x4F` before them. The rule worth keeping:

> **Only test fields the creator sets.** A field written by a later state is
> evidence about that state, not about identity.


---

## 10. Mode 10 is not shop-only (v0.34.7)

The item shop reads end to end. One cleanup came out of the same log.

The pool diagnostic fired 121 times, and eighty of those were on screens with no
shop in them. Two facts behind that, both worth keeping:

* **Game mode 10 is not shop-only.** The party-switch screen (`upd=0x004CBA50`,
  dispatch 9/10) runs in it too. Mode alone does not mean "a shop is open".
* **The shop-number global at `0x01D75450` goes stale between shops.** It still
  held the previous value, so `byte [0x00B88918 + n]` returned a valid-looking
  type on a screen that was not a shop.

So the rule for anything that needs to know what kind of shop this is:

> **The module's own `+0x45` is authoritative. The global is only trustworthy
> while a shop is being opened** — which is precisely when the creator reads it
> (`0x004EDB32`), and not afterwards.

The diagnostic is gated behind `SHOP_POOL_DIAG` rather than removed. It earned
that: the single line it produced identified both the clobbered `+0x08` and the
unwritten `+0x47`.
