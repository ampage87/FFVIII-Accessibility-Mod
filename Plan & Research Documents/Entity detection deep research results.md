# Deep Research: FF8 Field Entity Detection, Availability, Positioning, and SYM Naming

## 1. The Entity System Architecture

### 1.1 Entity Categories (from JSM)

FF8's JSM (field script) system defines four entity categories. According to the Qhimm wiki, the JSM file's entry point table is ordered:

**Doors → Lines → Backgrounds → Others**

However, the SYM (symbol names) file lists names for **all entities EXCEPT doors**. So SYM order is:

**Lines → Backgrounds → Others**

The game's runtime entity state array (`pFieldStateOthers` / `ff8_externals.field_state_others`) contains **ONLY the "Others" category**. This is the key insight — only NPCs, PCs, interactive objects, and similar entities have runtime state. Doors, lines, and background scripts are handled by separate subsystems.

### 1.2 The `ff8_field_state_other` Struct (from FFNx ff8.h)

```cpp
struct ff8_field_state_other {
    ff8_field_state_common common;   // 0x000-0x187 (0x188 bytes)
    uint8_t gap1[114];               // 0x188-0x1F9
    uint16_t current_triangle_id;    // 0x1FA
    uint8_t gap1b[28];               // 0x1FC-0x217
    int16_t model_id;                // 0x218
    uint8_t gap2[47];                // 0x21A-0x248
    uint8_t pushonoff;               // 0x249
    uint8_t gap3;                    // 0x24A
    uint8_t talkonoff;               // 0x24B
    uint8_t throughonoff;            // 0x24C
    uint8_t gap4[2];                 // 0x24D-0x24E
    uint8_t baseanim1;               // 0x24F
    uint8_t baseanim2;               // 0x250
    uint8_t baseanim3;               // 0x251
    uint8_t ladderanim1;             // 0x252
    uint8_t ladderanim2;             // 0x253
    uint8_t ladderanim3;             // 0x254
    uint8_t setpc;                   // 0x255
    uint8_t gap5;                    // 0x256
    uint8_t setgeta;                 // 0x257
    uint8_t gap6[12];                // 0x258-0x263
};
// Total stride: 0x264 bytes — CONFIRMED
```

### 1.3 Key Offsets (confirmed through our testing)

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| 0x190 | int32 | X position (fixed-point ×4096) | Always live for player |
| 0x194 | int32 | Y position (fixed-point ×4096) | Vertical axis |
| 0x198 | int32 | Z position (fixed-point ×4096) | Always live for player |
| 0x1FA | uint16 | current_triangle_id | Walkmesh triangle |
| 0x218 | int16 | model_id | -1 = invisible/no model |
| 0x249 | uint8 | pushonoff | Push interaction flag |
| 0x24B | uint8 | talkonoff | Talk interaction flag |
| 0x24C | uint8 | throughonoff | Walk-through trigger |
| 0x255 | uint8 | setpc | 0 = player character |
| 0x257 | uint8 | setgeta | Get-A interaction |

### 1.4 Entity Count Sources

- `pFieldStateOtherCount` (uint8*): Number of "other" entities for current field
- `pFieldStateOthers` (ff8_field_state_other**): Pointer to array of entity states
- `pFieldStateBackgroundCount` (uint8*): Number of background script entities
- `pFieldStateBackgrounds` (ff8_field_state_background**): Background states

---

## 2. Entity Availability Detection

### 2.1 What Makes an Entity "Available"?

An entity exists in the state array from field initialization, but its **availability** (whether it should appear in the navigation catalog) depends on runtime flags that JSM scripts modify dynamically:

