# FF8 field entity internals for accessibility mod development

**Draw points, save points, and all interactable entities in FF8 fields are detectable at runtime through a combination of static JSM script scanning and runtime struct monitoring.** The game organizes field entities into two runtime arrays — `pFieldStateOthers` (stride **0x264**, ≤16 entities with 3D models) and `pFieldStateBackgrounds` (stride **0x1B4**, ≤48 script-only entities) — but the JSM scripting layer partitions entities into four categories: Door, Line, Background, and Other. Draw points and save points are **Background** entities and live in `pFieldStateBackgrounds`. They acquire walkmesh positions through SET3 opcodes at initialization, meaning their coordinates can be captured either from runtime struct memory or by scanning JSM bytecode at field load. The most reliable detection strategy is a **hybrid approach**: parse JSM scripts at field load to classify entities by signature opcodes (DRAWPOINT, MENUSAVE, MENUSHOP, etc.), extract positions from SET3 parameters, then monitor runtime state for visibility and depletion changes.

---

## JSM header format and entity group ordering

The JSM file header defines four entity categories with byte-sized counts. The header structure per myst6re's documentation is:

```c
uint8  countEntitiesLine;        // byte 0
uint8  countEntitiesDoor;        // byte 1
uint8  countEntitiesBackground;  // byte 2
uint8  countEntitiesOther;       // byte 3
uint16 offsetScriptEntryPoint;   // byte 4-5
uint16 offsetScriptData;         // byte 6-7
```

Despite this header byte order, **entities in the JSM entity table appear in a different fixed order**: Door → Line → Background → Other. This was confirmed from the deling `JsmScripts.cpp` constructor signature, which receives `countDoors, countLines, countBackgrounds, countOthers` as parameters. Entity type is determined purely by index ranges:

| Index range | Category | Entry point flag (bit 15) | In SYM? |
|---|---|---|---|
| `[0, countDoors)` | **Door** | Set | ❌ Excluded |
| `[countDoors, countDoors+countLines)` | **Line** | Set | ✅ Included |
| `[…, …+countBackgrounds)` | **Background** | Set | ✅ Included |
| `[…, total)` | **Other** | Not set | ✅ Included |

The entry point flag (bit 15 of each script entry point word) is set for Door, Line, and Background entities but **not** for Other entities — providing an additional classification signal when parsing JSM data.

**SYM→Entity mapping**: The SYM file excludes door entities entirely. SYM index 0 maps to JSM entity index `countDoors` (the first Line entity). For any SYM index `i`, the corresponding JSM entity index is `i + countDoors`.

---

## Runtime state arrays and the JSM-to-struct mapping

FFNx exposes two runtime entity arrays through `ff8_externals`:

```c
ff8_externals.field_state_backgrounds    // ff8_field_state_background** 
ff8_externals.field_state_background_count  // uint8_t*
```

These are resolved from the engine's `field_scripts_init` function at specific instruction offsets. The **runtime mapping between JSM categories and struct arrays** works as follows:

- **`pFieldStateOthers`** (stride **0x264** = 612 bytes, max **16**): Contains only JSM "Other" entities — NPCs, player characters, and any entity with a 3D model assigned via SETMODEL.
- **`pFieldStateBackgrounds`** (stride **0x1B4** = 436 bytes, max **48**): Contains JSM "Door", "Line", and "Background" entities combined. These are script-only entities without character models.

The mapping from JSM entity table index to runtime array index is therefore: JSM "Other" entities (the last group in the entity table) map to `pFieldStateOthers[0..countOthers-1]`; JSM Door+Line+Background entities (the first three groups) map to `pFieldStateBackgrounds[0..countDoors+countLines+countBackgrounds-1]`, preserving their Door→Line→Background ordering within the array.

### Known struct offsets for `ff8_field_state_other` (0x264 bytes)

Both arrays share a common prefix (`ff8_field_state_common`), with `ff8_field_state_other` extending it with 3D model and walkmesh fields. The known offsets:

| Offset | Type | Field | Description |
|---|---|---|---|
| 0x000–0x15F | — | Common prefix | Shared with ff8_field_state_common |
| **0x160** | uint32_t | `execution_flags` | Script execution state flags |
| **0x190** | int32_t | `pos_x` | X position on walkmesh |
| **0x194** | int32_t | `pos_y` | Y position on walkmesh |
| **0x198** | int32_t | `pos_z` | Z position on walkmesh |
| **0x1F6** | uint16_t | `push_radius` | Push collision radius |
| **0x1F8** | uint16_t | `talk_radius` | Talk interaction radius |
| **0x1FA** | uint16_t | `current_triangle_id` | Current walkmesh triangle |
| **0x218** | uint8_t | `model_id` | Assigned 3D model index |
| **0x249** | uint8_t | `pushonoff` | Push interaction enabled |
| **0x24B** | uint8_t | `talkonoff` | Talk interaction enabled |
| **0x24C** | uint8_t | `throughonoff` | Walk-through collision state |
| **0x255** | uint8_t | `setpc` | Playable character flag |

The full struct definitions for `ff8_field_state_common` and `ff8_field_state_background` could not be retrieved from the FFNx source (GitHub raw file access was blocked during research). These structs are defined in FFNx's `src/ff8.h` or `src/ff8_data.h` and can be obtained by cloning the repo: `git clone --recursive https://github.com/julianxhokaxhiu/FFNx.git`.

**Critical question for background entities**: The common prefix shared between `ff8_field_state_other` and `ff8_field_state_background` is **0x160 bytes** (up to `execution_flags`). Whether background entities have position fields at offsets analogous to 0x190–0x198 depends on whether those offsets fall within the shared prefix or the "other"-only extension. Since the common prefix is 0x160 bytes and positions are at 0x190+, **positions are in the "other" extension and likely absent from the background struct**. However, background entities DO receive positions via SET3 opcodes — these values must be written somewhere in the struct, just potentially at different offsets within the 0x1B4-byte background layout.

---

## Draw point detection: opcodes, state, and positions

Draw points are **Background entities** in the JSM category system and reside in the `pFieldStateBackgrounds` runtime array. They are distinguishable from other background entities exclusively by their script content.

### Signature opcodes

| Opcode | Hex | Parameters | Purpose |
|---|---|---|---|
| **SETDRAWPOINT** | 0x155 | Stack: draw point ID (global index) | Configures entity as a draw point during init |
| **DRAWPOINT** | 0x137 | Inline: draw point ID | Opens the draw point interaction menu |
| **PARTICLEON** | 0x14E | Stack: parameters (undocumented) | Enables the glowing particle effect |
| **PARTICLEOFF** | 0x14F | No params | Disables particle effect |

A typical draw point entity script flow: (1) init script calls `SET3` to place the entity at walkmesh coordinates, (2) calls `SETDRAWPOINT` with the draw point's global index, (3) calls `PARTICLEON` for the visual glow, (4) checks variable state to decide `SHOW`/`HIDE`, (5) talk script calls `DRAWPOINT` when the player interacts.

### Draw point state storage

Draw point depletion is tracked in a **dedicated variable block**, separate from the entity struct:

- **Field draw points**: Variables 116–147 (32 bytes), 2 bits per draw point = **128 field draw point slots**
- **World map draw points**: Variables 148–179 (32 bytes) = **128 world map slots**
- **Runtime base address** (en-US): `0x18FE9B8 + 0x74 = 0x18FEA2C` through `0x18FEA4B`
- **Save file offset**: Within the field variables block starting at save offset `0x0D70`

Each draw point's 2-bit state encodes available/depleted status. Refill depends on the step counter (**10,240 steps** for 50% restock chance). The `SETDRAWPOINT` parameter is a **draw point index** (not a magic spell ID) — it indexes into a global draw point properties table (likely in `init.bin`) that maps each index to a magic spell ID, rich flag, and refillable flag.

### Hidden vs. visible draw points

Hidden draw points require the **Move-Find** GF ability (learned from Siren, 40 AP) to display their visual effect. The field script checks game state variables to decide whether to call `SHOW` or `HIDE`. Hidden draw points can still be interacted with at the correct position even without Move-Find — the ability just reveals the visual indicator. World map draw points are always invisible, even with Move-Find equipped.

### Extracting draw point positions

Draw point positions are set via **SET3** in the entity's initialization script. The opcode format is: three stack pushes (X, Y, Z coordinates) followed by `SET3` with an inline walkmesh triangle ID parameter. To capture positions: scan the init script (script 0) for `SET3`, then read the three preceding `PSHN_L` literal values from the bytecode. Example: `PSHN_L 402; PSHN_L -381; PSHN_L 20; SET3 17` places the draw point at position (402, -381, 20) on walkmesh triangle 17.

---

## Save point detection mirrors draw point patterns

Save points are also **Background entities** sharing the same detection methodology as draw points.

