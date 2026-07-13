// world_map_navmesh.inl - True triangle navmesh for world-map navigation
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone in the
// game build. Included AFTER world_map_geometry.inl and BEFORE
// world_map_segments.inl: LoadTerrainGrid feeds triangles in via
// Navmesh_AddTriangle() during its existing per-block polygon loop, then calls
// Navmesh_Build(). Initialize() (after catalog.inl) calls the diagnostics.
//
// WHY: the coarse 1024-unit fine grid blurs cliffs/coastlines and needs
// hardcoded patches. This keeps the wmx triangles AS triangles: world-space
// vertex dedup -> exact shared-edge adjacency -> axis-aligned T-junction
// bridging -> A* over the triangle graph with a per-edge height-step gate.
//
// VALIDATED OFFLINE against the real wmx.obj (NEXT_SESSION_PROMPT.md "OFFLINE
// PROTOTYPE RESULTS"): 157,416 navigable triangles; 253 connected components
// AFTER bridging; the Galbadia start component reaches Dollet, Timber, Galbadia
// Garden and Deling City, no hardcodes. T-junction bridging is REQUIRED --
// exact shared-edge adjacency alone fragments the mesh into 642 islands (a
// triangle on one side of a seam spans two sub-edges the other side splits, so
// they share collinear geometry but not a vertex PAIR). All seams are
// axis-aligned, so bridging single-use boundary edges that overlap a collinear
// boundary edge stitches them. v0.18.3.105: triangles are now WALKABILITY-
// FILTERED at feed time (poly[0x0E] bit7 -- the engine's on-foot collision flag
// from move-validator 0x54B860; this IS the Dollet "false coast", mountain polys
// the old mesh wrongly treated as walkable). Plus step-distance proximity links
// thread the sub-edge-width necks the engine's per-step movement crosses, and
// reachability floods UNGATED. Together these reach Dollet/Timber/Galbadia, not
// Balamb (across ocean). Finding 4 is overturned -- the collision WAS in the
// mesh. See issue #70.
//
// Host-compilable: no Win32/SEH/absolute-memory. std::vector/std::sort only
// (world_map.cpp provides <vector>); Log::World is the only external dep.
// Container-validated via tests/test_navmesh.cpp before this port.

#if NAVMESH_DIAG

// v0.18.3.120 (#70): the engine's on-foot collision rule, replicated DIRECTLY.
// Move validator 0x53E7A0 interpolates the EXACT ground height under the player
// and under a candidate one move-step away (barycentric interp 0x402620) and
// REJECTS the move when the two heights differ by >= 200 (0xC8). Foot is exempt
// from the per-vehicle ground-type bits (0x53E730), so this 200-step is the ONLY
// thing that stops Squall. Every candidate graph connection (shared edge,
// T-junction bridge, proximity link) is gated by NmEngineStepBlocked: sample the
// exact interpolated height along the centroid->centroid move at ~NM_STEP_DIST
// intervals and DROP the connection if any step crosses NM_HEIGHT_STEP. Gating
// CONNECTIONS (not triangles) drops only the steep MOVES while keeping a
// triangle's gentle connections, so the mesh stays connected -- the .119
// per-triangle slope gate deleted whole triangles (29% of the mesh, including
// their gentle edges) and disconnected Dollet. Replaces the .118 centroid-floor-step gate and the .117 NM_BRIDGE_ZTOL
// / NM_LINK_ZSTEP seam z-gates. Full trace: WMX_OBJ_FORMAT.md §12.
static const double  NM_HEIGHT_STEP = 200.0;     // engine's 0xC8: reject a move whose interpolated ground-height change >= this
static const double  NM_STEP_DIST   = 190.0;     // engine's per-move sample distance (~1 engine step; NM_STEP_LINK=400 = "~2 steps"). The one value not pinned to the digit (candidate-builder 0x53D8A0 -> 0x56CD50 vector math). Tunable.
static const int     NM_STEP_LINK  = 400;        // v0.18.3.105: proximity-link candidate radius (~2 engine steps) for sub-edge-width necks (#70). Geometry only; NmEngineStepBlocked decides walkability.
static const int     ROAD_BRIDGE   = 1536;       // v0.18.3.133 (#70): road-cell proximity-bridge radius (~1.5 fine cells). REVERTED to the .131 dense radius bridge after .132's nearest-pair (one bridge per cell-pair) connected the road too LOOSELY (navA* reverted to pre-bridge values) and the sparse corridor FROZE the drive near spawn. The dense radius bridge gave the smooth-to-4km drive. See Navmesh_Build step 3.6 (+ the .133 [GAPDIAG] read-only probe of the still-severed gap cell c113,r56).
static const int32_t NM_WX = 262144, NM_WY = 196608;

// engine(game) coord -> mesh(positive) coord, mirroring world_map_geometry.inl
static inline int32_t NmGameToMeshX(int32_t gx){ int32_t s = gx + 131072; return ((s % NM_WX) + NM_WX) % NM_WX; }
static inline int32_t NmGameToMeshY(int32_t gy){ int32_t s = gy + 98304;  return ((s % NM_WY) + NM_WY) % NM_WY; }

// ---- persistent navmesh storage (mesh coords) ----
static std::vector<int32_t> s_nmX0,s_nmY0,s_nmX1,s_nmY1,s_nmX2,s_nmY2; // triangle verts
static std::vector<int32_t> s_nmCX,s_nmCY;     // centroids
static std::vector<int32_t> s_nmFloor;          // floor elevation (mean vertex z)
static std::vector<uint8_t> s_nmTerr;           // ground-type byte
static std::vector<uint32_t> s_nmAdjOff;        // CSR row offsets (size N+1)
static std::vector<int32_t>  s_nmAdjNbr;        // CSR neighbour indices
static std::vector<uint8_t>  s_nmReach;         // flood-fill scratch (1 byte/tri)
// transient per-corner z, freed after build
static std::vector<int16_t> s_nmZ0,s_nmZ1,s_nmZ2;
// v0.18.3.139 (#70 Stage 1): PERSISTENT per-corner heights for WorldGroundHeight
// (s_nmZ* above are cleared after Navmesh_Build; these survive for runtime queries)
static std::vector<int16_t> s_nmH0,s_nmH1,s_nmH2;
// v0.18.3.141 (#70 Stage 1): BLOCK-LOCAL height-query index (CSR: triangles grouped
// by their CENTROID's 2048u mesh block). Replicates the engine's per-block search so a
// query never picks an overhang triangle bucketed to a neighbour block. Built lazily on
// the first WorldGroundHeightLocal() call; cleared by Navmesh_Reset. NmGameToMeshX/Y
// shift by a whole number of 2048u blocks, so this mesh-frame grid aligns with the
// engine's block boundaries (X identical; Z mirrored, irrelevant -- triangles and queries
// use the SAME mesh frame).
static const int32_t NM_BLK   = 2048;
static const int     NM_BCOLS = NM_WX / NM_BLK;   // 128
static const int     NM_BROWS = NM_WY / NM_BLK;   // 96
static std::vector<uint32_t> s_blkOff;            // CSR row offsets, size NM_BCOLS*NM_BROWS+1
static std::vector<int32_t>  s_blkTri;            // triangle indices grouped by centroid block
static bool s_blkBuilt = false;
static int  s_nmComponents = 0, s_nmLargest = 0;
static int  s_nmStepBlocked = 0;    // v0.18.3.120 (#70): graph connections dropped by the engine 200-step gate this build
static int  s_nmRoadExempt = 0;     // v0.18.3.128 (#70): connections the gate would have cut but the road-cell exemption KEPT (both centroids at s_roadFine cells)
static bool s_nmBuilt = false;

static void Navmesh_Reset()
{
    s_nmX0.clear(); s_nmY0.clear(); s_nmX1.clear(); s_nmY1.clear();
    s_nmX2.clear(); s_nmY2.clear(); s_nmCX.clear(); s_nmCY.clear();
    s_nmFloor.clear(); s_nmTerr.clear(); s_nmZ0.clear(); s_nmZ1.clear(); s_nmZ2.clear();
    s_nmH0.clear(); s_nmH1.clear(); s_nmH2.clear();
    s_blkOff.clear(); s_blkTri.clear(); s_blkBuilt = false;
    s_nmAdjOff.clear(); s_nmAdjNbr.clear(); s_nmReach.clear();
    s_nmComponents = 0; s_nmLargest = 0; s_nmBuilt = false; s_nmStepBlocked = 0; s_nmRoadExempt = 0;
}

