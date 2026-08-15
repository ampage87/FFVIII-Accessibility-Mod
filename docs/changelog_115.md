## v0.20.115

#minigame-bgbtl: **correction — blocking works. Tapping works; holding does
not.**

### I was wrong, and my own earlier trace proves it

v0.20.113 reported `squ_guarding0: NEVER` and I concluded blocking was
unreachable by a blind player. That was wrong.

**The v0.20.105 winning run's label histogram — which I generated myself —
contains `8 label=31 squall::squ_guarding0`.** That build armed on field 152
only, so every REQ in that trace is `bgbtl_1` and the name is correct.

**Aaron blocked eight times in the run he won.** Blocking is reachable and always
was. I checked the recent logs and not the one that mattered.

### What the v0.20.113 log actually shows, read attempt by attempt

In attempt 1 — the deliberate hold test — A reads held at **six consecutive
attack cues**, and at none of the cues before he started:

```
+828    fld=144  g0_punching0    A=0
+7500   fld=144  g0_kicking0     A=0
+16890  fld=144  g0_guarding0    A=0
+19562  fld=144  g0_kicking0     A=1   <- holding from here
+23828  fld=152  g0_punching0    A=1
+24265  fld=152  g0_kicking0     A=1
+26328  fld=152  g0_kicking0     A=1
+28406  fld=152  g0_guarding0    A=1
+30875  fld=152  g0_kicking0     A=1
```

During that held stretch, Squall's labels in `bgbtl_1` were:

```
+23593  squ_punchkeyscan0
+23796  squ_punching0
+23859  squ_punched_up0     \
+24968  squ_timeover0        |
+26296  squ_punched_up0      |  four hits taken,
+27031  squ_timeover0        |  four rounds timed out,
+28406  squ_punched_up0      |  zero guards
+28968  squ_timeover0        |
+30296  squ_punched_up0      |
+31578  squ_timeover0       /
```

**The hold was real, sustained, and produced nothing.**

### The conclusion

**The guard fires on a key-press edge, not a held state. Tap the block key once
per attack — holding it down is exactly equivalent to never pressing it.**

That reconciles everything: v0.20.105 (normal play, tapping) got 8 blocks;
v0.20.113 (deliberate holding) got 0.

The briefing now says so in as many words:

> *"Tap block for each attack. Holding it down does not work."*

### And a block is now announced

**"Blocked."**

You have no other way to know your timing landed — the game gives a blind player
nothing that distinguishes a block from a punch that missed. **Without that
feedback a 200 ms window cannot be learned.** The block summary counts them too.

### Also confirmed this run: the skip reached the rescue scene

This is the whole of your ask from last round, and v0.20.112's HP hold already
delivers it:

```
00:08:56  SKIP engaged, foe 0, Squall 800/800
00:09:44  disc01_33h.avi  — the rescue FMV, with its existing audio description
          "Rinoa grips a cable and slides down from the Garden's underside"
00:11:04  G-Garden - Back Entrance
00:11:05  "You win."
00:11:08  Rinoa "Squall!" / "Thank you."
```

### Note: this BAT ran v0.20.113

**The v0.20.114 fight-clock work is not yet tested** — you built before it
landed.

And measured against this run it's worth less than I advertised: of the ~2
minutes, only the **13 s inside `bgbtl_1`** is shortenable. The 35 s before it is
the battle FMV running in the host field (whose ladder gates only on 1 and 9),
and the 80 s after is the rescue scene itself, which you want.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, all v0.20.114 checks intact —
  clock advance to 579, skip release past the resolution, the 40-hit hold.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

**Tap A once per tone.** Don't hold it. You should hear **"Blocked."** when the
timing lands — that's the feedback that makes the window learnable.

Then grep `BLOCK SUMMARY` for the count. Eight blocks was your v0.20.105 score
with no cue at all; with the tone you should beat it.
