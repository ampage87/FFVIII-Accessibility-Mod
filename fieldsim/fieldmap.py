#!/usr/bin/env python3
"""Draw a field's navigable geometry: walkmesh, entity placements, INF gateway
exit lines and SETLINE interaction zones, in field coordinates.

This is the picture the mod is actually reasoning about. It is not the painted
background -- see BACKGROUND_FORMAT in the findings doc for where that stands --
but every exit, every entity position and every trigger line the catalog can
know about is here, which is what an exit audit needs to look at.

    python3 fieldmap.py <field> <out.png>
"""
import sys, os
from PIL import Image, ImageDraw
import ffield, scan, fieldids

COL = {'NPC':(95,208,138), 'ITEM':(240,138,95), 'DRAW_POINT':(127,208,255),
       'SAVE_POINT':(255,226,127), 'SHOP':(208,138,255), 'CARD_GAME':(208,138,255),
       'MODEL':(74,143,102), 'INTERACTIVE_OBJECT':(200,200,200),
       'LINE_EXIT':(255,204,68), 'EXIT':(255,204,68)}

def draw(field, out, size=1400):
    j = ffield.Jsm(field)
    if not getattr(j, 'ok', False): return None
    _, ents = scan.scan(field, j)
    wm = ffield.Walkmesh(field)
    inf = ffield.load_inf(field)
    gws = []
    if inf:
        for g in inf['gateways']:
            if g['fieldId'] in (0xFFFF, 32767): continue
            if not any(g[k] for k in ('x1','y1','x2','y2')): continue
            gws.append(g)
    xs, ys = [], []
    for t in range(wm.n):
        v = wm.tris[t]; xs += [v[0],v[3],v[6]]; ys += [v[1],v[4],v[7]]
    for e in ents:
        if e.pos: xs.append(e.pos[0]); ys.append(e.pos[1])
    for g in gws: xs += [g['x1'],g['x2']]; ys += [g['y1'],g['y2']]
    if not xs: return None
    pad = 250
    x0,x1 = min(xs)-pad, max(xs)+pad
    y0,y1 = min(ys)-pad, max(ys)+pad
    sc = min(size/max(1,(x1-x0)), size/max(1,(y1-y0)))
    W,H = int((x1-x0)*sc)+1, int((y1-y0)*sc)+1
    img = Image.new('RGB', (W,H), (18,20,26))
    d = ImageDraw.Draw(img)
    P = lambda x,y: ((x-x0)*sc, H-(y-y0)*sc)
    for t in range(wm.n):
        v = wm.tris[t]
        d.polygon([P(v[0],v[1]),P(v[3],v[4]),P(v[6],v[7])],
                  fill=(36,48,67), outline=(61,76,102))
    for g in gws:
        d.line([P(g['x1'],g['y1']), P(g['x2'],g['y2'])], fill=(255,204,68), width=5)
        mx,my = P((g['x1']+g['x2'])/2, (g['y1']+g['y2'])/2)
        d.text((mx+6,my-14), 'exit -> %s' % (fieldids.fname(g['fieldId']) or g['fieldId']),
               fill=(255,204,68))
    for e in ents:
        if e.setline:
            a,b,c,dd = e.setline
            d.line([P(a,b),P(c,dd)], fill=(255,107,107), width=3)
            mx,my = P((a+c)/2,(b+dd)/2)
            d.text((mx+5,my-12), e.name or '?', fill=(255,107,107))
    for e in ents:
        if not e.pos: continue
        k = scan.classify(e)
        c = COL.get(k,(122,132,148))
        x,y = P(e.pos[0], e.pos[1])
        d.ellipse([x-5,y-5,x+5,y+5], fill=c)
        d.text((x+8,y-6), '%s  %s' % (e.name or '?', k), fill=c)
    fid = fieldids.ID_OF.get(field)
    d.text((8,8), '%s  (id %s -- %s)   %d triangles, %d entities, %d gateway exits'
           % (field, fid, fieldids.dname(fid) if fid is not None else '?',
              wm.n, len(ents), len(gws)), fill=(232,236,243))
    img.save(out)
    return dict(size=(W,H), tris=wm.n, ents=len(ents), gws=len(gws))

if __name__ == '__main__':
    print(draw(sys.argv[1], sys.argv[2]))
