// trigwalk_test.cpp -- v0.21.5 (#79)
//
// The re-implemented world-map entry interpreter, checked against the two cases
// the game itself has already answered: Chocobo Forest 7 opened on demand, and
// Edea's House did not, from a position the screenshot puts at its wall.
//
//   g++ -std=c++17 -O0 -Isrc -o trigwalk_test tests/trigwalk_test.cpp

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

typedef unsigned long DWORD;
static DWORD GetTickCount() { return 0; }
namespace Log { void World(const char*, ...) {} }
static bool WmSafeReadBytes(uintptr_t, void* o, size_t n) { memset(o, 0, n); return true; }
static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z) { *x = 0; *y = 0; *z = 0; }
static uint16_t GetCurrentStoryFlag() { return 912; }
static int GetActiveVehicleId() { return 0; }

// v0.21.6: trigwalk's [ENTRYPATCH] line reads the firing-area table and the
// wrapped-distance helper, so both come in ahead of it.
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = (double)x1 - x2, dy = (double)y1 - y2;
    if (dx >  131072.0) dx -= 262144.0;  if (dx < -131072.0) dx += 262144.0;
    if (dy >   98304.0) dy -= 196608.0;  if (dy <  -98304.0) dy += 196608.0;
    return sqrt(dx*dx + dy*dy);
}
#include "world_map_trigger_data.inl"
#include "world_map_trigeval.inl"
#include "world_map_trigwalk.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

// Walk every program the way the engine does and report the first match.
static const TrigProgram* FirstMatch(const TwInput& in, int* dest, TwVerdict* whyEdea)
{
    const TrigProgram* hit = nullptr;
    for (int i = 0; i < TRIG_PROGRAM_N; i++) {
        int d = -1;
        TwVerdict v = TwEvaluate(TRIG_PROGRAMS[i], in, &d);
        if (TRIG_PROGRAMS[i].idx == 34 && whyEdea) *whyEdea = v;
        if (v == TW_MATCH && !hit) { hit = &TRIG_PROGRAMS[i]; if (dest) *dest = d; }
    }
    return hit;
}

