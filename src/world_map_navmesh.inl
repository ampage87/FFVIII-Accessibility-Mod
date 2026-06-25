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
// boundary edge stitches them. CAVEAT (Finding 4): the navmesh CANNOT model
// the Dollet false-coast (engine collision, not in the mesh) -- that stays a
// targeted exclusion; see the disassembly track.
//
// Host-compilable: no Win32/SEH/absolute-memory. std::vector/std::sort only
// (world_map.cpp provides <vector>); Log::World is the only external dep.
// Container-validated via tests/test_navmesh.cpp before this port.

#if NAVMESH_DIAG

static const int     NM_CLIMB_STEP = 400;        // offline-calibrated height-step gate
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
static int  s_nmComponents = 0, s_nmLargest = 0;
static bool s_nmBuilt = false;

static void Navmesh_Reset()
{
    s_nmX0.clear(); s_nmY0.clear(); s_nmX1.clear(); s_nmY1.clear();
    s_nmX2.clear(); s_nmY2.clear(); s_nmCX.clear(); s_nmCY.clear();
    s_nmFloor.clear(); s_nmTerr.clear(); s_nmZ0.clear(); s_nmZ1.clear(); s_nmZ2.clear();
    s_nmAdjOff.clear(); s_nmAdjNbr.clear(); s_nmReach.clear();
    s_nmComponents = 0; s_nmLargest = 0; s_nmBuilt = false;
}

static void Navmesh_AddTriangle(int32_t x0,int32_t y0,int16_t z0,
                                int32_t x1,int32_t y1,int16_t z1,
                                int32_t x2,int32_t y2,int16_t z2, uint8_t terrain)
{
    s_nmX0.push_back(x0); s_nmY0.push_back(y0);
    s_nmX1.push_back(x1); s_nmY1.push_back(y1);
    s_nmX2.push_back(x2); s_nmY2.push_back(y2);
    s_nmZ0.push_back(z0); s_nmZ1.push_back(z1); s_nmZ2.push_back(z2);
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
    std::vector<int32_t> vidX, vidY;     // vid -> coords (for bridging)
    int32_t curVid = -1, px = 0, py = 0; int16_t pz = 0; bool first = true;
    for (size_t i = 0; i < vr.size(); i++) {
        const VRec& v = vr[i];
        if (first || v.x != px || v.y != py || v.z != pz) {
            curVid++; vidX.push_back(v.x); vidY.push_back(v.y);
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
    struct BRec { int32_t key, lo, hi; uint32_t tri; uint8_t vert; };
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
                int32_t ax = vidX[va], ay = vidY[va], bx = vidX[vb], by = vidY[vb];
                if (ax == bx)      bnd.push_back({ ax, (ay<by?ay:by), (ay>by?ay:by), er[i].tri, 1 });
                else if (ay == by) bnd.push_back({ ay, (ax<bx?ax:bx), (ax>bx?ax:bx), er[i].tri, 0 });
            }
            i = j;
        }
    }
    std::vector<ERec>().swap(er);

    // ---- 3) T-junction bridging: connect overlapping collinear boundary edges ----
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
                    int32_t lo = (bnd[p].lo > bnd[q].lo ? bnd[p].lo : bnd[q].lo);
                    int32_t hi = (bnd[p].hi < bnd[q].hi ? bnd[p].hi : bnd[q].hi);
                    if (lo < hi) pairs.push_back({ (int32_t)bnd[p].tri, (int32_t)bnd[q].tri });
                }
            }
            k = m;
        }
    }
    std::vector<BRec>().swap(bnd);

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

// A* over the triangle graph, gate on floor-step. Fills outPath (start..goal)
// if non-null. Returns path length in world units, or -1 if no path.
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
            if (gate >= 0) { int32_t st = s_nmFloor[u]-s_nmFloor[v]; if (st<0) st=-st; if (st > gate) continue; }
            double nw = g[u] + NmCentroidDist(u, v);
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

static void Navmesh_ComponentStats(int* numComponents, int* largest)
{ if (numComponents) *numComponents = s_nmComponents; if (largest) *largest = s_nmLargest; }

// Diagnostic: flood from a reference game coord, log component stats + which
// catalog locations are reachable. Caller passes s_locations (defined later in
// the translation unit, so it can't be referenced from here directly).
static void Navmesh_LogConnectivity(const LocationEntry* locs, int n,
                                    int32_t refGameX, int32_t refGameY, int gate)
{
    if (!s_nmBuilt) { Log::World("WorldMap: [NAVMESH] not built -- skipping connectivity diag"); return; }
    int startTri = Navmesh_FindTriangleGame(refGameX, refGameY);
    int comp = Navmesh_FloodFrom(startTri, gate);
    Log::World("WorldMap: [NAVMESH] %d tris, %d components, largest=%d; flood from (%d,%d) tri#%d -> %d reachable (gate=%d)",
               Navmesh_TriangleCount(), s_nmComponents, s_nmLargest,
               refGameX, refGameY, startTri, comp, gate);
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
            double L = Navmesh_AStar(startTri, goal, gate, &path);
            Log::World("WorldMap: [NAVMESH] A* ref->Dollet: len=%.0f tris=%d (%s)",
                       L, (int)path.size(), L >= 0.0 ? "path found" : "NO PATH");
            break;
        }
    }
}

#endif  // NAVMESH_DIAG
