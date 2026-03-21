# Deep Research Request: Force FF8 Entity Script Execution for Inactive Entities

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe, 32-bit x86). The mod is a Win32 DLL (`dinput8.dll`) injected via DirectInput proxy, with FFNx v1.23.x installed as a graphics/compatibility layer.

## The Problem

FF8's field engine has a **10-slot active entity window**. Each field can define up to 31 "Other" entities in JSM scripts, but only the first 10 get entity state structs allocated in the `pFieldStateOthers` array (stride 0x264 per entity). Entities at index 10+ never have their scripts executed — the engine simply skips them.

Interactive objects like the B-Garden Directory panel (`dic`, othersIdx=12) and its dialog entity (`igyous1`, othersIdx=13) fall outside this window. Their JSM init scripts contain `SET3` opcodes that would position them in the game world, but since the engine never runs those scripts, the positions are never resolved.

We need to **force the engine to execute the init scripts** for these inactive entities so that the `SET3` opcode fires and writes real coordinates to an entity state struct we can read.

## What We've Already Tried (All Failed)

1. **SET3 opcode hook (persistent)** — hooks `pExecuteOpcodeTable[0x1E]`, captures every SET3 call for the entire lifetime of the field. Only catches the 10 active entities. dic never fires SET3 because its scripts never run.

2. **Direct entity state read** — entities beyond index 9 have no allocated state struct. Reading memory at `base + ENTITY_STRIDE * 12` returns garbage/zeros.

3. **PSHM_W varblock formula** — `*(int16_t*)(0x1CFE9B8 + param)` doesn't work for dic's addresses (135, -82, -8019). All are below the entity-scope threshold (~2696) and take an alternate code path requiring a per-entity descriptor struct that's never allocated.

4. **Descriptor table polling** — `0x01DCB340` is a per-entity pointer table. dic's entry is always NULL because the engine never allocates its descriptor. Polled for 10 seconds after field load — stays NULL.

5. **Extended SET3 capture window (3 seconds)** — Walking NPCs show up, but dic never does.

6. **Proximity-based active window swap** — Tested by walking to the Directory panel area. No new SET3 calls. The active window is fixed at field load.

7. **Parametric curve formula** — The entity-scope subroutine at `0x00532890` requires descriptor data that's only populated when the engine runs the entity's scripts. Dead end for the same reason.

## What I Want to Do

**Force the FF8 engine to execute a specific entity's JSM init method (method 0) using a temporarily allocated entity state struct.** After execution, read the position from the struct (offsets 0x190/0x194 for fixed-point X/Y), then discard the struct.

## What I Know About the Script Execution System

### Entity State Struct (`ff8_field_state_other`, stride 0x264)
- `+0x000`: VM stack (320 bytes, `uint8_t[320]`)
- `+0x140`: Instruction pointer / script execution state
- `+0x160`: Execution flags (uint32, controls PSHM_W code path selection)
- `+0x184`: Stack pointer (`uint8_t`, NOT uint32)
- `+0x188`: SETLINE data area
- `+0x190`: Fixed-point X position (int32, ×4096)
- `+0x194`: Fixed-point Y position (int32, ×4096)
- `+0x198`: Fixed-point Z position (int32, ×4096)
- `+0x1F8`: Talk radius (uint16)
- `+0x1FA`: Current walkmesh triangle ID (uint16)
- `+0x218`: Model ID (int16)
- `+0x255`: `setpc` flag (uint8, 0 = player)

### JSM Script System
- Scripts are stored in the `.jsm` section of the field archive
- Each entity has a group entry: bits 0-6 = number of methods, bits 7-15 = start instruction index
- Method 0 is the init method, executed during `field_scripts_init`
- Opcodes are 32-bit LE: high byte = opcode, low 24 bits = signed parameter
- The JSM bytecode is loaded into memory during field_scripts_init

### Key Game Functions
- `field_scripts_init` at `0x00471080` — initializes all field scripts, allocates entity states, runs init methods
- `pExecuteOpcodeTable` — dispatch table of opcode handler function pointers. Each handler has signature `int __cdecl handler(int entityStatePtr)`.
- `SET3` = opcode table index `0x1E`. Pops X, Y, Z from entity's VM stack and writes position to entity state struct.
- `PSHM_W` = opcode table index `0x06`. Pushes a value from field memory onto the entity's VM stack. Has two code paths: standard varblock read and entity-scope parametric curve.

### FFNx Source References
- `src/ff8.h` — entity struct definitions (`ff8_field_state_other`, `ff8_field_state_common`)
- `src/ff8_data.cpp` — resolved function addresses and struct layouts
- `src/ff8_field.cpp` — field subsystem logic

### What field_scripts_init Does (from observation)
1. Parses the JSM header to get entity counts per category (doors, lines, backgrounds, others)
2. Allocates entity state arrays (`pFieldStateOthers` for others, `pFieldStateBackgrounds` for bg entities)
3. For each entity in the "others" category (up to `entCount`, typically 10):
   a. Initializes the entity state struct (zeroes, sets execution flags)
   b. Sets the instruction pointer to the entity's method 0 start address
   c. Runs the script interpreter until the method returns or yields
