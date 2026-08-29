// drive_goal_tri_test.cpp -- the A* goal triangle for an entity target
// (#centra, v0.131.6).
//
// The fixture is crsphi1's lift, whose four stacked triangles are what broke it.
#include <cstdio>

#include "drive_goal_tri_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    const int CRSPHI1_TRIS = 156;
    int goal = -1;

    // THE FIXTURE. crsphi1's lift entity is ent3, and the catalog read its
    // triangle as 131 in the same second the drive failed pathing to 122.
    // BFS over the field's own mesh: 71 -> 131 is connected, 71 -> 122 is not.
    CHECK(DriveGoalTriFromEntity(3, 131, CRSPHI1_TRIS, &goal),
          "an entity on the mesh supplies its own goal triangle");
    CHECK(goal == 131, "and it is the one the engine says, not the one a search found");

    // NON-ENTITY TARGETS keep the coordinate search: a gateway (-400 and below),
    // a trigger line (-200..-299) and a JSM sentinel (-300..-399) have no entity
    // placement to ask, which is exactly why they were given a Z to search with.
    goal = -1;
    CHECK(!DriveGoalTriFromEntity(-400, 131, CRSPHI1_TRIS, &goal),
          "a gateway target has no entity triangle");
    CHECK(!DriveGoalTriFromEntity(-201, 131, CRSPHI1_TRIS, &goal),
          "nor does a trigger line");
    CHECK(!DriveGoalTriFromEntity(-301, 131, CRSPHI1_TRIS, &goal),
          "nor a JSM sentinel");
    CHECK(goal == -1, "and none of them writes a goal");

    // NOT PLACED YET. Triangle 0 is the engine's "not on the mesh" value -- the
    // drive's own position reader already treats it that way, and pathing to
    // triangle zero during a transition would send the drive somewhere arbitrary.
    CHECK(!DriveGoalTriFromEntity(3, 0, CRSPHI1_TRIS, &goal),
          "an entity not yet placed supplies nothing");

    // OUT OF RANGE. The walkmesh and the entity table disagreeing is not
    // something to path on, and indexing the mesh with it would be worse.
    CHECK(!DriveGoalTriFromEntity(3, CRSPHI1_TRIS, CRSPHI1_TRIS, &goal),
          "a triangle id at the mesh size is refused, not indexed");
    CHECK(!DriveGoalTriFromEntity(3, 60000, CRSPHI1_TRIS, &goal),
          "and so is a wild one");
    CHECK(DriveGoalTriFromEntity(3, CRSPHI1_TRIS - 1, CRSPHI1_TRIS, &goal) &&
          goal == CRSPHI1_TRIS - 1, "the last real triangle is fine");

    // A null out-pointer must not be written through -- the caller in
    // field_nav_handlekeys.inl always passes one, but the rule is the rule.
    CHECK(DriveGoalTriFromEntity(3, 131, CRSPHI1_TRIS, nullptr),
          "and the answer does not depend on somewhere to put it");

    printf("drive_goal_tri_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
