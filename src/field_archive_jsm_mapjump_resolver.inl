// field_archive_jsm_mapjump_resolver.inl - v0.17.9.6
//
// Static destField resolver for MAPJUMP-family instructions. Runs as a
// follow-up pass after ScanJSMScripts() main entity scan. For each entity
// classified as JSM_ENT_LINE_SCREEN_BOUND, computes the actual destField the
// engine will jump to and writes it into info.param.
//
// Two engines live here:
//
//   1. InterpretExitMethod() -- the AUTHORITATIVE forward concrete interpreter
//      (v0.17.9.6 promotion of the v0.17.9.5 [SHADOW] validator). It follows
//      live control flow with one continuous operand stack and reads the field
//      varblock, so it picks the branch the engine will actually take and
//      returns the exact hardcoded-immediate destField. Validated against the
//      live [MAPJUMP-HOOK] oracle on bgryo2_1 (gate-true fall-through -> 228)
//      and bgroad_5 (gate-false JPF-taken -> 245), both correct where the old
//      addr-as-literal labeling was wrong (174 / 237).
//
//   2. ResolveMapjumpDest() -- the older abstract resolver, kept only as a
//      FALLBACK for the rare method the interpreter can't complete (RET before
//      a MAPJUMP / stack underflow / unmodeled opcode chain). It resets the
//      simulated stack at every basic-block boundary because it can't choose a
//      branch, so on flag-gated multi-MAPJUMP3 methods it underflows or
//      mis-resolves -- which is exactly the Bug-4 failure the interpreter fixes.
//
// Output:
//   Per-line [MAPJUMP-RES] log entries showing the resolved destField and the
//   source ([INTERP] / [LITERAL fallback] / [VARBLOCK fallback]), plus a
//   per-field summary. info.param is rewritten in place ONLY for
//   JSM_ENT_LINE_SCREEN_BOUND entities; the downstream [PSHM-DEST] / catalog
//   labeling path in HookedFieldScriptsInit reads info.param. A plain positive
//   literal (what the interpreter returns) flows straight through as the
//   destination field id; a 0x80000000|addr marker (fallback only) is resolved
//   downstream.
//
// Cross-reference:
//   Stack effect verified by FF8_EN.exe disassembly:
//     opcode_mapjump  @ 0x00521A20 pops 4 (destField is 4th-from-top)
//     opcode_mapjump3 @ 0x00521AC0 pops 5 (destField is 5th-from-top)
//     Both store destField at 0x01CE4762 (transition request global).
//   Opcode model confirmed v0.17.9.3/.4 [OPDUMP] + capstone, cross-checked vs
//   the canonical Makou Reactor list and the live runtime oracle.
//
// Included from field_archive_jsm.inl AFTER scan.inl. Do not compile
// independently. The including TU (field_archive.cpp) pulls in <windows.h>,
// so the SEH guard in SafeInterpretExitMethod compiles here.

