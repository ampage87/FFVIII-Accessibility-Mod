# Battle Accessibility Implementation Plan

## Overview

This document maps out the work needed to make FF8's battle sequences accessible via TTS. Battle is the single largest remaining feature gap — without it, a blind player cannot meaningfully play the game.

The plan is organized into **phases**, ordered by dependency and value. Each phase produces a testable, useful increment.

---

## What We Know vs. What We Must Discover

### Known (from existing mod + FFNx source)

- **Game mode 999** = battle (`FF8_MODE_BATTLE` in ff8.h). Our `pGameMode` pointer already resolves this — battle entry/exit detection is trivial.
- **show_dialog hook** is already installed globally. Battle text (menu commands, battle log messages, Scan results) flows through the same text rendering path as field dialogs. The hook fires in battle mode.
- **Text decoder** already handles both field and menu font encodings. Battle uses the same system font.
- **Module pattern** is established: `BattleTTS::Init()`, `BattleTTS::Update()`, `BattleTTS::Shutdown()` — slot into dinput8.cpp's main loop exactly like MenuTTS and FieldNavigation.
- **No ASLR** — once we find addresses, they're stable.
- **FF8 battle effect IDs** are documented in FFNx's `ff8/battle/effects.h` (GF IDs, spell IDs).
- **H key** toggles target window in battle (per PC manual).

### Unknown (requires reverse-engineering)

These are the critical unknowns that will gate each phase:

1. **Battle menu cursor position** — which memory address holds the index of the currently highlighted command (Attack / Magic / GF / Draw / Item / etc.)? And the submenu cursor within Magic/GF/Draw/Item lists?
2. **Character stats struct in battle** — where are curr_hp, max_hp, status_flags for each of the 3 active party members? FFNx references `compute_char_stats_sub_495960` but doesn't expose the result pointer. Need to find the actual array base address.
3. **Active character index** — which party slot (0/1/2) currently has the ATB cursor? FFNx internally has `battle_current_active_character_id` but we need the resolved address.
4. **Enemy data struct** — names, HP, status for each enemy (up to 4 enemies). The Scan window likely reads from this.
5. **Target selection cursor** — when in target-selection mode, which enemy/ally index is highlighted?
6. **Battle window classification** — how do we distinguish command menus from battle log messages from Scan windows, given that all use `show_dialog`? Window ID, position, or content heuristics.
7. **ATB state** — is ATB gauge data accessible? (Lower priority — may not be needed for MVP.)
8. **Limit Break state** — how does the game signal that a Limit Break is available? Is it a flag on the character stat struct?

---

## Phase 0: Reconnaissance (REQUIRED FIRST)

**Goal**: Find the critical memory addresses before writing any battle code.

### Approach

This is primarily a ChatGPT deep research + binary analysis task, similar to how we found the menu cursor at `pMenuStateA + 0x1E6` and the save screen structures.

**Deep research prompts to prepare for Aaron:**

1. **Battle character stats struct**: "In the FF8 PC Steam 2013 edition (FF8_EN.exe), find the memory layout of the battle character stats array. The function `compute_char_stats_sub_495960` (from FFNx source) computes these. I need: base address of the 3-slot party stats array, offsets for current_hp, max_hp, status_flags, character_name_index, and any ATB-related fields. Also find the pointer or global variable that holds `battle_current_active_character_id` (the party slot index of whoever's turn it is)."

2. **Battle menu cursor**: "In FF8 PC Steam 2013 (FF8_EN.exe), how does the battle command menu track the current cursor position? When the player highlights Attack vs. Magic vs. GF vs. Draw vs. Item, what memory address stores this selection index? Also need the submenu cursor (e.g., which spell is highlighted inside the Magic list). The win_obj struct's `current_choice` field may be relevant — what offset within the battle window object holds the highlighted item index?"

3. **Enemy data**: "In FF8 PC Steam 2013, where is the enemy data array in battle memory? Need: base address, stride per enemy, offsets for enemy_name (or name index), current_hp, max_hp, level, status_flags. The Scan command reads from this data. Also: how many enemies max (believe 4)."

4. **Target selection**: "In FF8 PC Steam 2013 battle system, when the player is choosing a target (enemy or ally), what memory address holds the currently highlighted target index? How does the game distinguish targeting enemies vs. allies vs. all?"

### Alternative: Binary analysis approach