### Signature opcodes

| Opcode | Hex | Purpose |
|---|---|---|
| **SAVEENABLE** | 0x12F | Enables save functionality (called in init) |
| **MENUSAVE** | 0x12E | Opens the save menu (called in talk/interaction script) |
| **PHSENABLE** | 0x130 | Enables PHS party swap at save point |
| **MENUPHS** | 0x11B | Opens PHS menu |

There is no opcode literally named "SAVEPOINT" — save point functionality is implemented through `SAVEENABLE` + `MENUSAVE`. Save points also use `PARTICLEON` for their characteristic rotating ring visual effect. The engine maintains a dedicated variable (variable 88) for save points, described as "Built-in engine variable, used exclusively on save points, related to Siren's Move-Find ability." Some save points are hidden and require Move-Find to reveal. The "Can Save" flag lives at **`FF8_EN.exe+18E490B`** — save points set this to 3.

Save point positions are captured identically to draw points: find `SET3` in the init script.

---

## Complete entity type taxonomy with detection signatures

| Entity type | JSM category | Runtime array | Has 3D model? | Position source | Interaction | Detection opcode(s) |
|---|---|---|---|---|---|---|
| **Draw Point** | Background | Backgrounds | No (particle) | SET3 in init | Walk near + press X | `SETDRAWPOINT` (0x155), `DRAWPOINT` (0x137) |
| **Save Point** | Background | Backgrounds | No (particle) | SET3 in init | Walk near + press X | `MENUSAVE` (0x12E), `SAVEENABLE` (0x12F) |
| **Shop NPC** | Other | Others | Yes (MCH) | SET3 in init | Talk (press X) | `MENUSHOP` (0x11E) |
| **Card Game NPC** | Other | Others | Yes (MCH) | SET3 in init | Talk (press X) | `CARDGAME` (0x13A) |
| **Item Pickup** | Background/Other | Either | Varies | SET3 in init | Walk near/interact | `ADDITEM` (0x125) |
| **Ladder** | Line (trigger) | Backgrounds | No | SETLINE | Walk over trigger | `LADDERUP` (0x025), `LADDERDOWN` (0x026) |
| **Door** | Door | Backgrounds | No (BG anim) | Trigger line | Walk through | `DOORLINEON` (0x143), `BGANIME` (0x095) |
| **Map Exit** | Line | Backgrounds | No | SETLINE | Walk over trigger | `MAPJUMP` (0x029), `MAPJUMP3` (0x02A) |
| **NPC (talkable)** | Other | Others | Yes (MCH) | SET3 in init | Talk (press X) | `SETMODEL` (0x02B) + `TALKON` (0x057) |
| **Garden Directory** | Background/Other | Either | Varies | SET3 or SETLINE | Press X | `MES` (0x047) with directory text |
| **Interactive Object** | Background/Other | Either | Varies | SET3 in init | Press X | Context-dependent (`MES`, `ASK`, `BATTLE`) |

For Line entities specifically, position is defined by `SETLINE` (0x039), which takes stack parameters X1, Y1, X2, Y2 defining the trigger line's two endpoints in walkmesh coordinates. The midpoint of this line serves as the approximate entity position.

---

## JSM opcode reference for interactive entity detection

### Positioning and visibility opcodes

| Hex | Name | Parameters | Writes to struct | Notes |
|---|---|---|---|---|
| 0x01D | **SET** | Stack: X, Y; Inline: triangle_id | Position fields | 2D placement, Z from walkmesh |
| 0x01E | **SET3** | Stack: X, Y, Z; Inline: triangle_id | Position fields | Full 3D placement |
| 0x039 | **SETLINE** | Stack: X1, Y1, X2, Y2 | Line geometry | Line trigger definition |
| 0x060 | **SHOW** | None | Visibility flag | Makes entity visible |
| 0x061 | **HIDE** | None | Visibility flag | Makes entity invisible |
| 0x01A | **UNUSE** | None | Execution state | Fully deactivates entity |
| 0x0E5 | **USE** | None | Execution state | Reactivates entity |
| 0x057 | **TALKON** | None | `talkonoff` (0x24B) | Enables talk interaction |
| 0x058 | **TALKOFF** | None | `talkonoff` (0x24B) | Disables talk interaction |
| 0x059 | **PUSHON** | None | `pushonoff` (0x249) | Enables push collision |
| 0x05A | **PUSHOFF** | None | `pushonoff` (0x249) | Disables push collision |
| 0x062 | **TALKRADIUS** | Stack: radius | `talk_radius` (0x1F8) | Sets interaction distance |
| 0x063 | **PUSHRADIUS** | Stack: radius | `push_radius` (0x1F6) | Sets collision distance |
| 0x067 | **THROUGHON** | None | `throughonoff` (0x24C) | Disables solid collision |
| 0x068 | **THROUGHOFF** | None | `throughonoff` (0x24C) | Enables solid collision |

