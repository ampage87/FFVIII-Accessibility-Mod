# FF8 (Steam 2013, FF8_EN.exe) — Main-menu **CARD** submenu (card album)

Reverse-engineering notes for the blind-accessibility mod.
All addresses are **virtual addresses in `FF8_EN.exe`** (ImageBase 0x00400000; `.text` 0x00401000–0x00B69000, `.data` 0x00B6D000–0x0279F000).
Everything below was derived from the game's own code unless explicitly marked **UNVERIFIED**.

---

## 0. TL;DR for the mod

| thing | value |
|---|---|
| submenu dispatch index | **7** |
| creator | `0x004EF020` |
| update fn | `0x004EF6F0` (real state machine: `0x004EF180`) |
| draw fn | `0x004EF750` |
| state jump table | `0x004EF6BC`, **13 entries** (states 0..0xC) |
| **only steady state** | **5** (everything else is init / slide / fade / teardown) |
| cursor field | module `+0x2E` (u16) = **the card id 0..109 directly** (no indirection table) |
| page/level field | module `+0x2A` (u8) = 0..9 |
| rows per page | **11** (`[0x1D76A93] = 0x0B`) |
| card id | `level*11 + row` |
| card counts (savemap) | **`0x01CFEF38`**, 110 bytes, stride 1 (savemap + `0x12DC`) |
| rare-card "seen" bits | **`0x01CFEFA6`**, 5 bytes / 33 bits, for ids 77..109 (savemap + `0x134A`) |
| card stats table | **`0x00C74D00`** (and identical copy at **`0x00B96508`**), 110 × 8 bytes |
| card names | **`0x00C75074`** (u16 count=110, 110 u16 offsets, then FF8-encoded strings) |
| text group | **13 (0x0D)** of mngrp **section 1** (loaded because the Card overlay id is 12 = `menucrd.ovl`) |

---

## 1. Identification — how we know index 7 is the Card menu

Four independent proofs, in increasing strength:

### 1.1 The main menu's own entry table

`0x004BE9D0` builds the live main-menu entry array at `0x01D772B4` from the master
table at **`0x00B87FE0`**, a list of `{textEntry, actionByte}` byte pairs terminated by `0xFF`:

```
00B87FE0  01 91  02 02  03 83  05 85  04 04  34 0e  35 0a  06 07
00B87FF0  09 08  37 14  08 06  ff
```

`0x004C1098` reads the selected pair and does `dispatchIndex = actionByte & 0x1F`
(`0x004C10B2: and al, 0x1f`), then `0x004C10ED: call 0x4BDAF0` / `0x004C1359: call 0x4BDB30`.

The first byte is the text entry in **mngrp section 0 / bank 0** (the main-menu string bank);
entry *E* maps to strings *2E* / *2E+1*:

```
[12] "Card"          [13] "Look at cards"      -> entry 6
```

So the pair `06 07` is literally **“Card” → dispatch index 7**. (Cross-checks in the same table:
`03 83` → 3 = Magic, `04 04` → 4 = GF, `05 85` → 5 = Status, `08 06` → 6 = Save,
`35 0a` → 10 = Switch, `01 91` → 17 = Junction — all of which match the previously known values.
Bit `0x80` of the action byte means “pick a character first”; Card does **not** have it.)

### 1.2 The dispatch table

Submenu dispatch table `0x00B87ED8`, 8 bytes per entry `{creatorFn, textOverlayId}`, 32 entries.
Index 7 = `{0x004EF020, 12}`.

**Correction to a previously assumed fact:** the second dword is *not* a music id — it is the
**menu text-overlay id**. `0x004BDB30`/`0x004BDAF0` pass it to `0x004AC200`, which looks it up in
the 19-entry `{loadAddress, overlayName}` array built at `0x004ABD80` (base `0x01D751C0`):

```
 0 menumain.ovl   1 menucfg.ovl   2 menupty.ovl   3 menusts.ovl   4 menuabl.ovl
 5 menushop.ovl   6 menuext.ovl   7 menuitem.ovl  8 menumgc.ovl   9 menugf.ovl
10 menujnc2.ovl  11 menusav.ovl  12 menucrd.ovl  13 menututo.ovl 14 menutmag.ovl
15 menutips.ovl 16 menutest.ovl 17 mngrp.bin     18 init.out
```

**Index 7 → overlay 12 → `menucrd.ovl`.** No other dispatch index uses overlay 12.

`0x004AC200`'s jump table at `0x004AC340` then converts the overlay id to the **mngrp section**
copied into the swappable text slot at `[0x00B86D30] + 0x2E000`:
overlay 0 → section 0; overlays 1, 8, 11, 12 → **section 1**; everything else → section 2.

So the Card menu runs with **mngrp section 1** loaded, and `0x004BD630(1, group, entry, sub)`
therefore reads **section 1 / bank `group`**. Magic (overlay 8) also uses section 1 and its
group 8 is indeed section 1 bank 8 (“Use / Use magic / Exchg. …”) — consistent.
Junction (overlay 10 → section 2) group 0x0B is section 2 bank 11 (“Junction / Junctioning / Off …”) — consistent.

> **Trap that cost time earlier:** dispatch indices **20, 22 and 30** (creators `0x004C9B70`,
> `0x004D5A80`, `0x004CB2D0`) use overlay **13 = `menututo.ovl` → mngrp section 2**. Their
> `getText(1, 0x0D, …)` calls therefore hit **section 2 bank 13 = the TUTORIAL bank**
> (“Basic Operation / Card Game Rules / …”), *not* the card bank. They are the **tutorial**
> screens, not the card menu. Index 23 (`0x004D4960`, overlay 16 `menutest.ovl`) is likewise not
> the album.

### 1.3 The creator reads the card inventory

`0x004EF020` walks all 110 cards through the count getter `0x00534950` in four runs and stores
four category subtotals plus a grand total into the module (see §4.4):

```
ids   0.. 54  (0x37 = 55 iterations)  -> module +0x32   "MONSTER"
ids  55.. 76  (0x16 = 22 iterations)  -> module +0x34   "BOSS"
ids  77.. 98  (0x16 = 22 iterations)  -> module +0x36   "GF"
ids  99..109  (0x0B = 11 iterations)  -> module +0x38   "PLAYER"
                              total   -> module +0x3A   "TOTAL"
```

### 1.4 The draw fn uses text group 13 of section 1

`0x004EF750` calls `0x004BD630(1, 0x0D, 1, 0)` → section 1 bank 13 string 2 = **“Elemental”**,
and `0x004EF180` (update) calls `0x004BD630(1, 0x0D, 9|0xA|0xB|0xC, 0)` →
**“Level {0A}  Monster/Boss/GF/Player Card”**. Full bank listing in §6.2.

