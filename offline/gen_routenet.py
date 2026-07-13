"""gen_routenet.py -- precomputed validated route-network generator (v0.18.3.209).

Builds the world-map ROUTE NETWORK: nodes = catalog entry aims, edges = exact
polylines (arbitrary coordinates, not 128u grid rows) that
  1. are seeded by a 128u A* over the 32u bit7 walk grids,
  2. are refined onto the MAXIMUM-CLEARANCE channel (the point that maximizes
     distance to the nearest non-walkable first-containing geometry -- the
     BAT208 sticky-zone model says clearance > ZONE_R=90u is what the engine
     actually respects; interval midpoints cut corners, max-depth does not),
  3. avoid all non-endpoint decoded entry firing areas (s_entryAims bboxes),
  4. are validated by the fitted stateful-aware engine replica INCLUDING the
     full executor simulation (camera-write + slide + recovery, run_drive3,
     hatch="away") in BOTH directions.

Resumable: state in <outdir>/routenet_state.json + per-edge files. Final
artifacts: routenet.json + ../src/world_map_routenet.inl (via --emit).

Usage:
  python3 gen_routenet.py <wmx.obj> --grids <griddir> --out <outdir> [--budget 35]
  python3 gen_routenet.py <wmx.obj> --grids <griddir> --out <outdir> --emit <inl>
"""
import json, math, os, sys, time, heapq

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nav_sim as ns
from nav_sim import (Oracle, GridOracle, EngineSimC, Planner2, run_drive3,
                     tdist, au_sin, au_cos)

ZONE_R = ns.ZONE_R                 # 90
DEPTH_CAP = ZONE_R + 8             # 98: out of the sticky zone
RCAP = 160                         # refinement clearance target: bends near walls
                                   # poison the engine's MRU cache (camera boom) --
                                   # aim for 160u of air where the terrain has it
NARROW = DEPTH_CAP                 # local depth < 98 => pinch => 32u spacing
SPACING_WIDE = 96
SPACING_NARROW = 32
MAX_EDGE_PTS = 600

