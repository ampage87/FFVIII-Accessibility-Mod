**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD on GitHub = v0.15.9.11.3.6** (pushed 2026-05-15, commit `30bc7469`). **Local = v0.15.9.11.3.9** (BAT'd successfully 2026-05-15, ready to push). Four squashed builds atop the pushed HEAD: .11.3.7 ASK polish + log cleanup, .11.3.8 keyboard suppression extension, .11.3.9 ASK race-window fix.

---

## Current state: chase scene closed — v0.15.9.11.3.9 BAT passed, ready to push

**v0.15.9.11.3.9 BAT result (2026-05-15, Aaron):** *"That worked well. I think we can stick with that solution."* The `TRIGGER_DELAY_MS = 0` fix closes the race window cleanly — pressing confirm during Squall's chase-trigger line now lands on the ASK instead of advancing the MES to the chase-start opcode. NVDA's overlapping-speech sequencing was acceptable in practice; the fallback Options A and B documented in the source comment are not needed and remain available if any future regression resurfaces the UX concern.

**v0.15.9.11.3.8 (BAT'd successfully 2026-05-15 ~21:24):** *"I tried mashing buttons during this last run and no battle triggered."* Field log confirms zero `[CBF] PASS` lines across the entire chase. Chase keyboard machinery installed and active throughout (WndProc subclass installed, `ChaseKeyboard ACTIVATED` and `DEACTIVATED` lines per chase field). The extended drop-list (arrows + Ctrl + Enter + Space + Tab + Escape, plus VK_SHIFT in the WndProc layer) successfully suppressed Aaron's intentional button-mashing including action-key glances. The doopen2a `battleyarou` Interactive Object that fired in the v0.15.9.11.3.7 BAT is now fully behind the suppression layer.

**This closes the chase-scene chapter for real this time.** v0.15.9.11.3.6 was the structural close (auto-pilot routing the full chase end-to-end); v0.15.9.11.3.7 polished the ASK presentation and cleaned up log noise; v0.15.9.11.3.8 closed the last keyboard leak. Three squashed builds atop the pushed HEAD; Aaron will push via `Utilities/push_to_github.vbs` next.

**v0.15.9.11.3.7 (BAT'd successfully apart from the Ctrl leak):**

*ASK polish (`src/chase_ask_overlay.cpp`):* reorder options from {Manual, Auto, Original} to {Auto, Manual, Original} — most-to-least support — with Auto as the default cursor position (protects button-mashers from committing the harder option). Original description rewritten from `"vanilla chase, no mod help"` to `"vanilla, robot keeps getting up to pursue"` to convey the X-ATM092 rise-and-pursue mechanic. `ANSWER_AUTO`/`ANSWER_MANUAL` constants swap values (1 ↔ 2); `CommitChoice` switch body and INI persistence (stores mode by name) both unaffected.

*Log diagnostic cleanup:* review of the v0.15.9.11.3.6 BAT log set (8.30 MB total) found four sources of stale instrumentation that had served their research purpose and were dominating the post-chase logs. All retired or throttled in this build:
- `src/world_map.cpp::BuildDistanceCatalog` — `[DEFER]` log throttled to one line per defer cycle (was 1.79 MB of `ff8_world.log` from per-frame logging while in field mode).
- `src/chase_auto_pilot.cpp::LogChaseActiveDiagnostic` — early-return retirement (v0.15.9.3 camera-orientation research; research is complete).
- `src/chase_auto_pilot.cpp::LogBridgeDiagnostic` — early-return retirement (v0.15.9.8.2 kani-slot identification; v0.15.9.8.3 shipped the override). Bridge dance state-transition logs unchanged.
- `src/chase_auto_pilot.cpp` per-second `tick=60` logger — idle-sample suppression (was 74 identical lines during the disc00_07h FMV with party frozen).
- `src/nav_log.cpp::CoordSample` — `(fieldName, triIdx)` debounce (was 4.66 MB of `ff8_nav_data.log` from triangle-boundary flicker; audit confirmed no checked-in consumer reads the file).

BAT plan: trigger chase, verify three ASK paths (confirm-without-navigate → Auto, arrow-down once → Manual, arrow-down twice → Original). Verify in log tails that `ff8_world.log` is small, `ff8_field.log` is mostly transition events (no `ChaseActiveDiag` or `BridgeDiag` lines, no identical-tick=60 runs during FMV), and `ff8_nav_data.log` has no boundary-flicker repeats. Risk: very low. Behavior of the mod itself is unchanged; every cleanup edit is pure log volume reduction.

**v0.15.9.11.3.6 (pushed):**

v0.15.9.11.3.6 shipped 2026-05-14 and pushed 2026-05-15. Aaron arrow-mashed through every chase field and could not interrupt the auto-drive; full route announced naturally — MH-5 → MH-6 → MH-3 → MH-7 → MH-1 bridge → Town Square 1 → **Town Square 5 (`doopen2a`, the previously-failing field)** → Town Square 10 → disc00_06h FMV → Town Square 8 → Town Square 6 → **disc00_07h Lapin Beach chase-climax FMV played all 8 cues across 74 seconds to natural completion**. The 11→0 catch reduction first achieved hands-off in v0.15.9.8.3 now extends to arrow-mashing gameplay. Empirically verified in `Logs/ff8_field.log`: zero `[CBF] PASS` lines across the run and ChaseKeyboard activated/deactivated cleanly on each chase field.

All four chase scene items closed. The next session picks from the post-chase backlog (see below).

### What v0.15.9.11.3.6 shipped — Path B (WndProc subclass)

New module `src/chase_wndproc.cpp` with `EnsureInstalled()` that enumerates top-level visible windows owned by FF8_EN.exe (`EnumWindows` + PID filter + visibility + WS_CHILD reject) and `SetWindowLongPtrW(GWLP_WNDPROC)` subclasses each. The subclass runs on the message-pump thread, same thread as the chase script's catch evaluator.

When `ChaseKeyboard::IsActive()` AND `msg ∈ {WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP}` AND `wParam ∈ {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}`: return 0 without forwarding. FF8's WndProc and its `[+0xb48]` dispatch never see the message. Everything else forwards via `CallWindowProcW/A` so non-chase behavior is bit-for-bit identical.

Lazy install via `ChaseKeyboard::Activate() → ChaseWndProc::EnsureInstalled()` (idempotent thereafter, with the EnsureInstalled call before `s_active = true` to eliminate the one-frame race window). Permanent install — never uninstalled mid-gameplay. Outside chase Auto the subclass is a cheap pass-through (one volatile bool read, false short-circuit). Install line logs via `Log::Field` to `ff8_field.log`.

**Not WH_KEYBOARD_LL.** The .11.1/.11.2 attempts with WH_KEYBOARD_LL broke chase auto-pilot timing because the hook forces every SendInput to synchronously round-trip through the main thread's pump. WndProc subclass runs inline in the existing pump.

---

## Chase keyboard suppression architecture (final, v0.15.9.11.3.6)

**Four** coordinated hooks blocking Aaron's physical key presses from reaching FF8 during chase Auto:

1. **DirectInputCreateA chain** (v0.15.9.11.3.1) — `dinput8.cpp` loads `dinput.dll` at DllMain, MinHooks `DirectInputCreateA`. The hook vtable-hooks `IDirectInputA::CreateDevice` (vtable[3]) on the returned interface. CreateDevice's hook forwards the IDirectInputDeviceA* (binary-compatible with IDirectInputDevice8A* through GetDeviceState) to `chase_keyboard::OnDeviceCreated`.

2. **GetDeviceState vtable detour** (v0.15.9.11.3, completed by .11.3.1) — `chase_keyboard.cpp` vtable-hooks `GetDeviceState` (vtable[9]) on the IDirectInputDeviceA returned for `GUID_SysKeyboard`. When `s_active`, returns the mod-owned 256-byte synthetic buffer; otherwise pass-through. `chase_auto_pilot` calls `ChaseKeyboard::Activate()` at Engage / `Deactivate()` at Disengage. `field_nav_autodrive.inl::InjectKey` writes auto-pilot arrows + W via `SetScancodeDown`/`SetScancodeUp`. Since .11.3.4, when `IsActive()` is true `InjectKey` skips `SendInput` entirely — synthetic buffer is the sole delivery path during chase Auto.

3. **GetAsyncKeyState hook** (v0.15.9.11.3.2) — closes the non-DirectInput leak path. `user32.dll::GetAsyncKeyState` MinHooked at DllMain. During `ChaseKeyboard::IsActive()`, arrow VKs return 0; everything else passes through.

4. **WndProc subclass** (v0.15.9.11.3.6) — `chase_wndproc.cpp::EnsureInstalled()` enumerates FF8's top-level visible windows at first chase Activate and installs subclass via `SetWindowLongPtrW`. During `ChaseKeyboard::IsActive()`, arrow `WM_KEYDOWN/KEYUP/SYSKEYDOWN/SYSKEYUP` messages return 0 without forwarding. Closes the `[+0xb48]` dispatch path inside FF8's WndProc.

Together these four make every keyboard delivery path to FF8 return "no arrow pressed" for the duration of `ChaseKeyboard::IsActive()`. Empirically verified by v0.15.9.11.3.6 BAT.

### Failed approaches (kept brief for context)

- **v0.15.9.11 / .11.1 / .11.2** — WH_KEYBOARD_LL hook; the synchronous message-pump round-trip per SendInput broke auto-pilot timing on doopen2a. WH_KEYBOARD_LL is unsuitable when the mod also SendInputs from a non-main thread.
- **v0.15.9.11.3** — synthetic buffer + DI8 CreateDevice hook only; FF8 uses DI7 so the hook never fired for the keyboard (fixed in .11.3.1).
- **v0.15.9.11.3.4 / .11.3.5** — synthetic-buffer-only delivery + leak-probe removal; still caught on doopen2a, proving the disruption wasn't DI/GetAsyncKey/probe. The disassembly walk that followed revealed the `[+0xb48]` WndProc dispatch as the remaining vector, motivating .11.3.6.

---

## Chase scene status — ALL COMPLETE

- [x] **#4** ASK dialog text (v0.15.9.9, BAT-success, pushed)
- [x] **#1** auto-pilot self-sufficient (v0.15.9.9, BAT-success, pushed)
- [x] **post-BAT cleanup** duplicate "Let's go!" (v0.15.9.9.1, BAT-success, pushed)
- [x] **#2** MODE_ORIGINAL (v0.15.9.10, BAT-success, pushed)
- [x] **#3** keyboard suppression during Auto chase (v0.15.9.11.3.6, BAT-success 2026-05-14, **pushed 2026-05-15**)

---

## Key chase config

**Per-field configs** in `chase_auto_pilot.cpp::kFieldConfigs[]`:

- `domt4_1`: MODE_DIRECTION RUN south-east (v0.15.9.4)
- `domt3_2`: MODE_DIRECTION RUN east (v0.15.9.5)
- `domt5_1`: MODE_STAGED_DIRECTION walk SW→S→SE (v0.15.9.7.8 extended-key fix landed walking)
- `domt1_1`: MODE_BRIDGE_DANCE east/west state machine on kani X-velocity (v0.15.9.8.3 — 0 catches)
- `doopen2a`: MODE_TARGET south to (-952, -3800) (v0.15.9.8.1)
- `dotown_2`, `dotown_1`: MODE_DIRECTION RUN south
- All other chase fields: generic fallback via `BuildFallbackConfig` (three-tier: INF gateway → trigger line → cluster)

---

## Backlog (post-chase, in rough priority order)

1. **Cleanup vestigial `WALK_REPRESS_PERIOD` state** in `field_nav_directiondrive.inl` — constants + counters still present from v0.15.9.7.x but unreferenced. Small, mechanical, zero risk.
2. **BridgeDiag verbosity trim** — 10Hz per-sample BridgeDance + all-slots dump on `domt1_1` is noise now that the bridge dance is proven (v0.15.9.8.3). Trim to transition-only events.
3. **`deploy.bat` "Version: SINGLE-PRONGED" regex** — cosmetic regression from v0.15.9.3. Deploy log prints `Version: SINGLE-PRONGED` instead of the actual version string. Hunt the regex in `src/deploy.bat`.
4. **X-ATM092 battle-name fix** — battle TTS announces "X-ATM 6" instead of "X-ATM092". v0.15.9.11.3.6 BAT confirmed in `ff8_battle.log`: TTS string constructed as literal `"X-ATM?6?"`, indicating the kernel.bin enemy-name decoder is missing mappings for FF8's stylized small-form digits. Two fix paths: extend the character map with the missing byte mappings (right fix; needs ~30 min investigation), or hardcoded enemy-ID→ASCII name table like the Blue Magic 0x92/0xAA pattern (band-aid). Affects any enemy whose name uses FF8's stylized characters, not just X-ATM092.
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
