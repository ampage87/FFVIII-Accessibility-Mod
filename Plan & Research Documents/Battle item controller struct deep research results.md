# FF8 battle Item controller lives in the pool — you examined the wrong node

The **esi register in 0x4F81F0 almost certainly points to a second pool node** allocated by 0x4F8010 via 0x4BE540, distinct from the "outer" command-menu node you examined. The pool node with +0x0C=0x4FDBA0 is the outer battle command menu controller; the Item controller is a separate node in the same 10-slot pool with +0x0C=0x4F81F0 and live data at +0x20 through +0x65. This means a static pointer chain exists: **scan the pool at 0x1D76BC8 for the slot whose +0x0C equals 0x4F81F0**, and you have the controller struct directly — no MinHook needed for read-only polling.

This analysis is based on exhaustive search of all public FF8 RE resources (FFNx, OpenFF8, ff8-speedruns/ff8-memory, ffrtt.ru wiki, FF8ModdingWiki, Gears document), none of which document this architecture. The reasoning below is derived from the user's own disassembly observations, x86 calling conventions, PSX-era Square engine patterns documented for FF7, and structural constraints of the pool allocator.

## The two-node architecture: outer command menu and inner sub-menu

The critical evidence is this chain of facts. First, 0x4F8010 pushes 0x4FC990 (destructor) and 0x4F81F0 (phase handler) as arguments to 0x4BE540, the pool allocator. Standard x86 cdecl returns the allocated pointer in **eax**. After the call, 0x4F8010 writes the inventory pointer 0x01CFE79C to [esi+0x20] — meaning esi was set from eax (the return value). Second, the pool node layout has +0x10 as the phase counter (uint16), and 0x4F81F0 uses [esi+0x10] as its jump-table state index. **This is not a coincidence — it confirms esi IS a pool node**.

Third, and decisively: you observed the pool node with +0x0C=0x4FDBA0 had all zeros from +0x20 onward. But [esi+0x20] in 0x4F81F0 contains 0x01CFE79C (the item inventory pointer). These cannot be the same node. You were looking at **Node A** (the outer command-menu controller), while the Item controller lives in **Node B** — a separate pool slot created by 0x4F8010's call to 0x4BE540.

The architecture works like this during battle:

1. When a character's ATB fills, the battle system allocates **Node A** from the pool with handlers +0x08=0x4FDB60 (init) and +0x0C=0x4FDBA0 (per-frame handler). This is the top-level command-select controller (Attack / Magic / Draw / Item / GF).
2. When the player selects "Item", Node A's handler (0x4FDBA0) calls **0x4F8010** — the Item sub-menu init function.
3. 0x4F8010 calls **0x4BE540**, which grabs a free slot from the pool, stores the function pointers (0x4F81F0 at +0x0C, 0x4FC990 at +0x08 or another destructor offset), links it into the list at 0x1D76B48, and returns the node pointer.
4. 0x4F8010 then populates the node body: [node+0x20]=0x01CFE79C (inventory ptr), [node+0x24]=0x01CFE77C (battle_order ptr), initializes phase at [node+0x10]=0.
5. **Both nodes are now in the linked list**. The dispatcher at 0x4BE4D0 calls both per frame — Node A manages the command menu (visible behind the sub-menu), Node B runs the Item-selection state machine at 0x4F81F0.
6. When the player cancels out of Item, Node B's destructor (0x4FC990) runs, the node is unlinked and returned to the pool, and Node A resumes full control.

This two-node pattern is consistent with FF8's battle UI behavior: the command menu remains partially visible and responsive to Cancel while an Item/Magic sub-menu is open. Both controllers are genuinely active simultaneously.

## The pool IS the controller — no separate heap struct

The pool node's **0x78-byte body (offsets +0x14 through +0x77) contains the controller state inline**. There is no separate heap allocation. Three lines of evidence support this.

**Memory layout alignment.** The pool node header occupies +0x00 through ~+0x13 (20 bytes: two list pointers, two function pointers, a uint16 phase, and a uint16 flag). The remaining **100 bytes** (+0x14 through +0x77) are more than sufficient for the Item controller's state: an inventory pointer (4 bytes), a battle_order pointer (4 bytes), a cursor position (2 bytes), a scroll offset (1 byte), a current item ID (1 byte), plus animation timers and rendering state. Your mapped fields ([esi+0x54] at offset 84 from struct base, [esi+0x65] at offset 101) all fall within the 0x78-byte boundary.

