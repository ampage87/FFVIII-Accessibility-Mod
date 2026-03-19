# FF8 JSM opcode reference for trigger line classification

**Every documented FF8 field script opcode (0x000–0x183) is directly encoded in the JSM instruction's opcode field — there is no secondary dispatch mechanism in the bytecode itself.** The "extended opcodes" above 0x0FF that you confirmed via x86 disassembly (DRAWPOINT at 0x137, DOORLINEON at 0x143, etc.) sit in the same 14-bit opcode ID space as all other instructions. Trigger lines can be reliably classified by scanning the opcodes in a line entity's scripts for specific marker opcodes: MAPJUMP/MAPJUMP3 for room transitions, BGDRAW/BGOFF plus scroll opcodes for camera pans, and DRAWPOINT/MENUSAVE/CARDGAME for interaction points. The JSM header's entity-type counts provide the first discriminator — line entities are always grouped separately from doors, backgrounds, and NPCs.

---

## Complete opcode table for trigger-relevant opcodes

The authoritative source is the Qhimm wiki at `wiki.ffrtt.ru/index.php/FF8/Field/Script/Opcodes`, maintained by Aali, myst6re, and Shard. The full table spans **388 opcodes** (0x000–0x183). Below are the opcodes most relevant to trigger line classification, grouped by function.

### Background and camera opcodes

These are the primary indicators of **camera-pan trigger lines**. A line entity whose scripts contain only these opcodes (plus control flow) is almost certainly a camera angle change.

| Hex ID | Dec | Name | Description |
|--------|-----|------|-------------|
| 0x095 | 149 | **BGANIME** | Start background layer animation |
| 0x096 | 150 | RBGANIME | Reverse background animation |
| 0x097 | 151 | RBGANIMELOOP | Loop reverse background animation |
| 0x098 | 152 | BGANIMESYNC | Wait for background animation to finish |
| **0x099** | **153** | **BGDRAW** | **Draw/show a background layer** |
| **0x09A** | **154** | **BGOFF** | **Turn off/hide a background layer** |
| 0x09B | 155 | **BGANIMESPEED** | Set background animation speed |
| 0x0CE | 206 | BGANIMEFLAG | Background animation flag (unused) |
| 0x0D0 | 208 | BGSHADE | Background shading |
| 0x0D1 | 209 | BGSHADESTOP | Stop background shading |
| 0x0D2 | 210 | RBGSHADELOOP | Reverse background shade loop |
| 0x117 | 279 | BGSHADEOFF | Background shading off |
| 0x11C | 284 | BGCLEAR | Clear background |

### Camera scroll opcodes

These scroll the viewport and typically accompany BGDRAW/BGOFF in camera-pan triggers.

| Hex ID | Dec | Name | Description |
|--------|-----|------|-------------|
| 0x071 | 113 | **DSCROLL** | Direct scroll (instant) |
| 0x072 | 114 | **LSCROLL** | Linear scroll (smooth) |
| 0x073 | 115 | **CSCROLL** | Curved scroll |
| 0x074 | 116 | DSCROLLA | Direct scroll variant A |
| 0x075 | 117 | LSCROLLA | Linear scroll variant A |
| 0x076 | 118 | CSCROLLA | Curved scroll variant A |
| 0x077 | 119 | SCROLLSYNC | Wait for scroll to complete |
| 0x07F | 127 | DSCROLLP | Direct scroll variant P |
| 0x080 | 128 | LSCROLLP | Linear scroll variant P |
| 0x081 | 129 | CSCROLLP | Curved scroll variant P |
| 0x0D3 | 211 | DSCROLL2 | Layer-2 direct scroll |
| 0x0D4 | 212 | LSCROLL2 | Layer-2 linear scroll |
| 0x0D5 | 213 | CSCROLL2 | Layer-2 curved scroll |
| 0x0D6 | 214 | DSCROLLA2 | Layer-2 direct scroll A |
| 0x0D8 | 216 | CSCROLLA2 | Layer-2 curved scroll A |
| 0x0DC | 220 | SCROLLSYNC2 | Wait for layer-2 scroll |
| 0x10A | 266 | **SETCAMERA** | Set camera position directly |
| 0x120 | 288 | DSCROLL3 | Layer-3 direct scroll (unused) |
| 0x121 | 289 | LSCROLL3 | Layer-3 linear scroll |
| 0x122 | 290 | CSCROLL3 | Layer-3 curved scroll |

### Room transition and map jump opcodes

Presence of any of these in a line entity's scripts reliably marks it as a **room/screen boundary**.

