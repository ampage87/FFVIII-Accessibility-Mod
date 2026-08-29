#!/usr/bin/env python3
"""GENERATE src/rag_landing_table.inl -- where the Ragnarok sets down.

THE RULE THE GAME ITSELF DRAWS
------------------------------
`wmsetus.obj` section 7 (file offset 5580) is the game's own 64-entry table of
world-map places: `int32 X, int32 Z, int16 height, int16 flags`. Cross-referenced
against the two wmx.obj polygon bits, exactly THREE of its live records are
Ragnarok-landable and NOT foot-walkable:

    #13  (  20480,  -2560)  h= -500   Fisherman's Horizon
    #21  (-118784,  86016)  h= -868   Deep Sea Research Center
    #27  (  54791,   5650)  h=-1849   Esthar Airstation

Those are exactly the three Aaron named as places the airship lands ON rather
than beside: *"For those locations that Ragnarok can land on such as FH, Deep Sea
Research center, and Esthar Airstation, the Ragnarok should drive to the spot to
land on/at these locations."* A pad you can set down on but cannot walk onto is
a pad you arrive at by landing. The signature is the game's, not a guess, and it
picks out three records from fifty-two.

Everything else gets the generic rule: the nearest cell that is landable AND
foot-walkable AND foot-connected to the destination marker, so he lands as close
as he can and walks the rest. That is the opposite of the Garden's berths, which
are deliberately held 2-3 km off because the hull cannot approach a town.

Usage:  python3 offline/gen_rag_table.py [out.inl]
"""
import re
import struct
import sys
from collections import deque

import numpy as np
import gen_rag_landings as G

WMSETUS = '/home/claude/_scratch/wm/wmsetus.obj'
WMX     = '/home/claude/_scratch/wm/wmx.obj'
CATALOG = '/home/claude/src/world_catalog.inl'
LOC_OFF, LOC_N = 5580, 64
PAD_MATCH_UNITS = 4000     # how near a pad must be to claim a catalog name

# ESTHAR IS REACHED BY LANDING AT ITS AIRSTATION, and the data says so twice
# over: the city marker's own foot component contains no landable cell at all,
# and the only pad within 20 km is record 27 at (54791, 5650) -- 8.2 km away,
# far outside PAD_MATCH_UNITS, because the Airstation is a place in its own
# right rather than Esthar City's back garden. A sighted player flies to the
# Airstation and walks in from there; this is that, written down.
VIA_PAD = { 'Esthar City': 27 }


def game_records():
    d = open(WMSETUS, 'rb').read()
    out = []
    for i in range(LOC_N):
        x, z, h, fl = struct.unpack_from('<iihh', d, LOC_OFF + i*12)
        if x == 0 and z == 0 and h == 0: continue
        out.append((i, x, z, h, fl & 0xFFFF))
    return out


def catalog():
    src = open(CATALOG).read()
    return [(m[0], int(m[1]), int(m[2])) for m in
            re.findall(r'\{\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}', src)]


# v0.83.0: how much landable ground a landing point must have around it before
# it is worth flying to. The 10:08 BAT landed at Fisherman's Horizon (889 units
# of room) and failed at the Deep Sea Research Center (384) and the Esthar
# Airstation (465), with the two failures' stop coordinates sitting on ocean and
# on a cliff face. 384 is the smallest room any pad has, so it is the bar: a
# walk landing with less room than the tightest pad is a landing nobody has
# reason to believe in.
CLEAR_WANT = 384

