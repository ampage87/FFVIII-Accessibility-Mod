// world_map_route_test.cpp -- can the player actually WALK there? (#118)
//
//   g++ -std=c++17 -O0 -Isrc -Itests -o world_map_route_test tests/world_map_route_test.cpp
//
// WHY THIS EXISTS
// ---------------
// Aaron: *"Ensure your fix does not regress walking behavior on the other
// continents we've previously reached before as well. Your sim should still be
// able to walk between known walkable locations on those other continents."*
//
// The v0.56.0 change is to the locomotion VERDICT and to five Esthar entry
// aims. Neither touches the grid, the traversability rule or the planner, so a
// routing regression should be impossible by construction -- but "should be
// impossible by construction" is a claim, and this file is the check.
//
// THE GRID IS THE SHIPPED GRID, NOT A MODEL OF IT. tests/world_map_route_fixture.h
// is extracted from wmx.obj by offline/wmx.py, which reproduces
// world_map_segments.inl's rasterisation step for step and is pinned to all six
// statistics the mod's own startup log prints:
//
//     land 8311  forest 828  mountain 1960 (1372 steep-blocked)  ocean 38053
//     walkable 9727     clearance max 11     wall-hugging 4580
//
// All six match. The edge rule below is world_map_planner.inl's verbatim:
// 4-neighbour with torus wrap, IsFineTraversable on the destination cell, and
// the WM_CLIMB_STEP height guard unless the destination is a road cell.
//
// So a route that plans here plans in the game, and one that does not, does not.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <queue>
#include <vector>

#include "world_map_route_fixture.h"

static const int WM_CLIMB_STEP = 400;

static int bad = 0;
static void fail(const char* fmt, ...)
{ va_list ap; va_start(ap, fmt); std::printf("  BAD: "); vprintf(fmt, ap); va_end(ap); std::printf("\n"); bad++; }

static inline bool walkable(int r, int c)
{ return (RF_WALK[r][c >> 3] >> (7 - (c & 7))) & 1; }
static inline bool roadcell(int r, int c)
{ return (RF_ROAD[r][c >> 3] >> (7 - (c & 7))) & 1; }

static inline void toFine(int32_t gx, int32_t gy, int* c, int* r)
{ *c = (int)(((int64_t)gx + RF_OFF_X) / RF_CELL); *r = (int)(((int64_t)gy + RF_OFF_Y) / RF_CELL); }

// world_map_planner.inl's edge rule, verbatim.
static bool edgeOk(int cr, int cc, int nr, int nc)
{
    if (!walkable(nr, nc)) return false;
    if (roadcell(nr, nc))  return true;      // stepping ONTO road is guard-exempt
    int step = (int)RF_ELEV[cr][cc] - (int)RF_ELEV[nr][nc];
    if (step < 0) step = -step;
    return step <= WM_CLIMB_STEP;
}

// Returns the number of cells in the route, or -1 if the target cell cannot be
// reached. Dijkstra, 4-neighbour, wrapping -- the planner's own search.
static int planCells(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int c0, r0, c1, r1;
    toFine(ax, ay, &c0, &r0);
    toFine(bx, by, &c1, &r1);
    if (r0 < 0 || r0 >= RF_ROWS || c0 < 0 || c0 >= RF_COLS) return -2;
    if (r1 < 0 || r1 >= RF_ROWS || c1 < 0 || c1 >= RF_COLS) return -2;
    if (!walkable(r0, c0)) return -3;
    if (!walkable(r1, c1)) return -4;

    static int32_t dist[RF_ROWS][RF_COLS];
    for (int r = 0; r < RF_ROWS; r++)
        for (int c = 0; c < RF_COLS; c++) dist[r][c] = INT32_MAX;

    typedef std::pair<int32_t, int> Node;                 // (cost, r*COLS+c)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node> > q;
    dist[r0][c0] = 0;
    q.push(Node(0, r0 * RF_COLS + c0));
    static const int DR[4] = { -1, 1, 0, 0 };
    static const int DC[4] = { 0, 0, -1, 1 };
    while (!q.empty()) {
        Node n = q.top(); q.pop();
        int r = n.second / RF_COLS, c = n.second % RF_COLS;
        if (n.first > dist[r][c]) continue;
        if (r == r1 && c == c1) return n.first + 1;
        for (int d = 0; d < 4; d++) {
            int nr = ((r + DR[d]) % RF_ROWS + RF_ROWS) % RF_ROWS;
            int nc = ((c + DC[d]) % RF_COLS + RF_COLS) % RF_COLS;
            if (!edgeOk(r, c, nr, nc)) continue;
            int32_t nd = n.first + 1;
            if (nd < dist[nr][nc]) { dist[nr][nc] = nd; q.push(Node(nd, nr * RF_COLS + nc)); }
        }
    }
    return -1;
}

