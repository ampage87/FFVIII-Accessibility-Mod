# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

Older entries (pre-v0.15.12.0) are preserved in `CHANGELOG_HISTORY.md`.

## v0.15.13.2

Live timer reads now point at the address the v0.15.13.1 scanner discovered. The scanner has served its purpose and is disabled in this build, freeing ~6 MB of static memory and the per-frame snapshot/analyze CPU cost.

### v0.15.13.1 BAT — scanner found the timer

Cycle 11 of the v0.15.13.1 BAT (21:50:40) surfaced a single, unmistakable candidate in the R1 u32 list:

```
[CountdownScan] R1 u32 #0 addr=0x01CFE92C u32 cur=1711 old=1715 dec=4 rate=1.00/s
```

Perfect 1.00/second monotonic decrement, value 1711 = 28 minutes 31 seconds remaining — squarely consistent with a Dollet chase save loaded mid-run (chase starts at 1800 sec; 89 seconds elapsed by cycle 11). The address `0x01CFE92C` is `0x8C` bytes BELOW the game-object struct base `0x01CFE9B8`, in an adjacent engine-globals allocation. That's why v0.15.13.0's old Region 1 (8 KB starting AT the game object) missed it — the v0.15.13.1 expansion to `0x01CD0000 + 192 KB` was what surfaced it.

The candidate only appeared in cycle 11 because the top-16 cap pushed it out of most other cycles where 16+ faster-changing candidates ranked higher (entity-state churn at rate ~25/s during gameplay dominated the rankings). Cycle 11 was unusually calm — only 1 R1 u32 entry made it through the value-range and rate filters — letting our slow timer (dec=4, rate=1.00/s) take that lone slot.

This is the kind of find that justifies wider scan regions and accepting more noise in the ranked output: a low-rate, single-instance, perfectly-monotonic candidate in an otherwise quiet region is exactly the signature of a real countdown timer.

### Changes in `src/countdown_timer.cpp`

- New constant `LIVE_TIMER_ADDR = 0x01CFE92C` (the discovered address). Read as uint16 — value fits comfortably in 16 bits since the max representable timer is 65535 seconds = ~18 hours, well above any chase duration, and reading uint16 means Shift+T freeze writes won't clobber any unknown high-byte engine state.
- Old `TIMER_VAR724_ADDR = 0x01CFEC8C` renamed to `VAR724_SNAPSHOT_ADDR`, kept as a documented constant but no longer read. The script-side snapshot stays at 0 during the chase because the chase script doesn't call GETTIMER routinely; only `LIVE_TIMER_ADDR` updates.
- `ReadVar724Raw` / `WriteVar724Raw` renamed to `ReadLiveTimerRaw` / `WriteLiveTimerRaw`. All call sites updated.
- Log tag updated from `var724 raw=N` to `live raw=N` to make the new source obvious in the log.
- Initial announcement reworded "Timer started" → "Timer detected" since the player may be loading mid-chase rather than at the SETTIMER moment.
- Comment block rewritten to capture the v0.15.13.0/.1/.2 history and the rationale for picking uint16 over uint32.

### Changes in `src/countdown_scan.inl`

Scanner gated behind `#define COUNTDOWN_SCAN_ENABLED 0` at the top of the file. When disabled:

- The large static buffers (`s_region1Buf`, `s_region2Buf` — ~6 MB total) are not declared.
- `Initialize` becomes a one-line log saying "DISABLED (v0.15.13.2). Set `COUNTDOWN_SCAN_ENABLED=1` to re-enable."
- `Update` is an empty no-op.
- Full scanner implementation preserved inside the `#if` block so a future session can flip the flag to re-hunt for a different engine global without rewriting from scratch.

This is a deliberate pattern: when a diagnostic feature has served its purpose, gate the heavy work behind a flag rather than deleting the code. The file keeps documenting how scanning was done, and the next time we need to find an engine global, the only change is the flag and (optionally) the region addresses.

