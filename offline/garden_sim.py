"""Garden planner + executor sim, v2.
Planner grid: 256u, CONSERVATIVE downsample -- a 256 cell is usable only if all
four underlying 128u engine cells are Garden-walkable (byte15&0x20) and their
height spread is < 200 (so the cell is internally flat enough for the step gate).
Oracle for the simulator stays the 128u engine-accurate grid.
"""
import numpy as np, heapq, math
# The engine gate is |dH| >= 200 between the current polygon and the one ONE
# MOVE STEP away; the Garden's move step is 0x40 = 64 units (FF8_EN.exe
# 0x53E7A0:0x53E84D). A 256-unit planner cell is FOUR such steps, so the
# faithful budget between adjacent cells is 4 x 200 = 800. Applying the raw 200
# at cell scale is 4x too strict and manufactures walls along every gentle
# slope -- which is exactly what wall-followed the continental shelf in the
# v0.20.56 BAT. At 800, zero cell-to-cell edges block: the Garden's permission
# is the per-vehicle MASK, not a slope rule.
GATE=200          # engine, per 64-unit move step (used on the 128u oracle grid)
CELL_GATE=800     # the same rule expressed at 256u planner-cell scale
z=np.load("./wmgrid.npz"); B15=z['b15']; HGT=z['hgt'].astype(np.int32)
GW8=(B15&0x20)!=0; PK8=(B15&0x02)!=0; FT8=(B15&0x80)!=0
R8,K8=1536,2048; C8=128
RP,KP=768,1024; CP=256
def _q(a,f):  # 2x2 blocks
    return a.reshape(RP,2,KP,2)
gw4=GW8.reshape(RP,2,KP,2)
GWp = gw4.all(axis=(1,3))
h4  = HGT.reshape(RP,2,KP,2)
Hmax=h4.max(axis=(1,3)); Hmin=h4.min(axis=(1,3)); Hmid=h4.mean(axis=(1,3)).astype(np.int32)
GWp = GWp & ((Hmax-Hmin) < CELL_GATE)
PKp = PK8.reshape(RP,2,KP,2).all(axis=(1,3))
FTp = FT8.reshape(RP,2,KP,2).any(axis=(1,3))
HP  = Hmid
# Clearance field on the planner grid: Chebyshev cell distance to the nearest
# non-Garden cell. A hull with a ~1300u turning radius cannot follow a route
# that hugs a 256u-resolution coastline, so the planner pays a penalty for
# running close to land and prefers open water. Exempt near the goal, where the
# park point is by definition on a coast.
import scipy.ndimage as ndi
CLEAR = np.minimum(ndi.distance_transform_cdt(GWp, metric='chessboard'), 32).astype(np.uint8)
CLEAR_TARGET = 7        # 7*256 = 1792u
CLEAR_PENALTY = 0.55    # extra cost per missing clearance cell

def g2c8(gx,gz): return ((0x48000-int(gz))%0x30000)//C8, ((int(gx)+0x60000)%0x40000)//C8
def g2cp(gx,gz): return ((0x48000-int(gz))%0x30000)//CP, ((int(gx)+0x60000)%0x40000)//CP
def cp2g(r,c):
    mx=int(c)*CP+CP//2; mz=int(r)*CP+CP//2
    return (mx-0x60000+0x20000)%0x40000-0x20000,(0x48000-mz+0x18000)%0x30000-0x18000
def wdx(a,b):
    d=b-a
    return d-262144 if d>131072 else (d+262144 if d<-131072 else d)
def wdy(a,b):
    d=b-a
    return d-196608 if d>98304 else (d+196608 if d<-98304 else d)
def wdist(ax,ay,bx,by): return math.hypot(wdx(ax,bx),wdy(ay,by))
def bearing(ax,ay,bx,by): return int(math.atan2(wdx(ax,bx),-wdy(ay,by))/(2*math.pi)*4096)&0xFFF
def gok(gx,gz):
    r,c=g2c8(gx,gz)
    return bool(GW8[r,c]), int(HGT[r,c])
NB=[(-1,0,1.0),(1,0,1.0),(0,-1,1.0),(0,1,1.0),(-1,-1,1.4142),(-1,1,1.4142),(1,-1,1.4142),(1,1,1.4142)]
def snapp(r0,c0,rad=30):
    if GWp[r0,c0]: return r0,c0
    for k in range(1,rad+1):
        for dr in range(-k,k+1):
            for dc in range(-k,k+1):
                if max(abs(dr),abs(dc))!=k: continue
                r=r0+dr; c=(c0+dc)%KP
                if 0<=r<RP and GWp[r,c]: return r,c
    return None
