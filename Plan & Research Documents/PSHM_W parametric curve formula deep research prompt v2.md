# PSHM_W Entity-Scope Parametric Curve Formula — Deep Research Request v2

## Date: 2026-03-20
## Context: FF8 Steam 2013 Accessibility Mod — Interactive Object Coordinate Resolution

---

## What I Need

I need the **exact algorithm** inside the entity-scope subroutine at `0x00532890` in FF8_EN.exe (Steam 2013, 32-bit x86) that resolves PSHM_W address parameters into world coordinates via parametric curves stored in per-entity descriptor structs. The goal is to implement this computation statically in my accessibility mod so I can determine the world position of interactive objects (like the B-Garden Directory panel) without calling the engine function at runtime.

---

## Why This Matters

I'm building an accessibility mod for blind/low-vision players that enables auto-navigation to field entities. Some entities (like the Directory panel on bghall_1) have positions specified through PSHM_W shared memory opcodes that resolve via entity-scope parametric curves rather than simple variable reads. My mod can intercept the static JSM script data and the descriptor struct contents, but I need the formula to compute the final coordinate from them.

Currently the Directory panel's catalog position is ~494 world units off from the real interaction point. Getting the formula right would make it navigable with precision across all 894 game fields.

---

## Known Architecture (from prior deep research + runtime investigation)

### PSHM_W Handler Chain
- **PSHM_W opcode handler**: Dispatch table entry 0x0C. FFNx replaces this entry with its own hook (`opcode_pshm_w`), so reading the dispatch table gives FFNx code, not the original handler.
- **Core sub**: `0x0051C9C0` — type-clamping jump table. Called from the PSHM_W handler.
- **Entity-scope sub**: `0x00532890` — parametric curve computation. Called when entity execution flags (entity struct offset 0x160) indicate the alternate code path.

### Three PSHM_W Resolution Modes (per axis, independently)
1. **Negative parameter** → Passthrough literal coordinate (e.g., param = -82 → coordinate = -82). IMPLEMENTED in our mod.
2. **Small positive + entity execution flag** → Entity-scope parametric curve sub at `0x00532890`. THIS IS WHAT WE NEED.
3. **Standard positive (above threshold)** → Varblock read: `*(int16_t*)(0x1CFE9B8 + param)`. Does NOT work for entity-scope entities — varblock reads return wrong values for ALL PSHM_W entities on bghall_1.

### Threshold Mechanism
- **Global threshold selector**: WORD at `0x01CE476A`. On bghall_1 = 20.
- **Threshold formula**: Approximately `threshold ≈ 2696` for bghall_1 (value 20).
- Parameters below the threshold take the entity-scope path; parameters above take the standard varblock path.
- On bghall_1, ALL PSHM_W addresses (135, 142, 256, 588, 654, 717, 1032) are below the threshold, so ALL go through entity-scope.

### Per-Entity Descriptor Table
- **Table base**: `0x01DCB340` — array of DWORD pointers, indexed by flat JSM entity index (across all categories: doors + lines + bg + others).
- **Allocation**: On-demand. NULL until the entity's scripts first execute a PSHM_W. Our PSHM-PROBE diagnostic confirmed all entries are NULL during field_scripts_init for dic (ent24) because dic's PSHM_W fires in per-frame method 1+, not during init.
- **Struct size**: ~0x90 bytes (144 = 9 × 16).

### Descriptor Struct Key Fields (from prior deep research)
| Offset | Size | Description |
|--------|------|-------------|
| +0x00 | DWORD | First field (validity marker? -1 = invalid) |
| +0x0C | INT16 | Computed result X coordinate |
| +0x0E | INT16 | Computed result Y coordinate |
| +0x50 | INT16 | Field 0x50 (purpose unknown) |
| +0x52 | INT16 | Field 0x52 (purpose unknown) |
| +0x68 | DWORD | Data array pointer — parametric curve data |
| +0x6C | DWORD | Secondary pointer |
| +0x7E | WORD | Cache key — last PSHM address processed |

### Runtime Data from bghall_1 PSHM-PROBE Diagnostic
Our probe of the descriptor table at `0x01DCB340` during field load found **all entries NULL** for indices 0–42. This is because dic's scripts haven't executed yet at that point. The descriptors are allocated on-demand when the entity first processes a PSHM_W opcode.

### Ground Truth Coordinates (bghall_1 Directory panel — dic/igyous1)
- **JSM script data** (dic ent24): SET3 with PSHM_W params (135, 0, 0). The 0 params are actually negative passthrough (Y=-82 via shift-pattern, Z=-8019 via shift-pattern after promotion).
- **Shift-pattern result** (our current approximation): (-82, -8019) — this uses the Y,Z params as literal coords since X is the PSHM mode selector.
- **Player position when successfully interacting**: (21, -7536) — confirmed via F12 position announce.
- **Offset**: ~494 units. The first param (addr 135) contributes the missing delta through the entity-scope formula.