---

## 2. The card inventory in the savemap

Savemap base is `0x01CFDC5C` (confirmed by e.g. `0x0052321C` / `0x0054A0BD` writing script
variable 0 there).

### 2.1 Layout

| address | savemap offset | size | contents |
|---|---|---|---|
| `0x01CFEF38` | `+0x12DC` | 110 bytes | per-card byte, one per card id 0..109 |
| `0x01CFEFA6` | `+0x134A` | 5 bytes (33 bits used) | “card is known” bitfield for the **rare** cards, ids 77..109 |
| `0x01CFEFAB` | `+0x134F` | 1 byte | flag byte; bit 0 set by `0x00534A58` once all 33 rare cards are known |
| `0x01CFEFB4` | `+0x1358` | 4 bytes | Triple-Triad RNG seed (`0x00534AA0`, LCG `x = x*2879 + 1`, returns `x >> 17`) — **UNVERIFIED** that this is really part of the save file rather than scratch RAM adjacent to it |

`0x01CFEFA6 = 0x01CFEF38 + 0x6E`, i.e. the bitfield starts immediately after the 110-byte array.

### 2.2 Meaning of the per-card byte, and how a count is read

The one and only count getter is **`0x00534950  int getCardCount(int cardId)`**:

```asm
00534950  push esi
00534951  mov  esi, [esp+8]               ; cardId
00534955  cmp  esi, 0x4D                  ; 77
00534958  jge  0x534969
0053495A  mov  al, [esi + 0x1CFEF38]      ; --- COMMON CARDS (id < 77) ---
00534960  test al, 0x80                   ; bit7 = "you have seen/owned this card"
00534962  je   0x5349A7                   ;   not seen -> return -1
00534964  and  eax, 0x7F                  ; low 7 bits = how many you hold (0..100)
00534967  pop  esi
00534968  ret
00534969  lea  eax, [esi - 0x4D]          ; --- RARE CARDS (id >= 77) ---
...       ; bit (id-77) of the bitfield at 0x1CFEFA6
0053498C  mov  al, [eax + 0x1CFEFA6]
00534992  test al, bl
00534995  je   0x5349A7                   ;   bit clear -> return -1 (never seen)
00534997  mov  cl, [esi + 0x1CFEF38]
0053499F  cmp  cl, 0xF0
005349A3  sete al                         ; 1 if the byte is 0xF0 (you hold it), else 0
005349A6  ret
005349A7  or   eax, 0xFFFFFFFF            ; -1
```

So:

* **Common cards, id 0..76** — byte = `0x80 | count`.
  * `0x00` → never seen. `getCardCount` returns **-1**.
  * `0x80` → seen, own none. Returns **0**.
  * `0x80|n` → own *n* (n ≤ 100, enforced by the add function `0x005347F0` which caps at `0x64`).
* **Rare cards, id 77..109** — the *count byte* is repurposed as an **owner code**, and “do I know
  this card exists” lives in the bitfield:
  * bit `(id-77)` of `0x01CFEFA6` clear → never seen. Returns **-1**.
  * byte == `0xF0` → the card is in your hand. Returns **1**.
  * byte == `0x00` → “Used up” (card-modded / consumed). Returns **0**.
  * any other value → an NPC owner id (someone else in the world holds it). Returns **0**.

Helpers built on the same data:

| addr | signature | notes |
|---|---|---|
| `0x00534900` | `int cardIsKnown(id)` | 1/0; same seen test, no count |
| `0x00534950` | `int getCardCount(id)` | above; **-1 = unknown** |
| `0x005349B0` | `int totalCardsHeld()` | sums `getCardCount` over ids 0..109, skipping ≤ 0 |
| `0x00534A20` | `int rawOwnerByte(id)` | returns 0 for id < 77, else the raw byte |
| `0x00534A58` | `int allRareCardsKnown()` | tests 33 bits, sets `0x01CFEFAB |= 1` |
| `0x005347F0` | `void changeCard(id, mode)` | `mode == 0xF0` → +1 (cap 100), else −1 |
| `0x005348E0` | `char* getCardName(id)` | id < 110 only, see §6.1 |
| `0x00534AD0` | `int getCardField(id, sel)` | see §5.3 |

---

## 3. The state machine

Update entry `0x004EF6F0` → `0x004EF180`. Head:

```asm
004EF183  mov  ax, [0x1D76A9A]        ; local: auto-repeat direction word
004EF18B  mov  esi, [esp+0x14]        ; module
004EF196  mov  ax, [esi+0x10]         ; state
004EF19B  mov  edi, [0x1D76A9C]       ; local: newly-pressed button word
004EF1A1  cmp  eax, 0xC
004EF1A8  ja   0x4EF6A8               ; default: do nothing
004EF1AE  jmp  dword ptr [eax*4 + 0x4EF6BC]
```

**Jump table `0x004EF6BC`, 13 entries:**

| state | handler | steady? | what it does |
|---|---|---|---|
| 0 | `0x004EF1B5` | **no** | init: `+0x30 = 0`; → 2 |
| 1 | `0x004EF6A8` | **no** | unused (same target as the out-of-range default) |
| 2 | `0x004EF1D5` | **no** | waits for `0x004ABFB0()==0` (overlay load idle); → 3 |
| 3 | `0x004EF1FC` | **no** | same wait; → 4 |
| 4 | `0x004EF223` | **no** | **fade-in**: `+0x30 += 0x100` each frame until `0x1000`; clears `+0x42`; → 5 |
| **5** | **`0x004EF279`** | **YES** | **the only D-pad state** — see below |
| 6 | `0x004EF379` | **no** | page-**left** setup (one frame): level−1 (wrap 0→9), saves old page, `+0x28 = 0xF199`; → 7 |
| 7 | `0x004EF476` | **no*** | left-slide animation: `+0x28 += 0x199` until ≥ 0 → state 5. *Also* re-tests **left**(`0x8000`)→6 and **right**(`0x2000`)→8 on the edge word, so page flips can be queued. Does **not** read up/down. |
| 8 | `0x004EF4E5` | **no** | page-**right** setup (one frame): level+1 (wrap 9→0), `+0x28 = 0x0E67`; → 9 |
| 9 | `0x004EF5EC` | **no*** | right-slide animation: `+0x28 -= 0x199` until ≤ 0 → state 5; same queued-flip tests as state 7 |
| 10 (0xA) | `0x004EF638` | **no** | one instruction: `state = 0xB` |
| 11 (0xB) | `0x004EF63E` | **no** | **fade-out**: `+0x30 -= 0x100`; at ≤ 0 reloads mngrp sections 9 → `base+0x1B000` and 10 → `base+0x23000` and → 12 |
| 12 (0xC) | `0x004EF69A` | **no** | `0x004BE610(module)` (free the pool slot) + `0x004BDAC0()`; menu closes |

