# FF8 Steam 2013 accessibility mod: complete submenu reference

**The FF8 menu system comprises 11 submenus whose data lives almost entirely in one contiguous savemap block at `0x1CFDC5C`, with text sourced from kernel.bin (loaded into memory via `get_text_data`) and menu archives (mngrp.bin/tkmnmes).** Cursor positions for each submenu reside in the `pMenuStateA` region at `0x01D76A9A` and its surrounding offsets, though exact per-submenu cursor addresses require binary disassembly beyond what public documentation provides. The menu_callbacks array at `0x00B87ED8` dispatches 28+ submenu modules, but only indices 2, 7, 8, 11, 12, 16, and 27 are publicly confirmed — the remaining indices (Junction, Magic, GF, Status, Ability, Switch, Tutorial, Save) must be determined through IDA/Ghidra analysis of `FF8_EN.exe`. This report documents every visual element, navigation path, data source, and known memory offset for all 11 submenus to guide TTS announcement design.

---

## Savemap structure: the single source of truth for game state

All persistent game state lives in a contiguous save block. In the Steam 2013 English version, the savemap base address is **`0x1CFDC5C`**. Every offset below is relative to this base.

### Complete savemap layout (cumulative offsets from base)

| Offset | Size | Content |
|--------|------|---------|
| `+0x0000` | 96 bytes | **Save preview header** — checksum (2B), magic 0x08FF (2B), location ID (2B at +0x04), lead character HP (4B), save count (2B at +0x0A), Gil preview (4B at +0x0C), play time (4B at +0x20), level (1B at +0x24), portraits (3B at +0x25–0x27), Squall name (12B at +0x28), Rinoa name (12B at +0x34), Angelo name (12B at +0x40), Boko name (12B at +0x4C), disc (4B at +0x58), current save (4B at +0x5C) |
| `+0x0060` | 1088 bytes | **16 GFs × 68 bytes** — Quetzalcoatl at +0x60, Shiva at +0xA4, Ifrit at +0xE8 … Eden at +0x45C; block ends at **+0x04A0** |
| `+0x04A0` | 1216 bytes | **8 Characters × 152 bytes** — Squall at +0x4A0, Zell at +0x538, Irvine at +0x5D0, Quistis at +0x668, Rinoa at +0x700, Selphie at +0x798, Seifer at +0x830, Edea at +0x8C8; block ends at **+0x0960** |
| `+0x0960` | 400 bytes | **Shop data** (20 shops × 20 bytes) |
| `+0x0AF0` | 20 bytes | **Configuration settings** |
| `+0x0B04` | 4 bytes | **Active party** — 3 character IDs + 0xFF terminator (0=Squall, 1=Zell, 2=Irvine, 3=Quistis, 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea) |
| `+0x0B0C` | 12 bytes | Griever name (FF8 text encoding) |
| `+0x0B1C` | 4 bytes | **Gil** (actual gameplay value) |
| `+0x0B20` | 4 bytes | Laguna's Gil |
| `+0x0B24` | 10 bytes | **Limit break data** — Quistis blue magic bitmask (2B), Zell duel bitmask (2B), Irvine ammo (1B), Selphie (1B), Angelo complete/known/points (4B) |
| `+0x0B34` | 32 bytes | **Item battle order** — 32-byte index array for Item command in battle |
| `+0x0B54` | 396 bytes | **Item inventory** — 198 slots × 2 bytes each (1B item_id + 1B quantity); ends at **+0x0CE0** |
| `+0x0CE0` | 4 bytes | **Game time** (total seconds played) |
| `+0x0CEC` | 4 bytes | Victory count |
| `+0x0D33` | 16 bytes | Tutorial info flags (which tutorials viewed, Information entries unlocked) |
| `+0x0D43` | 1 byte | **SeeD test level** (highest passed) |
| `+0x0D48` | 4 bytes | Party (alternate/secondary reference, last byte = 0xFF) |
| `+0x0D50` | 2 bytes | Current module (1=field, 2=worldmap, 3=battle) |
| `+0x0D52` | 2 bytes | **Current field ID** |
| `+0x0D70` | 1280 bytes | **Field variables** (256 + 1024 bytes); game_moment at varblock +0x100 = savemap **+0x0E70** |
| `+0x1270` | 128 bytes | **World map data** |
| `+0x12F0` | 128 bytes | **Triple Triad data** — card collection, regional rules, trade rules |
| `+0x1370` | 64 bytes | Chocobo World data |

