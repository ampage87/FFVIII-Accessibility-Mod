# FF8's hidden field transition engine: INF gateways drive bghall_1 exits

**The field-to-field transitions in `bghall_1` that appear to lack a script trigger are almost certainly driven by the INF gateway system** — an engine-level, data-driven mechanism that checks player position against trigger lines every frame, entirely independent of JSM opcodes. The "garbage" INF gateway data the researcher observed is very likely a format-version parsing error, and there is also a critical opcode-identification error at play: **0x5D is MAPJUMPON (enables INF gateways), not WORLDMAPJUMP** (which is actually extended opcode 0x10D). This misidentification may have caused the researcher to overlook the exact mechanism activating the transition system.

---

## Two independent transition systems coexist in the engine

FF8 inherits from FF7 a **dual transition architecture** that operates through completely separate code paths. Understanding this duality is essential to solving the bghall_1 puzzle.

**System A — Script-driven (MAPJUMP family):** Entity scripts explicitly call `MAPJUMP (0x29)`, `MAPJUMP3 (0x2A)`, `DISCJUMP (0x38)`, or `WORLDMAPJUMP (0x10D)` to force an immediate field transition. These opcodes push destination field ID and coordinates onto the stack, then trigger a transition directly. They are typically combined with `SETLINE (0x39)` trigger lines — when the player crosses a SETLINE, the entity's script executes and calls MAPJUMP. This is the **explicit, scripted** path.

**System B — INF gateway-driven (engine-level):** Each field's INF file defines up to **12 gateway entries**, each containing a trigger line (two 3D vertices forming a line segment on the walkmesh) and destination data (field ID, spawn coordinates, walkmesh triangle ID). The engine checks the player's walkmesh position against all active gateway lines every frame inside `field_main_loop`. When a line crossing is detected, the engine initiates a transition with **zero script involvement**. Gateway-based transitions are toggled by `MAPJUMPON (0x5D)` and `MAPJUMPOFF (0x5E)`, and their destinations can be dynamically overwritten by `MAPJUMPO (0x5C)`.

The FF7-equivalent opcode documentation for MPJPO (0xD2) confirms this explicitly: *"Turns on or off all the gateways in the current field. If set to off, the player will not be able to transition to other fields defined by the gateways. MAPJUMP opcodes are not affected."* FF8 splits this single toggle into three opcodes — MAPJUMPO, MAPJUMPON, MAPJUMPOFF — likely adding per-gateway granularity and runtime destination overwrite capability.

---

## The critical opcode misidentification that obscured the answer

The researcher's background states `WORLDMAPJUMP=0x5D`. **This is incorrect.** The complete opcode table (documented by Aali, myst6re, and Shard on the ffrtt.ru wiki) shows:

| Opcode | Name | Function |
|--------|------|----------|
| **0x5C** | **MAPJUMPO** | Overwrite/activate an INF gateway's destination data at runtime |
| **0x5D** | **MAPJUMPON** | Enable INF gateway transitions (by gateway ID or globally) |
| **0x5E** | **MAPJUMPOFF** | Disable INF gateway transitions |
| **0x10D** | **WORLDMAPJUMP** | Jump to world map (extended opcode, requires 0x1C prefix) |

WORLDMAPJUMP is an extended opcode beyond 0xFF. In JSM bytecode, opcodes above 0xFF are encoded using the **0x1C (HALT) prefix mechanism**: the engine encounters opcode byte 0x1C, then reads the next instruction to determine the actual extended opcode. Since 0x10D > 0xFF, it cannot appear as a raw high-byte value.

This means **if 0x5D appears in bghall_1's scripts, it is MAPJUMPON** — the exact opcode that enables the INF gateway system. The researcher may have seen it and dismissed it as WORLDMAPJUMP, or may not have checked entity 0 (the init entity) where MAPJUMPON is typically called. The exit entities (l1–l6, entities 26/31–35) would not need MAPJUMPON; it would be called once during field initialization.

