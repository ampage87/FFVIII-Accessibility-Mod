// world_map_history.h - Archived per-version comments for world_map.cpp
//
// NOT INCLUDED BY THE BUILD. Preserved for reference only.
//
// This file holds the historical "CURRENT STATE / Prior baseline" comment
// blocks that previously accreted at the top of world_map.cpp. Moved here
// at the v0.16.0 refactor when world_map.cpp was split into .inl files
// and the inline-changelog pattern was retired (matching the v0.15.12.0
// retirement of the same pattern in ff8_accessibility.h).
//
// The canonical changelog lives in `CHANGELOG.md` at the project root;
// older entries are in `CHANGELOG_HISTORY.md`. This file preserves the
// per-version diagnostic narrative and rationale that lived alongside the
// code itself in v0.14.x and v0.15.x builds.
//
// ============================================================================
// ORIGINAL world_map.cpp HEADER + PER-VERSION COMMENT BLOCKS (v0.14.31 - v0.14.102)
// Archived 2026-05-16 at v0.16.0 world_map.cpp split.
// ============================================================================

#pragma once

#if 0   // preserved-text block; never compiled.

// world_map.cpp - World map navigation TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.14.102 — Restore A=gas key injection for car AD.
//
//   v0.14.101 BAT (Wed 2026-05-06 18:58-18:59) confirmed diagnosis:
//   Aaron loaded save next to car, entered car (engine sound), pressed \
//   to drive to Balamb Garden. The [DIAG-LOCO] trace showed:
//     - At drive start: locomotion=0x06 (decimal 6), s_lastVehicle=6
//       (NOT byte 21 from v0.14.100; byte 21 was the rental shop's
//       transitional state; once Aaron actually got in the car the byte
//       stayed at 6 = Selphie foot, even though he WAS demonstrably in
//       the car — confirmed by the game's distinctive engine running
//       sound and by Aaron's testimony that he heard AD's distance
//       announcements with distances NEVER changing).
//     - 5 stuck checks: locomotion=0x06, s_lastVehicle=6, player=
//       (16031,-26948) frozen across all 5 windows. AD's distance
//       announcements consistent with frozen position.
//
//   Diagnosis (c) confirmed: AD is reading the on-foot character's
//   position which doesn't update while in a vehicle (the foot
//   character is hidden inside the car), AND AD's keystrokes don't
//   actually move the car because the gas pedal isn't being held.
//   Aaron's reminder ("car AD was working before the whole Sonnet
//   regression") was the decisive clue. conversation_search recovered
//   the v0.11.13/v0.11.14 design from past Claude sessions: the
//   previous Claude session empirically confirmed via F12 key-state
//   diagnostic that the rental car uses the A KEY (VK=0x41, scan=0x1E)
//   as the gas pedal that must be HELD continuously, and W (VK=0x57,
//   scan=0x11) for reverse. ARROW KEYS STEER ONLY — they don't
//   accelerate the car. The v0.11.14 design always injected A whenever
//   wantUp=true — harmless on foot (A is unbound for walking; the
//   engine ignores it) and essential for car. The v0.14.x rewrite
//   during the "Sonnet regression" lost this entirely, leaving
//   SetDriveKeys with only UP/LEFT/RIGHT arrows. Cars receive arrow
//   keys and steer correctly but never accelerate because no gas pedal
//   is held. Foot AD works because foot uses arrow keys directly.
//
//   CRUCIAL DETAIL: A and W are NOT extended keys (per v0.11.14: 'NOT
//   extended keys (no KEYEVENTF_EXTENDEDKEY)'); arrow keys ARE
//   extended. The key-injection helpers must handle both flag types.
//
//   THIS BUILD ships four small changes:
//
//   (1) PressKey/ReleaseKey gain an 'extended' parameter (default true
//       preserves backward compatibility for all existing arrow-key
//       call sites). When extended=false, KEYEVENTF_EXTENDEDKEY is
//       omitted and the OS sends a normal letter-key scan code.
//
//   (2) New static bool s_keyGasHeld tracks the A-key press state
//       alongside the existing s_keyUp/Left/Right flags.
//
//   (3) SetDriveKeys: when up=true, also press A (scan 0x1E, NOT
//       extended) via PressKey('A', 0x1E, false). When up=false,
//       release A. Always-inject behavior (no car-detection gate)
//       because the locomotion byte cycles to non-canonical values
//       while driving (e.g. 21 for the rental car, 6 cycling here)
//       so reliable car detection isn't available. Always-inject is
//       harmless on foot (A unbound) and essential for car.
//
//   (4) ReleaseAllDriveKeys: also release A when held.
//
//   ALSO removed the v0.14.101 [DIAG-LOCO] logging at drive-start and
//   stuck-check sites, since we now know the answer and don't need
//   the byte-tracking trace anymore.
//
//   NOT shipping W (reverse) yet — forward-only AD doesn't need
//   reverse for normal navigation, and v0.11.14's forest-stuck
//   recovery (reverse-turn-forward state machine) is a separate
//   feature deferred to v0.14.103+ if needed (forest avoidance might
//   work fine for cars now that gas works).
//
//   v0.14.100 hardcoded refined-coord baseline UNCHANGED. v0.14.100
//   bearing-based final-approach steering UNCHANGED. v0.14.99 sweep-
//   abort-on-drift UNCHANGED. v0.14.98 program 9 fix UNCHANGED.
//   v0.14.97 PLAN-DEBUG logging UNCHANGED. NO new addresses (A/W are
//   keyboard scan codes, not memory addresses). NO new hooks. NO
//   build script changes.
//
//   v0.14.102 BAT plan: from the same save Aaron used for v0.14.101
//   (foot, next to car), enter car, press + to select Balamb Garden,
//   press \ to drive. Verify in Logs/ff8_world.log:
//     (a) Drive starts cleanly with [DRIVE] Start → Balamb Garden line.
//     (b) Car ACTUALLY MOVES on screen (Aaron's primary signal: the
//         distance announcements should decrease over time).
//     (c) Drive arrives at Balamb Garden OR fails with normal stuck
//         detection (not the previous 'car never moves' failure).
//     (d) NO 'Stuck. Cannot reach destination' from frozen-position
//         stuck checks (the car should move at least 100 units in 3s).
//
//   If (b) succeeds and the drive arrives, the v0.11.13/v0.11.14 design
//   has been fully restored and car AD is back online.
//
//   If (b) fails again, we'd need to re-verify A is still the gas key
//   on Aaron's current setup (the v0.11.13/v0.11.14 testing was almost
//   a year ago in real time — Aaron may have rebound keys since). A
//   diagnostic build with F12 key-state dump would re-verify it
//   empirically (same approach the previous Claude session used).
//
//   Prior baseline:
//   v0.14.101 — DIAGNOSTIC build for the v0.14.100 BAT car-AD-no-
//                movement issue. Added [DIAG-LOCO] log lines at drive
//                start and stuck check showing locomotion byte +
//                player position. v0.14.101 BAT confirmed the position
//                was frozen at (16031,-26948) with locomotion=6 across
//                5 stuck checks; combined with Aaron's car-engine-sound
//                confirmation, this nailed down diagnosis (c): the car
//                never received gas-pedal input. v0.14.102 ships the
//                fix.
//
//   Prior baseline:
//   v0.14.100 — Balamb Town refined-coord baseline + bearing-
//                based final-approach steering. v0.14.99 BAT (on foot, save
//                loads at exact catalog (13249,-26779) for Balamb Town):
//                drive started with dist=0, planner correctly says 'Player
//                already in goal segment empty path', AD enters final
//                approach and emits wantUp=true (walk forward). Player
//                barely moves (62 units in 3000ms, then 0). Sweep activates
//                after 2 stuck checks, runs all 6 phases without progress,
//                declares 'Could not find entrance'. Drive 2 (started 2.4km
//                away after Aaron repositioned) used bearing-based out-of-
//                approach steering, got stuck for 6 checks, cleanly stopped
//                with 'Stuck. Cannot reach destination'. The v0.14.99
//                sweep-abort-on-drift fix WORKED for the second drive (no
//                multi-km wandering this time — stuck detection terminated
//                cleanly). The first drive exposed the deeper architectural
//                issue I'd noted as deferred.
//
//                ROOT CAUSE: Balamb Town's catalog (13249,-26779) is the
//                world-map ICON CENTER, NOT the actual gate trigger
//                position. Prior BATs captured refined entry coords that
//                show the actual gate is ~350 units WEST and ~70 units
//                NORTH at (12896,-26711). When AD targets the catalog and
//                the player is sitting AT the catalog with arbitrary save-
//                induced heading, walk-forward goes some random direction
//                — not necessarily toward the gate. AND refined coords
//                aren't persistent across sessions (queued for v0.15.x), so
//                the empirical (12896,-26711) value captured in v0.14.96/
//                v0.14.98 is gone on this fresh session.
//
//                TWO INTEGRATED CHANGES:
//
//                (1) HARDCODE Balamb Town's refined-coord baseline at
//                (12896, -26711) in Initialize. Sets s_refinedX/Y/Has[1]
//                to the empirical entry coord captured during v0.14.98
//                BAT. Bootstraps the refined-coord system on fresh
//                sessions until persistence ships (v0.15.x). Single
//                location for now — once v0.14.100 BAT confirms the
//                approach works, expand to Balamb Garden and Fire Cavern.
//
//                (2) BEARING-BASED FINAL-APPROACH STEERING when dist > 200.
//                Replaces the v0.11-era 'wantUp = true sweeps through the
//                trigger band' design with proper bearing logic for the
//                200..1000 unit range. Below 200 units, KEEP walk-forward
//                (avoids near-target oscillation that the original design
//                protected against). The original design was correct for
//                'player walking toward target from 500+ units offset' —
//                the sweep-through scenario — but breaks for 'player at
//                catalog center with arbitrary heading'. Bearing-based
//                steering auto-corrects from any starting orientation.
//                NEW threshold constant FINAL_APPROACH_FORWARD_DIST = 200
//                governs the transition.
//
//                Together these mean: AD targets refined (12896,-26711)
//                from player (13249,-26779), dist=360, bearing-based logic
//                turns the player west-northwest, walks toward refined,
//                reaches dist<200, walk-forward sweeps through the actual
//                gate trigger, [DRIVE] Arrival via game-mode fires.
//
//                v0.14.99 sweep-abort-on-drift UNCHANGED (still in place;
//                will fire if a battle ever drifts the player far).
//                v0.14.97 PLAN-DEBUG logging UNCHANGED. v0.14.98 program 9
//                fix UNCHANGED. v0.14.96 deferred-arrival flow UNCHANGED.
//                NO new addresses, NO new hooks, NO build script changes.
//
//                v0.14.100 BAT plan: from the same save Aaron used for
//                v0.14.99 (loads at exact catalog for Balamb Town, on foot,
//                story 205), press \\ to drive. Verify in ff8_world.log:
//                  (a) [INIT] Refined entry default: Balamb Town
//                      (12896,-26711) at module init,
//                  (b) [DRIVE] Using refined entry for Balamb Town:
//                      (12896,-26711) at drive start,
//                  (c) drive starts with dist~360 instead of dist=0,
//                  (d) bearing-based steering active (no immediate stuck
//                      checks; player moves toward refined coord),
//                  (e) drive completes via deferred-arrival OR fails cleanly
//                      via 6-stuck-check 'Stuck. Cannot reach destination'.
//                NO more 'Could not find entrance' from sweep exhausting
//                near the catalog.
//
//                RISK: bearing-based steering in 200-1000 range could cause
//                oscillation around target if engine turning is twitchy.
//                Mitigated by walk-forward below 200 units. If oscillation
//                manifests, v0.14.101 would tighten threshold or add
//                hysteresis.
//
//   Prior baseline:
//   v0.14.99 — Fix unreachable v0.14.97 sweep-abort-on-drift.
//                v0.14.98 second BAT (Aaron testing car AD from save
//                outside Balamb Town): drive started near target with dist
//                oscillating around the FINAL_APPROACH_DIST=1000 boundary.
//                Stuck-in-final-approach detection fired correctly, sweep
//                activated. First sweep walk drove the car 4000+ units west
//                in 3 seconds (cars are fast; locomotion byte read 0 due to
//                v0.14.94 hotfix blocking vehicle updates during AD, so
//                isOnFoot=true and 'wantUp=true for 3 seconds' translated to
//                4km of car travel). Random encounter at dist=4217. Re-entry
//                replanned correctly (player still in region 0x07 multi-
//                segment area, empty-path) BUT sweep state persisted across
//                re-entry. More sweep walks, more drift, more battles —
//                eventually exhausted at phase 6 declaring 'Could not find
//                entrance' at dist=6800 from target.
//
//                ROOT CAUSE: the v0.14.97 sweep-abort-on-drift check was
//                placed in the else branch of 'if (isOnFoot && dist <
//                DRIVE_FINAL_APPROACH_DIST)'. But the sweep state machine
//                has an early-return at the top of UpdateAutoDrive that
//                fires BEFORE the final-approach branch is ever evaluated.
//                So when sweep was active (the only time the abort would
//                matter), the abort code was unreachable. v0.14.98 first
//                BAT didn't catch this because sweep didn't activate during
//                that drive (Aaron started 596 units from target and never
//                got stuck long enough to trigger sweep).
//
//                FIX: move the sweep-abort check to BEFORE the sweep state
//                machine in UpdateAutoDrive. Same logic, reachable
//                placement. When player drifts to dist >
//                FINAL_APPROACH_DIST * 1.5 (1500 units) while sweep is
//                active, clear sweep state and let normal steering take
//                over. For cars this means a single sweep walk will trigger
//                abort on the next tick (one car walk easily covers 1500
//                units), preventing the cascade. For foot, single walks
//                stay within the 500-unit grace band so the abort only
//                fires when a battle has drifted the player.
//
//                v0.14.97 PLAN-DEBUG logging UNCHANGED. v0.14.98 program 9
//                fix UNCHANGED. v0.14.96 deferred-arrival flow UNCHANGED.
//                NO new addresses, NO new hooks, NO build script changes.
//
//   Prior baseline:
//   v0.14.98 — Planner-decline FIX. v0.14.97 BAT'd 2026-05-06
//                17:07:01 with full PLAN-DEBUG trace. Confirmed the v0.14.96
//                hypothesis exactly: Balamb Town's region 0x07 fell out of
//                the active set because program 9 (loc_id=0x010B) was
//                rejected with 'SKIP top_story=[290,0) story=205 out of
//                window'. FIX: change s_triggerPrograms[9].top_story_gte
//                from 290 to 0. The disassembler put 0xFF02 0x0122 in the
//                wrong scope; that operand belongs to clause 0 (creating
//                effective window [290, 490) for region 0x06), not to the
//                program top.
//
//   Prior baseline:
//   v0.14.97 — Planner-decline diagnostic + sweep gating fix. Added
//                [PLAN-DEBUG] CLAUSE-REJECTION TRACE in MatchProgramForCatalog;
//                SWEEP ABORT ON DRIFT in UpdateAutoDrive.
//
//   Prior baseline:
//   v0.14.96 — Deferred-arrival state machine using settled game mode
//                AFTER world-map exit. Decision table:
//                  MODE_FIELD (1)        : ARRIVAL
//                  MODE_SWIRL (3) / MODE_BATTLE (999) / MODE_AFTER_BATTLE (4): ENCOUNTER
//                Wait timeout: ARRIVAL_DECISION_TIMEOUT_MS = 2000ms.
//
//   Prior baseline:
//   v0.14.95 — Chapter 3 Stage 5.1: planner correctness fix. Rewrote
//                MatchProgramForCatalog with closest-active-region search
//                (5-segment cap). Sections 9 and 19 explored as region-map
//                candidates and disproved.
//
//   v0.14.94 — Chapter 3 Stage 5: auto-drive A* path-planning refactor.
//                VEHICLE-NOISE HOTFIX, SECTION 2 LOADER (32x24 region map
//                into s_segmentRegionMap), A* PATH PLANNER replacing
//                v0.14.86 linear-direction steering.
//
//   v0.14.93 — 38 decoded field-entry trigger programs embedded as
//              static s_triggerPrograms[]. Section 2 in dump list.
//   v0.14.92 — Section 8 hex dump that drove the decode.
//   v0.14.86 — Auto-drive restored from v0.11.08 baseline.
//   v0.11.16 — Deferred catalog build (position validity check).
//   v0.14.31 — Update()/Shutdown() restored after v0.14.24 build damage.
//
// Note: The unabridged per-version commentary (including full BAT plans
// and root-cause narratives for v0.14.93 through v0.14.101) is preserved
// in git history at tag v0.15.13.2. To retrieve any pre-v0.16.0 block in
// full, use:
//   git show v0.15.13.2:src/world_map.cpp | head -609
// ============================================================================

#endif  // preserved-text block
