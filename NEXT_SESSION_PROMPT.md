# Next Session Prompt: Chapter 1 ready to push, Chapter 2 queued

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.8.16 BAT-CONFIRMED, awaiting Aaron's push.** Chapter 1 (Bug #1 -- Quistis infirmary FMV premature) is closed. BAT (2026-05-27 18:10-18:12) showed the gate holding for 17 seconds on `disc00_01h.avi` then clearing on engine PLAY, all 4 cues firing at exact absolute offsets in lockstep with the FMV. No regressions on other FMVs.

GitHub HEAD still `c7b80872` (v0.17.8.15.1) -- the v0.17.8.16 build is local-only until Aaron runs `Utilities/push_to_github.ps1`. The push utility validates that `CHANGELOG.md` top heading (`## v0.17.8.16`) matches `FF8OPC_VERSION` (`0.17.8.16`); both are in sync.

## If Aaron has already pushed v0.17.8.16

Open Chapter 2 (Laguna bundle). The first concrete step is Phase 0 research, which needs NO Laguna trigger:

1. **Disassembly search.** In `Game Files/disassembly/`, find char-ID reads inside battle init. Compare to known savemap-formation reads. Look for any conditional read path or different source field. The goal is to identify where the engine resolves the active-battle character IDs during a Laguna dream battle.
2. **FFNx-canary grep.** In `FFNx-Steam-v1.23.0.182\\Source Code\\FFNx-canary\\src\\`, grep for any reference to `dream`, `laguna`, or `fake party`. FFNx's struct definitions and address constants often point straight at the right field.

Once Phase 0 produces a hypothesis (or rules out a static-analysis answer), design the Phase 1 F12 diagnostic build that captures data for BOTH bug #7 (gwgrass1 entity dump for player-detection heuristic) AND bug #8 (compStats during dream battle init) in one Laguna playthrough.

## If Aaron has NOT yet pushed v0.17.8.16

Don't open Chapter 2. The push is what closes the chapter. Aaron runs `Utilities/push_to_github.ps1` to push; the utility refuses if `CHANGELOG.md` and version mismatch (they don't, so the push will succeed). If Aaron wants help interpreting any push output, read `Logs/push_diagnostic.log` and `Logs/git_latest.log`.

## Chapter 1 summary (for context if a future session asks \"what was v0.17.8.16?\")

- **Bug:** AD for Quistis infirmary FMV `disc00_01h.avi` fired ~17-22 seconds ahead of engine playback.
- **Root cause:** `FmvAudioDesc::OnFrame` started the cue timer on AVI file-handle open via `FmvSkip::GetCurrentAviName`, but the engine can open the handle long before it actually begins playback.
- **Fix:** Replaced the wall-clock cue timer in `src/fmv_audio_desc.cpp` with an engine-active-time accumulator (`g_engineActiveSeconds`) that only advances on frames where `FF8Addresses::IsMoviePlaying()` returns true. Gates `StartPlayback` on engine-confirmed playback; accumulator handles mid-FMV STOP/PLAY pause-resume for free.
- **New log lines:** `[FMV_AD] AVI handle open: <name> -- waiting for engine playback` (gate deferring), `[FMV_AD] AVI detected via FmvSkip: <name> (engine confirmed playing)` (gate clearing), `[FMV_AD] Engine stopped/resumed at cue clock X.X s` (mid-FMV pause edges).
- **Files touched:** `src/fmv_audio_desc.cpp` (the fix), `src/ff8_accessibility.h` (version), `CHANGELOG.md` (top entry).
- **BAT result:** Perfect. 17-second gate hold, then 4/4 cues fired at correct offsets, no regressions.

## Chapter 2: bundled Laguna bugs (#7 + #8)

Both bugs surface on `gwgrass1` (first Laguna dream field). Two Laguna playthroughs total: one for diagnostic capture, one for fix validation. The two fixes live in different files (`field_navigation.cpp` vs `battle_tts.cpp`) and different code paths, so bundling is safe.

