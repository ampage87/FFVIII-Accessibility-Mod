// chase_auto_pilot.cpp -- Dollet/X-ATM092 chase auto-drive
// See chase_auto_pilot.h for design notes.
//
// v0.15.9.2.8: Diagnostic-only -- log kani position and squall-kani distance
// every second to test Aaron's collision-push hypothesis (raised after the
// v0.15.9.2.7 BAT). v0.15.9.2.7 BAT showed: clean logs (spam gone), but the
// pattern at wp 13 on domt2_1 was IDENTICAL to v0.15.9.2.6 -- party walked
// 12 of 18 waypoints in ~2 seconds (19:54:41-19:54:43, ~700 units/sec, faster
// than calibration's measured walking pace), then froze at (683,8) with
// moveDist=0 for the entire rest of the BAT (~16 sec, ticks 408-1128). One
// transient moveDist=120 at tick=288, then dead silence. Crucially, NO
// "no-progress stuck" or "chase-drive: skipping wp" log lines appeared --
// the v0.15.9.2.5 advance-on-stuck mechanism is NOT firing on wp 13 on
// domt2_1, even though the conditions for it (sustained zero progress on
// a chase-drive target) clearly hold.
//
// Aaron's hypothesis after hearing the BAT: the kani's (X-ATM092 spider's)
// collision is what pushes Squall through chase fields. The auto-pilot's
// analog/keyboard input may have been doing nothing all along. No footsteps
// audible during the "working" fields supports this. The chase route would
// appear to make progress because the kani shoves Squall through screen
// transitions, not because the auto-pilot moves him.
//
// If true, this invalidates the entire chase-drive premise: every prior
// "success" (v0.15.9.2.5 west trail end-to-end, v0.15.9.2.4 wp 0-6 etc.)
// might have been collision-push, not input injection. The wp-13 stuck
// on domt2_1 would then be explained as: kani is somewhere out of pushing
// range (still on a previous field, or stuck behind a wall), so the only
// movement force is gone.
//
// v0.15.9.2.8 fix: add a ReadKaniPosition() helper that reads the kani's
// world position via ChaseDetector::GetKaniEntityPtr() (which already
// resolves the kani's runtime block at field-change time). The per-second
// diagnostic log now appends kani=(KX,KY) kdist=<squall-kani distance>
// to every status line. Three patterns to look for in the v0.15.9.2.8 BAT:
//   (A) kdist is consistently SMALL (<100) while wp progress happens,
//       and LARGE (>500) or kani=UNRESOLVED when stuck. -> Collision-push
//       confirmed. Auto-pilot input doing nothing. Rethink the approach
//       (perhaps script-pin kani's velocity to chase the party).
//   (B) kdist is roughly constant or varies independent of wp progress,
//       OR the party moves while kdist is large. -> Input injection works
//       at least sometimes. The wp-13 stuck has a different cause (likely
//       camera-unreachable funnel waypoint + advance-on-stuck guard bug).
//   (C) kani=UNRESOLVED on chase fields. -> ChaseDetector's slot resolution
//       broke, OR the kani is genuinely not a tracked entity on this field
//       (background entity vs others). v0.15.9.2.8 then refines kani slot
//       resolution.
//
// Risk: very low. Pure additive diagnostic. SEH-wrapped pointer read with
// null-check guard. No behavior change to engage/disengage/refresh paths.
//
// v0.15.9.2.7: Fix v0.15.9.2.6 log spam. v0.15.9.2.6 BAT result was an
// architectural success: the generic fallback fired on domt2_1 (chaseField=1,
// no explicit config), the path-finder computed 18 waypoints to (276,-727),
// and the party walked through 12 of them before getting stuck at wp 13.
// But BuildFallbackConfig() was called at the top of every Update() tick
// (~60Hz) because the per-tick refresh path ran AFTER the config lookup.
// Every call logged "fallback config built for field=...", flooding the
// field log with ~960 messages over 16 seconds of stuck-at-wp-13. Critical
// signal lost: any v0.15.9.2.5 advance-on-stuck "no-progress stuck" /
// "chase-drive: skipping wp" log lines were buried or possibly dropped.
//
// Fix: reorder Update() so the per-tick refresh path runs FIRST, using the
// cached s_engagedX state set by Engage() at fresh engagement. LookupConfig
// and BuildFallbackConfig only run on fresh engagement (new field or first
// engagement after disengage). The diagnostic log also reads s_engagedX
// instead of cfg->X so it doesn't need the config pointer. Net effect:
// BuildFallbackConfig's log line fires once per fresh engagement, not
// once per tick. After v0.15.9.2.7 the field log on domt2_1 should show
// one fallback message, then clean [drive] periodic logs interleaved with
// whatever stuck/skip/advance events v0.15.9.2.5's mechanism actually fires.
//
// v0.15.9.2.6: Generic chase-field engagement fallback. v0.15.9.2.5 BAT
// proved the west trail (domt5_1) walks end-to-end with the new chase-drive
// pipeline (kb-from-analog + advance-on-stuck). On field transition to
// domt2_1, the party stood still because chase_auto_pilot's per-field config
// table only knows about domt4_1 and domt5_1. v0.15.9.2.6 adds a fallback:
// when the engagement gate opens (chase field + auto mode + on field) but
// LookupConfig returns null, ask FieldNavigation::GetLargestClusterCenter()
// for the center of the largest dead-end cluster found at field load (the
// main corridor / south exit), then start MODE_TARGET chase-drive with
// walk=true and that target. The path-finder figures out the route.
//
// v0.15.9.2:  Per-field drive mode (DIRECTION vs TARGET). Adds path-finding
// support for fields where direction-drive hits walkmesh walls. v0.15.9.1.1
// BAT showed domt5_1 freezes the party at (-769, 2217) regardless of how
// fresh the keyboard injection is -- the corridor angles east of pure
// south, and constant-direction analog can't navigate it. domt5_1 now uses
// MODE_TARGET with a south-corridor target (cluster[2] center from the
// walkmesh dead-end scanner). Direction-mode path is preserved verbatim
// for fields that work with it (domt4_1's RUN LEFT cleared the field in
// one walking step in v0.15.9.1's BAT and doesn't need path-finding).
//
// v0.15.9.1.1: Cosmetic fix to per-second diagnostic logging. v0.15.9.1
// reset s_diagTickCounter to 0 BEFORE the Log::Field call, so every log
// line printed "tick=0" instead of the trigger value (60). The reset is
// now AFTER the log so the printed value matches the per-second cycle.
// Functional behavior unchanged.
//
// v0.15.9.1: Rewired to use FieldNavigation::StartDirectionDrive instead
// of standalone SendInput. v0.15.9 BAT proved keyboard alone wasn't
// enough -- the engine reads movement direction from the gamepad analog
// stick. FieldNavigation owns the fake-gamepad/analog-override plumbing
// (same path F9 path-finding uses); chase_auto_pilot owns the per-field
// config table.