static void Navmesh_AddTriangle(int32_t x0,int32_t y0,int16_t z0,
                                int32_t x1,int32_t y1,int16_t z1,
                                int32_t x2,int32_t y2,int16_t z2, uint8_t terrain)
{
    // v0.18.3.117 (#70): MOUNTAIN EXCLUSION REVERTED. The engine's on-foot
    // world-map walkability is NOT a terrain type -- it is a 200-unit (0xC8)
    // interpolated ground-height STEP gate, decoded from the movement validator
    // 0x53E7A0 in FF8_EN.exe (Plan & Research Documents/WMX_OBJ_FORMAT.md §12).
    // FOOT is exempt from the per-vehicle ground-type bits (0x53E730), so
    // terrain type 29 (mountains) is WALKABLE surface wherever the consecutive
    // height steps stay < 200 -- and the road-pass to Dollet IS terr-29. The
    // walls are the >=200 DISCONTINUITIES (cliff seams), cut by the z-aware
    // T-junction bridge (NM_BRIDGE_ZTOL) and the proximity-link floor gate
    // (NM_LINK_ZSTEP), both now set to the engine's 200. Feeding mountains back
    // restores the road-pass the .116 exclusion deleted; this also retires the
    // wrong .105 "poly[0x0E] bit7" filter (0x0E bit7 is a per-vehicle ground
    // bit, not foot collision) and the failed .114 depth / .115 steepness gates.
    // See issue #70.
    // v0.18.3.120 (#70): the per-triangle SLOPE gate (.119) is REMOVED -- it
    // dropped whole triangles -- 29% of the mesh, including the gentle edges of
    // steep-ish triangles -- and disconnected Dollet. Walkability is now gated
    // per-CONNECTION in Navmesh_Build (NmEngineStepBlocked), dropping only the
    // individual steep MOVES; the 200-step check runs on the exact interpolated
    // surface. (NOTE: the engine's exact steep-terrain rule beyond the 200-step
    // height gate is not fully decoded; see the v0.18.3.121 vertical-penalty
    // note. Do NOT assume the player can switchback up a steep face.) Feed ALL
    // fed triangles here.
    s_nmX0.push_back(x0); s_nmY0.push_back(y0);
    s_nmX1.push_back(x1); s_nmY1.push_back(y1);
    s_nmX2.push_back(x2); s_nmY2.push_back(y2);
    s_nmZ0.push_back(z0); s_nmZ1.push_back(z1); s_nmZ2.push_back(z2);
    s_nmH0.push_back(z0); s_nmH1.push_back(z1); s_nmH2.push_back(z2);   // persistent copy for WorldGroundHeight
    s_nmCX.push_back((x0 + x1 + x2) / 3);
    s_nmCY.push_back((y0 + y1 + y2) / 3);
    s_nmFloor.push_back(((int32_t)z0 + z1 + z2) / 3);
    s_nmTerr.push_back(terrain);
}

static int Navmesh_TriangleCount() { return (int)s_nmX0.size(); }

// BFS over CSR (no gate) to count connected components + largest.
static void NmComputeComponents()
{
    const int N = (int)s_nmX0.size();
    s_nmComponents = 0; s_nmLargest = 0;
    std::vector<uint8_t> seen(N, 0);
    std::vector<int32_t> q; q.reserve(1024);
    for (int s = 0; s < N; s++) {
        if (seen[s]) continue;
        s_nmComponents++;
        int sz = 0; q.clear(); q.push_back(s); seen[s] = 1;
        size_t head = 0;
        while (head < q.size()) {
            int u = q[head++]; sz++;
            for (uint32_t e = s_nmAdjOff[u]; e < s_nmAdjOff[u + 1]; e++) {
                int v = s_nmAdjNbr[e];
                if (!seen[v]) { seen[v] = 1; q.push_back(v); }
            }
        }
        if (sz > s_nmLargest) s_nmLargest = sz;
    }
}

// ---- v0.18.3.120 (#70): engine collision replica, used by Navmesh_Build ----
// NmInterpHeight: the engine's barycentric ground-height interp (0x402620). For
// points OUTSIDE the triangle it extends the plane (valid -- a triangle's 3
// corners define one plane), which is what we want when sampling just past a
// shared edge or across a short bridge/proximity gap.
static double NmInterpHeight(int t, double px, double py)
{
    const double x0=(double)s_nmX0[t], y0=(double)s_nmY0[t];
    const double x1=(double)s_nmX1[t], y1=(double)s_nmY1[t];
    const double x2=(double)s_nmX2[t], y2=(double)s_nmY2[t];
    const double h0=(double)s_nmZ0[t], h1=(double)s_nmZ1[t], h2=(double)s_nmZ2[t];
    const double det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (det < 1e-6 && det > -1e-6) return (h0 + h1 + h2) / 3.0;   // degenerate -> mean
    const double w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / det;
    const double w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / det;
    const double w2 = 1.0 - w0 - w1;
    return w0 * h0 + w1 * h1 + w2 * h2;
}

// NmPointInTri: is (px,py) inside triangle t (mesh coords)? Sign-consistency test.
static bool NmPointInTri(int t, double px, double py)
{
    const double x0=(double)s_nmX0[t], y0=(double)s_nmY0[t];
    const double x1=(double)s_nmX1[t], y1=(double)s_nmY1[t];
    const double x2=(double)s_nmX2[t], y2=(double)s_nmY2[t];
    const double d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
    const double d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
    const double d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0);
    const bool neg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
    const bool pos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);
    return !(neg && pos);
}

// NmEngineStepBlocked: replicate move-validator 0x53E7A0 for the move between
// triangle a's and b's centroids. Walk the centroid->centroid line in ~NM_STEP_
// DIST steps; at each sample take the EXACT interpolated ground height (a's plane
// while inside a, b's while inside b, the nearer plane in a bridge/proximity
// gap); if any consecutive pair's height change reaches NM_HEIGHT_STEP the engine
// would refuse that step, so the connection is blocked. Called only from
// Navmesh_Build (step 3.7), while s_nmZ0/1/2 are still live.
static bool NmEngineStepBlocked(int a, int b)
{
    // v0.18.3.128 (#70): road-cell exemption. Kept (the .144 removal didn't fix the
    // G-Garden stall -- A* still routed into the -1305 pocket via other bridges -- and
    // dropping it hurt seam connectivity). The real fix is the faithful GRID planner
    // (v0.18.3.145), which plans on the actual walkable surface instead of this navmesh;
    // this gate now only matters to legacy navmesh-A* callers.
    bool bothRoad;
    {
        const int afr = WorldYToFineRow(s_nmCY[a] - NM_WY / 2);
        const int afc = WorldXToFineCol(s_nmCX[a] - NM_WX / 2);
        const int bfr = WorldYToFineRow(s_nmCY[b] - NM_WY / 2);
        const int bfc = WorldXToFineCol(s_nmCX[b] - NM_WX / 2);
        bothRoad = (s_roadFine[afr][afc] != 0) && (s_roadFine[bfr][bfc] != 0);
    }
    const double ax = (double)s_nmCX[a], ay = (double)s_nmCY[a];
    const double bx = (double)s_nmCX[b], by = (double)s_nmCY[b];
    double dx = bx - ax, dy = by - ay;
    // torus-shortest delta (connections are local, but guard a seam-spanning pair)
    if (dx >  NM_WX / 2.0) dx -= NM_WX; else if (dx < -NM_WX / 2.0) dx += NM_WX;
    if (dy >  NM_WY / 2.0) dy -= NM_WY; else if (dy < -NM_WY / 2.0) dy += NM_WY;
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0) return false;                       // coincident centroids
    double prevH = NmInterpHeight(a, ax, ay);           // start: a's centroid (inside a)
    double s = NM_STEP_DIST;
    bool last = false;
    while (!last) {
        if (s >= dist) { s = dist; last = true; }
        const double frac = s / dist;
        const double px = ax + dx * frac, py = ay + dy * frac;
        int useTri;
        if (NmPointInTri(a, px, py)) useTri = a;
        else if (NmPointInTri(b, px, py)) useTri = b;
        else useTri = (frac < 0.5) ? a : b;             // gap: extrapolate the nearer plane
        const double h = NmInterpHeight(useTri, px, py);
        double d = h - prevH; if (d < 0.0) d = -d;
        if (d >= NM_HEIGHT_STEP) {                      // engine would refuse this step
            if (bothRoad) { s_nmRoadExempt++; return false; }  // ground-truth road: keep it
            return true;
        }
        prevH = h;
        s += NM_STEP_DIST;
    }
    return false;
}

