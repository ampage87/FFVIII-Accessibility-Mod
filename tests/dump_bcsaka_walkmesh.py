#!/usr/bin/env python3
"""
dump_bcsaka_walkmesh.py - one-off: extract ONLY bcsaka_1's walkmesh slice from
the committed Plan & Research Documents/ff8_walkmeshes.json into a small JSON
the Step-2 (Balamb Hotel A* trigger-line) offline proof can read.

Does NOT touch the committed chase_fixtures.h (whose mesh-integrity check gates
deploy/CI). Output is tests/bcsaka_1_walkmesh.json (~tens of KB).

USAGE (from repo root or tests/):
  python tests/dump_bcsaka_walkmesh.py
"""

import json
import os
import sys

FIELD = "bcsaka_1"


def locate_extract():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)  # tests/ -> repo root
    return os.path.join(root, "Plan & Research Documents", "ff8_walkmeshes.json")


def main():
    src = locate_extract()
    if not os.path.exists(src):
        print(f"FAIL: walkmesh extract not found at {src}")
        return 1
    with open(src, "r") as f:
        data = json.load(f)
    fields = data.get("fields", {})
    if FIELD not in fields:
        print(f"FAIL: '{FIELD}' absent from extract. Present sample: "
              f"{list(fields.keys())[:8]} ...")
        return 1
    m = fields[FIELD]
    verts = m["vertices"]
    tris = m["triangles"]

    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "bcsaka_1_walkmesh.json")
    with open(out_path, "w") as f:
        json.dump({"field": FIELD, "vertices": verts, "triangles": tris}, f)

    print(f"OK: wrote {out_path}")
    print(f"    {FIELD}: {len(verts)} vertices, {len(tris)} triangles")
    # Sanity echo: which triangle centers are nearest the known spawn/goal.
    def nearest(px, py):
        best, bd = -1, 1e30
        for i, t in enumerate(tris):
            dx = t["center_x"] - px
            dy = t["center_y"] - py
            d = dx * dx + dy * dy
            if d < bd:
                bd, best = d, i
        return best
    sp = nearest(1213.0, 1396.0)
    print(f"    nearest tri to spawn (1213,1396) = tri {sp} "
          f"(live log saw tri 12/13)")
    if len(tris) > 196:
        c = tris[196]
        print(f"    tri 196 center = ({c['center_x']:.0f},{c['center_y']:.0f}) "
              f"(live A* goal)")
    else:
        print(f"    NOTE: mesh has only {len(tris)} tris; live goal was tri 196/197")
    return 0


if __name__ == "__main__":
    sys.exit(main())
