// world_map.cpp - World map navigation TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.14.98 — Planner-decline FIX. v0.14.97 BAT'd 2026-05-06
//                17:07:01 with full PLAN-DEBUG trace. Confirmed the v0.14.96
//                hypothesis exactly: Balamb Town's region 0x07 fell out of
//                the active set because program 9 (loc_id=0x010B) was
//                rejected with 'SKIP top_story=[290,0) story=205 out of
//                window'. The 16-region active set lacked 0x07; closest-
//                active-region search picked 0x09 at seg(17,1) (wrong side
//                of the map); planner declined; drive fell back to v0.14.86
//                catalog-center steering.
//
//                The trace also exposed the structural inconsistency that
//                identifies the disassembler error: program 9's clauses
//                have story windows [0..490) for region 0x06 and [0..3900)
//                for region 0x07 — BOTH including stories below the
//                supposed top-level gate of 290. If "≥290" were truly the
//                top-level requirement, the clauses targeting pre-290
//                stories would be unreachable code. The v0.14.92 Python
//                disassembler put 0xFF02 0x0122 in the wrong scope; that
//                operand belongs to clause 0 (creating effective window
//                [290, 490) for region 0x06's disc-2 Balamb-during-Garden-
//                departure phase), not to the program top.
//
//                FIX: change s_triggerPrograms[9].top_story_gte from 290
//                to 0. With the spurious top-level gate removed, region
//                0x07 (Balamb Town's actual gate trigger) joins the active
//                set at any story; closest-active-region search picks it
//                with segDist=0 from catalog seg(17,20); PlanPath either
//                produces an actual route (if start segment differs from
//                goal) or recognizes that the player is already in the
//                goal segment (empty-path case, normal final-approach
//                walking takes over). One-line value change plus comment
//                expansion in src/world_map.cpp documenting the diagnosis.
//
//                v0.14.97 PLAN-DEBUG trace logging UNCHANGED — still emits
//                per-program/per-clause lines so we can confirm the fix
//                worked AND audit other locations for the same scope-
//                error pattern. v0.14.97 sweep-abort-on-drift UNCHANGED.
//                v0.14.96 deferred-arrival flow UNCHANGED. v0.14.95
//                closest-active-region search algorithm UNCHANGED. NO
//                new addresses, NO new hooks, NO build script changes.
//
//                v0.14.98 BAT plan: drive from current save (story=205,
//                on foot, near western Balamb coast) toward Balamb Town.
//                Verify in Logs/ff8_world.log:
//                  (a) [PLAN-DEBUG] [09] loc=0x010B clause 0/1 lines
//                      now show PASS for region 0x07.
//                  (b) Active region set now includes 0x07 (17 regions
//                      instead of 16).
//                  (c) [PLAN] line shows 'closest active region 0x07 at
//                      seg(17,20) segDist=0' — catalog and target are
//                      both in region 0x07's segment.
//                  (d) PlanPath either succeeds with a path or logs
//                      'Player already in goal segment'.
//                  (e) Drive completes with arrival via game-mode (or
//                      catalog-center final-approach into the trigger).
//                If this works, audit other story-gated programs (10,
//                13, 14 are the candidates with clause-local windows
//                that may also have mis-scoped top-level gates).
//
//   Prior baseline:
//   v0.14.97 — Planner-decline diagnostic + sweep gating fix.
//                Post-v0.14.96 push field-test (Aaron's BAT 2026-05-06
//                evening) showed every drive declines the planner with
//                [PLAN] No path from seg(X,Y) to any of N goal cells.
//                Drives still completed sometimes via v0.14.86 catalog-
//                center fallback steering with v0.14.96 deferred-arrival
//                detection, but a drive to Balamb Town from the western
//                coast hit a separate failure mode: sweep search
//                exhausted all 6 phases at (6354,-27941) — 7000 units
//                west of the target — declaring 'Could not find entrance'
//                while the player was nowhere near the entrance. Sweep
//                state had survived a battle pause/resume, persisted
//                across re-entry far from final-approach zone, and run
//                its course at the wrong location.
//
//                ROOT CAUSE for the planner decline (hypothesized):
//                Balamb Town's actual trigger region is 0x07, referenced
//                only by program 9 with top_story_gte=290. Aaron's story
//                flag is 205, so program 9 is filtered out by the top-
//                level story gate, region 0x07 falls out of the active
//                set, closest-active-region search picks an unrelated
//                region (0x09 at seg(17,1)) at an unrelated segment,
//                planner declines. Same pattern for B-Garden (region
//                0x0C, only program 20, Garden vehicle only) and Fire
//                Cavern (also 0x0C). v0.14.97 adds [PLAN-DEBUG] trace
//                logging to confirm or refute this hypothesis.
//
//                TWO INTEGRATED CHANGES:
//
//                (1) [PLAN-DEBUG] CLAUSE-REJECTION TRACE in
//                MatchProgramForCatalog. Walks every TriggerProgram and
//                emits one log line per program. Top-level skips log
//                their reason (vehicle-mismatch / story-out-of-window /
//                no-clauses). Per-clause lines include vehicle, region,
//                story window, unk_flags, and the explicit pass/fail
//                reason (PASS / FAIL veh / FAIL story / FAIL veh+story).
//                Followed by an active-region summary: 'Active region
//                set after walk (N): {0x07, 0x09, ...}'. This dump tells
//                us exactly which clauses are getting filtered and why,
//                enabling v0.14.98+ to ship the actual fix (correct
//                program-9 story bounds, missing program for pre-mobile-
//                Garden Balamb entry, or operand-decode correction).
//                Cost: ~50-80 log lines per StartAutoDrive call (38
//                programs * ~1-3 clauses each); diagnostic only,
//                strippable when the planner gap is closed.
//
//                (2) SWEEP ABORT ON DRIFT in UpdateAutoDrive. When sweep
//                is active and the player has drifted to dist >
//                FINAL_APPROACH_DIST * 1.5 (typically because a battle
//                resumed at a position far from where sweep activated),
//                clear s_sweepActive and return to normal steering.
//                Without this, sweep persists across pause/resume cycles
//                and exhausts at the wrong location — the v0.14.96 BAT
//                showed exactly this at 7km from Balamb Town. Grace
//                band of 1.5x final-approach distance prevents disrupting
//                a legitimate mid-sweep wandering search; only fires when
//                the player is clearly NOT in the entrance-search zone.
//                Stuck-far-from-target keeps falling through to the
//                existing 'Stuck. Cannot reach destination.' path.
//
//                NOTE: A third item proposed in NEXT_SESSION_PROMPT —
//                'use refined coords for the planner goal cell' — was
//                investigated and found to be ALREADY IN PLACE since
//                v0.14.94. StartAutoDrive overwrites s_driveTargetX/Y
//                with refined values when available, then PlanDrivePath
//                passes them to MatchProgramForCatalog. No change needed.
//
//                NO new addresses, NO new hooks, NO build script changes.
//                v0.14.96 deferred-arrival flow UNCHANGED. v0.14.95
//                closest-active-region search UNCHANGED — v0.14.97 only
//                adds visibility into why it's declining. v0.14.94
//                vehicle-noise hotfix and Section 2 loader UNCHANGED.
//
//                v0.14.97 BAT plan: from Aaron's current save (story=205,
//                on foot, near western Balamb coast), press \\ to start
//                a drive to Balamb Town. Cancel after a few seconds.
//                Upload Logs/ff8_world.log. Expect [PLAN-DEBUG] lines to
//                fire at drive start with full clause-rejection trace.
//                Diagnostic dump tells us which clauses are filtered out
//                and why; v0.14.98 ships the actual fix.
//
//   Prior baseline:
//   v0.14.96 — Chapter 3 Stage 5.2: false-positive arrival
//                fix after v0.14.95 BAT. Aaron BAT'd v0.14.95 and reported
//                drives to Balamb Town and Balamb Garden announced 'Arrived'
//                when a random encounter triggered as the player neared
//                each destination — the v0.14.95 BAT log shows two clear
//                cases (12:43:36 'Arrival via exit-distance (fallback)'
//                for Balamb Garden at lastPos=(25405,-30324) dist=1215,
//                where the player had drifted 1100 units southward in 3
//                seconds; 12:51:26 same pattern for Balamb Town). Both
//                are encounters, NOT arrivals. The v0.14.95 distance
//                heuristic can't distinguish them because both signals
//                say 'yes': scene flag flips, player IS near target.
//
//                Aaron's diagnosis (and the cure): use the game's settled
//                game mode AFTER the world-map exit. The mode register at
//                FF8Addresses::pGameMode (already resolved at startup,
//                used by IsOnField etc.) takes 1-3 polls to transition
//                from MODE_WORLDMAP (2) to its destination mode after
//                IsOnWorldMap flips false. The v0.14.90.2 changelog noted
//                this and abandoned the approach as 'fragile' — but the
//                fragility was about reading mode INSTANTLY at exit (which
//                always reads MODE_WORLDMAP). Reading mode AFTER a brief
//                wait IS robust.
//
//                NEW deferred-arrival state machine. When world map exits
//                while a drive is active: capture the exit tick, release
//                drive keys, set s_driveAwaitingArrivalDecision=true (drive
//                stays active so cancel still works). New ResolveDeferred-
//                Arrival() runs each Poll() tick (Poll restructured so it
//                does NOT early-return when waiting). Decision table:
//                  MODE_FIELD (1)        : entered a field — ARRIVAL
//                  MODE_SWIRL (3)        : pre-battle swirl — ENCOUNTER
//                  MODE_BATTLE (999)     : in battle      — ENCOUNTER
//                  MODE_AFTER_BATTLE (4) : post-battle    — ENCOUNTER
//                  anything else (incl. MODE_WORLDMAP=2) : keep waiting
//                Wait timeout: ARRIVAL_DECISION_TIMEOUT_MS = 2000ms. On
//                timeout, fall back to v0.14.95 segment-membership /
//                distance heuristic with 'timeout-fallback' suffix in logs.
//
//                Arrival path also reads pCurrentFieldId + pCurrentField-
//                Name and includes both in the log line:
//                  'Arrival via game-mode (mode=1 MODE_FIELD,
//                   fieldId=0x002F, fieldName='balamb_town_entrance',
//                   target=Balamb Town, ...)'
//                Encounter path identifies which mode triggered the pause:
//                  'Paused via game-mode (mode=3 MODE_SWIRL, ...)'
//                  'Paused via game-mode (mode=999 MODE_BATTLE, ...)'
//                These give post-BAT diagnostic clarity that v0.14.95
//                lacked.
//
//                v0.14.95 closest-active-region planner UNCHANGED.
//                v0.14.94 vehicle-noise hotfix and Section 2 loader
//                UNCHANGED. v0.14.93 trigger-program data UNCHANGED.
//                NO new addresses (pGameMode, pCurrentFieldId,
//                pCurrentFieldName all already exposed by ff8_addresses.h
//                since v04.00 / v01.13). NO new hooks. NO build script
//                changes.
//
//                ALSO from v0.14.95 BAT: Sections 9 and 19 are NOT region
//                maps — still 16 active regions in MatchProgramForCatalog
//                after their dump. Multi-section region-map hypothesis
//                wrong. v0.14.96 doesn't extend the dump list further;
//                resolving the missing 26 region IDs is deferred to
//                v0.14.97+. The deferred-arrival fix in v0.14.96 makes
//                the existing closest-active-region planner correctly
//                identify arrivals at Balamb Garden / Balamb Town anyway
//                via the game-mode branch when pGameMode says MODE_FIELD,
//                even when the planner declines (which it currently does
//                for those two locations because their trigger regions
//                aren't in Section 2).
//
//                v0.14.96 BAT plan: drive Garden → Balamb-Town → Fire-
//                Cavern again. Verify ff8_world.log shows: new
//                [DRIVE] Awaiting arrival decision (target=..., ...) lines
//                on every world-map exit during drive; new [DRIVE] Arrival
//                via game-mode (mode=1 MODE_FIELD, fieldId=0x..., field-
//                Name='...', ...) lines for real arrivals; new [DRIVE]
//                Paused via game-mode (mode=3 MODE_SWIRL, ...) or
//                (mode=999 MODE_BATTLE, ...) lines for encounters; NO
//                false-positive arrivals when random encounters fire near
//                Balamb / B-Garden. Drive should correctly resume after
//                each battle, eventually arriving at the actual location.
//
//   Prior baseline:
//   v0.14.95 — Chapter 3 Stage 5.1: planner correctness fix after v0.14.94
//                BAT. Rewrote MatchProgramForCatalog with closest-active-
//                region search (5-segment cap), extended dump list to
//                {2,7,8,9,19} (later disproved — not region maps),
//                enhanced [DRIVE] Paused log with seg+region.
//
//   v0.14.94 — Chapter 3 Stage 5: auto-drive refactor from
//                linear-direction-with-nudge steering to A* path
//                planning on the 32x24 segment grid. Three integrated
//                changes addressing v0.14.93 BAT findings:
//
//                (1) VEHICLE-NOISE HOTFIX in CheckVehicleChange —
//                early-return when s_driveActive prevents AD's
//                keybd_event arrow-key injection from polluting
//                s_lastVehicle via the locomotion byte at 0x02040A5E
//                (which cycles through canonical Car/Garden/Ragnarok
//                values during AD operation, each held >64ms past
//                the v0.14.90 4-poll debounce). Resolves Issues 1+2
//                from the BAT: 'Car' announcements during drives +
//                sweep search not firing when stuck in final approach
//                (s_lastVehicle stays at its pre-drive foot value,
//                so isOnFoot stays true, so the sweep guard fires).
//
//                (2) SECTION 2 LOADER captures the 32x24 segment-
//                region byte map from wmsetus.obj into
//                s_segmentRegionMap[24][32] at module init. Loader
//                folds into existing LoadTriggerZones (which already
//                reads the same archive for diagnostic dumping) — no
//                duplicate I/O. Indexing: byte at file offset
//                row*32+col with no header (the 4-byte trailer at
//                offset 768 was previously misdocumented as a header
//                in v0.14.93's comment; corrected). Loader logs
//                [REGION-MAP] populated-cell count + unique region
//                IDs at init for sanity check.
//
//                (3) A* PATH PLANNER replaces v0.14.86's linear-
//                direction steering. PlanPath(start, vehicle) runs
//                A* on the 32x24 grid with 4-neighbor edges (foot/
//                Chocobo/Car: land-only via existing s_terrainGrid;
//                Ship/Garden: any segment; Ragnarok skips the
//                planner entirely — it can fly anywhere so segment
//                routing is moot), wrap-aware Manhattan heuristic,
//                uniform edge cost. Goal-set construction:
//                MatchProgramForCatalog walks s_triggerPrograms[]
//                looking for any clause satisfiable from current
//                state (vehicle predicate matches AND story window
//                contains the savemap word at 0x2036BDE);
//                CollectGoalSegments collects every cell in
//                s_segmentRegionMap whose byte equals that clause's
//                region operand. Multi-target A* terminates at the
//                first goal segment popped from the priority queue,
//                provably the closest reachable goal.
//
//                StartAutoDrive runs the planner once; UpdateAutoDrive
//                steers toward s_drivePath[s_drivePathIdx]'s segment
//                center and advances when the player crosses into
//                it. ARRIVAL: replaces v0.14.90.2's distance heuristic
//                with segment-membership — when the world map exits,
//                the player's last segment is checked against the
//                goal set; in-set = arrival, out-of-set = paused
//                (battle/encounter). Fixes Issue 3 from BAT (Fire
//                Cavern entry at seg(19,20) was 6807 units from
//                catalog seg(20,20); 1500-unit threshold misclassified
//                it as 'Paused'; now any region-matching segment
//                counts as arrival).
//
//                REPLAN on world-map re-entry from a battle pause
//                (random encounters drift the player off the planned
//                path; replanning from post-battle position keeps
//                the drive efficient). Sweep search and stuck
//                detection STAY as fallbacks for sub-segment failures
//                (planner says traversable but a sub-segment obstacle
//                blocks the player). UNCHANGED: keybd_event injection,
//                camera-axis projection, pause/resume, BFS catalog
//                reachability filter, refined-coord capture (still
//                records the player's actual entry position for
//                future sessions on a per-location basis).
//
//                Cars treated as foot for clause matching: cars
//                travel the same land segments as foot, and to cross
//                a trigger the player will dismount and walk the
//                last few steps — the goal-segment region defines
//                the trigger zone either way.
//
//                Pressing `\` while a drive is in progress cancels.
//
//                BAT plan: drive Garden→Garden short test, then
//                Garden→Balamb-Town long drive, then Balamb-Town→
//                Fire-Cavern (the v0.14.93 Issue 3 case). Verify
//                ff8_world.log shows: [REGION-MAP] Section 2 loaded
//                (768 populated cells, ~70 unique region IDs) at
//                init; [TRIGGER-PROGRAMS] dump unchanged from v0.14.93;
//                no spurious 'Vehicle change: Car' lines during
//                drives; [PLAN] Matched program / Path found / Reached
//                waypoint lines per drive; [DRIVE] Arrival via
//                segment-membership at Fire Cavern (NOT 'Paused
//                dist=6807'); refined entry coords captured for all
//                three destinations.
//
//   v0.14.93 — 38 decoded field-entry trigger programs embedded as
//              static s_triggerPrograms[] C++ array; Section 2
//              capture in dump list. v0.14.94 wires this data into
//              AD targeting.
//   v0.14.92 — Section 8 hex dump that drove the decode (replaced
//              by v0.14.93's data embedding; dump infrastructure
//              stays).
//   v0.14.86 — Auto-drive restored from v0.11.08 baseline. Linear
//              direction-with-nudge steering toward catalog center;
//              v0.14.94 replaces this with A* path planning.
//   v0.11.16 — Deferred catalog build (position validity check).
//   v0.14.31 — Update()/Shutdown() restored after v0.14.24 build damage.
// ============================================================================
//
// Architecture mirrors FieldNavigation but simpler:
// - Hardcoded 37-entry location catalog (26 main + 7 chocobo + 4 alien)
// - On world map entry: compute distances, sort, freeze list
// - -/= cycle through sorted list with TTS announcement
// - Backspace announces bearing + distance to selected location
// - \ reserved for auto-drive (future)
// - Continuous polling: vehicle changes, world map entry/exit
//
// All addresses confirmed static (no pointer chains needed):
//   Position: 0x0203EE80/84/88 (X/Y/Z DWORDs)
//   Heading:  0x0203ED02 (WORD, 0-4095, 0=North CW)
//   Vehicle:  0x02040A5E (BYTE, locomotion mode)
//   Scene:    0x0203ED2C (WORD, 0=worldmap 1=field/battle)

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "world_map.h"

// Forward declarations for namespaces
namespace Log { void World(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); }

