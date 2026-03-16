# Field Navigation Improvements Plan (v05.43+)

Created: 2026-03-07
Status: Ready for implementation

## Three Problems to Solve

### Problem 1: Player Position Always (0,0) — SOLVED (v05.45)

**Root cause:** The player entity's simple int32 position at 0x20/0x28 is often
zero. The engine maintains a SEPARATE set of fixed-point coordinates.

**Solution (confirmed by v05.43/v05.44 diagnostics):**
Entity struct offsets 0x190, 0x194, 0x198 contain 32-bit fixed-point coordinates:
  - 0x190: int32 = X * 4096
  - 0x194: int32 = Y * 4096 (vertical axis)
  - 0x198: int32 = Z * 4096

Dividing by 4096 gives world coordinates in the same space as NPC int32 at
0x20/0x28. Proof: in bg2f_2 (hallway), NPC Z=8341, player [198]=34164736,
and 34164736/4096 = 8341 exactly.

GetEntityPos now tries fixed-point 0x190/0x198 first (always live for player),
then falls back to simple int32 0x20/0x28 (always populated for NPCs).

### Problem 2: Entity Catalog Missing Exits

**Evidence:** Exits in FF8 fields are not entity interaction flags. The corridor
(bg2f_1) has doorways but entities 4-7 have model=-1, talk=0, push=0, through=0.

**Solution: Parse INF Gateways from field data**

The INF section of each field's DAT file contains gateway definitions:
- Up to 12 gateways (field-to-field transitions)
- Up to 16 triggers (doors/interactions)
- Each gateway has: two vertices defining a walkmesh line, destination field ID

**INF Format (from Qhimm wiki, 672-byte PC format):**
```
Offset 0x00: Camera ranges (2x Range structs, 16 bytes)
Offset 0x10: Screen ranges (2x Range structs, 16 bytes)  
Offset 0x20: Gateways (12x Gateway structs)
  Each gateway (24 bytes):
    int16 vertex1_x, vertex1_y, vertex1_z  (line start)
    int16 vertex2_x, vertex2_y, vertex2_z  (line end)
    uint16 destination_field_id
    int16 unknown[4]
    uint8  unknown_data[4]
Offset 0x140: Triggers (16x Trigger structs)
  Each trigger (24 bytes):
    int16 vertex1_x, vertex1_y, vertex1_z
    int16 vertex2_x, vertex2_y, vertex2_z
    uint8  unknown_data[12]
Offset 0x2A0: End
```

Gateway center = midpoint of (vertex1, vertex2) → gives us position for the
exit in world coordinates. Destination field ID → we can look up the field
name from the maplist.

**Access path:** The field DAT is loaded by the engine. FFNx has
`read_field_data` in its externals. On the Original PC version, the DAT is
section 1 of the per-field archive entry in field.fi/fl/fs. We already know
how to extract field archive data from our Remaster work.

**Simpler alternative:** Hook or read the INF data after the engine loads it
into memory. The engine must parse the INF to know where gateways are.
If we can find the loaded gateway array in memory (via FFNx's address
resolution patterns), we can read it directly without file parsing.

### Problem 3: Entity Names Are Generic

**Evidence:** Catalog shows "NPC 1 of 5", "Entity 2 of 5" — no names.

**Solution: Parse SYM files for entity names**

The SYM file is a PC-only text file in the field archive. Each line is 32
characters (space-padded). First lines are entity names (excluding doors),
then script function names.

Example SYM content:
```
Director                       
Squall                         
Eventline1                     
Director                       
Director::default              
Director::talk                 
...
```

Entity names map to JSM entity indices. The JSM header tells us entity counts
by category (doors, lines, backgrounds, others), and SYM names align with
the "others" category entities.

**Access path:** SYM is a separate file in the field archive (same .fi/.fl/.fs
system). Alternatively, FFNx may load it — check `field_filename` which gives
the current field name, then construct the SYM path.

## Implementation Order

1. ~~**Player position fix** (Problem 1)~~ — DONE (v05.45)
2. **Entity names from SYM** (Problem 3) — improves usability significantly  
3. **Exit gateways from INF** (Problem 2) — adds missing navigation targets

## Session Strategy

Each improvement is self-contained and testable independently. Break into
micro-sessions:

- Session A: Diagnostic build to find player position offset (dump more
  entity struct fields during gameplay, not just at init)
- Session B: Implement SYM parsing and wire entity names into announcements
- Session C: Implement INF gateway parsing and add exits to catalog
- Session D: Polish and test all three together

## Key Files to Read at Session Start

- DEVNOTES.md (project state)
- This document (plan)
- `src/field_navigation.cpp` (current implementation)
- `src/ff8_addresses.h` (resolved game addresses)
- FFNx `src/ff8.h` (entity struct layout, externals)
- FFNx `src/ff8_data.cpp` (address resolution patterns)