#include "chase_auto_pilot.h"
#include "chase_ask_overlay.h"
#include "chase_detector.h"
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

// ============================================================================
// Per-field configuration
// ============================================================================
//
// Two drive modes per field, picked based on what the field's geometry
// allows:
//
//   MODE_DIRECTION : constant screen-relative analog direction held for
//                    the entire engagement. dirX/dirY are screen-relative
//                    direction signs in {-1, 0, +1} (DirectInput axis
//                    convention from field_nav_input_hooks.inl: dirX +1 =
//                    right, dirY +1 = south). Use for short, straight
//                    corridors where one walking step covers the field.
//                    Implementation: FieldNavigation::StartDirectionDrive.
//
//   MODE_TARGET    : path-finding drive toward an explicit (targetX,
//                    targetY) world coordinate. The F9 A*+funnel path-
//                    finder computes a walkmesh-aware route, handles
//                    narrow segments, dead-ends, and stuck recovery.
//                    Use for fields with bends, narrow corridors, or
//                    geometry where straight-line analog can't make it.
//                    Implementation: FieldNavigation::StartChaseDrive.
//
// walk = true holds W (FF8 PC default cancel = walk modifier on foot)
// for both modes -- needed on Aaron's AI-rule fields like domt5_1 where
// running shakes the mountain and the party gets caught.

enum FieldDriveMode {
    MODE_DIRECTION = 0,
    MODE_TARGET    = 1,
};

struct FieldConfig {
    const char*     fieldName;
    FieldDriveMode  mode;
    int8_t          dirX;       // MODE_DIRECTION: screen-relative direction sign
    int8_t          dirY;       // MODE_DIRECTION: screen-relative direction sign
    int32_t         targetX;    // MODE_TARGET: walkmesh world X
    int32_t         targetY;    // MODE_TARGET: walkmesh world Y
    bool            walk;
};

// Per-field auto-drive configuration. v0.15.9.2 adds MODE_TARGET to
// domt5_1 with a south-corridor target. v0.15.9.1 had only MODE_DIRECTION.
//
// v0.15.9.1.1 BAT empirical findings:
//   - domt4_1 (chase START, RUN LEFT): direction-drive cleared the field
//     in one walking step. Geometry is short. Keep MODE_DIRECTION.
//   - domt5_1 (west trail, WALK SOUTH): direction-drive hit a wall at
//     (-769, 2217) -- the corridor angles east of pure south. Need path-
//     finding to navigate. Switch to MODE_TARGET. Target chosen from the
//     walkmesh dead-end scanner: cluster[2] center=(382, 235) is the
//     largest cluster (14 narrow tris) in the south of the walkmesh,
//     likely the field-exit corridor.
//
// domt3_2 (between MH-6 and MH-7) is still unconfigured -- v0.15.9.2+
// will fill it in based on next BAT log evidence.
static const FieldConfig kFieldConfigs[] = {
    // domt4_1 = Mountain Hideout 6 (chase start, Selphie cliff).
    // Per Jegged: run immediately to the left (west). RUN, not walk.
    // v0.15.9.1 BAT: direction-drive cleared this field cleanly.
    { "domt4_1", MODE_DIRECTION,
      /*dirX=*/-1, /*dirY=*/ 0,
      /*targetX=*/0, /*targetY=*/0,
      /*walk=*/false },
    // domt5_1 = Mountain Hideout 7 (west trail). Walk south via path-finding.
    // Aaron's AI rule #1: running causes mountain shake / party caught.
    // v0.15.9.1 / v0.15.9.1.1 BAT: pure-south direction-drive froze at
    // (-769, 2217). Use path-finding with target near south corridor exit.
    { "domt5_1", MODE_TARGET,
      /*dirX=*/0, /*dirY=*/0,
      /*targetX=*/382, /*targetY=*/235,
      /*walk=*/true },
};
static const int kFieldConfigsCount =
    (int)(sizeof(kFieldConfigs) / sizeof(kFieldConfigs[0]));

