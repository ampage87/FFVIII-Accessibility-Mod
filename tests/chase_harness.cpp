// chase_harness.cpp - Step 0 chase-protection host harness.
//
// Compiles the REAL field nav pathfinding (src/field_nav_pathfinding.inl) off
// the game and runs it against real walkmesh fixtures (tests/chase_fixtures.h,
// generated at build time from the committed ff8_walkmeshes.json). The mere
// fact that it COMPILES is itself protection: it exercises the real nav core's
// struct/static surface, so an incompatible refactor of FindPortal /
// ComputeAStarPath / FunnelPath / the WalkmeshData layout breaks the build.
//
// HARD GATE (exit 1): walkmesh mesh integrity -- every neighbor link must share
// exactly two vertices. Fix-invariant; cannot false-fail on current code.
//
// REPORTED (not yet gating; promoted to hard asserts in Step 1 once the fixed
// routes are golden): per chase field, funnel waypoint count, how many land
// OUT-OF-MESH (the wall-hug signature), and closest approach to the scripted
// robot point. Under current code dotown_3 shows 5/30 wps out-of-mesh; the
// FindPortal fix drives that to 0 (verified in-container 2026-05-31).
//
// In-container findings (current vs sed-fixed .inl), all PASS:
//   - domt2_1: A* NO PATH either way (spawn tri 107 is in a 42-tri island with
//     no route to the goal; structural, not the bug) -> fix is NEUTRAL; the
//     field clears via chase-drive direct steering, not the funnel.
//   - dotown_3: 30 wps / 5 out-of-mesh (current) -> 6 wps / 0 out-of-mesh
//     (fixed); closest approach to the inert robot slot 895 -> 803, both far
//     outside catch range (known-good run cleared at 870, 0 catches).
//   - bggate_6 west corridor reproduces the documented (-1686,-553) wall-hug
//     under current code; gone under the fix.
//
// Build (host): generate tests/chase_fixtures.h, then
//   g++ -std=c++17 -O0 -g -o chase_harness tests/chase_harness.cpp

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <vector>

#include "../src/field_archive.h"
#include "chase_fixtures.h"

namespace Log { inline void Field(const char*, ...) {} }

namespace FieldNavigation {
static FieldArchive::WalkmeshData s_walkmesh = {};
static const int   MAX_WAYPOINTS = 256;
static float       s_waypoints[MAX_WAYPOINTS][2] = {};
static int         s_waypointCount = 0;
static int         s_waypointIdx   = 0;
static bool        s_usingFunnel   = false;
static const int   MAX_CORRIDOR = 4096;
static uint16_t    s_corridor[MAX_CORRIDOR] = {};
static int         s_corridorCount = 0;
static const double NAV_PI = 3.14159265358979323846;
static volatile bool s_chaseDriveActive = false;
static bool IsTriangleBlockedByNPC(float x, float y, int targetEntityIdx);

#include "../src/field_nav_pathfinding.inl"
// v0.55.0: EdgeCrossesScreenBound is forward-declared above and was defined
// only in field_navigation.cpp, so this harness had stopped linking and was
// out of the gate set entirely. It now lives in an .inl the game and this
// harness share -- the A* barrier rule under test is the shipped one.
#include "../src/field_nav_geometry.inl"

static bool IsTriangleBlockedByNPC(float, float, int) { return false; }
static bool IsSeparatedByTriggerLine(float, float, float, float, int) { return false; }
static bool WouldCrossTriggerLine(float, float, float, float, int) { return false; }
}  // namespace FieldNavigation

using FieldArchive::WalkmeshVertex;
using FieldArchive::WalkmeshTriangle;

static std::vector<WalkmeshVertex>   g_verts;
static std::vector<WalkmeshTriangle> g_tris;

static void load(const ChaseFixtures::Fixture& fx) {
    g_verts.assign(fx.verts, fx.verts + fx.vcount);
    g_tris.assign(fx.tris, fx.tris + fx.tcount);
    for (auto& t : g_tris) {
        float cx = 0, cy = 0;
        for (int k = 0; k < 3; k++) { cx += (float)g_verts[t.vertexIdx[k]].x; cy += (float)g_verts[t.vertexIdx[k]].y; }
        t.centerX = cx / 3.0f; t.centerY = cy / 3.0f;
    }
    FieldNavigation::s_walkmesh.numVertices  = (int)g_verts.size();
    FieldNavigation::s_walkmesh.numTriangles = (int)g_tris.size();
    FieldNavigation::s_walkmesh.vertices  = g_verts.data();
    FieldNavigation::s_walkmesh.triangles = g_tris.data();
    FieldNavigation::s_walkmesh.valid = true;
}

