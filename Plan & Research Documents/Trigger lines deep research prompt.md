# Deep Research Prompt: FF8 JSM Trigger Line Classification

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 PC edition) that provides TTS navigation for blind players. The mod hooks into the game engine via a DLL proxy (dinput8.dll) alongside FFNx v1.23.x.

I need to classify SETLINE trigger lines in FF8 field scripts by what they DO when the player crosses them. This is critical for the navigation catalog — the mod needs to know which trigger lines are mere camera angle changes (should be transparent to navigation) versus actual screen/room boundaries (should filter entities on the other side).

## What I Already Know

### JSM File Format
FF8 field scripts are stored in `.jsm` files within the field archive. The format:

- **Header** (8 bytes): byte0=doors, byte1=lines, byte2=backgrounds, byte3=others. Bytes 4-5=offset to entry point table. Bytes 6-7=offset to script data.
- **Entity group table**: 2 bytes per entity. Bits 0-6 = method count, bits 7-15 = starting method index.
- **Entry point table**: uint16 per method = dword index into script data.
- **Script data**: Array of uint32 instructions (native little-endian on PC).

### JSM Instruction Encoding
- **Bit 31 = 0**: Push literal (PSHN_L). Value = bits 0-30, sign-extended from bit 30.
- **Bit 31 = 1**: Opcode. ID = bits 1-14 (shift right 1, mask 0x3FFF). Bit 0 = sub-opcode flag.
- All opcode parameters come from the stack — NO inline parameters in the instruction word.

**IMPORTANT**: My scanner uses a different encoding (high-byte = primary opcode, low 24 bits = param) which works for primary opcodes but NOT for this analysis. The deling/Qhimm encoding above is the correct one for looking up opcode IDs in community references.

### Entity Categories
JSM entities are ordered: Doors, Lines, Backgrounds, Others. Line entities (indices countDoors to countDoors+countLines-1) are the trigger line entities I need to classify. Each Line entity has:
- An init script (method 0) that typically calls SETLINE to define the trigger geometry
- One or more interaction scripts (methods 1+) that fire when the player crosses the line

### What SETLINE Trigger Lines Do (My Observations)

From testing on several fields, trigger lines appear to serve these distinct purposes:

1. **Camera Pan / Background Switch**: When crossed, the pre-rendered background image changes to show a different camera angle of the same room/area. The player remains in the same navigable space. Example: bghall_1 (B-Garden Main Hall) has multiple camera-pan trigger lines along the length of the hallway.

2. **Screen/Room Boundary**: When crossed, the camera switches to show a different room or distinct area. Entities on the other side are generally not reachable without crossing back. Example: bgroom_1 (classroom) has a trigger separating the front seating area from the back.

3. **Event Trigger**: When crossed, a script event fires (NPC movement, dialog, cutscene trigger). Example: bgroom_1 has a line that triggers the Trepies to move aside.

4. **Save/Draw Point Zone**: A walk-on trigger that activates the save point or draw point interaction.

## What I Need From You

### Primary Question
**What JSM opcodes distinguish these trigger line types?** Specifically:

1. **Background/camera opcodes**: What are the opcode IDs (in the Qhimm/deling numbering scheme) for:
   - BGDRAW / BGDRAWOFF (draw/hide background layers)
   - BGON / BGOFF (enable/disable background layers)
   - BGANIMESPEED (background animation speed)
   - BGANIME / BGANIMESTOP
   - Any other background-layer-related opcodes
   
   These would indicate a camera-pan trigger line.

2. **Entity visibility/movement opcodes**: What opcodes indicate an event trigger?
   - SHOW / HIDE (make entity visible/invisible)
   - MOVE / MOVETO / ANIME (entity movement/animation)
   - MES / ASK (dialog display)
   - UNUSE / USE (entity activation)
   
3. **Are there specific opcode patterns that reliably distinguish camera pans from room transitions?** Both may use BGDRAW/BGOFF, but room transitions might additionally use entity SHOW/HIDE or movement commands, while pure camera pans only switch background layers.

### Secondary Questions

4. **Complete opcode table**: Is there a definitive mapping of opcode ID → name for FF8's JSM instruction set? The Qhimm wiki has a partial list but I need the extended opcodes (dispatched via the 0x1C mechanism, which in the bit-31 encoding would be primary opcode 0x0E, sub-opcodes via stack). Specifically the opcode IDs for:
   - All BG* (background) opcodes
   - SETLINE itself
   - Any camera-control opcodes
   
5. **Community resources**: Are there existing tools or databases that have already classified trigger line behavior per field? Tools like:
   - Deling (myst6re's FF8 field editor)
   - Hyne (save editor)
   - OpenVIII / JunctionXVIII
   - Qhimm wiki opcode reference
   
6. **FF8 scripting reference**: The most complete opcode reference I've found is the Qhimm wiki. Are there others? Particularly looking for:
   - Which opcodes are "primary" (encoded directly in bits 1-14) vs "extended" (dispatched via 0x0E/0x1C from stack)
   - The complete extended opcode dispatch table (the table indexed by the stack value when 0x0E/0x1C fires)

### Format of Answer
Please provide:
- A table mapping opcode names → numeric IDs (both the Qhimm/deling ID and, if possible, the extended dispatch table index)
- For each trigger line type, the specific opcode signatures to look for
- Any known tools/databases that could provide per-field trigger line classification
- Links to the most complete FF8 JSM opcode references

## Technical Details for Reference

### My Current Scanner
`ScanJSMScripts()` in `field_archive.cpp` already scans all JSM entities. For Line entities (category 1), it currently only looks for SETDRAWPOINT, MENUSAVE, etc. I need to extend it to classify Line entities by their interaction script content.

### Known Extended Opcodes (via 0x1C stack dispatch)
These are the ones I've confirmed by x86 disassembly:
- 0x137 = DRAWPOINT
- 0x155 = SETDRAWPOINT  
- 0x12E = MENUSAVE
- 0x12F = SAVEENABLE
- 0x130 = PHSENABLE
- 0x11E = MENUSHOP
- 0x13A = CARDGAME
- 0x14E = PARTICLEON
- 0x14F = PARTICLEOFF
- 0x10D = WORLDMAPJUMP
- 0x125 = ADDITEM
- 0x143 = DOORLINEON
- 0x142 = DOORLINEOFF
- 0x11B = MENUPHS

### Example Fields for Testing
- **bghall_1** (B-Garden Main Hall): 6 SETLINE trigger lines. Lines 0-4 are camera pans along the hallway. Line 5 is the save point activation zone near (-700,-8593).
- **bgroom_1** (Classroom): Has both camera transitions (front/back of room) and event triggers (Trepie movement).
- **bggate_2** (Lunatic Pandora area): No SETLINE triggers (L=0 in JSM header).
