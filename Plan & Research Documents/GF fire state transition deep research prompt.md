# Deep Research Request: FF8 GF Fire State Transition — What Code Triggers the Summon Animation?

## Context

I'm building a DLL-based accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe, App ID 39150, no ASLR, image base 0x00400000). The mod runs alongside FFNx v1.23.x (graphics/audio replacement layer). I need to find the exact code path that decides "the GF is done loading, execute the summon now" so I can prevent it from triggering during Enhanced Wait Mode.

## What We Have Exhaustively Proven (10 failed approaches)

**The GF fire decision does NOT read from any of these addresses:**

1. **compStats[slot]+0x14 / +0x16** (base 0x01CFF000, stride 0x1D0): These are display-only gauge values. FFNx writes to +0x14 from its rendering path. We capped +0x14 at max-1 via ATB hook sandwich, clamped it from a mod thread, inflated +0x16 to 0xFFFF — GF always fires on schedule regardless. compStats values read as `cs=0/0` during some GF loading sessions, proving they're not even populated consistently.

2. **Battle entity array [slot*0x1D0]+0x27C** (base 0x01D27B18): The "real ATB counter" from ff8-speedruns RE docs. Absolute address for ally 0: 0x01D27D94. We confirmed via hardware write breakpoint that this address IS written during GF loading (by both the vanilla ATB function at 0x004843D3 and by FFNx code). We implemented a save→zero→call→restore+cap sandwich in our MinHook on the ATB function at 0x004842B0. **We capped this at max/2 (6000 out of 12000) and it held perfectly (confirmed by diagnostic logging every second) — but the GF still fired at exactly the expected time (~3-4 seconds at max compatibility).** This proves entity+0x27C is NOT the fire-decision variable.

3. **GF state machine byte at 0x01D76868** ("state68"): We clamped this from 5→3 on the game thread inside HookedATBUpdate. GF still fires.

4. **GF timer function at 0x004B0500**: This is display-only — it reads compStats+0x14 to render the gauge. Skipping it entirely has no effect on the fire decision.

5. **0x01D769D6** (GF loading timer byte): Clamping/freezing this had no effect.

**What we know about the fire event from logs:**
- The GF summon animation text ("Diamond Dust") appears via the field dialog system (show_dialog hook catches it in battle mode=3).
- During GF loading, `0x01D76971` = 1 (GF active flag), `0x01D76970` = summoner party slot.
- State68 at 0x01D76868 transitions through values 0→1→2→3 during loading, then eventually reaches 5 (which we believe means "fire"). But clamping state68 at 3 doesn't prevent the fire — the engine either writes 5 and fires atomically in the same frame, or the fire decision bypasses state68 entirely.
- GF state region (0x01D76860-0x01D768DF) and GF struct region (0x01D76960-0x01D769DF) both show activity during loading. Address 0x01D7687F increments steadily by ~8-12 per poll (200ms intervals) — this could be an animation frame counter.
- The GF fires at exactly the expected time based on compatibility (max compat ≈ 3-4 seconds), suggesting an internal timer we haven't found.

**Hardware breakpoint data on entity+0x27C (0x01D27D94) — 5 writers per frame:**
1. FFNx at `0x6E909D15`: `C7 06 00 00 00 00` — MOV [ESI], 0 (zeroes the value)
2. FFNx at `0x6E909DCC`: `C7 02 00 00 00 00` — MOV [EDX], 0 (zeroes again)
3. Vanilla at `0x004843D5`: `89 16` — MOV [ESI], EDX (the ATB increment, inside our hooked function)
4. FFNx at `0x6E909EB3`: `89 0E` — MOV [ESI], ECX (post-process: CALL + CMOVNB + store)
5. FFNx at `0x6E909F16`: `89 08` — MOV [EAX], ECX (loop iteration, stride 0xD0 add visible: `05 D0 00 00 00 46 3D D4`)

Writer #5 is interesting — it iterates with stride 0xD0 (our small entity stride), NOT 0x1D0. This might be syncing from a 0xD0-stride source to the 0x1D0-stride destination.

