"""Berth table under the CLIFF-AWARE model (gsim3): fine-grid 200 gate."""
import sys, numpy as np, json
sys.path.insert(0,'/root/work')
import scipy.sparse as sp, scipy.ndimage as ndi
from scipy.sparse.csgraph import connected_components
ROWS,COLS,CELL=1536,2048,128
STEP_OFF=3; WALK_CAP=6000; GATE=200
z=np.load("/root/work/wmgrid.npz"); b15=z['b15']; hgt=z['hgt'].astype(np.int32)
foot=(b15&0x80)!=0; gard=(b15&0x20)!=0; park=(b15&0x02)!=0
labF=np.load("/root/work/lab_foot800.npy")
exec(open('/root/work/loclist.py').read())
def g2c8(gx,gz): return ((0x48000-int(gz))%0x30000)//CELL, ((int(gx)+0x60000)%0x40000)//CELL
def c2g8(r,c):
    mx=int(c)*CELL+CELL//2; mz=int(r)*CELL+CELL//2
    return (mx-0x60000+0x20000)%0x40000-0x20000,(0x48000-mz+0x18000)%0x30000-0x18000
idx=np.arange(ROWS*COLS).reshape(ROWS,COLS); rows=[];cols=[]
for dr,dc in [(0,1),(1,0)]:
    if dr==0: A,B,HA,HB,IA,IB=gard,np.roll(gard,-dc,1),hgt,np.roll(hgt,-dc,1),idx,np.roll(idx,-dc,1)
    else:
        A=gard[:-dr];B=gard[dr:];HA=hgt[:-dr];HB=hgt[dr:];IA=idx[:-dr];IB=idx[dr:]
    ok=A&B&(np.abs(HA.astype(np.int64)-HB.astype(np.int64))<GATE)
    rows.append(IA[ok]); cols.append(IB[ok])
r=np.concatenate(rows); c=np.concatenate(cols)
_,lg=connected_components(sp.coo_matrix((np.ones(len(r),np.int8),(r,c)),shape=(ROWS*COLS,ROWS*COLS)),directed=False)
labG=np.where(gard,lg.reshape(ROWS,COLS),-1)
def snap(m,r0,c0,rad=80):
    if m[r0,c0]: return r0,c0
    for k in range(1,rad+1):
        for rr in range(max(0,r0-k),min(ROWS,r0+k+1)):
            for cc in range(c0-k,c0+k+1):
                if max(abs(rr-r0),abs(cc-c0))!=k: continue
                if m[rr,cc%COLS]: return rr,cc%COLS
    return None
hull=(labG==labG[snap(gard,*g2c8(24576,-29406))])
print("hull component (fine, 200 gate): %d cells"%hull.sum())
from gsim3 import WALK as W3, CLEAR as C3
hull = hull & W3.repeat(2,axis=0).repeat(2,axis=1)[:ROWS,:COLS]
CLEARF = C3.repeat(2,axis=0).repeat(2,axis=1)[:ROWS,:COLS].astype(np.int32)
st=ndi.generate_binary_structure(2,2)
RR,CC=np.meshgrid(np.arange(ROWS),np.arange(COLS),indexing='ij')
out=[]
for nm,gx,gy in LOC:
    r0,c0=g2c8(gx,gy); fs=snap(foot,r0,c0)
    rec={"name":nm,"x":gx,"y":gy,"park":None}
    if fs is not None:
        D = park & foot & (labF==labF[fs])
        Dn = ndi.binary_dilation(D, st, iterations=STEP_OFF)
        H = hull & Dn
        if H.any():
            dr=(RR-r0).astype(np.float32); dc=np.minimum(np.abs(CC-c0),COLS-np.abs(CC-c0)).astype(np.float32)
            walkc=np.sqrt(dr*dr+dc*dc)
            miss=np.maximum(0,3-CLEARF).astype(np.float32)
            d=np.where(H,walkc+miss,1e9)
            flat=d.ravel(); order=np.argsort(flat)[:400]
            cl=[]
            for k in order:
                if flat[k]>1e8: break
                a,b=divmod(int(k),COLS)
                w=int(round(float(walkc[a,b])*CELL))
                if w>WALK_CAP: continue
                px,py=c2g8(a,b)
                if any(abs(px-q[0])<600 and abs(py-q[1])<600 for q in cl): continue
                cl.append((int(px),int(py),w))
                if len(cl)>=4: break
            if cl: rec["cands"]=cl; rec["park"]=[cl[0][0],cl[0][1]]; rec["walk"]=cl[0][2]
            else:  rec["why"]="beyond the %d-unit walk cap"%WALK_CAP
        else: rec["why"]="no Garden water reaches a legal step-off point on this landmass"
    else: rec["why"]="no foot ground"
    out.append(rec)
json.dump(out,open("/root/work/berth_cands3.json","w"),indent=1)
print("reachable %d / %d"%(sum(1 for x in out if x["park"]),len(out)))
for x in out:
    if x["park"]: print("%-26s berth=(%7d,%7d) walk=%5d"%(x['name'],x['park'][0],x['park'][1],x['walk']))
    else:         print("%-26s -- NOT REACHABLE: %s"%(x['name'],x['why']))
