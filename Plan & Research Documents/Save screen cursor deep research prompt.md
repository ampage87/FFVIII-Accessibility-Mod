# ChatGPT Deep Research Request: FF8 PC Save/Load Screen Cursor Address

## Context

I'm building an accessibility mod for Final Fantasy VIII (Steam 2013 edition, FF8_EN.exe + FFNx v1.23.x). The mod adds screen reader TTS output for blind players. I'm a Win32 DLL injected via DirectInput proxy (dinput8.dll).

I have the **in-game menu cursor** working (byte at absolute address `0x01D76C80`, which is `pMenuStateA + 0x1E6` where `pMenuStateA = 0x01D76A9A`). This byte holds values 0-10 for Junction/Item/Magic/etc.

Now I need to find the **save/load screen cursor** address. The save screen appears in two situations:
1. Title Screen → Continue (loads the memory card slot selection)  
2. In-game menu → Save (at a save point)

## What I Know

### Game Mode
- The save screen runs in **game mode 1** (`*pGameMode == 1`), same as the title screen and field gameplay. There is NO mode change when entering the save screen from the title.
- The in-game menu runs in **game mode 6** — the save screen does NOT use mode 6.

### Save Screen UI Structure (Steam/PC version)
The save screen has **two layers**:

**Layer 1: Memory card slot selection**
- Shows "Slot 1" and "Slot 2" (two PSX memory card slots)
- User selects one with Up/Down arrows and X button
- Header text: "GAME FOLDER in use: Slot N", "Load" or "Save" 
- Status text: "Checking GAME FOLDER", "No save data", or save block summaries
- The game title "FINAL FANTASY VIII" appears next to slots with save data

**Layer 2: Save block list within a slot**  
- After selecting a slot, shows 15 save blocks (individual save files)
- Each block shows: play time, character levels, location name, or "New Game" / empty
- User scrolls with Up/Down and selects with X

### Text Rendering
- Both layers render text through the standard `menu_draw_text` / `get_character_width` pipeline
- I have hooks on `get_character_width` and can capture all rendered glyph codes
- I've built a working menu font decoder (sysfnt glyph table from myst6re/Deling)
- **The decoded text is IDENTICAL regardless of cursor position** — the cursor is a graphical hand icon, not encoded in the text stream

### What I've Ruled Out
- **`pMenuStateA` region (4KB scan)**: Only a tick counter and rendering toggles change. No byte correlates with arrow key presses during the save screen. The cursor is NOT in this region.
- **`ff8_win_obj` windows array**: Zero changes during save screen. Not used.
- **GCW text content**: Identical between cursor positions. The hand cursor is a sprite overlay.
- **Game mode**: Stays at 1 throughout. No mode transition to detect.

