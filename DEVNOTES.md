# DEVNOTES — FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-04-06 (session 38 — file splitting + multi-channel logging)

> **File structure**: This file = current state + key learnings only (~10KB max).
> Build history in `DEVNOTES_HISTORY.md`. Immediate context in `NEXT_SESSION_PROMPT.md`.

---

## Current Build: v0.12.18

### What's new in v0.12.18
- **Multi-channel logging**: 6 domain-specific log files (`ff8_mod.log`, `ff8_battle.log`, `ff8_field.log`, `ff8_world.log`, `ff8_menu.log`, `ff8_dialog.log`). Auto-archives old logs to `Logs/archive/` with timestamps on game launch.
- **Source file splitting**: 4 large source files split into main files + `.inl` textual-include sections. See file layout below.
- **Log migration**: ALL `Log::Write()` calls migrated to domain-specific functions (`Log::Field()`, `Log::Battle()`, `Log::Menu()`, `Log::World()`, `Log::Dialog()`, `Log::Mod()`).

---

## Source File Layout (post-split)

### field_navigation.cpp (48KB) + 13 .inl files
| File | Contents |
|------|----------|
| field_navigation.cpp | State variables, Initialize, Update, Shutdown |
| field_nav_catalog.inl | RefreshCatalog |
| field_nav_autodrive.inl | Auto-drive, steering, recovery |
| field_nav_fieldscripts.inl | HookedFieldScriptsInit |
| field_nav_pathfinding.inl | A* search, funnel, BFS |
| field_nav_handlekeys.inl | HandleKeys dispatch |
| field_nav_opcode_hooks.inl | SETLINE/TALKRAD/SET3/PSHM hooks |
| field_nav_input_hooks.inl | DinputGamepad, EngineEval, GetKeyState hooks |
| field_nav_announce.inl | AnnounceCurrentTarget, Directions, CycleEntity |
| field_nav_helpers.inl | GetEntityPos, ReadVertexCoords, etc. |
| field_nav_gps.inl | GPS guided navigation |
| field_nav_settriangle.inl | HookedSetCurrentTriangle |
| field_nav_names.inl | SYM resolution, display names, classification |
| field_nav_diagnostics.inl | DumpPshmFunctions, PollDescriptorTable |

### battle_tts.cpp (25KB) + 5 .inl files
| File | Contents |
|------|----------|
| battle_tts.cpp | State, speech priority, entity helpers, battle lifecycle, Init/Update/Shutdown |
| battle_tts_ewm.inl | EWM, GF fire prevention, ATB/FFNx hooks |
| battle_tts_menu.inl | Turn/command menu, magic/GF/item/draw sub-menus |
| battle_tts_hp.inl | HP tracking, damage announce, target selection |
| battle_tts_diagnostics.inl | Menu diagnostic, cursor hunter, enemy cache |
| battle_tts_helpers.inl | Enemy names, text decoder |

### menu_tts.cpp (33KB) + 5 .inl files
| File | Contents |
|------|----------|
| menu_tts.cpp | State, constants, item names, Initialize, Update |
| menu_tts_save.inl | Save screen TTS |
| menu_tts_diagnostics.inl | Diagnostics, memory monitor, SUBMON |
| menu_tts_item.inl | Item submenu TTS |
| menu_tts_junction.inl | GCW decoder, junction TTS |
| menu_tts_hotkeys.inl | Help bar, Gil, Time, Location, SeeD |

### field_archive.cpp (44KB) + 1 .inl file
| File | Contents |
|------|----------|
| field_archive.cpp | Infrastructure, public API functions |
| field_archive_jsm.inl | JSM scanner, DumpEntityScript |

### Other source files (unsplit)
- dinput8.cpp (23KB), screen_reader.cpp (23KB), game_audio.cpp (28KB)
- world_map.cpp (53KB), field_dialog.cpp (78KB), ff8_addresses.cpp (70KB)
- ff8_text_decode.cpp (16KB), fmv_skip.cpp (16KB), fmv_audio_desc.cpp (15KB)
- nav_log.cpp, name_bypass.cpp, title_screen.cpp (small)

---

## Key Technical Learnings (Permanent)

### Critical Rules
- **NEVER re-enable the SET3 opcode hook (opcode 0x1E)** — hangs infirmary scene. CI guard in `.github/workflows/safety-checks.yml`.
- **PSHM_W positions**: All 7 approaches exhausted for `dic`. Engine never runs scripts for entities beyond active window. Shift-pattern passthrough (~494 units off) is the best approximation; SETLINE center override is the correct approach.
- **Savemap offset correction**: ChatGPT deep research assumes 96-byte header; confirmed header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets.
- **Analog steering**: Keyboard direction dominates when both active. World map uses `keybd_event()`. Field maps use calibrated camera-relative projection for analog steering.

### Key Addresses & Structures
- Entity struct: offsets 0x190/0x194/0x198 = 32-bit fixed-point world coords (4096× scale)
- NPC int16 positions at 0x20/0x28; talk radius at 0x1F8, push radius at 0x1F6
- `pFieldStateOthers` stride 0x264; `pFieldStateBackgrounds` stride 0x1B4
- Battle display struct at `0x1D8DFF4`; entity array at `0x1D27B18` stride 0xD0
- Savemap base: `0x1CFDC5C`; GF fire patch at `0x004B04B4`
- Entity sentinel ranges: -200..-299 = trigger lines, -300..-399 = JSM-injected, -400+ = gateways

### Architecture Notes
- JSM opcode encoding (PC): native little-endian uint32, high byte = opcode index, low 24 bits = signed parameter
- 47.5% of FF8 fields have disconnected walkmesh islands
- `.ff8` save files: LZSS compressed (N=4096, F=18, THRESHOLD=2, ring buffer init 0x00, write pos 0xFEE); 384-byte header before savemap at decompressed +0x180
- INF gateway data is vestigial PS1 — unreliable for positions. JSM-based exit detection is correct.
- EWM pre-cap sandwich preserves Speed-based ATB economy; blacklist of executing phases (14, 21, 23, 33, 34)

---

## Completed Feature Milestones
- Title screen TTS (v0.02) ✓ | FMV audio desc + skip (v0.03) ✓
- Field dialog TTS (v0.04.36) ✓ — MES/ASK/AMES/AASK/AMESW/RAMESW all hooked
- Junction/Item/Magic/Draw sub-menu TTS ✓ | Battle command + EWM + GF fire prevention ✓
- Field entity catalog + auto-drive ✓ | World map navigation (v0.11.16) ✓
- Naming bypass + GF acquisition TTS (v0.09.21) ✓ | Field GPS navigation (v0.12.06) ✓
- Multi-channel logging + file splitting (v0.12.18) ✓

---

## Recovery Notes

1. Read this file FIRST for current state
2. Read `NEXT_SESSION_PROMPT.md` for immediate next steps
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details
4. **Use filesystem MCP tools (not bash) for Windows file access**
5. `deploy.bat` is the ONLY build script
6. Version bump in **four locations**: `FF8OPC_VERSION` in `ff8_accessibility.h`, header + Initialize() log in `field_navigation.cpp`, header + Initialize() log in `battle_tts.cpp`
7. "BAT" = Aaron has Built And Tested; read tail of game log
8. F12 is reserved for per-session diagnostic builds — search all sources for existing VK_F12 before hooking
9. Log channels: `Log::Field()`, `Log::Battle()`, `Log::Menu()`, `Log::World()`, `Log::Dialog()`, `Log::Mod()`. `Log::Write()` still works (routes to ff8_mod.log).
