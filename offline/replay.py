#!/usr/bin/env python3
"""REPLAY CONFORMANCE -- does the offline model agree with the game?

PHASE 1 of the world-map navigation recovery plan, and the test that has been
missing since v0.20.60.

Every rule in this system was derived by me, encoded in BOTH world_garden*.inl
and gsim3/gexec3, and then "validated" by running the simulator -- which embeds
the same derived rule and therefore can never falsify it. A clean 2,112-run
matrix only ever proved the C++ agreed with my assumptions.

[GDTRACE] logs, once a second, the state the mod actually computed in-game:

    pos=(x,y) hd= cell=(r,c) clear= steer=(x,y) wp=i/n goal= remain= off=
    blk= aim= guard=on/dir/frames rev= replans= mv=n/N gate= cls=0xNN keys=ULRD

Each of those is a claim about the world that the offline model can be asked to
reproduce AT THE SAME POSITION, with no closed-loop drift to muddy it. Where the
two disagree, the model is wrong and the disagreement names the place.

This checks, per sample:
    cell    -- coordinate transform
    cls     -- the cell-class grid (walk/park/foot/water/beach/edges)
    clear   -- the clearance transform
    blk     -- the bow probes, at the logged heading
    aim     -- line of sight to the logged steer target

It deliberately does NOT re-run the closed loop: a single wrong step there makes
every later sample diverge for uninteresting reasons. Conformance first,
trajectory second.

Usage:  python3 offline/replay.py <ff8_world.log> [more logs...]
Exit:   0 = the model conforms, 1 = it does not
"""
import re
import sys
import math

sys.path.insert(0, '/root/work')

TRACE = re.compile(
    r'\[GDTRACE\] pos=\((-?\d+),(-?\d+)\) hd=(\d+) cell=\((-?\d+),(-?\d+)\) '
    r'clear=(-?\d+) steer=\((-?\d+),(-?\d+)\) wp=(\d+)/(\d+) goal=(\d+) '
    r'remain=(\d+) off=(\d+) blk=(\d) aim=(\d) guard=(\d)/(-?\d+)/(\d+) '
    r'rev=(\d) replans=(\d+)(?: mv=(\d+)/(\d+))?(?: gate=(-?\d+) cls=0x([0-9A-Fa-f]+))?'
)


def parse(paths):
    out = []
    for p in paths:
        with open(p, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = TRACE.search(line)
                if not m:
                    continue
                g = m.groups()
                # groups, 0-indexed: 0 x, 1 y, 2 hd, 3 row, 4 col, 5 clear,
                # 6 sx, 7 sy, 8 wp_i, 9 wp_n, 10 goal, 11 remain, 12 off,
                # 13 blk, 14 aim, 15 guard_on, 16 guard_dir, 17 guard_frames,
                # 18 rev, 19 replans, 20 mv_n, 21 mv_N, 22 gate, 23 cls.
                # (The first version of this miscounted from 9 onward and
                # reported blk=1260 -- an off-by-one in the harness built to
                # catch off-by-ones. Indices are spelled out now.)
                out.append(dict(
                    x=int(g[0]), y=int(g[1]), hd=int(g[2]),
                    row=int(g[3]), col=int(g[4]), clear=int(g[5]),
                    sx=int(g[6]), sy=int(g[7]),
                    goal=int(g[10]), off=int(g[12]),
                    blk=int(g[13]), aim=int(g[14]),
                    guard=int(g[15]),
                    cls=int(g[23], 16) if g[23] is not None else None,
                ))
    return out


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    import numpy as np
    import gsim3 as G
    import gexec3 as X

    rows = parse(paths)
    if not rows:
        print("no [GDTRACE] samples found")
        return 2

    # the C++ cell-class dump, so `cls` can be compared bit for bit
    try:
        cls_grid = np.fromfile('/root/work/gcheck/gdcls.bin',
                               dtype=np.uint8).reshape(G.RP, G.KP)
    except Exception:
        cls_grid = None

    stats = {k: [0, 0] for k in ('cell', 'cls', 'clear', 'blk', 'aim')}
    first = {}

    for i, r in enumerate(rows):
        # --- coordinate transform
        pr, pc = G.g2cp(r['x'], r['y'])
        ok = (pr == r['row'] and pc == r['col'])
        stats['cell'][ok] += 1
        if not ok:
            first.setdefault('cell', (i, f"game=({r['row']},{r['col']}) model=({pr},{pc})"))

        # --- cell class
        if r['cls'] is not None and cls_grid is not None:
            mine = int(cls_grid[pr, pc])
            ok = (mine == r['cls'])
            stats['cls'][ok] += 1
            if not ok:
                first.setdefault('cls', (i, f"game=0x{r['cls']:02X} model=0x{mine:02X} "
                                            f"at ({r['x']},{r['y']})"))

        # --- clearance
        mine = int(G.CLEAR[pr, pc])
        ok = (mine == r['clear'])
        stats['clear'][ok] += 1
        if not ok:
            first.setdefault('clear', (i, f"game={r['clear']} model={mine} "
                                          f"at ({r['x']},{r['y']})"))

        # --- bow probes at the logged heading
        pn, pf = X.probes_for(r['x'], r['y'])
        mine = 1 if (X.probe(r['x'], r['y'], r['hd'], pn) or
                     X.probe(r['x'], r['y'], r['hd'], pf)) else 0
        ok = (mine == r['blk'])
        stats['blk'][ok] += 1
        if not ok:
            first.setdefault('blk', (i, f"game={r['blk']} model={mine} at ({r['x']},{r['y']}) "
                                        f"hd={r['hd']} probes=({pn},{pf})"))

        # --- line of sight to the steer target the mod actually used
        mine = 1 if X.line_clear(r['x'], r['y'], r['sx'], r['sy']) else 0
        ok = (mine == r['aim'])
        stats['aim'][ok] += 1
        if not ok:
            first.setdefault('aim', (i, f"game={r['aim']} model={mine} at ({r['x']},{r['y']}) "
                                        f"-> steer=({r['sx']},{r['sy']})"))

    print(f"replayed {len(rows)} [GDTRACE] samples from {len(paths)} log(s)\n")
    print(f"{'check':8s} {'agree':>8s} {'disagree':>9s} {'rate':>8s}")
    bad = 0
    for k in ('cell', 'cls', 'clear', 'blk', 'aim'):
        no, yes = stats[k]
        tot = no + yes
        if not tot:
            print(f"{k:8s} {'-':>8s} {'-':>9s}      n/a")
            continue
        rate = 100.0 * yes / tot
        flag = '' if no == 0 else '   <-- DIVERGENCE'
        if no:
            bad += no
        print(f"{k:8s} {yes:8,d} {no:9,d} {rate:7.2f}%{flag}")
        if k in first:
            idx, why = first[k]
            print(f"         first at sample {idx}: {why}")

    if bad:
        print(f"\nREPLAY FAIL: {bad:,} disagreements. The offline model does not "
              f"describe what the game did, so offline results about it are not evidence.")
        return 1
    print("\nREPLAY OK: at every logged position the model computed what the game computed.")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
