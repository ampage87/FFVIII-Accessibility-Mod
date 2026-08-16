# FF8 (Steam 2013, FF8_EN.exe) — Magic submenu reverse-engineering

Investigation date: 2026-08-16. Target: `/root/work/FF8_EN.exe` (22,124,216 bytes).
All addresses are **virtual addresses** in the default image (ImageBase 0x00400000).
`.text`, `.rdata`, `.data` are flat-mapped: `file_offset = VA - 0x400000`
(`.rsrc`/`.dotemu`/`.bind` are not; parse the headers if you need them).

Every finding below is tagged **[PROVEN]** (read directly out of the machine code, decisive
instruction quoted) or **[INFERRED]** (reasoned, not directly proven — treat with suspicion).

---

## 0. The big structural discovery (read this first)

**There is no absolute reference anywhere in the exe to `pMenuStateA + 0x1E6`, `+0x1E8`,
`+0x22E`, `+0x272`, `+0x27F` …** I scanned the whole file for 4-byte little-endian encodings
of every address in `0x01D76000 – 0x01D77400`. Result: hits stop dead at `0x01D76BDA` and
resume at `0x01D77078`. The entire range the mod cares about is never addressed absolutely.

Reason: **that range is an array of menu-module objects, addressed through a base register.**

### The module pool  **[PROVEN]**

```
004BE540: mov     edx, 0x1d76bc8          ; <-- pool base
004BE545: xor     eax, eax
004BE547: mov     cl, byte ptr [edx + 0x12]   ; in-use flag
004BE54A: test    cl, cl
004BE54C: je      0x4be55a                    ; found a free slot
004BE54E: inc     eax
004BE54F: add     edx, 0x78                   ; <-- stride 0x78
004BE552: cmp     eax, 0xa                    ; <-- 10 slots
004BE555: jl      0x4be547
```

* Pool base **0x01D76BC8**, stride **0x78** (120 bytes), **10 slots**.
* Pool end = `0x01D76BC8 + 10*0x78` = **0x01D77078** — exactly the end of the reference gap. ✔
* `0x004BE540` is the allocator. Continuation:

```
004BE565: mov     eax, dword ptr [0x1d76b48]   ; list head
004BE56B: mov     dword ptr [edx], eax         ; new->next
004BE56D: mov     dword ptr [edx+4], 0x1d76b48 ; new->prev
004BE574: mov     dword ptr [eax+4], edx
004BE577: lea     edi, [edx + 0x20]
004BE57A: mov     ecx, 0x16
004BE581: rep stosd                            ; zero +0x20 .. +0x77
004BE58B: mov     dword ptr [edx+8], eax       ; update/state function
004BE58E: mov     dword ptr [edx+0xC], ecx     ; draw function
004BE591: mov     dword ptr [0x1d76b48], edx   ; head = new
004BE597: mov     word ptr [edx+0x10], 0       ; state = 0
```

### Common module header  **[PROVEN]**

| off | type | meaning |
|---|---|---|
| +0x00 | ptr | next (MRU-first doubly-linked list) |
| +0x04 | ptr | prev |
| +0x08 | ptr | **update/state-machine function** |
| +0x0C | ptr | draw function |
| +0x10 | u16 | **state / "focus"** — the value the mod calls `+0x22E` |
| +0x12 | u8 | in-use flag |
| +0x20..+0x77 | — | per-module scratch, zeroed on allocation |

**List head = `0x01D76B48` = pMenuStateA + 0x0AE.**

### Mapping the mod's existing offsets onto the pool  **[PROVEN]**

`slot_k = 0x01D76BC8 + k*0x78`.

| mod offset | absolute | = |
|---|---|---|
| +0x1DB roster | 0x01D76C75 | **slot 1** + 0x35 |
| +0x1E6 top cursor | 0x01D76C80 | **slot 1** + 0x40 |
| +0x1E8 subsystem | 0x01D76C82 | **slot 1** + 0x42 |
| +0x1E9 char-select | 0x01D76C83 | **slot 1** + 0x43 |
| +0x1F6 title cursor | 0x01D76C90 | **slot 1** + 0x50 |
| +0x22E focus | 0x01D76CC8 | **slot 2** + 0x10 ← the module state word |
| +0x230 "phase" | 0x01D76CCA | **slot 2** + 0x12 ← this is the allocator's *in-use flag*, **not a phase** |
| +0x272 item list cursor | 0x01D76D0C | **slot 2** + 0x54 |
| +0x276 item target cursor | 0x01D76D10 | **slot 2** + 0x58 |
| +0x27F action cursor | 0x01D76D19 | **slot 2** + 0x61 |
| +0x71E menu HP array | 0x01D771B8 | outside the pool (separate global at 0x01D771B0, stride 0x20) |

So: **slot 1 = the main-menu module, slot 2 = the currently-open submenu module.**
Conversions:
* main-menu module base = `0x01D76C40` = `pMenuStateA + 0x1A6`
* submenu module base   = `0x01D76CB8` = `pMenuStateA + 0x21E`
* → **`pMenuStateA_offset = 0x21E + module_field_offset`**

> **[INFERRED, high confidence]** that the Magic module lands in slot 2 like the Item module
> does. The allocator picks the lowest free slot; the mod has already proven Item lives there.
> **Do not rely on it.** See §7 for the robust way (walk the list at 0x01D76B48 and match
> `[mod+8] == 0x004F02F0`). Cost of getting this wrong: reading another screen's memory.

---

## 1. Identifying the Magic module  **[PROVEN]**

There is a submenu dispatch table at **0x00B87ED8**, entries of 8 bytes
`{ creator_fn, u32 id }`, indexed by the value the mod reads at `+0x1E8`:

```
004BDB30: mov     eax, dword ptr [esp + 4]        ; subsystem index
004BDB37: mov     esi, dword ptr [eax*8 + 0xb87edc]
004BDB3E: mov     edi, dword ptr [eax*8 + 0xb87ed8]  ; creator fn
...
004BDB61: call    edi
```

Table contents (index = the `+0x1E8` value):

