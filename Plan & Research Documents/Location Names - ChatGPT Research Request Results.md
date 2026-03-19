# How FF8 PC stores and retrieves save screen location names

**The save screen's location name is NOT derived from the field ID.** The save file preview header at offset 0x0004 stores a separate **SETPLACE location_id** (range 0–250), set by field script opcode 0x133. This index directly references ~251 location name strings stored in the menu data. The user's example — field ID 70 = crsanc1 = "B-Garden- Hall" — works because the SETPLACE index for that field *happens* to also be 70, but this is not a general rule. There are ~900 fields but only ~251 location names, so the mapping is many-to-one and encoded in each field's initialization script, not in a static lookup table.

The location name strings themselves are stored in **areames.dc1**, a separate file in the `menu.fi/fl/fs` archive alongside `mngrp.bin`. The strings use FF8's standard custom character encoding — the same encoding used across all menu text, where byte values are offset +0x20 from the sysfnt glyph table indices.

---

## The save preview header is not what it seems

The FF8 save format contains two distinct location-related fields that are easily confused:

| Save offset | Size | Field | Range | Description |
|---|---|---|---|---|
| **0x0004** | uint16 | Preview: Location ID | 0–250 | **SETPLACE index** — the location name to display on the save screen |
| **0x0D52** | uint16 | Current field | 0–899+ | **Field ID** — which field map is loaded, matches field.fl ordering |

The `savemap_ff8_header.location_id` the user is reading at offset 0x0004 is **not a field ID**. It's a location name index set by the `SETPLACE` opcode (0x133) during field initialization. The reason field ID 70 (crsanc1) and location name index 70 ("B-Garden- Hall") happen to match is coincidental for this particular field — many other fields will have completely different mappings. For example, dozens of individual Balamb Garden field screens (with field IDs spanning a wide range) all share location ID 70 ("B-Garden- Hall") or 80 ("B-Garden- Hallway") depending on which room they represent.

The **SETPLACE** opcode works as follows: each field's JSM initialization script pushes a location name index onto the stack and calls opcode 0x133, which writes that index into the game's save preview buffer. When the player saves, this value is persisted at offset 0x0004. The save/load screen then reads this uint16 and uses it directly as an index into the location name string table. No indirection table or field-ID-to-name mapping exists in the game data files.

---

## Where the location name strings live

The ~251 location name strings are stored in **areames.dc1**, which is a **separate file** within the `menu.fi/fl/fs` archive — it is not a section embedded inside mngrp.bin, though both reside in the same archive. The ff7-flat-wiki and FFRTT wiki categorize areames.dc1 under "Other files" alongside mag*.tex and *.sp1/sp2, distinct from the "text menu" files (mngrp.bin, tkmnmes).

On the **2013 Steam PC edition**, the path is `Data/lang-en/menu.fi` / `menu.fl` / `menu.fs`. Within this FIFLFS archive, `areames.dc1` appears as its own indexed entry in `menu.fl`, with its offset and size recorded in `menu.fi` (12 bytes per entry: uint32 uncompressed_size, uint32 offset_in_fs, uint32 compression_flag). The file may be LZSS-compressed within menu.fs.

The areames.dc1 file follows the standard FF8 text file format: an array of **uint16 offset pointers** at the start, followed by **null-terminated FF8-encoded strings**. Each offset pointer gives the position of the corresponding location name string relative to the start of the string data area. The location ID from the save preview (0–250) indexes directly into this offset pointer array to locate the string.

---

## The structure of mngrp.bin and mngrphd.bin

**mngrphd.bin** is a flat array of 8-byte entries, each defining one section within mngrp.bin:

```c
struct mngrphd_entry {
    uint32_t offset;  // byte offset into mngrp.bin
    uint32_t size;    // size of this section in bytes
};
// Total sections = file_size(mngrphd.bin) / 8
```

**mngrp.bin** is a large monolithic container concatenating dozens of sub-files. The known section types include:

- **tkmnmes*.bin** sections — menu text (ability descriptions, item names, GF info, status descriptions, and other UI strings), each following the tkmnmes format with a 16-padding header, string offset table, and encoded strings
- **m000–m004.bin / m000–m004.msg** — GF refine ability data (.bin) paired with their display text (.msg). Located at known offsets: m000.bin starts at **0x21F000**, m001.bin at **0x21F800**, m002.bin at **0x220000**, m003.bin at **0x220800**, m004.bin at **0x221000**
- **mngrp strings** — simple string data sections (documented at "Menu_mngrp_strings_locations")
- **mngrp complex strings** — formatted/textbox string data
- **mag*.tex** — magazine texture data
- **price.bin** — shop pricing data
- **shop.bin** — shop inventory data

The tkmnmes format (for menu text sections within mngrp.bin) uses this structure:

```c
struct tkmnmes_header {
    uint16_t pad_count;           // Always 16
    uint16_t paddings[pad_count]; // Offsets to string groups; 0x00 = skip. First usually 0x36
};
// Followed by:
struct tkmnmes_strings {
    uint16_t offset_count;
    uint16_t offsets[offset_count]; // Per-string offsets; 0x00 = skip
};
// String location = file_start + padding_value + string_offset_value
```

**areames.dc1 is NOT inside mngrp.bin** — it's a peer file in the menu archive. The "mngrp strings locations" section documented on the wikis refers to simple string blocks *within* mngrp.bin used for other menu UI text, not location names.

---

## Text encoding: the +0x20 sysfnt variant

All FF8 menu text — including location names in areames.dc1, strings in mngrp.bin, and character names in save data — uses the **same custom encoding**. This encoding maps byte values to characters via the sysfnt glyph texture, with a **+0x20 offset** from the raw glyph index:

**`character = sysfnt_glyph_table[byte_value - 0x20]`**

The key ranges for English text:

| Byte range | Characters |
|---|---|
| 0x00 | Null terminator (end of string) |
| 0x02 | Newline |
| 0x03–0x0C | Control codes (name insertion, icons, color, etc.) |
| 0x20 | Space |
| 0x21–0x2A | Digits '0'–'9' |
| 0x2B–0x36 | Punctuation/symbols |
| 0x37–0x50 | Uppercase 'A'–'Z' |
| 0x51–0x6A | Lowercase 'a'–'z' |
| 0x6B+ | Accented characters, extended punctuation |

This matches the user's "+0x20 offset variant" where **0x57 = 'S'** and **0x6F = 'q'**. The raw sysfnt glyph table indices (where 0x37 = 'S', 0x4F = 'q') must be adjusted: `string_byte = glyph_index + 0x20`. The FFRTT wiki confirms this: "Some of which aligns to the grid of text in the texture if you -32 from the value."

So to decode a location name string from areames.dc1, the user should apply the **same decoder they already use for save file names** (the +0x20 offset variant), since both sources use identical encoding.

---

## The complete location name table (IDs 0–250)

The SETPLACE location IDs map to these display names (sourced from ff8-speedruns/ff8-memory, confirmed against game data):

| ID | Location Name | ID | Location Name |
|---|---|---|---|
| 0 | Unknown | 64 | B-Garden - Library |
| 1 | Balamb - Alcauld Plains | 65 | B-Garden - Front Gate |
| 2 | Balamb - Gaulg Mountains | 66 | B-Garden - Classroom |
| 3 | Balamb - Rinaul Coast | 67 | B-Garden - Cafeteria |
| 4 | Balamb - Raha Cape | 68 | B-Garden - MD Level |
| 5–10 | Timber region areas | 69 | B-Garden - 2F Hallway |
| 11–14 | Dollet region areas | **70** | **B-Garden - Hall** |
| 15–23 | Galbadia region areas | 71 | B-Garden - Infirmary |
| 24–25 | Winhill region areas | 72–73 | B-Garden - Dormitory |
| 26–37 | Trabia region areas | 74 | B-Garden - Headmaster's Office |
| 38–53 | Esthar region areas | 75–82 | B-Garden - other rooms |
| 54–63 | Centra region areas | 83–88 | Balamb town locations |
| 89 | Train | 146–158 | Fishermans Horizon areas |
| 90 | Car | 159–168 | Esthar City interiors |
| 91 | Inside Ship | 170–176 | Lunar Gate / Sorceress Memorial |
| 92 | Fire Cavern | 177–182 | Tears' Point / Lunatic Pandora |
| 93–100 | Dollet locations | 183–189 | Edea's House / Centra Ruins |
| 101–110 | Timber locations | 190–196 | Trabia Garden |
| 111–125 | G-Garden locations | 197–204 | Shumi Village / Trabia Canyon |
| 126–134 | Deling City locations | 205–214 | White SeeD Ship / Ragnarok |
| 135 | Galbadia D-District Prison | 215–218 | Deep Sea Research Center |
| 136 | Desert | 219–226 | Lunar Base / Outer Space |
| 137 | Galbadia Missile Base | 227–228 | Chocobo Forest / Wilderness |
| 138–145 | Winhill / Tomb | 229–246 | Ultimecia Castle rooms |
| | | 248–250 | Ultimecia Castle / Commencement Room / Queen of Cards |

