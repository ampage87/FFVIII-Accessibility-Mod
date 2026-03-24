# NEXT SESSION PROMPT — FF8 Accessibility Mod
## Updated: 2026-03-24 end of session 11
## Current build: v0.09.49 (source + deployed)

---

## IMMEDIATE PRIORITY: Battle Sequence TTS

The Junction screen is usable for the current game stage. Next focus is making battles accessible. This will require deep research into FF8's battle mode memory layout, as no prior work has been done on battle TTS.

### What needs TTS in battle
1. **Enemy identification** — what enemies are present, how many
2. **Turn/ATB indicators** — whose turn it is, when a character's ATB bar is full
3. **HP tracking** — party member HP during battle
4. **Command menu navigation** — Attack/Magic/GF/Draw/Item and sub-menus
5. **Battle results** — EXP, AP, items gained, level ups
6. **Draw system** — what magic is available to draw from enemies

### What we know so far
- Battle mode = game mode 15 (`FF8_MODE_BATTLE` in FFNx)
- FFNx source has `src/ff8/battle/` directory — check for struct definitions, addresses
- Savemap character structs are at known addresses but battle uses separate computed stat structs
- `0x1CFF000` has computed stats (3 party slots × 0x1D0) — may be relevant during battle

### Recommended first step
Submit a ChatGPT deep research prompt for FF8 battle mode memory layout — enemy data addresses, ATB state, command menu state, battle result screen data. Include the savemap offset correction note.

---

## DEFERRED: Junction Menu Follow-ups

These items are blocked on game progress (need magic stocked, more GF abilities learned). Circle back after battle TTS is working and player has progressed further:

1. **Party & Character Abilities** — currently only command abilities are available to junction. Once GFs learn passive abilities (HP+20%, Cover, etc.), test that the right panel correctly shows them when cursor is on ability slots (3-6). The code already handles this range (IDs 39-82) — just needs verification.

2. **Auto action menu** — can't be accessed without magic stocked. Needs focus state discovery for the Auto sub-options (Atk/Mag/Def). Low complexity expected.

3. **Manual magic-to-stat assignment** — selecting individual magic spells to junction to stats (Str, Vit, Mag, etc.). Expected to be complex: stat list navigation, magic list for each stat, preview of stat changes. Deferred as lowest priority since Auto junction is a functional workaround.

---

## OTHER PRIORITIES (after battle)

- Top-level menu navigation TTS
- Save Game flow TTS
- Save Point entity catalog integration
- Title Screen Continue TTS
- SFX volume control (GitHub Issue #8)

---

## HOUSEKEEPING

- GitHub push needed (v0.09.41 through v0.09.49, 9 builds unpushed)
- DEVNOTES.md and NEXT_SESSION_PROMPT.md current as of this session

---

## VERSION BUMP LOCATIONS (3 required per build)
1. `FF8OPC_VERSION` in `src/ff8_accessibility.h`
2. Version comment near top of `src/field_navigation.cpp` (~line 5)
3. Init log string inside `src/field_navigation.cpp` Initialize() (~line 4683)

## KEY ADDRESSES
- Savemap base: `0x1CFDC5C`
- Characters: savemap+0x48C (8×0x98)
- GFs: savemap+0x4C (16×0x44), completeAbilities at GF+0x14 (16 bytes)
- Formation: savemap+0xAF0 (4 bytes) — WARNING: rewritten during Junction editing
- pMenuStateA: runtime, resolved by ff8_addresses.cpp
- Computed stats: `0x1CFF000` (3×0x1D0, curHP+0x172, maxHP+0x174)

## ABILITY SCREEN ARCHITECTURE (confirmed v0.09.49)
- LEFT panel: focus=24, cursor at pMenuStateA+0x27C (0=cmd0, 1=cmd1, 2=cmd2, 3-6=ability slots)
- RIGHT panel: focus=28, cursor at pMenuStateA+0x271 (index into reconstructed list)
- Focus 21,22,26,27 = transitions (ignore)
- GF bitmasks cached in `s_juncCachedGfMasks[8]` at Junction activation
- Selected charIdx cached in `s_juncSelectedCharIdx` at char select (formation gets rewritten)
- Right panel list built by `BuildAbilityRightPanel()`, stored in `s_abilRightList[]`
