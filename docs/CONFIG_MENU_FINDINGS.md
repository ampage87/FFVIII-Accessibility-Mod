# FF8 (Steam 2013, FF8_EN.exe) — Main-menu **CONFIG** submenu

Reverse-engineering notes for the blind-accessibility mod.
All addresses are **virtual addresses in `FF8_EN.exe`** (ImageBase 0x00400000; `.text`
0x00401000–0x00B69000, `.data` 0x00B6D000–0x0279F000).
Everything below was derived from the game's own code unless explicitly marked **UNVERIFIED**.
Companion doc: `docs/CARD_MENU_FINDINGS.md` (§1 there explains the dispatch-table structure that
this doc builds on).

---

## 0. TL;DR for the mod

| thing | value |
|---|---|
| submenu dispatch index | **8** |
| creator | `0x004EDD30` |
| update fn | `0x004EDE90` |
| draw fn | `0x004EE750` |
| state jump table | **`0x004EE6D8`**, dispatch bounded by `cmp eax, 0xD / ja` → **states 0..13** |
| **steady states** | **3** (the option list) and **7** (the *Customize* button-assignment screen). Nothing else. |
| option-row cursor | module **`+0x28`** (s8) = row 0..8 into the row table |
| row table | **`0x00B88970`**, stride 0x10, **9 rows**, terminated by `0xFFFF` |
| row count cached in | `0x01D8D440` (u16) = **9**, written by the creator |
| settings block | **`0x01CFE738`, 20 bytes** (5 dwords) — saved/loaded with the save file |
| flag word | **`0x01CFE73C` (u16)** — all the ON/OFF-style options are bits in here |
| text | mngrp **section 1 / bank 2** (`0x004BD630(1, 2, entry, sub)`), file offset `0x824` in `mngrp.bin` |
| text overlay | 1 = `menucfg.ovl` (only dispatch index that uses it) |
| customize screen | rows `0x00B88A10` (stride 8, 10 rows), page `+0x29` (0..2), cursor `+0x2A` |
| button remap array | **`0x01CFE740`, 12 bytes**, applied globally in `0x004A2D60` (called from `0x004BE39B`) |

---

## 1. Identification — how we know index 8 is Config

Three independent proofs.

### 1.1 The main menu's own entry table

Master table **`0x00B87FE0`**, `{textEntry, actionByte}` byte pairs terminated by `0xFF`:

```
00B87FE0  01 91  02 02  03 83  05 85  04 04  34 0e  35 0a  06 07
00B87FF0  09 08  37 14  08 06  ff
```

`0x004C1098` does `dispatchIndex = actionByte & 0x1F` (`0x004C10B2: and al, 0x1f`).
The first byte is the entry in mngrp **section 0 / bank 0**, whose strings are
label = `2E`, description = `2E+1`:

```
[18] "Config"   [19] "Configuration Menu"   -> entry 9
```

So the pair **`09 08` is literally “Config” → dispatch index 8.** Bit `0x80` (“pick a character
first”) is **not** set, so Config is entered directly.

### 1.2 The dispatch table

Submenu dispatch table `0x00B87ED8`, 8 bytes per entry `{creatorFn, textOverlayId}`:

```
idx  8  creator=0x004EDD30  overlay=1
```

Overlay **1 = `menucfg.ovl`** (per the 19-entry overlay name array built at `0x004ABD80`), and
**no other dispatch index uses overlay 1** (verified over all 32 entries). `0x004AC200`'s jump
table `0x004AC340` maps overlay 1 → **mngrp section 1**, so `0x004BD630(1, group, …)` inside this
module reads section 1 / bank `group`.

### 1.3 The code reads section 1 bank 2 with `group = 2` hard-coded

`0x004EDF07: mov ebx, 2` and every `0x004BD630` call in the module passes `ebx` (or a literal `2`)
as the group — e.g. `0x004EE0E5`, `0x004EEC07`, `0x004EEF43`. Bank 2 of section 1 is the bank that
contains “Vibration function”, “Battle speed”, “Set ATB”, “Cannot use vibration function” — the
Config bank. Full dump in §7.

---

## 2. The state machine

Update entry `0x004EDE90`. Head:

```asm
004EDE9E  mov  byte [esi+0x2E], 1        ; assume a gamepad is present
004EDEA2  mov  byte [esi+0x2D], 1        ; assume vibration-capable pad
004EDEA6  call 0x49F0A0(0,0,0)           ; analog axis probe; <0 -> [esi+0x2E] = 0
004EDEB6  call 0x49F2D0 / 0x49E9A0(0)    ; rumble capability; 0 -> [esi+0x2D] = 0
004EDED0  call 0x4C3060                  ; refresh flag bit 0x40 from the pad's rumble state
004EDED5  mov  ax,  [0x1D76A9A]          ; ebp  = held/auto-repeat word
004EDEDB  mov  ecx, [0x1D76A9C]          ; edge = newly-pressed word ([esp+0x18])
004EDEF6  mov  ax, [esi+0x10]            ; state
004EDEFA  cmp  eax, 0xD
004EDEFD  ja   0x4EE6C4                  ; default: cursor bookkeeping only
004EDF0C  jmp  dword ptr [eax*4 + 0x4EE6D8]
```

**Jump table `0x004EE6D8`, entries 0..13** (entries 14..20 of the same array are *not* states —
they are the per-widget-type handler table, see §4.2; the state dispatch is bounded by
`cmp eax, 0xD`).

