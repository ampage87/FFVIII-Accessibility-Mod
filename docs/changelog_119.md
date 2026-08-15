## v0.20.119

#minigame-bgbtl: **Aaron has been blocking correctly for four BATs. The mod was
reading the soldier's guard flag and telling him he had blocked nothing.**

> *"Go back to the exe. Update your sim of this mini-game with everything you've
> learned. Continue analyzing the exe and updating your sim until you have all
> of this pinned down and working."* — Aaron

No BAT was spent on any of this. It came out of the exe, the two field scripts,
and the log from the 2026-08-15 run.

---

### The one that matters: every label number was one too low

A `.sym` group is `(count, start)` and it spans **count + 1** label slots,
because the slot at `start` holds the group header — the bare entity name that
precedes the `entity::method` list. bg2f_31 proves it in one line: squall is
`(18, 0)`, the next group starts at 19, so squall owns 0..18 — nineteen slots —
and the `.sym` lists nineteen strings for it.

Read off by one, `squ_punchkeyscan0` lands on label 11, whose body is a
positioning script. Read correctly it lands on 12, and **label 12 is the keyscan
loop** — four `BTNTEST`s and a `JMP -53`. Every name in both fields matches its
body under the corrected rule; several contradict it under the old one.

Everything downstream of that was wrong:

| what the mod believed | what it actually is |
|---|---|
| block = mask 64 | **block = mask 128** |
| Squall's guard flag = var 1031 | **var 1030 (field 144) / var 1028 (field 152)** — 1031 is the **soldier's** |
| soldier attacks = 54, 55, 56 | **55, 56** — 54 is `g0_fall0`, the driver, and 57 is the soldier *reacting* |
| "tap block, holding does not work" | **holding is the only thing that can work** |

### The proof that he was blocking

`squ_hpcalc0` rolls a byte and reads exactly one flag:

```
guarded    15 + rnd/8   ->  15..46
unguarded  40 + rnd/4   ->  40..103
```

**All 35 hits in his log fall inside one of those two bands.** The nine in the
guarded band are all from the attempt he spent holding the key — six of the nine
hits he took in that attempt were blocked. The three attempts where he held
nothing produced twenty-six hits and not one landed in the guarded band.

He was right. `BLOCK SUMMARY: 0 BLOCKED` was the instrument, not the player.

### And blocking alone still loses, which nothing had ever said

`director0::default` resolves the round at `var80 >= 580` on `foeHP < squallHP`.
Block perfectly and the soldier's health never falls, so you lose on the
comparison. `gal_hpcalc0` is where the win lives:

```
soldier guarding   10 + rnd/16
ordinary punch     30 + rnd/4
kick               15 + rnd/6
squ_punching1     300 + rnd        <- 300..555 of the soldier's 600
```

and `squ_punching1` is the keyscan's fourth `BTNTEST`, gated on `var340 >= 3`.
`squ_punched_up0` increments var 340 on a **blocked** hit and zeroes it on an
unblocked one — and at exactly 3 the game REQs `director5::sys_mes`, which is how
a sighted player is told.

**So the scene is: block three in a row, throw the heavy punch, twice.** That is
now what the briefing says, and the mod announces the streak reaching three.

### Why the block has to be held, and why holding was not enough

Opcode 109 (handler `0x0051DA50`) reads the **level** of `0x01CE48B0` — no edge,
no consumption. And the warning is `WAIT 10` / `WAIT 8` inside
`g0_punching0` / `g0_kicking0`: **167 ms and 133 ms**. His log agrees to within a
tick (attack REQ at +484 ms, hit at +625 ms). Nothing the mod can *say* fits in
133 ms, so the block must already be held when the cue arrives — the tone is a
confirmation, not a prompt.

But `squ_guarding0` raises the flag 4 frames in, holds it 20, then spends its
animation tail with the priority-5 slot still occupied, so holding the key gives
a **duty cycle**, not a guard. His measured 6-of-9 is exactly that.

**So while the block key is held, the mod pins the guard flag the script itself
sets.** It changes no damage number, no timer and no script. F9 turns it off.

Modelled offline against his measured 0.72 s attack interval:

```
                       blocked   mean dmg   knocked out   round won
nothing held              0.0%       71.7        100.0%        --
block held, game's own   46.3%       52.8         86.2%       7.0%
block held + assist     100.0%       30.6          0.0%      91.8%
```

7% is what he has been playing against. The 8% of losses that remain with the
assist are rounds where two heavy punches did not quite close the gap before the
clock — it is still a fight.

### The mod no longer guesses which key is which, because it cannot

