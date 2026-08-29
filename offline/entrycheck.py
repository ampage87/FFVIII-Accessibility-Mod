"""Independent verification that an aim point stands on a foot-walkable
field-entry polygon: byte 0x0E bit 3 set (entry trigger) AND byte 0x0F bit 7
set (foot-walkable). Point-in-triangle against the real wmx triangles, in game
coordinates, with the same mesh->game mapping the shipped generator uses."""
import struct
D = open('/root/work/wmx.obj', 'rb').read()
SEG = 0x9000

def m2g(mx, mz):
    gx = mx - 0x60000
    if gx < -0x20000: gx += 0x40000
    gz = 0x48000 - mz
    if gz > 0x18000: gz -= 0x30000
    return gx, gz

def segof(x, y):
    return (((y + 0x48000) % 0x30000) >> 13) * 32 + (((x + 0x60000) & 0x3FFFF) >> 13)

def entry_tris():
    out = []
    for sn in range(768):                      # base map only: 768..834 are story swaps
        base = sn * SEG; sc = sn % 32; sr = sn // 32
        for sub in range(16):
            off = struct.unpack_from('<I', D, base + 4 + sub * 4)[0]
            p = base + off
            if off == 0 or p + 4 > len(D): continue
            pc, vc = D[p], D[p+1]
            pp = p + 4; vp = pp + pc * 16
            if vp + vc * 8 > len(D): continue
            v = [struct.unpack_from('<4h', D, vp + i * 8) for i in range(vc)]
            fr = sr * 4 + (sub // 4); fc = sc * 4 + (sub % 4)
            bx = fc * 2048; bz = (95 - fr) * 2048
            for i in range(pc):
                q = pp + i * 16
                if not (D[q+14] & 0x08): continue      # not an entry polygon
                if not (D[q+15] & 0x80): continue      # not foot-walkable
                i0, i1, i2 = D[q], D[q+1], D[q+2]
                if max(i0, i1, i2) >= vc: continue
                out.append(tuple(m2g(v[k][0] + bx, v[k][2] + 2048 + bz) for k in (i0, i1, i2)))
    return out

def inside(px, py, t):
    (x0,y0),(x1,y1),(x2,y2) = t
    d0 = (x1-x0)*(py-y0) - (y1-y0)*(px-x0)
    d1 = (x2-x1)*(py-y1) - (y2-y1)*(px-x1)
    d2 = (x0-x2)*(py-y2) - (y0-y2)*(px-x2)
    return (d0>=0 and d1>=0 and d2>=0) or (d0<=0 and d1<=0 and d2<=0)
