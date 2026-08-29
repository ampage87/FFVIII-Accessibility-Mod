// rag_nofly_pure.inl -- the one thing the Ragnarok cannot climb over.
//
// A statement-free header fragment #included by world_map_drive_exec.inl (the
// real build) and by tests/rag_nofly_test.cpp.
//
// Aaron: "The Pandora is directly above Tears Point and reaches up into the sky
// higher than Ragnarok can go."
//
// EVERY OTHER OBSTRUCTION ON THIS MAP IS AN ALTITUDE PROBLEM, AND THIS ONE IS
// NOT. The Fisherman's Horizon towers stopped the ship at 1,806 units in the
// 15:15 BAT and Aaron cleared them by climbing; from v0.89.0 the drive climbs on
// every cruise for exactly that reason. The Lunatic Pandora cannot be answered
// that way at any altitude, so it has to be flown AROUND, and a straight line
// from A to B is the one thing the airship's steering has always done.
//
// So: one no-fly cylinder, and an aim point that clears it.
//
// The centre is the catalog's own Tears' Point marker -- the same coordinate the
// drive already flies to when he asks for it -- rather than a number typed in
// beside it, so the two cannot drift apart.
//
// THE RADIUS IS A FIRST GUESS AND IS LOGGED AS ONE. Nobody has measured how wide
// the Pandora is; what is known is that FH stopped the ship 1,806 units from a
// point inside it, and the Pandora is the larger structure. 3,000 units is
// deliberately generous -- the cost of being too wide is a slightly longer flight
// past Tears' Point, and the cost of being too narrow is the failure this exists
// to prevent. Every application says so in the log so the next BAT can tighten it.
static const double RAG_NOFLY_RADIUS = 3000.0;

// Distance from point C to the SEGMENT AB. Not to the infinite line: a cylinder
// behind the ship or beyond the destination is not in the way.
static double RagSegPointDist(double ax, double ay, double bx, double by,
                              double cx, double cy)
{
    const double vx = bx - ax, vy = by - ay;
    const double wx = cx - ax, wy = cy - ay;
    const double vv = vx*vx + vy*vy;
    double t = (vv > 0.0) ? (wx*vx + wy*vy) / vv : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double px = ax + t*vx - cx;
    const double py = ay + t*vy - cy;
    return sqrt(px*px + py*py);
}

// If the route passes through the cylinder, hand back an aim point that clears
// it; otherwise leave the aim alone and return false.
//
// The detour aims at the rim on the side the route is ALREADY favouring, so a
// ship that is a little north of the Pandora goes north of it rather than being
// sent across its face. Pushed out past the rim by a margin, because aiming
// exactly AT the rim of a circle you are trying to miss is not a plan.
static bool RagNoFlyDetour(double sx, double sy, double tx, double ty,
                           double cx, double cy, double r,
                           double* ax, double* ay)
{
    if (!ax || !ay) return false;
    // A destination inside the cylinder is not something to steer around -- there
    // would be nowhere to arrive. Nothing in the catalog is, and if something
    // ever is, flying at it plainly beats orbiting it forever.
    const double tdx = tx - cx, tdy = ty - cy;
    if (sqrt(tdx*tdx + tdy*tdy) <= r) return false;

    if (RagSegPointDist(sx, sy, tx, ty, cx, cy) >= r) return false;

    // Which side is the ship already on? Take the component of ship-from-centre
    // perpendicular to the route.
    const double vx = tx - sx, vy = ty - sy;
    const double vlen = sqrt(vx*vx + vy*vy);
    if (vlen <= 0.0) return false;
    const double nx = -vy / vlen, ny = vx / vlen;      // unit normal to the route
    const double sdx = sx - cx, sdy = sy - cy;
    double side = sdx*nx + sdy*ny;
    if (side == 0.0) side = 1.0;                        // dead astern: pick one
    const double sgn = (side > 0.0) ? 1.0 : -1.0;

    *ax = cx + nx * sgn * r * 1.25;
    *ay = cy + ny * sgn * r * 1.25;
    return true;
}
