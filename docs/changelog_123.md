## v0.20.123

#minigame-bgbtl: **the Game Controls are on screen now — and getting them there
meant giving up the field_main pause.**

> *"Let's also implement a visual dialog on the Game Controls screen so any
> low-vision players or sighted players watching can see the Game Controls. Use
> an in-game dialog box. Make sure the dialog is also sized properly so all the
> text is visible."* — Aaron

### Why the pause had to go

Window rendering is `0x004A0880`, and the call chain is

```
field_main -> 0x00471010 -> 0x0052BC00 -> 0x004A0880
```

So the one-byte `RET` at `field_main`'s entry that has paused this scene since
v0.20.106 **also guaranteed no dialog box could ever be drawn**, or even finish
opening. The two features are mutually exclusive as built.

**The briefing now pauses the FIGHT rather than the FIELD**, using the mechanism
the F9 skip proved in the field on 2026-08-15: the soldier's attack REQs are
rewritten to the entity's own `push` script, and both HP values are pinned.
Nothing reaches Squall, nothing makes a sound, and the game keeps running.

That buys three things beyond the picture:

* the key learner reads the **real** button word instead of the engine-side
  stand-in it needed while everything was stopped;
* the FMV keeps playing, which is what is on screen anyway;
* **there is no longer any patched byte to restore** — so no heartbeat, no guard
  thread, and no way for a fault in the mod to leave the game stranded. That
  whole class of danger is deleted.

The one thing the freeze bought that the veto does not is a stopped clock. The
movie behind the fight keeps playing, so an unread briefing could run the round
down. **`CLOCK_BRIEF_LIMIT` hands the fight back at clock 420** — well before the
resolution at 580 — with *"Starting now."* Nobody loses to a reading speed.

### The box

Copied from **AMES** (opcode 0x65, `0x005291E0`) — the auto-positioning,
auto-sizing message variant, which is the right one because it is the one that
fits the box to its text:

```
set_window_object(win, text)      0x004A0410
size = measure_text(text)         0x004A0EC0   low word = width, high = height
w = (size & 0xFFFF) + 0x10        h = (size >> 16) + 0x11
clamp x + w < 0x130, y + h < 0xE0, both to a minimum of 8
set_window_geometry(win, rect)    0x004A07A0
open_window(win)                  0x004A0620
set_current_window(win)           0x0049FD50
[0x00B8EE90] + 0xD3 / +0xD4 |= (1 << win)
```

and WINCLOSE (`0x00529B60`) in reverse on the way out.

**The sizing is the game's own.** `0x004A0EC0` walks the string with the real
font metrics; the mod does not estimate anything. All it does first is word-wrap
to 34 columns, which is what keeps every line inside the box the measurer then
sizes. Window **7** — the scene's own legend lives in window 4, and 7 has never
been seen occupied here.

Text is encoded to FF8's charset (the inverse of `ff8_text_decode.h`'s table:
`A-Z` → `0x45+`, `a-z` → `0x5F+`, `0-9` → `0x21+`, newline `0x02`), and it names
the player's real keys:

```
GARDEN BATTLE

Block A   Punch W
Kick X   Heavy punch D

Hold Block. Three blocks in a row
arm the Heavy punch, which takes
most of the soldier's health.
Two of those win the round.

Enter starts.  Space repeats.
F9 skips.
```

If the window fails to open for any reason the briefing is still spoken — it
costs the picture, not the feature.

### The cosmetic item, fixed

The first *"Heavy punch ready"* of a session read **"(key unknown)"** unless the
player happened to tap that key during calibration — 14:32:33 in the last log
said it, and 14:32:40 said "D" once he had.

**The key table now ships seeded** with `W` punch, `X` kick, `A` block, `D` heavy
— which are FF8 PC's stock bindings and, more to the point, exactly what two runs
on Aaron's machine measured on `0x01CE48B0`. The seeds are deliberately left
**unlocked**, so the first real press on a differently-configured machine
overwrites them rather than arguing with them.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors.** The probe now maps an
  **executable** page over the six engine routines the box calls, filled with
  `ret`, with a `mov eax, imm32 ; ret` at the text measurer — so `OpenBriefDialog`
  runs its real geometry maths on a real measurement instead of being stubbed at
  the C++ level. New assertions: the FF8 encoding is byte-exact; wrapping never
  exceeds its column and does break lines; a source newline survives; the box
  opens, sets its open bit, closes, and clears it; the briefing hands the fight
  back at clock 420 but not at 100; the heavy-punch seed is present and is
  **not** locked.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` unchanged at **81,645 — 275 from the hard fail**. New
  file `src/field_minigame_bgbtl_dialog.inl` (9,353).

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **Look at the Game Controls screen** — or have someone look. There should be
   a proper FF8 dialog box, centred, with every line inside it and your own key
   names in it.
2. **The scene is no longer frozen behind it.** The movie keeps playing and the
   soldier should still land nothing — that is the veto, not the pause. Confirm
   you take no damage while reading.
3. Everything else as before: hold block, heavy punch when it arms, F9 to skip.

The one new failure mode worth knowing about: if the box does not appear, grep
`[BGBTL-DLG]` — it logs the measurement and the box it derived, or the exception
if it could not open.