If deep research doesn't yield addresses directly, we can:
- Hook `show_dialog` in battle mode and log every window ID, position, and text content
- Dump memory around known pointers (pMenuStateA region) during battle
- Trace `compute_char_stats_sub_495960` call chain from FFNx source to find the output pointer
- Use the FFNx source's `ff8_find_externals()` chains as starting points for battle-specific address resolution

### Deliverable

A set of confirmed addresses (or at minimum, strong candidates to scan for) before coding begins.

---

## Phase 1: Battle Entry/Exit + show_dialog Classification

**Goal**: Detect battle mode transitions and classify what the existing show_dialog hook captures during battle.

### Tasks

1. **BattleTTS module skeleton** — new files `battle_tts.cpp` / `battle_tts.h`. Init/Update/Shutdown pattern. Wire into dinput8.cpp after MenuTTS::Update().

2. **Mode transition detection** — in `BattleTTS::Update()`, compare current `*pGameMode` against 999. On entry: announce "Battle!" via TTS, initialize per-battle state. On exit: announce "Battle over", flush speech queue.

3. **show_dialog battle branch** — inside `FieldDialog::Hook_Show_Dialog`, add a branch when `*pGameMode == 999`. Log every window: ID, decoded text, position (x/y/w/h from WinObj), current_choice index. This is DIAGNOSTIC ONLY — no TTS yet. Goal is to classify which windows appear for commands, submenus, battle log, target list, Scan results.

4. **Window classification heuristics** — from the diagnostic logs, develop rules:
   - Command menu: appears when it's a character's turn, contains "Attack", "Magic", etc.
   - Submenus: opened after selecting Magic/GF/Draw/Item, contain spell names / GF names / etc.
   - Battle log: bottom-of-screen messages like "Squall attacks!" or "Critical!"
   - Target window: toggled by H key, lists enemy/ally names
   - Scan window: shows enemy HP/weakness after Scan cast

### Deliverable

Diagnostic logs that map every battle window to a classification. This tells us exactly what the existing hook already gives us "for free" and what needs additional memory reads.

---

## Phase 2: Command Menu TTS

**Goal**: Speak the currently highlighted battle command as the player navigates.

### Dependency

