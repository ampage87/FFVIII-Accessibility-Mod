#!/usr/bin/env python3
"""v0.62.3.1: the catalog harness must not see a header the mod's own
translation unit does not.

catalog_harness.cpp compiles field_catalog.inl outside field_navigation.cpp, so
anything the harness includes on top is a header the real build does NOT have.
v0.62.3 put JsmGateSatisfied in field_archive_jsm_decode.inl and included that
file from the harness: the harness built, all 46 fixtures passed, the scanner
golden matched -- and the mod failed to compile at Aaron's end with
"'JsmGateSatisfied': identifier not found". Every local gate was green because
every local gate was compiling a different program.

The rule: a src/ header the harness includes must also be reachable from
field_navigation.cpp (directly or through one hop of its .inl chain).
"""
import os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
INC  = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)

def includes(path):
    try:
        return set(INC.findall(open(path, encoding='utf-8', errors='replace').read()))
    except OSError:
        return set()

def reachable_from(start, depth=6):
    seen, frontier = set(), {start}
    while frontier and depth:
        nxt = set()
        for f in frontier:
            for inc in includes(os.path.join(ROOT, 'src', f)):
                if inc not in seen:
                    seen.add(inc); nxt.add(inc)
        frontier, depth = nxt, depth - 1
    return seen

mod  = reachable_from('field_navigation.cpp')
fail = 0
for harness in ('catalog_harness.cpp',):
    for inc in sorted(includes(os.path.join(ROOT, 'tests', harness))):
        if not os.path.exists(os.path.join(ROOT, 'src', inc)):
            continue                      # tests-local or system header
        if inc not in mod:
            print("%s includes src/%s, which field_navigation.cpp never sees --"
                  " the harness would compile a program the mod does not"
                  % (harness, inc))
            fail += 1
sys.exit(1 if fail else 0)
