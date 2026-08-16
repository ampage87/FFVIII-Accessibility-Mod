"""Decode FF8 mngrp.bin menu text using the sysfnt glyph table.

Text byte = glyph index + 0x20. The table is the 14x16 grid documented in
src/sysfnt_chartable.txt (from myst6re/deling)."""
import struct, re, sys
ROWS = [
 [" ","0","1","2","3","4","5","6","7","8","9","%","/",":","!","?"],
 ["…","+","-","=","*","&","「","」","(",")","·",".",",","~","“","”"],
 ["'","#","$","'","_","A","B","C","D","E","F","G","H","I","J","K"],
 ["L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","a"],
 ["b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q"],
 ["r","s","t","u","v","w","x","y","z","À","Á","Â","Ä","Ç","È","É"],
 ["Ê","Ë","Ì","Í","Î","Ï","Ñ","Ò","Ó","Ô","Ö","Ù","Ú","Û","Ü","Œ"],
 ["à","á","â","ä","ç","è","é","ê","ë","ì","í","î","ï","ñ","ò","ó"],
 ["ô","ö","ù","ú","û","ü","œ","ß","¡","¿","«","»","·","·","·","·"],
]
TBL={}
for r,row in enumerate(ROWS):
    for c,ch in enumerate(row):
        TBL[0x20 + r*16 + c] = ch
def dec(buf, start, maxlen=64):
    o=[]
    for i in range(start, min(start+maxlen, len(buf))):
        b=buf[i]
        if b==0: break
        if b in TBL: o.append(TBL[b])
        elif b<0x20: o.append("{%02X}"%b)
        else: o.append("·")
    return ''.join(o)

h=open('menuout/mngrphd.bin','rb').read(); g=open('menuout/mngrp.bin','rb').read()
secs=[]
for i in range(len(h)//8):
    off,size=struct.unpack_from('<II',h,i*8)
    if off!=0xFFFFFFFF: secs.append((i,off-1,size))

WANT=re.compile(r'^(Use|Sort|Swap|Trade|Give|Take|Arrange|Rearrange|Junction|Magic|Item|Cast|Discard|Exchange|Order|Name|Type|Number|Amount|Level|Element)', re.I)
for si,base,size in secs:
    n=struct.unpack_from('<H',g,base)[0]
    if not (0<n<64): continue
    for b in range(n):
        try: bo=struct.unpack_from('<H',g,base+2+b*2)[0]
        except: break
        sub=base+bo
        if sub+2>len(g): continue
        m=struct.unpack_from('<H',g,sub)[0]
        if not (0<m<400): continue
        strs=[]
        for k in range(m):
            try: so=struct.unpack_from('<H',g,sub+2+k*2)[0]
            except: break
            strs.append(dec(g,sub+so))
        hits=[s for s in strs if WANT.match(s)]
        if len(hits)>=3:
            print("\n=== mngrphd section %d, bank %d  (file 0x%X)  %d strings" % (si,b,sub,m))
            for k,s in enumerate(strs[:32]):
                print("   [%2d] %s"%(k,s))