### Key Addresses in FF8_EN.exe (Steam 2013)
- `pGameMode`: `0x01CF0890`
- `pMenuStateA`: `0x01D76A9A` (top-level menu cursor at +0x1E6 = `0x01D76C80`)
- `menu_callbacks` array: `0x00B87ED8`
- `main_menu_controller`: `0x004E3090`
- `menu_draw_text`: hooked by FFNx (our hook chains through FFNx's)
- `get_character_width`: hooked by FFNx (our hook chains through FFNx's)
- `pWindowsArray` (ff8_win_obj): field dialog windows, NOT used by save screen

### What I Need

1. **The memory address of the save/load screen cursor index** — a byte or word that indicates which slot (Slot 1 vs Slot 2) or which save block (0-14) is currently selected.

2. **The address of the save screen's state structure** — the equivalent of `pMenuStateA` for the save/load subsystem. The save system is the original PSX memory card logic adapted for PC disk folders. It likely has its own controller function and state struct.

3. **Any callback or function address for the save screen's controller** — the equivalent of `main_menu_controller` (0x004E3090) but for the save/load screen input handling. This would let me find the cursor by tracing the controller's logic.

### Useful Search Terms / References
- The save screen is part of the original PSX memory card system, ported to PC
- FFNx source: `src/ff8_data.cpp` references `menu_draw_text` and `get_character_width`
- Qhimm wiki: FF8 save system, memory card, `mngrp.bin` (menu strings), `mc00.TEX`-`mc09.TEX` (memory card textures)
- The Deling editor (myst6re/deling) handles save files via `hyne` (FF8 save editor by same author)
- OpenVIII (MaKiPL/OpenVIII-monogame) reimplements the game engine including save/load
- FFRTT wiki: https://wiki.ffrtt.ru/index.php/FF8
- FF8 decompilation projects or IDA databases for FF8_EN.exe

### Target Platform
- FF8_EN.exe: Steam 2013 release, App ID 39150
- All addresses are for the US English non-FFNx-patched executable
- The game is 32-bit x86

Please search for any reverse engineering documentation, forum posts, IDA analysis, or source code that reveals the memory address structure of FF8's save/load screen, particularly the cursor byte or the controller function that handles input on the save screen.

Findings:

I dug through the public reverse-engineering breadcrumbs, FF8 modding references, and the small amount of documented runtime-memory work that exists for the 2013/PC branch. The annoying truth-goblin is this:

**I did not find a public source that outright names the save/load cursor byte or the save/load state struct for FF8_EN.exe (Steam 2013).** What I *did* find is enough to narrow the hunt sharply and to give Claude a very concrete engineering plan instead of blind hex-poking. ([Hobbitdur][1])

## Bottom line

The best public evidence points to the save/load UI being **its own menu-ish subsystem**, not the normal in-game menu state block you already found, and not text-driven. That lines up with your observations:

* `MENUSAVE` is a dedicated field opcode whose documented behavior is simply “Opens the save menu,” which means the save screen is treated as a special engine path rather than just another page inside the standard field menu. ([Hobbitdur][1])
* The save/load strings live in the menu resource set (`mngrp.bin` and related menu assets), including the exact texts you listed such as “Slot 1”, “Slot 2”, “Choose block to save/load”, “Checking GAME FOLDER”, and “GAME FOLDER in use: Slot 1/2”. That supports your conclusion that the UI is a special menu pipeline with dedicated graphics/textures rather than plain field windows. ([Qhimm Forums][2])
* Public FF8 runtime memory documentation for the PC/Steam branch exposes a **global one-byte “Menu Choice” at `FF8_EN.exe+192B35B`**, described as inconsistent and “maybe based on the actual line number of the option.” That is not proof it is the save cursor, but it is the strongest documented *candidate* I found for a generic front-end selection index outside your `pMenuStateA` block. ([GitHub][3])

So the most useful result is:

## Most promising candidate address to test first

### Candidate 1: `FF8_EN.exe+0x192B35B`

**Type:** 1 byte
**Documented name:** `Menu Choice`
**Public description:** “A bit inconsistent. Sometimes starts at 0, sometimes starts at nonzero. Maybe it's based on the actual line number of the option.” ([GitHub][3])

That is the first address I would have Claude instrument during:

* Title → Continue → Slot 1 / Slot 2 screen
* In-game save point → Save → Slot 1 / Slot 2 screen
* Slot selected → 15-block save list

Why this is the best lead:

* It is explicitly documented as a generic **menu selection** byte, not tied to battle, field walk, or a single minigame. ([GitHub][3])
* Your own known top-level in-game menu cursor lives in a totally different state island (`pMenuStateA + 0x1E6`), which suggests the engine has **multiple cursor/state stores depending on subsystem**. ([Hobbitdur][4])
* The save screen staying in your observed “game mode 1” and not flipping into the standard menu mode fits the idea of a **global mode-1 UI cursor** rather than a `pMenuStateA` cursor. Your observation here is not from web sources, but it matches the public clue that FF8 uses different module/state addresses for different modules. ([Qhimm Forums][5])

## Other documented addresses that matter

These are not the save cursor, but they are useful anchors for narrowing the state machine.

### `FF8_EN.exe+0x18E490B` — “Can Save”

**Type:** 1 byte
**Meaning:** Publicly documented as the runtime “Can Save” byte. The ff8-memory docs say setting it to `1` allows saving anywhere; save points set it to `3`. ([GitHub][3])

Why it matters:

* It gives you a highly reliable **entry gate** for the field/save-point path.
* When you enter the save point UI, log this together with `Menu Choice`. If the cursor byte only starts changing when `Can Save != 0`, that helps separate save subsystem state from title-screen continue state. ([GitHub][3])

### `FF8_EN.exe+0x1976358` — “In Menu (or Rename Screen)”

**Type:** 1 byte
This is documented in the public ff8-memory map. ([GitHub][3])

Why it matters:

* If this flips during save/load, then the save screen is still participating in some generalized menu state.
* If it does **not** flip during title→continue or save-point→save, that is strong evidence the save UI is its own special mode-1 controller. ([GitHub][3])

### `FF8_EN.exe+0x1C3ED30` and `+0x1C3ED31` — Controller Button Presses bytes 1 and 2

**Type:** 1 byte each
The same repo documents these as experimental controller-button bytes, and separately documents the bit layout: Byte 1 includes Circle/Triangle/X/Square and shoulders; Byte 2 includes Select/Start and the D-pad directions. ([GitHub][3])

Why they matter:

* These are perfect for building a **write-breakpoint strategy**: trace what code reads the D-pad while the save screen is active.
* For your injected DLL, you can also poll these to correlate “Up/Down pressed this frame” with candidate cursor addresses. ([GitHub][3])

## What I could verify about the save/load UI resources

Public menu string references do confirm the save/load frontend is a dedicated, old-PSX-memory-card-flavored subsystem in the PC branch. The documented string set includes:

* “Save menu”
* “GAME FOLDER”
* “Slot 1”
* “Slot 2”
* “Choose block to save”
* “Choose block to load”
* “Checking GAME FOLDER”
* “Data saved”
* “No save data”
* “Load FINAL FANTASY VIII”
* “Save for FINAL FANTASY VIII”
* “GAME FOLDER in use: Slot 1”
* “GAME FOLDER in use: Slot 2” ([Qhimm Forums][2])

That matters because it supports your model of two layers:

1. memory-card-folder selection
2. save-block selection

Also, because the public resource references place these under FF8’s **menu asset set**, not field windows, your conclusion that the hand cursor is a graphical overlay rather than text-stream encoded is very plausible. ([Final Fantasy Inside][6])

## Save-file structure clues that help infer the block-list logic

The FF8 Modding Wiki’s documented PC Steam save format includes two preview fields that are relevant to the block-list screen:

* `0x000A` = **Preview: save count**
* `0x004C` = **Preview: Current save (last saved game)** ([Hobbitdur][7])

This does **not** reveal the runtime cursor address, but it is useful for reverse engineering because the block list almost certainly builds each visible row from a preview/header structure containing exactly these fields. In plain English: the save menu controller likely has to maintain both a **selected block index** and some buffer of loaded preview headers. The cursor variable you want is therefore likely adjacent to either:

* the current preview-entry pointer,
* the current block number,
* or a small state byte distinguishing **slot-selection layer** vs **block-list layer**. ([Hobbitdur][7])

## Public evidence about module structure

A Qhimm reverse-engineering thread for FF8 PC 2000 documents a field-side “module index to be transported” at `0x01CE4760`, with different values invoking field, battle, in-game menu, world, etc., and explicitly notes that **other modules use different memory addresses**. The same thread also says the important engine region is around `0x00400000-0x004F0000`. Separately, the FF8 Modding Wiki FAQ states the 2000 and 2013 versions are “literally on binary level” the same except for Steam-related additions at the end, and that addresses match. ([Qhimm Forums][5])

Why this matters for Claude:

* Do **not** assume the save screen’s cursor must live near the standard menu state.
* Do assume there is probably a **distinct controller/update function** for the save UI somewhere in the `.text` region, likely fed from common input bytes and writing to a generic selection byte or small struct. ([Qhimm Forums][5])

## My assessment of what is *most likely true*

Here is the working theory, clearly labeled as theory rather than proved fact:

### Working theory

The save/load frontend in FF8 PC/Steam uses a **small dedicated mode-1 UI state block**, separate from `pMenuStateA`, with at least:

* one byte for current **phase/layer** (`slot_select` vs `block_select`),
* one byte for **current choice** within that phase,
* maybe one byte for scroll/top-row if the block list pages or scrolls,
* and possibly a pointer to the currently loaded slot/save preview table.

The best public runtime candidate for the actual visible selection is the global `Menu Choice` byte at `FF8_EN.exe+192B35B`. The best way to find the surrounding struct is to identify **who writes to that byte while the save screen is active**. ([GitHub][3])

That theory fits:

* your failure to find movement in `pMenuStateA`,
* the save UI staying outside the normal in-game menu module,
* and the public description of a separate generic “Menu Choice” byte that behaves oddly across different menu contexts. ([GitHub][3])

## What I would tell Claude to do next

This is the practical handoff.

### 1) Instrument these exact addresses first

Log them every frame while on:

* title main menu
* title → Continue slot-select
* title → Continue block-list
* field save point → Save slot-select
* field save point → Save block-list

Addresses:

* `0x0192B35B` — candidate generic selection byte (`Menu Choice`) ([GitHub][3])
* `0x01976358` — “In Menu (or Rename Screen)” ([GitHub][3])
* `0x018E490B` — “Can Save” ([GitHub][3])
* `0x01C3ED30`, `0x01C3ED31` — controller button bytes ([GitHub][3])

Expected outcomes:

* If `0x192B35B` toggles `0 ↔ 1` on Slot 1 / Slot 2, you likely have the slot-layer cursor already.
* If it becomes `0..14` in the block list, the hunt is basically over.
* If it changes in one layer but not the other, then it is either phase-specific or accompanied by a second index nearby.

### 2) Put a write breakpoint on `0x192B35B`

If the debugger can do hardware watchpoints, this is the high-value move.

Goal:

* capture the instruction(s) that write the candidate selection byte while pressing Up/Down on the save screen.

What to extract:

* the writing instruction address,
* the base register used,
* nearby offsets in the same struct,
* comparison constants (`cmp al, 1`, `cmp al, 0x0F`, etc.) that reveal the selection range.

A deliciously useful pattern would be something like:

* decrement/increment a byte,
* clamp to `1` for slot-select or `0x0F` for block-select,
* branch on confirm/cancel,
* then draw the hand sprite from that same byte.

### 3) If `0x192B35B` is too noisy, break on reads of D-pad input during save screen

Use the documented button bytes:

* Byte 2 bit `+16` = Up
* Byte 2 bit `+64` = Down
* combinations also exist for diagonals, though the save menu probably ignores them. ([GitHub][8])

Strategy:

* break on code reading `0x01C3ED31`,
* only while the save screen is up,
* walk forward to the code that mutates a choice/index byte.

This often works better than “unknown value scan” when the UI state is stored in a compact struct.

### 4) Search around the writer for a tiny state struct

Once you catch the writer to `Menu Choice`, inspect surrounding fields. I would specifically test for:

* `state + 0x00` = phase/layer
* `state + 0x01` = current choice
* `state + 0x02` = previous choice / redraw dirty
* `state + 0x03` = scroll top index
* `state + 0x04` = active slot
* `state + 0x08/+0x0C` = pointer to loaded save previews

Why those guesses? Because the public save format shows distinct preview/header data per save block, and the UI has a clean two-stage flow. Some small controller struct almost has to remember both **which folder/slot is active** and **which block entry is highlighted**. ([Hobbitdur][7])

### 5) Test both entry paths separately

Do not assume the title Continue route and save-point Save route use the exact same controller instance.

Public evidence says the string/UI resources are shared, but your own observation says the title path stays in mode 1 and does not pass through the standard menu module. The field opcode `MENUSAVE` also implies an engine-level transition into the save subsystem. Those may converge on the same save UI logic, or they may populate it from different entry shims. ([Hobbitdur][1])

### 6) Don’t waste more time scanning `pMenuStateA`

Everything public plus your tests says that is the wrong rabbit hole for save/load. The normal in-game menu cursor you already found is real, but the save UI appears to live elsewhere. ([Hobbitdur][1])

## What I can state confidently vs what remains unknown

### Confident

* Save/load is a dedicated subsystem opened by opcode `MENUSAVE`, not just a text variant of the standard field window system. ([Hobbitdur][1])
* The save UI strings/resources are part of the menu asset stack and include the two-layer prompts you described. ([Qhimm Forums][2])
* There is a documented global runtime byte `FF8_EN.exe+192B35B` called `Menu Choice`, and it is the best public candidate for a non-`pMenuStateA` selection index. ([GitHub][3])
* There are documented runtime input bytes at `FF8_EN.exe+1C3ED30/31` that can be used to trace the controller/update function. ([GitHub][3])
* `FF8_EN.exe+18E490B` is the save-enable byte and is useful for gating the save-point path. ([GitHub][3])

### Not yet publically pinned down

* Exact save/load cursor address for slot-select
* Exact save/load cursor address for block-select
* Exact base pointer / struct for the save/load controller
* Exact controller function address for the save/load subsystem

That part remains undocumented in the public sources I could locate. ([GitHub][3])

## Concise handoff for Claude

You can hand this chunk over almost verbatim:

> Public docs do not expose a named save/load cursor address for FF8 Steam 2013. The strongest documented candidate is `FF8_EN.exe+192B35B` (`Menu Choice`, 1 byte), which is described as a generic menu selection byte and may map to save/load selection. Also log `FF8_EN.exe+1976358` (`In Menu`), `FF8_EN.exe+18E490B` (`Can Save`), and controller input bytes `FF8_EN.exe+1C3ED30/31`. The save UI is a dedicated subsystem opened by field opcode `MENUSAVE`, and the public menu strings confirm the two-layer save/load flow (`Slot 1/2`, then `Choose block to save/load`). Recommended approach: hardware write breakpoint on `0x192B35B` during title→Continue and field→Save; if noisy, break on reads of `0x1C3ED31` (D-pad byte) while save screen is active and trace forward to the code that mutates the selection byte/struct. Expect a small dedicated mode-1 save UI state struct, not `pMenuStateA`. ([Hobbitdur][1])

The public trail gets you to the right cave entrance, but the exact cursor byte is still hiding like a smug goblin in the dark. The likely next breakthrough is not another web search. It is a debugger session centered on `0x192B35B` and the input bytes.

[1]: https://hobbitdur.github.io/FF8ModdingWiki/technical-reference/field/field-opcodes/12e-menusave/ "12E_MENUSAVE | FF8 Modding wiki"
[2]: https://forums.qhimm.com/index.php?topic=15387.25&utm_source=chatgpt.com "[WIP] Final Fantasy VIII Greek Translation - Qhimm.com Forums"
[3]: https://github.com/ff8-speedruns/ff8-memory "GitHub - ff8-speedruns/ff8-memory: Final Fantasy VIII research and tools · GitHub"
[4]: https://hobbitdur.github.io/FF8ModdingWiki/technical-reference/exedata/memory-info/ "Memory info | FF8 Modding wiki"
[5]: https://forums.qhimm.com/index.php?topic=16838.0&utm_source=chatgpt.com "[FF8] Engine reverse engineering - Qhimm.com Forums"
[6]: https://wiki.ffrtt.ru/index.php/FF8?utm_source=chatgpt.com "FF8 - Final Fantasy Inside - FFRTT"
[7]: https://hobbitdur.github.io/FF8ModdingWiki/technical-reference/miscellaneous/game-save-format/ "Game Save Format | FF8 Modding wiki"
[8]: https://github.com/ff8-speedruns/ff8-memory/blob/main/buttons.md "ff8-memory/buttons.md at main · ff8-speedruns/ff8-memory · GitHub"