namespace WorldMap {

// ============================================================================
// Confirmed static addresses (from deep research + ff8-speedruns + v0.11.02 diag)
// ============================================================================
static const uint32_t WM_POS_X      = 0x0203EE80;  // DWORD - player X
static const uint32_t WM_POS_Y      = 0x0203EE84;  // DWORD - player Y
static const uint32_t WM_POS_Z      = 0x0203EE88;  // DWORD - player Z
static const uint32_t WM_HEADING    = 0x0203ED02;   // WORD  - 0=North, 0-4095 CW
static const uint32_t WM_LOCOMOTION = 0x02040A5E;   // BYTE  - locomotion / vehicle mode. v0.14.83 whitelisted to canonical {0..4}; per `Plan & Research Documents/World Map Terrain and Locomotion Reference.md` legitimate values include 0=Squall foot, 6=Selphie foot, 10=train, 31=Chocobo, 32=invisible-car — our GetVehicleName uses 0/1/2/3/4 for foot/Car/Chocobo/Ship/Ragnarok which DISAGREES with the research-doc enum (research says Chocobo=31, we say 2). Empirical reconciliation needed; see v0.14.84 changelog.
static const uint32_t WM_SCENE_FLAG = 0x0203ED2C;   // WORD  - 0=worldmap, 1=field
static const uint32_t WM_STORY_FLAG = 0x02036BDE;   // WORD  - savemap story-flag word read by sub_545F10's 0xFF02 (GTE) / 0xFF03 (LT) opcodes; v0.14.94 reads this for s_triggerPrograms[] gate evaluation. Range observed in artifact: 0..5000 across the 38 programs.

// World map dimensions (wrapping torus)
static const double WM_WIDTH  = 262144.0;
static const double WM_HEIGHT = 196608.0;

// ============================================================================
// wmx.obj terrain grid constants (v0.14.85 restoration from v0.11.16 impl)
// ============================================================================
// world.fi entry 9 points at wmx.obj inside world.fs. The mesh contains 835
// segments. Of those, the first 768 form the playable 32x24 grid; the remainder
// are story variants. Each segment is exactly 36864 (0x9000) bytes and is
// structured as:
//   bytes 0..3   : group_id (uint32)
//   bytes 4..67  : 16 x uint32 block offsets (relative to segment base)
//   then 16 variable-length blocks at the offsets above
//   then padding to 36864.
// Each block starts with a 4-byte header:
//   byte 0: poly_count (1 byte)
//   byte 1: vert_count (1 byte)
//   byte 2: norm_count (1 byte)
//   byte 3: pad
// Followed by poly_count x 16-byte polygon records, vert_count x 8-byte
// vertices, norm_count x 8-byte normals, and a 4-byte end pad.
// Each polygon's terrain (ground type) byte lives at polygon offset 0x0D.
// Ocean values are 32 (shallow) / 33 (light) / 34 (dark); everything else
// is land. Source: `Plan & Research Documents/wmx.obj polygon format deep
// research findings.md`. Earlier reconstructed parsers that scanned a flat
// `segment[poly * 16 + 13]` stride from segment offset 0 (ignoring the
// per-block headers) read garbage bytes — v0.14.85 BAT classified all 768
// segments as land because the garbage-byte distribution rarely landed in
// the 32-34 range. The correct walker per the research doc is implemented
// in v0.14.85.1's LoadTerrainGrid.
static const int      WMX_FL_INDEX        = 9;
static const int      WMX_SEG_COLS        = 32;
static const int      WMX_SEG_ROWS        = 24;
static const int      WMX_PLAYABLE_SEGS   = 768;
static const int      WMX_TOTAL_SEGS      = 835;
static const uint32_t WMX_SEGMENT_SIZE    = 36864;
static const int      WMX_BLOCKS_PER_SEG  = 16;
static const int      WMX_SEG_HEADER_SIZE = 4 + 16 * 4;   // group_id + 16 block offsets = 68 bytes
static const int      WMX_BLOCK_HDR_SIZE  = 4;            // poly_count + vert_count + norm_count + pad
static const int      WMX_POLY_SIZE       = 16;
static const int      WMX_TERRAIN_OFFSET  = 0x0D;

// ============================================================================
// wmsetus.obj section constants (v0.14.92 — field-entry bytecode decoder)
// ============================================================================
// world.fi entry 10 points at wmsetus.obj inside world.fs (the EN-locale
// wmset; bare wmset.obj is unused leftover per the FF Inside wiki). The file
// begins with a 48-entry section-offset header: 48 little-endian uint32s
// (192 bytes total), each holding the byte offset within wmsetus.obj where
// that section's data starts.
//
// Per disassembly of FF8_EN.exe sub_542DA0 (the wmsetus section-pointer
// setup function, 1-indexed iteration order matches array index +1):
//   Section 1 (392 bytes)  → [0x2040068]  encounter table, sub_541C80
//   Section 2 (772 bytes)  → [0x2040330]  region/terrain map (32x24+hdr)
//   Section 3 (88 bytes)   → [0x2040090]
//   Section 4 (1348 bytes) → [0x2036be8]  encounter destinations
//   Section 5 (8 bytes)    → [0x203ed40]
//   Section 6 (68 bytes)   → [0x2040080]
//   Section 7 (56 bytes)   → [0x2040074]  adjacent to 8, possibly metadata
//   Section 8 (2652 bytes) → [0x2040070]  FIELD ENTRY BYTECODE, sub_545EA0
//   ...
//   (Section 17/18 turn out to be wm2field-style destination data, not
//   trigger geometry as the deep research had hypothesized.)
//
// THIS BUILD hex-dumps Sections 7 and 8 to ff8_world.log under [TRIGGER-DUMP].
// Section 8 is the field-entry bytecode (the data we need to decode for AD).
// Section 7 is small enough (56 bytes) to dump for free in case it turns out
// to be related metadata (entry-point index, region-to-bytecode-offset map,
// etc.). Earlier sections (encounters / region map / destinations) are NOT
// dumped here — they're future work for an 'encounter warning' feature, not
// needed for the v0.14.92 → v0.14.94 AD-steering plan. The 48-entry section
// table is still logged in full so we have a layout sanity-check.
static const int      WMSETUS_FL_INDEX            = 10;
static const int      WMSETUS_SECTION_COUNT       = 48;
static const int      WMSETUS_HEADER_BYTES        = WMSETUS_SECTION_COUNT * 4;   // 192
static const uint32_t WMSETUS_DUMP_CAP_BYTES      = 16384; // safety cap per section in the hex-dump

// Sections to hex-dump (1-indexed in user-facing logs; array index = N-1).
// Order is dump-order in the log; the table sanity-check log line lists all
// 48 sections regardless. Adding more sections to this list is a one-line
// change for any future build that needs a different slice of wmsetus.obj.
// v0.14.93 added Section 2 (the 32x24 segment-region byte map): each byte
// is the region ID for one segment, addressed at file offset row * 32 + col.
// (A 4-byte trailer '00 00 00 00' lives at file offset 768 — not a header.)
// The 0xFF08 region operands in Section 8's bytecode reference these bytes.
// AD steering (v0.14.94+) uses Section 2 to translate catalog (X, Y) into
// a region byte and then matches that against s_triggerPrograms[]'s clauses.
static const int      WMSETUS_DUMP_SECTIONS_1IDX[] = { 2, 7, 8, 9, 19 };  // v0.14.95: +9,19 (also 772b each — hypothesis: per-area region maps; Section 2 alone has only 19 of the 45 region IDs s_triggerPrograms[] references, so the world is split across multiple 32x24 maps)
static const int      WMSETUS_DUMP_COUNT           = sizeof(WMSETUS_DUMP_SECTIONS_1IDX) / sizeof(WMSETUS_DUMP_SECTIONS_1IDX[0]);

// ============================================================================
// Field-entry trigger programs (v0.14.93 — decoded from wmsetus.obj Section 8)
// ============================================================================
// Section 8 of wmsetus.obj is a bytecode program walked each frame on foot
// by sub_545EA0 to determine whether the player has crossed a field-entry
// trigger. The program list was decoded from the v0.14.92 BAT [TRIGGER-DUMP]
// of Section 8 (2652 bytes, 38 programs, full opcode table mapped via the
// Python disassembler in the bash sandbox). Authoritative reference:
// `Plan & Research Documents/wmsetus Section 8 decoded.md`.
//
// Each program is identified by a wmField/location ID (0xFF06 operand,
// range 0x0031..0x02C1). The program may carry top-level gates: a story-
// flag window applied to the savemap word at [0x2036bde/0x2036bdf], and/or
// a vehicle-state restriction. Inside, it has zero or more clauses; each
// clause is a (vehicle, region) test the player's segment+state must
// satisfy, optionally with its own narrower story window. Clauses are
// OR'd at the top level (any one matches → program fires).
//
// Region operands (0xFF08) are bytes referenced into Section 2's 32x24
// segment-region map (which v0.14.93 also captures via the extended dump
// list). For each catalog (X, Y), AD computes the segment, reads Section
// 2's region byte for that segment, then finds all programs+clauses where
// the byte matches AND the player's vehicle / story state satisfy the
// gates. The set of segments sharing that region byte is the equivalent
// trigger-zone for that location; AD steers toward the closest such
// segment. v0.14.94 wires this into StartAutoDrive's targeting; v0.14.93
// only embeds the data and dumps a sanity-check log block at module init.
//
// ENCODING:
//   vehicle operands per the artifact's "Vehicle ID encoding" table:
//     0x80 = Squall foot       (most common operand)
//     0x84 = Selphie foot      (alt-party-leader foot, treated as foot for AD)
//     0x30 = Garden            (mobile B-Garden)
//     0x31 = Chocobo
//     0x32 = Ragnarok
//     0    = no restriction    (used in clauses for top-level-vehicle programs)
//
//   story-flag bounds: gte=0 means "no lower bound", lt=0 means "no upper
//   bound" (treated as +infinity). Both fields refer to the same savemap
//   word that sub_545F10 reads via the 0xFF02/0xFF03 opcodes.
//
//   unk_flags: bit-encoded record of which 0xFF0F/10/11/12/13/20/21 opcodes
//   appeared in the original clause. Operand values themselves are preserved
//   verbatim in the decoded-artifact MD file. v0.14.94+ may decode these by
//   instrumenting sub_545EA0's return path and observing which clauses fire
//   under what conditions; until then, treat any non-zero unk_flags as
//   "this clause may have additional conditions we don't yet model" and
//   prefer simpler clauses for AD steering.
static const uint16_t TRIG_VEH_ANY        = 0x0000;
static const uint16_t TRIG_VEH_FOOT       = 0x0080;
static const uint16_t TRIG_VEH_FOOT_ALT   = 0x0084;
static const uint16_t TRIG_VEH_GARDEN     = 0x0030;
static const uint16_t TRIG_VEH_CHOCOBO    = 0x0031;
static const uint16_t TRIG_VEH_RAGNAROK   = 0x0032;

static const uint16_t TRIG_UNK_0F = 0x0001;
static const uint16_t TRIG_UNK_10 = 0x0002;
static const uint16_t TRIG_UNK_11 = 0x0004;
static const uint16_t TRIG_UNK_12 = 0x0008;
static const uint16_t TRIG_UNK_13 = 0x0010;
static const uint16_t TRIG_UNK_20 = 0x0020;
static const uint16_t TRIG_UNK_21 = 0x0040;

struct TriggerClause {
    uint16_t vehicle;     // TRIG_VEH_* constant; 0 = any (used when top-level vehicle is set)
    uint16_t region;      // segment region-byte to match against Section 2
    uint16_t story_gte;   // savemap-word lower bound (0 = none)
    uint16_t story_lt;    // savemap-word upper bound (0 = +inf)
    uint16_t unk_flags;   // bitmask of UNK opcodes present in original clause (TRIG_UNK_*)
};

struct TriggerProgram {
    uint16_t loc_id;          // wmField/location ID from 0xFF06
    uint16_t top_story_gte;   // top-level story gate lower bound (0 = none)
    uint16_t top_story_lt;    // top-level story gate upper bound (0 = +inf)
    uint16_t top_vehicle;     // top-level vehicle restriction (TRIG_VEH_*; 0 = none)
    uint8_t  num_clauses;     // count of entries in clauses[]
    const TriggerClause* clauses;
};

// 38 per-program clause arrays. The array index matches s_triggerPrograms[]
// index for cross-reference to the decoded artifact (which uses the same
// idx column). s_clNN means "clauses for program NN." When num_clauses == 0
// (program 18, the late-game Chocobo-only mobile destination with top-level
// vehicle restriction and no inner-clause requirements), the pointer is
// nullptr and there is no s_cl18 array.
static const TriggerClause s_cl00[] = {
    { TRIG_VEH_FOOT,    0x14, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x14, 0,    0,    0 },
};
static const TriggerClause s_cl01[] = {
    { TRIG_VEH_FOOT,    0x21, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x21, 0,    0,    0 },
};
static const TriggerClause s_cl02[] = {
    { TRIG_VEH_FOOT,    0x22, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x22, 0,    0,    0 },
};
static const TriggerClause s_cl03[] = {
    { TRIG_VEH_FOOT,    0x13, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x13, 0,    0,    0 },
};
static const TriggerClause s_cl04[] = {
    { TRIG_VEH_FOOT,    0x13, 0,    0,    TRIG_UNK_12 },
    { TRIG_VEH_CHOCOBO, 0x13, 0,    0,    TRIG_UNK_12 },
    { TRIG_VEH_FOOT,    0x23, 0,    0,    TRIG_UNK_10 },
    { TRIG_VEH_CHOCOBO, 0x23, 0,    0,    TRIG_UNK_10 },
};
static const TriggerClause s_cl05[] = {
    { TRIG_VEH_FOOT,    0x24, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x24, 0,    0,    0 },
};
static const TriggerClause s_cl06[] = {
    { TRIG_VEH_FOOT,    0x09, 0,    0,    0 },
};
static const TriggerClause s_cl07[] = {
    { TRIG_VEH_FOOT,     0x03, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x03, 0,    0,    0 },
};
static const TriggerClause s_cl08[] = {
    { TRIG_VEH_FOOT,     0x08, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x08, 0,    0,    0 },
};
static const TriggerClause s_cl09[] = {
    // Heavy UNK flags on r=0x06; cleaner path on r=0x07.
    { TRIG_VEH_FOOT, 0x06, 0,    490,  TRIG_UNK_11 | TRIG_UNK_0F | TRIG_UNK_12 | TRIG_UNK_10 },
    { TRIG_VEH_FOOT, 0x07, 0,    3900, TRIG_UNK_12 },
};
static const TriggerClause s_cl10[] = {
    { TRIG_VEH_FOOT, 0x05, 290,  315,  0 },
};
static const TriggerClause s_cl11[] = {
    { TRIG_VEH_FOOT,     0x01, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x01, 0,    0,    0 },
};
static const TriggerClause s_cl12[] = {
    { TRIG_VEH_FOOT, 0x00, 0,    0,    0 },
};
static const TriggerClause s_cl13[] = {
    { TRIG_VEH_FOOT, 0x02, 0,    0,    TRIG_UNK_11 },
    { TRIG_VEH_FOOT, 0x00, 0,    570,  TRIG_UNK_0F },
};
static const TriggerClause s_cl14[] = {
    { TRIG_VEH_FOOT,    0x16, 0,    0,    TRIG_UNK_0F },
    { TRIG_VEH_CHOCOBO, 0x16, 0,    0,    TRIG_UNK_0F },
    { TRIG_VEH_FOOT,    0x17, 0,    0,    TRIG_UNK_11 },
    { TRIG_VEH_CHOCOBO, 0x17, 0,    0,    TRIG_UNK_11 },
};
static const TriggerClause s_cl15[] = {
    { TRIG_VEH_FOOT,     0x0B, 0,    0,    TRIG_UNK_12 },
    { TRIG_VEH_FOOT_ALT, 0x0B, 0,    0,    TRIG_UNK_12 },
};
static const TriggerClause s_cl16[] = {
    { TRIG_VEH_FOOT,     0x0A, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x0A, 0,    0,    0 },
};
static const TriggerClause s_cl17[] = {
    { TRIG_VEH_FOOT, 0x04, 0,    0,    0 },
};
// program 18 (locID 0x0172, story>=3900, topVeh=Chocobo): no inner clauses
// — the top-level vehicle restriction alone defines the trigger.
static const TriggerClause s_cl19[] = {
    { TRIG_VEH_ANY, 0x0D, 0,    3900, TRIG_UNK_20 },
};
static const TriggerClause s_cl20[] = {
    { TRIG_VEH_ANY, 0x0C, 0,    3900, 0 },
};
static const TriggerClause s_cl21[] = {
    { TRIG_VEH_FOOT, 0x18, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x2E, 3000, 3900, 0 },
};
static const TriggerClause s_cl22[] = {
    { TRIG_VEH_FOOT, 0x19, 0,    0,    0 },
};
static const TriggerClause s_cl23[] = {
    { TRIG_VEH_FOOT, 0x1C, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x39, 3000, 5000, 0 },
};
static const TriggerClause s_cl24[] = {
    { TRIG_VEH_FOOT, 0x0E, 0,    0,    TRIG_UNK_11 },
    { TRIG_VEH_FOOT, 0x0F, 0,    0,    TRIG_UNK_0F },
};
static const TriggerClause s_cl25[] = {
    // Multi-vehicle late-game evolution; most complex program in Section 8.
    { TRIG_VEH_RAGNAROK, 0x1B, 0,    3000, TRIG_UNK_0F | TRIG_UNK_20 },
    { TRIG_VEH_RAGNAROK, 0x44, 3000, 3900, TRIG_UNK_0F | TRIG_UNK_20 },
    { TRIG_VEH_CHOCOBO,  0x44, 3900, 0,    TRIG_UNK_0F },
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, TRIG_UNK_11 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, TRIG_UNK_11 },
};
static const TriggerClause s_cl26[] = {
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, 0 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, 0 },
};
static const TriggerClause s_cl27[] = {
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, 0 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, 0 },
};
static const TriggerClause s_cl28[] = {
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, 0 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, 0 },
};
static const TriggerClause s_cl29[] = {
    { TRIG_VEH_FOOT, 0x1E, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x45, 3000, 0,    0 },
};
static const TriggerClause s_cl30[] = {
    { TRIG_VEH_FOOT, 0x1D, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x2F, 0,    0,    0 },
};
static const TriggerClause s_cl31[] = {
    { TRIG_VEH_FOOT,    0x27, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x27, 0,    0,    0 },
};
static const TriggerClause s_cl32[] = {
    // Esthar-candidate locID 0x01FA: 4 story-windowed regions
    // (region cycles 0x1F -> 0x30 -> 0x31 -> 0x1F across story 0..5000).
    { TRIG_VEH_FOOT, 0x1F, 0,    2500, 0 },
    { TRIG_VEH_FOOT, 0x30, 2500, 3000, 0 },
    { TRIG_VEH_FOOT, 0x31, 3000, 3900, 0 },
    { TRIG_VEH_FOOT, 0x1F, 3900, 5000, 0 },
};
static const TriggerClause s_cl33[] = {
    { TRIG_VEH_FOOT,    0x10, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x10, 0,    0,    0 },
};
static const TriggerClause s_cl34[] = {
    { TRIG_VEH_FOOT,    0x12, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x12, 0,    0,    0 },
};
static const TriggerClause s_cl35[] = {
    { TRIG_VEH_FOOT,    0x26, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x26, 0,    0,    0 },
};
static const TriggerClause s_cl36[] = {
    { TRIG_VEH_FOOT,    0x25, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x25, 0,    0,    0 },
};
static const TriggerClause s_cl37[] = {
    { TRIG_VEH_ANY, 0x15, 0,    0,    TRIG_UNK_20 },
};

// Helper macro keeps the program table compact while letting the compiler
// derive num_clauses from the array's actual length — no risk of the
// hand-written count getting out of sync with the array size.
#define WMS_NCLS(arr)  ((uint8_t)(sizeof(arr) / sizeof(arr[0])))

static const TriggerProgram s_triggerPrograms[] = {
    // [00] locID=0x0031 story>=750 — foot/Choco region 0x14
    { 0x0031,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl00), s_cl00 },
    // [01] locID=0x0051 — foot/Choco region 0x21
    { 0x0051,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl01), s_cl01 },
    // [02] locID=0x0091 — foot/Choco region 0x22
    { 0x0091,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl02), s_cl02 },
    // [03] locID=0x0095 story>=750 — foot/Choco region 0x13
    { 0x0095,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl03), s_cl03 },
    // [04] locID=0x0096 story>=750 — 4 clauses with UNK flags (regions 0x13/0x23)
    { 0x0096,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl04), s_cl04 },
    // [05] locID=0x00DB — foot/Choco region 0x24
    { 0x00DB,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl05), s_cl05 },
    // [06] locID=0x00EA — foot region 0x09
    { 0x00EA,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl06), s_cl06 },
    // [07] locID=0x00EE story>=36 — foot/footAlt region 0x03
    { 0x00EE,   36,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl07), s_cl07 },
    // [08] locID=0x0108 story>=333 — foot/footAlt region 0x08
    { 0x0108,  333,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl08), s_cl08 },
    // [09] locID=0x010B (Balamb Town) — v0.14.98 fix: top_story_gte changed
    //      from 290 to 0. The decoded.md artifact recorded "≥290" at the
    //      top level, but the v0.14.97 PLAN-DEBUG trace exposed an
    //      internal inconsistency: the program's own clauses have story
    //      windows [0..490) for region 0x06 and [0..3900) for region
    //      0x07 — story values BELOW the supposed top-level gate. If
    //      the gate were really top-level, the clauses targeting
    //      pre-290 stories would be unreachable. The disassembler put
    //      0xFF02 0x0122 in the wrong scope; the "≥290" almost
    //      certainly belongs inside clause 0 (giving region 0x06 the
    //      effective window [290, 490) for the disc-2-mid Balamb-during-
    //      Garden-departure phase). Clause 1 (foot, region 0x07,
    //      story [0, 3900)) is the canonical Balamb Town foot-entry
    //      gate, which the engine fires at story 205 (confirmed by the
    //      v0.14.96 BAT log when Aaron entered Balamb Town: fieldName=
    //      'bcgate_1'). With top_story_gte=0, the clause-local windows
    //      keep their integrity — region 0x07 becomes active for the
    //      planner at any story, region 0x06 still opens at story ≥290
    //      via clause 0's own window when properly modeled.
    { 0x010B,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl09), s_cl09 },
    // [10] locID=0x010C — foot region 0x05 [story 290..315]
    { 0x010C,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl10), s_cl10 },
    // [11] locID=0x0111 — foot/footAlt region 0x01
    { 0x0111,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl11), s_cl11 },
    // [12] locID=0x0112 story<570 — foot region 0x00
    { 0x0112,    0,  570, TRIG_VEH_ANY,      WMS_NCLS(s_cl12), s_cl12 },
    // [13] locID=0x0113 — foot region 0x02 (UNK_11) OR foot region 0x00 [<570] (UNK_0F)
    { 0x0113,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl13), s_cl13 },
    // [14] locID=0x0117 — 4 clauses regions 0x16/0x17 with UNK flags
    { 0x0117,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl14), s_cl14 },
    // [15] locID=0x0147 story 350..490 — foot/footAlt region 0x0B (UNK_12)
    { 0x0147,  350,  490, TRIG_VEH_ANY,      WMS_NCLS(s_cl15), s_cl15 },
    // [16] locID=0x0169 story>=350 — foot/footAlt region 0x0A
    { 0x0169,  350,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl16), s_cl16 },
    // [17] locID=0x016D story>=205 — foot region 0x04
    { 0x016D,  205,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl17), s_cl17 },
    // [18] locID=0x0172 story>=3900 topVeh=Chocobo — NO inner clauses (mobile destination)
    { 0x0172, 3900,    0, TRIG_VEH_CHOCOBO,  0,                 nullptr },
    // [19] locID=0x0172 story>=636 topVeh=Ragnarok — region 0x0D [<3900] +UNK_20
    { 0x0172,  636,    0, TRIG_VEH_RAGNAROK, WMS_NCLS(s_cl19), s_cl19 },
    // [20] locID=0x0172 story>=636 topVeh=Garden — region 0x0C [<3900]
    { 0x0172,  636,    0, TRIG_VEH_GARDEN,   WMS_NCLS(s_cl20), s_cl20 },
    // [21] locID=0x0175 story>=1600 — foot region 0x18 [<3000] OR 0x2E [3000..3900]
    { 0x0175, 1600,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl21), s_cl21 },
    // [22] locID=0x0176 — foot region 0x19
    { 0x0176,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl22), s_cl22 },
    // [23] locID=0x017A story>=1750 — foot region 0x1C [<3000] OR 0x39 [3000..5000]
    { 0x017A, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl23), s_cl23 },
    // [24] locID=0x0189 story>=750 — foot regions 0x0E (UNK_11) / 0x0F (UNK_0F)
    { 0x0189,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl24), s_cl24 },
    // [25] locID=0x0196 story>=1750 — 5 clauses, multi-vehicle late-game evolution
    { 0x0196, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl25), s_cl25 },
    // [26] locID=0x0197 story>=1750 — foot/footAlt region 0x1A [<3900]
    { 0x0197, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl26), s_cl26 },
    // [27] locID=0x01B6 story>=1750 — foot/footAlt region 0x1A [<3900]
    { 0x01B6, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl27), s_cl27 },
    // [28] locID=0x01B7 story>=1750 — foot/footAlt region 0x1A [<3900]
    { 0x01B7, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl28), s_cl28 },
    // [29] locID=0x01B9 story>=1750 — foot region 0x1E [<3000] OR 0x45 [3000..]
    { 0x01B9, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl29), s_cl29 },
    // [30] locID=0x01BB story>=1750 — foot region 0x1D [<3000] OR 0x2F
    { 0x01BB, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl30), s_cl30 },
    // [31] locID=0x01D2 — foot/Choco region 0x27
    { 0x01D2,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl31), s_cl31 },
    // [32] locID=0x01FA story>=1750 — 4 story-windowed regions (Esthar candidate)
    { 0x01FA, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl32), s_cl32 },
    // [33] locID=0x0250 — foot/Choco region 0x10
    { 0x0250,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl33), s_cl33 },
    // [34] locID=0x028C story>=900 — foot/Choco region 0x12
    { 0x028C,  900,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl34), s_cl34 },
    // [35] locID=0x028D — foot/Choco region 0x26
    { 0x028D,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl35), s_cl35 },
    // [36] locID=0x02B5 — foot/Choco region 0x25
    { 0x02B5,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl36), s_cl36 },
    // [37] locID=0x02C1 topVeh=Ragnarok — region 0x15 (UNK_20)
    { 0x02C1,    0,    0, TRIG_VEH_RAGNAROK, WMS_NCLS(s_cl37), s_cl37 },
};
static const int TRIGGER_PROGRAM_COUNT = sizeof(s_triggerPrograms) / sizeof(s_triggerPrograms[0]);