---

## Why the INF data appears as "garbage" — format version mismatch

The INF file format has **four known versions on PC**, differentiated solely by file size:

| File size | Version | Gateway structure difference |
|-----------|---------|------------------------------|
| **672 bytes** | Standard PC | Full format with unknown data block per gateway + PVP field |
| **576 bytes** | Variant 1 | Unknown data = 4 bytes, no PVP field |
| **480 bytes** | Variant 2 (FF7-like) | No unknown data in gateways at all |
| **384 bytes** | Variant 3 (oldest) | Only one camera range, no screen ranges |

Each gateway entry in the standard 672-byte format occupies approximately **24 bytes**: two 3D vertices (12 bytes for the trigger line), destination field ID (2 bytes), destination coordinates (6 bytes), walkmesh triangle ID (2 bytes), and unknown/padding data (variable). In the 480-byte FF7-like format, the unknown data is absent, making each entry smaller.

**If a parser assumes the 672-byte format for a field that actually uses the 480- or 576-byte format**, every gateway entry's fields will be read at incorrect offsets. The trigger line vertices would bleed into destination fields, destination field IDs would read from coordinate data, and coordinates would read from the next gateway's trigger line. The result: **every value appears wrong** — wrong destination fields, wrong coordinates — exactly matching the researcher's observation.

The PC version mixes format versions because some field archives were converted from different PSX development stages. The engine detects the format by checking the INF section's byte count and adjusts its parsing offsets accordingly. Tools like Deling (by myst6re) implement this detection; a raw hex parser that assumes a single format will produce garbage for fields using an alternate layout.

Additionally, **MAPJUMPO (0x5C) can overwrite gateway destinations at runtime**. Even if the static INF data contains placeholder or incorrect destination values, the field's init script can call MAPJUMPO for each gateway to set the correct destination field ID and coordinates before the player can reach any exit. This would mean the INF file's stored destinations are intentionally ignored — they are overwritten before they matter.

---

## What opcodes 0x13 and 0x19 actually do

Neither opcode is transition-related:

**Opcode 0x13 — PSHAC (Push Actor):** A memory opcode in the PSH* family that pushes the current executing entity's actor/entity ID onto the stack. Used for self-identification in scripts — an entity can check its own ID for conditional logic. Its 12 occurrences in bghall_1 likely reflect entities checking which actor they control to branch behavior accordingly.

**Opcode 0x19 — PREQEW (Party Request Execute Wait):** A script-processing opcode that sends an execution request to a party member entity's script method and blocks until that method completes. Part of the REQ family (REQ=0x14, REQSW=0x15, REQEW=0x16, PREQ=0x17, PREQSW=0x18, PREQEW=0x19). The single occurrence in bghall_1 likely triggers a party member animation or dialog sequence.

---

## The engine's per-frame gateway check and module switching

Reverse engineering from the Qhimm forums (by Halfer, 2016) and FFNx source code reveals the engine's transition architecture:

**Module index variable:** At runtime, the engine maintains a **"module index to be transported"** variable (at `0x01CE4760` in FF8 PC 2000; equivalent address in Steam 2013 found via FFNx's `common_externals._mode`). This variable is checked every frame by the main game loop. Its values determine what module to load next:
- `0x01` = Load field module, with the destination field ID stored at the next word address
- `0x03` = Load battle module
- `0x07` = Load world map module

When a gateway crossing is detected inside `field_main_loop` (or when a MAPJUMP opcode executes), the engine writes to this module index and the destination field ID. The main loop then calls `field_main_exit` to tear down the current field and loads the new one.

**FFNx confirms key addresses** relative to `main_loop`:
- `field_main_loop` at offset **+0x144** (non-US: +0x147)
- `field_main_exit` at offset **+0x13C** (non-US: +0x13F)
- `current_field_id` (WORD*) at offset **+0x21F** (non-US: +0x225) — resolves to approximately `FF8_EN.exe+18D2FC0`
- `field_fade_transition_sub_472990` at offset **+0x19E** from `field_main_loop` — the fade effect function called during gateway transitions

The gateway trigger-line check itself is a standard **2D line-crossing test** using the cross product of the player's movement vector against the gateway line segment. Each frame, the engine computes which side of each gateway line the player stands on. When the sign flips relative to the initial side (recorded when the field loaded or the gateway was enabled), a crossing has occurred. The Y-axis/height component is typically ignored, making this a 2D test in the XZ plane of walkmesh coordinates.

---

## The l1–l6 entities handle camera scrolling, not transitions

The six entities l1–l6 in bghall_1 with SETLINE triggers and BGDRAW/BGOFF/scroll opcodes serve a **purely visual purpose**: they control camera panning as the player approaches the edges of the hallway. When the player crosses a SETLINE, the entity script executes camera operations (scrolling the background layers) but does **not** call MAPJUMP. The gateway trigger lines in the INF file are positioned slightly beyond these SETLINE regions.

The sequence is: player walks toward edge → crosses SETLINE → camera begins scrolling → player continues → crosses INF gateway line → engine triggers field transition. This two-layer design separates the visual effect (smooth camera pan) from the navigation logic (gateway-based transition).

The POPM_W writes to addresses **1024–1041** are field-local temporary variables (variables ≥1024 are zeroed on each field load). These likely track which camera state is active, which background layers are visible, or which scroll direction has been triggered — **not** transition-related data. The engine does not monitor arbitrary variable addresses for transition triggers. Transitions are signaled exclusively through the module index mechanism or direct MAPJUMP execution.

---

## Walkmesh edges and SETLINE cannot trigger transitions

**Walkmesh edge (neighbor=0xFFFF):** A walkmesh triangle edge with no neighbor simply **blocks movement**. The engine prevents the player from crossing that edge — the character stops at the boundary. It does not trigger any transition or callback. This is purely a collision boundary.

**SETLINE trigger lines:** These define regions that, when crossed, execute the associated entity's script. They have **no built-in transition capability**. A SETLINE can trigger a script that calls MAPJUMP, but the SETLINE itself is categorized as an "Entity" opcode, not a "Field related" opcode. Without a MAPJUMP call in the triggered script, crossing a SETLINE does nothing navigational.

---

## Conclusion: the complete picture for bghall_1

The bghall_1 field transitions work through the **INF gateway system**, which is the engine's native, data-driven field transition mechanism requiring no JSM opcodes in exit entities. The specific chain of events is:

1. When bghall_1 loads, entity 0's init script likely calls **MAPJUMPON (0x5D)** to enable gateway checking, and potentially **MAPJUMPO (0x5C)** to overwrite gateway destinations with correct values
2. The INF file's 12 gateway entries define trigger line segments positioned at each hallway exit (infirmary, cafeteria, library, training center, quad, dormitory, parking, 2F elevator, front gate, etc.)
3. Each frame, `field_main_loop` runs the gateway crossing check against the player's walkmesh position — a 2D cross-product side test against each active gateway line
4. When a crossing is detected, the engine writes the destination field ID and coordinates to the module transition registers and initiates `field_main_exit`
5. The l1–l6 SETLINE entities independently manage camera scrolling effects as the player approaches exits

The "garbage" INF data is almost certainly a **format-version parsing error** (the INF uses a different byte layout than the researcher's parser assumes) or reflects destinations that are **overwritten at runtime by MAPJUMPO** before they are ever used. To verify: check entity 0's init script for MAPJUMPON/MAPJUMPO calls, and parse the INF section using file-size-based format detection (as Deling does) rather than assuming a fixed layout.

The researcher should look at entity 0's scripts (not just l1-l6), check for opcode bytes 0x5C/0x5D/0x5E, and re-parse the INF using the correct format version for bghall_1's actual INF section size.