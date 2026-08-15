## v0.20.118

#minigame-bgbtl: **the briefing was costing you health, and the block key does
reach the engine.**

### You found the worst defect in this arc

> *"If you sit on the Game Controls screen the enemy is accumulating hits on
> Squall — on one attempt I sat there a long while, then started the game, and
> the enemy did a flurry of hits that immediately resulted in a game over."*

**The freeze stops `field_main`, but it plainly does not stop whatever schedules
the soldier's attacks, and the backlog lands the instant the field resumes.**

A pause you *chose* to take was costing you the fight. That's the opposite of
what the briefing is for.

**Fixed by construction rather than by chasing the mechanism first:** the
briefing now snapshots both HP values when it opens and restores them every tick
while it's up. It also **logs every drift it corrects**, so the run that fixes it
also measures how leaky the pause actually is.

The briefing also now polls HP and the clock at all — `Update()` returned early
while briefing, which is exactly why no previous log could have shown this.

### The block key reaches the engine — the mapping was never the problem

`[BGBTL-BTN]` caught your presses:

```
[BGBTL-BTN] +38188ms  held=0x0040   held(punch16=0 guard64=1 kick32=0)
```

**Bit 64 — precisely the mask `bgbtl_1`'s fight loop tests to request
`squ_guarding0`.** A is bound correctly and the press arrives at the engine.

**And yet the guard flag never set, across 75 cued attacks and four attempts.**
So the failure is between the engine's button word and the script — not the
keyboard, not the binding, and not your timing.

**One more measurement, and it undercuts the whole tap-versus-hold framing:** the
bit is **never held longer than 156 ms**, even in the attempt you spent holding
the key down — 131 button transitions in that attempt alone. Either the engine
consumes that word on read, or something is clearing it. That's the next thread,
and it's far better defined than "tap or hold."

### F10: the HP hold now continues through the ending

v0.20.117's skip released at clock 601, and Squall was then punched from **383
down to 147** across the rescue scene — exactly what you heard.

The clock write still stops at the resolution (the ending runs on that same
counter, and holding it would stall the scene you want), **but the HP hold now
runs until the module disarms.** It costs nothing once the winner is decided, and
it's the difference between watching the ending and being beaten up during it.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now asserts both
  halves of that split — HP still pinned after the resolution, clock left
  untouched — alongside the host-phase FMV skip (exactly one request), the
  guard-flag detector, and every earlier check.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,602 bytes — 318 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **Sit on the Game Controls screen for a good while, then start.** Your health
   should be exactly where it was — no flurry, no free hits. Grep
   `briefing HP drifted` to see how much the pause was leaking.
2. **F10** — you should stop taking damage from the moment you press it, all the
   way through the rescue scene.
3. Blocking is still expected to fail. That one is now a well-defined hunt on my
   side rather than something for you to keep testing.
