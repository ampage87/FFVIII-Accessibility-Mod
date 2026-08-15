## v0.20.104

#minigame-bgbtl: **the fight starts fifteen seconds before its field loads.**

Aaron took two F11 screenshots at the exact moment he could not hear anything,
and they overturn the v0.20.103 diagnosis.

### What the screenshots prove

At **21:07:25** and **21:07:28** the mini-game is fully on screen — the
`W Punch / A Block / X Kick` legend box, both named health bars, and Squall's bar
visibly lower in the second shot than the first. The fight is live and damage is
being taken.

**Field 152 does not load until 21:07:40.**

```
21:07:16  [FmvSkip] FMV started: disc01_32h.avi
21:07:23  TTS "Punch Block Kick"          <- the game's own legend appears
21:07:25  F11 #1 — fight in progress, both bars down
21:07:28  F11 #2 — Squall's bar lower still
21:07:38  MAPJUMPO ... destField=152 (bgbtl_1)
21:07:40  [BGBTL] entered the mini-game   <- the module armed HERE
```

The engine keeps reporting the **previous** field for fifteen seconds while the
fight plays over the FMV. Keying the module off the field id therefore missed the
opening of every single fight — which is exactly what *"did not hear the controls
or the damage announcements until several hits into the fight"* describes.

**v0.20.103 blamed the FMV audio description and was wrong.** The AD really did
finish before the field loaded — that part of the analysis holds — but **the late
signal was the field load, not the speech.** Aaron was right that the FMV was
involved; the mechanism was not the one either of us named.

### The trigger is now the game's own legend

`"Punch"` + `"Block"` + `"Kick"` in one decoded window, fed from `field_dialog`'s
`show_dialog` hook — the same wiring `ChaseAskOverlay` and `TrainModeAskOverlay`
already use. Three `strstr` calls and out.

That string is the instant the mini-game UI appears, in whatever field happens to
be hosting it. The field id survives only as a backstop, and **the host field is
recorded rather than assumed** (144 this run) — because the lesson of this whole
arc is that the field hosting the fight is not the field the fight is named
after.

Disarm on the first field outside `{host, 152, 95}`, plus a hard 5-minute cap.
Arming on a string does mean the REQ hook is briefly live in an ordinary Garden
field, which is a real widening of the v0.20.102 fence. It is accepted because
that string appears nowhere else in the game and the exit conditions are
bounded — but it is a widening, and it is named here rather than buried.

### HP is not in the field variable block — settled

The v0.20.103 change detector watched all 4,096 bytes across a full **92-second
run**, and it was a **win** — the first winning trace ever recorded.

129 bytes moved. The scans taken at the exact moment of each of the **twelve
landed hits** show only this:

```
soldier-hit 1 changed: [80]26->27
soldier-hit 1 changed: [80]58->61
soldier-hit 1 changed: [80]90->93     ... and so on, every time
```

Bytes 4/5/8/9/12/80/81/180/181/244/245/3380/3381 step ~20 per tick. **Frame
counters, not health.** Nothing in the block tracks damage.

HP is engine-side in a struct this module has not found. Percentage
announcements need a different hunt — the HP-bar draw call is the obvious next
place to look. The differ now filters the counters so the log stays readable.

### The winning run killed the last bad cue

`gal0::g0_fall0` fired **exactly once, at +297 ms, in a run Aaron won.** It is
never the defeat signal — v0.20.103's time gate was treating a symptom.
**"Soldier down." is gone.** The win is announced from the observed transition to
field 675 instead: **"You win."**

Also measured, and useful:

* `squall::squ_punched_up0` **never fired** — Aaron took no damage at all.
* The soldier attacks **rarely**: 3 kicks and 1 punch in 92 seconds, against 12
  landed hits from Squall. **The announcement load is far lighter than any model
  predicted** — including the offline sim that started this whole design.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now *exercises* the
  legend filter rather than only compiling it — matches the legend, rejects an
  ordinary line of dialogue.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.
* **`field_navigation.cpp` is unchanged at 81,404 bytes** — every line of this
  build went into the `.inl`. Still 516 from the hard fail; the split is still
  owed. `field_dialog.cpp` 16,405 and `field_dialog_show_dialog.inl` 11,442,
  both far inside the guard.

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **The controls should now arrive while the FMV is still playing** — right
   after the game's own "Punch Block Kick" is read, not fifteen seconds later.
   That is the whole build.
2. No "Defeated." and no "Soldier down." at any point.
3. **"You win."** when the fight ends in your favour.
4. **F9** still toggles speech/tone mid-fight; volume must not change.
5. If you retry from the Game Over screen, the keys should be spoken again.

Grep `[BGBTL] ARMED` — the timestamp on that line, compared against
`[FmvSkip] FMV started`, is the number that says whether this worked.
