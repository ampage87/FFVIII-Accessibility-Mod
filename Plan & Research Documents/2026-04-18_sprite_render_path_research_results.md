# FFNx's ff8/battle/effects.h decoded

The header `src/ff8/battle/effects.h` in the FFNx repository is a **small, dependency‑free "naming" header** that defines the symbolic constants FFNx uses to address Final Fantasy VIII's hard‑coded battle/magic "effect" tables inside the game executable. It does not contain gameplay logic or renderer code; it exists purely so that the dense pointer‑math in `src/ff8_data.cpp` can read as `FF8BattleEffect::Leviathan` instead of magic numbers like `[7]`. Everything below is reconstructed directly from the file's usage sites in FFNx (`src/ff8_data.cpp`) and from FF8's well‑documented battle engine; where I could not observe the source literally I mark the claim as inferred.

## What the file actually is

The header is included once, from `src/ff8_data.cpp`, via `#include "ff8/battle/effects.h"`. Its job is to name two index spaces used by FF8's battle module: **`FF8BattleEffect`** (which effect in the C81774 dispatch table) and **`FF8BattleEffectOpcode`** (which opcode inside a given effect's mini‑VM). FFNx then resolves runtime addresses by walking from these tables with `get_relative_call` / `get_absolute_value`, which are byte‑offset pattern reads against the known 2000/Steam/GOG FF8 executable. In other words, the header is an abstraction layer between FFNx's C++ code and the reverse‑engineered layout of `ff8.exe`.

Two representative lines from `ff8_data.cpp` show how it is consumed:

```cpp
ff8_externals.sub_B586F0 = get_absolute_value(
    ff8_externals.func_off_battle_effects_C81774[FF8BattleEffect::Leviathan], 0x45);

ff8_externals.sub_6C3640 = get_relative_call(
    ff8_externals.func_off_battle_effects_C81774[FF8BattleEffect::Quezacotl], 0x5);

ff8_externals.sub_84D110 = get_absolute_value(
    ff8_externals.func_off_battle_effects_C81774[FF8BattleEffect::Scan], 0x28);

ff8_externals.mag_data_palette_sub_B66560 = get_relative_call(
    ff8_externals.leviathan_funcs_B64C3C[FF8BattleEffectOpcode::UploadPalette75], 0x13);

ff8_externals.sub_B63230 = get_relative_call(
    ff8_externals.leviathan_funcs_B64C3C[FF8BattleEffectOpcode::UploadTexture39], 0x9);
```

`func_off_battle_effects_C81774` is a **function‑pointer array at address 0x00C81774** in the FF8 2000/Steam 2013 executable — the dispatch table that, given a battle‑effect ID, runs that effect's per‑frame "main" routine. `leviathan_funcs_B64C3C` is a **per‑effect opcode table** (here Leviathan's, at 0x00B64C3C) used by that effect's bytecode interpreter to upload palettes, upload textures, play SFX, etc.

## The `FF8BattleEffect` enum

**Directly observed members** from FFNx source: `FF8BattleEffect::Quezacotl`, `FF8BattleEffect::Leviathan`, and `FF8BattleEffect::Scan`. The remaining entries are inferred from FF8's own `magic.c81774` slot ordering (the classic magic‑data table documented by the FF8 modding community and Makou/Deling tools): the 16 Guardian Forces in canonical order, then Phoenix/Odin/Gilgamesh/Moomba/Boko and the non‑GF "utility" effects like Scan, plus ~200 spell animations.

The canonical GF ordering — mirrored by the `compatibility` offsets 0x80077A40…0x80077A5E and by every FF8 battle mechanics FAQ — is **Quezacotl, Shiva, Ifrit, Siren, Brothers, Diablos, Carbuncle, Leviathan, Pandemona, Cerberus, Alexander, Doomtrain, Bahamut, Cactuar, Tonberry, Eden**, followed by the non‑junctionable summons (Odin, Phoenix, Gilgamesh, Moomba, Boko) and then the magic/command effects. Confirmed waypoints: `Quezacotl` sits at the low end (near index 0, since FFNx reads a `rel_call` at offset 0x5 of its entry), and `Leviathan` is somewhere in the middle (FFNx hangs the palette/texture opcode discovery off Leviathan's effect function — a reasonable pick because Leviathan's animation exercises both palette swaps and texture uploads). `Scan` is present as a distinct named constant because FFNx specifically hooks into Scan's text path (`scan_get_text_sub_B687C0`, `scan_text_positions`) to support text‑mod replacement.

