# Disc 2 blockers — D-District Prison escape and the Missile Base terminal

What the executable and the game archives say about the two scenes, and what the
mod has to do about each. Written 2026-08-20 from `FF8_EN.exe`, `field.fs`,
`menu.fs` and `main.fs`. Nothing here rests on a BAT.

---

## 0. Tooling this needed (and the two things that were wrong before)

**The field-script opcode table is at `0x00B8DE94`.** An `optable.json` left over
from an earlier session was dumped from `0x00B8DE4C` — 18 entries lower, because
a separate 18-entry OPERATOR table (ADD, SUB, …, SHL) sits immediately below the
opcode table and opcode `0x01` indexes the pair as one array. Every opcode name
derived from that dump is shifted by 18. It is why a scan for "MOVIE = 0xB5"
found no movie in any field: **MOVIE is 0xA3.** The mod's own runtime log settles
the base — `execute_opcode_table = 0x00B8DE94` and `opcode_menuname [0x129] =
0x00521DA0`, which matches this table exactly.

**The instruction encoding has two forms** (decoder at `0x00530760`):

```
w & 0xFF000000 == 0   ->  opcode = w & 0xFFFFFF, NO operand
otherwise             ->  opcode = w >> 24, operand = sign-extend24(w)
```

The zero-high-byte form is how every opcode above `0x37` is reached, including
MES (`0x47`), MOVIE (`0xA3`) and MENUNAME (`0x129`) — which does not fit in a
byte at all. An earlier disassembler read those words as "push literal", which
hides every interesting call in the script. `fx/jsm2.py` in the work tree decodes
both forms.

**Two more things a field disassembler gets wrong silently**, both found while
looking for the screens Aaron asked about:

* **A method's entry index is `entries[start + M + 1]`, not `entries[start + M]`.**
  The group word's `start` points at the entry *before* the entity's first
  method. Off by one, every entity's LAST method disappears — which is exactly
  how the SET TARGET submenu went missing on the first pass.
* **The entity id a `REQ` uses is not the group-table index.** They differ by a
  rotation of sixteen: `group = (entityId - 16) mod entityCount`. The `.sym`
  name list is in *entity id* order, which is why the names looked scrambled
  against the group table. Settled by `REQ(settarget, 8)`: group 31's start is
  4 and `targetmenu` is method 3, so `4 + 3 + 1 = 8`, and independently by
  `ratiou::decrease`, whose body is the `var[482] -= 6` step.

Also note entry values carry a `0x8000` flag bit; mask it off.

Opcodes and addresses established on the way, useful well beyond these two
scenes:

| thing | where |
|---|---|
| field variables | byte array at **`0x01CFE9B8`**; `0x0A/0x0B` push/pop byte, `0x0C/0x0D` word, `0x0E/0x0F` dword, `0x10/0x11` signed byte/word |
| buttons held | **`0x01CE48B0`** (JSM opcode `0x6D`) |
| buttons newly pressed | **`0x01CE48B8`** (JSM opcode `0x6E`) |
| entity script context | `[0x01D9D020 + entity*4]`; locals at `+0x140` (8 dwords), pc at `+0x176`, stack pointer at `+0x184` |
| MOVIE | opcode `0xA3`, two stack args `(movieNo, mode)`; handler `0x0051F2C0` calls `0x0055A140(disc-1, movieNo)` |
| MES family | `0x47` MES, `0x46` MESW, `0x65` positioned MES `(win, msgId, x, y)`, `0x64` AMES |

---

## 1. The prison escape — what it is NOT

Aaron: *"an FMV plays after the final boss battle. During this FMV, Squall is
shown hanging onto a ledge and the player must move Squall to the right in order
to survive."*

**The movie player never reads the pad.** There are exactly twenty references to
the three pad masks in the whole executable:

