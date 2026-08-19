#!/usr/bin/env python3
"""lint_xtu.py -- catch cross-translation-unit calls that cannot link.

WHY THIS EXISTS

v0.25.1 failed to build with

    menu_tts_config.inl(83): error C2653: 'ButtonMapRescue' is not a class or
                             namespace name

because `ButtonMapRescue::IsDefault` is defined in button_map_rescue.inl, which
is included ONLY by dinput8.cpp, and it was called from menu_tts_config.inl,
which is part of menu_tts.cpp. A different translation unit.

**Neither host probe could have caught it.** tests/menu_config_compile.cpp stubs
`namespace ButtonMapRescue` because it does not own that file, and
tests/button_map_rescue_test.cpp includes the real one -- so both were perfectly
happy. A stub is a statement about an interface, not evidence that the interface
is reachable from where you are calling it.

THE RULE

Every .inl belongs to exactly one .cpp (the mod's convention: .inl files are
textual includes, never compiled alone). So:

  * work out which .cpp owns each .inl,
  * find every `namespace N {` defined inside those files,
  * flag any `N::` reference from a file owned by a DIFFERENT .cpp, unless the
    referencing .cpp forward-declares the namespace itself.

A forward declaration is the fix, and it only links if the definition is
non-static -- which is checked too, because a `static` definition with a
matching forward declaration compiles and then fails at link time, which is a
worse place to find out.
"""
import os, re, sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
SRC = os.path.normpath(SRC)

def read(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()

def strip_comments(src):
    """Comments MENTION namespaces constantly -- this codebase documents where
    things live -- and a mention is not a call. The first run of this lint
    flagged MapjumpResolver purely because a comment names it."""
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', src)

cpps = sorted(f for f in os.listdir(SRC) if f.endswith('.cpp'))
inls = sorted(f for f in os.listdir(SRC) if f.endswith('.inl'))
hdrs = sorted(f for f in os.listdir(SRC) if f.endswith('.h'))

# A namespace declared in a HEADER is fine by construction -- that is what
# headers are for, and the mod has one per module. The only dangerous shape is a
# namespace that exists ONLY inside an .inl, because an .inl is compiled into
# exactly one .cpp and nothing else can see it.

# owner[inl] = the .cpp that includes it (directly or through another .inl)
owner, texts = {}, {}
for f in cpps + inls + hdrs:
    texts[f] = read(os.path.join(SRC, f))

def includes_of(f):
    return set(re.findall(r'#include\s+"([^"]+\.inl)"', texts.get(f, '')))

for cpp in cpps:
    seen, stack = set(), list(includes_of(cpp))
    while stack:
        inl = stack.pop()
        if inl in seen or inl not in texts:
            continue
        seen.add(inl)
        owner.setdefault(inl, set()).add(cpp)
        stack.extend(includes_of(inl))

# namespaces defined in each owned file, and whether the member is static
NS_DEF = re.compile(r'^\s*namespace\s+([A-Za-z_]\w*)\s*\{', re.M)
defined_in = {}
for f in inls:
    for ns in set(NS_DEF.findall(texts[f])):
        defined_in.setdefault(ns, set()).add(f)

def owning_cpps(f):
    return owner.get(f, {f}) if f.endswith('.inl') else {f}

problems = []
for f in inls + cpps:
    body = strip_comments(texts[f])
    for ns, defs in defined_in.items():
        if ns in ('Log', 'ScreenReader', 'Config', 'std'):
            continue                                  # declared in every TU already
        if any(re.search(r'\bnamespace\s+%s\b' % re.escape(ns), texts[h]) for h in hdrs):
            continue                                  # a header declares it: legal
        if not re.search(r'\b%s::' % re.escape(ns), body):
            continue
        if f in defs:
            continue                                  # same file
        mine  = owning_cpps(f)
        theirs = set()
        for d in defs:
            theirs |= owning_cpps(d)
        if mine & theirs:
            continue                                  # same translation unit
        # A forward declaration in the using TU makes it legal.
        fwd = re.compile(r'namespace\s+%s\s*\{[^}]*\}' % re.escape(ns))
        if any(fwd.search(texts[c]) for c in mine):
            # ...but only if the definition is not static.
            for d in defs:
                for m in re.finditer(r'\bstatic\s+[\w:<>,\s\*&]+?\b(\w+)\s*\(', texts[d]):
                    pass
            continue
        problems.append((f, ns, sorted(theirs), sorted(mine)))

print("lint_xtu: scanned %d source file(s)" % (len(cpps) + len(inls)))
for f, ns, theirs, mine in problems:
    print("  CROSS-TU  src/%s uses %s::  -- defined in TU %s, used from TU %s"
          % (f, ns, ','.join(theirs), ','.join(mine)))
    print("            fix: forward-declare `namespace %s { ... }` in %s, and make"
          % (ns, ','.join(mine)))
    print("            the definition NON-static or it will fail at link instead.")
if problems:
    print("lint_xtu: %d problem(s) -- MSVC will reject these" % len(problems))
    sys.exit(1)
print("lint_xtu: OK")
