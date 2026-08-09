import struct, importlib.util, json, os
# import proven walkmesh/camera parsers from the checked-in extractor
spec=importlib.util.spec_from_file_location("ew","/sessions/rcw-01koictzhlca9qhsemquvf8v/mnt/FF8_OriginalPC_mod/Plan & Research Documents/extract_walkmeshes.py")
ew=importlib.util.module_from_spec(spec); spec.loader.exec_module(ew)
DATA="/sessions/rcw-01koictzhlca9qhsemquvf8v/mnt/FF8_OriginalPC_mod/Game Files/FINAL FANTASY VIII/Data/lang-en"
fl=open(DATA+"/field.fl",'rb').read().decode('latin1').replace('\r','').split('\n')
fi=open(DATA+"/field.fi",'rb').read()
def fientry(i): return struct.unpack_from('<III',fi,i*12)
def findfl(suf): return [i for i,l in enumerate(fl) if l.strip().lower().endswith(suf)]
def lzss(d): return ew.decompress_lzss(d, 1<<24)
def outer(i):
    us,off,comp=fientry(i)
    f=open(DATA+"/field.fs",'rb'); f.seek(off)
    if comp==0: data=f.read(us); f.close(); return data
    head=f.read(4); clen=struct.unpack('<I',head)[0]; cd=f.read(clen); f.close()
    return ew.decompress_lzss(cd, us)
def get_inner(field):
    fii=findfl(field+'.fi')[0]; fli=findfl(field+'.fl')[0]; fsi=findfl(field+'.fs')[0]
    ifi=outer(fii); ifl=outer(fli); ifs=outer(fsi)
    lines=ifl.decode('latin1').replace('\r','').split('\n')
    def ext(e):
        for k,l in enumerate(lines):
            if l.strip().lower().endswith('.'+e):
                us,off,comp=struct.unpack_from('<III',ifi,k*12)
                if comp==0: return ifs[off:off+us]
                clen=struct.unpack_from('<I',ifs,off)[0]; return ew.decompress_lzss(ifs[off+4:off+4+clen], us)
        return None
    return {e:ext(e) for e in ('id','ca','jsm','sym','inf')}

# ---- SYM name parser: FF8 field .sym is a list of null-terminated ASCII names ----
def parse_sym(sym):
    if not sym: return []
    # try: split on NUL, keep printable tokens
    toks=[]
    cur=bytearray()
    for b in sym:
        if 32<=b<127: cur.append(b)
        else:
            if len(cur)>=2:
                s=cur.decode('latin1')
                if s[0].isalpha(): toks.append(s)
            cur=bytearray()
    return toks

# ---- JSM per-entity facts ----
def parse_jsm(jsm):
    nD,nL,nB,nO=jsm[0],jsm[1],jsm[2],jsm[3]
    posFirst=struct.unpack_from('<H',jsm,4)[0]; posScripts=struct.unpack_from('<H',jsm,6)[0]
    nEnt=(posFirst-8)//2
    groups=[]
    for e in range(nEnt):
        raw=struct.unpack_from('<H',jsm,8+e*2)[0]; groups.append((raw&0x7F,raw>>7))
    nEP=(posScripts-posFirst)//2
    EP=[struct.unpack_from('<H',jsm,posFirst+i*2)[0]&0x7FFF for i in range(nEP)]
    nDW=(len(jsm)-posScripts)//4
    S=[struct.unpack_from('<I',jsm,posScripts+i*4)[0] for i in range(nDW)]
    ents=[]
    for e in range(nEnt):
        mc,st=groups[e]
        facts={'grp':e,'methods':mc,'set3_tri':None,'req':[],'mapjump':[],'guards':[],'ops':set(),'ladder':False,'drawpoint':False,'setmodel':False}
        for m in range(mc+1):
            mi=st+m
            if mi>=len(EP): break
            a=EP[mi]; b=EP[mi+1] if mi+1<len(EP) else nDW
            stack=[]
            ip=a
            while ip<b and ip<nDW:
                w=S[ip]; hb=w>>24
                if hb==0:
                    v=w&0xFFFFFF; v=v-0x1000000 if v&0x800000 else v
                    stack.append(v)
                else:
                    op=hb; p=w&0xFFFFFF
                    if p&0x800000: p-=0x1000000
                    facts['ops'].add(op)
                    if op==0x1C:
                        if stack: stack.pop()
                    elif op in (0x07,0x09,0x0A,0x0C,0x0D):
                        stack.append(('m',p))
                    else:
                        if op==0x1E and facts['set3_tri'] is None:
                            facts['set3_tri']=p  # opcParam = tri fallback
                        if op in (0x14,0x15,0x16): facts['req'].append(p)
                        if op in (0x29,0x2A): facts['mapjump'].append(p)
                        if op in (0x25,0x26,0x27,0x28): facts['ladder']=True
                        if op==0x137: facts['drawpoint']=True
                        if op==0x2B: facts['setmodel']=True
                ip+=1
        facts['ops']=sorted(facts['ops'])
        ents.append(facts)
    return {'counts':[nD,nL,nB,nO],'nEnt':nEnt,'ents':ents}

