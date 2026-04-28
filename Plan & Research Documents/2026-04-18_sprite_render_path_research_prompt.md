# Deep Research Prompt: FF8 Battle Floating-Sprite Render Path

**For use with ChatGPT Deep Research.** Paste the entire prompt below (from the horizontal rule down) into ChatGPT's Deep Research mode. The goal is to identify the FF8 Steam (2013) EXE function that creates the floating damage-number / "Miss" / "No Effect" / status-icon sprites that appear above battlers during combat.

---

## Context

I'm a developer building an accessibility mod for **Final Fantasy VIII Steam 2013 edition** (FF8_EN.exe, app ID 39150). The mod is a `dinput8.dll` shim that uses MinHook to intercept engine functions and generate TTS announcements for blind players. I need to find a specific battle-engine function and I'm looking for help from public FF8 reverse-engineering sources.

### The specific function I'm looking for

Final Fantasy VIII's battle screen renders **floating text sprites** over battlers:

- **Damage numbers** — e.g. "77" appearing over an enemy when hit
- **"Miss" text** — when a physical attack is dodged (common with Blinded attackers or high-evasion enemies)
- **"No Effect"** / **"Immune"** — when a status spell targets a resistant or already-afflicted enemy
- **Small status icons** — sleep Zs, silence symbols, poison indicators, etc.

These are distinct from **UI text** (command menus, victory screens, dialog boxes). UI text uses a different path — in particular `sub_47EC70` aka `get_battle_text(int text_id)` which is a text-ID-to-string resolver used during victory sequences and similar.

The floating sprites appear as **world-space billboards** positioned over specific battler entities, animating upward and fading out.

### What I've already ruled out

I've instrumented and tested multiple candidate functions. None of them handle the floating sprites:

1. **`sub_4877F0`** (at `0x004877F0`) — the spell/attack **result dispatcher**. Takes arguments (target_slot, kind, a3, a4). Kind=4 is the "status resist / no effect" branch. I hook this for semantic detection (e.g. kind=4 + a3=0x9 → status resist), but it fires at *result decision time*, not at sprite render time, and it doesn't create sprites itself.

2. **`sub_487DF0`** (at `0x00487DF0`) — a **bytecode interpreter** called by kind=4. Has an opcode dispatch table at `0x0048A0B8` with ~61 entries. I traced through its disassembly expecting it to call `sub_48D200` at `0x004881D3` (which would arrive at return address `0x004881D8`). **That call site never fires in practice** in live testing, confirmed via `_ReturnAddress()` capture in the hook.

3. **`sub_48D200`** (at `0x0048D200`) — initially thought to be the popup-sprite dispatcher based on static analysis showing ~12 callers. **Empirically, it only handles action-announce popups** — the icon that appears next to the *active* character showing what action they're about to take (physical attack = text_id 0x01, spell cast = text_id 0x06 with value = spell ID). The only return address I observe in live battle is **`0x00485938`** (one caller, always action-announce). The damage-number sprite "77" was definitively captured on-screen via OpenGL `glReadPixels` screenshot during a Strike Raid hit, but **no corresponding call to `sub_48D200` fired** during that window — proving the damage sprite goes through a different function.

4. **`sub_47EC70`** (text resolver) — used by victory screen text pipeline, has a completely different visual rendering style from floating battle sprites. Not a fit.

5. **Other hooked functions (not the render path, but installed for other purposes)**:
   - `sub_4B7210` @ `0x004B7210`
   - `sub_4A3EE0` @ `0x004A3EE0`
   - `sub_5348E0` @ `0x005348E0`
   - `sub_47EA30` @ `0x0047EA30` (ability name resolver)
   - `sub_47EA90` @ `0x0047EA90` (ability name resolver variant)
   - `sub_47E970` @ `0x0047E970` (GF name resolver)
   - `sub_47E710` @ `0x0047E710` (item name resolver)
   - `sub_483400` @ `0x00483400` (unrelated sprite-spawner for item pickup, not the one)
   - `sub_483470` @ `0x00483470` (dispatch)
   - `sub_482F80` @ `0x00482F80` (dispatch)
   - `sub_4842B0` @ `0x004842B0` (ATB tick)
   - `sub_4B0500` @ `0x004B0500` (GF effect timer)

### Timing evidence

The floating-sprite render is **tightly coupled** to a memory flag at **`0x01D280C0`** (the "damage animation flag"). This byte transitions 0→1 when a result-animation starts and 1→0 when it ends:

- Strike Raid limit break hit → flag set, flag cleared after **266ms** → "77" damage number was on screen when I screenshot at 400ms.
- Quistis physical attack → flag set, flag cleared after **2875ms** → "158" damage number didn't appear until the end of that window.
- Spell resist (Sleep on sleeping target) → flag set, flag cleared after **~8 seconds** → screenshot at 400ms caught nothing visible.

The flag's lifetime directly correlates with the sprite's visible lifetime. Whatever function creates the sprite is called shortly after kind=4 dispatch, and it's driven by the same subsystem that sets/clears `0x01D280C0`.

### Known related data addresses

- **Battler struct array** base: `0x01D27B10` (208 bytes per battler, 7 slots). HP is at a known offset inside this struct; previous-HP tracking works correctly.
- **Active-battler index**: `0x01D27B00` (0xFF = none active)
- **Damage animation flag**: `0x01D280C0` (byte, 0 or 1)
- **Battle menu phase** area: `0x01D76800`+ (battle UI state)
- **GF state** area: `0x01D76860`+
- **Battle magic ID**: `0x01D99A68`
- **Savemap base**: `0x1CFDC5C` with 76-byte (0x4C) header

