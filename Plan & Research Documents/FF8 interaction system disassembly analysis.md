# FF8 Field Interaction System — Complete x86 Disassembly Analysis

## Summary

Disassembled FF8_EN.exe (Steam 2013) using Python capstone to reverse-engineer the complete field interaction system. This solves the PSHM_W entity position problem — **we don't need entity positions at all**. The engine uses INF trigger zone line coordinates, not entity positions, for interaction with objects like the Directory.

## Three Interaction Mechanisms (called per tick from ~0x47A280)

### 1. SETLINE Entity Interaction (0x4775C0)
- **What:** Checks SETLINE data in runtime entity structs (Others array)
- **Data source:** Entity struct at offset +0x188 (12 bytes of line coords + flags)
- **Called from:** 0x47A300
- **Distance check:** Point-to-line via 0x4774A0, compared against pushRadius²
- **Only works for:** Entities IN the Others array with SETLINE data

### 2. Gateway Scanner (0x477980)
- **What:** Checks INF gateway zones for field transitions
- **Data source:** `[0x1CDC744] + 0x64` = 12 gateway entries
- **Stride:** 0x20 (32 bytes per gateway)
- **Loop:** `add esi, 0x20` / `dec eax` / `jne`, counter starts at 0xC
- **Gateway struct (32 bytes):**
  - +0x00..0x0A: Line endpoints (int16 coords)
  - +0x0C,+0x0E: Destination coordinates
  - +0x10: Flags (0xFFFF = invalid)
  - +0x12: Destination field ID (0x7FFF = invalid)
  - +0x1C: Flag byte

### 3. Trigger Zone Scanner (0x47B610) — **THE KEY DISCOVERY**
- **What:** Checks INF trigger zones for interactive object activation
- **Data source:** `[0x1CDC744] + 0x1E4` = 12 trigger zone entries
- **Stride:** 0x10 (16 bytes per trigger zone)
- **Loop:** `add esi, 0x10` / `dec eax` / `jne`, counter starts at 0xC
- **Trigger zone struct (16 bytes):**
  - +0x00: int16 x1 (line start X)
  - +0x02: int16 y1 (line start Y)
  - +0x04: int16 z1 (line start Z)
  - +0x06: int16 x2 (line end X)
  - +0x08: int16 y2 (line end Y)
  - +0x0A: int16 z2 (line end Z)
  - +0x0C: uint8 entity_index (0xFF = empty/skip)
  - +0x0D: uint8 (padding)
  - +0x0E: uint8 interaction_type (0-5)
  - +0x0F: uint8 (padding)
- **Distance check:** Point-to-line via 0x4774A0, vs player pushRadius²
- **Facing check:** Player facing (entity+0x23F) checked via 0x477380
- **Script activation:** Calls 0x47B500(esi, type)

### Math verification
0x64 + 12 × 0x20 = 0x64 + 0x180 = 0x1E4 ✓

## Entity Proximity Check (0x47B460)
- **Only caller:** 0x0051EDB3 (TALKRADIUS opcode handler)
- **Formula:** `combined = edi.talkRadius(+0x1F8) + esi.pushRadius(+0x1F6)`
- **Distance:** `dist_sq = dx² + dy²` where dx/dy are fixed-point >>12
- **Z check:** `|dz| < 128` (elevation constraint)
- **Result:** Returns 1 if `combined² > dist_sq`
- **Only works with:** Others entities (offsets beyond BG struct size)

## Point-to-Line Distance (0x4774A0)
- **Arguments:** Line struct (6 int16 coords), point, output closest point
- **Returns:** Squared distance, or -1 if outside line bounding box
- **Used by:** All three interaction mechanisms

## Script Activator (0x47B500)
- **Jump table for types 0-5:**
  - Types 0, 2, 4 → 0x47B51F (set activation flag)
  - Types 1, 3, 5 → 0x47B58A (clear activation flag)
- **Entity array:** Uses `[0x1D9CF90]` (Backgrounds array, 8 refs — different from Others at `[0x1D9CF88]`, 172 refs)
- **Stride calculation:** entityIdx × 99 × 4 = entityIdx × 396
- **Activation flag:** Written at entity struct +0x18A (set) or +0x18B (clear)

## Data Source
- **INF data pointer:** `[0x1CDC744]`
- **Written at:** 0x00471A7E during field load
- **INF layout:**
  - +0x00: Camera/background data (100 bytes)
  - +0x64: 12 gateways × 32 bytes = 384 bytes
  - +0x1E4: 12 trigger zones × 16 bytes = 192 bytes

## BUG IN OUR CODE
Our `LoadINFTriggers` (field_archive.h) says offset **0x140** but the engine reads from **0x1E4**. The struct is also missing `entity_index` and `interaction_type` fields. Our trigger data has been parsed from the wrong INF location.

## Implication for the Directory (dic)
The Directory's interaction goes through INF trigger zones, NOT entity arrays. One of the 12 trigger zone entries in bghall_1's INF file has:
- A line segment defining the interaction zone in front of the Directory panel
- An entity_index pointing to dic's JSM entity slot
- An interaction_type controlling script activation

**For navigation, we need the trigger zone's line coordinates as the target position** — these are the exact walkmesh coordinates where the player must stand to interact with the Directory.

## Implementation Plan
1. Fix LoadINFTriggers to parse at offset 0x1E4 with correct 16-byte struct
2. Add entity_index and interaction_type to TriggerInfo struct
3. Map entity_index to SYM names via JSM entity table
4. Wire trigger zone line centers into entity catalog as navigation targets
5. This completely bypasses the PSHM_W entity position problem