Phase 0 (battle menu cursor address) OR Phase 1 (if show_dialog's current_choice field on the command window already gives us the cursor position).

### Tasks

1. **Command menu detection** — identify command menu window by ID or text pattern. When it appears (character's turn starts), announce whose turn it is: "Squall's turn" (requires active character index from Phase 0 or character name from show_dialog context).

2. **Cursor tracking** — on each frame while command menu is active, check current_choice. If changed, speak the new command name. Map choice index to command strings (Attack, Magic, GF, Draw, Item, and any junctioned commands like Card, Doom, Mug, etc.).

3. **Submenu entry** — when Magic/GF/Draw/Item is confirmed, the command menu closes and a list opens. Detect this transition. In the submenu:
   - Magic: speak spell name as cursor moves
   - GF: speak GF name
   - Draw: speak "Stock" / "Cast" choice, then spell name
   - Item: speak item name

4. **Submenu cursor tracking** — same approach: detect submenu window, track current_choice, speak on change.

### Deliverable

A blind player can navigate the battle command menu and submenus by ear. This is the single most critical battle feature — without it, the player literally cannot act.

---

## Phase 3: Target Selection TTS

**Goal**: Announce which enemy or ally is currently targeted.

### Dependency

Phase 0 (target cursor address) OR show_dialog classification (if target window text is parseable).

### Tasks

1. **Target window detection** — the H key toggles a target list window. Detect when it's active. Parse text into individual target names.

2. **Target cursor tracking** — announce the highlighted target name. Distinguish "Enemy: Bite Bug" vs. "Ally: Squall". If targeting all, announce "All enemies" or "All allies".

3. **Default target announcement** — even without the target window open, when the player confirms a command that requires targeting, the game enters target mode. Announce the default target (usually first enemy for attack spells, first ally for heals).

### Deliverable

A blind player knows who they're targeting before confirming an action.

---

## Phase 4: HP and Status Readout

**Goal**: Announce character HP changes and status effects.

### Dependency

Phase 0 (character stats struct address, status flags).

### Tasks

1. **HP polling** — each frame during battle, read curr_hp for all 3 party members. Compare against previous values. On decrease: "Squall took 234 damage" (or "Squall took heavy damage" if throttling). On increase: "Squall healed 500".

2. **KO detection** — if HP reaches 0, announce "Squall is KO'd!" (high priority, interrupt other speech).

3. **Status tracking** — maintain a bitmask cache per character. On change, announce new statuses: "Rinoa is poisoned", "Protect wears off on Zell". Key status IDs: Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Slow, Stop, Doom, Confuse, Protect, Shell, Haste, Reflect.

4. **On-demand party readout** — assign a hotkey (e.g., P) that reads all 3 party members: "Squall 1234 of 3000 HP, poisoned. Rinoa 2100 of 2500 HP. Zell KO."

5. **Active character announcement** — when ATB fills and control switches to a new character, announce their name and HP.

### Deliverable

The blind player has full awareness of party health and status at all times.

---

## Phase 5: Battle Messages and Events

**Goal**: Speak battle log messages, attack names, and important events.

### Tasks

1. **Battle log TTS** — when show_dialog fires for a battle log window (classified in Phase 1), speak the text. Filter out empty/duplicate messages. Examples: "Squall attacks!", "Critical!", "Enemy was absorbed!"

2. **Damage to enemies** — if enemy HP addresses are known (Phase 0), announce damage dealt: "Bite Bug took 500 damage" or "Enemy defeated!" on HP reaching 0.

3. **GF summoning** — detect GF sequence start (either via show_dialog text or game state). Announce "Summoning Ifrit". Optionally announce Boost availability.

4. **Limit Break** — detect when Limit Break is available (character HP low or Crisis Level met). Announce "Limit ready!" When limit menu opens, speak the options (Renzokuken, Shot, etc.).

5. **Draw results** — after Draw command, announce "Drew 3 Tornado from enemy" (likely a show_dialog message).

6. **Victory** — announce "Victory!" and any post-battle results (EXP, AP, items).

### Deliverable

Full narrative awareness of what's happening in battle.

---

## Phase 6: Scan and Enemy Information

**Goal**: Make the Scan command's results accessible.

### Tasks

1. **Scan window detection** — when Scan is cast, a popup shows enemy name, HP, level, weaknesses. This should come through show_dialog. Capture and speak the full content.

2. **Enemy catalog** — if enemy addresses are known, optionally allow a hotkey to cycle through enemies and announce their names and approximate HP (e.g., "Bite Bug, about half health").

### Deliverable

Blind players can use Scan effectively and have tactical awareness of enemies.

---

## Speech Priority and Throttling

This must be designed from Phase 1 onward and refined as features are added:

**Priority levels (highest first):**
1. KO / Game Over — always interrupt
2. Character turn announcement — interrupt lower
3. Menu cursor changes — interrupt lower
4. Target changes — interrupt lower
5. Limit Break ready — queue
6. HP damage/heal (significant) — queue
7. Status changes — queue
8. Battle log messages — queue (lowest)

**Throttling rules:**
- Multi-hit attacks: aggregate or announce only first and last hit, plus total
- Rapid HP changes (poison ticks, regen): announce every N seconds, not every tick
- Menu navigation: interrupt previous command name immediately (user is scrolling fast)
- Same message deduplication: don't repeat identical strings within 2 seconds

**Implementation:**
- FIFO speech queue with priority preemption
- Each queued item has: text, priority level, timestamp, dedup key
- Update loop processes queue, speaking highest priority first
- Items older than 5 seconds are dropped (stale)

---

## File Structure

```
src/
  battle_tts.h        — BattleTTS namespace declarations
  battle_tts.cpp      — Core battle TTS module (mode detection, speech queue, polling)
  battle_addresses.h   — Battle-specific memory address declarations
  battle_addresses.cpp — Battle address resolution (like ff8_addresses but for battle structs)
```

Add to deploy.bat: `battle_tts.cpp` and `battle_addresses.cpp` in the compile list.

---

## Estimated Effort by Phase

| Phase | Description | Builds | Dependency |
|-------|-------------|--------|------------|
| 0 | Reconnaissance (addresses) | 0 (research) | None — do first |
| 1 | Entry/exit + show_dialog classification | 3-5 | Phase 0 started |
| 2 | Command menu TTS | 8-15 | Phase 0 (cursor addr) or Phase 1 |
| 3 | Target selection TTS | 5-8 | Phase 0 (target addr) or Phase 1 |
| 4 | HP and status readout | 8-12 | Phase 0 (stats struct) |
| 5 | Battle messages and events | 5-10 | Phases 1-4 |
| 6 | Scan and enemy info | 3-5 | Phase 0 (enemy struct) |

**Total: ~30-55 builds**, comparable to field navigation (which took ~60 builds from v05.00 to v06.22).

---

## Risks and Open Questions

1. **Battle text may not all flow through show_dialog.** Some battle UI elements (HP numbers, ATB bars, damage popups) might be drawn directly as sprites/textures without calling the text rendering path. If so, we must read from memory directly — show_dialog alone won't be sufficient for HP/damage.

2. **Menu cursor may be embedded in battle-specific window objects**, not in a global pointer like the field menu cursor. The WinObj struct's current_choice field is our best first bet.

3. **Battle runs at a different tick rate** than field mode. Our 16ms polling loop should be fine, but if ATB resolution is finer, we might miss fast events. Unlikely to be a problem for TTS.

4. **Enemy names may require decoding from battle data files** (scene.out or similar), not just from show_dialog text. If the target window doesn't show names, we'll need to read enemy name pointers from the enemy data struct.

5. **GF Boost mini-game** requires timed button presses. Making this accessible is a stretch goal — at minimum we announce Boost is active and let the player try.

6. **Gunblade trigger** (Squall's R1 timing on attacks) is similarly a timing mini-game. Stretch goal: audio cue for the trigger window. MVP: just announce the attack and damage.

---

## Next Steps

1. **Prepare deep research prompts** for Aaron to send to ChatGPT (Phase 0)
2. While waiting for research results, start Phase 1 (show_dialog classification) — this is purely diagnostic and doesn't need battle addresses
3. Once Phase 0 yields addresses, begin Phase 2 (command menu TTS) — this is the highest-value feature

---

## Deep Research Prompt (Ready to Send)

```
I'm building an accessibility mod for Final Fantasy VIII PC (Steam 2013 edition, FF8_EN.exe, no ASLR). I need to find battle system memory addresses in the game's executable. The game uses FFNx v1.23.x as a rendering replacement (github.com/julianxhokaxhiu/FFNx for reference source code).

Specifically, I need:

1. BATTLE CHARACTER STATS ARRAY
   - The function compute_char_stats_sub_495960 (referenced in FFNx source) computes character stats during battle. Where does it store the results?
   - I need: base address of the 3-slot party stats array in battle, and offsets for: current_hp, max_hp, status_flags (bitmask), character_id/name_index
   - Also: the global variable or pointer that stores which party slot (0/1/2) currently has the ATB-filled turn (battle_current_active_character_id or similar)

2. BATTLE COMMAND MENU CURSOR
   - When the player highlights Attack / Magic / GF / Draw / Item in the battle command menu, what memory address stores the current selection index?
   - Also: inside submenus (Magic spell list, GF list, Item list, Draw list), what address stores the highlighted item index?
   - The game's window object (WinObj) struct is used for dialog windows — does battle use the same struct? If so, what's the base address of the battle command window object?

3. ENEMY DATA ARRAY
   - Base address of the enemy data array during battle (up to 4 enemies)
   - Stride per enemy entry
   - Offsets for: enemy_name (or name string pointer), current_hp, max_hp, level, status_flags, elemental_weaknesses
   - The Scan command reads from this structure

4. TARGET SELECTION
   - When the player is choosing a target (after selecting an action), what memory address stores the highlighted target index?
   - How does the game distinguish targeting enemies vs allies vs all?

5. ATB GAUGE
   - Where is ATB fill percentage stored for each party member?
   - What triggers the "character ready" state transition?

6. BATTLE STATE FLAGS
   - Is there a "battle phase" flag (menu open, executing action, animating, etc.)?
   - Where is the Limit Break availability flag per character?

The game is 32-bit x86 with no ASLR. FF8_EN.exe base is 0x00400000. The FFNx source (github.com/julianxhokaxhiu/FFNx) has address resolution chains in src/ff8_data.cpp that may help trace these. The Qhimm wiki (wiki.ffrtt.ru) documents many FF8 internals. Deling (github.com/myst6re/deling) and Hyne save editors also document memory layouts.

Please find as many concrete memory addresses and struct layouts as possible, with sources. Even partial information (e.g., "the battle stats array starts near 0x1D7xxxx based on Hyne's save editor") would be valuable.
```