# ---- nodes: decoded entry AIM points (src/world_map_trigger_data.inl) where
# available, catalog coords otherwise. Names must match the mod catalog.
NODES = {
    "Timber":            (-22580,  -5291),
    "Dollet":            (-14513, -39119),
    "Balamb Town":       ( 12560, -26800),
    "Balamb Garden":     ( 24304, -30300),
    "Fire Cavern":       ( 30239, -29528),
    "Galbadia Garden":   (-36895, -27082),
    "Galbadia Station":  (-38914, -24767),
    # junction nodes (open plains, outside every decoded firing area): let
    # Station<->Timber/Dollet chains BYPASS the Galbadia Garden node, whose
    # aim point sits inside GG's own firing area (chaining through it would
    # fire the GG field entry mid-route).
    "GG West Plains":    (-39691, -27008),
    "GG East Plains":    (-32742, -26874),
    # v0.18.3.211 junctions (both are exact vertices of the fully sim-validated
    # .209 Dollet<->Timber polyline, in open terrain outside every firing area).
    # The .210 BAT proved the Galbadia pass IMPASSABLE live, so ALL Timber
    # traffic now routes the long way east over that proven corridor:
    # Yaulny Plains  = mouth of the west-channel diagonal NE of G-Garden --
    #                  the only crossing of the Timber mountain belt that both
    #                  the engine replica and the executor sim accept (the
    #                  road's own canyon neck at (-30040,-20000) is a ~64u
    #                  sliver, far inside the 90u sticky zone; the .205 massif-
    #                  tip gap seam sim-blocks deterministically).
    # Hasberry Plains = coastal point at the Dollet mouth, OUTSIDE Dollet's
    #                  firing bbox, so Timber traffic passes Dollet without
    #                  entering it (hard transit rule: Dollet = destination-only).
    "Yaulny Plains":     (-31345, -25449),
    "Hasberry Plains":   (-16452, -39492),
    "Lunar Gate":        ( 88021,   7865),
    "Sorceress Memorial":( 81521,  11865),
}
# decoded firing-area bboxes (non-endpoint areas are hard-avoided by edges)
AREAS = {
    "Timber":           (-22685, -22371,  -5632,  -5120),
    "Dollet":           (-15409, -13516, -39951, -38175),
    "Balamb Town":      ( 12288,  12884, -26896, -26624),
    "Balamb Garden":    ( 23552,  25410, -31370, -29696),
    "Fire Cavern":      ( 30112,  30394, -29750, -29192),
    "Galbadia Garden":  (-37764, -35964, -28036, -26236),
    "Galbadia Station": (-39426, -38398, -24936, -24682),
}
EDGES = [
    ("Galbadia Garden", "Galbadia Station"),
    ("Galbadia Garden", "GG West Plains"),
    # v0.18.3.211: the pass edge ("GG West Plains", "Timber") is REMOVED --
    # the .210 BAT proved it impassable live (26-29 km of cycling on the
    # validated max-clearance line, zero crossings; the 6-8u modeled margin
    # was real). Polyline kept in routenet_state under "GG West Plains|Timber".
    ("Galbadia Garden", "GG East Plains"),
    # v0.18.3.211: ("GG East Plains","Dollet") REMOVED -- the .205 track's
    # massif-tip gap seam (-31.3k,-26.2k) sim-blocks deterministically both
    # directions (steep double-geometry skirt poisons the |dH|>=200 gate) and
    # blocked 4 matrix pairs since .209; GG<->Dollet now rides the proven
    # corridor below at comparable length. ("Dollet","Timber") is not gone:
    # it is SPLIT at the two .211 junctions into the three edges below.
    ("GG East Plains", "Yaulny Plains"),    # new short link across open plains
    ("Yaulny Plains", "Timber"),            # old edge: west channel N, belt crossing, Timber
    ("Yaulny Plains", "Hasberry Plains"),   # old edge: SE diagonal + west coast
    ("Hasberry Plains", "Dollet"),          # old edge: coastal mouth stub
    ("Galbadia Station", "GG West Plains"),
    ("Galbadia Station", "GG East Plains"),
    # v0.18.3.210: junction<->junction BYPASS. With location-node transit
    # forbidden (the .209 BAT re-fired GG/Station fields when Dijkstra chained
    # THROUGH their aim nodes), the two Plains junctions were only connected
    # via GG or Station. This edge links them directly: north around GG's
    # firing area along the station corridors, cutting south of the Station
    # bbox (seed_hint splices the two validated Station edges).
    ("GG East Plains", "GG West Plains"),
    ("Balamb Garden", "Balamb Town"),
    ("Balamb Town", "Fire Cavern"),
    ("Balamb Garden", "Fire Cavern"),
    ("Lunar Gate", "Sorceress Memorial"),
]

_depth_memo = {}

