# RESEARCH: FF8 Dialog System — Squall's Thoughts & Non-Standard Text Rendering
## Started: 2026-03-04
## Goal: Understand how FF8 renders text that bypasses the standard ff8_win_obj window system

---

## PROBLEM SUMMARY

Squall's internal thoughts (gray italic text, no dialog border) are:
- NOT delivered via any hooked opcode (MES/MESW/ASK/AMES/AASK/AMESW)
- NOT stored in ff8_win_obj windows array (pWindowsArray at 0x01D2B330)
- NOT fetched via field_get_dialog_string (0x00530750) — hook installed, zero calls logged
- The GETSTR hook silence confirms FFNx's eax.dll may redirect the call chain

We need to find what code path renders these thoughts so we can hook it.

---

## RESEARCH PLAN

### STEP 1: FFNx Source Analysis — Dialog & Text Systems
Search the local FFNx source for all references to dialog, text rendering, tutorial,
and window management. Map out every function FFNx hooks related to text display.

- [x] 1a. Examine ff8.h for struct definitions (ff8_win_obj, related structs)
- [x] 1b. Examine ff8_data.cpp for ALL dialog-related hooks and externals
- [x] 1c. Examine ff8/field.cpp for field-specific text handling
- [x] 1d. Examine voice.cpp for how Echo-S/voice mod intercepts dialog text
- [x] 1e. Search all FFNx source for "tuto", "tutorial", "thought" references
- [x] 1f. Search all FFNx source for additional text rendering functions

### STEP 2: FF8 Game Mode Analysis
Investigate whether thoughts trigger a game mode change (MODE_TUTO=10, etc.)

- [x] 2a. Check FFNx source for MODE_TUTO / mode 10 handling (FOUND in voice.cpp)
- [x] 2b. Search for any mode-dependent text rendering paths (show_dialog branches by mode)
- [x] 2c. Document all game modes and their text display mechanisms (see 1d findings)

### STEP 3: Online Research — FF8 Modding Community
Search for community documentation on FF8's text rendering system.

- [ ] 3a. Search Qhimm forums for FF8 dialog/thought rendering
- [ ] 3b. Search for FF8 JSM opcode documentation (complete opcode list)
- [ ] 3c. Search for Deling/Makou Reactor docs on FF8 field script opcodes
- [ ] 3d. Search for any documentation on FF8's tutorial/thought system
- [ ] 3e. Check if there's an opcode specifically for "gray text" or "narration"

### STEP 4: FF8 Game File Analysis
Examine the actual field scripts for the hallway scene where thoughts appear.

- [ ] 4a. Identify the field file for the hallway scene (bghall_6 or similar)
- [ ] 4b. Look for field script decompilation tools/docs
- [ ] 4c. If possible, examine the JSM script for the thought scene
- [ ] 4d. Identify which opcode(s) the script uses for thought display

### STEP 5: Binary Analysis Clues
Use what we know from FFNx to trace alternative text rendering paths.

- [ ] 5a. Check FFNx's replace_call / replace_function usage on dialog functions
- [ ] 5b. Map the call chain: who calls field_get_dialog_string besides opcodes?
- [ ] 5c. Look for other text buffer addresses or rendering functions
- [ ] 5d. Check if there's a separate "draw_text" function for non-windowed text

### STEP 6: Synthesis & Implementation Plan
Combine findings into a concrete hooking strategy.

- [x] 6a. Document all discovered text rendering paths (see findings below)
- [x] 6b. Identify the specific function(s) to hook for thoughts (show_dialog)
- [x] 6c. Design the hook implementation approach (MinHook on show_dialog)
- [x] 6d. Update DEVNOTES.md with complete findings
- [x] 6e. IMPLEMENTED in v04.17: Hook_show_dialog in field_dialog.cpp

---

## FINDINGS LOG

### Step 1a: ff8.h struct & enum analysis (COMPLETE)
**Critical discoveries:**
1. `FF8_MODE_TUTO = 10` — Tutorial mode IS a real game mode. Thoughts likely use this.
2. `opcode_tuto` exists in ff8_externals — There IS a TUTO opcode! We aren't hooking it.
3. `opcode_ramesw` exists in ff8_externals — Another dialog opcode we're NOT hooking.
4. `current_tutorial_id` (BYTE*) — FFNx tracks which tutorial is active.
5. `show_dialog(int32_t, uint32_t, int16_t)` — A separate dialog display function.
6. `ff8_win_obj` struct confirmed: text_data1/text_data2 are char* at offset +0x08/+0x0C.
7. `opcode_mesmode` exists — Controls dialog display mode.
8. `field_get_dialog_string` and `set_window_object` confirmed in externals.

