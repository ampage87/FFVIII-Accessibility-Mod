#!/usr/bin/env python3
"""Render an FF8 field background from .mim + .map.

.mim layout (Steam/PC field archive, fixed 0x6B000 bytes):
    0x00000  palettes -- 24 slots of 256 RGB555 colours (512 bytes each)
    0x03000  13 texture pages of 0x8000 bytes: 256 rows of 128 bytes, 8bpp

.map is a list of 16-byte tiles; the PSX GPU attributes in each one give the
page and the palette:
    texBits bits0-3 = page column (x/64 in 16-bit VRAM units -> page index)
            bit4    = page row (y/256; always 1 for field backgrounds)
            bits7-8 = colour depth (1 = 8bpp for every field background)
    palBits bits0-5 = CLUT x/16 (0), bits6-15 = CLUT y (240 + palette index)
"""
import struct, sys, os
from PIL import Image

TILE       = 16
PAL_BASE   = 0x0000
PAL_STRIDE = 0x200      # 256 colours * 2 bytes
TEX_BASE   = 0x3000
PAGE_SIZE  = 0x8000     # 256 rows of 128 bytes
PAGE_PITCH = 128
PAL_Y0     = 240        # CLUT y of palette 0

def rgb555(v):
    r=(v&0x1F)<<3; g=((v>>5)&0x1F)<<3; b=((v>>10)&0x1F)<<3
    return (r|(r>>5), g|(g>>5), b|(b>>5), 0 if v==0 else 255)

def tiles(mp):
    out=[]
    for t in range(len(mp)//TILE):
        r=mp[t*TILE:(t+1)*TILE]
        x,y,z,texBits,palBits=struct.unpack_from('<hhHHH',r,0)
        if x==0x7FFF: continue
        out.append(dict(x=x,y=y,z=z,page=texBits&0xF,depth=(texBits>>7)&3,
                        pal=(palBits>>6)-PAL_Y0, sx=r[10], sy=r[11],
                        layer=r[12], blend=r[13], anim=r[14], state=r[15]))
    return out

def render(field, root, out=None, drawAnimated=False):
    mim=open(os.path.join(root,field,field+'.mim'),'rb').read()
    T=tiles(open(os.path.join(root,field,field+'.map'),'rb').read())
    if not T: return None
    xs=[t['x'] for t in T]; ys=[t['y'] for t in T]
    x0,y0=min(xs),min(ys)
    W,H=max(xs)-x0+TILE, max(ys)-y0+TILE
    img=Image.new('RGBA',(W,H),(0,0,0,255))
    pals={}
    def pal(p):
        if p not in pals:
            b=PAL_BASE+p*PAL_STRIDE
            pals[p]=[rgb555(struct.unpack_from('<H',mim,b+2*i)[0]) for i in range(256)]
        return pals[p]
    # Back to front: higher layer id is further back, then greater Z.
    for t in sorted(T,key=lambda t:(-t['layer'],-t['z'])):
        if t['anim']!=0xFF and not drawAnimated and t['state']!=0: continue
        P=pal(t['pal'] if 0<=t['pal']<24 else 0)
        base=TEX_BASE+t['page']*PAGE_SIZE
        buf=[]
        for row in range(TILE):
            o=base+(t['sy']+row)*PAGE_PITCH+t['sx']
            for col in range(TILE):
                buf.append(P[mim[o+col]] if o+col<len(mim) else (0,0,0,0))
        px=Image.new('RGBA',(TILE,TILE)); px.putdata(buf)
        img.paste(px,(t['x']-x0,t['y']-y0),px)
    if out: img.save(out)
    return img, dict(tiles=len(T), size=(W,H), origin=(x0,y0),
                     pages=sorted({t['page'] for t in T}),
                     pals=sorted({t['pal'] for t in T}))

if __name__=='__main__':
    f,root,out=sys.argv[1],sys.argv[2],sys.argv[3]
    _,info=render(f,root,out)
    print(info)
