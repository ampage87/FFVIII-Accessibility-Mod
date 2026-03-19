# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-19

> **File structure**: This file contains current state + key learnings only.
> Detailed build history (v05.47–v06.22, v07.00–v07.15) is in `DEVNOTES_HISTORY.md`.

---

## CURRENT OBJECTIVE: Entity Identification Accuracy + Exit Detection

### Current build: v0.07.94

### Save/Draw Point Detection (v0.07.75–v0.07.81)

**Draw points now detected** on bggate_2 via SET3 3-param fallback (X/Y/Z from PSHM_W markers + triangle from opcParam). JSM injection places them in the catalog with correct coordinates.

**Save points detected** via model ID 24 (save point crystal model). More reliable than SYM-based lookup because the visible save point entity (ent6) has a different SYM index than the save point script entity (JSM ent27).

**Key fixes in this series:**
- v0.07.75: SET3 position extraction fallback for draw points
- v0.07.76: Split catalog cycling (label only) from direction announcements (Backspace); camera-adjusted compass using heading calibration axes
- v0.07.77: Save/draw point walk-into arrive distance (30 units); fix default camera Y axis
- v0.07.78: Remove trigger-to-save rename heuristic; suppress event triggers near save/draw points
- v0.07.79: Model 24 save point detection
- v0.07.80: Skip JSM injection when runtime entity exists but is screen-filtered
- v0.07.81: Suppress screen-transition triggers near save/draw points

### Trigger Line Classification (v0.07.82 — WORKING)

JSM Line entity scripts are now scanned for opcode signatures to classify each SETLINE trigger line:
- **Camera Pan** (BGDRAW/BGOFF/scroll): transparent for screen filtering
- **Screen Boundary** (MAPJUMP family): filters entities on other side
- **Event Trigger** (SHOW/HIDE/MES/BATTLE): transparent

bghall_1 confirmed: all 6 lines classified as Camera Pan, entities remain visible while walking the hallway.

**Minor edge case**: bghall_4 has 6 captured SETLINEs but only 4 JSM Line entities. The extra 2 come from Background/Other entities calling SETLINE — they map beyond the Line range and get UNKNOWN (treated as separators). Low priority — fix by matching entity addresses instead of ordinal.

### JSM-Based Exits (v0.07.83–v0.07.88 — PARTIALLY WORKING)

INF gateway exit detection **stripped entirely** (vestigial PS1 data with bogus positions and destinations). Replaced with multi-layered JSM-based exit detection:

1. **Line-based exits** (`JSM_ENT_LINE_SCREEN_BOUND` captured trigger lines): Walk-on trigger lines with MAPJUMP. Position = runtime SETLINE center (accurate). Uses trigger-crossing arrival detection. No fields tested yet with actual screen boundary lines.
2. **Entity-based exits** (`JSM_ENT_MAP_EXIT` "Other" JSM entities): Direct MAPJUMP in entity scripts. Position from SET3 extraction or SETLINE fallback.
3. **REQ-following** (v0.07.85): If entity A calls REQ to entity B method M, and method M contains MAPJUMP, entity A is classified as MAP_EXIT. Works for indirect exit patterns like bgroom_1 jump entities.
4. **Variable-dispatch** (v0.07.87): If entity writes POPM_W to address X, and a MAPJUMP-containing method reads PSHM_W from address X, entity is classified as MAP_EXIT. Addr<8 filtered (v0.07.88) to eliminate scratch variable false positives.

**Destination names**: Resolved via `FIELD_DISPLAY_NAMES[destFieldId]` → "Exit to B-Garden - Hall 2". However, many fields load destFieldId from runtime memory (PSHM_W markers like 0xFF00FF) — static scanner cannot extract these. These show as generic "Exit".