int main()
{
    // Aaron's live state at the orphanage, from the v0.21.2/.3 [TRIGEVAL] lines.
    TwInput edea = { -29585, 70739, 912, 0, 0, false };
    // Chocobo Forest 7, the square next door -- the entry that DID fire.
    TwInput forest = { -20953, 68906, 912, 0, 0, false };

    // 1. THE KNOWN-GOOD. The game opened this one, so the model must too, and it
    //    must be program 35 with destination 38.
    {
        int dest = -1; TwVerdict why = TW_MATCH;
        const TrigProgram* hit = FirstMatch(forest, &dest, &why);
        check(hit != nullptr, "Chocobo Forest 7 does not match any program");
        if (hit) {
            check(hit->idx == 35, "the forest matched the wrong program");
            check(dest == 38, "the forest matched with the wrong destination");
            printf("Chocobo Forest 7 (%d,%d): program %d, destination %d -- the game agrees\n",
                   forest.x, forest.y, hit->idx, dest);
        }
    }

    // 2. THE KNOWN-BAD. The game refused this one; MATCH here means the segment,
    //    story, vehicle and clause gates all pass.
    //
    //    v0.21.6 -- READ THIS BEFORE TRUSTING A MATCH AGAIN. It is not the same
    //    as "the door should have opened". This evaluator's finest positional
    //    unit is the 8192-unit segment, so it reports MATCH across all of
    //    Centra's segment 652 -- which the 2026-08-16 log duly printed at
    //    twenty-three positions spanning 8 km while nothing loaded. The gate it
    //    cannot see is per-POLYGON: `test byte [eax+0x0E], 8`, satisfied by just
    //    7 foot-walkable triangles in that entire square. That gate lives in
    //    s_entryAims and is covered by tests/entryaim_test.cpp. So this case
    //    asserting MATCH is correct and expected; it says the refusal is
    //    positional at a resolution this file deliberately does not model.
    {
        int dest = -1; TwVerdict why = TW_MATCH;
        const TrigProgram* hit = FirstMatch(edea, &dest, &why);
        printf("Edea's House (%d,%d): program 34 verdict = %s",
               edea.x, edea.y, TwVerdictName(why));
        if (hit && hit->idx == 34) printf(", destination %d", dest);
        printf("\n");
        check(why == TW_MATCH,
              "program 34 no longer evaluates to MATCH -- the model changed, re-read it");
    }

    // 3. The gates must actually bite, or a MATCH means nothing.
    {
        int dest = -1;
        TwInput t = edea; t.story = 899;                 // one under the gate
        check(TwEvaluate(TRIG_PROGRAMS[34], t, &dest) == TW_STORY,
              "story 899 did not fail program 34's >= 900 gate");
        t = edea; t.unk21Bit = 1;                        // the bit the operand refuses
        check(TwEvaluate(TRIG_PROGRAMS[34], t, &dest) == TW_UNK21,
              "UNK21 bit 1 did not fail program 34");
        t = edea; t.unk21Bit = 1; t.unk21Skip = true;    // ...unless the skip is set
        check(TwEvaluate(TRIG_PROGRAMS[34], t, &dest) == TW_MATCH,
              "the UNK21 skip did not bypass the bit");
        t = edea; t.vehId = 50;                          // in the Ragnarok
        check(TwEvaluate(TRIG_PROGRAMS[34], t, &dest) == TW_NO_CLAUSE,
              "program 34 accepted a Ragnarok");
        t = edea; t.x = -23150; t.y = 62853;             // the pre-v0.21.1 marker
        check(TwEvaluate(TRIG_PROGRAMS[34], t, &dest) == TW_SEGMENT,
              "the old marker still matches program 34's segment");
        printf("gates bite: story, UNK21, the UNK21 skip, vehicle and segment all refuse\n");
    }

    // 4. Programs must not overlap: one square, one entry. If two ever matched
    //    the same spot, "first match wins" would be silently choosing.
    {
        int seen[1024]; memset(seen, 0, sizeof(seen));
        int dup = 0;
        for (int i = 0; i < TRIG_PROGRAM_N; i++) {
            const int s = TRIG_PROGRAMS[i].seg;
            if (s >= 0 && s < 1024) { if (seen[s]) dup++; seen[s]++; }
        }
        // Segment 370 legitimately carries three programs (one per vehicle).
        check(dup == 2, "unexpected number of programs sharing a segment");
        printf("program table: %d programs, %d clauses, only segment 370 is shared "
               "(its three vehicle variants)\n",
               TRIG_PROGRAM_N, (int)(sizeof(TRIG_CLAUSES)/sizeof(TRIG_CLAUSES[0])));
    }

    // 5. Coordinate bounds are offsets INSIDE the segment, masked with 0x1FFF --
    //    including for negative world coordinates, which is where a sign error
    //    would hide.
    {
        int dest = -1;
        // Program 24 (segment 393) splits its square at x offset 6144.
        const TrigProgram& p = TRIG_PROGRAMS[24];
        int32_t x0, x1, y0, y1;
        WmSegmentBounds(p.seg, &x0, &x1, &y0, &y1);
        TwInput lo = { x0 + 100,  y0 + 100, 5000, 0, -1, false };
        TwInput hi = { x0 + 7000, y0 + 100, 5000, 0, -1, false };
        check(TwEvaluate(p, lo, &dest) == TW_MATCH, "low side of segment 393 refused");
        int destLo = dest;
        check(TwEvaluate(p, hi, &dest) == TW_MATCH, "high side of segment 393 refused");
        check(destLo != dest, "both sides of segment 393 gave the same destination");
        printf("coordinate split: segment %d gives destination %d on the low side and "
               "%d on the high side\n", p.seg, destLo, dest);
    }

    printf("trigwalk: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
