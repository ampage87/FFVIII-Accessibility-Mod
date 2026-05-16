**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.15.13.2** (commit `3d6db2a`). **Local tree = v0.16.0.3** (log-spam cleanup on top of v0.16.0.2 BAT pass), ready for Aaron to push the v0.16.0/v0.16.0.1/v0.16.0.2/v0.16.0.3 chain via `Utilities/push_to_github.ps1`.

---

## Current state: v0.16.0.3 — VEH-POS-FALLBACK log-spam fix on top of v0.16.0.2 BAT pass

v0.16.0.3 is a single-file cleanup: `world_map_segments.inl` logs `[VEH-POS-FALLBACK]` only on `s_lastVehicle` transition (a function-local static tracks the last-logged value). No time-based heartbeat — once a given vehicle byte has been logged, subsequent identical entries stay silent. No functional change.

**v0.16.0.3 BAT — PASSED.** Zero `[VEH-POS-FALLBACK]` lines in 733 log lines (vs ~1800 in 2262 for v0.16.0.2). As a bonus, this BAT exercised the planner-ineligible branch of v0.16.0.2's Fix 1 (Poll() replan-gate): Fire Cavern drive hit a random encounter, re-entered the world map, and Poll() correctly logged `[DRIVE] Planner-ineligible destination -- keeping simple-coord steering, not replanning` before reaching final-approach arrival at dist=66. Both branches of Fix 1 (eligible via Balamb Town, ineligible via Fire Cavern) are now empirically verified. Aaron is ready to push v0.16.0/.0.1/.0.2/.0.3 chain.

### v0.16.0.2 BAT result summary (still the most recent functional BAT)

Log timestamps 15:33:49 (module init) through 15:40:32 (Balamb Town arrival).

**Fix 3 (Initialize hardcode) — VERIFIED at module init:**
```
[INIT] Refined entry default: Balamb Town (12896,-26711)
[INIT] Refined entry default: Fire Cavern (30326,-29221)
```

**Fire Cavern drive — CLEAN ARRIVAL in 7 seconds:**
- Used refined entry (30326,-29221) instead of catalog (36864,-28672).
- `Geometric-trigger destination Fire Cavern (locIdx=37, planner-ineligible) using simple-coord steering`.
- No `[PLAN-DEBUG]` walk.
- Arrival: `dist=66, lastPos=(30260,-29221), planned=0, fieldId=0x0088, fieldName='', elapsed=547ms`.
- Refined coord auto-updated from (30326,-29221) → (30260,-29221) (66 units west, the actual approach-trigger entry point).

**Balamb Town drive — 4 encounter cycles, all resumed correctly via planner:**
- Encounters at dist=12926, 10436, 9351, 5157, 1496 — each `Paused via game-mode (MODE_SWIRL)` → `Replanning after world-map re-entry` → normal `[PLAN-DEBUG]` walk → resume.
- The planner-eligible branch of the Poll() replan-gate fired correctly every time.
- Final arrival: `dist=65, fieldId=0x006A, fieldName='bcgate_1', elapsed=563ms`.
- Refined coord re-captured at (12894,-26776) — 2 units off the prior hardcode.

**Fix 1 (Poll() replan-gate) verification**: the gate's `if (s_drivePlannerEligible) PlanDrivePath else log+keep-simple-coord` ran the planner-eligible branch 4 times for Balamb Town. The planner-ineligible branch did not fire because the Fire Cavern drive arrived too quickly to encounter a battle — but the conditional is structurally exercised and the eligible-side behavior matches expected.

**Fix 2 (two-tier 2500/8000 cap) verification**: structurally in place but unused this BAT. Fire Cavern arrived at dist=66 (well inside strict 2500), Balamb Town arrived at dist=65 (also inside). The 8000 cap is a safety net for future geometric-trigger destinations on their first visit before refined-coord capture.

### Open follow-ups (not blockers)

1. **`fieldName=''` race at Fire Cavern arrival.** 7-second drives beat the field-name pointer's populate timing. fieldId 0x0088 is correct; the Balamb Town arrival 5 minutes later got both `fieldId=0x006A` and `fieldName='bcgate_1'`. Either accept (fieldId is sufficient) or add a brief retry in Part B before logging. Backlog.
2. **VEH-POS-FALLBACK log spam.** v0.16.0.1's guard works correctly (foot DWORDs always preserved), but `s_lastVehicle=33 (VEH_CAR)` latched mid-session and the fallback log line fired ~1800 times in this 7-minute run, dominating ff8_world.log. Functionally fine, diagnostically noisy. Backlog item: rate-limit the log (every 10s or transition-only) AND/OR detect "foot DWORDs are valid and moving" → snap s_lastVehicle back to 0 automatically.
3. **Fire Cavern A fieldId = 0x0088** — new data for the FieldAnnounce display-name catalog audit backlog item (`src/field_display_names.h`). Confirm it announces as "Fire Cavern A" or similar; tidy mapping if wrong.

---

## v0.16.0 baseline (still relevant)

The 222.80 KB / 4452-line `src/world_map.cpp` monolith of v0.15.13.2 is now 10 focused files plus a slim parent. Three behavioral safety nets accompany the structural split:

- **Part B** (arrival.inl): off-target-distance cap on arrival. **Two-tier as of v0.16.0.2**: 2500 for planner-eligible, 8000 for geometric-trigger (planner-ineligible) destinations. Refined-coord capture self-corrects to actual trigger position on first arrival; subsequent visits fall back inside the strict 2500 cap.
- **Part C** (drive.inl): `StartAutoDrive` checks `s_destPlannerEligible[locIdx]` before calling `PlanDrivePath`. Ineligible destinations skip the planner entirely; UpdateAutoDrive's non-planner branch handles them with v0.11.11-era simple-coord steering.
- **Poll() replan-gate** (slim world_map.cpp, v0.16.0.2): on world-map re-entry after random-encounter pause, replan now gated on `s_drivePlannerEligible`. Eligible destinations replan via PlanDrivePath; ineligible destinations stay on simple-coord steering (no closest-active-region fallback misroute).
- **ComputePlannerEligibility** (planner.inl): runs once at Initialize, after LoadTriggerZones. Sets `s_destPlannerEligible[LOCATION_COUNT]`. Logs per-catalog YES/NO classification. 11/38 eligible in master catalog as of v0.16.0 BAT.

CI guard (`.github/workflows/safety-checks.yml`) `source-file-size-check` job: 60 KB soft warning, 80 KB hard fail. Existing oversized files allowlisted with v0.16.x ticket numbers.

### File split layout

`state.inl` first (types/state). Then `segments.inl` (coord math + archive I/O), `trigger_data.inl` (38 wmsetus.obj Section 8 programs + LogTriggerPrograms), `catalog.inl` (`s_locations[]` + LOCATION_COUNT + BFS reachability), `announce.inl`, `planner.inl` (A* + ComputePlannerEligibility), **`drive.inl`** (StopAutoDrive + StartAutoDrive + UpdateAutoDrive), **`arrival.inl`** (ResolveDeferredArrival; AFTER drive.inl because it calls StopAutoDrive), `keys.inl` (PollKeys). Slim `world_map.cpp` opens namespace, includes all 9 .inl files in dependency order, defines `Initialize` / `Update` / `Shutdown` / `Poll`. `world_map_history.h` holds the pulled-out v0.14.31–v0.15.13.2 changelog narrative (NOT in build path, `#if 0` wrapper). `MAX_LOCATIONS = 64` in state.inl decouples state-array sizing from `LOCATION_COUNT`.

---

## Recently shipped (newest first; GitHub HEAD is v0.15.13.2 — v0.16.x not yet pushed)

### v0.16.0.3 — VEH-POS-FALLBACK log transition-only (local, awaiting push)
`world_map_segments.inl` — fallback log now uses a function-local static to fire only when `s_lastVehicle` changes from the last-logged value. No heartbeat. Eliminates ~1800-line floods like v0.16.0.2 BAT produced; one line per distinct vehicle byte that triggers the fallback.

### v0.16.0.2 — Fire Cavern works (local, BAT passed, awaiting push)
Three fixes from v0.16.0.1 BAT log. (1) Poll() replan-gate honors planner-eligibility via new `s_drivePlannerEligible` flag. (2) Part B two-tier cap: 2500 planner-eligible / 8000 geometric-trigger. (3) Fire Cavern refined-coord hardcoded at (30326,-29221) in Initialize() alongside Balamb Town. BAT confirmed Fire Cavern arrives in 7 seconds at dist=66, all three fixes verified.

### v0.16.0.1 — "Position unavailable" fix + Part C locIdx fix (local, awaiting push)
(1) `GetWorldMapPosition_Active` guards vehicle-pos overwrite with `if (vx != 0 || vy != 0)`; stale `s_lastVehicle=37` (VEH_CAR) no longer clobbers valid foot DWORDs. `[VEH-POS-FALLBACK]` diagnostic log added. (2) Part C gate switched from `s_destPlannerEligible[catIdx]` (BFS-filtered) to `[locIdx]` (master-table).

### v0.16.0 — world_map.cpp split + Parts B/C + CI guard (local, awaiting push)
10-file refactor of the 222 KB monolith. Part B distance cap + Part C planner-eligibility gate + ComputePlannerEligibility helper. CI source-file-size-check (60 KB warn / 80 KB fail).

### v0.15.13.2 — commit `3d6db2a`, tag `v0.15.13.2` (pushed 2026-05-15 22:28:49)
Live countdown timer at 0x01CFE92C. Auto-announce, T-key reads, Shift+T freeze. Confirmed on Dollet AND Fire Cavern.

### v0.15.12.0 — commit `b573fd1`
Countdown timer module first introduced. Structural cleanup of `ff8_accessibility.h` + `CHANGELOG.md`.

### Chase chapter (closed at v0.15.9.11.3.9)
X-ATM092 chase accessibility complete. AUTO battle-suppressor cap stays `INT_MAX`.

---

## Backlog (priority order)