def zone_depth(oracle, x, z, cap=RCAP):
    """Min distance to nonwalk first-containing geometry (16 rays, 8u pitch),
    capped at `cap`. EXACT oracle required (slivers matter)."""
    k = (int(x) // 8, int(z) // 8)
    v = _depth_memo.get(k)
    if v is not None:
        return v
    v = cap
    for hh in range(0, 4096, 256):
        dxu, dzu = au_sin(hh), -au_cos(hh)
        t = 8
        while t < v:
            ph, pw = Oracle.first(oracle, x + dxu * t, z + dzu * t)
            if ph is None or not pw:
                v = t
                break
            t += 8
    _depth_memo[k] = v
    return v


def seed_astar(goracle, start, goal, margin, step=128):
    """128u A* over the 32u bit7 walk grids (fitted-walkability seeding).
    Diagonals allowed; |dH|>=200 between cell centers rejected."""
    STEP = step
    s = (int(round(start[0] / STEP)), int(round(start[1] / STEP)))
    g = (int(round(goal[0] / STEP)), int(round(goal[1] / STEP)))
    m = margin // STEP
    lox, hix = min(s[0], g[0]) - m, max(s[0], g[0]) + m
    loz, hiz = min(s[1], g[1]) - m, max(s[1], g[1]) + m
    cache = {}
    def cell(c):
        v = cache.get(c)
        if v is None:
            h, w = goracle.first(c[0] * STEP, c[1] * STEP)
            v = (h, w and h is not None)
            cache[c] = v
        return v
    def snap(c):
        if cell(c)[1]:
            return c
        best, bd = None, 1e30
        for r in range(1, 9):
            for dx in range(-r, r + 1):
                for dz in (-r, r):
                    for cnd in ((c[0]+dx, c[1]+dz), (c[0]+dz, c[1]+dx)):
                        if cell(cnd)[1]:
                            d = (cnd[0]-c[0])**2 + (cnd[1]-c[1])**2
                            if d < bd:
                                best, bd = cnd, d
            if best:
                return best
        return None
    s = snap(s); g = snap(g)
    if s is None or g is None:
        return None
    SQ2 = math.sqrt(2)
    def heur(c):
        dx, dz = abs(c[0]-g[0]), abs(c[1]-g[1])
        return dx + dz + (SQ2 - 2) * min(dx, dz)
    openh = [(heur(s), 0, s)]
    gs = {s: 0.0}
    came = {}
    tie = 0
    expanded = 0
    while openh:
        _f, _t, cur = heapq.heappop(openh)
        if cur == g:
            path = [cur]
            while cur in came:
                cur = came[cur]
                path.append(cur)
            path.reverse()
            return [(c[0] * STEP, c[1] * STEP) for c in path]
        expanded += 1
        if expanded > 4000000:
            return None
        h0 = cell(cur)[0]
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                if not dx and not dz:
                    continue
                n = (cur[0] + dx, cur[1] + dz)
                if not (lox <= n[0] <= hix and loz <= n[1] <= hiz):
                    continue
                nh, nok = cell(n)
                if not nok or abs(nh - h0) >= 200:
                    continue
                ng = gs[cur] + (SQ2 if dx and dz else 1.0)
                if ng < gs.get(n, 1e30):
                    gs[n] = ng
                    came[n] = cur
                    tie += 1
                    heapq.heappush(openh, (ng + heur(n), tie, n))
    return None


def refine_maxdepth(oracle, seed, endpoints_names):
    """Refine a seed polyline onto the max-clearance channel:
    per vertex, hill-climb along the local perpendicular for maximum
    zone_depth (ties -> stay near the previous refined point), with a
    per-vertex lateral continuity clamp; skip refinement where the seed is
    already out-of-zone AND >160u deep in open land."""
    out = [seed[0]]
    prev_t = 0
    forbid = [(nm, AREAS[nm]) for nm in AREAS if nm not in endpoints_names]
    for i in range(1, len(seed) - 1):
        ax, az = seed[i - 1]
        bx, bz = seed[i + 1]
        dx, dz = bx - ax, bz - az
        L = math.hypot(dx, dz) or 1.0
        px, pz = -dz / L, dx / L         # unit perpendicular
        sx, sz = seed[i]
        base_d = zone_depth(oracle, sx, sz)
        if base_d >= DEPTH_CAP:
            out.append((sx, sz))
            prev_t = 0
            continue
        best, bt, bd = (sx, sz), 0, -1
        lo = max(-224, prev_t - 96)
        hi = min(224, prev_t + 96)
        t = lo
        while t <= hi:
            cx, cz = sx + px * t, sz + pz * t
            h, w = Oracle.first(oracle, cx, cz)
            if h is not None and w:
                inarea = False
                for _nm, (x0, x1, z0, z1) in forbid:
                    if x0 - 64 <= cx <= x1 + 64 and z0 - 64 <= cz <= z1 + 64:
                        inarea = True
                        break
                if not inarea:
                    d = zone_depth(oracle, cx, cz)
                    if d > bd or (d == bd and abs(t - prev_t) < abs(bt - prev_t)):
                        best, bt, bd = (int(round(cx)), int(round(cz))), t, d
            t += 8
        out.append(best)
        prev_t = bt
    out.append(seed[-1])
    return out


def resample(oracle, pts):
    """Arc-length resample: 32u through pinches (depth<DEPTH_CAP), 96u in the
    open; each emitted point re-hill-climbed +-24u perpendicular for depth.
    Correct accumulation across input vertices closer than the output spacing
    (lattice repair segments come in at 16u)."""
    out = [tuple(pts[0])]
    since = 0.0                    # distance walked since the last emitted point
    for a, b in zip(pts, pts[1:]):
        d = tdist(a[0], a[1], b[0], b[1])
        if d < 1e-9:
            continue
        da = zone_depth(oracle, a[0], a[1])
        db = zone_depth(oracle, b[0], b[1])
        spacing = SPACING_NARROW if min(da, db) < NARROW else SPACING_WIDE
        pos = 0.0
        while d - pos > spacing - since:
            pos += spacing - since
            since = 0.0
            x = a[0] + (b[0] - a[0]) * pos / d
            z = a[1] + (b[1] - a[1]) * pos / d
            px, pz = -(b[1] - a[1]) / d, (b[0] - a[0]) / d
            bx, bz, bd = x, z, zone_depth(oracle, x, z)
            if bd < DEPTH_CAP:
                for off in (-24, -16, -8, 8, 16, 24):
                    cx, cz = x + px * off, z + pz * off
                    h, w = Oracle.first(oracle, cx, cz)
                    if h is not None and w:
                        dd = zone_depth(oracle, cx, cz)
                        if dd > bd:
                            bx, bz, bd = cx, cz, dd
            out.append((int(round(bx)), int(round(bz))))
        since += d - pos
    if out[-1] != tuple(pts[-1]):
        out.append(tuple(pts[-1]))
    return out


def polyline_len(pts):
    return sum(tdist(a[0], a[1], b[0], b[1]) for a, b in zip(pts, pts[1:]))


def trim_head(oracle, pts):
    """First polyline index that is out of the sticky zone (or <=800u in):
    validation starts there -- LIVE the character starts wherever it stands
    and the local hop / entry machinery own the node mouths."""
    acc = 0.0
    for i in range(len(pts) - 1):
        if ((acc >= 640 and zone_depth(oracle, pts[i][0], pts[i][1]) >= DEPTH_CAP)
                or acc > 1100):
            return i
        acc += tdist(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1])
    return 0