| idx | creator | state fn | screen |
|---|---|---|---|
| 0 | 0x4FDB20 | 0x4FDB60 | — |
| 1 | 0x4C0B30 | 0x4C0CF0 | — |
| 2 | 0x4F8010 | **0x4F81F0** | **Item** |
| **3** | **0x4F00D0** | **0x4F02F0** | **MAGIC** |
| 4 | 0x4D4840 | 0x4D4D30 | — |
| 5 | 0x4CDFA0 | 0x4CE080 | Status |
| 6 | 0x4E6740 | 0x4E3090 | Save |
| 7 | 0x4EF020 | 0x4EF6F0 | — |
| 8 | 0x4EDD30 | 0x4EDE90 | — |
| 9,10 | 0x4CB850 | 0x4CBA50 | Switch (party) |
| 11 | 0x4EDA40 | 0x4EBE40 | — |
| 12 | 0x4EA4D0 | 0x4EA890 | — |
| 13 | 0x4E8A30 | 0x4E8B50 | — |
| 14 | 0x4E76D0 | 0x4E77A0 | — |
| 15 | 0x4E68E0 | 0x4E6990 | — |
| 16 | 0x4E67C0 | 0x4E3090 | — |
| 17 | 0x4E2DC0 | — | Junction |
| 18 | 0x4DA1F0 | 0x4DA9B0 | main menu |

Indices 3/5/6/10/17 line up exactly with the mod's observed `+0x1E8` values for
Magic/Status/Save/Switch/Junction. **This independently confirms `+0x1E8 == 3` ⇒ Magic.**

So the Magic screen is:
* **creator** `0x004F00D0`
* **state machine** `0x004F02F0` (114 states, jump table at **0x004F5C4C**)
* **draw** `0x004F67C0`

(For comparison the Item screen is creator 0x4F8010 / state 0x4F81F0 / jump table 0x4FBF5C —
also 114 states, but **the state numbering is completely different**, so Item's known
focus values 3/5/14/30/36/79/97/99 do *not* transfer.)

---

## 2. Magic module field map

Module = the slot-2 object. `pMenuStateA offset = 0x21E + module offset` (given `[INFERRED]`
slot 2; see §7).

| mod off | pMenuStateA | abs (slot 2) | type | meaning | tag |
|---|---|---|---|---|---|
| +0x10 | **+0x22E** | 0x01D76CC8 | u16 | **state** (see §3) | PROVEN |
| +0x16 | +0x234 | 0x01D76CCE | u16 | return-state after char-change slide | PROVEN |
| +0x1A | +0x238 | 0x01D76CD2 | u16 | return-state after page slide | PROVEN |
| +0x20 | +0x23E | 0x01D76CD8 | ptr | creator argument (parent param block) | PROVEN |
| +0x24 | +0x242 | 0x01D76CDC | ptr | **current help/description string pointer** | PROVEN |
| +0x28 | +0x246 | 0x01D76CE0 | ptr | previous help string (cross-fade) | PROVEN |
| +0x2C | +0x24A | 0x01D76CE4 | ptr | popup entry table (sort menu → 0xB88A9C) | PROVEN |
| +0x30 | +0x24E | 0x01D76CE8 | u16 | popup open/close animation | PROVEN |
| +0x32 | **+0x250** | 0x01D76CEA | u8 | **discard: character id snapshot** | PROVEN |
| +0x33 | **+0x251** | 0x01D76CEB | u8 | **discard: magic slot index snapshot (0..31)** | PROVEN |
| +0x34 | +0x252 | 0x01D76CEC | u16 | misc animation | PROVEN |
| +0x36 | **+0x254** | 0x01D76CEE | u16 | **selectable-character bitmask** (bit n = char n) | PROVEN |
| +0x38..0x3F | **+0x256..+0x25D** | 0x01D76CF0.. | u8[8] | **per-character spell cursor, absolute 0..31** | PROVEN |
| +0x40 | +0x25E | 0x01D76CF8 | s16 | slide animation counter | PROVEN |
| +0x42 | **+0x260** | 0x01D76CFA | u8 | **page / scroll of column A (0..7)** | PROVEN |
| +0x43 | +0x261 | 0x01D76CFB | u8 | previous page (during slide) | PROVEN |
| +0x46 | +0x264 | 0x01D76CFE | u8 | page of column B (2-character screens) | PROVEN |
| +0x4A | +0x268 | 0x01D76D02 | s16 | slide animation (2-char screen) | PROVEN |
| +0x50 | +0x26E | 0x01D76D08 | s16 | window X slide (0 = fully open) | PROVEN |
| +0x52 | +0x270 | 0x01D76D0A | s16 | slide animation | PROVEN |
| +0x54 | +0x272 | 0x01D76D0C | u8 | **NOT a live cursor** — snapshot of column A index, written only in state 75 | PROVEN |
| +0x55 | +0x273 | 0x01D76D0D | u8 | snapshot of column B index | PROVEN |
| +0x56 | **+0x274** | 0x01D76D0E | u8 | **screen mode** (see §3.1) | PROVEN |
| +0x57 | **+0x275** | 0x01D76D0F | u8 | **target-select cursor** (n-th set bit of +0x36) | PROVEN |
| +0x5E | +0x27C | 0x01D76D16 | u8 | render flag bits (0x20, 0x40 …) | PROVEN |
| +0x60 | **+0x27E** | 0x01D76D18 | u8 | **number of selectable targets** = popcount(+0x36) | PROVEN |
| +0x61 | **+0x27F** | 0x01D76D19 | u8 | **action-row cursor (0..3)** | PROVEN |
| +0x62 | **+0x280** | 0x01D76D1A | u8 | **second character id** (2-char flows) | PROVEN |
| +0x64 | **+0x282** | 0x01D76D1C | u8 | **savemap character id of this screen (0..7)** | PROVEN |
| +0x67 | **+0x285** | 0x01D76D1F | u8 | **action-row ENABLE BITMASK** (bit n = action n usable) | PROVEN |
| +0x69,+0x6A | +0x287,+0x288 | | u8 | char-change slide flags | PROVEN |
| +0x6C | +0x28A | 0x01D76D24 | u16 | return-state after yes/no dialog | PROVEN |
| +0x6E | **+0x28C** | 0x01D76D26 | u16 | **yes/no dialog open (1 = open)** | PROVEN |
| +0x70 | **+0x28E** | 0x01D76D28 | u8 | **yes/no dialog cursor** | PROVEN |
| +0x71 | **+0x28F** | 0x01D76D29 | u8 | **sort-order popup cursor (0..6)** | PROVEN |
| +0x72 | +0x290 | 0x01D76D2A | u8 | sort popup entry count (7) | INFERRED |

