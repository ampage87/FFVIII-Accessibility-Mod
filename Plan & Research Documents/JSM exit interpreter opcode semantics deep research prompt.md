# JSM exit interpreter opcode semantics — deep research prompt

**Created:** 2026-05-29 (Bug 4, dormitory/corridor exit-destination resolver)

## Why this exists

The FF8 Accessibility Mod labels field exits (SCREEN_BOUND lines) by statically
resolving each MAPJUMP3's destination field id at catalog-build time. A
walk-through BAT (v0.17.9.2 `[BC-DUMP]`) proved the destination is NOT a varblock
value — it is a hardcoded immediate operand of whichever MAPJUMP3 the script
branches to, selected at runtime by flag-gated control flow. To label exits
correctly (and track story/SeeD progression) the mod needs a small forward
concrete JSM interpreter that follows the branches using live memory and stops at
the first MAPJUMP3. Building that interpreter is blocked on one narrow unknown:
the exact semantics of the jump and push/pop opcodes in this build's bytecode.

This prompt asks for those semantics. The answer can be validated cold against a
runtime oracle (see the appendix): the engine, crossing the bgryo2_1 single-room
boundary, fired the FIRST of three MAPJUMP3s -> destination field 228 with the
operand stack `[228, 2696, -10, -39, 192]`.

Related docs in this folder: `JSM bytecode format deep research results.md`,
`JSM JMPFL exit destinations deep research prompt.md`,
`PSHM_W JSM memory system deep research results.md`.

---

## Prompt (paste into ChatGPT deep research)

I'm reverse-engineering the **field-script (JSM) bytecode interpreter** in **Final
Fantasy VIII, 2000 PC / 2013 Steam re-release (FF8_EN.exe)**. I need precise,
sourced opcode semantics. Please draw on Deling / Makou Reactor source, the qhimm
forums, the FF8 field-script documentation, and the FF8_EN.exe interpreter itself
if available.

**Encoding I've already confirmed empirically (Steam 2013 build):** each
instruction is a 32-bit little-endian dword. The opcode is the **high byte**
(`word >> 24`); the parameter is the low 24 bits, sign-extended from bit 23. When
the high byte is `0x00` the whole word is a pushed literal (PSHN_L). Opcode `0x2A`
is MAPJUMP3 and pops 5 arguments (the 5th-from-top is the destination field id). I
have verified at runtime that MAPJUMP3's five arguments are produced by five
`0x07` instructions whose pushed values equal the `0x07` operands verbatim
(including negative values like -10 and -39), so `0x07` appears to push its
operand as an immediate, not read memory — please confirm or correct.

**What I need, with exact stack effects and operand meaning:**

1. Opcodes `0x01`, `0x02`, `0x03` (the jump family). For each: is it conditional
   or unconditional? Does it pop value(s) from the stack, and if so how many and
   what comparison/relation does it apply? What is the jump-offset encoding
   (relative to what, forward/backward, in instructions or bytes)? In particular,
   can `0x01` itself perform a compare-and-branch that consumes preceding pushed
   values?
2. The push/pop family: `0x07`, `0x08`, `0x09`, `0x0A`, `0x0B`, `0x0C` (PSHSM_W),
   `0x0D`. Which push immediates vs read from memory, and for the memory ones,
   which memory region/bank (field local variables vs the savemap / "special"
   bank)? For `0x0C` with operand `0x0100`, what exactly does it read?
3. Any comparison/test opcodes that produce a boolean for the conditional jumps.

**Concrete test case to explain (this is the crux).** This is one method's
bytecode (column = decimal instruction index, then opcode name as I currently
label it, then decoded operand):

```
3750 LBL 98
3751 PUSH_L 78
3752 PSHSM_W 0x0100
3753 PSHM_W 570
3754 JMP -> 3764
3755 JMPB -> 3748
3756 PSHM_W 228
3757 PSHM_W 2696
3758 PSHM_W -10
3759 PSHM_W -39
3760 PSHM_W 192
3761 MAPJUMP3 (-> dest 228)
3762 JPF -> 3781
3763 PSHSM_W 0x0100
3764 PSHM_W 710
3765 JMP -> 3772
... (second MAPJUMP3 -> 231 at 3772, third -> 174 at 3779)
```

At runtime, with the player crossing this boundary, the engine executed the
**first** MAPJUMP3 (index 3761) and jumped to field **228**, with the operand
stack holding `[228, 2696, -10, -39, 192]`. **Question: how does control reach
instruction 3756-3761 given that 3754 is a forward jump to 3764?** Either my
labeling of `0x01` as an unconditional JMP is wrong (it may be a conditional that
falls through when its test is false), or the `PSHSM_W 0x0100; PSHM_W <imm>; JMP`
triple is a compiled `if`. Please explain the exact control-flow idiom FF8's
script compiler emits here.

**Second data point:** a sibling field's equivalent method has two MAPJUMP3s,
`-> 237` and `-> 245`, preceded by `PSHSM_W 0x0100; PSHM_W 177; JMP`. Empirically
one destination is taken before a story milestone and the other after, so
`PSHSM_W 0x0100` is reading a story-progress value. Please identify, if known,
what FF8 field variable / flag `PSHSM_W` with operand `0x0100` corresponds to.

Note: FF8's savemap has a 76-byte (0x4C) header in this build; community offset
tables often assume a 96-byte header, so published savemap offsets may be 0x14 too
high.

Please give the opcode table with stack effects and a step-by-step execution trace
of the test-case method that ends at the first MAPJUMP3 with destination 228,
citing sources.

---

## Validation oracle (for checking the results — do NOT send as part of the prompt)

The answer is correct only if a faithful forward execution reproduces both:

- **bgryo2_1** `squalls` method 7 (the bytecode above): stops at the FIRST
  MAPJUMP3, destination **228** (B-Garden - Hallway 5), against current memory.
  Runtime `[MAPJUMP-HOOK]` ground truth: `VM stack sp=12 [228, 2696, -10, -39, 192]`,
  `engine RESULT: destField=228`.
- **bgroad_5** `squalls` method 7: two MAPJUMP3s, `-> 237` (B-Garden - Dormitory
  Double 1, pre-SeeD) and `-> 245` (= bgryo2_1, the single SeeD room, post-SeeD),
  gated by `PSHSM_W 0x0100; PSHM_W 177; JMP` at indices 2055-2057.

Cross-check any opcode claim against the on-disk disassembly
(`Game Files/disassembly/FF8_EN_.text_0x00*.asm`): push handler at `0x0051C5C0`,
MAPJUMP3 handler at `0x00521AC0`, dispatch table at `0x00B8DE94` (in `.data`).

## Disassembly correlation hints (carry-forward)

- Engine "firing IP" reported by the live hook (2969) and the mod's scriptData
  dword index (3761) differ by a fixed base offset (792). The operand fingerprint,
  not the IP, is the reliable cross-reference.
- Two conflicting decoders exist in the mod tree: the high-byte model (opcode =
  `word >> 24`, matches runtime) and the Deling-style `DecodeJSMInstruction` in
  `field_archive_jsm_helpers.inl` (bit31 = opcode flag, no inline params). The
  high-byte model is the correct one for this build; results should be expressed
  in those terms.