\* States 7 and 9 do read the *edge* button word, but only for `left`/`right`. They are timed
animations that auto-advance to state 5; the player is not “sitting in” them. **Treat 5 as the
only state where the album is interactive.**

### 3.1 Input words and bit meanings

Both are produced by `0x004BE340`/`0x004BE391`:

* `0x01D76A98` (u16) — raw held
* **`0x01D76A9A` (u16)** — held/auto-repeat; used for cursor movement
* **`0x01D76A9C` (u32)** — newly-pressed (edge); used for buttons and for the queued page flips

Bit assignment (proved by `0x004C0A30`/`0x004C0A80` and the main menu at `0x004C1126`):

| bit | meaning |
|---|---|
| `0x0010` | **Cancel** (plays sound 3 via `0x004B92A0`) |
| `0x0040` | Confirm (sound 2) — **the card album never tests this** |
| `0x1000` | Up |
| `0x2000` | Right |
| `0x4000` | Down |
| `0x8000` | Left |

`0x004BE3C5: test esi, 0xF000` confirms `0xF000` is the direction nibble (the analog stick is
folded into the same bits by `0x0049EFE0`).

### 3.2 State 5 in detail (`0x004EF279`)

```
level = cursor / 11                     ; cursor = [module+0x2E]
row   = cursor % 11
newRow = 0x004C0A30(repeatWord, 11, row)     ; UP=0x1000 -> row-1 (wrap to 10)
                                             ; DOWN=0x4000 -> row+1 (wrap to 0)
if (newRow != row) { [+0x40] = row; [+0x3E] = 0; }   ; cursor-move animation bookkeeping
0x004BD850(1, 0, 0x25, 13*row + 0x26)        ; queue hand cursor at (0x25-0x1A, 13*row+0x29)
cursor = level*11 + newRow                   ; [module+0x2E] = cursor
n = getCardCount(cursor)
if (n < 0)  [module+0x20] = NULL              ; unknown card -> no header text
else        [module+0x20] = getText(1, 0x0D, cursor<55 ? 9 : cursor<77 ? 0xA : cursor<99 ? 0xB : 0xC, 0)
if (repeatWord & 0x8000) state = 6            ; LEFT  -> previous level page
if (repeatWord & 0x2000) state = 8            ; RIGHT -> next level page
if (edgeWord  & 0x0010) { sound(3); state = 0xA; }   ; CANCEL -> close
```

There is **no Confirm handler** — the album is strictly read-only.

### 3.3 Module fields (module base = pool slot base; `pMenuStateA + 0x21E` when the module lands in pool slot 2)

The module pool is `0x01D76BC8`, stride `0x78`, 10 slots; header `+0x00 next, +0x04 prev,
+0x08 update, +0x0C draw, +0x10 state (u16), +0x12 in-use`. Find the card module by scanning the
pool for `in-use && update == 0x004EF6F0`.

| off | type | meaning |
|---|---|---|
| `+0x10` | u16 | state (see table above) |
| `+0x20` | ptr | current “Level *n*  … Card” header string |
| `+0x24` | ptr | outgoing page's header string (slide) |
| `+0x28` | s16 | horizontal page-slide offset (0 when settled) |
| `+0x2A` | u8 | **current level page 0..9** |
| `+0x2B` | u8 | outgoing level page |
| `+0x2C` | u16 | outgoing cursor (card id) |
| **`+0x2E`** | **u16** | **cursor = card id 0..109** |
| `+0x30` | s16 | fade level, 0..0x1000 |
| `+0x32` | u16 | MONSTER subtotal (cards held, ids 0..54) |
| `+0x34` | u16 | BOSS subtotal (ids 55..76) |
| `+0x36` | u16 | GF subtotal (ids 77..98) |
| `+0x38` | u16 | PLAYER subtotal (ids 99..109) |
| `+0x3A` | u16 | TOTAL |
| `+0x3E` | u16 | row-move animation counter |
| `+0x40` | u16 | previous row (row-move animation) |
| `+0x42` | u8 | 1 while the level's card artwork has not finished loading (creator sets 1, state 4 clears) |

Related globals: `0x01D8D444` = graphics section currently resident at `base+0x1B000`;
`0x01D8D446` = graphics section requested (`level + 0x30`). mngrp sections **48..57 (0x30..0x39)**
are the card artwork for levels 1..10.

---

## 4. The list layout

### 4.1 There is no indirection table

Unlike the junction grid (`0x00B88604`), the card album maps **cursor → card id 1:1**.

* `cursor = [module+0x2E]`, range 0..109.
* `level = cursor / 11` (0..9), `row = cursor % 11` (0..10).
* Up/Down move `row` with wrap **inside the page** (`0x004C0A30(input, 11, row)`).
* Left/Right change `level` with wrap (state 6: `level-1`, `jns` else 9; state 8: `level+1`, `cmp 0xA` else 0). **`row` is preserved across a page change.**

### 4.2 Rows

`0x004EFC00` sets `[0x1D76A93] = 0x0B` → **11 rows, all visible, no scrolling within a page.**
`0x004B2900` invokes the row callback `0x004EFCD0` as
`cb(surf, vtx, levelByte, rowIndex, xOffset)`, and the callback computes

```asm
004EFCD9  lea ecx, [eax + eax*4]     ; eax = level
004EFCDC  lea edx, [esi + eax]       ; esi = row
004EFCDF  lea eax, [edx + ecx*2]     ; cardId = row + 11*level
```

The two panels rendered by `0x004B2900` are the incoming page (`[0x1D76A96] = module+0x2A`) and the
outgoing page (`[0x1D76A97] = module+0x2B`) during a left/right slide.

Hand-cursor placement (state 5, `0x004BD850(1, 0, 0x25, 13*row + 0x26)`) resolves to
screen (x = 0x0B, y = 13·row + 0x29) → **row pitch 13 px**.

### 4.3 Row rendering, and what happens to unobtained cards

`0x004EFCD0`:

```asm
004EFCE7  call 0x534950              ; getCardCount(cardId)
004EFCF3  test eax, eax
004EFCF5  jge  0x4EFCFD
004EFCF7  ...  ret                   ; count < 0 -> DRAW NOTHING (blank row)
...
004EFD3D  mov  esi, [esp+0x34]       ; the count
004EFD45  neg  esi
004EFD47  sbb  esi, esi
004EFD50  and  esi, 6
004EFD58  inc  esi                   ; colour = (count != 0) ? 7 : 1
004EFD59  call 0x5348E0              ; getCardName(cardId)
004EFD6C  call 0x4BDE30              ; draw name at x+0x15
004EFD8B  call 0x4A3530              ; draw the count as a number at x+0x9A
```

