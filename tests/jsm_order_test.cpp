// jsm_order_test -- the JSM group/slot/SYM ordering map, checked against the
// game's own files.
//
// WHY THIS EXISTS
// ---------------
// From v0.07.73 until v0.58.0 the scanner assumed the JSM group-word array ran
// Door, Line, Background, Other and that the SYM bare list ran Line, Background,
// Other. field_scripts_init (0x0052BC00) says otherwise on both counts, and the
// cost was concrete: on ecoway1 all 35 entity names were rotated by nBackgrounds,
// and on the 79 fields carrying both doors and lines the first min(nD,nL) groups
// had Door and Line swapped outright.
//
// The check below does NOT restate the derivation. Each fixture carries the two
// halves of the real .sym -- the leading bare-name list and the method-section
// declaration order -- which the game generated independently of each other. If
// symIdx[] is right then bare[symIdx[g]] is the same string as the method
// section's name for group g, and if any part of the ordering is wrong the two
// halves stop lining up. The group words are the real ones off the disc.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include "jsm_order_fixtures.h"

// The unit under test, lifted in whole. It has no dependencies of its own.
#include "field_archive_jsm_order.inl"

static int bad = 0;
static void fail(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    std::printf("  BAD: "); std::vprintf(fmt, ap); std::printf("\n");
    va_end(ap); bad++;
}

// ---------------------------------------------------------------------------
// 1. Every fixture: the two independent halves of its SYM must agree.
// ---------------------------------------------------------------------------
static void checkFixture(const JsmOrderFixture& F)
{
    int starts[JSM_ORDER_MAX];
    for (int g = 0; g < F.ngrp; g++) starts[g] = (int)(F.words[g] >> 7);

    JSMOrderMap o;
    BuildJSMOrderMap(F.nD, F.nL, F.nB, F.nO, F.ngrp, starts, o);
    if (!o.valid) { fail("%s: order map did not build", F.name); return; }

    const int nbare = F.nO + F.nL + F.nB;
    int seenSym[JSM_ORDER_MAX] = {};
    int seenSlot[JSM_ORDER_MAX] = {};

    for (int g = 0; g < F.ngrp; g++) {
        // --- category: the group word's method count must match the method
        // count the SYM lists for whatever name we resolve to. ---
        const int wantMeth = (int)(F.words[g] & 0x7F);
        if (F.declMeth[g] != wantMeth)
            fail("%s g%d: fixture self-check -- group word says %d methods, SYM lists %d",
                 F.name, g, wantMeth, F.declMeth[g]);

        // --- slot: in range and unique ---
        const int s = o.slot[g];
        if (s < 0 || s >= F.ngrp) { fail("%s g%d: slot %d out of range", F.name, g, s); continue; }
        if (seenSlot[s]++) fail("%s g%d: slot %d already taken", F.name, g, s);
        if (o.groupOfSlot[s] != g)
            fail("%s g%d: groupOfSlot[%d] = %d, not %d", F.name, g, s, (int)o.groupOfSlot[s], g);

        // --- doors carry no SYM name ---
        if (o.cat[g] == 0) {
            if (o.symIdx[g] != -1) fail("%s g%d: door has symIdx %d, expected -1",
                                        F.name, g, (int)o.symIdx[g]);
            continue;
        }

        // --- THE CHECK: bare[symIdx[g]] == the method section's name for g ---
        const int si = o.symIdx[g];
        if (si < 0 || si >= nbare) { fail("%s g%d: symIdx %d out of 0..%d", F.name, g, si, nbare-1); continue; }
        if (seenSym[si]++) fail("%s g%d: symIdx %d already taken", F.name, g, si);
        if (strcmp(F.bare[si], F.declName[g]) != 0)
            fail("%s g%d (cat %d, start %d): SYM bare[%d] = '%s' but the method section "
                 "names this group '%s'", F.name, g, (int)o.cat[g], starts[g], si,
                 F.bare[si], F.declName[g]);
    }

    // Within a category the engine walks the group pointer and the table index
    // forward together (0x0052BFD9 `add ebp,2` / 0x0052BFE2 `add edx,4`), so slot
    // order must follow group order inside each block -- reversing or shuffling a
    // block would still produce the right categories in the right ranges.
    for (int c = 0; c < 4; c++) {
        int prevG = -1, prevS = -1;
        for (int g = 0; g < F.ngrp; g++) {
            if (o.cat[g] != c) continue;
            if (prevG >= 0 && o.slot[g] != prevS + 1)
                fail("%s: category %d groups %d,%d map to slots %d,%d -- a block's "
                     "slots must run consecutively in group order",
                     F.name, c, prevG, g, prevS, (int)o.slot[g]);
            prevG = g; prevS = o.slot[g];
        }
    }

    // every bare name must have been claimed exactly once
    for (int i = 0; i < nbare; i++)
        if (seenSym[i] != 1) fail("%s: bare name %d ('%s') claimed %d times",
                                  F.name, i, F.bare[i], seenSym[i]);
}

