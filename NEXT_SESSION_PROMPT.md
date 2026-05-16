# Next Session Prompt: v0.15.12.0 ready to push; then v0.15.13 = countdown timer Case C remediation

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and this file before any work, per the session ritual rule.

## Where we are

**HEAD (GitHub):** v0.15.11.0, commit `8d29ee61`. **Local tree:** v0.15.12.0 fully assembled and ready for `Utilities/push_to_github.ps1`. Macro at "0.15.12.0", top CHANGELOG entry at `## v0.15.12.0`, push-utility version-match check will pass.

If Aaron has already pushed v0.15.12.0 by the start of the next session, the GitHub HEAD will be v0.15.12.0 — verify with `git log -1` or by reading the top heading in `CHANGELOG.md`.

## What v0.15.12.0 contains

Two changes shipped together:

**1. Countdown timer module** (`src/countdown_timer.{h,cpp}`, wired into `src/dinput8.cpp` and `src/deploy.bat`). Reads field var 724 at `0x01CFECCC`, state machine, scheduler at 25/20/15/10/5:00 + 1:00 + 0:30, T announces, Shift+T toggles experimental freeze. Heavy `[CountdownTimer]` diagnostic logging.

**2. Structural cleanup.** Two project files that had grown past Claude's full-rewrite capacity are slimmed:
- `src/ff8_accessibility.h`: 421.80 KB → 1.17 KB (history preserved at `src/ff8_accessibility_history.h`, NOT in build)
- `CHANGELOG.md`: 488.25 KB → 7.93 KB (history preserved at `CHANGELOG_HISTORY.md`)

Going forward, version bumps are one-line edits on a 1 KB header. Inline-changelog accretion is retired as a pattern.

## v0.15.12.0 BAT result (already done — confirm before working on v0.15.13)

**Case C — snapshot never observed positive.** Aaron triggered the Dollet chase. T key did not announce; Shift+T spoke "No timer to freeze," meaning `IsActive()` returned false the whole time. The classifier never saw a value at `0x01CFECCC` it would accept.

The cleanup didn't change the countdown_timer logic — only file structure. A re-BAT with the v0.15.12.0 build (post-cleanup) will produce the same `[CountdownTimer]` diagnostic logs against the same Dollet chase. Aaron may or may not have re-BAT'd before starting the next session; check `Logs/ff8_mod.log` for recent `[CountdownTimer]` lines either way — they're the data v0.15.13 needs.

Specifically look for:
- `[CountdownTimer] Initialize: ...` — confirms module loaded
- `[CountdownTimer] var724 raw=N (prev=M) state=S tickMs=T` — every value observation, rate-limited 50 ms
- `[CountdownTimer] ENTER ACTIVE: rawValue=N units=X initialSec=Y` — only fires if classifier accepts a value

If only the Initialize line appears across an entire chase and no `var724 raw=` lines change away from the initial value (or they all show `raw=0`), the snapshot really does stay at zero. If the `raw=` lines show nonzero values but no `ENTER ACTIVE` fires, the classifier needs a wider range. If `ENTER ACTIVE` fires but T still says no timer, there's a bug — check `IsActive()` logic.

## v0.15.13 pick: Case C remediation

Two paths. Aaron picks, or session works through both.

### Path A: In-mod memory scanner

Add a scanner that runs during the chase (gated on `pCurrentFieldId` matching one of the Dollet chase fields, or on a hotkey for general use). Each second, snapshot a candidate region (the research suggests `0x01D00000-0x01E00000` for the live engine timer global) into a buffer; diff against previous snapshot; surface uint16 and uint32 addresses whose values decrement monotonically at rates consistent with frames@30Hz (~30/sec) or seconds (~1/sec). Output candidates to `ff8_mod.log` with their addresses, values over time, and delta rates. Aaron reads the log, identifies the address by elimination (filtering out play-time counter, audio sync counter, etc.), and v0.15.14 hardcodes that address.

Cost analysis: 1 MB region scanned every second is roughly 500k uint16 reads. At the existing 60 Hz AccessibilityThread rate that's amortized to ~8k reads/frame — negligible. SEH-wrap each read because some pages in that range may not be mapped. Skip pages that fault.

Risk: low. Pure-additive diagnostic. If it finds the address, great. If it doesn't, we know to look elsewhere.

### Path B: SETTIMER opcode hook

