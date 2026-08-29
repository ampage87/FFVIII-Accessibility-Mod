// drive_goal_tri_model.inl -- WHICH TRIANGLE IS THE TARGET STANDING ON?
//
// v0.131.6 (#centra). Aaron: "there was also an earlier field where you have to
// walk onto an automated lift, but the lift was identified as an NPC and
// auto-drive wouldn't actually trigger it." The 21:01 log names the failure in
// two lines:
//
//     [NAV-PATH] startTri 71 and goalTri 122 are on disconnected walkmesh
//                islands; straight-line fallback.
//     [A*] No path from tri 64 to tri 122 (61 iterations)
//
// and the drive parked him four hundred units short. He targeted it twice; the
// second attempt never launched a drive at all, and he walked onto the lift
// himself.
//
// THE LIFT IS FOUR TRIANGLES. crsphi1 has five walkmesh islands and four of them
// share a single XY footprint at different heights -- tri 131 at z 942, tri 149
// at 1561, tri 122 at 2231, tri 140 at 2949. That is the lift platform drawn
// once per stop it makes. A search over the target's coordinates can return any
// of the four, and it returned the one two thousand units up, on an island the
// player has no way to reach. tri 131 is on the player's own island and was
// walkable the whole time: BFS over the mesh confirms 71 -> 131 connected and
// 71 -> 122 not.
//
// THE MOD ALREADY HAD THE RIGHT ANSWER AND THREW IT AWAY. The catalog reads each
// entity's live walkmesh triangle every refresh and logged this one in the same
// second the drive failed -- "[dedup] ent3 tri=131". The engine's own statement
// of where a thing is standing beats any search over its coordinates, and it
// costs a read rather than a scan of the mesh.
//
// This is the same family as v0.119.0's 3D nearest-triangle work on crtower1's
// stacked ladder rungs, which fixed the lookup for the PLAYER's position and
// left the TARGET's on the 2D one. Gateways and trigger lines got a Z to search
// with; entity targets never did, because an entity does not need one.

// True when the engine's own triangle id for this target is usable as an A* goal.
// A zero id means the engine has not placed the entity on the mesh yet -- during
// a transition, or before its first update -- and an out-of-range one means the
// walkmesh and the entity table disagree, which is not something to path on.
static bool DriveGoalTriFromEntity(int entityIdx, unsigned triangleId,
                                   int numTriangles, int* goalTriOut)
{
    if (entityIdx < 0) return false;             // gateways, lines, JSM sentinels
    if (triangleId == 0) return false;           // not placed on the mesh
    if ((int)triangleId >= numTriangles) return false;
    if (goalTriOut) *goalTriOut = (int)triangleId;
    return true;
}
