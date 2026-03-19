# ChatGPT Deep Research Request: FF8 Field Entity Types and Runtime Data Structures

## Context

We're building an accessibility mod for Final Fantasy VIII (PC Steam 2013) that enables blind players to navigate field maps via TTS. A core feature is the **entity catalog** — when the player presses minus/plus keys, it cycles through all interactable entities in the current field screen and announces them with type, direction, and distance.

**The catalog is incomplete.** We currently detect NPCs and exits, but we're likely missing draw points, save points, item pickups, shops, ladders, card game spots, and other interactive elements. A blind player needs to know about ALL of these. This research will help us detect every interactable entity type present in a field and announce it properly.

## What We Currently Detect

Our mod has access to two runtime entity arrays provided by the FF8 engine:

### 1. "Others" Entity Array (`pFieldStateOthers`)
- **Struct**: `ff8_field_state_other` (stride 0x264 bytes per entity)
- **Max**: ~16 entities per field
- **Contains**: Entities with 3D models on the walkmesh (NPCs, party members, visible characters)
- **Key offsets we read**:
  - `0x218` (int16): model_id (-1 = invisible controller)
  - `0x1FA` (uint16): current_triangle_id (walkmesh position)
  - `0x1F8` (uint16): talk_radius
  - `0x1F6` (uint16): push_radius
  - `0x255` (byte): setpc (0 = this IS the player entity)
  - `0x24B` (byte): talkonoff flag
  - `0x249` (byte): pushonoff flag
  - `0x24C` (byte): throughonoff flag
  - `0x190-0x198` (3×int32): fixed-point world position (X/Y/Z × 4096)
- **We include**: Entities with modelId >= 0 AND placed on walkmesh (triId > 0)
- **We classify by**: talkonoff → NPC, pushonoff → Object, throughonoff → Exit, default → NPC
- **We exclude**: Entities with modelId < 0 (invisible controllers)

### 2. "Background" Entity Array (`pFieldStateBackgrounds`)
- **Struct**: `ff8_field_state_background` (stride 0x1B4 bytes per entity)
- **Max**: ~48 entities per field
- **Contains**: Script-only entities — no 3D model, no walkmesh position
- **We COMPLETELY EXCLUDE these from the catalog** because we thought they had no navigable position
- **Known SYM names we've seen in background entities**: save, savepoint, shopkun, cliant (terminal), britinboard (bulletin board), evl1 (elevator), monitor, moni (study panel), mess (desk), cardgamemaster, doorcont, door01

### 3. SETLINE Trigger Lines
- Captured at runtime via SETLINE opcode hook
- Used as screen transition exits and event triggers
- Have 2D coordinates (line endpoints)

### 4. INF Gateway Exits
- Parsed from the INF file at field load
- Have walkmesh vertex positions
- Destination field IDs are unreliable (PS1-era data)

## What We're Likely Missing

Based on gameplay, the following interactable elements exist in FF8 fields but may not appear in our catalog:

1. **Draw Points** — Glowing spots where the player can draw magic. Some are visible, some hidden. There are ~200 across the game. They're probably background entities.
2. **Save Points** — Glowing circles where the player can save. We see "save" in SYM names but don't catalog them.
3. **Item Pickups** — Items on the ground the player can pick up (magazines, items).
4. **Shop/Store Access Points** — Where the player can access a shop.
5. **Card Game Opponents** — NPCs that offer Triple Triad card games.
6. **Ladders/Climb Points** — Interactive movement transitions (ladders in D-District Prison, ropes in Centra Ruins).
7. **Pet/Interact Objects** — Interactive field objects (e.g., computers, panels, beds in dormitory).
8. **Garden Directory Panels** — The information kiosks in Balamb Garden.
9. **Doors** — The JSM "door" entity type. How do they appear at runtime?

## What I Need Researched

### 1. Complete FF8 Field Entity Type Taxonomy

Document every type of interactable entity that can exist in an FF8 field map. For each type:
- What is it? (draw point, save point, NPC, etc.)
- Which runtime state array does it live in? (others, backgrounds, or something else?)
- What JSM entity category is it? (door, line, background, other?)
- Does it have a 3D model / walkmesh position?
- How does the player interact with it? (walk up + press X? walk over it? automatic?)
- What JSM opcodes are associated with it?

### 2. Draw Point Detection

Draw points are a critical missing feature. Research:
- Are draw points "background" entities or "other" entities?
- What JSM opcode handles draw point interaction? (Likely DRAWPOINT or similar)
- How to distinguish draw points from other background entities at runtime
- How to determine a draw point's position (they appear at fixed screen locations)
- How to detect hidden vs visible draw points
- What magic ID is associated with each draw point
- Is there a runtime flag/memory address that indicates a draw point's state (available, depleted, hidden)?

### 3. Save Point Detection

- Are save points "background" or "other" entities?
- What JSM opcodes or flags identify a save point?
- Do save points have a 3D model? A walkmesh position?
- How to detect save points at runtime vs from static JSM analysis