static bool Navmesh_Build()
{
    const int N = (int)s_nmX0.size();
    if (N == 0) { s_nmBuilt = false; return false; }

    // ---- 1) vertex dedup by (x,y,z) ----
    struct VRec { int32_t x, y; int16_t z; uint32_t tc; };   // tc = tri*3 + corner
    std::vector<VRec> vr; vr.reserve((size_t)N * 3);
    for (int t = 0; t < N; t++) {
        vr.push_back({ s_nmX0[t], s_nmY0[t], s_nmZ0[t], (uint32_t)(t*3+0) });
        vr.push_back({ s_nmX1[t], s_nmY1[t], s_nmZ1[t], (uint32_t)(t*3+1) });
        vr.push_back({ s_nmX2[t], s_nmY2[t], s_nmZ2[t], (uint32_t)(t*3+2) });
    }
    std::sort(vr.begin(), vr.end(), [](const VRec& a, const VRec& b){
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z; });
    std::vector<uint32_t> cornerVid((size_t)N * 3);
    std::vector<int32_t> vidX, vidY, vidZ;     // vid -> coords (vidZ: v0.18.3.107 z-aware bridging)
    int32_t curVid = -1, px = 0, py = 0; int16_t pz = 0; bool first = true;
    for (size_t i = 0; i < vr.size(); i++) {
        const VRec& v = vr[i];
        if (first || v.x != px || v.y != py || v.z != pz) {
            curVid++; vidX.push_back(v.x); vidY.push_back(v.y); vidZ.push_back(v.z);
            px = v.x; py = v.y; pz = v.z; first = false;
        }
        cornerVid[v.tc] = (uint32_t)curVid;
    }
    std::vector<VRec>().swap(vr);

    // ---- 2) edges -> shared-edge adjacency pairs + boundary-edge collection ----
    struct ERec { uint32_t a, b, tri; };
    std::vector<ERec> er; er.reserve((size_t)N * 3);
    for (int t = 0; t < N; t++) {
        uint32_t v0 = cornerVid[t*3+0], v1 = cornerVid[t*3+1], v2 = cornerVid[t*3+2];
        uint32_t e[3][2] = { {v0,v1}, {v1,v2}, {v2,v0} };
        for (int k = 0; k < 3; k++) {
            uint32_t a = e[k][0], b = e[k][1];
            if (a > b) { uint32_t s = a; a = b; b = s; }
            er.push_back({ a, b, (uint32_t)t });
        }
    }
    std::sort(er.begin(), er.end(), [](const ERec& a, const ERec& b){
        if (a.a != b.a) return a.a < b.a;
        if (a.b != b.b) return a.b < b.b;
        return a.tri < b.tri; });

    std::vector<std::pair<int32_t,int32_t>> pairs;
    // v0.18.3.107: zlo/zhi = z at the lo/hi endpoints of the boundary edge, for
    // z-aware T-junction bridging (stitch collinear seams only where z matches).
    struct BRec { int32_t key, lo, hi, zlo, zhi; uint32_t tri; uint8_t vert; };
    std::vector<BRec> bnd;
    {
        size_t i = 0;
        while (i < er.size()) {
            size_t j = i + 1;
            while (j < er.size() && er[j].a == er[i].a && er[j].b == er[i].b) j++;
            if (j - i >= 2) {
                for (size_t p = i; p < j; p++)
                    for (size_t q = p + 1; q < j; q++)
                        pairs.push_back({ (int32_t)er[p].tri, (int32_t)er[q].tri });
            } else {
                uint32_t va = er[i].a, vb = er[i].b;
                int32_t ax = vidX[va], ay = vidY[va], az = vidZ[va];
                int32_t bx = vidX[vb], by = vidY[vb], bz = vidZ[vb];
                if (ax == bx) {            // vertical edge: range/z along y
                    if (ay < by) bnd.push_back({ ax, ay, by, az, bz, er[i].tri, 1 });
                    else         bnd.push_back({ ax, by, ay, bz, az, er[i].tri, 1 });
                } else if (ay == by) {     // horizontal edge: range/z along x
                    if (ax < bx) bnd.push_back({ ay, ax, bx, az, bz, er[i].tri, 0 });
                    else         bnd.push_back({ ay, bx, ax, bz, az, er[i].tri, 0 });
                }
            }
            i = j;
        }
    }
    std::vector<ERec>().swap(er);

    // ---- 3) T-junction bridging: connect overlapping collinear boundary edges ----
    // v0.18.3.120: emit every collinear-overlap candidate UNGATED; the engine
    // 200-step gate (step 3.7) decides walkability uniformly (a cliff-seam's
    // top/base centroids differ >= 200 -> dropped; a gentle T-junction seam is
    // continuous -> kept). Replaces the .107 NM_BRIDGE_ZTOL edge-z match.
    std::sort(bnd.begin(), bnd.end(), [](const BRec& a, const BRec& b){
        if (a.vert != b.vert) return a.vert < b.vert;
        if (a.key  != b.key)  return a.key  < b.key;
        return a.lo < b.lo; });
    {
        size_t k = 0;
        while (k < bnd.size()) {
            size_t m = k + 1;
            while (m < bnd.size() && bnd[m].vert == bnd[k].vert && bnd[m].key == bnd[k].key) m++;
            for (size_t p = k; p < m; p++) {
                for (size_t q = p + 1; q < m; q++) {
                    if (bnd[q].lo >= bnd[p].hi) break;          // sorted by lo
                    if (bnd[q].tri == bnd[p].tri) continue;
                    int32_t olo = (bnd[p].lo > bnd[q].lo ? bnd[p].lo : bnd[q].lo);
                    int32_t ohi = (bnd[p].hi < bnd[q].hi ? bnd[p].hi : bnd[q].hi);
                    if (olo >= ohi) continue;                   // require real overlap
                    pairs.push_back({ (int32_t)bnd[p].tri, (int32_t)bnd[q].tri });
                }
            }
            k = m;
        }
    }
    std::vector<BRec>().swap(bnd);

    // ---- 3.5) step-distance proximity links (v0.18.3.105) ----
    // The engine validates the DESTINATION polygon of each ~190u step, so the
    // character hops between nearby walkable triangles across sub-edge-width
    // necks that share no edge/vertex -- the Dollet approach pinches through
    // mountain slivers exactly this way (4 walkable + 11 mountain tris in the
    // choke). All fed triangles are already walkability-filtered (poly[0x0E]
    // bit7), so every link is walkable-to-walkable. Connect centroids within
    // NM_STEP_LINK via a 512-unit bucket grid (>= link radius, so the 3x3
    // neighbourhood covers it). See issue #70.
    {
        const int64_t LINK = NM_STEP_LINK;
        const int32_t CELL = 512;
        const int32_t NBX  = (NM_WX / CELL) + 1;
        std::vector<std::pair<int64_t,int32_t>> bk; bk.reserve(N);
        for (int t = 0; t < N; t++) {
            int32_t bx = s_nmCX[t] / CELL, by = s_nmCY[t] / CELL;
            bk.push_back({ (int64_t)by * NBX + bx, t });
        }
        std::sort(bk.begin(), bk.end());
        for (int t = 0; t < N; t++) {
            int32_t bx = s_nmCX[t] / CELL, by = s_nmCY[t] / CELL;
            int32_t cx = s_nmCX[t], cy = s_nmCY[t];
            for (int dby = -1; dby <= 1; dby++) {
                for (int dbx = -1; dbx <= 1; dbx++) {
                    int64_t key = (int64_t)(by + dby) * NBX + (bx + dbx);
                    auto lo = std::lower_bound(bk.begin(), bk.end(),
                                  std::make_pair(key, (int32_t)-1));
                    for (auto it = lo; it != bk.end() && it->first == key; ++it) {
                        int u = it->second;
                        if (u <= t) continue;
                        int64_t ddx = (int64_t)s_nmCX[u] - cx;
                        int64_t ddy = (int64_t)s_nmCY[u] - cy;
                        if (ddx * ddx + ddy * ddy > LINK * LINK) continue;
                        // v0.18.3.120: emit the proximity candidate UNGATED; the
                        // engine 200-step gate (step 3.7) decides walkability (a
                        // cliff base<->top neck samples a >= 200 step -> dropped).
                        pairs.push_back({ t, u });
                    }
                }
            }
        }
    }

    // ---- 3.6) road-cell proximity bridge (v0.18.3.133 = reverted .131, #70) ----
    // REVERTED from .132's nearest-pair bridge. .132 bridged only ONE (nearest)
    // triangle pair per 8-adjacent road cell pair (1267 bridges) -- that connected
    // the road too LOOSELY: [ROADCON] navA* jumped back to the pre-bridge .130
    // values (17-20 tris between samples vs .131's 4), and the sparse corridor
    // FROZE the on-foot drive ~1-2 cells from spawn (idx stuck 0/28). .131's dense
    // radius bridge (THIS code: every road-cell triangle pair whose fine cells are
    // 8-ADJACENT and centroids within ROAD_BRIDGE) gave the smooth-to-4km drive,
    // so it's restored. It connects the road BODY but NOT the synthetic gap cell
    // c113,r56 (still SEVERED, see [GAPDIAG] below). Road-only = SAFE; the .128
    // exemption keeps the road-road moves through the gate (step 3.7).
    {
        const int64_t RB   = (int64_t)ROAD_BRIDGE;
        const int32_t CELL = ROAD_BRIDGE;             // >= radius so the 3x3 bucket neighbourhood covers it
        const int32_t NBX  = (NM_WX / CELL) + 1;
        struct RT { int32_t t, fr, fc; };
        std::vector<RT> rt; rt.reserve(2048);
        for (int t = 0; t < N; t++) {
            const int fr = WorldYToFineRow(s_nmCY[t] - NM_WY / 2);
            const int fc = WorldXToFineCol(s_nmCX[t] - NM_WX / 2);
            if (fr < 0 || fr >= WM_FINE_ROWS || fc < 0 || fc >= WM_FINE_COLS) continue;
            if (!s_roadFine[fr][fc]) continue;
            rt.push_back({ t, fr, fc });
        }
        std::vector<std::pair<int64_t,int32_t>> bk; bk.reserve(rt.size());
        for (size_t i = 0; i < rt.size(); i++) {
            const int32_t bx = s_nmCX[rt[i].t] / CELL, by = s_nmCY[rt[i].t] / CELL;
            bk.push_back({ (int64_t)by * NBX + bx, (int32_t)i });
        }
        std::sort(bk.begin(), bk.end());
        int roadBridged = 0;
        for (size_t i = 0; i < rt.size(); i++) {
            const int t = rt[i].t;
            const int32_t bx = s_nmCX[t] / CELL, by = s_nmCY[t] / CELL;
            const int32_t cx = s_nmCX[t], cy = s_nmCY[t];
            for (int dby = -1; dby <= 1; dby++) {
                for (int dbx = -1; dbx <= 1; dbx++) {
                    const int64_t key = (int64_t)(by + dby) * NBX + (bx + dbx);
                    auto lo = std::lower_bound(bk.begin(), bk.end(),
                                  std::make_pair(key, (int32_t)-1));
                    for (auto it = lo; it != bk.end() && it->first == key; ++it) {
                        const RT& U = rt[(size_t)it->second];
                        if (U.t <= t) continue;
                        int dr = U.fr - rt[i].fr; if (dr < 0) dr = -dr;
                        int dc = U.fc - rt[i].fc; if (dc < 0) dc = -dc;
                        if (dr > 1 || dc > 1) continue;          // 8-adjacent fine cells only
                        const int64_t ddx = (int64_t)s_nmCX[U.t] - cx;
                        const int64_t ddy = (int64_t)s_nmCY[U.t] - cy;
                        if (ddx * ddx + ddy * ddy > RB * RB) continue;
                        pairs.push_back({ t, U.t });
                        roadBridged++;
                    }
                }
            }
        }
        Log::World("WorldMap: [NMROADBRIDGE] %d road tris -> %d cell-adjacent road bridge pairs (radius %d) added pre-gate", (int)rt.size(), roadBridged, (int)RB);
    }

    // ---- 3.6b) gap-cell connectivity probe (v0.18.3.133, #70, READ-ONLY) ----
    // .131 (dense radius 1536) and .132 (nearest-pair, cap 3072) BOTH left
    // [ROADCON] road[0] fine(c113,r56) SEVERED -- so the gap cell's triangle is
    // farther than 3072u from any road triangle in an 8-adjacent cell, OR its
    // road-side neighbour cells aren't road. Log the gap cell's 8 neighbours'
    // road status and, for each triangle whose centroid lands in c113,r56, its
    // GLOBAL nearest OTHER road triangle (cell + floorZ + distance). That says
    // exactly which cell/triangle to connect it to so the next fix isn't a guess.
    // Pure logging -- no pairs added.
    {
        const int GR = 56, GC = 113;
        for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
            const int nr = GR + dr, nc = GC + dc;
            if (nr < 0 || nr >= WM_FINE_ROWS || nc < 0 || nc >= WM_FINE_COLS) continue;
            Log::World("WorldMap: [GAPDIAG] neighbour fine(c%d,r%d) road=%d", nc, nr, (int)s_roadFine[nr][nc]);
        }
        for (int t = 0; t < N; t++) {
            const int fr = WorldYToFineRow(s_nmCY[t] - NM_WY / 2);
            const int fc = WorldXToFineCol(s_nmCX[t] - NM_WX / 2);
            if (fr != GR || fc != GC) continue;
            int64_t bestD = -1; int bt = -1;
            for (int u = 0; u < N; u++) {
                if (u == t) continue;
                const int ur = WorldYToFineRow(s_nmCY[u] - NM_WY / 2);
                const int uc = WorldXToFineCol(s_nmCX[u] - NM_WX / 2);
                if (ur < 0 || ur >= WM_FINE_ROWS || uc < 0 || uc >= WM_FINE_COLS) continue;
                if (!s_roadFine[ur][uc]) continue;
                const int64_t ddx = (int64_t)s_nmCX[t] - s_nmCX[u];
                const int64_t ddy = (int64_t)s_nmCY[t] - s_nmCY[u];
                const int64_t d = ddx * ddx + ddy * ddy;
                if (bestD < 0 || d < bestD) { bestD = d; bt = u; }
            }
            if (bt >= 0) {
                const int br = WorldYToFineRow(s_nmCY[bt] - NM_WY / 2);
                const int bc = WorldXToFineCol(s_nmCX[bt] - NM_WX / 2);
                Log::World("WorldMap: [GAPDIAG] gap tri#%d game(%d,%d) floorZ=%d terr=%d -> nearest road tri#%d fine(c%d,r%d) floorZ=%d dist=%d",
                           t, s_nmCX[t] - NM_WX / 2, s_nmCY[t] - NM_WY / 2, s_nmFloor[t], (int)s_nmTerr[t],
                           bt, bc, br, s_nmFloor[bt], (int)std::sqrt((double)bestD));
            }
        }
    }

    // ---- 3.7) engine-faithful collision gate: drop every candidate connection
    //          the engine would refuse (v0.18.3.120, #70) ----
    // Replicate move-validator 0x53E7A0 on every candidate (shared-edge / bridge
    // / proximity): sample the EXACT interpolated ground height (NmInterpHeight =
    // barycentric interp 0x402620) along the centroid->centroid move at ~NM_STEP_
    // DIST intervals and drop the connection if ANY step's height change reaches
    // NM_HEIGHT_STEP (200). Gating CONNECTIONS (not triangles) drops only the
    // steep moves and keeps each triangle's gentle edges, so the mesh stays
    // connected. s_nmZ0/1/2 are still live here (freed in step 4 below).
    {
        std::vector<std::pair<int32_t,int32_t>> kept;
        kept.reserve(pairs.size());
        for (auto& pr : pairs) {
            if (NmEngineStepBlocked(pr.first, pr.second)) { s_nmStepBlocked++; continue; }
            kept.push_back(pr);
        }
        pairs.swap(kept);
    }

    // ---- 4) build CSR adjacency (both directions, deduped) ----
    std::vector<std::pair<int32_t,int32_t>> dir;
    dir.reserve(pairs.size() * 2);
    for (auto& pr : pairs) { dir.push_back({ pr.first, pr.second }); dir.push_back({ pr.second, pr.first }); }
    std::vector<std::pair<int32_t,int32_t>>().swap(pairs);
    std::sort(dir.begin(), dir.end());
    dir.erase(std::unique(dir.begin(), dir.end()), dir.end());
    s_nmAdjOff.assign(N + 1, 0);
    for (auto& d : dir) s_nmAdjOff[d.first + 1]++;
    for (int t = 0; t < N; t++) s_nmAdjOff[t + 1] += s_nmAdjOff[t];
    s_nmAdjNbr.resize(dir.size());
    {
        std::vector<uint32_t> cur(s_nmAdjOff.begin(), s_nmAdjOff.end());
        for (auto& d : dir) s_nmAdjNbr[cur[d.first]++] = d.second;
    }
    std::vector<std::pair<int32_t,int32_t>>().swap(dir);

    // free transient build buffers
    std::vector<uint32_t>().swap(cornerVid);
    std::vector<int16_t>().swap(s_nmZ0);
    std::vector<int16_t>().swap(s_nmZ1);
    std::vector<int16_t>().swap(s_nmZ2);

    s_nmReach.assign(N, 0);
    NmComputeComponents();
    s_nmBuilt = true;
    return true;
}

