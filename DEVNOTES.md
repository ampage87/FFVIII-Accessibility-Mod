**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. `main` HEAD = `e45b1a4f` (v0.14.104 pushed Thu 2026-05-07). v0.14.105 + v0.14.106 are local-only, ready to push together.

---

**Current build: v0.14.106 BUILT, AWAITING BAT**

Session goal (Thu 2026-05-07 evening): persistent settings — both items resolved.

**Item #2 (audio ducking persistence): ALREADY DONE** in v0.14.45.

**Item #1 (speech rate persistence): ROOT CAUSE FOUND IN v0.14.105.**

Aaron diagnosed mid-session: he closes the game with **Alt+F4** — and on the way out, the mod announced "Rate increased" because the F4 binding wasn't gated on Alt. Every Alt+F4 close triggered `IncreaseRate()`, bumping the saved rate by 1. After enough sessions, rate parked at 10.

**v0.14.105 fix (BAT'd successfully):** every function-key accessibility handler (F1–F8, F11) in `dinput8.cpp` now requires `!alt` (`GetAsyncKeyState(VK_MENU) & 0x8000`). Standalone presses unaffected; any Alt+combo no-ops. v0.14.105 BAT (Thu 2026-05-07 14:34): Aaron set rate=7, closed via Alt+F4, restarted. Log showed `Config loaded rate=7 volume=100`. Welcome played at the saved rate. F4 then went to 8 (correct increment). All `[RATE-DIAG]` lines showed shadow==SAPI rate=7 throughout init, confirming SAPI's `SetVoice()` does preserve rate/volume.

All persisted settings round-trip verified working:
- `speech_rate` (F4/F3) ✓
- `speech_volume` (Shift+F4/Shift+F3) ✓
- `speech_voice_id` (F1) ✓ (DAVID restored automatically)
- `game_volume` BGM (F8/F7) ✓ (loaded as 40% in second session)
- `sfx_volume` (F6/F5) ✓ (loaded as 80% in second session)
- `tts_duck_enabled` audio ducking (F2) ✓ (loaded as ON)
- `sfx_duck_ratio` ✓ (no in-game setter, hand-edit only; loaded as 30)
- `ewm_enabled` (O) ✓ (loaded as 1)

**v0.14.106 changes (this build):**

1. **Cleanup**: stripped the `LogActualSAPIState` diagnostic harness from `screen_reader.cpp` — helper, 5 instrumentation points, defensive re-apply block. The diagnostic served its purpose (proved persistence is sound); the defensive re-apply is also unnecessary.
2. **INI commented template** in `config.cpp`. Two new helpers: `HasTemplateMarker()` checks for our marker comment; `EnsureTemplate()` reads existing values via `GetPrivateProfileInt/String`, then rewrites the file with a commented template overlaid with current values. Called from `Load()` when (a) the INI doesn't exist yet (fresh install), or (b) the INI exists but lacks the marker (legacy upgrade — preserves all existing values). Subsequent `SetInt/SetString` calls hit `WritePrivateProfileStringA` which preserves comments line-by-line.

The commented INI looks like:
```ini
; ============================================================================
; FF8 Accessibility Mod settings
; ============================================================================
; This file stores your accessibility preferences across game sessions.
; ...
[Accessibility]
; Speech rate. Range: -10 (slowest) to 10 (fastest). Default 3.
; In-game: F4 = faster, F3 = slower.
speech_rate=7
...
```

**v0.14.106 BAT plan:**

1. Run `deploy.vbs`.
2. Launch the game once. The mod log should report `Config: Existing INI lacks comment header; upgrading...` followed by `Wrote commented INI template to ...`.
3. Quit (Alt+F4 or otherwise — both safe now).
4. Open `ff8_accessibility.ini` in a text editor and verify the commented template is in place with the current values populated.
5. Launch a second time. Log should NOT say "lacks comment header" again — should just say `Using ...` (idempotent).
6. Optional: tweak any setting via in-game shortcuts and confirm the value updates in-place without disturbing the comments.

After successful BAT, push v0.14.105 + v0.14.106 to GitHub via `Utilities/push_to_github.bat`.

---

**Open GitHub issues at session end (16 total):**
- #2, #3, #5-10, #15, #18-22, #25, #26 (PR), #27, #28, #29

---

**Key learnings & principles**

**Engine / data:**

- **SAVEMAP HEADER = 76 bytes (0x4C)**. Subtract 0x14 from deep-research offsets that assume 96-byte header. Confirmed base: `0x01CFDC5C`.
- **WORLDMAP struct at savemap+0x125C** (BAT-confirmed v0.14.103.2).
- **Savemap WORLDMAP positions at 1:1 scale with foot DWORDs** (BAT-confirmed v0.14.103.2). NOT 20.12 fixed-point.
- **Locomotion byte at 0x02040A5E does NOT reliably indicate rental car state** — stays at 6. Use `car_rent` flag at 0x01CFEF1A.
- **SET3 opcode hook PERMANENTLY DISABLED** — hangs the infirmary scene.
- **Foot DWORDs DO update during rental car drives**.
- **Car-vs-wall is OSCILLATION, not freeze**.
- **Victory TTS must hook text renderer, not read memory**.
- **Battle entity race condition**: `s_prevBattleMagicId` never resets on battle escape; capture snapshot after entity-ready check.

**Persistence layer (Config) — fully verified working as of v0.14.105:**

- `src/config.cpp` + `config.h`. INI at `<dll-dir>/ff8_accessibility.ini`, section `[Accessibility]`.
- Persisted: `speech_rate`, `speech_volume`, `speech_voice_id`, `game_volume` (BGM), `sfx_volume`, `tts_duck_enabled`, `sfx_duck_ratio`, `ewm_enabled`.
- v0.14.106 adds auto-generated commented template with usage docs and ranges. Legacy un-commented INIs auto-upgrade on next launch.
- **SAPI behavior confirmed**: `SetVoice()` preserves rate/volume across voice changes. v0.14.105's diagnostic harness proved this empirically; no defensive re-apply needed.
- **`GetAsyncKeyState(VK_MENU)` checks Alt** — used to gate F-key handlers from firing during Alt+F4 close (or any other Alt+combo).

**Tooling / workflow:**

- **DEEP RESEARCH DOCS FIRST**: Search `Plan & Research Documents/` before writing engine-data interpretation code.
- **EXISTING KNOWLEDGE FIRST**: For "used to work before Sonnet regression" reports, run `conversation_search` BEFORE writing new logic.
- **Cross-check arithmetic against authoritative target addresses**.
- **Diagnostic dumps must fire when data is actually loaded**.
- **When BAT log seems to show absence of feature exercise, ASK Aaron rather than assuming.**
- **dryRun=true as grep substitute**.
- **OneDrive EPERM on first edit**: Retry immediately.
- **Bash cannot reach Windows project files** — ever.
- **nightsolo tables more reliable** than FF Wiki for per-enemy status data.
- **Multi-line edits with verbose comments**: When `oldText` matches only the start of a long line, the trailing fragment is preserved. Use FULL line as `oldText` or follow up with cleanup edit.

---

**Approach & patterns**

- **Session ritual**: Read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` at start of every session.
- **SESSION CHECKPOINT RULE**: Update DEVNOTES + NEXT_SESSION_PROMPT at TWO checkpoints: (1) every version bump for testing, (2) after every BAT result.
- **BAT workflow**: Check `Logs/build_latest.log` tail for build errors, then domain-specific game log.
- **Build error**: Read `Logs/build_latest.log` tail before attempting fixes.
- **Version bump**: ONE location only — `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- **Build system**: `deploy.vbs` (root) → `src/deploy.ps1` → `src/deploy.bat`.
- **GitHub**: Claude NEVER pushes. Aaron uses `Utilities/push_to_github.bat`. Before quoting any backlog size, call `github:list_commits`. Provide Aaron: (1) version, (2) consolidated commit description.
- **Deep research escalation**: When source/game/logs are insufficient, ask Aaron to run deep research via ChatGPT.

**F12 diagnostic key rule**: F12 is reserved exclusively for per-session diagnostic/debug builds.

**Keyboard shortcut map (current as of v0.14.105):**
`` ` `` = repeat dialog/battle event | `V` = mod version | `F1` = cycle voice | `F2` = toggle audio ducking | `F3`/`F4` = speech rate down/up | Shift+`F3`/`F4` = speech volume down/up | `F5`/`F6` = SFX volume down/up | `F7`/`F8` = BGM volume down/up | `F9`/`F10` = field nav | `F11` = on-demand screenshot capture | `F12` = DIAGNOSTIC BUILDS ONLY | `G/T/L/R` = Gil/Time/Location/SeeD | `/` = help bar | `O` = EWM toggle | `1/2/3/H` = battle HP check | `M` = menu summary | `\` = world map auto-drive | `A` = gas pedal (car only) | `W` = reverse (car only). **All F-key accessibility handlers gated on `!alt`** so Alt+combos (notably Alt+F4) don't fire them.

---

**Tools & resources**

- **Filesystem MCP tools**: All project file access. Never bash for project files.
- **FFNx canary source**: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\` — read-only reference.
- **Game files**: `Game Files\FINAL FANTASY VIII\`.
- **Full disassembly reference**: `Game Files/disassembly/`.
- **Plan & Research Documents/**: Deep research docs.
- **Community references**: nightsolo.net, finalfantasy.fandom.com, ff8-speedruns/ff8-memory, myst6re/deling, Qhimm Modding Wiki.
- **Reusable OpenGL screenshot capture**: Only `glReadPixels` via SwapBuffers hook works.
- **Known issue (not a mod bug)**: JAWS intercepts game keys until user presses Insert+3. NVDA unaffected.

---

**Closed issues this session:** none yet (v0.14.106 not yet BAT'd; the speech-rate persistence bug was a session-introduced fix, no GitHub issue existed).
