// walkmesh_height_lookup_model.inl -- THE FIELD WALKMESH IS 3D AND THE LOOKUP WAS NOT
//
// v0.119.0 (#centra). One line of the 2026-08-28 log, on crtower1, driving to
// the ladder the player had just selected from the catalog:
//
//   [drive] target on different walkmesh island (start tri 52, goal tri 21)
//           -- searching for bridge trigger
//   [drive] no reachable trigger line found, using direct steering
//   ... moveDist=0, six recoveries, Cancelled.
//
// The ladder is NOT on a different island. crtower1's walkmesh is 137
// triangles in 14 connected components: one floor of 111, and **thirteen
// two-triangle pockets**. Read their centres out of the .id and the pattern is
// unmistakable --
//
//   island  9  tris 20,21   (1590,-397,11413) (1618,-466,11413)
//   island 10  tris 22,23   (1590,-397,11813) (1618,-466,11813)
//   island 11  tris 24,25   (1590,-397,12213) (1618,-466,12213)
//   island 12  tris 26,27   (1590,-397,11013) (1618,-466,11013)
//   island 13  tris 28,29   (1590,-397,10613) (1618,-466,10613)
//
// -- the SAME X and Y five times over, 400 units apart in Z. They are the
// LADDER RUNGS, stacked vertically above the floor, and the game teleports the
// party onto them one at a time as the climb plays out. Two more sets of five
// do the same for the other ladder and the stone.
//
// FindNearestTriangle scored candidates on X and Y alone. Asked where the
// ladder's trigger line at (1617,-444) is, it answered tri 21 -- a rung a
// THOUSAND UNITS IN THE AIR, 22 units away in 2D -- instead of tri 88, the
// floor directly under it at 52 units away and z=10378. The line's own Z is
// 10385. The drive then compared the player's floor triangle against a rung,
// correctly found them unconnected, and spent twelve seconds looking for a
// bridge to a place it was already standing next to.
//
// The same artefact accounts for the crtower2 failure v0.118.0 worked around:
// the exit to crtower3 at (1170,979) resolved 2D to tri 73 on the right-ladder
// platform (z=14060, island 12), and resolves in 3D to tri 91 on the main floor
// (z=14984, island 0) -- **the player's own island**. No bridge was ever needed
// there; ordinary A* walks it. And it is the shape of the Deep Sea Research
// Center hatch bug that has been open since v0.112.0, where the drive "reaches
// the goal triangle then steers at the captured line's raw midpoint, which is
// off the walkable floor".
//
// THE FIX NEEDS NO TUNING CONSTANT, which is why it is worth doing this way
// rather than with a Z tolerance. Field X, Y and Z are the same int16 world
// units, so plain 3D Euclidean distance separates the cases by an enormous
// margin: for the query above the rung scores 22^2 + 1028^2 and the floor
// 52^2 + 7^2. There is no threshold to pick and nothing to re-tune per field.
//
// WHERE THE Z COMES FROM, and why this is not a rewrite of navigation. Only a
// caller that HAS a height may use the 3D form, and exactly two kinds do:
// SETLINE trigger lines, which have carried z1/z2 since v05.58, and INF
// gateways, whose lineZ1/lineZ2 v0.18.3.298 stopped throwing away. Every other
// caller -- the player's own position, GPS, the diagnostics -- keeps the 2D
// function unchanged, because it has no height to offer and a wrong Z would be
// worse than none.

static float WmSqDist2D(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

static float WmSqDist3D(float ax, float ay, float az, float bx, float by, float bz)
{
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

// The nearest of `n` triangle centres to (x, y, z). With useZ false this is
// exactly what FindNearestTriangle has always computed, so the two forms can
// be compared against each other in a test rather than assumed to agree.
//
// Ties go to the LOWEST index, matching the original loop's strict `<`. On a
// stacked mesh a tie means two rungs at the same height and the same spot,
// which cannot happen; keeping the rule identical is what lets the 2D path
// stay provably unchanged.
static int WmPickNearest(const float* cx, const float* cy, const float* cz,
                         int n, float x, float y, float z, bool useZ)
{
    if (cx == nullptr || cy == nullptr || n <= 0) return -1;
    if (useZ && cz == nullptr) return -1;
    int   best  = -1;
    float bestD = 0.0f;
    for (int t = 0; t < n; t++) {
        const float d = useZ ? WmSqDist3D(cx[t], cy[t], cz[t], x, y, z)
                             : WmSqDist2D(cx[t], cy[t], x, y);
        if (best < 0 || d < bestD) { bestD = d; best = t; }
    }
    return best;
}