// ---- point-in-triangle (mesh coords); returns tri index or -1 ----
static inline int64_t NmCross(int64_t ax,int64_t ay,int64_t bx,int64_t by,int64_t cx,int64_t cy)
{ return (ax - cx) * (by - cy) - (bx - cx) * (ay - cy); }

static int Navmesh_FindTriangleMesh(int32_t mx, int32_t my)
{
    const int N = (int)s_nmX0.size();
    int best = -1; int64_t bestD = -1;
    for (int t = 0; t < N; t++) {
        int32_t x0=s_nmX0[t],y0=s_nmY0[t],x1=s_nmX1[t],y1=s_nmY1[t],x2=s_nmX2[t],y2=s_nmY2[t];
        int32_t lo,hi;
        lo = x0<x1?(x0<x2?x0:x2):(x1<x2?x1:x2); hi = x0>x1?(x0>x2?x0:x2):(x1>x2?x1:x2);
        if (mx < lo || mx > hi) continue;
        lo = y0<y1?(y0<y2?y0:y2):(y1<y2?y1:y2); hi = y0>y1?(y0>y2?y0:y2):(y1>y2?y1:y2);
        if (my < lo || my > hi) continue;
        int64_t d1 = NmCross(mx,my,x0,y0,x1,y1);
        int64_t d2 = NmCross(mx,my,x1,y1,x2,y2);
        int64_t d3 = NmCross(mx,my,x2,y2,x0,y0);
        bool neg = (d1<0)||(d2<0)||(d3<0);
        bool pos = (d1>0)||(d2>0)||(d3>0);
        if (!(neg && pos)) {
            int64_t dx = s_nmCX[t]-mx, dy = s_nmCY[t]-my; int64_t d = dx*dx+dy*dy;
            if (best < 0 || d < bestD) { best = t; bestD = d; }
        }
    }
    if (best >= 0) return best;
    // fallback: nearest centroid
    for (int t = 0; t < N; t++) {
        int64_t dx = s_nmCX[t]-mx, dy = s_nmCY[t]-my; int64_t d = dx*dx+dy*dy;
        if (best < 0 || d < bestD) { best = t; bestD = d; }
    }
    return best;
}

