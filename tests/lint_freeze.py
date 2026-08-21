#!/usr/bin/env python3
"""lint_freeze.py -- the ATB freeze and the status-timer hold must never drift.

WHY THIS EXISTS

v0.37.0 (#95). Enhanced Wait Mode holds the ATB by letting the engine's ATB
function run and then restoring the gauges. The engine reads a flag that
function sets (0x01D28DEB) to decide whether to age every timed status, so for
two years EWM held the gauges while Aura, Haste, Protect, Shell, Regen and
Reflect kept expiring in real time -- a blind player pays for that harder than
anyone, because reading a menu costs seconds a sighted player never spends.

The fix pairs the ATB freeze with a status-timer hold. The failure mode to guard
against is not the fix being wrong; it is the two flags DRIFTING APART later,
because one of them gets set at a new site and the other does not. That is
exactly what happened to `s_blockProcessReady`, which was maintained faithfully
for eleven versions while the hook it gated had never been installed.

THE RULE

`s_ewmShouldCap` and `s_holdStatusTimers` may only be assigned inside
EWM_SetFreeze(). Everywhere else must call EWM_SetFreeze().

Usage:  python3 tests/lint_freeze.py [src_dir]
Exit:   0 clean, 1 if the two can disagree.
"""
import os
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', 'src')
SRC = os.path.normpath(SRC)

FLAGS = ('s_ewmShouldCap', 's_holdStatusTimers')
SETTER = 'EWM_SetFreeze'

# `static volatile bool s_ewmShouldCap = false;` is the declaration, not a
# reassignment; so is the definition inside the setter.
DECL = re.compile(r'\b(?:static|volatile|bool)\b[^;=]*\b(%s)\s*=' % '|'.join(FLAGS))
ASSIGN = re.compile(r'(?<![\w.>])(%s)\s*=(?!=)' % '|'.join(FLAGS))


def setter_span(text):
    """Character range of the EWM_SetFreeze body, if this file defines it."""
    m = re.search(r'\b%s\s*\([^)]*\)\s*\{' % SETTER, text)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return (m.start(), i + 1)
        i += 1
    return (m.start(), len(text))


def main():
    bad = []
    checked = 0
    for root, _dirs, files in os.walk(SRC):
        for name in sorted(files):
            if not name.endswith(('.inl', '.cpp', '.h')):
                continue
            path = os.path.join(root, name)
            with open(path, encoding='utf-8', errors='replace') as f:
                text = f.read()
            if not any(flag in text for flag in FLAGS):
                continue
            checked += 1
            span = setter_span(text)
            # strip comments so a flag NAMED in prose is not a flag ASSIGNED
            stripped = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group(0)),
                              text, flags=re.S)
            stripped = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), stripped)
            for m in ASSIGN.finditer(stripped):
                if span and span[0] <= m.start() < span[1]:
                    continue
                if DECL.search(stripped, max(0, m.start() - 60), m.end()):
                    continue
                line = stripped.count('\n', 0, m.start()) + 1
                bad.append('%s:%d: %s assigned outside %s() -- call %s() instead'
                           % (os.path.relpath(path, SRC), line, m.group(1),
                              SETTER, SETTER))
    for b in bad:
        print('lint_freeze: ' + b)
    if bad:
        print('lint_freeze: FAILED (%d)' % len(bad))
        return 1
    print('lint_freeze: OK (%d files carry the freeze flags)' % checked)
    return 0


if __name__ == '__main__':
    sys.exit(main())
