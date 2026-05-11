**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.8.1** (pushed 2026-05-10). **Local tree: v0.15.9.2.15 BAT MAJOR PROGRESS 2026-05-11, ready to push.** v0.15.9 through v0.15.9.2.14 all folded into the comment trail; not pushed.

---

## Current state: v0.15.9.2.15 BAT MAJOR PROGRESS -- significantly farther than any previous build

For the next session see `NEXT_SESSION_PROMPT.md`.

### v0.15.9.2.15 BAT findings: significantly farther than before, but chase not complete

BAT'd 2026-05-11 14:00-14:05. Aaron loaded a save just before the chase trigger, walked into the chase scene, committed Auto in the chase ASK at 14:03:02, then went hands-off. Field log sequence:

- 14:03:02 -- `ChaseDetector: chase_mode set to 'auto' (persisted)` (Aaron commits Auto)
- 14:03:18 -- field announce: MH-3 (`domt3_2`)
- 14:03:24 -- field announce: MH-7 (`domt5_1`)
- 14:04:06 -- field announce: MH-1
- 14:04:22 -- field announce: Town Square 1
- 14:04:33 -- field announce: Town Square 5
- 14:04:48 -- field announce: Town Square 10 = `dotown_3`
- 14:04:49 -- `chase DEACTIVATED on entry to 'dotown_3' (non-chase field). CHASE-END SUMMARY mode=auto battles_fired=0 battles_suppressed=11`

**The mod's `CHASE-END SUMMARY` is misleading.** It fires when ChaseDetector sees a field not in its chase-fields list, not when the in-game chase ends. The actual in-game chase ends only when the Lapin Beach FMV plays (robot chasing Squall on the beach). There are 2-3 more fields after dotown_3 before that FMV fires. v0.15.9.2.15's auto-pilot disengaged on dotown_3 because the detector flipped state, so the final fields were not driven.

**11 battles suppressed indicates slow steering, not success.** The chase is designed so a skilled player can outrun X-ATM092 without any battles firing. Each battle call is the robot catching up; cap=0 NO-OPs the battle but the catch happened. Steering speed refinement is a deferred priority: get the full chase working first, then tune for zero catches.

What v0.15.9.2.15 DID achieve, beyond all previous builds:
- Multi-field chase auto-driving via the generic INF-gateway fallback. Each chase field's INF gateway found, direction-test selected the forward-progress one, chase-drive aimed at it, crossing detected, field transition fired.
- Domt5_1 (west trail) and several subsequent chase fields traversed without intervention.
- Survived doopen2a (bridge area, presumably one of Town Square 1 / 5) without the planned PJUMPA + AI rule #2 reverse-direction state machine.

The INF gateway insight from v0.15.9.2.14's failure: SETLINE Line entities can be Event Trigger (kani battle calls), not Screen Boundary, on chase fields like domt2_1. INF gateways are the engine's actual screen-transition mechanism. `GatewayInfo` gained `lineX1/Y1/lineX2/Y2` endpoint fields; `GetGatewayNearestCluster` picks the gateway whose direction-from-player aligns with the player->cluster vector (dot product > 0). `StartChaseDrive` accepts explicit cross-line endpoints; `BuildFallbackConfig` uses three-tier preference: INF gateway -> trigger line -> largest cluster center.

### Files changed in v0.15.9.2.15

- `src/field_archive.h`, `src/field_archive.cpp` -- `GatewayInfo` gains line endpoint fields; `LoadINFGateways` stores them.
- `src/field_navigation.h`, `src/field_navigation.cpp` -- new public `GetGatewayNearestCluster` API; new state vars `s_driveCrossLineX1/Y1/X2/Y2`, `s_driveCrossLineActive`.
- `src/field_nav_directiondrive.inl` -- `StartChaseDrive` signature extended with `crossLineX1/Y1/X2/Y2` endpoints.
- `src/field_nav_autodrive.inl` -- crossing block reads from `s_driveCrossLine*` state for chase-drive.
- `src/chase_auto_pilot.cpp` -- `BuildFallbackConfig` three-tier preference (gateway -> trigger line -> cluster); `Engage` passes both trigger index and gateway endpoints.
- `src/ff8_accessibility.h` -- version 0.15.9.2.15.
- `CHANGELOG.md` top entry, push-ready.

