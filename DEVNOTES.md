**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.9.10** (pushed 2026-05-13). Local tree is at **v0.15.9.11.3.6 — BAT-SUCCESS, ready to push** — nine unpushed versions ahead.

---

## Current state: v0.15.9.11.3.6 — BAT-SUCCESS (WndProc subclass) ✅

Aaron's 2026-05-14 BAT: arrow-mashed through the chase fields and never managed to interrupt the auto-drive. Mod log confirms the full route announced naturally — MH-5 → MH-6 → MH-3 → MH-7 → MH-1 bridge → Town Square 1 → **Town Square 5 (`doopen2a`, the previously-failing field)** → Town Square 10 → disc00_06h FMV → Town Square 8 → Town Square 6 → **disc00_07h Lapin Beach chase-climax FMV played all 8 cues across 74 seconds to natural completion**. Aaron then exited normally. The 11→0 catch reduction first achieved hands-off in v0.15.9.8.3 now extends to arrow-mashing gameplay.

**Chase scene item #3 (keyboard suppression during Auto chase) is DONE.** Push the squash and move to the post-chase backlog.

### What shipped in v0.15.9.11.3.6 — Path B (WndProc subclass)

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

## Chase scene status

- [x] **#4** ASK dialog text (v0.15.9.9, BAT-success, pushed)
- [x] **#1** auto-pilot self-sufficient (v0.15.9.9, BAT-success, pushed)
- [x] **post-BAT cleanup** duplicate "Let's go!" (v0.15.9.9.1, BAT-success, pushed)
- [x] **#2** MODE_ORIGINAL (v0.15.9.10, BAT-success, **pending push**)
- [x] **#3** keyboard suppression during Auto chase (v0.15.9.11.3.6, BAT-success 2026-05-14, **pending push**)

All four chase scene items closed. Push the squash of v0.15.9.10 → v0.15.9.11.3.6 (`Utilities/push_to_github.vbs`).

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

## Backlog (now post-chase)

1. **Push pending versions** — Aaron runs `Utilities/push_to_github.vbs` to squash v0.15.9.10 → v0.15.9.11.3.6 into one commit using v0.15.9.11.3.6 CHANGELOG entry.
2. **Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive: the fix is the input layer, not the band-aid. Cap stays `INT_MAX`.
3. **Cleanup vestigial `WALK_REPRESS_PERIOD` state** in `field_nav_directiondrive.inl` (constants + counters still present from v0.15.9.7.x but unreferenced).
4. **BridgeDiag verbosity trim** — 10Hz per-sample BridgeDance + all-slots dump on domt1_1 is noise now that the dance is proven. Trim to transition-only events.
5. **`deploy.bat` "Version: SINGLE-PRONGED" regex** (v0.15.9.3 cosmetic regression, unfixed).

### Standalone

- X-ATM092 battle-name fix
- Generalized countdown-timer hook
- Remove party members from field entity catalog

### Deferred

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
