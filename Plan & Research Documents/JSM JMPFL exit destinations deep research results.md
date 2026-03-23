# JSM JMPFL Exit Destinations - ChatGPT Research Results

Research completed March 17, 2026.

Source file: `FF8_JSM_bytecode_format_complete_technical_reference.md`
Saved to: `Plan & Research Documents/`

## Key Findings

### 1. No JMPFL in FF8
JMPFL is an FF7 opcode (0x11). FF8 uses **MAPJUMP (0x029)** and **MAPJUMP3 (0x02A)**.

### 2. 32-bit Fixed-Width Stack Machine
Every JSM instruction is exactly 4 bytes. Bit 31 discriminates:
- **Bit 31 = 0**: PSHN_L (push literal). Bits 30-0 = signed value.
- **Bit 31 = 1**: Regular opcode. Bits 30-16 = opcode ID (0x000-0x183). Bits 15-0 = inline param.

### 3. MAPJUMP Stack Arguments
**MAPJUMP (0x029)**: 3 stack args — FieldID (deepest), WalkmeshTriID, Direction
**MAPJUMP3 (0x02A)**: 6 stack args — FieldID (deepest), X, Y, Z, WalkmeshTriID, Direction
**FieldID is always the first (deepest) PSHN_L push before the opcode.**

### 4. JSM Header (CORRECTED)
```
Byte 0: countDoors        (NOT reserved — often 0 because many fields lack doors)
Byte 1: countLines
Byte 2: countBackgrounds
Byte 3: countOthers
Bytes 4-5: uint16 LE scriptDataOffset
Bytes 6-7: uint16 LE totalEntities
```

**Our existing code has byte 0 as "reserved" — this needs fixing for MAPJUMP extraction.**

### 5. Entity Data Ordering (CRITICAL)
Despite header order being Doors/Lines/Backgrounds/Others, the **entity group table** orders entities as:
**Lines → Doors → Backgrounds → Others**

Line entity L (0-based) is at entity group index L directly (offset 8 + L*2).

### 6. Entity Group Entry Format
```c
uint16_t entry = read_u16(jsm + 8 + entityIndex * 2);
int scriptCount = entry & 0x7F;       // bits 0-6: number of methods
int label       = entry >> 7;          // bits 7-15: first script entry index
```

### 7. Related Opcodes
- **DISCJUMP (0x038)**: Disc-change transitions
- **MAPJUMPO (0x05C)**: Variant
- **WORLDMAPJUMP (0x10D)**: World map exits

### 8. ~95% of transitions use literal PSHN_L
Static extraction is reliable for the vast majority of cases. Dynamic destinations (computed from variables) get a -1 sentinel.

## Implementation Plan

Add `LoadJSMExitDestinations()` to `field_archive.cpp`:
1. Extract JSM file for current field
2. Parse header (corrected byte order)
3. For each line entity (indices 0..countLines-1 in entity group table):
   a. Get scriptCount and label from entity group entry
   b. For each method, scan bytecode for MAPJUMP/MAPJUMP3/DISCJUMP
   c. Extract destination field ID from first PSHN_L in push sequence
4. Return array of {line_entity_index, destination_field_id} pairs
5. In RefreshCatalog, match SETLINE trigger entities to line entity indices
6. Look up display name via FIELD_DISPLAY_NAMES[destination_field_id]

## Matching SETLINE Entities to JSM Line Indices

SETLINE entities are captured at runtime by order of SETLINE opcode calls.
JSM line entities are indexed 0..countLines-1 in the entity group table.
SYM names are in the same order as JSM entities (lines first in data).
The SETLINE call order should correspond to JSM line entity index order.
Need to verify this empirically.
