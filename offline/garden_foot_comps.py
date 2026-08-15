"""Foot connectivity, with the gate applied at the faithful scale.

Same error as the Garden shelf bug: the engine tests |dH| >= 200 between
polygons ONE MOVE STEP apart, and the FOOT move step is 0x20 = 32 units
(FF8_EN.exe 0x53E7A0:0x53E84D -- foot 0x20, car/Garden 0x40, Ragnarok 0x100).
A 128-unit grid cell is FOUR foot steps, so the faithful budget between
adjacent cells is 4 x 200 = 800, not 200. Applying 200 fragments landmasses
that are really one walk.
"""
import numpy as np, scipy.sparse as sp
from scipy.sparse.csgraph import connected_components
COLS=2048; ROWS=1536
z=np.load("./wmgrid.npz"); hgt=z['hgt'].astype(np.int32); b15=z['b15']
N=ROWS*COLS; idx=np.arange(N).reshape(ROWS,COLS)
def comps(mask,gate,name):
    rows=[];cols=[]
    for dr,dc in [(0,1),(1,0),(1,1),(1,-1)]:
        if dr==0: A,B,HA,HB,IA,IB=mask,np.roll(mask,-dc,1),hgt,np.roll(hgt,-dc,1),idx,np.roll(idx,-dc,1)
        else:
            A=mask[:-dr];B=np.roll(mask,-dc,1)[dr:];HA=hgt[:-dr];HB=np.roll(hgt,-dc,1)[dr:]
            IA=idx[:-dr];IB=np.roll(idx,-dc,1)[dr:]
        ok=A&B&(np.abs(HA.astype(np.int64)-HB.astype(np.int64))<gate)
        rows.append(IA[ok]); cols.append(IB[ok])
    r=np.concatenate(rows); c=np.concatenate(cols)
    n,lab=connected_components(sp.coo_matrix((np.ones(len(r),np.int8),(r,c)),shape=(N,N)),directed=False)
    lab=np.where(mask,lab.reshape(ROWS,COLS),-1)
    sizes=np.bincount(lab[mask].ravel())
    print("%s gate=%d: %d land cells, biggest components: %s"%(name,gate,int(mask.sum()),sorted(sizes)[-6:]))
    return lab
foot=(b15&0x80)!=0
old=comps(foot,200,"foot")
new=comps(foot,800,"foot")
np.save("./lab_foot800.npy",new)
def g2c8(gx,gz): return ((0x48000-int(gz))%0x30000)//128, ((int(gx)+0x60000)%0x40000)//128
def snap(m,r0,c0,rad=64):
    if m[r0,c0]: return r0,c0
    for k in range(1,rad+1):
        for r in range(max(0,r0-k),min(ROWS,r0+k+1)):
            for c in range(c0-k,c0+k+1):
                if max(abs(r-r0),abs(c-c0))!=k: continue
                if m[r,c%COLS]: return r,c%COLS
    return None
print()
print("%-26s %12s %12s"%("location","comp @200","comp @800"))
for nm,gx,gy in [("Shumi Village",10362,-76967),("Edea's House",-23150,62853),
                 ("Fisherman's Horizon",48811,-1653),("Chocobo Forest 2",10927,-81010),
                 ("Chocobo Forest 7",-20953,68906),("Centra Ruins",6887,55285),
                 ("Esthar City",57011,-2295),("Great Salt Lake",49888,-2683)]:
    fs=snap(foot,*g2c8(gx,gy))
    o=int((old==old[fs]).sum()); n=int((new==new[fs]).sum())
    print("%-26s %12d %12d"%(nm,o,n))
