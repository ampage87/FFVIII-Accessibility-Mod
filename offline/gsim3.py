"""Garden planner/executor model v3 -- CLIFF-AWARE.

v0.20.60 BAT: the hull drove WSW out of Balamb and jammed against the cliff
coastline of the Galbadia continent at (-5127,-5196). F11 screenshots show it
pressed against a two-tier cliff with the rail line on top; the log shows it
pressing forward and not moving for 90 s.

The v0.20.57 derivation ("a 256u cell is four 64u move steps, so the gate is
4 x 200 = 800") is right for a RAMP and wrong for a CLIFF. The engine tests
|dH| >= 200 between the polygon under the hull and the polygon one move step
away; at a cliff that whole drop happens in ONE step no matter how the grid is
quantised. Averaging four sub-heights into one cell height smears the cliff
away entirely, which is why the planner routed straight into a coastline.

Model here:
  * sub-points are the 128u grid -- exactly what the C++ rasterizer samples
  * a 256u planner cell is traversable iff all four sub-points are Garden-
    walkable AND no internal 4-neighbour sub-pair differs by >= 200
  * an edge between adjacent planner cells is open iff at least ONE of the two
    sub-point crossings differs by < 200 (half the boundary being climbable is
    enough for a hull to pass)

Consistency with both BATs: at the Centra shelf (v0.20.56) the per-cell steps
were 0/107/166/85/48/62/32/17, so no sub-pair is anywhere near 200 and the
shelf stays open. At the Galbadia cliff the sub-pairs are 227/249/251 and the
edge closes.
"""
import numpy as np, heapq, math

GATE = 200
CP = 256
RP, KP = 768, 1024

_z = np.load("/root/work/wmgrid.npz")
B15 = _z['b15']; HGT = _z['hgt'].astype(np.int32)
GW8 = (B15 & 0x20) != 0
# v0.20.73: the Garden's TERRAIN WHITELIST, from FF8_EN.exe 0x53E3C1 inside the
# movement validator 0x53E2A0. For vehicle 0x30 only, a candidate polygon must
# have 30 <= terrain <= 34 unless the condition at [0x203EE88] lifts it. Water
# (32/33/34) sits at height 0; land (6/7/9/29) at -200..-1100. So the hull may
# only leave the sea where ALLOWED terrain rises above sea level -- a beach --
# and there are just 4,921 such cells map-wide, 0.18% of the Garden's area.
TERR = _z['terr']
WATER8 = (TERR >= 30) & (TERR <= 34)
SHELF8 = (TERR >= 30) & (TERR <= 32)   # v0.20.84: the shallow shelf
PK8 = (B15 & 0x02) != 0
FT8 = (B15 & 0x80) != 0

_g4 = GW8.reshape(RP, 2, KP, 2)
_h4 = HGT.reshape(RP, 2, KP, 2)


def _sub(r, c):
    return _h4[:, r, :, c]


_internal = ((np.abs(_sub(0, 0) - _sub(0, 1)) < GATE) &
             (np.abs(_sub(1, 0) - _sub(1, 1)) < GATE) &
             (np.abs(_sub(0, 0) - _sub(1, 0)) < GATE) &
             (np.abs(_sub(0, 1) - _sub(1, 1)) < GATE))
WALK = _g4.all(axis=(1, 3)) & _internal
# v0.20.78: MIRROR world_garden_grid.inl EXACTLY. The parity check found two
# silent divergences -- the same class of fault that shipped v0.20.67 broken and
# that I documented and then repeated:
#   * PARK was "all four sub-points carry the disembark bit"; the C++ has been
#     "ANY sub-point that is BOTH disembark-flagged AND foot-walkable" since
#     v0.20.77.  65,035 vs 74,184 cells -- a 14% disagreement.
#   * HP was the MEAN of the four sub-heights; the C++ uses (min+max)/2, which
#     moved 68 of only ~1,250 BEACH cells. Beaches are the scarce resource that
#     decides whether a landmass is reachable at all, so 5% of them is a lot.
PKp  = (PK8 & FT8).reshape(RP, 2, KP, 2).any(axis=(1, 3))
FTp  = FT8.reshape(RP, 2, KP, 2).any(axis=(1, 3))
HP   = ((_h4.min(axis=(1, 3)).astype(np.int32) +
         _h4.max(axis=(1, 3)).astype(np.int32)) / 2).astype(np.int32)