### Phase 0: pre-build research (no build, no Laguna trigger)

1. **Disassembly search** in `Game Files/disassembly/` for char-ID reads inside battle init. Compare to savemap-formation reads we already know about. Look for any divergence or conditional path.
2. **FFNx-canary grep** for `dream`, `laguna`, or `fake party` in the FFNx source.

If Phase 0 pinpoints where the dream party lives, Phase 1's compStats dump becomes a narrow byte read instead of a wide region scan.

### Phase 1: shared diagnostic build (one F12 diagnostic, one Laguna BAT)

Search source for existing `VK_F12` references and remove old handlers before binding the new one.

**For Bug #7 (field nav, gwgrass1):** On `gwgrass1` field-load, dump every entity once to `[gwgrass-diag]` in `ff8_field.log`:
- entity index, SYM name, model file, `setpc` value, `jsmCategory`, position (X,Y), `hasSetmodelInit`/`foundExtDispatch`/`hasTalkSetup` signals

Auto-disable after one dump.

**For Bug #8 (battle party):** On battle init during a Laguna dream battle, dump per active battler (slots 0/1/2) to `[gwgrass-batt-diag]` in `ff8_battle.log`:
- First ~0x80 bytes of `compStats[N]` (narrower if Phase 0 pinpointed the field)
- Savemap formation array `[0..3]` for comparison
- Whatever the battle-side code currently reads to identify the character for TTS

Fire once per battle init, then auto-disable.

### Phase 2: shared fix build (one Laguna BAT)

- **Bug #7 fix:** Extend `setpc==0` player-detection heuristic in `RefreshCatalog`/`Update` to accept the dream-player marker. File: `field_navigation.cpp`.
- **Bug #8 fix:** Adjust active-battle character ID resolver to read the dream-party source. File: `battle_tts.cpp`.

Keep both diagnostics ON in the fix build so the same BAT re-captures data on regression. Strip them in a follow-on cleanup build after BAT passes.

## Other open work (NOT this session's focus)

- Chase-chapter carry-over (v0.15.9.8.3 bridge catch + v0.15.3.1 chase-agent summary log)
- Source-file refactor queue (only if something is about to cross 80 KB)
- `DEVNOTES_HISTORY.md` trim (mechanical work; v0.17.8.7 cardgamemaster + bug #10 chara.one narrative)
- Plan & Research Documents update (Dollet countdown doc)

## Session-start ritual reminders

- Read `DEVNOTES.md` and THIS file at session start. Read `DEVNOTES_HISTORY.md` only to trace past decisions.
- Filesystem MCP for all Windows project files. Bare `view`/`str_replace`/`create_file` reach the Linux container only.
- `filesystem:edit_file` corrupts files when the replacement text contains a literal `$`. Use hex `0x24` in source, or `filesystem:write_file` to rewrite whole files.
- OneDrive throws a transient EPERM on first `edit_file` rename sometimes. Retry once.
- Aaron pushes via `Utilities/push_to_github.ps1`. Claude NEVER pushes. The utility refuses if `CHANGELOG.md` top heading doesn't match `FF8OPC_VERSION`.
- Diagnostics on F12 only. Search source for existing `VK_F12` references and remove old handlers before binding new ones.
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **One change per BAT cycle, with explicit exception for the Laguna chapter.** Don't generalize that exception to other chapters.
- BAT response: read `Logs/build_latest.log` first for build errors, then the relevant domain log (`ff8_field.log` for #7, `ff8_battle.log` for #8).
- Update DEVNOTES.md + this file at every version bump AND after every BAT result.

## What NOT to do on session open

- Don't open Chapter 2 (Laguna bundle) until v0.17.8.16 is pushed.
- Don't BAT or build anything before Aaron confirms direction.
- Don't pivot to refactors, history trim, or research-doc updates unless Aaron explicitly redirects.
