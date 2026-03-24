# FF8 Junction Ability Screen Memory Layout and ID Tables

**FF8 uses a single unified ability ID namespace (0–115) stored as one byte**, spanning all ability types: junction, command, stat-percentage, character, party, GF, and menu abilities. This report provides the complete ID tables, GF bitmap mechanics, and runtime architecture analysis needed for your TTS accessibility mod targeting the 2013 Steam edition. The unified ID system is confirmed by cross-referencing the HobbitDur FF8ModdingWiki kernel.bin documentation, WikiSquare's GF savemap PHP source, and the Hyne save editor struct definitions.

---

## 1. Complete command ability ID table

Command abilities occupy **unified IDs 20–38** in the Junctionable Abilities list (kernel.bin section 12, "Command abilities GF"). Each GF's ability list in kernel.bin references these unified IDs, and the GF's `completeAbilities` bitmap tracks them at these exact bit positions.

| Hex | Dec | Ability |
|-----|-----|---------|
| 0x14 | 20 | Magic |
| 0x15 | 21 | GF |
| 0x16 | 22 | Draw |
| 0x17 | 23 | Item |
| 0x18 | 24 | *(Empty/unused slot)* |
| 0x19 | 25 | Card |
| 0x1A | 26 | Doom |
| 0x1B | 27 | Mad Rush |
| 0x1C | 28 | Treatment |
| 0x1D | 29 | Defend |
| 0x1E | 30 | Darkside |
| 0x1F | 31 | Recover |
| 0x20 | 32 | Absorb |
| 0x21 | 33 | Revive |
| 0x22 | 34 | LV Down |
| 0x23 | 35 | LV Up |
| 0x24 | 36 | Kamikaze |
| 0x25 | 37 | Devour |
| 0x26 | 38 | MiniMog |

**IDs 20–25 are verified** against the WikiSquare GF data (Quezacotl: 20=Magic, 21=GF, 22=Draw, 23=Item, 25=Card; Shiva: 26=Doom; Ifrit: 27=Mad Rush). IDs 28–38 follow the established kernel.bin section ordering and are high-confidence.

**Critical caveat for `commands[3]` at character offset +0x50:** FF8 has a *separate* Battle Commands table (kernel.bin section 0) with its own indexing: Attack=1, Magic=2, GF=3, Draw=4, Item=5, Card=6, Doom=7, Mad Rush=8, Treatment=9, Defend=10, Darkside=11, Recover=12, Absorb=13, Revive=14, LV Down=15, LV Up=16, Kamikaze=17, Devour=18, MiniMog=19, plus character-specific limit breaks at 20+. **The bytes stored in `commands[3]` most likely use these Battle Command section 0 indices** (Magic=0x02, not 0x14), because the battle engine indexes directly into section 0 for command execution. Empty slot = **0x00**. Verify by hex-dumping a save with Magic/GF/Draw equipped and checking whether bytes at +0x50 are `02 03 04` (Battle Command IDs) or `14 15 16` (unified Junctionable Ability IDs).

---

## 2. Complete character and party ability ID table

Character and party abilities that can be equipped in `abilities[4]` span **unified IDs 39–82**, organized across three kernel.bin sections. These IDs are **the same values used as bit positions** in the GF `completeAbilities` bitmap.

### Stat percentage abilities (kernel.bin section 13, IDs 39–57)

| Hex | Dec | Ability |
|-----|-----|---------|
| 0x27 | 39 | HP+20% |
| 0x28 | 40 | HP+40% |
| 0x29 | 41 | HP+80% |
| 0x2A | 42 | Str+20% |
| 0x2B | 43 | Str+40% |
| 0x2C | 44 | Str+60% |
| 0x2D | 45 | Vit+20% |
| 0x2E | 46 | Vit+40% |
| 0x2F | 47 | Vit+60% |
| 0x30 | 48 | Mag+20% |
| 0x31 | 49 | Mag+40% |
| 0x32 | 50 | Mag+60% |
| 0x33 | 51 | Spr+20% |
| 0x34 | 52 | Spr+40% |
| 0x35 | 53 | Spr+60% |
| 0x36 | 54 | Spd+20% |
| 0x37 | 55 | Spd+40% |
| 0x38 | 56 | Eva+30% |
| 0x39 | 57 | Luck+50% |

**Verified:** Quezacotl has 48=Mag+20% and 49=Mag+40%; Shiva has 45=Vit+20%, 46=Vit+40%, 51=Spr+20%, 52=Spr+40%; Ifrit has 42=Str+20%, 43=Str+40%. All confirmed from WikiSquare data.

