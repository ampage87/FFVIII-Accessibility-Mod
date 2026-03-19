# Next Session Priorities

## Current State (v0.07.94 — source + deployed)

INF gateway catalog integration complete and working. Parser corrected using Deling source (offset 0x64, stride 32, fieldId at +18). Deduplicated gateway exits integrated into entity catalog with compass directions and auto-drive support.

### Tested fields this session:
- **bggate_2**: 4 catalog entries (NPC, Draw Point, 2 INF gateway exits). Compass directions worked. User manually navigated to Draw Point successfully.
- **bggate_1**: 5 catalog entries (NPC/Seifer, 3 INF gateway exits). All 3 exits gave compass directions. Destinations resolved: "B-Garden - Front Gate 2", "B-Garden - Parking Lot", "wm00".
- Auto-drive to INF gateways failed (A* pathfinding issues with gateway positions far from walkmesh). Low priority — entity identification is the focus.

### INF Gateway Key Facts
- INF format: 676 bytes (not 672). Confirmed from Deling source (`myst6re/deling` → `src/files/InfFile.h`).
- Gateway offset: 0x64, stride 32 bytes, 12 entries. fieldId at +18 within each 32-byte gateway.
- Static destinations are placeholders on some fields (overwritten at runtime by MAPJUMPO). Show as generic "Exit to [fieldname]".
- Deduplication: multiple gateways with same destFieldId → one catalog exit with averaged center.
- Entity index sentinel: -400 range for INF gateway exits.

---

## Priorities for Next Session

### 1. Top-level menu navigation TTS (HIGH)
Cursor at `pMenuStateA + 0x1E6`. Values 0–10 map to Junction through Save. Need to hook the cursor changes and speak menu item names.

### 2. Save Game flow TTS (HIGH)
The save screen cursor system works (v0.07.63). Need to connect it to the in-game Save command flow.

### 3. Save Point entity catalog integration (MEDIUM)
Save Points are detected via model ID 24. Need to verify they appear correctly as catalog exits that the player can drive to and interact with.

### 4. Title Screen Continue TTS (MEDIUM)
Enables saving after the opening sequence, reducing future test setup time.

### 5. Fix auto-drive to INF gateway exits (LOW)
Auto-drive fails because gateway positions are at walkmesh edges (far from player's current island). Need to either route to nearest trigger line or treat INF gateways as walk-toward targets without A* pathing.

### 6. Fix saveline0 position (LOW — unchanged)
saveline0 (JSM ent36) beyond 10-slot runtime entity array. SETLINE writeback can't reach it.

---

## What's Working

- **INF gateway exits**: Correct parser, deduplicated catalog entries, compass directions, display name resolution
- **JSM-based exits**: saveline0 (elevator), water (main gate) detected with positions
- **Camera pan filter**: l1-l6 correctly classified, don't leak as events
- **Draw/Save points**: Detected and navigable
- **All prior features**: dialog TTS, field navigation, FMV, menu TTS, BGM volume, save screen, trigger line classification

---

## Recovery Instructions

1. Read DEVNOTES.md for architecture and key learnings
2. Read this file for immediate context
3. Reference `DEEP_RESEARCH_INF_GATEWAYS_Findings.md` for gateway system details
4. **Use filesystem MCP tools** — mod files on Windows, bash is separate Linux container
5. `deploy.bat` is the ONLY build script
6. Bump `FF8OPC_VERSION` in `ff8_accessibility.h` on every build
7. When Aaron says **"BAT"** → read tail of `Logs/ff8_accessibility.log`
8. Versioning: `0.MM.BB` format. Current: v0.07.94.
9. GitHub repo: ampage87/FFVIII-Accessibility-Mod (main branch)
10. INF format reference: `myst6re/deling` on GitHub → `src/files/InfFile.h`