### Character struct (152 bytes, `savemap_ff8_character`)

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 2B | Current HP |
| 0x02 | 2B | Max HP |
| 0x04 | 4B | EXP (level calculated from EXP, not stored) |
| 0x08 | 1B | Model ID |
| 0x09 | 1B | Weapon ID |
| 0x0A–0x0F | 6B | **Base stats**: STR, VIT, MAG, SPR, SPD, LCK (1 byte each) |
| 0x10–0x4F | 64B | **Magics[32]**: 32 slots × 2 bytes (spell_id + quantity, 0–100) |
| 0x50–0x52 | 3B | Equipped command abilities (3 slots) |
| 0x54–0x57 | 4B | Equipped character/party abilities (bitmask or IDs) |
| 0x58–0x59 | 2B | **Junctioned GFs** (16-bit bitmask: bit 0=Quetzalcoatl … bit 15=Eden) |
| 0x5C | 1B | **HP junction** (magic ID junctioned to HP; 0 = none) |
| 0x5D | 1B | **STR junction** |
| 0x5E | 1B | VIT junction |
| 0x5F | 1B | MAG junction |
| 0x60 | 1B | SPR junction |
| 0x61 | 1B | SPD junction |
| 0x62 | 1B | EVA junction |
| 0x63 | 1B | HIT junction |
| 0x64 | 1B | LCK junction |
| 0x65 | 1B | **Elem-Atk** junction (magic ID) |
| 0x66 | 1B | **ST-Atk** junction (magic ID) |
| 0x67–0x6A | 4B | **Elem-Def** junctions (4 magic IDs) |
| 0x6B–0x6E | 4B | **ST-Def** junctions (4 magic IDs) |
| 0x70–0x8F | 32B | GF compatibility (16 GFs × 2 bytes each) |
| 0x90–0x93 | 4B | Kill count (2B) + KO count (2B) |
| 0x94 | 1B | Exists flag |