1. **Has a visible model**: `model_id >= 0` (set by `SETMODEL` opcode, cleared to -1 by `HIDE` or never set)
2. **Is placed on the walkmesh**: `current_triangle_id > 0` (set by `SETPLACE`/`SET` opcodes; 0 may also be valid in some fields)
3. **Has interaction capability**: `talkonoff > 0` OR `pushonoff > 0` OR `throughonoff > 0` (toggled dynamically by `TALKON`/`TALKOFF`, `PUSHON`/`PUSHOFF`, `THROUGHON`/`THROUGHOFF`)
4. **Is not the player**: `setpc != 0` for non-player entities (but `setpc == 0` is the player — we exclude the player from navigation targets)

### 2.2 Dynamic Availability Changes

Entities change availability at runtime. JSM scripts can:
- **Spawn/despawn** entities by toggling their model and position
- **Enable/disable** interaction via TALKON/TALKOFF opcodes
- **Move** entities between triangles via movement opcodes
- **Change the player character** via SETPC

**Critical finding from v05.41+**: Initial field_scripts_init doesn't reveal all entities. Scripts execute after init and may spawn NPCs dynamically. This is why we use on-demand `RefreshCatalog()` when the user presses navigation keys.

### 2.3 Entity Types for Navigation

| Entity Type | Characteristics | Navigation Relevance |
|-------------|----------------|---------------------|
| **Player (PC)** | setpc==0, model>=0, moves | Excluded from targets (IS the navigator) |
| **NPC (Character)** | model>=0, talkonoff>0 | Primary navigation target ("Quistis") |
| **Interactive Object** | model>=0 or model==-1, pushonoff>0 | Draw points, save points, etc. |
| **Walk-through Trigger** | throughonoff>0, often invisible | Field transitions, event triggers |
| **Invisible Script Entity** | model==-1, no interaction flags | Background controllers — EXCLUDE |
| **Gateway Exit** | From INF file, not entity array | Field-to-field transitions |

### 2.4 Recommended Qualification Criteria

```
INCLUDE if:
  (model_id >= 0)                          // Has a visible 3D model
  AND (current_triangle_id >= 0)           // Is placed somewhere (0 is valid!)
  AND (setpc != 0)                         // Not the player
  
OR:
  (talkonoff > 0 || pushonoff > 0)         // Has interaction flags
  AND (current_triangle_id >= 0)           // Is placed
  AND (setpc != 0)                         // Not the player

EXCLUDE if:
  (model_id == -1 AND talkonoff == 0 AND pushonoff == 0 AND throughonoff == 0)
  // Invisible background controller with no interaction
```

Note: `throughonoff` alone (without model or talk/push) likely indicates a walk-on trigger zone. These could be included if they have meaningful names from SYM.

---

## 3. Entity Positioning

### 3.1 Position Data Sources (Priority Order)

**Strategy 1: Fixed-point coordinates (0x190/0x198)**
- `int32` at offset 0x190: X × 4096 (fixed-point)
- `int32` at offset 0x198: Z × 4096 (fixed-point)
- **Always live for the player entity**. For NPCs, populated when they move.
- Convert: `worldX = (float)(raw / 4096)`

**Strategy 2: Simple int16 coordinates (0x20/0x28)**
- `int16` at offset 0x20: X position
- `int16` at offset 0x28: Z position  
- Populated for NPCs placed by JSM SET opcodes at field init.
- Less precise but works for stationary NPCs.

**Strategy 3: Triangle centroid from walkmesh hook**
- `set_current_triangle_sub_45E160` is called whenever any entity transitions triangles.
- Arguments are 3 vertex pointers → compute centroid.
- Stored in triId→center map. Look up by entity's `current_triangle_id`.
- Works for any entity on the walkmesh, but only updates on triangle changes.

**Strategy 4: INF gateway positions (for exits only)**
- Gateway center coordinates from the INF file.
- Only applicable to ENT_EXIT catalog entries.

### 3.2 Position Strategy Selection

For each entity in the catalog:

```
if (entity is gateway exit):
    use INF gateway centerX/centerZ
else:
    try fixed-point 0x190/0x198 first (precise, live)
    if (both zero):
        try int16 0x20/0x28 (works for placed NPCs)
    if (both zero AND triId is valid):
        use triCenter[triId] from walkmesh hook cache
    else:
        report "not yet located"
```

