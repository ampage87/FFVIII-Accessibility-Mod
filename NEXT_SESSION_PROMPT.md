# Next Session Prompt — NAVMESH VALIDATED (.104) → START THE DISASSEMBLY TRACK (engine walkmesh)

**Current local version: v0.18.3.104 (LOCAL diagnostic, NOT pushed). Main branch: v0.18.3.95 / commit c591803b (verified 2026-06-24; re-check with `github:list_commits`).**

> The navmesh is ported and BAT-validated in-game (`.104` matched the offline prototype to the digit). The Dollet drive still wedges, and we pinned why: it's a **false-coast engine wall ~4 cells WEST of the `.81` box** — an unmodeled engine collision the planner's grid (and the navmesh) can't see. So the next move is the **disassembly track**: recover how the FF8 engine builds its per-edge walkmesh, so the planner can know about walls like this one. That retires the `.81` hardcode map-wide and is the durable fix. **This requires a session with code-exec / `view` / `project_knowledge_search` tools** — the session that wrote this prompt had only GitHub + Windows-filesystem tools and could not read the disassembly. **Aaron will re-upload `FF8_EN.exe` and the `world.zip` (world.fi/.fl/.fs)** at the start of the new session. Start at "⇒ DISASSEMBLY TRACK" below.

---

## WHO / SETUP (standing context)

I'm Aaron: blind solo dev and sole tester, NVDA screen reader. Mod = `dinput8.dll` injection for FF8 Steam 2013 (App ID 39150, FF8_EN.exe) + FFNx v1.23.x. Local root `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`. GitHub `ampage87/FFVIII-Accessibility-Mod`; **main = v0.18.3.95, commit c591803b**.

RULES: You NEVER push (I run `Utilities/push_to_github.ps1`); you MAY create/update/comment GitHub issues. Everything must be blind-accessible — NO "press a key when X appears on screen"; automated, log/diagnostic-based only. F11 screenshots are YOUR eyes: `glReadPixels` in SwapBuffers hook → `Logs/screenshots/f11_*.png`; read with `filesystem:read_media_file`. Use `filesystem:`-prefixed tools for ALL Windows/OneDrive paths (bare container tools, when present, operate on the Linux sandbox, NOT OneDrive). Source files are CRLF — `filesystem:edit_file` multi-line anchors match fine (the tool normalizes line endings); `dryRun:true` first on risky anchors. `filesystem:edit_file` CORRUPTS on a literal `$` in the replacement — use `write_file`. OneDrive throws transient EPERM rename errors — retry once. The `FF8OPC_VERSION` macro line in `src/ff8_accessibility.h` is enormous: `edit_file` reports "Tool result too large" but the edit SUCCEEDS (verify via `read_text_file head=30`). Version lives in ONE place (that macro) and must match the top `## vX.Y.Z` in `CHANGELOG.md`. One change per BAT; pair every version bump with a CHANGELOG entry and update `DEVNOTES.md` (hard cap 10,240 bytes) + this file.

Every Claude response starts with `## Claude Says`.

---

## THE MISSION

Replace the world-map on-foot auto-navigation so it reaches ANY reachable destination reliably, with NO per-location hardcodes. **Acceptance test:** on foot from the Galbadia save, navigate to Dollet, Galbadia Garden, AND Timber — all three, zero per-location patches. The navmesh (validated) handles connectivity + real cliffs; the disassembly (this track) handles the false coast; the #68 executor (open) handles steering.

---

## ⇒ DISASSEMBLY TRACK — START HERE (recover the engine's per-edge walkmesh)

**Why:** the Dollet false coast is impassable in-game by ENGINE COLLISION, not by anything in the wmx mesh data (proven byte-level — see THE FALSE-COAST FINDING below). The `.81` box is a hand-placed patch approximating it, and this session proved the box is even mispositioned. The grid-patch approach is a fragile, leaking chain. The durable fix is to find how the engine decides a foot move is blocked, recover the rule or data structure behind it, and replicate it OFFLINE so the planner routes correctly everywhere. Target the **BUILD RULE / static source data** (replicable offline), NOT another runtime RAM read — the RAM-read descriptor model was already disproven.

