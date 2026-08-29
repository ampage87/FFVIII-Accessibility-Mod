#!/usr/bin/env python3
"""Offline FF8 field simulator.

Pulls one field out of the archive and reconstructs what the engine will have in
memory: the script-object table in the engine's own order, every entity's type
and placement as its bytecode sets it, the walkmesh, and every exit the field can
possibly offer (INF gateways, script MAPJUMPs, trigger lines).

  python3 simfield.py eccway11            # text report
  python3 simfield.py eccway11 --svg out.svg
  python3 simfield.py --all               # one-line summary for every field
"""
import sys, os, math
import ffield, scan, fieldids

def gateways(f):
    inf = ffield.load_inf(f)
    if not inf: return []
    out = []
    for g in inf['gateways']:
        if g['fieldId'] in (0xFFFF, 32767): continue
        if g['x1'] == 0 and g['y1'] == 0 and g['x2'] == 0 and g['y2'] == 0: continue
        out.append(dict(idx=g['i'], dest=g['fieldId'],
                        destName=fieldids.fname(g['fieldId']),
                        cx=(g['x1']+g['x2'])/2.0, cy=(g['y1']+g['y2'])/2.0,
                        x1=g['x1'], y1=g['y1'], x2=g['x2'], y2=g['y2']))
    return out

def report(name):
    j = ffield.Jsm(name)
    if not getattr(j, 'ok', False): return None
    _, ents = scan.scan(name, j)
    wm = ffield.Walkmesh(name)
    gws = gateways(name)
    return dict(field=name, id=fieldids.ID_OF.get(name),
                display=fieldids.dname(fieldids.ID_OF.get(name, -1)),
                jsm=j, ents=ents, wm=wm, gws=gws)

def fmt(r):
    j, ents, wm, gws = r['jsm'], r['ents'], r['wm'], r['gws']
    L = []
    L.append('=== %s  (id %s -- %s)' % (r['field'], r['id'], r['display']))
    L.append('    groups %d  (L=%d D=%d B=%d O=%d)   walkmesh %d triangles' %
             (j.ngrp, j.nL, j.nD, j.nB, j.nO, wm.n))
    L.append('    script-object table (engine order: Others, Lines, Backgrounds, Doors)')
    L.append('    %-4s %-5s %-3s %-22s %-19s %-16s %s' %
             ('slot', 'group', 'cat', 'name', 'type', 'pos', 'flags'))
    for e in ents:
        pos = ('(%d,%d)' % e.pos) if e.pos else ('tri %s' % e.tri if e.tri else '-')
        L.append('    %-4d %-5d %-3s %-22s %-19s %-16s %s' %
                 (e.slot, e.group, e.cat, e.name or '?', scan.classify(e), pos,
                  ' '.join(sorted(e.flags))))
    L.append('    exits')
    if not gws and not any(e.mapjumps for e in ents):
        L.append('      (none)')
    for g in gws:
        L.append('      INF gateway %d -> field %d %-10s at (%.0f,%.0f)' %
                 (g['idx'], g['dest'], g['destName'] or '?', g['cx'], g['cy']))
    for e in ents:
        for d in e.mapjumps:
            L.append('      script MAPJUMP  %-20s -> field %d %s' %
                     (e.name or ('slot%d' % e.slot), d, fieldids.fname(d) or '?'))
        if e.cat == 'L' and 'mapjump' in e.flags and not e.mapjumps:
            L.append('      trigger line    %-20s -> destination set at runtime' %
                     (e.name or ('slot%d' % e.slot)))
    return '\n'.join(L)