**Current status on bghall_1**: 2 exits detected:
- `saveline0` (elevator): MAP_EXIT, no position (JSM ent36, beyond 10-slot runtime entity array — SETLINE writeback can't reach it)
- `water` (main gate): MAP_EXIT, position (142,-1484) works with compass. Player goes DOWN from save point to reach this.

**Not yet detected**: l1-l6 hall exits. **Deep research (2026-03-19) revealed these are CAMERA PAN ONLY** — they do NOT trigger field transitions. The actual transitions come from INF gateways (engine-level, checked every frame by field_main_loop). The variable-dispatch investigation for l1-l6 was a dead end. See `DEEP_RESEARCH_INF_GATEWAYS_Findings.md`. POPM_W addresses [1024-1041] are camera state variables, not transition dispatch.

**Bugs fixed this session**:
- **v0.07.84 Bug 3 FIX**: Camera Pan trigger lines no longer leak as "Event" entities. Event trigger section skips lines with `lineType == JSM_ENT_LINE_CAMERA_PAN || JSM_ENT_LINE_EVENT`.
- **v0.07.87**: SETLINE position writeback — writes derived position back to `s_jsmEntities[j]` so AnnounceDirections can provide compass for exits whose position came from SETLINE center.
- **v0.07.88**: Addr<8 filter eliminates false positives from scratch variables in var-dispatch matching.

### Catalog UX (v0.07.76+)

- **Minus/Plus**: Cycle entities, speaks "Type N of M" only (no directions)
- **Backspace**: Speaks camera-adjusted compass directions to selected entity
- **Backslash**: Auto-drive to selected entity
- Camera calibration runs on first auto-drive per field; default axes (1,0)/(0,-1) correct for most fields

### Menu TTS (WORKING — v07.04+)

Top-level cursor at `pMenuStateA + 0x1E6`. Values 0–10: Junction through Save.

### Save Screen (WORKING — v0.07.63)

Slot/block/phase cursors. LZSS decompression. SETPLACE location names. Save block content TTS.

### BGM Volume (WORKING — v0.07.24)

Hook `set_music_volume_for_channel` via FFNx. F3/F4 controls. Default 20%.

---

## PREVIOUS MILESTONES

- **v0.07.94**: INF gateway catalog integration — deduplicated exits with compass directions and auto-drive support. Tested on bggate_1 (3 exits) and bggate_2 (2 exits + draw point).
- **v0.07.93**: Corrected INF parser using Deling source: offset 0x64, stride 32, fieldId at +18. INF size = 676 bytes.
- **v0.07.90–92**: INF gateway diagnostic builds — hex dump, field-ID scan, format investigation.
- **v0.07.88**: Variable-dispatch exit detection + SETLINE position writeback + addr<8 filter. bghall_1: 2 exits detected (main gate with compass, elevator without position).
- **v0.07.87**: Variable-dispatch exit detection (POPM_W/PSHM_W address matching across entities and MAPJUMP methods). SETLINE position writeback for compass.
- **v0.07.85**: REQ-following for indirect MAPJUMP. Tracks REQ call targets and checks if target methods contain MAPJUMP.
- **v0.07.84**: Fix camera pan event leak, bogus destId filter (removed — too aggressive), SETLINE position fallback for entity-based exits.
- **v0.07.83**: JSM-based exits replace INF gateways. Entity-based exits from MAP_EXIT entities.
- **v0.07.82**: Trigger line classification — camera pans transparent, only screen boundaries filter entities.
- **v0.07.81**: Suppress screen-transition triggers near save/draw points.
- **v0.07.80**: Skip JSM injection when runtime entity is screen-filtered.
- **v0.07.79**: Model 24 save point detection.
- **v0.07.78**: Remove trigger-to-save rename heuristic. Event trigger overlap suppression.
- **v0.07.77**: Save/draw arrive distance 30. Fix default camera Y axis.
- **v0.07.76**: Split catalog/directions UX. Camera-adjusted compass.
- **v0.07.75**: SET3 3-param fallback for draw point positions. Disable SVDUMP/0x1C logging.
- **v0.07.74**: AnnounceCurrentTarget typeLabel fix. Invisible entity inclusion. JSM injection infrastructure.
- **v0.07.73**: JSM wired into catalog. Trigger line renaming. LoadJSMCounts fix.
- **v0.07.72**: JSM method boundaries fixed. SYM-name fallback.
- **v0.07.68**: JSM bytecode scanner — instruction decode confirmed via x86 disassembly.
- **v0.07.66**: Field display name tables (982 entries)
- **v0.07.63**: SETPLACE location display names in save block TTS
- **v0.07.48**: Save block cursor TTS
- **v0.07.24**: BGM volume persistence fix
- **v0.06.22**: Field navigation — auto-drive with A* pathfinding, SSFA funnel
- **v0.04.36**: Field dialog TTS — all MES/ASK/AMES/AASK/AMESW/RAMESW opcodes
- **v0.03.00**: FMV audio descriptions + skip
- **v0.02.00**: Title screen TTS

---

## ARCHITECTURE

### Module System (dinput8.cpp)
AccessibilityThread polls game state ~60Hz. Modules: TitleScreen, FieldDialog, FieldNavigation, FmvAudioDesc, FmvSkip, MenuTTS.

### Key Source Files
| File | Purpose |
|------|---------|
| dinput8.cpp | DLL proxy entry + game loop + module dispatch |
| ff8_addresses.h/cpp | Runtime address resolution |
| menu_tts.cpp/h | In-game menu + save screen TTS |
| field_navigation.cpp | Entity catalog, auto-drive, A* pathfinding (~4800 lines) |
| field_dialog.cpp/h | Opcode dispatch hooks for field dialog TTS |
| field_archive.cpp/h | fi/fl/fs archive reader + JSM scanner |
| ff8_text_decode.cpp/h | FF8 field + menu font encoding → UTF-8 |
| screen_reader.cpp | NVDA direct + SAPI fallback TTS |
| field_display_names.h | FIELD_DISPLAY_NAMES[982] + FIELD_INTERNAL_NAMES[982] |
| deploy.bat | Build + deploy (ONLY build script) |

---

## KEY LEARNINGS

### Entity & Coordinate System
- Entity screen-vertical is Y (offset 0x194), NOT Z (0x198). Z is always ~0.
- World coords (fixed-point ×4096): X=0x190, Y=0x194, Z=0x198. Player triangle: 0x1FA.
- Talk radius: offset 0x1F8 (uint16). Push radius: 0x1F6 (uint16).
- Model ID at offset 0x218 (int16). model=-1 = invisible script entity.
- **Model 24 = save point crystal** (authoritative across all fields).

### SETLINE Triggers
- INF trigger section is BOGUS on PC — real data from SETLINE opcode at runtime.
- Line coords at entity offset 0x188: 6×int16 (X1,Y1,Z1,X2,Y2,Z2) + lineIndex.
- SETLINE fires DURING field_scripts_init BEFORE SYM names load → captured names always empty.
- **Trigger line classification by JSM opcodes** (deep research complete):
  - Camera pan: BGDRAW (0x099), BGOFF (0x09A), scroll family (0x071–0x081), SETCAMERA (0x10A). Most common line type. Should NOT filter entities.
  - Screen boundary: MAPJUMP (0x029), MAPJUMP3 (0x02A), DISCJUMP (0x038), MAPJUMPO (0x05C), WORLDMAPJUMP (0x10D). Should filter entities.
  - Event trigger: SHOW (0x060), HIDE (0x061), USE (0x0E5), UNUSE (0x01A), MES (0x047), BATTLE (0x069). Should NOT filter entities.
  - Priority: MAPJUMP-family first (a script with BGDRAW + MAPJUMP is a screen boundary, not a camera pan).
  - All camera/scroll/event opcodes are < 0x100 → detected as primary opcodes (high byte). No 0x1C dispatch needed.
  - Line entities are JSM indices [countDoors .. countDoors+countLines-1], category 1 in scanner.

### Exit Detection Architecture (v0.07.83–v0.07.88)
- FF8 exits use 4 patterns:
  (A) Direct MAPJUMP in entity script — detected by opcode scan.
  (B) REQ to another entity's method with MAPJUMP — detected by REQ-following.
  (C) Variable-dispatch (write POPM_W, Director reads PSHM_W + branches to MAPJUMP) — partially works but misses many fields.
  (D) **INF gateways** (engine-level) — checked every frame by field_main_loop, zero script involvement. Toggled by MAPJUMPON (0x5D) / MAPJUMPOFF (0x5E). Destinations can be overwritten by MAPJUMPO (0x5C). **This is how bghall_1 hall exits work.**
- Pattern A: Works for `saveline0`, `water`, lighting entities with direct MAPJUMP.
- Pattern B: Works for bgroom_1 `*_jump*` entities that REQ to Director.
- Pattern C: Addr<8 filter in place. bghall_1 l1-l6 were investigated under this pattern but **turned out to be camera-pan-only entities** — their POPM_W writes (addrs 1024-1041) are camera state, not transition dispatch.
- Pattern D: **NOT YET IMPLEMENTED** in catalog. INF gateway data is loaded by LoadINFGateways() but was previously dismissed as "bogus PS1 data". Deep research (2026-03-19) shows the data appeared garbage due to format-version parsing error (4 INF formats: 672/576/480/384 bytes, each with different gateway offsets).
- Many fields load MAPJUMP destination from runtime memory (PSHM_W markers 0xFF00xx). Static scanner sees marker value, not real field ID. These show as generic "Exit".
- SETLINE position fallback: when entity-based exit has no SET3 position, match JSM SYM name to captured SETLINE entity address within runtime Others array. Write position back to `s_jsmEntities[]` for compass. Limited to entities within runtime entCount (saveline0 at ent36 is beyond 10-slot array).

### Entity Naming & Classification
- Model ID 0–8 overrides SYM for main character names. Model 24 = Save Point.
- SYM offset = 0 (entity state index i = SYM[i]).
- JSM-classified types: Save Point ("savePoint"/"svpt"), Draw Point ("dp##"), Shop ("shop*"), Card Game ("cardgame*").
- **JSM SET3 positions unreliable for some fields** (bghall_1 save point: SET3 gives 135,588 but runtime entity is at -700,-8593). Runtime entity positions via model=24 are authoritative.
- **JSM injection must check runtime entities (even screen-filtered) before injecting** to avoid wrong-position duplicates.

### JSM Bytecode (CONFIRMED by x86 disassembly)

**Instruction encoding**: Native LE uint32, NO byte swap. High byte = primary opcode (0x01-0xFF). 0x00 = push literal. Low 24 bits = signed param.

**0x1C extended dispatch**: POPS dispatch index from VM stack (not from instruction param). Pattern: `PUSH 0x137` then `0x1C` → dispatches as table[0x137] (DRAWPOINT).

**ENCODING DISCREPANCY**: Deep research (from Deling/Qhimm wiki) claims opcodes use bits 1–14 of the instruction word. Our scanner uses high-byte encoding — confirmed by x86 disassembly and validated across many fields. `DecodeJSMInstruction()` and `SwapBE32()` in field_archive.cpp are DEAD CODE (never called). Do not change the encoding — the high-byte + 0x1C approach works.

**Header**: b0=countDoors, b1=countLines, b2=countBg, b3=countOthers. Bytes 4-5=posFirst, 6-7=posScripts (uint16 LE).

**Group table entry**: uint16, bits 0-6 = method count, bits 7-15 = starting method index (0-based), bit 15 also encodes class/category flag.

**Key opcodes**: MAPJUMP=0x29, MAPJUMP3=0x2A, SETLINE=0x39, MES=0x47, DRAWPOINT=0x137 (via 0x1C), MENUSAVE=0x12E (via 0x1C), SETDRAWPOINT=0x155 (via 0x1C).

**SET3 position extraction**: Primary path uses 4 stack params. Fallback uses 3 stack params + triangle from opcParam (handles PSHM_W markers). Validated on bggate_2 dp01.

### Compass Directions (v0.07.76+)
- FormatNavComponents projects world-space delta onto camera axes (s_camRightX/Y, s_camDownX/Y).
- Default axes: camRight=(1,0), camDown=(0,-1). Matches most field camera orientations.
- Calibration refines axes empirically on first auto-drive per field.
- Camera variables declared early in file (before FormatNavComponents) for visibility.

### Navigation Architecture
- 47.5% of FF8 fields have disconnected walkmesh islands — BFS island detection essential.
- Save/draw points use arriveDist=30 (walk-into). NPCs use talkRadius. Default=300.
- Analog steering via fake gamepad + per-field heading calibration.
- Corridor-level steering targets shared-edge midpoints.
- Trigger-line proximity check prevents accidental field transitions.

### Auto-Drive
- Fake gamepad device sentinel (0xDEAD0001) + fake DIJOYSTATE2 buffer.
- Hook get_key_state to zero arrow scancodes during drive.
- SSFA funnel for path smoothing. Wall-parallel portal skip. Agent-radius portal shrinking.
- Recovery: re-path/nudge cycle. Max 12 recovery phases.

### INF Gateway System (deep research + Deling source 2026-03-19)
- FF8 has a DUAL transition architecture: script-driven (MAPJUMP family) AND engine-driven (INF gateways).
- INF gateways are checked every frame inside field_main_loop — 2D line-crossing test (cross product).
- Toggled by MAPJUMPON (0x5D) / MAPJUMPOFF (0x5E). Destinations overwritable by MAPJUMPO (0x5C).
- **0x5D = MAPJUMPON**, NOT WORLDMAPJUMP (which is 0x10D via 0x1C dispatch).
- **INF format = 676 bytes** (not 672). Definitive from Deling source (`myst6re/deling` → `src/files/InfFile.h`):
  - 0x00: name[9] + control(1) + unknown[6] + pvp(2) + cameraFocusHeight(2) = 20 bytes
  - 0x14: cameraRange[8] (64 bytes)
  - 0x54: screenRange[2] (16 bytes)
  - 0x64: gateways[12] (12 × 32 = 384 bytes) — **offset 0x64, stride 32, fieldId at +18**
  - 0x1E4: triggers[12] (12 × 16 = 192 bytes)
- Gateway struct (32 bytes): exitLine[2]×6B + destPoint×6B + fieldId(uint16) + unknown(12B)
- Gateway center = average of exit line endpoints (X,Y). Uses X/Y not X/Z (Y is screen-vertical).
- Static destinations may be FH placeholders (overwritten by MAPJUMPO at runtime). Display as "Exit to [fieldname]".
- **Deduplication**: Multiple gateways with same destFieldId → one catalog exit with averaged center.
- **Entity index sentinel**: -400 range for INF gateway catalog entries.
- Reference: `DEEP_RESEARCH_INF_GATEWAYS_Findings.md`, Deling `InfFile.h`

### Menu / Save Screen
- Menu font (sysfnt) encoding differs from field dialog. Separate decoder.
- Save files: LZSS compression, 384-byte PC header before savemap.
- SETPLACE location names: hardcoded 251-entry table.

### Build System
- `deploy.bat` is the ONLY build script.
- Version: bump `FF8OPC_VERSION` in `ff8_accessibility.h`, version comment in field_navigation.cpp, init log string.

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
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details
4. Use filesystem MCP tools (not bash) for Windows file access
5. `deploy.bat` is the ONLY build script
6. Current version: v0.07.94
7. "BAT" = read tail of `Logs/ff8_accessibility.log`
8. GitHub repo: ampage87/FFVIII-Accessibility-Mod
