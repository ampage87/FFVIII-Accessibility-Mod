# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-17

> **File structure**: This file contains current state + key learnings only.
> Detailed build history (v05.47–v06.22, v07.00–v07.15) is in `DEVNOTES_HISTORY.md`.

---

## CURRENT OBJECTIVE: In-Game Menu TTS + Save Screen (v07.xx)

### Current build: v0.07.24 (source + deployed)

### Menu TTS (WORKING — v07.04+)

**Module**: `src/menu_tts.cpp` / `src/menu_tts.h`

**Top-level cursor**: BYTE at `pMenuStateA + 0x1E6` = absolute `0x01D76C80`
- Values 0–10: Junction, Item, Magic, Status, GF, Ability, Switch, Card, Config, Tutorial, Save
- Game remembers last position when reopening menu

**Key addresses**: `pMenuStateA`=`0x01D76A9A`, `main_menu_controller`=`0x004E3090`, `menu_callbacks`=`0x00B87ED8`

### Save Screen Cursor (BLOCKED)

The save/load screen cursor address is unknown. Not in pMenuStateA region (4KB scan confirmed), not in GCW text (identical regardless of cursor), not in ff8_win_obj. Cursor is a graphical hand icon sprite.

**Awaiting**: ChatGPT deep research results. Prompt at `Plan Documents/Save Screen Cursor - ChatGPT Research Request.md`.

**Fallback approaches**: Wide memory scan (VirtualQuery), menu_draw_text Y-coordinate capture, or save screen controller function hook.

### Menu Font Decoder (WORKING — v07.11)

`DecodeMenuText()` in `ff8_text_decode.h/.cpp`. 224-entry sysfnt glyph table from myst6re/deling.
Key: space=0x00, digits=0x01–0x0A, A–Z=0x25–0x3E, a–z=0x3F–0x58. Ref: `src/sysfnt_chartable.txt`.

### BGM Volume Control (WORKING — v0.07.24)

**Hook target**: `pSetMidiVolume` — resolved at runtime from `main_loop+0x487 → sm_battle_sound+0x173`.
FFNx replaces this with `set_music_volume_for_channel(int32_t channel, uint32_t volume)` (0-127 per channel).
Our hook scales volume by user setting. F3=down 10%, F4=up 10%. Default 20%.

**Key learning**: The credits-only function at `0x0046C6F0` (`dmusicperf_set_volume_sub_46C6F0`) does NOT control field/battle/worldmap music. The game uses `common_externals.set_midi_volume` for all normal music.

**Code**: `dinput8.cpp` — `TryInstallVolumeHookOnFFNx()`, `HookedSetMusicVolumeForChannel()`, `SetGameAudioVolume()`

---

## PREVIOUS MILESTONES

