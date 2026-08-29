import struct, os, sys
sys.path.insert(0, os.path.join(os.getcwd(), 'fieldsim'))
from lzs import lzs, load_index
BASE = sys.argv[1]; OUT = sys.argv[2]
WANT = set(sys.argv[3].split(','))
KEEP = {'.jsm','.msd','.sym','.id','.inf','.ca'}
names, ents = load_index(open(os.path.join(BASE,'field.fi'),'rb').read(),
                         open(os.path.join(BASE,'field.fl'),'rb').read())
fs = open(os.path.join(BASE,'field.fs'),'rb')
groups = {}
for i, nm in enumerate(names):
    low = nm.lower().replace('\\','/')
    if '/mapdata/' not in low: continue
    stem, ext = os.path.splitext(low)
    if ext not in ('.fs','.fl','.fi'): continue
    groups.setdefault(os.path.basename(stem), {})[ext] = i
def read(i):
    size, off, ctype = ents[i]
    fs.seek(off); raw = fs.read(size if ctype == 0 else struct.unpack('<I', fs.read(4))[0])
    return raw if ctype == 0 else lzs(raw)
n = 0
for key, g in groups.items():
    if key not in WANT or len(g) != 3: continue
    sub_fi = read(g['.fi']); sub_fl = read(g['.fl']); sub_fs_i = g['.fs']
    snames, sents = load_index(sub_fi, sub_fl)
    size, off, ctype = ents[sub_fs_i]
    fs.seek(off)
    blob = fs.read(size) if ctype == 0 else lzs(fs.read(struct.unpack('<I', fs.read(4))[0]))
    d = os.path.join(OUT, key); os.makedirs(d, exist_ok=True)
    for j, snm in enumerate(snames):
        e = os.path.splitext(snm.lower())[1]
        if e not in KEEP: continue
        ssize, soff, sctype = sents[j]
        chunk = blob[soff:soff+ssize] if sctype == 0 else lzs(blob[soff+4:soff+4+struct.unpack('<I', blob[soff:soff+4])[0]])
        open(os.path.join(d, os.path.basename(snm.lower())), 'wb').write(chunk)
        n += 1
    print(key, 'ok')
print('files:', n)