WATERp = WATER8.reshape(RP, 2, KP, 2).all(axis=(1, 3))     # every sub-point allowed
# v0.20.84: BEACH IS THE SHELF, NOT "WATER THAT SITS ABOVE ZERO".
# [0x203EE88] is the Garden's own altitude (0x54B49F writes the entity position
# there as x,-z,y), and the validator refuses land while it is positive. From
# 296 [GDTRACE] samples of the v0.20.83 BAT: over terrain 32 the altitude is
# -254..-58 in 46/46 samples; over 33/34 it is +208/+210 in 126/127. The mesh
# height is 0 on all three, so the old HP<0 rule was a proxy that coincided at
# the Tomb and nowhere on Balamb's coast -- 1,285 beaches, none of them there,
# which made Balamb island a one-way trap.
BEACHp = WATERp & SHELF8.reshape(RP, 2, KP, 2).any(axis=(1, 3))

# v0.20.95: 1..3 of the four sub-points Garden-masked. Only a beach_climb
# approach may cross such a cell (GdBeachOpen), and the .94 Shumi survey is why:
# planner (705,521) is three-quarters Garden ground and one quarter unmasked
# shore skirt, the engine had the hull standing on it, and the model called it
# solid -- which shut the only beach onto that island.
_g4any = GW8.reshape(RP, 2, KP, 2).any(axis=(1, 3))
PARTIALp = _g4any & ~GW8.reshape(RP, 2, KP, 2).all(axis=(1, 3))

EOK = ((np.abs(_sub(0, 1) - np.roll(_sub(0, 0), -1, axis=1)) < GATE) |
       (np.abs(_sub(1, 1) - np.roll(_sub(1, 0), -1, axis=1)) < GATE))
SOK = ((np.abs(_sub(1, 0) - np.roll(_sub(0, 0), -1, axis=0)) < GATE) |
       (np.abs(_sub(1, 1) - np.roll(_sub(0, 1), -1, axis=0)) < GATE))
EOK &= WALK & np.roll(WALK, -1, axis=1)
# v0.20.69: the world map is a torus in BOTH axes. wdist/wdy have always
# wrapped in y, but the grid did not -- the last row's south edge was hard-wired
# shut, so no route could ever go around the pole. Aaron: "make sure the route
# planner understands that you can go around the world... you go down around the
# southern pole." The seam is now a normal edge.
SOK &= WALK & np.roll(WALK, -1, axis=0)

import scipy.ndimage as ndi
# v0.20.85: the clearance field wraps in BOTH axes, like everything else since
# v0.20.69. scipy's transform treats the array edge as background, so the polar
# band came out open here and the C++ came out walled -- the two errors happened
# to point opposite ways, and the v0.20.84 replay caught the pair. Pad by the
# cap with wrap mode, transform, crop.
_PADC = 33
CLEAR = np.minimum(
    ndi.distance_transform_cdt(np.pad(WALK, _PADC, mode='wrap'), metric='chessboard'),
    32)[_PADC:-_PADC, _PADC:-_PADC].astype(np.uint8)
