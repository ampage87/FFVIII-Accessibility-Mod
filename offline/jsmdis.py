"""Correct FF8 JSM disassembler.

Word format (src/field_archive_jsm_decode.inl, verified against the exe's
dispatch at 0x0052A647):
    if (w & 0xFF000000) == 0 : opcode = w            (bare, no parameter)
    else                     : opcode = w >> 24, param = sign_extend24(w)
The opcode is the HIGH byte.  Reading the low byte -- which is what several
ad-hoc dumps in this project did -- turns every instruction into a different
one and produces plausible-looking nonsense.
"""
import sys, re, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'fieldsim'))
import ffield

_src = os.path.join(os.path.dirname(__file__), '..', 'src', 'field_archive_jsm_opnames.inl')
NAMES = {}
try:
    txt = open(_src, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'case\s+0x([0-9A-Fa-f]{1,3})\s*:\s*return\s*"([^"]+)"', txt):
        NAMES[int(m.group(1), 16)] = m.group(2)
except Exception:
    pass

# Opcodes that push a value the NEXT consuming opcode will read off the script
# stack (from field_archive_jsm_decode.inl's IsArgPush plus the ones the Centra
# scripts actually use).
PUSHES = {0x07, 0x09, 0x0A, 0x0C, 0x0D, 0x0E, 0x13}

def decode(w):
    if (w & 0xFF000000) == 0:
        return (w if w < 0x200 else 0xFFFF), 0, True
    op = w >> 24
    p = w & 0xFFFFFF
    if p & 0x800000:
        p -= 0x1000000
    return op, p, False

def name(op):
    return NAMES.get(op, "op_%03X" % op)

def method(field, group, m, jsm=None):
    j = jsm or ffield.Jsm(field)
    r = j.method_range(group, m)
    if not r:
        return []
    lo, hi = r
    return [decode(w) for w in j.code[lo:hi]]

def render(field, group, m, jsm=None, names=None):
    """Return a list of text lines with argument pushes folded into the
    consuming instruction, which is how the interpreter actually reads them."""
    ins = method(field, group, m, jsm)
    out, pend = [], []
    for op, p, bare in ins:
        nm = name(op)
        if op in PUSHES and not bare:
            pend.append(p)
            continue
        arg = ""
        if pend:
            arg = "(" + ", ".join(str(x) for x in pend) + ")"
            pend = []
        if not bare:
            arg += (" param=%d" % p) if arg else ("param=%d" % p)
        out.append("%-14s %s" % (nm, arg))
    for x in pend:
        out.append("PUSH           %d   <- unconsumed" % x)
    return out

def groups(field):
    j = ffield.Jsm(field)
    return j, ffield.group_names(field, j)

if __name__ == '__main__':
    f = sys.argv[1]
    j, gn = groups(f)
    want = sys.argv[2:] if len(sys.argv) > 2 else None
    for g in range(j.ngrp):
        nm = str(gn[g])
        if want and nm not in want:
            continue
        print("=== group %d '%s' (%s, %d methods)" % (g, nm, j.cat(g), j.method_count(g)))
        for m in range(j.method_count(g)):
            body = render(f, g, m, j)
            if len(body) <= 1:
                continue
            print("  -- method %d" % m)
            for line in body:
                print("     " + line)
