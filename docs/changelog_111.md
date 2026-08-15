## v0.20.111

#minigame-bgbtl: **labels are per field, and the forged jump is gone.**

### The tone was never late — it was never fired

> *"I sat back and let the enemy hit me in order to listen for the block sound,
> and did not hear it until I was almost dead."*

**Label numbers are an index into the field's own entry-point table. They mean
nothing across fields.**

v0.20.110 cued on 48/49 — `bgbtl_1`'s `gal0::g0_punching0` and `g0_kicking0` —
while **most of the fight runs in the host field**, where those numbers are
something else entirely. The log is exact: six attacks in `bg2f_31` with not one
cue, then the field changed to 152 at +23562 ms and the tones finally started —
by which point Squall was at 11 of 600.

The correlation in that same log is unambiguous, ~700 ms of warning every time:

```
+7468   label 55  ->  +8203   damage   Squall 440 -> 382
+9546   label 56  ->  +10140  damage   Squall 382 -> 280
+12546  label 56  ->  +13140  damage   Squall 280 -> 193
+14812  label 55  ->  +15546  damage   Squall 193 -> 146
+16875  label 56  ->  +17468  damage   Squall 146 -> 106
+19546  label 55  ->  +20281  damage   Squall 106 -> 11
```

`bg2f_31`'s own group table puts the soldier `g_hei0` at labels 45..57, so
**54/55/56 are its punching / kicking / guarding trio.** Both tables now ship,
keyed by field id, and **an unknown field never cues blind.**

The `[BGBTL-REQ]` line now carries the field id, the per-field **name**, and a
`<- BLOCK` marker — so the next mismatch shows up as a wrong name rather than as
silence.

### The forged victory jump is gone — it crashed

> *"I tried using the F10 skip functionality, and it caused the game to crash
> when I entered G-Garden."*

Writing `push 675, 1019, 3384, 0, 128 ; MAPJUMP3` reproduces the **bytes** the
winning script writes but **none of its state**. `director0::talk` reaches that
line having finished the fight and torn its scene down. Firing it mid-fight — in
the host field, where that script isn't even loaded — arrives in Galbadia Garden
with the previous scene half dismantled.

**The skip now uses the game's own path: zero the foe's HP and top the player
up**, then let the script resolve the win exactly as it does when you win on
merit. Knockout, scene, transition, flags — all of it. Nothing is forged, so
nothing can desync.

It's slower than a jump (the v0.20.110 BAT measured ~68 s from zero HP to the
field change), but the win is announced immediately from HP anyway, so the wait
is silent rather than confusing.

The transition finding stays in the source as a comment, because it is still
true and someone will be tempted again.

### The tone is a real waveform now

`Beep()` has no volume control at all — it is whatever the system beep happens to
be. Replaced with a synthesised 22 kHz 16-bit PCM buffer played through
`PlaySound(SND_MEMORY | SND_ASYNC)` at full scale:

* two rising tones, **1046 Hz then 1568 Hz** (C6 → G6)
* **90 ms each**, 180 ms total — well inside the 700 ms window
* 4 ms taper at each edge so it doesn't click

A rising pair is unmistakably not a game sound. **`winmm.lib` added to
`deploy.bat`** — that's the one build-system change in this release.

### Wording

> *"Just seems weird for the mod to refer to itself in first person."*

Agreed. "The mod plays a tone when it is time to block" / "The mod also calls out
health as it changes."

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now asserts the two
  label tables **against each other** — the host field accepts 54/55/56 and
  rejects 48/49/57, `bgbtl_1` the reverse, an unknown field rejects both — that
  label 55 **resolves to a different name per field**, and that the tone buffer
  actually contains a waveform (peak 21304 in the first 64 samples).
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **The tone should be loud and obviously synthetic** — two rising notes. Press
   **A** on the briefing screen to hear it.
2. **It should now fire from the very first attack**, in the host field, ~700 ms
   before you take damage. This is the fix that matters; if you sit back again,
   every hit should be announced in advance.
3. **F10** should no longer crash. Expect it to zero the foe, announce
   *"Skipping. The fight is won."* and *"Foe defeated. You win."*, then take up
   to a minute to reach Galbadia Garden through the game's own ending.
4. The briefing should say "The mod" rather than "I will".

Grep `[BGBTL-REQ]` — every line now shows `fld=` and a resolved name, and the
attacks are marked `<- BLOCK`.