### GF struct (68 bytes, `savemap_ff8_gf`)

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 12B | **GF name** (null-terminated, FF8 text encoding, editable) |
| 0x0C | 4B | EXP (level calculated, not stored) |
| 0x11 | 1B | Exists flag |
| 0x12 | 2B | HP |
| 0x14 | 16B | **Completed abilities** bitfield (1 bit per ability, ~119 trackable) |
| 0x24 | 24B | **AP progress** per ability (1 byte per ability slot, current AP toward learning) |
| 0x3C–0x3F | 4B | Kill count (2B) + KO count (2B) |
| 0x41 | 1B | **Currently learning ability** (index into GF's ability list) |
| 0x42 | 3B | Forgotten abilities bitfield |

---

## Kernel.bin string sections and the text pipeline

FF8's kernel.bin is a BIN-GZIP archive containing approximately **33 gzipped sections**. The first ~31 sections are data, and the remaining sections are text. Each data section that references names/descriptions has offset fields pointing into companion text sections. The PC version also has **kernel2.bin** which stores only the text sections (ungzipped, LZS-compressed).

### Data sections (confirmed from Doomtrain wiki)

| # | Section | Content |
|---|---------|---------|
| 0 | Battle Commands | 39 command definitions, 8B each |
| 1 | **Magic Data** | 57 spell definitions, ~60B each — junction values, element, status |
| 2 | Junctionable GFs | 16 GFs, 132B each (distinct from savemap GF struct — this is base data) |
| 3 | Enemy Attacks | 384 entries, 20B each |
| 4 | **Weapons** | 33 entries, 12B each |
| 5 | Renzokuken Finishers | 4 entries, 24B each |
| 6 | Characters | 11 entries, ~36B each (base stat curves) |
| 7 | **Battle Items** | 33 entries, 24B each |
| 8 | **Non-battle Items** | 198 entries, 4B each (name/desc offset pairs into text section) |
| 9 | Non-junction GF Attacks | Phoenix, Gilgamesh, MiniMog, etc. — 16 entries |
| 10 | Command Abilities (battle) | ~20 entries |
| 11 | **Junction Abilities** | HP-J through ST-Def-J definitions, ~20 entries, 8B each |
| 12 | Command Abilities (GF) | GF-learnable command abilities |
| 13 | Stat % Abilities | HP+20%, Str+40%, etc. |
| 14 | Character Abilities | Mug, Cover, Counter, etc. |
| 15 | Party Abilities | Enc-None, Move-Find, Alert, etc. |
| 16 | GF Abilities | SumMag+10%, GFHP+10%, Boost, etc. |
| 17 | **Menu Abilities** | T Mag-RF, Card Mod, Haggle, etc. — 24 entries, 8B each |
| 18–28 | Limit Break data | Temp chars, Blue Magic, Shot, Duel, Rinoa, Slot |
| 29 | Devour | Devour effects |
| 30 | Misc Section | Miscellaneous constants |
| 31 | Misc Text Pointers | Pointers to misc text (status names, element names, etc.) |

### Text sections and string lookup

Text sections follow the data sections in the kernel.bin archive. Each data section has a companion text section containing the names and descriptions for that data. The game engine's `get_text_data(pool_id, cat_id, text_id, a4)` function retrieves strings at runtime. The mapping is:

- **Magic/spell names**: Text companion to Section 1 (Magic Data). 57 spell entries, indexed 0–56.
- **GF names/attack names**: Text companion to Section 2 (Junctionable GFs)
- **Weapon names**: Text companion to Section 4
- **Battle item names + descriptions**: Text companion to Section 7
- **Non-battle item names + descriptions**: Text companion to Section 8. The 198 items' names are indexed by item_id from the savemap inventory.
- **Junction ability names** (HP-J, Str-J, etc.): Text companion to Section 11
- **Command ability names** (Draw, Card, etc.): Text companion to Sections 10/12
- **Character ability names**: Text companion to Section 14
- **Party ability names**: Text companion to Section 15
- **GF ability names**: Text companion to Section 16
- **Menu ability names**: Text companion to Section 17
- **Status effect names, elemental type names, character default names**: In text companion to Section 31 (Misc Text Pointers)
- **Card names**: NOT in kernel.bin — stored in **mngrp.bin / tkmnmes files** within menu.fs
- **Menu labels** (stat labels, "Junction", "Item", etc.): Stored in **mngrp.bin** menu text archives

### String encoding

FF8 uses a custom byte encoding (not ASCII). Key rules: byte value minus **0x20** maps to the font texture grid position. Special control bytes include `0x00` (null terminator), `0x02` (newline), `0x03 XX` (character name insertion), `0x05 XX` (icon from icons.tex), `0x06 XX` (color change), `0x0C XX` (spell name insertion by ID).

---

## Junction: the most complex submenu with 7+ navigation phases

The Junction menu is FF8's primary equipment system. A blind user must navigate character selection, action menus, GF assignment, stat-to-magic pairing with real-time previews, and ability configuration.

### Visual layout and phases

**Phase 1 — Character select**: Three active party members displayed vertically with name, portrait, level, and HP. Cursor moves up/down; Confirm enters that character's junction screen; L1/R1 cycles characters.

**Phase 2 — Action menu**: Top-left panel shows 4 options in a vertical list: **Junction** (sub-options: GF, Magic), **Off** (sub-options: Magic, All), **Auto** (sub-options: Atk, Mag, Def), and **Ability**. Right panel displays the character's computed stats (HP, Str, Vit, Mag, Spr, Spd, Eva, Hit, Luck) plus Elem-Atk, Elem-Def ×4, ST-Atk, ST-Def ×4. Bottom panel lists junctioned GF names. Stats in white are junctionable (a junctioned GF has the -J ability); stats in gray cannot be junctioned.

**Phase 3 — GF assignment** (Junction → GF): Scrollable list of all acquired GFs. Each shows name and which character (if any) it is currently assigned to. Toggle assignment with Confirm. No limit on GFs per character.

**Phase 4 — Magic-to-stat assignment** (Junction → Magic): Cursor moves to the stat panel. Only white stats are selectable. Selecting a stat opens a **spell list** showing all spells in the character's inventory with name, stock quantity, and a junction marker if already junctioned elsewhere. The critical **stat preview system** dynamically shows **current → projected** stat value as the cursor hovers over each spell. For TTS, announce: "Firaga, stock 78, Strength would change from 42 to 72."

**Phase 5 — Auto**: Selecting Atk/Mag/Def auto-assigns magic from stock to all available junction slots prioritizing the chosen stat category. Requires GFs to be assigned first.

**Phase 6 — Off**: "Magic" removes all junctioned magic; "All" removes everything (GFs, magic, abilities).

**Phase 7 — Ability**: Two sections — **Command abilities** (3 slots, first always "Attack") and **Support abilities** (2 slots, expandable to 4 with Abilityx4). Only abilities from junctioned GFs are available.

### Data sources for junction

All junction state is in the character struct. The magic ID stored at offsets 0x5C–0x6E tells exactly which spell is junctioned to which stat. The stat preview computation uses kernel.bin Section 1 (Magic Data) which contains the junction bonus values per spell — each spell record has fields for HP-J bonus, Str-J bonus, etc. **The game computes junctioned stats as: base_stat + (spell_junction_value × stock_quantity / 100).** The exact formula varies per stat, but this is the core mechanism. Whether the game calls a dedicated computation function or inlines the calculation requires disassembly to confirm.

### Accessibility approach for Junction

This submenu requires **multi-level cursor tracking**. The mod needs to detect: (1) which phase the user is in (action menu vs GF list vs stat selection vs spell list), (2) the cursor position within each phase, and (3) the stat preview values. The most reliable approach is a **hybrid**: read junction state directly from savemap (character struct offsets 0x5C–0x6E, magics array at 0x10), and **hook `get_character_width` / `menu_draw_text`** to capture the stat preview text as it renders. TTS should announce the spell name, stock, and stat change on each cursor movement.

---

## Item: inventory browsing with target selection

### Visual layout

Top-left panel shows help/description text for the highlighted item. The main panel is a scrollable vertical list of up to **198 item slots** (typically 8–10 visible at once). Each row shows item name and quantity (e.g., "Potion ×12"). A bottom panel provides sub-options: Rearrange (Customize, Auto-sort) and Battle (for battle item order). When using an item, a target selection panel appears showing 3 party members with names and HP.

### Navigation flow

Select item from list (up/down, confirm) → if usable in field (healing/support items), target selection appears (up/down to pick character, confirm to use) → stock decreases by 1 → HP updates on screen. Cancel backs out at each level. L1/R1 page-scrolls the item list.

### Data fields and sources

Inventory at savemap **+0x0B54**: 198 × 2-byte slots (item_id byte + quantity byte). Item names from kernel.bin text companion to Section 8 (Non-battle Items), indexed by item_id. Item descriptions from the same section. Battle items (Potions, Phoenix Downs, etc.) also have data in Section 7 for their effects. **FF8 has no separate key item category** — all items share the same 198-slot pool.

### Accessibility approach

Read the 198-slot array directly from savemap +0x0B54. For each non-zero item_id, look up the name via `get_text_data()` or pre-cache kernel.bin text. Track the item list cursor and scroll position from the `pMenuStateA` region. Announce: "Item [N] of [total]: [Name], quantity [Q]. [Description]." For target selection, track the target cursor and announce character name + current/max HP.

---

## Magic: spell inventory with use, exchange, and discard

### Visual layout

After character selection (3 party members, same as Junction), the screen shows a top-left action menu with 4 options: **Use**, **Rearrange**, **Sort**, **Exchange**. The main panel displays the character's magic inventory as a scrollable list of up to **32 spell slots**, each showing spell name, stock quantity (×1 to ×100), and a junction indicator if the spell is junctioned to any stat. Right panel shows character info (name, portrait, level, HP).

### Navigation flow

Character select → action menu (Use/Rearrange/Sort/Exchange) → spell list → if Use: target selection (party members) → cast spell, stock decreases by 1. Exchange: select spell → select target character → spells transfer. Discard: available via a special button (Square/equivalent) during Exchange mode with confirmation prompt. **Warning for TTS**: if a junctioned spell's stock is reduced, the stat bonus decreases — the junction indicator is critical information for blind users.

### Data fields

Character magic array at char_struct **+0x10**: 32 slots × 2 bytes (spell_id + quantity). Spell names from kernel.bin Section 1 companion text, indexed by spell_id. Junction status determined by cross-referencing the spell_id against the character's junction fields (0x5C–0x6E).

### Accessibility approach

Read the 32-slot magic array directly from the character struct. For each entry, announce: "[Spell name], stock [N]" and append "junctioned" if the spell_id matches any junction field value. Hook `get_character_width` to capture the description text being rendered. For the Use action, track the target cursor.

---

## Status: four pages of character detail

### Visual layout across 4 pages

**Page 1 — Stats & equipment**: Character name, portrait, level, HP/MaxHP, EXP/Next Level, equipped weapon name. Full stat panel: HP, Str, Vit, Mag, Spr, Spd, Eva, Hit, Luck (all **final computed values** including junction bonuses). Junction summary at bottom: junctioned GF names, spell junctioned to each stat, equipped command abilities (3–4), equipped support abilities (2–4).

**Page 2 — Elemental & status resistances**: Elemental defense percentages for all 8 elements (Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy) derived from Elem-Def junctions. Elemental attack info from Elem-Atk junction. Status defense percentages for ~13 status effects (Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Slow, Stop, Curse, Confuse, Drain) from ST-Def junctions. Status attack info from ST-Atk junction.

**Page 3 — GF compatibility**: Lists all 16 GFs with compatibility values for the current character (range ~0–1000, default ~600). Diamond icon marks GFs currently junctioned to this character. Higher compatibility = faster summon charge time.

**Page 4 — Limit break info**: Character-specific. Squall: Renzokuken finishers learned and enabled/disabled. Zell: Duel combo moves. Rinoa: Angelo abilities + learning progress. Quistis: Blue Magic abilities learned. Irvine: Shot types available. Selphie: Slot info.

### Navigation

L1/R1 cycles pages 1→2→3→4→1. L2/R2 switches between party members. Cancel returns to character select or main menu.

### Data sources

Stats on Page 1 are **computed values** (base + junction bonus + percentage modifiers). These may exist in a battle-computed stats buffer (FFNx references `character_data_1CFE74C`), or the Status menu may recompute them on the fly from base stats (char_struct +0x0A–0x0F), junction spell IDs (0x5C–0x6E), and kernel.bin Magic Data Section 1. For the mod, the simplest approach is to **capture the rendered text** via GCW hook rather than replicating the computation. GF compatibility from char_struct +0x70 (32 bytes, 16 × 2B). Limit break data from savemap +0x0B24 region.

### Accessibility approach

This menu is read-only — no cursor selection within pages, only page cycling. TTS should announce all visible data when a page is entered. Best approach: **hook `menu_draw_text`** to capture all stat text, element names, status names, and percentage values as they render.

---

## GF: Guardian Force management and ability learning

### Visual layout

Left panel: scrollable list of all acquired GFs (up to 16). Each shows GF name, level, and which character it's junctioned to. Right panel has two sub-views toggled with L1/R1:

**GF Status view**: Name, Level, HP/MaxHP, EXP/Next Level, kill/KO counts, currently learning ability with AP progress bar (e.g., "Boost 10/10 AP"), compatibility with current junction host.

**GF Ability view**: Scrollable list of up to 22 ability slots per GF. Each shows ability name + status: learned (full indicator), currently learning (blinking with "current AP / required AP"), not yet started (shows AP requirement with 0 progress), or not shown (prerequisites unmet or forgotten via Amnesia Greens).

### Navigation flow

GF select (up/down) → Confirm opens detail view → L1/R1 toggles Status/Ability views → in Ability view, Confirm on an unlearned ability sets it as "currently learning" → Cancel returns to GF list. Name editing available via Triangle/Square on the GF name (opens text input grid, max 12 characters).

### Data sources

GF struct at savemap +0x60 + (gf_index × 68). The 16 GFs in order: Quetzalcoatl (0), Shiva (1), Ifrit (2), Siren (3), Brothers (4), Diablos (5), Carbuncle (6), Leviathan (7), Pandemona (8), Cerberus (9), Alexander (10), Doomtrain (11), Bahamut (12), Cactuar (13), Tonberry (14), Eden (15). The "exists" flag at GF_struct+0x11 determines visibility. Currently learning ability index at +0x41. AP progress array at +0x24 (24 bytes). Completed abilities bitfield at +0x14 (16 bytes). GF ability names from kernel.bin text companions to Sections 11–17 (junction abilities, command abilities, character abilities, party abilities, GF abilities, menu abilities). GF names from the GF struct +0x00 (12 bytes, editable, FF8 text encoding).

### Accessibility approach

Read GF struct directly from savemap. For the ability list, cross-reference the completed bitfield and AP progress array against the GF's learnable ability list (from kernel.bin Section 2, Junctionable GFs data — each GF's 132-byte record contains its ability list). Announce: "[GF name], Level [L], HP [cur/max], learning [ability name] [cur_AP/required_AP] AP."

