# World-Map Walking Navigation — Offline Rebuild (Claude Cowork Kickoff Brief)

**Owner:** Aaron (blind solo dev + sole tester, NVDA). **Venue:** Claude Cowork (long, file-heavy, autonomous multi-step build). **Date opened:** 2026-06-27 (rev 2). **Tracks:** GitHub issue #70 (and the #67 all-continent umbrella).

---

## 0. Mission (one sentence)

Build a **walking-only** world-map auto-navigation system the right way: disassemble the engine's own walking functions, replicate them faithfully in an **offline simulator**, prove the router on three acceptance scenarios **without Aaron BATing**, then write a requirements doc and implement it in the mod.

Scope is **walking only** — no car/driving, no chocobo, no Garden flight, no Ragnarok.

---

## 1. Why offline-first (the problem we are escaping)

The in-mod approach has spun for ~50 builds and regressed to "Dollet works, Timber doesn't." Root cause is structural:

1. **The mod reconstructs the walkable surface lossily.** It parses the world data, then **filters and bridges** triangles before feeding them to the navmesh (`LoadTerrainGrid` / `world_map_segments.inl`). Every filter that drops a "bad" triangle also drops real walkable ground, and every guard added to patch a gap (e.g. `[DEEPGUARD]`) misfires elsewhere. Close one hole, open another.
2. **Testing is BAT-only** — slow, serial, and self-blind: we never compare our reconstruction against the engine's own answer except by watching the character wedge.

Offline-first fixes both: replicate the engine's walking logic **faithfully** (port it, don't approximate it), run it on the real world data, and validate **before** anything touches the game.

---

## 2. THE VALIDATION ORACLE — the single core principle

> **The ground truth is the engine's own code, ported faithfully — not our reconstruction of it.**

The failing approach reconstructed the walkmesh (filter, bridge, guess) and used heuristics. This approach does the opposite: **disassemble the engine's actual walking functions from the exe and port them as a literal translation** — the same block lookup, the same polygon search, the same height interpolation, the same step gate — then run that port on the **real world-map data**. By construction, a faithful port operating on the engine's own data produces the engine's heights and the engine's allow/refuse decisions. That port is the oracle the router is validated against.

**How we prove the port is faithful (so it can validate regions we cannot capture):**
- The mod has a read-only probe, **`[GROUNDH]`**, logging the engine's true ground height from global **`0x0203FE30`** at the live position every frame to `Logs/ff8_world.log`.
- Two captures exist (both in `Logs/ff8_world.log`): a **Balamb** Garden → beach descent, and a **Galbadia** wedge cluster (dense engine-height samples right at the Galbadia-exit pinch — see §5b). **Esthar cannot be captured (no save there) and does not need to be.**
- A third, free validation asset: the **Timber → Dollet road** is a corridor the player can walk in-game, so the port must find every step of it walkable end-to-end. Any road segment the port refuses is a port/extraction bug. The mod already traces road waypoints (`[ROADV:DRIVE]`, the `s_roadFine` overlay) — Cowork can pull the road polyline from there or rebuild it from the raw mesh.
- The port **must reproduce every available trace exactly** (height and allow/refuse) and keep the whole Timber→Dollet road walkable. Once it does, it is trusted to validate **all** regions, including Esthar, with no live capture.

The danger to avoid is unchanged in spirit: never validate the router against a loose reconstruction or an approximation of the engine. Validate against the faithful port, and prove the port against real traces. A port that drifts from the traces is a porting bug to fix, not a result to accept.

---

## 3. Sources — where ground truth comes from (and what to distrust)

### 3a. The binary (PRIMARY)
- **Aaron uploads the game exe directly to Cowork** — disassemble it there with real tooling; that is faster and more reliable than reading pre-dumped text.
- A pre-dumped IDA-style disassembly also exists on disk at `Game Files/disassembly/` (~98 MB) as a **secondary cross-reference**. Address→file map: `0x53xxxx` → `FF8_EN_.text_0x00501000.asm`; `0x40xxxx` → `FF8_EN_.text_0x00401000.asm`. Image base `0x00400000`; `.text` `0x00401000`–`0x00B69000`. These files exceed the 1 MB `read_text_file` cap — read mid-file via `filesystem:edit_file` `dryRun=true` with a unique `oldText` as a grep-substitute, or `head`/`tail`. Aux indexes in `/mnt/project/`: `FF8_EN_functions.txt`, `FF8_EN_callxrefs.txt`, `FF8_EN_sections.txt`, `FF8_EN_disasm_lookup_guide.txt`.

