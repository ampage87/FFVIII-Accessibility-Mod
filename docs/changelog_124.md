## v0.20.124

#minigame-bgbtl: **the box moves up, the double-tap goes, the win is called when
it is won, and the Game Over menu stops pretending to be a place.**

> *"Beautiful! I took a couple F11 screenshots... I did a power punch and it
> worked fine. I deliberately lost the game and the game controls announced
> properly when the mini-game restarted."* — Aaron

### The screenshots: nothing clipped, but something on top

The box works and the game's own measurer sized it correctly — every line is
inside it. What the shots show is a **collision the mod caused itself**: FF8
draws its own "W Punch / A Block / X Kick" legend at the lower left, and a
centred box lands right on it. Two lines — *"most of the soldier's health."* and
*"Two of those win the round."* — are covered by that panel.

Fixed by shrinking and moving:

* **Six lines instead of eleven**, no blank spacers.
* **Parked at the top** (`y = 8`) instead of vertically centred, which clears
  both the legend and the two HP bars along the bottom.

```
GARDEN BATTLE
Block A   Punch W
Kick X   Heavy punch D
Hold Block. 3 blocks in a row
arm the Heavy punch. 2 of those win.
Enter start   Space repeat   F9 skip
```

### The double-tap is gone

> *"Let's get rid of the double-tap F9 skip option. The single-tap option seems
> to work well and ensures the player hears the full FMV."*

It did work — requested at 14:33:14, out at field 675 by 14:33:18 — but it
existed to save a wait that only mattered while the fight was still dangerous,
and it cut the rescue scene to do it. One press, one behaviour.

That also removes the last code in this module that ever wrote **var 80**. The
probe now asserts it outright: across the whole round, at four different clock
values, twenty ticks each, the fight clock must come out exactly as it went in.
That is the standing guard against v0.20.119's 82 seconds of dead scene.

### "You win" now means you have won

> *"The You Win announcement still comes when the player reaches G-Garden. That
> should announce either when the enemy hits 0 HP or the player uses F9."*

The reason it moved to the field transition in the first place is worth keeping
in view: v0.20.111 announced the win at foe-HP-zero, and the foe then sat at
0/600 for **seventy seconds**, kept swinging, and Squall bled 1000 → −31 and
**lost**. Zero foe HP does not end the fight; the round is decided at clock 580
on `foeHP < squallHP`, so if Squall reaches zero too, the comparison goes against
him.

So the early announcement is made **honest by making it true**: putting the foe
at zero now engages the same protection F9 uses — attacks vetoed at the REQ,
both HP values pinned — and *then* says "You win." Nothing can take it back
between there and the resolution. The player did the work; the rest of the clock
is ceremony. F9 marks the win the same way, and the field-675 line only speaks
if neither already has.

### The Game Over screen is a menu, not a place

Field 95, `testbl6`, is the "Try again / Try again with HP+200" choice.
Announcing a location name over it reads as somewhere the player has arrived
rather than the decision in front of them. It is now on a short suppression list
in `field_announce.cpp` — a list rather than a blanked table entry, so
`FIELD_DISPLAY_NAMES` stays a pure id-to-name map and the reason for each
omission is written next to it.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors.** New: the skip must never
  write the fight clock at any point in the round; the win must be called from
  HP **and** must engage its protection when it is.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` unchanged at **81,645 — 275 from the hard fail**.
  `field_minigame_bgbtl_skip.inl` down to 8,702 with the double-tap removed.

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **F11 the Game Controls again** — the box should sit at the top with nothing
   over it, and the game's own legend should have the lower left to itself.
2. **Win on merit** — "You win." should arrive the moment the soldier's health
   hits zero, and nothing should hit you after it.
3. **F9** — one press, says "You win", and the rescue plays out in full. A second
   press should do nothing but say "Already skipping."
4. **Lose on purpose** — the Game Over screen should announce its options with no
   field name in front of them.