So, for every row:

* **never seen (`count < 0`)** → the row is **completely blank** (no name, no number).
* **seen but own 0** → name drawn in **palette 1 (dimmed/grey)**, count `0`.
* **own ≥ 1** → name drawn in **palette 7 (normal)**, count printed on the right.

For a screen reader this means: read `getCardCount(id)`; `-1` → announce nothing / “unknown”,
`0` → “<name>, none”, `n` → “<name>, n”.

### 4.4 The four “level grouping” labels

The category is a pure range test on the card id and appears three times identically
(`0x004EF2FE`, `0x004EF42A`, `0x004EF59A`):

| card ids | levels | text group-13 entry | resulting string |
|---|---|---|---|
| 0..54 | 1..5 | 9 | `Level {0A}  Monster Card` |
| 55..76 | 6..7 | 0x0A | `Level {0A}  Boss Card` |
| 77..98 | 8..9 | 0x0B | `Level {0A}  GF Card` |
| 99..109 | 10 | 0x0C | `Level {0A}  Player Card` |

The `{0A}` placeholder is filled in the header callback `0x004EFAC0` with
`0x00534AD0(cardId, 1)` = `cardId/11 + 1`, i.e. **the level number 1..10**.

The same four ranges give the summary panel (table at `0x00B88A78`, `{textEntry, fieldIdx}` pairs,
terminated by `0x0FFF`):

```
{2,0} MONSTER -> module+0x32   {3,1} BOSS -> +0x34   {4,2} GF -> +0x36
{5,3} PLAYER  -> module+0x38   {6,4} TOTAL -> +0x3A
```

These totals are **cards held (duplicates counted)**, not “unique cards discovered”:
`0x004EF020` sums `getCardCount()` and only skips values ≤ 0.

### 4.5 Screen furniture (for spatial descriptions)

| element | position | source |
|---|---|---|
| “Level *n* … Card” header | window at (0x18, 0x06), 0xF5 × 0x16 | `0x004EFA30` |
| 11-row card list | window at (0x1E, 0x1E), 0xA1 × 0x9F | `0x004EFC00` |
| card artwork | (0x117, 0x24) | `0x004EFB60` |
| the 4 power numbers | (0xC0+x, 0x1E+y) with the table at `0x00B88A68` | `0x004EF849` |
| “Elemental” label | (0xC8, 0x4A) | `0x004EF79D` |
| element icon(s) / “N/A” | (0xD1 + 0x10·i, 0x58) | `0x004EF7DB` / `0x004EF81E` |
| MONSTER/BOSS/GF/PLAYER/TOTAL panel | (0xC0, 0x1E), 0xA1 × 0x4C, rows 14 px apart from y=0xC7 | `0x004EF8E3` |
| bottom “MONSTER:/AREA:” info line | (0x18, 0xC0), 0x150 × 0x16 | `0x004EFDA0` |

---

## 5. The card's five numbers

### 5.1 The table

**Two byte-identical copies exist** (verified equal over all 880 bytes):

* `0x00C74D00` — used by the Triple Triad board renderer (`0x0053A2CB`, `0x0053A344`, `0x0053A8A0`, …)
* `0x00B96508` — used by the menu getter `0x00534AD0`

**Stride 8 bytes**, indexed by card id 0..109 (`table + id*8`). Layout:

| byte | meaning |
|---|---|
| `+0` | **Top** |
| `+1` | **Bottom** |
| `+2` | **Left** |
| `+3` | **Right** |
| `+4` | element bitmask |
| `+5` | AI strength rating |
| `+6`,`+7` | always 0 (padding) |

The power values are stored as **plain integers 1..10** — *not* nibble-packed in this build.
`10` renders as the glyph “A” (`0x0053A2D8: shl al, 4` selects the value's row in the digit
texture — 11 rows, 0..10; and in the menu `0x004EF863: cmp eax, 0xA / jl` picks glyph `0x7B` for
10 and `0x70+value` otherwise).

The table starts at `0x00C74D00` and ends at `0x00C75070` (`110 × 8 = 0x370`), immediately
followed by the sentinel `FF FF FF 00` and then the name table at `0x00C75074`.

### 5.2 Proof of the Top/Bottom/Left/Right order

Two independent proofs, from two different renderers:

**(a) Triple Triad board (`0x0053A100`).** The loop at `0x0053A1EB`…`0x0053A32F` walks a
4-entry × 8-byte position table from `0x00C75B10` to `0x00C75B30`, using loop index `i` both as the
position-table index and as `byte[cardId*8 + i]` (`0x0053A2CB: mov al, [ecx + ebx*8 + 0xC74D00]`).
The offsets (s16 x, s16 y, relative to the card centre; the digit sprite is 11×11 per `0x00C75AF0`):

```
i=0  (-26, -30)   centre (-20.5, -24.5)   8 px ABOVE  -> TOP
i=1  (-26, -14)   centre (-20.5,  -8.5)   8 px BELOW  -> BOTTOM
i=2  (-30, -22)   centre (-24.5, -16.5)   4 px LEFT   -> LEFT
i=3  (-22, -22)   centre (-16.5, -16.5)   4 px RIGHT  -> RIGHT
```

**(b) The card menu itself (`0x004EF849`).** It loops `sel = 2..5` over
`0x00534AD0(cardId, sel)` — which returns `byte[cardId*8 + (sel-2) + 0xB96508]` — paired with a
4-entry position table at `0x00B88A68`, drawn at `(0xC0 + x, 0x1E + y)`:

```
sel=2  (0x21, 0x04) -> (225, 34)   top of the diamond      -> TOP
sel=3  (0x21, 0x1C) -> (225, 58)   bottom of the diamond   -> BOTTOM
sel=4  (0x11, 0x10) -> (209, 46)   left  of the diamond    -> LEFT
sel=5  (0x31, 0x10) -> (241, 46)   right of the diamond    -> RIGHT
```

Cross-checks against the well-known card values (T/L/R/B):
Geezard **1/5/4/1**, PuPu **3/1/A/2**, Quezacotl **2/4/9/9**, Ifrit **9/8/6/2**,
Shiva **6/9/7/4**, Diablos **5/3/A/8**, Bahamut **A/6/8/2**, Eden **4/A/4/9**,
Squall **A/9/4/6** — all match the table read in T,B,L,R order.

### 5.3 Element byte (`+4`) — a bitmask

