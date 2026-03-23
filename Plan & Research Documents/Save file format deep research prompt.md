# ChatGPT Deep Research Request: FF8 Steam .ff8 Save File Format & Compression

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe + FFNx v1.23.x). The mod is a Win32 DLL (`dinput8.dll`) that adds screen reader TTS for blind players. I need to read save file headers to announce save block contents (character name, level, play time, gil, location) when navigating the save/load screen.

I have the save screen block cursor working — I know which block the user has selected. I have the `savemap_ff8_header` struct fully mapped from the FFNx source. I just need to **decompress the .ff8 file** to extract the header fields.

## What I Need

### Primary: The .ff8 file compression/encoding algorithm

The Steam 2013 edition stores save files as `.ff8` files in:
```
%USERPROFILE%\Documents\Square Enix\FINAL FANTASY VIII Steam\user_NNNNN\slotN_saveNN.ff8
```

These files are **compressed**. I need the exact decompression algorithm so I can extract the `savemap_ff8_header` from the first ~80 bytes of the decompressed data.

### Secondary: Location ID to location name mapping

The `savemap_ff8_header.location_id` field is a numeric field ID (e.g., 70 = B-Garden Hall). I need a mapping from field IDs to human-readable location names as displayed on the save screen. This data likely comes from `mngrp.bin` or the field string tables.

## What I Know About the .ff8 File Format

### File structure (from hex analysis of an actual save file)

- **Total file size**: 2106 bytes
- **First 4 bytes**: `36 08 00 00` = uint32 LE = 0x0836 = 2102. This equals (file_size - 4), so it's the payload length.
- **Bytes 4 onward**: Compressed save data (2102 bytes of payload)

### The decompressed savemap_ff8 struct (from FFNx source `src/ff8/save_data.h`)

```c
struct savemap_ff8 {
    uint16_t checksum;        // +0x00
    uint16_t fixed_value;     // +0x02  always 0x08FF
    savemap_ff8_header header; // +0x04  (76 bytes)
    // ... rest of save data (GFs, characters, items, etc.)
};

struct savemap_ff8_header {
    uint16_t location_id;       // +0x00
    uint16_t char1_curr_hp;     // +0x02
    uint16_t char1_max_hp;      // +0x04
    uint16_t save_count;        // +0x06
    uint32_t gil;               // +0x08
    uint32_t played_time_secs;  // +0x0C
    uint8_t  char1_lvl;         // +0x10
    uint8_t  char1_portrait;    // +0x11
    uint8_t  char2_portrait;    // +0x12
    uint8_t  char3_portrait;    // +0x13
    uint8_t  squall_name[12];   // +0x14  menu-font encoded
    uint8_t  rinoa_name[12];    // +0x20
    uint8_t  angelo_name[12];   // +0x2C
    uint8_t  boko_name[12];     // +0x38
    uint32_t curr_disk;         // +0x44
    uint32_t curr_save;         // +0x48
};
// Total header: 0x4C = 76 bytes
```

### Known decompressed values (from live memory scan at address 0x019CE018)

The in-memory savemap for this save file has these header values:
- checksum = 0x6F88
- fixed_value = 0x08FF
- location_id = 70 (0x0046)
- char1_curr_hp = 486 (0x01E6)
- char1_max_hp = 486 (0x01E6)
- save_count = 3
- gil = 5000 (0x1388)
- played_time_secs = 619 (0x026B) = 10 minutes 19 seconds
- char1_lvl = 7
- char1_portrait = 0xFF (Squall)
- char2_portrait = 0x00
- char3_portrait = 0xFF

### What I see in the compressed file

At raw file offset 0x0165, I can see the header data with extra bytes mixed in:

```
Raw bytes at offset 0x0160:
01 4E EA F1 88 6F FF FF 08 46 00 E6 01 E6 01 FF
03 00 88 13 00 00 6B 02 FF 00 00 07 FF 00 FF 57
6F EF 73 5F 6A 6A E8 F3 56 67 6C FB 6D 5F E7 F4
```

