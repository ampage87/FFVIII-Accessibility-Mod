"""Berth table, validated by construction.

Candidate berths are scored (closest to the destination, mild preference for sea
room) and then SIMULATED. The first candidate the executor can actually reach
from two representative starts is the one that ships. A berth that looks good on
the map but wedges the hull is not a berth.
"""
import sys, json, numpy as np, time
sys.path.insert(0,'.')
import gexec2
from gsim2 import wdist
cands=json.load(open('./berth_cands.json'))
STARTS=[(24640,-29376,0),(-60000,30000,2048),(-40000,-30000,3072),(20000,-88000,512),
        (0,20000,3000),(-100000,-40000,700),(70000,60000,1800)]
COMBOS=[(64,32),(64,16),(48,20),(96,48)]
out=[]; t0=time.time()
for rec in cands:
    if not rec.get('cands'):
        out.append({k:rec[k] for k in ('name','x','y','why') if k in rec} | {"park":None}); continue
    chosen=None
    for i,(px,py,w) in enumerate(rec['cands'][:4]):
        good=True
        for sx,sy,sh in STARTS:
            for sp,tn in COMBOS:
                r=gexec2.run(sx,sy,sh,px,py,speed=sp,turn=tn)
                if not r['ok']: good=False; break
            if not good: break
        if good: chosen=(px,py,w,i); break
    if chosen:
        out.append({"name":rec['name'],"x":rec['x'],"y":rec['y'],
                    "park":[chosen[0],chosen[1]],"walk":chosen[2],"cand_rank":chosen[3]})
        print("%-26s berth=(%7d,%7d) walk=%5d  (candidate #%d)"%(rec['name'],chosen[0],chosen[1],chosen[2],chosen[3]),flush=True)
    else:
        out.append({"name":rec['name'],"x":rec['x'],"y":rec['y'],"park":None,
                    "why":"no berth the hull can reach"})
        print("%-26s -- no candidate berth was drivable"%rec['name'],flush=True)
json.dump(out,open('./park_final.json','w'),indent=1)
print("\nreachable %d / %d  (%.0fs)"%(sum(1 for r in out if r['park']),len(out),time.time()-t0))
