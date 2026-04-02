# FF8 Battle Damage Display Trigger — Deep Research Prompt

## Context
I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 PC edition, FF8_EN.exe, App ID 39150). The mod hooks into the game via a dinput8.dll proxy and announces battle events via text-to-speech for blind players. I need to find the memory address or mechanism that controls when damage numbers are visually displayed on screen during battle.

## What I Already Know

### The damage display value address
- `0x01D2834A` (uint16) holds the last displayed damage number
- This address gets written with the correct damage value (e.g., 12 for 12 damage) at the SAME INSTANT that HP values change in the entity array
- The value NEVER returns to zero after being set — it holds the last damage value permanently until overwritten by the next damage event
- This address is NOT a countdown timer and does NOT indicate when the animation starts or ends

### The problem
- The engine computes HP damage and writes `0x01D2834A` BEFORE the attack animation plays
- Visually, the player sees: attack animation → damage number appears on screen → number fades away
- I need to detect when the damage number APPEARS on screen (after animation), not when HP is computed (before animation)
- Currently my mod announces damage when HP changes, which is too early — the announcement plays before the attack animation finishes

### Entity and battle state addresses
- Entity array base: `0x1D27B18`, stride `0xD0`, 7 slots (3 allies + 4 enemies)
- Entity curHP at `entity+0x0C` (uint16 for allies, uint32 for enemies)
- Battle menu phase: `0x01D768D0` (byte)
- Active character ID: `0x01D76844` (byte, 0-2 = player slots, 255 = no turn active)
- ATB update function at `0x004842B0` (hooked via MinHook for Enhanced Wait Mode)
- The ATB update function handles both ATB increments and status timer decrements

### FFNx context
- The mod runs alongside FFNx v1.23.x (user-installed separately)
- FFNx hooks various game functions but the damage display system should be mostly vanilla
- FFNx canary source is available for reference

### SAVEMAP OFFSET CORRECTION
All research results that reference savemap offsets assume a 96-byte (0x60) savemap header. The actual header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets. This correction does NOT apply to absolute runtime addresses like entity array addresses.

## What I Need

### Primary question
What memory address, flag, or mechanism does the FF8 battle engine use to trigger the visual display of damage numbers on screen? Specifically:

1. **Damage display animation state**: Is there a flag or byte that indicates "a damage number is currently being animated/displayed on screen"? Where is it?

2. **Damage display trigger mechanism**: What function or code path transitions from "damage computed" to "damage number visible"? Is it frame-count based, timer-based, or event-driven?

3. **Per-slot display state**: Does each entity slot have its own display animation state, or is there a single global display? If per-slot, what's the base address and stride?

4. **Display countdown/timer**: Is there a countdown timer for the damage number fade animation? If so, what address? (Note: `0x01D2834A` is NOT this — it holds the value permanently.)

### Secondary questions
5. **Battle scene display list**: The address region around `0x01D28340` seems related to display. What is the full structure? My scan shows 16 uint16 values from `0x01D28340` to `0x01D2835E`. During enemy-on-ally damage (12), the scan showed: `0000 0000 0401 0000 0300 [000C] 0000 4000 00FF 0000 0000 0000 0000 0000 0000 0000`. What do these other fields mean?

6. **Animation sequencer**: FF8 battles use an animation sequencer for attack animations. Does the damage number display get triggered by a specific animation event/callback? If so, what address or mechanism?

7. **Relevant functions**: What are the function addresses (in the Steam 2013 PC build) for:
   - The damage number display/render function
   - The animation state update function
   - Any callback that fires when damage numbers should appear

### What would help my mod
Ideally I need one of:
- A memory address that transitions from 0→non-zero when damage appears on screen, and back to 0 when it fades (or vice versa)
- A function address I can hook that gets called at the moment damage numbers are rendered
- A timer/counter address that starts counting when damage display begins

## Technical Details About My Approach
- I'm using MinHook for function hooking (already have ATB function hooked successfully)
- I can poll memory addresses every ~16ms from the mod's Update() function
- I can set up hardware write breakpoints (via the game's debug facilities) to find write instructions
- The mod is a Win32 DLL (dinput8.dll proxy), built with MSVC