def components(mesh):
    n=mesh['num_triangles']; tris=mesh['triangles']; seen=[-1]*n; comp=0
    for s in range(n):
        if seen[s]!=-1: continue
        stack=[s]; seen[s]=comp
        while stack:
            c=stack.pop()
            for nb in tris[c]['neighbors']:
                if nb!=0xFFFF and nb<n and seen[nb]==-1:
                    seen[nb]=comp; stack.append(nb)
        comp+=1
    return seen,comp

FIELDS=['glfuryb1','glwater1','glwater2','glwater3','glwater4','glwater5']
out={}
for fld in FIELDS:
    inner=get_inner(fld)
    mesh=ew.parse_walkmesh(inner['id'],fld) if inner['id'] else None
    cam=ew.parse_camera(inner['ca']) if inner['ca'] else None
    if mesh and cam: ew.apply_camera_to_mesh(mesh,cam)
    names=parse_sym(inner['sym'])
    jsm=parse_jsm(inner['jsm']) if inner['jsm'] else None
    comp=components(mesh) if mesh else ([],0)
    out[fld]={'ntri':mesh['num_triangles'] if mesh else 0,'ncomp':comp[1],
              'comp':comp[0],'names':names[:60],
              'jsm_counts':jsm['counts'] if jsm else None,'nEnt':jsm['nEnt'] if jsm else 0}
    # save full per-field
    out[fld]['_mesh']=mesh; out[fld]['_jsm']=jsm
    print("%-9s tri=%-4d comp=%d  jsmEnt=%d counts=%s names[:12]=%s"%(
        fld, mesh['num_triangles'] if mesh else 0, comp[1],
        jsm['nEnt'] if jsm else 0, jsm['counts'] if jsm else None, names[:12]))
json.dump(out, open('/tmp/sewer_data.json','w'))
print("saved /tmp/sewer_data.json")

# ===== ANALYSIS PASS =====
print("\n\n########## ANALYSIS ##########")
def sym32(field):
    inner=get_inner(field)
    s=inner['sym']; 
    return [s[i*32:i*32+32].split(b'\0')[0].decode('latin1') for i in range((len(s))//32)]
CHARS={'squall','zell','irvine','rinoa','selphie','quistis','laguna','kiros','ward','seifer','edea'}
data=json.load(open('/tmp/sewer_data.json'))
# verify mapping on glwater3 (known: grp8=ct_lf, grp26=saku1)
g3=sym32('glwater3')
print("glwater3 sym[8]=%r sym[26]=%r sym[31]=%r  (expect ct_lf, saku1, saku6)"%(g3[8] if len(g3)>8 else '?', g3[26] if len(g3)>26 else '?', g3[31] if len(g3)>31 else '?'))
for fld in FIELDS:
    jsm=data[fld]['_jsm']; comp=data[fld]['comp']; ntri=data[fld]['ntri']
    syms=sym32(fld)
    def nm(g): return syms[g] if g<len(syms) else '?'
    # component sizes
    from collections import Counter
    csz=Counter(comp)
    print("\n===== %s : %d tri, %d islands %s ====="%(fld, ntri, data[fld]['ncomp'], dict(csz)))
    for e in jsm['ents']:
        g=e['grp']; name=nm(g)
        tri=e['set3_tri']
        isl=comp[tri] if (tri is not None and 0<=tri<ntri) else None
        role=[]
        if e['mapjump']: role.append('EXIT->'+','.join(str(m) for m in e['mapjump']))
        if e['setmodel'] and e['req'] and not e['mapjump'] and name not in CHARS: role.append('GATE?req'+','.join(nm(r) for r in e['req'][:3]))
        if e['ladder']: role.append('LADDER')
        if e['drawpoint']: role.append('DRAWPOINT')
        if 0x12E in e['ops']: role.append('SAVE')
        # print entities that have a role OR a set3 position and are not plain chars
        if role or (tri is not None and name not in CHARS):
            print("  grp%-2d %-12s tri=%s isl=%s  %s"%(g, name, tri, isl, ' '.join(role)))
