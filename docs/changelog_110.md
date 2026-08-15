## v0.20.110

#minigame-bgbtl: **the tone, and two things the mod got wrong about its own
numbers.**

### 1. The tone is now the block cue

Aaron heard both and chose the tone — which is exactly what the F9 toggle
existed to settle. 1000 Hz, 70 ms, still on a worker thread so it never stalls
the game.

The briefing says so, and **pressing A on the briefing screen plays the tone**,
so the player learns the sound before it matters. It's the same key that will do
the blocking, on a frozen field where it does nothing else.

F9 still toggles back to speech mid-fight.

### 2. The mod lied about the one number it exists to report

> *"I clearly heard it say the foe was at 0 after a hit, but then the foe still
> hit me."*

The log has it exactly:

```
22:51:30  [BGBTL-HP] Squall 184/800 (25%)  Foe 2/600 (0%)
```

2 of 600 is 0.33%, which rounds to nearest-5 as **0**. A fighter with 2 HP left
was announced as empty, and then hit back.

**A live fighter now floors at 5%. Zero means zero.**

### 3. The win was announced a minute late

v0.20.109 called it from the transition to field 675. The BAT shows why that's
wrong:

```
22:51:30  Foe 2/600     <- effectively finished
22:52:38  disarmed by leaving the fight (field 675)
```

**Sixty-eight seconds**, because the knockout animation and the rest of the scene
run first.

**The outcome is now called from HP, the moment it is decided** — *"Foe defeated.
You win."* and *"You are down."* — with the field transition kept only as a
backstop in case HP never resolves.

**Guarded on having seen the fighter alive**, because HP carries over between
attempts. In this same BAT the foe still read 98/600 for **forty seconds** into
the retry before the script re-initialised it to 600:

```
22:50:09  Squall 800/800 (100%)  Foe 98/600 (15%)   <- stale from the last try
22:50:49  Squall 579/800 (70%)   Foe 600/600 (100%) <- script resets it here
```

Without that guard, a retry would announce a win before the first punch.

### Also confirmed, closing the last open question in the HP arc

```
22:50:09  [BGBTL-HP] Squall 800/800 (100%)
```

After choosing *"Try again with HP+200"*, Squall's max reads **800**. So var 380
is the retry bonus — 600 + 200 — and reading max rather than assuming it was the
right call.

### Answered, not built: the in-game dialog

> *"Would it be possible to display one of the game's dialogs with all of this
> information?"*

**What's on screen now:** with `field_main` RET'd out, the render pass still
runs. So it's the FMV **still playing and moving**, with the two fighters frozen
mid-pose and the legend box and both HP bars overlaid. Not a still frame.

**It is possible** — `DialogInject` Phase 1 synthesises a script context and
calls `opcode_mes` directly, and that's BAT-proven to render (2026-05-09). No
ASK needed, exactly as you say.

**But there's a real hazard.** The dialog state machine is driven by `field_main`
— precisely what the briefing RETs out. The dialog would have to be opened and
allowed to reach state `0xD` (~450 ms) *before* the freeze, and a dialog that
can't close while frozen would block the fight. That trades a session-ending
failure mode for polish, so it's written down rather than shipped blind. Happy to
do it as its own build where it's the only thing that can break.

**One thing worth knowing:** you pressed F11 during the briefing and **nothing
was logged in either log for the whole session** — the screenshot never happened.
F9, X and A all registered, so it isn't the key poll. Unexplained, and worth a
look.

### Verification

* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, and it now asserts the
  zero-floor (2/600 and 1/800 both non-zero, −1/600 zero) and the **outcome logic
  end-to-end against the mapped variable page** — stale carry-over ignored, 2 HP
  not a defeat, 0 HP a win — alongside every v0.20.108/109 check.
* `lint_seh` OK (85 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  13 checks / 0 failures; `garden_aboard_test` and `world_map_harness` pass.

**⚠ `field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
SPLIT BEFORE THE NEXT EDIT.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

1. The briefing should describe the tone, and **A should play it** while you're
   on that screen.
2. During the fight, blocking should be cued by that tone, not by speech.
3. **"Foe 0" should never be spoken while the foe can still fight** — the lowest
   you should hear before a defeat is 5.
4. **"Foe defeated. You win."** should land the moment the foe drops, not a
   minute later at the scene change.
