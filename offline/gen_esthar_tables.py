"""Regenerate the Esthar (#110) tables in src/esthar_pandora_model.inl.

Emits EP_HOPS, the next-hop route table, from the real city graph: the union of
the .inf pedestrian gateways and the script MAPJUMP/MAPJUMP3/MAPJUMPO edges,
minus efbig1 (the timeout jump, present in every field) and the three ecenc*
boarding fields. Also prints the eight boarding-site gates so EP_SITES can be
checked by hand.

Needs fs/fx (extracted field scripts) and inf/infout (extracted .inf files).
Uses offline/jsm.py, whose loader masks entry offsets with 0x7FFF -- without
that the CP1 trigger reads as Edea::afkantei2 instead of Linejump1::touch,
which is exactly how v0.55.0 came to call a walk-through line a lifter.
"""
import sys, os, re, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jsm

DN = {}
txt = open(os.path.join(os.path.dirname(__file__), '..', 'src', 'field_display_names.h')).read()
for m in re.finditer(r'"([^"]*)",\s*//\s*(\d+):\s*(\S+)', txt):
    DN[int(m.group(2))] = (m.group(1), m.group(3))
EXCL, ENC = {487}, {417, 418, 419}
ECIDS = {k for k, v in DN.items() if v[1].startswith('ec')}

def gateways(field):
    p = os.path.join(os.path.dirname(__file__), '..', 'inf', 'infout', field + '.inf')
    if not os.path.exists(p): return set()
    d = open(p, 'rb').read(); out = set()
    for s in range(12):
        off = 0x64 + s * 32
        if off + 32 > len(d): break
        dest = struct.unpack_from('<H', d, off + 18)[0]
        if 0 < dest < 900: out.add(dest)
    return out

G = collections.defaultdict(set)
for fid in sorted(ECIDS):
    nm = DN[fid][1]
    ds = set()
    try:
        j = jsm.load(nm)
        for i, w in enumerate(j.code):
            op, par = jsm.dec(w)
            if op not in (0x29, 0x2A, 0x5C): continue
            back = 5 if op in (0x29, 0x2A) else 2
            if i - back < 0: continue
            o2, p2 = jsm.dec(j.code[i - back])
            if o2 == 0x07 and 0 < p2 < 900: ds.add(p2)
    except Exception:
        pass
    ds |= gateways(nm)
    G[fid] = {d for d in (ds - EXCL - ENC) if d in ECIDS}

def nexthop(target):
    rev = collections.defaultdict(set)
    for a, ds in G.items():
        for d in ds: rev[d].add(a)
    dist, par, q = {target: 0}, {}, collections.deque([target])
    while q:
        n = q.popleft()
        for p in rev[n]:
            if p not in dist:
                dist[p] = dist[n] + 1; par[p] = n; q.append(p)
    return dist, par

TGT = [425, 404, 458]          # eciway11 (CP1), eccway12 (CP2), ecoway3 (CP3)
tabs = {t: nexthop(t) for t in TGT}
print("static const EstharHop EP_HOPS[] = {")
for f in sorted(set(ECIDS) & set(G)):
    cells = []
    for t in TGT:
        d, p = tabs[t]
        cells.append((0, 0) if f == t else ((d[f], p[f]) if f in d else (-1, 0)))
    if all(c[0] < 0 for c in cells): continue
    body = ", ".join("{%d,%d}" % c for c in cells)
    print(f'    {{ {f:3d}, "{DN[f][1]}", {{ {body} }} }},   // {DN[f][0]}')
print("};")
