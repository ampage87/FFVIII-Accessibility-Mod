// world_map_planner2.inl - planner part 2 (v0.18.3.225 split)
// Split from world_map_planner.inl (was 96 KB, over the 80 KB CI guard).
// Holds the NAVMESH_DIAG obstacle store (AddNavBlock/IsNavBlockedWorld/
// FootBlockedCached/SweptFootBlocked), the grid A* (PlanPathGrid/GridM),
// PlanDrivePath and ComputePlannerEligibility. Included from world_map.cpp
// immediately AFTER world_map_planner.inl and BEFORE world_map_routenet.inl.

#if NAVMESH_DIAG
// v0.18.3.161: discovered-obstacle store. PlanPathGrid validates edges with
// WorldGroundHeightLocal, which is WRONG at some overlapping-triangle cells (its height
// differs from the engine's -- e.g. the Dollet-exit cliff the .160 BAT jammed on: the planner
// read a smooth height and routed straight across a real |dH|>=200 cliff). When the executor
// JAMS despite the route, it records the spot here and the planner treats it as blocked on the
// re-plan, routing AROUND it -- self-correcting for height-model errors without needing them
// fixed first. Persists across drives (discovered cliffs are real).
// v0.18.3.202: capacity 96 -> 256. The offline S4 robustness run (executor-only recovery
// against a wrong planner gate) accumulated 100+ learned cells on one leg; the overlay is
// the scalability backstop, so give it room.
static int32_t s_navBlkX[256], s_navBlkY[256];
static int     s_navBlkN = 0;
// v0.18.3.204: returns whether a NEW block was recorded (G1 learning steps to a farther
// fallback distance when the near cell is already known).
static bool AddNavBlock(int32_t x, int32_t y) {
    for (int i = 0; i < s_navBlkN; i++) {
        int dx = x - s_navBlkX[i], dy = y - s_navBlkY[i];
        if (dx > -192 && dx < 192 && dy > -192 && dy < 192) return false;   // already near a known block
    }
    if (s_navBlkN < 256) { s_navBlkX[s_navBlkN] = x; s_navBlkY[s_navBlkN] = y; s_navBlkN++; return true; }
    // v0.21.4: **A FULL STORE USED TO GO SILENTLY STERILE.** Returning false here
    // does not merely drop the new block -- it is also the signal the recovery
    // ladder reads as "no new knowledge", which sends it to the fence-inflation
    // branch, which cannot help either because that branch's whole job is to add
    // blocks. So at 256 the executor entered a loop where every recovery was a
    // no-op and every replan was sterile, with nothing logged.
    //
    // Evict the OLDEST. The overlay is a record of where the engine refused to
    // walk, and the oldest entries are the least likely to still matter: they
    // were learned furthest back, most often on a leg the player has since left.
    // A ring keeps the store bounded, keeps recent knowledge, and -- crucially --
    // keeps AddNavBlock returning true so the ladder keeps making progress.
    Log::World("WorldMap: [NAVBLK] overlay full (256) -- evicting the oldest entry "
               "(%d,%d) to make room for (%d,%d)", s_navBlkX[0], s_navBlkY[0], x, y);
    for (int i = 1; i < 256; i++) { s_navBlkX[i-1] = s_navBlkX[i]; s_navBlkY[i-1] = s_navBlkY[i]; }
    s_navBlkX[255] = x; s_navBlkY[255] = y;
    return true;
}
static bool IsNavBlockedWorld(int32_t x, int32_t y) {
    for (int i = 0; i < s_navBlkN; i++) {
        int dx = x - s_navBlkX[i], dy = y - s_navBlkY[i];
        if (dx > -160 && dx < 160 && dy > -160 && dy < 160) return true;
    }
    return false;
}

// v0.18.3.202: cached engine foot-block lookup, 32u-quantized, direct-mapped. The fitted
// clearance probe (WorldFootBlockedAt at every sub-march point + 112u ahead) roughly
// doubles per-edge mesh queries; the mesh is static, so a persistent point cache makes the
// repeat lookups (probes overlap heavily between neighbouring edges) near-free. 2^18
// entries x 5 bytes ~= 1.3 MB, allocated on first plan (v0.18.3.205: back to 32u).
// v0.18.3.205: BACK to 32u quantization for the A* hot path. The .204 8u quantization was
// the game-freeze: it cut the cache hit rate ~16x (every nearby probe became a fresh mesh
// query), the first Dollet plan ran 45 SECONDS and failed, and the 384-margin retry then
// occupied the game thread for what looked like a hang (music on, input dead). 8u fidelity
// survives ONLY in FootBlocked8/SweptFootBlocked below, used by the cheap per-route lazy
// validation and the executor's per-frame checks -- not by the A* inner loop.
static bool FootBlockedCached(int32_t gx, int32_t gy)
{
    static std::vector<uint32_t> ck;
    static std::vector<uint8_t>  cv;
    const uint32_t SZ = 1u << 18;
    if (ck.empty()) { ck.assign(SZ, 0xFFFFFFFFu); cv.assign(SZ, 0); }
    // Torus-wrap BOTH axes properly before quantizing (X's period 262144 happens to equal
    // the 0x3FFFF mask+1, Y's 196608 does NOT -- a 112u probe overshooting the south seam
    // would otherwise cache a point 65536u away; review finding, 2026-07-02).
    const int32_t wgx = (int32_t)(((((long long)gx + 131072) % 262144) + 262144) % 262144);
    const int32_t wgy = (int32_t)(((((long long)gy +  98304) % 196608) + 196608) % 196608);
    const uint32_t qx = ((uint32_t)wgx) >> 5;   // 32u cells, 13 bits
    const uint32_t qy = ((uint32_t)wgy) >> 5;   // < 6144, 13 bits
    const uint32_t key = (qx << 13) | qy;
    const uint32_t idx = (key * 2654435761u) & (SZ - 1);
    if (ck[idx] == key) return cv[idx] != 0;
    const bool b = WorldFootBlockedAt((int32_t)(qx << 5) - 131072 + 16,
                                      (int32_t)(qy << 5) -  98304 + 16);
    ck[idx] = key; cv[idx] = b ? 1 : 0;
    return b;
}

