// field_nav_state.inl -- the navigation subsystem's state declarations.
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included from inside `namespace FieldNavigation`, at exactly the point these
// declarations used to sit, so the move is behaviour-identical by construction.
//
// v0.55.0: field_navigation.cpp was 81,827 bytes against an 81,920-byte CI hard
// fail -- 93 bytes of headroom, and three new minigames to wire in. This block
// (the catalog cursor, auto-drive, GPS, waypoints, the SET3/PSHM_W capture
// buffers and the camera-axis state) is the largest contiguous run in the file
// that declares state and defines no functions, so it moves whole and nothing
// else changes. tests/lint_braces.py checks the split did not lose a brace.

// Index into s_catalog[] of the currently selected entity.
// This is locked to field-load order, not distance order.
static int s_selectedCatalogIdx = 0;

// --- Auto-drive state ---
// When active, Update() injects arrow-key presses each tick to walk the player
// toward the selected entity.  s_driveHeld tracks which direction keys are
// currently being held so we can release them cleanly when stopping.
static bool    s_driveActive = false;
static uint8_t s_driveHeld   = 0;   // bitmask: DIR_UP/DOWN/LEFT/RIGHT

// v0.18.3.236 (#72): battle pause/resume for F9 auto-drive.
// Aaron's 2026-07-12 Fire Cavern run: battles that started mid-drive left the
// fake gamepad installed for the whole battle (Update() early-returns off-field,
// so nothing ever tore it down). The held steer state masked real arrow input in
// the battle menus — only the direction being held appeared to work. These
// statics let PollBattlePauseResume() (defined above Update()) stop the drive on
// the battle-entry edge and re-issue it to the same catalog target once the
// field is back and the player position has settled. Consumed as a synthetic
// backslash press in HandleKeys (field_nav_handlekeys.inl).
static bool     s_battlePausePending     = false;   // drive paused, resume armed
static bool     s_driveResumeRequest     = false;   // one-shot: HandleKeys restarts drive
static uint16_t s_battlePauseFieldId     = 0xFFFF;  // field the drive was paused on
static int      s_battlePauseEntityIdx   = 0;       // catalog target identity...
static int      s_battlePauseGatewayIdx  = -1;      // ...(entityIdx + gatewayIdx pair)
static bool     s_battlePauseTargetValid = false;
static int      s_battleResumeReadyTicks = 0;       // consecutive ready ticks post-battle

// Stuck detection: if the player's triId hasn't changed for DRIVE_STUCK_THRESH
// ticks, briefly inject a perpendicular direction to break free of walls.
// We track triId directly (not centroid) because centroid is constant within a
// triangle — the player can walk the full width of a large triangle without the
// centroid changing, which caused false stuck fires at the old 30-tick threshold.
static uint16_t s_driveLastTriId    = 0xFFFF;
static int      s_driveStuckTicks   = 0;
static int      s_driveWiggleTicks  = 0;
static uint8_t  s_driveWiggleDir    = 0;   // current wiggle direction
static int      s_driveWigglePhase  = 0;   // v05.68: rotates through 8 recovery directions
// v0.17.6.1: Player's walkmesh triangle at the most recent recovery cycle.
// Set to 0xFFFF at drive start (no prior recovery). When the recovery block
// fires and the player's current tri differs from s_lastRecoveryTri, that
// signals genuine corridor progress (the player escaped the previous triangle
// and entered the next one) and we reset s_driveWigglePhase = 0 to give the
// new triangle a fresh recovery budget. Without this reset, the v0.17.6.0
// BAT showed the recovery counter inflating across 5 corridor advances
// (tri 367 -> 366 -> 363 -> 362 -> 359) and hitting MAX_RECOVERY_PHASES at
// recovery 12 even though the player was making real progress.
static uint16_t s_lastRecoveryTri    = 0xFFFF;
static const int DRIVE_STUCK_THRESH  = 80;  // v06.14: ticks before wiggle (~1.3s, was 40). Analog steering needs more settling time.
static const int DRIVE_WIGGLE_TICKS  = 18;  // v05.90: quick nudge (~0.3s, was 45)
static const int NUDGE_TICKS         = 8;   // v06.07: micro-nudge duration (~0.13s)
static const int MAX_RECOVERY_PHASES = 30;  // v0.17.6.1: bumped from 12. Narrow corridors (e.g. bghall_1 save-point alcove) need 2-3 recoveries per triangle to escape; 12 was too small for multi-triangle traversals. The new tri-advance reset (see s_lastRecoveryTri logic in UpdateAutoDrive) also resets the counter on genuine progress, so 30 only fires as a true "can't escape this triangle" cap.

