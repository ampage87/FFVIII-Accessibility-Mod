# Deep Research Prompt — FF8 Battle Items Persistent Arrangement

**Date:** 2026-04-28 (after v0.14.41 BAT failed)
**Purpose:** Find the absolute RAM address of the user-configured "Items > Battle" arrangement in FF8 Steam 2013 + FFNx v1.23.x.

---

## Prompt to paste into ChatGPT (with web/deep-research enabled)

I'm reverse-engineering FF8 (Final Fantasy VIII) Steam 2013 edition (FF8_EN.exe, base 0x00400000) running with FFNx v1.23.x — I need to identify where in memory FF8 stores the persistent **"Items > Battle"** arrangement, which is separate from the **"Items > All Items"** arrangement. The player customizes "Items > Battle" via the field menu's Items > Battle screen, and that arrangement is what determines the visual layout of items shown in the in-battle items submenu. The two arrangements are independent — rearranging one does not affect the other.

### What I already know (confirmed via runtime memory inspection)

- FF8 savemap loads to absolute RAM at runtime. Character data block starts at **0x1CFE0E8**. There is a **76-byte (0x4C) savemap header** BEFORE the char-data block, so the savemap base is **0x1CFE0E8 − 0x4C = 0x1CFE09C**.
- **CRITICAL HEADER CORRECTION:** Many public FF8 savemap references (Hyne, qhimm wiki, etc.) assume a 96-byte (0x60) header. Confirmed via runtime inspection the real header is **76 bytes (0x4C)**. **Subtract 0x14 from any post-header offset you find in those references** to get the actual offset.
- Per-char struct stride is **152 bytes (0x98)**, starting at 0x1CFE0E8.
- Party formation is 3 bytes at 0x1CFE74C.
- The **"All Items" arrangement** is at absolute address **0x1CFE77C** — 32 bytes of `uint8` indices into full inventory. Test reads as: `00 03 04 05 06 07 01 08 09 0A 0B 0C 0D 0E 0F 02 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F`.
- **Full inventory** is at absolute address **0x1CFE79C** — 198 × `{uint8 id, uint8 qty}` = 396 bytes.
- The **items submenu cursor** is a `uint8` at absolute address **0x01D768EC**. Increases by 1 per Down-arrow press, by 4 per Right-arrow (page navigation).

### The problem — engine displays an arrangement we cannot locate

Test setup: player inventory has 4 battle-usable items (id < 33):
- Potion x10 at inv[0] (id=1)
- Phoenix Down x1 at inv[1] (id=7)
- Elixir x2 at inv[2] (id=9)
- Remedy x2 at inv[3] (id=16)

Plus non-battle items (id ≥ 33) at inv[4..11] and empties beyond.

When items submenu opens in battle, the engine cursor walks 0..N. **At cursor=9 (visible page 3 slot 2 by simple `cursor / 4` pagination), the engine renders Elixir on screen.** Other observed positions: cursor 0 = Potion (page 1 slot 1). The user reports Potion and Phoenix Down are NOT adjacent in the visual layout — there are empty slots between them.

This rules out:
- A simple compact-only-non-empty layout (would put Elixir at cursor=2)
- The All Items arrangement at 0x1CFE77C (would put Elixir at battle_order index 15, not cursor 9)
- A full-inventory-walk layout (Elixir at inv[2] = cursor 2, not 9)

**The user explicitly distinguishes "Items > All" from "Items > Battle" as two separately-customizable persistent arrangements. The Battle arrangement is what the engine reads in battle. We need its address and format.**

### What I have already checked (all empty / no match)

- **Direct byte-pair scan** for the packed sequence `01 0A 10 02 09 02` (the 3 in-battle items at qty 10, 2, 2) at strides 2/4/8 across these absolute address ranges:
  - 0x01CFE000–0x01D00000 (savemap region)
  - 0x01D27000–0x01D2A000 (battle entity array)
  - 0x01D76000–0x01D78000 (battle UI control block)
  - 0x01D8D000–0x01D8F000 (the documented v0.10.105 location 0x01D8DFF4 was here)
  - 0x01D90000–0x01D95000 (post-display)
  - **Result: ZERO matches anywhere.**
- **Index-sequence scan** for `00 03 02` (inv positions of the 3 items) at strides 1/2/4 across the same ranges. **Result: ONE stride=2 match at 0x01CFE345** which is char[3] + offset 0x95 (likely coincidence — surrounding bytes are `00 00 03 8D 02 00 00 10 27 00...`, no coherent struct).
- **Pool-pointer dereference scan** of every uint32 in the items submenu controller's pool nodes (10-node ring buffer at 0x01D76BC8, stride 0x78). Found 3 valid buffer pointers but they hold render data, NOT item content:
  - One pointed to a buffer of FF8 text glyphs (`57 63 72 71 20 5F 60 67...`)
  - One pointed to screen Y-coordinates (`28 00 00 00 3C 00 00 00 50 00 00 00 B4 00 00 00 D8 00 00 00` = 40, 60, 80, 180, 216 px)
  - One pointed to uint16 sequences with FFFF sentinels

### My questions

1. **In the FF8 savemap format**, is there a separate persistent Battle arrangement distinct from the All-items arrangement? If yes, what's its savemap offset (and the resulting absolute RAM address given the 0x1CFE09C savemap base)?
2. **What's the format?** 32 uint8 invIdx (like All-items)? 32 × `{id, qty}` packed? Some other structure?
3. **In FF8_EN.exe (Steam 2013 build, base 0x00400000)**, what function handles the in-battle items submenu rendering — specifically, the function that reads the cursor at 0x01D768EC and dereferences it to find which item to render at that position? Provide the absolute function address.
4. **FFNx v1.23.x replaces some engine functions.** The original v0.10.104 ITEM_HANDLER address 0x004F81F0 is no longer in the items submenu pool (now `pool[0]+0x08 = 0x004FDB60`, `+0x0C = 0x004FDBA0`). Has FFNx replaced the items submenu rendering path? Identify the new function in `julianxhokaxhiu/FFNx` canary source, specifically `src/ff8_data.cpp`, `src/ff8.h`, `src/ff8_battle.cpp` if present.
5. **Search FFNx canary source for any of these symbols** and report what they reference: `inventory_battle`, `battle_items`, `menu_battle_items`, `battle_item_order`, `battle_item_arrangement`, `item_order_battle`, `g_battle_items`, `field_battle_inv`, `inventory_battle_order`.
6. **Cross-reference Hyne save editor source** (open-source FF8 save editor on GitHub — search for the field that holds the customized battle arrangement) and the qhimm-forum savemap reference threads.
7. **Cross-reference the Doomtrain / Ifrit / Deling FF8 modding toolkits** for any documentation of a battle-only inventory arrangement separate from the main inventory.

### Output I need

A specific absolute RAM address (or savemap offset + savemap base), the byte format (32 indices? 32 × {id,qty}? other?), and ideally the FF8_EN.exe function address that reads it. With citations to FFNx source, Hyne source, qhimm forum threads, or wiki pages.

---

## After ChatGPT replies

Paste the answer back into the chat with Claude. Claude will validate the address claim against runtime memory and write a v0.14.42 build that reads from it.
