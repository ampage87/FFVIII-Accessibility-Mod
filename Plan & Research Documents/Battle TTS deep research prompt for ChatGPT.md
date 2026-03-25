# FF8 Battle System Deep Research Prompt for ChatGPT
## Date: 2026-03-24
## Purpose: Gather detailed memory layout information for FF8 battle TTS accessibility mod

---

## PROMPT TO SUBMIT TO CHATGPT DEEP RESEARCH:

---

I'm building an accessibility mod for blind players for Final Fantasy VIII (Steam 2013 PC edition, FF8_EN.exe, running with FFNx v1.23.x). The mod is a Win32 DLL (`dinput8.dll`) injected alongside FFNx. I need detailed memory layout information for the battle system so I can add text-to-speech (TTS) announcements during combat.

I need you to research the FF8 PC battle system's memory structures in as much detail as possible. My primary sources should be: the Qhimm wiki (wiki.ffrtt.ru), the Doomtrain tool documentation, the FF8 Modding Wiki (hobbitdur.github.io/FF8ModdingWiki), myst6re's reverse engineering work, the FFNx source code on GitHub (julianxhokaxhiu/FFNx), and any IDA/Ghidra disassembly notes from the FF8 PC modding community. I'm targeting the **English Steam 2013 release** (not PSX, not Remastered).

---

### CRITICAL: SAVEMAP OFFSET CORRECTION

Many community resources calculate savemap offsets assuming the PC savemap header is 96 bytes (0x60). The **CONFIRMED** header size on the Steam 2013 build is **76 bytes (0x4C)**. This means all post-header offsets from community wikis/tools may be **0x14 (20 bytes) too high**. 

My confirmed reference points:
- **Savemap base address: `0x1CFDC5C`**
- GFs start at savemap + 0x4C (16 GFs × 0x44 bytes each)
- Characters start at savemap + 0x48C (8 characters × 0x98 bytes each)
- Gil at savemap + 0x08 (inside the 76-byte header)
- Party formation at savemap + 0xAF0 (4 bytes, 0xFF terminated)

When providing any savemap-relative offsets, please note whether they come from a source that uses the 96-byte or 76-byte header assumption, so I can apply the correction.

---

### WHAT I ALREADY KNOW FROM FFNx SOURCE (ff8_data.cpp, ff8.h, save_data.h)

These are confirmed addresses/pointers resolved by FFNx at runtime. I'm listing them so you can build on top of this foundation rather than repeat it:

**Game mode detection:**
- `FF8_MODE_BATTLE = 999`, `FF8_MODE_SWIRL = 3` (battle transition), `FF8_MODE_AFTER_BATTLE = 4`

**Battle entry/loop (resolved dynamically from main_loop):**
- `battle_enter` = `main_loop + 0x330` (absolute value)
- `battle_main_loop` = `main_loop + 0x340` (absolute value)

**Party computed stats at runtime:**
- **Base: `0x1CFF000`**, 3 party slots, stride **0x1D0** per slot
- Confirmed: `+0x172` = curHP (uint16), `+0x174` = maxHP (uint16)
- Related functions: `compute_char_stats_sub_495960`, `compute_char_max_hp_496310(int,int)`, `get_char_level_4961D0(int,int)`
- The struct type is `ff8_char_computed_stats` but FFNx never defines its fields — it's opaque

**Active character tracking:**
- `battle_current_active_character_id` (BYTE*) — resolved from `sub_4BB840 + 0x13`
- `battle_new_active_character_id` (BYTE*) — resolved from `sub_4BB840 + 0x37`

**Enemy/monster names:**
- `battle_get_monster_name_sub_495100` — monster name retrieval function
- `battle_char_struct_dword_1D27B10` (BYTE**) — pointer to battle character/entity struct array

