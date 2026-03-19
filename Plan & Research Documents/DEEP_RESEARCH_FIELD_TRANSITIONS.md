# Deep Research: FF8 Field-to-Field Transition Mechanism for Non-MAPJUMP Exits

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 PC edition, original release, NOT the Remaster). The mod provides TTS navigation for blind players, including announcing exits and their destinations. I need to understand how the engine handles field transitions so I can detect exits statically from the game data files.

## The Problem

Balamb Garden's main hallway (`bghall_1`, field ID 165) has multiple exits — to classrooms, the library, the elevator corridor, the front gate, etc. The player walks to the edge of the screen and transitions to the connected field (e.g. `bghall_4`, `bg2f_1`).

**I have conclusively proven that these transitions do NOT use either of the two documented exit mechanisms:**

### Evidence: No Script-Level MAPJUMP

I scanned the entire JSM bytecode for `bghall_1` (34,604 bytes, 8,483 script instructions across 43 entities and 289 methods). There are exactly **3 MAPJUMP3 opcodes** in the entire file, ALL belonging to just 2 entities:
- `saveline0` (elevator trigger, JSM entity 36) — transitions to a different area
- `water` (front gate, JSM entity 37) — transitions to a different area

The hallway exit entities (`l1` through `l6`, `l5`, JSM entities 26, 31-35) contain **zero MAPJUMP/MAPJUMP3/DISCJUMP/MAPJUMPO/WORLDMAPJUMP opcodes**. There is no indirect MAPJUMP via REQ-following either — no entity they REQ to contains MAPJUMP. There is no variable-dispatch chain because there are no MAPJUMP-containing methods that read from matching memory addresses.

The `shu` entity (JSM entity 12, the "Director" script with 8 methods) also contains **zero MAPJUMP opcodes**.

What l1-l6 DO contain: POPM_W writes to field-temporary variables in the 1024-1041 address range. These appear to be scene-state flags, not transition triggers.

### Evidence: INF Gateways Are Garbage

The INF file's 12 gateway entries for `bghall_1` contain completely wrong data:
- Destination fields include `cwwood7`, `bchtr1a`, `cdfield8`, `ebroad7`, `ddruins2` — these are unrelated locations from completely different parts of the game world
- Gateway line coordinates (e.g. 184,-184 to 184,184) don't match the actual hall geometry (Y range approximately -1000 to -9500)
- This is consistent with known research that PC INF gateway data is vestigial PS1 data that was not properly converted

### Yet Transitions DO Happen

When the player walks to the edges of the hallway in `bghall_1`, the game transitions to `bghall_4` (continuing down the hall). This is a fact — I've captured SETLINE calls for the new field in my runtime logs immediately after the player walks off-screen.

## What I Need To Know

### Primary Question
**What is the engine-level mechanism that triggers field-to-field transitions in FF8 PC when neither JSM MAPJUMP opcodes nor INF gateways are responsible?**

### Specific Sub-Questions

1. **Walkmesh edge triggers**: Does the FF8 engine have a built-in mechanism where walking off the edge of the walkmesh (hitting a triangle with `neighbor = 0xFFFF` / no neighbor) triggers a field transition? If so, how does the engine know WHICH field to transition to and WHERE to spawn the player?

2. **SETLINE as transition trigger**: The 6 SETLINE trigger lines in `bghall_1` are all classified as camera pans (they contain BGDRAW/BGOFF/scroll opcodes). But could certain SETLINE trigger line crossings also cause field transitions at the engine level, independent of the JSM scripts attached to them? Is there a dual-purpose mechanism where crossing a line both runs the JSM script AND triggers an engine-level transition?

3. **Field variable-driven transitions**: The l1-l6 entities write to memory addresses 1024-1041 via POPM_W. Could the engine's main loop monitor specific memory addresses and trigger field transitions when certain values are written? Is there a known "transition request" variable address in the FF8 variable map?

4. **INF gateway mechanism on PC — is it truly dead?** The Qhimm wiki and community consensus say INF gateways work on PS1 but are broken/vestigial on PC. Is this 100% confirmed, or could there be a secondary INF interpretation where the engine reads different offsets or uses the data differently on PC? Could the gateway data be byte-swapped, offset-shifted, or encoded differently than the documented PS1 format?

