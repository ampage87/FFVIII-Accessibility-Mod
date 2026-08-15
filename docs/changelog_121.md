## v0.20.121

#minigame-bgbtl: **three regressions, three separate causes, and the log names
each one.** Plus the key layout Aaron asked for.

> *"I only heard Game Controls once — on the first attempt... The F10 key didn't
> seem to do anything, I tried it twice and still ended up in a Game over... I
> tried blocking attack after attack in the first run and never heard the power
> punch announce."* — Aaron

---

### 1. The retry briefing was dismissed by the keypress that caused it

```
13:45:47  briefing 2 opened by the host field reloading
13:45:47  confirm pressed 500ms into the briefing
13:46:58  briefing 3 opened
13:46:58  confirm pressed 93ms into the briefing
```

The briefing **did** open on every attempt — .120 fixed that. It then died in
half a second, because Aaron was still holding **X** from choosing *"Try again"*
on the Game Over menu. The host field reloads, the briefing opens into a key
that is already down, and `s_awaitRelease` fires on the release.

**A press now only counts once the key has been seen UP since the briefing
opened.** The first briefing is unaffected — nothing is held when a movie starts.

### 2. F10 lost the fight because zero HP is a lethal value

```
13:46:37  SKIP engaged -- foe at 0, Squall parked at 0
13:46:43  still armed across field 152
13:46:47  still armed across field 95        <- Game Over, four seconds later
```

.120 parked Squall's health at zero to silence the soldier, on the strength of
`gal0::g0_fall0` gating every attack on `RDVARSW 354 > 0`. It does silence him.
It also loses, because the resolution is not the only thing that reads that
value, and zero is lethal on whichever tick it lands.

**Squall is pinned at FULL again, and the soldier is silenced at the REQ
instead.** The hook already sits on opcode `0x014` and already reads the label
off the VM stack; while the skip is active it overwrites that slot with the
soldier's own `push` script before chaining. Both fields' push scripts are, in
full:

```
PUSH8 n
RET 8
```

So the engine does a completely ordinary REQ — same two pops, same priority
slot, same bookkeeping — of a script that does nothing. No variable written, no
timer moved, nothing forged. The attack never starts, so there is no wind-up, no
punch sound and no call into `squ_hpcalc0`.

### 3. The heavy-punch call was spoken and then talked over

```
13:45:10  [BGBTL-STREAK] var340 2 -> 3
13:45:10  [BGBTL-REQ] fld=144 label=87 director5::sys_mes     <- the game's own hint
13:45:10  [BGBTL-HP] Squall 332/600 (55%)                     <- and this cut it off
```

It fired correctly and on cue. Then the health report a fraction of a second
later called `Speak(..., interrupt)` and took the floor.

**It now goes out on its own path**: it never yields to a block cue, it holds the
floor against health reports for two seconds afterwards, and while the streak
stays armed and unused it **repeats every five seconds**. A call to action the
player missed once is worth saying twice.

### The key layout, as requested

> *"Pressing X confirms it is the kick but also closes the Game Controls and
> starts the game."*

He is right, and it was always a conflict — **X is mask 64, the kick**. The key
that dismissed the Game Controls screen was one of the four keys the screen
exists to teach.

| | was | now |
|---|---|---|
| start the fight | X | **Enter** |
| repeat the controls | F9 | **Space** |
| block hold on/off (mid-fight) | F9 | **Space** |
| skip | F10 | **F9** |

F10 is the Windows system menu key, so the skip moves off it entirely.

### Also in this build

* **The key learner works, and it found the last unknown.** From the briefing's
  calibration step: `Heavy punch = mask 32, vk 0x44` — **D**, locked after two
  presses. The full set on Aaron's machine: `W` punch, `X` kick, `A` block,
  `D` heavy punch. The briefing names them as he taps them.
* `field_minigame_bgbtl.inl` reached **85,190 bytes**, over the 81,920 hard fail.
  The skip machinery moved to **`src/field_minigame_bgbtl_skip.inl`**; the parent
  is back to 75,391.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and every new assertion is
  written from this log — a confirm already down when the briefing opens must be
  ignored until released; the skip must pin Squall at **full** and must never
  write a lethal value; the attack REQ must be rewritten to `push` (45 in
  bgbtl_1, 48 in bg2f_31) while everything else passes through untouched; the
  clock must stay untouched; the heavy-punch call must fire at 3 and re-arm at 0.
  Every previously silent `bad++` in the briefing block now prints what failed.
* `lint_seh` OK (87 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` unchanged at **81,645 — 275 from the hard fail.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. **Every attempt should open with the Game Controls now**, including retries —
   and it should wait for you, because **Enter** starts it and Enter is not the
   key you used on the Game Over menu. **Space** repeats it.
2. Tap your four keys during the briefing; it should say *"Punch, W"*,
   *"Block, A"*, *"Kick, X"*, *"Heavy punch, D"*.
3. **Hold A.** After three blocks you should hear *"Heavy punch ready. Let go of
   block and press D."* — and it should keep saying so until you use it. Let go,
   press D, and the soldier should lose most of his health. Twice wins.
4. **F9** should stop the punching dead and leave you unable to lose. **F9
   twice** leaves for Galbadia Garden.
