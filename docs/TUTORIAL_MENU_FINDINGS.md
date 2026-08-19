# Tutorial menu and SeeD written exam — findings (#85, v0.26.0)

Everything here was read out of `FF8_EN.exe` and `menu.fs`, and every address was
re-checked against the disassembly before it was written into the mod. Where
something could not be established it says so; the mod speaks nothing it could
not prove.

---

## 1. Two modules, not one

| | Tutorial | SeeD exam |
|---|---|---|
| main-menu dispatch | 20 | 23 (pushed, never chosen from the menu) |
| creator | `0x004C9B70` | `0x004D4960` |
| update | `0x004C9CB0` | `0x004D4D30` |
| draw | `0x004CAE10` | `0x004D58A0` |
| states / jump table | 34 / `0x004CAC0C` | 28 / `0x004D5828` |
| overlay | 13 `menututo.ovl` | 16 `menutest.ovl` |

Identification, two independent ways, the same discipline the Card and Config
screens needed:

* The main menu's own table at `0x00B87FE0` is **stride 2, 0xFF-terminated,
  eleven rows** of `{textEntry, actionByte}` — walked by `0x004BE970`. Row 9 is
  `{0x37, 0x14}`, and `0x14 & 0x1F = 20`.
* `0x00B87ED8` (stride 8, `{creator, overlayId}`) gives index 20 →
  `0x004C9B70`, overlay 13.

**Choosing TEST or Review does not change the Tutorial module's state.** State 17
calls `0x004BDB30(0x17, module)`, which pushes the exam module, and the Tutorial
module then parks in state 18 polling `0x004BDC30()` for the pop. So **both
modules are in the pool at the same time**, and a poll that finds the Tutorial
one first would announce a stale list on top of a live question. The mod walks
for the exam first, unconditionally.

### States the player can sit still in

Tutorial: **4** (the seven-row list) and **7** (the review picker). 6, 8, 10, 12,
13 and 15 are slides — and **9 and 11 read Left/Right mid-slide** to queue
another page flip, exactly like the Card album's states 7 and 9. Reading input
is not the test.

Exam: **21** (the answer input, `0x004D5534`) and the message windows **5, 8, 11,
13, 16, 23**. States 5, 8 and 23 literally share one handler, `0x004D4F43`.
**19 and 25 are slides that read nothing.**

---

## 2. Where the text lives

`0x004BD630(a, group, entry, sub)`:

```
base = a ? [0x00B86D30] + 0x2E000     // the current overlay's text file
         : [0x01D2BB78]               // the always-loaded common file
bank = base + u16[base + 2 + group*2]
str  = bank + u16[bank + 2 + (entry*2 + sub)*2]
```

`entry*2 + sub` is why every bank looks like it has twice as many strings as it
has entries: **sub 0 is the title, sub 1 is the description.**

The overlay id maps to an `mngrp` section through `0x004AC200`'s jump table at
`0x004AC340`; `menututo` → section 2, which is byte-identical to `tkmnmes3.bin`.

| what | where |
|---|---|
| Tutorial list titles, rows with a non-zero kind byte | section 2, bank 13 |
| Tutorial list titles, rows with kind 0 | section 0, bank 0, via `0x004C25B0` (sub 1) / `0x004C25D0` (sub 0) |
| Information / tutorial page bodies | sections 128–133, via `menutips.ovl` (`section = 0x80 + topic`) |
| **SeeD exam questions** | **sections 96–125, one per test level** |
| exam framing dialogs | section 95 |

### The seven rows (`0x00B88340`, stride 4, 0xFF-terminated)

| row | textId | kind | title | description |
|---|---|---|---|---|
| 0 | 0x02 | 1 | Battle Operation | Battle Explanation |
| 1 | 0x04 | 1 | Online Help | Explanation of Various Features |
| 2 | 0x03 | 1 | Card Game Rules | Card Game Explanation |
| 3 | 0x3D | 0 | TEST | Take Written Test to raise SeeD rank |
| 4 | 0x3E | 0 | Review | Review SeeD Written Test |
| 5 | 0x22 | 1 | Icon Explanation | Explanation About Icons |
| 6 | 0x3C | 0 | Information | FF8 Info Corner |

