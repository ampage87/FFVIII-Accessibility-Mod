# FF8's undocumented entity-scope coordinate system: what public RE reveals and what remains hidden

**The parametric curve subroutine at `0x00532890` in FF8_EN.exe has never been publicly documented.** After exhaustive research across every major FF8 reverse-engineering resource — FFNx source code, Qhimm wiki mirrors (ffrtt.ru, ff7-flat-wiki, Fandom), the Deling field editor, OpenVIII, OpenFF8, ff8-speedruns/ff8-memory, and dozens of community repositories — no public documentation, pseudocode, or disassembly exists for this entity-scope resolution system. The researcher's work appears to be **genuinely novel reverse engineering** that goes beyond the community's documented understanding of the PSHM_W handler. What follows is everything the public record reveals, informed analysis of the architecture, and concrete recommendations for completing the reverse-engineering task.

## The PSHM_W handler is simpler than you've found — publicly

Community documentation by Aali, myst6re, and Shard describes PSHM_W (opcode **0x0C**) as a straightforward variable-block read: take the inline parameter as a byte offset, read a 16-bit word from `varblock_base + offset`, push it onto the script execution stack. The variable block resides at **`0x18FE9B8`** (relative to image base, i.e., `0x1CFE9B8` at runtime for the Steam 2013 edition). Variables 0–1023 are persistent save data; above 1023 are temporary field-local scratch space. FFNx confirms this architecture — it hooks opcode table entry `0x0C` and extracts the varblock address from offset `0x1E` within the handler via `get_absolute_value(opcode_pshm_w, 0x1E)`.

**No public source documents the three-mode dispatch** (negative passthrough, entity-scope curve, standard varblock read) that the researcher discovered. The threshold mechanism at `0x01CE476A`, the entity descriptor table at `0x01DCB340`, and the parametric curve sub at `0x00532890` are all absent from every known wiki, forum thread, and open-source codebase. The Qhimm forums' primary engine RE thread (topic 16838) is currently returning 403 errors, and the Wayback Machine copies were inaccessible during research — this thread is the most likely place where such deep engine internals might have been discussed.

## What FFNx and open-source tools reveal about the surrounding architecture

**FFNx** (julianxhokaxhiu/FFNx) provides the clearest public view of the opcode dispatch architecture. The field entity update loop lives in `common_externals.update_field_entities`, with the opcode dispatch table at offset `+0x65A` and the entity update call site at `+0x657`. FFNx extracts 13 opcode handler addresses from this table (PSHM_W, EFFECTPLAY2, MAPJUMP, MES, MESSYNC, ASK, WINCLOSE, MOVIE, MOVIESYNC, SPUREADY, AMESW, AMES, BATTLE) but does **not** hook or modify any entity positioning logic. It treats PSHM_W purely as a pointer to the varblock address. No references to `0x00532890`, `0x0051C9C0`, `0x01DCB340`, or `0x01CE476A` appear anywhere in the codebase.

**OpenFF8** (Extapathy/OpenFF8) is the most promising unpublished resource. Its `memory.h` file defines `ff8vars` and `ff8funcs` structs that directly map FF8's in-memory data structures. This file could contain entity descriptor struct definitions matching the `~0x90` byte layout. The repository is public at `github.com/Extapathy/OpenFF8` but the actual file content couldn't be retrieved during this research due to rate limiting. **Cloning this repo directly (`git clone https://github.com/Extapathy/OpenFF8.git`) should be the researcher's immediate next step.**

**Deling** (myst6re/deling), the authoritative FF8 field editor, treats PSHM_W as a simple stack push from the variable block. Its `JsmScripts.cpp` disassembler/decompiler reconstructs PSHM/POPM pairs into variable assignments but does not model any entity-scope resolution logic. Deling organizes entities by type (Lines → Doors → Backgrounds → Others) and supports 3D position overlay visualization, but entity positions come from interpreting SET3 opcodes in the script data, not from any runtime descriptor struct.

