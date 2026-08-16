## v0.21.5

#79: **the entry interpreter, re-implemented from the exe and run live — with a
known-good and a known-bad going through identical code.**

---

### Why re-implement rather than probe again

Reading single addresses from the mod's own thread has now been wrong twice. The
v0.21.3 "master gate" — `test byte ptr [eax+0x0E], 8` — turned out to read a
**per-frame pointer that churns every tick** (`01FAACD8`, `01FAC720`,
`01FB6528`, `0201A878`…), and it printed `bit3=CLEAR` on every line *including
while an entry demonstrably succeeded*. A sampled address proves nothing when
you cannot tell a stable value from a transient one.

The control experiment gave the thing that does prove something. **Chocobo
Forest 7 — the segment next door — opened on demand.** So the engine's walker
runs, entries work, and Edea's House alone is refused.

So this build stops sampling and evaluates **the whole 38-program set**, with
the opcode semantics read out of the dispatch tables at `0x546CAC` / `0x546D3C`,
against the same state the game reads. One case the game accepted, one it
refused, through the same code.

### What the model says

`tests/trigwalk_test.cpp`:

```
Chocobo Forest 7 (-20953,68906): program 35, destination 38 -- the game agrees
Edea's House     (-29585,70739): program 34 verdict = MATCH, destination 18
```

**The model reproduces the known-good exactly, and says the known-bad should
fire.** Every condition the exe names — segment 652, story 912 past the 900
gate, on foot, `UNK21` bit 0 — is satisfied at the position Aaron's screenshot
puts at the orphanage wall.

That is a real result, not a dead end. It moves the search off the *conditions*
and onto what happens after them: `sub_545EA0` extracts the destination and
hands it to `sub_544630`. **Destination 18 is the next thing to decode** — the
table that turns a destination id into a field, and whatever gating lives there.

The test also proves the gates bite, so a MATCH means something: story 899 fails
the window, `UNK21` bit 1 fails the bit, the `UNK21` skip bypasses it, a Ragnarok
fails the vehicle clause, and the pre-v0.21.1 marker fails the segment. And it
checks the table's shape — 38 programs, 75 clauses, only segment 370 shared, by
its three vehicle variants.

### The live report

New `[TRIGWALK]` line, once a second on the world map and again at every sweep
start, riding the throttle the `[TRIGEVAL]` line already had:

```
[TRIGWALK] tick seg=652 prog 34 -> **MATCH, destination 18** (story=912 veh=0
           unk21 bit=0 skip=0). If no field loads, every condition the exe names
           is satisfied and the refusal is elsewhere.
```

or, when something does refuse:

```
[TRIGWALK] tick seg=652 prog 34 -> refused: story window
           (story=880 needs [900,0) | veh=0 | unk21 bit=0 needs 0 skip=0)
```

Only programs covering the player's current square are reported, so it is one or
two lines a second, not thirty-eight.

### Verification

* `trigwalk_test` **OK (0 bad)** — new gate.
* `trigseg_test` needed a one-line stub after `TriggerEvalTick` gained the walk
  call; **it caught that as a link error immediately**, which is the gate doing
  its job.
* `pathdecimate_test`, `vehsig_test`, `catalog_story_test`, `garden_harness`,
  `garden_aboard_test`, `world_map_harness`, `minigame_bgbtl_compile`,
  `lint_seh` (88 files) — all pass.
* Read-only. No steering, planning or catalog logic changed.

**NOT MSVC-built.**

### BAT

Stand at Edea's House for a few seconds, then at Chocobo Forest 7 for a few
seconds, and send `ff8_world.log`. Two possibilities, and both are progress:

* **Both report MATCH** — the model is right, the conditions are all satisfied,
  and the difference is downstream in the destination table. That is where I go
  next, and it is a much smaller search than the one this replaces.
* **They disagree with what the game did** — then the model is wrong in a way
  the log will name, and the line says which clause.
