## v0.20.102

#minigame-bgbtl: **the Garden-battle fight — instrument first.**

> *"During the Garden Battle there is a mini-game that we need to make
> accessible... We need to ensure the controls — punch, kick, and block are
> properly announced and announce their corresponding keys for the player... I
> would also be interested in adding a Skip option to the Game Over screen."*
> — Aaron
>
> *"There isn't a save point before this scene, and it occurs after a long
> monologue, so the offline sim and minimizing BAT cycles is the best we can do."*

Field **152 `bgbtl_1`**. Squall and a Galbadian Soldier hang off the flying
vehicle and trade punch / kick / block against a timer. Losing loads field **95
`testbl6`**, the Game Over screen. Winning runs a MAPJUMP3 to field **675
`ggback1`**. A blind player can see none of it: not the on-screen legend, not the
two health bars, not what the soldier is about to do.

### Zero BAT was spent finding out how it works

The field archive was pulled out of `field.fs` by byte range and decompressed
here, and **`bgbtl_1.sym` names all 81 script methods** — so every event Aaron
asked for already has a named script behind it:

```
squall::squ_punching0 / squ_kicking0 / squ_guarding0 / squ_punched_up0
gal0::g0_punching0 / g0_kicking0 / g0_guarding0 / g0_punched_up0 / g0_fall0
rinoa::squ_hpcalc0 / gal_hpcalc0
```

The fight is driven by entity 12's `director0::talk` REQ-ing them. So the entire
event stream is **one opcode: 0x014 REQ, operand = target entity, top of the VM
stack = label.** Confirmed **18 of 18** — every REQ in the driver resolves to a
label owned by the entity it names (ent3 → squall's, ent5 → gal0's, ent0 →
rinoa's).

New offline tooling, delivered to `minigame/`: `jsmdis.py` (JSM disassembler
built from the mod's own parser so offline and runtime cannot drift),
`SCRIPT_MAP.txt`, `optable.json`, `announcer.py`.

### The announcer sim earned its keep before a line of C++

`minigame/announcer.py` does not simulate the fight — HP and timing are
engine-side and unknowable offline, and pretending otherwise is how you ship a
model that disagrees with a BAT. It simulates the **announcement policy**, which
is the part that would otherwise cost BAT cycles.

The original spec — announce every move, interrupting — **cuts off 97% of lines
at a 900 ms exchange rate: 34 of 35.** It is unusable, and that was known before
anything was built.

Aaron's revised policy measures at **11%**, and the single casualty is the
controls line — which is exactly why it is now spoken **on entry**, before the
fight, rather than at the first exchange:

* controls once on entry: *"Punch, W. Block, A. Kick, X."*
* **"Block"** when the soldier attacks — nothing else per-move
* **"Defeated."** on `g0_fall0`
* health short (*"You 75"*, *"Foe 50"*) — **not this build, see below**

### Hot-path safety is why the hook is fenced

`field_nav_mapjump_diag.inl` records that hooking SET3 *"is hot-path enough that
ANY interception hangs the infirmary cutscene."* **REQ is far hotter than SET3 —
it is how every script in every field is invoked.**

So:

* the hook is installed **only while `bgbtl_1` is loaded**, and removed on any
  field change;
* the hook body reads two words, writes one slot of a 256-entry lock-free ring,
  and chains. No logging, no allocation, no speech, no locks inside it;
* everything else — logging, speech, variable dumps — happens on the mod's own
  tick in `Update()`;
* the read is inside `__try`, so a bad VM context drops one event rather than
  taking the game down.

### The Skip is exact, not invented

`director0::talk` ends with `push 675, 1019, 3384, 0, 128` then `MAPJUMP3 77`.
Field 675 is `ggback1` — and the v0.20.101 BAT log confirms it independently:
`[fieldload] id=675 name='ggback1'` right after Aaron won.

**F7 on the Game Over screen writes exactly that transition block** (0x01CE4760).
No invented story flag, no forced jump to an FMV — it does what the winning
script does. Precedent: the Dollet Chase and Timber Train skips.

**F7 during the fight** swaps the Block cue between speech and a tone
(`Beep(880,60)`). The reaction window in this fight is **not yet measured**, and
one visit to this scene is far too expensive to spend finding out — so both cues
ship and the player switches in place.

### Health is deliberately NOT announced yet

The script never reads or writes HP; it is engine-side. Announcing a guessed
address is worse than announcing nothing.

So this build **dumps the field variable block (0x01CFE9B8)** on entry, on every
`*_punched_up0`, and once a second. The HP bytes are the ones that only ever
fall, and fall exactly when a hit fires. **One run identifies them with
certainty, and build 2 turns the announcements on** — from measurement, not from
a guess.

The same run settles the other unknown: `g0_punching0` is not a single punch, it
is the soldier's *loop*, and it REQs `g0_kicking0`/`g0_guarding0` from inside
itself before branching back. So the per-attack event may be 48/49 or may be
something this file cannot see. **48/49 is the shipped guess; the trace corrects
it and nothing else changes.**

### Corrected on the record

**`field.fl` index is NOT the field id** — `bgbtl_1` is fl 123 / id 152. This
invalidates an earlier claim in this session comparing Galbadia Garden fl indices
against trigger-table ids.

Also retracted earlier and worth keeping written down: the mod's
`field_archive_jsm_opnames.inl` is **correct**. The 394-entry table at
`0x00B8DE4C` that appeared to contradict it is a *second-stage expression
dispatcher* reached through opcode 0x013. The real field opcode table is
`0x00B8DE94`, 18 entries later, verified on four opcodes against the mod's own
BAT-resolved log.

### Verification

* **`tests/minigame_bgbtl_compile.cpp` (new): 0 errors.** No host harness
  compiles `field_navigation.cpp`, so this is the only pre-MSVC syntax check the
  new module gets — the same gap that let the v0.20.89 declaration-order bug
  reach a build.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass. All unchanged by this build.
* Include order checked: 847 (this module) < 893 < 900 < 1260.

**⚠ `field_navigation.cpp` is 81,385 bytes — 535 from the 81,920 hard fail.
SPLIT BEFORE THE NEXT EDIT THAT NEEDS ROOM.**

**NOT MSVC-built, NOT BAT'd.**

### BAT

This one is expensive to reach, so the run is designed to answer everything at
once. Play the fight normally and **send `ff8_field.log`** — the trace matters
more than the experience this time.

1. On entry you should hear **"Punch, W. Block, A. Kick, X."**
2. During the fight you should hear **"Block"** when the soldier attacks. If it
   fires at the wrong moments, that is the expected outcome of a guess — the log
   fixes it.
3. Press **F7** once during the fight to hear the tone cue instead, and tell me
   which one you could actually react to.
4. **"Defeated."** when the soldier goes down.
5. If you lose, press **F7** on the Game Over screen — it should drop you into
   Galbadia Garden as though you had won.

Grep `[BGBTL]`, `[BGBTL-REQ]` and `[BGBTL-VARS]`.
