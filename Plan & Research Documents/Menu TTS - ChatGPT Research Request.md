# Deep Research Request: FF8 Steam (2013) Main Menu System — Addresses, Structures, and Hook Points

## Context

I'm building an accessibility mod (Win32 DLL, `dinput8.dll` proxy) for Final Fantasy VIII Steam 2013 edition (FF8_EN.exe, App ID 39150) running with FFNx v1.23.x. The mod makes the game playable for blind/low-vision players via TTS (screen reader output). I already have working TTS for:

- Title screen cursor navigation
- FMV audio descriptions and skip
- Field dialog (all MES/ASK/AMES/AASK/AMESW/RAMESW opcodes + show_dialog hook)
- Field navigation with entity catalog and auto-drive

**The next feature is TTS for the in-game main menu** — the menu opened with the Triangle/Menu button during field gameplay. This is game mode `FF8_MODE_MENU` (value 6) in FFNx's `ff8_game_modes` enum. This is NOT the title screen main menu (`FF8_MODE_MAIN_MENU`, value 200) and NOT the battle menu (`FF8_MODE_BATTLE`, value 999).

## What I Need Researched

### 1. Main Menu Architecture

The FF8 in-game menu has these top-level items (from memory — please verify the exact list and order):
- Junction
- Item
- Magic
- Status
- GF
- Switch (party member swap)
- Card
- Config
- Tutorial
- Save
- (possibly others?)

**Questions:**
- What is the exact ordered list of top-level menu items?
- How does the menu cursor/selection work? Is there a global "current menu item index" variable?
- What memory address(es) track which top-level menu item is currently highlighted?
- What memory address(es) track which submenu is currently open (e.g., inside Items list, inside Magic list)?
- Are menu items rendered via the same `ff8_win_obj` window system used for field dialog, or does the menu have its own rendering/text system?

### 2. Menu State Machine & Callback System

FFNx's `ff8_externals` struct (from `ff8.h`) contains these menu-related fields:

```cpp
ff8_menu_callback *menu_callbacks;           // Array of menu callback structs
uint32_t menu_config_controller;             // Config submenu controller function
uint32_t menu_config_render;                 // Config submenu render function
uint32_t menu_config_render_submenu;         // Config submenu render (inner)
ff8_menu_config_input *menu_config_input_desc;
ff8_menu_config_input_keymap *menu_config_input_desc_keymap;
uint32_t main_menu_render_sub_4E5550;        // Main menu render function
uint32_t main_menu_controller;               // Main menu controller function
uint32_t menu_sub_4D4D30;                    // Unknown menu sub
uint32_t menu_chocobo_world_controller;      // Chocobo World submenu
uint32_t create_save_file_sub_4C6E50;        // Save file creation
uint32_t menu_draw_text;                     // Menu text draw function
uint32_t (*get_character_width)(uint32_t);   // Character width for menu text
uint32_t get_text_data;                      // Menu text data retrieval
uint32_t *menu_data_1D76A9C;                 // Menu data pointer
```

**Questions:**
- What is the `menu_callbacks` array? How many entries does it have, and what does each entry correspond to (one per top-level menu item)?
- What is the `main_menu_controller` function? Does it handle input for the top-level menu selection? What is its signature and how does it update the cursor position?
- What is `main_menu_render_sub_4E5550`? Does it draw the top-level menu items? Can I hook it to detect which item is being drawn/highlighted?
- What is `menu_draw_text`? Is this the function that renders individual text strings in the menu? What are its parameters (text pointer, x, y, color/highlight flag)?
- What is `get_text_data`? Does it return pointers to the localized menu string table?
- What is `menu_data_1D76A9C`? Does it contain the current menu state (which submenu is active, cursor position, etc.)?
- Is there a menu state variable that indicates: top-level menu open, which submenu is active (Items, Magic, Junction, etc.), cursor row/column within a submenu?

### 3. Menu Text System

FF8's menu text uses a character encoding similar to field dialog but may use a different string table.

