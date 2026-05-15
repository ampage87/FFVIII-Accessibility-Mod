**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.10.1** (pushed 2026-05-15 21:38 UTC, commit `e934484e`, parent `d198b947`). Local tree at v0.15.10.2, BAT-confirmed, awaiting push.

---

## Current state: v0.15.10.2 BAT-confirmed, ready to push

Three-item cleanup pass shipped in one build. Build successful in 17s (16:24:53 → 16:25:10). All three BAT signals green: DLL init banner reads `Version: 0.15.10.2` + `Build: May 15 2026 16:24:54` with no parenthesized hard-coded date; ChaseAutoPilot Initialize log ends with the kani-slot override sentence and no "BridgeDiag still active" trailer; no compile errors.

First BAT attempt at 16:18 failed with a single C2065 error at `nav_log.cpp:101` — pre-flight grep missed a second `FF8OPC_VERSION_DATE` reference in the persistent TSV writer `SessionStart`. One-line fix (drop the date column), re-run, clean. Lesson logged in CHANGELOG (2): when removing a macro, grep the entire `src/` tree, not just the obvious banner-bearing files.

After push, Aaron picks the next backlog item; top remaining pick is the medium-risk text-decoder unification (was item #4, now item #1).

---

## Recently shipped

### v0.15.10.2 (built and BAT-confirmed 2026-05-15, awaiting push)

Three-item cleanup pass combining items #1/#2/#3 from the v0.15.10.1-era backlog. Pure dead-code/dead-data removal + log-line removal; no runtime behavior change.

- **(1) WALK_REPRESS_PERIOD cleanup** in `src/field_nav_directiondrive.inl`: the v0.15.9.7.1 defensive W re-press path was retired in v0.15.9.7.8; the constants + counters + multi-paragraph historical narrative were left as vestigial documentation. Now removed; replaced with a short ~10-line note above `RUN_ANALOG_MAGNITUDE` about the still-current `extended=false` convention.
- **(2) Stale FF8OPC_VERSION_DATE removal**: macro deleted from `src/ff8_accessibility.h`; banner in `src/dinput8.cpp` shortened to `Version: %s`; second use in `src/nav_log.cpp` `SessionStart` (TSV writer to `ff8_nav_data.log`) also dropped (column removed from the SESSION row). The nav_log reference was missed in pre-flight grep and caught by the first BAT attempt's C2065 error; one-line fix, second BAT clean.
- **(3) BridgeDance log verbosity trim** in `src/chase_auto_pilot.cpp`: removed the 10Hz per-sample `BridgeDance: sample state=...` log inside `UpdateBridgeDance`, the entire `LogBridgeDiagnostic` function + its header comment block (had been early-returned in v0.15.9.11.3.7 with ~120 lines of dead body), the call site in `Update()`, the `s_bridgeDiagTick` counter + its initializers in `Initialize()` and `Disengage()`, and the now-misleading "BridgeDiag still active for empirical confirmation" trailing sentence in the Initialize log message. All transition logs (EAST→WEST / WEST→EAST / WEST→EAST TIMEOUT / leap STARTED) and the `kani read FAILED` failure-mode log stay in place.

BAT evidence (2026-05-15 16:25 build):
- `build_latest.log` top: `Building FF8 Original PC Accessibility Mod Version 0.15.10.2`. Deployment Complete block: `Version: 0.15.10.2`.
- `ff8_mod.log` init banner: `Version: 0.15.10.2` then `Build:   May 15 2026 16:24:54` on consecutive lines — no parenthesized hard-coded date between them.
- `ff8_mod.log` chase init: "BridgeDiag still active for empirical confirmation." string verified absent via edit_file dryRun grep; "kani-slot override on domt1_1 -> Others slot 3 (SYM 'laguna')." verified present.

### v0.15.10.1 (pushed 2026-05-15, BAT-confirmed)

Fix the `deploy.bat` regex regression that has printed `Version: SINGLE-PRONGED` in every build log since v0.15.3. Cosmetic-only — the deployed DLL has always carried the real version from `FF8OPC_VERSION`; only the deploy script's text output was wrong.

Root cause: `findstr /C:"#define FF8OPC_VERSION "` matched not only line 12 of the header (the real macro) but also line 63 — the historical v0.15.3 comment block, which embedded the literal substring `#define FF8OPC_VERSION` while documenting the v0.15.3 fix. For-loop's last-iteration-wins put token 3 of line 63 into `VERSION`: tokens `1=//, 2=v0.15.3:, 3=SINGLE-PRONGED` (from `// v0.15.3: SINGLE-PRONGED CLEANUP...`). Beautifully ironic — the v0.15.3 entry's own meta-commentary about its findstr-tightening fix is what re-broke the same regex.

Fix: add `/B` (begin-of-line anchor) to findstr. The real `#define` is at column 0; all historical mentions are indented `  // ...`.

BAT evidence (2026-05-15 build):
- `build_latest.log` top: `Building FF8 Original PC Accessibility Mod Version 0.15.10.1`.
- Deployment Complete block: `Version: 0.15.10.1`. No `SINGLE-PRONGED` anywhere.
- `ff8_mod.log` init banner: `=== FF8 Accessibility Mod v0.15.10.1 — Log opened 2026-05-15 15:35:54 ===` and `AccessibilityThread: Starting main loop (v0.15.10.1).` across all subsystems.

### v0.15.10.0 (pushed 2026-05-15, BAT-confirmed)

First post-chase backlog work. Retired the v0.10.08 standalone decoder (`DecodeFF8Char` deleted; `DecodeFF8String` rewritten as a thin SEH-safe wrapper around the canonical `FF8TextDecode::Decode` from `ff8_text_decode.cpp`). Bug was off-by-0x03 in the digit range (`0x24-0x2D` instead of `0x21-0x2A`) — marked "estimated, not yet confirmed" in the v0.10.07 comment since 2024. Public function signature preserved so all five battle-module call sites kept working unchanged.

BAT evidence: `[TARGET] Entry announce: X-ATM092` clean (no `?`, no `6`). Regression check on G-Soldier pack: `[NAME-CACHE] slot3 = "G-Soldier 1" (base="G-Soldier")` intact with hyphen + disambiguation suffixes.

### Chase chapter (closed v0.15.9.11.3.9, pushed 2026-05-15)

v0.15.0 through v0.15.9.11.3.9 closed the X-ATM092 chase scene accessibility chapter. Aaron's BAT confirmations:

- v0.15.9.11.3.6 (auto-pilot routing the full chase end-to-end, 0 catches): empirical 11→0 catch reduction across `domt4_1 → domt3_2 → domt5_1 → domt2_1 → domt1_1 → doopen2a → dotown_3 → dotown_2 → dotown_1 → disc00_07h FMV`.
- v0.15.9.11.3.8 (keyboard suppression extended beyond arrows): *"I tried mashing buttons during this last run and no battle triggered."*
- v0.15.9.11.3.9 (ASK race-window fix): *"That worked well. I think we can stick with that solution."*

Key architecture summary (full detail in `chase_keyboard.cpp` / `chase_wndproc.cpp` comments and CHANGELOG entries):

- **Four coordinated keyboard hooks** block Aaron's physical key presses from reaching FF8 during chase Auto: `DirectInputCreateA` chain → `IDirectInputDevice::GetDeviceState` vtable detour returning a synthetic 256-byte buffer (v0.15.9.11.3.1); `user32::GetAsyncKeyState` MinHook (v0.15.9.11.3.2); `WndProc` subclass dropping arrow WM_KEY* messages (v0.15.9.11.3.6); drop list extended to arrows + Ctrl + Enter + Space + Tab + Escape + Shift in v0.15.9.11.3.8. WH_KEYBOARD_LL was tried first in .11.1/.11.2 and abandoned because it forces synchronous main-thread pumps per SendInput, breaking auto-pilot timing.
- **AUTO battle-suppressor cap stays `INT_MAX`.** Aaron's 2026-05-13 directive: fix the input layer, don't band-aid the catch.
- **Per-field auto-pilot configs** in `chase_auto_pilot.cpp::kFieldConfigs[]`: `domt4_1` MODE_DIRECTION RUN south-east; `domt3_2` MODE_DIRECTION RUN east; `domt5_1` MODE_STAGED_DIRECTION walk SW→S→SE; `domt1_1` MODE_BRIDGE_DANCE east/west state machine on kani X-velocity; `doopen2a` MODE_TARGET south to (-952, -3800); `dotown_2`/`dotown_1` MODE_DIRECTION RUN south. Other chase fields use the generic `BuildFallbackConfig` three-tier (INF gateway → trigger line → cluster).

---

## Backlog (in rough priority order)

1. **Fully unify all three FF8 text decoders** — v0.15.10.0 retired the v0.10.08 decoder but a third one remains: `DecodeFF8TextPreview` in `battle_tts_victory.inl`. Same wrong digit range as the retired one (so item names with digits broken the same way), plus a slightly different mismap for 0x06, BUT it does have 0xE8-0xFF compression-sequence coverage. Call sites are mostly diagnostic battle-text logging plus item-name announcements during victory phases. Items rarely contain digits so practical impact is low, but the architectural goal of single source of truth isn't met until this is migrated too. There's also a small inline ability-name decoder in `HookedBtCandidate8` (sub_47E710 ability name hook in victory.inl) that already uses the correct 0x21-0x2A digit range but has its own incomplete punctuation table; ideally fold it into `FF8TextDecode::Decode` as well. Risk: medium (touches victory phase machinery; the diagnostic logging paths are noisy but the item announce path is player-facing). Recommended approach: same SEH-split pattern from v0.15.10.0; migrate `DecodeFF8TextPreview` callers one cluster at a time (logging hooks first as low-risk, then item announce path, then the inline ability decoder).
2. **Generalized countdown-timer hook** — Dollet 30-min countdown is TTS'd via a chase-specific path; generalize for future timers.
3. **Remove party members from field entity catalog** — Squall/Zell/Selphie appear as targetable entities; filter them out.

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive: the fix is the input layer, not the band-aid. Cap stays `INT_MAX`. v0.15.9.11.3.6 BAT vindicates the call.

### Deferred (don't pick without explicit Aaron direction)

- SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` is wrong section size)
- Walk-and-talk dialog gap (hardcoded engine path)
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) + planner-fallback (#29)
- chase_diag::OnAskOpcodeFired snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression (fix in DeactivateFreeze before clearing agent state)

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at start of every session
- Update both at TWO checkpoints: every version bump AND after every BAT result
- **Filesystem MCP for all Windows project files** — bash runs in a Linux container that can't reach the Windows mod directory
- **Aaron pushes via `Utilities/push_to_github.vbs`**, Claude NEVER pushes
- **Build/BAT cycle**: Aaron runs `deploy.vbs`. "BAT" = built and tested → read `Logs/build_latest.log` tail then domain log (field/mod/dialog/battle/menu/world)
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)` to prevent Alt+Fx interception
- F12 reserved for per-session diagnostics (search source for existing F12 refs first and REMOVE before re-binding)
- **NEVER re-enable SET3 hook** (CI guard in `.github/workflows/safety-checks.yml`)
- DEVNOTES under 10KB — move older history to DEVNOTES_HISTORY.md
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1) — without it, historical `#define FF8OPC_VERSION` mentions in comments cause findstr to match the wrong line.
