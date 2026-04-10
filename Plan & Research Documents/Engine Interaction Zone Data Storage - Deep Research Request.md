# Deep Research Request: FF8 Engine Interaction Zone Data Storage

## Goal

In Final Fantasy VIII (Steam 2013 PC edition, FF8_EN.exe), field rooms have interactive background objects — beds, desks, directories, signs, terminals, wardrobes. These are part of the pre-rendered background image. When the player walks near one and presses the interact button, a dialog or action triggers.

I need to find WHERE the engine stores the interaction zone positions/coordinates for these background objects, and specifically whether this data exists in a parseable form outside of JSM scripts.

## What I Already Know (Confirmed by Disassembly and Testing)

### The Director Entity Pattern
Many interactive background objects use a "Director" entity pattern in the JSM scripting system:
- An invisible entity (no SETMODEL in init) called the "Director" runs a per-frame script
- The Director's script reads player position via `PSHM_L(720)`, reads zone coordinates via `PSHM_W(addr)`, performs proximity comparisons, and dispatches interactions to target entities via `REQ`
- Example: On field `bgryo1_4` (Balamb Garden dormitory), Director entity `seed` (JSM entity 13) dispatches to `kigaeyarou` (wardrobe), `dic` (desk), and `el1` (bed)
- The Director's script uses PSHM_W addresses 0, 1, 2 as zone indices; addresses 145, 175 as coordinate reads; and PUSH 60 as a radius literal

### The Core Problem
The interaction zone coordinates are NOT literals in the JSM scripts — they're runtime PSHM_W reads from the field's temp memory block (varblock at `0x01CFE9B8`). But:
- The Director entity falls outside the engine's active entity window (max 8 simultaneous entity scripts) and NEVER executes its scripts
- No other entity writes to the Director's PSHM_W addresses (confirmed by POPM_W/B/L hook capturing ALL varblock writes for 10 seconds after field load — only 1 trivial write occurred)
- The entity activation window is fixed at field load time regardless of how the player enters the field (tested with both save-state load and normal field transition)
- The varblock at the Director's read addresses contains all zeros at runtime

### Exhausted Runtime Approaches (8+ attempts)
1. **SET3 opcode hook** — Director is beyond active window, SET3 never fires
2. **Direct entity struct read** — Entity struct positions are (0,0) for inactive entities
3. **Varblock formula read** — `*(int16_t*)(0x1CFE9B8 + addr)` returns zero for Director's addresses
4. **Descriptor table polling** — `0x01DCB340[entityIndex]` always NULL for inactive entities
5. **Proximity-based window swap** — Active window doesn't change when player approaches
6. **Parametric curve formula** — Requires descriptor data that's never allocated
7. **PSHM_W opcode hook** — No active entity reads Director's addresses
8. **POPM_W/B/L write capture hook** — No entity writes to Director's addresses during field init or per-frame execution

### Yet Interactions WORK in Normal Gameplay
Despite all the above, the player CAN interact with the bed/desk/wardrobe in normal gameplay. Walking up to the bed and pressing interact triggers "Rest up" dialog. This means the engine has ANOTHER mechanism for determining interaction zones that does NOT go through JSM script execution for the Director entity.

### Known Engine Interaction Functions (from x86 Disassembly of FF8_EN.exe)
Previous disassembly work identified three per-tick interaction mechanisms:
1. **SETLINE entity check at `0x4775C0`** — Checks proximity to SETLINE trigger lines defined by Line entities
2. **INF gateway scanner at `0x477980`** — Checks proximity to INF gateway line definitions
3. **INF trigger zone scanner at `0x47B610`** — Checks proximity to INF trigger zones at INF offset `0x1E4` (12 entries × 16 bytes each: line coords as 6×int16, entity_index as uint8, interaction_type as uint8)
4. **Script activator at `0x47B500`** — References `[0x1D9CF90]` (Backgrounds array, 8 refs). Called 3 times per the call xref table

### Known Data Structures
- **pFieldStateOthers**: Base pointer for "Others" entity array. Stride = `0x264` bytes per entity
- **pFieldStateBackgrounds**: Base pointer for "Backgrounds" entity array. Stride = `0x1B4` bytes per entity
- **Entity struct key offsets**: 0x190/0x194/0x198 = 32-bit fixed-point world coords (4096× scale); 0x1F8 = talk radius (uint16); 0x1F6 = push radius (uint16); 0x20/0x24 = int16 positions
- **INF data pointer**: `[0x1CDC744]`; gateways at +0x64 (stride 0x20); trigger zones at +0x1E4 (stride 0x10)
- **Varblock (field temp memory)**: Base at `0x01CFE9B8`. Bytes 0-1023 = persistent (saved to disk). 1024+ = temporary per-field
- **PSHM_W handler at `0x0051CB30`**: Confirmed by disassembly to use byte offsets directly — `mov ax, word ptr [ecx + 0x1CFE9B8]`
- **POPM_W handler at `0x0051CCD0`**: Writes uint16 to varblock — `mov word ptr [esi + 0x1CFE9B8], dx`
- **JSM opcode dispatch table**: `pExecuteOpcodeTable` at runtime, indexed by opcode number
- **JSM encoding**: Native little-endian uint32 per instruction. High byte = opcode index, low 24 bits = signed parameter