**Tools needed (this is why it's deferred to a fresh session):** a code-exec / bash sandbox for `objdump`/`capstone` on `FF8_EN.exe` and for parsing `world.fs`, AND/OR `view` + `project_knowledge_search` to read the in-project disassembly. Confirm these are present before starting; if only GitHub + Windows-filesystem tools are available again, this track can't run — say so and pick the #68 executor or routing-swap instead.

**In-project disassembly resources (read-only):**
- `/mnt/project/FF8_EN_functions.txt` — 8,390 functions (names + addresses)
- `/mnt/project/FF8_EN_callxrefs.txt`, `FF8_EN_imports.txt`, `FF8_EN_exports.txt`, `FF8_EN_sections.txt`
- `/mnt/project/FF8_EN_strings_condensed.txt`, `/mnt/project/FF8_EN_disasm_lookup_guide.txt`
- On-disk full `.asm` under `Game Files/disassembly/` (~98 MB, 2.76M instructions) + 8 project-knowledge disasm files (`project_knowledge_search`)
- Re-uploaded by Aaron: `FF8_EN.exe` (objdump/capstone) and `world.zip` = `world.fi/.fl/.fs` (offline structure parsing). Ask him to drop them in `/mnt/user-data/uploads/`.

**Concrete starting plan (two known anchors — use both):**
1. **Disassemble around `0x542F17`** — this is the instruction that writes the descBase pointer `[0x020402DC]` (descBase observed at runtime = `0x01E9FDCC`). Our earlier descriptor/adjacency-stride interpretation of that structure was WRONG (the RAM probe returned garbage adjPtrs), but the function that BUILDS it reveals the true layout and how it's derived. Read that enclosing function: what does it write into the `0x01E9FDCC` region, from what source (wmx tris? a separate world.fs section?), with what stride/fields? This likely IS the engine walkmesh build — just not the layout we guessed.
2. **Find writers/readers of foot-X `0x0203EE80`** (foot-Y adjacent; runtime values this session: foot-X=`0xFFFF92BC`, foot-Y=`0xFFFF87FB`) — the world-map on-foot movement integrator reads/writes these each frame, and the collision gate that produces the false-coast wall is in or next to it. Identify the data structure the move-accept/deny check consults.
3. **Cross-reference (1) and (2):** does the foot-collision check read the `0x01E9FDCC`/descBase structure from (1)? If YES → that's the walkmesh, and (1) tells us how it's built → replicate the build offline from wmx/world.fs and feed real walkability into the planner (kills `.81` map-wide). If NO → the boundary lives in a separate `world.fs` structure; find which FL index / section and parse it offline.

**Hedge (only if the disasm stalls):** a learned passability map from real play — the mod already logs every player triangle change and detects drive wedges, so blocked edges can be accumulated from play. Coverage-limited for a solo blind tester; fallback, not primary.

---

## BAT .104 RESULT (navmesh validated in-game — DONE)

Built clean (Version 0.18.3.104, "Build successful," no compile errors). `Logs/ff8_world.log` `[NAVMESH]` lines matched the offline prototype AND the host-compiled `tests/test_navmesh.cpp` to the digit:
- `built 157416 triangles, 253 components, largest=74308`
- flood from the Galbadia save coord → `tri#64590 -> 35527 reachable (gate=400)`
- Dollet / Timber / Galbadia Garden / Deling City (and Tomb, D-District, Missile Base, Winhill, Alien Ship 3/4) REACHABLE; everything across the ocean (Balamb, Esthar, Trabia, Centra, islands, Fire Cavern on the Balamb landmass) correctly NOT reachable
- `A* ref->Dollet: len=30259 tris=126 (path found)`

The in-game wmx parse and navmesh build are sound. `.104` is LOCAL, not pushed.

---

## THE FALSE-COAST FINDING (the Dollet wedge, pinned this session)

The drive from the Galbadia save wedges at game **(-27972, -30725) = fine(col 100, row 65)**, ~15 km from Dollet, pressing `U` and never advancing past path cell 0. Reading the `[ROUTEMAP]`/`[YAWDRIVE]` trace:
- The `.81` blocked band (the false coast in the grid) is **cols 106–111** (`world X[-24576..-17408] Y[-37888..-27648]` = cols 104–111), sitting EAST of the wedge.
- The player is **engine-walled at col 100**, where the planner's grid says walkable AND draws the route cell directly north of him (col 100, row 64) as on-path. The movement asymmetry is the tell: pressing `U` he slides E–W ±30–60u along an invisible wall and never moves north, but the DOWN reverse-burst moves him ~190u/tick south freely. Free south, walled north, sliding along an E–W boundary = a real engine collision the grid doesn't model — the false-coast signature.
- Conclusion: the real coast wall extends WEST to ~col 100, and the `.81` box (cols 104–111) is **MISPOSITIONED** (~4 cells too far east) — it doesn't cover the wall the player actually hits. The #68 executor (fixed per-region yaw 3772, 8-way) makes it unrecoverable: `U` always walks NNW straight into the E–W wall. Root = unmodeled wall; #68 amplifies.
- Neither the `.81` patch as drawn NOR the navmesh swap fixes this: the navmesh is blind to the false coast too (Finding 4). Only the disassembly (real walkmesh) does. F11 `f11_202025_863.png` was captured at the jam (unread — would confirm shoreline vs cliff).

---

## PORT STATUS — what landed in .104 (NON-INVASIVE; planner/executor UNTOUCHED)

- **`src/world_map_navmesh.inl`** — host-compilable (no Win32/SEH/absolute-memory; std::vector/std::sort only), whole file `#if NAVMESH_DIAG`. API: `Navmesh_Reset / AddTriangle / Build / TriangleCount / ComponentStats / FindTriangleGame / FloodFrom / IsReachable / AStar / LogConnectivity`. Const `NM_CLIMB_STEP = 400`. Build = vertex dedup `(x,y,z)` → exact shared-edge adjacency → axis-aligned T-junction bridging → CSR.
- **`world_map_segments.inl` `LoadTerrainGrid`** (gated): `Navmesh_Reset()` before the seg loop; `Navmesh_AddTriangle(…)` from inside the existing `RasterizeTriFine` block (same verts + `i0/i1/i2<vertCount` guard); `Navmesh_Build()` + `[NAVMESH] built …` after.
- **`world_map.cpp`**: `<vector>`; `#include "world_map_navmesh.inl"` (after geometry.inl, before segments.inl); `Initialize()` calls `Navmesh_LogConnectivity(s_locations, LOCATION_COUNT, -29270, -24056, NM_CLIMB_STEP)`.
- **`world_map_state.inl`**: `#define NAVMESH_DIAG 1`.
- Container test `tests/test_navmesh.cpp` lives only in the prior sandbox (not the game build); the validated `.inl` is the source of truth.

## ROUTING SWAP — deferred (separate later BAT, after the disassembly informs walkability)

When ready: route on the navmesh (game coords → `Navmesh_FindTriangleGame` → `Navmesh_AStar(start,goal,400)` → triangle path → centroid waypoints the #68 yaw executor consumes); REMOVE the file-level `#if NAVMESH_DIAG` guard (build load-bearing); RETIRE the coarse fine-grid routing + `.85` road override; KEEP catalog/trigger zones/region map/arrival detection + the #68 executor. The false-coast exclusion comes from the disassembly walkmesh (preferred) or, interim, a correctly-placed box. Validate connectivity in-log; keep the old planner until the navmesh drive ARRIVES.

---

## OFFLINE PROTOTYPE RESULTS (validated against the real world.fs — reference)

Parsed wmx.obj (FL index 9, RAW; `uncompSize=30,781,440 = 835×36864`). Matches live-log sanity EXACTLY: **473,193 total / 315,777 ocean / 157,416 navigable**.

**FINDING 1 — naive adjacency fragments; T-JUNCTION BRIDGING is required.** Exact `(x,y,z)` vertex dedup + shared-edge adjacency → **642 components** (Galbadia islanded). Cause is T-junctions (a triangle spans two sub-edges its neighbour splits — collinear geometry, no shared vertex pair), not z-noise. FIX (validated, ported): after the exact pass, bridge any single-use axis-aligned boundary edge that overlaps a collinear boundary edge (bucket vertical edges by constant-x, horizontal by constant-y; strict interval overlap = neighbour). Keep `(x,y,z)` welding (cliffs stay distinct). ~17,380 bridges → **642 → 253 components**.

**FINDING 2 — with bridging, the Galbadian continent connects every destination, zero hardcodes.** Flood from the Galbadia save (mesh (101802,74248), engine (−29270,−24056)) → one **35,591-tri** component reaching Dollet/Timber/Galbadia Garden/Deling.

**FINDING 3 — the height-step gate is compatible + separates real cliffs; `WM_CLIMB_STEP=400` is good.** Edge step `|meanZ_A − meanZ_B|`: Galbadia component median 26, p90 171, p99 946, cliff tail ~1,900. Gated flood T=200 loses Dollet, T=300–400 connects all four; ~1,500 cliffs sit 8× above the <200 walkable band. Gated Galbadia flood at 400 = 35,527 tris, keeps every destination.

**FINDING 4 (THE CAVEAT) — the navmesh CANNOT model the `.81` Dollet false-coast.** On the real navmesh the ledge is geometrically indistinguishable from land (road↔ledge steps median 15, ledge→land 0). A* (gate 400) Galbadia→Dollet = 126 tris / ~7.4 km cuts straight across it; off-road penalties don't cleanly avoid it; road-only-through-box detours 44% longer. Confirmed in-game impassable → ENGINE COLLISION, settled by the DISASSEMBLY track, not geometry. (This session refined it: the real wall is ~4 cells WEST of the box — see THE FALSE-COAST FINDING.)

---

## THE wmx TRIANGLE FORMAT (confirmed parser spec — authoritative)

- **Archive**: `world.fi` = 12 B/entry `(uint32 uncompSize, uint32 fsOffset, uint32 compression)`. wmx.obj is **FL index 9**, RAW on this data (`compression=0`, `fsOffset=3040099`). If `compression!=0`, FF8 LZSS with a 4-byte uncompressed-size header (`WM_DecompressLZSS`). `world.fl` is the path list.
- **Segments**: 835 total, **first 768 = playable 32×24 grid**. Each **36864 (0x9000) B**. `row=seg/32, col=seg%32`; spans 8192 units. Header **68 B**: `uint32 group_id` + **16 × uint32 block offsets** (rel to segment base; 0=unused).
- **Blocks**: 16/segment, 4×4. `bRow=b/4, bCol=b%4`; spans 2048 units. **Origin `ox=col*8192+bCol*2048`, `oy=row*8192+bRow*2048`.** Header **4 B**: `[0]=poly_count, [1]=vert_count, [2]=norm_count, [3]=pad`. Then poly_count×16B polys, vert_count×8B verts, norms, 4B pad.
- **Polygon = TRIANGLE, 16 B**: `poly[0..2]`=uint8 vertex indices; `poly[0x0D]`=terrain byte. (Bytes 3–5 normal idx, 6–11 UVs, 12 texpage/blend, 14–15 type-correlated — NONE encode walkability; confirmed by byte-level dump.)
- **Vertex = 8 B**: `int16 x`@+0, `int16 (−elev/z)`@+2, `int16 y`@+4, pad@+6. World: `vwx=ox+x, vwy=oy+y, vwz=value@+2`. (Local Y runs 0..−2048 so a block spans world-y `[oy−2048, oy]`; parser hashes vertices so unaffected.)
- **Terrain**: `32/33/34`=ocean; `0–5`=forest; `27/28`=road/railroad; `29`=mountain; else land.
- **Engine→mesh** (`world_map_geometry.inl` / `NmGameToMeshX/Y`): `mesh_x=((game_x+131072) mod 262144)`, `mesh_y=((game_y+98304) mod 196608)`. (Dollet engine (−15639,−39437) → mesh (115433,58867) → seg(14,7).) Fine cell = 1024 units; `fine_col=mesh_x/1024`, `fine_row=mesh_y/1024`.

---

## ACTIVE DIAG FLAGS (LOCAL ONLY — never push)

- **`NAVMESH_DIAG 1`** (`world_map_state.inl`) — gates `world_map_navmesh.inl`, the build/AddTriangle calls, and the `Navmesh_LogConnectivity` probe. When routing swaps on, REMOVE the file-level guard (build load-bearing); keep only the verbose `LogConnectivity` behind a flag.
- `WM_RUNTIME_WALK_DIAG 1` (`world_map_state.inl`) — **DEAD** (RAM read abandoned), but its dump prints `descBase=0x01E9FDCC` each tick, which is the disassembly anchor (#1 above). Retire (set 0) + remove `DumpRuntimeWalkability()` + the Poll RTWALK block + `s_rtWalk*` state once the disassembly has what it needs.
- `DRIVE_STEER_DIAG true` (`world_map_state.inl`) — executor `[YAWDRIVE]` trace.
- `ROUTE_MAP_DIAG` / `ELEVMAP` (`world_map_planner.inl`) — `[ROUTEMAP]`/`[ELEVSTEP]`/`[ELEVMAP]`. False for push.
- `WM_CALIB_DIAG 0`, `ROAD_MAP_DIAG 0` (off).

The `.99` planner road-exemption stays for now (harmless).

---

## BACKLOG (untouched)

- #61 dialog decoder spoken-"L" (`FF8TextDecode::Decode`)
- #51–#53 naming/Angelo bugs; #50 Angelo gauges
- #45 junction non-command abilities (blocked — no GF teaches one yet)
- #37 source-size refactor queue (60/80 KB)
- Irvine Shot limit (hold until he joins the party)
- BAT-3 world-map catalog coordinate audit
- #67 (umbrella), #68 (steering — the executor half of the Dollet wedge), #69 (geometry terrain-ID — CAN ID cliffs, CANNOT ID the false coast), #70 (Dollet exit / false coast — the disassembly track) stay open.
