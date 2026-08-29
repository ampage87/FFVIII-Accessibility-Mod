import struct, os
ROOT='/root/work/fs/fx'
# The raw encoding IS the sysfnt grid shifted by 0x20: glyph = byte - 0x20.
BASE = (" !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~ ")
def decode(b):
    out=[]
    i=0
    while i < len(b):
        c=b[i]
        if c==0x00: break
        if c==0x01: out.append('\n'); i+=1; continue
        if c==0x02: out.append('{NEWPAGE}'); i+=1; continue
        if 0x20 <= c <= 0x9f:
            k=c-0x20
            out.append(BASE[k] if k < len(BASE) else '?')
        elif c==0x03: out.append('{CHAR}'); i+=1
        else: out.append('{%02X}'%c)
        i+=1
    return ''.join(out)
def messages(name):
    p=os.path.join(ROOT,name,name+'.msd')
    d=open(p,'rb').read()
    n=struct.unpack_from('<I',d,0)[0]//4
    offs=[struct.unpack_from('<I',d,i*4)[0] for i in range(n)]
    out=[]
    for i,o in enumerate(offs):
        e=len(d)
        for o2 in offs:
            if o2>o and o2<e: e=o2
        out.append(decode(d[o:e]))
    return out