| Hex ID | Dec | Name | Description |
|--------|-----|------|-------------|
| **0x029** | **41** | **MAPJUMP** | Jump to another field (fieldID, X, Y, triangleID) |
| **0x02A** | **42** | **MAPJUMP3** | Jump to field with 3D position |
| 0x038 | 56 | DISCJUMP | Jump to disc-specific field |
| 0x05C | 92 | MAPJUMPO | Map jump with offset |
| 0x05D | 93 | MAPJUMPON | Enable map jumping |
| 0x05E | 94 | MAPJUMPOFF | Disable map jumping |
| 0x0E4 | 228 | PREMAPJUMP | Pre-map-jump setup |
| **0x10D** | **269** | **WORLDMAPJUMP** | Jump to world map |

### Entity visibility, movement, and interaction opcodes

These indicate **event triggers** when found in line entity scripts — they manipulate NPCs, party members, or the player entity.

| Hex ID | Dec | Name | Description |
|--------|-----|------|-------------|
| 0x060 | 96 | **SHOW** | Make entity visible |
| 0x061 | 97 | **HIDE** | Make entity invisible |
| 0x01A | 26 | **UNUSE** | Deactivate entity |
| 0x0E5 | 229 | **USE** | Activate entity |
| 0x03E | 62 | MOVE | Move entity to position |
| 0x03F | 63 | MOVEA | Move entity (variant) |
| 0x041 | 65 | CMOVE | Curved move |
| 0x042 | 66 | FMOVE | Fast move |
| 0x01D | 29 | SET | Set entity 2D position |
| 0x01E | 30 | SET3 | Set entity 3D position + walkmesh triangle |
| 0x052 | 82 | DIR | Set entity facing direction |
| 0x02D | 45 | ANIME | Play character animation |
| 0x02E | 46 | ANIMEKEEP | Play animation, hold last frame |
| 0x047 | 71 | **MES** | Display dialog message |
| 0x04A | 74 | **ASK** | Display dialog with choices |
| 0x065 | 101 | AMES | Display message (auto-position) |
| 0x064 | 100 | AMESW | Display message (auto-position, wait) |
| 0x06F | 111 | AASK | Display choices (auto-position) |

### Trigger line setup and control opcodes

| Hex ID | Dec | Name | Description |
|--------|-----|------|-------------|
| **0x039** | **57** | **SETLINE** | Define trigger line (X1, Y1, X2, Y2 from stack) |
| 0x03A | 58 | **LINEON** | Enable trigger line |
| 0x03B | 59 | **LINEOFF** | Disable trigger line |
| 0x057 | 87 | TALKON | Enable talk interaction |
| 0x058 | 88 | TALKOFF | Disable talk interaction |
| 0x059 | 89 | PUSHON | Enable push/touch interaction |
| 0x05A | 90 | PUSHOFF | Disable push/touch interaction |
| 0x062 | 98 | TALKRADIUS | Set talk trigger radius |
| 0x063 | 99 | PUSHRADIUS | Set push trigger radius |
| **0x142** | **322** | **DOORLINEOFF** | Disable door trigger line |
| **0x143** | **323** | **DOORLINEON** | Enable door trigger line |

### Special interaction point opcodes (your confirmed extended opcodes)

| Hex ID | Dec | Name | Classification signal |
|--------|-----|------|----------------------|
| **0x137** | **311** | **DRAWPOINT** | Draw point interaction |
| **0x155** | **341** | **SETDRAWPOINT** | Set draw point state |
| **0x12E** | **302** | **MENUSAVE** | Save point menu |
| **0x12F** | **303** | **SAVEENABLE** | Enable save functionality |
| **0x130** | **304** | **PHSENABLE** | Enable PHS (party swap) |
| **0x11E** | **286** | **MENUSHOP** | Open shop menu |
| **0x13A** | **314** | **CARDGAME** | Initiate card game |
| **0x14E** | **332** | **PARTICLEON** | Enable particle effects |
| **0x14F** | **333** | **PARTICLEOFF** | Disable particle effects |
| **0x125** | **293** | **ADDITEM** | Add item to inventory |
| **0x11B** | **283** | **MENUPHS** | Open PHS menu |

---

## How to classify trigger lines programmatically

FF8 has **two independent trigger systems**, and understanding both is essential for your accessibility mod.

### The INF gateway system handles hard-coded room transitions

