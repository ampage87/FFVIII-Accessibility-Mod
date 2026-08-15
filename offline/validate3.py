import sys, json, time, math
sys.path.insert(0,'/root/work')
import gexec3
STARTS=[("BGhom",24640,-29376),("Balamb",13632,-26816),("Timber",-22592,-4544),
        ("Trabia",48832,-57792),("Centra",7232,55104),("Winhill",-49984,6336),
        ("Choco5",17472,20288),("Alien1",78784,-61120)]
COMBOS=[(64,32),(64,16),(64,48),(48,20)]
cands=json.load(open('/root/work/berth_cands3.json'))
final=[]
t0=time.time()
for rec in cands:
    nm=rec['name']
    if not rec.get('park'):
        print("%-26s UNREACHABLE"%nm, flush=True); final.append({"name":nm,"park":None}); continue
    chosen=None
    for (px,py,w) in rec['cands']:
        bad=[]
        for snm,sx,sy in STARTS:
            ok=True
            for sp,tn in COMBOS:
                r=gexec3.run(sx,sy,0,px,py,speed=sp,turn=tn)
                if not r['ok']: ok=False; break
            if not ok: bad.append(snm)
        if not bad:
            chosen=(px,py,w); break
    if chosen: print("%-26s park=(%7d,%7d) walk=%5d"%(nm,chosen[0],chosen[1],chosen[2]), flush=True)
    else:      print("%-26s NO DRIVABLE CANDIDATE"%nm, flush=True)
    final.append({"name":nm,"park":list(chosen[:2]) if chosen else None,
                  "walk":chosen[2] if chosen else 0})
json.dump(final,open('/root/work/park_final3.json','w'),indent=1)
print("elapsed %.0fs"%(time.time()-t0))