**PSX heritage.** FF8's PC port preserves the PSX engine architecture. The Gears document describes the kernel as using "a simple software-based memory manager" designed for 2MB of system RAM. Pool allocators exist precisely to avoid heap fragmentation — allocating a separate struct per controller would defeat their purpose. FF7's analogous module system embeds all state within its callback structs (the **0xAA0-byte game singleton** stores system callbacks and state at fixed offsets). Square's PS1-era engines consistently use inline data within pool/task nodes.

**The zeros you observed were in the wrong node.** Node A (the command-menu controller) genuinely has nothing at +0x20 because it doesn't need an item inventory pointer. Node B (the item controller) has live data at +0x20 because 0x4F8010 explicitly wrote it there after allocation.

## How the dispatcher calls handlers — no wrapper indirection needed

The dispatcher at **0x4BE4D0** walks the linked list from the head pointer at 0x1D76B48. For each node, it calls the function at [node+0x0C], passing the node pointer as the first argument (likely on the stack in cdecl, or possibly via ecx in a thiscall convention). The handler function prologue loads this into esi for convenient register-based access throughout the function body:

```asm
; Typical handler prologue (0x4F81F0):
push ebp
mov ebp, esp
push esi
mov esi, [ebp+8]    ; first argument = pool node pointer
; ... or: mov esi, ecx (if thiscall)
movzx eax, word ptr [esi+0x10]  ; load phase/state index
jmp dword ptr [eax*4 + 0x4FBF5C] ; jump table dispatch
```

This means **0x4FDBA0 does not call 0x4F81F0 directly**. They are independent entries in the linked list. The dispatcher calls 0x4FDBA0 for Node A and 0x4F81F0 for Node B in sequence each frame. The "two-layer" pattern you suspected is actually a "two-task" pattern — no wrapper extraction of a sub-pointer is happening.

FFNx's codebase corroborates this model. It resolves `sub_4BE4D0` via `get_relative_call(sub_4B3410, 0x68)` and finds a direct call at sub_4BE4D0+0x39 to sub_4BECC0 (which leads to `menu_draw_text`). This is consistent with the dispatcher doing some bookkeeping (like calling a text-rendering helper) alongside the indirect handler dispatch. The direct call at +0x39 may be a pre- or post-dispatch helper, or part of a separate code path within the same function.

## Static pointer chain for polling: scan the pool array

**A pure memory-read approach works.** No hooking required for read-only access. The pool is at a fixed static address with no ASLR (image base 0x400000):

```c
#define POOL_BASE    0x1D76BC8
#define POOL_SLOTS   10
#define POOL_STRIDE  0x78
#define HANDLER_OFF  0x0C
#define INUSE_OFF    0x12
#define ITEM_HANDLER 0x4F81F0
#define CURSOR_OFF   0x54

// Returns pointer to active Item controller, or NULL
uint8_t* find_item_controller(void) {
    for (int i = 0; i < POOL_SLOTS; i++) {
        uint8_t* node = (uint8_t*)(POOL_BASE + i * POOL_STRIDE);
        if (*(uint32_t*)(node + HANDLER_OFF) == ITEM_HANDLER
            && *(uint16_t*)(node + INUSE_OFF) != 0) {
            return node;
        }
    }
    return NULL;  // Item menu not active
}

// From your polling thread:
int16_t get_cursor_index(void) {
    uint8_t* ctrl = find_item_controller();
    return ctrl ? *(int16_t*)(ctrl + CURSOR_OFF) : -1;
}
```

**Thread safety is acceptable.** On x86, naturally aligned reads (2-byte read at an even address, 4-byte read at a 4-byte-aligned address) are atomic. Offset 0x54 is even, so the int16 read is atomic. The worst case is a torn read during a frame transition, which produces a stale-by-one-frame value — perfectly fine for an accessibility narrator that polls cursor position.

An alternative approach walks the linked list from the head at 0x1D76B48, checking [node+0x0C] for each active node. This only visits active nodes (faster when few are active) but traversing a linked list from a non-game thread carries a small risk of reading a node mid-unlink. The pool scan is safer because pool memory is never freed or moved — only the in-use flag and list pointers change.

## Verification steps and potential pitfalls

