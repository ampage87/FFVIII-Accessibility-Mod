"""Offline FF8 field parser: .jsm / .sym / .msd / .id / .inf / .ca / .pmp"""
import struct, os

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fielddata', 'out')

def fields():
    return sorted(d for d in os.listdir(DATA) if os.path.isdir(os.path.join(DATA, d)))

def path(f, ext):
    p = os.path.join(DATA, f, f + ext)
    return p if os.path.exists(p) else None

def rd(f, ext):
    p = path(f, ext)
    return open(p, 'rb').read() if p else None

# ---------------- SYM ----------------
def sym_lines(f):
    b = rd(f, '.sym')
    if b is None: return []
    lines = [x.rstrip() for x in b.decode('latin1').split('\n')]
    while lines and not lines[-1]: lines.pop()
    return lines

def load_sym(f, nbare):
    """The SYM leading bare list covers Others+Lines+Backgrounds ONLY -- doors are
    omitted -- so its length is nO+nL+nB, not ngrp. The remainder is the method
    section, which DOES list every group including doors."""
    lines = sym_lines(f)
    return lines[:nbare], lines[nbare:]

def sym_groups(f, nbare):
    """From the method-list half: [(entname, [methodnames...]), ...] in declaration order."""
    _, meth = load_sym(f, nbare)
    out = []
    for l in meth:
        if '::' in l:
            if out: out[-1][1].append(l.split('::', 1)[1])
        else:
            out.append((l, []))
    return out

