# Deep Research: FF8 ATB Increment Function Address

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 English edition, FF8_EN.exe, no ASLR, module base 0x400000). I need to find the function that increments ATB (Active Time Battle) gauges each frame during battle so I can hook it to implement an "Enhanced Wait Mode" that freezes all ATB when a player command menu is showing.

Currently I'm using a brute-force approach (snapshot ATB values and write them back every frame), but hooking the actual increment function would be much cleaner and more closely emulate how the game's own Wait Mode works.

## What I Know (CONFIRMED through runtime testing)

### Entity Array
- **Entity array base**: process VA `0x1D27B18`, stride `0xD0` (208 bytes), 7 slots
  - Slots 0-2: allies, slots 3-6: enemies
- **Entity array pointer** (BYTE**): `0x1D27B10` (but FFNx hooks the resolution, the data is at the static address)
- **ATB current**: entity + `0x0C` — `uint16` for allies, `uint32` for enemies
- **ATB max**: entity + `0x08` — `uint16` for allies, `uint32` for enemies
- When ATB current >= ATB max, the character is ready to act
- ATB max is typically 12000 for all entities

### Battle Function Call Chain (from FFNx source at github.com/julianxhokaxhiu/FFNx)
```
battle_main_loop (resolved from main_loop + 0x340)
  → sub_47CCB0 (at battle_main_loop + 0x1B3)
    → sub_47D890 → sub_4A94D0 → sub_4BCBE0 → sub_4C8B10
```

Key resolved addresses from FFNx `ff8_data.cpp`:
- `battle_enter`: from `main_loop + 0x330`
- `battle_main_loop`: from `main_loop + 0x340`
- `sub_47CCB0`: from `battle_main_loop + 0x1B3`
- `sub_47D890`: from `sub_47CCB0` (deeper in call chain)
- `sub_4A94D0`: from `sub_47D890 + 0x9`
- `battle_pause_sub_4CD140`: from `sub_4C8B10 + 0x3`
- `battle_pause_window_sub_4CD350`: from `battle_pause_sub_4CD140 + 0x225`

### Active/Wait Mode Config
- Savemap `config[20]` array at savemap offset `+0xADC` (runtime `0x1CFE738`)
- `config[3]` at runtime `0x1CFE73B`: **0 = Active mode, 1 = Wait mode**
- In Wait Mode, ATB freezes when a sub-menu (Magic/GF/Draw/Item) is open but continues on the top-level command menu
- There is NO separate "ATB pause flag" — confirmed by scanning 36KB of battle state memory. The engine checks the config byte + menu phase inline.

### Menu Phase Byte
- Address: `0x01D768D0`
- Values during player turn: 0 (command menu), 32 (sub-menu open), 64 (Limit Break), 3 (transition), 11 (target select), 14+ (action executing)

### SAVEMAP OFFSET CORRECTION
**IMPORTANT**: Some community documentation and ChatGPT deep research results assume the FF8 savemap header is 96 bytes (0x60). I have CONFIRMED the header is actually **76 bytes (0x4C)**. All post-header offsets from such research are **0x14 (20 bytes) too high**. When using research offsets for anything after the header: **subtract 0x14**. 

Confirmed savemap base at runtime: `0x1CFDC5C`. GFs at +0x4C, chars at +0x48C, config at +0xADC.

## What I Need

1. **The address of the function that increments entity+0x0C (ATB current value) each battle frame.** This is the function I want to hook with MinHook to conditionally skip the increment.

2. **Its calling convention and signature.** Is it `__cdecl`? Does it take an entity pointer as an argument, or does it loop over all entities internally? Does it take a slot index?

3. **What condition checks exist inside or above it?** Specifically:
   - Does it check the Active/Wait config byte before incrementing?
   - Does it check for Stop status (which also freezes ATB for a single entity)?
   - Does it apply Haste (2x speed) or Slow (0.5x speed) modifiers?
   - Does it check entity+0x08 (max ATB) as a cap?
   - Is there a "battle paused" check (for the Pause menu)?

4. **Where in the call chain does the Wait Mode check happen?** Is it inside the ATB increment function itself, or does a higher-level function decide whether to call the increment function at all? This determines the best hook point.

5. **Is there a single function that updates ATB for ALL entities in a loop, or does each entity get updated by separate calls?** If it's a loop, hooking the outer function and skipping the entire loop would be cleanest.

## Sources to Check

- **Qhimm wiki**: wiki.ffrtt.ru — FF8 battle system documentation
- **Qhimm forums**: forums.qhimm.com — FF8 reverse engineering threads, especially any discussing ATB mechanics or battle speed
- **ff8-speedruns**: github.com/ff8-speedruns/ff8-memory — Cheat Engine tables with battle addresses
- **FFNx source**: github.com/julianxhokaxhiu/FFNx — `src/ff8_data.cpp` has the battle function chain
- **Doomtrain**: FF8 data editor documentation — kernel.bin battle mechanics
- **Hext tools/mods**: Any FF8 battle speed mods that patch ATB increment code
- **OpenVIII**: github.com/MaKiPL/OpenVIII — open-source FF8 reimplementation that may document the ATB update logic

## Ideal Answer Format

```
ATB increment function: 0x00XXXXXX
Signature: void __cdecl atb_update(uint8_t* entity_ptr) [or whatever it actually is]
Called from: 0x00YYYYYY (the per-frame battle update loop)
Wait Mode check at: 0x00ZZZZZZ (cmp byte ptr [0x1CFE73B], 0 / jne skip_increment)
```

If the exact address can't be determined from documentation alone, any narrowing of the search space would be valuable — e.g., "the ATB update is called from within sub_4A94D0 at offset +0xNN" or "look for code that reads from [0x1D27B10] and adds to offset +0x0C in a loop of 7 iterations."