// v05.90: Velocity-based stuck detection — track position over a rolling window.
// If the player moves less than DRIVE_STUCK_MIN_DIST world units over
// DRIVE_STUCK_THRESH ticks, they're stuck. This catches oscillation within
// a single large triangle that triId-based detection misses.
static float    s_driveStuckPosX    = 0;
static float    s_driveStuckPosY    = 0;
static const float DRIVE_STUCK_MIN_DIST = 20.0f;  // must move at least this far per window

// v06.10: Progress-toward-target stuck detection.
// Tracks distance-to-target at the start of each stuck window. If the player
// is "moving" (resets velocity-based detection) but not closing distance to
// the target over consecutive windows, they're micro-oscillating.
// Example: bggate_2 NPC — player bounces tri 126<->127 (~31 unit moves),
// enough to reset DRIVE_STUCK_MIN_DIST=20, but dist-to-target stays ~5044.
static float    s_driveProgressDist    = 1e30f;  // dist-to-target at start of current progress window
static int      s_driveNoProgressCount = 0;       // consecutive stuck windows without meaningful progress
static const float DRIVE_PROGRESS_MIN  = 30.0f;   // must close this much distance per window to count as progress
static const int   DRIVE_NO_PROGRESS_MAX = 3;     // trigger recovery after this many no-progress windows

static const uint8_t DIR_UP    = 0x1;
static const uint8_t DIR_DOWN  = 0x2;
static const uint8_t DIR_LEFT  = 0x4;
static const uint8_t DIR_RIGHT = 0x8;

// Hardware scan codes for the dedicated (non-numpad) arrow keys.
// DirectInput reads raw hardware scan codes, not VK codes.
// These are the E0-prefixed scan codes; KEYEVENTF_EXTENDEDKEY signals E0 prefix.
static const WORD SC_UP    = 0x48;
static const WORD SC_DOWN  = 0x50;
static const WORD SC_LEFT  = 0x4B;
static const WORD SC_RIGHT = 0x4D;

static const float DRIVE_ARRIVE_DIST_DEFAULT = 300.0f; // fallback for non-entity targets
static float       s_driveArriveDist = 300.0f;  // v05.80: per-drive arrive distance (from entity talk radius)

// v06.21: Talk radius expansion for hard-to-reach NPCs.
// When recovery fires near a target NPC, we expand the game's actual talk
// radius so the player can interact from further away. This "meet in the
// middle" strategy combines auto-drive getting close with a forgiving
// interaction zone. Original radius is restored when the drive ends.
static int         s_driveTargetEntityIdx = -1;   // entity index of NPC target (for radius restore)
static uint16_t    s_driveOrigTalkRadius  = 0;    // original talk radius before expansion
static bool        s_driveTalkRadExpanded = false; // true if we've written an expanded radius
static const float TALK_RAD_EXPAND_FACTOR = 2.5f; // multiply original radius by this
static const float TALK_RAD_EXPAND_MAX    = 350.0f; // cap expanded radius
static const float TALK_RAD_EXPAND_DIST   = 500.0f; // only expand when player is within this distance
static const float DRIVE_AXIS_THRESH  = 150.0f; // ignore axis below this magnitude
static const int   DRIVE_MAX_TICKS    = 2400;   // v05.68: max drive time ~40s (increased for tighter arrive dist)
static int         s_driveTotalTicks  = 0;
static int         s_driveLogTimer    = 0;       // v05.62: periodic position log
static float       s_driveStartDist   = 0;       // v06.08: starting distance for NavLog

// v05.76: For trigger line targets, track which side the player started on
// so we can detect when they've crossed the line.
static float       s_driveTrigCrossStart = 0.0f; // cross product at drive start
static bool        s_driveTrigTarget     = false; // true if driving to a trigger line
// v06.05: Trigger line index to skip during A* and recovery (target trigger line).
static int         s_driveSkipTrigIdx    = -1;
// v0.15.9.2.15: Explicit crossing-line endpoints for chase-drive. The crossing
// detection in UpdateAutoDrive can fire for either a SETLINE-defined trigger
// (chosen by trigger-line index, copied here) or an INF gateway (passed
// directly by chase_auto_pilot). Unifying both into one pair of endpoint state
// variables keeps the crossing-check block simple and lets us extend to other
// crossing sources later without further plumbing changes.
static int16_t     s_driveCrossLineX1    = 0;
static int16_t     s_driveCrossLineY1    = 0;
static int16_t     s_driveCrossLineX2    = 0;
static int16_t     s_driveCrossLineY2    = 0;
static bool        s_driveCrossLineActive = false;

// v0.118.0 (#centra): the island-bridge redirect, in state the TICK LOOP reads.
// v06.01 already found the right bridge and then wrote it into a local that
// UpdateAutoDrive never sees, so the per-tick target snapped back to the
// unreachable gateway and six recoveries went nowhere. See
// drive_bridge_target_model.inl.
#include "drive_bridge_target_model.inl"
static bool        s_driveBridgeActive  = false;
static float       s_driveBridgeX       = 0.0f;
static float       s_driveBridgeY       = 0.0f;
static int         s_driveBridgeLineIdx = -1;

