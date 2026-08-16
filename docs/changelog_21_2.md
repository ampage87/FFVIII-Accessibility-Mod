## v0.21.2

#79: **the trigger decode was wrong, and the exe says so.**

> *"Still failed to arrive at Edea's House. Took an F11 screenshot when it said
> it was <1km out... Go back to the exe and game files to pin its location down
> like we did with Shumi if necessary."* — Aaron

The screenshot settles the geography: **Squall is standing beside the
orphanage's lighthouse.** The marker is right. The door is not opening for a
different reason, and finding it meant going back to the dispatch tables.

---

### The opcode meanings were the other way round

`sub_546100` dispatches through a jump table at `0x546CAC` indexed by a byte map
at `0x546D3C`. Read those two tables out of `FF8_EN.exe` and the research
document this module was built on does not survive:

| opcode | old reading | what the dispatch table actually says |
|---|---|---|
| `0xFF06` | "program header, location/field id" | slot 2 → `0x00546192`: **segment test** — calls `sub_553910(X,Y)` and compares the operand to its return |
| `0xFF08` | "region equals" | slot 35 — the **destination id**, not a test at all |
| `0xFF0F` | unknown | slot 5 → `0x005463A7`: **X offset < operand** |
| `0xFF10` | unknown | slot 6 → `0x00546406`: **Y offset < operand** |
| `0xFF11` | unknown | slot 7 → `0x00546461`: **X offset > operand** |
| `0xFF12` | unknown | slot 8 → `0x005464C0`: **Y offset > operand** |

and `sub_553910` is four lines:

```
row = ((Y + 0x48000) mod 0x30000) >> 13      ; 24 rows of 8192
col = ((X + 0x60000) &   0x3FFFF) >> 13      ; 32 cols of 8192
return row * 32 + col
```

**A program's "location id" is a segment index — an 8192-unit square — and the
four unknown opcodes are the bounds that narrow it.** The numbers this module
has been reading as field ids and regions are each other.

The transcription reproduces the mod's own segment arithmetic exactly: the old
catalog point (−23150, 62853) prints as `seg(13,19)` in every `PLAN-DEBUG` line
of the 2026-08-15 logs, and 19 × 32 + 13 = **621**.

### What that makes of Edea's House

Re-decoding wmsetus section 8 with the corrected opcodes, **program 34**:

```
SEGMENT== 652        -> x[-32768,-24576]  y[65536,73728]
STORY>=   900
UNK21     0
BEGIN
  VEHICLE==128 (on foot)  AND  DEST 18
  VEHICLE==49  (chocobo)  AND  DEST 18
END
```

**Segment 652 is the orphanage.** Aaron's story word reads 912 (≥ 900 ✓), he is
on foot ✓, and his position at the lighthouse — (−29585, 70739) — is inside that
box ✓. Every condition the mod can model passes.

And **program 32**, the one this module reported last build as the story-locked
Edea's House entrance, is `SEGMENT== 506` = x[81920, 90112] y[24576, 32768] —
the far east of the map, nowhere near Centra. It was never Edea's House. My
"the game has the door locked" conclusion two builds ago was built on that
mis-decode, and Aaron was right to reject it.

**The v0.21.1 marker is inside segment 652 and the pre-v0.21.1 marker was not.**
So that move was correct even though it did not open the door.

### What is left, and why this build instruments instead of guessing

Two conditions the mod has never modelled, either of which explains a silent
door, and **neither is decidable from static analysis**:

**1. `UNK21` (opcode `0xFF21`, handler `0x00546A11`).**

```
if ([0x2040A34] != 0) pass
else pass only if ((*(byte*)([0x20403A4] + 0x6D)) & 1) == operand
```

Program 34's operand is **0**, so that bit has to read 0.

**2. Whose position is tested.** Every position opcode has two paths:

```
if ([0x2040A30] != 0) use cached block coords [0x2040A24]/[0x2040A28], /4
else                  use the live player position
```

If a vehicle still owns the position while Aaron is on foot, the segment test
runs against **the Garden — which he parked 6.3 km away, in segment 621, a
different square.** No amount of walking would ever open that door.

This build logs both, once a second on the world map and again the moment a
drive gives up and starts sweeping:

```
[TRIGEVAL] sweep-start pos(...) liveSeg=652 box x[...] y[...] |
           posFlag=... cachedBlock=(...) cachedSeg=... | story=912 |
           UNK21 skip=... bit=... | vehId=0
```

One BAT reading those numbers answers it. A third guess would not.

### Verification

* `trigseg_test` **OK (0 bad)** — new gate. Five points whose segment the mod
  has already printed from its own independently written arithmetic reproduce
  exactly; segment 652's box is asserted; every point inside four segment boxes
  round-trips; and segment 506 is asserted to be in the far east, not Centra.
* `vehsig_test` OK; `catalog_story_test` 0 failures; `lint_seh` OK (88 files);
  `minigame_bgbtl_compile` 0 errors / 0 bad; `garden_harness` 26 ok / 0 bad;
  `garden_aboard_test` and `world_map_harness` pass.
* Read-only instrument. No steering, planning or catalog logic changed.

**NOT MSVC-built.**

### BAT

Walk to Edea's House and let it sweep — the sweep is now the *point*, not the
failure. Then send me `ff8_world.log` and I will read the answer straight off
the `[TRIGEVAL]` lines:

* `posFlag` non-zero and `cachedSeg` ≠ `liveSeg` → the game is testing the
  Garden's position, not yours, and the fix is to make the mod park it
  differently or clear that ownership.
* `UNK21 bit=1` → there is a world-state flag holding the door, and it becomes
  the next thing to find.
* Both clean → the trigger is firing its test and something downstream is
  eating it, which is a third and much narrower search.

If you would rather not walk it again, standing anywhere inside
**x −32768…−24576, y 65536…73728** for a few seconds produces the same lines.