static int Navmesh_FindTriangleGame(int32_t gx, int32_t gy)
{ return Navmesh_FindTriangleMesh(NmGameToMeshX(gx), NmGameToMeshY(gy)); }

// v0.18.3.106: mesh-space centroid of a triangle, converted back to engine
// (game) coords -- the inverse of NmGameToMeshX/Y. Centroids live in [0,NM_W),
// so subtracting the half-world bias yields the canonical signed game range.
// Used by the planner to turn an A* triangle path into fine-cell waypoints.
static bool Navmesh_TriangleCentroidGame(int tri, int32_t* gx, int32_t* gy)
{
    if (tri < 0 || tri >= (int)s_nmCX.size()) return false;
    if (gx) *gx = s_nmCX[tri] - (NM_WX / 2);
    if (gy) *gy = s_nmCY[tri] - (NM_WY / 2);
    return true;
}

// flood-fill from startTri across edges whose floor-step <= gate; fills
// s_nmReach, returns component size. gate<0 -> no gate.
static int Navmesh_FloodFrom(int startTri, int gate)
{
    const int N = (int)s_nmX0.size();
    std::fill(s_nmReach.begin(), s_nmReach.end(), 0);
    if (startTri < 0 || startTri >= N) return 0;
    std::vector<int32_t> q; q.reserve(1024);
    q.push_back(startTri); s_nmReach[startTri] = 1; int cnt = 1; size_t head = 0;
    while (head < q.size()) {
        int u = q[head++];
        for (uint32_t e = s_nmAdjOff[u]; e < s_nmAdjOff[u + 1]; e++) {
            int v = s_nmAdjNbr[e];
            if (s_nmReach[v]) continue;
            if (gate >= 0) { int32_t st = s_nmFloor[u]-s_nmFloor[v]; if (st<0) st=-st; if (st > gate) continue; }
            s_nmReach[v] = 1; q.push_back(v); cnt++;
        }
    }
    return cnt;
}

static bool Navmesh_IsReachable(int tri)
{ return tri >= 0 && tri < (int)s_nmReach.size() && s_nmReach[tri] != 0; }

// wrapped-torus distance between two centroids
static double NmCentroidDist(int a, int b)
{
    double dx = s_nmCX[a]-s_nmCX[b]; if (dx<0) dx=-dx;
    double dy = s_nmCY[a]-s_nmCY[b]; if (dy<0) dy=-dy;
    if (dx > NM_WX/2.0) dx = NM_WX - dx;
    if (dy > NM_WY/2.0) dy = NM_WY - dy;
    return std::sqrt(dx*dx + dy*dy);
}

// v0.18.3.121 (#70): A* edge-cost weight on |delta floor-z|. The engine gate
// keeps a route off true cliffs, but the canyon to Dollet is engine-WALKABLE by
// the 200-step test (gentle centroid steps) yet deep -- and the .120 BAT showed
// A* still dives into it (floor -679 -> -1601 -> out) because the base cost
// NmCentroidDist is HORIZONTAL only, so vertical travel is free and the canyon
// is the shortest horizontal line. The executor then wedges at the canyon wall
// (F11). (Caveat: the 200-step centroid sample can PASS a steep stretch the
// player cannot actually traverse in the needed direction -- steep terrain is
// where the walkmesh model is least reliable -- which is the real reason to
// avoid it, NOT a switchback assumption.) This adds NM_VERT_WEIGHT * the
// per-step |floorZ[u]-floorZ[v]| to each edge so a deep up-and-down route is
// expensive and A* prefers the flatter .107 corridor, which the executor can
// follow. Cost-shaping ONLY -- connectivity is unchanged, so it cannot
// disconnect Dollet; only the preferred route shifts. The horizontal heuristic
// stays admissible (a lower bound on the horizontal part of the remaining cost).
// Tunable: RAISE if A* still dives into the canyon, LOWER if the route takes an
// over-long flat detour. v0.18.3.122: raised 5->20 after the .121 BAT (W=5 was
// active -- A*->Dollet cost 30259->57748, route 85->90 tris, drive +1000u -- but
// still dived to floorZ -1567). A hard floor-z gate is NOT an option (the .114
// gate disconnected Dollet: its coastal approach also dips deep). So this is a
// decisive test: if A* STILL dives at 20, no shallower route to Dollet exists and
// the fix moves to the executor (#68: follow the navmesh centroids directly).
static const double NM_VERT_WEIGHT = 20.0;

// v0.18.3.114 (#70): absolute floor-z floor for A*. Triangles whose floor-z is
// BELOW this are skipped during the search, so A* cannot route DOWN into a deep
// ravine and back out (the Dollet drive's box canyon: floor -1087..-1665, far
// below the start -731 / Dollet -264 band). Default INT32_MIN = no floor (the
// connectivity diag at Initialize stays ungated); PlanDrivePathNavmesh sets it
// to -1050 before its A* call. A floor-STEP gate can't separate the canyon (its
// descent steps 158..235u overlap the gentle corridor's 176u bottleneck), but
// the canyon's DEPTH is unambiguous.
static int32_t s_nmAStarFloorMin = INT32_MIN;