### 3b. The world data (PRIMARY)
- **Aaron uploads the world data archive (world.zip) directly to Cowork** — parse the actual bytes there. This contains the world-map walkmesh data (`wmx.obj` and related world-map files). The truth about the format is **in these bytes plus the loader's disassembly**, not in any summary.

### 3c. The mod source and logs (via filesystem tools)
- Everything else lives in the mod directory and is reached with `filesystem:`-prefixed tools: `src/` (the current world-map module), `Logs/ff8_world.log` (the `[GROUNDH]` traces; accumulates across sessions, latest run at the tail), `CHANGELOG.md`, `DEVNOTES.md`, etc.
- Current world-map module: `src/world_map.cpp` includes, in order, `world_map_state.inl` → `world_map_geometry.inl` → `world_map_navmesh.inl` → `world_map_segments.inl` → trigger_data → catalog → announce → planner → drive → heading_scan → camera_scan → arrival → keys.
- **`WorldGroundHeightLocal` (the block-local height query) is correct and STAYS** — it returns "no ground" honestly instead of extrapolating garbage. The fix is built on it by feeding it a faithful mesh.
- **`world_map_segments.inl` / `LoadTerrainGrid` is where triangles are filtered/bridged before reaching the navmesh — the prime suspect for the dropped coastal ground.** The offline extraction parses the raw mesh with this filtering removed, and we let the faithful port + traces decide what (if anything) the engine actually excludes.

