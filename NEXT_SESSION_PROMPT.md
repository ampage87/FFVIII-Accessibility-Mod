# Next Session Prompt — v0.14.85 awaiting BAT, then Chapter 2 (auto-drive) of World Map restoration

## Where we are

**v0.14.84 PASSED BAT.** `\` placeholder confirmed working. Source on top of v0.14.83 fix.

**v0.14.85 awaiting BAT.** Chapter 1 of the World Map restoration: catalog reachability filter via BFS over wmx.obj terrain grid. ~265 lines of code added to `src/world_map.cpp`, reconstructed from past chats v0.11.12-v0.11.16 (specifically https://claude.ai/chat/fbcf02c5-f762-4400-bbd8-194aba6d8a0b for terrain + BFS, https://claude.ai/chat/2c72c890-58ea-41c1-b612-0b477c90d500 for the v0.11.16 +131072 coordinate fix). Implementation includes:

- WMX constants block, `VehicleType` enum, terrain/reachability static state
- File I/O helpers (`WM_ReadFileToBuffer`, `WM_ReadFileChunk`)
- Inline LZSS decompression matching field_archive.cpp's pattern
- Coordinate conversion with the v0.11.16 +131072 X-offset fix
- `LoadTerrainGrid()` reading wmx.obj from world.fs and classifying 768 segments
- `ComputeReachability()` BFS flood-fill with torus wrapping, vehicle-aware traversal rules
- `GetVehicleType()` mapping the authoritative locomotion enum (0/6=foot, 31=Chocobo, 32-40=cars, 48=Garden, 50=Ragnarok) plus our empirical 3=Ship and 4=Ragnarok
- Modified `BuildDistanceCatalog` with deferred (0,0) build + reachability-filter compaction
- `s_catalogCount` replaces `LOCATION_COUNT` in cycling, bounds checks
- `Initialize` calls `LoadTerrainGrid()` once at module init with graceful-degradation fallback
- Fire Cavern coords corrected from Ragnarok-marker (81152, 146176) to wmx.obj on-foot entrance (36864, -28672)

Full details in DEVNOTES § v0.14.85.

## Priority 1: BAT v0.14.85

Aaron walks on Balamb island on foot from Balamb Garden:

1. Press `-` `=` to cycle catalog. Should cycle ONLY among Balamb-island places (Balamb Garden, Balamb Town, SeeD Graduation Ball, Balamb Garden MD Level, possibly Beginner Forest, possibly Fire Cavern). Off-island places must be absent.
2. Press Backspace for bearing on the filtered list.
3. Mount Ragnarok (or Ship). Catalog should expand to all 37 entries (note: may require world-map enter/exit cycle to rebuild — Chapter 2 may need to add vehicle-change-triggered rebuild).
4. Press `\`. Placeholder announce should still fire.

Log evidence in `Logs/ff8_world.log`:

- One-time startup block: `[INIT] Terrain grid loaded successfully` → `[TERRAIN] wmx.obj loaded` → `[TERRAIN] Grid built: ~195 land, ~573 ocean` → 24 `[TERRAIN] row00..row23` visual rows.
- On world-map entry: `[DEFER] Position is (0,0)` (a few times) → `[BFS] Player at (X,Y) -> seg(C,R), vehicle type N` → `[BFS] From seg ... segments reachable` → `[BFS] Filtered to K reachable locations` → `Catalog built (K entries), nearest: ...`

If BAT fails, see DEVNOTES § v0.14.85 "Risks / things to watch in BAT" for the failure-mode-to-cause map (terrain-load failure, wrong segment classification, coordinate-offset error, empty catalog protection).

## Priority 2: Chapter 2 — Auto-drive (v0.14.86)

After v0.14.85 BAT passes. This is a fresh-session candidate — Chapter 2 is its own substantial scope and benefits from a full context budget.

Source material: https://claude.ai/chat/91996041-f041-448a-94b7-da30ea01a91a — v0.11.01-v0.11.11 auto-drive design and implementation.

Use `conversation_search` to extract code:

- `conversation_search('keybd_event SetDriveKeys VK_UP scan codes')` — keyboard injection
- `conversation_search('StartAutoDrive UpdateAutoDrive sweep search')` — drive state machine
- `conversation_search('battle persistence resume world map auto-drive')` — battle pause/resume
- `conversation_search('DRIVE_ARRIVE_DIST DRIVE_APPROACH_DIST')` — tunable values

Order of work:

1. Add keyboard injection: `s_keyUpHeld` / `s_keyLeftHeld` / `s_keyRightHeld` plus `PressKey` / `ReleaseKey` / `ReleaseAllDriveKeys` / `SetDriveKeys` using `keybd_event(vk, scan, KEYEVENTF_EXTENDEDKEY [| KEYEVENTF_KEYUP], 0)`. Scan codes: VK_UP=0x48, VK_LEFT=0x4B, VK_RIGHT=0x4D.
2. Add drive state: `s_driveActive`, `s_driveTargetCatIdx`, `s_driveStartTime`, `s_driveLastAnnounce`, `s_driveLastDist`, `s_driveStuckX/Y`, `s_driveStuckCheckTime`, `s_driveStuckCount`.
3. Add tunables: `DRIVE_ARRIVE_DIST`, `DRIVE_APPROACH_DIST`, `DRIVE_ANNOUNCE_INTERVAL`, `DRIVE_STUCK_CHECK_INTERVAL`, `DRIVE_STUCK_THRESHOLD`, `DRIVE_STUCK_MAX`. Pull values from chat 1.
4. `StartAutoDrive(catIdx)` — read target, compute initial distance, announce 'Driving to X. Y units.', set `s_driveActive`.
5. `UpdateAutoDrive()` — per-frame:
   - Read player position + heading from `WM_HEADING` (0x0203ED02, 12-bit 0-4095, 0=N CW)
   - Compute relative bearing to target via `atan2`-style math (with torus wrapping)
   - Steering: within 18° = forward only; 18-45° = turn+forward; >90° = turn only (spin to face)
   - Distance < 1000 = final approach (stop steering, walk forward to sweep through trigger zone)
   - Stuck detection on `DRIVE_STUCK_CHECK_INTERVAL` windows: if movement < `DRIVE_STUCK_THRESHOLD`, increment `s_driveStuckCount`. If max reached, run sweep search (try 6 different headings, alternating right/left, increasing duration); announce 'Searching.'
   - Approach announce one-shot when crossing `DRIVE_APPROACH_DIST` threshold
   - Periodic distance announce every `DRIVE_ANNOUNCE_INTERVAL` ms
   - Arrival: distance < `DRIVE_ARRIVE_DIST` → announce 'Arrived at X.' and stop
6. Battle persistence: detect world-map exit during drive → don't stop drive, just set 'paused' flag. On world-map re-entry with target still in catalog and player position within reasonable range of last position, re-announce remaining distance and resume.
7. Wire `\` (VK_OEM_5) in `PollKeys` to call `StartAutoDrive(s_catalogIndex)` — replace the v0.14.84 placeholder. Pressing `\` again while driving → `StopAutoDrive("Cancelled.")`.
8. Player-cancel: if any arrow key is pressed by the player (not us), `StopAutoDrive("Cancelled.")`.
9. Bump version, BAT. Aaron repeats his memorable test: walking from Fire Cavern area to Balamb on foot, then driving from B-Garden to Balamb in vehicle.

## Priority 3: Chapter 3 — Polish + push (v0.14.87)

- Verify the locomotion enum reconciliation in actual gameplay (Selphie foot, Chocobo, etc.).
- Consider vehicle-change-triggered catalog rebuild (so mounting/dismounting Ragnarok mid-session updates the cycle without requiring world-map enter/exit).
- Push v0.14.83 + v0.14.84 + v0.14.85 + v0.14.86 to GitHub as a single combined commit.
- Update DEVNOTES_HISTORY with the world-map regression saga.

## Priority 4: Older deferred priorities (after world-map restoration lands)

1. Persistent accessibility settings across play sessions.
2. Remove party members from field entity catalog.
3. X-ATM092 chase scene accessibility.
4. Walk-and-talk dialog gap (hardcoded engine path).

## Priority 5: GitHub issue #27 — SeeD Rank misreads as "No rank yet"

Unchanged. https://github.com/ampage87/FFVIII-Accessibility-Mod/issues/27

## Priority 6: DEVNOTES rotation (overdue)

Move completed investigations to `DEVNOTES_HISTORY.md` (audio-ducking, Scan TTS architecture chapter, popup-hook saga, threshold-tiering saga, the v0.14.83+v0.14.84+v0.14.85+v0.14.86 world-map regression saga once it lands).

## Files in current state

- `src/world_map.cpp` — v0.14.85: full Chapter 1 reachability filter restored. Helpers in place. `\` still goes through v0.14.84 placeholder. Awaiting BAT.
- `src/ff8_accessibility.h` — version `0.14.85`.
- `src/scan_tts.cpp` — v0.14.82 chance-based weakness tier (50% Vulnerable cutoff). Stable.
- `src/battle_tts_sprite.inl`, `src/battle_tts_screenshot.inl` — v0.14.79 popup-hook fixes intact.
- `src/battle_tts.h` — `BENT_STATUS_RESIST_BASE = 0x80` (BAT-validated since v0.14.77).
- `DEVNOTES.md` — top section is v0.14.85 awaiting BAT, full Chapter 1 narrative in § v0.14.85.
- `NEXT_SESSION_PROMPT.md` — this file.
- GitHub: `main` HEAD = `7c7afdf3` (v0.14.82). Local has v0.14.83 + v0.14.84 + v0.14.85 unpushed pending Chapter 2 completion.

## Mandatory session-start ritual

Read `DEVNOTES.md` and this file before doing any work. `DEVNOTES_HISTORY.md` only when tracing past decisions. Update both files at every version bump and after every BAT result. **For Priority 2 (Chapter 2 auto-drive), use `conversation_search` to read the implementation chat BEFORE writing any code; the chat contains the actual implementation patterns that made v0.11.16 work.**

**v0.14.83 PASSED BAT.** Vehicle-spam loop silenced via the canonical-locomotion whitelist; nav keys (`-` `=` Backspace) re-wired via PollKeys. BAT log shows exactly one `Ship (mode 3)` announce on boarding with no `Unknown vehicle` entries, and `[KEY]` log entries on every nav keypress.

**v0.14.84 ships next** with a `\` placeholder so the keypress isn't silent ('World map auto-drive is not yet implemented in this build.'). Source-only at the moment; not yet deployed/BAT'd.

**Major finding from this session — the BFS terrain filtering and auto-drive features were REAL shipped code at v0.11.16 (2026-04-04, sessions 32-33), not just designed-but-never-implemented.** Aaron explicitly remembered Fire Cavern→Balamb (walking) and B-Garden→Balamb (driving) auto-drives working. Verified via `conversation_search`: three past chats contain the implementation. The features were lost in the v0.14.24 build damage and never restored — the v0.14.31 fix only put back `Update()` and `Shutdown()` because those were the only deletions that triggered linker errors.

**Aaron answered the catalog filtering question.** Option 1: reachability filter (only show locations walkable/sailable from current position given current locomotion). This matches the v0.11.16 BFS implementation exactly.

## Old material below (kept for reference; superseded by sections above)

### Priority 1: Run the v0.14.84 BAT

Quick (~30s) validation:

1. Walk on world map, press `\`. Should hear 'World map auto-drive is not yet implemented in this build.' Check `Logs/ff8_world.log` for a `[KEY] backslash placeholder` line.
2. Confirm v0.14.83 behavior unchanged: `-` `=` Backspace still work, vehicle changes still announce cleanly.

If pass → move to Priority 2. If fail → debug normally; the change is small.

## Priority 2: Dedicated World Map restoration session (v0.14.85+)

This is the meaty work. **Recommended as a fresh session** because it has its own context budget needs (multiple chat reads + careful reconstruction of ~220 lines of code). The current session has done the heavy lifting of *figuring out what was lost and where the implementation lives*; the next session should focus on *carefully reconstructing the code and testing each layer*.

### Source material (all on disk + accessible via conversation_search)

**Past Claude chats** containing the actual implementation:

1. https://claude.ai/chat/91996041-f041-448a-94b7-da30ea01a91a — *World map navigation implementation planning* (v0.11.01-v0.11.11): auto-drive design, fake-gamepad failure analysis, switch to keyboard injection, location catalog with Fire Cavern, sweep search, battle persistence.
2. https://claude.ai/chat/fbcf02c5-f762-4400-bbd8-194aba6d8a0b — *Continuing world map development* (v0.11.12-v0.11.14): terrain grid infrastructure (`s_terrainGrid[24][32]`, `s_reachable[24][32]`), LZSS decompression, `WM_ReadFileToBuffer`/`WM_ReadFileChunk`, `LoadTerrainGrid` reading wmx.obj from world.fs, `ComputeReachability` BFS flood-fill with torus wrapping, integration into `BuildSortedCatalog`.
3. https://claude.ai/chat/2c72c890-58ea-41c1-b612-0b477c90d500 — *World map accessibility continuation* (v0.11.15-v0.11.16): deferred catalog build for position-validity polling (player position reads (0,0) for several frames at world-map entry — must wait); `WorldXToSegCol` +131072 offset coordinate fix that finally made BFS work end-to-end. Final BAT in this chat: *"Driving worked as expected! Auto-Drive took me from Garden to the town of Balamb as expected."*

**Research docs** on disk (Plan & Research Documents/):

- `World Map Accessibility deep research results.md` — overall architecture
- `World Map Terrain and Locomotion Reference.md` — terrain enum + locomotion enum (authoritative)
- `wmx.obj polygon format deep research findings.md` — binary format
- `World Map Location Coordinates Research Findings.md` — catalog coordinates
- `extract_walkmeshes.py`, `ff8_walkmeshes.json` — field walkmeshes (NOT world map; reference only)

### Use `conversation_search` first to read the relevant chats

The chats are long. Use targeted queries to pull the specific code:

- `conversation_search('LoadTerrainGrid wmx.obj LZSS')` — terrain loader
- `conversation_search('ComputeReachability BFS flood-fill')` — BFS implementation
- `conversation_search('WorldXToSegCol coordinate offset 131072')` — coordinate conversion (the v0.11.16 fix)
- `conversation_search('keybd_event auto-drive SetDriveKeys')` — keyboard injection
- `conversation_search('StartAutoDrive UpdateAutoDrive sweep search')` — drive state machine
- `conversation_search('battle persistence resume world map')` — battle pause/resume
- `conversation_search('Fire Cavern wmx.obj 36864 -28672')` — Fire Cavern catalog entry

Read in this order; each fetch costs context, so be deliberate.

### Phased restoration plan (suggested chapter breaks)

**Chapter 1 — Catalog reachability filter (v0.14.85)**

Goal: get the catalog to filter by terrain reachability per current locomotion. No auto-drive yet.

Order of work:

1. Read chats 2 and 3 from above to extract the terrain-loading and BFS code.
2. Add Fire Cavern as the 38th catalog entry (coords ~36864, -28672 per chat 1).
3. Add LZSS decompression + game-file I/O (`world.fi` entry 9 → wmx.obj from `world.fs`). Implement `WM_ReadFileToBuffer` and `WM_ReadFileChunk` for the read pipeline. This will need to mirror `field_archive.cpp`'s file-access pattern.
4. Add `s_terrainGrid[24][32]` (0=ocean, 1=land, 2=shallow per chat) and `LoadTerrainGrid()`. Call it once at module init. Expected result per v0.11.16 BAT: 195 land + 573 ocean = 768 segments classified.
5. Add `WorldXToSegCol(int32_t x)` returning `(x + 131072) / 8192 mod 32` and `WorldYToSegRow(int32_t y)` returning `y / 8192 mod 24`. **The +131072 X offset is critical — without it, BFS starts in ocean and filters everything out.**
6. Add `s_reachable[24][32]` and `ComputeReachability(startCol, startRow)` — BFS flood-fill, 4-connected with torus wrapping. Locomotion-aware: foot = land only; chocobo = land + shallow ocean; Garden = land + all ocean; Ragnarok = skip BFS entirely (everywhere reachable). Use the authoritative locomotion enum: 0/6=foot, 31=chocobo, 32-40=cars, 48=Garden, 50=Ragnarok.
7. Add deferred catalog build: `BuildSortedCatalog` skips and returns when player position reads (0,0); `Poll()` retries each frame until valid position is read.
8. Modify `BuildSortedCatalog` to filter out catalog entries whose segment isn't reachable.
9. Update `GetVehicleName` to use the authoritative enum (current values 1=Car, 2=Chocobo, 3=Ship, 4=Ragnarok are wrong).
10. Bump version, BAT. Aaron walks on Balamb island on foot — catalog should show only Balamb-island places (Balamb Garden, Balamb Town, Fire Cavern, Beginner Forest, SeeD Graduation Ball, Balamb Garden MD Level). Ride Ragnarok — catalog should expand to all 38.

**Chapter 2 — Auto-drive with sweep search (v0.14.86+)**

Goal: get `\` to drive the player toward the selected location.

Order of work:

1. Read chat 1 from above to extract the auto-drive implementation.
2. Add keyboard injection: `s_keyUpHeld` / `s_keyLeftHeld` / `s_keyRightHeld` plus `PressKey` / `ReleaseKey` / `ReleaseAllDriveKeys` / `SetDriveKeys` using `keybd_event(vk, scan, KEYEVENTF_EXTENDEDKEY [| KEYEVENTF_KEYUP], 0)`. Scan codes: VK_UP=0x48, VK_LEFT=0x4B, VK_RIGHT=0x4D.
3. Add drive state: `s_driveActive`, `s_driveTargetCatIdx`, `s_driveStartTime`, `s_driveLastAnnounce`, `s_driveLastDist`, `s_driveStuckX/Y`, `s_driveStuckCheckTime`, `s_driveStuckCount`.
4. Add tunables: `DRIVE_ARRIVE_DIST`, `DRIVE_APPROACH_DIST`, `DRIVE_ANNOUNCE_INTERVAL`, `DRIVE_STUCK_CHECK_INTERVAL`, `DRIVE_STUCK_THRESHOLD`, `DRIVE_STUCK_MAX`. Pull values from chat 1.
5. `StartAutoDrive(catIdx)` — read target, compute initial distance, announce 'Driving to X. Y units.', set s_driveActive.
6. `UpdateAutoDrive()` — per-frame:
   - Read player position + heading from `WM_HEADING` (0x0203ED02, 12-bit 0-4095, 0=N CW).
   - Compute relative bearing to target via `atan2`-style math (with torus wrapping).
   - Steering: within 18° = forward only; 18-45° = turn+forward; >90° = turn only (spin to face).
   - Distance < 1000 = final approach (stop steering, walk forward to sweep through trigger zone).
   - Stuck detection on `DRIVE_STUCK_CHECK_INTERVAL` windows: if movement < `DRIVE_STUCK_THRESHOLD`, increment `s_driveStuckCount`. If max reached, run sweep search (try 6 different headings, alternating right/left, increasing duration); announce 'Searching.'
   - Approach announce one-shot when crossing `DRIVE_APPROACH_DIST` threshold.
   - Periodic distance announce every `DRIVE_ANNOUNCE_INTERVAL` ms.
   - Arrival: distance < `DRIVE_ARRIVE_DIST` → announce 'Arrived at X.' and stop.
7. Battle persistence: detect world-map exit during drive → don't stop drive, just set a 'paused' flag. On world-map re-entry with target still in catalog and player position within reasonable range of last position, re-announce remaining distance and resume.
8. Wire `\` (VK_OEM_5) in `PollKeys` to call `StartAutoDrive(s_catalogIndex)` — replace the v0.14.84 placeholder. Pressing `\` again while driving → `StopAutoDrive("Cancelled.")`.
9. Player-cancel: if any arrow key is pressed by the player (not us), `StopAutoDrive("Cancelled.")`.
10. Bump version, BAT. Aaron repeats his memorable test: walking from Fire Cavern area to Balamb on foot, then driving from B-Garden to Balamb in vehicle.

**Chapter 3 — Polish + push (v0.14.87+)**

- Verify the locomotion enum reconciliation in actual gameplay (Selphie foot, Chocobo, etc.).
- Push v0.14.83 + v0.14.84 + v0.14.85 + v0.14.86 to GitHub as a single combined commit.
- Update DEVNOTES_HISTORY with the world-map regression saga (v0.14.83 — v0.14.86).

## Priority 3: Resume the older deferred priorities (after world-map restoration lands)

1. Persistent accessibility settings across play sessions.
2. Remove party members from field entity catalog.
3. X-ATM092 chase scene accessibility.
4. Walk-and-talk dialog gap (hardcoded engine path).

## Priority 4: GitHub issue #27 — SeeD Rank misreads as "No rank yet"

Unchanged. https://github.com/ampage87/FFVIII-Accessibility-Mod/issues/27

## Priority 5: DEVNOTES rotation (overdue)

Move completed investigations to `DEVNOTES_HISTORY.md` (audio-ducking, Scan TTS architecture chapter, popup-hook saga, threshold-tiering saga, the v0.14.83+v0.14.84+v0.14.85+v0.14.86 world-map regression saga once it lands).

## Files in current state

- `src/world_map.cpp` — v0.14.84: PollKeys with `-` `=` Backspace `\` (placeholder) handlers; whitelist guard in CheckVehicleChange; FF8OPC_VERSION-based init log. Awaiting v0.14.84 BAT, then expansion in v0.14.85+.
- `src/ff8_accessibility.h` — version `0.14.84`.
- `src/scan_tts.cpp` — v0.14.82 chance-based weakness tier (50% Vulnerable cutoff). Stable.
- `src/battle_tts_sprite.inl`, `src/battle_tts_screenshot.inl` — v0.14.79 popup-hook fixes intact.
- `src/battle_tts.h` — `BENT_STATUS_RESIST_BASE = 0x80` (BAT-validated since v0.14.77).
- `DEVNOTES.md` — top section is v0.14.84 awaiting BAT, with the implementation-discovery breakthrough fully documented.
- `NEXT_SESSION_PROMPT.md` — this file.
- GitHub: `main` HEAD = `7c7afdf3` (v0.14.82). Local has v0.14.83 + v0.14.84 unpushed pending v0.14.84 BAT pass + the dedicated restoration session.

## Lessons accumulated for next memory pruning pass

1. Always verify popup signatures directly from a [POPUP] log entry before changing popup-hook conditions (v0.14.79).
2. The FF8 vanilla Scan UI does NOT display status weakness/resistance.
3. Sleep=50 universal across early enemies is REAL FF8 design.
4. Status inflict % depends on BOTH byte AND target Spirit (for magic casts) or Vitality (for ST-Atk-J).
5. Trust direct log evidence over prior changelog claims when they conflict.
6. Test design assumptions in actual gameplay AND against community canon.
7. Conservative thresholds lose information.
8. **(v0.14.83)** Recovery seams (v0.14.31 from v0.14.24) commonly leave functions defined-but-orphaned: defined in the .cpp, never declared in the .h, never called from anywhere. After any such recovery, audit each module's public surface against actual callers.
9. **(v0.14.83)** "Static address" research can age in its *semantics* even when the address itself stays valid. WM_LOCOMOTION at 0x02040A5E reads canonical {0,1,2,3,4} at mount/dismount but reads animation-phase residue during steady locomotion. Whitelist on canonical values when the read is "right most of the time."
10. **(v0.14.84)** When a recovery only restores what triggers compile/link errors, silently-orphaned code can disappear forever. The v0.14.31 recovery restored `Update()` + `Shutdown()` (because those caused linker errors) but did NOT restore the ~220 lines of BFS + auto-drive code that lived alongside them — there were no callers complaining at link time, so nothing flagged the loss. After any recovery: spot-check feature parity against past BAT logs, not just compile success.
11. **(v0.14.84)** `conversation_search` is the right tool for "where did this feature originally land" questions when DEVNOTES_HISTORY doesn't cover the timeframe. It surfaces the actual implementation chats, including specific BAT-success messages that confirm what worked. The v0.11.x history was never archived to DEVNOTES_HISTORY (file has zero `v0.11` substrings) — `conversation_search` filled the gap.
12. **(v0.14.84)** When research-doc findings contradict the existing source code (locomotion enum disagreement: research says Chocobo=31, code says 2), the research is right. Verify in past BAT chats and update.
13. **(v0.14.84)** Don't reach for `web_fetch` on `raw.githubusercontent.com` URLs — that domain isn't in the bash allowed-domains list. Use the `github:get_file_contents` MCP tool. Note however that the tool's `branch` parameter rejects arbitrary commit SHAs in practice; for historical state, it's only reliable on actual branch names. Past-chat investigation is more productive than chasing API workarounds.

## Mandatory session-start ritual

Read `DEVNOTES.md` and this file before doing any work. `DEVNOTES_HISTORY.md` only when tracing past decisions. Update both files at every version bump and after every BAT result. **For Priority 2 (world-map restoration), use `conversation_search` to read the three identified chats BEFORE writing any code; the chats contain the actual implementation patterns that made v0.11.16 work.**