**Key insight:** `opcode_tuto` is almost certainly the opcode that triggers Squall's
thoughts. The tutorial system in FF8 is reused for narrative "thoughts" — they share
the same gray borderless text rendering. We need to hook opcode_tuto.

### Step 1b: ff8_data.cpp opcode & dialog analysis (COMPLETE)
**Critical discoveries from opcode dispatch table:**
1. `opcode_tuto` = dispatch table index `0x177` — THE tutorial/thought opcode!
   - `current_tutorial_id` extracted at opcode_tuto + 0x2A
2. `opcode_mesmode` = index `0x106` — controls dialog display mode
3. `opcode_ramesw` = index `0x116` — another dialog opcode we're NOT hooking
4. `opcode_mesw` is NOT listed in ff8_data.cpp's opcode extraction
   (we found it ourselves at 0x46, which means FFNx doesn't hook/track it)
5. `show_dialog(int32_t, uint32_t, int16_t)` resolved from sub_4A0C00 + 0x5F
6. `field_dialog_current_choice` exists in ff8_externals struct
7. `field_get_dialog_string` confirmed: resolved from opcode_mes + 0x5D
8. `update_tutorial_info_4AD170(int)` — battle-related tutorial update function

**Complete opcode map from ff8_data.cpp dispatch table:**
- 0x0B: popm_b, 0x0C: pshm_w, 0x0D: popm_w
- 0x21: effectplay2
- 0x29: mapjump
- 0x47: mes, 0x48: messync, 0x4A: ask, 0x4C: winclose
- 0x4F: movie, 0x50: moviesync
- 0x56: spuready
- 0x64: amesw, 0x65: ames
- 0x69: battle
- 0x6F: aask
- 0xA1: setvibrate, 0xA3: movieready
- 0xB5: musicload, 0xBA: crossmusic, 0xBB: dualmusic
- 0xC1: musicvoltrans, 0xC2: musicvolfade
- 0x106: mesmode
- 0x116: ramesw
- 0x129: menuname
- 0x135: choicemusic, 0x137: drawpoint, 0x13A: cardgame
- 0x144: musicskip, 0x149: musicvolsync
- 0x151: addgil, 0x153: addseedlevel
- 0x16F: getmusicoffset
- **0x177: tuto** <-- KEY FINDING

**Missing from our hooks:** opcode_mesw (0x46 — we hook it but FFNx doesn't track it),
opcode_ramesw (0x116), opcode_mesmode (0x106), and most critically opcode_tuto (0x177).

### Step 1c: field.cpp analysis (COMPLETE)
field.cpp is mostly FF7-specific. The only FF8 code is `ff8_field_init_from_file`
which proxies field script file loading for triangle ID resolution. No dialog-specific
code found here. The ff8/field/ subdirectory only has background.cpp and chara_one.cpp
(character model loading). No useful dialog information.

### Step 1d: voice.cpp analysis — CRITICAL FINDINGS (COMPLETE)
**This file reveals the entire FFNx dialog interception architecture.**

1. **FFNx replaces `field_get_dialog_string` entirely:**
   ```
   replace_function(ff8_externals.field_get_dialog_string, ff8_field_get_dialog_string);
   ```
   This is why our hook at 0x00530750 never fires! `replace_function` overwrites the
   function prologue. Our MinHook on that address is dead — the original function is
   completely replaced by FFNx's version.

2. **FFNx replaces `show_dialog` (the central rendering function):**
   ```
   replace_call(ff8_externals.sub_4A0C00 + 0x5F, ff8_show_dialog);
   ```
   The original `show_dialog(window_id, state, a3)` is wrapped by `ff8_show_dialog`.
   ALL text display passes through this function: field, battle, world, AND tutorials.

3. **ff8_show_dialog handles MODE_TUTO:**
   ```cpp
   else if (mode->mode == FF8_MODE_TUTO) {
       // reads win->text_data1 and uses current_tutorial_id
       std::string decoded_text = ff8_decode_text(win->text_data1);
       // ...
       sprintf(voice_file, "_tuto/%04u/%s", *ff8_externals.current_tutorial_id, ...);
   }
   ```
   Tutorials/thoughts DO use the window system and `text_data1` — but they render
   under `mode->mode == FF8_MODE_TUTO` (mode 10), not MODE_FIELD.

4. **FFNx hooks all dialog opcodes via `patch_code_dword`:**
   - 0x47 (mes) → ff8_opcode_voice_mes
   - 0x65 (ames) → ff8_opcode_voice_ames
   - 0x64 (amesw) → ff8_opcode_voice_amesw
   - 0x116 (ramesw) → ff8_opcode_voice_ramesw
   - 0x4A (ask) → ff8_opcode_voice_ask
   - 0x6F (aask) → ff8_opcode_voice_aask
   - 0x137 (drawpoint) → ff8_opcode_voice_drawpoint
   BUT NOT 0x46 (mesw) — confirming FFNx doesn't track mesw.

5. **Opcode hook interaction with us:**
   FFNx uses `patch_code_dword` which writes to the dispatch table AFTER
   our MinHook has installed. Since FFNx patches the dispatch table entries,
   FFNx's hooks replace ours for opcodes like 0x47, 0x65, etc.
   **Wait — we also patch the dispatch table!** This means whoever patches LAST wins.
   Our hooks may be overriding FFNx's voice hooks, or vice versa depending on load order.
   With dinput8.dll (us) loading before eax.dll (FFNx), FFNx patches LAST and wins.
   This explains why our opcode hooks DO fire — we use MinHook on the function addresses,
   not the dispatch table. So our hooks are on the underlying functions, while FFNx
   replaces the dispatch table entries to point to its own wrapper functions that
   call the (now-hooked-by-us) originals. Both hook chains execute.

6. **The KEY insight — `show_dialog` is the universal text rendering point:**
   `ff8_externals.show_dialog(window_id, state, a3)` is the original game function
   that renders ALL text to screen. FFNx wraps it in `ff8_show_dialog` which handles
   voice acting. If we hook the ORIGINAL `show_dialog` address, we'd catch everything
   including tutorials/thoughts.

7. **Tutorial/thought text location confirmed:**
   - Text is in `win->text_data1` (same as regular dialog)
   - Tutorial ID from `*ff8_externals.current_tutorial_id`
   - Game mode is `FF8_MODE_TUTO` (mode 10) when thoughts display
   - `win->open_close_transition` tracks dialog state transitions
   - `win->state` tracks internal dialog state
   - `win->field_30` used for dialog text change detection

8. **ff8_field_get_dialog_string replacement is simple:**
   ```cpp
   char *ff8_field_get_dialog_string(char *msg, int dialog_id) {
       ff8_current_window_dialog_id = dialog_id;
       return msg + *(uint32_t *)(msg + 4 * dialog_id);
   }
   ```
   Just pointer arithmetic + storing dialog_id globally.

**STRATEGY IMPLICATIONS:**
- We should NOT try to hook field_get_dialog_string (dead due to FFNx replace_function)
- We SHOULD consider monitoring `*pGameMode` for MODE_TUTO transitions
- We COULD hook `show_dialog` to catch ALL text including tutorials
- We need to find `show_dialog`'s address: it's at `ff8_externals.show_dialog`
  which was resolved from `sub_4A0C00 + 0x5F`
- `sub_4A0C00` is NOT `set_window_object` (0x4A0410). It's a DIFFERENT function at 0x4A0C00!
  - `set_window_object` = resolved from opcode_mes + 0x66 = 0x004A0410
  - `sub_4A0C00` = resolved from sub_4A0880 + 0x33 = 0x004A0C00
  - `show_dialog` = CALL target at sub_4A0C00 + 0x5F
- FFNx uses `replace_call` (NOT `replace_function`) for show_dialog!
  This means the original function still exists at its real address.
  FFNx only patches the CALL instruction at sub_4A0C00+0x5F.
  FFNx's ff8_show_dialog eventually calls the original via stored pointer.
  If we MinHook the original show_dialog, FFNx's trampoline passes through us.
- `current_tutorial_id` = (BYTE*)get_absolute_value(opcode_tuto, 0x2A)

**CONCRETE HOOKING STRATEGY:**
1. Resolve show_dialog: follow call chain sub_4A0880 → sub_4A0C00 → show_dialog
   - We already have opcode_mes (0x528F20), can resolve sub_4A0C00 differently
   - Or: resolve from our known set_window_object chain
   - Or: hardcode based on FFNx's resolution pattern
2. MinHook show_dialog(int32_t window_id, uint32_t state, int16_t a3)
3. In hook: check game mode via *_mode for FF8_MODE_TUTO (10)
4. If TUTO mode: read win->text_data1, decode, speak
5. Also resolve current_tutorial_id from opcode_tuto + 0x2A for logging

### Step 1e: Search for "tuto"/"tutorial" across FFNx source (COMPLETE)
- `FF8_MODE_TUTO = 10` in ff8_game_modes enum (ff8.h)
- In ff8_data.h mode table: maps to `MODE_UNKNOWN` driver mode
- `opcode_tuto` = dispatch table[0x177] (ff8_data.cpp)
- `current_tutorial_id` = BYTE* at opcode_tuto + 0x2A (ff8_data.cpp)
- `update_tutorial_info_4AD170(int)` = battle-related tutorial update (ff8_data.cpp)
- voice.cpp ff8_show_dialog has full MODE_TUTO handling block
- FF7 has `opcode_voice_tutor` and `ff7_menu_tutorial_render` for tutorial voice

### Step 1f: Additional text rendering functions (COMPLETE)
- `show_dialog(int32_t, uint32_t, int16_t)` = THE universal text renderer
  Signature: char show_dialog(int32_t window_id, uint32_t state, int16_t a3)
  Resolved: get_relative_call(sub_4A0C00, 0x5F)
- `menu_draw_text` = menu-specific text drawing (separate system)
- `get_text_data` = retrieves text data for menus
- `ff8_draw_icon_or_key*` = icon/button prompt rendering
- `scan_get_text_sub_B687C0` = battle scan text
- No other field/tutorial text rendering functions found — everything goes through show_dialog

### Step 3: Online Research - TUTO Opcode Documentation (COMPLETE)

**Sources checked:**
- wiki.ffrtt.ru FF8/Field/Script/Opcodes (full table)
- ff7-mods.github.io FF8 opcode reference
- Qhimm Modding Wiki (fandom) FF8 opcodes
- FF7's TUTOR opcode 0x21 (for comparison)

**KEY FINDING: Opcode 0x177 is poorly documented by the community.**
- The Qhimm fandom wiki lists 0x177 as **"UNKNOWN12"** with NO function type
- The ffrtt.ru wiki has the same table but doesn't have a dedicated page for 0x177
- ff7-mods.github.io lists it as "177 TUTO" (may have been updated from FFNx knowledge)
- NO wiki page exists describing TUTO's parameters or behavior
- There is also opcode 0x15A **MENUTUTO** (categorized as "Menus") — this is the
  menu-based tutorial/Information system, NOT the field thought bubble system

**FF7 comparison:**
- FF7 has opcode 0x21 TUTOR which opens the main menu and plays a tutorial by ID
- FF8's TUTO (0x177) serves a similar but different purpose: it triggers the
  tutorial/thought overlay display system that switches to MODE_TUTO (10)
- The FF8 TUTO system is used for both gameplay tutorials AND Squall's internal thoughts

**What FFNx tells us that the wiki doesn't:**
- `opcode_tuto` is at dispatch_table[0x177]
- It takes a tutorial_id parameter (BYTE) resolved at opcode_tuto + 0x2A
- When executed, the game enters FF8_MODE_TUTO (10)
- Text is stored in win->text_data1 within the ff8_win_obj window system
- show_dialog() renders the text under mode == 10 (TUTO)
- FFNx's voice system maps audio files as `_tuto/{tutorial_id}/{hash}`

**CONCLUSION:**
The community documentation gap confirms why this is an overlooked text path.
FFNx's source code is the authoritative reference for TUTO behavior.
Our hooking strategy (show_dialog with MODE_TUTO check) is well-founded.

---

## FINAL IMPLEMENTATION PLAN

### Approach: Hook show_dialog for MODE_TUTO detection

**Why show_dialog, not opcode_tuto:**
- opcode_tuto just initiates the tutorial — it doesn't have the text content yet
- show_dialog is called repeatedly during text rendering with actual text in win->text_data1
- show_dialog already handles all game modes (field, battle, world, tuto)
- Single hook point for ALL text types

**Hook target:** `show_dialog(int32_t window_id, uint32_t state, int16_t a3)`
- Address: resolve from sub_4A0C00 + 0x5F (CALL target)
- sub_4A0C00 = resolve from sub_4A0880 + 0x33
- Alternative: scan for known byte pattern from FFNx's resolution chain

**Detection logic in hook:**
```
if (*pGameMode == 10) {  // FF8_MODE_TUTO
    ff8_win_obj* win = &windows_array[window_id];
    if (win->text_data1 != nullptr) {
        std::string text = ff8_decode_text(win->text_data1);
        if (text != last_spoken_tuto_text) {
            speak(text);
            last_spoken_tuto_text = text;
        }
    }
}
```

**Risk: FFNx interaction**
- FFNx uses `replace_call` at sub_4A0C00+0x5F → patches one CALL site
- FFNx stores original show_dialog pointer and calls it from ff8_show_dialog
- If we MinHook the original show_dialog address, FFNx's trampoline → our hook → original
- This should work because replace_call only patches the CALL instruction, not the function itself
- Our MinHook patches the function prologue directly

**Implementation order:**
1. Resolve show_dialog address at DLL init
2. MinHook show_dialog with our wrapper
3. In wrapper: check mode, decode text, speak if TUTO
4. Add dedup (last_spoken_text check) to prevent repeats
5. Test with infirmary hallway Squall thoughts
6. Later: extend to handle all modes for unified dialog TTS