// v0.119.0 (#centra): the drive goal's height, when the target has one (a
// SETLINE trigger line or an INF gateway). The recovery re-paths re-derive the
// goal triangle and must not fall back to the 2D answer, which on a stacked
// mesh can be a ladder rung a thousand units in the air.
static float       s_driveGoalZ      = 0.0f;
static bool        s_driveGoalZValid = false;

// v0.120.0 (#centra): the BTNTEST mask the drive's target waits on, and the name
// to say with it. A ladder does nothing until a button is held, so the drive
// must arrive ON it rather than pushing 300 units through it, and must say what
// to press. See button_mask_model.inl.
#include "button_mask_model.inl"

// v0.131.1 (#centra): is the drive actually getting anywhere, and what to do
// about it when it is not. See drive_progress_model.inl.
#include "drive_progress_model.inl"

// v0.131.6 (#centra): which triangle a drive target is standing on, when the
// target is an entity. See drive_goal_tri_model.inl.
#include "drive_goal_tri_model.inl"

// v0.132.1 (#shumi): who survives the runtime entity scan -- the Sculptor did
// not and four decorative fish did. See scan_keep_model.inl.
#include "scan_keep_model.inl"

// v0.132.2 (#shumi): an event line the player has to press a button on is an
// interaction, not scenery. See line_event_surface_model.inl.
#include "line_event_surface_model.inl"


// v0.132.0 (#shumi): which INF gateways A* must route around, and which one the
// drive is deliberately walking through. See gateway_avoidance_model.inl.
#include "gateway_avoidance_model.inl"

// The gateway this drive is heading for, or -1. Set alongside s_driveSkipTrigIdx
// and cleared the same way; it is what keeps a drive TO a doorway from planning
// a route that avoids the doorway.
static int s_driveSkipGatewayIdx = -1;

// v0.132.0 (#shumi): which "jump"-named trigger lines are camera-view
// transitions and which are the way out of the village. See
// camera_transition_model.inl.
#include "camera_transition_model.inl"

// v0.131.7 (#centra): which script-derived map exits are real exits. See
// jsm_exit_surface_model.inl.
#include "jsm_exit_surface_model.inl"
#include "ladder_cue_model.inl"   // v0.123.0 (#centra): the climb cue
static uint16_t    s_driveButtonMask = 0;
static char        s_driveButtonLabel[48] = {};
// v0.121.0 (#centra): when the drive was redirected to a bridge, the name of
// what the player actually selected, so the arrival can say where this leads.
static char        s_driveButtonLeadsTo[48] = {};
// v0.122.0 (#centra): where the player's foot falls along the target line, kept
// so the arrival log can carry it out of the block that computes it.
static float       s_driveButtonAlongT = -99.0f;

// --- Camera axes (loaded per field from .ca file for screen-space mapping) ---
static FieldArchive::CameraAxes s_cameraAxes = {};

// --- GPS Guidance state (v0.12.02; projection rewritten v0.17.0) ---
// Continuous directional guidance to selected entity.
// Backspace toggles on/off. Provides distance+direction announcements
// where direction is a cardinal (north/northeast/east/...) derived by
// projecting the walkmesh delta through the per-field camera axes
// (s_camRightX/Y, s_camDownX/Y; populated from .ca file at field load).
// Cardinals map to arrow keys: north=up, east=right, south=down, west=left.
// See field_nav_gps.inl::ComputeScreenDirIndex for the projection + binning.
static bool    s_gpsActive = false;        // guidance is currently on
static int     s_gpsCatalogIdx = -1;       // which catalog entry we're guiding to
static float   s_gpsLastDist = 1e30f;      // distance at last full announcement
static DWORD   s_gpsLastAnnounceTime = 0;  // tick of last spoken announcement
static bool    s_gpsArrivedAnnounced = false; // true once "In range" has been said
static bool    s_gpsNearbyAnnounced = false;  // true once "Nearby" has been said
static float   s_gpsArriveDist = 120.0f;      // per-target arrival threshold (from talk radius)
static float   s_gpsNearbyDist = 500.0f;      // per-target nearby threshold (2x arrive dist)

// GPS tuning constants
static const DWORD GPS_ANNOUNCE_INTERVAL_FAR  = 3000;  // 3s when far (>500 units)
static const DWORD GPS_ANNOUNCE_INTERVAL_NEAR = 1500;  // 1.5s when near (200-500 units)
static const DWORD GPS_ANNOUNCE_INTERVAL_CLOSE = 800;  // 0.8s when very close (<200 units)
static const float GPS_NEARBY_DIST = 500.0f;           // "Nearby" threshold
static const float GPS_CLOSE_DIST  = 200.0f;           // switch to close interval
static const float GPS_ARRIVE_DIST = 120.0f;           // "In range" threshold (default for non-entity targets)
static const float GPS_ARRIVE_MARGIN = 0.9f;           // v0.12.07: safety margin — arrive at 90% of talk radius