# ---------------- JSM ----------------
class Jsm:
    def __init__(self, f):
        d = rd(f, '.jsm')
        self.ok = d is not None and len(d) >= 8
        if not self.ok: return
        self.raw = d
        self.nD, self.nL, self.nB, self.nO = d[0], d[1], d[2], d[3]
        self.eoff, self.coff = struct.unpack_from('<HH', d, 4)
        self.ngrp = self.nD + self.nL + self.nB + self.nO
        self.groups = []
        for i in range(self.ngrp):
            w = struct.unpack_from('<H', d, 8 + 2*i)[0]
            self.groups.append((w & 0x7F, w >> 7))   # (count, start)
        nent = (self.coff - self.eoff) // 2
        self.entries = [struct.unpack_from('<H', d, self.eoff + 2*i)[0] & 0x7FFF
                        for i in range(nent)]
        self.code = list(struct.unpack_from('<%dI' % ((len(d) - self.coff)//4), d, self.coff))

    # ENGINE TRUTH (field_scripts_init @0x0052BC00): the group array is consumed
    # in the order LINES, DOORS, BACKGROUNDS, OTHERS -- not the header's byte order.
    def cat(self, g):
        if g < self.nL: return 'L'
        if g < self.nL + self.nD: return 'D'
        if g < self.nL + self.nD + self.nB: return 'B'
        return 'O'

    # Runtime script-object table @0x01D9D020 is indexed
    #   [0 .. nO-1] Others  [nO .. nO+nL-1] Lines
    #   [nO+nL .. +nB-1] Backgrounds  [.. +nD-1] Doors
    def slot_to_group(self, s):
        if s < self.nO: return self.nL + self.nD + self.nB + s
        s -= self.nO
        if s < self.nL: return s
        s -= self.nL
        if s < self.nB: return self.nL + self.nD + s
        s -= self.nB
        return self.nL + s

    def group_to_slot(self, g):
        c = self.cat(g)
        if c == 'L': return self.nO + g
        if c == 'D': return self.nO + self.nL + self.nB + (g - self.nL)
        if c == 'B': return self.nO + self.nL + (g - self.nL - self.nD)
        return g - (self.nL + self.nD + self.nB)

    CAT_STRIDE = {'O': 0x264, 'L': 0x1A0, 'B': 0x1B4, 'D': 0x18C}
    CAT_EXEC   = {'O': 0x10080002, 'L': 0x20000000, 'B': 0x80001000, 'D': 0x40000000}

    # A group owns cnt+1 method entries: entries[start .. start+cnt], and the
    # NEXT entry (start+cnt+1) is the end boundary. Method 0 -- the init, where
    # SETMODEL lives -- is entries[start], NOT entries[start+1].
    def method_count(self, g):
        return self.groups[g][0] + 1

    def method_range(self, g, m):
        cnt, start = self.groups[g]
        idx = start + m
        if m > cnt or idx + 1 >= len(self.entries): return None
        lo = self.entries[idx]
        hi = self.entries[idx + 1]
        if hi <= lo or hi > len(self.code): hi = len(self.code)
        return (lo, hi)

    def method_code(self, g, m):
        r = self.method_range(g, m)
        if not r: return []
        return self.code[r[0]:r[1]]

def decode(code):
    """-> list of (index, op, param_or_None)"""
    out = []
    for i, w in enumerate(code):
        if (w & 0xFF000000) == 0:
            out.append((i, w & 0xFFFFFF, None))
        else:
            op = w >> 24
            p = w & 0xFFFFFF
            if p & 0x800000: p -= 0x1000000
            out.append((i, op, p))
    return out

# ---------------- name resolution (engine-accurate) ----------------
def nbare(j):
    return j.nO + j.nL + j.nB

def group_names(f, j):
    """Return name for each JSM group index, resolved via the SYM method section.

    The SYM method section lists entities in CODE order (ascending group `start`).
    Matching it to the group array by start-rank gives group -> name with no
    reliance on the SYM's leading bare list, which is in code order too and
    therefore does NOT match the engine's per-category group ordering."""
    decl = sym_groups(f, nbare(j))
    if not decl: return [None]*j.ngrp
    rank = sorted(range(j.ngrp), key=lambda g: j.groups[g][1])
    names = [None]*j.ngrp
    for k, g in enumerate(rank):
        if k < len(decl): names[g] = decl[k][0]
    return names

def slot_names(f, j=None):
    """Runtime-slot-indexed names: index == the engine's script-object table index."""
    if j is None: j = Jsm(f)
    gn = group_names(f, j)
    return [gn[j.slot_to_group(s)] for s in range(j.ngrp)]

# ---------------- .id walkmesh ----------------
class Walkmesh:
    """FF8 PC .id: uint32 numTri; numTri x 24-byte triangles (3 verts of int16 x,y,z,pad);
    numTri x 6-byte access (3 x int16 neighbour, -1 = not crossable)."""
    def __init__(self, f):
        d = rd(f, '.id')
        self.ok = False
        self.tris = []
        self.access = []
        if d is None or len(d) < 4: return
        n = struct.unpack_from('<I', d, 0)[0]
        if n == 0 or n > 4096 or len(d) < 4 + n*30: return
        for t in range(n):
            o = 4 + t*24
            v = struct.unpack_from('<12h', d, o)
            # layout per vertex: x, y, z, pad
            self.tris.append((v[0], v[1], v[2], v[4], v[5], v[6], v[8], v[9], v[10]))
        ab = 4 + n*24
        for t in range(n):
            self.access.append(struct.unpack_from('<3h', d, ab + t*6))
        self.ok = True

    def centroid(self, t):
        v = self.tris[t]
        return ((v[0]+v[3]+v[6])/3.0, (v[1]+v[4]+v[7])/3.0)   # x, y (FF8 field plane)

    def contains(self, t, px, py):
        x1,y1,_,x2,y2,_,x3,y3,_ = self.tris[t]
        d1 = (px-x2)*(y1-y2)-(x1-x2)*(py-y2)
        d2 = (px-x3)*(y2-y3)-(x2-x3)*(py-y3)
        d3 = (px-x1)*(y3-y1)-(x3-x1)*(py-y1)
        neg = (d1<0) or (d2<0) or (d3<0); pos = (d1>0) or (d2>0) or (d3>0)
        return not (neg and pos)

    def find(self, px, py):
        for t in range(self.n):
            if self.contains(t, px, py): return t
        return -1

    @property
    def n(self): return len(self.tris)

# ---------------- .inf gateways / triggers ----------------
def load_inf(f):
    d = rd(f, '.inf')
    if d is None or len(d) < 676: return None
    gws = []
    for i in range(12):
        g = d[0x64 + i*32 : 0x64 + i*32 + 32]
        x1,y1,z1,x2,y2,z2,dx,dy,dz = struct.unpack_from('<9h', g, 0)
        fid = struct.unpack_from('<H', g, 18)[0]
        gws.append(dict(i=i, x1=x1,y1=y1,z1=z1, x2=x2,y2=y2,z2=z2,
                        dx=dx,dy=dy,dz=dz, fieldId=fid))
    trg = []
    for i in range(12):
        t = d[0x1E4 + i*16 : 0x1E4 + i*16 + 16]
        x1,y1,z1,x2,y2,z2 = struct.unpack_from('<6h', t, 0)
        trg.append(dict(i=i, x1=x1,y1=y1,z1=z1, x2=x2,y2=y2,z2=z2,
                        doorId=t[12], flags=t[13]))
    return dict(name=d[:9].split(b'\0')[0].decode('latin1','replace'),
                gateways=gws, triggers=trg)

# ---------------- .msd messages ----------------
GLYPHS = None
def load_msd(f):
    d = rd(f, '.msd')
    if not d or len(d) < 4: return []
    n = struct.unpack_from('<I', d, 0)[0] // 4
    offs = [struct.unpack_from('<I', d, 4*i)[0] for i in range(n)]
    out = []
    for i, o in enumerate(offs):
        end = len(d)
        for p in offs:
            if o < p < end: end = p
        out.append(bytes(d[o:end]))
    return out
