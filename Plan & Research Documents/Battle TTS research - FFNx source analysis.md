# FF8 Battle Mode — Research & Known Addresses
## Compiled from FFNx source (ff8_data.cpp, ff8.h, save_data.h, battle/*.h)
## Date: 2026-03-24

---

## Game Mode Detection

- Battle mode = `FF8_MODE_BATTLE = 999` (game mode value)
- Swirl (transition into battle) = `FF8_MODE_SWIRL = 3`
- After battle = `FF8_MODE_AFTER_BATTLE = 4`
- Game mode pointer: `common_externals._mode` resolved from `main_loop + 0x115`

---

## Battle Entry & Main Loop (dynamically resolved)

From `ff8_data.cpp` (non-JP path):
- `battle_enter` = `get_absolute_value(main_loop, 0x330)` — battle initialization
- `battle_main_loop` = `get_absolute_value(main_loop, 0x340)` — per-frame battle update
- `sm_battle_sound` = `get_relative_call(main_loop, 0x487)` — battle sound/music handler
- `swirl_enter` = `get_absolute_value(main_loop, 0x493)`
- `swirl_main_loop` = `get_absolute_value(main_loop, 0x4A3)`
- `battle_trigger_field` = `sub_47CA90 + 0x15` — field-initiated battle trigger
- `battle_trigger_worldmap` = `worldmap_with_fog_sub_53FAC0 + 0x4EA` (US) / `+ 0x4E6` (non-US)

---

## Character Computed Stats (Runtime Battle Struct)

**Base address: `0x1CFF000`** (3 party slots × 0x1D0 stride)

From `ff8_data.cpp`:
```
char_comp_stats_1CFF000 = std::span<ff8_char_computed_stats>(get_absolute_value(..., 0x2A), 3)
```

Known offsets within ff8_char_computed_stats (size 0x1D0):
- `+0x172` = curHP (uint16_t) — confirmed from mod's existing menu TTS
- `+0x174` = maxHP (uint16_t) — confirmed from mod's existing menu TTS

NOTE: The struct definition for `ff8_char_computed_stats` was NOT found in FFNx source headers — it's likely defined in common_imports.h or used opaquely. Full field layout needs deep research.

Related functions:
- `compute_char_stats_sub_495960` — recalculates party stats
- `compute_char_max_hp_496310(int, int)` — compute max HP
- `get_char_level_4961D0(int, int)` — compute character level
- `sub_4954B0(int)` — called during stat computation

---

## Active Character / Turn Tracking

- `battle_current_active_character_id` (BYTE*) — resolved from `sub_4BB840 + 0x13`
- `battle_new_active_character_id` (BYTE*) — resolved from `sub_4BB840 + 0x37`

These likely track whose ATB bar is full / who is currently acting.

---

## Enemy / Monster Data

- `battle_get_monster_name_sub_495100` — function that retrieves monster name
- `battle_char_struct_dword_1D27B10` (BYTE**) — pointer to battle character/enemy struct array, resolved from `battle_get_monster_name_sub_495100 + 0xF`
- `battle_entities_1D27BCB` — resolved from `scan_get_text_sub_B687C0 + 0x18` — entity array used by Scan
- `character_data_1CFE74C` (byte*) — character data base for battle, resolved from `battle_menu_add_exp_and_stat_bonus_496CB0 + 0xD`

---

## Battle Menu System

- `battle_menu_loop_4A2690` — main battle menu loop, at `battle_main_loop + 0x216`
- `battle_menu_sub_4A6660` — called from menu loop at `+ 0xAF`
- `battle_menu_sub_4A3D20` — sub-handler
- `battle_menu_sub_4A3EE0` — further sub-handler
- `battle_menu_state` (void*) — resolved from `battle_pause_window_sub_4CD350 + 0x29`

---

## Battle Result / Win Detection

- `battle_check_won_sub_486500` — checks if battle is won
- `battle_result_state_1CFF6E7` (byte*) — battle result state:
  - 2 = escaped
  - 4 = won
  - 1-3 = other types
  - 5 = unknown
- `battle_sub_494D40()` — called after win detected
- `battle_menu_add_exp_and_stat_bonus_496CB0(int, uint16_t)` — adds EXP and stat bonuses post-battle

---

## Battle Encounter Info

- `global_battle_encounter_id_1CFF6E0` (WORD*) — set by `opcode_battle + 0x50 - 2`
- `battle_encounter_id` (WORD*) — set by `opcode_battle + 0x66`

---

## Actor Names in Battle

- `battle_get_actor_name_sub_47EAF0` — retrieves actor name for battle dialog
- `battle_current_actor_talking` (DWORD*) — resolved from `sub_485610 + 0x36E`
- Related data pointers:
  - `byte_1CFF1C3` — actor-related byte
  - `unk_1CFDC70`, `unk_1CFDC7C` — character name strings (near savemap base)
  - `word_1CF75EC` — word array
  - `unk_1CFF84C` — string near computed stats
  - `unk_1CF3E48` — string
  - `dword_1CF3EE0` — dword

