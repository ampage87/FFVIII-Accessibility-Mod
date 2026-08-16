# FF8 Magic submenu — Exchange / All / help-bar follow-up

Continuation of `magic_menu_findings.md`. Same conventions:
`module = pMenuStateA + 0x21E` (slot 2 of the pool at 0x01D76BC8 — still resolve it at
runtime by matching `*(u32*)(mod+8) == 0x004F02F0`).
Magic state machine = `0x004F02F0`, jump table `0x004F5C4C`; draw fn = `0x004F67C0`.

**[PROVEN]** = read out of the machine code, decisive instruction quoted.
**[INFERRED]** = reasoned, not directly proven.

---

## ⚠ Read this first: the "All" flow runs in the opposite direction to the brief

The brief says to treat as ground truth: *"Upon selecting All you select a character to give
all their magic, then select a character to receive all the magic."*

**The code says the opposite, and the game's own help string agrees with the code.**

* Step 1 (state 97) selects `module +0x64` (**pMenuStateA + 0x282**).
* Step 2 (state 99) selects `module +0x62` (**pMenuStateA + 0x280**).
* The transfer is `0x004F5FA0(arg1 = +0x62, arg2 = +0x64)`, and inside that function
  **arg1 loses its magic and arg2 gains it**.

⇒ **Step 1 picks the RECEIVER, step 2 picks the GIVER.**

That also matches the decoded label you supplied: action 2 = "All" / *"Take **all magic
from** other members"* — the character you are on **takes**. Step 1 starts on the character
whose Magic screen you opened, i.e. the taker; step 2 picks who to take from.

I think the tester described the screen from memory and inverted it. I have not silently
followed the instruction to treat the description as ground truth, because announcing
"choose who gives" on the receiver step would actively mislead a blind player. The proof is
below; it is three instructions long and worth checking before you ship a label.

### The proof  **[PROVEN]**

State 105 performs the transfer:
```
004F59D2: mov     al, byte ptr [ebp + 0x64]
004F59D7: mov     cl, byte ptr [ebp + 0x62]
004F59DA: push    eax                       ; -> arg2  = +0x64
004F59DB: push    ecx                       ; -> arg1  = +0x62
004F59DC: call    0x4f5fa0
```
`0x004F5FA0(arg1, arg2)`:
```
004F5FA2: mov     ebx, dword ptr [esp + 0xc]      ; ebx = arg1
004F5FA7: mov     ebp, dword ptr [esp + 0x14]     ; ebp = arg2
004F5FC3: lea     edi, [ecx*8 + 0x1cfe0f8]        ; &char[arg1].Magics[0]
004F5FBB: mov     dword ptr [esp + 0x18], 0x20    ; 32 slots
004F5FCA: movsx   esi, byte ptr [edi]             ; id
004F5FCD: movsx   eax, byte ptr [edi + 1]         ; qty
004F5FDB: push    eax ; push esi ; push ebp
004F5FDE: call    0x4c2c70                        ; ADD qty of id to arg2
004F5FF2: push    eax                             ; = amount actually added
004F5FF3: push    esi ; push ebx
004F5FF5: call    0x4c2d50                        ; REMOVE that amount from arg1
```
`0x004C2C70(charId, spellId, qty)` adds, caps at 100 and returns `newQty - oldQty`:
```
004C2CC0: movsx   eax, byte ptr [edx + 1]
004C2CCA: add     eax, edi
004C2CCC: cmp     eax, 0x64
004C2CD1: mov     eax, 0x64                       ; clamp to 100
004C2CD8: mov     byte ptr [edx + 1], al
004C2CDC: sub     eax, ecx                        ; return delta
```
`0x004C2D50(charId, spellId, qty)` subtracts, clamped at 0 (`004C2D8C: sub edx, edi`).

Corroboration: step 2's character mask **excludes the character chosen in step 1**, which
only makes sense if step 1 ran first and fixed one of the two roles:
```
004F580D: mov     cl, byte ptr [ebp + 0x64]
004F5810: mov     edx, 1
004F5815: shl     edx, cl
004F5822: not     edx
004F5824: and     eax, edx                        ; avail &= ~(1 << +0x64)
004F5832: mov     byte ptr [ebp + 0x62], al       ; <- step 2 writes +0x62
```

---

