// jsm_decode_test -- the field-script VM instruction encoding, checked against
// real bytecode and the geometry it has to produce.
//
// WHY THIS EXISTS
// ---------------
// From v0.07.x to v0.58.x the scanner read a word whose high byte is zero as a
// LITERAL PUSH and opcode 0x07 as PSHM_W, "push a word from memory". Both are
// wrong. A high-byte-zero word is an opcode (that is how everything above 0xFF
// is encoded), and 0x07 is PSHN_L, which pushes its own inline parameter. The
// cost was that any SET3 with a non-negative coordinate -- 8289 of the 8625 on
// the disc -- was discarded as "coordinates come from runtime memory", so 96% of
// the game's entity positions were unavailable statically.
//
// The check below is not a restatement of the decode. Each SET3 case carries the
// four real instruction words AND the three vertices of the walkmesh triangle
// that the SET3's own inline parameter names. The coordinates are popped values
// and the triangle is the parameter: they agree only if BOTH are read correctly,
// and the test asserts the decoded point lies inside that triangle. It also
// asserts that the OLD reading does not -- so a revert cannot pass quietly.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include "jsm_decode_fixtures.h"
#include "field_archive_jsm_decode.inl"

static int bad = 0;
static void fail(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    std::printf("  BAD: "); std::vprintf(fmt, ap); std::printf("\n");
    va_end(ap); bad++;
}

