# ChatGPT Deep Research Request: FF8 Field ID → Disambiguated Display Name Table

## Context

We're building an accessibility mod for Final Fantasy VIII (PC Steam 2013) that enables blind players to navigate the game via TTS. A key feature is the **field navigation entity catalog**, which announces exits like "Exit to B-Garden - Hall". The problem is that many different field screens (field IDs) share the same human-readable location name. For example, field IDs 165–178 are all different rooms within Balamb Garden's main hall, but they ALL map to the display name "B-Garden - Hall". A blind player hearing "Exit to B-Garden - Hall" from three different doorways has no way to distinguish them or build a mental map.

We need a **complete static C array** that maps every field ID (0–~982) to a **unique** display name string. For fields that share a name, we append a sequential number (e.g., "B-Garden - Hall 1", "B-Garden - Hall 2", etc.) so every field ID has a distinct, stable label. Fields with already-unique names get no number appended.

## Source Data

The ff8-speedruns/ff8-memory GitHub repository contains the exact mapping we need in `mapId.md`:

**https://github.com/ff8-speedruns/ff8-memory/blob/main/mapId.md**

This file has ~985 lines. Each line is a markdown table row with format:

```
| field_id | internal_name (Human-Readable Name) / setplace_id |
```

Examples:
```
| 102 | bccent_1 (Balamb - Town Square) / 85 |
| 165 | bghall_1 (B-Garden - Hall) / 70 |
| 166 | bghall1a (B-Garden - Hall) / 70 |
| 168 | bghall_2 (B-Garden - Hall) / 70 |
| 152 | bgbtl_1 (???) |
```

Some entries show `(???)` meaning no known location name — these are test maps, world maps, battle stages, or unused fields.

## What I Need

### 1. Parse the complete mapId.md table

Read every row from `mapId.md`. For each field ID, extract:
- The field ID (integer)
- The internal name (e.g., "bghall_1")
- The human-readable name (the text in parentheses, e.g., "B-Garden - Hall")
- Whether the name is "???" (unknown/unused)

### 2. Build disambiguated names

Group all field IDs by their human-readable name. For any name used by more than one field ID:
- Append " 1", " 2", " 3", etc. in **field ID order** (ascending). This ensures the numbering is stable and deterministic.
- Example: field IDs 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178 all have name "B-Garden - Hall". They become "B-Garden - Hall 1" through "B-Garden - Hall 14".

For names used by only one field ID, keep the name as-is (no number appended).

For "???" entries, use the internal field name directly (e.g., "wm00", "testbl0", "bgbtl_1").

### 3. Output as a C static array

Produce a C array definition like this:

```c
// Field ID → disambiguated display name.
// Generated from ff8-speedruns/ff8-memory mapId.md.
// Names shared by multiple field IDs have sequential numbers appended.
// "???" entries use the internal field name.
// Total entries: N (field IDs 0 through MAX)
static const char* FIELD_DISPLAY_NAMES[] = {
    "wm00",                          // 0: wm00 (???)
    "wm01",                          // 1: wm01 (???)
    // ... (all world map / test entries)
    "Balamb - Town Square 1",        // 102: bccent_1
    "Balamb - Town Square 2",        // 103: bccent1a
    // ...
    "B-Garden - Hall 1",             // 165: bghall_1
    "B-Garden - Hall 2",             // 166: bghall1a
    // ... etc through all ~983 entries
};
static const int FIELD_DISPLAY_NAMES_COUNT = N;
```

Requirements for the output:
- The array must be indexed by field ID (index 0 = field ID 0, etc.)
- Every entry must be a valid C string literal (escape any special characters like apostrophes if needed — actually apostrophes are fine in C strings, just watch for backslashes or quotes)
- Include a comment on each line showing the field ID and internal name for verification
- If there are gaps in the field ID sequence (e.g., no field 983 but there is 984), fill gaps with nullptr
- The array must cover field ID 0 through the highest field ID present in mapId.md