// v0.17.0: Cardinal direction names (8-way, indexed by ComputeScreenDirIndex result).
// Cardinals are SCREEN-relative and map directly to arrow keys:
//   north = up arrow, east = right, south = down, west = left.
//   northeast = up+right, etc.
// The mapping from walkmesh delta to cardinal goes through the per-field
// camera projection (s_camRightX/Y, s_camDownX/Y populated from .ca file
// at field load — see field_nav_fieldscripts.inl). On default-camera fields
// the projection is identity; on rotated cameras the projection ensures
// 'east' really does mean 'press the right arrow on the keyboard.'
static const char* GPS_DIR_NAMES[] = {
    "north",     "northeast", "east",      "southeast",
    "south",     "southwest", "west",      "northwest"
};

// Step conversion: divide entity distance units by this to get "steps".
// v0.12.06: Calibrated from empirical measurement. Player walked 3840 entity
// units in 5 seconds with ~12 audible footstep sounds = ~320 units per step.
// Previous value (24) overcounted by ~13x.
static const float GPS_STEPS_DIVISOR = 320.0f;

// Movement rate tracking for step calibration (v0.12.03 diagnostic).
static float  s_gpsDiagLastX = 0, s_gpsDiagLastY = 0;
static DWORD  s_gpsDiagLastTick = 0;
static float  s_gpsDiagAccumDist = 0;  // distance accumulated since last log
static int    s_gpsDiagFrames = 0;     // frames counted since last log

// --- v0.12.05: Animation scan diagnostic (F12 key) ---
// Snapshots the entire player entity struct (0x264 bytes) at F12 press,
// then polls every 100ms for 5 seconds while walking. Logs only bytes
// that change, to find the walking animation frame counter.
static bool    s_animScanActive = false;
static DWORD   s_animScanStart = 0;
static DWORD   s_animScanLastPoll = 0;
static int     s_animScanPollCount = 0;
static uint8_t s_animBaseline[0x264] = {};   // snapshot at scan start
static uint8_t s_animPrev[0x264] = {};       // previous poll snapshot
static int     s_animChangeCount[0x264] = {}; // how many times each byte changed
static uint8_t s_animMinVal[0x264] = {};     // min value seen at each offset
static uint8_t s_animMaxVal[0x264] = {};     // max value seen at each offset
static float   s_animScanStartX = 0, s_animScanStartY = 0; // position at scan start
static const DWORD ANIM_SCAN_DURATION_MS = 5000;
static const DWORD ANIM_SCAN_INTERVAL_MS = 100;

// --- Walkmesh data (loaded per field for A* pathfinding) ---
static FieldArchive::WalkmeshData s_walkmesh = {};

// --- A* waypoint path ---
static const int MAX_WAYPOINTS = 256;
static float     s_waypoints[MAX_WAYPOINTS][2] = {};  // (x, y) centers
static int       s_waypointCount = 0;
static int       s_waypointIdx   = 0;   // current waypoint we're heading toward
static const float WAYPOINT_ARRIVE_DIST = 400.0f;  // v05.65: for triangle-center waypoints (skip past dense clusters)
// v05.95: Funnel waypoints are precise turn points — must follow closely.
static const float FUNNEL_ARRIVE_DIST = 60.0f;
static bool        s_usingFunnel = false;  // true when waypoints came from FunnelPath

// v06.08: Closest-approach waypoint overshoot detection.
// Tracks the minimum distance seen to the current waypoint. When the player
// gets close and then the distance starts increasing, advance to the next wp.
// This prevents oscillation where the player passes through a waypoint zone
// but never gets close enough to trigger the FUNNEL_ARRIVE_DIST threshold.
static float       s_wpMinDist = 1e30f;    // min distance to current wp (reset on wp change)
static const float WP_OVERSHOOT_CLOSE = 200.0f;  // must get within this distance first
static const float WP_OVERSHOOT_RATIO = 1.5f;    // advance when dist > minDist * ratio + margin
static const float WP_OVERSHOOT_MARGIN = 50.0f;  // absolute margin above minDist

// v05.90: Triangle corridor from A* for funnel algorithm.
// Stores the sequence of triangle indices that A* found, which the
// funnel algorithm then processes into a smooth path.
static const int MAX_CORRIDOR = 4096;
static uint16_t  s_corridor[MAX_CORRIDOR] = {};
static int       s_corridorCount = 0;

