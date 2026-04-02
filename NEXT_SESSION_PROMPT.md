# NEXT SESSION PROMPT — FF8 Accessibility Mod

## Current build: v0.10.105 (source + deployed)

## What just happened (session 29)
**Item sub-menu TTS: FULLY WORKING with page/slot navigation.**

After extensive diagnostic investigation across sessions 27-29:
1. F12 comprehensive diagnostic dumped 8 data sources — identified display struct at 0x1D8DFF4 as field-menu-only cache.
2. Deep research confirmed three-layer architecture: savemap (persistent), field menu cache (transient), battle working buffer (ephemeral).
3. Multi-source diagnostic (4 sources A/B/C/D per cursor change) identified the correct mapping:
   - **Display struct mode** (after visiting Items > Battle): cursor indexes into 0x1D8DFF4. Used exclusively — qty=0 entries are valid empties.
   - **Direct inventory mode** (display struct zeroed): cursor byte 0x01D768EC directly indexes into inventory at 0x1CFE79C.
4. Page/slot announcements added: "Name, quantity N, page P, item I" with 4 items per page, matching field menu format.
5. Tested both scenarios (normal + rearranged) — correct in both.

## Immediate next priorities
1. **Draw sub-menu TTS** — Most complex sub-menu (multi-phase: draw list, stock, cast). Next battle TTS feature.
2. **Diagnostic cleanup** — Remove [ITEM-MULTI] verbose logging from item sub-menu (keep dual-source announce logic). Remove/repurpose F12 diagnostic.
3. **Scan spell info TTS** — Lower complexity than Draw.
4. **Push ~50+ unpushed builds to GitHub** (v0.09.41–v0.10.105)
5. **World map navigation** (longer-term, deep research pending)

## Key addresses for Draw sub-menu investigation
- Sub-menu cursor: `0x01D768EC` (same byte used by Magic/GF/Item sub-menus)
- Command ID for Draw: likely `0x14` or similar (check char command slots)
- Savemap magic slots: char+0x10 (32 slots × 2 bytes: magic_id, qty)
- The Draw sub-menu is multi-phase: first shows drawable spells from enemy, then allows Draw/Stock/Cast selection

## What to do at session start
1. Read DEVNOTES.md and this file (MANDATORY)
2. Read DEVNOTES_HISTORY.md only if tracing past decisions
3. Start with Draw sub-menu investigation — begin with diagnostic to discover cursor behavior and data sources