### 4. Background Entity Positions

This is crucial — we excluded ALL background entities because we assumed they have no position. But:
- Do background entities have any position data in their runtime struct?
- The `ff8_field_state_background` struct (stride 0x1B4) shares a common prefix with `ff8_field_state_other`. Does the common struct include position fields?
- If background entities don't have walkmesh positions, how does the game know where to display their visual effects (draw point glow, save point circle)?
- Can we get background entity positions from JSM SET/SET3 opcodes at script init time?

### 5. Runtime Struct Layouts

Document the struct layouts for both entity types as completely as possible:

**`ff8_field_state_other`** (0x264 bytes):
- We know offsets 0x190-0x198 (position), 0x1F6-0x1FA (radii + triId), 0x218 (modelId), 0x249-0x255 (flags)
- What are the other fields? Especially anything related to entity type, visibility, or interaction state.

**`ff8_field_state_background`** (0x1B4 bytes):
- We know almost nothing about this struct beyond that it shares a common prefix with "other"
- Document any known offsets: position, flags, entity type indicators, script state

The common prefix struct (`ff8_field_state_common`) — what's in it? Offsets and field meanings.

### 6. JSM Opcodes for Interactive Objects

Document the JSM opcodes that define interactive behavior:
- **DRAWPOINT** — draw point interaction
- **SAVEPOINT** — save point trigger
- **ADDITEM** / **REMOVEITEM** — item pickup/removal
- **SHOP** — shop access
- **CARDGAME** — Triple Triad initiation
- **LADDER** — ladder interaction
- **MENU** (opcode 0x059) — opens a menu
- Any other opcodes that indicate an entity is interactable

For each, explain: which entity type typically uses it, what parameters it takes, and whether we can detect it from static JSM analysis or only at runtime.

### 7. Entity Visibility and Active State

How does the game control which entities are visible/interactable at any given time?
- SHOW/HIDE opcodes — what do they write to the entity struct?
- How to detect at runtime whether an entity is currently visible
- How does execution_flags (offset 0x160 in the common struct) relate to visibility?
- Are there entities that are always present in the struct but only conditionally visible?

### 8. Door Entities

The JSM has a "door" entity category. At runtime:
- Do door entities appear in pFieldStateOthers or pFieldStateBackgrounds?
- Do they have their own state array?
- How do doors differ from regular entities in the struct?
- Can doors be interactable (open/close) or are they purely scripted animations?

### 9. The SYM→Entity Mapping

The SYM file lists entity names. The JSM header tells us entity counts by type. At runtime:
- What is the definitive mapping from SYM name index to runtime state array entry?
- Our current assumption: SYM index i maps directly to entity state array index i (for "others")
- For background entities: SYM index (othersCount + i) maps to background state array index i
- For line entities: they appear before doors and backgrounds in the JSM entity group table
- Is this mapping correct? How do door entities factor in?

### 10. Practical Detection Strategy

Given all the above, propose a detection strategy:
- How should we identify draw points, save points, items, etc. from the runtime state arrays?
- Should we scan JSM scripts at field load to pre-identify entity types? (e.g., "SYM entity 5 has a DRAWPOINT opcode in its script → it's a draw point")
- Or can we detect entity types purely from runtime struct flags?
- What's the most reliable approach for a mod that runs alongside the game?

## Key References

- **FFNx source** (`src/ff8.h`): Contains `ff8_field_state_other`, `ff8_field_state_background`, `ff8_field_state_common` struct definitions. This is our most authoritative reference for struct layouts.
  - GitHub: `julianxhokaxhiu/FFNx`, file `src/ff8.h`
- **myst6re/deling**: Field editor with complete JSM opcode table. Check `JsmOpcode.h` for opcode IDs and `JsmExpression.h` for parameter counts.
- **Qhimm/FFRTT Wiki**: FF8 field script opcode documentation
- **ff8-speedruns/ff8-memory**: Memory layout documentation
- **OpenVIII**: C# FF8 research project

## Our Technical Environment

- Platform: FF8 Steam 2013 (App ID 39150), 32-bit x86, no ASLR
- Mod: Win32 DLL (`dinput8.dll`) injected alongside FFNx v1.23.x
- We can read any runtime memory address and hook any game function
- We already extract JSM/SYM/INF files from the field.fi/fl/fs archive at field load time
- We already hook JSM opcodes: SETLINE, LINEON, LINEOFF, TALKRADIUS, PUSHRADIUS
- We could hook additional opcodes if needed (we have the opcode dispatch table address)

## What Would Be Most Valuable

In priority order:
1. **FFNx struct layouts** for both entity types with all known field offsets
2. **How to detect draw points and save points** at runtime
3. **Whether background entities can have positions** (or how to get their positions)
4. **Complete list of interactive JSM opcodes** with entity type associations
5. **Entity visibility detection** (which struct offset controls show/hide)
6. **Practical strategy** for building a complete entity catalog