---

## Recent history

- **v0.15.9.2.15** -- BAT MAJOR PROGRESS 2026-05-11 14:00-14:05. Multi-field chase auto-driving via INF-gateway fallback. Reached dotown_3 entry where mod's detector deactivated; in-game chase continues 2-3 fields further until Lapin Beach FMV. 11 battles suppressed = steering too slow, deferred refinement.
- **v0.15.9.2.14** -- trigger-line CROSSING detection. SETLINE filter rejected Event Trigger lines on domt2_1.
- **v0.15.9.2.13** -- tightened chase-drive arrive distance 300 -> 60.
- **v0.15.9.2.12** -- flipped fallback walk default to false (running).
- **v0.15.9.2.11** -- per-field completion marker to stop re-engagement loop.
- **v0.15.9.2.10** -- re-added ASK gate (chase auto-pilot wedged input during scripted intro).
- **v0.15.9.2.9** -- velocity-stuck advance + `std::sqrt` replacing IntSqrt.
- **v0.15.9.2.8** -- kani-position diagnostic (refuted collision-push hypothesis).
- **v0.15.9.2.7** -- fixed v0.15.9.2.6 log spam (moved BuildFallbackConfig out of per-tick path).
- **v0.15.9.2.6** -- generic chase-field fallback via largest cluster center.
- **v0.15.9.2.5** -- advance funnel wp on chase-drive no-progress. West trail (domt5_1) end-to-end.
- **v0.15.8.1** -- BAT SUCCESS, pushed 2026-05-10.
- **v0.15.x** trail: see `DEVNOTES_HISTORY.md`.

---

## Backlog

### Push v0.15.9.2.15 (highest priority)

Run `Utilities/push_to_github.vbs`. CHANGELOG top entry already in place with v0.15.9.2.15 push-ready body. Validator will pass. This is the new known-good baseline -- significantly farther than v0.15.8.1.

### Next development priorities

1. **Extend chase-fields list past dotown_3 to the Lapin Beach FMV.** The mod's ChaseDetector currently treats dotown_3 as a non-chase field, deactivating the auto-pilot. The in-game chase isn't over -- 2-3 more fields, then the Lapin Beach FMV terminates the scripted chase. Identify those fields (field log + game-script trace), add to ChaseDetector's chase-fields list, BAT to confirm the auto-pilot keeps driving through them. This is the work to actually complete the chase auto-pilot feature.
2. **Steering-speed refinement.** Once the full chase is working, address the 11-catches problem. Possibilities include: faster keyboard wake-up cadence, smarter analog dead-zone tuning, anticipatory turning ahead of corridor bends (vs reactive funnel-waypoint advance), per-field calibrated walk/run choice.
3. **v0.15.10 -- Original = chase-mod-active flag.** Currently ANSWER_ORIGINAL falls back to MODE_MANUAL. v0.15.10 implements vanilla-engine chase behavior (no cap, no auto-pilot, scripted X-ATM092 battles as designed).
4. **F9 path-finding cleanup.** Apply v0.15.9.2.4's kb-from-analog and v0.15.9.2.5's advance-on-stuck to F9 path-finding by dropping the `s_chaseDriveActive`-only gates.
5. **doopen2a bridge state machine (CONDITIONAL).** v0.15.9.4 originally planned PJUMPA hook + AI rule #2. v0.15.9.2.15 traversed the bridge area without it; possibly still needed for consistency. Re-evaluate after multi-replay verification.
6. **v0.15.x cleanup** -- remove `Phase2_TestAsk` + Shift+F12 diagnostic.

