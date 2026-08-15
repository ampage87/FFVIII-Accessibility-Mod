## v0.20.116

#minigame-bgbtl: **you were right to push — I misread the round driver.**

> *"Is it possible that you are mistaking punches for blocks? I didn't do any
> punches, but I did do blocks."*

Back to the scripts, and you're right on the substance.

### `squ_timeover0` is not "you failed to act"

Label 33's body reads Squall's HP (`OP_0x11 354`), branches on ≤ 0, clears the
damage flag 1028 and the state bytes 340/1030/1032, and ends in a wait loop.

**It is the per-round driver. It fires once a round no matter what happens.**

Reporting 44 of them as 44 failures was wrong. The timing said so too — its
offset from an attack ranges **172–1328 ms**, where a genuine response label
(`squ_punched_up0`) is tight at 328–563 ms. I read a round boundary as a miss.

### And the detection itself may be blind

`squ_guarding0` (label 31) really *is* the guard — its body writes **var
1031 = 1**, exactly mirroring `bg2f_31`'s guard writing 1033.

**But a REQ is only one way that flag can be set.** If the engine's own input
handler sets 1031 directly, a block works perfectly and is **completely invisible
to a REQ hook**.

That is the simplest explanation that fits both your account and the trace — and
it means the "regression since v0.20.105" I was about to chase may not exist at
all. **The pause-alternation experiment I'd started building is dropped**: its
premise no longer holds, and it would have degraded your run to test a conclusion
I couldn't justify.

### So the flags are watched directly

`[BGBTL-STATE]` — vars **1027–1034** every tick, change-only, logged with the
fight-relative timestamp:

```
[BGBTL-STATE] +4218ms  var1031 0 -> 1   <- GUARD FLAG
```

**Var 1031 going 0 → 1 is a block, whether or not a script was REQ'd for it**,
and that is what **"Blocked."** is announced from now. The REQ path is kept, but
demoted to recording *which* mechanism is in play.

### And a real key trace

`[BGBTL-KEY]` is now **change-only**. The v0.20.115 version sampled W/A/X only at
the instant of an attack cue, which cannot see a tap landing 150 ms later — so
"A held at 2 of 66" said nothing about whether you pressed anything. Presses and
the game's response now sit side by side in one trace.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now drives the
  guard-flag detector directly — two rising edges on var 1031 produce exactly two
  blocks, and a held flag is not double-counted.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### Also confirmed in your last run: the fight-clock skip works

```
[BGBTL] SKIP: fight clock 326 -> 579 (ending the round now; the rescue scene follows)
[BGBTL] SKIP done -- clock 601 is past the resolution, the game's own ending is running
```

v0.20.114's clock advance fired exactly as designed.

### BAT

Play it as you did — block when you hear the tone.

You should hear **"Blocked."** when it lands. If you don't, the log now answers
why on its own: `[BGBTL-KEY]` shows every press you made, and `[BGBTL-STATE]`
shows whether the game's guard flag responded to it. Between those two there is
no longer anywhere for this to hide.
