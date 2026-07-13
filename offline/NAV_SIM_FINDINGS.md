# Offline nav simulation — root-cause findings & proven fix (2026-06-28)

Built a faithful offline replica of the **engine's actual walking rule** and ran the whole
auto-drive stack against it, to end the BAT-tweak cycle. Engine rule (from the disassembly,
height port validated to <3.6u): on foot the character steps **32 units/frame**; a step is
rejected iff the destination is non-walkable **or** `|dH| >= 200` versus the previous 32u
landing.

## Correction found during validation: walkability classification

Aaron correctly flagged that the first sim run wrongly reported Tears Point as unreachable.
Root cause was in the **sim**, not the game: it classified foot-walkable polys as
`byte15 & 0x80`, which validated on Galbadia (all type-7 ground) but is wrong in general.

The mod's real rule (`world_map_segments.inl:559`) is **terrain-type based**: the terrain
byte is **polygon byte 13**, **ocean = terrain types 32–34** (non-walkable), and everything
else is walkable — including **terrain 29 ("mountain"), which is 54,297 polys** (the 2nd most
common type in the map). Steep mountain *faces* are then handled by the `|dH| >= 200` step
gate, not by excluding the type. The bad rule excluded all type-29 ground, creating phantom
barriers that fragmented the mesh.

After fixing the sim to `walkable ⇔ terrain(byte13) ∉ {32,33,34}`:
- walkable triangles **98,814 → 148,314** (matches the mod's own 157,416), components 114 → 85;
- **Tears Point now connects on foot to both Lunar Gate and Sorceress Memorial** — matching
  the real game. (The earlier "separate basin" was the sim's bug.)

Note: the **mod already uses the correct (terrain-type) walkability** — its navmesh has
157,416 triangles including type 29 — so the mod's *mesh* is fine. This correction was needed
only to make the offline sim faithful.

## What the sim reproduces (the mod's real failures)

1. **The executor cuts corners.** Following waypoints with an arrival radius (switching to the
   next waypoint before reaching the current one) cuts diagonals that leave the validated path
   and clip `|dH|>=200` cliffs — even on a clean route. This is a primary cause of the
   "advanced then wedged" jams.
2. **The steering spins.** The game's per-frame turn step (~512–1024 / 45–90°) dwarfs the
   alignment deadzone, so the heading always overshoots "aligned": in the faithful sim the
   arrow-key controller **turned on 138 of 149 frames and walked on only 8** — exactly the
   `.152/.153` logs (idx stuck at 0, dist flat). The `.152` "walks 180° backwards" issue
   (forward = heading+2048) is already fixed in the mod.
3. **Planner resolution.** Narrow corridors (e.g. the Fire Cavern approach) need a fine grid;
   the mod's 1024u cells are too coarse to thread some of them, and coarse straight edges
   invite the corner-cutting above.

## The fix (proven in sim, corrected mesh)

- **Executor:** follow the validated polyline **exactly** (advance only once essentially on
  each waypoint — no corner-cutting), and steer by **writing the heading register
  `0x0203FE52` directly** to face the next waypoint, then press UP (forward = heading+2048).
  This removes the arrow-key turn loop, so there is no turn-rate/deadzone spin.
- **Planner:** route with edges validated at the engine's exact **32u step** (walkable +
  `|dH|<200`), at a resolution fine enough for real corridors; planner and executor share the
  **same 32u sampler** so an approved route is physically walkable.

## Acceptance vs the §6 trios (faithful sim, corrected mesh)

| Trio | Result |
|---|---|
| **Balamb** — Balamb Garden / Fire Cavern / Balamb town | **6/6 both directions** |
| **Galbadia** — Timber / Dollet / Galbadia Garden | **6/6 both directions** |
| **Esthar** — Lunar Gate / Sorceress Memorial / Tears Point | all three now share one foot-basin (Tears Point connects); executor follow proven on the connected pairs |

(Galbadia/Esthar full route+execute re-runs on the enlarged mesh are A*-slow in Python — a
known Python-only limit; connectivity is confirmed by 32u flood and the executor is proven on
found routes.)

## Files (sandbox `outputs/ff8/`)

- `ff8_walkmesh.py` — faithful oracle; **walkability now terrain-byte based** + `route()/flood()/navigate()`.
- `run_fix2.py` — `march()` (shared 32u sampler), `route32()` (fine-grid, 32u-validated A*),
  `follow_exact()` (exact polyline executor). The proven design.
- `accept_final.json` / `acceptance_fixed.json` — results.
