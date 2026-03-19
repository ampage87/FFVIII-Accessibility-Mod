# ChatGPT Deep Research Request: Parsing FF8 JSM Scripts for JMPFL Exit Destinations

## Context

We're building an accessibility mod for Final Fantasy VIII (PC Steam 2013) that enables blind players to navigate the game via TTS. A key feature is the **field navigation entity catalog**, which announces exits to the player. Currently exits are announced as "Exit 1 of 3" — we want to say **"Exit to B-Garden - Hall 2, 1 of 3"** so blind players know where each exit leads.

We already have:
- A complete **982-entry display name table** (`FIELD_DISPLAY_NAMES[]`) mapping field ID → human-readable name (e.g., field 165 → "B-Garden - Hall 1")
- A parallel **982-entry internal name table** (`FIELD_INTERNAL_NAMES[]`) mapping field ID → internal name (e.g., field 165 → "bghall_1")
- An archive extraction system that can read any per-field file from the `field.fi/fl/fs` archives (SYM, INF, JSM, etc.)
- Runtime hooks on SETLINE/LINEON/LINEOFF opcodes that capture trigger line entity addresses and coordinates

**The problem**: INF gateway destination fields are unreliable. FF8 reuses field backgrounds across multiple story scenarios (e.g., `bggate_1` is used during normal gameplay, the Garden battle sequence, and disc 2+ events). The INF stores gateway destinations for all scenarios, and the `destFieldId` values point to completely wrong fields for the current context (e.g., B-Garden's front gate INF says its exit leads to `gmtika3` = Galbadia Missile Base).

**The solution**: Parse each **line entity's JSM script** to find its `JMPFL` (Jump to Field) opcode, which contains the **actual runtime field transition target**. Line entities are the walk-on trigger zones that fire field transitions when the player crosses them.

## What We Know About JSM Format

From our existing `field_archive.cpp` code and testing:

### JSM Header (8 bytes)
```
Byte 0: reserved (always 0)
Byte 1: number of door entities
Byte 2: number of line entities  
Byte 3: number of background entities
Bytes 4-5: uint16 LE offset to script data section (scriptEntryOffset)
Bytes 6-7: uint16 LE total number of entities (we derive "others" count from this)
```

### Entity Entry Point Table
Starting at byte 8, each entity has a 2-byte entry (uint16 LE). Entity ordering is:
1. Door entities (count = header byte 1)
2. Line entities (count = header byte 2)  
3. Background entities (count = header byte 3)
4. Other entities (derived count)

Each entry point is an offset into the script data section.

### SYM Correspondence
The SYM file contains 32-byte fixed-width entity name records in the same order as the JSM entities: doors first, then lines, then backgrounds, then others. Entity `i` in JSM = SYM record `i`.

### What We DON'T Know (and need researched)

1. **JSM bytecode format**: What is the instruction encoding? Are opcodes 1 byte or 2 bytes? How are arguments encoded? Is it a stack machine (like FF7's field script) or register-based?

2. **JMPFL opcode**: What is the opcode number for JMPFL (jump to field)? What are its arguments? We expect at minimum:
   - Destination field ID (uint16)
   - Possibly destination coordinates (spawn point in the target field)
   
3. **Script section layout**: Each entity has multiple script "methods" (init, main loop, talk handler, etc.). How do we find the start and end of each entity's script code? Is there a method table per entity?

4. **How to extract JMPFL from a line entity**: Given a JSM file binary and a line entity index (0-based within the lines group), what's the step-by-step process to find all JMPFL opcodes in that entity's scripts and extract the destination field ID?

5. **Related opcodes**: Are there other field-transition opcodes besides JMPFL? (e.g., JMPFL3, JMPB, etc.) Do any of them take a field ID parameter?

6. **Conditional transitions**: Some line entities may have conditional JMPFLs (different destinations based on story progress). How are conditions/branches structured in JSM bytecode? We'd want to extract ALL JMPFL targets from an entity, not just the first one.

## Key References to Investigate

- **Qhimm Wiki**: FF8 field script documentation (JSM/opcode format)
- **myst6re/deling**: Qt-based FF8 field editor — has complete JSM parser and opcode table in C++. This is likely the most authoritative source for bytecode format.
- **OpenVIII**: C# FF8 research project — may have JSM parsing code
- **ff8-speedruns/ff8-memory**: Memory documentation
- **Cactilio**: FF8 engine research
- **julianxhokaxhiu/FFNx**: Our FFNx overlay (has opcode dispatch table addresses but doesn't parse JSM files directly)

## What I Need

### 1. Complete JSM bytecode documentation

Document the JSM instruction format:
- How opcodes are encoded (size, byte order)
- How arguments/parameters are encoded (inline bytes? stack pushes?)
- The instruction set structure (is it a stack machine?)

### 2. JMPFL opcode specification

For the JMPFL opcode (and any related field-transition opcodes):
- Opcode number/identifier
- Parameter format (what bytes follow the opcode, and what do they mean)
- Which parameter is the destination field ID
- Any other parameters (spawn coordinates, facing direction, etc.)

### 3. Entity script structure in JSM

How to locate a specific entity's script code:
- How the entry point table maps to script offsets
- How many "methods" each entity type has (especially line entities)
- How to determine where one entity's code ends and another's begins

### 4. Working pseudocode for extraction

Given:
- A JSM file loaded as a byte array
- A line entity index `L` (0-based within the line entities group)

Provide pseudocode or C code that:
1. Locates entity `L`'s script methods in the JSM data
2. Scans each method's bytecode for JMPFL opcodes
3. Extracts the destination field ID from each JMPFL found
4. Returns a list of `{method_index, destination_field_id}` pairs

### 5. Practical example

Using a real FF8 field as an example (e.g., `bghall_1` which we know has exits to other B-Garden rooms), walk through the binary JSM data showing:
- The header bytes and what they mean
- The entry point table entries for the line entities
- The actual bytecode of a line entity's script
- Where the JMPFL opcode appears and what field ID it encodes

## How This Will Be Used

We'll add a new function `LoadJSMExitDestinations()` to our `field_archive.cpp` that:
1. Extracts the JSM file for the current field
2. Parses the header to find line entities
3. For each line entity, scans its script for JMPFL opcodes
4. Returns an array of `{line_entity_index, destination_field_id}` pairs
5. At runtime, when building the entity catalog, matches SETLINE trigger entities to their JSM line entity index and looks up the display name

This gives us reliable, per-scenario exit destinations because the JSM scripts contain conditional logic that only executes the JMPFL appropriate for the current story state — we'll extract ALL possible destinations and the game's runtime execution will have already selected the correct one via the SETLINE hook.

## Important Notes

- We're working with the **PC Steam 2013 version** (App ID 39150). The JSM format may differ slightly from the PS1 original.
- Our mod runs as a DLL injected alongside FFNx. We can read files from the archive at field load time but want to keep parsing fast (< 1ms per field).
- We already extract JSM files successfully via `ExtractInnerFile(fieldName, ".jsm", jsmData)` — we just need to know how to parse the bytecode.
- The **myst6re/deling** source code is probably the single best reference since it has a working JSM parser in C++ that handles all opcodes. If you can find the JMPFL handling in deling's source, that would be ideal.