### Field File Format
Each FF8 field is stored as a compressed archive containing multiple sections:
- **.jsm** — JSM bytecode scripts for all entities
- **.sym** — Entity name strings
- **.inf** — Field info: camera data, gateways (at +0x64), trigger zones (at +0x1E4)
- **.msd** — Dialog message strings
- **.tdw** — Text display data
- **.ca** — Camera axes
- **.id** — Walkmesh geometry (triangles, vertices, adjacency)
- **.rat/.mrt/.map/.pmp** — Background rendering data
- Other sections for textures, lighting, etc.

### JSM Script Structure
The JSM header defines: `uint8 doors`, `uint8 lines`, `uint8 backgrounds`, `uint8 others`. Entity scripts are laid out in this order. Each entity has N methods (method[0] = init, method[1] = per-frame, method[2+] = callable via REQ). The SYM file provides one name per entity in the same order.

## Questions for Deep Research

### Primary Question
**Where does the FF8 engine store interaction zone position data for Director-dispatched background objects?** The data must exist somewhere because interactions work at runtime, but it's not in:
- JSM script literals (they're PSHM_W runtime reads)
- The varblock (zeros at the relevant addresses)  
- Entity struct positions (zeros for inactive entities)
- INF trigger zones (bgryo1_4 has zero trigger zones)
- SETLINE trigger lines (bgryo1_4's only SETLINE is a camera pan, not an interaction zone)

### Specific Technical Questions

1. **How does the engine determine WHERE the player can interact with background objects like beds and desks?** Is there a per-field data structure (possibly in the .inf, .jsm header, or another field file section) that maps interaction zones to screen/world coordinates? Or does the engine rely entirely on the JSM Director script's proximity checks (which would require the Director to be active)?

2. **Disassemble the function at `0x47B500`** (called 3 times, references Backgrounds array at `0x1D9CF90`). This appears to be a "script activator" — does it activate entity scripts based on proximity? Does it read interaction zone coordinates from a data structure? What triggers it and what data does it consume?

3. **Disassemble the function at `0x4775C0`** (SETLINE entity check, per-tick). How does it determine which entities have active SETLINEs? Does it check ALL entities or only active ones? Could there be SETLINE-like data stored outside the entity struct?

4. **Is there a separate interaction zone table in the field data files?** The INF file has trigger zones at offset 0x1E4, but bgryo1_4 has none. Could there be ANOTHER section of the INF or another field file that stores Director-dispatched interaction zones? Perhaps in unused/undocumented INF offsets?

5. **How does the engine handle the "active entity window" for interaction purposes?** The engine limits JSM script execution to ~8 simultaneous entities. But does the per-tick interaction check (`0x47B460` and related functions) scan ALL entities in the Others/Backgrounds arrays, or only the active ones? If it scans all, how does it know their positions when their structs show (0,0)?

6. **Is the field's temp memory (varblock) pre-populated from field file data?** When a field loads, does the engine write initial values to the varblock from the JSM file, INF file, or another source BEFORE any scripts execute? This would explain how the Director reads zone positions without any POPM_W writes occurring. Check the field loading code path — specifically what happens between field file decompression and `field_scripts_init` execution.

7. **Does the Backgrounds entity array (`pFieldStateBackgrounds`, stride `0x1B4`) store interaction zone positions separately from the Others array?** Interactive background objects like dic (desk), el1 (bed), kigaeyarou (wardrobe) appear in the JSM as "Others" category entities, but the engine might also create corresponding entries in the Backgrounds array with pre-set positions derived from field data.

8. **What does OpenFF8 (github.com/Extapathy/OpenFF8) or Deling (github.com/myst6re/deling) reveal about field file parsing?** These open-source tools parse FF8 field files. Do they document any interaction zone position data that isn't in the INF trigger zone section? Check their field file parsers for any per-entity position data beyond what JSM scripts contain.

9. **Could the varblock be initialized from savemap data?** The varblock bytes 0-1023 are persistent (saved to disk). PSHM_W address 145 (used by the Director for a coordinate read) falls in this range. Could the interaction zone coordinates be stored in the savemap and restored on field load? If so, who writes them initially on first visit?

10. **Trace the complete code path from "player presses interact button" to "Director's REQ fires dialog on target entity."** What functions are called? At what point does the engine check proximity? Where does it read the interaction zone coordinates from? This trace would definitively reveal the data source.

## Available Resources
- Full x86 disassembly of FF8_EN.exe (Steam 2013, ~2.76M instructions) organized by section (.text_0x00401000 through .text_0x00B01000)
- Call cross-reference table (which functions call which, with call counts)
- Function list with boundaries
- Import/export tables
- String table with all embedded strings and their addresses
- The modding communities: Qhimm forums, FF8 wiki, OpenFF8, Deling, Hyne save editor
- FFNx source code (github.com/julianxhokaxhiu/FFNx) — modern rendering/modding layer that hooks many engine functions

## What I Need from This Research
1. The exact memory location or data structure where interaction zone positions are stored
2. Whether this data comes from field files (parseable statically) or is only computed at runtime
3. If from field files: which file, which offset, which format
4. If runtime-only: which engine function computes it and what inputs does it use
5. Any community documentation (Qhimm, wiki, OpenFF8, Deling) about this specific mechanism