`0x0053A344` reads `byte[cardId*8 + 0xC74D04]`, and if non-zero finds the **lowest set bit**
(`0x0053A35A` loop, bit index 0..7) to pick the element icon; the menu at `0x004EF7DB` loops all
8 bits and draws icon `0x004C25D0(bit + 0x15)` for each set one, or the string “N/A”
(group 13 entry 13) when the byte is 0.

| bit | value | element | witnesses in the table |
|---|---|---|---|
| 0 | `0x01` | Fire | Bomb, Ifrit, Phoenix, Ruby Dragon |
| 1 | `0x02` | Ice | Glacial Eye, Snow Lion, Shiva |
| 2 | `0x04` | Thunder | Gayla, Cockatrice, Quezacotl |
| 3 | `0x08` | Earth | Fastitocalon, Sacred, Minotaur |
| 4 | `0x10` | Poison | Anacondaur, Malboro, Doomtrain |
| 5 | `0x20` | Wind | Thrustaevis, Elvoret, Pandemona |
| 6 | `0x40` | Water | Chimera, Leviathan |
| 7 | `0x80` | Holy | Alexander |
| — | `0x00` | none (“N/A”) | most cards |

No card in the table has more than one bit set.

### 5.4 The AI rating byte (`+5`)

Not displayed anywhere in the album. It is a precomputed strength score; verified for **all 110
cards** that

```
byte[+5] == (T*T + B*B + L*L + R*R) / 2      (integer division)
```

e.g. Geezard 1+1+25+16 = 43 → 21 = `0x15`; Squall 100+36+81+16 = 233 → 116 = `0x74`.

### 5.5 `0x00534AD0  int getCardField(int cardId, int sel)`

Jump table `0x00534B94`, `sel` 1..7:

| sel | returns |
|---|---|
| 1 | `cardId/11 + 1` — **card level 1..10** |
| 2 | Top |
| 3 | Bottom |
| 4 | Left |
| 5 | Right |
| 6 | element bitmask |
| 7 | `char*` — the bottom info line (see below) |
| other | 0 |

**sel 7**, the “MONSTER:” / “AREA:” line drawn by `0x004EFE30`
(label = group 13 entry **7 “MONSTER”** for id < 77, entry **8 “AREA”** for id ≥ 77):

* **id < 77** — a static string from the table pointed to by `[0x00B96500]` = **`0x00C75524`**
  (u16 count `219`, then `{u16 offset, u16 pad}` per card, strings follow). This is the monster
  that carries the card (levels 1–5) or the pair of monsters that play it (levels 6–7). Dumped in §7.
* **id ≥ 77** — dynamic, from the savemap owner byte:
  * byte `0x00` → the string at `[0x00B96504] = 0x00C74B58`, index 51 → **“Used up”**
  * byte `0xF0` → `0x0047EB50(0)` — you hold it
  * otherwise → `ownerTextId = byte[0x00B96878 + ownerByte]`, then `0x004C0660(ownerTextId)`
    resolves it against the `areames.dc1` bank (`[0x01D2BB48]`) — the NPC/area currently holding
    the card. The `0x00B96878` map is 110-ish bytes long and starts right after the stat table
    copy at `0x00B96508 + 0x370`.

### 5.6 The card artwork

`0x004EFB60` draws sub-image `cursor % 11` of the mngrp section `level + 0x30` (sections 48..57,
0xC800 bytes each) loaded at `[0x00B86D30] + 0x1B000`. If the section isn't resident yet
(`[0x01D8D444] != level + 0x30`) or `module+0x42 != 0`, or if the selected card is unknown
(`getCardCount < 0`, branch at `0x004EF8B9`), it draws sub-image **11** instead — the card back.

---

## 6. Text

### 6.1 Card names — `0x005348E0`

```asm
005348E0  mov eax, [esp+4]
005348E4  cmp eax, 0x6E              ; 110
005348E7  jge 0x5348FB               ; -> NULL
005348E9  mov ecx, [0xB964F8]        ; = 0x00C75074 (static)
005348F1  mov dx, [ecx + eax*2 + 2]
005348F8  add eax, ecx               ; base + offset
```

Table at **`0x00C75074`**: `u16 count = 110`, then 110 `u16` offsets (relative to the table base),
then the strings. Text encoding is the usual FF8 one (**byte = glyph index + 0x20**, `sysfnt`
14×16 grid), NUL-terminated. Full dump in §7.

### 6.2 mngrp section 1 / bank 13 — the Card group (`group = 0x0D`)

`0x004BD630(1, group, entry, sub)` returns `bank + u16[bank + (entry*2 + sub)*2 + 2]`, i.e.
each *entry* owns a pair of strings (0 = label, 1 = description; here the descriptions are all
the `{1C}` filler). File offset in `mngrp.bin` is `0x15D4` (section 1 base `0x0800` + `0x0DD4`).

| entry | sub 0 | used by |
|---|---|---|
| 0 | `Empty` / (sub1 `??`) | not used by this screen |
| 1 | `Elemental` | `0x004EF79D` |
| 2 | `MONSTER` | summary row → module+0x32 |
| 3 | `BOSS` | → module+0x34 |
| 4 | `GF` | → module+0x36 |
| 5 | `PLAYER` | → module+0x38 |
| 6 | `TOTAL` | → module+0x3A |
| 7 | `MONSTER` | bottom info label, common cards |
| 8 | `AREA` | bottom info label, rare cards |
| 9 | `Level {0A}  Monster Card` | header, ids 0..54 |
| 10 | `Level {0A}  Boss Card` | header, ids 55..76 |
| 11 | `Level {0A}  GF Card` | header, ids 77..98 |
| 12 | `Level {0A}  Player Card` | header, ids 99..109 |
| 13 | `N/A` | element byte == 0 |

---

## 7. Full card table (all 110 cards)

`T`/`B`/`L`/`R` are Top/Bottom/Left/Right as stored at `0x00C74D00 + id*8 + 0..3`
(`A` = 10). `rating` is byte `+5`. The last column is the sel-7 “MONSTER” line
(`0x00C75524`) for common cards; rare cards resolve theirs at runtime (§5.5).

