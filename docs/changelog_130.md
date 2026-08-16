## v0.20.130

#minigame-bgbtl: **the briefing vetoed the soldier and left Squall swinging.**

> *"While the controls screen is visible, you can hear the scene playing in the
> background, and as I press punch or kick on the controls screen I can hear
> Squall striking the enemy. When I press enter to actually start, the enemy's
> HP is not full."* — Aaron

He is right, and **the log from 2026-08-15 had already measured it — nobody
looked at the number.**

```
16:12:06  briefing 1 opened          armed  Squall 440/600   Foe 0/0
   ...34.5 seconds of tapping W, D, A, X to have the keys named...
16:12:41  briefing ended
16:12:41  Squall 440/600 (75%)   Foe 431/600 (70%)      <-- NOT 600
```

**169 damage, dealt before "Game start."** The control run beside it is the same
log's second attempt: a 5.4-second briefing in which he pressed nothing but
Enter, and the first health line reads `Foe 600/600`.

### Why

It is the cost of the thing that makes the briefing work at all. Since
v0.20.123 the field is **not** frozen — freezing `field_main` would stop the
dialog box from ever being drawn — so `squall::squ_punchkeyscan0` is running
behind the Game Controls the whole time. Its four `BTNTEST`s read the same
button word the key learner reads, so every tap of the punch or kick key REQ'd
`squ_punching0` / `squ_kicking0`, which reach `gal_hpcalc0` and take `30 + rnd/4`
off the soldier.

**The calibration step was a free hit on the enemy** — and the player was being
charged for learning their own controls, in the only currency this fight has.
Two or three taps is most of an ordinary punch; a careful player who tapped each
key twice to lock a binding started the round against a soldier already down a
quarter of his health, which quietly makes the fight *easier* and makes every
"how many heavy punches does this take" answer wrong.

### The fix

The briefing now vetoes **both** fighters. Squall's three attack scripts are
redirected to `squall::push` — his own entity's `PUSH8 n ; RET 8`, the same
no-op trick the soldier's veto has used since .123 — so the tap is named by the
mod, and nothing else happens: no animation, no strike, no damage.

`squ_guarding0` is deliberately **not** vetoed. It only raises the guard flag,
costs nobody anything, and the block key is the one control worth letting the
player feel while they are being told about it.

**The labels were derived, then verified in the scripts themselves.** A `.sym`
group `(count, start)` spans `count+1` slots and its names run
`header, default, talk, push, …`, so `push` is always `start+3`:

| field | squall group | ⇒ `squall::push` | ⇒ keyscan | disassembly at `push` |
|---|---|---|---|---|
| 144 `bg2f_31` | `(18, 0)` | **3** | 12 | `PUSH8 3 ; RET 8` |
| 152 `bgbtl_1` | `(14, 22)` | **25** | 28 | `PUSH8 25 ; RET 8` |

Both keyscan numbers are the ones the log prints, and the same arithmetic
reproduces `gal0::push = 45` and `g_hei0::push = 48`, which this module already
used and the fight already proved. The three attack labels were disassembled
too: 13/14/15 and 29/30/31 are the animation-and-SFX scripts, and 16/32 are the
guards that write var 1030 / 1028 — Squall's own flag in each field, exactly as
`GuardVarFor` has it.

### Verification

* `minigame_bgbtl_compile`: **0 errors, 0 bad**, with eleven new cases — all six
  of Squall's attack labels redirect to his `push` in the right field, the
  soldier is still vetoed alongside him, and **the guard and the keyscan itself
  come through untouched**. One more asserts the *skip* veto still leaves
  Squall's punch alone, since after F9 the foe is pinned at zero anyway and
  swinging costs nothing.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` **untouched at 81,645** — 275 from the hard fail.

**NOT MSVC-built.**

### BAT

On the Game Controls screen, tap every key several times — punch, kick, heavy,
block. The mod should name each one and **nothing else should happen**: no
strike, no impact sound, no HP movement.

Then press Enter and listen to the first health report. **The soldier must be at
100.** The quickest way to be sure is to do what caught it: sit on the briefing
for half a minute, tapping, before you start.

---

## Also in v0.20.130: "Punch, W." fifty-three times

The same briefing that proved the bug above proved a second one. From
`ff8_field.log`, **53 `[BGBTL-LEARN]` lines, every one of them `Punch`** — and
in `ff8_mod.log`:

```
17:57:47  "Punch, W." (interrupt)      x6 inside one second
17:57:48  "Punch, W." (interrupt)      x5 more
```

A held key **auto-repeats**, and every repeat is a fresh rising edge on the
button word, so the learner announced it again — each announcement interrupting
the one before it, so the sentence never finished saying itself.

The learner now debounces **per mask**, 1200 ms. A deliberate re-tap still
confirms the binding, which is worth keeping: pressing a key and hearing nothing
would read as "the key is not registering." What goes away is the stutter.

The probe drives six auto-repeats 100 ms apart and requires **exactly one**
announcement, then a re-tap after the window and requires that it speaks. It
reports `a held key announced 6 times in 600 ms` against the old code.
