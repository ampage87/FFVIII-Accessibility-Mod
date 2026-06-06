# Scan spell research — open questions

This is a deep-research prompt for ChatGPT (or another agent with web + reverse-engineering tooling). The mod project's NEXT_SESSION_PROMPT.md has the broader context; this file is the question set.

---

## Background

I'm working with FF8 Steam 2013 edition (FF8_EN.exe, no ASLR). I'm building an accessibility mod that announces battle information via TTS for blind players. I want to wire up the Scan spell (battle effect ID 39, decimal — confirmed from the FFNx canary source's `FF8BattleEffect::Scan = 39` enum).

The runtime battle entity array is at `0x01D27B18`, stride `0xD0`, slot 0..2 = allies, slot 3..6 = enemies. Several offsets within each entity struct are already known and BAT-validated (HP TTS works in shipped builds):

- `0x10` cur HP (u16 ally / u32 enemy)
- `0x14` max HP (u16 ally / u32 enemy)
- `0x3C` elemental resistance, 8 × u16 (Fire / Ice / Thunder / Earth / Poison / Wind / Water / Holy)
- `0x78` persistent status bitfield (KO / Poison / Petrify / Blind / Silence / Berserk / Zombie / ...)
- `0xB3` monster_id byte (used by `scan_get_text_sub_B687C0` to index `scan_text_positions = 0x01887474` (u16 array) and `scan_text_data = 0x018875B4`). This is confirmed by reading the disasm directly:

```
0x00B687C0:  mov  eax, dword ptr [esp + 4]    ; slot index 0..6
0x00B687C4:  and  eax, 0xff
0x00B687C9:  lea  ecx, [eax + eax*2]          ; ecx = slot * 3
0x00B687CC:  lea  edx, [eax + ecx*4]          ; edx = slot * 13
0x00B687D1:  shl  edx, 4                      ; edx = slot * 0xD0  (matches BATTLE_ENTITY_STRIDE)
0x00B687D6:  mov  al,  byte ptr [edx + 0x1d27bcb]   ; monster_id at entity_base + 0xB3
0x00B687DC:  mov  cx,  word ptr [eax*2 + 0x1887474] ; scan_text_positions[monster_id]
0x00B687E4:  mov  eax, ecx
0x00B687E6:  add  eax, 0x18875b4               ; pointer into scan_text_data
0x00B687EB:  ret
```

- `0xB4` level (u8, documented, not yet exercised)
- `0xB5` STR (u8, documented, not yet exercised)

---

## What I still need

### Question 1: Status resistance offset within the entity struct

The .dat file format (per the Final Fantasy Inside wiki, Section 7 offset 360) has 20 bytes of status resistance in this order:

> Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Haste, Slow, Stop, Regen, Reflect, Doom, Slow Petrify, Float, Confuse, Drain, Expulsion, ???

What runtime offset within the 0xD0 entity struct holds these? My estimate is somewhere in `0x4C..0x77` (a 44-byte gap between the end of elem-resist at 0x4C and persistent-status at 0x78).

Please confirm the exact offset and the encoding (raw 0..100? signed? threshold for "Strong vs <list>"?).

How to find it: trace the Scan render code that displays "Strong vs <list>". Dispatch comes from `func_off_battle_effects_C81774[FF8BattleEffect::Scan = 39]` and progresses through these subs (per FFNx ff8_data.cpp's external resolution):

```
sub_84D110 -> sub_84D1F0 -> sub_84D230 -> sub_84D2C0 -> sub_84D4B0 -> sub_84F2A0 -> sub_84F860 -> sub_84F8D0
```

`sub_84D4B0` is a 9-state phase-machine ([esi + 0x29] is the phase byte), and `sub_84F2A0`/`sub_84F860` are sibling phases that draw the stat/element/status content. Somewhere in that chain (or a sibling render function dispatched from `[esp + 8/0xc/0x10/...]` tables in those phase functions — see `0x84D4B0` body) is the per-status loop that reads from `entity_base + ?` and decides if a status name should be added to the displayed list.

### Question 2: Elemental resistance encoding at offset 0x3C

The entity struct has 8 × u16 there. What value range maps to "Absorbs" vs "No Effect" vs "Weak to" vs "Halves" vs "Normal"?

FF8 community docs suggest signed bytes per element in the .dat file format with positive = resist/absorb and negative = weak. Confirm the runtime encoding (it's u16 in the entity struct, not u8 — may be sign-extended in the conversion from .dat to runtime) and the threshold the Scan UI applies to bucket each element into the four named categories (or three, if Halves is folded into Normal for display).

### Question 3: Hidden-HP whitelist mechanism

The Final Fantasy wiki lists certain enemies whose HP displays as `?????` regardless of value: Fastitocalon-F, Fastitocalon, Adel, Sorceress A/B/C, Griever, Ultimecia (Griever form), Helix, Ultimecia (final boss).

Is this gated by:

- (a) monster_id lookup against a small hard-coded table in the exe?
- (b) a flag bit in the entity struct (somewhere in 0x4C..0x77 perhaps)?
- (c) max HP > 99,999 alone (which would explain the wiki line about "if HP exceeds 99,999")?
- (d) some combination?

Find the comparison in the disasm — most likely inside the Scan HP-render function in the chain above. If it's a hard-coded ID table, list the IDs.

---

## Cross-reference

FFNx canary source ships at `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/`. Useful files:

- `ff8.h` — struct documentation; `scan_get_text_sub_B687C0`, `battle_entities_1D27BCB`, `scan_text_positions`, `scan_text_data` are all already named in the `ff8_externals` block at the bottom of the file.
- `ff8_data.cpp` — sub_84F8D0 trace and Scan effect dispatch resolution.
- `ff8/battle/effects.h` — `FF8BattleEffect::Scan = 39`.

Disassembly for the Steam exe is at `Game Files/disassembly/FF8_EN_.text_0x*.asm`. Address ranges:

- `0x00401000-0x00501000` -> FF8_EN_.text_0x00401000.asm
- `0x00501000-0x00601000` -> FF8_EN_.text_0x00501000.asm
- ... and so on through `0x00B01000-0x00B69000`.

Note about the savemap-offset correction in our DEVNOTES (header is 76 bytes, not 96 — subtract 0x14 from any offset that assumes the longer header): this does NOT apply here. This is the battle entity struct, not the savemap.

---

## Deliverable

Confirmed offsets and encoding for the three open questions above, plus enough disasm context (function name + address + the specific instruction that makes the comparison) so I can verify each finding live with a memory inspection during a Scan cast in the running game.
