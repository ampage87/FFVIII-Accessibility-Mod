# NEXT SESSION PROMPT — FF8 Accessibility Mod
## Updated: 2026-03-25 (post-session 13, EWM working)
## Current build: v0.10.27 (source + deployed)

---

## SESSION 13 RECAP

### Magic Sub-menu TTS (v0.10.15–v0.10.17)
- Sub-menu list cursor confirmed at `0x01D768EC`
- 57-entry MAGIC_NAMES[] table, BuildMagicList() reads savemap char+0x10
- 300ms debounce after turn start prevents false sub-menu detection
- Known issue: cursor=3 out of range when character has <4 spells (need page offset byte)

### Limit Break Toggle (v0.10.18–v0.10.22)
- Toggle byte confirmed at `0x01D7684A` (0=Attack, 64=Limit Break)
- Found via F11 snapshot diagnostic — only 5 bytes changed between states
- Polling and 16ms fast-poll both failed because menu phase never hits 64 on user toggle
- Announcement works on toggle and at turn start

### Enhanced Wait Mode (v0.10.23–v0.10.27)
- Freezes ALL ATBs when command menu appears (not just sub-menus like standard Wait Mode)
- Toggle: "O" key, works in all game modes, persists via ewm_config.txt (default: ON)
- Implementation: snapshot entity ATB values at turn start, write back every frame while deciding
- Release trigger: blacklist of executing phases (14, 21, 23, 33, 34). All other phases keep freeze.
- Three iterations to get phase logic right: v0.10.25 (active_char only — froze during animations), v0.10.26 (whitelist deciding phases — too narrow, missed phases 1/4), v0.10.27 (blacklist executing phases — working)
- Deep research submitted for ATB increment function hook (cleaner approach for future upgrade)
- ATB config byte found at `0x1CFE73B` (savemap config[3]: 0=Active, 1=Wait)

---

## NEXT SESSION PRIORITIES

### 1. Damage/Healing Announcements
- Announce when damage is dealt: "[Name] takes X damage" or "[Name] defeated"
- Announce when healing occurs: "[Name] recovers X HP" (cure spells, potions)
- HP polling already functional from Phase 1 (entity+0x10 current HP, entity+0x14 max HP)
- Track previous HP per entity per frame, announce on change
- Need to identify WHO caused the damage/healing (attacker ID at entity+0x80?)

### 2. Target Selection TTS
- When player enters target selection (phase 11), announce the currently selected target
- Target cursor address unknown — needs diagnostic build
- Enemy names available via GetEnemyName(), ally names via GetBattleCharName()
- Phase sequence: command select → phase 3 → phase 11 (target select) → phase 14 (confirmed)

### 3. Continue Battle Sub-menu TTS
- GF sub-menu: cursor at 0x01D768EC works; need GF name lookup from savemap (savemap+0x4C, 16 GFs × 0x44, name at +0x00)
- Item sub-menu: item display struct at 0x1D8DFF4
- Draw sub-menu: draw slots at 0x1D28F18 (4 slots/enemy, 0x47 stride)
- Magic list scrolling for >4 spells: need page offset byte

### 4. ATB Hook Upgrade (when deep research returns)
- Replace brute-force ATB freeze with hook on the ATB increment function
- Deep research prompt at: `Plan & Research Documents/ATB increment function deep research prompt for ChatGPT.md`

---

## KEY ADDRESSES (Battle, updated session 13)

| Address | Purpose |
|---------|---------|
| 0x01D76843 | Command cursor (0-3) |
| 0x01D76844 | active_char_id (255=none, 0-2=slot) |
| 0x01D768D0 | Menu phase (0/1/3/4=setup, 32=sub-menu, 11=target, 14+=executing) |
| 0x01D768EC | Sub-menu list cursor (0-N) |
| 0x01D7684A | Limit Break toggle (0=Attack, 64=Limit Break) |
| 0x1CFE73B | ATB config (0=Active, 1=Wait) — savemap config[3] |
| 0x1D27B18 | Entity array base (7 × 0xD0) |
| entity+0x0C | ATB current (uint16 ally, uint32 enemy) |
| entity+0x08 | ATB max |
| entity+0x10 | Current HP |
| entity+0x14 | Max HP |
| entity+0x78 | Persistent status flags |
| entity+0x80 | Last attacker ID |
| entity+0xB4 | Level |

## EWM Executing Phases (ATB unfreezes)
14, 21, 23, 33, 34

## VERSION BUMP LOCATIONS (4 required per build)
1. `FF8OPC_VERSION` in `src/ff8_accessibility.h`
2. Version comment near top of `src/field_navigation.cpp` (~line 5)
3. Init log string inside `src/field_navigation.cpp` Initialize() (~line 4683)
4. Version comment + init log in `src/battle_tts.cpp`

---

## HOUSEKEEPING
- GitHub push: v0.10.15–v0.10.27 (13 builds this session)
- Deep research pending: ATB increment function hook
- Diagnostic code still present: MENU-DIAG, CURSOR-HUNT, v0.10.07 name scan — cleanup needed before release
