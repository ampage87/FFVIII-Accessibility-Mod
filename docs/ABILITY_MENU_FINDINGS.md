# The Ability submenu, read out of FF8_EN.exe (#91, v0.32.0)

Aaron: *"I want to ensure that all of the various abilities are accessible when
used with the mod. I don't have access to all of the abilities in-game and it
isn't realistic to BAT each and every one. Let's make sure the best we can though
from the game exe and the code."*

---

## 1. There are twenty-four menu abilities, and they do not all go to one screen

The engine's own guard (`0x004C2B40`) accepts ability ids in `[0x5C, 0x74)` —
**92..115** — and the list builder at `0x004E770F` emits `id = 92 + bitIndex` for
all 24 bits.

Picking a row runs `0x004E7990`, which looks the ability up in the descriptor
table at `0x01CF7F28 + id*8` and branches on the **type byte at `+5`**:

| type | what happens | module |
|---|---|---|
| `0xFF` | nothing at all — the row is not selectable | — |
| `0x81` | `[esi+0x3C] = 0x0C`, state `0x12` → a **modal message** ("can't use that here") | dispatch 12, update fn `0x004EA890` |
| `0x80` | the **shop**. `[0x01D8CB6D]` decides: zero opens it (state `0x10`), non-zero refuses with a beep (state 8) | the shop module |
| anything else | `[esi+0x3D] = ability id`, `[esi+0x3C] = 0x13`, state `0x17` → push dispatch 19: the **refine screen** | creator `0x004D7180`, update `0x004D7410`, draw `0x004D90E0` |

**The refine screen is a module of its own that the Ability screen pushes.**
`menu_tts_ability.inl` has always been driving it through pool slot 3 without
that being written down anywhere.

### The 24, grouped by where they go

| ids | abilities | destination |
|---|---|---|
| 92, 93, 94 | Haggle, Sell-High, Familiar | passive — they change shop prices/stock, they are not selectable actions |
| 95, 96 | Call Shop, Junk Shop | **the shop** |
| 97..113 | T/I/F/L/Time/ST/Supt/Forbid Mag-RF, Recov/ST/Forbid/GFRecov/GFAbl Med-RF, Ammo-RF, Tool-RF, Mid/High Mag-RF | the refine screen, source list = **items** |
| 114 | Med LV Up | the refine screen, source list = **items** |
| 115 | Card Mod | the refine screen, source list = **cards** |

---

## 2. The refine module's fields, from its own state machine

| field | meaning | evidence |
|---|---|---|
| `+0x10` | state, 0..`0x2C` | `0x004D7432` |
| `+0x45` | sub-mode, 0..4 | jump table `0x004D8B40` |
| `+0x48` | the chosen character id | `0x004D7589`, resolved through `0x004AD030` / `0x004ABC40` |
| `+0x49` | the **source-list cursor — absolute**; the drawn row is `cursor % 11` | `0x004D75A7` |
| `+0x4A` | the character-picker cursor, packed over the available characters | `0x004D7562` |

Those are exactly the bytes this mod reads as `pMenuStateA + 0x2DE / +0x2DF /
+0x2E0` — pool slot 3 plus `0x48/0x49/0x4A`. **The offsets were right; the reason
was never recorded.** That is precisely how the GF screen's "two cursors" stayed
wrong for eight builds: a correct-looking number with no derivation behind it.

The cursor being **absolute** (not per-page) is why indexing the savemap
inventory by it works — the same shape as the Ability list, and the opposite of
the GF Learn list.

---

## 3. What is covered, and what is not

**Covered and now identified properly (v0.32.0):**

* the ability list itself — the id range and the engine list were fixed in
  v0.29.0;
* the refine source list, the "Refinable / Cannot be refined" tag, the character
  picker and the quantity step;
* the refine module is now found by **walk first, state-checked slot-3 alias
  second**, the same two-identifications-must-agree shape the Item screen needed.

**Not covered — named rather than left implied:**

| gap | why it matters |
|---|---|
| ~~**Card Mod's rows are cards**~~ | **Closed in v0.33.0 — see §6.** The creator at `0x004D7344` builds the list itself, so the rows name themselves. |
| **Call Shop / Junk Shop** | a different module entirely. The mod has no reader for the shop at all — buying, selling and the Junk Shop's weapon-remodel list are unread. |
| **The `0x81` modal** ("can't use that here") | silent. Already on the #88 list. |
| **The shop-refused beep** (`0x80` with `[0x01D8CB6D]` non-zero) | silent — the player presses Confirm and nothing is said. |