Two rows are gated **silently** — confirming them just beeps:

* **TEST** needs `0x004C3090() >= 0`.
* **Review** needs `[0x01CFE98B] != 0`.

The mod says which, because a screen that answers a keypress with nothing is
indistinguishable from a hang.

---

## 3. The exam

### Question data

The creator (`0x004D4A47`) loads `mngrp` section `0x60 + testIndex` to
`[0x00B86D30] + 0x1F000` when `testIndex < 30`. So **level 1 = section 96 …
level 30 = section 125.**

Layout, from `0x004D5475`:

```
base = [0x00B86D30] + 0x1F000
n    = u16[base]                          // always 10
o    = u16[base + 2 + questionIndex*2]
[0x01D7EC40] = base[o]                    // THE ANSWER KEY
text         = base + o + 1               // one byte later
```

**The byte at the offset is the correct answer, and the text starts one past
it.** Get the `+1` wrong and every question reads with a stray glyph in front.
`tests/menu_tutorial_compile.cpp` asserts it explicitly.

Section 126 is a complete, unreachable 31st question set — `0x004D4D79` refuses
any index ≥ 30.

### Answering and scoring

* Cursor: `0x004C0A80(repeatWord, [0x01D7EAB8], module+0x2F)` — horizontal,
  wrapping, **no Cancel**; once the questions start you cannot back out.
* Confirm is edge bit `0x0040`. `0x004D55BF`: `if (cursor == [0x01D7EC40]) module[+0x26]++`.
* `0x004D55E4` compares the score against **10**. All ten must be right.
* Choice 0 = YES, choice 1 = NO. Across the 300 live questions: 134 YES, 166 NO.

### Persistence

| address | savemap | meaning |
|---|---|---|
| `0x01CFE98B` | +0x0D2F | **tests passed, 0..30 — the only record.** There is no per-test bitmap; tests 1..N are passed and N+1 is next |
| `0x01CFE9C8` | +0x0D6C | SeeD rank points |
| `0x01CFE97A` | +0x0D1E | bit 0 set ⇒ not a SeeD |
| `0x01D2BA96` | — | a second gate on the rank getter |

`0x004C3090`: both gates → −1, else `clamp(points, 100, 3100) / 100`, so rank
runs 1..31 and **1 is the floor, not 0**. The mod reproduces the clamp rather
than dividing the raw points, so it cannot disagree with the salary screen.

### The drawn text (v0.26.1 correction)

**`module+0x20` is the FOOTER HINT**, the "X to quit" line along the bottom of
the window — not the message. v0.26.0 read it and every message window in the
exam announced "the Confirm button to quit".

The text the game draws is the **pre-processed buffer at `0x01D7DAB8`**.
`0x004D4A80(src, 0x01D7DAB8, 0x01D7EB40)` expands the chosen string into it —
`{03}` names, `{0C}` GF names and `{0A}` numbers all substituted — and
NUL-terminates it at `0x004D4CF5`; the draw fn renders from there
(`0x004D596C`). The score in "Your score was 80" is a `{0A}` variable and
**exists nowhere else**, so the stored string would not have been enough either.

`0x004D4A80` does **not** copy the `0x0B` answer markers into that buffer. It
diverts them to the position array at `0x01D7EB40` — 8 bytes each,
`{u16 x, u16 y, u16 slot}` — and copies the label letters (`YES     NO`, `END`)
straight through as ordinary words. The pen advances exactly `0x10` per line
break (`0x004D4CD3`), so:

```
labelLine = u16[0x01D7EB40 + 2] / 0x10
```

and everything from that line down is a label rather than a sentence. The return
value of `0x004D4A80` is the choice count, which also lands in `0x01D7EAB8`.

**The labels in that region are not throwaway.** They are the ANSWER WORDS, and
they are not always YES then NO:

| section 95 string | answer line |
|---|---|
| 0, 1, 6 (offers) | `YES     NO` |
| **7 ("Really?")** | **`NO      YES`** |
| 2, 3, 4, 8, 9, 10 | `END` |
| 5 (level 30 gate) | `GO BACK` |

