# FF8 Victory Screen Internals — Deep Research Results (Session 51)

**Source:** ChatGPT deep research, April 2026
**Query:** Victory screen phase counter, text rendering functions, btitle.ovl

---

## Key Finding: "In Post-Battle Screen" Boolean

Address **0x01A78CA4** (FF8_EN.exe+1678CA4) is a 1-byte boolean: 0 = not on victory screen, 1 = victory screen active. From ff8-speedruns/ff8-memory repo (contributor "Kaivel"). This is a gate, not a phase counter. The victory phase counter likely lives NEAR this address — this region (0x1A78xxx) was never scanned in our memory diffs.

## Phase Counter: Confirmed Stack-Local

No public source documents the victory phase counter address. Strong evidence it's a local stack variable inside the victory loop function (sub_47CCB0 or a function it calls). This is why 528KB memory scans found 0 changes between victory steps.

## Text Rendering Entry Points (CRITICAL FOR HOOKING)

### menu_draw_text
- Primary text rendering function for ALL FF8 menu/battle UI
- Resolution: `sub_4BECC0 + 0x127` (relative call)
- Hooking this intercepts every text draw during victory screen
- Renders "EXP received", "Item received", "Raising GF" headers
- Character width function at `menu_draw_text + 0x1D0`

### get_text_data
- Retrieves text content by pool/category/text ID
- Signature: `char*(*)(int pool_id, int cat_id, int text_id, int a4)`
- Hooking this detects WHICH text string is being loaded
- Serves as a proxy for victory phase detection

### scan_get_text_sub_B687C0
- Battle-specific text retrieval (Scan spell)
- Shows battle system has its own text pipeline
- Victory text from R0WIN.DAT may use similar dedicated function

## Function Chain: main_loop → victory state machine

| Step | Resolution | Name |
|------|-----------|------|
| main_loop + 0x115 | get_absolute_value | `_mode` (WORD*, value 4 during victory) |
| main_loop + 0x330 | get_absolute_value | `battle_enter` |
| main_loop + 0x340 | get_absolute_value | `battle_main_loop` |
| battle_main_loop + 0x1B3 | get_relative_call | **`sub_47CCB0`** (master battle state dispatcher) |
| sub_47CCB0 + 0xDA | computed jump | Jump table for battle sub-states (includes victory) |
| sub_47CCB0 + 0x98D | get_relative_call | `battle_load_textures_sub_500900` |

**sub_47CCB0 is the master battle state dispatcher.** Contains computed jump table at +0xDA dispatching to different battle sub-states including victory sequence.

## R0WIN.DAT Loading Chain

sub_47CCB0 → loc_47D490 → sub_500870 → sub_500C00 → sub_506C90 → sub_5084B0 → battle_open_file_wrapper → battle_open_file → battle_filenames array

R0WIN.DAT path string at 0x1CFF720 in memory. battle_filenames array (char**) resolved from `battle_open_file + 0x11`.

## Victory Screen Overlay Texture

Identified as "menu/btl_win" in FFNx texture system. Gets `force_zsort = true` treatment for correct Z-ordering.

## Debug String Addresses for Xref

| String | Address | Purpose |
|--------|---------|---------|
| "btitle.ovl" | 0x00B80FF4 | Victory screen overlay module |
| "EXP*10" | 0x00B81160 | EXP calculation handler |
| "AP*10" | 0x00B81154 | AP calculation handler |
| "kernel.bin" | 0x00B81000 | Kernel data (ability AP costs) |
| "pet_exp.bin" | 0x00B884F8 | GF EXP/AP data tables |
| "pet_exp.msg" | 0x00B88504 | GF EXP/AP display message strings |

## battle_menu_state

Resolved from `battle_pause_window_sub_4CD350 + 0x29`. FFNx treats as void*. Values undocumented. Could contain victory sub-state info.

## Other Game State Booleans (ff8-speedruns)

| Flag | Address (exe-relative) | Absolute |
|------|----------------------|----------|
| In Post-Battle Screen | +1678CA4 | 0x01A78CA4 |
| In Menu | +1976358 | 0x01D76358 |
| In FMV | +1C9A7A0 | — |

## Recommended Approach

Hook `menu_draw_text` or `get_text_data` and log arguments. When rendered text changes from "EXP received" to "Item received" to "Raising GF", we know which victory screen is active. The text content itself tells us what data to announce. This is the most reliable approach — it reacts to what the game is actually displaying.
