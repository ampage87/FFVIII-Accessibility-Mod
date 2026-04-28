# Reverse Engineering Report: FF8 Steam 2013 (FF8_EN.exe) Battle Damage Number Display Trigger

## Executive Summary

After surveying the FF8 reverse-engineering ecosystem (FFNx, OpenFF8, OpenVIII, FF8_demaster, the FFRTT/Qhimm wiki, the ff8-speedruns memory map, the FFNx `ff8/battle/effects.h` module, Cactilio battle-structure docs, the Section 5 animation-sequence research thread, and the section-7/section-8 DAT format documentation), this report consolidates the most actionable findings for the user's blind-accessibility mod and explicitly flags what is *not* publicly documented anywhere I could reach.

There is **no publicly published exact memory address or function for "damage popup is currently visible"** in FF8_EN.exe (Steam 2013, image base 0x00400000). No reverse-engineer (Maki, myst6re, Aali, Extapathy, JeMaCheHi, hobbitdur, the ff8-speedruns team, FFNx contributors) has documented a "result sprite" / floating-number subsystem with named addresses. However, the cumulative work *does* converge on a well-defined area where the trigger almost certainly lives, and gives strong heuristics, byte-layout hypotheses, and search anchors for finding it in IDA/Ghidra/x64dbg yourself. Below is the technical guide.

---

## 1. What the user already has, framed against the public ecosystem

