# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-21

> **File structure**: This file = current state + key learnings only (~10KB max).
> Build history in `DEVNOTES_HISTORY.md`. Immediate context in `NEXT_SESSION_PROMPT.md`.

---

## CURRENT OBJECTIVE: Menu TTS + Interactive Object Position Research

### Current build: v0.08.61 (source) / v0.08.01 (deployed)

### Submenu Cursor Discovery (v0.08.28–v0.08.61)

**Item submenu offsets confirmed:**
- **+0x22E**: Active focus indicator (v0.08.59 round-trip diagnostic). **3=action menu, 5=items list**. Transitions through intermediates: 5→2→3 (items→action), 3→4→5 (action→items). This is the primary detection mechanism — scalable to all submenus.
- **+0x27F**: Action menu cursor (0=Use, 1=Rearrange, 2=Sort, 3=Battle). Debounced 200ms.
- **+0x272**: Item list cursor index.
- **+0x230**: Phase flag (0=action, 1=items) — UNRELIABLE, does not always update on Cancel.
- **+0x5DF**: Sub-phase — UNRELIABLE, sometimes fires 3→2 and sometimes doesn't.
- **+0x234**: Submenu callback index (=5 for Item).

**Architecture (v0.08.60–61):** PollItemSubmenu watches +0x22E for transitions to stable endpoints (3 or 5). On arriving at 3: announce action option. On arriving at 5: announce current item. Debounced +0x27F handles left/right action navigation. Clean, no GCW string matching or phase fallbacks.

See full DEVNOTES.md in local project for complete content (PSHM_W investigation, savemap layout, key learnings, etc.).