static const ChaseFixtures::Fixture* find_fx(const char* name) {
    for (int i = 0; i < ChaseFixtures::kFixtureCount; i++)
        if (strcmp(ChaseFixtures::kFixtures[i].name, name) == 0)
            return &ChaseFixtures::kFixtures[i];
    return nullptr;
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, len2 = dx*dx + dy*dy;
    float t = (len2 > 1e-6f) ? ((px-ax)*dx + (py-ay)*dy) / len2 : 0.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    float ex = px - (ax + t*dx), ey = py - (ay + t*dy);
    return sqrtf(ex*ex + ey*ey);
}

struct Scenario {
    const char* field; float startX, startY, goalX, goalY;
    float robotX, robotY; int knownGoodClosest; bool hasRobot;
};
static const Scenario kScenarios[] = {
    { "domt2_1",  622.0f,  945.0f,  -93.0f, -3414.0f,  814.0f, -875.0f, 513, true },
    { "dotown_3", -1499.0f,1237.0f, -174.0f,-2143.0f,   16.0f,    0.0f, 870, true },
};
static const int kScenarioCount = (int)(sizeof(kScenarios)/sizeof(kScenarios[0]));

// HARD GATE: every neighbor link shares exactly two vertices.
static int check_mesh_integrity() {
    int bad = 0;
    for (int i = 0; i < ChaseFixtures::kFixtureCount; i++) {
        const ChaseFixtures::Fixture& fx = ChaseFixtures::kFixtures[i];
        int n = fx.tcount, fieldBad = 0;
        for (int t = 0; t < n; t++)
            for (int e = 0; e < 3; e++) {
                int nb = fx.tris[t].neighbor[e];
                if (nb == 0xFFFF || nb >= n) continue;
                int shared = 0;
                for (int p = 0; p < 3; p++) for (int q = 0; q < 3; q++)
                    if (fx.tris[t].vertexIdx[p] == fx.tris[nb].vertexIdx[q]) shared++;
                if (shared != 2) fieldBad++;
            }
        if (fieldBad) printf("  *** %s: %d neighbor links share != 2 vertices\n", fx.name, fieldBad);
        bad += fieldBad;
    }
    return bad;
}

int main() {
    printf("=== chase_harness: real nav core on real Dollet fixtures ===\n\n");

    int meshBad = check_mesh_integrity();
    printf("mesh integrity: %d bad neighbor links across %d fixtures%s\n\n",
           meshBad, ChaseFixtures::kFixtureCount, meshBad ? "" : " (all share exactly 2 verts)");

    for (int s = 0; s < kScenarioCount; s++) {
        const Scenario& sc = kScenarios[s];
        const ChaseFixtures::Fixture* fx = find_fx(sc.field);
        if (!fx) { printf("%-9s FIXTURE MISSING\n\n", sc.field); continue; }
        load(*fx);
        FieldNavigation::s_chaseDriveActive = true;
        int startTri = FieldNavigation::FindNearestTriangle(sc.startX, sc.startY);
        int goalTri  = FieldNavigation::FindNearestTriangle(sc.goalX,  sc.goalY);
        if (!FieldNavigation::ComputeAStarPath(startTri, goalTri)) {
            printf("%-9s start tri %d -> goal tri %d: A* NO PATH "
                   "(direct-drive field, funnel unused; FindPortal fix neutral)\n\n",
                   sc.field, startTri, goalTri);
            continue;
        }
        int corridor = FieldNavigation::s_corridorCount;
        FieldNavigation::FunnelPath(sc.startX, sc.startY, sc.goalX, sc.goalY);
        int nwp = FieldNavigation::s_waypointCount;
        int oob = 0;
        for (int i = 0; i < nwp; i++)
            if (!FieldNavigation::IsInsideWalkmesh(FieldNavigation::s_waypoints[i][0],
                                                   FieldNavigation::s_waypoints[i][1])) oob++;
        float minRobot = 1e30f;
        float px = sc.startX, py = sc.startY;
        for (int i = 0; i < nwp; i++) {
            float d = seg_dist(sc.robotX, sc.robotY, px, py,
                               FieldNavigation::s_waypoints[i][0], FieldNavigation::s_waypoints[i][1]);
            if (d < minRobot) minRobot = d;
            px = FieldNavigation::s_waypoints[i][0]; py = FieldNavigation::s_waypoints[i][1];
        }
        printf("%-9s A* corridor=%d tris, funnel=%d wps, %d OUT-OF-MESH; "
               "closest to robot (%.0f,%.0f)=%.0f (known-good %d)\n\n",
               sc.field, corridor, nwp, oob, sc.robotX, sc.robotY, minRobot, sc.knownGoodClosest);
    }

    if (meshBad) {
        printf("CHASEGUARD: *** FAIL *** (%d walkmesh integrity violations)\n", meshBad);
        return 1;
    }
    printf("CHASEGUARD: PASS (mesh integrity locked; real nav core compiled & ran; "
           "route metrics above are advisory until Step-1 golden snapshot)\n");
    return 0;
}
