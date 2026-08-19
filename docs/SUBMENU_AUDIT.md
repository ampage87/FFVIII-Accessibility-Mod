# Submenu audit — Junction through Save (#88, v0.29.0)

Every main-menu submenu read against its state machine in `FF8_EN.exe`.
Findings are marked **CONFIRMED** only where the disassembly was read directly;
everything else is marked SUSPECTED and says what would settle it.

---

## 1. Module identity — the thing that makes the rest of this readable

Menu modules are allocated from a pool at `0x01D76BC8` (stride `0x78`, 10 slots,
allocator `0x004BE540`) and threaded onto an MRU-first list whose head is
`0x01D76B48`. Each header is `+0x00 next`, `+0x04 prev`, `+0x08 update fn`,
`+0x0C draw fn`, `+0x10 state (u16)`, `+0x12 in-use`.

The update function is the module's identity. Read out of each creator:

| screen   | creator      | update fn    | draw fn      |
|----------|--------------|--------------|--------------|
| Tutorial | `0x004C9B70` | `0x004C9CB0` | `0x004CAE10` |
| Switch   | `0x004CB850` | `0x004CBA50` | `0x004CC6C0` |
| Status   | `0x004CDFA0` | `0x004CE080` | `0x004CECF0` |
| GF       | `0x004D4840` | `0x004D2A00` | `0x004D3A60` |
| Junction | `0x004E2DC0` | `0x004DA9B0` | `0x004E04F0` |
| Save     | `0x004E6740` | `0x004E3090` | `0x004E5550` |
| Ability  | `0x004E76D0` | `0x004E77A0` | `0x004E8550` |
| Config   | `0x004EDD30` | `0x004EDE90` | `0x004EE750` |
| Card     | `0x004EF020` | `0x004EF6F0` | `0x004EF750` |
| Magic    | `0x004F00D0` | `0x004F02F0` | `0x004F67C0` |
| Item     | `0x004F8010` | `0x004F81F0` | `0x004FC990` |

**Most of the mod still addresses modules by fixed pool slot** — the
`pMenuStateA + 0x1A6` ("mode 1") and `pMenuStateA + 0x21E` ("mode 2/6") aliases.
Those are slot 1 and slot 2 of the pool, and nothing guarantees a screen lands
there. Magic was moved to a pool walk in v0.22.x after it read a stranger's
bytes; Save's dialog reader is moved here. **Status, GF, Junction, Switch, Item
and the rest of Save still use the aliases.** SUSPECTED-HIGH: this is a latent
whole-screen failure, not a wrong sentence.

---

## 2. The shared confirmation window

One opener serves the entire menu:

```
0x004C2B10(body, id)  ->  0x004C2A20(body, opt1=0, opt2=0, id)
0x004C2A20:
    opt1 == 0  ->  0x004BD630(0, 0, 0x2F, 0)     default "Yes"
    opt2 == 0  ->  0x004BD630(0, 0, 0x30, 0)     default "No"
    [0x01D77300] = body      (stored at 0x004C2A5A)
    [0x01D772F0] = option 1  (0x004C2AA6)
    [0x01D772E0] = option 2  (0x004C2AAE)
```

The drawer `0x004C2950` renders all three from those globals; the cursor sprite
is placed by `0x004C2900`, which takes the cursor value as an argument and does
**not** store it — so there is no global cursor. Each module keeps its own.

`src/menu_dialog.inl` reads the three globals and composes
`"<question>. <the option under the cursor>"`. **The option words come off the
window**; v0.26.2 shipped a screen that assumed Yes-then-No and met the exam's
*"Really?"*, which lists NO first.

### Every call site in `.text`, and what it needs

`0x004C2B10` / `0x004C2A20` are called from **20 places**. The mod read one
before this build and reads three now.

