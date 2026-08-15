## v0.20.120

#minigame-bgbtl: **the clock write was mine, and it was the worst thing in the
scene. It is gone.**

> *"After pressing F10 I didn't hear any punches or anything, but then after
> pressing X it suddenly sounded like Squall was being punched repeatedly. I
> also could no longer hear any of the background sounds of the Garden battle,
> it was like the background animation was completely gone."* — Aaron

### The log says it in one column

```
13:21:16   F10 -- SKIP: fight clock 115 -> 580 written
13:21:17   ...
           ...  EIGHTY-TWO SECONDS WITH NOT ONE REQ EVENT  ...
13:22:38   the script wakes up mid-fight; the punches restart
13:22:53   disc01_33h.avi ends naturally
```

Zero script activity for 82 seconds. That is the "background animation was
completely gone" — exactly, and not a figure of speech.

### What v0.20.119 got wrong about var 80

.119 established that var 80 is the current movie's frame number. It did not
follow that through. **Field 152's entire fight plays over `disc01_33h.avi` —
one movie, about 105 seconds, whose last stretch *is* the Rinoa rescue.** There
is no separate rescue FMV to jump to, and there is no seek.

So writing 580 into that counter teleported `director0::default` to its own
resolution while the movie was still at frame 117. It did what it does at the
end of a real fight: released the fight entities (`op76` × 4 — that is the
silence), set the rescue choreography sixty seconds early, and then the movie's
own counter reasserted itself and dragged the ladder back into the fight. The
X press at 13:22:38 was a coincidence of timing; the clock reverting from my
fake 1057 to the movie's real 296 is what restarted the punching.

**The mod now never writes var 80 during the fight.** The stall governor is
deleted.

### So what F10 does instead

The soldier's driver gates every single attack on Squall's health:

```
gal0::g0_fall0   ...  RDVARSW 354 ; PUSHI 0 ; EXPR 7 (GT) ; JPF 13
```

So the skip **parks Squall's health at zero**, which silences the driver without
touching a timer, and restores it to full at clock 560 — before the resolution
reads it at 580, so `foeHP < squallHP` still picks him. Every value written is
one the game writes itself, and nothing is desynced from the movie.

It cannot make the scene shorter. The fight and the rescue are the same movie.
So the announcement now says so:

> *"Skipping. You cannot lose now and the soldier stops. The rescue scene still
> plays. Press F10 again to leave straight away."*

**F10 a second time leaves entirely.** That one ends the movie *first* — which is
the only state in which nothing else writes var 80 — and then walks the counter
through the ladder so `director0` fires its **own** `MAPJUMP3` to Galbadia
Garden. Nothing forged; v0.20.110 forged that jump and it crashed.

### The block count was right and the report was wrong

```
BLOCK SUMMARY: 35 attacks cued, 2 BLOCKED
[BGBTL-STREAK] var340 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6
```

Both numbers were honest. With the assist pinning the guard flag, the flag has
one rising edge **per hold**, not per hit. **The count and the "Blocked."
announcement now come from var 340**, which `squ_punched_up0` increments exactly
once per blocked hit. The flag edge is kept in the trace as proof the guard is
being set at all. The summary also reports the **best** streak instead of
whatever the counter happened to hold at disarm.

### The key mapping, settled on the right word

`punch=W kick=X block=W heavy=X` was an aliasing bug in my own log line — four
`NameForMask` labels sharing two rotating buffers. Fixed with one buffer per
name.

The real mapping, measured on `0x01CE48B0` across the run:

```
W -> 0x10 (16)   punch
X -> 0x40 (64)   kick
A -> 0x80 (128)  BLOCK
     0x20 (32)   heavy punch -- key still unseen
```

The original briefing's key names were right all along; the guard variable and
the label numbers were what was wrong.

**The briefing's calibration step learned nothing because the freeze freezes the
button word too** — `0x01CE48B0` is written inside the routine `field_main`
calls, and the briefing is a `RET` at `field_main`'s entry. It now reads the
engine's own held-buttons word while frozen, which is one layer up, keeps
updating, and carries the same bit layout (A → 0x0080 and X → 0x0040 on both,
across two runs).

### Two smaller things from the same log

* **`briefing HP frozen at Squall 440, Foe 0`** — the module arms on the battle
  FMV, and `squ_out0` does not write 600/600 until after it, so the briefing was
  pinning the *previous scene's* leftovers and holding the fight's own
  initialisation down. It now refuses to pin until both fighters read as
  initialised.
* **Health reports go quiet while the skip holds the numbers.** Otherwise
  parking Squall at zero would be announced as *"You 0"* — a lie in the player's
  own voice.
* The host-phase FMV skip no longer fires again once the fight has reached
  field 152.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors.** New assertions, all of them
  written from this log: the first F10 press **must not write the clock** and
  must park Squall at 0 then restore him before 580; the second press must wait
  for the movie to end before writing anything and must stop at 1057; the block
  count comes from var 340 and survives a streak reset; the guard-flag edge
  still honours the per-field byte and still ignores the soldier's; four key
  names in one log call do not alias.
* `lint_seh` OK (86 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` **81,645 bytes — 275 from the hard fail** (+43 for one
  forward declaration). **SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **Play it straight first.** Hold the block key; expect *"Blocked."* on most
   hits now, not twice a fight. When you hear *"Heavy punch ready"*, let go and
   try **S** — the fourth action key is the one thing the mod still has not seen,
   and the moment you press it once it will name it from then on.
2. **F10 once.** The punching should stop immediately and stay stopped, the
   background should keep running, and the scene should play through to the
   rescue on its own time.
3. **F10 twice** if you would rather not wait — that should take you to Galbadia
   Garden.

Grep `KEYS LEARNED`, `BLOCK SUMMARY` and `[BGBTL-STREAK]`.
