#!/usr/bin/env python3
"""lint_braces -- every source file must balance its own braces and parentheses.

WHY THIS EXISTS
---------------
v0.52.0 added four lines to world_map_segments.inl and left a stray `}` behind:

    WmDumpWorldmapSlots();
    }                          <- closes nothing

MSVC reported it as `error C2059: syntax error: 'for'` sixty lines later and
then twenty more errors after that, and Aaron lost a build cycle to it.

The reason it got out is specific and worth naming: **no host probe compiles
world_map_segments.inl.** The garden harness compiles the garden files, the
catalog probe compiles world_catalog.inl, and everything else in the world-map
set is only ever built by MSVC on Aaron's machine. For those files the first
syntax check in the loop is the BAT, which is the most expensive place to put
one.

This is the cheap check that catches the whole class: strip comments and string
literals, then count. A file that is a complete translation unit or a complete
textual include must come out at zero.

THE ONE ALLOWED PAIR
--------------------
world_map_drive.inl opens a function that world_map_drive_exec.inl closes -- a
deliberate split made for the 80 KB guard. They are checked as a pair instead,
and the pair must still balance, so a stray brace in either is still caught.
"""
import glob
import os
import sys

SRC = os.path.join(os.path.dirname(__file__), '..', 'src')

# Files that do not balance alone, and the group they balance within.
PAIRS = [('world_map_drive.inl', 'world_map_drive_exec.inl')]


def strip(src):
    """Remove // and /* */ comments and string/char literals."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n:
                if src[i] == '\\':
                    i += 2
                    continue
                if src[i] == q:
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def counts(path):
    t = strip(open(path, encoding='utf-8', errors='replace').read())
    return t.count('{') - t.count('}'), t.count('(') - t.count(')')


def main():
    files = sorted(glob.glob(os.path.join(SRC, '*.inl')) +
                   glob.glob(os.path.join(SRC, '*.cpp')) +
                   glob.glob(os.path.join(SRC, '*.h')))
    paired = {f for p in PAIRS for f in p}
    bad = 0
    scanned = 0
    for f in files:
        name = os.path.basename(f)
        if name in paired:
            continue
        scanned += 1
        b, p = counts(f)
        if b or p:
            bad += 1
            print("lint_braces: %s is unbalanced: braces %+d, parens %+d" % (name, b, p))
    for pair in PAIRS:
        tb = tp = 0
        missing = False
        for name in pair:
            path = os.path.join(SRC, name)
            if not os.path.exists(path):
                print("lint_braces: %s in a declared pair does not exist" % name)
                missing = True
                bad += 1
                continue
            scanned += 1
            b, p = counts(path)
            tb += b
            tp += p
        if not missing and (tb or tp):
            bad += 1
            print("lint_braces: the pair %s does not balance: braces %+d, parens %+d"
                  % (' + '.join(pair), tb, tp))

    print("lint_braces: scanned %d source file(s)" % scanned)
    print("lint_braces: FAILED (%d)" % bad if bad else "lint_braces: OK")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