// --- Hook trampolines ---
typedef int  (__cdecl *FieldScriptsInit_t)(int, int, int, int);
typedef void (__cdecl *SetCurrentTriangle_t)(int, int, int);
typedef int  (__cdecl *OpcodeHandler_t)(int);
static FieldScriptsInit_t   s_originalFieldScriptsInit   = nullptr;
static SetCurrentTriangle_t s_originalSetCurrentTriangle = nullptr;
static OpcodeHandler_t      s_originalSetline            = nullptr;
static OpcodeHandler_t      s_originalLineon             = nullptr;
static OpcodeHandler_t      s_originalLineoff            = nullptr;
static OpcodeHandler_t      s_originalTalkradius         = nullptr;
static OpcodeHandler_t      s_originalPushradius         = nullptr;
static OpcodeHandler_t      s_originalSet3               = nullptr;  // v0.08.03
static OpcodeHandler_t      s_originalPshmW              = nullptr;  // v0.08.07

// v0.08.03: SET3 opcode hook — capture entity positions at runtime.
// v0.08.26: PERSISTENT — always captures, no time window. This catches entities
// like dic (bghall_1 Directory) whose SET3 fires outside field_scripts_init.
// Entities with PSHM_W coordinates have their positions resolved by the engine
// at runtime. We capture the real positions from the entity state struct after
// SET3 executes.
struct CapturedSET3 {
    uint32_t entityAddr;
    int16_t  posX, posY, posZ;
    uint16_t triId;
    bool     firstLogged;  // v0.08.26: true once this slot has been logged
};
static const int MAX_SET3_CAPTURES = 64;
static CapturedSET3 s_set3Captures[MAX_SET3_CAPTURES] = {};
static int  s_set3CaptureCount = 0;
static bool s_capturingSET3 = false;  // v0.08.26: set true on first field load, stays true forever
static DWORD s_set3CaptureStartTime = 0;  // v0.08.16: GetTickCount() when capture started (kept for init summary)
static const DWORD SET3_CAPTURE_DURATION_MS = 3000;  // v0.08.16: 3s window for init summary logging
static bool s_set3SummaryLogged = false;  // v0.08.16: true once post-init summary is logged
static int  s_set3TotalCalls = 0;  // v0.08.26: total SET3 calls (for periodic summary)

// v0.08.07: PSHM_W opcode hook — capture all shared memory reads during field_scripts_init.
// Logs the entity address, PSHM_W address parameter, execution flags, and the
// value pushed to the VM stack after the read completes. This reveals which
// active entities read from the same addresses as dic (135, -82, -8019) and
// what the handler resolves those addresses to.
struct CapturedPSHM {
    uint32_t entityAddr;
    int16_t  param;        // PSHM_W address parameter (signed)
    uint32_t execFlags;    // entity execution flags at offset 0x160
    int32_t  resultValue;  // value pushed to VM stack after read
};
static const int MAX_PSHM_CAPTURES = 256;
static CapturedPSHM s_pshmCaptures[MAX_PSHM_CAPTURES] = {};
static int  s_pshmCaptureCount = 0;
static bool s_capturingPSHM = false;  // true for ~5s after field load
static DWORD s_pshmCaptureStartTime = 0;  // GetTickCount() when capture started
static const DWORD PSHM_CAPTURE_DURATION_MS = 5000;  // capture window after field load
static bool s_pshmSummaryLogged = false;  // true once end-of-window summary is logged

// v0.14.45: POPM_W/B/L shared memory write capture (F12 diagnostic) removed.
// Was used in v0.12.22 for Director varblock investigation (resolved session 43:
// SETLINE triggers are definitive, Director entities are dead code). Removed:
// - POPM_*_SHARED_ADDR constants, VARBLOCK_BASE, PopmSharedHandler_t typedef,
//   s_originalPopm[WBL] trampolines
// - CapturedVarWrite struct + s_varWrites[] + counters + capture window state
// - HookedPopm[WBL]Shared handlers in field_nav_opcode_hooks.inl
// - MH hook install/remove in Initialize()/Shutdown()
// - F12 trigger in field_nav_handlekeys.inl + Update() summary block

// v0.08.23: Descriptor table polling probe — check if PSHM_W entity-scope descriptors
// populate after field load. Table at 0x01DCB340 holds one 4-byte pointer per flat
// entity index. If non-NULL, points to a ~0x90-byte descriptor struct:
//   +0x00: int32 validity marker (-1 = invalid)
//   +0x0C: int16 computed X coordinate
//   +0x0E: int16 computed Y coordinate
//   +0x68: uint32 curve data pointer
//   +0x7E: int16 cache key (last PSHM address processed)
// We poll every ~1s for 10s after field load to see if any descriptors appear.
static bool  s_descriptorPollActive = false;
static DWORD s_descriptorPollStart = 0;
static int   s_descriptorPollCount = 0;
static DWORD s_descriptorPollLastCheck = 0;
static bool  s_descriptorPollSummaryLogged = false;
static uint16_t s_descriptorPollFieldId = 0xFFFF;  // last field ID we started polling for
static const DWORD DESCRIPTOR_POLL_DURATION_MS = 10000;  // poll for 10s after field load
static const DWORD DESCRIPTOR_POLL_INTERVAL_MS = 1000;   // check every 1s
static const uint32_t DESCRIPTOR_TABLE_ADDR = 0x01DCB340;  // runtime VA, per-entity ptr table
static const int MAX_DESCRIPTOR_SCAN = 64;  // max flat entity indices to check