**Questions:**
- Where are the menu item strings stored in memory? Are they in the `kernel.bin` string section, or hardcoded in the executable?
- Does `menu_draw_text` use the same FF8 character encoding as field dialog (the encoding I already decode in `ff8_text_decode.cpp`)?
- Are menu strings accessed via `get_text_data` with a section ID + string index, similar to how `field_get_dialog_string` works for field dialog?
- What is the text data format? Is it the same offset-table + packed-strings format used in kernel.bin?

### 4. Save Menu Specifics

When the player selects "Save" from the top-level menu (only available at save points), a save slot selection screen appears.

**Questions:**
- What memory address tracks which save slot (1-30) is currently highlighted?
- How does the game determine if a save slot is empty or occupied?
- For occupied slots, what data is displayed (character names, level, play time, location, Gil)?
- FFNx has `savemap_ff8` and `savemap_ff8_header` structs in `ff8/save_data.h` — are these the structures used to populate the save slot display? Key fields I see:
  - `header.squall_name[12]` — Squall's name
  - `header.char1_lvl` — Level
  - `header.played_time_secs` — Play time
  - `header.gil` — Gil amount
  - `header.location_id` — Location ID (how is this mapped to a location name string?)
  - `header.char1_portrait` / `char2_portrait` / `char3_portrait` — Party portraits
- What is `create_save_file_sub_4C6E50`? Is this the function that writes save data? Can I hook it to detect save completion?
- Is there a save confirmation dialog (Yes/No overwrite)?

### 5. Submenu Cursor Tracking

Each submenu (Items, Magic, GF, Status, Junction, Config) has its own cursor/selection state.

**Questions:**
- **Items submenu**: Where is the currently highlighted item index stored? Is it a scrolling list with a viewport offset + cursor offset?
- **Magic submenu**: First you select a character, then a spell list. Where are the character selection index and spell selection index stored?
- **GF submenu**: Similar to Magic — character then GF list. Where are these indices?
- **Status submenu**: Character selection then stat display. Where is the character index?
- **Junction submenu**: This is the most complex — character → junction category (HP-J, Str-J, etc.) → spell selection. Where are each of these cursor positions?
- **Config submenu**: FFNx has `menu_config_input_desc` and `menu_config_controller`. Does the config screen use a simple list with highlight index?
- **Switch submenu**: Party member arrangement. Where is the swap state tracked?
- **Card submenu**: Card collection viewer. Cursor position?

### 6. Hook Strategy Recommendations

