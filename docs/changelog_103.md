## v0.20.103

#minigame-bgbtl: **the trace corrected almost every guess.**

The v0.20.102 instrument worked. What it measured invalidates most of what
v0.20.102 announced — which is exactly what an instrument-first build is for.

> *"Not hearing the announcement of the keys or the block until the game is
> partway done. This may be because of the audio descriptions for the background
> fmv."* — Aaron

### The FMV was not the problem — the mod was talking over itself

The mod log is unambiguous:

```
20:41:47  [FMV_AD] Started playback: disc01_32h.vtt (3 cues, 18.2 seconds)
20:42:00  [FMV_AD] [13.0s] Cue 2: ...            <- last AD line
20:42:07  [FMV_AD] Stopped playback
20:42:10  [fieldload] id=152 name='bgbtl_1'      <- the fight starts here
```

The audio description finished **three seconds before the field loaded**, and its
last cue spoke ten seconds before. It never overlapped the fight. **No AD
suppression is needed and none has been added.**

What actually happened is in the next four lines:

```
20:42:10  "Defeated."                         <- the label-47 bug, below
20:42:11  "Block"                             <- label 48 at +328 ms
20:42:11  "Punch, W. Block, A. Kick, X."      <- our 400 ms entry delay
20:42:11  "Block"                             <- label 49 at +766 ms
```

The fourth line interrupted the third about 360 ms in. Aaron heard roughly
*"Punch, W."* and then nothing.

(Worth noting separately: at 20:41:54, mid-FMV, the mod spoke the game's own
on-screen legend — **"Punch Block Kick"** — buried between two AD cues. The
information existed; it just arrived sixteen seconds early and unlabelled.)

### Four corrections, all measured

**1. `gal0::g0_fall0` (47) is not defeat.** It fires at **+125 ms in both runs**
as scene setup — the soldier drops into frame. v0.20.102 therefore announced
*"Defeated."* one tenth of a second into every attempt. Now gated on elapsed
> 4 s, and logged as setup when it isn't.

**2. The reaction window is 700 ms — measured, not guessed.**
`squall::squ_timeover0` is the per-round expiry and lands a strikingly
consistent interval after the soldier acts:

```
run 1   +766 -> +1469 = 703 ms      run 2  +2875 -> +3578 = 703 ms
run 1  +9063 -> +9766 = 703 ms      run 2  +5141 -> +5703 = 562 ms
```

This is the number the cue toggle existed to find. A one-word cue only just
fits inside it; a sentence does not.

**3. The fight is ~10 s with events 1.2–1.7 s apart** — not the 900 ms exchange
rate the offline announcer sim assumed. There is far more speech budget here
than predicted, which is why hit reports now ship as well as the Block cue.

**4. The soldier acts on 48 / 49 / 50.** `g0_punching0` (48) fires once, in the
opening exchange; `g0_kicking0` (49) is the recurring attack; `g0_guarding0`
(50) is **him** defending and gets no cue at all.

### The controls line is now first, and protected

Spoken at **0 ms** (was 400) as **"W punch. A block. X kick."** — key first,
because the key is the part that has to survive if anything goes wrong.

Every cue is suppressed until `ScreenReader::IsSpeaking()` goes false, with a
3.5 s hard cap. Gating on the real speech state rather than a guessed interval
matters because Aaron's NVDA rate is far faster than any default — a fixed timer
would waste most of the opening. **Each suppressed cue is logged**, so the gate's
cost is visible rather than invisible.

### Hit reports ship; health still does not

* `g0_punched_up0` → **"Foe hit."**
* `squ_punched_up0` → **"You hurt."**

This answers the actual complaint behind the health request: the game plays the
**same** hit sound for both fighters, so it carries no information about who took
the damage. These do.

A report will not interrupt a Block cue less than 450 ms old. Only the cue is
time-critical; the report can wait.

### The HP hunt restarts, properly

v0.20.102 dumped two guessed windows and **both were dead** — 1020..1040 was zero
all fight except a transient 1 at 1031/1032, and 330..350 never moved at all.

Replaced with a **change detector over 4,096 bytes** of the variable block. It
reports only what moves, and prints one summary line at the end listing every
byte that changed during the whole fight. HP cannot hide from that, and it stays
quiet — because almost nothing in there changes.

### F7 was already taken, and the log proves it

```
20:42:20  ScreenReader: [TTS] "Block cue: tone." (interrupt)
20:42:20  ScreenReader: [TTS] "Music volume 30 percent" (interrupt)
```

One press, both handlers. F7 is `GameAudio::VolumeDown` in `dinput8.cpp`.

**Rebound to F9.** A full sweep of the source shows F1–F8, F11 and F12 all bound
and **F9 and F10 unbound**; F9 is chosen because F10 activates the window menu on
Windows.

### Also

The tone cue now runs `Beep()` **on a worker thread**. `Beep()` is synchronous
and was stalling the game thread for 60 ms — a twelfth of the reaction window,
inside the one scene where that matters. (`PlaySound` would be nicer but
`deploy.bat` does not link `winmm.lib`.)

### Still a guess, and said so

* **"Block" on 48/49** assumes those are the moments the player must react. The
  trace supports it — they precede `squ_timeover0` by ~700 ms — but it is not
  proven.
* **Label 47 as defeat**, once gated. Both BAT runs were losses, so **no winning
  trace exists yet**.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors** (probe extended with
  `CreateThread`/`CloseHandle`/`IsSpeaking` stubs).
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` is 81,404 bytes — 516 from the 81,920 hard fail**
(+19 for the `IsSpeaking` forward declaration). **SPLIT BEFORE THE NEXT EDIT
THAT NEEDS ROOM.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

Same scene, and the log still matters more than the experience.

1. On entry you should hear **"W punch. A block. X kick."** — complete, before
   anything else speaks.
2. **No "Defeated." at the start.**
3. **"Block"** when the soldier attacks; **"Foe hit."** / **"You hurt."** on
   damage.
4. **F9** mid-fight swaps to the tone. Volume should not change. Given the
   window is 700 ms, the real question is which of the two you can act on.
5. If you lose, **F9** on the Game Over screen skips to Galbadia Garden. (The
   game's own menu also offers *"Try again with HP+200"*, and its hint says the
   fighter with the most HP when time runs out wins — so the Skip is a last
   resort, not the only way through.)

Grep `[BGBTL]`, `[BGBTL-REQ]` and `[BGBTL-VARS]`. The line that unlocks health
announcements is the single `moved during fight:` summary at the end.