### What We Know About Other Entities on bghall_1
From SET3-HOOK captures and VARBLOCK-DIAG, many entities have PSHM_W addresses that resolve through entity-scope:
- **l1** (ent31): pshmAddr=(1032, -2865, -5421), SET3 resolved pos=(-2865,-5421). Here params 2,3 are negative passthrough and param 1 (1032) is the entity-scope selector.
- **stairlight** (ent39): pshmAddr=(1, 0, 0), SET3 resolved pos=(-700,-8593). Param 1 is just "1" — very small, definitely entity-scope.
- **elelight** (ent38): pshmAddr=(654, 0, 567), SET3 resolved pos=(654,-10304). Interesting: X=654 appears in both the address AND the result, suggesting passthrough for that axis.
- **cardgamemaster** (ent28): pshmAddr=(256, 0, 0), resolved pos=(-1596,-5807). Param 256 → -1596 through entity-scope.
- **VARBLOCK reads return wrong values for ALL of these** — confirming the entity-scope path is universal on this field.

### Extended SET3 Capture Window Finding (v0.08.16)
We extended the SET3 capture hook to stay active for 3 seconds after field load. Result: **dic never fires SET3 during this window**. Only walking NPCs (ent3/ent4 repositioning) and l1 appear. dic's SET3 is conditional — probably fires only when the player enters a specific camera zone. This means we cannot rely on runtime SET3 capture for dic and must compute the position statically.

### Walking NPC Captures Are Useful
The extended SET3 window DID successfully capture walking NPCs (model 21 at ent3, model 22 at ent4) repositioning after field load. This validates the extended capture window approach and will be useful for tracking moving NPC positions on other fields, even though it didn't help with dic specifically.

---

## What I Need From Deep Research

### Primary Goal
Reverse-engineer the exact algorithm inside `0x00532890` so I can implement it statically. Specifically:

1. **The parametric curve data format** at descriptor+0x68. How are entries structured? What are the data types? How many entries per curve?

2. **The computation that maps (PSHM address parameter, curve data) → output coordinate**. Is it a lookup table? Linear interpolation? Bezier evaluation? Something else?

3. **How the descriptor gets populated** — when the entity first processes PSHM_W, what writes to the descriptor struct? Is the data array pointer (offset +0x68) set during field_scripts_init, or later?

4. **How the cache key at +0x7E works** — does the sub skip computation if the address matches the cache? This would explain why we see computed X/Y at +0x0C/+0x0E.

5. **How the entity execution flags at struct offset 0x160 gate the code path** — what specific flag bits trigger entity-scope vs standard varblock?

### Secondary Goal
Determine if there's a simpler approach: can we read descriptor+0x0C/+0x0E directly at a later point (e.g., after 5-10 seconds when dic's per-frame scripts have populated the descriptor)? If so, what's the earliest reliable time to read it?

### Approach Suggestions
- **IDA Pro / Ghidra disassembly** of sub `0x00532890` from FF8_EN.exe would be ideal.
- **FFNx source code** at github.com/julianxhokaxhiu/FFNx may have references to this sub or the descriptor struct layout, even if not fully documented.
- **Qhimm wiki / community RE docs** may describe the field entity scripting VM's shared memory / parametric position system.
- The **Deling** or **Hyne** FF8 field editors may have insights into how PSHM_W entities' positions are encoded.

---

## Reference Files & Addresses (FF8_EN.exe Steam 2013, App ID 39150)

| Item | Address / Location |
|------|-------------------|
| FF8_EN.exe | Steam 2013 edition, 32-bit x86 |
| PSHM_W handler (FFNx-replaced) | Dispatch table entry 0x0C |
| PSHM_W core sub | `0x0051C9C0` |
| Entity-scope parametric sub | `0x00532890` |
| Descriptor pointer table | `0x01DCB340` |
| Varblock base | `0x1CFE9B8` (field_vars_stack) |
| Global threshold selector | `0x01CE476A` (WORD) |
| Entity struct execution flags | Entity base + 0x160 (uint32) |
| Entity struct stack pointer | Entity base + 0x184 (uint8, NOT uint32!) |
| Entity VM stack | Entity base + 0x000 (320 bytes) |
| update_field_entities | Resolved at runtime via offset chain |
| FFNx source | github.com/julianxhokaxhiu/FFNx (src/ff8_data.cpp, src/ff8.h) |

---

## Deliverable

The ideal output is a pseudocode implementation of `0x00532890` that I can translate to C++ in my mod. Something like:

```
int16_t ComputeEntityScopeCoordinate(
    int16_t pshmAddress,
    uint8_t* descriptorStruct,  // 0x90 bytes
    uint8_t* curveDataArray     // pointed to by descriptor+0x68
) {
    // ... the algorithm ...
    return result;
}
```

Even partial progress (e.g., "it's a cubic Bezier with control points at data[0..3]") would be extremely valuable.