### 3.3 Known Position Issues

- **triId == 0**: Valid in some fields (bgroom_1 where Squall starts seated at triangle 0). Don't filter these out.
- **Stationary NPCs**: May never trigger `set_current_triangle` after field load, so the hook-based cache might not have their position. The 0x20/0x28 fallback handles this.
- **Fixed-point coords at (0,0)**: A legitimate position (origin). Don't treat as "missing."

---

## 4. SYM Name Mapping

### 4.1 SYM File Format (confirmed from Qhimm wiki + ff7-flat-wiki)

- Text file, 32 bytes per line (space-padded, last byte = `\n`)
- **Excludes door entities** (doors have no SYM names)
- Lists entity names first (lines, backgrounds, others), then script function names (delimited by `::`)

### 4.2 Mapping SYM Names to Entity State Array

The entity state array (`pFieldStateOthers`) only contains "Others" entities. The SYM file lists names for Lines + Backgrounds + Others. Therefore:

```
SYM index of first "Other" entity = countLines + countBackgrounds
```

These counts come from the JSM header. For entity state index `i`:

```
symIndex = countLines + countBackgrounds + i
entityName = symNames[symIndex]
```

### 4.3 JSM Header Format

The JSM header is the first 8 bytes of the JSM file:

- Byte 0-3: Entity category counts (4 × uint8)
- Bytes 4-7: Offset to script data section (uint32 LE)

**The exact byte-to-category mapping needs empirical verification** via the hex dump in v05.48. The Qhimm wiki mentions the entity order in the entry point table is "Doors, Lines, Backgrounds, Others" but the header byte order may differ. Possible mappings:

1. `b0=others, b1=doors, b2=lines, b3=backgrounds` (current tentative)
2. `b0=doors, b1=lines, b2=backgrounds, b3=others` (natural order matching entry points)

The diagnostic hex dump + comparison with known entity counts will disambiguate.

### 4.4 Verification Strategy

For a known field like **bghoke_2** (infirmary):
- SYM has 15 names: squall, squall2, zell, selphie, quistis, quistis2, rinoa, irvine, kadowaki, cid, dic, musickun, evl1, curtain, squall
- Entity state has ~10 entities
- If JSM says: doors=0, lines=2, backgrounds=3, others=10
  → SYM offset = 2 + 3 = 5 → entity 0 maps to SYM[5] = "quistis2"? 
  
This won't make sense if squall should be entity 0. So we need to check:
- Does the SYM ordering truly skip doors, or does it skip something else?
- Are the first SYM names lines/backgrounds or characters?

The ENTDIAG dump from v05.48 will reveal this by showing the model_id and setpc for each entity, letting us identify which entity is the player (Squall) and match against the SYM name list.

---

## 5. Entity Type Classification

### 5.1 Determining What an Entity IS

For screen reader announcement, we want descriptive names. Beyond the SYM name, we can classify entities:

| Classification | Detection Method |
|---------------|-----------------|
| **Party member (PC)** | setpc == 0 for current player; for other party members, check if name matches known character names (Squall, Rinoa, etc.) |
| **NPC (talkable)** | talkonoff > 0, model >= 0 |
| **Draw Point** | Script triggers `opcode_drawpoint` (0x137). Could also be detected by SYM name containing "draw" |
| **Save Point** | Usually an invisible entity with a specific model or script behavior |
| **Door/Exit** | throughonoff > 0, or gateway from INF |
| **Item/Object** | pushonoff > 0, model >= 0 |

### 5.2 The Draw Point Problem

Draw points are special interactive entities. FFNx hooks `opcode_drawpoint` (0x137) for voice acting. The draw point's state (available/depleted) is stored in the save data via `set_drawpoint_state_521D90`. 

