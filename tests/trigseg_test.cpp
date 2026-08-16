// trigseg_test.cpp -- v0.21.2 (#79)
//
// The world-map entry trigger's position test, transcribed from FF8_EN.exe's
// sub_553910 and checked against points whose segment the mod has already
// printed in its own logs.
//
//   g++ -std=c++17 -O0 -Isrc -o trigseg_test tests/trigseg_test.cpp

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
// v0.21.5: TriggerEvalTick now also calls the program walk, which lives in the
// file included after this one. This probe only exercises the segment
// arithmetic, so a stub satisfies the link without pulling the table in.
static void LogTriggerWalk(const char*) {}

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

int main()
{
    // 1. The transcription must reproduce segment numbers the mod has already
    //    printed from its own, independently written, segment arithmetic.
    //    (-23150,62853) prints as seg(13,19) in every PLAN-DEBUG line of the
    //    2026-08-15 logs, and 19*32+13 = 621.
    struct { int32_t x, y; int seg; const char* what; } P[] = {
        { -23150, 62853, 621, "the pre-v0.21.1 Edea's House marker" },
        { -28950, 70090, 652, "the v0.21.1 marker" },
        { -29585, 70739, 652, "Aaron's position at the lighthouse" },
        { -20538, 64385, 621, "Balamb Garden, parked" },
        {  13249,-26779, 273, "Balamb Town" },
    };
    for (unsigned i = 0; i < sizeof(P)/sizeof(P[0]); i++) {
        const int s = WmSegmentIndex(P[i].x, P[i].y);
        if (s != P[i].seg) {
            bad++;
            printf("  BAD: %s (%d,%d) -> seg %d, want %d\n",
                   P[i].what, P[i].x, P[i].y, s, P[i].seg);
        }
    }
    printf("segment index: %u known points reproduce the mod's own arithmetic\n",
           (unsigned)(sizeof(P)/sizeof(P[0])));

    // 2. **THE FINDING.** wmsetus program 34 is Edea's House and its position
    //    test is `SEGMENT== 652`. That segment must be the 8192-unit square the
    //    orphanage stands in -- and the marker must be inside it, which the
    //    pre-v0.21.1 one was not.
    int32_t x0, x1, y0, y1;
    WmSegmentBounds(652, &x0, &x1, &y0, &y1);
    check(x0 == -32768 && x1 == -24576 && y0 == 65536 && y1 == 73728,
          "segment 652 is not the box the exe's arithmetic gives");
    printf("segment 652 (Edea's House): x[%d,%d] y[%d,%d]\n", x0, x1, y0, y1);

    check(WmSegmentIndex(-28950, 70090) == 652, "the shipped marker is outside segment 652");
    check(WmSegmentIndex(-23150, 62853) != 652, "the old marker was in 652 after all");

    // 3. Round trip: every point inside a segment's box maps back to it. Run it
    //    over the four segments this investigation names, including the one
    //    that program 32 turned out to be (nowhere near Centra).
    const int SEGS[] = { 652, 621, 506, 273 };
    for (unsigned k = 0; k < sizeof(SEGS)/sizeof(SEGS[0]); k++) {
        WmSegmentBounds(SEGS[k], &x0, &x1, &y0, &y1);
        for (int32_t x = x0; x < x1; x += 811)
            for (int32_t y = y0; y < y1; y += 811)
                if (WmSegmentIndex(x, y) != SEGS[k]) {
                    bad++;
                    printf("  BAD: (%d,%d) is in segment %d's box but maps to %d\n",
                           x, y, SEGS[k], WmSegmentIndex(x, y));
                    x = x1; y = y1;
                }
    }
    printf("round trip: 4 segment boxes map back to themselves\n");

    // 4. Program 32 -- the one the mod reported as the story-locked Edea's House
    //    entrance -- is segment 506, and segment 506 is not in Centra at all.
    WmSegmentBounds(506, &x0, &x1, &y0, &y1);
    check(x0 >= 60000, "segment 506 should be in the far east, not Centra");
    printf("segment 506 (what the old decode called Edea's House): x[%d,%d] y[%d,%d]\n",
           x0, x1, y0, y1);

    printf("trigseg: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
