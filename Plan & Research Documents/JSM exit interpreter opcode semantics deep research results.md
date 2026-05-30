# JSM exit interpreter opcode semantics — deep research RESULTS

> Results for the prompt in `JSM exit interpreter opcode semantics deep research prompt.md`
> (Bug 4 — dormitory/corridor exit-destination resolver / sandbox JSM interpreter).
> Received 2026-05-29. Independently corroborates the v0.17.9.3 `[OPDUMP]` + in-container
> capstone disassembly of the live FF8_EN.exe opcode handlers. See the cross-check notes
> Claude appended at the bottom of this file.

---

# FF8 Field-Script (JSM) Interpreter: Opcode Semantics for Forward MAPJUMP3 Resolution

## TL;DR
- **Your high-byte decoder is correct AND is identical to the canonical Deling/Makou Reactor opcode numbering** — there is no separate renumbering to reconcile: opcode = word>>24, param = sign-extended low 24 bits, high byte 0x00 = PSHN_L literal. Canonical names: 0x01=CAL, 0x02=JMP, 0x03=JPF, 0x07=PSHN_L, 0x0C=PSHM_W, 0x11=PSHSM_W, 0x29=MAPJUMP, 0x2A=MAPJUMP3.
- **The crux is a mislabel: the "JMP" gating each MAPJUMP3 block is actually JPF (0x03)** — a conditional jump that POPS one value and jumps forward only when that value == 0 (false). At the field-228 boundary the condition was true (nonzero), so JPF fell through and control reached the five immediate pushes and the first MAPJUMP3.
- **PSHSM_W/PSHM_W 0x0100 reads field variable word 256 = the "game moment" / main story-progress counter** (FFNx names it `field_game_moment = field_vars_stack + 0x100`); the SeeD-room destination is selected by comparing this counter against a story threshold.

## Key Findings

### 1. Decoder model confirmation — the two decoders are not in conflict
The high-byte model is authoritative. FFNx's runtime hook indexes the engine's live dispatch table by the canonical opcode number — verbatim from FFNx `src/ff8_data.cpp` (julianxhokaxhiu/FFNx, master): `execute_opcode_table[0x0C]` = PSHM_W, `[0x0D]` = POPM_W, `[0x0B]` = POPM_B, `[0x21]` = EFFECTPLAY2, `[0x29]` = MAPJUMP, `[0x47]` = MES. These indices match the canonical FF8/Field/Script/Opcodes table (authored "By Aali, myst6re and Shard"), which lists exactly: 000 NOP, 001 CAL, 002 JMP, 003 JPF, 005 LBL, 006 RET, 007 PSHN_L, 008 PSHI_L, 009 POPI_L, 00A PSHM_B, 00B POPM_B, 00C PSHM_W, 00D POPM_W, 011 PSHSM_W, 029 MAPJUMP, 02A MAPJUMP3.

Therefore your "high byte = opcode" IS the canonical Deling/Makou Reactor numbering. The Deling "DecodeJSMInstruction (bit31 flag, no inline params)" form you saw is an internal editor serialization detail; the actual FF8_EN.exe interpreter dispatches on the high byte and reads the signed 24-bit inline parameter, exactly as you confirmed empirically.

### 2. The jump family (your 0x01 / 0x02 / 0x03)
- **0x01 CAL ("Calculate") — NOT a jump.** Pops value2 then value1, applies the operation named by its inline 1-byte sub-operand, pushes one result. Sub-ops (from the canonical 001_CAL page): `000 ADD, 001 SUB, 002 MUL, 003 DIV, 004 MOD, 005 MIN, 006 EQ, 007 GT, 008 GE, 009 LS (less), 00A LE, 00B NT (not-equal), 00C AND, 00D OR, 00E EOR` (plus unnamed 00F NOT, 010 RSH, 011 LSH). **This is the only comparison/test mechanism — there is no standalone EQU/NEQ/GT opcode.** Stack effect: pop 2, push 1. 0x01 cannot itself branch.
- **0x02 JMP ("Jump") — unconditional.** Stack: "No change" (does not pop). Inline param = signed number of instructions to jump; negative jumps backward.
- **0x03 JPF ("Jump Forward with condition") — conditional.** Pops exactly one value (the condition) and jumps forward by the inline param ONLY when the popped value == 0. Otherwise it does nothing and falls through. The condition is always popped. Verbatim from the canonical wiki worked example: *"JPF LABEL1 # if the popped top of the stack is 0, jump to LABEL1 (stack = [])"*.

