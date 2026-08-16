## v0.20.129

#minigame-bgbtl: **the movie stops talking over the fight** — and the push
utility stops dying on a lock file left behind three days earlier.

---

### The fight's audio channel belongs to the fight

`disc01_33h.avi` plays behind the entire Garden battle, and it has an audio
description track. From the 2026-08-15 log:

```
16:14:07  [FMV_AD] Cue 2: Turquoise energy and fire flash; Galbadia Garden presses in at the edge.
16:14:08  "Heavy punch ready. Let go of block and press D."
```

**One second apart, both spoken with interrupt, and only one of them is
something you can act on.** The heavy punch already has a priority path
precisely because the health report was cutting it off; it should not also have
to out-shout a description of the scenery.

The briefing has suppressed FMV narration since v0.20.123 — but `EndBriefing`
resumed it, and `EndBriefing` is what *starts the round*. So the suppression
covered the one stretch where nothing was happening and lifted for the one
stretch where everything was.

**Narration is now held from the moment the briefing opens until the round is
decided**, and given back by whichever of the four things decides it: the win,
the loss, F9, or the module disarming. `Disarm` resumes unconditionally, so
there is no path that can leave a player's descriptions switched off.

**Nothing is lost.** `fmv_audio_desc` consumes each cue on its timestamp rather
than queueing it, so a suppressed cue is dropped, not deferred — and every cue
worth hearing in that movie (Rinoa on the cable, the soldiers exchanging fire,
the run for the entrance) falls *after* the win, when narration is back on.

### The push utility now clears a stale lock instead of stopping dead

Three pushes have failed on:

```
fatal: Unable to create '.../.git/index.lock': File exists.
ERROR: git add failed.
```

Each time from a lock left by a git process that had already gone away —
2026-08-02, 2026-08-12, 2026-08-15. Git cannot tell a crashed process from a
live one, so it refuses, and the failure names nothing actionable.

`tasklist` can tell them apart. `push_to_github.bat` gains a Step 0: if
`.git\index.lock` exists **and no `git.exe` is running**, it is a leftover and
gets removed, with a line in `git_latest.log` saying so. If a git process *is*
running the lock is real, so it is left alone and Step 1 fails with git's own
message. (`Utilities/` is git-ignored, so this stays local.)

**The recurring cause is worth writing down:** those locks came from git
commands run against the mod folder through the Cowork desktop bridge, which
cannot unlink them — `warning: unable to unlink '.git/index.lock': Operation not
permitted`. Every such command left one behind. Git through that bridge is off
the table; the push utility runs natively and is unaffected.

### Verification

* `minigame_bgbtl_compile`: **0 errors, 0 bad**, with the policy written as its
  own assertion — narration must survive `EndBriefing` (the check fails on the
  old code and passes on this one), and must be back on after both the
  HP-driven win and the disarm.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` **untouched at 81,645** — 275 from the hard fail.

**NOT MSVC-built.**

### BAT

Play the fight normally. **From "Game start." to the moment you win, lose or
press F9, the only voice should be the mod's** — block cues, "Blocked.", the
health reports, "Heavy punch ready". No descriptions of the battle scenery
mid-round.

The moment you win (or press F9), the descriptions come back and the rescue
scene should narrate as it always has: Rinoa on the cable, the soldiers below,
the run for the entrance. Grep `[FMV_AD]` — suppressed cues are logged with
`[SUPPRESSED -- the mod holds the channel]`, so the log shows exactly which ones
were held and when narration resumed.