// Vehicle classification used by the BFS reachability rules.
// Locomotion enum values per `Plan & Research Documents/World Map Terrain
// and Locomotion Reference.md`:
//   0  = Squall on foot       6  = Selphie on foot
//   3  = Ship (BAT-validated v0.14.83)
//   31 = Chocobo             32-40 = Cars
//   48 = Garden (mobile)     50 = Ragnarok
// Mode 4 is NOT in the canonical list. Earlier builds (v0.14.83-v0.14.85.2)
// tagged it 'Ragnarok' based on a v0.14.82 BAT log where Claude assumed the
// 'Ragnarok session' label, but Aaron's v0.14.85.2 BAT clarified he doesn't
// have Ragnarok in the current save — mode 4 was a transient byte read at
// a Fire Cavern field-transition moment, not a vehicle. v0.14.85.3 drops
// the assumption: mode 4 (and any other non-canonical value) defaults to
// VEH_ON_FOOT for BFS purposes and is silently ignored by CheckVehicleChange.
enum VehicleType {
    VEH_ON_FOOT,
    VEH_CHOCOBO,
    VEH_CAR,
    VEH_GARDEN,
    VEH_RAGNAROK
};

// ============================================================================
// Location catalog — v0.14.85.1: rebuilt from canonical research doc
// ============================================================================
// Source: `Plan & Research Documents/World Map Location Coordinates Research
// Findings.md`. The doc enumerates the 26 numbered world-map markers from
// FinalFantasyKingdom + 7 chocobo forests + 4 alien encounters with X/Y/Z
// coords from the ff8-speedruns/ff8-memory dataset — the SAME coordinate
// system the player position address (`FF8_EN.exe+1C3EE80`) reports at
// runtime, so distances and BFS segment-mapping work without conversion.
// Plus Fire Cavern from the v0.11.11 wmx.obj analysis (it was missing from
// the canonical 26).
//
// v0.14.85 BAT exposed two flaws in the prior catalog: (a) it used a
// completely different coordinate system (positive-Y, magnitudes mismatched
// with the runtime player-position address by ~50000 units) so BFS
// reachability was effectively random, and (b) it included interior /
// event-only locations like 'Timber Maniacs Building', 'SeeD Graduation
// Ball', 'SeeD on Train', 'Balamb Garden MD Level', and 'Dr. Odines Lab'
// that aren't world-map entry points at all.
//
// This rebuild matches the canonical FF8 world-map entry-point set: every
// location here is a place you can walk/drive/fly TO from the overworld
// (not an interior accessed from another field).
// LocationEntry holds the catalog center coordinate — a stable Ragnarok-
// autopilot coordinate from `Plan & Research Documents/World Map Location
// Coordinates Research Findings.md`. v0.14.89 added EMPIRICAL entry-coord
// refinement in a parallel `s_refinedEntries[]` table (see below) that
// indexes by LOCATION_COUNT — not stored on this struct because it lives
// in const data. The s_refinedEntries table is mutable and is what gets
// updated when an auto-drive completes.
struct LocationEntry {
    const char* name;
    int32_t x;
    int32_t y;
};

static const LocationEntry s_locations[] = {
    // Numbered markers 1-26 (canonical FinalFantasyKingdom set)
    {"Balamb Garden",              24576,  -29406},
    {"Balamb Town",                13249,  -26779},   // canonical name 'Balamb'; kept 'Town' for clarity vs Garden
    {"Dollet",                    -15639,  -39437},
    {"Timber",                    -22564,   -4867},
    {"Galbadia Garden",           -37471,  -25062},
    {"Deling City",               -61806,  -28649},
    {"Tomb of the Unknown King",  -42471,  -36562},
    {"D-District Prison",         -55306,   -4841},
    {"Galbadia Missile Base",     -71695,  -15591},
    {"Fisherman's Horizon",        48811,   -1653},
    {"Trabia Garden",              48893,  -57979},
    {"Edea's House",              -23150,   62853},
    {"White SeeD Ship",             4887,   51285},
    {"Great Salt Lake",            49888,   -2683},
    {"Esthar City",                57011,   -2295},   // canonical 'Esthar'; kept 'City' for clarity
    {"Lunatic Pandora Lab",        79521,   -9135},
    {"Lunar Gate",                 88021,    7865},
    {"Sorceress Memorial",         81521,   11865},
    {"Shumi Village",              10362,  -76967},
    {"Winhill",                   -50285,    6320},
    {"Centra Ruins",                6887,   55285},
    {"Deep Sea Research Center", -119138,   86000},
    {"Cactuar Island",             54806,   62040},
    {"Tears' Point",               83021,   31865},
    {"Island Closest to Hell",   -105137,   -3802},
    {"Island Closest to Heaven",  102251,  -53082},

    // Chocobo Forests (7) — generic numbering; the in-game forests don't have
    // canon names. Geographic hints in the comments help during testing.
    {"Chocobo Forest 1",           11332,  -63659},   // Trabia Continent (south of Trabia Garden)
    {"Chocobo Forest 2",           10927,  -81010},   // Trabia Continent (north)
    {"Chocobo Forest 3",           51893,   -3959},   // Esthar coast / FH region
    {"Chocobo Forest 4",           97253,  -48250},   // Far East / Esthar mountains
    {"Chocobo Forest 5",           17383,   22013},   // South Galbadia
    {"Chocobo Forest 6",           44504,   76259},   // Centra Continent
    {"Chocobo Forest 7",          -20953,   68906},   // Centra / Edea's House region

    // Alien Encounter (UFO/PuPu) sites (4)
    {"Alien Ship 1",               79823,  -61212},   // Esthar / Trabia border
    {"Alien Ship 2",               40495,   54649},   // Centra Continent
    {"Alien Ship 3",              -12952,  -10202},   // Galbadia Continent
    {"Alien Ship 4",              -48806,    5808},   // West Galbadia

    // Fire Cavern — missing from the canonical 26 marker list because the
    // FinalFantasyKingdom set is Ragnarok-era; Fire Cavern is the early-game
    // dungeon on Balamb Island. Coords from the v0.11.11 wmx.obj polygon
    // analysis (segment(20,20), 6 terrain-29 polygons, small cave entrance).
    {"Fire Cavern",                36864,  -28672}
};
static const int LOCATION_COUNT = sizeof(s_locations) / sizeof(s_locations[0]);

// ============================================================================
// Refined entry coordinates table (v0.14.89, Option B — empirical capture)
// ============================================================================
// Parallel to s_locations[]. When a successful on-foot auto-drive ends with
// the world map exiting to MODE_FIELD inside the DRIVE_ARRIVED_ON_EXIT_DIST
// proximity to the catalog target, the field-entry handler captures the
// player's last-known world-map (X, Y) into this table at the same index.
// On subsequent drives to the same destination, StartAutoDrive prefers the
// refined coord over the catalog center for steering — making the second
// visit to a narrow-entrance location (e.g. Balamb Town, Dollet) direct,
// no sweep required.
//
// PERSISTENCE: this build keeps the table in-memory only. Refined coords
// are lost on game restart. Persistent storage (a JSON file alongside
// DEVNOTES, or in %APPDATA%) is queued for v0.14.90 alongside the
// already-deferred 'persistent accessibility settings' priority.
//
// CORRECTNESS: refined coords reflect WHERE THE PLAYER WAS at the moment
// the trigger fired — i.e. the actual on-the-ground entry point. Not the
// trigger-zone center, not the catalog center. Distance from this coord
// to the autopilot center is typically 300-1500 units (matches the prior
// deep research's 'triggers offset by ~300-800 units' estimate).
static int32_t s_refinedX[LOCATION_COUNT];
static int32_t s_refinedY[LOCATION_COUNT];
static bool    s_refinedHas[LOCATION_COUNT];

// Find the s_locations[] index of a location by its catalog center coords.
// The drive uses (s_driveTargetX, s_driveTargetY) which were captured from
// s_catalog[catIdx].(x, y) in StartAutoDrive — those values come from
// s_locations[] (or from the refined table if has_refined was true), so
// we need to search both sources to identify which s_locations[] slot a
// given target coord belongs to. Returns -1 if no match.
static int FindLocationIndexByTargetCoords(int32_t tx, int32_t ty)
{
    for (int i = 0; i < LOCATION_COUNT; i++) {
        if (s_locations[i].x == tx && s_locations[i].y == ty) return i;
        if (s_refinedHas[i] && s_refinedX[i] == tx && s_refinedY[i] == ty) return i;
    }
    return -1;
}

// ============================================================================
// Navigation state
// ============================================================================
static struct LocationEntry s_catalog[LOCATION_COUNT];  // distance-sorted working copy
static int  s_catalogCount = 0;        // v0.14.85: post-filter count (≤ LOCATION_COUNT)
static bool s_catalogBuilt = false;
static int s_catalogIndex = 0;       // current selected location (0 = nearest)
static int s_lastVehicle = -1;       // last known vehicle state
static bool s_onWorldMap = false;    // true when on world map
static DWORD s_lastMovementTick = 0; // last time position changed significantly

// ============================================================================
// Auto-drive state (v0.14.86, restored from v0.11.08-v0.11.10)
// ============================================================================
// World map auto-drive uses keyboard injection rather than the field-nav
// fake gamepad. Discovered v0.11.05-v0.11.07: the world map's input handler
// `worldmap_input_update_sub_559240` has its own input pipeline separate
// from the field's `engine_eval_keyboard_gamepad_input`, so the fake gamepad
// signal never reaches it. `keybd_event` injection at the OS level reaches
// both pipelines and works reliably on the world map. v0.11.08 BAT validated.
//
// Steering uses heading-relative bearing (0-4095 native units; 0=North CW)
// computed each tick. Three behaviors based on relative bearing magnitude:
//   ahead   (within ~18°)  : just walk forward
//   diag    (~18-45°)      : turn AND walk forward (saves time vs. turn-then-walk)
//   side    (>45°)         : turn only until aligned, then forward
// Final approach (<DRIVE_ARRIVE_DIST*2 ≈ 1000 units): just walk forward —
// most catalog coordinates are not exactly on entrance triggers, and walking
// straight through the area sweeps reliably onto the trigger zone.
//
// State target is captured by VALUE (X/Y/name) at StartAutoDrive time, not
// as a catalog index. The catalog rebuilds whenever the player crosses a
// BFS rule-class boundary, which would invalidate any stored index. Stable
// X/Y/name lets the drive survive arbitrary catalog rebuilds.
static const double DRIVE_ARRIVE_DIST           = 600.0;   // vehicle arrival proximity (on-foot uses world-map-exit detection)
static const double DRIVE_APPROACH_DIST         = 3000.0;  // one-shot "Approaching X" threshold
static const double DRIVE_FINAL_APPROACH_DIST   = 1000.0;  // walk-forward (no steering) below this
static const double DRIVE_ARRIVED_ON_EXIT_DIST  = 1500.0;  // v0.14.87: world-map exit while closer than this counts as arrival
static const DWORD  DRIVE_ANNOUNCE_INTERVAL_MS  = 5000;    // periodic distance announce
static const DWORD  DRIVE_STUCK_CHECK_INTERVAL_MS = 3000;  // stuck-detection sample window
static const double DRIVE_STUCK_THRESHOLD       = 100.0;   // movement floor in one window
static const int    DRIVE_STUCK_MAX             = 6;       // 6 windows × 3s = 18s no movement → give up

// v0.14.87 — sweep search constants. Activated when on-foot drive is stuck
// inside the final-approach zone (target within 1000 units but no entrance
// trigger has fired). Past chats v0.11.10 validated 6-phase alternating
// turn-then-walk as effective for narrow entrances like Balamb Town.
static const int    SWEEP_MAX_PHASES        = 6;     // give up after 6 attempts
static const DWORD  SWEEP_TURN_BASE_MS      = 800;   // phase 1 turn duration; +200ms per phase
static const DWORD  SWEEP_WALK_DURATION_MS  = 3000;  // walk forward 3s per phase
static const DWORD  FINAL_APPROACH_TIMEOUT_MS = 6000;// in final approach >6s without exit → sweep

static bool     s_driveActive            = false;
static int32_t  s_driveTargetX           = 0;
static int32_t  s_driveTargetY           = 0;
static char     s_driveTargetName[64]    = {};
static DWORD    s_driveStartTime         = 0;
static DWORD    s_driveLastAnnounce      = 0;
static double   s_driveLastDist          = 0.0;
static int32_t  s_driveLastPosX          = 0;     // v0.14.89: last known player X (for refined-entry capture on MODE_FIELD)
static int32_t  s_driveLastPosY          = 0;
static int32_t  s_driveStuckX            = 0;
static int32_t  s_driveStuckY            = 0;
static DWORD    s_driveStuckCheckTime    = 0;
static int      s_driveStuckCount        = 0;
static bool     s_driveApproachAnnounced = false;  // one-shot guard
static bool     s_driveOnFootAtStart     = true;   // v0.14.87: captured at StartAutoDrive; arrival semantics differ
static DWORD    s_finalApproachEnterTick = 0;      // v0.14.87: when player crossed below FINAL_APPROACH_DIST (0 = not yet)

// v0.14.96: deferred arrival decision state. v0.14.95's instant-decision
// approach (segment-membership when planner active, distance fallback
// otherwise) gave false-positive arrivals when random encounters fired near
// a destination — both signals say 'yes' because the player IS near the
// target, but the actual game state is a battle, not a field entry. The
// v0.14.95 BAT log showed two clear cases: 12:43:36 declared arrival at
// Balamb Garden lastPos=(25405,-30324) dist=1215 (encounter ~1100 units
// south of the refined entry, NOT arrival); 12:51:26 same pattern for
// Balamb Town. Aaron's diagnosis (and the cure): use the game's settled
// game mode AFTER the world-map exit to disambiguate. The v0.14.90.2
// changelog noted reading pGameMode AT the moment of exit always reads
// MODE_WORLDMAP because the register hasn't transitioned yet — but reading
// it after a brief wait is robust. Decision table:
//   MODE_FIELD (1)        : entered a field — REAL ARRIVAL
//   MODE_SWIRL (3)        : pre-battle swirl — ENCOUNTER (paused)
//   MODE_BATTLE (999)     : in battle — ENCOUNTER (paused)
//   MODE_AFTER_BATTLE (4) : post-battle return — ENCOUNTER (paused)
//   anything else (incl. lingering MODE_WORLDMAP=2) : keep waiting
// Wait timeout: ARRIVAL_DECISION_TIMEOUT_MS. On timeout, fall back to the
// v0.14.95 segment-membership / distance heuristic as safety net.
//
// While awaiting decision: drive keys are released (no arrow-key injection
// during field/battle), s_driveActive stays true (so cancel works), Poll()
// runs ResolveDeferredArrival() each tick before its !s_onWorldMap early
// return so the decision can resolve.
static const DWORD ARRIVAL_DECISION_TIMEOUT_MS = 2000;
static bool  s_driveAwaitingArrivalDecision = false;
static DWORD s_driveExitTick                = 0;

// v0.14.87 sweep search state. Sweep alternates between TURNING and WALKING
// sub-states. Each phase: turn for SWEEP_TURN_BASE_MS + (phase-1)*200ms,
// then walk forward SWEEP_WALK_DURATION_MS. Odd phases turn right, even left.
// Field exit during any sub-state → arrival; sweep exhaustion → give up.
static bool     s_sweepActive  = false;
static int      s_sweepPhase   = 0;     // 1..SWEEP_MAX_PHASES (0 = not in sweep)
static bool     s_sweepTurning = true;  // sub-state: true = turn, false = walk
static DWORD    s_sweepStateEnd = 0;    // tick when current sub-state ends

// Held-key tracking for keybd_event injection. Press/release tracking lets
// SetDriveKeys() be idempotent — pressing UP twice doesn't double-press.
static bool s_keyUpHeld    = false;
static bool s_keyLeftHeld  = false;
static bool s_keyRightHeld = false;

// ============================================================================
// Terrain grid + BFS reachability state (v0.14.85)
// ============================================================================
// s_terrainGrid[row][col]: 0 = LAND, 1 = OCEAN. Loaded once at module init from
// wmx.obj inside world.fs. s_reachable[row][col] is rebuilt per catalog build
// via BFS flood-fill from the player's current segment.
static uint8_t s_terrainGrid[WMX_SEG_ROWS][WMX_SEG_COLS];
static uint8_t s_reachable  [WMX_SEG_ROWS][WMX_SEG_COLS];
static bool    s_terrainLoaded = false;

// ============================================================================
// Segment-region byte map (v0.14.94)
// ============================================================================
// 32x24 byte map from wmsetus.obj Section 2. Each byte is the region ID for
// one segment; the 0xFF08 region operands in s_triggerPrograms[]'s clauses
// reference these bytes. Loaded once at module init by LoadTriggerZones
// (which already reads the archive for diagnostic dumping — we extract
// Section 2's bytes into s_segmentRegionMap[] in the same pass to avoid
// duplicate I/O). Index as s_segmentRegionMap[row][col] where row=0..23 and
// col=0..31. The byte at file offset (row*32 + col) is the region for that
// segment; 0xFF means 'no region' (typically deep ocean cells); non-FF bytes
// are region IDs in the 0x00..0x45 range observed in the v0.14.93 BAT dump.
// (The 4-byte trailer at file offset 768, '00 00 00 00', is a section
// terminator and is not part of the data.)
//
// AD path planner uses this to construct goal sets: for a catalog target
// (X, Y), find the matching s_triggerPrograms[] entry, collect every cell
// in s_segmentRegionMap whose byte equals any matching clause's region
// operand — that's the equivalent trigger zone. Multi-target A* then runs
// from the player's current segment to the closest goal cell.
static uint8_t s_segmentRegionMap[WMX_SEG_ROWS][WMX_SEG_COLS];
static bool    s_segmentRegionLoaded = false;

// ============================================================================
// Coordinate utility functions
// ============================================================================
static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z)
{
    __try {
        *x = *(int32_t*)WM_POS_X;
        *y = *(int32_t*)WM_POS_Y;
        *z = *(int32_t*)WM_POS_Z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *x = *y = *z = 0;
    }
}

static uint16_t GetWorldMapHeading()
{
    uint16_t heading = 0;
    __try {
        heading = *(uint16_t*)WM_HEADING;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        heading = 0;
    }
    return heading;
}

static uint8_t GetLocomotionMode()
{
    uint8_t mode = 0;
    __try {
        mode = *(uint8_t*)WM_LOCOMOTION;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode = 0;
    }
    return mode;
}

// v0.14.94: read the savemap story-flag word at WM_STORY_FLAG. Used by the
// AD path planner's clause-evaluation logic to filter which s_triggerPrograms[]
// entries are currently satisfiable (each clause carries an optional
// [story_gte, story_lt) window; the program is gated only when the live
// story value falls inside that window). Returning 0 on access fault is safe
// — a story value of 0 satisfies any 'no lower bound' gate (story_gte=0)
// and fails any 'must be >=N' gate, which is the correct early-game behavior.
static uint16_t GetCurrentStoryFlag()
{
    uint16_t story = 0;
    __try {
        story = *(uint16_t*)WM_STORY_FLAG;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        story = 0;
    }
    return story;
}

static bool IsOnWorldMap()
{
    uint16_t scene = 1;  // default to field (not worldmap)
    __try {
        scene = *(uint16_t*)WM_SCENE_FLAG;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        scene = 1;
    }
    return (scene == 0);
}

// Wrap-aware distance calculation on a torus
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = abs(x2 - x1);
    double dy = abs(y2 - y1);
    
    // Check if wrapping gives shorter distance
    if (dx > WM_WIDTH / 2)  dx = WM_WIDTH - dx;
    if (dy > WM_HEIGHT / 2) dy = WM_HEIGHT - dy;
    
    return sqrt(dx * dx + dy * dy);
}

// ============================================================================
// File I/O helpers (v0.14.85, restored from v0.11.12 impl)
// ============================================================================
// Mirrors the field_archive.cpp pattern but uses raw uint8_t* with malloc/free
// rather than std::vector to keep the world_map module independent of
// field_archive's internals. The duplicated LZSS function below has the same
// rationale; both could be lifted to a shared header in a future refactor.
static bool WM_ReadFileToBuffer(const char* path, uint8_t** outData, uint32_t* outSize)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    *outData = (uint8_t*)malloc((size_t)sz);
    if (!*outData) { fclose(f); return false; }
    size_t rd = fread(*outData, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(*outData); *outData = nullptr; return false; }
    *outSize = (uint32_t)sz;
    return true;
}

static bool WM_ReadFileChunk(const char* path, uint32_t offset, uint32_t size, uint8_t** outData)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return false; }
    *outData = (uint8_t*)malloc(size);
    if (!*outData) { fclose(f); return false; }
    size_t rd = fread(*outData, 1, size, f);
    fclose(f);
    if (rd != size) { free(*outData); *outData = nullptr; return false; }
    return true;
}

