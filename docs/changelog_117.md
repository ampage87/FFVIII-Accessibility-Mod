## v0.20.117

#minigame-bgbtl: **the fight loop found, and the host clock is frozen.**

Two answers out of the exe and the scripts. No BAT spent.

### The input dispatcher

`bgbtl_1` label 28 — misnamed `squ_punching0` by the SYM, but it is **the fight
loop**. It reads var 356 (foe HP), loops, and tests four bit masks, requesting
one of Squall's actions for each:

```
mask  16  ->  REQ 29  squ_punching1
mask  64  ->  REQ 31  squ_guarding0    <- THE BLOCK
mask 128  ->  REQ 32  squ_punched_up0
mask  32  ->  REQ 30  squ_kicking0
```

The guard is requested from **exactly one site** (dword 319), so a block really
does go through that script — if bit 64 reaches whichever button word is read.

### Tap or hold? The exe says holding is *not* obviously wrong

You asked me to confirm this from the exe, and here is what it actually shows.
The engine keeps **two** button words:

```
0x01CD01F8   valid_buttons      -- currently held, level
0x01CD0200   confirmed_buttons  -- edge, PLUS AUTO-REPEAT
```

The repeat is visible in `engine_eval_is_button_pressed` (`0x00468540`): from
`+0x67` it compares an elapsed counter against a threshold and, when exceeded,
re-copies the held state into `confirmed` and sets a repeat flag.

**So a held key fires once immediately and then again on every repeat.** Your
instinct may well be right — holding would look like *intermittent* blocking, not
none at all.

Which word the fight loop reads is what decides it, and **I could not settle that
from the opcode table** — the same table that has now misled me three times
(`squ_timeover0` as a failure, the per-field labels, the "never fired" guard).
I'm not going to guess a fourth time.

**So both words are logged**, change-only, with the three masks broken out:

```
[BGBTL-BTN] +4218ms  held=0x0040 edge=0x0000   held(punch16=0 guard64=1 kick32=0) edge(...)
```

One run answers tap-versus-hold from data.

### F10: the host field's clock is frozen

Your last log has `clock=140` for the **entire** host phase — it never moves.
That is why pressing F10 there left you *"hearing the sound of Squall being
punched for quite a while"*: there was nothing to shorten. `bg2f_31`'s ladder
gates only on 1 and 9.

**That phase ends when the battle FMV finishes**, and the script jumps to
`bgbtl_1`. So the only way out is to end the movie.

New **`FmvSkip::RequestSkip()`** — the same three writes the Backspace path
makes, factored out so a code-driven skip and a player-driven one cannot diverge.
F10 now calls it **once** when pressed outside `bgbtl_1`. Then the existing clock
bump to 579 ends the round, and the game plays its own rescue scene.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now asserts the
  host-phase behaviour directly — `RequestSkip` called **exactly once** across 30
  ticks, and the frozen host clock left untouched — alongside the guard-flag
  detector (two rising edges, two blocks, no double-count) and every earlier
  check.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` is 81,602 bytes — 318 from the hard fail. SPLIT
BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **F10 should now cut through the host phase** instead of leaving you standing
   in it — expect "Skipping video" then the round ending shortly after.
2. For the block: **try holding A for one stretch and tapping it for another.**
   `[BGBTL-BTN]` records both button words and `[BGBTL-STATE]` records the guard
   flag, so whichever works will be visible — and so will which word the game
   is reading.