### 3d. The research docs (LEADS ONLY — treat with skepticism)
- `Plan & Research Documents/` holds many prior "deep research" results. **Several have been found inaccurate or incomplete.** Use them only to know *where to look* (which function, which offset to check), then **confirm every claim against the binary, the actual bytes, and the traces.** Do not treat any of them as authoritative.
- Useful starting leads (verify, don't trust): `WMX_OBJ_FORMAT.md`, `wmsetus Section 8 decoded.md`, `wmx.obj polygon format deep research findings.md`, `Walkmesh camera transform deep research results.md`, `World Map Terrain and Locomotion Reference.md`, `World Map Reachability Rework - offline wmx analysis findings.md` (closest precedent), and `World Map Location Coordinates Research Findings.md` / `World Map Entry Trigger Coordinates deep research results.md` (endpoint coordinates). ⚠️ `extract_walkmeshes.py` + `ff8_walkmeshes.json` here are **FIELD** walkmeshes, **not** the world map.

---

## 4. Phased plan (Aaron's five steps, sharpened)

### Phase 1 — Disassemble the world-map walking functions
From the uploaded exe, document and understand how the world map is **built, navigated, and the camera** transform — **leading with the walking-collision path, the actual blocker.** Anchor functions to find and verify (names below are leads from prior work — confirm addresses/behavior in the binary):
- `0x53E7A0` — movement validator (reads current height from `0x0203FE30`; applies the step gate).
- `0x53EB80` — find-poly for a position (CDECL; arg1 = ~260-byte movement-context buffer with a block index at `+0x20`; arg2 = `int* outHeight`; returns found-flag in `eax`).
- `0x402620` — point-in-triangle + barycentric height interpolation.
- `0x553E00` — world-data block loader.
- `0x53DC70` — game-coords → block-index locator.
- `0x53D8A0` — candidate-move builder.
- `0x53E730` — per-vehicle ground-type gate (**foot appears exempt — on foot the move is gated only by the height step rule; verify**).
- Camera: confirm the walkmesh→screen transform.
**Deliverable:** a function-map doc (call graph + each function's exact behavior + the precise walking-movement rule), and the basis for the faithful port in Phase 3.

### Phase 2 — Faithful raw walkmesh extraction
Parse the uploaded world data **exactly as the loader builds it** — every block, polygon, vertex, real heights, ground-type tags — with **no filtering, no bridging, no extra skirt-dropping** beyond what the engine itself does. Derive the format from the **bytes plus the loader disassembly**, using the docs only as hints. Produce the offline dataset + parser.
**Deliverable:** the extracted dataset + parser + a short "what the engine includes vs. what the mod was dropping" diff.

### Phase 3 — Offline simulator = the faithful engine port (the oracle)
Port the disassembled walking functions into an offline simulator: locate block → find containing polygon → interpolate height → apply the step gate. Run it on the Phase 2 data.
**Prove faithfulness against the available `[GROUNDH]` traces** (Balamb; Galbadia if captured): the port must reproduce their heights and allow/refuse decisions exactly. Once it matches, it is the trusted oracle for **all** regions including Esthar.
**Deliverable:** the port + a validation report showing per-trace match (height error and decision agreement) for every trace available.

### Phase 4 — Walking-only router
On the validated walkmesh, compute the **shortest walkable route** between two locations, staying on the mesh, navigating seamlessly (no wedging, no backtracking-into-walls). Validate **in the simulator (against the port)** for the three acceptance scenarios in §6 — each pair walkable in both directions.
**Deliverable:** the router + a pass report for all three scenarios.

### Phase 5 — Requirements doc, then implement
Only after Phase 4 passes: write a **requirements document** describing the whole system (data, extraction, the ported movement model, router, integration points, the block-local height query it builds on). Then implement in the mod, replacing the lossy reconstruction path. Implementation follows the live project rules in §7.

---

## 5. Trace-capture protocol (faithfulness anchors — NOT per-region requirements)

These captures exist only to prove the Phase 3 port is faithful. They are not the validation themselves, and not every region needs one.
- **Balamb:** ✔ captured — a manual Garden → beach descent (heights ~−654 down to ~0). In `Logs/ff8_world.log`.
- **Galbadia:** ✔ captured — a dense cluster at the Galbadia-exit wedge (the §5b findings). It does NOT cover the corridor north of the wedge (the auto-drive could not traverse it), but it pins the most contested spot precisely.
- **Timber → Dollet road:** a known-walkable validation corridor (see §2) — the port must keep all of it walkable.
- **Esthar:** **not capturable** (no save there) and **not required** — the faithful port validates it.

If the port later needs corridor coverage north of the Galbadia wedge, do NOT ask Aaron to hand-steer it (the camera is rotated there — see §5b — and he is blind). Build a blind-accessible capture aid instead: either a readout announcing the character's actual movement direction each second, or a scripted sweep that steps along the corridor and logs `[GROUNDH]` with no steering required.

---

## 5b. Captured ground-truth findings (2026-06-27 BAT traces)

**The Galbadia wedge — wrong shallow surface, not a hole.** At the Galbadia-Garden exit, every sample sat in fine cells column 99, rows 68–69 (≈ game (−29190, −27630)). There the engine's true ground (`0x0203FE30`) is ≈ **−595**, while the mod's navmesh reports ≈ **−200** — a consistent **~390-unit** gap on every sample, with `contain=1` throughout (one triangle covers the point — no overhang). So the mod is not missing a triangle here; it kept the **wrong, shallow** surface (≈ −200) and dropped the **deep** ground (≈ −595) the engine actually walks. With heights wrong by ~400 units the 200-step gate severs the connection — this is the wedge. **This is the strongest evidence for Phase 2: extract the raw mesh with no filtering — the deep −595 ground is in the raw `wmx` and the mod's filter threw it away.**

**The steering fights a rotated camera.** In the same trace, when the auto-drive pressed "up" to head north toward Timber, the character repeatedly drifted the *opposite* way (south-west). So the screen→world mapping at that location is not what the steering assumes. This is a strong hint the wedge is at least partly a **camera/projection** problem, not purely a navmesh hole — and it is why a blind manual walk through this spot is not feasible without a movement-direction readout (the player presses a direction and the character goes elsewhere, with no way to see and correct). The camera transform (Phase 1) must be nailed alongside the collision rule.

---

## 6. Acceptance scenarios (verbatim — the router must pass all three)

a. **Galbadia trio:** Timber, Dollet, and Galbadia Garden all walkable to one another.
b. **Balamb trio:** Balamb Garden, Fire Cavern, and the town of Balamb all walkable to one another.
c. **Esthar trio:** Esthar's Lunar Gate, Sorceress Memorial, and Tears' Point all walkable to one another (validated via the faithful port — no live capture).

"Walkable to one another" = a shortest on-mesh route exists in **both** directions for every pair, with no wedge and no off-mesh excursion, verified in the offline simulator.

---

## 7. Guardrails Cowork MUST honor

- **Primary sources first.** The binary, the actual world-data bytes, and the live `[GROUNDH]` traces are ground truth. The research docs in `Plan & Research Documents/` are **leads to verify, not authority** — they have been wrong before.
- **Aaron is the only one who pushes to GitHub** (`Utilities/push_to_github.ps1`). Cowork may read the repo and create/update issues, but **never pushes code**. **Do not comment on or close issue #70** until a fix is **BAT-confirmed and pushed by Aaron**.
- **Faithful port, not approximation.** Phase 3 is a literal translation of the disassembled functions; a port that disagrees with the traces is a bug to fix.
- **The block-local height query stays.** Build the coverage fix on it; do not revert to the global extrapolating query.
- **Lossy reconstruction is the enemy.** Floor-clamping / cost-correction after the fact cannot fix connections severed during build; the faithful raw mesh + ported rule is the fix.
- **Never re-enable the SET3 opcode hook (`0x1E`)** — it hangs the infirmary scene; a CI guard enforces this.
- **Filesystem:** use `filesystem:`-prefixed tools for OneDrive/Windows paths. `filesystem:edit_file` is atomic (one bad `oldText` aborts the whole edit) and **corrupts on a literal `$` in `newText`** (use `write_file`). OneDrive can throw a transient EPERM on rename — retry once. The local MCP server can hang (4-min timeout); if so, Aaron restarts Claude Desktop.
- **Three consecutive identical-outcome BATs = wrong diagnosis → pivot.** **One behavioral change per BAT** at implementation. Version bump is **one place** (`FF8OPC_VERSION` in `src/ff8_accessibility.h`, format `0.18.3.BB`) plus a matching new top `## vX.Y.Z` heading in `CHANGELOG.md` (push utility validates they match). Update `DEVNOTES.md` (10,240-byte cap) + `NEXT_SESSION_PROMPT.md` at every bump and BAT.
- **Accessibility:** every solution must be performable by a blind user via screen reader; prefer automation; never require sighted assistance or "press a key when X appears on screen."

---

## 8. Technical leads appendix (HYPOTHESES — verify against the binary and the actual bytes)

Most of the following came from research docs that have been wrong before. Items tagged **[trace-confirmed]** are backed by live engine behavior; everything else is a starting hypothesis to confirm.

**Coordinate frames.** Hypothesis: world data ≈ 30,781,440 bytes = 835 segments × 0x9000, base segments 0–767 in a 32-wide grid; engine world = 128 cols × 96 rows of **2048-unit** blocks. Mesh frame `NM_WX = 262144`, `NM_WY = 196608`, `game = mesh − half-world`; `NmGameToMeshX(gx) = ((gx + 131072) % 262144 …)`, `NmGameToMeshY(gy) = ((gy + 98304) % 196608 …)`; fine grid 256 × 192 (1024-unit). The game→mesh shift is a whole number of 2048-unit blocks, so a 2048-unit grid in mesh coords aligns with engine block boundaries (X identical; Z mirrored, irrelevant since triangles and queries bucket in the same frame). **[trace-confirmed]** that the block-local query in mesh coords works and that the global extrapolating query lies.

**World-data block/polygon/vertex layout (hypothesis — confirm from bytes + loader).** Block header 4 bytes: polyCount@0, vertCount@1, normalCount@2. Polygon 16 bytes: vertex indices @[0,1,2], ground-type @[13,14,15] (OCEAN tuple `(0x22,0x40,0x20)`). Vertex (file) 8 bytes: X@word0, pad@word1, Z@word2, height@word3. Height sign: UP = NEGATIVE Y; sea level 0; skirt verts ≤ −4000 filtered. Do **not** use polygon[0x0E] bit7 as a navmesh filter — prior work found it cuts real walkable ramps/rail. In-memory vertex layout for interp: X@0, Y@2, Z@4.

**Locator (hypothesis).** `blockCol = ((X + 0x60000) % 0x40000) / 2048`; `blockRow = ((0x48000 − Z) % 0x30000) / 2048`; `linearBlockIndex = blockRow*128 + blockCol` (0–12287). File↔engine Z-mirror: `blockCol_mine = (blockCol_eng − 1) % 128`; `blockRow_mine = (95 − blockRow_eng) % 96`.

**Collision rule (hypothesis — the thing to nail in the port).** On foot, per candidate move: locate block → find polygon → point-in-tri + barycentric height → **reject iff `|candidateHeight − currentHeight| >= 200` (`0xC8`).** Foot exempt from the per-vehicle ground-type bits. Current height read from `0x0203FE30`. Confirm the exact constant, the exact comparison, and any dynamic inputs from the disassembly.

**Landmarks [trace-confirmed]** (game coords / fine cell, from the Balamb BAT): Balamb Garden ≈ (24301, −29380), fine (c151, r67); Balamb beach/waterline ≈ (22460, −23703), fine (c149, r72); Galbadia wedge ≈ (−25040, −29700), fine (c103, r66/67); Dollet catalog entry ≈ (−15639, −39437). Pull the full town/entry set from the coordinate research docs and re-verify.

**The `[GROUNDH]` probe [trace-confirmed].** Reads engine ground height from `0x0203FE30`, logs with live game position to `Logs/ff8_world.log` (accumulates; latest run at tail). The ground-truth source for proving the port.

---

### Definition of done
The faithful port reproduces every available trace; the router walks all three scenario trios in both directions in the offline simulator (Esthar via the port, no live capture); a requirements doc exists; the system is implemented in the mod; and the in-game walk to Dollet **and onward to Timber** is BAT-confirmed by Aaron and pushed. Only then is issue #70 closed.
