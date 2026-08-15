#!/usr/bin/env python3
"""GENERATE THE GARDEN BERTH TABLE -- all the constraints, in one place.

Every previous regeneration applied a SUBSET of the rules and re-broke whatever
the missing one protected. v0.20.79 dropped the same-landmass check and shipped
Shumi Village a berth 4,471 units away across open water, so the mod told a
blind player to walk somewhere he physically cannot walk. v0.20.83 restored the
2-3 km standoff Aaron asked for and dropped it again.

v0.20.98 added the fifth and sixth, after the .97 BAT: the Garden parked exactly
where it was told and the WALK then ran at the blind face of the building. A
destination is a door, not a footprint, and a berth has an approach side.

So the six constraints are written out here, together, once:

  1. GARDEN-REACHABLE   the hull cell is in the 256-grid flood the planner uses,
                        under the same GdTransitionOk beach rule as the C++.
  2. ASHORE + STEP-OFF  the cell is NOT water and carries a sub-point that is
                        both disembark-flagged and foot-walkable. The Garden
                        parks on land; the player walks off it.
  3. SAME FOOT LANDMASS the step-off is foot-connected to the destination
                        marker. A berth on another island is not a berth.
  4. 2-3 KM STANDOFF    Aaron, after the .82 BAT: "it stopped essentially on top
                        of the destination so the Garden could not land. It
                        should stop 2-3km away from the destination on land so
                        the player can get off the Garden." Within the band,
                        prefer the most open ground (highest clearance).
  5. MARKER IS WALKABLE the destination marker is itself foot-walkable under the
                        ENGINE's step gate (200 over one move), not merely near
                        walkable ground. .97 aimed at the centre of the Shumi
                        dome, which is not walkable at all, so the planner
                        snapped to whatever was nearest and sent the player at
                        the back wall.
  6. APPROACH CORRIDOR  ADVISORY ONLY -- reported, never scored. The last
                        FINAL_ZONE units of the step-off -> marker line should
                        stay inside the marker's gate-200 foot component,
                        because the executor beelines once inside that zone.
                        It is advisory because a normal town's field trigger
                        fires well before you touch the building, so a blocked
                        last segment is harmless there: audited against the
                        shipped table it flags Balamb Town, Timber and Chocobo
                        Forest 2, all of which Aaron has arrived at. It earns a
                        vote only when something else says the approach matters.

Fallback when the band is empty on that landmass: the nearest qualifying cell
within WALK_CAP. No berth at all is reported as no berth -- a destination the
Garden cannot serve is hidden from its catalog rather than driven at.

Usage:  python3 offline/gen_berths.py [out.json]
"""
import sys
import math
import json
import re
from collections import deque

sys.path.insert(0, '/root/work')
import numpy as np                                                   # noqa: E402
import gsim3 as G                                                    # noqa: E402

FINE = 128
FOOT_GATE = 800          # smoothed: foot move step 0x20 = 32u, 4 steps per 128 cell
ENGINE_GATE = 200        # the navmesh gate itself -- ">=200 over ~190u" at load
FINAL_ZONE = 1200.0      # the executor beelines inside ~1000u; sample a little wider
BAND_LO, BAND_HI = 2000.0, 3000.0
BAND_IDEAL = 2500.0
WALK_CAP = 6000.0
GARDEN_START = (20271, -24355)   # the hull's cell in the BAT save

_z = np.load('/root/work/wmgrid.npz')
TER = _z['terr']
HGT = _z['hgt'].astype(np.int32)
B15 = _z['b15']
RF, CF = TER.shape                       # 1536 x 2048 at 128 units
FOOT = (B15 & 0x80) != 0
PARK = (B15 & 0x02) != 0

N4 = ((1, 0), (-1, 0), (0, 1), (0, -1))


def g2f(gx, gz):
    return ((0x48000 - int(gz)) % 0x30000) // FINE, ((int(gx) + 0x60000) % 0x40000) // FINE


def fdist(ar, ac, br, bc):
    dr = min((ar - br) % RF, (br - ar) % RF)
    dc = min((ac - bc) % CF, (bc - ac) % CF)
    return math.hypot(dr * FINE, dc * FINE)


def flood_foot(sr, sc, gate=None):
    gate = FOOT_GATE if gate is None else gate
    seen = np.zeros((RF, CF), bool)
    seen[sr, sc] = True
    q = deque([(sr, sc)])
    while q:
        r, c = q.popleft()
        h = int(HGT[r, c])
        for dr, dc in N4:
            nr = (r + dr) % RF
            nc = (c + dc) % CF
            if seen[nr, nc] or not FOOT[nr, nc]:
                continue
            if abs(int(HGT[nr, nc]) - h) >= gate:
                continue
            seen[nr, nc] = True
            q.append((nr, nc))
    return seen