---

## Ability: menu abilities for item refinement and shopping

### Visual layout

A single-level list of all learned **Menu Abilities** (kernel.bin Section 17). No character selection required — these are global GF abilities. Top-left panel shows help/description text. Main panel shows the ability list. Selecting a refine ability opens the inventory filtered to show only refinable items, then a quantity/preview dialog showing source → result conversion.

### Complete menu abilities list

Refining abilities: T Mag-RF, I Mag-RF, F Mag-RF, L Mag-RF, ST Mag-RF, Supt Mag-RF, Time Mag-RF, Mid Mag-RF, High Mag-RF, Forbid Mag-RF, Recov Med-RF, ST Med-RF, Ammo-RF, Tool-RF, Forbid Med-RF, GF Recov Med-RF, GF Abl Med-RF, Med LV Up, Card Mod. Passive/shop abilities: Haggle, Sell-High, Familiar, Call Shop, Junk Shop, Med Data.

### Navigation

Select ability → for RF abilities: filtered item list → select item → quantity prompt → preview ("1 Fish Fin → 20 Water") → Confirm to execute. Passive abilities (Haggle, Sell-High, Familiar) just display their description; no interaction needed. Call Shop opens a shop list; Junk Shop opens weapon remodeling.

### Accessibility approach

Hook `menu_draw_text` to capture the ability name on cursor hover. For refine dialogs, the conversion preview text passes through the standard text pipeline and can be captured via GCW. Announce: "[Ability name]: [description]." When refining: "[Source item] ×[qty] → [Result] ×[qty]. Confirm?"

