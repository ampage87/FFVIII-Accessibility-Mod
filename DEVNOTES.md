**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.15.12.0** (commit `b573fd12`). **Local tree = v0.15.13.2, BAT-verified, ready for push.**

---

## Current state: countdown timer chapter COMPLETE

### v0.15.13.2 BAT — all features verified

Aaron loaded the Dollet comm-tower save at 22:20:36. Mod log confirms:

```
[22:20:36] live raw=1731 (prev=0) state=0 tickMs=99957171
[22:20:36] ENTER ACTIVE: rawValue=1731 units=SECONDS initialSec=1731 (28m51s)
```

Classifier correctly tagged the value as SECONDS. State machine entered ACTIVE. TTS announced "Timer detected. 28 minutes 51 seconds remaining."

Decrement ran cleanly at exactly 1/sec for 15 seconds (1731 → 1717) before Aaron froze.

T-key tested 5 times in ACTIVE state (22:20:40 / .42 / .44 / .48 / .49) — each press read the current value correctly.

**FREEZE engaged at 22:20:51, raw=1717.** Log shows a beautifully tight alternation pattern for the next 20 seconds:
```
live raw=1716 (prev=1717) state=2 tickMs=99972500
live raw=1717 (prev=1716) state=2 tickMs=99972562  <- mod re-pinned 62ms later
```
Engine writes 1716 once per second; mod writes 1717 immediately after. Value spends ~6% of each second at 1716, ~94% at 1717. **T-key during freeze always reads 1717** — for screen-reader accessibility this is fully working.

F11 screenshot at 22:21:08 captured HUD reading **28:36** (= 1716 sec — the brief post-engine-decrement window). After 20 seconds holding the freeze, the on-screen value drifted at most 1 second from the 1717 freeze point. Minor cosmetic flicker for sighted players; irrelevant for Aaron's use case.

### Path summary (v0.15.12.0 → v0.15.13.2)

- **v0.15.12.0**: countdown timer module introduced, reading `0x01CFEC8C`. BAT showed Case C — snapshot stayed at 0 throughout the chase, no announcements.
- **v0.15.13.0**: in-mod scanner introduced. Region 1 = 8 KB at game-object struct. BAT showed scanner working mechanically but the timer wasn't in either region.
- **v0.15.13.1**: Region 1 expanded to 192 KB at `0x01CD0000`. BAT found the live timer at `0x01CFE92C` in cycle 11 (the only cycle calm enough for the slow rate-1.00/s candidate to make the top-16).
- **v0.15.13.2**: live address hardcoded, scanner gated off via `COUNTDOWN_SCAN_ENABLED 0` (~6 MB static memory freed). BAT verified all three features (auto-announce on entry, T-key read, Shift+T freeze).

### Push state

Local tree has three unpushed commits (v0.15.13.0, .1, .2). Aaron will run `Utilities/push_to_github.ps1` which reads the top CHANGELOG entry (currently v0.15.13.2). Push should combine all three since v0.15.12.0 is the last GitHub commit; pre-push diagnostic will tag them as v0.15.13.2.

---

## Recently shipped (in order, top is newest)

### v0.15.13.2 (built, BAT-verified, awaiting push)
Live timer reads at 0x01CFE92C. Scanner disabled via compile flag. All features working.

### v0.15.13.1 (built, BAT-verified — scanner found timer)
Region expansion to 192 KB at 0x01CD0000. Bounds widened. Found 0x01CFE92C.

### v0.15.13.0 (built)
In-mod memory scanner introduced. Worked mechanically but missed timer (region too narrow).

### v0.15.12.0 (pushed 2026-05-15, commit `b573fd12`)
Countdown module + structural cleanup of ff8_accessibility.h (421 KB → 1.17 KB) and CHANGELOG.md (488 KB → 7.93 KB).

### Chase chapter (closed v0.15.9.11.3.9)
X-ATM092 chase accessibility complete. AUTO battle-suppressor cap stays `INT_MAX`.

---

## Backlog (in priority order)

