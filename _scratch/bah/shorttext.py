import struct, sys
base = "Game Files/FINAL FANTASY VIII/Data/lang-en/battle"
names = [n for n in open(base+".fl","rb").read().decode("latin1").split("\r\n") if n.strip()]
fi = open(base+".fi","rb").read(); fs=open(base+".fs","rb")
def lzs(raw, usize):
    out=bytearray(); ring=bytearray(4096); rp=0xFEE; p=0
    while p<len(raw) and len(out)<usize:
        f=raw[p]; p+=1
        for b in range(8):
            if p>=len(raw) or len(out)>=usize: break
            if f&(1<<b):
                c=raw[p]; p+=1; out.append(c); ring[rp]=c; rp=(rp+1)&0xFFF
            else:
                if p+1>=len(raw): break
                lo=raw[p]; hi=raw[p+1]; p+=2
                o=lo|((hi&0xF0)<<4); ln=(hi&0x0F)+3
                for k in range(ln):
                    c=ring[(o+k)&0xFFF]; out.append(c); ring[rp]=c; rp=(rp+1)&0xFFF
    return bytes(out)
def get(i):
    us,off,comp=struct.unpack_from("<III",fi,i*12); fs.seek(off)
    if comp==0: return fs.read(us)
    cs=struct.unpack("<I",fs.read(4))[0]; return lzs(fs.read(cs),us)
lo,hi=int(sys.argv[1]),int(sys.argv[2])
for i,n in enumerate(names):
    bn=n.replace("\\","/").split("/")[-1]
    if not bn.startswith("c0m"): continue
    num=int(bn[3:6])
    if not (lo<=num<=hi): continue
    try:
        d=get(i)
        ns=struct.unpack_from("<I",d,0)[0]
        offs=[struct.unpack_from("<I",d,4+4*k)[0] for k in range(ns)]
        sec=d[offs[7]:offs[8]]
        m=struct.unpack_from("<I",sec,0)[0]
        sub=[struct.unpack_from("<I",sec,4+4*k)[0] for k in range(m)]
        if m<3: continue
        to,td=sub[1],sub[2]
        blob=sec[td:]
        for s in blob.split(b"\x00"):
            if 0<len(s)<=3 or b"\x05" in s:
                print(num,bn,s.hex(' '))
    except Exception as e:
        pass
