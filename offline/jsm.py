"""FF8 field-script (.jsm) loader.

Header (8 bytes):
    +0 u16  ?
    +2 u16  ?
    +4 u16  offset of the ENTRIES array        (0x004a = 74 for eccway21)
    +6 u16  offset of the CODE array           (0x0314 = 788)

Groups: (count, start) BYTE pairs from offset 8 up to the entries offset --
count first, start second. `start` indexes the entries array; method M of a
group lives at entries[start + M + 1] .. entries[start + M + 2].

The .sym lists bare entity names first, then `Entity::method` declarations. The
declaration order equals the groups SORTED BY START, not the raw group order.
Verified on eccway21: sorting by start reproduces the .sym's per-entity method
counts (29 x7, 7, 9, 3, 5, ...) exactly, and the raw order does not.
"""
import os, struct, re
ROOT = '/root/work/fs/fx'

class J: pass

def load(name):
    d = os.path.join(ROOT, name)
    raw = open(os.path.join(d, name + '.jsm'), 'rb').read()
    j = J(); j.name = name; j.raw = raw
    eoff, coff = struct.unpack_from('<HH', raw, 4)
    ngroups = (eoff - 8) // 2
    # group word: u16 LE, count = w & 0x7F, start = w >> 7.
    # Verified on eccway21: sum(count) == 323 == the .sym's declaration count,
    # and sorting by start reproduces the .sym's per-entity counts exactly
    # (29 x7, 7, 9, 3, 5, ... 9, 34, ...). The byte-pair reading gets 579.
    j.groups = [((w := struct.unpack_from('<H', raw, 8 + i*2)[0]) & 0x7F, w >> 7)
                for i in range(ngroups)]
    nent = (coff - eoff) // 2
    # & 0x7FFF: entries carry a 0x8000 flag the engine masks off itself
    # (0x0052BF98 / 0x0052C0A4, `and eax, 0x7fff`). Unmasked, eccway21's
    # entries[210..217] read 36247.. against a 6756-dword code array and seven
    # methods get swallowed into their predecessor -- which is how the CP1
    # trigger got mis-attributed to Edea::afkantei2 instead of Linejump1::touch.
    # Every masked value lands on opcode 0x005, the SCRIPT marker.
    j.entries = [struct.unpack_from('<H', raw, eoff + i*2)[0] & 0x7FFF
                 for i in range(nent)]
    j.code = [struct.unpack_from('<I', raw, coff + i*4)[0]
              for i in range((len(raw) - coff) // 4)]
    # names
    j.gname, j.gmeth = {}, {}
    sym = os.path.join(d, name + '.sym')
    if os.path.exists(sym):
        lines = [l.strip() for l in open(sym, errors='ignore').read().splitlines() if l.strip()]
        order = []
        for l in lines:
            if '::' not in l: continue
            e, m = l.split('::', 1)
            if not order or order[-1][0] != e: order.append((e, [m]))
            else: order[-1][1].append(m)
        by_start = sorted(range(len(j.groups)), key=lambda g: j.groups[g][1])
        for i, g in enumerate(by_start):
            if i < len(order):
                j.gname[g] = order[i][0]; j.gmeth[g] = order[i][1]
    return j

def methods(j):
    for g, (cnt, start) in enumerate(j.groups):
        for m in range(cnt):
            idx = start + m + 1
            if idx >= len(j.entries): continue
            lo = j.entries[idx]
            # NOT entries[idx+1]: for the LAST method of a group that reads the
            # next group's start value, which is unrelated and can be enormous
            # (eccway21's Edea::afkantei2 came out as 3468..36247 against a
            # 6756-dword file). The end of a method is the next code offset that
            # actually exists anywhere in the table.
            nxt = [e for e in j.entries if e > lo]
            hi = min(nxt) if nxt else len(j.code)
            ml = j.gmeth.get(g, [])
            yield g, j.gname.get(g, 'grp%d' % g), m, (ml[m] if m < len(ml) else 'm%d' % m), lo, hi

def dec(v):
    if (v & 0xFF000000) == 0: return (v & 0xFFFFFF, None)
    op = v >> 24; par = v & 0xFFFFFF
    if par & 0x800000: par -= 0x1000000
    return (op, par)

OPS = {0:'ADD',1:'SUB',2:'MUL',3:'DIV',4:'MOD',5:'NEG',6:'EQ',7:'GT',8:'GE',9:'LT',10:'LE',
       11:'NE',12:'AND',13:'OR',14:'XOR',15:'NOT'}
NAMES = {0x01:'OPER',0x02:'JMP',0x03:'JPF',0x07:'PSHN_L',0x08:'PSHL',0x0A:'PSHM_B',0x0B:'POPM_B',
         0x0C:'PSHM_W',0x0D:'POPM_W',0x0F:'POPL_L',0x12:'PUSHL_L',0x14:'REQSW',0x15:'REQEW',
         0x16:'REQ',0x29:'MAPJUMP',0x2A:'MAPJUMP3',0x30:'ANIME',0x38:'DISCJUMP',0x46:'MES',
         0x47:'MESW',0x4A:'ASK',0x5C:'MAPJUMPO',0x69:'BATTLE',0x6D:'BTN_HELD',0x6E:'BTN_PRESSED',
         0x9C:'SETTIMER',0xA4:'GETTIMER',0x10D:'WORLDMAPJUMP',0x13B:'GAUGE'}

def fmt(j, i):
    op, par = dec(j.code[i]); nm = NAMES.get(op, 'OP_0x%03X' % op)
    if op == 0x01: return f"{i:5d}  OPER {OPS.get(par,par)}"
    if op in (0x02, 0x03): return f"{i:5d}  {nm:12s} {par:+d} -> {i+par}"
    if op in (0x0A,0x0B,0x0C,0x0D): return f"{i:5d}  {nm:12s} var[{par}]  (0x{0x01CFE9B8+par:08X})"
    if par is None: return f"{i:5d}  {nm:12s}"
    return f"{i:5d}  {nm:12s} {par}"

def show(name, a, b):
    j = load(name)
    for i in range(a, min(b, len(j.code))): print(fmt(j, i))