1. **Push v0.15.13.0 + .1 + .2** (Aaron runs the push utility).
2. **Verify on Fire Cavern**: same `LIVE_TIMER_ADDR` global is used by Fire Cavern (10/20/30/40-min variants), Missile Base, Centra Odin, and Rinoa-in-space. A quick BAT with a Fire Cavern save should confirm the address is timer-system-wide rather than Dollet-specific. If it works there too, the module covers all FF8 mission countdowns automatically.
3. **`menu_tts.cpp` T-handler `!shift` gate**. One-line. Theoretical conflict only — Shift+T in menu mode 6 could fire both `AnnouncePlayTime` and `CountdownTimer::ToggleFreeze`.
4. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Surfaced in v0.15.12.0 BAT.
5. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` still has `0x01CFE9B8 + 724 = 0x01CFECCC` (wrong math from v0.15.12.0). Add a "v0.15.13.x — LIVE TIMER FOUND" appendix documenting that the field-var stack base 0x01CFE9B8 is correct but var 724 at 0x01CFEC8C is a script-side snapshot only updated by GETTIMER, NOT the live engine global. The actual live global is at 0x01CFE92C, 0x8C bytes BELOW the game-object struct base, in a separate engine-globals allocation.

### Future improvements (deferred, low priority)

- **Engine-write hook for cleaner freeze.** Current freeze has cosmetic ±1-second flicker because mod and engine race on writes to `0x01CFE92C`. A hook on the engine's write instruction (find via the disassembly's `MOV [0x01CFE92C], ...` references) would let us selectively suppress the engine's decrement when frozen. Not urgent — accessibility-side behavior is already correct.
- **Value-range "spotlight" pass for scanner.** v0.15.13.1 found the timer only because cycle 11 happened to be calm enough for the slow candidate to make the top-16. A spotlight pass (one slot per encoding: SECONDS, MINUTES, FRAMES_30HZ, MS) would surface slow timers reliably in every cycle. Worth adding before the scanner is re-enabled for a future engine-global hunt.

### Deferred

- SeeD rank bug #27
- Walk-and-talk dialog gap
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) — auto-handled by v0.15.13.2's live timer once verified, plus the chase-style gating around the field entry
- `chase_diag::OnAskOpcodeFired` snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at start of every session
- Update both at TWO checkpoints: every version bump AND after every BAT result
- **Filesystem MCP for all Windows project files**
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes
- **For "didn't fire" diagnostic results, ASK Aaron what was happening on screen.** Context matters as much as the log.
- **Read FIELD NAME from the field log, not FieldAnnounce display catalog.** Catalog has known-wrong entries.
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`
- F12 reserved for per-session diagnostics
- **NEVER re-enable SET3 hook** (CI guard)
- DEVNOTES under 10KB
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1)
- **`.inl` textual-include pattern**: no `deploy.bat` change needed; reaches the build via `#include` from a `.cpp` already in the compile list.
- **Inline-changelog accretion is a dead pattern.** Retired in v0.15.12.0.
- **F11 screenshots are gold for BAT context.** The v0.15.13.0/.1/.2 progression all hinged on Aaron's screenshots providing the reference on-screen value.
- **Diagnostic-feature gating pattern (v0.15.13.2)**: when a diagnostic has served its purpose, gate the heavy work behind a `#define X 0` flag rather than deleting. Preserves the implementation for future similar problems.
- **Top-N candidate caps can hide slow-changing signals.** v0.15.13.1's scanner found the timer in only 1 of 14 cycles because faster entity churn pushed the slow timer out of the top-16 in every other cycle. Future scanner versions should consider per-bucket value-range "spotlight" passes.
- **Memory-write race on shared addresses.** v0.15.13.2's freeze shows the mod and engine both writing `0x01CFE92C` at different cadences; the result is a stable oscillation between two adjacent values, not a perfect pin. Hooking the engine's WRITE (rather than racing it) is the proper fix when freeze pinning matters.
- Every Claude response starts with `## Claude Says`.
