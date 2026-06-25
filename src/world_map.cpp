// world_map.cpp - World map navigation TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.16.0 -- world_map.cpp split into focused .inl files.
//
// The 222 KB / 4452-line monolith of v0.15.13.2 has been carved into 10 files:
//   - world_map_history.h       narrative archive of v0.14.31 through v0.15.13.2
//   - world_map_state.inl       enums, structs, state arrays, constants
//   - world_map_segments.inl    coord readers, archive I/O, terrain grid,
//                               vehicle classification, region map loader
//   - world_map_trigger_data.inl 38 wmsetus.obj Section 8 trigger programs +
//                                LogTriggerPrograms
//   - world_map_catalog.inl     LocationEntry data, BFS reachability,
//                               distance-sorted catalog builder, vehicle
//                               state tracker, GetVehicleName
//   - world_map_announce.inl    AnnounceLocation, AnnounceBearing
//   - world_map_planner.inl     A* planner, IsLocationFootFriendly, and the
//                               new v0.16.0 ComputePlannerEligibility helper
//   - world_map_drive.inl       AD lifecycle: PressKey/SetDriveKeys/Stop/Start
//                               (with v0.16.0 Part C eligibility gate)/Sweep/
//                               UpdateAutoDrive
//   - world_map_arrival.inl     ResolveDeferredArrival (with v0.16.0 Part B
//                               off-target distance cap; INCLUDED AFTER
//                               drive.inl because it calls StopAutoDrive)
//   - world_map_keys.inl        PollKeys (catalog cycle, bearing, AD toggle)
//
// THREE NEW v0.16.0 BEHAVIORS:
//   Part B (arrival.inl)  - DRIVE_ARRIVAL_MAX_DIST = 2500. When MODE_FIELD
//                            fires but the player is >2500 units from target,
//                            refuse to declare arrival and refuse to capture
//                            a refined coord.
//   Part C (drive.inl)    - StartAutoDrive checks s_destPlannerEligible[catIdx]
//                            before calling PlanDrivePath. Ineligible
//                            destinations skip the planner entirely.
//   ComputePlannerEligibility (planner.inl) - run once at Initialize, walks
//                            every catalog entry and marks it eligible iff a
//                            foot-vehicle clause exists for that destination's
//                            region.
//
// History for v0.14.31 through v0.15.13.2 is in world_map_history.h. Git
// history (v0.15.13.2 tag) holds the full pre-split monolith for retrieval.
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "world_map.h"

// Forward declarations for namespaces used by the .inl files below.
namespace Log { void World(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); }