// v0.18.3.205: 8u-quantized variant -- catches the ~1u sliver walls (BAT203 sect. 6 item 1).
// Used ONLY by SweptFootBlocked (route validation: ~16K samples once per plan; executor
// wall-follow checks: 14 samples per frame) where the query volume is small.
static bool FootBlocked8(int32_t gx, int32_t gy)
{
    static std::vector<uint32_t> ck;
    static std::vector<uint8_t>  cv;
    const uint32_t SZ = 1u << 20;
    if (ck.empty()) { ck.assign(SZ, 0xFFFFFFFFu); cv.assign(SZ, 0); }
    const int32_t wgx = (int32_t)(((((long long)gx + 131072) % 262144) + 262144) % 262144);
    const int32_t wgy = (int32_t)(((((long long)gy +  98304) % 196608) + 196608) % 196608);
    const uint32_t qx = ((uint32_t)wgx) >> 3;   // 8u cells, 15 bits
    const uint32_t qy = ((uint32_t)wgy) >> 3;   // < 24576, 15 bits
    const uint32_t key = (qx << 15) | qy;
    const uint32_t idx = (key * 2654435761u) & (SZ - 1);
    if (ck[idx] == key) return cv[idx] != 0;
    const bool b = WorldFootBlockedAt((int32_t)(qx << 3) - 131072 + 4,
                                      (int32_t)(qy << 3) -  98304 + 4);
    ck[idx] = key; cv[idx] = b ? 1 : 0;
    return b;
}

// v0.18.3.204: SWEPT foot-block probe (offline/BAT203_ANALYSIS.md sect. 6 item 1). The .203
// BAT falsified the single 112u POINT probe: a ~1u sliver wall and 5 more logged blocks sit
// between sample points. Sweep every 8u along [minD,maxD] on the bearing instead -- offline
// this has the SAME false-rejection rate as the point probe across all 1,217 walked steps
// while catching the slivers. (~half the .203 rejections remain statically unpredictable --
// the engine's find-poly MRU cache is STATEFUL -- which is what the G1 engine-truth learning
// in the executor is for; this probe just gets the static part right.)
static bool SweptFootBlocked(int32_t fromX, int32_t fromY, int bearingAu,
                             int minD = 8, int maxD = 112)
{
    const double th = (double)(bearingAu & 0xFFF) / 4096.0 * 6.283185307179586;
    const double sx = sin(th), cy = -cos(th);
    for (int d = minD; d <= maxD; d += 8) {
        if (FootBlocked8(fromX + (int32_t)(sx * d), fromY + (int32_t)(cy * d)))   // v0.18.3.205: 8u variant
            return true;
    }
    return false;
}

// v0.18.3.202: MARGIN LADDER wrapper. The correct G-Garden->Dollet horseshoe needs ~20k
// units of bbox padding (the .201 MARGIN=176 cells = 22.5k was coincidentally enough for
// the wrong route but the fitted-gate route needs headroom); on failure retry once with a
// 384-cell (~49k) margin before falling through to the legacy planners.
static bool PlanPathGridM(int32_t startX, int32_t startY, int marginCells);

// v0.18.3.204: recovery replans start WIDE (24576u = 192 cells) so learned-block detours
// have room immediately (BAT203_ANALYSIS sect. 6 item 4). Set by the executor's recovery
// before PlanDrivePath; consumed (cleared) by the next PlanPathGrid call.
static bool s_planWideFirst = false;

// v0.18.3.205: HARD WALL-CLOCK BUDGET for planning. Planning runs on the GAME thread; the
// .204 BAT froze the game because a failing 45s plan escalated into a multi-minute
// 384-margin retry. The wrapper sets a deadline; the A* loop checks it every 2048
// expansions and bails; the wrapper stops escalating past it and accepts an imperfect
// (validation-dirty) plan rather than re-planning. On total failure the caller falls
// through to the legacy navmesh/road planners (fast), so the drive still functions.
static DWORD s_planDeadline = 0;

// v0.18.3.204: SOFT swept-fail edge set (lazy route validation; sect. 6 items 1+6). Instead
// of paying the swept probe on every A* edge (plan time is already seconds live), the final
// route is swept-validated; failing edges land here with a x6 cost and the plan re-runs on
// warm caches. World-cell keyed so entries survive across bbox changes and replans.
static const int SWEPT_FAIL_MAX = 96;
static uint64_t s_sweptFail[SWEPT_FAIL_MAX];
static int      s_sweptFailN = 0;
static uint64_t SweptEdgeKey(int cx, int cy, int k) { return ((uint64_t)(cx * 1536 + cy) << 3) | (uint64_t)k; }
static bool IsSweptFail(int cx, int cy, int k) {
    const uint64_t key = SweptEdgeKey(cx, cy, k);
    for (int i = 0; i < s_sweptFailN; i++) if (s_sweptFail[i] == key) return true;
    return false;
}

