# Next Session Prompt

## Status: v0.14.96 BAT-PASSED, Chapter 3 closed, ready to push to GitHub

**v0.14.96 BAT result (Wed 2026-05-06):** Aaron successfully drove to all three test destinations (Balamb Town, B-Garden, Fire Cavern), AD correctly identified each arrival, and resumed correctly after random encounters. Fire Cavern's arrival fired correctly for the first time across the entire v0.14.93→v0.14.96 build sequence — closes the v0.14.93 Issue 3 case.

**The world map auto-drive system is now functionally complete for the early-game (Balamb Island) test surface.**

---

## NEXT SESSION FIRST ACTION: Aaron pushes v0.14.93 + v0.14.94 + v0.14.95 + v0.14.96 to GitHub

Aaron uses `Utilities/push_to_github.bat`. Local working tree is FOUR versions ahead of GitHub HEAD `7fdce360` (v0.14.92, pushed Tue 2026-05-05 19:53 local).

**Consolidated commit message for the bundled push:**

```
v0.14.93 + v0.14.94 + v0.14.95 + v0.14.96

Chapter 3: world map field-entry trigger reverse-engineering + deferred-arrival fix

Four builds completing the world-map auto-drive system. v0.14.93 embedded
the 38 decoded trigger programs from wmsetus.obj Section 8 as a static C++
array. v0.14.94 refactored AD steering from linear-direction-with-nudge to
A* path planning on the 32x24 segment grid using the trigger data, with a
vehicle-noise hotfix and Section 2 (region byte map) loader. v0.14.95 fixed
v0.14.94's planner-decline issue by switching from "match catalog segment's
region" to closest-active-region search. v0.14.96 fixed v0.14.95's false-
positive arrivals (random encounters near a destination misclassified as
"Arrived") by deferring the arrival decision until the game's mode register
settles. v0.14.96 BAT-validated end-to-end: drove Balamb Town, B-Garden,
and Fire Cavern correctly, paused/resumed across random encounters, and
Fire Cavern arrived correctly for the first time across the entire build
sequence.

v0.14.93 - embedded 38 decoded trigger programs

Hardcoded the 38 field-entry trigger programs from
'Plan & Research Documents/wmsetus Section 8 decoded.md' (the v0.14.92
decode artifact) as a static s_triggerPrograms[] C++ array in
src/world_map.cpp. Schema: TriggerClause = {vehicle, region, story_gte,
story_lt, unk_flags}; TriggerProgram = {loc_id, top_story_gte,
top_story_lt, top_vehicle, num_clauses, *clauses}. Vehicle operands per
the artifact's encoding table: 0x80 = Squall foot, 0x84 = alt-leader
foot, 0x30 = Garden, 0x31 = Chocobo, 0x32 = Ragnarok, 0x00 = no
restriction. Region operands index Section 2's 32x24 segment-region byte
map. story_gte=0 means no lower bound, story_lt=0 means +infinity.
unk_flags is a bitmask of UNK opcodes (0xFF0F/10/11/12/13/20/21) present
in the original clause; operand values preserved verbatim in the artifact
MD file for future decoding.

Extended WMSETUS_DUMP_SECTIONS_1IDX[] from {7, 8} to {2, 7, 8} so
Section 2 (the 32x24 segment-region byte map) gets captured at module
init. New [TRIGGER-PROGRAMS] log block at Initialize walks
s_triggerPrograms[] and emits one line per program (38 lines total) for
runtime sanity-check that the embedded data compiled correctly. NO game
behavior change at this stage - data embedded but not yet wired into AD
targeting. AD's existing catalog-center steering + sweep-search fallback
continues to work unchanged.

v0.14.93 BAT validated [TRIGGER-DUMP] sect02 dump (772 bytes), all 38
[TRIGGER-PROGRAMS] lines, and that AD continued to work via the existing
catalog-center steering. Three issues identified for v0.14.94: spurious
'Vehicle change: Car' announcements during AD operation, sweep search
not firing in final approach, and Fire Cavern arrival never firing
because catalog (36864,-28672) is 6800 units off the actual trigger zone.

v0.14.94 - A* path planner refactor

Three integrated changes addressing v0.14.93 BAT findings.

(1) VEHICLE-NOISE HOTFIX in CheckVehicleChange. Early-return when
s_driveActive prevents AD's keybd_event arrow-key injection from
polluting s_lastVehicle via the locomotion byte at 0x02040A5E (which
cycles through canonical Car/Garden/Ragnarok values during AD operation,
each held >64ms past the v0.14.90 4-poll debounce). Resolves Issues 1+2
from the BAT: 'Car' announcements during drives + sweep search not firing
when stuck in final approach (s_lastVehicle had been getting overwritten
to a vehicle value, making isOnFoot false and the sweep guard miss).

(2) SECTION 2 LOADER captures the 32x24 segment-region byte map from
wmsetus.obj into s_segmentRegionMap[24][32] at module init. Extends the
existing LoadTriggerZones (which already reads the same archive for
diagnostic dumping) - no duplicate I/O. Indexing: byte at offset
row*32+col with no header. The 4-byte trailer at offset 768 was
previously misdocumented as a header in v0.14.93's comment; corrected.

(3) A* PATH PLANNER replaces v0.14.86's linear-direction steering. New
PlanPath(start_seg, vehicle, goal_seg_set) runs A* on the 32x24 grid
with 4-neighbor edges (foot/Chocobo/Car: land-only via existing
s_terrainGrid; Ship/Garden: any segment; Ragnarok: skips planner
entirely - flies anywhere), wrap-aware Manhattan heuristic (east-west
torus shortcut for Esthar approaches), uniform edge cost. Goal-set
construction: MatchProgramForCatalog(catalog_x, catalog_y, vehicle,
story) walks s_triggerPrograms[] looking for any clause satisfiable
from current state (vehicle predicate + story window with savemap word
at 0x2036BDE); CollectGoalSegments collects every cell in
s_segmentRegionMap whose byte equals that clause's region operand.
Multi-target A* terminates at the first goal segment popped from the
priority queue, provably the closest reachable goal. StartAutoDrive runs
the planner once; UpdateAutoDrive steers toward s_drivePath[idx]'s
segment center and advances when the player crosses into it.

ARRIVAL: replaces v0.14.90.2's distance heuristic with segment-
membership - when the world map exits AND the player's last segment was
in the goal set, AD declares arrival. Fixes Issue 3 from BAT (Fire Cavern
entry at seg(19,20) was 6807 units from catalog seg(20,20) and was
misclassified as 'Paused' by the 1500-unit threshold; now any goal
segment in the trigger zone counts as arrival).

REPLAN on world-map re-entry from a battle pause - random encounters
drift the player off the planned path; replanning from post-battle
position keeps the drive efficient. Sweep search and stuck detection
stay as fallbacks for sub-segment failures.

Cars treated as foot for clause matching: cars travel the same land
segments as foot, and to cross a trigger the player will dismount and
walk the last few steps - the goal-segment region defines the trigger
zone either way.

v0.14.94 BAT FAILED: [PLAN] No matching s_triggerPrograms[] entry for
ALL THREE drives. Every drive silently fell back to v0.14.86 catalog-
center steering with v0.14.93 distance-based arrival. Drives to Balamb
Town and Balamb Garden succeeded only because the v0.14.86 fallback's
1500-unit threshold caught their refined-coords; Fire Cavern's catalog
(36864,-28672) is 6800 units off its actual trigger zone, beyond any
fallback range, so its arrival never fired.

v0.14.95 - planner correctness fix

ROOT CAUSE diagnosed from v0.14.94 BAT: catalog (X, Y) for each location
is the icon center on the world map, sitting on OPEN LAND, NOT on the
trigger zone. Examples from BAT: Balamb Town catalog -> seg(17,20) ->
region 0x07 -> only prog 9 references 0x07, top_story_gte=290, Aaron's
story=205 fails the gate. Balamb Garden catalog -> seg(19,20) -> region
0x0C -> only prog 20 (Garden vehicle, not foot). Fire Cavern catalog ->
seg(20,20) -> region 0x0C -> same as B-Garden, Garden-only. None match
foot+story-205, so v0.14.94's 'match the catalog segment's region
directly' algorithm declined every drive.

THREE INTEGRATED CHANGES.

(1) MatchProgramForCatalog REWRITTEN with closest-active-region search.
Build the active region SET first - every region byte referenced by ANY
clause currently satisfiable from veh + story - then walk the 32x24 grid
for the segment closest to (catRow, catCol) whose region is in that
active set. 5-segment distance cap avoids accidentally routing to some
other location's trigger when nothing nearby matches; in that case
decline and fall back to catalog-center. Two-pass clean/UNK preference
kept: a clean active region beats a UNK-flagged one for the same region
byte.

(2) WMSETUS_DUMP_SECTIONS_1IDX[] extended from {2,7,8} to {2,7,8,9,19}.
Sections 9 and 19 are also 772 bytes each - same shape as Section 2 -
hypothesis: they may be additional region maps for parts of the world
Section 2 doesn't cover. v0.14.95 BAT log shows Section 2 alone has only
19 unique region IDs (305 of 768 cells populated) but s_triggerPrograms[]
references 45 unique regions, so something gives us the missing 26.

(3) [DRIVE] Paused log enhanced with seg=(C,R) and region=0xRR fields
alongside the existing lastPos so post-pause analysis can confirm whether
the trigger fired in a known-active region or not.

v0.14.95 BAT FAILED differently: drives to Balamb Town and Balamb Garden
were declared arrivals incorrectly when random encounters fired within
1500 units of the destination. Two clear cases in the log: 12:43:36
'Arrival via exit-distance (fallback)' for Balamb Garden at
lastPos=(25405,-30324) dist=1215 - the player had drifted 1100 units
southward in 3 seconds, which is encounter behavior, not field-entry.
Same pattern at 12:51:26 for Balamb Town. The v0.14.95 distance heuristic
can't distinguish 'world map exited because trigger fired' from 'world
map exited because random encounter started' - both look the same at the
moment of exit (scene flag flips, dist-to-target unchanged).

ALSO from v0.14.95 BAT: Sections 9 and 19 are NOT region maps. After the
dump, MatchProgramForCatalog still showed 16 active regions, not the
missing 26. Hypothesis disproved. Resolving the missing region IDs is
deferred to v0.14.97+; v0.14.96's deferred-arrival fix makes the existing
v0.14.95 closest-active-region planner correctly identify arrivals at
Balamb Garden / Balamb Town anyway via the game-mode branch.

v0.14.96 - deferred-arrival fix per Aaron's diagnosis

Aaron's diagnosis from the v0.14.95 BAT: 'AD is correctly determining
when we arrive in Balamb or at Balamb Garden. However, when nearing
these locations on the world map, if a random encounter triggers as you
are getting very close it misidentifies the random encounter as having
arrived at the destination. Something you might consider is the game's
region / field ID, which is shown in the game's main menu.'

The fix: use the game's settled game mode AFTER the world-map exit. The
mode register at FF8Addresses::pGameMode (already exposed since v04.00 /
v01.13, used by IsOnField etc.) takes 1-3 polls to transition from
MODE_WORLDMAP (2) to its destination mode after IsOnWorldMap flips
false. The v0.14.90.2 changelog had noted reading pGameMode AT the
moment of exit always reads MODE_WORLDMAP - but reading it AFTER a brief
wait IS robust.

NEW deferred-arrival state machine in src/world_map.cpp. When world map
exits while a drive is active: capture exit tick, release drive keys,
set s_driveAwaitingArrivalDecision = true (drive stays active so cancel
works). New ResolveDeferredArrival() runs each Poll() tick (Poll
restructured so it does NOT early-return when waiting). Decision table:

  MODE_FIELD (1)         -> real arrival (also reads pCurrentFieldId +
                            pCurrentFieldName for log clarity)
  MODE_SWIRL (3)         -> encounter (paused, drive resumes on world-
  MODE_BATTLE (999)         map re-entry)
  MODE_AFTER_BATTLE (4)
  anything else (incl.   -> keep waiting up to 2 seconds
   lingering MODE_WORLDMAP=2)

Wait timeout: ARRIVAL_DECISION_TIMEOUT_MS = 2000ms. On timeout, fall
back to v0.14.95 segment-membership / distance heuristic with 'timeout-
fallback' suffix in logs.

NO new addresses (pGameMode, pCurrentFieldId, pCurrentFieldName all
already exposed by ff8_addresses.h since v04.00 / v01.13). NO new hooks.
NO build script changes.

v0.14.96 BAT-PASSED Wed 2026-05-06. Aaron: 'Successfully used AD to get
to all three locations, AD correctly identified when it arrived, and
resumed AD following random encounters. It also correctly identified
when it arrived at the Fire Cavern, which it never did before.' Fire
Cavern's arrival fired correctly for the FIRST TIME across the entire
v0.14.93 -> v0.14.96 build sequence, closing the v0.14.93 Issue 3 case.

VALIDATION

- v0.14.93 BAT 2026-05-05: trigger-data sanity-check passed; Issues 1-3
  identified for v0.14.94.
- v0.14.94 BAT 2026-05-05: planner declined every drive; root cause
  diagnosed (catalog center != trigger zone).
- v0.14.95 BAT 2026-05-06: closest-active-region search worked
  geometrically; false-positive arrivals exposed at Balamb / B-Garden;
  multi-section region-map hypothesis disproved.
- v0.14.96 BAT 2026-05-06: all three drives succeeded; correct arrival
  detection on every destination; pause/resume across encounters worked;
  Fire Cavern's first-ever successful arrival.

LESSONS

- Catalog (X,Y) coords are icon centers, NOT trigger zones. They sit on
  open land and rarely match the segment region the trigger expects.
  Closest-active-region search is the right algorithm.
- Reading game-state registers (pGameMode etc.) AT the moment of an
  edge-detected event always reads stale values. Defer 1-3 polls and the
  register settles. The v0.14.90.2 'fragile' comment was about reading
  instantly; deferred reads are robust.
- Deep-research hypotheses can have unrelated false positives. Sections
  9 and 19 looked like region maps (772 bytes each, same as Section 2)
  but turned out to be other data. v0.14.96's deferred-arrival fix made
  the planner correctness issue moot for early-game arrivals because the
  game-mode branch is authoritative regardless of whether the planner
  declines.
- Always check user diagnoses against the BAT log carefully. Aaron's
  'random encounter misidentified as arrival' was precisely the pattern
  visible in the 12:43:36 and 12:51:26 incidents in ff8_world.log.
- Vehicle-noise hotfix from v0.14.94 was essential infrastructure - AD's
  keybd_event injection cycles the locomotion byte through canonical
  vehicle values, which would otherwise poison s_lastVehicle and break
  isOnFoot-gated logic. Early-return when s_driveActive is the simplest
  fix.

DEFERRED to post-Chapter-3:

- Persistent accessibility settings (refined-coord serialization first).
  Aaron has empirical refined coords for Balamb Town, B-Garden, and Fire
  Cavern from the v0.14.96 BAT - good seed data.
- Resolving the missing 26 region IDs in s_triggerPrograms[] (Section 2
  has 19 of 45). Sections 9 and 19 ruled out by v0.14.95 BAT. Likely
  requires disassembly hunt for the wmsetus initialization path.
- Field-ID-to-name mapping for the 38 programs (display names in [PLAN]
  log lines and AD announcements).
- UNK_0F/10/11/12/20/21 opcode interpretation (only ~6 of 38 programs
  use these).
- Encounter-warning feature (Section 1 + Section 2 combined).
- Remove party members from field entity catalog.
- X-ATM092 chase scene accessibility.
- Walk-and-talk dialog gap (hardcoded engine path).
- GitHub issue #27 (SeeD rank R key).

FILES

v0.14.93:
- src/world_map.cpp - ~280 lines: TriggerClause + TriggerProgram structs,
  UNK flag constants, vehicle alias constants, 38 clause arrays + 38-
  entry program table, LogTriggerPrograms function,
  WMSETUS_DUMP_SECTIONS_1IDX[] extended to {2,7,8}, file-header CURRENT
  STATE block updated, Initialize call site for the new dumper.
- src/ff8_accessibility.h - FF8OPC_VERSION 0.14.92 -> 0.14.93.

v0.14.94:
- src/world_map.cpp - ~600 lines: s_segmentRegionMap state + Section 2
  extraction inside LoadTriggerZones, GetCurrentStoryFlag helper +
  WM_STORY_FLAG address, ClauseMatches/MatchProgramForCatalog/
  CollectGoalSegments/IsGoalSegment helpers, A* PlanPath core,
  s_drivePath state, StartAutoDrive integration, UpdateAutoDrive
  waypoint follower, segment-membership arrival check,
  CheckVehicleChange early-return, file-header CURRENT STATE block
  rewritten.
- src/ff8_accessibility.h - FF8OPC_VERSION 0.14.93 -> 0.14.94.

v0.14.95:
- src/world_map.cpp - ~110 lines: MatchProgramForCatalog rewritten with
  closest-active-region search, dump list extension, Paused log
  enhancement, file-header CURRENT STATE block updated.
- src/ff8_accessibility.h - FF8OPC_VERSION 0.14.94 -> 0.14.95.

v0.14.96:
- src/world_map.cpp - ~150 net lines: ResolveDeferredArrival function
  (~120 lines), s_driveAwaitingArrivalDecision + s_driveExitTick +
  ARRIVAL_DECISION_TIMEOUT_MS state, Poll() restructured to call
  ResolveDeferredArrival before early-return, exit handler replaced with
  deferred-decision setup, StopAutoDrive resets new state, Initialize
  resets new state, file-header CURRENT STATE block rewritten.
- src/ff8_accessibility.h - FF8OPC_VERSION 0.14.95 -> 0.14.96.

NO new addresses across all four builds. NO new hooks. NO build script
changes. v0.14.90.3 WM_ENTRY_DEBOUNCE behavior unchanged. v0.14.85.3
catalog reachability filter and terrain BFS unchanged. Scan TTS
chapter (v0.14.50-82) unchanged.
```