// ============================================================================
// LZSS decompression (v0.14.85, restored from v0.11.12 impl)
// ============================================================================
// Standard FF8/FF7 LZSS variant. 4096-byte ring buffer, ringPos starts at
// 0xFEE. Flag byte's bits read LSB-first: 1=literal byte, 0=back-reference
// (12-bit offset, 4-bit length+3). Identical to field_archive.cpp's
// DecompressLZSS but file-private here for module independence.
static bool WM_DecompressLZSS(const uint8_t* input, uint32_t inputSize,
                              uint8_t* output, uint32_t outputSize)
{
    uint8_t ring[4096];
    memset(ring, 0, sizeof(ring));
    int ringPos = 0xFEE;
    uint32_t inPos = 0, outPos = 0;

    while (outPos < outputSize && inPos < inputSize) {
        uint8_t flags = input[inPos++];
        for (int bit = 0; bit < 8 && outPos < outputSize; bit++) {
            if (flags & (1 << bit)) {
                if (inPos >= inputSize) return false;
                uint8_t b = input[inPos++];
                output[outPos++] = b;
                ring[ringPos] = b;
                ringPos = (ringPos + 1) & 0xFFF;
            } else {
                if (inPos + 1 >= inputSize) return false;
                uint8_t b1 = input[inPos++];
                uint8_t b2 = input[inPos++];
                int off = b1 | ((b2 & 0xF0) << 4);
                int len = (b2 & 0x0F) + 3;
                for (int i = 0; i < len && outPos < outputSize; i++) {
                    uint8_t b = ring[(off + i) & 0xFFF];
                    output[outPos++] = b;
                    ring[ringPos] = b;
                    ringPos = (ringPos + 1) & 0xFFF;
                }
            }
        }
    }
    return (outPos == outputSize);
}

// ============================================================================
// Coordinate conversion: game world coords → segment grid (v0.14.85)
// ============================================================================
// World map torus is 262144 x 196608, divided into a 32 x 24 grid of 8192-unit
// segments. The X axis has a non-zero origin offset: per wmx.obj analysis,
// game_X = seg_col * 8192 + 4096 - 131072. The +131072 below is the inverse
// of that offset — without it, BFS starts in an ocean cell and filters
// everything as unreachable. This was the v0.11.16 fix that finally made
// terrain BFS work end-to-end ("Driving worked as expected!" — Aaron's BAT).
// Y axis aligns naturally because the torus wrap absorbs any constant offset.
static int WorldXToSegCol(int32_t x)
{
    int32_t shifted = x + 131072;
    int32_t nx = ((shifted % 262144) + 262144) % 262144;
    return (nx / 8192) % WMX_SEG_COLS;
}

static int WorldYToSegRow(int32_t y)
{
    int32_t ny = ((y % 196608) + 196608) % 196608;
    return (ny / 8192) % WMX_SEG_ROWS;
}

// ============================================================================
// Auto-drive helpers (v0.14.86)
// ============================================================================
// Bearing in native FF8 heading units (0-4095, 0=North, CW). Wrap-aware on
// the world torus. Mirrors the math in AnnounceBearing but returns the raw
// angle for the steering decision; AnnounceBearing converts to compass
// directions for speech.
static int TorusBearing(int32_t fromX, int32_t fromY, int32_t toX, int32_t toY)
{
    int32_t dx = toX - fromX;
    int32_t dy = toY - fromY;
    if (abs(dx) > (int32_t)WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else        dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > (int32_t)WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else        dy += (int32_t)WM_HEIGHT;
    }
    // -dy because FF8 Y axis increases downward; atan2(dx, -dy) gives
    // angle from +Y (North) clockwise, matching the heading convention.
    double radians = atan2((double)dx, -(double)dy);
    if (radians < 0) radians += 2.0 * 3.14159265358979;
    int bearing = (int)(radians / (2.0 * 3.14159265358979) * 4096.0);
    return bearing & 0xFFF;  // wrap to 0-4095
}

// keybd_event-based key injection. Arrow keys use scan codes 0x48 (UP),
// 0x4B (LEFT), 0x4D (RIGHT), all extended-key scancodes (the high bit of
// the keyboard scancode set). KEYEVENTF_EXTENDEDKEY is required so the OS
// (and the game's input handler) treats these as the cursor arrows rather
// than the numpad equivalents.
static void PressKey(BYTE vk, BYTE scan)
{
    keybd_event(vk, scan, KEYEVENTF_EXTENDEDKEY, 0);
}

static void ReleaseKey(BYTE vk, BYTE scan)
{
    keybd_event(vk, scan, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
}

static void ReleaseAllDriveKeys()
{
    if (s_keyUpHeld)    { ReleaseKey(VK_UP,    0x48); s_keyUpHeld    = false; }
    if (s_keyLeftHeld)  { ReleaseKey(VK_LEFT,  0x4B); s_keyLeftHeld  = false; }
    if (s_keyRightHeld) { ReleaseKey(VK_RIGHT, 0x4D); s_keyRightHeld = false; }
}

// Idempotent press/release: only generates events on state changes. Called
// every Update() tick from UpdateAutoDrive with the desired key state for
// the next frame; the game sees a continuous press as long as the same key
// is held across calls.
static void SetDriveKeys(bool up, bool left, bool right)
{
    if (up    && !s_keyUpHeld)    { PressKey(VK_UP,    0x48); s_keyUpHeld    = true; }
    if (!up   &&  s_keyUpHeld)    { ReleaseKey(VK_UP,  0x48); s_keyUpHeld    = false; }
    if (left  && !s_keyLeftHeld)  { PressKey(VK_LEFT,  0x4B); s_keyLeftHeld  = true; }
    if (!left &&  s_keyLeftHeld)  { ReleaseKey(VK_LEFT, 0x4B); s_keyLeftHeld = false; }
    if (right && !s_keyRightHeld) { PressKey(VK_RIGHT, 0x4D); s_keyRightHeld = true; }
    if (!right&&  s_keyRightHeld) { ReleaseKey(VK_RIGHT,0x4D); s_keyRightHeld = false; }
}

// ============================================================================
// Vehicle classification (v0.14.85.3)
// ============================================================================
// Maps the raw locomotion byte to a coarse VehicleType used by reachability
// rules. Conservative: only modes from the canonical list get non-foot
// classifications. Unknown modes (including the transient mode 4 seen at
// field-transition moments) default to VEH_ON_FOOT — safest for filtering
// because the player IS likely on foot if they aren't in a known vehicle.
static VehicleType GetVehicleType(uint8_t mode)
{
    if (mode == 0 || mode == 6) return VEH_ON_FOOT;       // Squall / Selphie foot
    if (mode == 3)               return VEH_GARDEN;       // Ship: ocean access, BAT-validated v0.14.83
    if (mode == 31)              return VEH_CHOCOBO;
    if (mode >= 32 && mode <= 40) return VEH_CAR;
    if (mode == 48)              return VEH_GARDEN;       // Garden mobile (ocean access)
    if (mode == 50)              return VEH_RAGNAROK;     // No filter (flies anywhere)
    return VEH_ON_FOOT;                                    // safe default for unknown / transient values
}

// Three BFS rule classes used by the v0.14.85.3 type-change-triggered rebuild:
// 0 = land-only (foot, chocobo, car), 1 = ocean-allowed (Ship, Garden),
// 2 = no-filter (Ragnarok). A rebuild only fires when the rule class
// changes, so foot ↔ car ↔ chocobo transitions (all land-only) don't trigger
// gratuitous rebuilds.
static int GetBfsRuleClass(VehicleType v)
{
    if (v == VEH_RAGNAROK) return 2;
    if (v == VEH_GARDEN)   return 1;
    return 0;  // VEH_ON_FOOT, VEH_CHOCOBO, VEH_CAR all share land-only rules
}

// Whitelist of canonical locomotion-byte values. The byte at WM_LOCOMOTION
// drifts through transient values (animation phase counters, field-transition
// state, etc.) and announcing those would be noise. Only canonical values per
// the research doc are eligible for vehicle-change announcements.
static bool IsCanonicalLocomotion(uint8_t mode)
{
    return mode == 0 || mode == 3 || mode == 6 ||
           mode == 31 ||
           (mode >= 32 && mode <= 40) ||
           mode == 48 || mode == 50;
}

// True iff the segment is reachable for the given vehicle. For the binary
// land/ocean grid we have, foot/chocobo/car require land, Garden allows ocean
// too (and Ragnarok callers should skip the BFS entirely — it can fly
// anywhere, so calling this is moot).
static bool IsSegmentTraversable(int row, int col, VehicleType veh)
{
    if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) return false;
    uint8_t cell = s_terrainGrid[row][col];   // 0 = land, 1 = ocean
    if (veh == VEH_GARDEN || veh == VEH_RAGNAROK) return true;   // any segment
    return (cell == 0);                       // land-only for foot/chocobo/car
}

// ============================================================================
// LoadTerrainGrid — reads wmx.obj from world.fs once at module init,
// classifies each of 768 playable segments as LAND or OCEAN by polygon
// terrain types. Restored from v0.11.12 impl.
// ============================================================================
static bool LoadTerrainGrid()
{
    if (s_terrainLoaded) return true;

    // Auto-detect game install path via FF8_EN.exe location.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // ---- Read world.fi (small index file, 12 bytes per entry).
    uint8_t* fiData = nullptr;
    uint32_t fiSize = 0;
    if (!WM_ReadFileToBuffer(fiPath, &fiData, &fiSize)) {
        Log::World("WorldMap: [TERRAIN] Failed to read world.fi at '%s'", fiPath);
        return false;
    }
    if (fiSize < (uint32_t)(WMX_FL_INDEX + 1) * 12) {
        Log::World("WorldMap: [TERRAIN] world.fi too small (%u bytes)", fiSize);
        free(fiData);
        return false;
    }

    // ---- Parse wmx.obj's FI entry (index 9).
    const uint8_t* fiEntry = fiData + WMX_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);

    Log::World("WorldMap: [TERRAIN] wmx.obj FI entry: uncomp=%u offset=%u comp=%u",
               uncompSize, fsOffset, compression);

    // Validate expected size: 835 segments x 36864 = 30,801,540.
    uint32_t expectedSize = (uint32_t)835 * WMX_SEGMENT_SIZE;
    if (uncompSize != expectedSize) {
        Log::World("WorldMap: [TERRAIN] WARNING: wmx.obj size %u != expected %u",
                   uncompSize, expectedSize);
    }

    // ---- Read wmx.obj from world.fs (compressed or raw per FI compression flag).
    uint8_t* wmxData = nullptr;
    if (compression == 0) {
        free(fiData);
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmxData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read wmx.obj from world.fs");
            return false;
        }
    } else {
        // Compressed: derive compressed-size by reading the next FI entry's offset.
        uint32_t compSize = uncompSize; // fallback
        for (int j = WMX_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read compressed wmx.obj");
            return false;
        }
        // FF8 LZSS storage: 4-byte uncompressed-size header precedes the bitstream.
        wmxData = (uint8_t*)malloc(uncompSize);
        if (!wmxData) { free(compData); return false; }
        bool ok = WM_DecompressLZSS(compData + 4, compSize - 4, wmxData, uncompSize);
        free(compData);
        if (!ok) {
            Log::World("WorldMap: [TERRAIN] LZSS decompression failed");
            free(wmxData);
            return false;
        }
    }

    Log::World("WorldMap: [TERRAIN] wmx.obj loaded (%u bytes), classifying %d segments...",
               uncompSize, WMX_PLAYABLE_SEGS);

    // ---- Classify each of the 768 playable segments by walking its proper
    // 68-byte header + 16 block headers + per-block polygon arrays.
    // v0.14.85.1: rewritten from the v0.14.85 flat-stride bug that read
    // 2304 "polygons" per segment and saw 0 oceans because the garbage
    // bytes between block boundaries rarely landed in 32-34.
    memset(s_terrainGrid, 0, sizeof(s_terrainGrid));
    int oceanSegs = 0, landSegs = 0;
    int totalRealPolys = 0, totalOceanPolys = 0;

    for (int seg = 0; seg < WMX_PLAYABLE_SEGS; seg++) {
        int row = seg / WMX_SEG_COLS;
        int col = seg % WMX_SEG_COLS;
        const uint8_t* segData = wmxData + (uint32_t)seg * WMX_SEGMENT_SIZE;

        int segPolyCount = 0, segOceanCount = 0;

        // Walk the 16 block offsets in the segment header. Skip the first 4
        // bytes (group_id), then read each uint32 little-endian offset.
        for (int b = 0; b < WMX_BLOCKS_PER_SEG; b++) {
            uint32_t blockOffset = *(const uint32_t*)(segData + 4 + b * 4);
            if (blockOffset == 0) continue;                       // unused slot
            if (blockOffset + WMX_BLOCK_HDR_SIZE > WMX_SEGMENT_SIZE) continue;  // out of range

            const uint8_t* blockBase = segData + blockOffset;
            uint8_t polyCount = blockBase[0];
            // (vert_count = blockBase[1], norm_count = blockBase[2], pad = blockBase[3]
            //  — not needed for terrain classification but kept for documentation.)

            // Bounds-guard: polygon array must fit within the segment.
            uint32_t polyArrayEnd = blockOffset + WMX_BLOCK_HDR_SIZE +
                                    (uint32_t)polyCount * WMX_POLY_SIZE;
            if (polyArrayEnd > WMX_SEGMENT_SIZE) continue;

            for (int p = 0; p < polyCount; p++) {
                uint8_t terrain = blockBase[WMX_BLOCK_HDR_SIZE + p * WMX_POLY_SIZE
                                            + WMX_TERRAIN_OFFSET];
                if (terrain >= 32 && terrain <= 34) segOceanCount++;
                segPolyCount++;
            }
        }

        totalRealPolys  += segPolyCount;
        totalOceanPolys += segOceanCount;

        // Majority-ocean polygons => OCEAN segment. Empty/degenerate segments
        // (segPolyCount == 0) default to LAND — the playable grid shouldn't
        // contain any, but if one exists, defaulting to land is the
        // conservative choice for accessibility filtering.
        if (segPolyCount > 0 && segOceanCount * 2 > segPolyCount) {
            s_terrainGrid[row][col] = 1;  // OCEAN
            oceanSegs++;
        } else {
            s_terrainGrid[row][col] = 0;  // LAND
            landSegs++;
        }
    }
    free(wmxData);
    s_terrainLoaded = true;

    Log::World("WorldMap: [TERRAIN] Grid built: %d land, %d ocean (of %d). Total real polys=%d (oceans=%d).",
               landSegs, oceanSegs, WMX_PLAYABLE_SEGS, totalRealPolys, totalOceanPolys);

    // Visual grid dump (# = land, ~ = ocean) — valuable for diagnosing
    // coordinate-mapping issues; cheap (24 lines, once per process).
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        char rowStr[WMX_SEG_COLS + 1];
        for (int c = 0; c < WMX_SEG_COLS; c++)
            rowStr[c] = s_terrainGrid[r][c] ? '~' : '#';
        rowStr[WMX_SEG_COLS] = '\0';
        Log::World("WorldMap: [TERRAIN] row%02d: %s", r, rowStr);
    }

    return true;
}

// ============================================================================
// LoadTriggerZones — hex-dumps a configurable list of wmsetus.obj sections
// (controlled by WMSETUS_DUMP_SECTIONS_1IDX) to ff8_world.log for trigger-
// system reverse engineering. Mirrors LoadTerrainGrid's archive-reader
// pattern (world.fi entry lookup + LZSS decompress) but reads world.fi entry
// 10 (wmsetus.obj) instead of entry 9 (wmx.obj). No game-side state is
// captured — the function is purely diagnostic; its output drives the
// decoder design in subsequent builds. v0.14.91 dumped Sections 17 and 18
// (deep research's leading hypothesis, since disproved). v0.14.92 dumps
// Sections 7 and 8 (the disassembly-confirmed field-entry bytecode plus
// its small adjacent section).
// ============================================================================

// Hex-dump a byte range to ff8_world.log under [TRIGGER-DUMP] with section
// label and a printable-ASCII gutter. Caller passes already-validated bounds.
static void DumpTriggerSection(const char* sectLabel, const uint8_t* base, uint32_t bytes)
{
    const uint32_t cap = (bytes < WMSETUS_DUMP_CAP_BYTES) ? bytes : WMSETUS_DUMP_CAP_BYTES;
    Log::World("WorldMap: [TRIGGER-DUMP] %s begin (size=%u, dumping %u)", sectLabel, bytes, cap);
    for (uint32_t off = 0; off < cap; off += 16) {
        char hexpart[16 * 3 + 1] = {};
        char asciipart[17]       = {};
        uint32_t row = (cap - off >= 16) ? 16 : (cap - off);
        for (uint32_t i = 0; i < row; i++) {
            uint8_t b = base[off + i];
            snprintf(hexpart + i * 3, sizeof(hexpart) - i * 3, "%02X ", b);
            asciipart[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        // Pad short final row's hex column to keep the gutter aligned.
        for (uint32_t i = row; i < 16; i++) {
            snprintf(hexpart + i * 3, sizeof(hexpart) - i * 3, "   ");
        }
        asciipart[row] = '\0';
        Log::World("WorldMap: [TRIGGER-DUMP] %s +%04X: %s %s", sectLabel, off, hexpart, asciipart);
    }
    if (cap < bytes) {
        Log::World("WorldMap: [TRIGGER-DUMP] %s truncated at %u (full size %u)", sectLabel, cap, bytes);
    }
    Log::World("WorldMap: [TRIGGER-DUMP] %s end", sectLabel);
}

static bool LoadTriggerZones()
{
    // ---- Build paths exactly the way LoadTerrainGrid does. Auto-detect
    // game install via FF8_EN.exe location.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // ---- Read world.fi (12 bytes per entry: uncompSize, fsOffset, compression).
    uint8_t* fiData = nullptr;
    uint32_t fiSize = 0;
    if (!WM_ReadFileToBuffer(fiPath, &fiData, &fiSize)) {
        Log::World("WorldMap: [TRIGGER-DUMP] Failed to read world.fi at '%s'", fiPath);
        return false;
    }
    if (fiSize < (uint32_t)(WMSETUS_FL_INDEX + 1) * 12) {
        Log::World("WorldMap: [TRIGGER-DUMP] world.fi too small (%u bytes) for entry %d",
                   fiSize, WMSETUS_FL_INDEX);
        free(fiData);
        return false;
    }

    const uint8_t* fiEntry = fiData + WMSETUS_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);
    Log::World("WorldMap: [TRIGGER-DUMP] wmsetus.obj FI entry %d: uncomp=%u offset=%u comp=%u",
               WMSETUS_FL_INDEX, uncompSize, fsOffset, compression);

    // ---- Read wmsetus.obj from world.fs (compressed or raw per FI compression flag).
    uint8_t* wmsData = nullptr;
    if (compression == 0) {
        free(fiData);
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmsData)) {
            Log::World("WorldMap: [TRIGGER-DUMP] Failed to read wmsetus.obj raw");
            return false;
        }
    } else {
        // Compressed: derive compressed-size by reading the next FI entry's offset.
        uint32_t compSize = uncompSize;   // fallback if no later entry exists
        for (int j = WMSETUS_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TRIGGER-DUMP] Failed to read compressed wmsetus.obj");
            return false;
        }
        // FF8 LZSS storage: 4-byte uncompressed-size header precedes the bitstream.
        wmsData = (uint8_t*)malloc(uncompSize);
        if (!wmsData) { free(compData); return false; }
        bool ok = WM_DecompressLZSS(compData + 4, compSize - 4, wmsData, uncompSize);
        free(compData);
        if (!ok) {
            Log::World("WorldMap: [TRIGGER-DUMP] LZSS decompression failed");
            free(wmsData);
            return false;
        }
    }

    // ---- Validate file size against the 48-entry header.
    if (uncompSize < (uint32_t)WMSETUS_HEADER_BYTES) {
        Log::World("WorldMap: [TRIGGER-DUMP] wmsetus.obj too small (%u bytes) for 48-entry header",
                   uncompSize);
        free(wmsData);
        return false;
    }

    // ---- Read the 48-entry section-offset table and log every offset for
    // diagnostic context. v0.14.93's decoder will reuse this header parse;
    // the full dump here makes the entire wmsetus layout visible at a glance
    // so we can sanity-check that section sizes match FF Inside wiki
    // annotations (region map at 2 = 772b, encounters at 1+4 = 392b+1348b,
    // textures at 38 = 257672b, etc.).
    uint32_t hdr[WMSETUS_SECTION_COUNT];
    for (int i = 0; i < WMSETUS_SECTION_COUNT; i++) {
        hdr[i] = *(const uint32_t*)(wmsData + i * 4);
    }
    for (int i = 0; i < WMSETUS_SECTION_COUNT; i++) {
        // Compute size as next-offset minus this-offset; for the last section,
        // size is uncompSize - hdr[last].
        uint32_t sectStart = hdr[i];
        uint32_t sectEnd   = (i + 1 < WMSETUS_SECTION_COUNT) ? hdr[i + 1] : uncompSize;
        uint32_t sectSize  = (sectEnd >= sectStart) ? (sectEnd - sectStart) : 0;
        Log::World("WorldMap: [TRIGGER-DUMP] section %02d (1-indexed): offset=0x%08X size=%u",
                   i + 1, sectStart, sectSize);
    }

    // ---- v0.14.94: extract Section 2 (the 32x24 segment-region byte map)
    // into s_segmentRegionMap[][] for AD path planning. Same buffer / same
    // header parse as the dump above — no duplicate I/O. Section 2 is at
    // 1-indexed position 2 (array index 1). Layout: 768 bytes of region
    // IDs (byte at offset row*32+col is the region ID for segment (col, row))
    // followed by a 4-byte '00 00 00 00' trailer. We bounds-check and skip
    // gracefully on size mismatch; AD's catalog-center fallback path still
    // works without the region map (just less precise arrival detection).
    {
        const int s2idx = 1;   // Section 2 = 1-indexed 2 = array index 1
        if (s2idx < WMSETUS_SECTION_COUNT) {
            uint32_t s2start = hdr[s2idx];
            uint32_t s2end   = (s2idx + 1 < WMSETUS_SECTION_COUNT)
                                ? hdr[s2idx + 1]
                                : uncompSize;
            uint32_t s2size  = (s2end >= s2start) ? (s2end - s2start) : 0;
            const uint32_t needed = (uint32_t)(WMX_SEG_ROWS * WMX_SEG_COLS);   // 768
            if (s2start < uncompSize && s2end <= uncompSize && s2size >= needed) {
                const uint8_t* s2 = wmsData + s2start;
                bool seenRegion[256] = {};
                int  uniqueRegions = 0;
                int  populatedCells = 0;
                for (int row = 0; row < WMX_SEG_ROWS; row++) {
                    for (int col = 0; col < WMX_SEG_COLS; col++) {
                        uint8_t b = s2[row * WMX_SEG_COLS + col];
                        s_segmentRegionMap[row][col] = b;
                        if (b != 0xFF) {
                            populatedCells++;
                            if (!seenRegion[b]) {
                                seenRegion[b] = true;
                                uniqueRegions++;
                            }
                        }
                    }
                }
                s_segmentRegionLoaded = true;
                Log::World("WorldMap: [REGION-MAP] Section 2 loaded into s_segmentRegionMap: %d populated cells (of %d), %d unique region IDs",
                           populatedCells, WMX_SEG_ROWS * WMX_SEG_COLS, uniqueRegions);
            } else {
                Log::World("WorldMap: [REGION-MAP] Section 2 size mismatch (start=0x%08X end=0x%08X size=%u, needed >=%u) \u2014 not loading",
                           s2start, s2end, s2size, needed);
            }
        }
    }

    // ---- Hex-dump each section in WMSETUS_DUMP_SECTIONS_1IDX (1-indexed).
    // Bounds-check before dumping; if any section's offset/end pair looks
    // out of range, log and skip rather than reading past the buffer.
    auto safeDump = [&](int sectArrayIdx, const char* label) {
        if (sectArrayIdx < 0 || sectArrayIdx >= WMSETUS_SECTION_COUNT) return;
        uint32_t sectStart = hdr[sectArrayIdx];
        uint32_t sectEnd   = (sectArrayIdx + 1 < WMSETUS_SECTION_COUNT)
                              ? hdr[sectArrayIdx + 1]
                              : uncompSize;
        if (sectStart >= uncompSize || sectEnd > uncompSize || sectEnd <= sectStart) {
            Log::World("WorldMap: [TRIGGER-DUMP] %s out-of-range (start=0x%08X end=0x%08X file=%u) — skipping",
                       label, sectStart, sectEnd, uncompSize);
            return;
        }
        DumpTriggerSection(label, wmsData + sectStart, sectEnd - sectStart);
    };
    for (int k = 0; k < WMSETUS_DUMP_COUNT; k++) {
        int sect1Idx = WMSETUS_DUMP_SECTIONS_1IDX[k];
        char label[16];
        snprintf(label, sizeof(label), "sect%02d", sect1Idx);
        safeDump(sect1Idx - 1, label);   // 1-indexed in user log, 0-indexed in array
    }

    free(wmsData);
    return true;
}

