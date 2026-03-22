# NEXT SESSION PROMPT
## FF8 Accessibility Mod — Immediate Context
## Updated: 2026-03-22 (end of session 2)

---

## What Happened This Session (v0.08.82–v0.08.87)

### Battle Arrangement TTS — FIXED (v0.08.82–v0.08.85)
- Neither `battle_order[32]` nor raw inventory matched what the battle arrangement screen shows
- **Display struct at `0x1D8DFF4`** is the authoritative source: `{item_id, qty}` pairs, 2 bytes/slot, updates live during swaps
- v0.08.85: Cleaned up ~300 lines of dead working buffer code

### Party Member HP/Status Announcement — IMPLEMENTED (v0.08.86–v0.08.87)
- **Computed stats at `0x1CFF000`**: FFNx `char_comp_stats_1CFF000`, 3 entries (one per active party slot)
- Struct: `ff8_char_computed_stats`, stride `0x1D0` (464 bytes), curHP at +0x172, maxHP at +0x174
- Indexed by **party formation slot** (0-2), NOT character index — map via savemap +0xAF0 formation array
- Confirmed: maxHP reads correctly (486/486 for Squall, 501/501 for Quistis)
- `FormatPartyMemberAnnouncement()` builds: "Name, HP X of Y" + status ailments if present
- Status ailment decoding from savemap char +0x96 bitfield (KO, Poison, Petrify, Blind, Silence, Berserk, Zombie)
- v0.08.87: Diagnostic removed, clean production code

---

## Current Build State
- **Source**: v0.08.87
- **Deployed**: v0.08.87
- **Battle TTS**: Display struct at 0x1D8DFF4 ✓
- **Use target TTS**: compStats HP/maxHP ✓, status ailments ✓

---

## Next Priorities

### 1. Item Use — Remaining Testing (BLOCKED until more game progress)
- Test using Potion on damaged character → verify HP updates in real time
- Test using items on GFs (G-Potion etc.) → need GF item target detection
- Test miscellaneous items (Magic Lamp, compatibility items) → need different focus states
- Test Tent/Cottage (heal all party) → may use different target flow
- HP change announcement after item use (before/after comparison)

### 2. Other Submenu TTS
- Magic, GF, Ability submenus

### 3. Save Game Flow TTS / Title Screen Continue

### 4. PSHM_W Option F — awaiting deep research

---

## Key Architecture Reference

### Computed Stats (v0.08.87)
- **Address**: `0x1CFF000`
- **Stride**: `0x1D0` (464 bytes per entry, 3 entries for active party)
- **curHP**: +0x172 (uint16)
- **maxHP**: +0x174 (uint16)
- **Indexed by**: party formation slot (0-2), map via savemap +0xAF0
- **Source**: FFNx `char_comp_stats_1CFF000`, `compute_char_stats_sub_495960`

### Battle Display Struct (v0.08.85)
- **Address**: `0x1D8DFF4`
- **Format**: `{item_id, quantity}` × 32 slots (2 bytes each)
- **Empty**: qty == 0
- **Live updates**: Engine modifies in place during swaps

### Status Ailments (savemap char +0x96)
- Bit 0: KO, Bit 1: Poison, Bit 2: Petrify, Bit 3: Blind
- Bit 4: Silence, Bit 5: Berserk, Bit 6: Zombie, Bit 7: unused

### Item Submenu Offsets (pMenuStateA)
| Offset | Purpose | Values |
|--------|---------|--------|
| +0x22E | Active focus indicator | 3=action, 5=items, 14=use target, ~97=rearrange, ~30=battle, 79=sort, 36=battle dest, 99=rearrange dest |
| +0x27F | Action cursor | 0=Use, 1=Rearrange, 2=Sort, 3=Battle |
| +0x272 | Item list cursor / rearrange source | 0-based |
| +0x276 | Party target / rearrange dest cursor | Reused |
| +0x285 | Battle source cursor | 0-based, indexes into display struct |
| +0x286 | Battle destination cursor | 0-based |

### Savemap
- Base: `0x1CFDC5C`
- Item inventory: +0x0B40 (198×2 bytes)
- Party formation: +0xAF0 (4 bytes)
- Live game time: +0x0CCC
- Characters: +0x48C (8 × 0x98 bytes)
- Character status: char struct +0x96 (bitfield)

---

## Files Modified This Session
- `src/menu_tts.cpp` — v0.08.82-87: Battle TTS fix, cleanup, party HP/status announcements
- `src/ff8_accessibility.h` — version bumps through v0.08.87
- `src/field_navigation.cpp` — version comment bumps
- `DEVNOTES.md` — updated
- `NEXT_SESSION_PROMPT.md` — updated

---

## Recovery Instructions
1. Read DEVNOTES.md for architecture and key learnings
2. Read this file for immediate context
3. **Use filesystem MCP tools** — mod files on Windows, bash is separate Linux container
4. `deploy.bat` is the ONLY build script
5. Bump `FF8OPC_VERSION` in 3 locations every build
6. "BAT" → read tail of `Logs/ff8_accessibility.log`
7. Build error → read `Logs/build_latest.log`
8. Current: v0.08.87 (source+deployed)
9. GitHub: ampage87/FFVIII-Accessibility-Mod (main branch)
10. **SAVEMAP CORRECTION**: All deep research offsets need -0x14 (header is 0x4C not 0x60)