namespace MapjumpResolver {

// EXIT_TRACE_DIAG: gated diagnostic instrumentation — the [EXIT-TRACE] survey,
// [EXIT-OPSEQ] op-walk, and [EXIT-DISASM] linear disassembler used to validate
// the exit interpreter (v0.17.9.7-.10). OFF for production; set to 1 to re-enable
// the survey + disassembler when probing a new field. Behavior-neutral: the
// production resolve path (info.param via [INTERP]) is identical either way.
#define EXIT_TRACE_DIAG 0

// (pops, pushes) stack effect for a JSM opcode.
// Returns {0, 0} (no-op) for any opcode we don't know -- conservative.
// PSHN_L (high byte == 0) is special-cased by the caller before this is consulted.
struct StackEffect { int8_t pops; int8_t pushes; };

static inline StackEffect GetStackEffect(uint8_t op)
{
    switch (op) {
        // --- Control flow ---
        case 0x01: return {0, 0};   // JMP
        case 0x02: return {1, 0};   // JPF (pops condition)
        case 0x03: return {0, 0};   // JMPB
        case 0x04: return {0, 0};   // RET (per JSM_OP_RET = 0x004)
        case 0x05: return {0, 0};   // LBL
        case 0x06: return {0, 0};   // (slot reserved)

        // --- Memory push/pop ---
        case 0x07: return {0, 1};   // PSHM_W  (handled specially -- caller intercepts)
        case 0x08: return {1, 0};   // POPM_W
        case 0x09: return {0, 1};   // PSHM_B  (handled specially)
        case 0x0A: return {0, 1};   // PSHM_L  (handled specially)
        case 0x0B: return {1, 0};   // POPM_L
        case 0x0C: return {0, 1};   // PSHSM_W (handled specially)
        case 0x0D: return {0, 1};   // PSHSM_B (handled specially)
        case 0x0E: return {1, 0};   // POPSM_W (assumed)
        case 0x0F: return {1, 0};   // POPSM_B (assumed)

        // --- Script invocation ---
        case 0x14: return {3, 0};   // REQ
        case 0x15: return {3, 0};   // REQSW
        case 0x16: return {3, 0};   // REQEW

        // --- Entity ops ---
        case 0x1A: return {0, 0};   // UNUSE (assumed)
        case 0x1C: return {1, 0};   // extended dispatch (pops opcode index)
        case 0x1D: return {3, 0};   // SET     (X, Y, tri)
        case 0x1E: return {4, 0};   // SET3    (X, Y, Z, tri)

        // --- Ladder ---
        case 0x25: return {0, 0};   // LADDERUP
        case 0x26: return {0, 0};   // LADDERDOWN

        // --- Transitions (we walk THROUGH them only for diagnostic; caller
        // stops at the target IP so these stack effects only matter for
        // preceding instances) ---
        case 0x29: return {4, 0};   // MAPJUMP
        case 0x2A: return {5, 0};   // MAPJUMP3
        case 0x38: return {5, 0};   // DISCJUMP
        case 0x5C: return {4, 0};   // MAPJUMPO

        // --- Model / talk / scroll / dialog -- mostly inline-param driven,
        // we assume no stack effect unless proven otherwise ---
        case 0x2B: return {1, 0};   // SETMODEL (assumed)
        case 0x39: return {7, 0};   // SETLINE  (x1,y1,z1,x2,y2,z2,idx)
        case 0x47: return {0, 0};   // MES
        case 0x4A: return {0, 0};   // ASK
        case 0x56: return {1, 0};   // TALKRADIUS
        case 0x57: return {0, 0};   // TALKON
        case 0x60: return {0, 0};   // SHOW
        case 0x61: return {0, 0};   // HIDE

        // Default: assume no stack effect. Safer than guessing wrong.
        default:   return {0, 0};
    }
}

// Abstract value tracked on the simulated stack (fallback resolver only).
enum ValueKind { VK_UNKNOWN = 0, VK_LITERAL, VK_VARBLOCK };
struct Value {
    ValueKind kind;
    int32_t   data;  // literal value, or varblock address (for VK_VARBLOCK)
};

// Build the set of jump-target IPs reachable from within [methodStart, methodEnd).
// Targets include:
//   - LBL instructions (op 0x05) at their own IP
//   - Computed targets of forward jumps (JMP=0x01, JPF=0x02)
//   - Computed targets of backward jumps (JMPB=0x03)
// JSM bytecode encodes the offset in the low 24 bits sign-extended (same
// convention the forward scanner uses for opcParam).
//
// Returns count of targets collected. Duplicates are fine for membership.
static int BuildJumpTargets(const uint32_t* scriptData,
                            int methodStart, int methodEnd,
                            int* outTargets, int maxTargets)
{
    int count = 0;
    for (int ip = methodStart; ip < methodEnd && count < maxTargets; ip++) {
        uint32_t word = scriptData[ip];
        uint8_t hb = (uint8_t)(word >> 24);
        if (hb == 0) continue;  // PSHN_L
        int32_t param = (int32_t)(word & 0x00FFFFFF);
        if (word & 0x00800000) param |= (int32_t)0xFF000000;  // sign-extend 24->32
        int target = -1;
        if (hb == 0x05) {
            target = ip;  // LBL is itself a target
        } else if (hb == 0x01 || hb == 0x02) {
            target = ip + 1 + param;  // forward jump or JPF
        } else if (hb == 0x03) {
            target = ip + 1 - param;  // JMPB (positive offset, subtract)
        }
        if (target >= methodStart && target < methodEnd) {
            outTargets[count++] = target;
        }
    }
    return count;
}

static inline bool IsJumpTarget(const int* targets, int count, int ip)
{
    for (int i = 0; i < count; i++)
        if (targets[i] == ip) return true;
    return false;
}

// ----------------------------------------------------------------------------
// FALLBACK resolver: resolve the destField source of a single MAPJUMP-family
// instruction by forward-walking the method bytecode and inspecting the
// simulated stack at the moment of the instruction. Resets the stack at every
// basic-block boundary (can't pick a branch), so it underflows or mis-resolves
// on flag-gated multi-MAPJUMP3 methods -- the interpreter is preferred. Kept
// for methods the interpreter can't complete.
//
// Returns either:
//   * a positive literal field ID  (info.param can adopt directly)
//   * a 0x80000000 | addr marker   (downstream [PSHM-DEST] resolves)
//   * -1 on failure (stack underflow or unresolvable due to unmodeled chain)
// ----------------------------------------------------------------------------
static int32_t ResolveMapjumpDest(const uint32_t* scriptData,
                                  int methodStart, int methodEnd,
                                  int targetIp, int argCount,
                                  const char* fieldName, int entityIdx,
                                  const char* symName, int methodIdx)
{
    static const int MAX_TARGETS = 1024;
    int targets[MAX_TARGETS];
    int numTargets = BuildJumpTargets(scriptData, methodStart, methodEnd,
                                       targets, MAX_TARGETS);

    static const int STACK_MAX = 32;
    Value stack[STACK_MAX];
    int sp = 0;
    auto push = [&](Value v) {
        if (sp < STACK_MAX) {
            stack[sp++] = v;
        } else {
            for (int i = 0; i < STACK_MAX - 1; i++) stack[i] = stack[i+1];
            stack[STACK_MAX - 1] = v;
        }
    };
    auto popN = [&](int n) {
        if (n <= 0) return;
        if (sp >= n) sp -= n;
        else         sp = 0;
    };

    for (int ip = methodStart; ip <= targetIp; ip++) {
        if (ip != methodStart && IsJumpTarget(targets, numTargets, ip)) {
            sp = 0;
        }
        uint32_t word = scriptData[ip];
        uint8_t hb = (uint8_t)(word >> 24);
        if (hb == 0) {
            Value v = { VK_LITERAL, (int32_t)word };
            push(v);
            continue;
        }

        int32_t param = (int32_t)(word & 0x00FFFFFF);
        if (word & 0x00800000) param |= (int32_t)0xFF000000;  // sign-extend

        if (ip == targetIp) {
            break;
        }

        if (hb == 0x07 || hb == 0x09 || hb == 0x0A || hb == 0x0C || hb == 0x0D) {
            Value v = { VK_VARBLOCK, param & 0xFFFF };
            push(v);
            continue;
        }

        StackEffect eff = GetStackEffect(hb);
        if (eff.pops > 0)  popN(eff.pops);
        for (int p = 0; p < eff.pushes; p++) {
            Value v = { VK_UNKNOWN, 0 };
            push(v);
        }
    }

    if (sp < argCount) {
        Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' m%d ip=%d inline_param=0x%04X: "
                   "stack underflow (sp=%d need %d) -- unresolved",
                   fieldName, entityIdx, symName, methodIdx, targetIp,
                   (unsigned)(scriptData[targetIp] & 0xFFFF), sp, argCount);
        return -1;
    }

