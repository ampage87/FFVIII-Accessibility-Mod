## v0.20.122

#minigame-bgbtl: **the retry armed in a hallway, the heavy punch was taken away
before it could be thrown, and the skip cued the attacks it had just cancelled.**

---

### 1. "Try again" puts you back in the hallway, and the mod woke up there

> *"Remember that after a game over and selecting try again you are put back in
> the B-Garden hallway before the game starts. All of the mini-game
> accessibility machinery needs to stop and wait to turn on again when the
> mini-game begins."* — Aaron

```
14:10:35  still armed across field 144
14:10:35  briefing 2 opened by the host field reloading
14:10:42  briefing ended (player confirmed)
14:11:28  ...the battle FMV finally starts
```

**Forty-six seconds of hallway** with the REQ hook live, the assist pinning a
guard flag and the block cue armed. v0.20.121 briefed on the host field's
*reload*, which is not the fight — it is the walk back to it.

**The module now lets go completely** when the host field comes back after a
Game Over, and waits for the same signal that armed it the first time:
`disc01_32h.avi`. The AVI latch clears itself once that name stops being
reported, so the movie starting again re-arms and re-briefs exactly like attempt
one — which is also what makes the briefing land at full health again.

### 2. The heavy punch was armed and then taken away

> *"I held down block and heard it announce the power punch was ready, but when
> I pressed D I never heard a punch land or the foe's HP go down."*

**In field 152 it worked perfectly** — and the log proves the whole mechanic:

```
14:10:21  squall::squ_punching1
14:10:22  gal0::g0_punched_up0
14:10:22  Squall 64/600 (10%)  Foe 66/600 (10%)     <- 600 -> 66, one hit
```

**In field 144 the same REQ fired and nothing happened.** Two things conspire,
and both punish the player for the one move the scene requires:

* **The streak is lost by letting go.** var 340 is zeroed by any unguarded hit,
  and you must release block to punch. At 14:10:06 Squall took 332 → 285 on
  exactly that gap and `var340 3 -> 0` with it, which shuts the keyscan's
  `var340 >= 3` gate — so six seconds of *held* D produced one attempt and then
  nothing.
* **That one attempt collided.** `squ_punching1` is REQ'd into the same priority
  slot `squ_guarding0` uses, and the guard script was still running out its
  animation tail, so the engine dropped it.

Two fixes, both small:

* **The gate is held open until the punch actually fires.** Once the streak
  reaches 3 the mod keeps var 340 at **4** — above the gate, and deliberately
  not 3, so the game's own hint (`director5::sys_mes`, REQ'd on `var340 == 3`)
  does not re-fire every tick. It releases the moment `squ_punching1` runs. A
  held key now keeps retrying until the priority slot frees.
* **The block assist covers a 1.5-second grace window after the key comes up**,
  so releasing block to throw the punch is no longer answered with an unguarded
  hit.

### 3. The skip cued the attacks it had just cancelled

> *"I tried F9 to skip and it caused the beep/block tone to spam repeatedly. I
> didn't hear any actual punches from the enemy just the beep as if they were
> about to."*

Both halves of that are exactly right, and they are the same bug. The veto
worked — no attack script ran, so no punches — but the ring still carried the
**original** label, so every cancelled attack rang the block tone. And it rang
faster than a real fight, because `push` returns instantly and the driver loops
straight back round. **171 cues** in that run.

**The ring now records the label the engine really ran.** The tone stops, and
the trace stops lying about what happened, in the same line of code.

### Also

* `KEYS LEARNED: punch=W kick=X block=A heavy=D` — the learner and its log line
  are both correct now, and that is the mapping the briefing reads out.
* The skip's own log line still said *"Squall parked at 0"*, left over from
  v0.20.120's approach. It says what it does now.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors.** New assertions: the hallway
  must disarm and the movie must re-arm; the heavy gate must survive a streak
  reset, must not sit at 3, and must release on `squ_punching1` (per-field
  label); a vetoed attack must not read as an attack; the grace window must hold
  the guard after the key comes up and must expire.
* `lint_seh` OK (87 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` unchanged at **81,645 — 275 from the hard fail**;
  `field_minigame_bgbtl.inl` 78,887.

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **After a Game Over, the hallway should be silent** — no cues, no briefing —
   and the Game Controls should arrive when the fight movie starts, at full
   health, as on the first attempt.
2. **Hold A, wait for *"Heavy punch ready"*, then let go and hold D.** It should
   land this time even if it does not take on the first try, and the gap should
   not cost you the streak. The soldier should drop by most of his health.
3. **F9** should be quiet — no tone spam, no punches.