### Engine architecture notes

FF8 PC 2013 is the Steam re-release of the 2000 PC port, which in turn was ported from the 1999 PSX original. Battle sprites are rendered via OpenGL (through FFNx or the original renderer when FFNx is not loaded). The battle system is largely unchanged from PSX architecture — damage numbers in PSX FF8 are rendered as sprite quads using the PSX GPU's sprite primitives, and in PC they pass through the PSX-GPU-emulation layer that ships in FF8_EN.exe (which is why the same function handles both).

The function I'm looking for likely:

- Takes a target battler slot (or pointer into the battler array at `0x01D27B10`) and a numeric/text value
- Writes to a **floating sprite queue** or **particle system** struct
- Is called once per sprite spawn — damage numbers appear once per hit, not continuously
- May have multiple entry points (one for damage numbers, one for text strings like "Miss"), or a single entry point with a type/kind parameter
- Sets or interacts with the `0x01D280C0` animation flag, or a parent function of it does
- Lives somewhere in the `0x00480000`–`0x004D0000` range (where most battle code lives)

## What I want ChatGPT to find

Search the following sources for information that would let me identify this function's address (or a narrow range of candidates):

1. **FFNx source code** (`github.com/julianxhokaxhiu/FFNx`) — this is an active, open-source FF8 and FF7 engine mod/shim. Its `src/ff8_data.cpp`, `src/ff8/battle/` directory, and related files contain hundreds of FF8_EN.exe addresses that FFNx hooks. **Search specifically for any hook or address reference involving:**
   - `damage_display`, `damage_popup`, `popup_damage`, `float_text`, `float_damage`, `battle_popup`, `spawn_popup`, `show_damage`, `display_hp_change`, `damage_number`, `battle_text_sprite`, `sprite_popup`, `add_popup`, `battle_damage_display`
   - Any reference to address `0x4D0000`–`0x4F0000` range in battle context
   - Any function that FFNx names related to battle result display
   - The FFNx changelog / commit history may mention battle damage fixes

2. **Qhimm Wiki** (`wiki.ffrtt.ru`) and **Qhimm Forums** (`forums.qhimm.com`) — extensive FF7/FF8 reverse engineering community. Search for threads on:
   - FF8 battle engine disassembly
   - Damage number modifications (there are mods that change damage display color/size)
   - "Miss" / "No Effect" text location
   - Status icon rendering
   - Specifically search for posts by users like **DLPB**, **MaKiPL**, **myst6re**, **Aali**, **Hyne**, and **sithlord48** who are the primary FF8 reversers.

3. **MaKiPL's GitHub** (`github.com/MaKiPL`) — has multiple FF8 tools including disassemblers and battle data parsers. His **openfftools** / **FF8Mod** / **Makoto** repos may name the damage display function.

4. **ifrit-al / MonoGame-FF8 / OpenVIII** (`github.com/MaKiPL/OpenVIII-Monogame`) — a C# reimplementation of FF8. While not a direct address reference, the **function names** in OpenVIII's battle code may reveal naming conventions used in public disassemblies. Look for methods on the `Battle` class that display floating damage.

5. **hyne** / **deling** / **Makoto** source code for any battle-related struct definitions.

6. **ff8info.com**, **ffviiiinfo**, the **Ultima.fr** FF8 wiki, and similar — gameplay info that might reference modding guides.

7. **GameHacking.org** and **cheatengine forums** — FF8 cheat tables often label the damage display function and related memory addresses.

8. **General web search** for FF8 PC modding terms like "ff8 battle damage display function address" and similar.

## Output format I need

Please produce:

1. **Primary candidate address**: the single most likely FF8_EN.exe function address for the floating-sprite spawn/render function, with citation(s) to the source that identifies it.

2. **Secondary candidates**: 2–5 additional addresses worth investigating, each with a one-sentence rationale.

3. **Function signature guess**: based on calling conventions used elsewhere in FF8 (cdecl, typically), propose what arguments this function likely takes. Something like `void spawn_damage_popup(int target_slot, int value, int type_flags)` or similar.

4. **Related struct layout**: if any sources describe the battler struct offsets used by the popup system (damage-to-display field, popup-request flags, etc.), list them.

5. **Related flag**: confirm or clarify what `0x01D280C0` represents — is it the damage animation flag specifically, or a broader "action in progress" flag?

6. **Verification hints**: suggest what byte pattern or instruction sequence I should look for at the candidate addresses to confirm I've found the right function. For example: "expect a function that reads from the battler at 0x01D27B10+slot*208, writes a byte to [battler + offset X], and increments a per-sprite counter at 0xYYYYYYYY."

7. **FFNx hook pattern** (if found): the exact hook pattern FFNx uses if it does hook this function, so I can match it.

Please cite your sources inline. If a source is ambiguous or contradicts another source, note that. If you cannot find a specific address and only find general architecture info, say so plainly — I'd rather have honest "I couldn't find a specific address, but here's what I learned" than a guess.

Do not speculate about addresses without a source. Do not invent offsets. It's okay to say "this function almost certainly exists somewhere around `0x004Dxxxx` based on surrounding code patterns" if that's the best you can do — just be clear about confidence level.
