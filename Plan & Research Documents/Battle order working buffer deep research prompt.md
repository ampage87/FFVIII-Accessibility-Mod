# Deep Research Request: FF8 Battle Order Working Buffer — Exact Copy Function Location

## Context

We are building an accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe, 32-bit x86, no ASLR). The mod needs to read the live "battle item arrangement" order table during the Item → Battle submenu, where the player rearranges which items are available in combat.

## What We Know (Confirmed)

### Savemap Layout
- **Savemap base**: `0x1CFDC5C` (runtime VA, confirmed)
- **Header**: 0x4C bytes (76 bytes, NOT 96 — all deep research offsets from ChatGPT need -0x14 correction)
- **`savemap_ff8_items` struct** starts at savemap + 0x0B10 (after header + GFs + chars + shops + config + party + weapons + griever + unk + gil + gil_laguna + limit_breaks)
- **`battle_order[32]`** at savemap + 0x0B20 (runtime `0x1CFE77C`). Format: `uint8[32]`, each entry = inventory slot index (0-197, 0xFF = empty)
- **`items[198]`** (each 2 bytes: item_id, quantity) at savemap + 0x0B40 (runtime `0x1CFE79C`)

### Deferred Write Pattern (Confirmed)
When the player enters Item → Battle arrangement:
1. The engine copies `battle_order[32]` + `items[198*2]` to a **working buffer** (likely on the thread stack)
2. All swaps during rearrangement happen in the working buffer
3. On exit, the engine copies the working buffer back to savemap
4. **Savemap at 0x1CFE77C does NOT change during active rearrangement** (confirmed via snapshot/diff diagnostic)

### Pattern Scan Results (Confirmed)
We scanned all process memory for the exact 32-byte `battle_order` sequence immediately after entering battle arrangement (before any swaps). Found 4 hits:
- **0x0019FF1C** (thread stack) — **THIS IS THE WORKING BUFFER.** Context bytes after it match inventory data. Stack address ~0x0019xxxx.
- 0x01CFE77C — savemap (known)
- 0x04BDF0C4 — coincidental match (garbage context)
- 0x6EA1A410 — coincidental match (zeroed memory)

### Disassembly Findings

**Swap function at `0x4AD0C0`:**
```asm
; Parameters: push battle_item_count (0x21 = 33?)
; Swaps two entries in battle_order[32] at hardcoded 0x1CFE77C
4AD0C0: push ebx / push ebp
4AD0C2: mov ebp, [esp+0xC]  ; parameter
4AD12E: movsx edx, BYTE PTR [eax+0x1CFE77C]  ; read battle_order[eax]
4AD144: movsx ecx, BYTE PTR [ebp+0x1CFE77C]  ; read battle_order[ebp]
4AD14B: mov dl, BYTE PTR [eax+0x1CFE77C]
4AD151: mov BYTE PTR [ebp+0x1CFE77C], dl      ; write swap
4AD157: mov BYTE PTR [eax+0x1CFE77C], cl      ; write swap
```
This writes directly to savemap. But our diff showed savemap didn't change during active swaps. So either:
- This function is NOT called during active battle arrangement swaps (only on exit/init)
- Or the working buffer swap uses a different code path

**Item → Battle initialization at `0x4F80EB`:**
```asm
4F80EB: push 0x1CFE77C       ; battle_order address
4F80F0: push 0x1CFE79C       ; items address
4F80F5: mov [esi+0x20], 0x1CFE79C  ; store items ptr in controller struct
4F80FC: mov [esi+0x24], 0x1CFE77C  ; store battle_order ptr in controller struct
4F8146: call 0x4F8180         ; setup function
4F8162: call 0x4F81F0         ; main controller (big switch, 0x2B4 stack frame)
```

**Setup function at `0x4F8180`:**
Processes battle_order[32] and writes to `0x1D8DFF4` (a display/render struct at stride 2 bytes per slot). This is the "expanded" display copy, not the raw order table.

