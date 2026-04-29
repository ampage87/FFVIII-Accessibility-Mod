# Next Session Prompt — FF8 Accessibility Mod

## Current state at session start

Build: **v0.14.44 — BAT PASSED.** GF summon audio descriptions are working in-game.

Aaron's BAT report (session 74): triggered Ifrit and Shiva — both audio descriptions announced correctly. Trigger fires from `PollBattleMagicId()` in `battle_tts_ewm.inl`, cues stream on Channel 2 (event voice), playback stops cleanly when `battle_magic_id` reverts.

**One usability issue surfaced during BAT:** the game's SFX (the GF animation sounds — Ifrit's roar, Shiva's ice shatter, etc.) are loud enough to mask the TTS narration. Aaron flagged this as the next priority.

## Top priority — SFX volume control + duck during TTS

### Background

The existing `GameAudio` module (`src/game_audio.cpp`) hooks FFNx's `set_music_volume_for_channel` and bypasses FFNx's `hold_volume_for_channel` flag by calling `nxAudioEngine.setMusicVolume` directly via a `__fastcall` thiscall shim. F3/F4 are the BGM volume keys. This is BGM only — SFX is on a parallel path.

FFNx uses **SoLoud** as the underlying audio engine (visible in `game_audio.cpp` references to `SoLoud::Soloud::fadeVolume`). SoLoud handles SFX through a separate volume bus or per-handle volume.

### Phase 1 — SFX volume keyboard shortcut (do this first, scoped narrowly)

Goal: a persistent SFX volume Aaron sets once via hotkeys, stored in `ff8_accessibility.ini` like BGM volume.

Implementation sketch:
1. **Identify FFNx's SFX-volume entry point.** Likely candidates: `set_sfx_volume`, `set_sfx_volume_trans`, or a SoLoud bus volume call. Search FFNx canary source at `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/` — `ff8_data.cpp` and `audio.cpp` are the likely files. The pattern from `set_music_volume_for_channel` should give a template for finding the SFX equivalent (look for `nxAudioEngine.setSfxVolume` or direct SoLoud bus calls).
2. **Hook it the same way.** Extract the nxAudioEngine pointer + setSfxVolume address by scanning the function bytes (the existing `ExtractNxAudioEngineAddresses` in `game_audio.cpp` is the template).
3. **Add `SfxVolumeUp` / `SfxVolumeDown` to the `GameAudio` namespace.** Step by 10% per press, announce via TTS, persist to config.
4. **Wire keys** in `dinput8.cpp` keyboard block. Shift+F3 / Shift+F4 (mirroring the BGM controls) is the natural choice. Verify the field nav and other modules don't already use Shift+F3/F4. Update the keyboard shortcut map in DEVNOTES.

### Phase 2 — Auto-duck during TTS (do this after Phase 1 lands)

Goal: while TTS is actively speaking on Channel 1 or Channel 2, drop SFX to ~30% of Aaron's set volume; restore on speech end.

Implementation sketch:
1. **Detection:** `ScreenReader::IsSpeaking()` already exists (used by `EWM_UpdateBattle` for damage TTS hold).
2. **In `GameAudio::Update()`** (already runs every frame), poll `IsSpeaking()`. On rising edge, drop SFX volume to `userSfxVolume * 0.3`. On falling edge, restore to `userSfxVolume`. Add a small grace period (~250ms) before restoring to avoid rapid up/down dipping during sentence boundaries.
3. **Make the duck ratio configurable** in `ff8_accessibility.ini` (`sfx_duck_ratio`, default 0.3). Some users may want stronger/weaker ducking.
4. **Make the auto-duck togglable.** A config key (`sfx_autoduck_enabled`, default 1) lets Aaron disable it if it interferes with normal play. No keyboard shortcut for the toggle in v1 — just the config file.

### Open questions for Phase 2

- **Should auto-duck apply to ALL TTS, or only certain channels?** Channel 2 (event voice — used for FMV AD, GF AD, dialog) is where the SFX-mask problem is worst. Channel 1 (menu nav) speech is typically over quieter UI sound. Could start with "all TTS" simplicity and split later if it feels off.
- **GF AD specifically might want a stronger duck** (~10-15% rather than 30%) since the GF SFX is the loudest in the game. Might be worth a per-context override.

## After SFX work

Backlog from earlier sessions:
1. Persistent accessibility settings across play sessions (general — beyond just the SFX/BGM volumes)
2. Verify GF naming bypass — Siren failed in earlier testing
3. Remove party members from entity catalog
4. X-ATMO92 chase scene accessibility
5. Bug 3 (Magic/GF auto-announce inconsistent)
6. Bug 4 (key 2 announced GF Shiva instead of Squall HP)
7. Quistis Blue Magic ordering, Draw "???" reveal
8. World map: vehicle-aware BFS, guided GPS mode, auto-announce location names
9. Boko Choco / Minimog / Moomba / Gilgamesh VTTs (extension of v0.14.44)
10. Per-GF AD timing tuning based on continued in-game listening

## GitHub push for v0.14.44

GF AD is BAT-passed. Push to GitHub via `Utilities/push_to_github.vbs` whenever you want — recommend doing it before the SFX work starts so the GF AD lands as its own clean commit, separate from the audio mixing changes.

## Required reading at session start

1. `DEVNOTES.md` (project root)
2. This file
3. `Logs/build_latest.log` tail — confirm any new build is clean
4. `src/game_audio.cpp` — read the `ExtractNxAudioEngineAddresses` block to understand the hook pattern before mirroring it for SFX
5. `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/audio.cpp` (or equivalent) — to find SFX volume entry points

## Workflow rules in effect

- **Filesystem MCP tools only** — never bash for project files. Bash sees `/C:/...` which is a separate container-local filesystem; the system `create_file` tool writes there. Real Windows files must use `filesystem:write_file` / `filesystem:edit_file` at `C:/...` (no leading slash).
- **Update DEVNOTES + NEXT_SESSION_PROMPT** at every version bump and BAT
- **"BAT" = Built and Tested.** Check `Logs/build_latest.log` tail first, then domain log
- **Version bump in 1 location** — `FF8OPC_VERSION` in `src/ff8_accessibility.h`
- **Aaron is blind** — every response starts with `## Claude Says`
- **Don't declare a fix successful from log markers alone** — verify against Aaron's user-facing experience
- **NEVER re-enable the SET3 opcode hook (0x1E)** — hangs infirmary scene. CI guard active
- **F12 reserved for diagnostics** — search/remove old VK_F12 refs before adding new

## v0.14.44 file inventory (BAT-passed)

New files:
- `src/gf_audio_desc.h`
- `src/gf_audio_desc.cpp`
- `Audio Descriptions/gf_quezacotl.vtt` ... `gf_odin.vtt` (18 files)

Modified files:
- `src/ff8_accessibility.h` (version + comment)
- `src/resources.h` (IDR_VTT_GF_BASE = 6000)
- `src/resources.rc` (18 RCDATA entries)
- `src/dinput8.cpp` (Initialize/OnFrame/Shutdown)
- `src/battle_tts.cpp` (forward decl of GfAudioDesc::OnGFAnimationStart)
- `src/battle_tts_ewm.inl` (trigger from PollBattleMagicId)
- `src/deploy.bat` (added gf_audio_desc.cpp to build)
- `.gitignore` (added GitHub Push.lnk shortcut)

GF AD timing tuning is a slow rolling task — Aaron will hear cues land too early/late as he plays through the game and trigger more summons. Edits to the .vtt files in `Audio Descriptions/` are picked up at the next rebuild (they're embedded as RCDATA resources).