```
0x01CE48B0 (held)     0x476824 0x476889 0x47692D 0x476BB3 0x477788 0x47779B
                      0x477843 0x477856 0x479737 0x51DA57
0x01CE48B4            0x476836 0x477790 0x4777A7 0x47784B 0x477862 0x47972B
0x01CE48B8 (pressed)  0x476898 0x51DAA7 0x52994E 0x52A1C8
```

Every one is in the **field** module (`0x476xxx`–`0x479xxx`), the JSM button
opcodes (`0x51DA50`, `0x51DAA0`) or the script interpreter. **None is in the
movie player** (`0x0055A140`–`0x0055A6xx`). So there is no interactive-FMV code
path: whatever the player is doing during that scene, they are doing it as
ordinary field movement, with a movie or animated background behind them.

**No prison field polls a direction either.** Scanning all twenty-six `gp*`
fields for opcodes `0x6D`/`0x6E` returns exactly one hit — `gpgmn2`, mask `0x40`,
the confirm button. So the script is not reading Left/Right; the engine's normal
walking code is. That is consistent with the above and rules out a scripted QTE.

### What it is

The prison plays four movies, and only four:

| field | movie | = on disc 2 | AD reference description |
|---|---|---|---|
| `gpcell1` | 0 | `disc01_00h` | prison interior, capsule descending the shaft |
| `gpexit1` | 1 | `disc01_01h` | exterior gangway, desert, the three drill legs |
| `gpbrdg1` | 2 | `disc01_02h` | gangway to the control tower, drill leg close-up |
| `gpexit2` | 2, 3 | `disc01_02h`, `disc01_03h` | the walking prison striding across the desert |

`gpbrdg1` is a ten-entity test field that loops movie 2 forever; ignore it.

Contact sheets pulled from `disc01_01h`, `02h` and `03h` show gangways, drill
legs and the prison walking — **no shot of a character hanging from a ledge.** So
the thing Aaron sees as an FMV is the *field* with a movie behind it, not a movie
with a character in it. Two pieces of the developers' own leftover text say so
outright:

* `gpexit1` message 0: **"In development / Character not in motion with movie /
  Please wait"** — a note about the character moving *at the same time as* the
  movie. That is this field's whole gimmick.
* `gpexit2` message 0: **"Movie/event in development. Test movie // What to do in
  the parking lot / Go to the desert / Do not return to prison. It crashes."**
* `gpexit2` message 1: **"&lt;NAME&gt;!!! Hold on! Over here! Hurry!"** — someone
  calling to the player character while the prison moves away.

Story progress (`var[256]`, word at `0x01CFEAB8`) is written **446** at the end
of `gpexit1`'s movie sequence and **450** at the end of `gpexit2`'s. The escape
therefore lives in the window `446 <= var[256] < 450`, in `gpexit1` and/or
`gpexit2`.

### What is still open

Which of the two fields, and at what moment inside it, the player must move. The
scripts hand control back with ordinary opcodes and the failure condition is not
a script branch I can point at, so this is the one thing the executable does not
settle. **It does not need to be settled before shipping** — see the design
below, which is keyed on facts that are settled and logs the rest.

---

## 2. The Missile Base terminal — fully settled

Field **`gmmoni1`** (Galbadia Missile Base, the monitor close-up). The whole UI
is field entities and background layers driven by field variables; there is no
menu module involved, which is why nothing in the mod sees it today.

### The screens, in the game's own words

`gmmoni1.msd` holds 35 messages. (Its header is *not* a count — the first dword
is the first message's offset, so the table is `offset[0]/4` entries long.
Reading it as a count shifts every string by one and turns "SET TARGET" into
"CONFIRM EQUIPMENT".)

```
 0 SET TARGET          4 SET ERROR RATIO
 1 CONFIRM EQUIPMENT   5 DATA UPLOAD
 2 SIMULATION          6 "Right directional button to increase. / Left ... decrease. / [Cancel] to exit."
 3 EXIT                7 "Use directional button to confirm equipment. / [Cancel] to exit."
                       8 "Use directional button to change. / [Cancel] to select."
                       9 "End simulation. / [Cancel] to return."
```

