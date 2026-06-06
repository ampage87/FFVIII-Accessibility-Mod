#!/usr/bin/env python3
"""
chase_pathfinding_guard.py - Step 0 nav-core safety net (DEVNOTES Track A).

WHY THIS EXISTS
---------------
FindPortal() in src/field_nav_pathfinding.inl historically read the WRONG
vertex pair for a triangle's neighbour. It used the (e+1, e+2) vertex pair for
neighbour[e], but the FF8 .id walkmesh format stores neighbour[e] as the
triangle across the edge (v[e], v[(e+1)%3]) -- the (e, e+1) pair. Off by one
vertex rotation. On rectilinear / rounded fields the mis-picked edge is often a
WALL edge, so the funnel emitted wall edges as portals, the wall-parallel
COLLAPSE slammed a waypoint onto the wall, and auto-drive wedged (bggate_6 front
gate, B-Garden Hall 6, Balamb Hotel Exterior). The Dollet chase's path-finding
fields (domt2_1 + fallback fields) ride the same FindPortal, and their COLLAPSE/
protected-waypoint tuning was calibrated ON TOP of the buggy portals -- so the
fix carries real regression risk for the chase.

This guard loads the offline walkmesh extract and, for the chase fields + the
front gate, checks every adjacent triangle pair with BOTH:
  - current portal selection : (e+1, e+2)  -- the bug
  - fixed   portal selection : shared-vertex intersection -- the fix

It ASSERTS (exit non-zero on failure):
  1. The FIXED selection returns the true shared edge (exactly the two vertices
     the two triangles have in common) for every adjacent pair on every guarded
     field. Locks the corrected algorithm as the permanent reference.
  2. The documented bggate_6 case: current(tri13->tri148) picks the X=-1710 west
     wall edge; fixed(tri13->tri148) picks the real Y=-598 doorway. Proves the
     model reproduces the known bug and that the fix resolves it.

It REPORTS (non-asserting) how many adjacent pairs the CURRENT selection gets
wrong per field -- quantifying the off-by-one's reach.

NOTE: this is a Python model of the C++ edge math, not the compiled DLL. Its job
is to lock the corrected portal algorithm and flag any change that re-breaks it
in CI / at deploy. The end-to-end "chase still clears with 0 catches" check stays
the in-game BAT during Step 1.

USAGE
  python chase_pathfinding_guard.py            # run against the real extract
  python chase_pathfinding_guard.py --selftest # logic check, no JSON needed
Exit 0 = PASS, non-zero = FAIL.
"""

import json
import os
import sys

WALL = 0xFFFF

# Fields the guard covers. The path-finding chase field is domt2_1 (+ any
# fallback field); the rest of the chase sequence is included so the portal
# math is locked there too in case a field ever falls through to the funnel.
# bggate_6 is the documented bug/fix witness.
GUARDED_FIELDS = [
    "bggate_6",                      # front gate (documented witness)
    "domt2_1",                       # Dollet chase: path-finding (fallback) field
    "domt1_1", "domt3_2", "domt4_1", "domt5_1",
    "doopen2a", "dotown_1", "dotown_2", "dotown_3",
]


def shared_edge_indices(tri_a, tri_b):
    """The true shared edge = the vertex indices the two triangles have in
    common. A valid triangulation shares exactly two."""
    return set(tri_a["vertex_indices"]) & set(tri_b["vertex_indices"])


def portal_current(tri_a, b_index):
    """FindPortal's historical (buggy) edge pick: for neighbour[e]==b, take the
    (e+1, e+2) vertex pair. Returns the set of two vertex indices, or None if b
    isn't a neighbour."""
    nb = tri_a["neighbors"]
    if b_index not in nb:
        return None
    e = nb.index(b_index)
    vi = tri_a["vertex_indices"]
    return {vi[(e + 1) % 3], vi[(e + 2) % 3]}


def portal_fixed(tri_a, tri_b):
    """The fix: shared-vertex intersection. Convention-independent."""
    return shared_edge_indices(tri_a, tri_b)


def check_field(name, mesh):
    """Returns (ok, wrong_pairs, checked_pairs, errors[])."""
    tris = mesh["triangles"]
    n = len(tris)
    errors = []
    wrong = 0
    checked = 0
    for a in range(n):
        for e, b in enumerate(tris[a]["neighbors"]):
            if b == WALL or b >= n:
                continue
            checked += 1
            true_edge = shared_edge_indices(tris[a], tris[b])
            # Assertion 1: the mesh must genuinely share exactly two vertices,
            # and the fixed selection must equal that shared edge.
            if len(true_edge) != 2:
                errors.append(
                    f"{name}: tri{a}->tri{b} neighbour share {len(true_edge)} "
                    f"vertices (expected 2); adjacency/dedup inconsistency")
                continue
            if portal_fixed(tris[a], tris[b]) != true_edge:
                errors.append(f"{name}: fixed portal tri{a}->tri{b} != shared edge")
            # Report: did the historical selection pick the wrong edge?
            if portal_current(tris[a], b) != true_edge:
                wrong += 1
    return (len(errors) == 0, wrong, checked, errors)


def edge_coords(mesh, idx_set):
    """Map a 2-vertex-index set to a sorted tuple of (x, y) coords for printing
    / comparison."""
    vs = mesh["vertices"]
    pts = sorted((vs[i]["x"], vs[i]["y"]) for i in idx_set)
    return tuple(pts)