    Value dest = stack[sp - argCount];
    if (dest.kind == VK_LITERAL) {
        Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' m%d ip=%d inline_param=0x%04X: "
                   "LITERAL destField=%d",
                   fieldName, entityIdx, symName, methodIdx, targetIp,
                   (unsigned)(scriptData[targetIp] & 0xFFFF), (int)dest.data);
        return dest.data;
    }
    if (dest.kind == VK_VARBLOCK) {
        int32_t marker = (int32_t)(0x80000000u | (uint32_t)(dest.data & 0xFFFF));
        Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' m%d ip=%d inline_param=0x%04X: "
                   "VARBLOCK addr=0x%04X -> marker=0x%08X",
                   fieldName, entityIdx, symName, methodIdx, targetIp,
                   (unsigned)(scriptData[targetIp] & 0xFFFF),
                   (unsigned)(dest.data & 0xFFFF), (unsigned)marker);
        return marker;
    }
    Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' m%d ip=%d inline_param=0x%04X: "
               "UNKNOWN destField (arithmetic/unmodeled chain) -- unresolved",
               fieldName, entityIdx, symName, methodIdx, targetIp,
               (unsigned)(scriptData[targetIp] & 0xFFFF));
    return -1;
}

// ============================================================================
// v0.17.9.6: forward concrete JSM exit interpreter (Bug-4 fix, PROMOTED).
// Authoritative destField resolver for SCREEN_BOUND lines. Unlike the abstract
// fallback above (which resets the stack at every basic-block boundary because
// it can't pick a branch), this FOLLOWS control flow with one continuous
// operand stack and reads the live field varblock, so it computes the exact
// destField the engine will use.
//
// Opcode model (confirmed v0.17.9.3/.4 OPDUMP + capstone, cross-checked vs the
// canonical Makou Reactor list and the live [MAPJUMP-HOOK] oracle):
//   op = word>>24; param = sign-extend(low 24).
//   0x00 PSHN_L push whole word | 0x07 push sign-extended immediate
//   0x0A/0x0C/0x11 push varblock byte / word-unsigned / word-signed @ base+param
//   0x0B/0x0D pop->varblock (interp: pop only, never writes) | 0x08 local read (push 0)
//   0x01 CAL: pop2 push1 value1<op>value2 (op 5=NEG / F=NOT are unary pop1)
//             ops: 0 ADD,1 SUB,2 MUL,3 DIV,4 MOD,6 EQ,7 GT,8 GE,9 LS,A LE,
//             B NT,C AND,D OR,E EOR,10 RSH,11 LSH
//   0x02 JMP IP+=param | 0x03 JPF pop; if==0 IP+=param
//   JMP target = ip+1+param (k=1); JPF taken-target = ip+param (k=0, v0.17.9.10)
//   0x05 LBL no-op | 0x06 RET stop
//   0x2A MAPJUMP3 / 0x38 DISCJUMP stop, dest = stack[-5]
//   0x29 MAPJUMP  / 0x5C MAPJUMPO stop, dest = stack[-4]
// Validated v0.17.9.5 (shadow) on bgryo2_1->228 + bgroad_5->245 vs live engine.
// ============================================================================
static const uintptr_t EXIT_VARBLOCK_BASE = 0x01CFE9B8;  // field var block (Steam 2013)

// Forward-execute one method from its entry. Returns destField (>=0, low 16
// bits) at the first MAPJUMP-family opcode reached, or -1 (RET / underflow /
// out-of-range / step-cap). POD locals only so the caller's SEH guard covers
// any wild varblock read.
// v0.17.9.7 LOCAL: interpreter bail/trace capture (see EXIT_TRACE_DIAG).
struct InterpTrace {
    int32_t result; int bailIp; uint8_t bailOp; int bailSp; int reason; int steps;
    int nrec; struct { int ip; uint8_t op; int sp; } rec[48];  // v0.17.9.8: per-step op trace (sp BEFORE exec)
};
// reason: 0=reached MAPJUMP (success) | 1=RET(0x06) | 2=MAPJUMP-arg underflow
//         | 3=fell off [mStart,mEnd) range | 4=step cap | 5=CAL/JPF underflow | -1=SEH/none
static inline int32_t IT_ret(InterpTrace* tr, int ip, int op, int sp, int reason, int steps, int32_t val)
{
    if (tr) { tr->result = val; tr->bailIp = ip; tr->bailOp = (uint8_t)op; tr->bailSp = sp; tr->reason = reason; tr->steps = steps; }
    return val;
}

