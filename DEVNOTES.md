# DEVNOTES - FF8 Accessibility Mod (Original PC + FFNx)
## Last updated: 2026-03-11

---

## CURRENT OBJECTIVE: Field Navigation (v05.xx)

### Current build: v06.07 — Micro-nudge to break wall contact during edge-midpoint recovery

### Previous builds:
- v05.76: heading fix (Y inverted, X direct), arrive=300
- v05.73: event triggers in catalog, auto-drive overshoot for trigger lines
- v05.71-72: screen transition vs event trigger classification, gateway filtering
- v05.70: screen filtering via SETLINE trigger lines (entities on other screens hidden)
- v05.69: VISDIAG diagnostic (inconclusive — bgroom_1 doesn't use SHOW/HIDE for screen transitions)
- v05.68: 8-dir wall recovery + arrive=100 + stable catalog

### CRITICAL FINDING (v05.60 COORDDIAG): Entity Y axis, not Z

**`GetEntityPos` reads the WRONG axis.** It uses offsets 0x190 (X) and 0x198 (Z),
but the actual screen-vertical coordinate is at **0x194 (Y)**. The Z axis (0x198)
is always 0 or near-0 (flat floor depth). This single bug causes:
- Entities appearing "right here" when they're far away (only X compared, Y ignored)
- Directions missing the vertical component entirely
- Auto-drive not working (phantom vertical distance from triggers, no real vertical from entities)
- Player always showing second coordinate as 0

**Evidence from bgroom_1 COORDDIAG:**
- Player ent1: `fp/4096=(537,432,0)` — Y=432 is the screen-vertical position, Z=0
- Quistis ent2: `fp/4096=(539,-433,0)` — Y=-433, Z=0
- NPC ent3: `fp/4096=(817,-2851,22)` — Y=-2851, Z=22
- Gateway: `INF_center=(0,-4628)` — second axis matches entity Y range
- Trigger SETLINE: `raw=(1610,-416,0)` — Y=-416 matches entity Y range, Z=0
- `GetEntityPos` returned `player=(537,0)` and `target=(539,0)` — using X,Z not X,Y

**Fix:** Change `GetEntityPos` to read 0x190 (X) and **0x194 (Y)** instead of 0x198 (Z).
Also update the simple int16 fallback from 0x28 (Z) to **0x24 (Y)**.

### Architecture: Entity Navigation Catalog

The navigation catalog (cycled with -/= keys, auto-drive with \) contains:
1. **Visible NPCs** — "others" entities with model >= 0, named by model ID (0-8 = main characters) or SYM name (10+ = generic NPCs)
2. **Gateway exits** — from INF file, filtered for valid destinations
3. **Trigger zones** — from SETLINE opcode hook (runtime capture), labeled "Interaction"

### SETLINE Runtime Trigger Capture (v05.56-05.57)

**Discovery:** The INF trigger section (offset 0x140) on the PC version contains only sentinel values — it does NOT store real walkmesh coordinates. Trigger lines are defined at runtime by the JSM SETLINE opcode (0x039).

**Solution:** Hook SETLINE, LINEON (0x03A), LINEOFF (0x03B) opcode handlers.

**Confirmed format (v05.56 hex dump analysis):**
- SETLINE handler signature: `int __cdecl handler(int entityPtr)`
- entityPtr = address of line entity's own state struct (NOT in pFieldStateOthers)
- Line coordinates at entity struct offset 0x188:
  - +0x00: int16 X1
  - +0x02: int16 Y1
  - +0x04: int16 Z1
  - +0x06: int16 X2
  - +0x08: int16 Y2
  - +0x0A: int16 Z2
  - +0x0C: int16 lineIndex
- Same coordinates also at offset 0x20 as int32 (X1, Y1, Z1, X2)

**Runtime behavior:**
- SETLINE fires during field_scripts_init for each line entity's init script
- LINEON/LINEOFF fire during gameplay to enable/disable triggers
- s_capturedLines[] stores per-field trigger data, reset on field load
- Only active lines appear in navigation catalog

### Model-Based Character Names (v05.53)

SYM names are entity SLOT names, not character names. The engine reuses slots:
- 'Squall_u' slot might hold Quistis (model 8) in bgroom_1
- 'Zell' slot might hold a Trepie Groupie (model 10)

**Resolution priority:**
1. Model ID → character name (models 0-8, authoritative)
   - 0=Squall, 1=Zell, 2=Irvine, 3=Quistis(casual), 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea, 8=Quistis(uniform)
2. SYM name → friendly name (for models 10+, with main-character guard)
3. Fallback: "NPC"

### Entity Classification Summary (bgroom_1 free-roam)

| Ent | Model | SYM Slot | Actual Character | Catalog Name |
|-----|-------|----------|-----------------|--------------|
| ent0 | -1 | Director | Invisible controller | (filtered) |
| ent1 | 0 | Squall | Squall (PLAYER) | (player) |
| ent2 | 8 | Squall_u | Quistis (instructor) | "Quistis" |
| ent3 | 10 | Zell | Trepie Groupie | "NPC" |
| ent4 | 13 | Zell_u | Trepie Groupie | "NPC" |
| ent5 | 14 | SelphieDummy | Trepie Groupie | "NPC" |
| ent6 | 15 | Selphie | Student | "NPC" |
| ent7-10 | -1 | various | Invisible controllers | (filtered) |

### Build History (v05.47–05.57)

| Build | Changes | Result |
|-------|---------|--------|
| v05.47 | SYM names + INF gateways, fi/fl/fs archive reader | TESTED WORKING |
| v05.48 | JSM counts, ENTDIAG dump, gateway filter | TESTED |
| v05.49 | SYM offset=0 confirmed, friendly name table | TESTED |
| v05.50 | pFieldStateBackgrounds resolved + BGDIAG dump | TESTED |
| v05.51 | Background entity catalog merge | TESTED WORKING |
| v05.52 | Clean catalog: visible entities + exits only | TESTED WORKING |
| v05.53 | Model-based character names (model ID overrides SYM) | TESTED — Quistis shows correctly |
| v05.54 | INF trigger zones in catalog (BOGUS positions) | TESTED — positions all sentinel values |
| v05.55 | INF hex dump diagnostic | TESTED — confirmed INF triggers unusable on PC |
| v05.56 | SETLINE opcode hook diagnostic | TESTED — confirmed coords at offset 0x188 |
| v05.57 | SETLINE capture + catalog integration | **PENDING TEST** |
| v05.58-60 | Simplified naming, COORDDIAG, set_tri log cleanup | TESTED |
| v05.61 | Y-axis fix: GetEntityPos reads 0x194(Y) not 0x198(Z), int16 fallback 0x24(Y) not 0x28(Z), HookedSetCurrentTriangle uses Y for center | TESTED — positions correct |
| v05.62 | A* walkmesh pathfinding (walkmesh load failed — wrong format assumed) | TESTED — walkmesh=NONE |
| v05.63 | ID format fix attempt: header=numTri+numVert, vtx first, then tri | TESTED — numVert=garbage, wrong format |
| v05.64 | ID format confirmed: FF7/PSX inline (4+N*24+N*6). A* finds paths (24wp, 7wp). Steering doesn't follow waypoints effectively — gets stuck. | TESTED — walkmesh=OK, A* works, steering broken |
| v05.65 | Path simplification (collinear cull ~30°), WAYPOINT_ARRIVE_DIST 200→400, triId-based A* start (no more island mismatch), waypoint progress logging, chain-advance past close waypoints | TESTED — simplification too aggressive (19→2), chain-advance fires at tick 0 skipping first waypoints |
| v05.66 | Cumulative-angle simplification (~15° threshold, resets on keep), delayed chain-advance (tick≥30 guard so nearby wp not skipped at start) | TESTED — simplification now preserves all (0 removed), heading still inverted, player walks opposite direction |
| v05.67 | CRITICAL: Inverted heading mapping. Walkmesh +X = screen-left, walkmesh -Y = screen-down. Arrow keys swapped: dx>0→LEFT, dx<0→RIGHT, dy>0→UP, dy<0→DOWN | TESTED — first successful Arrived! Player reaches nearby NPCs. Gets stuck on longer paths. |
| v05.68 | Rotating 8-dir wall recovery (cycles U/UR/R/DR/D/DL/L/UL), DRIVE_ARRIVE_DIST 300→100 for NPC interaction range, max ticks 1800→2400. Catalog ordering kept stable (no proximity sort). | TESTED — 8-dir recovery cycles correctly but pushes player onto disconnected walkmesh islands (triId 291/337). Once on island, A* fails permanently. First drive made good progress (wp 0-8 of 10) before getting stuck. |
| v05.69 | VISDIAG: F11 dumps visibility candidate bytes. Tested — all NPC bytes IDENTICAL between back/front of classroom. CONCLUSION: bgroom_1 doesn't use SHOW/HIDE for screen transitions. | TESTED — inconclusive, approach abandoned |
| v05.70 | Screen filtering via SETLINE trigger lines. `IsSeparatedByTriggerLine()` uses 2D cross product sign test. Entities on opposite side of any active trigger line from player are hidden. | TESTED — filtering correct, 5 entities hidden in back, 1 in front |
| v05.71 | Gateway screen filtering. Removed trigger "Interaction" entries from catalog. Show reachable trigger lines as "Screen transition" exits (entities-on-both-sides test). | TESTED — 4 exits shown in front (too many, event triggers mislabeled) |
| v05.72 | Screen transition vs event trigger classification: trigger line must separate player from screen-FILTERED entities (not just any entities). `screenFiltered[]` array tracks which entities were hidden by screen filter. Event triggers added back as "Event" type. | TESTED — correct separation, events shown separately |
| v05.73 | Event triggers in catalog as ENT_OBJECT/"Event". Auto-drive target offset 300 units past trigger line center. Type counting separates Exits/Events/NPCs. | TESTED |
| v05.74 | Heading fix attempt: removed v05.67 inversion (both axes direct). Back-to-front auto-drive worked by accident. | TESTED — back→front worked, left/right confirmed wrong |
| v05.75 | Heading: restored full v05.67 inversion (both axes inverted). | TESTED — left/right backwards |
| v05.76 | Heading: hybrid (Y inverted, X direct). UP=+Y, RIGHT=+X. arrive=300. | TESTED — left/right correct, back→front works |
| v05.77 | Trigger line crossing detection for "Arrived" announcement. `s_driveTrigCrossStart` tracks starting side via cross product. Sign flip = crossed. Reset in StopAutoDrive. | TESTED — crossing detection fires correctly when close, fires late when starting on wrong side |

| v05.78 | TALKRADIUS/PUSHRADIUS opcode hooks — diagnostic hex dump of entity struct | TESTED — hooks fire, raw data captured |
| v05.79 | Before/after diff diagnostic — confirmed talk radius at offset 0x1F8, push radius at 0x1F6 | TESTED — CHANGED @0x1F8 for TALKRAD, @0x1F6 for PUSHRAD |
| v05.80 | Per-entity talkRadius arrive distance (talkRad-20, min 30), pushRadius A* blackout, re-path after recovery | TESTED — re-pathing fires, still gets stuck in narrow walkmesh corridors due to 8-dir steering |
| v05.81 | A* edge width filtering (block <40, penalize <100 at 3x), angle alignment penalty (up to 2x for off-axis moves) | **PENDING TEST** |
| v05.82 | Resolve gamepad_states address (ff8_gamepad_state struct) for analog joystick steering. GPDIAG dumps struct layout at startup. | TESTED — address correct, struct all zeros at startup, keyboard does NOT populate analog fields |
| v05.83 | Analog steering write-test: wrote analog_lx/ly to gamepad_states entry — game ignored (no gamepad device registered). | TESTED — analog values written but no movement, keyboard fallback still works |
| v05.84 | Fake gamepad injection: install sentinel device ptr + fake DIJOYSTATE2 when auto-drive starts. Hook dinput_update_gamepad_status to return fake state instead of polling. lX/lY written BEFORE engine_eval processes input. Remove fake when drive stops. | TESTED — analog movement CONFIRMED, character moved through 7 waypoints rapidly before hitting narrow corridor |
| v05.85 | Analog-only auto-drive: removed keyboard injection — character didn't move. Game's movement code requires keyboard button state as trigger. | TESTED — FAILED, character stationary (analog alone doesn't activate movement path) |
| v05.86 | Hybrid keyboard+analog: restored keyboard injection to trigger movement, analog overrides direction. Recovery wiggle also uses analog. Arrow key cancel added but initially broken (detected injected keys). Fixed by masking s_driveHeld. | TESTED — arrow cancel works, but diagnostic proves keyboard direction DOMINATES analog. moveAng matches keyboard 8-dir, not analogAng |
| v05.87 | Diagnostic build: logs analogAng vs moveAng per tick to verify whether analog steering affects movement. | TESTED — CONFIRMED analog is NOT overriding keyboard direction. Game uses keyboard direction when keyboard buttons are active, ignoring analog stick values. |

| v05.88 | Approach D: keyboard buffer suppression AFTER engine_eval returns. Address resolution for keyboard_state byte** at 0x01CD02D8. | TESTED — suppression diagnostic never fired, timing was too late (ctrl_keyboard_actions runs INSIDE engine_eval) |
| v05.89 | get_key_state hook (0x004685F0): zero arrow keys AFTER buffer fill but BEFORE ctrl_keyboard_actions reads direction. Correct timing inside engine_eval. | **SUCCESS** — analog steering works! Player reached NPC on bg2f_1. analogAng matches moveAng. |
| v05.90 | Funnel algorithm (SSFA) for path smoothing, velocity-based stuck detection, faster recovery (40 tick/18 tick). Triangle corridor stored in s_corridor[]. | TESTED — Funnel BUG: all paths collapsed to 1 waypoint. Player overshoots. Velocity stuck + faster timing work well. |
| v05.91 | Funnel disabled (reverted to triangle-center waypoints). Close-enough arrival removed. arriveDist = full talkRadius. | TESTED — classroom NPCs all reached successfully. Corridor NPC fails (roundabout path into elevator). |
| v05.92 | Trigger-line avoidance in A* (skip triangles past trigger lines). Angle penalty removed. Edge widths increased to 80/200. | TESTED — edge thresholds too aggressive, blocked walkable aisles. |
| v05.93 | LOS walkmesh check (skip A* for clear paths). Smart heading-relative recovery directions. Edge widths reverted to 40/100. | TESTED — LOS fires but player still gets stuck (walkmesh connects but character can't fit pinch points). |
| v05.94 | Funnel fix: FindPortal uses travel direction cross product. Vertex dedup at walkmesh load (field_archive.cpp). Re-enabled FunnelPath() in drive-start and re-path. Portal diagnostic logging. | TESTED — vertex dedup works (441 tri -> 300 verts). FindPortal succeeds. But all portals still collapsed to goal because numVertices was 0 before dedup fix. After dedup, portals have L=R on same wall due to travel-direction cross product failing on parallel edges. |
| v05.95 | Tighter funnel arrive dist (FUNNEL_ARRIVE_DIST=60, s_usingFunnel flag). Wall-margin offset (30 units) on funnel waypoints. | TESTED — Quistis reachable on bgroom_1! Corridor NPC on bg2f_1 still fails — portals have both L/R at X=165 (same wall). |
| v05.96 | FindPortal uses triB CENTER cross product instead of travel direction. (v1->v2) x (v1->triB_center) always unambiguous. | TESTED — non-degenerate portals correct (different X). But corridor has wall-parallel shared edges (both endpoints at X=165) inherently degenerate. Player still stuck at corridor entrance. |

### v06.07: Micro-nudge to break wall contact during edge-midpoint recovery
- Added `NUDGE_TICKS = 8` (~0.13s) for brief perpendicular nudge
- Recovery now alternates: odd phases = edge-midpoint re-path, even phases (≥2) = micro-nudge
- Nudge direction: perpendicular to the shared edge between player's current triangle and next corridor triangle
- Direction selection: prefers perpendicular pointing toward next corridor center; validates with WouldCrossTriggerLine()
- Falls back to edge-midpoint if both perpendicular directions cross trigger lines
- After nudge completes, existing wiggle-completion code re-runs A*+FunnelPath from new position
- **TEST RESULT**: Micro-nudge fires correctly, player breaks wall contact and moves. But new problem: **steering oscillation**. Player bounces back and forth in corridor entrance (X=160-700) without making sustained forward progress toward target. Distance stays ~870-900. Recovery cycles endlessly through edge-midpoint→nudge→funnel→oscillate→stuck→repeat. Root cause: funnel path from corridor entrance generates correct waypoints but player overshoots and reverses direction every ~2 seconds. Needs walkmesh geometry analysis of bg2f_1 corridor entrance to understand why.

**Next session priority: Implement oscillation fix (closest-approach wp advancement) + wire NavLog**

### Walkmesh JSON + Camera Transform Research (v4, 2026-03-15)
- extract_walkmeshes.py upgraded to v4: extracts .ca camera file, parses axis vectors/position/zoom
- Tested ALL sign convention variants of FF7/FF8 camera projection formula
- NONE produce correct entity coordinates for bg2f_1 (angled field)
- bgroom_1 (flat field, Z≈0): raw walkmesh X,Y = entity X,Y directly (no transform needed)
- bg2f_1 (angled, Z=484-10413): entity coords clearly transformed but .ca formula doesn't match
- ChatGPT deep research confirms .ca should be the source but empirical results disagree
- Follow-up research prompt given to ChatGPT
- NavLog::CoordSample will passively collect 3D↔2D mappings for empirical solution
- JSON v4 has camera data stored for reference; projection coords are experimental/inaccurate

### NavLog Module Added (2026-03-15, NOT YET WIRED)
- src/nav_log.cpp: persistent append-mode navigation data log (ff8_nav_data.log)
- Declared in ff8_accessibility.h (NavLog namespace)
- Init/Close wired in dinput8.cpp  
- Added to deploy.bat build
- API: SessionStart, FieldLoad, DriveStart, DriveWaypoint, DriveSample, DriveRecovery, DriveEnd, CoordSample
- Still needs: wiring calls into field_navigation.cpp at actual event points

### Oscillation Root Cause Confirmed (2026-03-15)
- Player overshoots waypoints due to analog steering momentum
- Funnel wp1 at (535,-2571): player approaches from X≈290, overshoots to X≈700
- Distance to wp1 at X=700 is ~192, above FUNNEL_ARRIVE_DIST=60 threshold
- Player reverses, overshoots again → infinite oscillation
- Fix: closest-approach waypoint advancement (track s_wpMinDist, advance when dist increases)

### v06.02: Fix horizontal wall-parallel false positive
- Removed `dY < epsilon` arm from wall-parallel check in `FunnelPath()`
- Only vertical wall-parallel portals (dX near zero) are now skipped
- Horizontal portals (dY near zero, dX large) span across corridors and are always valid
- Fixed false positive on bg2f_1: portal at Y=-2342 with dX=209 dY=0 was incorrectly skipped
- **TEST RESULT**: Quistis reachable (couple attempts). Corridor NPC reachable (couple attempts). Elevator hall: Object 1 unreachable, Exits unreachable — A* returned no path because trigger-line avoidance blocks the path to screen transitions.

### v06.03: A* trigger-line exemption for screen transition exits
- Added `skipTriggerIdx` parameter to `IsSeparatedByTriggerLine()` and `ComputeAStarPath()`
- When driving to a screen transition exit, the target trigger line is exempted from A* avoidance
- Screen transitions are by definition on the other side of a trigger line — A* must be allowed to cross it
- Applied to: drive-start (same island), drive-start (island redirect bridge), and recovery re-path
- Root cause: `[A*] No path from tri 147 to tri 5` — goal triangle was behind the trigger line that A* was avoiding
- **TEST RESULT**: Screen transition exit pathfinding WORKS (`exit target: exempting trigger line 0` → `A* Path found` → `Arrived`). But: (1) Event triggers still blocked (exemption only applied to ENT_EXIT), (2) NPC drive got close (dist=129) then recovery flung player onto disconnected island (tri 137, `No path 1 iterations`), drive went haywire with 0 waypoints.

### v06.04: Trigger exemption for events + preserve waypoints on re-path failure
- Extended trigger-line exemption from ENT_EXIT only to ALL trigger targets (entityIdx <= -200)
- Events (ENT_OBJECT with entityIdx <= -200) now also get their trigger line exempted from A*
- Recovery re-path now preserves old waypoints when A* fails (player on disconnected island)
- Previously: A* failure zeroed waypoints, drive lost all guidance and wandered randomly
- Now: old waypoints restored, drive continues following the last known good path
- Classroom catalog correctly shows 2 items from front (Screen transition + Event), 8 items from back (4 NPCs + 3 exits + 1 event) — screen filtering working as designed
- **TEST RESULT**: Quistis reached successfully. Corridor NPC drive went haywire — recovery wiggle pushed player through trigger line into elevator. Trigger line at Y=-2619 was crossed during recovery phase, causing unintended screen transition.

### v06.05: Trigger-line-safe recovery wiggle
- Recovery wiggle now checks each candidate direction against active trigger lines
- `WouldCrossTriggerLine()` projects ahead by 400 units and tests if the endpoint is on the opposite side of any non-target trigger line
- Recovery angles that would cross a trigger line are skipped; next safe angle is tried
- During wiggle execution, direction is also checked each tick — wiggle aborted if player drifts toward a trigger line
- `s_driveSkipTrigIdx` persists for the drive session so recovery knows which trigger line is the target (exempt)
- Root cause: bg2f_1 corridor NPC near elevator — recovery phase pushed player past trigger0 at Y=-2619, game auto-transitioned to elevator
- **TEST RESULT**: v06.05 not built separately (superseded by v06.06)

### v06.06: Edge-midpoint fallback replaces wiggle recovery (Approach D)
- **New recovery strategy**: When stuck, instead of random wiggle directions, re-run A* from current triangle and generate edge-midpoint waypoints
- `EdgeMidpointPath()` places waypoints at the midpoint of each shared edge between consecutive A* corridor triangles
- Each edge midpoint is a guaranteed walkable "doorway" between triangles — the player steers through the exact gaps in the walkmesh
- Edge midpoints are shrunk toward the corridor center by AGENT_RADIUS (30 units) to stay away from walls
- **Eliminated**: Random 8-direction wiggle, wiggle ticks, trigger-line-crossing risk during recovery
- **Preserved**: Trigger-line exemption for exit/event targets, waypoint preservation on A* failure, trigger-line-safe checks (WouldCrossTriggerLine still available)
- Recovery flow: stuck detected → re-run A* from current tri → EdgeMidpointPath() → resume normal steering with new waypoints
- Hybrid approach: funnel path used initially (smooth), edge-midpoint used on stuck (reliable)
- **TEST RESULT**: Edge-midpoint fires correctly (A* re-runs, generates 23 wp). But player is physically wall-stuck at (-158,-2121) on tri 81 — `moveDist=0` every tick. Edge-midpoint gives the right direction but can't break wall collision. Recovery loops endlessly (same tri, same path, same stuck position). Needs a brief micro-nudge perpendicular to stuck wall before resuming edge-midpoint steering.

### Screen Filtering Architecture (v05.70-05.77)

**Core concept:** SETLINE trigger lines serve as screen boundaries. The 2D cross
product sign test determines which side of each line the player and each entity are on.
If they're on opposite sides, the entity is on a different screen.

**Implementation layers:**
1. `IsSeparatedByTriggerLine(px,py,ex,ey)` — checks ALL active trigger lines
2. `screenFiltered[]` array — tracks which entities were hidden by screen filter
3. Screen transition detection — trigger line qualifies if it separates player from
   at least one `screenFiltered` entity (or screen-filtered gateway)
4. Event detection — reachable trigger lines that are NOT screen transitions
5. Gateway filtering — gateways on other screens hidden from catalog
6. Crossing detection — `s_driveTrigCrossStart` cross product sign flip for Arrived

**Heading mapping (confirmed v05.76 for bgroom_1):**
- UP arrow key = +Y world direction (inverted from screen coordinates)
- RIGHT arrow key = +X world direction (direct, NOT inverted)
- This may vary per field depending on camera angle

**Key insight (v05.69):** bgroom_1 does NOT use JSM SHOW/HIDE opcodes for
screen transitions. The VISDIAG dumps showed all NPC entity bytes identical
between back and front of classroom. The screen transition is purely a camera
change triggered by SETLINE boundary crossing — entities are always present
in memory, the camera just shows a different portion of the field.

---

## PREVIOUS OBJECTIVE: Field Dialog TTS + Thought/Tutorial TTS

### Status: v04.36 — Field dialog TTS working
Dialog opcodes (MES, ASK, AMES, AASK, AMESW, RAMESW) all hooked and speaking via TTS.
show_dialog hook handles tutorial/thought text. Post-FMV suppression prevents stale
text. Page-advance dedup working. Walk-and-talk dialog gap remains (hardcoded engine
path, see key learnings in memory).

### Previous milestone: FMV Audio Descriptions + Skip (v03.00) — TESTED & WORKING
### Previous milestone: Title Screen TTS (v02.00) — TESTED & WORKING

---

## ARCHITECTURE

### Module System (dinput8.cpp)
AccessibilityThread polls game state ~60Hz. Module dispatch: TitleScreen, FieldDialog,
FieldNavigation, FmvAudioDesc, FmvSkip.

### Address Resolution (ff8_addresses.cpp)
Offset-chain resolution following FFNx's ff8_find_externals() pattern. All 14 FF8 PC
language variants supported. Key addresses: pFieldStateOthers, pFieldStateOtherCount,
pFieldStateBackgrounds, pFieldStateBackgroundCount, field_scripts_init,
execute_opcode_table, pWindowsArray, set_current_triangle, opcode_setline,
opcode_lineon, opcode_lineoff.

### Key Source Files
| File | Purpose |
|------|---------|
| dinput8.cpp | DLL proxy entry + game loop + module dispatch |
| ff8_addresses.h/cpp | Runtime address resolution (all game pointers) |
| title_screen.cpp | Title menu cursor TTS |
| screen_reader.cpp | Tolk-based TTS output (NVDA+SAPI dual) |
| fmv_audio_desc.h/cpp | FMV audio descriptions (WebVTT cues) |
| fmv_skip.h/cpp | FMV skip via Backspace (ReadFile EOF hook) |
| field_archive.h/cpp | fi/fl/fs archive reader for SYM/INF/JSM |
| field_navigation.cpp | Entity catalog, triggers, F9/F10 nav, auto-drive |
| field_dialog.h/cpp | Opcode dispatch hooks for field dialog TTS |
| ff8_text_decode.h/cpp | FF8 character encoding → UTF-8 decoder |
| deploy.bat | Build + deploy script (ONLY build script) |

---

## KEY LEARNINGS

- **Entity screen-vertical is Y (0x194), NOT Z (0x198)**: COORDDIAG v05.60 proved
  that the fp coord at 0x198 (Z) is always ~0 (flat floor depth), while 0x194 (Y)
  holds the actual screen-vertical position. `GetEntityPos` must use X,Y not X,Z.
  The simple int16 fallback is 0x20 (X) and 0x24 (Y), not 0x28 (Z).
  Gateway INF centers and SETLINE trigger Y values are in the same coordinate space.
- **SETLINE coordinates at offset 0x188**: Line entity struct stores X1,Y1,Z1,X2,Y2,Z2
  as 6 consecutive int16 values at offset 0x188, followed by a lineIndex int16.
  Also at offset 0x20 as int32 values. Discovered via v05.56 hex dump analysis.
- **INF trigger section is BOGUS on PC**: Offset 0x140 in INF contains only sentinel
  values (0x7FFF, 0x00FF). Real trigger data comes from SETLINE opcode at runtime.
- **Line entities have separate state structs**: The entityPtr passed to SETLINE is NOT
  in pFieldStateOthers or pFieldStateBackgrounds. Line entities get their own structs
  allocated during field_scripts_init.
- **Model ID overrides SYM for naming**: SYM names are slot names, not character names.
  Model ID (0-8) reliably identifies main party characters regardless of SYM slot.
- **SYM offset = 0 for others**: Entity state array index i maps directly to SYM[i].
- **SYM offset = otherCount for backgrounds**: bg index b maps to SYM[otherCount+b].
- **Runtime allocation ≠ JSM categories**: Engine allocates to "others" (model-bearing)
  vs "backgrounds" (script-only) based on model presence, not JSM header type.
- **Background entity activity**: bgstate at offset 0x188 in bg struct.
  0x0000=active, 0xFFFF=off.
- **Walkmesh-to-screen heading is INVERTED (v05.67)**: Arrow keys move the character
  in the opposite direction from walkmesh world coordinates. Pressing RIGHT moves
  in -X world direction, pressing UP moves in +Y world direction. The auto-drive
  heading must swap all four mappings: dx>0→LEFT, dx<0→RIGHT, dy>0→UP, dy<0→DOWN.
  Confirmed empirically by tracking player position deltas across all drive attempts.
- **Talk radius at entity offset 0x1F8** (uint16): Set by TALKRADIUS opcode (0x062).
  Confirmed via before/after diff hook. Typical values: 72, 128, 148 world units.
  This is the distance within which the player can press X to talk to the NPC.
- **Push radius at entity offset 0x1F6** (uint16): Set by PUSHRADIUS opcode (0x063).
  Confirmed via before/after diff hook. Values: 1, 8. Default (before opcode): 48.
  This is the NPC's physical collision radius.
- **Walkmesh IS the collision data**: No separate collision mesh for objects like desks.
  Areas where desks sit have no walkmesh triangles or blocked edges (neighbor=0xFFFF).
  A* routes around missing triangles. But the straight-line path between triangle CENTERS
  can pass near wall edges (0xFFFF neighbors), causing the character to get stuck on
  collision even though A* says the path is valid. The fix is the funnel/string-pulling
  algorithm which finds the widest path through the triangle corridor.
- **Analog steering solved (v05.89)**: Hook get_key_state to zero arrow scancodes after
  buffer fill but before ctrl_keyboard_actions reads direction. Keyboard arrows still
  trigger "player wants to move" but direction comes from analog gamepad path via FFNx.
  Fake gamepad device sentinel (0xDEAD0001) + fake DIJOYSTATE2 buffer.
- **FFNx replaces get_key_state and dinput_update_gamepad_status**: Our MinHook hooks
  chain through FFNx's replacements, not the original game functions. This is correct.
- **Walkmesh inline format needs vertex dedup**: The raw ID file has numVertices=0
  and no vertex array. vertexIdx[] must be built at load time by deduplicating
  inline vertices (field_archive.cpp). Without this, FindPortal() and
  GetSharedEdgeLength() silently fail (numVertices=0 means all vertex lookups
  fail the bounds check). Fixed in v05.94.
- **SSFA portal assignment**: Use cross product of (v1->v2) x (v1->triB_center)
  for left/right ordering. Travel direction fails when parallel to edge (corridors).
  triB center is always unambiguously on one side. Fixed in v05.96.
- **Degenerate wall-parallel portals**: Corridor walkmeshes have triangles sharing
  edges along the wall (both endpoints at same X or Y). These produce zero-width
  funnel portals that can't distinguish left from right. Needs special handling
  (skip degenerate portals, or widen to corridor width, or fall back to triangle centers).
- **Walkmesh JSON available for offline analysis**: `ff8_walkmeshes.json` (17MB) in project root contains all 894 game field walkmeshes extracted by `extract_walkmeshes.py`. Per-field data includes triangle vertices, neighbor connectivity, centers, and edge data. Accessible via filesystem MCP tools (`read_text_file`). Key stats: 151,651 triangles, 115,942 vertices, 47.5% of fields have disconnected islands.
- **Edge-midpoint recovery (v06.06)**: When funnel path gets player stuck, re-run A* and generate waypoints at shared-edge midpoints instead of funnel turn points. Edge midpoints are guaranteed walkable but can't break wall collision — fixed in v06.07 with micro-nudge.
- **Micro-nudge (v06.07)**: 8-tick perpendicular nudge breaks wall contact. Direction computed from shared edge between current and next corridor triangle. Prefers direction toward next corridor center. Validated with WouldCrossTriggerLine(). Successfully breaks wall-stuck, but reveals steering oscillation in corridor entrance areas.
- **Steering oscillation (v06.07, fixed v06.08)**: Player bounces back and forth in corridor entrance without forward progress. Root cause: waypoint overshoot. Fix: closest-approach wp advancement via `s_wpMinDist` tracking.
- **Micro-oscillation (v06.09, OPEN)**: Different from steering oscillation. Player bounces between two adjacent triangles (e.g. tri 126↔127 on bggate_2, ~31 unit moves). The movement is enough to reset stuck detection (DRIVE_STUCK_MIN_DIST=20) but the player makes zero progress toward the target. Need a "progress toward target" check, not just "moved at all" check.
- **Stuck detection grace period (v06.09)**: Game needs ~60 ticks to engage movement after fake gamepad install. Without grace period, stuck fires at tick 40 before player starts moving, causing immediate recovery→cancel cascade.
- **Arrow key cancel during recovery**: Must use phase≥4 threshold (not >0). Phase 1 fires almost immediately; residual key state from catalog cycling or JAWS key interception causes false cancels.
- **NavLog performance**: Per-write fflush() causes noticeable input lag during rapid recovery cycling (dozens of flushes/second). Periodic flush (5s) eliminates the lag.
- **Trigger-line crossing during drives (v06.09, OPEN)**: A* avoids trigger lines but the funnel path can approach them closely. Analog steering momentum carries the player through, causing unintended field transitions. Need per-tick trigger-line proximity check during drive.
- **deploy.ps1 writes build_latest.log**: The VBS→PS1 wrapper captures all deploy.bat output and writes its own build log. Must update `$logPath` in deploy.ps1 when changing log locations (not just deploy.bat).
- **CoordSample is the key to solving coordinate transforms**: For angled fields (bg2f_1), the 3D→2D transform is hardcoded in FF8_EN.exe. No data file or camera matrix provides it. The only way to determine the transform is empirically: log paired (3D walkmesh center, 2D entity position) data points while the player walks around, then solve for the affine matrix in Python. `NavLog::CoordSample` is declared but NOT yet wired into `HookedSetCurrentTriangle`. Wiring it is ~20 lines of code and will produce the data needed after a few minutes of gameplay on bg2f_1.
- **Walkmesh coordinates are 3D, entity coordinates are 2D (projected)**: The .id walkmesh stores 3D (X,Y,Z) vertices. Entity positions at 0x190/0x194 are in a 2D projected space. For flat fields (Z≈0 like bgroom_1), raw walkmesh X,Y = entity X,Y directly (identity). For angled fields (like bg2f_1, Z=484-10413), some transform projects 3D→2D. **The .ca camera file is NOT used for this** — it's for background rendering only. The transform is hardcoded in FF8_EN.exe and not stored in any data file (.ca, .mim, .map, .one). ChatGPT deep research (2x) confirms this. The NavLog CoordSample feature will collect empirical 3D↔2D mappings from gameplay to solve the transform numerically.
- **Walkmesh JSON triangle indices match runtime**: Both the Python extractor and C++ LoadWalkmesh read the same .id file bytes in the same order. Triangle indices, vertex dedup, and neighbor connectivity are identical. The coordinate SPACE differs (3D in JSON vs 2D in runtime) but the structural data is trustworthy.
- **Trigger-line avoidance in A***: Skip neighbor triangles whose centers are on the
  opposite side of any active SETLINE trigger line from the start position. Prevents
  A* from routing through screen transition zones.
- **Walk-and-talk is hardcoded**: Corridor dialog bypasses JSM opcodes and show_dialog.
- **Opcode dispatch SIB variant**: Game uses FF 14 95 (EDX-indexed), not FF 14 85 (EAX).
- **FMV skip — ReadFile hook**: Intercepting ReadFile returns zero bytes to simulate EOF.
- **rc.exe paths**: Resolve relative to .rc file location, not working directory.

---

## KEY FILE LOCATIONS

- Project root: `C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod\`
- Source: `src\` subdirectory
- FFNx reference: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\`
- Game folder: `C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\`
- Logs: `Logs\` subfolder (ff8_accessibility.log, ff8_nav_data.log, build_latest.log, ff8_accessibility_prev.log)
- Build: `deploy.bat` (build script), `deploy.vbs`/`deploy.ps1` (UI wrapper)

---

## RECOVERY NOTES

If conversation freezes or compacts:
1. Read this file FIRST for current state
2. Read NEXT_SESSION_PROMPT.md for immediate next steps
3. Check which task is marked "PENDING TEST"
4. Use filesystem MCP tools (not bash) for Windows file access
5. deploy.bat is the ONLY build script; deploy.vbs/deploy.ps1 is the UI wrapper
6. Current version: v06.09
7. When Aaron says "BAT" → read tail of `Logs/ff8_accessibility.log` immediately
8. Nav data log: `Logs/ff8_nav_data.log` (append-mode, persistent across sessions)
9. Build log: `Logs/build_latest.log` (written by deploy.ps1)

---

## BUILD HISTORY (v05.97 — v06.01): Navigation Pipeline Overhaul

### v05.97: Wall-parallel portal skip (epsilon=5.0) + LOS disabled
- Replaced degenerate portal detection from distance-based to axis-aligned check
- Disabled LOS bypass — always use A*+funnel at drive start
- Added pre-skip for nearby waypoints at drive start
- Result: bg2f_1 wall portals skipped, but epsilon=5.0 too aggressive for bgroom_1

### v05.98: Pre-skip nearby waypoints at drive start
- Fixed orbit bug where player circled around wp0 at their own start position
- Chain-advance delay (tick 30) caused player to steer away before skipping

### v05.99: Corridor-center steering bias
- Added wall-avoidance blend to steering direction when player near walls
- Broke bgroom_1 classroom navigation (epsilon=5.0 false positives on diagonals)

### v06.00: Tightened wall-parallel epsilon to 0.5
- Only exact axis-aligned portals (dX=0.0) caught as wall-parallel
- Fixed bgroom_1 classroom regression, but corridor NPC still unreachable

### v06.01: New pathfinding pipeline (CURRENT)
- **Walkmesh research**: Extracted all 894 game walkmeshes via Python script (`extract_walkmeshes.py`)
- **Key finding**: 47.5% of fields have disconnected walkmesh islands
- **Key finding**: bg2f_1 corridor has dual-column triangulation (center line at X=165)
- **New portal pipeline**: Wall-parallel skip (epsilon=1.0, 10x ratio) + agent-radius shrinking (30 units)
- **Removed**: Wall-margin post-processing (replaced by portal shrinking)
- **Removed**: Corridor-center steering bias (replaced by portal shrinking)
- **Added**: `AreTrianglesConnected()` BFS island connectivity check at drive start
- **Added**: Cross-island redirect to nearest trigger line bridge
- Validated offline: bg2f_1 corridor 12→5 waypoints, ratio 1.38→1.29
- Validated offline: bgroom_1 classroom 6 wp, ratio 1.13
- **TEST RESULT**: Both corridor NPC and Quistis reachable. Navigation smoother.
  Multiple attempts sometimes needed. Corridor NPC reached in 1 second on
  successful attempt (5 tris, 4 wp). Horizontal wall-parallel false positive
  found: dX=209 dY=0 portal at Y=-2342 incorrectly skipped. Fix needed:
  remove dY<epsilon check (only check dX<epsilon for vertical walls).

---

## BUILD HISTORY (v06.02 — v06.09): Drive Reliability Arc

### v06.02-v06.07: Trigger avoidance, recovery, micro-nudge
- v06.02: Exempt target trigger line from A* avoidance
- v06.04: Extended trigger exemption to event triggers; save/restore waypoints on re-path failure
- v06.05: WouldCrossTriggerLine() prevents recovery wiggle from crossing triggers; s_driveSkipTrigIdx saved for recovery
- v06.06: Edge-midpoint recovery path (alternate to funnel when stuck)
- v06.07: Micro-nudge (8-tick perpendicular to shared edge) to break wall contact

### v06.08: Overshoot detection + NavLog wiring + Log reorganization
- **Overshoot detection**: `s_wpMinDist` tracks closest approach to current waypoint. When player gets within 200 units then distance increases past minDist×1.5+50, advance to next wp. Fixes corridor entrance oscillation.
- **NavLog fully wired**: FieldLoad, DriveStart, DriveWaypoint, DriveSample (120-tick), DriveRecovery, DriveEnd all emit structured TSV lines to `Logs/ff8_nav_data.log`
- **NavLog performance**: Periodic flush (5 seconds) instead of per-write fflush. Fixes input lag during rapid recovery cycling.
- **Log reorganization**: All dev-side logs moved to `Logs/` subfolder. `deploy.bat` saves prev log there, `cl` output redirected there, `deploy.ps1` writes build_latest.log there.
- **MAX_RECOVERY_PHASES=12**: Auto-cancel drive with "Stuck. Distance remaining: N." after 12 recovery phases without waypoint progress.
- **Recovery phase resets on waypoint progress**: s_driveWigglePhase=0 when waypoint advances.

### v06.09: Stuck detection grace period + cancel responsiveness
- **Grace period**: Don't fire stuck detection until s_driveTotalTicks>=60 (~1 second). The game needs time to process fake gamepad installation before the player starts moving. Previously, stuck fired at tick 40 before any movement, causing immediate recovery → cancel.
- **Arrow key cancel threshold**: Changed from s_driveWigglePhase>0 to >=4. Phase 1 fires almost immediately; requiring phase 4 prevents cancel from residual key state during catalog cycling or JAWS key interception.
- **TEST RESULTS (v06.09 Garden exploration)**:
  - Arrived: bggate_5 cdfield8, bggate_1 screen transitions (×2)
  - Gave up: bg2f_1 NPC (×2, coordinate mismatch), bggate_2 NPC (micro-oscillation)
  - Cancelled: bggate_1 NPC/exits, bghall_1 exits, bggate_5 ddtower3 (wall-stuck)
  - Field transition: bghall_1→bghall_4, bggate_6→bghall_1 (trigger-line crossing)
  - **New bug found**: bggate_2 NPC — player oscillates tri 126↔127 (31-unit moves fool stuck detector, DRIVE_STUCK_MIN_DIST=20). Recovery phase resets every time. Need progress-toward-target check.
  - **New bug found**: Drives can cross trigger lines and cause field transitions. Need per-tick trigger-line check during drive.
