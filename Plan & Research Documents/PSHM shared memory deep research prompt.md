# Deep Research Request: FF8 JSM Shared Memory Table (PSHM_W / POPM_W)

## Give this entire prompt to ChatGPT deep research.

---

## Question

In Final Fantasy VIII (PC Steam 2013 edition, running via FFNx v1.23.x), how does the JSM scripting engine's shared memory system work — specifically the PSHM_W (push shared memory word) and POPM_W (pop/write shared memory word) opcodes?

I need to know:

1. **Where is the shared memory table stored in RAM?** Is it a fixed global array, or is it allocated per-field? What is the base address resolution chain from the game executable?

2. **How are PSHM_W addresses mapped to memory offsets?** The JSM instruction `PSHM_W addr` pushes a 16-bit (WORD) value from shared memory onto the VM stack. What is the formula: `value = *(int16_t*)(base + addr * 2)`? Or is there a more complex mapping involving entity-relative offsets, page/bank switching, or address ranges?

3. **What address ranges exist?** FF8's PSHM/POPM system appears to have multiple memory banks based on bit flags in the address:
   - Addresses 0-1023 seem to be "normal" shared variables
   - Addresses 1024+ seem to be entity-local or camera-related
   - The PSHM_W handler at 0x0051C5C0 (US NV/Steam) checks bit 28 (0x10000000) and bit 16 (0x00010000) of the execution flags to branch between different memory access modes

4. **Is there a global pointer we can read at runtime?** Specifically, I have a field entity "dic" (the B-Garden Directory panel) whose SET3 opcode uses PSHM_W addresses X=135, Y=0xFFAE (signed: -82), Z=0xE0AD (signed: -8019) to load its position from shared memory. These are the PSHM_W address parameters, not the actual coordinate values. I need to read the actual values these addresses resolve to at runtime.

5. **What does the Qhimm wiki / Deling source / makou reactor source say about the temp/shared memory layout?** The Qhimm wiki has documentation on FF8's field scripting. Deling (myst6re/deling on GitHub) is a field editor. Makou Reactor and Hyne are also FF8 tools that parse JSM scripts.

## Context

### What we've already found

The PSHM_W opcode handler is at `0x0051C5C0` (FF8 1.2 US NV/Steam). Disassembly of the first 256 bytes:

```
+00: PUSH EBX, EBP, ESI
+03: MOV ESI, [ESP+10]      ; ESI = entity state pointer
+07: PUSH EDI
+08: MOV ECX, 8             ; loop count
+0D: LEA EBP, [ESI+0x140]   ; EBP = entity VM stack base (offset 0x140 in entity struct)
+13: MOV EAX, EBP
+15: MOVSX EDX, BYTE [ESI+0x184]  ; stack pointer index
+1C: ADD EAX, 4
+1F: MOV [EAX-4], EDX       ; copy stack entries (8-iteration loop)
     ... (stack manipulation loop) ...
+38: MOV EBX, [ESI+0x160]   ; EBX = execution_flags from entity struct
+3E: XOR ECX, ECX
+40: MOV CL, [ESI+0x174]    ; some control byte
     ... (flag processing, clears bit 0x20 in flags) ...
+62: MOV ECX, EBX
+64: SUB ECX, 8
+67: JZ +269 (absolute)      ; branch if param_type == 8
+6D: DEC ECX
+6E: JZ +24E                 ; branch if param_type == 7
+74: TEST EAX, 0x10000000    ; bit 28 check
+7D: JZ +1B8                 ; branch if bit 28 clear
+83: TEST EAX, 0x00010000    ; bit 16 check
+8A: JZ +1AD                 ; branch if bit 16 clear
     ... (entity state field copies at +8C to +E9) ...
+F0: MOVSX ECX, WORD [0x01CE476A]  ; ← GLOBAL ADDRESS (field-level variable?)
+F7: MOVSX EDX, DX           ; sign-extend address param
+FA: LEA EAX, [ECX+ECX*8]   ; EAX = value * 9
     ... (continues beyond 256-byte dump) ...
```

Key observations:
- The handler receives the entity state pointer as its argument (same calling convention as all JSM opcode handlers)
- The VM stack is at entity struct offset 0x140, stack pointer at 0x184
- Execution flags at entity struct offset 0x160 control which memory access path is taken
- At +F0, a global WORD at `0x01CE476A` is loaded — this might be a field ID or memory page selector
- The address parameter appears to be sign-extended (MOVSX) and multiplied (LEA ECX+ECX*8 = *9), suggesting the shared memory table has 9-byte or 18-byte entries (if the address is further scaled)

### What we need

We're building an accessibility mod for blind players. The B-Garden Directory panel ("dic" / "igyous1") is an interactive object whose position is stored in shared memory variables. The entity's init script calls SET3 with PSHM_W-sourced coordinates, but the entity itself is beyond the 10-slot active entity window in pFieldStateOthers, so the engine never executes its scripts. We need to read the shared memory values directly to get its position.

The PSHM_W address values from dic's SET3 instruction are:
- X address: 135
- Y address: -82 (0xFFAE as signed int16)  
- Z address: -8019 (0xE0AD as signed int16)

The player-measured position of the Directory panel is approximately (-636, -8626) in world coordinates.

### Reference sources to check

1. **Qhimm wiki** — FF8 field script documentation, opcode reference, memory model
2. **Deling source** (github.com/myst6re/deling) — FF8 field editor, JsmExpression.cpp, JsmOpcode.cpp
3. **Makou Reactor** — FF8 script viewer/editor
4. **Hyne** — FF8 save editor (may reference memory layout)
5. **FFNx source** (github.com/julianxhokaxhiu/FFNx) — ff8.h has entity struct layouts, ff8_data.cpp has address resolution
6. **Garden wiki / FF8 modding community** — any documentation on the PSHM/POPM variable system

### Specific questions for the research

1. What is the base address of FF8's shared/temp memory table on the Steam 2013 PC version?
2. How does PSHM_W translate an address parameter into a memory read? Is it `base + addr * 2` (WORD array) or something more complex?
3. Are negative addresses (like -82, -8019) valid? Do they index into a different region?
4. What is the global at `0x01CE476A` — is it a field ID, memory bank selector, or entity count?
5. Is there existing documentation or source code that shows the complete PSHM_W implementation?