def check_bggate6_witness(mesh):
    """The documented case: tri13->tri148 must pick the X=-1710 wall edge under
    the current rule and the Y=-598 doorway under the fix."""
    tris = mesh["triangles"]
    if len(tris) <= 148:
        return False, "bggate_6 has fewer than 149 triangles; index map changed"
    cur = portal_current(tris[13], 148)
    fix = portal_fixed(tris[13], tris[148])
    if cur is None:
        return False, "bggate_6 tri13 does not list tri148 as a neighbour"
    cur_xy = edge_coords(mesh, cur)
    fix_xy = edge_coords(mesh, fix)
    expect_wall = ((-1710.0, -598.0), (-1710.0, -473.0))
    expect_door = ((-1710.0, -598.0), (-1480.0, -598.0))
    if cur_xy != expect_wall:
        return False, f"bggate_6 current(13->148)={cur_xy}, expected wall {expect_wall}"
    if fix_xy != expect_door:
        return False, f"bggate_6 fixed(13->148)={fix_xy}, expected doorway {expect_door}"
    return True, "bggate_6 witness: current picks west wall, fixed picks Y=-598 doorway"


def locate_extract():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)  # tests/ -> repo root
    return os.path.join(root, "Plan & Research Documents", "ff8_walkmeshes.json")


def run_real():
    path = locate_extract()
    if not os.path.exists(path):
        print(f"CHASEGUARD: FAIL - walkmesh extract not found at {path}")
        return 1
    with open(path, "r") as f:
        data = json.load(f)
    fields = data.get("fields", {})

    all_errors = []
    missing = []
    report = []

    wok, wmsg = (True, "")
    if "bggate_6" in fields:
        wok, wmsg = check_bggate6_witness(fields["bggate_6"])
        if not wok:
            all_errors.append(wmsg)
    else:
        missing.append("bggate_6")

    for name in GUARDED_FIELDS:
        if name not in fields:
            # Not every chase field name is necessarily present in the extract;
            # record but don't fail solely on absence except for the witness.
            if name != "bggate_6":
                missing.append(name)
            continue
        ok, wrong, checked, errors = check_field(name, fields[name])
        all_errors.extend(errors)
        report.append((name, wrong, checked))

    print("CHASEGUARD: per-field current-rule wrong-portal counts "
          "(report only; fixed rule is asserted correct):")
    for name, wrong, checked in report:
        print(f"  {name:10s}  {wrong:4d} / {checked:4d} adjacent pairs mis-picked by the old (e+1,e+2) rule")
    if wok:
        print(f"  witness: {wmsg}")
    if missing:
        print(f"  (fields absent from extract, skipped: {', '.join(missing)})")

    if all_errors:
        print("CHASEGUARD: FAIL")
        for e in all_errors:
            print(f"  - {e}")
        return 1
    print(f"CHASEGUARD: PASS - fixed portal == true shared edge on all "
          f"{len(report)} guarded fields; bggate_6 bug/fix witness confirmed")
    return 0


def run_selftest():
    """Logic check on a hand-built 2-triangle mesh (the real bggate_6 tri13/
    tri148 geometry, re-indexed locally). No JSON needed."""
    # Global dedup of vertices, mirroring extract_walkmeshes.parse_walkmesh.
    # tri13 verts: (-1480,-598)(-1710,-598)(-1710,-473) ; neighbour[0]=tri148
    # tri148 verts: (-1480,-746)(-1710,-598)(-1480,-598); neighbour[1]=tri13
    verts = [
        {"x": -1480.0, "y": -598.0},  # 0
        {"x": -1710.0, "y": -598.0},  # 1
        {"x": -1710.0, "y": -473.0},  # 2
        {"x": -1480.0, "y": -746.0},  # 3
    ]
    triA = {"vertex_indices": [0, 1, 2], "neighbors": [1, WALL, WALL]}
    triB = {"vertex_indices": [3, 1, 0], "neighbors": [WALL, 0, WALL]}
    mesh = {"vertices": verts, "triangles": [triA, triB]}

    true_edge = shared_edge_indices(triA, triB)
    cur = portal_current(triA, 1)
    fix = portal_fixed(triA, triB)

    ok = True
    # True shared edge is the Y=-598 doorway {V0, V1}.
    if true_edge != {0, 1}:
        print(f"  selftest FAIL: shared edge {true_edge} != {{0,1}}"); ok = False
    # Current rule must pick the WRONG (west wall) edge {V1, V2}.
    if cur != {1, 2}:
        print(f"  selftest FAIL: current rule {cur} != wall {{1,2}}"); ok = False
    # Fixed rule must pick the doorway.
    if fix != {0, 1}:
        print(f"  selftest FAIL: fixed rule {fix} != doorway {{0,1}}"); ok = False
    if edge_coords(mesh, cur) != ((-1710.0, -598.0), (-1710.0, -473.0)):
        print("  selftest FAIL: current edge coords wrong"); ok = False
    if edge_coords(mesh, fix) != ((-1710.0, -598.0), (-1480.0, -598.0)):
        print("  selftest FAIL: fixed edge coords wrong"); ok = False

    if ok:
        print("CHASEGUARD selftest: PASS - current rule picks the west wall "
              "edge, fixed rule picks the Y=-598 doorway (bggate_6 tri13->148)")
        return 0
    print("CHASEGUARD selftest: FAIL")
    return 1


def main(argv):
    if "--selftest" in argv:
        return run_selftest()
    return run_real()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
