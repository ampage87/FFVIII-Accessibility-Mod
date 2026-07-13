# Offline World-Map Walking Rebuild — Manifest

## ★ 2026-07-01 update (v0.18.3.201 session) — camera transform added, code PERSISTED
The Python tooling now lives IN THIS FOLDER (it previously existed only in a session
sandbox and was lost):
- `extract_wmx.py` — re-extracts `wmx.obj` (30,781,440 bytes) from `world.fs` (FI entry 9);
  run it first in any new sandbox (the 30 MB binary itself is not kept in the repo).
- `ff8_walkmesh.py` — the validated oracle (locator/loader/find-poly/height/step gate +
  flood/route). Rebuilt this session; landmarks match to <=0.1u, mesh stats exact
  (473,193 polys / 101,703 bit7-walkable / 157,416 terrain-rule).
- `nav_sim.py` — NEW: camera-faithful navigation simulator. Models the exe-verified
  input->heading->camera physics (heading = camYaw + key*512 + bias/2; velocity-driven
  camera follow; 0x200/frame turn; step gate + wall slide), the mod's grid planner, and
  BOTH executors (.200 8-way keys vs .201 camera-write). All 24 directed validation
  routes arrive (see `SIM_CAMERA_RESULTS.md`).
- `CAMERA_EXE_ANALYSIS.md` — NEW: definitive FF8_EN.exe camera/input reverse-engineering
  (memory map, per-frame equations, control spec). Read before ANY steering work.
- `SIM_CAMERA_RESULTS.md` — NEW: the 24/24 validation matrix + robustness results.

The manifest below describes the ORIGINAL (2026-06-27) rebuild and remains accurate for
`REQUIREMENTS.md`/`VALIDATION.md`/`PATCH_phase5.md`; its "delivered as file cards" note
is obsolete — the code is now checked in here.

Offline-first rebuild of the walking-only world-map navigation (issue #70 / #67),
done against primary sources only: `FF8_EN.exe`, raw `wmx.obj` bytes, and the live
`[GROUNDH]` traces. Phases 1-4 are complete and validated here; Phase 5 (implementing
in the mod) is specified and left for Aaron to BAT + push. **Nothing here was pushed;
issue #70 is untouched.**

## Read these
- `PATCH_phase5.md` — **the proposed fix.** Root cause + a proven one-line patch
  (`oy + lvy` -> `oy - lvy` in `LoadTerrainGrid`) that turns the wedge's wrong ~-200 into
  the engine's correct ~-595, with before/after over 205 engine samples. Reviewable; not
  applied, not pushed.
- `REQUIREMENTS.md` — the full spec: function map (Phase 1), data format + the
  file<->game coordinate transform (Phase 2), the ported movement model (Phase 3),
  router (Phase 4), and the implementation requirements (Phase 5).
- `VALIDATION.md` — trace-match table, map-wide landmark cross-check, and the
  three-trio acceptance results.

## Code + data (delivered as file cards; drop into this folder)
- `ff8_walkmesh.py` — the faithful port: locator + block loader + find-poly + planar
  height interp + the step gate, plus `flood()`/`route()` walking router. Importable
  library; run directly to re-print the Galbadia trace validation and mesh stats.
- `run_offline.py` — driver: extract dataset, validate vs trace, run trio acceptance,
  emit `offline_report.json` + `walkmesh_gamecoords.bin` + a sample route.
- `disasm.py` — small capstone helper used for Phase 1 (point it at `FF8_EN.exe`).
- `offline_report.json` — machine-readable results (trace errors, acceptance, stats).
- `walkmesh_gamecoords.bin` — extracted walkable triangles in game coords
  (header `F8WM`, uint32 triCount, then 3 vertices x int16 (gx,gz) per triangle).

## How to run
```
# 1) get wmx.obj (uncompressed FI entry 9 inside world.fs; or use the uploaded world.zip)
python3 ff8/ff8_walkmesh.py /path/to/wmx.obj      # trace validation + mesh stats
python3 ff8/run_offline.py  /path/to/wmx.obj      # full report + dataset + route
```
Needs `capstone` + `pefile` only for `disasm.py` (Phase 1); the mesh/router code is
pure-Python stdlib.

## Headline results
- Faithful port reproduces the engine's `[0x203FE30]` at the Galbadia wedge: 15/15
  samples, **max error 3.57 / mean 0.83 units**. The deep ~-595 ground the mod's
  filter was dropping is present and walkable in the raw mesh.
- Raw mesh: 473,193 polys; 101,703 foot-walkable; 98,814 welded land triangles.
- **All three acceptance trios PASS bidirectionally** (Galbadia, Balamb, Esthar),
  including the historical "Dollet works, Timber doesn't" failure.

## Root cause (one line)
The navmesh is mirrored along Y inside every block: `LoadTerrainGrid` places each vertex
as `oy + lvy`, but the wmx vertex Y (word2) runs 0..-2048 while the height query's mesh
frame runs the other way. Every triangle ends up ~one block off in Y, so the query reads
the adjacent shallow coast (~-200) instead of the deep ground (~-595) -> the 200 step gate
severs the connection -> the wedge. Fix: `oy - lvy`. See `PATCH_phase5.md`.

## Guardrails honored
Primary sources only; research docs treated as leads (and one vertex-layout / filter
claim corrected against the binary). No GitHub push, no comment/close on #70, no mod
`src/` changes this session. Block-local height query kept. All steps are
screen-reader / automation friendly (no sighted-assistance instructions).