A compare-and-branch is therefore always a two-opcode pair — verbatim canonical idiom: *"…CAL EQ # compare the two numbers… push the result (1 or 0)… JPF LABEL1 # if the popped top of the stack is 0, jump to LABEL1."*

### 3. The push/pop family
All PSHM/PSHSM/POPM opcodes address the **same** field variable block (the savemap-backed global/"special" variable region: base 0xD10 in an uncompressed PC save; runtime base 0x1CFE9B8 in the Steam 2013 build per FFNx; older en-US/SE builds 0x18FE9B8 — the FF8/Variables wiki states *"the varblock begins at 0x18fe9b8"*). The opcode distinctions are **size and signedness, not different banks**:

| High-byte | Canonical | Push/Pop | Source | Stack effect | Operand meaning |
|---|---|---|---|---|---|
| 0x00 | (PSHN_L literal) | push | immediate | push 1 | whole 32-bit word is the literal |
| 0x01 | CAL | — | (arith/compare) | pop 2, push 1 | inline = operation selector |
| 0x02 | JMP | — | — | none | signed instruction offset (unconditional) |
| 0x03 | JPF | — | pops cond | pop 1 | signed forward offset; jump if popped==0 |
| 0x05 | LBL | — | — | none | label marker (no-op at runtime) |
| 0x06 | RET | — | — | none | end of method |
| 0x07 | PSHN_L | push | **immediate** | push 1 | inline value pushed **verbatim** (incl. negatives) |
| 0x08 | PSHI_L | push | memory (indexed) | push 1 | indirect long |
| 0x09 | POPI_L | pop | memory (indexed) | pop 1 | indirect store |
| 0x0A | PSHM_B | push | memory | push 1 | **unsigned byte** at offset=param |
| 0x0B | POPM_B | pop | memory | pop 1 | store byte at offset=param |
| 0x0C | PSHM_W | push | memory | push 1 | **unsigned word** at offset=param |
| 0x0D | POPM_W | pop | memory | pop 1 | store word at offset=param |
| 0x11 | PSHSM_W | push | memory | push 1 | **signed word** at offset=param |
| 0x29 | MAPJUMP | field jump | pops args | — | map jump (no Z) |
| 0x2A | MAPJUMP3 | field jump | **pop 5** | pop 5 | field exit (see below) |

**Two corrections to your labeling:**
- **0x07 PSHN_L pushes its operand as an immediate, NOT a memory read** — confirmed. The canonical page says simply *"Push Argument onto the stack."* This is why the five MAPJUMP3 args equal the operands verbatim including −10 and −39.
- **Your "0x0C = PSHSM_W" is off by name.** Canonically **0x0C = PSHM_W (unsigned word)** and **0x11 = PSHSM_W (signed word)**. The "S" means SIGNED (sign-extended read), not "special bank." Both read the same variable block; for offset 0x100 with small positive story values they return the same number, so this does not affect your trace.

### 4. PSHSM_W / PSHM_W 0x0100 identification
Offset 0x100 (word 256) is the **main story-progress / "game moment" counter** in the global field-variable bank. Verbatim from FFNx `src/ff8_data.cpp`: `common_externals.field_game_moment = (WORD*)(ff8_externals.field_vars_stack_1CFE9B8 + 0x100); //0x1CFEAB8`. Corroborated verbatim by the FF8/Variables wiki: *"getting main story progress (word 256, which is word 0x100 in hex) just gets the two bytes starting at address 0xD10 + 0x100 = 0xE10."*

This is a global story-progress value (distinct from the SeeD-rank byte). The bgroad_5 SeeD-room gate works because the story milestone of becoming SeeD advances `game_moment` past the compared threshold, flipping which `CAL`/`JPF` branch falls through — selecting field 237 (pre-SeeD, B-Garden Dormitory Double 1) before the milestone and 245 (post-SeeD, single SeeD room bgryo2_1) after. The "PSHSM_W 0x0100; PSHM_W 177; JMP" triple you observed is the compiled head of one such conditional (push game_moment, push the comparison constant, then the CAL/JPF that the disassembler mislabeled).

## Details

### Jump-offset encoding
The inline parameter is a **signed count of INSTRUCTIONS (dwords), not bytes** — each JSM instruction is exactly 4 bytes ("Each opcode is 4 bytes"). JMP with a negative param jumps backward; JPF jumps forward by its (positive) count. The exact base (whether `target = jump_index + param` or `next_index + param`, i.e. any "+1") could not be confirmed verbatim from Deling's source because GitHub blocked automated source-file fetches. Calibrate it once against a known resolved arrow in your own dump — e.g. your `3765 JMP -> 3772` implies the param resolves to +7 from the jump index (or +6 from the next index). Implement `target = jump_index + k + sign_extend(param)` and choose the single constant `k` that reproduces all your already-resolved arrows; JMP and JPF share the same convention.

