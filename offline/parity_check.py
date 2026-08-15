#!/usr/bin/env python3
"""GARDEN PARITY GUARD -- the offline model must equal the shipped C++.

Run after building tests/garden_harness.cpp, which dumps the C++ cell-class
array to gcheck/gdcls.bin. This compares it against gsim3.py cell for cell.

WHY THIS EXISTS

Twice now the offline model and world_garden_grid.inl have drifted apart, and
both times the offline matrix went on passing a build that did not exist:

  * v0.20.67 -- gsim3 kept a x12 land penalty while the C++ carried a hard
    sea-first mask. Shipped broken.
  * v0.20.77 -- the C++ disembark rule changed twice (all-four sub-points, then
    any sub-point, then any sub-point that is BOTH park AND foot) and the offline
    model was never updated. 9,211 PARK cells and 68 BEACH cells disagreed, and
    beaches are the scarce resource that decides whether a landmass is reachable
    at all.

A clean matrix means nothing if the two sides model different worlds. Run this
BEFORE trusting any offline result, and treat a non-zero total as a hard fail.

Usage:  python3 offline/parity_check.py [gcheck/gdcls.bin]
Exit:   0 = identical, 1 = divergence (prints what and where)
"""
import sys
import numpy as np

sys.path.insert(0, '/root/work')

BITS = [
    ('WALK',   0x01, 'WALK'),
    ('PARK',   0x02, 'PKp'),
    ('FOOT',   0x04, 'FTp'),
    ('OPEN_E', 0x08, 'EOK'),
    ('OPEN_S', 0x10, 'SOK'),
    ('WATER',  0x20, 'WATERp'),
    ('BEACH',  0x40, 'BEACHp'),
    ('PARTIAL', 0x80, 'PARTIALp'),   # v0.20.95
]

# One cell may legitimately differ on PARK: the C++ tracks which sub-points were
# ever covered by a polygon ("seen") and the offline rasteriser has no such
# concept, so a cell with an uncovered sub-point can fall either way. Anything
# beyond that is a real divergence.
TOLERANCE = {'PARK': 1}


def main(path='/root/work/gcheck/gdcls.bin'):
    import gsim3 as G
    cls = np.fromfile(path, dtype=np.uint8)
    if cls.size != G.RP * G.KP:
        print(f"FAIL: {path} is {cls.size} bytes, expected {G.RP * G.KP}")
        return 1
    cls = cls.reshape(G.RP, G.KP)
    bad = 0
    print(f"{'field':8s} {'C++':>10s} {'python':>10s} {'differ':>8s}")
    for name, bit, attr in BITS:
        c = (cls & bit) != 0
        p = getattr(G, attr)
        d = int((c != p).sum())
        allowed = TOLERANCE.get(name, 0)
        flag = '' if d <= allowed else '   <-- DIVERGENCE'
        if d > allowed:
            bad += d
        print(f"{name:8s} {int(c.sum()):10,d} {int(p.sum()):10,d} {d:8,d}{flag}")
        if d > allowed:
            rs, cs = np.nonzero(c != p)
            for r, cc in list(zip(rs, cs))[:5]:
                print(f"         first mismatch at cell ({r},{cc}): "
                      f"C++={bool(c[r, cc])} python={bool(p[r, cc])}")
    # v0.20.85: the CLEARANCE field, which was unguarded until the .84 replay
    # found the two sides disagreeing at the pole seam -- the C++ chamfer did not
    # wrap rows, scipy's transform treated the array edge as background, and the
    # two errors pointed opposite ways so nothing offline could see it.
    try:
        import numpy as _np
        cl = _np.fromfile(path.replace('gdcls', 'gdclear'), dtype=_np.uint8)
        if cl.size == G.RP * G.KP:
            cl = cl.reshape(G.RP, G.KP)
            d = int((cl != G.CLEAR).sum())
            print(f"{'CLEAR':8s} {'-':>10s} {'-':>10s} {d:8,d}"
                  f"{'' if d == 0 else '   <-- DIVERGENCE'}")
            if d:
                bad += d
                rs, cs = _np.nonzero(cl != G.CLEAR)
                for r, c in list(zip(rs, cs))[:5]:
                    print(f"         first mismatch at cell ({r},{c}): "
                          f"C++={int(cl[r, c])} python={int(G.CLEAR[r, c])}")
    except FileNotFoundError:
        print(f"{'CLEAR':8s} {'-':>10s} {'-':>10s} {'not dumped':>8s}")

    if bad:
        print(f"\nPARITY FAIL: {bad:,} cells disagree. The offline matrix is NOT "
              f"testing the shipped build -- fix before trusting any result.")
        return 1
    print("\nPARITY OK: the offline model and the shipped C++ describe the same world.")
    return 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
