// field_archive_jsm_decode.inl -- the field script VM's instruction encoding.
// Included from field_archive_jsm.inl before the scanner. Do not compile alone.
//
// All of this is read out of FF8_EN.exe rather than assumed:
//
//   DECODER  0x00530760
//       w = code[ip]
//       if ((w & 0xFF000000) == 0)  opcode = w          ; NO parameter
//       else                        opcode = w >> 24    ; param = sign_extend24(w)
//   DISPATCH 0x0052A647   call opcodeTable[opcode]   ; table at 0x00B8DE94
//
//   So a word whose high byte is zero is an OPCODE, and that is how every opcode
//   above 0xFF is encoded: MENUSAVE 0x12E, ADDITEM 0x125, DRAWPOINT 0x137,
//   CARDGAME 0x13A, SETDRAWPOINT 0x155. Reading those words as literal pushes is
//   what forced the old "0x1C is a prefix that pops the real opcode off the
//   stack" theory; opcode 0x1C is in fact `mov eax,1; ret` (0x0051D710).
//
//   PUSH OPCODES, from walking every handler in the table and counting writes to
//   the VM stack pointer at [ctx+0x184]:
//       0x07 PSHN_L 0x0051C990   pushes the INLINE PARAM        -- a literal
//       0x13        0x0051CD30   pushes the INLINE PARAM        -- a literal
//       0x08 PSHL   0x0051CAB0   pushes local [ctx + n*4 + 0x140]
//       0x0A PSHM_B 0x0051CAF0 | 0x0C PSHM_W | 0x0E PSHM_L   field var bank
//                                             (0x01CFE9B8 + param)
//       0x10 0x11 0x12           push; value not statically knowable
//       0x04 CALL   0x0051C530   pushes the return IP     -- never an argument
//       0x05        0x0051C570   pushes all EIGHT locals  -- never an argument
//
//   POP-TO-VARIABLE-BANK: 0x0B POPM_B (0x0051CCA0), 0x0D POPM_W (0x0051CCD0),
//   0x0F POPM_L (0x0051CD00) -- each stores at [param + 0x01CFE9B8].
//
// Arguments are the contiguous run of push opcodes immediately preceding the
// consumer, deepest first. That is the shape the compiler emits and it cannot
// drift, unlike a stack simulated across a whole method.

struct JsmInsn {
    uint16_t opcode;   // 0x0000-0x01FF, or 0xFFFF for a word outside the table
    int32_t  param;    // sign-extended 24-bit; 0 for a bare word
    bool     bare;     // true when the high byte was zero (opcode, no parameter)
};

static inline JsmInsn JsmDecodeWord(uint32_t w)
{
    JsmInsn ins;
    if ((w & 0xFF000000u) == 0) {
        ins.opcode = (w < 0x200u) ? (uint16_t)w : (uint16_t)0xFFFF;
        ins.param  = 0;
        ins.bare   = true;
    } else {
        ins.opcode = (uint16_t)(w >> 24);
        int32_t p  = (int32_t)(w & 0x00FFFFFFu);
        if (w & 0x00800000u) p |= (int32_t)0xFF000000;
        ins.param  = p;
        ins.bare   = false;
    }
    return ins;
}

// A push whose value is an ARGUMENT to the next consuming opcode.
static inline bool JsmIsArgPush(const JsmInsn& i)
{
    if (i.bare) return false;
    switch (i.opcode) {
        case 0x07: case 0x13:                            // literal
        case 0x08:                                       // local
        case 0x0A: case 0x0C: case 0x0E:                 // field variable bank
        case 0x10: case 0x11: case 0x12:                 // computed
            return true;
        default: return false;
    }
}

// Of the argument pushes, the two that carry a value we can read statically.
static inline bool JsmIsLiteralPush(const JsmInsn& i)
{
    return !i.bare && (i.opcode == 0x07 || i.opcode == 0x13);
}

// Pushes that are not arguments: CALL's return address and the prologue's
// eight saved locals. They end the current argument run.
static inline bool JsmIsNonArgPush(const JsmInsn& i)
{
    return !i.bare && (i.opcode == 0x04 || i.opcode == 0x05);
}

