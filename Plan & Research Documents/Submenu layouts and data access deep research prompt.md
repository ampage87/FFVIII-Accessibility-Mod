# Deep Research Request: FF8 Steam (2013) Submenu Layouts, Cursor Tracking, and Data Access for Accessibility TTS

## Date: 2026-03-20
## Context: FF8 Steam 2013 Accessibility Mod — Making Every In-Game Menu Screen Accessible via TTS

---

## Overview

I'm building a Win32 DLL accessibility mod for Final Fantasy VIII Steam 2013 (FF8_EN.exe, App ID 39150, no ASLR, base 0x00400000) running with FFNx v1.23.x. The mod makes the game playable for blind/low-vision players via screen reader TTS output.

**What's already working:**
- Top-level menu cursor tracking (byte at pMenuStateA + 0x1E6, values 0–10 for Junction through Save)
- Gil, play time, location, SeeD rank read from live savemap at 0x1CFDC5C
- Character stats (HP, EXP, model ID, name) from savemap character structs
- Help text visible in rendered text buffer (GCW capture of `get_character_width` calls)
- Save/load slot and block cursor tracking

**What I need now:** A comprehensive understanding of every submenu's visual layout, navigation structure, and data sources — so I can make each one fully accessible. For each submenu, I need to know what's on screen, what the cursor options are, and where I can read the currently selected value from memory or from the text rendering pipeline.

---

## The 11 Submenus to Cover

The top-level menu items are (in order, indices 0–10):
0. **Junction**
1. **Item**
2. **Magic**
3. **Status**
4. **GF**
5. **Ability**
6. **Switch**
7. **Card**
8. **Config**
9. **Tutorial**
10. **Save**

For EACH of these, I need the information described in the "Per-Submenu Questions" section below.

---

## Per-Submenu Questions (Answer for Each of the 11)

### A. Visual Layout
- What does this submenu screen look like? Describe every panel, list, and data field visible on screen.
- Please find and reference actual screenshots or gameplay images showing this submenu. Describe the layout in terms a blind user would need: what information is displayed, in what order, and how is it organized (lists, grids, columns, etc.).
- Are there multiple "phases" or "pages" within this submenu? (e.g., Junction has character select → junction slot select → magic assignment)

### B. Navigation Flow
- What is the cursor navigation path? (e.g., "first select a character, then select a junction slot, then select a magic to assign")
- At each navigation level, what are the cursor options? Is it a vertical list, horizontal row, or grid?
- Which buttons advance deeper into the submenu (X/confirm) and which go back (Circle/cancel)?
- Are there any special navigation modes (e.g., pressing Triangle in Magic to sort, pressing Square in Items to use)?

### C. Data Displayed
- For each piece of information shown on screen, what is the data source?
  - **Text labels** (e.g., "HP-J", "Str", "Fire"): where are these strings stored? Are they from kernel.bin string tables, the menu asset files (mngrphd.bin), or hardcoded?
  - **Numeric values** (e.g., HP 486/486, EXP 6500, Gil 5000): where are these stored in the live game memory? Are they in the savemap (base 0x1CFDC5C), or in separate game state variables?
  - **Character names**: are they always from the savemap header names (with +0x20 encoding)?
  - **Item names, spell names, GF names, ability names**: are these from kernel.bin string sections? Which section IDs?
  - **Status effect names, elemental names**: same question.
- Which values change dynamically (e.g., HP after junctioning) vs. which are static labels?

### D. Cursor Position Tracking
- Where in memory is the cursor position stored for this submenu?
- If there are multiple cursor levels (e.g., character select then slot select), where is each level's cursor stored?
- Is the cursor position in the `pMenuStateA` region (base 0x01D76A9A), in the `ff8_win_obj` window array, or somewhere else?
- Does the `menu_callbacks` array (base 0x00B87ED8, 8 bytes per entry) have a dedicated entry for this submenu? If so, what index?

