# wmsetus.obj Section 8 — Field-Entry Bytecode (Decoded)

**Source:** v0.14.92 BAT (2026-05-05 20:30) — `[TRIGGER-DUMP]` of `wmsetus.obj` Sections 7 + 8 from `ff8_world.log`, decoded by the Python disassembler in this session's bash sandbox using the opcode dispatch table mapped from FF8_EN.exe `sub_546100` plus the multi-mode parser state from `sub_545F10`.

**Status:** Authoritative reference for v0.14.93+ AD trigger-data hardcoding. Supersedes the wm2field-style hypothesis from `World Map Entry Trigger Coordinates deep research results.md` (Sections 17/18 are not trigger geometry).

---

## Architecture summary

The world-map tick at `sub_53FAC0` calls `sub_545EA0` each frame on foot (`[0x2036b70] == 0`). `sub_545EA0` reads from `[0x2040070]` (= wmsetus Section 8, 2652 bytes) and walks it via `sub_545F10`, which dispatches opcodes through a jump table at `[0x546cac]` indexed by `[opcode + 0x546d3c]`. Section 8 is a bytecode program; `sub_546100` provides the per-opcode handlers.

When a program path evaluates to "match", `sub_545EA0` extracts the wmField/location ID and fires the field transition via `sub_544630`. (`sub_541C80` is the separate ENCOUNTER trigger using Section 1 + Section 2's terrain map; not relevant for AD steering.)

**Section→runtime-pointer map** (from `sub_542DA0` body, first 8 iterations):

| Section | Size (bytes) | Runtime pointer | Purpose |
|---|---|---|---|
| 1 | 392 | `[0x2040068]` | Encounter table (terrain/vehicle key) |
| 2 | 772 | `[0x2040330]` | Region map: 4-byte header + 32×24 segment-region bytes |
| 3 | 88 | `[0x2040090]` | (unknown) |
| 4 | 1348 | `[0x2036be8]` | Encounter destinations |
| 5 | 8 | `[0x203ed40]` | (unknown, very small) |
| 6 | 68 | `[0x2040080]` | (unknown) |
| 7 | 56 | `[0x2040074]` | Region-ID permutation table (see below) |
| **8** | **2652** | **`[0x2040070]`** | **FIELD-ENTRY BYTECODE** |

---

## Section 8 layout

```
0x0000..0x0093   38-entry u32 LE offset table (one per program)
0x0094..0x0097   u32 0x00000000 terminator
0x0098..0x009B   u16 0xFF01 0x0000 — global section-begin marker
0x009C..0x0A57   bytecode (38 programs, terminated by 0xFF16 each)
0x0A58..0x0A5B   u32 0x00000000 terminator
```

Each program starts with a `0xFF06 <location_id>` header. Subsequent programs are separated by a `0xFF01 0x0000` token. The offset table's first entry (0x009C) skips the global `0xFF01` marker and points directly at the first program's `0xFF06`; subsequent entries point at each program's separator `0xFF01`.

---

## Section 7 layout (56 bytes)

14 records of `1D 00 NN 3C` where `NN` ∈ {`00, 0B, 03, 05, 04, 01, 02, 09, 0A, 07, 08, 06`} (12 entries, a permutation of 0..11), then a special record `1D 00 3E 38`, then a 4-byte null terminator.

Hypothesis: this is a **region-ID-to-physical-region permutation table** — Section 7 maps the 12 major world-map continental regions onto canonical IDs that Section 2 references. `1D` is constant across rows, suggesting a record-type tag; the variable byte is the region permutation index. The `3E 38` special entry may represent a "world-wide / any region" virtual marker.

The `0xFF08 <region>` opcode in Section 8 references region IDs in the 0..0x45 (69) range — wider than the 12-entry Section 7 table, so the relationship between Section 7 indices and Section 8 region operands needs cross-referencing with Section 2's full 32×24 region-byte map (which v0.14.93 will dump).

---

## Opcode table (decoded)

| Opcode | Mnemonic | Operand semantics |
|---|---|---|
| `0xFF01` | `BEGIN_PROG` | Program separator (operand always `0x0000`) |
| `0xFF02` | `STORY_FLAG_GTE` | Savemap word at `[0x2036bde/0x2036bdf]` must be ≥ operand |
| `0xFF03` | `STORY_FLAG_LT` | Savemap word must be < operand |
| `0xFF04` | `BEGIN_CHECK` | State-machine entry into clause-evaluation mode |
| `0xFF05` | `END_CLAUSE` | End of an AND-clause |
| `0xFF06` | `PROG_HEADER` | Program header; operand = location/wmField ID |
| `0xFF08` | `REGION_EQ` | Player's segment region-byte must equal operand |
| `0xFF09` | `VEHICLE_EQ` | Locomotion byte must equal operand (see vehicle table below) |
| `0xFF0A` | `OR_BLOCK_A` | Begin OR-alternative A |
| `0xFF0B` | `AND` | AND combinator within a clause |
| `0xFF0C` | `OR_BLOCK_B` | Begin OR-alternative B |
| `0xFF0D` | `OR_BLOCK_C` | Begin OR-alternative C (rare) |
| `0xFF0E` | `FOLLOW_LINK` | Jump to offset (intra-section follow-link; not seen in this dump but referenced by `sub_545EA0`) |
| `0xFF0F` | `UNK_0F` | Unknown — operand pattern `0xNNXX`; appears in story-windowed clauses (likely additional condition flag) |
| `0xFF10` | `UNK_10` | Unknown — appears in pairs with `0xFF0F` for multi-region locations |
| `0xFF11` | `UNK_11` | Unknown — operand pattern `0xNNXX`; appears alongside `0xFF0F` |
| `0xFF12` | `UNK_12` | Unknown — operand pattern `0xNNXX` |
| `0xFF13` | `UNK_13` | Unknown (rare) |
| `0xFF16` | `END_PROG` | End-of-program success terminator |
| `0xFF20` | `UNK_20` | Operand `0x0040` consistently — likely a vehicle-state flag (appears with Ragnarok) |
| `0xFF21` | `UNK_21` | Unknown (rare) |

The `UNK_0F`/`UNK_10`/`UNK_11`/`UNK_12` opcodes likely encode story-window endpoints or secondary-region requirements; their consistent operand patterns (`0x0FFF`, `0x0800`, `0x09FF`, `0x1000`, `0x1A00`, `0x17FF`, `0x1800`, `0x19FF`, `0x1B58`, `0x12FC`, `0x1384`, etc.) suggest they're 16-bit numeric thresholds. v0.14.93 will leave them as opaque flags; if AD steering needs them, they'll be decoded against runtime BAT logs of actual entry events.

---

## Vehicle ID encoding (operands of `0xFF09`)

| Operand | Meaning | Notes |
|---|---|---|
| `0x80` | "Squall foot" | High bit set + party-leader index 0 — most common operand |
| `0x84` | "Selphie foot" / alt-leader foot | Appears alongside `0x80` for Esthar/Centra-area locations and the orphanage (Edea's House) cluster — likely Selphie's POV (canonical mode 6) with the `0x80` "foot flag" applied |
| `0x30` | Garden (mobile B-Garden) | Matches canonical locomotion enum |
| `0x31` | Chocobo | Matches canonical enum |
| `0x32` | Ragnarok | Matches canonical enum |

For AD purposes, both `0x80` and `0x84` are treated as "on foot" — the player's party-leader index doesn't affect navigation reachability.

---

## Program-by-program decode

| Idx | Off | LocID | StoryGate | TopVeh | Clauses (vehicle, region, story-range) |
|---|---|---|---|---|---|
| 0 | 0x009C | 0x0031 | ≥750 | – | (foot, 0x14) (Choco, 0x14) |
| 1 | 0x00D8 | 0x0051 | – | – | (foot, 0x21) (Choco, 0x21) |
| 2 | 0x0110 | 0x0091 | – | – | (foot, 0x22) (Choco, 0x22) |
| 3 | 0x0148 | 0x0095 | ≥750 | – | (foot, 0x13) (Choco, 0x13) |
| 4 | 0x0184 | 0x0096 | ≥750 | – | (foot, 0x13 +UNK12=0x1000) (Choco, 0x13 +UNK12=0x1000) (foot, 0x23 +UNK10=0x0FFF) (Choco, 0x23 +UNK10=0x0FFF) |
| 5 | 0x01F8 | 0x00DB | – | – | (foot, 0x24) (Choco, 0x24) |
| 6 | 0x0230 | 0x00EA | – | – | (foot, 0x09) |
| 7 | 0x0254 | 0x00EE | ≥36 | – | (foot, 0x03) (footAlt, 0x03) |
| 8 | 0x0290 | 0x0108 | ≥333 | – | (foot, 0x08) (footAlt, 0x08) |
| 9 | 0x02CC | 0x010B | ≥290 | – | (foot, 0x06 [story 0..490] +UNK11=0x0C7C, +UNK0F=0x1384, +UNK12=0x127C, +UNK10=0x1984) (foot, 0x07 [story 0..3900] +UNK12=0x1B58) |
| 10 | 0x0324 | 0x010C | – | – | (foot, 0x05 [story 290..315]) |
| 11 | 0x0350 | 0x0111 | – | – | (foot, 0x01) (footAlt, 0x01) |
| 12 | 0x0388 | 0x0112 | <570 | – | (foot, 0x00) |
| 13 | 0x03B0 | 0x0113 | – | – | (foot, 0x02 +UNK11=0x15A0) (foot, 0x00 [story 0..570] +UNK0F=0x0800) |
| 14 | 0x03F4 | 0x0117 | – | – | (foot, 0x16 +UNK0F=0x09FF) (Choco, 0x16 +UNK0F=0x09FF) (foot, 0x17 +UNK11=0x0A00) (Choco, 0x17 +UNK11=0x0A00) |
| 15 | 0x0464 | 0x0147 | ≥350 <490 | – | (foot, 0x0B +UNK12=0x0598) (footAlt, 0x0B +UNK12=0x0598) |
| 16 | 0x04AC | 0x0169 | ≥350 | – | (foot, 0x0A) (footAlt, 0x0A) |
| 17 | 0x04E8 | 0x016D | ≥205 | – | (foot, 0x04) |
| **18** | **0x0510** | **0x0172** | **≥3900** | **Choco** | *(no clause — top-level Chocobo)* |
| **19** | **0x0530** | **0x0172** | **≥636** | **Ragnarok** | (–, 0x0D [story 0..3900] +UNK20=0x0040) |
| **20** | **0x0560** | **0x0172** | **≥636** | **Garden** | (–, 0x0C [story 0..3900]) |
| 21 | 0x0590 | 0x0175 | ≥1600 | – | (foot, 0x18 [story 0..3000]) (foot, 0x2E [story 3000..3900]) |
| 22 | 0x05DC | 0x0176 | – | – | (foot, 0x19) |
| 23 | 0x0600 | 0x017A | ≥1750 | – | (foot, 0x1C [story 0..3000]) (foot, 0x39 [story 3000..5000]) |
| 24 | 0x064C | 0x0189 | ≥750 | – | (foot, 0x0E +UNK11=0x1800) (foot, 0x0F +UNK0F=0x17FF) |
| **25** | **0x0690** | **0x0196** | **≥1750** | **–** | (Rag, 0x1B [story 0..3000] +UNK0F=0x19FF +UNK20=0x40) (Rag, 0x44 [story 3000..3900] +UNK0F=0x19FF +UNK20=0x40) (Choco, 0x44 [story 3900..∞] +UNK0F=0x1A00) (foot, 0x1A [story 0..3900] +UNK11=0x1A00) (footAlt, 0x1A [story 0..3900] +UNK11=0x1A00) |
| 26 | 0x073C | 0x0197 | ≥1750 | – | (foot, 0x1A [story 0..3900]) (footAlt, 0x1A [story 0..3900]) |
| 27 | 0x0780 | 0x01B6 | ≥1750 | – | (foot, 0x1A [story 0..3900]) (footAlt, 0x1A [story 0..3900]) |
| 28 | 0x07C4 | 0x01B7 | ≥1750 | – | (foot, 0x1A [story 0..3900]) (footAlt, 0x1A [story 0..3900]) |
| 29 | 0x0808 | 0x01B9 | ≥1750 | – | (foot, 0x1E [story 0..3000]) (foot, 0x45 [story 3000..∞]) |
| 30 | 0x084C | 0x01BB | ≥1750 | – | (foot, 0x1D [story 0..3000]) (foot, 0x2F) |
| 31 | 0x088C | 0x01D2 | – | – | (foot, 0x27) (Choco, 0x27) |
| **32** | **0x08C4** | **0x01FA** | **≥1750** | **–** | (foot, 0x1F [story 0..2500]) (foot, 0x30 [story 2500..3000]) (foot, 0x31 [story 3000..3900]) (foot, 0x1F [story 3900..5000]) |
| 33 | 0x0948 | 0x0250 | – | – | (foot, 0x10) (Choco, 0x10) |
| 34 | 0x0980 | 0x028C | ≥900 | – | (foot, 0x12) (Choco, 0x12) |
| 35 | 0x09C0 | 0x028D | – | – | (foot, 0x26) (Choco, 0x26) |
| 36 | 0x09F8 | 0x02B5 | – | – | (foot, 0x25) (Choco, 0x25) |
| 37 | 0x0A30 | 0x02C1 | – | Ragnarok | (–, 0x15 +UNK20=0x40) |

**Notable program clusters:**

- **Programs 18–20 (locID 0x0172, "mobile destination")** — three vehicle-specific paths to the same location: Chocobo (story ≥3900, late game), Ragnarok (story ≥636, region 0x0D), Garden (story ≥636, region 0x0C). Likely the **mobile B-Garden destination** since the disc-2+ flying Garden has separate landing rules per vehicle.
- **Programs 25–28 (locID 0x0196 / 0x0197 / 0x01B6 / 0x01B7)** — all share region 0x1A with foot+footAlt. Likely the **Edea's House / orphanage cluster** where Selphie's POV (`0x84`) is active.
- **Program 25 (locID 0x0196)** — most complex (172 bytes, 5 OR-clauses, regions 0x1B/0x44/0x1A across multiple story windows for Ragnarok/Chocobo/foot). Multi-vehicle late-game-evolving location.
- **Program 32 (locID 0x01FA, "Esthar candidate")** — 4 story-windowed regions (0x1F → 0x30 → 0x31 → 0x1F across story 0..5000). Heavy story-conditional access — likely **Esthar City** which is locked until Disc 3 then opens.
- **Program 37 (locID 0x02C1, Ragnarok-only, region 0x15 +UNK20)** — Ragnarok-only entry. Likely the **Lunar Gate or alien ship**.

---

## Region usage summary (45 unique regions referenced)

Most regions are owned by a single program (= narrow-entrance location). Region 0x1A is shared by 4 programs (the orphanage cluster). Region 0x44 is shared by 2 programs (program 25's Ragnarok / Chocobo paths to the same location).

| Region | Programs |
|---|---|
| 0x00 | prog12, prog13 |
| 0x01 | prog11 |
| 0x02 | prog13 |
| 0x03 | prog7 |
| 0x04 | prog17 |
| 0x05 | prog10 |
| 0x06 | prog9 |
| 0x07 | prog9 |
| 0x08 | prog8 |
| 0x09 | prog6 |
| 0x0A | prog16 |
| 0x0B | prog15 |
| 0x0C | prog20 (Garden) |
| 0x0D | prog19 (Ragnarok) |
| 0x0E | prog24 |
| 0x0F | prog24 |
| 0x10 | prog33 |
| 0x12 | prog34 |
| 0x13 | prog3, prog4 |
| 0x14 | prog0 |
| 0x15 | prog37 (Ragnarok) |
| 0x16 | prog14 |
| 0x17 | prog14 |
| 0x18 | prog21 |
| 0x19 | prog22 |
| 0x1A | prog25, prog26, prog27, prog28 |
| 0x1B | prog25 (Ragnarok) |
| 0x1C | prog23 |
| 0x1D | prog30 |
| 0x1E | prog29 |
| 0x1F | prog32 |
| 0x21 | prog1 |
| 0x22 | prog2 |
| 0x23 | prog4 |
| 0x24 | prog5 |
| 0x25 | prog36 |
| 0x26 | prog35 |
| 0x27 | prog31 |
| 0x2E | prog21 |
| 0x2F | prog30 |
| 0x30 | prog32 |
| 0x31 | prog32 |
| 0x39 | prog23 |
| 0x44 | prog25 |
| 0x45 | prog29 |

---

## Correspondence with `s_locations[]` catalog

The catalog has 38 entries (26 main + 7 chocobo + 4 alien + Fire Cavern). Section 8 has 38 programs. **One-to-one mapping is the leading hypothesis.** The location-ID-to-name mapping requires either:

1. A deep research lookup of FF8 field ID → field name (e.g. via Deling or Maki tools), or
2. An empirical capture: enter each location in-game, log the field ID resolved by the engine, match against this program list. v0.14.93 can add an instrumentation hook on `sub_545EA0`'s return path that logs `[FIELD-ENTRY]` events with the resolved location ID; Aaron's natural play activates each entry trigger over a session.

Until the mapping lands, AD targeting can use the **region-byte indirect approach**: for each catalog entry's (X, Y), compute its segment, look up Section 2 to find the region byte, then find all segments sharing that region byte (= all valid trigger positions for that location), and AD steers toward the nearest such segment. This works without the field-ID-to-name mapping because the player's position itself selects the program at runtime.

---

## v0.14.93 build plan (preview)

- Extend `WMSETUS_DUMP_SECTIONS_1IDX[]` to include Section 2 (so we get the segment→region byte map).
- Hardcode the 38 programs above as a static `s_triggerPrograms[]` C++ array (each entry: `{loc_id, top_story_gte, top_story_lt, top_vehicle, clauses[]}`; clauses hold `{vehicle, region, story_lo, story_hi}`).
- BAT to confirm Section 2 dump succeeds and matches our prediction (4-byte header + 32×24 = 768-byte map).
- v0.14.94 wires the decoded data into `StartAutoDrive`'s targeting + arrival check (region-byte-based).

---

## v0.14.92 BAT artifacts (provenance)

- `Logs/build_latest.log` — build clean at 20:30:09 UTC-7
- `Logs/ff8_world.log` — full `[TRIGGER-DUMP]` of Sections 7 + 8 plus the 48-section sanity-check
- Bash sandbox at `/home/claude/ff8/disasm.py` + `build_summary.py` — Python disassembler implementing the opcode table and program walker (lost on next sandbox reset, but the decoded output in this document is the durable artifact)