// Writes the popped value into the field variable bank at 0x01CFE9B8 + param.
static inline bool JsmIsVarBankPop(const JsmInsn& i)
{
    return !i.bare && (i.opcode == 0x0B || i.opcode == 0x0D || i.opcode == 0x0F);
}

// The marker a non-literal argument carries, so "only known at runtime" reads
// the same everywhere it is tested. Literal pushes never set bit 31: a literal
// is at most a sign-extended 24-bit value and a negative one has bits 16-30 set,
// which is why the test is the full 0xFFFF0000 mask and not just bit 31.
static inline int32_t JsmRuntimeMarker(int32_t addr)
{
    return (int32_t)(0x80000000u | (uint32_t)(addr & 0xFFFF));
}
static inline bool JsmIsRuntimeMarker(int32_t v)
{
    return ((uint32_t)v & 0xFFFF0000u) == 0x80000000u;
}

// ============================================================================
// v0.62.3 (#123): THE STORY GATE a method opens with.
//
// FF8 scripts guard a scene on the progress word with exactly this preamble:
//   0x05 <id>          method prologue
//   0x0A/0x0C/0x0E <a> PSHM_B/W/L -- read 1/2/4 bytes, zero-extended, at
//                      0x01CFE9B8 + a  (handlers 0x0051CAF0/CB30/CB70)
//   0x07 <literal>     PSHN_L
//   0x01 <cmp>         binary operator, dispatched through the table at
//                      0x00B8DE4C: 6 ==  7 >  8 >=  9 <  10 <=  11 !=
//   0x03 <n>           pop; jump n forward IF ZERO  (0x0051C4F0)
// so the method body runs only while the comparison holds. Everything else is
// left alone: a guard that is not exactly this shape yields hasGate = false and
// the consumer behaves as it always did.
struct JsmGate {
    bool     ok;
    int32_t  addr;
    uint8_t  width;   // 1, 2 or 4
    uint8_t  op;      // 6..11
    int32_t  value;
    // Where a FAILED test lands. Opcode 0x03 adds its param to the instruction
    // pointer and returns 4, which the VM loop at 0x0052A671 reads as "do not
    // auto-advance" -- so the new IP is the index OF the 0x03 plus n, i.e. 4 + n
    // relative to the method start. A gate only governs an instruction that lies
    // BEFORE this: anything at or after it runs either way.
    int32_t  skipTo;
};
static JsmGate JsmDecodeGate(const uint32_t* code, int len)
{
    JsmGate g = {};
    if (!code || len < 5) return g;
    JsmInsn i0 = JsmDecodeWord(code[0]);
    JsmInsn i1 = JsmDecodeWord(code[1]);
    JsmInsn i2 = JsmDecodeWord(code[2]);
    JsmInsn i3 = JsmDecodeWord(code[3]);
    JsmInsn i4 = JsmDecodeWord(code[4]);
    uint8_t w = (i1.opcode == 0x00A) ? 1 : (i1.opcode == 0x00C) ? 2
              : (i1.opcode == 0x00E) ? 4 : 0;
    if (i0.opcode != 0x005 || w == 0 || i2.opcode != 0x007 ||
        i3.opcode != 0x001 || i4.opcode != 0x003 ||
        i3.param < 6 || i3.param > 11 || i1.param < 0) return g;
    g.ok = true; g.addr = (int32_t)i1.param; g.width = w;
    g.op = (uint8_t)i3.param; g.value = (int32_t)i2.param;
    g.skipTo = 4 + i4.param;
    return g;
}
// The comparison itself lives in field_archive.h as JsmGateSatisfied, because
// the CONSUMER is the catalog (field_navigation.cpp) and the producer is the
// scanner (field_archive.cpp) -- two translation units, one shared header.
// v0.62.3.1: it was defined here, which compiles in the scanner's TU and in the
// harness (which includes this file) but NOT in field_navigation.cpp. The
// harness therefore built and passed while the real build did not.
