# FF8's JSM memory system: what the opcodes actually do

**The PSHM_W opcode (0x00C) reads a 16-bit unsigned word from a contiguous variable block at runtime address `0x1CFE9B8` (Steam 2013 en-US), using its parameter as a byte offset.** However, the disassembly you've uncovered — execution flag branching, the multiply-by-9 pattern, and sign-extended negative addresses — reveals that the handler does significantly more than the community wiki documents. The negative parameters (-82, -8019) in the dic entity's SET3 call cannot work with the simple `varblock + param` formula, confirming the handler takes an alternate code path governed by entity execution flags at offset 0x160. Below is everything recoverable from public sources, combined with analysis of what your disassembly observations imply.

---

## The variable block is a flat byte array with a known base

The FF8 community documentation (by Aali, myst6re, and Shard) describes the PSHM/POPM system as accessing a single contiguous byte array — the "varblock." FFNx resolves this base dynamically using a pointer chain:

```
update_field_entities
  → +0x65A → execute_opcode_table (array of function pointers)
    → [0x0C] → opcode_pshm_w handler address
      → +0x1E → embedded absolute reference to varblock base
```

The naming convention in FFNx is explicit: the field is called `field_vars_stack_1CFE9B8`, where the suffix **`0x1CFE9B8`** is the absolute address in the Steam en-US binary. The original/SE en-US release uses **`0x18FE9B8`**. This base is consistent with save file structure — the same variable block maps to save file offset **0xD10** in uncompressed PC saves. The documented access formula is simply:

```c
// Documented PSHM_W behavior for standard variables
int16_t value = *(int16_t*)(0x1CFE9B8 + param);  // param = byte offset
```

Example: `PSHM_W 256` reads the 16-bit word at `0x1CFE9B8 + 0x100 = 0x1CFEAB8`, which is the **main story quest progress** variable. This formula is confirmed by Cheat Engine tables in the ff8-speedruns/ff8-memory repository.

The variable space divides cleanly: **bytes 0–1023** are persistent game-state variables saved to disk (Gil at offset 72, story progress at 256, draw points at 116–179, character costumes at 720–723), while **bytes above 1023** are temporary per-field variables that reset on field transitions. The temp region is what the wiki calls variables "used pretty much everywhere."

---

## PSHSM is "Push Signed Memory," not "Push Shared Memory"

A critical clarification: the PSHSM opcodes (0x010–0x012) are **signed-read variants** of PSHM, not separate "shared memory" accessors. They read from the **same varblock** but sign-extend the result. The evidence is definitive:

- There are **no POPSM (Pop Signed Memory) counterparts** — writing doesn't require sign treatment, only reading does
- The wiki's variable table explicitly marks certain variables as "Signed Word" (e.g., var 16 for SeeD rank, var 528 for sub-story progression) and "Signed Byte" (var 86 for car rent), corresponding exactly to PSHSM_W and PSHSM_B usage
- The hobbitdur FF8 Modding Wiki describes PSHSM_W as "same as PSHM_W but the value is sign-extended"

The complete memory opcode family:

| Opcode | Name | Width | Signed | Direction |
|--------|------|-------|--------|-----------|
| 0x00A | PSHM_B | Byte | No | Read → Stack |
| 0x00B | POPM_B | Byte | — | Stack → Write |
| **0x00C** | **PSHM_W** | **Word** | **No** | **Read → Stack** |
| 0x00D | POPM_W | Word | — | Stack → Write |
| 0x00E | PSHM_L | Long | No | Read → Stack |
| 0x00F | POPM_L | Long | — | Stack → Write |
| 0x010 | PSHSM_B | Byte | Yes | Read → Stack |
| **0x011** | **PSHSM_W** | **Word** | **Yes** | **Read → Stack** |
| 0x012 | PSHSM_L | Long | Yes | Read → Stack |

Both PSHM_W and PSHSM_W use the same underlying memory access mechanism; the only difference is zero-extension vs sign-extension of the result pushed onto the 32-bit VM stack.

---

## The multiply-by-9 and execution flags reveal undocumented handler complexity

Your disassembly of the handler at `0x0051C5C0` exposes behavior not covered in any public documentation. The community wiki describes PSHM_W as a simple byte-offset read, but the actual x86 implementation branches based on entity execution flags — meaning the handler has **at least two distinct code paths**.

**What the execution flags likely control:** The flags at entity struct offset 0x160 appear to switch between a "standard varblock access" mode and an "entity-relative or model-data access" mode. The bit-field checks — bit 28 (`0x10000000`) and bit 16 (`0x00010000`) — suggest at least two mode selectors. This architecture would allow the same PSHM_W opcode to serve double duty: reading from global game variables in one mode and reading from entity-local or per-model data in another.

**The multiply-by-9 pattern** (`LEA ECX, [ECX+ECX*8]`) applied to the sign-extended address parameter is the strongest indicator of an alternate memory layout. Three hypotheses for what this multiplication indexes:

1. **9-byte entity variable entries:** Each variable slot could be a 9-byte record (e.g., 4-byte value + 4-byte metadata/pointer + 1-byte type flag), with the WORD read at some fixed offset within each 9-byte entry
2. **Entity state table with 9 fields per entity:** The parameter could be an entity index, and *9 produces the base offset for that entity's set of 9 properties (position X/Y/Z, rotation, animation state, etc.)
3. **18-byte entries with WORD indexing:** If the *9 produces a WORD-aligned index (multiply by 9 then by 2), each entry would be 18 bytes — consistent with an entity slot containing multiple coordinate/state words

**The global WORD at `0x01CE476A`** loaded at handler+0xF0 is likely a **field entity count or field ID** used for bounds checking or as a multiplier in the address computation. Its proximity to the varblock base (`0x1CFE9B8`) in the data segment supports it being a related field engine global.