So the mod splits the cut region on a **run** of two or more spaces — a single
space is part of the label, which is what keeps `GO BACK` one answer — and names
the cursor position from that rather than from a fixed Yes/No. Getting this wrong
would have named the opposite answer on the one screen that exists to
double-check the player.

### One glyph outside the table

`0xB5` is the only byte above `0xAF` anywhere in sections 95–126. It occurs three
times — section 95 string 6, section 98 string 5, section 100 string 4 — and every
one sits where a pause belongs ("won't go any higher⟨B5⟩ will you still take the
written test?"). It is **not** the `0x3C` comma and the shape is not established,
but dropping it runs two clauses together in all three, so the mod speaks a comma.

### A typo that is not ours

The pass screen reads *"Your scored 100%."* That is FF8's own text — mngrp
section 95 string 2, verbatim.

### Module fields

`+0x10` state · `+0x20` **the footer hint** · `+0x26` score ·
`+0x2A` scroll · `+0x2C` review mode · `+0x2D` test index · `+0x2E` question
index · `+0x2F` answer cursor.

Tutorial module: `+0x10` state · `+0x32` review-picker index (flat, 10 rows a
page) · `+0x34` list cursor.

---

## 4. Control codes

| code | param | meaning |
|---|---|---|
| `00` | — | end of string |
| `01` | — | end of page |
| `02` | — | **line break — a WRAP, not a sentence end** |
| `03` | 1 | character name, `0x30 + id` |
| `05` | 1 | button or icon sprite |
| `06` | 1 | text colour |
| `0A` | 1 | number variable, or a progress-flag visibility condition |
| `0B` | 1 | a selectable line; in the exam, answer slot `param − 0x20` |
| `0C` | 1 | GF name, `0x60 + id` |

Two of these caused real defects that only appeared against real data:

* **`02` as a full stop was wrong.** It read fine on a fixture and split the
  game's own sentences in half the moment it met the corpus — test 4 question 9
  came out *"Squall's gunblade causes more damage. by pressing the first Escape
  button at the right time."* A space is right in both cases.
* **Everything after the first `0B` is answer LABELS, not the question.** Every
  stored question ends `…\n\n  <slot0>YES     <slot1>NO`, so emitting past that
  point made all three hundred read *"…the Gauntlet. YES NO"* before the mod then
  said *"Answer Yes"*.

Also: each question's stored text **opens with its own "Question N"**, so the
mod's position label strips it rather than saying the number twice.

---

## 5. The symbols

`0x0049F930(param)`:

| range | behaviour |
|---|---|
| `0x20–0x2F` | a **game function**; sprite = `0x80 + 0x004A2DF0(param − 0x20)`, i.e. **remapped through the player's own button map** |
| `0x30–0x3F` | a **fixed physical button**, no remap |
| `0x40+` | an inline icon, sprite = `u16[0x00B86D84 + param*2]` |

**29 of the 300 live questions contain one.**

### The ability icons — named, and the names are the game's own

`mngrp` section 89 string 54 is the Icon Explanation page, and it labels each
sprite in text. Every one is independently confirmed by the stored answer key of
the question it appears in.

| bytes | name |
|---|---|
| `05 45` | Junction Ability icon |
| `05 46` | Command Ability icon |
| `05 48` | Character Ability icon |
| `05 49` | Party Ability icon |
| `05 4A` | GF Ability icon |
| `05 4B` | Menu Ability icon |
| `05 43` | the "damage absorbed" marker (section 166 states its meaning) |

**Naming them is a deliberate choice with a cost.** Six questions read
"⟨icon⟩ signifies Junction Ability", so speaking the true name turns a
recognition test into a string comparison. The alternative is worse: a test is
passed only by answering **all ten** correctly, so an unnamed icon does not make
those six tests harder — it makes them **unpassable except by luck.** Two of the
six are traps whose correct answer is No, and the mod names them truthfully.

### Buttons — spoken as what they do

The names are the game's own function names, from the Config Customize row table
`0x00B88A10` and its battle-page labels, which is why they match what
`menu_config_model.inl` already says. The sprite is chosen through the player's
map, so a fixed shape name would be wrong exactly as often as the player has
remapped anything.

`05 20` first Escape · `05 21` second Escape · `05 22` Change Select Window ·
`05 23` Trigger · `05 24` Cancel · `05 25` Change Character · `05 26` Confirm ·
`05 27` View Status.

---

## 6. Not established

These are deliberately unresolved, and the mod says nothing about them:

* **Which physical shape** each of the button bits is (L1/R1/L2/R2 ordering). The
  glyphs live in `icon.sp1`; the exe holds no shape names at all.
* **`05 38` — Start or Select.** It is one of the two buttons the Customize
  screen refuses to rebind, and which one is an inference from a single help
  string. Its one question ("Press ⟨x⟩ to hide battle commands temporarily",
  answer YES) reads correctly without the name, so the mod speaks "a button".
* **`05 47`** (sprite `0xDA`) is labelled nowhere in the game's text. It occurs
  in no exam question.
* **Colour indices.** The arithmetic is proven; the palette entries have no names
  in the exe, so they are dropped rather than described.
* **Character names other than ids 0 and 4.** `0x0047EB50` answers those two
  directly and sends everything else through a party-slot indirection at
  `0x01CF75EC`. No tutorial or exam string uses another id.

---

## 6a. The three magazine rows (v0.27.0 — SHIPPED)

**Battle Operation, Card Game Rules and Icon Explanation are ONE module.** The
three creators (`0x004C8FF0`, `0x004C9820`, `0x004C9890`) all make the same call,
`0x004BE540(0x004C9060, 0x004C9330)`, and differ only in what the Tutorial wrote
into `0x01D7D3A5/A6` first. Record ranges come from `mtmag.bin` (12 bytes, three
`{first, last}` pairs):

| topic | dispatch | records | pages | mngrp section |
|---|---|---|---|---|
| Battle Operation | 25 | 43–50 | 8 | 88 |
| Card Game Rules | 26 | 51–63 | 13 | 89 |
| Icon Explanation | 31 | 64–67 | 4 | 89 |

Update `0x004C9060`, draw `0x004C9330`, 17 states, jump table `0x004C92E8`.
**Steady state 9 only, and no slide state samples input** — unlike the Card
album. No cursor, nothing selectable. Left/Right turn pages, Confirm turns and
then *leaves* past the last page, Cancel leaves.

Fields: `+0x10` state · `+0x28` the record **on screen** (not `+0x2C`, which is
the one being moved to).

Text, per page:
```
rec  = [0x01D2BAF8] + 68 * module[+0x28]        // mmag.bin, 69 records
for i in 0..3:
    idx = u8[rec + 0x34 + 4*i + 3]              // 0xFF terminates the list
    str = 0x01D773A4 + u16[0x01D773A6 + idx*2]  // raw, NUL-terminated
```
**No pre-processing buffer.** The draw fn renders the stored bytes as-is, so no
name or number substitution happens and the mod does its own.

Records 0–42 are the field magazines (Weapons Monthly, Pet Pals, Occult Fan,
Chocobo World) sharing the same viewer. The Tutorial sets `[0x01D7D3A4] = 0xFF`
before dispatching; gate on the record range if that ever matters.

## 6b. Online Help (v0.27.0 — the LIST is shipped, the demos are not)

**Not a module.** Row 1's action byte `0xFF` reaches a handler that sets state
**24** inside the Tutorial module, so the list is a second panel next to the
seven-row one. Steady state **27**.

Fields: `+0x35` cursor · `+0x36` visible row count · `+0x39..+0x41` row →
descriptor index · `+0x24` pointer to the highlighted row's description.

Descriptor table `0x00B88360`, **9 entries of 12 bytes**:

| byte | meaning |
|---|---|
| `[0]` | bank-13 entry: sub 0 title, sub 1 description |
| `[1]` | dispatch index of the demo module to push |
| `[2]` | sub-mode byte, handed to the demo through the parent |
| `[3]`–`[6]` | mngrp sections the demo loads |
| `[8]` | progress-flag id — **the row exists only if `0x004AD1D0(flag)`** |

| # | title | pushes |
|---|---|---|
| 0 | GF Junction | 18 |
| 1 | Magic Junction | 18 |
| 2 | Junction to Elements | 18 |
| 3 | Junction of Status | 18 |
| 4 | GF Tutorial | 24 |
| 5 | ⟨character 0⟩'s Status Screen | 28 |
| 6 | Zell's Status Screen | 28 |
| 7 | ⟨character 4⟩'s Status Screen | 28 |
| 8 | Switch | 29 |

The nine demos are sandboxes — state 28 snapshots the savemap and
`0x004CADA0` restores it on return, so nothing they do is real. **They are not
spoken yet**, and the list says so on arrival rather than letting the player
press Confirm into silence.

## 7. Information — the nested page browser (v0.28.0 — SHIPPED)

Dispatch 21, creator `0x004D5E00`, update `0x004D5F10`, draw `0x004D6EE0`,
22 states, jump table `0x004D6A5C`, overlay 15 `menutips.ovl`.
**Steady state 7 only**, and no slide state samples input.

**425 records**, sections 128–133, loaded as `section = 0x80 + topic`
(`0x004D5F94`). Global index = mngrp section 127 at `0x01D82E48`: `u16 count`,
then `{u16 offset, u16 topic}` per record at `0x01D82E4C + 4*r`. A record is
`u16 parent, u16 prevPage, u16 nextPage, u16 size`, then a NUL-terminated title,
then a NUL-terminated body; the next record starts at `align4(start + size)`.
Global numbering: §128 0–87, §129 88–170, §130 171–237, §131 238–306,
§132 307–350, §133 351–424. Record 0 is the only one with no parent.

### Runtime globals — read these, do not re-derive

| address | meaning |
|---|---|
| module `+0x28` | u16, the current record id |
| `0x01D84E50` | the title, `strcpy`'d verbatim |
| `0x01D7EC48` | **the expanded body** — numbers substituted, NUL-terminated |
| `0x01D85658` | link position array, 8 bytes each: `{u16 penX, u16 penY, u16 target}` |
| `0x01D85650` | u16 link count |
| `0x01D83E4C` | **u8 per record id — the cursor, saved PER RECORD by the game** |
| `0x01D84E4C` | u16 parent, `0xFFFF` at the root |
| `0x01D8575A` / `0x01D85758` | u16 prev / next page, `0xFFFF` = none |

### Why the links can be enumerated exactly

`0x004D6B20(src, 0x01D7EC48, 0x01D85658)` **does not copy the `0x0B` markers**
into the body. It diverts each into the position array and copies the link's
*label* through as ordinary text — the same shape as the exam's answer labels.
The pen advances `0x10` per line break, so:

```
linkLine = u16[0x01D85658 + 8*i + 2] / 0x10
```

**Verified against all 425 records: no page puts two links on one line.** So a
link's line *is* its label, with no inference — which is exactly what the
magazine pages could not offer, and why the "short line = list item" heuristic
had to be reverted there in v0.27.0.

Links inside a `{0A}`-hidden region are not emitted at all, so link indices are
dynamic. Never precompute them from the file.

### Input, state 7

Confirm follows the selected link · Cancel climbs to the parent, or leaves at the
root · Left / Right move between sibling pages when they exist · one further
button pops the history stack, and **the game names it in no footer, no help line
and nowhere else in the menu.**

### Corpus check

Rendering all 425 offline: 49 link pages, 376 prose pages, 0 empty titles,
0 unlabelled links, 0 overflows, 0 truncated labels.

---

## 8. Still to do — the nine Online Help demos

Descriptor `[1]` of each entry at `0x00B88360` names the module each one pushes
(18, 24, 28, 29). They are sandboxed replicas of the Junction, GF, Status and
Switch screens — state 28 snapshots the savemap and `0x004CADA0` restores it on
return, so nothing they do is real. The Online Help list announces them as
undescribed rather than letting the player press Confirm into silence.
