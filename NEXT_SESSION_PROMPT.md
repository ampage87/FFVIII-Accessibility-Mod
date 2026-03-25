# NEXT SESSION PROMPT — FF8 Accessibility Mod
## Updated: 2026-03-25 (post-session 12, battle command menu TTS working)
## Current build: v0.10.14 (source + deployed)

---

## SESSION 12 RECAP: Battle Command Menu TTS (v0.10.09–v0.10.14)

This session completed battle command menu navigation TTS. Key results:

### Confirmed Battle Menu Addresses
| Address | Purpose |
|---------|---------|
| 0x01D76843 | Battle command cursor (0-3, cycles through 4 slots) |
| 0x01D76844 | active_char_id (BYTE, 255=no turn, 0-2=party slot) |
| 0x01D76845 | new_active_char_id |
| 0x01D768D0 | Menu phase (1=open, 3=executing, 4=idle) |
| 0x01D76860 | Cursor visual Y-position (87→118→131→202) |
| 0x01D7689A | Which character's menu is active (alternates) |
| 0x01D76968 | ATB timer ally 0 (monotonic) |
| 0x01D769D4 | ATB timer ally 1 (monotonic) |

### Key Discovery: Savemap Ability IDs
- Savemap char struct at +0x50 stores **GF ability IDs**, NOT battle command IDs
- Ability ID = battle_cmd_ID + 0x12
- Mapping: 0x14=Magic, 0x15=GF, 0x16=Draw, 0x17=Item, 0x18=Card, 0x19=Devour, etc.
- The -0x14 header correction does NOT apply to SAVEMAP_CHAR_DATA_BASE (0x1CFE0E8 is correct as-is)
- Slot 0 is always "Attack" (hardcoded, not read from savemap)

### What Works Now
- Turn announcement: "Quistis's turn. Attack." when ATB fills
- Command navigation: "Magic", "GF", "Item" as you move cursor up/down
- Character name resolved via SAVEMAP_PARTY_FORMATION (0x1CFE74C) → charIdx → CHAR_NAMES[]

### Diagnostic Code Still Present
- v0.10.07 diagnostic code (6-approach name scan functions) still in battle_tts.cpp — cleanup needed
- MENU-DIAG wide-scan logging still active (1024-byte region monitor) — should be disabled for production but useful for next phase

---

## IMMEDIATE PRIORITY: Battle TTS Phases 4-7

### Phase 4: Command Sub-menus + Target Selection
The next goals are:
1. **Command sub-menu TTS** — when player selects Magic, GF, Draw, or Item, a sub-menu appears with a list of spells/GFs/items. Need to find the sub-menu cursor address and read the spell/GF/item names.
   - Magic: list of stocked spells with quantities
   - GF: list of junctioned GFs
   - Draw: targets enemy draw slots (4 spells per enemy)
   - Item: battle item list (already have display struct at 0x1D8DFF4 from menu work)
   - Need diagnostic builds to find sub-menu cursor position
   
2. **Target selection TTS** — after selecting a command (or its sub-option), the game enters target selection mode where an arrow hovers over enemies/allies. Need to announce the currently selected target name.
   - Target cursor address unknown — needs diagnostic
   - Enemy names via entity array at 0x1D27B18 (already have decoder)
   - Ally names via party formation array

3. **Damage announcements** — announce damage dealt to enemies and allies
   - HP change detection by polling entity HP values (already reading HP in Phase 1)
   - Announce "[Name] takes X damage" or "[Name] defeated"

4. **Healing announcements** — announce HP recovered
   - Same HP polling, detect increases instead of decreases
   - Announce "[Name] recovers X HP"

### Key Documents
- **Implementation plan**: `Plan & Research Documents/Battle TTS implementation plan.md`
- **Memory map**: `Plan & Research Documents/Battle system memory map deep research results.md`
- **FFNx source**: `Plan & Research Documents/Battle TTS research - FFNx source analysis.md`

### Key Code (battle_tts.cpp)
- `PollTurnAndCommands()` — current turn detection + command cursor TTS
- `GetCommandName()` — ability ID → name lookup (0x14=Magic through 0x38=Treatment)
- `GetBattleCharName()` — party slot → character name
- `BuildCharCommandList()` — reads equipped commands from savemap
- Constants: BATTLE_CMD_CURSOR, BATTLE_MENU_PHASE, SAVEMAP_PARTY_FORMATION, SAVEMAP_CHAR_DATA_BASE
- Enemy HP reading + name decode already functional from Phase 1

---

## SECOND PRIORITY: World Map Accessibility

Research prompt submitted but results not yet received.
**Prompt**: `Plan & Research Documents/World Map Accessibility deep research prompt for ChatGPT.md`

---

## DEFERRED

- Junction Menu: Auto sub-options, manual magic-to-stat (needs magic stocked)
- Top-level menu navigation TTS
- Save Game flow TTS  
- Save Point entity catalog integration
- Title Screen Continue TTS
- SFX volume control (GitHub Issue #8)
- PSHM_W Option F (force entity script execution — deep research pending)
- Item screen HP not updated in real-time (GitHub Issue #10)
- Cleanup v0.10.07 diagnostic code (6-approach name scan)
- Remove MENU-DIAG wide-scan when no longer needed

---

## HOUSEKEEPING

- **GitHub push needed**: v0.09.41–v0.10.14 unpushed (grew from 9 to ~23 builds)
- Git: `cd C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod && git add -A && git commit -m "v0.09.41-v0.10.14: Ability screen TTS, battle TTS phases 1-3 (enemy announce, turn announce, command menu)" && git push origin main`

---

## VERSION BUMP LOCATIONS (4 required per build)
1. `FF8OPC_VERSION` in `src/ff8_accessibility.h`
2. Version comment near top of `src/field_navigation.cpp` (~line 5)
3. Init log string inside `src/field_navigation.cpp` Initialize() (~line 4683)
4. Version comment + init log in `src/battle_tts.cpp`

## KEY ADDRESSES (Battle-specific)
- Entity array base: `0x1D27B18` (7 × 0xD0: allies 0-2, enemies 3-6)
- Entity HP: +0x10 (cur), +0x14 (max) — uint16 allies, uint32 enemies  
- Entity ATB: +0x0C (cur), +0x08 (max) — uint16 allies, uint32 enemies
- Battle command cursor: `0x01D76843` (BYTE, 0-3)
- Active char ID: `0x01D76844` (BYTE, 255=none, 0-2=slot)
- Menu phase: `0x01D768D0` (BYTE, 1=open, 3=executing, 4=idle)
- ATB timers: `0x01D76968` (ally 0), `0x01D769D4` (ally 1)
- Savemap char data: `0x1CFE0E8` (8 × 0x98, equipped cmds at +0x50 as ability IDs)
- Party formation: `0x1CFE74C` (3 bytes: slot→charIdx)
- Entity statuses: +0x78 (persistent), +0x00-0x03 (timed)
- Battle result state: `0x1CFF6E7` (2=escaped, 4=won)
- XP earned: `0x1CFF574` (3 × uint16)
- AP earned: `0x1CFF5C0` (uint16)
- Prize items: `0x1CFF5E0` (4 × {id,qty})
- Draw slots: `0x1D28F18` base, 4 slots/enemy, 0x47 stride
- Encounter ID: `0x1CFF6E0` (WORD)
- Computed stats: `0x1CFF000` (3 × 0x1D0)
- Savemap base: `0x1CFDC5C`