| site | screen | entry state (`+0x10`) | cursor byte | read? |
|------|--------|----------------------|-------------|-------|
| `0x004CC39A` | Switch   | `0x10` | `+0x3F` | no |
| `0x004CE74C` | Status   | `0x09` | `+0x3B` | no |
| `0x004D0191` | Status   | `0x08` | `+0x3D` | no |
| `0x004D03C1` | Status   | `0x0D` | `+0x3D` | no |
| `0x004D0945` | Status   | `0x22` | `+0x3D` | no |
| `0x004D0D89` | Status   | `0x48` | none (message) | no |
| `0x004D3560` | GF       | `0x18` | `+0x57` | no |
| `0x004DC7D4` | Junction | `0x12` (sub `+0x16`=`0x13`) | see state 0x12 | no |
| `0x004DC808` | Junction | `0x12` (sub `+0x16`=`0x14`) | see state 0x12 | no |
| `0x004DDBE2` | Junction | `0x2C` | `+0x5C` | no |
| `0x004E3B26` | **Save** | `0x37` (phase `+0x48`=4) | `+0x4F` | **YES (v0.29.0)** |
| `0x004E4FD9` | **Save** | `0x29` (phase `+0x48`=8) | `+0x4F` | **YES (v0.29.0)** |
| `0x004E966F` | Ability  | `0x0D` | `+0x2F` | no |
| `0x004EAD94` | Ability  | `0x0B` | `+0x42` | no |
| `0x004F1C9C` | **Magic** | `0x6B` | `+0x70` | **YES (v0.29.0)** |
| `0x004F5A2C` | Magic    | `0x6D` | `+0x70` (cursor \|0x80 = hidden) | no |
| `0x004F5AF1` | Magic    | `0x6F` | `+0x70` (0x81 = hidden) | no |
| `0x004F84A0` | Item     | `0x18` | none (message) | no |
| `0x004F84E9` | Item     | `0x18` | none (message) | no |
| `0x004FB02F` | Item     | `0x4B` | `+0x66` | no |

The state numbers were checked for uniqueness inside each module. All are written
once except GF `0x18` (four writers — `0x004D356C`, `0x004D4FA9`, `0x004D51E3`,
`0x004D840B`; the last three are **returns to the same dialog poll after a
sub-step**, so they are not false entries) and Ability `0x0B`/`0x0D` (three each,
not yet classified).

**Why the other seventeen are not wired in v0.29.0.** The shared reader has never
run on live hardware. Pointing an unproven reader at sixteen more screens is the
shape of mistake this project has made before — v0.26.2's help-bar probe was green
throughout because it was built around the same wrong belief as the code. BAT the
Save prompt, then wire the rest from this table.

Highest harm among the unwired, in order:

1. **Junction "Off"** (`0x004DC7D4`, state `0x12`) — unjunctions everything.
2. **Junction "Keep previous setting"** (`0x004DDBE2`, `0x2C`, `+0x5C`).
3. **Switch junction exchange** (`0x004CC39A`, `0x10`, `+0x3F`).
4. **GF "Don't learn anything?"** (`0x004D3560`, `0x18`, `+0x57`).
5. Status ×4, Ability ×2, Item ×3, Magic ×2.

---

## 3. Fixed in v0.29.0

| # | screen | defect | evidence |
|---|--------|--------|----------|
| 1 | Save | overwrite prompt entirely silent; its cursor defaults to **No**, so Confirm-in-the-dark abandons the save | `0x004E3B0E` sets `+0x48`=4, `+0x4F`=1; `0x004E4C1E` maps cursor 0 → state `0x38` (write), else `0x20` (abandon) |
| 2 | Save | the unformatted-folder prompt was silent too | `0x004E4FD9`, phase 8, state `0x29` |
| 3 | Magic | the All warning repeated every frame, with wording that was wrong twice over | state 107 at `0x004F1CB6` is a steady 2-option poll; 106/107 shared with Exchange (`0x004F47B8`) and Split |
| 4 | Ability | `ABIL_MENU_ID_LO` 97 → 92 | engine guard `0x004C2B40` accepts `[0x5C, 0x74)` |
| 5 | Ability | list read from the 11 drawn GCW rows while the cursor indexes the whole list | engine flat list `0x01D8CB54`, count `0x01D8CB6C`, read at `0x004E770F` |
| 6 | Status | Zell's Duel page off by one | `mov ebx, 1` `0x004CE968` + `mov [esp+0x30], ebx` `0x004CE9AD`, against Squall's `mov dword [esp+0x30], 4` `0x004CE92C`; help bias `0x004CE4BD` vs `0x004CE536`; cell 0 is Duel-Auto `0x004CE3BB` |
| 7 | Item | cancelling an arrange announced "Swapped", on both flows | both flows return to the same state either way; decision moved to the item id at the armed source slot |