**Main controller at `0x4F81F0`:**
Allocates 0x2B4 bytes on stack (`sub esp, 0x2B4`). Reads pMenuStateA (`0x1D76A9A`). Contains a jump table at `0x4FBF5C` for different sub-states.

**Callers of swap function 0x4AD0C0:**
- `0x4A40B6` — item use/consumption context
- `0x4D7B3C` — battle arrangement handler (inside a nested function)
- `0x4D7C34` — battle arrangement handler
- `0x4D7CC6` — battle arrangement handler
- `0x4EBA66` — sort/auto-arrange loop (iterates 0xC8 times)

**Two `rep movsd` (memcpy) in the item menu region:**
- `0x4F7DB3`: copies 0x26 DWORDs (152 bytes = one character struct) FROM savemap character data TO `0x1D8DC80`. This is character data copy, not battle_order.
- `0x4F7F03`: copies 0x26 DWORDs FROM `0x1D8DC80` BACK to savemap character data. This is the write-back.

### The Missing Piece

We cannot find the exact instruction that copies `battle_order[32]` + `items[198*2]` (428 bytes total) from savemap to the stack buffer at ~0x0019FF1C. It may be:
1. Inside one of the deeply nested calls from the controller at 0x4F81F0
2. A standard CRT `memcpy` call that's hard to distinguish from other copies
3. Done by the function at 0x4D7B3C or its parent before entering the battle arrangement loop

## What We Need

1. **The exact function and instruction** that copies the 428-byte block (`battle_order[32]` + `items[198*2]`) from savemap (0x1CFE77C) to the thread stack working buffer.

2. **The function that performs swaps in the working buffer** during active battle arrangement. Since 0x4AD0C0 writes to hardcoded savemap addresses, there must be a different swap path that operates on the stack copy. OR: does the engine temporarily redirect 0x4AD0C0's reads/writes somehow?

3. **The function that copies the working buffer back** to savemap on battle arrangement exit.

4. **The address of the controller struct** (`esi` in the init code). If this is a static/global, we could read `[controller+0x24]` to get the battle_order pointer, which might point to the working buffer during active arrangement.

## Key Addresses for Reference

| What | VA | Notes |
|------|-----|-------|
| Savemap base | 0x1CFDC5C | |
| battle_order[32] | 0x1CFE77C | savemap + 0x0B20 |
| items[198] | 0x1CFE79C | savemap + 0x0B40 |
| Swap function | 0x4AD0C0 | Writes hardcoded savemap addrs |
| Item→Battle init | 0x4F80EB | Pushes both addresses, stores in [esi+0x20/0x24] |
| Setup function | 0x4F8180 | Processes order → display struct at 0x1D8DFF4 |
| Main controller | 0x4F81F0 | 0x2B4 stack frame, jump table at 0x4FBF5C |
| pMenuStateA | 0x1D76A9A | Menu state base |
| Display struct | 0x1D8DFF4 | 2 bytes per battle slot (expanded from order table) |
| Battle handler calls | 0x4D7B3C, 0x4D7C34, 0x4D7CC6 | Call 0x4AD0C0 |
| Sort/auto-arrange | 0x4EBA66 | Loops 0xC8 times calling 0x4AD0C0 |
| Character copy buffer | 0x1D8DC80 | 152-byte character struct working copy |
| menu_callbacks array | 0x00B87ED8 | |

## Technical Environment
- FF8_EN.exe: PE32, 32-bit x86, no ASLR, image base 0x400000
- .text section: VA 0x401000, size 0x768000
- Steam 2013 English (US) release, App ID 39150
- Running with FFNx v1.23.x overlay (hooks some rendering functions but not menu logic)

## SAVEMAP OFFSET CORRECTION
ChatGPT deep research typically assumes savemap header is 96 bytes (0x60). **The actual header is 76 bytes (0x4C).** All post-header offsets from prior research are 0x14 (20 bytes) too high. When using any research-provided savemap offsets: **subtract 0x14**.