---

## Switch: party roster management

### Visual layout

Two sub-options at top: **Switch Member** and **Junction Exchange**. Below, the screen shows the active party (3 slots, top) and reserve characters (bottom). Each character entry displays name, level, and HP. **8 total character slots** exist in the savemap (Squall through Edea), but only unlocked/available characters appear. Squall is usually locked in the active party.

### Navigation flow

**Switch Member**: Select an active party member (source) → select a reserve character (destination) → they swap. Only available on world map or at forced party-select story points.

**Junction Exchange**: Select first character → select second character → all GFs, magic, command abilities, and support abilities swap between them.

### Data source

Active party at savemap **+0x0B04** (4 bytes: 3 character IDs + 0xFF terminator). Character IDs: 0=Squall, 1=Zell, 2=Irvine, 3=Quistis, 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea. The secondary party reference at +0x0D48 also holds party data. Each character's "exists" flag (char_struct +0x94) determines visibility. Story-locked characters are tracked through field variables.

### Accessibility approach

Read party array at +0x0B04 and cross-reference with all 8 character structs. Announce: "Active party: [Name1] Lv[L] HP[cur/max], [Name2]…" and "Reserve: [Name3]…". Track the two-phase cursor (source selection → destination selection).

---

## Card: Triple Triad collection viewer