struct Leg { const char* from; int32_t ax, ay; const char* to; int32_t bx, by; };

int main()
{
    std::printf("world_map_route_test\n");

    // =======================================================================
    // 1. THE CONTINENTS ALREADY REACHED. Aaron named these; the coordinates
    //    are the shipped entry aims, i.e. exactly where auto-drive goes.
    // =======================================================================
    static const Leg KNOWN[] = {
        { "Deling City",      -61947, -30631, "Tomb of the Unknown King", -42011, -36843 },
        { "Galbadia Garden",  -36895, -27082, "Dollet",                   -14513, -39119 },
        { "Dollet",           -14513, -39119, "Timber",                   -22580,  -5291 },
        { "Timber",           -22580,  -5291, "Deling City",              -61947, -30631 },
        { "Galbadia Station", -38914, -24767, "Galbadia Garden",          -36895, -27082 },
        { "Deling City",      -61947, -30631, "D-District Prison",        -55308,  -6296 },
        { "Balamb Town",       12560, -26800, "Fire Cavern",               30239, -29528 },
        { "Balamb Garden",     24304, -30300, "Balamb Town",               12560, -26800 },
        { "Fire Cavern",       30239, -29528, "Balamb Garden",             24304, -30300 },
        { "Chocobo Forest 7", -21004,  69632, "Edea's House",             -29459,  69772 },
        { "Edea's House",     -29459,  69772, "Chocobo Forest 7",         -21004,  69632 },
    };
    for (unsigned k = 0; k < sizeof(KNOWN)/sizeof(KNOWN[0]); k++) {
        const Leg& L = KNOWN[k];
        const int n = planCells(L.ax, L.ay, L.bx, L.by);
        if (n < 0) fail("%s -> %s does not plan (code %d) -- this route worked before",
                        L.from, L.to, n);
        else std::printf("  %-24s -> %-24s %4d cells\n", L.from, L.to, n);
    }

    // =======================================================================
    // 2. ESTHAR. The five retargeted aims, from where Aaron actually stood.
    //    (56885,10613) is his position when he pressed the drive key for Lunar
    //    Gate; (89121,7419) is where the failed drives left him.
    // =======================================================================
    static const Leg ESTHAR[] = {
        { "Aaron 20:37:42",  56885, 10613, "Esthar City",         56965,  9788 },
        { "Aaron 20:37:42",  56885, 10613, "Lunatic Pandora Lab", 83966, -3199 },
        { "Aaron 20:37:42",  56885, 10613, "Lunar Gate",          95460,  9956 },
        { "Aaron 20:37:42",  56885, 10613, "Sorceress Memorial",  77820, 12162 },
        { "Aaron 20:37:42",  56885, 10613, "Tears' Point",        86020, 25836 },
        { "Aaron 20:41:50",  89121,  7419, "Lunar Gate",          95460,  9956 },
        { "Aaron 20:41:50",  89121,  7419, "Sorceress Memorial",  77820, 12162 },
        { "Aaron 20:41:50",  89121,  7419, "Tears' Point",        86020, 25836 },
    };
    for (unsigned k = 0; k < sizeof(ESTHAR)/sizeof(ESTHAR[0]); k++) {
        const Leg& L = ESTHAR[k];
        const int n = planCells(L.ax, L.ay, L.bx, L.by);
        if (n < 0) fail("%s -> %s does not plan (code %d) -- the retarget is unreachable",
                        L.from, L.to, n);
        else std::printf("  %-24s -> %-24s %4d cells\n", L.from, L.to, n);
    }

    // =======================================================================
    // 3. THE FAILURE WAS NEVER ROUTING, AND THIS PROVES IT.
    //
    //    The five OLD Esthar markers are all perfectly walkable -- Aaron walked
    //    31 km to one of them and stood on it. They opened nothing because they
    //    sit on no entry-trigger polygon, which is a different fault entirely.
    //    If a future edit "fixes" this by making the old markers unwalkable, it
    //    has misunderstood the bug, and this fails.
    // =======================================================================
    static const Leg OLDMARK[] = {
        { "Aaron", 56885, 10613, "old Esthar City marker",         57011, -2295 },
        { "Aaron", 56885, 10613, "old Lunatic Pandora Lab marker", 79521, -9135 },
        { "Aaron", 56885, 10613, "old Lunar Gate marker",          88021,  7865 },
        { "Aaron", 56885, 10613, "old Sorceress Memorial marker",  81521, 11865 },
        { "Aaron", 56885, 10613, "old Tears' Point marker",        83021, 31865 },
    };
    for (unsigned k = 0; k < sizeof(OLDMARK)/sizeof(OLDMARK[0]); k++) {
        const Leg& L = OLDMARK[k];
        if (planCells(L.ax, L.ay, L.bx, L.by) < 0)
            fail("%s is no longer walkable -- the Esthar failure was an ENTRY fault, "
                 "not a routing one, and this test exists to keep that straight", L.to);
    }
    std::printf("  all five dead Esthar markers are still walkable (the fault was entry, not routing)\n");

    // =======================================================================
    // 4. THE GRID IS THE SHIPPED GRID. Re-assert the parity tallies here, so
    //    the fixture cannot be regenerated from a drifted extractor without the
    //    test noticing.
    // =======================================================================
    {
        int wcount = 0, rcount = 0;
        for (int r = 0; r < RF_ROWS; r++)
            for (int c = 0; c < RF_COLS; c++) {
                if (walkable(r, c)) wcount++;
                if (roadcell(r, c)) rcount++;
            }
        if (wcount != 9727)
            fail("fixture has %d walkable cells; the mod's own [WALKFINE] log says 9727", wcount);
        if (rcount != 739)
            fail("fixture has %d road cells; the mod's own [ROADMAP] log says 739", rcount);
        std::printf("  fixture: %d walkable cells, %d road cells -- both match the shipped log\n",
                    wcount, rcount);
    }

    // =======================================================================
    // 5. THE EDGE RULE IS DOING ITS TWO JOBS.
    //
    //    Every route above is a POSITIVE result, and positive results survive a
    //    rule that has stopped refusing anything. Mutation-testing proved it:
    //    deleting the WM_CLIMB_STEP height guard, and making the road exemption
    //    unconditional, both left every assertion above passing. So the guard's
    //    work is counted directly.
    //
    //    The height guard is what keeps the planner off the false coasts -- a
    //    cliff is an EDGE between two cells that are each individually "land",
    //    which no per-cell test can see. Losing it silently is how v0.18.3.92
    //    regressed and wedged a drive 15 km out.
    // =======================================================================
    {
        int total = 0, guardBlocked = 0, roadOnly = 0;
        for (int r = 0; r < RF_ROWS; r++)
            for (int c = 0; c < RF_COLS; c++) {
                if (!walkable(r, c)) continue;
                static const int DR2[4] = { -1, 1, 0, 0 }, DC2[4] = { 0, 0, -1, 1 };
                for (int d = 0; d < 4; d++) {
                    const int nr = ((r + DR2[d]) % RF_ROWS + RF_ROWS) % RF_ROWS;
                    const int nc = ((c + DC2[d]) % RF_COLS + RF_COLS) % RF_COLS;
                    if (!walkable(nr, nc)) continue;
                    total++;
                    // THROUGH edgeOk(), not a second copy of its arithmetic.
                    // The first version of this block recomputed the rule inline
                    // and both mutations walked straight past it -- a check that
                    // reimplements the thing it is checking agrees with itself.
                    const bool ok = edgeOk(r, c, nr, nc);
                    int step = (int)RF_ELEV[r][c] - (int)RF_ELEV[nr][nc];
                    if (step < 0) step = -step;
                    if (!ok) guardBlocked++;
                    else if (step > WM_CLIMB_STEP && roadcell(nr, nc)) roadOnly++;
                }
            }
        if (total != 33686)
            fail("%d walkable adjacencies, expected 33686 -- the fixture has changed", total);
        if (guardBlocked != 1941)
            fail("the climb guard refuses %d edges, expected 1941 -- if this is 0 the "
                 "guard is gone and the planner will route across cliff faces", guardBlocked);
        if (roadOnly != 119)
            fail("%d edges are permitted only by the road exemption, expected 119 -- "
                 "if this jumps, the exemption has stopped being road-specific", roadOnly);
        std::printf("  edge rule: %d adjacencies, %d refused by the climb guard, "
                    "%d allowed only because the step lands on road\n",
                    total, guardBlocked, roadOnly);
    }

    // A negative control: two points on different landmasses must NOT plan, or
    // the edge rule has gone permissive and every "OK" above is worthless.
    if (planCells(12560, -26800, -61947, -30631) >= 0)
        fail("Balamb Town plans to Deling City on foot -- they are different continents, "
             "so the traversability rule has stopped rejecting anything");
    else
        std::printf("  negative control: Balamb Town does not walk to Deling City\n");

    std::printf("%s -- %d bad\n", bad ? "FAIL" : "OK", bad);
    return bad ? 1 : 0;
}
