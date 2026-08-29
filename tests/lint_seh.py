#!/usr/bin/env python3
"""lint_seh.py -- catch MSVC C2712 before MSVC does.

    error C2712: Cannot use __try in functions that require object unwinding

The host harnesses compile with g++, where `__try` is a macro that expands to
`if(1)`, so this class of error is INVISIBLE to every pre-build check the mod
has. v0.20.109 shipped it: one std::string inside a __try block, and the only
thing that noticed was Aaron's build.

The rule MSVC applies is per-FUNCTION, not per-block -- any object with a
destructor anywhere in a function that also contains __try is an error, even if
the object is declared outside the __try. So this scans whole functions.

Usage:  python3 tests/lint_seh.py [src_dir]
Exit:   0 clean, 1 if anything looks like C2712.
"""
import os
import re
import sys

# Types whose locals require unwinding. Deliberately conservative: this list is
# what the mod actually uses, and a false positive costs one comment while a
# false negative costs a build.
UNWINDING = re.compile(
    r'\b(?:std::(?:string|wstring|vector|map|set|unique_ptr|shared_ptr|'
    r'ostringstream|istringstream|stringstream)|'
    r'std::to_string|std::move)\b')

# v0.59.0: a LAMBDA in the same function as __try. A captureless lambda has a
# trivial destructor and MSVC very probably accepts it -- but "very probably"
# is a build cycle to verify and Aaron is the only person who can run one, so
# this is flagged and the fix is always the same: hoist it to a plain function.
# Matches `[](`, `[&](`, `[=](`, `[x](` at the start of a lambda introducer.
LAMBDA = re.compile(r'=\s*\[[^\]]*\]\s*\(')

# A function body opener at file scope: "static <stuff> Name(...)\n{"
FUNC = re.compile(r'^[ \t]*(?:static\s+|inline\s+)*[A-Za-z_][\w:<>*&\s]*?'
                  r'\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?\{?\s*$', re.M)


def functions(text):
    """Yield (name, start_line, body) for brace-balanced top-level functions."""
    lines = text.split('\n')
    i = 0
    while i < len(lines):
        m = FUNC.match(lines[i])
        if not m:
            i += 1
            continue
        # find the opening brace (same line or the next non-blank one)
        j = i
        while j < len(lines) and '{' not in lines[j]:
            if lines[j].rstrip().endswith(';'):
                break
            j += 1
        if j >= len(lines) or '{' not in lines[j]:
            i += 1
            continue
        depth, k, body = 0, j, []
        while k < len(lines):
            depth += lines[k].count('{') - lines[k].count('}')
            body.append(lines[k])
            if depth <= 0 and k >= j:
                break
            k += 1
        yield m.group(1), i + 1, '\n'.join(body)
        i = max(k, i + 1)


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'src'
    bad = []
    scanned = 0
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith(('.inl', '.cpp', '.h')):
                continue
            path = os.path.join(dirpath, fn)
            try:
                text = open(path, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            if '__try' not in text:
                continue
            scanned += 1
            for name, line, body in functions(text):
                code = strip_comments(body)
                if '__try' not in code:
                    continue
                hit = UNWINDING.search(code)
                if hit:
                    bad.append((path, line, name, hit.group(0)))
                lam = LAMBDA.search(code)
                if lam:
                    bad.append((path, line, name, 'a lambda -- hoist it to a plain function'))

    print("lint_seh: scanned %d file(s) containing __try" % scanned)
    for path, line, name, what in bad:
        print("  C2712 RISK  %s:%d  %s() uses __try and '%s'" %
              (path, line, name, what))
    if bad:
        print("lint_seh: %d problem(s) -- MSVC will reject these" % len(bad))
        return 1
    print("lint_seh: OK")
    return 0


if __name__ == '__main__':
    sys.exit(main())