- **v0.07.24**: BGM volume persistence fix (GitHub issue #1)
- **v0.06.22**: Field navigation — auto-drive with A* pathfinding, SSFA funnel, heading calibration, talk radius expansion
- **v0.04.36**: Field dialog TTS — all MES/ASK/AMES/AASK/AMESW/RAMESW opcodes
- **v0.03.00**: FMV audio descriptions + skip
- **v0.02.00**: Title screen TTS

---

## ARCHITECTURE

### Module System (dinput8.cpp)
AccessibilityThread polls game state ~60Hz. Modules: TitleScreen, FieldDialog, FieldNavigation, FmvAudioDesc, FmvSkip, MenuTTS.

### Address Resolution (ff8_addresses.cpp)
Offset-chain resolution following FFNx's pattern. All 14 FF8 PC language variants supported.

### Key Source Files
| File | Purpose |
|------|---------|
| dinput8.cpp | DLL proxy entry + game loop + module dispatch |
| ff8_addresses.h/cpp | Runtime address resolution |
| menu_tts.cpp/h | In-game menu + save screen TTS |
| field_navigation.cpp | Entity catalog, auto-drive, A* pathfinding |
| field_dialog.cpp/h | Opcode dispatch hooks for field dialog TTS |
| field_archive.cpp/h | fi/fl/fs archive reader for SYM/INF/JSM/walkmesh |
| ff8_text_decode.cpp/h | FF8 field + menu font encoding → UTF-8 |
| screen_reader.cpp | NVDA direct + SAPI fallback TTS |
| fmv_audio_desc.cpp/h | FMV audio descriptions (WebVTT cues) |
| fmv_skip.cpp/h | FMV skip via Backspace (ReadFile EOF hook) |
| title_screen.cpp | Title menu cursor TTS |
| deploy.bat | Build + deploy (ONLY build script) |

---

## KEY LEARNINGS

### Entity & Coordinate System
- Entity screen-vertical is Y (offset 0x194), NOT Z (0x198). Z is always ~0.
- Walkmesh .id coordinates ARE entity 2D coordinates — no projection needed.
- World coords (fixed-point ×4096): X=0x190, Y=0x194, Z=0x198. Player triangle: 0x1FA.
- Talk radius: offset 0x1F8 (uint16). Push radius: 0x1F6 (uint16, default 48).

### SETLINE Triggers
- INF trigger section is BOGUS on PC — real data from SETLINE opcode at runtime.
- Line coords at entity offset 0x188: 6×int16 (X1,Y1,Z1,X2,Y2,Z2) + lineIndex.
- Line entities have separate state structs (not in pFieldStateOthers/Backgrounds).

### Entity Naming
- Model ID 0–8 overrides SYM for main character names. SYM names are slot names.
- SYM offset = 0 for others, otherCount for backgrounds.

### Auto-Drive / Steering
- Analog input is camera-relative, not world-relative. Per-field heading calibration required.
- Hook get_key_state to zero arrow scancodes — keyboard direction dominates analog when both active.
- Fake gamepad device sentinel (0xDEAD0001) + fake DIJOYSTATE2 buffer for analog injection.
- Walkmesh inline format needs vertex dedup at load time (numVertices=0 in raw file).
- SSFA portal assignment: use triB center cross product, not travel direction.
- Talk radius expansion (2.5×) solves last-mile interaction gap.

### Menu / Save Screen
- Menu font (sysfnt) encoding differs from field dialog (Ifrit). Separate decoder.
- Save screen cursor is a graphical sprite, not in text stream or pMenuStateA region.
- Save screen runs in mode 1, does NOT use mode 6 or ff8_win_obj.

### Build System
- `deploy.bat` is the ONLY build script. `deploy.ps1`/`deploy.vbs` are UI wrappers.
- Version: bump `FF8OPC_VERSION` in `ff8_accessibility.h`, init log in source, file header.
- Logs in `Logs/` subfolder.

### Misc
- FFNx replaces get_key_state and dinput_update_gamepad_status — our hooks chain through.
- Walk-and-talk dialog is hardcoded engine path (deferred).
- JAWS intercepts game keys in fullscreen DirectX (Insert+3 for passthrough). NVDA unaffected.

---

## KEY FILE LOCATIONS

- Project root: `C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod\`
- Source: `src\`
- FFNx reference: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\`
- Game folder: `C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\`
- Logs: `Logs\` (ff8_accessibility.log, ff8_nav_data.log, build_latest.log)
- Build history: `DEVNOTES_HISTORY.md`

---

## RECOVERY NOTES

1. Read this file FIRST for current state
2. Read `NEXT_SESSION_PROMPT.md` for immediate next steps
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details or investigation narratives
4. Use filesystem MCP tools (not bash) for Windows file access
5. `deploy.bat` is the ONLY build script
6. Current version: v0.07.24 (source + deployed)
7. "BAT" = read tail of `Logs/ff8_accessibility.log`
8. GitHub repo: ampage87/FFVIII-Accessibility-Mod