| id | name | lvl | cat | T | B | L | R | elem | rating | MONSTER/AREA line |
|---:|------|----:|-----|--:|--:|--:|--:|------|------:|---|
| 0 | Geezard | 1 | Monster | 1 | 1 | 5 | 4 | - | 21 | Geezard |
| 1 | Funguar | 1 | Monster | 5 | 1 | 3 | 1 | - | 18 | Funguar |
| 2 | Bite Bug | 1 | Monster | 1 | 3 | 5 | 3 | - | 22 | Bite Bug |
| 3 | Red Bat | 1 | Monster | 6 | 1 | 2 | 1 | - | 21 | Red Bat |
| 4 | Blobra | 1 | Monster | 2 | 1 | 5 | 3 | - | 19 | Blobra |
| 5 | Gayla | 1 | Monster | 2 | 4 | 4 | 1 | Thunder | 18 | Gayla |
| 6 | Gesper | 1 | Monster | 1 | 4 | 1 | 5 | - | 21 | Gesper |
| 7 | Fastitocalon-F | 1 | Monster | 3 | 2 | 1 | 5 | Earth | 19 | Fastitocalon-F |
| 8 | Blood Soul | 1 | Monster | 2 | 6 | 1 | 1 | - | 21 | Blood Soul |
| 9 | Caterchipillar | 1 | Monster | 4 | 4 | 3 | 2 | - | 22 | Caterchipillar |
| 10 | Cockatrice | 1 | Monster | 2 | 2 | 6 | 1 | Thunder | 22 | Cockatrice |
| 11 | Grat | 2 | Monster | 7 | 3 | 1 | 1 | - | 30 | Grat |
| 12 | Buel | 2 | Monster | 6 | 2 | 3 | 2 | - | 26 | Buel |
| 13 | Mesmerize | 2 | Monster | 5 | 3 | 4 | 3 | - | 29 | Mesmerize |
| 14 | Glacial Eye | 2 | Monster | 6 | 4 | 3 | 1 | Ice | 31 | Glacial Eye |
| 15 | Belhelmel | 2 | Monster | 3 | 5 | 3 | 4 | - | 29 | Belhelmel |
| 16 | Thrustaevis | 2 | Monster | 5 | 2 | 5 | 3 | Wind | 31 | Thrustaevis |
| 17 | Anacondaur | 2 | Monster | 5 | 3 | 5 | 1 | Poison | 30 | Anacondaur |
| 18 | Creeps | 2 | Monster | 5 | 5 | 2 | 2 | Thunder | 29 | Creeps |
| 19 | Grendel | 2 | Monster | 4 | 5 | 2 | 4 | Thunder | 30 | Grendel |
| 20 | Jelleye | 2 | Monster | 3 | 1 | 7 | 2 | - | 31 | Jelleye |
| 21 | Grand Mantis | 2 | Monster | 5 | 5 | 3 | 2 | - | 31 | Grand Mantis |
| 22 | Forbidden | 3 | Monster | 6 | 3 | 2 | 6 | - | 42 | Forbidden |
| 23 | Armadodo | 3 | Monster | 6 | 1 | 6 | 3 | Earth | 41 | Armadodo |
| 24 | Tri-Face | 3 | Monster | 3 | 5 | 5 | 5 | Poison | 42 | Tri-Face |
| 25 | Fastitocalon | 3 | Monster | 7 | 1 | 3 | 5 | Earth | 42 | Fastitocalon |
| 26 | Snow Lion | 3 | Monster | 7 | 5 | 3 | 1 | Ice | 42 | Snow Lion |
| 27 | Ochu | 3 | Monster | 5 | 3 | 3 | 6 | - | 39 | Ochu |
| 28 | SAM08G | 3 | Monster | 5 | 2 | 4 | 6 | Fire | 40 | SAM08G |
| 29 | Death Claw | 3 | Monster | 4 | 7 | 2 | 4 | Fire | 42 | Death Claw |
| 30 | Cactuar | 3 | Monster | 6 | 6 | 3 | 2 | - | 42 | Cactuar |
| 31 | Tonberry | 3 | Monster | 3 | 4 | 4 | 6 | - | 38 | Tonberry |
| 32 | Abyss Worm | 3 | Monster | 7 | 3 | 5 | 2 | Earth | 43 | Abyss Worm |
| 33 | Turtapod | 4 | Monster | 2 | 6 | 7 | 3 | - | 49 | Turtapod |
| 34 | Vysage | 4 | Monster | 6 | 4 | 5 | 5 | - | 51 | Vysage,Lefty,Righty |
| 35 | T-Rexaur | 4 | Monster | 4 | 2 | 7 | 6 | - | 52 | T-Rexaur |
| 36 | Bomb | 4 | Monster | 2 | 6 | 3 | 7 | Fire | 49 | Bomb |
| 37 | Blitz | 4 | Monster | 1 | 4 | 7 | 6 | Thunder | 51 | Blitz |
| 38 | Wendigo | 4 | Monster | 7 | 1 | 6 | 3 | - | 47 | Wendigo |
| 39 | Torama | 4 | Monster | 7 | 4 | 4 | 4 | - | 48 | Torama |
| 40 | Imp | 4 | Monster | 3 | 3 | 6 | 7 | - | 51 | Imp |
| 41 | Blue Dragon | 4 | Monster | 6 | 7 | 3 | 2 | Poison | 49 | Blue Dragon |
| 42 | Adamantoise | 4 | Monster | 4 | 5 | 6 | 5 | Earth | 51 | Adamantoise |
| 43 | Hexadragon | 4 | Monster | 7 | 4 | 3 | 5 | Fire | 49 | Hexadragon |
| 44 | Iron Giant | 5 | Monster | 6 | 6 | 5 | 5 | - | 61 | Iron Giant |
| 45 | Behemoth | 5 | Monster | 3 | 5 | 7 | 6 | - | 59 | Behemoth |
| 46 | Chimera | 5 | Monster | 7 | 5 | 3 | 6 | Water | 59 | Chimera |
| 47 | PuPu | 5 | Monster | 3 | 2 | 1 | A | - | 57 | PuPu |
| 48 | Elastoid | 5 | Monster | 6 | 6 | 7 | 2 | - | 62 | Elastoid |
| 49 | GIM47N | 5 | Monster | 5 | 7 | 4 | 5 | - | 57 | GIM47N |
| 50 | Malboro | 5 | Monster | 7 | 4 | 2 | 7 | Poison | 59 | Malboro |
| 51 | Ruby Dragon | 5 | Monster | 7 | 7 | 4 | 2 | Fire | 59 | Ruby Dragon |
| 52 | Elnoyle | 5 | Monster | 5 | 7 | 6 | 3 | - | 59 | Elnoyle |
| 53 | Tonberry King | 5 | Monster | 4 | 7 | 4 | 6 | - | 58 | Fastitocalon, Malboro |
| 54 | Wedge, Biggs | 5 | Monster | 6 | 2 | 7 | 6 | - | 62 | Snow Lion, Funguar |
| 55 | Fujin, Raijin | 6 | Boss | 2 | 8 | 4 | 8 | - | 74 | Iron Giant, Jelleye |
| 56 | Elvoret | 6 | Boss | 7 | 3 | 4 | 8 | Wind | 69 | Ochu, Bite Bug |
| 57 | X-ATM092 | 6 | Boss | 4 | 7 | 3 | 8 | - | 69 | SAM08G, Red Bat |
| 58 | Granaldo | 6 | Boss | 7 | 8 | 5 | 2 | - | 71 | Death Claw, Blobra |
| 59 | Gerogero | 6 | Boss | 1 | 8 | 3 | 8 | Poison | 69 | Cactuar, Gayla |
| 60 | Iguion | 6 | Boss | 8 | 8 | 2 | 2 | - | 68 | Tonberry, Gesper |
| 61 | Abadon | 6 | Boss | 6 | 4 | 5 | 8 | - | 70 | Abyss Worm, Blood Soul |
| 62 | Trauma | 6 | Boss | 4 | 5 | 6 | 8 | - | 70 | Turtapod, Caterchipillar |
| 63 | Oilboyle | 6 | Boss | 1 | 4 | 8 | 8 | - | 72 | GIM47N, Cockatrice |
| 64 | Shumi Tribe | 6 | Boss | 6 | 8 | 4 | 5 | - | 70 | T-Rexaur, Grat |
| 65 | Krysta | 6 | Boss | 7 | 8 | 1 | 5 | - | 69 | Bomb, Buel |
| 66 | Propagator | 7 | Boss | 8 | 4 | 8 | 4 | - | 80 | Blitz, Mesmerize |
| 67 | Jumbo Cactuar | 7 | Boss | 8 | 4 | 4 | 8 | - | 80 | Wendigo, Glacial Eye |
| 68 | Tri-Point | 7 | Boss | 8 | 2 | 8 | 5 | Thunder | 78 | Torama, Belhelmel |
| 69 | Gargantua | 7 | Boss | 5 | 6 | 8 | 6 | - | 80 | Imp, Thrustaevis |
| 70 | Mobile Type 8 | 7 | Boss | 8 | 7 | 3 | 6 | - | 79 | Blue Dragon, Anacondaur |
| 71 | Sphinxara | 7 | Boss | 8 | 5 | 8 | 3 | - | 81 | Adamantoise, Creeps |
| 72 | Tiamat | 7 | Boss | 8 | 5 | 4 | 8 | - | 84 | Hexadragon, Grendel |
| 73 | BGH251F2 | 7 | Boss | 5 | 8 | 5 | 7 | - | 81 | Behemoth, Grand Mantis |
| 74 | Red Giant | 7 | Boss | 6 | 4 | 7 | 8 | - | 82 | Chimera, Forbidden |
| 75 | Catoblepas | 7 | Boss | 1 | 7 | 7 | 8 | - | 81 | Elnoyle, Armadodo |
| 76 | Ultima Weapon | 7 | Boss | 7 | 2 | 8 | 7 | - | 83 | Elastoid, Tri-Face |
| 77 | Chubby Chocobo | 8 | GF | 4 | 8 | 9 | 4 | - | 88 | (rare: dynamic, see 5.5) |
| 78 | Angelo | 8 | GF | 9 | 7 | 3 | 6 | - | 87 | (rare: dynamic, see 5.5) |
| 79 | Gilgamesh | 8 | GF | 3 | 9 | 6 | 7 | - | 87 | (rare: dynamic, see 5.5) |
| 80 | MiniMog | 8 | GF | 9 | 9 | 2 | 3 | - | 87 | (rare: dynamic, see 5.5) |
| 81 | Chicobo | 8 | GF | 9 | 8 | 4 | 4 | - | 88 | (rare: dynamic, see 5.5) |
| 82 | Quezacotl | 8 | GF | 2 | 9 | 4 | 9 | Thunder | 91 | (rare: dynamic, see 5.5) |
| 83 | Shiva | 8 | GF | 6 | 4 | 9 | 7 | Ice | 91 | (rare: dynamic, see 5.5) |
| 84 | Ifrit | 8 | GF | 9 | 2 | 8 | 6 | Fire | 92 | (rare: dynamic, see 5.5) |
| 85 | Siren | 8 | GF | 8 | 6 | 2 | 9 | - | 92 | (rare: dynamic, see 5.5) |
| 86 | Sacred | 8 | GF | 5 | 9 | 9 | 1 | Earth | 94 | (rare: dynamic, see 5.5) |
| 87 | Minotaur | 8 | GF | 9 | 2 | 9 | 5 | Earth | 95 | (rare: dynamic, see 5.5) |
| 88 | Carbuncle | 9 | GF | 8 | A | 4 | 4 | - | 98 | (rare: dynamic, see 5.5) |
| 89 | Diablos | 9 | GF | 5 | 8 | 3 | A | - | 99 | (rare: dynamic, see 5.5) |
| 90 | Leviathan | 9 | GF | 7 | 1 | 7 | A | Water | 99 | (rare: dynamic, see 5.5) |
| 91 | Odin | 9 | GF | 8 | 3 | 5 | A | - | 99 | (rare: dynamic, see 5.5) |
| 92 | Pandemona | 9 | GF | A | 7 | 7 | 1 | Wind | 99 | (rare: dynamic, see 5.5) |
| 93 | Cerberus | 9 | GF | 7 | 6 | A | 4 | - | 100 | (rare: dynamic, see 5.5) |
| 94 | Alexander | 9 | GF | 9 | 4 | 2 | A | Holy | 100 | (rare: dynamic, see 5.5) |
| 95 | Phoenix | 9 | GF | 7 | 7 | A | 2 | Fire | 101 | (rare: dynamic, see 5.5) |
| 96 | Bahamut | 9 | GF | A | 2 | 6 | 8 | - | 102 | (rare: dynamic, see 5.5) |
| 97 | Doomtrain | 9 | GF | 3 | A | A | 1 | Poison | 105 | (rare: dynamic, see 5.5) |
| 98 | Eden | 9 | GF | 4 | 9 | A | 4 | - | 106 | (rare: dynamic, see 5.5) |
| 99 | Ward | 10 | Player | A | 2 | 8 | 7 | - | 108 | (rare: dynamic, see 5.5) |
| 100 | Kiros | 10 | Player | 6 | 6 | A | 7 | - | 110 | (rare: dynamic, see 5.5) |
| 101 | Laguna | 10 | Player | 5 | 3 | 9 | A | - | 107 | (rare: dynamic, see 5.5) |
| 102 | Selphie | 10 | Player | A | 6 | 4 | 8 | - | 108 | (rare: dynamic, see 5.5) |
| 103 | Quistis | 10 | Player | 9 | A | 2 | 6 | - | 110 | (rare: dynamic, see 5.5) |
| 104 | Irvine | 10 | Player | 2 | 9 | A | 6 | - | 110 | (rare: dynamic, see 5.5) |
| 105 | Zell | 10 | Player | 8 | A | 6 | 5 | - | 112 | (rare: dynamic, see 5.5) |
| 106 | Rinoa | 10 | Player | 4 | 2 | A | A | - | 110 | (rare: dynamic, see 5.5) |
| 107 | Edea | 10 | Player | A | 3 | 3 | A | - | 109 | (rare: dynamic, see 5.5) |
| 108 | Seifer | 10 | Player | 6 | A | 4 | 9 | - | 116 | (rare: dynamic, see 5.5) |
| 109 | Squall | 10 | Player | A | 6 | 9 | 4 | - | 116 | (rare: dynamic, see 5.5) |
---

