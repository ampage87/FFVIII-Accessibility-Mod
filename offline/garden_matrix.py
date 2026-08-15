import sys, json, time
sys.path.insert(0,'.')
from gexec2 import run
from gsim2 import GWp, g2cp, wdist
exec(open('./loclist.py').read())
P1={p['name']:p for p in json.load(open('./park_final.json'))}
STARTS=[("BGhome",24576,-29406,0),("oceanE",60000,-45000,1024),("oceanSW",-60000,30000,2048),
        ("GalbCoast",-40000,-30000,3072),("northSea",20000,-88000,512),("centreSea",0,20000,3000),
        ("farWest",-100000,-40000,700),("SEsea",70000,60000,1800)]
PARAMS=[(64,32),(64,16),(48,20),(96,48)]
out={}; t0=time.time()
for nm,gx,gy in LOC:
    a=P1.get(nm); fine=None
    if a and a.get('park'): fine={'px':a['park'][0],'py':a['park'][1],'walk':a['walk']}
    if not fine:
        out[nm]={"reach":False}; print("%-26s UNREACHABLE BY GARDEN"%nm, flush=True); continue
    px,py,walk=fine['px'],fine['py'],fine['walk']
    row={"reach":True,"park":[px,py],"walk":walk,"runs":[]}; line=[]
    for sn,sx,sy,sh in STARTS:
        allok=True; worst=0; rp=0; why=set()
        for sp,tn in PARAMS:
            r=run(sx,sy,sh,px,py,speed=sp,turn=tn)
            if not r["ok"]: allok=False; why.add(r["why"])
            worst=max(worst,r["frames"]); rp=max(rp,r["replans"])
        row["runs"].append({"start":sn,"ok":allok,"frames":worst,"replans":rp,"why":sorted(why)})
        line.append("%s:%s"%(sn[:5],"OK" if allok else "/".join(sorted(why))))
    out[nm]=row
    print("%-26s park=(%7d,%7d) walk=%5d  %s"%(nm,px,py,walk," ".join(line)), flush=True)
json.dump(out,open('./sim5.json','w'),indent=1)
print("elapsed %.0fs"%(time.time()-t0))
