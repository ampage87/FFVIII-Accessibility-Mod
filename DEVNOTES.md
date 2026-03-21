# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-21

> **File structure**: This file = current state + key learnings only (~10KB max).
> Build history in `DEVNOTES_HISTORY.md`. Immediate context in `NEXT_SESSION_PROMPT.md`.

---

## CURRENT OBJECTIVE: Menu TTS + Interactive Object Position Research

### Current build: v0.08.61 (source) / v0.08.01 (deployed)

### PSHM_W Investigation (v0.08.03–v0.08.26 — ALL RUNTIME APPROACHES EXHAUSTED, Option F pending)

**Goal**: Resolve runtime positions for PSHM_W entities (e.g. bghall_1 Directory panel dic/igyous1) whose coordinates come from shared memory variables rather than literal values in JSM scripts.

**Approaches tried (ALL FAILED for dic):**
1. SET3 opcode hook (v0.08.03, extended v0.08.16, persistent v0.08.26): dic is beyond 10-slot active window, engine never executes its scripts, never fires SET3.
2. Direct entity state read (v0.08.02/v0.08.05): entities beyond index 9 have no allocated state struct.
3. Varblock formula (v0.08.11–12): `*(int16_t*)(0x1CFE9B8 + addr)` returns wrong values. dic's address 135 is below entity-scope threshold (~2696), takes alternate code path.
4. Descriptor table polling (v0.08.23): `0x01DCB340[dic_index]` always NULL. Polled 10s after load.
5. Proximity-based active window swap (v0.08.26): walked to Directory, no new SET3 calls. Active window is fixed at field load.
6. Parametric curve formula (deep research): requires descriptor data at +0x68 which is never allocated.
7. Mini JSM interpreter (Option D): would hit same entity-scope dead end — varblock read for addr 135 returns 0.

**Remaining approach: Option F — Force Entity Script Execution**
Allocate temporary entity state struct, configure it for dic's JSM context, call the engine's script interpreter directly. Deep research prompt prepared: `Plan & Research Documents/Force Entity Script Execution - Deep Research Request.md`. Awaiting ChatGPT results.

**What works today**: Shift-pattern passthrough gives approximate coordinates (-82, -8019) for dic/igyous1. Entity is in catalog and interactable but ~494 units off from true position (21, -7536).

### Savemap Offset Correction (v0.08.27 — CRITICAL for all future research)

ChatGPT deep research assumes savemap header is 96 bytes (0x60). **Confirmed header is 76 bytes (0x4C).** All post-header offsets from research are 0x14 (20 bytes) too high. When using research offsets: subtract 0x14.

Verified corrected new offsets: Live game time +0x0CCC, gameplay Gil +0x0B08, item inventory +0x0B40 (198×2 bytes), current field ID +0x0D3E, SeeD test level +0x0D2F, active party +0xAF1 (unchanged). T hotkey now uses live timer.

### Submenu Cursor Discovery (v0.08.28–v0.08.61)

**Item submenu offsets confirmed:**
- **+0x22E**: Active focus indicator (v0.08.59 round-trip diagnostic). **3=action menu, 5=items list**. Transitions through intermediates: 5→2→3 (items→action), 3→4→5 (action→items). This is the primary detection mechanism — scalable to all submenus.
- **+0x27F**: Action menu cursor (0=Use, 1=Rearrange, 2=Sort, 3=Battle). Debounced 200ms.
- **+0x272**: Item list cursor index.
- **+0x230**: Phase flag (0=action, 1=items) — UNRELIABLE, does not always update on Cancel.
- **+0x5DF**: Sub-phase — UNRELIABLE, sometimes fires 3→2 and sometimes doesn't.
- **+0x234**: Submenu callback index (=5 for Item).

**Architecture (v0.08.60–61):** PollItemSubmenu watches +0x22E for transitions to stable endpoints (3 or 5). On arriving at 3: announce action option. On arriving at 5: announce current item. Debounced +0x27F handles left/right action navigation. Clean, no GCW string matching or phase fallbacks.

Auto-monitor infrastructure (SUBMON) still active for discovering future submenu offsets.