## 1. Action 2 — "All" (states 93 … 107)

### 1.1 State sequence  **[PROVEN]**

| state (`+0x22E`) | addr | role |
|---|---|---|
| 93 | 0x4F1B4A | entry: pick an initial `+0x62` ≠ `+0x64`; → 94 |
| 94, 95, 96 | 0x4F1B77/7D/83 | one-frame stubs; → 97 |
| **97** | 0x4F1B8E | **STEP 1 — steady. Choose `+0x64` (the RECEIVER).** |
| 4 / 6 (+5/7) | 0x4F1FD2 / 0x4F2247 | character-change slide used by step 1; returns to `[+0x16]` = 97 |
| 98 | 0x4F56D6 | one-frame: mode := 4, `+0x46` := partner cursor >> 2; → 99 |
| **99** | 0x4F572C | **STEP 2 — steady. Choose `+0x62` (the GIVER).** |
| 100, 101 | 0x4F57F0, 0x4F587A | slide "previous" for `+0x62` |
| 102, 103 | 0x4F58DD, 0x4F5967 | slide "next" for `+0x62` |
| 104 | 0x4F1C3B | confirm: run the pre-flight check `0x4F6140` |
| 105 | 0x4F59CC | **THE TRANSFER RUNS HERE** (`0x4F5FA0`); → 96 → 97 |
| 106, 107 | 0x4F1C8B, 0x4F1CB6 | warning message box (text `0x4C25D0(0x3A)`, window 0x6F) |

### 1.2 How to tell the two steps apart — the answer  **[PROVEN]**

**Use the state word `pMenuStateA + 0x22E`:**

* `== 97` → **step 1, choosing the RECEIVER** (the character who will take everything)
* `== 99` → **step 2, choosing the GIVER** (the character who will be emptied)

States 97 and 99 are reached from nowhere else in the state machine, so this test is exact.

Secondary corroborating signal — the screen-mode byte `pMenuStateA + 0x274`
(`module +0x56`): **3** during step 1 (`004F1C03/004F1C29/004F57DB: mov byte ptr [ebp+0x56], 3`),
**4** during step 2 (`004F56EB: mov byte ptr [ebp+0x56], 4`). Mode alone is *not* sufficient —
the Exchange flow uses 3 and 4 as well. Gate on the state.

The slide states are transient: 4/5/6/7 during step 1, 100/101/102/103 during step 2. If you
want a stable announcement, announce on entry to 97 / 99 and on return from a slide
(the slide states restore `[+0x16]` = 97 for step 1, and 101/103 fall back to 99).

### 1.3 Cursors and masks  **[PROVEN]**

**There is no numeric "cursor index" and no `+0x36`/`+0x275` involvement in this flow.**
Both steps store the chosen **character id directly**, and Left/Right step through a
character order list:

```
004BFD90 / 004BFDF0  (prev / next):
004BFD99: mov     cl, byte ptr [edx + 0x1d77194]   ; character order list
004BFD9F: cmp     cl, 0xff                          ; 0xFF-terminated
```
* **Character order list = `0x01D77194` = pMenuStateA + 0x6FA**, `0xFF`-terminated,
  ≤ 9 entries. This is the on-screen left-to-right order.
* Step 1 (`state 97` → states 4/6) writes **`+0x64`** (pMenuStateA + 0x282) —
  `004F204D: mov byte ptr [ebp + 0x64], al`. Mask = `0x4AD030()` (available characters).
* Step 2 (`state 99` → states 100/102) writes **`+0x62`** (pMenuStateA + 0x280) —
  `004F5832: mov byte ptr [ebp + 0x62], al`. Mask = `0x4AD030() & ~(1 << +0x64)`
  (**rebuilt each time, and it excludes the step-1 choice**).
* `module +0x65` (**pMenuStateA + 0x283**) holds the *previous* character id while a slide
  animation is running (`004F201F`/`004F581F`). Useful to suppress double announcements.
* Step 1 also re-derives the action-enable mask `+0x285` and the page `+0x260` for the new
  character (`004F20D0`/`004F215C`), so those stay valid.