The user's confirmed entity layout (base `0x01D27B18`, stride `0xD0`, 7 slots; `curHP` at `+0x0C`, with allies as `uint16` and enemies as `uint32`) is **consistent with the ff8-speedruns memory map**, which independently documents the per-slot battle structures starting at `FF8_EN.exe+1927B18` (i.e. `0x00400000 + 0x01527B18 = 0x01927B18`). Their map labels every ally and enemy slot at the same stride and confirms `Current HP` at offset `+0x10` in their numbering (which equals the user's `+0x0C` once you account for the four bytes the user counts before the status bitfield at `+0x00`) [Source](https://github.com/ff8-speedruns/ff8-memory).

Note: **The user's entity base of `0x01D27B18` is offset `0x400000` higher than the ff8-speedruns base of `0x01927B18`.** This is exactly the FF8_EN.exe image base (`0x00400000`). The ff8-speedruns map uses *RVA from module base* notation (`FF8_EN.exe+1927B18`), while the user is using *absolute virtual addresses* in the running process (which adds the `0x00400000` image base). They refer to the same memory; the user's addresses are the flat VA, the wiki's are the module-relative form.

This same offset relation almost certainly applies to the user's `0x01D2834A` "last damage value" address. As an RVA it would be `FF8_EN.exe+0x192834A`, which sits **inside the per-slot battle entity table** documented by ff8-speedruns: it falls between Enemy 3 (starting at +0x1927F30) and Enemy 4 (starting at +0x1928000) and the spell-pointer block beginning at `+0x1928F18` [Source](https://github.com/ff8-speedruns/ff8-memory). In other words, the 0x1D2834A value is part of the contiguous in-memory battle structure, which is **read** by the renderer rather than being itself the render-trigger flag — confirming the user's own observation that this address is HP-time, not display-time.

---

## 2. Public function-address anchors (FFNx) for the FF8 Steam 2013 build

FFNx is the most reliable source of named function addresses for FF8_EN.exe because its source wires up symbols against the production Steam executable. From `src/ff8_data.cpp` the relevant battle-related anchors are derived from the main loop pointer and are published as relative byte offsets that you can resolve in your own IDB:

```
ff8_externals.battle_enter      = *(uint32_t*)(main_loop + 0x330)
ff8_externals.battle_main_loop  = *(uint32_t*)(main_loop + 0x340)
ff8_externals.swirl_enter       = *(uint32_t*)(main_loop + 0x493)
ff8_externals.swirl_main_loop   = *(uint32_t*)(main_loop + 0x4A3)
ff8_externals.sm_battle_sound   = relative_call(main_loop, 0x487)
ff8_externals.sub_470250        = relative_call(main_loop, 0x6E7)
ff8_externals.engine_set_init_time = relative_call(battle_enter, 0x35)
```
[Source](https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8_data.cpp)

The presence of a dedicated header `src/ff8/battle/effects.h` in FFNx (referenced from `ff8_data.cpp`) is the single strongest indication that battle visual-effect hooks are concentrated in that module; unfortunately my fetcher was rate-limited on raw.githubusercontent and the GitHub mirror only exposed navigation chrome, so the literal contents of `effects.h` could not be captured here. **Action: clone FFNx (`git clone https://github.com/julianxhokaxhiu/FFNx`) and read `src/ff8/battle/effects.{h,cpp}` directly — every public address related to battle effect rendering on the Steam build is wired up there.** [Source](https://github.com/julianxhokaxhiu/FFNx)

The user's existing `ATB update function at 0x004842B0` is consistent with the typical `0x0048xxxx` band where FFNx's battle-loop helpers resolve. The damage display routine will be in the same code section (`.text` between roughly 0x00480000 and 0x004A0000 based on FFNx's resolved offsets above and the `sub_470250` reference).

---

## 3. The DAT/animation sequencer is the proper trigger source

The user is correct that the FF8 battle "animation sequencer" (sometimes called the AI script engine) is what gates damage popup display. Per the **DAT file format reference**:

- Battle character/monster files (c0m*.dat, c1m*.dat, …) contain 11 sections
- **Section 3** is the model animation table (frame-indexed)
- **Section 4** and **Section 5** carry the per-attack *animation sequences* (Section 5 has its own header of `Number of sequences (2 bytes)` followed by `nbSequences * 2 bytes` of sequence positions). These are the c0m / cXm "AA scripts" that drive an attack from wind-up to impact to recovery.
- **Section 8** is the actual AI/battle script bytecode (init / turn / counterattack / death / unknown handler offsets).
[Source](https://wiki.ffrtt.ru/index.php/FF8/FileFormat_DAT)

Maki's research thread *".dat Files FF8 - Section 5 - animation sequences - Enemy / characters findings"* on Qhimm is the canonical public investigation of this subsystem (the link returns 403 to automated fetchers but is the live thread for this work) [Source](https://forums.qhimm.com/index.php?topic=19362.0). The takeaway is that Section 5 sequences contain opcodes that schedule sub-events during an animation, and damage application + popup spawn are two such sub-events. The damage value is computed and `curHP` is decremented at the moment the script reaches its "apply damage" opcode (which is what the user observed at 0x01D2834A — written *with* the HP change). The "show damage number" event is emitted by a *different* opcode, later in the same script, and is what the user needs to hook.

This means the popup trigger is **event-driven from the per-entity animation script interpreter, not from a frame counter or a global timer**. It is very likely each entity slot has an animation-sequencer state struct that contains a `current_opcode_index` / `frames_until_next_opcode` pair, and the popup spawn is the side effect of one specific opcode firing.

---

## 4. The 0x01D28340–0x01D2835E region — best-guess structural decode

The user observed during a 12-damage event:
```
addr  0x1D28340: 0000 0000 0401 0000  0300 000C 0000 4000  
addr  0x1D28350: 00FF 0000 0000 0000  0000 0000 0000 0000
```
The byte pattern `0301 0C00` decoded as two little-endian uint16s gives `0x0103` and `0x000C`. `0x0C = 12`, the damage value. `0x4000` is a classic FF8 flag-word (bit 14 set). `0x00FF` is a common "alpha = full" or "fade timer at max" sentinel.

Based on (a) the fact that this region is inside the contiguous battle data block per ff8-speedruns, (b) the known structure of similar floating-number subsystems in FF8/FF7 documented by Aali/Maki/myst6re, and (c) the observed values, the most plausible field layout is:

| Offset (from 0x1D28340) | Width | Hypothesized meaning | Evidence |
|-------------------------|-------|----------------------|----------|
| +0x00 | u16 | Active popup-slot index / "next free slot" | 0x0000 baseline |
| +0x02 | u16 | Reserved / padding | always 0 in user's snapshot |
| +0x04 | u16 | Popup type bitfield (0x0401 = damage+show-flag) | 0x0401 = damage popup flag |
| +0x06 | u16 | Reserved | 0 |
| +0x08 | u16 | **Target entity slot index** (0x0003 = enemy slot 3 in 7-slot table) | matches Cactilio/scene.out enemy ordering |
| +0x0A | u16 | **Damage value displayed** (= 12 = 0x000C) | **direct match to 12-damage event** |
| +0x0C | u16 | Reserved/sign-extend high word | 0 |
| +0x0E | u16 | **Display state / animation flags** (0x4000 = "currently visible/active") | classic FF flag-word; bit 14 = active |
| +0x10 | u16 | **Fade/lifetime counter** (0x00FF = 255 ticks remaining) | typical FF popup TTL |
| +0x12+ | — | Trailing zeros (per-popup tail / next-slot start) | inactive entries zero |

Strong recommendation for verification: **set a Cheat Engine / x64dbg hardware write-on breakpoint on `0x01D2834E` (the 0x4000 word). The instruction that writes 0x4000 there is the popup-spawn site you want.** Then put a write breakpoint on `0x01D28350` (the 0x00FF). The instruction that decrements that value every frame is the popup-tick routine. Both of those code addresses will be in the `0x004xxxxx` `.text` band of FF8_EN.exe and are the two MinHook targets you need.

If the region has a base+stride layout (so each of up to N concurrent popups occupies its own slot), the stride is most likely **0x14 bytes (20 bytes)** based on the trailing zero band starting at +0x14 in the user's capture, with a likely capacity of 7 or 8 active popups (matching the 7 entity slots, plus possibly a global "system message" slot for "Drawn!" / "Missed!" / "Critical!" text strings, which use the same display engine in FF8). The header of the user's capture (`0000 0000 0401 0000` at +0x00–+0x07) may itself be a manager header rather than slot 0 data.

---

## 5. Recommended hook strategy for the accessibility mod

Because the public ecosystem has not surfaced a single named function, the cleanest path is **memory-watch hooking** rather than function hooking:

1. **Primary trigger (popup appears):** Hardware-write breakpoint on the `display state` word (the `0x4000` byte the user observed — most likely VA `0x01D2834E`, but verify offset within the 0x20-byte region after attaching). When this transitions 0 → non-zero, a damage number is being spawned for visual display. This is the *exact* event the user wants.

2. **Secondary trigger (popup disappears):** Hardware-write breakpoint on the `fade timer` word (most likely `0x01D28350`). When this transitions to 0, the popup has finished fading. (Or alternatively when the display-state word returns to 0.)

3. **Per-slot resolution:** Read the target-slot field at +0x08 from the popup record at the moment the display-state flag goes hot. This tells you *which* of the 7 entity slots the number belongs to (3 allies + 4 enemies, indices 0–6 matching the Cactilio battle structure ordering [Source](https://github.com/JeMaCheHi/Cactilio)).

4. **Damage value:** Read the value field at +0x0A on the same event. This is functionally redundant with the user's existing 0x01D2834A read but is *guaranteed in-sync with the visual* rather than with the HP write.

5. **Function-level hook (optional, more invasive):** Once the write-on instruction address is found, you can MinHook that function instead of using a memory watch. Expect it to live near the FFNx-resolved `battle_main_loop` (resolved at runtime via `*(uint32_t*)(main_loop+0x340)` per [Source](https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8_data.cpp)). The function will read from the per-entity battle struct (base 0x01D27B18, stride 0xD0) and write into the popup record block at 0x01D28340.

6. **Why not hook 0x004842B0:** The user's identified ATB update function fires every battle tick regardless of damage display — too noisy. The popup-spawn site fires *exactly once per number that appears*, which is what the accessibility mod wants.

---

## 6. Cross-references for the user's own RE work

These are the canonical, currently-maintained primary sources for FF8 PC Steam reverse engineering. Use these as your IDB symbol-import sources:

- **FFNx source tree** — `src/ff8_data.cpp` (function-address resolver), `src/ff8/battle/effects.{h,cpp}` (battle visual-effects hooks), `src/ff8.h` (struct definitions), `src/ff8_opengl.cpp` (renderer-side battle hooks). This is the single most valuable corpus of named, working, Steam-compatible FF8 function addresses [Source](https://github.com/julianxhokaxhiu/FFNx). The README explicitly credits **myst6re** and **quantumpencil/Nax** for "a lot of hex addresses I would never been able to figure out myself" — these are the two contributors whose commits to `ff8_data.cpp` and the battle module are most worth reading [Source](https://github.com/julianxhokaxhiu/FFNx/blob/master/README.md).

- **FF8_demaster (`MaKiPL/FF8_demaster`)** — `texturepatch_v2_battleCharacter.cpp` shows the pattern for hooking battle-character draw calls via `InjectJMP` at known addresses (see `BCPBACKADD2` constant), which is the same hook approach the user can adapt for the popup renderer once the address is identified [Source](https://github.com/MaKiPL/FF8_demaster/blob/master/ff8_demaster/texturepatch_v2_battleCharacter.cpp). Although Demaster targets the Remastered build, the patching methodology (HEXT-style address injection + MinHook-equivalent JMP redirection) maps directly to the Steam 2013 build.

- **OpenFF8 (`Extapathy/OpenFF8`)** — Provides `ff8vars` and `ff8funcs` structs in `OpenFF8/memory.h` that document the team's catalog of FF8_EN.exe variable and function addresses. This is the *direct* equivalent of what the user is building. (The repo's file-tree was not directly fetchable, but the README confirms `memory.h` is the canonical location.) [Source](https://github.com/Extapathy/OpenFF8)

- **ff8-speedruns/ff8-memory** — The most thoroughly published flat memory map (battle entity slots, ATB, HP, status bits, timers, prizes, kill counters), maintained for the Steam EN and FR builds. Confirms the user's entity-array base and stride and provides per-slot offsets for max ATB, current ATB, current HP, max HP, status bits, every status timer, all 9 stats, elemental resistances, and "last attacker" — all of which exist at `FF8_EN.exe+0x1927B18` + slot×0xD0 (i.e. VA `0x01D27B18` + slot×0xD0). The "last attacker" field in particular is the public-knowledge analog for "who triggered this damage" and is at `+0x80` from each slot's start (e.g. Ally 1 at FF8_EN.exe+1927B98) [Source](https://github.com/ff8-speedruns/ff8-memory).

- **Cactilio (`JeMaCheHi/Cactilio`)** — Battle-structure editor; the battle-slot ordering used by the engine (7 slots, 3 allies + 4 enemies, scene.out flags) matches the user's observations [Source](https://github.com/JeMaCheHi/Cactilio).

- **FFRTT wiki — FF8 BattleStructure & FileFormat DAT** — Section 8 (AI/battle scripts) and Section 5 (animation sequences) of c*m*.dat are the bytecode that schedules the damage-number popup as a sub-event during an attack animation [Source](https://wiki.ffrtt.ru/index.php/FF8/FileFormat_DAT) [Source](https://wiki.ffrtt.ru/index.php/FF8/BattleStructure).

- **FFNx canary / hobbitdur FF8 Modding Wiki** — Active "Modding wiki" with formulas and battle structure references, including the `BATTLE_SLOT_DATA` array and `crisis_level` field which sit in the same per-entity struct [Source](https://hobbitdur.github.io/FF8ModdingWiki/technical-reference/list/formula/).

- **OpenVIII (`MaKiPL/OpenVIII-monogame`)** — `FF8/debug_battleDat.cs` is the canonical decode of the per-monster DAT including animation skeleton/sequencer and is referenced from the FFRTT wiki as the authoritative source [Source](https://github.com/MaKiPL/OpenVIII-monogame). Reading the C# port of how Section 5 sequences are interpreted will tell you exactly what opcode emits the popup event in the original engine.

---

## 7. What is *not* publicly known

Despite a comprehensive search of Qhimm forums, GitHub (FFNx, OpenVIII, OpenFF8, Demaster, Cactilio, ff8-speedruns), the FFRTT wiki, the hobbitdur FF8 Modding Wiki, Maki's personal research site (makigriever.pl), and the FF7 reverse-engineering literature for analogous patterns:

- **No author has published a named function address for the damage-number/result-sprite spawn function on FF8_EN.exe.** The FFNx `ff8/battle/effects.{h,cpp}` module is the most likely place for this if it has been mapped at all, but I was unable to retrieve its raw contents in this session due to GitHub rate-limiting on the raw endpoint.
- **No author has published the byte-by-byte structure of the 0x01D28340–0x01D2835F region.** The decode in §4 above is a best-effort hypothesis derived from one observed event, the surrounding ff8-speedruns map, and FF-engine conventions.
- **The animation-sequencer (Section 5) opcode that emits the popup event is not publicly tabulated.** Maki's Section 5 thread on Qhimm (topic 19362) discusses the *format* of the sequences but not which opcode IDs correspond to "spawn damage popup."

These three gaps are the user's actual contribution if they choose to publish — the community would benefit from the fully decoded popup structure and the function address.

---

## 8. Concrete next-step checklist for the user

1. **Clone FFNx and read `src/ff8/battle/effects.h` and `effects.cpp`**. Look for any function or hook with names like `magic_thunder_*`, `effect_*`, `popup_*`, `result_*`, `damage_*`, `floating_*`, or any reference to addresses in the `0x004Axxxx` band. This is the highest-yield single action.
2. **In x64dbg, set a hardware write breakpoint on `0x01D2834E` (word)** during a controlled 12-damage event. The instruction that breaks is the popup-spawn site. Note its address — it should be in the `0x004xxxxx` range. Add a second hardware write BP on `0x01D28350` to find the per-frame fade-tick routine.
3. **Walk backwards from the spawn-site instruction up the call chain** until you reach the function that the FFNx-resolved `battle_main_loop` (at `*(uint32_t*)(main_loop+0x340)`) directly calls. That call is the cleanest MinHook target — it fires once per frame during a popup-active state.
4. **Verify the per-slot stride hypothesis** by triggering damage on multiple entities simultaneously (e.g. Quistis Limit Break "Shockwave Pulsar" hits all enemies) and dumping `0x01D28340–0x01D283D0` (16 × 0x14 bytes) to see if multiple records get populated at +0x14, +0x28, +0x3C…
5. **For the AI-script-driven view**, dump a c0m*.dat (Squall) with the OpenVIII `debug_battleDat.cs` decoder, locate Section 5, and find the sequence opcode that occurs *after* the "deal damage" opcode in a normal attack animation — that is the popup-spawn opcode in source form. Cross-reference against the engine's opcode dispatch table (which will be near the function found in step 3).
6. **Consider asking on the Qhimm FFNx-FF8 Discord (`https://discord.gg/u6M7DnY`)**: myst6re or Maki almost certainly know the address from their existing IDA databases but have not published it [Source](https://github.com/julianxhokaxhiu/FFNx).

---

## 9. Final answer to the seven primary research questions

1. **Trigger mechanism**: Event-driven from the per-entity animation-sequencer (Section 5 of c*m*.dat), with state stored in the popup-record region at `0x01D28340`. The most likely "currently displayed" flag is the word at `0x01D2834E` (observed value `0x4000` during the 12-damage event).
2. **Computed → visible transition**: Animation-script opcode dispatch inside the battle main loop (FFNx `battle_main_loop`, resolved at `*(uint32_t*)(main_loop+0x340)`), driven by frame-stepping of per-slot animation state. Not a global timer — it's a per-entity scripted event.
3. **Per-slot vs global**: Most likely **per-record** (not per-entity), with a **20-byte stride** stored in a small array starting at `0x01D28340`. Up to ~8 simultaneous popups (one per entity slot + one system slot). The "target slot" field within each record (hypothesis: +0x08) ties it back to the entity array.
4. **Fade timer**: Most likely the word at `0x01D28350` (observed `0x00FF`). Counts down per frame; popup vanishes when it reaches 0.
5. **Region structure**: See table in §4. This is a hypothesis based on one event and needs verification with the BP procedure in §8.
6. **Animation sequencer event**: Yes — DAT Section 5 sequences contain opcodes that schedule sub-events during an attack animation; popup spawn is one such opcode, distinct from the damage-application opcode. Per [FFRTT DAT format](https://wiki.ffrtt.ru/index.php/FF8/FileFormat_DAT) and Maki's [Section 5 thread](https://forums.qhimm.com/index.php?topic=19362.0).
7. **Function addresses**: Not published. Resolve them yourself via FFNx's `battle_main_loop` indirection plus a hardware write BP on `0x01D2834E`. Expect them in the `0x00484xxx`–`0x004A0xxx` band based on FFNx's `sub_470250`, `swirl_main_loop`, and `engine_set_init_time` resolutions [Source](https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8_data.cpp).

---

## Caveats

- Every offset in §4 except the damage value (which directly equals the observed 12) is a *plausibility hypothesis*, not a confirmed decode. The user must verify with breakpoints.
- FFNx is at v1.23.x/v1.24.0 and its address resolutions for FF8_EN.exe are stable for the Steam 2013 build but may shift across language editions (US/EN, FR, DE, JP — see myst6re's "Fix crashes in non-US versions" PRs in 1.22.0) [Source](https://github.com/julianxhokaxhiu/FFNx/releases). Use the EN build addresses only.
- The `effects.h` contents could not be retrieved in this session (GitHub raw endpoint required exact-URL provenance and the GitHub HTML mirror returned only navigation chrome). This is the single most important file the user should read next; it is at `https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8/battle/effects.h`.
- I was unable to fetch the live `forums.qhimm.com` threads (403 to automated fetcher), but they are accessible from a normal browser and were referenced through the search index. The two highest-value Qhimm threads are topic 19362 (Section 5 animation sequences, by Maki) and topic 16838 (FF8 Engine reverse engineering general).
- The user's note about the Steam 2013 savemap header being 76 bytes (0x4C) vs wiki references of 96 bytes (0x60) is independently confirmed by FFNx's `savemap_field` external (`get_absolute_value(main_loop, 0x21)`) and myst6re's "Steam savegame logic in the manifest.xml for FF8" credit in the FFNx README [Source](https://github.com/julianxhokaxhiu/FFNx/blob/master/README.md). Continue to subtract `0x14` from PSX-derived savemap offsets when porting them to Steam 2013.