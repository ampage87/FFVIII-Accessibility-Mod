# Next Session Prompt

## Status: v0.14.102 BAT-PASSED. Awaiting ChatGPT deep research before v0.14.103.

Aaron's BAT confirmed v0.14.102 fix works — the rental car actually moves now under AD control. Car AD restored after the regression.

## Aaron's v0.14.103 decision

**One bigger build that knocks out all four robustness priorities together** (vehicle detection + forest avoidance + separate car AD path + arrive-near-location). Fuel awareness DEFERRED — A* already optimizes path length; if AD is too indirect we'll disable fuel consumption rather than route-plan around it.

## Behavioral clarifications from Aaron

- **Cars CAN enter "Balamb-style" towns** (Balamb, Dollet, Deling City, Esthar). Engine auto-dismounts when the car drives onto the town's entry trigger.
- **Cars CANNOT enter "Garden-style" locations** (Balamb Garden, etc.). The car physically bounces off the location's collision walls.

This simplifies arrival design: AD doesn't need a per-location car-entry table. Natural behavior covers both:
- Car-friendly town → engine fires field transition → existing arrival path runs.
- Non-car-friendly location → car bounces, AD's stuck-near-target detection fires → announce "Arrived near [Location]. Dismount and walk to enter."

## Deep research prompt drafted

Saved at `Plan & Research Documents/Vehicle state and car position deep research prompt.md`. Three questions:

1. Runtime address of the live vehicle-state flag (current vehicle being piloted; the byte at `0x02040A5E` is unreliable — reads 6 = foot in rental car).
2. Runtime address of the car's world-map position (separate from foot character; foot position freezes while in car).
3. Data source for per-location car-entry capability (per-polygon ENTERABLE flag in wmx.obj? per-trigger-program flag in wmsetus Section 8? hardcoded engine list?).

The prompt includes confirmed runtime addresses, savemap WORLDMAP struct layout, locomotion enum, terrain enum, and the SAVEMAP HEADER CORRECTION. It also asks ChatGPT to verify or refute the prior caveat that PC version may omit position arrays from the runtime savemap struct.

**Aaron's next step:** paste the prompt into ChatGPT deep research mode. When results come back, paste them into the next chat and I'll ship v0.14.103.

## v0.14.103 scope (single coherent build, ships once research returns)

1. **Vehicle detection** — read the new runtime flag; populate `s_currentVehicle` enum.
2. **Car position runtime address** — wire AD's distance/bearing/arrival logic to read car position when `s_currentVehicle == VEH_CAR`, foot position otherwise.
3. **Forest avoidance for car AD** — extend `s_terrainGrid[][]` from 2-state (land/ocean) to 3-state (land/forest/ocean); `IsSegmentTraversable` returns false for forest when vehicle == VEH_CAR. Existing `World Map Terrain and Locomotion Reference.md` puts forest at terrain values 0–5.
4. **Separate car AD path** — once detection works, AD steering and arrival use the right state per vehicle. SetDriveKeys behavior doesn't need to change (A is always injected; harmless on foot).
5. **Arrive-near-location announce** — stuck-near-target while in car at a non-car-friendly location → "Arrived near [Location]. Dismount and walk to enter."

## Implementation notes for v0.14.103

- Forest avoidance: existing `LoadTerrainGrid` walks polygon terrain bytes per segment. Replace the binary `s_terrainGrid[24][32]` (0=land, 1=ocean) with a 3-state classifier (0=land, 1=forest, 2=ocean) using a "majority of polygons" rule per segment. Update `IsSegmentTraversable(row, col, veh)` to check forest separately: `veh == VEH_CAR` rejects forest segments; foot/Chocobo/Garden/Ragnarok ignore forest.
- Car position: once research returns the runtime address, add a parallel set of getters `GetWorldMapPosition_Active(x, y, z)` that returns car position when in car, foot position otherwise. UpdateAutoDrive calls the active getter; PollKeys (cycle/announce) uses foot for catalog-distance calculations (catalog is from the player's POV, not the car's).
- Vehicle detection: poll the new flag each tick in `CheckVehicleChange`; replace the unreliable byte-21-debounce logic with the new flag. Keep the `s_lastVehicle` interface unchanged so downstream code (BFS rule class, AD, catalog filter) doesn't need rewriting.
- Arrive-near-location: when AD is in car-mode and stuck-detection fires within `DRIVE_NEAR_LOCATION_DIST` (e.g. 1500 units) of target, announce "Arrived near [Location]. Dismount and walk to enter." and StopAutoDrive(silent). For locations the car CAN enter, the engine fires a field transition before stuck-detection trips, so the existing arrival path runs first.

## GitHub state

`main` HEAD = `77e6ef28` (v0.14.98). Local at v0.14.102 (four ahead, all BAT-PASSED). Recommended bundle push: v0.14.99 + v0.14.100 + v0.14.101 + v0.14.102 as ONE commit. Commit message in DEVNOTES.md.

## Deferred queue (post-v0.14.103)

1. Fuel awareness (low-fuel warning, route weighting toward roads). Backup: disable fuel consumption if AD is indirect.
2. v0.15.x persistent accessibility settings (refined-coord serialization).
3. Empty-path refined-coord steering for narrow-gate locations.
4. Final-approach log-spam edge-detection.
5. Audit other story-gated programs.
6. Remove party members from field entity catalog.
7. Issue #27 (SeeD rank R key).
8. X-ATM092 chase scene.
9. Walk-and-talk dialog gap.

## Persistent rules reinforced this session

The "existing knowledge first" memory rule (entry 30) saved us twice:
- Recovered A=gas/W=reverse from v0.11.13/v0.11.14 conversation_search (v0.14.102 fix).
- Surfaced existing terrain + savemap research before drafting the deep research prompt — let the prompt be narrowly focused on the genuine gaps rather than re-asking already-answered questions.

Always check `Plan & Research Documents/` AND past conversations BEFORE proposing new logic.