// Same-side test. Vertices are (x, y, z) triples; the field plane is x/y.
static bool inside(const short* v, int px, int py)
{
    long d1 = (long)(px - v[3]) * (v[1] - v[4]) - (long)(v[0] - v[3]) * (py - v[4]);
    long d2 = (long)(px - v[6]) * (v[4] - v[7]) - (long)(v[3] - v[6]) * (py - v[7]);
    long d3 = (long)(px - v[0]) * (v[7] - v[1]) - (long)(v[6] - v[0]) * (py - v[1]);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

// The extraction the scanner performs, in miniature: walk the contiguous run of
// argument pushes before the consumer and take the last three literals.
static bool extractSet3(const unsigned int* w, int n, int* x, int* y, int* tri)
{
    int lits[8]; int nl = 0;
    for (int i = 0; i < n - 1; i++) {
        JsmInsn p = JsmDecodeWord(w[i]);
        if (!JsmIsArgPush(p)) { nl = 0; continue; }
        if (!JsmIsLiteralPush(p)) { if (nl < 8) lits[nl++] = 0x7FFFFFFF; continue; }
        if (nl < 8) lits[nl++] = p.param;
    }
    JsmInsn op = JsmDecodeWord(w[n - 1]);
    if (op.opcode != 0x1E || op.bare) return false;
    if (nl < 3) return false;
    if (lits[nl - 3] == 0x7FFFFFFF || lits[nl - 2] == 0x7FFFFFFF) return false;
    *x = lits[nl - 3]; *y = lits[nl - 2]; *tri = op.param;
    return true;
}

// ---------------------------------------------------------------------------
// 1. Every SET3 case must land inside the triangle its own parameter names.
// ---------------------------------------------------------------------------
static void checkSet3()
{
    for (int i = 0; i < SET3_CASE_COUNT; i++) {
        const Set3Case& c = SET3_CASES[i];
        int x = 0, y = 0, tri = -1;
        if (!extractSet3(c.w, 4, &x, &y, &tri)) {
            fail("%s '%s': SET3 words did not decode to three literal coordinates "
                 "(0x%08X 0x%08X 0x%08X 0x%08X)",
                 c.field, c.ent, c.w[0], c.w[1], c.w[2], c.w[3]);
            continue;
        }
        if (tri != c.tri)
            fail("%s '%s': triangle %d, expected %d -- the inline parameter of SET3 "
                 "(0x0051D780 writes it to [ctx+0x1FA]) is the walkmesh triangle",
                 c.field, c.ent, tri, c.tri);
        if (!inside(c.v, x, y))
            fail("%s '%s': decoded (%d,%d) is outside triangle %d "
                 "[(%d,%d) (%d,%d) (%d,%d)]",
                 c.field, c.ent, x, y, c.tri,
                 c.v[0], c.v[1], c.v[3], c.v[4], c.v[6], c.v[7]);
    }
}

// ---------------------------------------------------------------------------
// 2. The OLD reading must NOT reproduce them. If it did, the fixtures would not
//    be discriminating and the regression could return unnoticed.
// ---------------------------------------------------------------------------
static void checkOldModelIsRefuted()
{
    int oldWouldResolve = 0;
    for (int i = 0; i < SET3_CASE_COUNT; i++) {
        const Set3Case& c = SET3_CASES[i];
        // Pre-v0.59.0: 0x07 was PSHM_W and only a NEGATIVE parameter was treated
        // as a literal; a non-negative one became a "value comes from memory"
        // marker and the placement was thrown away.
        bool allNegative = true;
        for (int k = 0; k < 3; k++) {
            JsmInsn p = JsmDecodeWord(c.w[k]);
            if (p.opcode != 0x07 || p.bare) { allNegative = false; break; }
            if (p.param >= 0) { allNegative = false; break; }
        }
        if (allNegative) oldWouldResolve++;
    }
    if (oldWouldResolve == SET3_CASE_COUNT)
        fail("every fixture has all-negative coordinates, so the old PSHM_W reading "
             "would have resolved them too -- these cases prove nothing");
    std::printf("jsm_decode_test: the pre-v0.59.0 reading resolves %d of %d SET3 cases\n",
                oldWouldResolve, SET3_CASE_COUNT);
}

// ---------------------------------------------------------------------------
// 3. MAPJUMP / MAPJUMP3: the destination is the DEEPEST argument, not the
//    opcode's inline parameter. 0x00521AC0 pops five and writes the last into
//    0x01CE4762; the inline parameter goes to 0x01CE476C, which is the entrance
//    id. Measured over the whole disc, the deepest argument names a field in the
//    same area 1270 times out of 1993 while the inline parameter manages 81.
// ---------------------------------------------------------------------------
static void checkJumps()
{
    for (int i = 0; i < JUMP_CASE_COUNT; i++) {
        const JumpCase& c = JUMP_CASES[i];
        int lits[8]; int nl = 0;
        for (int k = 0; k < c.n; k++) {
            JsmInsn p = JsmDecodeWord(c.w[k]);
            if (!JsmIsLiteralPush(p)) { nl = 0; break; }
            lits[nl++] = p.param;
        }
        if (nl != c.n) { fail("%s '%s': jump arguments did not decode", c.field, c.ent); continue; }
        JsmInsn op = JsmDecodeWord(c.w[c.n]);
        if (op.opcode != 0x29 && op.opcode != 0x2A)
            fail("%s '%s': consumer decoded as 0x%03X, expected MAPJUMP or MAPJUMP3",
                 c.field, c.ent, op.opcode);
        if (lits[0] != c.dest)
            fail("%s '%s': destination %d, expected %d (%s)",
                 c.field, c.ent, lits[0], c.dest, c.destName);
        if (op.param == c.dest)
            fail("%s '%s': the inline parameter equals the destination, so this case "
                 "cannot tell the two readings apart", c.field, c.ent);
    }
}

// ---------------------------------------------------------------------------
// 4. The encoding itself, on values taken from the fixtures and from the exe.
// ---------------------------------------------------------------------------
static void checkEncoding()
{
    JsmInsn a = JsmDecodeWord(0x0000012Eu);          // MENUSAVE, a bare word
    if (!a.bare || a.opcode != 0x12E || a.param != 0)
        fail("bare word 0x0000012E decoded as opcode 0x%03X param %d bare=%d -- an "
             "opcode above 0xFF is encoded with a zero high byte and carries no parameter",
             a.opcode, a.param, a.bare ? 1 : 0);
    JsmInsn b = JsmDecodeWord(0x07FFF4E2u);          // PSHN_L -2846
    if (b.bare || b.opcode != 0x07 || b.param != -2846)
        fail("0x07FFF4E2 decoded as opcode 0x%03X param %d -- PSHN_L's parameter is "
             "a sign-extended 24-bit value", b.opcode, b.param);
    if (!JsmIsLiteralPush(b)) fail("PSHN_L is not being treated as a literal push");
    JsmInsn c = JsmDecodeWord(0x0C0002A0u);          // PSHM_W from the var bank
    if (!JsmIsArgPush(c) || JsmIsLiteralPush(c))
        fail("PSHM_W must be an argument push whose value is NOT statically known");
    JsmInsn d = JsmDecodeWord(0x05000008u);          // prologue: saves eight locals
    if (!JsmIsNonArgPush(d)) fail("the 0x05 prologue must terminate an argument run");
    JsmInsn e = JsmDecodeWord(0x0B0002B0u);          // POPM_B
    if (!JsmIsVarBankPop(e)) fail("0x0B POPM_B must count as a variable-bank write");
    JsmInsn f = JsmDecodeWord(0x080000FFu);          // PSHL: a local, not a var write
    if (JsmIsVarBankPop(f))
        fail("0x08 is PSHL -- a PUSH of a local. Treating it as a savemap write is "
             "the pre-v0.59.0 bug that fed foundNonInitVarWrite.");
    if (!JsmIsRuntimeMarker(JsmRuntimeMarker(0x2A0)))
        fail("runtime marker does not round-trip");
    if (JsmIsRuntimeMarker(-2846))
        fail("a negative literal must not read as a runtime marker");
}

int main()
{
    std::printf("jsm_decode_test: %d SET3 cases, %d jump cases\n",
                SET3_CASE_COUNT, JUMP_CASE_COUNT);
    if (SET3_CASE_COUNT < 20) fail("SET3 fixture set shrank below 20");
    if (JUMP_CASE_COUNT < 10) fail("jump fixture set shrank below 10");
    checkSet3();
    checkOldModelIsRefuted();
    checkJumps();
    checkEncoding();
    if (bad) { std::printf("jsm_decode_test: %d FAILURES\n", bad); return 1; }
    std::printf("jsm_decode_test: OK\n");
    return 0;
}
