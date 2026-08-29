"""Census of the Ragnarok LANDING mask in wmx.obj.

The engine's set-down predicate (0x53E730, via 0x54B860) reads, for vehicle
0x32:  (typeTriple >> 8) & 0x80  where typeTriple = poly[15]<<16 | poly[14]<<8
| poly[13].  So the bit is simply poly[14] & 0x80.

Movement needs no mask at all -- the Ragnarok flies -- so landing is the only
question the world map asks about it.
"""
import struct, sys
import numpy as np

SEG_COLS, SEG_ROWS = 32, 24
SEGMENT_SIZE   = 36864
BLOCKS_PER_SEG = 16
BLOCK_HDR      = 4
POLY_SIZE      = 16
PLAYABLE_SEGS  = SEG_COLS * SEG_ROWS      # 768; 768..834 are story swaps

def polygons(path):
    """Yield (segment, terrain, footWalkable, ragLandable, (x,y,z) x3)."""
    data = open(path, 'rb').read()
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
                land    = (data[pb + 0x0E] & 0x80) != 0     # RAGNAROK set-down
                foot    = (data[pb + 0x0F] & 0x80) != 0     # on-foot walkable
                yield (seg, terrain, foot, land,
                       (vx[i0], vy[i0], vz[i0]),
                       (vx[i1], vy[i1], vz[i1]),
                       (vx[i2], vy[i2], vz[i2]))

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else '_scratch/wm/wmx.obj'
    tot = 0; land = 0; foot = 0; both = 0
    byterr = {}
    for seg, terrain, f, l, a, b, c in polygons(path):
        tot += 1
        if l: land += 1
        if f: foot += 1
        if l and f: both += 1
        d = byterr.setdefault(terrain, [0, 0, 0])
        d[0] += 1
        if l: d[1] += 1
        if f: d[2] += 1
    print('polygons %d  ragnarok-landable %d (%.1f%%)  foot %d  both %d'
          % (tot, land, 100.0*land/tot, foot, both))
    print('%-8s %10s %10s %10s' % ('terrain', 'polys', 'landable', 'foot'))
    for t in sorted(byterr, key=lambda k: -byterr[k][0])[:24]:
        n, l, f = byterr[t]
        print('%-8d %10d %9d%% %9d%%' % (t, n, 100*l//max(n,1), 100*f//max(n,1)))