---

## Draw System

- `battle_get_draw_magic_amount_48FD20(int, int, int)` — gets amount of magic available to draw
- `battle_sub_48D200` — related to draw mechanics

---

## Scan System

- `scan_get_text_sub_B687C0` — retrieves scan text for an enemy
- `scan_text_data` — resolved from `scan_get_text_sub_B687C0 + 0x27`
- `scan_text_positions` — resolved from `scan_get_text_sub_B687C0 + 0x20`
- `battle_entities_1D27BCB` — entity list used by scan

---

## Battle AI

- `battle_ai_opcode_sub_487DF0` — battle AI opcode processor
- `update_tutorial_info_4AD170(int)` — tutorial tracking during battle

---

## Battle Pause

- `battle_pause_sub_4CD140` — pause menu handler
- `battle_pause_window_sub_4CD350` — pause window rendering
- `is_alternative_pause_menu` (uint32_t*) — alternative pause flag
- `pause_menu_option_state` (uint32_t*) — which pause option is selected

---

## Battle Files & Textures

- `battle_filenames` (char**) — array of battle file paths
- `battle_open_file` — opens battle data files
- `battle_load_textures_sub_500900` — loads battle textures
- `battle_upload_texture_to_vram` — uploads textures to VRAM

---

## Battle Effects & Magic

- `battle_magic_id` (int*) — current magic/effect being executed
- `func_off_battle_effects_C81774` (DWORD*) — function table for all battle effects
- `func_off_battle_effect_textures_50AF93` (DWORD*) — texture loading for effects
- `battle_read_effect_sub_50AF20` — reads effect data
- Effect enum in `effects.h` maps spell IDs to names (Cure=0, Shiva=184, etc.)

---

## Savemap Battle Data

From `save_data.h`, `savemap_ff8_battle` struct:
- `victory_count` (uint32)
- `battle_escaped` (uint16)
- `magic_drawn_once[8]` — bitmap of which magics have been drawn
- `ennemy_scanned_once[20]` — bitmap of which enemies have been scanned
- `renzokuken_auto`, `renzokuken_indicator`
- `special_flags` — dream|Odin|Phoenix|Gilgamesh|Angelo|Angel Wing

---

## Vibration Data

- `vibrate_data_battle` (uint8_t**) — battle vibration data
- `vibrate_data_summon_quezacotl` (uint8_t**) — per-GF vibration data

---

## Key Static Addresses (compiled)

| Address | Type | Description |
|---------|------|-------------|
| 0x1CFF000 | ff8_char_computed_stats[3] | Party computed stats (3 slots × 0x1D0) |
| 0x1CFF6E0 | WORD | Global battle encounter ID |
| 0x1CFF6E7 | BYTE | Battle result state (2=escaped, 4=won) |
| 0x1CFF1C3 | BYTE | Actor-related byte |
| 0x1CFF84C | char* | String near computed stats |
| 0x1CFE74C | byte* | Character data base for battle |
| 0x1D27B10 | BYTE** | Battle char struct pointer |
| 0x1D27BCB | -- | Battle entities (scan system) |

---

## WHAT WE STILL NEED (Deep Research Required)

1. **ff8_char_computed_stats full struct layout** — we only know curHP (+0x172) and maxHP (+0x174). Need: ATB gauge, status effects, active/dead flags, character ID within the struct, all stats.

2. **Enemy data struct** — `battle_char_struct_dword_1D27B10` points to something but layout is unknown. Need: enemy HP, max HP, name pointer, level, weaknesses, status, how many enemies, enemy slot count.

3. **ATB system** — `battle_current_active_character_id` and `battle_new_active_character_id` exist but we need: ATB gauge value/address per character, how to detect "ready to act" vs "waiting", timer mechanics.

4. **Command menu state** — `battle_menu_state` pointer exists but layout unknown. Need: current selected command (Attack/Magic/GF/Draw/Item), sub-menu cursor position, magic list, draw list, item list.

5. **Battle result screen data** — we know `battle_menu_add_exp_and_stat_bonus_496CB0` exists. Need: EXP gained, AP gained, items dropped, GF AP distributed, level up detection.

6. **Draw system details** — `battle_get_draw_magic_amount_48FD20` exists. Need: which magics are available from current target, stock amounts, draw success/fail.

7. **Status effects** — both for party and enemies. Poison, Sleep, Silence, etc. Where stored, bitmask format.

---

## SAVEMAP OFFSET CORRECTION NOTE

ChatGPT deep research assumes savemap header is 96 bytes (0x60). CONFIRMED header is 76 bytes (0x4C). All post-header offsets from deep research are 0x14 (20 bytes) too high. When using research offsets: subtract 0x14. Confirmed base: 0x1CFDC5C. GFs at +0x4C, chars at +0x48C, Gil at +0x08 (header).