### ⚠ The trap
**The Item submenu's list cursor `+0x272` is NOT the Magic spell cursor.** In the Magic
module `+0x54` is only written in state 75 (post-sort redraw). Reading `+0x272` on the Magic
screen will give a stale or wrong slot. This is exactly the class of plausible-but-wrong
inference the project has already been burned by.

---

## 3. The spell-list screen (state 13) — the thing the mod is missing

Jump-table entry 13 → `0x004F03D4`.

### 3.1 Cursor + scroll  **[PROVEN]**

Cursor draw (row within the visible window):
```
004F03DC: mov     dl, byte ptr [ebp + 0x64]        ; charId
004F03EA: mov     al, byte ptr [edx + ebp + 0x38]  ; per-character cursor
004F0416: and     esi, 0x80000003                  ; row = cursor & 3
004F0423: lea     eax, [esi + esi*2]
004F0426: lea     ecx, [esi + eax*4 + 0x43]        ; Y = 13*row + 0x43
004F042A: push    ecx
004F042B: push    0xd4                             ; X = 0xD4 (single column)
004F0434: call    0x4bd850
```

Cursor update (every frame while in state 13):
```
004F0439: push    esi                              ; current row (0..3)
004F043E: push    4                                ; <-- 4 rows per page
004F0440: push    esi                              ; pad input
004F0441: call    0x4c0a30                         ; up/down with wrap, no scroll
004F0446: movsx   edx, byte ptr [ebp + 0x42]       ; page
004F044F: mov     cl, byte ptr [ebp + 0x64]        ; charId
004F0452: lea     eax, [eax + edx*4]               ; ABS INDEX = newRow + page*4
004F045B: mov     dword ptr [esp + 0x18], eax
004F045F: mov     byte ptr [ecx + ebp + 0x38], al  ; store back
```

Reading the highlighted spell:
```
004F0591: mov     al, byte ptr [ebp + 0x64]        ; charId
004F0597: mov     cl, byte ptr [eax + ebp + 0x38]
004F059B: and     ecx, 0x80000003                  ; row
004F05A8: movsx   edx, byte ptr [ebp + 0x42]       ; page
004F05AC: lea     ecx, [ecx + edx*4]               ; abs index
004F05AF: lea     edx, [eax + eax*8]
004F05B2: lea     eax, [eax + edx*2]               ; 19 * charId
004F05B7: lea     ecx, [ecx + eax*4]               ; abs + 76*charId
004F05BC: shl     ecx, 1                           ; 2*abs + 152*charId
004F05BE: mov     bl, byte ptr [ecx + 0x1cfe0f9]   ; QUANTITY
004F05C4: mov     dl, byte ptr [ecx + 0x1cfe0f8]   ; SPELL ID
```

**Conclusions:**
* The list is **1 column × 4 visible rows × 8 pages = 32 slots**. Up/Down wrap inside the
  4-row page (they never scroll). Left/Right change the page.
* **`spell_slot = (menu[+0x256 + charId] & 3) + menu[+0x260] * 4`**
  Always use this formula. The stored byte *is* resynced to the absolute index every frame,
  but it lags by one frame immediately after a page change (state 14/16 only touch `+0x260`).
* **There is no display remapping.** The index goes straight into
  `savemap.char[charId].Magics[idx]`. What you see is raw savemap slot order.
* No separate "top visible row" variable — the scroll is the page byte `+0x260`, and the
  visible rows are slots `page*4 .. page*4+3`.

### 3.2 The `+0x274` screen-mode byte  **[PROVEN]** (values), **[INFERRED]** (labels)

Written at exactly these sites:

| value | written at | context |
|---|---|---|
| 0 | 0x4F1EC7 (state 2), 0x4F4910 (state 66), 0x4F5681/0x4F56C1 | action row |
| 1 | 0x4F2965 (state 12) | entering the spell list under action 0 |
| 2 | 0x4F2D4B (state 18) | entering target select |
| 3 | 0x4F30D5 (state 23), 0x4F1C03/0x4F1C29, 0x4F57DB | action 1 / action 2 setup |
| 4 | 0x4F31AB (state 27), 0x4F56EB | two-character screen |
| 5 | 0x4F4B8F (state 75) | post-sort redraw |

### 3.3 Which states are which  **[PROVEN]** unless noted

| state (`+0x22E`) | address | meaning |
|---|---|---|
| 0 | 0x4F1DEE | init → 1 |
| 1 | 0x4F1E06 | build character mask → 2 |
| 2 | 0x4F1EBE | arrive at action row (`+0x274`=0) → 3 |
| **3** | 0x4F0336 | **ACTION ROW (steady)** |
| 4, 6 | 0x4F1FD2, 0x4F2247 | character change slide from the action row; returns to `[+0x234]` |
| 5, 7 | 0x4F2187, 0x4F23FC | character slide continuation |
| 8, 10 | 0x4F24BC, 0x4F270D | character change slide from the spell list |
| 9, 11 | 0x4F267F, 0x4F28CF | ditto continuation |
| 12 | 0x4F295C | enter spell list (`+0x274`=1) → 13 |
| **13** | 0x4F03D4 | **SPELL LIST (steady)** |
| 14, 15 | 0x4F29D8, 0x4F2AC5 | **page LEFT** (`+0x260`--, wraps 0→7) → back to `[+0x238]` |
| 16, 17 | 0x4F2B6C, 0x4F2C57 | **page RIGHT** (`+0x260`++, wraps 7→0) |
| 18, 19 | 0x4F2CE7, 0x4F2D78 | enter target select (`+0x274`=2), resyncs `+0x260 = cursor>>2` |
| **20** | 0x4F06DA | **TARGET SELECT (steady)** — cursor `+0x275`, count `+0x27E` |
| 21, 22 | 0x4F2E2D, 0x4F2EA7 | apply the spell to the chosen target |
| 23, 24 | 0x4F30C6, 0x4F3116 | **action 1** entry (picks a 2nd character, `+0x274`=3) |
| 25..32 | 0x4F0782 … 0x4F3600 | two-character screen (`+0x274`=4) |
| 33..39 | — | unused (fall through to the default handler 0x4F5C40) |
| 40..68 | — | two-character transfer machinery |
| **69** | 0x4F4998 | **open the SORT popup** (table 0xB88A9C, cursor `+0x28F` = 0) |
| 70, 71 | 0x4F49AF, 0x4F4A64 | popup open animation |
| **72** | 0x4F4A6A | **SORT MENU (steady)** — 7 entries |
| 73, 74 | 0x4F4B1C, 0x4F4B22 | popup close (cancel / sort failed) |
| 75, 76 | 0x4F4B8D, 0x4F4C01 | post-sort redraw (`+0x274`=5) |
| 93..97 | 0x4F1B4A … | **action 2** entry (picks a 2nd character) |
| **108** | 0x4F59F5 | **DISCARD confirmation dialog opens** |
| **109** | 0x4F1D54 | **DISCARD dialog polling; YES ⇒ slot wiped** |
| 110, 111 | 0x4F5AE7, 0x4F5B18 | secondary message box (text id 0x3A) |
| 112, 113 | 0x4F5C08, 0x4F5C0E | closing; 113 calls `0x4BE610` (free module) + `0x4BDAC0` (pop menu stack) |