def validate(oracle, pts, cap=None):
    """Executor-sim the polyline BOTH directions on the exact oracle with the
    BAT208 engine (hatch='away'). Returns dict with statuses + frames."""
    if cap is None:
        cap = int(polyline_len(pts) / 32 * 8) + 4000
    A, B = pts[0], pts[-1]
    # arrive_r=560: an edge must deliver the character to within 560u of the
    # node; from there the mod's live final-approach machinery (retarget to
    # the decoded aim, TRIGREADY, ENTRYMOW area sweep) owns the entry -- the
    # decoded firing areas themselves span hundreds of units around the aim.
    def one_dir(wps, target):
        """Drive the polyline; on a block, resume ONCE from the block position
        with the learned overlay carried over -- the live executor replans
        around a transient (cache-capture) block and continues; a REAL grind
        (e.g. the .208 pass line) re-blocks at the same spot and still fails."""
        r = run_drive3(oracle, wps, wps[0], target, engine="cache", g1=True,
                       g2=True, g4=True, mod_probe="swept", cap=cap,
                       arrive_r=560)
        resumes = 0
        if r["status"] != "arrived":
            bi = min(range(len(wps)), key=lambda i: (wps[i][0] - r["pos"][0]) ** 2
                     + (wps[i][1] - r["pos"][1]) ** 2)
            tail = wps[max(0, bi - 1):]
            if len(tail) >= 2:
                ov = set(r["learned"])
                r2 = run_drive3(oracle, tail, (r["pos"][0], r["pos"][1]), target,
                                engine="cache", g1=True, g2=True, g4=True,
                                mod_probe="swept", cap=cap, arrive_r=560,
                                overlay=ov)
                if r2["status"] == "arrived":
                    resumes = 1
                    r2["frames"] += r["frames"]
                    r = r2
        return dict(status=r["status"], frames=r["frames"],
                    blocked=r["blocked_frames"], end=list(r["pos"]),
                    resumes=resumes)
    fpts = pts[trim_head(oracle, pts):]
    rev = list(reversed(pts))
    rpts = rev[trim_head(oracle, rev):]
    return dict(fwd=one_dir(fpts, B), rev=one_dir(rpts, A))


def depth_pen(d):
    """Lattice traversal penalty for clearance d (u). 0 above 140u; steep
    below DEPTH_CAP (98) -- the sticky zone boundary is 90."""
    if d >= 140:
        return 0.0
    if d >= DEPTH_CAP:
        return ((140 - d) / 12.0) ** 2 * 0.25
    if d >= 72:
        return 20.0 + ((DEPTH_CAP - d) / 4.0) ** 2
    return 400.0 + (72 - d) * 40.0


