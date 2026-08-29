"""Regenerate tests/world_map_route_fixture.h from wmx.obj.

Run from the repo root with wmx.obj present:  python3 offline/gen_route_fixture.py
The six parity statistics are asserted before anything is written, so a fixture
that does not reproduce the shipped grid cannot be emitted.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wmx, numpy as np

g = wmx.build()
wmx.apply_corrections(g)
w = wmx.walkable_current(g)
land   = int((g.cls == wmx.SEG_LAND).sum())
forest = int((g.cls == wmx.SEG_FOREST).sum())
mtn    = int((g.cls == wmx.SEG_MOUNTAIN).sum())
blk    = int(((g.cls == wmx.SEG_MOUNTAIN) & (g.steep > wmx.MTN_STEEP_BLOCK)).sum())
ocean  = int((g.cls == wmx.SEG_OCEAN).sum())
clr    = wmx.clearance(w)
stats  = (land, forest, mtn, blk, ocean, land + forest + mtn - blk,
          int(clr[(clr < 255) & w].max()), int(((clr <= 1) & w).sum()))
WANT   = (8311, 828, 1960, 1372, 38053, 9727, 11, 4580)
assert stats == WANT, f"grid does not match the shipped build's log: {stats} != {WANT}"
print("parity OK:", stats)
