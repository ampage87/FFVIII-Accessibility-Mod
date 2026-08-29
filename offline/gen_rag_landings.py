#!/usr/bin/env python3
"""GENERATE THE RAGNAROK LANDING TABLE.

Aaron, 2026-08-25: *"For most locations Ragnarok should stop as close as
possible where it can land and within walking distance of the destination. For
those locations that Ragnarok can land on such as FH, Deep Sea Research center,
and Esthar Airstation, the Ragnarok should drive to the spot to land on/at
these locations."*

The Ragnarok is the easiest vehicle on the world map and the hardest to place.
Easiest because it has NO movement mask at all -- `0x53E6B0` falls through to
`return 1` for vehicle 0x32, so it flies over everything and the planner has
nothing to route around. Hardest because the only question the map asks about it
is where it may SET DOWN, and that is a different mask on a different byte:
`0x53E730` reads `(typeTriple >> 8) & 0x80`, i.e. **poly[14] bit 7**.

So this generator answers one question per destination: what is the closest
point the Ragnarok may land that the player can then WALK to the destination
from?

  1. LANDABLE      poly[14] bit 7, the engine's own set-down bit.
  2. STEP-OFF      the same cell is foot-walkable (poly[15] bit 7). 99.5% of
                   landable polygons already are; the half-percent that is not
                   would strand him on a ledge.
  3. SAME LANDMASS the cell is foot-connected to the destination marker under
                   the engine's own step gate (|dH| >= 200 refuses, five call
                   sites, `0x53E56B` among them). A landing on the far side of a
                   strait is not a landing.
  4. AS CLOSE AS POSSIBLE. This is where the Ragnarok differs from the Garden,
                   whose berths are deliberately held 2-3 km off because the
                   hull is enormous and cannot approach a town. The Ragnarok
                   sets down in a field; the nearest qualifying cell wins.

Usage:  python3 offline/gen_rag_landings.py [wmx.obj] [out.json]
"""
import json
import struct
import sys
from collections import deque

import numpy as np

SEG_COLS, SEG_ROWS = 32, 24
SEGMENT_SIZE   = 36864
BLOCKS_PER_SEG = 16
BLOCK_HDR      = 4
POLY_SIZE      = 16
PLAYABLE_SEGS  = SEG_COLS * SEG_ROWS

CELL      = 256                       # planner scale, four Ragnarok move steps
COLS      = SEG_COLS * 8192 // CELL    # 1024
ROWS      = SEG_ROWS * 8192 // CELL    # 768
OFF_X     = 131072                     # game X = raw X - OFF_X
OFF_Y     = 98304
STEP_GATE = 200                        # the engine's own height refusal


def raw_to_cell(gx, gy):
    return (gx + OFF_X) // CELL, (gy + OFF_Y) // CELL

def cell_to_raw(c, r):
    return c * CELL - OFF_X + CELL // 2, r * CELL - OFF_Y + CELL // 2