def lattice_path(oracle, A, B, extra=None, pad=900, step=16, cap_nodes=260000):
    """16u Dijkstra from A to B maximizing clearance (min depth-penalty
    path). Box = bbox(A,B,extra)+pad. Returns list of pts or None."""
    x0 = min(A[0], B[0]) - pad; x1 = max(A[0], B[0]) + pad
    z0 = min(A[1], B[1]) - pad; z1 = max(A[1], B[1]) + pad
    if extra:
        x0 = min(x0, extra[0] - pad); x1 = max(x1, extra[0] + pad)
        z0 = min(z0, extra[1] - pad); z1 = max(z1, extra[1] + pad)
    sa = (int(round(A[0] / step)), int(round(A[1] / step)))
    sb = (int(round(B[0] / step)), int(round(B[1] / step)))
    walk = {}
    def ok(c):
        v = walk.get(c)
        if v is None:
            x, z = c[0] * step, c[1] * step
            if not (x0 <= x <= x1 and z0 <= z <= z1):
                v = False
            else:
                h, w = Oracle.first(oracle, x, z)
                v = (h is not None and w)
            walk[c] = v
        return v
    if not ok(sa) or not ok(sb):
        return None
    SQ2 = math.sqrt(2)
    def hcost(c):
        return math.hypot(c[0] - sb[0], c[1] - sb[1])
    openh = [(hcost(sa), 0, sa)]
    gs = {sa: 0.0}
    came = {}
    tie = 0
    seen = 0
    while openh:
        _f, _t, cur = heapq.heappop(openh)
        if cur == sb:
            path = [cur]
            while cur in came:
                cur = came[cur]
                path.append(cur)
            path.reverse()
            return [(c[0] * step, c[1] * step) for c in path]
        seen += 1
        if seen > cap_nodes:
            return None
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                if not dx and not dz:
                    continue
                n = (cur[0] + dx, cur[1] + dz)
                if not ok(n):
                    continue
                d = zone_depth(oracle, n[0] * step, n[1] * step)
                w = (SQ2 if dx and dz else 1.0) * (1.0 + depth_pen(d))
                ng = gs[cur] + w
                if ng < gs.get(n, 1e30):
                    gs[n] = ng
                    came[n] = cur
                    tie += 1
                    heapq.heappush(openh, (ng + hcost(n), tie, n))
    return None


def repair(oracle, pts, block, endpoints_names):
    """Replace the polyline section around a validation block position with a
    lattice max-clearance path. Returns new pts or None."""
    if (tdist(pts[0][0], pts[0][1], block[0], block[1]) < 800 or
            tdist(pts[-1][0], pts[-1][1], block[0], block[1]) < 800):
        return None       # node-end approach: the entry machinery owns it
    bi = min(range(len(pts)),
             key=lambda i: (pts[i][0] - block[0]) ** 2 + (pts[i][1] - block[1]) ** 2)
    i0 = bi
    while i0 > 0 and tdist(pts[i0][0], pts[i0][1], block[0], block[1]) < 1300:
        i0 -= 1
    i1 = bi
    while i1 < len(pts) - 1 and tdist(pts[i1][0], pts[i1][1], block[0], block[1]) < 1300:
        i1 += 1
    A, B = tuple(pts[i0]), tuple(pts[i1])
    seg = lattice_path(oracle, A, B, extra=tuple(block))
    if not seg:
        return None
    seg = resample(oracle, seg)
    return [tuple(p) for p in pts[:i0]] + seg + [tuple(p) for p in pts[i1 + 1:]]


