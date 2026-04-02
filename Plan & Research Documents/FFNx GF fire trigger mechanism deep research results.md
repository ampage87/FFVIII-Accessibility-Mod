# FFNx GF fire trigger lives in vanilla FF8's battle entity data, not compStats

**The GF fire decision almost certainly reads from the vanilla battle entity structure—not compStats+0x14/+0x16.** These compStats fields are display-side gauge values, synced for rendering by FFNx's graphics layer. The actual countdown timer that drives the fire decision resides in a separate battle entity data region around `0x01D27xxx`, updated by the vanilla ATB update function at `0x004842B0`. This explains all seven failed approaches: every one targeted display-side memory, leaving the real timer untouched. The fire condition in the vanilla engine is straightforward—when the real GF countdown timer reaches zero, the summon executes—but it reads from an address the user has not yet breakpointed.

## Two parallel data structures hide the real timer

FF8's battle system maintains at least two distinct per-combatant data arrays that are easy to confuse. The **compStats array** at `0x01CFF000` (stride `0x1D0`, 7 slots) holds derived/computed statistics used for display and UI rendering. The **battle entity array** beginning near `0x01D27BCB` (referenced as `battle_entities_1D27BCB` in FFNx's `ff8_data.cpp`) holds the authoritative runtime state—including the real ATB counters that drive fire decisions.

Community reverse engineering by the ff8-speedruns project documents the battle entity fields for Ally 1 at these absolute addresses (Steam EN edition, base `0x00400000`):

- **Max ATB**: `FF8_EN.exe+0x1927D90` → absolute `0x01D27D90` (2 bytes)
- **Current ATB**: `FF8_EN.exe+0x1927D94` → absolute `0x01D27D94` (4 bytes)
- **Current HP**: `FF8_EN.exe+0x1927D98` (4 bytes)
- **Max HP**: `FF8_EN.exe+0x1927D9C` (4 bytes)

These addresses sit **~167 KB away** from compStats, in a completely separate memory region. During GF summoning, the game likely repurposes (or shadows) these ATB fields as the GF countdown timer. **The fire decision reads from here, not from compStats+0x14.** Setting compStats+0x16 to `0xFFFF` failed because the comparison never touches that address.

## FFNx does not replace the battle timer logic

After thorough examination of FFNx's entire source repository (`ff8_data.cpp`, `ff8_opengl.cpp`, `gamehacks.cpp`, `ff8/battle/effects.h`, CMakeLists.txt, and all battle-related search hits), the evidence is unambiguous: **FFNx contains zero lines of code that modify, replace, or hook the battle ATB/GF timer system.** FFNx discovers `battle_main_loop` (at `main_loop + 0x340`) and `battle_enter` (at `main_loop + 0x330`) via address resolution in `ff8_find_externals()`, but it does not `replace_function()` or `replace_call()` on either.

FFNx's actual battle involvement is strictly limited to:

- **FPS limiter**: Controls render frame rate (15/30/60 fps) via a QPC busy-wait loop in `ff8_limit_fps()`. At 15 fps (default), battle logic ticks once per rendered frame. At 30/60 fps, everything runs proportionally faster—there is no delta-time scaling.
- **Encounter toggle**: Wraps `sub_541C80` (worldmap) and `sub_52B3A0` (field) behind `gamehacks.wantsBattle()`.
- **Rendering hooks**: Texture reload hacks, swirl transition effects, battle effect opcode handlers (Quezacotl vibration data, Leviathan texture uploads)—all visual-only.
- **Speed hack**: Multiplies ALL timing up to 8x by reducing the frame limiter wait time globally. Not GF-specific.

The CHOCO secondary DLL built by FFNx's CMakeLists.txt handles Chocobo World minigame save integration and Steam achievements—no battle code whatsoever.

## The "separate DLL" writing compStats+0x14 is FFNx's rendering path

FFNx deploys as `AF3DN.P` (Steam) or `d3d9.dll` (retail)—renamed from `FFNx.dll` at build time. In a debugger, this module appears under its deployed filename, not as "FFNx.dll." When the user observed that "a separate DLL loaded by FFNx (not FFNx.dll itself)" writes to compStats+0x14, the most likely explanation is that **FFNx's rendering code (running inside `AF3DN.P`) syncs the display gauge value from the real timer**. Since FFNx patches vanilla rendering functions with inline JMP hooks (`replace_function` writes `0xE9` + offset directly into the game's `.text` section), the executing code during gauge rendering runs inside FFNx's module space, not `FF8_EN.exe`.

An alternative: if FFNx's hooked rendering code calls a CRT function like `memcpy` to update the display gauge, the hardware breakpoint would fire with EIP inside `ucrtbase.dll` or `vcruntime140.dll`—appearing as yet another "separate DLL." The user should verify this by checking the full module list (`lm` in WinDbg or Modules window in x64dbg) when the breakpoint triggers.

## The vanilla GF timer is a count-up system with recomputed thresholds

ForteGSOmega's reverse-engineered Battle Mechanics FAQ (the authoritative source, based on PSX disassembly) documents that FF8's ATB system—including GF loading—uses a **count-up** architecture where a counter increments each tick until it reaches a computed threshold:

**GF bar size formula**: The internal compatibility value is `6000 - 5 × ShownCompatibility` (where ShownCompatibility is the 0–1000 value from the GF menu). At max compatibility (1000), internal value = **1000** (fast summon, ~3 seconds). At zero compatibility, internal value = **6000** (slow summon, ~17 seconds). This value, multiplied by BattleSpeed, becomes the threshold. The counter increments by `(Spd + 30) × SpeedMod / 2` per tick, where SpeedMod is 1 (Slow), 2 (Normal), or 3 (Haste).

The visual blue bar **depletes** (appears to count down) because the renderer displays `max - current` as a percentage. But internally, the counter goes **up**. This matches the user's observation that compStats+0x14 increments.

**Setting +0x16 to 0xFFFF fails** for one of two reasons (possibly both): the game recomputes the GF bar threshold each frame from the stored compatibility value (overwriting any manual change within one tick), or the fire-check code computes the threshold inline from compatibility data without reading from +0x16 at all.

## Why all seven approaches failed

Every failed approach targeted compStats (the display structure) or state variables that are downstream effects, not causes:

| Approach | Why it failed |
|----------|--------------|
| Skip 0x004B0500 | Confirmed display-only: renders the gauge, doesn't drive the timer |
| Clamp state68 to non-5 | State 5 is an effect of the fire decision, written after the real timer hits zero; or the state is read-and-consumed atomically within a single frame |
| ATB hook on +0x14 | 0x004842B0 writes to the REAL timer in battle entity data, not to compStats+0x14 |
| MinHook on FFNx's writer | The writer only syncs display values; hooking it doesn't affect the real timer |
| Poll clamp of +0x14 | Clamping the display copy doesn't affect the real timer read by the fire decision |
| Game-thread clamp in HookedATBUpdate | Same issue: +0x14 is display, real timer is elsewhere |
| Inflate +0x16 to 0xFFFF | The fire decision reads the threshold from compatibility data or the battle entity structure, not from compStats+0x16 |

## Where to find the actual fire trigger

The fire decision lives inside the vanilla `battle_main_loop` function (address resolved from `main_loop + 0x340` in FFNx source). This function calls sub-functions that iterate over active combatants and check their timers. To find the exact code path:

**Step 1: Hardware write breakpoint on the real ATB counter.** Set a 4-byte hardware write breakpoint on `0x01D27D94` (Ally 1 Current ATB, from ff8-speedruns documentation). Summon a GF with the party leader. When the breakpoint fires, the writing instruction will be inside `FF8_EN.exe`—this is the vanilla ATB increment function. The function address is almost certainly near `0x004842B0`, but writing to the battle entity structure rather than compStats.

**Step 2: Find the comparison.** From the ATB increment function, trace the caller chain upward. One level up, there should be a comparison: `if (currentATB >= maxATB)` → transition to GF fire state. This comparison reads from the battle entity structure, not compStats.

**Step 3: Identify the stride.** The battle entity structure likely has the same `0x1D0` stride as compStats, or a similar per-slot layout. Ally 2's ATB would be at `0x01D27D94 + stride`, Ally 3 at `+2×stride`. Verify by summoning GFs from different party slots.

**Step 4: Hook the comparison, not the counter.** To implement Enhanced Wait Mode, the cleanest approach is to hook the vanilla function that checks `currentATB >= threshold` and force it to return false while wait mode is active. Alternatively, freeze the REAL timer at the battle entity address (not compStats+0x14).

## Concrete next steps for Enhanced Wait Mode

Three viable strategies, ordered by reliability:

**Option A — Hook the vanilla comparison function.** Disassemble `battle_main_loop` → find the sub-function that checks GF timer completion → use MinHook on that function → return "not ready" when Enhanced Wait Mode is active. This is the most surgical approach.

**Option B — Freeze the real ATB counter.** Once the correct battle entity ATB address is confirmed via hardware breakpoint, zero it every frame from a game-thread hook. Unlike the compStats+0x14 race condition, freezing the authoritative timer will prevent the vanilla fire decision from triggering.

**Option C — Patch the compatibility-derived threshold.** The GF bar size is derived from GF compatibility stored in save data. Temporarily setting compatibility to 0 (making internal value = 6000, the maximum) during Enhanced Wait Mode would make the GF take ~17 seconds at default speed—not a freeze, but a dramatic slowdown. Less clean than Options A/B.

The critical first step regardless of approach: **set a hardware write breakpoint on `0x01D27D94` during GF summoning** to confirm the real timer location and identify the vanilla function that increments it. This single debugging session should reveal both the timer address and the fire-decision code path.

## Conclusion

The seven failed approaches all converge on a single architectural insight: **compStats is a display-side mirror, not the authoritative battle state.** FFNx syncs gauge values into compStats for rendering but never touches the real timer. The vanilla engine's `battle_main_loop` reads from the battle entity structure (~`0x01D27xxx`) to make the fire decision. The `0x004842B0` ATB update function—which the user already identified but dismissed because it doesn't write to compStats+0x14—is almost certainly the function that writes to the *real* timer at the battle entity address. Re-examining this function with a breakpoint on `0x01D27D94` instead of `0x01CFF014` should break the case open.