### E. Accessibility Strategy
- For this submenu, should I primarily:
  - **Read from memory** (for numeric values, cursor indices, stat changes)?
  - **Scrape rendered text** from the GCW buffer (for labels, names, help text)?
  - **Hook specific functions** (for complex interactions like junction stat preview)?
- What's the minimum information a blind user needs to navigate this submenu effectively?

---

## Detailed Submenu Breakdown

### 0. Junction
This is the most complex submenu. It involves:
- Character selection (which of the 3 party members to junction)
- Junction slot selection (HP-J, Str-J, Vit-J, Mag-J, Spr-J, Spd-J, Eva-J, Hit-J, Luck-J, Elem-Atk-J, Elem-Def-J, ST-Atk-J, ST-Def-J)
- For each slot: selecting which magic (from the character's inventory of up to 32 spells) to assign
- Stat preview: when hovering over a magic assignment, the game shows what the stat WOULD become
- Auto-junction option (lets the game auto-assign)
- Remove all option

**Key questions:**
- How many screens/phases does Junction have?
- Where is the character selection index stored?
- Where is the junction slot cursor stored?
- Where is the magic selection cursor stored within the junction assignment list?
- Where are the stat preview values (the "before → after" numbers) stored or rendered?
- How does the game compute junction stat bonuses? Is there a function I can call or memory I can read for the preview?

### 1. Item
- Scrollable list of items in inventory
- Each entry shows: item name, quantity
- Can use items, rearrange, or sort
- Items can be used on party members (secondary selection)

**Key questions:**
- Where is the inventory stored in memory? (savemap_ff8_items.items[198] in save_data.h — where in the live savemap?)
- Where is the item list scroll position and cursor position stored?
- Item names: which kernel.bin string section?
- When using an item on a party member, where is the target character cursor?

### 2. Magic
- First: character selection
- Then: list of spells the selected character has stocked
- Each entry shows: spell name, quantity (out of 100)
- Can exchange magic between characters

**Key questions:**
- Character cursor + spell list cursor positions in memory?
- Spell names: which kernel.bin string section?
- Spell stock quantities: in the savemap_ff8_character.magics[32] array?

### 3. Status
- Character selection, then detailed stat display
- Shows: Level, HP, EXP, Next Level, base stats (Str/Vit/Mag/Spr/Spd/Luck/Eva/Hit), elemental resistances, status resistances, equipped weapon, junctioned GFs, learned abilities
- Multiple pages of information

**Key questions:**
- How many pages does Status have?
- Where is the page indicator or page cursor?
- Are all displayed stats computed values? Are they in the battle-computed stats buffer (character_data_1CFE74C from FFNx) or calculated on the fly?

### 4. GF (Guardian Force)
- Lists all obtained GFs
- For each GF: name, level, HP, EXP, abilities (learned and learning), compatibility with each character
- Can select abilities to learn

**Key questions:**
- GF list cursor position in memory?
- GF data in savemap: savemap_ff8_gf structs at savemap+0x4C (16 GFs × 68 bytes)?
- GF names, ability names: which kernel.bin string sections?
- How is the "currently learning" ability indicated?

### 5. Ability
- Lists abilities learned by junctioned GFs
- Shows: party abilities, character abilities, junction abilities, GF abilities, menu abilities
- This is greyed out until a GF is junctioned

**Key questions:**
- How is the ability list structured? Is it filtered by ability type?
- Ability names: which kernel.bin string section?
- Is there a "use" option for menu abilities (like Move-Find)?

### 6. Switch
- Rearrange party member order
- Shows all available characters (not just current party of 3)
- Select a character, then select where to place them

**Key questions:**
- Where are the available characters listed? All 8 slots, or only unlocked characters?
- What's the cursor flow — select source character, then destination slot?
- Where is the current party order stored in memory? (we found a candidate at savemap+0xAF1)
- What about characters not in the party (benched) — where is the full roster list?

### 7. Card
- Triple Triad card collection viewer
- Shows all cards the player owns
- Card names, card levels (1-10), quantities
- May show card details (stats: top/bottom/left/right numbers, element)

**Key questions:**
- Card list cursor position?
- Card data: where in the savemap?
- Card names: which string table? (FFNx has `get_card_name` and `card_name_positions`)
- Card stats (the 4 directional numbers): where are they stored?

### 8. Config
- Game settings: sound, controller, etc.
- Options include: Sound (Stereo/Mono), Controller config, Cursor (Initial/Memory), ATB (Active/Wait), Camera movement, Scan, and more
- Each option has a current value

**Key questions:**
- FFNx has `menu_config_controller`, `menu_config_render`, `menu_config_input_desc` — how do these work?
- Where is each config option's current value stored?
- Is the config screen a simple list with Left/Right to change values?

### 9. Tutorial
- Shows various tutorial/information screens
- Includes information about gameplay mechanics, GFs, etc.
- Also provides access to SeeD test (written exam for rank promotion)
- May show Weapons Monthly, Pet Pals, and other acquired information

**Key questions:**
- What tutorial categories exist?
- Is tutorial text from a specific data file?
- How does the SeeD test work (multiple choice questions)?
- Where are test questions and answers stored?

### 10. Save
- We already have this working (slot/block selection + cached header TTS)
- Save is only available at save points

---

## Data Source Questions (Cross-Cutting)

### kernel.bin String Sections
FF8's kernel.bin contains multiple string sections. For each submenu's text data, I need to know:
- Which section ID contains item names?
- Which section ID contains magic/spell names?
- Which section ID contains GF names?
- Which section ID contains ability names?
- Which section ID contains status effect names?
- Which section ID contains elemental type names?
- Which section ID contains weapon names?
- Which section ID contains junction slot names (HP-J, Str-J, etc.)?
- Which section ID contains menu help text strings?
- Are any strings NOT in kernel.bin but instead in the menu binary files (mngrphd.bin, etc.)?

Please provide the section numbers/IDs and describe how the indexing works (e.g., "magic names are in kernel section 6, indexed by magic ID 0-56").

### savemap Structure After Characters
The live savemap at 0x1CFDC5C contains (confirmed):
- Header: 0x4C bytes
- GFs: 16 × 68 bytes at +0x4C
- Characters: 8 × 152 bytes at +0x48C

After characters (at savemap+0x94C), the savemap continues with:
- Shops (savemap_ff8_shop × 20)
- Limit breaks (savemap_ff8_limit_break)
- Items (savemap_ff8_items — battle_order[32] + items[198])
- Battle data (savemap_ff8_battle)
- Field header (savemap_ff8_field_h — contains SeeD EXP, steps, victory count, etc.)
- Field data (savemap_ff8_field — contains game_moment, Triple Triad rules, card queen location, etc.)
- World map data (savemap_ff8_worldmap)

**I need the exact byte offsets** from the savemap base for each of these sections. The save_data.h struct definitions give sizes, but I need them computed as cumulative offsets.

### Menu Callback Array Mapping
The `menu_callbacks` array at 0x00B87ED8 has entries of 8 bytes each. Known indices:
- 2: Items submenu
- 7: Cards submenu
- 8: Config submenu
- 11: Shop (not in-game menu)
- 12: Junk Shop
- 16: Top-level menu
- 23: Unknown
- 27: Chocobo World

**I need the callback indices for ALL submenus** (Junction, Magic, Status, GF, Ability, Switch, Tutorial, Save).

---

## Text Rendering Pipeline

We've confirmed that the `get_character_width` function is called for every character drawn on the menu screen. By hooking this function and buffering the character codes, we can capture all rendered text.

**Observed GCW buffer pattern on main menu:**
```
"JunctionItemMagicStatusGFAbilitySwitchCardConfigTutorialSave[HelpText][CharacterName]B-Garden- Hall"
```

The help text changes per cursor position (e.g., "Junction Menu", "Item Menu", "Rearrange party order").

**For submenus, I need to know:**
- Does each submenu also render through `get_character_width` / `menu_draw_text`?
- Would the GCW buffer show submenu text (e.g., spell names, item names, stat labels)?
- Or do submenus use a different text rendering path?
- For numeric values specifically (HP, quantities, stats), are these rendered as text through the same pipeline, or drawn as sprite digits?

---

## Technical Environment

- **Executable**: FF8_EN.exe (Steam 2013, 32-bit x86, no ASLR, base 0x00400000)
- **Mod layer**: FFNx v1.23.x (replaces certain game functions; my hooks chain through FFNx)
- **Hook method**: MinHook (inline function hooking) + `get_character_width` buffer capture
- **Known addresses**:
  - pGameMode: 0x01CD8FC6 (WORD, 6 = menu mode)
  - pMenuStateA: 0x01D76A9A (menu controller state region)
  - Menu cursor: pMenuStateA + 0x1E6 (BYTE, 0-10)
  - menu_callbacks: 0x00B87ED8 (array of 8-byte callback entries)
  - menu_draw_text: 0x004BDE30
  - get_character_width: 0x004A0CD0
  - get_text_data: resolved from main_menu_render + 0x203
  - Savemap base: 0x1CFDC5C (header + GFs + characters + items + field data)
  - ff8_win_obj windows: 0x01D2B330 (array of 8 windows, 0x3C bytes each)

---

## Accessibility Design Principles

For a blind player navigating these menus:

1. **Announce the current cursor position** whenever it changes (item name, spell name, character name, etc.)
2. **Announce relevant values** alongside names (e.g., "Fire, 87 stocked" not just "Fire")
3. **Announce stat changes** when previewing junction assignments (e.g., "Strength would change from 12 to 45")
4. **Provide context** on submenu entry (e.g., "Junction — select a character" when first entering)
5. **Announce disabled/greyed options** with explanation (e.g., "Ability — requires junctioned GF")
6. **Support hotkeys** for quick info access (we already have G=Gil, T=Time, L=Location, R=SeeD Rank on the main menu)

For each submenu, advise on the minimum information needed to make it usable, and whether the information can be obtained through:
- **Memory reads** (preferred for numeric/structured data)
- **GCW text scraping** (preferred for labels and help text)
- **Function hooks** (for computed/preview values)

---

## Deliverable

For each of the 11 submenus, provide:
1. **Visual layout description** with reference to screenshots/images
2. **Navigation flow** (cursor path through screens)
3. **Data fields** with memory offsets or string table references
4. **Cursor tracking** — where the cursor position is stored for each navigation level
5. **Recommended accessibility approach** (memory read vs text scrape vs hook)
6. **kernel.bin string section mapping** for any text data used

Also provide:
- Complete cumulative byte offsets for all savemap sections
- Complete menu_callbacks index mapping
- kernel.bin section directory (all string section IDs and their contents)

---

## Sources to Consult

- **FFNx source code** (github.com/julianxhokaxhiu/FFNx): src/ff8.h, src/ff8_data.cpp, src/ff8/save_data.h
- **Qhimm Wiki** (wiki.ffrtt.ru): FF8 kernel.bin documentation, menu system docs
- **FF8 Modding Wiki** (hobbitdur.github.io/FF8ModdingWiki): save format, kernel.bin sections
- **OpenVIII** (github.com/MaKiPL/OpenVIII): C# reimplementation with menu state machines
- **Deling** / **Ifrit** (FF8 editors): kernel.bin parsing, string tables
- **Final Fantasy Wiki** (finalfantasy.fandom.com): screenshots and gameplay descriptions of each menu
- **Qhimm Forums**: FF8 engine reverse engineering threads
- **GameFAQs FF8 guides**: detailed menu descriptions and gameplay mechanics
- **YouTube gameplay videos**: visual reference for each submenu's layout

Please include actual screenshots or detailed descriptions of each submenu's visual layout where possible. The visual layout descriptions are critical because I (the mod developer) can see, but I'm designing for a user who cannot — I need to understand exactly what's on each screen to decide what to announce.
