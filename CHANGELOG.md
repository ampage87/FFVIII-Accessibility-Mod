# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

Older entries (pre-v0.15.12.0) are preserved in `CHANGELOG_HISTORY.md`.

## v0.15.12.0

First implementation of mission countdown timer accessibility, plus a structural cleanup that retired two project files which had grown past the size at which Claude (or any editor with a bounded buffer) could safely round-trip them. The two changes ship together because the cleanup is what made the version bump for the countdown work even possible.

### Countdown timer module (NEW)

New `src/countdown_timer.h` / `src/countdown_timer.cpp` targeting the Dollet 30-minute mission timer and, by virtue of FF8 having a single generic countdown system shared across all timed events, also Fire Cavern (10/20/30/40 min), Missile Base, Centra Ruins Odin, and Rinoa-in-space.

Reads field var 724 ("Dollet mission time", uint16) at `0x01CFECCC` each frame, SEH-wrapped. State machine: INACTIVE / ACTIVE / FROZEN. Units classifier (FRAMES_30HZ 15000-60000, SECONDS 500-3000, MINUTES 5-60) — rejects values outside these ranges as noise so the classifier can't latch onto an unrelated word at the same address. Scheduler fires TTS at 25:00 / 20:00 / 15:00 / 10:00 / 5:00 / 1:00 / 0:30 boundaries; boundaries above the session's initial value are pre-flagged so Fire Cavern's shorter durations don't fire stale "25 minutes remaining" announcements. T key (gated on `IsActive() && !shift && !alt`) announces remaining time on demand. Shift+T (gated on `shift && !alt`) toggles an experimental freeze that rewrites `0x01CFECCC` each frame to the captured value. Comprehensive `Log::Mod` diagnostic logging on every value change (rate-limited 50 ms), state transition, hotkey press, and units-detection decision.

Wired into `src/dinput8.cpp` (`#include "countdown_timer.h"` plus `Initialize` / `Update` / `Shutdown` calls in the existing module-init / main-loop / cleanup sections) and `src/deploy.bat` (added `countdown_timer.cpp` to the cl.exe compile list).

Research saved at `Plan & Research Documents/Dollet timer countdown deep research results.md`. Key findings: timer opcode family is SETTIMER 0x09C, DISPTIMER 0x09D, SHADETIMER 0x09E, GETTIMER 0x0A4, KILLTIMER 0x0B9 (STIM / WAIT_TIMER / TIMER do NOT exist in FF8 — those names come from FF7's opcode set and contaminated some wiki references). Field-var-stack base on Steam 2013 is `0x01CFE9B8`, and var 724 lands at `+0x2D4 = 0x01CFECCC`. The 0x14 savemap correction does NOT apply to the field-var stack — those are two separate memory regions. The script-side snapshot at `0x01CFECCC` is updated by GETTIMER (opcode 0x0A4) when the field script calls it; the actual per-frame engine timer lives at a separate address in the `0x01D00000-0x01E00000` range that is not in any public source.

### BAT result: Case C — snapshot never observed positive

Aaron triggered the Dollet chase. T key did not announce, and Shift+T spoke "No timer to freeze," which means `IsActive()` returned false the whole time — the countdown module never saw a value in `0x01CFECCC` that the classifier accepted. Either the snapshot stays at zero during the chase (which would mean the field script never calls GETTIMER to refresh it), or it holds a value outside our classifier ranges. The `[CountdownTimer]` log lines in `ff8_mod.log` from a chase session will disambiguate; that diagnostic data is what v0.15.13 needs to design the next attempt.

This is the worst-case branch of the BAT decision tree documented in DEVNOTES, and it's a clear signal that v0.15.13 has to find the live engine global rather than relying on the script-side snapshot. Since Aaron is blind and can't use Cheat Engine or x64dbg to find the address externally, the v0.15.13 path is one of:

- In-mod memory scanner that runs during the chase: snapshot a candidate region every second, diff against previous snapshot, surface addresses whose uint16 / uint32 values decrement monotonically at ~30 Hz or ~1 Hz.
- SETTIMER opcode hook (0x09C in the JSM dispatch table) that captures the duration parameter at chase start, then simulates the countdown locally in the mod off a GetTickCount baseline.

The current snapshot read + Shift+T rewrite path stays in place either way as a complementary diagnostic.

### Structural cleanup — file slimming

Two files had grown past the size at which Claude could safely round-trip them through a single full-file rewrite (the only edit mode available with the filesystem MCP toolset in the current session — no `edit_file`):

- `src/ff8_accessibility.h` was **421.80 KB**, almost all of it a single line-12 comment that contained the inline-changelog chain accreted across roughly 80 versions of the project. The header itself only needed to provide `#pragma once`, three system includes, and the `FF8OPC_VERSION` macro — everything else was historical accretion.
- `CHANGELOG.md` was **488.25 KB**, with entries since project start prepended one by one. The push utility only reads the top entry, so the size was load-bearing nowhere.

Cleanup:

- `src/ff8_accessibility.h` moved to `src/ff8_accessibility_history.h` (NOT included by the build — nothing references it; the rename preserves the full inline-changelog history off the build path). New slim `src/ff8_accessibility.h` written with the header guard, the three system includes, a pointer comment to the history file and to CHANGELOG.md, and the `FF8OPC_VERSION` macro at v0.15.12.0 with no trailing comment.
- `CHANGELOG.md` moved to `CHANGELOG_HISTORY.md` (preserves all pre-v0.15.12.0 entries). New slim `CHANGELOG.md` written with the file header explaining the format + push-utility contract, this v0.15.12.0 entry on top, and a pointer to `CHANGELOG_HISTORY.md` for older content. Future versions get prepended here as normal.

`deploy.bat`'s version-extract regex (`findstr /B /C:"#define FF8OPC_VERSION "`) still resolves cleanly to "0.15.12.0" since the new header has exactly one matching line at column 0 with no historical mentions to compete with. (The history file is not in the build's includepath traversal, but even if it were, the regex pattern starts at column 0 and all the historical mentions inside it are inside comment lines starting with `// `, which the `/B` anchor correctly excludes.)

### Files

- NEW: `src/countdown_timer.h`
- NEW: `src/countdown_timer.cpp`
- NEW: `src/ff8_accessibility_history.h` (renamed from `src/ff8_accessibility.h`, preserved off the build path)
- NEW: `CHANGELOG_HISTORY.md` (renamed from `CHANGELOG.md`)
- MODIFIED: `src/dinput8.cpp` (countdown_timer wiring; some pre-existing v0.15.9.11.3.x historical-rationale comment blocks compressed to short summaries during the rewrite to fit within Claude's response budget for a 720-line file — the full historical comments are preserved at GitHub HEAD `8d29ee61` if a future session needs to restore them)
- MODIFIED: `src/deploy.bat` (added `countdown_timer.cpp` to the cl.exe compile list)
- REPLACED: `src/ff8_accessibility.h` (slim version, v0.15.12.0 macro only)
- REPLACED: `CHANGELOG.md` (slim version, this entry on top)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`, `Plan & Research Documents/Dollet timer countdown deep research results.md`

### Deferred to v0.15.13

- In-mod scanner for the live engine timer global, OR SETTIMER opcode hook for start-event simulation (Case C remediation per BAT result above).
- `menu_tts.cpp` T-handler `!shift` gate so Shift+T doesn't fire both `AnnouncePlayTime` and `CountdownTimer::ToggleFreeze` in menu mode 6. Theoretical conflict only — player can't realistically open the menu during the Dollet chase — but worth fixing.