1. **v0.16.1**: split `src/chase_auto_pilot.cpp` (108 KB) using the world_map split as the template.
2. **v0.16.2**: split `src/field_dialog.cpp` (88 KB).
3. **v0.16.3**: split `src/field_archive_jsm.inl` (91 KB).
4. **v0.16.4**: split `src/battle_tts_ewm.inl` (90 KB).
5. **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB).
6. **`menu_tts.cpp` T-handler `!shift` gate**. One-line cleanup.
8. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify the mapping for Fire Cavern A (fieldId 0x0088) too — Aaron heard it announced as "Fire Cavern A" but `fieldName=''` came through empty in the arrival log due to a populate-timing race; confirm display works correctly in steady-state.
9. **Field-name populate race** at Part B arrival check — **DIAGNOSTIC LOG ONLY, audio is fine**. `fieldName=''` arrived empty in the log line for the 7-second Fire Cavern drive because the engine's `pCurrentFieldName` string pointer hadn't populated yet at the 547ms mark when Part B took its snapshot. The FieldAnnounce module (separate from world_map) reads the pointer hundreds of ms later and announces correctly — Aaron confirmed in v0.16.0.2 BAT that he heard "Fire Cavern A" announce as expected. Backlog action: either retry briefly in Part B before logging, or accept (fieldId alone is sufficient diagnostically).
10. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.

### Future (deferred)

- **Refined-coord persistence** (JSON or %APPDATA% store so BAT-captured coords survive sessions, replacing per-destination hardcodes).
- **Other geometric-trigger destinations**: as v0.16.0.2's two-tier cap catches them on first arrival, add their refined coords to the Initialize() hardcode chain. Candidates: Centra Ruins, Tomb of the Unknown King, Cactuar Island, Shumi Village, Edea's House, Chocobo Forest entrances.
- **Engine-write hook for cleaner countdown freeze** (cosmetic ±1-sec flicker; not urgent).

### Deferred (don't pick without Aaron's direction)

- SeeD rank bug #27
- Walk-and-talk dialog gap
- Refined-coord narrow-gate steering (#29)
- `chase_diag::OnAskOpcodeFired` snprintf bug

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.

---

## Catalog of known fieldIds for geometric-trigger destinations

(Grown as BATs reveal them; useful for the FieldAnnounce display-name audit.)

- **Fire Cavern A** (approach field, world-map trigger): `fieldId=0x0088`, engine `fieldName='bdview1'`. Trigger position ≈ (30260, -29221), ~6.5k units southwest of catalog icon (36864, -28672). v0.16.0.3 BAT captured the fieldName once the populate race resolved.
- **Balamb Town gate** (planner destination, not geometric): `fieldId=0x006A`, fieldName=`bcgate_1`. Trigger position ≈ (12894, -26776).

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at session start
- Update both at every version bump AND after every BAT result
- **Filesystem MCP for all Windows project files**
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`
- F12 reserved for per-session diagnostics
- **NEVER re-enable SET3 hook** (CI guard)
- DEVNOTES under 10KB
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1)
- **`.inl` textual-include pattern** for source splitting; no `deploy.bat` change needed
- **Inline-changelog accretion is dead** (retired v0.15.12.0)
- **F11 screenshots are gold for BAT context**
- **Diagnostic-feature gating pattern**: gate behind `#define X 0` instead of deleting
- **Source file size limits (v0.16.0 CI guard)**: 60 KB soft warning, 80 KB hard fail. Split before substantive edits cross the warning line.
- **Arrival detection needs VERIFICATION, not just signal-presence.** v0.14.96 fixed encounter false-positives; v0.16.0 Part B fixed off-target-field false-positives; v0.16.0.2 fixed icon-vs-trigger false-negatives via two-tier cap.
- **Empirical-data capture (refined coords) needs the underlying decision VALIDATED before storage.** Bad decisions self-reinforce otherwise (the Fire-Cavern-into-bggate_1 cascade).
- **Geometric-trigger vs script-trigger destinations need different navigation strategies.** The wmsetus planner only handles script-trigger destinations; geometric-trigger destinations (Fire Cavern, early-game Balamb Garden, likely Centra Ruins / Tomb / Cactuar Island) use simple-coord steering + engine terrain trigger via Part C, with the wider Part B cap on first arrival.
- **When "fixing" a planner decline, don't substitute a different region — that's the v0.14.95 mistake.** If the data says there's no scripted path, the right answer is fall back to non-planner logic, not invent a route the data doesn't support.
- **Mid-drive replan must honor the same planner-eligibility gate as initial Start.** v0.16.0.2's s_drivePlannerEligible flag was needed because Poll()'s 2014-era replan code predates Part C and would silently re-introduce the closest-active-region fallback the gate exists to prevent.
- **Two-stage destination entry** (Fire Cavern, possibly other major dungeons): the world-map terrain trigger drops the player into an approach field, not the destination interior. The refined coord for these destinations is the approach-field trigger position, several thousand units offset from the icon. v0.16.0.2 BAT confirmed: Fire Cavern A approach field at (30260, -29221) is ~6.5k units southwest of icon (36864, -28672).
- **GitHub commit history is authoritative for "when did X change" questions.** Memory and DEVNOTES can drift; `list_commits` queries reveal exact regression points.
- Every Claude response starts with `## Claude Says`.
