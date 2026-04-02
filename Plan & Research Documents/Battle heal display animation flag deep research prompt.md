# FF8 Battle Heal Display Animation Flag — Deep Research Prompt

## Context
I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 PC edition, FF8_EN.exe, App ID 39150). The mod hooks into the game via a dinput8.dll proxy and announces battle events via text-to-speech for blind players. I need to find the memory address or mechanism that controls when HEAL numbers are visually displayed on screen during battle.

## What I Already Know — Damage Display (SOLVED)

For DAMAGE, I found the animation completion flag:
- **`0x01D280C0` (byte):** Set to 01 when the engine starts displaying damage numbers on screen. Clears to 00 when the damage number animation finishes (~1.4 seconds later). This is the trigger for our damage TTS announcements.
- **`0x01D2834A` (uint16):** Holds the actual displayed damage value. Gets written at the same instant as HP changes (before the visual animation). Never returns to zero — holds the last value permanently.
- **Entity `+0xBF` (byte):** Per-entity animation state. Goes to 02 when the entity takes damage, returns to 00 when the damage animation completes.

For damage, the flow is:
1. HP computed, `0x01D2834A` written, entity+0xBF set to 02, `0x01D280C0` set to 01 — all in the same frame
2. Attack animation plays (~1 second)
3. Damage number appears on screen
4. Damage number fades away
5. `0x01D280C0` clears to 00, entity+0xBF clears to 00 — this is our announce trigger

## The Problem — Heal Display (UNSOLVED)

For HEALING (Cure spell, items, etc.), the behavior is different:
- **`0x01D280C0`:** Goes to 01 at the instant HP changes, but **NEVER clears back to 00**. It stays at 01 indefinitely during heals.
- **`0x01D2834A`:** Gets written with the heal amount (e.g., 140 for Cure healing 140 HP), same as damage. But since the flag at `0x01D280C0` never clears, we can't use it as a trigger.
- **Entity `+0xBF`:** Goes to 02 when healed, but **stays at 02 indefinitely**. Never returns to 00 during our 4-second observation window.

## What I've Tried (Extensive Memory Scanning)

### Approach 1: Baseline diff (v0.10.45, v0.10.49)
- Scanned `0x01D28000-0x01D28800` (2KB) and `0x01D27000-0x01D27400` (1KB)
- Captured baseline at moment HP changed, diffed every 200ms for 4 seconds
- Result: ZERO changes in either region. Everything set at t=0 stays set.

### Approach 2: Rolling diff (v0.10.50)
- Scanned `0x01D28000-0x01D28C00` (3KB) — display area + wider
- Scanned `0x01CFF000-0x01CFF800` (2KB) — computed stats array (7 slots × 0x1D0 stride)
- Scanned all 7 entity structs (0xD0 × 7) at `0x1D27B18` — rolling diff on all bytes except HP/ATB
- Each sample compared to PREVIOUS sample (not baseline) to catch transitions
- Result: **ZERO rolling changes across 5KB+ of memory over 4 seconds** during heal events

### Conclusion from scanning
The heal animation state is NOT stored in:
- The display value region (`0x01D28000-0x01D28C00`)
- The computed stats array (`0x01CFF000-0x01CFF800`)
- The entity array at `0x1D27B18` (7 × 0xD0)
- The region around `0x01D27000`

The heal number display animation appears to be driven by something completely outside the battle state area — possibly in the rendering/scene graph system, a heap allocation, or a frame counter in a code-local variable.

## What I Need

### Primary question
Where does FF8's battle engine store the state for healing number display animation? Specifically:

1. **What function renders the green heal numbers on screen?** What's the function address in the Steam 2013 PC build?

2. **How does the engine know to start/stop displaying the heal number?** Is it a timer, frame counter, animation sequence callback, or flag?

3. **Is there a hookable function** that gets called when the heal number should appear, or when it finishes displaying? If I can hook that function, I can trigger the TTS announcement.

4. **Why does `0x01D280C0` behave differently for heals vs damage?** For damage it clears to 00 after ~1.4s. For heals it stays at 01 forever. Is this the same flag with different behavior, or are there actually two different code paths?

5. **Does `entity+0xBF` eventually clear for heals?** My scanning window was 4 seconds. Could it take longer (5-10 seconds)? Or does it only clear when the next action starts?

### Secondary questions
6. **What is the battle display/rendering pipeline?** How does the engine queue visual effects like damage/heal numbers for rendering? Is there a display list or scene graph?

7. **Are there separate functions for damage number rendering vs heal number rendering?** The numbers are visually different (white for damage, green for heals) which suggests possibly different code paths.

8. **What function writes to `0x01D280C0`?** If I can find the code that sets and clears this byte, I might be able to find the equivalent heal path by looking at nearby code.

9. **Are there other battle animation state addresses** outside the regions I've scanned? Specifically in the range `0x01D00000-0x01E00000` that I haven't checked?

## Known Memory Addresses (for reference)
- Entity array base: `0x1D27B18`, stride `0xD0`, 7 slots (3 allies + 4 enemies)
- Entity curHP at `entity+0x0C` (uint16 for allies)
- Entity maxHP at `entity+0x0E` (uint16 for allies)
- Entity animation state: `entity+0xBF` (byte: 00=idle, 02=animating)
- Damage display value: `0x01D2834A` (uint16)
- Damage animation active flag: `0x01D280C0` (byte: 01=active, 00=done) — DAMAGE ONLY
- Damage animation active flag2: `0x01D280C1` (byte, same behavior as C0)
- Computed stats array: `0x1CFF000`, stride `0x1D0`, 7 slots
- Battle menu phase: `0x01D768D0` (byte)
- Active character ID: `0x01D76844` (byte)
- ATB update function: `0x004842B0` (hooked via MinHook)

## SAVEMAP OFFSET CORRECTION
All research results that reference savemap offsets assume a 96-byte (0x60) savemap header. The actual header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets. This correction does NOT apply to absolute runtime addresses like entity array addresses.

## Technical Details
- I'm using MinHook for function hooking (already have ATB function hooked successfully)
- I can poll memory addresses every ~16ms from the mod's Update() function
- The mod is a Win32 DLL (dinput8.dll proxy), built with MSVC
- FFNx v1.23.x is installed alongside (hooks various game functions)
- I have FFNx canary source available for reference at `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/`
