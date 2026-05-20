// field_archive_jsm_mapjump_resolver.inl - v0.17.7.5.2
//
// Static destField resolver for MAPJUMP-family instructions. Runs as a
// follow-up pass after ScanJSMScripts() main entity scan. For each entity
// classified as JSM_ENT_LINE_SCREEN_BOUND, walks the entity's bytecode
// method-by-method to find the actual destField source of every MAPJUMP /
// MAPJUMP3 / DISCJUMP / MAPJUMPO instruction.
//
// Why this exists:
//   The forward scanner in field_archive_jsm_scan.inl accumulates pushCount
//   across basic block boundaries and does not model the stack effect of
//   most opcodes. By the time it reaches a MAPJUMP3 at the end of a complex
//   walk-on method, pushStack[pushCount - 5] is far away from the actual
//   5th-most-recent push the engine consumes. v0.17.7.4 BAT confirmed:
//   scanner-reported destField PSHM addresses (0x0002, 0x023A, 0x01F6) hold
//   values (14381, 0, 0) at MAPJUMP3 fire time that don't match the actual
//   destination (field 170 then 165).
//
// What this fixes:
//   Per-method bytecode simulation with proper basic-block awareness:
//     1. Pre-build the set of jump-target IPs within the method.
//     2. Forward-walk from method start to the MAPJUMP3 instruction.
//     3. Reset the simulated stack at every jump target (basic block start) --
//        only pushes that reach MAPJUMP3 through its own basic block count.
//     4. Model the stack effect of every known opcode; unknown opcodes are
//        treated as no-op (safer than guessing wrong).
//     5. At the MAPJUMP instruction, inspect the deepest of the top-N stack
//        values. If LITERAL -> destField is the literal. If PSHM_W ref ->
//        store 0x80000000 | addr (downstream [PSHM-DEST] resolves via the
//        live varblock at field load).
//
// Output:
//   Per-line MAPJUMP-RES log entries showing the resolved destField (literal
//   or marker), and a summary line per field. info.param is rewritten in
//   place for each successfully resolved entity. The existing PSHM-DEST
//   resolution in HookedFieldScriptsInit then reads info.param and resolves
//   PSHM markers via varblock at field load.
//
// Cross-reference:
//   Stack effect verified by FF8_EN.exe disassembly:
//     opcode_mapjump  @ 0x00521A20 pops 4 (destField is 4th-from-top)
//     opcode_mapjump3 @ 0x00521AC0 pops 5 (destField is 5th-from-top)
//     Both store destField at 0x01CE4762 (transition request global).
//
// Included from field_archive_jsm.inl AFTER scan.inl. Do not compile
// independently.

namespace MapjumpResolver {

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

        // Default: assume no stack effect. Safer than guessing wrong. If a
        // real-world MAPJUMP-RES result later shows arithmetic-mediated
        // unknown-destField cases we can extend this table from BAT logs.
        default:   return {0, 0};
    }
}

// Abstract value tracked on the simulated stack.
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

// v0.17.7.5.2: Dump 9 bytecode words around a MAPJUMP-family instruction
// (ip-7 through ip+1). One log line per MAPJUMP3 scanned. Used to decode the
// basic-block structure around each MAPJUMP3 by hand and identify the
// unmodeled push opcode that causes the underflow at MAPJUMP3 #2 in each
// SCREEN_BOUND line's method 7.
static void DumpBytecodeContext(const uint32_t* scriptData, int scriptDataDwords,
                                int targetIp, const char* fieldName,
                                int entityIdx, const char* symName, int methodIdx)
{
    char buf[512] = {};
    int off = 0;
    for (int rel = -7; rel <= 1 && off < 480; rel++) {
        int ip = targetIp + rel;
        if (ip < 0 || ip >= scriptDataDwords) {
            off += snprintf(buf + off, 512 - off, "%+d=OOB ", rel);
            continue;
        }
        uint32_t w = scriptData[ip];
        const char* mark = (rel == 0) ? "*" : "";
        off += snprintf(buf + off, 512 - off, "%s%+d=0x%08X ",
                        mark, rel, (unsigned)w);
    }
    Log::Field("FieldArchive: [MAPJUMP-CTX] %s ent%d '%s' m%d ip=%d ctx: %s",
               fieldName, entityIdx, symName, methodIdx, targetIp, buf);
}

