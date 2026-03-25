# Battle TTS Implementation Plan
## Created: 2026-03-24
## Based on: Battle system memory map deep research results + FFNx source analysis + BATTLE_PLAN.md
## Starting build: v0.10.01 (new major feature = bump minor)

---

## What The Research Gave Us vs. What Still Needs Discovery

### CONFIRMED (high confidence, ready to code against)

| Area | Data | Source |
|------|------|--------|
| Entity array | 7 × 0xD0 at `0x1D27B18` (slots 0-2 = allies, 3-6 = enemies) | ff8-speedruns |
| HP per entity | +0x10 (cur), +0x14 (max) — uint16 allies, uint32 enemies | ff8-speedruns |
| ATB per entity | +0x0C (cur), +0x08 (max) — uint16 allies, uint32 enemies | ff8-speedruns |
| Stats per entity | +0xB4 level, +0xB5 STR, +0xB7 MAG, +0xB9 SPD, +0xC2 crisis | ff8-speedruns |
| Persistent statuses | +0x78 bitfield (KO/Poison/Petrify/Blind/Silence/Berserk/Zombie) | ff8-speedruns |
| Timed statuses | +0x00–0x03 bitfields (Sleep/Haste/Slow/Stop/Regen/etc.) | ff8-speedruns |
| Elemental resists | +0x3C–0x4A (8 elements, uint16 each, 500=weak/800=neutral/1000=absorb) | ff8-speedruns |
| Active char ID | `battle_current_active_character_id` (BYTE*) from FFNx `sub_4BB840 + 0x13` | FFNx source |
| New active char ID | `battle_new_active_character_id` (BYTE*) from FFNx `sub_4BB840 + 0x37` | FFNx source |
| Battle result state | `0x1CFF6E7` (2=escaped, 4=won) | ff8-speedruns |
| XP earned | `0x1CFF574`–`0x1CFF578` (3 × uint16) | ff8-speedruns |
| AP earned | `0x1CFF5C0` (uint16) | ff8-speedruns |
| Prize items | `0x1CFF5E0`–`0x1CFF5E7` (4 × {id,qty}) | ff8-speedruns |
| Post-battle flag | `0x1A78CA4` (uint8, bool) | ff8-speedruns |
| Draw spell slots | `0x1D28F18` base, 4 slots/enemy, 0x47 stride | ff8-speedruns |
| Encounter ID | `0x1CFF6E0` (WORD) | ff8-speedruns |
| Enemy names | `battle_get_monster_name_sub_495100` retrieves from loaded DAT | FFNx source |
| Command IDs | Full table (0x01=Attack through 0x26=Treatment) | Doomtrain wiki |
| Equipped commands | Savemap char +0x50 (3 bytes) | deep research |
| Party formation | Savemap +0xAF0 (maps slot→charIdx) | confirmed prior |
| Computed stats | `0x1CFF000` (3 × 0x1D0, curHP +0x172, maxHP +0x174) | confirmed prior |
| GF Boost gauge | `0x20DCEF0` (uint8, 0-255) | ff8-speedruns |
| Zell Duel timer | `0x1D76750` (uint16, countdown) | ff8-speedruns |
| Game mode | `common_externals._mode` — 999=battle, 3=swirl, 4=after_battle | FFNx source |

### GAPS (need diagnostic builds to resolve)

| Gap | Why It Matters | Discovery Approach |
|-----|---------------|-------------------|
| **Battle menu cursor position** | Can't announce Attack/Magic/GF/Draw/Item selection | Diagnostic: dump `battle_menu_state` region + scan for changing byte while navigating |
| **Sub-menu cursor** (spell/item/GF list) | Can't announce which spell/item is highlighted | Same diagnostic, different menu phase |
| **Target cursor index** | Can't announce which enemy/ally is targeted | Diagnostic: scan entity-adjacent memory while cycling targets |
| **Monster name retrieval** | Need to call `battle_get_monster_name_sub_495100` or find cached names | Diagnostic: trace pointer from `battle_char_struct_dword_1D27B10` |
| **Battle text routing** | Which battle messages flow through show_dialog vs. direct rendering? | Diagnostic: log all show_dialog calls during battle |
| **Encounter flags** (preemptive/back attack) | Need to announce ambush conditions at battle start | scene.out flags byte at encounter+0x01, need to confirm runtime copy |
| **Entity "alive/present" check** | Need to distinguish active vs. reserve enemies (8 loaded, 4 visible) | Diagnostic: check HP>0 + targetable bitmask |
| **Battle result screen phase** | Need to time result announcements (XP, AP, items, level-up) | Diagnostic: monitor `0x1A78CA4` + poll result fields |

