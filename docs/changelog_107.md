## v0.20.107

#minigame-bgbtl: **the pause held; the cap let go.**

> *"The mini-game started on its own without my pressing the X key. It let the
> battle proceed for a few hits, then the instructions reappeared. I think we
> need to pause the fmv / background as well — my guess is it is triggering the
> battle to start when the scene transitions even if the player has not pressed
> X yet."* — Aaron

### The freeze worked perfectly, and the log proves it

Between `[BGBTL] field PAUSED` at 22:13:00 and `field RESUMED` at 22:13:30 there
are **zero REQ events and zero HP changes**. HP sat at Squall 440/600, Foe
600/600 for the entire thirty seconds:

```
22:13:00  [BGBTL] field PAUSED (field_main 0xA1 -> 0xC3)
22:13:00  [BGBTL-HP] armed Squall 440/600 (75%)  Foe 600/600 (100%)
          ... thirty seconds of nothing at all ...
22:13:30  [BGBTL] field RESUMED (the 30-second cap)
22:13:30  [BGBTL-REQ] +30016ms ent=13 label=92
22:13:31  [BGBTL-HP] Squall 382/600 (65%)
```

The first REQ and the first damage land **after** the resume. The FMV was not
driving the fight.

**What started it was our own 30-second cap**, which fired because no confirm
ever arrived. So the fix isn't to pause more — it's to stop giving up.

* **Cap 30 s → 180 s.** A safety net that fires in ordinary use is not a safety
  net, it's a timer.
* **"Press X to begin." every 12 seconds** while the freeze holds, so silence can
  never be mistaken for a hang. That is what actually rescues this case: the
  player wasn't told the game was waiting for them.

### The second defect was real, and Aaron saw it

The game re-shows its legend at **every phase change**, and v0.20.106 re-opened
the briefing on each one — twice in the middle of a live fight:

```
22:15:07  [BGBTL] legend again -- briefing re-opened
22:15:46  [BGBTL] legend again -- briefing re-opened
```

That's the "instructions reappeared". **Briefing is now once per attempt.** Only
a pass through the Game Over screen (field 95) earns another one.

### His instinct about timing was right, even though the mechanism wasn't

```
22:13:00  [BGBTL-HP] armed Squall 440/600 (75%)
```

**He had already lost a quarter of his health before the legend ever appeared** —
in the seven seconds between the movie starting and the UI showing.

So the module now **also arms on the FMV itself**: `disc01_32h.avi`, which plays
nowhere else. The check runs in `FreezeWatchdog`, above `Update()`'s early
returns, so the scene freezes **before the first punch lands**.

The AVI name outlives playback, so the arm is **latched** — without that the
module would re-freeze the game the instant the fight ended. That's a test, not
a hope.

### F10 skips the fight

> *"We should also map F10 to the skip function during the mini-game and inform
> the player in the instructions that F10 will skip the mini-game."*

**F10 now skips during the fight as well as on the Game Over screen**, and the
briefing says so. A player who can't make the 700 ms reaction window shouldn't
have to lose first to get past the scene.

`SkipToVictory` **thaws first** — the transition it writes is executed by the
field script, which is exactly what the freeze stops.

F9 keeps its jobs: repeat the briefing while it's up, toggle the Block cue
during the fight.

The briefing now reads:

> *"Garden battle. You fight the soldier hand to hand. W punches. A blocks.
> X kicks. I will say block when he attacks, and call out health as it changes.
> F10 skips the whole fight. F9 repeats this. Press X to begin."*

### Instrument kept

`[BGBTL-KEY]` logs the raw X / Enter / F10 state once a second for the whole
briefing. Whether a confirm was never pressed or never seen is not something to
guess at twice.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now also asserts that
  the FMV arms the scene, an **unrelated** movie does not, the legend does **not**
  re-brief within one attempt, a Game Over **does**, and the latched AVI cannot
  re-freeze after the fight — alongside the existing patch/restore, HP replay and
  legend-filter checks.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.

**Process note worth keeping:** a `g++ … ; ./probe` shell line ran a **stale
binary** when the compile failed, and reported OK. The probe is now built with
`if g++ …; then` so a failed build can never be read as a pass. That went
unnoticed for one edit cycle.

**⚠ `field_navigation.cpp` is 81,587 bytes — 333 from the 81,920 hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. The briefing should now open **as the movie starts**, before any damage —
   check `[BGBTL-HP] armed` reads **600/600** for both.
2. **The game must wait for you.** If you don't press anything you should hear
   *"Press X to begin."* every twelve seconds, indefinitely — not the fight
   starting.
3. **X** resumes with *"Game start."*
4. The instructions should **not** come back mid-fight. Only after a Game Over.
5. **F10** should skip straight to Galbadia Garden, from during the fight or from
   the Game Over screen.

If X still doesn't register, `[BGBTL-KEY]` will say so outright — it logs the raw
key state every second, so that question gets answered by this run either way.

**One caution:** F10 is the Windows menu key. In a full-screen game with no menu
bar it should be inert, but if it does anything odd, say so and it moves.