JSM method/entry-point framing (relevant to your IP↔index base offset of 792): each entry point is 2 bytes, `position = (entryPointScript & 0x7FFF) * 4; flag = entryPointScript >> 15`, with position relative to `offsetScriptData`. This is why the engine's live "firing IP" and your scriptData dword index differ by a fixed base — use the operand fingerprint, not the IP, as the cross-reference (as you already do).

### The crux trace (bgryo2_1 method), reframed in canonical opcodes
The compiler emits, per conditional field exit: *push operands → CAL <relop> → JPF (skip the block if false) → five PSHN_L → MAPJUMP3 → JMP (to method end)*. The "JMP -> 3764" you decoded at index 3754 is actually **JPF (high byte 0x03)**: a conditional forward jump that POPS the comparison boolean and jumps over the block ONLY if that boolean is 0. At this boundary the game-moment comparison for the field-228 branch evaluated **true (nonzero)**, so JPF did **not** take its forward jump and execution fell straight through:

- 3756 PSHN_L 228 → stack `[228]`
- 3757 PSHN_L 2696 → `[228, 2696]`
- 3758 PSHN_L −10 → `[228, 2696, −10]`
- 3759 PSHN_L −39 → `[228, 2696, −10, −39]`
- 3760 PSHN_L 192 → `[228, 2696, −10, −39, 192]`
- 3761 **MAPJUMP3** (inline = walkmesh ID) → pops 5 = `[FieldMapID=228, X=2696, Y=−10, Z=−39, angle=192]`, jumps to **field 228**. ✔ matches the observed runtime state exactly.

An unconditional JMP (0x02) at 3754 would make 3756 permanently unreachable — that contradiction is the proof that 3754 is JPF, not JMP. And the five "PSHM_W" values matching the MAPJUMP3 argument list verbatim (including the negatives) confirms they are PSHN_L (0x07) immediates, not memory reads — exactly as you suspected.

**MAPJUMP3 (0x2A) signature** (verbatim from the canonical 02A page): inline Argument = **Walkmesh ID**; stack (top last) = *Field Map ID, XCoord, YCoord, ZCoord, (angle?)* → **MAPJUMP3**. The 5th-from-top (Field Map ID) is the destination field. The last param is the facing angle ("never above 360, highest found 240… always even… usually a multiple of 4"), consistent with 192.

### Savemap header caveat
The 0x100 offset is a position **inside the variable block** and is header-independent at runtime (`field_vars_stack + 0x100`). The 0x4C (76-byte) vs 0x60 (96-byte) header discrepancy only matters when locating `game_moment` as a byte position inside a `.ff8` save FILE: for the Steam 2013 build (76-byte header) subtract 0x14 (20 bytes) from community offset tables that assume a 0x60 header. It does not affect the script-level operand (256) or the runtime read.

## Recommendations
1. **Re-derive every opcode from `word>>24`**, ignoring the tool's mnemonics — treat your "JMP/JMPB" tokens as JPF (0x03) vs JMP (0x02) by high byte. This single fix resolves the crux.
2. **Implement the core set:** 0x00 → PSHN_L literal (push whole word); 0x07 PSHN_L (push immediate); 0x01 CAL (pop2/push1, switch on sub-op: 06=EQ,07=GT,08=GE,09=LS,0A=LE,0B=NT, plus arithmetic/bitwise); 0x02 JMP (unconditional, no pop); 0x03 JPF (pop1, branch forward iff ==0); 0x05 LBL (no-op); 0x06 RET (stop); 0x0A/0x0C/0x11 memory reads (byte-unsigned/word-unsigned/word-signed); 0x0B/0x0D stores; 0x29/0x2A MAPJUMP/MAPJUMP3.
3. **Forward concrete interpreter loop:** maintain an operand stack; for unknown opcodes, model only their net stack delta (most "action" opcodes pop their documented arg count and push nothing); evaluate CAL; take JPF only when the popped value == 0; follow JMP unconditionally; **STOP at the first 0x2A and read the 5th-from-top stack entry as the destination field id**.
4. **Calibrate the jump base constant `k` once** against a known resolved arrow in your dump, then lock it; both JMP and JPF use it.
5. **Seed `game_moment` (word 0x100) to the value matching the boundary you want to resolve.** Benchmark/threshold that changes the outcome: if the field-228 (or SeeD-room 245) branch stops being taken, your `game_moment` seed crossed the story milestone — adjust it (or the modeled comparison constant, e.g. the 177/threshold operand) until the first reachable MAPJUMP3 matches the expected destination.