Matching against known decompressed values:
- `88 6F` at +0x0164 = checksum 0x6F88 ✓
- `FF FF 08` at +0x0166 = fixed value 0x08FF (the FF is doubled to FF FF)
- `46 00` at +0x0169 = location_id 0x0046 = 70 ✓
- `E6 01` at +0x016B = HP 0x01E6 = 486 ✓
- `E6 01` at +0x016D = max HP 486 ✓
- `FF 03 00` at +0x016F = save_count 3 (FF appears before 03)
- `88 13 00 00` at +0x0172 = gil 0x1388 = 5000 ✓
- `6B 02 FF 00 00` at +0x0176 = play_time? 0x026B = 619 ✓ (FF appears before 00 00)
- `07` at +0x017B = level 7 ✓
- `FF 00 FF` at +0x017C = portraits? (FF bytes mixed in)

The `FF` bytes appear at specific positions. `FF FF` encodes literal `0xFF`. But `FF` also appears before non-FF data bytes, which suggests this is NOT simple byte-level escaping.

### Compression candidates

1. **LZS (Lempel-Ziv-Stac)**: FF8 uses LZS extensively for other data (field archives, etc.). FFNx has `lzs_uncompress` function. However, LZS is a bitstream format, not byte-aligned.

2. **Custom FF8 PC save encoding**: The PC port may use a different encoding than the PSX version. The PSX saves on memory card blocks, while PC writes to disk files.

3. **FF-escape with zero run-length**: One theory is that `FF XX` where `XX != FF` means "insert XX zero bytes" while `FF FF` means literal `0xFF`. I tested this but it produced garbage.

## Source Code References to Check

### Hyne Save Editor (github.com/myst6re/hyne)
This is the definitive FF8 save editor by myst6re. It reads ALL save formats including Steam .ff8 files. Key files to check:
- `SavecardData.cpp` — file reading and format detection
- `SavecardData.h` — format type enum and reader declarations  
- `SaveData.cpp` / `SaveData.h` — individual save data parsing
- Any LZS or compression utility files
- Look for how it handles `Type::Ff8` or `Type::Slot` or similar Steam format

### FFNx (github.com/julianxhokaxhiu/FFNx)
- `src/ff8/save_data.h` — struct definitions (I already have these)
- `src/ff8_data.cpp` — has `sm_pc_read` function resolution and `savemap` pointer
- `src/metadata.cpp` — Steam save file path handling, hash calculation
- Any LZS decompression code

### FF8 Modding Wiki (hobbitdur.github.io/FF8ModdingWiki)
- Game Save Format page: `technical-reference/miscellaneous/game-save-format`
- Any documentation on PC save file encoding vs PSX save format

### OpenVIII (github.com/MaKiPL/OpenVIII-monogame)
- C# reimplementation that reads save files
- Search for save/load related classes

### FFRTT Wiki (wiki.ffrtt.ru)
- FF8 save format documentation

## What I Need You to Find

1. **The exact decompression algorithm for .ff8 files.** Ideally as C/C++ pseudocode I can implement directly. Must handle:
   - The 4-byte size header
   - Whatever compression/encoding is used on the payload
   - Producing the raw `savemap_ff8` struct as output

2. **Whether the header (first 80 bytes) can be extracted WITHOUT fully decompressing the entire file.** If the compression is streaming (like LZS), I may be able to decompress just the first 80 bytes and stop, which is more efficient.

3. **The location ID to name mapping.** Either:
   - A lookup table mapping field IDs to location names
   - Documentation on where these strings live in the game files (mngrp.bin, etc.)
   - How the save screen itself resolves location_id to a display string

4. **Portrait ID mapping.** The header has `char1_portrait`, `char2_portrait`, `char3_portrait` (uint8 each). What are the portrait ID values for each character? (e.g., 0=Squall, 1=Zell, etc. or 0xFF=empty slot?)

## Target Platform
- FF8_EN.exe: Steam 2013 release, App ID 39150
- Win32 DLL (dinput8.dll proxy), C++, MSVC compiler
- I need a C/C++ implementation, not Python or C#

## Attached Reference

I have a copy of the actual save file (`slot1_save01.ff8`, 2106 bytes) that I've analyzed above. The known decompressed values from the live memory scan provide ground truth for verifying any decompression algorithm.