static bool PlanPathGrid(int32_t startX, int32_t startY)
{
    const bool wide = s_planWideFirst; s_planWideFirst = false;
    // v0.18.3.204: PRUNE VALVE (sect. 6 item 2; offline a 9-cell learned fence at the Timber
    // pass turned G-Garden into a graph island). If planning fails outright, drop the
    // learned block nearest the start whose own center the static gate considers walkable
    // (i.e. a cell learned from the engine's STATEFUL misbehavior, not a real wall) and
    // retry -- fences must never make the goal unplannable.
    int valPass = 0;   // validation re-plans used THIS call (review finding: was a leaky static)
    s_planDeadline = GetTickCount() + 10000;  // v0.18.3.205: total budget for this whole call
                                              // (~.203's observed single-plan time + headroom)
    for (int attempt = 0; attempt < 5; attempt++) {
        bool ok = wide ? (PlanPathGridM(startX, startY, 192) || PlanPathGridM(startX, startY, 384))
                       : (PlanPathGridM(startX, startY, 176) || PlanPathGridM(startX, startY, 384));
        if (ok) {
            // v0.18.3.204: LAZY SWEPT VALIDATION of the returned route. Sweep each leg (8u
            // pitch, through the leg + 112u overshoot); a failing leg is recorded as a soft
            // x6 edge and the plan re-runs once on warm caches. Two validation passes max;
            // a route that still carries failing legs is accepted (the executor's G1/G2
            // recovery owns whatever the static model can't see).
            bool dirty = false;
            for (int i = 0; i + 1 < s_drivePathLen && s_drivePathWorld; i++) {
                int32_t ax = s_drivePathWX[i],     ay = s_drivePathWY[i];
                int32_t bx = s_drivePathWX[i + 1], by = s_drivePathWY[i + 1];
                int32_t dx = bx - ax, dy = by - ay;
                int b = TorusBearing(ax, ay, bx, by);
                int len = (dx && dy) ? 181 : 128;
                if (SweptFootBlocked(ax, ay, b, 8, len + 112)) {
                    int acx = (int)(((((long long)ax + 131072) % 262144) + 262144) % 262144) / 128;
                    int acy = (int)(((((long long)ay +  98304) % 196608) + 196608) % 196608) / 128;
                    int k = (dx > 0) ? (dy > 0 ? 4 : (dy < 0 ? 5 : 0)) :
                            (dx < 0) ? (dy > 0 ? 6 : (dy < 0 ? 7 : 1)) :
                                       (dy > 0 ? 2 : 3);
                    const uint64_t key = SweptEdgeKey(acx, acy, k);
                    bool known = false;
                    for (int e = 0; e < s_sweptFailN; e++) if (s_sweptFail[e] == key) { known = true; break; }
                    if (!known && s_sweptFailN < SWEPT_FAIL_MAX) { s_sweptFail[s_sweptFailN++] = key; dirty = true; }
                }
            }
            if (dirty && valPass < 2 && GetTickCount() < s_planDeadline) {
                valPass++;
                Log::World("WorldMap: [PLAN] swept validation flagged edges (%d total) -- re-planning around them", s_sweptFailN);
                continue;   // re-plan with the soft penalties in place
            }
            if (dirty)
                Log::World("WorldMap: [PLAN] accepting plan with %d swept-flagged edge(s) (budget/passes spent) -- executor recovery owns them", s_sweptFailN);
            return true;
        }
        // v0.18.3.205: out of budget -> stop escalating; the caller's legacy planners take over.
        if (GetTickCount() >= s_planDeadline) {
            Log::World("WorldMap: [PLAN] budget exhausted (10s) without a grid route -- falling through");
            return false;
        }
        // plan failed: try the prune valve
        if (s_navBlkN <= 0) return false;
        int pick = -1; double pd = 1e30;
        for (int i = 0; i < s_navBlkN; i++) {
            if (FootBlockedCached(s_navBlkX[i], s_navBlkY[i])) continue;   // genuinely blocked terrain: keep
            double d = CalculateWrappedDistance(startX, startY, s_navBlkX[i], s_navBlkY[i]);
            if (d < pd) { pd = d; pick = i; }
        }
        if (pick < 0) return false;
        Log::World("WorldMap: [NAVBLK-PRUNE] plan failed -- releasing learned block (%d,%d) (%d left) and retrying",
                   s_navBlkX[pick], s_navBlkY[pick], s_navBlkN - 1);
        s_navBlkX[pick] = s_navBlkX[s_navBlkN - 1];
        s_navBlkY[pick] = s_navBlkY[s_navBlkN - 1];
        s_navBlkN--;
    }
    return false;
}