Input bits in both steps: `0x8004` (Left **or** L1) = previous character,
`0x2008` (Right **or** R1) = next character, `0x40` = confirm, `0x10` = cancel.
Cancel in step 2 returns to step 1 (`004F57E0: mov word ptr [ebp+0x10], 0x61`).
Both steps are skipped entirely if fewer than 2 characters are available
(`004F5769: cmp eax, 2 ; jl`).

### 1.4 Confirmation before the transfer  **[PROVEN]**

**There is no Yes/No dialog.** The second confirm goes straight to state 104, which runs a
pre-flight check and then either warns or commits:

```
004F1C63: call    0x4f6140                 ; pre-flight check
004F1C6B: test    eax, eax
004F1C6D: jne     0x4f59af                 ; PROBLEM -> state 106 = message box
004F1C73: push    2 ; call 0x4b92a0        ; confirm sfx
004F1C81: mov     eax, 0x69                ; -> state 105 = DO IT
```
`0x4F59AF` sets `[+0x16] = 105`, `[+0x18] = 98` and enters state 106, which opens a message
window: `0x4C25D0(0x3A)` → `0x4BD630(0, 0, 0x3A, 0)` — note **text bank 0** (the field/battle
bank at `*(u32*)0x01D2BB78`), group 0, entry 0x3A — displayed in window id 0x6F.

`0x004F6140(giver, receiver)` is a **simulation**: it reads the giver's current HP
(`004F6163: mov ax, word ptr [edi + 0x1cfe0e8]`), reads a junction-slot byte at
`char[giver] + 0x5C` (`004F6185`), temporarily rewrites the giver's magic array, calls
`0x4BFC90` to recompute derived stats, samples the new max HP, then restores everything
(`004F6296`–`004F62DD`). **[INFERRED]** it is the "this would un-junction magic and change
your HP" warning; the exact junction slot at `char + 0x5C` I did not pin down and am not
going to guess. What matters for the mod is only: *nonzero ⇒ a message box appears instead
of the transfer*.

For the mod: announce the transfer as done when `+0x22E` reaches **105**, and announce a
warning when it reaches **106**.

### 1.5 Practical gate

```c
uint16_t st = *(uint16_t*)(pMenuStateA + 0x22E);
if (st == 97) { /* "Take magic — choose who receives", char = *(u8*)(pMenuStateA+0x282) */ }
if (st == 99) { /* "Choose who to take from",          char = *(u8*)(pMenuStateA+0x280) */ }
if (st == 105) { /* transfer executed */ }
if (st == 106) { /* warning window */ }
```
Label wording: use group 8 entry **12** for step 1 and entry **13** for step 2 — that is what
the game itself puts in the help bar (`0x4F6600(0xC)` in state 97, `0x4F6600(0xD)` in state
98). `0x4F6600(n)` = `0x4BD630(1, 8, n, 0)`, i.e. **sub-index 0 = the label**, so those are
group-8 string indices **24** and **26**. You already have group 8 decoded — read those two
strings and you have the game's own names for the two steps.

---

## 2. The help bar

### 2.1 There is exactly one, and it is driven by `module +0x24`  **[PROVEN]**

In the draw function `0x004F67C0`:
```
004F6996: push    0x4f70c0                      ; content callback
004F699B: lea     edx, [esi + 0x24]             ; ctx = &module[+0x24]
004F69AE: mov     word ptr [0x1d76a80], 0x1e    ; X = 30
004F69B7: mov     word ptr [0x1d76a82], 0x1d    ; Y = 29
004F69C0: mov     word ptr [0x1d76a84], 0x144   ; W = 324
004F69C9: mov     word ptr [0x1d76a86], 0x1a    ; H = 26
004F69E7: mov     word ptr [0x1d76a94], cx      ; scroll = [+0x34] + [+0x44]
004F69EE: mov     dword ptr [0x1d76aa0], edx    ; callback context
004F69F4: call    0x4b2900
```
That is a **324 × 26 px bar at (30, 29)** — the wide strip across the top of the Magic screen.
It is the only `0x4B2900` call in the whole Magic draw function that installs a text context
(every other site does `mov dword ptr [0x1d76aa0], 0`). **There is no second help/description
text source.**

