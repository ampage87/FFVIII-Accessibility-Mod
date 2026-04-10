# DEVNOTES — FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-04-09 (session 45 — V key version announce, quieter startup)

> **File structure**: This file = current state + key learnings only (~10KB max).
> Build history in `DEVNOTES_HISTORY.md`. Immediate context in `NEXT_SESSION_PROMPT.md`.

---

## Current Build: v0.12.25

### What's new in v0.12.25
- **Startup speech simplified**: Now says "Final Fantasy 8 Accessibility Mod loaded." instead of including the version number. Prevents the announcement from talking over the Square Electronic Arts FMV audio description that plays immediately on game launch.
- **V key announces mod version**: Pressing V speaks "Version X.XX.XX" on demand. Added to the keyboard shortcut block in dinput8.cpp. No conflict with FF8 defaults or existing mod shortcuts.

### What's new in v0.12.22-23
- **POPM_W/B/L varblock write capture hooks**: MinHook on three internal write handlers. Captures all varblock writes for 10s after field load. Result: only 1 trivial write on bgryo1_4 — Director never executes, no entity writes interaction zone coordinates.
- **Walkmesh dead-end detection prototype**: BFS clusters dead-end triangles (1 neighbor) through narrow passages (<=2 neighbors). On bgryo1_4: 158 tris, 15 clusters, 8 significant (>=2 tris). Minimum cluster size filter reduces noise.
- **Party character name filter**: Director target promotion now skips squall/zell/selphie/quistis/rinoa/irvine/seifer/edea/laguna/kiros/ward. Promotions dropped from 7 to 3 on bgryo1_4.
- **Target entity init script dumps**: Unpositioned Director-promoted targets now get their full init script dumped for analysis.
- **Deep research received (session 43)**: Interaction zone coordinates are in each TARGET entity's own init script as PSHN_L literals before SETLINE/SET3/TALKRADIUS. Engine runs init scripts for ALL entities on field load. Director pattern is redundant dead code.
- **Deep research partially wrong for bgryo1_4**: Target Others entities (kigaeyarou/dic/el1) have NO SETLINE/SET3/TALKRADIUS in their init scripts — only SETMODEL+BASEANIME. Background entities ('squalls', 'squallsd') also have empty init scripts — they control BGDRAW/BGOFF layer visibility only. NO entity on bgryo1_4 has interaction zone coordinate literals in any init script. The Director (seed) is the ONLY entity with proximity logic, but it never executes.
- **JSM entry point bit15 fix**: Entry point table values for Door/Line/Background entities have bit 15 set as a flag. Masking `& 0x7FFF` fixes parsing — Background entity scripts at dword ~494 were invisible when read as dword 33262. Fix applied to both ScanJSMScripts and DumpEntityScript. Side effect: Line entity classification may change on some fields (e.g. bgryo1_4 Line 'squall' changed from CamPan to MapExit).
- **Open mystery SOLVED**: Interactions on shared dormitory fields are triggered by **Line entity SETLINE triggers**, NOT by Others entity proximity. Confirmed by F12 on-demand POPM_W capture: player at (-903,237) interacted with "Uniform" dialog while SETLINE center at (-858,230) — only 45 units away. Zero POPM_W writes, zero entity position data needed. The Line entity's script fires when the player crosses the SETLINE geometry, then the script shows the dialog (AASK). Director entity is dead code on these fields. **SETLINE hook data is the definitive source for interaction zone positions.** On bgryo1_4: 1 Line/1 SETLINE = uniform interaction. On bgryo1_1: 3 Lines/3 SETLINEs = bed/desk/wardrobe interactions. Next: associate SETLINE positions with interactive object names.

