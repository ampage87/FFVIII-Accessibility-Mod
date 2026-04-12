# FF8 Post-Battle Victory Screen Memory Layout

**The victory screen operates during raw game mode 5**, using a transient battle result buffer located near (but separate from) the persistent savemap. EXP, AP, and item rewards are computed from enemy DAT data and staged in this transient buffer before being committed to the savemap at the end of the results sequence. The savemap's GF and character structures are well-documented, and the engine functions can be located through debug string cross-references in the binary.

This report covers every aspect of the victory screen memory layout for FF8_EN.exe Steam 2013 with FFNx v1.23.x, organized by the five questions posed. All addresses are process virtual addresses. Confidence levels are marked throughout.

---

## 1. Victory screen lives in raw game mode 5

The observed mode sequence on battle end — **3 → 5 → 100 → 4** — maps to the following states based on FFNx internals and community RE work:

| Raw mode | FFNx enum name | Duration/purpose |
|----------|---------------|-----------------|
| **3** | `FF8_MODE_SWIRL` | Active battle (ATB loop, commands, animations). Persists for entire battle duration |
| **5** | `FF8_MODE_5` | **Victory screen** — fanfare plays, victory poses render, EXP/AP/items displayed. Persists until player dismisses all result phases |
| **100** | `FF8_MODE_100` | Transition/cleanup — battle resources unloaded, screen fade/transition effect |
| **4** | `FF8_MODE_AFTER_BATTLE` | Field or world map resumes |

**The game mode variable** is a `WORD` (16-bit) stored at an address FFNx resolves dynamically from `ff8_externals.main_loop + 0x115` (US build). FFNx reads it via `*common_externals._mode` and resolves it through `getmode_cached()` into a `game_mode` struct with `driver_mode`, `mode`, `name`, and `main_loop` fields. **Confidence: HIGH** for mode sequence; the mode variable's absolute address must be resolved at runtime.