| state | handler | steady? | what it does |
|---|---|---|---|
| 0 | `0x004EE05A` | **no** | one frame: places the hand cursor, `state = 1` |
| 1 | `0x004EE070` | **no** | **fade-in**: `+0x24 += 0x100` each frame until `0x1000`, then `state = 2` |
| 2 | `0x004EE093` | **no** | one frame: `state = 3` and *falls straight through into the state-3 body* (so state 3's logic already runs on this frame) |
| **3** | **`0x004EE099`** | **YES** | **the option list.** Up/Down move `+0x28`; Left/Right change the focused setting; Confirm on the Controller row can open Customize; Cancel closes the menu |
| 4 | `0x004EE38D` | **no** | one frame: `+0x2A = 0` (customize cursor), `state = 5`, falls into 5 |
| 5 | `0x004EE397` | **no** | **slide-out**: `+0x26 -= 0xCC` per frame until ≤ 0 → `state = 6` (~20 frames). **Reads no input at all.** |
| 6 | `0x004EE424` | **no** | one frame: repositions the cursor, `state = 7` |
| **7** | **`0x004EDF13`** | **YES** | **the Customize (button-assignment) screen** — see §6 |
| 8 | `0x004EE4FD` | — | **unreachable dead code** (PSX-style “swap two button assignments”). Nothing ever writes state 8. |
| 9 | `0x004EE589` | — | **unreachable dead code** (toggles flag bit `0x80`, the L/R analog-stick swap) |
| 10 (0xA) | `0x004EE5F1` | **no** | `state = 0xB`, falls into 0xB. Reached **only** by the internal `mov eax,0xA / jmp 0x4EDF0C` at `0x004EE050` (leaving Customize) |
| 11 (0xB) | `0x004EE5F7` | **no** | **slide-back**: `+0x26 += 0xCC` until `0x1000` → `state = 3`. Reads no input. |
| 12 (0xC) | `0x004EE688` | **no** | `state = 0xD`, falls into 0xD |
| 13 (0xD) | `0x004EE68E` | **no** | **fade-out**: `+0x24 -= 0x100`; at ≤ 0 → `0x004BE610(module)` (free the pool slot) + `0x004BDAC0()`; the menu closes |

### 2.1 Which states the player can actually sit in

Applying the “can the player rest here?” test rather than “does it read input?”:

* **State 3 — YES.** No timer, no auto-advance; it only leaves on player input.
* **State 7 — YES.** Same: only leaves on player input.
* **States 5 and 11 — NO, and they are not even input-reading.** They are pure `+0x26`
  animations between the list and the Customize screen (`0x1000 / 0xCC` ≈ 20 frames each way).
  Unlike the Card menu's states 7/9, they do not sample the button words at all.
* **States 0, 2, 4, 6, 10, 12 — NO.** Each executes for exactly one frame and rewrites `+0x10`.
  Note state 2 falls through into the state-3 body, so a screen reader hooked on “state == 3”
  will miss that first frame; hook on “state ∈ {2,3}” if you want the very first frame.
* **State 1 — NO** (fade-in animation), **state 13 — NO** (fade-out/teardown).
* **States 8, 9 — never entered.** `mov word [esi+0x10], N` occurs only for
  N ∈ {1, 2, 3, 4, 5, 6, 7, 0xB, 0xC, 0xD} (exhaustive scan of `0x004EDE90`–`0x004EE700`), and the
  only internal re-dispatch is `eax = 0xA`.

**Announce only in states 3 and 7. Suppress everything in 0,1,2,4,5,6,10,11,12,13.**

### 2.2 Input words and bit meanings

* `0x01D76A98` (u16) — raw held
* **`0x01D76A9A` (u16)** — held/auto-repeat → used for cursor Up/Down **and for Left/Right value
  changes** (so holding Left/Right ramps a slider)
* **`0x01D76A9C` (u32)** — newly pressed (edge) → Cancel, Confirm, and the Customize screen's
  page/defaults/exit buttons

| bit | meaning | where proved |
|---|---|---|
| `0x0010` | Cancel (**state 3 only** — the Customize screen never tests it) | `0x004EE35F: test byte [esp+0x18], 0x10` → sound 3 + close |
| `0x0040` | Confirm | `0x004EE156: test dword [esp+0x18], 0x840` (Controller row) |
| `0x0100` | “restore defaults” in Customize | `0x004EE007: test ah, 1` |
| `0x0800` | “end/exit” in Customize (also accepted as Confirm on the Controller row) | `0x004EE03E: test ah, 8` |
| `0x1000` | Up | `0x004C0A30` |
| `0x2000` | Right | `0x004EE1EA`, `0x004EE2A6`, `0x004EE314` |
| `0x4000` | Down | `0x004C0A30` |
| `0x8000` | Left | `0x004EE1B1`, `0x004EE2BE`, `0x004EE331` |

Sound ids passed to `0x004B92A0`: **1** = cursor/slider tick, **2** = value changed / confirm,
**3** = cancel, **5** = error buzz (UNVERIFIED naming, consistent with the rest of the menus).

### 2.3 Module fields

Module = a slot of the standard menu pool (`0x01D76BC8`, stride `0x78`, 10 slots; header
`+0x00 next, +0x04 prev, +0x08 update, +0x0C draw, +0x10 state(u16), +0x12 in-use`).
**Find it by scanning the pool for `byte[+0x12] != 0 && dword[+0x08] == 0x004EDE90`.**
`0x004BE540` zeroes `+0x20 … +0x77` on allocation and `0x004BE610` zeroes `+0x10` on free, so the
menu always starts at state 0 with every field below zeroed.

| off | type | meaning |
|---|---|---|
| `+0x10` | u16 | state (§2) |
| `+0x20` | ptr | **help/description string of the focused row** (what the top window shows) |
| `+0x24` | s16 | fade level 0..0x1000 |
| `+0x26` | s16 | list↔Customize slide, `0x1000` = list fully shown, `0` = Customize fully shown |
| **`+0x28`** | **s8** | **option-list cursor, row 0..8** into `0x00B88970` |
| `+0x29` | s8 | Customize **page** 0..2 (Field / Battle / World Map) |
| `+0x2A` | s8 | Customize cursor row 0..9 (into `0x00B88A10`) |
| `+0x2B` | u8 | number of *visible* Customize rows (8 without a pad, 10 with one) |
| `+0x2C` | u8 | only read by dead state 8 |
| `+0x2D` | u8 | 1 = a rumble-capable pad is present (`0x49F2D0() && 0x49E9A0(0)`) |
| `+0x2E` | u8 | 1 = a gamepad is present (`0x49F0A0(0,0,0) >= 0`) |

---

## 3. The settings block

`0x004CA960` / `0x004CAA60` (save) copy **5 dwords = 20 bytes** from `0x01CFE738` into the save
buffer at `+0x27AE0`, and `0x004CADC0` (load) copies them back. So the whole configuration is
exactly these 20 bytes, and it is **stored per save slot**, not globally.

| address | size | contents |
|---|---|---|
| `0x01CFE738` | u8 | **Battle speed** 0..4 |
| `0x01CFE739` | u8 | **Battle message speed** 0..4 |
| `0x01CFE73A` | u8 | **Field message speed** 0..4 |
| `0x01CFE73B` | u8 | **Sound volume 0..100** (mirror of the audio engine's `0x01CD1794`) |
| `0x01CFE73C` | u16 | **flag word** (all toggles) |
| `0x01CFE73E` | u8 | **Camera Movement** 0..4 |
| `0x01CFE73F` | u8 | *not a Config setting* — a button-mask filter read by `0x004C3050` and by the field/world code (`0x0047FA1A`, `0x00480EE3`, `0x0051E952`, …). Config never writes it. |
| `0x01CFE740` | u8[12] | **button assignment map**, default `1..12` |

### 3.1 The flag word `0x01CFE73C`

| bit | meaning | set by Config? | consumers |
|---|---|---|---|
| `0x0001` | **ATB = Wait** (clear = Active) | yes, row 2 | `0x004A9450` (called from the battle loop `0x004842D1`) |
| `0x0002` | sound **stereo** (clear = mono) | **no row in this build** | `0x004C2FD0`: `0x46C040((flags>>1)&1)` |
| `0x0004` | **Cursor = Memory** (clear = Initial) | yes, row 1 | `0x004BB9B0`, `0x004BC668` |
| `0x0010` | set by `0x004A93A1` (input/analog related) | no | — |
| `0x0020` | **Controller = Customize** (clear = Normal) | yes, row 0 | `0x004A2D60` (button remap), `0x004A2DF0` (button icons) |
| `0x0040` | **vibration on** | no — *mirrored from the pad every frame* by `0x004C3060` | `0x004C2FF0` → `0x49F170(0, 0xFF/0)` |
| `0x0080` | L/R analog stick swapped | only by dead state 9 | `0x004C3010` |
| `0x0100` | **Scan = Always** (clear = Once) | yes, row 3 | `0x00492565` (`test ah,1`) |

Everything else is unused as far as this build's code goes.

### 3.2 Defaults / reset

* **New game / load screen**: `0x004E4501` writes `[0x01CFE73B] = 0x64` (volume 100) and calls
  `0x46A390(100)`.
* **After loading a save**: `0x004E4F35` — `if ([0x01CFE73B] < 0x14) [0x01CFE73B] = 0x64;`
  i.e. any saved volume below 20 is treated as a stale PSX-era value and forced to 100.
* `0x004E67C0` (dispatch index 16, the other `menusav` module) does
  `[0x01CFE73C] = 0` and `[0x01CFE73B] = 2`, then `0x4C3010` / `0x4C2FF0`. The `= 2` is legacy
  (it is exactly what the `< 0x14` clamp above exists to undo). **Defaults are therefore:
  flags = 0 → ATB Active, Cursor Initial, Scan Once, Controller Normal; volume 100.** The four
  speed/amount bytes are not explicitly initialised anywhere I could find — they come from the
  save file, and are 0 (= fastest / max camera movement) on a zeroed block. *UNVERIFIED: whether
  a genuinely fresh boot leaves them at 0 or whether something else seeds them.*
* **There is no “reset to defaults” in the option list.** The only defaults button is inside the
  Customize screen (edge bit `0x0100`), and it only resets the 12 button assignments
  (`0x004EE01C: for(i=0;i<12;i++) [0x1CFE740+i] = i+1;`) and clears flag bit `0x80`.
* **There is no confirmation prompt and no “apply” step** — every change is written to
  `0x01CFE738…` immediately, on the frame the direction is pressed.

---

## 4. The option list (state 3)

### 4.1 The row table `0x00B88970`

9 entries of 16 bytes, terminated by `0xFFFF`. Layout:

| off | type | meaning |
|---|---|---|
| `+0x00` | u16 | **label** text entry (label = string `2E`, help = string `2E+1`) |
| `+0x02` | u16 | text entry of the value shown when the flag bit is **clear** (toggles only) |
| `+0x04` | u16 | text entry of the value shown when the flag bit is **set** (toggles only) |
| `+0x06` | u16 | **widget type** (§4.2) |
| `+0x08` | u16 | **argument**: flag *bitmask* for toggles, or *index into `0x01CFE738`* for sliders |
| `+0x0A` | u16 | scratch: animated bar position 0..0x1000 (written by the creator and by the draw fn) |
| `+0x0C` | u32 | optional “value changed” callback, or NULL |

Decoded (this is the **on-screen order, top to bottom**, because the draw loop walks the table
from row 0 and steps y by 16 px):

| row | label (entry) | help text | widget | storage | values |
|---:|---|---|---|---|---|
| 0 | **Controller** (0x38) | `Change controller setting` | type 1 (toggle + Confirm) | flags bit `0x0020` | left = **Normal** (0x39, bit clear) / right = **Customize** (0x3A, bit set) |
| 1 | **Cursor** (0x11) | `Set cursor` | type 0 (toggle) | flags bit `0x0004` | left = **Initial** (0x13) / right = **Memory** (0x12) |
| 2 | **ATB** (0x0E) | `Set ATB` | type 0 (toggle) | flags bit `0x0001` | left = **Active** (0x0F) / right = **Wait** (0x10) |
| 3 | **Scan** (0x3D) | `Set close-up for Scan` | type 0 (toggle) | flags bit `0x0100` | left = **Once** (0x3F) / right = **Always** (0x3E) |
| 4 | **Camera Movement** (0x0B) | `Set battle camera movement` | type 5 (5-step bar, `0%`/`100%` ends) | byte `0x01CFE73E` | 0..4, **0 = full bar = most movement** |
| 5 | **Battle speed** (0x06) | `Set battle speed` | type 3 (5-step bar, icon ends) | byte `0x01CFE738` | 0..4, **0 = full bar = fastest** |
| 6 | **Battle message** (0x07) | `Set battle message speed` | type 3 | byte `0x01CFE739` | 0..4, **0 = fastest** |
| 7 | **Field message** (0x08) | `Set field message speed` | type 3 | byte `0x01CFE73A` | 0..4, **0 = fastest** |
| 8 | **Sound** (0x35) | `Set sound` | type 0x21 (0–100 bar) | byte `0x01CFE73B` + live `0x01CD1794` | 0..100 |

There are **no other rows** — see §8 for the vibration/analog rows that exist in the strings and
in the code but are absent from this build's table.

### 4.2 How a row is operated

Cursor movement (`0x004EE099`):

```
[0xB8600F] = 1                                   ; disarms the rebind poller (see §6)
row = 0x004C0A30(repeatWord, [0x01D8D440] /*=9*/, row)   ; UP/DOWN, wraps 8<->0
[esi+0x28] = row
0x004BD850(1, 0, 0x30, row*0x10 + 0x24)          ; hand cursor
[esi+0x20] = getText(1, 2, table[row].label, 1)  ; help line for the focused row
type = table[row].type
jmp handlerTable[ typeMap[type] ]                ; typeMap = 0x004EE72C, handlers = 0x004EE710
```

`typeMap` (byte per type 0..0x21) → handler:

| type | handler | used by | behaviour |
|---|---|---|---|
| 0 | `0x004EE195` | rows 1,2,3 | **Left clears the mask bit, Right sets it** (only when it actually changes); sound 2; calls `table[row].fn` if non-NULL |
| 1 | `0x004EE156` | row 0 | same as type 0, **plus**: edge `0x0840` (Confirm) while the bit is *set* (= Customize) → sound 2, `state = 4` → opens the Customize screen. If the bit is clear, Confirm does nothing. |
| 2 | `0x004EE118` | *(no row)* | “needs a DualShock”: if `[esi+0x2D]==0`, forces the help line to entry `0x44` and beeps (sound 5) on Left/Right |
| 3, 5 | `0x004EE309` | rows 4..7 | **5-step bar. Right → value−− (min 0), Left → value++ (max 4)**; sound 1 per step; writes `[0x01CFE738+arg]` immediately |
| 4 | `0x004EE231` | *(no row)* | “needs an analog pad”: if `[esi+0x2E]==0`, help line = entry `0x43`, beep on Left/Right |
| 0x21 | `0x004EE265` | row 8 | **volume. Right → +1 (max 100), Left → −1 (min 0)**; sound 1 per step; writes the byte and, because `label == 0x35`, also `0x46A390(v)` (apply) after first re-reading the live value with `0x46A470()` |
| 6..0x20 | `0x004EE35F` | — | nothing (common tail) |

Common tail `0x004EE35F`: `edge & 0x10` (**Cancel**) → sound 3, `state = 0xC` → fade out → menu
closes. **Confirm is ignored on every row except row 0.**

So: **every setting is a left/right change on the row itself.** The only row where Confirm does
anything is *Controller*, and only when it is already set to *Customize*; that opens the sub-screen
described in §6. There is no pop-up value list anywhere.

### 4.3 How the current value is displayed (and how to read it)

Draw fn `0x004EE750` → row loop `0x004EEB70`; per-type draw dispatch via byte map `0x004EEFF0`
and jump table `0x004EEFDC`.

**Toggle rows (types 0/1)** — `0x004EEBE4`:

```asm
mov bx, [row+0x08]              ; mask
and bx, [0x01CFE73C]
neg bx / sbb ebx,ebx / and ebx,0xFFFFFFFA / add ebx,7   ; ebx = 1 if bit SET, 7 if CLEAR
```
`ebx` is the palette for the **left** value label (`row+0x02`, x = 0xCD); the **right** label
(`row+0x04`, x = 0x11B) gets the complement. Palette **7 = bright/selected, 1 = dimmed**
(same convention as the card album). So:

> **the active value is whichever label is drawn in palette 7 — i.e. `row+0x02` when the mask bit
> is clear, `row+0x04` when it is set.** A screen reader should just test the bit.

Cross-check: ATB's mask is `0x0001` with `+0x02 = "Active"`, and `0x004A9450` pauses the ATB clock
only when bit `0x0001` is **set** — i.e. bit set = Wait. Consistent. Same for Cursor
(`0x004BB9B0` remembers the cursor when bit `0x0004` is set = “Memory”) and Controller
(`0x004A2D60` remaps only when bit `0x0020` is set = “Customize”).

**Bar rows (types 3/5/0x21)** — `0x004EEC89`:

* target = `(4 - value) * 0x1000 / 4` for the 5-step rows, or `value * 0x1000 / 100` for type 0x21;
* `row+0x0A` is animated toward the target by ±0xAA per frame (purely cosmetic);
* trough icon `0x44` at x = 0xD1; the filled bar is drawn at **x = 0xD9, full width 96 px**,
  fill = `pos * 96 / 0x1000` (minimum 1 px);
* end markers: for types **5 and 0x21** they are the strings **`0%`** (entry 0x48, x = 0xB9) and
  **`100%`** (entry 0x49, x = 0x143); for type **3** they are sprites **icon 0x140** (left) and
  **icon 0x141** (right) via `0x004B77C0`. *That is the only difference between types 3 and 5.*
  *UNVERIFIED: which glyph each icon actually is; from the timing code the right-hand end is the
  “fast” end, and the unused strings `Fast` (entry 0x3B) / `Slow` (entry 0x3C) are almost certainly
  their text equivalents.*
* when the row is disabled (`[esp+0x2c] == 0`, only possible for the dead types 2/4) the bar is
  drawn at half brightness.

**Note the creator/draw inconsistency:** the creator `0x004EDD9E` seeds `row+0x0A` for a type-0x21
row with `(100 - v) * 0x1000 / 100` while the draw fn animates toward `v * 0x1000 / 100`. The bar
therefore starts at the mirrored position and slides to the right one on entry — cosmetic only,
but do not use `row+0x0A` as the value; read the byte.

### 4.4 Screen furniture (for spatial descriptions)

| element | position |
|---|---|
| help/description window | (0x18, 0x06), 0xF4 × 0x16 (`0x004EE8B1`) |
| help text (`module+0x20`) | (0x22, 0x0D), palette 7 |
| row label | x = **0x30**, y = **0x24 + 16·row** |
| toggle value, “bit clear” label | x = **0xCD** |
| toggle value, “bit set” label | x = **0x11B** |
| bar left end marker | x = 0xB9 |
| bar | x = 0xD9, width 96 |
| bar right end marker | x = 0x143 |
| hand cursor | `0x004BD850(1, 0, 0x30, 16·row + 0x24)` |

Row pitch is **16 px** (`0x004EEF69: add ebx, 0x10`), all 9 rows are visible at once, there is no
scrolling.

---

## 5. The rows in detail

### 5.1 ATB — `0x01CFE73C` bit `0x0001`

* `0` = **Active** (default), `1` = **Wait**.
* Sole consumer `0x004A9450`, called once from the battle main loop at `0x004842D1`:

```asm
004A9450  test byte [0x1CFE73C], 1
004A9457  je   ret1                     ; Active -> always "let the clock run"
          ; Wait mode: pause only in the right sub-state
004A9460  cl = battleState[+0x3BD]      ; command/target selection phase
004A9469  cmp cl, 4 / je ret1
004A947E  test cl,cl / jl ret1          ; negative -> run
004A9489  cl = battleState[+0x3AE]
004A9492  test cl, 0x10 / je ret1       ; the "a menu is open" flag
004A9497  call 0x4AD3D0 / test eax,eax / je ret1
004A94A0  return 0                      ; -> FREEZE the ATB clock
```

So **Wait does not stop time in general** — it freezes the ATB gauges (and everything the battle
clock drives) only while a character's command/target menu is open. Active leaves the clock
running the whole time.

**Relationship to the mod's “Enhanced Wait Mode”:** the stock setting is a single bit with exactly
one effect — return 0 from `0x004A9450` while the command menu is up. Anything the mod wants
beyond that (pausing during message boxes, during target-less prompts, during animations,
auto-pausing when a turn becomes available) is *not* what this bit does, so the two must be
described to the player as different features. If the mod implements its own pause it should
either (a) leave this bit alone and hook `0x004A9450`'s callers, or (b) force the bit set and
extend `0x004A9450` — but note that flipping the bit is player-visible in this menu (row 2 will
read “Wait”) and that the bit is **saved into the save file**, so writing it silently changes the
player's save.

### 5.2 Battle speed — `0x01CFE738`, 0..4

Consumers, all of the form “base × (value + 1)”:

| addr | code | meaning |
|---|---|---|
| `0x004832F0` | `cx = [0x1CFE738]; dx = byte[statusIdx + 0x1CF8B14]; ecx++; imul edx, ecx` | status-effect tick durations |
| `0x00484490` | `ecx = [0x1CFE738] & 0xFF; ecx++; edx = ecx*125 << 5` → `[entity+0x10] = (v+1)*4000` | a battle countdown threshold |
| `0x0048D8FC` | `edx = [0x1CFE738] & 0xFF; cx = u16[0x1CFE0D8 + i*2]; edx++; imul ecx, edx;` then a magic-number divide | the ATB charge step |

Every one of them **multiplies a duration/threshold by `value + 1`**, so:

| stored byte | bar | speed |
|---:|---|---|
| 0 | full (96 px) | **fastest** (×1) |
| 1 | 3/4 | ×2 |
| 2 | 1/2 | ×3 |
| 3 | 1/4 | ×4 |
| 4 | empty | **slowest** (×5) |

Pressing **Right decreases the byte** (`0x004EE314`), i.e. Right = faster = longer bar.

### 5.3 Battle message — `0x01CFE739`, 0..4

`0x00485CF8`: `dx = [0x1CFE739]; ecx = v*8 + 8` → passed to the message pacer `0x0047E220`.
Delay = `8·(v+1)` frames-ish → **0 = fastest, 4 = slowest**; Right = faster.

### 5.4 Field message — `0x01CFE73A`, 0..4

`0x0052B7CB`: `al = [0x1CFE73A]; cx = u16[0x00B8EE94 + v*2]` → `0x0049FBC0(char, rate)` per
on-screen character. The table at `0x00B8EE94` is
**`0x1C00, 0x1800, 0x1400, 0x1000, 0x0C00`** — monotonically decreasing, i.e. a *rate*:
**0 = fastest, 4 = slowest**. Also read at `0x0052CAB7` and `0x0052D1F5`.

### 5.5 Camera Movement — `0x01CFE73E`, 0..4

Single consumer `0x005008CE`:

```asm
005008CE  mov cl, [0x1CFE73E]
005008D9  mov al, 4
005008E1  sub al, cl                 ; 4 - value
005008F5  mov byte [0x1D98424], al   ; battle-camera "amount"
```

So the byte is inverted into a 0..4 “how much the battle camera moves” level:
**stored 0 → 4 = maximum camera movement (full bar / `100%`), stored 4 → 0 = camera does not move
(empty bar / `0%`)**. This row is the one that is drawn with `0%`/`100%` end labels even though it
only has 5 steps.

### 5.6 Sound — `0x01CFE73B`, 0..100

This is a **master volume**, not mono/stereo:

* `0x0046A390 setMasterVolume(v)` — rejects `v > 100` (`cmp eax,0x64 / jbe`), stores to
  **`0x01CD1794`** and re-applies it to the mixer channels.
* `0x0046A470 getMasterVolume()` — returns the dword at `0x01CD1794`.
* Update handler `0x004EE265` **re-syncs the config byte from the engine every frame while the
  cursor is on this row** (`0x004EE284: call 0x46A470` → `[0x1CFE73B] = al`), then applies
  Left/Right, then calls `0x46A390(v)`. So the authoritative value is `0x01CD1794`; the config
  byte is the copy that gets saved.
* It is a **discrete 101-step control** (±1 per input frame, auto-repeat), rendered as a
  continuous 96-px bar with `0%` / `100%` labels at the ends. There is **no numeric readout on
  screen** — a blind player gets nothing but bar length, so the mod should speak the number.
* `0x004C3010` (called on load, and whenever the Controller row is toggled via its callback
  `0x004EDD20 = jmp 0x4C3010`) pushes `([0x1CFE73B] + 5) * 12` into `0x0049EF30(0, x)` — a second,
  coarser volume path. *UNVERIFIED: the unit of that argument.*
* **Mono/Stereo has no row in this build.** Flag bit `0x0002` still drives
  `0x004C2FD0 → 0x0046C040((flags>>1)&1)`, and the strings `Mono` (entry 0x36) / `Stereo`
  (entry 0x37) still exist, but nothing in the Config menu can change the bit, and `0x004C2FD0`
  is only called from the load path (`0x004E2EAB`, `0x004E44C9`, `0x004E4F4F`).

---

## 6. The Customize screen (state 7)

Entered from row 0 when it reads **Customize** and Confirm is pressed. `state 4 → 5 (slide) → 6 →
7`; leaving is `edge & 0x0800` → `state 10 → 11 (slide back) → 3`.

* **Title**: `getText(1, 2, 0x32 + page, 0)` → `Field Map Controls` / `Battle Controls` /
  `World Map Controls`, centred in a 0x11C-wide window; page = `module+0x29`.
* **Left/Right change the page** (`0x004EE49F`: edge `0x8000` → page−1 wrapping to 2,
  edge `0x2000` → page+1 wrapping to 0), sound 1.
* **Up/Down** move `module+0x2A` over the row table `0x00B88A10` via
  `0x004C0A30(repeat, [esi+0x2B], row)`.
* **Footer**: entry `0x40` = `{05}. to end, {05}( to default`, centred.
* `edge & 0x0100` (**default**) → sound 2, `0x00499680()`, then
  `for (i=0;i<12;i++) [0x01CFE740+i] = i+1;` and `flags &= ~0x0080`, then `0x004C3010`.
* `edge & 0x0800` (**end**) → sound 2, back to the list.
* **Cancel (`0x10`) does nothing here.** State 7 never reaches the shared cancel tail
  `0x004EE35F`: an exhaustive scan of its code path (`0x004EDF13`–`0x004EE055` and
  `0x004EE49F`–`0x004EE4FC`) finds tests for edge bits `0x0100`, `0x0800`, `0x8000` and `0x2000`
  only. **The player can only leave the Customize screen with the `0x0800` button** — worth
  announcing explicitly, since a blind player who presses Cancel will appear to be stuck.

### 6.1 Row table `0x00B88A10` (stride 8, 10 rows + `0xFF` terminator)

`{u8 buttonBit, u8 group, u8 slot, u8 y, u32 helpEntry}`

| row | buttonBit | group | slot | y | help entry |
|---:|---:|---:|---:|---|---|
| 0 | 6 | 0 | 0 | 0x04 | 0x41 `Button assignment` |
| 1 | 4 | 0 | 1 | 0x11 | 0x41 |
| 2 | 7 | 0 | 2 | 0x1E | 0x41 |
| 3 | 5 | 0 | 3 | 0x2B | 0x41 |
| 4 | 2 | 0 | 4 | 0x38 | 0x41 |
| 5 | 3 | 0 | 5 | 0x45 | 0x41 |
| 6 | 0 | 0 | 6 | 0x52 | 0x41 |
| 7 | 1 | 0 | 7 | 0x5F | 0x41 |
| 8 | 9 | 1 | 0 | 0x74 | 0x42 `{05}4 to switch L/R ANALOG controller` |
| 9 | 10 | 1 | 1 | 0x84 | 0x42 |

**Rows with `group == 1` (the two analog-stick rows) are hidden when no gamepad is present** —
both the creator (`0x004EDE62`) and the update fn (`0x004EDF26`) count only rows whose group byte
is 0 in that case, and store the count in `module+0x2B`. The draw fn additionally greys such a row
(`0x004EE9BC: [esp+0x28] = 0`) when `0x49F0A0` reports no pad.

The `buttonBit` field is the **bit index in the input word**, confirmed against the known bits:
row 0 is bit 6 = `0x0040` = Confirm and its Field-page label is `Talk/Confirm`; row 1 is bit 4 =
`0x0010` = Cancel and its label is `Walk/Cancel`. The rendered button glyph is
`0x004B7210(..., 0x004A2DF0(buttonBit) + 0x80, ...)`.

**Row label** = `getText(1, 2, 0x14 + page*10 + slot, 0)`:

| slot | page 0 “Field Map Controls” | page 1 “Battle Controls” | page 2 “World Map Controls” |
|---:|---|---|---|
| 0 | Talk/Confirm | Confirm | On,Off/Examine |
| 1 | Walk/Cancel | Cancel | Move BACK |
| 2 | Talk | View Status | Move FWD |
| 3 | Menu | Change Character | Menu |
| 4 | N/A | Change Select Window | Look left |
| 5 | N/A | Trigger | Look right |
| 6 | N/A | Escape (with `{05}0`) | N/A |
| 7 | N/A | Escape (with `{05} `) | Switch POV |
| 8 | Walk | Move cursor | Walk/Direction(Vehicle) |
| 9 | N/A | N/A | Vehicle FWD/BACK |

### 6.2 The assignment array `0x01CFE740[12]` and how remapping works

`0x004A2D60(u16 raw)` — called from the input builder at `0x004BE39B`, so it filters **every**
button read in the game:

```
if ((flags & 0x0020) == 0) return raw;          ; "Normal" -> identity
out = 0
for (i = 0; i < 12; i++)
    if (raw & (1 << i)) {
        m = [0x01CFE740 + i];
        if (m == 0) break;
        out |= 1 << (m - 1);
    }
return out | (raw & 0xF000);                    ; the direction nibble is never remapped
```

So `map[i]` = the **1-based logical button** produced by physical bit *i*; default `map[i] = i+1`
(identity). `0x004A2DF0(action)` is the inverse lookup used to pick the glyph.

**How a binding is actually changed:** state 7 calls `0x004989D0(row)` each frame
(`0x004EDFEE`) and plays sound 2 when it returns non-zero. `0x004989D0` polls the raw input
device (`0x00468E00` / `0x00468F30` / `0x00469370`), compares the device/button name against
`0xB86A68` and `0xB86A70`, and records the pick in `0x00B86410` / `0x00B86414`. It is armed only
when `0x00B8600F == 0` — state 3 sets that byte to 1 every frame (`0x004EE0AA`) and `0x004989D0`
clears it on its first call, i.e. **the poller is disarmed while you are on the option list and
armed while you are on the Customize screen.** This is the Steam port's “press the key/button you
want” rebinder; the PSX-style “swap two entries” logic still exists at `0x004EE4FD` (state 8) but
is unreachable. *UNVERIFIED: the exact structure written by `0x00498700` at the end of
`0x004989D0`.*

---

## 7. Text — mngrp section 1 / bank 2

`0x004BD630(1, 2, entry, sub)` returns `bank + u16[bank + (entry*2 + sub)*2 + 2]`; each *entry*
owns two consecutive strings (0 = label, 1 = description). Bank base in `mngrp.bin` is
**file offset `0x824`**, **148 strings = 74 entries**. `ç` is this bank's filler string.

| entry | str idx (label/desc) | label (sub 0) | description (sub 1) |
|---:|---|---|---|
| 0 (0x00) | 0 / 1 | `Vibration function` | `Set vibration function` |
| 1 (0x01) | 2 / 3 | `ON` | `ç` |
| 2 (0x02) | 4 / 5 | `OFF` | `ç` |
| 3 (0x03) | 6 / 7 | `Cannot use vibration function` | `ç` |
| 4 (0x04) | 8 / 9 | `Vibration function ON` | `ç` |
| 5 (0x05) | 10 / 11 | `Vibration function OFF` | `ç` |
| 6 (0x06) | 12 / 13 | `Battle speed` | `Set battle speed` |
| 7 (0x07) | 14 / 15 | `Battle message` | `Set battle message speed` |
| 8 (0x08) | 16 / 17 | `Field message` | `Set field message speed` |
| 9 (0x09) | 18 / 19 | `ANALOG input L/R invert` | `ç` |
| 10 (0x0A) | 20 / 21 | `ANALOG input` | `Set ANALOG stick response` |
| 11 (0x0B) | 22 / 23 | `Camera Movement` | `Set battle camera movement` |
| 12 (0x0C) | 24 / 25 | `Move` | `ç` |
| 13 (0x0D) | 26 / 27 | `Cannot move` | `ç` |
| 14 (0x0E) | 28 / 29 | `ATB` | `Set ATB` |
| 15 (0x0F) | 30 / 31 | `Active` | `ç` |
| 16 (0x10) | 32 / 33 | `Wait` | `ç` |
| 17 (0x11) | 34 / 35 | `Cursor` | `Set cursor` |
| 18 (0x12) | 36 / 37 | `Memory` | `ç` |
| 19 (0x13) | 38 / 39 | `Initial` | `ç` |
| 20 (0x14) | 40 / 41 | `Talk/Confirm` | `ç` |
| 21 (0x15) | 42 / 43 | `Walk/Cancel` | `ç` |
| 22 (0x16) | 44 / 45 | `Talk` | `ç` |
| 23 (0x17) | 46 / 47 | `Menu` | `ç` |
| 24 (0x18) | 48 / 49 | `N/A` | `ç` |
| 25 (0x19) | 50 / 51 | `N/A` | `ç` |
| 26 (0x1A) | 52 / 53 | `N/A` | `ç` |
| 27 (0x1B) | 54 / 55 | `N/A` | `ç` |
| 28 (0x1C) | 56 / 57 | `Walk` | `ç` |
| 29 (0x1D) | 58 / 59 | `N/A` | `ç` |
| 30 (0x1E) | 60 / 61 | `Confirm` | `ç` |
| 31 (0x1F) | 62 / 63 | `Cancel` | `ç` |
| 32 (0x20) | 64 / 65 | `View Status` | `ç` |
| 33 (0x21) | 66 / 67 | `Change Character` | `ç` |
| 34 (0x22) | 68 / 69 | `Change Select Window` | `ç` |
| 35 (0x23) | 70 / 71 | `Trigger` | `ç` |
| 36 (0x24) | 72 / 73 | `Escape (with {05}0)` | `ç` |
| 37 (0x25) | 74 / 75 | `Escape (with {05} )` | `ç` |
| 38 (0x26) | 76 / 77 | `Move cursor` | `ç` |
| 39 (0x27) | 78 / 79 | `N/A` | `ç` |
| 40 (0x28) | 80 / 81 | `On,Off/Examine` | `ç` |
| 41 (0x29) | 82 / 83 | `Move BACK` | `ç` |
| 42 (0x2A) | 84 / 85 | `Move FWD` | `ç` |
| 43 (0x2B) | 86 / 87 | `Menu` | `ç` |
| 44 (0x2C) | 88 / 89 | `Look left` | `ç` |
| 45 (0x2D) | 90 / 91 | `Look right` | `ç` |
| 46 (0x2E) | 92 / 93 | `N/A` | `ç` |
| 47 (0x2F) | 94 / 95 | `Switch POV` | `ç` |
| 48 (0x30) | 96 / 97 | `Walk/Direction(Vehicle)` | `ç` |
| 49 (0x31) | 98 / 99 | `Vehicle FWD/BACK` | `ç` |
| 50 (0x32) | 100 / 101 | `Field Map Controls` | `ç` |
| 51 (0x33) | 102 / 103 | `Battle Controls` | `ç` |
| 52 (0x34) | 104 / 105 | `World Map Controls` | `ç` |
| 53 (0x35) | 106 / 107 | `Sound` | `Set sound` |
| 54 (0x36) | 108 / 109 | `Mono` | `ç` |
| 55 (0x37) | 110 / 111 | `Stereo` | `ç` |
| 56 (0x38) | 112 / 113 | `Controller` | `Change controller setting` |
| 57 (0x39) | 114 / 115 | `Normal` | `ç` |
| 58 (0x3A) | 116 / 117 | `Customize` | `{05}. to customize controller setting` |
| 59 (0x3B) | 118 / 119 | `Fast` | `ç` |
| 60 (0x3C) | 120 / 121 | `Slow` | `ç` |
| 61 (0x3D) | 122 / 123 | `Scan` | `Set close-up for Scan` |
| 62 (0x3E) | 124 / 125 | `Always` | `ç` |
| 63 (0x3F) | 126 / 127 | `Once` | `ç` |
| 64 (0x40) | 128 / 129 | `{05}. to end, {05}( to default` | `ç` |
| 65 (0x41) | 130 / 131 | `Button assignment` | `ç` |
| 66 (0x42) | 132 / 133 | `{05}4 to switch L/R ANALOG controller` | `ç` |
| 67 (0x43) | 134 / 135 | `For ANALOG Controller` | `ç` |
| 68 (0x44) | 136 / 137 | `For DUAL SHOCK·` | `ç` |
| 69 (0x45) | 138 / 139 | `Left stick` | `ç` |
| 70 (0x46) | 140 / 141 | `Right stick` | `ç` |
| 71 (0x47) | 142 / 143 | `ç` | `ç` |
| 72 (0x48) | 144 / 145 | `0%` | `ç` |
| 73 (0x49) | 146 / 147 | `100%` | `ç` |

Entries actually referenced by the Config module: **0x06, 0x07, 0x08, 0x0B, 0x0E, 0x0F, 0x10,
0x11, 0x12, 0x13, 0x14–0x31 (customize labels), 0x32–0x34 (customize titles), 0x35, 0x38, 0x39,
0x3A, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x49**.

---

## 8. What is *not* in this build (and what a blind player will therefore never hear)

* **No “Vibration function” row.** Entries 0x00–0x05 (`Vibration function`, `ON`, `OFF`,
  `Cannot use vibration function`, `Vibration function ON/OFF`) are **unreferenced** by any code in
  the module — the row table has no entry with label 0x00 and no row of widget type 2. The
  machinery for a greyed-out, unusable row does exist (draw case `0x004EEBD1` dims both value
  labels when `module+0x2D == 0`; update handler `0x004EE118` replaces the help line with entry
  `0x44` `For DUAL SHOCK·` and answers Left/Right with the error sound 5), but **nothing reaches
  it in this build**. Vibration is instead slaved to the pad driver: `0x004C3060` copies the pad's
  rumble state into flag bit `0x40` every frame while the Config menu is open.
* **No “analog input” row** (type 4, help entry `0x43` `For ANALOG Controller`) — same story.
* **No mono/stereo row**, although the bit and the strings exist (§5.6).
* **No `ANALOG input L/R invert`, `Fast`/`Slow` labels, `Move`/`Cannot move` labels** —
  entries 0x09, 0x0C, 0x0D, 0x3B, 0x3C are unreferenced leftovers.

**Consequences for the mod:** if it wants to *add* rows (e.g. an “Enhanced Wait Mode” toggle), the
cleanest hook is to relocate/extend the 9-entry table at `0x00B88970` (the count is recomputed by
the creator into `0x01D8D440`, so a longer table just works, as long as the terminator is `0xFFFF`
and the extra rows use widget type 0 with a spare bit of `0x01CFE73C` — bits `0x0008`, `0x0200`
and above appear unused). Note that the flag word is **persisted in the save file**, so a mod bit
stored there will travel with saves.

---

## 9. Reading the screen (recipe for the mod)

```c
mod = scan pool 0x01D76BC8, stride 0x78, 10 slots, for (u8[+0x12] && u32[+0x08]==0x004EDE90);
u16 state = *(u16*)(mod+0x10);
if (state == 3 || state == 2) {                 // option list (2 = its first frame)
    int row  = *(s8*)(mod+0x28);                // 0..8
    const RowDesc *r = (RowDesc*)(0x00B88970 + row*0x10);
    // label:
    say( getText(1, 2, r->label, 0) );          // e.g. "ATB"
    // help (the top window already shows this, mod+0x20 is the same pointer):
    say( getText(1, 2, r->label, 1) );          // e.g. "Set ATB"
    // value:
    switch (r->type) {
      case 0: case 1: {
        int set = (*(u16*)0x01CFE73C) & r->arg;
        say( getText(1, 2, set ? r->valSet : r->valClear, 0) );   // "Wait" / "Active"
        break; }
      case 3: case 5: {
        int v = *(u8*)(0x01CFE738 + r->arg);     // 0..4
        say_level( 4 - v, 4 );                   // 4 = full bar; speak "4 of 4" or "fastest"
        break; }
      case 0x21: {
        int v = *(u8*)(0x01CFE738 + r->arg);     // 0..100  (== *(int*)0x01CD1794)
        say_percent(v);
        break; }
    }
    say("row %d of 9", row+1);
}
else if (state == 7) {                           // customize screen
    int page = *(s8*)(mod+0x29);                 // 0..2
    int crow = *(s8*)(mod+0x2A);                 // 0..(mod+0x2B)-1
    const CustRow *c = (CustRow*)(0x00B88A10 + crow*8);
    say( getText(1, 2, 0x32 + page, 0) );        // "Battle Controls"
    say( getText(1, 2, 0x14 + page*10 + c->slot, 0) );  // "Change Character"
    say_button( c->buttonBit );                  // physical bit; map via 0x01CFE740 if flags&0x20
}
```

Announcement rules:

* **Announce only in states 3 and 7** (§2.1). Everything else is fade/slide/teardown.
* Re-announce the whole row when `+0x28` changes; re-announce **just the value** when the flag
  word or the setting byte changes (Left/Right does not move the cursor).
* On entering Customize (state 3 → 4) and on leaving it (7 → 10), announce the screen change once
  the slide finishes, because the entire screen content changes.
* Left/Right auto-repeat comes from `0x01D76A9A`, so a held direction fires every frame on the
  sliders — throttle the speech (the game itself plays sound 1 per step).
* There is **no numeric or textual readout of any slider** on screen and **no highlight indicator
  other than the palette difference on the toggle rows**, so all of that has to be synthesised.
* There is **no confirmation prompt, no “are you sure”, no apply/discard** — Cancel simply closes,
  and every change already took effect.

---

## 10. Confidence / open items

**Verified from code:** dispatch index 8 (twice: main-menu table pair `09 08`, and the unique
overlay-1 dispatch entry); creator/update/draw; the 14-entry state table and which states are
steady; the 9-row option table and every field in it; the widget-type→handler maps for both update
and draw; the direction of every Left/Right change; the flag-bit meanings and their external
consumers; the 20-byte settings block and its save/load path; the 0..100 volume and its live
mirror at `0x01CD1794`; the battle/message/field speed and camera semantics from their consumers;
the Customize screen's tables, pages, labels and the global remap function.

**Marked UNVERIFIED:**

* Which sprite is which for the type-3 bar end icons `0x140` / `0x141` (only that the right-hand
  end is the “fast” end, from the timing code).
* The unit of the `(volume + 5) * 12` argument to `0x0049EF30` in `0x004C3010`.
* Whether a truly fresh boot leaves the four speed/amount bytes at 0, or something seeds them
  before the first save is written.
* The exact record written by `0x00498700` when `0x004989D0` accepts a new binding (the Steam
  rebinder's storage), and whether it feeds `0x01CFE740` or a separate keyboard table.
* Sound-effect id names (1 = tick, 2 = change/confirm, 3 = cancel, 5 = error) — inferred from
  consistent usage across menus, not from the audio code.
* The magic-number divisor in `0x0048D919` (the ATB charge scaling); only the `×(speed+1)`
  proportionality is proven.
