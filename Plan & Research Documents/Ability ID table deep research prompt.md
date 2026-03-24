# Deep Research Prompt: FF8 Ability ID Tables and Junction Ability Screen Memory Layout

## Project Context

I am building an accessibility mod for Final Fantasy VIII (PC Steam 2013 edition, FF8_EN.exe + FFNx v1.23.x renderer). The mod provides TTS (text-to-speech) for blind players. I am currently implementing TTS for the Junction menu's **Ability screen**, which lets the player equip command abilities (Magic, GF, Draw, Item, etc.) and character/party abilities (HP+20%, Str+20%, Cover, Move/Find, etc.) to a character.

## What I Need

I need **four specific things** to implement this feature:

### 1. Complete Command Ability ID → Name Table

In the savemap, each character has a `commands[3]` array (3 bytes at character struct offset +0x50). Each byte is a **command ability ID**. The first visible slot on screen is always "Attack" (hardcoded by the engine, not stored in `commands[]`). The `commands[3]` array stores the 3 additional command slots.

**I need the exact 1-byte ID for every command ability in the game.** These are abilities like:
- Magic (I believe this is ID 0x02)
- GF (I believe this is ID 0x03)
- Draw (I believe this is ID 0x04)
- Item (I believe this is ID 0x05)
- Card
- Doom
- Mad Rush
- Treatment
- Defend
- Darkside
- Recover
- Absorb
- Revive
- LV Down
- LV Up
- Kamikaze
- Devour
- MiniMog
- Etc.

**Also tell me what value represents "empty/none"** — is it 0x00, 0xFF, or something else?

Please provide the COMPLETE list in a format like:
```
0x00 = (none/empty)
0x01 = ???
0x02 = Magic
0x03 = GF
...
```

### 2. Complete Character/Party Ability ID → Name Table

In the savemap, each character has an `abilities[4]` array (4 bytes at character struct offset +0x54). Each byte is a **character or party ability ID**. These are passive abilities like:
- HP+20%
- HP+40%
- HP+80%
- Str+20%
- Str+40%
- Str+60%
- Vit+20%
- Vit+40%
- Vit+60%
- Mag+20%
- Mag+40%
- Mag+60%
- Spr+20%
- Spr+40%
- Spr+60%
- Spd+20%
- Spd+40%
- Eva+30%
- Luck+50%
- Cover
- Move/Find
- Enc-Half
- Enc-None
- Rare Item
- HP Bonus
- Str Bonus
- Vit Bonus
- Mag Bonus
- Spr Bonus
- Auto-Haste
- Auto-Protect
- Auto-Shell
- Auto-Reflect
- Auto-Potion
- Expendx2-1
- Expendx3-1
- Ribbon
- Alert
- Move-HP Up
- Mug
- Med Data
- Counter
- Return Damage
- Initiative
- Etc.

**I need the exact 1-byte ID for every character and party ability.** These IDs are stored in `abilities[4]` in the savemap character struct.

**Also tell me what value represents "empty/none"** — is it 0x00, 0xFF, or something else?

Please provide the COMPLETE list with IDs.

### 3. How the Junction Ability Screen's "Available Abilities" List Works in Memory

The Junction Ability screen has two panels:
- **Left panel**: Shows the character's currently equipped command/ability slots (e.g., Attack, Magic, GF, Draw in the COMMAND section; and equipped character abilities in the ABILITY section)
- **Right panel**: Shows a list of available abilities that can be equipped in the selected slot

When the player selects a slot on the left, the right panel populates with abilities available from the character's junctioned GFs.

**I need to know:**

a) **Where is the "available abilities" list stored in memory at runtime?** Is there a runtime array/buffer that the engine builds when the player opens the Ability screen or selects a slot? If so, what is its base address (or how to find it), how many entries, and what is the format of each entry (just an ability ID byte? a struct with ID + flags?)?

b) **If no runtime list exists**, how does the engine determine which abilities to show? Does it iterate through the character's junctioned GFs, check each GF's `complete_abilities[16]` bitmap, and filter for the relevant ability type? If so, how does the bitmap work — see question 4.

c) **What does cursor offset +0x272 (from pMenuStateA) index into?** In the Junction Ability screen, when the cursor is on the right-side available abilities list, offset +0x272 tracks the cursor position. Does this index directly into a runtime array of ability IDs? Or is it a visual row index that I need to map to an ability ID through some other mechanism?

d) **What does cursor offset +0x271 (from pMenuStateA) represent on the left panel?** My diagnostic shows +0x271 tracking 0→1→2→3 as the user moves down through command slots. Is +0x271 = 0 the first command slot (the one right below Attack), and does it index into the character's `commands[3]` array? What about the lower ABILITY section — does +0x271 continue counting (e.g., 4→5→6→7 for the 4 ability slots), or is there a separate cursor offset for the ABILITY section?

### 4. GF Ability Bitmap → Ability ID Mapping