# v0.97.0: how much landable ground a landing point must have MEASURED ON THE
# POLYGONS. Aaron, after the clean global tour: "A few times when the mod told me
# to land I was not on a spot where the game would let me land, and I had to
# manually move forward or backward to land nearby."
#
# The reason is arithmetic, and it is the MARGIN rather than the walk. The arrival
# radius is floored at 288 -- just over the airship's 0x100 move step -- and
# v0.83.0 set it to clearance minus 96. So a row whose ground reaches only 320
# units got a 288 radius with THIRTY-TWO UNITS TO SPARE: the ship is allowed to
# stop 288 units from a point that has 320 units of room, and anything the game
# wants beyond a single bare point falls off the edge. Twenty-nine of the forty
# rows had under 256 units of margin.
#
# 288 of radius plus 256 of margin -- the airship's own move step -- is 544. The
# 256-unit cell grid cannot tell 320 from 544, which is exactly why v0.83.0's bar
# was met on paper and missed in the game, so the grid now only picks a shortlist
# and the polygon measurement decides.
CLEAR_NEED = 544
CAND_N     = 24        # nearest candidates worth measuring at polygon precision

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/src/rag_landing_table.inl'
    landable, foot, have, elev = G.build_grids(WMX)

    # v0.83.0: how much landable ground is around every cell, in units. The
    # exact Euclidean distance transform of the NON-landable set: for a landable
    # cell this is the distance to the nearest cell the airship cannot set down
    # on, which is the radius of the all-landable disc around it.
    import rag_clear
    rag_clear.load(WMX)
    from scipy import ndimage
    clear_grid = (ndimage.distance_transform_edt(landable) * G.CELL).astype(np.int32)

    # --- the pads: landable, not foot-walkable, in the game's own table ------
    pads = []
    for i, x, z, h, fl in game_records():
        c, r = G.raw_to_cell(x, z)
        if not (0 <= c < G.COLS and 0 <= r < G.ROWS): continue
        if landable[r][c] and not foot[r][c]:
            pads.append((i, x, z, h))

    cat = catalog()
    rows = []
    for name, gx, gy in cat:
        pad = None
        want = VIA_PAD.get(name)
        if want is not None:
            for i, x, z, h in pads:
                if i == want:
                    d = ((x-gx)**2 + (z-gy)**2) ** 0.5
                    rows.append(dict(name=name, x=gx, y=gy, land_x=x, land_y=z,
                                     dist=int(round(d)), kind='PAD', rec=i,
                                     note="reached by landing at the Esthar Airstation "
                                          "(record %d); the city's own ground has no "
                                          "landable cell" % i))
                    break
            else:
                rows.append(dict(name=name, x=gx, y=gy, kind='NONE',
                                 note='via-pad %d not found' % want))
            continue
        for i, x, z, h in pads:
            d = ((x-gx)**2 + (z-gy)**2) ** 0.5
            if d <= PAD_MATCH_UNITS and (pad is None or d < pad[4]):
                pad = (i, x, z, h, d)
        if pad is not None:
            rows.append(dict(name=name, x=gx, y=gy, land_x=pad[1], land_y=pad[2],
                             dist=int(round(pad[4])), kind='PAD', rec=pad[0],
                             note='the game\'s own record %d, landable and not walkable' % pad[0]))
            continue
        anchor = G.nearest_foot_cell(foot, G.raw_to_cell(gx, gy))
        if anchor is None:
            rows.append(dict(name=name, x=gx, y=gy, kind='NONE',
                             note='no foot ground within 40 cells of the marker'))
            continue
        comp = G.foot_component(foot, elev, anchor)
        cand = landable & foot & comp
        if not cand.any():
            rows.append(dict(name=name, x=gx, y=gy, kind='NONE',
                             note='no landable cell on the marker landmass'))
            continue
        rr, cc = np.nonzero(cand)
        wx = cc.astype(np.int64)*G.CELL - G.OFF_X + G.CELL//2
        wy = rr.astype(np.int64)*G.CELL - G.OFF_Y + G.CELL//2
        d2 = (wx-gx)**2 + (wy-gy)**2
        # v0.83.0: NEAREST IS NOT THE SAME AS LANDABLE-ON.
        #
        # Up to v0.82.0 this took the closest qualifying cell outright, and the
        # 10:08 BAT showed what that costs: 23 of the 40 rows landed on ground
        # 64 to 256 units wide, because "landable AND foot-walkable" is most
        # easily satisfied exactly ON the seam between the two, which is a strip
        # a couple of cells across. A landing point with 64 units of room is one
        # the airship cannot set down on -- the ship never stops on the exact
        # coordinate, and off the strip is ocean or cliff.
        #
        # So: prefer room, then prefer nearness. Among cells with at least
        # CLEAR_WANT units of landable ground all round, take the closest; if no
        # cell reaches that, take the roomiest available and say so.
        clear_u = clear_grid[rr, cc]
        # v0.97.0: THE CELL GRID PICKS THE SHORTLIST, THE POLYGONS DECIDE.
        good = np.nonzero(clear_u >= CLEAR_WANT)[0]
        cand = good if good.size else np.argsort(clear_u)[::-1][:CAND_N]
        order_by_walk = cand[np.argsort(d2[cand])][:CAND_N]
        k = None
        best_fallback = None
        for idx in order_by_walk:
            cl_i = rag_clear.clearance(int(wx[idx]), int(wy[idx]))
            if best_fallback is None or cl_i > best_fallback[1]:
                best_fallback = (int(idx), cl_i)
            if cl_i >= CLEAR_NEED:
                k = int(idx)
                note = ('nearest cell with %d units of landable ground all round '
                        '(measured on the polygons, not the grid)' % cl_i)
                break
        if k is None:
            k, cl_i = best_fallback
            note = ('NO cell within reach has %d units of room; took the roomiest '
                    'at %d' % (CLEAR_NEED, cl_i))
        rows.append(dict(name=name, x=gx, y=gy, land_x=int(wx[k]), land_y=int(wy[k]),
                         dist=int(round(float(np.sqrt(d2[k])))), kind='WALK',
                         clear=int(clear_u[k]), note=note))
    # v0.83.0: the two numbers the arrival now needs, measured on the polygons
    # rather than on the 256-unit planner grid the chooser above works in.
    import rag_clear
    rag_clear.load(WMX)
    for r in rows:
        if r['kind'] == 'NONE': continue
        cl = rag_clear.clearance(r['land_x'], r['land_y'])
        ok, h = rag_clear.probe(r['land_x'], r['land_y'])
        r['clear']    = cl
        r['ground_h'] = h if h is not None else 0
        # The stop must land INSIDE the all-landable disc, so the radius is the
        # clearance less a margin -- floored at 288 because the airship's move
        # step is 0x100 = 256 units and a radius at or under one step cannot be
        # hit reliably, and capped at 512 because past that the walk starts
        # growing for no gain.
        # v0.97.0: the margin is the airship's own move step for a WALK row --
        # 256, not 96. The ship may stop anywhere inside the radius, so what has
        # to be landable is the radius PLUS whatever the game wants around the
        # ship, and 96 units of that was plainly not enough.
        #
        # A PAD keeps the old 96. Its coordinate is the game's own and cannot be
        # moved to find more room -- the Deep Sea pad simply IS 384 units across
        # -- and all three have landed correctly in every BAT since v0.83.0.
        margin = 96 if r['kind'] == 'PAD' else 256
        r['arrive'] = max(288, min(512, cl - margin))
        if r['arrive'] >= cl:
            print('!! %-26s radius %d >= clearance %d -- CANNOT LAND RELIABLY'
                  % (r['name'], r['arrive'], cl))

    emit(out, rows, pads)
    for r in rows:
        if r['kind'] == 'NONE':
            print('%-26s  --  %s' % (r['name'], r['note']))
        else:
            print('%-26s  %-4s (%7d,%7d)  walk %5d u' %
                  (r['name'], r['kind'], r['land_x'], r['land_y'], r['dist']))


