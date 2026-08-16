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
#include <mmsystem.h>          // v0.18.3.172: PlaySound for the teleport audio cue
#pragma comment(lib, "winmm.lib")
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <queue>          // #80: Garden planner priority queue
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "resources.h"        // v0.18.3.172: IDR_WAV_TELEPORT
#include "world_map.h"

// Forward declarations for namespaces used by the .inl files below.
// v0.20.87: the Garden drive must know when a scripted cutscene has the floor.
namespace FieldDialog { bool IsDialogOpen(); }
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
#include "world_map_vehsig.inl"          // v0.21.0 (#79): the foot/vehicle discriminator's policy, unit-tested
#include "world_map_state.inl"
#include "world_map_geometry.inl"
#include "world_map_navmesh.inl"
#include "world_map_segments.inl"
#include "world_map_trigger_data.inl"
#include "world_map_trigeval.inl"        // v0.21.2 (#79): the entry trigger, evaluated the way the GAME evaluates it
#include "world_map_trigwalk.inl"        // v0.21.5 (#79): the whole program set, walked live -- a known-good and a known-bad through identical code
#include "world_catalog.inl"
#include "world_map_announce.inl"
#include "world_map_planner.inl"
#include "world_map_planner2.inl"   // v0.18.3.225: planner part 2 (grid A* + PlanDrivePath), split from planner.inl for the 80 KB CI guard
#include "world_map_routenet.inl"   // v0.18.3.209 (#70): validated route network -- data + RouteNetPlan (uses planner helpers; PlanDrivePath calls in via forward decl)
#include "world_map_drive_helpers.inl"   // v0.18.3.225: AD lifecycle helpers (split from drive.inl for the 80 KB CI guard)
#include "world_garden_dump.inl"         // #80 diagnostic: runtime world-map polygon dump (gated off)
#include "world_garden_grid.inl"         // #80: mobile Balamb Garden -- the traversability grid and its build (v0.20.63 split)
#include "world_garden_berths.inl"       // #80: the berth (park-point) table -- split out of world_garden.inl at v0.20.74 for the 80 KB CI guard
#include "world_garden_plan.inl"         // #80: reachability, docks, aboard latch, A* -- split out of world_garden.inl at v0.20.84 for the same guard
#include "world_garden_probe.inl"        // #80: collision probes -- split out at v0.20.94
#include "world_garden.inl"              // #80: reachability, planner, dock tables and executor; runs only when the engine vehicle id is 0x30
#include "world_map_drive.inl"           // UpdateAutoDrive (textually includes world_map_drive_exec.inl mid-body)
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
        Garden_OnWorldMapEntry();   // #80: arm the aboard/on-foot classification
        Log::World("WorldMap: Entered world map");
        // v0.18.3.255 (#79): capture the vehicle-state candidates the instant
        // the world map loads (scenes can spawn the player ALREADY aboard the
        // exam car / mobile Garden / Ragnarok). Diagnostic only.
        DumpVehicleState("entry");

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

        if (s_driveActive && s_drivePausedInField &&
            (GetTickCount() - s_drivePauseTick) > 300000) {
            // v0.18.3.203: the off-target field pause went stale (player stayed off the
            // world map > 5 minutes -- they've moved on to something else). Cancel quietly.
            s_drivePausedInField = false;
            StopAutoDrive("Auto-drive cancelled.");
        }
        if (s_driveActive) {
            // v0.14.88: drive paused during a random encounter OR paused in an off-target
            // field (v0.18.3.203), now resuming. The replan below already consults the
            // learned trigger circles, and a circle we spawned inside is exempt (disarmed).
            s_drivePausedInField = false;
            DWORD now = GetTickCount();
            s_driveLastAnnounce      = now;
            s_driveStuckCheckTime    = now;
            s_driveStuckCount        = 0;
            s_finalApproachEnterTick = 0;
            s_driveWatchdogGen++;    // v0.18.3.216: reseed route-progress + give-up clocks
            // v0.18.3.220: DEFER the replan ~20 world ticks. The immediate replan
            // read a stale position on the first re-entry frame (engine hadn't
            // updated the foot DWORDs yet), planted the hop-on point inside the
            // just-exited location's firing area, and steering re-entered the
            // field (BG->Fire Cavern BAT 23:54). Steering holds until the
            // deferred replan runs below with a settled position.
            s_driveResumeReplanTicks = 20;
            Log::World("WorldMap: [DRIVE] Resume replan deferred 20 ticks (position settle)");
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
        // #80: a Garden drive never ends in a field entry -- the Garden cannot
        // fire any trigger program except its own (locID 0x0172). So leaving
        // the world map while piloting means a battle or a scripted event, not
        // an arrival: release the keys and drop the drive rather than waiting
        // on an arrival decision that will never come.
        if (Garden_Active()) {
            // #80: on a DOCKING run (Fisherman's Horizon) the world map handing
            // off to a field IS the arrival -- driving the hull in is what
            // fires it. On any other Garden drive it is a battle or an event.
            char gbuf[256];
            if (Garden_LeavingIsArrival(gbuf, sizeof(gbuf))) Garden_Stop(gbuf);
            else                                             Garden_Stop("Garden navigation interrupted.");
        }
        if (s_driveActive) {
            ReleaseAllDriveKeys();
            s_driveAwaitingArrivalDecision = true;
            s_driveExitTick = GetTickCount();
            Log::World("WorldMap: [DRIVE] Awaiting arrival decision (target=%s, dist=%.0f, lastPos=(%d,%d), planned=%d)",
                       s_driveTargetName, s_driveLastDist,
                       s_driveLastPosX, s_driveLastPosY,
                       s_drivePathPlanned ? 1 : 0);
        } else {
            // v0.18.3.198: no auto-drive, but the player still left the world map --
            // arm manual-entry capture so a plain walk-in pins the location. Resolved
            // by ResolveManualArrival() once the game mode settles to a field.
            s_manualArrivalPending = true;
            s_manualArrivalTick    = GetTickCount();
            s_manualArrivalPosX    = s_lastWorldPosX;
            s_manualArrivalPosY    = s_lastWorldPosY;
            Log::World("WorldMap: [DRIVE] Manual world-map exit -- arming entry capture (lastPos=(%d,%d))",
                       s_lastWorldPosX, s_lastWorldPosY);
        }
    }

    // v0.14.96: resolve deferred arrival decision while off world map. Runs
    // BEFORE the early-return below so it's called every Poll tick until the
    // decision settles or times out.
    if (!s_onWorldMap && s_driveAwaitingArrivalDecision) {
        ResolveDeferredArrival();
    }

    // v0.18.3.198: resolve a manual (non-drive) field entry the same way.
    if (!s_onWorldMap && s_manualArrivalPending) {
        ResolveManualArrival();
    }

    if (!s_onWorldMap) return;

    // #80: resolve "is the player piloting the Garden" BEFORE the catalog is
    // built. The v0.20.54 BAT proved the engine vehicle id is 0 at every entry
    // and every catalog build (it only names a vehicle while one is MOVING),
    // so this has to be a latched decision rather than a spot read.
    Garden_UpdateAboard();

    if (!s_catalogBuilt) {
        BuildDistanceCatalog();
    }

    // v0.18.3.220: deferred resume replan (see the re-entry block above).
    // Hold ALL drive processing until the engine position has settled, then
    // replan from the live position. Keys were released at pause; nothing
    // steers during the hold.
    if (s_driveActive && s_driveResumeReplanTicks > 0) {
        if (--s_driveResumeReplanTicks > 0) return;
        int32_t rx, ry, rz;
        GetWorldMapPosition_Active(&rx, &ry, &rz);
        if (rx != 0 || ry != 0) {
            s_driveStuckX = rx;
            s_driveStuckY = ry;
            Log::World("WorldMap: [DRIVE] Replanning after world-map re-entry from (%d,%d) [deferred]", rx, ry);
            // v0.16.0.2: gate replan on planner-eligibility. Without this, the resume
            // path runs PlanDrivePath unconditionally and the closest-active-region
            // fallback converts a planner-ineligible drive (planned=0) into a
            // misrouted planner drive (planned=1).
            if (s_drivePlannerEligible) {
                PlanDrivePath(rx, ry);
            } else {
                Log::World("WorldMap: [DRIVE] Planner-ineligible destination -- keeping simple-coord steering, not replanning");
            }
            // v0.18.3.221: re-arm the firing-area escape from the settled
            // resume position (the resume spawn can land inside a non-target
            // area exactly like the initial start).
            ArmFiringAreaEscape(rx, ry);
        }
    }

    CheckVehicleChange();
    // v0.21.2 (#79): once a second, what the GAME's own entry test is seeing --
    // segment index, whose position it is using, the story word and the UNK21
    // bit. Read-only. See the note at the top of world_map_trigeval.inl.
    TriggerEvalTick();
    // #80: the Garden has its own executor. When it owns the drive the
    // foot/car one is not ticked at all -- the two never run together.
    if (Garden_Active()) {
        Garden_Update();
    } else {
        UpdateAutoDrive();
    }

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
            // v0.18.3.177: RAW HEX dump of the runtime walkmesh region. The structured interpretation
            // above has never been solved (adjacency reads fault), so dump the raw bytes at descBase and
            // re-derive the real layout OFFLINE by cross-referencing wmx.obj -- this is what will let me
            // fix the ~15% height-model errors at overhang cells (engine surfaces our mesh lacks). Once.
            {
                static bool s_rtRawDone = false;
                if (!s_rtRawDone && descBase >= 0x00010000u && descBase < 0x7FFF0000u) {
                    s_rtRawDone = true;
                    Log::World("WorldMap: [RTRAW] descBase=0x%08X dumping 8192 bytes (32/line)", descBase);
                    for (int off = 0; off < 8192; off += 32) {
                        char hex[80]; int p = 0; bool fault = false;
                        __try {
                            for (int b = 0; b < 32; b++)
                                p += sprintf(hex + p, "%02X", *(const unsigned char*)(descBase + off + b));
                        } __except (EXCEPTION_EXECUTE_HANDLER) { fault = true; }
                        if (fault) { Log::World("WorldMap: [RTRAW] +%04X FAULT", off); break; }
                        Log::World("WorldMap: [RTRAW] +%04X %s", off, hex);
                    }
                }
            }
        }
    }
