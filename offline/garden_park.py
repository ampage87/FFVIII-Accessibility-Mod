"""Final Garden park table: a park point must be valid on BOTH grids --
Garden-parkable + foot-standable on the engine-accurate 128u grid, AND sitting
in a conservative 256u planner cell, so the route the planner returns actually
ends where the hull can stop."""
import sys, json, numpy as np
sys.path.insert(0, '.')
from gsim2 import GWp, PKp, HP, RP, KP, CP, g2cp, cp2g, GATE
import scipy.sparse as sp
from scipy.sparse.csgraph import connected_components

exec(open('./loclist.py').read())
z = np.load("./wmgrid.npz"); B15 = z['b15']
FT8 = (B15 & 0x80) != 0
lf8 = np.load("./lab_foot.npy")
C8 = 128


def g2c8(gx, gz):
    return ((0x48000-int(gz)) % 0x30000)//C8, ((int(gx)+0x60000) % 0x40000)//C8


# Garden components on the conservative planner grid
N = RP*KP; idx = np.arange(N).reshape(RP, KP); rows = []; cols = []
for dr, dc in [(0, 1), (1, 0), (1, 1), (1, -1)]:
    if dr == 0:
        A, B, HA, HB, IA, IB = GWp, np.roll(GWp, -dc, 1), HP, np.roll(HP, -dc, 1), idx, np.roll(idx, -dc, 1)
    else:
        A = GWp[:-dr]; B = np.roll(GWp, -dc, 1)[dr:]; HA = HP[:-dr]
        HB = np.roll(HP, -dc, 1)[dr:]; IA = idx[:-dr]; IB = np.roll(idx, -dc, 1)[dr:]
    ok = A & B & (np.abs(HA.astype(np.int64)-HB.astype(np.int64)) < GATE)
    rows.append(IA[ok]); cols.append(IB[ok])
r = np.concatenate(rows); c = np.concatenate(cols)
_, lab = connected_components(sp.coo_matrix((np.ones(len(r), np.int8), (r, c)), shape=(N, N)), directed=False)
LG = np.where(GWp, lab.reshape(RP, KP), -1)
GC = LG[g2cp(24576, -29406)]

# foot component + foot-standable, sampled at each planner cell's centre
rr8 = (np.arange(RP)*2+1)[:, None]; cc8 = (np.arange(KP)*2+1)[None, :]
LFat = lf8[rr8, cc8]
FTat = FT8[rr8, cc8]
RRp, CCp = np.meshgrid(np.arange(RP), np.arange(KP), indexing='ij')


def snap8(mask, r0, c0, rad=64):
    if mask[r0, c0]:
        return r0, c0
    for k in range(1, rad+1):
        for r in range(max(0, r0-k), min(1536, r0+k+1)):
            for cc in range(c0-k, c0+k+1):
                if max(abs(r-r0), abs(cc-c0)) != k:
                    continue
                if mask[r, cc % 2048]:
                    return r, cc % 2048
    return None


out = []
for nm, gx, gy in LOC:
    r8, c8 = g2c8(gx, gy)
    fs = snap8(FT8, r8, c8)
    rec = {"name": nm, "x": gx, "y": gy}
    if fs is None:
        rec["park"] = None; rec["why"] = "no foot ground"; out.append(rec); continue
    fc = lf8[fs]
    m = (LG == GC) & PKp & FTat & (LFat == fc)
    if not m.any():
        rec["park"] = None; rec["why"] = "no Garden-parkable cell on the destination's foot landmass"
        out.append(rec); continue
    pr, pc = g2cp(gx, gy)
    dr = (RRp-pr).astype(np.float32)
    dc = np.minimum(np.abs(CCp-pc), KP-np.abs(CCp-pc)).astype(np.float32)
    d = np.where(m, np.sqrt(dr*dr+dc*dc), 1e9)
    k = int(np.argmin(d)); a, b = divmod(k, KP)
    px, py = cp2g(a, b)
    rec["park"] = [int(px), int(py)]
    rec["walk"] = int(round(float(d[a, b])*CP))
    rec["cand"] = int(m.sum())
    out.append(rec)
json.dump(out, open('./park_final.json', 'w'), indent=1)
nreach = sum(1 for r in out if r["park"])
print("reachable %d / %d" % (nreach, len(out)))
for rec in out:
    if rec["park"]:
        print("%-26s park=(%7d,%7d) walk=%6d cand=%6d" % (rec["name"], rec["park"][0], rec["park"][1], rec["walk"], rec["cand"]))
    else:
        print("%-26s -- UNREACHABLE: %s" % (rec["name"], rec["why"]))