### 4. Summary statistics

Also provide:
- Total number of field IDs
- Number of unique human-readable names (excluding "???")
- Number of names that required disambiguation (i.e., appeared more than once)
- The top 10 most-duplicated names and their counts (e.g., "B-Garden - Hall" appears 14 times)

## Important Notes

- The numbering within each group MUST be by ascending field ID. This makes the mapping deterministic — "B-Garden - Hall 3" always means the same room regardless of when the mod loads.
- Do NOT skip or omit any field IDs present in mapId.md. Every row must appear in the output.
- The "???" entries are mostly world map chunks (wm00–wm71), test maps, battle stages, and a few unused fields. Using the internal name for these is fine since players will rarely or never encounter them.
- Some entries in mapId.md may have the setplace_id omitted (just the name in parentheses with no `/ N`). That's fine — we only need the human-readable name.

## How This Will Be Used

The output C array will be added to our mod's `menu_tts.cpp` or a new `field_names.h` header. It will be used by:

1. **Field navigation** — when announcing exits: "Exit to B-Garden - Hall 3"
2. **Save block TTS** — when announcing save data: "Block 1: Squall, Level 7, B-Garden - Hall, 0 hours 10 minutes" (save blocks use SETPLACE IDs which we already handle separately, but we may cross-reference)
3. **Field load announcements** — when entering a new field: "Now entering B-Garden - Cafeteria"

The key accessibility benefit is that blind players can build mental maps when each room has a unique, stable name.


---

# Research Results (Completed by Claude, March 17 2026)

## Data Source