// v0.12.22: Varblock poller — auto-poll shared memory after field load.
static bool  s_varblockPollActive = false;
static DWORD s_varblockPollStart = 0;
static int   s_varblockPollCount = 0;
static DWORD s_varblockPollLastCheck = 0;

#if FEPIC1_GATE_DIAG
// v0.17.9.12: fepic1 gate diagnostic one-shot arming. Set in
// HookedFieldScriptsInit when the loaded field is fepic1; the dump fires once
// from Update() after GATEDIAG_DELAY_MS so the init scripts have populated the
// captured trigger lines and the player entity has settled at its spawn.
static bool  s_gateDiagPending = false;
static DWORD s_gateDiagArmTime = 0;
static const DWORD GATEDIAG_DELAY_MS = 1500;
#endif

// v0.08.24: One-shot hex dump of PSHM_W entity-scope functions.
// Reads raw x86 instruction bytes from the two key subroutines so we can
// disassemble the parametric curve formula without Ghidra/IDA.
static bool s_pshmFuncDumpDone = false;  // true once the dump has fired

// v05.82: engine_eval_keyboard_gamepad_input hook for analog input diagnostic + steering
typedef void (__cdecl *EngineEvalInput_t)();
static EngineEvalInput_t    s_originalEngineEvalInput    = nullptr;

// v05.89: get_key_state hook for arrow key suppression.
// get_key_state fills the 256-byte keyboard buffer from hardware/FFNx.
// By hooking it, we can zero arrow keys AFTER the buffer is filled but BEFORE
// ctrl_keyboard_actions reads direction from it. This is the correct timing
// because both get_key_state and ctrl_keyboard_actions are called from within
// engine_eval_keyboard_gamepad_input.
typedef int (__cdecl *GetKeyState_t)();
static GetKeyState_t s_originalGetKeyState = nullptr;
static DWORD                s_gpDiagLastDump             = 0;
static bool                 s_gpDiagEnabled              = true;  // fires ~once/sec on field (reset for v05.88 diag)

// v05.83/84: Analog steering injection state.
// Written by UpdateAutoDrive (mod thread), read by HookedEngineEvalInput (game thread).
static volatile bool    s_analogOverrideActive = false;
static volatile int32_t s_analogDesiredLX      = 0;    // DIJOYSTATE2 lX: -1000..+1000, 0=center
static volatile int32_t s_analogDesiredLY      = 0;    // DIJOYSTATE2 lY: -1000..+1000, 0=center

// v05.84: Fake gamepad device injection.
// When auto-drive is active, we set the game's dinput_gamepad_device pointer
// to a non-null sentinel so the game thinks a gamepad is connected, and point
// dinput_gamepad_state at our fake DIJOYSTATE2 struct with our desired lX/lY.
// DIJOYSTATE2 is 272 bytes. We only care about lX (offset 0) and lY (offset 4).
static uint8_t  s_fakeDIJOYSTATE2[272] = {};  // zeroed = all centered/released
static uint32_t s_savedDevicePtr = 0;          // original *pDinputGamepadDevicePtr
static uint32_t s_savedStatePtr  = 0;          // original *pDinputGamepadStatePtr
static bool     s_fakeGamepadInstalled = false;
static const uint32_t FAKE_DEVICE_SENTINEL = 0xDEAD0001;  // non-null, non-real pointer

// Diagnostic log throttle for set_current_triangle.
// v05.58: Reduced to 0 (set_tri logging removed — walkmesh capture confirmed working).
static int       s_setTriCallCount = 0;
static const int SET_TRI_LOG_MAX   = 0;

// v05.48: Entity diagnostic dump flag (reset on field load).
// v05.58: ENTDIAG/BGDIAG dumps removed — entity classification confirmed working.
static bool      s_entDiagDumped   = true;   // true = skip old ENTDIAG dump

// v0.18.3.227: Race-free interactability capture.
// The TALKRADIUS/PUSHRADIUS opcodes fire during field-script execution, which
// often runs AFTER the first catalog scan. That means an entity's runtime
// talkonoff/pushonoff FLAGS (0x24B/0x249) can still read 0 at catalog-build
// time even for genuinely talkable NPCs (the "train guard" symptom). We instead
// capture the radius directly in the opcode hook — which fires ONLY for entities
// that actually declare a radius — into a per-entity side table keyed by entity
// index. This is race-free (populated by the time the player browses the
// catalog) and offset-robust (no struct-layout guessing at scan time). Cleared
// on field load by HookedFieldScriptsInit. A nonzero entry means "this entity
// is talkable this field", used by RefreshCatalog for both classification and
// the party-member filter.
static uint16_t  s_entTalkRadius[MAX_ENTITIES] = {};

