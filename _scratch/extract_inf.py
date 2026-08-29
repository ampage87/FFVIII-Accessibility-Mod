"""Pull every ec*/em*/rg*/ss* field's .inf out of field.fs, on the device."""
import struct, os, sys

def lzs(d, cap):
    o=bytearray(); w=bytearray(b'\x00'*4096); wp=0xFEE; p=0
    while p<len(d) and len(o)<cap:
        f=d[p]; p+=1
        for b in range(8):
            if p>=len(d) or len(o)>=cap: break
            if (f>>b)&1:
                c=d[p]; p+=1; o.append(c); w[wp]=c; wp=(wp+1)&0xFFF
            else:
                if p+1>=len(d): break
                a=d[p]; bb=d[p+1]; p+=2
                off=a | ((bb&0xF0)<<4); ln=(bb&0x0F)+3
                for k in range(ln):
                    c=w[(off+k)&0xFFF]; o.append(c); w[wp]=c; wp=(wp+1)&0xFFF
    return bytes(o[:cap])

BASE = sys.argv[1]
OUT  = sys.argv[2]
PREFIXES = tuple(sys.argv[3].split(','))
fi=open(BASE+'.fi','rb').read()
fl=[x.strip() for x in open(BASE+'.fl','rb').read().decode('latin-1').replace('\r\n','\n').split('\n') if x.strip()]
fs=open(BASE+'.fs','rb')
os.makedirs(OUT, exist_ok=True)

def entry(i):
    usz,off,ct=struct.unpack_from('<III',fi,i*12)
    fs.seek(off)
    if ct==0: return fs.read(usz)
    if ct==1:
        csz=struct.unpack_from('<I',fs.read(4),0)[0]
        return lzs(fs.read(csz), usz)
    return None

idx={}
for i,name in enumerate(fl):
    base=name.replace('\\','/').split('/')[-1]
    idx[base.lower()]=i

names=set()
for k in idx:
    if k.endswith('.fi') and k.startswith(PREFIXES): names.add(k[:-3])
print("fields:", len(names))
got=0
for nm in sorted(names):
    try:
        ifi=entry(idx[nm+'.fi']); ifl=entry(idx[nm+'.fl']); ifs=entry(idx[nm+'.fs'])
    except Exception as e:
        print("  outer fail", nm, e); continue
    if not (ifi and ifl and ifs): continue
    inner=[x.strip() for x in ifl.decode('latin-1').replace('\r\n','\n').split('\n') if x.strip()]
    for j,iname in enumerate(inner):
        b=iname.replace('\\','/').split('/')[-1].lower()
        if not (b.endswith('.inf') or b.endswith('.id')): continue
        usz,off,ct=struct.unpack_from('<III',ifi,j*12)
        if ct==0: data=ifs[off:off+usz]
        elif ct==1:
            csz=struct.unpack_from('<I',ifs,off)[0]
            data=lzs(ifs[off+4:off+4+csz], usz)
        else: continue
        open(os.path.join(OUT,b),'wb').write(data); got+=1
print("wrote", got, "files")