// ============================================================================
// State
// ============================================================================

static bool s_initialized      = false;
static bool s_engaged          = false;
static char s_engagedField[32] = {0};

// v0.15.9.2.10: ASK gate state. v0.15.9.1 dropped the original
// !IsAskActive() gate on the theory that FF8 blocks input during the
// ASK anyway, so the gate added no protection but delayed engagement.
// v0.15.9.2.9 BAT proved that theory wrong: on domt4_1 Aaron reported
// "unable to move" with NO footsteps the whole BAT. The auto-pilot
// engaged at field-entry time (between chase_detector debounce settling
// and the chase-trigger MES + deferred-open ASK firing), installed the
// fake gamepad and held arrows + analog through the entire scripted
// intro and ASK. Position changes in the log were the chase script's
// own scripted entity motion -- the auto-pilot's input wasn't reaching
// the engine at all, AND it wedged the input pipeline so that after
// the ASK closed Aaron couldn't move via any path.
//
// Aaron's clarification: "auto-drive for the chase scene should not
// start or be affecting navigation until the ASK dialog fires and an
// option is selected." Just !IsAskActive() isn't enough -- the ASK is
// deferred-opened 3 seconds after Squall's chase-trigger MES fires,
// which itself fires after some script intro time. There's a 5-12
// second window between field-entry and ASK-open where the ASK isn't
// yet active but engaging would still wedge the engine.
//
// Solution: a two-step state machine that tracks "has the chase ASK
// been observed open AND then closed?" Once both happen we know the
// scripted intro is done and the player has handed control to the
// auto-pilot. Reset on chase activation transitions (chase-end
// followed by a new chase session re-arms the gate).
static bool s_prevChaseActive  = false;  // tracks IsChaseActive transitions
static bool s_askWasActive     = false;  // ASK has been seen open
static bool s_askAnswered      = false;  // ASK was open and is now closed

// v0.15.9.2.14: Trigger-line index for the current fallback config, if any.
// chase-drive uses this for cross-product sign-flip line crossing detection
// instead of stopping at a fixed point-distance from the target. -1 means
// the fallback is a plain point target (largest cluster center).
static int s_fallbackTriggerLineIdx = -1;

// v0.15.9.2.15: INF gateway crossing-line endpoints for the current fallback
// config. When the fallback finds a gateway via GetGatewayNearestCluster,
// these hold the gateway's full line segment so StartChaseDrive can install
// cross-product detection. All four zero means no gateway -- fall back to
// trigger-line index (s_fallbackTriggerLineIdx) or plain point target.
static int32_t s_fallbackGwLineX1 = 0;
static int32_t s_fallbackGwLineY1 = 0;
static int32_t s_fallbackGwLineX2 = 0;
static int32_t s_fallbackGwLineY2 = 0;

// v0.15.9.2.11: Completed-field gate. v0.15.9.2.10 BAT on domt2_1 showed
// an infinite engagement loop: player spawned at (444,-500), fallback
// target was (276,-727), startDist=282 -- already inside the default
// arrive distance. Chase-drive engaged, immediately called Arrived.,
// disengaged. Next Update tick saw s_engaged=false, rebuilt the fallback,
// re-engaged, arrived, disengaged. Loop at ~60Hz: fake gamepad installed
// and uninstalled hundreds of times per second, log flooded with
// "fallback config built" lines, no actual driving happens, and the
// input pipeline thrash probably explains the no-footsteps symptom on
// fallback fields too.
//
// Fix: when chase-drive completes (Arrived or Stuck) we mark the field
// as "auto-pilot done" and refuse to re-engage on that field until the
// field actually changes. Player can drive manually for the rest of
// that field if there's more ground to cover (e.g., the target was the
// largest dead-end cluster but the actual screen-transition trigger is
// further out). On field change, the marker clears.
static char s_completedField[32] = {0};

// v0.15.9.1: Per-second diagnostic tick counter. Increments every Update()
// call while engaged; logs status line and resets at 60. Reset to 0 at
// Engage() so each fresh engagement gets a clean count.
static int s_diagTickCounter = 0;