The callback:
```
004F70C0: mov     eax, dword ptr [esp + 0xc]     ; 0 = incoming, 1 = outgoing
004F70C4: mov     ecx, dword ptr [0x1d76aa0]     ; = &module[+0x24]
004F70D0: mov     eax, dword ptr [ecx + eax*4]   ; module[+0x24] or module[+0x28]
004F70D3: test    eax, eax
004F70D5: je      0x4f712c                        ; NULL -> draw nothing
004F70FD: call    0x4b8b30                        ; decode / measure the FF8 string
004F711B: call    0x4bde30                        ; draw, colour 7
```
So `+0x24` = the current string, `+0x28` = the outgoing string during the slide/cross-fade
(`module +0x34` and `+0x44` are the scroll offsets). **NULL means the bar is blank** — which
is exactly what happens on an empty magic slot.

The window globals are the engine's shared "window build" block, at
`pMenuStateA − 0x1A / −0x18 / −0x16 / −0x14` (rect) and `pMenuStateA + 0x06` (context).

### 2.2 Raw encoded bytes, not a rendered buffer  **[PROVEN]**

`0x004BD630(bank, group, entry, sub)` does no copying at all — it returns a pointer straight
into the loaded text image:
```
004BD638: mov     eax, dword ptr [0xb86d30]      ; bank != 0 : menu text image
004BD63D: add     eax, 0x2e000
004BD644: mov     eax, dword ptr [0x1d2bb78]     ; bank == 0 : field/battle text image
004BD651: mov     cx, word ptr [eax + ecx*2 + 2] ; groupOff = u16 at bank+2+group*2
004BD661: add     eax, ecx                       ; groupBase = bank + groupOff
004BD66D: lea     edx, [edx + ecx*2]             ; j = sub + entry*2
004BD674: mov     cx, word ptr [eax + edx*2 + 2] ; strOff  = u16 at groupBase+2+j*2
004BD684: add     eax, ecx                       ; string  = groupBase + strOff
004BD688: mov     eax, 0x1d7714c                 ; fallback "no text"
```
(This also confirms your decoding rule: **string index = entry\*2 + sub**, sub 0 = label,
sub 1 = help.)

So `+0x24` points at **raw FF8-encoded bytes, NUL-terminated**, inside the loaded
`mngrp.bin` image. `0x4B8B30` (decode/measure) and `0x4BDE30` (draw) are applied at render
time; nothing is pre-rendered into a buffer.

### 2.3 Stability — safe to read from another thread  **[PROVEN]**

Three possible sources, all stable pointers into long-lived loaded data, never per-frame
scratch:

| source | pointer formula | used by |
|---|---|---|
| menu text (bank 1) | `*(u32*)0x00B86D30 + 0x2E000 + groupOff + strOff` | action row, sort menu, step labels |
| field text (bank 0) | `*(u32*)0x01D2BB78 + groupOff + strOff` | message boxes (`0x4C25D0`) |
| spell description | `0x01CF3E48 + *(u32*)0x01CF3ECC + off` (`0x0047E9C0`) | spell list, exchange lists |

Sentinels to treat as "no text":
* **`NULL`** — written deliberately (state 0, and state 13 on an empty slot)
* **`0x01D7714C`** (= pMenuStateA + 0x6B2) — `0x4BD630`'s "missing string" fallback
* **`0x01CFF84C`** — `0x47E970`/`0x47E9C0`'s "unknown spell id" fallback

The field itself is a naturally-aligned dword written with a single `mov`, so a cross-thread
read is atomic on x86: worst case you get the previous frame's pointer, which is still a
valid string. **No locking needed, no copying needed — just deref and decode.**

### 2.4 Which states write `+0x24`  **[PROVEN]**

Written (every frame while the state is active, unless noted):

`0, 3, 4, 6, 13, 14, 16, 26, 27, 28, 44, 45, 47, 51, 54, 61, 62, 72, 75, 77, 79, 80, 82, 85, 86, 88, 97, 98`

Against the four you asked about:

| screen | state | `+0x24` refreshed? | content |
|---|---|---|---|
| action row | 3 | **yes**, every frame | `0x4BD630(1, 8, tbl[cursor], **1**)` — the *help* string (e.g. "Use magic") |
| spell list | 13 | **yes**, every frame | `0x47E9C0(spellId)` = the spell description; **NULL on an empty slot** |
| target select | **20** | **NO — stale** | keeps the description written by state 13 |
| sort popup | 72 | **yes**, every frame | `0x4BD630(1, 8, 15 + cursor, **1**)` — the help for the highlighted sort order |
| two-column (Exchange) | 26, 27, 28, 44 | **yes** | 26/44: spell description; 27/28: `0x4BD630(1, 8, 3, 0)` |
| All step 1 / step 2 | 97 / 98 | 97 **yes**, 99 **NO** | written on entry to 97 and 98 (entries 0xC / 0xD, sub 0) |

