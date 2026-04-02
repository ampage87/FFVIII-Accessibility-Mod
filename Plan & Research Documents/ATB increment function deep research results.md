# FF8 ATB increment function: narrowed but not pinpointed

**No publicly documented disassembly of the ATB increment function exists for FF8_EN.exe.** After exhaustive research across FFNx source code, Qhimm wiki/forums, OpenVIII, ff8-speedruns memory tables, FearLess Revolution cheat tables, and the broader reverse engineering community, the exact function address has never been published. However, the search space has been narrowed to a tight window within the `sub_4A94D0` call tree, and the formula, entity layout, and a reliable method to locate the function are all fully documented below.

## The ATB formula is fully reverse-engineered

ForteGSOmega's Battle Mechanics FAQ (the definitive FF8 RE source, based on PS1 binary analysis) confirms the per-tick increment logic your hook needs to intercept:

```
BarSize    = BattleSpeed × 4000          (default BattleSpeed=3 → 12000, matching your finding)
BarIncrement = (Spd + 30) × SpeedMod / 2
```

**SpeedMod** is determined by status flags at entity+0x00: **1** if Slow (bit 2 set), **2** if normal, **3** if Haste (bit 1 set). Stop (bit 3) and Sleep (bit 0) freeze ATB entirely — no increment occurs. The Speed stat lives at **entity+0xB9** (confirmed: Ally 1 at absolute VA `0x1D27BD1` = base `0x1D27B18` + `0xB9`). The tick rate is **60Hz** internal game logic, meaning at the 15fps battle render rate (confirmed in FFNx's `ff8_opengl.cpp`), the engine processes **4 ATB ticks per rendered frame**.

Wait Mode checks the config byte you identified (`0x1CFE73B`) combined with the menu phase byte (`0x1D768D0`). When config byte = 1 (Wait) **and** a sub-menu is open (menu phase = 32), ATB increments are skipped for all entities. This check is inline — there is no separate pause flag.

## The call chain narrows the search to sub_4A94D0's internals

FFNx's `ff8_data.cpp` confirms and extends the call chain you identified. The critical derivation path is:

```
sub_4A94D0                          ← battle update dispatcher
  └─ (+0x1E0) call → sub_4BCBE0    ← intermediate handler  
       └─ (+0x8E) call → sub_4C8B10 ← leads to pause system
            └─ (+0x03) jmp → battle_pause_sub_4CD140 (0x4CD140)
                 └─ (+0x225) call → battle_pause_window_sub_4CD350
```

**The ATB increment almost certainly lives inside `sub_4A94D0` itself or in a callee invoked before the `+0x1E0` call to `sub_4BCBE0`.** The reasoning: `sub_4BCBE0` leads directly to the pause/menu system (`sub_4C8B10` → `battle_pause_sub_4CD140`), which is UI logic, not ATB math. The ATB update loop — reading Speed stats, checking status flags, computing increments, writing to entity+0x0C — must execute before or parallel to that pause chain. Look in the **first ~0x1DF bytes of `sub_4A94D0`** for the ATB loop, or in functions called from that region.

The function `sub_47CCB0` is the top-level battle dispatch anchor. FFNx derives the battle texture loader from `sub_47CCB0 + 0x98D` and a card-game check from its jump table, confirming it's a large switch/dispatch function. The ATB update path branches off somewhere between `sub_47CCB0` and `sub_4A94D0`.

## Entity structure confirmed from two independent sources

The ff8-speedruns Cheat Engine table and your own runtime testing agree exactly:

| Field | Offset | Size (ally/enemy) | Absolute VA (Ally 1) |
|-------|--------|-------------------|---------------------|
| Status flags (Sleep/Haste/Slow/Stop/Regen/Protect/Shell/Reflect) | +0x00 | 1 byte (bitfield) | 0x1D27B18 |
| Max ATB | +0x08 | uint16 / uint32 | 0x1D27B20 |
| **Current ATB** | **+0x0C** | **uint16 / uint32** | **0x1D27B24** |
| Current HP | +0x10 | uint16 / uint32 | 0x1D27B28 |
| Speed stat | +0xB9 | uint8 | 0x1D27BD1 |

Entity stride is **0xD0** (208 bytes), 7 slots total. French version offsets are uniformly **−0x328** from English. The entity array pointer at `0x1D27B10` (a `BYTE**`) is referenced in FFNx as `battle_char_struct_dword_1D27B10`, extracted from the monster name function `battle_get_monster_name_sub_495100`.

## How to find the function: hardware write breakpoint on entity+0x0C

Since no published disassembly exists, the definitive method is a **Cheat Engine hardware write breakpoint** (or x64dbg/OllyDbg equivalent):

1. Attach to `FF8_EN.exe`, enter a battle, set a **4-byte hardware write breakpoint on `0x1D27B24`** (Ally 1 Current ATB).
2. The breakpoint will fire every frame (or every 4 ticks per frame at 15fps). The instruction pointer when it fires is the ATB write instruction.
3. **Expected instruction pattern**: You're looking for an `add` or `mov` that writes to a register-indexed address like `[esi+0x0C]` or `[ebx+0x0C]` where the base register points to the entity. The value being added should be the result of `(Spd + 30) * SpeedMod / 2`.
4. Scroll up from the write instruction to find the function prologue (`push ebp; mov ebp, esp` or `sub esp, ...`). That's the function entry point.
5. To confirm you've found the right function, look for these telltale patterns in the surrounding code:
   - A **loop counter or index** from 0 to 6 (7 entities), with the entity pointer advancing by `0xD0` each iteration
   - A read from `entity+0xB9` (the Speed stat) followed by `add reg, 0x1E` (adding 30)
   - A test of `entity+0x00` bit 3 (Stop check: `test byte ptr [entity], 0x08`) with a conditional jump to skip the increment
   - A test of bit 1 and bit 2 (Haste/Slow: `test byte ptr [entity], 0x02` / `0x04`) to select SpeedMod
   - A `shr` or `sar` by 1 (the divide-by-2 step)
   - A `cmp` of the result against entity+0x08 (Max ATB cap check)

## Expected function signature and structure

Based on FF8's PS1 heritage (MSVC-compiled C, no C++ vtables in battle code) and the entity array design:

```c
// Most likely: a single function that loops over all entities
void __cdecl atb_update_all(void);    // no args; reads entity array from global pointer

// Less likely: called per-entity from an outer loop  
void __cdecl atb_update_entity(uint8_t* entity);  // entity pointer in arg
```

FF8's PC port uses **`__cdecl`** calling convention throughout (confirmed by FFNx's hook declarations and the MSVC 6.0 compilation). The function is almost certainly called from `battle_main_loop`'s dispatch chain, not from a callback table. If it takes no arguments and accesses the global entity array directly (via `0x1D27B10` or `0x1D27B18`), the disassembly will show something like:

```asm
mov  esi, dword ptr [0x1D27B10]   ; load entity array base pointer
xor  ecx, ecx                     ; entity index = 0
.loop:
  ; check if entity is active/alive
  ; check Stop/Sleep status at [esi+0x00]
  ; read Speed from [esi+0xB9]
  ; compute (Spd + 30) * SpeedMod / 2
  ; add result to [esi+0x0C]        ← THIS IS THE WRITE YOU'LL HIT
  ; compare [esi+0x0C] with [esi+0x08]
  ; clamp if needed
  add  esi, 0xD0                   ; next entity
  inc  ecx
  cmp  ecx, 7
  jl   .loop
```

## Where the Wait Mode check likely lives

The user confirmed there is no separate ATB pause flag — the engine checks config + menu phase inline. Two plausible locations:

**Option A (most likely):** The Wait Mode check is **above** the ATB loop, at the top of the ATB update function or in its caller. Pseudocode:

```c
if (config[3] == 1 && menu_phase == 32)  // Wait Mode + sub-menu open
    return;  // skip all ATB increments this frame
// ... then run the entity loop
```

In assembly, look for `cmp byte ptr [0x1CFE73B], 0` / `jne` combined with `cmp byte ptr [0x1D768D0], 0x20` near the function entry, before the entity loop begins. This would be the cleanest hook point — a single-instruction NOP/patch at the conditional jump could freeze all ATB.

**Option B:** The check is **inside** the per-entity loop, evaluated for every entity every tick. This is less efficient but would match the PS1 architecture where the check and increment were interleaved.

## A PSX anchor point exists at 0x0A5736

A PS1 GameShark "Super Active ATB" code patches PSX address **0x0A5736** with a NOP (`0x2400` = MIPS `addiu $zero, $zero, 0`), which bypasses the ATB-full check to allow immediate actions. While PC addresses differ completely, this confirms the ATB logic lives in the PS1 battle overlay code section around that address. The PC equivalent would be in the same logical position in the call chain — likely within or just above `sub_4A94D0`.

## Recommended hook strategy

Once you locate the function entry point via the hardware breakpoint method:

**If the function loops over all entities internally** (most likely), hook its entry with MinHook and conditionally `return` early when your Enhanced Wait Mode condition is true. This is cleanest — one hook freezes all ATB:

```c
typedef void (__cdecl *atb_update_fn)(void);
atb_update_fn original_atb_update;

void __cdecl hooked_atb_update(void) {
    uint8_t menu_phase = *(uint8_t*)0x1D768D0;
    // Enhanced Wait: freeze ATB when ANY command menu is showing (phase 0 or 32)
    if (menu_phase == 0 || menu_phase == 32)
        return;  // skip all ATB increments
    original_atb_update();
}
```

**If the function is called per-entity**, hook the caller's loop instead, or hook the per-entity function and check which entity is being updated:

```c
void __cdecl hooked_atb_update_entity(uint8_t* entity) {
    uint8_t menu_phase = *(uint8_t*)0x1D768D0;
    if (menu_phase == 0 || menu_phase == 32)
        return;
    original_atb_update_entity(entity);
}
```

## What existing tools prove is possible but don't reveal

The FearLess Revolution Cheat Engine table for FF8 Remastered (by DrummerIX, updated by Noobzor) includes an **"ATB Multiplier for Allies and Enemies"** script added in version 3.6. This script necessarily hooks the ATB increment instruction via AOB scan inside `FFVIII_EFIGS.dll` (the Remastered version's game logic DLL). The script uses the same `aobscanmodule` + code injection pattern visible in other scripts from the same table. Unfortunately, the .CT file is a downloadable attachment (not inline text), and the forum returns 403 errors. **Downloading this .CT file and searching for "ATB" in the XML would immediately reveal the AOB pattern and original instruction bytes for the Remastered version**, which could then be cross-referenced to the 2013 Steam version's equivalent code.

The 2013 Steam version's Squall8 table has "Max ATB Gauge" and "Enemies Won't Attack" features that also necessarily reference the ATB write addresses — these tables are downloadable from the FearLess thread (t=1029).

## Bottom line: the definitive method in under 5 minutes

Set a hardware write breakpoint on **`0x1D27B24`** (Ally 1 Current ATB). The very first break during normal ATB filling gives you the increment instruction address. Walk up to the function prologue for the hook point. Look for the Wait Mode conditional (`cmp byte ptr [0x1CFE73B], 0`) near the entry for context on where the engine's own check lives. The function will be somewhere in the **0x4A0000–0x4C8B10 address range**, called from the `battle_main_loop` → `sub_47CCB0` → `sub_4A94D0` chain.