// A* over the triangle graph, gate on floor-step (+ s_nmAStarFloorMin depth
// floor). Fills outPath (start..goal) if non-null. Returns path length in world
// units, or -1 if no path.
static double Navmesh_AStar(int startTri, int goalTri, int gate, std::vector<int>* outPath)
{
    const int N = (int)s_nmX0.size();
    if (startTri < 0 || goalTri < 0 || startTri >= N || goalTri >= N) return -1.0;
    std::vector<double> g(N, 1e30);
    std::vector<int32_t> came(N, -1);
    std::vector<uint8_t> closed(N, 0);
    struct Node { double f; int t; };
    struct Cmp { bool operator()(const Node&a, const Node&b) const { return a.f > b.f; } };
    std::vector<Node> heap;
    g[startTri] = 0.0;
    heap.push_back({ NmCentroidDist(startTri, goalTri), startTri });
    std::push_heap(heap.begin(), heap.end(), Cmp());
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), Cmp());
        Node nd = heap.back(); heap.pop_back();
        int u = nd.t;
        if (u == goalTri) break;
        if (closed[u]) continue;
        closed[u] = 1;
        for (uint32_t e = s_nmAdjOff[u]; e < s_nmAdjOff[u + 1]; e++) {
            int v = s_nmAdjNbr[e];
            if (s_nmFloor[v] < s_nmAStarFloorMin) continue;   // v0.18.3.114 (#70): skip deep-ravine triangles
            if (gate >= 0) { int32_t st = s_nmFloor[u]-s_nmFloor[v]; if (st<0) st=-st; if (st > gate) continue; }
            double dz = (double)s_nmFloor[u] - (double)s_nmFloor[v]; if (dz < 0.0) dz = -dz;
            double nw = g[u] + NmCentroidDist(u, v) + NM_VERT_WEIGHT * dz;   // v0.18.3.121 (#70): vertical-travel penalty
            if (nw < g[v]) {
                g[v] = nw; came[v] = u;
                heap.push_back({ nw + NmCentroidDist(v, goalTri), v });
                std::push_heap(heap.begin(), heap.end(), Cmp());
            }
        }
    }
    if (g[goalTri] >= 1e30) return -1.0;
    if (outPath) {
        outPath->clear();
        for (int t = goalTri; t != -1; t = came[t]) outPath->push_back(t);
        std::reverse(outPath->begin(), outPath->end());
    }
    return g[goalTri];
}

// ============================================================================
// v0.18.3.108 (#68/#70): FUNNEL (string-pulling) over the A* triangle corridor.
// ============================================================================
// The old centroid waypoints cut corners: the #68 executor's steer-target
// lookahead aimed across walls the winding route goes AROUND, and its coarse
// 1024u fine-grid LOS clamp could not catch the sub-cell wall, so the drive
// wedged (the Dollet/Timber BATs). The funnel threads the A* TRIANGLE path
// through its PORTALS (shared edges) with the Simple Stupid Funnel Algorithm
// (Mononen), producing the minimal turning points whose connecting straight
// legs stay INSIDE the walkable corridor by construction -- so the steer target
// can never aim across a wall. Non-shared-edge links (T-junction bridges,
// proximity links) have no clean portal; they become a DEGENERATE portal at the
// next triangle's centroid (a point strictly inside walkable geometry), pinning
// the string through that neck. Returns corner points in GAME coords; the
// planner rasterizes them into dense fine cells the executor follows leg-by-leg.

// 2x signed area of (a,b,c); > 0 iff c is to the LEFT of directed a->b.
static inline double NmTriArea2(double ax,double ay,double bx,double by,double cx,double cy)
{ return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay); }

// shared mesh vertices between two triangles (exact (x,y) match); fills up to 2
// into sx/sy, returns the count (a real shared edge gives 2).
static int NmSharedVerts(int t0, int t1, int32_t* sx, int32_t* sy)
{
    const int32_t ax[3] = { s_nmX0[t0], s_nmX1[t0], s_nmX2[t0] };
    const int32_t ay[3] = { s_nmY0[t0], s_nmY1[t0], s_nmY2[t0] };
    const int32_t bx[3] = { s_nmX0[t1], s_nmX1[t1], s_nmX2[t1] };
    const int32_t by[3] = { s_nmY0[t1], s_nmY1[t1], s_nmY2[t1] };
    int n = 0;
    for (int i = 0; i < 3 && n < 2; i++)
        for (int j = 0; j < 3; j++)
            if (ax[i] == bx[j] && ay[i] == by[j]) {
                bool dup = false;
                for (int k = 0; k < n; k++) if (sx[k]==ax[i] && sy[k]==ay[i]) { dup = true; break; }
                if (!dup) { sx[n] = ax[i]; sy[n] = ay[i]; n++; }
                break;
            }
    return n;
}

// Funnel a start/goal through the A* triangle corridor. outGX/outGY <- corner
// points in GAME coords (>= 2 on success). *outTriCount (optional) <- A* tri
// count. Returns false if A* finds no path.
static bool Navmesh_FunnelPath(int startTri, int goalTri,
                               int32_t startGX, int32_t startGY,
                               int32_t goalGX,  int32_t goalGY,
                               std::vector<int32_t>& outGX,
                               std::vector<int32_t>& outGY,
                               int* outTriCount)
{
    outGX.clear(); outGY.clear();
    std::vector<int> tris;
    double L = Navmesh_AStar(startTri, goalTri, -1, &tris);
    if (outTriCount) *outTriCount = (int)tris.size();
    if (L < 0.0 || tris.empty()) return false;

    // funnel runs in MESH coords (consistent with the stored vertices).
    const double sX = (double)NmGameToMeshX(startGX), sY = (double)NmGameToMeshY(startGY);
    const double gX = (double)NmGameToMeshX(goalGX),  gY = (double)NmGameToMeshY(goalGY);

    // ---- build the portal list: [start,start] | per-pair gate | [goal,goal] ----
    std::vector<double> pLX, pLY, pRX, pRY;
    pLX.push_back(sX); pLY.push_back(sY); pRX.push_back(sX); pRY.push_back(sY);
    for (size_t i = 0; i + 1 < tris.size(); i++) {
        const int t0 = tris[i], t1 = tris[i + 1];
        int32_t svx[2], svy[2];
        const int ns = NmSharedVerts(t0, t1, svx, svy);
        if (ns >= 2) {
            const double c0x = (double)s_nmCX[t0], c0y = (double)s_nmCY[t0];
            const double c1x = (double)s_nmCX[t1], c1y = (double)s_nmCY[t1];
            const double Ax = (double)svx[0], Ay = (double)svy[0];
            const double Bx = (double)svx[1], By = (double)svy[1];
            // Orient the gate: the shared vertex to the LEFT of travel (c0->c1)
            // is the funnel's left, the other its right. If a BAT shows the
            // funnel hugging the WRONG wall (consistently cutting one side of
            // corners), flip this single comparison ( > 0  ->  < 0 ).
            if (NmTriArea2(c0x, c0y, c1x, c1y, Ax, Ay) > 0.0) {
                pLX.push_back(Ax); pLY.push_back(Ay); pRX.push_back(Bx); pRY.push_back(By);
            } else {
                pLX.push_back(Bx); pLY.push_back(By); pRX.push_back(Ax); pRY.push_back(Ay);
            }
        } else {
            // bridge / proximity link: no shared edge -> degenerate portal at
            // the entered triangle's centroid (strictly inside walkable geom).
            const double cx = (double)s_nmCX[t1], cy = (double)s_nmCY[t1];
            pLX.push_back(cx); pLY.push_back(cy); pRX.push_back(cx); pRY.push_back(cy);
        }
    }
    pLX.push_back(gX); pLY.push_back(gY); pRX.push_back(gX); pRY.push_back(gY);

    // ---- Simple Stupid Funnel Algorithm (Mononen / recastnavigation) ----
    std::vector<double> cX, cY;                 // corners (mesh coords)
    double apX = pLX[0], apY = pLY[0];          // apex
    double poLX = pLX[0], poLY = pLY[0];        // funnel left edge endpoint
    double poRX = pRX[0], poRY = pRY[0];        // funnel right edge endpoint
    size_t apI = 0, leI = 0, riI = 0;
    cX.push_back(apX); cY.push_back(apY);
    const int M = (int)pLX.size();
    int guard = 0; const int guardMax = 8 * M + 16;   // bound restarts (safety)
    for (int i = 1; i < M && guard < guardMax; ) {
        guard++;
        const double LX = pLX[i], LY = pLY[i];
        const double RX = pRX[i], RY = pRY[i];
        // --- right side ---
        if (NmTriArea2(apX, apY, poRX, poRY, RX, RY) <= 0.0) {
            if ((apX == poRX && apY == poRY) ||
                NmTriArea2(apX, apY, poLX, poLY, RX, RY) > 0.0) {
                poRX = RX; poRY = RY; riI = (size_t)i;          // tighten right
            } else {
                cX.push_back(poLX); cY.push_back(poLY);         // left is a corner
                apX = poLX; apY = poLY; apI = leI;
                poLX = apX; poLY = apY; leI = apI;
                poRX = apX; poRY = apY; riI = apI;
                i = (int)apI + 1; continue;
            }
        }
        // --- left side ---
        if (NmTriArea2(apX, apY, poLX, poLY, LX, LY) >= 0.0) {
            if ((apX == poLX && apY == poLY) ||
                NmTriArea2(apX, apY, poRX, poRY, LX, LY) < 0.0) {
                poLX = LX; poLY = LY; leI = (size_t)i;          // tighten left
            } else {
                cX.push_back(poRX); cY.push_back(poRY);         // right is a corner
                apX = poRX; apY = poRY; apI = riI;
                poLX = apX; poLY = apY; leI = apI;
                poRX = apX; poRY = apY; riI = apI;
                i = (int)apI + 1; continue;
            }
        }
        i++;
    }
    cX.push_back(gX); cY.push_back(gY);

    // ---- mesh -> game (exact: SSF only copies portal points), dedup ----
    for (size_t i = 0; i < cX.size(); i++) {
        const int32_t cgx = (int32_t)(cX[i] + 0.5) - (NM_WX / 2);
        const int32_t cgy = (int32_t)(cY[i] + 0.5) - (NM_WY / 2);
        if (!outGX.empty() && outGX.back() == cgx && outGY.back() == cgy) continue;
        outGX.push_back(cgx); outGY.push_back(cgy);
    }
    return outGX.size() >= 2;
}

