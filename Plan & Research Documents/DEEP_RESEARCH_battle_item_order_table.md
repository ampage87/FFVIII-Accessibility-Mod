# Deep Research: FF8 Battle Item Order Table

## Context

I'm reverse-engineering the FF8 Steam 2013 PC edition (FF8_EN.exe, App ID 39150) with FFNx v1.23.x. I need to find the **battle item order table** — the array that determines the order items appear in the in-battle Item command and in the menu's Item > Battle arrangement screen.

## Critical Savemap Correction

- **The savemap header is 76 bytes (0x4C), NOT 96 bytes (0x60).**
- All post-header offsets from other research sources (Qhimm wiki, etc.) are 0x14 (20 bytes) too high.
- When using offsets from external research: **subtract 0x14** from any offset that is relative to post-header savemap data.
- Confirmed savemap base address at runtime: **0x1CFDC5C**

## What I've Confirmed

- Item inventory is at savemap +0x0B40 (198 slots × 2 bytes: item_id, quantity)
- When using the Item > Battle rearrangement screen, the inventory bytes at +0x0B40 do **NOT** change — the items stay in the same inventory slots
- The visual order on the Battle arrangement screen DOES change after swaps
- The battle order persists across menu exits (re-entering Battle shows the rearranged order)
- I scanned pMenuStateA (the menu state struct) at offsets 0x000-0x2B0 and 0x5F0-0x618 — no order table was visible (compared before/after swap dumps byte by byte)
- I scanned savemap offsets 0x0AD0-0x0B30 and 0x0CCC-0x0D10 — no changes after swaps
- The only pMenuStateA bytes that changed between BATTLE_ENTER and AFTER_SWAP were the known cursor offsets (+0x285/+0x286) — no permutation array appeared anywhere

## What I Need

1. The exact location of the battle item order table — either as a savemap offset (relative to savemap base 0x1CFDC5C) or as a runtime memory address/pointer
2. The format of the table: is it an array of uint8 inventory indices? An array of item IDs? How many entries? (198 matching inventory, or fewer?)
3. Whether FF8 stores this in the savemap (persisted to save files) or in a separate runtime-only structure
4. If it's in the savemap, what section is it in? (The savemap layout is: header 0x4C, GFs 16×0x44=0x440, chars 8×0x98=0x4C0, shops, limit breaks, items section, field_h, field, world)
5. If it's a runtime-only structure, what is the base pointer address in FF8_EN.exe's memory space?

## Additional Context

- FF8's battle Item command shows a filtered, reorderable subset of inventory items
- The Item > Battle menu lets the player rearrange which items appear first in battle
- This order also determines the order items appear when using the Item battle command during actual combat
- The savemap "items" section starts at +0x0B40 and contains 198 × 2 bytes of item data
- After the items section there should be shops data, limit break data, and then field_h
- The FF8 save file format documentation on the Qhimm wiki describes a `battle_order[32]` array within the items section — this may be what I need, but I need the **corrected offset** (remember to subtract 0x14 from any published offset)

## Key Reference Sources

- FFNx source at github.com/julianxhokaxhiu/FFNx, specifically `src/ff8_data.cpp` for address offsets and struct layouts
- Qhimm wiki FF8 save file format documentation (sections on items and battle order)
- FF8 save_data.h structures in any FF8 modding tool source code
- The `savemap_ff8_items` struct likely contains the battle_order array

## Expected Answer Format

Please provide:
- The exact savemap offset of the battle order table (relative to savemap base, with the 0x4C header correction applied)
- The data format (array of what type, how many elements)
- How the cursor index maps to this table and then to inventory slots
- Any pointer indirection needed (is it a direct array in the savemap, or does the engine copy it to a runtime buffer?)
