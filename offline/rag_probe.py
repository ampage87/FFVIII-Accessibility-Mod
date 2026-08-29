#!/usr/bin/env python3
"""Look at the LANDABLE ground around a point, without the foot-walkable rule.

The three destinations the generic rule cannot serve -- Fisherman's Horizon,
the Deep Sea Research Center and Esthar -- are exactly the three Aaron named as
places the Ragnarok lands ON rather than beside. The generic rule requires the
landing cell to be foot-walkable so the player can step off and walk; these are
platforms where landing IS the arrival, so that requirement is the wrong one.
"""
import sys, json
import numpy as np
import gen_rag_landings as G

def probe(landable, foot, have, elev, name, gx, gy, radius_cells=32):
    c0, r0 = G.raw_to_cell(gx, gy)
    print('=== %s  marker (%d,%d) cell (%d,%d)' % (name, gx, gy, c0, r0))
    hits = []
    for dr in range(-radius_cells, radius_cells+1):
        for dc in range(-radius_cells, radius_cells+1):
            c, r = c0+dc, r0+dr
            if not (0 <= c < G.COLS and 0 <= r < G.ROWS): continue
            if not landable[r][c]: continue
            wx, wy = G.cell_to_raw(c, r)
            d = int(round(((wx-gx)**2 + (wy-gy)**2) ** 0.5))
            hits.append((d, wx, wy, bool(foot[r][c]), int(elev[r][c])))
    hits.sort()
    print('  %d landable cells within %d units' % (len(hits), radius_cells*G.CELL))
    for d, wx, wy, f, h in hits[:12]:
        print('   %6d u  (%7d,%7d)  foot=%-5s elev=%d' % (d, wx, wy, f, h))
    if hits:
        # how big is the landable patch (4-connected) containing the nearest hit?
        seen = set(); from collections import deque
        s = G.raw_to_cell(hits[0][1], hits[0][2]); q = deque([s]); seen.add(s)
        while q:
            c, r = q.popleft()
            for dc, dr in ((1,0),(-1,0),(0,1),(0,-1)):
                n = (c+dc, r+dr)
                if n in seen: continue
                if not (0 <= n[0] < G.COLS and 0 <= n[1] < G.ROWS): continue
                if not landable[n[1]][n[0]]: continue
                seen.add(n); q.append(n)
        print('  nearest landable patch: %d cells (%d x %d units each)'
              % (len(seen), G.CELL, G.CELL))
        xs = [G.cell_to_raw(*p)[0] for p in seen]; ys = [G.cell_to_raw(*p)[1] for p in seen]
        print('  patch bbox x[%d..%d] y[%d..%d]  centre (%d,%d)'
              % (min(xs), max(xs), min(ys), max(ys), (min(xs)+max(xs))//2, (min(ys)+max(ys))//2))
    print()

if __name__ == '__main__':
    landable, foot, have, elev = G.build_grids(sys.argv[1] if len(sys.argv)>1 else '/home/claude/_scratch/wm/wmx.obj')
    for name, gx, gy in [("Fisherman's Horizon", 20480, -2560),
                         ("Deep Sea Research Center", -119138, 86000),
                         ("Esthar City", 57011, -2295)]:
        probe(landable, foot, have, elev, name, gx, gy)
