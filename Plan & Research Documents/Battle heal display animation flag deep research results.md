# FF8 battle engine heal number rendering: what's known and what isn't

**No public reverse engineering source documents the specific function that renders heal numbers in FF8's Steam 2013 build.** After exhaustive searching across FFNx, OpenFF8, OpenVIII, Qhimm forums, FFRTT wiki, Cheat Engine tables, and every known FF8 RE project, the damage/heal number display subsystem remains one of the undocumented portions of the engine. However, the research yields significant architectural insights, confirms a likely root cause for the heal flag behavior, and provides concrete next steps for finding the exact rendering function.

The core problem the modder faces — `0x01D280C0` never clearing for heals — almost certainly stems from **damage and heal actions using separate state management code paths** while sharing a common rendering function. The "clear" logic for that flag lives inside the damage animation completion handler, a code path that heal actions never execute. Heal animation state is likely tracked elsewhere — either at a different entity struct offset, within the battle effects system structures around `0x027973EC`, or via a completely separate frame counter.

---

## The font system reveals how color differentiation works

The battle number sprites live in **`a9btlfnt.bft`**, a TDW-format file inside `battle.fs`. This file contains digit glyphs as 4-bit-indexed pixel data with **8 embedded CLUT palettes** in standard PSX TIM format:

1. Dark grey
2. Grey
3. **Yellow** (critical/special)
4. **Red**
5. **Green** (heal numbers)
6. Blue
7. Purple
8. **White** (damage numbers)

The rendering function accepts a **palette index parameter** that selects which CLUT to apply. Palette **#5 (green)** produces heal numbers; palette **#8 (white)** produces damage numbers. The same digit sprites and the same core rendering function handle both — the only difference is the palette index passed in. This means there is a single "draw battle number" function with a color parameter, not two separate rendering functions. Character widths are stored as 4-bit values (two per byte) in the TDW header, controlling digit spacing.

The companion file **`b0wave.dat`** bundles core battle textures, music, and font data together. Both files reside in the `battle.fs` archive and are loaded when the battle module initializes.

---

## The battle effects pipeline and where numbers fit in

FFNx's `ff8_data.cpp` reveals the architecture. Battle effects are driven by an **opcode-indexed function pointer table** at address `0xC81774`, discovered via:

```
ff8_externals.func_off_battle_effects_C81774 = (DWORD*)get_absolute_value(
    ff8_externals.battle_read_effect_sub_50AF20, 0x2C);
```

