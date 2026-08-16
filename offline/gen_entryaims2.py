"""v0.21.7: resolve the five held-back retargets from the engine's own data.

The v0.21.6 generator assigned patches to destinations by nearest centroid, which
is a guess. This one uses the authority: a patch sits in a SEGMENT, a segment has
exactly one entry PROGRAM, and a program names its DESTINATIONS with clause
coordinate bounds. Where a program splits its square, the patch splits with it,
and the aim must land on the correct side."""
import struct, math, re, collections, numpy as np
D=open('wmx.obj','rb').read(); SEG=0x9000
def m2g(mx,mz):
    gx=mx-0x60000
    if gx<-0x20000: gx+=0x40000
    gz=0x48000-mz
    if gz>0x18000: gz-=0x30000
    return gx,gz
def segof(x,y): return (((y+0x48000)%0x30000)>>13)*32 + (((x+0x60000)&0x3FFFF)>>13)

W=[]
for sn in range(len(D)//SEG):
    base=sn*SEG; sc=sn%32; sr=sn//32
    for sub in range(16):
        off=struct.unpack_from('<I',D,base+4+sub*4)[0]; p=base+off
        if p+4>len(D): continue
        pc=D[p]; vc=D[p+1]; pp=p+4; vp=pp+pc*16
        if vp+vc*8>len(D): continue
        v=np.frombuffer(D,'<i2',count=vc*4,offset=vp).reshape(vc,4).astype(int)
        fr=sr*4+(sub//4); fc=sc*4+(sub%4); bx=fc*2048; bz=(95-fr)*2048
        for i in range(pc):
            q=pp+i*16
            if not (D[q+14]&0x08) or not (D[q+15]&0x80): continue
            i0,i1,i2=D[q],D[q+1],D[q+2]
            if max(i0,i1,i2)>=vc: continue
            W.append([m2g(int(v[k][0])+bx,int(v[k][2])+2048+bz) for k in (i0,i1,i2)])
CEN=[(sum(p[0] for p in t)//3,sum(p[1] for p in t)//3) for t in W]

def inside(px,py,t):
    (x0,y0),(x1,y1),(x2,y2)=t
    d0=(x1-x0)*(py-y0)-(y1-y0)*(px-x0); d1=(x2-x1)*(py-y1)-(y2-y1)*(px-x1)
    d2=(x0-x2)*(py-y2)-(y0-y2)*(px-x2)
    return (d0>=0 and d1>=0 and d2>=0) or (d0<=0 and d1<=0 and d2<=0)

def aim_of(tris, keep=None):
    """interior point farthest from the region edge; `keep(x,y)` restricts the
    region to the clause's side of a split."""
    xs=[p[0] for t in tris for p in t]; ys=[p[1] for t in tris for p in t]
    X0,X1,Y0,Y1=min(xs),max(xs),min(ys),max(ys)
    step=max(4,max(X1-X0,Y1-Y0)//160)
    gx=np.arange(X0,X1+1,step); gy=np.arange(Y0,Y1+1,step)
    M=np.zeros((len(gy),len(gx)),bool)
    for j,y in enumerate(gy):
        for i,x in enumerate(gx):
            if keep and not keep(int(x),int(y)): continue
            M[j,i]=any(inside(int(x),int(y),t) for t in tris)
    if not M.any(): return None
    Dm=np.zeros_like(M,int); cur=M.copy(); d=0
    while cur.any():
        d+=1; Dm[cur]=d; nxt=cur.copy()
        for dj in(-1,0,1):
            for di in(-1,0,1): nxt&=np.roll(np.roll(cur,dj,0),di,1)
        nxt[0,:]=nxt[-1,:]=False; nxt[:,0]=nxt[:,-1]=False; cur=nxt
    j,i=np.unravel_index(Dm.argmax(),Dm.shape)
    # bbox of the KEPT region only
    kx=[];ky=[]
    for jj,y in enumerate(gy):
        for ii,x in enumerate(gx):
            if M[jj,ii]: kx.append(int(x)); ky.append(int(y))
    return int(gx[i]),int(gy[j]),(int(Dm.max())-1)*step,len(tris),(min(kx),max(kx),min(ky),max(ky))

# collect the triangles of the patch containing a seed point, optionally
# restricted to one segment
def patch_at(px,py,rad=2500,only_seg=None):
    cand=[i for i,c in enumerate(CEN) if math.hypot(c[0]-px,c[1]-py)<=rad]
    if not cand: return None
    seed=min(cand,key=lambda i:math.hypot(CEN[i][0]-px,CEN[i][1]-py))
    grp={seed}; st=[seed]
    while st:
        k=st.pop()
        for j,c in enumerate(CEN):
            if j in grp: continue
            if abs(c[0]-CEN[k][0])<=400 and abs(c[1]-CEN[k][1])<=400: grp.add(j); st.append(j)
    if only_seg is not None:
        grp={k for k in grp if segof(*CEN[k])==only_seg}
    return [W[k] for k in sorted(grp)]

SEGX0 = lambda s: ((s%32)*8192) - 0x60000 + (0x40000 if ((s%32)*8192 - 0x60000) < -0x20000 else 0)

JOBS=[
 dict(name="Deling City",       seed=(-61518,-30716), seg=264, prog=8,  dest=8,  keep=None,
      note="prog 8, no coordinate bound, one destination, story>=333"),
 dict(name="D-District Prison", seed=(-55300,-6250),  seg=361, prog=16, dest=10, keep=None,
      note="prog 16, no coordinate bound, one destination, story>=350"),
 dict(name="Trabia Garden",     seed=(49107,-59360),  seg=149, prog=3,  dest=19, keep=None, only_seg=149,
      note="prog 3 covers segment 149 unconditionally; the patch also spills into 150 where dest 19 needs Yoff>4096, so the aim is kept in 149"),
 dict(name="Winhill",           seed=(-51140,6321),   seg=393, prog=24, dest=14, keep='xoff>6144',
      note="prog 24 SPLITS segment 393 at Xoff 6144: dest 14 high, dest 15 low. The marker is at Xoff 7059 -- the HIGH side -- and the v0.21.6 aim was at Xoff 5825, the wrong one"),
 dict(name="Great Salt Lake",   seed=(48397,-2171),   seg=373, prog=21, dest=24, keep=None,
      note="prog 21, foot only (no chocobo clause), story>=1600 -- see notes on Chocobo Forest 3"),
]
print("%-20s %-18s %-8s %s" % ("destination","aim","margin","bbox"))
print("-"*96)
rows=[]
for j in JOBS:
    tris=patch_at(*j['seed'], only_seg=j.get('only_seg'))
    keep=None
    if j['keep']=='xoff>6144':
        keep=lambda x,y: ((x + 0x60000) & 0x1FFF) > 6144
    r=aim_of(tris,keep)
    ax,ay,marg,nt,(x0,x1,y0,y1)=r
    rows.append((j['name'],ax,ay,x0,x1,y0,y1,marg,nt,j))
    print("%-20s (%7d,%7d) %-8d x[%d,%d] y[%d,%d]  %d tris" % (j['name'],ax,ay,marg,x0,x1,y0,y1,nt))
print()
for n,ax,ay,x0,x1,y0,y1,marg,nt,j in rows:
    print('    { "%s",%s %7d, %7d, %7d, %7d, %7d, %7d, %-5s },   // prog %d dest %d, %d tris, margin %d'
          % (n," "*max(0,18-len(n)),ax,ay,x0,x1,y0,y1,"true",j['prog'],j['dest'],nt,marg))