---

## Architecture

### New Files

```
src/battle_tts.h         — BattleTTS namespace + struct declarations
src/battle_tts.cpp       — Core module (mode detection, polling, speech queue, all phases)
```

We do NOT need separate `battle_addresses.h/cpp` — the addresses are all static (no ASLR) and can be defined as constants in `battle_tts.h`. Dynamic resolution (active char ID pointers) uses FFNx's existing chains already exposed via `ff8_addresses.cpp`.

### Module Integration

- `dinput8.cpp`: Add `#include "battle_tts.h"`, call `BattleTTS::Initialize()` after `MenuTTS::Initialize()`, `BattleTTS::Update()` after `MenuTTS::Update()`, `BattleTTS::Shutdown()` in cleanup.
- `deploy.bat`: Add `"%SRC_DIR%\battle_tts.cpp"` to the cl compile list.
- Battle hotkeys (P = party readout, S = scan/enemy info, etc.) handled inside `BattleTTS::Update()`.

### Speech Priority System

Built into BattleTTS from the start. Priority levels (highest first):

| Priority | Category | Behavior |
|----------|----------|----------|
| 0 (CRITICAL) | KO / Game Over | Always interrupt |
| 1 (TURN) | "Squall's turn" / Limit Ready | Interrupt lower |
| 2 (MENU) | Cursor navigation (command/spell/item/target) | Interrupt lower, immediate |
| 3 (ACTION) | "Drew 3 Fire" / "Squall attacks!" | Queue |
| 4 (HP) | Damage/heal amounts | Queue, throttle multi-hit |
| 5 (STATUS) | "Rinoa poisoned" / "Protect wears off" | Queue |
| 6 (INFO) | Battle log, misc messages | Queue, dedup |

Implementation: `BattleTTS::Speak(const char* text, int priority, bool interrupt)`. If `interrupt` or new priority < current speaking priority, cancel current speech. Otherwise enqueue. Stale items (>5s) auto-dropped.

---

## Phase 1: Skeleton + Battle Entry/Exit + Enemy Announcement (v0.10.01–v0.10.05)

**Goal**: Module wired in, battle mode transitions detected, enemies announced at battle start.

### v0.10.01 — Module skeleton + mode detection
- Create `battle_tts.h` / `battle_tts.cpp` with Init/Update/Shutdown
- Wire into `dinput8.cpp` and `deploy.bat`
- Detect game mode 999 (battle entry) and mode != 999 (battle exit)
- On entry: log "BATTLE ENTERED". On exit: log "BATTLE EXITED"
- Add `s_inBattle` flag, `s_battleJustStarted` edge trigger

### v0.10.02 — Resolve active character pointers
- In `ff8_addresses.cpp`, add resolution for `battle_current_active_character_id` and `battle_new_active_character_id` using FFNx's `sub_4BB840` chain
- Also resolve `battle_char_struct_dword_1D27B10` (BYTE** at `battle_get_monster_name_sub_495100 + 0xF`)
- Log resolved addresses at init

### v0.10.03 — Entity array reader + enemy count
- Read entity array at `0x1D27B18` (slots 3-6 for enemies)
- Determine active enemies: HP > 0 at slot+0x10 (uint32 for enemies)
- Log count + HP values for each active enemy slot
- **NOTE**: Must read HP as uint32 for enemy slots (offset +0x10, 4 bytes)

### v0.10.04 — Enemy name retrieval
- Trace `battle_char_struct_dword_1D27B10` pointer to get entity name data
- Call or replicate `battle_get_monster_name_sub_495100` logic to extract enemy names
- Decode FF8 string encoding to ASCII using existing `ff8_text_decode`
- Log "Enemy 1: Bite Bug, HP 200/200" etc.

### v0.10.05 — Battle start TTS announcement
- On battle entry (after ~500ms delay for engine init), read enemy array
- Announce: "Battle! 2 Bite Bugs" or "Battle! Ifrit" (group identical names)
- Announce preemptive/back attack if detectable from encounter flags
- Speech priority: TURN level (interrupt nothing since battle just started)

**Test**: Enter a battle, hear enemy composition announced.

---

## Phase 2: Turn Announcement + ATB Tracking (v0.10.06–v0.10.10)

