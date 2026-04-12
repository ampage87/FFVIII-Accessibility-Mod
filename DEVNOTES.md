# DEVNOTES — FF8 Accessibility Mod

## Current Build: v0.13.45

### Source File Split — COMPLETE (session 63)

battle_tts.cpp (~2800 lines / 100KB+) split into 7 .inl files + core framework:

**Include order (complete):**
```
battle_tts_helpers.inl       // enemy names, DecodeFF8String
battle_tts_diagnostics.inl   // menu diagnostic, cursor hunter, enemy cache
battle_tts_hp.inl            // HP tracking, damage, target selection
battle_tts_ewm.inl           // EWM, GF fire prevention, ATB hook
battle_tts_menu.inl          // turn/command menu, sub-menus
battle_tts_screenshot.inl    // GL screenshot capture, memory diff, victory step diagnostics
battle_tts_victory.inl       // Victory TTS: 8 hooks, phase detection, GF/ability tables, thread
```

**Resulting sizes:**
- battle_tts.cpp: ~26KB (core framework, entity helpers, enter/exit, init/update/shutdown)
- battle_tts_screenshot.inl: ~19KB (SwapBuffers hook, DiffMemorySnapshots, DumpVictoryStep, PollVictoryScreen)
- battle_tts_victory.inl: ~67KB (all 8 BT hooks, VictoryPhase state machine, GF fallback/ability tables, VictoryScreenThreadFunc)

### Victory Screen TTS — COMPLETE (sessions 53-62)

All victory phases announced via TTS, verified clean across 6+ consecutive battles:
- EXP Phase 1+2 (with character level-ups)
- Items (quantity from drop list + description with "Description:" prefix)
- GF AP
- GF Level-Up (multiple per battle, deep hook name-change detection)
- Ability Learned (correct GF + ability names from deep rendering hooks)

### Deep Rendering Hooks (v0.13.40-v0.13.43)

- **sub_47E970** (BT7): GF name retrieval. Takes encoded GF index (0x40=Quezacotl, etc.). Hooked → `s_renderedGFName`.
- **sub_47E710** (BT8): Ability name retrieval. Custom inline decoder handles shifted digits (0x21-0x2A), +=0x31, %=0x2B.
- Item drop list at `[0x01A78C88 + 0x50]`: 4-byte entries, word[0]=item, byte[2]=qty.

### Battle Text Hooks (8/8 installed)

| Hook | Address    | Function   | Purpose |
|------|-----------|------------|---------|
| BT1  | 0x0047EC70 | sub_47EC70 | Battle text by ID (BTXT) |
| BT2  | 0x004B7210 | sub_4B7210 | GPU glyph quad draw |
| BT3  | 0x004A3EE0 | sub_4A3EE0 | Victory per-frame loop |
| BT4  | 0x005348E0 | sub_5348E0 | Variable expansion |
| BT5  | 0x0047EA30 | sub_47EA30 | Entity name (items) |
| BT6  | 0x0047EA90 | sub_47EA90 | Sibling name (descriptions) |
| BT7  | 0x0047E970 | sub_47E970 | GF name (victory rendering) |
| BT8  | 0x0047E710 | sub_47E710 | Ability name (victory rendering) |

---

## Known Open Issues

- INF gateway destination direction accuracy
- bghall_1 catalog regression (noted session 48)
- Interaction zone naming refinements
- Submenu mode 0x00 ambiguity

## Key Technical Notes

- SAVEMAP OFFSET CORRECTION: Header is 76 bytes (0x4C), not 96 (0x60). Subtract 0x14 from community offsets.
- NEVER re-enable SET3 opcode hook (0x1E): hangs infirmary scene. CI guard active.
- Ability names use kernel.bin encoding: digits 0x21-0x2A, '+'=0x31, '%'=0x2B, '-'=0x2F, '.'=0x2E, '='=0x30.
- GF names via sub_47E970 use standard FF8 entity encoding (no +0x20 offset).
- textID=121 "GF " fires EVERY FRAME during level-up rendering (not once per screen).
- Multiple GF level-ups use name-change detection via s_lastAnnouncedVictoryGFName.
- Post-ability level-ups caught by separate handler (phase state is monotonic).