### The screens, and the state behind each

There are **five** interactive screens, all in `gmmoni1`, and the field script
names them itself:

| screen (script name) | cursor | address | values |
|---|---|---|---|
| `Director::default` — main menu | `var[1024]` byte | **`0x01CFEDB8`** | 0 SET TARGET · 1 CONFIRM EQUIPMENT · 2 SIMULATION · 3 EXIT |
| `settarget::targetmenu` — SET TARGET submenu | `var[1025]` byte | **`0x01CFEDB9`** | 0 SET TARGET · 1 SET ERROR RATIO · 2 DATA UPLOAD · 3 EXIT |
| `control::errorratioset` — the ratio bar | `var[482]` **signed word** | **`0x01CFEB9A`** | `0` minimum … `-156` maximum, step 6 |
| `control::uploader` — **the DATA UPLOAD yes/no** | `var[1028]` byte | **`0x01CFEDBC`** | 0 = No (leave) · 1 = Yes (upload) |
| `control::equipment` — CONFIRM EQUIPMENT | `var[1028]` byte | **`0x01CFEDBC`** | 0-3 = the four equipment lines (messages 10-13) · 4 = exit |

Supporting state, shared by all five:

| what | variable | address | meaning |
|---|---|---|---|
| progress flags | `var[484]` byte | **`0x01CFEB9C`** | bit0 = ratio accepted at maximum, bit1 = data uploaded |
| screen latch | `var[1030]` byte | `0x01CFEDBE` | 0 = screen live, 1 = leaving, 4/5 = finished |
| input latch | `var[1027]` byte | `0x01CFEDBB` | 1 while an input is being consumed |
| confirm latch | `var[1029]` byte | `0x01CFEDBD` | 1 when confirm was pressed |

Button masks, read off the five loops and consistent across all of them:
**`0x1000` up · `0x2000` right · `0x4000` down · `0x8000` left · `0x40` confirm.**

### The logic that matters

**Main menu** (`Director::default`, dwords 100-144) — `0x4000`/`0x1000` step
`var[1024]` with wraparound, `0x40` confirms; each of the four blocks ends in
`MES(win 1, msg n, 16, 160)` where **n is the cursor value**. The readout is a
table lookup with nothing to infer.

**SET TARGET submenu** (`settarget::targetmenu`, dwords 416-631) — the same
shape on `var[1025]`, ending in messages 0, 4, 5, 3. Confirming item 1 runs
`REQ(control, errorratioset)`; item 2 runs `REQ(control, uploader)`. Item 3
(EXIT) checks `var[484]`: it only lets you out when **both** bit 0 and bit 1 are
set, and otherwise refuses with one of the party's nag lines.

**SET ERROR RATIO** (`control::errorratioset`, dwords 652-712) — draws message 6
("Right … to increase. Left … to decrease."), then *holds* `0x2000` / `0x8000`
to run the increase / decrease steps. The bar moves six units at a time while
`var[482] > -156`, so there are 27 positions. On confirm:

```
if var[482] <= -150:  var[484] |= 1        <- accepted
else:                 nothing happens      <- silently refused
```

That silent refusal is worth announcing: the screen gives no feedback at all
when the ratio was not far enough.

**DATA UPLOAD confirmation** (`control::uploader`, dwords 732-813) — this is the
yes/no Aaron remembered. `0x2000` selects 0, `0x8000` selects 1, `0x40` confirms:

```
cursor 0 -> confirm: close, var[1030] = 1, leave            = NO
cursor 1 -> confirm: if (var[484] & 1) var[484] |= 2        = YES
```

The two options are drawn by the entity the `.sym` file calls **`upbyesno`**
(`yeson` / `yesoff` / `noon` / `nooff`), so they are labelled YES and NO on
screen — and the effect mapping above is what the mod should announce, because
it is true whichever way round the sprites sit. **Note the trap:** choosing YES
does nothing at all unless the error ratio has already been accepted. The screen
does not say so.