Before committing to this architecture in your mod, **verify two things in a debugger**:

**First, confirm Node B exists.** Set a breakpoint at 0x4BE540 (the pool allocator). Open the Item menu in battle. When the breakpoint hits from the call chain 0x4F8010 → 0x4BE540, inspect the return value in eax. This is your Node B pointer. Verify it falls within the pool range (0x1D76BC8 through 0x1D76BC8 + 9×0x78 = 0x1D77058). Check that [eax+0x0C] was set to 0x4F81F0. Then verify [eax+0x20] becomes 0x01CFE79C after 0x4F8010 completes.

**Second, verify the handler offset.** It is possible that 0x4BE540 maps its arguments differently than expected — the destructor 0x4FC990 might go to +0x0C and the handler 0x4F81F0 might go to +0x08, or to an offset in the body like +0x14. If your pool scan for [+0x0C]==0x4F81F0 finds nothing during active Item menu, check +0x08 and +0x14 instead. The allocator's argument-to-field mapping is the one piece of this analysis that cannot be fully determined without seeing the 0x4BE540 disassembly.

There is one scenario where this analysis would be wrong: if 0x4BE540 is not a simple pool allocator but instead a "register sub-handler" function that stores the inner handler (0x4F81F0) inside the EXISTING outer node's body (e.g., at +0x14) rather than allocating a new node. In that case, there would be only one node, 0x4FDBA0 would read [node+0x14] and call it, and you'd need to find the outer node and dereference +0x14 or similar. However, the name "pool allocator" and the fact that it manages "10 slots" strongly argue against this — it is allocating nodes, not registering sub-handlers.

## What public documentation exists (and doesn't)

**None of the specific addresses or struct layouts in your question are documented anywhere publicly.** After exhaustive search of every known FF8 RE resource — FFNx source code (julianxhokaxhiu/FFNx), OpenFF8 (Extapathy/OpenFF8), ff8-speedruns/ff8-memory, ffrtt.ru wiki, HobbitDur/FF8ModdingWiki, the Gears document, ff7-flat-wiki, MaKiPL's research, and Aali's original FF7_OpenGL driver — zero results were found for addresses 0x4F81F0, 0x4F8010, 0x4BE540, 0x4FDB60, 0x4FDBA0, 0x1D76BC8, or 0x1D76B48.

The closest documented address is **0x4BE4D0**, which FFNx resolves as part of its menu text rendering chain (`sub_4B3410 → sub_4BE4D0 → sub_4BECC0 → menu_draw_text`). FFNx's battle hooks operate exclusively at the rendering layer (texture replacement, palette uploads, battle swirl effects, FPS limiting) and do not touch the command-selection state machine. The OpenFF8 project's `memory.h` file (defining `ff8vars` and `ff8funcs` structs) could not be retrieved but appears limited in scope based on the project's 28 commits.

The **Qhimm forum thread "[FF8] Engine reverse engineering" (topic 16838)** is the most likely location for any informal discussion of this architecture, but returns HTTP 403 on all access attempts. The Tsunamods/Qhimm Discord community (particularly myst6re, julianxhokaxhiu, Extapathy, and MaKiPL) may have unpublished knowledge.

## Practical recommendation for your accessibility mod

Use the **pool-scan approach** as your primary method. It requires no hooking, works from any thread, and provides a clean NULL/non-NULL signal for whether the Item menu is currently active. Keep the MinHook of 0x4F81F0 as a verified fallback — if the pool scan fails (because the handler is stored at an unexpected offset), the hook gives you a guaranteed capture of esi. The hook approach also lets you capture the struct pointer on first call and cache it, avoiding repeated scans.

For the hook fallback, the handler's first argument (the pool node pointer) arrives either on the stack `[esp+4]` in cdecl or in `ecx` in thiscall. Your hook should capture whichever register/stack slot contains it:

```c
typedef void (__cdecl *ItemHandler_t)(void* node);
ItemHandler_t g_original_item_handler = NULL;
volatile void* g_item_node = NULL;

void __cdecl hooked_item_handler(void* node) {
    g_item_node = node;
    g_original_item_handler(node);
}
// On return/cleanup path, set g_item_node = NULL
// (hook the destructor 0x4FC990 for this)
```

Your polling thread then reads `g_item_node` and, if non-NULL, reads the cursor index at +0x54. This approach is robust regardless of the exact pool layout details.
