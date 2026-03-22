# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-22 (session 2)

> **File structure**: This file = current state + key learnings only (~10KB max).
> Build history in `DEVNOTES_HISTORY.md`. Immediate context in `NEXT_SESSION_PROMPT.md`.

---

## CURRENT OBJECTIVE: Menu TTS + Interactive Object Position Research

### Current build: v0.08.87 (source + deployed)

### Battle Arrangement TTS — FIXED (v0.08.82–v0.08.85)
- **Display struct at `0x1D8DFF4`** is the authoritative data source
- Format: `{item_id, quantity}` pairs, 2 bytes per slot, qty=0 = empty
- Engine populates from battle_order on screen entry, filtering non-battle items
- Updates live during swaps — no manual tracking needed
- v0.08.82–83: Diagnosed that neither battle_order[32] nor raw inventory matched screen
- v0.08.84: Switched to display struct — perfect match
- v0.08.85: Cleaned up ~300 lines of dead working buffer code
- Deep research confirmed no public RE covers item menu controller (0x4F81F0, 0x4AD0C0)

### Party HP/Status Announcement — CONFIRMED (v0.08.86–v0.08.87)
- **Computed stats at `0x1CFF000`** (FFNx `char_comp_stats_1CFF000`): curHP at +0x172, maxHP at +0x174
- Struct stride `0x1D0` (464 bytes), 3 entries indexed by party formation slot (not charIdx)
- Map charIdx to slot via savemap +0xAF0 formation array
- `FormatPartyMemberAnnouncement()` builds "Name, HP X of Y" + status ailments
- Status: savemap char +0x96 bitfield (KO/Poison/Petrify/Blind/Silence/Berserk/Zombie)
- TODO: test with damaged characters, GF items, miscellaneous items

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

Verified corrected new offsets: Live game time +0x0CCC, gameplay Gil +0x0B08, item inventory +0x0B40 (198x2 bytes), current field ID +0x0D3E, SeeD test level +0x0D2F, active party +0xAF1 (unchanged). T hotkey now uses live timer.

### Submenu Cursor Discovery (v0.08.28–v0.08.61)

**Item submenu offsets confirmed:**
- **+0x22E**: Active focus indicator (v0.08.59 round-trip diagnostic). **3=action menu, 5=items list**. Transitions through intermediates: 5>2>3 (items>action), 3>4>5 (action>items). This is the primary detection mechanism — scalable to all submenus.
- **+0x27F**: Action menu cursor (0=Use, 1=Rearrange, 2=Sort, 3=Battle). Debounced 200ms.
- **+0x272**: Item list cursor index.
- **+0x230**: Phase flag (0=action, 1=items) — UNRELIABLE, does not always update on Cancel.
- **+0x5DF**: Sub-phase — UNRELIABLE, sometimes fires 3>2 and sometimes doesn't.
- **+0x234**: Submenu callback index (=5 for Item).

**Architecture (v0.08.60–64):** PollItemSubmenu watches +0x22E focus state for mode transitions. Extended focus values map to sub-flows:
- 3=action menu, 5=items list, 14=use target, ~97=rearrange source, 99=rearrange dest
- ~30=battle source, 36=battle dest, 79=sort flash
- Sort: flash through 79→3, announce "Items sorted" then queue "Use" (interrupt=false)
- Rearrange: source cursor +0x272, dest cursor +0x276 (reused from party target)
- Battle: source cursor +0x285, dest cursor +0x286
- Use target: +0x276 party cursor. HP/name fixed v0.08.67 (sorted party order, curHP only)

Auto-monitor infrastructure (SUBMON) still active for discovering future submenu offsets.

### Battle Order Table (v0.08.68–v0.08.77)

**Confirmed via deep research + diagnostic builds:**
- `battle_order[32]` at savemap +0x0B20 (runtime 0x1CFE77C)
- Format: `uint8[32]`, each entry = inventory slot index (0-197, 0xFF=empty)
- Mapping: `battle_order[cursor_pos]` → inv index → `items[inv_idx*2]` for id/qty
- Engine uses **deferred write**: copies to working buffer on screen open, writes back on exit
- **BLOCKED**: Runtime working buffer not yet located. Current workaround: local copy + swap tracking.
- Deep research: `Plan & Research Documents/DEEP_RESEARCH_battle_item_order_table.md`