---

## 4. The action row  **[PROVEN]**

State 3 reads the cursor and moves it horizontally across the *enabled* entries only:

```
004F0336: xor     ecx, ecx
004F0338: push    1
004F033A: mov     cl, byte ptr [ebp + 0x61]                 ; action cursor
004F033D: movsx   eax, word ptr [ecx*2 + 0xb88a90]          ; entry -> text index
004F0346: push    8                                          ; text group 8
004F034A: call    0x4bd630                                   ; get help string
004F034F: mov     dword ptr [ebp + 0x24], eax
004F0354: mov     dl, byte ptr [ebp + 0x61]                 ; cursor
004F0359: mov     al, byte ptr [ebp + 0x67]                 ; ENABLE MASK
004F0367: call    0x4c2e00                                   ; f(input, mask, cursor)
004F036C: mov     byte ptr [ebp + 0x61], al
```

Compare the Item screen, which passes a **constant** mask:
```
004F824B: movsx   edx, byte ptr [esi + 0x61]
004F825C: push    0xf                                        ; Item: all 4 always enabled
004F825F: call    0x4c2e00
```

### 4.1 There are exactly **4** actions, cursor 0..3

Entry table at **0x00B88A90**, `s16`, `0xFFFF`-terminated:
```
00B88A90: 0   1   11   2   -1
```
Those four numbers are **string indices inside menu text group 8** (Item uses group 9 with
`{0, 2, 21, 1}` at 0x00B88AB8). Text is fetched by
`0x004BD630(1, group, index, sub)` from the runtime-loaded `mngrp.bin`
(`base = *(u32*)0x00B86D30 + 0x2E000`), so **the literal English labels are not in the exe
and I could not read them.** See §4.4.

### 4.2 Confirm dispatch — what each action does  **[PROVEN]**

```
004F1F21: mov     al, byte ptr [ebp + 0x61]
004F1F24: cmp     eax, 3
004F1F27: ja      0x4f5c40
004F1F2D: jmp     dword ptr [eax*4 + 0x4f5e14]
```
Table at 0x004F5E14 = `{0x4F1F34, 0x4F1F45, 0x4F1F56, 0x4F1F67}`:

| action | goes to state | what it actually does |
|---|---|---|
| **0** | 12 → 13 | opens the character's own spell list in "use" mode (`+0x274`=1); confirming a spell there leads to target select → cast |
| **1** | 23 → 24 → 27… | picks a *second* character (`+0x280`), builds their stats into 0x1D8D448, opens a **two-column, two-character** screen (`+0x274`=4) |
| **2** | 93 → 94..97 | also picks a second character (`+0x280`); a second two-character flow |
| **3** | 69 → 72 | opens the **7-entry sort/arrange popup** |

Cancel on the action row (`test bl, 0x10`) → state 112 = close the submenu.

### 4.3 Enable mask `+0x285` (module +0x67)  **[PROVEN]**

Built in the creator `0x004F00D0`:
```
004F01CB: mov     byte ptr [esi + 0x67], 9    ; character CAN cast -> bits 0 and 3
004F01D1: mov     byte ptr [esi + 0x67], 8    ; cannot cast       -> bit 3 only
004F01D5: push    2
004F01D7: call    0x4c3050                    ; savemap[0xAE3] & 2  (menu lock)
004F01E3: mov     byte ptr [esi + 0x67], 8    ; locked -> bit 3 only
004F01F2: call    0x4abc20                    ; popcount(available chars)
004F01FA: cmp     eax, 2
004F01FF: or      byte ptr [esi + 0x67], 6    ; >=2 characters -> bits 1 and 2
```
"Can cast" = the character is available **and** is not Petrified (status bit 0x04) and not
Silenced (status bit 0x10):
```
004F0187: mov     eax, 0x1cfe17e              ; savemap + 0x522 = char[0] + 0x96 (status)
004F018C: mov     dx, word ptr [eax]
004F018F: test    dl, 4                        ; Petrify -> clear this char's bit
004F019F: test    dl, 0x10                     ; Silence -> clear this char's bit
004F01AF: add     eax, 0x98                    ; next character (stride 0x98)
004F01B5: cmp     eax, 0x1cfe63e               ; 8 characters
```

So the practical meaning of `+0x285`:
* bit 0 = action 0 (cast) — off when silenced/petrified/unavailable or menu-locked
* bit 1, bit 2 = actions 1 and 2 — **only present when ≥ 2 characters are available**
* bit 3 = action 3 (sort) — **always** on

The cursor never lands on a disabled entry (`0x4C2E00` skips to the next set bit), so the
mod can safely announce "action N of 4" but should suppress disabled entries if it enumerates.