def emit(path, rows, pads):
    n = 0
    with open(path, 'w') as f:
        f.write(HEADER % (len(pads), '\n'.join(
            '//   #%-3d (%8d,%8d)  h=%6d' % p for p in pads)))
        f.write('static const RagLanding RAG_LANDINGS[] = {\n')
        for r in rows:
            if r['kind'] == 'NONE':
                f.write('    // %-26s -- %s\n' % ('"%s"' % r['name'], r['note']))
                continue
            n += 1
            f.write('    { %-28s %8d, %8d, %6d, RAG_%-5s %5d, %6d, %5d },   // margin %d; %s\n' %
                    ('"%s",' % r['name'], r['land_x'], r['land_y'], r['dist'],
                     r['kind'] + ',', r['arrive'], r['ground_h'], r['clear'],
                     r['clear'] - r['arrive'], r['note']))
        f.write('};\n')
        f.write('static const int RAG_LANDING_COUNT = '
                '(int)(sizeof(RAG_LANDINGS) / sizeof(RAG_LANDINGS[0]));\n')
    sys.stderr.write('wrote %s (%d rows)\n' % (path, n))


HEADER = '''// rag_landing_table.inl -- GENERATED by offline/gen_rag_table.py. Do not edit.
//
// Where the Ragnarok sets down for each world-map destination.
//
// The airship has no movement mask at all -- 0x53E6B0 falls through to
// `return 1` for vehicle 0x32, so it flies over everything and there is nothing
// to route around. The only question the world map asks about it is where it
// may SET DOWN, and that is a different bit on a different byte: 0x53E730 reads
// (typeTriple >> 8) & 0x80, i.e. poly[14] bit 7.
//
// Two kinds of row:
//
//   RAG_PAD   the destination IS a landing pad -- fly to the spot and land on
//             it. Identified by the game's own data rather than by hand: a
//             record in the wmsetus location table that is landable and NOT
//             foot-walkable. Exactly %d records in the whole table qualify:
%s
//             ...which are Fisherman's Horizon, the Deep Sea Research Center
//             and the Esthar Airstation -- the three Aaron named.
//
//   RAG_WALK  land as close as possible and walk. The cell must be landable,
//             foot-walkable, and foot-connected to the destination marker under
//             the engine's own step gate (|dH| >= 200 refuses), so the landing
//             is never across a strait from the place it serves.
//
// Distances are the walk left after landing, in world units.
'''

if __name__ == '__main__':
    main()