// Resolve the destField source of a single MAPJUMP-family instruction by
// forward-walking the method bytecode and inspecting the simulated stack at
// the moment of the instruction.
//
//   scriptData       - whole JSM script data section as native LE dwords
//   methodStart/End  - dword indices bounding this method's bytecode
//   targetIp         - dword index of the MAPJUMP* instruction
//   argCount         - how many args the opcode pops (4 for MAPJUMP, 5 for MAPJUMP3)
//   fieldName/ent... - for log labeling
//
// Returns either:
//   * a positive literal field ID  (info.param can adopt directly)
//   * a 0x80000000 | addr marker   (downstream [PSHM-DEST] resolves)
//   * -1 on failure (stack underflow or unresolvable due to unmodeled chain)
static int32_t ResolveMapjumpDest(const uint32_t* scriptData,
                                  int methodStart, int methodEnd,
                                  int targetIp, int argCount,
                                  const char* fieldName, int entityIdx,
                                  const char* symName, int methodIdx)
{
    // Pre-build jump-target set for this method. Cap at 1024 -- big enough
    // for any single method we've observed.
    static const int MAX_TARGETS = 1024;
    int targets[MAX_TARGETS];
    int numTargets = BuildJumpTargets(scriptData, methodStart, methodEnd,
                                       targets, MAX_TARGETS);

    // Simulated stack. Wider than the forward scanner (32 slots vs 8) so
    // intra-block underestimates are less common. We still reset at jump
    // targets to keep cross-block contamination out.
    static const int STACK_MAX = 32;
    Value stack[STACK_MAX];
    int sp = 0;
    auto push = [&](Value v) {
        if (sp < STACK_MAX) {
            stack[sp++] = v;
        } else {
            // Shift out oldest -- matches the forward scanner's overflow policy.
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
        // Reset stack at basic-block boundaries (jump targets). Skip the
        // reset at methodStart -- method entry is reached with an empty
        // stack, no contamination possible.
        if (ip != methodStart && IsJumpTarget(targets, numTargets, ip)) {
            sp = 0;
        }
        uint32_t word = scriptData[ip];
        uint8_t hb = (uint8_t)(word >> 24);
        if (hb == 0) {
            // PSHN_L: push literal value. High byte == 0 means the entire
            // dword IS the literal (max 0x00FFFFFF since high byte must be 0).
            Value v = { VK_LITERAL, (int32_t)word };
            push(v);
            continue;
        }

        int32_t param = (int32_t)(word & 0x00FFFFFF);
        if (word & 0x00800000) param |= (int32_t)0xFF000000;  // sign-extend

        if (ip == targetIp) {
            // At the MAPJUMP instruction itself, don't apply its stack
            // effect -- we want to inspect the args it WILL pop.
            break;
        }

        if (hb == 0x07 || hb == 0x09 || hb == 0x0A || hb == 0x0C || hb == 0x0D) {
            // PSHM family (W/B/L + savemap variants): track as varblock ref.
            // The downstream PSHM-DEST resolver handles varblock lookup.
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

// Public entry point: run the resolver pass over EVERY entity in the JSM.
// v0.17.7.5.1: widened from SCREEN_BOUND-only to all entity types. v0.17.7.5
// BAT confirmed the engine fires MAPJUMP3 from entities OTHER than the four
// SCREEN_BOUND lines we were scanning (Event Trigger lines, Background
// scripts, or Other entities reached via REQ). The 4 destFields the engine
// resolved (165, 227, 224, 170) didn't match any varblock value at the
// addresses my v0.17.7.5 resolver picked, which means those 4 SCREEN_BOUND
// lines aren't the ones firing. We need to scan everywhere and log all
// MAPJUMP instructions found so we can identify the actual firing entity.
//
// info.param is still ONLY overwritten for JSM_ENT_LINE_SCREEN_BOUND --
// other entities have their own classification semantics. The log lines
// produced for non-SCREEN_BOUND entities are pure diagnostic.
//
// v0.17.7.5.1 also adds a LITERAL-preference policy: when an entity has
// multiple MAPJUMP-family instructions across its methods that all resolve
// successfully, prefer a LITERAL result over a VARBLOCK marker. The
// LITERAL is statically self-contained and doesn't need a downstream
// varblock lookup at field load; the VARBLOCK marker risks reading 0
// (or a stale value) at the field-load lifecycle point.
//
//   methodStartIdxs/methodCounts/totalEntities describe the EntityGroup
//   layout the scan loop built up. Caller passes them as parallel arrays so
//   this resolver doesn't have to share the function-local EntityGroup type.
//   methodCounts[e] is the INCLUSIVE max method index offset (matching the
//   `m <= methodCount` loop convention used inside ScanJSMScripts).
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

        // v0.17.7.5.1: collect ALL successful resolutions for this entity,
        // then pick the best (LITERAL preferred over VARBLOCK).
        int32_t bestLiteral = -1;     // first LITERAL resolved (>= 0)
        int32_t bestMarker  = -1;     // first VARBLOCK marker (0x8000xxxx)
        bool    anyResolved = false;

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
                // v0.17.7.5.2: Dump bytecode context (9 dwords) per MAPJUMP3.
                // One log line per scanned instruction; lets us decode by
                // hand which opcode in the gap between MAPJUMP3 #1 and #2
                // is the unmodeled push causing the underflow.
                DumpBytecodeContext(scriptData, scriptDataDwords, ip,
                                    fieldName, ei, info.symName, m);
                int32_t r = ResolveMapjumpDest(scriptData, mStart, mEnd, ip,
                                                argCount, fieldName, ei,
                                                info.symName, m);
                if (r == -1) continue;
                anyResolved = true;
                if (((uint32_t)r & 0x80000000u) == 0) {
                    // LITERAL (positive int) -- take the first one.
                    if (bestLiteral == -1) bestLiteral = r;
                } else {
                    // VARBLOCK marker -- take the first one.
                    if (bestMarker == -1) bestMarker = r;
                }
            }
        }

        if (anyResolved) {
            resolved++;
            // LITERAL wins over VARBLOCK marker.
            int32_t newParam = (bestLiteral != -1) ? bestLiteral : bestMarker;

            // Only overwrite info.param for SCREEN_BOUND lines -- the
            // existing downstream [PSHM-DEST] / catalog labeling path treats
            // info.param as the destination field for those. Other entity
            // types have their own param semantics; we log only.
            if (info.type == JSM_ENT_LINE_SCREEN_BOUND) {
                int32_t oldParam = info.param;
                info.param = newParam;
                paramUpdates++;
                Log::Field("FieldArchive: [MAPJUMP-RES] %s ent%d '%s' (SCREEN_BOUND): "
                           "param 0x%08X -> 0x%08X%s",
                           fieldName, ei, info.symName,
                           (unsigned)oldParam, (unsigned)newParam,
                           (bestLiteral != -1) ? " [LITERAL]" :
                           (bestMarker  != -1) ? " [VARBLOCK]" : "");
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
            // No MAPJUMP-family instruction in this entity (or all underflowed).
            // Most entities fall here -- it's silent unless we actually saw
            // a MAPJUMP that failed to resolve. The per-instruction
            // "stack underflow" / "UNKNOWN destField" lines already log
            // each failure case so no per-entity log line is needed here.
            unresolved++;
        }
    }
    Log::Field("FieldArchive: [MAPJUMP-RES] %s summary: %d MAPJUMP instructions "
               "scanned, %d entities with at least one resolution, %d without, "
               "%d SCREEN_BOUND params updated",
               fieldName, scanned, resolved, unresolved, paramUpdates);
}

}  // namespace MapjumpResolver
