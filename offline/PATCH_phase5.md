# Phase 5 — Proposed mod patch (reviewable; NOT applied, NOT pushed)

This is a draft for Aaron to review and BAT. Cowork did not edit `src/`, did not push,
and did not touch issue #70.

## TL;DR

The navmesh is **mirrored along the Y (north–south) axis inside every block.** In
`LoadTerrainGrid` (`world_map_segments.inl`) each vertex's in-plane Y is placed as
`oy + lvy`, but `lvy` is the wmx vertex's word2 which runs **0 → −2048**, while the
ground-height query maps game→mesh with `NmGameToMeshY` (which runs the other way).
Net effect: every triangle sits ~one block off in Y, so the height query lands on the
*adjacent, shallower* coastal triangle (~−200) instead of the real deep ground (~−595).
That ~390-unit error is exactly what the 200-unit step gate turns into the wedge.

**Primary fix = one character:** place the vertex Y as `oy − lvy`.

This is not a guess — it is proven below against the engine's own `[0x203FE30]` values.

## Proof (replicated the mod's exact parse, then measured vs 205 engine samples)

Rebuilding the navmesh with the mod's own math (segment origins, ocean drop, the wmx
record layout) reproduces the mod **exactly**: 157,416 triangles — identical to the live
`[NAVMESH] built 157416 triangles` log line.

| Build | (−29196,−27642) eng −596 | (−28675,−27417) eng −526 | (−29173,−27543) eng −587 |
|-------|--------------------------|--------------------------|--------------------------|
| current `oy + lvy` | **−203.5** (the wedge bug) | −149.4 | −193.8 |
| fixed `oy − lvy`   | **−595.4** | −525.4 | −586.4 |

Across **all 205** `[GROUNDH]` samples in the current log:

| Build | mean err | median | max | within 5u |
|-------|---------:|-------:|----:|----------:|
| current placement | (systematically ~390 off) | — | — | ~0 |
| fixed `oy − lvy` (mod's existing nearest-centroid pick) | 1.51 | 0.53 | 190.4 | **203 / 205** |
| fixed + prefer-walkable pick (optional, below) | 0.59 | 0.53 | 5.2 | **204 / 205** |

## Patch 1 (the BAT) — one-line Y placement fix

File: `src/world_map_segments.inl`, inside `LoadTerrainGrid`, the per-vertex loop.

```diff
                 for (int v = 0; v < vertCount; v++) {
                     int16_t lvx = *(const int16_t*)(vertBase + v * 8 + 0);
                     int16_t lvy = *(const int16_t*)(vertBase + v * 8 + 4);
                     vwz[v]      = *(const int16_t*)(vertBase + v * 8 + 2);
                     vwx[v] = ox + lvx;
-                    vwy[v] = oy + lvy;
+                    vwy[v] = oy - lvy;   // wmx vertex Y (word2) runs 0..-2048; mesh frame
+                                         // (NmGameToMeshY) runs the other way. Negating
+                                         // aligns the navmesh with the height query so the
+                                         // engine's deep ground is found instead of the
+                                         // adjacent shallow coast (was the Galbadia wedge).
                 }
```

That is the entire behavioral change for this BAT. Point-in-triangle is sign-agnostic,
block-boundary vertex welding is preserved (shared edges still coincide), and adjacency
is by shared vertices — so flipping the local Y axis is safe; only the placement is
corrected.

### Expected in-game effect
- The Galbadia trio (Timber / Dollet / Galbadia Garden) connects — the "Dollet works,
  Timber doesn't" wedge is from the misplaced shallow surface, now corrected.
- Ground heights map-wide become correct (the `[GROUNDH]` diff should collapse from
  ~390 to ~0 across the whole run).

### BAT checklist (per guardrails)
- One behavioral change only (this line).
- Bump `FF8OPC_VERSION` in `src/ff8_accessibility.h` and add a matching `## vX.Y.Z`
  heading in `CHANGELOG.md` (push utility validates the pair).
- Update `DEVNOTES.md` (10,240-byte cap) and `NEXT_SESSION_PROMPT.md`.
- Walk Balamb Garden → beach and the Galbadia exit; confirm `[GROUNDH] diff` ≈ 0 in
  `Logs/ff8_world.log`, then the Dollet **and onward to Timber** walk.
- Aaron BATs and pushes. Do not close #70 until that walk is confirmed and pushed.

## Patch 2 (optional follow-up BAT) — last 2 overlapping-marker cases

After Patch 1, 2 of 205 samples still read a shallow non-walkable *overlay marker*
polygon that shares the same footprint as the real terrain, because
`WorldGroundHeightLocal` picks the **nearest centroid** among containing triangles. The
engine's standing height is always the **walkable** surface. Two options:

- Store a per-triangle foot-walkable flag at build time —
  `walkable = (poly[0x0F] & 0x80) != 0` (bit 7 of the wmx polygon's byte 15; note this is
  byte **0x0F**, distinct from the `0x0D` terrain byte the mod already reads) — into a new
  `std::vector<uint8_t> s_nmWalk`, then in `WorldGroundHeightLocal`'s candidate selection
  prefer a containing triangle with `s_nmWalk[t]` set (fall back to current logic if none).
- This drops max error from 190.4 → 5.2 over the 205 samples. Low priority: it only
  affects a few overlapping-marker frames and does not change reachability.

## Cleanup candidates (later BATs, after Patch 1 is confirmed)

These hardcoded patches in `LoadTerrainGrid` were compensating for the mirror and are
likely now unnecessary or counter-productive. Re-evaluate **one at a time, each its own
BAT**, only after Patch 1 is confirmed good:
- `s_roadFine[56][112] = 1; s_roadFine[57][112] = 1;` (Dollet road-gap bridge).
- The `DOLLET_COAST_*` AABB that force-blocks fine cells as `SEG_MOUNTAIN` — with correct
  geometry this may now block real walkable ground.
- `[ROADFLOOR]` clamp (road triangles deeper than −1000 raised to −500) — a mean-of-corners
  artifact fix that may be moot once placement is correct.
- The step-distance / road-cell proximity bridges in `Navmesh_Build` (`NM_STEP_LINK`,
  `ROAD_BRIDGE`) — verify they are still needed once adjacency is correct.

## Why this matches everything we know
- Reproduces the live trace exactly (current → −203, fixed → −595; `contain=1`).
- Reproduces the mod's triangle count (157,416) — same parse, only the Y sign differs.
- Consistent with the disassembly: the engine's locator places blocks with
  `rowEng = ((0x48000 − gz) % 0x30000)/2048`, i.e. Y increases as game-Z decreases — the
  opposite sense to `oy + lvy`. Negating `lvy` restores that sense.