// v0.18.3.235: STICKY talkability. The runtime talkonoff flag is not merely set
// late — it is TRANSIENT. On ggroom1 (G-Garden reception) Quistis reads
// talk=1/push=1 on the first catalog build and 0 on every build after, so she was
// kept and named on entry and then silently party-filtered a second later. Once an
// entity has been observed talkable on a field, remember it for the rest of that
// field. Cleared on field load by HookedFieldScriptsInit.
static bool      s_entSeenTalkable[MAX_ENTITIES] = {};

// v0.18.3.231: one-shot extended entity scan (reads past the engine's reported
// entity count). Retired — kept for re-arming if the entity array ever needs
// re-triage on a new field (this is what located the G-Garden train staff).
static bool      s_extScanDumped = true;

// v0.18.3.234: catalog scan trace, emitted ONCE per field load (reset by
// HookedFieldScriptsInit). RefreshCatalog runs ~1/sec, so tracing every rebuild
// would flood the log; once per field is enough to diagnose a missing entity.
static bool      s_scanTraced    = true;

// v0.131.8: the same rule for the map-exit injector's drop lines. s_scanTraced
// cannot serve here -- RefreshCatalog sets it true before it calls
// InjectMapExits, so the injector has never once seen it false -- and the
// v0.131.7 party-member/no-position filter printed seven lines four times over
// in the space of a few seconds because of it. Its own flag, reset on the same
// field load, so the reason an exit is missing is stated once and findable.
static bool      s_mapExitTraced = true;

// v05.50: Background entity diagnostic dump flag (reset on field load).
static bool      s_bgDiagDumped    = true;   // true = skip old BGDIAG dump

// v05.59: Coordinate diagnostic flag (fires once per field).
static bool      s_coordDiagDumped = false;

// v0.14.107: Party-state diagnostic flag. Set to false on field load by
// HookedFieldScriptsInit so the [party-state] log fires once per field.
// Initial value `true` here so we don't double-log on the very first load
// (HookedFieldScriptsInit will reset to false right before the engine
// populates the savemap for the new field).
static bool      s_partyDiagDumped = true;
static bool      s_puzzleDiagDumped = true;  // v0.18.3.267: [PUZZLE-DIAG] one-shot per field

// v06.13: CoordSample Approach B — track player's previous triangle for
// shared-edge midpoint computation. Separate from s_hookPrevTri[] because
// that array is updated inside the entity scan loop before we can read it.
static uint16_t  s_coordPrevPlayerTri = 0;

// v06.13: Approach C diagnostic — position-write detector.
// Snapshot the player's 2D position before and after engine_eval to detect
// when the engine writes the projection result. When a change is detected,
// log the return addresses from the call stack to identify the projection
// function. Fires only on angled fields (Z range > 100 in walkmesh) and
// only the first 10 detections per field to avoid log spam.
static int32_t   s_projDiagPrevFpX = 0;
static int32_t   s_projDiagPrevFpY = 0;
static int       s_projDiagCount = 0;
static const int PROJ_DIAG_MAX = 10;

// v0.14.75: VISDIAG removed; F11 reassigned to global screenshot in
// dinput8.cpp. s_f11WasDown declaration deleted along with the handler.

// v05.39: Track which entities have logged the struct-position fallback.
static uint16_t  s_structFallbackLogged = 0;