### Party Identification (v0.08.67)
- Header portraits (+0x11) are in **formation order, NOT visual menu order**
- Party at +0xAF0 (4 bytes), sorted ascending by char index for visual order
- maxHP in savemap = 0 (runtime computed). Announce curHP only.

### Catalog Status (bghall_1 v0.08.16)
- 5 NPCs + 1 Save Point + 1 Interactive Object (Directory) + 4 INF gateway exits (named)
- **Directory panel (igyous1)**: In catalog at (-82, -8019) via shift-pattern. Interactable but ~494 units off from true interaction zone (21, -7536).

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
| Item submenu TTS | v0.08.61 | +0x22E focus state, debounced action cursor |
| Item sub-flows | v0.08.64 | Sort, Rearrange (src+dest), Battle (src+dest) |
| Use target TTS | v0.08.87 | compStats HP/maxHP, status ailments |
| Battle item TTS | v0.08.85 | Display struct 0x1D8DFF4, page/position, swap detection |

---

## MENU TTS (v0.08.17–v0.08.22)

### Savemap Memory Layout (confirmed)
- **Savemap base**: `0x1CFDC5C`
- **Header**: 0x4C bytes. locId(+0x00 u16), HP(+0x02/0x04 u16), Gil(+0x08 u32), time(+0x0C u32 seconds), lvl(+0x10 u8), portraits(+0x11 u8[3]), names(+0x14 12-byte blocks, -0x20 encoded)
- **GF section**: 16 x 68 bytes = 0x440, starting at savemap+0x4C
- **Character section**: 8 x 152 (0x98) bytes = 0x4C0, starting at savemap+0x48C (= 0x1CFE0E8)
- **Party indices**: savemap+0xAF1 (3 bytes: char index 0-7 or 0xFF=empty)
- **Name encoding**: Live savemap uses +0x20 offset from menu font encoding. Subtract 0x20 before decoding.

### Menu Hotkeys
- G = Gil, T = Play time (live timer +0x0CCC), L = Location, R = SeeD rank
- F11 = full summary, Shift+F11 = memory monitor, Ctrl+F11 = diagnostic dump

---

## KEY LEARNINGS

### Entity & Coordinate System
- Screen-vertical = Y (offset 0x194), NOT Z. World coords x4096: X=0x190, Y=0x194. Triangle: 0x1FA.
- Talk radius: 0x1F8. Push radius: 0x1F6. Model ID: 0x218. Model 24 = save point.
- SYM offset = 0 (entity state index i = SYM[i]).
- `pFieldStateOthers` only allocates `entCount` active slots (typically 10). Entities beyond are inaccessible.

### PSHM_W / Shared Memory Architecture
- **Varblock base**: `0x1CFE9B8` (Steam 2013 en-US). Save file offset `0xD10`.
- **Three resolution modes** (per-axis independent): (1) Negative param = passthrough literal. (2) Small positive + entity flag = entity-scope sub 0x00532890. (3) Standard positive = varblock read.
- **PSHM markers**: `0x8000xxxx` (bits 16-30 zero). Negative literals: `0xFFFFxxxx`. Use `& 0xFFFF0000 == 0x80000000` to distinguish.
- **Shift-pattern**: When first PSHM_W param is positive (mode selector) but Y,Z are negative passthrough, actual position is (litY, litZ).
- **Dispatch table hook**: Write directly to `pExecuteOpcodeTable[index]` — MinHook conflicts with FFNx.

### Exit Detection (4 patterns)
- (A) Direct MAPJUMP, (B) REQ-following, (C) Variable-dispatch, (D) INF gateways

### Navigation Architecture
- 47.5% of fields have disconnected walkmesh islands — BFS essential
- Analog steering via fake gamepad + get_key_state hook
- SSFA funnel path smoothing. Recovery: re-path/nudge cycle.

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

## RECOVERY NOTES

1. Read this file FIRST for current state
2. Read `NEXT_SESSION_PROMPT.md` for immediate next steps
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details
4. Use filesystem MCP tools (not bash) for Windows file access
5. `deploy.bat` is the ONLY build script
6. Current version: v0.08.87 (source), v0.08.87 (deployed)
7. "BAT" = read tail of `Logs/ff8_accessibility.log`
8. GitHub repo: ampage87/FFVIII-Accessibility-Mod