**CONFIRM EQUIPMENT** (`control::equipment`, dwords 830-1078) — `var[1028]`
0-4, each value drawing one of messages 10-13, and 4 exiting. Two of its five
transitions need button *chords* (`0x80`+`0x10`+`0x1000`, and `0x10`+`0x80`+
`0x4000`), which is worth knowing before anyone reports that two of the items
"can't be reached".

---

## 2b. The password — `gmtika4`, and it is a standard choice dialog

The Missile Base password scene is in field **`gmtika4`**, and the answer is
given by the Wounded Soldier in message 19:

> *"You can… adjust the missile coordinates on that control panel. Use my… ID
> card to log in… The password is… 'EDEA'"*

The entry UI is **not** a bespoke keyboard. It is four ordinary **AASK** choice
dialogs in a row, all on message 23:

```
{3F}Please enter / your password.{3E} / A / B / C / D / E / F
```

`AASK(win 0, msg 23, firstLine 2, lastLine 7, default 2, cancel 2, x 20, y 20)`
— so the six choices are lines 2-7, "A" through "F", and the value returned is
the **0-based choice index**. The script (entity `Quistis` in `.sym` order,
method 2, dwords 2189-2320) reads:

```
if (var[484] & 0x80)  and  (var[492] & 0x40):        ; ID card received
    AMES(msg 22)                                     ; "Verification Complete"
    loop while attempts < 4:
        AASK -> c ;  if c == 4        ; E
        AASK -> c ;  if c == 3        ; D
        AASK -> c ;  if c == 4        ; E
        AASK -> c ;  if c == 0        ; A
            AMES(msg 24)                             ; "ID Number XT-225W. Access granted."
            MAPJUMP 773
    AMES(msg 25)                                     ; "Incorrect password. Please try again."
```

4, 3, 4, 0 over A-F is **E, D, E, A**.

**The good news: the mod already hooks AASK** — `Hook_opcode_aask` →
`ScanAndSpeakChoiceWindows("AASK")` — so the prompt and the six letters should
already be spoken and navigable. What is missing is only **position**: four
identical prompts in a row with nothing to tell the player which letter they are
entering, and no way to re-hear the password once the soldier has finished
talking.

## 3. Implementation plan

### 3a. Missile Base terminal — `src/menu_tts_missile.inl` (new)

A pure model plus a poller, in the shape the limit-break and shop readers use.
Everything it does is a read of a documented byte: no hooks, no patches, no
writes.

**Model** (`src/missile_terminal_model.inl`, host-testable):

* the address table above as named constants;
* `MissileRatioSteps(v) = -v / 6`, `MissileRatioPercent(v) = -v * 100 / 156`,
  `MissileRatioAtMax(v) = v <= -150`;
* `MISSILE_MAIN_LABELS[4]`, `MISSILE_SUB_LABELS[4]`, `MISSILE_EQUIP_LABELS[4]` —
  **not typed out.** The probe asserts them against the real `gmmoni1.msd`
  bytes, the way `battle_limit_compile` asserts the limit names against
  `kernel.bin`. Cursor value equals message id on both menus, so the assertion
  is `label[i] == decode(msd[i])` and a shifted `.msd` reading fails it.

**Screen tracking.** `var[1028]` is shared by the uploader and the equipment
screen, so the reader keeps the same small state machine the script does rather
than guessing from the value:

```
main cursor 1 + confirm  -> CONFIRM EQUIPMENT   (var[1028] 0..4)
main cursor 0 + confirm  -> SET TARGET submenu  (var[1025] 0..3)
     sub cursor 1 + confirm -> ERROR RATIO      (var[482])
     sub cursor 2 + confirm -> UPLOAD YES/NO    (var[1028] 0..1)
```

`var[1030]` returning to 0 pops back a level; leaving the field resets it.

**What it says:**

