# Deep Research Prompt — FF8 Victory Screen Phase Counter

I'm reverse engineering Final Fantasy VIII (Steam 2013, FF8_EN.exe) to build an accessibility mod. I need to find the memory address or function that controls which phase/step of the post-battle victory screen is currently displayed.

## Context

- The victory screen runs during game mode 4 ("after battle"). Mode 3 = battle, mode 5 and 100 are brief transitions.
- The victory screen displays multiple sub-screens that the player advances through by pressing Confirm:
  1. Victory pose celebration
  2. "EXP received" — shows per-character EXP Acquired, Current EXP, Next LEVEL
  3. EXP countdown/add animation + level up notices (same screen, second state after pressing X)
  4. "Item received" — one item per screen with quantity and help/description text; player presses Confirm to advance through each item
  5. "Raising GF" — AP received, GF level ups, ability learned (multiple sub-steps)
- The victory screen text and layout is loaded from `\ff8\data\eng\battle\R0WIN.DAT`.
- The game blocks DirectInput polling during the victory screen — the blocking loop handles input internally via its own input polling.
- I've scanned these memory regions between button presses at different victory steps and found **0 bytes changed** across all of them:
  - 0x1D76800–0x1D77400 (battle menu state, 3072 bytes)
  - 0x1D28D00–0x1D28F00 (battle vars, 512 bytes)
  - 0x1CFF500–0x1CFF800 (result buffer, 768 bytes)
  - 0x1D27B00–0x1D27D00 (entity header, 512 bytes)
  - 0x1D29000–0x1D29400 (battle state extra, 1024 bytes)
  - 0x1D77000–0x1D77400 (win_obj area, 1024 bytes)
- The transient result buffer at 0x1CFF570 contains pre-computed EXP (3×u16 at +0x04), AP (3×u16 at +0x52), and GF stats (at +0xA0) — but these values don't change between victory steps.
- Despite the screens visually changing (confirmed via glReadPixels screenshot capture), no monitored memory changed. The step counter is likely a local stack variable in the victory loop, or stored in a memory region I haven't scanned.

## CRITICAL — Savemap Offset Correction

All ChatGPT/community deep research assumes a 96-byte (0x60) savemap header. The actual header on the PC Steam version is 76 bytes (0x4C). **Subtract 0x14 from all post-header research offsets.** Confirmed savemap base: 0x1CFDC5C.

## What I Need

1. **The memory address of the victory screen phase/step counter** — the byte or word that tracks which sub-screen (EXP / items / GF) is currently displayed. This might be a global variable, or it might be identified as a local variable in the victory loop function (in which case I need the function address and stack offset).

2. **The engine function address that runs the victory screen main loop** — the blocking loop during mode 4 that handles rendering and input for the results screens. This is the function that calls the text rendering system to display "EXP received", "Item received", "Raising GF" etc.

3. **Any known addresses for the R0WIN.DAT loaded data buffer** — where the victory screen template data ends up in memory after being loaded from disk.

4. **The function that renders text for each victory sub-screen** — the one that puts "EXP received", "Item received", "Raising GF" header text on screen. This might be part of the general win_obj / dialog text rendering system.

5. **The general text rendering function used by the battle/victory system** — any function the engine calls to draw text strings during battle mode 3 and after-battle mode 4. If I can hook this, I can detect which victory screen is active by seeing what text is being rendered.

## Platform Details

- PC Steam 2013 build, 32-bit, image base 0x00400000
- .text section: 0x00401000–0x00B69000
- .data section: 0x00B6D000–0x0279F000
- FFNx v1.23.x as OpenGL renderer (DLL injection)
- No ASLR — addresses are stable

## Known Related Addresses

- Game mode: `common_externals._mode` resolved via `get_absolute_value(main_loop, 0x115)` — points to a WORD, value 4 during victory screen
- Battle entity array: 0x1D27B18 (stride 0xD0)
- Battle comp stats: base near 0x1CFF000
- Transient result buffer: 0x1CFF570
- Encounter ID: 0x1CFF6E0
- R0WIN.DAT path string visible at 0x1CFF720: `\ff8\data\eng\battle\R0WIN.DAT`
- Battle menu state area: 0x1D76800+
- Savemap base: 0x1CFDC5C

## Sources to Check

- Qhimm wiki (wiki.ffrtt.ru) — FF8 battle system documentation
- OpenFF8 project on GitHub (especially memory.h, battle-related files)
- Deling / Makou Reactor source code for R0WIN.DAT parsing
- myst6re's FF8 research and hext files
- FFNx source code (ff8_data.cpp, ff8.h) for function address resolution chains
- Any Cheat Engine tables for FF8 that include victory screen variables