---

## Why -82 and -8019 break the documented formula

The dic entity's SET3 uses PSHM_W with addresses **135**, **-82** (0xFFAE), and **-8019** (0xE0AD). These parameters expose the handler's branching behavior:

**Address 135** is the only value that makes sense as a standard varblock byte offset, but it falls in the **draw point data region** (bytes 116–179). Reading a coordinate value from draw point storage would be semantically wrong, suggesting even this address is likely processed through the alternate code path.

**Addresses -82 and -8019** are definitively impossible under the standard `varblock + param` formula — negative byte offsets into a fixed-base array would read garbage from unrelated memory. Their validity as PSHM_W parameters proves the handler **must** interpret them differently based on entity context.

Your measured panel position of approximately **(-636, -8626)** does not match any obvious arithmetic transformation of the raw address parameters (135, -82, -8019). This confirms these are memory **addresses** (into some calculated region), not the coordinate **values** themselves. The actual coordinate values live at whatever memory locations the handler resolves these addresses to at runtime.

If the *9 multiplication is in the standard path:
- 135 × 9 = 1215 (in temp variable range, 1024+)
- -82 × 9 = -738 (offset backward from some base)
- -8019 × 9 = -72171 (far backward from some base)

The negative results suggest the base pointer for this access mode might not be the varblock start but rather a **field-specific or entity-table-relative pointer** — perhaps the address of the current field's entity data block, with negative offsets indexing into previously allocated entities or a header region.

---

## The JSM encoding and entity struct layout

Each JSM opcode is **4 bytes** (32 bits). The opcode table contains **184 entries** (0x000–0x183 = 0–387), requiring 9 bits for the opcode key. The parameter occupies the remaining bits. Based on the observed signed 16-bit parameters in your dic entity analysis, the encoding is most likely:

```
Bits [31:16] = opcode key (16 bits, upper 7 always zero for standard opcodes)
Bits [15:0]  = signed parameter (int16, range -32768 to +32767)
```

This interpretation is consistent with the MOVSX (Move with Sign Extension) instruction you observed — the handler extracts the lower 16 bits and sign-extends to 32 bits before the *9 multiplication.

The entity struct layout from your disassembly:

| Offset | Size | Contents |
|--------|------|----------|
| 0x0F0 | WORD* | Pointer to or reference to global at 0x01CE476A |
| 0x140 | DWORD[] | VM stack (array of 32-bit values) |
| 0x160 | DWORD | Execution flags (controls memory access mode) |
| 0x184 | DWORD | Stack pointer (index into stack at 0x140) |

FFNx's `ff8_data.cpp` does not document these internal entity struct offsets. The public codebase focuses on hooking rendering and audio functions, using the field entity system only as a reference point for finding the opcode table and varblock base. The struct layout at this level of detail exists only in private reverse engineering work.

---

## Practical path forward for the accessibility mod

Since the dic entity is beyond the engine's 10-slot active entity window and its scripts never execute, you cannot simply read the result of PSHM_W at those addresses — the handler never runs for that entity. Three approaches to obtain the Directory panel's position:

**Approach 1 — Hook the PSHM_W handler.** Inject a hook at `0x0051C5C0` that logs the parameter, execution flags, and resulting memory read for every call. Then trigger a field reload of the B-Garden map and capture the values. This gives you the exact resolution chain for all three addresses. Even if the dic entity itself doesn't execute, other entities on the same field may use the same addresses, revealing the formula.

**Approach 2 — Emulate the handler in your mod.** Since you have the disassembly, reconstruct the handler logic in C:

```c
// Pseudocode for PSHM_W handler (speculative, needs validation)
int16_t pshm_w_read(entity_state* entity, int16_t param) {
    uint32_t flags = entity->execution_flags;  // offset 0x160
    
    if (flags & 0x10000000) {
        // Alternate path: entity-relative or model-data access
        int32_t offset = (int32_t)param * 9;
        // ... additional base resolution using 0x01CE476A global ...
        return *(int16_t*)(alternate_base + offset + word_offset);
    } else {
        // Standard path: varblock byte offset
        return *(int16_t*)(0x1CFE9B8 + param);
    }
}
```

**Approach 3 — Parse the JSM bytecode directly.** Read the dic entity's init script from the field archive, decode the SET3 instruction sequence, and trace which PSHM_W addresses it uses. Then use a Cheat Engine-style memory scan to find the varblock values at those resolved addresses. The FFNx-provided `field_vars_stack_1CFE9B8` global gives you the base, and the `execute_opcode_table` gives you the handler addresses for validation.

For the immediate need, **Approach 1 is fastest**: set a conditional breakpoint at `0x0051C5C0`, filter for param values 135/-82/-8019, and read the return value. The resulting coordinates should match your measured (-636, -8626) position, confirming the resolution formula.

---

## Conclusion

The documented PSHM_W behavior — a simple byte-offset read from the varblock at `0x1CFE9B8` — covers only the standard code path. Your disassembly has uncovered a second path controlled by entity execution flags at struct offset 0x160, involving sign-extension and a *9 multiplication that indexes into a different memory layout entirely. No public documentation, source code, or community wiki describes this alternate path. The negative PSHM_W addresses (-82, -8019) used by the dic entity's SET3 instruction are proof that this undocumented path exists and is the one your mod needs to replicate.

The key unknowns that require runtime verification are: what base pointer does the alternate path use, what additional offset is applied after the *9 multiplication, and whether the global at `0x01CE476A` participates in the address calculation. A single debugging session with a breakpoint on the handler — even using another entity that reads from these same addresses — should resolve all three questions and give you the formula needed to read the Directory panel's position directly from memory.