// Cached config for the engaged field (so the per-second log can read
// dir/walk values without re-running LookupConfig).
static FieldDriveMode s_engagedMode = MODE_DIRECTION;
static int8_t  s_engagedDirX    = 0;
static int8_t  s_engagedDirY    = 0;
static int32_t s_engagedTargetX = 0;
static int32_t s_engagedTargetY = 0;
static bool    s_engagedWalk    = false;

// FF8 entity-array stride for "others" (party slots). Same value
// field_navigation.cpp uses internally. Squall in the chase scene is
// always entity[0] -- the lead member. Position bytes at +0x190 (X * 4096)
// and +0x194 (Y * 4096), matching field_nav_helpers.inl::GetEntityPos.
static const uint32_t ENTITY_STRIDE_OTHERS = 0x264;

// ============================================================================
// Config lookup
// ============================================================================

static const FieldConfig* LookupConfig(const char* fieldName)
{
    if (fieldName == nullptr || *fieldName == '\0') return nullptr;
    for (int i = 0; i < kFieldConfigsCount; ++i) {
        if (std::strcmp(fieldName, kFieldConfigs[i].fieldName) == 0)
            return &kFieldConfigs[i];
    }
    return nullptr;
}

// v0.15.9.2.6: Per-field fallback config buffer. When LookupConfig() returns
// null on a chase field, BuildFallbackConfig() asks FieldNavigation for the
// largest cluster center and synthesizes a MODE_TARGET FieldConfig pointing
// at it. Returns true on success (and *outCfg is populated) or false if the
// walkmesh didn't load or the scanner found no clusters.
//
// The fallback buffer is module-static so the returned pointer stays valid
// for the duration of the engagement. Engage() copies the fieldName into
// it before each fallback engagement so its lifetime matches the engaged
// field. fieldName must remain valid for the engagement (chase_detector's
// debounced name is static, so this is fine).
static char        s_fallbackFieldName[32] = {0};
static FieldConfig s_fallbackConfig = {};

static const FieldConfig* BuildFallbackConfig(const char* fieldName)
{
    if (fieldName == nullptr || *fieldName == '\0') return nullptr;
    int32_t tgtX = 0, tgtY = 0;
    // v0.15.9.2.15: Three-tier target preference for chase fallback:
    //   1. INF gateway crossing line  -- the engine's actual screen-transition
    //      exit, with explicit destination field ID. Picked by direction-
    //      alignment with the cluster (not nearest-to-cluster, because the
    //      entry-back gateway can be geometrically closer; see header).
    //   2. SETLINE trigger nearest cluster -- works on fields whose JSM Line
    //      entities are SCREEN_BOUND/UNKNOWN, not on Event Trigger chase fields
    //      like domt2_1. v0.15.9.2.14's primary mechanism. Still useful for
    //      non-chase fallback uses of this path.
    //   3. Cluster center only -- plain point-distance arrival as last resort.
    //      Chase fields hit this path only when both gateway and SETLINE
    //      lookups fail (rare; should never happen in practice).
    s_fallbackTriggerLineIdx = -1;
    s_fallbackGwLineX1 = 0; s_fallbackGwLineY1 = 0;
    s_fallbackGwLineX2 = 0; s_fallbackGwLineY2 = 0;

    int32_t gwX1 = 0, gwY1 = 0, gwX2 = 0, gwY2 = 0;
    bool gotGw = FieldNavigation::GetGatewayNearestCluster(&tgtX, &tgtY,
                                                           &gwX1, &gwY1,
                                                           &gwX2, &gwY2);
    int trigIdx = -1;
    if (gotGw) {
        s_fallbackGwLineX1 = gwX1; s_fallbackGwLineY1 = gwY1;
        s_fallbackGwLineX2 = gwX2; s_fallbackGwLineY2 = gwY2;
    } else {
        bool gotTrig = FieldNavigation::GetTriggerLineNearestCluster(&tgtX, &tgtY, &trigIdx);
        if (gotTrig) {
            s_fallbackTriggerLineIdx = trigIdx;
        } else {
            if (!FieldNavigation::GetLargestClusterCenter(&tgtX, &tgtY)) {
                Log::Field("ChaseAutoPilot: fallback for field='%s' UNAVAILABLE "
                           "(walkmesh not loaded; no gateways, no triggers, no clusters)",
                           fieldName);
                return nullptr;
            }
        }
    }
    std::strncpy(s_fallbackFieldName, fieldName, sizeof(s_fallbackFieldName) - 1);
    s_fallbackFieldName[sizeof(s_fallbackFieldName) - 1] = '\0';
    s_fallbackConfig.fieldName = s_fallbackFieldName;
    s_fallbackConfig.mode      = MODE_TARGET;
    s_fallbackConfig.dirX      = 0;
    s_fallbackConfig.dirY      = 0;
    s_fallbackConfig.targetX   = tgtX;
    s_fallbackConfig.targetY   = tgtY;
    // v0.15.9.2.12: Default to RUNNING (walk=false). The chase as a whole is
    // Squall fleeing X-ATM092 at top speed; running is the right default for
    // any chase field we don't have an explicit config for. Aaron confirmed
    // after the v0.15.9.2.11 BAT: "the party should be running on this field
    // not walking" (re: domt2_1). The walking-mode exception (Aaron's AI rule
    // #1) applies only to domt5_1 where running shakes the cliff path and
    // the party gets caught -- that field has an explicit config with
    // walk=true. Previous default of walk=true was a misreading.
    s_fallbackConfig.walk      = false;
    if (gotGw) {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running INF-GATEWAY line(%d,%d)->(%d,%d) (cross-product detection)",
                   fieldName, (int)tgtX, (int)tgtY,
                   (int)gwX1, (int)gwY1, (int)gwX2, (int)gwY2);
    } else if (trigIdx >= 0) {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running TRIGGER-LINE idx=%d (cross-product detection)",
                   fieldName, (int)tgtX, (int)tgtY, trigIdx);
    } else {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running (cluster-center fallback, no gateway/trigger found)",
                   fieldName, (int)tgtX, (int)tgtY);
    }
    return &s_fallbackConfig;
}

