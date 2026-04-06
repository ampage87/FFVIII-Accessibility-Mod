# Deep Research Request: FF8 wmx.obj Binary Format — Polygon Terrain Byte Location

## Context
I'm building an accessibility mod for Final Fantasy VIII (Steam 2013, PC). I need to parse the world map mesh file (`wmx.obj`) to build a land/ocean terrain grid for BFS reachability filtering.

The file is extracted from `world.fs` via the standard FF8 fi/fl/fs archive system. It's entry index 9 in `world.fl` (`C:\ff8\Data\eng\World\dat\wmx.obj`). Uncompressed size: **30,781,440 bytes** (835 segments × 36,864 bytes per segment).

## What I Know

### Segment header (confirmed by hex dump)
Each segment is exactly **0x9000 (36,864) bytes**. The first 68 bytes are a header:
- **Bytes 0–3**: uint32 — segment group ID (varies per segment: 0x03, 0xFF, etc.)
- **Bytes 4–67**: 16 × uint32 — offsets to 16 mesh sub-blocks within the segment

Confirmed offsets from hex dump of segment 0:
```
Block  0: offset 0x0044 (68)
Block  1: offset 0x031C (796)
Block  2: offset 0x05F4 (1524)
Block  3: offset 0x08CC (2252)
...
Block 15: offset 0x2AEC (10988)
```
Each block is **728 bytes** (stride between consecutive offsets). 16 blocks × 728 = 11,648 bytes of block data, starting at byte 68 and ending at byte 11,716.

The remaining **25,148 bytes** (bytes 11,716 through 36,863) contain unknown data.

### What the community says
- Each polygon/triangle is **16 bytes** with a **ground type byte at offset 0x0D** within the polygon structure
- Ground types: 0=Galbadia Forest, 6=Plains, 8=Desert, 9=Snow, 29=City/Town, 32=Shallow Ocean, 33=Light Ocean, 34=Dark Ocean
- Source: ff8-speedruns/ff8-memory repo (wmTerrain.md)

### What my diagnostic found
When I scan byte 0x0D at 16-byte stride across the ENTIRE 36,864-byte segment, I get:
- Value distribution is **identical across segments 0, 200, and 400**: `0:1876 5:1 17:1 25:8 28:1 34:256 40:1 248:40 250:40 252:40 254:40`
- The identical distributions prove I'm reading structural/vertex data, NOT polygon terrain
- Bytes 12000–12063 are all zeros for all tested segments
- Raw byte counts for values 32 and 34 are also identical across segments (val32=1296, val34=513)

This means the polygon data with terrain bytes is NOT at the beginning of each segment, and the 16-byte stride from byte 0 is wrong.

## What I Need

1. **The exact internal structure of each 36,864-byte wmx.obj segment** — specifically:
   - What is the format of each 728-byte mesh sub-block? (vertex count, vertex format, normal data, polygon data — where does each section begin?)
   - Are the polygons (with terrain type at byte 0x0D) INSIDE the 728-byte blocks, or in the 25,148-byte region AFTER the blocks?
   - What is the structure of the 25,148 bytes after the 16 blocks (bytes 11716–36863)?

2. **The exact polygon/triangle format** — specifically:
   - Is it truly 16 bytes per polygon, or 24 bytes, or variable?
   - Is the ground type truly at byte 0x0D within each polygon?
   - How many polygons per block/segment?

3. **Any per-block header** that indicates vertex count or polygon count, so I can skip vertex data and read only polygon terrain bytes.

## Target Platform
FF8 PC Steam 2013 (App ID 39150). The wmx.obj format should be identical to the PS1 original in structure, just with PC-specific byte ordering (little-endian).

## Sources to Check
- Qhimm.com wiki: FF8/WorldMap/Wmx
- ff8-speedruns/ff8-memory GitHub repo (wmTerrain.md, wmx.md)
- Qhimm forum threads on FF8 world map modding
- Any FF8 world map editor source code (e.g., Deling, Cactilio, or similar tools)
- The FF8 OpenVIII C# project (likely has wmx.obj parsing code)
- The FFNx source code (may reference wmx.obj structures)
- Any Hyne save editor code that deals with world map data

## Ideal Output
A complete C struct layout for the 36,864-byte segment showing exactly where polygon terrain data lives, with byte offsets I can use to extract terrain type values programmatically. If the polygon section has a variable offset per block, I need to know how to compute that offset from the block header.
