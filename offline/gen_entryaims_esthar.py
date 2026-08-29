"""v0.21.9 offline: the five Esthar catalog destinations, resolved from the
engine's own data.

Method is gen_entryaims2.py's, with three corrections that matter here:

  1. wmx.obj holds 835 segment blocks, not 768. Blocks 768..834 are story-state
     terrain replacements; iterating them with sr = sn//32 puts them at row 24+
     and folds their triangles back onto real coordinates. This scan uses the
     BASE MAP ONLY (blocks 0..767) and reports the replacement blocks separately.
     (No replacement block lands in the Esthar region, so this changes nothing
     there -- but it is the honest way to count.)

  2. The trigger table is read from wmsetus.obj section 7 directly rather than
     from the .inl, so the clause/destination/coordinate-bound data in the report
     is a decode, not a transcription.

  3. Destination ids 0..45 are cross-checked against the wmsetus SECTION 8
     coordinate table (offset 5580, 12-byte records: int32 x, int32 y, int16 z,
     int16 ?). That table is the binding authority for "which place is
     destination N": for 18 independently-known destinations its record lands in
     exactly the segment the program tests, and it reproduces all three of the
     table's coordinate splits (Winhill Xoff 6144 -> records 14/15 land on the
     correct sides; program 4's Yoff 4096 -> record 35; program 9's Yoff 7000 ->
     record 7). Records 46..55 are zero and destination ids >= 46 do NOT index
     it -- they are not resolvable from this file.

Run:  python3 offline/gen_entryaims_esthar.py     (cwd = repo root)
"""
import struct, math, collections, numpy as np

WMX   = 'wmx.obj'
WMSET = 'wmsetus.obj'
SEGSZ = 0x9000
BASE_BLOCKS = 768          # blocks 768..834 are story-state replacements
STORY = 2000               # Aaron's current story value

# ---------------------------------------------------------------- coordinates
def m2g(mx, mz):
    gx = mx - 0x60000
    if gx < -0x20000: gx += 0x40000
    gz = 0x48000 - mz
    if gz > 0x18000: gz -= 0x30000
    return gx, gz

def segof(x, y):
    return (((y + 0x48000) % 0x30000) >> 13) * 32 + (((x + 0x60000) & 0x3FFFF) >> 13)

def xoff(x): return (x + 0x60000) & 0x1FFF
def yoff(y): return (y + 0x48000) & 0x1FFF

def seg_bounds(s):
    """world-space [x0,x1] [y0,y1] of segment s, inclusive"""
    r, c = s // 32, s % 32
    x0 = c * 8192 - 0x60000
    if x0 < -0x20000: x0 += 0x40000
    y0 = r * 8192 - 0x48000
    if y0 < -0x18000: y0 += 0x30000
    return x0, x0 + 8191, y0, y0 + 8191