### What the next BAT verifies

Aaron loads the Dollet comm-tower save. The mod log should now show:

- `[CountdownTimer] Initialize v0.15.13.2: reading live engine timer at 0x01CFE92C ...`
- `[CountdownScan] DISABLED (v0.15.13.2). ...` (and nothing else from the scanner).
- Shortly after fieldload: `[CountdownTimer] live raw=NNNN (prev=-1) state=0 tickMs=...` (the first observation).
- Then `[CountdownTimer] ENTER ACTIVE: rawValue=NNNN units=SECONDS initialSec=NNNN (NNmNNs) ...`
- TTS announcement: "Timer detected. NN minutes NN seconds remaining."
- As the chase progresses: `[CountdownTimer] BOUNDARY 1500 seconds reached ...` etc. at 25:00, 20:00, 15:00, 10:00, 5:00, 1:00, 0:30.
- Pressing T at any point: "NN minutes NN seconds remaining."
- Pressing Shift+T: "Timer frozen." Then on-screen timer stops advancing (or flickers between current and frozen value at HUD refresh rate). Pressing Shift+T again: "Timer resumed."

Static memory should drop by ~6 MB (verifiable indirectly via taskmgr if Aaron cares to check). No `[CountdownScan]` lines beyond the disabled announcement.

### Failure modes to watch for