// ============================================================================
// LogTriggerPrograms — walks s_triggerPrograms[] at module init and emits one
// log line per program for runtime sanity-check that the embedded v0.14.93
// trigger data compiled correctly into the binary. Format per line:
//   [TRIGGER-PROGRAMS] [NN] loc=0xLLLL storyGate=... topVeh=0xVV clauses=K: [(v=...,r=...,...) ...]
// where the storyGate is the top-level story window (or 'any'), topVeh is
// the top-level vehicle restriction (or 0x00 for none), and the clauses
// list is the inline (vehicle,region) tests with optional ',s=[lo,hi)' for
// per-clause story windows and ',unk=0x...' for UNK opcode flags.
// 38 lines total, plus a header summary line. Diagnostic-only — zero
// game-side state captured.
// ============================================================================
static void LogTriggerPrograms()
{
    int totalClauses = 0;
    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        totalClauses += s_triggerPrograms[i].num_clauses;
    }
    Log::World("WorldMap: [TRIGGER-PROGRAMS] count=%d totalClauses=%d (sanity check that embedded data compiled)",
               TRIGGER_PROGRAM_COUNT, totalClauses);

    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        const TriggerProgram& p = s_triggerPrograms[i];

        // Top-level story window.
        char gateBuf[40];
        if (p.top_story_gte == 0 && p.top_story_lt == 0) {
            snprintf(gateBuf, sizeof(gateBuf), "any");
        } else if (p.top_story_lt == 0) {
            snprintf(gateBuf, sizeof(gateBuf), "[%u,inf)", (unsigned)p.top_story_gte);
        } else if (p.top_story_gte == 0) {
            snprintf(gateBuf, sizeof(gateBuf), "[0,%u)", (unsigned)p.top_story_lt);
        } else {
            snprintf(gateBuf, sizeof(gateBuf), "[%u,%u)",
                     (unsigned)p.top_story_gte, (unsigned)p.top_story_lt);
        }

        // Build inline clauses list. Worst case (program 25, 5 clauses with
        // story windows + UNK flags) fits comfortably under 400 chars; 768
        // bytes is generous.
        char clausesBuf[768];
        clausesBuf[0] = '\0';
        size_t cpos = 0;

        if (p.num_clauses == 0 || p.clauses == nullptr) {
            snprintf(clausesBuf, sizeof(clausesBuf), "(none)");
        } else {
            for (uint8_t k = 0; k < p.num_clauses; k++) {
                const TriggerClause& c = p.clauses[k];

                char storyB[40];
                if (c.story_gte == 0 && c.story_lt == 0) {
                    storyB[0] = '\0';   // omit story field entirely when both bounds are 0
                } else if (c.story_lt == 0) {
                    snprintf(storyB, sizeof(storyB), ",s=[%u,inf)", (unsigned)c.story_gte);
                } else if (c.story_gte == 0) {
                    snprintf(storyB, sizeof(storyB), ",s=[0,%u)", (unsigned)c.story_lt);
                } else {
                    snprintf(storyB, sizeof(storyB), ",s=[%u,%u)",
                             (unsigned)c.story_gte, (unsigned)c.story_lt);
                }

                char unkB[20];
                if (c.unk_flags == 0) {
                    unkB[0] = '\0';
                } else {
                    snprintf(unkB, sizeof(unkB), ",unk=0x%04X", (unsigned)c.unk_flags);
                }

                int written = snprintf(clausesBuf + cpos,
                                       sizeof(clausesBuf) - cpos,
                                       "%s(v=0x%02X,r=0x%02X%s%s)",
                                       (k == 0) ? "" : ",",
                                       (unsigned)c.vehicle, (unsigned)c.region,
                                       storyB, unkB);
                if (written < 0 || (size_t)written >= sizeof(clausesBuf) - cpos) {
                    // Buffer full — stop appending. Defensive; won't happen
                    // for the 5-max-clause programs we have.
                    break;
                }
                cpos += (size_t)written;
            }
        }

        Log::World("WorldMap: [TRIGGER-PROGRAMS] [%02d] loc=0x%04X storyGate=%s topVeh=0x%02X clauses=%u: [%s]",
                   i, (unsigned)p.loc_id, gateBuf, (unsigned)p.top_vehicle,
                   (unsigned)p.num_clauses, clausesBuf);
    }
}

// ============================================================================
// ComputeReachability — BFS flood-fill from player segment, populates
// s_reachable[][] for the given vehicle's traversal rules. Restored from
// v0.11.12 impl. 4-connected with torus wrapping (the world map wraps both
// axes, so segment col 31 borders col 0, and row 23 borders row 0).
// ============================================================================
static void ComputeReachability(int startCol, int startRow, VehicleType veh)
{
    memset(s_reachable, 0, sizeof(s_reachable));

    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) return;

    // Player's current cell is always reachable (they're standing there)
    // even if the cell classifies as ocean — e.g. transition frames where
    // position briefly snaps to a coastline edge classified as ocean.
    s_reachable[startRow][startCol] = 1;

    // BFS queue sized for 768 cells (the entire playable grid).
    static int qCol[WMX_PLAYABLE_SEGS];
    static int qRow[WMX_PLAYABLE_SEGS];
    int qHead = 0, qTail = 0;
    qCol[qTail] = startCol;
    qRow[qTail] = startRow;
    qTail++;

    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };

    while (qHead < qTail) {
        int cc = qCol[qHead];
        int cr = qRow[qHead];
        qHead++;

        for (int d = 0; d < 4; d++) {
            int nc = (cc + dx[d] + WMX_SEG_COLS) % WMX_SEG_COLS;
            int nr = (cr + dy[d] + WMX_SEG_ROWS) % WMX_SEG_ROWS;

            if (!s_reachable[nr][nc] && IsSegmentTraversable(nr, nc, veh)) {
                s_reachable[nr][nc] = 1;
                if (qTail < WMX_PLAYABLE_SEGS) {
                    qCol[qTail] = nc;
                    qRow[qTail] = nr;
                    qTail++;
                }
            }
        }
    }

    int reachCount = 0;
    for (int r = 0; r < WMX_SEG_ROWS; r++)
        for (int c = 0; c < WMX_SEG_COLS; c++)
            if (s_reachable[r][c]) reachCount++;

    Log::World("WorldMap: [BFS] From seg(%d,%d) veh=%d: %d/%d segments reachable",
               startCol, startRow, (int)veh, reachCount, WMX_PLAYABLE_SEGS);
}

// ============================================================================
// Catalog management
// ============================================================================
static void BuildDistanceCatalog()
{
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);

    // v0.14.85 (restored from v0.11.15): the engine reports player position as
    // (0,0) for several frames at world-map entry before populating the real
    // value. Building the catalog from (0,0) lands BFS in an arbitrary cell
    // (segment col 16, row 0) that is almost always wrong, so defer until we
    // see a non-zero position. s_catalogBuilt stays false, Poll() retries us
    // each frame until success.
    if (px == 0 && py == 0) {
        Log::World("WorldMap: [DEFER] Position is (0,0), retrying catalog build next poll");
        return;
    }

    // Copy all locations and compute distances
    for (int i = 0; i < LOCATION_COUNT; i++) {
        s_catalog[i] = s_locations[i];
        // Store distance in x field temporarily for sorting
        int32_t dist = (int32_t)CalculateWrappedDistance(px, py, s_locations[i].x, s_locations[i].y);
        s_catalog[i].x = dist;  // temporarily store distance here
    }

    // Sort by distance (ascending)
    std::sort(s_catalog, s_catalog + LOCATION_COUNT, [](const LocationEntry& a, const LocationEntry& b) {
        return a.x < b.x;  // x field contains distance
    });

    // Restore original coordinates
    for (int i = 0; i < LOCATION_COUNT; i++) {
        for (int j = 0; j < LOCATION_COUNT; j++) {
            if (strcmp(s_catalog[i].name, s_locations[j].name) == 0) {
                s_catalog[i].x = s_locations[j].x;
                s_catalog[i].y = s_locations[j].y;
                break;
            }
        }
    }

    // v0.14.85: apply reachability filter. If terrain failed to load (e.g.
    // missing world.fs at runtime), fall back to no-filter mode so cycling
    // still works — graceful degradation rather than empty catalog. If the
    // player is in Ragnarok, also skip the BFS — it can fly anywhere.
    if (!s_terrainLoaded) {
        s_catalogCount = LOCATION_COUNT;
        Log::World("WorldMap: [BFS] Terrain not loaded — catalog unfiltered (%d entries)",
                   s_catalogCount);
    } else {
        VehicleType veh   = GetVehicleType(GetLocomotionMode());
        int         pCol  = WorldXToSegCol(px);
        int         pRow  = WorldYToSegRow(py);
        Log::World("WorldMap: [BFS] Player at (%d,%d) -> seg(%d,%d), vehicle type %d",
                   px, py, pCol, pRow, (int)veh);

        if (veh == VEH_RAGNAROK) {
            // Ragnarok flies over everything — keep all entries, no BFS.
            s_catalogCount = LOCATION_COUNT;
            Log::World("WorldMap: [BFS] Ragnarok mode — catalog unfiltered (%d entries)",
                       s_catalogCount);
        } else {
            ComputeReachability(pCol, pRow, veh);

            // Compact in place: keep only entries on reachable segments.
            int kept = 0;
            for (int i = 0; i < LOCATION_COUNT; i++) {
                int locCol = WorldXToSegCol(s_catalog[i].x);
                int locRow = WorldYToSegRow(s_catalog[i].y);
                if (s_reachable[locRow][locCol]) {
                    if (kept != i) s_catalog[kept] = s_catalog[i];
                    kept++;
                }
            }
            s_catalogCount = kept;
            Log::World("WorldMap: [BFS] Filtered to %d reachable locations (vehicle type %d)",
                       s_catalogCount, (int)veh);
        }
    }

    // Pathological case: nothing reachable. Better than crashing on cycle
    // math — just leave catalog empty and surface in the log; Aaron will
    // notice immediately and we can investigate.
    if (s_catalogCount == 0) {
        Log::World("WorldMap: [BFS] WARNING — no reachable locations from current position");
    }

    s_catalogBuilt = true;
    s_catalogIndex = 0;
    if (s_catalogCount > 0) {
        Log::World("WorldMap: Catalog built (%d entries), nearest: %s",
                   s_catalogCount, s_catalog[0].name);
    }
}

// ============================================================================
// Navigation announcements
// ============================================================================
static void AnnounceLocation(int index)
{
    if (index < 0 || index >= s_catalogCount || !s_catalogBuilt) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    double distance = CalculateWrappedDistance(px, py, s_catalog[index].x, s_catalog[index].y);
    int distanceKm = (int)(distance / 1000.0);  // rough conversion to kilometers
    
    char buf[256];
    if (distanceKm < 1) {
        snprintf(buf, sizeof(buf), "%s. Very close.", s_catalog[index].name);
    } else {
        snprintf(buf, sizeof(buf), "%s. %d kilometers away.", s_catalog[index].name, distanceKm);
    }
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [LOCATION] %s", buf);
}

static void AnnounceBearing()
{
    if (!s_catalogBuilt || s_catalogIndex >= s_catalogCount) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    // Calculate bearing to selected location
    int32_t tx = s_catalog[s_catalogIndex].x;
    int32_t ty = s_catalog[s_catalogIndex].y;
    
    // Handle world wrapping for shortest path
    int32_t dx = tx - px;
    int32_t dy = ty - py;
    
    if (abs(dx) > WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else dy += (int32_t)WM_HEIGHT;
    }
    
    // Convert to bearing (0=North, clockwise)
    double radians = atan2(dx, -dy);  // -dy because FF8 Y increases downward
    double degrees = radians * 180.0 / 3.14159;
    if (degrees < 0) degrees += 360.0;
    
    const char* direction;
    if (degrees < 22.5 || degrees >= 337.5) direction = "North";
    else if (degrees < 67.5) direction = "Northeast";
    else if (degrees < 112.5) direction = "East";
    else if (degrees < 157.5) direction = "Southeast";
    else if (degrees < 202.5) direction = "South";
    else if (degrees < 247.5) direction = "Southwest";
    else if (degrees < 292.5) direction = "West";
    else direction = "Northwest";
    
    double distance = CalculateWrappedDistance(px, py, tx, ty);
    int distanceKm = (int)(distance / 1000.0);
    
    char buf[256];
    snprintf(buf, sizeof(buf), "%s. %s, %d kilometers.", 
             s_catalog[s_catalogIndex].name, direction, distanceKm);
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [BEARING] %s", buf);
}

// ============================================================================
// Vehicle state tracking (v0.14.85.3)
// ============================================================================
// Names match the canonical research-doc enum. Modes outside the canonical
// set never reach this function in normal operation — CheckVehicleChange's
// whitelist filters them — but the default returns 'Unknown vehicle' for
// safety. Cars are identified as a single 'Car' name regardless of the
// specific mode 32-40 value.
static const char* GetVehicleName(uint8_t mode)
{
    if (mode == 0 || mode == 6) return "On foot";        // Squall / Selphie
    if (mode == 3)               return "Ship";          // BAT-validated v0.14.83
    if (mode == 31)              return "Chocobo";
    if (mode >= 32 && mode <= 40) return "Car";
    if (mode == 48)              return "Garden";
    if (mode == 50)              return "Ragnarok";
    return "Unknown vehicle";
}

// v0.14.90: debounce window. The v0.14.89 BAT log demonstrated that even
// canonical locomotion values (3 = Ship, 32–40 = Car, 50 = Ragnarok) read
// TRANSIENTLY during normal on-foot travel, especially at coastlines and
// field-transition boundaries. The previous v0.14.85.3 IsCanonicalLocomotion
// whitelist passed these values straight through to s_lastVehicle, which
// then poisoned downstream arrival logic (the BAT log shows 'Arrived near
// Balamb Garden' firing at 11:24:28 because mode 50 = Ragnarok briefly read,
// vehicle proximity arrival path fired at <600 units before the player
// actually entered the Garden field). Real vehicle changes hold the canonical
// value steady for many frames; transients last 1–2 polls. Debounce: a new
// canonical value must read consistently across DEBOUNCE_POLLS consecutive
// polls before s_lastVehicle is updated and the announcement / catalog
// rebuild fires. Non-canonical values are still ignored at the IsCanonical
// gate; this debounce specifically targets transient CANONICAL values.
static const int DEBOUNCE_POLLS = 4;
static int     s_pendingVehicle      = -1;   // canonical value seen in the last N polls (or -1 if none pending)
static int     s_pendingVehicleCount = 0;    // consecutive polls reading s_pendingVehicle

// v0.14.90.3: world-map entry suppression window. The v0.14.90 debounce above
// handles 1–3-poll (~16–64 ms) transient byte excursions, but the v0.14.90.2
// BAT log exposed a more obstinate noise pattern: during the camera zoom-in
// animation that plays out for ~3 seconds after every world-map re-entry
// (post-battle return is the canonical case), the locomotion byte cycles
// through canonical values — e.g. mode 0 (Squall foot) → mode 3 (Ship) →
// mode 6 (Selphie foot) — each held for ~1 second. Every value sails through
// the 4-poll debounce as a 'real' transition and CheckVehicleChange announces
// each one + fires a BFS catalog rebuild. From the user's POV: three jarring
// vehicle announcements per battle exit and a catalog that briefly contains
// 38 ocean-allowed entries the player can't actually reach.
//
// The animation residue is indistinguishable from real vehicle changes purely
// on byte hold-time — they're all canonical values held for hundreds of ms.
// The only signal we have is 'we just entered the world map.' So time-gate
// CheckVehicleChange for the first WM_ENTRY_DEBOUNCE_MS after every entry.
// During the window every byte change is dropped silently. At expiry, snapshot
// whatever value the byte reads and commit it to s_lastVehicle as the new
// baseline — silently, no announce, no rebuild — because the player did NOT
// perform a vehicle action; the byte just settled to its steady state.
//
// 3000 ms covers the BAT's observed 3-cycle noise pattern with margin. The
// camera zoom-in is ~2 seconds and the player can't manually do anything
// during it, so we don't lose any genuine player-initiated vehicle change
// to the gate. Scripted story events that mount Garden / Ragnarok exactly
// at battle-victory return are the theoretical false-negative case; vanishingly
// rare in practice and the next legitimate transition restores the chain.
static const DWORD WM_ENTRY_DEBOUNCE_MS = 3000;
static DWORD s_wmEntryTick = 0;   // tick when world map was entered (0 = past the window or never)

