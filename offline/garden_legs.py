import sys, json, time, math
sys.path.insert(0, '.')
import gexec2

P = {p['name']: p for p in json.load(open('./park_final.json'))}

REQ = [("Fire Cavern", "Timber"),
       ("Timber", "Centra Ruins"),
       ("Centra Ruins", "Shumi Village"),
       ("Shumi Village", "Winhill"),
       ("Winhill", "Balamb Town"),
       ("Balamb Town", "Trabia Garden"),
       ("Trabia Garden", "Edea's House"),
       ("Edea's House", "Fisherman's Horizon"),
       ("Fisherman's Horizon", "White SeeD Ship")]

# stand-ins so the chain still exercises the same water for the legs whose
# endpoints have no berth at all
SUB = [("Centra Ruins", "Chocobo Forest 7"),
       ("Chocobo Forest 7", "Winhill"),
       ("Trabia Garden", "Chocobo Forest 2"),
       ("Chocobo Forest 2", "White SeeD Ship"),
       ("Winhill", "Centra Ruins")]          # the exact route the BAT wedged on

COMBOS = [(64, 32), (64, 16), (64, 48), (48, 20)]


def leg(a, b, tag):
    pa, pb = P[a]['park'], P[b]['park']
    if not pa or not pb:
        print("%-46s SKIPPED -- %s has no berth" %
              ("%s -> %s" % (a, b), a if not pa else b), flush=True)
        return
    res = {}
    for label, bug in (("v0.20.56", True), ("v0.20.57", False)):
        worst = None
        for sp, tn in COMBOS:
            r = gexec2.run(pa[0], pa[1], 0, pb[0], pb[1],
                           speed=sp, turn=tn, buggy_probe=bug)
            key = (0 if r['ok'] else 1, r['replans'], r['frames'])
            if worst is None or key > worst[0]:
                worst = (key, r, (sp, tn))
        res[label] = worst
    o, n = res["v0.20.56"], res["v0.20.57"]
    print("%-46s %-9s | .56: %-9s rp=%-2d gd=%-5d | .57: %-9s rp=%-2d gd=%-5d" %
          ("%s -> %s" % (a, b), tag,
           o[1]['why'], o[1]['replans'], o[1].get('gd', 0),
           n[1]['why'], n[1]['replans'], n[1].get('gd', 0)), flush=True)


t0 = time.time()
print("=== the nine requested legs (worst of 4 speed/turn combos each) ===", flush=True)
for a, b in REQ:
    leg(a, b, "requested")
print("\n=== stand-ins for the legs whose endpoints have no berth ===", flush=True)
for a, b in SUB:
    leg(a, b, "stand-in")
print("\nelapsed %.0fs" % (time.time() - t0))
