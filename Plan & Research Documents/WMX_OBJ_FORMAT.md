# wmx.obj World-Map Mesh Format — Validated Specification

Status: **FULLY REVERSE-ENGINEERED AND VALIDATED** (0 errors across 473,193
polygons). Derived three independent ways and cross-checked: (a) FF8_EN.exe
disassembly (loader 0x553E00, triangle-finder 0x53EB80, locator 0x53DC70),
(b) raw-byte structural analysis of `world.zip:wmx.obj`, (c) FF8 Modding Wiki /
wmx2obj community converter behaviour. This document is the authoritative
reference for any world-map navmesh / on-foot / auto-drive work (issue #70).
Search this file before interpreting any wmx.obj field.

File analysed: `wmx.obj`, 30,781,440 bytes = 835 × 0x9000.

---

## 1. Top-level file structure

- File = **835 segments**, each exactly **0x9000 (36,864) bytes**.
- **Base world map = file segments 0–767**, laid out in a **32-wide,
  row-major grid** (segment column = `S % 32`, segment row = `S // 32`,
  giving 24 segment-rows). Segments **768–834 are story/event variants —
  skip them** for the base map.
- Each segment contains **16 blocks** arranged 4×4.

## 2. Segment header

Starting at the segment's base byte:
- 4-byte lead value (u32).
- Then **16 × u32 block offsets** (one per block), each relative to the
  segment base. First block offset is 0x44. The 17th implicit boundary for
  block 15 is the segment end region.

```
segment base + 0      : u32  lead
segment base + 4 + 4*i : u32  blockOffset[i]   for i in 0..15
```

## 3. Block header (4 bytes) + layout

Each block begins with a **4-byte header** (NOT 16 — a 16-byte guess only
coincidentally fits the uniform ocean blocks; the engine loader confirms 4):

```
block+0 : u8  polyCount   (P)
block+1 : u8  vertCount   (V)
block+2 : u8  normalCount (N)
block+3 : u8  (flags/pad)
```

Layout within a block: **polys** (at block+4), then **normals**
(12 bytes each — validated by 7,874 blocks), then **vertices**.

Robust vertex base (sidesteps needing N for blocks 0..14):
```
vert_base = segBase + blockOffset[bi+1] - V*8     for bi in 0..14
vert_base = block+4 + P*16 + N*12                 for bi == 15
poly_base = block+4
```

## 4. Polygon (16 bytes)

```
poly+0  : u8  vertex index 0   (into this block's vertex array)
poly+1  : u8  vertex index 1
poly+2  : u8  vertex index 2
poly+3  : u8  (normal index / pad)
poly+4..+11 : texture UVs + pad
poly+13 : u8  ground-type/region byte 0   (engine reads [poly+0x0D])
poly+14 : u8  ground-type/region byte 1   (engine reads [poly+0x0E])
poly+15 : u8  ground-type/region byte 2   (engine reads [poly+0x0F])
```

The 24-bit tuple at bytes **[13,14,15]** is the ground-type / region bitmask
(decoded by 0x53E730 into the engine ground-type code). 222 distinct tuples.

- **OCEAN (water surface) tuple = (0x22, 0x40, 0x20)** — dominant, 284,710
  polys. Use this as the ocean discriminator for water/land classification.
  Do **not** rely on height to separate water from land (their ranges overlap
  near the coast).
- Next-most-common land tuples include (0x1d,0x00,0x00), (0x21,0x40,0x30),
  (0x07,0x80,0xd5), etc.

NOTE: The `poly[0x0E]` bit-7 "walkability" flag must **NOT** be used as a
navmesh filter — it cuts the real walkable corridor (gentle mountain ramps +
railroad triangles). Ocean-exclusion only. (Confirmed prior session.)

## 5. Vertex (8 bytes, 4× s16)

```
word0 : s16  X       (block-local)
word1 : s16  pad     (always 0)
word2 : s16  Z       (block-local)
word3 : s16  height  (Y)
```

- Block-local X ∈ [−2048, 0], Z ∈ [0, 2048] for a nominal block; aggregate
  range is ±4096 because seam/overhang triangles reach into neighbours.
- **Height sign: UP = NEGATIVE Y (PSX convention).** Sea level = 0
  (228,552 verts). Land surface runs from ~0 at the coast down to ≈ −3837 at
  peaks (more negative = higher). Empirical centroid-height percentiles:
  ocean [2,25,50,75,98] = [−1236, 0, 0, 0, 0]; land = [−3837, −2900, −1589,
  −42, 0].
- The **−4096 height cluster = seam skirts** (vertical curtains that hide the
  gaps between blocks). Filter these out: drop any triangle whose minimum
  vertex height ≤ −4000.

## 6. Seams — blocks do NOT share boundary vertices

Adjacent blocks are stitched with **skirts**, not shared vertices. Exact
edge-matching yields only ~2% cross-block shared edges. Consequences for
navmesh construction:
- Cross-block adjacency must come from **proximity / fine-raster with gap
  closing**, never exact vertex/edge identity.
- Moderate **overhang triangles** (verts spanning one block boundary, all at
  land height) are legitimate surface that bridges seams — keep them. Only
  exclude (a) skirt triangles (min height ≤ −4000) and (b) extreme backdrop
  triangles (extent > ~3000–4500u). Excluding all overhangs fragments the
  continents; keeping huge ones falsely bridges across ocean.

## 7. Placement (file block → world XZ, my-frame)

```
blockCol = (S % 32) * 4 + (bi % 4)        # 0..127
blockRow = (S // 32) * 4 + (bi // 4)      # 0..95
worldX   = blockCol * 2048 + (vx + 2048)  # vx∈[-2048,0] -> [0,2048] in-cell
worldZ   = blockRow * 2048 + vz           # vz∈[0,2048]
```
World torus = **128 block-cols × 96 block-rows** = 262,144 × 196,608 units.

The rendered land map is **east-heavy** = FF8's Esthar continent in the east —
a quick visual correctness check that placement is right.

## 8. Engine locator (game coord → block index), 0x53DC70

Input position struct: {s32 X@0, s32 Y@4, s32 Z@8} (low words used).

```
blockCol = ((X + 0x60000) mod 0x40000) / 2048     # +0x60000 = +64-col bias, wraps mod 128
blockRow = ((0x48000 - Z) mod 0x30000) / 2048     # Z is NEGATED, +48-row bias, mod 96
linearBlockIndex = blockRow * 128 + blockCol
```

This maps any in-game world coordinate (same space as `GetWorldMapPosition`
and the `s_locations[]` catalog in `world_map_catalog.inl`) onto the block
grid.

## 9. Registration — file-order frame vs engine locator frame

The file-order placement (Section 7) is **Z-mirrored** relative to the engine
locator frame (because the locator negates Z via `0x48000 - Z`). To look up an
engine-frame landmark in the file-order mesh, transform engine→mine:

```
blockCol_mine = (blockCol_eng - 1) mod 128
blockRow_mine = (95 - blockRow_eng) mod 96
```
Fine-resolution equivalent (validated by search, 14/14 landmarks on land):
```
worldX_mine = (worldX_eng + DX) mod 262144      # DX ≈ -3072 (block-search) ; -2048 from pure Z-flip derivation
worldZ_mine = (196608 - worldZ_eng + DZ) mod 196608   # DZ ≈ -1024
```
This transform put **15/15** catalog landmarks onto land cells (identity put
only 9/15 — the partial hits that originally looked like a base-rate
coincidence were the tell of the Z-flip).

## 10. Validation results

- **Format:** 0 out-of-range vertex indices across **473,193** polygons.
  Normal entry size = 12 bytes (7,874 blocks agree). Sea level = 0
  (228,552 verts).
- **Placement/registration:** 15/15 catalog landmarks register onto land
  after the Z-flip; east-heavy landmass matches FF8 Esthar.
- **Connectivity (the issue-#70 gate):** ocean separation **PASSES** under
  three independent methods —
  - vertex-proximity union-find (R=1200, no height gate): Galbadia continent
    (Squall + Dollet + Timber + Galbadia Garden + Deling + Winhill) = ONE
    component (size 10,809); Balamb (Town + Garden + Fire Cavern) = a SEPARATE
    component (the eastern Balamb/FH/Esthar mass), disjoint from Galbadia.
  - ocean-priority fine raster (512u) + land dilation + flood-fill, HTH=∞:
    Galbadia all-connected incl. Dollet (largest comp 36,026); Balamb
    isolated. **PASS.**
  - landmass occupancy render: coherent FF8-shaped world, east-heavy.
- **Floor-step caveat:** naive finite height-step gating between 512u cells
  (or between >256u-quantized triangle edges, or >1200u proximity links)
  **fragments the continent** — a coarse-reconstruction artifact (centroid
  steps over rolling terrain + dilation-copied heights + seam quantization),
  NOT evidence against reachability. Ocean separation is the reliable signal
  and it passes. The precise cliff-gated routing (bottleneck **176u to
  Dollet**, 115u to Timber/Galbadia Garden/Deling, Balamb unreachable) was
  validated the prior session with the **z-aware axis bridging + z-gated
  proximity** recipe (ztol≈100, floor-step ≤300u) — that is the construction
  to port to C++, and this format spec is the verified foundation under it.

## 11. Reference parser (Python)

```python
import struct
SEG=0x9000; NSZ=12
d=open('wmx.obj','rb').read()
def seg_offs(s):
    base=s*SEG
    return base,[struct.unpack_from('<I',d,base+4+i*4)[0] for i in range(16)]
def block_verts_polys(base,o,bi):
    off=o[bi]; p=base+off; P=d[p]; V=d[p+1]; N=d[p+2]
    vb=(base+o[bi+1]-V*8) if bi<15 else (p+4+P*16+N*NSZ)
    pb=p+4
    polys=[(d[pb+i*16],d[pb+i*16+1],d[pb+i*16+2],
            d[pb+i*16+13],d[pb+i*16+14],d[pb+i*16+15]) for i in range(P)]
    verts=[struct.unpack_from('<4h',d,vb+i*8) for i in range(V)]
    return P,V,polys,verts          # vert = (X, pad, Z, height)
def grid_of(S,bi):                  # (blockCol, blockRow), file/my-frame
    return (S%32)*4+(bi%4),(S//32)*4+(bi//4)
def world_xz(bc,br,v):
    x,_,z,y=v
    return bc*2048+(x+2048), br*2048+z
def game_to_block(X,Z):             # engine locator 0x53DC70
    return ((X+0x60000)%0x40000)//2048, ((0x48000-Z)%0x30000)//2048
OCEAN=(0x22,0x40,0x20)              # water-surface ground-type tuple
```

## 12. World-map ON-FOOT walkability — the ENGINE's actual rule (decoded)

**This answers the standing #69/.104 question ("how does the engine build its
per-edge walkmesh / what makes the Dollet false-coast impassable"). It is NOT a
data flag — it is a live interpolated-height-step gate.**

### The rule
The engine constrains world-map movement with a **200-unit (0xC8) ground-height
step gate**. For each candidate neighbour position it samples, it:
1. finds the triangle under the candidate (X,Z),
2. **barycentrically interpolates the ground height (Y)** from that triangle's 3
   vertices at the exact (X,Z),
3. rejects the step iff `|candidateHeight - currentHeight| >= 200`.

So the "false coast" is simply a **>=200u height ledge**: byte-identical walkable
ground type, but the height jump across it trips the gate. The legitimate
road-pass survives because its consecutive steps stay < 200u.

### Where it lives (FF8_EN.exe)
- **0x53E7A0** — world-map movement/collision validator (9 callers). Loops over a
  set of candidate neighbour positions (built by 0x53D8A0, stride 0x2C), and for
  each runs the height-step gate. The decisive instructions:
  ```
  mov ecx,[ebp-0x18]      ; candidate interpolated ground height (out of 0x53EB80/0x402620)
  mov edx,[0x203fe30]     ; current reference ground height
  sub eax,edx ; abs       ; |dH|
  cmp eax,0xC8            ; 200
  jge <reject>            ; step >= 200 -> not walkable
  ```
- **0x53EB80** — "find polygon under (X,Z)": block-getter 0x553E00 -> point-in-tri
  search 0x402620. Writes the interpolated height to its out-param.
- **0x402620** — point-in-triangle (2D containment on vertex word[+0]=X, word[+4]=Z)
  + 3D plane interpolation (reads word[+0],word[+2],word[+4] per vertex; fixed-
  point cross/dot/normalize via 0x45DBC0/0x45DBF0/0x45E670/0x45E450) -> returns
  the interpolated ground height.
- **0x53E730** and the inlined copy at the top of 0x53E7A0 — a *separate*
  per-VEHICLE ground-type passability check (car/foot-alt->byte[15] bit2,
  garden->byte[15] bit1, chocobo->byte[15] bit0, ragnarok->byte[14] bit7).
  **FOOT (mode 0x80) falls through (default), so on-foot walkability is gated
  ONLY by the 200u height step, not by any ground-type bit.** This is why the
  false-coast is "byte-indistinguishable" — for foot, the bytes are irrelevant.

### In-memory vs file vertex layout
The loader (plytopd) transposes the vertex. FILE layout (this doc, sec 5) =
`X@0, pad@2, Z@4, height@6`. IN-MEMORY layout used by 0x402620 = `X@0, Y@2, Z@4`.
The mod parses the FILE, so it uses the file layout (height@6) — fine; just don't
confuse the two when reading the disassembly.

### Implication for the mod (#69 / the .81 false-coast hardcode)
The prior conclusion "geometry cannot separate the legit route from the false
coast" was a **height-RESOLUTION artifact**, not a true impossibility:
- The mod gated at **400** (`WM_CLIMB_STEP`) / 300 (navmesh `NM_LINK_ZSTEP`) and
  read **coarse** heights (1024u fine-grid cell averages, or triangle centroids).
  Coarse averaging blurs the >=200 ledge below the gate, and the wrong threshold
  let >=200 ledges through -> the false coast looked crossable.
- The engine uses **exactly 200** on **exact barycentric-interpolated** heights.

**Durable fix:** replicate the engine: (1) interpolate ground height
barycentrically from the exact triangle vertices (file layout: X@0,Z@4,height@6)
at fine sample points along the path, and (2) gate adjacent samples at **|dH| >=
200 == not walkable**. With exact heights + 200, the false-coast ledge separates
from the road-pass on its own, which should retire the .81 hardcode and close
#69. Validate in-game by BAT (the mod walks the Timber->Dollet pass without the
hardcode and still refuses the false coast). NOTE: this is the on-foot rule;
vehicles additionally consult the per-vehicle ground-type bits above.

---

*Authored after the wmx.obj RE session (issue #70, Path 1). Companion files:
`world_map_catalog.inl` (landmark game coords), `world_map_segments.inl`
(LoadTerrainGrid / RasterizeTriFine), `world_map_navmesh.inl`,
`world_map_planner.inl`.*
