#!/usr/bin/env python3
"""v0.20.84 regression: every shipped berth, from four representative starts.

The starts are chosen to be the three places the v0.20.83 BAT actually left the
hull -- Balamb (the save's start), the Tomb berth, and the Trabia berth -- plus
the Centra berth for the long southern crossing. The Tomb start is the one that
matters: from there v0.20.83 answered "plan FAILED" for anything on Balamb
island, because the OLD height-based beach rule made that island a one-way
pocket.

Usage:  python3 offline/regress84.py [--full]
"""
import sys
import re

sys.path.insert(0, '/root/work')
from gexec3 import run                                              # noqa: E402

STARTS = [
    ("Balamb (save start)", 20271, -24355),
    ("Tomb berth", -44672, -37248),
    ("Trabia berth", 51072, -56960),
    ("Centra berth", 9344, 54400),
]
HEADINGS = (0, 1024, 2048, 3072)


def berths():
    src = open('/root/work/src/world_garden_berths.inl').read()
    out = []
    skipped = []
    for m in re.finditer(
            r'\{\s*"([^"]+)",\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(true|false),\s*(true|false),'
            r'\s*(-?\d+),\s*(-?\d+),\s*(true|false)', src):
        name, x, y, w, ok, drive, dx, dy, beach = m.groups()
        if ok != 'true' or drive != 'false':
            continue
        if beach == 'true':
            # v0.20.96: gexec3 has no GdBeachOpen and no approach-point routing,
            # so a beach_climb berth is unrepresentable offline. Skip it LOUDLY --
            # a silent drop here would read as "everything passes".
            skipped.append(name)
            continue
        out.append((name, int(x), int(y)))
    for n in skipped:
        print(f"SKIPPED (beach_climb, not modelled offline): {n}")
    return out


def main(full=False):
    dests = berths()
    print(f"{len(dests)} berths x {len(STARTS)} starts x {len(HEADINGS)} headings "
          f"= {len(dests) * len(STARTS) * len(HEADINGS)} runs\n")
    bad = []
    tot = 0
    for sname, sx, sy in STARTS:
        fails = 0
        for name, px, py in dests:
            if abs(px - sx) < 400 and abs(py - sy) < 400:
                continue
            for hd in HEADINGS:
                tot += 1
                r = run(sx, sy, hd, px, py, speed=64, turn=32)
                if not r['ok']:
                    fails += 1
                    bad.append((sname, name, hd, r))
                    print(f"FAIL  {sname:20s} -> {name:26s} hd={hd:4d} "
                          f"why={r['why']:10s} gd={r.get('gd')} replans={r['replans']}")
                    sys.stdout.flush()
        print(f"  {sname:20s} {fails} failures")
    print(f"\nTOTAL {tot}  FAILURES {len(bad)}")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main('--full' in sys.argv))
