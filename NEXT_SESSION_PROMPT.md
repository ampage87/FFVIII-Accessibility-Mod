# Next Session Priority: Save Screen Cursor + Source Push

## Current State (v0.07.24)

Build is stable and deployed. BGM volume control fully working.
Menu TTS working (top-level). Save screen cursor BLOCKED.
GitHub issue #1 (BGM volume persistence) CLOSED.
Logs are in `Logs/` subfolder.

### Session 2026-03-17 Major Achievement

**GitHub Issue #1 — BGM Volume Persistence: FIXED (v0.07.24)**

Root cause: We were hooking `dmusicperf_set_volume_sub_46C6F0` (0x0046C6F0),
a function only called during game credits. Field/battle/worldmap music uses
`set_midi_volume` (`common_externals.set_midi_volume`), which FFNx replaces
with `set_music_volume_for_channel(int32_t channel, uint32_t volume)`.

Fix: Resolved `set_midi_volume` via FFNx's address chain
(`main_loop → sm_battle_sound → set_midi_volume`). Hooked FFNx's replacement
with correct 2-parameter signature. All game volume calls now flow through
our hook. No periodic re-apply needed.

Investigation timeline: v0.07.17–v0.07.24 (8 builds).

### What's Working
- **BGM volume control**: F3/F4 adjust, default 20%, persists across scenes
- Top-level menu cursor TTS (mode 6, all 11 items)
- DecodeMenuText() — correctly decodes menu/save screen glyph codes
- Save screen detection via GCW text content
- Field dialog TTS (all opcodes)
- Title screen TTS
- FMV audio descriptions + skip
- Field navigation with auto-drive

### What's NOT Working / Blocked
- **Save screen cursor detection**: Cursor address unknown. Not in pMenuStateA
  region. Not in GCW text. Awaiting deep research results.
- **Save block list navigation**: Depends on cursor detection.

## Next Session Priorities

### 1. Push Source to GitHub (IMMEDIATE)
Source changed significantly (ff8_addresses.h/.cpp, dinput8.cpp,
ff8_accessibility.h). Needs full push to main branch.

### 2. Process Deep Research Results (HIGH — if available)
Check if ChatGPT deep research found save screen cursor address.
Research prompt at `Plan Documents/Save Screen Cursor - ChatGPT Research Request.md`.

### 3. Bug Fixes (as reported by Aaron)

### 4. Menu Submenu TTS (MEDIUM — after save screen)

## Volume Control Architecture (v0.07.24)
- **F3** = music volume down 10%, **F4** = music volume up 10%
- Default: 20% (`s_gameVolume = 0.2f`)
- Hook target: `pSetMidiVolume` (resolved at runtime from main_loop → sm_battle_sound)
- FFNx replaces this with `set_music_volume_for_channel(channel, volume)` (0-127, per channel)
- Our hook scales volume by user setting before passing through
- No periodic re-apply — game calls this function for ALL music volume changes

## Key Code Locations
- `src/dinput8.cpp` — volume hook: `TryInstallVolumeHookOnFFNx()`, `HookedSetMusicVolumeForChannel()`
- `src/ff8_addresses.h/.cpp` — `pSetMidiVolume` (resolved from sm_battle_sound+0x173)
- `src/ff8_accessibility.h` — version constant
- `src/menu_tts.cpp` — menu TTS + save screen detection
- `src/ff8_text_decode.h/.cpp` — text decoders
- `deploy.bat` — ONLY build script

## Recovery Instructions
1. Read DEVNOTES.md for full architecture
2. Read this file for immediate context
3. Use filesystem MCP tools for Windows files (not bash)
4. `deploy.bat` is the ONLY build script
5. Bump FF8OPC_VERSION in ff8_accessibility.h on every build
6. When Aaron says "BAT" → read tail of `Logs/ff8_accessibility.log`
7. Versioning: 0.MM.BB format (pre-production). First public release = 1.0.0.