### Visual layout

A browsing screen showing all 110 Triple Triad cards organized by level (10 levels × 11 cards per level). Each card entry shows the card image, card name, four directional rank values (Top/Right/Bottom/Left, values 1–9 and A=10), elemental icon if applicable, and quantity owned. Cards previously owned but no longer held appear grayed out. Rare cards (levels 8–10) show location status: "Squall" if owned, an NPC region name if lost, or "Used" if Card Mod'd. A gold star appears on the main menu's Card option if all 110 unique cards have been collected.

### Data sources

**Card collection** at savemap **+0x12F0** (128 bytes total). Estimated layout: 77 bytes for common card counts (levels 1–7, 1 byte per card type), 33 bytes for rare card locations (levels 8–10, 1 byte per card indicating owner/location), ~16 bytes for regional rules and trade rules, plus padding. **Card stat data** (the four directional values and element) is **hardcoded in the game executable** (`FF8_EN.exe`), not in the savemap — each of the 110 cards has a ~6-byte record with top/bottom/left/right ranks and element type. Card names are stored in **mngrp.bin / tkmnmes files** within menu.fs, not in kernel.bin. The FFNx source references card text offsets at address **~0xB96504** (`card_texts_off_B96504`).

### Accessibility approach

Read the 128-byte Triple Triad block from savemap. For card names, either hook `menu_draw_text` to capture text as it renders, or use the card text pointer at 0xB96504 to read name strings directly. Announce: "[Card name], Level [L], ranks [Top]/[Right]/[Bottom]/[Left], element [E], owned ×[qty]" or for rare cards: "[Card name], [status: Owned/Lost to [NPC region]/Used]."

---

## Config: a simple option list with left/right cycling

### Visual layout

Single-page vertical list of configuration options. Each row shows an option label and its current value. Navigation: up/down selects option, left/right changes value. **PC Steam 2013 options**: Sound (volume slider), Controller (Normal/Custom — Custom opens remapping sub-screen), Cursor (Initial/Memory — Memory remembers last battle command), Camera Movement, Scan (Normal/Always).

### Data source

Config values at savemap **+0x0AF0** (20 bytes). External controller config in `ff8input.cfg`. FFNx hooks: `menu_config_controller` (input processing, resolved from `menu_callbacks[8].func +0x8`), `menu_config_render` (from `menu_callbacks[8].func +0x3`), `menu_config_input_desc` (from `menu_callbacks[8].func +0x39`).

### Accessibility approach

The Config menu is one of the simpler menus. Read config state from savemap +0x0AF0. Hook `menu_draw_text` to capture option labels and current values. Announce: "[Option name]: [current value]. Left/Right to change." The controller customization sub-screen is more complex and may require dedicated cursor tracking.

---

## Tutorial and SeeD test: hierarchical text viewer plus quiz system

### Tutorial structure

A hierarchical category menu with 5–6 top-level categories: **Basic Tutorials** (junction, draw, GF, magic, battle, limit breaks, elements, status effects), **Information** (progressively unlocked glossary of locations, people, organizations, events — some entries are missable), **Icon Explanation**, **Online Help**, **SeeD Tests**, and **Battle Meter** (available only after obtaining the Battle Meter item from Cid).

Tutorial text is stored in **tkmnmes files** within menu.fs. Information glossary entries unlock based on story progress and are tracked via the 16-byte tutorial info flags at savemap +0x0D33. Text is presented as full paragraphs in standard FF8 menu windows with scrolling support.

### SeeD test details

