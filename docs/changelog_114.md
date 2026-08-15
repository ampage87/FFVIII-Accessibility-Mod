## v0.20.114

#minigame-bgbtl: **the fight clock — and F10 now lands where you asked.**

> *"It should essentially bypass the mini-game and go to the FMV where Squall
> rescues Rinoa. It should not jump all the way to the G-Garden entrance."*

Found offline, no BAT spent.

### The fight is a fixed-length round

`bgbtl_1`'s driver, `director0::talk`, is a ladder of waits on **one** value:

```
dw  970  OP_0x0E 80   then 1
dw  976  OP_0x0E 80   then 20
dw  990  OP_0x0E 80   then 580     <- THE FIGHT ENDS HERE
dw  995  read var 356 (foe HP), read var 354 (Squall HP), branch
dw 1004  REQ gal0 fall    /    dw 1012  REQ squall fall
dw 1026  OP_0x0E 80   then 750     <- the ending plays out
dw 1043  OP_0x0E 80   then 840
dw 1060  OP_0x0E 80   then 1057
dw 1078  MAPJUMP3 675 (ggback1)    <- the LAST instruction
```

**HP is not checked until the clock reaches 580.** That is the entire explanation
for *"the enemy kept punching me after I pressed F10"* — zeroing the foe changes
nothing until the script looks.

And **everything between 580 and 1057 is the rescue scene** — which is exactly
what v0.20.110's forged jump to 675 threw away. You were right that it shouldn't
land at the Garden entrance; the Garden entrance is the *last instruction*.

### The clock is pinned by the exe, not inferred

Opcode-table entry `0x0E` (table `0x00B8DE94`) is handler **`0x0051CB70`**:

```
mov ecx, dword ptr [eax + 0x1CFE9B8]
```

A **dword** read at an unscaled byte offset — the 32-bit sibling of the `RDVARB`
(`0x0051CBB0`) and `RDVARW` (`0x0051CBF0`) handlers that pinned HP.

`OP_0x0E` appears **six times in `bgbtl_1` and twice in `bg2f_31`, and its
operand is 80 in all eight.** It reads one thing. And bytes **80 and 81** were
both in the v0.20.103 whole-block trace's "moved during fight" list, counting
upward — the low half of this counter.

**The fight clock is a `uint32` at `0x01CFE9B8 + 80`.**

### So F10 now

1. **Pins the HP** — foe at zero, you at full — so the script picks you at 580.
2. **Pushes the clock to 579** once `bgbtl_1` is loaded, so that moment is *now*.
3. **Lets go.** Past clock 600 it stops writing anything, because the ending from
   580 to 1057 runs on the same counter and holding it would stall the very scene
   you want to see.

Pressing F10 in the host field — which is what you did last time — holds your HP
and waits for `bgbtl_1`, then ends the round there.

**No forged transition.** The game plays its own ending, rescue FMV included, and
reaches Galbadia Garden by its own `MAPJUMP3`.

### New `[BGBTL-CLOCK]` line

Once a second. The clock is the spine of the whole scene — fight ends at 580,
rescue runs to 1057 — so having it in the log makes future timing questions
answerable without another BAT.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now asserts the clock
  is advanced to 579 under the 40-hit skip simulation **and** that the skip
  *releases* once the clock passes the resolution — no more HP writes — because
  holding it would stall the ending.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**The v0.20.113 block instrument is untouched** — the same run still answers the
hold-A question.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

Both questions on one run:

1. **Hold A down** through the fight, then grep `BLOCK SUMMARY`.
2. On another attempt, press **F10**. Expect *"Skipping. Ending the round now,
   then the rescue scene plays."*, the round to finish within a second or two,
   and then **the rescue FMV** — not a jump to the Garden entrance.

Grep `[BGBTL-CLOCK]` to watch the counter, and `SKIP: fight clock` for the bump.