static void Navmesh_ComponentStats(int* numComponents, int* largest)
{ if (numComponents) *numComponents = s_nmComponents; if (largest) *largest = s_nmLargest; }

// ============================================================================
// v0.18.3.139 (#70 Stage 1): EXACT ground height at a game coord -- replicating
// the engine's on-foot height interpolation (find-polygon 0x53EB80 -> point-in-
// triangle + plane interp 0x402620; WMX_OBJ_FORMAT.md S12). Point-locate the
// triangle under (gx,gy), then barycentrically interpolate the height from its
// three corner heights. This is the exact quantity the engine's 200-unit on-foot
// collision gate (0x53E7A0) compares; Stage 2 will gate the on-foot steering on
// |dH| >= 200 the same way. Returns INT_MIN when no triangle covers the point
// (off-mesh / water -> caller treats as blocked). Heights are mesh/world units,
// UP = NEGATIVE (PSX), the SAME frame as the engine's live value at 0x0203FE30
// (which Stage 1 logs alongside this to prove the replication is faithful).
// Three points define one plane, so any correct barycentric interp yields the
// engine's number to sub-unit rounding -- no need to mirror its fixed-point ops.
static const int WGH_NO_GROUND = 0x7FFFFFFF;   // WorldGroundHeight sentinel: no triangle under the point (heights are always <= 0)
// v0.18.3.140 (#70 Stage 1): set to 1 to log [GROUNDH] every world-map frame
// (validates WorldGroundHeight vs the engine's live 0x0203FE30). Flip to 0 to silence.
#define GROUNDH_VALIDATE 1
// Optional out-params (all default null) expose the chosen triangle, its 3 corner
// heights, and the barycentric weights -- used by the [GROUNDH] validation probe to
// check whether the engine's height falls WITHIN our triangle and whether the offset
// is constant. Stage 2 calls WorldGroundHeight(gx,gy) with no out-params (no overhead).
static int WorldGroundHeight(int32_t gx, int32_t gy,
                             int* outTri=nullptr, int* outH0=nullptr, int* outH1=nullptr, int* outH2=nullptr,
                             double* outA=nullptr, double* outB=nullptr, double* outC=nullptr)
{
    int t = Navmesh_FindTriangleGame(gx, gy);
    if (outTri) *outTri = t;
    if (t < 0 || t >= (int)s_nmH0.size()) return WGH_NO_GROUND;
    if (outH0) *outH0 = s_nmH0[t];
    if (outH1) *outH1 = s_nmH1[t];
    if (outH2) *outH2 = s_nmH2[t];
    // work in MESH coords (consistent with the stored triangle vertices).
    double mx = (double)NmGameToMeshX(gx), my = (double)NmGameToMeshY(gy);
    double x0 = (double)s_nmX0[t], y0 = (double)s_nmY0[t];
    double x1 = (double)s_nmX1[t], y1 = (double)s_nmY1[t];
    double x2 = (double)s_nmX2[t], y2 = (double)s_nmY2[t];
    double denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (denom > -1e-6 && denom < 1e-6) {
        if (outA) *outA = 1.0/3.0; if (outB) *outB = 1.0/3.0; if (outC) *outC = 1.0/3.0;
        return ((int)s_nmH0[t] + (int)s_nmH1[t] + (int)s_nmH2[t]) / 3;   // degenerate sliver -> mean
    }
    double a = ((y1 - y2) * (mx - x2) + (x2 - x1) * (my - y2)) / denom;
    double b = ((y2 - y0) * (mx - x2) + (x0 - x2) * (my - y2)) / denom;
    double c = 1.0 - a - b;
    if (outA) *outA = a; if (outB) *outB = b; if (outC) *outC = c;
    double h = a * (double)s_nmH0[t] + b * (double)s_nmH1[t] + c * (double)s_nmH2[t];
    return (int)(h < 0.0 ? h - 0.5 : h + 0.5);   // round to nearest
}

// v0.18.3.141 (#70 Stage 1): centroid -> 2048u mesh-block index (clamped).
static inline int NmBlockOf(int32_t mx, int32_t my)
{
    int bc = (int)(mx / NM_BLK); if (bc < 0) bc = 0; else if (bc >= NM_BCOLS) bc = NM_BCOLS - 1;
    int br = (int)(my / NM_BLK); if (br < 0) br = 0; else if (br >= NM_BROWS) br = NM_BROWS - 1;
    return br * NM_BCOLS + bc;
}

// v0.18.3.141 (#70 Stage 1): build the block-local triangle index (lazy, once).
static void NmBuildBlockIndex()
{
    const int N = (int)s_nmCX.size();
    const int B = NM_BCOLS * NM_BROWS;
    s_blkOff.assign(B + 1, 0);
    for (int t = 0; t < N; t++) s_blkOff[NmBlockOf(s_nmCX[t], s_nmCY[t]) + 1]++;
    for (int i = 0; i < B; i++) s_blkOff[i + 1] += s_blkOff[i];
    s_blkTri.resize(N);
    std::vector<uint32_t> cur(s_blkOff.begin(), s_blkOff.end());
    for (int t = 0; t < N; t++) s_blkTri[cur[NmBlockOf(s_nmCX[t], s_nmCY[t])]++] = t;
    s_blkBuilt = true;
    Log::World("WorldMap: [GROUNDHL] block index built: %d tris over %dx%d 2048u blocks", N, NM_BCOLS, NM_BROWS);
}