1. On entry — *"Missile control terminal. Set target, confirm equipment,
   simulation, exit. Up and down to change, confirm to select."*
2. On any cursor change — the label for the new cursor.
3. Entering the submenu — *"Set target. Set error ratio, data upload, exit."*
4. Entering the ratio screen — *"Error ratio, 0 percent. Right to increase,
   left to decrease."* then the percentage on every change of `var[482]`,
   throttled to one utterance per ~250 ms so holding the button does not queue
   twenty-seven of them, and **"maximum"** the instant `var[482] <= -150`.
5. Confirm on the ratio screen — `var[484] & 1` → *"Error ratio set to
   maximum."*; otherwise *"Not accepted. The ratio must be at maximum."*
   (the screen itself gives no feedback here at all).
6. Entering the upload confirmation — *"Upload coordinate data? Yes, no."*,
   then *"Yes"* / *"No"* on cursor change, described by **effect** so it is
   right whichever way the sprites sit.
7. Confirm on the upload screen — `var[484] & 2` → *"Coordinate data
   uploaded."*; otherwise *"Nothing was uploaded. Set the error ratio to
   maximum first."*
8. EXIT refused — read the party's nag line (the mod's dialog reader already
   sees it) and add *"Error ratio not set"* or *"Data not uploaded"* from
   `var[484]`, so the reason is explicit.
9. `/` re-reads the current screen, cursor and value.

### 3c. The Missile Base password — `gmtika4`

The choice dialog itself needs nothing: AASK is already hooked. Two small
additions, both in the existing field-dialog layer:

* **Say which letter you are on.** When an AASK prompt repeats identically
  within the same field, announce *"Letter 1 of 4"* … *"Letter 4 of 4"* ahead of
  the choice list. The counter is the mod's own — count consecutive AASKs
  carrying the same message text and reset on any other dialog or on a field
  change. That covers this scene without a `gmtika4` special case.
* **Make the password re-readable.** The answer is stated once, inside a long
  multi-page speech (message 19), and the choice dialogs follow immediately.
  The mod already keeps the last field dialogue; bind re-reading the last
  *completed* dialogue to a number key so the player can hear "EDEA" again while
  the prompt is up.

Worth checking in the same pass: the six choices are single letters, and the
choice reader trims and decodes each line — confirm that a one-character line
survives whatever whitespace handling `DecodeChoices` does, and that "A" is
spoken as a letter rather than swallowed.

### 3b. Prison escape — `src/field_urgent_prompt.inl` (new)

The mechanism Aaron asked for — *"at the appropriate time, start announcing
'Move right' until the scene ends"* — but keyed on what is settled rather than on
which field I guess.

A small table of cues:

```c
struct UrgentCue {
    const char* field;      // field name, or nullptr for "any"
    const char* avi;        // AVI basename that must be playing, or nullptr
    uint16_t    storyLo, storyHi;   // var[256] window
    uint32_t    startDelayMs;       // after control is handed back
    uint32_t    repeatMs;
    const char* text;
};
```

with the prison entry covering the settled window:

```c
{ "gpexit1", nullptr, 446, 450, 1500, 2000, "Move right" },
{ "gpexit2", nullptr, 446, 450, 1500, 2000, "Move right" },
```

Firing conditions, all of which the mod already tracks:

* the current field name matches;
* `var[256]` is inside the window;
* **the player has control** (not in a dialog, not in a menu, not mid-cutscene) —
  this is the gate that keeps the prompt out of the surrounding cutscenes, and it
  is why the cue does not need to know the exact frame the scene starts;
* the cue stops on field change, on losing control, or on a battle starting.

If the scene turns out to be in only one of the two fields, the other entry never
matches a moment where the player has control and stays silent. If the timing is
slightly off, the prompt is early or late by a second, not absent.