namespace WorldMap {

// .inl include chain. ORDER MATTERS:
//   - state.inl first: declares types/state used by every later file.
//   - geometry.inl: pure coord/segment/BFS math, no Win32/SEH so it is
//     host-compilable; tests/world_map_harness.cpp guards it (#67/#65).
//   - segments.inl: pure math, archive I/O.
//   - trigger_data.inl: needs state.inl structs.
//   - catalog.inl: defines LOCATION_COUNT used by planner.inl.
//   - announce.inl: depends on state + segments + catalog only.
//   - planner.inl: A* + ComputePlannerEligibility (needs s_locations).
//   - drive.inl: defines StopAutoDrive / StartAutoDrive / UpdateAutoDrive.
//   - arrival.inl: ResolveDeferredArrival CALLS StopAutoDrive, so it
//     must come AFTER drive.inl (forward decl wouldn't work because
//     all four files are in the same translation unit; the compiler
//     needs the definition visible when arrival.inl is compiled).
//   - keys.inl last: calls StartAutoDrive + StopAutoDrive (drive.inl)
//     and AnnounceLocation + AnnounceBearing (announce.inl).
#include "world_map_state.inl"
#include "world_map_geometry.inl"
#include "world_map_navmesh.inl"
#include "world_map_segments.inl"
#include "world_map_trigger_data.inl"
#include "world_map_catalog.inl"
#include "world_map_announce.inl"
#include "world_map_planner.inl"
#include "world_map_drive.inl"
#include "world_map_heading_scan.inl"
#include "world_map_camera_scan.inl"
#include "world_map_arrival.inl"
#include "world_map_keys.inl"

// ============================================================================
// Main polling loop
// ============================================================================
void Poll()
{
    bool nowOnWorldMap = IsOnWorldMap();

    // Detect world map entry
    if (nowOnWorldMap && !s_onWorldMap) {
        s_onWorldMap = true;
        s_wmEntryTick = GetTickCount();   // v0.14.90.3: arm locomotion-byte suppression
        s_catalogBuilt = false;
        Log::World("WorldMap: Entered world map");

#if WM_RUNTIME_WALK_DIAG
        // v0.18.3.102: arm the one-shot runtime-walkmesh dump and reset its
        // gating counters. The dump is fired from Poll() once the mesh is
        // actually populated (position valid + settle delay + descriptor
        // entries read as plausible pointers), not merely once [0x020402DC]
        // is non-zero -- the .101 BAT proved that fires before world.fs has
        // finished streaming the mesh.
        s_rtWalkDumpPending = true;
        s_rtWalkSettleTicks = -1;
        s_rtWalkPollTicks   = 0;
#endif

#if HEADING_SCAN_DIAG
        HScanResume();   // #67: resume a heading scan that an encounter interrupted
#endif
#if CAMERA_SCAN_DIAG
        CamScanResume(); // #67: resume a camera diagnostic an encounter interrupted
#endif

        if (s_driveActive) {
            // v0.14.88: drive paused during a random encounter, now resuming.
            DWORD now = GetTickCount();
            s_driveLastAnnounce      = now;
            s_driveStuckCheckTime    = now;
            s_driveStuckCount        = 0;
            s_finalApproachEnterTick = 0;
            int32_t rx, ry, rz;
            GetWorldMapPosition_Active(&rx, &ry, &rz);
            if (rx != 0 || ry != 0) {
                s_driveStuckX = rx;
                s_driveStuckY = ry;
                Log::World("WorldMap: [DRIVE] Replanning after world-map re-entry from (%d,%d)", rx, ry);
                // v0.16.0.2: gate replan on planner-eligibility. Without this, the resume
                // path runs PlanDrivePath unconditionally and the closest-active-region
                // fallback converts a planner-ineligible drive (planned=0) into a
                // misrouted planner drive (planned=1). Observed in Fire Cavern BAT
                // 14:39:00 where resume after random-encounter swirl turned a simple-
                // coord drive into a planner walk toward a different region.
                if (s_drivePlannerEligible) {
                    PlanDrivePath(rx, ry);
                } else {
                    Log::World("WorldMap: [DRIVE] Planner-ineligible destination -- keeping simple-coord steering, not replanning");
                }
            }
            char buf[160];
            snprintf(buf, sizeof(buf), "Resuming drive to %s.", s_driveTargetName);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: [DRIVE] Resumed after world-map re-entry -> %s",
                       s_driveTargetName);
        } else {
            ScreenReader::Speak("World map.", true);
        }

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

#if HEADING_SCAN_DIAG
        HScanPause();   // #67: pause a running heading scan (encounter / field entry)
#endif
#if CAMERA_SCAN_DIAG
        CamScanPause(); // #67: pause a running camera diagnostic (encounter / field entry)
#endif

        // v0.14.96: defer the arrival decision; ResolveDeferredArrival resolves
        // based on the settled game mode (MODE_FIELD = arrival, battle modes =
        // encounter). v0.16.0 Part B added a distance cap inside the arrival
        // branch so accidental wrong-field entries don't poison s_refined*.
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

    // v0.14.96: resolve deferred arrival decision while off world map. Runs
    // BEFORE the early-return below so it's called every Poll tick until the
    // decision settles or times out.
    if (!s_onWorldMap && s_driveAwaitingArrivalDecision) {
        ResolveDeferredArrival();
    }

    if (!s_onWorldMap) return;

    if (!s_catalogBuilt) {
        BuildDistanceCatalog();
    }

    CheckVehicleChange();
    UpdateAutoDrive();

#if WM_RUNTIME_WALK_DIAG
    // v0.18.3.102: re-gated runtime-walkmesh dump. The .101 BAT proved that
    // [0x020402DC] (descBase) goes non-zero at world-map ENTRY, before world.fs
    // finishes streaming the mesh -- so firing on descBase!=0 read garbage
    // (player pos still (0,0), polyCount=11, descriptor entries 0xFFFF914E /
    // 0x00000268...). Now we keep polling and only fire once the mesh looks
    // populated: descBase set AND player position valid for a short settle AND
    // the first two descriptor entries read as plausible pointers. A bounded
    // fire-anyway budget guarantees we still get a dump even if it never
    // settles -- and if THAT dump still shows garbage after the mesh is
    // definitely loaded, the structural interpretation (not the timing) is
    // wrong, which is itself the answer we need.
    if (s_rtWalkDumpPending) {
        const uint32_t RT_PTR_LO  = 0x00010000u;  // below this = offset/garbage (saw 0x268..0x484)
        const uint32_t RT_PTR_HI  = 0x7FFF0000u;  // above this = kernel/garbage (saw 0xFFFF914E)
        const int      RT_SETTLE  = 30;           // ticks position must stay valid before trusting the mesh
        const int      RT_POLL_MAX = 1200;        // fire-anyway budget so we never silently never-fire

        uint32_t descBase = 0, footX = 0, footY = 0, adj0 = 0, adj1 = 0;
        __try {
            descBase = *(const uint32_t*)0x020402DCu;
            footX    = *(const uint32_t*)0x0203EE80u;   // foot-X (also the aliased "adj ptr" slot)
            footY    = *(const uint32_t*)0x0203EE84u;   // foot-Y
            if (descBase) {
                adj0 = *(const uint32_t*)(descBase + 0 * 12);
                adj1 = *(const uint32_t*)(descBase + 1 * 12);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        static uint32_t s_lastBase = 0xDEADBEEF;
        if (descBase != s_lastBase) {
            Log::World("WorldMap: [RTWALK] poll [020402DC]=0x%08X foot=(0x%08X,0x%08X) adj0=0x%08X adj1=0x%08X",
                       descBase, footX, footY, adj0, adj1);
            s_lastBase = descBase;
        }

        bool posValid = (footX != 0 || footY != 0);
        if (!posValid) {
            s_rtWalkSettleTicks = -1;                       // scene not initialized yet
        } else if (s_rtWalkSettleTicks < 0) {
            s_rtWalkSettleTicks = 0;                        // just became valid
            Log::World("WorldMap: [RTWALK] position valid (0x%08X,0x%08X) -- starting %d-tick settle",
                       footX, footY, RT_SETTLE);
        } else {
            s_rtWalkSettleTicks++;
        }

        bool adj0ok = (adj0 >= RT_PTR_LO && adj0 < RT_PTR_HI);
        bool adj1ok = (adj1 >= RT_PTR_LO && adj1 < RT_PTR_HI);
        // v0.18.3.103: adj-sanity is now LOGGED, not gated. The .102 BAT showed the
        // gate correctly suppressed the premature entry-tick dump -- but then NEVER
        // fired, because adj0/adj1 stayed 0xFFFFxxxx (out of pointer range) and the
        // 1200-tick fire-anyway budget wasn't reached in the session window, so we
        // never got the post-load probe (the whole point). Fire on scene-init alone
        // (descBase + position valid + settle), regardless of whether the entries
        // look like pointers; the dump itself reveals whether the descriptor model
        // is right (sane neighbour indices) or wrong (still 0xFFFFxxxx, polyCount ok).
        bool ready  = (descBase != 0) && posValid && (s_rtWalkSettleTicks >= RT_SETTLE);
        bool fireAnyway = (++s_rtWalkPollTicks >= RT_POLL_MAX);

        if (ready || fireAnyway) {
            s_rtWalkDumpPending = false;
            Log::World("WorldMap: [RTWALK] firing dump (%s): descBase=0x%08X settle=%d polled=%d adj0=0x%08X(ptr?%d) adj1=0x%08X(ptr?%d)",
                       ready ? "mesh-ready" : "FIRE-ANYWAY budget hit -- data may be incomplete",
                       descBase, s_rtWalkSettleTicks, s_rtWalkPollTicks, adj0, adj0ok ? 1 : 0, adj1, adj1ok ? 1 : 0);
            DumpRuntimeWalkability();
        }
    }
#endif

    // Track significant position changes (placeholder for future features).
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    static int32_t lastX = 0, lastY = 0;

    double movement = CalculateWrappedDistance(px, py, lastX, lastY);
    if (movement > 1000) {
        s_lastMovementTick = GetTickCount();
        lastX = px;
        lastY = py;
    }

#if WM_CALIB_DIAG
    // #67 calibration: trace live pos -> segment -> terrain + region as the
    // player walks (one line per new cell). Passive read; retire by flipping
    // WM_CALIB_DIAG to 0 in world_map_segments.inl.
    PollWorldCalibDiag(px, py);
#endif

#if HEADING_SCAN_DIAG
    UpdateHeadingScan();   // #67: live-facing discovery state machine (F12)
#endif
#if CAMERA_SCAN_DIAG
    UpdateCameraScan();    // #67: camera-control discovery state machine (F12)
#endif

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

    s_wmEntryTick = 0;
    s_pendingVehicle = -1;
    s_pendingVehicleCount = 0;

    s_driveActive = false;
    s_driveTargetX = 0;
    s_driveTargetY = 0;
    s_driveTargetName[0] = '\0';
    s_driveStuckCount = 0;
    s_driveApproachAnnounced = false;
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    s_driveOnFootAtStart = true;
    s_driveLastPosX = 0;
    s_driveLastPosY = 0;

    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;

    s_driveAwaitingArrivalDecision = false;
    s_driveExitTick                = 0;

    memset(s_refinedX, 0, sizeof(s_refinedX));
    memset(s_refinedY, 0, sizeof(s_refinedY));
    memset(s_refinedHas, 0, sizeof(s_refinedHas));

    // v0.14.100: hardcoded refined-coord defaults for known-good entry points.
    // v0.16.0.2: Fire Cavern added — captured from v0.16.0.1 BAT lastPos
    // at the OFF-TARGET stop (dist=6561 from icon (36864,-28672)). This is
    // the position of the approach-field "Fire Cavern A" trigger, ~6.5k
    // units southwest of the icon. Hardcoding eliminates the off-target
    // stop on the very first Fire Cavern drive of a new install/session.
    for (int i = 0; i < (int)(sizeof(s_locations)/sizeof(s_locations[0])); i++) {
        if (strcmp(s_locations[i].name, "Balamb Town") == 0) {
            s_refinedX[i]   = 12896;
            s_refinedY[i]   = -26711;
            s_refinedHas[i] = true;
            Log::World("WorldMap: [INIT] Refined entry default: %s (%d,%d)",
                       s_locations[i].name, s_refinedX[i], s_refinedY[i]);
        }
        else if (strcmp(s_locations[i].name, "Fire Cavern") == 0) {
            s_refinedX[i]   = 30326;
            s_refinedY[i]   = -29221;
            s_refinedHas[i] = true;
            Log::World("WorldMap: [INIT] Refined entry default: %s (%d,%d)",
                       s_locations[i].name, s_refinedX[i], s_refinedY[i]);
        }
    }

    memset(s_segmentRegionMap, 0xFF, sizeof(s_segmentRegionMap));
    s_segmentRegionLoaded = false;

    ReleaseAllDriveKeys();

    if (LoadTerrainGrid()) {
        Log::World("WorldMap: [INIT] Terrain grid loaded successfully");
    } else {
        Log::World("WorldMap: [INIT] Terrain grid load failed -- catalog will be unfiltered");
    }

#if NAVMESH_DIAG
    // v0.18.3.104: BAT 1 navmesh connectivity probe. Flood the true navmesh
    // from the Galbadia save coord (the offline reference start) and log which
    // catalog destinations are reachable + an A* route to Dollet, to confirm
    // the in-game build matches the offline numbers (157416 tris / 253 comps /
    // largest 74308; Dollet/Timber/Galbadia Garden/Deling City reachable;
    // A* ref->Dollet ~30259 / 126 tris). s_locations + LOCATION_COUNT +
    // NM_CLIMB_STEP are all visible here (after the full .inl chain).
    Navmesh_LogConnectivity(s_locations, LOCATION_COUNT, -29270, -24056, NM_CLIMB_STEP);
#endif

    if (LoadTriggerZones()) {
        Log::World("WorldMap: [INIT] Trigger-zone hex dump complete (see [TRIGGER-DUMP] entries above)");
        if (s_segmentRegionLoaded) {
            Log::World("WorldMap: [INIT] Segment region map loaded for AD path planning");
        } else {
            Log::World("WorldMap: [INIT] Segment region map NOT loaded -- AD will fall back to catalog-center steering");
        }
    } else {
        Log::World("WorldMap: [INIT] Trigger-zone load failed -- see preceding [TRIGGER-DUMP] entries");
    }

#if WM_CALIB_DIAG
    // #67 calibration: dump the game's own region land/sea grid once at init,
    // for overlay against the [TERRAIN] classifier grid above.
    DumpRegionGridDiag();
#endif

    LogTriggerPrograms();

    // v0.16.0 Part C: compute per-destination planner eligibility from the
    // loaded region map + embedded trigger programs. Must run AFTER
    // LoadTriggerZones (so s_segmentRegionMap is populated) but the function
    // is internally safe to call when LoadTriggerZones failed -- it logs
    // "s_segmentRegionMap not loaded" and leaves all flags at false.
    ComputePlannerEligibility();

    s_destFootFriendly = true;

    Log::World("WorldMap: Module initialized (v%s)", FF8OPC_VERSION);
}

void Update()
{
    Poll();
}

void Shutdown()
{
    if (s_driveActive) {
        StopAutoDrive(nullptr);
    } else {
        ReleaseAllDriveKeys();
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