### Character abilities (kernel.bin section 14, IDs 58–77)

| Hex | Dec | Ability |
|-----|-----|---------|
| 0x3A | 58 | Mug |
| 0x3B | 59 | Med Data |
| 0x3C | 60 | Counter |
| 0x3D | 61 | Return Damage |
| 0x3E | 62 | Cover |
| 0x3F | 63 | Initiative |
| 0x40 | 64 | Move-HP Up |
| 0x41 | 65 | HP Bonus |
| 0x42 | 66 | Str Bonus |
| 0x43 | 67 | Vit Bonus |
| 0x44 | 68 | Mag Bonus |
| 0x45 | 69 | Spr Bonus |
| 0x46 | 70 | Auto-Protect |
| 0x47 | 71 | Auto-Shell |
| 0x48 | 72 | Auto-Reflect |
| 0x49 | 73 | Auto-Haste |
| 0x4A | 74 | Auto-Potion |
| 0x4B | 75 | Expendx2-1 |
| 0x4C | 76 | Expendx3-1 |
| 0x4D | 77 | Ribbon |

**ID 66=Str Bonus is confirmed** from Ifrit's WikiSquare ability list. The ordering of IDs 58–65 and 67–77 follows the Doomtrain kernel.bin section 14 ordering but has medium confidence — the exact sequence within this section should be verified against the Doomtrain wiki's "Characters abilities" page or the Doomtrain C# source code.

### Party abilities (kernel.bin section 15, IDs 78–82)

| Hex | Dec | Ability |
|-----|-----|---------|
| 0x4E | 78 | Alert |
| 0x4F | 79 | Move-Find |
| 0x50 | 80 | Enc-Half |
| 0x51 | 81 | Enc-None |
| 0x52 | 82 | Rare Item |

The `abilities[4]` field at character offset +0x54 stores **unified Junctionable Ability IDs** from the ranges above. Both stat-percentage and character/party abilities can be equipped in these 4 slots. Empty slot = **0x00**. The number of usable slots depends on whether the character's junctioned GFs have learned Ability×3 (ID 18) or Ability×4 (ID 19); the default is 2 slots.

---

## 3. Junction Ability screen runtime memory layout

No public documentation exists for the specific runtime buffer, menu state machine, or cursor-to-ability mapping of the Junction Ability screen. Here is what can be determined from the architecture and available reverse-engineering.

### How the engine likely builds the available abilities list

The Ability screen aggregates abilities from all GFs junctioned to the selected character. The process follows this logic:

1. Read `character.junctioned_gfs` (uint16 bitmask at +0x58). For each set bit N (0–15), GF N is junctioned.
2. For each junctioned GF, read its `completeAbilities[16]` bitmap (128-bit field at GF struct +0x14 in the savemap). Each set bit M means ability with unified ID M has been learned.
3. Filter by ability type for each panel: **command abilities** (IDs 20–38) for the top-left Command section, **character/party abilities** (IDs 39–82, specifically the stat%, character, and party ranges) for the bottom-left Ability section.
4. Deduplicate abilities available from multiple GFs (an ability learned by two junctioned GFs appears only once).
5. Build a sorted runtime array of available ability IDs for each panel.

This dynamically constructed list is almost certainly stored in a **temporary runtime buffer** allocated when the Ability sub-screen opens. The game's menu rendering code iterates this buffer to draw ability names and check equipped status.

### Cursor offsets from pMenuStateA

The `pMenuStateA` symbol name comes from IDA Pro disassembly of the FF8 executable and is not publicly documented in any modding wiki or GitHub source. No public source documents the specific offsets +0x271 and +0x272.

**For +0x272 (right-panel cursor on available abilities list):** This value most likely indexes into the runtime-built array of available ability IDs described above. The index would be a row position in the scrollable list on the right side. To get the actual ability ID, read `runtime_buffer[cursor_index]`, which yields a unified Junctionable Ability ID.

**For +0x271 (left-panel cursor):** The left panel of the Ability screen has two sections: Commands (3 equippable slots below the fixed Attack entry) and Abilities (2–4 passive ability slots). If +0x271 is a row index, then values **0–2** likely correspond to the three command slots (mapping to `commands[0]` through `commands[2]`), and values **3–6** (or a separate offset range) correspond to the passive ability slots (mapping to `abilities[0]` through `abilities[3]`). The boundary between command and ability sections may use a gap value or a separate state variable to indicate which section the cursor is in.

### Recommended approach for your mod

Rather than intercepting the menu's internal buffer (which requires reverse-engineering the undocumented menu state machine), a more reliable approach is:

1. **For equipped abilities (left panel):** Read directly from the character struct in the savemap. At runtime base `0x1CFDC5C`, the selected character's `commands[3]` is at `base + 0x48C + (char_index × 0x98) + 0x50`, and `abilities[4]` is at `+ 0x54`. Map the byte values to names using the tables in this report.

2. **For available abilities (right panel):** Iterate the character's junctioned GFs, check each GF's `completeAbilities` bitmap, and build your own filtered list in your mod. Then use the cursor offset (+0x272 from pMenuStateA) as an index into your reconstructed list. This avoids needing to locate the engine's internal buffer.

3. **For determining which character is selected:** The menu state machine likely stores the active character index somewhere near pMenuStateA. The ff8-speedruns/ff8-memory repo documents some menu-related addresses at `FF8_EN.exe+0x18FE9B8` (savemap variable block base).

---

## 4. GF ability bitmap to ability ID mapping

The GF struct in the savemap (68 bytes per GF, starting at savemap +0x4C) contains three related fields that use **different indexing systems**.

### completeAbilities — unified global IDs as bit positions

The **`completeAbilities[16]`** field at GF struct offset +0x14 is a **128-bit bitmap indexed by unified Junctionable Ability IDs**. Bit N being set means the GF has fully learned the ability with unified ID N. The Hyne save editor source confirms: "128 bits; 115+1 valid, 124 existing." This bitmap is NOT per-GF-slot indexed — it uses the global ID system.

To check if a GF has learned Magic (unified ID 20): read byte `completeAbilities[20/8]` = `completeAbilities[2]` and check bit `20 % 8` = bit 4. If set, the GF knows Magic.

To check Str+20% (unified ID 42): read `completeAbilities[5]` and check bit 2.

### APs — per-GF slot indices

The **`APs[24]`** field at GF struct offset +0x24 (22 bytes used, 2 padding) stores accumulated AP per ability **slot**, indexed 0–21. These are per-GF local slot indices, NOT unified ability IDs. Slot N corresponds to the Nth ability in that GF's kernel.bin ability list.

### learning — per-GF slot index

The **`learning`** byte at GF struct offset +0x40 stores the **slot index** (0–21) of the ability currently being learned. To determine which ability this is, look up slot N in the GF's kernel.bin entry.

### kernel.bin GF ability list structure

Each GF entry in kernel.bin section 2 ("Junctionable GFs") is **132 bytes**, starting at kernel.bin offset 0x0F78. The GF has **21 ability slots** at offsets 0x001C–0x006F within its entry. Each slot is 4 bytes:

| Byte | Description |
|------|-------------|
| +0 | Ability Unlocker index (references the AP threshold table) |
| +1 | Unknown |
| +2 | **Unified Ability ID** (from the Junctionable Abilities list) |
| +3 | Unknown |

So the mapping chain is:
- `GF.APs[slot]` → AP accumulated for this slot
- `kernel_gf_entry.abilities[slot].ability_id` → the unified ability ID for this slot
- `GF.completeAbilities[ability_id / 8] & (1 << (ability_id % 8))` → whether it's fully learned
- `GF.learning` → the slot index currently being learned

### Per-GF kernel.bin offsets

| Offset | GF |
|--------|-----|
| 0x0F78 | Quezacotl |
| 0x0FFC | Shiva |
| 0x1080 | Ifrit |
| 0x1104 | Siren |
| 0x1188 | Brothers |
| 0x120C | Diablos |
| 0x1290 | Carbuncle |
| 0x1314 | Leviathan |
| 0x1398 | Pandemona |
| 0x141C | Cerberus |
| 0x14A0 | Alexander |
| 0x1524 | Doomtrain |
| 0x15A8 | Bahamut |
| 0x162C | Cactuar |
| 0x16B0 | Tonberry |
| 0x1734 | Eden |

---

## 5. The unified ability ID namespace

FF8 uses a **single unified byte namespace (0–115)** covering all learnable GF abilities. The ID is assigned sequentially across kernel.bin sections 11–17. ID 0 represents None/Empty. The complete map:

| ID Range | Hex Range | Category | kernel.bin Section | Count |
|----------|-----------|----------|-------------------|-------|
| 0 | 0x00 | None/Empty | — | 1 |
| 1–19 | 0x01–0x13 | Junction abilities | Section 11 | 19 |
| 20–38 | 0x14–0x26 | Command abilities | Section 12 | 19 |
| 39–57 | 0x27–0x39 | Stat % abilities | Section 13 | 19 |
| 58–77 | 0x3A–0x4D | Character abilities | Section 14 | 20 |
| 78–82 | 0x4E–0x52 | Party abilities | Section 15 | 5 |
| 83–91 | 0x53–0x5B | GF abilities | Section 16 | 9 |
| 92–115 | 0x5C–0x73 | Menu abilities | Section 17 | 24 |

