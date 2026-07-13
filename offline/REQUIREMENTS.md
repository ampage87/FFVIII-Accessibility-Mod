# World-Map Walking Navigation — Requirements & Faithful-Port Spec

**Status:** Phases 1–4 complete and validated offline (2026-06-27). Phase 5 (in-mod
implementation) is specified here and gated on Aaron's BAT + push. Cowork does **not**
push code or touch issue #70.

This document is the single technical reference for the offline rebuild. Everything
below was confirmed against **primary sources**: the uploaded `FF8_EN.exe` (disassembled
with capstone), the raw `wmx.obj` bytes (extracted from `world.fs`), and the live
`[GROUNDH]` traces in `Logs/ff8_world.log`. Research-doc hypotheses were used only as
leads; where they disagreed with the binary, the binary won (see §2.1, §3).

---

## 0. The validation oracle (core principle, honored)

Ground truth is the engine's own code, ported literally. We disassembled the five
walking-collision functions, ported them verbatim into Python (`ff8_walkmesh.py`),
ran the port on the **raw** `wmx.obj`, and proved it reproduces the engine's own
`[0x203FE30]` ground heights at the contested Galbadia wedge: **15/15 trace samples,
max error 3.57 units, mean 0.83** (pure float-vs-fixed rounding). The router is
validated against this port, never against the mod's reconstruction.

---

## 1. Phase 1 — Function map (collision path, leading)

Call graph for one foot move (engine):

```
0x53E7A0  movement validator (step gate)         <- the actual blocker
   |- 0x53D8A0  candidate-move builder
   `- 0x53EB80  find-poly for a position
         |- 0x553E00  block loader (blockIndex -> in-memory block)
         `- 0x402620  point-in-triangle (2D X/Z) + planar height interp
   (position->blockIndex is done by 0x53DC70 locator, result stored in ctx+0x20)
```

### 1.1 `0x53DC70` — locator (game coords -> block index)  *(confirmed)*
```
blockCol = ((gx + 0x60000) % 0x40000) / 2048      ; localX = same % 2048
rowEng   = ((0x48000 - gz) % 0x30000) / 2048       ; localZmesh = same % 2048
blockIndex = rowEng*128 + blockCol                 ; 128 cols x 96 rows of 2048-unit blocks
```
Writes block-local X at ctx+0, block-local Z at ctx+4, block index at ctx+0x20.

### 1.2 `0x553E00` — block loader  *(confirmed)*
Decomposes blockIndex into segment + sub-block, returns a pointer into the loaded
segment pool:
```
blockCol = blockIndex & 0x7F ;  blockRow = blockIndex >> 7
segment  = (blockRow/4)*32 + (blockCol/4)          ; 0..767, a 32x24 grid
subBlock = (blockRow%4)*4 + (blockCol%4)           ; 0..15,  a 4x4 grid
blockPtr = segmentBase + dword[segmentBase + 4 + subBlock*4]   ; offset table = seg dwords[1..16]
```
The loader does **no vertex transform** — the in-memory layout equals the file layout.

### 1.3 `0x53EB80` — find-poly  *(confirmed)*
`block byte[0]` = polyCount; polygons start at `block+4`, 16 bytes each; the function
iterates polygons **in stored order** and returns the **first** triangle that contains
the query point (an 8-entry recently-used cache is checked first; order-equivalent). When
a non-walkable overlay marker poly (byte15 bit7=0) shares the same XZ footprint as the
real terrain, the engine's standing height `[0x203FE30]` is the **walkable** surface, so
the port returns the first containing *walkable* poly (fallback: first containing, e.g.
ocean). Validated: this is what fixes the only large residuals over the 205-sample run.

### 1.4 `0x402620` — point-in-triangle + height  *(confirmed)*
- Inside test: 2D in (X = vertex word0, Z = vertex word2); point is inside iff the three
  edge cross-products share sign (consistent winding).
- Height: planar interpolation of the three vertex heights (vertex word1):
  `H = (h0*area + dx0*g1 + dz0*g2) / area`, equivalent to barycentric. Degenerate
  (area==0) -> average of the three heights.