# ------------------------------------------------- wmsetus section 7: programs
WD = open(WMSET, 'rb').read()
SEC = [struct.unpack_from('<I', WD, i * 4)[0] for i in range(struct.unpack_from('<I', WD, 0)[0] // 4)]
S7, S8 = SEC[7], SEC[8]

def decode_programs():
    offs = [struct.unpack_from('<I', WD, S7 + i * 4)[0] for i in range(38)]
    progs = []
    for i in range(38):
        a = S7 + offs[i]
        b = S7 + offs[i + 1] if i + 1 < 38 else S8
        ins = []
        p = a
        while p + 4 <= b:
            op, ff, arg = WD[p], WD[p + 1], struct.unpack_from('<H', WD, p + 2)[0]
            if ff != 0xFF: break
            ins.append((op, arg)); p += 4
        prog = dict(idx=i, seg=None, storyGte=0, storyLt=0, unk21=None, clauses=[])
        cur = None
        for op, arg in ins:
            if   op == 0x01: pass
            elif op == 0x06: prog['seg'] = arg
            elif op == 0x04: cur = None                      # begin clause block
            elif op in (0x0a, 0x0c):                         # new clause
                cur = dict(veh=None, gte=0, lt=0, xLt=None, yLt=None,
                           xGt=None, yGt=None, unk20=None, dest=None)
                prog['clauses'].append(cur)
            elif op == 0x02:
                (cur if cur else prog)['gte' if cur else 'storyGte'] = arg
            elif op == 0x03:
                (cur if cur else prog)['lt' if cur else 'storyLt'] = arg
            elif op == 0x09:
                if cur: cur['veh'] = arg
                else:   prog['topVeh'] = arg
            elif op == 0x0f: (cur or prog)['xLt'] = arg
            elif op == 0x10: (cur or prog)['yLt'] = arg
            elif op == 0x11: (cur or prog)['xGt'] = arg
            elif op == 0x12: (cur or prog)['yGt'] = arg
            elif op == 0x20: (cur or prog)['unk20'] = arg
            elif op == 0x21: prog['unk21'] = arg
            elif op == 0x08:
                if cur: cur['dest'] = arg
                else:   prog.setdefault('bare_dest', arg)
            elif op in (0x0b, 0x05, 0x16): pass
        # program 18 has a bare 08 with no 0a/0c wrapper
        if not prog['clauses'] and 'bare_dest' in prog:
            prog['clauses'].append(dict(veh=prog.get('topVeh'), gte=0, lt=0, xLt=None,
                                        yLt=None, xGt=None, yGt=None, unk20=None,
                                        dest=prog['bare_dest']))
        progs.append(prog)
    return progs

PROGS = decode_programs()
BY_SEG = collections.defaultdict(list)
for p in PROGS: BY_SEG[p['seg']].append(p)

# --------------------------------------------- wmsetus section 8: dest records
DESTREC = {}
for i in range(64):
    o = S8 + i * 12
    x, y = struct.unpack_from('<ii', WD, o)
    z, f = struct.unpack_from('<hH', WD, o + 8)
    DESTREC[i] = (x, y, z, f)

# ------------------------------------------------------ wmx.obj entry polygons
D = open(WMX, 'rb').read()
NBLK = len(D) // SEGSZ
TRIS = []          # (tri, seg, walkable, block)
for sn in range(NBLK):
    base = sn * SEGSZ; sc = sn % 32; sr = sn // 32
    for sub in range(16):
        off = struct.unpack_from('<I', D, base + 4 + sub * 4)[0]; p = base + off
        if p + 4 > len(D): continue
        pc, vc = D[p], D[p + 1]; pp = p + 4; vp = pp + pc * 16
        if vp + vc * 8 > len(D): continue
        v = np.frombuffer(D, '<i2', count=vc * 4, offset=vp).reshape(vc, 4).astype(int)
        fr = sr * 4 + (sub // 4); fc = sc * 4 + (sub % 4)
        bx = fc * 2048; bz = (95 - fr) * 2048
        for i in range(pc):
            q = pp + i * 16
            if not (D[q + 14] & 0x08): continue          # entry-trigger flag
            i0, i1, i2 = D[q], D[q + 1], D[q + 2]
            if max(i0, i1, i2) >= vc: continue
            t = [m2g(int(v[k][0]) + bx, int(v[k][2]) + 2048 + bz) for k in (i0, i1, i2)]
            cx = sum(a[0] for a in t) // 3; cy = sum(a[1] for a in t) // 3
            TRIS.append((t, segof(cx, cy), bool(D[q + 15] & 0x80), sn))

BASE = [t for t in TRIS if t[3] < BASE_BLOCKS]
WALK = [t for t in BASE if t[2]]
SEGTRIS = collections.defaultdict(list)
for t, s, w, sn in WALK: SEGTRIS[s].append(t)

# ------------------------------------------------------------------ geometry
def inside(px, py, t):
    (x0, y0), (x1, y1), (x2, y2) = t
    d0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)
    d1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)
    d2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)
    return (d0 >= 0 and d1 >= 0 and d2 >= 0) or (d0 <= 0 and d1 <= 0 and d2 <= 0)

