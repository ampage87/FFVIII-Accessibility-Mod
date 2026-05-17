// chase_auto_pilot.cpp -- Dollet/X-ATM092 chase auto-drive
//
// ============================================================================
// CURRENT STATE: v0.16.1 -- chase_auto_pilot.cpp split into focused .inl files.
//
// The 108 KB / 1402-line monolith of v0.16.0.3 has been carved into 9 files:
//   - chase_auto_pilot_history.h    pulled-out v0.15.9.x narrative, NOT in
//                                   the build path (#if 0 wrapper).
//   - chase_auto_pilot_state.inl    enums (FieldDriveMode, BridgeDanceState),
//                                   structs (FieldStage, FieldConfig), all
//                                   s_* module-static state, bridge-dance
//                                   constants, ENTITY_STRIDE_OTHERS.
//   - chase_auto_pilot_route.inl    kStages_domt5_1[] + kFieldConfigs[] data
//                                   tables with per-field rationale comments.
//   - chase_auto_pilot_io.inl       ReadSquallPosition + ReadKaniPosition
//                                   (SEH-guarded), DistSquared, IntSqrt.
//   - chase_auto_pilot_helpers.inl  IsDirectionLikeMode, PickStageIdx,
//                                   LookupConfig, BuildFallbackConfig,
//                                   DirectionName.
//   - chase_auto_pilot_diag.inl     LogChaseActiveDiagnostic (currently
//                                   retired / early-returns; kept for
//                                   future camera research).
//   - chase_auto_pilot_bridge.inl   UpdateBridgeDance state machine
//                                   (domt1_1 EAST/WEST kani-leap dance).
//   - chase_auto_pilot_engage.inl   Engage, Disengage.
//   - chase_auto_pilot_update.inl   the big per-tick Update function with
//                                   the per-second diagnostic.
//
// This file holds the public-API thin shell: system includes, namespace
// forward declarations, namespace block, the .inl chain in dependency
// order, and the small Initialize / Shutdown / IsEngaged functions. The
// Update function lives in chase_auto_pilot_update.inl and is wired in
// via the textual include below.
//
// History for the v0.15.9 -> v0.15.9.11.3.7 narrative is in
// chase_auto_pilot_history.h. See chase_auto_pilot.h for the public-API
// design notes.
// ============================================================================

#include "chase_auto_pilot.h"
#include "chase_ask_overlay.h"
#include "chase_detector.h"
#include "chase_keyboard.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_navigation.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ChaseAutoPilot {

// .inl include chain. ORDER MATTERS:
//   - state.inl first: declares enums/structs and module-static state used
//     by every later file.
//   - route.inl: kFieldConfigs[] + kStages_domt5_1[] data; references the
//     FieldDriveMode / FieldStage / FieldConfig types from state.inl.
//   - io.inl: ReadSquallPosition / ReadKaniPosition / DistSquared / IntSqrt;
//     uses ENTITY_STRIDE_OTHERS from state.inl.
//   - helpers.inl: IsDirectionLikeMode / PickStageIdx / LookupConfig /
//     BuildFallbackConfig / DirectionName; uses kFieldConfigs from route.inl
//     and s_fallback* state from state.inl.
//   - diag.inl: LogChaseActiveDiagnostic; uses io + helpers + state.
//   - bridge.inl: UpdateBridgeDance; uses io + the s_bridge* state.
//   - engage.inl: Engage / Disengage; uses helpers + io + state. Must come
//     before update.inl because Update() calls Engage and Disengage.
//   - update.inl last: Update() calls everything above.
#include "chase_auto_pilot_state.inl"
#include "chase_auto_pilot_route.inl"
#include "chase_auto_pilot_io.inl"
#include "chase_auto_pilot_helpers.inl"
#include "chase_auto_pilot_diag.inl"
#include "chase_auto_pilot_bridge.inl"
#include "chase_auto_pilot_engage.inl"
#include "chase_auto_pilot_update.inl"

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized      = true;
    s_engaged          = false;
    s_engagedField[0]  = '\0';
    s_engagedMode      = MODE_DIRECTION;
    s_engagedDirX      = 0;
    s_engagedDirY      = 0;
    s_engagedTargetX   = 0;
    s_engagedTargetY   = 0;
    s_engagedWalk      = false;
    s_diagTickCounter  = 0;
    s_prevChaseActive  = false;  // v0.15.9.2.10
    s_askWasActive     = false;  // v0.15.9.2.10
    s_askAnswered      = false;  // v0.15.9.2.10
    s_completedField[0] = '\0';  // v0.15.9.2.11
    s_chaseActiveTickCounter = 0;  // v0.15.9.3
    s_prevPosX            = 0;     // v0.15.9.3
    s_prevPosY            = 0;     // v0.15.9.3
    s_prevPosValid        = false; // v0.15.9.3
    s_bridgeDanceState        = BRIDGE_DANCE_EAST;  // v0.15.9.8.3
    s_bridgeLastKaniValid     = false;              // v0.15.9.8.3
    s_bridgeSampleCounter     = 0;                  // v0.15.9.8.3
    s_bridgeConsecLandSamples = 0;                  // v0.15.9.8.3
    s_bridgeWasLeaping        = false;              // v0.15.9.8.3
    s_bridgeTicksSinceXition  = 0;                  // v0.15.9.8.3
    s_bridgeLeapCount         = 0;                  // v0.15.9.8.3
    s_engagedStages       = nullptr;  // v0.15.9.7
    s_engagedStageCount   = 0;        // v0.15.9.7
    s_currentStageIdx     = -1;       // v0.15.9.7
    Log::Mod("ChaseAutoPilot: Initialized v%s. %d field configs ready: "
             "domt4_1 (DIRECTION run south-east, v0.15.9.4), "
             "domt3_2 (DIRECTION run east, v0.15.9.5), "
             "domt5_1 (STAGED_DIRECTION walk SW->S->SE by Y, v0.15.9.7), "
             "domt1_1 (BRIDGE_DANCE east/west by kani X-velocity, v0.15.9.8.3), "
             "doopen2a (TARGET south, v0.15.9.8), "
             "dotown_2/_1 (DIRECTION run south). "
             "Unknown chase fields fall back to MODE_TARGET via largest-cluster scan. "
             "Engagement gated on chase ASK being answered (v0.15.9.2.10). "
             "Per-field completed marker prevents re-engagement loop (v0.15.9.2.11). "
             "v0.15.9.8.3: kani-slot override on domt1_1 -> Others slot 3 (SYM 'laguna').",
             FF8OPC_VERSION, kFieldConfigsCount);
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_engaged) Disengage("Shutdown");
    s_initialized = false;
    Log::Mod("ChaseAutoPilot: Shutdown.");
}

bool IsEngaged()
{
    return s_initialized && s_engaged;
}

}  // namespace ChaseAutoPilot