def seed_hint(a, b, outdir, goracle=None):
    """Optional per-edge macro-route overrides."""
    if (a, b) == ("GG East Plains", "GG West Plains"):
        # Splice the two VALIDATED station corridors, cut where each is
        # safely outside the padded Station firing bbox (>=20u margin past
        # the +-64u pad), bridged south of the Station area.
        st = json.load(open(os.path.join(outdir, "routenet_state.json")))
        e74 = [tuple(p) for p in st["edges"]["Galbadia Station|GG East Plains"]["pts"]]
        e75 = [tuple(p) for p in st["edges"]["Galbadia Station|GG West Plains"]["pts"]]
        x0, x1, z0, z1 = AREAS["Galbadia Station"]
        def outside(p):
            return not (x0 - 64 <= p[0] <= x1 + 64 and z0 - 64 <= p[1] <= z1 + 64)
        i74 = next(i for i, p in enumerate(e74) if outside(p) and p[1] <= z0 - 84)
        i75 = next(i for i, p in enumerate(e75) if outside(p) and p[1] <= z0 - 84)
        seed = list(reversed(e74[i74:]))          # East Plains -> cut
        bx, bz = seed[-1]; cx, cz = e75[i75]
        n = max(1, int(tdist(bx, bz, cx, cz) // 96))
        for i in range(1, n):
            seed.append((int(bx + (cx - bx) * i / n), int(bz + (cz - bz) * i / n)))
        return seed + e75[i75:]                   # cut -> West Plains
    if a == "Galbadia Station" and b in ("GG West Plains", "GG East Plains"):
        ax, az = NODES[a]; bx, bz = NODES[b]
        n = max(1, int(tdist(ax, az, bx, bz) // 128))
        return [(int(ax + (bx - ax) * i / n), int(az + (bz - az) * i / n))
                for i in range(n + 1)]
    if (a, b) == ("Galbadia Garden", "Dollet"):
        # LIVE-PROVEN east corridor: the successful v0.18.3.205 BAT drive
        # (2026 session log 21:45:20-21:46:46) from the plains east of GG to
        # Dollet -- the only end-to-end live traversal of this corridor.
        tf = os.path.join(os.path.dirname(outdir), "bat208", "track205_dollet.json")
        if os.path.exists(tf):
            track = [tuple(p) for p in json.load(open(tf))]
            seed = [NODES[a]]
            ax, az = NODES[a]; bx, bz = track[0]
            n = max(1, int(tdist(ax, az, bx, bz) // 128))
            for i in range(1, n):
                seed.append((int(ax + (bx - ax) * i / n), int(az + (bz - az) * i / n)))
            return seed + track + [NODES[b]]
    if (a, b) == ("GG East Plains", "Yaulny Plains"):
        # short NE link over open plains onto the Dollet<->Timber corridor
        ax, az = NODES[a]
        wps = [(ax, az), (-32300, -26100), (-31900, -25700), NODES[b]]
        seed = []
        for p, q in zip(wps, wps[1:]):
            n = max(1, int(tdist(p[0], p[1], q[0], q[1]) // 128))
            for i in range(n):
                seed.append((int(p[0] + (q[0] - p[0]) * i / n),
                             int(p[1] + (q[1] - p[1]) * i / n)))
        seed.append(wps[-1])
        return seed
    if a in ("Yaulny Plains", "Hasberry Plains") or b in ("Yaulny Plains", "Hasberry Plains"):
        # splits of the fully validated .209 "Dollet|Timber" polyline at the
        # exact junction vertices (state surgery pre-seeds these; this hint is
        # the fallback if the state entry is absent).
        st2 = json.load(open(os.path.join(outdir, "routenet_state.json")))
        old = [tuple(p) for p in st2["edges"]["Dollet|Timber"]["pts"]]
        def vidx(p):
            return min(range(len(old)),
                       key=lambda i: (old[i][0]-p[0])**2 + (old[i][1]-p[1])**2)
        iY, iH = vidx(NODES["Yaulny Plains"]), vidx(NODES["Hasberry Plains"])
        if (a, b) == ("Yaulny Plains", "Timber"):
            return old[iY:]
        if (a, b) == ("Yaulny Plains", "Hasberry Plains"):
            return list(reversed(old[iH:iY + 1]))
        if (a, b) == ("Hasberry Plains", "Dollet"):
            return list(reversed(old[:iH + 1]))
    if (a, b) == ("Galbadia Garden", "Timber") and goracle is not None:
        # west-loop macro route (through the Galbadia pass, up the -51456
        # column, east along z~-14400): center-sampled 128u A*. Validation
        # proved this macro FWD all the way to Timber; the one rev speckle
        # gets lattice-repaired. (The Planner2 seed picks the north-coast
        # cape instead, which is a true sticky belt -- avoid it.)
        return seed_astar(goracle, NODES[a], NODES[b], 24576, step=128)
    return None


def seed_terrain(oracle_exact, a, b):
    """Terrain-rule (byte13) 128u A* -- crosses walkable-preferred overlay
    spots the bit7 planner refuses (the engine walks them; validation is the
    real gate)."""
    return oracle_exact.wmx.route(NODES[a], NODES[b], step=128, margin=12288)


def main():
    wmx_path = sys.argv[1]
    grids = out = emit = None
    budget = None
    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--grids":
            grids = args[i + 1]; i += 2
        elif args[i] == "--out":
            out = args[i + 1]; i += 2
        elif args[i] == "--budget":
            budget = float(args[i + 1]); i += 2
        elif args[i] == "--emit":
            emit = args[i + 1]; i += 2
        else:
            i += 1
    t0 = time.time()
    os.makedirs(out, exist_ok=True)
    stf = os.path.join(out, "routenet_state.json")
    st = json.load(open(stf)) if os.path.exists(stf) else {"edges": {}}

    wmx = ns.ff8_walkmesh.WMX(wmx_path)
    oracle = Oracle(wmx)                       # EXACT (refine + validate)
    goracle = GridOracle(wmx, grids) if grids else oracle

    # persistent depth memo (biggest cost)
    memof = os.path.join(out, "depth_memo_r160.json")
    if os.path.exists(memof) and not _depth_memo:
        try:
            for k, v in json.load(open(memof)).items():
                a, b = k.split(",")
                _depth_memo[(int(a), int(b))] = v
        except Exception:
            pass

    def save():
        json.dump(st, open(stf + ".tmp", "w"), indent=1)
        os.replace(stf + ".tmp", stf)
        json.dump({"%d,%d" % k: v for k, v in _depth_memo.items()},
                  open(memof + ".tmp", "w"))
        os.replace(memof + ".tmp", memof)

    def left():
        return None if budget is None else budget - (time.time() - t0)

    if emit:
        emit_inl(st, emit)
        return

    for a, b in EDGES:
        key = "%s|%s" % (a, b)
        e = st["edges"].setdefault(key, {"stage": "seed"})
        if e["stage"] == "done" or e["stage"] == "failed":
            continue
        if left() is not None and left() < 10:
            save(); print("budget out"); return
        if e["stage"] == "seed":
            # Planner2 = the fitted-gate 128u A* with 32u edge sub-march and
            # the x4 near-wall clearance cost (BAT203) -- seeds prefer open
            # land and see 1.5-cell passes the center-sampled grid misses.
            seed = seed_hint(a, b, out, goracle)
            if not seed and (a, b) == ("Galbadia Garden", "Dollet"):
                seed = seed_terrain(oracle, a, b)   # east route: engine walks
                                                    # overlay spots bit7 refuses
            if not seed:
                pl = Planner2(goracle, mode="fitted", clearance_penalty=4)
                for margin in (12288, 24576, 49152):
                    seed = pl.plan(NODES[a], NODES[b], margin=margin)
                    if seed:
                        break
            if not seed:
                seed = seed_terrain(oracle, a, b)   # byte13 terrain-rule A*
            if not seed:
                for step, margin in ((64, 24576), (32, 16384)):
                    seed = seed_astar(goracle, NODES[a], NODES[b], margin, step=step)
                    if seed:
                        break
            if not seed:
                e["stage"] = "failed"; e["why"] = "no seed path"
                save(); continue
            e["seed"] = seed
            e["stage"] = "refine"
            save()
            print(key, "seeded:", len(seed), "pts", flush=True)
        if left() is not None and left() < 10:
            save(); print("budget out"); return
        if e["stage"] == "refine":
            pts = [tuple(p) for p in e["seed"]]
            for _pass in range(2):
                pts = refine_maxdepth(oracle, pts, (a, b))
            pts = resample(oracle, pts)
            if len(pts) > MAX_EDGE_PTS:
                # thin wide-area points keeping pinch density
                keep = [pts[0]]
                for p in pts[1:-1]:
                    if (zone_depth(oracle, p[0], p[1]) < NARROW or
                            tdist(keep[-1][0], keep[-1][1], p[0], p[1]) >= 120):
                        keep.append(p)
                keep.append(pts[-1])
                pts = keep
            e["pts"] = [list(p) for p in pts]
            e["len"] = int(polyline_len(pts))
            e["stage"] = "validate"
            save()
            print(key, "refined:", len(pts), "pts len", e["len"], flush=True)
        if left() is not None and left() < 10:
            save(); print("budget out"); return
        if e["stage"] == "validate":
            pts = [tuple(p) for p in e["pts"]]
            v = None
            for rnd in range(9):
                v = validate(oracle, pts)
                ok = (v["fwd"]["status"] == "arrived"
                      and v["rev"]["status"] == "arrived")
                print(key, "validate round", rnd, ":", v["fwd"]["status"],
                      v["rev"]["status"], "frames", v["fwd"]["frames"],
                      v["rev"]["frames"], flush=True)
                if ok or rnd == 8:
                    break
                fixed = False
                for d in ("fwd", "rev"):
                    if v[d]["status"] != "arrived":
                        np_ = repair(oracle, pts, v[d]["end"], (a, b))
                        if np_:
                            pts = np_
                            fixed = True
                        else:
                            print(key, "repair FAILED at", v[d]["end"], flush=True)
                if not fixed:
                    break
                e["pts"] = [list(p) for p in pts]
                e["repairs"] = e.get("repairs", 0) + 1
                save()
            e["pts"] = [list(p) for p in pts]
            e["len"] = int(polyline_len(pts))
            e["validation"] = v
            ok = v["fwd"]["status"] == "arrived" and v["rev"]["status"] == "arrived"
            e["stage"] = "done" if ok else "failed"
            if not ok:
                e["why"] = "validation"
            save()
    save()
    print("all edges processed")


def emit_inl(st, path):
    """Emit src/world_map_routenet.inl: data tables only (code is handwritten
    in the same file below the GENERATED marker -- regeneration replaces the
    data section, see gen_routenet.py)."""
    nodes = sorted(NODES.keys())
    nidx = {n: i for i, n in enumerate(nodes)}
    pts_flat = []
    edges = []
    for a, b in EDGES:
        key = "%s|%s" % (a, b)
        e = st["edges"].get(key)
        if not e or e.get("stage") != "done":
            continue
        off = len(pts_flat)
        for p in e["pts"]:
            pts_flat.append((p[0], p[1]))
        edges.append((nidx[a], nidx[b], off, len(e["pts"]), e["len"],
                      e["validation"]["fwd"]["frames"],
                      e["validation"]["rev"]["frames"]))
    L = []
    L.append("// world_map_routenet.inl -- GENERATED by offline/gen_routenet.py (v0.18.3.211).")
    L.append("// Precomputed VALIDATED route network: nodes = decoded entry aims; edges =")
    L.append("// exact max-clearance polylines validated by the BAT208 stateful engine")
    L.append("// replica + full executor simulation in BOTH directions (offline/ROUTENET.md).")
    L.append("// DATA SECTION (regenerate with: python3 offline/gen_routenet.py ... --emit).")
    L.append("// v0.18.3.211: the Galbadia pass edge is GONE (proven impassable in the .210")
    L.append("// live BAT); Timber/Dollet traffic routes east over the split .209")
    L.append("// Dollet<->Timber corridor via two new junctions (Yaulny Plains at the")
    L.append("// mountain-belt crossing NE of G-Garden, Hasberry Plains at the Dollet")
    L.append("// mouth outside Dollet's firing bbox). Full matrix 20/20 arrived.")
    L.append("")
    L.append("static const int ROUTENET_NODE_COUNT = %d;" % len(nodes))
    L.append("static const char* const s_rnNodeNames[%d] = {" % len(nodes))
    for n in nodes:
        L.append('    "%s",' % n)
    L.append("};")
    L.append("static const int32_t s_rnNodeXY[%d][2] = {" % len(nodes))
    for n in nodes:
        L.append("    { %d, %d }," % NODES[n])
    L.append("};")
    L.append("")
    L.append("// edges: nodeA, nodeB, ptOffset, ptCount, lenUnits, simFramesFwd, simFramesRev")
    L.append("static const int ROUTENET_EDGE_COUNT = %d;" % len(edges))
    L.append("static const int32_t s_rnEdges[%d][7] = {" % max(1, len(edges)))
    for e in edges:
        L.append("    { %d, %d, %d, %d, %d, %d, %d }," % e)
    L.append("};")
    L.append("")
    L.append("// packed polyline points (world coords), %d points" % len(pts_flat))
    L.append("static const int ROUTENET_PT_COUNT = %d;" % len(pts_flat))
    L.append("static const int32_t s_rnPts[%d][2] = {" % max(1, len(pts_flat)))
    row = []
    for (x, z) in pts_flat:
        row.append("{%d,%d}" % (x, z))
        if len(row) == 6:
            L.append("    " + ",".join(row) + ",")
            row = []
    if row:
        L.append("    " + ",".join(row) + ",")
    L.append("};")
    open(path, "w").write("\n".join(L) + "\n")
    print("emitted", path, ":", len(nodes), "nodes,", len(edges), "edges,",
          len(pts_flat), "points")


if __name__ == "__main__":
    main()