**And the same build makes the BAT self-identifying.** A `[PRISON]` diagnostic,
active only in `gp*` fields, logs once a second: field name, `var[256]`, the
current AVI from `FmvSkip::GetCurrentAviName()`, whether the player has control,
and the character's position. One run through the escape then pins the field, the
moment and the direction exactly — so if the cue needs adjusting, the adjustment
is a data edit in the table, not another investigation.

---

## 4. BAT strategy

Both changes go in **one build**, because they are on opposite sides of the game
and neither can break the other.

* **Missile Base**: reachable and fully specified; expect it to work first time.
  Listen for the four main labels, the four submenu labels, the ratio percentage
  climbing to "maximum", the upload yes/no, and the two confirm messages. The
  password scene is on the same trip — listen for "Letter 1 of 4" and the six
  letters.
* **Prison**: the prompt should speak during the escape. Whatever happens, the
  `[PRISON]` log lines from that one run are what fix any timing error without a
  second exploratory run.

If the prison cue misfires, the correction is one line in the cue table, and it
can ride along with whatever the next build is — it does not need its own trip
back to the prison.

---

## 5. Loose ends worth writing down

* `gpexit1`'s walkmesh (`.id`, `u32 count` then `count * 30` bytes) is far longer
  than any other prison field's. The 30-byte record layout is not fully decoded
  (my first reading gave correlated X/Y/Z, so the vertex offsets are wrong); a
  correct decode would let the mod describe ledges and gangways generally.
* The `.sym` entity-name order does **not** match the JSM entity-group order.
  Names from `.sym` are a hint, not an index. Everything above is keyed on entity
  index, not name.
* Script entry-point offsets carry a `0x8000` flag bit; mask it off or half the
  methods appear to start past the end of the code.

---

## 6. Settled by the 2026-08-20 BAT (shipped in v0.39.0)

The one open question in section 1 — which field, and when — is closed. The log:

```
22:25:44  field 'gpexit2' loads
22:25:48  disc01_02h plays (3 audio-description cues)
22:26:10  RAMESW: Rinoa -- "Squall!!!  Hold on!  Over here!  Hurry!"
22:26:10  disc01_03h starts                       <- the window opens here
22:26:12  Aaron's F11 shot, "when the player should start moving right"
22:26:40  disc01_03h ends
22:26:45  MAPJUMP from (3817,115) -> 'gppark1'    <- success
```

**Trigger: field `gpexit2` + movie `disc01_03h`.** The cue arms on the movie and
lives on the field, because the movie ends five seconds before the map jump and
the player is still walking; leaving the field is what success looks like, so
that is what ends the prompt.

Shipped as `src/field_urgent_prompt.inl`, a cue table keyed on
`{field, armAvi, startDelay, repeat, cap, text}`, polled at the **top** of
`FieldDialog::PollWindows()` — above that function's FMV early-out, which is
what would otherwise have made it unfireable.

The same BAT found that `FmvAudioDesc` spoke every cue with `interrupt=true`,
which truncated Rinoa's line one second in. A cue at 0.0 s always lands on the
dialogue that triggered the movie, so that was every FMV that follows a line,
not just this one. Cues queue now.

---

## 7. Shipped in v0.40.0 — the terminal

`src/missile_terminal_model.inl` + `src/field_missile_terminal.inl`, polled from
the top of `FieldDialog::PollWindows()`. Reads only; no hooks, no patches.

What it adds beyond the labels: the ratio as a percentage with "maximum" at
`var[482] <= -150`, the upload yes/no named by effect, and an outcome line on
leaving the ratio or upload screen (both of whose refusals are silent on screen)
computed by diffing `var[484]` against its value on entry. `/` reads back the
screen, the value and both flags, bound only while `gmmoni1` is loaded.

`tests/missile_terminal_compile.cpp` asserts the labels against the encoded
bytes of messages 0-5 from `gmmoni1.msd`, decoded by the real decoder.

Still open: the **password** counter (section 3c). AASK is already hooked, so
the `gmtika4` screen may already read; the missing piece is only which of the
four letters is being entered.
