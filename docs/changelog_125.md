## v0.20.125

#minigame-bgbtl: **one line of someone else's code ate the win announcement, and
the soldier's health bar was lying.**

### The bug: "Skipping video" instead of "You win"

> *"I still did not hear You Win once I pressed F9. I instead heard it say
> skipping video which was weird since it did not skip any video that I could
> tell."*

Both halves of that were the same line. `FmvSkip::RequestSkip()` — the *code*
path, added in v0.20.117 so the mod could end the battle movie behind the scenes
— was setting `g_skipRequested`, and that flag is what makes `FmvSkip::OnFrame`
say **"Skipping video"** with interrupt on. So the mod's own
*"Skipping. You win…"* was spoken and then immediately talked over by the
subsystem it had just called.

And the reason no video appeared to skip: the mod ends `disc01_32h.avi` to move
the host phase along, which is a phase change the player experiences as the
scene simply continuing.

```
15:26:09  SKIP: host phase -- FMV skip requested
15:26:09  SKIP engaged -- foe at 0, Squall pinned at full
```

**`RequestSkip()` is now silent.** It is a code path; a caller that wants the
player told says so itself. Backspace is untouched and still announces.

### The question: he falls, and the bar was wrong

> *"After pressing F9, is the soldier still visible and not moving, or does he
> fall down as if he'd been punched out?"*

**He falls.** Your 15:26:42 shot shows Squall alone on the wire — the game plays
its own knockout: at clock 580 `director0` takes the win branch and REQs
`gal0::gal0_timeover0`, which is the soldier's fall animation. Nothing is frozen
in place; you get the same ending a won fight gives.

But the same screenshot caught something worth fixing: **the Galbadian Soldier's
bar was still full red** while the mod had just said "You win." The bars are not
the HP variables. Opcode 315 (`0x00529BF0`) is what moves them —

```
shl esi, 4
mov word ptr [esi + 0x1D9CF5C], di
```

— and the scripts only call it from inside `squ_hpcalc0` / `gal_hpcalc0`, which
is to say only when a punch actually lands. F9 writes the variables directly, so
the bars never heard about it. **The skip now writes both gauges too**, and so
does the win called from the foe reaching zero.

### Also

The box moved to the top and cleared the legend, as intended — but the measurer
returned `210x108` for seven lines and the last one sat right on the bottom
border. The measurement is not wrong; AMES's `+0x11` border allowance just
leaves nothing under a final line that reaches full height. **+8 of slack.**

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors.** The probe now maps the gauge
  page too, and asserts the soldier's bar is emptied and Squall's refilled by the
  skip — not just the variables.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` unchanged at **81,645 — 275 from the hard fail**.

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **F9** — you should hear *"Skipping. You win…"* and nothing about video.
2. **F11 after F9** — the soldier's bar should be empty and Squall's full, so the
   picture matches what you were told.
3. **F11 on the Game Controls** — the last line should have clear space under it
   now.