def f2g(r, c):
    x = (c * FINE + FINE // 2 - 0x60000 + 0x20000) % 0x40000 - 0x20000
    z = (0x48000 - (r * FINE + FINE // 2) + 0x18000) % 0x30000 - 0x18000
    return x, z


def corridor_clear(sr, sc, mr, mc, land):
    """Constraint 6, ADVISORY: is the FINAL APPROACH ZONE walkable?

    Only the last FINAL_ZONE units matter -- outside it the executor is
    following an A* route, not a sight-line, so a straight line that clips a
    building 2 km out means nothing. Sampled every half cell.
    """
    ax, ay = f2g(sr, sc)
    bx, by = f2g(mr, mc)
    dx, dy = G.wdx(ax, bx), G.wdy(ay, by)
    d = math.hypot(dx, dy)
    if d < 1.0:
        return True
    t0 = max(0.0, 1.0 - FINAL_ZONE / d)
    n = int(FINAL_ZONE // (FINE // 2)) + 1
    for i in range(n + 1):
        t = t0 + (1.0 - t0) * i / n
        r, c = g2f(ax + dx * t, ay + dy * t)
        if not land[r, c]:
            return False
    return True


def flood_garden():
    """The planner's own 256-grid reachability, via gsim3.step_open."""
    r0, c0 = G.snapp(*G.g2cp(*GARDEN_START))
    seen = np.zeros((G.RP, G.KP), bool)
    seen[r0, c0] = True
    q = deque([(r0, c0)])
    while q:
        r, c = q.popleft()
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                if dr == 0 and dc == 0:
                    continue
                nr = (r + dr) % G.RP
                nc = (c + dc) % G.KP
                if seen[nr, nc]:
                    continue
                if G.step_open(r, c, dr, dc):
                    seen[nr, nc] = True
                    q.append((nr, nc))
    return seen


def sub_cells(pr, pc):
    """The four 128-unit sub-cells of planner cell (pr, pc)."""
    return [(pr * 2 + a, pc * 2 + b) for a in (0, 1) for b in (0, 1)]


def main(out_path='/root/work/berths_new.json'):
    reach = flood_garden()
    print(f"Garden-reachable planner cells: {int(reach.sum()):,}"
          f"  (beaches: {int(G.BEACHp.sum()):,})\n")

    # Candidate hull cells: reachable, ashore, with a real step-off sub-point.
    cand = reach & ~G.WATERp & G.WALK
    stepoff = PARK & FOOT
    step_any = stepoff.reshape(G.RP, 2, G.KP, 2).any(axis=(1, 3))
    cand &= step_any
    print(f"candidate berth cells (reachable, ashore, step-off): {int(cand.sum()):,}\n")
    ys, xs = np.nonzero(cand)

    src = open('/root/work/src/world_catalog.inl').read()
    cat = [(m[0], int(m[1]), int(m[2]))
           for m in re.findall(r'\{"([^"]+)",\s*(-?\d+),\s*(-?\d+)\}', src)]

    out = []
    for name, mx, mz in cat:
        mr, mc = g2f(mx, mz)
        if not FOOT[mr, mc]:
            best = None
            for dr in range(-6, 7):
                for dc in range(-6, 7):
                    r = (mr + dr) % RF
                    c = (mc + dc) % CF
                    if FOOT[r, c]:
                        d = math.hypot(dr * FINE, dc * FINE)
                        if best is None or d < best[0]:
                            best = (d, r, c)
            if best is None:
                out.append(dict(name=name, park=[0, 0], walk=0, ok=False,
                                why='marker not foot-walkable'))
                print(f"  {name:26s} marker not walkable at all")
                continue
            mr, mc = best[1], best[2]
            # CONSTRAINT 5. Snapping is a fallback, not a silent one: a marker
            # that has to be moved is a marker aimed at a building rather than
            # at its door, and .97 shipped exactly that.
            print(f"  {name:26s} !! marker not foot-walkable, snapped "
                  f"{int(best[0])}u to {f2g(mr, mc)}")
        land = flood_foot(mr, mc)
        # CONSTRAINT 6 works on the ENGINE's step gate, not the smoothed one:
        # the navmesh blocks a connection at >=200 over one move, and a corridor
        # that is only open to the 800 gate is not open to the player.
        land200 = flood_foot(mr, mc, ENGINE_GATE)

        best = None          # (score, walk, pr, pc, clear)
        for k in range(len(ys)):
            pr, pc = int(ys[k]), int(xs[k])
            # cheap reject on planner-cell distance before the sub-cell scan
            gx, gy = G.cp2g(pr, pc)
            d = G.wdist(gx, gy, mx, mz)
            if d > WALK_CAP + 512:
                continue
            for sr, sc in sub_cells(pr, pc):
                if not (stepoff[sr, sc] and land[sr, sc]):
                    continue
                walk = fdist(sr, sc, mr, mc)
                if walk > WALK_CAP:
                    continue
                clear = int(G.CLEAR[pr, pc])
                # A blocked corridor is a worse berth than a badly-spaced one:
                # it is the difference between a long walk and a wall.
                corr = 0 if (land200[sr, sc]
                             and corridor_clear(sr, sc, mr, mc, land200)) else 1
                if BAND_LO <= walk <= BAND_HI:
                    score = (0, abs(walk - BAND_IDEAL) - clear * 40.0)
                else:
                    score = (1, walk)
                if best is None or score < best[0]:
                    best = (score, walk, pr, pc, clear, corr)
                break

        if best is None:
            out.append(dict(name=name, park=[0, 0], walk=0, ok=False,
                            why="no berth on the destination's own landmass"))
            print(f"  {name:26s} no berth on the destination's own landmass")
            continue
        score, walk, pr, pc, clear, corr = best
        gx, gy = G.cp2g(pr, pc)
        note = '' if score[0] == 0 else '  (no land in the 2-3 km band on that landmass)'
        if corr:
            note += '  (advisory: final-approach corridor crosses ground the engine gate blocks)'
        out.append(dict(name=name, park=[int(gx), int(gy)], walk=int(round(walk)),
                        ok=True, clear=clear, corridor=(corr == 0)))
        print(f"  {name:26s} ({gx:7d},{gy:7d}) walk {int(round(walk)):5d} clear={clear:2d}{note}")

    json.dump(out, open(out_path, 'w'), indent=1)
    print(f"\n{sum(1 for o in out if o['ok'])} of {len(out)} have a valid berth")
    print(f"written to {out_path}")
    return 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