**SHOW/HIDE** toggle a visibility flag in the entity struct (exact offset within the common prefix is undocumented but within the first 0x160 bytes). An entity can be hidden but still active — HIDE does not stop script execution. **UNUSE/USE** are more aggressive: UNUSE stops all script processing for the entity, while USE reactivates it. The `execution_flags` field at offset 0x160 reflects the overall execution state.

### Menu and interaction opcodes

| Hex | Name | Stack params | Inline params | Entity type |
|---|---|---|---|---|
| 0x137 | **DRAWPOINT** | — | Draw point ID | Background (draw point) |
| 0x155 | **SETDRAWPOINT** | Draw point ID | — | Background (draw point) |
| 0x12E | **MENUSAVE** | — | — | Background (save point) |
| 0x12F | **SAVEENABLE** | — | — | Background (save point) |
| 0x11E | **MENUSHOP** | Shop ID | — | Other (shop NPC) |
| 0x13A | **CARDGAME** | Opponent config | — | Other (card game NPC) |
| 0x125 | **ADDITEM** | Item ID, quantity | — | Any (item pickup) |
| 0x025 | **LADDERUP** | X, Y, Z, tri_id, dir, speed, anim | — | Other (ladder user) |
| 0x026 | **LADDERDOWN** | X, Y, Z, tri_id, dir, speed, anim | — | Other (ladder user) |
| 0x029 | **MAPJUMP** | Field ID, X, Y, tri_id | — | Line (map transition) |
| 0x02A | **MAPJUMP3** | Field ID, X, Y, Z, tri_id | — | Line (map transition) |
| 0x069 | **BATTLE** | Encounter ID | — | Any (scripted battle) |

Note: There is no opcode named "REMOVEITEM" — item removal is handled through variable manipulation. There is no opcode named "MENU" at 0x059; **opcode 0x059 is PUSHON**. There is no opcode literally named "SAVEPOINT" — use MENUSAVE/SAVEENABLE instead.

---

## Door entities occupy a unique position in the architecture

Doors are the **first group** in the JSM entity table and the only category excluded from SYM debug names. They live in `pFieldStateBackgrounds` at runtime (indices 0 through countDoors-1 in the backgrounds array). Doors combine two mechanisms: a **trigger line** (controlled by `DOORLINEON`/`DOORLINEOFF`, opcodes 0x143/0x142) and **background tile animation** (using `BGANIME`, `BGDRAW`, `BGOFF` opcodes for open/close visuals). They have no 3D character model.

Doors are typically map transitions — walking through the trigger line plays the door animation and calls `MAPJUMP`/`MAPJUMP3`. Their position can be inferred from the trigger line geometry (if `SETLINE` is used) or from the associated INF gateway data in the field file.

Some doors are interactable (locked doors requiring key items), while others are purely automatic triggers. For accessibility, doors that lead to map transitions are functionally equivalent to Line entities with `MAPJUMP` — both serve as exits. The door category exists primarily to associate visual animation (background tile changes) with the transition.

---

## Recommended runtime detection architecture

### Phase 1: JSM static analysis at field load

Hook the field loading function (via `ff8_externals.read_field_data` or `field_scripts_init`). When a new field loads:

1. **Parse the JSM header** — read the 4 count bytes (Line, Door, Background, Other at bytes 0-3) and the two offset words.
2. **Build an entity type map** — iterate through all entity entries and classify by index range into Door/Line/Background/Other.
3. **Scan each entity's script bytecode** for signature opcodes. For every entity, decode opcodes in its init script (script 0) and interaction scripts (scripts 1-2). Look for:
   - `SETDRAWPOINT` (0x155) or `DRAWPOINT` (0x137) → mark as **Draw Point**; extract the draw point ID parameter
   - `MENUSAVE` (0x12E) or `SAVEENABLE` (0x12F) → mark as **Save Point**
   - `MENUSHOP` (0x11E) → mark as **Shop**; extract shop ID
   - `CARDGAME` (0x13A) → mark as **Card Game**
   - `ADDITEM` (0x125) → mark as **Item Pickup**; extract item ID
   - `LADDERUP`/`LADDERDOWN` (0x025/0x026) → mark as **Ladder**
   - `MAPJUMP`/`MAPJUMP3` (0x029/0x02A) → mark as **Map Exit**; extract destination field ID