// v06.14/v0.07.76: Per-field camera heading axes for analog steering + compass directions.
// Calibrated empirically on first auto-drive; default = identity (world axes).
// Declared here (before FormatNavComponents) so compass directions can use them.
//
// v0.17.2: SPLIT INTO TWO PAIRS to stop auto-drive's empirical calibration
// from corrupting manual-nav's screen-projection.
//
//   s_camRightX/Y, s_camDownX/Y  -- MANUAL NAV axes. Set at field load by
//                                    HookedFieldScriptsInit:
//                                      1. Parse .ca file -> 2D-normalize axis0/axis1
//                                         (v0.17.0.1).
//                                      2. Det convention check -- negate camDown if
//                                         det(camRight, camDown) > 0 so left-handed
//                                         .ca fields (e.g. bg2f_2 classroom) end up
//                                         in standard screen-down convention
//                                         (v0.17.4).
//                                      3. Quantize camRight angle to nearest 90deg
//                                         and derive camDown by rotating 90deg CW
//                                         from camRight (v0.17.5). The engine
//                                         appears to use per-axis world-cardinal
//                                         input mapping; quantization makes
//                                         predicted == engine actual on every
//                                         clean field and ~5-11deg off on bg2f_2
//                                         (well within the 22.5deg sector
//                                         tolerance).
//                                    Or identity defaults when .ca is absent /
//                                    degenerate. NEVER written by anything after
//                                    field load -- no observer cal, no auto-drive
//                                    cal. Read by:
//                                      - field_nav_gps.inl :: ComputeScreenDirIndex (GPS cardinals)
//                                      - field_nav_gps.inl :: [NAV-PROJ] log lines
//                                      - field_nav_helpers.inl :: FormatNavComponents (Backspace announce)
//
//   s_driveCamRightX/Y, s_driveCamDownX/Y -- AUTO-DRIVE PRIVATE axes. Mirrors
//                                    the manual-nav pair at field load (so
//                                    auto-drive starts from CA values), then
//                                    overwritten by the empirical calibration
//                                    in field_nav_autodrive.inl's phase 1/2.
//                                    Read by:
//                                      - field_nav_autodrive.inl :: SetAnalogFromVector
//
// Background: v0.17.1 BAT showed manual-nav cardinals reading wrong axes ~3
// minutes into a session. The simplest explanation was that auto-drive's
// calibration had overwritten s_camRight/Down at some intermediate field.
// The chase doc "Auto-drive lessons" Finding #10 also notes that empirical
// calibration and .ca-derived values aren't proven equivalent on every field
// (the chase doc's verified-working axis source is empirical calibration; the
// CA-derived values are still in evaluation for manual nav). Splitting the
// state pairs lets each consumer use the source most appropriate for it
// without cross-contamination, and the next BAT log will show definitively
// whether the wrong-cardinal issue persists once calibration can no longer
// touch the manual-nav pair.
static float s_camRightX = 1.0f;   // camera right vector X component (normalized) -- MANUAL NAV
static float s_camRightY = 0.0f;   // camera right vector Y component                -- MANUAL NAV
static float s_camDownX  = 0.0f;   // camera down vector X component                 -- MANUAL NAV
static float s_camDownY  = -1.0f;  // camera down vector Y component (default: -Y world = screen-down) -- MANUAL NAV
static float s_driveCamRightX = 1.0f;   // AUTO-DRIVE private camera right X (calibration-mutable)
static float s_driveCamRightY = 0.0f;   // AUTO-DRIVE private camera right Y
static float s_driveCamDownX  = 0.0f;   // AUTO-DRIVE private camera down X
static float s_driveCamDownY  = -1.0f;  // AUTO-DRIVE private camera down Y
// v0.17.2: Source tag for the MANUAL-NAV camera axes pair. Logged in [NAV-PROJ]
// lines so the BAT log makes obvious whether the axes came from the .ca file
// (preferred) or fell back to identity defaults.
//   "identity" = no .ca file or degenerate 2D projection; world-bearing fallback.
//   "ca-file"  = parsed from the field's .ca and normalized to 2D unit length.
static const char* s_camAxesSource = "identity";
static bool  s_camCalibrated = false;

// v0.17.7.6: Closed-loop empirical camera-axes calibration state.
//
// Active ONLY when s_camAxesSource == "identity" (CA file missing or its
// 2D projection was degenerate). The observer in field_nav_observe.inl
// pushes single-arrow measurements into s_navObsBuffer; when
// OBS_EMPIRICAL_MIN_SAMPLES recent samples for the same arrow agree within
// the threshold, the consensus direction is mapped to the camera axis
// the arrow corresponds to, snapped to nearest 90-degree world cardinal,
// and the orthogonal axis is derived via the standard det=-1 rotation.
// Both s_cam*X/Y (manual nav) and s_driveCam*X/Y (auto-drive private)
// are overwritten so all consumers pick up the corrected values.
//
// s_camAxesEmpiricalApplied locks the correction to one-shot per field
// load. All three fields are reset in HookedFieldScriptsInit.
//
// REGRESSION SAFETY: on any field where the CA file's 2D projection
// was non-degenerate, s_camAxesSource was set to "ca-quantized" by the
// CA-load block; this state is never touched and the observer never
// pushes a sample. Behavior on those fields is byte-for-byte identical
// to v0.17.7.5.5.
struct NavObsSample {
    float wx;   // normalized world-X of measured movement direction
    float wy;   // normalized world-Y of measured movement direction
};
static const int OBS_NUM_ARROWS  = 4;   // index 0=UP, 1=DOWN, 2=LEFT, 3=RIGHT
static const int OBS_MAX_SAMPLES = 8;
static NavObsSample s_navObsBuffer[OBS_NUM_ARROWS][OBS_MAX_SAMPLES] = {};
static int          s_navObsSampleCount[OBS_NUM_ARROWS] = {};
static bool         s_camAxesEmpiricalApplied = false;