For our purposes, a draw point entity will appear in the catalog as an entity with `pushonoff > 0` or `talkonoff > 0`. The SYM name will identify it (e.g., "draw_point" or similar). We may want to query the save data to know if it's available or depleted, but that's a future enhancement.

---

## 6. Win_obj field_30 and Text Change Detection

FFNx's `voice.cpp` reveals an important field we should be aware of:

```cpp
bool _has_dialog_text_changed = win->field_30 != current_opcode_message_status[window_id].message_dialog_id;
```

The `ff8_win_obj.field_30` field (offset 0x30 in the window object struct) is used by FFNx to detect text changes in battle and tutorial modes. The full win_obj struct is 0x3C bytes (confirmed: has `callback1` and `callback2` at the end).

This is relevant for the walk-and-talk dialog gap investigation (noted in DEVNOTES) — monitoring `field_30` changes could catch hardcoded dialog that bypasses opcode hooks.

---

## 7. Recommended Implementation Plan

### Phase 1: Verify JSM Header + Fix SYM Mapping (v05.48 — current build)
- Test the diagnostic build
- Determine correct JSM header byte mapping from hex dump
- Fix byte assignment if needed
- Verify SYM names appear on correct entities in ENTDIAG output

### Phase 2: Refine Entity Qualification (v05.49)
Based on ENTDIAG results:
- Accept triId==0 entities with models (already done in v05.48)
- Consider including entities with SYM names even if no interaction flags
- Classify entities by type (NPC, object, exit, etc.) using a combination of flags and SYM names

### Phase 3: Entity Availability Monitoring (v05.50+)
- Track dynamic flag changes (talkonoff/pushonoff toggling) via periodic polling
- Refresh catalog entries when flags change
- Consider hooking key JSM opcodes (TALKON/TALKOFF, SETMODEL, SETPLACE) for instant detection

### Phase 4: Rich Entity Naming (v05.51+)  
- Capitalize SYM names for announcement ("quistis" → "Quistis")
- Map known entity names to friendly descriptions
- Identify draw points, save points by SYM name patterns
- Add "available" / "depleted" status for draw points from save data

---

## 8. The `ff8_field_state_common` Execution State

Every entity (background and "other") shares a common prefix struct at offset 0x000:

```
ff8_field_state_common (0x188 bytes):
  0x000-0x13F: stack_data[0x140]          - JSM operand stack
  0x140:       return_value (uint32)
  0x160:       execution_flags (uint32)    - Bit flags for entity activity
  0x174:       has_anim (uint8)           - Entity has animation?
  0x175:       has_anim_mask (uint8)
  0x176:       current_instruction_position (uint16) - Script IP
  0x184:       stack_current_position (uint8)
```

The `execution_flags` field is particularly interesting:
- 0x10 = bgdraw (background drawing active)
- 0x800 = animation ongoing
- 0x980 = bganime/rbganime

An entity with nonzero `execution_flags` is "alive" — its scripts are executing. An entity with `execution_flags == 0` and `current_instruction_position == 0` may be dormant/uninitialized.

This could provide an additional filter: entities whose scripts haven't started yet (instruction position 0, no execution flags) might be placeholders that shouldn't appear in the catalog.

---

## 9. Key References

- **FFNx ff8.h**: Entity struct definitions, ff8_win_obj, field state pointers
- **FFNx ff8_data.cpp**: Address resolution for field_scripts_init, entity arrays, dialog system
- **FFNx voice.cpp**: Dialog detection via opcode hooks + window state tracking  
- **Qhimm wiki FileFormat_JSM**: JSM header format, entity ordering
- **Qhimm wiki FileFormat_SYM**: SYM excludes doors, 32-byte fixed-width records
- **Qhimm wiki FileFormat_INF**: Gateway format (12 × 24 bytes at offset 0x20)
- **Our DEVNOTES.md**: Confirmed offsets, position strategies, entity struct layout
