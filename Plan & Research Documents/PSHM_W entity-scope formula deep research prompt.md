# PSHM_W Entity-Scope Formula — Deep Research Request

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe + FFNx v1.23.x). The mod enables blind/low-vision players to navigate the game through TTS announcements. I need to resolve the positions of interactive objects (Directory panels, Squall's bed, classroom signs, desks, etc.) whose coordinates are computed by the PSHM_W opcode's undocumented entity-scope code path.

## What We Know

### The PSHM_W opcode (0x00C in the JSM VM dispatch table)

The documented behavior is a simple varblock read:
```
int16_t value = *(int16_t*)(0x1CFE9B8 + param);  // param = byte offset into variable block
```

But the actual handler at `0x0051C5C0` (Steam 2013 en-US) has **two code paths** controlled by execution flags at entity struct offset `0x160`:
1. **Standard path**: Simple varblock byte-offset read (addresses ≥ threshold)
2. **Entity-scope path**: Parametric curve computation (addresses below threshold or when specific execution flag bits are set)

### Entity struct layout (from FFNx ff8.h)

```c
struct ff8_field_state_common {
    uint8_t stack_data[0x140];       // VM stack: 320 bytes at offset 0x000
    uint32_t return_value;            // offset 0x140
    uint32_t field_144;               // offset 0x144
    // ... padding fields ...
    uint32_t execution_flags;         // offset 0x160 — controls PSHM_W code path
    // ... more fields ...
    uint8_t stack_current_position;   // offset 0x184 — single byte, NOT uint32_t
    // ... more fields ...
};
```

### The PSHM descriptor table

At address `0x01DCB340` there is a pointer table indexed by flat entity index (doors + lines + bg + others). Each entry points to a descriptor struct with key fields:
- `descriptor + 0x68`: data array pointer (parametric curve data)
- `descriptor + 0x6C`: secondary pointer
- `descriptor + 0x7E`: last PSHM address (WORD)
- `descriptor + 0x0C`: computed result X (WORD)
- `descriptor + 0x0E`: computed result Y (WORD)
- `descriptor + 0x50`/`0x52`: additional computed fields

The entity-scope subroutine is at `0x00532890`. It appears to use a multiply-by-9 pattern and sign-extended parameters.

### The global at `0x01CE476A`

A WORD value (reads as 20 on bghall_1) used in the handler's address computation. Likely a field entity count or threshold value.

### Diagnostic data from bghall_1 (B-Garden Hall)

We have SET3-captured runtime positions for 5 active entities AND their PSHM_W addresses from JSM bytecode analysis. We also tried reading the flat varblock at `0x1CFE9B8 + (uint16_t)addr` — all returned MISMATCH, proving these addresses DO NOT use the standard varblock formula.

**Entities with known positions (from SET3 hook capture):**

| Entity | SYM Name | PSHM Addr X | PSHM Addr Y | PSHM Addr Z | Actual Pos X | Actual Pos Y | Actual Pos Z |
|--------|----------|-------------|-------------|-------------|-------------|-------------|-------------|
| ent31 | l1 | 1032 | -2865 | -5421 | -2865 | -5421 | 567 |
| ent34 | l4 | 1032 | 1040 | 4 | -290 | -11298 | ? |
| ent35 | l6 | 1032 | 1036 | 1 | -448 | -11298 | ? |
| ent38 | elelight | 654 | -10304 | 567 | 654 | -10304 | ? |
| ent39 | stairlight | 1 | -700 | -8593 | -700 | -8593 | 567 |

**Entities we need positions for (no runtime data available — beyond 10-slot active window):**

| Entity | SYM Name | Type | PSHM Addr X | PSHM Addr Y | PSHM Addr Z |
|--------|----------|------|-------------|-------------|-------------|
| ent24 | dic | Directory panel | 135 | -82 | -8019 |
| ent25 | igyous1 | Interactive Object | 135 | -82 | -8019 |
| ent27 | savePoint | Save Point | 135 | 588 | -10392 |
| ent37 | water | Map Exit (elevator) | 142 | -1484 | -6059 |

**Varblock reads at these addresses (all failed to produce coordinates):**

| Entity | Varblock(addrX) | Varblock(addrY) | Varblock(addrZ) |
|--------|----------------|----------------|----------------|
| l1 | 0 | 0 | 0 |
| elelight | 0 | 0 | 0 |
| stairlight | 11590 | 0 | 0 |
| dic | 0 | 0 | 0 |
| savePoint | 0 | 0 | 0 |

### Critical observation: PSHM addresses sometimes ARE the coordinates

For several entities, the PSHM address parameters match the resolved coordinate values:

- **elelight**: addr=(654, -10304, 567), actual pos=(654, -10304, ?) → X and Y addresses ARE the coordinates
- **stairlight**: addr=(1, -700, -8593), actual pos=(-700, -8593, 567) → Y and Z addresses are X and Y coords (shifted by one position)
- **l1**: addr=(1032, -2865, -5421), actual pos=(-2865, -5421, 567) → Y and Z addresses are X and Y coords

But this doesn't hold for all entities:
- **l4**: addr=(1032, 1040, 4), actual pos=(-290, -11298) → no obvious match

### POPM_W writes from JSM scripts

Active entities write values into the varblock via POPM_W during their scripts. Some addresses written by active entities overlap with PSHM_W read addresses used by inactive entities:
- ent29 'cardgamemaster2' writes to: [0, 1029, 1040, 1039]
- ent30 'cardgamemaster3' writes to: [1040, 0, 1, 478, 292, 1033]
- ent31 'l1' writes to: [1040, 0, 1, 478, 292, 1037]
- ent32 'l2' writes to: [0, 1030, 1028, 1029, 1040, 1041, 1031, 1, 478, 292]

Addresses 1028-1041 are heavily used as both read and write targets across multiple entities, suggesting these are shared temp variables in the varblock region ≥ 1024.

## What I Need

1. **Disassembly analysis of the PSHM_W handler at `0x0051C5C0`** (Steam 2013 FF8_EN.exe). Specifically:
   - What are the execution flag bits at entity+0x160 that select the entity-scope path?
   - What is the threshold value that separates standard varblock reads from entity-scope reads?
   - How does the global WORD at `0x01CE476A` factor into the computation?

2. **Disassembly analysis of the entity-scope subroutine at `0x00532890`**. Specifically:
   - What is the multiply-by-9 pattern doing? Is it indexing into a 9-field descriptor struct?
   - How does it use the descriptor at `table[entityIndex]` from `0x01DCB340`?
   - What are the fields at descriptor+0x0C, +0x0E, +0x50, +0x52, +0x68, +0x6C, +0x7E?

3. **The formula**: Given an entity's PSHM_W address parameter and the entity-scope context, what is the exact computation that produces the coordinate value? Can we replicate this without running the entity's scripts?

4. **Why do some PSHM addresses equal the coordinates directly?** Is there a passthrough mode where the parameter is returned as-is? Or is this coincidental due to how the parametric data is structured?

5. **Can we extract entity-scope data from static game files** (JSM bytecode, field archives) to pre-compute these positions without runtime execution?

## Technical Details

- **Platform**: Windows 10, 32-bit x86 process (FF8_EN.exe)
- **Game version**: Steam 2013 (App ID 39150)
- **FFNx version**: v1.23.x (canary)
- **Varblock base**: `0x1CFE9B8` (confirmed by FFNx source: `field_vars_stack_1CFE9B8`)
- **PSHM_W handler**: `0x0051C5C0` (from dispatch table `pExecuteOpcodeTable[0x06]`)
- **Entity-scope sub**: `0x00532890`
- **Descriptor table**: `0x01DCB340` (pointer array, indexed by flat entity index)
- **Global field var**: `0x01CE476A` (WORD, value=20 on bghall_1)
- **Entity execution flags**: offset `0x160` in entity struct
- **VM stack**: offset `0x000` (320 bytes), stack pointer at `0x184` (uint8_t)

## Ideal Output

A pseudocode implementation of the entity-scope PSHM_W formula that I can implement in C++ to resolve coordinates for inactive entities from their JSM bytecode parameters alone, without needing the engine to execute their scripts.