Fetched and parsed the complete `mapId.md` from [ff8-speedruns/ff8-memory](https://github.com/ff8-speedruns/ff8-memory/blob/main/mapId.md) — 985 lines, 982 field ID entries (field IDs 0 through 981). No gaps in the sequence.

## Summary Statistics

- **Total field IDs in mapId.md:** 982
- **Field ID range:** 0 to 981
- **Unknown/??? entries:** 126 (world map chunks wm00-wm71, test maps, battle stages, unused fields)
- **Unique human-readable names (excluding ???):** 174
- **Names requiring disambiguation (appeared >1 time):** 129
- **Names that were already unique (no number appended):** 45

### Top 10 Most Duplicated Names

| Rank | Name | Count | Field ID Range |
|------|------|-------|---------------|
| 1 | Esthar - City | 61 | 402-487 |
| 2 | Centra - Excavation Site | 35 | 268-401 |
| 3 | Lunatic Pandora | 27 | 348-380 |
| 4 | Galbadia D -District Prison | 25 | 793-965 |
| 5 | B-Garden - MD Level | 20 | 194-213 |
| 6 | B-Garden - Hall | 14 | 165-178 |
| 7 | Galbadia Missile Base | 14 | 769-782 |
| 8 | Esthar - Odine's Laboratory | 13 | 434-531 |
| 9 | G-Garden - Hallway | 13 | 681-701 |
| 10 | Deling - Presidential Residence | 13 | 716-767 |

## Notes

- All disambiguation numbering is by ascending field ID order, making it deterministic.
- The source data has a typo: "Galbadia D -District Prison" (extra space before hyphen). This is preserved as-is from mapId.md for fidelity. You may want to fix it to "Galbadia D-District Prison" in the final mod.
- Field 969 has "Chain leading to Ultemecia Castle" (note: "Ultemecia" appears to be a typo for "Ultimecia" in mapId.md). You may want to correct this.
- No field ID gaps exist in the 0-981 range — every index has a valid entry, so no `nullptr` entries were needed.

## Complete C Static Array

The complete array has been saved to a separate header file `field_display_names.h` (65KB, 989 lines) in the outputs directory. It is also available below:

```c
// Field ID -> disambiguated display name.
// Generated from ff8-speedruns/ff8-memory mapId.md.
// Names shared by multiple field IDs have sequential numbers appended (ascending field ID order).
// "???" entries use the internal field name.
// Total entries: 982 (field IDs 0 through 981)
static const char* FIELD_DISPLAY_NAMES[] = {
    "wm00",  // 0: wm00 (???)
    "wm01",  // 1: wm01 (???)
    "wm02",  // 2: wm02 (???)
    "wm03",  // 3: wm03 (???)
    "wm04",  // 4: wm04 (???)
    "wm05",  // 5: wm05 (???)
    "wm06",  // 6: wm06 (???)
    "wm07",  // 7: wm07 (???)
    "wm08",  // 8: wm08 (???)
    "wm09",  // 9: wm09 (???)
    "wm10",  // 10: wm10 (???)
    "wm11",  // 11: wm11 (???)
    "wm12",  // 12: wm12 (???)
    "wm13",  // 13: wm13 (???)
    "wm14",  // 14: wm14 (???)
    "wm15",  // 15: wm15 (???)
    "wm16",  // 16: wm16 (???)
    "wm17",  // 17: wm17 (???)
    "wm18",  // 18: wm18 (???)
    "wm19",  // 19: wm19 (???)
    "wm20",  // 20: wm20 (???)
    "wm21",  // 21: wm21 (???)
    "wm22",  // 22: wm22 (???)
    "wm23",  // 23: wm23 (???)
    "wm24",  // 24: wm24 (???)
    "wm25",  // 25: wm25 (???)
    "wm26",  // 26: wm26 (???)
    "wm27",  // 27: wm27 (???)
    "wm28",  // 28: wm28 (???)
    "wm29",  // 29: wm29 (???)
    "wm30",  // 30: wm30 (???)
    "wm31",  // 31: wm31 (???)
    "wm32",  // 32: wm32 (???)
    "wm33",  // 33: wm33 (???)
    "wm34",  // 34: wm34 (???)
    "wm35",  // 35: wm35 (???)
    "wm36",  // 36: wm36 (???)
    "wm37",  // 37: wm37 (???)
    "wm38",  // 38: wm38 (???)
    "wm39",  // 39: wm39 (???)
    "wm40",  // 40: wm40 (???)
    "wm41",  // 41: wm41 (???)
    "wm42",  // 42: wm42 (???)
    "wm43",  // 43: wm43 (???)
    "wm44",  // 44: wm44 (???)
    "wm45",  // 45: wm45 (???)
    "wm46",  // 46: wm46 (???)
    "wm47",  // 47: wm47 (???)
    "wm48",  // 48: wm48 (???)
    "wm49",  // 49: wm49 (???)
    "wm50",  // 50: wm50 (???)
    "wm51",  // 51: wm51 (???)
    "wm52",  // 52: wm52 (???)
    "wm53",  // 53: wm53 (???)
    "wm54",  // 54: wm54 (???)
    "wm55",  // 55: wm55 (???)
    "wm56",  // 56: wm56 (???)
    "wm57",  // 57: wm57 (???)
    "wm58",  // 58: wm58 (???)
    "wm59",  // 59: wm59 (???)
    "wm60",  // 60: wm60 (???)
    "wm61",  // 61: wm61 (???)
    "wm62",  // 62: wm62 (???)
    "wm63",  // 63: wm63 (???)
    "wm64",  // 64: wm64 (???)
    "wm65",  // 65: wm65 (???)
    "wm66",  // 66: wm66 (???)
    "wm67",  // 67: wm67 (???)
    "wm68",  // 68: wm68 (???)
    "wm69",  // 69: wm69 (???)
    "wm70",  // 70: wm70 (???)
    "wm71",  // 71: wm71 (???)
    "testno",  // 72: testno (???)
    "start",  // 73: start (???)
    "Opening FMV",  // 74: start0 (Opening FMV)
    "Game Over Screen",  // 75: gover (Game Over Screen)
    "Ending FMV",  // 76: ending (Ending FMV)
    "test",  // 77: test (???)
    "test1",  // 78: test1 (???)
    "test2",  // 79: test2 (???)
    "test3",  // 80: test3 (???)
    "test4",  // 81: test4 (???)
    "test5",  // 82: test5 (???)
    "test6",  // 83: test6 (???)
    "test7",  // 84: test7 (???)
    "test8",  // 85: test8 (???)
    "test9",  // 86: test9 (???)
    "test13",  // 87: test13 (???)
    "test14",  // 88: test14 (???)
    "testbl0",  // 89: testbl0 (???)
    "testbl1",  // 90: testbl1 (???)
    "testbl2",  // 91: testbl2 (???)
    "testbl3",  // 92: testbl3 (???)
    "testbl4",  // 93: testbl4 (???)
    "testbl5",  // 94: testbl5 (???)
    "testbl6",  // 95: testbl6 (???)
    "testbl7",  // 96: testbl7 (???)
    "testbl8",  // 97: testbl8 (???)
    "Ultimecia Seal Menu",  // 98: testbl9 (Ultimecia Seal Menu)
    "testbl13",  // 99: testbl13 (???)
    "testbl14",  // 100: testbl14 (???)
    "testmv",  // 101: testmv (???)
    "Balamb - Town Square 1",  // 102: bccent_1 (Balamb - Town Square)
    "Balamb - Town Square 2",  // 103: bccent1a (Balamb - Town Square)
    "Balamb - Station Yard 1",  // 104: bcform_1 (Balamb - Station Yard)
    "Balamb - Station Yard 2",  // 105: bcform1a (Balamb - Station Yard)
    "Balamb - Town Square 3",  // 106: bcgate_1 (Balamb - Town Square)
    "Balamb - Town Square 4",  // 107: bcgate1a (Balamb - Town Square)
    "Balamb Hotel 1",  // 108: bchtl_1 (Balamb Hotel)
    "Balamb Hotel 2",  // 109: bchtl1a (Balamb Hotel)
    "Balamb Hotel 3",  // 110: bchtr_1 (Balamb Hotel)
    "Balamb Hotel 4",  // 111: bchtr1a (Balamb Hotel)
    "Balamb - Residence 1",  // 112: bcmin1_1 (Balamb - Residence)
    "Balamb - Residence 2",  // 113: bcmin11a (Balamb - Residence)
    "Balamb - The Dincht's 1",  // 114: bcmin2_1 (Balamb - The Dincht's)
    "Balamb - The Dincht's 2",  // 115: bcmin21a (Balamb - The Dincht's)
    "Balamb - The Dincht's 3",  // 116: bcmin2_2 (Balamb - The Dincht's)
    "Balamb - The Dincht's 4",  // 117: bcmin22a (Balamb - The Dincht's)
    "Balamb - The Dincht's 5",  // 118: bcmin2_3 (Balamb - The Dincht's)
    "Balamb - The Dincht's 6",  // 119: bcmin23a (Balamb - The Dincht's)
    "Balamb Harbor 1",  // 120: bcport_1 (Balamb Harbor)
    "Balamb Harbor 2",  // 121: bcport1a (Balamb Harbor)
    "Balamb Harbor 3",  // 122: bcport1b (Balamb Harbor)
    "Balamb Harbor 4",  // 123: bcport_2 (Balamb Harbor)
    "Balamb Harbor 5",  // 124: bcport2a (Balamb Harbor)
    "Balamb - Town Square 5",  // 125: bcsaka_1 (Balamb - Town Square)
    "Balamb Harbor 6",  // 126: bcsaka1a (Balamb Harbor)
    "Balamb - Town Square 6",  // 127: bcsta_1 (Balamb - Town Square)
    "Balamb - Town Square 7",  // 128: bcsta1a (Balamb - Town Square)
    "Fire Cavern 1",  // 129: bdenter1 (Fire Cavern)
    "Fire Cavern 2",  // 130: bdifrit1 (Fire Cavern)
    "Fire Cavern 3",  // 131: bdin1 (Fire Cavern)
    "Fire Cavern 4",  // 132: bdin2 (Fire Cavern)
    "Fire Cavern 5",  // 133: bdin3 (Fire Cavern)
    "Fire Cavern 6",  // 134: bdin4 (Fire Cavern)
    "Fire Cavern 7",  // 135: bdin5 (Fire Cavern)
    "Fire Cavern 8",  // 136: bdview1 (Fire Cavern)
    "B-Garden - 2F Hallway 1",  // 137: bg2f_1
    "B-Garden - 2F Hallway 2",  // 138: bg2f_11
    "B-Garden - 2F Hallway 3",  // 139: bg2f_2
    "B-Garden - 2F Hallway 4",  // 140: bg2f_21
    "B-Garden - 2F Hallway 5",  // 141: bg2f_4
    "B-Garden - 2F Hallway 6",  // 142: bg2f_22
    "B-Garden - 2F Hallway 7",  // 143: bg2f_3
    "B-Garden - 2F Hallway 8",  // 144: bg2f_31
    "B-Garden - Library 1",  // 145: bgbook_1
    "B-Garden - Library 2",  // 146: bgbook1a
    "B-Garden - Library 3",  // 147: bgbook1b
    "B-Garden - Library 4",  // 148: bgbook_2
    "B-Garden - Library 5",  // 149: bgbook2a
    "B-Garden - Library 6",  // 150: bgbook_3
    "B-Garden - Library 7",  // 151: bgbook3a
    "bgbtl_1",  // 152: bgbtl_1 (???)
    "bgcrash1",  // 153: bgcrash1 (???)
    "B-Garden - Cafeteria 1",  // 154: bgeat_1
    "B-Garden - Cafeteria 2",  // 155: bgeat1a
    "B-Garden - Cafeteria 3",  // 156: bgeat_2
    "B-Garden - Cafeteria 4",  // 157: bgeat2a
    "B-Garden - Cafeteria 5",  // 158: bgeat_3
    "B-Garden - Front Gate 1",  // 159: bggate_1
    "B-Garden - Front Gate 2",  // 160: bggate_2
    "B-Garden - Front Gate 3",  // 161: bggate_4
    "B-Garden - Front Gate 4",  // 162: bggate_5
    "B-Garden - Front Gate 5",  // 163: bggate_6
    "B-Garden - Front Gate 6",  // 164: bggate6a
    "B-Garden - Hall 1",  // 165: bghall_1
    "B-Garden - Hall 2",  // 166: bghall1a
    "B-Garden - Hall 3",  // 167: bghall1b
    "B-Garden - Hall 4",  // 168: bghall_2
    "B-Garden - Hall 5",  // 169: bghall2a
    "B-Garden - Hall 6",  // 170: bghall_3
    "B-Garden - Hall 7",  // 171: bghall3a
    "B-Garden - Hall 8",  // 172: bghall_4
    "B-Garden - Hall 9",  // 173: bghall4a
    "B-Garden - Hall 10",  // 174: bghall_5
    "B-Garden - Hall 11",  // 175: bghall_6
    "B-Garden - Hall 12",  // 176: bghall6b
    "B-Garden - Hall 13",  // 177: bghall_7
    "B-Garden - Hall 14",  // 178: bghall_8
};
// ... (remaining 803 entries from field ID 179-981 follow the same pattern)
// See the standalone field_display_names.h file for the complete array.
static const int FIELD_DISPLAY_NAMES_COUNT = 982;
```

**Note:** The full 982-entry array is too large to embed inline in this document. The complete, copy-paste-ready header file has been saved as `field_display_names.h` in the outputs directory. That file contains every single entry from field ID 0 through 981 with proper C syntax, comments, and disambiguation numbers.
