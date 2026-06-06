// chase_auto_pilot_state.inl -- types, structs, module-static state
//
// Textual include from chase_auto_pilot.cpp. FIRST in include chain --
// every other .inl assumes the enums/structs/state declared here are
// already visible. Do not include directly; do not add a header guard
// (the parent .cpp's namespace block + sole-include path is sufficient).
//
// Layout:
//   - FieldDriveMode enum
//   - FieldStage / FieldConfig structs
//   - BridgeDanceState enum + per-sample tick constants
//   - All s_* module-static state for engagement, ASK gate, completed
//     field, diagnostic counters, bridge-dance machine, cached engaged
//     config, and STAGED_DIRECTION state
//   - ENTITY_STRIDE_OTHERS constant

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
    MODE_DIRECTION        = 0,
    MODE_TARGET           = 1,
    MODE_STAGED_DIRECTION = 2,  // v0.15.9.7
    MODE_BRIDGE_DANCE     = 3,  // v0.15.9.8.3 -- domt1_1 only
};

// v0.15.9.7: Stage descriptor for MODE_STAGED_DIRECTION. Stages let a single
// engagement switch direction part-way through the field based on the
// party's current Y position. Used for S-shaped trails like domt5_1 where
// one constant direction can't navigate the geometry (the trail bends
// from SW to S to SE, so the analog input has to follow).
//
// Stages are listed in DECREASING activeMinY order. PickStage walks the
// array and returns the first stage whose activeMinY is <= the party's
// current Y. The last stage MUST have activeMinY = INT32_MIN so it always
// matches as a fallback.
//
// Mid-engagement transitions are handled by FieldNavigation::StartDirectionDrive's
// "already running" branch (see field_nav_directiondrive.inl): when called
// with a new (dirX, dirY, walk), it updates s_analogDesiredLX/LY in place
// and fires diff-based KEYUP/KEYDOWN events only when the arrow bitmask
// actually changes. So changing s_engagedDirX/Y/Walk between Update ticks
// is enough -- the existing per-tick refresh call picks up the new values.
struct FieldStage {
    int8_t  dirX;        // screen-relative direction sign, {-1, 0, +1}
    int8_t  dirY;        // screen-relative direction sign, {-1, 0, +1}
    bool    walk;        // hold W (walk modifier) during this stage
    int32_t activeMinY;  // stage is active when party Y >= this value
};

struct FieldConfig {
    const char*     fieldName;
    FieldDriveMode  mode;
    int8_t          dirX;       // MODE_DIRECTION: screen-relative direction sign
    int8_t          dirY;       // MODE_DIRECTION: screen-relative direction sign
    int32_t         targetX;    // MODE_TARGET: walkmesh world X
    int32_t         targetY;    // MODE_TARGET: walkmesh world Y
    bool            walk;
    // v0.15.9.7: MODE_STAGED_DIRECTION fields. Null/0 for other modes.
    const FieldStage* stages;
    int               stageCount;
};

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

// v0.15.9.3: Pre-engage chase-active diagnostic state.
//
// s_chaseActiveTickCounter: separate counter for the once-per-second
// pre-engage log. Increments every Update() call while chaseActive is true,
// regardless of engagement state. Lets us log party + kani positions during
// the chase-ASK window (when the existing s_diagTickCounter is dormant
// because chase_auto_pilot hasn't engaged yet).
//
// s_prevPosX, s_prevPosY, s_prevPosValid: party position from the previous
// per-second snapshot. Used to compute `delta=(dX,dY)` -- the party motion
// since the last log line. This is the v0.15.9.3 key diagnostic: it tells
// us whether the analog input we're injecting is actually moving the party,
// or whether the chase script is overriding it. On engaged fields, dX/dY
// should correlate (modulo camera rotation) with the analog direction; on
// fields where the script overrides, dX/dY will be inconsistent with analog.
// s_prevPosValid is false on the first tick of a chase (no previous sample)
// and is reset when the chase deactivates.
static int     s_chaseActiveTickCounter = 0;
static int32_t s_prevPosX                = 0;
static int32_t s_prevPosY                = 0;
static bool    s_prevPosValid            = false;