### 4.4 Getting the real labels — recommended
The strings live in `mngrp.bin`, loaded at runtime. **The mod can call the game's own getter:**

```c
typedef const char* (__cdecl *GetMenuText_t)(int useMenuBank, int group, int index, int sub);
GetMenuText_t GetMenuText = (GetMenuText_t)0x004BD630;
// Magic action labels:
for (int i = 0; i < 4; i++) {
    static const short tbl[4] = {0, 1, 11, 2};
    const char* s = GetMenuText(1, 8, tbl[i], 0);   // FF8-encoded text, needs the mod's decoder
}
// Sort-order labels: GetMenuText(1, 8, 15 + n, 0), n = 0..6
// Currently-highlighted help text is already cached at pMenuStateA + 0x242 (module +0x24)
```
The returned buffer is FF8's own character encoding, not ASCII — the mod already has a
decoder for item/spell names.

> **[INFERRED]** from behaviour, actions are plausibly *Use / <cross-character transfer> /
> <cross-character transfer> / Sort*. **I deliberately do not assert the English names.**
> Do not hard-code "Use, Rearrange, Sort, Battle" from the Item menu — the mechanics differ.

---

## 5. Sub-flows

### 5.1 Cast (action 0) → target select  **[PROVEN]**

Confirm on the spell list (state 13):
```
004F05F6: test    byte ptr [esp + 0x10], 0x40      ; CONFIRM
004F0613: mov     cl, byte ptr [eax + 0x1cfe0f8]   ; spell id
004F061B: mov     cl, byte ptr [eax + 0x1cfe0f9]   ; qty; 0 -> buzzer
004F0625: mov     edx, dword ptr [0x1d2bb10]       ; mmagic.bin
004F062D: mov     al, byte ptr [edx + esi*4]       ; per-spell attribute byte
004F0630: and     eax, 1                           ; bit 0 = FIELD-USABLE
004F0664: mov     word ptr [ebp + 0x10], 0x12      ; -> state 18 (target select)
004F066E: push    5 ; call 0x4b92a0                 ; else buzzer sfx
```

Target select = **state 20**:
```
004F06DA: movsx   edx, byte ptr [ebp + 0x57]       ; target cursor
004F06E1: mov     al, byte ptr [ebp + 0x60]        ; number of targets
004F06E6: call    0x4c0a30                          ; up/down with wrap
004F06F4: mov     byte ptr [ebp + 0x57], al
...
004F0752: movsx   eax, byte ptr [ebp + 0x57]
004F0759: lea     edx, [eax + ecx*4 + 0x42]         ; Y = 13*n + 0x42
004F075E: push    0x26                              ; X = 0x26
004F0764: call    0x4bd850
```

**Cursor `+0x275`, count `+0x27E`.** Mapping cursor → character (state 22):
```
004F2EA7: movsx   ecx, byte ptr [ebp + 0x57]
004F2EAB: movsx   edx, word ptr [ebp + 0x36]       ; selectable-character bitmask
004F2EB1: call    0x4abc40                          ; index of the n-th set bit
```
`0x004ABC40(mask, n)` returns the index of the n-th set bit (or -1). `0x004ABC20(mask)` is
popcount. **This is exact** — much better than the mod's current "collect roster and sort"
heuristic in `GetPartyCharAtVisualPos`, which happens to give the same answer only because a
bitmask enumerated low-to-high *is* sorted by character index. Use the mask directly:

```c
uint16_t mask = *(uint16_t*)(pMenuStateA + 0x254);
uint8_t  n    = *(uint8_t*) (pMenuStateA + 0x275);
int charId = -1, seen = 0;
for (int b = 0; b < 16; b++) if (mask & (1u<<b)) { if (seen++ == n) { charId = b; break; } }
```

Cancel from target select returns to state 13 (`0x4F2E07: test byte ptr [esp+0x14], 0x10 → state 21`).

### 5.2 Sort / arrange (action 3)  **[PROVEN]**

State 69 opens a vertical popup:
```
004F4998: mov     dword ptr [ebp + 0x2c], 0xb88a9c   ; entry table
004F49A5: mov     byte ptr [ebp + 0x71], 0           ; popup cursor = 0
```
Table at **0x00B88A9C** (`s16`, 0xFFFF-terminated): `15 16 17 18 19 20 21 -1` ⇒ **7 sort
orders**, labels = text group 8 indices 15..21, i.e. **label(n) = GetMenuText(1, 8, 15+n, 0)**.

State 72 (steady):
```
004F4A6A: movsx   edx, byte ptr [ebp + 0x71]     ; cursor
004F4A6E: movsx   eax, byte ptr [ebp + 0x72]     ; count (7)
004F4A75: call    0x4c0a30
004F4A7A: mov     byte ptr [ebp + 0x71], al
004F4A82: add     eax, 0xf                        ; help text = group 8 index 15+cursor
004F4A8A: call    0x4bd630
...
004F4A95: test    bl, 0x40                        ; CONFIRM
004F4AA8: mov     dl, byte ptr [ebp + 0x64]       ; charId
004F4AAC: call    0x4f0030                        ; DO THE SORT
004F4AB4: neg     eax ; sbb eax,eax ; and al,0xfe ; add eax,0x4b
004F4ABD: mov     word ptr [ebp + 0x10], ax       ; 0x4B (75) on success, 0x49 (73) on failure
004F4AC3: test    bl, 0x10                        ; CANCEL -> state 73
```

The sorter `0x004F0030(charId, orderIndex)` **rewrites the savemap array in place**:
```
004F0030: mov     ecx, dword ptr [0x1d2bb5c]      ; magsort.bin
004F003E: shl     ebx, 6                           ; orderIndex * 0x40
004F0067: lea     ecx, [eax + eax*8]
004F006A: lea     eax, [eax + ecx*2]               ; 19*charId
004F006D: lea     esi, [eax*8 + 0x1cfe0f8]         ; &Magics[0] for charId
004F0062: mov     edi, 0x20                        ; 32 slots
004F007A: mov     dl, byte ptr [eax]               ; id
004F007C: mov     cl, byte ptr [eax + 1]           ; qty
004F0089: mov     byte ptr [esp + edx + 0xc], cl   ; qtyByIdd[id] = qty  (64-byte scratch)
004F0092: mov     ecx, 0x20 ; ... zero all 32 slots
004F00A8: mov     al, byte ptr [ebx] ; inc ebx     ; walk the 0x40-byte order list
004F00B7: mov     byte ptr [ecx], al               ; re-emit {id, qty} compacted
004F00BE: cmp     esi, 0x40
```
Consequences for the mod: **after a sort, every slot index changes** — the mod must re-read.
Sorting is not destructive of *quantities*, only of ordering, and empty slots are compacted
to the end. Spell ids are < 0x40 (the scratch table is 64 bytes indexed by id).