static int32_t InterpretExitMethod(const uint32_t* scriptData, int scriptDataDwords,
                                   int mStart, int mEnd, InterpTrace* tr = nullptr)
{
    static const int STK = 64;
    int32_t stack[STK];
    // v0.18.3.281 (#86 follow-up): parallel provenance tag per stack slot.
    // true = this value was read from the live varblock (or is otherwise of
    // unknown origin -- local-frame reads, unmodeled opcodes); false = it
    // traces back only to script-literal pushes (0x00/0x07) and arithmetic
    // over literals. The interpreter still LEGITIMATELY reads the varblock
    // to pick the taken branch (that's its whole point, and it's proven
    // correct doing so on bgryo2_1/bgroad_5 and now glprein1's trapdoor) --
    // this only gates trust in the FINAL destField operand at a MAPJUMP-
    // family instruction. A destField sourced from a variable read (as
    // opposed to a hardcoded literal in the taken branch) can hold a
    // different value at scan time than it will when the script actually
    // fires (glclock1 'irvine' resolved a live-but-wrong field 5/"wm05"
    // this way -- the true destination, confirmed by the earlier session's
    // live MAPJUMP-HOOK oracle, is 762). Declining (returning -1) when the
    // destination itself is tainted restores the old fallback resolver's
    // LITERAL-vs-VARBLOCK caution for exactly this one operand, without
    // weakening the interpreter's branch-following anywhere else.
    bool taint[STK];
    int sp = 0;
    const uint8_t* vb = (const uint8_t*)EXIT_VARBLOCK_BASE;
    int ip = mStart;
    int steps = 0;
    const int MAX_STEPS = 200000;

    while (ip >= mStart && ip < mEnd && ip < scriptDataDwords && steps++ < MAX_STEPS) {
        uint32_t word = scriptData[ip];
        uint8_t hb = (uint8_t)(word >> 24);
        if (tr && tr->nrec < 48) { tr->rec[tr->nrec].ip = ip; tr->rec[tr->nrec].op = hb; tr->rec[tr->nrec].sp = sp; tr->nrec++; }  // v0.17.9.8 op trace
        if (hb == 0x00) { if (sp < STK) { taint[sp] = false; stack[sp++] = (int32_t)word; } ip++; continue; }

        int32_t param = (int32_t)(word & 0x00FFFFFF);
        if (word & 0x00800000) param |= (int32_t)0xFF000000;  // sign-extend 24->32
        int off = param & 0xFFFF;  // varblock byte offset

        switch (hb) {
            case 0x07: if (sp < STK) { taint[sp] = false; stack[sp++] = param; } ip++; break;                       // push immediate
            case 0x0A: if (sp < STK) { taint[sp] = true; stack[sp++] = *(const uint8_t*)(vb + off); }  ip++; break;  // varblock byte (unsigned)
            case 0x0C: if (sp < STK) { taint[sp] = true; stack[sp++] = *(const uint16_t*)(vb + off); } ip++; break;  // varblock word (unsigned)
            case 0x11: if (sp < STK) { taint[sp] = true; stack[sp++] = *(const int16_t*)(vb + off); }  ip++; break;  // varblock word (signed)
            case 0x08: if (sp < STK) { taint[sp] = true; stack[sp++] = 0; } ip++; break;                            // local-frame read (unknown at scan)
            case 0x0B: case 0x0D: if (sp > 0) sp--; ip++; break;                              // pop->varblock (no write in interp)
            case 0x01: {                                                                      // CAL
                uint8_t cop = (uint8_t)(param & 0xFF);
                if (cop == 0x05) { if (sp < 1) return IT_ret(tr, ip, hb, sp, 5, steps, -1); int32_t a = stack[--sp]; bool ta = taint[sp]; if (sp < STK) { taint[sp] = ta; stack[sp++] = -a; } }
                else if (cop == 0x0F) { if (sp < 1) return IT_ret(tr, ip, hb, sp, 5, steps, -1); int32_t a = stack[--sp]; bool ta = taint[sp]; if (sp < STK) { taint[sp] = ta; stack[sp++] = ~a; } }
                else {
                    if (sp < 2) return IT_ret(tr, ip, hb, sp, 5, steps, -1);
                    int32_t b = stack[--sp]; bool tb = taint[sp];
                    int32_t a = stack[--sp]; bool ta = taint[sp];
                    int32_t r = 0;
                    switch (cop) {
                        case 0x00: r = a + b; break;
                        case 0x01: r = a - b; break;
                        case 0x02: r = a * b; break;
                        case 0x03: r = (b != 0) ? a / b : 0; break;
                        case 0x04: r = (b != 0) ? a % b : 0; break;
                        case 0x06: r = (a == b) ? 1 : 0; break;
                        case 0x07: r = (a >  b) ? 1 : 0; break;
                        case 0x08: r = (a >= b) ? 1 : 0; break;
                        case 0x09: r = (a <  b) ? 1 : 0; break;
                        case 0x0A: r = (a <= b) ? 1 : 0; break;
                        case 0x0B: r = (a != b) ? 1 : 0; break;
                        case 0x0C: r = a & b; break;
                        case 0x0D: r = a | b; break;
                        case 0x0E: r = a ^ b; break;
                        case 0x10: r = (int32_t)((uint32_t)a >> (b & 31)); break;
                        case 0x11: r = a << (b & 31); break;
                        default:   r = 0; break;
                    }
                    if (sp < STK) { taint[sp] = (ta || tb); stack[sp++] = r; }
                }
                ip++;
                break;
            }
            case 0x02: ip = ip + 1 + param; break;                                            // JMP unconditional
            case 0x03: {                                                                      // JPF
                if (sp < 1) return IT_ret(tr, ip, hb, sp, 5, steps, -1);
                int32_t c = stack[--sp];
                // v0.17.9.10: JPF taken-target is ip+param (k=0), NOT ip+1+param.
                // Proven by the bgryo2_1 l1 [EXIT-DISASM]: every taken JPF must land
                // on the per-case `PSHM_W var[0x100]` reload (else those reloads are
                // unreachable dead code, since each prior case ends with JMP->RET).
                // JMP (0x02) stays k=1 (its case-end jumps land exactly on RET).
                ip = (c == 0) ? (ip + param) : (ip + 1);
                break;
            }
            case 0x05: ip++; break;                                                           // LBL (no-op)
            case 0x06: return IT_ret(tr, ip, hb, sp, 1, steps, -1);                           // RET (no dest)
            case 0x2A: case 0x38:                                                             // MAPJUMP3 / DISCJUMP
                if (sp < 5) return IT_ret(tr, ip, hb, sp, 2, steps, -1);
                if (taint[sp - 5]) return IT_ret(tr, ip, hb, sp, 6, steps, -1);  // dest variable-sourced -- don't trust it
                return IT_ret(tr, ip, hb, sp, 0, steps, stack[sp - 5] & 0xFFFF);
            case 0x29: case 0x5C:                                                             // MAPJUMP / MAPJUMPO
                if (sp < 4) return IT_ret(tr, ip, hb, sp, 2, steps, -1);
                if (taint[sp - 4]) return IT_ret(tr, ip, hb, sp, 6, steps, -1);  // dest variable-sourced -- don't trust it
                return IT_ret(tr, ip, hb, sp, 0, steps, stack[sp - 4] & 0xFFFF);
            default: {                                                                        // unknown: conservative stack delta
                StackEffect eff = GetStackEffect(hb);
                for (int i = 0; i < eff.pops && sp > 0; i++) sp--;
                for (int i = 0; i < eff.pushes && sp < STK; i++) { taint[sp] = true; stack[sp++] = 0; }
                ip++;
                break;
            }
        }
    }
    return IT_ret(tr, ip, 0xFF, sp, (steps >= MAX_STEPS ? 4 : 3), steps, -1);  // fell off end / range / step cap
}

