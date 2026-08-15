## v0.20.108

#minigame-bgbtl: **the cap is gone; liveness replaces it.**

> *"Why have the cap at all? Also, let's adjust that announcement every 12
> seconds to say Press X to confirm, press F9 to hear the controls again."*
> — Aaron

### The cap was guarding the wrong thing

The danger a timeout protected against was never the player taking their time.
It was **the mod itself dying** while the game's main loop is patched out with a
`RET`.

A timeout cannot tell those two apart, so it punished the first in order to
protect against the second — and in the v0.20.107 BAT it did exactly that: the
fight started while Aaron was still deciding.

### The right test is liveness, not elapsed time

`FreezeWatchdog` — which runs above `Update()`'s early returns — now stamps a
heartbeat every tick while the field is frozen. A **dedicated guard thread**
restores the byte if that stamp ever goes stale for 3 seconds.

```
FreezeField()      -> patch 0xC3, stamp heartbeat, start guard thread
FreezeWatchdog()   -> stamp heartbeat, every tick, forever
FreezeGuardThread  -> heartbeat stale > 3 s ? restore the byte and get out
```

That condition is true **only** when something has actually gone wrong. It is
never true because a player thought about it for a while.

The guard writes one byte and clears two flags. It never speaks and never
touches the event ring — by the time it runs, the thread that owns those is
gone.

**So the briefing now has no time limit at all.** It holds until X, and the
reminder simply repeats.

### Wording

The 12-second reminder, and the closing line of the briefing itself, are now the
same sentence:

> *"Press X to confirm, press F9 to hear the controls again."*

### Also fixed, quietly

**The fight clock restarts when the briefing ends.** `[BGBTL-REQ]` offsets now
read from the start of the fight rather than from the arm — and the 5-minute arm
cap no longer inherits however long the player spent listening, which would have
been a second timeout wearing a different hat.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now asserts that **ten
  minutes of waiting** leaves the briefing up and the field still frozen (byte
  still `0xC3`), and that X still ends it cleanly afterwards.
* Still checked: the FMV arm, the unrelated-movie negative, the once-per-attempt
  rule, the Game Over re-arm, the AVI latch, patch/restore on every exit path,
  the HP replay, and the legend filter.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
All of this went into the `.inl`. SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. The briefing should now wait **indefinitely**. Leave it a minute or two and
   confirm the fight never starts on its own — you should just hear the reminder
   every twelve seconds.
2. **X** resumes with *"Game start."*
3. Everything else as in v0.20.107: armed at the movie with 600/600, no
   mid-fight re-briefing, F10 skips.

Grep `[BGBTL] GUARD:` — that line should **never** appear. If it does, the mod's
own loop stopped and the guard thread caught it, which is exactly the case the
cap was there for and the only case it should ever fire in.
