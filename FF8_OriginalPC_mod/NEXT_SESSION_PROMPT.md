# Next Session Priorities

## Current State (v0.07.94 — source + deployed)

INF gateway catalog integration complete and working. Parser corrected using Deling source (offset 0x64, stride 32, fieldId at +18). Deduplicated gateway exits integrated into entity catalog with compass directions and auto-drive support.

### Tested fields this session:
- **bggate_2**: 4 catalog entries (NPC, Draw Point, 2 INF gateway exits). Compass directions worked.
- **bggate_1**: 5 catalog entries (NPC/Seifer, 3 INF gateway exits). All 3 exits gave compass directions.
- Auto-drive to INF gateways needs work (A* pathfinding issues with edge positions).

See local NEXT_SESSION_PROMPT.md for full priorities and recovery instructions.