#endif

    // Track significant position changes (placeholder for future features).
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);

    // v0.18.3.198: remember the latest valid world-map position every frame so
    // that when the map exits (Poll returns early that frame, before this read)
    // ResolveManualArrival has the on-foot entry point to attribute a capture.
    if (px != 0 || py != 0) { s_lastWorldPosX = px; s_lastWorldPosY = py; }

#if NAVMESH_DIAG && GROUNDH_VALIDATE
    // #70 v0.18.3.140 (Stage 1): validate WorldGroundHeight vs the engine's live
    // ground height at 0x0203FE30, on EVERY world-map frame -- so it logs while the
    // player walks MANUALLY (Galbadia + the Balamb save), not only during a wedged
    // auto-drive. Logs the chosen triangle + 3 corner heights + barycentric weights:
    // does the engine's height sit WITHIN our triangle (right surface?), and is the
    // ~92u offset constant across terrain/continents (constant -> cancels in the
    // Stage 2 step gate)? Throttled to 250ms; read-only.
    {
        static DWORD s_ghPollLast = 0;
        DWORD ghNow = GetTickCount();
        if (ghNow - s_ghPollLast >= 60) {   // v0.18.3.177: 250->60ms, 4x denser engine-truth height samples for sim fidelity
            int tri = -1, h0 = 0, h1 = 0, h2 = 0;
            double ba = 0.0, bb = 0.0, bc = 0.0;
            int ourH = WorldGroundHeight(px, py, &tri, &h0, &h1, &h2, &ba, &bb, &bc);
            int engH = (int)(*(volatile int32_t*)0x0203FE30);
            if (ourH == WGH_NO_GROUND)
                Log::World("WorldMap: [GROUNDH] pos(%d,%d) fine(c%d,r%d) tri=NONE engineH=%d",
                           px, py, WorldXToFineCol(px), WorldYToFineRow(py), engH);
            else
                Log::World("WorldMap: [GROUNDH] pos(%d,%d) fine(c%d,r%d) tri=%d corners(%d,%d,%d) bary(%.2f,%.2f,%.2f) ourH=%d engineH=%d diff=%d",
                           px, py, WorldXToFineCol(px), WorldYToFineRow(py),
                           tri, h0, h1, h2, ba, bb, bc, ourH, engH, ourH - engH);

            // v0.18.3.141 (#70 Stage 1): BLOCK-LOCAL query beside the global [GROUNDH].
            // Does replicating the engine's per-block search fix the overhang mispick
            // (Balamb Garden ourH -201 vs engine -545) and stop the beach extrapolation?
            // 'contain' shows how many triangles cover the point (overlap visibility).
            {
                int ltri = -1, lh0 = 0, lh1 = 0, lh2 = 0, lcnt = 0;
                double la = 0.0, lb = 0.0, lc = 0.0;
                int ourHL = WorldGroundHeightLocal(px, py, &ltri, &lh0, &lh1, &lh2, &la, &lb, &lc, &lcnt);
                if (ourHL == WGH_NO_GROUND)
                    Log::World("WorldMap: [GROUNDHL] pos(%d,%d) tri=NONE contain=%d engineH=%d", px, py, lcnt, engH);
                else
                    Log::World("WorldMap: [GROUNDHL] pos(%d,%d) tri=%d contain=%d corners(%d,%d,%d) bary(%.2f,%.2f,%.2f) ourHL=%d engineH=%d diff=%d",
                               px, py, ltri, lcnt, lh0, lh1, lh2, la, lb, lc, ourHL, engH, ourHL - engH);
            }
            s_ghPollLast = ghNow;
        }
    }
