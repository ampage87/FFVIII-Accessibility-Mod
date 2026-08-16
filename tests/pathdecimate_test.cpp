// pathdecimate_test.cpp -- v0.21.4 (#70)
//
// The grid planner's path emission, which used to TRUNCATE a long route and
// call it planned. This is the arithmetic from PlanPathGridM, lifted verbatim,
// with the properties that matter asserted over every route length up to twice
// the buffer.
//
//   g++ -std=c++17 -O0 -Isrc -o pathdecimate_test tests/pathdecimate_test.cpp

#include <cstdio>
#include <vector>

static const int DRIVE_PATH_MAX = 768;   // world_map_state.inl
static const int STEP           = 128;   // planner grid pitch

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

// Verbatim from world_map_planner2.inl's emission loop: `revBuf` holds the route
// GOAL-first (index rc-1 is the start), and the loop walks it backwards so the
// emitted path runs start -> goal.
static std::vector<int> Emit(int rc)
{
    int stride = 1;
    for (;;) {
        // stride 1 emits every cell; any coarser stride also emits the goal, so
        // it costs one extra slot.
        const int emit = (stride == 1) ? rc : ((rc + stride - 1) / stride) + 1;
        if (emit <= DRIVE_PATH_MAX) break;
        stride++;
    }

    std::vector<int> out;
    int n = 0;
    for (int i = rc - 1; i >= 0 && n < DRIVE_PATH_MAX; i--) {
        const int fromStart = (rc - 1) - i;
        if (stride > 1 && i != 0 && (fromStart % stride) != 0) continue;
        out.push_back(i);
        n++;
    }
    return out;
}

// What the code did before v0.21.4.
static std::vector<int> EmitOld(int rc)
{
    std::vector<int> out;
    int n = 0;
    for (int i = rc - 1; i >= 0 && n < DRIVE_PATH_MAX; i--) { out.push_back(i); n++; }
    return out;
}

int main()
{
    // 1. THE REGRESSION. A route longer than the buffer used to lose its tail --
    //    the emitted path ended in open country and was still marked planned.
    {
        const int rc = 1500;
        std::vector<int> old = EmitOld(rc);
        check(old.back() != 0, "the fixture does not reproduce the truncation");
        printf("old emission at rc=%d: %d waypoints, ends at cell %d of %d "
               "-- %d cells of route silently dropped\n",
               rc, (int)old.size(), old.back(), rc - 1, old.back());
    }

    // 2. Over every plausible route length: never overflow, always start at the
    //    start, always END AT THE GOAL, and never go backwards.
    for (int rc = 2; rc <= DRIVE_PATH_MAX * 2 + 3; rc++) {
        std::vector<int> p = Emit(rc);
        if ((int)p.size() > DRIVE_PATH_MAX) {
            bad++; printf("  BAD: rc=%d emitted %d > %d\n", rc, (int)p.size(), DRIVE_PATH_MAX);
            break;
        }
        if (p.empty() || p.front() != rc - 1) {
            bad++; printf("  BAD: rc=%d does not start at the start\n", rc); break;
        }
        if (p.back() != 0) {
            bad++; printf("  BAD: rc=%d does not end at the goal (ends at %d)\n", rc, p.back());
            break;
        }
        if ((int)p.size() < 2) { bad++; printf("  BAD: rc=%d emitted < 2\n", rc); break; }
        for (size_t k = 1; k < p.size(); k++) {
            if (p[k] >= p[k-1]) {
                bad++; printf("  BAD: rc=%d not monotonic at %d\n", rc, (int)k); break;
            }
        }
    }
    printf("emission: every route length 2..%d fits the buffer, starts at the start, "
           "ends at the goal, and is monotonic\n", DRIVE_PATH_MAX * 2 + 3);

    // 3. Short routes must be untouched -- full 128-unit resolution is what lets
    //    the executor follow a validated polyline through a canyon without
    //    corner-cutting (world_map_planner2.inl, v0.18.3.163).
    for (int rc = 2; rc <= DRIVE_PATH_MAX; rc++) {
        if ((int)Emit(rc).size() != rc) {
            bad++; printf("  BAD: rc=%d was decimated when it fits\n", rc); break;
        }
    }
    printf("short routes: untouched at full resolution up to %d cells\n", DRIVE_PATH_MAX);

    // 4. The resolution loss on a long route is bounded and reportable.
    {
        std::vector<int> p = Emit(1500);
        check((int)p.size() <= DRIVE_PATH_MAX, "1500-cell route overflows");
        check(p.back() == 0, "1500-cell route does not reach the goal");
        printf("rc=1500 -> %d waypoints, stride 2, %d-unit spacing, goal kept\n",
               (int)p.size(), 2 * STEP);
    }

    printf("pathdecimate: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