**Goal**: Announce whose turn it is when ATB fills.

### v0.10.06 — ATB polling diagnostic
- Each frame in battle: read `current_ATB` (+0x0C) vs `max_ATB` (+0x08) for ally slots 0-2
- Read `battle_current_active_character_id` pointer
- Log: "Slot 0 ATB: 1234/5000, active_char_id=0"
- Confirm ATB fill detection works: `current >= max` AND KO bit clear (+0x78 & 0x01 == 0)

### v0.10.07 — Turn announcements
- Track `s_lastActiveCharId`. When `battle_current_active_character_id` changes to a new value:
  - Map slot index to character name via formation array (savemap+0xAF0) → charIdx → name
  - Announce "[Name]'s turn"
- Also check for Limit Break: if crisis level (+0xC2) > 0, append "Limit ready!"

### v0.10.08 — ATB ready queue awareness
- Multiple characters can have full ATB simultaneously
- When one turn is completed, the next ready character auto-activates
- Track all 3 ally ATB states, announce in order as they become active
- Suppress duplicate announcements (same char announced twice without acting)

### v0.10.09 — Edge cases
- Handle party member KO (skip ATB tracking for dead members)
- Handle Berserk/Confuse (auto-act, no menu — just announce "[Name] is berserk, attacks!")
- Handle Stop status (ATB frozen, skip turn announce)

### v0.10.10 — Refinement
- Tune announcement timing (slight delay after ATB fills to avoid rapid-fire)
- Test with Haste/Slow effects

**Test**: Play a battle, hear turn announcements as each character's ATB fills.

---

## Phase 3: Command Menu TTS — DIAGNOSTIC PHASE (v0.10.11–v0.10.18)

**Goal**: Find battle menu cursor address, then announce command navigation.

This is the CRITICAL unknown. The deep research says `battle_menu_state` (from `battle_pause_window_sub_4CD350 + 0x29`) tracks menu phase, but cursor POSITION within the command list is undocumented.

### v0.10.11 — battle_menu_state diagnostic
- Resolve `battle_menu_state` pointer via FFNx chain
- Each frame during battle: log value changes of `battle_menu_state` region (dump ~64 bytes around the pointer)
- Aaron navigates Attack→Magic→GF→Draw→Item, we look for a byte that cycles 0→1→2→3→4

### v0.10.12 — Broader scan diagnostic
- If battle_menu_state region doesn't contain cursor:
  - Scan a wider region (256 bytes around `battle_menu_state`)
  - Also scan around pMenuStateA (the field menu struct sometimes reused?)
  - Log all bytes that change when Aaron presses up/down in command menu

### v0.10.13 — show_dialog battle classification
- Inside `FieldDialog::Hook_Show_Dialog`, when game mode == 999:
  - Log window ID, text content, position, current_choice for EVERY dialog call
  - This may reveal the command menu as a WinObj with `current_choice` tracking the cursor
- This is the alternate path: if the battle menu uses the same WinObj system as field menus, the cursor is in the window object itself

### v0.10.14 — Command menu TTS (once cursor found)
- Build command name table: index → "Attack", "Magic", "GF", "Draw", "Item", plus junctioned commands
- For junctioned commands: read savemap char+0x50 (3 equipped command IDs), map via COMMAND_NAMES table
- Each frame: check cursor. On change → speak command name
- Speech priority: MENU (interrupts immediately on cursor change)

### v0.10.15 — Character command list building
- Each character has: Attack (always) + up to 3 junctioned commands
- Build per-character command list at turn start from equipped commands
- "Attack" at index 0, then equipped[0], equipped[1], equipped[2]
- Limit Break replaces Attack when crisis > 0 — announce limit name instead

### v0.10.16–v0.10.18 — Sub-menu entry + cursor
- Detect transition from command menu → sub-menu (Magic/GF/Draw/Item list)
- Sub-menu cursor likely in a different byte than command cursor
- Magic list: read character's magic inventory (savemap char+0x10, 32 slots × 2 bytes)
- GF list: read junctioned GFs from savemap char+0x58 bitmask
- Item list: read from inventory
- Draw list: read from `0x1D28F18` + enemy stride (already confirmed addresses)
- Announce highlighted spell/GF/item name on cursor change

**Test**: Navigate through Attack→Magic→[spell list]→target, hearing each selection spoken.

---

## Phase 4: Target Selection TTS (v0.10.19–v0.10.24)

**Goal**: Announce which enemy/ally is currently targeted.