### Standalone

- X-ATM092 battle-name fix.
- Generalized countdown-timer hook.
- Cleanup: dead `Hook_field_get_dialog_string` override branch.

### Deferred

- `chase_diag::OnAskOpcodeFired` snprintf bug.
- Remove party members from entity catalog.
- SeeD rank bug #27, walk-and-talk dialog gap.
- X-ATM092 chase audio descriptions during the chase.
- Refined-coord narrow-gate steering.
- Fire Cavern entry (#28) + planner-fallback (#29).
- Cosmetic rename: `chase_kani_freeze` -> `chase_agent_pin`.

---

## Key infrastructure (reference)

**Chase auto-pilot per-field config** (`src/chase_auto_pilot.cpp`):

```cpp
enum FieldDriveMode { MODE_DIRECTION, MODE_TARGET };
struct FieldConfig {
    const char*    fieldName;
    FieldDriveMode mode;
    int8_t         dirX, dirY;
    int32_t        targetX, targetY;
    bool           walk;
};
```

- `domt4_1`: `MODE_DIRECTION` RUN LEFT (dirX=-1, dirY=0, walk=false). Explicit config.
- `domt5_1`: `MODE_TARGET` (382, 235), walk=true. Explicit config.
- All other chase fields: generic fallback via `BuildFallbackConfig`. Three-tier preference:
  1. `GetGatewayNearestCluster` -- INF gateway aligned with player->cluster direction (cross-product test).
  2. `GetTriggerLineNearestCluster` -- SETLINE Screen Boundary/Unknown lines (rejects Event Trigger).
  3. `GetLargestClusterCenter` -- largest walkmesh dead-end cluster center.

**Chase-fields list (incomplete).** ChaseDetector's chase-fields list does not extend through to the Lapin Beach FMV. dotown_3 deactivates the detector despite the in-game chase still being active. Extending this list is the next priority.

**`CHASE-END SUMMARY` log line is mod-side bookkeeping, not in-game chase end.** It fires on entry to any field not in the chase-fields list. The true in-game chase end is the Lapin Beach FMV.

**Chase-drive API** (`src/field_nav_directiondrive.inl`): `StartChaseDrive(targetX, targetY, walk, crossLineX1, Y1, X2, Y2)` / `StopChaseDrive()` / `IsChaseDriveActive()`. Path-finding A*+funnel via shared F9 infrastructure.

**Chase-drive state**: `s_chaseDriveActive`, `s_chaseDriveWalk`, `s_chaseDriveTargetX/Y`, `s_driveCrossLineX1/Y1/X2/Y2`, `s_driveCrossLineActive`. The cross-line state is read by `UpdateAutoDrive`'s crossing block for chase-drive (no longer dependent on `s_capturedLines` for chase-drive, since gateways aren't SETLINE entities).

**Mutex:** chase-drive vs direction-drive vs F9 path-finding. F9 backslash refuses to cancel chase-drive. Arrow-cancel suppressed during chase-drive.

**Camera calibration** (per field, on first drive): `camRight` = world dir produced by lX=+1000; `camDown` = world dir produced by lY=+1000.

---

## Session ritual

Read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` at START of every session.

Update DEVNOTES + NEXT_SESSION_PROMPT at TWO points: every version bump and after every BAT result.

**Filesystem MCP for all Windows project files.** Bash runs in a Linux container.

**Aaron pushes via `Utilities/push_to_github.vbs`.** Claude NEVER pushes.

**Build/BAT cycle.** Aaron runs `deploy.vbs`. "BAT" = built and tested -> read `Logs/build_latest.log` tail then domain log.

**Keep `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` updated** with new findings as chase / F9 auto-drive work progresses. The v0.15.9.2.15 INF-gateway success deserves a new finding entry (priority for next session if not already added).
