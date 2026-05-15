# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

## v0.15.9.11.3.9

Fix the chase ASK pre-open race window. Aaron reported that pressing confirm during the ~3-second gap between Squall's "Forget it! Let's go!" MES and the chase-mode ASK opening would advance Squall's MES to the chase-start opcode, bypassing the ASK entirely. Lowering `TRIGGER_DELAY_MS` from 3000 to 0 in `chase_ask_overlay.cpp` closes the race: the deferred-open check now resolves on the next AccessibilityThread tick (~16 ms after `OnDialogText` fires), so the ASK is visible and accepting input before any human reaction time. NVDA's speech queue still serialises the utterances — Squall's line plays, then the ASK prompt, then the default-cursor option label — just without a multi-second silent gap. Default cursor on Auto means a button-mashing player still commits the safest option.

### Why this build

The 3-second defer was introduced in v0.15.1.2 so NVDA could speak Squall's chase-trigger line cleanly before our ASK preempted with its own prompt. It worked for that goal but left a window where the player's reflex-confirm landed on Squall's dialog (not the not-yet-open ASK), and FF8 advanced Squall's MES to the next opcode — the chase start. By the time `Update()` finally opened the ASK, the chase script had already kicked off, `s_askAnswered` was never set on a fresh trigger, and the player was driving manually in whatever mode the INI carried.

### The fix

**`src/chase_ask_overlay.cpp`** — `TRIGGER_DELAY_MS` changed from `3000` to `0`. `OnDialogText` still sets `s_triggerPending` synchronously on the MES detection; `Update()` still polls and calls `OpenAsk` in its own tick. With the delay zeroed, the gap collapses to one AccessibilityThread tick (~16 ms), which beats any human reaction time. The pre-existing 60-second `DialogInject` ASK timeout and the existing chase-end cleanup path are unaffected.

### What changes for the player

A *patient* player now hears Squall's "Forget it! Let's go!" line overlapping with the ASK prompt "X-ATM092 is heading right for you. How do you want to run?" rather than cleanly before it — NVDA queues the two utterances back-to-back. The ASK is still navigable while either is speaking. A *button-mashing* player commits Auto (the default cursor) before hearing any of it, which is the intended behavior since v0.15.9.11.3.7.

### Out of scope

If overlapping speech proves annoying, the two layered fixes documented in the code comment are still available:

- **Option A**: extend `ChaseKeyboard` to suppress confirm keys during the dialog-settle window only (activate at `OnDialogText`, deactivate at `OpenAsk` so the ASK itself stays interactive).
- **Option B**: gate on `ScreenReader::IsSpeaking()` instead of a fixed timer.

### Risk

Very low. One constant edit. Worst case (the ASK and Squall's MES collide in script-VM ways we haven't foreseen): revert to a small nonzero delay like 100 ms that still beats human reaction time, or fall back to Option A.

### BAT plan

Aaron triggers the chase and mashes confirm immediately when Squall's line starts. Expected outcome: the ASK opens essentially instantly, the mashed confirm commits Auto in the ASK, and the chase auto-pilots cleanly through every field as in v0.15.9.11.3.8 — zero `[CBF] PASS` lines anywhere.

### Files

- `src/chase_ask_overlay.cpp` — `TRIGGER_DELAY_MS = 0` plus rationale comment.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — current state + BAT plan.

## v0.15.9.11.3.8

Extend chase Auto keyboard suppression beyond arrow keys. v0.15.9.11.3.7 BAT confirmed the auto-pilot drove the full chase route cleanly until a single `[CBF] PASS` BATTLE fired on `doopen2a` after Aaron accidentally brushed right Ctrl while mashing arrows. Right Ctrl is FF8 PC's action-key binding and triggered the `battleyarou` Interactive Object listed in the field's JSM. This build adds Ctrl plus the other action / menu keys near the arrow cluster to both the WndProc subclass drop-list and the GetAsyncKeyState mask-list so a finger slip onto Ctrl, Enter, Space, Tab, or Escape during chase Auto no longer reaches FF8.

### Why this build

The v0.15.9.11.3.7 BAT logged a chase that engaged correctly (Auto routing, WndProc subclass installed, ChaseKeyboard activated) and auto-piloted from `domt4_1` through every chase field to `doopen2a` waypoint 10/27 — a clean ~56 seconds of suppressed-arrow play. Then at 20:25:21 the field log fired:

```
[CBF] PASS chase BATTLE call #1 (total #2) field='doopen2a' mode=auto battleCount=0 cap=2147483647 caller=other entityPtr=0x0188CA04
[CBF] PASS in doopen2a -- skipping RegisterChaseAgent (agent is chase-progress director; pin would block transition to dotown_3). BATTLE NO-OP carries the load.
ChaseDetector: battle entered (game-mode 0x0001 -> 0x0003)
```

The JSM scan at field load lists `ent11 cat=3 type=Interactive Object sym='battleyarou'` on `doopen2a`. Interactive Objects fire when the player presses an action key while adjacent. Aaron's verbatim report: *"All I pressed were arrow keys, though I may have accidentally hit the right CTRL key while hitting arrow keys."* Right Ctrl borders the right arrow on standard keyboard layouts. FF8 PC's default keyboard binding uses Ctrl as an action-key alternate. The press fired `battleyarou`, the chase BATTLE fired, the freeze let it through (`caller=other` plus `cap=INT_MAX` semantics), `ChaseDetector` saw the battle entered, the chase ended.

This was not a v0.15.9.11.3.7 regression — the v0.15.9.11.3.6 BAT happened not to brush Ctrl. Both BATs would have produced the same outcome with the same key glance.

### The fix

Two file edits, mirror-extending the drop-lists in the two existing hooks:

**`src/chase_wndproc.cpp`** — `SubclassProc`'s drop-list expanded from `{VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}` to also include `VK_CONTROL`, `VK_RETURN`, `VK_SPACE`, `VK_TAB`, `VK_ESCAPE`, and `VK_SHIFT`. WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP for these VKs return 0 without forwarding while `ChaseKeyboard::IsActive()`.

**`src/dinput8.cpp`** — `HookedGetAsyncKeyState`'s mask-list extended to include `VK_CONTROL` plus `VK_LCONTROL` and `VK_RCONTROL` explicitly (a polling caller can ask for a specific side, so we mask all three), `VK_RETURN`, `VK_SPACE`, `VK_TAB`, `VK_ESCAPE`. Polls for these VKs return 0 while chase Auto is active. The Shift family (`VK_SHIFT` / `VK_LSHIFT` / `VK_RSHIFT`) and the Alt family (`VK_MENU` / `VK_LMENU` / `VK_RMENU`) are deliberately omitted because the mod's own hotkey path in `dinput8.cpp` reads them via `GetAsyncKeyState` for the Shift+F3/F4 speech-volume modifier and the Alt-gate on every F-key handler. Masking either would silently break those mod hotkeys during chase Auto.

The WndProc subclass does drop `VK_SHIFT` because FF8's WndProc reads Shift via WM_KEYDOWN dispatch, not via `GetAsyncKeyState`. Dropping the messages doesn't affect the mod's own Shift detection (which uses `GetAsyncKeyState` and bypasses WM_KEYDOWN entirely). Alt remains forwarded by the WndProc so Alt+F4 close-window still works during a chase.

### Out of scope

Letter keys (Z, X, C, V, A, W, M, O, G, T, L, R, H), the digit row, and the F-key row are left alone in both hooks. Many overlap with mod hotkeys (V/M/O/A/W/G/T/L/R/H/digits/F1–F12), and the mod's hotkey poller reads them via `GetAsyncKeyState` — masking them would break the mod's own version readout, info queries, and TTS controls.

### Risk

Low. Both hooks already exist and work; this just lengthens their drop-list. The added `VK_SHIFT` in the WndProc drop-list could in theory affect FF8 or FFNx if either has a Shift-based input handler that consumes WM_KEYDOWN — mitigated by the mod's `GetAsyncKeyState` pass-through for Shift (engine-level polling reads still work; only WM_KEYDOWN dispatch is dropped). `VK_CONTROL` similarly: any FF8 path that consumes Ctrl WM_KEYDOWN for non-chase functionality is briefly suppressed during the ~60-second chase window, vs the alternative of `battleyarou` getting triggered on a finger slip.

### BAT plan

Aaron triggers the chase, picks Auto, rides through every chase field while pressing arrow keys — and ideally also intentionally brushing right Ctrl to specifically verify this fix. Expected outcome: zero `[CBF] PASS` lines on `doopen2a` regardless of which keys near the arrow cluster get glanced. Chase completes to the `disc00_07h` FMV. Field log shows the same install lines as v0.15.9.11.3.7 (`ChaseKeyboard ACTIVATED` per chase field, `ChaseWndProc subclass INSTALLED` at first activate).

### Files

- `src/chase_wndproc.cpp` — extended VK drop-list with switch statement plus rationale block comment.
- `src/dinput8.cpp` — extended `HookedGetAsyncKeyState` VK mask-list plus rationale block comment.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — current state + BAT plan.

## v0.15.9.11.3.7

Chase ASK dialog polish (Auto/Manual/Original reorder, Auto as default cursor, Original description rewrite) bundled with log diagnostic cleanup (retire stale per-second instrumentation that survived past its research purpose and was dominating the post-chase BAT logs).

### Why this build

The v0.15.9.11.3.6 push (commit `30bc7469`) closed the chase scene. After living with the ASK as it shipped, two small UX issues were apparent:

1. The option order — Manual, Auto, Original — put the most-assistance option in the middle. A blind player who hammers X to commit without listening lands on Manual, the more demanding option. The natural ordering is most-to-least support, with Auto first.
2. The Original description ("Original: vanilla chase, no mod help") was technically accurate but didn't capture the key gameplay difference from Manual. In Manual, `chase_battle_freeze` caps catches at 1 per field; in Original, the vanilla X-ATM092 chase plays out unmodified, which means the robot gets back up after each battle and resumes pursuit within the same field — leading to multiple battles per field.

### The change

Three edits to `src/chase_ask_overlay.cpp`:

- **`kChaseChoices[]`** reordered to `{Auto, Manual, Original}`. The labels themselves are unchanged for Auto and Manual; Original's label is rewritten to `"Original: vanilla, robot keeps getting up to pursue"` — parallel to Manual's `"one battle per field"` framing and conveys the rise-and-pursue mechanic.
- **`ANSWER_AUTO`** and **`ANSWER_MANUAL`** swap values (1 ↔ 2) so the 1-based answer slots match the new positions. `ANSWER_ORIGINAL` stays at 3. The `CommitChoice` switch body is unchanged — it still routes each `ANSWER_*` to its corresponding `ChaseDetector::MODE_*`.
- **`kChaseChoicesDefaultCursor`** stays at 1, but now points to Auto (the new slot 1) instead of Manual. Protects button-mashers: a player who confirms without navigating commits Auto and gets full mod assistance through the chase, rather than committing Manual.

Also fixes a stale comment in the top-of-file design block that referenced the old prompt `"Mode?"` (replaced in v0.15.9.9) and the old option order.

### Bundled log diagnostic cleanup

A review of the v0.15.9.11.3.6 BAT logs (8.30 MB total) found four sources of stale instrumentation spam dominating the volume:

1. **`ff8_world.log` (1.79 MB)** — `WorldMap: [DEFER] Position is (0,0)` fired every frame while the player was in field mode (chase scene). Fix in `src/world_map.cpp::BuildDistanceCatalog`: one-shot static latch logs the first defer line, suppresses subsequent identical defers, and emits a `Position became valid` line when the position recovers. Polling behavior unchanged.

2. **`ff8_field.log` ChaseActiveDiag spam** — v0.15.9.3 added a per-second pre-engage/engage diagnostic to derive camera orientation for the v0.15.9.4 domt4_1 / v0.15.9.5 domt3_2 / v0.15.9.6 domt5_1 direction configs. That research is complete; the post-chase disc00_07h FMV pinned the log at 74 identical lines with `delta=(0,0)`. Fix in `src/chase_auto_pilot.cpp::LogChaseActiveDiagnostic`: early-return retirement. The function body is kept (skipped via early-return) so existing call sites compile without modification.

3. **`ff8_field.log` BridgeDiag per-sample spam** — v0.15.9.8.2 added a 10 Hz all-slots enumerator on `domt1_1` to identify the actually-pursuing X-ATM092 entity (Others slot 3, SYM `laguna`). v0.15.9.8.3 shipped the kani-slot override and the bridge dance state machine; the per-sample dump is no longer useful. Fix in `src/chase_auto_pilot.cpp::LogBridgeDiagnostic`: early-return retirement. The bridge dance's own state-transition log lines (EAST→WEST, WEST→EAST, WEST→EAST TIMEOUT, leap STARTED) remain in `UpdateBridgeDance` and continue to fire on transitions only.

4. **`ff8_field.log` per-second `ChaseAutoPilot: tick=60` spam** — same root cause as #2: during the post-chase FMV the auto-pilot is technically engaged but the party is frozen at `pos=(-210,-1000)`, and the per-second tick logger fires 74 identical lines. Fix in the existing `if (s_diagTickCounter >= 60)` block: track previous-tick position, classify the current sample as idle when position is unchanged from last tick, log the first idle sample so the freeze is recorded, suppress subsequent idle samples via a `suppressLog` flag that wraps both the DIRECTION-like and TARGET branches, and emit a `tick log RESUMED after N idle samples` line on first motion.

5. **`ff8_nav_data.log` (4.66 MB) COORD duplication** — the COORD record was the v0.15.9.3–v0.15.9.5 camera-orientation research log (`for parsing by Python/analysis tools` per the file header). Audit of `Utilities/` confirms no checked-in consumer reads `ff8_nav_data.log`; the only log-parsing script (`dump_chase_events.ps1`) reads `ff8_field.log`. On `dotown_3` the BAT recorded 19 alternating COORD lines for triangles 30 and 22 in one second — the party stood on a triangle boundary and the engine's read flickered every frame. Fix in `src/nav_log.cpp::CoordSample`: cache the last `(fieldName, triIdx)` pair and skip duplicate consecutive samples. Preserves the triangle SEQUENCE the record is structured to capture; kills the boundary-flicker spam. Note: the file is opened in append mode (`fopen_s(..., "a")`) so the 4.66 MB accumulated across sessions; new sessions start clean once Aaron deletes the existing file.

Estimated combined reduction across the four logs: roughly 6–7 MB of the 8.30 MB total. Behavior of the mod itself is unchanged — every edit is pure log volume.

### Risk

Very low. Three string edits + two constant value swaps + one cursor-comment change. No behavior change to `CommitChoice`, `ChaseDetector::SetChaseMode`, `DialogInject`, or any chase machinery. INI persistence stores mode by name (`"auto"` / `"manual"` / `"original"`), not by `ANSWER_*` slot, so existing save files are unaffected.

### BAT plan

Aaron triggers the chase. ASK reads the prompt ("X-ATM092 is heading right for you. How do you want to run?") + the three labels in new order: Auto first, then Manual, then Original. Default cursor on Auto. If Aaron confirms without navigating, MODE_AUTO is committed. Arrowing down once announces Manual; twice announces the new Original label.

### Files

- `src/chase_ask_overlay.cpp` — `kChaseChoices` reorder, `ANSWER_*` swap, default-cursor comment, top-of-file design comment, history comment block extension.
- `src/world_map.cpp` — `BuildDistanceCatalog` `[DEFER]` log throttle.
- `src/chase_auto_pilot.cpp` — `LogChaseActiveDiagnostic` early-return retirement, `LogBridgeDiagnostic` early-return retirement, per-second tick=60 idle-sample suppression.
- `src/nav_log.cpp` — `CoordSample` `(fieldName, triIdx)` debounce.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — current state + BAT plan.

## v0.15.9.11.3.6

Install a WndProc subclass that drops arrow-key `WM_KEY*` messages during chase Auto. Closes the last untreated keyboard delivery path to FF8's own WndProc — the `[+0xb48]` per-message handler dispatch invoked from FF8_EN.exe's window procedure at `0x0040AC5B`.

### Why this build

Across v0.15.9.11.3.1 through .11.3.5 the chase still got caught on `doopen2a` whenever arrow keys were pressed, even after the .11.3.4 SendInput-collision fix and the .11.3.5 leak-probe removal. Synthetic buffer masking confirmed working, GetAsyncKeyState arrow queries returning 0 confirmed working, leak-probe gone, no SendInput collision — yet the catch reproduced reliably with keys pressed.

That narrows the disruption to a path that none of those hooks touch. The v0.15.9.11.3.5 disassembly walk through FF8's WndProc at `0x0040AC5B` (the function containing the Ctrl+Q `GetAsyncKeyState` call at `0x0040AE14`, confirmed to be a WndProc by structure — switch on `uMsg` with `WM_KEY*` branches that route into a `[+0xb48]` per-message-handler dispatch table) showed that arrow `WM_KEYDOWN`/`WM_KEYUP` messages reach that `[+0xb48]` dispatch. The synthetic-buffer `GetDeviceState` detour, the `GetAsyncKeyState` hook, and the v0.15.9.11.3.4 fix all leave that path alone.

Either `[+0xb48]` writes a global the chase-progress director script polls ("is the player struggling against the chase"), or the sheer volume of `WM_KEY*` dispatch on the user's keypresses shifts the main thread's frame timing relative to the chase director's catch evaluator. Both are addressed by stopping the messages before FF8's WndProc sees them.

### The fix

New module `src/chase_wndproc.cpp` with `EnsureInstalled()` that enumerates top-level visible windows owned by FF8_EN.exe (`EnumWindows` + `GetCurrentProcessId` + `GetWindowThreadProcessId` filter) and `SetWindowLongPtrW(GWLP_WNDPROC)` subclasses each. The subclass runs on whichever thread pumps the message — for FF8's main game window that's the main thread, the same thread that runs the chase script's catch evaluator.

When `ChaseKeyboard::IsActive()` AND `msg` is in `{WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP}` AND `wParam` is in `{VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}`: return 0 (the documented `WM_KEY*` "handled" reply) without forwarding. FF8's WndProc and its `[+0xb48]` dispatch never see the message. No global is written, no main-thread cycles spent on it, no message-pump volume effect.

Everything else (mouse, paint, timer, non-arrow keys, all messages when chase is inactive) forwards via `CallWindowProcW`/`CallWindowProcA` so FF8's behavior is bit-for-bit identical outside the narrow arrow-key / chase-Auto window. Character set matched per-window via `IsWindowUnicode`.

### Why not WH_KEYBOARD_LL

v0.15.9.11.1/.11.2 used `WH_KEYBOARD_LL` and the chase broke on `doopen2a` at waypoint 5–6 of 26, ~2 seconds after engagement. `WH_KEYBOARD_LL` forces every `SendInput` call from any thread to synchronously round-trip through the main thread's message pump — fatal for per-tick path-finding timing.

WndProc subclassing has no such cost. It runs *inline* in the existing message pump on the existing thread — no extra synchronization, no cross-thread round-trip, no per-tick latency penalty.

Also: the mod's own `SendInput` from `field_nav_autodrive.inl::InjectKey` is already gated on `ChaseKeyboard::IsActive()` (v0.15.9.11.3.4) — during chase Auto the auto-pilot writes directly to the synthetic DirectInput buffer and skips `SendInput` entirely. So the new subclass only suppresses *the user's* physical keypresses, never the auto-pilot's own arrows.

### Architecture now

Four coordinated hooks during chase Auto:

1. `DirectInputCreateA` chain (`dinput8.cpp`) → catches FF8's v7 keyboard device creation
2. `IDirectInputDeviceA::GetDeviceState` vtable detour (`chase_keyboard.cpp`) → returns synthetic buffer
3. `user32::GetAsyncKeyState` MinHook (`dinput8.cpp`) → arrow VK queries return 0
4. **WndProc subclass on every top-level window (`chase_wndproc.cpp`) → arrow `WM_KEY*` return 0** — NEW

Together they make every documented and undocumented keyboard delivery path to FF8 return "no arrow pressed" for the duration of `ChaseKeyboard::IsActive()`.

### Install timing

`ChaseKeyboard::Activate()` lazy-calls `ChaseWndProc::EnsureInstalled()` on first chase activation. `EnsureInstalled` is idempotent on subsequent calls. We do this at Activate rather than at module init because `EnumWindows` needs FF8/FFNx to have created its main game window first; the first chase Auto happens many seconds after launch (the player has to reach the Dollet chase scene), so the window is guaranteed to exist.

Permanent install — never uninstalled during gameplay. `SetWindowLongPtrW` is documented as cross-thread safe for install, but a mid-gameplay uninstall would race against in-flight `DispatchMessage` on the main thread. Outside chase Auto the subclass is a cheap pass-through (one `volatile bool` read per message, false short-circuit), so leaving it permanently installed costs nothing.

### BAT plan

Trigger the chase, pick Auto, ride it **while pressing arrow keys**. Two outcomes:

- **Clean** — full route (`domt4_1` → `domt3_2` → `domt5_1` → `domt2_1` → `domt1_1` → `doopen2a` → `dotown_3` → `dotown_2` → `dotown_1`) completes, 0 `[CBF] PASS` lines, matching the v0.15.9.8.3 hands-off result. Mod log near first chase Auto activation shows `ChaseWndProc: WndProc subclass INSTALLED on HWND 0x...` for each FF8 window discovered. Chase scene item #3 done; push the squash.
- **Still caught on `doopen2a`** — the disruption is happening *below* the WndProc layer. Next suspects: raw input messages (`WM_INPUT` via `RegisterRawInputDevices`), DirectInput buffered mode rather than immediate state, or some other delivery path we haven't traced. Next step then is diagnostic: per-message WM type/wParam logging from inside the subclass to find any arrow-bearing message getting through.

### Risk

Low. WndProc subclassing is a textbook Win32 pattern that has shipped in countless mods, tools, and screen readers; `SetWindowLongPtrW` is documented as cross-thread safe. The `CallWindowProc` forward keeps FF8's WndProc behavior bit-for-bit identical outside the narrow arrow-key/chase-Auto window. The only real fragility is the install-time race (window must exist when `EnumWindows` runs), mitigated by lazy install at `Activate` — the first chase happens many seconds after launch, so the window definitely exists.

### Files

- `src/chase_wndproc.h` (new)
- `src/chase_wndproc.cpp` (new)
- `src/chase_keyboard.cpp` — `#include "chase_wndproc.h"`, call `ChaseWndProc::EnsureInstalled()` from `Activate`
- `src/deploy.bat` — add `chase_wndproc.cpp` to the compile list
- `src/ff8_accessibility.h` — version bump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.9.11.3.5

Remove the v0.15.9.11.3.3 leak-probe from `chase_keyboard.cpp`. It was diagnostic-only code that has served its purpose — and it's now a confirmed confound.

### What the v0.15.9.11.3.4 BAT showed

The v0.15.9.11.3.4 BAT (2026-05-13 23:33) proved the .11.3.4 input-layer fix **works**. The `[LEAK-PROBE]` lines showed FF8 receiving the synthetic buffer with the physical arrows masked (`PHYSICAL KEY MASKED` on every state change), and the party moved correctly — waypoints 0 through 6 reached on `doopen2a`, running south at normal pace, synthetic buffer coherent.

But the chase **still got caught** at wp 6/28 on `doopen2a` — the chase-progress director (`battleyarou`, `entityPtr=0x0188CA04`) fired BATTLE, `[CBF] PASS` let it through, real catch. That's the same failure location as v0.15.9.11.3.1 and .2.

So across four builds (.11.3.1 through .11.3.4), all with the keyboard-device `GetDeviceState` masking confirmed working, keys-pressed still causes a catch. The .11.3.4 disassembly already proved FF8's movement reads **only** that masked device buffer — so the user's keys are not reaching FF8's movement code. The disruption is something else that pressing keys triggers.

### Why the probe has to go

The leak-probe (added in .11.3.3, never removed) is now pure liability. `HookedGetDeviceState` polled the real device an **extra** time per frame and wrote a `[LEAK-PROBE]` log line on **every** physical-key state change. During key-mashing that is roughly 20 file-I/O writes per second on the field thread — and zero hands-off. That asymmetric logging load is itself a candidate disruptor of chase Auto timing.

Critically: **no build has ever run with both the SendInput-collision bug fixed *and* the probe I/O storm absent.** v0.15.9.11.3.1/.2 had the SendInput collision (fixed in .11.3.4); v0.15.9.11.3.3/.4 have the probe. Removing the probe makes this the first genuinely clean test of the .11.3.4 fix.

### The change

`HookedGetDeviceState` is now just the synthetic-buffer `memcpy` + `return DI_OK` — the part FF8 actually sees is byte-for-byte identical to .11.3.4. The extra real-device poll, the `DIAG_ARROW_DIK` array, the `s_diag*` statics, and the diag-state reset in `Activate()` are all gone.

### BAT plan

Trigger the chase, pick Auto, ride it **while pressing arrow keys**. If the probe's I/O storm was the remaining disruptor, the chase now completes the full route (`domt4_1` → `domt3_2` → `domt5_1` → `domt2_1` → `domt1_1` → `doopen2a` → `dotown_3` → `dotown_2` → `dotown_1`) clean, 0 `[CBF] PASS` lines, matching the v0.15.9.8.3 hands-off result. The field log will also be much quieter — no `[LEAK-PROBE]` lines at all.

If it **still** catches on `doopen2a` with keys pressed, the probe is ruled out and the remaining suspect is the Windows message queue: the user's physical keypresses flood FF8's window with `WM_KEYDOWN`/`WM_KEYUP` messages, and either FF8's WndProc does something with them beyond the known Ctrl+Q hotkey, or the message-pump volume shifts the main thread's frame timing relative to the chase director's catch clock. Next step then is to read FF8's WndProc (function at `0x0040AC5B`, which contains the `GetAsyncKeyState` call at `0x0040AE14`) in the disassembly.

### Risk

Very low. Pure removal of diagnostic-only code. No signature changes, no new dependencies. The substitution behavior FF8 sees is unchanged from .11.3.4.

### Files

- `src/chase_keyboard.cpp` — leak-probe removed: comment block, `DIAG_ARROW_DIK`, `s_diag*` statics, the probe block in `HookedGetDeviceState`, and the diag-reset in `Activate()`
- `src/ff8_accessibility.h` — version bump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.9.11.3.4

Stop `SendInput` during chase Auto — drive FF8 purely through chase_keyboard's synthetic buffer. This is **the fix** for the v0.15.9.11.3.1/.2/.3 BAT failures: the chase completes clean hands-off, but gets caught when arrow keys are pressed.

### What the investigation established

v0.15.9.11.3.3's leak-probe BAT came back **Outcome #1** — `[LEAK-PROBE]` lines tagged `PHYSICAL KEY MASKED`. chase_keyboard's `GetDeviceState` hook *is* being called for FF8 field input and *is* masking the physical arrows. The DirectInput keyboard-state path is clean.

Two follow-up disassembly investigations then explained why the chase still broke:

- **FFNx source** (`input.cpp`, `joystick.cpp`, `gamepad.cpp`, `gamehacks.cpp`): FFNx does not bridge the keyboard into gamepad or joystick input anywhere. `GetGameKeyState()` only reads the keyboard device; the joystick and gamepad code polls real physical hardware; `gamehacks.cpp` is the sole consumer and uses them only for FFNx's own speedhack/debug shortcuts.
- **FF8_EN.exe input pipeline** (`engine_eval_keyboard_gamepad_input` @0x00467D10, `get_key_state` @0x004685F0, `ctrl_keyboard_actions` @0x004A2E50, `dinput_update_gamepad_status` @0x004692B0): FF8 reads keyboard state **only** from the 256-byte DirectInput device buffer whose pointer lives at `*0x01CD02D8`. `engine_eval` makes exactly one call per frame to `get_keyboard_state` — the function FFNx replaces with `GetGameKeyState` → `GetDeviceState(256,...)`, the call chase_keyboard hooks — stores the returned buffer pointer, and every downstream reader (engine_eval's own direct reads, `get_key_state`'s per-scancode accessor, `ctrl_keyboard_actions`) reads only that buffer. There is **no path** in FF8's field-input pipeline that reads OS-level key state: no `GetAsyncKeyState`, no `GetKeyboardState`, no `WM_KEYDOWN` consumption for movement. The lone `GetAsyncKeyState` import is called from exactly one place — inside FF8's WndProc, handling `WM_KEYUP` for 'Q' while Ctrl is held — a Ctrl+Q hotkey, which is why hooking it in v0.15.9.11.3.2 changed nothing.

### Root cause

Once chase_keyboard substitutes the keyboard device buffer (which the probe proved it does), FF8's keyboard input is *entirely* the synthetic buffer. The mod's `SendInput` calls in `InjectKey` reached **nothing** in FF8 during chase Auto — they only dumped synthetic key events into the shared OS input queue, where they interleaved with the user's physical key events and desynced the diff-based `SetHeldDirections` model (it tracks what it *thinks* is held; pressing/releasing the same scancodes corrupts that). The bridge dance + keep-alive pulse is the most `SendInput`-edge-sensitive chase code — deliberate release/re-press every 30 ticks, direction flips on transitions — which fits `domt1_1` being where v0.15.9.11.3.3 broke.

### The fix

`InjectKey` (`field_nav_autodrive.inl`) now checks `ChaseKeyboard::IsActive()` first. When active (chase Auto only), it writes **only** to the synthetic buffer via `SetScancodeDown`/`SetScancodeUp` and returns immediately — `SendInput` is skipped entirely. The synthetic-buffer write is the complete and sufficient delivery path: it produces the same `0x80`↔`0x00` byte transitions the engine's edge detection needs, including for the keep-alive pulse's deliberate KEYUP→KEYDOWN edge.

Outside chase Auto (F9 path-finding, world-map AD), `ChaseKeyboard::IsActive()` is false and `InjectKey` falls through to the original `SendInput` path completely unchanged. ChaseKeyboard is Activated at chase auto-pilot Engage and Deactivated at Disengage, so the entire engaged window — keep-alive pulses and key releases included — goes through the synthetic-buffer path; the release calls clear the synthetic buffer bytes, then `ChaseKeyboard::Deactivate()` clears the whole buffer. This replaces the v0.15.9.11.3 dual-write (SendInput *then* synthetic-buffer write) with synthetic-buffer-only during chase Auto.

### BAT plan

Trigger the chase, pick Auto, ride it **while pressing arrow keys**. Expected: the party drives through every chase field (`domt4_1` → `domt3_2` → `domt5_1` → `domt2_1` → `domt1_1` → `doopen2a` → `dotown_3` → `dotown_2` → `dotown_1`) without disruption, the bridge dance fires its EAST/WEST transitions cleanly, `domt5_1` walks rather than runs (listen for walking-speed audio — the W-key synthetic-buffer delivery wasn't explicitly exercised in a passing .11.3.x BAT), 0 `[CBF] PASS` lines, chase completes to the Lapin Beach FMV — matching the v0.15.9.8.3 hands-off result but now with keys pressed. Also confirm there are no `[InjectKey] SendInput FAILED` lines (there should be none — `SendInput` isn't called during chase Auto anymore).

### Risk

Low. Single-function change, surgically gated on `ChaseKeyboard::IsActive()`. The synthetic-buffer write path is unchanged from v0.15.9.11.3 — only `SendInput` is now skipped. No new dependencies. If the chase still breaks with keys pressed, the next suspect is the W-key synthetic-buffer delivery specifically (the probe verified the four arrow DIK slots, not `DIK_W` = 0x11) or a timing interaction between the synthetic-buffer write and the engine's per-frame read. If non-chase auto-drives regress, `ChaseKeyboard::IsActive()` is somehow returning true outside chase Auto — check the Activate/Deactivate call sites.

### Files

- `src/field_nav_autodrive.inl` — `InjectKey`: `ChaseKeyboard::IsActive()` early-return with synthetic-buffer-only write; `SendInput` path now reached only when chase_keyboard is inactive
- `src/ff8_accessibility.h` — version bump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.9.11.3.3

Keyboard-leak probe — a diagnostic-only build that instruments `HookedGetDeviceState` to find why physical arrow presses still corrupt chase Auto navigation on doopen2a, after v0.15.9.11.3.2's GetAsyncKeyState hook failed to stop it. This build fixes nothing; it produces the log evidence that pins the leak path.

### What v0.15.9.11.3.2 BAT revealed

The mod log head confirmed all four keyboard hooks installed at startup:

```
DirectInputCreateA hooked at 0x71BCC390 (from dinput.dll)
GetAsyncKeyState hooked at 0x75DD9150 (from user32.dll)
IDirectInputA::CreateDevice hooked at 0x71BCC6E0
ChaseKeyboard: IDirectInputDevice8A::GetDeviceState hooked at 0x71BC7680
```

The field log confirmed `ChaseKeyboard: ACTIVATED` fired on doopen2a engage and stayed active for the whole drive. Yet with Aaron pressing arrow keys, the party was still caught at waypoint 7/28: `battleyarou` (entityPtr=0x0188CA04) fired BATTLE, `[CBF] PASS` let it through (AUTO cap=INT_MAX), `game-mode 0x0001 -> 0x0003` — a real catch battle. Same hands-off-clean / keys-pressed-caught split as v0.15.9.11.3.1. The GetAsyncKeyState hook installed and was active, so that path is ruled out as the leak.

### The paradox

FF8_EN.exe imports exactly two keyboard functions — `DirectInputCreateA` (v7) and `GetAsyncKeyState` — and both are hooked. FFNx replaces FF8's internal `get_keyboard_state` with its own `GetGameKeyState()`, which calls `keyboard_device->GetDeviceState(256, keys)` on FF8's keyboard device. The mod hooks `GetDeviceState` at the function level via MinHook, and `HookedGetDeviceState` cleanly `memcpy`s the synthetic buffer and returns `DI_OK` without ever polling the real device. By static analysis, that chain *should* already mask physical keys during chase Auto — but it doesn't. The leak is a path static analysis can't see.

### The probe

`HookedGetDeviceState`, when `s_active`, now also polls the real device into a scratch buffer and compares the four arrow DIK slots against the synthetic buffer. Logging is transition-gated — a first-call marker, then a line only when the physical-vs-synthetic arrow state changes, plus a ~2-second heartbeat — so the field log stays readable. Three conclusive outcomes:

- **`state-change ... PHYSICAL KEY MASKED`** → the hook *is* catching FFNx's reads and *is* masking physical arrows. The leak is a different path; hunt that next.
- **first-call + heartbeats, but `physMask` never exceeds `synthMask`** → physical arrows aren't even reaching the real device while we substitute.
- **no `[LEAK-PROBE]` logs at all during a chase Auto drive** → FFNx is not calling this hooked `GetDeviceState` for field input; look at FFNx's wiring.

Diagnostic state is reset in `Activate()` so each chase Auto engagement starts clean.

### Why a probe instead of a fix

Aaron's directive: do not paper over this with battle suppression. v0.15.9.8.3 proved the chase completes with zero encounters hands-off, so key presses are corrupting working navigation — that corruption has to be found and stopped, not hidden behind the `[CBF]` band-aid.

### BAT plan

Trigger the chase, pick Auto, ride it while pressing arrow keys. The doopen2a catch is expected to still happen. Read back the `[LEAK-PROBE]` lines from `ff8_field.log` — they identify which of the three outcomes holds, and therefore where the leak is.

### Risk

Very low. Pure-additive instrumentation inside the existing hook, gated on `s_active` (chase Auto only). The extra real-device poll is an immediate-mode read — idempotent and cheap. What FF8 receives is unchanged: still the synthetic buffer.

### Files

- `src/chase_keyboard.cpp` — `[LEAK-PROBE]` instrumentation in `HookedGetDeviceState`, diagnostic-state reset in `Activate()`
- `src/ff8_accessibility.h` — version bump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.9.11.3.2

GetAsyncKeyState hook to close the non-DirectInput keyboard leak path. v0.15.9.11.3.1 got the DirectInput hook installed correctly but Aaron's physical arrows still reached FF8 on doopen2a — through a path the synthetic buffer doesn't cover.

### What v0.15.9.11.3.1 BAT revealed

Startup log confirmed all three hooks installed successfully:

```
DirectInputCreateA hooked at 0x... (from dinput.dll)
IDirectInputA::CreateDevice hooked at 0x... (vtable[3] on IDirectInputA=0x...)
ChaseKeyboard: IDirectInputDevice8A::GetDeviceState hooked at 0x...
```

Field log on chase Auto engage showed the GOOD activation:

```
ChaseKeyboard: ACTIVATED -- synthetic keyboard buffer now substituted for
GetDeviceState reads. Physical key presses will NOT reach the engine
until Deactivate.
```

Aaron's two-test sequence:
- **Test 1 (no keys pressed):** chase completed successfully through all fields.
- **Test 2 (arrow keys pressed):** suppression worked through domt4_1 / domt3_2 / domt5_1 / domt2_1 / domt1_1, but on doopen2a Aaron's arrows started reaching the engine, disrupted the auto-pilot, and triggered the chase-progress director (entityPtr=0x0188CA04) to fire BATTLE at waypoint 6/28.

The failure pattern is identical to v0.15.9.11.1 / .11.2 (the WH_KEYBOARD_LL builds): same field, same director, same waypoint range. But the synthetic buffer hook IS confirmed active. So Aaron's keys must be taking a different path that bypasses DirectInput entirely.

### Root cause

FF8_EN.exe imports `GetAsyncKeyState` from USER32.dll (FF8_EN_imports.txt line 227). `GetAsyncKeyState` reads OS-level keyboard state directly — it doesn't go through DirectInput at all, and FFNx's `replace_function` redirect on `get_keyboard_state` doesn't touch it.

On doopen2a specifically, the chase-progress director's JSM script polls `GetAsyncKeyState` for arrow keys to decide when to fire the catch BATTLE. Aaron's physical presses go straight to the OS keyboard state where `GetAsyncKeyState` reads them, completely bypassing our synthetic buffer. Earlier chase fields use simpler progression logic that doesn't poll this path, which is why suppression worked there.

### Fix

Hook `GetAsyncKeyState` from `user32.dll` via MinHook. During `ChaseKeyboard::IsActive()`, return 0 for `VK_UP` / `VK_DOWN` / `VK_LEFT` / `VK_RIGHT` queries; pass through all other VK queries to the real implementation. This blocks Aaron's physical arrow presses from reaching FF8 via the non-DirectInput path while leaving F1-F12, V/G/T/L/R info-readout keys, /, =, -, \, Backspace, ` repeat, and all other mod-handled keys fully functional.

Why return 0 (not the synthetic buffer state): the auto-pilot's own arrows are NOT consumed via `GetAsyncKeyState` by any path that matters — test 1 (no keys pressed) completed the chase successfully with the auto-pilot driving via DirectInput alone — so returning 0 for ALL arrow queries during chase Auto is safe and simple. If a future regression shows FF8 also needs to see the auto-pilot's arrows via `GetAsyncKeyState`, we can switch to a VK→DIK mapped read from `ChaseKeyboard::s_keyBuf` at that point.

The mod's own arrow handler in `field_nav_handlekeys.inl` is already gated on `s_driveActive && !s_chaseDriveActive` (since v0.15.9.2), so it doesn't query arrow VKs during chase Auto — no mod-side regression.

### Expected v0.15.9.11.3.2 BAT outcome

Mod log near startup should show a new line:

```
DllMain: GetAsyncKeyState hooked at 0x... (from user32.dll). During chase
Auto, arrow VK queries return 0; all other VKs pass through. Closes the
non-DirectInput keyboard leak path that affected doopen2a in v0.15.9.11.3.1 BAT.
```

On chase Auto engage: existing `ChaseKeyboard: ACTIVATED` line. Aaron tests with arrow key presses during chase Auto — party drives through ALL fields including doopen2a / dotown_3 / dotown_2 / dotown_1 without disruption, 0 [CBF] PASS lines, chase completes successfully to Lapin Beach FMV.

### Files

- `src/dinput8.cpp` — `HookedGetAsyncKeyState` + `InstallGetAsyncKeyStateHook` (full implementation with rationale comments) + DllMain wiring after `InstallDirectInputCreateAHook`.
- `src/ff8_accessibility.h` — version bump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Risk

Low. Single new hook on a well-known Win32 API. Hook is gated on `ChaseKeyboard::IsActive()` so it only affects behavior during chase Auto — F9 path-finding, world-map AD, all other gameplay unchanged. Graceful degradation if install fails (chase Auto still works without arrow suppression, falling back to v0.15.9.11.3.1 behavior).

If suppression STILL leaves Aaron's keys reaching FF8 after this, the next leak paths to investigate are WM_KEYDOWN messages (unlikely since FFNx's `HandleInputEvents` consumes them) and `GetKeyState` (not imported by FF8 per the import table, but worth confirming).

## v0.15.9.11.3.1

Add DirectInputCreateA hook chain for FF8's DirectInput 7 keyboard. The v0.15.9.11.3 synthetic-buffer approach was structurally correct but hooked the wrong DirectInput version.

### What v0.15.9.11.3 BAT revealed

Field log at chase Auto engage (2026-05-13 19:05:07):

```
ChaseKeyboard: ACTIVATED but hook NOT installed -- synthetic buffer set but
GetDeviceState detour absent; physical key presses will continue to reach the
engine. This is the v0.15.9.11.3 graceful-degradation path.
```

The synthetic buffer was set up but no GetDeviceState detour was installed. Aaron's physical arrow presses then disrupted the auto-pilot, confirming keys still reached the engine.

### Root cause

FF8_EN.exe imports `DirectInputCreateA` from `DINPUT.dll`, NOT `DirectInput8Create` from `dinput8.dll`. FF8 is a 2013 Steam port of a 1999 game and uses the original DirectInput 7 API for its keyboard. FFNx's `input.cpp` source confirms: `IDirectInputDeviceA* keyboard_device = *common_externals.keyboard_device` — old-API interface type, no `8`.

Our v0.15.9.11.3 `IDirectInput8A::CreateDevice` hook only caught FFNx's own DirectInput 8 device creations (FFNx uses DirectInput 8 for its own internal polling, gamepad, overlay UI). FF8's keyboard is on the v7 chain entirely separate from anything we were hooking.

### Fix

Install a parallel hook chain on the DirectInput 7 API:

1. `LoadLibraryA("dinput.dll")` + `GetProcAddress("DirectInputCreateA")` and MinHook the result.
2. `HookedDirectInputCreateA` vtable-hooks `IDirectInputA::CreateDevice` (vtable[3]) on the returned `IDirectInputA*`.
3. `HookedCreateDeviceA` forwards the returned `IDirectInputDeviceA*` to `ChaseKeyboard::OnDeviceCreated` (via `reinterpret_cast` to `IDirectInputDevice8A*`; the COM vtable layouts are binary-compatible through `GetDeviceState` at vtable[9]).

From there, the existing v0.15.9.11.3 logic takes over unchanged — `OnDeviceCreated` checks the GUID and vtable-hooks `GetDeviceState` on keyboard devices. The chase auto-pilot's `InjectKey` dual-write path was already correct; it just had no synthetic buffer to write into because the hook never installed.

Also moves `MH_Initialize` from `AccessibilityThread` to `DllMain` so the hook is installed synchronously before any FF8 code runs. `MH_Initialize` is idempotent — the later call in `AccessibilityThread` returns `MH_ERROR_ALREADY_INITIALIZED`, which is logged and execution continues normally. This eliminates any race condition with FF8's init-time `DirectInputCreateA` call.

### Expected v0.15.9.11.3.1 BAT outcome

Mod log near startup shows both:
- `DirectInputCreateA hooked at 0x... (from dinput.dll)`
- `IDirectInputA::CreateDevice hooked at 0x... (vtable[3] on IDirectInputA=0x...)`

Then on FF8's keyboard device creation (during FF8 init):
- `ChaseKeyboard: IDirectInputDevice8A::GetDeviceState hooked at 0x... (vtable[9] on keyboard device 0x...). Synthetic buffer path armed`

On chase Auto engage:
- `ChaseKeyboard: ACTIVATED -- synthetic keyboard buffer now substituted for GetDeviceState reads. Physical key presses will NOT reach the engine until Deactivate`

Aaron presses arrow keys during chase Auto — auto-pilot continues uninterrupted because physical state is never polled.

### Files

- `src/dinput8.cpp` — DirectInputCreateA + IDirectInputA::CreateDevice hook chain + `InstallDirectInputCreateAHook` called from DllMain. MH_Initialize moved earlier to DllMain.
- `src/ff8_accessibility.h` — version bump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Risk

Low-medium. The new hook chain mirrors the v0.15.9.11.3 IDirectInput8A path exactly; the only differences are the entry point (`DirectInputCreateA` instead of our `DirectInput8Create` proxy) and the C type names (`IDirectInputA*`/`IDirectInputDeviceA*` vs the v8 versions). Standard MinHook patterns throughout. Graceful degradation if any install step fails (chase Auto still works without suppression).

## v0.15.9.11.3

Synthetic keyboard buffer substitution for chase Auto. Replaces three failed `WH_KEYBOARD_LL` approaches (v0.15.9.11 / .11.1 / .11.2) that all broke the chase auto-pilot on doopen2a (caught at waypoint 5-6/26 of the 26-waypoint chase path, dmag=825 running speed, ~2s after engagement, by the chase-progress director entity).

### Why the LL hook failed

`WH_KEYBOARD_LL` is a global Windows hook installed on the calling thread's message pump. SendInput-generated keyboard events become synchronous round-trips through that thread's message pump before SendInput returns to the caller. The chase auto-pilot calls SendInput from the accessibility worker thread; with the LL hook installed on the main thread (the only thread with a message pump), every InjectKey from the worker thread waits on the main thread to service the message before continuing.

That per-call latency was enough to break the auto-pilot's per-tick path-finding on doopen2a. v0.15.9.11.2 was an isolation build (LL hook only, no other suppression layers) and reproduced the failure exactly — proving the LL hook itself, not any other layer, was the cause.

### What v0.15.9.11.3 does instead

New architecture using a function-level vtable detour on `IDirectInputDevice8::GetDeviceState`:

1. Our DirectInput8Create proxy hooks `IDirectInput8::CreateDevice` (vtable[3]) via MinHook.
2. When FF8/FFNx creates the keyboard device (GUID_SysKeyboard), our hooked CreateDevice forwards the device pointer to `chase_keyboard::OnDeviceCreated`, which vtable-hooks `IDirectInputDevice8::GetDeviceState` (vtable[9]) on that specific device.
3. The mod owns a 256-byte synthetic DirectInput keyboard buffer.
4. When chase Auto engages, `ChaseKeyboard::Activate()` is called. From that point, the GetDeviceState detour returns the synthetic buffer instead of polling the real keyboard device. Physical key presses never reach FF8.
5. The chase auto-pilot's `InjectKey` in `field_nav_autodrive.inl` dual-writes its arrow + W events into the synthetic buffer (via `ChaseKeyboard::SetScancodeDown/Up`) when `ChaseKeyboard::IsActive()` is true. So the engine still sees the auto-pilot's intended input.
6. On `Disengage()`, `ChaseKeyboard::Deactivate()` restores pass-through behavior.

Outside chase Auto (F9 path-finding, world-map AD), `IsActive()` returns false and the hook is a pure pass-through to the real device. F9 and world-map AD behavior is fully preserved.

### FFNx source dive (2026-05-13)

FFNx's `common.cpp` line ~950: `replace_function(common_externals.get_keyboard_state, &GetGameKeyState)`. FFNx replaces FF8's `get_keyboard_state` entirely. FFNx's `GetGameKeyState` calls `keyboard_device->GetDeviceState(256, keys)` and returns the buffer to FF8. FFNx also has a built-in `SetBlockKeysFromGame(bool)` that conceptually zeros the buffer — but using it directly would also block the auto-pilot's keys (no injected-vs-physical distinction at that level).

Hooking `GetDeviceState` one layer deeper than FFNx's block path lets us substitute the buffer entirely while controlling exactly what the engine reads.

### Future use cases

The same Activate / Deactivate mechanism can be reused for any scripted-sequence accessibility feature where user input needs to be suppressed: cutscene playback, walk-and-talk dialog gap, timed events, etc. Chase Auto is the first user.

### Files

- NEW: `src/chase_keyboard.h`, `src/chase_keyboard.cpp`
- `src/dinput8.cpp` (CreateDevice vtable hook + ChaseKeyboard::Shutdown wiring)
- `src/chase_auto_pilot.cpp` (Activate at Engage + Deactivate at Disengage)
- `src/field_nav_autodrive.inl` (InjectKey dual-write)
- `src/field_navigation.cpp` (chase_keyboard.h include before namespace opens)
- `src/deploy.bat` (chase_keyboard.cpp added to compile list)
- `src/ff8_accessibility.h` (version bump)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`, `CHANGELOG.md`

### Risk

Medium. The CreateDevice + GetDeviceState vtable hooks are new ground for the mod — previously we only MinHook'd FF8 engine functions, not COM interface methods. Both hooks are standard MinHook patterns. Graceful degradation: if MH_CreateHook or MH_EnableHook fails at either site, the mod logs a warning and continues with normal pass-through. Chase Auto still works; physical key presses just reach the engine. Suppression is the only thing disabled.

### Expected BAT outcome

Chase Auto plays through end-to-end identical to v0.15.9.10 Auto BAT (clean transit, 0 [CBF] PASS, full completion to Lapin Beach FMV). With Aaron pressing arrow keys during the chase, the auto-pilot continues uninterrupted — proving suppression works. Verification markers in mod log: `ChaseKeyboard: IDirectInputDevice8A::GetDeviceState hooked at 0x...` near startup. In field log on chase engage: `ChaseKeyboard: ACTIVATED -- synthetic keyboard buffer now substituted...`. On disengage: `ChaseKeyboard: DEACTIVATED`.

## v0.15.9.10

MODE_ORIGINAL implementation (Aaron's chase-scene item #2). The third ASK option is now a real third mode that bypasses all mod chase machinery, letting the vanilla FF8 chase play out unmodified.

### Design

When the player picks Original at the chase ASK, `ChaseDetector::SetChaseMode(MODE_ORIGINAL)` persists the mode to `ff8_accessibility.ini`. From that point until the user picks a different mode, every piece of mod chase machinery short-circuits:

- `chase_battle_freeze::Hook_opcode_battle` returns `s_origBattle(entityPtr)` immediately when mode is Original, with no chase-counter increment, no `[CBF] PASS`/`NO-OP` log, and no `RegisterChaseAgent` call. Vanilla FF8 chase battles fire as Square shipped them.
- `chase_kani_freeze::Update` bails before reading `pGameMode` when mode is Original. If a freeze was active from a previous mode (mid-chase mode swap via INI), `DeactivateFreeze` runs cleanly so no stale pin keeps writing.
- `chase_kani_freeze::RegisterChaseAgent` also short-circuits as belt-and-suspenders, in case any future code path calls it without going through chase_battle_freeze.
- `chase_auto_pilot` needs no change: its engagement gate already requires `mode == MODE_AUTO`, so MODE_ORIGINAL naturally fails the gate and the auto-pilot stays disengaged.

The mod's screen-reader assistance (field TTS, dialog announcements, etc.) is unaffected -- only the chase-specific mod machinery goes inert. The chase ASK itself still fires and reads out the three options; the user just chooses Original instead of Manual or Auto.

### Implementation

Five-file touch:

- **`src/chase_detector.h`**: `MODE_ORIGINAL = 2` added to the `Mode` enum with rationale comments. Existing `MODE_MANUAL = 0` and `MODE_AUTO = 1` values unchanged for backward compatibility with existing INI files.
- **`src/chase_detector.cpp`**:
  - `LoadChaseModeFromIni` accepts `"original"` string -> `MODE_ORIGINAL`.
  - `ChaseModeName` returns `"original"` for `MODE_ORIGINAL` via switch (previously a ternary; rewritten for the third case).
- **`src/chase_battle_freeze.cpp::Hook_opcode_battle`**: short-circuit at top of the `if (inChase)` branch. Sequence: read mode -> if MODE_ORIGINAL, return `s_origBattle(entityPtr)` immediately. Reordered so `mode` is read before `chaseN` is incremented so the short-circuit doesn't leak counts. Initialize log message updated to mention the MODE_ORIGINAL behavior.
- **`src/chase_kani_freeze.cpp`**:
  - `Update`: short-circuit before reading `pGameMode`. Calls `DeactivateFreeze` if `s_freezeActive` is true (handles mid-chase mode swap via INI edit).
  - `RegisterChaseAgent`: short-circuit immediately after the `!entityPtr` early return.
- **`src/chase_ask_overlay.cpp::CommitChoice`**: `ANSWER_ORIGINAL` branch now sets `MODE_ORIGINAL` instead of `MODE_MANUAL`. Comment updated to describe the new behavior and the short-circuits that make it work.

### Expected v0.15.9.10 BAT

Aaron triggers the chase, hears the explainer prompt + three option labels, picks **Original**. The chase plays out as vanilla FF8: chase battles fire (Aaron will hear the battle music and the battle TTS announce them as usual), the X-ATM092 robot pursues and catches the party, the ground shakes on the west trail, etc. None of the mod's chase-specific machinery activates.

For verification in `Logs/ff8_field.log`:
- `ChaseBattleFreeze: Initialized v0.15.9.10 (... MODE_ORIGINAL short-circuits before any chase logic ...)` at startup.
- After the ASK commits Original: `ChaseDetector: chase_mode set to 'original' (persisted)`.
- On chase ACTIVATED: `ChaseDetector: chase ACTIVATED on entry to 'domt4_1' (mode=original, baseline calls=N freezes=0)`.
- Throughout the chase: **no `[CBF]` lines** (PASS or NO-OP) and **no `KaniFreeze:` lines** (no FREEZE ACTIVATED, no CAPTURE STARTED).
- Battles WILL fire and be visible in `Logs/ff8_battle.log` as normal battle activity.

### Risk

Low-medium. Five-file integration but each individual change is small and the short-circuits are at well-defined top-of-hook points. The mode is opt-in via the ASK so default users (MODE_MANUAL / MODE_AUTO) are completely unaffected. Worst case (a chase machinery path was missed and still activates in MODE_ORIGINAL): the user reports unexpected mod behavior in Original mode, we add another short-circuit, and other modes are not affected.

### Files

- `src/chase_detector.h` -- Mode enum + comments.
- `src/chase_detector.cpp` -- LoadChaseModeFromIni + ChaseModeName switch.
- `src/chase_battle_freeze.cpp` -- top-of-chase-branch short-circuit + Initialize log update.
- `src/chase_kani_freeze.cpp` -- Update + RegisterChaseAgent short-circuits.
- `src/chase_ask_overlay.cpp` -- ANSWER_ORIGINAL routes to MODE_ORIGINAL.
- `src/ff8_accessibility.h` -- version bump.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- current state + BAT plan.

## v0.15.9.9.1

Fix duplicate Squall "Let's go!" line at chase ASK open.

### Root cause

v0.15.9.9 BAT confirmed the auto-pilot is fully self-sufficient (0 chase battles fired with `cap=INT_MAX`) and that the new ASK option labels read out correctly, but Aaron still heard the chase-trigger MES `"Forget it!  Let's go!"` announced twice when the ASK opened.

Tracing `Logs/ff8_dialog.log` from the BAT identified the source. At 10:27:54 the chase-trigger MES fires through the AMESW path:

```
[10:27:54] FieldDialog: [AMESW] win[0] Speaking: "Squall "Forget it!  Let's go!""
[10:27:54] FieldDialog: [SHOW_DIALOG-TEXT] win[0] (already spoken by opcode hook)   <- correctly suppressed
```

Three seconds later, when `chase_ask_overlay` opens the chase ASK in slot 2, `field_dialog.cpp`'s `ScanAndSpeakChoiceWindows` (which runs for every `opcode_ask`) iterates over ALL 8 dialog window slots looking for valid ASK choice fields. Slot 2 contains the new chase ASK. Slot 0 still holds Squall's chase-trigger MES from 3 seconds earlier with `firstQ=0xFF lastQ=0xFF` (FF8's "no ASK fields set" sentinel). The existing skip predicates `if (firstQ == 0 && lastQ == 0) continue;` and `if (lastQ < firstQ) continue;` correctly catch the all-zeros sentinel and inverted ranges respectively but miss the `(0xFF, 0xFF)` case. Slot 0 got decoded as a 0-choice dialog with non-empty prompt, and the prompt was spoken as if it were the ASK:

```
[10:27:57] FieldDialog: [ASK] win[0] Parsed 0 choices (firstQ=255 lastQ=255 curChoice=0)
[10:27:57] FieldDialog: [ASK] win[0] Speaking: "Squall "Forget it! Let's go!""   <- THE DUPLICATE
[10:27:57] FieldDialog: [ASK] win[2] Parsed 3 choices (firstQ=1 lastQ=3 curChoice=1)
[10:27:57] FieldDialog: [ASK] win[2] Speaking: "X-ATM092 is heading right for you..."
```

The v0.15.9.9 prompt change only affected slot 2; the duplicate was always coming from slot 0, which is why the new explainer prompt couldn't eliminate it.

### Fix

One-line additive predicate in `src/field_dialog.cpp::ScanAndSpeakChoiceWindows` (around line 681):

```cpp
if (firstQ == 0xFF || lastQ == 0xFF) continue;
```

Windows where either choice-field is FF8's "unset" sentinel are not real ASKs and should be skipped. `0xFF` is unambiguously a sentinel (FF8 dialogs cap at ~16 choices, so neither `firstQ` nor `lastQ` can be `0xFF` in a real ASK).

### Expected v0.15.9.9.1 BAT outcome

Aaron triggers the chase, hears Squall's `"Forget it!  Let's go!"` line **once** via the AMESW opcode hook, then 3 seconds later hears the new ASK prompt + the three option labels with **no duplicate Squall line in between**. Everything else from v0.15.9.9 stays the same: 0 chase battles, full chase completion in Auto mode.

If the duplicate persists, the source isn't the [ASK] handler; investigate `Hook_show_dialog` or `DialogInject::OpenAsk` next.

### Risk

Very low. One-line additive predicate. Worst case (we missed a legitimate ASK with one of the fields set to `0xFF`): the slot's text won't be read out, which is a quieter regression than the duplicate.

### Files

- `src/field_dialog.cpp`: sentinel check in `ScanAndSpeakChoiceWindows` around line 681 with the v0.15.9.9.1 BAT-traced rationale in comments.
- `src/ff8_accessibility.h`: version bump to `0.15.9.9.1` with full top-comment.
- `CHANGELOG.md`: this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: current state + BAT plan.

## v0.15.9.9

VERIFICATION BUILD. Combines two coordinated changes (Aaron's chase-scene items #4 and #1, 2026-05-12):

### Change 1: chase_battle_freeze AUTO cap raised to INT_MAX

The v0.15.9.8.3 BAT recorded 0 `[CBF] NO-OP` lines on the whole chase. That number proves the suppressor didn't fire, but it doesn't prove WHY -- the underlying chase battle calls might have been 0 (chase_auto_pilot doing all the work) or N>0 (suppressor band-aid doing the work, just not recording NO-OPs because cap=0 forces NO-OP before the count is logged).

v0.15.9.9 raises the AUTO-mode cap from 0 to `INT_MAX`, removing the band-aid for one BAT cycle. Any chase battle the auto-pilot fails to avoid will now PASS through to a real battle screen instead of being silently NO-OP'd.

Expected BAT outcomes:

- **Best (proves auto-pilot self-sufficient)**: 0 `[CBF] PASS chase BATTLE call` lines AND 0 NO-OPs on any chase field. The suppressor is provably vestigial in AUTO and can be removed entirely in v0.15.10.
- **Worst (real regression to fix)**: 1+ `[CBF] PASS chase BATTLE call` lines. The log identifies the field and timing; we fix the auto-pilot before retiring the band-aid. Revert the cap constant to 0 in v0.15.9.9.1 to restore the band-aid while investigating.

MANUAL cap stays at 1 (the scripted first-battle-per-field pass-through preserves vanilla MANUAL behavior).

Implementation: one-line constant change in `Hook_opcode_battle` (`int cap = (mode == ChaseDetector::MODE_AUTO) ? INT_MAX : 1;`), `<climits>` added to the include list for `INT_MAX`, and the Initialize log message updated to reflect the new cap value so the field log shows "cap=INT_MAX in AUTO (VERIFICATION BUILD)" at startup.

### Change 2: chase_ask_overlay text revisions

Four string changes in `chase_ask_overlay.cpp` to address Aaron's BAT feedback that the ASK opens with a redundant Squall "Let's go!" announcement (probably the engine re-reading the prior field-dialog slot text when DialogInject's ASK opens in that slot):

- Prompt changed from `"Mode?"` to `"X-ATM092 is heading right for you. How do you want to run?"`. Replaces the redundant Squall line with situational context. If the duplication was coming from the slot's pre-existing text, the new prompt overwrites it. If it's coming from somewhere else (DialogInject internals, field_dialog's show_dialog hook), the new prompt at least replaces the wasted reading time with useful information.
- `kChaseChoices` labels updated to describe what each option does:
  - `"Manual: drive yourself, one battle per field"` (was `"Manual: one battle per field"`).
  - `"Auto: mod drives, no battles or shake"` (was `"Auto: falls back to manual"`, now accurate).
  - `"Original: vanilla chase, no mod help"` (was `"Original: falls back to manual"`).

The Original label promises behavior that requires v0.15.9.10's `MODE_ORIGINAL` to actually deliver. Aaron's call: ship the descriptive label now (sets expectations for the eventual implementation) versus keeping the honest `"falls back to manual"` label until v0.15.9.10. v0.15.9.9 ships the descriptive label; if Aaron changes his mind during BAT, we revert one string in v0.15.9.9.1.

The brief commit announces in `CommitChoice` (`"Manual selected"` / `"Automatic selected"` / `"Original selected"`) stay unchanged -- they were already concise and the new descriptive labels carry the explanation during cursor navigation.

### Verification log markers

- `ChaseBattleFreeze: Initialized v0.15.9.9 (... cap=INT_MAX in AUTO (VERIFICATION BUILD) ...)` at startup.
- `ChaseAskOverlay: chase ASK opened via DialogInject (slot=2, 3 choices, default cursor=1)` when the ASK fires.
- During the chase: ZERO `[CBF]` lines (PASS or NO-OP) on chase fields. If any PASS fires, it's a regression to investigate.

### Risk

Low. Single constant change in chase_battle_freeze, four strings in chase_ask_overlay. Worst case is a real chase battle firing during the BAT, in which case we revert the cap constant to 0 in v0.15.9.9.1.

### Files

- `src/chase_battle_freeze.cpp`: cap constant, log message, `<climits>` include.
- `src/chase_ask_overlay.cpp`: prompt string, `kChaseChoices` labels.
- `src/ff8_accessibility.h`: version bump to `0.15.9.9` with full top-comment.
- `CHANGELOG.md`: this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: current state + BAT plan.

## v0.15.9.8.3

Bridge dance (`domt1_1`) + kani-slot override. Ships two coordinated changes targeting the bridge's remaining catch at X~2053 reported by every BAT from v0.15.9.7.8 onward through v0.15.9.8.2.

### Change 1: ChaseDetector per-field kani-slot override

`chase_detector.cpp` gains `ApplyPerFieldKaniOverride(fieldName)`, called from `OnDebouncedFieldChange` immediately after `ResolveKaniLocation`. For `domt1_1` it overwrites `s_kaniLoc` with `arrayKind=2 (Others)`, `arraySlot=3`, `symIdx=9`, `symName="laguna"`. v0.15.9.8.2 BAT BridgeDiag (416 samples over 6.7 seconds) empirically proved this is the actually-pursuing X-ATM092: Others slot 3 tracked the party throughout the chase and leaped east to land at X=3836 during the catch event, while ChaseDetector's default `"kani"` SYM (Others slot 6) read `(0, 0)` the entire transit. The bridge field reuses a generic NPC SYM template ("laguna") for its chase agent. With the override in place, `ReadKaniPosition()` returns the correct position for the first time on this field, and the per-second chase-pilot tick log will show `kani=(X,Y) kdist=K` populated rather than `kani=(0,0)`.

### Change 2: chase_auto_pilot MODE_BRIDGE_DANCE

New drive mode (`MODE_BRIDGE_DANCE = 3`) used only on `domt1_1`. Two-state machine (EAST_LEG / WEST_LEG) updated at 10Hz via `UpdateBridgeDance(fieldName)`, with the existing per-tick `StartDirectionDrive` refresh path picking up state changes the same tick.

**Detection signals from v0.15.9.8.2 BAT** (laguna at Others slot 3):

- Y is constant (-446) throughout the bridge -- jumps are X-axis only in walkmesh coords. The visual jump is the 3D Z-axis, NOT walkmesh Y. **This refutes the v0.15.9.8.2 Y-axis-divergence hypothesis from Aaron's pre-BAT mechanic description.**
- Normal pursuit speed: ~106 units / 100ms tick.
- Leap speed: ~372 units / 100ms tick (3.5x normal).
- Landed/stationary: 0-1 units / tick.
- Pre-chase stationary phase (laguna at far-west spawn X=-4419): 12 samples (~1.2s) with dX=0 before any motion.

**Thresholds** (in `chase_auto_pilot.cpp`):

- `kBridgeLeapThreshold = 200` units/tick (above this == leaping)
- `kBridgeLandThreshold = 50` units/tick (below this == landed)
- `kBridgeLandConsec = 2` samples (debounce for east-leg landed-ahead trigger)
- `kBridgeMinDwellTicks = 60` (1 second between transitions, anti-oscillation)
- `kBridgeWestTimeoutTicks = 300` (5-second west-leg safety bail back to east)
- `kBridgeSamplePeriodTicks = 6` (10Hz sample cadence, matches v0.15.9.8.2 BridgeDiag)

**State transitions:**

- **EAST_LEG -> WEST_LEG**: kani lands in front of party (`isLanded && kX > pX && wasLeaping == true`) for `kBridgeLandConsec` consecutive samples AND `s_bridgeTicksSinceXition >= kBridgeMinDwellTicks`. The `wasLeaping` latch is essential -- without it, the kani's initial stationary phase at the spawn (12 samples of dX=0) would trip the landed-ahead detector on the very first sample. Logs `BridgeDance: EAST->WEST transition kani=... party=... kdx=...` and updates `s_engagedDirX = -1`.
- **WEST_LEG -> EAST_LEG**: `justStartedLeaping` edge (first sample where `isLeaping && !wasLeaping` -- i.e., a NEW leap just began) AND `minDwellMet`. Logs the transition with leap-count diagnostic. The instant-turn-on-leap-start is the asymmetric rule from Aaron's mechanic: the robot is mid-air during a leap and cannot course-correct, so reversing direction the moment we detect it lets the party slip past while the robot is airborne.
- **WEST_LEG safety timeout**: if no leap fires for `kBridgeWestTimeoutTicks` (5 seconds at 60Hz), force a transition back to east and log `BridgeDance: WEST->EAST TIMEOUT`. Without this, a bridge variant where the robot never re-leaps would leave the party retreating west forever, never reaching the east-edge SETLINE exit at X=4446. Worst-case behavior with timeout firing: identical to v0.15.9.8.2 (1 catch at X=2053, post-catch script teleport to X=3529, field transition fires normally).

**Engage / Disengage / Initialize:** `MODE_BRIDGE_DANCE` is added to `IsDirectionLikeMode()` (so all the existing direction-drive plumbing -- StartDirectionDrive on engage, StopDirectionDrive on disengage, per-tick refresh -- handles it). Engage installs `(+1, 0, false)` (east, running) and zeroes all bridge state. Disengage and Initialize zero state for hygiene.

**Field config:** Explicit `"domt1_1"` entry inserted between `domt5_1` and `doopen2a` in `kFieldConfigs`. Replaces v0.15.9.8.2's fallback config (which used BuildFallbackConfig's INF-gateway path to target the east-edge gateway at X=4446). Path-finding worked OK on this field -- the catch was the only failure -- but BRIDGE_DANCE replaces the path entirely with direction-drive plus state machine.

### Diagnostic continuity

The v0.15.9.8.2 `BridgeDiag` per-tick all-slots dump remains active for one more BAT cycle. Two purposes: (1) confirm the slot-3 kani is consistent across multiple bridge runs, and (2) capture the previously-missing west-leg robot trajectory data (we have no data on what the X-ATM092 does AFTER the party turns west, since v0.15.9.8.2's catch fired before any turn).

### Predicted v0.15.9.8.3 BAT outcomes

- `ChaseDetector: field='domt1_1' v0.15.9.8.3 OVERRIDE -> kani -> Others slot 3 (SYM 'laguna')` log line at field-debounce settle.
- `ChaseAutoPilot: ENGAGED on field='domt1_1' mode=BRIDGE_DANCE initial state=EAST_LEG direction=east running` after the chase ASK is answered.
- `BridgeDance: sample state=... motion=... kani=(X,Y) party=(X,Y) kdx=N ...` at 10Hz.
- `BridgeDance: leap #1 STARTED state=EAST kani=(X,Y) party=(X,Y) kdx=N` ~5 seconds into bridge.
- `BridgeDance: EAST->WEST transition` ~1 second after that when laguna lands east of party.
- **Best case**: `BridgeDance: WEST->EAST transition` on a second leap, 0 catches recorded for `domt1_1` in `[CBF] count by field`, bridge transit ~10-15 seconds.
- **Acceptable**: `BridgeDance: WEST->EAST TIMEOUT` after 5s if no second leap; 0-1 catches; still progresses through the bridge.
- **Worst**: some unforeseen interaction (e.g., turning west violates a script invariant we don't know about). BridgeDiag captures the slot positions throughout, so post-BAT analysis can characterize whatever went wrong.

### Risk

Medium. The east-leg detection is fully calibrated from v0.15.9.8.2 empirical data. The west-leg detection is inferred from Aaron's verbal description with no empirical backing yet -- the BAT will give us that data. The 5-second west-leg timeout backstops any failure mode where the robot doesn't re-leap.

### Files

- `src/chase_detector.cpp`: `ApplyPerFieldKaniOverride` + call site in `OnDebouncedFieldChange`.
- `src/chase_auto_pilot.cpp`: `MODE_BRIDGE_DANCE` enum, `BridgeDanceState` type, `kBridgeXxx` constants, `s_bridgeXxx` state vars, `UpdateBridgeDance` helper, `Engage`/`Disengage`/`Initialize` handling, explicit `domt1_1` config in `kFieldConfigs`, `ENGAGED` log case for the new mode, `IsDirectionLikeMode` update, Initialize log message update, mode-string update in the per-second tick log, UpdateBridgeDance call in the engaged-tick refresh path.
- `src/ff8_accessibility.h`: version bump to `0.15.9.8.3` with extensive top-comment.
- `CHANGELOG.md`: this entry.
- `DEVNOTES.md`: v0.15.9.8.2 BAT BridgeDiag findings and v0.15.9.8.3 dance design.
- `NEXT_SESSION_PROMPT.md`: BAT plan for v0.15.9.8.3.

## v0.15.9.8.2

DIAGNOSTIC-ONLY build for the bridge (`domt1_1`). No behavior change. v0.15.9.8.1 BAT brought total chase catches to 1 (just the mid-east-traversal catch on the bridge). To eliminate that catch we need to implement Aaron's bridge dance, which requires reliable kani position reads -- but the v0.15.9.8.1 BAT log shows `kani=(0,0)` on every per-tick log throughout the bridge traversal even though ChaseDetector resolved kani's symbol to a slot. This build adds a per-tick 10Hz diagnostic on `domt1_1` that enumerates every non-zero entity slot in both `pFieldStateOthers` and `pFieldStateBackgrounds` so we can find the actually-pursuing kani.

### Why the existing resolution fails on the bridge

v0.15.9.8.1 BAT field-log entries on `domt1_1`:

```
[23:18:17] ChaseDetector: field='domt1_1' kani symIdx=12 -> Others slot 6 (doors=0 lines=4 bgs=2 others=17)
[23:18:18] ChaseActiveDiag: ... pos=(-3738,-576) ... kani=(0,0) kdist=3782
[23:18:19] ChaseActiveDiag: ... pos=(-3025,-607) ... kani=(0,0)
[23:18:20] ChaseActiveDiag: ... pos=(-2096,-607) ... kani=(0,0)
[...every subsequent tick the same...]
```

With `kani=(0,0)` on every tick we cannot drive the asymmetric dance Aaron described (turn west when robot lands ahead during east leg; turn east immediately when robot jumps during west leg). The detection logic needs `kani.Y` to compare against `party.Y` -- a Y-match indicates "robot landed on the bridge corridor" (east-leg turn signal); a Y-divergence indicates "robot is airborne / off corridor" (west-leg turn signal). Both signals require reading the actually-moving entity, not whatever Others slot 6 holds on this field.

Two possible root causes the diagnostic distinguishes:

1. **Wrong array.** The bridge's kani entity may live in `pFieldStateBackgrounds` rather than `pFieldStateOthers` (chase_diag.cpp explicitly notes "kani is always in pFieldStateBackgrounds across every chase field" -- but ChaseDetector's resolution mapped to Others on this field, suggesting either a counts-table mismatch or that the bridge truly is the exception).
2. **Wrong slot.** The kani entity may be in the Others array but at a different slot than slot 6 -- perhaps an entity whose SYM name doesn't match the "kani" / "Kani" pattern ChaseDetector greps for (could be a generic chase agent name, a numbered slot, or a slot the field reuses for the actively-pursuing entity).

Either way, dumping every non-zero slot in both arrays per tick produces a position trajectory we can correlate with the party's east-traversal trajectory: the entity whose X tracks the party with a consistent offset is the robot.

### Diagnostic implementation

`chase_auto_pilot.cpp::LogBridgeDiagnostic(fieldName)`:

- Hard-gated to `fieldName == "domt1_1"`. No-op on every other field.
- Reads party position via the existing `ReadSquallPosition` helper.
- Enumerates `pFieldStateOthers` slots 0..count-1, SEH-guarded, stride 0x264. Reads X at +0x190, Y at +0x194 (matching the `ReadKaniPosition` pattern). Skips slots whose computed pos is `(0, 0)` to keep the log concise.
- Enumerates `pFieldStateBackgrounds` slots 0..count-1, SEH-guarded, stride 0x1B4. Same offset/skip rules.
- For each surviving slot, looks up the SYM name via `ChaseDetector::GetSymName(symIdx)` where `symIdx = bgStart + slot` for Backgrounds and `othersStart + slot` for Others. For `domt1_1` the BAT-confirmed counts give `bgStart=4`, `othersStart=6` -- hard-coded here since the diagnostic only runs on this field.
- Emits one `Log::Field` line per non-zero slot:

```
BridgeDiag: field='domt1_1' party=(pX,pY) array=Others slot=I sym='X' pos=(eX,eY) ydelta=N
```

`ydelta = entity.Y - party.Y` is included on every line so the v0.15.9.8.3 threshold analysis can read it directly. On the bridge corridor (party.Y ~= -607), an entity tracking the party will show `ydelta` near zero when landed and far from zero when airborne.

Called from the per-tick refresh path in `Update()` at 10Hz (every 6 Update ticks). A new `s_bridgeDiagTick` static counter drives the cadence; reset in `Initialize()` and `Disengage()`. The 10Hz cadence is chosen so a brief jump (estimated ~0.5s) reliably lands in 4-5 samples while keeping log volume bounded (~17 active slots * 10Hz * ~10s of bridge transit = ~1700 lines max per BAT, filtered to non-zero = probably ~300-500 lines actually emitted).

### Expected BAT output

On the bridge (~10 seconds of traversal, ~100 per-tick dumps at 10Hz), the diagnostic should reveal:

- **One or two slots tracking the party's east trajectory.** Those are the actual chase entities. The X delta to party will be roughly constant (the robot maintains a chase distance); the Y delta varies as the robot jumps and lands.
- **Several static slots** at non-zero positions. These are scenery/director entities -- ignorable.
- **Y-delta excursion magnitudes.** During jumps, by how much does the entity Y diverge from the corridor (Aaron's audio of robot landings can correlate with BAT timestamps to identify exact landing moments).

With this data v0.15.9.8.3 can:

1. Patch `ChaseDetector` (or add a bridge-specific override) to resolve kani to the correct slot/array on `domt1_1`.
2. Set Y-delta thresholds for "landed" (small) and "airborne" (large) based on the BAT's empirical jump amplitude.
3. Implement the asymmetric dance in `chase_auto_pilot.cpp` with per-tick (not per-second) detection of the Y-match and Y-divergence signals.

### Risk

Very low. Pure additive logging, no behavior change, hard-gated to one field. SEH-guarded per array. Other chase fields, all menu/battle/world states, and ordinary field navigation are completely unaffected.

### Files

- `src/chase_auto_pilot.cpp` -- `LogBridgeDiagnostic` helper, `s_bridgeDiagTick` counter, call site in per-tick `Update()` refresh path, reset in `Initialize()` and `Disengage()`, Initialize log mention.
- `src/ff8_accessibility.h` -- version `0.15.9.8.2`.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- current state updated to v0.15.9.8.2 READY-TO-BAT.

## v0.15.9.8.1

**BAT SUCCESS 2026-05-12.** Town square (`doopen2a`) now produces zero `[CBF]` catches. Total chase catches: **1** (down from 2 in v0.15.9.8, 3 in v0.15.9.7.8, 11 in the v0.15.9.2.18 baseline). Only the mid-traversal bridge catch on `domt1_1` remains.

### v0.15.9.8.1 BAT result on doopen2a

TargetY bumped from -3703 to -3800 (97 units past the south trigger line). A* found a path further south than v0.15.9.8 -- party reached pos `(-955, -3672)`, just 31 units north of the trigger line at y ~= -3703. Field transitioned cleanly 3 seconds later, before the chase script's catch evaluator window expired.

Comparison v0.15.9.8 vs v0.15.9.8.1 (south-stop position):

```
v0.15.9.8   (tgt=-3703): party stopped at (-958, -3541)  -- 162 units N of trigger -- 1 catch fired during 8s wait
v0.15.9.8.1 (tgt=-3800): party stopped at (-955, -3672)  --  31 units N of trigger -- 0 catches, transitioned in 3s
```

The walkmesh DID extend further south than v0.15.9.8's stop point; A* was conservatively pulling its goal back from the requested target. Asking for a target further south let A* find waypoints further south. The 31-unit residual gap to the trigger line was close enough that either the party crossed it during the final south burst between 1Hz log samples, or the engine's trigger detection accepts the walkmesh-edge stop as a crossing.

### v0.15.9.8.1 BAT [CBF] catch counts by field

```
domt4_1   (chase start)                       0 catches
domt3_2   (intermediate)                       0 catches
domt5_1   (west trail)                         0 catches
domt2_1   (between west trail and bridge)      0 catches
domt1_1   (bridge)                             1 catch  -- mid-east-traversal ~23:18:25
doopen2a  (town square)                        0 catches ✨ -- the v0.15.9.8.1 win
dotown_3                                        0 catches
dotown_2                                        0 catches
dotown_1  (final pre-FMV)                      0 catches (stuck at (-210,-1000); extract ends here)
---------------------------------------------------------
Total                                          1 catch
```

### Cumulative progression

```
v0.15.9.2.18 (baseline)            11 catches
v0.15.9.7.8  (west trail fix)       3 catches  (-8)
v0.15.9.8    (town square partial)  2 catches  (-1)
v0.15.9.8.1  (town square clean)    1 catch    (-1)
```

91% reduction from baseline.

### Remaining open items (deferred)

1. **Bridge catch.** Kani at `(0, 0)` (unresolved), so this isn't proximity-based -- looks like a periodic time-based catch evaluator structural to the bridge's chase script. v0.15.9.8.2 investigation if pursued: can bridge transit be faster than the evaluator's window?
2. **dotown_1 stuck at (-210, -1000).** Unresolved across v0.15.9.7.8 / .8 / .8.1 BATs. Pending Aaron's confirmation of whether the Lapin Beach FMV eventually fires after the extract ends.

### Files

- `src/chase_auto_pilot.cpp` -- `doopen2a` entry's `targetY` already updated in v0.15.9.8.1 to `-3800`; no further code changes for the BAT-success checkpoint.
- `src/ff8_accessibility.h` -- version `0.15.9.8.1` (already bumped).
- `CHANGELOG.md` -- this entry replacing the pre-BAT v0.15.9.8.1 entry.
- `DEVNOTES.md` -- current state updated to v0.15.9.8.1 BAT SUCCESS.

## v0.15.9.8

Explicit MODE_TARGET config for `doopen2a` (Dollet Town Square) -- target the south exit gateway. The v0.15.9.7.8 BAT [CBF] count by field showed 2 catches on `doopen2a` (the only remaining heavy-catch field besides `domt1_1`'s 1 mid-bridge catch). Investigation of the v0.15.9.7.8 field log found that the chase auto-pilot was steering the party in the wrong direction.

### v0.15.9.7.8 BAT diagnosis on doopen2a

Field log at 22:22:07 shows the field's gateway data at load time:

```
FieldArchive: INF parsed: 0 active gateways for 'doopen2a'
FieldNavigation: [SETLINE] call#21 field=doopen2a line(-1432,660,0)->(-1349,1048,0) idx=1 center=(-1390,854)
FieldNavigation: [SETLINE] call#22 field=doopen2a line(-814,-3717,0)->(-1091,-3689,0) idx=257 center=(-952,-3703)
```

Zero INF gateways. Two SETLINE trigger lines: one NW at center `(-1390, 854)`, one SOUTH at center `(-952, -3703)`. With INF gateways unavailable, the `BuildFallbackConfig` chain dropped from tier 1 (INF-GATEWAY) to tier 2 (TRIGGER-LINE). The trigger-line selection then went wrong:

```
FieldNavigation: GetTriggerLineNearestCluster matched cluster(-1315,503)
                 -> trigger[0] center=(-1390,854) distFromCluster=359
ChaseAutoPilot: fallback config built for field='doopen2a' mode=TARGET
                tgt=(-1390,854) walk=0 running TRIGGER-LINE idx=0
                (cross-product detection)
```

The walkmesh's largest dead-end cluster on this field lands at `(-1315, 503)` -- an interior corner in the NW area, not an exit. `GetTriggerLineNearestCluster` matched that cluster to the NW trigger (359 units away) instead of the SOUTH trigger (~4200 units away). So the auto-pilot targeted NW.

Aaron's authoritative recipe (2026-05-12): *"The Town Square does require the party to keep running. You mostly head down from where you enter the field in order to get to the next."* Party spawns at `~(-784, -474)`; the field needs the party to run south to cross the SOUTH trigger at `y ~= -3700`. The NW target was the opposite direction.

The mis-targeted run produced a brief north movement at 22:22:10, the catch evaluator fired its first window during that movement, the script's post-catch knockback teleported the party south to `(-1042, -917)` (the suspicious dmag=1187 in a single tick), the A* path was now stale, and the party sat stuck for ~9 seconds while the chase script eventually forced a field transition at 22:22:21. Two catches fired during the stuck window (22:22:10 and 22:22:15).

### Why the cluster heuristic broke here

`GetTriggerLineNearestCluster` assumes the walkmesh's largest dead-end cluster correlates with the field exit direction. That assumption holds on corridors and narrow trails where the biggest walkmesh feature is the exit funnel. It breaks on fields with interior architectural features (town squares, plazas, building corners) where the biggest walkmesh dead-end is a non-exit -- like the NW corner on `doopen2a`.

The deeper fix would be to re-score trigger lines by direction-from-spawn or by INF-destination-field-IDs, but per-field explicit configs are surgical enough for the remaining problem fields.

### The fix

New explicit MODE_TARGET config entry for `doopen2a` between `domt5_1` and `dotown_2` in `kFieldConfigs[]` (matching chase-route order):

```cpp
{ "doopen2a", MODE_TARGET,
  /*dirX=*/0, /*dirY=*/0,
  /*targetX=*/-952, /*targetY=*/-3703,
  /*walk=*/false,
  /*stages=*/nullptr, /*stageCount=*/0 },
```

Targets the SOUTH SETLINE trigger center directly. Running (walk=false) per Aaron's recipe. The point-distance arrival radius (~60 units per v0.15.9.2.13) is small enough that the party crosses the trigger line at `y ~= -3700` during travel even if they stop short of the target. If a future BAT shows the party arriving at target without crossing, bump `targetY` to `-3800`.

### Predicted v0.15.9.8 BAT outcome

On entry to `doopen2a` (after the bridge `domt1_1` transitions), field log should show:

```
ChaseAutoPilot: ENGAGED on field='doopen2a' mode=TARGET tgt=(-952,-3703) running (walk=0)
```

With NO `fallback config built` line for doopen2a (because the explicit config matches before fallback runs). Party runs south at running speed (~900 units/sec), crosses the south trigger line at `y ~= -3700` after roughly 4-5 seconds, field transitions cleanly to `dotown_3`. **0 catches on doopen2a.** Total chase route catches drop from 3 to 1 (the remaining catch is on `domt1_1` bridge, mid-traversal).

If the party arrives at the target without crossing the trigger (point-distance arrives before line-crossing), the AD will Disengage early and the chase script's natural transition timer takes over. This would still likely give 0 catches because the party would already be at the south end of the field, well clear of the kani.

### Risk

Very low. Single config-array entry added; matches the established pattern of explicit configs for `domt4_1`, `domt3_2`, `domt5_1`, `dotown_2`, `dotown_1`. The fallback path used previously was producing the wrong result; bypassing it on this specific field can only improve things. If the explicit target is also wrong somehow, a v0.15.9.8.1 follow-up adjusts the coordinates.

### Files

- `src/chase_auto_pilot.cpp` -- new `doopen2a` config entry with full diagnostic comment block.
- `src/ff8_accessibility.h` -- version `0.15.9.8` with rationale.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated for v0.15.9.8 ready-to-BAT state.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- Finding #34 (cluster heuristic limitation on fields where the largest walkmesh feature isn't the exit).

## v0.15.9.7.8

**BAT SUCCESS 2026-05-12.** Aaron: "BAT. It worked! We successfully made it down the west trail by walking and without triggering the robot."

After 14 versions of investigation, the `domt5_1` (west trail) catch is fixed. Field log confirms the full chase route completed end-to-end: `domt4_1` → `domt3_2` → `domt5_1` → `domt2_1` → `domt1_1` → `doopen2a` → `dotown_3` → `dotown_2` → `dotown_1`. The chase progressing past `domt5_1` to `dotown_1` (the last chase field) is itself proof — every prior BAT either got caught and respawned or got stuck on the west trail.

**Root cause finally found.** `InjectKey` in `field_nav_autodrive.inl` was setting `KEYEVENTF_EXTENDEDKEY` for every key. Arrow keys ARE extended (E0 prefix in hardware); letter keys like W are NOT. So every W injection from v0.15.9.7 onward produced a malformed scancode `E0+0x11` that FF8's DirectInput keyboard reader didn't recognize as W. **Arrows worked the whole time. W never did, since the day we first tried to inject it.**

Full credit to Aaron for the diagnosis after the v0.15.9.7.7 BAT.

### v0.15.9.7.7 BAT trace (2026-05-12 21:58:55 onward) -- the smoking gun

All v0.15.9.7.7 markers confirmed present in `Logs/ff8_field.log`:

- `FRESH START walk=1 -- keyboard-only path (no fake gamepad, no analog override)` ✓
- `FRESH-START W KEYDOWN injected (scancode=0x11)` ✓
- `STARTED dir=(-1,1) walk=1 lX=0 lY=0 arrows=0x6 override=0 gamepad=0` ✓
- `walk modifier RE-PRESSED (tick #1 ..., period=1, override=0, gamepad=0)` through tick #1321 ✓

v0.15.9.7.7 ran exactly as designed. Pure keyboard. No fake gamepad. No analog override. But:

- `dmag=756` to `855` per second on the walking stages (your manual is `270-303`).
- `[CBF] NO-OP chase BATTLE call` fired three times: at 21:59:00, 21:59:07, 21:59:13. The mod's freeze blocked the actual battles, but the chase script's catch trigger went off three times. "Robot showed up" was real.

Aaron's verbatim diagnosis:

> "I am quite certain it is W. I suspect the W key press is not making its way to the game. You also mentioned that we're repressing W on every tick, but we should be holding W down continuously."

Both right.

### The bug

`field_nav_autodrive.inl`:

```cpp
static void InjectKey(WORD scanCode, bool down)
{
    INPUT inp      = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = 0;
    inp.ki.wScan   = scanCode;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY  // <-- ALWAYS extended
                   | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
}
```

`KEYEVENTF_EXTENDEDKEY` unconditionally. Correct for arrows; wrong for W. Our own `world_map.cpp` v0.14.102 comment from earlier flagged this exact issue for car driving:

> "CRUCIAL DETAIL: A and W are NOT extended keys (per v0.11.14: 'NOT extended keys (no KEYEVENTF_EXTENDEDKEY)'); arrow keys ARE extended. The key-injection helpers must handle both flag types."

`world_map.cpp` has its own `PressKey`/`ReleaseKey` with an `extended` parameter that was fixed in v0.14.102. But the field-navigation `InjectKey` (a separate function) was never updated.

### The fix

Four parts:

1. **`InjectKey` gains an `extended` parameter** (default `true` preserves backward compat for every existing arrow-key call site). When `false`, `KEYEVENTF_EXTENDEDKEY` is omitted.
2. **W call sites in `field_nav_directiondrive.inl` now pass `extended=false`** — three sites: fresh-start, walk-state flip in the already-running branch, and `StopDirectionDrive`.
3. **Defensive W re-press path REMOVED.** Per Aaron's holding-vs-tapping point. The re-press was added in v0.15.9.7.1 on an unverified theory that the chase intro choreography swallowed the initial KEYDOWN. The real reason it didn't reach the engine was the extended-key bug. With the bug fixed, one KEYDOWN should suffice and the OS holds the state until our `KEYUP` at `Disengage`. Re-pressing 60 times per second was potentially counterproductive in DirectInput buffered mode — 60 discrete "tap" events instead of one sustained hold.
4. **`InjectKey` now logs SendInput failures** (return value != 1). Drops are silent today; future drops will surface in the field log.

### Sequence summary

| Version | Walk-mode input | Result |
|---|---|---|
| v0.15.9.7 - .7.4 | analog 1000 + W (broken: extended flag) | Caught (analog ran the show) |
| v0.15.9.7.5 | analog 350 + W (broken) | Caught (still ran) |
| v0.15.9.7.6 | analog 0 with fake gamepad + W (broken) | Caught (still ran) |
| v0.15.9.7.7 | no fake gamepad + W (broken) | Caught (W never reached engine; ran at full keyboard speed) |
| **v0.15.9.7.8 (this)** | **no fake gamepad + W (fixed extended=false, held continuously)** | **Predicted: walks, 0 catches** |

### Predicted v0.15.9.7.8 BAT outcome

- Field log: `FRESH-START W KEYDOWN injected (scancode=0x11, extended=0) -- will be held continuously until Disengage (no re-press)` once at engagement on `domt5_1`. **No subsequent W re-press lines.**
- Audio: walking-speed footsteps on `domt5_1` from the first audible step.
- `domt5_1` transit ~13-18s with dmag ~270-303 matching manual play.
- **0 catches. No robot announcement.**
- Full chase completes cleanly: domt4_1 → domt3_2 → domt5_1 → ... → dotown_1.
- If a `[InjectKey] SendInput FAILED` line ever appears, we'd know our input was dropped at the OS level (separate from any FF8-side issue).

If the catch STILL fires after this fix, the input speed isn't the trigger — some other chase-script predicate is at play, and the next investigation goes to FF8 disasm / FFNx source.

### Risk

Very low. `InjectKey` signature change is backward-compatible (default `extended=true`). All existing arrow-key callers behave identically (verified: `SetHeldDirections` and `ReleaseAllDirections` only call `InjectKey` with arrow scancodes). Only the three W sites are updated. The removed re-press code path was the noisy one; clean deletion. The `WALK_REPRESS_PERIOD` constant and `s_walkRepressCounter/Logged` statics remain in the file as vestigial documentation of the history but are no longer referenced.

### Files

- `src/field_nav_autodrive.inl` -- `InjectKey` gains `extended` parameter + SendInput failure logging.
- `src/field_nav_directiondrive.inl` -- W call sites pass `extended=false` + defensive re-press path removed + comment block rewrite explaining the root cause.
- `src/ff8_accessibility.h` -- version `0.15.9.7.8` with full comment trail.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- current state.
- `NEXT_SESSION_PROMPT.md` -- BAT plan.

## v0.15.9.7.7

Walk mode uses pure keyboard input: no fake gamepad install, no analog override. Plus diagnostic logging. Aaron's challenge after v0.15.9.7.6 drove the change.

### v0.15.9.7.6 BAT result (2026-05-12) -- FAILED

Aaron: "Still triggered the robot on the west trail. It sounded like the party was running. Are you sure W was being held the whole time and that only directional keys were sent to the game?"

Honest answer to his question: I wasn't 100% sure. The code calls `InjectKey(W, KEYDOWN)` at field entry and re-presses every tick. The only place that calls `KEYUP` is `StopDirectionDrive` which doesn't fire mid-field. So in theory W is held. But we never verified SendInput's return value, never logged subsequent re-presses (only the first), and never logged the engine's perceived keyboard state. Intent != evidence.

### The likely cause

Even with the analog magnitude at 0, the **fake gamepad was still installed**. FF8 PC likely has an input-mode flag that flips based on gamepad presence:

- Gamepad attached: analog magnitude is the speed authority. W treated as a keyboard-only modifier the analog code path ignores.
- No gamepad: keyboard + W is the speed authority. W applies.

Aaron's manual play has no gamepad attached. Manual `domt5_1`: keyboard arrows + W, 13s, 0 catches every time. Our fake gamepad install (even with zeroed analog) may have flipped the engine into "gamepad mode", making it ignore W for speed determination while still reading keyboard for direction.

### The fix

In `src/field_nav_directiondrive.inl`'s `StartDirectionDrive`:

- **Walk=true path**: skip `DD_InstallFakeGamepad()`, leave `s_analogOverrideActive=false`. Press keyboard arrows + W. Pure keyboard, exactly like manual play.
- **Walk=false path**: unchanged (install fake gamepad, full analog deflection).
- **Walk-state flip mid-engagement**: run → walk uninstalls the fake gamepad and disables override; walk → run reinstalls and re-enables.

### Re-evaluating the v0.15.9 "keyboard alone doesn't work" finding

The v0.15.9 BAT shipped keyboard-only injection with no fake gamepad. Party didn't move. v0.15.9.1 added the fake gamepad and party moved. We've assumed since then that the fake gamepad install is REQUIRED for any movement.

But Aaron's manual play with no gamepad moves the party fine. So keyboard CAN drive movement in FF8 PC. The v0.15.9 failure was probably a different bug (timing, focus, scancode) that's since been fixed by other refinements (the keep-alive pulse, the `SetHeldDirections` diff mechanism). We didn't isolate at the time. v0.15.9.7.7 re-evaluates empirically.

### Added diagnostic logging

To stop guessing whether W is reaching the engine:

- Every W press/release logs the scancode (`0x11`).
- The fresh-start W KEYDOWN logs explicitly.
- Walk-state flip logs the path swap (install/uninstall).
- W re-press logs every 60 ticks (~once per second), including current override/gamepad flag state, instead of the v0.15.9.7.1 latched "first re-press only" log.
- `STARTED` log now includes `override=N gamepad=N` to show input path.

Noisy by design for this BAT. Restored to the latched form after diagnosis.

### Risk

Medium. If the v0.15.9-era finding is still operative (some path in FF8 needs a gamepad pointer present to dispatch keyboard input), the party won't move on `domt5_1` at all. Fallback: v0.15.9.7.8 re-installs the fake gamepad but freezes its state (no analog writes) so it has presence without influence.

If the party walks and the catch still fires, the chase script overrides speed regardless of input. Next step: FF8 disassembly / FFNx source for the catch predicate.

### Predicted v0.15.9.7.7 BAT outcome

- **Success path**: `domt5_1` log shows `STARTED dir=(-1,+1) walk=1 lX=0 lY=0 arrows=0x06 override=0 gamepad=0`. Party walks. 0 catches. Per-second re-press log confirms W KEYDOWN firing throughout.
- **Fallback path A**: party doesn't move on `domt5_1`. v0.15.9 finding still operative.
- **Fallback path B**: party walks but catch still fires. Chase script ignores W; deeper investigation.

### Files

- `src/field_nav_directiondrive.inl` -- conditional fake-gamepad install + walk-state flip handler + diagnostic logging.
- `src/ff8_accessibility.h` -- version `0.15.9.7.7` with full comment trail.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- current state.
- `NEXT_SESSION_PROMPT.md` -- BAT plan.

## v0.15.9.7.6

Set `WALK_ANALOG_MAGNITUDE` from 350 to 0. Aaron's pivot after the v0.15.9.7.5 BAT cuts through everything.

### v0.15.9.7.5 BAT result (2026-05-12) -- FAILED

Aaron's report: "Still ran and triggered the robot. Should we consider just disabling analog navigation and using just directional keys when walking is required?"

Magnitude 350 was still above whatever threshold triggers the catch. We don't actually know whether the threshold is a speed value (and our 350 is above it), or whether the catch evaluator reads something else entirely from the analog path (e.g., "is analog non-zero"). Either way, the fix is the same: don't write any analog when walking.

### Aaron's suggestion

Mirror manual play exactly. Aaron has no joystick. His manual play on `domt5_1` uses keyboard arrows + W only, no analog stick input at all. Reliably walks the field in 13 seconds with 0 catches every time. Our auto-pilot should do the same shape of input when walking.

### The fix

One-line constant change in `src/field_nav_directiondrive.inl`:

```cpp
static const int WALK_ANALOG_MAGNITUDE = 0;   // v0.15.9.7.6: zero deflection
```

When `walk=true`, both analog-write sites in `StartDirectionDrive` now write `lX=0, lY=0`. The fake gamepad stays installed (its presence is what activates FF8's input dispatch path -- v0.15.9 BAT without it showed the party didn't move at all). The engine sees "gamepad present, stick centered" and falls back to keyboard for direction.

Keyboard arrows + W modifier do all the work. Same shape as Aaron's manual play.

### Why this is more likely to work

The sequence so far:

| Version | Analog | W | Result |
|---|---|---|---|
| v0.15.9.7 - .7.4 | 1000 (run) | held | Caught (analog = run) |
| v0.15.9.7.5 | 350 (partial) | held | Caught (still ran) |
| **v0.15.9.7.6 (this)** | **0 (none)** | **held** | **Predicted: walks, no catch** |

The trajectory says: every non-zero magnitude triggered the catch. Setting to zero is the only untested value, and it matches Aaron's known-working manual play.

### Risk

Very low. One-line constant change. The walk=false path is untouched -- `domt4_1`, `domt3_2`, `dotown_*`, and the fallback configs all keep running at full deflection.

### Fallback if even magnitude=0 doesn't work

If the party doesn't move at all (the centered gamepad + active analog override might block keyboard reading), v0.15.9.7.7 disables `s_analogOverrideActive` entirely for walk fields. The fake gamepad stays installed (presence matters for input dispatch) but the engine reads from the saved-pointer gamepad state instead of our zeroed fake. That should be functionally equivalent to no gamepad being active for input purposes.

If v0.15.9.7.7 also fails, the next step is investigating FF8's actual input dispatch logic via the disassembly or FFNx source.

### Predicted v0.15.9.7.6 BAT outcome

- Field log: `[direction-drive] STARTED dir=(-1,+1) walk=1 lX=0 lY=0 arrows=0x06` (note `lX=0 lY=0`, with the arrow mask still showing the keyboard intent).
- Audio: walking-speed footsteps on `domt5_1` from the first audible frame.
- `domt5_1` transit ~13-18s.
- **0 catches. No robot announcement.**
- Full chase completes cleanly.

### Files

- `src/field_nav_directiondrive.inl` -- constant value change + comment block rewrite explaining the strategy pivot.
- `src/ff8_accessibility.h` -- version bump to `0.15.9.7.6` with full comment trail.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- current state.
- `NEXT_SESSION_PROMPT.md` -- v0.15.9.7.6 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- Finding #32 updated with v0.15.9.7.5 result + the pivot rationale.

## v0.15.9.7.5

Reduce analog deflection magnitude when `walk=true`. This is the long-missing fix for the `domt5_1` (west trail) catch issue. Credit to Aaron for the diagnosis after the v0.15.9.7.4 BAT.

### Aaron's insight (verbatim)

> "Okay I think this was an improvement, at least for the second field in the chase, as I heard the party run across that field as expected now. We still somehow triggered the robot on the west trail. Are we emulating directional keys or the analog stick? Could it be that the way we are emulating the navigation is not respecting the W key being held down to walk? I know the directional keys respect that, but not sure if the analog stick does."

He nailed it. FF8 PC walk/run determination depends on input source:

- **Keyboard arrows + W (Cancel modifier)**: W asserts walking speed regardless of arrow deflection. This is the path Aaron uses in manual play.
- **Analog stick**: speed is determined by **deflection magnitude**. Full deflection = run; partial deflection = walk. The W modifier doesn't apply (or barely applies) to the analog path -- the PSX original used analog magnitude exclusively, and the PC port inherited that code path.

Our direction-drive emulates BOTH simultaneously:

- Writes fake-gamepad analog at full deflection (`lX = dirX * 1000, lY = dirY * 1000`)
- Injects keyboard arrows + W via SendInput

The engine reads both, but the **catch evaluator on chase fields reads analog magnitude**. Full deflection = running, no matter how many W re-presses we fire on the keyboard.

This is the actual root cause of every `domt5_1` catch since v0.15.9.7. Walking footstep audio (Aaron's main feedback signal) is driven by W + keyboard arrows, so it sounded correct. The catch evaluator was on a different code path.

### v0.15.9.7.4 BAT result (2026-05-11/12)

Aaron: "this was an improvement, at least for the second field in the chase, as I heard the party run across that field as expected now. We still somehow triggered the robot on the west trail."

Good news on `domt3_2`: the direction fix (east -> west) landed correctly. Aaron heard the party running across the field as the recipe required. Per the v0.15.9.7.4 carryover-hypothesis test design, this rules carryover OUT as the cause of `domt5_1`'s catch. The catch is something else.

Aaron's question then pinpointed the something else.

### The fix

Two constants added to `src/field_nav_directiondrive.inl`:

```cpp
static const int RUN_ANALOG_MAGNITUDE  = 1000;  // full deflection = running
static const int WALK_ANALOG_MAGNITUDE = 350;   // ~35% deflection = walking
```

Both analog-write sites in `StartDirectionDrive` (the fresh-start branch and the already-running branch) select `walk ? WALK_ANALOG_MAGNITUDE : RUN_ANALOG_MAGNITUDE` instead of the hardcoded `1000`. No other changes to logic.

### Magnitude calibration

- PSX FF8 analog range was `-128..+127`. Walk threshold was around analog value 50.
- Scaled to our DirectInput convention `-1000..+1000`: that's `50/127 * 1000 = ~394`.
- **350** stays solidly below the threshold (with some margin for engine variance).
- For diagonal directions like SW (`dirX=-1, dirY=+1`), the vector magnitude is `sqrt(350^2 * 2) = ~495`, still well below the run-threshold vector magnitude of `sqrt(394^2 * 2) = ~557`. Diagonals walk too.
- The FF8 PC analog deadzone is small (~100 units), so 350 is well above it. Party should still move.

If 350 doesn't move the party at all (deadzone hit), v0.15.9.7.6 bumps to 400-500. If 350 still results in running (catch fires), v0.15.9.7.6 drops to 250.

### Why we still hold W

Belt and suspenders:

1. If FF8 does ANY walk-modifier consultation on the analog path (e.g., "W held forces walking regardless of magnitude"), we still cover that case.
2. Walking animation and footstep audio are driven by W + keyboard arrows. Aaron's accessibility depends on audio cues to gauge speed and progress.

### Why running fields are unchanged

`domt4_1` SE, `domt3_2` W, `dotown_2/dotown_1` S, and the fallback configs all have `walk=false`. They continue to use `RUN_ANALOG_MAGNITUDE = 1000` and clear their fields quickly. No behavior change for any running field.

### Predicted v0.15.9.7.5 BAT outcome

- `[direction-drive] STARTED dir=(-1,+1) walk=1 lX=-350 lY=350 arrows=0x06` (note the new magnitudes in the log).
- Audio: walking-speed footstep cadence (unchanged; was already correct).
- `domt5_1` transit ~13-18s, matching Aaron's manual.
- **0 catches on `domt5_1`.** No robot announcement.
- Full chase completes cleanly: domt4_1 -> domt3_2 -> domt5_1 -> domt2_1 -> domt1_1 -> doopen2a -> dotown_3 -> dotown_2 -> dotown_1.

If the catch still triggers at magnitude 350, it's something other than input speed entirely. v0.15.9.7.6 then investigates the chase script's actual catch predicate via FF8 disasm or FFNx source (both already in project knowledge).

### Risk

Very low. Two constants added; two write-site changes select the new constant when `walk=true`. Walk=false fields untouched. Existing log line continues to show `lX`/`lY` values, now reflecting the chosen magnitude.

### Files

- `src/field_nav_directiondrive.inl` -- WALK/RUN magnitude constants + two write-site changes + new header comment block documenting Aaron's insight and the fix rationale.
- `src/ff8_accessibility.h` -- version bump to `0.15.9.7.5` with full comment trail.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- current state.
- `NEXT_SESSION_PROMPT.md` -- v0.15.9.7.5 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- Finding #32 (analog magnitude is the walk/run controller on the FF8 PC analog input path; W modifier doesn't apply there).

## v0.15.9.7.4

Revert v0.15.9.7.3's Y-axis flip on `domt5_1` (the west trail) AND fix `domt3_2` direction from east to west. Aaron's 2026-05-11 clarified recipe surfaced two distinct issues this version addresses together.

### v0.15.9.7.3 BAT FAILED 2026-05-11

Aaron's report: "AD got stuck when we reached the west trail. I tried to manually move to the next field but couldn't find it for some reason."

v0.15.9.7.3 had set `domt5_1` to `MODE_DIRECTION` with `(dirX=-1, dirY=-1)` -- screen-up-left. The party stuck at spawn because that direction opposes the script's down-trail flee force in a way the walkmesh can't override.

### Aaron's correction

> "The first field, where the chase actually begins, is good to go.
> The second field, between the starting field and the west trail, you said has the character going east when it should be west, northwest, west.
> This latest build broke the AD on the west trail. It should be heading generally southwest, south, southeast."

Aaron uses cardinal directions consistently in his terminology. Translation to our analog convention (DirectInput from `field_nav_input_hooks.inl`):

| Aaron's word | Arrow keys | (dirX, dirY) |
|---|---|---|
| southwest | Down + Left | (-1, +1) |
| south | Down | (0, +1) |
| southeast | Down + Right | (+1, +1) |
| west | Left | (-1, 0) |
| northwest | Up + Left | (-1, -1) |
| north | Up | (0, -1) |
| northeast | Up + Right | (+1, -1) |
| east | Right | (+1, 0) |

Aaron's earlier "LEFT and slightly UP" message that I interpreted as screen-up-left was actually a description of position-on-the-trail (upper part of the trail toward spawn), not screen-direction. The cardinal-direction recipe he just clarified is the authoritative ground truth and I should have asked him to translate the earlier phrasing into cardinal directions before shipping v0.15.9.7.3.

### v0.15.9.7.4 source changes (two config entries)

**`domt3_2`**: flip dirX from `+1` (east) to `-1` (west).

```cpp
{ "domt3_2", MODE_DIRECTION,
  /*dirX=*/-1, /*dirY=*/ 0,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/false,
  /*stages=*/nullptr, /*stageCount=*/0 },
```

The v0.15.9.5 BAT "success" on this field (5s / 1 catch) was misattributed in the v0.15.9.5 comments to script-forced kani co-location. We now think it was a direction-conflict catch firing in 5 seconds because we were pushing east while Aaron's actual recipe is west. Aaron's manual trace on `domt3_2` (2026-05-11 20:14-20:20): 2 seconds of travel, 851 world-units west, 0 catches.

Aaron's full recipe is "west, northwest, west" -- a three-stage pattern. v0.15.9.7.4 ships single MODE_DIRECTION west to fix the gross direction error; if the NW middle stage matters for catch avoidance, v0.15.9.7.5 adds a staged direction table here too. The field is only ~2 seconds in Aaron's manual so single direction likely suffices.

**`domt5_1`**: revert to `MODE_STAGED_DIRECTION` with `kStages_domt5_1[]` -- the exact v0.15.9.7 / .7.1 / .7.2 config.

```cpp
{ "domt5_1", MODE_STAGED_DIRECTION,
  /*dirX=*/-1, /*dirY=*/+1,  // initial fallback direction (matches stage 0)
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/true,
  /*stages=*/kStages_domt5_1, /*stageCount=*/kStages_domt5_1_count },
```

The stages encode SW=(-1,+1) -> S=(0,+1) -> SE=(+1,+1) with Y thresholds 2200 and 1100 -- exactly Aaron's recipe.

### The carryover hypothesis now becomes testable

Aaron's pushback after v0.15.9.7.2 was: "Did you check the possibility if the prior field is somehow carrying over?" At the time we dismissed it in favor of a direction-conflict theory; v0.15.9.7.3 was supposed to test that theory and failed.

v0.15.9.7.4 sets up the actual carryover test:

- `domt3_2` going east while Aaron presses west was a sustained direction conflict for ~5 seconds.
- That may have primed some script state (kani aggression timer, chase-fighting flag, etc.) that carried into `domt5_1` and triggered the catch even with correct staged direction and walking.
- Fixing `domt3_2` to go west removes the priming.
- If `domt5_1` now gets 0 catches with the same staged-direction config that was failing in v0.15.9.7.2, carryover was the cause.
- If catches still fire on `domt5_1` after both fixes, the catch trigger is independent of direction state. v0.15.9.7.5 would then investigate whether SendInput injection vs physical-key input is read differently by the catch script.

### What v0.15.9.7.4 expects

- `domt3_2`: transit ~2-3s (matching Aaron's manual) with 0 catches. Log shows `ENGAGED on field='domt3_2' mode=DIRECTION direction=WEST RUNNING (dirX=-1 dirY=0 walk=0)`.
- `domt5_1`: transit ~13-18s. Ideally 0 catches if carryover hypothesis is right. Log shows `ENGAGED on field='domt5_1' mode=STAGED_DIRECTION ... starting stage 0/3 direction=SOUTHWEST WALKING (dirX=-1 dirY=+1 walk=1)`. Stage transitions log at Y=2200 and Y=1100.
- No AD stuck. Party reaches each field's exit.

### Risk

Very low. Two config-table edits. domt3_2 is a single dirX sign flip. domt5_1 reverts to a config that has navigated successfully in three prior BATs (v0.15.9.7, .7.1, .7.2). The `kStages_domt5_1[]` table itself is unchanged. `WALK_REPRESS_PERIOD=1` from v0.15.9.7.2 stays in place as defensive coverage.

### Files

- `src/chase_auto_pilot.cpp` -- `domt3_2` dirX flip + `domt5_1` revert + comment trail.
- `src/ff8_accessibility.h` -- version bump to `0.15.9.7.4` with full comment trail.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- current state.
- `NEXT_SESSION_PROMPT.md` -- v0.15.9.7.4 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- Finding #31 (terminology translation requirement) + Aaron's cardinal-direction table near the top.

## v0.15.9.7.3

Flip the Y axis on `domt5_1` (west trail). The v0.15.9.7.2 BAT confirmed walking reached the engine (audible footsteps from the first frame, walking cadence throughout) but the robot still triggered. Aaron's pushback surfaced the root cause: the analog direction was inverse of his manual recipe on the Y axis. His manual play uses screen-up-left; our config had screen-down-left. The walkmesh was narrow enough that the party navigated to the exit anyway (script-forced motion wins when analog disagrees with the script direction), so the BAT "looked like it worked" geometrically. But the engine evidently reads the analog-vs-script direction conflict as a catch trigger, independent of the W (walk) modifier state. Walking didn't help because the catch isn't on running.

### v0.15.9.7.2 BAT result (2026-05-11) — PARTIAL

Aaron heard walking-speed footsteps on `domt5_1` from the first audible frame. The defensive re-press at period=1 worked exactly as designed. But the robot still appeared. Aaron's framing:

> "Still had the robot appear on the west trail. Did you check the possibility if the prior field is somehow carrying over? You mentioned that the mod is pushing east on that field, but when I played through manually I went mostly LEFT and slightly UP."

The correction in his words: he physically presses LEFT + slightly UP on his keyboard. That maps to analog `(lX=-1000, lY=-1000)` = screen-up-left in the DirectInput convention from `field_nav_input_hooks.inl`. Our v0.15.9.7 stage 0 had `(dirX=-1, dirY=+1)` = screen-down-left. **The Y axis was flipped.**

### Root cause analysis

Reinterpreting Aaron's manual playthrough trace through this lens:

- Aaron's keyboard input: LEFT + UP.
- World position result: `(-1004, 3253) → (655, 123)`. ΔX = +1659 (world-east), ΔY = -3130 (world-Y-decreasing).
- So screen-LEFT → world-X-increasing (east), and screen-UP → world-Y-decreasing (toward exit).

This field's camera is **rotated/inverted on the Y axis**. Screen-UP maps to the direction the trail goes (Y-decreasing). The X-axis is also inverted (screen-LEFT = world-east), established in the v0.15.9.6 analysis.

When our mod set `dirY=+1` (screen-down) it asked the engine to move the party AWAY from the exit (world-Y-increasing, back up the trail). The walkmesh is narrow, so the script's forced "flee down the trail" motion still drove the party to the exit. But the engine reads the analog-vs-script direction conflict as something equivalent to "player is fighting the chase" and triggers the robot.

The v0.15.9.6 BAT looked different (got stuck on the east wall) because it had BOTH axes wrong: `(dirX=+1, dirY=+1)` = screen-down-right = world-west + world-Y-increasing. Stuck immediately, never reached exit. v0.15.9.7's flip to `dirX=-1` fixed the X-axis but left Y wrong; the walkmesh saved us geometrically but not from the catch trigger.

### The fix

Simplify `domt5_1`'s config from `MODE_STAGED_DIRECTION` (three stages with `dirY=+1`) to `MODE_DIRECTION` (single direction with `dirY=-1`):

```cpp
{ "domt5_1", MODE_DIRECTION,
  /*dirX=*/-1, /*dirY=*/-1,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/true,
  /*stages=*/nullptr, /*stageCount=*/0 },
```

Aaron's manual trace shows ONE direction throughout the 13-second traversal (dmag 267-303 walking, no transitions, net world path X+/Y- which the walkmesh forces regardless of analog X sign). The `MODE_STAGED_DIRECTION` machinery from v0.15.9.7 stays in place (`enum FieldDriveMode`, `struct FieldStage`, `kStages_domt5_1[]`, branches in `Engage`/`Update`) but is unused for `domt5_1`. Other fields may need stages later.

The `WALK_REPRESS_PERIOD=1` change from v0.15.9.7.2 stays in place as defensive coverage against W swallow on field-load.

### Predicted v0.15.9.7.3 BAT outcome

- `ENGAGED on field='domt5_1' mode=DIRECTION direction=NORTH-WEST WALKING (dirX=-1 dirY=-1 walk=1)` log (or however the direction-name lookup formats up-left).
- `[direction-drive] STARTED dir=(-1,-1) walk=1` log.
- `[direction-drive] walk modifier RE-PRESSED (defensive, period=1 ticks)` within ~17ms of STARTED.
- Audio: walking footstep cadence from the first audible footstep.
- Transit ~13-18s. **0 catches on domt5_1.**
- No robot appearance.

### Risk

Low. One config-table entry edited: mode, dirY sign, stages pointer.

If the X-axis interpretation was ALSO wrong (unlikely given v0.15.9.6 with `dirX=+1, dirY=+1` got stuck immediately while v0.15.9.7 with `dirX=-1, dirY=+1` navigated), v0.15.9.7.4 tries `(dirX=+1, dirY=-1)` or the remaining quadrants empirically. The stage machinery is available if a single direction can't cover the field after all, but Aaron's trace strongly suggests single direction works.

### Files

- `src/chase_auto_pilot.cpp` — `domt5_1` config rewritten; long header comment block updated with v0.15.9.7.3 rationale.
- `src/ff8_accessibility.h` — version bump to `0.15.9.7.3` with full comment trail.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md` — current state.
- `NEXT_SESSION_PROMPT.md` — v0.15.9.7.3 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` — Finding #30 (analog-vs-script direction conflict triggers the catch; W modifier is orthogonal).

## v0.15.9.7.2

Reduce `WALK_REPRESS_PERIOD` from 30 to 1 in `field_nav_directiondrive.inl`. The v0.15.9.7.1 BAT shipped period=30 and Aaron heard walking footsteps on `domt5_1` (the defensive re-press worked from ~0.5s onwards), but the robot still triggered. Aaron's manual playthrough trace captured via the new `dump_chase_events` utility shows the engine commits the running-vs-walking decision in the very first frame or two of field-load, well before period=30 re-press fires.

### v0.15.9.7.1 BAT result (2026-05-11) — PARTIAL

Auto-pilot navigated the S-curve geometry on `domt5_1` correctly and the chase progressed through `dotown_2` and `dotown_1` (continuing the v0.15.9.7 result). Audio confirmed walking-speed footsteps on `domt5_1`. But the robot still appeared, indicating a catch fired despite the walking audio.

Aaron's framing:

> "It sounded like walking on the west trail, but somehow we still triggered the robot to appear."

### Ground-truth manual playthrough trace

A new `Utilities/dump_chase_events.vbs` utility filters the 100k+ line `ff8_field.log` (mostly SCRIPT-DUMP verbosity) down to chase-relevant lines only, writing `Logs/chase_events_extract.log`. Aaron ran a full manual chase playthrough at 20:14-20:20 with Original mode (no auto-pilot engagement), so the per-second `ChaseActiveDiag PRE-ENGAGE` lines captured his actual path.

Key trace on `domt5_1` (20:19:05-20:19:18, 13 seconds, 0 catches):

```
20:19:05  pos=(-1004,3253)  START
20:19:06  pos=(-988,2965)   delta=(16,-288)   dmag=288
20:19:07  pos=(-949,2682)   delta=(39,-283)   dmag=285
20:19:08  pos=(-806,2428)   delta=(143,-254)  dmag=291
20:19:09  pos=(-787,2154)   delta=(19,-274)   dmag=274
20:19:10  pos=(-737,1871)   delta=(50,-283)   dmag=287
20:19:11  pos=(-575,1642)   delta=(162,-229)  dmag=280
20:19:12  pos=(-356,1432)   delta=(219,-210)  dmag=303
20:19:13  pos=(-236,1193)   delta=(120,-239)  dmag=267
20:19:14  pos=(-142,936)    delta=(94,-257)   dmag=273
20:19:15  pos=(63,721)      delta=(205,-215)  dmag=297
20:19:16  pos=(261,533)     delta=(198,-188)  dmag=273
20:19:17  pos=(497,362)     delta=(236,-171)  dmag=291
20:19:18  pos=(655,123)     delta=(158,-239)  dmag=286   EXIT
```

**Every dmag is 267-303 — walking speed from the very first measured frame.** Aaron's physical W press is held BEFORE the engine evaluates running-vs-walking on field-load. Net path is roughly south-east throughout (ΔX=+1659, ΔY=-3130, ratio ~1:2 east-to-south), no SW→S→SE transitions — the field is one direction continuously in screen-coords (which with this field's inverted-X camera means world south-east, established v0.15.9.6). The v0.15.9.7 stage transitions are essentially harmless (the walkmesh forces the same path) but not what makes walking succeed.

### Root cause refinement

The engine commits the running-vs-walking decision very early on field-load — within the first frame or two after `Engage` fires. Aaron's physical W press is asserted BEFORE that decision point. v0.15.9.7.1's `InjectKey` at fresh-start was either queued behind the engine's first read OR the script's intro reset keyboard state before the press could land. First-frame keyboard state seen by the engine: "W up" = run. The re-press at t~500ms came too late — the catch flag was already set, even though the walking-speed motion that followed was audible to Aaron.

### The fix

One-line constant change in `field_nav_directiondrive.inl`:

```cpp
-static const int WALK_REPRESS_PERIOD = 30;
+static const int WALK_REPRESS_PERIOD = 1;  // v0.15.9.7.2: was 30 in v0.15.9.7.1
```

Re-press every tick (~17ms cadence at 60fps). Worst-case latency from field-entry to engine-sees-W-down drops from ~500ms to ~17ms. If the engine's catch-eval window is wider than one frame, this lands W press inside it. KEYDOWN on a held key is idempotent at the OS level (Windows treats as auto-repeat at the input layer; engine re-acknowledges press intent; no glitches).

Cost: 60 SendInput calls per second while walking. SendInput is microsecond-cheap. Repeated KEYDOWN on a held key has no semantic effect at the engine layer — the keyboard state buffer reflects "W is currently down" regardless of how many times the KEYDOWN event fires.

### Why other fields are unaffected

The re-press branch is guarded by `else if (s_directionDriveWalk)` — it only fires when walk is currently held. All other chase fields (`domt4_1`, `domt3_2`, `dotown_2`, `dotown_1`, `domt2_1`, `domt1_1`, `doopen2a`) have `walk=false`, so `s_directionDriveWalk` stays `false` on those fields and the re-press branch is dead code. **No behavior change for any field other than `domt5_1`.**

### Bonus: chase-event extraction utility

New `Utilities/dump_chase_events.vbs` (and its `.ps1` worker) filter `Logs/ff8_field.log` down to chase-relevant lines only — `ChaseActiveDiag`, `ENGAGED`, `[CBF]`, `fieldId changed`, `walk modifier`, etc. — writing `Logs/chase_events_extract.log` (~100 KB, fits in a single tool call). Lets Claude read full chase traces without hitting the 1 MB tool-result ceiling against the SCRIPT-DUMP bulk. Reusable for any future BAT analysis.

### Predicted v0.15.9.7.2 BAT outcome

- `[direction-drive] walk modifier RE-PRESSED (defensive, period=1 ticks)` log within ~17ms of `STARTED` (vs ~500ms in v0.15.9.7.1).
- Audio: walking-speed footstep cadence from the first audible footstep.
- domt5_1 transit ~15-18s with 0 catches matching Aaron's manual (13s, 0 catches).
- No robot appearance during `domt5_1`.

If the robot still triggers despite period=1, the engine reads keyboard at the exact load frame BEFORE any SendInput dispatch can land — even 17ms is too late. v0.15.9.7.3 would then keep W held across chase-field transitions so the engine never sees a W-up frame on field-load (plumb `keepWalkModifier` param through `StopDirectionDrive`, change `ChaseAutoPilot::Disengage` at field-change to pass `true`).

### Risk

Very low. One-line constant change. All other behavior unchanged. Other fields' re-press branch is dead code.

### Files

- `src/field_nav_directiondrive.inl` — `WALK_REPRESS_PERIOD = 1`, expanded header comment block documenting v0.15.9.7.1 history and v0.15.9.7.2 rationale.
- `src/ff8_accessibility.h` — version bump to `0.15.9.7.2` with full comment trail.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md` — current state.
- `NEXT_SESSION_PROMPT.md` — v0.15.9.7.2 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` — Finding #29 (engine commits running/walking decision on first frame; modifier-key timing).
- `Utilities/dump_chase_events.vbs` + `Utilities/dump_chase_events.ps1` — chase event extraction utility (added 2026-05-11).

## v0.15.9.7.1

Defensive W (walk modifier) re-press in `field_nav_directiondrive.inl`'s `StartDirectionDrive` "already running" branch. The v0.15.9.7 BAT navigated the S-curve geometry on `domt5_1` correctly but the party ran instead of walked, the mountain shook, and the robot caught up. Aaron's audio confirmed running footsteps and quick robot arrival. The stage table and engage logic were correct; the W KEYDOWN never reached the engine.

### v0.15.9.7 BAT result (2026-05-11 19:35) — wrong speed

Build succeeded at 19:35:05. Chase fired and `MODE_STAGED_DIRECTION` engaged on `domt5_1`. The geometry was navigated (the BAT eventually reached `dotown_2` at 19:39:07 and `dotown_1` at 19:39:22, which means `domt5_1` was traversed). But Aaron heard the running-speed footstep cadence and the kani arrived on the timer. Aaron's exact framing:

> "Somehow we triggered the robot on the west trail. Remember we need to be holding down the W key to walk on this field. The robot appeared quickly as if we'd run and running on this field triggers it to appear."

### Root cause analysis

Code review of the path the W press takes:

1. `kStages_domt5_1[]` declares all three stages with `walk=true`. ✅
2. `ChaseAutoPilot::Engage` MODE_STAGED_DIRECTION branch picks the active stage by Y, calls `FieldNavigation::StartDirectionDrive(stg->dirX, stg->dirY, stg->walk)`. With spawn Y≈3300 > 2200, stage 0 is picked with `walk=true`. ✅
3. `StartDirectionDrive` fresh-start branch fires once on field entry. It calls `InjectKey(SC_W_CANCEL_DD, true)` and sets `s_directionDriveWalk = true`. ✅ (in theory)
4. Every subsequent tick, `Update`'s per-tick refresh re-calls `StartDirectionDrive(s_engagedDirX, s_engagedDirY, s_engagedWalk)`. Now `s_directionDriveActive` is `true`, so we hit the "already running" branch.
5. The W diff check there is `if (walk != s_directionDriveWalk)`. Both sides are `true`. **No-op**. ❌

If the initial `InjectKey` at step 3 was swallowed (the chase script's intro choreography on `domt5_1` entry runs camera pan, mountain shake setup, and kani teleport during the first ~1 second of field life and can absorb keyboard events from the SendInput queue before the engine processes them), then the engine sees W as never-pressed for the entire field while our state insists it's held.

The arrow keep-alive pulse (added v0.15.9.1.1) re-fires arrow KEYDOWNs every 30 ticks to keep "movement intent" fresh. The header comment block of this file already noted that W is NOT pulsed because toggling it would cause one frame of run-speed motion between release and re-press. The defensive fix below is press-only — it never releases — so the speed-glitch concern doesn't apply.

### The fix

In `field_nav_directiondrive.inl`:

- New constants `WALK_REPRESS_PERIOD = 30` (matches the arrow keep-alive cadence, ~0.5s at 60fps).
- New state vars `s_walkRepressCounter` and `s_walkRepressLogged`.
- New `else if (s_directionDriveWalk)` clause in the "already running" branch's walk handling. Fires only when walk state is already true and didn't just flip. Increments the counter and re-fires `InjectKey(SC_W_CANCEL_DD, true)` every `WALK_REPRESS_PERIOD` ticks. **No release between presses** — KEYDOWN on a held key is idempotent at the OS level; Windows treats it as auto-repeat at the input layer; the engine re-acknowledges the modifier press intent.
- The walk-state-flip branch also resets the counter and log latch (so future MODE_STAGED_DIRECTION fields with mixed walk/run stages work cleanly).
- Counter and log latch reset on fresh-start and on `StopDirectionDrive`.

### Logging

First re-press logs once at INFO with the period value: `[direction-drive] walk modifier RE-PRESSED (defensive, period=30 ticks); suppressing further re-press logs until next walk-state flip`. Subsequent re-presses suppressed via `s_walkRepressLogged` latch. Avoids 60+ log lines per field for the diagnostic while still providing one definitive proof-of-fire signal.

### Why this is safe for other fields

Aaron confirmed only `domt5_1` should walk; every other chase field has `walk=false`. The re-press branch is guarded by `else if (s_directionDriveWalk)` — it only fires when walk is currently held. On `domt4_1`, `domt3_2`, `dotown_2`, and `dotown_1`, `s_directionDriveWalk` is `false` and the branch is dead code. **No behavior change for any field other than `domt5_1`.**

### Predicted v0.15.9.7.1 BAT outcome

- `ENGAGED on field='domt5_1' mode=STAGED_DIRECTION starting stage 0/3 direction=southwest WALKING (dirX=-1 dirY=1 walk=1)` log line.
- `[direction-drive] STARTED dir=(-1,1) walk=1` log line.
- `[direction-drive] walk modifier RE-PRESSED (defensive, period=30 ticks)` log line on the first re-press (~0.5s after engage).
- Audio: walking-speed footstep cadence on the trail, not running cadence.
- Transit under 18 seconds, target under 6 seconds (Aaron's manual play).
- 0–2 catches depending on transit time.

If the party still runs after this fix, the `InjectKey` path itself is being suppressed by something deeper — a different root cause requiring SendInput-layer investigation (e.g. check whether the engine has a keyboard-input lockout during chase script intro that affects only specific keys).

### Risk

Very low. Single file changed (`field_nav_directiondrive.inl`). All other fields are walk=false and never enter the re-press branch. Walk-state flips still work via the existing diff branch. Reverting is a single-edit operation.

### Files

- `src/field_nav_directiondrive.inl` — defensive re-press counter + state vars + branch.
- `src/ff8_accessibility.h` — version bump to `0.15.9.7.1` with comment trail entry.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md` — current state.
- `NEXT_SESSION_PROMPT.md` — v0.15.9.7.1 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` — Finding #28 (input swallowing by chase script choreography).

## v0.15.9.7

Add `MODE_STAGED_DIRECTION` drive mode and apply it to `domt5_1` (west trail). The v0.15.9.6 BAT failed because the trail is S-shaped, not straight: held south-east at spawn drove the party into the east wall within one second, then froze for ~30 seconds. Single-direction `MODE_DIRECTION` cannot navigate the geometry — whatever direction is held is wrong for at least one of the three segments.

### v0.15.9.6 BAT failure analysis (2026-05-11 18:31-18:34)

Engaged at spawn `(-1057, 3301)` with held south-east analog `(+1000, +1000)`.

- 18:34:08: moved 31 units east to `(-1026, 3301)` — drove into east wall immediately.
- 18:34:09 through 18:34:14: frozen at `(-1026, 3301)` for 7 seconds.
- 18:34:15: micro-shift to `(-1004, 3321)`, then frozen for 22 more seconds.
- 18:34:37–41: brief bursts of motion ending at `(-751, 2325)`.
- 18:34:42+: frozen at `(-751, 2325)` for the rest of the BAT.

Party never reached the south exit. Aaron's keyboard recipe confirmed the trail geometry:

> "very southwest initially, then go south, then south east at the end."
>
> "if you press down at first you won't move, you have to press both down and left."

At spawn the trail runs south-west; pressing south-east drives into the east wall. After that the trail straightens to pure south through the middle, then bends east toward the south-east exit. Three segments, three different directions. No single fixed direction can navigate that.

### MODE_STAGED_DIRECTION

A new drive mode that switches direction part-way through a field based on the party's current Y position. Each staged field declares an array of `FieldStage` entries:

```cpp
struct FieldStage {
    int8_t  dirX;        // screen-relative direction sign, {-1, 0, +1}
    int8_t  dirY;        // screen-relative direction sign, {-1, 0, +1}
    bool    walk;        // hold W (walk modifier) during this stage
    int32_t activeMinY;  // stage is active when party Y >= this value
};
```

Stages are listed in DECREASING `activeMinY` order. `PickStageIdx` walks the array and returns the first stage whose `activeMinY` is `<= party Y`. The last stage has `activeMinY = INT32_MIN` so it always matches as a fallback.

At every Update tick the per-tick refresh re-picks the stage by current Y. If the picked index differs from the previously-active one, `s_engagedDirX/Y/Walk` are updated and the existing `StartDirectionDrive` call on the next line picks up the new analog/arrow values via its idempotent "already running" branch (diff-based `SendInput`, see `field_nav_directiondrive.inl`). No stop/restart needed; the keep-alive pulse cycle continues uninterrupted across transitions.

### domt5_1 stages

```cpp
static const FieldStage kStages_domt5_1[] = {
    { -1, +1, true, 2200 },        // SW until Y < 2200
    {  0, +1, true, 1100 },        // S until Y < 1100
    { +1, +1, true, INT32_MIN },   // SE for the rest
};
```

Thresholds derived from BAT evidence:

- v0.15.9.1.1 BAT showed pure south froze at Y=2217 — the trail bends out of the south-only corridor at that point. SW must extend past 2217; picked 2200 for the SW→S transition.
- SETLINE call#14 center `(-366, 1110)` marks the final bend toward the south-east exit. South-only would fail past this point; picked 1100 for the S→SE transition.
- Spawn cluster center Y~3500; exit cluster center Y~235.

Aaron uses keyboard arrow keys (not analog). His "very southwest" is literally Down+Left pressed together. That maps directly to `(dirX=-1, dirY=+1)` — the arrow bitmask `DD_DirsToArrowMask` produces is exactly `DIR_DOWN | DIR_LEFT`.

### Implementation summary

All changes in `src/chase_auto_pilot.cpp`:

- New enum value `MODE_STAGED_DIRECTION = 2`.
- New `FieldStage` struct.
- `FieldConfig` extended with `stages` pointer and `stageCount`. Existing config entries get explicit `nullptr, 0`.
- `kStages_domt5_1[]` array (3 stages) inserted before `kFieldConfigs[]`.
- `domt5_1` entry switched from `MODE_DIRECTION dirX=+1 dirY=+1` to `MODE_STAGED_DIRECTION` with `stages=kStages_domt5_1`.
- New state: `s_engagedStages`, `s_engagedStageCount`, `s_currentStageIdx`.
- New helpers: `IsDirectionLikeMode()` (treats `MODE_DIRECTION` and `MODE_STAGED_DIRECTION` together) and `PickStageIdx()` (finds active stage for a Y position).
- `Engage()` extended with the `MODE_STAGED_DIRECTION` branch (reads Y, picks initial stage, calls `StartDirectionDrive`). State-assignment block updated to copy stage params into `s_engagedDirX/Y/Walk` and `stages/stageCount` into the cached engaged state. Third log branch added for staged-mode engagement message.
- `Disengage()` converted to use `IsDirectionLikeMode` for the `StopDirectionDrive` call and resets stage state.
- Per-tick refresh in `Update()` re-picks the stage from current Y and logs transitions when the index changes. Existing `StartDirectionDrive` call picks up the new values cleanly.
- Diagnostic tick log shows `mode=STAGED` and the current `stage=N` index.
- `LogChaseActiveDiagnostic` displays `ENGAGED-STG` for staged engagements.
- `Initialize()` resets the new state and announces v0.15.9.7 with `domt5_1 STAGED_DIRECTION walk SW->S->SE by Y`.

No other source changes. Drive mechanism (`StartDirectionDrive`) and its keep-alive pulse cycle are unchanged; staged mode is a thin selection layer above it.

### Predicted v0.15.9.7 BAT outcome

Party walks SW from spawn through the first bend, transitions to S at Y=2200, continues to Y=1100, transitions to SE for the final approach to the south-east exit. Transit under 18 seconds. Catches on this field fire roughly every 6 seconds while the party is still on the field, so:

- Transit <6s → 0 catches.
- Transit 6–12s → 1 catch.
- Transit 12–18s → 2 catches.

Aaron's manual play target is the under-6s bracket. If a stage's direction still freezes the party at a wall, adjust direction signs or Y threshold for that stage in v0.15.9.7.1 — stages are isolated, changing one doesn't affect the others.

### Risk

Low. Drive mechanism unchanged. New code is purely the stage-selection layer. Other fields (`domt4_1`, `domt3_2`, `dotown_2`, `dotown_1`) are unaffected. Reverting to v0.15.9.6 behavior is a one-line config change.

### Files

- `src/chase_auto_pilot.cpp` — enum value, struct, helpers, state, engagement/refresh/disengage branches, log updates.
- `src/ff8_accessibility.h` — version bump to `0.15.9.7` with comment trail entry.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md` — current state.
- `NEXT_SESSION_PROMPT.md` — v0.15.9.7 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` — Finding #27 (S-curve trails require staged directions).

## v0.15.9.6

Switch `domt5_1` (west trail) from `MODE_TARGET` path-finding to `MODE_DIRECTION dirX=+1 dirY=+1 walk=1` (WALK SOUTH-EAST). The biggest remaining catch source on the chase route. Driven by Aaron's recipe for this field: walk straight through, head directly for next field exit, no hangups, no delay.

### Aaron's recipe for domt5_1

The west trail is governed by AI rule #1: running causes mountain shake which catches the party. But the field has a second mechanic on top: catches are TIME-based, not distance-based. If the party walks straight through fast enough, the robot doesn't catch up. If running OR taking too long on this field, the mountain shakes and catches fire.

Four requirements distilled from Aaron's recipe:

1. **Walk** (AI rule #1 -- running causes mountain shake catches).
2. **Direct to exit** (sustained motion toward where the exit lives).
3. **No hangups** (no velocity-stuck pauses, no waypoint re-evaluation).
4. **No delay** (no startup overhead like CALIB).

### Why the previous MODE_TARGET violated three of four

v0.15.9.2 through v0.15.9.5 used `MODE_TARGET (382,235) walk=1`. Field log analysis from v0.15.9.5 BAT:

- ✅ **Walked** (`walk=1` honored)
- ❌ **Direct**: path-finder computed a 28-waypoint winding A* path with funnel smoothing. Each waypoint required steering, re-evaluation, possible re-routing.
- ❌ **No hangups**: log shows `velocity-stuck (stuckDist=0 < 20), skipping wp X/28, advancing to wp Y/28` events at waypoints 11, 15, and 26. Each velocity-stuck event burns 1-2 seconds while the path-finder figures out the next waypoint.
- ❌ **No delay**: CALIB on this field had both phases attempt calibration but produced inconsistent basis (`camRight=(0.07, 0.998)`, `camDown=(0, -1)` -- not orthogonal because script motion contaminated the measurement). The ~1 second of CALIB was wasted.

Result: 30+ seconds total transit, 3 catches (each ~6 seconds apart, time-based).

### Why MODE_DIRECTION walking south-east satisfies all four

1. ✅ **Walk**: `walk=1` -- unchanged.
2. ✅ **Direct to exit**: sustained analog `(+1000, +1000)` toward south-east where the exit lives. Spawn `(-1057, 3301)` to target `(382, 235)` is `+1439 east, -3066 south` -- mostly south with east component. Screen south-east is the natural mapping.
3. ✅ **No hangups**: `MODE_DIRECTION` has no path-finder waypoint state machine. No `velocity-stuck` recoveries, no waypoint advancement, no re-routing.
4. ✅ **No delay**: `MODE_DIRECTION` skips CALIB entirely. Engagement is immediate.

### Direction choice: dirX=+1, dirY=+1 (screen south-east)

Three reasons:

1. **World direction is south-east.** Spawn-to-target vector `(+1439, -3066)`.
2. **v0.15.9.1.1 BAT documented pure-south failure.** That earlier attempt used `dirX=0, dirY=+1` (screen pure south) and the party froze at `(-769, 2217)` -- about halfway down the trail. The trail bends east at that point in the walkmesh; pure south hits the wall. South-east adds an east component to push around the bend.
3. **CALIB is unreliable on this field.** Script motion contaminates the measurement, producing a non-orthogonal basis. South-east is the best heuristic; BAT validates empirically.

### Time-based catch math

Catches on `domt5_1` fire roughly every 6 seconds while party is on the field. With 30+ seconds, 3 catches. To get to 0 catches, transit must be under ~6 seconds.

- v0.15.9.5 baseline: 32s = 3 catches
- v0.15.9.6 prediction:
  - Best case (under 6s, matches Aaron's manual play target): **0 catches**
  - Good case (6-12s): **1 catch**
  - Acceptable case (12-18s): **2 catches**
  - Worse case (18-24s): **3 catches** (no improvement, would revert)

Given Finding #25's ~15x analog-as-force-multiplier observation on cooperating script-driven fields, the under-6s case is plausible. The chase script on `domt5_1` presumably wants the party to go south-east anyway (toward the exit); our analog reinforces that.

### The fix

One config-array entry edited in `src/chase_auto_pilot.cpp`. The `domt5_1` entry was previously `MODE_TARGET` with target `(382, 235)` walk=1. Now:

```cpp
{ "domt5_1", MODE_DIRECTION,
  /*dirX=*/+1, /*dirY=*/+1,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/true },
```

The comment block on the entry was rewritten (~50 lines) documenting Aaron's recipe, the four requirements, why each mode satisfies or violates them, the direction-choice rationale, and the time-based catch math. Top-of-file comment block gains v0.15.9.6 entry. v0.15.9.5 entry retroactively annotated with BAT result. Initialize log message text updated to reflect current config table.

### What v0.15.9.6 does NOT change

- Drive-mode logic unchanged.
- v0.15.9.3's diagnostic instrumentation retained.
- Other chase fields' configs untouched.
- cap=0 safety net unchanged.

### Risk and fallback

**Risk:** the party may freeze at a different wall than the v0.15.9.1.1 pure-south freeze. The walkmesh has bends; if south-east hits a different bend's wall, the party stops and catches accumulate at the time-based rate.

**Fallback plan for v0.15.9.7** if v0.15.9.6 BAT shows freezing:

- Try `dirX=+1, dirY=0` (screen east only)
- Try `dirX=0, dirY=+1` (pure south -- known to fail per v0.15.9.1.1, kept here for completeness)
- Try `dirX=-1, dirY=+1` (screen south-west)
- Revert to MODE_TARGET (last resort)

Each experiment is one BAT and a one-line config edit.

### Files changed

- `src/chase_auto_pilot.cpp` -- one config-array entry switched mode and direction; entry's comment block rewritten with ~50 lines of v0.15.9.6 rationale; top-of-file comment block gains v0.15.9.6 entry; v0.15.9.5 entry retroactively marked BAT successful; Initialize log line updated.
- `src/ff8_accessibility.h` -- version `0.15.9.6` with comment trail entry; v0.15.9.5 entry annotated with BAT result.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- v0.15.9.6 ready-to-BAT.
- `NEXT_SESSION_PROMPT.md` -- v0.15.9.6 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- Finding #26 (time-based catch fields + Aaron's recipe + four-requirement diagnostic).

## v0.15.9.5

Explicit `MODE_DIRECTION` config for `domt3_2` (RUN EAST). Second chase field refined with the three-signal pattern. v0.15.9.4 BAT was a major success on `domt4_1` (3 catches/16s → 0 catches/3s) and validated the pattern. Refinement continues chronologically.

### v0.15.9.4 BAT result (2026-05-11 17:27-17:30, AUTO mode)

**Primary success on domt4_1:** 3 catches / 16 seconds → **0 catches / 3 seconds**. The field is essentially trivialized.

**Total chase: 2:35 / 11 catches → 2:06 / 7 catches.** 29 seconds faster, 4 catches eliminated. Cascade benefit on domt5_1 (44s → 30s) and domt2_1 (lost 1 catch) from auto-pilot arriving in better shape.

**Empirical analog effectiveness on domt4_1 with SOUTH-EAST:**
```
[17:28:04] (engage) pos=(-578,3036) delta=(79,-96)   dmag=124  analog=(1000,1000)
[17:28:05] (sec 1)  pos=(-226,2202) delta=(352,-834) dmag=905  analog=(1000,1000)
[17:28:06] (sec 2)  pos=(-325,1319) delta=(-99,-883) dmag=888  analog=(1000,1000)
[17:28:06] Field transition to domt3_2
```

~900 units/sec sustained motion in the analog direction. Compare v0.15.9.3 (WEST) where the cleanest between-catches sample was `dmag=61`. **The right direction is ~15x faster than the wrong direction on this field.** The script's flow is strong and cooperating with it dramatically multiplies analog effectiveness — the analog isn't subtle, it's a force multiplier on the script's choreography.

### Three-signal analysis on domt3_2 from v0.15.9.4 BAT data

The pattern validated on `domt4_1` is now applied to `domt3_2`:

**Signal 1 — Camera: default.** CALIB on this field had both phases fail (`no movement (dist=0.0)`) but defaults were kept: `camRight=(1.000, 0.000)`, `camDown=(0.000, -1.000)`. With defaults, `dirX=+1` = world east, `dirY=+1` = world south.

**Signal 2 — Kani approach: co-located.** First engaged tick: `kani=(-289,-3513)`, `party=(-211,-3461)`, `kdist=93`. The chase script teleports both to the south end of the field on entry, putting them in catch range immediately. **No clean flee direction from this signal** — the kani is essentially on top of the party. This is the field where 1 catch fires due to forced co-location regardless of our action.

**Signal 3 — Field exit: east.** Trigger line endpoints `(-71,-3390)` and `(-139,-3562)`, nearly vertical, center `(-105,-3476)`. Party engage position `(-289,-3513)` is well west of the line. After the post-catch teleport at `(-1358,-3439)` is even further west. Both states need east-only motion to cross the line. The trigger line's verticality means north-south motion doesn't progress toward crossing.

**Sanity check — script choreography:** the script teleports party from `(-367,1139)` to `(-289,-3513)` on field entry. Mostly south with negligible east-west bias (`+78 east`). The script doesn't impose a preferred east-west direction, so we're free to push east without fighting it.

Two of three signals agree on east (camera + exit); kani is null. Plus an additional efficiency point:

### CALIB-on-domt3_2 was wasted overhead

v0.15.9.4 log shows:
```
[17:28:09] [CALIB] phase 1 FAILED: no movement (dist=0.0), keeping default camRight
[17:28:09] [CALIB] phase 2 FAILED: no movement (dist=0.0), derived camDown=(0.000,-1.000) from camRight perpendicular
```

MODE_TARGET wasted ~1 second on a calibration that produced no useful data. `MODE_DIRECTION` skips CALIB entirely, recovering that second.

### The fix

New entry in `kFieldConfigs` (`src/chase_auto_pilot.cpp`), inserted between the `domt4_1` and `domt5_1` entries:

```cpp
{ "domt3_2", MODE_DIRECTION,
  /*dirX=*/+1, /*dirY=*/ 0,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/false },
```

The `Initialize` log line was updated to reflect the current config table (had been stale since v0.15.9.4 changed domt4_1's direction without updating the log text). Top-of-file comment block gains a v0.15.9.5 entry documenting the three-signal analysis. v0.15.9.4's entry retroactively marked BAT SUCCESSFUL with the result summary.

### What v0.15.9.5 does NOT change

- Drive-mode logic unchanged.
- v0.15.9.3's diagnostic instrumentation retained — `ChaseActiveDiag` continues capturing data for the next refinement iteration.
- Other chase fields' configs untouched.
- cap=0 safety net unchanged.

### Predicted v0.15.9.5 BAT outcome

- `domt3_2`: at minimum ~1 second faster transit (CALIB removed). 6s → 5s.
- Possibly 0 catches instead of 1 if party crosses the trigger line before the script forces the co-location catch.
- If catch still fires (most likely outcome given the co-location is forced by the script), the post-catch teleport puts party further west and direction-drive continues pushing east to cross the line. Same catch count, slightly faster transit.
- Other fields: identical behavior to v0.15.9.4.
- Chase still completes end-to-end.

The limit on this field is the script-forced co-location catch. v0.15.9.5 can't eliminate that without a structural change (e.g., pre-engage analog injection before the script's catch trigger fires). Saving the CALIB second is the available win.

### Files changed

- `src/chase_auto_pilot.cpp` — new `domt3_2` entry in `kFieldConfigs` with ~25-line comment documenting the three-signal analysis; top-of-file comment block gains v0.15.9.5 entry; v0.15.9.4 entry retroactively annotated with BAT result; Initialize log line text updated for current config table.
- `src/ff8_accessibility.h` — version `0.15.9.5` with comment trail entry; v0.15.9.4 entry annotated with BAT result.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md` — v0.15.9.5 ready-to-BAT.
- `NEXT_SESSION_PROMPT.md` — v0.15.9.5 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` — Finding #25 (three-signal validation + ~15x analog effectiveness multiplier from v0.15.9.4 empirical data).

### Risk

Very low. One new config-array entry. `domt3_2`'s previous behavior (BuildFallbackConfig + trigger-line fallback to `(-105,-3476)`) was already aiming the path-finder in the same direction; we're switching to a simpler implementation with the same intent and saving the CALIB overhead.

## v0.15.9.4

`domt4_1` direction config changed from RUN WEST to RUN SOUTH-EAST. First fix produced by the v0.15.9.3 diagnostic build — three independent signals in the BAT data all pointed to south-east as the correct flee direction; the previous WEST config (inherited verbatim from Jegged's strategy guide at v0.15.9.1) was the worst possible direction.

### v0.15.9.3 BAT data on domt4_1

The new `ChaseActiveDiag` log lines captured the data we needed. Key samples:

**Pre-engage phase** (chase ACTIVATED at 17:03:12, ASK answered at 17:03:31):

```
[17:03:13] state=PRE-ENGAGE pos=(-1230,3699) delta=N/A    kani=(-3143,4586) kdist=2108
[17:03:14] state=PRE-ENGAGE pos=(-657,3132)  delta=(573,-567) dmag=806 kani=(-2793,4489) kdist=2530
[17:03:15] state=PRE-ENGAGE pos=(-657,3132)  delta=(0,0)   dmag=0   kani=(-2455,4392) kdist=2195
[17:03:18] state=PRE-ENGAGE pos=(-657,3132)  delta=(0,0)   dmag=0   kani=(-1645,4154) kdist=1421
[17:03:22] state=PRE-ENGAGE pos=(-657,3132)  delta=(0,0)   dmag=0   kani=(-1645,4154) kdist=1421  (chase trigger MES fires)
[17:03:31] (ASK answered, engagement)
```

Two observations from pre-engage:

1. The chase script's own intro animation moves the party from spawn to (-657, 3132) over the first 2 seconds — a delta of `(+573, -567)`, mostly **south-east**. The script's natural choreography is south-east, not west.
2. The kani has a 6-second scripted intro (moves from (-3143, 4586) toward party), then **freezes at (-1645, 4154)** until the ASK is answered. The kani does not have a 17-second head start as we feared; it has a 6-second intro then waits politely. The catches start because the kani moves fast once unfrozen, not because it had time to walk over.

**Clean engaged sample** (between catches #2 and #3, sec 9):

```
[17:03:39] (catch #2 fires; party teleports to (-366,1365))
[17:03:40] state=ENGAGED-DIR pos=(-427,1371) delta=(-61,+6) dmag=61 kani=(-548,2615) kdist=1249 analog=(-1000,0)
```

With `analog=(-1000,0)` (pure screen-left) held for one second, the party moved `(-61, +6)` — pure west with negligible north. **This is the camera-orientation answer.** camRight ≈ `(1, 0)`, camDown ≈ `(0, -1)`. The camera is approximately default; no significant rotation.

### Three signals, one answer

**Signal 1 — Camera:** default orientation. So `dirX=+1, dirY=+1` maps to world `(+east, +south) = south-east`.

**Signal 2 — Kani direction:** Kani at (-1645, 4154), party at (-657, 3132). Kani is at `-988 X, +1022 Y` relative — **north-west** of party. Flee direction = `+X, -Y` = **south-east**.

**Signal 3 — Field exit direction:** Party ended at (-404, 1117) from spawn region (-1230, 3699). Net direction to exit: `+826 east, -2582 south` — mostly south with east component. **South-east** again.

Additionally, the script's intro animation pre-engage already moves the party south-east. Our analog now reinforces the script's flow instead of fighting it.

### Why WEST was wrong

The previous config (v0.15.9.1 through v0.15.9.3, Jegged-derived):

- WEST sends the party toward the kani's approach column (kani is north-west).
- WEST is perpendicular to the field exit direction (south).
- WEST fights the chase script's own south-east movement.

The config got into the codebase at v0.15.9.1 based on the Jegged strategy guide saying "run immediately to the left," and was confirmed only insofar as the party did transition to the next field. With cap=0 NO-OPing the catches, navigation quality was invisible. The config sat untouched through 17 subsequent versions.

Jegged may have been correct for a different camera setup (or for a human player who can adjust mid-chase) but is wrong for our build with default-camera coordinates and a rigid auto-pilot direction.

### The fix

One edit to `src/chase_auto_pilot.cpp`:

```cpp
{ "domt4_1", MODE_DIRECTION,
  /*dirX=*/+1, /*dirY=*/+1,     // was -1, 0 (RUN WEST)
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/false },
```

The comment block on the entry expanded to ~30 lines documenting the v0.15.9.3 BAT-derived rationale. The top-of-file comment block gains a v0.15.9.4 entry summarizing the change.

### What v0.15.9.4 does NOT change

- Drive-mode logic unchanged. MODE_DIRECTION still works as before.
- The v0.15.9.3 diagnostic instrumentation is retained — `ChaseActiveDiag` lines continue firing once per second from chase ACTIVATED through chase end. These will keep capturing data on this field and subsequent fields as refinement progresses to `domt3_2`, `domt5_1`, etc.
- Other chase fields' configs untouched.
- cap=0 safety net stays in place.

### Predicted v0.15.9.4 BAT outcome

- `domt4_1`: substantially fewer catches (target 0-1, was 3 in v0.15.9.2.18 and v0.15.9.3 BATs). Faster transit (target under 10s, was 16s).
- Other fields: identical behavior to v0.15.9.3. Other catches (#4-#11 in v0.15.9.2.18 BAT) remain on the other chase fields until those fields are refined.
- Chase still completes end-to-end (already proven in v0.15.9.2.18).

If the BAT does not reduce catches on `domt4_1` as expected, the data is still useful: it tells us the chase script overrides analog more than the delta data suggested, and the next direction-experiment becomes a fishing expedition (try pure south, pure east, etc.) or the fix has to be structural (pre-engage during ASK, MODE_TARGET with calibration, etc.).

### Files changed

- `src/chase_auto_pilot.cpp` — one config-array entry flipped (`dirX=-1 dirY=0` → `dirX=+1 dirY=+1`); comment on the entry rewritten with v0.15.9.3 BAT analysis (~30 lines); top-of-file comment block gains v0.15.9.4 entry.
- `src/ff8_accessibility.h` — version `0.15.9.4` with comment trail entry.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md` — v0.15.9.4 ready-to-BAT.
- `NEXT_SESSION_PROMPT.md` — v0.15.9.4 BAT plan.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` — Finding #24 (default camera + script-cooperation direction selection).

### Risk

Very low. One config-array entry edited. Behavior change scoped to `domt4_1`. Reverting to WEST or trying another direction is a one-line edit if v0.15.9.4 BAT surprises us.

## v0.15.9.3

Diagnostic-only build to answer two open questions about `domt4_1` (chase start field) before designing the refinement fix. v0.15.9.2.18 BAT completed the chase end-to-end with 11 NO-OPed catches concentrated on the mountain trail and bridge. Aaron's framing: "our current battle nope system is a band-aid for poor navigation." Refinement starts with `domt4_1` since the chase starts there. This version captures the data needed to choose between competing fix strategies.

### Why a diagnostic build first

v0.15.9.2.18 BAT analysis of `domt4_1` (16 seconds total, 3 catches):

- At engage moment (16:26:41) the kani has already been running its chase script for 17 seconds (since chase ACTIVATED at 16:26:25 while the player was reading/answering the ASK).
- By tick=60 (16:26:42, 1 second after engage) kani is at `kdist=401` from party -- already within catch range.
- First catch fires immediately. Each catch teleports the party slightly south and freezes them for 4-5 seconds, then another catch fires.
- Despite analog being held at `lX=-1000, lY=0` (pure screen-left), the party drifts net `+710 east, -2157 south`. The chase script appears to be overriding our analog input -- our "RUN LEFT" config produces motion the script chose, not motion we requested.

Two unknowns:

- **Q1: Is the analog input doing ANYTHING on `domt4_1`?** Or is the chase script the sole motion source?
- **Q2: What's the camera orientation on this field?** With default `camDown=(0,-1)` assumption, `lX=-1000` should drive screen-left = world `-X`. The actual party motion is mostly south (`-Y`) -- either camera is rotated, or script overrides explain the motion.

Fix strategies hinge on these answers. If analog has effect, a different `dirX/dirY` config might cooperate with the script better. If script overrides entirely, we need a different lever (pre-engage analog injection during the ASK, or MODE_TARGET with explicit calibration).

### What v0.15.9.3 ships

Two pure-additive diagnostics in `src/chase_auto_pilot.cpp`:

**1. Pre-engage chase-active per-second log.**

New helper `LogChaseActiveDiagnostic()` is called once per second (60 `Update` ticks) while `ChaseDetector::IsChaseActive()` returns true, **regardless of whether chase_auto_pilot is engaged**. The existing per-second log only fires when engaged, so it goes dark during the ASK window. This new log fills that gap.

Output format:

```
ChaseActiveDiag: field='X' state=PRE-ENGAGE|ENGAGED-DIR|ENGAGED-TGT
  pos=(pX,pY) delta=(dX,dY) dmag=N kani=(kX,kY) kdist=K [analog=(lX,lY)|tgt=(tX,tY)]
```

Three states reported: `PRE-ENGAGE` (chase active but auto-pilot hasn't engaged), `ENGAGED-DIR` (engaged in MODE_DIRECTION), `ENGAGED-TGT` (engaged in MODE_TARGET).

**2. Movement delta computation.**

New state vars `s_prevPosX/Y/Valid` track the party position between log lines. Every diagnostic log line includes `delta=(dX,dY) dmag=N` -- the position change since the previous snapshot.

This is the key data for answering Q1. If `dmag` is zero or doesn't correlate with our analog direction during engaged ticks, the chase script is in control. If `dmag` is positive and consistent with (a rotated version of) the analog direction, input is being applied and we can derive camera orientation from the world delta vector.

### What v0.15.9.3 does NOT change

- No behavior change to any drive mode. MODE_DIRECTION and MODE_TARGET work exactly as in v0.15.9.2.18.
- The existing per-second engaged tick log (`s_diagTickCounter` based) is unchanged -- it continues firing as before. The new ChaseActiveDiag fires on an independent counter (`s_chaseActiveTickCounter`), so engaged fields get two log lines per second from different cadences. This is intentional: more samples, more data.
- The `kFieldConfigs` table is unchanged (`domt4_1` still MODE_DIRECTION RUN LEFT).

### v0.15.9.3 BAT plan

Identical to v0.15.9.2.18: load save, walk to chase trigger, commit Auto, hands-off. The chase completes end-to-end (proven in v0.15.9.2.18). Diagnostic data accumulates in the field log throughout the chase.

### Post-BAT analysis

The key data points to extract:

1. **Kani position during the ASK window.** Pre-engage logs at seconds 1-N (where N is the duration of the ASK + delay) show kani trajectory. Tells us when the kani enters catch range relative to when the auto-pilot engages.
2. **Delta direction during engaged-state ticks early in `domt4_1`.** First few engaged ticks have less catch interference. If `delta=(dX,dY)` consistently points in some world direction, that's `-camRight` (since we're pressing `lX=-1000`). Camera orientation derived.
3. **`dmag` during engaged ticks vs `dmag` during catch-freeze periods.** Distinguishes "input working slowly" from "script completely overriding".
4. **`dmag` correlation with kani proximity.** Does the party move more when the kani is far away (kdist > 1000) and less when it's close? Would suggest the kani's catch animation is what's freezing motion.

With these data points, v0.15.9.4 picks a fix:

- **If analog works at expected magnitude** -> try alternate direction signs (south, south-east, etc.) to find one that cooperates with the script's natural flow.
- **If analog works but slowly / partially** -> attempt to maximize analog magnitude or switch to MODE_TARGET with explicit calibration on this field.
- **If analog has no effect during catches** -> investigate pre-engage analog injection (start direction-drive during the ASK so the party has built up momentum before the kani's catch range).
- **If analog has no effect even between catches** -> deeper script investigation needed. Maybe the script's chase loop ignores analog by design and the auto-pilot must work around it differently (e.g., movement is purely script-timed and avoiding catches requires interrupting the script).

### Files changed

- `src/chase_auto_pilot.cpp` -- new state vars (`s_chaseActiveTickCounter`, `s_prevPosX`, `s_prevPosY`, `s_prevPosValid`); new helper function (`LogChaseActiveDiagnostic`); per-second call site in `Update()`; reset on chase-just-started and chase-just-ended transitions. Comment block at top extended with v0.15.9.3 rationale.
- `src/ff8_accessibility.h` -- version `0.15.9.3` with comment trail entry.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- pivot to refinement noted; v0.15.9.3 ready-to-BAT state.
- `NEXT_SESSION_PROMPT.md` -- v0.15.9.3 BAT plan and post-BAT analysis checklist.

### Risk

Very low. Pure additive diagnostic. No new tools, no API surface changes, no drive-mode behavior changes. Two SEH-guarded position reads (already-existing helpers `ReadSquallPosition` and `ReadKaniPosition`). One additional log line per second during chase-active state.

## v0.15.9.2.18

Explicit `MODE_DIRECTION` per-field config for `dotown_1`. Mirror of the v0.15.9.2.17 dotown_2 fix. v0.15.9.2.17 BAT showed AUTO mode auto-piloted the entire chase route and the disc00_07h.avi FMV did fire -- but the party got stuck at `~(89, 2297)` on dotown_1 for 24 seconds before the FMV triggered. Same CALIB-failure root cause as the v0.15.9.2.16 BAT on dotown_1, this time without manual interference, confirming the issue is intrinsic to dotown_1's first-second state.

### v0.15.9.2.17 BAT result (2026-05-11 16:11-16:14)

AUTO mode drove the chase end-to-end. dotown_2 fix worked perfectly:

```
[16:13:25] ChaseAutoPilot: ENGAGED on field='dotown_2' mode=DIRECTION direction=south running
[16:13:26] pos=(-1363,3599)
[16:13:27] pos=(-1102,2739)  -- 900 units/sec south
[16:13:28] pos=(-779,1839)
[16:13:39] dotown_1 transition (~14s vs 41s manual baseline)
```

But dotown_1 stuck:

```
[16:13:40] ChaseAutoPilot: ENGAGED on field='dotown_1' mode=TARGET tgt=(-246,-555) running
[16:13:40] [CALIB] phase 1 FAILED: no movement (dist=0.0), keeping default camRight
[16:13:41] [CALIB] phase 2 FAILED: no movement (dist=0.0), derived camDown=(0.000,-1.000) from camRight perpendicular
[16:13:48] pos=(89,2297)  -- party moved 3300+ units from spawn (1629,6120) but now stuck
[16:13:49] velocity-stuck, skipping wp 3/65, advancing to wp 4/65
[16:13:50] velocity-stuck, skipping wp 4/65, advancing to wp 5/65
... (24 seconds of velocity-stuck / wp-skipping cycle) ...
[16:14:13] disc00_07h.avi FMV fires (party finally drifted into trigger zone)
[16:14:19] chase-drive Arrived. (after FMV started)
```

Aaron's exact observation: "got right up to the trigger line to the beach fmv, then stopped for some reason before actually triggering it."

### Root cause

Same as dotown_2's root cause, presenting as a different symptom. dotown_1's walkmesh is connected (A* found 65 valid waypoints), so chase-drive doesn't fail at the engagement stage. But CALIB phase 1 fails on field entry -- the engine doesn't respond to the lX=+1000 test input during the first second of dotown_1. Without proper camera axes, the path-finding drive's analog steering is approximate; the party moves in the correct general direction but drifts off-course and gets stuck. The advance-on-stuck mechanism (v0.15.9.2.5) tries to skip waypoints but can't make real progress because the underlying steering is wrong.

Not reproducible by retrying calibration (the engine's input-block lasts longer than the 0.4-second phase 1 window). The chase scene's scripted segment transitions (SETLINE-driven) dominate movement on these town fields regardless; A* path-finding isn't the right mechanism.

### The fix

One file: `src/chase_auto_pilot.cpp`. `kFieldConfigs` gains a fourth entry:

```cpp
{ "dotown_1", MODE_DIRECTION,
  /*dirX=*/0, /*dirY=*/+1,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/false },
```

Identical structure to the dotown_2 entry. MODE_DIRECTION bypasses CALIB entirely (screen-relative directions don't need camera axes for sign), and never reports arrival (no target), so the party walks continuously south until the field changes via the FMV trigger.

`dirY=+1` (screen-down) with default camera assumption gives the correct general direction toward the FMV trigger zone -- the v0.15.9.2.17 BAT confirmed the party moved correctly south-west during the working phase, before getting stuck. Direction-mode removes the drift mechanism (no target chasing, no waypoint cycling) and just holds the analog south.

If v0.15.9.2.18 BAT shows the party walks south but misses the trigger because it's slightly west, the next iteration adds `dirX=-1`. For now, mirror the minimal dotown_2 config.

### Predicted v0.15.9.2.18 BAT outcome

AUTO mode auto-pilots through the full chase, with both `dotown_2` and `dotown_1` now on direction-drive:

- `domt4_1` (DIRECTION west) -> ASK -> Auto committed.
- `domt3_2`, `domt5_1`, `domt2_1`, `domt1_1`, `doopen2a` via INF-gateway fallback.
- `dotown_3` via INF-gateway fallback (30 waypoints, real arrival).
- FMV `disc00_06h.avi`.
- `dotown_2` (DIRECTION south, from v0.15.9.2.17). Cleared in ~14 seconds.
- `dotown_1` (DIRECTION south, NEW v0.15.9.2.18). Cleared quickly (a few seconds), party reaches FMV trigger zone with no stuck cycling.
- FMV `disc00_07h.avi` (chase climax).
- `CHASE-END SUMMARY mode=auto battles_fired=0` after the FMV terminator field loads.

Expected wall-clock time on dotown_1: under 10 seconds, not 24.

### Files changed

- `src/chase_auto_pilot.cpp` -- `kFieldConfigs` gains a fourth entry (5 lines); comment block above the array extended with v0.15.9.2.18 BAT findings (~25 lines).
- `src/ff8_accessibility.h` -- version `0.15.9.2.18` with comment trail entry.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- v0.15.9.2.18 ready-to-BAT state.
- `NEXT_SESSION_PROMPT.md` -- v0.15.9.2.18 BAT plan.

### Risk

Low. Same code path as the dotown_2 fix that worked perfectly in v0.15.9.2.17 BAT. One array gets one entry. No API changes, no behavior changes for fields other than `dotown_1`.

## v0.15.9.2.17

Explicit `MODE_DIRECTION` per-field config for `dotown_2`. v0.15.9.2.16 BAT auto-piloted one field further than v0.15.9.2.15 (reached and cleared `dotown_3`), then transition FMV `disc00_06h.avi` played, party loaded on `dotown_2`, and auto-pilot disengaged permanently for that field. The fix is a targeted explicit config that bypasses path-finding.

### v0.15.9.2.16 BAT diagnosis (2026-05-11 15:21-15:24)

Field log on `dotown_2`:

```
[15:23:49] [chase-drive] target on different walkmesh island (start tri 45, goal tri 137) -- searching for bridge trigger
[15:23:49] [chase-drive] redirecting to trigger line 3 center=(-840,908) tri=77 dist=3699
[15:23:49] [A*] No path from tri 45 to tri 77 (48 iterations)
[15:23:49] [chase-drive] STARTED tgt=(-197,-600) walk=0 player=(-1642,4518) waypoints=0 startDist=3699 trigIdx=-1 crossLine=yes
[15:23:49] ChaseAutoPilot: ENGAGED on field='dotown_2' mode=TARGET tgt=(-197,-600) running (walk=0)
[15:23:49] [CALIB] phase 1 done: lX=+1000 moved (237,72) ...
[15:23:50] [CALIB] phase 2 done: lY=+1000 moved (72,-237) ...
[15:23:50] [drive] stopped: Arrived.
[15:23:50] ChaseAutoPilot: DISENGAGED (chase-drive completed (target reached or stuck))
[15:23:50] ChaseAutoPilot: field 'dotown_2' marked auto-pilot complete; won't re-engage until field changes
```

Three A* failures on the same field: player on tri 45, INF gateway target on tri 137, trigger-line bridge on tri 77 -- all separate walkmesh islands. `chase-drive STARTED` with `waypoints=0` because A* found no path. Calibration moved the player ~500 units across two phases; the arrival check fired trivially (zero waypoints means "all waypoints reached" is vacuously true); the v0.15.9.2.11 completion marker locked the field out. Aaron drove `dotown_2` manually for 39 seconds afterward.

### Root cause

Dollet town chase fields use SETLINE-triggered scripted animations to teleport the party between street segments. The walkmesh polygons aren't connected; A* fundamentally can't navigate this. `dotown_3` (where A* found a 30-waypoint path) and `dotown_1` (65 waypoints) work fine because their walkmeshes are connected. `dotown_2` is the outlier.

### The fix

One file: `src/chase_auto_pilot.cpp`. The `kFieldConfigs` array gains a third entry:

```cpp
{ "dotown_2", MODE_DIRECTION,
  /*dirX=*/0, /*dirY=*/+1,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/false },
```

`dirY=+1` is screen-down. From the v0.15.9.2.16 BAT calibration on dotown_2 (`camRight=(0.957,0.291)`, `camDown=(0.291,-0.957)`), screen-down maps to mostly world south (-Y, slight +X), which is the direction from spawn `(-1642, 4518)` toward the gateway at `(-198, -600)`. `MODE_DIRECTION` doesn't need A* -- it just holds the analog south, the party walks into the next SETLINE trigger line, the engine fires the segment-transition script, repeat until the field exit.

`MODE_DIRECTION` also bypasses the v0.15.9.2.11 completion-marker trap entirely: there's no "target" to arrive at, so the drive never reports "completed" until the field changes.

### Why only dotown_2

BAT empirical data:

- `dotown_3`: A* found 30 waypoints, real arrival, cleared the field correctly. No change.
- `dotown_2`: A* found 0 waypoints (this fix).
- `dotown_1`: A* found 65 waypoints. Calibration failed in the BAT (`no movement (dist=0.0)`) because Aaron was pressing keys to drive manually, fighting the fake gamepad. With a clean BAT where v0.15.9.2.17 reaches dotown_1 on auto-pilot (no manual interference), calibration should succeed and the generic INF-gateway fallback should drive cleanly. No change.

### Part 2 deferred

A general-purpose fix for the "A* fails -> completion marker traps re-engagement" pattern was discussed but not implemented this version. Aaron explicitly chose Part 1 only (the targeted dotown_2 fix). If future fields surface the same issue, implement Part 2 then: refuse to mark a field as completed when `StartChaseDrive` produced `waypoints=0`, so the next tick can retry (and pick up an explicit per-field config if one was added).

### Predicted v0.15.9.2.17 BAT outcome

AUTO mode auto-pilots through:

- `domt4_1` (explicit DIRECTION RUN LEFT) -> ASK -> Auto committed.
- `domt3_2`, `domt5_1`, `domt2_1`, `domt1_1`, `doopen2a` via INF-gateway fallback.
- `dotown_3` via INF-gateway fallback (A* finds 30 waypoints, real arrival).
- FMV `disc00_06h.avi` plays.
- `dotown_2` engages with `MODE_DIRECTION dirY=+1 running` (NEW). LookupConfig hits the explicit entry, fallback path isn't reached. Auto-pilot holds screen-down on the analog. Party walks into SETLINE triggers, scripts teleport them through the field. Eventually transitions to `dotown_1`.
- `dotown_1` engages via INF-gateway fallback (A* finds 65 waypoints).
- FMV `disc00_07h.avi` (chase climax) fires.
- Post-FMV: ChaseDetector deactivates on the non-chase field that follows. `CHASE-END SUMMARY mode=auto battles_fired=0` logged.

### Files changed

- `src/chase_auto_pilot.cpp` -- `kFieldConfigs` gains a third entry (5 lines); comment block above the array extended with v0.15.9.2.17 BAT findings (~25 lines).
- `src/ff8_accessibility.h` -- version `0.15.9.2.17` with comment trail entry.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- v0.15.9.2.17 ready-to-BAT state.
- `NEXT_SESSION_PROMPT.md` -- updated for v0.15.9.2.17 BAT plan.

### Risk

Low. One array gets one entry. No API changes, no behavior changes for fields other than `dotown_2`. The MODE_DIRECTION path is well-exercised (domt4_1 has used it since v0.15.9.1) so the code path itself is stable.

## v0.15.9.2.16

Extend `CHASE_FIELD_NAMES` (in `src/chase_detector.cpp`) to cover the full chase route through the Lapin Beach FMV. v0.15.9.2.15 BAT auto-piloted 6 chase fields end-to-end (`domt4_1 -> domt3_2 -> domt5_1 -> domt2_1 -> domt1_1 -> doopen2a`) then disengaged on entry to `dotown_3` because that field wasn't in the chase set. The in-game chase isn't over there — it continues for 2-3 more fields up to the chase climax FMV.

### v0.15.9.2.15 manual exploration BAT (2026-05-11 14:54-15:01)

With chase mode set to MANUAL, the BAT walked the full chase route and through the chase climax FMV. Field announces:

```
14:54:47  chase_mode set to 'manual'
14:56:34  domt3_2  (Mountain Hideout 3)
14:56:38  domt5_1  (Mountain Hideout 7)
14:56:54  domt2_1  (Mountain Hideout 1)
14:57:16  domt1_1  (Town Square 1, internal name)
14:57:30  doopen2a (bridge; ~101s spent here doing AI rule #2 reverse-direction trick)
14:59:11  dotown_3 (Town Square 10)
14:59:23  FMV disc00_06h.avi (transition intro: 'ornate car drives through Dollet streets')
14:59:26  dotown_2 (Town Square 8)
14:59:42  dotown_1 (Town Square 6)
14:59:51  FMV disc00_07h.avi (chase climax: X-ATM092 enters town, party flees, robot slams onto bridge, beach extraction by Quistis)
```

`disc00_07h.avi` is the chase terminator FMV. Three new chase fields confirmed: `dotown_3`, `dotown_2`, `dotown_1`.

### The change

One file: `src/chase_detector.cpp`. The `CHASE_FIELD_NAMES` array gains three entries:

```cpp
static const char* CHASE_FIELD_NAMES[] = {
    "domt1_1",
    "domt2_1",
    "domt3_2",
    "domt4_1",
    "domt5_1",
    "doopen2a",
    "dotown_3",  // NEW
    "dotown_2",  // NEW
    "dotown_1",  // NEW
};
```

The comment block above the array gains a v0.15.9.2.16 rationale explaining the re-add and the risk analysis (v0.15.2.11 had removed these fields to protect the dotown_3 cutscene from `chase_kani_freeze`'s animation pin).

### Risk analysis: v0.15.2.11's documented failure mode

v0.15.2.10 hung ~16 seconds after entering `dotown_3` because `chase_kani_freeze.StartCapture` pinned the dotown_3 kani's animation against its cutscene script. That failure mode required a mode 4->1 (battle->field) transition inside `dotown_3` to trigger `StartCapture`.

In **AUTO mode**, `chase_battle_freeze` caps battles at 0 -> no battle ever fires inside any chase field -> no mode 4->1 transition -> no `StartCapture` -> no animation pin -> cutscene plays untouched.

In **MANUAL mode**, the cap is 1, but the v0.15.9.2.15 manual exploration BAT walked through `dotown_3` (and `dotown_2`, and `dotown_1`) without any chase battle firing or hang, suggesting the v0.15.2.10 crash condition isn't reproducible in current builds. Aaron's instinct on this call was: "if auto-pilot simply moves to the next gateway as expected then the robot animation will play normally and we don't need to nope a battle or anything else."

**Fallback plan if a future MANUAL-mode BAT regresses:** gate `chase_kani_freeze.StartCapture` on a stricter predicate (e.g. `IsInKaniActiveChaseField()`) while leaving `CHASE_FIELD_NAMES` inclusive for `chase_auto_pilot`. Not implemented in v0.15.9.2.16 because the empirical evidence says it's not needed.

### Predicted v0.15.9.2.16 BAT outcome

AUTO mode auto-pilots through the full sequence:

- `domt4_1` (explicit config, RUN LEFT) -> ASK -> Auto committed.
- Chase progresses through `domt3_2`, `domt5_1`, `domt2_1`, `domt1_1`, `doopen2a` via the generic INF-gateway fallback from v0.15.9.2.15.
- **NEW:** auto-pilot stays engaged on entry to `dotown_3`. INF gateway fallback computes the south-exit gateway center, builds a chase-drive route through dotown_3's walkmesh.
- Transition FMV `disc00_06h.avi` plays during dotown_3 -> dotown_2 transition.
- Auto-pilot re-engages on `dotown_2`. Continues driving.
- Transition to `dotown_1`. Auto-pilot re-engages.
- Chase climax FMV `disc00_07h.avi` fires (party reaches the trigger position).
- ChaseDetector deactivates on entry to whatever non-chase field comes after dotown_1 (likely `dotown1a` Town Square 7).
- `CHASE-END SUMMARY mode=auto` logged with `battles_fired=0`.

`battles_suppressed` count will likely be even higher than v0.15.9.2.15's 11 since the chase route is now longer. Steering speed is slow (Finding #21 in `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md`), so X-ATM092 catches the party multiple times per chase field. Each catch is NO-OP'd by cap=0. Steering speed refinement is deferred to a separate version.

### Bridge trick deferred

Aaron's AI rule #2 — reverse direction when X-ATM092 leaps over the party on the `doopen2a` bridge — remains unimplemented. The v0.15.9.2.15 manual exploration BAT confirmed Aaron successfully performed the trick manually, but v0.15.9.2.15's build doesn't log PJUMPA opcodes or per-tick kani position, so the field log doesn't contain enough data to reverse-engineer the pattern automatically. A diagnostic-instrumented build is needed:

1. Hook the PJUMPA opcode in field script execution; log every firing with timestamp, source entity (kani vs party), and target position.
2. Add per-tick player + kani position diff logging during `doopen2a` (mirrors the v0.15.9.2.8 diagnostic pattern for kani position).
3. BAT focused on the bridge: Aaron performs the trick manually, full telemetry captured.
4. Analyze the pattern: leap-to-reversal delay, reversal duration, recovery direction.
5. Design and implement state machine. Test.

This is a separate task, probably v0.15.9.3 or v0.15.9.4. Until it ships, AUTO mode on the bridge will get caught by X-ATM092's leaps. Each catch is NO-OP'd by cap=0, so the chase continues, but the steering inefficiency is real.

### Files changed

- `src/chase_detector.cpp` -- `CHASE_FIELD_NAMES` array gains three entries; comment block extended with v0.15.9.2.16 rationale (~36 new lines).
- `src/ff8_accessibility.h` -- version `0.15.9.2.16` with comment trail entry.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md` -- v0.15.9.2.16 ready-to-BAT state.
- `NEXT_SESSION_PROMPT.md` -- updated for v0.15.9.2.16 BAT plan.

### Risk

Low. One array gets three entries. No API changes, no signature changes, no behavior changes for fields other than the three additions. The `chase_kani_freeze` failure mode from v0.15.2.10 is gated on a condition (battle inside dotown_3) that doesn't occur in either AUTO or MANUAL gameplay per empirical evidence.

## v0.15.9.2.15

Use INF gateways as the chase-drive crossing target. v0.15.9.2.14's trigger-line crossing detection works, but the SETLINE entities on domt2_1 are Event Triggers (kani battle calls), not screen boundaries. The actual screen-transition exit is the INF gateway, which the engine already parses but the auto-pilot wasn't using.

### v0.15.9.2.14 BAT diagnosis

Field log on domt2_1 at 10:44:09:

```
ChaseAutoPilot: fallback config built for field='domt2_1' mode=TARGET
                tgt=(276,-727) walk=0 running
                (cluster-center fallback, no trigger line found)
[chase-drive] STARTED tgt=(276,-727) ... trigIdx=-1
```

`GetTriggerLineNearestCluster` returned false. Earlier in the same log:

```
[fieldload] lineType assigned: 2 captured, 2 mapped
            (camPan=0 screenBd=0 event=2 interact=0 unknown=0)
ent0 cat=1 type=Event Trigger sym='squall' param=-1
ent1 cat=1 type=Event Trigger sym='zell' param=-1
```

Both captured Line entities are typed Event Trigger — v0.15.9.2.14's filter (`SCREEN_BOUND` or `UNKNOWN`) correctly rejected them. They're battle-summon triggers, not screen transitions.

But the INF parser already found the right thing:

```
[INF-GW] gw[0] line=(-497,-3414)->(311,-3414) center=(-93,-3414)
         destId=321 dest='bgmon_5'
[INF-GW] gw[1] line=(482,1825)->(118,1351) center=(300,1588)
         destId=328 dest='bgryo1_8'
INF parsed: 2 active gateways for 'domt2_1'
```

gw[0] is the south exit. gw[1] is the entry-back. Aaron's instruction: *"Let's make sure we're actually targeting the exit gateway and its trigger line."*

### The fix

**1. Preserve gateway endpoints.** `FieldArchive::GatewayInfo` gains `lineX1, lineY1, lineX2, lineY2`. `LoadINFGateways` already reads them off the INF — just store them now.

**2. New public API `GetGatewayNearestCluster`.** Picks the gateway whose direction-from-player aligns with the player→cluster direction (positive dot product). Direction-aligned rather than nearest-to-cluster because the entry-back gateway can be geometrically closer (on domt2_1: gw[1] center distance from cluster is ~2316, gw[0] is ~2711). The dot product correctly discriminates:

```
player (520,1391) -> cluster (276,-728): direction (-244, -2119)
player -> gw[0] (-93,-3414):              direction (-613, -4805)
  dot = (-244)(-613) + (-2119)(-4805) = +10,331,967    SELECTED
player -> gw[1] (300,1588):                direction (-220, +197)
  dot = (-244)(-220) + (-2119)(+197)  =    -363,763    rejected
```

**3. Unified crossing-line state.** New `s_driveCrossLineX1/Y1/X2/Y2` and `s_driveCrossLineActive` in field_navigation.cpp. Both trigger-line and gateway paths write the line endpoints here. `UpdateAutoDrive`'s crossing-check block reads from these state vars for chase-drive (decoupled from `s_capturedLines`).

**4. `StartChaseDrive` signature extended:**

```cpp
bool StartChaseDrive(int32_t targetX, int32_t targetY,
                     int triggerLineIdx,
                     int32_t crossLineX1, int32_t crossLineY1,
                     int32_t crossLineX2, int32_t crossLineY2,
                     bool walk);
```

The two crossing-line sources are mutually exclusive — caller passes either a trigger index (`triggerLineIdx >= 0`) or gateway endpoints (`crossLineX1/Y1/X2/Y2` non-zero), not both.

**5. Three-tier fallback in `BuildFallbackConfig`:**

```cpp
int32_t gwX1, gwY1, gwX2, gwY2;
if (FieldNavigation::GetGatewayNearestCluster(&tgtX, &tgtY, &gwX1, &gwY1, &gwX2, &gwY2)) {
    // Tier 1: INF gateway -- the engine's actual screen-transition exit.
    s_fallbackGwLineX1 = gwX1; /* etc. */
} else if (FieldNavigation::GetTriggerLineNearestCluster(&tgtX, &tgtY, &trigIdx)) {
    // Tier 2: SETLINE SCREEN_BOUND or UNKNOWN line.
    s_fallbackTriggerLineIdx = trigIdx;
} else if (FieldNavigation::GetLargestClusterCenter(&tgtX, &tgtY)) {
    // Tier 3: plain cluster center (point-distance arrival).
}
```

Explicit per-field configs (`domt5_1`) keep their point-target behavior — only the fallback path uses crossing detection.

### Files changed

- `src/field_archive.h` — GatewayInfo gains line endpoints.
- `src/field_archive.cpp` — LoadINFGateways stores them.
- `src/field_navigation.h` — new API; StartChaseDrive signature.
- `src/field_navigation.cpp` — GetGatewayNearestCluster impl; new state vars.
- `src/field_nav_directiondrive.inl` — StartChaseDrive crossing setup.
- `src/field_nav_autodrive.inl` — crossing-check reads s_driveCrossLine* for chase-drive.
- `src/chase_auto_pilot.cpp` — BuildFallbackConfig three-tier; Engage passthrough.
- `src/ff8_accessibility.h` — version `0.15.9.2.15`.
- `CHANGELOG.md` — this entry.

### Risk

Medium-high. Seven files across multiple subsystems, one struct field-addition, one API signature change. The existing F9 trigger-line crossing path is preserved: the `!s_chaseDriveActive` branch in UpdateAutoDrive still reads `s_capturedLines[trigCrossIdx]` exactly as before, only chase-drive switched to the unified state vars. The struct field addition is at the end of `GatewayInfo` and shouldn't break ABI for any existing reader.

### Predicted v0.15.9.2.15 BAT outcomes

**SUCCESS** — On domt2_1, log shows:
```
GetGatewayNearestCluster matched cluster(276,-728) player=(520,1391)
  -> gateway[0] center=(-93,-3414) line=(-497,-3414)->(311,-3414)
  destFieldId=321 score=10331967
fallback config built ... INF-GATEWAY line(-497,-3414)->(311,-3414)
[chase-drive] gateway crossing line (-497,-3414)->(311,-3414) crossStart=...
[chase-drive] STARTED ... crossLine=yes
```
Party walks south through the corridor, crosses the gateway line, `Arrived.` fires from crossing detection, field transitions to `bgmon_5`. Subsequent chase fields work the same way.

**PARTIAL** — Direction heuristic picks the wrong gateway on a field with three or more gateways where the player→cluster vector doesn't clearly favor one exit. v0.15.9.2.16 would refine the heuristic (e.g., require not just positive dot product but also farthest-from-spawn).

**FAIL** — INF gateways missing on some chase fields, causing fall-through to trigger lines / cluster center on those. Each such field then needs an explicit per-field config or a script-based exit detector.

## v0.15.9.2.14

Fix the fundamental design: detect when the player physically **crosses** the trigger line, instead of stopping in front of it. Aaron pointed out the obvious: "As these are trigger lines to move between fields or trigger animations, we need to actually cross them not stop in front of them."

### v0.15.9.2.13 BAT result

Field log on domt2_1:

```
22:39:32 wp 14 reached (dist=56), wp 15 reached (dist=49), wp 16 reached (dist=40)
22:39:32 stopped: Arrived.
```

Tighter arrive distance (60) got the player one more waypoint than v0.15.9.2.12 (wp 16 vs wp 15). But still "Arrived" while ~300 units from target. The arrive-distance approach is geometrically wrong for screen-transition triggers — FF8 only fires transitions when the player physically crosses the line, not when they approach it.

### The fix

F9 auto-drive already handles trigger-line targets correctly: cross-product sign-flip detection plus a heading offset that aims 300 units past the line center so player momentum carries them through. Chase-drive was explicitly disabling that logic. This change wires it in.

**1. New public API: `FieldNavigation::GetTriggerLineNearestCluster()`**

Finds the SETLINE-defined trigger line whose center is closest to the largest dead-end cluster. The cluster heuristic finds the "deepest pocket" of the walkmesh; the screen-transition trigger lives at the boundary of that pocket. Combining the two gives "the trigger line you want."

**2. `StartChaseDrive` signature extended:**

```cpp
bool StartChaseDrive(int32_t targetX, int32_t targetY, int triggerLineIdx, bool walk);
```

When `triggerLineIdx >= 0`:
- `s_driveTrigTarget = true` (enables crossing detection)
- `s_driveTrigCrossStart` computed from initial player position
- `s_driveSkipTrigIdx = triggerLineIdx` (exempts target line from A* avoidance)

When `triggerLineIdx == -1`, falls back to point-target arrival (existing behavior, used by explicit per-field configs like domt5_1).

**3. `UpdateAutoDrive` crossing-check extended:**

The existing block was gated on `ei <= -200` (F9's trigger-line entity encoding). Now also fires for chase-drive by reading the trigger index from `s_driveSkipTrigIdx`:

```cpp
int trigCrossIdx = -1;
if (s_driveTrigTarget) {
    if (s_chaseDriveActive)  trigCrossIdx = s_driveSkipTrigIdx;
    else if (ei <= -200)     trigCrossIdx = -(ei + 200);
}
if (trigCrossIdx >= 0 && trigCrossIdx < s_capturedLineCount) {
    // ... existing cross-product check + 300-unit heading offset ...
}
```

**4. `BuildFallbackConfig` prefers trigger lines:**

```cpp
int trigIdx = -1;
bool gotTarget = FieldNavigation::GetTriggerLineNearestCluster(&tgtX, &tgtY, &trigIdx);
if (!gotTarget) {
    if (!FieldNavigation::GetLargestClusterCenter(&tgtX, &tgtY)) return nullptr;
    trigIdx = -1;  // no trigger; cluster-center fallback
}
s_fallbackTriggerLineIdx = trigIdx;
```

Explicit per-field configs (`domt5_1`) keep point-target behavior — only the fallback path passes a trigger index.

### Files changed

- `src/field_navigation.h` — new API declaration; `StartChaseDrive` signature change.
- `src/field_navigation.cpp` — `GetTriggerLineNearestCluster` implementation.
- `src/field_nav_directiondrive.inl` — `StartChaseDrive` signature + trigger setup block.
- `src/field_nav_autodrive.inl` — crossing-check gate extended to chase-drive.
- `src/chase_auto_pilot.cpp` — `BuildFallbackConfig` trigger lookup, `Engage` passes trigger index.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

### Risk

Medium. Five files changed, API signature change, two compilation units (field_navigation.cpp and chase_auto_pilot.cpp). Existing F9 trigger-line logic is unmodified — only the gate condition was extended. The chase-drive point-target path is preserved for explicit configs.

### Predicted v0.15.9.2.14 BAT outcomes

**SUCCESS** — On domt2_1, log shows `TRIGGER-LINE idx=N` in the fallback config line. Chase-drive aims through the trigger center (300 units past), player crosses the line, `Arrived.` fires from crossing detection (not arrive distance), domt2_1 transitions to the next chase field. Subsequent fallback chase fields work the same way. Chase eventually completes naturally.

**PARTIAL** — Trigger line found but its center isn't actually the screen-transition line (the cluster has multiple nearby lines and we picked the wrong one). Drive crosses wrong line and stays on domt2_1. v0.15.9.2.15 then refines line selection (filter by `lineType=SCREEN_BOUND` only, or prefer lines farther from spawn).

**FAIL** — Trigger-line crossing never fires (cross-product never flips sign). Can happen if the trigger line's orientation makes the 300-unit offset point project onto the same side as spawn. v0.15.9.2.15 either inverts `crossStart` sign for chase-drive, or picks the trigger endpoint farthest from spawn as the offset target.

## v0.15.9.2.13

Tighten chase-drive arrive distance from 300 to 60 units so the auto-pilot actually walks the player onto the screen-transition trigger instead of stopping 250–300 units short. Builds on v0.15.9.2.12 — the walk-vs-run fix worked, running footsteps audible on domt2_1.

### v0.15.9.2.12 BAT result

Field log on domt2_1 showed the auto-pilot mechanically working but stopping short:

```
22:08:00 STARTED tgt=(276,-727) player=(520,1391) waypoints=18
22:08:01 wp 0-1 reached
22:08:02 wp 2-7 reached
22:08:03 wp 8-12 reached (player at (683,8))
22:08:05 velocity-stuck advance from wp 13 to wp 14 (v0.15.9.2.9 fix firing correctly)
22:08:06 wp 14 reached (dist=56), wp 15 reached (dist=49), Arrived.
22:08:06 DISENGAGED (chase-drive completed)
```

Aaron reported: "after a few seconds the party either quits moving or gets stuck and does not proceed." Total auto-pilot run was 6 seconds. The party reached wp 14 and 15, then "Arrived" while still ~300 units from the actual target `(276, -727)`.

### Diagnosis

The funnel pathing data tells the story:

```
portal 17/19: L=(419,-531) R=(717,-643)  tri 40->39
portal 18/19: L=(276,-727) R=(276,-727)  tri 39->-1
```

Portal 18 is **degenerate** — both endpoints at exactly `(276, -727)` — which means tri 39 (the goal triangle) has only one vertex/edge on the world boundary, and it's at that point. That's where the screen-transition trigger line lives. The cluster center heuristic landed on the right place.

But `s_driveArriveDist = DRIVE_ARRIVE_DIST_DEFAULT` is **300 units**. That's appropriate for F9 navigation to entities (NPCs have talk radii, save points have walk-into zones, etc.), but it's way too loose for chase-drive where the target IS the trigger line. The auto-pilot considered the player "close enough" 300 units before reaching the trigger, and disengaged.

### Fix

One line in `StartChaseDrive`:

```cpp
s_driveArriveDist = 60.0f;  // was: DRIVE_ARRIVE_DIST_DEFAULT (300)
```

60 matches `FUNNEL_ARRIVE_DIST`, the per-waypoint advance threshold. The player will keep driving through wp 16, 17 and onto the trigger itself.

### Safety nets

If 60 turns out to be unreachable (final waypoint camera-unreachable, geometry mismatch, etc.):

- **v0.15.9.2.9 velocity-stuck advance** fires when the player stops moving for 80 ticks. It already saved us at wp 13 in the v0.15.9.2.12 BAT.
- **v0.15.9.2.11 completion marker** ensures we don't loop — if the auto-pilot can't reach 60, it eventually times out and marks the field done.

### Files changed

- `src/field_nav_directiondrive.inl` — `s_driveArriveDist = 60.0f` in `StartChaseDrive` + updated comment.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

### Risk

Low. One constant changed in the chase-drive code path only. F9 path-finding's arrive distance is untouched. Direction-drive (no path-finding) is untouched. If 60 is too tight, v0.15.9.2.14 picks something between 60 and 300.

### Predicted v0.15.9.2.13 BAT outcomes

**SUCCESS** — On domt2_1 the party reaches `(276,-727)` or very close, crosses the screen-transition trigger, field transitions to the next chase field. v0.15.9.2.11's path-changed marker cleanup re-engages auto-pilot on the new field. Chase continues with running footsteps audible throughout.

**PARTIAL** — Party reaches the trigger area but doesn't cross. Trigger geometry mismatch, or the last waypoint is camera-unreachable AND the trigger requires precise positioning. v0.15.9.2.14 then either projects the target past the cluster, or uses F9-style trigger-line crossing detection.

**FAIL** — 60 is too tight, auto-pilot enters a perma-stuck loop on chase fields. v0.15.9.2.14 picks a middle value like 150.

## v0.15.9.2.12

Flip the fallback walk default from `true` to `false` (running). v0.15.9.2.11 BAT on domt2_1 (Hideout 1, the field after the west trail) was a mechanical success in the log — ASK gate held, completion marker behaved correctly, chase-drive computed 18 waypoints, party walked through wp 0–12, v0.15.9.2.9's velocity-stuck advance fired correctly on wp 13, party reached wp 14–15 and Arrived, no engagement loop. But Aaron heard no footsteps on domt2_1 and clarified what he wanted:

> "The party should be running on this field not walking. On the west trail I did hear footsteps clearly."

### Diagnosis

The fallback config was hardcoded to `walk=true` based on a misreading of Aaron's AI rule #1. That rule applies **only** to domt5_1 (running shakes the cliff path, party gets caught). Other chase fields should default to running — the chase as a whole is Squall fleeing X-ATM092 at top speed.

The walking modifier may also be why footsteps weren't audible on domt2_1:

- Walking footsteps are quieter than running.
- The W keyboard injection has `KEYEVENTF_EXTENDEDKEY` set unconditionally in `InjectKey` — correct for arrow keys (E0 extended), wrong for W (scancode 0x11, regular alphanumeric). The engine may discard or misinterpret the malformed event, producing a half-state that's neither pure walking nor pure running.
- The chase scene script on domt2_1 may mask walking audio.

We'll find out which on the next BAT.

### Fix

One line, plus comment + log message update:

```cpp
s_fallbackConfig.walk = false;  // was: true
```

domt5_1's explicit config keeps `walk=true` as before. domt4_1's explicit config keeps `walk=false`. Only fallback fields (the ones we don't have an explicit config for) get the new default.

### Files changed

- `src/chase_auto_pilot.cpp` — ~8 lines: `walk=false` in `BuildFallbackConfig`, updated comment, updated log line.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

### Risk

Very low. Only the fallback default changes; the explicit per-field configs are untouched. If walking turns out to be needed on some other fallback field, we add an explicit config for that field.

### Predicted v0.15.9.2.12 BAT outcomes

**SUCCESS** — Aaron hears running footsteps on domt2_1 and any other fallback chase fields. domt5_1 still walks audibly as before (explicit config unchanged).

**PARTIAL** — footsteps audible on some fallback fields, still silent on domt2_1 specifically. Means the field has a unique audio quirk separate from the walk modifier. Investigate per-field audio configuration.

**FAIL** — footsteps still silent on domt2_1 with `walk=false`. Likely the `InjectKey` `KEYEVENTF_EXTENDEDKEY` bug or some deeper cause. Then v0.15.9.2.13 fixes `InjectKey` to only set the extended flag for arrow scancodes.

## v0.15.9.2.11

Stop the engage/arrive/disengage re-engagement loop on chase fields where the fallback target lands inside the player's spawn arrival radius. Builds on v0.15.9.2.10's ASK gate — which worked correctly: Aaron heard footsteps on the west trail (domt5_1), no footsteps elsewhere.

### The loop

Field log on domt2_1 showed hundreds of identical lines per second:

```
[chase-drive] STARTED tgt=(276,-727) player=(444,-500) startDist=282
ENGAGED on domt2_1
[drive] stopped: Arrived.
DISENGAGED (chase-drive completed)
fallback config built for field='domt2_1'
[chase-drive] STARTED ...      ← repeats at ~60Hz
```

The player spawned at `(444, -500)` on domt2_1. The fallback target was the largest dead-end cluster at `(276, -727)` — 282 units away, which is inside `DRIVE_ARRIVE_DIST_DEFAULT`. Chase-drive engaged, immediately called `Arrived.` on its first tick, disengaged with reason "chase-drive completed (target reached or stuck)". Next `Update()` saw `s_engaged=false` and rebuilt the fallback, engaged, arrived, disengaged again. **The fake gamepad was being installed and uninstalled hundreds of times per second.** The input pipeline thrash probably explains why no footsteps were audible on any of the fallback-target chase fields — the engine never had time to settle into a steady input state.

### Fix

Add a per-field completion marker:

```cpp
static char s_completedField[32] = {0};
```

In `Disengage()`, when the reason contains `"chase-drive completed"`, copy `s_engagedField` into `s_completedField` before clearing engaged state:

```cpp
if (reason && std::strstr(reason, "chase-drive completed") != nullptr &&
    s_engagedField[0] != '\0') {
    std::strncpy(s_completedField, s_engagedField, sizeof(s_completedField) - 1);
    Log::Field("ChaseAutoPilot: field '%s' marked auto-pilot complete; "
               "won't re-engage until field changes", s_completedField);
}
```

In `Update()`, after resolving the debounced field name:

```cpp
// Field changed since last completion? Clear the marker.
if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) != 0) {
    s_completedField[0] = '\0';
}

// If we've already completed auto-pilot on this field, refuse to re-engage.
if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) == 0) {
    return;
}
```

Player can still drive manually if there's more ground to cover (the fallback target was the largest dead-end cluster, which may or may not be the actual screen-transition trigger). The marker only suppresses auto-pilot retries.

### Files changed

- `src/chase_auto_pilot.cpp` — `s_completedField` state, Disengage marker set, Update marker check + clear-on-change, Initialize reset.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

### Risk

Low. The marker is purely additive — doesn't change any existing engagement path. If the marker ever gets stuck (a clear-on-field-change bug), the player can switch to manual mode and back to auto via the in-game UI to reset.

### Predicted v0.15.9.2.11 BAT outcomes

**SUCCESS** — ASK gate still works. On domt5_1 chase-drive runs with footsteps as before. On domt2_1 (and other fallback fields) engagement happens ONCE per field visit, then either (a) chase-drive runs to a far target and we hear footsteps, or (b) chase-drive arrives instantly because the target is too close, marker is set, no loop. Log shows ONE "fallback config built" per field visit instead of hundreds.

The direction-drive issue on domt4_1 (no footsteps despite engagement) is a **separate** open question for v0.15.9.2.12. That probably needs the v0.15.9.1.1 keepalive pulse to be examined — it may not be producing fresh-enough KEYDOWN events for the engine on this field. Or possibly the engine on domt4_1 specifically ignores fake-gamepad analog values during the scripted chase intro tail.

**PARTIAL** — marker prevents the loop but the player is genuinely stuck at each fallback field's close target. v0.15.9.2.12 then revisits the fallback heuristic to pick targets farther from spawn or use field-specific configs.

**FAIL** — marker doesn't clear properly on field change, auto-pilot misses subsequent chase fields after the first completion. Quick fix: switch chase mode to Manual and back to Auto via the in-game UI to reset all state.

## v0.15.9.2.10

Re-add the chase ASK gate to auto-pilot engagement. Fixes the regression Aaron reported on the v0.15.9.2.9 BAT: "unable to move" on domt4_1 with no footsteps audible.

### What the BAT showed

Auto-pilot engaged on field-entry to domt4_1, immediately installed the fake gamepad and held `lX=-1000 lY=0` (run west). Over 14 seconds the position log showed Squall moving 320 units west to (-2150, 4080), then stuck near the west exit. But Aaron heard **no footsteps the entire BAT**. The position changes weren't from auto-pilot input — they were the chase script's own scripted entity motion during the intro. The auto-pilot's input wasn't reaching the engine, AND it wedged the input pipeline so that after the ASK closed Aaron couldn't move via any path (auto-pilot's stale held arrows + analog confused the engine's state).

### Why this regressed

v0.15.9.1 dropped the `!IsAskActive()` gate based on the theory that "FF8 blocks input during the ASK regardless, so the gate adds no protection but delays engagement." That ignored the **window between chase activation and ASK open**:

- T+0: field load.
- T+2s: `chase_detector` debounce settles. `IsInChaseField()` returns true.
- T+2s–T+12s: scripted intro plays. Squall's chase-trigger MES line plays.
- T+5s after MES: chase ASK deferred-opens.

So there's roughly a 5–12 second window where `inChaseField` is true and `IsAskActive` is false but the engine is mid-scripted-intro. With v0.15.9.1's looser gate, auto-pilot engaged immediately at T+2s and held input through that entire window plus the ASK itself.

Aaron's clarification:

> "auto-drive for the chase scene should not start or be affecting navigation until the ASK dialog fires and an option is selected."

Just `!IsAskActive()` isn't enough — the gate needs to wait for the ASK to have **been seen open and then closed**, which is the precise signal that the scripted intro is done and the player has handed control to the auto-pilot.

### What ships

A two-step state machine in `chase_auto_pilot.cpp`:

```cpp
static bool s_prevChaseActive  = false;  // tracks IsChaseActive transitions
static bool s_askWasActive     = false;  // ASK has been seen open
static bool s_askAnswered      = false;  // ASK was open and is now closed
```

In `Update()`, before the existing engagement gate:

```cpp
bool chaseActive = ChaseDetector::IsChaseActive();
if (chaseActive && !s_prevChaseActive) {
    s_askWasActive = false;
    s_askAnswered  = false;
} else if (!chaseActive && s_prevChaseActive) {
    s_askWasActive = false;
    s_askAnswered  = false;
}
s_prevChaseActive = chaseActive;

if (chaseActive) {
    bool askActiveNow = ChaseAskOverlay::IsAskActive();
    if (askActiveNow) {
        s_askWasActive = true;
    } else if (s_askWasActive && !s_askAnswered) {
        s_askAnswered = true;
    }
}

bool wantEngage = inChaseField && autoMode && onField && s_askAnswered;
```

Both flags reset on chase-activation transitions, so a new chase session re-arms the gate. The chase-end → new-chase cycle works correctly even if the previous chase didn't complete normally.

### What this means for the flow on domt4_1

1. Field load. `chase_detector` debounces.
2. Debounce settles. `IsInChaseField` becomes true. `s_askAnswered` is false.
3. Auto-pilot logs `chase activated, waiting for ASK to fire and be answered before engaging`. **Does not** install the fake gamepad.
4. Squall's chase-trigger MES line plays. ASK deferred-opens.
5. Auto-pilot logs `chase ASK observed open (auto-pilot stays disengaged)`.
6. Aaron picks Auto.
7. ASK closes. Auto-pilot logs `chase ASK answered, engagement gate is now open`.
8. Next tick: `wantEngage` becomes true. Auto-pilot calls `StartDirectionDrive(-1, 0, false)`. Fake gamepad installed for the first time. Engine sees fresh first-time input.

### Files changed

- `src/chase_auto_pilot.cpp` — include `chase_ask_overlay.h`, three state flags, state machine at top of `Update()`, 4th gate condition, new reason string in disengage.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

### Risk

Low. The state machine is purely additive — it adds a gate but doesn't change any existing engagement or refresh path. If the chase activates without an ASK firing for some reason (unlikely — the ASK is integral to chase-mode dispatch), auto-pilot stays disengaged for that session and the player drives manually, same as if they'd picked Manual.

### Predicted v0.15.9.2.10 BAT outcomes

**SUCCESS** — ASK fires normally on domt4_1, Aaron picks Auto, auto-pilot engages cleanly. Footsteps audible. Party runs west. May still encounter the camera-axis issue near the west exit (player needs NW, direction-drive only pushes W) — if so, that's a separate fix for v0.15.9.2.11 (likely switching domt4_1 to MODE_TARGET aimed at the west exit trigger `(-2290, 4337)`).

**PARTIAL** — ASK gate works (logs show `engagement gate is now open` after Aaron picks Auto) but party still doesn't move audibly after engagement. Means there's a deeper issue with direction-drive on domt4_1 beyond just the timing of when the fake gamepad installs.

**FAIL** — ASK never fires or never closes (game state bug); auto-pilot never engages. Recovery: switch chase mode to Manual via in-game UI, then back to Auto, which should re-trigger the ASK on the next chase activation.

## v0.15.9.2.9

Two fixes. The collision-push hypothesis is refuted; input injection works; the wp-13 stuck is now a known bug in the no-progress detection structure, and v0.15.9.2.9 patches it.

### v0.15.9.2.8 BAT result: kani is dormant on domt2_1

The kani-position diagnostic worked. On domt2_1:

```
20:18:04 pos=(586,1102) kani=(814,-875) kdist=61903
20:18:05 pos=(545,700)  kani=(814,-875) kdist=39910
20:18:06 pos=(683,8)    kani=(814,-875) kdist=12471
... 27 seconds later, every line: kani=(814,-875) ...
```

The kani's runtime position was bit-for-bit constant at (814, -875) for the entire 27-second BAT. Squall moved 1,383 units south from (520, 1391) through (683, 8) while the kani sat motionless 1,990+ units away. **Squall walked under his own power, not via collision-push.** On domt2_1 specifically, the kani SYM entry exists but the entity isn't an active AI agent (matches v0.15.0's finding that `battleyarou` is the chase-active entity on this and several other fields, not `kani`).

This refutes the collision-push hypothesis for domt2_1. Earlier chase fields (domt4_1, domt3_2, domt5_1) might still have an active kani contributing to motion — we'd need separate kani logs to be sure — but at minimum, **the auto-pilot's analog/keyboard input is doing real work on at least one chase field**, so v0.15.9.2.5's successful west-trail run wasn't *only* kani push either.

Also shipped a cosmetic bug: the `kdist` values in the log were inflated about 14×. Real distance from (683, 8) to (814, -875) is sqrt(131² + 883²) ≈ 892, log showed 12471. Newton's method starting from `x = v` doesn't converge in 6 iterations for large squared values. Fixed below.

### What went wrong with v0.15.9.2.5's advance-on-stuck

v0.15.9.2.8 BAT also confirmed: ZERO `no-progress stuck` or `chase-drive: skipping wp` log lines appeared during the 27-second wp-13 stall on domt2_1. v0.15.9.2.5's mechanism isn't firing. Reading `field_nav_autodrive.inl` shows why — the no-progress check (and v0.15.9.2.5's chase-drive wp advance inside it) is structurally nested:

```cpp
if (s_driveStuckTicks >= DRIVE_STUCK_THRESH && s_driveTotalTicks >= 60) {
    float stuckDist = sqrtf(sdx*sdx + sdy*sdy);
    if (stuckDist < DRIVE_STUCK_MIN_DIST) {
        // Player hasn't moved enough — trigger recovery.
        // (stuckTicks stays >= thresh so the recovery block below fires)
    } else {
        // Player moved — check no-progress
        if (closed < DRIVE_PROGRESS_MIN) {
            s_driveNoProgressCount++;
            if (s_driveNoProgressCount >= DRIVE_NO_PROGRESS_MAX) {
                // ... v0.15.9.2.5's chase-drive wp advance is HERE ...
            }
        }
    }
}
// Recovery block (gated off for chase-drive per v0.15.9.2.2 Fix B):
else if (s_driveStuckTicks >= DRIVE_STUCK_THRESH && !s_chaseDriveActive) { ... }
```

v0.15.9.2.5 fixed the **oscillation-stuck** case (player moving but not making progress toward target). On wp 13 on domt2_1, the player isn't moving AT ALL (`moveDist=0` for 27 seconds), so `stuckDist < DRIVE_STUCK_MIN_DIST` is true, the if-branch is taken, the no-progress check never runs, and v0.15.9.2.5's advance never fires. Meanwhile the regular recovery branch is also gated off for chase-drive. **Velocity-stuck on chase-drive was completely unhandled** — the party hangs forever.

### What ships

**Fix 1: Chase-drive velocity-stuck advance** in `field_nav_autodrive.inl`. Add the chase-drive wp advance inside the `if (stuckDist < DRIVE_STUCK_MIN_DIST)` branch, parallel to v0.15.9.2.5's no-progress advance:

```cpp
if (stuckDist < DRIVE_STUCK_MIN_DIST) {
    // Player hasn't moved enough — trigger recovery.
    // ...
    if (s_chaseDriveActive && s_waypointIdx < s_waypointCount - 1) {
        Log::Field("FieldNavigation: [drive] chase-drive: velocity-stuck "
                   "(stuckDist=%.0f < %d), skipping wp %d/%d, advancing to wp %d/%d",
                   stuckDist, (int)DRIVE_STUCK_MIN_DIST,
                   s_waypointIdx, s_waypointCount,
                   s_waypointIdx + 1, s_waypointCount);
        s_waypointIdx++;
        s_wpMinDist = 1e30f;
        s_driveStuckTicks = 0;  // wait another window before next advance
    }
}
```

Resetting `s_driveStuckTicks = 0` ensures each wp advance waits another full ~80-tick window (≈1.3s at 60Hz) before the next one fires — preventing chain-skip through all remaining waypoints in one tick.

For wp 13 on domt2_1: after ≈80 ticks (1.3s) of `moveDist=0`, advance to wp 14. If wp 14 is reachable, party walks SW. If wp 14 is also unreachable (camera-stuck again), another 1.3s and advance to wp 15. Funnel has 18 waypoints total; worst case is ≈6 advances × 1.3s = 8 seconds before exhausting the funnel and triggering the main 200-second drive timeout.

**Fix 2: `IntSqrt`** in `chase_auto_pilot.cpp`. Switch from Newton's method to `std::sqrt` via `<cmath>`:

```cpp
static int32_t IntSqrt(int32_t v)
{
    if (v <= 0) return 0;
    return (int32_t)std::sqrt((double)v);
}
```

The `kdist` values in the per-second diagnostic log will be correct now.

### Files changed

- `src/field_nav_autodrive.inl` — ~33 lines: chase-drive velocity-stuck advance inside the `if (stuckDist < DRIVE_STUCK_MIN_DIST)` branch.
- `src/chase_auto_pilot.cpp` — ~6 lines: `IntSqrt` rewrite, `<cmath>` include.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

### Risk

Very low. The velocity-stuck advance is gated on `s_chaseDriveActive` so F9 path-finding sees no change. `IntSqrt` change is purely mathematical correctness (the old code was wrong; the new code is right).

### Predicted v0.15.9.2.9 BAT outcomes

**SUCCESS** — at wp 13 on domt2_1, ~80 ticks of `moveDist=0` triggers velocity-stuck advance. Log shows `chase-drive: velocity-stuck (stuckDist=... < 30), skipping wp 13/18, advancing to wp 14/18`. If wp 14 is camera-reachable, party walks SW. Continues through remaining waypoints, exits the field at the south corridor (target (276, -727)), transitions to the next chase field.

**PARTIAL** — advance fires, party advances past wp 13 but stalls at a later wp. Chain-skip eventually reaches end of funnel; refine in v0.15.9.2.10 (e.g., switch to direct steering toward long-range target on funnel exhaustion).

**FAIL** — party still hangs at (683, 8) even after velocity-stuck advance fires. Means the engine refuses to move from this triangle regardless of analog/kb input — walkmesh collision, scripted block, or something deeper. v0.15.9.2.10 then adds geometric tri ID diagnostic to figure out what the engine is doing.

Most likely outcome is SUCCESS or PARTIAL. We have v0.15.9.2.5 evidence that the chain-skip approach works on domt5_1, and the wp-13 stuck on domt2_1 has the same pattern (camera-unreachable funnel waypoint), so the same workaround should apply.

## v0.15.9.2.8

Diagnostic-only release. Adds kani position and Squall-kani distance to the per-second chase auto-pilot log so we can directly test the collision-push hypothesis. No behavior change.

### v0.15.9.2.7 BAT result: clean logs reveal two things

The log spam fix worked. Field log on domt2_1 is clean:

```
ChaseAutoPilot: fallback config built for field='domt2_1' mode=TARGET tgt=(276,-727) walk=1
ChaseAutoPilot: ENGAGED on field='domt2_1' mode=TARGET tgt=(276,-727) WALKING (walk=1)
[CALIB] phase 1 done: lX=+1000 moved (206,-122) dist=239 -> camRight=(0.860,-0.510)
[CALIB] phase 2 done: lY=+1000 moved (-155,-197) dist=251 -> camDown=(-0.618,-0.786)
[drive] wp 0/18 reached (dist=34), advancing
[drive] wp 1/18 reached (dist=23), advancing
... wp 2 dist=58, wp 3 dist=57, wp 4 dist=51, wp 5 dist=47, wp 6 dist=48, wp 7 dist=55
[drive] wp 8 dist=50, wp 9 dist=58, wp 10 dist=53, wp 11 dist=51, wp 12 dist=37
[drive] tick=168 dist=959 player=(730,118) ... moveDist=1083
[drive] tick=288 dist=840 player=(683,8) wp=13/18 kb=DL ... moveDist=120
[drive] tick=408 dist=840 player=(683,8) wp=13/18 kb=DL ... moveDist=0  <-- frozen
... 9 more identical [drive] logs through tick=1128 (16 seconds), moveDist=0 throughout ...
```

Two observations:

1. **The party walked 12 waypoints in ~2 seconds (~700 units/sec)**, then froze at (683, 8) targeting wp 13 = (622, -48). 700 units/sec is faster than the v0.15.9.2.2 estimate of walking speed (~250 units/sec) and is consistent with running speed (or a kani push).
2. **No `no-progress stuck` or `chase-drive: skipping wp` log lines appear during the 16-second stuck period.** v0.15.9.2.5's advance-on-stuck mechanism is NOT firing on wp 13 on domt2_1 even though the conditions for it (sustained zero progress on a chase-drive target) clearly hold.

### Aaron's collision-push hypothesis

After hearing the v0.15.9.2.7 BAT, Aaron noted he doesn't hear footsteps on the "working" chase fields either. His hypothesis: **the kani's (X-ATM092 spider's) collision is what pushes Squall through chase fields**. The auto-pilot's analog and keyboard input may have been doing nothing the entire time. The chase route would appear to advance because the kani shoves Squall through screen transitions, not because the auto-pilot moves him.

If true, this invalidates the entire chase-drive premise. Every "success" so far (v0.15.9.2.5 west trail end-to-end, v0.15.9.2.4 first 6 waypoints, etc.) might have been collision-push. The wp-13 stuck on domt2_1 would then be explained as: kani is somewhere out of pushing range (still entering the field, stuck behind geometry, or just not pursuing close enough), so the only movement force is gone.

### What ships

**Pure-diagnostic helper** in `chase_auto_pilot.cpp`:

```cpp
static bool ReadKaniPosition(int32_t& outX, int32_t& outY)
{
    uintptr_t kani = ChaseDetector::GetKaniEntityPtr();
    if (kani == 0) return false;
    __try {
        uint8_t* block = reinterpret_cast<uint8_t*>(kani);
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
```

Plus `DistSquared` and `IntSqrt` helpers for kani-to-Squall distance display.

The per-second diagnostic log line now ends with one of:
- ` kani=(KX,KY) kdist=KD` when both Squall and kani positions are readable.
- ` kani=(KX,KY) kdist=?` when only the kani reads (Squall read failed mid-load).
- ` kani=UNRESOLVED` when ChaseDetector has no kani slot for this field.

Reads the kani's runtime block pointer from `ChaseDetector::GetKaniEntityPtr()` which is cached at field-change time. SEH-wrapped pointer read so a field load mid-tick won't crash. No behavior change to engage / disengage / per-tick refresh paths.

### Three patterns to watch for in the v0.15.9.2.8 BAT

**(A)** `kdist` is consistently SMALL (<100) when waypoint progress happens, and LARGE (>500) or `kani=UNRESOLVED` when the party stalls. → Collision-push confirmed. The auto-pilot's input injection is doing nothing. Need to rethink the entire approach — perhaps script-pin the kani's velocity to chase the party, or accept that the chase isn't safely playable in auto mode without sighted assistance.

**(B)** `kdist` is roughly constant or independent of waypoint progress, OR the party moves with the kani clearly far away (>500 units). → Input injection works at least sometimes. The wp-13 stuck has a different cause: most likely a camera-unreachable funnel waypoint combined with the advance-on-stuck guard bug from v0.15.9.2.5 that's not firing on this field. v0.15.9.2.9 then investigates why advance-on-stuck doesn't fire on wp 13 (re-reads `field_nav_autodrive.inl` no-progress block).

**(C)** Every kani log line says `kani=UNRESOLVED`. → ChaseDetector's slot resolution didn't work in this context, or the kani is genuinely not in the others-entity array on these fields (it's a Background entity per v0.15.0 findings). v0.15.9.2.9 then refines `ReadKaniPosition` to handle the Backgrounds array case.

### Files changed

- `src/chase_auto_pilot.cpp` — ~70 lines: `ReadKaniPosition` + `DistSquared` + `IntSqrt` helpers; per-second diagnostic extended with `kaniBuf`; `<cstdio>` include; header trail entry.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.

Deferred: DEVNOTES.md, NEXT_SESSION_PROMPT.md, research doc — will update after v0.15.9.2.8 BAT reveals which hypothesis is correct, since the next steps depend entirely on which pattern (A/B/C) we see.

### Risk

Very low. Purely additive diagnostic. SEH wrap around the kani pointer dereference protects against field-load timing faults. The kani pointer comes from ChaseDetector which already uses it safely for `IsKaniEntityPtr` comparisons. If `ReadKaniPosition` fails for any reason (null pointer, fault), the log line shows `kani=UNRESOLVED` and the auto-pilot keeps running normally.

## v0.15.9.2.7

Fix v0.15.9.2.6 log spam. Restructured `chase_auto_pilot.cpp::Update()` so config lookup only runs on fresh engagement, not every tick.

### v0.15.9.2.6 BAT result: architectural success + structural log-spam bug

The generic fallback worked as designed. Field log shows:

```
ChaseAutoPilot: fallback config built for field='domt2_1' mode=TARGET tgt=(276,-727) walk=1 (largest cluster from walkmesh dead-end scan)
```

...and the party walked 12 of 18 waypoints (`wp=13/18` in the [drive] log) on domt2_1 before stalling at wp 13 = (622, -48). Aaron's chase-multi-field generic engagement request: solved at the architectural level. Every chase field now engages.

But: there were ~960 copies of that one log line over 16 seconds (~60/sec). `BuildFallbackConfig()` was being called from the TOP of `Update()` before the "already engaged on this field" branch, so it ran every tick. The logging line buried the [drive] periodic logs and presumably the v0.15.9.2.5 advance-on-stuck signals ("no-progress stuck:" / "chase-drive: skipping wp"), so we can't tell from the v0.15.9.2.6 logs whether advance-on-stuck fired at wp 13 or not.

The stuck-at-wp-13 pattern is the same shape as the v0.15.9.2.4 wp 7 stuck on domt5_1 (dist=840 constant, player=(683,8) constant, moveDist=0 constant for 16+ seconds, analog says SW toward wp 13 but engine produces no movement). Camera-unreachable target is plausible. But that's a v0.15.9.2.8 question; v0.15.9.2.7 just unmutes the diagnostic.

### What ships

**Update() reorder** in `chase_auto_pilot.cpp`:

Before (v0.15.9.2.6):
```
1. gate check (wantEngage)
2. fieldName lookup
3. if engaged on different field -> disengage
4. LookupConfig + (if null) BuildFallbackConfig    <-- runs every tick
5. if already engaged on this field -> per-tick refresh using cfg->X
6. else -> Engage(cfg)
```

After (v0.15.9.2.7):
```
1. gate check (wantEngage)
2. fieldName lookup
3. if engaged on different field -> disengage
4. if already engaged on this field -> per-tick refresh using s_engagedX cached state + return
5. LookupConfig + (if null) BuildFallbackConfig    <-- runs only on fresh engagement
6. Engage(cfg)
```

The per-tick refresh's diagnostic log also moves from `cfg->dirX/cfg->dirY/cfg->targetX/cfg->targetY/cfg->walk` to `s_engagedDirX/s_engagedDirY/s_engagedTargetX/s_engagedTargetY/s_engagedWalk`. Same printed values, no functional difference — the cached state was already populated by `Engage()` at fresh engagement.

Net effect: `BuildFallbackConfig` fires at most once per fresh engagement on a given field, not once per tick. Same for `LookupConfig`. Periodic [drive] logs and any v0.15.9.2.5 advance-on-stuck events become visible again.

### Files changed

- `src/chase_auto_pilot.cpp` — ~30 lines: Update() reorder; diagnostic log reads cached state; header trail entry.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — still TODO from v0.15.9.2.6; will update after the v0.15.9.2.7 BAT.

### Risk

Very low. Structural reorder only; same calls, same state, just a different order. `s_engagedX` cached state was already populated by `Engage()` before this change — just unused in the per-tick refresh path. The chase route behavior is preserved for explicit-config fields (domt4_1, domt5_1) and unchanged for fallback fields except that the noisy log line stops firing per-tick.

### Predicted v0.15.9.2.7 BAT outcomes

**SUCCESS** — clean logs reveal whether v0.15.9.2.5's advance-on-stuck mechanism is doing its job at wp 13 on domt2_1.

- (A) advance-on-stuck WAS firing in v0.15.9.2.6 but the spam buried the logs — v0.15.9.2.7 logs show it firing, party walks past wp 13 through the funnel.
- (B) advance-on-stuck WAS NOT firing because of some guard we missed — v0.15.9.2.7 logs show party still stuck the same way, and we'll know to look for the guard in v0.15.9.2.8 (likely the no-progress trigger needs to be loosened for chase-drive's MODE_TARGET specifically, or fall through to next waypoint when player distance from current wp stabilizes for N seconds).
- (C) advance-on-stuck fires but the next wp is also unreachable — party chain-skips through funnel waypoints until reaching a navigable one.

**PARTIAL** — logs clean but unexpected pattern; mechanism revealed by trace.

**FAIL** — build error or regression from the structural change. Very unlikely given how mechanical the reorder is.

## v0.15.9.2.6

Generic chase-field engagement fallback. When chase auto-pilot encounters a chase field with no per-field config, use the walkmesh dead-end scanner's largest cluster as the target. Every chase field engages automatically now.

### v0.15.9.2.5 BAT result: SUCCESS on west trail, exposed new problem

Aaron audibly confirmed: party walked the entire west trail (domt5_1) end-to-end. The advance-on-stuck fix kicked in at wp 7 (the camera-unreachable east waypoint) and the party continued through the remaining funnel waypoints. Saga across v0.15.9.2.1 - .2.5 resolved.

Then the party transitioned to domt2_1 and stood still for the rest of the BAT. ChaseDetector correctly identified domt2_1 as a chase field (chaseField=1, kani symIdx=16 -> Others slot 14, battleyarou symIdx=20 -> Others slot 18), but chase_auto_pilot's per-field config table only has entries for domt4_1 and domt5_1. LookupConfig() returned null, Engage() returned silently, no chase-drive engaged.

### Generic fallback

The right fix isn't to hardcode every chase field individually. The chase scene threads through many maps; the BAT pattern would expose them one at a time forever. Generic engagement is better: when no per-field config matches AND the chase field is real, pick a reasonable target dynamically.

The walkmesh dead-end scanner already runs at field load (in `HookedFieldScriptsInit`). It uses BFS through 1-2 neighbor triangles to find narrow corridors and alcoves. Output for domt2_1:

```
[DEADEND] domt2_1: 141 tris, 6 dead-ends, 93 narrow, 4 clusters
[DEADEND]  * cluster[1] center=(276,-728)  tris=42    <- largest
[DEADEND]    cluster[3] center=(-223,-250) tris=10
[DEADEND]    cluster[2] center=(1495,516)  tris=2
[DEADEND]    cluster[0] center=(-369,-3744) tris=1
```

cluster[1] at (276, -728) with 42 triangles is almost certainly the south exit corridor (-Y is screen-south). cluster[3] is the next-largest, likely a side area or the player spawn. Use the largest as the default target.

### What ships

**1. Promote cluster storage to file scope** (`field_navigation.cpp`).
The `DeadEndCluster` struct and `deadClusters[]` array were locals in `HookedFieldScriptsInit` -- results logged then discarded. Moved to file-scope `s_deadClusters[MAX_DEAD_CLUSTERS]` / `s_deadClusterCount`. Per-field reset clears the count. Scanner writes to the file-scope state.

**2. Public API** `FieldNavigation::GetLargestClusterCenter(int32_t* outX, int32_t* outY)`.
Linear scan over `s_deadClusters`; returns the center of the cluster with the highest `triCount`. Ties broken by first found. Returns false if walkmesh didn't load or no clusters.

**3. Fallback config builder** in `chase_auto_pilot.cpp`.
New `BuildFallbackConfig(fieldName)`: calls `GetLargestClusterCenter`, synthesizes a `MODE_TARGET` FieldConfig in module-static `s_fallbackConfig`, walks at default. Returns a pointer to the buffer. `Update()` calls it whenever `LookupConfig()` returns null. If both return null (walkmesh failed), behaves as before.

All downstream Engage/Disengage/per-tick refresh/diagnostic logic works unchanged because the fallback config is structurally identical to an explicit one. The only visible behavior change is engagement happens on previously-unhandled chase fields.

### Behavior across the chase route

- `domt4_1` -- MODE_DIRECTION (explicit, RUN LEFT), unchanged. Cleared in v0.15.9.1.
- `domt3_2` -- no explicit config, fallback fires, MODE_TARGET to largest cluster, walk.
- `domt5_1` -- MODE_TARGET (explicit, (382, 235), walk), unchanged. Walked end-to-end in v0.15.9.2.5.
- `domt2_1` -- no explicit config, fallback fires, MODE_TARGET to (276, -728), walk.
- Any further chase fields downstream -- fallback handles each one.

Chase ends naturally with `CHASE-END SUMMARY mode=auto` when the scene ends.

### Files changed

- `src/field_navigation.cpp` -- ~38 lines: `DeadEndCluster` struct, `s_deadClusters[]`, `s_deadClusterCount`, `GetLargestClusterCenter()` implementation.
- `src/field_nav_fieldscripts.inl` -- ~12 lines: scanner uses file-scope state, per-field reset clears count.
- `src/field_navigation.h` -- ~20 lines: `GetLargestClusterCenter` public declaration with full doc comment.
- `src/chase_auto_pilot.cpp` -- ~50 lines: `BuildFallbackConfig()` + integration in `Update()` + header trail entry + initialize log update.
- `src/ff8_accessibility.h` -- version bump.
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- new finding on the generic fallback pattern.

### Risk

Low. The fallback only fires when no explicit config matches AND the engagement gate is open AND the walkmesh loaded clusters. Existing per-field configs are unchanged. The new public API is read-only with no side effects. F9 path-finding's behavior is bitwise unchanged.

### Predicted v0.15.9.2.6 BAT outcomes

**SUCCESS** -- chase auto-pilot engages on every chase field encountered. Same v0.15.9.2.5 flow on domt4_1 and domt5_1. On domt2_1, fallback fires with target (276, -728), party walks SE toward the south corridor. Field transitions trigger as the party reaches each exit. Chase ends naturally. Push v0.15.9.2.6.

**PARTIAL** -- fallback engages on domt2_1 but party gets stuck at some point (camera-unreachable funnel wp, walkmesh wall, etc). The advance-on-stuck from v0.15.9.2.5 should handle most cases; if not, refine in v0.15.9.2.7.

**FAIL** -- fallback fires but the cluster center is on a separate walkmesh island from player spawn (no bridge). Chase-drive times out and disengages. Rare for chase fields (the kani would walk that path too) but possible. Would need a different fallback heuristic.

## v0.15.9.2.5

Advance funnel waypoint when chase-drive sees no-progress stuck. Workaround for camera-unreachable waypoints on rotated cameras.

### v0.15.9.2.4 BAT result: MAJOR PROGRESS, stuck at wp 7

The kb-from-analog fix WORKED. Party walked from `(-1057, 3301)` start through **7 waypoints in ~3 seconds**, reaching `(-978, 1741)` -- 1,560 units of progress. Aaron heard the party walking. The kb/analog conflict that was freezing v0.15.9.2.2/.3 is solved.

Waypoints reached: wp 0 (dist 42), wp 1 (50), wp 2 (49), wp 3 (47), wp 4 (42), wp 5 (47), wp 6 (46). Then stuck at wp 7 at `(-878, 1752)`, oscillating between `(-978, 1741)` and `(-978, 1770)` for 60+ seconds, dist hovering at ~100.

### Root cause: rotated camera squashes X-axis access

domt5_1's camera: `camRight = (0.069, 0.998)`, `camDown = (0, -1)`. Cross-product determinant ~-0.07. The camera projects mostly to +Y world for `lX`, and pure -Y world for `lY`. World-X is barely accessible.

Calibration measured: `lX = +1000` produces `(+12, +174)` world movement. East movement is ~80x slower than south movement on this camera.

wp 7 at `(-878, 1752)` is east of the player at `(-978, 1741)` -- needs +100 X. But the camera can barely produce +X. Player oscillates around the wp's Y line:

- At `Y=1741` (south of wp), analog says screen-up (NE in projection), kb=UR. Engine moves player north past 1752 to Y=1770.
- At `Y=1770` (north of wp), analog says screen-down (SW), kb=DL. Engine moves player south past 1752 to Y=1741.

Classic oscillation around an axis-unreachable target. The player stays at constant X distance ~100 from wp 7, never reaching FUNNEL_ARRIVE_DIST=60.

### The no-progress detection IS firing

Log shows:

```
[drive] no-progress stuck: dist=2029 progressBaseline=2051 closed=22 noProgressCount=3 -- forcing recovery
```

Fires every few seconds. But recovery is gated off for chase-drive (v0.15.9.2.2 Fix B), so nothing happens.

### The fix

In the no-progress branch, when `s_chaseDriveActive` AND waypoints remain, **advance `s_waypointIdx` by 1** and reset `s_wpMinDist`. This treats no-progress as the signal that we've gotten as close to the current wp as the camera will allow; move on. If the next wp is also camera-unreachable, no-progress fires again and we advance again.

The next funnel waypoint (wp 8) comes from portal 11 (tri 37→21), L=(-803, 1659) R=(-864, 1706). From player `(-978, 1741)`:

- To `(-803, 1659)`: `dx=+175, dz=-82`. Strong south + strong east. Both camera-aligned (south is the dominant axis).
- To `(-864, 1706)`: `dx=+114, dz=-35`. Moderate east, moderate south.

Either way, the displacement is camera-aligned -- the strong Y axis carries most of the work, and the small X component happens incidentally.

### Files changed

- `src/field_nav_autodrive.inl` -- ~22 lines (chase-drive wp advance in no-progress branch).
- `src/ff8_accessibility.h` -- version bump.
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- new finding on funnel waypoints being camera-unreachable; new priority entry for advance-on-stuck.

### Risk

Low. The advance only fires when chase-drive is active AND no-progress is detected AND more waypoints exist. F9 path-finding's no-progress branch behavior is bitwise unchanged.

### Predicted v0.15.9.2.5 BAT outcomes

**SUCCESS** -- party walks through all funnel waypoints. wp 7 stuck → advance to wp 8 (camera-aligned) → party walks SE. Continues through corridor. Transitions through south exit gateway. Chase ends with `CHASE-END SUMMARY mode=auto`. Push v0.15.9.2.5.

**PARTIAL** -- party advances past wp 7 but gets stuck at a later wp. Log shows which wp. Refine in v0.15.9.2.6 (may need overshoot threshold relaxation or a more general "camera-aware" reachability check).

**FAIL** -- chain-skip cycles through all 28 waypoints rapidly without progress. Means the entire path is camera-unreachable; would need a fundamentally different navigation approach. Most likely fix would be to switch to constant-direction analog (like v0.15.9.1 did for domt4_1) for the dominant axis, accepting that off-axis precision will be lost.

## v0.15.9.2.4

Derive keyboard heading from ANALOG values for chase-drive. The keyboard and analog must agree on direction or FF8 freezes movement.

### v0.15.9.2.3 BAT result: PARTIAL FAIL

Build clean at 17:21:10. Chase-drive engaged at 17:25:16 on domt5_1. With v0.15.9.2.3's corridor-steering disable, the steer target was correctly the funnel waypoint at `(-1225, 2811)` (L vertex of portal 2, tri 36 → 38 edge) -- a proper south-then-east turning point along the planned route. Steer was right.

But the party stayed frozen at `(-989, 3195)` for 60+ seconds. Every periodic [drive] log was identical:

```
player=(-989,3195) steer=(-1225,2811) wp=0/28 kb=DR lX=-886 lY=852 moveDist=0
```

### Root cause: ANY axis-level kb/analog conflict freezes movement

- Analog `lX=-886 lY=852` projects through camera `camRight=(0.069, 0.998)` `camDown=(0, -1)` to world direction `(~-61, ~-1736)` = mostly **south on screen, slight west** -- correctly aimed at the funnel waypoint SW of player.
- Keyboard `kb=DR` = south-east, set by v0.15.9.2.2 Fix A which uses origDx/origDz from the long-range target (382, 235) which is SE of player.
- **Y axis agrees** (both south). **X axis disagrees** (kb=east, analog=west).
- Movement: zero.

In v0.15.9.2.1, kb=UP fought analog=SW (full Y disagreement, no X bit set). Player drifted slowly. In v0.15.9.2.2/.3, kb has BOTH bits set (DR), and the X-axis mismatch is enough to lock movement entirely. Confirms Finding #13 from `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md`: **the keyboard heading must match the analog direction, not the target direction**. The keyboard's job is to wake up FF8's movement code; the analog provides the actual steering vector. When they conflict on any axis, FF8 reads inconsistent input and won't move.

### The fix

For chase-drive, derive the keyboard heading from the analog values (s_analogDesiredLX/LY set by `SetAnalogFromVector`). Screen-relative:

- `lX > 100` → `DIR_RIGHT` (screen right)
- `lX < -100` → `DIR_LEFT`
- `lY > 100` → `DIR_DOWN` (DirectInput convention: lY +1000 = screen down)
- `lY < -100` → `DIR_UP`

Dominant-axis fallback if neither passes threshold (rare; analog in deadzone).

For F9 path-finding, keep v0.15.9.2.2's origDx/origDz heading unchanged. F9's NPC targets are usually far away, so origDx/origDz matches the analog direction by default. Gating on chase-drive minimizes F9 regression risk; if F9 ever hits the same freeze pattern, the global fix is to replace F9's branch with the chase-drive logic.

### Reorganization

Moved `SetAnalogFromVector(dx, dz)` to BEFORE the heading bitmask computation (was after), so the heading code can read the analog values that the analog projection just produced.

### Files changed

- `src/field_nav_autodrive.inl` -- ~40 lines (SetAnalogFromVector reorder, dual-branch heading bitmask with chase-drive analog-derived path).
- `src/ff8_accessibility.h` -- version bump.
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- new findings confirming axis-level conflict and updating priorities.

### Risk

Low. F9's heading branch is unchanged. Only chase-drive uses the new analog-derived heading. The reorganization (SetAnalogFromVector moved up) has no behavioral effect: the wiggle and recovery branches both call `SetAnalogFromVector` again inside themselves, overriding whatever the pre-compute set.

### Predicted v0.15.9.2.4 BAT outcomes

**SUCCESS** -- keyboard now derived from analog. With analog SW (`lX=-886 lY=852`), heading becomes `DL` (DIR_DOWN | DIR_LEFT). Both kb and analog agree on south-west. Party walks SW toward funnel wp 0 at full walking pace, reaches it, wp advances, party continues along the funnel path. Eventually transitions through the south exit gateway.

**PARTIAL** -- party moves but doesn't track waypoints precisely or gets stuck at a specific point. Indicates a different problem (waypoint advance logic, walkmesh collision at a tight gap, etc.).

**FAIL: still stuck** -- means there's something more fundamental than kb/analog conflict locking movement. Top candidates:

- FF8 movement-validation refuses to leave the engine-reported tri (51), even when the player wants to go SW into tri 13. The engine's collision check may use its own tri ID to decide whether the move is valid.
- Some walkmesh geometry blocks SW movement from the player's current position.
- A scripted lockout we haven't identified.

If FAIL, v0.15.9.2.5 ships diagnostic: log the geometric containing triangle (computed locally) alongside the engine's reported tri. If they diverge, we've confirmed the stale-tri-ID hypothesis and can pursue workarounds (e.g., teleport the player a few units in the desired direction to force engine reclassification).

## v0.15.9.2.3

Disable corridor-level steering for chase-drive. Falls back to funnel waypoints, which don't depend on the engine's per-tick tri ID.

### v0.15.9.2.2 BAT result: PARTIAL FAIL (worse than v0.15.9.2.1)

Build clean at 17:01:35. Chase-drive engaged at 17:05:02 on domt5_1 with target (382, 235). A* found 31-triangle path with 28 waypoints. Calibration completed: `camRight=(0.069, 0.998)` `camDown=(0, -1)`. Then the party **fully froze at (-989, 3195) for 60+ seconds**. Zero motion. Every periodic `[drive]` log line was identical:

```
player=(-989,3195) steer=(-1135,3293) wp=0/28 kb=DR lX=499 lY=-557 moveAng=0 moveDist=0
```

v0.15.9.2.2's fixes did exactly what they were supposed to (kb=DR now reflects the long-range SE target, no recovery thrashing, longer timeout) -- but the freeze is now WORSE than v0.15.9.2.1. In v0.15.9.2.1 with the broken DIR_UP fallback, kb=U fought analog=SW in the Y axis only, leaving X free so the player drifted (slowly). In v0.15.9.2.2 with kb=DR fully consistent with the long-range target, BOTH axes oppose the analog, producing total freeze.

### Root cause: engine tri ID is stale

The steer target `(-1135, 3293)` is the midpoint of portal 0 (tri 51 → 13 shared edge). The player at `(-989, 3195)` is geometrically SOUTH of this edge (Y=3316-3330 for the edge, player Y=3195 below it). The player should be on tri 13, past portal 0. But the engine's reported tri ID (read from entity +0x1FA) still says tri 51.

Corridor-level steering finds tri 51 at `corridor[0]`, picks `nextTri = corridor[1] = 13`, and targets the 51-13 shared edge midpoint -- which is NORTHWEST of the player's actual position. The analog projection of that direction works out to `lX=499 lY=-557`, which through the camera projection is mostly +Y world = NORTH on screen. Keyboard says SE (toward long-range target). Analog says NW (toward backward portal). Perfect 2D opposition.

Possibly the engine only reclassifies the tri ID when the entity actively moves. The player was frozen post-calibration, so the engine never updated the tri ID. Chicken-and-egg:

- Stale tri ID → corridor steering targets backward portal
- Analog says NW (toward backward portal)
- Keyboard says SE (toward long-range target, per v0.15.9.2.2 Fix A)
- Perfect opposition → zero movement
- Zero movement → engine doesn't reclassify → stale tri ID persists

### The fix

Don't depend on the engine's per-tick tri ID for chase-drive steering. v0.15.9.2.3 ships one change: gate the v06.17 corridor-level steering block on `!s_chaseDriveActive`. When chase-drive is active, the steer target falls back to the **funnel waypoint** at `s_waypoints[s_waypointIdx]`.

Funnel waypoints are computed by `FunnelPath` at A* time from the portal sequence. They're position-based (specific (X, Y) coords), not tri-ID-based. They don't suffer from the stale-tri-ID feedback loop. The funnel waypoint advances naturally as the player closes on it (FUNNEL_ARRIVE_DIST=60 plus overshoot detection), pulling the player along the planned path.

The corridor steering's advantage was anchoring the player to specific portal midpoints for precise edge crossings; the funnel waypoints can be slightly less precise at edges. But they don't lock up.

### Files changed

- `src/field_nav_autodrive.inl` -- ~17 lines (one condition + rationale comment block).
- `src/ff8_accessibility.h` -- version bump.
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.
- `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` -- 3 new findings.

### Risk

Low. Corridor steering remains active for F9 path-finding (gate only fires when `s_chaseDriveActive` is true, which only chase auto-pilot sets). Chase-drive's funnel waypoints are the same code path F9 uses by default when the corridor steering isn't applicable.

### Predicted v0.15.9.2.3 BAT outcomes

**SUCCESS** -- with corridor steering off, the steer target becomes funnel waypoint 0 (somewhere along the south-tending corridor). Keyboard kb=DR and analog should largely agree -- funnel waypoints follow the corridor which goes south then east. Party walks south at calibration-pace, reaches funnel waypoints further along, crosses through the south exit gateway, chase ends naturally.

**PARTIAL** -- party moves but doesn't reach the exit. Means funnel waypoints alone aren't tracking the corridor precisely enough (the corridor steering's job was to fix that). Refine in v0.15.9.2.4 with funnel waypoint dump in the log.

**FAIL** -- party still stuck. Means there's another issue we haven't identified (e.g., FF8 movement-validation refusing to enter tri 13 because the engine still thinks the player is in tri 51, walkmesh collision against tri 13's geometry, etc.). If FAIL, debug by logging player's actual geometric tri (computed via point-in-triangle test) alongside engine's reported tri.

## v0.15.9.2.2

Three coordinated fixes for v0.15.9.2.1 BAT (PARTIAL FAIL: party stuck thrashing near spawn on domt5_1 for 20+ seconds, footsteps audible throughout, net progress zero).

### v0.15.9.2.1 BAT result: PARTIAL FAIL

Build clean at 16:25:39. Chase-drive engaged at 16:32:42 with target (382, 235), walk=1, starting position (-1233, 3576). The v0.15.9.2.1 fixes WORKED — no "No target" SAPI, chase-drive ran for the full engagement window. But the party never escaped the top of domt5_1's walkmesh.

Player position over 20 seconds (one row per chase auto-pilot 1-sec log):

```
16:32:43 (-1255, 3555)   post-calibration start
16:32:44-46 (-1255, 3555) FROZEN 3 sec
16:32:47-50 (-1257, 3578) drifted NORTH 23
16:32:51    (-1264, 3568) brief south
16:32:53-54 (-1234, 3574)..(-1264, 3580) oscillates
16:32:56-59 (-1242, 3559) FROZEN 3 sec
16:33:00-02 (-1244, 3576) FROZEN 2 sec
```

Net Y displacement: zero (started 3576, ended 3576). 12 recovery phases fired in 20 seconds. The party walked in a 30-unit radius near spawn the entire time.

### Three problems identified from the field log

**(1) Heading-fallback bug.** The corridor-level steering (v06.17, lines around 530 of `field_nav_autodrive.inl`) overwrites `dx`/`dz` to point at the nearby shared-edge midpoint between the current and next corridor triangle. On domt5_1, that midpoint is only ~30 units west and ~116 units south of the player — both below `DRIVE_AXIS_THRESH=150`. The heading-bitmask threshold check fails for ALL four direction bits, and the fallback `if (heading == 0) heading = DIR_UP;` fires. So the keyboard presses UP arrow every tick while the analog says SW (toward the actual target). FF8 reads keyboard and analog as voting inputs; with them in direct opposition, the engine resolves to crawl-speed.

Periodic [drive] logs show this clearly across 20 seconds:

```
[16:32:52] tick=600  player=(-1264,3568) steer=(-1294,3452) wp=1/31 kb=U lX=-978 lY=968 analogAng=-135 moveAng=-35 moveDist=12
[16:32:54] tick=720  player=(-1264,3580) steer=(-1138,3586) wp=0/30 kb=U lX=92   lY=-51 analogAng=61   moveAng=180 moveDist=12
[16:32:56] tick=840  player=(-1242,3559) steer=(-1294,3452) wp=0/31 kb=U lX=-917 lY=900 analogAng=-134 moveAng=46  moveDist=30
[16:32:58] tick=960  player=(-1242,3559) steer=(-1294,3452) wp=0/31 kb=U lX=-917 lY=900 analogAng=-134 moveAng=0   moveDist=0
```

`kb=U` is `DIR_UP` only — for the entire log. Despite the analog pointing SW (lX≈-950, lY≈+936), the keyboard insists on UP. Result: 30 units of movement in 2 seconds = 15 units/sec walk pace, vs calibration's 864 units/sec.

**(2) Misdirected recovery nudges.** Every ~1.5 seconds, `DRIVE_STUCK_THRESH=80` ticks fires recovery. Even phases pick the perpendicular nudge direction whose dot-product with `(next-tri-center − player)` is larger — preferring the perp pointing TOWARD the centroid. On elongated triangles (tri 51 in domt5_1 spans X from -1313 to ~-980, ~330 units wide), the centroid can be EAST of the player even when the shared edge to cross is WEST. The chosen perp pushes AWAY from the edge the corridor wants to cross.

Worse: on this field, the camera calibration produces `camRight=(0.041, 0.999)` and `camDown=(0, -1)` — both axes nearly parallel to world ±Y, cross-product determinant ≈ −0.041. The world-direction nudge `(1.00, -0.01)` projects through `SetAnalogFromVector` to `lX=31 lY=10` — tiny values the engine treats as deadzone analog. Net effect: nudge produces no useful movement, but the small `lY=10` alone projects back to slight +Y world (NORTH), drifting the player into tri 29. Next recovery fires from tri 29 wanting south back to tri 32. The party oscillates between tri 29 and tri 32 forever.

**(3) Too-short timeout.** `DRIVE_MAX_TICKS=2400` (40 s) is too short for chase corridors. domt5_1 is ~3300 world units start-to-finish; even at full walking pace without interference, traversal takes ~110 seconds.

### Fixes

**Fix A — Heading bitmask uses original target direction.** Save `float origDx = dx; float origDz = dz;` right after dx/dz are computed from `tx`/`tz` (the long-range target). Use origDx/origDz for the heading bitmask threshold check instead of the post-corridor-override dx/dz. The keyboard wake-up trigger doesn't need to be pixel-precise — it just needs to push in the right broad direction. Analog steering continues to use the corridor-tuned dx/dz for fine waypoint targeting.

Net effect: keyboard and analog AGREE on direction, FF8 reads consistent input, party walks at calibration pace.

**Fix B — Skip stuck-detection-and-recovery for chase-drive.** Gate the entire `else if (s_driveStuckTicks >= DRIVE_STUCK_THRESH)` block on `!s_chaseDriveActive`. Chase corridors are hand-picked and don't need recovery's wiggle nudges; with Fix A in place, main steering can hold a coherent direction without recovery thrashing it. Recovery's perpendicular-nudge logic is fundamentally wrong for elongated triangles on rotated cameras, and disabling it for chase-drive is safer than trying to fix the perp-selection logic in general.

**Fix C — Extended timeout for chase-drive.** `int driveMaxTicks = s_chaseDriveActive ? 12000 : DRIVE_MAX_TICKS;` then check `>= driveMaxTicks` instead of `>= DRIVE_MAX_TICKS`. 12000 ticks = 200 seconds; gives enough slack for full corridor traversal at walking pace. F9 path-finding stays at 40 s.

### Files changed

- `src/field_nav_autodrive.inl` — ~50 lines: origDx/origDz save, heading-bitmask using origDx/origDz, extended timeout, recovery gate, three rationale comment blocks.
- `src/ff8_accessibility.h` — version bump.
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.

### Risk

Low.

- (a) origDx/origDz heading change only affects the keyboard bitmask, which is a wake-up trigger; the analog (which determines actual movement direction in the engine) is unchanged. F9's existing NPC-target use case uses long-range steer too, so `origDx/origDz == dx/dz` in that path and behavior is identical.
- (b) Recovery gate adds one boolean check; F9 path-finding's behavior is bitwise-identical.
- (c) Timeout extension is gated on chase-drive flag; F9 sees `DRIVE_MAX_TICKS` unchanged.

### Predicted v0.15.9.2.2 BAT outcomes

**SUCCESS** — party walks south through the domt5_1 corridor at full walking pace, reaches the cluster[2] target (382, 235) or transitions through a south exit gateway, chase auto-pilot disengages cleanly with `chase-drive completed` log, chase ends naturally with `CHASE-END SUMMARY mode=auto`.

**PARTIAL** — party makes real progress (positions visible in periodic [drive] logs each second) but doesn't reach target. Target refinement needed for v0.15.9.2.3 (cluster[2] center may be off-walkmesh or wrong end of corridor).

**FAIL** — party still stuck. Means the corridor steering itself produces a nonsense steer target, or the engine's input-reading is broken in a way we haven't diagnosed; debug further with more periodic-log instrumentation.

Most likely actual outcome: SUCCESS or PARTIAL. The heading-bitmask fix alone should resolve the keyboard-vs-analog conflict that was strangling movement.

## v0.15.9.2.1

Fix two bugs in v0.15.9.2 chase-drive exposed by 2026-05-10 15:48-15:52 BAT.

### v0.15.9.2 BAT result: PARTIAL FAIL

Build clean. Chase ASK worked. Chase-drive `STARTED tgt=(382,235) walk=1 player=(-1057,3301) waypoints=28 startDist=3387`. A* found a 31-triangle path with 28 funnel waypoints — path computation works end-to-end. Calibration completed cleanly: `camRight=(0.041,0.999) camDown=(0.000,-1.000)`.

Then immediately after calibration:

```
[CALIB] complete
[drive] fake gamepad removed, original ptrs restored
[drive] stopped: No target.
```

F9's `UpdateAutoDrive` fell through to its entity-catalog check, read `s_catalog[s_selectedCatalogIdx]` (stale state from before chase started), saw the catalog entity's `entityIdx == s_playerEntityIdx`, and called `StopAutoDrive("No target.")` — which spoke *"no target"* via SAPI. Drive torn down (fake gamepad removed, analog deactivated, arrows released) but `s_chaseDriveActive` stayed true because F9's `StopAutoDrive` doesn't know about chase-drive's flag. chase_auto_pilot's per-tick `IsChaseDriveActive()` check kept returning true; per-second diagnostic kept logging for 30+ seconds while the actual drive was dead.

Aaron's manual arrow presses moved the party (analog off, fake gamepad gone, raw keyboard reached the engine):

> *"When we got to the west trail it walked for a moment then I heard no target. I pressed down and the party could move freely."*

Aaron's directional feedback *"party needs to head down and slightly right"* confirms the target `(382, 235)` is in the correct direction — player at `(-1057, 3301)` needs to go to `(382, 235)`, which is south (Y decreasing) and east (X increasing). Path was right; teardown was the bug.

### Two coordinated bugs

1. **`UpdateAutoDrive`'s entity-catalog passthrough**: chase-drive's `s_driveTargetEntityIdx = -1` setting wasn't enough. UpdateAutoDrive reads from `s_catalog[s_selectedCatalogIdx]` (a different state path) and fires `StopAutoDrive("No target.")` if the catalog entity matches the player.

2. **`StopAutoDrive` doesn't reset `s_chaseDriveActive`**: When F9 internally stops the drive, chase_auto_pilot never notices because `IsChaseDriveActive()` only checks `s_chaseDriveActive`, not `s_driveActive`.

### Positive signals from v0.15.9.2 BAT

Framework wiring is solid. A* path computation works end-to-end (28-waypoint route from `(-1057, 3301)` to `(382, 235)` computed and funnel-smoothed). Calibration phase works (correct axes derived). Target direction is correct. Chase ASK still works.

### Fix design

**Fix 1 — chase-drive bypass in UpdateAutoDrive** (`field_nav_autodrive.inl`):

Branch on `s_chaseDriveActive` at three places:

- **Entity validation block**: if chase-drive owns the drive, set `ei = -1` sentinel and skip the player/range/target checks (which were firing the spurious *"No target."*).
- **Target-coord lookup block**: read `tx/tz` from `s_chaseDriveTargetX` / `s_chaseDriveTargetY` (new file-scope statics in `field_nav_directiondrive.inl`, cached by `StartChaseDrive`) instead of dereferencing the entity catalog.
- **Wiggle re-path target lookup**: so recovery (wiggle nudge + re-path A*) works for chase-drive, not just nudges.

Replace `catTarget.entityIdx` references with `ei` in the `rpSkipTrigIdx` logic (semantically identical in the F9 path since `ei = catTarget.entityIdx` there, but safer for chase-drive where catTarget would be stale catalog state).

**Fix 2 — disengage detection** (`field_nav_directiondrive.inl`):

`IsChaseDriveActive()` now returns `s_chaseDriveActive && s_driveActive`. If F9's `StopAutoDrive` ever fires internally during chase-drive (defense-in-depth, since Fix 1 should prevent it), `s_driveActive` flips false, `IsChaseDriveActive()` returns false on the next tick, and chase_auto_pilot's Update detects this and calls Disengage which releases W and clears `s_chaseDriveActive` cleanly. No zombie engagement state.

**Fix 3 — silent internal stops** (`field_nav_autodrive.inl`):

`StopAutoDrive`'s SAPI announce gated on `!s_chaseDriveActive`. Internal stops (`"No target."`, `"Stuck."`, `"Arrived."`) fire silently when chase-drive owns the drive — chase auto-pilot is supposed to be silent. Log lines still fire unconditionally for diagnostics.

**Fix 4 — `StartChaseDrive` caches target coords**:

New file-scope statics `s_chaseDriveTargetX` / `s_chaseDriveTargetY` in `field_nav_directiondrive.inl`, set at the bottom of `StartChaseDrive` before `s_chaseDriveActive = true`. `UpdateAutoDrive` reads these via Fix 1.

### Files changed

- `src/field_nav_directiondrive.inl` (~30 lines: target coord statics, cache in `StartChaseDrive`, `IsChaseDriveActive` gating, version comment bump).
- `src/field_nav_autodrive.inl` (~50 lines: `StopAutoDrive` SAPI gate, entity-validation chase branch, target-coord chase branch, wiggle re-path chase branch, `catTarget.entityIdx` → `ei` replacement in skipTrig logic).
- `src/ff8_accessibility.h` (this version).
- `CHANGELOG.md` (this entry).
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.

No `deploy.bat` change needed.

### Risk: low

All chase-drive bypass branches activate only when `s_chaseDriveActive` is true (only chase_auto_pilot sets it). F9's behavior is unchanged because all branches have an `else` clause running the original code unmodified. The `catTarget.entityIdx` → `ei` replacement is semantically identical in the F9 path.

### Predicted v0.15.9.2.1 BAT outcomes

- **SUCCESS**: same chase trigger flow. Party engages on domt4_1 (run west direction-drive, clears in one step). Party engages on domt5_1 with mode=TARGET. A* path follows the 28-waypoint route. Party walks south-then-east through the corridor. Reaches south exit. Field transitions. Chase ends naturally with `CHASE-END SUMMARY mode=auto`. **No spurious "no target" announce.**
- **PARTIAL: party path-finds for a while then gets stuck**. Recovery should kick in (nudge + re-path); if it gives up, `IsChaseDriveActive()` returns false and chase_auto_pilot disengages with the *"chase-drive completed (target reached or stuck)"* log line. Final position visible; refine target in v0.15.9.2.2 if needed.
- **FAIL: F9 path-finding regressed**. Mitigation: branches all have else fallthroughs preserving F9 behavior exactly. F9 backslash test still available.

## v0.15.9.2

Pivot chase auto-pilot from constant-direction analog to F9-style path-finding for fields with bends.

### v0.15.9.1.1 BAT result: PARTIAL SUCCESS, theory wrong

Build clean, cosmetic `tick=60` fix landed, party walked one cycle from `(-877, 2531)` to `(-769, 2217)` in domt5_1 (correct south direction, slightly more motion than v0.15.9.1's first cycle). But position then froze at exactly `(-769, 2217)` for 30+ seconds. Both v0.15.9.1 and v0.15.9.1.1 BATs converged on the **same end coordinates** from different starting positions.

Aaron confirmed during BAT review: *"I did not hear the spider approach."* Mountain-shake catch ruled out (no kani audio = no AI-rule trigger). Walkmesh wall confirmed: domt5_1's corridor angles east of pure south, and constant-direction analog can't navigate the bend. v0.15.9.1.1's keep-alive theory was wrong; the engine isn't dropping movement intent, the party physically can't proceed past `(-769, 2217)` without changing heading.

### Fix: use FF8's existing path-finding

F9 backslash auto-drive already does walkmesh-aware A*+funnel pathing with stuck-detection and wiggle recovery. It threads narrow corridors, navigates bends, and handles the exact geometry chase auto-pilot was tripping on.

### New public API

```cpp
bool FieldNavigation::StartChaseDrive(int32_t targetX, int32_t targetY, bool walk);
void FieldNavigation::StopChaseDrive();
bool FieldNavigation::IsChaseDriveActive();
```

Lives in `field_nav_directiondrive.inl` alongside the existing direction-drive API since both share the fake-gamepad infrastructure. Implementation:

- **Drive setup**: duplicates the relevant subset of F9's start logic from `field_nav_handlekeys.inl` -- drive state init (`s_driveActive`, `s_driveLastTriId` from player's current triangle, stuck/wiggle/ticks counters), calibration setup (piggybacks on `s_calibPending` so a single calibration covers both F9 and chase-drive), fake gamepad install, default arrive distance.
- **Path computation**: same `ComputeAStarPath` + `FunnelPath` pipeline F9 uses. Includes island-bridging via trigger lines for the rare cross-island case. Skips entity-specific setup (talkRadius, save/draw walk-into, trigger crossing) since chase auto-pilot has no entity to target.
- **Walk modifier**: chase fields like domt5_1 require walking instead of running (Aaron's AI rule #1: the mountain shakes when running and the party gets caught). Chase-drive holds W (cancel scancode 0x11) for the duration of the drive. F9 path-finding doesn't support this because F9 always runs to its target.
- **Teardown**: `StopChaseDrive` releases W if held, then calls F9's `StopAutoDrive(nullptr)` to release arrows, deactivate analog, remove fake gamepad. `nullptr` reason suppresses the "Cancelled." SAPI announce.

### Mutex with F9

- `StartChaseDrive` refuses if `s_driveActive` is true and `s_chaseDriveActive` is false (i.e., F9 backslash drive is running).
- F9 backslash handler refuses to cancel the drive when `s_chaseDriveActive` is true; instead announces *"Auto-drive unavailable: chase auto-pilot is active."*
- Arrow-key cancel branch suppressed for chase-drive (player can't accidentally bump out of chase mode by tapping a direction).
- Mutex with direction-drive: chase auto-pilot routes to one or the other per-field via its config; never both.

### chase_auto_pilot per-field mode discriminator

New `enum FieldDriveMode { MODE_DIRECTION, MODE_TARGET }`. `FieldConfig` extended to `{fieldName, mode, dirX, dirY, targetX, targetY, walk}`.

Per-field assignment:

- `domt4_1` stays `MODE_DIRECTION` (RUN LEFT, `dirX=-1 dirY=0`). v0.15.9.1 BAT proved direction-drive cleared this field in one walking step; geometry is short.
- `domt5_1` switches to `MODE_TARGET (382, 235)` walk=true. Target chosen from the v0.15.9.1.1 BAT walkmesh dead-end scanner output: cluster[2] at `(382, 235)` is the largest narrow cluster (14 tris) in the south of the walkmesh, likely the field-exit corridor entry. Significantly **east** of the v0.15.9.1.1 stuck point `(-769, 2217)` -- the corridor turns east before heading south, which is why straight-south analog couldn't navigate it.

`Engage()` routes to `StartDirectionDrive` or `StartChaseDrive` per `cfg->mode`. `Disengage()` routes to the matching stop function per cached `s_engagedMode`. Per-tick refresh in `Update`'s already-engaged branch:

- `MODE_DIRECTION`: re-call `StartDirectionDrive` (idempotent + keep-alive pulse).
- `MODE_TARGET`: check `IsChaseDriveActive()` and `Disengage` with *"chase-drive completed (target reached or stuck)"* if false (path-finder finished but no field transition fired -- means target was wrong, refine in v0.15.9.3).

Per-second diagnostic split per mode:

- DIRECTION: `mode=DIRECTION dir=(dX,dY) walk=B pos=(pX,pY) lX=N lY=M`
- TARGET: `mode=TARGET tgt=(tX,tY) walk=B pos=(pX,pY) dist=(dX,dY)`

### Files changed

- `src/field_navigation.h` (~32 lines: 3 new public-API decls + design comment block).
- `src/field_nav_directiondrive.inl` (~280 lines: chase-drive design rationale, file-scope state `s_chaseDriveActive` + `s_chaseDriveWalk`, `StartChaseDrive` / `StopChaseDrive` / `IsChaseDriveActive`).
- `src/field_nav_handlekeys.inl` (~15 lines: arrow-cancel suppression when `s_chaseDriveActive`, F9 toggle refusal with announce).
- `src/chase_auto_pilot.cpp` (~150 lines net: header trail extended, `FieldDriveMode` enum, `FieldConfig` refactored, kFieldConfigs updated, `s_engagedMode` cached state, `DirectionName` extended for `(0,0)='target'`, `Engage`/`Disengage` route per mode, Initialize/Update logs reflect mode).
- `src/ff8_accessibility.h` (this version).
- `CHANGELOG.md` (this entry).
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.

No `deploy.bat` change needed -- `.inl` is a textual include.

### Risk: moderate

The chase-drive duplicates ~150 lines of F9's start logic to avoid refactoring F9 itself (lower regression risk for general nav). The shared `s_driveActive` flag means F9's `UpdateAutoDrive` state machine runs whenever EITHER F9 or chase-drive is active; same path-finding logic, just different entry points. The `s_chaseDriveActive` flag distinguishes them for mutex purposes.

Edge cases:

1. If chase auto-pilot's target is unreachable (different walkmesh island, no bridge trigger), `StartChaseDrive`'s island-bridging logic kicks in -- mirrors F9 verbatim.
2. If the player presses backslash to cancel during a chase, the F9 handler's new gate refuses with announce -- cleaner than silently ignoring.
3. If the player taps an arrow during chase, the new arrow-cancel gate suppresses cancel -- chase auto-pilot stays engaged.
4. If F9 is already running when chase-drive tries to start, `StartChaseDrive` returns false; chase_auto_pilot retries on next Update tick when F9 might have stopped.

### Predicted v0.15.9.2 BAT outcomes

- **SUCCESS**: same chase trigger flow. Auto-pilot engages on domt4_1 with MODE_DIRECTION RUN LEFT (clears field per v0.15.9.1 BAT pattern). Engages on domt5_1 with MODE_TARGET `(382, 235)` walk=true. F9 path-finder computes A*+funnel route from spawn position around the wall at `(-769, 2217)` and continues to the south corridor. Per-second diagnostic shows pos changing each second as path-finding steers. Field transitions when party reaches the south exit gateway. Chase ends naturally with `CHASE-END SUMMARY mode=auto`.
- **PARTIAL: domt5_1 path computed but party gets stuck before reaching target**. F9's wiggle-recovery state machine handles most stuck cases; if it gives up, `IsChaseDriveActive()` returns false and we Disengage with the *"completed (target reached or stuck)"* log line. Final position visible in diagnostic; tells us which target refinement is needed for v0.15.9.3.
- **FAIL: F9 path-finding regressed in some way**. Most likely cause: chase-drive's shared use of `s_driveActive` collides with F9's expectations. Mitigation: I duplicated all F9 state init verbatim; F9 backslash test path still available for verification.

After v0.15.9.2 BAT SUCCESS, v0.15.9.3 could fill in domt3_2 and refine targets; v0.15.9.4+ ships the doopen2a bridge state machine.

## v0.15.9.1.1

Keep-alive pulse for `FieldNavigation`'s direction-drive path so the chase auto-pilot's continuous southward analog input doesn't get debounced by FF8's apparent ~60-tick walking-cycle movement-intent gate.

### v0.15.9.1 BAT result: PARTIAL SUCCESS

Build clean, chase ASK fully worked (Aaron committed Auto at 14:38:10, INI persisted), field transitions on domt4_1 and domt3_2 happened (auto-pilot likely engaged silently on domt4_1's RUN LEFT and the run cleared the field in one step), and `ChaseAutoPilot` ENGAGED correctly on domt5_1 with WALK SOUTH (`dirX=0 dirY=1 walk=1`) at 14:38:32.

**The party MOVED on domt5_1.** Position went from `(-843, 2482)` at 14:38:33 to `(-769, 2217)` at 14:38:34: that is -265 in Y (= screen-down = south) and +74 in X. That single ~1-second walking cycle proved the entire framework -- fake gamepad install, analog override, keyboard wake-up, direction-drive plumbing all reach the engine and move Squall in the correct screen-relative direction.

But after that one walking step, the position froze at `(-769, 2217)` for 80+ seconds straight while diagnostic logs continued to confirm `lX=0`/`lY=1000` was being asserted every frame.

### Theory: engine debounces movement intent after one walking cycle

FF8's engine appears to drop "user wants to move" abstract button state after one walking cycle (~60 ticks) when keyboard input is constant. F9 path-finding sidesteps this because its heading vector wobbles tick-to-tick as the player walks toward dynamic waypoints -- `SetHeldDirections` fires fresh KEYUP/KEYDOWN events whenever the arrow bitmask flips, which the engine treats as new movement-intent events. chase direction-drive's heading is fixed (e.g., always `(0, +1)` for domt5_1), so the arrow bitmask never changes, no fresh events fire, and the engine drops intent after one walking cycle. The single walking-step boundary in the BAT data matches this hypothesis cleanly.

### v0.15.9.1.1 fix

`field_nav_directiondrive.inl`: in the "already running" branch of `StartDirectionDrive` (which `chase_auto_pilot::Update` calls every tick on an engaged field), run a short cycle that releases the held arrow for one tick and re-presses it the next, every `KEEPALIVE_PERIOD` ticks (30, well below the ~60-tick debounce window).

New file-scope statics:
- `KEEPALIVE_PERIOD` (const int, 30)
- `s_keepAliveCounter` (volatile int)

Cycle:
- Counter increments each call.
- Ticks 1..29: normal hold (idempotent `SetHeldDirections(arrows)`).
- Tick 30: RELEASE arrows -- `SetHeldDirections(0)` fires KEYUP.
- Tick 31: RE-PRESS arrows -- `SetHeldDirections(arrows)` fires KEYDOWN, counter resets to 0.

`SetHeldDirections` is diff-based, so the actual `SendInput` KEYUP/KEYDOWN events only fire on the two boundary ticks per cycle -- the rest are no-ops. At 60 FPS that's a re-press every ~0.5 seconds, well inside the engine's apparent debounce window.

The analog vote (`lX`/`lY`) never goes to zero across the pulse -- only the discrete keyboard re-pulse generates the intent event, so the visible movement is continuous (not stop-and-go).

**W (walk modifier) is NOT pulsed** because it's a held modifier the engine reads continuously to set walk-vs-run speed; toggling it would cause speed glitches.

Fresh-start branch and `StopDirectionDrive` both reset `s_keepAliveCounter` to 0 so a stop+restart can't land mid-cycle and immediately fire a release pulse.

### Cosmetic fix bundled

v0.15.9.1's per-second diagnostic logging in `chase_auto_pilot.cpp` reset `s_diagTickCounter` to 0 BEFORE the `Log::Field` call, so every log line read `tick=0` instead of the trigger value (60). Reset moved to AFTER the log call so the printed value matches the per-second cycle.

### Files changed

- `src/field_nav_directiondrive.inl` (~75 lines: KEEP-ALIVE PULSE header comment block, `KEEPALIVE_PERIOD` const, `s_keepAliveCounter` state, three-branch keep-alive logic in "already running" branch, counter reset in fresh-start branch and in `StopDirectionDrive`, version comment bumped to v0.15.9.1.1).
- `src/chase_auto_pilot.cpp` (~10 lines: header comment trail extended, log-before-reset in per-second diagnostic block).
- `src/ff8_accessibility.h` (this version).
- `CHANGELOG.md` (this entry).
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.

### Risk: very low

The keep-alive logic only activates on the "already running" path, which only chase auto-pilot hits today. F9 path-finding's `StartAutoDrive` uses the same field-scope state (`s_analogOverrideActive`, `s_fakeGamepadInstalled`, `s_driveHeld`) but never enters `StartDirectionDrive`. The mutex check in `field_nav_handlekeys.inl` (F9 refuses if `s_directionDriveActive` is true) and `StartDirectionDrive`'s reverse check (refuses if `s_driveActive` is true) prevent concurrent operation.

The release-tick is exactly 1 frame; even if the engine misinterprets the brief KEYUP, the immediate KEYDOWN re-press the next tick should restore intent. `SetHeldDirections`'s diff logic guarantees we only fire `SendInput` when the bitmask actually changes, so per-frame cost during the held portion of the cycle is one volatile increment + one if/else branch -- negligible.

### Predicted v0.15.9.1.1 BAT outcomes

- **SUCCESS**: same chase trigger as v0.15.9.1, party walks south on domt5_1 continuously across multiple walking cycles, diagnostic shows pos changing every second until field transitions to next chase field (likely doopen2a). Tick value in diagnostic now shows 60, not 0.
- **PARTIAL**: party walks for some additional cycles but stops short of the field exit. Could mean the engine debounce window is shorter than `KEEPALIVE_PERIOD=30`, in which case v0.15.9.1.2 lowers the period (try 15 or 20). Or freeze pattern matches v0.15.9.1 exactly with no additional motion -- could mean the engine doesn't process WM_KEYDOWN events at all and v0.15.9.1.2 needs a different keep-alive mechanism (e.g., subtle analog jitter via per-tick variation of `lX`/`lY`).
- **FAIL**: F9 path-finding broke. Should be impossible by design (F9 doesn't enter `StartDirectionDrive`), but BAT verifies. Roll back keep-alive, leave cosmetic fix.

## v0.15.9.1

Wire chase auto-pilot into FieldNavigation's analog-override path. v0.15.9 BAT was a PARTIAL FAIL: the framework wiring fired correctly (chase ASK opened with descriptive labels, commit captured Auto, INI persistence wrote `chase_mode=auto`, ChaseAutoPilot ENGAGED on domt5_1 with the right direction and walk modifier) but two things broke. The auto-pilot MISSED domt4_1 (chase START) because the `!IsAskActive()` engagement gate kept it disengaged for the entire ~37s the chase ASK was open; by the time the ASK cleared, Aaron had already crossed two fields. And once engaged on domt5_1, the party did not move. 46 seconds of no field transition, no `CHASE-END SUMMARY` line, no audible footsteps during the ASK.

Root cause is in `field_nav_autodrive.inl`'s v05.85 comment:

> Keyboard injection is REQUIRED to activate the game's movement code path. Analog steering overrides the direction, but keyboard buttons are the trigger that makes the game process movement at all.

chase_auto_pilot's v0.15.9 standalone SendInput arrows + W weren't enough. The engine reads movement direction from the gamepad analog stick (DIJOYSTATE2 lX/lY); keyboard is just a wake-up trigger. F9 path-finding works because StartAutoDrive installs a fake gamepad and writes lX/lY values; chase_auto_pilot did neither.

Also captured empirically from the BAT log: chase route is `domt4_1 -> domt3_2 -> domt5_1 -> ... -> doopen2a`. domt3_2 was unconfigured in v0.15.9; v0.15.9.2+ will fill it in once we have BAT data confirming what works.

### What ships

**Three coordinated fixes plus per-second diagnostic logging.**

**1. Drop the `!IsAskActive()` gate.** chase_auto_pilot.cpp's engagement gate is now THREE conditions (was four): `IsInChaseField + GetChaseMode==MODE_AUTO + IsOnField`. FF8 already blocks input during ASK regardless, so the dropped gate added no protection but did delay engagement by tens of seconds on chase-START fields. With the gate removed, auto-pilot engages immediately on field entry; the engine queues our analog values until the ASK closes, then movement begins.

**2. New public API in FieldNavigation.**

```cpp
namespace FieldNavigation {
    void StartDirectionDrive(int8_t dirX, int8_t dirY, bool walk);
    void StopDirectionDrive();
    bool IsDirectionDriveActive();
}
```

`dirX` / `dirY` are screen-relative direction signs in `{-1, 0, +1}` matching DirectInput axis convention (`dirX +1` = right, `dirY +1` = south). New module `field_nav_directiondrive.inl` (~190 lines):

- Installs the fake gamepad, mirroring the install code in `field_nav_handlekeys.inl`'s F9 drive branch (idempotent).
- Activates `s_analogOverrideActive` and writes screen-relative analog values directly: `lX = dirX * 1000`, `lY = dirY * 1000`. NO camera projection -- chase fields hand us the direction the engine should see directly.
- Holds one keyboard arrow as the wake-up trigger via the existing `SetHeldDirections` helper. Direction matches `dirX` / `dirY` so the keyboard "vote" agrees with the analog "vote".
- If `walk=true`, holds W (scancode 0x11) via SendInput. FF8 PC default keymap: cancel = W = walk modifier on foot.

**Mutex with F9 path-finding** via new `s_directionDriveActive` flag. `StartDirectionDrive` refuses if `s_driveActive` is true; the F9 handler in `field_nav_handlekeys.inl` refuses (with announce + log line) if `s_directionDriveActive` is true. The two paths share the analog override and fake gamepad infrastructure but cannot run concurrently.

The new `.inl` is included in `field_navigation.cpp` AFTER `field_nav_autodrive.inl` (so `SetHeldDirections` / `InjectKey` / `ReleaseAllDirections` are visible) and BEFORE `field_nav_handlekeys.inl` (so the F9 handler sees the new flag).

**3. chase_auto_pilot rewired to use the new API.** Replaces v0.15.9's standalone SendInput infrastructure (`InjectKey` / `ReleaseAll` / `SetHeld` helpers, scan code constants, `DIR_*` bitmask) with `FieldNavigation::StartDirectionDrive` / `StopDirectionDrive` calls. `FieldConfig` struct refactored from `{fieldName, arrowMask, holdCancel}` to `{fieldName, dirX, dirY, walk}` -- maps more directly to the DirectInput convention. Same two configured fields:

```cpp
static const FieldConfig kFieldConfigs[] = {
    { "domt4_1", -1,  0, /*walk=*/false },  // RUN LEFT
    { "domt5_1",  0, +1, /*walk=*/true  },  // WALK SOUTH
};
```

`chase_ask_overlay.h` include dropped (no longer needed).

### Per-second diagnostic logging

While engaged, `chase_auto_pilot::Update` logs once per ~60 ticks:

```
ChaseAutoPilot: tick=T field='X' dir=(dX,dY) walk=B pos=(pX,pY) lX=N lY=M
```

Position reads SEH-guarded from `entity[0]` (Squall) at `+0x190` / `+0x194` (matching `field_nav_helpers.inl::GetEntityPos`'s fixed-point path). If `pX` / `pY` change tick-to-tick, movement IS reaching the engine; if frozen, the fake gamepad install isn't taking effect.

### Predicted v0.15.9.1 BAT outcome

Same chase trigger as v0.15.9. Aaron should keep playing through MH-7 even if it feels like nothing's happening for a few seconds (the engine needs ~60 ticks to engage movement after fake gamepad install per `field_nav_autodrive.inl`'s v06.08 comment). Watch for the per-second diagnostic line in `Logs/ff8_field.log`.

### BAT outcomes

- **SUCCESS**: party walks south on domt5_1, transitions to next field, chase eventually ends with `CHASE-END SUMMARY mode=auto battles_fired=0 battles_suppressed=N`. Push v0.15.9.1; move to v0.15.9.2 (bridge state machine for doopen2a, fill in domt3_2 direction).
- **PARTIAL: party still doesn't move on domt5_1**: the diagnostic log tells us why. (a) Position frozen, lX/lY correct -> fake gamepad install isn't taking effect; re-check `DD_InstallFakeGamepad` ordering vs FF8's gamepad init. (b) Position moves but never reaches exit -> direction wrong for this field's camera, or stuck on geometry; try other directions, check walkmesh.
- **FAIL: F9 path-finding broke**: refactor regression in `field_nav_directiondrive.inl` or its mutex hook. Roll back the FieldNavigation changes; ship v0.15.9.1.1 with just Fix 1 + diagnostic logging, leave Fix 2 for v0.15.9.2.

### Files changed

- `src/field_navigation.h` -- 3 new public-API decls in namespace, comment block documenting screen-relative axis convention + mutex with F9.
- `src/field_nav_directiondrive.inl` (NEW, ~190 lines) -- design rationale, file-scope state (`s_directionDriveActive` + `s_directionDriveWalk` + `SC_W_CANCEL_DD`), `DD_InstallFakeGamepad` / `DD_UninstallFakeGamepad` / `DD_DirsToArrowMask` helpers, `StartDirectionDrive` / `StopDirectionDrive` / `IsDirectionDriveActive` at namespace scope.
- `src/field_navigation.cpp` -- include the new `.inl` between `autodrive.inl` and `gps.inl`.
- `src/field_nav_handlekeys.inl` -- F9 mutex check refuses if `s_directionDriveActive`.
- `src/chase_auto_pilot.h` -- design rationale updated for v0.15.9.1, three-condition engagement gate documented, diagnostic logging documented.
- `src/chase_auto_pilot.cpp` -- dropped standalone SendInput infrastructure, dropped `chase_ask_overlay.h` include, `FieldConfig` struct refactored to `{fieldName, dirX, dirY, walk}`, `ReadSquallPosition` SEH-guarded helper, `Engage` now calls `FieldNavigation::StartDirectionDrive`, `Disengage` calls `StopDirectionDrive`, three-condition engagement gate, per-second diagnostic block in `Update`.
- `src/ff8_accessibility.h` -- this version.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated for v0.15.9.1 ready-to-BAT state.

### Risk

Low-to-moderate. The new direction-drive API is purely additive; it cannot affect F9 path-finding when no one calls `Start` / `Stop`. The mutex check in F9 is a single boolean condition that fires only when chase auto-pilot is active. The chase_auto_pilot rewrite preserves the v0.15.9 engagement logic (just drops one gate condition + swaps the input mechanism). Edge case: both paths run on the same Update tick and never interleave; the mutex check makes concurrent install impossible by design.

## v0.15.9

Auto chase mode framework + linear-field auto-drive + walk-not-run on the west trail. v0.15.8.1 BAT was a clean SUCCESS (descriptive labels, suppressed redundant commit announce, chase resumed promptly on Auto pick) and the menu UX is now solid. v0.15.9 routes Auto from "falls back to manual" to actual auto-drive behavior so the X-ATM092 chase becomes hands-off on the configured fields.

### What ships

**Three coordinated changes route Auto to MODE_AUTO with cap=0 battle suppression.**

1. `chase_ask_overlay::CommitChoice` ANSWER_AUTO branch now calls `ChaseDetector::SetChaseMode(MODE_AUTO)` instead of `MODE_MANUAL`. ANSWER_ORIGINAL still falls back to MODE_MANUAL until v0.15.10 ships the chase-mod-active flag (vanilla chase, no battle cap).

2. `chase_battle_freeze::Hook_opcode_battle` gate reworked into a per-mode cap:
   ```cpp
   int  cap    = (mode == ChaseDetector::MODE_AUTO) ? 0 : 1;
   bool freeze = (battleCount >= cap);
   ```
   MANUAL keeps cap=1 (first scripted chase battle PASSes, subsequent NO-OP) so the v0.15.8.1 BAT-proven chase-cap behavior is preserved verbatim. AUTO uses cap=0 -- ALL chase battles NO-OP'd, including the scripted opener in domt4_1. The chase progresses as a movement-only scenario: chase_auto_pilot drives, robot scripts play visually, the BATTLE opcode never reaches a battle screen. The doopen2a strcmp guard in the PASS branch is unchanged. NO-OP and PASS log lines now print `cap=N` so the post-BAT log shows which mode was active.

3. **NEW MODULE** `src/chase_auto_pilot.{h,cpp}` (~285 lines total). On chase field entry with mode=AUTO, looks up the field name in a hardcoded FieldConfig table and (if found) injects raw keyboard input via SendInput hardware scan codes -- same pattern as `field_nav_autodrive.inl::InjectKey`. No analog stick override since chase fields are linear corridors.

### Field configuration

```cpp
static const FieldConfig kFieldConfigs[] = {
    { "domt4_1", DIR_LEFT, /*holdCancel=*/false },  // chase start, run west
    { "domt5_1", DIR_DOWN, /*holdCancel=*/true  },  // west trail, walk south
};
```

- **domt4_1** (Mountain Hideout 6, chase start / Selphie cliff): RUN LEFT. Per Jegged.com chase route: "run immediately to the left as quickly as possible. If you delay at all, you will have to fight X-ATM092 again."
- **domt5_1** (Mountain Hideout 7, west trail): WALK DOWN. Per Jegged: "pathway leading south. Walk, don't run, to the bottom of the screen." Aaron confirmed AI rule #1 on 2026-05-10: mountain shakes when running, party loses balance, gets caught. Walk modifier is W (scancode 0x11) -- FF8 PC default cancel binding which on foot acts as the walk modifier (opposite of FF7's run modifier).

Other chase fields (`domt1_1`, `domt2_1`, `domt3_2`, `doopen2a`) are unconfigured -- ChaseAutoPilot::Update returns silently for unrecognized field names, player drives manually. Bridge handling (`doopen2a`) is deferred to v0.15.9.1 with a PJUMPA hook detecting kani's two scripted leaps over the party + reverse-direction state machine per Aaron's AI rule #2.

### Engagement gate

All four conditions must hold:

1. `ChaseDetector::IsInChaseField()` -- in a known chase field
2. `ChaseDetector::GetChaseMode() == MODE_AUTO` -- Auto selected in ASK
3. `FF8Addresses::IsOnField()` -- game mode 1 (not battle/menu)
4. `!ChaseAskOverlay::IsAskActive()` -- chase ASK isn't open (defensive)

Disengagement releases all held keys (arrows + W) cleanly via SendInput KEYUP events. The diff-based `SetHeld` function reconciles held-key state to a desired tuple, idempotent if state already matches -- defends against rare cases where Windows drops a held key (focus-loss races); cheap, called every Update tick on engaged field.

### Wiring

- `dinput8.cpp`: include `chase_auto_pilot.h`, `ChaseAutoPilot::Initialize` after `ChaseAskOverlay::Initialize`, `ChaseAutoPilot::Update` after `ChaseAskOverlay::Update` in the main loop, `ChaseAutoPilot::Shutdown` between `ChaseBattleFreeze::Shutdown` and `ChaseKaniFreeze::Shutdown`.
- `deploy.bat`: `chase_auto_pilot.cpp` added to the compile list after `chase_ask_overlay.cpp`.

### Predicted v0.15.9 BAT outcome

Drive FF8 to chase trigger, pick Auto in dialog, then go hands-off:

- Dialog opens, cursor announces "Auto: falls back to manual selected" (description label still says "falls back to manual" until v0.15.9.1 ships and we update `kChaseChoices`).
- X to commit. SAPI says "Automatic selected"; ChaseDetector logs `chase_mode = auto`.
- Chase resumes; chase_auto_pilot picks up on next Update. Field log shows:
  ```
  ChaseAutoPilot: ENGAGED on field='domt4_1' direction=west running (arrows=0x4 holdCancel=0)
  ```
  Party walks left, no chase battle fires (cap=0 NO-OP). Field log shows:
  ```
  [CBF] NO-OP chase BATTLE call ... mode=auto cap=0
  ```
  for the would-be opener.
- Field transitions when party reaches gateway. ENGAGED line for the next field (likely an unconfigured one) doesn't fire; Aaron drives manually until reaching domt5_1.
- ENGAGED line for domt5_1 fires:
  ```
  ChaseAutoPilot: ENGAGED on field='domt5_1' direction=south WALKING (arrows=0x2 holdCancel=1)
  ```
  Party walks south slowly with W held; no mountain-shake battle (the hypothetical battle is NO-OP'd anyway by cap=0).
- Continued progress through unconfigured fields (player drives) until reaching doopen2a. ENGAGED doesn't fire for doopen2a; Aaron handles the bridge manually (turns around twice as the robot leaps).
- doopen2a -> dotown_3 transition fires per v0.15.2.15's preserved fieldId-flip mechanic; Lapin Beach FMV plays through; control returns to dotown_2.

Total chase battles fought: zero. Total chase battles NO-OP'd: 5-7 depending on which fields the auto-pilot didn't reach in time.

### Risk

Low. The `chase_auto_pilot` module is purely additive -- it does nothing when `mode != AUTO`, so MANUAL chases work exactly as v0.15.8.1. The cap=0 in AUTO is a single-line gate change that preserves the entire MANUAL path verbatim. The keyboard injection uses SendInput which is a documented Win32 API the `field_nav_autodrive.inl` path has been exercising successfully since v0.05.85; same scancode (W=0x11) as v0.14.102's car AD path.

The W modifier might briefly skip a stray dialog if one fires while engaged on domt5_1 (cancel acts as "next" in dialogs). Chase fields rarely have mid-field dialogs so risk is low. If it surfaces in BAT, a v0.15.9.0.1 patch can add a `!FieldDialog::IsDialogActive()` gate.

### Files changed

- `src/chase_auto_pilot.h` (NEW, ~75 lines)
- `src/chase_auto_pilot.cpp` (NEW, ~210 lines)
- `src/chase_ask_overlay.cpp` (~15 lines)
- `src/chase_battle_freeze.cpp` (~30 lines)
- `src/dinput8.cpp` (4 spots)
- `src/deploy.bat` (1 line)
- `src/ff8_accessibility.h` (this version)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

After v0.15.9 BAT SUCCESS, v0.15.9.1 implements the `doopen2a` bridge state machine. After v0.15.9.1, the `kChaseChoices` labels in `chase_ask_overlay` can be updated from "Auto: falls back to manual" to "Auto: hands-off chase" or similar.

## v0.15.8.1

Chase ASK menu UX polish. v0.15.8 BAT was a clean SUCCESS end-to-end: dialog opened on Squall's chase-trigger MES, cursor announced through all three options, X commit captured Manual at 11:05:06, and the chase resumed with the 1-battle-per-field cap holding through Selphie's rendezvous line. But Aaron noticed a glitch -- the chase resumed while three TTS announces were still queued post-commit:

1. "Manual selected" (cursor highlight, just before commit)
2. "You chose Manual" (DialogInject's commit announce)
3. "Manual mode selected. One battle per chase field will be allowed." (chase_ask_overlay's verbose CommitChoice)

Combined that's roughly 10 seconds of speech, and the chase doesn't wait for TTS to drain. v0.15.8.1 trims this to a single short line per Aaron's UX feedback.

### What ships

**chase_ask_overlay -- descriptive choice labels.** `kChaseChoices` updated from short names to brief descriptions:

```cpp
static const char* kChaseChoices[] = {
    "Manual: one battle per field",
    "Auto: falls back to manual",
    "Original: falls back to manual"
};
```

The descriptions get spoken naturally on cursor change during navigation (DialogInject's existing `"<name> selected"` announce path) and on dialog open (FieldDialog's `[ASK]` hook reads them from the encoded override buffer). No new code path -- the labels themselves carry the description.

Manual states the implemented behavior. Auto and Original honestly note they fall back to manual until v0.15.9 / v0.15.10 implement them. When those versions ship, the relevant label updates.

**chase_ask_overlay -- brief commit announces.** `CommitChoice` trimmed from verbose multi-clause messages to single short lines matching Aaron's exact phrasing:

- Manual -> `"Manual selected"`
- Auto -> `"Automatic selected"`
- Original -> `"Original selected"`
- default -> `"Manual selected"` (silent fallback, no "unknown choice" announce)

`ChaseDetector::SetChaseMode(MODE_MANUAL)` routing for all three options is unchanged; v0.15.8's proven chase-cap behavior is preserved verbatim.

**dialog_inject -- announceCommit param.** `OpenAsk` gains a sixth parameter, `bool announceCommit = true`, plumbed through `OpenAskInternal` and stored in a new `s_phase2AnnounceCommit` static. When false, `Update()`'s commit branch suppresses the generic `"You chose <name>"` SAPI announce (still logs `[DLG-INJ] v0.15.8.1 commit announce suppressed by caller`). The cursor-change announces on navigation are unaffected; this only gates the final commit line.

Default `true` keeps `Phase2_TestAsk` (Shift+F12 diagnostic) unchanged -- it has no other commit announce so suppressing it would leave the test silent at commit. `chase_ask_overlay` passes `false` since `CommitChoice` now speaks a brief mode-specific line.

**Infrastructure bumps.** `PHASE2_NAME_CAP` 32 -> 64 to fit the descriptive labels (~30 chars each) with comfortable headroom. The cursor-announce and commit-announce `msg` buffers in `Update()` bumped 64 -> 128 so longer names plus `" selected"` / `"You chose "` suffixes don't `snprintf`-truncate. Static buffer cost: 8 * 64 = 512 bytes (was 256). Negligible.

### Predicted v0.15.8.1 BAT outcome

Trigger the chase. After the ~3s deferred-open delay, dialog renders. FieldDialog `[ASK]` hook speaks something like:

> "Mode?. Selected: Manual: one battle per field. Auto: falls back to manual. Original: falls back to manual."

Followed by initial cursor announce: `"Manual: one battle per field selected"`.

Press **Down** -> `"Auto: falls back to manual selected"`. Press **Down** again -> `"Original: falls back to manual selected"`. Press **X** to commit on any -> single brief `"Manual selected"` (or `"Automatic selected"` / `"Original selected"`). NO `"You chose <name>"` announce -- suppressed. Chase resumes promptly. Subsequent chase battles cap at 1 per field as in v0.15.8.

### Expected log signature delta vs v0.15.8

Commit branch now logs either the announce or the suppression note:

```
[DLG-INJ] v0.15.7 commit reason=state left 0xD capturing answer=1
[DLG-INJ] v0.15.8.1 commit announce suppressed by caller (answer=1 name="Manual: one battle per field")
[FIELD] ChaseAskOverlay: DialogInject::GetLastAnswer returned 1; dispatching
[FIELD] ChaseAskOverlay: committed choice = 1 (Manual: one battle per field)
[FIELD] ChaseAskOverlay: chase ASK closed
```

Arm-time log line gains the announceCommit flag value:

```
[DLG-INJ] v0.15.7 answer-detection armed for slot 2 (timeout 60000 ms, announceCommit=0)
```

### BAT outcomes

- **SUCCESS**: chase ASK opens, descriptions spoken on navigation, commit announces brief line, chase resumes promptly. Move to v0.15.9 (Auto = run-from-robot).
- **DESCRIPTION TOO LONG**: if the engine truncates a label visually or rejects it, fall back to shorter labels (e.g. just "Manual" / "Auto" / "Original") with the description moved into a separate per-cursor-move announce.
- **REGRESSION**: any return of v0.15.8 BAT's three-stack TTS would mean either announceCommit didn't gate or CommitChoice still has the verbose strings. Diagnose via the new arm-time log line.

### Files changed

- `src/dialog_inject.h` -- announceCommit parameter added to OpenAsk with rationale comment; default true.
- `src/dialog_inject.cpp` -- PHASE2_NAME_CAP bump 32 -> 64, msg buffer bumps 64 -> 128 in two spots, s_phase2AnnounceCommit state var, OpenAskInternal new bool param, gating in commit branch with new suppression log line, arm-time log shows announceCommit flag, Phase2_TestAsk pass-through with announceCommit=true, public OpenAsk pass-through.
- `src/chase_ask_overlay.cpp` -- kChaseChoices descriptive strings (3 entries), OpenAsk call passes announceCommit=false with rationale comment, CommitChoice trimmed to brief single-line announces (4 cases including default fallback), v0.15.8.1 design rationale in code comments.
- `src/ff8_accessibility.h` -- FF8OPC_VERSION = "0.15.8.1" with ASCII-only inline summary plus full rationale carried forward.
- `CHANGELOG.md` -- this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated for v0.15.8.1.

### Risk

Very low. Description-strings change is data-only; only ASCII punctuation that EncodeFf8 covers (colon 0x2D, space 0x20, period 0x3B, letters). announceCommit gating is a single boolean check; default-true preserves Phase2_TestAsk's existing diagnostic behavior. Buffer size bumps are safe (snprintf truncates; we're enlarging not shrinking). chase_ask_overlay's MODE_MANUAL routing unchanged.

Edge case: if the engine's dialog box has visual layout limits per option line, descriptive labels (~30 chars each) might overflow on screen visually. Not a functional concern -- Aaron is blind, and the [ASK] hook reads the encoded buffer regardless of visual rendering, so the TTS path is unaffected. If a sighted user later complains, we can shorten labels or split the description into a separate announce.

### Next

After v0.15.8.1 BAT SUCCESS, v0.15.9 reclaims its planned slot for Auto = run-from-robot (X-ATM092 chase auto-flee). v0.15.10 stays Original = chase-mod-active flag (vanilla chase behavior). Auto and Original descriptions in `kChaseChoices` update at those points to reflect implemented behavior.

## v0.15.8

Wire `chase_ask_overlay` into DialogInject's OpenAsk pipeline. v0.15.7.1 BAT proved end-to-end -- dialog opens via the engine, cursor input announces, commit captured on X. Now the chase mode prompt actually renders as a real engine dialog box instead of the v0.15.2.2 TTS-only fallback.

### What ships

**dialog_inject** -- two new public APIs:

```cpp
bool OpenAsk(const char* prompt,
             const char* const* choices,
             int numChoices,
             int defaultCursor,    // 1-based
             int slot);            // typically 2

void ResetLastAnswer();
```

Internal refactor: `Phase2_TestAsk`'s body factored into `OpenAskInternal(prompt, choices, numChoices, defaultCursor, slot, logBanner)`. `Phase2_TestAsk` is now an 8-line wrapper that calls `OpenAskInternal` with the hardcoded `"Mode?" / {"Manual","Auto","Original"}` test buffer. The public `OpenAsk` calls the same internal with caller-supplied parameters. Single code path = single set of bugs.

`CurQToOptionName` replaced with a lookup into `s_phase2ChoiceNames[8][32]` populated by `OpenAskInternal` from caller strings. The cursor-change announcer in `Update()` now speaks any caller-supplied choice name; v0.15.7's hardcoded `Manual/Auto/Original` switch is gone. Bound checks also generalized: previously `curQ <= 3`, now `curQ <= s_phase2ChoiceCount`.

**chase_ask_overlay** -- full rewrite. Drops:

- All proxy-slot scaffolding (`PopulateProxySlot`, `ReleaseProxySlot`, `LogProxySlotSnapshot`, `SyncEngineCursor`, the FF8 `EncodeChar` table, `WIN_OBJ_*` template constants, geometry templates, callback constants).
- The manual `VK_UP/VK_DOWN/VK_RETURN/'1'/'2'` polling and `s_*WasDown` edge-detect state.
- `SpeakInitialPrompt`, `SpeakHighlightChange`, `s_currentHighlight`, `s_encodedOptionsBuf`.
- The 2-choice `AskChoice` enum (replaced by 3 answer constants matching DialogInject's 1-based answer values).

Keeps:

- The trigger detection in `OnDialogText` matching Squall's `"Forget it!  Let's go!"`.
- The 3-second deferred-open delay (`TRIGGER_DELAY_MS` = 3000) so Squall's line plays first.
- The once-per-chase gate (`s_askFiredThisChase`) cleared on `ChaseDetector::IsChaseActive()` falling edge.
- `IsAskActive()` for future input-gating consumers.

New flow:

```cpp
static void OpenAsk() {
    DialogInject::ResetLastAnswer();
    bool ok = DialogInject::OpenAsk("Mode?",
                                    {"Manual","Auto","Original"},
                                    3, 1, 2);
    if (ok) { s_askOpen = true; s_askFiredThisChase = true; }
}

void Update() {
    // ... deferred-open timer, chase-end edge detection ...
    if (s_askOpen) {
        int answer = DialogInject::GetLastAnswer();
        if (answer != -1) CommitChoice(answer);
    }
}

static void CommitChoice(int answer) {
    // Per Aaron's v0.15.8 plan: all three options route to MODE_MANUAL.
    // Auto / Original announce 'not yet implemented, falling back to manual'.
    // v0.15.9 will implement Auto = run-from-robot.
    // v0.15.10 will implement Original = chase-mod-active flag.
    ChaseDetector::SetChaseMode(ChaseDetector::MODE_MANUAL);
}
```

### Choice list and dispatch (per Aaron's v0.15.8 plan)

- **Manual** (default cursor): "Manual mode selected. One battle per chase field will be allowed." -> `MODE_MANUAL`.
- **Auto**: "Auto is not yet implemented. Falling back to manual mode." -> `MODE_MANUAL`.
- **Original**: "Original is not yet implemented. Falling back to manual mode." -> `MODE_MANUAL`.

All three currently land at `MODE_MANUAL` because Auto and Original aren't implemented yet. The mode-specific announcement makes the user's choice clear and signals the fallback.

### What does NOT change in v0.15.8

- **Squall and party walking during the ASK.** Per Aaron's instruction (v0.15.7.1 trail explains the engine architecture), input gating is deferred until after wiring is proven. The ASK opens, takes input, and dispatches correctly; the player just has to live with characters wandering during the dialog. Will be revisited in a later version.
- **Number-key shortcuts (1/2/3).** Removed -- per Aaron's v0.15.8 plan, players are already used to FF8's natural arrow + X navigation in dialogs and dropping the shortcuts keeps the UX consistent.
- **`Phase2_TestAsk` (Shift+F12).** Stays in code as a fallback diagnostic. Aaron will not toggle it during BAT unless instructed. Once v0.15.8 chase wiring is fully proven (i.e., chase ASK fires correctly across multiple chase entries), the test can be removed in a cleanup version.

### Predicted v0.15.8 BAT outcome

1. Trigger the chase scene (any chase field where Squall says `"Forget it!  Let's go!"`).
2. Squall's line plays normally through field_dialog's TTS path.
3. After ~3 seconds (TRIGGER_DELAY_MS), the engine dialog box opens. Hear `"Mode?. Selected: Manual. Auto. Original"` followed by `"Manual selected"` (from DialogInject's cursor-change announcer with curQ initial transition `255 -> 1`).
4. Press **Down** -> hear `"Auto selected"` + cursor SFX.
5. Press **Down** again -> hear `"Original selected"`.
6. Press **X** (FF8 confirm) -> hear `"You chose Original"` (from DialogInject's commit announcer), then `"Original is not yet implemented. Falling back to manual mode."` (from chase_ask_overlay's CommitChoice), then `ChaseDetector` log line for the mode change.
7. Subsequent chase battles cap at 1 per field.

Log signature in `Logs/ff8_dialog.log`:

```
[FIELD] ChaseAskOverlay: chase trigger MES detected: "Forget it!  Let's go!"; deferring ASK open by 3000 ms
[FIELD] ChaseAskOverlay: deferred-open timer expired; opening ASK now
[DLG-INJ] ===== OPEN-ASK TEST #N START =====
[DLG-INJ] v0.15.8 override text: "Mode?\nManual\nAuto\nOriginal" -> 27 bytes encoded
[DLG-INJ] FIRING opcode_ask(...)...
[DLG-INJ] opcode_ask returned 1
[DLG-INJ] v0.15.7 answer-detection armed for slot 2
[FIELD] ChaseAskOverlay: chase ASK opened via DialogInject (slot=2, 3 choices, default cursor=1)
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 255->1 announce="Manual selected"
[DLG-INJ] v0.15.7.1 active-state observed slot=2 t+~450ms (state=0xD D2=0x04); commit gating now armed
            ... user navigates and presses X ...
[DLG-INJ] v0.15.7 commit reason=state left 0xD capturing answer=3
[DLG-INJ] v0.15.7 announce="You chose Original"
[FIELD] ChaseAskOverlay: DialogInject::GetLastAnswer returned 3; dispatching
[FIELD] ChaseAskOverlay: committed choice = 3 (Original)
[FIELD] ChaseAskOverlay: chase ASK closed
```

### BAT outcomes

- **SUCCESS**: ASK opens 3s after Squall's line, all three options announce on cursor moves, X commits, mode-specific message + manual fallback announce, chase proceeds normally. Move to v0.15.9 (Auto = run-from-robot).
- **NO DIALOG OPENS**: check log for `chase trigger MES detected` (did `OnDialogText` match?) and `deferred-open timer expired` (did the 3s timer fire?) and `DialogInject::OpenAsk returned false` (did the slot-busy guard trip?). If `OpenAsk returned false` with no other clue, slot 2 may be in use by another dialog -- try a different slot or wait longer.
- **DIALOG OPENS BUT NO ANSWER CAPTURED ON X**: same diagnosis as v0.15.7.1 NO COMMIT path -- check `[DLG-INJ] v0.15.7.1 active-state observed` line fired (gating armed?) and which commit signal the engine produces.
- **COMMITS BEFORE USER PRESSES X**: regression of v0.15.7.1's premature-commit fix; would mean the gate flag isn't being respected, but we're calling the same Update() so unlikely.
- **CRASH**: SEH-guarded reads in DialogInject::Update + chase_ask_overlay's own state is benign; would be a build error or a missing forward decl.

### Files changed

- `src/dialog_inject.h`: v0.15.8 entry in design rationale; `OpenAsk` and `ResetLastAnswer` public decls; `GetLastAnswer` doc updated to describe choice-list semantics (1=first choice, 2=second, ..., -1=no answer).
- `src/dialog_inject.cpp`: v0.15.8 comment trail; `s_phase2ChoiceNames[8][32]` and `s_phase2ChoiceCount` state; `CurQToOptionName` rewritten as array lookup; cursor-change bound check uses `s_phase2ChoiceCount`; `OpenAskInternal` factored from `Phase2_TestAsk` body; `Phase2_TestAsk` rewritten as 8-line wrapper; new `OpenAsk` and `ResetLastAnswer` implementations.
- `src/chase_ask_overlay.cpp`: full rewrite (~220 lines down from ~460). Removes proxy-slot code, EncodeChar, manual key polling. Calls `DialogInject::OpenAsk` and polls `GetLastAnswer`.
- `src/chase_ask_overlay.h`: slim public API; v0.15.8 comment trail; updated description of the two-path implementation -> single-path via DialogInject.
- `src/ff8_accessibility.h`: version bump to 0.15.8 + comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.8 ready-to-BAT.

### Risk

Medium. The dialog wiring path itself is proven (v0.15.6.2 + v0.15.7.1 BAT-passed end-to-end with the hardcoded test buffer). New risk surface:

1. **OpenAskInternal refactor**: extracted ~250 lines of code into a parameterized function. Phase2_TestAsk should behave identically to v0.15.7.1 since it calls OpenAskInternal with the same hardcoded values; if the refactor is bug-free, Shift+F12 still works. (Aaron won't toggle Shift+F12 during BAT, but it's there as a safety net.)
2. **chase_ask_overlay's deferred-trigger interaction with DialogInject**: ResetLastAnswer is called immediately before OpenAsk so any stale answer is wiped; OpenAsk arms detection only on retCode==1; GetLastAnswer returns -1 until commit. The polling path is similar to the v0.15.7.1 standalone test loop, just driven by chase_ask_overlay's once-per-chase entry rather than Shift+F12.
3. **Slot-busy edge case**: if `DialogInject::OpenAsk` returns false (slot 2 in use by some other engine dialog), `s_askFiredThisChase` stays false and a future trigger MES could try again. Trigger MES only fires once per chase entry naturally so this is mostly defensive.
4. **Squall-walking during ASK**: known limitation per Aaron's instruction. Doesn't affect functional correctness; just affects UX feel.

## v0.15.7.1

Premature commit fix from v0.15.7 BAT. v0.15.7 BAT log showed answer-detection firing `commit reason=state left 0xD` on the very first poll after arming, before Aaron pressed any key. Aaron's reported behavior matched: cursor announces worked, but the mod said "You chose Manual" before he made a selection.

### v0.15.7 BAT diagnosis

```
10:02:21 [DLG-INJ] opcode_ask returned 1
10:02:21 [DLG-INJ] POST ASK slot=2 trans=0x0000 vel=0x0200 state=0x00000000
10:02:21 [DLG-INJ] v0.15.7 answer-detection armed for slot 2 (timeout 60000 ms)
10:02:21 [DLG-INJ] v0.15.7 cursor-change slot=2 curQ 255->1 announce="Manual selected"
10:02:21 [DLG-INJ] v0.15.7 commit reason=state left 0xD capturing answer=1   <-- WRONG
```

The slot's state field at `+0x24` starts at `0x00000000` immediately after `opcode_ask` returns. It progresses `0x00 -> 0x01 -> 0x0D` over ~450 ms (the 3-second diagnostic poll captured this clearly: poll #0 state=0x00, poll #2 state=0x01, poll #4 state=0x0D, poll #4-#27 state=0x0D until shutdown). v0.15.7's commit detector treated `state != 0xD` as a commit signal -- so the initial transient `state == 0` satisfied "left 0xD" before `0xD` was ever entered.

The cursor-change announce fired correctly (the `0xFF -> 1` transition is the initial state populating), and the commit fired one frame later because `state == 0` is not `0xD`. By the time the dialog actually rendered and entered cursor-active state, our answer-detection was already disarmed.

### v0.15.7.1 fix

Gate the `state left 0xD` and `D2 bit clear` commit branches on having OBSERVED the cursor-active state at least once. New state variable `s_phase2SeenActive` flips true the first time `Update()` reads `state == 0xD` AND the `gameObj.D2` bit for our slot is set. Until then both natural commit signals are suppressed; once observed, normal commit detection takes over.

The 60-second timeout is unconditional -- it doesn't gate on `s_phase2SeenActive`, so a stuck arming (slot never reaches `0xD`, e.g. because the dialog never opened) can't poll forever.

New log line confirms gating transitioned:

```
[DLG-INJ] v0.15.7.1 active-state observed slot=2 t+~450ms (state=0xD D2=0x04); commit gating now armed
```

Expect this to fire ~450 ms after the `armed` line, then nothing else from the answer-detection block until the user presses X (FF8 confirm).

### Documentation correction: confirm key is X

v0.15.7 docs and code comments referenced "Enter" as the commit key. The actual FF8 confirm key is X (or whatever the player has bound in FF8 controls). The mod doesn't intercept the key; we observe slot state changes either way -- the engine consumes X internally and clears either the D2 bit or transitions state out of 0xD. Doc-only fix; no behavior change.

### Squall and party still walking during the ASK

Known limitation deferred. Aaron reported during the v0.15.7 BAT that the cursor announces work but Squall and party walk around freely while the ASK is open. Background:

The engine's natural ASK rendering doesn't directly block field-character movement. The script-VM normally parks on `opcode_ask` returning 1 (the wait-for-answer path) so subsequent script opcodes -- which would move characters via `set_pos` etc. -- don't run until the player commits. But field input flows independently through `update_field_entities`, reading the keyboard and moving the player character, regardless of dialog state.

Our injected `opcode_ask` populates the slot correctly but doesn't keep the script-VM parked because we're not running it. The natural "don't move party while waiting" behavior depends on script ordering, not a global input-block flag.

Resolution path:

- **v0.15.8 (chase wiring)** is the right place to address this. `chase_ask_overlay::OpenAsk` already gates input during chase ASKs via its own input-suppression flag. Once Phase 2B is wired into the chase overlay, the gating inherits naturally -- the standalone `Phase2_TestAsk` test isn't user-facing, so its missing input gate is acceptable.
- **v0.15.7.2 alternative**: if the standalone test feels broken enough to warrant a fix now, we can intercept arrow/dpad reads via the existing `dinput8.dll` proxy and zero them out while `s_phase2Active` is true (and `s_phase2SeenActive` is true to avoid blocking input during the open animation). Risk: shared input source between cursor and walk -- need to verify the engine reads cursor through a different code path than walk, or selectively gate by examining the call site.

Deferring to v0.15.8 unless Aaron specifically wants the standalone test cleaned up first.

### Predicted v0.15.7.1 BAT outcome

1. Clean save reload, slot 2 fresh.
2. Press **Shift+F12**. Hear: "Mode?. Selected: Manual. Auto. Original" + diagnostic + "Manual selected" (initial cursor announce). Cursor SFX on arrows works as before.
3. Press **Down**, **Down**, **Up** -- cursor announces work.
4. **No premature commit.** The dialog stays open until the user presses X.
5. Press **X**. Hear: "You chose <Option>" announce.

Log should show:

```
[DLG-INJ] v0.15.7.1 active-state observed slot=2 t+~450ms (state=0xD D2=0x04); commit gating now armed
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 1->2 announce="Auto selected"      (Down)
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 2->3 announce="Original selected"  (Down)
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 3->2 announce="Auto selected"      (Up)
[DLG-INJ] v0.15.7 commit reason=D2 bit clear capturing answer=2                (X)
[DLG-INJ] v0.15.7 announce="You chose Auto"
```

(Or `reason=state left 0xD` -- both are valid commit signals, gated on `s_phase2SeenActive`.)

### BAT outcomes

- **SUCCESS**: Aaron makes selections without premature commit, hears "You chose X" only after pressing X. Move to v0.15.8 chase wiring. Squall/party walking during ASK acknowledged as deferred.
- **STILL PREMATURE COMMIT**: `s_phase2SeenActive` never went true. Inspect log: did `[DLG-INJ] v0.15.7.1 active-state observed` line fire? If not, why not -- did slot 2's state never reach 0xD? Was D2 bit ever set? Compare to the 3-second diagnostic poll which clearly showed both.
- **NO COMMIT EVER**: `s_phase2SeenActive` went true but neither `state left 0xD` nor `D2 bit clear` fires on X. Means the engine commits via a different mechanism we don't observe. Investigate which slot fields change on X press.
- **CRASH**: pure SEH-guarded reads, can't crash directly.

### Files changed

- `src/dialog_inject.h`: design rationale extending v0.15.7.1 trail with the three v0.15.7 BAT findings (premature commit, X confirm key, party-walk limitation).
- `src/dialog_inject.cpp`: comment trail extends; new `s_phase2SeenActive` state var; `Update()` adds active-state observation block before cursor-change/commit blocks; commit detection now gates on `s_phase2SeenActive` for `state left 0xD` and `D2 bit clear` (timeout still unconditional); `Phase2_TestAsk` clears `s_phase2SeenActive` on arm; `Shutdown` clears it; comment line referencing "Enter" updated to "X".
- `src/ff8_accessibility.h`: version bump to 0.15.7.1 + comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.7.1 ready-to-BAT state. Confirm key documented as X.

### Risk

Very low. Adds a single boolean flag and gates two existing branches on it. No engine writes; no new hooks; no new addresses. The 60-second timeout safety net unchanged.

## v0.15.7

Phase 2b answer detection. v0.15.6.2 BAT confirmed end-to-end mod-driven dialog rendering with custom FF8-encoded text working under FFNx -- Aaron heard "Mode?. Selected: Manual. Auto. Original" through the engine ASK render path, with cursor SFX on arrow keys. v0.15.7 layers per-frame cursor polling and commit detection on top so the user's selection is announced and captured.

### What ships

While a Phase 2B ASK is open, `DialogInject::Update()` polls `slot+0x2B` (current_choice_question, the cursor position the engine updates on Up/Down arrows). On each cursor change announce the new option via SAPI: "Manual selected" / "Auto selected" / "Original selected", with `interrupt=false` so the announcement queues after any in-flight TTS rather than preempting it.

Commit is detected via three independent conditions, any one of which fires:

1. `gameObj.D2` bit for our slot clears -- the engine consumed the ASK and moved on.
2. The slot's state field at `+0x24` transitions out of `0xD` -- the engine left the cursor-active state.
3. 60-second timeout (sanity ceiling). Generous for any user pondering time; prevents unbounded polling if neither natural commit signal fires.

On commit, capture the most recent observed cursor value (`s_phase2LastCurQ`) as the answer, announce "You chose <Option>" via SAPI, store the answer in `s_phase2LastAnswer` for v0.15.8's chase wiring to read via the new `GetLastAnswer()` public API, and disarm the active flag.

New public API:

```cpp
int GetLastAnswer();   // returns 1=Manual, 2=Auto, 3=Original, -1=no commit yet
```

The new per-frame poll runs concurrently with the existing 3-second slot-state diagnostic poll. Both are gated independently. Pure read of slot bytes plus SAPI calls; no engine state writes; no new hooks.

### Cosmetic correction: curQ is at slot+0x2B, not slot+0x2C

v0.15.5.1's POST-ASK readback labeled `slot+0x2C` as `curQ` and read it as such. Cross-check during v0.15.7 implementation: `field_dialog.cpp`'s offset constants (`WIN_OBJ_FIRST_Q_OFFSET=0x29`, `WIN_OBJ_LAST_Q_OFFSET=0x2A`, `WIN_OBJ_CUR_CHOICE_OFFSET=0x2B`) place curQ at `0x2B`. Cross-check via v0.15.6.2 BAT log: the `[ASK] win[2]` hook in field_dialog.cpp reads `curChoice` from `0x2B` and produced `curChoice=1`, matching our `TEST_ASK_CUR_Q=1`. The dialog_inject.cpp empirical-arg-map comment had arg4/arg5 crossed (claimed curQ at 0x2C, aux at 0x2B). v0.15.7 corrects:

- The empirical-arg-map comment block: arg4 -> slot+0x2B (curQ), arg5 -> slot+0x2C (aux).
- The POST-ASK readback in `Phase2_TestAsk`: log `slot[+0x2B]curQ=N` and `slot[+0x2C]aux=N` separately.
- New `WIN_OBJ_CUR_Q_OFFSET = 0x2B` constant for `ReadSlotCurQ()`.

The v0.15.5.1/.5.2/.6.x BATs were not affected by this -- the slot fields were populated correctly by the engine; only the readback log line was misleading. v0.15.6.2 still rendered the right cursor on the right line because the engine's input handler reads its own offset, not ours.

### Predicted v0.15.7 BAT outcome

1. Clean save reload, slot 2 fresh.
2. Press **Shift+F12**: hear "Mode?. Selected: Manual. Auto. Original" (FieldDialog [ASK] hook), then queued diagnostic "Dialog inject phase two B. Slot 2. Return code 1." Same as v0.15.6.2 SUCCESS.
3. Press **Down** arrow: cursor moves Manual -> Auto, hear FF8 cursor-move SFX, hear "Auto selected" within ~100ms.
4. Press **Down** again: hear "Original selected" + cursor SFX.
5. Press **Up**: hear "Auto selected" + cursor SFX.
6. Press **Enter** (engine commit key for ASK): hear "You chose Auto".

Log signature for steps 3-5:

```
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 1->2 announce="Auto selected"
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 2->3 announce="Original selected"
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 3->2 announce="Auto selected"
```

Log signature for step 6:

```
[DLG-INJ] v0.15.7 commit reason=D2 bit clear capturing answer=2
[DLG-INJ] v0.15.7 announce="You chose Auto"
```

(Or `reason=state left 0xD` -- both are valid commit signals.)

### v0.15.7 BAT outcomes

- **SUCCESS**: each cursor move produces "X selected" within ~100ms; commit produces "You chose X". Move to v0.15.8 chase_ask_overlay wiring.
- **NO CURSOR ANNOUNCE**: `slot+0x2B` not changing on arrows. Possible causes: the engine writes curQ to a different offset under FFNx (unlikely -- field_dialog.cpp's hook reads from `0x2B` and confirmed correct curChoice in v0.15.6.2 BAT), or our 0x2B reads are racing with the engine's writes (also unlikely -- single-threaded, byte reads are atomic on x86). Inspect log: are `[DLG-INJ] v0.15.7 cursor-change` lines firing at all? If so, with what curQ values?
- **DOUBLE/STUTTERING ANNOUNCE**: cursor poll firing too fast, OR the same curQ value being detected as a change (loose dedup). The `s_phase2LastCurQ` check should prevent this -- inspect log for repeated `cursor-change slot=2 curQ N->N` (same value).
- **NO COMMIT DETECTION**: neither `D2 bit clear` nor `state left 0xD` fires on Enter. Investigate the engine's commit mechanism -- it might write the answer to a different slot field or push it back to the script-VM through a path we don't observe. Worst case: rely on the 60s timeout (announce "You chose <last cursor>" after 60 seconds, which is too slow for v0.15.8 chase wiring).
- **WRONG ANSWER**: cursor poll captured stale curQ at commit time. We use `s_phase2LastCurQ` (last observed) rather than re-reading at commit, so this would mean we missed a cursor change just before commit. Possible fix: poll faster, or also re-read at commit moment as a tiebreaker.
- **CRASH**: pure SEH-guarded slot reads + SAPI calls, no engine writes. If something crashes it's downstream of this code.

### Known-but-deferred (resolved by v0.15.8)

- **Squall walks while cursor moves.** Standalone Phase 2B doesn't suspend field input. The chase_ask_overlay wiring in v0.15.8 inherits existing input gating that handles this.
- **Test buffer is hardcoded.** Phase 2B uses "Mode? / Manual / Auto / Original" verbatim. v0.15.8 will accept caller-supplied text via a new `OpenAsk()` API that takes a prompt and choice list.

### Files changed

- `src/dialog_inject.h` (~25 lines): design rationale extending v0.15.7 trail; new `GetLastAnswer()` public decl with documented return-value semantics.
- `src/dialog_inject.cpp` (~150 lines): comment trail extends; EMPIRICAL ARG MAP comment corrected (arg4 -> 0x2B curQ, arg5 -> 0x2C aux); new `WIN_OBJ_CUR_Q_OFFSET=0x2B` constant; `s_phase2Active`/`Slot`/`LastCurQ`/`LastAnswer`/`StartMs` state with `PHASE2_TIMEOUT_MS=60000` sanity timeout; `GetLastAnswer`/`CurQToOptionName`/`ReadSlotCurQ`/`ReadSlotState` helpers; `Update()` extended with answer-detection block ahead of the existing 3-sec diagnostic poll; `Phase2_TestAsk` arms detection only when `retCode==1`; `Shutdown` disarms detection state; POST-ASK readback corrected to read 0x2B for curQ and 0x2C for aux.
- `src/ff8_accessibility.h`: version bump to 0.15.7 + comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.7 ready-to-BAT state.

### Risk

Very low. The new poll is a per-frame read of one byte (`slot+0x2B`), one bitmask byte (`gameObj.D2`), and one DWORD (slot state at `+0x24`), all SEH-guarded via existing helpers. SAPI calls are the same `ScreenReader::Speak(msg, false)` pattern proven in v0.15.5.3. No engine state writes, no hooks, no new addresses. The 60-second timeout prevents unbounded polling in the worst case where neither commit signal fires.

## v0.15.6.2

Phase 2b fix follow-up. v0.15.6.1 BAT confirmed our pointer swap landed cleanly but Aaron still heard Selphie's natural elevator dialog. Root cause: `field_dialog.cpp`'s `IsValidTextPointer` heuristic capped accepted pointers at `0x30000000` (FF8 heap range), and our static override buffer at `0x6E98E020` lives in the DLL data section, well above that cap. Both the TTS path and the show_dialog fallback rejected our buffer.

### v0.15.6.1 BAT diagnosis

v0.15.6.1 BAT log at 00:20:55, Phase 2B Test #1, clean slot 2 (D2=0x00 PRE):

```
[DLG-INJ] FIRING opcode_ask(0x65315610)(ctx=0x6E98DD10)...
[DLG-INJ] sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02
[DLG-INJ] v0.15.6.1 SetOverride active for slot 2; opcode_ask post-call patch will swap slot[+0x08] = 0x6E98E020
FieldDialog: [POST-ASK-OVERRIDE] Patched slot[2]+0x08: 0x16C3EE98 -> 0x6E98E020   <-- our patch fired
[DLG-INJ] v0.15.6.1 ClearOverride called
[DLG-INJ] opcode_ask returned 1
[DLG-INJ] POST ASK ... text1=0x6E98E020 (override=0x6E98E020)                       <-- pointers match
```

The pointer swap landed cleanly. `slot+0x08` holds our override buffer's address and the post-call read confirms it. But the next entries are wrong:

```
FieldDialog: [SHOW_DIALOG-TEXT] win[2] mode=1 ... tr=512 [T2] text="Selphie \"Wanna go up?\" Go up Stay"
FieldDialog: [SHOW_DIALOG-SPEAK] win[2] mode=1 Speaking: "Selphie \"Wanna go up?\" Go up Stay"
```

Two signals from the log:

1. **No `[ASK] win[2] Speaking:` line.** `Hook_opcode_ask`'s `ScanAndSpeakChoiceWindows` runs immediately after our patch in the same call, and normally produces `Parsed N choices ...` and `Speaking: ...` lines for slot 2. None appeared. Looking at the loop: `ScanAndSpeakChoiceWindows` calls `IsValidTextPointer(text1)` and `continue`s when it returns false. Our pointer (`0x6E98E020`) is above the `0x30000000` cap, so it failed validation and the slot was silently skipped.
2. **`[T2]` tag in `[SHOW_DIALOG-TEXT]`.** `Hook_show_dialog` checks `text_data1` first; if `IsValidTextPointer(text1)` fails, it falls back to `text_data2` (slot+0x0C). Our text1 (`0x6E98E020`) failed the same check. text2 still held the engine's natural pointer (`0x16C3EE98`, well inside the FF8 heap range, validated fine), so show_dialog decoded that and `[SHOW_DIALOG-SPEAK]` spoke Selphie's dialog.

Both issues stem from the same heuristic: `IsValidTextPointer` was tuned for FF8 heap addresses (~`0x00010000`-`0x30000000`) and rejects pointers in the DLL data section above that range. The check exists to filter out spurious pointers like menu glyphs (~`0xFFFFFFFF`) or stack addresses; it wasn't designed with mod-injected buffers in mind.

### v0.15.6.2 fix

Expose the override buffer's stable address range via two new accessors and whitelist that exact range in `IsValidTextPointer`.

**dialog_inject.{h,cpp}**: new `GetOverrideBufferStart()` and `GetOverrideBufferSize()` public APIs return `s_overrideBuffer`'s address and `OVERRIDE_BUFFER_SIZE` respectively. They do NOT depend on the override flag being active -- show_dialog can fire after `ClearOverride` returns and still need to validate the buffer.

**field_dialog.cpp**: `IsValidTextPointer` body extended:

```cpp
static bool IsValidTextPointer(const char* ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (addr >= 0x00010000 && addr <= 0x30000000) return true;
    // v0.15.6.2: whitelist DialogInject's static override buffer.
    const unsigned char* obStart = ::DialogInject::GetOverrideBufferStart();
    if (obStart != nullptr) {
        uintptr_t obStartAddr = (uintptr_t)obStart;
        uintptr_t obEndAddr   = obStartAddr + (uintptr_t)::DialogInject::GetOverrideBufferSize();
        if (addr >= obStartAddr && addr < obEndAddr) return true;
    }
    return false;
}
```

The whitelist accepts addresses within the override buffer's exact byte range only. We do not blanket-accept all addresses above `0x30000000` -- spurious pointers from elsewhere in the DLL data section or stack still fail. The buffer is statically allocated and its location is fixed for the DLL's lifetime, so the comparison is safe and stable.

The forward-decl block in field_dialog.cpp adds the two new function declarations alongside the existing `IsOverrideActive` / `GetOverrideText` / `GetOverrideSlot` decls.

### Predicted v0.15.6.2 BAT outcome

Clean save reload (slot 2 fresh), press Shift+F12 once. Expected log signature:

```
[DLG-INJ] PRE  ASK gameObj.D2(ASK)=0x00 ...                                       (clean slot 2)
[DLG-INJ] v0.15.6.1 SetOverride active for slot 2; ...
FieldDialog: [POST-ASK-OVERRIDE] Patched slot[2]+0x08: 0x... -> 0x6E98E020         (same as v0.15.6.1)
FieldDialog: [ASK] win[2] Parsed 3 choices (firstQ=1 lastQ=3 curChoice=N)          (NEW -- previously skipped)
FieldDialog: [ASK] win[2] Speaking: "Mode?. Selected: Manual. Auto. Original"      (NEW)
[DLG-INJ] opcode_ask returned 1
[DLG-INJ] POST ASK ... text1=0x6E98E020 (override=0x6E98E020)
```

Aaron hears "Mode?. Selected: Manual. Auto. Original" first, then the queued diagnostic "Dialog inject phase two B. Slot 2. Return code 1." Arrow keys move cursor between Manual/Auto/Original with FF8's cursor-move SFX.

Subsequent show_dialog calls during the slot poll may also speak our text (text1 is now valid for them too), but should be deduped via `ws.lastSpokenText` / `ws.lastRawText` since `ScanAndSpeakChoiceWindows` ran first and stored both versions.

### BAT outcomes

- **SUCCESS**: Aaron hears "Mode?. Selected: Manual. Auto. Original" before the queued diagnostic. `[ASK] win[2] Speaking: "Mode?..."` line in log. Arrow keys move cursor with cursor-move SFX. Move to v0.15.7 answer detection.
- **PARTIAL (no SFX)**: TTS speaks correct text but cursor SFX missing. The engine reads `slot+0x2B` (curQ) for input; the `firstQ=1 lastQ=3` clamp may interact unexpectedly with the 4-line buffer (line 0 = "Mode?", lines 1-3 = choices). Inspect `slot+0x2B` polls in the post-fire window.
- **GARBLED TEXT**: `[ASK]` decodes wrong characters from our buffer. EncodeFf8 utility bug; cross-reference the `override buffer hex:` log line against `ff8_text_decode.cpp`'s decode table.
- **STILL HEARING SELPHIE**: would mean `ScanAndSpeakChoiceWindows` ran before our patch, OR `Hook_show_dialog` is hitting a path that bypasses dedup. Inspect log for `[POST-ASK-OVERRIDE]` line position relative to `[ASK]` and `[SHOW_DIALOG-SPEAK]`.
- **CRASH**: the post-ASK write is SEH-guarded so the patch can't crash directly. If something downstream crashes, compare with v0.15.5.3's clean BAT to isolate.

### Files changed

- `src/dialog_inject.h`: design rationale block extended for v0.15.6.2; new `GetOverrideBufferStart()` and `GetOverrideBufferSize()` public decls.
- `src/dialog_inject.cpp`: comment trail extended; two new accessor implementations returning `s_overrideBuffer`'s address and `OVERRIDE_BUFFER_SIZE`.
- `src/field_dialog.cpp`: forward-decl block extended with new APIs; `IsValidTextPointer` body rewritten to check the whitelist after the existing heap-range check.
- `src/ff8_accessibility.h`: version bump to 0.15.6.2 + comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.6.2 ready-to-BAT state.

### Risk

Very low. The whitelist accepts only the override buffer's exact address range, not all of DLL memory. The buffer is statically allocated and never moves. Other paths that consume `IsValidTextPointer` results (show_dialog text2 fallback, PollWindows, RAWDUMP) are unaffected by the whitelist because they operate on FF8-heap pointers, which still pass via the existing range check. The new API surface is two getter functions with no side effects.

## v0.15.6.1

Phase 2b fix. v0.15.6 BAT failed -- Aaron heard Selphie's natural elevator dialog instead of our injected "Mode? / Manual / Auto / Original" prompt. Diagnosis is in: FFNx's `replace_call` pattern bypassed our hook on the engine's `field_get_dialog_string`. v0.15.6.1 moves the substitution to a point downstream of the bypass.

### v0.15.6 BAT diagnosis

Smoking gun: the v0.04.16 hook on `field_get_dialog_string` logs every one of its first 10 calls unconditionally as `[GETSTR-RAW] call#N`. The full v0.15.6 BAT log (~1 minute of gameplay, init through Shift+F12 Phase 2B Test #4) has **zero** `[GETSTR-RAW]` lines. `show_dialog` fired thousands of times; `field_get_dialog_string` fired zero times. The hook is installed (init log: `Hooked field_get_dialog_string: target=0x00530750 trampoline=0x0FFE0E80`) but nothing reaches it.

Mechanism: FFNx uses the `replace_call` pattern to locate the engine's internal `CALL field_get_dialog_string` instruction (inside the engine's `opcode_ask` body) and rewrites the operand to point at FFNx's own implementation. Our MinHook on the engine's entry point at `0x00530750` is unreachable because no caller invokes that address anymore -- callers go through FFNx's function instead. The v0.15.6 design was hooking a dead address.

Phase 2B Test #1 (the only test with clean slot 2):

- `PRE  ASK gameObj.D2(ASK)=0x00 D3(win)=0x00 D4(MES)=0x00` (slot 2 fresh)
- `FIRING opcode_ask(0x65325610)(ctx=0x6EAADD10)`
- `sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02`
- `v0.15.6 SetOverride active; opcode_ask will receive custom text`
- `[ASK] win[2] Parsed 3 choices (firstQ=1 lastQ=3 curChoice=1)` -- our values
- `[ASK] win[2] Speaking: "Selphie. Selected: \"Wanna go up?\". Go up. Stay"` -- natural text!
- `v0.15.6 ClearOverride called`
- `opcode_ask returned 1` (success path)
- `POST ASK slot[+0x29]firstQ=1 slot[+0x2A]lastQ=3 slot[+0x2C]curQ=0 text1=0x16B24E98 (override=0x6EAAE020)`

Mechanically the call worked: `opcode_ask` reached the `.alloc` branch, populated the slot, set `gameObj.D2` bit 2, returned 1 (the wait-for-answer success code). The slot's `firstQ`/`lastQ` got our values. But `slot+0x08` text pointer (`0x16B24E98`) does not match our override buffer (`0x6EAAE020`) -- the engine wrote a different pointer because FFNx's `field_get_dialog_string` returned the natural Selphie text from the field's message table, not our override. The TTS path then decoded that pointer and spoke Selphie's text.

Tests #2-4 hit `WARNING: gameObj.D2 bit 2 already set; opcode_ask will return 5 without rendering` -- slot 2 was busy from Test #1, so nothing useful there.

### v0.15.6.1 fix

Don't rely on the bypassed `field_get_dialog_string` hook. Inside `Hook_opcode_ask` (`field_dialog.cpp`), after `s_origAsk(entityPtr)` returns, the engine has populated `slot+0x08` with the natural text pointer (`0x16B24E98`). Right there -- between `s_origAsk` returning and `ScanAndSpeakChoiceWindows` reading the slot for TTS -- check `DialogInject::IsOverrideActive()` and overwrite `slot+0x08` with our override buffer pointer.

The TTS path then decodes our text. The engine's render loop reads `slot+0x08` every frame to draw the dialog box, so visually the dialog also displays our text. The engine's input handler reads `slot+0x08` to position the cursor on choice lines. `firstQ`/`lastQ` at `slot+0x29`/`+0x2A` are already our values from the `opcode_ask` call (BAT log confirmed `firstQ=1 lastQ=3`), so cursor positions match our line layout (Manual/Auto/Original).

New API: `DialogInject::GetOverrideSlot()` returns the target slot. `SetOverride` now takes `(int slot, const char* text)` so `Phase2_TestAsk` communicates which slot to patch. `Hook_opcode_ask` uses the slot to identify which window to overwrite, so natural game ASKs in other slots are unaffected. The flag is set immediately before `opcode_ask` and cleared immediately after, so the patch only runs for our injected call.

Why post-ASK and not pre-fetch: post-ASK patching attacks the slot at a single well-defined point (after FFNx's logic completes, before our TTS scan reads it). It's robust to FFNx version changes because it doesn't depend on FFNx's internal addresses. The pre-fetch override would have required finding FFNx's `field_get_dialog_string` symbol or hooking FFNx-relative addresses -- both fragile.

### v0.15.6.1 BAT plan

1. Deploy via `deploy.vbs`.
2. Quit FF8 and re-launch (clean restart for fresh slot 2 state).
3. Load any save with field-mode access.
4. Press **Shift+F12** once. Expected:
   - Hear FIRST: "Mode?. Selected: Manual. Auto. Original" (or similar, depending on how `[ASK]` decodes the override buffer).
   - Hear THEN: "Dialog inject phase two B. Slot 2. Return code 1." (queued diagnostic).
5. Press arrow keys. Cursor should move between Manual/Auto/Original with FF8's cursor-move SFX.
6. Wait for the slot poll to complete (3 seconds).
7. Quit and send `Logs/ff8_dialog.log`.

Expected log signature:

```
[DLG-INJ] PRE  ASK gameObj.D2(ASK)=0x00 ...     (clean slot 2)
[DLG-INJ] v0.15.6.1 SetOverride active for slot 2; opcode_ask post-call patch will swap slot[+0x08] = 0x...
[DLG-INJ] FIRING opcode_ask(0x65325610)(ctx=0x...)
FieldDialog: [POST-ASK-OVERRIDE] Patched slot[2]+0x08: 0x16B24E98 -> 0x6EAAE020
FieldDialog: [ASK] win[2] Parsed 3 choices (firstQ=1 lastQ=3 curChoice=1)
FieldDialog: [ASK] win[2] Speaking: "Mode?. Selected: Manual. Auto. Original"
[DLG-INJ] v0.15.6.1 ClearOverride called
[DLG-INJ] opcode_ask returned 1
[DLG-INJ] POST ASK slot[+0x29]firstQ=1 slot[+0x2A]lastQ=3 slot[+0x2C]curQ=... text1=0x6EAAE020 (override=0x6EAAE020)
```

The key new line is `[POST-ASK-OVERRIDE] Patched slot[2]+0x08: 0x... -> 0x...` (our pointer wins). The post-call `text1` should now match `override` instead of being a different value.

### BAT outcomes

- **SUCCESS**: Aaron hears "Mode?. Selected: Manual. Auto. Original" before the queued diagnostic. Arrow keys move cursor with cursor-move SFX. `[POST-ASK-OVERRIDE]` line appears in log; `text1` matches `override`. Move to v0.15.7 answer detection.
- **GARBLED TEXT**: `[POST-ASK-OVERRIDE]` fires but `[ASK]` decodes wrong characters. EncodeFf8 utility bug; cross-reference the `override buffer hex:` log line against `ff8_text_decode.cpp`'s decode table.
- **CURSOR MISMATCH**: TTS speaks correct text but cursor SFX doesn't trigger or moves wrong. The engine reads `slot+0x2B` (curQ) for input; the `firstQ=1 lastQ=3` clamp may interact unexpectedly with our 4-line buffer. Inspect `slot+0x2B` polls in the post-fire window.
- **CRASH**: SEH catches inside opcode_ask or post-call. The post-ASK write is SEH-guarded so it can't crash; if anything crashes it's downstream of our write. Compare with v0.15.5.3's clean BAT to isolate.
- **ENGINE OVERWRITES OUR POINTER**: post-call `text1` reverts to natural pointer between `[POST-ASK-OVERRIDE]` log and `[ASK]` decode. Would mean the engine has another buffer copy we're not patching. Fallback: memcpy our encoded bytes into the engine's existing buffer location (in-place rewrite instead of pointer swap).

### Known-but-deferred (resolved by v0.15.7+)

Same as v0.15.6: no answer detection (v0.15.7), Squall walks during ASK (v0.15.8 chase wiring inherits gating).

### Files changed

- `src/dialog_inject.h`: header rewrite documenting v0.15.6 BAT failure and v0.15.6.1 design pivot. New `GetOverrideSlot()` decl alongside `IsOverrideActive`/`GetOverrideText`.
- `src/dialog_inject.cpp`: `SetOverride` signature now `(int slot, const char* text)`. New `s_overrideSlot` state var. `GetOverrideSlot()` impl. `Phase2_TestAsk` passes `TEST_SLOT_ASK` to `SetOverride`. Log line wording updated to v0.15.6.1.
- `src/field_dialog.cpp`: forward-decl block adds `GetOverrideSlot`. `Hook_opcode_ask` gets a v0.15.6.1 post-ASK patch block between `s_origAsk` return and `EnterCriticalSection`, with SEH-guarded `slot+0x08` write and `[POST-ASK-OVERRIDE]` log line.
- `src/ff8_accessibility.h`: version bump to 0.15.6.1 + comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.6.1 ready-to-BAT state.

### Risk

Low. The post-ASK patch is one SEH-guarded pointer write inside a guarded `if (IsOverrideActive())` block. The override flag is set/cleared in a tightly-scoped window around our injected `opcode_ask` call, on the same thread, so natural game ASKs (which fire on different game-thread events outside our `Phase2_TestAsk` invocation) won't see the flag set. The dead `Hook_field_get_dialog_string` override branch (added in v0.15.6) stays in the code as harmless documentation of the original approach -- it would fire if FFNx ever stopped using `replace_call` for this function, but for now it's never reached.

## v0.15.6

Phase 2b ships -- custom FF8-encoded dialog text injection via `field_get_dialog_string` hook override.

### v0.15.5.3 BAT recap (Phase 2a fully proven)

Aaron's BAT confirmed the v0.15.5.3 SAPI fix worked: with `interrupt=false` on the diagnostic announcement, he heard the dialog text spoken first (via the FieldDialog `[ASK]` hook) and then the queued "Dialog inject phase two A" diagnostic. Test #1 at 20:11:50 in `doani1_2`: `sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02`, `[ASK] win[2] Speaking: "Selphie. 'Wanna go up?'. Selected: Go up. Stay"`, opcode_ask returned 1, slot 2 trans `0 -> 0x400 -> 0x1000`, state `0 -> 1 -> 0xD`, `gameObj.D2=0x04`. Mod-driven engine dialog rendering with cursor input and proper SAPI sequencing all work end-to-end. v0.15.5.3 pushed to GitHub as commit `c58d993a` (parent `41251c39` v0.15.4).

### Phase 2b architecture: hook override pattern

v0.15.5.3 renders dialog text but the text comes from the field's natural msg 0 (Selphie elevator ASK in `doani1_2`, "Battle" in `doan1_2`). For chase wiring ("Manual / Auto / Original" prompt), we need custom text. Two paths considered:

- **Path A (rejected):** Call `set_window_object_ASK` directly with our buffer + manually trigger `sub_4A0620` open transition + manually set `gameObj.D2` bit. Reconstructs engine internals; high risk of missing state.
- **Path B (chosen):** Reuse the entire proven Phase 2a recipe (opcode_ask + sub_49FD50 + ASK-pending bytes). When opcode_ask internally calls `field_get_dialog_string(msgBase, dialogId)` at 0x5295CD to fetch the field's text, our **existing v0.04.16 hook** on that function intercepts. With a one-shot override flag set by DialogInject, the hook returns our custom buffer instead of calling the original. `set_window_object_ASK` then stores our pointer in `slot+0x08` (text_data1) and the engine renders our text. All other state -- transition machinery, gameObj bits, cursor wiring, FieldDialog `[ASK]` SAPI announcement -- is reused verbatim from the v0.15.5.x proven path.

### v0.15.6 implementation

**dialog_inject.h (~50 lines added).** Header rewrite documenting the v0.15.6 Phase 2b override pattern and the rationale for path B over A. New public decls:

```cpp
bool        IsOverrideActive();
const char* GetOverrideText();
```

These are called by `field_dialog.cpp`'s hook on the game thread. The flag is a `volatile LONG` (atomic on x86), the text pointer is 32-bit aligned. Single-threaded coordination: SetOverride immediately before opcode_ask, ClearOverride immediately after, both on the same thread that fires the hook. No race.

**dialog_inject.cpp (full rewrite, ~600 lines).** New module state:

```cpp
static const size_t OVERRIDE_BUFFER_SIZE = 256;
static uint8_t s_overrideBuffer[OVERRIDE_BUFFER_SIZE] = {0};
static volatile LONG s_overrideActive = 0;
static const char* s_overrideText = nullptr;
```

New `EncodeFf8` utility maps ASCII to FF8 dialog encoding (inverse of `ff8_text_decode.cpp`'s table): `'\n' -> 0x02`, `'A'-'Z' -> 0x45..0x5E`, `'a'-'z' -> 0x5F..0x78`, `'?' -> 0x2F`, etc. The currency symbol case (which decodes to 0x42) is written in source as the hex literal `0x24` rather than as a character literal, working around an editor-tooling hazard discovered mid-implementation that mangled inserted text containing that character.

Internal `SetOverride()` / `ClearOverride()` helpers manage the flag via `InterlockedExchange`. Public `IsOverrideActive()` / `GetOverrideText()` for the hook to call.

Phase 2 test parameter constants reworked from speculative `TEST_ASK_ARG2/3/4/5` to empirically-confirmed `TEST_ASK_FIRST_Q=1`, `TEST_ASK_LAST_Q=3`, `TEST_ASK_CUR_Q=1`, `TEST_ASK_AUX=0` (matching the v0.15.5.1 BAT empirical map: stack[SP-3] -> slot+0x29 firstQ, stack[SP-2] -> slot+0x2A lastQ, stack[SP-1] -> slot+0x2C curQ, stack[SP] -> slot+0x2B aux).

`Phase2_TestAsk` modified to: (1) encode `"Mode?\nManual\nAuto\nOriginal"` via EncodeFf8 into `s_overrideBuffer`; (2) log the encoded hex for verification; (3) keep all v0.15.5.1/.5.2/.5.3 fixes intact (ASK-pending bytes ctx[+0x174]/[+0x175], sub_49FD50 call, interrupt=false); (4) `SetOverride((const char*)s_overrideBuffer)` immediately before opcode_ask; (5) `ClearOverride()` immediately after opcode_ask returns; (6) post-fire decode now also logs `slot+0x08` text_data1 pointer alongside our override buffer address to verify the engine stored our pointer.

Diagnostic banner renamed "PHASE 2B TEST". SAPI announcement renamed "Dialog inject phase two B".

**field_dialog.cpp (~30 lines added).** New forward-declaration namespace block at file scope (before the `FieldDialog` namespace) declaring `DialogInject::IsOverrideActive()` / `GetOverrideText()`. Modified `Hook_field_get_dialog_string` to check the override at the very top -- if active and pointer non-null, log `[GETSTR-OVERRIDE]` line and return the override buffer; if active but null, log a warning and fall through; otherwise call the original game function and continue normal logging/dedup behavior. The override path bypasses the downstream pending-text logging, dedup, and getstr-call counter -- intentionally, since our injected text shouldn't pollute those tracking buffers.

**ff8_accessibility.h.** Version bump to 0.15.6 with full comment-trail entry.

**deploy.bat / dinput8.cpp.** No changes -- `dialog_inject.cpp` is already in the compile list (since v0.15.4) and the F12/Shift+F12 hotkey handlers already call `DialogInject::Phase1_TestMes()` / `Phase2_TestAsk()`.

### Predicted v0.15.6 BAT outcome

Clean save reload. Press Shift+F12 in any field (the field's natural msg 0 is irrelevant now -- our override replaces it). Expected log sequence:

```
[DLG-INJ] ===== PHASE 2B TEST #1 START =====
[DLG-INJ] v0.15.6 override text: "Mode?\nManual\nAuto\nOriginal" -> 27 bytes encoded
[DLG-INJ] override buffer hex: 51 6D 62 63 2F 02 51 5F 6C 73 5F 6A 02 45 73 72 6D 02 53 70 67 65 67 6C 5F 6A 00
[DLG-INJ] sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02
[DLG-INJ] v0.15.6 SetOverride active; opcode_ask will receive custom text
FieldDialog: [GETSTR-OVERRIDE] DialogInject providing custom text (orig msgBase=0x... dialogId=0 -> override=0x...)
FieldDialog: [ASK] win[2] Parsed 3 choices (firstQ=1 lastQ=3 curChoice=1)
FieldDialog: [ASK] win[2] Speaking: "Mode?. Selected: Manual. Auto. Original"
[DLG-INJ] v0.15.6 ClearOverride called
[DLG-INJ] opcode_ask returned 1
[DLG-INJ] POST ASK slot[+0x29]firstQ=1 slot[+0x2A]lastQ=3 slot[+0x2C]curQ=1 text1=0x... (override=0x...)
```

Critical verification: the `slot+0x08 text1` pointer should match our `s_overrideBuffer` address, proving the engine stored our pointer. Aaron should hear "Mode?. Selected: Manual. Auto. Original" spoken first, then the queued "Dialog inject phase two B. Slot 2. Return code 1." Pressing arrows during the open dialog should move the cursor between Manual/Auto/Original options with FF8's cursor-move SFX.

### Failure modes to look for

- **Override flag not seen by hook.** Log shows no `[GETSTR-OVERRIDE]` line. Likely cause: forward-decl namespace mismatch in field_dialog.cpp. Fix: confirm `::DialogInject::IsOverrideActive` resolves correctly.
- **Text encoded wrong.** Log shows `[GETSTR-OVERRIDE]` but `[ASK]` decodes garbled text. Likely cause: encoder bug. Fix: cross-reference the hex dump with `ff8_text_decode.cpp`'s table.
- **Crash inside opcode_ask.** SEH catches the exception. Likely cause: our buffer ptr is reading past terminator somehow. Fix: verify the encoder writes 0x00 terminator and the buffer is null-initialized.
- **Engine ignores our text.** `slot+0x08 text1` pointer doesn't match our buffer address. Likely cause: opcode_ask copies text rather than storing pointer (would be surprising given how Phase 2a worked). If this happens, fallback is to memcpy our encoded bytes into the slot's existing buffer location.

### What v0.15.6 does NOT yet ship

- **Answer detection.** opcode_ask returns 1 ("wait for user choice"); the engine's input handler updates slot+0x2B (curQ) on arrows but our injected call doesn't run the script-VM polling loop that reads the answer back. v0.15.7 will add per-frame polling of slot+0x2B for cursor changes (speak "Manual selected" / "Auto selected" / "Original selected" on each change) and detect commit when `gameObj.D2` bit clears.
- **Chase wiring.** Phase 2b is still a Shift+F12 standalone diagnostic. v0.15.8 will wire this into `chase_ask_overlay::OpenAsk` as the primary chase ASK path, replacing the v0.15.3 TTS-only overlay. chase_ask_overlay's existing input gating will resolve the v0.15.5.x "arrows move Squall AND cursor simultaneously" issue.
- **Auto/Original behavior.** v0.15.9 / v0.15.10 will implement the run-from-robot logic for the Auto option and the chase-mod-active flag gating for the Original option.

### Files changed

- `src/dialog_inject.h` (~50 lines: header rewrite + new override API decls)
- `src/dialog_inject.cpp` (full rewrite, ~600 lines)
- `src/field_dialog.cpp` (~30 lines: forward decl + override check at top of Hook_field_get_dialog_string)
- `src/ff8_accessibility.h` (version bump + comment trail)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Risk

Low. The override path is gated behind an explicit Shift+F12 press in field mode plus a flag that's only set inside Phase2_TestAsk's narrow window. When the flag is clear (which is the default state), the hook behaves exactly as it did in v0.15.5.3 -- the v0.15.5.3 commit's behavior is fully preserved. The one new code path (override return) is a single early-return in the hook, SEH not needed since we're returning a static buffer pointer that's known-valid.

## v0.15.5.3

Two-character SAPI fix. **v0.15.5.2 BAT showed the cursor input fix worked perfectly** -- arrow keys now move the cursor and trigger FF8's cursor-move SFX -- but Aaron heard only the diagnostic announcement, never the dialog text itself. Root cause: `AnnouncePhase2Result` called `ScreenReader::Speak(msg, true)` where `true` interrupts in-flight speech. The FieldDialog `[ASK]` hook had already started speaking the dialog text inside `opcode_ask`, and our diagnostic announcement preempted it.

### v0.15.5.2 BAT recap

Log confirmed cursor input is fully wired:

- `sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02` (the new call).
- `opcode_ask returned 1`, slot 2 trans `0 -> 0x400 -> 0x1000`, state `0 -> 1 -> 0xD`.
- `gameObj.D2=0x04` (slot 2 bit set).
- Two BATs in two different fields:
  - Test #1 in `doani1_2`: `[ASK] win[2] Speaking: "Selphie. 'Wanna go up?'. Selected: Go up. Stay"` -- the elevator ASK at msg 0.
  - Test #2 in `doan1_2`: `[ASK] win[2] Speaking: "Battle. Battle"` -- that field's msg 0 is just the word "Battle".
- Aaron heard the cursor-move SFX when pressing arrows during the open dialog.
- Aaron did NOT hear the dialog text -- only the diagnostic announcement.
- Aaron's character (Squall) walked simultaneously with the cursor moving.

### v0.15.5.3 fix (2 character changes + ~15 lines of comment)

In `src/dialog_inject.cpp`:

```cpp
// AnnouncePhase1Result and AnnouncePhase2Result both:
-       ScreenReader::Speak(msg, true);   // interrupt in-flight speech
+       ScreenReader::Speak(msg, false);  // queue, don't interrupt
```

Also adds two comment blocks documenting why -- the FieldDialog hook fires DURING opcode_ask and starts speaking the dialog text via SAPI, so our subsequent post-call Speak with `interrupt=true` was racing in and cutting it off mid-sentence. With `false`, SAPI queues the diagnostic announcement after the dialog text completes.

Phase 1's `AnnouncePhase1Result` gets the same fix proactively for consistency. The v0.15.4 BAT had the same race but Aaron didn't notice -- likely because Phase 1's MES dialog text and the diagnostic announcement are textually similar enough ("Selphie..." vs "Dialog inject phase one...") that the cut-off wasn't obvious.

Other diagnostic Speak calls (the error guards like "Dialog inject opcode address missing" or "Dialog inject crashed") still use `interrupt=true` intentionally -- those are error paths where preemption is correct.

### Predicted v0.15.5.3 BAT outcome

Clean save reload. Press F12 once: hear the elevator dialog text spoken first, then "Dialog inject phase one. Slot 1. Return code 3." Press Shift+F12 once: hear "Selphie. 'Wanna go up?'. Selected: Go up. Stay" first, then "Dialog inject phase two A. Slot 2. Return code 1." Pressing arrows during the open ASK still moves the cursor with audible SFX (v0.15.5.2 fix preserved).

### Known-but-deferred issue

Aaron also reported arrows moved Squall AND the dialog cursor SIMULTANEOUSLY. This is a **known limitation** of the standalone Phase 2a diagnostic -- our injected dialog doesn't suspend field input because we don't run the script-VM polling loop that normally blocks field movement during ASK. **Phase 2b (chase wiring, v0.15.6) will resolve this naturally** because `chase_ask_overlay` already handles input gating during ASKs. NOT addressed in v0.15.5.3.

### Files changed

- `src/dialog_inject.cpp`: 2 character changes (`true` -> `false` in two locations) + ~15 lines of new comment/rationale.
- `src/ff8_accessibility.h`: version bump to 0.15.5.3 with new comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.5.3 ready-to-BAT state.

### Risk

Minimal. The change is from "interrupt in-flight speech" to "queue speech" for two diagnostic announcements. Cannot cause crashes; cannot affect engine state; just changes SAPI scheduling.

## v0.15.5.2

Quick follow-up to v0.15.5.1 BAT. **v0.15.5.1 actually succeeded at rendering the ASK on the first Shift+F12 press** -- the apparent failure was about cursor input, not rendering. v0.15.5.2 adds the missing `sub_49FD50(slot)` call to enable arrow input for the rendered slot.

### v0.15.5.1 BAT was a clean SUCCESS

Test #2 at 19:45:49 in `doani1_2`:

- `opcode_ask` returned 1 (correct "wait for answer").
- `gameObj.D2` acquired bit 2 (`0x04`).
- Slot 2 transition advance `0 -> 0x400 -> 0x1000`.
- State machine `0 -> 1 -> 0xD` (ASK active with cursor).
- FieldDialog `[ASK]` hook fired on `win[2]` with `"Parsed 3 choices (firstQ=1 lastQ=3 curChoice=2)"`.
- SAPI spoke: `"Selphie. 'Wanna go up?'. Selected: Go up. Stay"`.

Subsequent Shift+F12 presses correctly returned 5 (slot busy) since slot 2 was still locked.

### Empirical SWO_ASK arg map (CONFIRMED)

The `slot+0x29/0x2A/0x2C` post-fire decode landed our values exactly as expected:

| Stack pos | Our value | Lands in slot field | Engine semantic |
|---|---|---|---|
| `stack[SP-3]` (arg2) | 1 | `slot+0x29` | **firstQ** |
| `stack[SP-2]` (arg3) | 3 | `slot+0x2A` | **lastQ** |
| `stack[SP-1]` (arg4) | 2 | `slot+0x2C` | **curQ** (cursor; clamped to [firstQ, lastQ] by SWO_ASK entry) |
| `stack[SP]` (arg5) | 2 | `slot+0x2B` | aux (not decoded post-fire) |

The SWO_ASK signature is therefore: `(slot, msg_id, firstQ, lastQ, curQ, aux)`. v0.15.6 Phase 2b can now confidently pass the right values for any custom ASK.

### Why arrows didn't move the cursor

The `.alloc` branch we successfully reached at `0x5295AB` does NOT call `sub_49FD50`. Only the stage-1 setup path at `0x529683` does (which we deliberately bypassed by setting `ctx[+0x174]/[+0x175]`). Without `sub_49FD50(slot)` setting the global `pCurrentDialogSlot` (BYTE at `0x01D2B51C`) to point at our slot, the engine's input handler doesn't route arrow keys to `slot+0x2B` (curQ), so the cursor stays put and Aaron doesn't hear FF8's standard cursor-move SFX -- the unambiguous "this is a navigable menu" audio cue.

### v0.15.5.2 fix (~30 lines)

In `Phase2_TestAsk`, before the `opcode_ask` call:

```cpp
const uint32_t SUB_49FD50_ADDR = 0x0049FD50;
typedef void (__cdecl *sub_49fd50_t)(int);
sub_49fd50_t sub_49fd50_fn = (sub_49fd50_t)(uintptr_t)SUB_49FD50_ADDR;

// Pre/post pCurrentDialogSlot diagnostic
const uint32_t PCURRENT_DIALOG_SLOT_ADDR = 0x01D2B51C;
uint8_t* pCurrentDialogSlot = (uint8_t*)(uintptr_t)PCURRENT_DIALOG_SLOT_ADDR;
uint8_t preCurSlot = *pCurrentDialogSlot;

sub_49fd50_fn(TEST_SLOT_ASK);

uint8_t postCurSlot = *pCurrentDialogSlot;
Log::Dialog("sub_49FD50(%d): pCurrentDialogSlot 0x%02X -> 0x%02X", ...);
```

All wrapped in SEH for safety. Hardcoded address `0x0049FD50` since `sub_49FD50` is a stable internal helper not in the JSM opcode dispatch table (no FFNx wrapping concern; the dispatch-table-wrapping pattern only applies to opcodes). If a future game-version mismatch surfaces, promote to `FF8Addresses`.

### Predicted v0.15.5.2 BAT outcome

Clean save reload (slot 2 fresh). Press F12 once (Phase 1 should still work, ret=3). Press Shift+F12 once (Phase 2a). Now:

- `opcode_ask` returns 1 same as v0.15.5.1.
- Dialog renders.
- Log shows `sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02` confirming the address resolved and the global was written.
- **Pressing arrow keys moves the cursor and triggers FF8's cursor-move SFX** -- the audio cue Aaron was looking for. (`slot+0x2B` curQ field updates as arrows are pressed; the standard cursor-move sound plays through FFNx's audio.)

If arrows still don't move the cursor: the engine's input handler may need additional state we haven't mirrored (e.g., a per-frame "current ASK slot" pointer separate from `pCurrentDialogSlot`, or a button-edge-detection variable in the script-VM globals). Iterate v0.15.5.3.

### Files changed

- `src/dialog_inject.cpp` (~30 lines added): `sub_49FD50(slot)` call with pre/post `pCurrentDialogSlot` diagnostic logging in `Phase2_TestAsk` before the `opcode_ask` call. NO changes to `Phase1_TestMes`.
- `src/ff8_accessibility.h`: version bump to 0.15.5.2 with new comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.5.2 ready-to-BAT state.

No changes to `dialog_inject.h`, `dinput8.cpp`, `deploy.bat`, or any other file.

### Risk

Very low. `sub_49FD50` is a small internal helper that writes one global byte. SEH-wrapped. The `pCurrentDialogSlot` global is normally written by the engine itself during ASK setup; we're just doing the same write the engine would have done in stage-1 setup that we bypassed. Phase 1 unchanged.

## v0.15.5.1

Fix Phase 2a PARTIAL outcome from the v0.15.5 BAT. Two-byte addition to the phantom `script_context` to make `opcode_ask` take the `.alloc` rendering path instead of the answer-correlation early-exit path.

### What v0.15.5 BAT showed

Phase 1 (F12 -> opcode_mes on slot 1): identical to v0.15.4 success. ret=3, slot 1 trans 0 -> 0x400 -> 0x1000, gameObj.D3=D4=0x02, FieldDialog hook spoke the elevator dialog. Phase 1 is solid.

Phase 2a (Shift+F12 -> opcode_ask on slot 2): ret=1 but slot 2 entirely untouched across all 28 polls -- trans=0x0000 vel=0x0000 state=0x0 field16=0x00, gameObj.D2=0x00, slot[+0x29] firstQ=0xFF and slot[+0x2A] lastQ=0xFF (both still at 0xFF default placeholder set by SWO_ASK's first writes -- meaning SWO_ASK was NEVER called).

The Test #3 FieldDialog `[ASK]` hook line spoke slot 1 content (the natural Phase 1 MES from 5 seconds earlier that hadn't cleared) -- not slot 2. That's the existing v0.04.36 hook's normal post-call scan finding slot 1 still populated; it doesn't reflect our injected call rendering anything.

### Diagnosis

Extended the disassembly walk through `0x52956D-0x5296B6` (10 more anchors past v0.15.5's stopping point). The early-exit path is gated on two persistent script_context bytes:

```
0x0052956D:  cl  = [esi+0x174]            ; "current pending ASK slot" tracker
0x00529573:  al  = [esi+0x175]            ; "ASK pending bitmask" tracker
0x00529579:  edx = 1
0x0052957E:  shl edx, cl                  ; edx = 1 << ctx[+0x174]
0x00529580:  test al, dl                  ; ctx[+0x175] & (1 << ctx[+0x174])
0x00529582:  je   0x529622                ; if 0 -> answer-correlation path (NO render)
                                          ; if non-zero -> fall through to slot-busy + .alloc (render)
```

With both bytes zeroed in our phantom ctx, `al & dl = 0 & 1 = 0`, the `je` is taken, and we land at the answer-correlation path. That path checks `word [esi+0x204]`:

- `[esi+0x204] == 0` -> branch to `0x529683` which calls `sub_49FD50` (set current dialog slot), `sub_49FD70` (returns eax stored at `[esi+0x140]`), `sub_4A0660` (writes `pWindowsArray[slot]+0x1E = 0xFE00`, no SWO call), increments `[esi+0x204]` to 1, returns 1.
- `[esi+0x204] == 1` -> falls through to `0x529631` which is the **answer-received cleanup** (clears `gameObj.D2` bit, decrements SP by 6 via `dl + 0xfa`, returns 3).
- `[esi+0x204] >= 2` -> jumps to `0x5296ac` which returns 1 unchanged.

**`set_window_object_ASK` is ONLY called from the `.alloc` branch at `0x5295AB`** -- entered when `(ctx[+0x175] & (1 << ctx[+0x174])) != 0` AND `gameObj.D2 & (1 << slot) == 0`.

### The fix (~30 lines)

Before writing the script-stack args in `Phase2_TestAsk`, set:

```cpp
s_phantomCtx[0x174] = 0;  // shift count
s_phantomCtx[0x175] = 1;  // bit 0 set
```

Then `cl=0`, `al=1`, `edx=1<<0=1`, `test al, dl = 1 & 1 = 1` (non-zero), `je` is NOT taken, execution falls through. v0.15.5 PRE confirmed `gameObj.D2 = 0x00` for slot 2, so the slot-busy gate `je 0x5295ab` is taken to `.alloc`. `.alloc` calls `field_get_dialog_string(msg_table, msg_id=0)` to get the elevator dialog text pointer, calls `set_window_object_ASK(slot=2, text_ptr, arg2, arg3, arg4, arg5)` with our four script-stack values, sets `gameObj.D2 |= (1<<2) = 0x04` at `0x529613`, and returns 1.

The natural FF8 script-VM sets these tracking bytes via a preparatory opcode before `opcode_ask` runs in script flow. We don't run that preparatory opcode, so we set the bytes ourselves to mimic the post-prep state.

### Predicted v0.15.5.1 BAT outcome (replay Phase 2a in `doani1_2`)

- ret=1.
- Slot 2 trans advances 0 -> 0x400 -> 0x1000 (matching Phase 1 MES pattern).
- `gameObj.D2` acquires bit 2 (`0x04`) post-call.
- `slot+0x29` (firstQ) and `slot+0x2A` (lastQ) acquire values from our four script-stack args (1 / 3 / 2 / 2 in some order).
- Existing FieldDialog `[ASK]` hook fires with `win[2]` (not win[1] like the noise from Test #3) and SAPI speaks the elevator dialog with parsed choices.
- F11 screenshot post-fire confirms a visible dialog box.

If SUCCESS: empirical map of which arg lands in `slot+0x29/0x2A/0x2C` is nailed down for v0.15.6 Phase 2b (custom text + answer detection + chase wiring). Both outcomes leave that empirical map in the log -- the slot decode runs unconditionally after the call, so we learn something either way.

If still PARTIAL (slot populated but cursor doesn't track input): the engine's input handler needs additional state we haven't mirrored. Suspect: an entry in an ASK-pending list elsewhere (gameObj or script-VM globals), or a slot-state byte not in our model.

### Files changed

- `src/dialog_inject.cpp` (~30 lines): two ctx-byte writes after the `memset` in `Phase2_TestAsk`, plus a ~25-line comment block documenting the `0x52956D-0x529582` test mechanic and a new log line confirming the values were set. NO changes to `Phase1_TestMes`.
- `src/ff8_accessibility.h`: version bump to 0.15.5.1 with new comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.5.1 ready-to-BAT state.

No changes to `dialog_inject.h`, `dinput8.cpp`, `deploy.bat`, or any other file.

### Risk

Very low. The two ctx bytes are persistent script_context state which the natural FF8 script-VM writes anyway during ASK preparation; we're just preempting that write with values matching the post-prep state. The phantom ctx is our private buffer, not FF8's real script_context, so no engine state is being overwritten externally. Phase 1 is unchanged.

## v0.15.5

Phase 2a -- experimental `opcode_ask` call. v0.15.4 BAT was complete success; recipe is proven. v0.15.5 extends it from MES to ASK with the same dispatch-table-with-cached-fallback pattern, in preparation for v0.15.6 chase ASK wiring.

### What v0.15.4 proved (recap)

Aaron pressed F12 in field `doani1_2` (Dollet Comm Tower top). Every metric green:

- `opcode_mes` returned 3.
- Existing dialog hook fired and SAPI spoke: `Selphie "Wanna go up?" Go up Stay`.
- `pWindowsArray[1] + 0x1C` (open_close_transition) advanced 0 -> 0x400 (+15ms) -> 0x1000 (+125ms) and held.
- `+0x1E` velocity 0x200 armed by engine on entry.
- State machine 0 -> 1 -> 7.
- `gameObj.D3 = 0x02`, `D4 = 0x02` (bit 1 set for slot 1).
- `show_dialog` callback fired for slot 1 -- the per-slot callback registration v0.15.x worried about happens automatically through the opcode path.
- F11 screenshot at 17:36:00 confirmed: dialog visually rendered, indistinguishable from any natural in-game MES.

Useful incidental: dispatch table value (`0x649E57F0`) differed from cached value (`0x00528F20`) -- FFNx wraps these table entries. The defensive table-with-cached-fallback pattern in `dialog_inject.cpp` correctly chained through FFNx. v0.15.5 keeps this pattern for `opcode_ask`.

### What v0.15.5 ships

New function `DialogInject::Phase2_TestAsk` in `src/dialog_inject.cpp`. Bound to **Shift+F12**; Phase 1 (MES) stays on F12 alone per the F12 rule of one diagnostic per physical key state.

Phantom `script_context` with `SP=6` (vs Phase 1's `SP=2`). Stack layout determined by walking `opcode_ask`'s body at `0x00529520-0x005295D7`:

| Stack pos | Reg | Meaning | Set to |
|---|---|---|---|
| `stack[SP-5]` | `edi` | **slot index** (CONFIRMED via assertion at `0x52955A`) | **2** (avoid Phase 1's slot 1) |
| `stack[SP-4]` | `ecx` | **msg_id** (CONFIRMED via `field_get_dialog_string` call at `0x5295CD`) | **0** |
| `stack[SP-3]` | `edx` | `set_window_object_ASK` arg2 (clamp lower bound) | **1** |
| `stack[SP-2]` | `ecx` | SWO_ASK arg3 (clamp upper bound, written to `slot+0x29`) | **3** |
| `stack[SP-1]` | `ebp` | SWO_ASK arg4 (clamped value, written to `slot+0x2A`) | **2** |
| `stack[SP]` | `ebx` | SWO_ASK arg5 (aux byte, slot-indexed) | **2** |

Slot 2 chosen to avoid colliding with Phase 1's slot 1: the slot-busy gate at `0x529588-0x52959E` returns 5 if `gameObj+0xD2` bit `(1<<slot)` is already set.

In Aaron's BAT field `doani1_2`, msg 0 is the Selphie elevator ASK with two choice lines ("Go up" / "Stay"). Setting our cursor range to `[2, 3]` should land the cursor on those lines.

### Verification (mirrors Phase 1)

- SEH-wrap the `opcode_ask` call.
- Log return code (1 = wait, 5 = slot busy, 3 = advance, exception caught and reported).
- Pre/post snapshots of slot bytes (`+0x1C` trans, `+0x1E` vel, `+0x24` state, `+0x16` field16) and `gameObj.D2/D3/D4` masks.
- Post-fire decode of `slot+0x29` (firstQ), `slot+0x2A` (lastQ), `slot+0x2C` (curQ_2) to **empirically map which arg landed in which slot field** -- this turns the BAT into a concrete arg-to-meaning mapping for SWO_ASK.
- 3-second slot poll at 100ms cadence.
- SAPI announces "Dialog inject phase two A. Slot N. Return code X."

### What Phase 2a does NOT yet do

- **Custom text.** Uses the field's natural msg 0 because we haven't wired FF8 text encoding yet. `doani1_2`'s msg 0 happens to be a real ASK with choices, which is convenient for testing.
- **Answer commit detection.** `opcode_ask` returns 1 (wait for answer); the engine's input handler updates `slot+0x2B` (curQ) on arrows and clears state on Enter, but our injected call doesn't run the script-VM polling loop that reads the answer back from gameObj. Phase 2b (next ship) will add answer detection.
- **Chase ASK wiring.** `chase_ask_overlay::OpenAsk` still uses the v0.15.2.2 TTS+keyboard-only path. Phase 2b/v0.15.6 will swap its body to use `Phase2_OpenAsk(prompt, options[], default_idx)` with the strings "Manual / Auto / Original" (Aaron's preference).

### Three predicted BAT outcomes

- **SUCCESS**: ret=1, dialog renders with cursor on "Go up" / "Stay", arrows move cursor (engine input handler picks up our slot), state machine progresses, slot poll shows trans 0 -> 0x1000. Empirical SWO_ASK arg map nailed via `slot+0x29/0x2A/0x2C` decode. Phase 2b (custom text + answer detection) follows immediately on the same primitive.
- **PARTIAL**: ret=1 but cursor doesn't render or input doesn't track. Means engine's ASK input handling needs additional state we haven't mirrored (likely script-VM `ctx[+0x174/0x175]` ASK-pending bits or a per-frame tracking var). Diagnose by comparing the post-fire slot bytes against a captured natural-ASK snapshot.
- **FAIL/CRASH**: SEH catches an exception. The phantom context is missing fields `opcode_ask` reads beyond what `opcode_mes` needed. v0.15.5.1 expands the buffer / populates additional offsets based on the SEH-caught instruction pointer.

### Files changed

- `src/dialog_inject.h` (~30 lines): header rewrite documenting Phases 1 + 2a, new `Phase2_TestAsk` decl, Shift+F12 binding noted.
- `src/dialog_inject.cpp` (~210 lines added): Phase 2a constants block (`TEST_SP_ASK`, `TEST_SLOT_ASK`, etc., plus `OPCODE_ASK_INDEX`), `AnnouncePhase2Result` helper, full `Phase2_TestAsk` implementation with SEH-wrapped `opcode_ask` call and post-fire choice-field decode.
- `src/dinput8.cpp` (~10 lines): F12 handler now calls `Phase1_TestMes` by default and `Phase2_TestAsk` when shift held; comment block updates documenting the addition.
- `src/ff8_accessibility.h`: version bump to 0.15.5 with new comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.5 ready-to-BAT state.

### Risk

Low. The new function is gated behind an explicit Shift+F12 press in field mode. The SEH wrap catches malformed-context crashes. Slot 2 choice avoids the Phase 1 slot collision. Field-mode and address-resolved guards prevent calls when the engine isn't in a state to handle them. Phase 1 is unchanged (still bound to F12 alone) so the v0.15.4 capability is preserved.

## v0.15.4

Engine-rendered dialog injection -- Phase 1. New module `src/dialog_inject.{h,cpp}` synthesizes a phantom `script_context` and calls `opcode_mes(&ctx)` directly to prove the recipe for mod-driven engine dialog rendering. F12 fires a one-shot test. If a dialog box renders, Phase 2 (chase ASK via `opcode_ask`) is mechanical -- it follows the same pattern with a different opcode dispatch index and arg layout.

### Why this approach (the v0.15.x bitmask recipe was wrong)

v0.15.0 through v0.15.2.1 attempted to populate `ff8_win_obj` slot 1 with byte-perfect contents derived from a captured engine ASK snapshot. Every iteration ended with the engine ignoring the populated slot. The follow-up section of `Plan & Research Documents/Field dialog system disassembly analysis.md` identifies why: `show_dialog` is registered as a per-slot callback via `sub_4B6210/sub_4B6230` inside `sub_4A0880` (window-system init) at engine startup, and externally-populated slots are never part of that registry. The `gameObj+0xD2/0xD3/0xD4` bitmasks v0.15.x targeted are per-slot allocation flags (used by script-VM opcodes to refuse double-allocation), NOT the render trigger.

`Plan & Research Documents/ASK render binding deep research results.md` recommends Path A: synthesize a fake `script_context` and call `opcode_mes(&ctx)` / `opcode_ask(&ctx)` directly. Path A reuses the engine's full setup path verbatim, including the per-slot callback registration that triggers actual rendering.

### Phase 1 implementation

- Phantom 0x300-byte zero-init script_context buffer (sized to cover ASK's `[+0x204]` write with margin).
- `ctx[0x184] = 2` -- script-VM SP byte.
- `ctx[8] = 0` -- msg_id arg (every field has msg 0).
- `ctx[4] = 1` -- slot index arg (slot 1 leaves slot 0 free for the engine's main MES).
- Resolve `opcode_mes` from the dispatch table at fire time (with cached fallback).
- SEH-wrap the call. `__cdecl`, single arg = phantom ctx pointer.

### Verification (all automated for blind dev)

- Log `opcode_mes` return code (3 = advance/success, 5 = wait/slot-busy; exceptions caught and reported).
- The existing v0.04.36 dialog hook fires on `opcode_mes` entry, so Aaron hears the dialog text via SAPI as the "call entered" signal.
- Per-frame slot poll for 3 seconds at 100 ms cadence logging `pWindowsArray[1]+0x1C` (open_close_transition), `+0x1E` (velocity), `+0x24` (state), `+0x16` (field16), and `gameObj.D2/D3/D4` bitmasks. If `+0x1C` advances from 0 toward 0x1000, the open-transition is animating and the render path is alive.
- SAPI announces "Dialog inject phase one. Slot N. Return code X." so Aaron knows the result without checking logs.
- Pre/post slot snapshots and gameObj bitmask snapshots are logged around the call for diff visibility.

### Safeguards

- Field-mode guard (`IsOnField()`) before fire -- field opcodes are only valid in MODE_FIELD.
- Address-resolved guards before fire -- abort with TTS message if `opcode_mes` or `pWindowsArray` is unresolved.
- SEH wrap on the opcode call to catch crashes from a malformed phantom context (which would indicate the script_context layout needs more fields populated than v0.15.4's minimal set).
- Static phantom buffer persists for the dialog's lifetime (engine retains the pointer indirectly through `sub_49FD50` + window state).

### F12 hotkey rebinding

Replaces v0.15.0's `ChaseDiag::Toggle` binding. Per the F12 rule (only one diagnostic active on F12 at a time), the chase diagnostic is retired -- the chase chapter shipped end-to-end as v0.15.3. The `ChaseDiag` module remains in source and continues to poll if previously enabled, but cannot be toggled at runtime now. If a future session needs chase-diag, it can be re-bound.

### Predicted outcomes

Three branches the BAT could land on:

- **SUCCESS**: opcode_mes returns 3, the SAPI hook speaks msg 0 of the current field, the slot poll shows `+0x1C` advancing 0 -> 0x200 -> 0x400 -> ... -> 0x1000. Phase 2 (chase ASK) follows immediately on the same primitive.
- **PARTIAL**: opcode_mes returns 3 but the slot poll shows `+0x1C` stuck at 0. Means the call entered but the engine's open transition didn't kick. Investigate: check `set_window_object` ran (slot text pointers populated post-call), check `sub_4A0620` ran (slot velocity at `+0x1E` == 0x200), and trace the difference vs a captured natural-MES snapshot.
- **FAIL/CRASH**: SEH catches an exception. The phantom context is missing fields the opcode reads. Expand the buffer or populate additional offsets based on the SEH-caught instruction pointer (logged for debugging).

### Files changed

- `src/dialog_inject.h` (NEW, ~80 lines): design rationale documenting Path A and why the bitmask recipe was outdated; public API.
- `src/dialog_inject.cpp` (NEW, ~280 lines): phantom ctx buffer, slot snapshot helpers, gameObj mask read helpers, `Phase1_TestMes` with pre/post snapshots and SEH-wrapped call, `Update` slot poll.
- `src/dinput8.cpp` (~10 lines): include `dialog_inject.h`, `DialogInject::Initialize` after `ChaseBattleFreeze::Initialize`, `DialogInject::Update` in main loop after `ChaseDiag::Update`, `DialogInject::Shutdown` before `ChaseBattleFreeze::Shutdown`, F12 handler swap from `ChaseDiag::Toggle` to `DialogInject::Phase1_TestMes`, comment block updates documenting the swap.
- `src/deploy.bat` (1 line): `dialog_inject.cpp` added to compile list after `chase_battle_freeze.cpp`.
- `src/ff8_accessibility.h`: version bump to 0.15.4 with new comment trail entry.
- `CHANGELOG.md`: this top entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: refreshed for v0.15.4 ready-to-BAT state.

### Risk

Low-to-moderate. The new module is gated behind an explicit F12 press in field mode; nothing fires automatically. The opcode call is SEH-wrapped so a malformed phantom context cannot crash the game. The F12 binding swap removes the chase-diag toggle, but chase-diag is no longer a needed feature path post-v0.15.3 milestone. The only behavioral change at engine level is what happens when F12 is pressed -- if the recipe works, a dialog opens; if it doesn't, the call is harmless and only logs.

## v0.15.3

Single-pronged cleanup: remove the static kani+battleyarou pin from chase_kani_freeze; fix the deploy.bat "Version: World" regex bug.

The v0.15.2.15 BAT was a milestone success. Aaron played through the entire X-ATM092 chase end-to-end -- five mountain-trail fields, the bridge (doopen2a), Town Square (dotown_3), and out the chase-end Lapin Beach FMV with all eight audio descriptions playing cleanly across the 74-second cutscene. One battle per field, no crashes, no hangs, no robot-walking-around. Every chase field's CHASE-AGENT FINAL SUMMARY showed the dynamic agent pin holding the actual robot at zero or near-zero changed bytes. The doopen2a strcmp guard worked exactly as designed (one PASS log line, no [CHASE-AGENT] line, fieldId-flip deactivation fired correctly on the doopen2a -> dotown_3 handoff). The DEVNOTES decision criterion was satisfied: "if [CHASE-AGENT] resolves AND CHASE-AGENT FINAL SUMMARY shows few/zero byte changes AND freeze# count is low for that field, the agent pin is sufficient."

### What changes in v0.15.3

The static kani+battleyarou pin in `src/chase_kani_freeze.cpp` is removed. Across three v0.15.2.x BATs, the OTHERS-DIAG scanner consistently showed kani had at most 7 changed bytes and battleyarou had 0 -- both static pins were inert in every chase field tested, because the actual chase agents in those fields were rinoa-slot in domt5_1, director0 in doopen2a, and various robot-slots in the trail fields, NOT the kani or battleyarou symbols the static pin was targeting. They were dead code.

What goes:

- The `s_kaniPtr` / `s_strideBytes` / `s_arrayKind` / `s_haveFullSnapshot` / `s_fullSnapshot` / `s_initial` / `s_prev` / `s_byteFirstChangeLogged` state.
- The `s_battleyarouPtr` / `s_battleyarouStrideBytes` / `s_battleyarouArrayKind` / `s_battleyarouInitial` / `s_haveBattleyarouSnapshot` / `s_battleyarouSnapshot` state.
- `ReadKaniBlock`, `LogInitialSnapshot`, `LogChangeSummary`, `DiffAndLogFirstChanges` helpers.
- The kani INITIAL / snapshot / memcpy / FINAL blocks in `StartCapture`, `ApplyFreezePin`, `EndCapture`.
- The battleyarou INITIAL / snapshot / memcpy / FINAL blocks in the same three functions.
- The per-tick FIRST CHANGE diff loop and the MID-WINDOW heartbeat in `Update` (both anchored to the kani buffer).
- The kani-related cleanup lines in `DeactivateFreeze`.

What stays:

- The dynamic chase-agent pin (`RegisterChaseAgent` + agent INITIAL / snapshot / memcpy / FINAL SUMMARY blocks).
- The v0.15.2.14 fieldId-flip deactivation (`ReadCurrentFieldId` helper, `s_freezeFieldId` capture, raw-fieldId check before debounced-name check).
- The v0.15.2.9 OTHERS-DIAG diagnostic scanner (kept for future agent-resolution audits).
- The v0.15.2.3.1 capture trigger (`s_battleSeenRecently` mode 3->1 detection).
- The SEH probe pattern before agent writes.
- The `LogHexRow` helper (used for the AGENT-INIT log block).

### Net effect

`chase_kani_freeze.cpp` goes from ~700 lines to ~580 lines. The field log gets much quieter during chase battles -- no per-tick FIRST CHANGE spam, no MID-WINDOW heartbeat, no kani / battleyarou INITIAL or FINAL SUMMARY blocks. The CHASE-AGENT lines and the OTHERS-DIAG block remain. The per-frame cost in chase fields drops by two memcpys plus one ReadKaniBlock per Update tick.

`chase_kani_freeze.h`'s design comment is rewritten to document the v0.15.3 single-pronged design, with the v0.15.2.x history retained as a terse trail. The Initialize log line is updated to "v0.15.3 DYNAMIC AGENT PIN ONLY" wording.

### Cosmetic fix bundled in: deploy.bat "Version: World" regex bug

The v0.15.2.x deploy log lines all printed `Version: World` instead of the actual version string. Root cause: the `findstr /C:"FF8OPC_VERSION "` pattern in `src/deploy.bat` matched comment-trail lines in `ff8_accessibility.h` in addition to the actual `#define`. The `for /f` loop's last-iteration-wins behavior left `VERSION` set to token 3 of an unrelated comment line.

Fix: tighten the findstr to `/C:"#define FF8OPC_VERSION "` so only the actual `#define` line matches. Drop the now-redundant `^| findstr /V "DATE"` filter. The `%%~V` modifier strips surrounding quotes from `"0.15.3"` to give `VERSION=0.15.3` in the deploy log.

### Risk

Very low. The removed code paths only ran during chase-field battle exits, never wrote to entities outside their resolved kani/battleyarou pointers, and v0.15.2.15 BAT's OTHERS-DIAG already proved kani and battleyarou were inert -- removing inert pins changes nothing the engine observes.

### Predicted v0.15.3 BAT outcome

Identical chase behavior to v0.15.2.15: single-fight chase, robots stay down, dotown_3 transition succeeds, Lapin Beach FMV plays through to dotown_2. Field log is shorter and cleaner. Deploy log shows `Version: 0.15.3` instead of `Version: World`.

### Files changed

- `src/chase_kani_freeze.cpp` (rewritten, ~580 lines down from ~700)
- `src/chase_kani_freeze.h` (design comment rewrite)
- `src/ff8_accessibility.h` (version bump to 0.15.3 + new comment trail entry)
- `src/deploy.bat` (1 line: tighten findstr to `#define`-prefixed)
- `CHANGELOG.md` (this top entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.15

Surgical doopen2a fix on top of v0.15.2.14 -- skip dynamic chase-agent pin in doopen2a only

The v0.15.2.14 BAT confirmed that the new dynamic chase-agent pin works beautifully across the five mountain-trail chase fields (domt1_1 through domt5_1). Auto-announce field name fired correctly on every load. The robots stayed pinned. No crashes.

The failure was in doopen2a, the bridge field between the trail and dotown_3 (fieldId=0x014D; Aaron called it "town square" in his report, but the freeze actually happened one field upstream of dotown_3). After the chase battle in doopen2a resolved, the field froze before any transition. Music kept playing, no crash, but no dotown_3 entry was ever logged.

### Diagnosis

The v0.15.2.14 BAT log gives the answer. In doopen2a, the dynamic agent pin registered the BATTLE caller as `director0` (Others slot 4, symIdx 9) -- and the CHASE-AGENT FINAL SUMMARY showed 41 changed bytes in the t=0..1500ms grace period before our snapshot, scattered across the animation/position regions (+0x140-+0x147 X/Y, +0x190-+0x197, +0x1B5-+0x1BA, +0x1F6-+0x1FA, +0x206-+0x207). After t=1500ms, the pin held director0 frozen for the rest of the field session.

Cross-referencing with the v0.15.2.13 BAT: the BATTLE caller in doopen2a was logged at entityPtr=0x0188CA04, which v0.15.2.14 correctly resolved to director0. So in doopen2a the BATTLE caller and the chase-progress-tracker are the same entity. Pinning its full state for the rest of the field session prevents the chase-end script from advancing -- director0 presumably waits on a flag/counter byte at one of the offsets we're now overwriting every frame, so the transition to dotown_3 never fires.

The v0.15.2.10 deferred concern about director0 was prescient. domt1_1 through domt5_1 don't share this pattern: their BATTLE caller is the actual robot (the chase agent), not the field's progress director, so pinning it works.

### Fix

`src/chase_battle_freeze.cpp` Hook_opcode_battle PASS branch now captures the debounced field name and wraps the RegisterChaseAgent call in a strcmp:

```cpp
const char* fieldName = ChaseDetector::GetDebouncedFieldName();
if (fieldName != nullptr && std::strcmp(fieldName, "doopen2a") == 0) {
    Log::Field("[CBF] PASS in doopen2a -- skipping RegisterChaseAgent ...");
} else {
    ChaseKaniFreeze::RegisterChaseAgent((uintptr_t)entityPtr);
}
```

The BATTLE NO-OP gate (battleCount >= 1) is unchanged and carries the load in doopen2a -- that field has exactly one chase battle in the whole sequence, so capping at 1 is sufficient. Other chase fields keep the dynamic pin.

The static kani+battleyarou pin in chase_kani_freeze still runs in doopen2a, but it's inert there: per the v0.15.2.14 OTHERS-DIAG, kani had 7 changed bytes and battleyarou had 0, so the pin had nothing to hold.

Also added `<cstring>` include for `std::strcmp` and updated the Initialize log line to v0.15.2.15 wording ("skip register-agent in doopen2a only"). v0.15.2.14 dynamic agent pin design, tightened deactivation (raw fieldId check + SEH probe), and field_announce module are all UNCHANGED.

### What v0.15.2.15 BAT should show

- Each chase field except doopen2a: one `[CBF] PASS` line plus one `[CHASE-AGENT]` line (same as v0.15.2.14)
- doopen2a: one `[CBF] PASS in doopen2a -- skipping RegisterChaseAgent` line, NO `[CHASE-AGENT]` line for that field, no CHASE-AGENT FINAL SUMMARY block
- doopen2a kani+battleyarou FINAL SUMMARY shows changed_bytes=0 (those entities still dormant)
- A `KaniFreeze: FREEZE DEACTIVATED -- fieldId changed 0x014D -> 0x0158 (pre-debounce)` line shortly after the doopen2a battle ends
- dotown_3 cutscene plays through, chase-end FMV fires, control returns to dotown_2 or wherever the chase ends

If the freeze recurs even with the dynamic pin disabled in doopen2a, the cause is something else and we'll need a different angle (next candidates: was the static kani+battleyarou pin in earlier doopen2a entries causing trouble? does the BATTLE NO-OP itself break the chase-end script in doopen2a?).

### Files changed

- `src/chase_battle_freeze.cpp` (~30 lines: capture fieldName, wrap RegisterChaseAgent in strcmp guard, add `<cstring>` include, update Initialize log line)
- `src/chase_battle_freeze.h` (~20 lines: new design rationale paragraph at top documenting v0.15.2.15 doopen2a skip)
- `src/ff8_accessibility.h` (version bump to 0.15.2.15 + new comment trail entry)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.14

Dynamic chase-agent pin + tightened deactivation (dotown_3 crash fix) + auto-announce field name on field load

v0.15.2.13 BAT confirmed end-to-end traversal of the X-ATM092 chase but exposed two issues that this build addresses, plus an unrelated user-requested feature.

### Issue 1: dotown_3 crash recurred

v0.15.2.13 BAT crashed approximately 16 seconds after entering dotown_3 from doopen2a, the same pattern as the v0.15.2.10 BAT crash. v0.15.2.11's removal of dotown_3 from CHASE_FIELD_NAMES had stopped chase_kani_freeze from starting a new capture inside dotown_3, but it didn't fix the handoff. The actual mechanism: when fieldId flips from doopen2a (0x014D) to dotown_3 (0x0158), there is a roughly 2-second window before ChaseDetector's name-debounce settles on the new name. The v0.15.2.4 ApplyFreezePin design explicitly preserved the cached pointer through that window ("empty-string field name during the 2s name-debounce after fieldId flip does NOT deactivate -- we don't yet know the destination"). During those 2 seconds, ApplyFreezePin kept writing 0x21 to bytes at +0x150/+0x23F/+0x241 and 0x14 to +0x154/+0x1FA on the cached doopen2a kani pointer, plus a 292-byte memcpy of the full-state snapshot, on a memory region that had been freed and reallocated to dotown_3 entities. The cutscene crashed when it read the corrupted state.

v0.15.2.11 was pushed to GitHub but never demonstrably proven crash-free -- the v0.15.2.11 BAT didn't actually replay through dotown_3, v0.15.2.12 got stuck in domt5_1, and v0.15.2.13 was the first build since v0.15.2.10 to reach dotown_3.

### Issue 2: Pin was hitting the wrong entities

v0.15.2.13's BATTLE NO-OP suppressed combat correctly (6 PASS + 8 NO-OP across 14 chase BATTLE calls in 6 fields, exactly one PASS per field). But the actual chase agent in domt5_1 -- the rinoa-slot wearing kani's robot model (Others slot 3, model 12, 47 changed bytes in the v0.15.2.12 OTHERS-DIAG) -- was waking up and walking around silently while the BATTLE NO-OP suppressed combat. The kani+battleyarou pin from v0.15.2.7-.8 was holding two dormant entities perfectly still while the actual robot wandered loose. Aaron's design preference inverted: pin should keep the agent down on the ground (so it stays incapacitated visually too); BATTLE NO-OP becomes the safety net for cases where the pin misses.

### Fix 1: Dynamic chase-agent pin

`src/chase_kani_freeze.h` exposes a new `RegisterChaseAgent(uintptr_t entityPtr)` entry point. `src/chase_battle_freeze.cpp` calls it from the PASS branch of Hook_opcode_battle, handing over the entity pointer that just made the BATTLE call. `RegisterChaseAgent` resolves the pointer to (arrayKind, slot, symIdx, symName) by reading `pFieldStateOthers` and `pFieldStateBackgrounds` bases under SEH, walking with the appropriate stride (0x264 / 0x1B4), and checking offset modulo + slot range. On success, logs a structured line for per-field identity audit:

```
[CHASE-AGENT] field='domt5_1' entityPtr=0x0188C5D8
              -> array=Others slot=3 symIdx=7 sym='rinoa'
              stride=0x264 header[0x00..0x10]: <16 hex bytes>
```

On failure (pointer outside both arrays, JSMCounts unavailable, etc.), logs `[CHASE-AGENT-UNRESOLVED]` with the reason -- pin stays inactive, BATTLE NO-OP carries the load.

The new agent state runs alongside kani+battleyarou. StartCapture snapshots the agent's INITIAL state (logged as `AGENT-INIT` hex dump). ApplyFreezePin takes the agent's full-state snapshot at SNAPSHOT_DELAY_MS=1500ms post-activation, then memcpy's it back over the agent's +0x140..stride region every frame. EndCapture logs CHASE-AGENT FINAL SUMMARY with changed_bytes count and per-byte deltas vs INITIAL.

Thread safety: RegisterChaseAgent runs on the game thread, ApplyFreezePin on the mod thread. Identity fields (stride, kind, slot, symName) are written first; s_chaseAgentPtr is written last (32-bit aligned, atomic on x86, store-store reordering forbidden), so the mod thread sees either uninitialized or fully populated state.

### Fix 2: Tightened deactivation

`src/chase_kani_freeze.cpp` ApplyFreezePin captures `pCurrentFieldId` at FREEZE ACTIVATED time (new state s_freezeFieldId) and reads it under SEH every frame. If the live fieldId differs from the captured value, it calls a new centralized `DeactivateFreeze(reason)` helper that clears all pin state (kani + battleyarou + agent + snapshot flags + freeze ticks) immediately -- before the 2-second name-debounce settles. The existing debounced-name check stays as a backup. Plus a one-byte SEH-guarded probe read before each entity write to catch torn pointers; on fault, skip the frame silently.

This fixes the dotown_3 crash without dotown_3 needing any special-case logic. The fieldId check fires the moment the engine flips it, before any reallocation can cross-contaminate.

### Fix 3: BATTLE NO-OP becomes the safety net

`src/chase_battle_freeze.h` and `.cpp` are repurposed from primary suppression (v0.15.2.13) to safety-net + agent identifier. The PASS branch now calls RegisterChaseAgent(entityPtr) as a side effect of the existing log line. The NO-OP gate (battleCount >= 1) is preserved unchanged as the fallback. In a healthy v0.15.2.14 run, the pin holds the agent on the ground and the NO-OP fires zero or rarely; the freeze# counter from the Shutdown summary becomes a real diagnostic (low = pin healthy, high = pin missing the agent and safety net carrying the load).

### Feature: auto-announce field name on field load

New module `src/field_announce.{h,cpp}` (~140 lines total). Polls `FF8Addresses::pCurrentFieldId` every Update tick under SEH; on fieldId change, starts an 800ms debounce timer; once the new fieldId is stable, looks up `FIELD_DISPLAY_NAMES[fieldId]` (the existing 982-entry catalog from `src/field_display_names.h`) and calls `ScreenReader::Speak(name, false)` -- queued, not interrupting, so dialog or in-flight TTS finishes first. Skip rules: fieldId == 0 (title screen), fieldId out of range, already-announced (no spam on battle/menu re-entry to the same field), not in MODE_FIELD. Wired into dinput8.cpp's main loop next to FieldNavigation::Update.

### Files changed

- `src/chase_kani_freeze.h` (rewritten ~70 lines)
- `src/chase_kani_freeze.cpp` (rewritten ~700 lines)
- `src/chase_battle_freeze.h` (rewritten ~50 lines)
- `src/chase_battle_freeze.cpp` (rewritten ~135 lines: include chase_kani_freeze.h, RegisterChaseAgent call in PASS branch)
- `src/field_announce.h` + `.cpp` (NEW)
- `src/dinput8.cpp` (~20 lines: include field_announce.h, FieldAnnounce::Initialize/Update/Shutdown wired in, ChaseKaniFreeze + ChaseBattleFreeze comment blocks rewritten)
- `src/deploy.bat` (1 line: field_announce.cpp added to compile list)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### What v0.15.2.14 BAT should show

For each chase field, exactly one `[CBF] PASS` line followed by exactly one `[CHASE-AGENT]` line resolving the BATTLE caller's entityPtr to a slot identity. Six fields total = six pairs. After the post-battle StartCapture in each field, a `KaniFreeze: CHASE-AGENT INITIAL snapshot` line and a `KaniFreeze: CHASE-AGENT full-state snapshot taken` line about 1500ms later. Subsequent collisions in the same field should NOT generate `[CBF] NO-OP` lines if the pin is working (the agent stays on the ground and never reaches BATTLE again); a low freeze# count at Shutdown indicates a healthy pin. CHASE-AGENT FINAL SUMMARY changed_bytes should be near zero per field.

On the doopen2a -> dotown_3 transition, expect a single `KaniFreeze: FREEZE DEACTIVATED -- fieldId changed 0x014D -> 0x0158 (pre-debounce)` line, then no further pin activity, no crash, dotown_3 cutscene plays through to credits.

Aaron should also hear field display names auto-spoken on every field load.

## v0.15.2.13

Flip `chase_battle_freeze` from passive observer to active BATTLE NO-OP based on v0.15.2.12 BAT data

The v0.15.2.12 BAT in `domt5_1` produced three `[CBF]` log lines, one
per chase battle, all paired one-to-one with `ChaseDetector battle
entered` events:

```
11:51:32  [CBF] chase BATTLE call #2 (total #3)  battleCount=0
          caller=other  entityPtr=0x0188C5D8
11:53:21  [CBF] chase BATTLE call #3 (total #4)  battleCount=1
          caller=other  entityPtr=0x0188C5D8
11:54:47  [CBF] chase BATTLE call #4 (total #5)  battleCount=2
          caller=other  entityPtr=0x0188C5D8
```

`opcode_battle` fires for every chase battle in `domt5_1`. The
v0.15.2.2 finding that opcode_battle was dead in `domt5_1` was wrong:
v0.15.1's pass-through logger sampled at every-50th-call frequency and
the freeze branch only logged kani-driven calls, so non-kani chase
BATTLE calls were essentially invisible. v0.15.2.12's per-call
chase-field logging caught what earlier builds missed.

### What entityPtr 0x0188C5D8 actually is

`othersBase(0x0188BEAC) + 3 * 0x264 = Others slot 3 = symIdx 7 =
'rinoa' SYM entry`. Same-window OTHERS-DIAG showed rinoa-slot had
`changed_bytes=47/612` (second-most-active entity in `domt5_1`); the
immediately-prior `[TALKRAD]` line for the same pointer showed
`model=12`, kani's robot model. The rinoa-slot is the actual chase
agent in `domt5_1`, wearing model 12. Not the kani-slot (slot 8, only
the 5 pinned bytes changed). Not the battleyarou-slot (slot 10, 0
changes).

v0.15.2.8/9/10 had been pinning the wrong two entities the whole time
in `domt5_1`. The pin worked perfectly but missed the chase agent.

### What v0.15.2.13 ships

A simple BATTLE-opcode NO-OP that doesn't depend on knowing which
entity is the chase agent. `chase_battle_freeze.cpp` reactivates the
freeze branch with gate:

```cpp
freeze = (mode == MODE_MANUAL && IsInChaseField()
          && GetCurrentFieldBattleCount() >= 1);
```

Returns `JSM_RC_ADVANCE = 3` without invoking the original handler
when the gate matches. **The caller-identity check (kani / battleyarou)
is dropped from the gate** -- it's still computed for log tagging only.
The first chase battle per field still passes through so the scripted
opening encounter fires; only the second-and-later calls are NO-OP'd.

Same mechanism v0.15.1 used successfully in `domt4_1`, just with a
broader gate.

### Implementation

**`src/chase_battle_freeze.h` (rewritten):**
- Documents the v0.15.2.13 design rationale and includes the v0.15.2.12
  BAT findings inline so future maintainers don't repeat the v0.15.2.2
  misinterpretation.
- Public API (`Initialize`, `Shutdown`) unchanged.

**`src/chase_battle_freeze.cpp` (rewritten):**
- `Hook_opcode_battle` re-enables the freeze branch with the
  caller-agnostic gate.
- New `s_freezeCount` global counts NO-OP'd calls for the Shutdown
  summary.
- Per-call log lines tagged `NO-OP` or `PASS` so the BAT can verify the
  freeze pattern at a glance.
- `Initialize` log line documents the active-freeze role.

**`src/dinput8.cpp`:**
- `ChaseBattleFreeze::Initialize` / `Shutdown` comment blocks updated to
  reflect ACTIVE FREEZE role.

### Predicted v0.15.2.13 BAT outcome

Enter `domt5_1`, fight battle #1 normally (`PASS` log line), walk
toward field exit, robot collides with Squall, opcode_battle fires but
is NO-OP'd (`NO-OP` log line). Squall continues walking, exits to next
chase field, pattern repeats. End result: traverse the entire chase
scene through the Lapin Beach FMV.

### Risk

Very low.

1. No entity bytes touched -- v0.15.2.11's `dotown_3` cutscene fix
   stays intact.
2. Returning JSM advance code without calling original is exactly what
   v0.15.1 did in `domt4_1` -- and that worked.
3. Aaron confirmed there are no random encounters during the chase
   scene, so the "cap at 1 battle per chase field" policy doesn't
   suppress legitimate non-chase battles.
4. The v0.15.2.12 BAT showed Aaron stuck in `domt5_1` with three
   battles and no forward progress -- this build directly addresses
   that failure mode.

### What this also resolves

The `doopen2a` second-chase-battle issue (deferred from v0.15.2.10 over
concerns about a `director0` pin breaking the chase-end logic) is
**solved by this fix without ever needing to pin `director0`**.
v0.15.2.13 caps `doopen2a` at one battle the same way it caps every
other chase field.

### What this does NOT change

- v0.15.2.11 dotown removal preserved.
- v0.15.2.10 `domt1_1` chase coverage preserved.
- v0.15.2.9 OTHERS-DIAG scanner preserved.
- v0.15.2.8 dual-entity (kani + battleyarou) pin preserved -- now
  defensive belt-and-suspenders, harmless when those entities aren't
  the chase agent and may still help in fields where kani-slot IS the
  agent (e.g. `domt4_1`).
- All earlier kani-pin layers preserved.

### Files changed

- `src/chase_battle_freeze.h` (rewritten)
- `src/chase_battle_freeze.cpp` (rewritten)
- `src/dinput8.cpp` (init / shutdown comment blocks)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.12

Reactivate `chase_battle_freeze.cpp` as a passive opcode_battle observer (no behavior change, log only)

v0.15.2.10 BAT confirmed `doopen2a` fires a second chase battle even
with kani + battleyarou pinned. The clean OTHERS-DIAG at 22:03:34
flagged `director0` (31 changes / 612 bytes) as the prime non-pinned
suspect, but pinning a third entity carries real risk -- if `director0`
is the chase-progress-tracker, freezing it could re-break the chase-end
cutscene that v0.15.2.11 just unblocked, or stall progression in some
other way we can't predict from byte-change counts alone.

Before committing to a `director0` pin we want one piece of empirical
data: does the second-battle event in `doopen2a` go through
`opcode_battle` at all? If yes, the cleanest fix is a simple BATTLE
NO-OP hook (no entity bytes touched, no script-state corruption). If
no, we know the second battle takes a different code path -- the same
dead-code situation v0.15.2.2 BAT documented for `domt2_1` and
`domt5_1` -- and we move to entity-level work knowing the cheaper
strategy was never available.

### What v0.15.2.12 ships

**Reactivates the orphan `chase_battle_freeze.{h,cpp}` as a pure
passive observer.** The hook is installed on
`pExecuteOpcodeTable[0x69]` and ALWAYS forwards to the original
handler. No freeze, no NO-OP, no short-circuit. This is observation
only.

When `ChaseDetector::IsInChaseField()` is true, every BATTLE call
emits one log line:

```
[CBF] chase BATTLE call #N (total #M) field='X' mode=Y battleCount=Z
      caller=kani|battleyarou|other entityPtr=0xADDR
```

The `caller` tag uses `ChaseDetector::IsKaniEntityPtr` and
`IsBattleyarouEntityPtr` to identify whether the calling script entity
is one of the two we already track. For other callers, the raw
`entityPtr` value lets the BAT analyst correlate against the entity
addresses captured in OTHERS-DIAG.

Outside chase fields the hook is silent (no log spam from random
encounters or non-chase scripted battles).

### Three predicted BAT outcomes

**(A) FIRES-BOTH** -- two `[CBF]` lines in `doopen2a` (battleCount=0
for the first battle, battleCount=1 for the second). Strategy 1
(NO-OP the second+ call) is viable; v0.15.2.13 ships the active
freeze.

**(B) FIRES-FIRST-ONLY** -- one `[CBF]` line for the first battle,
none for the second that `ChaseDetector` still observes via
game-mode 1 -> 3 transition. Same dead-code pattern as `domt2_1`
and `domt5_1`. Move to Strategy 2 (targeted byte pin on the specific
`director0` offsets that change) or Strategy 3 (full `director0`
freeze, riskiest).

**(C) FIRES-NEITHER** -- no `[CBF]` lines at all but `ChaseDetector`
still reports battles. Confirms `opcode_battle` is fully dead in
`doopen2a`. Same conclusion as B.

### Implementation

**`src/chase_battle_freeze.h` (rewritten ~30 lines):**
- Updated header comment block to describe the v0.15.2.12 passive
  observer role and the three predicted outcomes.
- Public API (`Initialize`, `Shutdown`) unchanged.

**`src/chase_battle_freeze.cpp` (rewritten ~140 lines):**
- Removed the freeze branch entirely. `Hook_opcode_battle` now
  computes chase-field state purely for logging and unconditionally
  forwards to `s_origBattle(entityPtr)`.
- Per-call logging inside chase fields includes both kani and
  battleyarou caller-pointer matches via the existing
  `ChaseDetector::Is*EntityPtr` helpers (battleyarou support added
  in v0.15.2.8 was not present in v0.15.1).
- Removed periodic pass-through summary lines (every-50th sampling
  no longer needed -- we want EVERY chase-field call).
- `Initialize` log line updated to v0.15.2.12 wording.
- `Shutdown` log line now reports both total opcode_battle calls and
  the chase-field subset.

**`src/deploy.bat` (1 line):**
- `chase_battle_freeze.cpp` added back to the cl.exe compile list,
  immediately after `chase_kani_freeze.cpp`.

**`src/dinput8.cpp` (~12 lines):**
- `#include "chase_battle_freeze.h"` added.
- `ChaseBattleFreeze::Initialize()` called after
  `ChaseKaniFreeze::Initialize()`. ChaseDetector is already
  initialized earlier in the chain so its kani / battleyarou queries
  return valid data when the hook fires.
- `ChaseBattleFreeze::Shutdown()` called before
  `ChaseKaniFreeze::Shutdown()` (reverse order).

### What this does NOT change

- v0.15.2.11 dotown removal preserved.
- v0.15.2.10 `domt1_1` chase coverage preserved.
- v0.15.2.9 OTHERS-DIAG scanner preserved.
- v0.15.2.8 dual-entity (kani + battleyarou) pin preserved.
- All earlier kani-pin layers preserved.
- `ChaseAskOverlay`, `ChaseDiag`, `ChaseDetector` unchanged.

### Risk

Zero. The hook does not modify engine behavior in any case. The only
output is log lines. If for some reason the hook fails to install
(MinHook error, address resolution failure), the build still runs
identically to v0.15.2.11 -- the failure path logs an error and
leaves the engine untouched.

### v0.15.2.12 BAT plan

Drive Squall through the chase scene focusing on `doopen2a`. Whatever
outcome we observe (A, B, or C above) directly determines v0.15.2.13's
strategy.

### Files changed

- `src/chase_battle_freeze.h` (rewritten)
- `src/chase_battle_freeze.cpp` (rewritten)
- `src/deploy.bat` (compile list)
- `src/dinput8.cpp` (init / shutdown wiring)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.11

Remove `dotown_3`/`dotown_2`/`dotown_1` from `CHASE_FIELD_NAMES[]`

v0.15.2.10 BAT crashed/hung ~16 seconds after entering `dotown_3` from
`doopen2a` (Fri 2026-05-08 22:05:13–22:05:31). Aaron's diagnosis:
`dotown_3`'s chase-end cutscene plays an animation where X-ATM092
walks across the town square and shorts out, driven by the `dotown_3`
kani entity in `Backgrounds` slot 1. With `dotown_3` in
`CHASE_FIELD_NAMES`, our `chase_kani_freeze` module kept tracking
`dotown_3`'s kani address and — if a mode 4→1 transition fired during
the cutscene — would `StartCapture` and pin the kani's anim ID bytes
(`+0x150`/`+0x154`/`+0x1FA`/`+0x23F`/`+0x241`), directly fighting the
cutscene's animation script every frame. v0.15.2.9 BAT didn't crash on
this transition because timing happened to skip the `StartCapture`
trigger; v0.15.2.10 got unlucky.

### Implementation

**`chase_detector.cpp` (~5 lines deleted, ~20 lines of comment):**
- Removed `"dotown_3"`, `"dotown_2"`, `"dotown_1"` from
  `CHASE_FIELD_NAMES[]`.
- These are post-chase town fields where the chase-end cutscene plays.
  No kani battles fire there; the chase is over.
- Comment block extended explaining the v0.15.2.10 crash and the
  rationale for removing all three fields together.

### Behavioral change

- `ChaseDetector::IsInChaseField()` returns `false` on entry to
  `dotown_3` instead of returning `true` until reaching Lapin Beach.
- `chase_kani_freeze::StartCapture` won't fire in `dotown_3`/`dotown_2`/
  `dotown_1` (no chase battle fires there anyway, but the field-tracking
  no longer attempts to engage).
- `chase_ask_overlay::s_askFiredThisChase` flag stays true since the
  ASK plays at chase START in `domt4_1` (long before reaching `dotown_3`).
  No re-fire risk.
- The `dotown_3` cutscene plays unimpeded.

### What this does NOT fix

**The `doopen2a` "second chase battle" issue.** v0.15.2.10 BAT showed
kani+battleyarou pinned but battle still triggered (capture #2 at
22:03:34 ended cleanly, then ANOTHER chase battle fired before capture #3
at 22:05:05). The clean OTHERS-DIAG at 22:03:34 identifies `director0`
(31 changes/612) as the prime non-pinned suspect.

A `director0` pin is **deferred to v0.15.2.12** for a separate reason:
`director0` might be the chase-progress-tracker, in which case pinning
it could break the same chase-end cutscene we just unblocked. We want
v0.15.2.11 to ship a clean win first, then evaluate `director0`
separately.

### v0.15.2.10 BAT — other major data point

**`domt5_1` clean OTHERS-DIAG (18 slots, in-field, post-battle):**

| Sym | Changes | |
|-----|---------|---|
| selphie2 | 73 | party member, highest |
| irvine | 64 | party member |
| rinoa | 47 | party member |
| zell2 | 31 | party member |
| kani | 5 | pinned |
| battleyarou | 0 | pinned (already dormant) |
| dic, plane1, onkyou, Garutyan, liti, gura, saidotoujou, Gakekuzure | 0 | **all static** |

**The previous "Director-is-the-chase-agent in `domt5_1`" hypothesis is
refuted.** Every Director candidate shows zero changes. The active
entities are all party members running their normal chase-cutscene
animations (Selphie/Irvine running alongside Squall, dialogue triggers).
The kani+battleyarou pin worked correctly in `domt5_1` (only one chase
battle fired, no second-battle issue), so the chase IS triggered by kani
contact in `domt5_1`; the party member script activity is incidental.

### domt1_1 chase coverage — confirmed working

```
[21:59:51] ChaseDetector: battle entered (game-mode 0x0001 -> 0x0003);
           field='domt1_1' chaseActive=1 count=1
```

`chaseActive=1` (was `0` in v0.15.2.9 BAT). The kani pin now activates
in `domt1_1` battles. v0.15.2.10's domt1_1 fix is doing its job.

### Risk

Minimal. The change is a deletion of three array entries plus a comment
update. The chase scene's only active engagement points are the mountain
trail and bridge fields (`domt1_1` through `doopen2a`); `dotown_x` was
never functionally in scope.

### Files changed

- `src/chase_detector.cpp` (~25 lines: 3 entries removed, comment block extended)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.10

Add `domt1_1` to the chase-field set

v0.15.2.9 BAT (Fri 2026-05-08 20:40–20:58) revealed Aaron fought 4
battles during the chase return run. Two of them — in `domt1_1` at
20:52:15 and 20:53:33 — logged with `chaseActive=0` because
`domt1_1` was missing from `CHASE_FIELD_NAMES[]`. Per Aaron's
clarification "no random encounters during the chase scene," those
two battles were chase-robot fights that bypassed the kani pin
entirely. v0.15.2.10 fixes the omission.

### Implementation

**`chase_detector.cpp` (~5 lines):**
- Added `"domt1_1"` to `CHASE_FIELD_NAMES[]`.
- Reordered the array to ascending mountain-trail numbering for
  readability: `domt1_1 → domt2_1 → domt3_2 → domt4_1 → domt5_1`,
  then bridge/town fields.
- Comment trail extended with the v0.15.2.9 BAT findings that
  motivated the addition.

### What this does NOT fix

The `doopen2a` OTHERS-DIAG capture in v0.15.2.9 BAT was contaminated
by the field transition `doopen2a → dotown_3` firing during the 10s
window. kani's full state went `0xFF → 0x00` across many bytes from
engine teardown, inflating `director0` (144), `director1` (122),
`dog` (117), `jumptotown0` (111), and `g_hei2` (105) byte counts —
but these are deallocation artefacts, not active script behavior.
The data tells us nothing about chase-agent identity in `doopen2a`.

`domt5_1` — the field where v0.15.2.8 "Still getting up" was
reported — was NOT visited in this BAT, so the all-Others scanner
data for that field is still pending.

### BAT plan

1. Drive Squall through the chase scene starting from the comm tower.
2. Confirm `chaseActive=1` when battles fire in `domt1_1`
   (FREEZE ACTIVATED log line, KaniFreeze pin engaged).
3. **Specifically reach `domt5_1`** — that's the unsolved field.
4. The OTHERS-DIAG output for `domt5_1` is the next decision point.

### Risk

Zero. Adding a field name to a string array activates more chase
logic; v0.14.x and earlier behavior in non-chase contexts unchanged.
All v0.15.2.9 diagnostic infrastructure preserved unchanged.

### Files changed

- `src/chase_detector.cpp` (~20 lines: array entry + comment block)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.9

All-Others diagnostic scanner

v0.15.2.8 BAT in `domt5_1` confirmed both kani (slot 8) AND battleyarou
(slot 10) are dormant: kani INITIAL has substantial state but FINAL
SUMMARY `changed_bytes=0/612`, battleyarou INITIAL is almost entirely
zero (just `+0x020=0x09`) and FINAL `0/612`. Yet battle still triggered
(count=3 at 20:21:32). Both pins working perfectly, neither entity is
the chase agent. v0.15.2.9 ships a diagnostic that scans EVERY Others
slot in the field to find what entity is actually running scripts during
the chase window.

### Implementation

**`chase_detector.h`/`cpp` (~20 lines):**
- New public functions `GetSymName(int idx)` and `GetSymNameCount()`.
  Read from the existing `s_symNames` cache populated by
  `FieldArchive::LoadSYMNames` in `OnDebouncedFieldChange`.

**`chase_kani_freeze.cpp` (~120 lines):**
- New `#include "field_archive.h"` for `FieldArchive::LoadJSMCounts`.
- New state: `MAX_OTHERS = 32`, `s_othersCountSnapshot`,
  `s_othersBaseSnapshot`, `s_othersStartSymIdx`,
  `s_othersInitial[32][612]` (~20KB).
- `StartCapture` (after kani+battleyarou capture): calls
  `LoadJSMCounts` for the current field, computes
  `othersStartSymIdx = doors+lines+bgs`, dereferences
  `pFieldStateOthers`, snapshots up to `min(others, MAX_OTHERS)` slots
  via SEH-guarded `memcpy`. Logs `OTHERS-DIAG snapshot taken`.
- `EndCapture` (after kani+battleyarou FINAL summaries): for each
  snapshotted slot, SEH-reads current state, computes
  `changed_bytes` count vs `s_othersInitial[slot]`, looks up sym name
  via `ChaseDetector::GetSymName(othersStartSymIdx + slot)`. Logs
  `OTHERS-DIAG slot=N symIdx=K sym='X' changed_bytes=K/612` for every
  slot, plus an overall `N/M slots had byte changes` summary line.
- Field-change deactivation, `Initialize`, `Shutdown` reset the new
  state vars. `Initialize` log line updated to v0.15.2.9 wording.

### What changes for the player

Nothing. The kani+battleyarou pins from v0.15.2.8 are unchanged.
`domt4_1` should still work identically. `domt5_1` will still trigger
the chase battle — we're collecting data, not fixing yet.

### Predicted BAT outcomes

- **ENTITY-DRIVEN-CASE:** one or two slots show high `changed_bytes`
  counts (50-200 bytes). Slot with most changes is the chase agent;
  v0.15.2.10 generalizes the pin to cover it.
- **NO-ENTITY-CASE:** all Others slots show `changed_bytes=0/612`. The
  chase battle is not entity-driven; it likely comes from a SETLINE
  trigger zone (`LineEvent=1` in `domt5_1`'s JSMScan). v0.15.2.10 would
  hook the SETLINE entity's Line script or an alternate battle-start
  function to intercept the trigger.
- **MIXED-CASE:** a few slots show small (1-10 byte) changes — likely
  heartbeat/idle counters. No standout slot. Same path as NO-ENTITY-CASE.

## v0.15.2.8

Dual-entity pin (kani + battleyarou)

v0.15.2.7 BAT in `domt5_1` produced FINAL SUMMARY `changed_bytes=0/612`
over the full 10-second window — zero byte changes anywhere in kani's
entity — but the chase battle still triggered (count=3 at 19:47:33).
MID-WINDOW heartbeat at t=5000ms confirmed `diff_this_tick=0
total_changed_so_far=0`. The resolved kani entity is dead code in this
field. Aaron clarified there are no random encounters during the chase
scene, so the battle MUST come from another entity.

### Hypothesis: `battleyarou` is the universal chase agent

The SYM and JSMScan output for `domt5_1` shows `ent14 cat=3 type=Interactive
Object sym='battleyarou' pos=no(0,0,0 tri=0) param=-1`. Translation:
"battle guy" in Japanese, runtime-driven (no static position), Interactive
Object (collision-eligible). Same 3-method JSM signature as kani in BOTH
`domt4_1` and `domt5_1`. `kani` and `battleyarou` may be a pair: `kani`
is the visible "down" model and `battleyarou` is the active collision
agent. v0.15.2.7 happened to work in `domt4_1` because some chain of
effects, but in `domt5_1` only `battleyarou` is alive.

### Strategy

Pin BOTH entities every frame. If either is the active chase agent in
any field, the pin catches it.

### Implementation

**`chase_detector` (h + cpp, ~90 lines):**
- Refactored `ResolveKaniLocation` into a generic
  `ResolveEntityLocation(targetSym, fieldName, logTag, outLoc)` helper.
  `ResolveKaniLocation` is now a one-line wrapper. New
  `ResolveBattleyarouLocation` is the parallel wrapper for `"battleyarou"`.
- New `ResolveLocPtr` static helper used by both `Get*EntityPtr()`
  functions to walk `pFieldStateBackgrounds`/`pFieldStateOthers` via the
  same JSM-counts arithmetic kani uses.
- New public functions: `GetBattleyarouEntityPtr()`,
  `IsBattleyarouEntityPtr()`, `GetBattleyarouLocation()`. Same
  signatures and semantics as the kani equivalents.
- New static `s_battleyarouLoc`, populated in `OnDebouncedFieldChange`
  alongside `s_kaniLoc` on every field transition. Reset in `Initialize`.

**`chase_kani_freeze.cpp` (~150 lines):**
- New state vars: `s_battleyarouPtr`, `s_battleyarouStrideBytes`,
  `s_battleyarouArrayKind`, `s_battleyarouInitial[612]`,
  `s_haveBattleyarouSnapshot`, `s_battleyarouSnapshot[612]`.
- `StartCapture`: after the existing kani INITIAL hex dump, resolves
  battleyarou via `GetBattleyarouEntityPtr`/`GetBattleyarouLocation`,
  captures INITIAL state via SEH-guarded `memcpy`, logs `BATTLEYAROU
  INITIAL snapshot` hex dump (`BYOU-INIT` label, 16 bytes per row). If
  battleyarou is absent (`symIdx<0`), logs single-line skip and continues
  — battleyarou pin is inert for that field, kani-only behavior identical
  to v0.15.2.7.
- `ApplyFreezePin`: NEW second SEH-guarded block after the existing kani
  pin block. Snapshots battleyarou at the same `t = SNAPSHOT_DELAY_MS
  = 1500ms` moment, `memcpy`s back every frame thereafter. NO
  belt-and-suspenders byte writes (kani's `0x21` and `0x14` magic values
  are kani-specific; applying them blindly to battleyarou could corrupt
  its state). Snapshot-only is safer.
- `EndCapture`: parallel BATTLEYAROU FINAL SUMMARY block —
  byte-by-byte comparison against `s_battleyarouInitial`, lists every
  changed byte with delta. Per-tick FIRST CHANGE diff is NOT done for
  battleyarou (would double the log volume); INITIAL/FINAL pair is
  enough.
- Field-change deactivation clears all new battleyarou state in
  addition to the v0.15.2.7 kani state.
- `Initialize` and `Shutdown` reset all new state vars. `Initialize` log
  line updated to v0.15.2.8 wording.

### Predicted BAT outcomes

- **GOOD:** in `domt5_1`, `BATTLEYAROU INITIAL` shows non-zero state,
  `BATTLEYAROU FINAL SUMMARY` shows few-to-zero changed bytes (pin held),
  no second chase battle, no audible kani movement. Hypothesis confirmed.
- **PARTIAL:** `BATTLEYAROU INITIAL` non-zero but `FINAL` shows many
  changes — our pin isn't winning the write race. Investigate hook
  ordering or alternate offsets.
- **WRONG-CANDIDATE:** `BATTLEYAROU INITIAL` is all zeros (battleyarou
  also dead in this field). Battle still triggers. v0.15.2.9 will try
  the next candidate (`plane1` Director, `dic`, `onkyou`, `gura`, or
  `saidotoujou` — all Interactive Objects in `domt5_1`'s JSMScan).

### Risk

Pinning battleyarou could break field functionality if battleyarou is
involved in scripted events outside the chase. Mitigation: the pin only
activates after a chase-field battle exit (StartCapture trigger), not on
initial field entry, and clears on field change. Pre-battle behavior
(any cutscene triggers) is unaffected.

## v0.15.2.7

Brute-force full-state pin per Aaron's design pivot

v0.15.2.6 BAT in `domt4_1` prevented kani collision (no battle) but
Aaron heard kani's running animation playing in place — the +0x150 anim
ID pin doesn't actually drive rendered animation. The engine reads other
unpinned bytes for playback. Aaron's design preference: "keep the robot
from getting up, rather than locking it in place when it does."

### Strategy

Drop the surgical pin sets. At t=1500ms post-`FREEZE ACTIVATED` (well past
Phase A re-init's last write at t=765ms, well before Phase C wakeup at
t=5375ms), snapshot kani's full post-header state region
(`+0x140`..stride). Then `memcpy` that snapshot back over every frame.
Every byte the engine would otherwise modify to drive wakeup — anim
playback drivers, AI state, position, collision flags, sub-state mirrors,
all the unidentified bytes — stays at "down and settled" forever.

Header bytes (`+0x000`..`+0x028`) are NOT pinned so the engine's
heartbeat/frame counter at `+0x028` still ticks. This preserves whatever
life-detection logic the engine uses while freezing all wakeup state.

For `domt5_1` (where the v0.15.2.6 BAT showed the resolved kani entity
had all-zero bytes), the brute-force pin will snapshot zeros and pin
zeros — effectively a no-op. If the chase battle still triggers there,
it confirms kani isn't the chase agent in that field.

### Subsumed and preserved

- v0.15.2.6's three-region position pin is subsumed (those regions are
  inside `+0x140`..stride and now pinned by the full snapshot).
- The five sub-state byte writes from v0.15.2.5 (`+0x150`/`+0x23F`/
  `+0x241`=0x21, `+0x154`/`+0x1FA`=0x14) are preserved as belt-and-
  suspenders during the `t < 1500ms` grace period. Once the snapshot
  kicks in, the memcpy overwrites them with the same values.

### Implementation

`src/chase_kani_freeze.cpp` (~120 lines):

1. Removed v0.15.2.6 position-pin state vars (`s_havePinnedPosition`,
   `s_pinnedPos_140/190/1B4`).
2. New state vars: `s_haveFullSnapshot` (bool), `s_fullSnapshot[612]`,
   `s_freezeStartTick` (DWORD).
3. New constants: `SNAPSHOT_DELAY_MS = 1500`, `SNAPSHOT_OFFSET_START =
   0x140`.
4. `StartCapture` sets `s_freezeStartTick = GetTickCount()` if
   `!s_haveFullSnapshot` (only on first activation per field).
5. `ApplyFreezePin`:
   - If `!s_haveFullSnapshot && elapsed >= SNAPSHOT_DELAY_MS`: SEH-guarded
     `memcpy` from `kani+0x140` (length = `stride - 0x140` bytes) into
     `s_fullSnapshot`, set flag, log.
   - If `s_haveFullSnapshot`: `memcpy` from `s_fullSnapshot` to
     `kani+0x140`.
6. Field-change deactivation clears `s_haveFullSnapshot` AND
   `s_freezeStartTick` so the next chase field gets a fresh post-Phase-A
   snapshot.
7. `Initialize`/`Shutdown` reset the new state.
8. `FREEZE ACTIVATED` and `Initialize` log lines updated.
9. Header comment trail extended with v0.15.2.7 design pivot rationale.

### Risk

Pinning ~292 bytes including unidentified ones could trip unexpected
engine behavior. Mitigation: header (heartbeat) excluded; if
engine-life-detection logic uses post-header bytes too, kani might
appear "hung" to the engine — which is what we want anyway, since he
should be incapacitated for the entire chase scene. The chase exits via
the Lapin Beach FMV which is position-independent, so a frozen kani can't
break progression.

### Predicted outcomes for next BAT

- **GOOD:** no battle, no audible kani movement in `domt4_1`. FINAL
  SUMMARY shows only `+0x028` heartbeat changed (and possibly a couple
  of header-byte transients).
- **PARTIAL:** no battle but some other audible artifact (e.g., a
  one-shot sound triggered before our snapshot kicks in, or a sound
  played from a separate audio source not gated by entity state).
- **FAILED:** battle still triggers — means kani isn't the chase entity
  in this field. For `domt5_1` especially, expect this; the all-zero
  snapshot tells us the resolved entity is dead. Next investigation:
  identify the actual chase entity (likely `battleyarou`, which has the
  same 3-method signature as kani in both fields).

## v0.15.2.6

Layer position pin on top of v0.15.2.5's full sub-state pin

v0.15.2.5 BAT in `domt4_1` proved the full sub-state pin held perfectly
but kani still triggered battles #2 and #3. Diagnosis: kani has TWO
independent wakeup paths, and the AI/movement subsystem doesn't read
any of the bytes we've pinned so far.

### v0.15.2.5 BAT findings

- **Pin held:** all five pinned bytes (`+0x150`, `+0x154`, `+0x1FA`,
  `+0x23F`, `+0x241`) ABSENT from FINAL SUMMARY in capture #1. v0.15.2.5
  had 17 changed bytes vs v0.15.2.4's 19 — the two-byte difference is
  exactly `+0x154` and `+0x1FA`, the new pins this build added.
- **Engine never decremented `+0x154`:** capture #2 INITIAL `+0x154` =
  `0x14` (vs v0.15.2.4 BAT's `0x0C`). The pin successfully prevented the
  cross-battle persistence we saw last time.
- **Phase C still happened.** Position bytes `+0x140-+0x148`,
  `+0x190-+0x199`, `+0x1B5-+0x1BD` drift at t=~5400ms regardless of
  whether `+0x154` is pinned. Battle #2 fired at 18:53:43 (7s into
  capture #1, count=2). Battle #3 at 18:54:53 (count=3).

### Diagnosis: timer-driven AI is decoupled from sub-state

Kani has at least two parallel wakeup mechanisms:

1. Sub-state countdown via `+0x154` / `+0x1FA` — controls some
   animation transitions. Pinned by v0.15.2.5.
2. AI/movement subsystem — timer-driven, fires at Phase C onset
   regardless of sub-state value. Moves kani's coordinates toward Squall.
   Collision detection on those coordinates triggers a new battle.

The AI doesn't gate movement on `+0x154`. So pinning `+0x154` doesn't
stop movement.

### v0.15.2.6 changes

`src/chase_kani_freeze.cpp` (~50 lines):

1. New state vars `s_havePinnedPosition` (bool) and three 12-byte
   buffers `s_pinnedPos_140`, `s_pinnedPos_190`, `s_pinnedPos_1B4`.
2. `StartCapture` snapshots three position regions on FIRST freeze
   activation per field: `+0x140-+0x14B` (X/Y/Z dwords),
   `+0x190-+0x19B` (second copy), `+0x1B4-+0x1BF` (third copy).
   SEH-guarded read with diagnostic log line on success and on read
   failure. Subsequent captures in the same field do NOT re-snapshot.
3. `ApplyFreezePin` writes the three buffers back via `memcpy` every
   frame after the existing five byte writes, gated on
   `s_havePinnedPosition`.
4. Field-change deactivation in `ApplyFreezePin` clears
   `s_havePinnedPosition` so the next chase field gets a fresh
   snapshot.
5. `Initialize` and `Shutdown` reset the new state.
6. `FREEZE ACTIVATED` and `Initialize` log lines updated to mention
   the position pin.

Why snapshot ONLY on first activation: capture #2 INITIAL position is
kani's chase-end position from prior battle. We want capture #1's
INITIAL spawn position (-995, 3562, 230 in `domt4_1`).

### Risk

Pinning kani's coordinates may produce a visual rendering glitch (kani
standing still in the chase scene). Functionally safe — the chase scene
ends via the Lapin Beach FMV, which is position-independent (scripted
event on Squall reaching the field exit). Aaron is blind so the visual
glitch doesn't affect UX.

### Predicted outcomes for next BAT

- **GOOD:** all eight pinned regions ABSENT from FINAL SUMMARY (or
  position bytes appear with `delta=0` because we wrote back same value).
  Kani stays at spawn, no second/third battle. Push v0.15.2.6.
- **PARTIAL:** position pin holds but other AI state (e.g., velocity,
  collision flag, target reference) drives a battle some other way.
  Diagnostic FINAL SUMMARY will show what new bytes the engine touched.
- **FAILED-PIN:** position bytes STILL change in FINAL despite memcpy.
  Engine is winning the write race or writing to a different memory
  location for the rendered position.

## v0.15.2.5

Layer +0x154 and +0x1FA sub-state pins on top of v0.15.2.4's anim-ID trio

v0.15.2.4 BAT in `domt4_1` had a partial-success outcome. The pin held
perfectly on the three animation-ID bytes — across two consecutive
captures the FINAL SUMMARY confirmed `+0x150`, `+0x23F`, and `+0x241`
were never modified — but kani still woke up and triggered battles #2
and #3 in rapid succession.

### Diagnosis: the anim-ID trio is purely a RENDERING pin

The `+0x150` byte controls which pose the model displays. Pinning it
makes kani render as "down" but does not affect the engine's AI or
movement logic. The actual wakeup is driven by:

- `+0x154` (dword LSB): sub-state countdown, `0x14` → `0x0C`
- `+0x1FA` (byte): sub-state mirror, `0x14` → `0x0C`

The engine decrements the `+0x154` LSB from `0x14` over ~5 seconds.
Once it crosses some threshold the AI starts moving kani toward
Squall, and collision detection triggers a new battle regardless of
what the rendered pose is.

### Cross-battle persistence

Capture #2 INITIAL `+0x154` = `0x0C`. The engine PERSISTED the
post-wakeup sub-state value across the battle/field reload — only
`+0x150` was reset to `0x21` by the engine's normal init pass. That's
why each successive battle in the v0.15.2.4 BAT woke up faster than
the last: the countdown never had a chance to reset.

### v0.15.2.5 changes

`src/chase_kani_freeze.cpp` (~25 lines):

1. `ApplyFreezePin()` now writes `0x14` to bytes `+0x154` and `+0x1FA`
   every frame in addition to the existing `0x21` writes to `+0x150`,
   `+0x23F`, `+0x241`.
2. Header comment trail and inline pin-section comment updated with
   v0.15.2.4 BAT findings and v0.15.2.5 rationale.
3. `FREEZE ACTIVATED` log line updated to mention all five pinned bytes.
4. `Initialize` log line updated to v0.15.2.5.

Byte-level writes are correct here — the `+0x154` dword's upper three
bytes were already `0` in INITIAL, and the FIRST CHANGE log only ever
showed the LSB changing.

### Predicted outcomes for next BAT

- **GOOD:** All five pinned bytes ABSENT from FINAL SUMMARY, kani stays
  down, no second/third battle. Push v0.15.2.5 to GitHub.
- **PARTIAL:** Sub-state pinned (absent from FINAL) but position bytes
  `+0x140-+0x148`, `+0x190-+0x199`, `+0x1B5-+0x1BD` still drift. AI
  is moving kani's coordinates despite the full sub-state pin. v0.15.2.6
  layers on a position pin using the INITIAL position as the target.
- **FAILED-PIN:** `+0x154` STILL changes despite the pin. Engine wins
  the write race. Need to investigate timing or hook a different write
  site.

## v0.15.2.4

Add per-frame FREEZE on top of the v0.15.2.3.1 diagnostic capture

v0.15.2.3.1 BAT in `domt4_1` (capture started 18:00:11 on `mode 4->1`,
ran the full 10s window) revealed a clean three-phase pattern matching
Aaron's UX description.

### Three-phase wakeup pattern

- **Phase A (0–700ms):** field re-init burst as kani spawns into the
  field after battle. Many bytes update once.
- **Phase B (700–5300ms, ~4.6s):** QUIET PERIOD — kani is visibly on
  the ground. The mid-window heartbeat at t=5000ms shows 18 bytes
  changed total, only 1 diff that tick.
- **Phase C (5300ms onward):** wakeup burst. Animation flip, position
  fields start updating rapidly. Battle #2 fired at 18:00:18 — only
  7 seconds after capture start — confirming kani fully woke up and
  caught Squall inside our window.

### Wakeup-control bytes (Phase C onset, t=5593–5640ms)

Five bytes flip in a tight 50ms window and stay at the new value
through end-of-capture (per FINAL SUMMARY):

| Offset | Initial | Final | Pattern |
|--------|---------|-------|---------|
| +0x150 (dword) | 0x00000021 | 0x00000011 | animation ID, "down" → "running" |
| +0x154 (dword) | 0x00000014 | 0x0000000C | paired sub-state |
| +0x23F (byte) | 0x21 | 0x11 | anim shadow register A |
| +0x241 (byte) | 0x21 | 0x11 | anim shadow register B |
| +0x1FA (byte) | 0x14 | 0x0C | sub-state mirror |

Position bytes confirm the wakeup is real: kani moved from world
position (-995, 3562, 230) to (-548, 2615, 231) — about 947 units
toward Squall during Phase C.

### v0.15.2.4 changes

`src/chase_kani_freeze.cpp` (~50 lines):

1. Header comment block updated with v0.15.2.3.1 → v0.15.2.4 trail.
2. New state vars `s_freezeActive` and `s_freezeFieldName[64]`.
3. `StartCapture` now sets `s_freezeActive = true` and saves
   `ChaseDetector::GetDebouncedFieldName()` into `s_freezeFieldName`
   as the freeze-anchor.
4. New helper `ApplyFreezePin()` called every `Update()` after the
   capture-trigger logic:
   - If `!s_freezeActive` → no-op.
   - If current debounced field name is non-empty AND different from
     `s_freezeFieldName` → deactivate freeze and log.
   - Otherwise re-resolve kani via `ChaseDetector::GetKaniEntityPtr()`
     each frame and SEH-write `0x21` to bytes at +0x150, +0x23F, +0x241.
   - Empty-string field name during the 2s name-debounce after a
     `fieldId` flip does NOT deactivate (we don't yet know the
     destination field).
5. `Initialize`/`Shutdown` clear the new state.

### Conservative first attempt

Only the three animation-ID bytes are pinned in this build. The
+0x154 dword and +0x1FA byte sub-state mirrors are left free — if
the next BAT shows kani still wakes up, v0.15.2.5 layers them on.

The diagnostic capture continues to run alongside the freeze so the
next BAT's FINAL SUMMARY can verify pin compliance: +0x150, +0x23F,
+0x241 should be ABSENT from the changed-bytes list if the freeze
is working. If kani still moves and triggers a second battle despite
the pin, the diagnostic will show what other bytes the engine wrote
at the wakeup moment to drive it.

## v0.15.2.3.1

Fix v0.15.2.3 capture trigger — fired during the wrong game-mode window

v0.15.2.3 BAT (`domt4_1`, `count=1` chase-mode kani battle ending at
17:38:31) showed the capture started cleanly on the mode 3->non-3
edge but logged ZERO byte changes across the entire 5-second window.

### Root cause

The post-battle game-mode sequence is:

- mode 3 (battle)
- mode 5 (fade-to-field transition, ~6 seconds, **engine pauses ALL
  entity updates** — the entity blocks are frozen)
- mode 1 (active field, entity state machines resume)

v0.15.2.3 triggered on `prev==3 && cur!=3`, which fires the moment
battle ends — i.e., entry into mode 5. The capture window therefore
landed entirely in the dead transition phase and saw no activity.

Evidence in the BAT log:

- 17:38:31 — capture started (mode 3->5)
- 17:38:36 — capture complete, 0 changes
- 17:38:37 — `FieldNavigation: [PSHM_W-HOOK] Init done` (field
  re-initializing, mode 5->1 transition completing) — **one second
  after our window closed**
- 17:38:44 — second kani battle (`count=2`)

The wakeup completed within ~7 seconds of mode 1 starting, entirely
outside our v0.15.2.3 capture window.

### Fix

`src/chase_kani_freeze.cpp`:

1. New `MODE_FIELD_VAL = 1` constant.
2. New `s_battleSeenRecently` flag, set true when game mode reads
   `MODE_BATTLE_VAL = 3`, cleared after a capture starts.
3. Trigger condition rewritten: capture fires on the first frame of
   `MODE_FIELD_VAL` with `s_battleSeenRecently` true, i.e., the
   moment the engine returns to active field mode following any
   battle. Catches `3->5->1`, `3->1`, `3->4->1` etc. uniformly.
   Re-arms after each capture so subsequent kani battles in the
   same chase session each get their own capture.
4. `CAPTURE_DURATION_MS` bumped 5000 -> 10000 ms.
5. `MID_SUMMARY_AT_MS` bumped 2500 -> 5000 ms.

### Same scope as v0.15.2.3

Diagnostic only. v0.15.2.4 still installs the actual freeze hook
based on the (now correctly-captured) wakeup-byte data.

### Files

- `src/chase_kani_freeze.cpp` (~40 lines)
- `src/ff8_accessibility.h` (this version)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` updated

## v0.15.2.3

DIAGNOSTIC build for chase robot freeze redesign — capture kani's wakeup byte

v0.15.2.2 BAT (domt2_1, kani battle exit at 16:53:57) proved the v0.15.1
`ChaseBattleFreeze` approach is not viable in two of the chase fields.
`ChaseDetector` saw both battles via game-mode 1→3 polling (count=1 then
count=2), but the field log shows ZERO `ChaseBattleFreeze: NO-OP` or
`ChaseBattleFreeze: pass-through` lines. `opcode_battle` is dead code for
these fields — they enter battle via a different path (random-encounter
step counter or scripted encounter). The v0.15.2.1 robot fix worked in
domt4_1 only because that's the one chase field where the kani battle
goes through `opcode_battle`.

### Design pivot — hook the wakeup, not the battle

Aaron clarified the desired UX: post-battle, when the field re-appears,
kani is visibly on the ground for several seconds before the engine's
wakeup animation/AI-state runs and the robot stands back up to resume
chasing. The cleaner design is to hook that wakeup transition — which
is the SAME state machine across every chase field — and pin kani at
"incapacitated" until the field changes, instead of trying to intercept
battles at all.

This sidesteps the entire domt2_1/domt5_1 battle-entry mystery. It also
means the robot stays down for the duration of the party being on that
field (the goal Aaron stated), with natural reset on field unload
because entity state is reinitialized when a new field loads.

### v0.15.2.3 ships a diagnostic

New module `src/chase_kani_freeze.{h,cpp}` (~330 lines). On every
game-mode 3→non-3 transition (battle exit) while `ChaseDetector` reports
we're in a chase field with kani's entity address resolved, snapshot
kani's full entity block (stride `0x264` for Others, `0x1B4` for
Backgrounds) and continuously diff for 5 seconds.

Log output:

- `KaniFreeze: ===== CAPTURE STARTED =====` plus trigger context
- `KaniFreeze: INITIAL +0x000:` — full entity block hex dump (~32 lines)
- Per-byte first-change events:
  `KaniFreeze: t=1234ms tick=42 +0x07A: FIRST CHANGE 0x00 -> 0x01`
- `KaniFreeze: MID-WINDOW heartbeat t=2500ms ...` — visibility pulse
- `KaniFreeze: FINAL SUMMARY ...` — every changed byte with
  initial-vs-final values and signed delta
- `KaniFreeze: ===== CAPTURE COMPLETE =====`

The expected pattern in the BAT log: kani is on the ground for several
seconds = mostly-quiet log with maybe one byte (an animation tick or
AI-state counter) incrementing slowly, then a sharp inflection where
ONE byte (or small word/dword) flips at the moment kani stands up, then
a flood of position-byte changes per tick as kani begins moving.

v0.15.2.4 will pin the inflection byte at its initial value, freezing
kani in the on-ground state until field exit.

### Other changes

- `src/chase_battle_freeze.cpp` removed from build (`deploy.bat` compile
  line replaced with `chase_kani_freeze.cpp`). The `.h` and `.cpp`
  source files remain on disk as orphans — v0.15.2.4 cleanup may delete
  them after the new approach is BAT-validated.
- `src/dinput8.cpp`: include swapped `chase_battle_freeze.h` →
  `chase_kani_freeze.h`, `Initialize`/`Shutdown` calls updated, new
  `ChaseKaniFreeze::Update()` call added to per-tick chain (after
  `ChaseAskOverlay::Update()` so `ChaseDetector`'s kani address has
  been refreshed for the same tick).

### Preserved

- v0.15.1.2 timing fix (3-second deferred `OpenAsk` so Squall's
  chase-trigger line plays first)
- v0.15.2.1 `MODE_BATTLE_VAL=3` fix in `chase_detector.cpp` —
  unrelated to wakeup, still needed for `ChaseDetector`'s per-field
  battle counter and for `ChaseKaniFreeze`'s battle-exit edge detection
- v0.15.2.2 `chase_ask_overlay` TTS+keyboard-only path

### Files

- `src/chase_kani_freeze.h` (NEW, ~30 lines)
- `src/chase_kani_freeze.cpp` (NEW, ~300 lines)
- `src/dinput8.cpp` (~10 lines: include + Initialize/Update/Shutdown wiring)
- `src/deploy.bat` (1 line: compile-list filename swap)
- `src/ff8_accessibility.h` (this version)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` updated

## v0.15.2.2

Drop engine-rendered chase ASK proxy slot (TTS+keyboard only)

v0.15.2.1 BAT shipped two diagnostic findings that close the
engine-ASK chapter for v0.15.x.

### Finding 1 — the engine doesn't render externally-populated slots

The `LogProxySlotSnapshot` polling logged our proxy slot's state
every second for 13 seconds after a fully-correct
`PopulateProxySlot` (captured-from-real-ASK template values:
`state=0x0D`, `mode1=0x1000`, `trans=0x1000`, `geom=[0x50, 0x0A,
0xCC, 0x5D]`, `t1=t2=non-NULL`, `firstQ=1 lastQ=2 curQ` tracking,
`cb1=cb2=NULL`).

Every single SLOT-SNAP from `[22:27:21]` through `[22:27:34]`
showed our writes preserved EXACTLY. The engine never modified
our slot — it just IGNORED slot 1 in its render pass for the
entire 13 seconds the ASK was 'open'. At `[22:27:34]` the kani
collision triggered a battle, and the field-to-battle transition
wiped the slot to defaults (`geom=[0x40, 0x40, 0x80, 0x80]`,
`t1=t2=NULL`, `state=0`, `firstQ=lastQ=0xFF`).

This rules out every 'we're missing a field' hypothesis. Conclusion:
the engine's render loop doesn't iterate `pWindowsArray` looking
for `state=0x0D` dialogs. **Render is bound to script-VM context—only slots that an active script has parked on via `opcode_ask` /
`opcode_aask` get rendered.** Replicating that binding from a DLL
hook would require disassembly work to locate the script-VM's
'current dialog slot' reference — a v0.15.3+ investigation,
outside v0.15.2.x's scope.

v0.15.2.x ships with the chase ASK as TTS+keyboard only. **The
engine-rendered visual is an open feature gap, not a closed item.**
It remains on the backlog to revisit once the script-VM binding
mechanism is understood, or once a different rendering path
(e.g. hooking the renderer directly) becomes tractable. The five
iterations (v0.15.0–0.15.2.1) of engine-ASK debugging produced
solid documentation of the `ff8_win_obj` layout and confirmed the
script-VM binding constraint; that investment isn't lost. The
decision to defer is about engineering tractability for the
v0.15.x line, not about declaring the audio-only path
sufficient on its own.

### Finding 2 — the v0.15.2.1 robot fix WORKS in domt4_1

Field log shows:

```
[22:29:57] ChaseBattleFreeze: NO-OP kani BATTLE in 'domt4_1' (battleCount=1, freeze#1) — returning 3
[22:30:04] ChaseBattleFreeze: NO-OP kani BATTLE in 'domt4_1' (battleCount=1, freeze#2) — returning 3
```

These NO-OPs are the 'interruption / delay' Aaron heard between
battles. The freeze gate fires correctly: first kani contact in
domt4_1 plays out (count goes 0 → 1), subsequent contacts NO-OP
(opcode_battle returns advance-code 3 without calling original).

The kani's bounce-back animation runs on its own AI timeline
independent of `opcode_battle`, so the entity still 'gets up'
visually — a sighted player would see the kani animate even
though the battle screen was suppressed. That's a separate issue
for v0.15.3 (suppress the wakeup animation, or hook a
lower-level battle-entry function that catches both code paths).

### Finding 3 — ChaseBattleFreeze doesn't fire in domt5_1 (deferred)

Log shows two domt5_1 battles at `[22:32:43]` and `[22:34:17]`,
both detected by `ChaseDetector::PollGameMode` (mode 1 → 3) but
zero `pass-through` or `NO-OP` log lines from `ChaseBattleFreeze`.
`opcode_battle` isn't being called for those battles. They use
a different code path — likely random-encounter step counter or
scripted encounter, not kani-collision-triggered `opcode_battle`.
Deferred to v0.15.3 investigation. Possible v0.15.3 fixes:
hook the lower-level battle-entry function (the one actually
triggering mode 1 → 3) instead of `opcode_battle`.

### v0.15.2.2 changes

One function modified: `chase_ask_overlay.cpp::OpenAsk`. Replace
the `FindFreeWindowSlot` + `PopulateProxySlot` block with
`s_proxySlotIdx = -1` plus a one-line log and a 30-line comment
explaining the rationale. Helper functions remain in source as
unreferenced statics (MSVC C4505 is off at default `/W3`,
compiles clean) so a future version can re-enable proxy-slot
rendering if the script-VM binding mechanism is ever discovered:

- `FindFreeWindowSlot`
- `PopulateProxySlot`
- `ReleaseProxySlot`
- `LogProxySlotSnapshot`
- `SyncEngineCursor`

All downstream paths gated on `s_proxySlotIdx >= 0` are now
inert: the SLOT-SNAP polling in `Update()` early-returns inside
`LogProxySlotSnapshot`; `SyncEngineCursor` early-returns;
`ReleaseProxySlot` in `CloseAsk` early-returns. No log spam, no
behavior change in the TTS+keyboard path.

### What v0.15.2.2 BAT will verify

1. Chase trigger MES `"Forget it!  Let's go!"` plays via NVDA.
2. After the 3-second deferred-open delay, the chase ASK prompt
   is spoken (TTS-only — no engine dialog box, that's intended).
3. Up/Down toggles between Auto-drive and Manual with TTS
   announcements, Enter commits, 1/2 number-key shortcuts work.
4. Selection persists to `ff8_accessibility.ini` under `[Chase]`.
5. First kani contact in domt4_1 fires a normal battle. Subsequent
   contacts in same field NO-OP (audible as 'no battle music
   transition, kani bounces off').
6. Field log no longer shows the 1Hz SLOT-SNAP spam.
7. Lapin Beach FMV ends the chase cleanly; INI choice survives.

### v0.15.1.2 timing fix preserved

OnDialogText still defers OpenAsk by 3 seconds via
`s_triggerPending` / `s_triggerTimestamp` so Squall's line plays
first. Update() polls per-tick and calls OpenAsk when delay
elapses.

### v0.15.2.1 robot fix preserved

`MODE_BATTLE_VAL = 3` in `chase_detector.cpp` still applies. Edge
detection on game-mode 1 → 3 increments `s_currentFieldBattleCount`
on each kani-triggered battle in chase fields.

### Files changed

- `src/chase_ask_overlay.cpp` — ~30 lines: OpenAsk proxy
  allocation block replaced with disable + 30-line rationale
  comment.
- `src/ff8_accessibility.h` — version bumped 0.15.2.1 → 0.15.2.2,
  comment trail extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — ready-to-BAT state,
  v0.15.3 backlog updated with the domt5_1 finding.

## v0.15.2.1

Fix robot getting up after battle, plus diagnostic snapshot for the engine ASK rendering issue

v0.15.2 BAT surfaced two issues:

1. **Robot still gets up after a brief pause after battle.** ChaseBattleFreeze
   was passing through every kani battle even after the first.
2. **Engine-rendered ASK dialog still doesn't appear.** The proxy slot
   was successfully populated (`PopulateProxySlot` returned true,
   `ASK opened (proxySlot=1)`) but no dialog box rendered visually.

### Fix #1 — ROBOT (high-confidence one-line fix)

`chase_detector.cpp` had `MODE_BATTLE_VAL = 999`. v0.15.2 BAT showed
the game mode goes to `3` during chase battles and stays there for
95 seconds (with Squall's field-position frozen, classic 'Squall is
on the battle screen' pattern). Battle mode in this Steam 2013 build
is `3` at the field-mode polling resolution — not 999.

The edge detection `prev != 999 && cur == 999` never fired, so
`s_currentFieldBattleCount` stayed at 0 across kani contacts. The
freeze gate's `count >= 1` condition was never satisfied. Result:
every kani battle passed through, including the second-and-later
battles we wanted to NO-OP.

Fix: `MODE_BATTLE_VAL = 3`. Edge detection fires on first kani
battle (count goes to 1), freeze gate triggers on second
(`NO-OP kani BATTLE`).

Field log will now show:

```
ChaseDetector: battle entered (game-mode 0x0001 -> 0x0003); field='domt4_1' chaseActive=1 count=1
[… first battle plays out …]
ChaseBattleFreeze: NO-OP kani BATTLE  (mode=manual inChase=1 kaniCaller=1 count=1)
```

### Fix #2 — ASK rendering: geometry writes + diagnostic snapshot

v0.15.2 BAT log shows `proxy slot 1 populated v0.15.2 (textBuf=0x6DCA3B10,
mode1=0x1000, state=0x0000000D, trans=0x1000, firstQ=1, lastQ=2,
curQ=2, callbacks=NULL)` — our writes succeeded. But the engine
didn't render anything.

Looking at the captured slot[0] from a real engine ASK earlier:
bytes 0x00–0x07 contained `50 00 0A 00 CC 00 5D 00` (4 × uint16 LE
= 0x0050, 0x000A, 0x00CC, 0x005D — likely x, y, w, h or similar
dialog box geometry). v0.15.2 left those zero, which may have caused
the engine to cull the slot from the render pass (zero-sized
window).

**v0.15.2.1 changes:**

1. New `WIN_OBJ_GEOM0_OFFSET = 0x00` constant and
   `TEMPLATE_GEOM[4] = { 0x0050, 0x000A, 0x00CC, 0x005D }` —
   captured values written by `PopulateProxySlot`.
2. New `LogProxySlotSnapshot(label)` diagnostic helper. Dumps the
   full state of our proxy slot to the field log: geometry, both
   text pointers, win_id, mode1, trans, state, firstQ/lastQ/curQ,
   both callbacks. Called once right after `PopulateProxySlot`
   (label `"post-populate"`) and again every 1 second while the
   ASK is open (label `"while-open"`).

The snapshot data tells us which scenario we're in:
- **State stays at 0x0D over time** → engine ignores our slot
  entirely. Our slot is missing something needed to be a render
  target (perhaps slot 0 is special, or the script VM needs to
  be parked on this slot).
- **State resets to 0 or fields get rewritten** → engine touches
  the slot but invalidates it (bad fields).
- **Engine writes new values to fields we didn't set** → we'd
  see them in the snapshot, learn the missing pieces.

### What v0.15.2.1 BAT will verify

1. **Robot fix.** Second kani battle in same chase field NO-OPs.
   Field log shows `count=1` after first battle and the freeze
   line on second contact.
2. **Engine ASK dialog box appears** (if geometry fix is the
   issue) OR snapshot data narrows down what's missing.

### Files changed

- `src/chase_detector.cpp` — ~10 lines: `MODE_BATTLE_VAL` constant
  changed to 3, with comment explaining why.
- `src/chase_ask_overlay.cpp` — ~80 lines:
  `WIN_OBJ_GEOM0_OFFSET` constant, `TEMPLATE_GEOM[4]` constant,
  geometry writes in `PopulateProxySlot`, log line update,
  `LogProxySlotSnapshot` helper, post-populate snapshot call in
  `OpenAsk`, while-open snapshot call in `Update`,
  `s_lastProxySnapshotTick` state var.
- `src/ff8_accessibility.h` — version bumped, comment trail
  extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — ready-to-BAT state.

## v0.15.2

Re-enable engine-rendered chase ASK dialog using captured template values

v0.15.1.2 BAT included a quick chase-diag ASK snapshot capture. Aaron
triggered an in-game 3-option ASK with chase-diag (F12) enabled, and
`OnAskOpcodeFired` dumped the full state of slot[0]. The captured
values let us re-enable the engine-rendered window that v0.15.1.1
had to disable.

### What we learned from the snapshot

```
slot[0] when AASK is active:
  state    = 0x0000000D    (was guessing 0x07)
  mode1    = 0x00001000     (was guessing 0x0005)
  trans    = 0x00001000     (open_close_transition; was 0)
  cb1      = 0x00000000     NULL!
  cb2      = 0x00000000     NULL!
  firstQ=2, lastQ=4, curQ=2 (3-option ASK in capture; ours is 1/2/1)
  t1, t2   = both populated, ~75 bytes apart in same buffer
```

**v0.15.1's lockup root cause is now confirmed.** The engine doesn't
use the `+0x34` / `+0x38` callbacks for ASK windows at all — they're
NULL in real ASKs. v0.15.1's mistake was setting them to function
pointers, which the engine apparently interpreted as some other
dispatch target (per-frame render tick?) and invoked at ~180 Hz.

### What v0.15.2 does

1. **Updates `chase_ask_overlay.cpp` template constants** to match
   captured values:
   - `TEMPLATE_MODE1 = 0x1000` (was 0x0005)
   - `TEMPLATE_STATE = 0x0000000D` (was 0x07)
   - new `TEMPLATE_TRANS = 0x1000` constant
2. **Updates `PopulateProxySlot`** to write the captured values:
   - `text2 = textBuf` (was nullptr; real ASKs have both populated)
   - `open_close_transition = 0x1000` (was 0; that probably hid the
     window even after state was set to active)
   - **callbacks set to 0** (was function pointers — the bug)
3. **Re-enables proxy-slot allocation in `OpenAsk()`** (undoes
   v0.15.1.1's `s_proxySlotIdx = -1` hardcode).
4. **Adds `SyncEngineCursor()`** — PollKeys writes
   `s_currentHighlight + 1` to the engine's `curQ` field after each
   Up/Down keypress, so the engine-rendered cursor visually tracks
   the selection. Hybrid input: engine renders, our PollKeys still
   owns the input mechanism. On commit, `CommitChoice` → `CloseAsk`
   → `ReleaseProxySlot` releases the slot via `state = 0`.

v0.15.1.2's timing fix is preserved unchanged: `OnDialogText` still
defers `OpenAsk` by 3 seconds via `s_triggerPending` /
`s_triggerTimestamp` so Squall's `"Forget it!  Let's go!"` plays
through first.

### What v0.15.2 BAT will verify

1. Aaron hears Squall's chase-trigger line first.
2. Chase-mode ASK appears as **engine-rendered window** AFTER the
   3-second delay. Field log shows `proxy slot %d populated v0.15.2
   (textBuf=..., mode1=0x1000, state=0x0000000D, trans=0x1000,
   firstQ=1, lastQ=2, curQ=2, callbacks=NULL)`.
3. Up/Down/Enter and 1/2 still work correctly with TTS
   announcements.
4. Engine cursor visually tracks selection (sighted spectators
   only — not strictly necessary since Aaron is blind).
5. `chase_battle_freeze` still caps battles at one per chase field.
6. Lapin Beach FMV ends chase cleanly. ASK once-per-chase flag
   clears.
7. **No callback spam.** Field log should NOT contain repeating
   `ConfirmCallback fired` / `CancelCallback fired` lines (those
   functions remain in source as unreferenced statics but are no
   longer assigned to slot fields).

### Risks

- **Engine input race.** If the engine ALSO consumes our key
  events (because it sees `state=0x0D` as 'active ASK awaiting
  input'), our PollKeys handler may double-fire. Symptom: Up/Down
  counting twice per press in the field log. If observed, v0.15.2.1
  will need to either suppress engine input on our slot or
  detect-and-ignore.
- **Encoded text rendering.** Our `EncodeChar` table is best-effort
  for FF8 text encoding. The rendered window may show garbled text
  on screen. This doesn't affect Aaron's experience (TTS speaks
  the right thing); refining the table is a v0.15.3+ polish item.

### Known minor issue (defer to v0.15.3)

The `chase_diag` ASK snapshot's `+10:` hex dump row showed only
`"000"` instead of the full 16 bytes — `snprintf` size-tracking
bug in `chase_diag.cpp` where `p2`/`p3` accumulators go negative
when `row3` overflows, causing UB in subsequent calls. The
high-level fields all captured cleanly so this didn't block
v0.15.2.

### Files changed

- `src/chase_ask_overlay.cpp` — ~50 lines: constants update,
  `PopulateProxySlot` rewrite, `OpenAsk` re-enable, new
  `SyncEngineCursor` helper, PollKeys integration.
- `src/ff8_accessibility.h` — version bumped, comment trail
  extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — ready-to-BAT state.

## v0.15.1.2

Fix chase ASK timing so Squall's chase-trigger line plays first

v0.15.1.1 fixed the lockup but introduced a timing problem: Aaron heard
the chase-mode prompt but never heard Squall's `"Forget it!  Let's go!"`.

### Root cause

`ChaseAskOverlay::OnDialogText` was called synchronously from inside
`field_dialog`'s `Hook_show_dialog`, BEFORE field_dialog's own NVDA
speak path got to announce Squall's line. Our `SpeakInitialPrompt`
called `ScreenReader::Speak(…, /*interrupt=*/true)`, which clobbered
the SAPI queue. Aaron then advanced the dialog past Squall's line
while picking an option from our prompt.

### The fix

Defer the open by 3 seconds. `OnDialogText` no longer calls `OpenAsk`
synchronously; it sets `s_triggerPending = true` and stores
`s_triggerTimestamp = GetTickCount() + TRIGGER_DELAY_MS` (3000 ms).
`Update()` polls per-tick and calls `OpenAsk` once the delay elapses,
with a re-check of `IsInChaseField()` and `!s_askFiredThisChase` so
the ASK doesn't open if the chase ended during the delay window.

3 seconds covers a 5-word line at any reasonable TTS rate. The
default NVDA pace renders `"Forget it!  Let's go!"` in roughly 2
seconds; the extra second is buffer for slower rates and the dialog
box's letter-by-letter display.

### Engine-rendered ASK — still deferred to v0.15.2

Aaron also reported the ASK fired as TTS only — no in-game dialog
box. v0.15.1.1 explicitly disabled the proxy-slot population in
`OpenAsk()` to prevent the v0.15.1 callback feedback loop. The
proxy-window code is preserved verbatim but won't be re-enabled
until we have real engine-set values for `mode1` (currently 0x05
guess), `state` (currently 0x07 guess), and the callback ABI at
`+0x34` / `+0x38`.

`ChaseDiag::OnAskOpcodeFired` (added in v0.15.1) is already wired and
dumps full snapshots of all 8 `ff8_win_obj` slots whenever
`opcode_ask` or `opcode_aask` fires AND chase-diag is enabled (F12).
**To unlock v0.15.2 engine ASK, capture a snapshot:**

1. Turn chase-diag ON (F12) before any natural in-game ASK.
2. Trigger any opcode_ask: vendor `Buy / Cancel`, NPC yes/no
   question, save-point's save-yes-no, Cid's `"Wanna become a SeeD?"`.
3. Send the resulting `Logs/ff8_field.log` (or just the
   `[CHASE-DIAG-ASK]` block).

v0.15.2 will hardcode the captured values into `TEMPLATE_MODE1` /
`TEMPLATE_STATE` and replace the stub `ConfirmCallback` /
`CancelCallback` with engine-aware no-ops or an ABI-correct handler
based on what we observe.

### What v0.15.1.2 BAT will verify

1. **Aaron hears Squall's line.** `"Forget it!  Let's go!"` plays
   through NVDA before our chase-mode prompt starts.
2. **The chase-mode prompt fires after the 3-second delay.** Field
   log shows `chase trigger MES detected ... deferring ASK open by
   3000 ms` followed 3 seconds later by `deferred-open timer
   expired; opening ASK now`.
3. **The rest still works** — chase mode persists, `chase_battle_freeze`
   no-ops second-and-later kani battles, Lapin Beach FMV plays.

### Files changed

- `src/chase_ask_overlay.cpp` — new state vars (`s_triggerPending`,
  `s_triggerTimestamp`), constant (`TRIGGER_DELAY_MS = 3000`),
  rewrite of `OnDialogText` to defer instead of open, addition to
  `Update()` for the deferred-open timer + chase-end cancel.
- `src/ff8_accessibility.h` — version bumped, comment trail
  extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated to ready-to-BAT
  state.

## v0.15.1.1

Hotfix for v0.15.1 BAT lockup: disable the engine-rendered proxy window in the chase ASK overlay

The v0.15.1 BAT showed the chase trigger working perfectly — ChaseDetector
activated, the kani slot resolved correctly (`Backgrounds slot 4`), the
`"Forget it!  Let's go!"` MES was detected, and the ASK opened. But the
proxy-window experiment locked the game.

### Root cause

When `OpenAsk()` populated a free `pWindowsArray` slot with `state=0x07`
and registered our `ConfirmCallback` / `CancelCallback` at offsets `+0x34`
and `+0x38`, the engine's window state machine started dispatching to
our slot every frame. `ConfirmCallback` and `CancelCallback` fired in a
repeating "Confirm, Cancel, Cancel" pattern at roughly 180 calls per
second, burning every frame in callback-stub returns. From the player's
perspective the game appeared frozen.

Aaron eventually completed the choice (`committed choice = Auto-drive`
at 21:01:03) because the TTS+keyboard path kept working alongside the
spam, but the engine-window approach was the lock cause.

The educated-guess template values (`mode1=0x05`, `state=0x07`) were
documented in v0.15.1 as needing v0.15.2 tuning via chase_diag's new
ASK snapshot logging. The lockup confirms we don't yet understand the
callback ABI either.

### The fix

Disable the proxy-slot allocation entirely in `OpenAsk()`. Pure
TTS+keyboard. Aaron is blind, so the engine-rendered window was a
sighted-player nicety, not a functional requirement. The TTS path was
already proven by the v0.15.1 BAT to work correctly even amid the
callback spam.

All proxy-window code is preserved verbatim:
- `FindFreeWindowSlot`
- `PopulateProxySlot`
- `ConfirmCallback` / `CancelCallback`
- `ReleaseProxySlot` (which already early-returns on `s_proxySlotIdx<0`)
- `EncodeChar` / `EncodeAskOptions`
- `TEMPLATE_MODE1` / `TEMPLATE_STATE` constants

v0.15.2 will re-enable this code once chase_diag's `OnAskOpcodeFired`
snapshot logging captures real engine-set template values from a
natural in-game ASK and we understand the callback ABI by observing
the engine's invocation pattern on legitimate dialogs.

### What v0.15.1.1 BAT will verify

1. **No more lockup.** When the chase ASK opens, the game continues
   running normally. Aaron hears the prompt, picks an option with
   Up/Down/Enter or 1/2, and the game proceeds.
2. **The rest of v0.15.1 still works.** ChaseDetector debounce, kani
   slot resolution (Backgrounds slot 4), `ChaseBattleFreeze` no-op of
   second-and-later kani BATTLE calls, and clean chase-end on
   transition to Lapin Beach.
3. **Optional:** With chase-diag ON (F12) before the chase, trigger
   any natural NPC ASK to confirm the snapshot logging fires and
   captures the engine's real `ff8_win_obj` template values for
   v0.15.2.

### Files changed

- `src/chase_ask_overlay.cpp` — the proxy-allocation block in
  `OpenAsk` is replaced by `s_proxySlotIdx = -1` plus an explanatory
  log line. Surrounded by a comment block explaining the rationale
  and pointing at v0.15.2.
- `src/ff8_accessibility.h` — version bumped, comment trail extended
  with the v0.15.1.1 narrative.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated to ready-to-BAT
  state.

## v0.15.1

First functional chase-mode build: manual mode caps Dollet/X-ATM092 chase battles at one per field

v0.15.0 was the diagnostic-only foundation. v0.15.1 is the first build that
actually changes chase behavior. Three new modules ship together as one
coordinated feature; v0.15.0's chase-diag is also fixed and extended.

### v0.15.0 BAT findings that shape this build

The v0.15.0 BAT (full chase playthrough — pre-tower battle, escape, all
five X-ATM092 chase battles, final Lapin Beach FMV) produced three findings
that invalidated parts of the original plan:

1. **Kani is in `pFieldStateBackgrounds`, not `pFieldStateOthers`.** Every
   chase field had `kani` in SYM but always BEFORE the v0.15.0 computed
   `othersStart = symNameCount - otherCount` index. v0.15.1 fixes this by
   routing through `FieldArchive::LoadJSMCounts` (already present in the
   code base) and tracking which array (Backgrounds or Others) holds kani
   in a new `s_kaniArrayKind` state.

2. **Var 84 / var 530 read 0x00 throughout the chase** at the documented
   savemap-on-disk addresses. Live engine state uses a different base.
   Until the live var area is found, **chase detection uses field-name
   matching, not var 530 polling.**

3. **`pCurrentFieldName` lags `pCurrentFieldId` by 2-5 seconds.** v0.15.1
   debounces field transitions: every fieldId change starts a 2-second
   timer; consumers see no field name until the timer expires.

### What ships

- **`chase_detector` (new)** — single source of truth for chase-scene state.
  Hard-coded chase-field name set (`domt4_1`, `domt5_1`, `domt3_2`,
  `domt2_1`, `doopen2a`, `dotown_3`, `dotown_2`, `dotown_1`). 2-second
  debounce on field name. Per-field battle counter via game-mode edge
  detection (`MODE != BATTLE` → `MODE == BATTLE` ⇒ +1). Resolves kani's
  runtime block address through `LoadJSMCounts` + SYM lookup, supporting
  both Backgrounds and Others arrays. Chase mode (`manual` / `auto`)
  persists to `ff8_accessibility.ini` under a new `[Chase]` section.

- **`chase_ask_overlay` (new)** — fires when Squall says
  `"Forget it!  Let's go!"` (the exact MES, with the double-space, confirmed
  by v0.15.0 dialog log capture; not `"Run!"` as the deep research
  suggested). Two-path hybrid:

  - *Engine-rendered window:* allocates a free slot in `pWindowsArray`,
    populates it with two options (`Auto-drive`, `Manual`), registers
    callbacks at the `+0x34` / `+0x38` offsets the engine reads.
    Template values (`mode1=0x05`, `state=0x07`) are educated-guess
    defaults; v0.15.2 will tune them with real values from the new ASK
    snapshot logging.
  - *TTS + keyboard:* always works, regardless of whether the engine
    renders the slot. ScreenReader speaks the prompt; up/down cycle the
    highlight (TTS announces new option each press); Enter commits.
    Number keys 1 and 2 also work as direct shortcuts. All keys are
    Alt-gated per the v0.14.105 convention.

  Auto-drive isn't shipped in v0.15.1 — selecting it announces
  `"Auto-drive is not yet implemented. Falling back to manual."` and
  stores `manual` to the INI. The selection slot is wired so v0.15.2+
  auto-drive arrives as a behavior change without overlay rework.

- **`chase_battle_freeze` (new)** — MinHook detour on
  `pExecuteOpcodeTable[0x69]` (opcode_battle). Gates:

      mode == manual
   && IsInChaseField()
   && IsKaniEntityPtr(entityPtr)
   && GetCurrentFieldBattleCount() >= 1
   ⇒ return JSM-VM advance code 3 without calling original

  All other cases pass through to `s_origBattle`. The kani check is
  pointer equality between the hook's `entityPtr` parameter and
  `ChaseDetector::GetKaniEntityPtr()` (which returns
  `backgroundsBase + 0x1B4 * kaniSlot` or `othersBase + 0x264 * kaniSlot`
  per `s_kaniArrayKind`). Result: the chase is capped at one battle per
  field, the post-knockdown WAIT timer is sidestepped entirely.

  Risk noted: if BATTLE pushes a return value to its script-VM stack
  (we haven't disassembled it to confirm), post-BATTLE script paths
  might misbehave. The v0.15.1 BAT will surface this if it happens.

### chase_diag fixes and extensions

- `FindKaniSlot` (broken — assumed Others-only, computed wrong
  `othersStart`) replaced with `FindKaniLocation` using `LoadJSMCounts`.
- New `s_kaniArrayKind` state; `PollKani` now picks the correct stride
  (0x1B4 Backgrounds, 0x264 Others) per kind.
- New `ChaseDiag::OnAskOpcodeFired(opcodeLabel)` — when chase-diag is
  enabled (F12), dumps all 8 `ff8_win_obj` slots in detail (state,
  mode1, open_close_transition, win_id, firstQ/lastQ/curQ, field30,
  callbacks, text pointers) plus a 60-byte hex dump per slot. Called
  from `field_dialog.cpp`'s `Hook_opcode_ask` / `Hook_opcode_aask`.
  Workflow: turn chase-diag ON before the chase, trigger any natural
  in-game ASK (NPC question, vendor `buy / cancel`) to capture template
  values, then enter the chase. v0.15.2 will hardcode the captured
  values into `TEMPLATE_MODE1` / `TEMPLATE_STATE`.
- No-op when chase-diag is disabled — doesn't spam the field log on
  normal NPC dialogs.

### What the v0.15.1 BAT will verify

1. Chase-field name set is complete (any chase field not in our
   hardcoded list?).
2. Field-name debounce dodges the v0.15.0 stale-name false positives.
3. The chase ASK opens at chase entry (Squall's MES detected), NVDA
   reads the prompt and options, selection routes correctly, INI
   persists.
4. (Best-effort) The engine renders our allocated window slot. If a
   natural ASK fires before chase, captured template values yield a
   clean visible ASK; otherwise the TTS + keyboard fallback handles
   the choice and v0.15.2 tunes the engine path.
5. `chase_battle_freeze` correctly no-ops the second-and-later kani
   BATTLE call per chase field. Existing v0.15.0 chase-diag still
   captures cleanly with the kani slot fix.
6. Chase still ends correctly (Lapin Beach FMV plays) when the kani
   battles after the first are no-op'd.

## v0.15.0

Dollet / X-ATM092 chase scene work begins: F12-toggleable diagnostic logger

This ships only the diagnostic infrastructure. The actual chase accessibility
feature (in-engine ASK overlay + manual-mode `opcode_battle` no-op +
auto-drive option) lands in v0.15.1+ once we have the playthrough data.

The deep research on the chase (saved at `Plan & Research Documents/X-ATM092
chase accessibility deep research results.md`) confirmed five things that
shape the implementation:

1. **Var 530 at absolute address 0x01CFEB7E** is the Dollet state bitmap.
   Bit 0x10 ("xatm first knock out") flips on at the chase start. We don't
   need broad memory monitoring — one byte gives us the chase-active signal.
2. **Var 84 at 0x01CFE9C0** is the place ID. Dollet places are 99 (Comm
   Tower), 100 (Mountain Hideout), 93 (Town Square), 94 (Lapin Beach).
3. **X-ATM092's field entity is named `kani`** in SYM data (Japanese for
   crab — the boss is a giant crab-shaped war machine). The mod can find
   its slot per field by string match against the SYM names already loaded
   by the field archive.
4. **`opcode_battle` (0x69)** is the recommended freeze hook for manual
   mode: when calling entity is kani in a chase field AND we've already had
   one battle this field, no-op the BATTLE call. This caps the chase at
   one battle per field and entirely sidesteps the post-knockdown WAIT
   timer. Modifying var 530 directly is unsafe (bits not fully enumerated
   in public docs).
5. **Engine `opcode_ask` is fragile to call from outside the script VM.**
   Use a proxy-window pattern via the existing `show_dialog` hook surface
   instead: allocate our own ff8_win_obj slot, populate it, register
   confirm/cancel callbacks at the +0x34 / +0x38 offsets the engine
   already exposes. From the player's perspective it's a real in-engine
   ASK dialog; from the engine's perspective it's a real win_obj in the
   active windows array; we just don't go through the script-VM ASK
   handler.

v0.15.0 instruments the engine state we still need to verify empirically
before building the feature: chase field short names in order, kani's
slot index per chase field, the rise-timer duration, the chase formation
ID(s), and the full var 530 bit-transition history.

New module `src/chase_diag.{h,cpp}` adds an F12-toggleable diagnostic.
When toggled on (TTS announces "Chase diagnostic enabled"), six log
streams are emitted to the appropriate domain logs:

- `[CHASE-DIAG-FIELD]` on every field transition — field ID, short name
  from `pCurrentFieldName`, place ID (var 84), var 530 current value,
  full per-entity dump (model, flags, position, exec flags), full SYM
  names list with the kani slot in the runtime entity array highlighted
  if found.
- `[CHASE-DIAG-VAR530]` per-frame poll at 0x01CFEB7E. Logs only on
  change. The bit deltas are decoded into human-readable phrases
  ("+0x10 xatm first knock out", etc.) per the Qhimm wiki documentation.
- `[CHASE-DIAG-PLACE]` per-frame poll of var 84 at 0x01CFE9C0. Logs only
  on change with the place name decoded for Dollet places 92–100.
- `[CHASE-DIAG-KANI]` when kani is detected in the current field via
  SYM name match. Logs whenever its position, walkmesh triangle, or
  push/talk/through flags change. Timestamps on these log lines let us
  measure the rise-timer duration empirically (collapse → resume).
- `[CHASE-DIAG-FRAME]` heartbeat every 5 seconds with field name,
  player position, var 530, place ID, game mode, and current kani slot.

F12 reservation rule honored: searched all source files for `VK_F12` /
`0x7B` before adding the handler. None found — F12 has been free since
v0.14.75 promoted the screenshot binding to F11 and stripped the F12
code. The new F12 handler is alt-gated alongside every other F-key
handler per the v0.14.105 lesson, so Alt+F4 doesn't accidentally fire
the toggle.

The module is purely additive: no behavior change when the diagnostic is
off (the default), and no inter-module hook insertions when on. Game
state is read via FF8Addresses convenience accessors (`pCurrentFieldId`,
`pCurrentFieldName`, `pFieldStateOthers`, `pFieldStateOtherCount`) plus
three confirmed absolute addresses for the savemap variables. Memory
access is SEH-guarded throughout because the addresses point into the
FF8 process's address space and may be transiently invalid during
mode/field transitions.

The v0.14.108 module set is unchanged in v0.15.0 — the follower
behavioral-fingerprint filter, the persistence layer, the screenshot
feature, the existing battle/dialog/scan/world-map/menu TTS modules all
ship as-is.

FILES: `src/chase_diag.h` (new), `src/chase_diag.cpp` (new),
`src/dinput8.cpp` (Initialize/Update/Shutdown calls + F12 handler +
alt-gating + comment block update), `src/deploy.bat` (chase_diag.cpp
added to compile list), `src/ff8_accessibility.h` (version bump).

QUEUED with this version: the previous session's push-utility hardening
(`Utilities/push_to_github.ps1` Step 7b duplicate-version refusal,
phase-by-phase logging to `Logs/push_diagnostic.log`, non-modal progress
dialog, success-message enrichment with HEAD shortstat) plus the
DEVNOTES and NEXT_SESSION_PROMPT updates from v0.14.108. These ride
along because v0.14.108 push happened with the changes uncommitted; the
utility's new Step 7b would refuse a same-version repush, so the
bundling is intentional.

BAT plan: load the pre-tower-drop save. Press F12 (TTS: "Chase
diagnostic enabled"). Walk through the X-ATM092 drop cutscene. Engage
the first battle, damage to collapse, escape. Walk through the
post-battle field, trigger Squall's "Run!". Enter the first chase
field. Stand still — let X-ATM092 catch up. Engage the chase battle,
escape. **Stand still on the same field through one or two respawn
cycles** — this is the critical capture. Press F12 to disable. Send
the four logs (`ff8_field.log`, `ff8_battle.log`, `ff8_dialog.log`,
`ff8_mod.log`).

## v0.14.108

Filter party members from the field entity catalog (take 2)

When the player has 2+ active party members, the followers (Zell, Quistis,
etc.) appear in the field navigation catalog because they have throughonoff
set (you walk through them so they don't block) but no talkonoff/pushonoff.
The existing classification chain falls through to ENT_EXIT, which is
wrong — they're not exits, they're invisible-to-interaction party
members. Pressing X on them does nothing. They're noise that pads out
the F9 cycle list.

v0.14.107 attempted a savemap-aware filter that cross-referenced the
entity's model ID against the active party formation at savemap+0xAF0
(0x01CFE74C). That assumed canonical model→charId mapping (0=Squall,
1=Zell, 2=Irvine, etc.). BAT on bggate_1 with a Squall + Zell + Selphie
party (formation [1,0,5,255]) revealed the assumption is wrong: the
followers showed up as model 2 and model 4 — canonical Irvine and Rinoa
IDs, neither in this party. The savemap cross-reference correctly
returned false, the filter no-op'd, and the followers stayed in the
catalog. The engine reuses model slots per-field; the canonical mapping
is an averaged truth that doesn't hold per-field.

v0.14.108 replaces the model→charId filter with a behavioral fingerprint
that catches followers regardless of which model slot the field assigned
them:

  modelId in [0, 9]   visible character (party-character model range)
  throughonoff  > 0   player walks through them
  talkonoff    == 0   not talkable
  pushonoff    == 0   no collision

The modelId < 10 guard excludes save points (model 24) and other non-
character interactive objects with throughonoff. Save point detection
runs later in RefreshCatalog() via the modelId == 24 check, so save
points qualify and reach the JSM-based reclassification regardless.

This filter also catches non-interactive cutscene characters that walk
through scenes — actually correct behavior since they're not navigation
targets either.

Known trade-offs:
- A real NPC whose script sets throughonoff before TALKRADIUS sets
  talkonoff would be transiently filtered. The catalog refreshes on
  every F9 press, so the next press picks them up. Acceptable race.
- Followers using model 10+ (generic NPC range) wouldn't be caught.
  Empirically followers seem to land in 0–9, but if a future BAT shows
  otherwise this can be relaxed.

The v0.14.107 helpers (IsCharacterInActiveParty, ModelIdToCharId) and
the per-field [party-state] formation diagnostic stay in place. They're
harmless, well-documented, and may be useful for future party-aware
features. Only the filter call site changes.

Files: src/field_nav_catalog.inl (replace v0.14.107 filter block with
the behavioral fingerprint, ~12 lines), src/ff8_accessibility.h
(version bump). v0.14.107 helpers in field_nav_helpers.inl and the
[party-state] diagnostic UNCHANGED.

## v0.14.107

Filter party members from the field entity catalog

When the player has 2+ active party members, the followers (Zell, Quistis,
etc.) appear in the field navigation catalog as if they were interactable
NPCs because they have talkonoff set. Pressing X on them does nothing —
they're noise that pads out the F9 cycle list. v0.14.107 filters them out
using a savemap-aware check.

Approach: in RefreshCatalog(), for each non-player entity, if its model
ID maps to a character ID currently in the active party formation, skip
it. The model→charId map is straightforward (model 0=Squall, 1=Zell,
2=Irvine, 3=Quistis, 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea, with model 8
Quistis-uniform also mapping to charId 3); the formation array lives at
savemap+0xAF0 (absolute 0x01CFE74C), four bytes, each a charId 0–7 or
0xFF for empty. Same address used by Junction TTS and save block content
TTS — confirmed reliable across many BATs.

Why cross-reference savemap formation rather than filtering on model ID
alone: the engine reuses model slots across scenes. A model 7 in early-
game might be a generic background character, not Edea. The April 2026
entity-classification thread confirmed this empirically. Only filter
when the model corresponds to a character actually in the active party
right now.

Edge cases handled by design:
- Solo Squall (Fire Cavern, formation [0xFF, 0x00, 0xFF, 0xFF]) — Squall
  is the player and excluded by the `i != s_playerEntityIdx` guard, so
  nothing filters.
- Pre-recruitment cutscene NPCs sharing a party-character model — the
  formation byte for that charId is still 0xFF, IsCharacterInActiveParty
  returns false, filter no-ops, NPC stays in catalog.
- Quistis as classroom instructor (model 8) — only filtered when she's
  in the active party; otherwise kept.

Adds two helpers in field_nav_helpers.inl: IsCharacterInActiveParty
(charId) reads the 4-byte formation array under SEH; ModelIdToCharId
(modelId) maps 0–7 directly and 8→3 with the Quistis-uniform special
case. New per-field [party-state] diagnostic logs the formation array
contents once per field load; new [party-filter] line per filtered
entity for verification.

Files: src/field_nav_helpers.inl (two new helpers near top, ~50 lines
including documentation), src/field_nav_catalog.inl (filter check before
classification + one-shot per-field diagnostic, ~50 lines), src/field_
navigation.cpp (1 line: new s_partyDiagDumped flag declaration), src/
field_nav_fieldscripts.inl (1 line: reset s_partyDiagDumped = false on
field load), src/ff8_accessibility.h (version bump).

## v0.14.106

v0.14.105 + v0.14.106: Fix speech rate persistence + INI template

v0.14.105: Speech rate (and other accessibility settings) were creeping up
by 1 each session because Alt+F4 close was firing the F4 IncreaseRate()
handler on the way out. Every F-key accessibility handler (F1-F8, F11) is
now gated on !alt (GetAsyncKeyState(VK_MENU)) so Alt+combos no-op. Also
added a one-shot LogActualSAPIState diagnostic to confirm the fix; BAT
verified persistence is sound and SAPI preserves rate/volume across voice
changes.

v0.14.106: Strip the diagnostic harness now that the fix is verified. Add
a human-readable commented template to ff8_accessibility.ini explaining
each setting, its range, and the in-game shortcut. Existing INIs auto-
upgrade on next launch (preserving all values). New helpers in config.cpp:
HasTemplateMarker(), EnsureTemplate().

