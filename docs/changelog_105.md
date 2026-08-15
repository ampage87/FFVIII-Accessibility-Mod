## v0.20.105

#minigame-bgbtl: **health is found — entirely offline, no BAT spent.**

> *"Go back to the exe and game files with what you've learned and see if you can
> find the health values in the mini-game so we don't have to keep doing BAT
> cycles to pin it down."* — Aaron

Four `int16`s in the field variable block:

```
0x01CFE9B8 + 350   Squall  max HP
0x01CFE9B8 + 354   Squall  current HP
0x01CFE9B8 + 352   Foe     max HP
0x01CFE9B8 + 356   Foe     current HP
```

### Three independent steps, and they agree

**1. The script names the pair.** In `bgbtl_1`, `rinoa::gal_hpcalc0` reads damage
byte **1028** — which `squall::squ_punched_up0` writes — and does `WRVARW 354`.
Its structural twin reads byte **1029** — which `gal0::g0_punched_up0` writes —
and does `WRVARW 356`.

So 354 is the value that falls when **Squall** is hit and 356 the one that falls
when the **soldier** is hit. Which is which is not a guess.

**2. The host field initialises all four.** `bg2f_31` — pulled out of `field.fs`
by byte range and decompressed here, the same way `bgbtl_1` was:

```
rinoa::m6    PSHM 600 ; RDVARW 380 -> WRVARW 350      Squall max
             PSHM 600 ; RDVARW 380 -> WRVARW 354      Squall current
rinoa::m11   PSHM 600            -> WRVARW 352        Foe max
             PSHM 600            -> WRVARW 356        Foe current
```

Both fighters start at **600**. Squall's is 600 **adjusted by var 380** — that is
the Game Over menu's *"Try again with HP+200"*. **So max is read, never
assumed.**

`bg2f_31` also carries `stc0::squ_hpcalc0`, `stc0::gal_hpcalc0` and
`g_hei0::g0_punching0 / kicking0 / guarding0 / punched_up0` — which
**independently confirms v0.20.104's finding** that the fight runs in the host
field before `bgbtl_1` ever loads. That was inferred from two screenshots; now
the host field's own script says it.

**3. The exe pins the addressing.** The `RDVARW` handler:

```
0x0051CBF0   movsx ecx, word ptr [eax + 0x1CFE9B8]
```

The operand is a **raw byte offset**, unscaled, and the read is
**sign-extended** (the `RDVARB` twin sits at `0x0051CBB0`). Signed matters:
current HP goes **negative** rather than clamping — the v0.20.103 trace caught
the soldier at **−18** — so percentages clamp.

### It agrees with the trace we already had

Across all 4,096 watched bytes, **354 and 356 were the only non-counter bytes
that moved on damage**, every one of the twelve landed hits moved one of them,
and they stopped dead when the hits stopped.

At arm time the soldier read **400** and Squall **251**, both of 600 — exactly
right for the seventeen seconds of unwatched fighting before v0.20.104 learned
to arm early.

### So health ships

In Aaron's own short form, and on **every change** rather than at thresholds —
because the measured rate makes that affordable. Twelve damage events in 92
seconds is one every two to seven seconds.

```
"You 75"            one fighter changed
"You 60, Foe 45"    both changed in one poll
"Foe down."         foe HP reached zero
```

Percentages round to the nearest 5. **Replaying the real recorded sequences
through the policy** gives:

```
foe   : 65 60 50 45 35 30 25 10 5 0
squall: 40 35 20 5
```

Strictly falling, all distinct — **no recorded change is lost to rounding and
none repeats.** That replay is now a test, not an argument.

### Removed

* **"Foe hit." and "You hurt."** — the number says who *and* how much in the same
  breath.
* **The 4,096-byte change detector.** It did its job and its job is over.
* `g0_fall0` as a defeat signal was already gone in v0.20.104; **"Foe down." now
  fires on foe HP reaching zero**, which is a real measurement.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now **replays the
  recorded HP sequences and asserts the policy** (0 bad), alongside the legend
  filter check. Also asserts −18 clamps to 0 and an unreadable max announces
  nothing.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.
* **`field_navigation.cpp` unchanged at 81,404 bytes** — 516 from the hard fail.
  Everything went into the `.inl`. The split is still owed.

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. Health should now be spoken on every change — **"You 60"**, **"Foe 45"** — in
   percentages of 600.
2. **"Foe down."** when the soldier reaches zero.
3. The controls should still arrive while the FMV is playing (v0.20.104).
4. **F9** toggles the Block cue between speech and tone; volume must not change.

Grep `[BGBTL-HP]`. The `armed` line gives both fighters' raw HP and max at the
moment the module wakes up — if Squall's max reads 800 after a *"Try again with
HP+200"*, that confirms var 380 is the retry bonus and closes the last open
question in this arc.
