# NEXT SESSION PROMPT — FF8 Accessibility Mod
## Updated: 2026-03-25 (post-session 13, Magic sub-menu TTS + Limit Break detection)
## Current build: v0.10.22 (source + deployed)

---

## SESSION 13 RECAP: Battle Sub-menu TTS + Limit Break (v0.10.15–v0.10.22)

### GitHub pushed at session start
Commit: "v0.09.41-v0.10.14: Ability screen TTS, battle TTS phases 1-3"

### Confirmed New Battle Menu Addresses
| Address | Purpose |
|---------|---------|
| 0x01D768EC | Sub-menu list cursor (0-N, scrolls through spells/GFs/items) |
| 0x01D7684A | Limit Break toggle byte: 0=Attack, 64=Limit Break at cursor 0 |

### What's Working Now (v0.10.22)
- **Magic sub-menu TTS**: "Fire, 5" / "Cure, 3" as you scroll through spells
  - 57-entry MAGIC_NAMES[] table (kernel.bin IDs 0x00-0x38)
  - BuildMagicList() reads savemap char struct +0x10 (32 slots × 2 bytes)
  - 300ms debounce after turn start prevents false sub-menu entry detection
- **Limit Break toggle detection**: "Limit Break" / "Attack" announced when pressing Right on cursor 0
  - Toggle byte at 0x01D7684A (0=Attack, 64=Limit Break)
  - Turn start checks toggle byte for initial announcement
- **Command menu TTS** (from session 12): turn announcement + cursor navigation still working

### Known Issue: cursor=3 out of range with 3 spells
When a character has 3 spells, cursor wraps to position 3 which is out of range. The cursor byte represents visible list position (0-3 for 4 slots), not spell count. Need page offset byte for >4 spells (scrolled lists).

### Diagnostic code still present (cleanup needed)
- MENU-DIAG event-triggered scan (PollMenuDiagnostic) — 4096+2048 byte regions
- CURSOR-HUNT continuous poll (PollCursorHunter) — 512 bytes at 16ms
- PollLimitToggleFast/PollLimitToggleDiag stubs (empty, harmless)

---

## IMMEDIATE PRIORITY: Continue Battle TTS Phase 4

### Next items (in suggested order):
1. **GF sub-menu TTS** — sub-menu cursor at 0x01D768EC works for all sub-menus; need GF name lookup from savemap GF structs (savemap+0x4C, 16 GFs × 0x44 stride, name at +0x00)
2. **Item sub-menu TTS** — item display struct at 0x1D8DFF4 (already known from menu work)
3. **Draw sub-menu TTS** — draw slots at 0x1D28F18 (4 slots/enemy, 0x47 stride)
4. **Magic list scrolling for >4 spells** — need to find page offset byte in battle menu struct
5. **Target selection TTS** — target cursor address unknown, needs diagnostic. Phase sequence: 32→3→11 (target select)→14→21→33→34
6. **Damage/healing announcements** — HP polling already functional

### Key Documents
- **Implementation plan**: `Plan & Research Documents/Battle TTS implementation plan.md`
- **Memory map**: `Plan & Research Documents/Battle system memory map deep research results.md`

---

## SECOND PRIORITY: World Map Accessibility

Research prompt submitted but results not yet received.

---

## DEFERRED

- Junction Menu: Auto sub-options, manual magic-to-stat
- Top-level menu navigation TTS
- Save Game flow TTS  
- Save Point entity catalog integration
- Title Screen Continue TTS
- PSHM_W Option F (force entity script execution — deep research pending)
- Cleanup diagnostic code (MENU-DIAG, CURSOR-HUNT, v0.10.07 name scan)

---

## HOUSEKEEPING

- **GitHub push needed**: v0.10.15–v0.10.22 unpushed (8 builds this session)
- Git: `cd C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod && git add -A && git commit -m "v0.10.15-v0.10.22: Magic sub-menu TTS, Limit Break toggle detection" && git push origin main`

---

## VERSION BUMP LOCATIONS (4 required per build)
1. `FF8OPC_VERSION` in `src/ff8_accessibility.h`
2. Version comment near top of `src/field_navigation.cpp` (~line 5)
3. Init log string inside `src/field_navigation.cpp` Initialize() (~line 4683)
4. Version comment + init log in `src/battle_tts.cpp`

## KEY ADDRESSES (Battle-specific, updated)
- Entity array base: `0x1D27B18` (7 × 0xD0: allies 0-2, enemies 3-6)
- Battle command cursor: `0x01D76843` (BYTE, 0-3)
- Active char ID: `0x01D76844` (BYTE, 255=none, 0-2=slot)
- Menu phase: `0x01D768D0` (BYTE, 32=cmd menu, 3=executing, 11=target, etc.)
- Sub-menu list cursor: `0x01D768EC` (BYTE, 0-N)
- Limit Break toggle: `0x01D7684A` (BYTE, 0=Attack, 64=Limit Break)
- Savemap char data: `0x1CFE0E8` (8 × 0x98, magic at +0x10, cmds at +0x50)
- Party formation: `0x1CFE74C` (3 bytes: slot→charIdx)
- Draw slots: `0x1D28F18` base, 4 slots/enemy, 0x47 stride
- Item display: `0x1D8DFF4`