CLEAR_TARGET = 7
CLEAR_PENALTY = 0.55
# v0.20.67: the Garden's mask admits a lot of LAND, and the planner cheerfully
# routed 66 of 67 waypoints overland across Centra -- where the engine then
# refused to climb the coastal escarpment and the hull stalled at the water's
# edge. The mask is not the whole story: getting ONTO that land needs a step the
# 200 gate forbids. Rather than ban land (every berth is a coastal cell, so that
# loses all 26), make it expensive: routes then run on open water and only touch
# the shore at the endpoints, which is how the Garden is actually driven.
LAND_PENALTY = 2.5
# v0.20.81: A SURCHARGE ON CORRIDORS THE EXECUTOR CANNOT DRIVE.
#
# The .79 Tomb route had 30 of 340 waypoints at clearance <= 2, and its three
# longest tight stretches -- (-31360,-55424), (-50304,-37504), (-45184,-35456) --
# are exactly where the game logged its wall-follows. 25% of that drive was spent
# inside the guard. Aaron: "It does not have to take the most direct route, it is
# perfectly fine to take a bit longer to take a cleaner / more open path."
#
# A flat surcharge of 4 on clearance <= 2 drops the tight waypoints from 30 to 1
# and costs 108 km -> 134 km. Larger values buy nothing, so 4 it is. The single
# survivor is the destination approach itself: the Tomb of the Unknown King sits
# on the TIP OF A PENINSULA, which is narrow by construction, and the existing
# nearGoal exemption is what lets the route reach it at all.
TIGHT_CLEAR = 2
TIGHT_PENALTY = 4.0


def g2cp(gx, gz):
    return ((0x48000 - int(gz)) % 0x30000) // CP, ((int(gx) + 0x60000) % 0x40000) // CP


def cp2g(r, c):
    mx = int(c) * CP + CP // 2
    mz = int(r) * CP + CP // 2
    return ((mx - 0x60000 + 0x20000) % 0x40000 - 0x20000,
            (0x48000 - mz + 0x18000) % 0x30000 - 0x18000)


def wdx(a, b):
    d = b - a
    return d - 262144 if d > 131072 else (d + 262144 if d < -131072 else d)


def wdy(a, b):
    d = b - a
    return d - 196608 if d > 98304 else (d + 196608 if d < -98304 else d)


def wdist(ax, ay, bx, by):
    return math.hypot(wdx(ax, bx), wdy(ay, by))


def bearing(ax, ay, bx, by):
    return int(math.atan2(wdx(ax, bx), -wdy(ay, by)) / (2 * math.pi) * 4096) & 0xFFF


def edge_open(r, c, nr, nc):
    """Is the step from cell (r,c) to the 4-neighbour (nr,nc) allowed? Torus."""
    r %= RP; nr %= RP; c %= KP; nc %= KP
    if nr == r:
        if nc == (c + 1) % KP:
            return bool(EOK[r, c])
        if c == (nc + 1) % KP:
            return bool(EOK[r, nc])
        return False
    if nc == c:
        if nr == (r + 1) % RP:
            return bool(SOK[r, c])
        if r == (nr + 1) % RP:
            return bool(SOK[nr, c])
    return False


def transition_ok(r, c, nr, nc):
    """v0.20.73: leaving the sea is only legal from a beach. Mirrors
    GdTransitionOk in world_garden_grid.inl -- keep the two identical."""
    if not WATERp[r, c]:
        return True
    if WATERp[nr, nc]:
        return True
    return bool(BEACHp[r, c])


def step_open(r, c, dr, dc):
    """Allow a diagonal only if one of the two L-shaped routes is open. Torus."""
    r %= RP; c %= KP
    nr, nc = (r + dr) % RP, (c + dc) % KP
    if not WALK[nr, nc]:
        return False
    if not transition_ok(r, c, nr, nc):
        return False
    if dr == 0 or dc == 0:
        return edge_open(r, c, nr, nc)
    if (WALK[r, nc] and transition_ok(r, c, r, nc) and transition_ok(r, nc, nr, nc)
            and edge_open(r, c, r, nc) and edge_open(r, nc, nr, nc)):
        return True
    if (WALK[nr, c] and transition_ok(r, c, nr, c) and transition_ok(nr, c, nr, nc)
            and edge_open(r, c, nr, c) and edge_open(nr, c, nr, nc)):
        return True
    return False


NB = [(-1, 0, 1.0), (1, 0, 1.0), (0, -1, 1.0), (0, 1, 1.0),
      (-1, -1, 1.4142), (-1, 1, 1.4142), (1, -1, 1.4142), (1, 1, 1.4142)]