### v0.10.19 — Target cursor diagnostic
- When in target selection mode (after confirming a command):
  - Scan memory near entity array for a byte that changes when cycling targets with left/right
  - Also check `battle_menu_state` region for target index
  - Log all changing bytes while Aaron cycles through enemies

### v0.10.20 — Target announcement
- On cursor change: read enemy name from entity array + name retrieval function
- Announce: "Bite Bug A" / "Bite Bug B" (disambiguate duplicates with letters)
- For ally targeting (heal spells): "Squall" / "Rinoa" / "Zell"
- For all-targets: "All enemies" / "All allies"

### v0.10.21 — Target type detection
- Command's target byte flags from kernel.bin determine valid targets
- 0x01=enemy default, 0x02=can target allies, 0x04=toggleable, 0x08=single, 0x10=multi
- Announce target switch when player toggles enemy↔ally targeting

### v0.10.22–v0.10.24 — Edge cases
- Multi-target commands (Quake, Tornado) → announce "All enemies"
- Dead-target commands (Life, Phoenix Down) → only cycle KO'd allies
- Self-only commands (Defend) → auto-target, announce character name
- Random target commands → announce "Random enemy"

**Test**: Select Magic→Fire, hear "Bite Bug A", press right, hear "Bite Bug B".

---

## Phase 5: HP and Status Tracking (v0.10.25–v0.10.32)

**Goal**: Real-time party health awareness + status effect announcements.

### v0.10.25 — Party HP polling
- Each frame: read curHP/maxHP for ally slots 0-2 from entity array (+0x10/+0x14, uint16)
- Cache previous values. On change:
  - Decrease: "Squall took [delta] damage" (or "Squall took heavy damage" for >25% max HP)
  - Increase: "Squall healed [delta]"
  - Reach 0: "Squall is KO'd!" (CRITICAL priority, interrupt)

### v0.10.26 — Enemy HP tracking
- Track enemy HP changes (uint32 at +0x10 for enemy slots)
- On enemy HP reaching 0: "Bite Bug defeated!" (ACTION priority)
- All enemies defeated → "Victory!" announcement before result screen

### v0.10.27 — Status effect tracking
- Cache persistent status byte (+0x78) and timed status bytes (+0x00–0x03) per entity
- On bit flip 0→1: announce "Squall is [status]!" 
- On bit flip 1→0: announce "[Status] wears off on Squall"
- Status name table: KO, Poison, Petrify, Blind, Silence, Berserk, Zombie, Sleep, Haste, Slow, Stop, Regen, Protect, Shell, Reflect, Aura, Curse, Doom, Float, Confuse, Double, Triple, Defend, Vit 0, Angel Wing

### v0.10.28 — Throttling for multi-hit / DOT
- Renzokuken (4-8 hits), Angelo Rush, etc. produce rapid HP changes
- Aggregate: announce first hit, then "X more hits, total [sum] damage"
- Poison/Regen ticks: announce at most every 3 seconds, not every tick

### v0.10.29 — On-demand party readout (P key)
- Press P in battle: read all 3 party members
- Format: "Squall, 1234 of 3000 HP, poisoned. Rinoa, 2100 of 2500 HP. Zell, KO."
- Include active statuses (both persistent and timed)

### v0.10.30 — On-demand enemy readout (S key)
- Press S: cycle through active enemies
- Format: "Bite Bug A, about half health" (approximate since enemy max HP not always obvious)
- If Scan has been used: "Bite Bug A, 120 of 200 HP, weak to Fire"

### v0.10.31–v0.10.32 — GF HP during summon
- During GF summon: summoning character's HP bar shows GF HP
- Read GF HP from savemap GF struct: savemap+0x4C + (GF_idx × 0x44) + 0x12
- Announce GF damage: "Ifrit took [X] damage" / "Ifrit was KO'd!"

**Test**: Take damage in battle, hear HP change announcements. Press P, hear full party status.

---

## Phase 6: Battle Results (v0.10.33–v0.10.37)

**Goal**: Announce post-battle rewards.

### v0.10.33 — Victory detection
- Monitor `battle_result_state` at `0x1CFF6E7`: value changes to 4 = won, 2 = escaped
- On won: announce "Victory!"
- On escaped: announce "Escaped!"

### v0.10.34 — Result screen data
- When `In post-battle screen` (`0x1A78CA4`) becomes true:
  - Read XP earned per ally (3 × uint16 at `0x1CFF574`)
  - Read AP earned (`0x1CFF5C0`)
  - Read prize items (4 × {id,qty} at `0x1CFF5E0`)