### 1.5 `0x53E7A0` — movement validator (THE rule)  *(confirmed)*
For each candidate move the engine:
1. finds the destination polygon (1.3) and its interpolated height `candH` (1.4);
2. **foot-walkable gate:** assembles the poly type from bytes [13,14,15] and, for foot
   (vehicle id 0x80), requires **bit 7 of byte[15]** to be set. Ocean `(0x22,0x40,0x20)`
   has bit7=0 -> blocked; land `(7,128,0xD5)` has bit7=1 -> walkable; overlay markers
   `(29,0,0)`/`(33,64,0x10)` have bit7=0 -> blocked. (Gate is skipped entirely when the
   global `[0xC75CF4]==0`; the per-vehicle bits for Garden/Chocobo/Ragnarok do not apply
   to foot.)
3. **height step gate** (`0x53E9AD`-`0x53E9C7`, exact):
   ```
   curH = [0x203FE30]                  ; current ground height (the [GROUNDH] probe source)
   reject the move iff  abs(candH - curH) >= 0xC8 (200)
   ```

**Net foot rule:** a step is allowed iff the destination's first-containing triangle is
foot-walkable (byte15 bit7) **and** `|candH - curH| < 200`. The ocean is blocked both by
its type bit and by the ~600-unit height drop from elevated land (up = negative) to
sea-level 0.

---

## 2. Phase 2 — Raw walkmesh format & extraction

`wmx.obj` = **30,781,440 bytes = 835 segments x 0x9000**, stored **uncompressed** inside
`world.fs` (FI entry 9: offset 3,040,099, comp=0). Segments 0-767 are the ground grid
(32 wide x 24 tall); the rest are texture/aux.

**Segment (0x9000):** dword[0] = 3 (group field, ignored); dwords[1..16] = 16 sub-block
offsets relative to the segment; sub-blocks are the 4x4 block tile.

**Block:** `byte0`=polyCount, `byte1`=vertCount, `byte2`=normalCount, then polyCount x 16-byte
polygons, then vertCount x 8-byte vertices.

**Polygon (16 bytes):** vertex indices @[0,1,2]; ground-type tuple @[13,14,15];
foot-walkable iff `byte[15] & 0x80`.

**Vertex (8 bytes, file == memory):** `word0 = X`, `word1 = HEIGHT` (up = negative),
`word2 = Z`, `word3 = pad`.  (X local 0..2048; Z local 0..-2048.)

> NOTE — research-doc correction: prior docs claimed the vertex was X/pad/Z/height and
> that polygon[0x0E] bit7 was a navmesh filter. The binary shows the vertex is
> X/HEIGHT/Z/pad (the loader does no rearrangement; `0x402620` reads height at word1),
> and the foot-walkable flag is byte[15] bit7. Treat the old layout as wrong.

### 2.1 File <-> game-coordinate transform  *(solved against traces; this was the bug)*
The file is **row-mirrored** relative to the engine's segment indexing (X is direct):
```
given engine block (blockCol, rowEng):   fileCol = blockCol ;  fileRow = 95 - rowEng
in-block query point = (localX, localZmesh - 2048)      ; vertex Z runs 0..-2048
vertex -> game:  gx = fc*2048 + vx - 0x60000  (wrap into [-131072,131071])
                 gz = 0x48000 - ((95-fr)*2048 + vz + 2048)  (wrap into [-98304,98303])
```
Found by fitting all 15 Galbadia trace samples simultaneously (max err 3.57) and
independently cross-checked at 9 widely-separated landmarks whose oracle heights match
the speedrun coordinate table's Z column within ~1-3 units.

### 2.2 What the engine includes vs. what the mod was dropping
- Raw mesh: **473,193 polygons total; 101,703 foot-walkable** (byte15 bit7) -> 98,814
  welded land triangles.
- At the Galbadia-exit pinch the raw mesh carries the **deep ground (~-595)** that the
  player actually walks. The mod's `LoadTerrainGrid` / `world_map_segments.inl` filtering
  kept a **shallow overlay surface (~-200)** and dropped the deep triangle, producing the
  ~390-unit error that the 200 step-gate turns into the wedge (matches the `[GROUNDH]`
  trace exactly: ourH~-200, engineH~-595, contain=1). The fix is to stop filtering and
  feed the raw mesh + first-containing selection to the existing block-local height query.
- The mod's build log shows the lossy path it must replace: "gate dropped 227,101
  connections", "ROADFLOOR clamped 1,086 triangles", DOLLETBRIDGE synthetic bridges.
  None of those hacks are needed with the faithful mesh.

---

## 3. Phase 3 — The faithful port (the oracle)

