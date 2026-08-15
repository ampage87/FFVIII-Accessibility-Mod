import sys, json, time
sys.path.insert(0,'/root/work')
import gexec3
P={r['name']:r for r in json.load(open('/root/work/park_final3.json'))}
P["Fisherman's Horizon"]={"name":"Fisherman's Horizon","park":[46464,-3200],"walk":0}
REQ=[("Fire Cavern","Timber"),("Timber","Centra Ruins"),("Centra Ruins","Shumi Village"),
     ("Shumi Village","Winhill"),("Winhill","Balamb Town"),("Balamb Town","Trabia Garden"),
     ("Trabia Garden","Edea's House"),("Edea's House","Fisherman's Horizon"),
     ("Fisherman's Horizon","White SeeD Ship"),("Balamb Garden","Fisherman's Horizon"),
     ("Balamb Town","Fisherman's Horizon"),("Deling City","Fisherman's Horizon")]
COMBOS=[(64,32),(64,16),(64,48),(48,20)]
t0=time.time()
for a,b in REQ:
    pa,pb=P[a]['park'],P[b]['park']
    worst=None
    for sp,tn in COMBOS:
        r=gexec3.run(pa[0],pa[1],0,pb[0],pb[1],speed=sp,turn=tn)
        k=(0 if r['ok'] else 1,r['replans'],r['frames'])
        if worst is None or k>worst[0]: worst=(k,r,(sp,tn))
    r=worst[1]
    print("%-46s %-10s rp=%-2d revs=%-2d gd=%-5s  (worst at speed %d turn %d)"%(
        "%s -> %s"%(a,b),r['why'],r['replans'],r['revs'],r.get('gd'),worst[2][0],worst[2][1]),flush=True)
print("elapsed %.0fs"%(time.time()-t0))