IDs 29, 34, 169, and 247 are unused/skipped. ID 0xFFFF (−1 as signed) represents "???".

---

## Pseudocode to extract a location name from a save file

```c
// Step 1: Read the SETPLACE location_id from the save preview header
// (This is at offset 0x0004 within the save block, NOT the field ID at 0x0D52)
uint16_t location_id = read_uint16_le(save_data + 0x0004);

// Step 2: Locate areames.dc1 in the menu archive
// Parse menu.fi/fl/fs to find areames.dc1 entry
// Decompress if LZSS-compressed (check compression flag in menu.fi)
uint8_t* areames_data = extract_file_from_menu_archive("areames.dc1");

// Step 3: Read the string offset table from areames.dc1
// areames.dc1 starts with an array of uint16 offsets
// (The exact header format follows FF8 standard text file conventions)
// Number of entries can be determined from the first offset value
// or from the file structure

// Standard FF8 text file: offsets relative to start of string data
uint16_t num_offsets = /* determine from file structure */;
uint16_t* offsets = (uint16_t*)(areames_data);
uint16_t string_start = offsets[location_id];

// Step 4: Read the null-terminated string
uint8_t* raw_string = areames_data + string_start;

// Step 5: Decode using FF8 string encoding (+0x20 offset from sysfnt)
char decoded[256];
int out = 0;
for (int i = 0; raw_string[i] != 0x00; i++) {
    uint8_t b = raw_string[i];
    if (b < 0x20) {
        // Control code - handle or skip
        if (b == 0x02) decoded[out++] = '\n';
        else if (b >= 0x03 && b <= 0x0C) {
            i++; // Skip parameter byte(s)
            if (b == 0x0B && /* multi-select */) i++; // Extra byte
        }
    } else {
        // Printable character: use sysfnt glyph table
        decoded[out++] = sysfnt_glyph_table[b - 0x20];
    }
}
decoded[out] = '\0';
// decoded now contains e.g. "B-Garden- Hall"
```

---

## Why field ID ≠ location name index, and what to do about it

The core architectural insight is that FF8 uses a **two-level indirection** for location names on the save screen:

1. **Field script → SETPLACE**: Each field's JSM initialization script calls SETPLACE (opcode 0x133) with a hardcoded location name index. Field crsanc1 calls `SETPLACE 70`, field bghall_2 might also call `SETPLACE 70`, and field bggate_1 calls `SETPLACE 65`.

2. **SETPLACE → save preview**: The engine writes this index to the save preview buffer at offset 0x0004.

3. **Save preview → areames.dc1**: The save screen reads offset 0x0004 and indexes into areames.dc1 for the display string.

**There is no static field_id → location_name_id table** in the game data. The mapping is embedded in ~900 individual JSM scripts. To build one programmatically, you would need to parse every field's JSM binary for the SETPLACE opcode. The Hyne save editor sidesteps this by using a **hardcoded list** of ~251 location names compiled directly into the binary (via `Data::locations()`) rather than reading from game data at runtime.

For the user's mod, the correct approach is to **read the uint16 at save offset 0x0004** — this is already the location name index, not a field ID. If the mod needs to display a location name for a *field ID* (from offset 0x0D52 or from the field archive), it would need either a precomputed lookup table built by scanning all JSM scripts, or a reverse mapping from the game's own field script data.