**Total: 116 defined abilities (IDs 0–115), fitting in 128 bits (16 bytes).**

The junction abilities (IDs 1–19) are listed here for completeness, since these are NOT equippable in the Ability screen's command/ability slots — they control which stats can receive junctioned magic:

| Hex | Dec | Ability |
|-----|-----|---------|
| 0x01 | 1 | HP-J |
| 0x02 | 2 | Str-J |
| 0x03 | 3 | Vit-J |
| 0x04 | 4 | Mag-J |
| 0x05 | 5 | Spr-J |
| 0x06 | 6 | Spd-J |
| 0x07 | 7 | Eva-J |
| 0x08 | 8 | Hit-J |
| 0x09 | 9 | Luck-J |
| 0x0A | 10 | Elem-Atk-J |
| 0x0B | 11 | ST-Atk-J |
| 0x0C | 12 | Elem-Def-J |
| 0x0D | 13 | ST-Def-J |
| 0x0E | 14 | Elem-Def-Jx2 |
| 0x0F | 15 | Elem-Def-Jx4 |
| 0x10 | 16 | ST-Def-Jx2 |
| 0x11 | 17 | ST-Def-Jx4 |
| 0x12 | 18 | Ability×3 |
| 0x13 | 19 | Ability×4 |

The GF abilities (83–91) and Menu abilities (92–115) also appear in the Ability screen under the GF and Menu categories but are not equippable in the character's command/ability slots:

**GF abilities:** 83=SumMag+10%, 84=SumMag+20%, 85=SumMag+30%, 86=SumMag+40%, 87=GFHP+10%, 88=GFHP+20%, 89=GFHP+30%, 90=GFHP+40%, 91=Boost

**Menu abilities:** 92=Haggle, 93=Sell-High, 94=Familiar, 95=Call Shop, 96=Junk Shop, 97=T Mag-RF, 98=I Mag-RF, 99=F Mag-RF, 100=L Mag-RF, 101=Time Mag-RF, 102=ST Mag-RF, 103=Supt Mag-RF, 104=Forbid Mag-RF, 105=Recov Med-RF, 106=ST Med-RF, 107=Ammo-RF, 108=Tool-RF, 109=Forbid Med-RF, 110=GFRecov Med-RF, 111=GFAbl Med-RF, 112=Mid Mag-RF, 113=High Mag-RF, 114=Med LV Up, 115=Card Mod

**Confirmed from WikiSquare data:** 83=SumMag+10%, 87=GFHP+10%, 91=Boost, 97=T Mag-RF, 98=I Mag-RF, 99=F Mag-RF, 107=Ammo-RF, 112=Mid Mag-RF, 115=Card Mod.

---

## Practical implementation guidance

For your dinput8.dll TTS proxy mod, the most reliable approach to speak ability names on the Ability screen is:

**Build your own ability name lookup table** using the unified IDs above as a `const char* ability_names[116]` array. Index 0 = "None", index 20 = "Magic", index 42 = "Str plus 20 percent", etc.

**For the left panel (equipped abilities):** Read the character struct directly from the savemap at runtime. For commands, read 3 bytes at `char_base + 0x50` and translate (if they are Battle Command IDs, use a 39-entry command name table; if they are unified IDs, use the main lookup table). For passive abilities, read 4 bytes at `char_base + 0x54` — these are unified Junctionable Ability IDs you can look up directly.

**For the right panel (available abilities list):** Reconstruct the list by iterating all junctioned GFs and their `completeAbilities` bitmaps. Filter by the relevant ability type range based on which section the cursor is in (commands: IDs 20–38; character/party abilities: IDs 39–82). Use the cursor offset from pMenuStateA as an index into your reconstructed list.

**Verify the `commands[3]` ID format** with a single test: equip Magic/GF/Draw on a character, then read the 3 bytes at character offset +0x50 in memory. If you see `02 03 04`, they're Battle Command IDs. If you see `14 15 16`, they're unified Junctionable Ability IDs.

**Key sources for further verification:** Clone the Doomtrain wiki repo (`git clone https://github.com/DarkShinryu/doomtrain.wiki.git`) and read `Junctionable-Abilities.md` for the authoritative master ID table. The Hyne save editor source at `github.com/myst6re/hyne` contains the definitive C++ struct definitions. The HobbitDur FF8ModdingWiki page at `technical-reference/list/junctionable-abilities/` has the most current documentation (updated February 2025).
