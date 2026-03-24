# Deep Research Prompt: FF8 Junction Ability Screen — Right Panel Available Abilities List

## Project Context

I am building an accessibility mod for Final Fantasy VIII (PC Steam 2013 edition, App ID 39150, FF8_EN.exe + FFNx v1.23.x renderer). The mod is a Win32 DLL (`dinput8.dll`) that provides TTS (text-to-speech) for blind players. I am implementing TTS for the Junction menu's **Ability screen**.

## What I Already Have Working

- **Left panel TTS**: COMPLETE. Reads equipped ability IDs directly from the savemap character struct. Commands at `chr+0x50` (3 bytes), passive abilities at `chr+0x54` (4 bytes). Uses unified ability ID → name lookup table (116 entries, IDs 0–115). Cursor at pMenuStateA+0x27C, focus state = 24. Works perfectly.

- **ABILITY_NAMES[116] table**: COMPLETE. All unified IDs mapped: 0=None, 1-19=Junction, 20-38=Command, 39-57=Stat%, 58-77=Character, 78-82=Party, 83-91=GF, 92-115=Menu. Confirmed via runtime testing.

- **Panel detection**: CONFIRMED. Left panel = focus 24, cursor +0x27C. Right panel = focus 28, cursor +0x271.

## What I Need

I need to identify the **ability at the right panel cursor position** so I can announce it by name. The right panel shows a list of available abilities from the character's junctioned GFs that can be equipped in the currently selected left-panel slot.

### The Specific Problem

The right panel cursor lives at pMenuStateA+0x271 and increments as the user navigates (0, 1, 2, 3...). I need to map `cursor position → ability ID` so I can look up the name in my ABILITY_NAMES table.

I tried reading the GCW (get_character_width) text buffer to get the help text description, but it lags 1-2 frames behind cursor movement and frequently announces the wrong ability. **I need a direct ability ID read, not rendered text.**

### What I Need You to Find

#### 1. Runtime ability list location

When the Junction Ability screen is open and the user is browsing the right panel, the engine must have an array somewhere in memory that contains the list of ability IDs shown on screen, in display order. **Where is this array?**

Possible locations:
- Somewhere within the pMenuStateA region (base address 0x01D76A9A at runtime, ~4KB scanned already — no ability list found in the first 4KB)
- A separate heap allocation
- A static buffer at a fixed address in FF8_EN.exe's data segment
- Inside a menu controller struct referenced from the menu_callbacks dispatch table

I scanned the first 4KB of pMenuStateA for runs of 3+ consecutive valid ability IDs (1-115) and found NO match for the expected right panel contents (e.g., Magic=0x14, GF=0x15, Draw=0x16, Item=0x17 in sequence). The list is NOT in pMenuStateA+0x000 through +0xFFF.

#### 2. If no runtime list exists: how to reconstruct it

If the engine generates the list on-the-fly from GF bitmaps without storing it in a persistent array, I need to know:

a) **Exactly which ability ID range is shown** based on which left-panel slot type is selected:
   - When a COMMAND slot is selected (left cursor 0-2), does the right panel show IDs 20-38 (command abilities)?
   - When an ABILITY slot is selected (left cursor 3-6), does it show IDs 39-82 (stat%/character/party)?
   - Or does it show ALL learned abilities regardless of slot type?

b) **The exact display order** of abilities in the right panel:
   - Are they sorted by unified ability ID (ascending)?
   - Are they sorted by GF index (Quezacotl's abilities first, then Shiva's, etc.)?
   - Are they in kernel.bin section order within each GF?
   - Is there deduplication (e.g., if both Quezacotl and Shiva have Magic learned, does Magic appear once or twice)?
   - Are already-equipped abilities excluded from the list, or shown with a marker?

c) **How empty slots work**: If the right panel has more visual rows than available abilities, are the extra rows padded with 0x00? 0xFF? Or does the list end and the cursor wraps?

#### 3. GF completeAbilities bitmap mechanics (confirmation needed)

I believe the following is correct based on prior research, but need confirmation:

- `GF struct + 0x14`: 16 bytes (128 bits) = `completeAbilities` bitmap
- Bit N set = ability with unified ID N is fully learned by this GF
- To check if a GF knows Magic (ID 20): `completeAbilities[20/8]` bit `(20 % 8)` → `completeAbilities[2]` bit 4
- The bitmap uses little-endian bit ordering within each byte (bit 0 = LSB)

If this is correct, I can reconstruct the right panel list by:
1. Reading the character's GF bitmask at `chr+0x58` (uint16, bit N = GF N junctioned)
2. For each junctioned GF, reading `completeAbilities[16]` at `GF_base + 0x14`
3. Union all set bits across junctioned GFs
4. Filter to the relevant ID range for the selected slot type
5. Sort in display order
6. Index by cursor position

**But I need to know step 4 (which IDs per slot type) and step 5 (sort order) to make this work.**

#### 4. Menu controller struct / Junction Ability function addresses

If you can identify the function that builds/renders the right panel list, that would help me find the list in memory. Relevant known addresses:

- `pMenuStateA`: 0x01D76A9A (runtime)
- `menu_callbacks` dispatch table: 0x00B87ED8
- Junction menu callback index: unknown (not one of the publicly confirmed indices 2,7,8,11,12,16,27)
- The Junction subsystem activates when `pMenuStateA+0x1E8 == 17`
- The Ability sub-screen uses focus values 20-28 at `pMenuStateA+0x22E`

Any known addresses for:
- The Junction menu controller function
- The Ability screen renderer
- The function that populates the "available abilities" list
- Any static buffer used by the Ability screen

## Known Memory Layout

### Savemap (all offsets CONFIRMED at runtime)

**CRITICAL: Savemap header is 76 bytes (0x4C), NOT 96 bytes (0x60). Subtract 0x14 from any research sources that assume 96-byte header.**

```
Savemap base:         0x1CFDC5C
Header:               +0x00 (76 bytes, 0x4C)
GFs:                  +0x4C (16 × 0x44 = 1088 bytes, base = 0x1CFDCA8)
Characters:           +0x48C (8 × 0x98 = 1216 bytes, base = 0x1CFE0E8)
Party formation:      +0xAF0 (4 bytes: char indices 0-7, 0xFF=empty)
```

### GF struct (0x44 = 68 bytes each)
```
+0x00: name[12]              FF8-encoded name
+0x0C: exp                   uint32
+0x10: unk1                  uint8
+0x11: exists                uint8, non-zero = obtained
+0x12: HPs                   uint16
+0x14: completeAbilities[16] 128-bit bitmap, bit N = unified ability ID N learned
+0x24: APs[24]               per-slot AP accumulation (22 used + 2 pad)
+0x3C: kills                 uint16
+0x3E: KOs                   uint16
+0x40: learning               uint8, slot index currently being learned
+0x41: forgotten[3]           3 bytes
```

### Character struct (0x98 = 152 bytes each)
```
+0x00: current_hp     uint16
+0x02: max_hp         uint16
+0x04: exp            uint32
+0x50: commands[3]    3 × uint8, equipped command ability IDs (unified IDs: 0x14=Magic, 0x15=GF, 0x16=Draw)
+0x54: abilities[4]   4 × uint8, equipped character/party ability IDs (unified IDs)
+0x58: gfs            uint16, bitmask of junctioned GFs (bit 0=Quezacotl, bit 1=Shiva, etc.)
```

### pMenuStateA offsets for Ability screen
```
+0x22E: focus state (24=left panel, 28=right panel; 20-23,25-27=transitions)
+0x27C: left panel cursor (0=first command slot below Attack, 1-2=more commands, 3-6=passive ability slots)
+0x271: right panel cursor (0-based index into available abilities list)
```

### Unified Ability ID ranges
```
0       = None/Empty
1-19    = Junction abilities (HP-J, Str-J, etc.) — not shown in Ability screen
20-38   = Command abilities (Magic, GF, Draw, Item, Card, Doom, etc.)
39-57   = Stat% abilities (HP+20%, Str+20%, etc.)
58-77   = Character abilities (Mug, Counter, Cover, etc.)
78-82   = Party abilities (Alert, Move-Find, Enc-Half, etc.)
83-91   = GF abilities (SumMag+10%, GFHP+10%, Boost)
92-115  = Menu abilities (Haggle, T Mag-RF, Card Mod, etc.)
```

## What I Tried That Didn't Work

1. **GCW help text parsing**: Read the rendered help bar text from the GCW buffer. The text lags 1-2 frames behind cursor movement, causing the wrong ability description to be announced. Not reliable enough.

2. **4KB pMenuStateA scan**: Searched bytes 0x000-0xFFF of pMenuStateA for runs of 3+ valid ability IDs (1-115). Found NO match for the expected ability list (Magic/GF/Draw/Item = 0x14/0x15/0x16/0x17). The list is not stored in the first 4KB of pMenuStateA.

## Platform Details

- FF8 Steam 2013 (App ID 39150), English, FF8_EN.exe
- FFNx v1.23.x renderer (github.com/julianxhokaxhiu/FFNx)
- 32-bit x86, Win32
- My mod is a `dinput8.dll` DLL injected alongside FFNx
- I have full memory read access at runtime

## Ideal Output

1. **The address of the right panel ability list** (or how to compute it), with the format of each entry (byte? word? struct?)
2. **If no runtime list**: the exact reconstruction algorithm including filtering rules and sort order
3. **Confirmation of the GF bitmap mechanics** (bit ordering, byte ordering within the 16-byte field)
4. **Any relevant function addresses** in FF8_EN.exe that handle the Ability screen

## References

- Qhimm Wiki FF8 savemap documentation
- HobbitDur FF8ModdingWiki (hobbitdur.github.io/FF8ModdingWiki) — kernel.bin sections
- Doomtrain wiki (github.com/DarkShinryu/doomtrain.wiki.git) — Junctionable-Abilities.md
- Hyne save editor source (github.com/myst6re/hyne) — C++ struct definitions
- FFNx source (github.com/julianxhokaxhiu/FFNx) — src/ff8/save_data.h