def plan(sx,sy,tx,ty,cap=1200000,clear_target=None):
    s=snapp(*g2cp(sx,sy)); g=snapp(*g2cp(tx,ty))
    if s is None or g is None: return None
    sr,sc=s; gr,gc=g
    d0=gc-sc
    if d0>KP//2: d0-=KP
    if d0<-KP//2: d0+=KP
    gcu=sc+d0
    def h(r,c):
        dr=abs(r-gr); dc=abs(c-gcu); return (dr+dc)+(1.4142-2.0)*min(dr,dc)
    start=(sr,sc); goal=(gr,gcu)
    G={start:0.0}; came={}; hp=[(h(*start),0,start)]; tie=0; exp=0
    while hp:
        _,_,cur=heapq.heappop(hp)
        if cur==goal:
            path=[cur]
            while cur in came: cur=came[cur]; path.append(cur)
            path.reverse(); return [cp2g(r,c%KP) for r,c in path]
        exp+=1
        if exp>cap: return None
        r,c=cur; base=G[cur]; hh=HP[r,c%KP]
        nearGoal = ((abs(r-gr)<=10 and abs(c-gcu)<=10) or
                    (abs(r-sr)<=10 and abs(c-sc)<=10))
        for dr,dc,w in NB:
            nr=r+dr; nc=c+dc
            if nr<0 or nr>=RP: continue
            ncw=nc%KP
            if not GWp[nr,ncw]: continue
            if abs(int(HP[nr,ncw])-int(hh))>=CELL_GATE: continue
            pen=0.0
            if not nearGoal:
                miss=(CLEAR_TARGET if clear_target is None else clear_target)-int(CLEAR[nr,ncw])
                if miss>0: pen=CLEAR_PENALTY*miss
            ng=base+w*(1.0+pen)
            if ng<G.get((nr,nc),1e30):
                G[(nr,nc)]=ng; came[(nr,nc)]=cur; tie+=1
                heapq.heappush(hp,(ng+h(nr,nc),tie,(nr,nc)))
    return None
def decimate(path,pitch=256):
    out=[path[0]]
    for p in path[1:-1]:
        if wdist(out[-1][0],out[-1][1],p[0],p[1])>=pitch: out.append(p)
    out.append(path[-1]); return out

STEER_DEADZONE=320; STEER_FWD_CONE=576
def probe_blocked(x,y,hd,d):
    a=hd/4096.0*2*math.pi
    nx=(x+math.sin(a)*d+131072)%262144-131072; ny=(y-math.cos(a)*d+98304)%196608-98304
    ok,nh=gok(nx,ny); _,ch=gok(x,y)
    return (not ok) or abs(nh-ch)>=GATE
def run(sx,sy,heading,wps,speed=64,turn=32,arrive=384,look=1024,final=1200,maxframes=90000):
    x,y,hd=float(sx),float(sy),heading
    n=len(wps); idx=0; seg=[wdist(*wps[i],*wps[i+1]) for i in range(n-1)]
    fx,fy=wps[-1]; anchor=(x,y); anchorF=0; revUntil=-1; revs=0
    for f in range(1,maxframes+1):
        best=idx; bd=wdist(x,y,*wps[idx])
        for j in range(idx+1,min(idx+13,n)):
            d=wdist(x,y,*wps[j])
            if d<bd: bd=d; best=j
        idx=best
        while idx<n-1 and wdist(x,y,*wps[idx])<arrive: idx+=1
        gd=wdist(x,y,fx,fy)
        if gd<arrive: return True,f,(x,y),revs
        if gd<final: tx,ty=fx,fy
        else:
            ti=idx; acc=wdist(x,y,*wps[idx])
            while ti<n-1 and acc<look: acc+=seg[ti]; ti+=1
            tx,ty=wps[ti]
        tb=bearing(x,y,tx,ty); err=(tb-hd)&0xFFF
        if err>2048: err-=4096
        off=abs(err)
        inRev = f<revUntil
        guard = (not inRev) and (probe_blocked(x,y,hd,speed*4) or probe_blocked(x,y,hd,speed*8))
        wantUp = (not inRev) and (off<=STEER_FWD_CONE) and (not guard)
        # turn: deadzone normally, but ALWAYS turn when the forward guard trips
        if off>STEER_DEADZONE or (guard and off>0):
            st=min(turn,max(off,turn)) if guard else min(turn,off)
            hd=(hd+(st if err>=0 else -st))&0xFFF
        elif guard:
            hd=(hd+turn)&0xFFF
        if wantUp or inRev:
            dirh = hd if wantUp else (hd+2048)&0xFFF
            a=dirh/4096.0*2*math.pi
            nx=(x+math.sin(a)*speed+131072)%262144-131072
            ny=(y-math.cos(a)*speed+98304)%196608-98304
            ok,nh=gok(nx,ny); _,ch=gok(x,y)
            if ok and abs(nh-ch)<GATE: x,y=nx,ny
        if wdist(x,y,*anchor)>=250: anchor=(x,y); anchorF=f
        elif f-anchorF>72 and not inRev:
            revUntil=f+36; revs+=1; anchorF=f
            if revs>40: return False,f,(x,y),revs
    return False,maxframes,(x,y),revs