- **No `[CountdownTimer] live raw=NNNN` after fieldload**: read may have faulted on 0x01CFE92C. SEH should catch this gracefully; log would be empty rather than crashing. Could mean the address isn't always mapped before fieldload finishes initializing the engine state. Mitigation: read attempts run every frame, so it'd start working once the page maps. If it never maps, the scanner finding was a false positive (unlikely given the exact 1.00/s signature).
- **`live raw=0` throughout**: the address holds zero. Could mean the timer hasn't started yet for this save, or 0x01CFE92C is actually a per-save-slot offset rather than a global. Aaron would confirm by watching the on-screen HUD.
- **Units misclassified**: if the live address holds a value outside our three ranges (5-60, 500-3000, 15000-60000), the classifier returns UNKNOWN and the state machine stays INACTIVE. The "Observed nonzero value N but units UNKNOWN" log line will tell us which range to add. (Aaron's BAT had value 1711 which is in SECONDS range — should be fine.)
- **Shift+T freeze doesn't visually freeze the timer**: the engine writes to 0x01CFE92C more aggressively than our mod thread can rewrite. If this happens, we have the read working but freeze remains unreliable; that's an acceptable trade-off — read-and-announce is the primary feature. Could be addressed in v0.15.14 by hooking the engine's write instead of polling.

### Files

- MODIFIED: `src/countdown_timer.cpp` (live timer address, renames, log tags, comments)
- MODIFIED: `src/countdown_scan.inl` (compile flag gating heavy work; full implementation preserved)
- MODIFIED: `src/ff8_accessibility.h` (version 0.15.13.2)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Deferred to later builds

- `menu_tts.cpp` T-handler `!shift` gate. Theoretical conflict only.
- Deep-research doc comment fix (still says `0x01CFECCC`).
- `field_display_names.h` audit (fieldIds 0x0134 / 0x0136 mislabeled).
- v0.15.14.0 candidate work: hook the engine write to 0x01CFE92C for more reliable freeze; or add value-range "spotlight" pass to the scanner so future address hunts surface slow timers even amid faster-changing neighbors.

## v0.15.13.1

Region expansion for the in-mod timer scanner, after the v0.15.13.0 BAT showed the scanner working mechanically but failing to surface a candidate matching the visibly-active Dollet 30-minute countdown.

### v0.15.13.0 BAT findings (why this build exists)

Aaron loaded a save in the Dollet comm tower (post-Elvoret, timer actively counting down) and captured an F11 screenshot at 21:24:47 showing `28:19` on the timer HUD. The mod log confirms the scanner ran correctly: `Initialize: armed`, `First snapshot done at slot 0: Region 1 2/2 pages mapped, Region 2 256/256 pages mapped`, 11 analysis cycles. No SEH faults; both regions fully mapped.

But none of the candidates surfaced over those 11 cycles match any plausible encoding of "28:19 remaining":

- SECONDS encoding expected cur ≈ 1699 — no candidate near that value
- MINUTES encoding expected cur ≈ 28 or 29 — no candidate in that range
- FRAMES@30Hz encoding expected cur ≈ 50970 — no candidate near that
- MS encoding expected cur ≈ 1,699,000 — filtered out by v0.15.13.0's `MAX_PLAUSIBLE_VAL = 200,000`

The actual candidates were either (a) very-fast-changing animation counters during field load (cycle 7's 16 entries at `0x01DC67xx-0x01DC68xx` with rate ~115/s and cur values 60-75 — entity state during the comm-tower-interior load), (b) menu-state byte-boundary artifacts (uint16 reads spanning a byte where the low byte changed, looking like dec=256 in uint16), or (c) the recurring `0x01D2B106 dec=32 rate=8/s` counter (constant pattern, probably an audio/input system tick).

Diagnosis: the chase timer global lives in a region the v0.15.13.0 scanner did not cover. The address-resolution log lists many engine globals — `pCurrentFieldId = 0x01CD2FC0`, `pCurrentFieldName = 0x01CD2DB0`, `pMode0Phase = 0x01CE4760`, `pMode0InitFlag = 0x01CE0758`, `pMasterSfxVolume = 0x01CD1794`, `_mode = 0x01CD8FC6`, `pKeyboardState = 0x01CD02D8`, `pEngineInputValidButtons = 0x01CD01F8` — all in the `0x01CD0000-0x01D00000` range, which is exactly the 192 KB gap between v0.15.13.0's Region 1 (8 KB at the game-object struct base `0x01CFE9B8`) and Region 2 (`0x01D00000-0x01E00000`). The chase timer is almost certainly in that neighborhood.

### Changes

**Region 1 expanded**: from 8 KB at `0x01CFE9B8` to **192 KB at `0x01CD0000`** (covers the broader engine-globals zone). The game-object struct at `0x01CFE9B8` is now at offset `0x2E9B8` inside this expanded region. Page count grows from 2 to 48. Static buffer grows from 40 KB to 960 KB.

**`MAX_PLAUSIBLE_VAL` raised** from 200,000 to **2,000,000**. Admits ms-encoded 30-minute timers (1,800,000 ms at chase start).

**`MAX_RATE_PER_SEC` raised** from 200 to **2000**. Admits ms-encoded decrements at ~1000/sec.

**Bug fix: "Ring is now full" log spam.** v0.15.13.0 fired this line every snapshot tick (every second) after `s_snapshotsTaken` saturated at `SNAPSHOT_COUNT`. v0.15.13.1 adds `s_ringFullLogged` boolean so the line fires exactly once on the transition from 4→5 snapshots.

### Memory cost

Static buffers grow from 5.04 MB to ~5.96 MB total. Per-snapshot CPU cost grows proportionally with region 1 size (now scans 48 pages instead of 2, but region 2's 256 pages dominate anyway). Per-analyze CPU cost grows: 192 KB has ~98k uint16 + ~49k uint32 candidates, plus region 2's ~786k. Inner loop is still tight; should land in the 50-100 ms range at 5-second cadence.

### What the next BAT will tell us

Aaron loads the same comm-tower save and plays for ~10-15 seconds (enough for the ring to fill plus one analyze cycle). The `[CountdownScan]` log shows the candidate dumps. Expected outcomes:

- **Clean find in R1**: the timer global is in the newly-scanned engine-globals area. Look for a candidate whose `cur` value matches the on-screen timer value at the screenshot moment (1699 for seconds, 50970 for frames@30Hz, 28 or 29 for minutes, ~1.7M for ms). Then v0.15.13.2 hardcodes that address.
- **Multiple plausible candidates**: several addresses look timer-shaped. v0.15.13.2 adds a value-range "spotlight" pass that filters per encoding so the right one is easier to pick out.
- **Still no candidate**: the timer is either outside both regions or encoded in a way our filters miss. v0.15.14.0 pivots to Path B — hook the DISPTIMER opcode (`0x09D`) at JSM dispatch and read whatever memory address the engine reads to render the HUD value.

### Files

- MODIFIED: `src/countdown_scan.inl` (region 1 expansion, bound widening, log-spam fix)
- MODIFIED: `src/ff8_accessibility.h` (version bump to 0.15.13.1)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

No other source changes. `src/countdown_timer.cpp` is unchanged from v0.15.13.0.

## v0.15.13.0

In-mod memory scanner for the live FF8 mission timer global. After Aaron clarified that his v0.15.12.0 BAT save was post-Elvoret in the Dollet comm tower — i.e. the 30-minute escape countdown was actively running and visibly decrementing on screen — we know the script-side snapshot at `0x01CFEC8C` does not mirror the live timer. The `[CountdownTimer]` log lines from that BAT showed `var724 raw=0` throughout, conclusively. v0.15.13.0 adds a scanner that hunts for the address that actually does decrement, so v0.15.13.1 can repoint reads to it.

### New module: `src/countdown_scan.inl`

A textually-included `.inl` (no `deploy.bat` change needed; the .inl reaches the build through the existing `countdown_timer.cpp` compilation unit) that:

- Snapshots two regions of process memory into its own buffers every 1 second:
  - **Region 1** at `0x01CFE9B8 + 8 KB`. Covers the game-object struct in case the field-var stack lives at some offset inside it rather than at offset 0. Cheap (2 pages).
  - **Region 2** at `0x01D00000 .. 0x01E00000`. The 1 MB engine-globals zone the deep-research doc identified as the most plausible home of the live engine countdown global. 256 pages.
- Maintains a 5-snapshot ring → 4-second time window of history.
- Each `SEH-wrapped` per-page read; pages that fault on first read are marked invalid and skipped from then on.
- Analyses every aligned `uint16` and `uint32` inside the regions whose values across the 5 snapshots are: all nonzero, all not `0xFFFF` / `0xFFFFFFFF` (FF8 unset sentinels), monotonically non-increasing, with total decrement > 0, with current value < 200000 (filters out pointer-like values), and with per-second rate in [0.10, 200.0] (admits seconds-level, frames@30Hz-level, and even minutes-level timers that happen to tick during the window, while rejecting random data noise and large counters).
- Logs the top-16 candidates per region per width every 5 seconds to `ff8_mod.log` under tag `[CountdownScan]`, sorted by total decrement. Each line includes address, current value, oldest value, total decrement, and per-second rate. From these Aaron can identify the Dollet timer by matching expected values (~1800 if SECONDS, ~30 if MINUTES, ~54000 if FRAMES_30HZ) at the start of his BAT session and seeing them drop steadily.
- Always-on for v0.15.13.0 — no field-id gating. Aaron loads any save with an active timer, plays for ~5 seconds to fill the ring, then for ~5 more seconds to see the first analyse cycle. v0.15.13.1+ may add gating once the address is known.

Memory cost: ~5 MB static (snapshot ring) + ~40 KB (region 1 ring). Per-frame cost: dominated by `Scan::Update` which mostly short-circuits on the snapshot/log-interval checks; per-snapshot is ~256 SEH-wrapped 4 KB memcpys (~1-2 ms total); per-analyse is the inner loop over ~786k candidate addresses (~50-100 ms). The analyse cost lands in a 5-second cadence so the per-second amortised cost is small.

### Wired into `src/countdown_timer.cpp`

Three changes there:

1. `#include <cstring>` added at the top (the `.inl` uses `memcpy` and `memset`).
2. Forward declaration of the `Scan` sub-namespace's `Initialize` and `Update(DWORD)` so the calls from `CountdownTimer::Initialize` and `CountdownTimer::Update` resolve before the `.inl` definition.
3. `Scan::Initialize()` added at end of `CountdownTimer::Initialize()`; `Scan::Update(GetTickCount())` added unconditionally at end of `CountdownTimer::Update()` (runs regardless of whether the var724 read faulted, since the scanner has its own per-page fault handling). The `#include "countdown_scan.inl"` sits at the bottom of the `CountdownTimer` namespace block so the definitions land in `CountdownTimer::Scan::*`.

Existing var724 logic is unchanged in behavior — still reads `0x01CFEC8C`, still runs the state machine, still polls T and Shift+T. The scanner is purely additive diagnostic for this build.

### Cosmetic cleanup also in this commit

The deep-research doc and the original `countdown_timer.cpp` comments both said "0x01CFE9B8 + 724 = 0x01CFECCC" — that's wrong math. 724 decimal = 0x2D4 hex, and 0xE9B8 + 0x2D4 = `0x01CFEC8C`. The C++ code computed the correct value at compile time (via `FIELD_VAR_STACK_BASE + 724`), so the binary was right; only the comments were misleading. Corrected throughout `src/countdown_timer.cpp` (header block + the BAT-result comment that explains what we learned in v0.15.12.0). The deep-research doc still has the original wrong math; that's a documentation cleanup task tracked in backlog.

### Expected BAT outcome

Aaron loads a save with the Dollet timer active (the same save shape he used for the v0.15.12.0 BAT). The mod log shows the existing `[CountdownTimer]` lines (`Initialize`, `var724 raw=0` once, no further changes because the snapshot doesn't update — same as v0.15.12.0). New: `[CountdownScan] Initialize: armed.` near startup; `[CountdownScan] First snapshot done at slot 0: Region 1 N/2 pages mapped, Region 2 N/256 pages mapped.` after ~1 second; `[CountdownScan] Ring is now full (5 snapshots). Analysis will begin on the next scheduled log tick.` after ~5 seconds; then every 5 seconds a `=== Scan cycle #N ===` block listing the top candidates per region/width.

Three possible BAT outcomes (in increasing severity of follow-up needed):

- **Clean find.** The Dollet timer appears at the top of `R2 u16` or `R2 u32` (or `R1` if the field-var stack really is inside the game-object struct) with the expected value (~1800 / ~30 / ~54000 at fresh chase start, decreasing). v0.15.13.1 hardcodes that address and the timer reads work. Aaron reports which address + width + initial value.
- **Multiple plausible candidates.** Several addresses show timer-like behavior but it's not obvious which is the real Dollet timer. v0.15.13.1 adds a tighter filter (e.g. value must match a known starting duration within tolerance at chase start) or adds a longer ring window (covers more time so minute-level timers stand out more).
- **No clean candidate.** The scanner finds many addresses but none match expected values, OR finds nothing because Region 2 is mostly unmapped. v0.15.14 falls back to Path B (SETTIMER opcode hook at JSM dispatch slot 0x9C): hook the script-VM dispatch table at the SETTIMER index, capture the duration parameter when the chase script calls it, simulate locally off `GetTickCount`. Doesn't give freeze, but gives reliable read-and-announce.

### Files

- NEW: `src/countdown_scan.inl`
- MODIFIED: `src/countdown_timer.cpp` (scanner wiring, comment corrections)
- MODIFIED: `src/ff8_accessibility.h` (version bump to 0.15.13.0)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Deferred (post-scanner-find or post-Path-B)

- `menu_tts.cpp` T-handler `!shift` gate. Theoretical conflict only.
- `Plan & Research Documents/Dollet timer countdown deep research results.md` comment fix (still has the 0x01CFECCC typo).
- `src/field_display_names.h` audit (wrong mappings for fieldIds 0x0134 / 0x0136 in the Dollet comm tower area, surfaced by the v0.15.12.0 BAT interpretation cycle).

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
