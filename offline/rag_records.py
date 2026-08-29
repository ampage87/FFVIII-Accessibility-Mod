#!/usr/bin/env python3
"""Cross-reference the game's own world-map location table against the
Ragnarok landing mask.

wmsetus.obj section index 7 (file offset 5580) is a 64-entry table of 12-byte
records -- `int32 X, int32 Z, int16 height, int16 flags`. Records 0..45 are
real; 46..55 and 62 are zeroed. Balamb Town, Deling City, Fisherman's Horizon
and the Deep Sea Research Center all appear at their known coordinates, so this
is the game's own list of places on the world map.
"""
import struct, sys, re
from collections import deque
import numpy as np
import gen_rag_landings as G

WMSETUS = '/home/claude/_scratch/wm/wmsetus.obj'
WMX     = '/home/claude/_scratch/wm/wmx.obj'
LOC_OFF, LOC_N = 5580, 64

def records(path=WMSETUS):
    d = open(path, 'rb').read()
    for i in range(LOC_N):
        x, z, h, fl = struct.unpack_from('<iihh', d, LOC_OFF + i*12)
        yield i, x, z, h, fl & 0xFFFF

def comp_size(foot, elev, cell, cap=200000):
    c0, r0 = cell
    if not foot[r0][c0]: return 0
    seen = {(c0, r0)}; q = deque([(c0, r0)]); n = 0
    while q and n < cap:
        c, r = q.popleft(); n += 1
        h = int(elev[r][c])
        for dc, dr in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
            nc, nr = c+dc, r+dr
            if not (0 <= nc < G.COLS and 0 <= nr < G.ROWS): continue
            if (nc, nr) in seen or not foot[nr][nc]: continue
            if abs(int(elev[nr][nc]) - h) >= G.STEP_GATE: continue
            seen.add((nc, nr)); q.append((nc, nr))
    return n

if __name__ == '__main__':
    landable, foot, have, elev = G.build_grids(WMX)
    cat = [(m[0], int(m[1]), int(m[2])) for m in
           re.findall(r'\{\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}',
                      open('/home/claude/src/world_catalog.inl').read())]
    print('%3s %9s %9s %7s %-6s %-6s %8s  %-26s %8s' %
          ('#','x','z','h','land','foot','footcomp','nearest catalog entry','dist'))
    for i, x, z, h, fl in records():
        if x == 0 and z == 0 and h == 0: continue
        c, r = G.raw_to_cell(x, z)
        inb = 0 <= c < G.COLS and 0 <= r < G.ROWS
        L = bool(inb and landable[r][c]); F = bool(inb and foot[r][c])
        cs = comp_size(foot, elev, (c, r)) if F else 0
        best, bd = None, None
        for nm, cx, cy in cat:
            d = ((cx-x)**2 + (cy-z)**2) ** 0.5
            if bd is None or d < bd: bd, best = d, nm
        print('%3d %9d %9d %7d %-6s %-6s %8d  %-26s %8d' %
              (i, x, z, h, L, F, cs, best, round(bd)))