The four masks are **pad bits**. `0x004A2D60` only consults FF8's remap table
when `[0x01CFE73C] & 0x20`, which is clear on his machine — so the word is the
raw pad word and the keyboard mapping lives in the 2013 wrapper, not in
`FF8_EN.exe`. Guessing it is how four builds told him to press the wrong thing.

**The briefing now names his keys instead.** The field is frozen there, so every
one of them is inert: he taps them, the mod watches `0x01CE48B0`, and says
*"Block, A."* / *"Heavy punch, N."* Two clean presses lock a binding. It works
for a remapped keyboard or a gamepad, and the disarm line records what it
learned.

### F10 was one short. Literally one.

```
01:14:48  clock 326 -> 579        the skip writes it
01:14:53  clock=579
   ...    clock=579 for FIFTY-THREE SECONDS ...
01:15:42  clock=585               the rescue movie finally takes over
```

Squall was punched from 600 down to 147 inside that stall. Aaron: *"you press
F10 and sit there hearing the sound of Squall being punched for quite a while
and it is very odd."*

**Var 80 is the movie's frame number.** `0x0052A016`: if a movie is running, call
`0x005305A0` and store it at `[0x00B8EE90]+0x50`. F10 had just ended the movie,
so nothing was left to carry 579 across the `wait var80 >= 580` that resolves the
fight and REQs `gal0::gal0_timeover0` — the script that stops the soldier.

Two changes: **write 580, not 579**, and when the clock stalls with no movie
behind it, carry it on at 15/s — the rate measured off the rescue movie in that
same log — backing off the instant the engine starts writing it again. Forward
only, never past 1057, so the transition to Galbadia Garden is still the game's
own `MAPJUMP3`.

The rescue FMV is `disc01_33h.avi`, confirmed from his log.

### The briefing was arriving 43 seconds late on a retry

> *"if you sit on the Game Controls screen the enemy is accumulating hits on
> Squall"*

The first attempt arms on the battle FMV and briefs at full health. A retry
cannot — the AVI name is latched — so the only trigger left was the on-screen
legend, and in his log field 144 reloaded at 01:12:16 while the legend did not
appear until **01:12:59**, by which point Squall was down to 498 of 600.

That is the whole of it. The pause itself is airtight: across the 47-second
briefing the fight clock moved from 100 to 106. **A retry now briefs the moment
the host field comes back.** The HP hold from .118 stays as a backstop.

### Offline tooling that now exists

* `minigame/jsm2.py` — a field-script disassembler whose decoding is verified
  against the engine's own decoder at `0x00530760`. The old reading treated a
  zero-high-byte dword as "push literal N"; the decoder proves the whole 24-bit
  value is the **opcode**. Every zero-high dword in both scripts is ≤ 323, inside
  the 400-entry handler table — which is what makes that provable rather than
  plausible.
* `minigame/sim.py` — the fight model above. Damage, guard window, wind-up,
  cadence and the streak rule are all read out of the scripts; the one modelled
  quantity is the animation tail, calibrated against his measured interval and
  labelled ASSUMED in the output.
* `minigame/handlers.txt` — the disassembled handler for every opcode either
  script uses, plus the 16-entry expression table (`ADD SUB MUL DIV MOD NEG EQ
  GT GE LT LE NE AND OR XOR NOT`).

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now maps a real page
  at `0x01CE4000` so the assist and the key learner run against the same address
  the script reads. New assertions: the guard flag is per-field and the
  **soldier's flag does not count**; the heavy-punch streak announces once at 3
  and re-arms at 0; the assist pins only while held and only when enabled; the
  key learner refuses to name a bit when two keys are down, and four names in one
  log call do not alias; the skip writes **580**; the stall governor nudges a
  stalled clock, caps at 1057, and leaves an engine-driven clock alone.
* `lint_seh` OK (86 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* **`field_navigation.cpp` UNCHANGED at 81,602 bytes** — 318 from the hard fail.
  Everything landed in the two `.inl`s; `field_minigame_bgbtl.inl` is 76,744
  after pruning three superseded comment blocks that this session proved wrong.

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **The briefing.** It should open at full health on every attempt, including
   retries. Tap your four action keys while it is up — the mod should name each
   one. Then X.
2. **Hold the block key down** and leave it down. You should hear *"Blocked."*
   on most hits, and damage should be small.
3. **Listen for *"Heavy punch ready"*** — it names the key. Let go of block,
   press it, and the soldier should lose most of his health in one hit. Twice
   wins the round.
4. **F9** toggles the block hold, if you want to feel the difference.
5. **F10** should reach the Rinoa rescue with no long stretch of punching.

Grep `[BGBTL] KEYS LEARNED` and `BLOCK SUMMARY` at the end — between them they
say which keys the mod worked out, how many blocks it saw, and the best streak.
