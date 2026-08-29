import struct, sys, os
base = "Game Files/FINAL FANTASY VIII/Data/lang-en/battle"
names = [n for n in open(base+".fl","rb").read().decode("latin1").split("\r\n") if n.strip()]
fi = open(base+".fi","rb").read()
def lzs(raw, usize):
    out=bytearray(); ring=bytearray(4096); rp=0xFEE; p=0
    while p<len(raw) and len(out)<usize:
        flags=raw[p]; p+=1
        for b in range(8):
            if p>=len(raw) or len(out)>=usize: break
            if flags&(1<<b):
                c=raw[p]; p+=1; out.append(c); ring[rp]=c; rp=(rp+1)&0xFFF
            else:
                if p+1>=len(raw): break
                lo=raw[p]; hi=raw[p+1]; p+=2
                ofs=lo|((hi&0xF0)<<4); ln=(hi&0x0F)+3
                for k in range(ln):
                    c=ring[(ofs+k)&0xFFF]; out.append(c); ring[rp]=c; rp=(rp+1)&0xFFF
    return bytes(out)
want=sys.argv[1]
for i,n in enumerate(names):
    if want not in n.lower(): continue
    usize, off, comp = struct.unpack_from("<III", fi, i*12)
    f=open(base+".fs","rb"); f.seek(off)
    if comp==0: data=f.read(usize)
    elif comp==1:
        csize=struct.unpack("<I",f.read(4))[0]; data=lzs(f.read(csize), usize)
    else: print("comp",comp,"unsupported"); continue
    bn=n.replace("\\","/").split("/")[-1]
    outp="_scratch/bah/"+bn
    open(outp,"wb").write(data)
    print("idx",i,bn,"usize",usize,"got",len(data))