## What I Need You To Find

**Primary question: What code in FF8_EN.exe (vanilla engine, not FFNx) decides that a GF summon is complete and should execute?**

Specifically:

### 1. The GF fire comparison
The vanilla engine must have a comparison somewhere like `if (gf_timer >= threshold)` → initiate summon animation. Where is this comparison? What address does it READ to make the decision? It's not compStats+0x14, not entity+0x27C, and not state68. There must be a third timer or counter.

### 2. The GF loading timer increment
ForteGSOmega's Battle Mechanics FAQ says the GF bar uses a count-up system: counter increments by `(Spd + 30) × SpeedMod / 2` per tick until it reaches `6000 - 5 × ShownCompatibility`. Where is the actual counter variable that gets compared against the threshold? Is it inside the battle entity struct at a different offset than +0x27C? Is it in the 0x01D76860-0x01D769DF region?

### 3. The battle_main_loop dispatch
FFNx resolves `battle_main_loop` from `main_loop + 0x340`. This function orchestrates the battle tick. Somewhere in its call chain, there's a sub-function that checks GF timer completion and transitions the battle state to execute the summon. What is the address of this sub-function?

### 4. The 0x01D7687F counter
This byte at 0x01D7687F increments by ~8-12 every 200ms during GF loading. Is this the actual fire-decision timer? If so, what code reads it to decide when to fire?

## Technical Details for Your Search

**Game binary**: FF8_EN.exe, Steam 2013 edition, no ASLR, image base 0x00400000. App ID 39150.

**FFNx version**: v1.23.x canary. FFNx source is at https://github.com/julianxhokaxhiu/FFNx. Key files: `src/ff8_data.cpp`, `src/ff8.h`, `src/ff8_opengl.cpp`, `src/gamehacks.cpp`, `src/ff8/battle/`. FFNx deploys as `AF3DN.P` on Steam (renamed from FFNx.dll at build time).

**Key addresses confirmed:**
- Battle entity array: 0x01D27B18, stride 0xD0 for the first section (0xD0 per slot, 7 slots), but full struct may be 0x1D0
- compStats: 0x01CFF000, stride 0x1D0, 7 slots
- ATB update function: 0x004842B0 (we have a MinHook on this)
- GF timer display function: 0x004B0500 (we have a MinHook on this)
- GF active flag: 0x01D76971 (1 = GF loading)
- GF summoner slot: 0x01D76970
- GF state machine: 0x01D76868 (values 0-5)
- Menu phase: 0x01D768D0
- Battle main loop: resolved from main_loop + 0x340

**Community RE sources to check:**
- ff8-speedruns project (battle entity memory maps)
- Qhimm forums (FF8 battle system reverse engineering threads)
- ForteGSOmega's Battle Mechanics FAQ (PSX-based but timer formulas should match)
- Doomtrain wiki (kernel.bin structures, GF compatibility data)
- myst6re's FF8 tools and documentation
- FFNx source code (for understanding what FFNx patches in the battle system)

**IMPORTANT OFFSET CORRECTION**: If any research references savemap offsets, the actual FF8 Steam savemap header is 76 bytes (0x4C), NOT 96 bytes (0x60). Subtract 0x14 from any post-header savemap offsets found in research. This correction does NOT apply to runtime battle addresses like the entity array — those are direct memory addresses.

## What Would Be Most Helpful

1. The exact address of the timer/counter the fire decision reads (not entity+0x27C, not compStats+0x14)
2. The exact address of the comparison instruction that decides "GF is ready, fire now"
3. Any PSX disassembly that shows the GF fire logic, translated to PC addresses
4. The relationship between the 0x01D76860-0x01D769DF state region and the actual fire trigger
5. Whether the fire decision might be embedded in the GF animation system rather than the battle timer system (i.e., maybe the animation plays for a fixed duration and the "fire" is triggered by the animation completing, not by a timer reaching a threshold)