---

## 4. Open findings, by screen

Marked **CONFIRMED** where the disassembly was read; SUSPECTED otherwise, with
what would settle it.

### Junction
* **CONFIRMED** — the "Off" flow (states 12–18) is entirely silent, **including
  its confirmation**, which unjunctions everything.
* **CONFIRMED** — "Keep previous setting" (states 43/44, dialog at `0x004DDBE2`)
  is silent.
* SUSPECTED — the magic preview reports only the cursor row's category. Settle by
  reading the preview builder's row loop.
* SUSPECTED — state 24 removes a junction without moving the cursor, so nothing
  re-announces and the player is not told the slot emptied.

### Ability
* **CONFIRMED** — the GF picker (state 10) is silent.
* **CONFIRMED** — the modal error (states 16/17) is silent.
* SUSPECTED — dimmed (unaffordable / already-learned) rows are read the same as
  live ones. Settle by finding the per-row draw colour.

### Item
* **CONFIRMED** — GF-target mode is read as if the targets were party members.
* **CONFIRMED** — the message popups (states 22/23, `0x004F84A0` / `0x004F84E9`)
  are silent.
* **CONFIRMED** — the all-party target mode (states 15/16) is silent.

### Switch
* **CONFIRMED** — the pick-up and the junction-exchange dialog are silent.
* SUSPECTED-HIGH — `ForcedPselMenuFlag` reads `pMenuStateA + 0x1DB + id` on a
  hypothesis the source comments admit is unverified ("HYPOTHESIS — the .41 BAT
  confirms by ear"), and the reported symptom is Quistis reading as
  *"unavailable"* permanently. Settle by finding what the Switch module's draw
  function actually tests when it greys a row.

### Magic
* SUSPECTED — the junctioned marker on a spell is not read.

### Status
* SUSPECTED — page 3 is not announced on entry.
* SUSPECTED-HIGH — the comp buffer address may be wrong (`0x01CFF000` where the
  rest of the mod uses `0x01D7D7C0`).
* SUSPECTED — a renamed character produces silence.
* CONFIRMED-BY-INSPECTION — Irvine, Selphie, Seifer and the dream cast have no
  limit-page entry at all (`ST_LIMIT_CHARS` has four rows).

### GF
* **CONFIRMED** — "Don't learn anything?" (`0x004D3560`, state `0x18`,
  cursor `+0x57`) is silent.

### Save
* SUSPECTED — block emptiness is answered from the mod's own cache rather than
  from the engine's table at `0x01D8CB30`.
* SUSPECTED — the slot screen's horizontal cursor is not read.
* **CONFIRMED** — the format prompt was silent (fixed) ; the progress messages
  still are.

---

## 5. Method note

Two of these were found only because the *engine* was read rather than the mod's
own log. The Zell off-by-one and the ability paging both produced perfectly
well-formed, confident, wrong sentences — a log cannot show that, and neither can
a probe built from the same belief as the code. **The disassembly is the only
thing in this loop that is not downstream of my own assumptions.**

---

## 6. What the v0.29.0 BAT settled (2026-08-17)

**Confirmed fixed on hardware:**

* the Save overwrite prompt speaks, once per cursor move, with both labels read
  off the window and the default correctly announced as No;
* Zell's Duel page indexes correctly (cursor 1 → Punch Rush … 5 → Burning Rave),
  checked against the screenshot rather than the log;
* the Magic All transfer announces.

**Not exercised, so still unproven:** the Magic `MP_ALL_WARN` window (the receiver
was never full), and the Ability list past eleven rows.

**Found by the BAT, fixed in v0.29.1:**

* the v0.29.0 item-rearrange arm was dead — it sat in a block that runs after
  `s_rearrangePrevFocus` is updated, so every rearrange said "Cancelled";
* the GF Learn help slice swallowed the top of the ability list, because the game
  **drops "-J" in front of a multiplier** ("Elem-Defx2", not "Elem-Def-Jx2") and
  the parse stopped where it could not match.

**Found by the BAT, still open:**

* **Zell's Duel moves are listed with the button inputs that perform them** —
  Punch Rush is Down then X, Burning Rave is a five-step string — and the mod
  never says them. The glyphs are sprites and do not appear in the GCW buffer at
  all, so this needs the exe's Duel input table. On a page whose whole purpose is
  telling you how to perform the move, this is the largest single gap the audit
  turned up.
* Dimmed (not-yet-available) ability rows read exactly like available ones —
  visible in both GF screenshots (Cerberus: Mag-J/Spr-J/Spd-J/Alert; Leviathan:
  Recover/Spr+20%/Auto-Potion/SumMag+10%).
* `[MenuGCW]` shows the GF Learn list is **paged** ("ABILITY P.1" plus a scroll
  arrow). Nothing in this BAT crossed a page boundary, so the list reader has the
  same untested paging exposure the Ability list had.

---

## 7. The GF Learn list (v0.29.2)

Aaron: *"It did not seem to consistently announce abilities as I moved through
the list on later GFs like Leviathan, Cerberus, and Pandemona."*

`0x004D35ED` is the whole answer:

```
mov   al,  byte [esi+0x36]              ; page
movsx ecx, byte [eax+esi+0x39]          ; row = cursor[page]
lea   edx, [eax+eax*4]                  ; page*5
lea   eax, [eax+edx*2]                  ; page*11
add   ecx, eax                          ; ABSOLUTE = page*11 + row
mov   eax, [0x01D7DAA0]
cmp   ecx, eax
jge   no_ability                        ; 0x004D361D writes 0 into the help slot
mov   al,  byte [ecx*8 + 0x01D7D9F0]
```

| field | meaning | evidence |
|---|---|---|
| `+0x36` | page, 0 or 1 | `sete al` at `0x004D2C1F` |
| `+0x37` | page shadow | `0x004D33D8` |
| `+0x39[page]` | cursor row on that page | `0x004D35F0`, `0x004D2C2B` |
| `+0x3B[page]` | rows on that page | clamp at `0x004D2C2F` |
| 11 | rows per page | `idiv 0xB` at `0x004D33DC` |
| `0x01D7D9F0` | the whole list, stride 8, byte 0 = ability id | built at `0x004D2F13` |
| `0x01D7DAA0` | its length (dword) | `0x004D2F24` |

The mod read `pMenuStateA+0x257` / `+0x258` — module `+0x39` and `+0x3A`, i.e.
`cursor[0]` and `cursor[1]` — and used whichever had changed, indexing a list it
had parsed out of the GCW draw buffer. **The bytes were right and everything
around them was guessed**: no page byte, and a list that only ever holds the page
on screen.

**Cursor conventions differ between screens, and that is the lesson.** The
Ability screen composes its absolute index and stores it —
`mov dl,0xB / imul dl / add al,bl / mov [esi+0x3a],al` at `0x004E7975` — so
reading `+0x258` there was correct. GF keeps a cursor per page and composes on
demand. One guess worked, one did not, and neither screen could have told you
about the other. **Any remaining screen whose cursor was found by memory scan
rather than read out of its state machine is unverified by construction** — that
is the whole Status/GF/Save/Junction/Switch fixed-slot family in section 1.

---

## 8. The Item use-target list (v0.30.0)

Aaron: *"The list of characters / party members / GFs doesn't always seem to be
accurate. Most of the time it is, but sometimes not."*

`0x004F8600..0x004F86BF` builds a **32-bit target mask** into `+0x38`:

| half | source | meaning |
|---|---|---|
| low 16 | `0x004AD030` | bit *i* = character *i* (0..7); savemap `+0x94` odd at `0x01CFE17C` stride `0x98`; narrowed to the three formation ids at `0x01CFE74C` when `[0x01CFE97A] & 1` |
| high 16 | `0x004AD090` | bit *16+j* = GF *j* (0..15); savemap `+0x11` odd at `0x01CFDCB9` stride `0x44` |

| field | meaning |
|---|---|
| `+0x38` | the mask |
| `+0x58` | **the bit index under the cursor** (i16) |
| `+0x62` | the confirmed slot, `0xFF` when the bit is clear |
| `+0x64` | 0 = characters (one column), 1 = GFs (two columns) |

**The screen leaves gaps.** With characters it draws row `cur` at
`y = cur*13 + 0x42` (`0x004F8886`); with GFs at `row = cur/2, col = cur&1`
(`0x004F889C`). Position comes from the slot, never from a packed index.

The mod sorted a roster into a packed list and indexed it with the cursor. **That
is right exactly while the set bits run 0,1,2,… with no gaps** — Aaron's current
party — and shifts every name below the first gap otherwise. GFs were not handled
at all.

### The pattern this is the fourth instance of

| screen | what was guessed | what the engine does |
|---|---|---|
| Ability | list from the drawn rows | flat list `0x01D8CB54`, absolute cursor composed at `0x004E7975` |
| GF Learn | two unrelated cursor bytes | `cursor[page]`, absolute = `page*11 + row` (`0x004D35ED`) |
| Item target | packed, sorted roster | a bit mask; the cursor is the bit index (`0x004F8600`) |
| Save dialog | fixed pool slot | module found by update fn |

Every one was found with SUBMON (now `src/menu_submon.inl`), and every one was a
byte that **correlated**. Remaining unverified by the same construction: the
Switch screen's availability flag (`pMenuStateA+0x1DB`, whose own comment says
"HYPOTHESIS"), and the Status/Junction/Switch fixed-slot module addressing in
section 1.