**Battle entities (Scan system):**
- `battle_entities_1D27BCB` — entity array base, resolved from `scan_get_text_sub_B687C0 + 0x18`
- `scan_text_data` — resolved from `scan_get_text_sub_B687C0 + 0x27`
- `scan_text_positions` — resolved from `scan_get_text_sub_B687C0 + 0x20`

**Battle menu:**
- `battle_menu_loop_4A2690` — at `battle_main_loop + 0x216`
- `battle_menu_state` (void*) — resolved from `battle_pause_window_sub_4CD350 + 0x29`
- `battle_menu_sub_4A6660`, `battle_menu_sub_4A3D20`, `battle_menu_sub_4A3EE0`

**Battle result:**
- `battle_check_won_sub_486500` — win detection
- `battle_result_state_1CFF6E7` (BYTE): values 2=escaped, 4=won
- `battle_menu_add_exp_and_stat_bonus_496CB0(int, uint16_t)` — post-battle EXP/stat handler

**Character data for battle:**
- `character_data_1CFE74C` (byte*) — character data base

**Encounter info:**
- `global_battle_encounter_id_1CFF6E0` (WORD*)
- `battle_encounter_id` (WORD*)

**Draw system:**
- `battle_get_draw_magic_amount_48FD20(int, int, int)` — draw magic amount function

**Battle AI:**
- `battle_ai_opcode_sub_487DF0` — AI opcode processor

**Actor names:**
- `battle_get_actor_name_sub_47EAF0` — actor name for battle dialog
- `battle_current_actor_talking` (DWORD*)
- Related: `byte_1CFF1C3`, `unk_1CFDC70`, `unk_1CFDC7C`, `word_1CF75EC`, `unk_1CFF84C`, `unk_1CF3E48`, `dword_1CF3EE0`

**Battle effects:**
- `battle_magic_id` (int*) — current magic/effect being executed
- `func_off_battle_effects_C81774` (DWORD*) — function table for all 337+ battle effects
- Effect IDs from FFNx `effects.h`: Cure=0, Leviathan=5, Scan=39, Quezacotl=115, Shiva=184, Ifrit=200, etc.

**Battle pause:**
- `battle_pause_sub_4CD140`, `battle_pause_window_sub_4CD350`
- `is_alternative_pause_menu` (uint32_t*), `pause_menu_option_state` (uint32_t*)

---

### QUESTIONS — PLEASE ANSWER EACH IN DETAIL

#### 1. FULL `ff8_char_computed_stats` STRUCT LAYOUT (at 0x1CFF000, 3 × 0x1D0)

This is the runtime party stats struct. I confirmed curHP at +0x172 and maxHP at +0x174, and the struct is 0x1D0 (464) bytes per party slot. I need the COMPLETE field map.

Specifically:
- Where is the **character ID** (which of the 8 game characters this slot represents)?
- Where are the base stats: **STR, VIT, MAG, SPR, SPD, LCK, EVA, HIT**?
- Where is the **ATB gauge** value? What is its type (uint8, uint16, uint32, float)? What value means "full/ready"?
- Where are **status effect flags**? What format — bitmask? Which bits map to which statuses?
- Where is the **limit break gauge** / crisis level?
- Where is the **character level** at runtime?
- Where are the **currently equipped commands** (Attack slot, Magic slot, GF slot, Draw slot, Item slot, and the 3 junction command slots)?
- Where is the **dead/alive/KO flag**?
- Where are **current elemental affinities** (attack element, defense elements)?
- Where is the **GF compatibility** value (for boost mechanic)?
- Are there any **animation state** or **action state** flags (idle, attacking, casting, defending)?

#### 2. BATTLE ENTITY ARRAY — UNIFIED CHARACTER + ENEMY STRUCT

The pointer `battle_char_struct_dword_1D27B10` points to an array of battle entities. I need:

