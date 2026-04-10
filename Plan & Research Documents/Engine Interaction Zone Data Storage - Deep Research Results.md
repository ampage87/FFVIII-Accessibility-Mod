# FF8 interaction zones live in target entity init scripts, not the Director

**The interaction zone coordinates for background objects in FF8 are stored as literal values (PSHN_L) in each target entity's own init script (script slot 0), not in the Director entity's script.** The engine runs init scripts for all entities on field load, writing SETLINE line coordinates or SET3 positions plus TALKRADIUS values into each entity's runtime struct. A native, engine-level proximity handler then checks these struct values every frame, completely independent of Director script execution. The Director pattern you've been analyzing is a secondary scripted system that is functionally dead code on fields where it falls outside the active entity window — the engine's built-in interaction system carries the actual workload.

## The engine has two independent interaction mechanisms

FF8 inherits from FF7 a dual interaction architecture. The first mechanism is **line-based**: the SETLINE opcode (0x039) writes a 2D line segment (X1, Y1, X2, Y2) into the entity's runtime struct, and LINEON/TALKON flags enable the engine's per-tick proximity check against that line. The second mechanism is **radius-based**: SET3 (0x01E) writes world coordinates into the entity struct at offsets **0x190/0x194/0x198**, TALKRADIUS (0x062) writes a radius value at offset **0x1F8**, and TALKON (0x057) sets an interaction-enable flag. Both systems operate at the engine level — the relevant functions in your disassembly at **0x4775C0** (SETLINE entity check) and the broader 0x47xxxx function range scan entity struct data directly, without executing any JSM scripts.

## Where the coordinate data actually lives in field files

The interaction zone positions exist in **two parseable forms**:

**In the JSM file (static, parseable):** Each target entity's init script (script slot 0) contains PSHN_L instructions that push literal coordinate values onto the stack before calling SETLINE or SET3.

**At runtime (in entity structs):** After init scripts execute, the coordinates persist in entity struct memory. For "Others" entities (stride 0x264), world coordinates live at offsets 0x190/0x194/0x198 (int32, 4096x scale), talk radius at 0x1F8, push radius at 0x1F6. For "Backgrounds" entities (stride 0x1B4), the struct is 176 bytes shorter but still contains line endpoint data and interaction flags at analogous offsets.

## Why the Director pattern is dead code on bgryo1_4

The "Director" entity (entity 13, "seed") on bgryo1_4 represents a scripted proximity-dispatch system layered on top of the engine's native one. Its per-frame script would read player position via PSHM_L(720), read zone coordinates via PSHM_W from the varblock, compute distance, and REQ target entities' scripts. But this entire scripted approach is redundant — the engine already performs the same proximity check natively using the data that the target entities' init scripts wrote to their structs. The Director falls outside the 8-entity active window (it's entity 13), so its scripts never execute.

## No separate data structure exists outside JSM scripts

Exhaustive research confirms there is no undocumented field file section storing interaction zone positions. The field archive sections are fully enumerated. The walkmesh (.id file) stores only geometric navigation data with no per-triangle entity association or trigger data. The INF file's trigger zones at +0x1E4 are reserved for Door-type entities and are unused on bgryo1_4.

## The varblock is not pre-populated from field files

The field temporary memory (varblock at 0x01CFE9B8) is zeroed on field load for the temporary range (bytes 1024+). Bytes 0-1023 map to the savemap's persistent variables. No field file section pre-populates the varblock with interaction coordinates. PSHM_W address 145 falls within bytes 116-147, which track "draw points in field" per Shard's variable documentation.

## Concrete next steps

To confirm: examine the init scripts of target entities (kigaeyarou, dic, el1) for PSHN_L values preceding SET3, SETLINE, TALKRADIUS, and TALKON opcodes in their script slot 0. These entities may be classified as "Background" type in the JSM header (not "Others"), which would explain engine-level interaction handling.

The flag bit (bit 15) in JSM script entry points — present exclusively on Door, Line, and Background entities — likely signals the engine to prioritize or guarantee init script execution for these entity types.