The **INF file** (included in each field archive) defines up to **12 gateways** — geometric trigger volumes with a destination field ID. These are the primary room transition mechanism. Each gateway stores two vertices (X, Y, Z as sint16) defining a line segment, plus a destination field ID (uint16). The INF file is a fixed-size binary (typically **672 bytes** on PC). **Parse the INF file first** — any gateway with a nonzero destination field ID represents a room exit. This gives you room-boundary triggers without any script analysis.

### Script-based trigger lines require opcode pattern matching

Line entities in the JSM file use SETLINE to define a walkmesh trigger line. When the player crosses this line, the engine executes the entity's **across/touch** script method. The entity's type is determined from the JSM header: **line entities appear after door entities** in the entity table, and their count is stored in the header's second byte (byte index 1).

The JSM header structure is: `byte0 = countDoor, byte1 = countLine, byte2 = countBackground, byte3 = countOther`. Entity ordering in the group table follows: **other entities first, then doors, then lines, then backgrounds**. Each entity entry is 2 bytes: bits 0–6 = script count, bits 7–15 = starting script index.

### Opcode signature patterns for each trigger type

For each line entity, scan its script methods (especially method indices 2+ which handle the "across" and "touch" callbacks) for these opcode patterns:

**Camera pan** — Background layer switch with optional viewport scroll:
- Contains: BGDRAW (0x099) and/or BGOFF (0x09A)
- Often contains: DSCROLL/LSCROLL/CSCROLL family (0x071–0x076, 0x07F–0x081) or SETCAMERA (0x10A)
- Does NOT contain: MAPJUMP/MAPJUMP3/WORLDMAPJUMP, SHOW/HIDE, MES/ASK, or any menu opcodes
- May contain: BGANIMESPEED (0x09B), BGANIME (0x095), SCROLLSYNC (0x077), FADEIN/FADEOUT (0x0A5/0x0A6)
- **This is the most common line entity type** — FF8 fields use prerendered backgrounds with multiple camera angles, and SETLINE triggers switch between them

**Room/screen boundary** — Field transition via script:
- Contains: **MAPJUMP (0x029) or MAPJUMP3 (0x02A) or DISCJUMP (0x038) or MAPJUMPO (0x05C) or WORLDMAPJUMP (0x10D)**
- May also contain BGDRAW/BGOFF (for visual transitions), FADEOUT (0x0A6), UCOFF (0x04E, disable user control)
- The MAPJUMP destination field ID is pushed onto the stack as a literal before the opcode — you can extract it from the preceding PSHN_L instruction

**Event trigger** — NPC manipulation, cutscene, or interaction:
- Contains: SHOW (0x060) / HIDE (0x061) / USE (0x0E5) / UNUSE (0x01A) for entity state changes
- Or contains: MOVE (0x03E) / SET3 (0x01E) for repositioning entities
- Or contains: MES (0x047) / ASK (0x04A) for dialog
- Or contains: REQ/REQSW/REQEW (0x014–0x016) to invoke scripts on other entities
- Or contains: BATTLE (0x069) for encounter triggers
- May contain: ANIME (0x02D) family for character animations, MUSICCHANGE (0x0B4) for atmosphere shifts

**Save/draw point** — Specific interaction hotspots:
- Contains: **DRAWPOINT (0x137)** or **SETDRAWPOINT (0x155)** → draw point
- Contains: **MENUSAVE (0x12E)** or **SAVEENABLE (0x12F)** → save point
- Contains: **CARDGAME (0x13A)** → card game trigger
- Contains: **MENUSHOP (0x11E)** → shop trigger

**Door line** — Door-specific triggers:
- Uses DOORLINEON (0x143) / DOORLINEOFF (0x142) instead of LINEON/LINEOFF
- May contain BGDRAW/BGOFF for door animation layer toggling
- Identified at the entity level: door entities precede line entities in the JSM entity table

### Recommended classification algorithm

```
for each line entity in JSM:
    opcodes = collect_all_opcodes(entity.scripts[2:])  // skip init/idle
    
    if any(op in {MAPJUMP, MAPJUMP3, DISCJUMP, MAPJUMPO, WORLDMAPJUMP}):
        classify = ROOM_TRANSITION
    elif any(op in {DRAWPOINT, SETDRAWPOINT}):
        classify = DRAW_POINT
    elif any(op in {MENUSAVE, SAVEENABLE}):
        classify = SAVE_POINT
    elif any(op in {CARDGAME, MENUSHOP}):
        classify = INTERACTION_POINT
    elif any(op in {BATTLE}):
        classify = BATTLE_TRIGGER
    elif any(op in {SHOW, HIDE, USE, UNUSE, MES, ASK, AMES, AASK}):
        classify = EVENT_TRIGGER
    elif any(op in {BGDRAW, BGOFF, DSCROLL..CSCROLLP, SETCAMERA}):
        classify = CAMERA_PAN
    else:
        classify = UNKNOWN
```