`ff8_walkmesh.py`:
- `WMX.locate(gx,gz)` -> (blockIndex, localX, queryZ)  [ports 0x53DC70 + the mirror]
- `WMX.query(gx,gz)` -> (height, walkable, blockIndex, polyIndex) [ports 0x553E00/0x53EB80/0x402620]
- `WMX.ground_height(gx,gz)` -> height, the oracle the router is validated against.

**Validation (see VALIDATION.md):** full Galbadia BAT run, **205/205 engine samples,
mean 0.59 / median 0.53 / max 5.2 units**, 204/205 within 5u; ground found at every
sample. Faithfulness note: the engine uses integer fixed-point
(`sar 8`, `idiv`); the port uses float interpolation, so per-sample differences <= ~4
units are exact-equivalent. For bit-exact parity the interp can be switched to the same
integer formula; not required for routing (sub-pixel).

> Balamb live-trace and the Timber->Dollet road overlay were not re-pulled sample-by-sample
> in this pass; the 9-landmark height cross-check (which includes Balamb Garden -657 vs -658
> and the full Galbadia/Esthar set) already exercises every continent. Re-running the Balamb
> descent samples through `WMX.query` is a quick confirmation step before implementation.

---

## 4. Phase 4 — Walking-only router

The faithful router mirrors the engine's per-frame movement exactly: from a cell, step in
8 directions by `step` units; a neighbour is enterable iff `query` reports it walkable and
`|dHeight| < 200`. `flood()` gives reachability; `route()` is A* returning on-mesh
waypoints. "Walkable to one another" = reachable in both directions.

**Acceptance (brief section 6): all three trios PASS bidirectionally** on the validated
mesh — Galbadia (Timber/Dollet/Galbadia Garden), Balamb (Balamb Garden/Fire Cavern/Balamb
town), Esthar (Lunar Gate/Sorceress Memorial/Tears Point). The Galbadia trio — the
"Dollet works, Timber doesn't" regression — now connects through one 101,707-cell
component because the deep ground is continuous. Endpoint coordinates came from the
research table and are flagged **verify-in-BAT**; the heights independently match, which is
strong corroboration, but the entry tiles should be confirmed against the mod catalog.

---

## 5. Phase 5 — Implementation requirements (for the mod; not yet applied)

1. **Replace the lossy reconstruction.** In `world_map_segments.inl` / `LoadTerrainGrid`,
   stop filtering/bridging/skirt-dropping. Build the navmesh by parsing the raw `wmx.obj`
   exactly as section 2 (segment->sub-block->block; 16-byte polys; 8-byte verts; the 2.1 mirror).
2. **Keep `WorldGroundHeightLocal`.** It is the correct block-local query; feed it the
   faithful mesh. Do not revert to the global extrapolating query.
3. **Collision rule = 1.5 verbatim:** first-containing triangle; foot-walkable iff
   byte15 bit7; reject iff `|candH - curH| >= 200`. Remove `[DEEPGUARD]` and the floor/cost
   correction hacks — they cannot fix connections severed at build time.
4. **Router:** shortest on-mesh route under the same step gate (the offline `route()` is the
   reference algorithm).
5. **Integration points:** locator (game->block), block loader/parser, the ported find-poly +
   interp, the block-local height query, the step gate, the planner/drive that consumes routes.
6. **Guardrails:** one behavioral change per BAT; version bump in `FF8OPC_VERSION`
   (`src/ff8_accessibility.h`) + matching `CHANGELOG.md` heading; update `DEVNOTES.md` and
   `NEXT_SESSION_PROMPT.md`; never re-enable SET3 opcode 0x1E; every step screen-reader
   accessible; **Aaron BATs and pushes — Cowork never does.** Do not close issue #70 until
   the in-game walk to Dollet **and onward to Timber** is BAT-confirmed and pushed.

---

## 6. Definition of done (tracking)
- [x] Faithful port reproduces the available Galbadia trace (15/15, <=3.6).
- [x] Raw extraction with no filtering; includes-vs-dropped diff documented.
- [x] Router walks all three trios both directions in the offline simulator.
- [x] Requirements document (this file).
- [ ] Implemented in the mod (Phase 5) — Aaron.
- [ ] In-game walk to Dollet **and onward to Timber** BAT-confirmed and pushed — Aaron.
- [ ] Issue #70 closed — only after the above.