4. Entities beyond `entCount` are skipped entirely — no allocation, no init

## What I Need From Research

### Primary Question
**How can I safely call the FF8 script interpreter to execute a single entity's init method on a temporary entity state struct?**

Specifically:
1. **What function is the per-entity script interpreter?** Is there a function like `execute_entity_script(entityStatePtr)` that runs one entity's current method until it returns? Or does `field_scripts_init` have an inner loop function I can call?

2. **What state must be set up in the entity struct before calling it?** Beyond zeroing the struct, what fields must be initialized? The instruction pointer obviously needs to point to the entity's method 0 bytecodes. What else?

3. **What global state does the script interpreter read?** Does it reference `pFieldStateOthers` to find the current entity? If so, can I temporarily swap the pointer or append to the array? Does it use a "current entity index" global?

4. **What side effects might method 0 execution have?** Could running dic's init script trigger model loading, animation, sound effects, or other visible side effects? Can these be suppressed?

5. **Is there a simpler "evaluate expression" function?** Instead of running the full init script, can I just evaluate the three PSHM_W addresses (135, -82, -8019) through the engine's PSHM_W handler with a properly initialized entity context?

### Secondary Questions

6. **Where is the entity-scope data loaded from?** The parametric curve at `0x00532890` reads from descriptor+0x68 (curve data pointer). Where does this data originate? Is it loaded from the field archive, or computed from the JSM bytecodes? If it's from the archive, I could load it myself without executing scripts.

7. **What is the structure of the 9×16-byte (0x90) descriptor at `0x01DCB340[entityIndex]`?** The deep research identified it as allocated on-demand. What function allocates it? What populates descriptor+0x68 with curve data? Is this done during init or only when PSHM_W is called for the first time?

8. **Can I call the PSHM_W handler directly with a fake entity context?** If I allocate a 0x264-byte struct, set the execution flags at +0x160 appropriately, set the stack pointer, push the address parameter, and call `pExecuteOpcodeTable[0x06](entityPtr)` — would it resolve the address and push the result to the VM stack? What other fields must be valid?

9. **What is the "entity index" that PSHM_W uses for the descriptor table lookup?** Is it derived from the entity state pointer's offset from the base of `pFieldStateOthers`, or is it stored somewhere in the entity struct? This matters because a temporary struct at an arbitrary address would compute the wrong index.

## Key Addresses (Steam 2013 en-US, FF8_EN.exe, base 0x00400000)

| Address | Description |
|---------|-------------|
| `0x00471080` | `field_scripts_init` |
| `0x0045E160` | `set_current_triangle` |
| `0x004685F0` | `get_key_state` |
| `0x00401F60` | `engine_eval_keyboard_gamepad_input` |
| `0x0051C5C0` | PSHM_W handler (original game code) |
| `0x00532890` | Entity-scope parametric curve subroutine |
| `0x0051C9C0` | Type-clamping dispatch (caller of 0x00532890) |
| `0x0051CAF0` | POPM_W handler |
| `0x01CFE9B8` | Varblock base (field variable stack) |
| `0x01DCB340` | Per-entity descriptor pointer table |
| `0x01CE476A` | Global threshold selector (WORD, =20 on bghall_1) |

The `pExecuteOpcodeTable` is a table of function pointers resolved at runtime by FFNx. Opcode indices: SET3=0x1E, PSHM_W=0x06, PSHN_L=0x00, POPM_W=0x09.

## Additional Context

- **FFNx hooks some opcode handlers** — it replaces dispatch table entries with its own functions. Our PSHM_W dispatch table hook (writing directly to `pExecuteOpcodeTable[0x06]`) works but MinHook on the same handler crashes due to FFNx conflict.
- **The entity struct's VM stack is small** — 320 bytes at offset 0x000, with a `uint8_t` stack pointer at 0x184. Previous crash was caused by reading the stack pointer as `uint32_t` (3 garbage bytes).
- **dic's JSM init method** pushes 3 values via PSHM_W then calls SET3. The PSHM_W addresses are (135, -82, -8019). Address 135 is positive but below the entity-scope threshold, so it takes the alternate code path.

## Search Guidance

Relevant open-source projects that may have documentation:
- **FFNx** (github.com/julianxhokaxhiu/FFNx) — `src/ff8.h`, `src/ff8_data.cpp`, `src/ff8_field.cpp`
- **OpenFF8** (github.com/Extapathy/OpenFF8) — `memory.h` with entity struct definitions
- **Deling** (github.com/myst6re/deling) — FF8 field editor, JSM parser
- **Qhimm Wiki / FFRTT** — FF8 engine documentation
- **ff8-speedruns/ff8-memory** — Cheat Engine tables with memory addresses

## Desired Output Format

1. The most promising approach to force-execute dic's init method, with specific function addresses and calling conventions
2. The minimum entity state struct initialization required before calling the script interpreter
3. Any global state that needs to be temporarily modified (and restored after)
4. Risk assessment: what could crash or produce visible side effects
5. If force-execution is too risky, an alternative path to resolve PSHM_W entity-scope addresses without the engine's help
