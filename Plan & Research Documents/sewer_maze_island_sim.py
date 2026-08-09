import json
data=json.load(open('/tmp/sewer_data.json'))
def isl(f,tri):
    c=data[f]['comp']; 
    return c[tri] if (tri is not None and 0<=tri<len(c)) else '?'
# gate/mechanism tris per field (from mod SET3-DIAG, authoritative)
GATES={
 'glwater1':{'seigyo':None},
 'glwater2':{'saku2':69,'saku3':48,'saku4':96},
 'glwater3':{'ct_lf':83,'ct_rt':138,'ct_rt2':147,'rt_up':181},
 'glwater4':{'ct_lf_dw':59,'lf_up':73,'ct_lt_up':165,'ct_rt_dw':194,'ct_rt_dw2':194,'rt_up':199},
 'glwater5':{'ct_rt_up':146},
}
# player tris observed in log (tri: reachableCount)
VISITS={
 'glwater1':{81:50,104:46,58:110,49:14},
 'glwater2':{70:91,101:91,94:124},
 'glwater3':{77:36,53:171,203:167,162:167,14:191,137:167,174:167},
 'glwater4':{58:51,56:44,61:51,48:51,170:64},
 'glwater5':{53:47,170:64,68:109,149:103},
}
order=['glwater1','glwater2','glwater3','glwater4','glwater5']
FIELDID={'glwater1':762,'glwater2':763,'glwater3':764,'glwater4':765,'glwater5':766}
print("MAZE: glwater1(762)->2(763)->3(764)->4(765)->5(766)->glwitch1(767=Gateway)\n")
for f in order:
    ntri=data[f]['ntri']; ncomp=data[f]['ncomp']; comp=data[f]['comp']
    from collections import Counter
    csz=Counter(comp)
    # visited islands
    visI={}
    for tri,rc in VISITS[f].items():
        visI.setdefault(isl(f,tri),[]).append((tri,rc))
    print("===== %s (%d)  %d tri, %d islands sizes=%s ====="%(f,FIELDID[f],ntri,ncomp,dict(csz)))
    print("  visited islands: "+", ".join("isl%s(via tri %s, reach~%d)"%(k,v[0][0],v[0][1]) for k,v in sorted(visI.items(),key=lambda x:str(x[0]))))
    for g,tri in GATES[f].items():
        i=isl(f,tri) if tri is not None else '?'
        vis='VISITED' if i in visI else ('no-pos' if tri is None else 'NOT-visited-island')
        print("    gate %-10s tri=%-4s island=%-3s  %s"%(g,tri,i,vis))