### 5.3 Discard — the destructive flow, **with a Yes/No confirmation**  **[PROVEN]**

Trigger: on the spell list (state 13), pressing the button whose bit is `0x80` of the low
input byte (Square on a PSX layout), on a slot with a non-zero id **and** non-zero qty:
```
004F067C: test    byte ptr [esp + 0x14], 0x80
004F0697: mov     cl, byte ptr [eax + 0x1cfe0f8]  ; id  == 0 -> buzzer (0x4F51C7)
004F06A5: mov     cl, byte ptr [eax + 0x1cfe0f9]  ; qty == 0 -> buzzer
004F06BE: mov     word ptr [ebp + 0x6c], 0xd      ; return state = 13
004F06CA: mov     byte ptr [ebp + 0x32], cl       ; charId snapshot
004F06CD: mov     byte ptr [ebp + 0x33], bl       ; SLOT INDEX snapshot
004F06D0: mov     eax, 0x6c                        ; -> state 108
```

State 108 builds the prompt and opens a modal dialog:
```
004F59F5: push    0xe ; call 0x4f6600              ; message template
004F5A12: mov     cl, byte ptr [edx*2 + 0x1cfe0f8] ; the spell id being discarded
004F5A1A: push    0x1d8da68                        ; formatted message buffer
004F5A20: call    0x4eff40
004F5A25: push    0x6b ; push 0x1d8da68 ; call 0x4c2b10   ; open window 0x6B
004F5A31: mov     byte ptr [ebp + 0x70], 1         ; yes/no cursor init = 1
004F5A35: mov     word ptr [ebp + 0x6e], 1         ; dialog ACTIVE
004F5A4B: mov     word ptr [ebp + 0x10], 0x6d      ; -> state 109
```

State 109, the **YES** branch (0x4F5A64) — this is where magic is destroyed:
```
004F5A66: mov     word ptr [ebp + 0x6e], 0
004F5A7F: mov     byte ptr [ecx*2 + 0x1cfe0f8], 0  ; Magics[slot].id  = 0
004F5A9A: mov     byte ptr [edx*2 + 0x1cfe0f9], 0  ; Magics[slot].qty = 0
004F5AA6: call    0x4c3120                          ; recompute junction/stats
004F5AB1: call    0x4be790
004F5ABC: call    0x4bfcf0
```
The **NO** branch (0x4F5A5C) just clears `+0x6E` and restores the state from `+0x6C`.

**For the mod:** gate on `+0x28C != 0` to know the confirm window is up, read `+0x28E` for the
Yes/No cursor, and read `+0x250` / `+0x251` for exactly which character and slot is at risk.
Announce the spell name from `savemap.char[+0x250].Magics[+0x251]`.

### 5.4 The two-character flows (actions 1 and 2)  **partly [PROVEN], partly [INFERRED]**

**[PROVEN]** for action 1 (state 23):
```
004F30CB: mov     dl, byte ptr [ecx + ebp + 0x38]  ; own cursor
004F30CF: shr     dl, 2
004F30D2: mov     byte ptr [ebp + 0x46], dl        ; column-B page
004F30D5: mov     byte ptr [ebp + 0x56], 3
004F30D9: call    0x4ad030                          ; available-character mask
004F30ED: shl     edx, cl ; not edx ; and eax, edx  ; remove self
004F30F5: call    0x4bfdf0                          ; choose the other character
004F30FA: mov     byte ptr [ebp + 0x62], al         ; SECOND character id
004F3102: push    0x1d8d448 ; call 0x4bfc90         ; build their computed stats
004F3110: mov     word ptr [ebp + 0x10], 0x18
```
State 27 then sets `+0x274 = 4` and drives two lists side by side, with column A paged by
`+0x260` (character `+0x282`) and column B paged by `+0x264` (character `+0x280`); each
column's cursor still comes from the same per-character array `+0x256 + charId`.
Both flows converge on helper routines `0x4F5E30 / 0x4F6030 / 0x4F6140 / 0x4F6300` which
swap/transfer `{id, qty}` pairs between two savemap slots, e.g.:
```
004F1291: mov     cl, byte ptr [esi*2 + 0x1cfe0f9]
004F12A7: mov     al, byte ptr [edx*2 + 0x1cfe0f9]
004F12DA: mov     byte ptr [esi*2 + 0x1cfe0f9], cl
004F12F7: mov     byte ptr [edx*2 + 0x1cfe0f9], al
```

**[INFERRED]** — these two actions are cross-character magic transfer/exchange (that is what
"needs ≥ 2 available characters" plus "picks a second character id" plus "swaps quantities
between two slots" means). I did **not** finish separating action 1 from action 2 (which is
give-vs-swap, or single-slot-vs-whole-list). **I could not determine this from a static read
in the time available, and I am not going to guess.**

**Cheapest experiment to settle it:** on the Magic screen, log
`+0x22E (state)`, `+0x274 (mode)`, `+0x280 (2nd char)`, `+0x260`, `+0x264`,
`+0x256..+0x25D` at 100 ms, then enter action 1 and action 2 in turn and note which savemap
slots change. Two runs, ~2 minutes.

### 5.5 There is **no** "Battle" action and no item-style two-step rearrange within one
character. Rearranging within a character is done by **sorting** (action 3), which is a single
step, not a source→destination pick.

---

## 6. Magic stock data — the mod's assumption is CORRECT  **[PROVEN]**

The mod uses `SAVEMAP_BASE + 0x048C + charId*152`, `Magics[32]` of `{u8 id, u8 qty}` at
`struct + 0x10`. With `SAVEMAP_BASE = 0x1CFDC5C` that puts `Magics[0]` at **0x01CFE0F8**.