// Compass name for log messages. Returns a short descriptive string for
// the (dirX, dirY) pair so the FREEZE/ENGAGED log lines are readable.
static const char* DirectionName(int8_t dirX, int8_t dirY)
{
    if (dirX == 0 && dirY == 0) return "target";
    if (dirX == 0 && dirY <  0) return "north";
    if (dirX == 0 && dirY >  0) return "south";
    if (dirX <  0 && dirY == 0) return "west";
    if (dirX >  0 && dirY == 0) return "east";
    if (dirX <  0 && dirY <  0) return "northwest";
    if (dirX >  0 && dirY <  0) return "northeast";
    if (dirX <  0 && dirY >  0) return "southwest";
    if (dirX >  0 && dirY >  0) return "southeast";
    return "?";
}

// ============================================================================
// Entity[0] (Squall) position read for diagnostic logging
// ============================================================================

// SEH-guarded read of Squall's screen-space position. Returns true and
// fills (x, y) if successful; returns false if the address chain isn't
// resolved or the read faults (e.g. mid-field-load transition).
//
// Coordinates are integer divisions of the fixed-point bytes -- matching
// field_nav_helpers.inl::GetEntityPos so log values line up with anything
// FieldNavigation logs about player position. We don't fall back to the
// 0x20/0x24 simple-int16 path here because in chase fields Squall is
// always actively moving (or being driven by us) and the fixed-point
// path at 0x190/0x194 is always populated.
static bool ReadSquallPosition(int32_t& outX, int32_t& outY)
{
    if (!FF8Addresses::pFieldStateOthers) return false;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return false;
        uint8_t* block = base + ENTITY_STRIDE_OTHERS * 0;  // entity[0] = Squall
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.15.9.2.8: SEH-guarded read of the kani's (X-ATM092 spider's) world
// position via ChaseDetector::GetKaniEntityPtr(). Returns true and fills
// (x, y) on success; returns false if the kani slot is unresolved in this
// field, the runtime block pointer is null, or the read faults.
//
// Used by the per-second diagnostic to test Aaron's hypothesis (raised
// after v0.15.9.2.7 BAT) that the kani's collision is what pushes Squall
// through chase fields -- not the auto-pilot's analog/keyboard input.
// If the kani sits right behind Squall (small distance) every time wp
// progress occurs, and stays far away when the party is stuck, that
// confirms the collision-push hypothesis and means the entire chase
// auto-pilot premise (input injection) needs rethinking.
//
// Reads at +0x190 (X*4096) / +0x194 (Y*4096), divided by 4096 -- same
// layout as Squall (entity blocks are uniform). The kani entity block
// pointer is owned by ChaseDetector which caches it at field-change time.
static bool ReadKaniPosition(int32_t& outX, int32_t& outY)
{
    uintptr_t kani = ChaseDetector::GetKaniEntityPtr();
    if (kani == 0) return false;
    __try {
        uint8_t* block = reinterpret_cast<uint8_t*>(kani);
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Squared distance helper (avoids sqrt for log threshold checks).
static int32_t DistSquared(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int64_t dx = (int64_t)ax - (int64_t)bx;
    int64_t dy = (int64_t)ay - (int64_t)by;
    int64_t sq = dx*dx + dy*dy;
    if (sq > 0x7FFFFFFFLL) sq = 0x7FFFFFFFLL;
    return (int32_t)sq;
}

// Integer sqrt for distance display (returns 0 for negative input).
// v0.15.9.2.9: Newton's method from x=v doesn't converge in 6 iterations for
// large squared values (e.g. v=796850 converges to 12471 after 6 iter, real
// answer is 892). v0.15.9.2.8 BAT showed kdist values inflated 14x. Switch to
// std::sqrt via <cmath>; the cast back to int32_t truncates fractional pixels
// which is fine for distance display.
static int32_t IntSqrt(int32_t v)
{
    if (v <= 0) return 0;
    return (int32_t)std::sqrt((double)v);
}

// ============================================================================
// Engage / disengage
// ============================================================================

static void Engage(const FieldConfig* cfg)
{
    if (cfg == nullptr) return;

    bool ok = false;
    if (cfg->mode == MODE_DIRECTION) {
        FieldNavigation::StartDirectionDrive(cfg->dirX, cfg->dirY, cfg->walk);
        // StartDirectionDrive doesn't return a status; assume success unless
        // log evidence proves otherwise. F9 mutex is the only known refusal
        // cause and chase auto-pilot doesn't engage while F9 runs.
        ok = true;
    } else if (cfg->mode == MODE_TARGET) {
        // v0.15.9.2: path-finding drive. StartChaseDrive validates state
        // (no F9 active, no dialog open, on field) and returns false on
        // failure. We don't retry -- if it fails on this Update tick, the
        // gate must have flipped (e.g., dialog opened) and we'll try again
        // on the next tick when the gate re-evaluates.
        //
        // v0.15.9.2.14: Pass the trigger-line index. Only the fallback path
        // (BuildFallbackConfig) sets s_fallbackTriggerLineIdx >= 0 currently.
        // Explicit per-field configs use point targets (pass -1). When the
        // index is >= 0, chase-drive enables cross-product sign-flip line-
        // crossing detection -- the player walks ONTO the line and the drive
        // stops the instant they cross, which is what fires FF8's screen
        // transition.
        //
        // v0.15.9.2.15: Also pass INF gateway crossing-line endpoints. Set
        // by BuildFallbackConfig when GetGatewayNearestCluster succeeded.
        // Mutually exclusive with the trigger-line index (only one wins per
        // engagement). Explicit per-field configs pass all zeros = no
        // crossing line, plain point arrival.
        int trigIdx = -1;
        int32_t gwX1 = 0, gwY1 = 0, gwX2 = 0, gwY2 = 0;
        if (cfg == &s_fallbackConfig) {
            trigIdx = s_fallbackTriggerLineIdx;
            gwX1 = s_fallbackGwLineX1; gwY1 = s_fallbackGwLineY1;
            gwX2 = s_fallbackGwLineX2; gwY2 = s_fallbackGwLineY2;
        }
        ok = FieldNavigation::StartChaseDrive(cfg->targetX, cfg->targetY,
                                              trigIdx,
                                              gwX1, gwY1, gwX2, gwY2,
                                              cfg->walk);
    }

    if (!ok) {
        Log::Field("ChaseAutoPilot: failed to engage on field='%s' mode=%d",
                   cfg->fieldName, (int)cfg->mode);
        return;
    }

    std::strncpy(s_engagedField, cfg->fieldName, sizeof(s_engagedField) - 1);
    s_engagedField[sizeof(s_engagedField) - 1] = '\0';
    s_engaged          = true;
    s_engagedMode      = cfg->mode;
    s_engagedDirX      = cfg->dirX;
    s_engagedDirY      = cfg->dirY;
    s_engagedTargetX   = cfg->targetX;
    s_engagedTargetY   = cfg->targetY;
    s_engagedWalk      = cfg->walk;
    s_diagTickCounter  = 0;

    if (cfg->mode == MODE_DIRECTION) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=DIRECTION direction=%s "
                   "%s (dirX=%d dirY=%d walk=%d)",
                   cfg->fieldName, DirectionName(cfg->dirX, cfg->dirY),
                   cfg->walk ? "WALKING" : "running",
                   (int)cfg->dirX, (int)cfg->dirY, (int)cfg->walk);
    } else {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=TARGET tgt=(%d,%d) "
                   "%s (walk=%d)",
                   cfg->fieldName, (int)cfg->targetX, (int)cfg->targetY,
                   cfg->walk ? "WALKING" : "running",
                   (int)cfg->walk);
    }
}

