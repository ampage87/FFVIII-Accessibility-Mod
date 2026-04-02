# Deep Research Request: FFNx GF Fire Trigger Mechanism
## What triggers a GF summon to actually fire in FF8 PC with FFNx v1.23.x?

### Context
We are building a DLL-based accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe, App ID 39150) that runs alongside FFNx v1.23.x. We have an Enhanced Wait Mode (EWM) feature that freezes enemy/ally ATB bars while the player reads TTS output. We need to also freeze the GF loading timer during this window so summoned GFs don't fire while the player is deciding their next action.

### SAVEMAP OFFSET CORRECTION (CRITICAL)
All ChatGPT deep research assumes a 96-byte (0x60) savemap header. The actual header is **76 bytes (0x4C)**. You must **subtract 0x14** from all post-header offsets found in any documentation or prior research results.

### What We Know

**Computed stats array**: `0x01CFF000`, stride `0x1D0` (464 bytes), 7 slots (3 allies + 4 enemies).
- `+0x14` (uint16): GF loading current value — incremented by FFNx every frame while a GF is charging
- `+0x16` (uint16): GF loading max value — the target the loading gauge must reach
- `+0x18` (uint16): GF current HP
- `+0x1A` (uint16): GF max HP  
- `+0x1C` (uint8): GF charging flag byte
- `+0x1D` (uint8): GF index (which GF is summoned)
- `+0x172` (uint16): Character current HP
- `+0x174` (uint16): Character max HP

**GF state machine region** (battle menu struct area):
- `0x01D76868`: State machine variable. Value 5 = "GF ready to fire" (we tried clamping this — didn't prevent fire)
- `0x01D76970` (int8): GF summoner slot index (0-2 = party slot, -1 = none)
- `0x01D76971` (uint8): GF active flag (1 = GF is loading/active)

**Vanilla engine GF timer function**: `0x004B0500` — confirmed DISPLAY ONLY. Reads +0x14/+0x16 to compute visual timer bar percentage. Does NOT write +0x14. Does NOT make the fire decision.

**Vanilla ATB update function**: `0x004842B0` — does NOT increment compStats+0x14. FFNx replaces the battle timing pipeline.

**FFNx is the writer of compStats+0x14**: Confirmed via hardware write breakpoint. Two instructions in FFNx DLL space alternately write to +0x14 every frame:
- Writer 1: `MOV ECX, 0x01CFF016; MOV [ESI], AX` (signature: `B9 16 F0 CF 01 66 89 06`)
- Writer 2: `JB +3; LEA ECX,[EAX-1]; MOV [ESI], CX` (signature: `72 03 8D 48 FF 66 89 0E`)
- Both run on TID = game thread.
- The writer lives in a **separate DLL loaded by FFNx** (not FFNx.dll itself). Module base changes per launch (ASLR). We found it via EnumProcessModules + signature scan.

### What We've Tried (ALL FAILED to prevent GF from firing)

1. **Skipping the vanilla GF timer function** (0x004B0500 via MinHook — return immediately when cap active): No effect. Function is display-only.

2. **Clamping state68** at `0x01D76868` from value 5 (fire) to value 3 (loading): No effect. GF still fires. Done on game thread inside HookedATBUpdate.

3. **ATB hook sandwich on compStats+0x14**: Save value, zero it, call original ATB function, measure increment, restore+cap at max-1. No effect — the ATB function doesn't write +0x14, FFNx does separately.

4. **MinHook on FFNx's writer function**: Signature scan found the bytes, walked backward to find function entry via CC/90 padding. MH_CreateHook + MH_EnableHook succeeded (MH_OK). BUT the hook was **never called** (ffnxCalls=0). The backward walk from signature to function entry landed on the wrong function boundary. The actual calling code likely inlines this logic or calls it via an indirect mechanism that doesn't go through the detected entry point.

5. **Brute-force poll clamp of +0x14** from mod thread (~60fps): Read +0x14, if >= max write max-1. No effect — FFNx writes +0x14 and the game engine checks it on the **same game thread in the same frame**, before our mod thread can intervene.

6. **Game-thread clamp of +0x14** inside HookedATBUpdate (runs ~60x/sec on game thread): Still no effect. FFNx's writer and the fire check both run in the same frame loop iteration AFTER our ATB hook already returned.

7. **Inflating +0x16 (max) to 0xFFFF** while cap is active: Set the GF loading max to 65535 so the `current >= max` comparison can never pass. **STILL FIRES.** This is the critical finding — it proves the GF fire decision does NOT use compStats+0x14 >= compStats+0x16.

### What We Need To Know

The core question: **What code path in FFNx (or in the vanilla engine as called by FFNx) decides that a GF summon has finished loading and should execute its attack animation?**

Specific questions:

1. **Does FFNx have its own internal timer/counter for GF loading** separate from compStats+0x14? FFNx may maintain a parallel float/double-precision timer that drives the fire decision, and only writes to compStats+0x14 for display purposes (to keep the visual gauge in sync).

2. **Where in FFNx source code is the GF fire/completion decision made?** Look at FFNx's battle update loop. The source repo is https://github.com/julianxhokaxhiu/FFNx (we're using v1.23.x / canary builds). Key source files to examine:
   - `src/ff8/battle/` — any battle-related code
   - `src/ff8.cpp` and `src/ff8.h` — battle struct definitions
   - `src/ff8_data.cpp` — address resolution, look for anything related to GF loading or computed stats
   - `src/gamehacks.cpp` — game timing modifications
   - Any file that references `compute_char_stats`, `char_comp_stats`, or the address `0x01CFF000`
   - Any file that references GF-related terms: summon, guardian force, GF loading, GF timer