# --------------------------------------------------------------------------
# SVG: walkmesh + entities + gateways, in field coordinates (y grows downward
# on screen, so flip it to match how the player sees the map).
# --------------------------------------------------------------------------
def svg(r, path):
    j, ents, wm, gws = r['jsm'], r['ents'], r['wm'], r['gws']
    xs, ys = [], []
    for t in range(wm.n):
        v = wm.tris[t]
        xs += [v[0], v[3], v[6]]; ys += [v[1], v[4], v[7]]
    for e in ents:
        if e.pos: xs.append(e.pos[0]); ys.append(e.pos[1])
    for g in gws:
        xs += [g['x1'], g['x2']]; ys += [g['y1'], g['y2']]
    if not xs: return False
    pad = 300
    x0, x1 = min(xs)-pad, max(xs)+pad
    y0, y1 = min(ys)-pad, max(ys)+pad
    W, H = 1100.0, 1100.0 * (y1-y0) / max(1.0, (x1-x0))
    H = max(400.0, min(2200.0, H))
    sx = lambda x: (x - x0) / max(1.0, (x1-x0)) * W
    sy = lambda y: H - (y - y0) / max(1.0, (y1-y0)) * H
    P = ['<svg xmlns="http://www.w3.org/2000/svg" width="%.0f" height="%.0f" '
         'viewBox="0 0 %.0f %.0f" font-family="monospace" font-size="11">' % (W, H, W, H),
         '<rect width="100%%" height="100%%" fill="#12141a"/>']
    for t in range(wm.n):
        v = wm.tris[t]
        pts = ' '.join('%.1f,%.1f' % (sx(v[i*3]), sy(v[i*3+1])) for i in range(3))
        P.append('<polygon points="%s" fill="#243043" stroke="#3d4c66" stroke-width="0.7"/>' % pts)
    for g in gws:
        P.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ffcc44" stroke-width="4"/>'
                 % (sx(g['x1']), sy(g['y1']), sx(g['x2']), sy(g['y2'])))
        P.append('<text x="%.1f" y="%.1f" fill="#ffcc44">gw%d -> %s</text>'
                 % (sx(g['cx'])+6, sy(g['cy'])-6, g['idx'], g['destName'] or g['dest']))
    COL = {'NPC':'#5fd08a', 'ITEM':'#f08a5f', 'DRAW_POINT':'#7fd0ff', 'SAVE_POINT':'#ffe27f',
           'SHOP':'#d08aff', 'CARD_GAME':'#d08aff', 'LADDER':'#9fb0c0',
           'MODEL':'#4a8f66', 'INTERACTIVE_OBJECT':'#c0c0c0'}
    for e in ents:
        if not e.pos: continue
        k = scan.classify(e)
        c = COL.get(k, '#7a8494')
        P.append('<circle cx="%.1f" cy="%.1f" r="5" fill="%s"/>' % (sx(e.pos[0]), sy(e.pos[1]), c))
        P.append('<text x="%.1f" y="%.1f" fill="%s">%s</text>'
                 % (sx(e.pos[0])+7, sy(e.pos[1])+4, c, (e.name or '?')))
    for e in ents:
        if e.setline:
            a, b, c2, d = e.setline
            P.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#ff6b6b" '
                     'stroke-width="2.5" stroke-dasharray="6 4"/>'
                     % (sx(a), sy(b), sx(c2), sy(d)))
            P.append('<text x="%.1f" y="%.1f" fill="#ff6b6b">%s</text>'
                     % (sx((a+c2)/2)+6, sy((b+d)/2)-4, e.name or '?'))
    P.append('<text x="10" y="18" fill="#e8ecf3">%s  (id %s -- %s)   %d triangles, %d entities</text>'
             % (r['field'], r['id'], r['display'], wm.n, len(ents)))
    P.append('</svg>')
    open(path, 'w').write('\n'.join(P))
    return True

if __name__ == '__main__':
    a = sys.argv[1:]
    if not a or a[0] == '--all':
        for f in ffield.fields():
            r = report(f)
            if not r: print('%-10s (no jsm)' % f); continue
            print('%-10s id=%-4s grp=%-3d L=%-2d D=%-2d B=%-2d O=%-2d tris=%-4d gw=%d'
                  % (f, r['id'], r['jsm'].ngrp, r['jsm'].nL, r['jsm'].nD, r['jsm'].nB,
                     r['jsm'].nO, r['wm'].n, len(r['gws'])))
        sys.exit(0)
    r = report(a[0])
    if not r: print('no such field / no jsm:', a[0]); sys.exit(1)
    print(fmt(r))
    if '--svg' in a:
        p = a[a.index('--svg')+1]
        print('svg:', svg(r, p), p)