Each GF in the savemap has `complete_abilities[16]` — a 128-bit bitmap at GF struct offset +0x14. The FFNx source comment says "115+1 valid, 124 existing."

**I need to know:**
- How do bit positions in this bitmap map to ability IDs?
- Is bit 0 = some global ability ID, bit 1 = the next ID, etc.?
- Or does each GF have its own mapping (GF-specific ability indices)?
- The `APs[24]` array at GF struct offset +0x24 (22 used + 2 unused) — does each index correspond to a specific ability for that GF? How do these map to the bitmap bits and to ability IDs?
- Each GF can learn different abilities. Is there a table in kernel.bin that defines which abilities are available to each GF, and then `complete_abilities` tracks which ones are learned?

## Savemap Memory Layout Reference

These offsets are CONFIRMED correct for the runtime savemap at base address 0x1CFDC5C:

```
Savemap base:        0x1CFDC5C
Header:              +0x00 (76 bytes / 0x4C)
GFs:                 +0x4C (16 × 0x44 = 1088 bytes)
Characters:          +0x48C (8 × 0x98 = 1216 bytes)
```

### GF struct (0x44 bytes each, 16 GFs at savemap+0x4C):
```
+0x00: name[12]          (FF8-encoded name, 12 bytes)
+0x0C: exp               (uint32)
+0x10: unk1              (uint8)
+0x11: exists            (uint8, non-zero = GF obtained)
+0x12: HPs               (uint16)
+0x14: complete_abilities[16]  (128-bit bitmap, 16 bytes)
+0x24: APs[24]           (24 bytes, 22 used + 2 unused)
+0x3C: kills             (uint16)
+0x3E: KOs               (uint16)
+0x40: learning           (uint8, index of ability currently being learned)
+0x41: forgotten1-3       (3 bytes)
```

### Character struct (0x98 bytes each, 8 characters at savemap+0x48C):
```
+0x00: current_hp        (uint16)
+0x02: max_hp            (uint16)
+0x04: exp               (uint32)
+0x08: model_id          (uint8)
+0x09: weapon_id         (uint8)
+0x0A: str, vit, mag, spr, spd, lck  (6 × uint8)
+0x10: magics[32]        (32 × uint16 = 64 bytes, magic stock)
+0x50: commands[3]       (3 × uint8, equipped COMMAND ability IDs)
+0x53: padding           (1 byte)
+0x54: abilities[4]      (4 × uint8, equipped CHARACTER/PARTY ability IDs)
+0x58: gfs               (uint16, bitmask of junctioned GFs — bit 0 = Quezacotl, etc.)
```

### Character indices:
```
0 = Squall, 1 = Zell, 2 = Irvine, 3 = Quistis, 4 = Rinoa, 5 = Selphie
6 = Seifer, 7 = Edea
```

### GF indices (order in savemap):
```
0 = Quezacotl, 1 = Shiva, 2 = Ifrit, 3 = Siren, 4 = Brothers, 5 = Diablos
6 = Carbuncle, 7 = Leviathan, 8 = Pandemona, 9 = Cerberus, 10 = Alexander
11 = Doomtrain, 12 = Bahamut, 13 = Cactuar, 14 = Tonberry, 15 = Eden
```

## IMPORTANT: Savemap Offset Correction

Previous deep research results assumed the savemap header is 96 bytes (0x60). The CONFIRMED header size is 76 bytes (0x4C). This means offsets from older research sources that reference "savemap + offset" may be 0x14 (20 bytes) too high for post-header data. The offsets listed above in this document are already corrected and confirmed via runtime testing.

When referencing any external documentation about FF8 savemap offsets, **subtract 0x14 from post-header offsets** to get the correct runtime address.

## What I Will Use This Information For

I will build a static C++ lookup table mapping ability IDs to English name strings (e.g., `ABILITY_NAMES[0x02] = "Magic"`). When the player moves the cursor on the Ability screen, the mod will:

1. Read the cursor position from pMenuStateA offsets
2. Determine which ability ID is at that position (either from the character's equipped slots on the left, or from the available abilities list on the right)
3. Look up the ability name from the static table
4. Speak the name via TTS

## Source Material

- FF8 PC Steam 2013 edition (FF8_EN.exe)
- FFNx v1.23.x source code (particularly `src/ff8/save_data.h` for struct definitions)
- kernel.bin in the game's Data/lang-en directory contains ability data tables
- The Qhimm Wiki and FF8 Modding Wiki (hobbitdur.github.io/FF8ModdingWiki) are good references for kernel.bin section layouts

## Output Format

Please provide:
1. A numbered table of ALL command ability IDs (byte values) and their English names
2. A numbered table of ALL character/party ability IDs (byte values) and their English names
3. A clear explanation of how the available abilities list works in memory, with any known addresses or struct layouts
4. A clear explanation of how GF ability bitmaps map to ability IDs, including the kernel.bin table structure if relevant
5. If there is a unified "ability ID" numbering system that covers all ability types (command + character + party + junction + GF-specific), explain the full numbering scheme and ID ranges for each type
