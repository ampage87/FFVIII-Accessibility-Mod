**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader. Aaron knew the game well from sighted play before he lost his vision.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. Last pushed: **v0.15.2.11** (commit `d65edb32`). v0.15.2.12, .13, .14, .15, and v0.15.3 are local-only.

---

**Current build: v0.15.3 -- BUILT, NOT YET BAT'd. Single-pronged cleanup on top of the v0.15.2.15 milestone.**

The v0.15.2.15 BAT was a milestone success: Aaron played through the entire X-ATM092 chase end-to-end and exited via the Lapin Beach FMV with all 8 audio descriptions playing cleanly. Every chase field's CHASE-AGENT FINAL SUMMARY showed the dynamic agent pin holding the actual robot at zero or near-zero changed bytes. The doopen2a strcmp guard worked; fieldId-flip deactivation fired correctly on the doopen2a -> dotown_3 handoff. The OTHERS-DIAG scanner data confirmed across every chase field that kani had at most 7 changed bytes and battleyarou had 0 -- both static pins were inert. The DEVNOTES decision criterion was satisfied: the dynamic agent pin alone (with chase_battle_freeze's BATTLE NO-OP and doopen2a strcmp guard) is sufficient.

v0.15.3 acts on that decision. The static kani+battleyarou pin in `src/chase_kani_freeze.cpp` is removed entirely. The dynamic chase-agent pin, the v0.15.2.14 fieldId-flip deactivation, and the v0.15.2.9 OTHERS-DIAG diagnostic scanner all stay. ALSO bundled: cosmetic fix to the `src/deploy.bat` "Version: World" regex bug -- tighten `findstr /C:"FF8OPC_VERSION "` to `findstr /C:"#define FF8OPC_VERSION "` so only the actual `#define` line matches.

### Files changed in v0.15.3

- `src/chase_kani_freeze.cpp` (rewritten, ~580 lines down from ~700)
- `src/chase_kani_freeze.h` (design comment rewrite to document v0.15.3 single-pronged design)
- `src/ff8_accessibility.h` (version bump to 0.15.3 + new comment trail entry)
- `src/deploy.bat` (1 line: tighten findstr to `#define`-prefixed pattern)
- `CHANGELOG.md` (top entry, ASCII-only body)
- `DEVNOTES.md` (this file)
- `NEXT_SESSION_PROMPT.md` (BAT plan)

### What v0.15.3 BAT should show

Same chase behavior as v0.15.2.15 -- single-fight chase, robots stay down, dotown_3 transition succeeds, Lapin Beach FMV plays through to dotown_2 -- BUT the field log is shorter and cleaner:

- Each chase field except doopen2a: one `[CBF] PASS` + one `[CHASE-AGENT]` line + one CHASE-AGENT INITIAL hex dump + one CHASE-AGENT FINAL SUMMARY block + OTHERS-DIAG scanner block. NO kani INITIAL / FINAL blocks. NO battleyarou INITIAL / FINAL blocks. NO per-tick FIRST CHANGE lines. NO MID-WINDOW heartbeat.
- doopen2a: one `[CBF] PASS in doopen2a -- skipping RegisterChaseAgent` line. NO `[CHASE-AGENT]` line. NO CHASE-AGENT FINAL SUMMARY. OTHERS-DIAG block still fires (diagnostic preserved). NO kani / battleyarou blocks.
- The `KaniFreeze: FREEZE ACTIVATED` log line uses the new wording "v0.15.3 dynamic agent pin only (static kani+battleyarou pin removed)".
- The `KaniFreeze: Initialize` log line uses the new wording "v0.15.3 DYNAMIC AGENT PIN ONLY".
- Deploy log shows `Version: 0.15.3` instead of `Version: World`.

If chase behavior diverges from v0.15.2.15 (any new crashes, hangs, or robots walking around), the regression is in the cleanup -- check whether some agent-pin code path got accidentally removed alongside the kani-pin code, or whether a state variable was reset prematurely.

### Push plan after BAT confirms

Push history v0.15.2.12, .13, .14, .15, and v0.15.3 to GitHub via `Utilities/push_to_github.vbs` (last pushed was v0.15.2.11). The push utility validates the CHANGELOG top heading matches `FF8OPC_VERSION` -- both are now set to `0.15.3` so it'll work in one push.

---

**Push utility (relevant if it breaks)**

Working as of 2026-05-09 after fixing two bugs in `Utilities/push_to_github.ps1`:
1. `$version:` parser bug -- fixed by `${version}:`.
2. Em dash encoding bug -- replaced all `--` with ASCII `--`.

Diagnostic chain in place if push fails silently:
- `Logs/vbs_diagnostic.log`
- `Logs/powershell_output.log`
- `Logs/push_diagnostic.log`
- `Utilities/push_to_github_direct.bat` (no-VBS fallback)

CHANGELOG hygiene: ASCII-only commit body. v0.15.3 entry uses ASCII-only body.

---

**v0.15.3+ backlog**

**After successful BAT and push:**
- Re-enable engine-rendered chase ASK using the `gameObj+0xD2/0xD3` bitmask recipe (currently TTS+keyboard only).
- Fix `chase_diag::OnAskOpcodeFired` snprintf size-tracking bug.
- Remove party members from entity catalog (existing v0.14.108 filter is incomplete in some fields).
- SeeD rank bug #27, walk-and-talk dialog gap.
- X-ATM092 chase audio descriptions during the chase scene (separate from FMV ADs).
- Refined-coord narrow-gate steering (Balamb Town entrance, etc.).
- Fire Cavern entry (#28) + planner-fallback (#29) per old DEVNOTES history.
- Cosmetic: rename `chase_kani_freeze` module to something like `chase_agent_pin` since "kani" is no longer the relevant referent. Scope creep -- only when there's a good reason to touch the file.
- Once chase is stable in production, archive obsolete diagnostic infrastructure (the OTHERS-DIAG block stays; chase_diag's F12 snapshot mechanism stays; specific debug log lines could be tightened).

**Stretch:** auto-drive chase mode (the "auto" ASK option currently falls back to manual).
