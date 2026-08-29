#!/usr/bin/env python3
"""lint_msvc_macros -- do not name anything after a windows.h macro.

WHY THIS EXISTS
---------------
v0.63.3 added one line to the space rescue:

    const int far = (abs(x) > abs(y)) ? abs(x) : abs(y);

It compiled clean on this side of the wire and every gate was green. MSVC
answered:

    field_disc3_space.inl(346): error C2513: 'const int': no variable declared
                                before '='

windows.h still defines `far`, `near`, `pascal` and `huge` as EMPTY macros --
the segmented-memory keywords of 16-bit Windows, kept for source compatibility
thirty years after the memory model that needed them. So `const int far = ...`
reaches the compiler as `const int = ...`.

The host probes hand-roll their Win32 surface, which is why they did not see it.
tests/winshim/windows.h and tests/disc3_wiring_compile.cpp now define these
macros so the probes that use them do fail -- but a lint is translation-unit
independent, and this is the second time a v0.6x release has reached Aaron's
compiler because a probe compiled a different program from the mod (the first
was v0.62.3's JsmGateSatisfied, which is why tests/lint_harness_includes.py
exists).

Comments and string literals are stripped first, so prose may say "a long way
out" and a screen may read "Boost if she is far out" without tripping it.
"""
import os, re, sys

SRC = os.path.join(os.path.dirname(__file__), '..', 'src')

# The EMPTY macros only. windows.h also defines WINAPI, CALLBACK and CDECL, but
# those expand to calling conventions and the mod uses them for exactly that --
# `static DWORD WINAPI AccessibilityThread(LPVOID)` is correct code, not a
# collision. What this lint is for is the set that expands to NOTHING, because
# that is the set whose misuse compiles here and vanishes there.
RESERVED = ['far', 'near', 'pascal', 'huge', 'FAR', 'NEAR', 'PASCAL', 'HUGE']

def strip(src):
    """Remove block comments, line comments and string/char literals."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i+1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == '/' and i + 1 < n and src[i+1] == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
            out.append(' ')
        elif c in '"\'':
            q, i = c, i + 1
            while i < n:
                if src[i] == '\\': i += 2; continue
                if src[i] == q: i += 1; break
                i += 1
            out.append(' ')
        else:
            out.append(c)
            i += 1
    return ''.join(out)

def main():
    bad = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(('.cpp', '.inl', '.h')):
            continue
        path = os.path.join(SRC, name)
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            raw = f.read()
        code = strip(raw)
        for lineno, line in enumerate(code.split('\n'), 1):
            for word in RESERVED:
                if re.search(r'\b' + word + r'\b', line):
                    bad.append((name, lineno, word,
                                raw.split('\n')[lineno-1].strip()[:100]))
    if bad:
        print("FAIL: identifiers that windows.h will erase or rewrite:")
        for name, lineno, word, text in bad:
            print("  %s(%d): '%s'  ->  %s" % (name, lineno, word, text))
        print("\nwindows.h defines far/near/pascal/huge as EMPTY macros. Rename the")
        print("identifier -- MSVC will not see it at all.")
        return 1
    print("lint_msvc_macros: OK (no src identifier collides with a windows.h macro)")
    return 0

if __name__ == '__main__':
    sys.exit(main())
