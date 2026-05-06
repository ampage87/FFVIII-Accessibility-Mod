**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader. The mod is open-source at `github.com/ampage87/FFVIII-Accessibility-Mod`.

**Target platform:** FF8 Steam 2013 + FFNx v1.23.x (user installs separately). Mod builds as MSBuild .sln (Win32), outputs `dinput8.dll`. FFNx source at `github.com/julianxhokaxhiu/FFNx` is reference only (address offsets). Echo-S voice mod proves field dialog hooks work.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

**Completed milestones:**
- Title screen TTS (v02.00)
- FMV audio descriptions + skip (v03.00)
- Field dialog TTS at v04.36 — all MES/ASK/AMES/AASK/AMESW/RAMESW opcodes hooked; `show_dialog` hook for tutorials/thoughts; walk-and-talk gap remains (hardcoded engine path)
- World map navigation with BFS terrain filtering, auto-drive, location catalog
- Field navigation: entity catalog, GPS navigation, A\* pathfinding with walkmesh, camera-calibrated compass directions, SETLINE/SET3 runtime hooks, JSM scanner for interactive objects
- Battle TTS: command menus, sub-menus (Magic/GF/Draw/Item), EWM, GF fire prevention, victory screen (screenshot-based pipeline), damage/HP announcements (impact-time via sub_5068B0 render hook = production trigger; sub_48EF80 popup-create = diagnostic publisher only; anim-flag-fall = catch-all fallback)
- Junction menu TTS, save/load screen TTS, menu system TTS
- Scan spell auto-announce + interactive number-key UI (v0.14.50 → v0.14.82) — all keys 1–0 wired, chance-based weakness tiering with Spirit accounting, BENT_STATUS_RESIST_BASE = 0x80
- GF summon audio descriptions (v0.14.44) — 18 VTTs covering 16 junctioned GFs + Phoenix + Odin
- SFX volume control + ducking-toggle scaffold + keyboard layout reshuffle (v0.14.45 + v0.14.46)
- World map BFS catalog reachability filter + auto-drive + animation-byte suppression (v0.14.83 → v0.14.90.3) — pushed to GitHub
- **World map field-entry trigger reverse-engineering + deferred-arrival fix (v0.14.91 → v0.14.96) — Chapter 3 functionally closed for early-game test surface, BAT-validated end-to-end**
- Multi-channel logging system (6 domain logs); `.inl` file splitting
- Full FF8_EN.exe disassembly reference at `Game Files/disassembly/`

---

**Current build: v0.14.96 BAT-PASSED Wed 2026-05-06.** Aaron successfully drove to all three test destinations (Balamb Town, B-Garden, Fire Cavern), AD correctly identified each arrival, and resumed correctly after random encounters. Fire Cavern's arrival fired correctly for the first time across the entire build sequence — closes the v0.14.93 Issue 3 case.

**Chapter 3 closed for early-game (Balamb Island) test surface.** Local working tree is FOUR versions ahead of GitHub (HEAD `7fdce360` = v0.14.92). Aaron's next action: push v0.14.93 + v0.14.94 + v0.14.95 + v0.14.96 as a single bundled commit using `Utilities/push_to_github.bat`. Consolidated commit message provided below.

---

**v0.14.93 — embedded the 38 decoded trigger programs** from wmsetus.obj Section 8 as `s_triggerPrograms[]` C++ array; extended dump list to {2,7,8} so Section 2 (the 32×24 segment-region byte map) gets captured at module init. New `[TRIGGER-PROGRAMS]` log block at `Initialize` walks the array and emits one line per program (38 lines total) for runtime sanity-check. Schema: `TriggerClause` = {vehicle, region, story_gte, story_lt, unk_flags}; `TriggerProgram` = {loc_id, top_story_gte, top_story_lt, top_vehicle, num_clauses, *clauses}. Vehicle operands per `Plan & Research Documents/wmsetus Section 8 decoded.md`: 0x80=foot, 0x84=alt-leader foot, 0x30=Garden, 0x31=Chocobo, 0x32=Ragnarok, 0x00=any. Region operands index Section 2's 32×24 byte map. story_gte=0 means no lower bound, story_lt=0 means +∞. NO game behavior change at this stage — data embedded but not yet wired into AD targeting.

**v0.14.94 — auto-drive refactored from linear-direction-with-nudge steering to A* path planning** on the 32×24 segment grid. Three integrated changes:

(1) **Vehicle-noise hotfix in `CheckVehicleChange`:** early-return when `s_driveActive` prevents AD's keybd_event arrow-key injection from polluting `s_lastVehicle` via the locomotion byte at 0x02040A5E (which cycles through canonical Car/Garden/Ragnarok values during AD operation, each held >64 ms past the v0.14.90 4-poll debounce). Resolves Issues 1+2 from the v0.14.93 BAT: 'Car' announcements during drives + sweep search not firing when stuck in final approach.

(2) **Section 2 loader** captures the 32×24 segment-region byte map from wmsetus.obj into `s_segmentRegionMap[24][32]` at module init. Loader extends the existing `LoadTriggerZones` (which already reads the same archive for diagnostic dumping) — no duplicate I/O. Indexing: byte at offset `row*32+col` with no header (the 4-byte trailer at offset 768 was previously misdocumented as a header in v0.14.93's comment; corrected).

(3) **A* path planner** replaces v0.14.86's linear-direction steering. New `PlanPath(start_seg, vehicle, goal_seg_set)` runs A* on the 32×24 grid with 4-neighbor edges (foot/Chocobo/Car: land-only; Ship/Garden: any segment; Ragnarok: skips planner entirely), wrap-aware Manhattan heuristic (east-west torus shortcut for Esthar approaches), uniform edge cost. Goal-set construction: `MatchProgramForCatalog(catalog_x, catalog_y, vehicle, story)` walks `s_triggerPrograms[]` looking for any clause satisfiable from current state (vehicle predicate + story window with savemap word at 0x2036BDE); `CollectGoalSegments` collects every cell whose region byte equals that clause's region operand. `StartAutoDrive` runs the planner once; `UpdateAutoDrive` steers toward `s_drivePath[s_drivePathIdx]`'s segment center and advances when the player crosses into it. Arrival: replaces v0.14.90.2's distance heuristic with segment-membership — when the world map exits AND the player's last segment was in the goal set, AD declares arrival. Replan on world-map re-entry after battle pause. Sweep search and stuck detection stay as fallbacks.

**v0.14.95 — fixed v0.14.94 BAT failure** where `MatchProgramForCatalog` declined every drive because catalog (X,Y) coords are icon centers on open land, NOT trigger zones. Rewrote with closest-active-region search: build the active region SET first (every region byte referenced by ANY satisfiable clause from veh + story), then walk the 32×24 grid for the segment closest to (catRow, catCol) whose region is in that active set. 5-segment distance cap avoids accidentally routing to some other location's trigger. Two-pass clean/UNK preference kept. Also extended `WMSETUS_DUMP_SECTIONS_1IDX[]` from {2,7,8} to {2,7,8,9,19} to investigate whether Sections 9 and 19 (also 772 bytes each) are additional region maps — v0.14.95 BAT later DISPROVED this: still 16 active regions after the dump. Multi-section region-map hypothesis was wrong. Resolving the missing 26 region IDs (s_triggerPrograms[] references 45 unique regions but Section 2 alone has only 19) is deferred to v0.14.97+ and likely requires disassembly hunt for the wmsetus initialization path. Also enhanced `[DRIVE] Paused` log with seg=(C,R) and region=0xRR fields.

**v0.14.96 — fixed v0.14.95 BAT false-positive arrivals** per Aaron's diagnosis. v0.14.95's distance heuristic announced "Arrived" when random encounters fired within 1500 units of a destination because both signals (scene flag flip + dist-to-target unchanged) look identical at exit. v0.14.95 BAT log: 12:43:36 declared `Arrival via exit-distance (fallback)` for Balamb Garden at lastPos=(25405,-30324) dist=1215 — but the player had drifted 1100 units southward in 3 seconds, which is encounter behavior, not field-entry. Same pattern at 12:51:26 for Balamb Town.

Aaron's suggestion: use the game's settled game mode AFTER the world-map exit. The mode register at `FF8Addresses::pGameMode` (already exposed since v04.00 / v01.13, used by `IsOnField` etc.) takes 1-3 polls to transition from MODE_WORLDMAP (2) to its destination mode after `IsOnWorldMap` flips false. The v0.14.90.2 changelog noted reading pGameMode AT the moment of exit always reads MODE_WORLDMAP because the register hasn't transitioned yet — but reading it AFTER a brief wait IS robust.

**New deferred-arrival state machine.** When world map exits while a drive is active: capture exit tick, release drive keys, set `s_driveAwaitingArrivalDecision = true` (drive stays active so cancel works). New `ResolveDeferredArrival()` runs each Poll() tick (Poll restructured so it does NOT early-return when waiting). Decision table:
- `MODE_FIELD (1)` → real arrival (also reads pCurrentFieldId + pCurrentFieldName for log clarity)
- `MODE_SWIRL (3)` / `MODE_BATTLE (999)` / `MODE_AFTER_BATTLE (4)` → encounter (paused, drive resumes on world-map re-entry)
- Anything else (incl. lingering MODE_WORLDMAP=2) → keep waiting
- Wait timeout: `ARRIVAL_DECISION_TIMEOUT_MS = 2000ms`. On timeout, fall back to v0.14.95 segment-membership / distance heuristic with 'timeout-fallback' suffix in logs.

NO new addresses (`pGameMode`, `pCurrentFieldId`, `pCurrentFieldName` all already exposed). NO new hooks. NO build script changes.

**v0.14.96 BAT-PASS:** Aaron successfully drove Balamb Garden → Balamb Town → Fire Cavern. AD correctly identified each arrival, paused/resumed correctly across multiple random encounters, and Fire Cavern's arrival fired correctly for the FIRST TIME across the entire build sequence (closes the v0.14.93 Issue 3 case). The deferred game-mode check works as designed.

---

**Files touched in v0.14.93–v0.14.96:**
- `src/world_map.cpp` (~1100 net lines across the four builds)
- `src/ff8_accessibility.h` (FF8OPC_VERSION bumps 0.14.92 → 0.14.93 → 0.14.94 → 0.14.95 → 0.14.96 with cumulative changelog comments)
- `Plan & Research Documents/wmsetus Section 8 decoded.md` was created in v0.14.92 (already on GitHub) and is unchanged

NO new addresses across all four builds. NO new hooks. NO build script changes.

---

**Lessons from this chapter:**

- Catalog (X,Y) coords are icon centers, NOT trigger zones — they sit on open land and rarely match the segment region the trigger expects. Closest-active-region search is the right algorithm.
- Reading game-state registers (pGameMode etc.) AT the moment of an edge-detected event always reads stale values. Defer 1-3 polls and the register settles. The v0.14.90.2 "fragile" comment was about reading instantly; deferred reads are robust.
- Deep-research hypotheses can have unrelated false positives. Sections 9 and 19 looked like region maps (772 bytes each, same as Section 2) but turned out to be other data. v0.14.96's deferred-arrival fix made the planner correctness issue moot for early-game arrivals because the game-mode branch is authoritative regardless of whether the planner declines.
- Always check user diagnoses against the BAT log carefully — Aaron's "if a random encounter triggers as you are getting very close it misidentifies the random encounter as having arrived at the destination" was precisely the pattern I confirmed by tracing the 12:43:36 and 12:51:26 incidents in `Logs/ff8_world.log`.

---

**On the horizon (deferred queue, in priority order):**

1. **Persistent accessibility settings** — refined-coord serialization to `%APPDATA%\FF8AccessibilityMod\refined_entries.json` is the first slice. Currently only EWM persists; refined entry coords for world-map locations are lost on game restart. Aaron has empirical refined coords for Balamb Town, B-Garden, and Fire Cavern from the v0.14.96 BAT — good seed data.
2. **Remove party members from field entity catalog** — minor cleanup so navigating field entities skips Squall/Quistis/Selphie.
3. **X-ATM092 chase scene accessibility** — proposed: freeze X-ATM092 movement after battles until new field screen loads (so the player isn't randomly killed during dialog).
4. **Walk-and-talk dialog gap** — hardcoded engine path; lowest priority because no clean hook point.
5. **GitHub issue #27 (SeeD rank R key)** — `FIELD_H_OFFSET = 0xF94` likely uses wrong section size; mirrors prior savemap offset correction.
6. **Resolving missing 26 region IDs** in v0.14.95 closest-active-region planner — Section 2 alone has 19 of the 45 unique region IDs `s_triggerPrograms[]` references. Sections 9/19 weren't it. Likely requires disassembly hunt for the wmsetus initialization path. Lower priority because v0.14.96's deferred-arrival fix makes early-game AD work correctly without it.

---

**GitHub state:** `main` HEAD = `7fdce360` (v0.14.92, pushed Tue 2026-05-05 19:53 local, message length: huge). Local working tree at v0.14.96 (FOUR versions ahead, BAT-validated, ready to push). Aaron uses `Utilities/push_to_github.bat` for pushes; consolidated commit message for v0.14.93 + v0.14.94 + v0.14.95 + v0.14.96 is provided in this session's transcript.