4. **Extract positions** — for each classified entity, find `SET3` (0x01E) or `SET` (0x01D) in the init script and read preceding `PSHN_L` (0x007) stack pushes for X, Y, Z coordinates and the inline walkmesh triangle ID. For Line entities, extract `SETLINE` (0x039) parameters (X1, Y1, X2, Y2) and compute the midpoint.
5. **Cross-reference draw point IDs** to magic spell names from `init.bin` or kernel data. Cross-reference shop IDs to shop inventories. Cross-reference `MAPJUMP` destination field IDs to field name strings.
6. **Read SYM names** — map SYM index `i` to JSM entity index `i + countDoors` to get human-readable entity names (e.g., "save", "savepoint", "draw", "shop").

### Phase 2: Runtime state monitoring

Each game frame (or at a suitable interval):

1. **Read player position** from the `pFieldStateOthers` array (the entity with `setpc` flag set at offset 0x255) — read pos_x/pos_y/pos_z at offsets 0x190/0x194/0x198.
2. **Compute distances** to all classified interactable entities using their extracted positions.
3. **Check entity visibility** — read the visibility state from the entity struct (likely a flag in the common prefix, or check `execution_flags` at 0x160 for UNUSE state). Skip entities that are hidden/inactive.
4. **Check draw point availability** — read the 2-bit state from variable block address `0x18FEA2C + (drawPointID / 4)`, extracting the appropriate 2-bit field. Report "available" vs "depleted" to the player.
5. **Generate TTS announcements** based on proximity thresholds, differentiated by entity type: "Draw Point — Thundaga — available, 50 units ahead", "Save Point nearby", "Exit to Balamb Garden", "Shop — Weapons", etc.

### Why hybrid detection beats runtime-only detection

Entity types **cannot be determined purely from runtime struct flags** — there is no "entity type" field in the struct. The JSM category (Door/Line/Background/Other) tells you the broad structural class, but distinguishing a draw point from a save point from a generic background animation requires inspecting the entity's script content. Static JSM analysis at load time provides this classification definitively and cheaply. Runtime struct monitoring then provides dynamic state (visibility, position updates, depletion). The combination delivers complete, reliable detection for all entity types.

---

## Key memory addresses for FF8 PC Steam 2013 (en-US)

| Description | Address | Size |
|---|---|---|
| Variable block base | `FF8_EN.exe + 0x18FE9B8` | — |
| Draw point states (field) | `0x18FEA2C` – `0x18FEA4B` | 32 bytes |
| Draw point states (world map) | `0x18FEA4C` – `0x18FEA6B` | 32 bytes |
| Player field coord X | `FF8_EN.exe + 0x1677238` | 4 bytes |
| Player field coord Y | `FF8_EN.exe + 0x167723C` | 4 bytes |
| Player field coord Z | `FF8_EN.exe + 0x1677240` | 4 bytes |
| Can Save flag | `FF8_EN.exe + 0x18E490B` | 1 byte (3 = save point active) |
| Step counter offset | `FF8_EN.exe + 0x18DC748` | 1 byte |

All addresses are for the English US Steam 2013 release. French release addresses differ by a small fixed offset (typically `-0x328` per documented FR offsets).

## Conclusion

The FF8 field engine's entity system is fundamentally script-driven — entity types are an emergent property of their JSM bytecode, not a stored classification flag. This makes **JSM static analysis at field load the cornerstone** of any entity detection system. The four-category JSM hierarchy (Door→Line→Background→Other) maps cleanly to two runtime arrays (backgrounds for the first three categories, others for the last), and signature opcodes like `SETDRAWPOINT`, `MENUSAVE`, and `MENUSHOP` provide unambiguous type identification. Background entities including draw points and save points receive walkmesh positions through SET3 opcodes in their init scripts, making position extraction straightforward through bytecode parsing. The draw point depletion state lives in a dedicated 32-byte variable block rather than the entity struct, requiring separate memory monitoring. For a comprehensive accessibility mod, building this entity catalog at field load and then tracking proximity plus state changes at runtime will provide blind players with reliable, real-time awareness of every interactable element in the game world.