Notable stale states: **20** (target select) and **99** (All step 2) keep whatever the
preceding state left. In both cases the retained string is still semantically right
(state 20 keeps the spell you are casting; state 99 keeps the step-2 label state 98 wrote),
so the bar is never *wrong* — but if the mod re-reads on a timer it will see no change.
All other unlisted states (5, 7–12, 15, 17–19, 21–25, 29–43, 46, 48–50, 52, 53, 55–60,
63–71, 73, 74, 76, 78, 81, 83, 84, 87, 89–96, 99–113) also leave it stale; most are
one-frame animation states, so this rarely matters.

### 2.5 Verification recipe for "Use magic"  **[PROVEN arithmetic, prediction untested]**

While sitting on the Magic action row with cursor 0 (`pMenuStateA + 0x27F == 0`,
`pMenuStateA + 0x22E == 3`), `*(char**)(pMenuStateA + 0x242)` must equal:

```c
uint8_t* bank  = *(uint8_t**)0x00B86D30 + 0x2E000;
uint8_t* grp   = bank + *(uint16_t*)(bank + 2 + 8*2);        // group 8
int      entry = 0;                                          // 0xB88A90[0]
int      j     = entry*2 + 1;                                // sub 1 = help
uint8_t* s     = grp + *(uint16_t*)(grp + 2 + j*2);          // == string index 1
```
and the bytes at `s` are the FF8-encoded "Use magic" terminated by `0x00`. If your offline
mngrp.bin decode puts "Use magic" at group-8 string index 1, this is a one-line assertion in
the next BAT run and it validates the whole chain.

---

## 3. Action 1 — "Exchg." (Exchange)

### 3.1 There is no "which column has focus" byte — the flow is modal  **[PROVEN]**

I looked for one and it does not exist. Focus is carried entirely by the **state number**;
`module +0x56` (mode) only coarsely separates "phase A" (3) from "phase B" (4), and the All
flow reuses the same two values. The full sequence:

| state | addr | role | steady? |
|---|---|---|---|
| 23 | 0x4F30C6 | entry: mode := 3, `+0x46` := own cursor >> 2, pick partner `+0x62`, build partner stats into 0x1D8D448 | no |
| 24, 25 | 0x4F3116, 0x4F0782 | one-frame; → 26 | no |
| **26** | 0x4F078D | **browse YOUR list** (character `+0x64`) | **yes** |
| 14/15, 16/17 | | page left / right for column A, return via `[+0x1A]` = 26 | no |
| 27 | 0x4F3190 | one-frame: mode := 4, `+0x46` := partner cursor >> 2; → 28 | no |
| **28** | 0x4F3249 | **choose the PARTNER character** (`+0x62`); your chosen spell stays highlighted | **yes** |
| 29/30, 31/32 | 0x4F3381 … | partner-change slides (write `+0x62`, mask excludes `+0x64`) | no |
| 40 | 0x4F09A1 | resolve: does the partner already hold that spell? | no |
| 41, 42, 43 | 0x4F36A0/A6/AC | one-frame routing → 44 / 27 / 53 | no |
| **44** | 0x4F0A84 | **browse the PARTNER's list** (character `+0x62`) | **yes** |
| 45, 47, 51 | | help-text refresh / animation variants of the two-column screen | |
| 53, 54 | 0x4F3E2D, 0x4F3E31 | open the 3-entry popup; mode flags `+0x5E := 2` | no |
| **55** | 0x4F101A | **3-entry popup**, cursor `+0x5F` | **yes** |
| **52** | 0x4F0E83 | **2-entry popup**, cursor `+0x5F`, `+0x5E := 1` | **yes** |
| 56, 57, 59 | 0x4F3EF0, 0x4F115B, 0x4F417C | execute swap / move | no |
| 60, 61, 62 | 0x4F43A7 … | open the quantity picker | no |
| **63** | 0x4F1432 | **quantity split**, `+0x58` / `+0x59` | **yes** |
| 65 | 0x4F490A | leave the Exchange screen (mode := 0) | no |