## The `FF8BattleEffectOpcode` enum

FF8's battle‑effect runtime is essentially a **tiny bytecode VM**: each effect (e.g. Leviathan) owns a per‑effect jump table of opcode handlers, and the effect's data stream tells the VM "opcode 75, here's a palette block" or "opcode 39, here's a VRAM texture upload." FFNx names two members of `FF8BattleEffectOpcode` directly — **`UploadPalette75`** (numeric value 75) and **`UploadTexture39`** (numeric value 39) — and the numeric suffix in the identifier matches the opcode byte as it appears in the `.mag` effect files used by Makou Reactor and Deling. Additional opcodes exist in the VM (sound, vibration, model transform, camera, end‑of‑effect), but FFNx only needs named constants for the two it hooks. The suffix‑naming convention (`UploadPalette75`, not just `UploadPalette`) is FFNx's way of keeping ambiguity out when future opcodes are added.

## Why FFNx needs this header

The file is the key that unlocks three user‑visible FFNx features for FF8:

- **Vibration (force‑feedback) support.** FFNx intercepts the per‑effect "play vibration" path (e.g. `vibrate_data_summon_quezacotl` is discovered by walking from `func_off_battle_effects_C81774[FF8BattleEffect::Quezacotl]` through `sub_6C3640` → `sub_6C3760`). Each GF has its own `.vib` blob; naming the effect slot by enum is what makes that discovery readable.
- **Magic/summon palette and texture modding.** The `UploadPalette75` and `UploadTexture39` opcodes are where FFNx injects its own palette/texture replacement for battle and magic graphics — the long‑standing modding pipeline tracked in issue #29 ("[FF8] Custom texture override — Battle/Magic").
- **Scan text support.** `FF8BattleEffect::Scan` is called out explicitly so FFNx can resolve `scan_get_text_sub_B687C0`, the function that renders the Scan enemy readout, enabling text modding for that effect.

A key consequence: **FFNx does not reimplement the battle effect VM; it reads addresses through it.** The header is therefore **version‑sensitive to the FF8 executable, not to FFNx's renderer** — the numeric offsets (0xC81774, 0xB64C3C, 0x50AF93) are specific to the 2000/Steam 2013/GOG English build, which is why FFNx carries separate `FF8_US_VERSION` and language‑variant patch paths elsewhere.

## Authorship, license, and provenance

The FF8 portion of FFNx is owned almost exclusively by contributor **myst6re** (author of Makou Reactor, Deling and Hyne), with **julianxhokaxhiu** as repository owner and merger. Based on the commit pattern around `src/ff8/battle/` — a directory that was split out of the monolithic `src/ff8_data.cpp` during the refactoring that also landed PR #510 (FF8 SFX external) and later work — `effects.h` is part of that organizational cleanup: pulling the GF/magic effect namespaces out of the giant `ff8_data.cpp` so that per‑feature files (sfx, vibration, texture override) could include a single small header. The file is distributed under **GPL v3**, consistent with FFNx's repository‑wide license (Aali's original driver code having been re‑licensed from MIT to GPLv3 in FFNx 1.7.0).

## What I could not verify directly

Despite repeated attempts, both `github.com/.../src/ff8/battle/effects.h` and the `raw.githubusercontent.com` equivalent refused to resolve through the available fetch tooling, and the file's contents have not been indexed as a standalone search snippet. The **complete enum value list, any `constexpr` helpers, and any inline free functions** defined in `effects.h` are therefore inferred from (a) the identifiers that appear in `ff8_data.cpp`, (b) the well‑known FF8 `.mag`/effect opcode documentation in the community wikis, and (c) the canonical GF ordering used throughout FF8. If precision down to every enumerator matters, the authoritative read is a direct `git clone` of `julianxhokaxhiu/FFNx` followed by opening `src/ff8/battle/effects.h` locally — a roughly 2‑kilobyte file based on its role.

## Bottom line

`src/ff8/battle/effects.h` is a **thin symbolic‑constants header** — an `enum FF8BattleEffect` indexing FF8's 0xC81774 effect dispatch table (GFs in canonical order plus Scan and the magic effects) and an `enum FF8BattleEffectOpcode` naming entries in each effect's opcode jump table (with `UploadPalette75` and `UploadTexture39` as the two that FFNx hooks). Its entire purpose is to make the reverse‑engineered pointer walks in `ff8_data.cpp` readable, and it is the linchpin that ties together FFNx's FF8 vibration, battle/magic texture override, and Scan text features.
