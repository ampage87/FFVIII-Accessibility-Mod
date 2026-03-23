# FF8 battle item order table lives at savemap +0x0B20

The battle item order table is a **32-byte `uint8[32]` array at savemap offset 0x0B20** (runtime address **0x1CFE77C**), sitting immediately before the 198-slot item inventory. It is stored in the savemap and persisted to save files. Your scan at 0x0AD0–0x0B30 partially overlapped this array but missed the final 15 bytes (0x0B31–0x0B3F), and the game almost certainly uses a deferred-write pattern that only commits changes to the savemap on menu exit — both factors explain why your scan showed no changes after swaps.

## Confirmed offset and source verification

The canonical documentation at **wiki.ffrtt.ru/index.php/FF8/GameSaveFormat** (authored by myst6re, the same developer who wrote the Hyne save editor) lists the battle order table at **save file offset 0x0B34** with a size of **32 bytes**. Applying your confirmed 0x14 correction (validated by items: wiki 0x0B54 − 0x14 = your confirmed 0x0B40) yields:

| Field | Wiki offset | Corrected savemap offset | Runtime address | Size |
|---|---|---|---|---|
| **battle_order[32]** | **0x0B34** | **0x0B20** | **0x1CFE77C** | **32 bytes** |
| items[198] | 0x0B54 | 0x0B40 | 0x1CFE79C | 396 bytes |

The Hyne source code (SaveData.h) confirms the struct layout with 1-byte packing — `battle_order[32]` immediately precedes `items[198]` inside a packed struct with no padding.

## Data format and struct definition

From the Hyne SaveData.h (myst6re/hyne), the containing struct is packed and approximately **444 bytes**:

```cpp
PACK(
struct ITEMS_SECTION // 444 bytes, starts at savemap+0x0B10 (wiki 0x0B24)
{
    quint16 limit_quistis;       //  2 bytes — savemap+0x0B10
    quint16 limit_zell;          //  2 bytes — savemap+0x0B12
    quint8  limit_irvine;        //  1 byte  — savemap+0x0B14
    quint8  limit_selphie;       //  1 byte  — savemap+0x0B15
    quint8  angelo_completed;    //  1 byte  — savemap+0x0B16
    quint8  angelo_known;        //  1 byte  — savemap+0x0B17
    quint8  angelo_points[8];    //  8 bytes — savemap+0x0B18
    quint8  battle_order[32];    // 32 bytes — savemap+0x0B20  ← TARGET
    quint16 items[198];          // 396 bytes— savemap+0x0B40
}); // Total: 2+2+1+1+1+1+8+32+396 = 444 bytes
```

Each entry in `battle_order[32]` is a **uint8 index into the 198-slot inventory array**. A value of `0xFF` likely indicates an unused/empty slot. The array maps battle menu position → inventory slot index: `battle_order[battle_menu_pos]` yields the inventory index, and `items[that_index]` gives you the item ID and quantity. FF8 limits battle items to roughly 32 types in kernel.bin, which is why the array is exactly 32 entries.

## Why your scan missed it

Two factors combined to hide the table from your scan:

**Incomplete coverage.** Your scan of savemap offsets 0x0AD0–0x0B30 covered only the first **17 bytes** of the 32-byte array (0x0B20–0x0B30, indices 0–16). The remaining **15 bytes** at 0x0B31–0x0B3F (indices 17–31) were outside your scan window. If you swapped items at positions 17+ in the battle list, the changed bytes would be invisible.

**Deferred write pattern.** PS1-era engine architecture (confirmed by the FF8 engine's modular design and FFNx's savemap double-pointer access pattern) strongly suggests the game uses copy-on-open / write-on-confirm. When you open the Item → Battle rearrangement screen, the engine copies `battle_order[32]` to a local working buffer. All swap operations during interaction modify only this buffer. Changes are written back to the savemap **only when you confirm and exit the screen** — not during individual swaps. This is standard for supporting cancel/undo behavior. Your comparison between `BATTLE_ENTER` and `AFTER_SWAP` states would show no savemap changes because the savemap hasn't been updated yet.

## Savemap section layout for context

The battle_order sits in the **limits + items section** of the savemap, which falls between MISC1 (party/gil data) and MISC2 (game time/battle stats):

| Savemap offset | Size | Section |
|---|---|---|
| 0x0000 | 0x4C (76) | Header (preview data) |
| 0x004C | 0x440 (1088) | GFs — 16 × 68 bytes |
| 0x048C | 0x4C0 (1216) | Characters — 8 × 152 bytes |
| 0x094C | 0x190 (400) | Shops |
| 0x0ADC | 0x14 (20) | Config |
| 0x0AF0 | 0x20 (32) | MISC1 (party, gil, Griever name) |
| 0x0B10 | 0x1BC (444) | **Limits + Items (contains battle_order at +0x10)** |
| 0x0CCC | 0x90 (144) | MISC2 (game time, battle stats) |
| 0x0D5C | 0x500 (1280) | Field variables |

## How cursor index maps to inventory slots

The mapping chain works as follows: the battle menu cursor position (0–31) indexes into `battle_order[32]` at savemap+0x0B20. The value stored there is an inventory slot index (0–197). That inventory slot index then indexes into `items[198]` at savemap+0x0B40, where each 2-byte entry contains the item ID (byte 0) and quantity (byte 1). In pseudocode:

```
cursor_pos       = current battle menu selection (0-31)
inventory_index  = savemap[0x0B20 + cursor_pos]     // uint8, 0-197 or 0xFF
item_id          = savemap[0x0B40 + inventory_index * 2]      // uint8
quantity         = savemap[0x0B40 + inventory_index * 2 + 1]  // uint8
```

## Pointer indirection at runtime

FFNx's source reveals the savemap is accessed via **double pointer indirection**:

```cpp
ff8_externals.savemap = (uint32_t**)get_absolute_value(ff8_externals.main_loop, 0x21);
```

A fixed code address holds a pointer, which points to another pointer, which points to the savemap data block. At runtime with your confirmed base of **0x1CFDC5C**, the battle_order array lives at **0x1CFE77C** through **0x1CFE79B**. However, during active menu interaction, the game likely operates on a separate stack/heap buffer and only writes back to 0x1CFE77C on menu exit confirmation.

## Recommended next steps for verification

To confirm this location definitively, set a **hardware write breakpoint** in Cheat Engine or x64dbg at address **0x1CFE77C** (savemap+0x0B20), then open the Item → Battle rearrangement screen, perform a swap, and **exit the menu**. The breakpoint should fire on menu exit when the working buffer is copied back to the savemap. This will also reveal the address of the working buffer (the source operand in the write instruction) and confirm the exact engine function responsible for the copy-back. Alternatively, expand your memory scan to cover the full range **0x0B20–0x0B3F** and compare snapshots from **before opening** the rearrange screen to **after exiting** it (not during interaction).