static void CheckVehicleChange()
{
    // v0.14.94: while a drive is active, ignore the locomotion byte entirely.
    // The byte at WM_LOCOMOTION cycles through canonical vehicle values
    // (32-40 Car range, 48 Garden, 50 Ragnarok) during AD's keybd_event
    // arrow-key injection — each value held long enough (>4 polls / >64ms)
    // to pass v0.14.90's debounce. Real vehicle mounts/dismounts during a
    // drive are ruled out: AD only injects arrow keys, and mounting Garden/
    // Ragnarok or boarding Ship requires action keys the player isn't
    // pressing. Side benefit: s_lastVehicle stays at its pre-drive value,
    // so isOnFoot in UpdateAutoDrive's stuck-handling sweep guard stays
    // truthful (the v0.14.93 BAT showed sweep failed to fire because a
    // spurious 'Ship (mode 3)' announcement set s_lastVehicle = 3, making
    // isOnFoot false even though Aaron was on foot the entire time).
    // After the drive ends (arrival / cancel / stuck-give-up), the next
    // CheckVehicleChange tick reads the byte normally with the existing
    // debounce + WM_ENTRY_DEBOUNCE logic intact.
    if (s_driveActive) {
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    uint8_t vehicle = GetLocomotionMode();

    // v0.14.90.3: suppress all vehicle-change processing during the world-map
    // re-entry animation window. See WM_ENTRY_DEBOUNCE_MS state declaration
    // above for full rationale.
    if (s_wmEntryTick != 0) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - s_wmEntryTick;
        if (elapsed < WM_ENTRY_DEBOUNCE_MS) {
            // Still inside the suppression window. Drop any pending state
            // and exit. The byte's animation cycling does not commit.
            s_pendingVehicle      = -1;
            s_pendingVehicleCount = 0;
            return;
        }
        // Window expired this tick. Snapshot the current byte as the new
        // baseline, silently. No announce (player didn't perform an action),
        // no catalog rebuild (the byte's settled value is what reachability
        // logic should have been using all along; if the rule class differs
        // from what's currently filtered we'd want a rebuild, but the v0.14.90
        // debounce on subsequent ticks will detect that on its own).
        if (IsCanonicalLocomotion(vehicle)) {
            int prev = s_lastVehicle;
            s_lastVehicle = vehicle;
            Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=%u (was %d, suppressed %lums of byte noise)",
                       vehicle, prev, (unsigned long)elapsed);
        } else {
            // Non-canonical at expiry — unusual but possible at story-script
            // boundaries. Leave s_lastVehicle untouched; the next tick's normal
            // CheckVehicleChange flow will start fresh once a canonical value
            // shows up. Log so we notice if this happens repeatedly.
            Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Window expired with non-canonical locomotion=%u; keeping s_lastVehicle=%d",
                       vehicle, s_lastVehicle);
        }
        s_wmEntryTick         = 0;
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;  // skip the rest of CheckVehicleChange this tick; normal flow resumes next tick
    }

    // v0.14.85.3 whitelist: only canonical locomotion values per the research
    // doc are eligible to trigger vehicle-change announcements or catalog
    // rebuilds. Transient bytes (animation phase counters, field-transition
    // state values like the mode 4 we observed at the Fire Cavern boundary in
    // the v0.14.85.2 BAT) are silently ignored — s_lastVehicle is not updated,
    // nothing is announced, no rebuild fires. Real vehicle changes still
    // register because the byte passes through a canonical value at the
    // mount/dismount moment, and that is the transition we capture.
    // v0.14.90 debounce: even canonical values can be transient (BAT showed
    // mode 50 / mode 3 reading for 1 poll during foot travel near coastlines).
    // Track a pending value across consecutive polls. Only commit to
    // s_lastVehicle (and fire the announcement / rebuild side effects) once
    // the same value has held for DEBOUNCE_POLLS in a row.
    if (!IsCanonicalLocomotion(vehicle)) {
        // Non-canonical → reset pending state. The byte's transient excursion
        // is over; whatever it tries next must build up its count from zero.
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    // Canonical value matches the already-committed s_lastVehicle — nothing
    // to do, ensure the pending state is also clear.
    if ((int)vehicle == s_lastVehicle) {
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    // Canonical value differs from current. Track / advance the pending
    // count. If it reaches DEBOUNCE_POLLS we commit. Otherwise we wait.
    if ((int)vehicle == s_pendingVehicle) {
        s_pendingVehicleCount++;
    } else {
        s_pendingVehicle      = vehicle;
        s_pendingVehicleCount = 1;
    }

    if (s_pendingVehicleCount < DEBOUNCE_POLLS) {
        // Not yet stable enough; don't commit, don't announce, don't rebuild.
        return;
    }

    // Stable for DEBOUNCE_POLLS polls in a row — treat as a real transition.
    s_pendingVehicle      = -1;
    s_pendingVehicleCount = 0;

    {
        if (s_lastVehicle != -1) {  // skip initial announcement
            const char* newVehicle = GetVehicleName(vehicle);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.", newVehicle);
            ScreenReader::Speak(buf, true);  // interrupt previous speech
            Log::World("WorldMap: Vehicle change: %s (mode %u)", newVehicle, vehicle);

            // v0.14.85.2 + .3: rebuild the catalog when the vehicle's BFS rule
            // CLASS changes (not just the VehicleType). Three rule classes:
            //   0 = land-only  (foot, chocobo, car)
            //   1 = ocean-allowed (Ship, Garden)
            //   2 = no-filter  (Ragnarok)
            // Foot ↔ car ↔ chocobo transitions stay within class 0, so they
            // don't trigger gratuitous rebuilds (the BFS result would be
            // identical). Class crossings (foot ↔ Ragnarok, car ↔ Ship)
            // do rebuild because the reachability set genuinely differs.
            VehicleType oldType = (s_lastVehicle >= 0)
                                  ? GetVehicleType((uint8_t)s_lastVehicle)
                                  : VEH_ON_FOOT;
            VehicleType newType = GetVehicleType(vehicle);
            int oldClass = GetBfsRuleClass(oldType);
            int newClass = GetBfsRuleClass(newType);
            if (oldClass != newClass && s_onWorldMap) {
                s_catalogBuilt = false;
                Log::World("WorldMap: [BFS] Vehicle rule class changed (%d -> %d), forcing catalog rebuild",
                           oldClass, newClass);
            }
        }
        s_lastVehicle = vehicle;
    }
}

// ============================================================================
// Path planner state (v0.14.94)
// ============================================================================
// A* on the 32x24 segment grid produces a sequence of segment cells from
// the player's start segment to a goal segment. The drive then follows the
// path one waypoint at a time, advancing the index when the player crosses
// into the current waypoint's segment. Replanned on world-map re-entry
// after a battle pause (random encounters drift the player off the path).
//
// Encoding: each waypoint is packed as (row << 8) | col into a uint16_t.
// 768 waypoints is a hard upper bound (visiting every segment exactly once);
// real paths are typically <40 waypoints across the longest reachable
// diagonal. s_drivePathPlanned distinguishes 'planner produced a path'
// (segment-membership arrival, waypoint steering) from 'planner declined or
// failed' (catalog-center steering, distance-based arrival fallback).
static const int DRIVE_PATH_MAX = WMX_PLAYABLE_SEGS;   // 768

static uint16_t s_drivePath[DRIVE_PATH_MAX];           // packed (row<<8)|col waypoints
static int      s_drivePathLen      = 0;
static int      s_drivePathIdx      = 0;
static bool     s_drivePathPlanned  = false;

// Goal-segment set for arrival detection. Populated when the planner picks
// a destination program: every cell in s_segmentRegionMap whose byte equals
// any matching clause's region operand. The player entering ANY of these
// via world-map exit counts as arrival. Stored as packed (row<<8)|col, max
// 768 entries (entire grid in the degenerate case).
static uint16_t s_driveGoalSegs[DRIVE_PATH_MAX];
static int      s_driveGoalSegCount = 0;

static inline uint16_t PackSeg(int row, int col)
{
    return (uint16_t)(((row & 0xFF) << 8) | (col & 0xFF));
}
static inline int UnpackRow(uint16_t s) { return (s >> 8) & 0xFF; }
static inline int UnpackCol(uint16_t s) { return  s       & 0xFF; }

// ============================================================================
// Path planner (v0.14.94)
// ============================================================================

// Story window predicate. A clause's [story_gte, story_lt) gates pass when
// the live story value is in that half-open range. Conventions from the
// decoded artifact: gte=0 means 'no lower bound' (always passes the lower
// check), lt=0 means '+infinity' (always passes the upper check).
static bool StoryWindowMatches(uint16_t gte, uint16_t lt, uint16_t story)
{
    bool lowerOk = (gte == 0) || (story >= gte);
    bool upperOk = (lt  == 0) || (story <  lt);
    return lowerOk && upperOk;
}

// Vehicle predicate. Clause vehicle codes: 0x80=Squall foot, 0x84=alt-leader
// foot (treated as foot for AD), 0x30=Garden, 0x31=Chocobo, 0x32=Ragnarok,
// 0x00=any (used in clauses when the program's top-level vehicle is set).
// Player VehicleType enum: VEH_ON_FOOT/VEH_CHOCOBO/VEH_CAR/VEH_GARDEN/
// VEH_RAGNAROK. Cars are treated as foot for clause matching: cars travel
// the same land segments as foot, and to actually cross a trigger the
// player will dismount and walk the last few steps — the goal-segment
// region defines the trigger zone either way.
static bool VehicleClauseMatches(uint16_t clauseVeh, VehicleType playerVeh)
{
    if (clauseVeh == TRIG_VEH_ANY) return true;
    if (clauseVeh == TRIG_VEH_FOOT || clauseVeh == TRIG_VEH_FOOT_ALT)
        return playerVeh == VEH_ON_FOOT || playerVeh == VEH_CAR;
    if (clauseVeh == TRIG_VEH_CHOCOBO)  return playerVeh == VEH_CHOCOBO;
    if (clauseVeh == TRIG_VEH_GARDEN)   return playerVeh == VEH_GARDEN;
    if (clauseVeh == TRIG_VEH_RAGNAROK) return playerVeh == VEH_RAGNAROK;
    return false;
}

// Whole-clause match: vehicle predicate + story window. unk_flags is logged
// but not gated — clauses with non-zero UNK flags are accepted with caution
// (we don't model the additional conditions, but the clause's reachable
// region is still a valid-by-evidence trigger zone the engine sometimes
// uses in this state). Only used as fallback when no UNK-free clauses match.
static bool ClauseMatches(const TriggerClause& c, VehicleType veh, uint16_t story)
{
    return VehicleClauseMatches(c.vehicle, veh) &&
           StoryWindowMatches(c.story_gte, c.story_lt, story);
}

// Find the closest segment to catalog (X, Y) whose region byte is
// referenced by ANY clause currently satisfiable from the player's
// vehicle and story state. v0.14.95 superseded the v0.14.94 "match the
// catalog segment's region directly" approach: catalog (X, Y) points at
// each location's polygon center on the world map (e.g. Balamb Garden's
// catalog seg(19,20) maps to region 0x0C, Balamb Town's seg(17,20) maps
// to region 0x07), but those are OPEN LAND regions — NOT trigger zones.
// The actual field-entry trigger zones sit in adjacent segments with
// different region bytes. v0.14.94 therefore failed to match any program
// for any catalog target; drives all fell back to v0.14.93 distance-based
// arrival, which can't detect Fire Cavern (catalog ~6800 units off).
//
// New algorithm: build the active region set first (regions referenced
// by clauses currently satisfiable from veh + story), then walk the
// 32x24 grid for any segment whose region is in that set, taking the
// one closest to (catRow, catCol). A 5-segment distance cap avoids
// steering toward some other location's trigger when nothing nearby
// matches — in that case decline and fall back to catalog-center.
//
// Returns the chosen program index (for log identification) or -1 on
// failure. Writes the matched region byte to *outRegion. Two-pass clean/
// UNK preference is kept: a clean active region beats a UNK-flagged one
// for the same region byte.
static int WrapManhattan(int r1, int c1, int r2, int c2);   // v0.14.95: forward decl for MatchProgramForCatalog's distance-cap logic

static int MatchProgramForCatalog(int32_t catX, int32_t catY,
                                  VehicleType veh, uint16_t story,
                                  uint8_t* outRegion)
{
    if (!s_segmentRegionLoaded) return -1;
    int catCol = WorldXToSegCol(catX);
    int catRow = WorldYToSegRow(catY);
    if (catCol < 0 || catCol >= WMX_SEG_COLS ||
        catRow < 0 || catRow >= WMX_SEG_ROWS) return -1;

    // Build the active region set. Each entry records (region byte, owning
    // program index, isClean flag).
    static uint8_t activeRegions[64];
    static int     activeProgIdx[64];
    static bool    activeIsClean[64];
    int activeCount = 0;

    auto addActive = [&](uint8_t region, int progIdx, bool isClean) {
        for (int j = 0; j < activeCount; j++) {
            if (activeRegions[j] == region) {
                if (isClean && !activeIsClean[j]) {
                    activeProgIdx[j] = progIdx;
                    activeIsClean[j] = true;
                }
                return;
            }
        }
        if (activeCount < (int)(sizeof(activeRegions)/sizeof(activeRegions[0]))) {
            activeRegions[activeCount] = region;
            activeProgIdx[activeCount] = progIdx;
            activeIsClean[activeCount] = isClean;
            activeCount++;
        }
    };

    // v0.14.97: PLAN-DEBUG trace logging. Walks every program and emits one
    // log line per program with explicit pass/fail reason. Per-clause lines
    // include vehicle/region/story/unk values and the skip reason (vehicle-
    // mismatch / story-out-of-window / PASS). Followed by an active-region
    // summary line. This dump tells us exactly why the closest-active-region
    // search may pick the wrong region for narrow-entrance locations like
    // Balamb Town (whose actual trigger region is 0x07, referenced only by
    // program 9 with top_story_gte=290 — at story 205, program 9 is filtered
    // out and 0x07 falls out of the active set).
    int catRegByte = (catRow >= 0 && catRow < WMX_SEG_ROWS &&
                      catCol >= 0 && catCol < WMX_SEG_COLS)
                     ? s_segmentRegionMap[catRow][catCol] : 0xFF;
    Log::World("WorldMap: [PLAN-DEBUG] Walking %d programs for veh=%d story=%u catalog=(%d,%d) seg(%d,%d) catRegion=0x%02X",
               TRIGGER_PROGRAM_COUNT, (int)veh, (unsigned)story, catX, catY, catCol, catRow,
               (unsigned)catRegByte);

    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        const TriggerProgram& p = s_triggerPrograms[i];

        // Top-level vehicle gate.
        bool topVehOK = (p.top_vehicle == TRIG_VEH_ANY) ||
                        VehicleClauseMatches(p.top_vehicle, veh);
        if (!topVehOK) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X SKIP top_vehicle=0x%02X mismatch (player veh=%d)",
                       i, (unsigned)p.loc_id, (unsigned)p.top_vehicle, (int)veh);
            continue;
        }

        // Top-level story gate.
        if (!StoryWindowMatches(p.top_story_gte, p.top_story_lt, story)) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X SKIP top_story=[%u,%u) story=%u out of window",
                       i, (unsigned)p.loc_id,
                       (unsigned)p.top_story_gte, (unsigned)p.top_story_lt,
                       (unsigned)story);
            continue;
        }

        if (p.num_clauses == 0 || p.clauses == nullptr) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X SKIP no clauses (top-level only)",
                       i, (unsigned)p.loc_id);
            continue;
        }

        // Per-clause walk with explicit reasons.
        int clausesPassed = 0;
        for (uint8_t k = 0; k < p.num_clauses; k++) {
            const TriggerClause& c = p.clauses[k];
            bool vehOK   = VehicleClauseMatches(c.vehicle, veh);
            bool storyOK = StoryWindowMatches(c.story_gte, c.story_lt, story);
            const char* reason;
            if (vehOK && storyOK) {
                reason = "PASS";
                addActive(c.region, i, c.unk_flags == 0);
                clausesPassed++;
            } else if (!vehOK && !storyOK) {
                reason = "FAIL veh+story";
            } else if (!vehOK) {
                reason = "FAIL veh";
            } else {
                reason = "FAIL story";
            }
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X clause %u: v=0x%02X r=0x%02X s=[%u,%u) unk=0x%04X => %s",
                       i, (unsigned)p.loc_id, (unsigned)k,
                       (unsigned)c.vehicle, (unsigned)c.region,
                       (unsigned)c.story_gte, (unsigned)c.story_lt,
                       (unsigned)c.unk_flags, reason);
        }
        if (clausesPassed == 0) {
            Log::World("WorldMap: [PLAN-DEBUG] [%02d] loc=0x%04X => 0 clauses passed; nothing added",
                       i, (unsigned)p.loc_id);
        }
    }

    // Summary: active region set after the walk completes.
    {
        char regBuf[256];
        int pos = 0;
        regBuf[0] = '\0';
        for (int j = 0; j < activeCount && pos < (int)sizeof(regBuf) - 8; j++) {
            int n = snprintf(regBuf + pos, sizeof(regBuf) - pos, "%s0x%02X",
                             j == 0 ? "" : ",", (unsigned)activeRegions[j]);
            if (n < 0) break;
            pos += n;
        }
        Log::World("WorldMap: [PLAN-DEBUG] Active region set after walk (%d): {%s}",
                   activeCount, regBuf);
    }

    if (activeCount == 0) {
        Log::World("WorldMap: [PLAN] No active regions for veh=%d story=%u \u2014 fallback",
                   (int)veh, (unsigned)story);
        return -1;
    }

    // Walk all segments looking for one whose region is active; pick the
    // closest to catalog (catCol, catRow). 5-segment cap avoids accidentally
    // routing to some other location's trigger when no nearby region is
    // active for the player's state.
    static const int SEGMENT_DISTANCE_CAP = 5;
    int bestDist = SEGMENT_DISTANCE_CAP + 1;
    int bestRow = -1, bestCol = -1;
    int bestActiveIdx = -1;
    for (int row = 0; row < WMX_SEG_ROWS; row++) {
        for (int col = 0; col < WMX_SEG_COLS; col++) {
            uint8_t r = s_segmentRegionMap[row][col];
            if (r == 0xFF) continue;
            int activeIdx = -1;
            for (int j = 0; j < activeCount; j++) {
                if (activeRegions[j] == r) { activeIdx = j; break; }
            }
            if (activeIdx < 0) continue;
            int d = WrapManhattan(row, col, catRow, catCol);
            if (d < bestDist) {
                bestDist  = d;
                bestRow   = row;
                bestCol   = col;
                bestActiveIdx = activeIdx;
            }
        }
    }

    if (bestActiveIdx < 0) {
        Log::World("WorldMap: [PLAN] %d active regions but none within %d segs of catalog (%d,%d) seg(%d,%d) catRegion=0x%02X \u2014 fallback",
                   activeCount, SEGMENT_DISTANCE_CAP, catX, catY, catCol, catRow,
                   (unsigned)s_segmentRegionMap[catRow][catCol]);
        return -1;
    }

    *outRegion = activeRegions[bestActiveIdx];
    int progIdx = activeProgIdx[bestActiveIdx];
    Log::World("WorldMap: [PLAN] Catalog (%d,%d) seg(%d,%d) \u2192 closest active region 0x%02X at seg(%d,%d) segDist=%d (program [%02d] locID=0x%04X, %s, %d active regions for veh=%d story=%u)",
               catX, catY, catCol, catRow,
               (unsigned)*outRegion, bestCol, bestRow, bestDist,
               progIdx, (unsigned)s_triggerPrograms[progIdx].loc_id,
               activeIsClean[bestActiveIdx] ? "clean" : "UNK-flagged",
               activeCount, (int)veh, (unsigned)story);
    return progIdx;
}

// Collect every segment in s_segmentRegionMap whose byte equals the matched
// region. This is the equivalent trigger zone for the destination — entering
// any of these via world-map exit counts as arrival. Writes packed values
// into s_driveGoalSegs[] up to DRIVE_PATH_MAX, returns the count.
static int CollectGoalSegments(uint8_t region)
{
    int count = 0;
    for (int row = 0; row < WMX_SEG_ROWS && count < DRIVE_PATH_MAX; row++) {
        for (int col = 0; col < WMX_SEG_COLS && count < DRIVE_PATH_MAX; col++) {
            if (s_segmentRegionMap[row][col] == region) {
                s_driveGoalSegs[count++] = PackSeg(row, col);
            }
        }
    }
    return count;
}

// True iff (row, col) is in the current goal set.
static bool IsGoalSegment(int row, int col)
{
    uint16_t target = PackSeg(row, col);
    for (int i = 0; i < s_driveGoalSegCount; i++) {
        if (s_driveGoalSegs[i] == target) return true;
    }
    return false;
}

// Wrap-aware Manhattan distance on the 32x24 torus. East-west wrap matters
// for Esthar approaches from the west side of the map.
static int WrapManhattan(int r1, int c1, int r2, int c2)
{
    int dr = abs(r1 - r2);
    int dc = abs(c1 - c2);
    if (dr > WMX_SEG_ROWS / 2) dr = WMX_SEG_ROWS - dr;
    if (dc > WMX_SEG_COLS / 2) dc = WMX_SEG_COLS - dc;
    return dr + dc;
}

// Min wrap-Manhattan from (row, col) to any goal segment. Admissible A*
// heuristic: 4-neighbor uniform-cost grid + Manhattan never overestimates
// true path length, so A* is optimal.
static int HeuristicToGoals(int row, int col)
{
    int best = 9999;   // larger than any reachable Manhattan on a 32x24 torus (max 28)
    for (int i = 0; i < s_driveGoalSegCount; i++) {
        int gr = UnpackRow(s_driveGoalSegs[i]);
        int gc = UnpackCol(s_driveGoalSegs[i]);
        int h = WrapManhattan(row, col, gr, gc);
        if (h < best) best = h;
    }
    return best;
}

// Convert a segment's center to world coordinates. Inverse of
// WorldXToSegCol/Row:  world_x = col*8192 + 4096 - 131072,
//                      world_y = row*8192 + 4096.
// World map wraps both axes; for steering targets we emit the canonical
// (un-wrapped) coordinate — TorusBearing handles wrap on its own.
static void SegmentCenterToWorld(int col, int row, int32_t* outX, int32_t* outY)
{
    *outX = (int32_t)(col * 8192 + 4096) - 131072;
    *outY = (int32_t)(row * 8192 + 4096);
}

