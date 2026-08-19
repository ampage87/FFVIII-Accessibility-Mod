#!/usr/bin/env python3
"""lint_stub -- a probe stub must have the same shape as the thing it stands in for.

WHY THIS EXISTS
---------------
v0.34.2 hand-wrote a stub in tests/menu_shop_compile.cpp:

    static std::string MenuDialogCompose(const MenuDialogRaw&, int cursor)

against the real declaration in src/menu_dialog.inl:

    static bool MenuDialogCompose(int cursor, char* out, size_t n)

The probe compiled, ran, and passed every assertion -- against a function that
does not exist. MSVC failed the build. The same shape of mistake had already
cost a build once before (the FF8TextDecode::Decode stub that took glyph indices
when the real one takes stream bytes), and in that case nothing failed at all:
the probe simply tested the wrong decoder and reported OK.

A stub is a SECOND IMPLEMENTATION that nothing checks against the first. This
lint checks the one property that is cheap to check and catches both cases: if a
test defines a free function whose name is also defined in src/, the parameter
COUNT must match. Legitimate thin stubs pass (they mirror the signature);
invented ones do not.
"""
import os, re, sys

SRC   = os.path.join(os.path.dirname(__file__), '..', 'src')
TESTS = os.path.dirname(__file__)

# `static <stuff> Name(args)` at the start of a line -- free functions only.
DEF = re.compile(r'^static\s+[A-Za-z_][\w:<>,\s\*&]*?([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*(?:\{|$)',
                 re.MULTILINE)

def arity(argstr):
    a = argstr.strip()
    if not a or a == 'void':
        return 0
    depth, n = 0, 1
    for c in a:
        if c in '(<[': depth += 1
        elif c in ')>]': depth -= 1
        elif c == ',' and depth == 0: n += 1
    return n

def defs(path):
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return {}
    out = {}
    for m in DEF.finditer(text):
        out.setdefault(m.group(1), set()).add(arity(m.group(2)))
    return out

def main():
    src_defs = {}
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(('.inl', '.cpp', '.h')):
            continue
        for name, ars in defs(os.path.join(SRC, fn)).items():
            src_defs.setdefault(name, {}).setdefault(fn, set()).update(ars)

    bad, scanned = 0, 0
    for fn in sorted(os.listdir(TESTS)):
        if not fn.endswith('.cpp'):
            continue
        path = os.path.join(TESTS, fn)
        text = open(path, encoding='utf-8', errors='replace').read()
        included = set(re.findall(r'#include\s+"([^"]+)"', text))
        scanned += 1
        for name, ars in defs(path).items():
            owners = src_defs.get(name)
            if not owners:
                continue
            # If the probe includes the file that really defines it, this is the
            # real thing, not a stub.
            if any(o in included for o in owners):
                continue
            real = set()
            for s in owners.values():
                real |= s
            if not (ars & real):
                bad += 1
                print("lint_stub: %s stubs %s(%s args) but src/%s defines it with %s"
                      % (fn, name, sorted(ars),
                         sorted(owners)[0], sorted(real)))

    print("lint_stub: scanned %d test file(s)" % scanned)
    if bad:
        print("lint_stub: FAILED (%d stub(s) do not match the real signature)" % bad)
        return 1
    print("lint_stub: OK")
    return 0

if __name__ == '__main__':
    sys.exit(main())
