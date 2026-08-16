"""Generate EntryAimInfo rows from the engine's own entrance polygons.

An entrance polygon is a wmx.obj polygon with byte 0x0E bit 3 set -- the flag
sub_545EA0 tests before it evaluates a single entry program. Only polygons that
are ALSO foot-walkable (byte 0x0F bit 7) can be stood on, and at Edea's House
only 7 of 103 are, which is why an aim point taken from the whole patch misses.

Cross-check: the five hand-proven firing areas already in world_map_trigger_data.inl
must come back within ~150 units."""
import struct, math, collections, re, numpy as np
D=open('wmx.obj','rb').read(); SEG=0x9000
def m2g(mx,mz):
    gx=mx-0x60000
    if gx<-0x20000: gx+=0x40000
    gz=0x48000-mz
    if gz>0x18000: gz-=0x30000
    return gx,gz
W=[]   # walkable entrance triangles
for segno in range(len(D)//SEG):
    base=segno*SEG; sc=segno%32; sr=segno//32
    for sub in range(16):
        off=struct.unpack_from('<I',D,base+4+sub*4)[0]; p=base+off
        if p+4>len(D): continue
        pc=D[p]; vc=D[p+1]; pp=p+4; vp=pp+pc*16
        if vp+vc*8>len(D): continue
        verts=np.frombuffer(D,'<i2',count=vc*4,offset=vp).reshape(vc,4).astype(int)
        fr=sr*4+(sub//4); fc=sc*4+(sub%4); bx=fc*2048; bz=(95-fr)*2048
        for i in range(pc):
            q=pp+i*16
            if not (D[q+14]&0x08) or not (D[q+15]&0x80): continue
            i0,i1,i2=D[q],D[q+1],D[q+2]
            if max(i0,i1,i2)>=vc: continue
            W.append([m2g(int(verts[k][0])+bx,int(verts[k][2])+2048+bz) for k in (i0,i1,i2)])
CEN=[(sum(p[0] for p in t)//3,sum(p[1] for p in t)//3) for t in W]
print("walkable entrance triangles: %d" % len(W))

def inside(px,py,t):
    (x0,y0),(x1,y1),(x2,y2)=t
    d0=(x1-x0)*(py-y0)-(y1-y0)*(px-x0); d1=(x2-x1)*(py-y1)-(y2-y1)*(px-x1)
    d2=(x0-x2)*(py-y2)-(y0-y2)*(px-x2)
    return (d0>=0 and d1>=0 and d2>=0) or (d0<=0 and d1<=0 and d2<=0)

def patch_near(mx,my,rad=1500):
    """flood the walkable triangles reachable from the seed nearest (mx,my)"""
    cand=[i for i,c in enumerate(CEN) if math.hypot(c[0]-mx,c[1]-my)<=rad]
    if not cand: return None
    seed=min(cand,key=lambda i:math.hypot(CEN[i][0]-mx,CEN[i][1]-my))
    if math.hypot(CEN[seed][0]-mx,CEN[seed][1]-my)>rad: return None
    grp={seed}; st=[seed]
    while st:
        k=st.pop()
        for j,c in enumerate(CEN):
            if j in grp: continue
            if abs(c[0]-CEN[k][0])<=400 and abs(c[1]-CEN[k][1])<=400:
                grp.add(j); st.append(j)
    return sorted(grp)

def aim_of(grp):
    tris=[W[k] for k in grp]
    xs=[p[0] for t in tris for p in t]; ys=[p[1] for t in tris for p in t]
    X0,X1,Y0,Y1=min(xs),max(xs),min(ys),max(ys)
    step=max(4,max(X1-X0,Y1-Y0)//140)
    gx=np.arange(X0,X1+1,step); gy=np.arange(Y0,Y1+1,step)
    M=np.zeros((len(gy),len(gx)),bool)
    for j,y in enumerate(gy):
        for i,x in enumerate(gx): M[j,i]=any(inside(int(x),int(y),t) for t in tris)
    Dm=np.zeros_like(M,int); cur=M.copy(); d=0
    while cur.any():
        d+=1; Dm[cur]=d; nxt=cur.copy()
        for dj in(-1,0,1):
            for di in(-1,0,1): nxt&=np.roll(np.roll(cur,dj,0),di,1)
        nxt[0,:]=nxt[-1,:]=False; nxt[:,0]=nxt[:,-1]=False; cur=nxt
    j,i=np.unravel_index(Dm.argmax(),Dm.shape)
    return int(gx[i]),int(gy[j]),(int(Dm.max())-1)*step,len(tris),(X0,X1,Y0,Y1)

# --- regression: the hand-proven aims ---
PROVEN={"Timber":(-22580,-5291),"Dollet":(-14513,-39119),"Balamb Town":(12560,-26800),
        "Fire Cavern":(30239,-29528),"Galbadia Station":(-38914,-24767)}
print("\n-- cross-check against the five hand-proven firing areas --")
worst=0
for n,(ax,ay) in PROVEN.items():
    g=patch_near(ax,ay,800)
    r=aim_of(g); d=math.hypot(r[0]-ax,r[1]-ay); worst=max(worst,d)
    print("  %-18s proven(%7d,%7d)  generated(%7d,%7d)  %5.0f u  %d tris margin %d"
          % (n,ax,ay,r[0],r[1],d,r[3],r[2]))
print("  worst disagreement: %.0f units" % worst)

# --- generate for the catalog ---
src=open('src/world_catalog.inl').read()
body=src[src.index('s_locations[] = {'):]; body=body[:body.index('\n};')]
LOC=[(m.group(1),int(m.group(2)),int(m.group(3)))
     for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}',body)]
HAVE={"Timber","Dollet","Balamb Town","Balamb Garden","Fire Cavern",
      "Galbadia Garden","Galbadia Station"}
print("\n-- new rows --")
rows=[]
for n,mx,my in LOC:
    if n in HAVE: continue
    g=patch_near(mx,my,1500)
    if not g: continue
    ax,ay,marg,nt,(x0,x1,y0,y1)=aim_of(g)
    rows.append((n,ax,ay,x0,x1,y0,y1,marg,nt,math.hypot(ax-mx,ay-my)))
rows.sort(key=lambda r:-r[9])
for n,ax,ay,x0,x1,y0,y1,marg,nt,d in rows:
    print('    { "%s",%s %7d, %7d, %7d, %7d, %7d, %7d, true  },   // %d tris, margin %d, marker %.0fu away'
          % (n," "*max(0,18-len(n)),ax,ay,x0,x1,y0,y1,nt,marg,d))
print("\n%d new rows" % len(rows))
