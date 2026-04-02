# Deep Research: FF8 Battle Item Menu Controller Struct Location

## Context

We're building a blind-accessibility mod for FF8 (Steam 2013 edition, FF8_EN.exe, App ID 39150) + FFNx v1.23.x. We need to announce the item name and quantity when the player navigates the battle Item sub-menu. The field Item menu uses a display struct at `0x1D8DFF4` which works perfectly — but during **battle**, this struct is all zeros. The battle item menu uses a completely different controller mechanism.

## What We Know From Disassembly

### Battle item controller function: `0x4F81F0`

This is the main per-frame handler for the battle item sub-menu. It's called with `push esi; call 0x4F81F0` where `esi` = pointer to the controller struct. The function is a state machine dispatched via a jump table at `0x4FBF5C` (indexed by `[esi+0x10]`, up to 0x72 phases).

Key struct fields confirmed from disassembly:
- `[esi+0x10]` (uint16) — current phase/state index for the jump table
- `[esi+0x20]` (uint32) — pointer to item inventory, set to `0x01CFE79C` at `0x4F80F5`
- `[esi+0x24]` (uint32) — pointer to battle_order, set to `0x01CFE77C` at `0x4F80FC`
- `[esi+0x54]` (int16) — **absolute inventory index** — the engine's resolved cursor position into the 198-slot inventory, accounting for battle_order mapping and scroll offset
- `[esi+0x61]` (uint8) — related to page/scroll position
- `[esi+0x65]` (uint8) — current item ID at cursor position

Phase 5 (browsing) at `0x4F82BD`:
```asm
movsx ecx, word ptr [esi + 0x54]  ; absolute inventory index
... (navigation math with div/mod by 11) ...
mov word ptr [esi + 0x54], ax     ; update after navigation
movsx edx, word ptr [esi + 0x54]  ; re-read
mov eax, dword ptr [esi + 0x20]   ; inventory pointer (0x1CFE79C)
lea ecx, [eax + edx*2]            ; inventory[absIdx * 2]
mov al, byte ptr [ecx]            ; item ID
mov byte ptr [esi + 0x65], al     ; store current item ID
```

### Controller allocation: `0x4BE540`

The controller struct is allocated from a **pool** of 10 slots starting at `0x1D76BC8` with stride `0x78`. Each slot has:
- `+0x00` (uint32) — next pointer (linked list)
- `+0x04` (uint32) — prev pointer (linked list)
- `+0x08` (uint32) — init function pointer
- `+0x0C` (uint32) — per-frame handler function pointer (called by dispatcher at `0x4BE529`)
- `+0x10` (uint16) — phase counter
- `+0x12` (uint8) — in-use flag

The linked list head is at `0x1D76B48`.

### What we observed at runtime

When we walked the linked list during an active Item sub-menu, we found only **one node** in the list — and it had `+0x08=0x4FDB60` and `+0x0C=0x4FDBA0`, NOT `0x4F81F0`. The entire struct from offset +0x20 onward was zeros.

This means `0x4F81F0` is NOT directly registered as a linked list handler. Instead, `0x4FC990` (the destructor, pushed at `0x4F8012`) or `0x4FDB60`/`0x4FDBA0` is the outer handler, which **internally** calls `0x4F81F0` with a different `esi` pointer — likely a sub-struct or a separately heap-allocated controller.

### The initialization chain

```
0x4F8010:  ; Item menu init
  push 0x4FC990      ; destructor
  push 0x4F81F0      ; phase handler
  call 0x4BE540      ; allocate pool slot → returns edx (pool node)
  mov esi, eax       ; esi = pool node?? or different?
  ...
  mov [esi+0x20], 0x1CFE79C  ; write inventory pointer at esi+0x20
```

The critical question: the `esi` that holds the inventory pointer at `+0x20` — is it the same address as the pool node returned by `0x4BE540`, or is it a different struct? The pool node dump showed `+0x20=0x00000000`, so either:
1. The pool node IS the struct but +0x20 was zeroed by the time we read it, OR
2. `esi` in `0x4F8010` is re-assigned AFTER `0x4BE540` returns, pointing to a different allocation

### Inventory at `0x01CFE79C`

This is the runtime address of the savemap item inventory (198 × 2-byte entries: item_id + quantity). Savemap base is `0x1CFDC5C`, inventory is at savemap offset `+0x0B40` (after applying the confirmed 0x14 correction from the 96→76 byte header difference).

**IMPORTANT savemap offset correction**: ChatGPT deep research assumes 96-byte (0x60) savemap header. Actual header is 76 bytes (0x4C). All post-header research offsets are 0x14 too high — subtract 0x14 from any wiki-sourced offsets.

## What We Need

1. **Where does the controller struct pointer (`esi` in `0x4F81F0`) actually live at runtime?** Is there a static pointer chain from the pool node or the linked list head that reaches this heap-allocated struct? For example, does the pool node at `0x1D76BC8` contain a pointer at some offset that points to the actual controller struct used by `0x4F81F0`?

2. **What is the relationship between the pool node returned by `0x4BE540` and the `esi` used in `0x4F81F0`?** The `0x4F8010` function calls `0x4BE540` and then does setup on `esi`. Is `esi` the return value of `0x4BE540`, or does it come from somewhere else?

3. **How does the outer handler (`0x4FDB60` or `0x4FDBA0`) invoke `0x4F81F0`?** What struct does it pass? Is there a wrapper that extracts a sub-pointer from the pool node?

4. **Is there ANY static address that holds a pointer to the active battle Item controller struct during browsing?** We need to be able to read `[struct+0x54]` (absolute inventory index) from our mod's polling thread.

## What Would Be Most Helpful

- Disassembly analysis of `0x4FDB60` and `0x4FDBA0` to see how they call `0x4F81F0`
- Disassembly of `0x4F8010` (full function) to trace where `esi` comes from
- Disassembly of the dispatcher at `0x4BE4D0-0x4BE537` to see what arguments it passes
- Any public RE knowledge about FF8's battle menu controller architecture (Qhimm forums, ffrtt.ru wiki, myst6re's work, Deling source code)
- The Hyne save editor source (myst6re/hyne) or Deling source for any battle menu struct definitions

## Key Addresses Summary

| Address | Description |
|---------|-------------|
| 0x4F81F0 | Battle item phase handler (the function we need esi from) |
| 0x4F8010 | Battle item init (calls 0x4BE540, sets up controller) |
| 0x4BE540 | Pool allocator (10 slots at 0x1D76BC8, stride 0x78) |
| 0x4BE4D0 | Dispatcher (walks linked list from 0x1D76B48, calls +0x0C) |
| 0x4FDB60 | Outer init handler (registered at pool node +0x08) |
| 0x4FDBA0 | Outer per-frame handler (registered at pool node +0x0C) |
| 0x4FC990 | Destructor (pushed before 0x4F81F0 in 0x4F8010) |
| 0x1D76B48 | Linked list head pointer |
| 0x1D76BC8 | Pool array base (10 × 0x78 byte slots) |
| 0x01CFE79C | Item inventory (198 × {id,qty} byte pairs) |
| 0x01CFE77C | battle_order[32] (inventory slot indices) |
| 0x4FBF5C | Phase jump table (0x72 entries for 0x4F81F0) |

## Our Fallback Plan

If no static pointer chain exists, we plan to MinHook `0x4F81F0` to capture the `esi` argument on every call and stash it in a global variable that our mod thread can read. This is guaranteed to work but adds a hook. Knowing whether a simpler static-pointer approach exists would be valuable.