- Announce: "Earned [X] EXP, [Y] AP. Received [item name] ×[qty]."

### v0.10.35 — Level-up detection
- Cache party levels (entity +0xB4) at battle start
- After battle: compare against savemap char levels
- If level increased: "Squall leveled up to [X]!"

### v0.10.36 — GF ability learned
- Cache GF completeAbilities bitmaps at battle start
- After battle: diff against current bitmaps
- If new ability bit set: "Quezacotl learned Card!"
- This requires AP table + ability name lookup (already have ABILITY_NAMES[116] from ability screen)

### v0.10.37 — Gil earned
- Read Gil from savemap (`0x1CFE764`) before and after battle
- Announce delta: "Earned [X] Gil"

**Test**: Win a battle, hear "Victory! Earned 45 EXP, 3 AP. Received Potion ×2. Earned 100 Gil."

---

## Phase 7: Draw System (v0.10.38–v0.10.42)

**Goal**: Announce available draw spells and draw results.

### v0.10.38 — Draw list reading
- When Draw command is selected and target chosen:
  - Read 4 draw slots from `0x1D28F18 + (enemy_slot - 3) * 0x47`
  - Map magic IDs to spell names via MAGIC_NAMES table
  - Announce: "Fire, Blizzard, Cure, [empty]"

### v0.10.39 — Draw sub-menu cursor
- Draw presents: Stock / Cast choice, then spell selection
- Find cursor byte (part of Phase 3 sub-menu discovery)
- Announce highlighted draw option

### v0.10.40 — Draw result announcement
- After draw action: the game shows "Drew [X] [Spell]" via battle text
- Capture via show_dialog hook or read magic inventory delta
- Announce: "Drew 3 Fire. Now stocked: 45"

### v0.10.41 — Draw point awareness (enemy Scan integration)
- On first encounter with enemy type, proactively announce drawable spells
- "Bite Bug has Fire, Blizzard available to draw"
- Only announce once per enemy type per battle (dedup)

### v0.10.42 — Edge cases
- Draw failure ("Failed to draw")
- Draw cast (using drawn spell immediately instead of stocking)
- Full stock (already have 100 of that spell)

**Test**: Use Draw command, hear available spells, hear draw result.

---

## Phase 8: Battle Events + Limit Breaks (v0.10.43–v0.10.50)

**Goal**: Announce battle flow events, GF summons, and limit breaks.

### v0.10.43 — Battle text capture
- Log all show_dialog calls during battle mode
- Classify: command execution text ("Squall attacks!"), damage text, status text, system messages
- Route appropriate messages through TTS with ACTION/INFO priority

### v0.10.44 — GF summon detection
- Detect GF summon start: `battle_magic_id` changes to a GF effect ID (Quezacotl=115, Shiva=184, Ifrit=200, etc.)
- Announce: "Summoning Ifrit"
- Optionally: "Boost! Press Square repeatedly" (GF Boost at `0x20DCEF0`)

### v0.10.45 — Limit Break activation
- When crisis level (+0xC2) > 0 and Attack command is replaced:
  - Squall: "Renzokuken ready!"
  - Selphie: "Slot ready!" (then announce spell options)
  - Zell: "Duel ready!" (timer at `0x1D76750`)
  - Quistis: "Blue Magic ready!"
  - Irvine: "Shot ready!"
  - Rinoa: "Combine ready!" / "Angel Wing ready!"

### v0.10.46 — Limit Break sub-menus
- Selphie Slot: announce the randomly offered spell, "Do Over" option
- Zell Duel: announce available commands (complex — may need cursor discovery)
- Irvine Shot: announce ammo type selection
- Quistis Blue Magic: announce available blue magic spells

### v0.10.47–v0.10.50 — Polish and edge cases
- Gunblade trigger timing (Squall): audio cue or just announce damage
- Angelo limit breaks (random triggers)
- Doom countdown announcements ("Doom: 3... 2... 1...")
- Ultimecia Castle seal announcements (check `0x1CFF6E8` bitmask — "Magic is sealed!")

**Test**: Trigger a limit break, hear availability and sub-menu options.

---

## Estimated Build Count Summary

