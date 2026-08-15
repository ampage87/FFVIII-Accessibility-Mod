"""Rasterize wmx.obj into per-vehicle traversability grids in MESH space.

Mesh space:  mx in [0,262144)  = blockCol*2048 + vx
             mz in [0,196608)  = rowEng*2048 + (vz+2048)
Game space:  gx = wrap(mx - 0x60000)      -> [-131072,131071]
             gz = wrap(0x48000 - mz)      -> [-98304,98303]

Engine walk masks (FF8_EN.exe 0x53E6B0, movement) on poly byte15:
    foot 0x80 | car 0x40 | GARDEN 0x20 | chocobo 0x10 | ragnarok = always
Engine land/park masks (0x53E730) on byte15: car 0x04 | GARDEN 0x02 |
    chocobo 0x01 ; ragnarok = byte14 0x80
Step gate: |dH| >= 200 rejects (0x53E7A0 / 0x54B860), vehicle-independent.
"""
import struct, numpy as np, sys

CELL = 128
COLS = 262144 // CELL      # 2048
ROWS = 196608 // CELL      # 1536
SEG = 0x9000

def build(path="./wmx.obj"):
    d = open(path, "rb").read()
    hgt   = np.zeros((ROWS, COLS), np.int32)
    terr  = np.full((ROWS, COLS), 34, np.uint8)
    b15   = np.zeros((ROWS, COLS), np.uint8)
    b14   = np.zeros((ROWS, COLS), np.uint8)
    filled= np.zeros((ROWS, COLS), bool)
    # per-cell we keep FIRST-CONTAINING (stored order) -> matches engine validator
    for file_row in range(96):
        row_eng = 95 - file_row
        for file_col in range(128):
            seg = (file_row >> 2) * 32 + (file_col >> 2)
            sub = (file_row & 3) * 4 + (file_col & 3)
            base = seg * SEG
            off = struct.unpack_from("<I", d, base + 4 + sub * 4)[0]
            p = base + off
            pc = d[p]; vc = d[p + 1]
            pp = p + 4
            vp = pp + pc * 16
            verts = np.frombuffer(d, np.int16, count=vc * 4, offset=vp).reshape(vc, 4).astype(np.int32)
            bx = file_col * 2048
            bz = row_eng * 2048
            VX = verts[:, 0] + bx
            VH = verts[:, 1]
            VZ = verts[:, 2] + 2048 + bz
            for i in range(pc):
                q = pp + i * 16
                i0, i1, i2 = d[q], d[q + 1], d[q + 2]
                if i0 >= vc or i1 >= vc or i2 >= vc:
                    continue
                x0, x1, x2 = VX[i0], VX[i1], VX[i2]
                z0, z1, z2 = VZ[i0], VZ[i1], VZ[i2]
                h0, h1, h2 = VH[i0], VH[i1], VH[i2]
                c0 = int(min(x0, x1, x2)) // CELL
                c1 = int(max(x0, x1, x2)) // CELL
                r0 = int(min(z0, z1, z2)) // CELL
                r1 = int(max(z0, z1, z2)) // CELL
                if c0 < 0: c0 = 0
                if r0 < 0: r0 = 0
                if c1 > COLS - 1: c1 = COLS - 1
                if r1 > ROWS - 1: r1 = ROWS - 1
                if c1 < c0 or r1 < r0: continue
                cs = np.arange(c0, c1 + 1)
                rs = np.arange(r0, r1 + 1)
                px = cs * CELL + CELL // 2
                pz = rs * CELL + CELL // 2
                PX = px[None, :]; PZ = pz[:, None]
                d0 = (x1 - x0) * (PZ - z0) - (z1 - z0) * (PX - x0)
                d1 = (x2 - x1) * (PZ - z1) - (z2 - z1) * (PX - x1)
                d2 = (x0 - x2) * (PZ - z2) - (z0 - z2) * (PX - x2)
                inside = ((d0 >= 0) & (d1 >= 0) & (d2 >= 0)) | ((d0 <= 0) & (d1 <= 0) & (d2 <= 0))
                if not inside.any(): continue
                sub_f = filled[r0:r1 + 1, c0:c1 + 1]
                m = inside & (~sub_f)
                if not m.any(): continue
                den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
                if den == 0:
                    H = np.full(inside.shape, (h0 + h1 + h2) / 3.0)
                else:
                    w0 = ((z1 - z2) * (PX - x2) + (x2 - x1) * (PZ - z2)) / den
                    w1 = ((z2 - z0) * (PX - x2) + (x0 - x2) * (PZ - z2)) / den
                    H = w0 * h0 + w1 * h1 + (1.0 - w0 - w1) * h2
                    H = np.broadcast_to(H, inside.shape)
                hs = hgt[r0:r1 + 1, c0:c1 + 1]; hs[m] = H[m].astype(np.int32)
                ts = terr[r0:r1 + 1, c0:c1 + 1]; ts[m] = d[q + 13]
                s5 = b15[r0:r1 + 1, c0:c1 + 1]; s5[m] = d[q + 15]
                s4 = b14[r0:r1 + 1, c0:c1 + 1]; s4[m] = d[q + 14]
                sub_f[m] = True
    return hgt, terr, b15, b14, filled

if __name__ == "__main__":
    import time
    t = time.time()
    hgt, terr, b15, b14, filled = build()
    print("raster %.1fs  filled %d/%d cells" % (time.time() - t, filled.sum(), ROWS * COLS))
    np.savez_compressed("./wmgrid.npz", hgt=hgt, terr=terr, b15=b15, b14=b14, filled=filled)
    for nm, msk in (("foot", 0x80), ("car", 0x40), ("garden", 0x20), ("chocobo", 0x10)):
        print("%-8s walkable cells: %8d (%.1f%%)" % (nm, ((b15 & msk) != 0).sum(),
              100.0 * ((b15 & msk) != 0).sum() / (ROWS * COLS)))
    print("garden-LAND(0x02) cells:", ((b15 & 2) != 0).sum())