---

## 4. What Aaron can actually test

Read out of `slot2_save30.ff8` (28h01m, 164 saves) — the newest save available
here; his live game is a few hours further on:

| GF | menu ability |
|---|---|
| Quezacotl | **T Mag-RF** |
| Shiva | **I Mag-RF** |
| Siren | **L Mag-RF**, **Tool-RF** |
| Leviathan | **Supt Mag-RF** |

**Five of the twenty-four, and they cover two of the three refine output kinds:**
four magic-refines (item → magic) and **Tool-RF (item → item)**, which is the one
worth watching — the preview parser was written against "N will refine into M
⟨Magic⟩" and an item-output recipe phrases it differently.

Not yet obtainable in that save: Card Mod (Quezacotl learns it, not yet learned),
both shop abilities (Tonberry), and everything from Doomtrain/Bahamut/Cactuar/
Eden.

**Suggested BAT:** Ability → each of the five in turn. Walk the source list a few
rows on each, let the cursor settle so the Refinable tag fires, then drill into
one refine on **Tool-RF** specifically and take it through the character picker
and the quantity step. Grep `Refine item` — the line now carries the ability id
and the resolved module base.

---

## 5. The "/" preview and what the v0.32.0 BAT found (v0.32.1)

The BAT confirmed §1–§4 in the game: item names, ability ids (100 = L Mag-RF,
108 = Tool-RF), recipient, quantity and the Refinable tag all read correctly.
It also produced the failure §4 had predicted, verbatim from the log:

```
Refine info (/): "1 will refine into 30 Shell StonesCoral FragmentBetrayal SwordDead SpiritHeroForce Armlet"
Refine info (/): "100 will refine into 1 Dark MatterZombie PowderPet Pals Vol.2Girl Next DoorSoft..."
Refine info (/): "1 will refine into 30 Shell StonesForce ArmletNumber to r"
Refine info (/): "3 will refine into 1 Phoenix Pinion"        <- correct
```

**Why.** `ParseRefinePreview` anchored on `"will refine into"` and cut at the
first **party name**. That is not a property of the sentence — it is a property
of the *magic* screens, which draw the recipient panel immediately after the
preview. An item-output recipe has no recipient, draws no party panel, and so
no marker was ever found: the slice ran to the end of the buffer and took the
drawn source list with it.

This is the same failure mode as the four screens in `SUBMENU_AUDIT.md`: a value
that **correlated** with the right answer on every case tested, for a reason
nobody had written down, and stopped correlating the moment a different case
appeared.

**The fix, and why it does not depend on layout.** A refine result is always a
named thing — an item on an item-output recipe, a spell on a magic one. From the
position just past `"into <count> "`, `RefineResultEnd()` takes the **longest**
match across the item table (1..198) and the spell table, and ends the sentence
there, absorbing a trailing plural `s` ("30 Shell Stones", "10 Curagas"). Longest
match matters: a shorter name that is a prefix of the real result would cut the
sentence in half.

The layout bounds remain as the fallback for a result neither table knows, and a
second one was added for the item case: **the current page of drawn source rows**
— eleven from `(cursor/11)*11`, read out of the savemap inventory, which is
where the source list comes from in the first place.

**The probe could not have caught this.** `tests/menu_ability_compile.cpp`
declared `SAVEMAP_BASE = 0`, `ITEM_INVENTORY_OFFSET = 0` and a three-name spell
stub, so the inventory read mapped page zero and the result match had almost
nothing to match against — a broken cut would have passed. It now uses the real
savemap address, the real item and spell tables and the real party markers, with
the two failing BAT strings as verbatim fixtures, plus an unknown-result case so
the fallback path stays exercised. Same shape as the magazine probe that stubbed
`FindItemModule()` to null: **a probe that stubs the thing under test is not a
test, it is a compile.**

---

## 6. The refine screen has five flows, not one (v0.33.0)

The v0.32.1 BAT produced two screenshots and one root cause.

**`f11_151824_110.png` — Mid Mag-RF.** The screen's only panel is
Squall / Zell / Irvine / Quistis / Rinoa / Selphie with their HP. The log has the
mod reading the item inventory over it — "Potion, 34", "Phoenix Down, 3",
"Elixir, 6" — three lines after the game's own help text said
*"Refine Mid-Level Magic from other Magic."*

**`f11_151810_436.png` — Tool-RF's quantity popup.** "Force Armlet:1 will refine
into 30 Shell Stones", a source count, a "Number to refine", a result count.
Every word of it is in the GCW in the log. No `[MenuTTS]` line follows.

