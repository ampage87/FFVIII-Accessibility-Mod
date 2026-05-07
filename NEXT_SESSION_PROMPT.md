# Next Session Prompt — FF8 Accessibility Mod

## Status at handoff

**v0.14.106 is built locally, AWAITING BAT.** Pairs with v0.14.105's Alt+F4 fix. Both ready to push to GitHub together once v0.14.106 BATs. `main` HEAD on GitHub is still `e45b1a4f` (v0.14.104).

## What just happened (last session summary)

Persistent settings session, both items resolved:

**Item #2 (audio ducking persistence)**: Already done in v0.14.45. Verified working in v0.14.105 BAT log.

**Item #1 (speech rate persistence)**: Aaron diagnosed the actual root cause mid-session — Alt+F4 close was firing `IncreaseRate()` on the way out because the F4 handler wasn't gated on Alt. v0.14.105 added the `!alt` gate to every F-key accessibility handler (F1–F8, F11). BAT'd successfully: rate=7 saved, restarted, log showed `Config loaded rate=7`, F4 then went to 8. All `[RATE-DIAG]` lines showed shadow==SAPI rate=7 throughout init, also confirming SAPI's `SetVoice()` preserves rate/volume.

**v0.14.106 is the cleanup pass plus a polish item:**

1. Stripped the v0.14.105 `LogActualSAPIState` diagnostic harness (helper + 5 instrumentation points + defensive re-apply block).
2. Added a commented INI template — `config.cpp` now writes a documented `ff8_accessibility.ini` with each setting explained, ranges noted, and in-game shortcuts referenced. Auto-upgrades existing un-commented INIs (Aaron's included) on next launch, preserving all current values.

All persistence verified working: speech_rate, speech_volume, speech_voice_id (DAVID), game_volume, sfx_volume, tts_duck_enabled, sfx_duck_ratio, ewm_enabled.

## v0.14.106 BAT plan

1. Run `deploy.vbs`.
2. Launch the game once.
3. Listen for the welcome announcement at your saved rate (no behavior change expected).
4. Mod log should report `Config: Existing INI lacks comment header; upgrading...` followed by `Wrote commented INI template to ...`.
5. Quit (Alt+F4 or otherwise — both are safe now).
6. Open `ff8_accessibility.ini` in a text editor and verify the commented template is in place with current values populated.
7. Launch a SECOND time. Log should NOT say "lacks comment header" (idempotent — only upgrades once).
8. Optional: tweak any setting via in-game shortcut and confirm the value updates in-place without disturbing the comments.

After successful BAT, push v0.14.105 + v0.14.106 to GitHub via `Utilities/push_to_github.bat`. Suggested commit description for the push:

```
v0.14.105 + v0.14.106: Fix speech rate persistence + INI template

v0.14.105: Speech rate (and other accessibility settings) were creeping up
by 1 each session because Alt+F4 close was firing the F4 IncreaseRate()
handler on the way out. Every F-key accessibility handler (F1-F8, F11) is
now gated on !alt (GetAsyncKeyState(VK_MENU)) so Alt+combos no-op. Also
added a one-shot LogActualSAPIState diagnostic to confirm the fix; BAT
verified persistence is sound and SAPI preserves rate/volume across voice
changes.

v0.14.106: Strip the diagnostic harness now that the fix is verified. Add
a human-readable commented template to ff8_accessibility.ini explaining
each setting, its range, and the in-game shortcut. Existing INIs auto-
upgrade on next launch (preserving all values). New helpers in config.cpp:
HasTemplateMarker(), EnsureTemplate().
```

## Top of mind for next session

After v0.14.106 ships and pushes, the deferred priority list returns to:
1. Remove party members from field entity catalog.
2. X-ATM092 chase scene accessibility.
3. Walk-and-talk dialog gap.
4. SeeD rank bug (#27) investigation.
5. Refined-coord steering for narrow-gate locations.
6. Fire Cavern entry trigger (#28) and planner-fallback (#29).

## Open GitHub issues at session end (16 total)

Carryover: #2, #3, #5-10, #15, #18-22, #25, #26 (PR), #27, #28, #29.

## Persistent rules (do not break these)

- `## Claude Says` prefix on every response.
- Filesystem MCP for Windows project files; bash only for Linux container.
- Claude NEVER pushes to GitHub. Aaron uses `Utilities/push_to_github.bat`.
- F12 reserved for diagnostic builds only.
- SET3 opcode hook is permanently disabled.
- BAT workflow: check `Logs/build_latest.log` first, then domain-specific log.
- Update DEVNOTES + NEXT_SESSION_PROMPT at every version bump and after every BAT.
- Always check `Plan & Research Documents/` AND past conversations BEFORE proposing new logic.
- Always call `github:list_commits` before quoting GitHub state.
- For "used to work before Sonnet regression": ALWAYS `conversation_search` BEFORE writing new logic.
- When BAT log seems to show absence of feature exercise, ASK Aaron rather than assuming.
- Locomotion byte at 0x02040A5E does NOT reliably indicate rental car state. Use `car_rent` flag at 0x01CFEF1A.
- Don't try to detect bouncing as frozen-position; cars oscillate.
- Verify engine-internals assumptions empirically before relying on them.
- Search log format strings by unique fragments, not bracket prefixes.
- Accessibility wording: prefer concrete action verbs over abstractions.
- `dryRun=true edit_file` works as grep substitute for finding code locations.
- When `oldText` matches only a partial line, the trailing fragment is preserved as-is — use full lines for replacements, or follow up with a cleanup edit.
- **Accessibility hotkeys must be gated against modifier-key combinations** (notably `Alt`) to avoid window-management combos like Alt+F4 firing the same key as the bare keypress. v0.14.105 hit this with the F4 IncreaseRate handler.

## Reference: persistence layer (Config)

- `src/config.cpp` + `config.h`. INI at `<dll-dir>/ff8_accessibility.ini`, section `[Accessibility]`.
- Currently persisted: `speech_rate`, `speech_volume`, `speech_voice_id`, `game_volume` (BGM), `sfx_volume`, `tts_duck_enabled`, `sfx_duck_ratio`, `ewm_enabled`.
- v0.14.106 auto-generates a commented INI template with documentation and ranges. Legacy INIs auto-upgrade on launch.
- `WritePrivateProfileStringA` preserves comments line-by-line on in-place updates.
- SAPI's `SetVoice()` preserves rate/volume across voice changes — empirically confirmed.

## Reference: keyboard shortcuts (current as of v0.14.105)

`` ` `` = repeat | V = version | F1 = cycle voice | F2 = toggle ducking | F3/F4 = speech rate down/up | Shift+F3/F4 = speech volume down/up | F5/F6 = SFX volume down/up | F7/F8 = BGM volume down/up | F9/F10 = field nav | F11 = screenshot capture | F12 = diagnostic builds only | G/T/L/R = Gil/Time/Location/SeeD | `/` = help bar | O = EWM toggle | 1/2/3/H = battle HP | M = menu summary | `\` = world map auto-drive | A = gas | W = reverse. **All F-key accessibility handlers gated on `!alt`.**
