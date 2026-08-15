## v0.20.113

#minigame-bgbtl: **answering two questions, and instrumenting the one I can't.**

### "Isn't there some way to tell when the enemy is about to start their wind-up?"

**The REQ *is* the wind-up.** The soldier's animation script is invoked at the
moment we hook — that instant is the frame the animation starts, and the hit
lands 172–375 ms later.

So a sighted player is **not** getting an earlier signal than your tone. They're
getting a **richer** one: which attack it is, and a visual rhythm to anticipate.
Their window is the same ~200 ms; they just have more in it.

There is nothing in the script before the REQ to fire on — the director decides
and REQs in the same block. An earlier cue isn't available, and claiming
otherwise would mean inventing a mechanism that isn't there.

### But one thing that would settle it was never measured

**`squall::squ_guarding0` has never fired once, in any run.**

That leaves two very different explanations, and the trace can't distinguish
them:

* the presses arrived, but too late for the window, or
* they never reached the game at all

**New `[BGBTL-KEY]` instrument.** Raw W/A/X state sampled on every soldier
attack, the guard script watched for directly, and a summary line on exit:

```
[BGBTL] BLOCK SUMMARY: 14 attacks cued, A held at 9 of them,
                       squ_guarding0 fired: NEVER
```

One run decides whether an auto-block assist is the answer, or whether something
simpler is wrong.

**Worth trying first, because it costs nothing: hold A down** rather than tapping
it at the tone. If the guard is a held state rather than a timed press, that
alone makes the fight playable — and the summary line will say so.

### "Is that going to stop me getting hit?"

**No — and the announcement now says so.**

The v0.20.112 hold pins your HP at full every tick, so the damage is *undone*.
But the foe keeps swinging and the game keeps playing its hit sound. A player
told "skipping" who then hears themselves being punched will reasonably assume it
failed — which is exactly what v0.20.111 did to you.

F10 now says:

> *"Skip on. He will keep swinging and you will still hear it, but you cannot
> lose. Wait for the round to end."*

and the briefing says *"F10 makes the fight unloseable, though it still plays out
to the end."*

**Making the hitting actually stop means ending the fight early, which means
finding its timer.** I haven't located it, and I'm not going to guess at it —
that's the next investigation if you want F10 to be instant rather than just
safe.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, all v0.20.112 checks intact
  including the 40-hit skip-hold simulation.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

**One run, one question: hold A down through the fight** — not taps at the tone,
just hold it whenever you aren't punching.

Then grep `BLOCK SUMMARY`. That single line tells us whether blocking is
reachable at all:

* `squ_guarding0 fired: YES` → holding works, and the fight is playable as-is
* `fired: NEVER` with `A held at N of them` where N > 0 → the presses reach the
  game but the guard needs something else; auto-block is the answer
* `A held at 0` → the presses aren't reaching the game at all, which is a
  different and more fixable problem
