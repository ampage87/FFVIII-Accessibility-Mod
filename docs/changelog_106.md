## v0.20.106

#minigame-bgbtl: **pause, brief, confirm.**

> *"It was really hard to hear the controls with everything going on — the game
> battle sounds, the punching sounds of the mini-game, the audio descriptions,
> the instructions, and the block announcement. Would it be possible to basically
> pause the game when the scene starts to inform the player how to do this
> mini-game, and upon pressing X the confirm key in-game it resumes."* — Aaron

Five things were competing for one pair of ears at the exact moment the player
has to learn the controls. Speaking louder or shorter does not fix that.
Removing four of the five does.

### The pause is the engine's own pause point, not an invention

`field_main_loop` (0x0046FEE0) calls `field_main` (0x00471F70) once per frame at
`+0x148` — and the disassembly shows it **skips that call entirely** when FF8's
own pause flag `[0x01CD2EBC]` is non-zero:

```
+0x144  cmp   eax, edi            ; eax = [0x1CD2EBC], the pause flag
+0x146  jne   0x470033            ; paused -> the pause-menu path
+0x148  call  0x471f70            ; NOT paused -> field_main
```

So not calling `field_main` is precisely what the game does when it pauses
itself. We do it with a **one-byte `RET` at the entry** — atomic on x86, no
trampoline, one write to undo. `field_main` takes no arguments (the call site
pushes nothing), so a bare `RET` is a correct no-op.

Rendering, input and the frame limiter all live **outside** it and keep running.
Only field logic stops: the script VM, the entity animation, the mini-game timer
**and its punch sounds.**

**Verified by signature, not by faith.** The five entry bytes are checked against
`mov eax, [0x01CE4A64]` (`A1 64 4A CE 01`) before anything is written. On
mismatch the briefing still speaks — it just doesn't pause, and says so in the
log.

### The other four noises

* **The fight and its punch sounds** stop with `field_main`.
* **FMV narration** is suppressed for the duration, via a new
  `FmvAudioDesc::SetSuppressed`. Cues still advance and **log** on schedule so
  the track stays in sync; they just don't reach the screen reader. An AD cue
  firing mid-briefing would interrupt it — the same defect class v0.20.103 fixed
  for the mod's own cues.
* **The music** ducks by itself, because the existing ducker follows
  `ScreenReader::IsSpeaking`.

### The flow

```
legend appears
  -> freeze
  -> "Garden battle. You fight the soldier hand to hand.
      W punches. A blocks. X kicks.
      I will say block when he attacks, and call out health as it changes.
      Press X to begin. Press F9 to hear this again."
  -> X
  -> "Game start."
  -> thaw
```

**F9 repeats the briefing.** There are no cues to toggle while frozen, so the key
is free — and a blind player who missed a word shouldn't have to guess or
restart the scene to hear it again.

**The resume waits for the key to be released.** X is also Kick; thawing on the
press would throw a kick the player didn't ask for on the very next frame.

A retry from the Game Over screen re-shows the legend, which re-opens the
briefing.

### The freeze cannot strand the game

This is checked, not reasoned about. A mod that leaves `field_main` patched
hasn't inconvenienced the player — it has ended their session.

1. **30-second cap** inside the briefing.
2. **Thaw on every `Disarm` path**, including the 5-minute arm cap.
3. **A watchdog** called from `PollBattlePauseResume`, which sits *above*
   `Update()`'s `IsOnField()` / `HasFieldStateArrays()` early-returns.
   `GardenBattle::Update()` owns the un-pause and lives *below* them — so if
   either gate ever went false while frozen, the cap would never run.

### Known and accepted

**Freezing field logic does not stop the FMV.** The movie runs on during the
briefing, so phase one of the fight is correspondingly shorter. That costs the
player nothing — they're listening, not fighting, and not being punched either —
and the fight continues in `bgbtl_1` regardless. The freeze duration is logged so
the BAT can measure the drift.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now **maps real pages**
  at `0x00471000` and `0x01CFE000` so the probe exercises the *actual* patch
  path — signature check, `0xC3` written, original byte restored — and asserts
  the byte is back on **both** exits (player confirm, and disarm without
  confirming). The HP replay and legend-filter checks still run.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` is 81,489 bytes — 431 from the 81,920 hard fail**
(+85 for the `FmvAudioDesc` forward declaration). **SPLIT BEFORE THE NEXT EDIT
THAT NEEDS ROOM.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. When the legend appears, **everything should go quiet** and the briefing
   should play alone — no punch sounds, no FMV narration, music ducked.
2. **F9** should repeat it.
3. **X** should resume, with **"Game start."**
4. Then the fight as in v0.20.105 — Block cues and health.
5. If anything goes wrong, **the game must un-pause on its own within about 30
   seconds.** That is the one failure mode worth knowing about, and it is
   deliberately bounded.

Grep `[BGBTL] field PAUSED` / `field RESUMED` — the gap between them is how long
the movie ran ahead.
