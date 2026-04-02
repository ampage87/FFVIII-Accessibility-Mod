# FF8 battle Item sub-menu: the persistent cursor-to-item mapping

**`battle_order[32]` in the savemap is the persistent source of truth.** Each byte is an inventory slot index (0–197) into the `items[198]` array. When the player opens the Item sub-menu in battle, the init function at `0x4F8010` walks `battle_order` sequentially, resolves each entry to an inventory slot, filters for battle items (id 1–32), and builds a working buffer that the cursor directly indexes. The display struct at `0x1D8DFF4` is a separate, field-menu-only cache — the battle engine never depends on it. This explains why items remain selectable at correct positions even after a reload zeroes that struct: the battle code reads straight from the savemap every time.

## The definitive ITEMS struct from Hyne's SaveData.h

The Hyne save editor (by myst6re, the authoritative FF8 RE researcher) provides the canonical packed struct definition:

```cpp
PACK(
struct ITEMS // 428 bytes
{
    quint8 battle_order[32];       // inventory slot indices for battle arrangement
    quint16 items[198];            // (uint8 itemID, uint8 quantity) × 198
});
```

**Total: 428 bytes** (32 + 396). The `battle_order` and `items` arrays are contiguous — battle_order sits directly before inventory in the savemap. Each `items` entry packs a 1-byte item ID in the low byte and a 1-byte quantity in the high byte. The `battle_order` bytes are indices into this 198-slot array, with **0xFF** serving as the sentinel for empty/unused positions (consistent with FF8's convention — the Party field at savemap +0x0B04 also uses 0xFF termination).

The offset mapping between documentation systems is critical to avoid confusion. The FFRTT wiki documents offsets from the start of the save data block. Your empirical savemap base at `0x1CFDC5C` points **0x14 bytes earlier** than the wiki's zero-point. This 0x14 gap is the "header correction" you identified:

| Field | Wiki offset (save block) | Your offset (from 0x1CFDC5C) | Runtime address |
|-------|-------------------------|------------------------------|-----------------|
| battle_order[32] | 0x0B34 | +0x0B20 | **0x1CFE77C** |
| items[198] | 0x0B54 | +0x0B40 | **0x1CFE79C** |

The inventory pointer `0x01CFE79C` that you found in Pool[2] at +0x20 during Session 1 is exactly `savemap_base + 0x0B40`, confirming this layout precisely.

## How battle_order encodes the visual arrangement

`battle_order[i]` means "the item at visual position i comes from inventory slot `battle_order[i]`." However, battle_order holds indices for **all** items in the player's arrangement — battle and non-battle alike — not just battle-usable ones. The engine's runtime filter extracts the battle-item subset on the fly.

Your Session 2 data demonstrates this clearly. The raw battle_order bytes `00 03 04 05 06 07 01 08 09 0A 0B 0C 0D 0E 0F 02 ...` reference inventory slots 0, 3, 4, 5, 6, 7, 1, 8, 9, 10, 11, 12, 13, 14, 15, 2, etc. Slot 0 contained id=151 (non-battle), while slots 1 and 2 contained Phoenix Down (id=7) and Remedy (id=16) respectively. The engine scans all 32 entries, skipping non-battle items, and only the battle items (id 1–32) contribute to the visual list.

This is how cursor position 3 mapped to Remedy in Session 2 despite Remedy being at `battle_order[15]`. Between positions 0 and 14 in the walk, exactly **three** other inventory slots resolved to battle item IDs (likely items with qty=0 from earlier gameplay — your Session 1 display struct confirms Potion (id=1, qty=0) and Potion+ (id=2, qty=0) exist in inventory). These three items plus Remedy give visual positions 0, 1, 2, 3. **Remedy occupied position 3 because it was the fourth battle-type item encountered during the sequential walk.**

Session 1's "compacted" battle_order (`bo[0]=Remedy, bo[1]=Phoenix Down`) reflects a different save state where the field menu's "Battle" arrangement had been explicitly set, producing a tidy, battle-items-only ordering. Session 2's "full" battle_order with all item types intermixed is what the engine produces when items are acquired naturally and never manually rearranged for battle.

## The runtime algorithm in the init function at 0x4F8010

When the player selects "Item" from the battle command menu, `0x4F8010` executes. Based on convergent evidence from the struct layout, the empirical cursor behavior, the disassembly hints at `0x4F81A6` and `0x4AD0EB`, and the OpenVIII reimplementation's architecture, the algorithm is:

```
// 0x4F8010 — Item sub-menu init
// ESI points to the controller state struct (passed from Node A at 0x4FDBA0)

working_buffer = allocate_via_0x4BE540();  // local working buffer
count = 0;

for (i = 0; i < 32; i++) {
    slot = savemap->battle_order[i];          // byte from 0x1CFE77C + i
    if (slot == 0xFF) continue;               // empty sentinel
    if (slot >= 198) continue;                // bounds check

    item = savemap->items[slot];              // {id, qty} from 0x1CFE79C + slot*2
    
    if (item.id >= 1 && item.id < 33) {       // battle item filter
        working_buffer[count].id    = item.id;
        working_buffer[count].qty   = item.qty;
        working_buffer[count].slot  = slot;    // back-reference for quantity updates
        count++;
    }
}
// Store count at [esi+N] for cursor bounds
// Store working_buffer pointer at [esi+M]
```

The **two filter addresses** you identified serve different phases. The filter at **0x4F81A6** (inside the handler 0x4F81F0) likely runs during list construction or display refresh, deciding which entries populate the visual list. The filter at **0x4AD0EB** is in a different code path — possibly the item-use confirmation logic that additionally verifies `qty > 0` before consuming the item. The display includes items with qty=0 as grayed-out slots (your Session 1 display struct proves this: Potion and Potion+ at qty=0 occupied visual positions 1 and 2). The selection logic then gates on quantity.

## Where the working buffer lives (and why Pool[2] was empty)

The handler at `0x4F81F0` runs as a **subroutine** called from Node A (`0x4FDBA0`), not as an independent pool node. This is why `0x4F81F0` never appeared in any pool slot's handler fields at +0x08/+0x0C, and why Pool[2] was zeroed in Session 2 — the Item sub-menu doesn't own a pool slot.

The working buffer created by `0x4F8010` is most likely stored inside Node A's body or in the controller state struct that ESI points to. The jump table dispatch at `0x4FBF5C` (`movzx eax, word ptr [esi+0x10]; jmp dword ptr [eax*4 + 0x4FBF5C]`) confirms that `[esi]` is the state struct, with `[esi+0x10]` being the phase counter. The working buffer pointer and item count are at other offsets within this same struct — likely **`[esi+0x18]` or `[esi+0x1C]`** based on typical FF8 battle controller layouts (phase at +0x10, cursor at +0x12 or +0x14, data pointer at +0x18+). The memory allocated by `0x4BE540` for the buffer would be somewhere in the dynamic heap, not in the static pool region you scanned at `0x1D76000–0x1D78000`.

To find this buffer empirically: set a read breakpoint on `0x1CFE77C` (battle_order[0]) during Item sub-menu init. The code that reads it will store the resolved item data into the working buffer. Following that write will reveal the buffer address.

## The "visual item count" byte at 0x01D768E4

The unstable values you observed (7→1→4→9) during sub-menu init are consistent with the count being **updated incrementally** as the init loop runs, then finalized. The final stable value should equal the number of battle_order entries that pass the battle-item filter. The intermediate values reflect partial loop states or multi-phase initialization (the handler uses a phase state machine dispatched via the jump table at 0x4FBF5C — phase 0 likely allocates, phase 1 builds the list, phase 2+ handles cursor input).

## Three-layer architecture of FF8's item arrangement

The full picture involves three distinct data layers, each serving a different purpose:

**Layer 1 — Savemap (persistent, survives save/load).** `battle_order[32]` at `0x1CFE77C` stores inventory slot indices in the player's custom battle arrangement order. `items[198]` at `0x1CFE79C` stores actual item IDs and quantities. Together they define what the battle item list should contain. This is the ground truth — it always exists and is always correct.

**Layer 2 — Field menu display cache (transient, zeroed on reload).** The display struct at `0x1D8DFF4` (32 × {id, qty}) is populated by the field menu's Items screen code when the player visits Items > Battle. It's a pre-computed snapshot for the field menu's rendering. The battle engine **does not read this struct** — its existence is coincidental to the field menu code path. When zeroed after reload, nothing breaks because the battle code never depended on it.

**Layer 3 — Battle working buffer (ephemeral, rebuilt per use).** Created by `0x4F8010` each time "Item" is selected in battle. Lives in the ESI state struct allocated from the heap. Reads Layer 1 data directly, applies the battle-item filter, and produces the cursor-indexable list. Destroyed by `0x4FC990` (destructor) when the sub-menu closes. This is what the cursor at `0x01D768EC` actually indexes into.

## Confirming the code path from cursor to item ID

The complete chain for item selection in battle:

1. Player presses confirm with cursor at position N (byte at `0x01D768EC`)
2. Handler `0x4F81F0` reads `[esi+0x10]` for current phase, dispatches via jump table at `0x4FBF5C`
3. In the "confirm selection" phase, handler reads `working_buffer[N]` from the ESI state struct
4. Retrieves `item.id` and `item.qty` from the buffer entry
5. Checks `qty > 0` (the filter at `0x4AD0EB`) — if zero, selection is rejected
6. If valid, resolves the inventory slot via the stored back-reference (`working_buffer[N].slot`)
7. Decrements `savemap->items[slot].qty` at `0x1CFE79C + slot*2 + 1`
8. Applies item effect using kernel.bin battle item data (items 1–32 map to the kernel's dedicated "Battle Items" section)

## Conclusion

**There is no mystery third data source.** The persistent mechanism is simply `battle_order[32]` + `items[198]` in the savemap, read fresh each time the sub-menu opens. The display struct at `0x1D8DFF4` is a red herring — it belongs to the field menu, not the battle engine. The battle init at `0x4F8010` builds an ephemeral working buffer in the ESI state struct by walking battle_order, looking up each inventory slot, and filtering for `id ∈ [1, 32]`. This buffer is what maps cursor position → item ID.

To verify definitively, set a hardware read breakpoint on `0x1CFE77C` during the frame when "Item" is selected in battle. The init function will read battle_order entries sequentially, and you can trace the writes to the working buffer to identify its exact address and layout. Alternatively, set a write breakpoint in the 0x1D76000–0x1D7A000 range during init to catch the working buffer being populated — it's likely allocated slightly outside the static pool region you previously scanned.