The sorter computes exactly that:
```
004F005E: mov     eax, dword ptr [esp + 0x50]     ; charId
004F0067: lea     ecx, [eax + eax*8]              ; 9*charId
004F006A: lea     eax, [eax + ecx*2]              ; 19*charId
004F006D: lea     esi, [eax*8 + 0x1cfe0f8]        ; 152*charId + 0x1CFE0F8
004F0062: mov     edi, 0x20                       ; 32 entries
004F007A: mov     dl, byte ptr [eax]              ; +0 = spell id
004F007C: mov     cl, byte ptr [eax + 1]          ; +1 = quantity
004F007F: inc     eax ; inc eax                   ; stride 2
```

* `char` stride = **0x98 (152)** ✔
* `Magics` base = **savemap + 0x49C** = `0x48C + 0x10` ✔
* **32** entries × 2 bytes ✔
* character id range **0..7** — proven by the status-scan bound
  `0x4F04B0: mov eax, 0x1cfe17e` … `0x4F04DE: cmp eax, 0x1cfe63e` (0x4C0 / 0x98 = 8)
* `savemap + 0x522` (`char[0] + 0x96`) is the **status** byte/word, matching the mod's
  `CHR_STATUS`; bit 0x04 = Petrify, bit 0x10 = Silence (both block casting)

**Cursor → slot mapping: identity, no sorting or compaction at display time.**
Sorting is applied destructively to the savemap by action 3, not at render time.

### Names / descriptions  **[PROVEN]**

Magic data array = **0x01CF4064**, **stride 60 (0x3C)**, valid for ids `< 0x40`:
```
0047E980: mov     eax, dword ptr [eax*4 + 0x1cf4064]   ; +0x00 word = NAME text offset
0047E993: mov     ecx, dword ptr [0x1cf3ecc]
0047E99E: lea     eax, [eax + ecx + 0x1cf3e48]         ; string = 0x1CF3E48 + [0x1CF3ECC] + off

0047E9CF: mov     ax, word ptr [eax*4 + 0x1cf4066]     ; +0x02 word = DESCRIPTION text offset
```
* `0x0047E970` (entry point of the above): **spell id → name string**
* `0x0047E9C0`: **spell id → description string**
* `0x01CF4064 + id*60 + 0x07` = target/attack type byte (values 5,6 are special-cased, §7)
* `0x0047E980`/`0x47E9C0` return `0x01CFF84C` (an empty/placeholder string) for unknown ids

The Magic screen also caches the highlighted entry's description pointer in
`module +0x24 = pMenuStateA + 0x242` — the mod can just read that pointer.

---

## 7. Which spells are castable in the field  **[PROVEN]**