Priority ordering matters: a script containing both BGDRAW and MAPJUMP is a room transition that happens to also switch background layers (common pattern for visual transitions during map changes). **Check for MAPJUMP-family opcodes first.**

---

## JSM instruction encoding and the opcode ID space

Your description from x86 disassembly matches the documented format. Each JSM instruction is **4 bytes**. **Bit 31=0** encodes a PSHN_L (push literal), with the value in the remaining bits. **Bit 31=1** encodes an opcode, with the opcode ID in bits 1–14. All opcode parameters come from the stack, though some opcodes also use an inline argument from the instruction's remaining bits.

The full opcode space spans **0x000 through 0x183** (decimal 0–387). All 388 opcodes are encoded directly in the instruction — there is no two-level dispatch in the bytecode format. Your finding of a "0x1C mechanism" in the x86 engine is likely an **implementation detail of the game's opcode interpreter**, not a bytecode encoding feature. The engine may use a split dispatch table: opcodes 0x000–0x0FF dispatched via one jump table, and opcodes 0x100–0x183 via a secondary handler. But from the JSM bytecode perspective, DRAWPOINT (0x137) is encoded identically to BGDRAW (0x099) — both use bits 1–14 for their opcode ID.

The CAL opcode (0x001) is the only opcode that performs internal sub-dispatch: its inline argument selects arithmetic/comparison operations (ADD=0x00, SUB=0x01, MUL=0x02, DIV=0x03, MOD=0x04, NEG=0x05, EQ=0x06, NE=0x07, GT=0x08, GE=0x09, LS=0x0A, LE=0x0B, NOT=0x0C, AND=0x0D, OR=0x0E, XOR=0x0F).

---

## Tools and community resources for field script analysis

**Deling** (github.com/myst6re/deling) by myst6re remains the definitive FF8 field editor. Its `JsmScripts.cpp` and related headers contain the canonical opcode enum, script decompiler, and control-flow reconstruction logic. Version 0.11.0 added a tile editor and background mass export. The source code's `searchJumps()` method demonstrates how to trace control flow through JMP/JPF/GJMP targets. Deling can visually inspect any field's entity scripts, trigger lines, and background layers.

**OpenVIII-monogame** (github.com/MaKiPL/OpenVIII-monogame) is an open-source FF8 engine reimplementation in C#/MonoGame. Archived January 2026, it contains JSM parsing code under `Core/Field/JSM/` that implements the same opcode definitions. **Sebanisu's OpenVIII_CPP_WIP** (github.com/Sebanisu/OpenVIII_CPP_WIP) provides a C++ archive reading library for FF8's ZZZ/FIFLFS archives, plus a **Field-Map-Editor** (github.com/Sebanisu/Field-Map-Editor) for tile editing.

**FFNx** (github.com/julianxhokaxhiu/FFNx) uses the same dinput8 proxy DLL approach you're implementing, supporting the Steam 2013 and GOG editions. Its hooking infrastructure and field rendering pipeline are directly relevant reference material for your accessibility mod.

No pre-existing database classifying all trigger lines across all FF8 fields by type was found in any community resource. **You would need to build this by parsing all JSM files from field.fs and running the opcode pattern-matching algorithm described above.** The SYM files (PC-only) provide human-readable entity names like "EventlineX" that can serve as sanity checks — entity names often hint at their purpose.

---

## Conclusion

The critical insight for your accessibility mod is that **two completely independent systems** define trigger behavior: the INF gateway table (parse it directly for room transitions with destination field IDs) and the JSM script system (requires opcode pattern analysis). For script-based triggers, the opcode ID space is flat — every opcode from NOP (0x000) through UNKNOWN18 (0x183) occupies the same 14-bit field in the instruction encoding. Your x86-confirmed "extended opcodes" like DOORLINEON (0x143) and DRAWPOINT (0x137) are standard opcodes, not specially dispatched.

The most reliable classification heuristic is a priority-ordered scan: MAPJUMP-family presence definitively marks room transitions; DRAWPOINT/MENUSAVE marks interaction points; the combination of BGDRAW/BGOFF with scroll opcodes and the *absence* of entity-manipulation opcodes marks camera pans. For a blind-accessible navigation system, camera pans should be transparent (the player remains in the same logical space), while room transitions and event triggers should be announced. The SYM file's entity names, combined with the INF gateway coordinates, provide cross-validation for your automated classifier.