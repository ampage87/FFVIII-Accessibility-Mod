# Deep Research Request: FF8 World Map Location Entry Trigger Coordinates

## Context
I am building an accessibility mod for FF8 (Steam 2013, FF8_EN.exe). I have the Ragnarok autopilot coordinates for 26 major locations, but there are additional walkable locations (like Fire Cavern on Balamb Island) that are NOT in the Ragnarok autopilot set. The deep research has confirmed that town entry trigger coordinates are HARDCODED in the engine executable, not stored in data files.

## What I Need
Find the exact world map X, Y coordinates for ALL world map entry trigger zones in FF8. These are the coordinates where walking close enough triggers a transition from the world map (mode 2) to a field (mode 1).

## Known Information

### Key addresses (FF8_EN.exe, Steam 2013 en-US, base address 0x00400000)
- World map position: X at `+1C3EE80`, Y at `+1C3EE84` (DWORD each)
- World map heading: `+1C3ED02` (WORD, 0-4095)
- Game mode: MODE_WORLDMAP = 2, MODE_FIELD = 1
- Location name popup function: `world_dialog_assign_text_sub_543790` (0x00543790)
- World map main processing: `worldmap_input_update_sub_559240` (0x00559240)
- wmset section pointers: resolved by `worldmap_wmset_set_pointers_sub_542DA0` (0x00542DA0)

### Known Ragnarok autopilot coordinates (confirmed working)
These are the coordinates used by the in-game Ragnarok autopilot system. They mark the approximate center of each location on the world map. The actual walking entrance trigger zones are offset from these by ~300-800 units.

| Location | X | Y |
|----------|-------|--------|
| Balamb Garden | 24576 | -29406 |
| Balamb | 13249 | -26779 |
| Dollet | -15639 | -39437 |
| Timber | -22564 | -4867 |
| Deling City | -61806 | -28649 |
| Fire Cavern | ??? | ??? |
| Obel Lake | ??? | ??? |
| Seaside Station | ??? | ??? |

### What the trigger function probably does
1. Each frame on the world map, checks player position against a table of (X, Y, radius, destFieldID) entries
2. When player is within radius of a trigger point, initiates field transition
3. The destination field IDs would map to wm2field.tbl entries

### Where to look
1. **world_dialog_assign_text_sub_543790** (0x543790) — shows location name popups. Must check player proximity to locations. The coordinate table it references should contain the trigger positions.
2. **The world map update loop** near 0x559240 — the main world map processing function, which calls the trigger checking code each frame.
3. **wm2field.tbl** in main.fs — maps world map events to field IDs. Format: 24 bytes per entry with two 12-byte scenarios (int16 X, Y, triangleID, uint16 fieldID, uint8 direction, uint8[3] padding). But these may be field-side destination coordinates, not world map trigger coordinates.
4. Search the .text section (0x401000-0x5FFFFF) for patterns of int16/int32 coordinate pairs that match known locations.

### Locations I need coordinates for (not in Ragnarok autopilot set)
- **Fire Cavern** (Balamb Island, east of Balamb Garden) — field dungeon entrance
- **Obel Lake** (Galbadia continent, side quest location)
- **Seaside Station** / **Balamb Station** (train stations)
- **Kashkabald Desert** draw points area
- Any other enterable locations on the world map not listed in the 26 Ragnarok destinations

### Savemap offset correction
All ChatGPT/community deep research assumes a 96-byte (0x60) savemap header. Confirmed header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets.

## Ideal Output
A complete table of world map entry trigger coordinates: X, Y (in world map coordinate system, matching the values at +1C3EE80/84), trigger radius, and destination field ID for every enterable location on the world map.

If the trigger table cannot be found in the executable, please describe:
1. The function address and disassembly of the trigger checking code
2. How the engine determines proximity to locations
3. Any alternative approach to extract these coordinates