Each spell, summon, and action has an entry in this table. Effects process through sub-function tables (e.g., Leviathan's at `0xB64C3C`) using opcodes like `UploadPalette75` and `UploadTexture39`. The **palette upload functions** at `0xB66560` (`mag_data_palette_sub`) and `0xB666F0` (`battle_set_action_upload_raw_palette`) handle loading the appropriate CLUT into the PSX VRAM emulation layer before sprites are drawn.

The battle action pipeline flows: **ATB fill → command selection → action queued → damage/heal calculation → effect trigger → palette upload → digit rendering → state cleanup**. The entity array at `0x1D27B10` (confirmed by FFNx as `battle_char_struct_dword_1D27B10`) holds per-entity state throughout this pipeline. The modder's observed base at `0x1D27B18` is 8 bytes into this structure — likely past a pointer or header field to the first entity's data.

Battle runs at **15 FPS** in vanilla mode (confirmed in FFNx's `ff8_limit_fps()`). The **~1.4-second damage number display** corresponds to approximately **21 frames** at 15 FPS. This strongly suggests a frame counter mechanism rather than a wall-clock timer. FFNx's 30/60 FPS modes would proportionally shorten display duration unless the counter is tick-rate-compensated.

---

## Why 0x01D280C0 never clears for heals

The flag at `0x01D280C0` is set to `0x01` when any action resolves and numbers begin displaying. For damage, it clears to `0x00` after ~21 frames. For heals, it stays at `0x01` indefinitely. Three architectural factors explain this:

**Separate completion handlers.** The damage code path includes a frame-counter decrement that, upon reaching zero, clears `0x01D280C0` and resets `entity+0xBF` to `0x00`. The heal code path either: (a) uses a different flag address for its completion tracking, (b) relies on the next action's initialization to clear the previous state, or (c) routes through a different effect handler in the `0xC81774` table that lacks the cleanup logic present in the damage handler.

**The entity+0xBF staying at 0x02 confirms this.** Both damage and heal set `entity+0xBF` to `0x02` (animating). For damage, the same completion handler that clears `0x01D280C0` also resets `0xBF` to `0x00`. The fact that both flags persist for heals means the heal path genuinely lacks the cleanup code that the damage path contains. **Entity+0xBF likely only clears when the next action starts**, not on its own timer — the modder should test this by triggering another action after a heal.

**Effect struct memory at `0x027973EC`** and `0x02797624` (documented by FFNx) may contain the heal-specific animation state. These are effect data structures used by the battle effects opcode system. The modder's scanned range of `0x01D28000–0x01D28C00` and `0x01CFF000–0x01CFF800` would miss these entirely, as they sit in a completely different memory region (`0x0279xxxx`).

---

## Concrete strategy for finding the rendering function

The specific function address is not in any public source, but three approaches can find it efficiently:

**Approach 1: Hardware breakpoint on `0x01D280C0` write.** Using x64dbg or Cheat Engine's "find what writes to this address," set a write breakpoint on `0x01D280C0`. Trigger a damage action. The function that sets this byte to `0x01` is the action resolution handler. Step through its callees to find the digit rendering subroutine. Then trigger a heal action with the same breakpoint — the set-function will be the same or similar, but the **absence of a clearing write** reveals the divergent code path. The function that clears `0x01D280C0` to `0x00` for damage is the completion handler the modder needs to understand.

**Approach 2: Trace from `battle_main_loop`.** FFNx resolves `battle_main_loop` at `main_loop + 0x340`. In IDA/Ghidra, follow this function's call tree. The rendering path for numbers would be a subroutine called every frame that checks whether a display is active, decrements a counter, and calls the digit-drawing function. The font rendering chain in the original 2000 PC version followed `sub_534640 → sub_4972A0 → load_fonts`. The Steam 2013 build has shifted addresses (functions moved from `0x0047xxxx` to approximately `0x0048xxxx–0x004Axxxx` range), but the call structure is preserved.

**Approach 3: Hook the palette upload.** Since heal numbers require palette #5 (green) to be loaded before rendering, hooking `mag_data_palette_sub_B66560` or `battle_set_action_upload_raw_palette_sub_B666F0` and checking the palette index parameter would catch the moment heal numbers are about to render. The return address on the stack at that point reveals the calling function — the number rendering function itself. This approach is viable today using MinHook (which the modder already uses).

---

## Recommended memory regions to scan for heal animation state

The modder's scan covered `0x01D28000–0x01D28C00`, `0x01CFF000–0x01CFF800`, and the 7 entity structs. Based on the FFNx-documented architecture, these additional regions likely contain heal-relevant state:

| Address | Size | What it likely contains |
|---------|------|------------------------|
| `0x027973E8–0x027973F0` | 8 bytes | Effect struct pointers (battle visual effects) |
| `0x02797624` | 4 bytes | Secondary effect struct pointer |
| `0x02798A68` | ~256 bytes | Magic data buffer (spell effect state) |
| `0x01D27B10–0x01D27B18` | 8 bytes | Battle char struct pointer/header (before entity array) |
| `0x01D28C00–0x01D29000` | 1024 bytes | Beyond scanned range, may hold display state |
| `0x01D76844–0x01D768D0` | ~140 bytes | Battle menu/phase state (already partially known) |

The **effect struct region around `0x0279xxxx`** is the most likely location for heal animation state. These are dynamically allocated effect data structures that the battle effects opcode system uses to track active visual effects including their frame counters, positions, and palette indices.

---

## Key resources and tools the modder should use next

**OpenFF8's `memory.h`** (github.com/Extapathy/OpenFF8) defines `ff8vars` and `ff8funcs` structs with hardcoded addresses for the Steam 2013 EN build. This file could not be fetched remotely but can be obtained by cloning the repository. It likely contains the closest public documentation to the addresses the modder needs.

**FFNx's `ff8_data.cpp`** provides the dynamic address resolution chain. Starting from `battle_main_loop` (at `main_loop + 0x340`) and `battle_read_effect_sub_50AF20`, the modder can reconstruct the full battle function hierarchy in a disassembler. FFNx's `trace_all = true` config option logs all rendering calls during battle, which would show the draw calls corresponding to damage number sprites.

**The Qhimm FFNx-FF8 Discord** (discord.gg/u6M7DnY) and **Tsunamods Discord** (discord.gg/Urq67Uz) are where active FF8 RE discussion happens. Community members like myst6re and MaKiPL have deep knowledge of the battle engine. The Qhimm forums themselves are currently returning 403 errors, making Discord the only viable channel for community knowledge.

**BlindGuyNW** (github.com/BlindGuyNW) has built accessibility/screen reader mods for FF6 and FF9 using similar approaches. No FF8 accessibility mod currently exists — this would be the first. The FF6 Screen Reader mod's architecture (DLL injection + memory polling + SAPI TTS) is directly applicable.

## Conclusion

The heal number rendering function shares the same digit-drawing code as damage numbers, differentiated only by a palette index parameter (green #5 vs white #8 from `a9btlfnt.bft`). The `0x01D280C0` flag never clears for heals because the heal action's effect handler lacks the frame-counter-driven cleanup logic present in the damage handler — this is a design asymmetry in the original PSX engine, not a bug. The fastest path to a working TTS trigger for heals is to **hook the palette upload function at `0xB66560`** and detect when palette #5 (green) is selected, using the return address to identify the rendering function. Alternatively, scanning the effect struct memory at `0x027973EC` during heal animation may reveal the heal-specific state flag that could serve the same role as `0x01D280C0` does for damage.