## 8. Everything else the mod needs

### 8.1 Reading the screen

Locate the module: scan the pool `0x01D76BC8`, stride `0x78`, 10 slots, for
`byte[+0x12] != 0 && dword[+0x08] == 0x004EF6F0`. (In practice it lands in slot 2, i.e.
`0x01D76CB8`, which is `pMenuStateA + 0x21E` with `pMenuStateA = 0x01D76A9A`; do not rely on it.)

Then:

```c
u16 state  = *(u16*)(mod + 0x10);      // announce only while state == 5
u16 cursor = *(u16*)(mod + 0x2E);      // card id 0..109
u8  level  = *(u8 *)(mod + 0x2A);      // 0..9, page
int row    = cursor % 11;              // 0..10 within the page
```

Announcing the focused row:

```
level  = cursor / 11 + 1                        // 1..10
class  = cursor <  55 ? "Monster"
       : cursor <  77 ? "Boss"
       : cursor <  99 ? "GF" : "Player"
count  = getCardCount(cursor)                   // 0x00534950
name   = getCardName(cursor)                    // 0x005348E0, or the static dump in §7
```

* `count < 0` → the row is visually **blank**; say something like “empty slot, row *r* of 11”.
* `count == 0` → known but not held (drawn dimmed).
* `count > 0` → say the count.