Given that my mod already hooks game functions via MinHook (through FFNx's replacements), what would be the most effective hook points?

**Possible approaches:**
1. **Hook `main_menu_controller`** — intercept input processing, read cursor state before/after
2. **Hook `menu_draw_text`** — intercept every text string being drawn, detect highlighted items by color/flag parameter
3. **Hook individual submenu controllers** — each submenu may have its own controller function
4. **Poll menu state variables** — read cursor position from known memory addresses each frame

**Questions:**
- Which approach is most reliable for detecting the currently highlighted menu item?
- Are there any "cursor changed" callbacks or events, or must I poll?
- Does the menu system use the `ff8_win_obj` window array (same as field dialog), or is it a completely separate system?
- The `pause_menu` and `pause_menu_with_vibration` functions in FFNx externals — are these related to the in-game menu, or only to the battle pause menu?

### 7. Game Mode Transitions

**Questions:**
- When the player presses Triangle to open the menu, does `pGameMode` change from `FF8_MODE_FIELD` (1) to `FF8_MODE_MENU` (6)?
- When closing the menu, does it return to `FF8_MODE_FIELD`?
- Is the transition immediate or does it go through an intermediate state?
- Is there an address that indicates "menu is opening" vs "menu is fully open" (animation state)?

### 8. Relevant Qhimm Wiki / Community Documentation

**Questions:**
- Is the FF8 menu system documented on the Qhimm wiki (wiki.ffrtt.ru)?
- Are there any known reverse-engineering efforts for the menu state machine (e.g., in Deling, Hyne save editor, or other FF8 modding tools)?
- The `kernel.bin` file contains menu-related string sections — which sections contain the top-level menu item names, and which contain submenu text (item names, spell names, GF names, ability names, status labels)?

## Technical Environment

- **Executable**: `FF8_EN.exe` (Steam 2013, no ASLR, fixed base 0x00400000)
- **Mod layer**: FFNx v1.23.x (replaces certain game functions; my hooks chain through FFNx)
- **Hook method**: MinHook (inline function hooking)
- **My mod already resolves**: `pGameMode` (via FFNx pattern), `pWindowsArray` (`ff8_win_obj` array), `show_dialog`, `menu_draw_text`, `get_character_width`, `get_text_data`, and the full field entity/dialog/navigation system
- **Key constraint**: I cannot modify FFNx source — I hook through it. FFNx's function replacements are my "original" functions for MinHook trampolines.

## Ideal Output Format

For each menu-related address or function, please provide:
1. The address in FF8_EN.exe (or the FFNx external name if resolved through FFNx)
2. The function signature (parameters, return type, calling convention)
3. What the function does and when it's called
4. What memory it reads/writes that indicates menu state

For memory addresses, provide:
1. The absolute address (or offset from a known base)
2. The data type and size
3. What values it takes and what each value means
4. When it's updated (on input, on render, on menu open/close)

## Sources to Consult

- FFNx source code (GitHub: julianxhokaxhiu/FFNx, especially `src/ff8.h`, `src/ff8_data.cpp`, `src/ff8_data.h`, `src/ff8/save_data.h`, `src/gamehacks.cpp`)
- Qhimm wiki (wiki.ffrtt.ru) — FF8 kernel.bin documentation, menu system docs
- Deling (FF8 field/script editor) source code
- Hyne (FF8 save editor) source code — for save data structure verification
- FF8 modding community resources (Qhimm forums)
- IDA/Ghidra disassembly notes if available from community reverse engineering
- The `hobbitdur.github.io/FF8ModdingWiki` site referenced in FFNx's save_data.h

## Priority Order

1. **Top-level menu cursor position** — the single most important thing to identify first
2. **Game mode detection** (field → menu transition confirmation)
3. **Menu text rendering hook point** (how to intercept what's being drawn)
4. **Save slot selection state**
5. **Submenu cursor positions** (Items, Magic, Junction, etc.)
6. **String table locations** (where menu item names live in memory)

Findings:

# FF8 (2013 Steam) In-Game Menu Overview

The in‐game menu (opened with the Triangle/Menu button during field play) is a special game mode (`FF8_MODE_MENU` = 6【108†L2328-L2331】). Its top‐level entries, in order, are exactly:

- **Junction**  
- **Item**  
- **Magic**  
- **Status**  
- **GF**  
- **Ability**  
- **Switch** (Junction Exchange)  
- **Card**  
- **Config**  
- **Tutorial**  
- **Save**  
- **Exit**  
- **Game Launcher**  

This list of 13 items (with *Exit* and *Game Launcher* last) is confirmed by the Final Fantasy Wiki【74†L281-L289】【74†L290-L293】. (In the Steam/PC version the Game Launcher appears if the game was started via the launcher.)  

## Cursor & Selection State

Internally, the game must track which menu item is currently highlighted (a “cursor index” from 0–12). However, **no public documentation** gives a fixed address for this. It is likely stored in the menu’s state data (possibly in the `menu_data_1D76A9C` pointer mentioned in FFNx, or within the `ff8_win_obj` data for the menu window). FFNx’s `ff8_win_obj` structure (used for dialog and menu windows) contains fields like `current_choice_question` and `first_question`【85†L3383-L3391】, which are used for list scrolling. The top‐level menu may use this same window system; menu rendering does call a dedicated `menu_draw_text` routine (per FFNx’s externals), but it likely still uses window objects. In practice, one must hook or poll the menu controller functions or draw routines to determine the current selection; the raw address of a “current menu index” is not publicly documented.

## Menu Rendering & Callbacks

FFNx defines several externals related to menu handling (listed in `ff8.h`), but none have published signatures. For example, there are pointers for `menu_config_controller`, `menu_config_render`, `main_menu_controller`, `main_menu_render_sub_4E5550`, `menu_draw_text`, etc. We do know:

- **`main_menu_controller`** – presumably handles input at the top menu (e.g. moving the cursor left/right). No signature is documented, but it is called every frame when in menu mode. Hooking this could capture when the highlighted item changes.
- **`main_menu_render_sub_4E5550`** – likely the routine that draws the top‐level menu graphics and text each frame. Intercepting it could reveal which item is being drawn (perhaps via color or alpha for the highlight).
- **`menu_draw_text`** – a function that draws an individual menu string on screen. Its parameters are not documented, but by analogy to the field/dialog text routines it likely takes a text pointer plus X,Y coordinates (and possibly a color/highlight flag). Hooking it would let a mod see every string the game attempts to draw. Highlighted items might be drawn with a different color or shading, which the hook could detect.

The array **`menu_callbacks`** in FFNx is probably one entry per top‐level menu item, containing function pointers to handle when that menu is activated. Its length should match the 13 entries above, though the exact number of callbacks and their functions aren’t documented in the source. FFNx also lists `create_save_file_sub_4C6E50`, which is likely the function that actually writes a save; hooking it could detect save completion. In general, the menu shares the FF8 window/text system (it uses the same window array and text encoding as other UI), even if it has dedicated controller/draw functions.

## Menu Text Encoding and Data

Unlike field/dialog text, **menu strings are not stored in `kernel.bin`**. Instead, they are packed in the game’s menu asset files (e.g. in `\ff8\data\eng\menu\mngrphd.bin` etc.), which are loaded at startup【102†L1314-L1318】. These files use FF8’s special “encoded strings” format (with a header and pointer table)【38†L33-L40】【41†L39-L43】. For example, the wiki notes that menu strings (like “Junction”, “Item”, etc.) are stored in these encoded archives, with each string ending in `0x00` after an encoding offset【38†L33-L40】. Therefore, the routine `get_text_data` (which FFNx points to) may not be used for menu text in the same way it is for field dialog. Instead, menu text is likely fetched from memory buffers loaded by the game’s file loader. In short, menu text uses the same FF8 character encoding as other in-game text, but is accessed from the menu-specific binary files, not via the kernel string table.

## Save Menu (Slot Selection)

When **Save** is chosen, a slot-selection menu appears. The game reads each slot’s header and displays:

- **Squall’s name** (12-byte string)  
- **Squall’s level**  
- **Total play time** (in seconds)  
- **Location name** (looked up from the location ID)  
- **Gil amount**  

These fields come from the save header (`savemap_ff8_header`). The FF8 Modding Wiki documents the save header layout for PC Steam: e.g. bytes at offset 0x0018 for Squall’s name, 0x0014 for his level, 0x0010 for play time, 0x000C for Gil, and 0x0004 for location ID【107†L450-L458】【107†L460-L462】. The game marks a slot empty if these values indicate no save (typically, an empty name or zeroes). When displaying an occupied slot, it renders the above info. (Location IDs map to names via the game’s location text data.) A confirmation dialog (Yes/No) is shown if you overwrite a nonempty slot; this is just a standard dialog and can be caught with the existing `show_dialog` hook in FFNx. The exact in-memory variable tracking the *highlighted slot index* is not documented publicly. One would likely need to hook the slot-list controller or draw routines (or poll an index in the save menu’s window object) to read which slot (1–30) is selected at any moment.

## Submenu Cursor Positions

Each submenu (Item, Magic, GF, Status, Junction, Switch, Config, etc.) maintains its own cursor state. The precise addresses of these state variables are not documented. Generally:

- **Items:** A scrolling list of inventory items. Likely tracks an overall list offset plus a highlight index (based on the current character’s inventory).
- **Magic/GF:** First a character select (cursor on party members), then a list of spells/GFs. So there are two indices (character index 0–3, and spell/GF index within that character’s list).
- **Status:** Character select (0–3), then stat screens (multiple pages). The selected character index is tracked (FFNx’s `character_index_current` would be relevant, though not documented for menu).
- **Junction:** Has three levels of selection: character (0–3), category (HP‐J, Str‐J, etc., 0–%), and then spell index. Each is presumably stored in the menu data or window state.
- **Config:** A simple list of options (e.g. Controller, Sound, Visuals, others). Likely has one index (which option is highlighted). FFNx has structures for `menu_config_input`, suggesting it uses its own input data tables.
- **Switch:** A grid to swap party members; it likely tracks two indices (first character to swap, then second), but exact details aren’t public.
- **Card:** A list of cards or rules, one index in a list.

In summary, *every* submenu controller must update some internal variables for the cursor. These were not found in open documentation. The safest approach is to hook the specific submenu controller functions (if known) or observe the `ff8_win_obj` fields (e.g. `current_choice_question`, `first_question`, `last_question`) that get updated by window drawing.

## Hooking Strategy

Given the above, recommended hook points are:

- **Top‐Level Controller:** Hook `main_menu_controller`. This is called each frame when the top menu is open, so you can detect cursor movements or button presses directly and read/record the current index *before* the game processes it. Since its signature is unknown, one would hook the function pointer in FFNx’s externals or pattern-match the game code.
- **Menu Draw/Text:** Hook `menu_draw_text` or the GTE rendering calls. Every time the game draws a menu string, your hook gets the text and position. By detecting which strings are drawn in highlight color, you can infer the current selection. This is often simpler than finding a hidden index variable.
- **Submenu Controllers:** Similarly, each submenu likely has a controller function (e.g. `menu_item_controller`, `menu_magic_controller`, etc.). Hooking those lets you track inner menu movement.
- **Polling Memory:** If you can locate the menu window objects (`pWindowsArray`) used by the menu, you could poll their fields each frame for changes in `current_choice_question`. This is less direct and requires reverse-engineering those object offsets.
- **Game Mode & Win Conditions:** You should check `pGameMode` or relevant flags to know when you’ve entered or left the menu (mode 6). There are no built‐in “cursor changed” events; polling or hooking is necessary.

Note: FFNx also has `pause_menu` and `pause_menu_with_vibration` functions, but those relate only to pausing during battle (not the field menu), so they can be ignored for the in-game menu.

## Mode Transitions

When Triangle/Menu is pressed in field mode, the game sets the module index to 0x05, which invokes the in-game menu module【92†L119-L122】. This immediately changes `pGameMode` from field (1) to menu (6)【108†L2328-L2331】【92†L119-L122】. Exiting the menu (via Exit or Close) sets the mode back to field. In practice this is instantaneous (aside from any fade animations); there is no separate “opening” vs “open” state variable exposed. In mod code you can detect `FF8_MODE_MENU` (6) to know the menu is active【108†L2328-L2331】, and watch for it to revert to field mode.

## Documentation References

Much of the menu system is *not* fully documented online, but some useful references include:

- The **Final Fantasy Wiki (Fandom)** lists the menu contents and functions【74†L281-L289】【74†L290-L293】. 
- The **FFInside / Qhimm wiki** explains the file formats for menu strings (e.g. *tkmnmes*, *mngrp*)【38†L33-L40】【41†L39-L43】. These show that menu text is in encoded binary archives rather than in kernel.bin. 
- The **FF8 Modding Wiki** (hobbitdur) provides the PC save file format, confirming which header fields correspond to name, level, time, etc.【107†L450-L458】【107†L460-L462】. 
- The **Qhimm forum (FF8 engine reverse) thread** gives low-level insights (e.g. it shows “05 = call in-game menu”【92†L119-L122】 and lists which data files are loaded, including *menu\mngrphd.bin*【102†L1314-L1318】). 

No single source lists the in-memory menu state variables or function signatures, so implementing the above features relies on combining these references with in-game testing or disassembly. 

**Sources:** Menu architecture and text data (FF8 Wiki)【74†L281-L293】; in-game module switching (Qhimm forum)【92†L119-L122】; menu string archive loading (Qhimm log)【102†L1314-L1318】; menu text encoding (FFInside)【38†L33-L40】【41†L39-L43】; save slot data format (FF8 Modding Wiki)【107†L450-L458】【107†L460-L462】; FFNx mode enum (source code)【108†L2328-L2331】.