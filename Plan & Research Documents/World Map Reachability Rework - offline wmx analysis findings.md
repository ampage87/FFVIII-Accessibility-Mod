# World Map Reachability Rework — Offline wmx.obj Analysis Findings (Issue #67)

Author: Claude, derived from a full offline extraction of `wmx.obj` + `wmsetus.obj`
(parsed byte-exact in a Linux container from Aaron's uploaded `world.fs`/`world.fi`).
This document is AUTHORITATIVE over earlier docs where they conflict — specifically it
corrects (a) the World-Y -> segment-row mapping, (b) the terrain-type-29 label in
`World Map Terrain and Locomotion Reference.md`, and (c) several hardcoded location
coordinates sourced from the ff8-speedruns community data.

## TL;DR

1. **#67 ROOT CAUSE — `WorldYToSegRow` is missing a +98304 half-height centering offset.**
   `WorldXToSegCol` adds +131072 (= half map width, 16*8192) to centre the column.
   `WorldYToSegRow` adds nothing, so it treats world-Y=0 as the north edge instead of the
   vertical centre. The fix mirrors X exactly: add +98304 (= half map height, 12*8192)
   before the wrap. `SegmentCenterToWorld` (the inverse) must subtract the same 98304.
   PROVEN three independent ways (below). Under the current (buggy) mapping only 9/26
   catalog locations land on land; with +98304 it is 26/26.

2. **The segment-grid BFS reachability model is fundamentally unsound** and only separated
   continents *by accident* of the wrong Y mapping. With correct coordinates, NO land/ocean
   threshold on the 32x24 (or 128x96) grid both keeps same-continent coastal locations
   (e.g. Fire Cavern) AND separates continents across thin straits. Majority-rule drops
   coastal land; any-land bridges every continent into one blob.

3. **Correct reachability = rasterize non-ocean polygons into a fine grid and flood-fill in
   continuous space.** Ocean straits block the fill; mesh T-junctions don't. At 1024-unit
   cells (256x192 grid) this is 17/17 correct for the tested locations: Balamb (incl. Fire
   Cavern) isolated, every other continent correctly unreachable on foot.

4. **Terrain type 29 is MOUNTAINS, not "City/Town".** The existing
   `World Map Terrain and Locomotion Reference.md` table is wrong (it took the ff8-speedruns
   label at face value). Evidence: type 29 is the steepest terrain on the map (avg face
   slope 57 deg, 84% of its polys steeper than 30 deg, elevations to 2184), the most common
   land type (54,297 polys ~ 10% of land), and spatially forms the interior ranges of every
   continent — not the ~26 isolated dots cities would be.

5. **Mountains matter for reachability and need gameplay calibration.** Including all type-29
   merges Trabia+Esthar (linked through mountains the player can't cross); excluding all
   type-29 over-fragments (isolates Esthar city). The true walkable boundary within type-29
   is slope-dependent and can't be nailed from geometry alone — it must be calibrated against
   Aaron's actual movement (passive terrain logger).

6. **Several hardcoded catalog coordinates are unreliable placeholders.** Clearest example:
   Fire Cavern at (0x9000, -0x7000) = (36864, -28672) sits in open ocean 5120 units off the
   continent. The real coordinate (30326, -29221) lands on the Balamb landmass. Source is the
   same ff8-speedruns data that gave the bad Y model. The catalog needs a coordinate audit.

## Proof of the Y-offset root cause

- **Brute force.** Searching (xoff, yoff, ysign) for the transform that lands the 26 catalog
  locations on land: current (xoff=131072, yoff=0) = 9/26; best (xoff=131072, yoff=98304,
  ysign=+1) = 26/26. Neighbours +/-4096 give 25/26 — a sharp, unique optimum at exactly
  half-height.
- **Ground truth (Aaron's live position).** Aaron is on the Galbadia continent post-#66. His
  live coord (-29270, -24056) maps under the current mapping to seg(col12,row21) = OCEAN
  (impossible, he's walking on land); under the fix to seg(col12,row9) = LAND, region 0x02.
- **Region clustering.** With the fix, every continent's locations collapse into one region
  family: Balamb 0x00/0x01; Galbadia 0x02/0x03/0x04/0x13; Centra 0x07; Trabia 0x08/0x09;
  Esthar 0x0A (all seven Esthar locations). Under the current mapping they scatter into the sea.

## Corrected coordinate mapping

```
col = ((x + 131072) % 262144) / 8192        // unchanged (already centred)
row = ((y +  98304) % 196608) / 8192        // FIX: was ((y) % 196608) / 8192
// inverse (segment centre):
worldX = col*8192 + 4096 - 131072
worldY = row*8192 + 4096 -  98304           // FIX: was row*8192 + 4096
// fine-grid (1024-unit) cell, same centring:
fineCol = ((x + 131072) % 262144) / 1024    // 0..255
fineRow = ((y +  98304) % 196608) / 1024    // 0..191
// mesh world coord for a player coord (for rasterizing/point tests):
meshX = (x + 131072) % 262144 ; meshY = (y + 98304) % 196608
```

Map = 32x24 segments, 8192 units/cell, width 262144, height 196608. wmx seg index = row*32+col.

## Region -> continent identification (corrected, from the fixed mapping)

```
0x00, 0x01            Balamb         (Balamb Garden, Balamb Town, Dollet, Fire Cavern)
0x0C                  Balamb         (Garden landing pad / Fire Cavern region — vehicle entry)
0x02,0x03,0x04,0x13   Galbadia       (Timber, Galbadia Garden, Deling, Tomb, D-District, Missile)
0x06                  Winhill/Lakeside
0x07                  Centra         (Edea's House, White SeeD Ship, Centra Ruins)
0x08, 0x09, 0x0D      Trabia         (Trabia Garden, Shumi Village)
0x0A, 0x0B            Esthar         (Esthar, FH, Salt Lake, Lunatic Pandora, Lunar Gate, Sorc Mem, Tears Pt)
0x0E                  Cactuar Island
0x0F                  Island Closest To Hell
0x12                  Island Closest To Heaven
```
NOTE: region adjacency in the grid does NOT imply on-foot connectivity (e.g. region 0x00
Balamb and 0x01 Dollet are different continents across water). Use the rasterized flood-fill,
not region grouping, for reachability.

## wmx.obj terrain-type table (byte at polygon +0x0D) — VERIFIED, with mountain correction

```
0-5  Forests (Galbadia/Trabia/Esthar/Centra/Balamb)  LAND, walkable on foot (steep but passable)
6    Plains            7  Esthar Plains              LAND
8    Desert            9  Snow                       LAND
10   Balamb Beach     12  Esthar Bridge             LAND
14   Clifftop  (avg elev 1584 — highest; flat tops)  LAND but high plateau (often unreachable on foot)
15   Cliffside/Grass                                 LAND
16   Galbadia Dirt                                   LAND
27   Railroad         28  Road                       LAND
29   *** MOUNTAINS *** (NOT "City/Town" — doc was wrong)  IMPASSABLE on foot (steep)
32   Ocean-Shallow    33  Ocean-Light  34 Ocean-Dark OCEAN, impassable except Garden/Ragnarok
```
Global poly counts (land types): 7=39656, 29=54297, 6=12426, 14=8445, 2=8557, 9=7343...
Ocean: 34=286459, 33=23914, 32=5404. Bytes 14/15 of the polygon are NOT a walkability flag
(they cluster by terrain category — normal/shading attributes).

## Reachability engine design (validated)

- Build a fine walkable-class grid at module init by rasterizing wmx polygons (point-in-triangle
  on cell centres). Resolution 1024-unit cells = 256 cols x 192 rows = 49152 cells (~48 KB).
  1024 chosen over 512: it gave CLEANER continent separation (coarser cells drop thin false
  coastal bridges) and is proven 17/17. Store 3-state class (land/forest/ocean) per cell so the
  per-vehicle rules survive (car excludes forest; Garden/Ragnarok bypass).
- Reachability = 4-connected flood-fill from the player's fine cell over cells passable for the
  current vehicle, torus-wrapped on both axes. Catalog/planner check the player-and-location's
  fine cell (snap to nearest passable cell within a few cells for coastal coordinate slop).
- Walkable terrain set is CONFIGURABLE (a blocked-type set). Start = ocean only. The Trabia+Esthar
  mountain merge is the only inaccuracy from deferring mountains and does not bite until disc-3
  Esthar; Galbadia/Balamb/Centra are cleanly ocean-separated regardless. Tighten the blocked set
  to include impassable mountains (type 29 above a slope threshold) AFTER calibrating against the
  passive terrain logger.

## Validated continent grouping (1024-cell, walkable = non-ocean, CORRECT coords)

```
Balamb:    Balamb Garden, Balamb Town, Fire Cavern            (isolated)   CORRECT
Galbadia:  Dollet, Galbadia Garden, Deling, Timber, Winhill   (one comp)   CORRECT
Centra:    Centra Ruins, White SeeD                                         CORRECT
Esthar+Trabia merged via mountains (FH, Lunatic, Esthar, Trabia)           needs mountain calib
Islands:   Cactuar, Island Closest To Hell, Island Closest To Heaven       CORRECT (isolated)
```

## Staged implementation plan (Option A — Aaron approved)

- **BAT 1**: Y-offset fix (`WorldYToSegRow` + `SegmentCenterToWorld`, geometry.inl) + new fine
  rasterized walk grid + fine flood-fill reachability; switch the CATALOG reachability filter to
  the fine model (walkable = non-ocean). Add the passive terrain logger (logs the terrain-type
  breakdown of every cell the player occupies, to calibrate mountains). Update the CI harness
  (tests/world_map_harness.cpp + terrain fixture) to the fine model and the REAL Fire Cavern
  coordinate (30326,-29221). BAT on Galbadia (the fix) AND Balamb (regression).
- **BAT 2**: mountain calibration — tighten the blocked-terrain set to exclude impassable
  type-29 (likely slope-gated), verified against BAT-1 logger data; align the A* planner's
  region/segment logic with the corrected mapping if needed.
- **BAT 3**: catalog coordinate audit/cleanup against the verified atlas (Fire Cavern is the
  known-bad one; sweep the rest).

## Key file touch-points

- `src/world_map_geometry.inl` — WorldYToSegRow (+98304), SegmentCenterToWorld (-98304), comments.
- `src/world_map_state.inl` — fine-grid constants + arrays (s_walkClassFine, s_reachFine), 1024 res.
- `src/world_map_segments.inl` — rasterizer in the wmx-parse path (alongside LoadTerrainGrid).
- `src/world_map_catalog.inl` — reachability filter -> fine flood-fill.
- `src/world_map_planner.inl` — region/segment A* now reads corrected regions (verify, BAT 2).
- `tests/world_map_harness.cpp` + `tests/world_map_terrain_grid.txt` — fine model, real Fire Cavern.

## Artifacts (regenerable from world.fs in-container; scripts were in /home/claude/wm)

- worldmap_catalog_verified.csv — 27 locations: name, x, y, col, row, region, continent, terrain.
- worldmap_atlas.txt — annotated region map + terrain map + corrected location cells.