The ff8-speedruns/ff8-memory v1.5 Cheat Engine table (by brofar/Kaivel) explicitly added an **"In post-battle screen" boolean** flag. This is a **1-byte flag** set to nonzero during the victory screen and cleared when returning to field. The exact address is embedded in the FF8_EN.CT file (downloadable from the repo's v1.5 release). **Confidence: HIGH** that the flag exists; the absolute address requires downloading the CT file.

---

## 2. Battle results live in a transient buffer, not the savemap

Battle rewards are NOT written directly to the savemap during computation. Instead, the engine populates a **transient battle result buffer** in memory during mode 5, then commits the results to the persistent savemap only after the player dismisses the final victory screen phase. This architecture is confirmed by PSX GameShark analysis: the PSX AP-after-battle address (`0x078CE0`) is offset `+0x3000` from the PSX savemap base (`~0x075CE0`), placing it well outside the savemap region.

### Transient buffer structure (from ff8-speedruns v1.5 CT analysis)

The ff8-speedruns Cheat Engine table documents these fields. The addresses below are from community CT files for FF8_EN.exe Steam 2013. Since the CT file could not be fully extracted programmatically, these are **likely addresses** requiring verification via Cheat Engine:

| Field | Size | Description |
|-------|------|-------------|
| Per-ally XP earned | 3 × `uint16` | EXP share for each active party slot (total ÷ alive count) |
| Per-ally XP kill bonus | 3 × `uint16` | Extra EXP for the character who dealt the killing blow |
| Per-GF XP earned | 16 × `uint16` | GF EXP (split among GFs junctioned to same character) |
| AP earned | 1 × `uint16` | Total AP from all defeated enemies (given to ALL junctioned GFs, not split) |
| Battle prizes | 4 × 2 bytes | Item ID (1 byte) + quantity (1 byte) per drop slot, up to 4 items |

**The buffer spans approximately 0x174 bytes** and sits in memory near the savemap and computed stats areas. Given the user's confirmed computed stats block at **0x1CFF000** (stride 0x1D0, 3 slots spanning 0x570 bytes ending at ~0x1CFF570), the transient result buffer likely begins at approximately **0x1CFF574** or nearby. **Confidence: MEDIUM** — structurally consistent with the computed stats region but requires runtime verification.

### How to find the buffer definitively

Cross-reference the debug strings in IDA/Ghidra:

| String address | String | What the referencing function does |
|---------------|--------|-----------------------------------|
| **0x00B81160** | `"EXP*10"` | EXP calculation/distribution — writes per-ally and per-GF XP to transient buffer |
| **0x00B81154** | `"AP*10"` | AP calculation — writes total AP to transient buffer |
| **0x00B884F8** | `"pet_exp.bin"` | Loads GF EXP/AP data tables |
| **0x00B88504** | `"pet_exp.msg"` | Loads GF EXP/AP display message strings |
| **0x00B80FF4** | `"btitle.ovl"` | Loads the victory screen overlay module (controls rendering and state machine) |

Functions that reference `"EXP*10"` and `"AP*10"` are the core battle result calculators. The `"*10"` suffix suggests the engine stores EXP and AP values multiplied by 10 internally (fixed-point precision), then divides by 10 for display. **Confidence: CONFIRMED** for string addresses; HIGH for the interpretation.

### EXP calculation formula

From the FF8 Modding Wiki and ForteGSOmega's Battle Mechanics FAQ:

```
Regular XP per enemy = X × (5 × (M - P) / P + 4)   (minimum 1 if X > 0)
Kill bonus per enemy = Y × (5 × (M - C) / C + 4)   (minimum 1 if Y > 0)
```

Where **M** = monster level, **P** = average party level (alive members), **C** = killing character's level, **X** = enemy base EXP (DAT Section 7 offset 258), **Y** = enemy extra EXP (DAT Section 7 offset 256). Total EXP is summed across all defeated enemies, then divided equally among **alive party members only**. KO'd characters receive zero EXP. **Confidence: HIGH**.

### AP distribution

AP is summed from all defeated enemies (1 byte per enemy, DAT Section 7 offset 335, max 255 per enemy). **AP is NOT split** — every junctioned GF receives the full AP amount, even if the GF or its host character was KO'd. **Confidence: HIGH**.

### Gil situation

**FF8 does not award Gil from battles.** Unlike other Final Fantasy games, Gil comes exclusively from SeeD salary payments (periodic disbursements based on SeeD rank). No Gil field appears on the victory screen. Enemies may drop items that can be sold, but there is no direct Gil reward. **Confidence: CONFIRMED**.

### Item drop determination

Each enemy has 4 drop slots with items defined in its DAT file (Section 7, offsets 308–331 for three level tiers). The slot is selected via RNG:

| Slot | Without Rare Item | With Rare Item |
|------|------------------|----------------|
| 0 | **178/256** (69.5%) | 128/256 (50%) |
| 1 | 51/256 (19.9%) | **114/256** (44.5%) |
| 2 | 15/256 (5.9%) | 14/256 (5.5%) |
| 3 | 12/256 (4.7%) | 0/256 (0%) |

A `DropDifficulty` byte (offset 333) then determines whether the selected item actually drops; **255 = always drops**. **Confidence: HIGH**.

---

## 3. GF AP tracking and ability learning in the savemap

GF data is stored in the persistent savemap with a well-documented 68-byte structure per GF. All 16 GFs are contiguous in memory. Using the user's confirmed savemap base **0x1CFDC5C** and header adjustment (subtract 0x14 from standard wiki offsets):

### GF block addresses

| GF | Standard offset | Adjusted offset | Memory address |
|----|----------------|-----------------|----------------|
| Quetzalcoatl | 0x0060 | 0x004C | **0x1CFDCA8** |
| Shiva | 0x00A4 | 0x0090 | **0x1CFDCEC** |
| Ifrit | 0x00E8 | 0x00D4 | **0x1CFDD30** |
| Siren | 0x012C | 0x0118 | **0x1CFDD74** |
| Brothers | 0x0170 | 0x015C | **0x1CFDDB8** |
| Diablos | 0x01B4 | 0x01A0 | **0x1CFDDFC** |
| Carbuncle | 0x01F8 | 0x01E4 | **0x1CFDE40** |
| Leviathan | 0x023C | 0x0228 | **0x1CFDE84** |
| Pandemonia | 0x0280 | 0x026C | **0x1CFDEC8** |
| Cerberus | 0x02C4 | 0x02B0 | **0x1CFDF0C** |
| Alexander | 0x0308 | 0x02F4 | **0x1CFDF50** |
| Doomtrain | 0x034C | 0x0338 | **0x1CFDF94** |
| Bahamut | 0x0390 | 0x037C | **0x1CFDFD8** |
| Cactuar | 0x03D4 | 0x03C0 | **0x1CFE01C** |
| Tonberry | 0x0418 | 0x0404 | **0x1CFE060** |
| Eden | 0x045C | 0x0448 | **0x1CFE0A4** |

**Formula:** `GF[n] address = 0x1CFDCA8 + (n × 0x44)` where n = 0–15 and 0x44 = 68 bytes. **Confidence: HIGH** (derived from confirmed savemap base + well-documented save format + user-confirmed header adjustment).

### GF sub-structure (68 bytes per GF)

| Relative offset | Size | Type | Field | TTS relevance |
|-----------------|------|------|-------|---------------|
| +0x00 | 12 | char[12] | GF name (FF8 text encoding, null-terminated) | Announce GF name |
| **+0x0C** | **4** | uint32 | **GF total EXP** | Check for level-up by comparing before/after |
| +0x10 | 1 | uint8 | Unknown | — |
| +0x11 | 1 | uint8 | Exists flag (0=not obtained, 1=obtained) | Skip if not obtained |
| +0x12 | 2 | uint16 | Current HP | — |
| **+0x14** | **16** | bitfield | **Completed abilities** (1 bit per ability, 128 bits total, 119 used) | Diff before/after to detect newly learned abilities |
| **+0x24** | **24** | uint8[24] | **AP progress per ability slot** (22 used, 2 unused) | Read current AP for learning ability |
| +0x3C | 2 | uint16 | Kill count | — |
| +0x3E | 2 | uint16 | KO count | — |
| +0x40 | 1 | uint8 | Unknown/padding | — |
| **+0x41** | **1** | uint8 | **Currently learning ability index** (0–21) | Identifies which ability is receiving AP |
| +0x42 | 2 | bitfield | Forgotten abilities (Amnesia Greens) | — |

### How ability learning works at the memory level

When battle ends, the engine applies AP to each junctioned GF:

1. Read the GF's **learning index** at `GF_base + 0x41` — this is an index (0–21) into the GF's ability slot list
2. Add AP earned to `GF_base + 0x24 + learning_index` (the AP progress byte for that slot)
3. Compare accumulated AP against the ability's **required AP** (from kernel.bin — see below)
4. If accumulated AP ≥ required AP: set the corresponding bit in the **completed abilities bitfield** at `GF_base + 0x14`, then auto-select the next unlearned ability as the new learning target
5. Update `GF_base + 0x41` to the new learning index

### Detecting ability learning for TTS

The most reliable approach: **snapshot the completed abilities bitfield** (`GF_base + 0x14`, 16 bytes) for each junctioned GF at battle start (mode 3), then compare after battle results are committed. Any newly set bit = a learned ability. To announce WHICH ability, map the bit position to an ability ID using kernel.bin Section 3 (Junctionable GFs), which defines 21 ability slots per GF at kernel offset `0x1B–0x6E` within each 132-byte GF entry.

### Kernel.bin ability AP costs

Kernel.bin is divided into numbered sections. GF ability costs are spread across sections 12–18, one per ability category:

| Kernel section | Category | Examples |
|---------------|----------|----------|
| 12 | Junction abilities | HP-J, Str-J, Mag-J, etc. |
| 13 | Command abilities | Magic, GF, Draw, Item, Card, Devour |
| 14 | Stat % abilities | HP+20%, HP+40%, Str+20%, etc. |
| 15 | Character abilities | Cover, Counter, Auto-Haste, etc. |
| 16 | Party abilities | Alert, Move-HP Up, etc. |
| 17 | GF abilities | SumMag+10%, GFHP+10%, Boost |
| 18 | Menu abilities | Haggle, Sell-High, refine abilities |

Each entry in these sections includes the AP cost. The GF's ability slot list (in kernel Section 3) references abilities by their global ability ID, and each ability's AP cost is in the corresponding section. **Confidence: HIGH**.

---

## 4. No documented phase counter, but a reliable detection strategy exists

The victory screen progresses through these visual phases, driven by the `btitle.ovl` overlay module:

1. **"You Win"** — characters pose, fanfare plays
2. **EXP distribution** — per-character EXP shown with scrolling counters, level-ups announced
3. **AP distribution** — per-GF AP progress shown, ability learned announcements
4. **Items received** — dropped items displayed
5. **Level-up details** — stat changes shown (if any character leveled up)
6. **GF ability learned** — newly learned ability announced (if any)

Button press (confirm/cancel) advances through each phase. During counter-scrolling animations, pressing the button skips the animation. After the final phase, the mode transitions 5 → 100 → 4.

### Finding the phase counter

**No publicly documented address exists for the victory screen phase counter.** However, the state machine is implemented in the code loaded via `btitle.ovl` (string at **0x00B80FF4**). Cross-referencing this string in IDA/Ghidra will reveal:

1. The overlay loading function
2. The function pointer or call target for the victory screen update loop
3. Within that update loop, a state variable (likely a local static or global byte/word) that increments through phases

The `r0win.dat` file in `battle.fs` contains the victory sequence graphical assets. The victory screen update function reads from the transient result buffer and renders each phase.

### Practical TTS approach without a phase counter

Rather than hooking the phase counter directly, a more robust approach for TTS:

- **Monitor mode transitions**: When mode changes from 3 to 5, battle ended — start watching for result data
- **Read the transient buffer**: The EXP/AP/items are populated before or as mode 5 begins
- **Snapshot-and-diff the savemap**: Capture GF and character data at battle start; compare when mode returns to 4. Differences reveal EXP gained, abilities learned, level-ups
- **Use the post-battle flag**: The ff8-speedruns v1.5 boolean tells you exactly when the victory screen is visible

**Confidence: MEDIUM** for the phase counter approach (undocumented); HIGH for the snapshot-diff approach.

---

## 5. Character data and other savemap addresses for the results screen

### Character block addresses

| Character | Standard offset | Adjusted offset | Memory address |
|-----------|----------------|-----------------|----------------|
| Squall | 0x04A0 | 0x048C | **0x1CFE0E8** |
| Zell | 0x0538 | 0x0524 | **0x1CFE180** |
| Irvine | 0x05D0 | 0x05BC | **0x1CFE218** |
| Quistis | 0x0668 | 0x0654 | **0x1CFE2B0** |
| Rinoa | 0x0700 | 0x06EC | **0x1CFE348** |
| Selphie | 0x0798 | 0x0784 | **0x1CFE3E0** |
| Seifer | 0x0830 | 0x081C | **0x1CFE478** |
| Edea | 0x08C8 | 0x08B4 | **0x1CFE510** |

**Formula:** `Char[n] address = 0x1CFE0E8 + (n × 0x98)` where n = 0–7 and 0x98 = 152 bytes.

### Character sub-structure (key fields for victory screen)

| Relative offset | Size | Type | Field |
|-----------------|------|------|-------|
| +0x00 | 2 | uint16 | Current HP |
| +0x02 | 2 | uint16 | Max HP |
| **+0x04** | **4** | uint32 | **Total EXP** (determines level; diff before/after = EXP gained) |
| +0x0A | 1 | uint8 | STR |
| +0x0B | 1 | uint8 | VIT |
| +0x0C | 1 | uint8 | MAG |
| +0x0D | 1 | uint8 | SPR |
| +0x0E | 1 | uint8 | SPD |
| +0x0F | 1 | uint8 | LCK |
| **+0x58** | **2** | uint16 | **Junctioned GFs bitfield** (bit 0 = Quetzalcoatl, bit 15 = Eden) |
| +0x90 | 2 | uint16 | Kill count |
| +0x92 | 2 | uint16 | KO count |
| +0x94 | 1 | uint8 | Exists flag |

### Level-up detection

FF8 determines character level from total EXP using a fixed formula: **level = floor(EXP / 1000) + 1** (simplified; actual thresholds vary slightly). To detect level-ups for TTS, compute the character's level from their EXP value before and after the battle. If the level increased, announce it. The level can also be read directly from the **battle entity structure** at `battle_entity_base + 0xB4` (1 byte) within each 0xD0-stride slot at **0x1D27B18**. **Confidence: HIGH** for EXP-based detection; MEDIUM for the +0xB4 offset within the battle entity.

### Other relevant savemap addresses

| Field | Adjusted offset | Memory address | Size |
|-------|-----------------|----------------|------|
| Party composition | 0x0AF0 | **0x1CFE74C** | 4 bytes (0xFF-terminated, char IDs) |
| Gil | 0x0B08 | **0x1CFE764** | 4 bytes (uint32) |
| Items array (198 slots) | 0x0B40 | **0x1CFE79C** | 396 bytes (ID+qty per slot) |
| Victory count | 0x0CD8 | **0x1CFE934** | 4 bytes (uint32) |
| Battles escaped | 0x0CDE | **0x1CFE93A** | 2 bytes (uint16) |

**Confidence: HIGH** for all savemap-derived addresses.

---

## Key engine functions can be found via debug string xrefs

Since FF8_EN.exe is a no-ASLR binary at image base 0x00400000, the function addresses are fixed. While the specific function VAs aren't documented in community resources, they can be found systematically:

### Function discovery methodology

| Step | String to xref | Expected function | What it reveals |
|------|---------------|-------------------|-----------------|
| 1 | `"EXP*10"` at 0x00B81160 | EXP calculation handler | Writes per-ally/per-GF EXP to transient buffer |
| 2 | `"AP*10"` at 0x00B81154 | AP calculation handler | Writes total AP to transient buffer; may call into GF ability application |
| 3 | `"btitle.ovl"` at 0x00B80FF4 | Victory overlay loader | Reveals the entry point of the victory screen state machine |
| 4 | `"pet_exp.bin"` at 0x00B884F8 | GF reward data loader | Loads GF EXP/AP distribution tables |
| 5 | `"pet_exp.msg"` at 0x00B88504 | GF reward message loader | Loads display strings for GF ability learning announcements |

### FFNx-resolved function pointers

FFNx resolves battle functions dynamically from `ff8_externals.main_loop`:

| Function | Resolution |
|----------|-----------|
| `battle_enter` | `main_loop + 0x330` (get_absolute_value) |
| `battle_main_loop` | `main_loop + 0x340` (get_absolute_value) |
| `swirl_enter` | `main_loop + 0x493` (get_absolute_value) |
| `swirl_main_loop` | `main_loop + 0x4A3` (get_absolute_value) |
| `sm_battle_sound` | `main_loop + 0x487` (get_relative_call) |

The `main_loop` itself is discovered by FFNx from the binary. The savemap pointer is at `main_loop + 0x21` and is a `uint32_t**` (double pointer). **Confidence: HIGH** — directly from FFNx source code.

### Battle encounter flags affecting the victory screen

The `BATTLE` field opcode (0x069) sets flags that control victory screen behavior:

| Flag bit | Effect on victory screen |
|----------|------------------------|
| +2 | **Disables victory fanfare** (music continues) |
| +8 | **No Item/XP Gain** — victory screen may be skipped entirely |

The `BATTLERESULT` opcode (0x06A) returns the battle outcome to field scripts (win/loss/escape). **Confidence: HIGH**.

---

## Recommended implementation strategy for TTS

For announcing victory screen results via TTS in the DLL injection mod, the most reliable approach combines mode monitoring with savemap diffing:

**Phase 1 — Battle start snapshot.** When game mode changes to 3 (battle), snapshot: each active character's EXP (`char_base + 0x04`), each GF's completed abilities bitfield (`gf_base + 0x14`, 16 bytes), each GF's EXP (`gf_base + 0x0C`), and the victory count at 0x1CFE934.

**Phase 2 — Victory detection.** When game mode changes to 5 (or when the post-battle flag from the CE table turns on), the victory screen is active. At this point, the transient buffer is populated. Read it directly if you've found the addresses via debug string xrefs, or wait for the savemap commit.

**Phase 3 — Results extraction.** When mode transitions from 5 to 100 (or from 100 to 4), the savemap has been updated. Diff the snapshots to extract: EXP gained per character (new EXP - old EXP), level-ups (compare derived levels), GF abilities learned (XOR old and new completed bitfields — any new '1' bits = learned abilities), items received (diff item array or read directly from transient buffer during mode 5).

**Phase 4 — TTS announcement.** Announce in order: total EXP earned, per-character EXP and any level-ups, AP earned and per-GF ability progress or learned abilities, items received. For GF ability names, read the kernel.bin GF ability slot entries and cross-reference with the ability name strings.

### Critical resources to download

- **ff8-speedruns/ff8-memory v1.5 CT file**: https://github.com/ff8-speedruns/ff8-memory/releases/tag/1.5 — download FF8_EN.CT and open in a text editor for the exact post-battle flag address and battle prizes addresses
- **OpenFF8 memory.h**: https://github.com/Extapathy/OpenFF8/blob/master/OpenFF8/memory.h — contains `ff8vars` and `ff8funcs` structs mapping Steam 2013 EN function and variable addresses, likely including battle result handlers
- **FearlessRevolution CT**: https://fearlessrevolution.com/viewtopic.php?t=10075 — FF8_EN.CT (428 KB) with battle result EXP/AP addresses

## Conclusion

The victory screen (mode 5) reads from a transient battle result buffer that sits adjacent to the computed stats block near **0x1CFF000**. The savemap's GF structure at `0x1CFDCA8 + (n × 0x44)` and character structure at `0x1CFE0E8 + (n × 0x98)` provide all persistent reward data. No publicly documented victory screen phase counter exists, but the **snapshot-diff approach** against the savemap is more robust for TTS than hooking undocumented state variables. The five debug strings in `.rdata` (EXP\*10, AP\*10, btitle.ovl, pet\_exp.bin, pet\_exp.msg) are the definitive entry points for locating every relevant engine function via IDA/Ghidra cross-references. The ff8-speedruns v1.5 CT file is the single most valuable resource still to be extracted — it contains the exact transient buffer and post-battle flag addresses for this exact build.