- What is the **total number of slots**? (I've seen references to 4 party + 4-6 enemy slots = 8-10 total, but need confirmation)
- What is the **stride** (bytes per entity)?
- What is the **base address** of this array in the Steam 2013 build?
- For each entity slot, where are these fields:
  - **HP** (current and max)
  - **Name** (pointer to name string or inline?)
  - **Level**
  - **Status flags** (same format as party stats?)
  - **Entity type** flag (party member vs enemy vs GF?)
  - **Is alive / is targetable** flag
  - **Elemental weaknesses/resistances** 
  - **STR, MAG, VIT, SPR, SPD** and other stats
  - **Draw list** (what magics this enemy has for Draw — see question 7)
- How do party slots map to the computed stats struct at 0x1CFF000? (Is slot 0 in the entity array the same as slot 0 at 0x1CFF000?)

#### 3. ATB (ACTIVE TIME BATTLE) SYSTEM

FF8 uses ATB where each character has a fill gauge. When full, they can act.

- Where is the **ATB gauge** stored for each party member? Is it inside the 0x1CFF000 struct, or in the entity array, or in a separate memory region?
- What **data type** is the gauge? (uint8 0-255? uint16 0-65535? float 0.0-1.0?)
- What **value** indicates the gauge is full / character is ready to act?
- Is there a separate **"is this character's turn" flag**, or is fullness purely determined by gauge >= threshold?
- How does **Haste/Slow** affect the gauge? Is there a speed multiplier stored somewhere?
- Where is the **battle speed setting** (from Config menu) reflected in the ATB calculation?
- When a character's ATB is full and their command menu opens, is there a state flag distinguishing "menu open / selecting command" from "executing command"?

#### 4. BATTLE COMMAND MENU STATE

When a character's turn arrives, the menu shows: Attack / Magic / GF / Draw / Item (plus any junctioned commands like Card, Devour, etc.).

- Where is the **current top-level command selection** stored? (Which of the 5+ commands is highlighted)
- Where is the **sub-menu state** stored? For example:
  - In the **Magic sub-menu**: which spell is the cursor on?
  - In the **Item sub-menu**: which item is selected?
  - In the **GF sub-menu**: which GF is selected?
  - In the **Draw sub-menu**: which drawable magic is highlighted?
- Where is the **target selection state**? (Which enemy/ally is targeted, single vs all)
- Is there a **menu depth** indicator (top-level vs sub-menu vs target select)?
- What memory address tells me **which party member's menu is currently open**?
- How are the **junctioned commands** reflected in the menu? (The 3 customizable command slots from Junction — what IDs map to which command, and where is the current visible command list stored?)

#### 5. ENEMY DATA IN BATTLE

- How many **enemy slots** exist at runtime? (Usually up to 4, but bosses may differ?)
- Are enemies stored in the **same entity array** as party members (question 2), or in a separate structure?
- For each enemy, where is:
  - **Enemy name** (text string — is it loaded from the encounter data file, or decoded at runtime?)
  - **Current HP / Max HP**
  - **Level** (enemies level-scale with party in FF8 — where is their computed level?)
  - **Elemental weaknesses** (for Scan display)
  - **Status vulnerabilities**
  - **Is this slot occupied** (for encounters with variable enemy counts)?
- How does the game determine **how many enemies** are in the current encounter? Is it a count byte, or do you check empty slots?
- Where is the **enemy AI script data** loaded? (Not the opcodes, but the data it references)

#### 6. BATTLE RESULT SCREEN

After winning a battle, the game displays EXP, AP, items, and gil earned.

- I know `battle_result_state_1CFF6E7` indicates win/escape. Where is the **detailed result data**?
- Where is **total EXP earned** stored?
- Where is **AP earned** stored (for GF ability learning)?
- Where are **items dropped** listed? (Item IDs + quantities)
- Where is **gil earned**?
- Is there a **"level up" flag** per character that triggers during the result screen?
- Where is the **result screen phase** tracked? (e.g., "showing EXP" → "showing AP" → "showing items" → "press X to continue")
- Are there addresses for the **individual EXP values per character** (some characters may be dead and receive less)?

#### 7. DRAW SYSTEM

FF8's unique Draw mechanic lets characters steal magic from enemies.

- When targeting an enemy for Draw, where is the **list of available magics** stored? (Each enemy typically has 1-4 drawable spells)
- What format is the draw list? (Array of magic IDs? With stock amounts?)
- Where is the **currently selected draw target** (which enemy)?
- Where is the **draw result** (how many spells were drawn, success/fail)?
- The function `battle_get_draw_magic_amount_48FD20(int, int, int)` exists — what are its three parameters? (My guess: character slot, enemy slot, magic index?)
- Is the draw list stored in the enemy entity struct, or in a separate encounter data table?
- Can I read the draw list **before** the player uses Draw (to proactively announce "Enemy has Fire, Blizzard, Cure available to draw")?

#### 8. STATUS EFFECTS

Both party members and enemies can have status effects in battle.

- Where are **active status effects** stored for party members? For enemies?
- What is the **bitmask format**? Please list bit positions for all statuses:
  - Death, Poison, Petrify, Darkness/Blind, Silence, Berserk, Zombie
  - Sleep, Haste, Slow, Stop, Regen, Protect, Shell, Reflect
  - Float, Doom, Confuse, Drain, Eject
  - Invincible, Aura, Curse, etc.
- Is it a single uint32 bitmask, or multiple bytes/words?
- Are **temporary battle statuses** (like Protect, Shell, Haste) stored differently from **persistent statuses** (like Poison, Zombie)?
- Where is the **status timer** for timed effects? (e.g., how long until Haste wears off)

#### 9. TARGETING SYSTEM

- When selecting a target (enemy or ally), where is the **current target cursor position** stored?
- How does the game distinguish **single target** vs **all targets** (e.g., casting Fire vs Firaga on all)?
- Where is the **targeting mode flag** (targeting enemies vs allies vs self)?
- Is there a **target index** that maps to the entity array index?

#### 10. GF SUMMON IN BATTLE

- When a GF is summoned, where is the **GF HP** displayed (GF replaces the character's HP bar)?
- Where is the **boost gauge** (the manual mash-X mechanic during GF animation)?
- Where is the **currently summoning GF ID** stored?
- Is there a flag indicating **"GF summon animation in progress"**?

#### 11. LIMIT BREAKS

- Where is the **crisis level** per character? (Determines if limit break is available)
- Where is the **limit break trigger flag** (the arrow icon that appears)?
- For Squall's Renzokuken: where is the **trigger bar timing data**?
- For Zell's Duel: where is the **time remaining** and **current combo input state**?
- For Selphie's Slot: where is the **currently displayed spell** and **Do Over count**?

#### 12. BATTLE DIALOG / TEXT

- During battle, text appears for various events (e.g., "Draw [Fire]", "Scan", enemy names). Where is this **battle text buffer**?
- Are battle strings loaded from a separate string table, or built dynamically?
- Where is the **"Obtained [Item]" / "Drew [Magic]" / "Card" result text** stored?

#### 13. ENCOUNTER DATA FORMAT

- The encounter ID is at `0x1CFF6E0` (WORD). How does this map to enemy composition?
- Where is the **encounter data table** loaded in memory? (Lists which enemies, positions, flags)
- Is there a way to read the encounter data to know enemy names/types before the battle fully initializes?

---

### OUTPUT FORMAT

Please provide answers with:
1. **Specific byte offsets** relative to struct bases where possible
2. **Data types** (uint8, uint16, uint32, int16, float, etc.)
3. **Address ranges** for the Steam 2013 PC build where known
4. **Source citations** — which wiki page, tool, or research documented this
5. **Confidence level** — mark anything uncertain as "unconfirmed" vs "confirmed by [source]"

If exact addresses aren't known, provide the best approximation with reasoning (e.g., "based on PSX offset translation" or "from Doomtrain's kernel.bin section mapping").

Thank you for being as thorough as possible — this research directly enables a blind player to experience FF8's battle system through audio.