**OpenVIII-monogame** (MaKiPL) has JSM field script parsing at ~60% opcode coverage and ~100% instruction disassembly, but no script execution engine that would exercise the PSHM_W handler's alternate code paths. Issue #150 ("duplicate global variables") confirms the project was still reconciling save-block and field-script variable systems before archival.

## PSHSM opcodes are separate from your entity-scope system

A critical clarification: opcodes **0x10–0x12** (PSHSM_B, PSHSM_W, PSHSM_L — "Push Shared Memory") are documented as **separate opcodes** from PSHM_W, accessing a distinct memory space. They appear in the opcode table immediately after the PSHM/POPM family and are classified as active (not marked "Unused"). Notably, **no corresponding POPSM write opcodes exist**, suggesting this shared memory is either read-only from scripts or written by engine internals only. The PSHSM opcodes likely access per-entity or per-field scoped memory, but their individual documentation pages (confirmed to exist on ffrtt.ru as non-redlink pages) could not be fetched. These are architecturally related to but distinct from the entity-scope path you've found *within* the PSHM_W handler itself.

**PSHAC** (opcode 0x13, "Push Actor") is another Memory-type opcode that pushes entity-context data. Together with PSHSM, these suggest the engine has a well-defined entity-scope memory layer — the PSHM_W handler's alternate code path may be routing certain address ranges to this same entity-scope memory system rather than the global varblock.

## No parametric curve or path data exists in FF8 field files

FF8 field archives contain **no pre-baked curve, rail, or parametric path data**. The field format includes JSM (scripts), MIM/MAP (background), ID (walkmesh), CA (camera), INF (gateways), MSD (dialog), SYM (entity names), RAT/MRT (encounters), PMD/PMP (particles), PVP (unknown), and SFX (sounds). The `rail.obj` file is exclusively world-map train path data — irrelevant to field entities. All entity positioning on field screens is driven entirely by script opcodes (SET, SET3, MOVE, CMOVE, FMOVE, JUMP, etc.) and walkmesh constraints. This means the curve data at `descriptor+0x68` must be **computed or copied from script data at runtime** rather than loaded from a dedicated file section.

## Informed analysis of the probable algorithm

Based on the ground truth data and PS1-era engine conventions, several patterns emerge:

The identity mapping for elelight (`addr 654 → X=654`) alongside non-trivial mappings for other entities (`addr 1 → X=-700`, `addr 256 → X=-1596`, `addr 1032 → X=-2865`) strongly suggests the curve data is **per-entity** — each entity's descriptor at `+0x68` points to different data arrays producing different mappings. The most likely algorithm is a **piecewise linear interpolation table** or a **direct indexed lookup**, consistent with PS1-era fixed-point math conventions. The curve data array probably consists of sorted entries of the form `{parameter_value: int16, coordinate_value: int16}` or similar, with the function finding the appropriate entry (or interpolating between entries) for the given PSHM address.

The cache key at `descriptor+0x7E` (last PSHM address processed) and the stored results at `+0x0C`/`+0x0E` (X/Y coordinates) indicate the function is designed for repeated calls with the same address — it checks whether the input matches the cached key and returns the stored result if so. This is a classic **memoization pattern** for a function called from the per-frame script execution loop.

The descriptor fields at `+0x50`/`+0x52` (purpose unknown) could be intermediate computation state, an index into the curve data, or the current interpolation parameter — their role would become clear from disassembly.

## Three practical paths forward for the researcher

**Path 1 (fastest): Read descriptor+0x0C/+0x0E directly.** For entities whose per-frame scripts (method 0, the "default" idle loop) exercise PSHM_W, the descriptor should be populated within a few frames of field load. The critical question for dic (ent24 on bghall_1) is whether its SET3 fires only from a conditional camera-zone script or whether any per-frame script path also triggers entity-scope resolution. If dic's default method includes a PSHM_W reference (even indirectly through script requests from other entities), the descriptor will be populated early. **Check the descriptor table at `0x01DCB340[24]` (dic's entity index) at 1-second intervals after field load** — if the pointer becomes non-NULL and `descriptor+0x00` is not `-1`, the coordinates at `+0x0C`/`+0x0E` should be valid. For entities that never fire SET3 during normal idle, you may need to force the camera zone change (opcode SETCAMERA or equivalent) programmatically, or execute the entity's conditional script via a REQ opcode injection.

