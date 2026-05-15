# Next Session Prompt: BAT v0.15.10.2, then pick next backlog item

## Where we are

**GitHub HEAD = v0.15.10.1**, pushed 2026-05-15 21:38 UTC, commit `e934484e`.

**Local tree = v0.15.10.2**, built but NOT YET BAT'd or pushed. Three-item cleanup pass combining backlog items #1/#2/#3 from the v0.15.10.1 era. All three changes are dead-code/dead-data removal or log-line removal — zero runtime behavior change. Aaron approved combining them into one build rather than three separate ones.

## Step 1: BAT v0.15.10.2

Aaron runs `deploy.vbs`.

### Expected build-log signals (read `Logs/build_latest.log` first)

- Top of file: `Building FF8 Original PC Accessibility Mod Version 0.15.10.2` (not `SINGLE-PRONGED`, not `0.15.10.1`).
- Deployment Complete block: `Version: 0.15.10.2`.
- No compile errors. The risky surface area is just three name-removal sites — pre-flight `edit_file` dryRun-greps confirmed `WALK_REPRESS_PERIOD`, `s_walkRepressCounter`, `s_walkRepressLogged`, `LogBridgeDiagnostic`, and `s_bridgeDiagTick` are all fully unreferenced after the edits. If a build error nevertheless surfaces, the message will be a clear `undeclared identifier` pointing at the missed reference; add the cleanup site and rebuild.

### Expected runtime signals (read domain logs after Aaron launches the game)

- `ff8_mod.log` init banner reads `=== FF8 Accessibility Mod v0.15.10.2 — Log opened <timestamp> ===` followed by `Version: 0.15.10.2` and `Build:   <__DATE__> <__TIME__>` on two consecutive lines. **No parenthesized hard-coded date** between them.
- `ChaseAutoPilot: Initialized v0.15.10.2 ...` log line ends with `v0.15.9.8.3: kani-slot override on domt1_1 -> Others slot 3 (SYM 'laguna').` — no trailing "BridgeDiag still active for empirical confirmation" sentence.

### Functional regression check (low priority — no behavior changed)

If Aaron is willing to trigger the chase to validate v0.15.10.2 didn't break anything:

- Trigger the X-ATM092 chase, pick Auto.
- On `domt1_1` (the bridge): chase still completes with 0 catches. `ff8_field.log` shows the transition-only logs (`BridgeDance: leap #N STARTED`, `BridgeDance: EAST->WEST transition`, `BridgeDance: WEST->EAST transition`) but **no** `BridgeDance: sample state=...` lines at 10 Hz, and **no** `BridgeDiag: ... slot=I sym='Y' ...` lines anywhere.
- Other chase fields (domt4_1, domt3_2, domt5_1, doopen2a, dotown_2/_1) behave identically to v0.15.9.11.3.9.

If Aaron prefers to skip the chase regression check since no behavior changed (acceptable given the trivial risk profile), just verify the build/init banner signals above and push.

## Step 2: Push (only if BAT passes)

Aaron runs `Utilities/push_to_github.vbs`. The script auto-reads `FF8OPC_VERSION` from `src/ff8_accessibility.h` and the top CHANGELOG entry; both already say `0.15.10.2`.

## Step 3: Pick next backlog item

After the push, Aaron picks the next item. Recommended priority order from `DEVNOTES.md`:

### Top remaining picks

1. **#1 Unify all three FF8 text decoders** (medium risk, continues v0.15.10.0's consolidation)
   - Files: `src/battle_tts_victory.inl::DecodeFF8TextPreview` + the small inline ability-name decoder in `HookedBtCandidate8` (sub_47E710 hook in victory.inl).
   - The third decoder has the same wrong digit range as the one retired in v0.15.10.0, plus a slightly different 0x06 mismap, but DOES have 0xE8-0xFF compression-sequence coverage. Item names with digits are quietly broken on the victory phase path.
   - Recommended approach: same SEH-split pattern from v0.15.10.0 (wrap `FF8TextDecode::Decode` because `std::string` inside a `__try` block triggers C2712 in `/EHsc`). Migrate `DecodeFF8TextPreview` callers cluster by cluster — diagnostic logging hooks first (low risk), then the item-announce path (player-facing), then the inline ability decoder.
   - Risk surfaces: touches victory phase machinery; the diagnostic logging paths are noisy but the item announce path is player-facing during every battle win.

2. **#2 Generalized countdown-timer hook** — Dollet 30-min countdown is TTS'd via a chase-specific path; generalize for future timers.

3. **#3 Remove party members from field entity catalog** — Squall/Zell/Selphie appear as targetable entities; filter them out.

### Deferred (don't pick without explicit Aaron direction)

See `DEVNOTES.md` Deferred section: SeeD rank bug #27, walk-and-talk dialog gap, refined-coord narrow-gate steering, Fire Cavern entry, chase_diag::OnAskOpcodeFired snprintf bug, CHASE-AGENT FINAL SUMMARY log regression.

## Hard constraints (unchanged)

- **Do NOT revert the AUTO `[CBF]` battle-suppressor cap to 0.** Aaron's 2026-05-13 directive stands. v0.15.9.11.3.6 BAT vindicated the input-layer fix.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.vbs`.** Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)` to prevent Alt+Fx interception.
- **F12 reserved** for per-session diagnostics — search source for existing F12 refs and REMOVE old code before re-binding.

## Session ritual reminder

Read `DEVNOTES.md` and this file at session start, before any work. Update both at every version bump AND after every BAT result. Every Claude response starts with `## Claude Says`.
