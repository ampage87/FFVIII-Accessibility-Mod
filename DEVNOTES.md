**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD on GitHub = v0.15.10.0** (pushed 2026-05-15 05:22 UTC, commit `d198b947`, parent `9c8af9c3`). **Local HEAD = v0.15.10.1** in progress — `deploy.bat` regex regression fix queued, BAT pending.

---

## Current state: v0.15.10.1 in progress (regex regression fix)

**v0.15.10.1 (local, not yet built):** Backlog #3 picked. Fix the `deploy.bat` regex regression that prints `Version: SINGLE-PRONGED` in build logs instead of the real version string. Root cause: `findstr /C:"#define FF8OPC_VERSION "` matches all lines containing that literal substring — line 12 (the real macro) AND line 63 (the historical v0.15.3 comment, which embedded the literal pattern while documenting its own fix). For-loop's last-iteration-wins puts token 3 of line 63 (`SINGLE-PRONGED`) into `VERSION`. Fix: add `/B` (begin-of-line anchor) so only line 12 matches; comment-line indentation (`  // ...`) is no longer false-positive. One-flag change + comment block rewrite in `src/deploy.bat`. Risk: trivial.

---

## Recently shipped

### v0.15.10.0 (pushed 2026-05-15, BAT-confirmed)

First post-chase backlog work. Retired the v0.10.08 standalone decoder (`DecodeFF8Char` deleted; `DecodeFF8String` rewritten as a thin SEH-safe wrapper around the canonical `FF8TextDecode::Decode` from `ff8_text_decode.cpp`). Bug was off-by-0x03 in the digit range (`0x24-0x2D` instead of `0x21-0x2A`) — marked "estimated, not yet confirmed" in the v0.10.07 comment since 2024. Public function signature preserved so all five battle-module call sites kept working unchanged.

BAT evidence (2026-05-14 build, encounter ID 33 + X-ATM092 fight):
- Target fix: `[TARGET] Entry announce: X-ATM092 (mask=0x08 scope=3)` — clean digits "092", no `?`, no `6`.
- Regression check (G-Soldier pack): `[NAME-CACHE] slot3 = "G-Soldier 1" (base="G-Soldier")` — hyphen + base name + disambiguation suffix all intact.
- DLL identified itself as `Initialized v0.15.10.0` confirming the right binary deployed.

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

1. **Cleanup vestigial `WALK_REPRESS_PERIOD` state** in `field_nav_directiondrive.inl` — constants + counters still present from v0.15.9.7.x but unreferenced. Small, mechanical, zero risk.
2. **BridgeDiag verbosity trim** — 10Hz per-sample BridgeDance + all-slots dump on `domt1_1` is noise now that the bridge dance is proven (v0.15.9.8.3). Trim to transition-only events.
3. **`deploy.bat` "Version: SINGLE-PRONGED" regex — IN PROGRESS v0.15.10.1.**
4. **Fully unify all three FF8 text decoders** — v0.15.10.0 retired the v0.10.08 decoder but a third one remains: `DecodeFF8TextPreview` in `battle_tts_victory.inl`. Same wrong digit range as the retired one (so item names with digits broken the same way), plus a slightly different mismap for 0x06, BUT it does have 0xE8-0xFF compression-sequence coverage. Call sites are mostly diagnostic battle-text logging plus item-name announcements during victory phases. Items rarely contain digits so practical impact is low, but the architectural goal of single source of truth isn't met until this is migrated too. There's also a small inline ability-name decoder in `HookedBtCandidate8` (sub_47E710 ability name hook in victory.inl) that already uses the correct 0x21-0x2A digit range but has its own incomplete punctuation table; ideally fold it into `FF8TextDecode::Decode` as well. Risk: medium (touches victory phase machinery; the diagnostic logging paths are noisy but the item announce path is player-facing). Recommended approach: same SEH-split pattern from v0.15.10.0; migrate `DecodeFF8TextPreview` callers one cluster at a time (logging hooks first as low-risk, then item announce path, then the inline ability decoder).
5. **Generalized countdown-timer hook** — Dollet 30-min countdown is TTS'd via a chase-specific path; generalize for future timers.
6. **Remove party members from field entity catalog** — Squall/Zell/Selphie appear as targetable entities; filter them out.

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
- `deploy.bat` version-extract regex needs `/B` anchor (v0.15.10.1) — without it, historical `#define FF8OPC_VERSION` mentions in comments cause findstr to match the wrong line.
