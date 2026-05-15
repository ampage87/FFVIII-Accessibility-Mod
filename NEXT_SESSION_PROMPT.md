# Next Session Prompt: v0.15.9.11.3.6 BAT-SUCCESS — push pending, chase scene DONE

## Where we are

**Chase scene complete.** All four chase scene items closed. v0.15.9.11.3.6 BAT'd successfully on 2026-05-14 — Aaron mashed arrow keys through the chase fields and could not interrupt the auto-drive. Full route announced naturally in the mod log: MH-5 → MH-6 → MH-3 → MH-7 → MH-1 bridge → Town Square 1 → **Town Square 5 (`doopen2a`, the field that had defeated every prior v0.15.9.11.3.x attempt)** → Town Square 10 → disc00_06h FMV → Town Square 8 → Town Square 6 → disc00_07h Lapin Beach chase-climax FMV played all 8 cues across 74 seconds to natural completion. The 11→0 catch reduction first achieved hands-off in v0.15.9.8.3 now extends to arrow-mashing gameplay.

HEAD on GitHub is still **v0.15.9.10**. Local tree is at **v0.15.9.11.3.6**, nine unpushed versions ahead.

## Start of session: check whether the push happened

When this session begins, first call `github:list_commits` against `ampage87/FFVIII-Accessibility-Mod` and check the most recent commit message. If the top commit is the v0.15.9.11.3.6 squash, the push is done — skip to "Post-chase backlog" below. If HEAD is still v0.15.9.10, gently remind Aaron the chase-scene squash is pending — he runs `Utilities/push_to_github.vbs` and it squashes v0.15.9.10 → v0.15.9.11.3.6 into one commit using the v0.15.9.11.3.6 CHANGELOG entry as the message. Claude NEVER pushes.

## What v0.15.9.11.3.6 shipped (for reference)

New module `src/chase_wndproc.cpp` — fourth coordinated hook joining (1) DirectInputCreateA chain, (2) GetDeviceState vtable detour, (3) GetAsyncKeyState MinHook. `EnsureInstalled()` enumerates top-level visible windows owned by FF8_EN.exe (`EnumWindows` + PID filter) and `SetWindowLongPtrW(GWLP_WNDPROC)` subclasses each. The subclass runs on the message-pump thread (same thread as the chase script's catch evaluator). When `ChaseKeyboard::IsActive()` AND `msg ∈ {WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP}` AND `wParam ∈ {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}`: return 0 without forwarding. FF8's WndProc and its `[+0xb48]` dispatch never see the message. Everything else forwards via `CallWindowProcW/A` so non-chase behavior is bit-for-bit identical.

Lazy install via `ChaseKeyboard::Activate() → ChaseWndProc::EnsureInstalled()`, called BEFORE `s_active = true` to eliminate the one-frame race window. Permanent install — never uninstalled mid-gameplay. Install line logs via `Log::Field` to `ff8_field.log`.

Why this worked when .11.3.1–.5 didn't: the disassembly walk of FF8's WndProc at `0x0040AC5B` revealed a `[+0xb48]` per-message-handler dispatch table that arrow `WM_KEY*` messages route through, untouched by hooks 1–3. Subclassing the WndProc drops those messages before that dispatch runs.

## Post-chase backlog (in rough priority order)

Pick one with Aaron. None are urgent; all are tidy-up:

1. **Cleanup vestigial `WALK_REPRESS_PERIOD` state** in `field_nav_directiondrive.inl` — constants + counters still present from v0.15.9.7.x but unreferenced. Small, mechanical, zero risk.
2. **BridgeDiag verbosity trim** — 10Hz per-sample BridgeDance + all-slots dump on `domt1_1` is noise now that the bridge dance is proven (v0.15.9.8.3). Trim to transition-only events.
3. **`deploy.bat` "Version: SINGLE-PRONGED" regex** — cosmetic regression from v0.15.9.3. The deploy log prints `Version: SINGLE-PRONGED` instead of the actual version string. Hunt the regex in `src/deploy.bat`.
4. **X-ATM092 battle-name fix** — standalone; battle TTS announces the wrong name for the X-ATM092 encounter.
5. **Generalized countdown-timer hook** — standalone; the Dollet 30-min countdown is currently TTS'd via a chase-specific path. Generalize for future timers.
6. **Remove party members from field entity catalog** — Squall/Zell/Selphie/etc. appear in the field entity catalog as targetable entities, which they shouldn't be. Filter them out.

### Deferred (don't pick from these without explicit Aaron direction)

- SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` is wrong section size)
- Walk-and-talk dialog gap (hardcoded engine path)
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) + planner-fallback (#29)
- chase_diag::OnAskOpcodeFired snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression (fix in DeactivateFreeze before clearing agent state)

## Hard constraints

- **Do NOT revert the AUTO `[CBF]` battle-suppressor cap to 0.** Aaron's 2026-05-13 directive: the fix was the input layer, not the band-aid. Cap stays `INT_MAX`. v0.15.9.11.3.6 BAT vindicates the call.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.vbs`.** Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`. Hangs infirmary scene.

## Session ritual reminder

Read `DEVNOTES.md` and this file at session start. Update both at every version bump AND after every BAT result. Every Claude response starts with `## Claude Says`.