static void Disengage(const char* reason)
{
    if (!s_engaged) return;
    if (s_engagedMode == MODE_DIRECTION) {
        FieldNavigation::StopDirectionDrive();
    } else {
        FieldNavigation::StopChaseDrive();
    }
    Log::Field("ChaseAutoPilot: DISENGAGED (%s) was on field='%s' mode=%d",
               reason ? reason : "?", s_engagedField, (int)s_engagedMode);

    // v0.15.9.2.11: When chase-drive completes (Arrived or Stuck) mark
    // this field as auto-pilot-done so we don't re-engage in a loop. The
    // reason string from the per-tick refresh path is
    // "chase-drive completed (target reached or stuck)". Match a stable
    // substring rather than the whole string in case the reason text is
    // ever refined.
    if (reason && std::strstr(reason, "chase-drive completed") != nullptr &&
        s_engagedField[0] != '\0') {
        std::strncpy(s_completedField, s_engagedField, sizeof(s_completedField) - 1);
        s_completedField[sizeof(s_completedField) - 1] = '\0';
        Log::Field("ChaseAutoPilot: field '%s' marked auto-pilot complete; "
                   "won't re-engage until field changes", s_completedField);
    }

    s_engagedField[0]  = '\0';
    s_engaged          = false;
    s_engagedMode      = MODE_DIRECTION;
    s_engagedDirX      = 0;
    s_engagedDirY      = 0;
    s_engagedTargetX   = 0;
    s_engagedTargetY   = 0;
    s_engagedWalk      = false;
    s_diagTickCounter  = 0;
}

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
    Log::Mod("ChaseAutoPilot: Initialized v%s. %d field configs ready: "
             "domt4_1 (DIRECTION run west), domt5_1 (TARGET path-find south). "
             "Unknown chase fields fall back to MODE_TARGET via largest-cluster scan. "
             "Engagement gated on chase ASK being answered (v0.15.9.2.10). "
             "Per-field completed marker prevents re-engagement loop (v0.15.9.2.11).",
             FF8OPC_VERSION, kFieldConfigsCount);
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_engaged) Disengage("Shutdown");
    s_initialized = false;
    Log::Mod("ChaseAutoPilot: Shutdown.");
}

