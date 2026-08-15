## v0.20.112

#minigame-bgbtl: **the win condition is not what it looks like, and the block
window is below reaction time.**

Three runs, and the log answers all three.

### Run 2 — blocking: zero successful blocks, and it isn't your fault

`squall::squ_guarding0` **never fires once** in the whole attempt, in either
field. Every single attack lands.

The reason is in the timings, and it corrects a number this module has carried
since v0.20.102. The gap from the soldier's attack REQ to the damage:

```
bg2f_31   +203 -> +375   (172 ms)
          +891 -> +1063  (172 ms)
          +1375 -> +1422 ( 47 ms)
          +1906 -> +2156 (250 ms)
          +2734 -> +2922 (188 ms)
bgbtl_1   +5563 -> +5828 (265 ms)
          +7469 -> +7797 (328 ms)
          +16359 -> +16734 (375 ms)
```

**The "700 ms reaction window" was measured to `squ_timeover0`, which is the
round timer — not the hit.** The real warning is **47–375 ms, median around
200**.

A 180 ms tone starting ~200 ms before the punch has barely finished playing when
the punch lands. **Blocking on the cue is below human reaction time.** A sighted
player has the wind-up *animation*, which begins earlier than anything the script
exposes.

That's a real gap, and no amount of making the tone louder or earlier closes it —
there is nothing earlier to fire on. An auto-block assist would close it. See the
question at the end.

### Run 3 — the skip: foe HP at zero is not a win, and the mod said it was

> *"I heard it say I won but then I still heard the enemy hitting me until my
> health hit 0 and when I tried to hit back nothing happened."*

```
23:38:44  Foe 0/600      <- "Foe defeated. You win." announced here
23:38:59  Squall 1000/1000  Foe 0/600
23:39:23  Squall  680/1000  Foe 0/600
23:39:53  Squall  -31/1000  Foe 0/600   <- actual loss, 69 s later
```

**The foe sat at zero for seventy seconds, kept attacking the whole time, and
Squall was ground down and lost.**

So the fight ends when **Squall** reaches zero (immediate loss), or when the
**timer** expires with most-HP-wins. Zero foe HP only means the player can't lose
the HP *race* — it does not stop the foe swinging. The game's own hint said as
much all along: *"When time is up, the one with the most HP is the winner."*

**"You win." now fires only on the transition to field 675** — the only win
signal ever actually observed. Foe-at-zero now says something true instead:
**"Foe out of health. You are ahead."**

### The skip is now a mode, not a write

One-shot zeroing was never going to hold. **F10 now pins the foe at 0 and Squall
at full every tick** — not on the 250 ms health poll, because one hit at low HP
is the whole difference — until the timer ends the fight.

Nothing is forged, so nothing can desync. That's what crashed in v0.20.110.

The outcome check also moved **above** the "nothing changed" gate in
`PollHealth`: a decision this important must not depend on a rounded percentage
having moved.

### Run 1 — the good news

The tone fires just before the hit, in the host field, from the first attack. The
v0.20.111 per-field label fix works.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now **simulates the
  failure** — engage the skip, then take 40 hits of 90 damage while the script
  re-inflates the foe, and assert Squall is still pinned at 1000 and the foe
  still at 0 — plus the corrected foe-at-zero reporting, alongside every earlier
  check.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **F10 should now actually work.** Expect *"Skipping. You cannot lose now. Wait
   for the fight to end."*, your health pinned at full, and the fight running to
   its timer before Galbadia Garden loads. It may take a minute or two — that's
   the game's own ending, not a hang.
2. **No "You win." until you're actually in Galbadia Garden.** Foe-at-zero should
   say *"Foe out of health. You are ahead."*
3. Blocking is still expected to fail — that's the open question below, not a
   regression.