---

## 9. Why the Item module's pool walk fails (settled, v0.31.0 BAT)

Four builds addressed the Item screen by a pool walk that returned nothing, and
each time the state-verified `pMenuStateA + 0x21E` alias answered instead. The
one-shot pool dump added in v0.30.2 fired on the first alias hit and settles it:

```
pool dump: head=01D76CB8 base=01D76BC8 end=01D77078
  slot 0 @01D76BC8 inUse=1 state=1  upd=0x004FDB60 next=01D76AC8 prev=01D76C40
  slot 1 @01D76C40 inUse=1 state=7  upd=0x004C0CF0 next=01D76BC8 prev=01D76CB8
  slot 2 @01D76CB8 inUse=1 state=81 upd=0x605D8200 next=01D76C40 prev=01D76B48
  slot 3..9 free
```

The list itself is intact — head → slot 2 → slot 1 → slot 0 → `0x01D76AC8`, the
tail sentinel, which is out of pool range and correctly ends the walk. Slots 0
and 1 carry sane update functions. **Slot 2 is the Item module — its state word
reads 81 (`0x51`), the magazine opening state, exactly as the reader expects —
and the dword where its update function should sit reads `0x605D8200`, which is
not a code address at all.**

So `FindItemModule()` cannot match it, and no amount of hardening the walk will
change that. **For this screen, identity-by-update-function is not a stricter
test than the slot alias; it is a test that cannot pass.** The alias with the
state check is the correct identification here, not a fallback:

* the walk is still tried first, and still wins on every screen where `+0x08`
  holds what the allocator put there (Magic, GF, Save, the magazine viewer);
* the alias is accepted only when the module's own state word agrees with the
  state the caller is in, so it is evidence, not an assumption.

What overwrites `+0x08` is not yet established — the Item update function
(`0x004F81F0..0x004FC990`) contains no write to `[reg+0x08]`, so it happens
elsewhere. It does not need to be established for the reader to be correct, and
it is recorded here rather than guessed at.

**The general lesson for the rest of this audit:** "find the module by its update
function" was adopted in v0.22.x as the safe alternative to fixed pool slots, and
it is — but it is not universal. An identification is only as good as the field
it reads, and the right shape is *two* identifications that must agree with
something the caller already knows.