Card face for the focused card, if you want to read it out:
`T,B,L,R = table[cursor*8 + 0..3]` (say “A” for 10), `element = table[cursor*8 + 4]`
(bitmask per §5.3, `0` → “N/A”).

Page header: `"Level {level}  {class} Card"`.
Summary panel: `MONSTER/BOSS/GF/PLAYER/TOTAL` from module `+0x32/+0x34/+0x36/+0x38/+0x3A`
(cards **held**, duplicates counted).
Bottom info line: label `MONSTER` (id < 77) or `AREA` (id ≥ 77), value from
`0x00534AD0(cursor, 7)`.

### 8.2 Navigation summary

| input | effect |
|---|---|
| Up / Down | move `row` within the current level page (wraps 10 ↔ 0); cursor stays in state 5 |
| Left | previous level page (wraps 0 → 9), `row` preserved; **state 6 → 7 → 5**, ≈21 frames of slide |
| Right | next level page (wraps 9 → 0), `row` preserved; **state 8 → 9 → 5** |
| Confirm | **nothing** — never tested |
| Cancel | state 10 → 11 (fade out) → 12 (module destroyed), back to the main menu |

Suppress announcements in states 6–9 (slide) and 0–4 / 10–12 (fade & teardown); re-announce
when the machine settles back into 5, because a page flip changes 11 rows at once.

### 8.3 Rare / unique cards

Ids **77..109** (GF cards + player cards, 33 of them) are unique. They use a completely different
storage scheme (§2.2):

* existence is a **bit** in `0x01CFEFA6`;
* the byte in the count array is an **owner code**, not a count:
  `0xF0` = you have it, `0x00` = “Used up”, anything else = an NPC owner id resolved through
  `0x00B96878` → `areames.dc1`.
* `getCardCount()` normalises this to 1 / 0 / −1, so the album shows a count of 1 or 0 as usual.

`0x00534A58` reports “all 33 rare cards discovered” and latches `0x01CFEFAB |= 1`.

### 8.4 Things the Triple Triad minigame will also need (learned in passing)

* Add/remove a card: `0x005347F0(cardId, mode)` — `mode == 0xF0` adds (common cards capped at
  100), any other value removes. It handles both the common and rare representations.
* The TT rules / prompt text bank is at `0x00C74B58` (`{u16 offset}` per index; index 3
  “Are you sure?”, 9 “Card acquired”, 11 “Card lost”, 15 “Rules: ”, 23 “· Open”, 25 “· Random”,
  27 “· Sudden Death”, 29 “Same”, 31 “Plus”, 33 “Same Wall”, 35 “· Elemental”,
  37 “· Trade Rule: ”, 39 “Null”, 41 “One”, 43 “Diff”, 45 “Direct”, 47 “All”, 49 “Play/Quit”,
  51 “Used up”).
* The board flip comparison is `0x0053A820`; it compares `stat[myCard*8 + i]` against
  `stat[hisCard*8 + (i^1)]` for the paired sides, i.e. Top(0)↔Bottom(1) and Left(2)↔Right(3) —
  another confirmation of the byte order.
* The TT RNG is `0x00534AA0` over the u32 at `0x01CFEFB4`.
* Card artwork is mngrp sections **48..57** (level 1..10), 11 faces + 1 back per section.

---

## 9. Confidence / open items

**Verified from code:**
dispatch index 7; creator/update/draw; the 13-entry state table and that only state 5 reads the
D-pad; input bit meanings; cursor == card id with no indirection; 11 rows per page and the
`row + 11*level` formula; the savemap addresses and both count encodings; the stat table address,
stride, byte order (two independent renderers) and value encoding; the element bitmask; the
`(T²+B²+L²+R²)/2` rating identity for all 110 cards; the name table; text group 13 of mngrp
section 1.

**Marked UNVERIFIED:**

* `0x01CFEFB4` (TT RNG seed) being genuinely part of the persisted savemap rather than adjacent
  scratch RAM — only its address and use are proven.
* The exact length of the owner-id → `areames` map at `0x00B96878` (it is at least 0x7B entries;
  the reachable owner ids were not enumerated).
* Sound ids: 1 = cursor move, 2 = confirm, 3 = cancel — inferred from consistent usage across
  menus (`0x004B92A0`), not from the audio code.
* The `0x0047EB50(0)` call in the rare-card “you hold it” path is assumed to yield a
  “(in your possession)” style string; the function itself was not disassembled.
* Module fields `+0x3E` / `+0x40` are described as row-move animation state from their write
  pattern in state 5; their consumers were not traced.