**Gate the mod on the state number**, exactly as for the All flow:
26 = "your list", 28 = "choose partner", 44 = "partner's list", 52/55 = option popup,
63 = quantity.

### 3.2 Both columns use the same per-character cursor array  **CONFIRMED [PROVEN]**

Yes — your assumption is right, and the safe expression is the same shape as state 13.

**Column A (state 26)** — character `A = *(u8*)(pMenuStateA + 0x282)`:
```
004F0796: mov     al, byte ptr [ebp + 0x64]
004F0799: mov     cl, byte ptr [eax + ebp + 0x38]   ; per-character cursor
004F079F: and     eax, 0x80000003                   ; row = cursor & 3
004F07AC: push    4                                 ; 4 rows per page
004F07AF: call    0x4c0a30
004F07B4: movsx   edx, byte ptr [ebp + 0x42]        ; page A
004F07C0: lea     eax, [eax + edx*4]                ; abs = row + pageA*4
004F07CD: mov     byte ptr [ecx + ebp + 0x38], al
```
**Column B (state 44)** — character `B = *(u8*)(pMenuStateA + 0x280)`:
```
004F0B37: push    4
004F0B3A: call    0x4c0a30
004F0B4E: mov     dl, byte ptr [ebp + 0x62]
004F0B5D: mov     byte ptr [edx + ebp + 0x38], al   ; same +0x38 array, indexed by B
004F0B66: mov     cl, byte ptr [eax + ebp + 0x38]
004F0B6A: and     ecx, 0x80000003
004F0B77: movsx   edx, byte ptr [ebp + 0x46]        ; page B
004F0B7B: lea     ecx, [ecx + edx*4]                ; abs = row + pageB*4
```

```c
// column A (state 26, and the frozen highlight shown in states 28/44/52/55/63)
uint8_t A    = *(uint8_t*)(pMenuStateA + 0x282);          // module +0x64
uint8_t slotA= (*(uint8_t*)(pMenuStateA + 0x256 + A) & 3)
             + *(uint8_t*)(pMenuStateA + 0x260) * 4;      // page A = module +0x42

// column B (state 44)
uint8_t B    = *(uint8_t*)(pMenuStateA + 0x280);          // module +0x62
uint8_t slotB= (*(uint8_t*)(pMenuStateA + 0x256 + B) & 3)
             + *(uint8_t*)(pMenuStateA + 0x264) * 4;      // page B = module +0x46

// then: savemap magic = 0x01CFE0F8 + charId*152 + slot*2  -> {id, qty}
```
Screen geometry (useful for narration): both panels are at **X = 0xD4**, column A rows at
**Y = 13·row + 0x43**, column B rows at **Y = 13·row + 0x9A** — the two lists are **stacked
vertically**, not side by side (`004F3EC2: lea eax, [eax + edx*4 + 0x9a]`).

### 3.3 What confirm does at each step  **[PROVEN]**

* **State 26 (your list)** — `0x40` confirm: force `+0x62 != +0x64`, then → 27 → **28**.
  `0x10` cancel → state 65 (leave). `0x8000/0x2000` page. `0x04/0x08` (L1/R1) change `+0x64`.
  **`0x80` (Square) opens the DISCARD dialog** with return state 26
  (`004F0985: mov word ptr [ebp+0x6c], 0x1a` … `004F0997: mov eax, 0x6c`) — so the discard
  confirm window (`+0x28C`, `+0x28E`, victim `+0x250`/`+0x251`) can appear from here too.
* **State 28 (choose partner)** — `0x8004` → 29, `0x2008` → 31 (both write `+0x62`,
  mask excludes `+0x64`); `0x40` confirm → **40**; `0x10` cancel → back to 26.
* **State 40 (resolve)** — reads the spell id at column A's slot:
  * id == 0 → 41 (nothing to move)
  * partner **already holds** that spell → `+0x5A := spell id`, partner's cursor is moved to
    that slot, `+0x68 := 0x81`, `[+0x1C] := 28`, → 53 → 54 → **55** (3-entry popup)
  * partner does **not** hold it → 41 → 42 → 43 → **44** (browse the partner's list to pick a
    destination slot)
