import numpy as np, scipy.sparse as sp
from scipy.sparse.csgraph import connected_components
CELL=128; COLS=2048; ROWS=1536; GATE=200
z=np.load("./wmgrid.npz"); hgt=z['hgt'].astype(np.int32); b15=z['b15']
N=ROWS*COLS
idx=np.arange(N).reshape(ROWS,COLS)
def comps(mask,name):
    H=hgt
    rows=[];cols=[]
    # neighbour offsets: (dr,dc) with wrap on columns only
    for dr,dc in [(0,1),(1,0),(1,1),(1,-1)]:
        if dr==0:
            A=mask; B=np.roll(mask,-dc,axis=1)
            HA=H;   HB=np.roll(H,-dc,axis=1)
            IA=idx; IB=np.roll(idx,-dc,axis=1)
        else:
            A=mask[:-dr]; B=np.roll(mask,-dc,axis=1)[dr:]
            HA=H[:-dr];   HB=np.roll(H,-dc,axis=1)[dr:]
            IA=idx[:-dr]; IB=np.roll(idx,-dc,axis=1)[dr:]
        ok=A&B&(np.abs(HA.astype(np.int64)-HB.astype(np.int64))<GATE)
        rows.append(IA[ok]); cols.append(IB[ok])
    r=np.concatenate(rows); c=np.concatenate(cols)
    g=sp.coo_matrix((np.ones(len(r),np.int8),(r,c)),shape=(N,N))
    n,lab=connected_components(g,directed=False)
    lab=lab.reshape(ROWS,COLS)
    lab=np.where(mask,lab,-1)
    print(name,"components:",n,"walkable cells:",int(mask.sum()))
    return lab
foot=(b15&0x80)!=0
gard=(b15&0x20)!=0
land=(b15&0x02)!=0
lf=comps(foot,"foot"); np.save("./lab_foot.npy",lf)
lg=comps(gard,"garden"); np.save("./lab_gard.npy",lg)
np.save("./mask_land.npy",land)