3. **Does FFNx replace the entire battle timing system?** FFNx is known to replace the ATB timing with its own implementation (our ATB hook proved the vanilla function is still called but FFNx also writes separately). Does FFNx use a frame-counting or time-based approach for GF loading that bypasses the vanilla integer counter?

4. **What is the vanilla engine's GF fire trigger?** Even without FFNx, the original FF8 engine has a mechanism to decide when the GF fires. Where is this check in the decompiled code? Is it a comparison of compStats+0x14 vs +0x16, or something else? The function at 0x004B0500 is confirmed display-only — what other function reads +0x14 to make the fire decision?

5. **Is the fire trigger based on a memory address we haven't identified?** Our hardware write BP approach can target any 2-byte address. If we knew which byte/word transitions when the GF fires (the "go" signal), we could BP it and find the writer. We've tried:
   - `0x01D769D0` (some state bytes that change near fire time)
   - `compStats[slot]+0x14` (loading current — written by FFNx but not the fire trigger)
   - `0x01D76868` (state machine var — clamping it doesn't prevent fire)
   What other addresses are involved in the GF fire decision?

6. **Is there a function pointer or callback that FFNx installs** for the GF completion event? FFNx might hook a vanilla function that the battle loop calls to check "is the GF ready?" — and the fire decision happens inside FFNx's replacement.

7. **Does FFNx's speed/FPS handling affect GF timing?** FFNx normalizes the game's frame rate. The original FF8 was locked to ~15fps for battle logic. FFNx may run the battle loop at 60fps but scale timers accordingly. The GF loading might be driven by a delta-time accumulator inside FFNx rather than an integer counter.

### FFNx Source Reference

The FFNx source is at https://github.com/julianxhokaxhiu/FFNx. Key files that are most likely to contain GF-related battle timing:

- `src/ff8.h` — struct definitions including `ff8_char_computed_stats` (where +0x14 lives)
- `src/ff8_data.cpp` — address resolution, look for `char_comp_stats_1CFF000` and `compute_char_stats_sub_495960`
- `src/gamehacks.cpp` — battle timing modifications, frame rate normalization
- `src/ff8/battle/` directory (if it exists)
- Any code that calls or replaces functions in the `0x0048xxxx` - `0x004Bxxxx` range (battle code area)

We have a local copy of FFNx canary source at: `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/`

### What Would Solve Our Problem

If we can identify:
- **The exact memory address** that serves as the "fire" trigger (the byte/word whose transition from X to Y causes the GF to execute), OR
- **The FFNx function** that makes the fire decision (so we can MinHook it), OR
- **A configuration or flag** in FFNx that controls GF loading speed (so we can set it to 0 during EWM cap)

...then we can prevent the GF from firing while the player is reading their command menu. The GF should resume loading and fire normally when the EWM cap releases.

### Summary of the Problem

Despite 7 different approaches targeting compStats+0x14/+0x16, state68, the vanilla timer function, and the FFNx writer function, the GF still fires while EWM cap is active. The critical proof: **inflating +0x16 (max) to 0xFFFF does not prevent the fire**, which means the fire decision is NOT based on `compStats+0x14 >= compStats+0x16`. There must be a separate timer or counter that we haven't found yet.