// ---------------------------------------------------------------------------
// 2. The two orderings, stated as the exe states them, on a field that has all
//    four categories. escouse1: D=2 L=2 B=2 O=15.
// ---------------------------------------------------------------------------
static void checkOrderingShape()
{
    const JsmOrderFixture* F = nullptr;
    for (int i = 0; i < JSM_ORDER_FIXTURE_COUNT; i++)
        if (strcmp(JSM_ORDER_FIXTURES[i].name, "escouse1") == 0) F = &JSM_ORDER_FIXTURES[i];
    if (!F) { fail("escouse1 fixture missing"); return; }
    if (!(F->nD > 0 && F->nL > 0 && F->nB > 0 && F->nO > 0))
        fail("escouse1 no longer exercises all four categories (D=%d L=%d B=%d O=%d)",
             F->nD, F->nL, F->nB, F->nO);

    int starts[JSM_ORDER_MAX];
    for (int g = 0; g < F->ngrp; g++) starts[g] = (int)(F->words[g] >> 7);
    JSMOrderMap o;
    BuildJSMOrderMap(F->nD, F->nL, F->nB, F->nO, F->ngrp, starts, o);

    // group array: Lines, Doors, Backgrounds, Others  (0x0052BF32 / C02A / C13B / C270)
    for (int g = 0; g < F->ngrp; g++) {
        int want;
        if      (g < F->nL)                       want = 1;  // Line
        else if (g < F->nL + F->nD)               want = 0;  // Door
        else if (g < F->nL + F->nD + F->nB)       want = 2;  // Background
        else                                      want = 3;  // Other
        if (o.cat[g] != want)
            fail("escouse1 g%d: cat %d, expected %d (group array is L,D,B,O)",
                 g, (int)o.cat[g], want);
    }
    // runtime table: Others, Lines, Backgrounds, Doors  (indices at BF44/C050/C155/C279)
    for (int s = 0; s < F->ngrp; s++) {
        int g = o.groupOfSlot[s];
        int want;
        if      (s < F->nO)                       want = 3;  // Other
        else if (s < F->nO + F->nL)               want = 1;  // Line
        else if (s < F->nO + F->nL + F->nB)       want = 2;  // Background
        else                                      want = 0;  // Door
        if (g < 0 || o.cat[g] != want)
            fail("escouse1 slot%d: group %d cat %d, expected cat %d "
                 "(runtime table is O,L,B,D)", s, g, g >= 0 ? (int)o.cat[g] : -1, want);
    }
}

// ---------------------------------------------------------------------------
// 3. ecoway1 -- the field the old mapping got completely wrong. It has no doors
//    and no lines, so group order is B(10) then O(25) while the SYM bare list is
//    O(25) then B(10): the old `symIdx = e - countDoors` rotated every name by
//    ten. Pin the rotation so the regression cannot come back quietly.
// ---------------------------------------------------------------------------
static void checkEcoway1()
{
    const JsmOrderFixture* F = nullptr;
    for (int i = 0; i < JSM_ORDER_FIXTURE_COUNT; i++)
        if (strcmp(JSM_ORDER_FIXTURES[i].name, "ecoway1") == 0) F = &JSM_ORDER_FIXTURES[i];
    if (!F) { fail("ecoway1 fixture missing"); return; }
    int starts[JSM_ORDER_MAX];
    for (int g = 0; g < F->ngrp; g++) starts[g] = (int)(F->words[g] >> 7);
    JSMOrderMap o;
    BuildJSMOrderMap(F->nD, F->nL, F->nB, F->nO, F->ngrp, starts, o);

    // group 0 is a Background; the OLD code would have named it bare[0].
    if (o.cat[0] != 2) fail("ecoway1 g0 should be a Background, got cat %d", (int)o.cat[0]);
    if (o.symIdx[0] != F->nO)
        fail("ecoway1 g0: symIdx %d, expected %d (first Background sits after all %d Others)",
             (int)o.symIdx[0], F->nO, F->nO);
    if (strcmp(F->bare[o.symIdx[0]], F->declName[0]) != 0)
        fail("ecoway1 g0 resolves to '%s', method section says '%s'",
             F->bare[o.symIdx[0]], F->declName[0]);
    // and the old answer must now be WRONG -- if bare[0] still matched, the
    // fixture would not be exercising the bug.
    if (strcmp(F->bare[0], F->declName[0]) == 0)
        fail("ecoway1 no longer distinguishes the old mapping from the new one");
    // the first Other (group nB) must be runtime slot 0 and SYM index 0.
    if (o.slot[F->nB] != 0 || o.symIdx[F->nB] != 0)
        fail("ecoway1: first Other is group %d -> slot %d symIdx %d, expected 0/0",
             F->nB, (int)o.slot[F->nB], (int)o.symIdx[F->nB]);
}