static bool PlanPathGridM(int32_t startX, int32_t startY, int marginCells)
{
    const int STEP = 128;
    const int GCOLS = 262144 / STEP;   // 2048
    const int GROWS = 196608 / STEP;   // 1536
    const int MARGIN = marginCells;    // bbox padding around start+goal (cells; ladder above)
    const int GATE = 200;              // engine |dH| step gate (0xC8) -- now clearance/snap heuristics only
    // v0.18.3.187: EDGE_GATE -- the HARD per-32u walkability limit for A* edge traversal. The .186
    // [MFRAME] capture measured the engine's REAL ceiling: across 1927 logged on-foot moves it NEVER
    // took a step steeper than ~23 height per 32u (a hard wall in the distribution; it was BLOCKED at
    // 65/32u trying to leave Dollet). The RE'd 0xC8=200 must be in finer engine height units, or over
    // a larger step than this 32u sub-march, because at the 32u granularity the planner actually
    // marches, the true limit is ~24. With GATE=200 the planner routed Dollet->Timber straight down a
    // 65-90/32u canyon wall the engine refuses to descend (the character pinned on the rim); at 24
    // that descent is correctly blocked, so A* must route around it on the gentle high ground that the
    // ->Dollet leg already proved walkable (it never touched the canyon). Clearance/snap keep the
    // looser 200 so normal slopes aren't over-penalized.
    // v0.18.3.188: REVERTED to 200. The .187 EDGE_GATE=24 regressed ->Dollet -- it rerouted the drive
    // through (-21984,-26304), flat open ground where the engine still refuses to move the character
    // north (held UP 150ms+, frozen, oracle says all 8 dirs walkable <24). That is a walkmesh-REGION
    // boundary the height model cannot see, not a slope -- so a gate value can't fix it and 24 only
    // exposed it on a leg that previously worked. Back to 200 to restore ->Dollet while the real gap
    // (engine walkmesh topology vs our height-only oracle) is tackled separately.
    const int EDGE_GATE = 200;
    const int HC_NONE = 0x40000000;    // ground-cache "not computed" sentinel (no real height)
    const int G_INF   = 0x3FFFFFFF;

    auto toCellX = [&](int32_t gx){ int n=(int)(((((long long)gx+131072)%262144)+262144)%262144); return n/STEP; };
    auto toCellY = [&](int32_t gy){ int n=(int)(((((long long)gy+98304 )%196608)+196608)%196608); return n/STEP; };
    int sCx=toCellX(startX),          sCy=toCellY(startY);
    int gCx=toCellX(s_driveTargetX), gCy=toCellY(s_driveTargetY);

    int bx0=(sCx<gCx?sCx:gCx)-MARGIN; if(bx0<0)bx0=0;
    int bx1=(sCx>gCx?sCx:gCx)+MARGIN; if(bx1>GCOLS-1)bx1=GCOLS-1;
    int by0=(sCy<gCy?sCy:gCy)-MARGIN; if(by0<0)by0=0;
    int by1=(sCy>gCy?sCy:gCy)+MARGIN; if(by1>GROWS-1)by1=GROWS-1;
    const int BW=bx1-bx0+1, BH=by1-by0+1;
    if(BW<=0||BH<=0||(long long)BW*BH>4000000LL) return false;
    const int N=BW*BH;
    auto LI=[&](int cx,int cy){ return (cx-bx0)*BH+(cy-by0); };
    auto inB=[&](int cx,int cy){ return cx>=bx0&&cx<=bx1&&cy>=by0&&cy<=by1; };

    std::vector<int> hc(N, HC_NONE);
    // v0.18.3.205: per-cell NEAR-WALL memo. The .204 build evaluated the 8-sample 96u wall
    // ring per EDGE ARRIVAL (x8 per cell) -- a large part of the 45s plan. It's a per-cell
    // property; compute it once.
    std::vector<int8_t> nwc(N, -1);
    auto groundH=[&](int cx,int cy)->int{
        int li=LI(cx,cy); int v=hc[li];
        if(v!=HC_NONE) return v;
        int32_t gx=cx*STEP+STEP/2-131072, gy=cy*STEP+STEP/2-98304;
        int h=WorldGroundHeightLocal(gx,gy);
        hc[li]=h; return h;
    };
    auto walkable=[&](int cx,int cy){ return groundH(cx,cy)!=WGH_NO_GROUND; };
    auto snap=[&](int& cx,int& cy)->bool{
        if(inB(cx,cy)&&walkable(cx,cy)) return true;
        for(int r=1;r<=16;r++)
            for(int dx=-r;dx<=r;dx++) for(int dy=-r;dy<=r;dy++){
                if(dx>-r&&dx<r&&dy>-r&&dy<r) continue;
                int nx=cx+dx,ny=cy+dy;
                if(inB(nx,ny)&&walkable(nx,ny)){cx=nx;cy=ny;return true;}
            }
        return false;
    };
    // v0.18.3.169: SURFACE-AWARE start snap. The character's 128u grid cell CENTER can land on a
    // DIFFERENT surface than the character actually stands on: when the character is within ~64u of
    // a cliff edge, the cell center falls ACROSS the cliff. The .168 jam was exactly this -- the
    // character stood on the lower road at h=-547, but its cell center was at h=-143 on the upper
    // mountain (terrain 29), so the planner planned along the UPPER surface and the route's first
    // step was up a 400u cliff the character can't climb (offline-reproduced: faithful planner
    // routed east-north into the cliff; surface-aware snap routes south along the lower road and the
    // follower arrives). Snap the start to the nearest cell whose center height is within the step
    // gate of the character's ACTUAL ground height, so the route stays on the character's surface.
    int h0 = WorldGroundHeightLocal(startX, startY);
    auto snapStart=[&](int& cx,int& cy)->bool{
        if(h0==WGH_NO_GROUND) return false;
        for(int r=0;r<=16;r++)
            for(int dx=-r;dx<=r;dx++) for(int dy=-r;dy<=r;dy++){
                if(r>0 && dx>-r&&dx<r&&dy>-r&&dy<r) continue;
                int nx=cx+dx,ny=cy+dy;
                if(!inB(nx,ny)) continue;
                int gh2=groundH(nx,ny);
                if(gh2==WGH_NO_GROUND) continue;
                int dd=gh2-h0; if(dd<0)dd=-dd;
                if(dd<GATE){ cx=nx; cy=ny; return true; }
            }
        return false;
    };
    if(!snapStart(sCx,sCy) && !snap(sCx,sCy)) return false;   // surface-aware first; fall back to any walkable
    if(!snap(gCx,gCy)) return false;
    if(sCx==gCx&&sCy==gCy) return false;   // already there; let the caller's shortcut handle it

    std::vector<int> g(N, G_INF), came(N, -1);
    std::vector<long long> heap;
    auto hpush=[&](long long v){ heap.push_back(v); int i=(int)heap.size()-1;
        while(i>0){ int p=(i-1)/2; if(heap[p]<=heap[i])break; long long t=heap[p];heap[p]=heap[i];heap[i]=t; i=p; } };
    auto hpop=[&]()->long long{ long long r=heap[0]; long long last=heap.back(); heap.pop_back();
        if(!heap.empty()){ heap[0]=last; int i=0,n=(int)heap.size();
            for(;;){ int l=2*i+1,rr=2*i+2,s=i; if(l<n&&heap[l]<heap[s])s=l; if(rr<n&&heap[rr]<heap[s])s=rr; if(s==i)break; long long t=heap[s];heap[s]=heap[i];heap[i]=t; i=s; } }
        return r; };
    auto Hh=[&](int cx,int cy){ int dx=cx-gCx; if(dx<0)dx=-dx; int dy=cy-gCy; if(dy<0)dy=-dy; return (dx+dy)*STEP; };

    // v0.18.3.203: learned field-trigger avoidance (see s_trigAvoid* in world_map_state.inl).
    // Soft-penalize cells inside a known NON-TARGET trigger circle so routes go around when
    // terrain allows; still crossable when it's the only way (the paused-drive resume handles
    // the field visit). Exemptions: a circle containing the START (the engine disarms a
    // trigger you spawn inside), and a circle at/hugging the DESTINATION (entering it is the
    // point). Filtered once per plan.
    int     avN = 0;
    int32_t avX[TRIG_AVOID_MAX], avY[TRIG_AVOID_MAX];
    int     avR[TRIG_AVOID_MAX];
    for (int i = 0; i < s_trigAvoidN; i++) {
        if (s_trigAvoidR[i] <= 0) continue;
        if (CalculateWrappedDistance(startX, startY, s_trigAvoidX[i], s_trigAvoidY[i])
                <= (double)s_trigAvoidR[i]) continue;                        // start inside: disarmed
        if (CalculateWrappedDistance(s_driveTargetX, s_driveTargetY, s_trigAvoidX[i], s_trigAvoidY[i])
                <= (double)s_trigAvoidR[i] + 1500.0) continue;               // it IS the destination
        avX[avN] = s_trigAvoidX[i]; avY[avN] = s_trigAvoidY[i]; avR[avN] = s_trigAvoidR[i];
        avN++;
    }
    if (avN > 0)
        Log::World("WorldMap: [TRIGAVOID] plan avoids %d learned trigger circle(s)", avN);

    // v0.18.3.207: DECODED-AREA avoidance. The .206 BAT proved the learned circles are the
    // wrong shape for big trigger regions: G-Garden's field fired 2990u from the circle
    // center, and the "start inside = exempt" rule let the resumed route cross the region
    // again -- four field entries in 40s. The decoded firing-area bboxes (s_entryAims) ARE
    // the real geometry, so: any route cell inside a NON-TARGET decoded area costs +4096
    // (~32 cells) -- steep enough that A* leaves by the shortest path and never re-enters,
    // with NO start-inside exemption (the trigger re-arms the moment you step off the entry
    // polys, so "crossing freely" was never safe). The destination's own area is exempt.
    int  eaAvoid[ENTRY_AIM_COUNT];
    int  eaN = 0;
    {
        const int tgtIdx = FindEntryAim(s_driveTargetName);
        for (int i = 0; i < ENTRY_AIM_COUNT; i++) {
            if (i == tgtIdx) continue;
            eaAvoid[eaN++] = i;
        }
    }

    int sLi=LI(sCx,sCy), gLi=LI(gCx,gCy);
    g[sLi]=0;
    hpush(((long long)Hh(sCx,sCy)<<24)|sLi);
    const int DX[8]={1,-1,0,0,1,1,-1,-1}, DY[8]={0,0,1,-1,1,-1,1,-1};
    bool found=false; int expanded=0;
    // v0.18.3.202: GOAL RELAXATION. Some destinations sit flush against non-walkable terrain
    // (Fire Cavern on its mountain face); under the fitted clearance gate their exact cell can
    // be unreachable even though the character can get close enough for the 400u arrival
    // radius / entrance sweep. Track the expanded cell nearest the goal; if A* exhausts
    // without reaching it, accept that cell when it is within ~8 cells (1km) of the goal.
    int bestLi=sLi, bestH=Hh(sCx,sCy);
    while(!heap.empty()){
        long long top=hpop();
        int cLi=(int)(top&0xFFFFFF);
        int f=(int)(top>>24);
        int cx=bx0+cLi/BH, cy=by0+cLi%BH;
        if(f - Hh(cx,cy) > g[cLi]) continue;   // stale entry
        { int hh=Hh(cx,cy); if(hh<bestH){ bestH=hh; bestLi=cLi; } }
        if(cLi==gLi){ found=true; break; }
        if(++expanded > N) break;
        // v0.18.3.205: wall-clock bail -- planning must NEVER hang the game thread (the
        // .204 freeze). Checked every 2048 expansions; goal relaxation below may still
        // salvage a partial route toward the nearest reached cell.
        if((expanded & 2047)==0 && s_planDeadline!=0 && GetTickCount()>s_planDeadline){
            Log::World("WorldMap: [PLAN] A* wall-clock bail at %d expansions (margin %d)", expanded, MARGIN);
            break;
        }
        int ch=groundH(cx,cy);
        for(int k=0;k<8;k++){
            int nx=cx+DX[k], ny=cy+DY[k];
            if(!inB(nx,ny)) continue;
            // v0.18.3.147: FAITHFUL collision edge -- the engine moves 32u/frame on foot and
            // refuses a step iff |dH|>=200 between consecutive 32u landings (validator 0x53E7A0).
            // Sub-march the 128u edge at 32u and reject any >=200 jump or no-ground, so the
            // route is proactively collision-free at the engine's own granularity (coarse
            // cell-center-only checks step over thin cliffs the character then wedges on).
            int32_t agx=cx*STEP+STEP/2-131072, agy=cy*STEP+STEP/2-98304;
            int32_t bgx=nx*STEP+STEP/2-131072, bgy=ny*STEP+STEP/2-98304;
            int seglen=(DX[k]&&DY[k])?181:128; int nsub=(seglen+31)/32;
            // v0.18.3.202: FITTED ENGINE GATE (offline/BAT201_ANALYSIS.md sect. 2). The engine's
            // on-foot validator, model-fitted against the .201 BAT's 545 walked steps + the
            // frozen state, is: dest(+32u) must have ground, be foot-walkable terrain, and
            // |dH|<200 -- AND a probe ~112u ahead along the movement direction must also land
            // on walkable terrain (probe failure is a HARD block, no slide). The .201 route
            // passed plain walkability at every 32u but lacked CLEARANCE: edge idx 85 probed
            // into a mountain wall. Edges are DIRECTED (the gate is anisotropic), which A*'s
            // per-direction expansion already gives us. Probe offset computed per edge.
            // v0.18.3.204: the hard 112u point-probe is GONE (the .203 BAT falsified it --
            // the engine's gate is partly STATEFUL, see BAT203_ANALYSIS sect. 2). Static
            // clearance is now handled by lazy swept validation of the final route (soft x6
            // edges, wrapper above) + the x4 near-wall cost below; live residuals are
            // learned by the executor (G1) into s_navBlk*, which stays a hard reject here.
            int ph=ch; bool blocked=false;
            for(int t=1;t<=nsub;t++){
                int32_t sx=agx+(int32_t)((long long)(bgx-agx)*t/nsub);
                int32_t sy=agy+(int32_t)((long long)(bgy-agy)*t/nsub);
                int sh=WorldGroundHeightLocal(sx,sy);
                if(sh==WGH_NO_GROUND){ blocked=true; break; }
                if(IsNavBlockedWorld(sx,sy)){ blocked=true; break; }   // v0.18.3.161/.204: learned engine blocks
                if(FootBlockedCached(sx,sy)){ blocked=true; break; }             // v0.18.3.202: dest walkability
                int dd=sh-ph; if(dd<0)dd=-dd;
                if(dd>=EDGE_GATE){ blocked=true; break; }   // v0.18.3.187: real engine ceiling (~24/32u), not 200
                ph=sh;
            }
            if(blocked) continue;
            int nLi=LI(nx,ny);
            // ROAD PREFERENCE: the road overlay (s_roadFine, terrain 27/28/12) marks the gentle,
            // reliably-walkable corridors. Discount their cost so A* follows roads instead of
            // grazing marginal cross-country terrain near the 200 gate (the .143 road planner
            // reached Dollet/Timber this way).
            int frr=WorldYToFineRow(bgy), fcc=WorldXToFineCol(bgx);
            bool isRoad=(frr>=0&&frr<WM_FINE_ROWS&&fcc>=0&&fcc<WM_FINE_COLS&&s_roadFine[frr][fcc]);
            int base=(DX[k]&&DY[k])?181:128;
            // v0.18.3.167: CLEARANCE bias -- penalize cells that hug a wall/cliff so the route runs
            // down the OPEN MIDDLE of corridors, giving the 8-way on-foot executor room to thread
            // canyons. The shortest path hugged the canyon wall and the character drifted into it
            // (the Timber<->Dollet jam). Count n's 8 neighbours that are off-mesh or across a >=200
            // cliff; each adds a cost nudge, so A* prefers cells with more open space around them.
            int hN = groundH(nx, ny), wallnbr = 0;
            for (int kk = 0; kk < 8; kk++) {
                int wnx = nx + DX[kk], wny = ny + DY[kk];
                if (!inB(wnx, wny)) { wallnbr++; continue; }
                int wh = groundH(wnx, wny);
                if (wh == WGH_NO_GROUND) { wallnbr++; continue; }
                if (hN != WGH_NO_GROUND) { int dd2 = wh - hN; if (dd2 < 0) dd2 = -dd2; if (dd2 >= GATE) wallnbr++; }
            }
            // v0.18.3.203: learned trigger-circle penalty (soft; see the avoid set above).
            int trigPen = 0;
            for (int ti = 0; ti < avN; ti++) {
                if (CalculateWrappedDistance(bgx, bgy, avX[ti], avY[ti]) <= (double)avR[ti]) {
                    trigPen = 1536;   // ~12 cells' cost per crossed cell: strong detour bias
                    break;
                }
            }
            // v0.18.3.207: decoded firing-area penalty (see eaAvoid above) -- dominates the
            // circle penalty inside the exact region geometry.
            for (int ti = 0; ti < eaN; ti++) {
                const EntryAimInfo& ez = s_entryAims[eaAvoid[ti]];
                if (bgx >= ez.x0 && bgx <= ez.x1 && bgy >= ez.y0 && bgy <= ez.y1) {
                    trigPen = 4096;
                    break;
                }
            }
            // v0.18.3.204: x4 NEAR-WALL cost (BAT203 sect. 6 item 6) -- terrain walls within
            // ~96u of the destination cell center. The .203 grind happened because the route
            // line ran 36-88u from walls, inside the engine's sticky rejection zone; pricing
            // wall-adjacent cells x4 pulls A* to corridor centerlines. 8 cached samples.
            // v0.18.3.205: memoized per CELL (nwc) instead of per edge arrival (x8 waste).
            bool nearWall;
            {
                int8_t& mw = nwc[LI(nx,ny)];
                if (mw < 0) {
                    static const int WOFF[8][2] = { {96,0},{-96,0},{0,96},{0,-96},{68,68},{68,-68},{-68,68},{-68,-68} };
                    mw = 0;
                    for (int wk = 0; wk < 8; wk++) {
                        if (FootBlockedCached(bgx + WOFF[wk][0], bgy + WOFF[wk][1])) { mw = 1; break; }
                    }
                }
                nearWall = (mw != 0);
            }
            // v0.18.3.204: x6 soft cost for edges the lazy swept validation flagged.
            int cost = isRoad ? base/3 : base;
            if (nearWall) cost *= 4;
            if (IsSweptFail(cx, cy, k)) cost *= 6;
            int ng=g[cLi]+cost+wallnbr*56+trigPen;
            if(ng<g[nLi]){ g[nLi]=ng; came[nLi]=cLi; hpush(((long long)(ng+Hh(nx,ny))<<24)|nLi); }
        }
    }
    if(!found){
        // v0.18.3.202: goal relaxation -- path to the nearest reached cell within ~8 cells
        // of the goal (see comment above the search loop).
        if(bestLi!=sLi && bestH<=8*STEP && g[bestLi]<G_INF){
            gLi=bestLi;
            Log::World("WorldMap: [PLAN] goal unreachable -- relaxed to nearest reached cell (%d units short)", bestH);
        } else {
            return false;
        }
    }
    static int revBuf[4096];
    int rc=0, cur=gLi;
    while(cur!=-1 && rc<4096){ revBuf[rc++]=cur; if(cur==sLi)break; cur=came[cur]; }
    if(rc==0 || revBuf[rc-1]!=sLi) return false;
    // v0.21.4: **DECIMATE, DO NOT TRUNCATE.** The old emission was
    // `for (i = rc-1; i >= 0 && n < DRIVE_PATH_MAX; i--)`, which on a route longer
    // than DRIVE_PATH_MAX waypoints simply STOPPED COPYING partway along and then
    // set s_drivePathPlanned = true. The executor followed a route that ended in
    // open country, with no log line saying so. The 384-cell margin ladder exists
    // to allow ~49 km horseshoe detours, which is exactly the case that overflows.
    //
    // PlanPathFine has always stride-sampled for this reason
    // (world_map_planner.inl); the live grid planner never did. Stride here the
    // same way, and always keep the LAST cell so the route still ends at the goal.
    // The +1 reserves a slot for the goal cell, which is emitted unconditionally
    // below. Without it, rc=1536 picks stride 2, every stride-aligned index is
    // odd, and the goal (index 0) is never reached -- a route that ends 128 units
    // short, which is precisely the class of bug this change exists to remove.
    // tests/pathdecimate_test.cpp caught that on the first run.
    int stride = 1;
    for (;;) {
        // stride 1 emits every cell; any coarser stride also emits the goal, so
        // it costs one extra slot.
        const int emit = (stride == 1) ? rc : ((rc + stride - 1) / stride) + 1;
        if (emit <= DRIVE_PATH_MAX) break;
        stride++;
    }
    if (stride > 1) {
        Log::World("WorldMap: [PLAN] route is %d cells for a %d-waypoint buffer -- "
                   "decimating by %d (the route is kept whole; resolution drops to %d units)",
                   rc, DRIVE_PATH_MAX, stride, stride * STEP);
    }
    int n=0;
    for(int i=rc-1;i>=0 && n<DRIVE_PATH_MAX;i--){
        // Keep every `stride`-th cell counting from the START, and always the goal.
        const int fromStart = (rc - 1) - i;
        if (stride > 1 && i != 0 && (fromStart % stride) != 0) continue;
        int li=revBuf[i]; int cx=bx0+li/BH, cy=by0+li%BH;
        int32_t gx=cx*STEP+STEP/2-131072, gy=cy*STEP+STEP/2-98304;
        // v0.18.3.163: keep FULL 128u resolution in the world-coord path so the executor follows
        // the validated polyline through canyons (no 1024u corner-cut across detoured cliffs).
        s_drivePathWX[n]=gx; s_drivePathWY[n]=gy;
        int fc=WorldXToFineCol(gx), fr=WorldYToFineRow(gy);
        if(fr<0)fr=0; else if(fr>=WM_FINE_ROWS)fr=WM_FINE_ROWS-1;
        if(fc<0)fc=0; else if(fc>=WM_FINE_COLS)fc=WM_FINE_COLS-1;
        s_drivePath[n]=PackSeg(fr,fc);
        n++;
    }
    if(n<2) return false;
    s_drivePathLen=n; s_drivePathIdx=0; s_drivePathPlanned=true; s_drivePathWorld=true;
    s_drivePlanExpanded=9999; s_drivePlanDist=0; s_driveNavmeshPath=false;
    Log::World("WorldMap: [PLAN] GRID planner ok: cell(%d,%d)->cell(%d,%d) bbox %dx%d expanded=%d -> %d fine cells",
               sCx,sCy,gCx,gCy,BW,BH,expanded,n);
    return true;
}
#endif

