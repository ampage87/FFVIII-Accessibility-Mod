# NEXT SESSION PROMPT
## FF8 Accessibility Mod — Immediate Context
## Updated: 2026-03-21

---

## What Just Happened (v0.08.51–v0.08.61)

### Item Submenu TTS — Focus State Discovery

**Problem**: Announcing the selected action option (Use/Rearrange/Sort/Battle) when user Cancels from Items List back to Action Menu. Multiple approaches failed:
- +0x5DF sub-phase (3→2) — unreliable, sometimes fires, sometimes doesn't
- +0x230 phase flag — doesn't update on Cancel from items to action
- GCW string matching ("Use Item", etc.) — worked but not scalable

**Solution (v0.08.59–61): +0x22E focus state indicator**
- Discovered via round-trip snapshot diagnostic: take 4KB snapshot in items list, use GCW as trigger to diff on Cancel, then diff again on re-entry
- **+0x22E values: 3=action menu has focus, 5=items list has focus**
- Transitions through intermediates: 5→2→3 (items→action), 3→4→5 (action→items)
- Code watches for arrival at stable endpoints (3 or 5), ignoring intermediates
- Clean architecture: 5 state variables, no GCW parsing, no phase tracking

**Current v0.08.61 behavior (confirmed working):**
- Enter Item submenu → focus lands on items list (5) → announces first item
- Scroll items → each item announced immediately
- Cancel → focus goes to action menu (5→2→3) → announces "Use" (or current action)
- Left/right on action menu → debounced 200ms announce of new action
- Confirm → focus goes to items list (3→4→5) → announces current item
- Cancel from action menu → back to main menu

---

## Next Priorities

### 1. Item Submenu — Remaining Sub-flows
- **Sort confirmation**: detect when Sort executes, speak "Sorted"
- **Rearrange mode**: two cursors for source/destination selection
- **Use → party member selection**: target submenu cursor (which character to use item on)
- **Battle option sub-screen**: needs investigation

### 2. Other Submenu TTS
- +0x22E focus state likely generalizes to other submenus (Magic, GF, Ability, etc.)
- SUBMON auto-monitor is still active — entering any submenu captures byte changes
- Use same pattern: watch +0x22E for focus transitions, announce appropriate content

### 3. Save Game Flow TTS
- Save Point entity catalog integration
- Title Screen Continue TTS (enables saving after opening sequence)

### 4. Interactive Object Positions — BLOCKED
- PSHM_W / Option F (force entity script execution) — awaiting ChatGPT deep research
- Deep research prompt at: `Plan & Research Documents/Force Entity Script Execution - Deep Research Request.md`

---

## Key Architecture Reference

### Item Submenu Offsets (pMenuStateA)
| Offset | Purpose | Values |
|--------|---------|--------|
| +0x22E | Active focus indicator | 3=action menu, 5=items list |
| +0x27F | Action cursor | 0=Use, 1=Rearrange, 2=Sort, 3=Battle |
| +0x272 | Item list cursor | 0-based index into inventory |
| +0x1E6 | Top-level menu cursor | 0=Junction..10=Save |

### Savemap
- Base: `0x1CFDC5C`
- Item inventory: +0x0B40, 198 slots × 2 bytes (id, qty)
- Live game time: +0x0CCC
- Gil: +0x08 (header)

### Deep Research Correction
All ChatGPT deep research savemap offsets need -0x14 adjustment (header is 0x4C not 0x60).

---

## Files Modified This Session
- `src/menu_tts.cpp` — v0.08.51–61: Item submenu TTS rewrite, focus state detection
- `src/ff8_accessibility.h` — v0.08.61
- `src/field_navigation.cpp` — v0.08.61 version bump
- `DEVNOTES.md` — Updated with +0x22E discovery
- `NEXT_SESSION_PROMPT.md` — This file

---

## Recovery Instructions
1. Read DEVNOTES.md for architecture and key learnings
2. Read this file for immediate context
3. **Use filesystem MCP tools** — mod files on Windows, bash is separate Linux container
4. `deploy.bat` is the ONLY build script
5. Bump `FF8OPC_VERSION` in `ff8_accessibility.h` on every build (3 locations)
6. When Aaron says **"BAT"** → read tail of `Logs/ff8_accessibility.log`
7. When build error occurs → read `Logs/build_latest.log` for compiler errors
8. Versioning: `0.MM.BB` format. Current: v0.08.61 (source), v0.08.01 (deployed)
9. GitHub repo: ampage87/FFVIII-Accessibility-Mod (main branch)
10. **SAVEMAP CORRECTION**: All deep research offsets need -0x14 adjustment (header is 0x4C not 0x60)