* **State 52 (2-entry popup)** — confirm on entry 0: both slots occupied → **57** (swap),
  else → **59** (move into the empty slot). Entry 1 → **60** → 63 (quantity).
* **State 55 (3-entry popup)** — entry 0 → `+0x68 := 0x80`, state 57; entry 1 →
  `+0x68 := 0x81`, state 57; entry 2 → `[+0x1A] := 52`, state **60** → 63 (quantity).
  Cancel returns to `[+0x1C]`.

### 3.4 The popup labels  **[PROVEN]**

Neither popup state fetches its own entry text; the **draw function** renders them, gated on
`module +0x5E`:

`+0x5E & 1` ⇒ **2-entry popup** (state 52), X = `0xC0 − (+0x5B)/2`:
```
004F6C00: test    al, al                       ; slot occupied?
004F6C04: push    0xa                          ;  no -> group 8 entry 10
004F6C08: push    5                            ; yes -> group 8 entry  5
004F6C0E: call    0x4bd630                     ; entry 0, drawn at Y = 0x78
004F6C2C: push    6                            ; entry 1 = group 8 entry 6, Y = 0x85
```
`+0x5E & 2` ⇒ **3-entry popup** (state 55), X = `0xC0 − (+0x5B)/2`:
```
004F6CC7: push    5      ; entry 0  Y = 0x71   -> group 8 entry 5
004F6CEF: push    0xa    ; entry 1  Y = 0x7E   -> group 8 entry 10
004F6D1E: push    6      ; entry 2  Y = 0x8B   -> group 8 entry 6
```
All with `sub = 0`, so these are **group-8 string indices 10, 20 and 12**. Decode those three
offline and you have the popup labels. **[INFERRED]** from your string list they will be the
"Split" / "Give All" / "Take All" trio, but the mapping to cursor positions should come from
your decode, not from me.

`module +0x5B` (**pMenuStateA + 0x279**) is the popup's pixel **width** (used for centering),
not a count. `module +0x5F` (**pMenuStateA + 0x27D**) is the popup cursor: range 0–1 in
state 52, 0–2 in state 55.

### 3.5 Yes, there is a quantity-selection step — state 63  **[PROVEN]**

Two paired bytes, and their **sum is conserved**:

* **`module +0x58` = pMenuStateA + 0x276** — the amount on the receiving side
* **`module +0x59` = pMenuStateA + 0x277** — the amount left on the giving side
* **`module +0x5A` = pMenuStateA + 0x278** — the spell id being moved (set in state 40)

```
004F1498: mov     al, byte ptr [ebp + 0x58]
004F14A0: cmp     al, 0x64                    ; hard cap 100
004F14A4: mov     cl, byte ptr [ebp + 0x59]
004F14A7: test    cl, cl ; je                 ; nothing left to move
004F14AB: cmp     cl, 0xa
004F14AE: mov     ebx, 0xa                    ; coarse step = 10 ...
004F14B5: movsx   ebx, cl                     ; ... clamped to what remains ...
004F14C0: mov     eax, 0x64 ; sub eax, ecx
004F14C4: cmp     eax, ebx ; jg ; mov ebx, eax ; ... and to the 100 headroom
004F14C8: test    edi, 0x8000                 ; +1
004F14DD: inc     cl ; mov [ebp+0x58], cl
004F14E7: mov     al, [ebp+0x59] ; dec al ; mov [ebp+0x59], al
004F14EF: test    edi, 0x1000                 ; +step
004F1508: add     cl, bl ; ... sub al, bl
004F1545: test    edi, 0x2000                 ; -1
004F1570: test    edi, 0x4000                 ; -step
```

| input bit | effect |
|---|---|
| 0x8000 | `+0x58` += 1, `+0x59` -= 1 |
| 0x1000 | `+0x58` += step, `+0x59` -= step |
| 0x2000 | `+0x58` -= 1, `+0x59` += 1 |
| 0x4000 | `+0x58` -= step, `+0x59` += step |

`step = min(10, amount available on the losing side, 100 − amount on the gaining side)`.

**Bounds: both bytes are 0…100 (0x64) and neither side can exceed 100.** Confirm (`0x40`)
commits; cancel (`0x10`) restores and returns to `[+0x1C]`.