// v0.18.3.209 (#70): the precomputed validated ROUTE NETWORK planner (defined in
// world_map_routenet.inl, included after this file; same TU, so the forward
// declaration resolves at link of the translation unit).
static bool RouteNetPlan(int32_t startX, int32_t startY);

static bool PlanDrivePath(int32_t startX, int32_t startY)
{
    s_drivePathWorld = false;   // v0.18.3.163: only PlanPathGrid fills the 128u world path; others use fine cells
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;
    s_driveNavmeshPath  = false;   // #70 v0.18.3.109: cleared here; set true only on a navmesh-funnel success below

#if NAVMESH_DIAG
    // v0.18.3.209 (#70): the precomputed VALIDATED route network is tried FIRST --
    // exact offline-validated polylines (e.g. the z=-25086 line through the Galbadia
    // pass that no 128u grid row can express). Declines cleanly (off-network, no
    // path, hop failure, 4-plans-per-drive cap) -> grid A* runs exactly as before.
    if (RouteNetPlan(startX, startY)) { s_driveBridgeActive = false; return true; }
    // v0.18.3.147: faithful collision-aware + road-preferring grid planner is PRIMARY again
    // (the .145 version routed cross-country into marginal terrain; this one sub-marches edges
    // at the engine's 32u step and prefers roads). Falls through to the navmesh/fine planners.
    if (PlanPathGrid(startX, startY)) { s_driveBridgeActive = false; return true; }
#endif
    // (.145/.146 heritage note: an early grid planner was disabled here pre-.147; see
    // CHANGELOG v0.18.3.146 for the story. Dead block removed in .209 for file size.)

    if (!s_segmentRegionLoaded) {
        Log::World("WorldMap: [PLAN] Region map not loaded \u2014 fallback to catalog-center steering");
        return false;
    }

    VehicleType veh   = (s_lastVehicle < 0) ? VEH_ON_FOOT
                                            : GetVehicleType((uint8_t)s_lastVehicle);
    uint16_t    story = GetCurrentStoryFlag();

    if (veh == VEH_RAGNAROK) {
        Log::World("WorldMap: [PLAN] Ragnarok mode \u2014 skipping planner (catalog-center steering)");
        return false;
    }

    uint8_t region = 0;
    int progIdx = MatchProgramForCatalog(s_driveTargetX, s_driveTargetY,
                                         veh, story, &region);
    if (progIdx < 0 && veh != VEH_ON_FOOT) {
        // v0.18.3.193: FOOT-RETRY. The world-map locomotion byte occasionally latches a bogus
        // NON-foot vehicle -- this long session showed s_lastVehicle=33 (a car mode) sticking after
        // the Laguna sequence / encounters, so every on-foot trigger clause failed its vehicle check
        // ("FAIL veh (player veh=3)"), the walk found zero active regions, the planner returned false,
        // and the drive dropped to dumb straight-line steering -- which is the "auto-drive keeps
        // forgetting where it's going" the player saw (toggling AD off/on re-planned once the byte
        // settled). World-map auto-drive is a foot feature, so if nothing matches the read vehicle,
        // retry as ON-FOOT before abandoning the route rather than wandering off-plan.
        Log::World("WorldMap: [PLAN] No program for target under veh=%d -> retrying ON-FOOT (stale vehicle byte)",
                   (int)veh);
        progIdx = MatchProgramForCatalog(s_driveTargetX, s_driveTargetY,
                                         VEH_ON_FOOT, story, &region);
    }
    if (progIdx < 0) {
        return false;
    }

    s_driveGoalSegCount = CollectGoalSegments(region);
    if (s_driveGoalSegCount == 0) {
        Log::World("WorldMap: [PLAN] Region 0x%02X has zero cells in s_segmentRegionMap \u2014 fallback",
                   (unsigned)region);
        return false;
    }

    int startCol = WorldXToSegCol(startX);
    int startRow = WorldYToSegRow(startY);

#if NAVMESH_DIAG && NAVMESH_ROUTING
    // v0.18.3.106 (#70 routing swap, stage 1): try the navmesh route FIRST. The
    // walkability-filtered A* gives an all-walkable path that inherently avoids
    // the false coast / mountains, so no .81/.85/#69 compensation is needed on
    // it. Fall through to the fine-grid planner only if the navmesh has no path
    // (e.g. mesh not yet built). The departure bridge-out below is a fine-grid
    // pocket recovery, so disable it on the navmesh path.
    //
    // v0.18.3.109: the navmesh now runs BEFORE the coarse IsGoalSegment
    // shortcut. Dollet's region 0x01 spans a large coarse segment, so on the
    // final approach the player enters the goal segment ~4km out; the old
    // early-return then handed back an EMPTY path and the executor steered
    // straight at the Dollet coordinate THROUGH the false coast and wedged
    // (BAT .108: idx 0/0, JAM at dist 4681). Routing the navmesh first threads
    // the gentle corridor that final stretch. IsGoalSegment stays just below as
    // the fallback for when the navmesh has no path (mesh not built, or start
    // == goal tri at the destination itself -> empty path -> arrival).
    if (PlanDrivePathNavmesh(startX, startY)) {
        s_driveBridgeActive = false;
        return true;
    }
#endif

    if (IsGoalSegment(startRow, startCol)) {
        s_drivePathLen     = 0;
        s_drivePathIdx     = 0;
        s_drivePathPlanned = true;
        Log::World("WorldMap: [PLAN] Player already in goal segment seg(%d,%d) region=0x%02X \u2014 empty path",
                   startCol, startRow, (unsigned)region);
        return true;
    }

    // #67 stage 2: route on the FINE slope-aware grid (around mountains/ocean),
    // not the coarse 32x24 segment grid that has no mountain class.
    int startFineCol = WorldXToFineCol(startX);
    int startFineRow = WorldYToFineRow(startY);
    bool ok = PlanPathFine(startFineCol, startFineRow, veh);

    // #70 v0.18.3.97: departure bridge-out. If the route starts in a tiny pocket
    // (the BFS expanded only a few cells AND dead-ended well short of target),
    // the start is sealed in by the #69 height-step guard (the Dollet shelf).
    // Steer to the nearest road cell -- the guard-exempt ramp the character
    // arrived on -- then UpdateAutoDrive re-plans from there. Bounded per drive.
    s_driveBridgeActive = false;
    // #70 v0.18.3.98: key on the pocket signal regardless of PlanPathFine's return
    // value. The real Dollet pocket exits via the early-return-FALSE branch
    // ("start already closest reachable"), so the old `ok &&` gate skipped the
    // bridge entirely. The block is only reached right after PlanPathFine runs, so
    // the signals are always fresh; normal drives expand hundreds + reach dist~0,
    // so the pocket test still cannot false-trigger.
    if (s_drivePlanExpanded <= WM_POCKET_MAX_EXPANDED &&
        s_drivePlanDist >= WM_POCKET_MIN_DIST &&
        s_driveBridgeCount < WM_BRIDGE_MAX) {
        int32_t bx, by;
        if (FindNearestRoadWorld(startFineCol, startFineRow, &bx, &by)) {
            s_driveBridgeActive = true;
            s_driveBridgeX = bx;
            s_driveBridgeY = by;
            s_driveBridgeCount++;
            Log::World("WorldMap: [DRIVE] Pocketed start fine(%d,%d) (expanded %d, dist %d) -> BRIDGE-OUT to nearest road cell at world(%d,%d) (attempt %d/%d)",
                       startFineCol, startFineRow, s_drivePlanExpanded, s_drivePlanDist,
                       (int)bx, (int)by, s_driveBridgeCount, WM_BRIDGE_MAX);
        } else {
            Log::World("WorldMap: [DRIVE] Pocketed start fine(%d,%d) (expanded %d, dist %d) but no road cell within %d cells -- cannot bridge",
                       startFineCol, startFineRow, s_drivePlanExpanded, s_drivePlanDist, WM_BRIDGE_ROAD_RADIUS);
        }
    }
    return ok;
}