### What's new in v0.12.19
- **F12 JSM script dump**: Diagnostic now dumps decoded JSM scripts for all unclassified Others entities, revealing interaction patterns.
- **Director-dispatched interaction pattern discovered**: Dormitory bed/desk/wardrobe and classroom desk/sign are NOT standalone interactive objects. They're dispatched by a Director entity that runs position checks and invokes dialog via REQ. Positions embedded as PSHM_W reads in repeating blocks. Pattern confirmed on `bgryo1_4` (Director=`seed`) and `bgroom_1` (Director=`door`).

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
- **Director-dispatched interactions**: Dormitory bed/desk, classroom desk/sign use a Director entity pattern (invisible Others, no SETMODEL, method with REQ calls + PSHM position checks). Positions embedded as PSHM_W reads in repeating blocks with literal radius (typically PUSH 100). NOT the same as standalone interactive objects.
- **Director script pattern** (decoded v0.12.20): Director method[1] has repeating blocks: `PSHM_L(720)` = player pos, `PSHM_W(0/1/2)` = zone index, `JMP/JMPB` = proximity check, `PSHM_W(3)` = target entity (runtime party slot), `PSHM_W(58/62/63/64)` = target method, `REQ` = dispatch. All positions are runtime memory — no static literals. `PSHM_W(145)` matches dic’s SET3 X coordinate address. Director init methods are typically empty (LBL+RET).
- **Savemap offset correction**: ChatGPT deep research assumes 96-byte header; confirmed header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets.
- **Analog steering**: Keyboard direction dominates when both active. World map uses `keybd_event()`. Field maps use calibrated camera-relative projection for analog steering.

### Key Addresses & Structures
- Entity struct: offsets 0x190/0x194/0x198 = 32-bit fixed-point world coords (4096× scale)
- NPC int16 positions at 0x20/0x28; talk radius at 0x1F8, push radius at 0x1F6
- `pFieldStateOthers` stride 0x264; `pFieldStateBackgrounds` stride 0x1B4
- Battle display struct at `0x1D8DFF4`; entity array at `0x1D27B18` stride 0xD0
- Savemap base: `0x1CFDC5C`; GF fire patch at `0x004B04B4`
- Entity sentinel ranges: -200..-299 = trigger lines/interactions, -300..-399 = JSM-injected, -400+ = gateways

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

## Disassembly Reference (session 42)

Full disassembly of FF8_EN.exe (21MB, Steam 2013) stored at `Game Files/disassembly/`.

**In project knowledge** (search directly — no tool calls needed):
- `FF8_EN_disasm_lookup_guide.txt` — lookup workflows for all common tasks
- `FF8_EN_sections.txt` — PE layout, image base 0x00400000
- `FF8_EN_imports.txt` — 12 DLLs
- `FF8_EN_exports.txt` — none
- `FF8_EN_functions.txt` — 8,390 function entries (prologues + call targets)
- `FF8_EN_callxrefs.txt` — cross-references (3+ callers)
- `FF8_EN_strings_condensed.txt` — 655 key strings (debug, paths, errors)
- `FF8_EN_strings.txt` — full 213K raw strings

**On disk** (use `filesystem:search_files` or `filesystem:read_text_file`):
- 8 `.text` section .asm files (split by 1MB VA range, 0x00401000–0x00B69000)
- 1 `.bind` section .asm file (DotEmu, 0x027C2000+)
- Total: ~98MB, 2.76M instructions

This eliminates the need to upload and re-disassemble FF8_EN.exe each session.

---

## Recovery Notes

1. Read this file FIRST for current state
2. Read `NEXT_SESSION_PROMPT.md` for immediate next steps
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details
4. **Use filesystem MCP tools (not bash) for Windows file access**
5. `deploy.bat` is the ONLY build script
6. Version bump in **five locations**: `FF8OPC_VERSION` in `ff8_accessibility.h`, header + Initialize() log in `field_navigation.cpp`, header + Initialize() log in `battle_tts.cpp`
7. "BAT" = Aaron has Built And Tested; read tail of game log
8. F12 is reserved for per-session diagnostic builds — search all sources for existing VK_F12 before hooking
9. Log channels: `Log::Field()`, `Log::Battle()`, `Log::Menu()`, `Log::World()`, `Log::Dialog()`, `Log::Mod()`. `Log::Write()` still works (routes to ff8_mod.log).