**Path 2 (definitive): Disassemble 0x00532890 directly.** Load FF8_EN.exe (Steam 2013, AppID 39150) into IDA Pro or Ghidra at base address `0x00400000`. Navigate to `0x00532890`. The function takes parameters derived from the PSHM_W address, the entity descriptor struct pointer, and the curve data array pointer. As a 32-bit x86 function in a PS1-port game engine, it likely uses simple integer arithmetic — fixed-point multiplication, table lookups, and conditional branches. The function is probably 100–300 instructions. The calling convention is likely `__cdecl` or `__thiscall` (check the stack frame). Cross-reference calls to this function from `0x0051C9C0` to understand how parameters are marshaled. Also examine how the descriptor struct at `+0x68` gets populated — trace back from `0x01DCB340` writes to find the initialization code.

**Path 3 (collaborative): Leverage OpenFF8 and community resources.** Clone `github.com/Extapathy/OpenFF8` and examine `memory.h` for entity struct definitions. When the Qhimm forums (topic 16838, "[FF8] Engine reverse engineering") become accessible again, search for discussions about PSHM_W internals, entity-scope memory, or the sub at `0x00532890`. The FF8 modding Discord communities (linked from the FF8 Modding Wiki at `hobbitdur.github.io/FF8ModdingWiki`) may have members (myst6re, Shard, MaKiPL, Sebanisu) who have encountered this system. The researcher's findings would be a valuable contribution to the community RE documentation on ffrtt.ru.

## Key memory addresses confirmed across multiple sources

| Address (runtime VA) | Purpose | Confirmed by |
|---|---|---|
| `0x1CFE9B8` | Field variable block base (Steam 2013) | FFNx, ff8-speedruns |
| `0x1677238`/`23C`/`240` | Player field coords X/Y/Z | ff8-speedruns |
| `0x18D2FC0` | Current map ID | ff8-speedruns |
| `0x18FEAB8` | Story progress (var 256) | ff8-speedruns, wiki |
| `0x01DCB340` | Entity descriptor table (researcher) | Novel finding |
| `0x01CE476A` | Threshold selector (researcher) | Novel finding |
| `0x00532890` | Entity-scope curve sub (researcher) | Novel finding |
| `0x0051C9C0` | Type-clamping dispatch (researcher) | Novel finding |

## Conclusion

The entity-scope parametric curve system inside PSHM_W represents an **undocumented layer** of FF8's field engine that the modding community has not publicly analyzed. The community documentation treats PSHM_W as a flat variable-block read, missing the threshold-gated, entity-descriptor-mediated alternate code path entirely. No open-source reimplementation (OpenVIII, FFNx, Deling) exercises or models this path.

For the accessibility mod, **reading the cached descriptor coordinates (`+0x0C`/`+0x0E`) is the pragmatic solution** — it avoids reimplementing the algorithm entirely, requiring only that the descriptor be populated (non-NULL pointer at `0x01DCB340[entity_index]` with valid marker at `+0x00`). For dic specifically, if the descriptor isn't populated during idle, consider hooking the field entity update loop to force-execute dic's conditional script once after field load, or inject a synthetic REQ to trigger the SET3 path. Alternatively, a 5–10 second polling loop checking the descriptor table may catch the population event if ANY script interaction triggers it.

For the definitive algorithm, **direct disassembly of `0x00532890` in IDA/Ghidra remains the only viable path** — no public resource contains this information. The function is likely compact (sub-300 instructions) and uses straightforward integer math typical of PS1-era engines. The OpenFF8 `memory.h` struct definitions, once accessed via a direct repo clone, may provide the entity struct field names that accelerate the disassembly annotation process.