FF8's JSM opcode dispatch is a function-pointer table indexed by opcode number. The research notes: "Locate the table by following any reference to the PSHM_W (0x00C) handler in your disassembly; the SETTIMER handler is at table-base + 0x09C × 4." We already have `opcode_pshm_w` from FFNx externals (referenced extensively in `field_dialog.cpp` for our existing dialog hooks). From that we can compute the dispatch table base, then hook the SETTIMER handler at slot 0x9C.

When SETTIMER fires, the handler runs with the duration parameter (in seconds per the research). We capture: duration + start tick (GetTickCount). The mod's local simulation tracks remaining = duration - (now - start). Announcements fire from the local sim, not from memory polling.

KILLTIMER hook (slot 0xB9) to mark the timer ended.

This doesn't give us freeze (still needs the engine decrement instruction) but it gives reliable read-and-announce that doesn't depend on snapshot updates.

Risk: medium — opcode dispatch hooking is well-understood in this codebase (it's how `field_dialog.cpp` works) but the specific table base for the timer family hasn't been computed before.

### Recommendation

**Do Path A first.** The scanner is a one-time investment that pays off for every future memory-address research — if Aaron ever needs to find another engine global without external tools (he can't use CE / x64dbg, he's blind), the scanner module exists. It's also useful for v0.15.12.0's own diagnostic question: even if Path B becomes the read path, knowing where the live engine global is unlocks the freeze story later.

If the scanner finds a clear candidate, ship that as v0.15.13 read path. Path B remains a fallback for v0.15.14 if the scanner output is ambiguous.

## Smaller deferred items

- **`menu_tts.cpp` T-handler `!shift` gate.** One-line fix in `menu_tts.cpp::Update()`: change `if (GetAsyncKeyState('T') & 1)` to `if ((GetAsyncKeyState('T') & 1) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000))`. Theoretical conflict only.

- **`src/dinput8.cpp` v0.15.9.11.3.x comment restoration.** Some pre-existing multi-paragraph rationale blocks were compressed during the v0.15.12.0 wiring rewrite to fit Claude's response budget for the 720-line file. Code paths are intact and behaviorally identical. Full original text preserved at GitHub HEAD `8d29ee61` — restore if Aaron wants.

## Hard constraints (unchanged)

- **Do NOT revert AUTO `[CBF]` battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`.** Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)` to prevent Alt+Fx interception.
- **F12 reserved** for per-session diagnostics — search source for existing F12 refs and REMOVE old code before re-binding. (Currently DialogInject Phase1/2 own F12 / Shift+F12.)
- **Check file sizes** via `filesystem:list_directory_with_sizes` BEFORE attempting a full rewrite of any file. Past ~50 KB, the `move_file` to `_history` + slim rewrite pattern (used for v0.15.12.0's cleanup) is the safer path.
- **No more inline-changelog accretion.** v0.15.12.0 retired the line-12 chain in `ff8_accessibility.h`. Canonical changelog is only `CHANGELOG.md` from now on.

## Carry-forward lessons from v0.15.12.0

- **File-size discipline is a build-time tooling concern, not just a runtime concern.** A 421 KB header file isn't a problem for the compiler but is a problem for any tool with a bounded buffer. The inline-changelog pattern was a good idea at 5 entries; at 80+ entries it became a liability that blocked version bumps. The slim header + `_history` companion is the new pattern.
- **`move_file` + `write_file` is the workaround for "no `edit_file` available + file too big to round-trip".** When the only edit mode is full overwrite, the cost of preserving large existing content is proportional to file size. Moving the old file aside (renaming) is constant-cost regardless of size. The new file is written fresh with whatever's actually needed. The trade-off is that the old content stops being live — for code, that means it needs to not be in the build; for narrative content like changelogs, it means the canonical reader (push utility, here) only sees the new file.
- **Heavy diagnostic logging on first-touch modules earns its keep on the first BAT failure.** v0.15.12.0's countdown_timer logs every value observation, state transition, hotkey, and units decision. Aaron's BAT reported "T didn't announce, Shift+T said no timer" — that's a clear Case C signal, but the actual diagnostic data lives in `[CountdownTimer]` log lines that tell us exactly what the snapshot held and when. Without the logging, "didn't fire" is ambiguous; with it, v0.15.13's design choice is data-driven.

## Session ritual reminder

Read `DEVNOTES.md` and this file at session start. Update both at every version bump AND after every BAT result. Every Claude response starts with `## Claude Says`.