// A* over the 32x24 segment grid. 4-neighbor edges with torus wrap on both
// axes. Edge validity comes from IsSegmentTraversable() which already handles
// vehicle class. Ragnarok callers should skip the planner entirely.
//
// On success: populates s_drivePath[] with the segment sequence from the
// step AFTER start to the goal (start segment itself is NOT a waypoint, the
// player is already there); sets s_drivePathLen, s_drivePathIdx=0,
// s_drivePathPlanned=true; returns true. On failure: returns false and
// leaves s_drivePathPlanned=false; caller falls back to catalog-center.
static bool PlanPath(int startCol, int startRow, VehicleType veh)
{
    s_drivePathLen     = 0;
    s_drivePathIdx     = 0;
    s_drivePathPlanned = false;

    if (s_driveGoalSegCount == 0) {
        Log::World("WorldMap: [PLAN] No goal segments \u2014 planner cannot run");
        return false;
    }
    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) {
        Log::World("WorldMap: [PLAN] Start segment (%d,%d) out of range", startCol, startRow);
        return false;
    }

    static const uint16_t INF_COST = 0xFFFF;
    static uint16_t gScore [WMX_SEG_ROWS][WMX_SEG_COLS];
    static uint16_t cameRow[WMX_SEG_ROWS][WMX_SEG_COLS];
    static uint16_t cameCol[WMX_SEG_ROWS][WMX_SEG_COLS];
    static uint8_t  closed [WMX_SEG_ROWS][WMX_SEG_COLS];
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        for (int c = 0; c < WMX_SEG_COLS; c++) {
            gScore[r][c]  = INF_COST;
            cameRow[r][c] = 0xFFFF;
            cameCol[r][c] = 0xFFFF;
            closed[r][c]  = 0;
        }
    }

    // Min-heap as the open set. Each entry packs (f-score << 16) | (row<<8)|col
    // so that uint32_t compare prioritizes by f-score; ties break on packed
    // segment which gives stable ordering. Capped at 4 * grid size since each
    // node may be pushed multiple times before being closed.
    static uint32_t heap[4 * WMX_PLAYABLE_SEGS];
    int heapSize = 0;

    auto heapPush = [&](int f, int row, int col) {
        if (heapSize >= (int)(sizeof(heap)/sizeof(heap[0]))) return;
        uint32_t entry = ((uint32_t)f << 16) | (uint32_t)PackSeg(row, col);
        heap[heapSize] = entry;
        int i = heapSize++;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (heap[parent] <= heap[i]) break;
            uint32_t tmp = heap[parent]; heap[parent] = heap[i]; heap[i] = tmp;
            i = parent;
        }
    };
    auto heapPop = [&](int* outF, int* outRow, int* outCol) -> bool {
        if (heapSize == 0) return false;
        uint32_t top = heap[0];
        *outF   = (int)(top >> 16);
        uint16_t packed = (uint16_t)(top & 0xFFFF);
        *outRow = UnpackRow(packed);
        *outCol = UnpackCol(packed);
        heap[0] = heap[--heapSize];
        int i = 0;
        for (;;) {
            int l = 2*i + 1, r = 2*i + 2, smallest = i;
            if (l < heapSize && heap[l] < heap[smallest]) smallest = l;
            if (r < heapSize && heap[r] < heap[smallest]) smallest = r;
            if (smallest == i) break;
            uint32_t tmp = heap[i]; heap[i] = heap[smallest]; heap[smallest] = tmp;
            i = smallest;
        }
        return true;
    };

    gScore[startRow][startCol] = 0;
    heapPush(HeuristicToGoals(startRow, startCol), startRow, startCol);

    int goalRow = -1, goalCol = -1;
    int popsExpanded = 0;
    while (heapSize > 0) {
        int f, row, col;
        if (!heapPop(&f, &row, &col)) break;
        if (closed[row][col]) continue;   // duplicate entry from earlier push
        closed[row][col] = 1;
        popsExpanded++;

        if (IsGoalSegment(row, col)) {
            goalRow = row;
            goalCol = col;
            break;
        }

        const int dx[] = { 0, 0, -1, 1 };
        const int dy[] = { -1, 1, 0, 0 };
        for (int d = 0; d < 4; d++) {
            int nr = (row + dy[d] + WMX_SEG_ROWS) % WMX_SEG_ROWS;
            int nc = (col + dx[d] + WMX_SEG_COLS) % WMX_SEG_COLS;
            if (closed[nr][nc]) continue;
            if (!IsSegmentTraversable(nr, nc, veh)) continue;
            int tentative = gScore[row][col] + 1;
            if (tentative < gScore[nr][nc]) {
                gScore[nr][nc]  = (uint16_t)tentative;
                cameRow[nr][nc] = (uint16_t)row;
                cameCol[nr][nc] = (uint16_t)col;
                int h = HeuristicToGoals(nr, nc);
                heapPush(tentative + h, nr, nc);
            }
        }
    }

    if (goalRow < 0) {
        Log::World("WorldMap: [PLAN] No path from seg(%d,%d) to any of %d goal cells (expanded %d nodes, veh=%d)",
                   startCol, startRow, s_driveGoalSegCount, popsExpanded, (int)veh);
        return false;
    }

    // Reconstruct the path by walking cameFrom[] back from goal to start.
    // Build into a temporary buffer in reverse order then copy to s_drivePath
    // forward. Skip the start segment itself — the player is already there.
    static uint16_t reverseBuf[DRIVE_PATH_MAX];
    int rcount = 0;
    int cr = goalRow, cc = goalCol;
    while ((cr != startRow || cc != startCol) && rcount < DRIVE_PATH_MAX) {
        reverseBuf[rcount++] = PackSeg(cr, cc);
        uint16_t pr = cameRow[cr][cc];
        uint16_t pc = cameCol[cr][cc];
        if (pr == 0xFFFF || pc == 0xFFFF) break;
        cr = pr;
        cc = pc;
    }
    for (int i = 0; i < rcount; i++) {
        s_drivePath[i] = reverseBuf[rcount - 1 - i];
    }
    s_drivePathLen     = rcount;
    s_drivePathIdx     = 0;
    s_drivePathPlanned = true;

    Log::World("WorldMap: [PLAN] Path found: %d waypoints from seg(%d,%d) to goal seg(%d,%d) (%d goal cells in zone, expanded %d nodes, veh=%d)",
               rcount, startCol, startRow, goalCol, goalRow,
               s_driveGoalSegCount, popsExpanded, (int)veh);
    return true;
}

// Plan a path for the current drive. Reads s_driveTargetX/Y and the live
// vehicle/story state. On success, populates s_drivePath/Len/Idx/Planned
// and s_driveGoalSegs/Count. On failure leaves s_drivePathPlanned=false;
// AD then falls back to catalog-center steering and distance-based arrival.
static bool PlanDrivePath(int32_t startX, int32_t startY)
{
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;

    if (!s_segmentRegionLoaded) {
        Log::World("WorldMap: [PLAN] Region map not loaded \u2014 fallback to catalog-center steering");
        return false;
    }

    VehicleType veh   = (s_lastVehicle < 0) ? VEH_ON_FOOT
                                            : GetVehicleType((uint8_t)s_lastVehicle);
    uint16_t    story = GetCurrentStoryFlag();

    // Ragnarok flies anywhere — skip the planner, catalog-center steering
    // works fine because Ragnarok lands directly on the catalog coord.
    if (veh == VEH_RAGNAROK) {
        Log::World("WorldMap: [PLAN] Ragnarok mode \u2014 skipping planner (catalog-center steering)");
        return false;
    }

    uint8_t region = 0;
    int progIdx = MatchProgramForCatalog(s_driveTargetX, s_driveTargetY,
                                         veh, story, &region);
    if (progIdx < 0) {
        return false;   // no matching program; MatchProgramForCatalog logged why
    }

    s_driveGoalSegCount = CollectGoalSegments(region);
    if (s_driveGoalSegCount == 0) {
        Log::World("WorldMap: [PLAN] Region 0x%02X has zero cells in s_segmentRegionMap \u2014 fallback",
                   (unsigned)region);
        return false;
    }

    int startCol = WorldXToSegCol(startX);
    int startRow = WorldYToSegRow(startY);

    // Player already in a goal segment: empty path. UpdateAutoDrive will
    // walk forward (catalog-center final-approach behavior) until the
    // engine fires the trigger and Poll's exit handler declares arrival.
    if (IsGoalSegment(startRow, startCol)) {
        s_drivePathLen     = 0;
        s_drivePathIdx     = 0;
        s_drivePathPlanned = true;
        Log::World("WorldMap: [PLAN] Player already in goal segment seg(%d,%d) region=0x%02X \u2014 empty path",
                   startCol, startRow, (unsigned)region);
        return true;
    }

    return PlanPath(startCol, startRow, veh);
}

// ============================================================================
// Auto-drive lifecycle (v0.14.86)
// ============================================================================
// StartAutoDrive captures the destination by VALUE (not by index) so the
// drive survives catalog rebuilds. UpdateAutoDrive does the per-tick work.
// StopAutoDrive is idempotent and centralizes key release. The drive
// pauses automatically on world-map exit (Poll() handles that path) and
// resumes on re-entry, allowing random encounters not to abort the route.
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    ReleaseAllDriveKeys();
    s_driveActive = false;
    // v0.14.87: clear sweep + final-approach state so a fresh drive starts clean.
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    // v0.14.94: clear path-planner state so the next StartAutoDrive begins
    // with a clean slate. PlanDrivePath also resets these on call, but
    // clearing here ensures any post-stop log inspection shows the drive
    // as fully torn down.
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;
    // v0.14.96: clear deferred-arrival state. StopAutoDrive may be called
    // from inside ResolveDeferredArrival (success path); the deferred-arrival
    // flag is cleared there before this call, but reset here too for any
    // path that calls StopAutoDrive without going through the resolver
    // (cancel, stuck-give-up, sweep-give-up).
    s_driveAwaitingArrivalDecision = false;
    s_driveExitTick                = 0;
    if (reason && *reason) {
        ScreenReader::Speak(reason, true);
        Log::World("WorldMap: [DRIVE] Stopped: %s", reason);
    } else {
        Log::World("WorldMap: [DRIVE] Stopped (silent)");
    }
}

static void StartAutoDrive(int catIdx)
{
    if (s_driveActive) return;                       // toggle handler should have called Stop first
    if (!s_catalogBuilt || s_catalogCount == 0) {
        ScreenReader::Speak("No locations available.", true);
        return;
    }
    if (catIdx < 0 || catIdx >= s_catalogCount) {
        ScreenReader::Speak("Invalid destination.", true);
        return;
    }

    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    if (px == 0 && py == 0) {
        ScreenReader::Speak("Position unavailable. Try again.", true);
        return;
    }

    const LocationEntry& dest = s_catalog[catIdx];

    // v0.14.89: prefer refined entry coord when available. The s_catalog[]
    // working copy holds the catalog center (s_locations[].x/y); we look up
    // the matching s_locations[] index and check the parallel refined table
    // for an empirical entry coord captured on a prior successful drive.
    int locIdx = FindLocationIndexByTargetCoords(dest.x, dest.y);
    if (locIdx >= 0 && s_refinedHas[locIdx]) {
        s_driveTargetX = s_refinedX[locIdx];
        s_driveTargetY = s_refinedY[locIdx];
        Log::World("WorldMap: [DRIVE] Using refined entry for %s: (%d,%d) instead of catalog (%d,%d)",
                   dest.name, s_refinedX[locIdx], s_refinedY[locIdx], dest.x, dest.y);
    } else {
        s_driveTargetX = dest.x;
        s_driveTargetY = dest.y;
    }
    strncpy(s_driveTargetName, dest.name, sizeof(s_driveTargetName) - 1);
    s_driveTargetName[sizeof(s_driveTargetName) - 1] = '\0';

    double dist = CalculateWrappedDistance(px, py, dest.x, dest.y);
    DWORD now = GetTickCount();

    s_driveActive            = true;
    s_driveStartTime         = now;
    s_driveLastAnnounce      = now;
    s_driveLastDist          = dist;
    s_driveStuckX            = px;
    s_driveStuckY            = py;
    s_driveStuckCheckTime    = now;
    s_driveStuckCount        = 0;
    s_driveApproachAnnounced = (dist < DRIVE_APPROACH_DIST);  // suppress one-shot if already inside
    s_finalApproachEnterTick = 0;     // v0.14.87: not yet in final approach zone
    s_sweepActive            = false;
    s_sweepPhase             = 0;
    s_sweepTurning           = true;

    // v0.14.87: capture vehicle state at drive start. Arrival semantics differ:
    // on-foot waits for actual world-map exit (entering the location) to
    // announce arrival; vehicles announce arrival at proximity since they
    // can't enter most locations anyway. Re-checked dynamically each tick
    // via GetVehicleType(s_lastVehicle) so vehicle changes mid-drive are
    // honored (e.g. dismount car → walk into town).
    s_driveOnFootAtStart = (s_lastVehicle < 0) ||
                           (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    int distKm = (int)(dist / 1000.0);
    char buf[160];
    if (distKm < 1) {
        snprintf(buf, sizeof(buf), "Driving to %s. Very close.", s_driveTargetName);
    } else {
        snprintf(buf, sizeof(buf), "Driving to %s. %d kilometers.", s_driveTargetName, distKm);
    }
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [DRIVE] Start \u2192 %s at (%d,%d), dist=%.0f units (%d km)",
               s_driveTargetName, s_driveTargetX, s_driveTargetY, dist, distKm);

    // v0.14.94: run the path planner once. Sets s_drivePath[]/Len/Idx/Planned
    // and s_driveGoalSegs[]/Count. On failure (Ragnarok, region map not
    // loaded, no matching trigger program, no path), s_drivePathPlanned
    // stays false and AD falls back to catalog-center steering with the
    // v0.14.93 distance-based arrival heuristic. UpdateAutoDrive picks the
    // active mode from s_drivePathPlanned each tick.
    PlanDrivePath(px, py);
}

static void StartSweep(int32_t px, int32_t py, DWORD now)
{
    s_sweepActive   = true;
    s_sweepPhase    = 1;
    s_sweepTurning  = true;
    s_sweepStateEnd = now + SWEEP_TURN_BASE_MS;
    // Reset stuck tracking so the sweep itself can't be classified as stuck
    s_driveStuckX = px;
    s_driveStuckY = py;
    s_driveStuckCheckTime = now;
    s_driveStuckCount = 0;
    ScreenReader::Speak("Searching for entrance.", true);
    Log::World("WorldMap: [DRIVE-SWEEP] Started (target=%s, phase 1 turning right %dms)",
               s_driveTargetName, SWEEP_TURN_BASE_MS);
}

static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    uint16_t heading = GetWorldMapHeading();

    // Position can briefly read (0,0) at world-map re-entry. Skip the tick
    // rather than treating it as a real location far from target.
    if (px == 0 && py == 0) return;

    double dist = CalculateWrappedDistance(px, py, s_driveTargetX, s_driveTargetY);
    DWORD now = GetTickCount();

    // v0.14.87: dynamic on-foot check each tick. If the player dismounts a
    // car or otherwise switches to foot mid-drive, the arrival semantics
    // switch immediately. The locomotion byte's transient noise is filtered
    // by the v0.14.85.3 IsCanonicalLocomotion whitelist before s_lastVehicle
    // updates, so this is stable.
    bool isOnFoot = (s_lastVehicle < 0) ||
                    (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    // ---- v0.14.90: removed vehicle proximity arrival ('Arrived near X').
    // The original v0.11.06 design announced arrival ONLY when the world
    // map exits to a field — same code path for foot and vehicle. Adding
    // a vehicle-only proximity branch in v0.14.87 was wrong because it
    // relied on s_lastVehicle which gets poisoned by transient locomotion-
    // byte values. The v0.14.89 BAT log showed 'Arrived near Balamb Garden'
    // firing at distance ~600 because the locomotion byte transiently read
    // mode 50 (Ragnarok) for one poll; the player wasn't actually in a
    // vehicle. Field-entry detection in Poll()'s exit handler is the
    // single source of truth for arrival; if the engine triggered a field
    // transition, the player arrived. Vehicles that can enter fields
    // (Garden → FH dock, Ship → docks) work the same way.


    // ---- One-shot approach announcement when crossing DRIVE_APPROACH_DIST. ----
    // Suppressed during sweep — the user already heard "Searching for entrance."
    if (!s_driveApproachAnnounced && dist < DRIVE_APPROACH_DIST && !s_sweepActive) {
        s_driveApproachAnnounced = true;
        int distKm = (int)(dist / 1000.0);
        char buf[128];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Approaching %s.", s_driveTargetName);
        } else {
            snprintf(buf, sizeof(buf), "Approaching %s. %d kilometers.", s_driveTargetName, distKm);
        }
        ScreenReader::Speak(buf, true);
        s_driveLastAnnounce = now;
    }
    s_driveLastDist = dist;
    s_driveLastPosX = px;       // v0.14.89: stash for refined-entry capture
    s_driveLastPosY = py;

    // ---- Periodic distance announce (suppressed during sweep). ----
    if (!s_sweepActive && now - s_driveLastAnnounce >= DRIVE_ANNOUNCE_INTERVAL_MS) {
        s_driveLastAnnounce = now;
        int distKm = (int)(dist / 1000.0);
        char buf[64];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Less than 1 kilometer.");
        } else {
            snprintf(buf, sizeof(buf), "%d kilometers.", distKm);
        }
        ScreenReader::Speak(buf, true);
    }

    // ---- Sweep state machine (on-foot, narrow-entrance recovery). ----
    // When stuck or timed-out in final approach, sweep alternately turns
    // and walks to scan the local area for the entrance trigger. Field
    // exit during any sub-state is detected by Poll()'s exit handler and
    // counts as arrival. Sweep exhaustion (phase > SWEEP_MAX_PHASES) gives
    // up with "Could not find entrance."
    if (s_sweepActive) {
        if (now >= s_sweepStateEnd) {
            if (s_sweepTurning) {
                // Turn done → start walk sub-state.
                s_sweepTurning = false;
                s_sweepStateEnd = now + SWEEP_WALK_DURATION_MS;
                Log::World("WorldMap: [DRIVE-SWEEP] Phase %d walk start (%dms)",
                           s_sweepPhase, SWEEP_WALK_DURATION_MS);
            } else {
                // Walk done → advance to next phase or give up.
                s_sweepPhase++;
                if (s_sweepPhase > SWEEP_MAX_PHASES) {
                    StopAutoDrive("Could not find entrance.");
                    return;
                }
                s_sweepTurning = true;
                DWORD turnDur = SWEEP_TURN_BASE_MS + (DWORD)(s_sweepPhase - 1) * 200;
                s_sweepStateEnd = now + turnDur;
                const char* dir = (s_sweepPhase % 2 == 1) ? "right" : "left";
                Log::World("WorldMap: [DRIVE-SWEEP] Phase %d turn start (%s, %dms)",
                           s_sweepPhase, dir, turnDur);
            }
        }
        // Output keys based on current sweep sub-state.
        bool wantUp    = !s_sweepTurning;
        bool wantRight = s_sweepTurning && (s_sweepPhase % 2 == 1);
        bool wantLeft  = s_sweepTurning && (s_sweepPhase % 2 == 0);
        SetDriveKeys(wantUp, wantLeft, wantRight);
        return;
    }

    // ---- Final-approach timeout (on-foot only). ----
    // Track when the player crossed below FINAL_APPROACH_DIST. If we've been
    // in final approach for more than FINAL_APPROACH_TIMEOUT_MS without the
    // world-map exiting, we likely walked past a narrow entrance. Start the
    // sweep search to scan local area for the trigger.
    if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST) {
        if (s_finalApproachEnterTick == 0) {
            s_finalApproachEnterTick = now;
            Log::World("WorldMap: [DRIVE] Entered final approach zone (dist=%.0f)", dist);
        }
        if (now - s_finalApproachEnterTick > FINAL_APPROACH_TIMEOUT_MS) {
            Log::World("WorldMap: [DRIVE] Final-approach timeout (%dms in zone, no entry)",
                       (int)(now - s_finalApproachEnterTick));
            StartSweep(px, py, now);
            return;
        }
    } else {
        // Out of final approach — reset so we re-arm next time we cross in.
        s_finalApproachEnterTick = 0;

        // v0.14.97: if sweep was active but the player has drifted far from
        // the target (typically because a battle resumed at a position far
        // from where sweep was activated), abort sweep so normal steering
        // gets us back to final approach. Without this, sweep can persist
        // across pause/resume cycles and exhaust all 6 phases at the wrong
        // location — the v0.14.96 post-push BAT showed this happening at
        // ~7km from Balamb Town. Grace band of 1.5x final-approach distance
        // before aborting; if the player is just barely outside the zone
        // (e.g. mid-sweep wandering), don't disrupt the search.
        if (s_sweepActive && dist > DRIVE_FINAL_APPROACH_DIST * 1.5) {
            Log::World("WorldMap: [DRIVE-SWEEP] Aborting (drifted out of final approach: dist=%.0f, threshold=%.0f) \u2014 returning to normal steering",
                       dist, DRIVE_FINAL_APPROACH_DIST * 1.5);
            s_sweepActive  = false;
            s_sweepPhase   = 0;
            s_sweepTurning = true;
        }
    }

    // ---- Stuck detection. ----
    if (now - s_driveStuckCheckTime >= DRIVE_STUCK_CHECK_INTERVAL_MS) {
        double moved = CalculateWrappedDistance(s_driveStuckX, s_driveStuckY, px, py);
        if (moved < DRIVE_STUCK_THRESHOLD) {
            s_driveStuckCount++;
            Log::World("WorldMap: [DRIVE] Stuck check %d/%d (moved %.0f units in %dms window)",
                       s_driveStuckCount, DRIVE_STUCK_MAX, moved, DRIVE_STUCK_CHECK_INTERVAL_MS);
            // v0.14.87: on-foot stuck inside final approach → sweep instead
            // of giving up. Catches blocked entrances (e.g. trying to walk
            // into Balamb at the wrong angle and bumping a wall).
            if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount >= 2) {
                Log::World("WorldMap: [DRIVE] Stuck in final approach → sweep");
                StartSweep(px, py, now);
                return;
            }
            if (s_driveStuckCount >= DRIVE_STUCK_MAX) {
                StopAutoDrive("Stuck. Cannot reach destination.");
                return;
            }
        } else {
            s_driveStuckCount = 0;  // any meaningful movement resets the counter
        }
        s_driveStuckX         = px;
        s_driveStuckY         = py;
        s_driveStuckCheckTime = now;
    }

    // ---- v0.14.94: waypoint advancement. When the player crosses into the
    // current waypoint's segment, advance s_drivePathIdx to the next one.
    // Empty path (s_drivePathLen == 0) means the player started in a goal
    // segment — nothing to advance, the steering target stays at the catalog
    // center and Poll's exit handler announces arrival when the trigger fires.
    if (s_drivePathPlanned && s_drivePathIdx < s_drivePathLen) {
        int playerRow = WorldYToSegRow(py);
        int playerCol = WorldXToSegCol(px);
        int wpRow = UnpackRow(s_drivePath[s_drivePathIdx]);
        int wpCol = UnpackCol(s_drivePath[s_drivePathIdx]);
        if (playerRow == wpRow && playerCol == wpCol) {
            s_drivePathIdx++;
            Log::World("WorldMap: [PLAN] Reached waypoint %d/%d at seg(%d,%d) \u2014 advancing",
                       s_drivePathIdx, s_drivePathLen, wpCol, wpRow);
        }
    }

    // ---- Steering target selection. Waypoint when planner active and not
    // yet through the path; catalog target otherwise (initial fallback,
    // post-path final approach into the goal segment, or planner declined).
    int32_t steerX = s_driveTargetX;
    int32_t steerY = s_driveTargetY;
    if (s_drivePathPlanned && s_drivePathIdx < s_drivePathLen) {
        int wpRow = UnpackRow(s_drivePath[s_drivePathIdx]);
        int wpCol = UnpackCol(s_drivePath[s_drivePathIdx]);
        SegmentCenterToWorld(wpCol, wpRow, &steerX, &steerY);
    }

    // ---- Steering decision. ----
    int targetBearing = TorusBearing(px, py, steerX, steerY);
    int relBearing    = (targetBearing - (int)heading + 4096) & 0xFFF;
    // relBearing: 0=ahead, 1024=right 90°, 2048=behind, 3072=left 90°.

    bool wantUp = false, wantLeft = false, wantRight = false;

    if (dist < DRIVE_FINAL_APPROACH_DIST) {
        // Final approach: catalog coordinates aren't always exactly on the
        // entrance trigger zone. Walking forward through the area sweeps
        // through the trigger reliably without micro-corrections that can
        // overshoot the trigger band entirely. The final-approach timeout
        // (above) covers the case where this fails for narrow entrances.
        wantUp = true;
    } else if (relBearing < 200 || relBearing > 3896) {
        // Within ~17.6° of dead ahead — just go.
        wantUp = true;
    } else if (relBearing < 1800) {
        // Target is to the right (up to ~158°).
        wantRight = true;
        if (relBearing < 512) wantUp = true;  // within ~45°: turn AND walk
    } else {
        // Target is to the left (the remaining ~158-360° arc).
        wantLeft = true;
        if (relBearing > 3584) wantUp = true;  // within ~45° of ahead-left: turn AND walk
    }

    SetDriveKeys(wantUp, wantLeft, wantRight);
}

