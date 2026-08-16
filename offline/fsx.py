"""Extract an FF8 .fi/.fl/.fs triple.

The .fi entry is {u32 uncompressed_size, u32 offset, u32 compression} -- SIZE
FIRST. Reading it as {offset,size,...} yields plausible-looking nonsense; the
tell is magsort.bin, which must be 7 orders x 0x40 = 448 bytes and only is under
this reading (and mmagic.bin, 57 spells x 4 = 228)."""
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
def extract(base,out,only=None):
    fi=open(base+'.fi','rb').read(); fs=open(base+'.fs','rb').read()
    fl=[x.strip() for x in open(base+'.fl','rb').read().decode('latin-1').replace('\r\n','\n').split('\n') if x.strip()]
    os.makedirs(out,exist_ok=True)
    for i in range(len(fi)//12):
        usz,off,ct=struct.unpack_from('<III',fi,i*12)
        nm=(fl[i] if i<len(fl) else "e%03d"%i).replace('\\','/').split('/')[-1]
        if only and nm not in only: continue
        if ct==0: data=fs[off:off+usz]
        elif ct==1:
            csz=struct.unpack_from('<I',fs,off)[0]
            data=lzs(fs[off+4:off+4+csz], usz)
        else: continue
        open(os.path.join(out,nm),'wb').write(data)
        print("  %-14s %8d bytes (want %d) ctype=%d"%(nm,len(data),usz,ct))
if __name__=='__main__':
    extract(sys.argv[1],sys.argv[2], set(sys.argv[3:]) or None)