5. **The `0x13` opcode**: The opcode histogram for `bghall_1` shows `13=12` (12 occurrences of primary opcode 0x13). This opcode is not in our standard opcode table. What is opcode 0x13 in FF8's JSM bytecode? Could it be a transition-related instruction we're not scanning for? (Our scanner recognizes: MAPJUMP=0x29, MAPJUMP3=0x2A, DISCJUMP=0x38, MAPJUMPO=0x5C, WORLDMAPJUMP=0x10D)

6. **The `0x19` opcode**: Also appears once (`19=1`). What is opcode 0x19?

7. **Engine-level field loading**: In the FF8 PC engine (FF8_EN.exe), what function or code path actually performs a field-to-field transition? In FFNx source code, the relevant hooks are around `field_scripts_init` and the field loading pipeline. What triggers the engine to call this? Is there a global "pending field transition" variable or flag that gets checked each frame?

8. **Cross-field walkmesh connections**: Some FF8 fields appear to be stitched together as a continuous walkable space (like the Garden hallway segments bghall_1 → bghall_4 → bg2f_1). Is there a mechanism in the engine or data files that defines these connections? A "field connection table" or "area map" separate from INF and JSM?

## Technical Details About Our Scanner

Our JSM bytecode scanner uses the **high-byte encoding** (confirmed by x86 disassembly of the FF8 engine):
- Each instruction is a native LE uint32
- High byte (bits 24-31) = primary opcode (0x01-0xFF), or 0x00 for push literal
- Low 24 bits = signed parameter
- Extended opcodes (>0xFF) use prefix opcode 0x1C: preceding PSHN_L pushes the dispatch table index, then 0x1C pops it and dispatches

We track:
- PSHM_W (opcode 0x07): push word from memory — records the memory address parameter
- POPM_W (opcode 0x08): pop word to memory — records the memory address parameter
- MAPJUMP family: 0x29, 0x2A, 0x38, 0x5C, and 0x10D (via 0x1C dispatch)
- REQ/REQSW/REQEW (0x14, 0x15, 0x16): cross-entity method calls
- SET3 (0x1E): entity position
- SETLINE (0x39): trigger line geometry
- All camera/scroll opcodes for trigger line classification

## Reference Sources

Please investigate using these sources:
- **Qhimm Wiki**: FF8/Field/Script/Opcodes, FF8/FileFormat JSM, FF8/Variables, FF8/FileFormat INF
- **Qhimm Forums**: [FF8] Engine reverse engineering thread, any threads about field transitions
- **myst6re's Deling source code** (GitHub: myst6re/deling) — FF8 field script editor, contains JSM parser and opcode definitions
- **FFNx source code** (GitHub: julianxhokaxhiu/FFNx) — FF8 PC engine hook layer, especially `src/ff8_data.cpp`, `src/ff8/field/` directory
- **OpenVIII** (GitHub) — C# FF8 data parser
- **Ifrit** (GitHub) — Another FF8 field script tool
- **FF8 speedrun research** (GitHub: ff8-speedruns/ff8-memory) — Memory addresses and variable maps
- **Final Fantasy Inside wiki** (wiki.ffrtt.ru) — Mirror/alternate of Qhimm wiki content
- **ff7-flat-wiki** (ff7-mods.github.io) — Another documentation mirror

## What Would Be Most Helpful

1. The actual x86 code or pseudocode for the field transition check in the FF8 engine main loop
2. Whether INF gateways are processed by the engine at all on PC, and if so, how the data is interpreted
3. Any undocumented opcode (0x13, 0x19, or others) that could trigger transitions
4. The memory address or engine variable that signals "transition to field X at position Y"
5. How connected-hallway fields (bghall_1 ↔ bghall_4) know about each other in the data files
6. Any relevant source code from Deling, FFNx, OpenVIII, or Ifrit that handles field transitions

## Additional Context

- The FF8 engine processes field scripts via an opcode dispatch table at a known address. Each opcode handler is a function pointer. The dispatch uses the high byte of the instruction word (SHR 24) as the table index.
- The game is 32-bit x86 Windows, the EXE is `FF8_EN.exe` (Steam 2013 AppID 39150)
- FFNx v1.23.x is installed alongside as a graphics/audio replacement layer
- The variable map starts at offset 0xD10 in uncompressed save data; runtime base is approximately 0x18FE9B8
- Variables 0-1023 are global (persist across fields), 1024+ are field-local temporaries
