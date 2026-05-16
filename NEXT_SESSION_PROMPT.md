# Next Session Prompt: v0.15.13.2 BAT-verified, ready to push

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and this file before any work.

## Where we are

**GitHub HEAD = v0.15.12.0** (commit `b573fd12`). **Local tree = v0.15.13.2, BAT-verified, awaiting Aaron's push.** The countdown timer chapter is functionally complete.

## v0.15.13.2 BAT recap — everything works

Aaron loaded Dollet comm-tower save at 22:20:36. Log:
- `live raw=1731 (prev=0) state=0` — first observation succeeded
- `ENTER ACTIVE: rawValue=1731 units=SECONDS initialSec=1731 (28m51s)` — classifier correct, state transition correct
- Decrement runs cleanly at 1/sec for 15 seconds (1731 → 1717)
- T-key tested 5 times in ACTIVE state, all read correctly
- **Shift+T freeze engaged at raw=1717.** For the next 20 seconds the value oscillated tightly between 1716 (engine decrement) and 1717 (mod rewrite ~62 ms later). T-key during freeze always announced 1717.
- F11 screenshot at 22:21:08 captured HUD showing 28:36 (= 1716 sec), confirming the freeze visually — value never drifted more than 1 second from the frozen point after 20 sec of holding.

Three iterations to get here:
- v0.15.13.0 introduced the scanner (region too narrow, missed timer)
- v0.15.13.1 expanded Region 1, found `0x01CFE92C` in cycle 11
- v0.15.13.2 hardcoded that address, disabled the scanner, verified all features

## What's next (priority order)

### 1. Push v0.15.13.0/.1/.2

Aaron runs `Utilities/push_to_github.ps1`. The push utility reads the top CHANGELOG entry (v0.15.13.2) and the FF8OPC_VERSION macro (0.15.13.2) — both match, so it'll push cleanly. Since GitHub HEAD is at v0.15.12.0 and local tree is at v0.15.13.2, all three intermediate commits go up in this one push. Diagnostic logs go to `Logs/push_diagnostic.log`. Claude NEVER pushes; only Aaron runs the utility.

### 2. Fire Cavern verification

The same `LIVE_TIMER_ADDR = 0x01CFE92C` should drive the 10/20/30/40-minute Fire Cavern variants, Missile Base, Centra Odin, and Rinoa-in-space — all of these use the same engine countdown system per the deep research. A quick BAT with a Fire Cavern save would confirm the address is timer-system-wide rather than Dollet-specific. Expected: same `[CountdownTimer] live raw=NNNN` line at field load, SECONDS classification (600 / 1200 / 1800 / 2400 for the four duration choices), boundary announcements as the timer ticks down.

If Fire Cavern shows `live raw=0` instead, the address is Dollet-specific and we'd need to either:
- Re-scan (flip COUNTDOWN_SCAN_ENABLED to 1 in countdown_scan.inl, load a Fire Cavern save, BAT)
- Or check whether 0x01CFE92C is a per-event slot and the Fire Cavern timer lives at a different but nearby address

### 3. Smaller backlog items

- **`menu_tts.cpp` T-handler `!shift` gate**. One-line change. Theoretical conflict between Shift+T → `AnnouncePlayTime` in menu mode 6 and Shift+T → `CountdownTimer::ToggleFreeze`. The countdown handler runs first (Update is before menu_tts in dinput8.cpp's main loop), so the practical conflict probability is zero — but the gate is still correct hygiene.
- **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136 surfaced in v0.15.12.0 BAT. Likely more Dollet entries are wrong too.
- **Deep-research doc update**: `Plan & Research Documents/Dollet timer countdown deep research results.md` needs (a) wrong-math fix (`0x01CFEC8C` not `0x01CFECCC`) and (b) a "v0.15.13 — LIVE TIMER FOUND" appendix documenting that the live engine global is at `0x01CFE92C`, NOT in the field-var stack.

### 4. Future improvements (low priority)

- **Engine-write hook for cleaner freeze.** Current Shift+T behavior is functionally correct (value held within ±1 sec, T-key always reads frozen value) but has cosmetic ±1-sec flicker on the HUD for sighted users. A hook on the engine's write to `0x01CFE92C` would let us suppress the engine's decrement instead of racing it. Requires disassembly lookup for the engine's write site — easy with the disassembly files in `Game Files/disassembly/`. Not urgent; nobody's complaining about the flicker because Aaron is blind and T-key reads correctly.
- **Value-range "spotlight" pass for scanner.** v0.15.13.1 found the timer in only 1 of 14 cycles because the top-16 cap pushed it out elsewhere. A spotlight pass (one guaranteed slot per encoding type: SECONDS, MINUTES, FRAMES_30HZ, MS) would surface slow timers reliably. Worth adding before next time the scanner is re-enabled.

## Hard constraints (unchanged)

- **Do NOT revert AUTO `[CBF]` battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`.** Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics.
- **Check file sizes** via `filesystem:list_directory_with_sizes` BEFORE attempting a full rewrite. Past ~50 KB, use the `move_file` to `_history` + slim rewrite pattern.
- **Verify chase/field entry from the field log, not FieldAnnounce.** Catalog has known-wrong entries.

## Carry-forward lessons

- **Diagnostic-feature gating pattern**: v0.15.13.2 kept the full scanner implementation behind `COUNTDOWN_SCAN_ENABLED 0`. Future address hunts: flip the flag, adjust REGION1_BASE/SIZE if needed, rebuild. Don't delete diagnostic code.
- **Top-N caps can hide slow-changing signals.** A real timer's signature is LOW decrement rate, not high. Future scanner versions should add a spotlight pass per encoding to ensure slow timers always have a slot.
- **F11 screenshots are gold for BAT context.** All three v0.15.13.x BATs depended on Aaron's screenshots providing the reference on-screen value to grep for in the log.
- **Memory-write race produces stable oscillation, not pin.** v0.15.13.2's freeze shows the mod and engine both writing the same address at different cadences; result is the value alternating between two adjacent points, not staying at one. Adequate for screen-reader use; hook the engine write if pixel-perfect pin matters.
- **Scanner success criterion**: a candidate with EXACT integer-ratio rate (1.00/s, 30.00/s, 60.00/s, 1000.00/s) and value in a known timer range is overwhelming evidence even with a sample size of one cycle.

## Session ritual reminder

Read `DEVNOTES.md` and this file at session start. Update both at every version bump AND after every BAT result. Every Claude response starts with `## Claude Says`.