### Catalog Status (bghall_1 v0.08.16)
- 5 NPCs + 1 Save Point + 1 Interactive Object (Directory) + 4 INF gateway exits (named)
- **Directory panel (igyous1)**: In catalog at (-82, -8019) via shift-pattern. Interactable but ~494 units off from true interaction zone (21, -7536).
- **v0.08.15**: Extended SET3-MATCH to scan pFieldStateBackgrounds — no new matches for dic/igyous1 (they're actually Others cat=3, not Background cat=2). Infrastructure still valuable for future fields with genuine bg entities using PSHM_W.
- **v0.08.16**: Extended SET3 capture window to 3s post-init. Captured walking NPCs (ent3/ent4) repositioning, proving the extended window works for per-frame SET3. However dic never fires SET3 during normal gameplay — its position is resolved on-demand by the PSHM_W entity-scope parametric formula, not via explicit SET3 calls.
- **Next step**: Deep research needed on parametric curve formula at sub `0x00532890`. Prompt prepared: `Plan & Research Documents/PSHM_W Parametric Curve Formula - ChatGPT Deep Research Request v2.md`
- **Bonus finding**: Extended SET3 window captures walking NPCs repositioning after field load. This will be useful for tracking moving NPC positions on other fields (e.g., students walking between classes).

---

## WORKING FEATURES (stable)

| Feature | Version | Notes |
|---------|---------|-------|
| Title screen TTS | v0.02.00 | Cursor tracking |
| FMV audio descriptions + skip | v0.03.00 | ReadFile EOF hook |
| Field dialog TTS | v0.04.36 | All MES/ASK/AMES/AASK/AMESW/RAMESW |
| Field navigation + auto-drive | v0.06.22 | A*, SSFA funnel, analog steering, recovery |
| Menu TTS (top-level) | v0.07.04+ | pMenuStateA+0x1E6 cursor |
| BGM volume control | v0.07.24 | F3/F4, hook set_music_volume |
| Save screen TTS | v0.07.63 | Slot/block/phase cursors, LZSS, SETPLACE |
| Field display names | v0.07.66 | 982-entry table |
| Save/draw point detection | v0.07.75–81 | SET3 fallback, model 24 |
| Trigger line classification | v0.07.82 | Camera pan / screen boundary / event |
| JSM-based exit detection | v0.07.83–88 | MAPJUMP, REQ-following, var-dispatch |
| INF gateway exits | v0.07.93–96 | Dedup, named destinations, world map |
| Interactive object detection | v0.07.98–v0.08.01 | Paired inheritance, PSHM_W markers |
| SET3 opcode hook | v0.08.03 | Runtime position capture (active entities only) |

---

## MENU TTS (v0.08.17–v0.08.22)

### Savemap Memory Layout (confirmed)
- **Savemap base**: `0x1CFDC5C` (derived from Gil=5000 at 0x1CFDC64, header.gil is at +0x08)
- **Header**: 0x4C bytes. locId(+0x00 u16), HP(+0x02/0x04 u16), Gil(+0x08 u32), time(+0x0C u32 seconds), lvl(+0x10 u8), portraits(+0x11 u8[3]), names(+0x14 12-byte blocks, -0x20 encoded)
- **GF section**: 16 × 68 bytes = 0x440, starting at savemap+0x4C
- **Character section**: 8 × 152 (0x98) bytes = 0x4C0, starting at savemap+0x48C (= 0x1CFE0E8)
- **Party indices**: savemap+0xAF1 (3 bytes: char index 0-7 or 0xFF=empty) — candidate, not fully verified
- **Name encoding**: Live savemap uses +0x20 offset from menu font encoding. Subtract 0x20 before decoding.
- **character_data_1CFE74C** from FFNx is battle-computed stats, NOT savemap characters.

### Known Issue: Header played_time_secs is Stale
The uint32 at savemap+0x0C is only synced at save/load time, NOT updated in real-time during gameplay. Need to find the live timer variable for the T hotkey.

### Help Text in GCW Buffer
GCW buffer captures ALL rendered menu text concatenated. Pattern:
`"...SaveJunction MenuSquallB-Garden- Hall"` — help text is between "Save" and character name.
Need to parse the help substring from the repeating block.

### Menu Hotkeys (implemented v0.08.21, need live-timer fix)
- G = Gil, T = Play time, L = Location, R = SeeD rank
- F11 = full summary, Shift+F11 = memory monitor, Ctrl+F11 = diagnostic dump
- Keys G/T/R/L confirmed safe — not bound to any FF8 function

---

## PREVIOUS MILESTONES (v0.08.xx)

- **v0.08.22**: Memory monitor + GCW help text capture. Left-panel cursor investigation started.
- **v0.08.21**: Menu hotkeys G/T/L/R + F11 summary. Removed auto-announce on menu open.
- **v0.08.19**: Fixed GF struct size (68 not 76), name -0x20 decode. All 8 chars + names confirmed.
- **v0.08.17**: First menu data diagnostic. Confirmed savemap base 0x1CFDC5C via Gil/time cross-reference.
- **v0.08.16**: Extended SET3 capture window (3s post-init) + SET3-LATE-MATCH in RefreshCatalog. Deduplication in SET3 hook. Captures walking NPCs repositioning. dic still needs parametric formula.
- **v0.08.15**: SET3-MATCH extended to background entities (cat 2). Three matching sites updated (init, direct-read, late-resolve).
- **v0.08.14**: F12 position announce key. Shift-pattern promotion in catalog.
- **v0.08.13**: PSHM_W negative-param passthrough. Tightened isPshm marker detection (`&0xFFFF0000==0x80000000`). Shift-pattern: litY→posX, litZ→posY when X=PSHM. Paired inheritance hasPosition propagation.
- **v0.08.12**: PSHM_W investigation builds. Hardcoded varblock, dispatch table hook, time-based capture.
- **v0.08.06**: PSHM_W handler diagnostic. Descriptor table probe (all NULL). Deep research prompt.
- **v0.08.05**: Direct struct read fallback + late PSHM resolution. Both return 0 for dic.
- **v0.08.04**: Light-entity paired inheritance filter. IntObj=1 confirmed.
- **v0.08.03**: SET3 opcode hook. 5 captures on bghall_1. dic not captured (non-init method).
- **v0.08.02**: Runtime PSHM_W read attempt. FAILED — 10-slot active window.
- **v0.08.01**: (DEPLOYED) Paired entity inheritance (dic→igyous1). Exit dedup bit31 fix.
- **v0.08.00**: PSHM_W marker pattern fix: `0x80000000|addr` (was `0x00FF0000`).
For v0.07.xx and earlier build tables, see `DEVNOTES_HISTORY.md`.

---

## KEY LEARNINGS

### Entity & Coordinate System
- Screen-vertical = Y (offset 0x194), NOT Z. World coords ×4096: X=0x190, Y=0x194. Triangle: 0x1FA.
- Talk radius: 0x1F8. Push radius: 0x1F6. Model ID: 0x218. Model 24 = save point.
- SYM offset = 0 (entity state index i = SYM[i]).
- `pFieldStateOthers` only allocates `entCount` active slots (typically 10). Entities beyond are inaccessible.

### PSHM_W / Shared Memory Architecture
- **Varblock base**: `0x1CFE9B8` (Steam 2013 en-US), `0x18FE9B8` (Original/SE). FFNx: `field_vars_stack_1CFE9B8`. Save file offset `0xD10`.
- **Variable space**: bytes 0–1023 = persistent (saved to disk). 1024+ = temp per-field (reset on transition).
- **Standard formula**: `*(int16_t*)(0x1CFE9B8 + param)` — only for standard code path.
- **Two code paths**: execution flags at entity struct 0x160 control branching. Standard = varblock read. Alternate = entity-relative via *9 multiply.
- **PSHSM = "Push Signed Memory"** (NOT shared). Same varblock, sign-extended result. No POPSM counterpart.
- **JSM encoding**: 32-bit native LE. Bits[31:16] = opcode key. Bits[15:0] = signed int16 parameter.
- `0x01DCB340`: per-entity descriptor pointer table (NOT variable array). Allocated on-demand.
- Entity scope sub `0x00532890`: parametric curve computation from descriptor+0x68 data array.
- POPM_W `0x0051CAF0` → core sub `0x0051C9C0` (type-clamping jump table).
- Global threshold selector: `0x01CE476A` (WORD). On bghall_1 = 20 → threshold ≈ 2696.
- **PSHM markers**: `0x8000xxxx` (bits 16-30 zero). Negative literals: `0xFFFFxxxx` (bits 16-30 ones). Must use `& 0xFFFF0000 == 0x80000000` to distinguish.
- **Shift-pattern**: When first PSHM_W param is positive (mode selector) but Y,Z are negative passthrough, actual position is (litY, litZ). Confirmed: l1 (1032,-2865,-5421)→pos(-2865,-5421). Safety: both litY AND litZ must be non-zero.
- **Three PSHM_W resolution modes** (per-axis independent): (1) Negative param → passthrough literal coordinate. (2) Small positive + entity flag → entity-scope sub 0x00532890. (3) Standard positive → varblock read.
- **Descriptor struct**: 0x90 bytes (9×16), per flat entity index. +0x0C/+0x0E=computed X/Y, +0x68=curve data ptr, +0x7E=cache key.
- Paired entity pattern: position entity (dic) + dialog entity (igyous1), consecutive JSM indices.
- **Dispatch table hook**: MinHook conflicts with FFNx on same handler. Write directly to `pExecuteOpcodeTable[index]` instead — swap pointer, save original as trampoline. Restore on shutdown via VirtualProtect.
- **FFNx replaces dispatch table entries**: `opcode_pshm_w` from the table points to FFNx code, NOT the original handler. Cannot read embedded constants (e.g., +0x1E for varblock base) from it.
- **Entity struct stack**: VM stack is at entity+0x000 (320 bytes). Stack pointer at +0x184 is `uint8_t`, NOT `uint32_t`. Reading DWORD at 0x184 causes crash (3 garbage bytes).
- **Varblock reads don't work for entity-scope addresses**: `*(int16_t*)(0x1CFE9B8 + addr)` returns wrong values for ALL PSHM_W entities on bghall_1. Every address goes through entity-scope parametric path.

### Exit Detection (4 patterns)
- (A) Direct MAPJUMP in entity script — opcode scan
- (B) REQ-following — indirect MAPJUMP via REQ call
- (C) Variable-dispatch — POPM_W/PSHM_W address matching (addr≥8 filter)
- (D) INF gateways — engine-level, toggled by MAPJUMPON/OFF, destinations by MAPJUMPO

### Trigger Lines & Screen Filtering
- Classified by JSM opcodes: Camera Pan (BGDRAW), Screen Boundary (MAPJUMP), Event (SHOW/HIDE)
- Cross-product sign test for screen filtering
- INF trigger section is bogus on PC — real data from SETLINE opcode hook at runtime

### INF Gateway System
- Dual transition architecture: script-driven (MAPJUMP) + engine-driven (INF gateways)
- INF format = 676 bytes. Gateways at offset 0x64, stride 32, fieldId at +18.
- Deduplication by destFieldId. World map IDs 0-71 → "Exit to World Map".

### JSM Bytecode
- Native LE uint32. High byte = primary opcode. 0x00 = push literal. Low 24 bits = signed param.
- 0x1C extended dispatch: pops index from VM stack. Key dispatches: DRAWPOINT=0x137, MENUSAVE=0x12E.
- Header: b0-b3 = countDoors/Lines/Bg/Others. Group table: bits 0-6 = methods, 7-15 = start index.

### Navigation Architecture
- 47.5% of fields have disconnected walkmesh islands — BFS essential
- Analog steering via fake gamepad (0xDEAD0001 sentinel) + get_key_state hook
- SSFA funnel path smoothing. Corridor-level steering. Recovery: re-path/nudge cycle.
- Per-field heading calibration on first auto-drive.

---

## ARCHITECTURE

### Module System (dinput8.cpp)
AccessibilityThread polls ~60Hz. Modules: TitleScreen, FieldDialog, FieldNavigation, FmvAudioDesc, FmvSkip, MenuTTS.

### Key Source Files
| File | Purpose |
|------|---------|
| field_navigation.cpp | Entity catalog, auto-drive, A* pathfinding (~5000 lines) |
| field_archive.cpp/h | fi/fl/fs archive reader + JSM scanner |
| ff8_addresses.h/cpp | Runtime address resolution |
| field_dialog.cpp/h | Opcode dispatch hooks for field dialog TTS |
| menu_tts.cpp/h | In-game menu + save screen TTS |
| screen_reader.cpp | NVDA direct + SAPI fallback TTS |
| deploy.bat | Build + deploy (ONLY build script) |

---

## KEY FILE LOCATIONS

- Project root: `C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod\`
- Source: `src\`
- FFNx reference: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\`
- Log: `Logs\ff8_accessibility.log`
- Build history: `DEVNOTES_HISTORY.md`

---

## RECOVERY NOTES

1. Read this file FIRST for current state
2. Read `NEXT_SESSION_PROMPT.md` for immediate next steps
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details
4. Use filesystem MCP tools (not bash) for Windows file access
5. `deploy.bat` is the ONLY build script
6. Current version: v0.08.61 (source), v0.08.01 (deployed)
7. "BAT" = read tail of `Logs/ff8_accessibility.log`
8. GitHub repo: ampage87/FFVIII-Accessibility-Mod