| Phase | Description | Builds | Running Total |
|-------|-------------|--------|---------------|
| 1 | Skeleton + entry/exit + enemies | 5 | v0.10.01–05 |
| 2 | Turn announcements + ATB | 5 | v0.10.06–10 |
| 3 | Command menu TTS (includes diagnostics) | 8 | v0.10.11–18 |
| 4 | Target selection | 6 | v0.10.19–24 |
| 5 | HP + status tracking | 8 | v0.10.25–32 |
| 6 | Battle results | 5 | v0.10.33–37 |
| 7 | Draw system | 5 | v0.10.38–42 |
| 8 | Events + limits | 8 | v0.10.43–50 |
| **Total** | | **~50 builds** | v0.10.01–v0.10.50 |

Realistic expectation: 50-65 builds (diagnostics may reveal unexpected complications, as they always do). This is comparable to field navigation (~60 builds).

---

## Data Tables Needed

### COMMAND_NAMES (battle commands)
```c
static const char* COMMAND_NAMES[] = {
    "",              // 0x00 unused
    "Attack",        // 0x01
    "Magic",         // 0x02
    "GF",            // 0x03
    "Draw",          // 0x04
    "Item",          // 0x05
    "Card",          // 0x06
    "Devour",        // 0x07
    ...
    "Mug",           // 0x24
    "",              // 0x25
    "Treatment",     // 0x26
};
```

### STATUS_NAMES (persistent + timed)
```c
// Persistent (+0x78)
static const char* PERSISTENT_STATUS[] = {
    "KO", "Poison", "Petrify", "Blind", "Silence", "Berserk", "Zombie"
};
// Timed byte 0 (+0x00)
static const char* TIMED_STATUS_0[] = {
    "Sleep", "Haste", "Slow", "Stop", "Regen", "Protect", "Shell", "Reflect"
};
// Timed byte 1 (+0x01)  
static const char* TIMED_STATUS_1[] = {
    "Aura", "Curse", "Doom", "Invincible", "Gradual Petrify", "Float", "Confuse"
};
// Timed byte 2 (+0x02) — selected
static const char* TIMED_STATUS_2[] = {
    "Eject", "Double", "Triple", "Defend", "", "", "Retribution"
};
// Timed byte 3 (+0x03) — selected
static const char* TIMED_STATUS_3[] = {
    "Vit 0", "Angel Wing"
};
```

### MAGIC_NAMES (0-based magic IDs)
- Already partially in the mod from existing menu work
- Full table needed: 57 spells + special entries
- Source: kernel.bin Section 2 text, or hardcode from wiki

### ELEMENT_NAMES
```c
static const char* ELEMENT_NAMES[] = {
    "Fire", "Ice", "Thunder", "Earth", "Poison", "Wind", "Water", "Holy"
};
```

---

## Hotkey Summary (Battle Mode)

| Key | Action |
|-----|--------|
| P | Party readout (all 3 members: name, HP, statuses) |
| S | Enemy info (cycle through enemies: name, approx HP, known weaknesses) |
| H | Toggle target window (existing game function) |
| ` (grave) | Repeat last battle announcement |

---

## Risk Register

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Battle menu uses a completely novel UI system (not WinObj) | Phase 3 blocked until cursor found via brute-force scan | show_dialog classification + wider memory scan |
| Enemy names require calling game function (not just memory read) | Need to set up correct calling convention for `battle_get_monster_name_sub_495100` | Alternative: scan for name strings near entity data at runtime |
| ATB types differ (uint16 vs uint32) and code reads wrong size | Garbage values, crashes | Always branch on slot index: 0-2 = uint16 (allies), 3-6 = uint32 (enemies) |
| Multi-hit attack TTS spam | Annoying rapid-fire speech | Aggregate hits, announce total after brief delay |
| show_dialog doesn't fire for most battle text (rendered as sprites) | Phases relying on show_dialog for battle log won't work | Direct memory polling for all critical data; show_dialog is supplementary |
| Limit break sub-menus are custom UI (Zell Duel especially) | Those limit break TTS may need significant additional research | Defer complex limit UIs to v2; announce availability only in v1 |
| Encounter flags at scene.out need runtime address for loaded data | Can't announce preemptive/back attack | Use encounter ID at `0x1CFF6E0` to index into scene.out if loaded, or just skip |

---

## Session Start Checklist

1. Read DEVNOTES.md + NEXT_SESSION_PROMPT.md
2. Read this plan document
3. Read `Plan & Research Documents/Battle system memory map deep research results.md` (the full memory map)
4. Start with Phase 1, v0.10.01 (module skeleton)
5. Bump version in 3 locations before each build