**30 test levels**, each with **10 true/false (Yes/No) questions**. All 10 must be answered correctly to pass. No penalty for failure — retake unlimited times. Tests must be completed sequentially. Squall can only attempt tests up to his current character level. Passing raises SeeD rank by 1 (31 ranks total: 1–30 plus Rank A). SeeD salary ranges from **500 Gil** (Rank 1) to **30,000 Gil** (Rank A), paid approximately every 24,300 steps. Rank decays over time (SeeD experience decreases by 10 per payment).

Questions and answers stored in tkmnmes/mngrp.bin text files. Test level progress at savemap **+0x0D43** (1 byte).

### Accessibility approach

Tutorial text renders through the standard text pipeline — **hook `menu_draw_text`** to capture all text content. For SeeD tests, capture the question text and announce: "Question [N] of 10: [question text]. Yes or No?" Announce pass/fail result.

---

## Save: slot selection with preview data

### Visual layout

Scrollable list of save slots. PC Steam 2013 has **60 slots** (2 pages × 30 slots, Slot 1-1 reserved for autosave). Each occupied slot shows: character portraits (3 party members), lead character name and level, current HP/max HP, location name, play time (HH:MM:SS), disc number, Gil, and save count. Empty slots display blank.

### Navigation

Up/down scrolls through slots, Confirm on a slot → overwrite confirmation if occupied → save completes. Save is only available at save points or on the world map. Load is accessed from the title screen, not from the in-game Save menu.

### Data source

Preview data from the save header (savemap +0x00 through +0x5C). Location ID at +0x04 maps to a location name string. The save/load system uses `ff8_create_save_file` (FFNx hook: `create_save_file_sub_4C6E50`).

### Accessibility approach

For save slot browsing, capture the preview text via GCW hook as each slot renders. Announce: "Slot [N]: [Character name] Level [L], HP [cur/max], [Location], [Play time], [Gil] Gil, Disc [D]" or "Slot [N]: Empty."

---

## Menu callbacks: confirmed and unconfirmed indices

The `menu_callbacks` array at **0x00B87ED8** holds 8-byte entries (struct with at minimum a `func` field as a function pointer). The menu dispatch function `sub_4BDB30` indexes into this array to call the appropriate submenu handler.

| Index | Submenu | Confidence |
|-------|---------|------------|
| 0 | **Junction** (likely) | Speculative — first in menu display order |
| 1 | Unknown | Unconfirmed |
| 2 | **Items** | Confirmed (user-provided) |
| 3 | **Magic** (likely) | Speculative |
| 4 | **GF** (likely) | Speculative |
| 5 | **Status** (likely) | Speculative |
| 6 | **Ability** (likely) | Speculative |
| 7 | **Cards** | Confirmed (FFNx source) |
| 8 | **Config** | Confirmed (FFNx source) |
| 9 | **Tutorial** (likely) | Speculative |
| 10 | **Save** (likely) | Speculative |
| 11 | **Shop** | Confirmed (user-provided) |
| 12 | **Junk Shop** | Confirmed (user-provided) |
| 16 | **Top-level menu** | Confirmed (user-provided) |
| 23 | Unknown | Confirmed exists (user-provided) |
| 27 | **Chocobo World** | Confirmed (user-provided) |

**The speculative indices (0, 1, 3–6, 9, 10) are NOT publicly documented.** They must be confirmed through IDA/Ghidra disassembly of `sub_4BDB30` in `FF8_EN.exe`. The indices do NOT follow the in-game menu display order (proven by Items=2, Cards=7, Config=8 not being sequential). Approach: set a breakpoint on the menu_callbacks array access and observe which index is loaded when entering each submenu.

---

## Cursor tracking: where positions live in memory

The top-level menu cursor is at **`pMenuStateA + 0x1E6`** (byte, values 0–10). For submenu cursors, the situation is more complex. The `pMenuStateA` region (starting at `0x01D76A9A`) contains a large block of menu state data. Based on the FF8 engine's architecture:

Each submenu likely stores its cursor state in **offsets near pMenuStateA**, within the ff8_win_obj window structures at **`0x01D2B330`**, or within the submenu's own local state managed by its callback function. The `ff8_win_obj` array holds window/panel structures that include scroll positions and cursor indices.

**Known cursor addresses from ff8-speedruns/ff8-memory project**: `0x192B35B` (1 byte) is documented as "Menu Choice" but noted as "somewhat inconsistent." The `pMenuStateA + 0x1E6` top-level cursor is confirmed reliable.

### Recommended discovery method

For each submenu, use Cheat Engine to:
1. Enter the submenu, note cursor position 0
2. Move cursor down, scan for value 1
3. Move again, scan for value 2
4. The address that consistently tracks is the cursor variable

