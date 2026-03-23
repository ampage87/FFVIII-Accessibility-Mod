# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-23 (session 9)

> **File structure**: This file = current state + key learnings only (~10KB max).
> Build history in `DEVNOTES_HISTORY.md`. Immediate context in `NEXT_SESSION_PROMPT.md`.

---

## CURRENT OBJECTIVE: Menu TTS + GitHub Push

### Current build: v0.09.40 (source + deployed)

### GameAudio Module — COMPLETE (v0.09.22-v0.09.31)
- **BGM volume**: Direct `nxAudioEngine.setMusicVolume()` call bypasses FFNx hold_volume_for_channel flag. Addresses auto-detected by scanning FFNx's compiled `set_music_volume_for_channel` for MOV ECX + CALL pattern.
- **FMV volume**: Direct `SoLoud::Soloud::fadeVolume()` on `_currentStream.handle`. Offsets extracted from FFNx's compiled `stopStream`: _engine at nxAE+0x08, handle at nxAE+0x0E0754.
- **F3/F4**: ±10%, default 20%, TTS announcement, immediate ReapplyVolume.
- **SFX/SAPI**: Unaffected by F3/F4 — separate future controls possible.
- **Key insight**: FFNx's `setStreamMasterVolume` is store-only (never updates active SoLoud handle). Must use `fadeVolume(handle, vol, 0.0)` directly. Similarly `set_music_volume_for_channel` has internal hold flag that silently drops calls during track loads.

### Junction Menu TTS — Implementation Plan (v0.08.88+)

**Phase order** (GF assignment before sub-options; magic-to-stat deferred since Auto covers the need):
1. Diagnostic (v0.08.88): Discover focus states at +0x22E for all Junction phases
2. Character Select (v0.08.89–90): Party cursor with name/level/HP
3. Action Menu top-level (v0.08.91–93): Junction/Off/Auto/Ability
4. GF Assignment (v0.08.94–98): Junction→GF list, toggle assignment — MUST come first, unlocks everything
5. Action Menu sub-options (v0.08.99–101): GF/Magic, Magic/All, Atk/Mag/Def
6. Auto Execution (v0.08.102–103): Confirm auto-junction applied
7. Ability Assignment (v0.08.104–108): Command + Support equip — **unblocks battle work**
8. Off/Remove (v0.08.109–111): Off→Magic and Off→All
9. Magic-to-Stat (deferred): Full stat preview system

**Key data**: Character junction fields at savemap char +0x58 (GF bitmask), +0x5C–0x6E (stat/elem/status junctions), +0x50–0x57 (abilities). GF structs at savemap +0x4C (16×68 bytes). See NEXT_SESSION_PROMPT.md for full offset table.

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
| SET3 opcode hook | v0.08.03 | DISABLED v0.09.40 (causes infirmary hang) |
| Item submenu TTS | v0.08.61 | +0x22E focus state, debounced action cursor |
| Item sub-flows | v0.08.64 | Sort, Rearrange (src+dest), Battle (src+dest) |
| Use target TTS | v0.08.87 | compStats HP/maxHP, status ailments |
| Battle item TTS | v0.08.85 | Display struct 0x1D8DFF4, page/position, swap detection |
| Junction TTS (partial) | v0.08.97 | Char select, action menu, GF sub-option, GF list |
| Naming bypass | v0.09.16 | Call original + suppress UI flags. Chars + GFs. No naming screen. |
| GF acquisition TTS | v0.09.21 | MENUNAME snapshot before/after, zero polling. Suppresses char name when GF acquired. |
| BGM+FMV volume ctrl | v0.09.31 | F3/F4, direct nxAudioEngine + SoLoud calls, bypasses FFNx hold flags. Default 20%. |

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

