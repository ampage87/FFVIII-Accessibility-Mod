## v0.20.126

#minigame-bgbtl: **Aaron was right — the mod was carrying the soldier's damage
between attempts, and it found the bug by the fight feeling too easy.**

> *"When I did a second attempt just now, I only did a single power punch and it
> caused me to win. I thought it usually took two. The mod isn't causing a
> problem where the enemy's partial health is being carried over between
> attempts, is it?"*

It was. The log names the line:

```
15:46:03  Squall reached -20 -- loss              (the soldier left on 248/600)
15:49:47  briefing HP frozen at Squall 604, Foe 248
15:49:54  briefing HP drifted (Foe 248->600) -- restoring      <-- here
15:50:07  squall::squ_punching1
15:50:08  foe HP reached -195 -- WIN
```

**That "drift" was not a leak — it was the game.** `squall::squ_out0` runs
`PUSHI 600 ; WRVARW 356` as part of setting the scene up for a fresh attempt,
and the briefing's HP hold reverted it to the 248 the soldier had been left on
when the previous attempt ended. One heavy punch then finished a soldier who
started the round at 41% health. The retry was easier than it should have been,
and it was the mod's doing.

### Why the hold existed, and why it should not any more

It was written in v0.20.118 to stop the enemy scoring hits while the Game
Controls were up, back when nothing else could — the briefing then was a
`field_main` freeze that stopped the script but not, apparently, the damage.

**v0.20.123 replaced that job entirely.** The briefing now vetoes the soldier's
attack REQs, so `g0_punching0` and `g0_kicking0` never run, so they never REQ
`squ_punched_up0`, so `squ_hpcalc0` is never reached and **no damage is
possible**. Two mechanisms for one job, and the redundant one was quietly
overwriting the scene's own initialisation.

**HoldBriefingHp is deleted.** One mechanism, and it is the one that cannot get
this wrong: if no attack runs, no HP moves, and everything the *script* writes is
left alone.

### The screenshot

Clean. Seven lines, all inside the box, clear space under the last one, the
game's own legend untouched below it. That one is done.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, with the regression written
  as its own assertion — a briefing that opens on a leftover `Foe 248` must let
  `squ_out0`'s `600` stand, and Squall's retry bonus with it. It fails on the
  old code and passes on this one.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` unchanged at **81,645 — 275 from the hard fail**.

**NOT MSVC-built, NOT BAT'd.**

### BAT

Lose on purpose, then look at the retry: the soldier should be back to full, and
it should take **two** heavy punches again. Grep `[BGBTL-HP] armed` on the second
attempt — the foe should read 600/600, not whatever you left him on.
