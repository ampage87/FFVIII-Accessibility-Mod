# Deep Research Request: FF8 PC Entity Position Projection Function

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe, 32-bit x86). The mod is a Win32 DLL (`dinput8.dll`) injected via DirectInput proxy, with FFNx v1.23.x installed as a graphics/compatibility layer. I need to find the exact function in FF8_EN.exe that projects 3D walkmesh coordinates into the 2D entity coordinate space.

## The Problem

FF8 field screens have a 3D walkmesh (stored in .id files) and entities whose positions are stored as 2D projected coordinates in the entity struct. On flat fields (where the walkmesh Z≈constant), the 3D X,Y map directly to entity 2D X,Y. But on **angled fields** (like `bg2f_1`, the Garden 2F corridor, where Z ranges from 484 to 10413), the engine applies some projection/transform to convert 3D walkmesh coordinates to the 2D coordinates stored in the entity struct.

I need to find this projection function so I can either:
- Hook it and read the transform matrix directly, or
- Understand the math so I can replicate it in my mod for A* pathfinding

## What I Know

### Entity struct layout (ff8_field_state_other, stride 0x264)
- `+0x190`: int32 X * 4096 (fixed-point, 12-bit fractional) — 2D projected X
- `+0x194`: int32 Y * 4096 — 2D projected Y  
- `+0x198`: int32 Z * 4096 — appears to always equal the walkmesh Z (not projected)
- `+0x1FA`: uint16 current triangle ID on the walkmesh

The entity position at 0x190/0x194 is in a **2D projected coordinate space** — it's what the game uses for collision detection, NPC interaction distances, trigger line checks, etc. It is NOT raw walkmesh 3D coordinates on angled fields.

### Key game functions (resolved at runtime from FF8_EN.exe)
- `set_current_triangle` at 0x0045E160: called when entity moves to new walkmesh triangle. Args are 3 pointers to int16_t[3] vertex records (x,y,z). I've hooked this — it fires whenever the player steps onto a new triangle.
- `engine_eval_keyboard_gamepad_input` at 0x00401F60: processes input each frame. I've confirmed via before/after snapshot that the entity position at 0x190/0x194 does **NOT** change during this function call.
- `field_scripts_init`: initializes field scripts and entities on field load.
- `ctrl_keyboard_actions`: reads keyboard direction from buffer (called inside engine_eval).
- `get_key_state` at 0x004685F0: fills keyboard buffer.

### FFNx reference (src/ff8_data.cpp and related)
FFNx's source code (used as read-only reference for address offsets and struct layouts) contains:
- `ff8_externals.field_state_other_base_ptr` — pointer to the entity state array
- `ff8_externals.set_current_triangle_sub` — the set_current_triangle function
- `ff8_field_state_other` struct definition
- Various field update functions

### What I've confirmed empirically
1. On flat fields (bgroom_1, Z≈0): entity 2D X,Y ≈ walkmesh 3D X,Y directly (identity transform)
2. On angled fields (bg2f_1, Z=484-10413): entity 2D coords are clearly transformed — a ~0.94 scale on X, ~0.97 scale on Y, small rotation, offset of about (4, -87)
3. The .ca camera file does NOT provide this transform — multiple ChatGPT deep research sessions confirmed the .ca data is for background rendering only, not entity positioning
4. The transform is NOT stored in any data file (.ca, .mim, .map, .one, .id, .inf)
5. The position update does NOT happen inside `engine_eval_keyboard_gamepad_input`

### Empirical data (bg2f_1 field)
Sample paired data points (2D entity position vs 3D walkmesh edge midpoint):
```
2D=(241,-2876)  3D=(165,-2779,8341)
2D=(241,-3309)  3D=(165,-3129,8341)  
2D=(241,-3464)  3D=(245,-3303,8341)
2D=(-253,-3278) 3D=(-171,-3469,8341)
2D=(180,-2907)  3D=(325,-2849,8341)
2D=(162,-2664)  3D=(245,-2674,8341)
```

The Z coordinate is constant at 8341 for this field, so the transform is effectively a 2D affine:
```
2dX ≈ 0.80 * 3dX + 0.01 * 3dY + 31
2dY ≈ -0.25 * 3dX + 1.15 * 3dY + 417
```
But the fit has ~60-125 unit RMSE, too noisy for precise pathfinding.

## What I Need

1. **Primary goal**: Identify the function in FF8_EN.exe (or the game's main loop) that writes to entity offset 0x190/0x194 after computing the 3D→2D projection. This would be a function that:
   - Reads the entity's 3D position on the walkmesh
   - Applies some camera/field-dependent transform (possibly using the .ca camera matrix, or a hardcoded projection)
   - Writes the result to the entity struct at offsets 0x190 and 0x194
   - Runs once per frame for each entity that moved

2. **Secondary goal**: Identify what data the projection uses. Does it read from:
   - The .ca camera file data (axis vectors, position, zoom)?
   - A hardcoded matrix in the executable?
   - Some runtime-computed transform stored in a global?

3. **Tertiary goal**: If you can identify the projection math, what is it? Is it:
   - A simple orthographic projection using the camera axis vectors?
   - A perspective projection?
   - Something else (e.g., the PSX GTE-style fixed-point matrix multiply)?

## Search Guidance

The function I'm looking for is likely:
- Called from the main game loop (not from the input handler — I've confirmed that)
- Called after entity movement is computed but before collision/interaction checks
- Part of the field entity update pipeline
- Possibly related to `set_current_triangle` (called nearby or as part of the same update sequence)

Relevant FFNx source files to check for function names/addresses:
- `src/ff8_data.cpp` — contains most resolved function addresses and struct layouts
- `src/ff8.h` — FF8 struct definitions
- `src/ff8_field.cpp` / `src/ff8_field.h` — field-specific logic
- `src/field/` directory — field subsystem code

The FF8_EN.exe base address is 0x00400000. All function addresses are absolute (not RVA). The game is 32-bit x86 Windows.

The original PSX version used the GTE (Geometry Transform Engine) for 3D→2D projection. The PC version likely reimplements this in software. The PSX projection formula for field entity positions might involve the camera rotation matrix from the .ca file, but our empirical tests showed the .ca formula doesn't match. The PC version may use a different projection method or different sign conventions.

Key insight: the entity positions at 0x190/0x194 are NOT screen pixel coordinates — they're in a 2D "field coordinate" space used for gameplay logic (collision, interaction, trigger lines). This space is what the SETLINE trigger coordinates, INF gateway coordinates, and entity-to-entity distances are computed in. It appears to be a top-down orthographic projection of the 3D walkmesh onto a camera-aligned 2D plane.

## Additional Context

The FF8 field engine processes entities in this rough order each frame:
1. Script execution (JSM opcodes)
2. Input processing (engine_eval_keyboard_gamepad_input)  
3. Movement computation (walkmesh collision, triangle transitions)
4. Position projection (3D walkmesh → 2D entity coords) ← **THIS IS WHAT I'M LOOKING FOR**
5. Interaction checks (trigger lines, NPC talk radius)
6. Rendering

Steps 1-2 do NOT modify the entity position at 0x190/0x194 (confirmed). The projection must happen at step 3 or 4. The `set_current_triangle` function fires during step 3 but the projection may happen in the caller or a sibling function.

## Format

Please provide:
1. The most likely function address(es) in FF8_EN.exe that write to entity offset 0x190/0x194
2. The call chain from the main game loop to this function
3. Any relevant data structures or global variables the projection reads from
4. The projection math if identifiable
5. Suggestions for which function to hook to capture the transform matrix at runtime