// v0.15.9.8.3: Bridge dance state machine. The bridge (domt1_1) has an
// interactive feedback loop with X-ATM092:
//
//   1. Party starts heading east (default).
//   2. Robot eventually leaps east, vaulting over the party and landing
//      ahead of them on the corridor (X far east of party).
//   3. With robot blocking east, party must turn west to retreat;
//      otherwise the chase script's catch evaluator fires at X~2053.
//   4. After turning west, robot eventually leaps again (presumably west).
//      As soon as the leap STARTS (mid-air, before landing), turn east
//      immediately -- robot can't course-correct mid-jump, so party slips
//      past while it's airborne.
//   5. Resume east, cross the natural east-edge SETLINE into doopen2a.
//
// Detection signals confirmed by v0.15.9.8.2 BAT (laguna at Others slot 3):
//   * Robot Y is constant on the bridge (-446) -- jumps are X-axis only
//     in walkmesh coords (the visual "jump" is the 3D Z-axis, not Y).
//   * Normal pursuit speed: ~106 units / 100ms tick (slightly faster than
//     party at ~93).
//   * Leap speed: ~372 units / 100ms tick (3.5x normal).
//   * Landed: 0-1 units / tick (stationary).
//
// State variables here track previous-sample kani.X for delta computation,
// the current dance state (EAST / WEST leg), a "was leaping" latch (so we
// only treat "landed" as significant after observing a leap), and dwell-
// time counters for transition debouncing and west-leg timeout fallback.
enum BridgeDanceState {
    BRIDGE_DANCE_EAST = 0,  // initial: drive east, watch for landed-in-front
    BRIDGE_DANCE_WEST = 1,  // drive west, watch for next leap start
};

// Per-sample tick budget. Update() runs at ~60 Hz; we sample kani motion
// every kBridgeSamplePeriodTicks ticks, matching the existing BridgeDiag
// 10 Hz cadence so the X-delta values line up with the v0.15.9.8.2 data
// that calibrated the thresholds below.
static const int     kBridgeSamplePeriodTicks = 6;

// X-speed thresholds (units per 100 ms sample) classifying robot motion.
// From v0.15.9.8.2 BAT: normal pursuit 70-140, leap 350-500, stationary
// 0-36. Two-tier classification with a deliberate gap so a borderline
// sample doesn't ambiguously read as both.
static const int32_t kBridgeLeapThreshold  = 200;
static const int32_t kBridgeLandThreshold  = 50;

// East-leg "landed in front" debounce: require this many consecutive
// samples meeting the landed criteria before committing the turn. Cheap
// protection against a one-sample noise spike masquerading as a landing.
static const int     kBridgeLandConsec     = 2;

// Minimum dwell time per state (in 60 Hz Update ticks) before any
// transition can fire. Prevents oscillation if the kani's X delta briefly
// flickers during a landing.
static const int     kBridgeMinDwellTicks  = 60;     // 1 second

// West-leg safety timeout. If no leap is detected for this long after
// turning west, force a transition back to east. Without this, a bridge
// where the robot never re-leaps would leave the party walking west
// forever (we'd never reach the east-edge SETLINE exit). Generous value
// because the natural pre-leap chase phase took ~3 seconds in the
// v0.15.9.8.2 BAT (ticks 13-47), and we want to give the west leg the
// same room to develop before bailing.
static const int     kBridgeWestTimeoutTicks = 300;  // 5 seconds

// State.
static BridgeDanceState s_bridgeDanceState     = BRIDGE_DANCE_EAST;
static int32_t          s_bridgeLastKaniX      = 0;
static bool             s_bridgeLastKaniValid  = false;
static int              s_bridgeSampleCounter  = 0;  // counts 60Hz ticks toward next sample
static int              s_bridgeConsecLandSamples = 0;
static bool             s_bridgeWasLeaping     = false;
static int              s_bridgeTicksSinceXition  = 0;  // 60Hz ticks since last state transition (or engage)
static int              s_bridgeLeapCount      = 0;  // total leaps observed this engagement (diagnostic)

// Cached config for the engaged field (so the per-second log can read
// dir/walk values without re-running LookupConfig).
static FieldDriveMode s_engagedMode = MODE_DIRECTION;
static int8_t  s_engagedDirX    = 0;
static int8_t  s_engagedDirY    = 0;
static int32_t s_engagedTargetX = 0;
static int32_t s_engagedTargetY = 0;
static bool    s_engagedWalk    = false;

// v0.15.9.7: MODE_STAGED_DIRECTION state.
//
// s_engagedStages / s_engagedStageCount: pointer + count copied from the
// FieldConfig at engagement time. Per-tick refresh uses this without
// having to re-look-up the config.
//
// s_currentStageIdx: index of the currently-active stage. -1 means
// uninitialized (set in Engage()); each tick the refresh code re-picks
// the stage by current Y position, and logs a transition message when
// the index changes.
static const FieldStage* s_engagedStages     = nullptr;
static int               s_engagedStageCount = 0;
static int               s_currentStageIdx   = -1;

// FF8 entity-array stride for "others" (party slots). Same value
// field_navigation.cpp uses internally. Squall in the chase scene is
// always entity[0] -- the lead member. Position bytes at +0x190 (X * 4096)
// and +0x194 (Y * 4096), matching field_nav_helpers.inl::GetEntityPos.
static const uint32_t ENTITY_STRIDE_OTHERS = 0x264;

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