def snapp(r0, c0, rad=30):
    if WALK[r0, c0 % KP]:
        return r0, c0 % KP
    for k in range(1, rad + 1):
        for dr in range(-k, k + 1):
            for dc in range(-k, k + 1):
                if max(abs(dr), abs(dc)) != k:
                    continue
                r = (r0 + dr) % RP; c = (c0 + dc) % KP
                if WALK[r, c]:
                    return r, c
    return None


def plan(sx, sy, tx, ty, cap=1500000, clear_target=None):
    s = snapp(*g2cp(sx, sy)); g = snapp(*g2cp(tx, ty))
    if s is None or g is None:
        return None
    sr, sc = s; gr, gc = g
    # v0.20.69: unwrap the goal relative to the start in BOTH axes, and let node
    # coordinates run unbounded, mapping to the grid by modulo. The column axis
    # has always worked this way; the row axis did not, which is why no route
    # could go over a pole even though wdist has always measured that way.
    d0 = gc - sc
    if d0 > KP // 2: d0 -= KP
    if d0 < -KP // 2: d0 += KP
    gcu = sc + d0
    d0r = gr - sr
    if d0r > RP // 2: d0r -= RP
    if d0r < -RP // 2: d0r += RP
    gru = sr + d0r

    def h(r, c):
        dr = abs(r - gru); dc = abs(c - gcu)
        return (dr + dc) + (1.4142 - 2.0) * min(dr, dc)

    start = (sr, sc); goal = (gru, gcu)
    G = {start: 0.0}; came = {}; hp = [(h(*start), 0, start)]; tie = 0; exp = 0
    while hp:
        _, _, cur = heapq.heappop(hp)
        if cur == goal:
            path = [cur]
            while cur in came:
                cur = came[cur]; path.append(cur)
            path.reverse()
            return [cp2g(r % RP, c % KP) for r, c in path]
        exp += 1
        if exp > cap:
            return None
        r, c = cur; base = G[cur]
        nearGoal = ((abs(r - gru) <= 10 and abs(c - gcu) <= 10) or
                    (abs(r - sr) <= 10 and abs(c - sc) <= 10))
        for dr, dc, w in NB:
            nr = r + dr; nc = c + dc
            if not step_open(r % RP, c % KP, dr, dc):
                continue
            ncw = nc % KP; nrw = nr % RP
            pen = 0.0
            if not nearGoal:
                miss = (CLEAR_TARGET if clear_target is None else clear_target) - int(CLEAR[nrw, ncw])
                if miss > 0:
                    pen = CLEAR_PENALTY * miss
                # v0.20.81: WATER channels only. Every wall-follow disaster in
                # the .79 Tomb log was a narrow SEA channel being coast-followed.
                # Fire Cavern's route is 16 of 41 waypoints tight and it is a
                # short INLAND run that demonstrably drives fine in game -- the
                # hull has no alternative there, so charging for it risks a
                # regression on a working destination for no gain.
                if int(CLEAR[nrw, ncw]) <= TIGHT_CLEAR:
                    pen += TIGHT_PENALTY
            # Many berths ARE land cells -- every walk=0 berth is one, the hull
            # parks on the destination -- so the penalty must lift close to the
            # endpoints or those approaches get distorted. Six cells, well under
            # the ten the clearance term uses: enough to reach a land berth,
            # nowhere near enough to buy an overland shortcut.
            nearEnd = ((abs(r - gru) <= 6 and abs(c - gcu) <= 6) or
                       (abs(r - sr) <= 6 and abs(c - sc) <= 6))
            mult = 1.0 if nearEnd else (LAND_PENALTY if FTp[nrw, ncw] else 1.0)
            ng = base + w * (1.0 + pen) * mult
            if ng < G.get((nr, nc), 1e30):
                G[(nr, nc)] = ng; came[(nr, nc)] = cur; tie += 1
                heapq.heappush(hp, (ng + h(nr, nc), tie, (nr, nc)))
    return None


def decimate(path, pitch=256):
    out = [path[0]]
    for p in path[1:-1]:
        if wdist(out[-1][0], out[-1][1], p[0], p[1]) >= pitch:
            out.append(p)
    out.append(path[-1])
    return out