// ============================================================================
// ComputePlannerEligibility (v0.16.0 -- Part C)
// ============================================================================
// Called once at Initialize() time, AFTER LoadTriggerZones (so s_segmentRegionMap
// is populated) and AFTER the catalog is registered (so s_locations[] is valid).
// Walks every catalog entry, finds its region byte from s_segmentRegionMap,
// and scans s_triggerPrograms[] looking for at least one clause that names
// that region with a foot vehicle code (TRIG_VEH_FOOT 0x80 or TRIG_VEH_FOOT_ALT
// 0x84). If such a clause exists, the destination is "planner-eligible" and
// StartAutoDrive will route via PlanDrivePath / A* + closest-active-region.
// If no foot clause matches, the destination is a geometric-trigger destination
// (entered via terrain-29 polygon trigger, no wmsetus script event); the A*
// planner cannot represent it and its closest-active-region fallback will
// misroute the drive toward unrelated destinations (e.g. Fire Cavern catalog
// at region 0x0C maps only to program 20's Garden clause, no foot clause; the
// v0.14.95 fallback picks an unrelated active region and routes the player
// across the map).
//
// Geometric-trigger destinations include: Fire Cavern, early-game Balamb Garden
// (before mobile-Garden phase), several chocobo forests on small islands. AD
// for these falls back to v0.11.11-era simple-coord steering toward catalog
// coordinates -- not perfect but bounded and predictable.
//
// Logs each catalog entry's classification at init for diagnostic clarity.
// Defaults all flags to false; if s_segmentRegionLoaded is false (e.g.
// LoadTriggerZones failed), no entry is marked eligible -- safer than
// over-marking and routing into the wrong destination.
static void ComputePlannerEligibility()
{
    memset(s_destPlannerEligible, 0, sizeof(s_destPlannerEligible));

    if (!s_segmentRegionLoaded) {
        Log::World("WorldMap: [INIT] Planner-eligibility: s_segmentRegionMap not loaded, no destinations marked eligible");
        return;
    }

    int eligibleCount = 0;
    for (int catIdx = 0; catIdx < LOCATION_COUNT; catIdx++) {
        int col = WorldXToSegCol(s_locations[catIdx].x);
        int row = WorldYToSegRow(s_locations[catIdx].y);
        if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) {
            Log::World("WorldMap: [INIT] Planner-eligibility: %s (%d,%d) -> seg out of range -> NO",
                       s_locations[catIdx].name, s_locations[catIdx].x, s_locations[catIdx].y);
            continue;
        }
        uint8_t region = s_segmentRegionMap[row][col];
        if (region == 0xFF) {
            Log::World("WorldMap: [INIT] Planner-eligibility: %s seg(%d,%d) region=0xFF (no region) -> NO",
                       s_locations[catIdx].name, row, col);
            continue;
        }

        // Walk s_triggerPrograms[] looking for any clause that names this
        // region with a foot vehicle code.
        bool footClauseFound = false;
        for (int p = 0; p < TRIGGER_PROGRAM_COUNT && !footClauseFound; p++) {
            const TriggerProgram& prog = s_triggerPrograms[p];
            for (uint32_t c = 0; c < prog.num_clauses && !footClauseFound; c++) {
                const TriggerClause& cl = prog.clauses[c];
                if (cl.region != region) continue;
                if (cl.vehicle == TRIG_VEH_FOOT || cl.vehicle == TRIG_VEH_FOOT_ALT) {
                    footClauseFound = true;
                }
            }
        }

        s_destPlannerEligible[catIdx] = footClauseFound;
        if (footClauseFound) {
            eligibleCount++;
            Log::World("WorldMap: [INIT] Planner-eligibility: %s seg(%d,%d) region=0x%02X -> YES",
                       s_locations[catIdx].name, row, col, region);
        } else {
            Log::World("WorldMap: [INIT] Planner-eligibility: %s seg(%d,%d) region=0x%02X -> NO (no foot clause; will use simple-coord steering)",
                       s_locations[catIdx].name, row, col, region);
        }
    }
    Log::World("WorldMap: [INIT] Planner-eligibility: %d of %d catalog entries are planner-eligible",
               eligibleCount, LOCATION_COUNT);
}