// ============================================================================
// Keyboard input polling (v0.14.83)
// ============================================================================
// Replaces the orphaned v0.11.x HandleKeyPress dispatch path. HandleKeyPress
// was defined in this file but never declared in world_map.h and never
// invoked from anywhere — leftover collateral from the v0.14.24 build damage
// / v0.14.31 partial recovery (Update() and Shutdown() were restored, the
// keyboard dispatch was not). Result: world map nav keys had been silently
// dead since the recovery.
//
// New design mirrors FieldNavigation::HandleKeys: PollKeys() is called from
// inside Poll() and uses GetAsyncKeyState with edge-detected statics.
// Implicitly gated on s_onWorldMap because Poll() early-returns when off
// world map. No collision with FieldNavigation: it gates its identical key
// set on FF8Addresses::IsOnField() which is mutually exclusive with the
// world-map scene flag (0x0203ED2C == 0).
static void PollKeys()
{
    static bool s_minusWas = false;
    static bool s_plusWas  = false;
    static bool s_bkspWas  = false;
    static bool s_bslashWas = false;
    // v0.14.90.1: input-injection diagnostic. F12 fires a 200ms VK_UP pulse via
    // keybd_event (the current AD mechanism). F2 fires the same pulse via
    // SendInput. Aaron presses each while standing still on the world map and
    // listens for a footstep. If F12 moves the character, keybd_event works
    // and AD has a different bug (timing, state-machine race, etc.). If F12
    // does nothing but F2 works, switch AD to SendInput. If neither moves
    // the character, the world map reads input through a pipeline that
    // bypasses both — DirectInput direct read, raw input, or memory-mapped
    // game-pad state. v0.14.89 BAT showed AD said it was "driving" while the
    // distance-to-target never decreased — character was not moving — which
    // exposed that the entire keybd_event assumption (carried over from past
    // chats v0.11.05-v0.11.08 that allegedly BAT-validated it) had not been
    // re-verified on the current system. v0.14.90.1 verifies it directly.
    static bool s_diagF12Was = false;
    static bool s_diagF2Was  = false;

    bool minus  = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus   = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    bool bksp   = (GetAsyncKeyState(VK_BACK)      & 0x8000) != 0;
    bool bslash = (GetAsyncKeyState(VK_OEM_5)     & 0x8000) != 0;  // '\' key

    if (minus && !s_minusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex - 1 + s_catalogCount) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] minus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (plus && !s_plusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex + 1) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] plus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (bksp && !s_bkspWas) {
        AnnounceBearing();
        Log::World("WorldMap: [KEY] backspace bearing");
    }
    if (bslash && !s_bslashWas) {
        // v0.14.86: toggle auto-drive. If a drive is already running, cancel it;
        // otherwise start a drive toward the currently-selected catalog entry.
        // This replaces the v0.14.84 placeholder. The auto-drive system is
        // restored from past chats v0.11.05-v0.11.10.
        if (s_driveActive) {
            StopAutoDrive("Cancelled.");
            Log::World("WorldMap: [KEY] backslash → cancel");
        } else if (s_catalogBuilt && s_catalogCount > 0) {
            Log::World("WorldMap: [KEY] backslash → start drive to idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
            StartAutoDrive(s_catalogIndex);
        } else {
            ScreenReader::Speak("No locations available.", true);
            Log::World("WorldMap: [KEY] backslash → no catalog");
        }
    }

    s_minusWas  = minus;
    s_plusWas   = plus;
    s_bkspWas   = bksp;
    s_bslashWas = bslash;
}

// ============================================================================
// v0.14.96: Deferred arrival decision
// ============================================================================
// Called from Poll() each tick while s_driveAwaitingArrivalDecision is true
// (world map exited, drive still active, decision not yet resolved). Reads
// pGameMode and decides arrival vs encounter when the mode has settled.
// May be called many ticks before resolving. SEH-guarded reads on every
// FF8Addresses pointer in case the resolver hadn't populated them at startup
// (defensive — they should all be valid by the time AD is usable).
static void ResolveDeferredArrival()
{
    DWORD now = GetTickCount();
    DWORD elapsed = now - s_driveExitTick;

    uint16_t mode = 0xFFFF;
    __try {
        if (FF8Addresses::pGameMode) mode = *FF8Addresses::pGameMode;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode = 0xFFFF;
    }

    bool isFieldArrival = (mode == FF8Addresses::MODE_FIELD);
    bool isEncounter   = (mode == FF8Addresses::MODE_SWIRL ||
                          mode == FF8Addresses::MODE_BATTLE ||
                          mode == FF8Addresses::MODE_AFTER_BATTLE);

    if (isFieldArrival) {
        // Read field info for diagnostic logging. Both addresses are
        // populated when mode==1; SEH guard belt-and-suspenders.
        uint16_t fieldId = 0xFFFF;
        const char* fieldName = "<unknown>";
        __try {
            if (FF8Addresses::pCurrentFieldId) fieldId = *FF8Addresses::pCurrentFieldId;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try {
            if (FF8Addresses::pCurrentFieldName) fieldName = FF8Addresses::pCurrentFieldName;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // Refined-coord capture: same logic as the v0.14.95 arrival path.
        // The player's last-known world-map (X,Y) before exit is the actual
        // entry trigger position; subsequent drives steer there directly.
        int locIdx = FindLocationIndexByTargetCoords(s_driveTargetX, s_driveTargetY);
        if (locIdx >= 0 && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
            bool wasRefined = s_refinedHas[locIdx];
            s_refinedX[locIdx]   = s_driveLastPosX;
            s_refinedY[locIdx]   = s_driveLastPosY;
            s_refinedHas[locIdx] = true;
            Log::World("WorldMap: [DRIVE] %s refined entry for %s at (%d,%d) (was target=(%d,%d))",
                       wasRefined ? "Updated" : "Captured",
                       s_locations[locIdx].name,
                       s_driveLastPosX, s_driveLastPosY,
                       s_driveTargetX, s_driveTargetY);
        }

        char buf[160];
        snprintf(buf, sizeof(buf), "Arrived at %s.", s_driveTargetName);
        Log::World("WorldMap: [DRIVE] Arrival via game-mode (mode=%u MODE_FIELD, fieldId=0x%04X, fieldName='%s', target=%s, dist=%.0f, lastPos=(%d,%d), elapsed=%lums)",
                   (unsigned)mode, (unsigned)fieldId, fieldName,
                   s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   (unsigned long)elapsed);
        s_driveAwaitingArrivalDecision = false;
        StopAutoDrive(buf);
        return;
    }

    if (isEncounter) {
        // Battle/encounter — drive stays active and paused; will resume on
        // world-map re-entry via the existing entry handler in Poll().
        int pausedCol = WorldXToSegCol(s_driveLastPosX);
        int pausedRow = WorldYToSegRow(s_driveLastPosY);
        uint8_t pausedRegion = 0xFF;
        if (s_segmentRegionLoaded &&
            pausedRow >= 0 && pausedRow < WMX_SEG_ROWS &&
            pausedCol >= 0 && pausedCol < WMX_SEG_COLS) {
            pausedRegion = s_segmentRegionMap[pausedRow][pausedCol];
        }
        const char* modeLabel = (mode == FF8Addresses::MODE_SWIRL)        ? "MODE_SWIRL" :
                                (mode == FF8Addresses::MODE_BATTLE)       ? "MODE_BATTLE" :
                                                                            "MODE_AFTER_BATTLE";
        Log::World("WorldMap: [DRIVE] Paused via game-mode (mode=%u %s, target=%s, dist=%.0f, lastPos=(%d,%d), seg=(%d,%d), region=0x%02X, planned=%d, elapsed=%lums) \u2014 will resume on re-entry",
                   (unsigned)mode, modeLabel,
                   s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   pausedCol, pausedRow, (unsigned)pausedRegion,
                   s_drivePathPlanned ? 1 : 0,
                   (unsigned long)elapsed);
        s_driveAwaitingArrivalDecision = false;
        return;
    }

    // Mode hasn't settled yet (still MODE_WORLDMAP, or some unknown value).
    // Keep waiting up to the timeout; ResolveDeferredArrival is called again
    // next Poll tick.
    if (elapsed < ARRIVAL_DECISION_TIMEOUT_MS) {
        return;
    }

    // Timeout — fall back to v0.14.95 segment-membership / distance heuristic
    // as safety net. Logged with 'timeout-fallback' suffix so the log makes
    // it clear this path fired.
    Log::World("WorldMap: [DRIVE] Arrival decision timeout (mode=%u after %lums) \u2014 falling back to segment/distance heuristic",
               (unsigned)mode, (unsigned long)elapsed);

    bool arrived = false;
    const char* arrivalReason = "";
    int  arrivedSegRow = -1, arrivedSegCol = -1;

    if (s_drivePathPlanned && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
        arrivedSegRow = WorldYToSegRow(s_driveLastPosY);
        arrivedSegCol = WorldXToSegCol(s_driveLastPosX);
        arrived       = IsGoalSegment(arrivedSegRow, arrivedSegCol);
        arrivalReason = arrived ? "segment-membership (timeout-fallback)" : "";
    } else if (s_driveLastDist > 0 && s_driveLastDist < DRIVE_ARRIVED_ON_EXIT_DIST) {
        arrived       = true;
        arrivalReason = "exit-distance (timeout-fallback)";
    }

    s_driveAwaitingArrivalDecision = false;

    if (arrived) {
        int locIdx = FindLocationIndexByTargetCoords(s_driveTargetX, s_driveTargetY);
        if (locIdx >= 0 && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
            bool wasRefined = s_refinedHas[locIdx];
            s_refinedX[locIdx]   = s_driveLastPosX;
            s_refinedY[locIdx]   = s_driveLastPosY;
            s_refinedHas[locIdx] = true;
            Log::World("WorldMap: [DRIVE] %s refined entry for %s at (%d,%d) (was target=(%d,%d))",
                       wasRefined ? "Updated" : "Captured",
                       s_locations[locIdx].name,
                       s_driveLastPosX, s_driveLastPosY,
                       s_driveTargetX, s_driveTargetY);
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "Arrived at %s.", s_driveTargetName);
        Log::World("WorldMap: [DRIVE] Arrival via %s (target=%s, dist=%.0f, lastPos=(%d,%d), seg=(%d,%d))",
                   arrivalReason, s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   arrivedSegCol, arrivedSegRow);
        StopAutoDrive(buf);
    } else {
        Log::World("WorldMap: [DRIVE] Paused via timeout-fallback (target=%s, dist=%.0f, lastPos=(%d,%d), planned=%d) \u2014 will resume on re-entry",
                   s_driveTargetName, s_driveLastDist,
                   s_driveLastPosX, s_driveLastPosY,
                   s_drivePathPlanned ? 1 : 0);
    }
}

// ============================================================================
// Main polling loop
// ============================================================================
void Poll()
{
    bool nowOnWorldMap = IsOnWorldMap();
    
    // Detect world map entry
    if (nowOnWorldMap && !s_onWorldMap) {
        s_onWorldMap = true;
        s_wmEntryTick = GetTickCount();   // v0.14.90.3: arm the locomotion-byte suppression window
        s_catalogBuilt = false;  // force rebuild on next poll
        Log::World("WorldMap: Entered world map");

        if (s_driveActive) {
            // v0.14.88: drive paused during a random encounter / battle, now
            // resuming. Re-arm timers so the stuck-detection window doesn't
            // trigger on the field/battle gap, and announce so the user
            // knows the drive is still going. Stable s_driveTargetX/Y/name
            // survive arbitrary catalog rebuilds. Note: drives that exited
            // world map FOR a field (location entry) were already terminated
            // by the MODE_FIELD detection path below, so reaching this branch
            // means the off-map state was a battle.
            DWORD now = GetTickCount();
            s_driveLastAnnounce      = now;
            s_driveStuckCheckTime    = now;
            s_driveStuckCount        = 0;
            s_finalApproachEnterTick = 0;  // re-arm final-approach timer
            int32_t rx, ry, rz;
            GetWorldMapPosition(&rx, &ry, &rz);
            if (rx != 0 || ry != 0) {
                s_driveStuckX = rx;
                s_driveStuckY = ry;
                // v0.14.94: replan after a battle / random-encounter pause.
                // Random encounters can drift the player off the previously
                // planned path; replanning from the post-battle position
                // keeps the drive efficient. If the planner declines
                // (region 0xFF, no matching program, no path), s_drivePathPlanned
                // becomes false and AD finishes the drive with catalog-
                // center steering.
                Log::World("WorldMap: [DRIVE] Replanning after world-map re-entry from (%d,%d)", rx, ry);
                PlanDrivePath(rx, ry);
            }
            char buf[160];
            snprintf(buf, sizeof(buf), "Resuming drive to %s.", s_driveTargetName);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: [DRIVE] Resumed after world-map re-entry → %s",
                       s_driveTargetName);
        } else {
            ScreenReader::Speak("World map.", true);
        }

        // Check if we're in the right game mode for world map functionality
        __try {
            if (FF8Addresses::pGameMode && *FF8Addresses::pGameMode != FF8Addresses::MODE_WORLDMAP) {
                Log::World("WorldMap: Warning - On world map but game mode is %u (expected %u)", 
                          *FF8Addresses::pGameMode, FF8Addresses::MODE_WORLDMAP);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::World("WorldMap: Exception checking game mode");
        }
    }
    
    // Detect world map exit
    if (!nowOnWorldMap && s_onWorldMap) {
        s_onWorldMap = false;
        s_catalogBuilt = false;
        Log::World("WorldMap: Exited world map");

        // v0.14.96: defer the arrival decision instead of deciding instantly.
        // The instant-decision approaches (v0.14.94 segment-membership +
        // v0.14.95 distance heuristic) gave false-positive arrivals when
        // random encounters fired near a destination — both signals say
        // 'yes' because the player IS near the target, but the actual game
        // state is a battle, not a field entry. v0.14.96 captures exit
        // state and lets ResolveDeferredArrival decide based on the settled
        // game mode (MODE_FIELD = arrival, battle modes = encounter). The
        // wait is brief (typically 1-3 polls / ~16-50ms) and bounded by
        // ARRIVAL_DECISION_TIMEOUT_MS = 2000ms.
        if (s_driveActive) {
            ReleaseAllDriveKeys();
            s_driveAwaitingArrivalDecision = true;
            s_driveExitTick = GetTickCount();
            Log::World("WorldMap: [DRIVE] Awaiting arrival decision (target=%s, dist=%.0f, lastPos=(%d,%d), planned=%d)",
                       s_driveTargetName, s_driveLastDist,
                       s_driveLastPosX, s_driveLastPosY,
                       s_drivePathPlanned ? 1 : 0);
        }
    }

    // v0.14.96: resolve deferred arrival decision while off world map.
    // Runs BEFORE the early-return below so it gets called every Poll() tick
    // until the decision settles or times out. ResolveDeferredArrival
    // self-clears s_driveAwaitingArrivalDecision when the decision resolves.
    if (!s_onWorldMap && s_driveAwaitingArrivalDecision) {
        ResolveDeferredArrival();
    }

    
    if (!s_onWorldMap) return;
    
    // Build catalog on first poll while on world map
    if (!s_catalogBuilt) {
        BuildDistanceCatalog();
    }
    
    // Check for vehicle changes
    CheckVehicleChange();

    // v0.14.86: per-frame auto-drive update. Internally gated on s_driveActive
    // so this is a near-no-op when no drive is in progress. Placed AFTER
    // CheckVehicleChange so that any rule-class rebuild fires first — the
    // drive's stable target X/Y/name don't depend on the catalog state, but
    // the order keeps logs in a sensible sequence (vehicle change → catalog
    // rebuild → drive tick).
    UpdateAutoDrive();
    
    // Track significant position changes for future auto-drive features
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    static int32_t lastX = 0, lastY = 0;
    
    double movement = CalculateWrappedDistance(px, py, lastX, lastY);
    if (movement > 1000) {  // moved more than 1km
        s_lastMovementTick = GetTickCount();
        lastX = px;
        lastY = py;
    }

    // v0.14.83: Poll nav keys (-, =, Backspace) at the end of the world-map
    // poll cycle. Catalog is guaranteed built by this point; s_onWorldMap is
    // true (we early-returned above otherwise). See PollKeys comment for
    // ownership notes.
    PollKeys();
}

// ============================================================================
// Module initialization
// ============================================================================
void Initialize()
{
    s_onWorldMap = false;
    s_catalogBuilt = false;
    s_catalogCount = 0;
    s_catalogIndex = 0;
    s_lastVehicle = -1;
    s_lastMovementTick = 0;

    // v0.14.90.3: reset entry-debounce state. s_wmEntryTick = 0 means 'not
    // currently in a suppression window'; the first world-map entry will set
    // it. s_pendingVehicle/Count are also reset for cleanliness, though the
    // existing v0.14.90 debounce logic re-initializes them on demand.
    s_wmEntryTick = 0;
    s_pendingVehicle = -1;
    s_pendingVehicleCount = 0;

    // v0.14.86: auto-drive state. Initialized to inactive; no keys held.
    // ReleaseAllDriveKeys() is a no-op given the bool flags are false, but
    // calling it makes the intent explicit if module re-init ever happens
    // mid-session.
    s_driveActive = false;
    s_driveTargetX = 0;
    s_driveTargetY = 0;
    s_driveTargetName[0] = '\0';
    s_driveStuckCount = 0;
    s_driveApproachAnnounced = false;
    // v0.14.87: sweep + final-approach state
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    s_driveOnFootAtStart = true;
    s_driveLastPosX = 0;
    s_driveLastPosY = 0;

    // v0.14.94: path planner state. Cleared so the first StartAutoDrive
    // begins with empty path and goal set; PlanDrivePath populates these
    // when invoked.
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;

    // v0.14.96: deferred-arrival state. Reset on every Initialize so a fresh
    // game session starts without any pending arrival decision.
    s_driveAwaitingArrivalDecision = false;
    s_driveExitTick                = 0;

    // v0.14.89: refined entry-coord table. Cleared on Initialize because
    // persistence is not yet implemented — a fresh game session starts
    // with the canonical catalog and accumulates refined entries through
    // gameplay. Persistence is queued for v0.14.90.
    memset(s_refinedX, 0, sizeof(s_refinedX));
    memset(s_refinedY, 0, sizeof(s_refinedY));
    memset(s_refinedHas, 0, sizeof(s_refinedHas));

    // v0.14.94: clear the segment-region map. Real values get populated
    // inside LoadTriggerZones below if Section 2 reads cleanly. If the load
    // fails (missing world.fs, decompression error, size mismatch) the
    // planner sees s_segmentRegionLoaded = false and AD falls back to
    // v0.14.93's catalog-center steering.
    memset(s_segmentRegionMap, 0xFF, sizeof(s_segmentRegionMap));
    s_segmentRegionLoaded = false;

    ReleaseAllDriveKeys();

    // v0.14.85: load terrain grid once at module init. Idempotent
    // (s_terrainLoaded short-circuits subsequent calls). If the load fails
    // — e.g. world.fs missing or corrupt — we log and continue; catalog
    // building will fall back to no-filter mode rather than blocking.
    if (LoadTerrainGrid()) {
        Log::World("WorldMap: [INIT] Terrain grid loaded successfully");
    } else {
        Log::World("WorldMap: [INIT] Terrain grid load failed — catalog will be unfiltered");
    }

    // v0.14.92: hex-dump wmsetus.obj Sections 7 and 8 (the field-entry
    // bytecode + its small adjacent section) to ff8_world.log. Diagnostic-
    // only (no game-side state captured); failure is non-fatal because
    // AD's existing catalog-center + sweep-search path continues to work
    // without the decoded trigger data. v0.14.93 will hardcode the decoded
    // s_triggerData[] from this dump; v0.14.94 wires it into AD targeting.
    if (LoadTriggerZones()) {
        Log::World("WorldMap: [INIT] Trigger-zone hex dump complete (see [TRIGGER-DUMP] entries above)");
        if (s_segmentRegionLoaded) {
            Log::World("WorldMap: [INIT] Segment region map loaded for AD path planning");
        } else {
            Log::World("WorldMap: [INIT] Segment region map NOT loaded \u2014 AD will fall back to catalog-center steering");
        }
    } else {
        Log::World("WorldMap: [INIT] Trigger-zone load failed \u2014 see preceding [TRIGGER-DUMP] entries");
    }

    // v0.14.93: dump the embedded s_triggerPrograms[] for runtime sanity-check
    // that the data compiled correctly. 38 lines under [TRIGGER-PROGRAMS].
    // Always runs (no failure path) since the data is static-initialized.
    LogTriggerPrograms();

    Log::World("WorldMap: Module initialized (v%s)", FF8OPC_VERSION);
}

// ============================================================================
// Public Update entry — called every ~16ms from the accessibility thread.
// Restored v0.14.31 (was deleted from world_map.cpp during build damage).
// Thin wrapper: defers all polling/announcement work to internal Poll().
// ============================================================================
void Update()
{
    Poll();
}

// ============================================================================
// Public Shutdown entry — restored v0.14.31.
// Resets module state. No hooks installed by this module so nothing to unhook.
// ============================================================================
void Shutdown()
{
    // v0.14.86: cancel any in-flight drive and release injected keys before
    // module teardown. StopAutoDrive is idempotent and silent when reason
    // is null, which is correct here — shutdown shouldn't speak.
    if (s_driveActive) {
        StopAutoDrive(nullptr);
    } else {
        ReleaseAllDriveKeys();  // defensive; flags should already be false
    }

    s_onWorldMap = false;
    s_catalogBuilt = false;
    s_catalogCount = 0;
    s_catalogIndex = 0;
    s_lastVehicle = -1;
    s_lastMovementTick = 0;
    Log::World("WorldMap: Shutdown complete.");
}

} // namespace WorldMap