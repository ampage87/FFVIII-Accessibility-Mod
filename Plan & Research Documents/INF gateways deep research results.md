# FF8 INF Gateway System — Deep Research Findings (2026-03-19)

## Summary

The bghall_1 hall exits (l1-l6) do NOT trigger field transitions via JSM scripts.
They are camera-pan-only entities. The actual transitions come from **INF gateways** —
an engine-level, data-driven mechanism that checks player position against trigger
lines every frame, entirely independent of JSM opcodes.

## Key Findings

### 1. Opcode 0x5D is MAPJUMPON, NOT WORLDMAPJUMP

| Opcode | Name | Function |
|--------|------|----------|
| 0x5C | MAPJUMPO | Overwrite/activate an INF gateway's destination data at runtime |
| 0x5D | MAPJUMPON | Enable INF gateway transitions (by gateway ID or globally) |
| 0x5E | MAPJUMPOFF | Disable INF gateway transitions |
| 0x10D | WORLDMAPJUMP | Jump to world map (extended opcode via 0x1C prefix) |

WORLDMAPJUMP is an extended opcode (>0xFF) that requires the 0x1C dispatch mechanism.
0x5D is a PRIMARY opcode (< 0x100) and appears directly as the high byte of the instruction word.

### 2. Dual Transition Architecture

**System A — Script-driven (MAPJUMP family):** Entity scripts explicitly call MAPJUMP/MAPJUMP3/DISCJUMP.
Combined with SETLINE trigger lines. This is the "explicit, scripted" path.

**System B — INF gateway-driven (engine-level):** Each field's INF file defines up to 12 gateway entries.
Each gateway has a trigger line (two 3D vertices) and destination data (field ID, spawn coords, walkmesh tri).
The engine checks player walkmesh position against all active gateway lines every frame inside field_main_loop.
When a crossing is detected, the engine initiates a transition with ZERO script involvement.

Gateway transitions are toggled by MAPJUMPON (0x5D) and MAPJUMPOFF (0x5E).
Gateway destinations can be overwritten at runtime by MAPJUMPO (0x5C).

### 3. INF Format Versions (why data appeared as "garbage")

| File size | Version | Difference |
|-----------|---------|------------|
| 672 bytes | Standard PC | Full format with unknown data block + PVP field |
| 576 bytes | Variant 1 | Unknown data = 4 bytes, no PVP field |
| 480 bytes | Variant 2 (FF7-like) | No unknown data in gateways |
| 384 bytes | Variant 3 (oldest) | Only one camera range, no screen ranges |

If a parser assumes 672-byte format for a field using 480/576-byte format, every gateway
entry's fields read at WRONG OFFSETS. The trigger line vertices bleed into destination
fields, destination field IDs read from coordinate data, etc. This produces the "garbage"
we observed previously.

The engine detects format by checking INF section byte count and adjusts parsing.
Tools like Deling implement this detection. A fixed-format parser produces garbage
for fields using alternate layouts.

### 4. l1-l6 Are Camera Pan Only

The six l1-l6 entities handle camera scrolling as the player approaches hallway edges.
When player crosses a SETLINE, the entity script executes camera operations (scrolling
background layers) but does NOT call MAPJUMP.

The sequence: player walks toward edge → crosses SETLINE → camera scrolls → player
continues → crosses INF gateway line → engine triggers field transition.

### 5. What To Look For in bghall_1

1. **Entity 0's init script** should call MAPJUMPON (0x5D) to enable gateway checking
2. **Entity 0 may also call MAPJUMPO (0x5C)** to overwrite gateway destinations with correct values
3. The INF file's 12 gateway entries define trigger lines at each hallway exit
4. POPM_W writes to addresses 1024-1041 are field-local temporary variables for camera state, NOT transition data

### 6. Opcodes 0x13 and 0x19 (Previously Investigated)

- 0x13 = PSHAC (Push Actor) — pushes current entity's actor ID onto stack. Self-identification.
- 0x19 = PREQEW (Party Request Execute Wait) — sends execution request to party member entity.
Neither is transition-related.

### 7. Engine Addresses (for future runtime hooks)

- Module index variable: 0x01CE4760 (FF8 PC 2000; find Steam 2013 equivalent via FFNx common_externals._mode)
- field_main_loop: offset +0x144 from main_loop
- field_main_exit: offset +0x13C from main_loop
- current_field_id (WORD*): offset +0x21F from main_loop → approx FF8_EN.exe+18D2FC0
- field_fade_transition_sub_472990: offset +0x19E from field_main_loop

Gateway trigger check = 2D line-crossing test (cross product of player movement vector
against gateway line segment). Each frame, engine checks which side of each gateway line
the player stands on. When sign flips = crossing detected.

## Source

ChatGPT deep research, 2026-03-19. Based on Qhimm forums (Halfer 2016), ffrtt.ru wiki
(Aali, myst6re, Shard), FFNx source code, Deling source code.