void Update()
{
    if (!s_initialized) return;

    // v0.15.9.2.10: ASK gate state machine. Track chase-activation
    // transitions to reset the gate, and watch IsAskActive transitions to
    // detect when the ASK has been answered. See the state-declaration
    // comment block above for the full rationale.
    bool chaseActive = ChaseDetector::IsChaseActive();
    if (chaseActive && !s_prevChaseActive) {
        // Chase just started. Re-arm the gate -- a fresh ASK answer is
        // required for this chase session.
        s_askWasActive = false;
        s_askAnswered  = false;
        Log::Field("ChaseAutoPilot: chase activated, waiting for ASK to fire and be answered before engaging");
    } else if (!chaseActive && s_prevChaseActive) {
        // Chase just ended. Clear the gate state for next session.
        s_askWasActive = false;
        s_askAnswered  = false;
    }
    s_prevChaseActive = chaseActive;

    if (chaseActive) {
        bool askActiveNow = ChaseAskOverlay::IsAskActive();
        if (askActiveNow) {
            if (!s_askWasActive) {
                s_askWasActive = true;
                Log::Field("ChaseAutoPilot: chase ASK observed open (auto-pilot stays disengaged)");
            }
        } else if (s_askWasActive && !s_askAnswered) {
            // ASK was open and is now closed -- user must have selected.
            s_askAnswered = true;
            Log::Field("ChaseAutoPilot: chase ASK answered, engagement gate is now open");
        }
    }

    // Engagement gate: chase field, auto mode, on field, AND chase ASK has
    // been answered for this chase session.
    bool inChaseField = ChaseDetector::IsInChaseField();
    bool autoMode     = (ChaseDetector::GetChaseMode() == ChaseDetector::MODE_AUTO);
    bool onField      = FF8Addresses::IsOnField();

    bool wantEngage = inChaseField && autoMode && onField && s_askAnswered;

    if (!wantEngage) {
        if (s_engaged) {
            const char* reason =
                !inChaseField ? "left chase field"             :
                !autoMode     ? "mode != auto"                  :
                !onField      ? "off-field (battle/menu/etc.)" :
                                "ASK not yet answered";
            Disengage(reason);
        }
        return;
    }

    // Engagement window is open. Get the debounced field name.
    const char* fieldName = ChaseDetector::GetDebouncedFieldName();
    if (fieldName == nullptr || *fieldName == '\0') {
        // Field name not yet settled (during the 2s name-debounce after
        // a transition). Don't engage yet; release any prior held state.
        if (s_engaged) Disengage("field name not settled");
        return;
    }

    // Field changed since last engagement? Disengage cleanly so the
    // new field starts with fresh direction-drive state.
    if (s_engaged && std::strcmp(s_engagedField, fieldName) != 0) {
        Disengage("field changed");
    }

    // v0.15.9.2.11: Field changed since last completion? Clear the
    // completion marker so the new field can be auto-piloted from scratch.
    if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) != 0) {
        Log::Field("ChaseAutoPilot: field changed from completed field '%s' to '%s'; "
                   "clearing completion marker",
                   s_completedField, fieldName);
        s_completedField[0] = '\0';
    }

    // v0.15.9.2.11: If we've already completed auto-pilot on this field,
    // refuse to re-engage. Prevents the engage/arrive/disengage loop
    // discovered in the v0.15.9.2.10 BAT on domt2_1.
    if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) == 0) {
        return;
    }

    // v0.15.9.2.7: Per-tick refresh path FIRST, before any config lookup.
    // v0.15.9.2.6 ran LookupConfig() and BuildFallbackConfig() at the top
    // of every Update tick. BuildFallbackConfig() always logs when it
    // succeeds, so on a fallback-engaged field it flooded the log with
    // hundreds of "fallback config built" lines per second (one per tick,
    // ~60Hz) — confirmed in the v0.15.9.2.6 BAT field log on domt2_1:
    // 16 seconds of stuck-at-wp-13 buried under ~960 spam messages, with
    // any v0.15.9.2.5 advance-on-stuck logs presumably drowned out.
    //
    // Fix: when we're already engaged on the same field, run the per-tick
    // refresh and diagnostic using the cached s_engagedX state (set by
    // Engage() once at fresh engagement). The config pointer is only
    // needed for fresh engagement; the engaged-state cache is sufficient
    // for everything else.
    if (s_engaged && std::strcmp(s_engagedField, fieldName) == 0) {
        // Already engaged on this field. Per-tick refresh depends on mode:
        //
        // MODE_DIRECTION: re-call StartDirectionDrive every tick. The API's
        // "already running" branch is idempotent and runs the keep-alive
        // pulse cycle (see field_nav_directiondrive.inl) so the engine
        // sees fresh KEYDOWN events periodically.
        //
        // MODE_TARGET: path-finding runs autonomously inside the F9 update
        // state machine (UpdateAutoDrive in field_nav_autodrive.inl). We
        // just verify it's still active. If chase-drive completed (arrived
        // at target or stuck-detection gave up), disengage so the player
        // knows we're done. The field-change branch above handles the
        // happy case where reaching the target triggered a field exit.
        if (s_engagedMode == MODE_DIRECTION) {
            FieldNavigation::StartDirectionDrive(s_engagedDirX, s_engagedDirY, s_engagedWalk);
        } else {
            if (!FieldNavigation::IsChaseDriveActive()) {
                Disengage("chase-drive completed (target reached or stuck)");
                return;
            }
        }

        // Per-second diagnostic, using cached engaged state (no cfg lookup).
        s_diagTickCounter++;
        if (s_diagTickCounter >= 60) {
            // v0.15.9.1.1: log first, THEN reset, so the printed tick
            // value matches the trigger (60) rather than always reading 0.
            int32_t pX = 0, pY = 0;
            bool gotPos = ReadSquallPosition(pX, pY);

            // v0.15.9.2.8: also read the kani's position and compute the
            // squall-kani distance. This is purely diagnostic -- no behavior
            // change. Goal: test Aaron's collision-push hypothesis. If kdist
            // is consistently small when movement happens (party advances
            // through waypoints) and large when movement stops (stuck), the
            // kani's collision is the actual movement source and the auto-
            // pilot's analog/kb input is doing nothing. Conversely, if the
            // party moves while kani is far away, input injection works at
            // least sometimes and the wp-13 stuck has a different cause.
            int32_t kX = 0, kY = 0;
            bool gotKani = ReadKaniPosition(kX, kY);
            char kaniBuf[96];
            if (gotKani && gotPos) {
                int32_t kdist = IntSqrt(DistSquared(kX, kY, pX, pY));
                std::snprintf(kaniBuf, sizeof(kaniBuf),
                              " kani=(%d,%d) kdist=%d", (int)kX, (int)kY, (int)kdist);
            } else if (gotKani) {
                std::snprintf(kaniBuf, sizeof(kaniBuf),
                              " kani=(%d,%d) kdist=?", (int)kX, (int)kY);
            } else {
                std::snprintf(kaniBuf, sizeof(kaniBuf), " kani=UNRESOLVED");
            }

            if (s_engagedMode == MODE_DIRECTION) {
                int32_t lX = (int32_t)s_engagedDirX * 1000;
                int32_t lY = (int32_t)s_engagedDirY * 1000;
                if (gotPos) {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=DIRECTION "
                               "dir=(%d,%d) walk=%d pos=(%d,%d) lX=%d lY=%d%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk,
                               pX, pY, lX, lY, kaniBuf);
                } else {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=DIRECTION "
                               "dir=(%d,%d) walk=%d pos=READ_FAILED lX=%d lY=%d%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk,
                               lX, lY, kaniBuf);
                }
            } else {
                if (gotPos) {
                    int32_t dx = s_engagedTargetX - pX;
                    int32_t dy = s_engagedTargetY - pY;
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=TARGET "
                               "tgt=(%d,%d) walk=%d pos=(%d,%d) dist=(%d,%d)%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedTargetX, (int)s_engagedTargetY, (int)s_engagedWalk,
                               pX, pY, dx, dy, kaniBuf);
                } else {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=TARGET "
                               "tgt=(%d,%d) walk=%d pos=READ_FAILED%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedTargetX, (int)s_engagedTargetY, (int)s_engagedWalk,
                               kaniBuf);
                }
            }
            s_diagTickCounter = 0;
        }
        return;
    }

    // Not engaged on this field yet -- look up config (explicit first,
    // then fallback). BuildFallbackConfig's log line fires at most once
    // per fresh engagement now, instead of once per tick.
    const FieldConfig* cfg = LookupConfig(fieldName);
    if (cfg == nullptr) {
        // v0.15.9.2.6: No per-field config -- try the generic fallback.
        // BuildFallbackConfig() asks FieldNavigation for the largest cluster
        // center (typically the main corridor / exit area) and returns a
        // synthesized MODE_TARGET config. If the walkmesh didn't load or no
        // clusters were found, fallback returns null and we behave as before
        // (player drives manually).
        cfg = BuildFallbackConfig(fieldName);
        if (cfg == nullptr) {
            // The strcmp above already disengaged us if we were previously
            // engaged on a different field.
            return;
        }
    }

    // Fresh engagement on this field.
    Engage(cfg);
}

bool IsEngaged()
{
    return s_initialized && s_engaged;
}

}  // namespace ChaseAutoPilot