// ---------------------------------------------------------------------------
// 4. Backgrounds are the one category whose group order is NOT code order (48
//    fields). bcport2a is one: its bg groups run start 77, 72, 81. Ranking by
//    start is what makes the SYM line up, so prove the fixture needs it.
// ---------------------------------------------------------------------------
static void checkPermutedBackgrounds()
{
    const JsmOrderFixture* F = nullptr;
    for (int i = 0; i < JSM_ORDER_FIXTURE_COUNT; i++)
        if (strcmp(JSM_ORDER_FIXTURES[i].name, "bcport2a") == 0) F = &JSM_ORDER_FIXTURES[i];
    if (!F) { fail("bcport2a fixture missing"); return; }
    const int bgBase = F->nL + F->nD;
    bool permuted = false;
    for (int k = 1; k < F->nB; k++)
        if ((F->words[bgBase + k] >> 7) < (F->words[bgBase + k - 1] >> 7)) permuted = true;
    if (!permuted)
        fail("bcport2a background groups are in code order -- fixture no longer "
             "exercises the per-category start ranking");

    int starts[JSM_ORDER_MAX];
    for (int g = 0; g < F->ngrp; g++) starts[g] = (int)(F->words[g] >> 7);
    JSMOrderMap o;
    BuildJSMOrderMap(F->nD, F->nL, F->nB, F->nO, F->ngrp, starts, o);
    for (int k = 0; k < F->nB; k++) {
        int g = bgBase + k;
        if (strcmp(F->bare[o.symIdx[g]], F->declName[g]) != 0)
            fail("bcport2a bg g%d (start %d): bare[%d]='%s' vs method section '%s'",
                 g, starts[g], (int)o.symIdx[g], F->bare[o.symIdx[g]], F->declName[g]);
    }
}

// ---------------------------------------------------------------------------
// 5. Refuse malformed input rather than indexing off the end.
// ---------------------------------------------------------------------------
static void checkGuards()
{
    JSMOrderMap o;
    int starts[4] = {0,1,2,3};
    BuildJSMOrderMap(1, 1, 1, 1, 5, starts, o);        // counts do not sum to total
    if (o.valid) fail("guard: accepted D+L+B+O != total");
    BuildJSMOrderMap(0, 0, 0, 0, 0, starts, o);        // empty
    if (o.valid) fail("guard: accepted total == 0");
    BuildJSMOrderMap(0, 0, 0, 200, 200, starts, o);    // over the array bound
    if (o.valid) fail("guard: accepted total > JSM_ORDER_MAX");
    BuildJSMOrderMap(-1, 1, 1, 1, 2, starts, o);       // negative count
    if (o.valid) fail("guard: accepted a negative category count");
}

int main()
{
    std::printf("jsm_order_test: %d fixtures\n", JSM_ORDER_FIXTURE_COUNT);
    if (JSM_ORDER_FIXTURE_COUNT < 10) fail("fixture set shrank below 10 fields");
    for (int i = 0; i < JSM_ORDER_FIXTURE_COUNT; i++) checkFixture(JSM_ORDER_FIXTURES[i]);
    checkOrderingShape();
    checkEcoway1();
    checkPermutedBackgrounds();
    checkGuards();
    if (bad) { std::printf("jsm_order_test: %d FAILURES\n", bad); return 1; }
    std::printf("jsm_order_test: OK\n");
    return 0;
}
