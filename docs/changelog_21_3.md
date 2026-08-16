## v0.21.3

#79: **every condition passes and the door is still shut, so instrument the one
gate above them all.**

---

### What the v0.21.2 instrument said

Three sweep-starts and 215 ticks, all identical:

```
[TRIGEVAL] sweep-start pos(-28950,70102) liveSeg=652 box x[-32768,-24576] y[65536,73728]
           | posFlag=0 cachedBlock=(0,0) cachedSeg=0 (live position is used)
           | story=912 | UNK21 skip=0 bit=0 | vehId=0
```

Every one of them is the answer I was hoping *not* to get:

| condition | program 34 requires | measured |
|---|---|---|
| segment | 652 | **652** ✓ |
| whose position | — | **live** — not the parked Garden ✓ |
| story | ≥ 900 | **912** ✓ |
| `UNK21` | bit 0 == 0 | **0** ✓ |
| vehicle | on foot (128) | **vehId 0, on foot** ✓ |

**Every condition the exe's own decode names is satisfied, and no field loads.**
Both of the suspects I raised last build are cleared: the Garden is not stealing
the position test, and there is no world-state flag holding the orphanage shut.

### The one gate left, and it is the first instruction in the walker

```
0x00545EA3  mov  eax, [0x020409FC]
0x00545EAB  test byte ptr [eax + 0x0E], 8
0x00545EAF  jne  <walk the 38 programs>
0x00545EB1  xor  eax, eax
0x00545EB4  ret                        <-- NOTHING IS EVALUATED
```

`sub_545EA0` checks **bit 3 of the byte at `[[0x020409FC] + 0x0E]`** before it
looks at anything else. With that bit clear the game never reads a single entry
program — not Edea's House, not anywhere on the world map. Every `[TRIGEVAL]`
line now carries it:

```
| GATE [020409FC+0x0E]=0x?? bit3=SET (entry armed)
| GATE [020409FC+0x0E]=0x?? bit3=CLEAR <-- NO PROGRAM IS EVALUATED
```

### The control experiment, which costs a two-minute walk

Decoding the neighbouring programs gives a near-perfect control. **Chocobo
Forest 7 is segment 653 — the square immediately east of Edea's House** — and
its program (35) is as simple as they come:

```
SEGMENT== 653
BEGIN
  VEHICLE==128 (on foot)  AND  DEST 38
  VEHICLE==49  (chocobo)  AND  DEST 38
END
```

**No story gate. No `UNK21`. No coordinate bounds.** Nothing but "be in segment
653 on foot". It is already in the catalog, roughly 8 km east of where you are
standing.

* **If the Chocobo Forest opens and Edea's House does not** — the master gate is
  set, the walker is running, and program 34 is being refused for a reason still
  unnamed. That is a much narrower search than the one I have been running.
* **If neither opens** — the gate is clear and nothing on the world map is
  enterable right now. That is a game-state condition, not a location problem,
  and the `GATE` field will say so outright.

Either way the next step stops being a guess.

### Verification

* `trigseg_test` OK (0 bad); `vehsig_test` OK; `catalog_story_test` 0 failures;
  `lint_seh` OK (88 files); `minigame_bgbtl_compile` 0 errors / 0 bad;
  `garden_harness` 26 ok / 0 bad; `garden_aboard_test` and `world_map_harness`
  pass.
* Read-only instrument. One extra field on a line that already existed.

**NOT MSVC-built.**

### BAT

1. Stand at Edea's House for a few seconds — the ticks are enough, no need to
   drive.
2. Then auto-drive to **Chocobo Forest 7** and walk into it.
3. Send `ff8_world.log`.

I only need the `GATE` field and whether the forest let you in.