## Caveats
- **Jump base offset is inferred, not source-confirmed.** Deling's `Script.cpp`/`Opcode.cpp` were inaccessible to automated fetch; the "signed instruction count" unit and direction are documented, but any "+1" must be calibrated empirically.
- **0x0C vs 0x11 for the 0x0100 read** affects only sign extension; irrelevant for small positive story-progress values but implement both correctly for general fields.
- **The 3755 "JMPB→3748" back-jump** (loop re-entry vs else-arm) cannot be fully classified without the raw dwords, but it does not change the conclusion that MAPJUMP3 entry is JPF-gated on `game_moment`.
- **"Special bank" assumption corrected:** PSHSM_W is not a separate savemap/special bank — it is the same global field-variable block read as a signed word. There is no distinct field-local vs special bank at the opcode level here; all PSHM/PSHSM/POPM family ops index one variable block.
- Sources used throughout: canonical FF8/Field/Script/Opcodes pages and sub-pages (001 CAL, 002 JMP, 003 JPF, 007 PSHN_L, 00C PSHM_W, 011 PSHSM_W, 02A MAPJUMP3) on FFRTT/ff7-flat-wiki/Qhimm Modding Wiki (Aali, myst6re, Shard); FF8/Variables (varblock base, word 0x100 = story progress); FF8/FileFormat JSM (4-byte opcodes, entry-point encoding); and FFNx `src/ff8_data.cpp` (live dispatch-table indices and `field_game_moment = field_vars_stack + 0x100`). The Hyne save editor / 0x4C-header note governs only file-offset translation, not runtime reads.

---

# Claude cross-check vs the v0.17.9.3 `[OPDUMP]` capstone disassembly (2026-05-29)

This deep-research result independently agrees with the live-binary disassembly on every
material point. Concordance:

- **High-byte decoder = canonical numbering.** Confirmed both ways.
- **0x01 = CAL, not a jump.** Capstone showed `call [param*4 + 0x00B8DE4C](ctx)` (pops 2 /
  pushes 1 via the sub-handler). Research names the sub-op table: **param 9 = LS (less-than),
  param 6 = EQ (equal)** — these are exactly the two operators our dorm/corridor gates use.
  (v0.17.9.4 `[OPDUMP]` will dump `0x00B8DE4C[6]` and `[9]` so capstone can confirm LS/EQ at
  the instruction level — belt-and-suspenders, since the canonical names now look settled.)
- **0x02 = JMP unconditional (IP += param); 0x03 = JPF (pop; branch iff popped == 0).**
  Capstone confirmed both handlers verbatim.
- **0x07 = push immediate.** Confirmed.
- **0x0C** capstone = `mov ax,[ecx+0x1CFE9B8]` (varblock WORD read). Research refines the NAME:
  canonical **0x0C = PSHM_W (unsigned word)**; **0x11 = PSHSM_W (signed word)** is a *separate*
  opcode. Our earlier "0x0C = PSHSM_W" label was the off-by-name. Irrelevant to the trace
  (offset 0x100 holds a small positive value), but the interpreter should implement 0x0A/0x0C/
  0x11 as byte-unsigned / word-unsigned / word-signed respectively.
- **varblock[0x100] = `field_game_moment` (0x1CFEAB8) = main story-progress counter.** This is
  the live branch variable. The SeeD milestone advances game_moment past the compared threshold,
  flipping the JPF — selecting 237 (pre) vs 245 (post) at bgroad_5, and 228-now vs later at
  bgryo2_1. NOTE: it is the STORY-PROGRESS word, *not* the SeeD-rank/points byte we used in
  Chapter 5 — different variable.
- **MAPJUMP3 destField = 5th-from-top = stack[-5].** Confirmed; matches the live
  `[MAPJUMP-HOOK]` stack `[228,2696,-10,-39,192] -> 228`.
- **Jump unit = instructions (dwords); base constant k must be calibrated.** From our own
  `[BC-DUMP]`: index 3762 `JMP param 0x12(18)` and index 3773 `JMP param 7` BOTH land on the
  method-end RET at 3781 ⇒ `target = jump_index + 1 + sign_extend(param)` (**k = 1**). Lock k = 1
  for both JMP and JPF.

Bottom line: the opcode model is fully corroborated. After the v0.17.9.4 BAT confirms LS/EQ at
0x00B8DE4C[9]/[6], the forward concrete interpreter can be written directly against this model.
