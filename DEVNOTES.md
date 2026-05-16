**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.11.0** (commit `8d29ee61`). Local tree dirty with v0.15.12.0 ready to push (countdown timer + structural cleanup).

---

## Current state: v0.15.12.0 ready to push; needs re-BAT after cleanup, then v0.15.13 work

### What shipped this session

**v0.15.12.0 — countdown timer module + structural cleanup of huge files.** Combined into one entry because the cleanup is what made the version bump for the countdown work possible (Claude couldn't round-trip the prior 421 KB `ff8_accessibility.h` or 488 KB `CHANGELOG.md` for any kind of full-file edit).

Build state: macro at 0.15.12.0, top CHANGELOG entry at v0.15.12.0, push-utility version-match check will pass. Ready for `Utilities/push_to_github.ps1`.

**Countdown timer module:**
- New `src/countdown_timer.h` / `src/countdown_timer.cpp` (~370 lines), wired into `src/dinput8.cpp` and `src/deploy.bat`
- Reads field var 724 at `0x01CFECCC` (uint16, FF8 Steam 2013 field-var-stack base 0x01CFE9B8 + offset 0x2D4 = 0x01CFECCC; 0x14 savemap correction does NOT apply, that's a different region)
- State machine (INACTIVE/ACTIVE/FROZEN), units classifier (FRAMES_30HZ 15000-60000, SECONDS 500-3000, MINUTES 5-60), scheduler at 25/20/15/10/5:00 + 1:00 + 0:30
- T = announce remaining (gated on `IsActive() && !shift && !alt`)
- Shift+T = experimental freeze (rewrites snapshot each frame to captured value)
- Heavy `Log::Mod [CountdownTimer]` diagnostic logging on every value change (rate-limited 50 ms), state transition, hotkey, units detection
- Research saved to `Plan & Research Documents/Dollet timer countdown deep research results.md`. Confirmed timer opcode family: SETTIMER 0x09C, DISPTIMER 0x09D, SHADETIMER 0x09E, GETTIMER 0x0A4, KILLTIMER 0x0B9. Same engine system drives Dollet, Fire Cavern, Missile Base, Centra Odin, Rinoa-in-space. STIM/WAIT_TIMER/TIMER do NOT exist in FF8.

**First v0.15.12.0 BAT: Case C — snapshot never observed positive.**
Aaron triggered the Dollet chase. T key did not announce; Shift+T spoke "No timer to freeze," meaning `IsActive()` returned false the whole time. The classifier never saw a value at `0x01CFECCC` it would accept. Either the snapshot stays at zero during the chase (script never calls GETTIMER to refresh) or it holds a value outside the classifier ranges. The `[CountdownTimer]` lines in `ff8_mod.log` from that chase session are the diagnostic data v0.15.13 needs.

This is exactly the worst-case branch of the BAT decision tree. v0.15.13 has to find the live engine global rather than rely on the snapshot. Since Aaron is blind and can't use Cheat Engine or x64dbg externally, the v0.15.13 path is one of:
1. **In-mod scanner.** Run during the chase, snapshot a candidate region (likely 0x01D00000-0x01E00000 per the research) every second, diff against previous snapshot, surface uint16 / uint32 addresses whose values decrement monotonically at ~30 Hz or ~1 Hz. Output candidates to `ff8_mod.log` for Aaron to identify the timer address by elimination. Then v0.15.14 uses that address for real reads + freeze.
2. **SETTIMER opcode hook (0x09C).** Hook FF8's JSM dispatch table at slot 0x9C. When the chase script calls SETTIMER, the handler runs with the duration parameter. We capture the duration and start a local simulation off `GetTickCount`. Engine timer continues to drive the displayed HUD and game-over, but TTS announcements come from our simulation which tracks alongside. This doesn't give us freeze — that still requires the engine decrement instruction — but it gives us read-and-announce that actually works.

The current snapshot read + Shift+T rewrite stays in place either way as a complementary diagnostic.

**Structural cleanup — file slimming.**
Two project files had grown past the size Claude could round-trip through a single full-file rewrite. They're now slim, with full history preserved off the build path:

| File | Before | After | History preserved at |
|---|---|---|---|
| `src/ff8_accessibility.h` | 421.80 KB | **1.17 KB** | `src/ff8_accessibility_history.h` (NOT in build) |
| `CHANGELOG.md` | 488.25 KB | **7.93 KB** | `CHANGELOG_HISTORY.md` |

The new slim `ff8_accessibility.h` provides exactly what's needed: `#pragma once`, `<windows.h>`, `<cstdint>`, `<string>`, and `#define FF8OPC_VERSION "0.15.12.0"`. Every other module that previously got these system includes transitively via `ff8_accessibility.h` still does. `deploy.bat`'s `findstr /B /C:"#define FF8OPC_VERSION "` regex resolves to a single unambiguous match on the new file. The history file is on disk but not referenced by any `#include`, so it's pure storage — `git status` will show it as new, push it with the rest.

The new slim `CHANGELOG.md` keeps the file-header format the push utility parses (per the v0.15.11.0 entry's own self-description: `## vMAJOR.MINOR.BUILD` heading, push utility reads top heading + body to next `## v`, version in heading must match `FF8OPC_VERSION`). The v0.15.12.0 entry sits on top.

Going forward: future versions get prepended to `CHANGELOG.md`. The inline-comment chain in `ff8_accessibility.h` is retired — that pattern is not coming back. Version bumps are now a one-line edit on a 1 KB header.

### What's NOT in this build (deferred to v0.15.13)

1. **`menu_tts.cpp` T-handler `!shift` gate.** In menu mode 6 with an active countdown, Shift+T would fire both `MenuTTS::AnnouncePlayTime` AND `CountdownTimer::ToggleFreeze`. Theoretical conflict only — Aaron won't open the menu during the Dollet chase — but worth fixing. One-line patch in `menu_tts.cpp::Update()`.

2. **`src/dinput8.cpp` v0.15.9.11.3.x historical-rationale comments compressed.** During the rewrite (needed to wire countdown_timer), some pre-existing multi-paragraph comment blocks were collapsed to short summaries to fit within Claude's response budget for a 720-line file. The code paths are intact and behaviorally identical. The full original comment text is preserved at GitHub HEAD `8d29ee61` if you ever want to restore. Not urgent.

---

## Recently shipped (pre-v0.15.12.0)

### v0.15.11.0 (pushed 2026-05-16 01:05 UTC, commit `8d29ee61`, BAT-confirmed)

Finished unifying the FF8 text decoders. Retired `DecodeFF8TextPreview` from `battle_tts_victory.inl` and the inline ability-name decoder in `HookedBtCandidate8`. All FF8 text now routes through canonical `FF8TextDecode::Decode`. BAT 2026-05-15 19:00: item drops, enemy names, abilities all clean across multiple battles.

### v0.15.10.0 / .1 / .2 — text decoder consolidation, deploy.bat regex fix, three-item cleanup pass. See `CHANGELOG_HISTORY.md` for detail.

### Chase chapter (closed v0.15.9.11.3.9)

X-ATM092 chase accessibility complete. Four coordinated keyboard hooks block physical key presses during chase Auto. Per-field auto-pilot configs. AUTO battle-suppressor cap stays `INT_MAX` per Aaron's directive.

---

## Backlog (in priority order)

1. **v0.15.13: Countdown timer Case C remediation.** In-mod scanner for the live engine timer global, OR SETTIMER opcode hook for start-event simulation. See "First v0.15.12.0 BAT" above for the two-path decision.
2. **`menu_tts.cpp` T-handler `!shift` gate** — small follow-up to v0.15.12.0.
3. **Remove party members from field entity catalog** — already shipped per userMemories (party-member filter shipped v0.14.108 in `field_nav_catalog.inl`); confirm in v0.15.13 and retire from backlog.

### Deferred (don't pick without explicit Aaron direction)

- SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` is wrong section size)
- Walk-and-talk dialog gap (hardcoded engine path)
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) — partially addressed by v0.15.12.0 since same engine system
- `chase_diag::OnAskOpcodeFired` snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression (fix in `DeactivateFreeze` before clearing agent state)

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at start of every session
- Update both at TWO checkpoints: every version bump AND after every BAT result
- **Filesystem MCP for all Windows project files** — bash runs in a Linux container that can't reach the Windows mod directory
- **Aaron pushes via `Utilities/push_to_github.ps1`** (not `.vbs` as previously noted — the actual tool is `.ps1`), Claude NEVER pushes
- **Build/BAT cycle**: Aaron runs `deploy.vbs`. "BAT" = built and tested → read `Logs/build_latest.log` tail then domain log (field/mod/dialog/battle/menu/world)
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)` to prevent Alt+Fx interception
- F12 reserved for per-session diagnostics (search source for existing F12 refs first and REMOVE before re-binding)
- **NEVER re-enable SET3 hook** (CI guard in `.github/workflows/safety-checks.yml`)
- DEVNOTES under 10KB — move older history to DEVNOTES_HISTORY.md
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1)
- **No filesystem `edit_file` available in current session toolset** — only `read_text_file`, `write_file`, `move_file`, etc. Large file rewrites must be planned with size in mind. Check `list_directory_with_sizes` BEFORE attempting any full rewrite. For files past ~50 KB, the safer pattern is `move_file` to a `_history` filename + write a slim replacement, as done for `ff8_accessibility.h` and `CHANGELOG.md` in v0.15.12.0.
- **Inline-changelog accretion is a dead pattern.** v0.15.12.0 retired the line-12 inline chain in `ff8_accessibility.h`. The canonical changelog lives only in `CHANGELOG.md` from now on.
- Every Claude response starts with `## Claude Says`.
