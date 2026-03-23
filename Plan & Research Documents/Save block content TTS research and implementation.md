# Save Block Content TTS — Research & Implementation

## Goal
When the user navigates to a save block on the save/load screen, announce the block's
contents: character name, level, play time, gil, location, party portraits.

## Approach: Direct Memory Read
Find the memory address where the game stores loaded save block headers during the
save screen. Read the `savemap_ff8_header` struct fields and announce them via TTS.

---

## Key Reference: savemap_ff8_header (from FFNx src/ff8/save_data.h)

```c
struct savemap_ff8_header {     // offset within full savemap: +4 (after checksum + fixed_value)
    uint16_t location_id;       // +0x00  field ID
    uint16_t char1_curr_hp;     // +0x02
    uint16_t char1_max_hp;      // +0x04
    uint16_t save_count;        // +0x06
    uint32_t gil;               // +0x08
    uint32_t played_time_secs;  // +0x0C  seconds
    uint8_t  char1_lvl;         // +0x10  level
    uint8_t  char1_portrait;    // +0x11  party member 1 model ID
    uint8_t  char2_portrait;    // +0x12  party member 2
    uint8_t  char3_portrait;    // +0x13  party member 3
    uint8_t  squall_name[12];   // +0x14  menu-encoded name
    uint8_t  rinoa_name[12];    // +0x20
    uint8_t  angelo_name[12];   // +0x2C
    uint8_t  boko_name[12];     // +0x38
    uint32_t curr_disk;         // +0x44
    uint32_t curr_save;         // +0x48
};
// Total header size: 0x4C = 76 bytes
```

Full `savemap_ff8` starts with:
- `uint16_t checksum`  (+0x00)
- `uint16_t fixed_value` (+0x02, always 0x08FF)
- `savemap_ff8_header header` (+0x04)

So the header fields start at savemap+0x04.

## Key Reference: Character Name Encoding (menu font / sysfnt)
Default "Squall" in menu encoding: `0x37 0x53 0x57 0x3F 0x4E 0x4E 0x00 ...`
- S=0x37, q=0x53, u=0x57, a=0x3F, l=0x4E, l=0x4E

## Key Reference: savemap Pointer in FFNx
```c
ff8_externals.savemap = (savemap_ff8*)get_absolute_value(ff8_externals.pubintro_enter_main, 0x9);
```
This points to the CURRENT game's live savemap. NOT the save screen display buffer.

## Key Reference: Known Save Screen Offsets (from pMenuStateA)
- Slot cursor: +0x276 (mode 6), +0x1FE (mode 1)
- Block cursor: +0x268 (mode 6), +0x1F0 (mode 1)
- Phase: +0x266 (mode 6), +0x1EE (mode 1)

## Key Reference: sm_pc_read
```c
ff8_externals.sm_pc_read = (uint32_t(*)(char*, void*))get_relative_call(ff8_externals.main_loop, 0x9C);
```
This function reads .ff8 save files from disk into a buffer. The `void*` parameter is
the destination buffer. Hooking or tracing this function when the save screen opens
could reveal where all save data is loaded.

---

## Implementation Plan (Bite-Sized Steps)

### Step 1: Resolve savemap base address ✅ / ☐
Add resolution of `pubintro_enter_main` and `savemap` pointer to ff8_addresses.
This gives us the current game's savemap base address.
Status: ☐ DEFERRED (not needed for scan approach)

### Step 2: Diagnostic — Scan for save header patterns ✅ / ☐
F11-triggered diagnostic in menu_tts.cpp that uses VirtualQuery to scan ALL
readable process memory for the 0x08FF fixed_value fingerprint. Validates
surrounding fields (location_id < 1000, level <= 100, play_time < 100hrs,
disk <= 4, valid name char). Decodes character name via DecodeMenuText.
Logs absolute address, offset from pMenuStateA, and all header fields.
Status: ✅ IMPLEMENTED (v0.07.49) — ready for testing

### Step 3: Determine buffer layout ✅ / ☐
Once we find one save header, check if there's an array of them. Measure the stride
between consecutive save block headers. Expected: each block is either a full
savemap_ff8 (~6KB+) or just headers (~76 bytes). Log the first few fields of each
found header to verify correctness.
Status: ☐ NOT STARTED

