# World Map Terrain & Locomotion Reference Data
## Source: github.com/ff8-speedruns/ff8-memory (wmTerrain.md + locomotion.md)

> **CORRECTION (#67 offline wmx analysis):** Two entries below are WRONG, per the byte-exact
> parse in `World Map Reachability Rework - offline wmx analysis findings.md` (authoritative):
> (1) **Type 29 is MOUNTAINS, not "City/Town/Enterable Area"** — it is the steepest (avg 57 deg)
> and most common (54,297 polys) land type and forms every continent's interior ranges; it is
> IMPASSABLE on foot. The old "Do NOT filter by terrain-29" advice in Phase 1 below is therefore
> wrong for on-foot reachability (mountains DO block). (2) The on-foot land/ocean BFS must run on a
> fine rasterized grid (continuous flood-fill), not the 32x24 segment majority grid, which only
> separated continents by accident of a coordinate bug. See the findings doc.

## Terrain Types (wmx.obj polygon byte 13 / offset 0x0D)

| Value | Description | Category |
|-------|-------------|----------|
| 0 | Galbadia Forest | LAND |
| 1 | Trabia Forest | LAND |
| 2 | Esthar Forest | LAND |
| 3 | Centra Forest | LAND |
| 4 | Balamb Forest | LAND |
| 5 | Esthar Forest | LAND |
| 6 | Plains | LAND |
| 7 | Esthar Plains | LAND |
| 8 | Desert | LAND |
| 9 | Snow | LAND |
| 10 | Balamb Beach | LAND |
| 12 | Esthar Bridge | LAND |
| 14 | Clifftop | LAND |
| 15 | Cliffside - Grass | LAND |
| 16 | Galbadia Dirt | LAND |
| 27 | Railroad | LAND |
| 28 | Road | LAND |
| 29 | **MOUNTAINS** (mislabeled "City/Town" in source; impassable on foot) | LAND-but-impassable |
| 32 | Ocean - Shallow | OCEAN |
| 33 | Ocean - Light | OCEAN |
| 34 | Ocean - Dark | OCEAN |

Note: Values 11, 13, 17-26, 30-31, 35+ are undocumented. Treat unknown values conservatively (assume LAND unless confirmed otherwise).

## Locomotion Values (byte at 0x02040A5E)

| Value | Description | Terrain Access |
|-------|-------------|---------------|
| 0 | Squall - On Feet | Land only |
| 6 | Selphie - On Feet | Land only |
| 10 | Train? (problematic) | Rail only? |
| 31 | Chocobo? (problematic) | Land + shallow? |
| 32 | Invisible car (Missile Base) | Land/Road |
| 33 | Invisible car (Balamb Garden Car) | Land/Road |
| 34 | Invisible car (Sky Blue Van) | Land/Road |
| 35 | Invisible car (Classic Car) | Land/Road |
| 36-39 | Various invisible cars | Land/Road |
| 40 | Invisible car (Esthar Car) | Land/Road |
| 48 | Balamb Garden | Land + shallow ocean |
| 49 | *CRASH* | DO NOT USE |
| 50 | Ragnarok | ALL (flying) |

Note: The locomotion byte at 0x02040A5E cycles through animation sub-states while walking (observed 0→3→7→10→14→...). The base vehicle values above may be the STARTING value before animation cycling begins. Need diagnostic to confirm which value indicates stable vehicle type.

## wmx.obj Structure

- File: `world.fs` → `wmx.obj`
- 835 segments × 36,864 (0x9000) bytes each
- Segments 1-768: playable map in 32×24 grid
- Each segment: 16 blocks (4×4 sub-grid), each 2048×2048 world units
- Total: 128×96 blocks = 12,288 cells
- Each polygon: 16 bytes, ground type at offset 0x0D

## Grid coordinate conversion

```
blockX = ((int32_t)playerX / 2048) mod 128   // handle negative coords + torus wrapping
blockY = ((int32_t)playerY / 2048) mod 96
```

## Implementation Plan: Terrain-Based Location Filtering

### Phase 1: Build walkability grid from wmx.obj
- Parse wmx.obj at mod startup (or first world map entry)
- For each of the 768 segments (32×24), for each of the 16 blocks (4×4):
  - Read all triangles within that block
  - Determine dominant terrain type (most common groundType value)
  - Classify as OCEAN (32-34) or LAND (everything else)
- Result: 128×96 byte array, 1 = LAND, 0 = OCEAN
- **IMPORTANT** (from session 32 analysis): Many segments use default template terrain
  (Galbadia Forest type 0 everywhere). These ARE walkable land. Only types 32-34
  (ocean) should be treated as impassable on foot. Do NOT filter by terrain-29.

### Alternative: Precomputed bitmap (simpler, RECOMMENDED)
- Generate the 128×96 land/ocean bitmap offline from wmx.obj
- Embed as static const array in world_map.cpp (only 12,288 bytes)
- Avoids runtime wmx.obj parsing entirely
- Can be generated from the analysis scripts used in session 32
- Downside: doesn't adapt to story-variant segments (mobile Garden, etc.)

### Phase 2: BFS reachability at catalog build time
- On world map entry, determine player's grid cell
- BFS flood-fill from player's cell across LAND cells (4-connected or 8-connected)
- Mark all reachable cells
- For each catalog location, check if its grid cell is reachable
- Include only reachable locations in the sorted catalog

### Phase 3: Vehicle-aware terrain access
- Read locomotion byte at 0x02040A5E
- Determine vehicle category: ON_FOOT, CAR, CHOCOBO, GARDEN, RAGNAROK
- Adjust which terrain types are "walkable" for BFS:
  - ON_FOOT/CAR: land only (values 0-29)
  - CHOCOBO: land + shallow ocean (values 0-29, 32)
  - GARDEN: land + all ocean (values 0-34)
  - RAGNAROK: everything (skip BFS, show all locations)

### Phase 4: Story variant handling (future)
- World map version variable controls which segments are active
- Mobile Garden, hidden Esthar, disc 4 changes
- Could swap segment overrides in the grid based on game_moment/version vars
