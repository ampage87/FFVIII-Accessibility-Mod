# Deep Research Request: FF8 Battle Item Sub-Menu Cursor-to-Item Mapping

## Platform
Final Fantasy VIII PC (Steam 2013, FF8_EN.exe, App ID 39150) + FFNx v1.23.x. No ASLR. All addresses are static.

## CRITICAL: Savemap Offset Correction
ChatGPT deep research assumes 96-byte (0x60) savemap header. **Actual header is 76 bytes (0x4C).** All post-header research offsets are 0x14 too high — subtract 0x14 from any savemap offset found in public RE docs. Confirmed savemap base: `0x1CFDC5C`.

## The Problem
In the battle Item sub-menu, the cursor byte at `0x01D768EC` indexes into 32 positions that correspond to the player's **Items > Battle** visual arrangement (set via the field menu). We need to find the runtime data structure that maps these 32 cursor positions to actual inventory items, because:

1. The **display struct at `0x1D8DFF4`** (32 × {uint8 id, uint8 qty}) contains the correct mapping **when populated** — but it gets **zeroed on game reload** and is only populated when the player opens the field menu Items screen.

2. The **battle_order[32] at `0x1CFE77C`** (savemap offset +0x0B20) contains inventory slot indices in a **different order** from the visual layout. It includes non-battle items mixed in. The cursor does NOT index into battle_order directly.

3. Despite the display struct being zeroed, the cursor still navigates the same 32 visual positions and items are selectable at the correct positions. So the engine has ANOTHER data source for the cursor-to-item mapping.

## What We Know (Empirical Evidence)

### Session 1: Display struct populated (player visited Items > Battle screen before battle)
- **Display struct `0x1D8DFF4`**: `10 02 02 00 01 00 07 03 03 00 04 00 05 00 06 00 08 00 09 00 ...`
  - ds[0] = id=16(Remedy), qty=2
  - ds[1] = id=2(Potion+), qty=0 (empty)
  - ds[2] = id=1(Potion), qty=0 (empty)
  - ds[3] = id=7(Phoenix Down), qty=3
  - ds[4..31] = remaining battle items, all qty=0
- **Cursor behavior**: subCursor=0 → Remedy (correct). subCursor=1,2 → empty positions. subCursor=3 → Phoenix Down (correct).
- **battle_order**: bo[0]=inv[2]=Remedy, bo[1]=inv[1]=PhoenixDown (compacted, no gaps, different order from display struct)
- **Pool[2] at `0x1D76CB8`**: Had inventory pointer (0x01CFE79C) at +0x20 and battle_order pointer (0x01CFE77C) at +0x24. Handler pointers at +0x08/+0x0C were NULL (not 0x4F81F0 as deep research predicted).

### Session 2: Display struct zeroed (game reloaded, didn't open Items screen)
- **Display struct `0x1D8DFF4`**: ALL ZEROS (64 bytes of 0x00)
- **Cursor behavior**: subCursor still goes 0, 1, 2, 3... Items still selectable at correct positions. At subCursor=3, player selected and used Remedy successfully.
- **battle_order**: `00 03 04 05 06 07 01 08 09 0A 0B 0C 0D 0E 0F 02 ...`
  - bo[0]=inv[0]=id=151(non-battle), bo[6]=inv[1]=id=7(Phoenix Down), bo[15]=inv[2]=id=16(Remedy)
  - The cursor values 0-3 do NOT correspond to battle_order positions 0-3
- **Pool[2]**: Completely zeroed. Inventory pointer NOT found in pool.
- **Pointer scan**: 0x4F81F0 (Item handler) NOT found anywhere in 0x1D76000-0x1D78000. 0x01CFE79C NOT found either.

### Key Observations
- The Item sub-menu handler is at **0x4F81F0** (phase handler) with init at **0x4F8010** and destructor at **0x4FC990**.
- The pool allocator is at **0x4BE540**, dispatcher at **0x4BE4D0**.
- The Item handler is called as a SUBROUTINE from Node A (0x4FDBA0), NOT as a separate pool node. 0x4F81F0 was never found in any pool slot's +0x08 or +0x0C.
- The battle_order array maps visual arrangement positions to inventory slot indices, but includes ALL inventory items (not just battle items). The first N entries that resolve to valid battle items (id 1-32, qty>0) are the ones shown on screen.
- The display struct at 0x1D8DFF4 contains ONLY battle items (ids 1-32) in visual arrangement order, with quantities. This is populated by the field menu Items screen code.

## What We Need

1. **Where does the engine store the battle item visual arrangement persistently?** The display struct at 0x1D8DFF4 is transient (zeroed on reload). There must be a persistent source — likely in the savemap — that maps the 32 visual positions to item IDs. Is it derived from battle_order at runtime? If so, what's the algorithm?

2. **How does the Item sub-menu handler (0x4F81F0) resolve cursor position to inventory item?** The handler receives its state via ESI register (not stack). What memory does it read to determine which item is at cursor position N? Specifically, does it:
   - Read the display struct (0x1D8DFF4) directly?
   - Walk battle_order and filter to battle items?
   - Use a separate runtime buffer populated by 0x4F8010 (init function)?

3. **What does 0x4F8010 (Item sub-menu init) populate?** When the player selects "Item" from the command menu, 0x4F8010 runs. It calls 0x4BE540 (allocator) and sets up the controller state. What working buffer does it create? Where is this buffer stored if not in the pool node body?

4. **Is there a "visual item count" byte** that tells the engine how many of the 32 positions have valid items? We observed `0x01D768E4` transitioning through values (7→1→4→9) during sub-menu init but it's unstable.

## Disassembly Hints
- The Item handler 0x4F81F0 uses a jump table at 0x4FBF5C for phase dispatch: `movzx eax, word ptr [esi+0x10]; jmp dword ptr [eax*4 + 0x4FBF5C]`
- 0x4F8010 pushes 0x4FC990 and 0x4F81F0 as arguments to 0x4BE540
- The disassembly at 0x4F81A6 and 0x4AD0EB contains the battle item filter: `id >= 1 && id < 33 && qty > 0`
- The field menu item code that populates 0x1D8DFF4 may be near the "Battle" sub-option handler in the Items menu controller

## What Would Be Most Helpful
- The exact code path from cursor position (0x01D768EC) to the item ID that gets used when the player confirms selection
- Any intermediate buffer between battle_order and the visual display that persists across the display struct being zeroed
- Whether battle_order itself encodes the visual positions (e.g., the FIRST entry in battle_order that resolves to a battle item = visual position 0, regardless of its index in the array)