### Step 4: Implement save header reader ✅ / ☐
Given the base address and stride, read the header for the currently selected block
(using block cursor at +0x268/+0x1F0). Decode character name, format play time
(hours:minutes), format gil, map portrait IDs to character names.
Status: ☐ NOT STARTED

### Step 5: Announce block contents on cursor change ✅ / ☐
When block cursor changes (already detected in PollBlockCursor), read the save header
for the new block index and speak: "Block N: [Name] Level [L], [Location],
[H] hours [M] minutes, [Gil] gil" — or "Block N: Empty" if no data.
Status: ☐ NOT STARTED

### Step 6: Location ID to name mapping ✅ / ☐
The header stores `location_id` as a numeric field ID. Need a lookup table mapping
field IDs to human-readable location names. Options:
- Extract from mngrp.bin / field string tables
- Hardcode the ~30 most common save locations
- Fall back to "Location [ID]" for unknowns
Status: ☐ NOT STARTED

---

## Progress Log

### Session 2026-03-17 — Step 2 (Memory Scan Diagnostic)
- v0.07.49: Added F11-triggered memory scan. F11 not detected (Steam/JAWS intercept).
- v0.07.50: Moved scan to F12. Scan ran but found 50 hits — 49 were all-zero false positives.
  Only real hit: #0 at 0x019CE018 = live savemap (lvl=7, gil=5000, loc=70).
- v0.07.51: Added non-zero data filter + "Squall" name pattern scan. Wrong encoding used
  (0x37 0x53 0x57 = "Suy" instead of 0x37 0x4F 0x53 = "Squ"). Pass 2 found 0 hits.
- v0.07.52: Fixed Squall encoding to 37 4F 53 3F 4A 4A. Extended hex dump to 48 bytes.
  Results: Pass 1 found live savemap at 0x019CE018 (only 1 real hit). Pass 2 found 11
  "Squall" hits at 96-byte stride in FFNx display buffer (0x6E8Axxxx). These are
  pre-formatted render strings, NOT raw save headers. Conclusion: game does NOT keep
  save block headers in memory — reads from disk, formats text, discards raw data.
- v0.07.53: Implemented sm_pc_read hook. Hook installed OK but NEVER FIRED.
  sm_pc_read is for Moriya archive filesystem, NOT .ff8 save files.
- v0.07.54: Added .ff8 detection to CreateFileA hook. CONFIRMED: game uses CreateFileA
  for save files. Path: Documents\Square Enix\FINAL FANTASY VIII Steam\user_NNNNN\slotN_saveNN.ff8
  Only slot1_save01.ff8 was opened (the one block with save data).
- v0.07.55: Pivoted to reading .ff8 file directly from CreateFileA hook.
  Reads first 256 bytes via separate ReadFile handle. Cache + announce infrastructure working.
  BUT: fixed_value=0x0000 at offset +2 — the file is COMPRESSED.
- v0.07.56: Added hex dump + auto-search for 0x08FF. Found marker at raw offset 0x0167.
  Header data IS in the file but interleaved with escape bytes.

### File Format Analysis (from uploaded slot1_save01.ff8)
- File: 2106 bytes. First uint32 = 0x0836 (2102) = payload size.
- **Compression**: NOT raw savemap. Some encoding with `FF` as escape byte.
- `FF FF` in file = literal `0xFF` in data (confirmed)
- `FF XX` where XX != FF = UNKNOWN rule. Could be run-length or just literal.
- Savemap checksum starts at raw file offset 0x0164.
- Header fields visible in raw data: location=70, HP=486, all match memory scan.
- BUT: extra `FF` bytes appear before some data bytes (e.g., `FF 03` for save_count=3).
- Also non-FF extras appear later (e.g., `EF` before name byte `73`).
- This suggests **bitstream compression** (LZS or similar), not simple byte escaping.

### Key Finding: Live savemap address
- `0x019CE018` = live game savemap (from memory scan). Only ONE in memory.
- Game does NOT keep multiple save headers in memory. Reads, displays, discards.

### Next Steps for File Format
1. **Option A**: Deep research the exact .ff8 compression. Check Hyne source code
   (github.com/myst6re/hyne SavecardData.cpp) for the Steam format reader.
2. **Option B**: Hook ReadFile to capture the raw buffer, then scan for decompressed
   header in nearby memory after the game processes it.
3. **Option C**: Parse the rendered GCW display text instead of raw save data.
   11 copies of formatted text found at 96-byte stride in render buffer.
   Text includes name, level, location — just needs string parsing.

