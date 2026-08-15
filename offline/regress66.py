import sys, json, time
sys.path.insert(0,'/root/work')
import gexec3
P={r['name']:r for r in json.load(open('/root/work/park_final3.json'))}
P["Fisherman's Horizon"]={"name":"Fisherman's Horizon","park":[20480,-2560]}
NAMES=[n for n,r in P.items() if r.get('park')]
STARTS=[("BGhome",24640,-29376),("Timber",-22592,-4544),("Winhill",-49984,6336),
        ("FH",20480,-2560),("Trabia",48832,-57920),("Centra",7232,55104)]
COMBOS=[(64,32),(64,16),(48,20)]
t0=time.time(); bad=[]; n=0
for nm in NAMES:
    tgt=P[nm]['park']
    for snm,sx,sy in STARTS:
        if abs(sx-tgt[0])<400 and abs(sy-tgt[1])<400: continue
        for sp,tn in COMBOS:
            n+=1
            r=gexec3.run(sx,sy,0,tgt[0],tgt[1],speed=sp,turn=tn)
            if not r['ok']: bad.append("%s<-%s @%d/%d %s"%(nm,snm,sp,tn,r['why']))
    print("  %-26s ok so far, %d runs, %d bad"%(nm,n,len(bad)), flush=True)
print("\n%d runs, %d failures  (%.0fs)"%(n,len(bad),time.time()-t0))
for b in bad[:20]: print("   ",b)