// SEH-guarded wrapper. The interpreter reads the live field varblock; a wild
// read (malformed/early-lifecycle field) is caught here and reported as
// "no concrete result" (-1) so Run falls back to the abstract resolver.
// Leaf function so Run stays free of __try (no C++ unwinding conflict).
static int32_t SafeInterpretExitMethod(const uint32_t* scriptData, int scriptDataDwords,
                                       int mStart, int mEnd, InterpTrace* tr = nullptr)
{
    __try {
        return InterpretExitMethod(scriptData, scriptDataDwords, mStart, mEnd, tr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

#if EXIT_TRACE_DIAG
// v0.17.9.7 LOCAL: limit [EXIT-TRACE] volume to the survey fields.
static inline bool ExitTraceFieldGated(const char* f)
{
    return f && (strcmp(f, "bgryo2_1") == 0 || strcmp(f, "bgroad_5") == 0 ||
                 strcmp(f, "bcport_2") == 0 || strcmp(f, "dotown_2") == 0);
}

// v0.17.9.9 LOCAL: linear disassembly of a bailing exit method. Decodes EVERY
// instruction in [mStart,mEnd) (capped), showing operands the [EXIT-OPSEQ]
// path-walk omits: varblock offsets + their LIVE values, immediates, CAL
// sub-ops, jump targets, and MAPJUMP destinations. This answers "what does the
// gate actually test, and is it a readable story global?". Logs [EXIT-DISASM].
static const char* CalSubOpName(uint8_t c)
{
    switch (c) {
        case 0x00: return "ADD"; case 0x01: return "SUB"; case 0x02: return "MUL";
        case 0x03: return "DIV"; case 0x04: return "MOD"; case 0x05: return "NEG";
        case 0x06: return "EQ";  case 0x07: return "GT";  case 0x08: return "GE";
        case 0x09: return "LS";  case 0x0A: return "LE";  case 0x0B: return "NT";
        case 0x0C: return "AND"; case 0x0D: return "OR";  case 0x0E: return "EOR";
        case 0x0F: return "NOT"; case 0x10: return "RSH"; case 0x11: return "LSH";
        default:   return "?";
    }
}

static void LinearDisasmExitMethod(const char* fieldName, int ei, const char* sym,
                                   const uint32_t* scriptData, int scriptDataDwords,
                                   int mStart, int mEnd)
{
    const uint8_t* vb = (const uint8_t*)EXIT_VARBLOCK_BASE;
    int cap = mStart + 1024;
    int end = (mEnd < cap) ? mEnd : cap;
    if (end > scriptDataDwords) end = scriptDataDwords;
    Log::Field("FieldArchive: [EXIT-DISASM] %s ent%d '%s' method [%d,%d)%s:",
               fieldName, ei, sym, mStart, mEnd, (end < mEnd) ? " (capped@1024)" : "");
    for (int ip = mStart; ip < end; ip++) {
        uint32_t word = scriptData[ip];
        uint8_t hb = (uint8_t)(word >> 24);
        if (hb == 0x00) {
            Log::Field("FieldArchive: [EXIT-DISASM]   %d: PSHN_L lit=%d", ip, (int)(int32_t)word);
            continue;
        }
        int32_t param = (int32_t)(word & 0x00FFFFFF);
        if (word & 0x00800000) param |= (int32_t)0xFF000000;
        int off = param & 0xFFFF;
        switch (hb) {
            case 0x07: Log::Field("FieldArchive: [EXIT-DISASM]   %d: PSHN_L imm=%d", ip, (int)param); break;
            case 0x0A: Log::Field("FieldArchive: [EXIT-DISASM]   %d: PSHM_B  var[0x%04X]=%d", ip, off, (int)*(const uint8_t*)(vb+off)); break;
            case 0x0C: Log::Field("FieldArchive: [EXIT-DISASM]   %d: PSHM_W  var[0x%04X]=%d", ip, off, (int)*(const uint16_t*)(vb+off)); break;
            case 0x11: Log::Field("FieldArchive: [EXIT-DISASM]   %d: PSHSM_W var[0x%04X]=%d", ip, off, (int)*(const int16_t*)(vb+off)); break;
            case 0x0B: Log::Field("FieldArchive: [EXIT-DISASM]   %d: POPM_B  var[0x%04X]", ip, off); break;
            case 0x0D: Log::Field("FieldArchive: [EXIT-DISASM]   %d: POPM_W  var[0x%04X]", ip, off); break;
            case 0x08: Log::Field("FieldArchive: [EXIT-DISASM]   %d: PSHI_L  local[%d] (interp pushes 0)", ip, off); break;
            case 0x01: Log::Field("FieldArchive: [EXIT-DISASM]   %d: CAL %s", ip, CalSubOpName((uint8_t)(param & 0xFF))); break;
            case 0x02: Log::Field("FieldArchive: [EXIT-DISASM]   %d: JMP -> %d", ip, ip + 1 + param); break;
            case 0x03: Log::Field("FieldArchive: [EXIT-DISASM]   %d: JPF iffalse-> %d", ip, ip + 1 + param); break;
            case 0x05: Log::Field("FieldArchive: [EXIT-DISASM]   %d: LBL", ip); break;
            case 0x06: Log::Field("FieldArchive: [EXIT-DISASM]   %d: RET", ip); break;
            case 0x29: Log::Field("FieldArchive: [EXIT-DISASM]   %d: <<< MAPJUMP  (wmId=%d) dest=stack[-4]", ip, off); break;
            case 0x2A: Log::Field("FieldArchive: [EXIT-DISASM]   %d: <<< MAPJUMP3 (wmId=%d) dest=stack[-5]", ip, off); break;
            case 0x38: Log::Field("FieldArchive: [EXIT-DISASM]   %d: <<< DISCJUMP dest=stack[-5]", ip); break;
            case 0x5C: Log::Field("FieldArchive: [EXIT-DISASM]   %d: <<< MAPJUMPO dest=stack[-4]", ip); break;
            default:   Log::Field("FieldArchive: [EXIT-DISASM]   %d: op=0x%02X param=0x%06X", ip, (unsigned)hb, (unsigned)(word & 0x00FFFFFF)); break;
        }
    }
}

static void SafeLinearDisasmExitMethod(const char* fieldName, int ei, const char* sym,
                                       const uint32_t* scriptData, int scriptDataDwords,
                                       int mStart, int mEnd)
{
    __try {
        LinearDisasmExitMethod(fieldName, ei, sym, scriptData, scriptDataDwords, mStart, mEnd);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldArchive: [EXIT-DISASM] %s ent%d '%s' SEH fault during disasm", fieldName, ei, sym);
    }
}
#endif

// Public entry point: run the resolver pass over EVERY entity in the JSM.
// info.param is overwritten ONLY for JSM_ENT_LINE_SCREEN_BOUND entities (the
// downstream [PSHM-DEST] / catalog labeling path treats info.param as the
// destination field for those). Other entity types are logged diagnostically.
//
//   methodStartIdxs/methodCounts/totalEntities describe the EntityGroup
//   layout the scan loop built up. methodCounts[e] is the INCLUSIVE max method
//   index offset (matching the `m <= methodCount` loop convention in scan).
static void Run(const char* fieldName,
                const uint32_t* scriptData, int scriptDataDwords,
                const uint16_t* entryPoints, int totalMethods,
                const int* methodStartIdxs,
                const int* methodCounts,
                int totalEntities,
                JSMEntityInfo* entities, int entityCount)
{
    int resolved = 0, unresolved = 0, scanned = 0, paramUpdates = 0;
    for (int e = 0; e < entityCount; e++) {
        JSMEntityInfo& info = entities[e];
        int ei = info.jsmIndex;
        if (ei < 0 || ei >= totalEntities) continue;

        int mStartIdx = methodStartIdxs[ei];
        int mCount    = methodCounts[ei];

        // Fallback-resolver accumulators (LITERAL preferred over VARBLOCK).
        int32_t bestLiteral = -1;     // first LITERAL resolved (>= 0)
        int32_t bestMarker  = -1;     // first VARBLOCK marker (0x8000xxxx)
        bool    anyResolved = false;
        // First method containing a MAPJUMP-family op -- the interpreter
        // entry point. Interpreting from the method start lets it follow the
        // live branch to whichever MAPJUMP3 the engine actually reaches.
        int mjMethodStart = -1, mjMethodEnd = -1;

        for (int m = 0; m <= mCount; m++) {
            int methodIdx = mStartIdx + m;
            if (methodIdx >= totalMethods) break;
            uint16_t mStart = entryPoints[methodIdx] & 0x7FFF;
            uint16_t mEnd = (uint16_t)scriptDataDwords;
            if (methodIdx + 1 < totalMethods)
                mEnd = entryPoints[methodIdx + 1] & 0x7FFF;
            if (mStart >= mEnd) continue;

            for (int ip = (int)mStart; ip < (int)mEnd && ip < scriptDataDwords; ip++) {
                uint32_t word = scriptData[ip];
                uint8_t hb = (uint8_t)(word >> 24);
                int argCount = 0;
                if (hb == 0x29)      argCount = 4;  // MAPJUMP
                else if (hb == 0x2A) argCount = 5;  // MAPJUMP3
                else if (hb == 0x38) argCount = 5;  // DISCJUMP
                else if (hb == 0x5C) argCount = 4;  // MAPJUMPO
                else continue;
                scanned++;
                if (mjMethodStart < 0) { mjMethodStart = (int)mStart; mjMethodEnd = (int)mEnd; }
                int32_t r = ResolveMapjumpDest(scriptData, mStart, mEnd, ip,
                                                argCount, fieldName, ei,
                                                info.symName, m);
                if (r == -1) continue;
                anyResolved = true;
                if (((uint32_t)r & 0x80000000u) == 0) {
                    if (bestLiteral == -1) bestLiteral = r;
                } else {
                    if (bestMarker == -1) bestMarker = r;
                }
            }
        }

        // v0.17.9.6: the forward concrete interpreter is authoritative for
        // SCREEN_BOUND exits. It follows live control flow (reads the field
        // varblock, picks the taken branch) and returns the exact destField
        // the engine will use -- validated on bgryo2_1->228 and bgroad_5->245
        // against the live [MAPJUMP-HOOK] oracle. The abstract resolver result
        // (bestLiteral/bestMarker) is kept only as a fallback for the rare
        // method the interpreter can't complete (RET/underflow/unmodeled).
        // v0.18.3.281 (#86): also run the interpreter for JSM_ENT_MAP_EXIT.
        // It was scoped to SCREEN_BOUND-only when it shipped (v0.17.9.6) --
        // that changelog entry itself flagged "the dropped real-door MAP_EXIT
        // 'l1' in bgryo2_1" as a still-open gap. glprein1 'irvine' (the
        // Presidential Residence trapdoor, #86) hits the same wall: its
        // script has real branching the abstract fallback can't follow, so
        // it underflows (`stack underflow (sp=1 need 4)`) instead of
        // resolving. The interpreter follows live control flow + the real
        // varblock, so it succeeds where the fallback can't.
        int32_t interpDest = -1;
        if ((info.type == JSM_ENT_LINE_SCREEN_BOUND || info.type == JSM_ENT_MAP_EXIT) &&
            mjMethodStart >= 0) {
            interpDest = SafeInterpretExitMethod(scriptData, scriptDataDwords,
                                                 mjMethodStart, mjMethodEnd);
        }

        if (info.type == JSM_ENT_LINE_SCREEN_BOUND && (interpDest >= 0 || anyResolved)) {
            int32_t newParam;
            const char* src;
            if (interpDest >= 0)        { newParam = interpDest;  src = " [INTERP]"; }
            else if (bestLiteral != -1) { newParam = bestLiteral; src = " [LITERAL fallback]"; }
            else                        { newParam = bestMarker;  src = " [VARBLOCK fallback]"; }
            int32_t oldParam = info.param;
            info.param = newParam;
            paramUpdates++;
            resolved++;
            Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' (SCREEN_BOUND): "
                       "param 0x%08X -> 0x%08X%s",
                       fieldName, ei, info.symName,
                       (unsigned)oldParam, (unsigned)newParam, src);
        } else if (info.type == JSM_ENT_MAP_EXIT && interpDest >= 0) {
            // v0.18.3.281 (#86): concrete INTERP result for a MAP_EXIT "Other"
            // entity. Trusted the same way the SCREEN_BOUND branch trusts it --
            // this is the destField the engine will actually use, not a guess --
            // and marked paramFromInterp so the catalog's INF-gateway
            // cross-check (field_nav_catalog_mapexits.inl) knows not to
            // distrust it just because this field's INF gateways don't happen
            // to list the same destination.
            int32_t oldParam = info.param;
            info.param = interpDest;
            info.paramFromInterp = true;
            paramUpdates++;
            resolved++;
            Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' (Map Exit): "
                       "param 0x%08X -> 0x%08X [INTERP]",
                       fieldName, ei, info.symName,
                       (unsigned)oldParam, (unsigned)interpDest);
        } else if (anyResolved) {
            resolved++;
            int32_t newParam = (bestLiteral != -1) ? bestLiteral : bestMarker;
            // v0.18.3.265 (#82): adopt the VARBLOCK marker for an UNPOSITIONED
            // cat=3 Others MAP_EXIT whose destination is runtime-supplied.
            //
            // The per-opcode scanner takes mapjumpDestField from a push-stack slot,
            // so for a runtime-variable destination it fabricates a bogus LITERAL
            // (glfurin4 ent4 'rinoa' -> 325 = "Dollet - Mountain Hideout 4"). The
            // resolver already knows better -- it derived VARBLOCK marker
            // 0x800002D6 -- but only rewrote info.param for SCREEN_BOUND lines, so
            // the garbage literal survived. Because 325 is a VALID in-range field
            // id, the catalog's dead-exit suppression (param < 0 || param >=
            // FIELD_DISPLAY_NAMES_COUNT) never fired and a phantom exit to an
            // unrelated map was the only entry in the room.
            //
            // Adopting the marker (bit31 set => negative param) lets that existing
            // suppression drop it. Deliberately narrow -- MAP_EXIT + marker-only
            // (no literal was truly resolved) + !hasPosition -- so no positioned
            // real exit can regress. A positioned runtime-var exit keeps its
            // position and simply degrades to a generic "Exit" label instead of a
            // fabricated destination name, which is also the correct outcome.
            if (info.type == JSM_ENT_MAP_EXIT &&
                bestLiteral == -1 && bestMarker != -1 && !info.hasPosition) {
                int32_t oldParam = info.param;
                info.param = bestMarker;
                paramUpdates++;
                Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' (Map Exit): "
                           "param 0x%08X -> 0x%08X [VARBLOCK adopted -- unpositioned "
                           "runtime-var dest, fabricated literal discarded]",
                           fieldName, ei, info.symName,
                           (unsigned)oldParam, (unsigned)bestMarker);
            } else {
                Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' (%s): "
                           "would-be param 0x%08X%s -- not a SCREEN_BOUND line, "
                           "diagnostic only",
                           fieldName, ei, info.symName,
                           JSMEntityTypeName(info.type),
                           (unsigned)newParam,
                           (bestLiteral != -1) ? " [LITERAL]" :
                           (bestMarker  != -1) ? " [VARBLOCK]" : "");
            }
        } else {
            unresolved++;
        }

#if EXIT_TRACE_DIAG
        // v0.17.9.7 LOCAL survey: for EVERY exit-bearing entity (any type) on the
        // gated fields, run the interpreter with trace capture and log result +
        // bail point next to the abstract decode. Does NOT touch info.param.
        if (mjMethodStart >= 0 && ExitTraceFieldGated(fieldName)) {
            InterpTrace tr; tr.result = 0; tr.bailIp = -1; tr.bailOp = 0; tr.bailSp = 0; tr.reason = -1; tr.steps = 0; tr.nrec = 0;
            int32_t d = SafeInterpretExitMethod(scriptData, scriptDataDwords,
                                                mjMethodStart, mjMethodEnd, &tr);
            static const char* const RZ[] = { "reached-MAPJUMP", "RET", "MAPJUMP-arg-underflow",
                                              "fell-off-range", "step-cap", "CAL/JPF-underflow",
                                              "dest-variable-sourced" };  // v0.18.3.281
            const char* rstr = (tr.reason >= 0 && tr.reason <= 6) ? RZ[tr.reason] : "SEH-fault/none";
            Log::Field("FieldArchive: [EXIT-TRACE] %s ent%d '%s' type=%s interp=%d reason=%s "
                       "bailIp=%d bailOp=0x%02X bailSp=%d steps=%d | abstract lit=%d marker=0x%08X",
                       fieldName, ei, info.symName, JSMEntityTypeName(info.type),
                       (int)d, rstr, tr.bailIp, (unsigned)tr.bailOp, tr.bailSp, tr.steps,
                       (int)bestLiteral, (unsigned)bestMarker);
            // v0.17.9.8: on any bail (interp<0) dump the op walk so the opcode that
            // shorts the stack before the CAL is visible. Format ip:op/spBEFORE.
            if (d < 0) {
                char seq[640]; int n = 0;
                for (int k = 0; k < tr.nrec; k++) {
                    int rem = (int)sizeof(seq) - n; if (rem <= 1) break;
                    int w = snprintf(seq + n, (size_t)rem, "%d:%02X/sp%d ",
                                     tr.rec[k].ip, (unsigned)tr.rec[k].op, tr.rec[k].sp);
                    if (w < 0 || w >= rem) { n = (int)sizeof(seq) - 1; break; }
                    n += w;
                }
                seq[(n >= 0 && n < (int)sizeof(seq)) ? n : (int)sizeof(seq) - 1] = '\0';
                Log::Field("FieldArchive: [EXIT-OPSEQ] %s ent%d '%s' nrec=%d (ip:op/spBEFORE): %s",
                           fieldName, ei, info.symName, tr.nrec, seq);
                // v0.17.9.9: full linear disasm of the bailing method so the
                // tested operands (varblock offset + live value) are visible.
                SafeLinearDisasmExitMethod(fieldName, ei, info.symName,
                                           scriptData, scriptDataDwords,
                                           mjMethodStart, mjMethodEnd);
            }
        }
#endif
    }

    Log::Field("FieldArchive: [MAPJUMP-RES] %s summary: %d MAPJUMP instructions "
               "scanned, %d entities with at least one resolution, %d without, "
               "%d SCREEN_BOUND params updated",
               fieldName, scanned, resolved, unresolved, paramUpdates);
}

}  // namespace MapjumpResolver
