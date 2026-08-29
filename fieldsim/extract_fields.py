#!/usr/bin/env python3
"""Extract per-field files out of the game's field archive.

    python3 extract_fields.py "<.../Data/lang-en>" <outdir> [--big]

Writes <outdir>/<field>/<field>.<ext> for every field on the disc. Without
--big only the small files are written (.id .inf .ca .jsm .msd .sym .pmp .pmd
.rat .tdw .sfx .mrt .pcb, about 53 MB in total); with it, the backgrounds and
model archives come too. field.fs is a two-level archive: the outer .fi/.fl/.fs
triple indexes one nested archive per field, each with its own .fi/.fl/.fs.
"""
import struct, os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzs import lzs, load_index

SMALL = {'.id','.inf','.ca','.jsm','.msd','.sym','.pmp','.pmd','.rat','.tdw',
         '.sfx','.mrt','.pcb'}

def main(base, out, want_big=False):
    names, ents = load_index(open(os.path.join(base,'field.fi'),'rb').read(),
                             open(os.path.join(base,'field.fl'),'rb').read())
    fs = open(os.path.join(base,'field.fs'),'rb')
    groups = {}
    for i, nm in enumerate(names):
        low = nm.lower().replace('\\','/')
        if '/mapdata/' not in low: continue
        stem, ext = os.path.splitext(low)
        if ext not in ('.fs','.fl','.fi'): continue
        groups.setdefault(os.path.basename(stem), {})[ext] = i
    keys = sorted(k for k,v in groups.items() if len(v) == 3)

    def rd(idx):
        usz, off, ct = ents[idx]
        fs.seek(off)
        if ct == 0: return fs.read(usz)
        clen = struct.unpack('<I', fs.read(4))[0]
        return lzs(fs.read(clen), usz)

    n = 0
    for k in keys:
        g = groups[k]
        inner_fs = rd(g['.fs'])
        inames, ients = load_index(rd(g['.fi']), rd(g['.fl']))
        d = os.path.join(out, k)
        for j, inm in enumerate(inames):
            if j >= len(ients): break
            usz, off, ct = ients[j]
            b = os.path.basename(inm.lower().replace('\\','/'))
            if not want_big and os.path.splitext(b)[1] not in SMALL: continue
            if ct == 0: data = inner_fs[off:off+usz]
            else:
                clen = struct.unpack_from('<I', inner_fs, off)[0]
                data = lzs(inner_fs[off+4:off+4+clen], usz)
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, b), 'wb').write(data)
        n += 1
    print('extracted %d fields to %s' % (n, out))

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    main(sys.argv[1], sys.argv[2], '--big' in sys.argv)