---

## After the push

The next chapter is Aaron's call. Suggested priorities (in order):

1. **Persistent accessibility settings** — start with refined-coord serialization for world-map locations. Aaron has empirical refined coords for Balamb Town, B-Garden, and Fire Cavern from the v0.14.96 BAT, plus EWM already persists. Good first target because it's small in scope and unblocks a long-standing user-experience issue (drives to known locations are direct on second visit).
2. **Remove party members from field entity catalog** — minor cleanup.
3. **GitHub issue #27** — `FIELD_H_OFFSET = 0xF94` SeeD rank investigation.
4. **X-ATM092 chase scene accessibility** — proposed: freeze X-ATM092 movement after battles until new field screen loads.

The two world-map deferreds (missing 26 region IDs, walk-and-talk gap) are lower priority because the early-game test surface works correctly without them.

## If something goes wrong with the push

If `Utilities/push_to_github.bat` reports an error, check:
- Is the working tree clean of untracked/modified files outside the four expected (src/world_map.cpp, src/ff8_accessibility.h, DEVNOTES.md, NEXT_SESSION_PROMPT.md)?
- Does GitHub HEAD still match `7fdce360`? If someone (Aaron, a teammate, an automated process) pushed since this session started, the branch will need a rebase or pull first.

Aaron should report any error verbatim and Claude will diagnose.
