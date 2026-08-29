#!/usr/bin/env python3
"""How much landable ground surrounds a landing point, at polygon precision.

THE 2026-08-25 10:08 BAT: FH LANDED, THE OTHER TWO PADS DID NOT.

v0.79.0 shipped one arrival radius for every destination -- RAG_ARRIVE_DIST =
512, two of the airship's own 0x100 move steps. The comment beside it read "the
Deep Sea Research Center's pad is 768 units across, so a tighter radius flies
past it": the right number and the wrong conclusion. 768 ACROSS IS 384 FROM THE
MIDDLE, and 512 > 384, so the drive was allowed to stop a hundred and thirty
units clear of the pad it was aiming at.

The BAT is that arithmetic happening:

    pad                        clearance   stopped at   landed
    Fisherman's Horizon             889         501       yes
    Deep Sea Research Center        384     464, 499       no
    Esthar Airstation               465         506        no

and the polygons under the two failures say the same thing without reference to
any radius: terrain 34 (ocean) under one Deep Sea stop, terrain 29 (mountain
face) under the Esthar stop. The engine's own live ground height at 0x0203FE30
agreed with this file's reading at all four stops -- 0/-19 at FH, -378/-401 at
the Deep Sea pad, -100 on the Esthar mountain -- so the polygon test below is
validated against the game rather than trusted.

CLEARANCE is the radius of the largest disc centred on the landing point whose
every sample is landable. Sampled at 64 units, which is a quarter of the
planner's cell and well under the 256-unit move step.
"""
import struct
import gen_rag_landings as G

SS, BPS, BH, PS = G.SEGMENT_SIZE, G.BLOCKS_PER_SEG, G.BLOCK_HDR, G.POLY_SIZE
SAMPLE = 64          # sampling step, units
REACH  = 1024        # stop looking past this; no radius will ever exceed it

_data = None
_seg  = {}

def load(path):
    global _data
    _data = open(path, 'rb').read()
    _seg.clear()

def _tris(sc, sr):
    k = (sc, sr)
    if k in _seg: return _seg[k]
    out = []
    base = (sr * G.SEG_COLS + sc) * SS
    if 0 <= sc < G.SEG_COLS and 0 <= sr < G.SEG_ROWS and base + SS <= len(_data):
        for b in range(BPS):
            off = struct.unpack_from('<I', _data, base + 4 + b * 4)[0]
            if off == 0 or off + BH > SS: continue
            bb = base + off
            pc, vc = _data[bb], _data[bb + 1]
            pe = off + BH + pc * PS
            if pe > SS or pe + vc * 8 > SS: continue
            vb = bb + BH + pc * PS
            ox = sc * 8192 + (b % 4) * 2048
            oy = sr * 8192 + (b // 4) * 2048
            vx = [0]*vc; vy = [0]*vc; vz = [0]*vc
            for v in range(vc):
                lvx, lvz, lvy = struct.unpack_from('<hhh', _data, vb + v*8)
                vx[v] = ox + lvx; vz[v] = lvz; vy[v] = oy - lvy
            for p in range(pc):
                pb = bb + BH + p * PS
                i0, i1, i2 = _data[pb], _data[pb+1], _data[pb+2]
                if i0 >= vc or i1 >= vc or i2 >= vc: continue
                out.append((vx[i0], vy[i0], vx[i1], vy[i1], vx[i2], vy[i2],
                            (_data[pb + 0x0E] & 0x80) != 0,
                            (vz[i0] + vz[i1] + vz[i2]) // 3))
    _seg[k] = out
    return out

def probe(gx, gy):
    """(landable, mean vertex height) for the polygon under a GAME coordinate."""
    rx, ry = gx + G.OFF_X, gy + G.OFF_Y
    for ax, ay, bx, by, cx, cy, land, h in _tris(rx // 8192, ry // 8192):
        d1 = (rx-bx)*(ay-by) - (ax-bx)*(ry-by)
        d2 = (rx-cx)*(by-cy) - (bx-cx)*(ry-cy)
        d3 = (rx-ax)*(cy-ay) - (cx-ax)*(ry-ay)
        if ((d1 < 0) or (d2 < 0) or (d3 < 0)) and ((d1 > 0) or (d2 > 0) or (d3 > 0)):
            continue
        if land: return True, h
    return False, None

def clearance(gx, gy):
    """Radius of the largest all-landable disc centred here, capped at REACH."""
    ok, _ = probe(gx, gy)
    if not ok: return 0
    best = REACH
    for dy in range(-REACH, REACH + 1, SAMPLE):
        for dx in range(-REACH, REACH + 1, SAMPLE):
            d = (dx*dx + dy*dy) ** 0.5
            if d >= best: continue
            hit, _ = probe(gx + dx, gy + dy)
            if not hit: best = d
    return int(best)