### The five sub-modes

`0x004D71D4` calls `0x004E7620(abilityId)` — `0x01CF7F28 + id*8` — and copies the
descriptor's byte `+5` into module `+0x45`. That byte is a jump-table index, and
three separate tables branch on it:

* `0x004D8DA8` — **where the source rows come from**, read by `0x004D8CC0`
* `0x004D8C04` — **what happens after the source is chosen** (state `0x14`)
* `0x004D8B7C` — **what is granted and consumed** (state `0x2A`)

| `+0x45` | source rows | result | after the source |
|---|---|---|---|
| 0 | item inventory (`0x01CFE79C`) | magic (`0x004C2D20`) | recipient picker |
| 1 | item inventory | item (`0x0047ED00`) | quantity popup |
| 2 | **a character's magic** — `0x01CFE0F8 + charId*152`, 32 slots | magic | quantity popup |
| 3 | item inventory | item | quantity popup |
| 4 | **the card list** — `0x01D8B064` | item | quantity popup |

Sub-mode 2 is the one that was wrong. `0x004D83C6` resolves the character from
cursor `+0x49` **before the source list exists**, and `0x004D8CDD` then points the
source at that character's record. The 32-slot count is the `cmp edi, 0x20` at
`0x004D7BEE`.

### Why one gate broke both screens

The mod gated the whole post-source flow on `+0x53 == 0`. The engine clears that
byte at state `0x1A` — the **first state of the recipient picker** — and only
sub-mode 0 ever enters `0x1A`. Tool-RF goes `0x14 → 0x28` straight to the popup;
Mid Mag-RF goes `0x14 → 0x1D` straight to the magic grid. On both, `+0x53` stayed
`0xFF`, so the gate never opened and the item-list branch ran over a screen that
was not an item list.

The flow now keys off the state machine, plus the engine's own popup flag `+0x51`
(set at `0x004D89F8`, cleared at `0x004D7946` on cancel and `0x004D7A29` on
confirm) so the popup cannot be missed while states animate.

### The quantity popup's numbers

Recipe entries live at module `+0x24`, stride 8, and state `0x2A` reads them at
`0x004D7A4C..0x004D7A74`:

| offset | meaning |
|---|---|
| `+0x00` u16 | result name string offset |
| `+0x02` u16 | **result count per unit** |
| `+0x04` u8 | required refine level (vs `+0x47`) |
| `+0x05` u8 | source id |
| `+0x06` u8 | **source count per unit** |
| `+0x07` u8 | result id |

So the popup's three rows are computable exactly, including for a recipe that
consumes more than one source per unit — the old reader multiplied a preview
string's numbers and assumed 1:1.

### Card Mod, closed

§3 listed Card Mod's row names as a gap because the list's order could not be
checked against any save Aaron has. It can be checked against the **creator**:
`0x004D7344` walks card ids `0..0x6D`, and for each card owned writes
`{cardId + 1, count}` into `0x01D8B064`. The row under the cursor therefore names
itself out of the existing 110-card table, with the `+1` undone.

### Still open

* **Call Shop / Junk Shop** (`+5 == 0x80`) — a different module, no reader.
* **The `0x81` modal** ("can't use that here") — silent.
* **The shop-refused beep** — silent.
* **Med LV Up (114)** — its sub-mode is not in Aaron's save, so which of the five
  it uses is inferred from the table, not observed.


---

## 7. What the v0.33.0 BAT settled (v0.33.1)

§6's two fixes are confirmed in the log: Mid Mag-RF reads its character picker
and then that character's magic ("Cure, 84", "Esuna, 4", "Protect, 94",
"Fire, 69"), and the quantity popup speaks on every arrow with the recipe's own
per-unit arithmetic. Two corrections came out of the screenshots.

### The popup's third row is the resulting stock

```
Cure                4      <- source left after the refine
Number to refine   16
Cura               87      <- NOT the sixteen being made
```

Squall held 71 Cura; 71 + 16 = 87. The other two shots agree — "Fira 51" for
thirteen made, "Death Stone 4" for four made from none.

This matters beyond matching the screen. **Both grant paths clamp at 100** —
magic at `0x004C2CCC`, items at `0x0047ED54` — so a player who cannot see that
third row cannot see that he is about to spend materials on nothing. The stock is
found by id: 32 slots at `0x01CFE0F8 + charId*152` for magic (the same scan as
`0x004C2C8D`), 198 inventory slots for items (`0x0047ED18`).