Focus the search around: `pMenuStateA` (0x01D76A9A) ± 0x400 bytes, and the ff8_win_obj region at 0x01D2B330.

---

## Text rendering pipeline: the GCW approach works broadly

The text rendering call chain is: **`sub_4B3410` → `sub_4BE4D0` → `sub_4BECC0` → `menu_draw_text` (0x004BDE30) → `get_character_width` (0x004A0CD0)`**. Font data comes from **sysfnt.tdw** (character widths) and **sysfnt.tex** (font texture atlas with 8 palette colors).

**All dynamic text** — character names, stat numbers, spell names, item names, ability names, descriptions, quantities, Gil amounts, play time — renders through this pipeline as character strings. Hooking `get_character_width` captures individual characters as they process; accumulating characters between frame boundaries reconstructs full strings. **Numeric values (HP, stats, quantities) ARE rendered as text strings** through the same pipeline, not as sprite-based digits.

**What GCW will NOT capture**: pre-rendered graphical labels stored as textures in mngrp.bin, controller button icons (0x05XX sequences draw sprites via a separate `ff8_draw_icon_or_key` path), character face portraits (face.sp2/face*.tex), card images (cardanm.sp2), menu window borders, cursor/selection indicators, and decorative elements.

### Practical implication for the mod

The GCW buffer approach the developer already uses for help text should generalize to all submenus. When the user enters any submenu, all text that renders on screen passes through `get_character_width`. The challenge is **associating captured text with its semantic meaning** (is this string a spell name, a stat value, or a label?) — this requires knowing the rendering order or correlating with cursor position changes. A complementary approach is to read savemap data directly for structured information and use GCW only for labels and descriptions that are hard to read from memory.

---

## Recommended accessibility strategy per submenu

| Submenu | Primary approach | Supplementary |
|---------|-----------------|---------------|
| Junction | Savemap reads (junction fields, magic array) + GCW for stat previews | Hook stat preview computation function if found |
| Item | Savemap +0x0B54 direct read + GCW for descriptions | Track item cursor in pMenuStateA region |
| Magic | Character struct +0x10 magic array + GCW for descriptions | Cross-reference junction fields for indicator |
| Status | GCW text scrape (captures all rendered stats) | Savemap reads as fallback for raw values |
| GF | Savemap GF struct reads + GCW for ability names | kernel.bin Section 2 for ability list definitions |
| Ability | GCW text scrape (ability names + refine previews) | Menu ability list from kernel.bin Section 17 |
| Switch | Savemap party array +0x0B04 + character structs | GCW for character names |
| Card | GCW for card names + savemap +0x12F0 for collection | Card text pointer at ~0xB96504 for direct name reads |
| Config | GCW for option labels and values + savemap +0x0AF0 | FFNx menu_config hooks |
| Tutorial | GCW text scrape (captures all tutorial/test text) | — |
| Save | GCW for slot preview text + savemap header reads | — |

### Minimum TTS announcements for blind users

For every submenu, a blind user needs: **(1)** the current cursor position (which option/item/slot is highlighted), **(2)** the key data for that item (name, value, quantity, status), and **(3)** context about available actions (what does Confirm do, what does Cancel do). For multi-phase menus like Junction, the user also needs to know **(4)** which phase/level they are in. Audio cues for phase transitions (entering a sub-list, returning to a parent menu) greatly improve orientation.

---

## What requires further reverse engineering

Several critical pieces are not publicly documented and require IDA/Ghidra analysis of `FF8_EN.exe`:

- **Complete menu_callbacks index mapping** for Junction, Magic, GF, Status, Ability, Switch, Tutorial, and Save
- **Per-submenu cursor addresses** — exact offsets within the pMenuStateA/ff8_win_obj regions for each submenu's cursor state at each navigation level
- **Junction stat preview computation function** — the address of the function that calculates "what would this stat be if I junction this spell?" for real-time preview capture
- **Triple Triad 128-byte block internal layout** — the exact byte-by-byte structure within savemap +0x12F0 (the Hyne save editor source at github.com/myst6re/hyne is the best existing reference)
- **Card stat data addresses in EXE** — the HobbitDur CC-Group tool has identified these for patching but the exact Steam 2013 English offsets need confirmation
- **Kernel.bin text section exact numbering** — while the data section order (0–31) is documented, the companion text sections' exact index numbers in the gzip archive require examination of the kernel.bin file itself or the Doomtrain editor's section parsing code

The most productive next step is to attach a debugger to `FF8_EN.exe`, set breakpoints on `menu_callbacks` array access in `sub_4BDB30`, and catalog the index used when entering each submenu. This single session would resolve the callback mapping, and from each callback function's code, the submenu's cursor variable addresses can be traced.