def build_grids(path):
    """Rasterise wmx.obj into landable / foot / elevation grids at CELL units."""
    data = open(path, 'rb').read()
    landable = np.zeros((ROWS, COLS), np.bool_)
    foot     = np.zeros((ROWS, COLS), np.bool_)
    have     = np.zeros((ROWS, COLS), np.bool_)
    elev     = np.zeros((ROWS, COLS), np.int32)
    nseg = min(len(data) // SEGMENT_SIZE, PLAYABLE_SEGS)
    for seg in range(nseg):
        row, col = seg // SEG_COLS, seg % SEG_COLS
        base = seg * SEGMENT_SIZE
        for b in range(BLOCKS_PER_SEG):
            off = struct.unpack_from('<I', data, base + 4 + b * 4)[0]
            if off == 0 or off + BLOCK_HDR > SEGMENT_SIZE: continue
            bb = base + off
            polyCount, vertCount = data[bb], data[bb + 1]
            polyEnd = off + BLOCK_HDR + polyCount * POLY_SIZE
            if polyEnd > SEGMENT_SIZE: continue
            vertBase = bb + BLOCK_HDR + polyCount * POLY_SIZE
            if polyEnd + vertCount * 8 > SEGMENT_SIZE: continue
            ox = col * 8192 + (b % 4) * 2048
            oy = row * 8192 + (b // 4) * 2048
            vx = [0]*vertCount; vy = [0]*vertCount; vz = [0]*vertCount
            for v in range(vertCount):
                lvx, lvz, lvy = struct.unpack_from('<hhh', data, vertBase + v*8)
                vx[v] = ox + lvx; vz[v] = lvz; vy[v] = oy - lvy
            for p in range(polyCount):
                pb = bb + BLOCK_HDR + p * POLY_SIZE
                i0, i1, i2 = data[pb], data[pb+1], data[pb+2]
                if i0 >= vertCount or i1 >= vertCount or i2 >= vertCount: continue
                terrain = data[pb + 0x0D]
                if 32 <= terrain <= 34: continue                  # ocean
                isLand = (data[pb + 0x0E] & 0x80) != 0
                isFoot = (data[pb + 0x0F] & 0x80) != 0
                ax, ay = vx[i0], vy[i0]; bx, by = vx[i1], vy[i1]; cx, cy = vx[i2], vy[i2]
                h = (vz[i0] + vz[i1] + vz[i2]) // 3
                c0 = max(0, min(ax,bx,cx)//CELL); c1 = min(COLS-1, max(ax,bx,cx)//CELL)
                r0 = max(0, min(ay,by,cy)//CELL); r1 = min(ROWS-1, max(ay,by,cy)//CELL)
                for rr in range(r0, r1+1):
                    py = rr*CELL + CELL//2
                    for cc in range(c0, c1+1):
                        px = cc*CELL + CELL//2
                        d1 = (px-bx)*(ay-by) - (ax-bx)*(py-by)
                        d2 = (px-cx)*(by-cy) - (bx-cx)*(py-cy)
                        d3 = (px-ax)*(cy-ay) - (cx-ax)*(py-ay)
                        if ((d1<0)or(d2<0)or(d3<0)) and ((d1>0)or(d2>0)or(d3>0)): continue
                        if not have[rr][cc]:
                            have[rr][cc] = True
                            elev[rr][cc] = h
                        if isLand: landable[rr][cc] = True
                        if isFoot: foot[rr][cc]     = True
    return landable, foot, have, elev


def foot_component(foot, elev, start):
    """Flood the foot-walkable cells reachable from `start` under the step gate."""
    seen = np.zeros(foot.shape, np.bool_)
    sc, sr = start
    if not foot[sr][sc]: return seen
    seen[sr][sc] = True
    q = deque([(sc, sr)])
    while q:
        c, r = q.popleft()
        h = elev[r][c]
        for dc, dr in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
            nc, nr = c+dc, r+dr
            if not (0 <= nc < COLS and 0 <= nr < ROWS): continue
            if seen[nr][nc] or not foot[nr][nc]: continue
            if abs(int(elev[nr][nc]) - int(h)) >= STEP_GATE: continue
            seen[nr][nc] = True
            q.append((nc, nr))
    return seen


def nearest_foot_cell(foot, cell, radius=40):
    c0, r0 = cell
    best = None; bestd = None
    for dr in range(-radius, radius+1):
        for dc in range(-radius, radius+1):
            c, r = c0+dc, r0+dr
            if not (0 <= c < COLS and 0 <= r < ROWS): continue
            if not foot[r][c]: continue
            d = dc*dc + dr*dr
            if bestd is None or d < bestd:
                bestd = d; best = (c, r)
    return best


def main():
    wmx = sys.argv[1] if len(sys.argv) > 1 else '_scratch/wm/wmx.obj'
    out = sys.argv[2] if len(sys.argv) > 2 else '_scratch/wm/rag_landings.json'
    import time
    t0 = time.time()
    landable, foot, have, elev = build_grids(wmx)
    sys.stderr.write('grids in %.1fs: %d landable cells, %d foot cells\n'
                     % (time.time()-t0, int(landable.sum()), int(foot.sum())))

    locs = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else LOCATIONS
    rows = []
    for name, gx, gy in locs:
        mc = raw_to_cell(gx, gy)
        anchor = nearest_foot_cell(foot, mc)
        rec = {'name': name, 'x': gx, 'y': gy}
        if anchor is None:
            rec['status'] = 'no foot ground within 40 cells of the marker'
            rows.append(rec); continue
        comp = foot_component(foot, elev, anchor)
        cand = landable & foot & comp
        if not cand.any():
            rec['status'] = 'no landable cell on the marker landmass'
            rec['anchor'] = list(cell_to_raw(*anchor))
            rows.append(rec); continue
        rr, cc = np.nonzero(cand)
        wx = cc.astype(np.int64) * CELL - OFF_X + CELL//2
        wy = rr.astype(np.int64) * CELL - OFF_Y + CELL//2
        d2 = (wx - gx)**2 + (wy - gy)**2
        k = int(np.argmin(d2))
        rec['land_x'] = int(wx[k]); rec['land_y'] = int(wy[k])
        rec['dist']   = int(round(float(np.sqrt(d2[k]))))
        rec['component_cells'] = int(comp.sum())
        rec['landable_on_component'] = int(cand.sum())
        # how landable is the marker itself? (the "lands ON it" cases)
        c, r = mc
        rec['marker_landable'] = bool(0 <= c < COLS and 0 <= r < ROWS and landable[r][c])
        rec['status'] = 'ok'
        rows.append(rec)
    json.dump(rows, open(out, 'w'), indent=1)
    w = max(len(r['name']) for r in rows)
    for r in rows:
        if r['status'] != 'ok':
            print('%-*s  %s' % (w, r['name'], r['status']))
        else:
            print('%-*s  land (%7d,%7d)  %6d u away%s' %
                  (w, r['name'], r['land_x'], r['land_y'], r['dist'],
                   '   [marker itself is landable]' if r['marker_landable'] else ''))
    sys.stderr.write('wrote %s\n' % out)


LOCATIONS = []
if __name__ == '__main__':
    import re
    src = open('src/world_catalog.inl').read()
    LOCATIONS = [(m[0], int(m[1]), int(m[2]))
                 for m in re.findall(r'\{\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}', src)]
    main()
