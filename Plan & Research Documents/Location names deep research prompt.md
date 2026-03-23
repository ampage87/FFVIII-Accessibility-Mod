# ChatGPT Deep Research Request: FF8 Location Name Extraction

## Context

In the FF8 PC Steam 2013 version, the save screen displays a human-readable location name for each save block (e.g., "B-Garden- Hall" for field ID 70). Our accessibility mod needs to speak these names via TTS when a blind user navigates save blocks. We currently have the numeric `location_id` from the save header and the internal field name (e.g., "crsanc1") from our field archive reader, but we need the actual display text the game shows to sighted users.

## What I Need

In the FF8 PC Steam 2013 version, the save screen displays a human-readable location name for each save block (e.g., "B-Garden- Hall" for field ID 70). I need to find where these location name strings are stored and how to extract them.

The game's menu data is in `menu.fi/fl/fs` archives (same fi/fl/fs two-level format as field archives). Inside this archive is `mngrp.bin` which contains menu text strings including location names.

I need:

1. **The exact structure of `mngrp.bin`** — how is it divided into sections? What is `mngrphd.bin` and how does it index into `mngrp.bin`?

2. **Which section of `mngrp.bin` or `tkmnmes` files contains the location name strings** that appear on the save screen?

3. **How does the `location_id` field from `savemap_ff8_header` (a field ID, e.g. 70) map to the index into the location name string table?** Is it a direct 1:1 mapping or is there an indirection table? The `location_id` is a field ID (0-899 range, matching the field.fl ordering where each field has 3 entries and fieldId = fl_index / 3).

4. **The text encoding used in these strings** — is it the same as sysfnt menu encoding (where 0x37=S, 0x4F=q, etc.), or the +0x20 offset variant seen in save file names (where 0x57=S, 0x6F=q), or standard ASCII, or something else?

5. **A complete list of all location names indexed by field ID**, if possible. There are ~900 fields total.

## Sources to Check

Please check these specific sources:

### Hyne Save Editor (github.com/myst6re/hyne)
- `SaveData.cpp` — how it reads and displays location names for save blocks
- `FF8Installation.cpp` — how it locates and reads game data files
- Any `Mngrp`-related files or location name string handling
- The save editor displays location names in its UI, so it must extract them somehow

### Deling Field Editor (github.com/myst6re/deling)
- This parses `mngrp.bin` for its text editor feature
- Look for how it reads the location/area name text section
- Check `MngrpArchive.cpp`, `MngrpArchive.h`, or similar files

### FF8 Modding Wiki (hobbitdur.github.io/FF8ModdingWiki)
- Documentation on the mngrp.bin format
- tkmnmes files documentation
- Menu archive structure

### FFRTT Wiki (wiki.ffrtt.ru)
- `FF8/Menu_mngrp` page
- `FF8/Menu_mngrp_complex_strings` page
- `FF8/Menu_tkmnmes` page
- Any pages about the save screen text rendering

### FF7-flat-wiki (ff7-mods.github.io/ff7-flat-wiki)
- FF8 menu format documentation

## Technical Context

Our mod already has:
- A working fi/fl/fs two-level archive reader (`field_archive.cpp`) that extracts files from `field.fi/fl/fs`
- LZSS decompression (standard FF8 variant: N=4096, F=18, THRESHOLD=2)
- A menu font decoder (`DecodeMenuText()`) with the 224-entry sysfnt glyph table
- Field name lookup by ID (`GetFieldNameById()`) returning internal names like "crsanc1"
- The `menu.fi/fl/fs` archive is at `Data/lang-en/` alongside `field.fi/fl/fs`

The ideal output is: working C code (or clear pseudocode) to read the `menu.fi/fl/fs` archive, find the location name section in `mngrp.bin`, and extract the display name string for a given field ID. If the mapping requires an indirection table (field ID → location name index), please document that table's format and location within the archive.

## What We Already Know

- Field ID 70 = internal name "crsanc1" = display name "B-Garden- Hall"
- The `savemap_ff8_header.location_id` is a uint16 field ID
- The save screen renders this as "B-Garden- Hall" using the game's menu font
- The location names must be stored somewhere in the menu archive since they appear on the menu/save screen
- `mngrp.bin` and `mngrphd.bin` are the main menu data containers
- `tkmnmes*.bin` files within mngrp contain various menu text sections