#endif

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

    s_manualArrivalPending = false;   // v0.18.3.198
    s_lastWorldPosX        = 0;
    s_lastWorldPosY        = 0;

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
        else if (strcmp(s_locations[i].name, "Timber") == 0) {
            // v0.18.3.192: Timber is a WALLED town -- its icon (-22564,-4867) is ~640u SOUTH of the
            // entrance, outside the wall, so reaching the icon never loads the town. Aim at the gate
            // MOUTH instead. The .190 attempt aimed at the gate THROAT (-22532,-5603, 7/8 wall-
            // enclosed); that cell is so boxed in that the clearance-weighted planner built a 55km
            // detour to reach it and looped back into Dollet. This time we target the LAST OPEN road
            // cell on the straight approach, right where the terrain-28 road dead-ends at the town wall
            // (computed from the .189 walkmesh: the road runs north from the icon at zero wall-
            // adjacency, then closes to a gate and ends at the wall at y=-5539; the last walkable road
            // cell is (-22564,-5507), only 4/8 enclosed). The approach is a straight open road, so the
            // route stays sensible (~35km, heading away from Dollet -> no re-entry loop), and the
            // character ends right at the town threshold where the field trigger sits. Open towns keep
            // their icon -- their entry is on open road by the icon already.
            s_refinedX[i]   = -22564;
            s_refinedY[i]   = -5507;
            s_refinedHas[i] = true;
            Log::World("WorldMap: [INIT] Refined entry default: %s (%d,%d) [walled-town gate mouth]",
                       s_locations[i].name, s_refinedX[i], s_refinedY[i]);
        }
    }

    // v0.18.3.195: load persisted refined entry coordinates AFTER the hard-coded
    // seeds, so a real captured entrance overrides its estimate. This file grows
    // as the player visits locations and can ship with the mod as a complete table.
    LoadRefinedEntries();

    // v0.18.3.200: hard-seed Galbadia Garden's REAL entrance. The .199 BAT log
    // caught it on a manual walk-in: field 'ggview1' (fieldId 0x02C8) at
    // (-37475,-26232), ~1170u south of the icon and clearly distinct from the
    // station (-38394,-24803). We FORCE it here -- AFTER LoadRefinedEntries -- so
    // that a stale persisted value (e.g. the .198 station mis-capture on an
    // install that skipped .199's one-time migration) can never override it.
    // G-Garden remains capture-exempt (see IsCaptureExempt) so a drive that
    // wanders into the adjacent station can't re-poison this seed. The .199
    // migration block is gone: it did its one-time job on Aaron's machine
    // (Galbadia Garden -> Galbadia Station), the station's real coord is now the
    // hard-coded catalog base, and keeping the migration would fight this seed.
    for (int i = 0; i < LOCATION_COUNT; i++) {
        if (strcmp(s_locations[i].name, "Galbadia Garden") == 0) {
            s_refinedX[i]   = -37475;
            s_refinedY[i]   = -26232;
            s_refinedHas[i] = true;
            Log::World("WorldMap: [INIT] Refined entry (forced): Galbadia Garden (%d,%d) [field 'ggview1' entrance]",
                       s_refinedX[i], s_refinedY[i]);
        }
    }

    // v0.18.3.203: seed the two field-trigger circles the .202 BAT demonstrated, so the
    // first drive of a session already routes around them instead of re-learning by
    // walking in. Galbadia Garden's 'ggview1' trigger fired 1815u from its entrance
    // (observed entry (-35939,-27200)); the station trigger caught the second drive.
    // Radii = observed distance + margin; further off-target entries keep refining these
    // (and add new locations) at runtime via [TRIGAVOID] learning in the arrival code.
    s_trigAvoidN = 0;
    s_trigAvoidX[s_trigAvoidN] = -37475; s_trigAvoidY[s_trigAvoidN] = -26232;
    s_trigAvoidR[s_trigAvoidN] = 2048; s_trigAvoidN++;   // Galbadia Garden plateau ('ggview1')
    s_trigAvoidX[s_trigAvoidN] = -38394; s_trigAvoidY[s_trigAvoidN] = -24803;
    s_trigAvoidR[s_trigAvoidN] = 1536; s_trigAvoidN++;   // Galbadia Station
    Log::World("WorldMap: [INIT] Seeded %d learned trigger circles (G-Garden 2048, Station 1536)", s_trigAvoidN);

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
    // A* ref->Dollet ~30259 / 126 tris). s_locations + LOCATION_COUNT are visible
    // here; the gate arg is ignored (v0.18.3.119: the slope gate in
    // Navmesh_AddTriangle already excluded cliffs, so flood/A* run ungated).
    Navmesh_LogConnectivity(s_locations, LOCATION_COUNT, -29270, -24056, -1);
#endif

#if NAVMESH_DIAG && NAVMESH_ROUTING
    // v0.18.3.123: road-verification oracle -- road bbox + a Timber->Dollet A*
    // dumped & road-flagged, to check whether A* tracks the known-walkable road
    // (model validated -> executor is the sole problem) or diverges into the
    // deep canyon (model still passing spurious terrain). Read-only; runs once.
    RoadVerifyTimberDollet();
    // v0.18.3.124: trace the walkable Timber->Dollet road ribbon vs the navmesh
    // -- is the road corridor navmesh-connected, or did the gate sever it?
    RoadConnectivityDiag();
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