### State 0x28 writes the numbers it is read for

`0x28` sets `+0x4C` (max) and `+0x4F` (count) at `0x004D89F1`/`0x004D89F4`.
Treating it as "the popup is up" is right — otherwise a source row gets announced
over it — but reading the two bytes during it caught them pre-write:

```
Refine quantity: count=0 max=0 -> "Number to refine 0, makes 0 Death Stone, 2 Dead Spirit left"
Refine quantity: count=1 max=2 -> "1 of 2, makes 2 Death Stone, 1 Dead Spirit left"
```

The phase still covers `0x28`; the announcement waits for a real count and a real
maximum.

### A naming note

Sub-mode 2's picker was still logging as "Refine recipient". It is the **source**
character — whose magic is consumed — and the recipient/source confusion is
precisely what made this screen read the item inventory for four builds. The log
now says which it is.


---

## 8. The refine itself (v0.33.2)

The v0.33.1 BAT walked every screen in §6 and §7 and confirmed both fixes — and
backed out of every refine. Had one been confirmed, the mod would have said
nothing.

**Why it is not simply "watch for state 0x2A".** `0x2A` does the grant and the
consume and then jumps straight to `0x25` or `0x17` in the same frame
(`0x004D7DEA`). A poller at 80 ms will usually miss it entirely. And the two ways
out of the popup are indistinguishable from the state alone: cancel
(`0x004D7946`) and confirm (`0x004D7A29`) both clear `+0x51`, and both land the
player on a list with the cursor where it already was, so the source row does not
re-announce either.

**What does separate them is the source's own count.** A confirm consumes exactly
`sourcePer * count`; a cancel consumes nothing. So the popup stages
`{sourceId, ownedNow, consumed, produced, total}` while it is up — none of which
survives it closing — and the close re-reads the source by id:

| before → after | verdict |
|---|---|
| dropped by exactly `consumed` | **"Refined 5 Tent into 50 Curaga, 50 total"** |
| unchanged | **"Cancelled"** |
| anything else | **silent** |

The third row matters. A partial drop, a count that went *up*, an unreadable
before or after, or a zero consumed all mean the measurement disagreed with the
model — and announcing a refine that did not happen would be the same class of
error as every other entry in this document.


---

## 9. Two lessons from shipping §8 (v0.33.3)

**The reader shipped without the line that arms it.** `PollRefineOutcome()` and
the block that sets `s_abilQtyArmed` were one edit; the edit aborted partway and
only the half I had watched fail got re-applied. The flag was declared, reset and
read — never set. Everything compiled, every probe passed, and the BAT log had
not one `Refine outcome` line in it.

The probe asserted `RefineOutcomeOf()`, which is pure and correct and was never
called by anything. **Testing a pure function is not testing that anything calls
it.** This is the magazine probe's lesson (`FindItemModule()` stubbed to null,
so the decode under test never ran) in different clothes, and it now has a
counter-measure: `menu_ability_compile` drives `PollAbilityItemList()` through
real module memory and asserts the spoken string.

Two things that block make possible and a pure test cannot:

* The `GetTickCount()` stub had to **advance**. Pinned at `0`, the poll's own
  80 ms gate never opens and the whole block is vacuous — a test that runs
  nothing and passes.
* It caught a second defect immediately. The outcome announcement **fell through
  to the source-row branch in the same poll**, and both utterances interrupt — so
  "Refined 1 Dead Spirit into 2 Death Stone, 2 total" would have been cut off by
  "Dead Spirit, 1" and the player would have heard only the row he was already
  standing on. The poll returns when the outcome speaks, and the probe asserts
  the utterance count, not just its text.


### Addendum (v0.33.4): returning early only protects the poll you are in

§8's outcome line landed on all three refines with correct numbers. It was still
spoken over on sub-mode 0 — one poll later rather than in the same one:

```
17:12:30  "Refined 3 Tent into 30 Curaga, 30 total"
17:12:30  Refine recipient: id=0 slot=0 "Squall"
17:12:30  Refine recip stock: stock=30
```

A sub-mode 0 refine lands back on the **recipient picker** (state `0x25` →
`0x1C`), and the quantity popup re-arms that announcer on its way past. So the
next poll re-described a screen the outcome line had already covered, and both
utterances interrupt.

The outcome now seeds the picker's dedupe and its stock follow-up to whatever the
engine drops us on. Seeded, not disabled — moving the picker still announces.
The probe asserts both, and asserts that the poll *after* the outcome is silent.