**[INFERRED]** which of the two bytes belongs to which character on screen. 0x8000/0x2000
are Left/Right in this build's pad map, but the two lists are stacked vertically, so I
cannot say from the code alone whether "Left" visually means "toward the top panel". Cheapest
experiment: on the split screen, log `+0x276` and `+0x277` while pressing Left once, then
check which panel's number went up. One run, ten seconds.

### 3.6 `module +0x68` (pMenuStateA + 0x286) — transfer mode  **[PROVEN values, [INFERRED] meaning]**

Written as `0x80`, `0x81` and `0`; read as `test byte ptr [ebp+0x68], 0x7f` (state 52's guard,
`004F0E83`) and as a whole byte in the execute states. So **bit 7 = "whole stack" and bits
0–6 carry a small amount/selector**. `0x80` vs `0x81` distinguishes the two non-Split popup
entries. I did not fully separate the two directions; the state-level gating in §3.3 is
sufficient for narration without it.

---

## 4. New / corrected offsets to add to the mod

`pMenuStateA` = 0x01D76A9A; submenu module = `pMenuStateA + 0x21E`.

| pMenuStateA | module | type | meaning | tag |
|---|---|---|---|---|
| +0x23A | +0x1C | u16 | return state for the exchange popups | PROVEN |
| +0x264 | +0x46 | u8 | **page of the partner's list (column B)** | PROVEN |
| +0x276 | +0x58 | u8 | **quantity split — receiving side (0…100)** | PROVEN |
| +0x277 | +0x59 | u8 | **quantity split — giving side (0…100)** | PROVEN |
| +0x278 | +0x5A | u8 | **spell id being exchanged** | PROVEN |
| +0x279 | +0x5B | u8 | popup pixel width (centering only) | PROVEN |
| +0x27C | +0x5E | u8 | render flags: bit0 = 2-entry popup up, bit1 = 3-entry popup up | PROVEN |
| +0x27D | +0x5F | u8 | **popup cursor** (0–1 in state 52, 0–2 in state 55) | PROVEN |
| +0x280 | +0x62 | u8 | **partner character** — All: the GIVER; Exchange: the other list | PROVEN |
| +0x282 | +0x64 | u8 | **own character** — All: the RECEIVER | PROVEN |
| +0x283 | +0x65 | u8 | previous character id during a slide | PROVEN |
| +0x286 | +0x68 | u8 | transfer mode (bit7 = whole stack) | PROVEN / INFERRED |
| +0x6FA | — | u8[] | **character order list, 0xFF-terminated** (0x01D77194) | PROVEN |

Useful engine entry points added this round:

| address | what |
|---|---|
| 0x004F5FA0 | `takeAllMagic(giver, receiver)` |
| 0x004F6140 | pre-flight check for the above; nonzero ⇒ warning box |
| 0x004C2C70 | `addMagic(charId, spellId, qty)` → amount actually added (caps at 100) |
| 0x004C2D50 | `removeMagic(charId, spellId, qty)` (clamps at 0) |
| 0x004C3120 | `tidyMagic(charId)` — clears ids whose qty is 0 |
| 0x004BFD90 / 0x004BFDF0 | previous / next character in the order list at 0x01D77194 |
| 0x004F6600 | `helpText(n)` = `0x4BD630(1, 8, n, 0)` (label, not help) |
| 0x004C25D0 | `fieldText(n)` = `0x4BD630(0, 0, n, 0)` (message-box text) |
| 0x004C2B10 | `openMessageWindow(textPtr, windowId)` |
| 0x004F70C0 | help-bar content callback (`ctx[0]` = `+0x24`, `ctx[1]` = `+0x28`) |

---

## 5. Still open

1. **Which of `+0x276` / `+0x277` is the top panel** on the split screen (§3.5). Ten-second
   experiment described there.
2. **The exact junction slot at `char + 0x5C`** used by the pre-flight check `0x4F6140`
   (§1.4). Not needed for narration.
3. **The 0x80 vs 0x81 semantics of `+0x286`** (§3.6). Not needed for narration.
4. **Group-8 string indices 10, 12, 20, 24, 26** — I can prove which *indices* the code uses
   but not what they say; you have the decoder. These five give you: both All step labels and
   all three exchange-popup labels.