There **is** a table, and it is **not** in the exe or `kernel.bin` — it is
**`mmagic.bin`** (the menu's magic attribute file), loaded to `*(void**)0x01D2BB10`:

```
004A1C75: push    0xb87000                ; "mmagic.bin"
004A1C7A: push    0x1d2bb10
004A1C7F: call    0x4b96c0                ; loader
```
(0x00B87000 literally contains `"mmagic.bin"`; `0x01D2BB2C` ← `"mitem.bin"`,
`0x01D2BB5C` ← `"magsort.bin"`.)

**Entry stride 4 bytes; byte 0, bit 0 = usable from the field/menu.**

Input path (state 13, quoted in §5.1) and — decisively — the **draw** path, which is what
makes the entry look greyed out:

```
004F71DB: mov     edx, dword ptr [0x1d2bb10]
004F71E1: push    0x40
004F71E3: mov     al, byte ptr [edx + ebx*4]       ; ebx = spell id
004F71E6: and     eax, 1
004F71E9: mov     ebp, eax
004F71EB: call    0x4c3050                          ; savemap[0xAE3] & 0x40
004F71F3: test    eax, eax
004F71F5: je      0x4f720c
004F71F7: lea     eax, [ebx + ebx*2]
004F71FA: lea     ecx, [eax + eax*4]
004F71FD: mov     al, byte ptr [ecx*4 + 0x1cf406b] ; magic[id] + 0x07  (target type)
004F7204: cmp     al, 5
004F7206: jb      0x4f720c
004F7208: cmp     al, 6
004F720A: jbe     0x4f7215                          ; type 5 or 6 -> forced grey
004F720C: test    ebp, ebp
004F720E: mov     ebp, 7                            ; colour 7 = normal
004F7213: jne     0x4f721a
004F7215: mov     ebp, 1                            ; colour 1 = GREYED OUT
004F721A: push    ebx
004F721B: call    0x47e970                          ; spell name
```

**Answer:** unusable spells are **greyed out, not hidden**. Every one of the 32 slots is
rendered; the name is drawn in colour 1 instead of colour 7. Exact predicate:

```c
uint8_t* mmagic = *(uint8_t**)0x01D2BB10;
bool base   = (mmagic[spellId * 4] & 1) != 0;
uint8_t tt  = *(uint8_t*)(0x01CF4064 + spellId*60 + 7);
uint8_t lock= *(uint8_t*)0x01CFE73F;              // savemap + 0xAE3, menu-lock flags
bool castable = base && !((lock & 0x40) && (tt == 5 || tt == 6));
```
`0x004C3050(bits)` is just `savemap[0xAE3] & bits`; bit 0x02 of the same byte disables the
whole "cast" action (§4.3).

---

## 8. Recommended implementation notes for the mod

### 8.1 Resolve the module pointer instead of assuming slot 2  **[strongly recommended]**

Every offset in §2 is `slot2_base + field`, and slot 2 is an **inference**. Make it a fact at
runtime — it is ~15 lines and removes the whole class of risk:

```c
// Walk the active-module list; the Magic module is the one whose update fn is 0x004F02F0.
static uint8_t* FindMagicModule(void) {
    uint8_t* m = *(uint8_t**)0x01D76B48;          // MRU list head
    for (int i = 0; i < 12 && m; i++) {
        if (m < (uint8_t*)0x01D76BC8 || m >= (uint8_t*)0x01D77078) break;  // sanity
        if (*(uint32_t*)(m + 8) == 0x004F02F0) return m;
        m = *(uint8_t**)m;                        // ->next
    }
    return NULL;
}
```
Then read fields as `mod + 0x10` (state), `mod + 0x64` (charId), etc. If you prefer to stay
with `pMenuStateA`-relative reads, at minimum **assert**
`FindMagicModule() == (uint8_t*)pMenuStateA + 0x21E` once and log a warning if not.

### 8.2 The safe read for "what spell is highlighted"

```c
uint8_t* mod = FindMagicModule();              // or (uint8_t*)pMenuStateA + 0x21E
uint16_t st  = *(uint16_t*)(mod + 0x10);       // pMenuStateA + 0x22E
if (st != 13) return;                          // 13 = the steady spell-list state
uint8_t  ch   = *(mod + 0x64);                 // pMenuStateA + 0x282
uint8_t  page = *(mod + 0x42);                 // pMenuStateA + 0x260
uint8_t  raw  = *(mod + 0x38 + ch);            // pMenuStateA + 0x256 + ch
uint8_t  slot = (uint8_t)((raw & 3) + page * 4);          // 0..31
uint8_t* mg   = (uint8_t*)0x01CFE0F8 + ch * 152 + slot*2;
uint8_t  id = mg[0], qty = mg[1];
// row within the visible window = raw & 3 (0..3), page = 0..7, "N of 32" = slot+1
```
Announce empty slots explicitly (`id == 0 || qty == 0`), and announce the greyed-out state
using the predicate in §7 — a blind player otherwise cannot tell that Cure is selectable but
Meltdown is not.

### 8.3 Gating summary

| screen | gate |
|---|---|
| Magic active at all | `+0x1E6 == 2` **and** `+0x1E8 == 3` |
| character select (already handled) | `+0x1E8 == 3` and `+0x22E == 0` |
| action row | `+0x22E == 3` (announce `+0x27F` 0..3, mask `+0x285`) |
| spell list | `+0x22E == 13` (see §8.2) |
| paging | `+0x22E == 14/15` (left) or `16/17` (right) — re-announce on return to 13 |
| target select | `+0x22E == 20` (cursor `+0x275`, count `+0x27E`, mask `+0x254`) |
| sort menu | `+0x22E == 72` (cursor `+0x28F`, 7 entries, labels group 8 #15..21) |
| discard confirm | `+0x28C != 0` (cursor `+0x28E`, victim = char `+0x250`, slot `+0x251`) |
| 2-character screens | `+0x274 == 3 or 4`; 2nd char `+0x280`, its page `+0x264` |
| closing | `+0x22E == 112/113` |

---

## 9. Things I could NOT determine

1. **The English labels of the four action-row entries and the seven sort orders.** They are
   in `mngrp.bin`, not in the exe. *Cheapest fix:* call `0x004BD630(1, 8, idx, 0)` at runtime
   (idx from `{0,1,11,2}` and `15..21`) and run the result through the mod's existing FF8
   text decoder. One build cycle, no guessing.
2. **The precise difference between action 1 and action 2** (both are cross-character
   magic transfer flows). *Cheapest fix:* the logging experiment in §5.4.
3. **`module +0x72` = sort-popup entry count.** No write to it exists in the state machine
   0x4F02F0–0x4F5C40, so it must be written by the draw function 0x4F67C0 (which walks the
   `+0x2C` table). Marked INFERRED. It doesn't matter — the count is a constant 7 from the
   table at 0x00B88A9C.
4. **Whether slot 2 of the module pool is always the submenu.** Handled by §8.1.
5. The exact input-bit → PC-key mapping. I used the PSX layout
   (0x1000 Up / 0x2000 Right / 0x4000 Down / 0x8000 Left; low byte 0x04 L1, 0x08 R1,
   0x40 confirm, 0x10 cancel, 0x80 the discard button). Directions are **[PROVEN]** from
   `0x004C0A30`; the face-button assignments are **[INFERRED]** from usage.

---

## 10. Quick reference — addresses used above

| address | what |
|---|---|
| 0x004BE540 | menu module allocator |
| 0x004BE610 | menu module free |
| 0x004BD630 | menu text getter `(bank, group, index, sub)` |
| 0x004BDB30 | open submenu by index (uses table 0x00B87ED8) |
| 0x004BFFC0 | build horizontal command-row X positions from an entry table |
| 0x004C0A30 | vertical cursor move with wrap `(input, count, cur)` |
| 0x004C2E00 | horizontal cursor move over a bitmask `(input, mask, cur)` |
| 0x004ABC20 | popcount |
| 0x004ABC40 | index of the n-th set bit |
| 0x004AD030 | available-character bitmask |
| 0x004C3050 | `savemap[0xAE3] & bits` (menu locks) |
| 0x0047E970 | spell id → name string |
| 0x0047E9C0 | spell id → description string |
| 0x004F0030 | **magic sort** `(charId, orderIndex)` — rewrites the savemap |
| 0x004F00D0 | Magic module creator |
| 0x004F02F0 | **Magic state machine** (jump table 0x004F5C4C) |
| 0x004F67C0 | Magic draw function |
| 0x004F5E14 | action-row confirm jump table (4 entries) |
| 0x00B88A90 | Magic action-row entry table `{0,1,11,2,-1}` (group 8) |
| 0x00B88A9C | Magic sort-order popup table `{15..21,-1}` (group 8) |
| 0x00B88AB8 | Item action-row entry table `{0,2,21,1,-1}` (group 9) |
| 0x00B87ED8 | submenu dispatch table `{creator, id}` × N |
| 0x01D76B48 | active-module list head |
| 0x01D76BC8 | module pool base (10 × 0x78) |
| 0x01CF4064 | magic data array, stride 60 |
| 0x01CFE0F8 | savemap char[0].Magics[0] |
| 0x01D2BB10 | → `mmagic.bin` (field-usable flags, stride 4, bit 0) |
| 0x01D2BB5C | → `magsort.bin` (sort orders, 0x40 bytes each) |
| 0x01D8D898 | scratch: this character's computed stats (464 bytes) |
| 0x01D8D448 | scratch: second character's computed stats |
| 0x01D8DA68 | discard-confirmation formatted message buffer |