// v0.18.3.141 (#70 Stage 1): engine-faithful BLOCK-LOCAL ground height. Restricts the
// triangle search to the point's home 2048u block + its 8 neighbours (torus-wrapped) and,
// among containing triangles, PREFERS one bucketed to the home block -- so a neighbour's
// overhang loses to the home block's own ground (fixes the global search's nearest-centroid
// mispick: Balamb Garden ourH -201 vs engine -545). Returns WGH_NO_GROUND when nothing in
// the 3x3 contains the point (honest, vs the global search extrapolating a far triangle's
// plane -> the beach garbage). *outContain = how many triangles covered the point (overlap
// visibility). Same barycentric math + mesh frame as WorldGroundHeight.
static int WorldGroundHeightLocal(int32_t gx, int32_t gy,
                                  int* outTri=nullptr, int* outH0=nullptr, int* outH1=nullptr, int* outH2=nullptr,
                                  double* outA=nullptr, double* outB=nullptr, double* outC=nullptr,
                                  int* outContain=nullptr)
{
    if (!s_blkBuilt) NmBuildBlockIndex();
    if (outTri) *outTri = -1;
    if (outContain) *outContain = 0;
    if (s_nmX0.empty()) return WGH_NO_GROUND;

    const int32_t mx = NmGameToMeshX(gx), my = NmGameToMeshY(gy);
    int homeBc = (int)(mx / NM_BLK); if (homeBc < 0) homeBc = 0; else if (homeBc >= NM_BCOLS) homeBc = NM_BCOLS - 1;
    int homeBr = (int)(my / NM_BLK); if (homeBr < 0) homeBr = 0; else if (homeBr >= NM_BROWS) homeBr = NM_BROWS - 1;
    const int homeBlk = homeBr * NM_BCOLS + homeBc;

    int best = -1; bool bestHome = false; double bestHgt = 0.0; int contain = 0;
    for (int dbr = -1; dbr <= 1; dbr++) {
        int br = homeBr + dbr; if (br < 0) br += NM_BROWS; else if (br >= NM_BROWS) br -= NM_BROWS;
        for (int dbc = -1; dbc <= 1; dbc++) {
            int bc = homeBc + dbc; if (bc < 0) bc += NM_BCOLS; else if (bc >= NM_BCOLS) bc -= NM_BCOLS;
            const int blk = br * NM_BCOLS + bc;
            const bool isHome = (blk == homeBlk);
            for (uint32_t e = s_blkOff[blk]; e < s_blkOff[blk + 1]; e++) {
                const int t = s_blkTri[e];
                const int32_t x0=s_nmX0[t],y0=s_nmY0[t],x1=s_nmX1[t],y1=s_nmY1[t],x2=s_nmX2[t],y2=s_nmY2[t];
                int32_t lo,hi;
                lo = x0<x1?(x0<x2?x0:x2):(x1<x2?x1:x2); hi = x0>x1?(x0>x2?x0:x2):(x1>x2?x1:x2);
                if (mx < lo || mx > hi) continue;
                lo = y0<y1?(y0<y2?y0:y2):(y1<y2?y1:y2); hi = y0>y1?(y0>y2?y0:y2):(y1>y2?y1:y2);
                if (my < lo || my > hi) continue;
                const int64_t d1 = NmCross(mx,my,x0,y0,x1,y1);
                const int64_t d2 = NmCross(mx,my,x1,y1,x2,y2);
                const int64_t d3 = NmCross(mx,my,x2,y2,x0,y0);
                if (((d1<0)||(d2<0)||(d3<0)) && ((d1>0)||(d2>0)||(d3>0))) continue;   // point not inside
                contain++;
                // v0.18.3.162: select the TOPMOST containing surface -- the engine's rule,
                // validated to <=14u vs engineH across 3852 ground-truth samples (mean 0.64u,
                // zero >30u). "up = negative", so topmost = MOST-NEGATIVE barycentric height: the
                // character stands on TOP of overlapping terrain (e.g. an elevated road above
                // lower ground). The old nearest-centroid rule grabbed a lower overlapping poly,
                // producing heights off by up to 420u -> phantom cliffs -> the planner routed the
                // character into walls it then jammed on (Dollet-exit, .157/.160 BATs).
                double dn = (double)(y1 - y2) * (double)(x0 - x2) + (double)(x2 - x1) * (double)(y0 - y2);
                double hT;
                if (dn > -1e-6 && dn < 1e-6) {
                    hT = ((double)s_nmH0[t] + (double)s_nmH1[t] + (double)s_nmH2[t]) / 3.0;
                } else {
                    double aa = ((double)(y1 - y2) * (double)(mx - x2) + (double)(x2 - x1) * (double)(my - y2)) / dn;
                    double bb = ((double)(y2 - y0) * (double)(mx - x2) + (double)(x0 - x2) * (double)(my - y2)) / dn;
                    hT = aa * (double)s_nmH0[t] + bb * (double)s_nmH1[t] + (1.0 - aa - bb) * (double)s_nmH2[t];
                }
                bool better;
                if (best < 0) better = true;
                else if (isHome != bestHome) better = isHome;     // home-block triangle wins
                else better = (hT < bestHgt);                     // else TOPMOST (most-negative height)
                if (better) { best = t; bestHome = isHome; bestHgt = hT; }
            }
        }
    }
    if (outContain) *outContain = contain;
    if (outTri) *outTri = best;
    if (best < 0 || best >= (int)s_nmH0.size()) return WGH_NO_GROUND;

    const int t = best;
    if (outH0) *outH0 = s_nmH0[t];
    if (outH1) *outH1 = s_nmH1[t];
    if (outH2) *outH2 = s_nmH2[t];
    const double dmx = (double)mx, dmy = (double)my;
    const double x0 = (double)s_nmX0[t], y0 = (double)s_nmY0[t];
    const double x1 = (double)s_nmX1[t], y1 = (double)s_nmY1[t];
    const double x2 = (double)s_nmX2[t], y2 = (double)s_nmY2[t];
    const double denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (denom > -1e-6 && denom < 1e-6) {
        if (outA) *outA = 1.0/3.0; if (outB) *outB = 1.0/3.0; if (outC) *outC = 1.0/3.0;
        return ((int)s_nmH0[t] + (int)s_nmH1[t] + (int)s_nmH2[t]) / 3;
    }
    const double a = ((y1 - y2) * (dmx - x2) + (x2 - x1) * (dmy - y2)) / denom;
    const double b = ((y2 - y0) * (dmx - x2) + (x0 - x2) * (dmy - y2)) / denom;
    const double c = 1.0 - a - b;
    if (outA) *outA = a; if (outB) *outB = b; if (outC) *outC = c;
    const double h = a * (double)s_nmH0[t] + b * (double)s_nmH1[t] + c * (double)s_nmH2[t];
    return (int)(h < 0.0 ? h - 0.5 : h + 0.5);
}

// v0.18.3.202: ENGINE FOOT-BLOCK term (offline/BAT201_ANALYSIS.md). The .201 freeze was
// model-fitted against 545 observed walked steps + the frozen state: the engine's on-foot
// validator checks not just the 32u step destination but a probe ~112u AHEAD along the
// heading, and HARD-blocks (no wall slide, d+0) when that probe lands on a non-foot-walkable
// poly (byte15 bit7=0 -- in this mesh equivalent to terrain byte13 in {29 mountain,
// 32-34 ocean} for 99.6% of polys; the terrain byte is what the navmesh stores). This
// helper answers "would the engine's walkability term reject a point": no ground, or the
// selected triangle is a blocked terrain type. NOTE: our triangle selection is
// topmost/home-preferred, not the engine's strict stored-order first-containing -- close
// but not identical on overlap cells; the executor's learned-block overlay (F3) covers the
// residual divergence.
static bool WorldFootBlockedAt(int32_t gx, int32_t gy)
{
    int tri = -1;
    int h = WorldGroundHeightLocal(gx, gy, &tri);
    if (h == WGH_NO_GROUND || tri < 0 || tri >= (int)s_nmTerr.size()) return true;
    const int terr = (int)s_nmTerr[tri];
    return terr == 29 || (terr >= 32 && terr <= 34);
}

// Diagnostic: flood from a reference game coord, log component stats + which
// catalog locations are reachable. Caller passes s_locations (defined later in
// the translation unit, so it can't be referenced from here directly).
static void Navmesh_LogConnectivity(const LocationEntry* locs, int n,
                                    int32_t refGameX, int32_t refGameY, int gate)
{
    if (!s_nmBuilt) { Log::World("WorldMap: [NAVMESH] not built -- skipping connectivity diag"); return; }
    int startTri = Navmesh_FindTriangleGame(refGameX, refGameY);
    // v0.18.3.120: flood/A* are UNGATED on floor-step -- the engine 200-step gate
    // (NmEngineStepBlocked) has already dropped the cliff/ledge CONNECTIONS from
    // the graph, so "REACHABLE" reflects the engine-walkable surface. Only the
    // steep moves are dropped; a triangle's gentle edges remain. #70.
    (void)gate;
    int comp = Navmesh_FloodFrom(startTri, -1);
    Log::World("WorldMap: [NAVMESH] %d tris, %d engine-gate-blocked connections (>=%.0f over ~%.0fu), %d components, largest=%d; flood from (%d,%d) tri#%d -> %d reachable",
               Navmesh_TriangleCount(), s_nmStepBlocked, NM_HEIGHT_STEP, NM_STEP_DIST, s_nmComponents, s_nmLargest,
               refGameX, refGameY, startTri, comp);
    for (int i = 0; i < n; i++) {
        int t = Navmesh_FindTriangleGame(locs[i].x, locs[i].y);
        Log::World("WorldMap: [NAVMESH]   %-26s (%7d,%7d) tri#%d terr=%d %s",
                   locs[i].name, locs[i].x, locs[i].y, t,
                   (t >= 0 ? s_nmTerr[t] : -1),
                   Navmesh_IsReachable(t) ? "REACHABLE" : "not reachable");
    }
    // exercise A* end-to-end: route to Dollet (the #70 acceptance target) if present
    for (int i = 0; i < n; i++) {
        if (strcmp(locs[i].name, "Dollet") == 0) {
            int goal = Navmesh_FindTriangleGame(locs[i].x, locs[i].y);
            std::vector<int> path;
            double L = Navmesh_AStar(startTri, goal, -1, &path);
            Log::World("WorldMap: [NAVMESH] A* ref->Dollet: len=%.0f tris=%d (%s)",
                       L, (int)path.size(), L >= 0.0 ? "path found" : "NO PATH");
            break;
        }
    }
}

#endif  // NAVMESH_DIAG