### Naming Bypass Architecture (v0.09.16)
- **opcode_menuname (0x129)**: Handler at 0x00521DA0. Signature: `int __cdecl(int entityPtr)`. Returns 3 to advance script.
- **Entity script stack**: Stack at entityPtr+0x00, stack pointer byte at entityPtr+0x184. Param = `*(int32_t*)(entityPtr + sp * 4)`. Original handler does `sp--` internally.
- **Correct bypass**: Call original handler (does full init via 21-case switch table), then zero `*(uint8_t*)0x01CE4760` (pMode0Phase) and `*(uint8_t*)0x01CE490B` (naming flag) to suppress UI.
- **enableGF(idx) at 0x47E480**: NOT sufficient alone for GF activation. Original handler's switch table does much more.
- **GF acquisition TTS (v0.09.21)**: Snapshot GF exists flags before calling original MENUNAME handler, diff after. Any 0→non-zero transitions announced as "GF [name] acquired". Zero polling cost — only runs when MENUNAME fires. When GF acquired, character name TTS suppressed (avoids duplicate speech). Squall (param 0, no GFs) still announces his name.
- **GF exists flags**: savemap+0x4C + i*0x44 + 0x11 (base 0x1CFDCA8). 16 GFs total.

### BGM Volume Bug — FIXED (v0.09.22-v0.09.31)
- **Was**: F3/F4 volume had no effect during infirmary/classroom scenes. FFNx's `set_music_volume_for_channel` has `hold_volume_for_channel` flag that silently drops calls during track loads. Game never calls this function during normal field play.
- **Fix**: Created GameAudio module. Scans FFNx compiled code to extract `nxAudioEngine` pointer + `setMusicVolume` address. Calls `setMusicVolume` directly (bypasses hold flag). FMV audio controlled via `SoLoud::fadeVolume` on `_currentStream.handle` (FFNx's `setStreamMasterVolume` is store-only, never updates active handle).

### Infirmary Glitch — FIXED (v0.09.07-v0.09.40)
- **Root cause**: HookedSet3 (SET3 opcode hook, opcode 0x1E). ANY interception of SET3 hangs the infirmary scene (Dr. Kadowaki walk-to-phone never completes).
- **Binary search** (session 9, v0.09.32-v0.09.37): Confirmed FieldNavigation module is the culprit (v0.09.32). Narrowed to Group B script hooks (v0.09.33). Eliminated field_scripts_init (v0.09.34), setline/lineon/lineoff (v0.09.35). Isolated to SET3 (v0.09.37 = bug, v0.09.36 = no bug).
- **Fix attempts that FAILED**: MinHook → dispatch table patch (v0.09.38, still hangs). SEH removal from wrapper (v0.09.39, still hangs). Even a minimal `call original + return` wrapper triggers the bug.
- **Conclusion**: The FF8 script interpreter is fundamentally incompatible with SET3 handler replacement. The engine likely uses return address inspection or stack frame assumptions that any C wrapper violates.
- **Fix**: SET3 hook permanently disabled (v0.09.40). `false &&` guard in Initialize(). GitHub Actions CI check prevents accidental re-enablement.
- **Impact**: SET3 capture was only used for PSHM_W entity position investigation (already exhausted). Shift-pattern passthrough provides adequate fallback positions (~494 unit offset).
- **FieldNavigation**: Re-enabled with all other hooks working. Only SET3 is disabled.
- **Future**: If SET3 capture is ever needed, investigate naked/asm thunk that preserves the exact stack frame expected by the interpreter.

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
| game_audio.cpp/h | BGM + FMV volume control (FFNx/SoLoud direct calls) |
| screen_reader.cpp | NVDA direct + SAPI fallback TTS |
| deploy.bat | Build + deploy (ONLY build script) |

---

## RECOVERY NOTES

1. Read this file FIRST for current state
2. Read `NEXT_SESSION_PROMPT.md` for immediate next steps
3. Read `DEVNOTES_HISTORY.md` ONLY if you need past build details
4. Use filesystem MCP tools (not bash) for Windows file access
5. `deploy.bat` is the ONLY build script
6. Current version: v0.09.40 (source), v0.09.40 (deployed)
7. "BAT" = read tail of `Logs/ff8_accessibility.log`
8. GitHub repo: ampage87/FFVIII-Accessibility-Mod