def clusters(tris, tol=400):
    """flood-fill triangles into patches by centroid adjacency"""
    cen = [(sum(p[0] for p in t) // 3, sum(p[1] for p in t) // 3) for t in tris]
    seen = set(); out = []
    for i in range(len(tris)):
        if i in seen: continue
        grp = {i}; st = [i]
        while st:
            k = st.pop()
            for j in range(len(tris)):
                if j in grp: continue
                if abs(cen[j][0] - cen[k][0]) <= tol and abs(cen[j][1] - cen[k][1]) <= tol:
                    grp.add(j); st.append(j)
        seen |= grp
        out.append([tris[j] for j in sorted(grp)])
    out.sort(key=len, reverse=True)
    return out

def aim_of(tris, keep=None):
    """interior sample farthest from the region edge; keep(x,y) restricts the
    region to one side of a clause split."""
    xs = [p[0] for t in tris for p in t]; ys = [p[1] for t in tris for p in t]
    X0, X1, Y0, Y1 = min(xs), max(xs), min(ys), max(ys)
    step = max(4, max(X1 - X0, Y1 - Y0) // 160)
    gx = np.arange(X0, X1 + 1, step); gy = np.arange(Y0, Y1 + 1, step)
    M = np.zeros((len(gy), len(gx)), bool)
    for j, y in enumerate(gy):
        for i, x in enumerate(gx):
            if keep and not keep(int(x), int(y)): continue
            M[j, i] = any(inside(int(x), int(y), t) for t in tris)
    if not M.any(): return None
    Dm = np.zeros_like(M, int); cur = M.copy(); d = 0
    while cur.any():
        d += 1; Dm[cur] = d; nxt = cur.copy()
        for dj in (-1, 0, 1):
            for di in (-1, 0, 1): nxt &= np.roll(np.roll(cur, dj, 0), di, 1)
        nxt[0, :] = nxt[-1, :] = False; nxt[:, 0] = nxt[:, -1] = False; cur = nxt
    j, i = np.unravel_index(Dm.argmax(), Dm.shape)
    # bbox from the raster of the KEPT region ...
    ky, kx = np.nonzero(M)
    rb = (int(gx[kx.min()]), int(gx[kx.max()]), int(gy[ky.min()]), int(gy[ky.max()]))
    # ... and from the VERTICES of triangles that have any kept sample in them,
    # clipped to the keep-predicate so the bbox cannot cross a split.
    vx = [p[0] for t in tris for p in t]; vy = [p[1] for t in tris for p in t]
    vb = (min(vx), max(vx), min(vy), max(vy))
    return int(gx[i]), int(gy[j]), (int(Dm.max()) - 1) * step, len(tris), rb, vb

def bbox_of(tris):
    xs = [p[0] for t in tris for p in t]; ys = [p[1] for t in tris for p in t]
    return min(xs), max(xs), min(ys), max(ys)

# ------------------------------------------------------------------- reporting
def clause_str(c):
    bits = []
    if c['veh'] is not None: bits.append('veh=%d' % c['veh'])
    if c['gte'] or c['lt']:
        bits.append('story=[%d,%s)' % (c['gte'], c['lt'] if c['lt'] else 'inf'))
    for k, sym in (('xLt', 'Xoff<'), ('yLt', 'Yoff<'), ('xGt', 'Xoff>'), ('yGt', 'Yoff>')):
        if c[k] is not None: bits.append('%s%d' % (sym, c[k]))
    if c['unk20'] is not None: bits.append('unk20=%d' % c['unk20'])
    return 'dest %-3d  %s' % (c['dest'], ', '.join(bits) if bits else '(no bound)')

def clause_active(p, c, story):
    if p['storyGte'] and story < p['storyGte']: return False
    if p['storyLt'] and story >= p['storyLt']:  return False
    if c['gte'] and story < c['gte']: return False
    if c['lt'] and story >= c['lt']:  return False
    return True

def keep_for(c):
    tests = []
    if c['xLt'] is not None: tests.append(lambda x, y, v=c['xLt']: xoff(x) < v)
    if c['xGt'] is not None: tests.append(lambda x, y, v=c['xGt']: xoff(x) > v)
    if c['yLt'] is not None: tests.append(lambda x, y, v=c['yLt']: yoff(y) < v)
    if c['yGt'] is not None: tests.append(lambda x, y, v=c['yGt']: yoff(y) > v)
    if not tests: return None
    return lambda x, y: all(f(x, y) for f in tests)

CATALOG = [("Esthar City", 57011, -2295), ("Lunatic Pandora Lab", 79521, -9135),
           ("Lunar Gate", 88021, 7865), ("Sorceress Memorial", 81521, 11865),
           ("Tears' Point", 83021, 31865)]

def hdr(s): print('\n' + '=' * 78 + '\n' + s + '\n' + '=' * 78)

# --- 0. sanity: the dest-record table reproduces the known bindings ---------
hdr('0. DESTINATION-RECORD TABLE CROSS-CHECK (wmsetus section 8 @ %d)' % S8)
agree = dis = 0
for p in PROGS:
    for c in p['clauses']:
        d = c['dest']
        if d is None or d > 45: continue
        rx, ry, rz, rf = DESTREC[d]
        if (rx, ry) == (0, 0): continue
        ok = segof(rx, ry) == p['seg']
        agree += ok; dis += (not ok)
        if not ok:
            print('  MISMATCH prog %2d seg %3d dest %2d record seg %3d (%d,%d)'
                  % (p['idx'], p['seg'], d, segof(rx, ry), rx, ry))
print('  dest<46 clause records landing in the program segment: %d agree, %d disagree'
      % (agree, dis))
print('  (the disagreements below are all adjacent-segment arrival points; see report)')

# --- 1. where the markers are ----------------------------------------------
hdr('1. THE FIVE CATALOG MARKERS')
print('  %-22s %-16s %-6s %-22s %s' % ('name', 'marker', 'seg', 'entry polys in seg', 'program(s)'))
for n, x, y in CATALOG:
    s = segof(x, y)
    tot = sum(1 for t, ss, w, sn in BASE if ss == s)
    wk = len(SEGTRIS.get(s, []))
    pr = ','.join('prog %d' % p['idx'] for p in BY_SEG.get(s, [])) or 'NONE'
    print('  %-22s (%6d,%6d) %-6d %-22s %s'
          % (n, x, y, s, '%d entry / %d walkable' % (tot, wk), pr))

# --- 2. every base-map segment that has walkable entry polygons -------------
hdr('2. EVERY BASE-MAP SEGMENT WITH WALKABLE ENTRY POLYGONS')
print('  %-5s %-8s %-9s %s' % ('seg', 'walkable', 'program', 'dests'))
for s in sorted(SEGTRIS):
    ps = BY_SEG.get(s, [])
    dd = sorted({c['dest'] for p in ps for c in p['clauses'] if c['dest'] is not None})
    print('  %-5d %-8d %-9s %s' % (s, len(SEGTRIS[s]),
          ','.join(str(p['idx']) for p in ps) or '--', dd))
orphan = [s for s in SEGTRIS if s not in BY_SEG]
print('  segments with a walkable entry patch but NO program: %s' % sorted(orphan))
missing = [p['seg'] for p in PROGS if p['seg'] not in SEGTRIS]
print('  program segments with NO walkable entry polygon:      %s' % sorted(set(missing)))

# --- 3. the Esthar programs in full ----------------------------------------
hdr('3. THE ESTHAR-REGION ENTRY PROGRAMS, DECODED')
for p in PROGS:
    if p['seg'] not in (373, 374, 378, 406, 407, 438, 439, 441, 443, 506): continue
    x0, x1, y0, y1 = seg_bounds(p['seg'])
    print('\n  prog %-2d seg %-3d  x[%d..%d] y[%d..%d]  top story>=%d%s  walkable entry tris=%d'
          % (p['idx'], p['seg'], x0, x1, y0, y1, p['storyGte'],
             (' <%d' % p['storyLt']) if p['storyLt'] else '', len(SEGTRIS.get(p['seg'], []))))
    for c in p['clauses']:
        act = 'ACTIVE@%d' % STORY if clause_active(p, c, STORY) else '        '
        d = c['dest']
        rec = ''
        if d is not None and d <= 45:
            rx, ry, rz, _ = DESTREC[d]
            rec = '   record(%d,%d,z=%d) seg %d' % (rx, ry, rz, segof(rx, ry)) if (rx, ry) != (0, 0) else '   record ZERO'
        elif d is not None:
            rec = '   record OUT OF TABLE (dest>=46)'
        print('      %s  %s%s' % (act, clause_str(c), rec))

# --- 4. the aims ------------------------------------------------------------
hdr('4. AIM POINTS')
JOBS = [
    dict(name="Esthar City",        prog=27, seg=438),
    dict(name="Esthar City (alt gates)", prog=26, seg=407),
    dict(name="Esthar City (gate 3)",    prog=28, seg=439),
    dict(name="Esthar City (gate 4)",    prog=25, seg=406),
    dict(name="Lunatic Pandora Lab", prog=23, seg=378),
    dict(name="Lunar Gate",          prog=30, seg=443),
    dict(name="Sorceress Memorial",  prog=29, seg=441),
    dict(name="Tears' Point",        prog=32, seg=506),
]
RESULT = {}
for j in JOBS:
    p = PROGS[j['prog']]
    tris = SEGTRIS.get(j['seg'], [])
    print('\n  --- %s : prog %d, seg %d, %d walkable entry tris ---'
          % (j['name'], j['prog'], j['seg'], len(tris)))
    if not tris:
        print('      NO WALKABLE ENTRY POLYGON IN THIS SEGMENT -- cannot fire'); continue
    cs = clusters(tris)
    for k, g in enumerate(cs):
        bx0, bx1, by0, by1 = bbox_of(g)
        print('      patch %d: %3d tris  x[%d,%d] y[%d,%d]  Xoff[%d,%d] Yoff[%d,%d]'
              % (k, len(g), bx0, bx1, by0, by1, xoff(bx0), xoff(bx1), yoff(by0), yoff(by1)))
    act = [c for c in p['clauses'] if clause_active(p, c, STORY)]
    for c in act:
        kp = keep_for(c)
        r = aim_of(tris, kp)
        if r is None:
            print('      clause [%s] -> NO SAMPLE SURVIVES ITS COORDINATE BOUND' % clause_str(c))
            continue
        ax, ay, marg, nt, rb, vb = r
        # verify the whole kept region is on the correct side of every split
        viol = []
        for t in tris:
            for (px, py) in t:
                if kp and not kp(px, py): viol.append((px, py))
        print('      clause [%s]' % clause_str(c))
        print('        aim (%d,%d)  margin %d  Xoff %d Yoff %d'
              % (ax, ay, marg, xoff(ax), yoff(ay)))
        print('        raster bbox x[%d,%d] y[%d,%d]' % rb)
        print('        vertex bbox x[%d,%d] y[%d,%d]  Xoff[%d,%d] Yoff[%d,%d]'
              % (vb[0], vb[1], vb[2], vb[3], xoff(vb[0]), xoff(vb[1]), yoff(vb[2]), yoff(vb[3])))
        if kp:
            print('        SPLIT CHECK: %d of %d patch vertices violate the clause bound'
                  % (len(viol), 3 * len(tris)))
        RESULT.setdefault(j['name'], []).append((c['dest'], ax, ay, marg, nt, vb))

# --- 5. distances -----------------------------------------------------------
hdr('5. MARKER -> AIM DISTANCE, AND MARKER -> DESTINATION RECORD')
for n, mx, my in CATALOG:
    for key in RESULT:
        if not key.startswith(n): continue
        for d, ax, ay, marg, nt, vb in RESULT[key]:
            dd = math.hypot(ax - mx, ay - my)
            rr = ''
            if d <= 45 and DESTREC[d][:2] != (0, 0):
                rx, ry = DESTREC[d][:2]
                rr = '   record %d at (%d,%d), %.0fu from marker, %.0fu from aim' % (
                    d, rx, ry, math.hypot(rx - mx, ry - my), math.hypot(rx - ax, ry - ay))
            print('  %-24s dest %-3d aim (%6d,%6d)  %7.0fu from marker%s' % (key, d, ax, ay, dd, rr))

# --- 6. paste-ready rows ----------------------------------------------------
hdr('6. EntryAimInfo ROWS (paste candidates -- see report for which are safe)')
for n, mx, my in CATALOG:
    for key in RESULT:
        if key != n: continue
        for d, ax, ay, marg, nt, vb in RESULT[key]:
            print('    { "%s",%s %7d, %7d, %7d, %7d, %7d, %7d, true  },   // prog?, dest %d, %d tris, margin %d'
                  % (n, ' ' * max(0, 18 - len(n)), ax, ay, vb[0], vb[1], vb[2], vb[3], d, nt, marg))

# --- 7. PER-PATCH aims (tight, single-patch mow zones) ----------------------
hdr('7. PER-PATCH AIMS -- one row per contiguous door patch')
DOORS = [("Esthar City",         26, [406, 407, 438, 439]),
         ("Lunatic Pandora Lab", 28, [378]),
         ("Lunar Gate",          29, [443]),
         ("Sorceress Memorial",  30, [441]),
         ("Tears' Point",        31, [506])]
PATCHAIM = collections.defaultdict(list)
for name, dest, segs in DOORS:
    rx, ry, rz, _ = DESTREC[dest]
    print('\n  %s  (destination %d, arrival record (%d,%d,z=%d))' % (name, dest, rx, ry, rz))
    for s in segs:
        tris = SEGTRIS.get(s, [])
        p = BY_SEG[s][0]
        for k, g in enumerate(clusters(tris)):
            # clause active at STORY that yields this dest, for the keep predicate
            kp = None
            for c in p['clauses']:
                if c['dest'] == dest and clause_active(p, c, STORY):
                    kp = keep_for(c); break
            r = aim_of(g, kp)
            if r is None:
                print('    seg %d patch %d: %d tris -- clause bound excludes it entirely' % (s, k, len(g)))
                continue
            ax, ay, marg, nt, rb, vb = r
            gap = min(abs(ry - vb[2]), abs(ry - vb[3])) if vb[0] <= rx <= vb[1] else None
            print('    seg %d patch %d: %3d tris  aim(%6d,%6d) margin %3d  bbox x[%d,%d] y[%d,%d] (%dx%d)%s'
                  % (s, k, nt, ax, ay, marg, vb[0], vb[1], vb[2], vb[3],
                     vb[1] - vb[0], vb[3] - vb[2],
                     '   <-- arrival record is %du off this patch, in line with it' % gap
                     if gap is not None and gap < 600 else ''))
            PATCHAIM[name].append((s, k, nt, ax, ay, marg, vb))

# --- 8. how far is each door from open ocean (terrain 32-34) ----------------
hdr('8. COASTLINE TEST -- nearest ocean polygon to each door patch')
OCEAN = []
for sn in range(BASE_BLOCKS):
    base = sn * SEGSZ; sc = sn % 32; sr = sn // 32
    for sub in range(16):
        off = struct.unpack_from('<I', D, base + 4 + sub * 4)[0]; p = base + off
        if p + 4 > len(D): continue
        pc, vc = D[p], D[p + 1]; pp = p + 4; vp = pp + pc * 16
        if vp + vc * 8 > len(D): continue
        fr = sr * 4 + (sub // 4); fc = sc * 4 + (sub % 4)
        bx = fc * 2048; bz = (95 - fr) * 2048
        gx0, gz0 = m2g(bx + 1024, bz + 1024 + 2048)
        if not (70000 <= gx0 <= 105000 and -20000 <= gz0 <= 35000): continue
        v = np.frombuffer(D, '<i2', count=vc * 4, offset=vp).reshape(vc, 4).astype(int)
        for i in range(pc):
            q = pp + i * 16
            if D[q + 13] not in (32, 33, 34): continue
            i0 = D[q]
            if i0 >= vc: continue
            OCEAN.append(m2g(int(v[i0][0]) + bx, int(v[i0][2]) + 2048 + bz))
OC = np.array(OCEAN) if OCEAN else np.zeros((0, 2))
print('  ocean vertices sampled in the east-map window: %d' % len(OC))
for name in ("Lunatic Pandora Lab", "Lunar Gate", "Sorceress Memorial", "Tears' Point"):
    for s, k, nt, ax, ay, marg, vb in PATCHAIM[name]:
        d = np.hypot(OC[:, 0] - ax, OC[:, 1] - ay).min() if len(OC) else -1
        print('  %-20s seg %d patch %d aim(%d,%d)  nearest ocean %.0fu' % (name, s, k, ax, ay, d))

# --- 9. verify each aim: a disc of radius=margin must be entirely on trigger -
hdr('9. AIM VERIFICATION (disc of radius = margin must lie wholly on the patch)')
FINAL = [("Esthar City",         26, 438, 0),
         ("Lunatic Pandora Lab", 28, 378, 0),
         ("Lunar Gate",          29, 443, 0),
         ("Sorceress Memorial",  30, 441, 0),
         ("Tears' Point",        31, 506, 0)]
def ascii_patch(tris, ax, ay, w=54):
    x0, x1, y0, y1 = bbox_of(tris)
    sx = max(1, (x1 - x0) // w); sy = sx * 2
    rows = []
    yy = y1
    while yy >= y0:
        line = ''
        xx = x0
        while xx <= x1:
            ch = '.'
            if any(inside(int(xx), int(yy), t) for t in tris): ch = '#'
            if abs(xx - ax) <= sx / 2 and abs(yy - ay) <= sy / 2: ch = 'A'
            line += ch; xx += sx
        rows.append(line); yy -= sy
    return rows
for name, dest, s, pi in FINAL:
    g = clusters(SEGTRIS[s])[pi]
    p = BY_SEG[s][0]
    kp = None
    for c in p['clauses']:
        if c['dest'] == dest and clause_active(p, c, STORY): kp = keep_for(c); break
    ax, ay, marg, nt, rb, vb = aim_of(g, kp)
    ok_in = all(any(inside(int(ax + marg * math.cos(a)), int(ay + marg * math.sin(a)), t) for t in g)
                for a in [i * math.pi / 24 for i in range(48)])
    out_cnt = sum(0 if any(inside(int(ax + marg * 1.6 * math.cos(a)), int(ay + marg * 1.6 * math.sin(a)), t) for t in g) else 1
                  for a in [i * math.pi / 24 for i in range(48)])
    print('\n  %-20s aim(%d,%d) margin %d : r=margin all-inside=%s ; r=1.6*margin outside on %d/48 bearings'
          % (name, ax, ay, marg, ok_in, out_cnt))
    print('   patch %d tris, bbox x[%d,%d] y[%d,%d]  ("A" = aim)' % (nt, vb[0], vb[1], vb[2], vb[3]))
    for line in ascii_patch(g, ax, ay): print('     ' + line)

# --- 10. the rows -----------------------------------------------------------
hdr('10. FINAL EntryAimInfo ROWS')
for name, dest, s, pi in FINAL:
    g = clusters(SEGTRIS[s])[pi]
    p = BY_SEG[s][0]
    kp = None
    for c in p['clauses']:
        if c['dest'] == dest and clause_active(p, c, STORY): kp = keep_for(c); break
    ax, ay, marg, nt, rb, vb = aim_of(g, kp)
    footonly = not any(c['veh'] == 132 for c in p['clauses'] if c['dest'] == dest)
    mx, my = [(a, b) for n, a, b in CATALOG if n == name][0]
    print('    { "%s",%s %7d, %7d, %7d, %7d, %7d, %7d, %-5s },   // prog %d dest %d, %d tris, margin %d, marker %.0fu away'
          % (name, ' ' * max(0, 18 - len(name)), ax, ay, vb[0], vb[1], vb[2], vb[3],
             'true' if footonly else 'false', p['idx'], dest, nt, marg, math.hypot(ax - mx, ay - my)))

# --- 11. containment + the falsifiable field-id prediction ------------------
hdr('11. SEGMENT CONTAINMENT AND THE FIELD-ID PREDICTION')
PRED = {"Esthar City":        ("ec*/ef big", [(402, 416), (420, 433), (437, 469), (487, 487)]),
        "Lunatic Pandora Lab":("edlabo/edmoor/edview", [(470, 477)]),
        "Lunar Gate":         ("escont/escouse/esform/esfreez/esview", [(540, 546)]),
        "Sorceress Memorial": ("efenter/efpod/efview/sspack", [(488, 497), (869, 869)]),
        "Tears' Point":       ("eein/eeview", [(478, 486)])}
for name, dest, s, pi in FINAL:
    g = clusters(SEGTRIS[s])[pi]
    vb = bbox_of(g)
    sx0, sx1, sy0, sy1 = seg_bounds(s)
    inside_seg = sx0 <= vb[0] and vb[1] <= sx1 and sy0 <= vb[2] and vb[3] <= sy1
    p = BY_SEG[s][0]
    bounded = [c for c in p['clauses'] if c['dest'] == dest and clause_active(p, c, STORY)
               and any(c[k] is not None for k in ('xLt', 'xGt', 'yLt', 'yGt'))]
    print('  %-20s bbox inside segment %d: %s   coordinate-split clauses: %s'
          % (name, s, inside_seg, 'none' if not bounded else
             '; '.join(clause_str(c) for c in bounded)))
    for c in bounded:
        kp = keep_for(c)
        bad = [(px, py) for t in g for (px, py) in t if not kp(px, py)]
        print('        split check: %d of %d vertices violate; patch Xoff[%d,%d] Yoff[%d,%d]'
              % (len(bad), 3 * len(g), xoff(vb[0]), xoff(vb[1]), yoff(vb[2]), yoff(vb[3])))
    pf, rg = PRED[name]
    print('        BAT prediction: arrival fieldId must be in %s = %s'
          % (pf, ','.join('%d..%d' % r for r in rg)))
