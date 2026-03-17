// field_navigation.cpp - Field navigation assistance for blind players
//
// See field_navigation.h for full architecture and phasing notes.
//
// ============================================================================
// CURRENT STATE: v0.07.16 — Save screen: arrow key tracking for slot cursor
// ============================================================================
//
// What's new in v05.12:
//   - Entity center cache: HookedSetCurrentTriangle now stores the world (x,z)
//     center of each triangle as entities move, keyed by entity index.
//   - F9 key: announces nearest character and compass direction from player.
//     Repeated presses cycle through catalog entities sorted by distance.
//   - F10 key: announces player's current field name, triangle ID, and position.
//
// How center capture works:
//   set_current_triangle(ptr0, ptr1, ptr2) is called every time any entity
//   transitions to a new walkmesh triangle. Each argument is a pointer to an
//   int16_t[3] vertex record (x,y,z). We compute center = mean(x,z) of the 3
//   vertices, then scan the entity array to find which entity's triangle ID
//   just changed, and store the center for that entity.
//
// Coordinate system (corrected v05.61 via bgroom_1 COORDDIAG):
//   FF8 walkmesh: +X = screen-right, -Y = screen-up (north).
//   Entity Y (offset 0x194) is the screen-vertical axis. Z (0x198) is
//   always ~0 (depth). Gateway INF and SETLINE Y values match entity Y.
//   Negative Y = screen-up (feart2f1 exit at Y=-4628 is top-right).
//
// Key bindings:
//   F9  = announce nearest character + direction (repeated = cycle outward)
//   F10 = announce player field name + position
//
// TODO Step 4c: Exit/gateway catalog from INF gateway table.
// TODO Step 5:  Full object cycling UI with entity names from script parsing.
// TODO Step 6:  Auto-drive input injection.

#include <windows.h>
#include <cmath>
#include <cstdio>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_dialog.h"
#include "field_archive.h"
#include "field_navigation.h"
#include "minhook/include/MinHook.h"

namespace FieldNavigation {

// ============================================================================
// Constants
// ============================================================================

static const int    MAX_ENTITIES  = 16;
static const int    MAX_BG_ENTITIES = 48;  // v05.50: background entities can be numerous
static const int    MAX_CATALOG   = 64;    // v05.50: increased to hold both arrays + gateways
static const DWORD  ENTITY_STRIDE = 0x264;   // bytes between "other" entity blocks
static const DWORD  BG_STRIDE     = 0x1B4;   // v05.50: bytes between background entity blocks
static const double NAV_PI        = 3.14159265358979323846;

// ============================================================================
// Module state
// ============================================================================

static bool     s_initialized = false;
static DWORD    s_lastLogTime = 0;

// --- Player entity detection (triangle-change scoring, Update() thread) ---
static uint16_t s_prevTriangles[MAX_ENTITIES] = {};
static int      s_changeScore[MAX_ENTITIES]   = {};
static int      s_playerEntityIdx             = -1;
static uint16_t s_cachedFieldId               = 0xFFFF;
static uint16_t s_playerTri                   = 0xFFFF;

// --- SYM entity names (loaded per field from archive) ---
// SYM lists ALL JSM entities (doors, lines, backgrounds, others).
// pFieldStateOthers only contains "others" — the last group.
// s_symOthersOffset = total SYM names - entityStateOtherCount
// gives the SYM index of the first "other" entity.
static const int    MAX_SYM_NAMES = 64;  // v05.50: increased for fields with many entities
static char         s_symNames[MAX_SYM_NAMES][32] = {};
static int          s_symNameCount = 0;
static int          s_symOthersOffset = 0;  // SYM index of first "other" entity

// v05.50: JSM counts (for SYM index mapping diagnostics)
static int          s_jsmDoors = 0;
static int          s_jsmLines = 0;
static int          s_jsmBackgrounds = 0;
static int          s_jsmOthers = 0;

// --- INF gateway exits (loaded per field from archive) ---
static const int    MAX_GATEWAYS  = 12;
static FieldArchive::GatewayInfo s_gateways[MAX_GATEWAYS] = {};
static int          s_gatewayCount = 0;

// --- INF trigger zones (loaded per field from archive, v05.54) ---
static const int    MAX_TRIGGERS  = 16;
static FieldArchive::TriggerInfo s_triggers[MAX_TRIGGERS] = {};
static int          s_triggerCount = 0;

// --- NPC catalog (rebuilt at each field load) ---
enum EntityType { ENT_UNKNOWN = 0, ENT_NPC, ENT_OBJECT, ENT_EXIT, ENT_BG_NPC, ENT_BG_OBJECT };

static const char* EntityTypeName(EntityType t) {
    switch (t) {
        case ENT_NPC:       return "NPC";
        case ENT_OBJECT:    return "Object";
        case ENT_EXIT:      return "Exit";
        case ENT_BG_NPC:    return "NPC";
        case ENT_BG_OBJECT: return "Object";
        default:            return "Entity";
    }
}

struct EntityInfo {
    int        entityIdx;    // entity array index, or -1 for gateway exits
    int16_t    modelId;
    uint16_t   triangleId;   // kept current in Update()
    EntityType type;         // v05.37: NPC / Object / Exit / Unknown
    char       name[48];     // v05.47: from SYM, or gateway destination name
    int        gatewayIdx;   // v05.47: index into s_gateways[] for ENT_EXIT, -1 otherwise
};
static EntityInfo s_catalog[MAX_CATALOG] = {};
static int        s_catalogCount         = 0;
static int        s_nonPlayerCount       = 0;  // catalog count minus player entity (stable M)

// --- Entity center cache (written by game thread hook, read by mod thread) ---
// Stores world (x,z) center of the walkmesh triangle each entity currently
// occupies. Written atomically enough for x86: float stores are 32-bit
// aligned, so torn reads at worst give a slightly stale value — acceptable
// for compass guidance.
struct EntityCenter {
    float cx;     // world X of triangle centre
    float cz;     // world Z of triangle centre
    bool  valid;  // true once first real data is captured
};
static EntityCenter s_entityCenters[MAX_ENTITIES] = {};

// Triangle-ID → spatial centre map.
// Populated by HookedSetCurrentTriangle; keyed by the entity's new triId.
// Because each triangle has a fixed walkmesh position, this lookup is always
// spatially correct regardless of which entity is standing on it — avoiding
// the false-attribution race that caused NPC centres to jump wildly.
static const int    MAX_TRI_ID = 4096;
static EntityCenter s_triCenter[MAX_TRI_ID] = {};

// Shadow triangle IDs for the hook, separate from Update()'s s_prevTriangles
// to avoid cross-thread clobbering.
static uint16_t s_hookPrevTri[MAX_ENTITIES] = {};

// --- Entity rescan (v05.40+) ---
// Entities spawned by JSM scripts after field_scripts_init aren't visible
// at init time.  We rescan on-demand when the user presses -/= to cycle,
// appending newly-discovered entities to the END of the catalog so that
// existing entries keep their positions and the user isn't confused.

// --- Navigation key state ---
// s_cycleIdx = index of the currently selected target in the distance-sorted
// catalog list. -/+ move through it; Backspace re-speaks the current selection.
static bool s_minusWasDown  = false;
static bool s_plusWasDown   = false;
static bool s_bkspWasDown   = false;
static bool s_driveWasDown  = false;
static int  s_cycleIdx      = 0;   // current target index (0 = nearest)

// Index into s_catalog[] of the currently selected entity.
// This is locked to field-load order, not distance order.
static int s_selectedCatalogIdx = 0;

// --- Auto-drive state ---
// When active, Update() injects arrow-key presses each tick to walk the player
// toward the selected entity.  s_driveHeld tracks which direction keys are
// currently being held so we can release them cleanly when stopping.
static bool    s_driveActive = false;
static uint8_t s_driveHeld   = 0;   // bitmask: DIR_UP/DOWN/LEFT/RIGHT

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
static const int DRIVE_STUCK_THRESH  = 80;  // v06.14: ticks before wiggle (~1.3s, was 40). Analog steering needs more settling time.
static const int DRIVE_WIGGLE_TICKS  = 18;  // v05.90: quick nudge (~0.3s, was 45)
static const int NUDGE_TICKS         = 8;   // v06.07: micro-nudge duration (~0.13s)
static const int MAX_RECOVERY_PHASES = 12;  // v06.08: auto-cancel after this many recovery phases without progress

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

// v05.50: Background entity diagnostic dump flag (reset on field load).
static bool      s_bgDiagDumped    = true;   // true = skip old BGDIAG dump

// v05.59: Coordinate diagnostic flag (fires once per field).
static bool      s_coordDiagDumped = false;

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

// v05.69: VISDIAG — dump entity visibility candidate bytes on F11.
static bool s_f11WasDown = false;

// v05.39: Track which entities have logged the struct-position fallback.
static uint16_t  s_structFallbackLogged = 0;

// ============================================================================
// v05.82: engine_eval_keyboard_gamepad_input post-call diagnostic hook
// ============================================================================
// Fires AFTER the original function processes keyboard/gamepad input.
// Dumps the gamepad_states entry and engine button state once per second
// while on a field, so we can see what values the keyboard path produces.
// This tells us whether analog_lx/ly are populated by keyboard input
// or whether we need a different injection point.

// v05.84: Hook dinput_update_gamepad_status to prevent it from calling
// IDirectInputDevice8::Poll()/GetDeviceState() on our fake device sentinel.
// When fake gamepad is installed, we skip the real poll and just return
// our fake DIJOYSTATE2 pointer (the game expects the return value to be
// the DIJOYSTATE2 pointer or null).
typedef void* (__cdecl *DinputUpdateGamepad_t)();
static DinputUpdateGamepad_t s_originalDinputUpdateGamepad = nullptr;

// v05.89: Hook get_key_state to zero arrow keys when auto-drive is active.
// This runs INSIDE engine_eval_keyboard_gamepad_input, after the keyboard buffer
// is filled but before ctrl_keyboard_actions reads direction from it.
// The arrow keys were injected via SendInput to trigger "player wants to move"
// abstract buttons, but we don't want them to determine the movement DIRECTION.
// The gamepad analog path (via FFNx's ff8_get_analog_value) provides the direction.
static int __cdecl HookedGetKeyState()
{
    // Call the original (or FFNx's replacement) to fill the keyboard buffer.
    int result = 0;
    if (s_originalGetKeyState)
        result = s_originalGetKeyState();

    // If auto-drive analog override is active, zero arrow key scancodes
    // so ctrl_keyboard_actions sees no keyboard direction.
    if (s_analogOverrideActive && FF8Addresses::HasKeyboardState()) {
        __try {
            uint8_t* kbBuf = *FF8Addresses::pKeyboardState;
            if (kbBuf) {
                static bool s_kbSuppressLogged = false;
                if (!s_kbSuppressLogged) {
                    s_kbSuppressLogged = true;
                    Log::Write("FieldNavigation: [v05.89] get_key_state hook: zeroing arrows "
                               "(buf=0x%08X, up=%02X dn=%02X lt=%02X rt=%02X)",
                               (uint32_t)(uintptr_t)kbBuf,
                               kbBuf[0x48], kbBuf[0x50], kbBuf[0x4B], kbBuf[0x4D]);
                }
                kbBuf[0x48] = 0;  // Up
                kbBuf[0x50] = 0;  // Down
                kbBuf[0x4B] = 0;  // Left
                kbBuf[0x4D] = 0;  // Right
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    return result;
}

static void* __cdecl HookedDinputUpdateGamepad()
{
    if (s_fakeGamepadInstalled) {
        // Don't poll the fake device — just return our fake state.
        // The original function returns LPDIJOYSTATE2 (or null on failure).
        return (void*)s_fakeDIJOYSTATE2;
    }
    // No fake installed — call the real poll function.
    if (s_originalDinputUpdateGamepad)
        return s_originalDinputUpdateGamepad();
    return nullptr;
}

static void __cdecl HookedEngineEvalInput()
{
    // v05.84: If analog override is active, write our desired lX/lY into the
    // fake DIJOYSTATE2 BEFORE calling the original. The original function will
    // see dinput_gamepad_device as non-null (our sentinel) and the hooked
    // dinput_update_gamepad_status will return our fake DIJOYSTATE2.
    if (s_analogOverrideActive && s_fakeGamepadInstalled) {
        // DIJOYSTATE2: lX at offset 0, lY at offset 4 (both LONG/int32_t)
        // Range: -1000 to +1000 (DirectInput axis range for FF8)
        *(int32_t*)(s_fakeDIJOYSTATE2 + 0) = s_analogDesiredLX;
        *(int32_t*)(s_fakeDIJOYSTATE2 + 4) = s_analogDesiredLY;
    }

    // v06.13: Approach C — snapshot player 2D position BEFORE engine_eval.
    // If the position changes after the call, the engine performed a 3D→2D
    // projection during this frame. We log the stack to find the function.
    int32_t projSnapX = 0, projSnapY = 0;
    bool projSnapValid = false;
    if (s_projDiagCount < PROJ_DIAG_MAX && s_playerEntityIdx >= 0 &&
        FF8Addresses::pFieldStateOthers && FF8Addresses::IsOnField()) {
        __try {
            uint8_t* pBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (pBase) {
                uint8_t* pBlk = pBase + ENTITY_STRIDE * s_playerEntityIdx;
                projSnapX = *(int32_t*)(pBlk + 0x190);
                projSnapY = *(int32_t*)(pBlk + 0x194);
                projSnapValid = true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Call the original — it will now process our fake gamepad data.
    if (s_originalEngineEvalInput)
        s_originalEngineEvalInput();

    // v06.13: Approach C — check if position changed during engine_eval.
    if (projSnapValid && s_projDiagCount < PROJ_DIAG_MAX) {
        __try {
            uint8_t* pBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (pBase) {
                uint8_t* pBlk = pBase + ENTITY_STRIDE * s_playerEntityIdx;
                int32_t afterX = *(int32_t*)(pBlk + 0x190);
                int32_t afterY = *(int32_t*)(pBlk + 0x194);
                // Only log if the position actually changed (player moved this frame).
                if (afterX != projSnapX || afterY != projSnapY) {
                    // Only log on first detection, then periodically.
                    if (s_projDiagCount == 0 || s_projDiagCount == 5) {
                        // Walk the stack to find return addresses.
                        // On x86, EBP chain gives us the call stack.
                        uint32_t stackAddrs[8] = {};
                        int stackDepth = 0;
                        __try {
                            uint32_t* ebp;
                            __asm { mov ebp, ebp }
                            // CaptureStackBackTrace is safer than manual EBP walking.
                            stackDepth = (int)CaptureStackBackTrace(0, 8, (PVOID*)stackAddrs, NULL);
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        Log::Write("FieldNavigation: [PROJDIAG] #%d position changed during engine_eval: "
                                   "before=(%d,%d)/4096=(%d,%d) after=(%d,%d)/4096=(%d,%d) "
                                   "delta=(%d,%d)",
                                   s_projDiagCount,
                                   projSnapX, projSnapY, projSnapX/4096, projSnapY/4096,
                                   afterX, afterY, afterX/4096, afterY/4096,
                                   afterX - projSnapX, afterY - projSnapY);
                        if (stackDepth > 0) {
                            char stackBuf[256] = {};
                            int pos = 0;
                            for (int s = 0; s < stackDepth && pos < 240; s++) {
                                pos += snprintf(stackBuf + pos, 256 - pos, "0x%08X ", stackAddrs[s]);
                            }
                            Log::Write("FieldNavigation: [PROJDIAG]   stack(%d): %s", stackDepth, stackBuf);
                        }
                        // Also log the engine_eval address for reference.
                        Log::Write("FieldNavigation: [PROJDIAG]   engine_eval=0x%08X set_tri=0x%08X",
                                   FF8Addresses::engine_eval_keyboard_gamepad_input_addr,
                                   FF8Addresses::set_current_triangle_addr);
                    }
                    s_projDiagCount++;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // v05.88 Approach D post-call suppression removed in v05.89.
    // Arrow key suppression now happens inside HookedGetKeyState, which runs
    // between get_key_state (buffer fill) and ctrl_keyboard_actions (direction read).

    // Diagnostic: dump state once per second while on field (first 10 field-mode dumps only).
    if (!s_gpDiagEnabled) return;
    if (!FF8Addresses::IsOnField()) return;

    DWORD now = GetTickCount();
    if ((now - s_gpDiagLastDump) < 1000) return;
    s_gpDiagLastDump = now;

    static int s_gpDiagCount = 0;
    if (s_gpDiagCount >= 10) {
        s_gpDiagEnabled = false;
        Log::Write("FieldNavigation: [GPDIAG2] 10 field dumps complete, diagnostic disabled.");
        return;
    }
    s_gpDiagCount++;

    __try {
        uint8_t* gp = FF8Addresses::pGamepadStates;
        if (!gp) return;
        uint8_t entries_offset = gp[0x18];
        uint8_t gamepad_options = gp[0xC3];

        Log::Write("FieldNavigation: [GPDIAG2] #%d entries_offset=%u gamepad_options=0x%02X override=%s fakeGP=%s lX=%d lY=%d",
                   s_gpDiagCount, (unsigned)entries_offset, (unsigned)gamepad_options,
                   s_analogOverrideActive ? "ON" : "off",
                   s_fakeGamepadInstalled ? "YES" : "no",
                   (int)s_analogDesiredLX, (int)s_analogDesiredLY);

        // Dump the active entry.
        if (entries_offset < 8) {
            uint8_t* entry = gp + 0x1C + entries_offset * 20;
            Log::Write("FieldNavigation: [GPDIAG2]   entry[%u] adis=%u aflg=0x%02X kscan=0x%04X "
                       "rx=%u ry=%u lx=%u ly=%u kon=0x%04X kinv=0x%04X",
                       (unsigned)entries_offset,
                       entry[0], entry[1], *(uint16_t*)(entry + 2),
                       entry[4], entry[5], entry[6], entry[7],
                       *(uint16_t*)(entry + 16), *(uint16_t*)(entry + 18));
        }

        if (FF8Addresses::pEngineInputValidButtons && FF8Addresses::pEngineInputConfirmedButtons) {
            Log::Write("FieldNavigation: [GPDIAG2]   validButtons=0x%08X confirmedButtons=0x%08X",
                       *FF8Addresses::pEngineInputValidButtons,
                       *FF8Addresses::pEngineInputConfirmedButtons);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: [GPDIAG2] Exception reading gamepad state");
        s_gpDiagEnabled = false;
    }
}

// ============================================================================
// Helpers
// ============================================================================

// Look up the current world-space centre for an entity.
// v05.44: DIAGNOSTIC CONFIRMED that entity world positions are stored as:
//   0x190: int32 X * 4096  (fixed-point, 12-bit fractional)
//   0x194: int32 Y * 4096  (vertical axis)
//   0x198: int32 Z * 4096
// These are ALWAYS populated for the player entity, even when the simpler
// int32 values at 0x20 (X) / 0x24 (Y) / 0x28 (Z) read as zero.
// For NPCs, 0x20/0x28 are populated at init time. We try the fixed-point
// coords first (precise and always live), fall back to the simple int32.
//
// For navigation, X = screen-right, Y = screen-up (confirmed v05.60 COORDDIAG).
// We return (X, Y) and ignore Z (depth, always ~0 on flat floors).
// v05.61: FIXED — was reading Z (0x198) which is always ~0. Now reads Y (0x194).
static bool GetEntityPos(int entityIdx, float& cx, float& cy)
{
    if (entityIdx < 0 || entityIdx >= MAX_ENTITIES) return false;
    if (!FF8Addresses::pFieldStateOthers) return false;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base) {
            uint8_t* block = base + ENTITY_STRIDE * entityIdx;
            // Check if entity is placed on walkmesh (triId > 0).
            uint16_t triId = *(uint16_t*)(block + 0x1FA);
            if (triId == 0) return false;  // not yet placed
            // Strategy 1: Fixed-point coords at 0x190 (X) and 0x194 (Y).
            // Always live for the player; also works for some NPCs.
            // v05.61: Changed from 0x198 (Z, always ~0) to 0x194 (Y, screen-vertical).
            int32_t fpX = *(int32_t*)(block + 0x190);
            int32_t fpY = *(int32_t*)(block + 0x194);
            if (fpX != 0 || fpY != 0) {
                cx = (float)(fpX / 4096);
                cy = (float)(fpY / 4096);
                return true;
            }
            // Strategy 2: Simple int16 at 0x20 (X) and 0x24 (Y).
            // Works for NPCs placed by JSM SET opcodes.
            // v05.61: Changed from 0x28 (Z) to 0x24 (Y).
            int16_t simX = *(int16_t*)(block + 0x20);
            int16_t simY = *(int16_t*)(block + 0x24);
            if (simX != 0 || simY != 0) {
                cx = (float)simX;
                cy = (float)simY;
                return true;
            }
            // Both zero but entity is on walkmesh — position is literally (0,0).
            cx = 0.0f;
            cy = 0.0f;
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// v05.80: Read entity talk radius (offset 0x1F8) and push radius (offset 0x1F6).
// Returns the raw uint16 value, or 0 if the entity can't be read.
// These are in the same world coordinate units as entity positions.
static uint16_t GetEntityTalkRadius(int entityIdx)
{
    if (entityIdx < 0 || entityIdx >= MAX_ENTITIES) return 0;
    if (!FF8Addresses::pFieldStateOthers) return 0;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base) return *(uint16_t*)(base + ENTITY_STRIDE * entityIdx + 0x1F8);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// v06.21: Write a new talk radius to the entity struct.
static bool SetEntityTalkRadius(int entityIdx, uint16_t newRadius)
{
    if (entityIdx < 0 || entityIdx >= MAX_ENTITIES) return false;
    if (!FF8Addresses::pFieldStateOthers) return false;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base) {
            *(uint16_t*)(base + ENTITY_STRIDE * entityIdx + 0x1F8) = newRadius;
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static uint16_t GetEntityPushRadius(int entityIdx)
{
    if (entityIdx < 0 || entityIdx >= MAX_ENTITIES) return 0;
    if (!FF8Addresses::pFieldStateOthers) return 0;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base) return *(uint16_t*)(base + ENTITY_STRIDE * entityIdx + 0x1F6);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// v05.80: Check if a walkmesh triangle center is blocked by any NPC's push radius.
// Used by A* to route around NPC collision bodies. Skips the player and the
// target entity (we want to path TO the target, not avoid them).
static bool IsTriangleBlockedByNPC(float triCenterX, float triCenterY, int targetEntityIdx)
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return false;
    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return false;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;
        for (int i = 0; i < (int)lim; i++) {
            if (i == s_playerEntityIdx) continue;  // don't block on self
            if (i == targetEntityIdx) continue;     // don't block on our target
            uint8_t* block = base + ENTITY_STRIDE * i;
            int16_t modelId = *(int16_t*)(block + 0x218);
            if (modelId < 0) continue;  // invisible controller, no collision
            uint16_t pushRad = *(uint16_t*)(block + 0x1F6);
            if (pushRad == 0) continue;  // no collision radius set
            // Get entity position
            float ex, ey;
            if (!GetEntityPos(i, ex, ey)) continue;
            // Check if triangle center is within push radius
            float dx = triCenterX - ex;
            float dy = triCenterY - ey;
            float distSq = dx*dx + dy*dy;
            float radF = (float)pushRad;
            if (distSq < radF * radF) return true;  // blocked!
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// Safe read of vertex struct at ptr as (x, y, z) int16_t.
// Returns false if ptr is outside plausible data range.
static bool ReadVertexCoords(uintptr_t ptr, int16_t& x, int16_t& y, int16_t& z)
{
    if (ptr < 0x00010000 || ptr > 0x7FFFFFFF) return false;
    __try {
        const int16_t* v = reinterpret_cast<const int16_t*>(ptr);
        x = v[0]; y = v[1]; z = v[2];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Format navigation as component-based directions: "Right 5, Up 3"
// +X = right (screen-right), -Y = up (screen-up). Corrected v05.61.
// Each axis is converted to steps independently at 250 world units/step.
// If both axes are tiny, says "right here".
// v05.61: Parameter renamed from dz to dy — now represents the Y axis.
static void FormatNavComponents(float dx, float dy, char* buf, int bufsz)
{
    int hSteps = (int)(fabsf(dx) / 250.0f + 0.5f);
    int vSteps = (int)(fabsf(dy) / 250.0f + 0.5f);
    const char* hDir = (dx >= 0.0f) ? "right" : "left";
    // v05.61: Y axis — negative Y = screen-up (confirmed: entities above
    // player have more negative Y in bgroom_1 COORDDIAG).
    const char* vDir = (dy >= 0.0f) ? "down" : "up";

    if (hSteps == 0 && vSteps == 0) {
        snprintf(buf, bufsz, "right here");
    } else if (hSteps == 0) {
        snprintf(buf, bufsz, "%s %d", vDir, vSteps);
    } else if (vSteps == 0) {
        snprintf(buf, bufsz, "%s %d", hDir, hSteps);
    } else {
        snprintf(buf, bufsz, "%s %d, %s %d", hDir, hSteps, vDir, vSteps);
    }
}

// Sanity threshold: if the computed distance exceeds this, the center data
// is likely stale/wrong and we report the entity as "not yet located".
static const float MAX_SANE_DIST = 30000.0f;

// ============================================================================
// A* Walkmesh Pathfinding (v05.62)
// ============================================================================
//
// Finds the shortest path through the walkmesh triangle graph from the
// player's current triangle to the triangle nearest the target position.
// Returns a sequence of triangle-center waypoints that UpdateAutoDrive
// follows instead of heading straight-line.
//
// The graph nodes are walkmesh triangles. Edges connect triangles that
// share an edge (neighbor[0..2]). Edge cost = Euclidean distance between
// triangle centers. Heuristic = straight-line distance to goal.

// Find the walkmesh triangle whose center is closest to (x, y).
// Returns triangle index, or -1 if walkmesh not loaded.
static int FindNearestTriangle(float x, float y)
{
    if (!s_walkmesh.valid || s_walkmesh.numTriangles == 0) return -1;
    int best = -1;
    float bestDist = 1e30f;
    for (int t = 0; t < s_walkmesh.numTriangles; t++) {
        float dx = s_walkmesh.triangles[t].centerX - x;
        float dy = s_walkmesh.triangles[t].centerY - y;
        float d = dx*dx + dy*dy;
        if (d < bestDist) { bestDist = d; best = t; }
    }
    return best;
}

// A* open set node.
struct AStarNode {
    uint16_t triIdx;
    float    gCost;    // cost from start
    float    fCost;    // gCost + heuristic
    int16_t  cameFrom; // parent triangle index (-1 = start)
};

// v05.81: Compute the length of the shared edge between triangle triIdx and
// its neighbor on edge edgeIdx (0-2). The shared edge connects the two
// vertices that are NOT vertexIdx[edgeIdx]. Returns 0 if data is invalid.
static float GetSharedEdgeLength(int triIdx, int edgeIdx)
{
    if (!s_walkmesh.valid || triIdx < 0 || triIdx >= s_walkmesh.numTriangles) return 0;
    // Shared edge connects vertex[(edge+1)%3] and vertex[(edge+2)%3]
    int vi1 = s_walkmesh.triangles[triIdx].vertexIdx[(edgeIdx + 1) % 3];
    int vi2 = s_walkmesh.triangles[triIdx].vertexIdx[(edgeIdx + 2) % 3];
    if (vi1 >= s_walkmesh.numVertices || vi2 >= s_walkmesh.numVertices) return 0;
    float dx = (float)(s_walkmesh.vertices[vi1].x - s_walkmesh.vertices[vi2].x);
    float dy = (float)(s_walkmesh.vertices[vi1].y - s_walkmesh.vertices[vi2].y);
    return sqrtf(dx*dx + dy*dy);
}

// v05.81: Compute how well a movement direction aligns with the 8 arrow-key
// directions (N, NE, E, SE, S, SW, W, NW). Returns a penalty multiplier:
//   1.0 = perfectly aligned with one of the 8 directions
//   up to ~2.0 = worst case (22.5 degrees off from nearest direction)
// The 8 directions are spaced 45 degrees apart, so maximum misalignment is 22.5 degrees.
static float GetAngleAlignmentPenalty(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f) return 1.0f;
    float angle = atan2f(dy, dx);  // radians, -PI to PI
    // Snap to nearest 45-degree increment
    float sector = angle / (float)(NAV_PI / 4.0);  // -4 to 4
    float nearest = roundf(sector);
    float diff = fabsf(sector - nearest);  // 0 to 0.5 (0 = aligned, 0.5 = 22.5 deg off)
    // Scale: 0 deviation = 1.0x cost, 0.5 deviation = 2.0x cost
    return 1.0f + diff * 2.0f;
}

// v05.81: Minimum shared edge width for A* to consider a passage navigable.
// Edges shorter than this are treated as too narrow for 8-directional steering.
// Typical character width is roughly 30-50 world units.
// v05.93: Reverted to v05.89 values that successfully navigated bg2f_1.
// The v05.92 increase to 80/200 was too aggressive and blocked walkable aisles.
static const float MIN_EDGE_WIDTH = 40.0f;
static const float NARROW_EDGE_THRESHOLD = 100.0f;
static const float NARROW_EDGE_PENALTY = 3.0f;

// v05.92: Forward declarations for trigger-line avoidance in A*.
// These are defined later in the file but needed by ComputeAStarPath.
// v06.02: skipTriggerIdx allows exempting one trigger line (used when
// driving TO a screen transition — we need to cross that specific line).
static bool IsSeparatedByTriggerLine(float px, float py, float ex, float ey, int skipTriggerIdx = -1);
// v06.05: Check if moving from (px,py) in direction (dx,dy) by RECOVERY_CHECK_DIST
// would cross any non-target active trigger line. Used to prevent recovery
// wiggle from accidentally pushing the player through screen transitions.
static const float RECOVERY_CHECK_DIST = 400.0f;  // how far ahead to check
static bool WouldCrossTriggerLine(float px, float py, float dx, float dy, int skipTriggerIdx);

// v05.92: Trigger line data moved here (was in SETLINE section) so A* can access it.
struct CapturedTriggerLine {
    uint32_t entityAddr;
    int      lineOrder;
    int16_t  x1, y1, z1;
    int16_t  x2, y2, z2;
    bool     active;
    char     name[48];
};
static const int MAX_CAPTURED_LINES = 32;
static CapturedTriggerLine s_capturedLines[MAX_CAPTURED_LINES] = {};
static int s_capturedLineCount = 0;
static int s_setlineCallCount = 0;

// v05.93: Walkmesh line-of-sight check.
// Walks through walkmesh triangles from startTri toward (goalX, goalY).
// At each triangle, picks the neighbor whose shared edge is closest to the
// goal direction. If we reach the goal triangle (or a triangle within
// arriveDist of the goal), returns true — meaning A* can be skipped and
// the player can steer directly to the target.
// Also checks that the path doesn't cross any active trigger lines.
static bool HasLineOfSight(int startTri, float goalX, float goalY, float arriveDist)
{
    if (!s_walkmesh.valid || startTri < 0) return false;
    int numTri = s_walkmesh.numTriangles;
    if (startTri >= numTri) return false;

    // Walk up to 200 triangles (safety limit for large walkmeshes).
    int curTri = startTri;
    bool visited[4096] = {};
    if (numTri > 4096) return false;

    float startCX = s_walkmesh.triangles[startTri].centerX;
    float startCY = s_walkmesh.triangles[startTri].centerY;

    for (int step = 0; step < 200; step++) {
        if (curTri < 0 || curTri >= numTri) return false;
        visited[curTri] = true;

        float cx = s_walkmesh.triangles[curTri].centerX;
        float cy = s_walkmesh.triangles[curTri].centerY;

        // Check if we've reached the goal.
        float dx = goalX - cx;
        float dy = goalY - cy;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < arriveDist + 100.0f) return true;  // close enough

        // Check if this triangle is on the other side of a trigger line from start.
        if (s_capturedLineCount > 0) {
            if (IsSeparatedByTriggerLine(startCX, startCY, cx, cy)) return false;
        }

        // Find the neighbor that makes the most progress toward the goal.
        // Use dot product of (edge midpoint - current center) with (goal direction).
        int bestNeighbor = -1;
        float bestProgress = -1e30f;
        float dirLen = sqrtf(dx*dx + dy*dy);
        float ndx = (dirLen > 0.001f) ? dx / dirLen : 0;
        float ndy = (dirLen > 0.001f) ? dy / dirLen : 0;

        for (int e = 0; e < 3; e++) {
            uint16_t nb = s_walkmesh.triangles[curTri].neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
            if (visited[nb]) continue;

            // Check edge width — skip too-narrow passages.
            float edgeWidth = GetSharedEdgeLength(curTri, e);
            if (edgeWidth > 0 && edgeWidth < MIN_EDGE_WIDTH) continue;

            float nbCX = s_walkmesh.triangles[nb].centerX;
            float nbCY = s_walkmesh.triangles[nb].centerY;
            // Progress = how much closer this neighbor gets us to the goal.
            float progress = (nbCX - cx) * ndx + (nbCY - cy) * ndy;
            if (progress > bestProgress) {
                bestProgress = progress;
                bestNeighbor = (int)nb;
            }
        }

        if (bestNeighbor < 0) return false;  // dead end
        curTri = bestNeighbor;
    }
    return false;  // exceeded step limit
}

// v06.01: Check if two triangles are on the same walkmesh island.
// Uses BFS from startTri. Returns true if goalTri is reachable.
// This is fast (<1ms for 512 triangles) and detects disconnected islands
// that make A* pathfinding impossible (47.5% of FF8 fields have these).
static bool AreTrianglesConnected(int startTri, int goalTri)
{
    if (!s_walkmesh.valid) return false;
    if (startTri < 0 || goalTri < 0) return false;
    if (startTri == goalTri) return true;
    int numTri = s_walkmesh.numTriangles;
    if (startTri >= numTri || goalTri >= numTri) return false;

    // BFS from startTri.
    static bool visited[4096];
    if (numTri > 4096) return false;
    memset(visited, 0, sizeof(bool) * numTri);

    static uint16_t queue[4096];
    int qHead = 0, qTail = 0;
    queue[qTail++] = (uint16_t)startTri;
    visited[startTri] = true;

    while (qHead < qTail) {
        uint16_t cur = queue[qHead++];
        if (cur == (uint16_t)goalTri) return true;
        for (int e = 0; e < 3; e++) {
            uint16_t nb = s_walkmesh.triangles[cur].neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
            if (visited[nb]) continue;
            visited[nb] = true;
            queue[qTail++] = nb;
        }
    }
    return false;
}

// Run A* from startTri to goalTri. Writes waypoint centers into
// s_waypoints[] and sets s_waypointCount. Returns true if path found.
// v05.80: targetEntityIdx is used for push-radius blackout — triangles
// within any NPC's push radius (except the target) are treated as blocked.
// v06.02: skipTriggerIdx exempts one trigger line from A* avoidance
// (used when driving to a screen transition exit).
static bool ComputeAStarPath(int startTri, int goalTri, int targetEntityIdx = -1, int skipTriggerIdx = -1)
{
    s_waypointCount = 0;
    s_waypointIdx   = 0;
    s_usingFunnel   = false;  // v05.95: reset until FunnelPath sets it
    if (!s_walkmesh.valid) return false;
    if (startTri < 0 || goalTri < 0) return false;
    if (startTri == goalTri) {
        // Already on the goal triangle.
        s_waypoints[0][0] = s_walkmesh.triangles[goalTri].centerX;
        s_waypoints[0][1] = s_walkmesh.triangles[goalTri].centerY;
        s_waypointCount = 1;
        return true;
    }

    int numTri = s_walkmesh.numTriangles;
    float goalX = s_walkmesh.triangles[goalTri].centerX;
    float goalY = s_walkmesh.triangles[goalTri].centerY;

    // Per-triangle best known gCost and parent.
    // Using static arrays to avoid heap allocation in game thread.
    static float bestG[4096];
    static int16_t parent[4096];
    static bool closed[4096];
    if (numTri > 4096) return false;  // safety

    for (int i = 0; i < numTri; i++) {
        bestG[i] = 1e30f;
        parent[i] = -1;
        closed[i] = false;
    }

    // Simple open list (not a heap — walkmesh is small enough).
    // Max 4096 triangles, so linear scan is fine.
    static uint16_t openList[4096];
    int openCount = 0;

    bestG[startTri] = 0.0f;
    openList[openCount++] = (uint16_t)startTri;

    bool found = false;
    int iterations = 0;
    static const int MAX_ITERATIONS = 20000;  // safety limit

    while (openCount > 0 && iterations < MAX_ITERATIONS) {
        iterations++;

        // Find node in open list with lowest fCost.
        int bestIdx = 0;
        float bestF = 1e30f;
        for (int i = 0; i < openCount; i++) {
            uint16_t ti = openList[i];
            float dx = s_walkmesh.triangles[ti].centerX - goalX;
            float dy = s_walkmesh.triangles[ti].centerY - goalY;
            float h = sqrtf(dx*dx + dy*dy);
            float f = bestG[ti] + h;
            if (f < bestF) { bestF = f; bestIdx = i; }
        }

        uint16_t current = openList[bestIdx];
        // Remove from open list (swap with last).
        openList[bestIdx] = openList[--openCount];
        closed[current] = true;

        if (current == (uint16_t)goalTri) { found = true; break; }

        // Expand neighbors.
        float curX = s_walkmesh.triangles[current].centerX;
        float curY = s_walkmesh.triangles[current].centerY;

        for (int e = 0; e < 3; e++) {
            uint16_t nb = s_walkmesh.triangles[current].neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)numTri) continue;
            if (closed[nb]) continue;

            float nbX = s_walkmesh.triangles[nb].centerX;
            float nbY = s_walkmesh.triangles[nb].centerY;

            // v05.80: Skip triangles blocked by NPC push radius.
            // Don't block the goal triangle itself (we need to reach it).
            if (targetEntityIdx >= 0 && nb != (uint16_t)goalTri) {
                if (IsTriangleBlockedByNPC(nbX, nbY, targetEntityIdx)) continue;
            }

            // v05.91: Skip triangles on the other side of active trigger lines.
            // This prevents the A* path from routing through screen transition
            // zones, which would cause the player to accidentally leave the field.
            // We check if the neighbor triangle's center is separated from the
            // start triangle's center by any active trigger line.
            if (s_capturedLineCount > 0 && nb != (uint16_t)goalTri) {
                float startCX = s_walkmesh.triangles[startTri].centerX;
                float startCY = s_walkmesh.triangles[startTri].centerY;
                if (IsSeparatedByTriggerLine(startCX, startCY, nbX, nbY, skipTriggerIdx)) continue;
            }

            // v05.81: Check shared edge width. Block edges too narrow to navigate.
            float edgeWidth = GetSharedEdgeLength((int)current, e);
            if (edgeWidth > 0 && edgeWidth < MIN_EDGE_WIDTH && nb != (uint16_t)goalTri) {
                continue;  // too narrow, skip entirely
            }

            float edgeCost = sqrtf((nbX - curX)*(nbX - curX) + (nbY - curY)*(nbY - curY));

            // v05.81: Penalize narrow edges — prefer wider corridors.
            // v05.92: Penalize narrow edges to strongly prefer wide aisles.
            // Even with analog steering, the character's collision body can't
            // fit through desk gaps. Use full penalty to route around.
            if (edgeWidth > 0 && edgeWidth < NARROW_EDGE_THRESHOLD) {
                edgeCost *= NARROW_EDGE_PENALTY;
            }

            // v05.81 angle alignment penalty REMOVED in v05.91.
            // With analog steering (v05.89), the player can move in any direction,
            // so penalizing non-cardinal movement creates unnecessarily roundabout paths.
            float tentG = bestG[current] + edgeCost;

            if (tentG < bestG[nb]) {
                bestG[nb] = tentG;
                parent[nb] = (int16_t)current;
                // Add to open list if not already there.
                bool inOpen = false;
                for (int i = 0; i < openCount; i++) {
                    if (openList[i] == nb) { inOpen = true; break; }
                }
                if (!inOpen && openCount < 4096) {
                    openList[openCount++] = nb;
                }
            }
        }
    }

    if (!found) {
        Log::Write("FieldNavigation: [A*] No path from tri %d to tri %d (%d iterations)",
                   startTri, goalTri, iterations);
        return false;
    }

    // Reconstruct path: trace parent[] from goal back to start.
    static uint16_t pathReverse[4096];
    int pathLen = 0;
    uint16_t cur = (uint16_t)goalTri;
    while (cur != (uint16_t)startTri && pathLen < 4096) {
        pathReverse[pathLen++] = cur;
        int16_t p = parent[cur];
        if (p < 0) break;
        cur = (uint16_t)p;
    }
    // v05.90: Store triangle corridor (start..goal order) for funnel algorithm.
    // Also store triangle-center waypoints as fallback.
    s_corridorCount = 0;
    s_waypointCount = 0;
    // Add start triangle first (not in pathReverse).
    if (s_corridorCount < MAX_CORRIDOR) s_corridor[s_corridorCount++] = (uint16_t)startTri;
    for (int i = pathLen - 1; i >= 0; i--) {
        if (s_corridorCount < MAX_CORRIDOR) s_corridor[s_corridorCount++] = pathReverse[i];
        if (s_waypointCount < MAX_WAYPOINTS) {
            s_waypoints[s_waypointCount][0] = s_walkmesh.triangles[pathReverse[i]].centerX;
            s_waypoints[s_waypointCount][1] = s_walkmesh.triangles[pathReverse[i]].centerY;
            s_waypointCount++;
        }
    }

    Log::Write("FieldNavigation: [A*] Path found: %d triangles, %d waypoints, %d iterations, start=%d goal=%d",
               s_corridorCount, s_waypointCount, iterations, startTri, goalTri);
    return (s_waypointCount > 0);
}

// v05.90: Simple Stupid Funnel Algorithm (SSFA) for path smoothing.
// Takes the triangle corridor from A* and produces the shortest path through
// it by "string-pulling" — finding the tightest rope through the portal edges.
// This replaces the old cumulative-angle SimplifyPath (v05.66) which just
// removed collinear waypoints from triangle-center paths.
//
// The algorithm walks through the portals (shared edges between consecutive
// corridor triangles) maintaining a funnel defined by left and right boundaries.
// When the funnel crosses itself, a turn point is emitted.
//
// Reference: "Simple Stupid Funnel Algorithm" by Mikko Mononen.

// Helper: 2D cross product of (OA x OB).
static float Cross2D(float ox, float oy, float ax, float ay, float bx, float by)
{
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

// Find the shared edge (portal) between two adjacent corridor triangles.
// Returns the two vertex positions of the shared edge.
// The left/right ordering is from the perspective of walking from triA to triB.
// v05.94: Fixed left/right determination. Use direction of travel (triA center
// → triB center) instead of opposite vertex. The cross product of the travel
// direction with the edge vector determines which side is left and right from
// the traveler's perspective.
static bool FindPortal(uint16_t triA, uint16_t triB,
                       float& leftX, float& leftY, float& rightX, float& rightY)
{
    if (!s_walkmesh.valid) return false;
    const auto& tA = s_walkmesh.triangles[triA];
    const auto& tB = s_walkmesh.triangles[triB];
    // Find which edge of triA connects to triB.
    int edgeIdx = -1;
    for (int e = 0; e < 3; e++) {
        if (tA.neighbor[e] == triB) { edgeIdx = e; break; }
    }
    if (edgeIdx < 0) return false;
    // Shared edge connects vertex[(edge+1)%3] and vertex[(edge+2)%3].
    int vi1 = tA.vertexIdx[(edgeIdx + 1) % 3];
    int vi2 = tA.vertexIdx[(edgeIdx + 2) % 3];
    if (vi1 >= s_walkmesh.numVertices || vi2 >= s_walkmesh.numVertices) return false;
    float x1 = (float)s_walkmesh.vertices[vi1].x;
    float y1 = (float)s_walkmesh.vertices[vi1].y;
    float x2 = (float)s_walkmesh.vertices[vi2].x;
    float y2 = (float)s_walkmesh.vertices[vi2].y;
    // v05.96: Determine left/right using the center of triB (the "far" triangle).
    // We need left/right as seen by a traveler standing in triA looking through
    // the portal toward triB. The center of triB is always unambiguously on one
    // side of the shared edge — unlike the travel direction, which can be nearly
    // parallel to the edge in long corridors, causing near-zero cross products.
    //
    // Cross product of (v1→v2) × (v1→triB_center) tells us which side triB is on.
    // The SSFA convention: looking from apex through the funnel, LEFT is the
    // side that triB is NOT on (the wall side of triA), and RIGHT is the other.
    // Actually the standard: LEFT boundary = left side of corridor from traveler.
    // triB center is on the "forward" side. We pick left/right so that the
    // corridor interior (triB side) is between them.
    //
    // If (v1→v2) × (v1→Bcenter) > 0, Bcenter is LEFT of v1→v2.
    //   So v1 is the RIGHT boundary, v2 is the LEFT boundary.
    // If < 0, Bcenter is RIGHT of v1→v2.
    //   So v1 is the LEFT boundary, v2 is the RIGHT boundary.
    float toBX = tB.centerX - x1;
    float toBY = tB.centerY - y1;
    float edgeDX = x2 - x1;
    float edgeDY = y2 - y1;
    float cross = edgeDX * toBY - edgeDY * toBX;
    if (cross > 0) {
        // triB center is to the LEFT of v1→v2.
        // v2 = left boundary, v1 = right boundary.
        leftX = x2; leftY = y2;
        rightX = x1; rightY = y1;
    } else {
        // triB center is to the RIGHT of v1→v2.
        // v1 = left boundary, v2 = right boundary.
        leftX = x1; leftY = y1;
        rightX = x2; rightY = y2;
    }
    return true;
}

static void FunnelPath(float startX, float startY, float goalX, float goalY)
{
    // Build portal list from corridor.
    // Each portal is the shared edge between corridor[i] and corridor[i+1].
    struct Portal { float lx, ly, rx, ry; };
    static Portal portals[MAX_CORRIDOR];
    int numPortals = 0;

    // v06.01: New portal pipeline — wall-parallel skip + agent-radius shrinking.
    // Validated offline against all 894 game walkmeshes.
    //
    // Step 1: Skip wall-parallel portals. A portal is wall-parallel when both
    // endpoints lie on the same wall line — one axis has near-zero span AND the
    // other has significant span. These portals run ALONG a wall, not across
    // the walkable corridor. The funnel can't form proper L/R bounds from them.
    //   - bg2f_1 corridor: portals at X=165 and X=325 (dX=0, dY=300+) are wall-parallel
    //   - bgroom_1 classroom: diagonal edges with dX=1-2 are NOT wall-parallel
    //   - Epsilon=1.0 with 10x ratio test cleanly separates these cases
    //
    // Step 2: Shrink surviving portals inward by AGENT_RADIUS on each end.
    // This keeps the SSFA path away from walls by the character's collision
    // radius. Turn points land agent_radius units from the wall instead of
    // exactly on wall corners. Replaces the old wall-margin post-processing.
    static const float WALL_PARALLEL_EPSILON = 1.0f;
    static const float AGENT_RADIUS = 30.0f;  // character collision half-width
    int degenerateSkipped = 0;

    for (int i = 0; i + 1 < s_corridorCount && numPortals < MAX_CORRIDOR; i++) {
        float lx, ly, rx, ry;
        if (FindPortal(s_corridor[i], s_corridor[i+1], lx, ly, rx, ry)) {
            // Wall-parallel check: one axis < epsilon AND other > 10*epsilon.
            float absDX = fabsf(lx - rx);
            float absDY = fabsf(ly - ry);
            // v06.02: Only check vertical wall-parallel (dX near zero).
            // Horizontal portals (dY near zero, dX large) span ACROSS the
            // corridor and are always valid. The old dY<epsilon check caused
            // false positives on bg2f_1 (dX=209 dY=0 portal at Y=-2342).
            bool wallParallel = (absDX < WALL_PARALLEL_EPSILON && absDY > WALL_PARALLEL_EPSILON * 10.0f);
            if (wallParallel) {
                degenerateSkipped++;
                Log::Write("FieldNavigation: [funnel] SKIP wall-parallel portal %d "
                           "dX=%.1f dY=%.1f L=(%.0f,%.0f) R=(%.0f,%.0f) tri %d->%d",
                           i, absDX, absDY, lx, ly, rx, ry,
                           (int)s_corridor[i], (int)s_corridor[i+1]);
                continue;
            }
            // Shrink portal inward by AGENT_RADIUS on each end.
            // Direction from left to right endpoint.
            float edgeLen = sqrtf(absDX*absDX + absDY*absDY);
            if (edgeLen <= AGENT_RADIUS * 2.0f) {
                // Portal too narrow — collapse to midpoint.
                float mx = (lx + rx) / 2.0f;
                float my = (ly + ry) / 2.0f;
                portals[numPortals].lx = mx;
                portals[numPortals].ly = my;
                portals[numPortals].rx = mx;
                portals[numPortals].ry = my;
            } else {
                float nx = (rx - lx) / edgeLen;
                float ny = (ry - ly) / edgeLen;
                portals[numPortals].lx = lx + nx * AGENT_RADIUS;
                portals[numPortals].ly = ly + ny * AGENT_RADIUS;
                portals[numPortals].rx = rx - nx * AGENT_RADIUS;
                portals[numPortals].ry = ry - ny * AGENT_RADIUS;
            }
            numPortals++;
        }
    }
    if (degenerateSkipped > 0) {
        Log::Write("FieldNavigation: [funnel] Skipped %d wall-parallel portals",
                   degenerateSkipped);
    }
    // Add a degenerate portal at the goal (both sides = goal point).
    if (numPortals < MAX_CORRIDOR) {
        portals[numPortals].lx = goalX;
        portals[numPortals].ly = goalY;
        portals[numPortals].rx = goalX;
        portals[numPortals].ry = goalY;
        numPortals++;
    }

    if (numPortals == 0) return;  // no portals, keep triangle-center path

    // v05.94: Diagnostic — log first 20 portals to verify left/right ordering.
    for (int d = 0; d < numPortals && d < 20; d++) {
        Log::Write("FieldNavigation: [funnel] portal %d/%d L=(%.0f,%.0f) R=(%.0f,%.0f) tri %d->%d",
                   d, numPortals,
                   portals[d].lx, portals[d].ly,
                   portals[d].rx, portals[d].ry,
                   (d < s_corridorCount) ? (int)s_corridor[d] : -1,
                   (d+1 < s_corridorCount) ? (int)s_corridor[d+1] : -1);
    }

    // SSFA: walk the portals maintaining a funnel.
    float apexX = startX, apexY = startY;
    float funnelLX = startX, funnelLY = startY;
    float funnelRX = startX, funnelRY = startY;
    int apexIdx = 0, leftIdx = 0, rightIdx = 0;

    float result[MAX_WAYPOINTS][2];
    int resultCount = 0;

    for (int i = 0; i < numPortals; i++) {
        float pLX = portals[i].lx, pLY = portals[i].ly;
        float pRX = portals[i].rx, pRY = portals[i].ry;

        // Update right vertex.
        if (Cross2D(apexX, apexY, funnelRX, funnelRY, pRX, pRY) <= 0.0f) {
            if ((apexX == funnelRX && apexY == funnelRY) ||
                Cross2D(apexX, apexY, funnelLX, funnelLY, pRX, pRY) > 0.0f) {
                // Tighten the funnel.
                funnelRX = pRX; funnelRY = pRY;
                rightIdx = i;
            } else {
                // Right crosses left — left becomes new apex.
                if (resultCount < MAX_WAYPOINTS) {
                    result[resultCount][0] = funnelLX;
                    result[resultCount][1] = funnelLY;
                    resultCount++;
                }
                apexX = funnelLX; apexY = funnelLY;
                apexIdx = leftIdx;
                funnelRX = apexX; funnelRY = apexY;
                rightIdx = apexIdx;
                // Restart scan from apex portal.
                i = apexIdx;
                continue;
            }
        }

        // Update left vertex.
        if (Cross2D(apexX, apexY, funnelLX, funnelLY, pLX, pLY) >= 0.0f) {
            if ((apexX == funnelLX && apexY == funnelLY) ||
                Cross2D(apexX, apexY, funnelRX, funnelRY, pLX, pLY) < 0.0f) {
                // Tighten the funnel.
                funnelLX = pLX; funnelLY = pLY;
                leftIdx = i;
            } else {
                // Left crosses right — right becomes new apex.
                if (resultCount < MAX_WAYPOINTS) {
                    result[resultCount][0] = funnelRX;
                    result[resultCount][1] = funnelRY;
                    resultCount++;
                }
                apexX = funnelRX; apexY = funnelRY;
                apexIdx = rightIdx;
                funnelLX = apexX; funnelLY = apexY;
                leftIdx = apexIdx;
                // Restart scan from apex portal.
                i = apexIdx;
                continue;
            }
        }
    }

    // Add goal as final waypoint.
    if (resultCount < MAX_WAYPOINTS) {
        result[resultCount][0] = goalX;
        result[resultCount][1] = goalY;
        resultCount++;
    }

    // v06.01: Wall-margin post-processing REMOVED. Portal shrinking (AGENT_RADIUS)
    // handles wall clearance at the portal level, so waypoints are already
    // agent_radius units from walls. No need for expensive per-waypoint
    // wall-distance scanning.

    // Replace the A* triangle-center waypoints with the funnel result.
    int oldCount = s_waypointCount;
    memcpy(s_waypoints, result, sizeof(float) * 2 * resultCount);
    s_waypointCount = resultCount;
    s_waypointIdx = 0;
    s_usingFunnel = true;  // v05.95: use tighter arrive distance for funnel waypoints

    Log::Write("FieldNavigation: [funnel] %d triangles -> %d waypoints (was %d centers)",
               s_corridorCount, resultCount, oldCount);
}

// v06.06: Edge-midpoint path generation — the reliable fallback for when funnel
// paths get the player stuck. Instead of the SSFA funnel (which produces optimal
// but sometimes tight waypoints near wall corners), this generates waypoints at
// the midpoints of shared edges between consecutive corridor triangles.
//
// Each midpoint is guaranteed to be on a walkable edge boundary, so steering
// toward it always aims the player through the "doorway" between triangles.
// The path is less smooth than funnel but never gets stuck on wall corners.
//
// Edge midpoints are shrunk inward by AGENT_RADIUS (same as funnel portal
// shrinking) to keep the player away from walls.
static const float EDGE_MIDPOINT_ARRIVE_DIST = 50.0f;  // tight arrive for precise waypoints

static void EdgeMidpointPath(float startX, float startY, float goalX, float goalY)
{
    if (!s_walkmesh.valid || s_corridorCount < 2) return;

    static const float AGENT_RADIUS_EM = 30.0f;
    float result[MAX_WAYPOINTS][2];
    int resultCount = 0;

    // For each pair of consecutive corridor triangles, find the shared edge
    // and place a waypoint at its midpoint (shrunk inward by agent radius).
    for (int i = 0; i + 1 < s_corridorCount && resultCount < MAX_WAYPOINTS - 1; i++) {
        uint16_t triA = s_corridor[i];
        uint16_t triB = s_corridor[i + 1];
        if (triA >= (uint16_t)s_walkmesh.numTriangles || triB >= (uint16_t)s_walkmesh.numTriangles)
            continue;

        const auto& tA = s_walkmesh.triangles[triA];
        // Find which edge of triA connects to triB.
        int edgeIdx = -1;
        for (int e = 0; e < 3; e++) {
            if (tA.neighbor[e] == triB) { edgeIdx = e; break; }
        }
        if (edgeIdx < 0) continue;

        // Shared edge connects vertex[(edge+1)%3] and vertex[(edge+2)%3].
        int vi1 = tA.vertexIdx[(edgeIdx + 1) % 3];
        int vi2 = tA.vertexIdx[(edgeIdx + 2) % 3];
        if (vi1 >= s_walkmesh.numVertices || vi2 >= s_walkmesh.numVertices) continue;

        float x1 = (float)s_walkmesh.vertices[vi1].x;
        float y1 = (float)s_walkmesh.vertices[vi1].y;
        float x2 = (float)s_walkmesh.vertices[vi2].x;
        float y2 = (float)s_walkmesh.vertices[vi2].y;

        // Midpoint of the shared edge.
        float mx = (x1 + x2) / 2.0f;
        float my = (y1 + y2) / 2.0f;

        // Shrink toward the corridor center (triB center) by AGENT_RADIUS.
        // This keeps the waypoint away from walls.
        float toCenterX = s_walkmesh.triangles[triB].centerX - mx;
        float toCenterY = s_walkmesh.triangles[triB].centerY - my;
        float toCenterLen = sqrtf(toCenterX * toCenterX + toCenterY * toCenterY);
        if (toCenterLen > 0.001f) {
            mx += (toCenterX / toCenterLen) * AGENT_RADIUS_EM;
            my += (toCenterY / toCenterLen) * AGENT_RADIUS_EM;
        }

        result[resultCount][0] = mx;
        result[resultCount][1] = my;
        resultCount++;
    }

    // Add goal as final waypoint.
    if (resultCount < MAX_WAYPOINTS) {
        result[resultCount][0] = goalX;
        result[resultCount][1] = goalY;
        resultCount++;
    }

    if (resultCount == 0) return;  // shouldn't happen

    // Replace waypoints.
    int oldCount = s_waypointCount;
    memcpy(s_waypoints, result, sizeof(float) * 2 * resultCount);
    s_waypointCount = resultCount;
    s_waypointIdx = 0;
    s_usingFunnel = true;  // use tight arrive distance (FUNNEL_ARRIVE_DIST)

    Log::Write("FieldNavigation: [edge-midpoint] %d triangles -> %d edge-midpoint waypoints (was %d)",
               s_corridorCount, resultCount, oldCount);
}

// ============================================================================
// SYM name → friendly display name resolution (v05.49)
// ============================================================================
//
// SYM names are Japanese developer shorthand for script entity slots.
// This function maps them to English-friendly names for TTS announcement.
// Strategy:
//   1. Check static table of known SYM→display mappings
//   2. Strip known suffixes (_u, _n, _s, _dummy, Dummy) that indicate variants
//   3. Capitalize the first letter
//   4. Return the cleaned name

struct SymNameMapping {
    const char* sym;      // SYM name (case-insensitive match)
    const char* display;  // friendly display name
};

// Known SYM name → display name mappings.
// Japanese shorthand and internal codes used across many fields.
static const SymNameMapping s_knownNames[] = {
    // Main party characters
    { "squall",       "Squall" },
    { "squall_u",     "Squall" },
    { "rinoa",        "Rinoa" },
    { "zell",         "Zell" },
    { "zell_u",       "Zell" },
    { "selphie",      "Selphie" },
    { "selphie_u",    "Selphie" },
    { "selphie_s",    "Selphie" },
    { "selphiedummy", "Selphie" },
    { "quistis",      "Quistis" },
    { "quistis_n",    "Quistis" },
    { "irvine",       "Irvine" },
    { "seifer",       "Seifer" },
    { "seifer_n",     "Seifer" },
    { "laguna",       "Laguna" },
    { "kiros",        "Kiros" },
    { "ward",         "Ward" },
    { "edea",         "Edea" },
    { "elone",        "Ellone" },
    // NPCs (Japanese shorthand)
    { "kadowaki",     "Dr. Kadowaki" },
    { "cid",          "Headmaster Cid" },
    { "dic",          "Director" },
    { "nida",         "Nida" },
    { "xu",           "Xu" },
    { "fujin",        "Fujin" },
    { "raijin",       "Raijin" },
    // Common entity types (Japanese)
    { "seito1",       "Student" },
    { "seito2",       "Student" },
    { "seito3",       "Student" },
    { "seito4",       "Student" },
    { "seito5",       "Student" },
    { "seito6",       "Student" },
    { "meskun",       "Student" },
    { "betunikun",    "Student" },
    { "naidarokun",   "Student" },
    { "student1",     "Student" },
    { "student2",     "Student" },
    { "student4",     "Student" },
    { "cameraman",    "Cameraman" },
    { "cardgamemaster",  "Card Player" },
    { "cardgamemaster2", "Card Player" },
    // Trepies (Quistis fan club)
    { "trepiegroupie",  "Trepie" },
    { "trepiegroupie1", "Trepie" },
    { "trepiegroupie2", "Trepie" },
    { "trepiegroupie3", "Trepie" },
    // Field objects / controllers
    { "director",     "Director" },
    { "doorcont",     "Door" },
    { "door01",       "Door" },
    { "cliant",       "Terminal" },
    { "britinboard",  "Bulletin Board" },
    { "kage",         "Shadow" },
    { "curtain",      "Curtain" },
    { "glass",        "Window" },
    { "cut",          "Cutscene" },
    { "redlight",     "Red Light" },
    { "greenlight",   "Green Light" },
    { "musickun",     "Musician" },
    { "evl1",         "Elevator" },
    // Classroom objects (bgroom_4 free-roam)
    { "moni",         "Study Panel" },
    { "monitor",      "Study Panel" },
    { "mess",         "Desk" },
    // Garden directory / save points
    { "save",         "Save Point" },
    { "savepoint",    "Save Point" },
    { "shopkun",      "Shop" },
    // Sentinel
    { nullptr, nullptr }
};

// Resolve a SYM name to a friendly display name.
// Writes to outBuf (up to outBufSize-1 chars + null).
static void ResolveFriendlyName(const char* symName, char* outBuf, int outBufSize)
{
    if (!symName || symName[0] == '\0') {
        outBuf[0] = '\0';
        return;
    }

    // 1. Check static table (case-insensitive).
    for (const SymNameMapping* m = s_knownNames; m->sym != nullptr; m++) {
        if (_stricmp(symName, m->sym) == 0) {
            strncpy(outBuf, m->display, outBufSize - 1);
            outBuf[outBufSize - 1] = '\0';
            return;
        }
    }

    // 2. Not in table — clean up the raw name.
    //    Copy, strip trailing _u/_n/_s/Dummy suffixes, capitalize first letter.
    char temp[48];
    strncpy(temp, symName, 47);
    temp[47] = '\0';

    // Strip known suffixes.
    int len = (int)strlen(temp);
    if (len > 2 && (strcmp(temp + len - 2, "_u") == 0 ||
                    strcmp(temp + len - 2, "_n") == 0 ||
                    strcmp(temp + len - 2, "_s") == 0)) {
        temp[len - 2] = '\0';
        len -= 2;
    }
    if (len > 5 && _stricmp(temp + len - 5, "dummy") == 0) {
        temp[len - 5] = '\0';
        len -= 5;
    }

    // Capitalize first letter.
    if (temp[0] >= 'a' && temp[0] <= 'z')
        temp[0] = temp[0] - 'a' + 'A';

    // Replace underscores with spaces.
    for (int i = 0; i < len; i++) {
        if (temp[i] == '_') temp[i] = ' ';
    }

    strncpy(outBuf, temp, outBufSize - 1);
    outBuf[outBufSize - 1] = '\0';
}

// ============================================================================
// v05.53: Model-ID-based character name resolution
// ============================================================================
//
// SYM names are entity SLOT names, not character names. The engine reuses
// slots across scenes, so 'Squall_u' might actually hold Quistis's model.
// For main party characters (models 0-9), the model ID is authoritative.
// This function returns the character name for a known model ID, or nullptr
// if the model is a generic NPC (10+) that should use SYM-based naming.
//
// Model IDs confirmed from ENTDIAG dumps across multiple fields:
//   0=Squall, 1=Zell, 2=Irvine, 3=Quistis(casual), 4=Rinoa, 5=Selphie,
//   6=Seifer, 7=Edea, 8=Quistis(uniform), 9=Laguna(?)
//   10+=generic NPCs/students (use SYM name or "NPC")

static const char* ResolveNameByModelId(int16_t modelId)
{
    switch (modelId) {
        case 0:  return "Squall";
        case 1:  return "Zell";
        case 2:  return "Irvine";
        case 3:  return "Quistis";
        case 4:  return "Rinoa";
        case 5:  return "Selphie";
        case 6:  return "Seifer";
        case 7:  return "Edea";
        case 8:  return "Quistis";
        // 9 might be Laguna but needs confirmation
        default: return nullptr;  // generic NPC, use SYM name
    }
}

// ============================================================================
// v05.51: Background entity classification by SYM name
// ============================================================================
//
// Background entities have no model/talk/push flags. We classify them by name:
// - Known NPC names (characters, students, etc.) → ENT_BG_NPC
// - Known interactive objects (terminal, bulletin board) → ENT_BG_OBJECT
// - Known controllers/animations (cut, redlight, door, etc.) → skip
// - Unknown names → ENT_BG_NPC (default to showing them)

// Names that indicate a controller/animation entity — skip these.
static const char* s_bgSkipNames[] = {
    "cut", "redlight", "greenlight", "doorlight", "door", "doorcont",
    "door01", "kage", "curtain", "glass", "musickun",
    "shadow", "light", "fog", "wind", "rain", "snow",
    nullptr
};

// Returns true if the SYM name indicates a controller/animation that should be skipped.
static bool IsBgControllerName(const char* symName)
{
    if (!symName || symName[0] == '\0') return true;  // unnamed = skip
    for (const char** p = s_bgSkipNames; *p; p++) {
        if (_stricmp(symName, *p) == 0) return true;
    }
    // Names containing "jump" are trigger zones.
    if (strstr(symName, "jump") || strstr(symName, "Jump")) return true;
    // Names starting with "to_" are exit triggers (handled by gateways).
    if (strncmp(symName, "to_", 3) == 0 || strncmp(symName, "To_", 3) == 0) return true;
    return false;
}

// Classify a background entity by its SYM name.
static EntityType ClassifyBgEntity(const char* symName)
{
    if (!symName || symName[0] == '\0') return ENT_UNKNOWN;
    // Check known objects in the friendly name table.
    for (const SymNameMapping* m = s_knownNames; m->sym != nullptr; m++) {
        if (_stricmp(symName, m->sym) == 0) {
            // If it resolves to "Terminal", "Bulletin Board", etc. → object
            if (_stricmp(m->display, "Terminal") == 0 ||
                _stricmp(m->display, "Bulletin Board") == 0 ||
                _stricmp(m->display, "Elevator") == 0 ||
                _stricmp(m->display, "Door") == 0)
                return ENT_BG_OBJECT;
            // Otherwise it's a named character/NPC.
            return ENT_BG_NPC;
        }
    }
    // Default: treat as NPC (shows up in navigation as interactable).
    return ENT_BG_NPC;
}

// ============================================================================
// v05.56: SETLINE/LINEON/LINEOFF hooks — capture trigger line coordinates
// ============================================================================
//
// SETLINE(entityPtr) is called by JSM scripts to define a trigger line.
// The entityPtr is the address of the entity state struct. After the
// original handler runs, the line coordinates are stored somewhere in
// that struct. We capture them by dumping the struct.

// CapturedTriggerLine struct, s_capturedLines[], s_capturedLineCount, and
// s_setlineCallCount are declared above (before ComputeAStarPath) in v05.92.

// SETLINE hook: call original, then read line coordinates from entity struct.
// v05.57: Coordinates confirmed at offset 0x188 in the entity struct:
//   0x188: int16 X1, int16 Y1, int16 Z1, int16 X2, int16 Y2, int16 Z2, int16 lineIdx
static const DWORD LINE_COORD_OFFSET = 0x188;

static int __cdecl HookedSetline(int entityPtr)
{
    int result = s_originalSetline(entityPtr);
    s_setlineCallCount++;

    const char* fieldName = FF8Addresses::pCurrentFieldName
                            ? FF8Addresses::pCurrentFieldName : "(null)";

    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        int16_t x1 = *(int16_t*)(ent + LINE_COORD_OFFSET + 0);
        int16_t y1 = *(int16_t*)(ent + LINE_COORD_OFFSET + 2);
        int16_t z1 = *(int16_t*)(ent + LINE_COORD_OFFSET + 4);
        int16_t x2 = *(int16_t*)(ent + LINE_COORD_OFFSET + 6);
        int16_t y2 = *(int16_t*)(ent + LINE_COORD_OFFSET + 8);
        int16_t z2 = *(int16_t*)(ent + LINE_COORD_OFFSET + 10);
        int16_t lineIdx = *(int16_t*)(ent + LINE_COORD_OFFSET + 12);

        // Store in captured lines array (deduplicate by entity address).
        int slot = -1;
        for (int i = 0; i < s_capturedLineCount; i++) {
            if (s_capturedLines[i].entityAddr == (uint32_t)entityPtr) {
                slot = i;  // update existing
                break;
            }
        }
        if (slot < 0 && s_capturedLineCount < MAX_CAPTURED_LINES)
            slot = s_capturedLineCount++;

        if (slot >= 0) {
            s_capturedLines[slot].entityAddr = (uint32_t)entityPtr;
            // v05.58: SETLINE stores (X,Y,Z) where Y=vertical. For 2D nav
            // we use X (screen-right) and Y (screen-up), not Z (depth).
            s_capturedLines[slot].x1 = x1;
            s_capturedLines[slot].y1 = y1;
            s_capturedLines[slot].z1 = z1;
            s_capturedLines[slot].x2 = x2;
            s_capturedLines[slot].y2 = y2;
            s_capturedLines[slot].z2 = z2;
            s_capturedLines[slot].active = true;  // SETLINE implies active
            s_capturedLines[slot].lineOrder = s_setlineCallCount - 1; // 0-based
            // Name resolved later in RefreshCatalog (SYM not yet loaded here).
            s_capturedLines[slot].name[0] = '\0';
        }

        // v05.58: Center uses X and Y (not Z) for 2D navigation.
        float cx = (float)(x1 + x2) / 2.0f;
        float cy = (float)(y1 + y2) / 2.0f;
        Log::Write("FieldNavigation: [SETLINE] call#%d field=%s ent=0x%08X "
                   "line(%d,%d,%d)->(%d,%d,%d) idx=%d center=(%.0f,%.0f)",
                   s_setlineCallCount, fieldName, (uint32_t)entityPtr,
                   (int)x1, (int)y1, (int)z1, (int)x2, (int)y2, (int)z2,
                   (int)lineIdx, cx, cy);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: [SETLINE] call#%d field=%s ent=0x%08X (SEH)",
                   s_setlineCallCount, fieldName, (uint32_t)entityPtr);
    }

    return result;
}

static int __cdecl HookedLineon(int entityPtr)
{
    int result = s_originalLineon(entityPtr);
    for (int i = 0; i < s_capturedLineCount; i++) {
        if (s_capturedLines[i].entityAddr == (uint32_t)entityPtr)
            s_capturedLines[i].active = true;
    }
    Log::Write("FieldNavigation: [LINEON] field=%s ent=0x%08X",
               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
               (uint32_t)entityPtr);
    return result;
}

static int __cdecl HookedLineoff(int entityPtr)
{
    int result = s_originalLineoff(entityPtr);
    for (int i = 0; i < s_capturedLineCount; i++) {
        if (s_capturedLines[i].entityAddr == (uint32_t)entityPtr)
            s_capturedLines[i].active = false;
    }
    Log::Write("FieldNavigation: [LINEOFF] field=%s ent=0x%08X",
               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
               (uint32_t)entityPtr);
    return result;
}

// ============================================================================
// v05.78: TALKRADIUS/PUSHRADIUS hooks — capture interaction radii
// ============================================================================
//
// TALKRADIUS(entityPtr) sets the radius within which the player can talk to
// this entity (by pressing X/Confirm). The radius value was on the JSM stack
// before dispatch and has been consumed by the handler — we read it from the
// entity struct after the handler returns.
//
// The entity struct is the "others" entity state (ff8_field_state_other).
// The entityPtr passed to the opcode handler is the same pointer as
// base + ENTITY_STRIDE * i for the entity executing the opcode.

static int __cdecl HookedTalkradius(int entityPtr)
{
    // v05.79: Capture BEFORE values at candidate offsets, then call original,
    // then capture AFTER. Log only the offsets that changed.
    // Scan 0x188-0x25E = 0xD6 bytes = 107 uint16 slots
    static const int SCAN_START = 0x188;
    static const int SCAN_SLOTS = 107;
    uint16_t before[SCAN_SLOTS] = {};
    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        for (int i = 0; i < SCAN_SLOTS; i++)
            before[i] = *(uint16_t*)(ent + SCAN_START + i * 2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    int result = s_originalTalkradius(entityPtr);

    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        int16_t  modelId = *(int16_t*)(ent + 0x218);

        Log::Write("FieldNavigation: [TALKRAD] ent=0x%08X model=%d",
                   (uint32_t)entityPtr, (int)modelId);

        // Log only offsets that changed (the smoking gun)
        int changedCount = 0;
        for (int i = 0; i < SCAN_SLOTS; i++) {
            uint16_t after = *(uint16_t*)(ent + SCAN_START + i * 2);
            if (after != before[i]) {
                uint32_t off = SCAN_START + i * 2;
                Log::Write("FieldNavigation: [TALKRAD]   CHANGED @0x%03X: %u -> %u",
                           off, (unsigned)before[i], (unsigned)after);
                changedCount++;
            }
        }
        if (changedCount == 0)
            Log::Write("FieldNavigation: [TALKRAD]   NO changes in 0x188-0x25E range!");

        // Also dump the full 0x21A-0x24E region as context
        Log::Write("FieldNavigation: [TALKRAD]   context: @21A=%u @21C=%u @21E=%u @220=%u @222=%u @224=%u @234=%u @236=%u @244=%u @246=%u",
                   *(uint16_t*)(ent+0x21A), *(uint16_t*)(ent+0x21C),
                   *(uint16_t*)(ent+0x21E), *(uint16_t*)(ent+0x220),
                   *(uint16_t*)(ent+0x222), *(uint16_t*)(ent+0x224),
                   *(uint16_t*)(ent+0x234), *(uint16_t*)(ent+0x236),
                   *(uint16_t*)(ent+0x244), *(uint16_t*)(ent+0x246));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: [TALKRAD] ent=0x%08X (SEH)", (uint32_t)entityPtr);
    }

    return result;
}

static int __cdecl HookedPushradius(int entityPtr)
{
    // v05.79: Before/after diff, same as TALKRADIUS.
    static const int SCAN_START_P = 0x188;
    static const int SCAN_SLOTS_P = 107;
    uint16_t before[SCAN_SLOTS_P] = {};
    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        for (int i = 0; i < SCAN_SLOTS_P; i++)
            before[i] = *(uint16_t*)(ent + SCAN_START_P + i * 2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    int result = s_originalPushradius(entityPtr);

    __try {
        uint8_t* ent = (uint8_t*)(uint32_t)entityPtr;
        int16_t  modelId = *(int16_t*)(ent + 0x218);

        Log::Write("FieldNavigation: [PUSHRAD] ent=0x%08X model=%d",
                   (uint32_t)entityPtr, (int)modelId);

        int changedCount = 0;
        for (int i = 0; i < SCAN_SLOTS_P; i++) {
            uint16_t after = *(uint16_t*)(ent + SCAN_START_P + i * 2);
            if (after != before[i]) {
                uint32_t off = SCAN_START_P + i * 2;
                Log::Write("FieldNavigation: [PUSHRAD]   CHANGED @0x%03X: %u -> %u",
                           off, (unsigned)before[i], (unsigned)after);
                changedCount++;
            }
        }
        if (changedCount == 0)
            Log::Write("FieldNavigation: [PUSHRAD]   NO changes in 0x188-0x25E range!");

        Log::Write("FieldNavigation: [PUSHRAD]   context: @21A=%u @21C=%u @21E=%u @220=%u @222=%u @224=%u @234=%u @236=%u @244=%u @246=%u",
                   *(uint16_t*)(ent+0x21A), *(uint16_t*)(ent+0x21C),
                   *(uint16_t*)(ent+0x21E), *(uint16_t*)(ent+0x220),
                   *(uint16_t*)(ent+0x222), *(uint16_t*)(ent+0x224),
                   *(uint16_t*)(ent+0x234), *(uint16_t*)(ent+0x236),
                   *(uint16_t*)(ent+0x244), *(uint16_t*)(ent+0x246));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: [PUSHRAD] ent=0x%08X (SEH)", (uint32_t)entityPtr);
    }

    return result;
}

// ============================================================================
// Key handlers
// ============================================================================

// Forward declarations.
static bool AnnounceCurrentTarget();
static void CycleEntity(int delta);
static void RefreshCatalog();

// Compute and speak the current distance/direction from the player to the
// entity at s_selectedCatalogIdx. Always reads live positions so Backspace
// gives a fresh reading after the player has moved.
// Returns false if player position is not yet known.
static bool AnnounceCurrentTarget()
{
    if (s_nonPlayerCount == 0) {
        ScreenReader::Speak("No entities in this area.");
        return false;
    }

    // Skip player entity when selected.
    if (s_selectedCatalogIdx < s_catalogCount &&
        s_catalog[s_selectedCatalogIdx].entityIdx == s_playerEntityIdx) {
        CycleEntity(+1);
        return true;
    }

    // Compute stable 1-based rank among non-player catalog entries.
    int rank = 0;
    for (int c = 0; c <= s_selectedCatalogIdx && c < s_catalogCount; c++) {
        if (s_catalog[c].entityIdx != s_playerEntityIdx)
            rank++;
    }
    if (rank == 0) rank = 1;

    const EntityInfo& catEnt = s_catalog[s_selectedCatalogIdx];
    int ei = catEnt.entityIdx;

    // v05.59: Simplified type+number labels.
    // v05.72: Trigger lines use their name field ("Screen transition" or "Event").
    const char* typeLabel = "Entity";
    if (catEnt.entityIdx <= -200 && catEnt.type == ENT_EXIT)
        typeLabel = "Exit";   // screen transition
    else if (catEnt.entityIdx <= -200)
        typeLabel = "Event";  // event trigger
    else if (catEnt.type == ENT_EXIT)
        typeLabel = "Exit";
    else if (catEnt.type == ENT_NPC || catEnt.type == ENT_BG_NPC) typeLabel = "NPC";
    else if (catEnt.type == ENT_OBJECT || catEnt.type == ENT_BG_OBJECT) typeLabel = "Object";

    // Count entities of same type up to this one to get type-specific number.
    // v05.72: Events and Exits are separate categories even though both can
    // have entityIdx <= -200. Match by typeLabel string for trigger entries.
    int typeNum = 0;
    for (int c = 0; c < s_catalogCount; c++) {
        const EntityInfo& ce = s_catalog[c];
        if (ce.entityIdx == s_playerEntityIdx) continue;
        bool sameType = false;
        // Exits: all ENT_EXIT entries (gateway + screen transition) are one group.
        // Events: trigger lines with ENT_OBJECT are their own group.
        // NPCs: regular entities (entityIdx >= 0).
        if (strcmp(typeLabel, "Exit") == 0 && ce.type == ENT_EXIT)
            sameType = true;
        else if (strcmp(typeLabel, "Event") == 0 && ce.entityIdx <= -200 && ce.type != ENT_EXIT)
            sameType = true;
        else if (strcmp(typeLabel, "NPC") == 0 && ce.entityIdx >= 0 && ce.entityIdx != s_playerEntityIdx)
            sameType = true;
        else if (strcmp(typeLabel, "Object") == 0 && ce.entityIdx >= 0 && ce.type == ENT_OBJECT)
            sameType = true;
        if (sameType) typeNum++;
        if (c == s_selectedCatalogIdx) break;
    }
    if (typeNum == 0) typeNum = 1;

    // Count total of same type.
    int typeTotal = 0;
    for (int c = 0; c < s_catalogCount; c++) {
        const EntityInfo& ce = s_catalog[c];
        if (ce.entityIdx == s_playerEntityIdx) continue;
        bool sameType = false;
        if (strcmp(typeLabel, "Exit") == 0 && ce.type == ENT_EXIT)
            sameType = true;
        else if (strcmp(typeLabel, "Event") == 0 && ce.entityIdx <= -200 && ce.type != ENT_EXIT)
            sameType = true;
        else if (strcmp(typeLabel, "NPC") == 0 && ce.entityIdx >= 0 && ce.entityIdx != s_playerEntityIdx)
            sameType = true;
        else if (strcmp(typeLabel, "Object") == 0 && ce.entityIdx >= 0 && ce.type == ENT_OBJECT)
            sameType = true;
        if (sameType) typeTotal++;
    }

    char label[96];
    snprintf(label, sizeof(label), "%s %d of %d", typeLabel, typeNum, typeTotal);

    float px = 0, pz = 0;
    if (s_playerEntityIdx < 0 || !GetEntityPos(s_playerEntityIdx, px, pz)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s. Player position not yet known.", label);
        ScreenReader::Speak(buf);
        return true;
    }

    // Get target position: gateway exits use INF center; triggers use INF center; entities use struct.
    float tx = 0, tz = 0;
    bool targetLocated = false;
    if (catEnt.type == ENT_EXIT && catEnt.gatewayIdx >= 0 && catEnt.gatewayIdx < s_gatewayCount) {
        tx = s_gateways[catEnt.gatewayIdx].centerX;
        tz = s_gateways[catEnt.gatewayIdx].centerZ;
        targetLocated = true;
    } else if (ei <= -200) {
        // v05.57: Trigger zone — position from SETLINE opcode capture.
        int trigIdx = -(ei + 200);
        if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
            tx = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
            tz = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
            targetLocated = true;
        }
    } else if (ei >= 0 && ei < MAX_ENTITIES) {
        targetLocated = GetEntityPos(ei, tx, tz);
    }

    if (!targetLocated) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s. Not yet located.", label);
        ScreenReader::Speak(buf);
        Log::Write("FieldNavigation: [nav] cat%d ent%d rank=%d/%d NOT_YET_LOCATED",
                   s_selectedCatalogIdx, ei, rank, s_nonPlayerCount);
        return true;
    }

    float dx   = tx - px;
    float dz   = tz - pz;
    float dist = sqrtf(dx*dx + dz*dz);

    // Sanity check: positions over MAX_SANE_DIST are likely stale/wrong center data.
    if (dist > MAX_SANE_DIST) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s. Position not yet reliable — walk closer.", label);
        ScreenReader::Speak(buf);
        Log::Write("FieldNavigation: [nav] cat%d ent%d rank=%d/%d SANE_FAIL dist=%.0f "
                   "player=(%.0f,%.0f) target=(%.0f,%.0f)",
                   s_selectedCatalogIdx, ei, rank, s_nonPlayerCount, dist, px, pz, tx, tz);
        return true;
    }

    char dirBuf[128];
    FormatNavComponents(dx, dz, dirBuf, sizeof(dirBuf));

    char buf[256];
    if (dist < 250.0f) {
        snprintf(buf, sizeof(buf), "%s. Right here.", label);
    } else {
        snprintf(buf, sizeof(buf), "%s. %s.", label, dirBuf);
    }
    ScreenReader::Speak(buf);

    Log::Write("FieldNavigation: [nav] cat%d ent%d rank=%d/%d '%s' %s dist=%.0f "
               "player=(%.0f,%.0f) target=(%.0f,%.0f)",
               s_selectedCatalogIdx, ei, rank, s_nonPlayerCount,
               catEnt.name, dirBuf, dist, px, pz, tx, tz);
    return true;
}

// Step forward (+1) or backward (-1) through catalog[], skipping the player.
// Wraps around. Accepts entities with invalid/unknown centers — announce handles those.
static void CycleEntity(int delta)
{
    if (s_nonPlayerCount == 0) {
        ScreenReader::Speak("No entities in this area.");
        return;
    }

    int attempts = s_catalogCount;
    while (attempts-- > 0) {
        s_selectedCatalogIdx =
            ((s_selectedCatalogIdx + delta) % s_catalogCount + s_catalogCount) % s_catalogCount;
        const EntityInfo& entry = s_catalog[s_selectedCatalogIdx];
        if (entry.entityIdx == s_playerEntityIdx) continue;
        // v05.54: Allow "others" entities (>=0), gateways (-1), and triggers (<=-200).
        if (entry.entityIdx < -1 && entry.entityIdx > -200 && entry.gatewayIdx < 0) continue;
        if (entry.entityIdx >= MAX_ENTITIES) continue;
        break;
    }
    AnnounceCurrentTarget();
}

// ============================================================================
// Auto-drive: inject arrow-key input to walk toward the selected entity
// ============================================================================

// Inject or release a direction key via SendInput using hardware scan codes.
// DirectInput reads raw hardware scan codes, so we must use KEYEVENTF_SCANCODE
// rather than KEYEVENTF_EXTENDEDKEY+VK.  Arrow keys have the E0 extended prefix,
// indicated by KEYEVENTF_EXTENDEDKEY alongside KEYEVENTF_SCANCODE.
static void InjectKey(WORD scanCode, bool down)
{
    INPUT inp      = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = 0;      // must be 0 when using KEYEVENTF_SCANCODE
    inp.ki.wScan   = scanCode;
    inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY
                   | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));
}

// Release all held direction keys and clear the held bitmask.
static void ReleaseAllDirections()
{
    if (s_driveHeld & DIR_UP)    InjectKey(SC_UP,    false);
    if (s_driveHeld & DIR_DOWN)  InjectKey(SC_DOWN,  false);
    if (s_driveHeld & DIR_LEFT)  InjectKey(SC_LEFT,  false);
    if (s_driveHeld & DIR_RIGHT) InjectKey(SC_RIGHT, false);
    s_driveHeld = 0;
}

// Apply a new desired direction bitmask: release keys no longer needed,
// press keys newly needed.
// v05.85: Keyboard injection is REQUIRED to activate the game's movement code
// path. Analog steering overrides the direction, but keyboard buttons are the
// trigger that makes the game process movement at all.
static void SetHeldDirections(uint8_t desired)
{
    uint8_t toRelease = s_driveHeld  & ~desired;
    uint8_t toPress   = desired & ~s_driveHeld;
    if (toRelease & DIR_UP)    InjectKey(SC_UP,    false);
    if (toRelease & DIR_DOWN)  InjectKey(SC_DOWN,  false);
    if (toRelease & DIR_LEFT)  InjectKey(SC_LEFT,  false);
    if (toRelease & DIR_RIGHT) InjectKey(SC_RIGHT, false);
    if (toPress   & DIR_UP)    InjectKey(SC_UP,    true);
    if (toPress   & DIR_DOWN)  InjectKey(SC_DOWN,  true);
    if (toPress   & DIR_LEFT)  InjectKey(SC_LEFT,  true);
    if (toPress   & DIR_RIGHT) InjectKey(SC_RIGHT, true);
    s_driveHeld = desired;
}

// v06.14: Per-field heading calibration.
// The game interprets analog stick input relative to the camera orientation.
// On each field, lX=+1000 moves the player along the camera's right vector
// in entity/world space, and lY=+1000 moves along the camera's down vector.
// We calibrate by injecting a known analog direction at drive start and
// measuring the resulting world-space movement direction.
//
// Until calibrated, we use the .ca camera axes (loaded at field load) as
// a best guess. The calibration refines this empirically.
static float s_camRightX = 1.0f;   // camera right vector X component (normalized)
static float s_camRightY = 0.0f;   // camera right vector Y component
static float s_camDownX  = 0.0f;   // camera down vector X component  
static float s_camDownY  = 1.0f;   // camera down vector Y component (default: +Y = down)
static bool  s_camCalibrated = false;

// v06.14: Heading calibration state machine.
// At drive start, we inject lX=+1000,lY=0 for a few ticks, measure the
// resulting movement direction, and use that as the camera right axis.
// Then inject lX=0,lY=+1000 for a few ticks to get the camera down axis.
// After both are measured, s_camCalibrated=true and we use the measured axes.
static int   s_calibPhase = 0;       // 0=not calibrating, 1=measuring right, 2=measuring down, 3=done
static int   s_calibTicks = 0;       // ticks in current calibration phase
static float s_calibStartX = 0;      // player position at calibration phase start
static float s_calibStartY = 0;
static const int CALIB_SETTLE_TICKS = 8;   // ticks to let the game start moving
static const int CALIB_MEASURE_TICKS = 16; // ticks to measure movement direction
static bool  s_calibPending = false;  // true if calibration should run at drive start

// v05.84/v06.14: Set analog override from a world-space direction vector.
// Converts (dx, dy) in entity/world space into DIJOYSTATE2 lX/lY values
// using the per-field camera axes to produce correct screen-relative input.
// DirectInput axis convention: lX +1000 = screen right, lY +1000 = screen down.
static void SetAnalogFromVector(float dx, float dy)
{
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1.0f) {
        s_analogDesiredLX = 0;
        s_analogDesiredLY = 0;
        return;
    }
    float nx = dx / len;
    float ny = dy / len;
    // v06.14: Project world-space direction onto camera axes.
    // lX = dot(worldDir, camRight) = how much of the desired direction
    //   aligns with the camera's rightward axis.
    // lY = dot(worldDir, camDown) = how much aligns with camera's downward axis.
    float lxF = (nx * s_camRightX + ny * s_camRightY) * 1000.0f;
    float lyF = (nx * s_camDownX  + ny * s_camDownY)  * 1000.0f;
    int lx = (int)lxF;
    int ly = (int)lyF;
    if (lx < -1000) lx = -1000; if (lx > 1000) lx = 1000;
    if (ly < -1000) ly = -1000; if (ly > 1000) ly = 1000;
    s_analogDesiredLX = lx;
    s_analogDesiredLY = ly;
}

// Stop auto-drive cleanly: release keys, clear state, optionally speak reason.
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    // v05.85: Release any held keyboard direction keys.
    ReleaseAllDirections();
    // v05.84: Deactivate analog override and remove fake gamepad.
    s_analogOverrideActive = false;
    s_analogDesiredLX = 0;
    s_analogDesiredLY = 0;
    // Restore original dinput pointers.
    if (s_fakeGamepadInstalled && FF8Addresses::HasDinputGamepadPtrs()) {
        *FF8Addresses::pDinputGamepadDevicePtr = s_savedDevicePtr;
        *FF8Addresses::pDinputGamepadStatePtr  = s_savedStatePtr;
        s_fakeGamepadInstalled = false;
        Log::Write("FieldNavigation: [drive] fake gamepad removed, original ptrs restored");
    }
    // v06.21: Do NOT restore talk radius here — the player needs the expanded
    // radius to persist so they can press X to interact after "Arrived".
    // The game's TALKRADIUS opcode resets it naturally on the next field load.
    if (s_driveTalkRadExpanded) {
        Log::Write("FieldNavigation: [drive] talkRadius stays expanded (%u -> %u) for ent%d — resets on field load",
                   (unsigned)s_driveOrigTalkRadius,
                   (unsigned)GetEntityTalkRadius(s_driveTargetEntityIdx),
                   s_driveTargetEntityIdx);
    }
    s_driveTalkRadExpanded = false;
    s_driveTargetEntityIdx = -1;
    s_driveOrigTalkRadius = 0;

    // v06.08: NavLog drive end
    NavLog::DriveEnd(reason ? reason : "unknown", s_driveTotalTicks, 0.0f,
                     s_driveWigglePhase, s_driveStartDist);

    s_driveActive = false;
    s_driveTrigTarget = false;
    s_driveTrigCrossStart = 0.0f;
    s_driveSkipTrigIdx = -1;
    Log::Write("FieldNavigation: [drive] stopped: %s", reason);
    if (reason) ScreenReader::Speak(reason);
}

// Called from Update() every tick while auto-drive is active.
// Computes direction to target, injects appropriate arrow keys.
static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    // Safety: must be on field and not in a menu/FMV.
    if (!FF8Addresses::IsOnField()) { StopAutoDrive("Left field."); return; }

    if (s_playerEntityIdx < 0) { StopAutoDrive("Player position lost."); return; }

    // v06.14: Heading calibration — runs at the start of the first drive on each field.
    // Injects known analog directions and measures the resulting world-space movement
    // to determine the camera-to-world rotation for this field.
    if (s_calibPhase > 0 && s_calibPhase < 3) {
        float cpx = 0, cpy = 0;
        GetEntityPos(s_playerEntityIdx, cpx, cpy);
        s_calibTicks++;

        if (s_calibPhase == 1) {
            // Phase 1: inject lX=+1000, lY=0 (screen-right) and measure movement.
            s_analogOverrideActive = true;
            s_analogDesiredLX = 1000;
            s_analogDesiredLY = 0;
            SetHeldDirections(DIR_RIGHT);  // keyboard trigger for movement

            if (s_calibTicks == CALIB_SETTLE_TICKS) {
                // Record position after settling.
                s_calibStartX = cpx;
                s_calibStartY = cpy;
            } else if (s_calibTicks >= CALIB_SETTLE_TICKS + CALIB_MEASURE_TICKS) {
                // Measure displacement.
                float cdx = cpx - s_calibStartX;
                float cdy = cpy - s_calibStartY;
                float cdist = sqrtf(cdx*cdx + cdy*cdy);
                if (cdist > 5.0f) {
                    // Normalize: this is the world-space direction of lX=+1000.
                    s_camRightX = cdx / cdist;
                    s_camRightY = cdy / cdist;
                    Log::Write("FieldNavigation: [CALIB] phase 1 done: lX=+1000 moved (%.1f,%.1f) dist=%.1f -> camRight=(%.3f,%.3f)",
                               cdx, cdy, cdist, s_camRightX, s_camRightY);
                } else {
                    Log::Write("FieldNavigation: [CALIB] phase 1 FAILED: no movement (dist=%.1f), keeping default camRight", cdist);
                }
                // Transition to phase 2.
                s_calibPhase = 2;
                s_calibTicks = 0;
            }
            s_driveTotalTicks++;
            return;  // don't run normal navigation during calibration
        }

        if (s_calibPhase == 2) {
            // Phase 2: inject lX=0, lY=+1000 (screen-down) and measure movement.
            s_analogOverrideActive = true;
            s_analogDesiredLX = 0;
            s_analogDesiredLY = 1000;
            SetHeldDirections(DIR_DOWN);  // keyboard trigger for movement

            if (s_calibTicks == CALIB_SETTLE_TICKS) {
                s_calibStartX = cpx;
                s_calibStartY = cpy;
            } else if (s_calibTicks >= CALIB_SETTLE_TICKS + CALIB_MEASURE_TICKS) {
                float cdx = cpx - s_calibStartX;
                float cdy = cpy - s_calibStartY;
                float cdist = sqrtf(cdx*cdx + cdy*cdy);
                if (cdist > 5.0f) {
                    s_camDownX = cdx / cdist;
                    s_camDownY = cdy / cdist;
                    Log::Write("FieldNavigation: [CALIB] phase 2 done: lY=+1000 moved (%.1f,%.1f) dist=%.1f -> camDown=(%.3f,%.3f)",
                               cdx, cdy, cdist, s_camDownX, s_camDownY);
                } else {
                    // v06.17: Derive camDown from camRight by 90° clockwise rotation.
                    // In screen space, rotating right vector 90° CW gives the down vector.
                    // rotation: (x,y) -> (y, -x)
                    s_camDownX = s_camRightY;
                    s_camDownY = -s_camRightX;
                    Log::Write("FieldNavigation: [CALIB] phase 2 FAILED: no movement (dist=%.1f), derived camDown=(%.3f,%.3f) from camRight perpendicular",
                               cdist, s_camDownX, s_camDownY);
                }
                // Calibration complete.
                s_calibPhase = 3;
                s_camCalibrated = true;
                s_calibPending = false;
                // Log the final calibration result.
                Log::Write("FieldNavigation: [CALIB] complete: camRight=(%.3f,%.3f) camDown=(%.3f,%.3f)",
                           s_camRightX, s_camRightY, s_camDownX, s_camDownY);
                // Reset stuck detection to account for calibration movement.
                s_driveStuckTicks = 0;
                GetEntityPos(s_playerEntityIdx, s_driveStuckPosX, s_driveStuckPosY);
            }
            s_driveTotalTicks++;
            return;  // don't run normal navigation during calibration
        }
    }

    const EntityInfo& catTarget = (s_selectedCatalogIdx < s_catalogCount)
                                   ? s_catalog[s_selectedCatalogIdx]
                                   : s_catalog[0]; // safety fallback
    int ei = catTarget.entityIdx;
    if (ei == s_playerEntityIdx) { StopAutoDrive("No target."); return; }
    // Gateway exits (entityIdx == -1) and triggers (<=-200) are valid drive targets.
    if (ei < 0 && ei > -200 && catTarget.gatewayIdx < 0) { StopAutoDrive("Target lost."); return; }
    if (ei >= MAX_ENTITIES)                              { StopAutoDrive("Target lost."); return; }

    // v05.37: Suspend key injection during dialog (scripted cutscenes lock movement).
    // Don't stop the drive — just pause until dialog clears.
    if (FieldDialog::IsDialogOpen()) {
        // Release any held keys so the game doesn't see stuck inputs.
        ReleaseAllDirections();
        // Freeze stuck counter so dialog pauses don't count as being stuck.
        s_driveStuckTicks = 0;
        return;
    }

    float px = 0, pz = 0, tx = 0, tz = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, pz)) {
        StopAutoDrive("Player position lost.");
        return;
    }
    // v05.47: Gateway exits use INF center position.
    // v05.54: Trigger zones also use INF center position.
    bool gotTarget = false;
    if (catTarget.type == ENT_EXIT && catTarget.gatewayIdx >= 0 && catTarget.gatewayIdx < s_gatewayCount) {
        tx = s_gateways[catTarget.gatewayIdx].centerX;
        tz = s_gateways[catTarget.gatewayIdx].centerZ;
        gotTarget = true;
    } else if (ei <= -200) {
        int trigIdx = -(ei + 200);
        if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
            tx = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
            tz = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
            gotTarget = true;
        }
    } else if (ei >= 0) {
        gotTarget = GetEntityPos(ei, tx, tz);
    }
    if (!gotTarget) {
        StopAutoDrive("Target lost.");
        return;
    }
    float dx   = tx - px;
    float dz   = tz - pz;
    float dist = sqrtf(dx*dx + dz*dz);

    // v05.76: For trigger line targets, check if the player has crossed the line.
    // This is the primary arrival condition for screen transitions and events.
    if (s_driveTrigTarget && ei <= -200) {
        int trigIdx = -(ei + 200);
        if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
            float tlx1 = (float)s_capturedLines[trigIdx].x1;
            float tly1 = (float)s_capturedLines[trigIdx].y1;
            float tlx2 = (float)s_capturedLines[trigIdx].x2;
            float tly2 = (float)s_capturedLines[trigIdx].y2;
            float tdx = tlx2 - tlx1;
            float tdy = tly2 - tly1;
            float crossNow = tdx * (pz - tly1) - tdy * (px - tlx1);
            // Player has crossed if the sign flipped from start.
            if (s_driveTrigCrossStart != 0.0f && crossNow * s_driveTrigCrossStart < 0.0f) {
                StopAutoDrive("Arrived.");
                return;
            }
            // Also offset the target 300 units past the line center
            // so the heading aims through the line, not just to its center.
            float dirLen = sqrtf(dx*dx + dz*dz);
            if (dirLen > 1.0f) {
                tx += (dx / dirLen) * 300.0f;
                tz += (dz / dirLen) * 300.0f;
                dx = tx - px;
                dz = tz - pz;
                dist = sqrtf(dx*dx + dz*dz);
            }
        }
    }
    if (dist < s_driveArriveDist) {
        StopAutoDrive("Arrived.");
        return;
    }

    // v05.66: If we have A* waypoints, steer toward the current waypoint
    // instead of the final target. Advance to the next waypoint when close.
    // Chain-advance is delayed until tick 30 (~0.5s) so we don't skip
    // nearby waypoints before the player has started moving.
    float steerX = tx, steerY = tz;  // default: straight to target
    if (s_waypointCount > 0 && s_waypointIdx < s_waypointCount) {
        // v05.66: Only chain-advance after the player has had time to move.
        // On the first few ticks, nearby waypoints shouldn't be skipped
        // because they represent the initial steering direction.
        if (s_driveTotalTicks >= 30) {
            // Chain-advance: skip past all waypoints we're already close to.
            float wpArriveDist = s_usingFunnel ? FUNNEL_ARRIVE_DIST : WAYPOINT_ARRIVE_DIST;
            int prevWpIdx = s_waypointIdx;
            while (s_waypointIdx < s_waypointCount - 1) {
                float wpDx = s_waypoints[s_waypointIdx][0] - px;
                float wpDy = s_waypoints[s_waypointIdx][1] - pz;
                float wpDist = sqrtf(wpDx*wpDx + wpDy*wpDy);
                if (wpDist >= wpArriveDist) {
                    // v06.08: Overshoot detection — if we got close and are now
                    // moving away, advance even though we didn't hit the exact threshold.
                    // This catches the corridor oscillation where the player passes
                    // through the waypoint zone at dist~192 but FUNNEL_ARRIVE_DIST=60
                    // never triggers.
                    if (s_usingFunnel && s_wpMinDist < WP_OVERSHOOT_CLOSE &&
                        wpDist > s_wpMinDist * WP_OVERSHOOT_RATIO + WP_OVERSHOOT_MARGIN) {
                        Log::Write("FieldNavigation: [drive] wp %d/%d overshoot (dist=%.0f, minDist=%.0f), advancing",
                                   s_waypointIdx, s_waypointCount, wpDist, s_wpMinDist);
                        NavLog::DriveWaypoint(s_waypointIdx, s_waypointCount, px, pz, dist, s_driveTotalTicks);
                        s_wpMinDist = 1e30f;  // reset for next wp
                        s_waypointIdx++;
                        continue;  // check the next waypoint too
                    }
                    // Update min distance tracker.
                    if (wpDist < s_wpMinDist) s_wpMinDist = wpDist;
                    break;  // not close enough yet and no overshoot
                }
                Log::Write("FieldNavigation: [drive] wp %d/%d reached (dist=%.0f), advancing",
                           s_waypointIdx, s_waypointCount, wpDist);
                NavLog::DriveWaypoint(s_waypointIdx, s_waypointCount, px, pz, dist, s_driveTotalTicks);
                s_wpMinDist = 1e30f;  // reset for next wp
                s_waypointIdx++;
            }
            // Reset min dist tracker and recovery phase if we changed waypoints (progress made).
            // v06.11: Don't reset recovery phase when a recovery re-path places wp0/wp1
            // near the player and they're instantly "reached." Only count as progress if
            // we advance to wpIdx >= 3 during recovery (past the trivial near-player wps),
            // OR if we were already past wp 2 before recovery started (prevWpIdx >= 3).
            if (s_waypointIdx != prevWpIdx) {
                s_wpMinDist = 1e30f;
                bool genuineProgress = (s_driveWigglePhase == 0) ||
                                       (s_waypointIdx >= 3) ||
                                       (prevWpIdx >= 3);
                if (genuineProgress) {
                    s_driveWigglePhase = 0;  // v06.08: genuine progress resets recovery counter
                    s_driveNoProgressCount = 0;  // v06.10: waypoint progress resets no-progress counter
                    s_driveProgressDist = dist;  // v06.10: re-baseline from new position
                }
            }
        }
        steerX = s_waypoints[s_waypointIdx][0];
        steerY = s_waypoints[s_waypointIdx][1];
    }
    // v06.17: Corridor-level steering — steer toward the shared-edge midpoint
    // of the next corridor triangle instead of distant funnel waypoints.
    // This gives very local targets that are always close, preventing overshoot.
    // The corridor from A* tells us which triangle sequence leads to the goal.
    // Each tick, we find the player's current triangle in the corridor and target
    // the midpoint of the shared edge to the next corridor triangle.
    if (s_walkmesh.valid && s_corridorCount >= 2 && s_driveTotalTicks >= 30) {
        uint16_t nowTri = 0xFFFF;
        {
            uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base2)
                nowTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
        }
        if (nowTri != 0xFFFF && nowTri < (uint16_t)s_walkmesh.numTriangles) {
            // Find player's position in the corridor.
            int corridorPos = -1;
            for (int ci = 0; ci < s_corridorCount; ci++) {
                if (s_corridor[ci] == nowTri) { corridorPos = ci; break; }
            }
            if (corridorPos >= 0 && corridorPos + 1 < s_corridorCount) {
                // Target = midpoint of shared edge to next corridor triangle.
                uint16_t nextTri = s_corridor[corridorPos + 1];
                const auto& tCur = s_walkmesh.triangles[nowTri];
                int sharedEdge = -1;
                for (int e = 0; e < 3; e++) {
                    if (tCur.neighbor[e] == nextTri) { sharedEdge = e; break; }
                }
                if (sharedEdge >= 0) {
                    int vi1 = tCur.vertexIdx[(sharedEdge + 1) % 3];
                    int vi2 = tCur.vertexIdx[(sharedEdge + 2) % 3];
                    if (vi1 < s_walkmesh.numVertices && vi2 < s_walkmesh.numVertices) {
                        float emx = ((float)s_walkmesh.vertices[vi1].x + (float)s_walkmesh.vertices[vi2].x) / 2.0f;
                        float emy = ((float)s_walkmesh.vertices[vi1].y + (float)s_walkmesh.vertices[vi2].y) / 2.0f;
                        // Shrink toward next triangle center by agent radius.
                        float toCX = s_walkmesh.triangles[nextTri].centerX - emx;
                        float toCY = s_walkmesh.triangles[nextTri].centerY - emy;
                        float toCLen = sqrtf(toCX*toCX + toCY*toCY);
                        if (toCLen > 0.001f) {
                            emx += (toCX / toCLen) * 30.0f;
                            emy += (toCY / toCLen) * 30.0f;
                        }
                        // v06.22: Don't use corridor steering if the edge midpoint
                        // is across a non-target trigger line from the player.
                        // This prevents the corridor from routing through trigger zones.
                        bool edgeCrossesTrig = false;
                        if (s_capturedLineCount > 0) {
                            edgeCrossesTrig = IsSeparatedByTriggerLine(px, pz, emx, emy, s_driveSkipTrigIdx);
                        }
                        if (!edgeCrossesTrig) {
                            steerX = emx;
                            steerY = emy;
                        }
                        // else: keep the funnel waypoint as steer target
                    }
                }
            } else if (corridorPos < 0) {
                // Player left the corridor — re-path needed (recovery will handle).
            }
        }
    }

    // Recompute dx/dz toward the steer target.
    dx = steerX - px;
    dz = steerY - pz;

    // v06.17: Wall-avoidance steering bias.
    // DISABLED in v06.20: Causes more harm than good. In narrow corridors
    // (bg2f_1), the bias pushes the player OUT of the corridor. In classrooms,
    // it interferes with short drives. The corridor-level steering + recovery
    // system handles wall-stuck better without active avoidance.
    // The code remains for potential re-enabling with better narrow-space logic.
    if (false && s_walkmesh.valid) {
        uint16_t nowTri2 = 0xFFFF;
        {
            uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base2)
                nowTri2 = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
        }
        if (nowTri2 != 0xFFFF && nowTri2 < (uint16_t)s_walkmesh.numTriangles) {
            const auto& tri = s_walkmesh.triangles[nowTri2];
            static const float WALL_BIAS_DIST = 40.0f;   // activate when within this distance
            static const float WALL_BIAS_STRENGTH = 0.25f; // blend factor (0=no bias, 1=full perpendicular)
            // v06.19: Check if corridor is narrow (walls on multiple edges).
            // If so, reduce bias to avoid ping-ponging between walls.
            int wallEdgeCount = 0;
            for (int ec = 0; ec < 3; ec++)
                if (tri.neighbor[ec] == 0xFFFF) wallEdgeCount++;
            float effectiveStrength = WALL_BIAS_STRENGTH;
            if (wallEdgeCount >= 2) effectiveStrength *= 0.3f; // very narrow, minimal bias
            for (int e = 0; e < 3; e++) {
                if (tri.neighbor[e] != 0xFFFF) continue; // not a wall edge
                // Wall edge: vertices (e+1)%3 and (e+2)%3
                int wvi1 = tri.vertexIdx[(e + 1) % 3];
                int wvi2 = tri.vertexIdx[(e + 2) % 3];
                if (wvi1 >= s_walkmesh.numVertices || wvi2 >= s_walkmesh.numVertices) continue;
                float wx1 = (float)s_walkmesh.vertices[wvi1].x;
                float wy1 = (float)s_walkmesh.vertices[wvi1].y;
                float wx2 = (float)s_walkmesh.vertices[wvi2].x;
                float wy2 = (float)s_walkmesh.vertices[wvi2].y;
                // Distance from player to this edge (point-to-line-segment).
                float edx = wx2 - wx1, edy = wy2 - wy1;
                float edLenSq = edx*edx + edy*edy;
                if (edLenSq < 1.0f) continue;
                float t = ((px - wx1)*edx + (pz - wy1)*edy) / edLenSq;
                if (t < 0) t = 0; if (t > 1) t = 1;
                float closestX = wx1 + t * edx;
                float closestY = wy1 + t * edy;
                float wallDx = px - closestX;
                float wallDy = pz - closestY;
                float wallDist = sqrtf(wallDx*wallDx + wallDy*wallDy);
                if (wallDist < WALL_BIAS_DIST && wallDist > 0.1f) {
                    // Blend steering away from wall. Stronger when closer.
                    float factor = effectiveStrength * (1.0f - wallDist / WALL_BIAS_DIST);
                    float awayX = wallDx / wallDist; // unit vector away from wall
                    float awayY = wallDy / wallDist;
                    float steerMag = sqrtf(dx*dx + dz*dz);
                    dx = dx * (1.0f - factor) + awayX * factor * steerMag;
                    dz = dz * (1.0f - factor) + awayY * factor * steerMag;
                }
            }
        }
    }

    // v06.17: Trigger-line proximity check.
    // Per-tick: if the current steering direction would carry the player across
    // a non-target trigger line within the next ~200 units, redirect steering
    // to be parallel to the trigger line instead of crossing it.
    // Skip this check for trigger lines that the A* path legitimately crosses
    // (the target trigger line, exempted via s_driveSkipTrigIdx).
    // Also skip for NPC targets where the NPC is on the other side of a trigger
    // line — A* already routed through it, so crossing is intentional.
    if (s_capturedLineCount > 0) {
        float steerLen = sqrtf(dx*dx + dz*dz);
        if (steerLen > 1.0f) {
            float projDist = 200.0f;
            float projX = px + (dx / steerLen) * projDist;
            float projY = pz + (dz / steerLen) * projDist;
            for (int t = 0; t < s_capturedLineCount; t++) {
                if (!s_capturedLines[t].active) continue;
                if (t == s_driveSkipTrigIdx) continue;
                float lx1 = (float)s_capturedLines[t].x1;
                float ly1 = (float)s_capturedLines[t].y1;
                float lx2 = (float)s_capturedLines[t].x2;
                float ly2 = (float)s_capturedLines[t].y2;
                float ldx = lx2 - lx1, ldy = ly2 - ly1;
                // v06.18: Skip trigger lines where the target is on the other side.
                // If the target is across this trigger line from the player, A*
                // already planned to cross it, so we must allow the crossing.
                float crossPlayer = ldx * (pz - ly1) - ldy * (px - lx1);
                float crossTarget = ldx * (tz - ly1) - ldy * (tx - lx1);
                if (crossPlayer * crossTarget < -1.0f) continue; // target is across, allow crossing
                float crossProj = ldx * (projY - ly1) - ldy * (projX - lx1);
                if (crossPlayer * crossProj < -1.0f) {
                    // Projected endpoint crosses trigger line — redirect parallel.
                    float trigLen = sqrtf(ldx*ldx + ldy*ldy);
                    if (trigLen > 0.001f) {
                        float trigNx = ldx / trigLen, trigNy = ldy / trigLen;
                        float dot = dx * trigNx + dz * trigNy;
                        dx = trigNx * dot;
                        dz = trigNy * dot;
                    }
                    break;
                }
            }
        }
    }

    // v05.62: Max drive time safety cutoff.
    s_driveTotalTicks++;
    if (s_driveTotalTicks >= DRIVE_MAX_TICKS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Gave up. Distance remaining: %.0f.", dist);
        StopAutoDrive(msg);
        return;
    }

    // v05.65: Periodic position log every ~2s with waypoint progress.
    // v05.86: Also log analog lX/lY and movement delta to verify analog steering.
    s_driveLogTimer++;
    if (s_driveLogTimer >= 120) {
        s_driveLogTimer = 0;
        // Compute actual movement delta from last logged position.
        static float s_lastLogPX = 0, s_lastLogPZ = 0;
        float moveDx = px - s_lastLogPX;
        float moveDz = pz - s_lastLogPZ;
        float moveDist = sqrtf(moveDx*moveDx + moveDz*moveDz);
        // Compute angle between analog vector and actual movement.
        // If analog is working, these should be similar.
        float analogAngle = atan2f((float)s_analogDesiredLX, (float)-s_analogDesiredLY) * (180.0f / (float)NAV_PI);
        float moveAngle = (moveDist > 5.0f) ? atan2f(moveDx, -moveDz) * (180.0f / (float)NAV_PI) : 0.0f;
        Log::Write("FieldNavigation: [drive] tick=%d dist=%.0f player=(%.0f,%.0f) "
                   "steer=(%.0f,%.0f) wp=%d/%d kb=%s%s%s%s "
                   "lX=%d lY=%d analogAng=%.0f moveAng=%.0f moveDist=%.0f",
                   s_driveTotalTicks, dist, px, pz,
                   steerX, steerY, s_waypointIdx, s_waypointCount,
                   (s_driveHeld & DIR_UP) ? "U" : "", (s_driveHeld & DIR_DOWN) ? "D" : "",
                   (s_driveHeld & DIR_LEFT) ? "L" : "", (s_driveHeld & DIR_RIGHT) ? "R" : "",
                   (int)s_analogDesiredLX, (int)s_analogDesiredLY,
                   analogAngle, moveAngle, moveDist);
        s_lastLogPX = px;
        s_lastLogPZ = pz;

        // v06.08: NavLog periodic sample
        {
            uint16_t sampTri = 0xFFFF;
            uint8_t* base3 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base3)
                sampTri = *(uint16_t*)(base3 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            NavLog::DriveSample(px, pz, (int)sampTri, dist, s_waypointIdx, s_waypointCount, s_driveTotalTicks);
        }
    }

    // v05.90: Velocity-based stuck detection.
    // Every DRIVE_STUCK_THRESH ticks, check if the player has moved at least
    // DRIVE_STUCK_MIN_DIST world units from the position recorded at the
    // start of the window. This catches both triId-stuck (player on one
    // triangle) and oscillation (bouncing between two triangles without
    // making progress). Much more responsive than the old triId-only check.
    s_driveStuckTicks++;
    // v06.08: Grace period — don't check for stuck until the player has had
    // time to start moving. The game needs ~60 ticks to engage movement after
    // we install the fake gamepad and start injecting keys.
    if (s_driveStuckTicks >= DRIVE_STUCK_THRESH && s_driveTotalTicks >= 60) {
        float sdx = px - s_driveStuckPosX;
        float sdy = pz - s_driveStuckPosY;
        float stuckDist = sqrtf(sdx*sdx + sdy*sdy);
        if (stuckDist < DRIVE_STUCK_MIN_DIST) {
            // Player hasn't moved enough — trigger recovery.
            // (stuckTicks stays >= thresh so the recovery block below fires)
        } else {
            // v06.10: Player moved, but check if they're making progress
            // toward the target. Micro-oscillation (e.g. bggate_2 tri 126<->127)
            // produces enough movement to pass the velocity check but zero
            // progress toward the target.
            if (s_driveProgressDist > 1e29f) {
                // First window — seed the progress baseline.
                s_driveProgressDist = dist;
                s_driveNoProgressCount = 0;
            } else {
                float closed = s_driveProgressDist - dist;  // positive = closer
                if (closed < DRIVE_PROGRESS_MIN) {
                    s_driveNoProgressCount++;
                    if (s_driveNoProgressCount >= DRIVE_NO_PROGRESS_MAX) {
                        // Not making progress toward target — force stuck recovery.
                        Log::Write("FieldNavigation: [drive] no-progress stuck: "
                                   "dist=%.0f progressBaseline=%.0f closed=%.0f "
                                   "noProgressCount=%d — forcing recovery",
                                   dist, s_driveProgressDist, closed,
                                   s_driveNoProgressCount);
                        // Leave stuckTicks >= thresh so recovery block fires.
                        s_driveProgressDist = dist;
                        s_driveNoProgressCount = 0;
                        // Don't reset stuckTicks — fall through to recovery.
                        s_driveStuckPosX = px;
                        s_driveStuckPosY = pz;
                        goto stuck_check_done;  // skip the normal reset
                    }
                } else {
                    // Genuine progress — reset the no-progress counter.
                    s_driveNoProgressCount = 0;
                }
                s_driveProgressDist = dist;
            }
            // Player is making progress — reset the window.
            s_driveStuckTicks = 0;
            s_driveWiggleTicks = 0;
        }
        // Always update the reference position at window boundary.
        s_driveStuckPosX = px;
        s_driveStuckPosY = pz;
    }
    stuck_check_done:

    // v05.75: Heading computation. Map world-space delta to arrow keys.
    // Log analysis confirms: pressing UP moves player in +Y world direction.
    // For X axis: pressing RIGHT moves player in +X world direction (v05.74
    // confirmed back-to-front auto-drive worked with direct X mapping).
    // Y axis is inverted (UP=+Y but -Y=screen-up), X axis is NOT inverted.
    uint8_t heading = 0;
    if (dz >  DRIVE_AXIS_THRESH) heading |= DIR_UP;    // +Y world = press UP
    if (dz < -DRIVE_AXIS_THRESH) heading |= DIR_DOWN;  // -Y world = press DOWN
    if (dx >  DRIVE_AXIS_THRESH) heading |= DIR_RIGHT; // +X world = press RIGHT
    if (dx < -DRIVE_AXIS_THRESH) heading |= DIR_LEFT;  // -X world = press LEFT
    if (heading == 0) heading = DIR_UP;  // fallback: shouldn't happen (dist > arrive)

    // v05.83: Activate analog override and set direction from the computed vector.
    // This gives us true 360-degree steering via the gamepad analog path.
    // The keyboard injection (SetHeldDirections) is kept as a fallback
    // in case the analog path isn't read by the game engine.
    s_analogOverrideActive = true;
    SetAnalogFromVector(dx, dz);

    if (s_driveWiggleTicks > 0) {
        // v05.68/85: Wall recovery using analog steering.
        // Convert the 8-dir bitmask to a vector for analog injection.
        float wdx = 0, wdy = 0;
        if (s_driveWiggleDir & DIR_RIGHT) wdx += 1.0f;
        if (s_driveWiggleDir & DIR_LEFT)  wdx -= 1.0f;
        if (s_driveWiggleDir & DIR_UP)    wdy += 1.0f;
        if (s_driveWiggleDir & DIR_DOWN)  wdy -= 1.0f;
        // v06.05: Abort wiggle if the direction would now cross a trigger line
        // (player may have drifted during the wiggle).
        if (s_capturedLineCount > 0 &&
            WouldCrossTriggerLine(px, pz, wdx * 1000.0f, wdy * 1000.0f, s_driveSkipTrigIdx)) {
            s_driveWiggleTicks = 0;  // abort this wiggle
            Log::Write("FieldNavigation: [drive] wiggle aborted — would cross trigger line");
        } else {
            // Scale to a large magnitude so SetAnalogFromVector normalizes it.
            SetAnalogFromVector(wdx * 1000.0f, wdy * 1000.0f);
            SetHeldDirections(s_driveWiggleDir);  // kept for bitmask tracking/logging only
            s_driveWiggleTicks--;
        }
        // v05.80: When recovery finishes and we moved to a new triangle,
        // recompute A* from current position. This prevents the cascading
        // failure where recovery flings the player far away and the old
        // waypoints become unreachable.
        if (s_driveWiggleTicks == 0 && s_walkmesh.valid && s_waypointCount > 0) {
            uint16_t nowTri = 0xFFFF;
            {
                uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                if (base2)
                    nowTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            }
            if (nowTri != 0xFFFF && nowTri < (uint16_t)s_walkmesh.numTriangles) {
                // Find goal triangle from target position
                float rpTx = 0, rpTz = 0;
                bool rpGot = false;
                if (catTarget.type == ENT_EXIT && catTarget.gatewayIdx >= 0 && catTarget.gatewayIdx < s_gatewayCount) {
                    rpTx = s_gateways[catTarget.gatewayIdx].centerX;
                    rpTz = s_gateways[catTarget.gatewayIdx].centerZ;
                    rpGot = true;
                } else if (ei <= -200) {
                    int trigIdx2 = -(ei + 200);
                    if (trigIdx2 >= 0 && trigIdx2 < s_capturedLineCount) {
                        rpTx = (float)(s_capturedLines[trigIdx2].x1 + s_capturedLines[trigIdx2].x2) / 2.0f;
                        rpTz = (float)(s_capturedLines[trigIdx2].y1 + s_capturedLines[trigIdx2].y2) / 2.0f;
                        rpGot = true;
                    }
                } else if (ei >= 0) {
                    rpGot = GetEntityPos(ei, rpTx, rpTz);
                }
                if (rpGot) {
                    int rpGoal = FindNearestTriangle(rpTx, rpTz);
                    if (rpGoal >= 0 && (int)nowTri != rpGoal) {
                        int oldWp = s_waypointIdx;
                        int oldTotal = s_waypointCount;
                        // v06.02: Exempt target trigger line from A* avoidance during re-path.
                        // v06.04: Also exempt for event triggers (not just exits).
                        int rpSkipTrig = -1;
                        if (catTarget.entityIdx <= -200) {
                            rpSkipTrig = -(catTarget.entityIdx + 200);
                        }
                        // v06.04: Save old waypoints before A* overwrites them.
                        // If re-path fails (player on disconnected island), we
                        // restore the old waypoints so the drive can keep trying.
                        float savedWp[MAX_WAYPOINTS][2];
                        int savedWpCount = s_waypointCount;
                        int savedWpIdx = s_waypointIdx;
                        bool savedFunnel = s_usingFunnel;
                        memcpy(savedWp, s_waypoints, sizeof(float) * 2 * savedWpCount);
                        if (ComputeAStarPath((int)nowTri, rpGoal, ei, rpSkipTrig)) {
                            // v05.94: Funnel re-enabled after FindPortal fix.
                            FunnelPath(px, pz, rpTx, rpTz);
                            s_wpMinDist = 1e30f;  // v06.08
                            Log::Write("FieldNavigation: [drive] re-pathed after recovery: %d wp (was wp %d/%d)",
                                       s_waypointCount, oldWp, oldTotal);
                        } else {
                            // v06.04: Re-path failed — restore old waypoints.
                            memcpy(s_waypoints, savedWp, sizeof(float) * 2 * savedWpCount);
                            s_waypointCount = savedWpCount;
                            s_waypointIdx = savedWpIdx;
                            s_usingFunnel = savedFunnel;
                            Log::Write("FieldNavigation: [drive] re-path FAILED from tri %d — restored old %d wp",
                                       (int)nowTri, savedWpCount);
                        }
                    }
                }
            }
        }
    } else if (s_driveStuckTicks >= DRIVE_STUCK_THRESH) {
        // v06.16: Simplified recovery system.
        // No more odd/even phase alternation. Simple cycle:
        //   Odd phases:  re-run A* from current position → funnel path
        //   Even phases: single perpendicular nudge to break wall contact
        // After nudge completes, the wiggle-completion code above re-paths via funnel.
        s_driveStuckTicks = 0;
        s_driveWigglePhase++;

        // Auto-cancel after too many recovery phases without progress.
        if (s_driveWigglePhase > MAX_RECOVERY_PHASES) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Stuck. Distance remaining: %.0f.", dist);
            StopAutoDrive(msg);
            return;
        }

        // NavLog recovery event
        {
            uint16_t recTri = 0xFFFF;
            uint8_t* base4 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base4)
                recTri = *(uint16_t*)(base4 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            NavLog::DriveRecovery(s_driveWigglePhase, (int)recTri, px, pz, dist);
        }

        // v06.21: Expand talk radius when stuck near NPC target.
        // If recovery is firing and we're close to the target, expand the
        // game's talk radius so the player can interact from further away.
        // This is the "meet in the middle" strategy.
        if (!s_driveTalkRadExpanded && s_driveTargetEntityIdx >= 0 &&
            s_driveOrigTalkRadius > 0 && dist < TALK_RAD_EXPAND_DIST) {
            float expanded = (float)s_driveOrigTalkRadius * TALK_RAD_EXPAND_FACTOR;
            if (expanded > TALK_RAD_EXPAND_MAX) expanded = TALK_RAD_EXPAND_MAX;
            uint16_t newRad = (uint16_t)expanded;
            if (newRad > s_driveOrigTalkRadius) {
                SetEntityTalkRadius(s_driveTargetEntityIdx, newRad);
                s_driveTalkRadExpanded = true;
                // Also expand our arriveDist to match.
                s_driveArriveDist = expanded;
                Log::Write("FieldNavigation: [drive] expanded talkRadius %u -> %u for ent%d (dist=%.0f)",
                           (unsigned)s_driveOrigTalkRadius, (unsigned)newRad,
                           s_driveTargetEntityIdx, dist);
            }
        }

        if (s_walkmesh.valid) {
            uint16_t nowTri = 0xFFFF;
            {
                uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                if (base2)
                    nowTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
            }
            if (nowTri != 0xFFFF && nowTri < (uint16_t)s_walkmesh.numTriangles) {
                float rpTx = tx, rpTz = tz;
                int rpGoal = FindNearestTriangle(rpTx, rpTz);

                if ((s_driveWigglePhase % 2) == 0) {
                    // Even phase: perpendicular nudge to break wall contact.
                    // Compute nudge perpendicular to the shared edge between
                    // current triangle and the next corridor triangle.
                    bool nudged = false;
                    if (s_corridorCount >= 2) {
                        int corridorPos = -1;
                        for (int ci = 0; ci < s_corridorCount; ci++) {
                            if (s_corridor[ci] == nowTri) { corridorPos = ci; break; }
                        }
                        int neighborCorridorIdx = -1;
                        if (corridorPos >= 0 && corridorPos + 1 < s_corridorCount)
                            neighborCorridorIdx = corridorPos + 1;
                        else if (corridorPos > 0)
                            neighborCorridorIdx = corridorPos - 1;

                        if (neighborCorridorIdx >= 0) {
                            uint16_t nextTri = s_corridor[neighborCorridorIdx];
                            const auto& tCur = s_walkmesh.triangles[nowTri];
                            int sharedEdge = -1;
                            for (int e = 0; e < 3; e++) {
                                if (tCur.neighbor[e] == nextTri) { sharedEdge = e; break; }
                            }
                            if (sharedEdge >= 0) {
                                int vi1 = tCur.vertexIdx[(sharedEdge + 1) % 3];
                                int vi2 = tCur.vertexIdx[(sharedEdge + 2) % 3];
                                if (vi1 < s_walkmesh.numVertices && vi2 < s_walkmesh.numVertices) {
                                    float ex1 = (float)s_walkmesh.vertices[vi1].x;
                                    float ey1 = (float)s_walkmesh.vertices[vi1].y;
                                    float ex2 = (float)s_walkmesh.vertices[vi2].x;
                                    float ey2 = (float)s_walkmesh.vertices[vi2].y;
                                    float edx = ex2 - ex1;
                                    float edy = ey2 - ey1;
                                    float edLen = sqrtf(edx*edx + edy*edy);
                                    if (edLen > 0.001f) {
                                        float perp1x = -edy / edLen, perp1y =  edx / edLen;
                                        float perp2x =  edy / edLen, perp2y = -edx / edLen;
                                        float nextCX = s_walkmesh.triangles[nextTri].centerX;
                                        float nextCY = s_walkmesh.triangles[nextTri].centerY;
                                        float toNextX = nextCX - px, toNextY = nextCY - pz;
                                        float dot1 = perp1x * toNextX + perp1y * toNextY;
                                        float dot2 = perp2x * toNextX + perp2y * toNextY;
                                        float ndx = (dot1 >= dot2) ? perp1x : perp2x;
                                        float ndy = (dot1 >= dot2) ? perp1y : perp2y;
                                        float altdx = (dot1 >= dot2) ? perp2x : perp1x;
                                        float altdy = (dot1 >= dot2) ? perp2y : perp1y;
                                        bool crossesTrig = (s_capturedLineCount > 0 &&
                                            WouldCrossTriggerLine(px, pz, ndx * 100.0f, ndy * 100.0f, s_driveSkipTrigIdx));
                                        if (crossesTrig) {
                                            ndx = altdx; ndy = altdy;
                                            crossesTrig = (s_capturedLineCount > 0 &&
                                                WouldCrossTriggerLine(px, pz, ndx * 100.0f, ndy * 100.0f, s_driveSkipTrigIdx));
                                        }
                                        if (!crossesTrig) {
                                            s_driveWiggleTicks = NUDGE_TICKS;
                                            uint8_t nudgeDir = 0;
                                            if (ndy >  0.3f) nudgeDir |= DIR_UP;
                                            if (ndy < -0.3f) nudgeDir |= DIR_DOWN;
                                            if (ndx >  0.3f) nudgeDir |= DIR_RIGHT;
                                            if (ndx < -0.3f) nudgeDir |= DIR_LEFT;
                                            if (nudgeDir == 0) nudgeDir = DIR_UP;
                                            s_driveWiggleDir = nudgeDir;
                                            SetAnalogFromVector(ndx * 1000.0f, ndy * 1000.0f);
                                            SetHeldDirections(nudgeDir);
                                            nudged = true;
                                            Log::Write("FieldNavigation: [drive] recovery %d — nudge perpendicular "
                                                       "tri %d->%d dir=(%.2f,%.2f) %d ticks",
                                                       s_driveWigglePhase, (int)nowTri, (int)nextTri,
                                                       ndx, ndy, NUDGE_TICKS);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (!nudged) {
                        // Couldn't compute nudge — fall back to re-path.
                        Log::Write("FieldNavigation: [drive] recovery %d — nudge failed, falling back to re-path",
                                   s_driveWigglePhase);
                        if (rpGoal >= 0 && (int)nowTri != rpGoal) {
                            float savedWp[MAX_WAYPOINTS][2];
                            int savedWpCount = s_waypointCount;
                            int savedWpIdx = s_waypointIdx;
                            bool savedFunnel = s_usingFunnel;
                            memcpy(savedWp, s_waypoints, sizeof(float) * 2 * savedWpCount);
                            if (ComputeAStarPath((int)nowTri, rpGoal, ei, s_driveSkipTrigIdx)) {
                                FunnelPath(px, pz, rpTx, rpTz);
                                s_wpMinDist = 1e30f;
                                Log::Write("FieldNavigation: [drive] recovery %d — re-pathed (nudge fallback): %d wp",
                                           s_driveWigglePhase, s_waypointCount);
                            } else {
                                memcpy(s_waypoints, savedWp, sizeof(float) * 2 * savedWpCount);
                                s_waypointCount = savedWpCount;
                                s_waypointIdx = savedWpIdx;
                                s_usingFunnel = savedFunnel;
                            }
                        }
                    }
                } else {
                    // Odd phase: re-run A* and generate fresh funnel path.
                    if (rpGoal >= 0 && (int)nowTri != rpGoal) {
                        float savedWp[MAX_WAYPOINTS][2];
                        int savedWpCount = s_waypointCount;
                        int savedWpIdx = s_waypointIdx;
                        bool savedFunnel = s_usingFunnel;
                        memcpy(savedWp, s_waypoints, sizeof(float) * 2 * savedWpCount);

                        if (ComputeAStarPath((int)nowTri, rpGoal, ei, s_driveSkipTrigIdx)) {
                            FunnelPath(px, pz, rpTx, rpTz);
                            s_wpMinDist = 1e30f;
                            Log::Write("FieldNavigation: [drive] recovery %d — re-pathed: %d wp from tri %d",
                                       s_driveWigglePhase, s_waypointCount, (int)nowTri);
                        } else {
                            memcpy(s_waypoints, savedWp, sizeof(float) * 2 * savedWpCount);
                            s_waypointCount = savedWpCount;
                            s_waypointIdx = savedWpIdx;
                            s_usingFunnel = savedFunnel;
                            Log::Write("FieldNavigation: [drive] recovery %d — A* failed from tri %d, keeping old %d wp",
                                       s_driveWigglePhase, (int)nowTri, savedWpCount);
                        }
                    } else {
                        Log::Write("FieldNavigation: [drive] recovery %d — already on goal tri or no goal",
                                   s_driveWigglePhase);
                    }
                }
            }
        }
        // Reset stuck position for fresh window.
        s_driveStuckPosX = px;
        s_driveStuckPosY = pz;
        if (s_driveWiggleTicks == 0)
            SetHeldDirections(heading);
    } else {
        // Normal heading toward waypoint/target.
        SetHeldDirections(heading);
    }
}

// Called every Update() tick (16ms cadence, unthrottled).
// Keys: VK_OEM_MINUS (-) = previous, VK_OEM_PLUS (+/=) = next,
//       VK_BACK (Backspace) = repeat current (gated: only on field, not during FMV).
static void HandleKeys()
{
    // Only handle nav keys when on the field.
    if (!FF8Addresses::IsOnField()) return;

    bool minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus  = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    // Backspace for nav only when no FMV is playing (FmvSkip owns it during FMVs).
    bool bksp  = !FF8Addresses::IsMoviePlaying() &&
                 (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    // \ (backslash) toggles auto-drive to selected entity.
    bool drive = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;
    // v05.69: F11 = VISDIAG dump
    bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;

    if (f11 && !s_f11WasDown) {
        // Dump candidate visibility bytes for all model-bearing entities.
        __try {
            uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
            uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base && entCount > 0) {
                uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;
                Log::Write("FieldNavigation: [VISDIAG] === Visibility flag dump ===");
                for (int i = 0; i < (int)lim; i++) {
                    uint8_t* blk = base + ENTITY_STRIDE * i;
                    int16_t  mdl = *(int16_t*)(blk + 0x218);
                    // Dump a wide range of candidate offsets as hex.
                    // Flags area: 0x240-0x25F covers talk/push/through/setpc
                    // and likely the SHOW/HIDE visibility flag.
                    // Also dump 0x00-0x0F (early control bytes) and 0x160-0x16F (exec flags).
                    // Dump regions covering the gaps in ff8_field_state_other:
                    // gap1 (0x188-0x1F9): 114 bytes after ff8_field_state_common
                    // gap2 (0x21A-0x248): 47 bytes after model_id
                    // flags area (0x240-0x25F): talkon/pushon/throughon/setpc
                    char hexGap1a[80] = {}; // 0x188..0x19F (24 bytes, needs 24*3+1=73)
                    char hexGap1b[80] = {}; // 0x1A0..0x1B7 (24 bytes)
                    char hexGap2[80] = {};  // 0x21A..0x231 (24 bytes)
                    char hexFlags[80] = {}; // 0x240..0x257 (24 bytes)
                    for (int b = 0; b < 24; b++)
                        snprintf(hexGap1a + b*3, 4, "%02X ", blk[0x188 + b]);
                    for (int b = 0; b < 24; b++)
                        snprintf(hexGap1b + b*3, 4, "%02X ", blk[0x1A0 + b]);
                    for (int b = 0; b < 24; b++)
                        snprintf(hexGap2 + b*3, 4, "%02X ", blk[0x21A + b]);
                    for (int b = 0; b < 24; b++)
                        snprintf(hexFlags + b*3, 4, "%02X ", blk[0x240 + b]);
                    Log::Write("FieldNavigation: [VISDIAG] ent%d model=%d %s",
                               i, (int)mdl, (i == s_playerEntityIdx) ? "[PLAYER]" : "");
                    Log::Write("FieldNavigation: [VISDIAG]   @0x188: %s", hexGap1a);
                    Log::Write("FieldNavigation: [VISDIAG]   @0x1A0: %s", hexGap1b);
                    Log::Write("FieldNavigation: [VISDIAG]   @0x21A: %s", hexGap2);
                    Log::Write("FieldNavigation: [VISDIAG]   @0x240: %s", hexFlags);
                }
                Log::Write("FieldNavigation: [VISDIAG] === End dump ===");
                ScreenReader::Speak("Visibility diagnostic logged.");
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("FieldNavigation: [VISDIAG] Exception");
        }
    }
    s_f11WasDown = f11;

    // v05.86: Arrow keys cancel auto-drive immediately.
    // The player pressing any direction key means they want manual control.
    // We must only cancel if the arrow key is NOT one we're currently injecting
    // via SetHeldDirections. Check which keys are held by us (s_driveHeld bitmask)
    // and only cancel on keys we're NOT injecting.
    if (s_driveActive) {
        bool arrowUp    = (GetAsyncKeyState(VK_UP)    & 0x8000) != 0;
        bool arrowDown  = (GetAsyncKeyState(VK_DOWN)  & 0x8000) != 0;
        bool arrowLeft  = (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0;
        bool arrowRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
        // v06.08: During recovery (stuck phases), any arrow key cancels immediately.
        // This ensures the player can always regain control when the drive is stuck.
        // During normal steering, mask out keys we're injecting ourselves.
        // v06.14: Arrow-key cancel REMOVED during recovery.
        // JAWS intercepts arrow keys in fullscreen DirectX, causing false
        // cancels at phase >= 4. The user can cancel via backslash toggle.
        // During normal steering (phase 0), still cancel on non-injected arrows
        // to allow manual override before recovery starts.
        if (s_driveWigglePhase == 0) {
            bool playerUp    = arrowUp    && !(s_driveHeld & DIR_UP);
            bool playerDown  = arrowDown  && !(s_driveHeld & DIR_DOWN);
            bool playerLeft  = arrowLeft  && !(s_driveHeld & DIR_LEFT);
            bool playerRight = arrowRight && !(s_driveHeld & DIR_RIGHT);
            if (playerUp || playerDown || playerLeft || playerRight) {
                StopAutoDrive("Cancelled.");
            }
        }
    }

    if (minus && !s_minusWasDown) { RefreshCatalog(); if (s_driveActive) StopAutoDrive("Cancelled."); CycleEntity(-1); }
    if (plus  && !s_plusWasDown)  { RefreshCatalog(); if (s_driveActive) StopAutoDrive("Cancelled."); CycleEntity(+1); }
    if (bksp  && !s_bkspWasDown)  AnnounceCurrentTarget(); // live refresh
    if (drive && !s_driveWasDown) {
        if (s_driveActive) {
            StopAutoDrive("Cancelled.");
        } else if (FieldDialog::IsDialogOpen()) {
            ScreenReader::Speak("Auto-drive unavailable: dialog is open.");
        } else {
            // Validate we have a usable target before starting.
            const EntityInfo& drTgt = (s_selectedCatalogIdx < s_catalogCount)
                                      ? s_catalog[s_selectedCatalogIdx] : s_catalog[0];
            {
            float _px = 0, _pz = 0, _tx = 0, _tz = 0;
            bool drValid = false;
            if (drTgt.entityIdx != s_playerEntityIdx &&
                GetEntityPos(s_playerEntityIdx, _px, _pz)) {
                if (drTgt.type == ENT_EXIT && drTgt.gatewayIdx >= 0 && drTgt.gatewayIdx < s_gatewayCount) {
                    _tx = s_gateways[drTgt.gatewayIdx].centerX;
                    _tz = s_gateways[drTgt.gatewayIdx].centerZ;
                    drValid = true;
                } else if (drTgt.entityIdx <= -200) {
                    int trigIdx = -(drTgt.entityIdx + 200);
                    if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
                        _tx = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
                        _tz = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
                        drValid = true;
                    }
                } else if (drTgt.entityIdx >= 0 && GetEntityPos(drTgt.entityIdx, _tx, _tz)) {
                    drValid = true;
                }
            }
            if (drValid) {
                // Seed the triId from the player's current triangle so the
                // first tick doesn't see a "change" from the uninitialized value.
                uint16_t seedTri = 0xFFFF;
                {
                    uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    if (base)
                        seedTri = *(uint16_t*)(base + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
                }
                s_driveActive      = true;
                s_driveLastTriId   = seedTri;
                s_driveStuckTicks  = 0;
                s_driveWiggleTicks = 0;
                s_driveWiggleDir   = 0;
                s_driveWigglePhase = 0;  // v05.68: reset recovery rotation
                s_driveTotalTicks  = 0;
                s_driveLogTimer    = 0;
                s_driveStuckPosX   = _px;  // v05.90: velocity-based stuck detection
                s_driveStuckPosY   = _pz;
                s_driveProgressDist    = 1e30f;  // v06.10: reset progress tracking
                s_driveNoProgressCount = 0;

                // v06.14: Start heading calibration if not yet calibrated for this field.
                if (s_calibPending && !s_camCalibrated) {
                    s_calibPhase = 1;
                    s_calibTicks = 0;
                    Log::Write("FieldNavigation: [CALIB] starting heading calibration for field '%s'",
                               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?");
                } else {
                    s_calibPhase = 3;  // skip calibration, use existing axes
                }

                // v05.84: Install fake gamepad so the game processes our analog values.
                if (FF8Addresses::HasDinputGamepadPtrs() && !s_fakeGamepadInstalled) {
                    s_savedDevicePtr = *FF8Addresses::pDinputGamepadDevicePtr;
                    s_savedStatePtr  = *FF8Addresses::pDinputGamepadStatePtr;
                    // Zero the fake DIJOYSTATE2 (centered, no buttons).
                    memset(s_fakeDIJOYSTATE2, 0, sizeof(s_fakeDIJOYSTATE2));
                    // Point the game's state pointer at our fake struct.
                    *FF8Addresses::pDinputGamepadStatePtr = (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2;
                    // Set device to non-null sentinel so game thinks gamepad exists.
                    *FF8Addresses::pDinputGamepadDevicePtr = FAKE_DEVICE_SENTINEL;
                    s_fakeGamepadInstalled = true;
                    Log::Write("FieldNavigation: [drive] fake gamepad installed: device=0x%08X state=0x%08X (saved dev=0x%08X state=0x%08X)",
                               FAKE_DEVICE_SENTINEL, (uint32_t)(uintptr_t)s_fakeDIJOYSTATE2,
                               s_savedDevicePtr, s_savedStatePtr);
                }

                // v05.80: Set per-drive arrive distance from entity talk radius.
                // For NPC entities, use talkRadius - 20 so we stop INSIDE the
                // interaction zone, allowing the player to press X to talk.
                // Minimum 30 to avoid overshoot into the NPC's collision body.
                // For gateways/triggers, use the default 300.
                // v05.90: Arrive distance strategy:
                // Drive aims to reach talkRadius distance from the NPC.
                // This is the distance at which the player can press X to talk.
                // We use talkRadius directly (not talkRadius-20) to give more
                // margin for collision bodies and narrow approaches.
                // For non-entity targets (gateways, triggers), use the default.
                s_driveArriveDist = DRIVE_ARRIVE_DIST_DEFAULT;
                s_driveTargetEntityIdx = -1;
                s_driveOrigTalkRadius = 0;
                s_driveTalkRadExpanded = false;
                if (drTgt.entityIdx >= 0 && drTgt.entityIdx < MAX_ENTITIES) {
                    uint16_t talkRad = GetEntityTalkRadius(drTgt.entityIdx);
                    if (talkRad > 0) {
                        s_driveArriveDist = (float)talkRad;
                        if (s_driveArriveDist < 60.0f) s_driveArriveDist = 60.0f;
                        // v06.21: Save original talk radius for potential expansion.
                        s_driveTargetEntityIdx = drTgt.entityIdx;
                        s_driveOrigTalkRadius = talkRad;
                        Log::Write("FieldNavigation: [drive] talkRadius=%u -> arriveDist=%.0f",
                                   (unsigned)talkRad, s_driveArriveDist);
                    }
                }

                // v05.76: Track trigger line crossing for arrival detection.
                s_driveTrigTarget = false;
                s_driveTrigCrossStart = 0.0f;
                if (drTgt.entityIdx <= -200) {
                    int trigIdx = -(drTgt.entityIdx + 200);
                    if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
                        s_driveTrigTarget = true;
                        float tlx1 = (float)s_capturedLines[trigIdx].x1;
                        float tly1 = (float)s_capturedLines[trigIdx].y1;
                        float tlx2 = (float)s_capturedLines[trigIdx].x2;
                        float tly2 = (float)s_capturedLines[trigIdx].y2;
                        float tdx = tlx2 - tlx1;
                        float tdy = tly2 - tly1;
                        s_driveTrigCrossStart = tdx * (_pz - tly1) - tdy * (_px - tlx1);
                    }
                }

                // v05.93: Path computation — try line-of-sight first, fall back to A*.
                s_waypointCount = 0;
                s_waypointIdx   = 0;
                s_usingFunnel   = false;  // v05.95
                s_wpMinDist     = 1e30f;  // v06.08: reset overshoot tracker
                if (s_walkmesh.valid) {
                    int startTri = -1;
                    if (seedTri != 0xFFFF && seedTri < (uint16_t)s_walkmesh.numTriangles) {
                        startTri = (int)seedTri;
                    } else {
                        startTri = FindNearestTriangle(_px, _pz);
                    }
                    int goalTri  = FindNearestTriangle(_tx, _tz);

                    if (startTri >= 0 && goalTri >= 0) {
                        // v05.93: Check line-of-sight first. If the walkmesh has
                        // a clear, unobstructed path from player to target (no
                        // dead-end edges, no trigger line crossings), skip A*
                        // and just steer directly. This handles simple cases like
                        // open corridors and straight aisles perfectly.
                        // v06.01: Island connectivity check + A*+funnel pipeline.
                        // 47.5% of FF8 fields have disconnected walkmesh islands.
                        // If the target is on a different island, redirect to the
                        // nearest trigger line that bridges the gap.
                        if (!AreTrianglesConnected(startTri, goalTri)) {
                            // Target on different island. Find the nearest active
                            // trigger line (screen transition) and drive to it.
                            Log::Write("FieldNavigation: [drive] target on different walkmesh island "
                                       "(start tri %d, goal tri %d) — searching for bridge trigger",
                                       startTri, goalTri);
                            float bestTrigDist = 1e30f;
                            int bestTrigIdx = -1;
                            for (int tl = 0; tl < s_capturedLineCount; tl++) {
                                if (!s_capturedLines[tl].active) continue;
                                float tcx = (float)(s_capturedLines[tl].x1 + s_capturedLines[tl].x2) / 2.0f;
                                float tcy = (float)(s_capturedLines[tl].y1 + s_capturedLines[tl].y2) / 2.0f;
                                float tdx = tcx - _px;
                                float tdy = tcy - _pz;
                                float tdist = sqrtf(tdx*tdx + tdy*tdy);
                                // Must be reachable from player's island
                                int trigTri = FindNearestTriangle(tcx, tcy);
                                if (trigTri >= 0 && AreTrianglesConnected(startTri, trigTri)) {
                                    if (tdist < bestTrigDist) {
                                        bestTrigDist = tdist;
                                        bestTrigIdx = tl;
                                    }
                                }
                            }
                            if (bestTrigIdx >= 0) {
                                // Redirect to the trigger line center.
                                float bridgeX = (float)(s_capturedLines[bestTrigIdx].x1 + s_capturedLines[bestTrigIdx].x2) / 2.0f;
                                float bridgeY = (float)(s_capturedLines[bestTrigIdx].y1 + s_capturedLines[bestTrigIdx].y2) / 2.0f;
                                int bridgeTri = FindNearestTriangle(bridgeX, bridgeY);
                                Log::Write("FieldNavigation: [drive] redirecting to trigger line %d "
                                           "center=(%.0f,%.0f) tri=%d dist=%.0f",
                                           bestTrigIdx, bridgeX, bridgeY, bridgeTri, bestTrigDist);
                                // Set up trigger crossing detection.
                                s_driveTrigTarget = true;
                                float tlx1 = (float)s_capturedLines[bestTrigIdx].x1;
                                float tly1 = (float)s_capturedLines[bestTrigIdx].y1;
                                float tlx2 = (float)s_capturedLines[bestTrigIdx].x2;
                                float tly2 = (float)s_capturedLines[bestTrigIdx].y2;
                                float tdx2 = tlx2 - tlx1;
                                float tdy2 = tly2 - tly1;
                                s_driveTrigCrossStart = tdx2 * (_pz - tly1) - tdy2 * (_px - tlx1);
                                // Path to the trigger line.
                                // v06.02: Exempt the bridge trigger from A* avoidance.
                                if (bridgeTri >= 0 && ComputeAStarPath(startTri, bridgeTri, -1, bestTrigIdx)) {
                                    FunnelPath(_px, _pz, bridgeX, bridgeY);
                                }
                                _tx = bridgeX;
                                _tz = bridgeY;
                            } else {
                                Log::Write("FieldNavigation: [drive] no reachable trigger line found, using direct steering");
                                s_waypoints[0][0] = _tx;
                                s_waypoints[0][1] = _tz;
                                s_waypointCount = 1;
                                s_waypointIdx = 0;
                            }
                        } else {
                            // Same island — use A*+funnel.
                            // v06.02: When driving to a trigger line target, exempt
                            // that trigger line from A* avoidance so A* can path
                            // across it. Screen transitions and events are by definition
                            // on or near a trigger line.
                            // v06.04: Extended from ENT_EXIT only to all trigger targets.
                            int driveSkipTrigIdx = -1;
                            if (drTgt.entityIdx <= -200) {
                                driveSkipTrigIdx = -(drTgt.entityIdx + 200);
                                Log::Write("FieldNavigation: [drive] trigger target: exempting trigger line %d from A* avoidance",
                                           driveSkipTrigIdx);
                            }
                            s_driveSkipTrigIdx = driveSkipTrigIdx;  // v06.05: save for recovery
                            if (ComputeAStarPath(startTri, goalTri, drTgt.entityIdx, driveSkipTrigIdx)) {
                                FunnelPath(_px, _pz, _tx, _tz);
                                Log::Write("FieldNavigation: [drive] A*+funnel path: %d waypoints from tri %d to %d",
                                           s_waypointCount, startTri, goalTri);
                            }
                        }
                        // v05.97: Pre-skip waypoints we're already close to at drive start.
                        // The funnel may place the first waypoint near the player's current
                        // position. Without this, the tick-30 chain-advance delay causes
                        // the player to steer away from wp0 before skipping it, creating
                        // a circular orbit as the character tries to return to a passed waypoint.
                        if (s_waypointCount > 1 && s_usingFunnel) {
                            float wpSkipDist = FUNNEL_ARRIVE_DIST * 2.0f;  // generous initial skip
                            while (s_waypointIdx < s_waypointCount - 1) {
                                float wdx = s_waypoints[s_waypointIdx][0] - _px;
                                float wdy = s_waypoints[s_waypointIdx][1] - _pz;
                                float wd = sqrtf(wdx*wdx + wdy*wdy);
                                if (wd >= wpSkipDist) break;
                                Log::Write("FieldNavigation: [drive] pre-skip wp %d (dist=%.0f < %.0f)",
                                           s_waypointIdx, wd, wpSkipDist);
                                s_waypointIdx++;
                            }
                        }
                    } else {
                        Log::Write("FieldNavigation: [drive] A* skipped: start=%d goal=%d",
                                   startTri, goalTri);
                    }
                } else {
                    Log::Write("FieldNavigation: [drive] No walkmesh — straight-line mode");
                }

                // v06.08: Compute and store starting distance for NavLog
                {
                    float sdx = _tx - _px, sdz = _tz - _pz;
                    s_driveStartDist = sqrtf(sdx*sdx + sdz*sdz);
                }

                Log::Write("FieldNavigation: [drive] started toward ent%d gw%d waypoints=%d",
                           drTgt.entityIdx, drTgt.gatewayIdx, s_waypointCount);

                // v06.08: NavLog drive start
                {
                    const char* fld = FF8Addresses::pCurrentFieldName
                                      ? FF8Addresses::pCurrentFieldName : "?";
                    const char* tType = EntityTypeName(drTgt.type);
                    NavLog::DriveStart(fld, drTgt.name, tType,
                                       (int)seedTri, _px, _pz,
                                       -1, _tx, _tz, s_driveArriveDist,
                                       s_corridorCount, s_waypointCount, s_usingFunnel);
                }

                ScreenReader::Speak("Driving.");
            } else {
                ScreenReader::Speak("Target not yet located.");
            }
            }
        }
    }

    s_minusWasDown  = minus;
    s_plusWasDown   = plus;
    s_bkspWasDown   = bksp;
    s_driveWasDown  = drive;
}

// ============================================================================
// Hook: set_current_triangle_sub_45E160
// ============================================================================
//
// Called by the field engine whenever any entity moves to a new walkmesh
// triangle. The three arguments are DIRECT POINTERS to vertex structs (each
// int16_t[3] = x,y,z). We compute the triangle centre and store it for the
// entity whose triangle ID just changed.
//
// Threading note: runs on the game thread. s_entityCenters is read by the
// mod thread (Update/HandleKeys). On x86, 32-bit aligned float stores and
// bool stores are individually atomic, so the worst case is that Update()
// sees an old cx while cz is already updated — a cosmetically stale position
// for one 500ms polling cycle, which is acceptable for compass guidance.

static void __cdecl HookedSetCurrentTriangle(int a1, int a2, int a3)
{
    // Call original first — game behaviour unchanged.
    if (s_originalSetCurrentTriangle)
        s_originalSetCurrentTriangle(a1, a2, a3);

    // Read vertex coords from the three pointer arguments.
    int16_t x0=0,y0=0,z0=0, x1=0,y1=0,z1=0, x2=0,y2=0,z2=0;
    bool ok = ReadVertexCoords((uintptr_t)(unsigned)a1, x0, y0, z0)
           && ReadVertexCoords((uintptr_t)(unsigned)a2, x1, y1, z1)
           && ReadVertexCoords((uintptr_t)(unsigned)a3, x2, y2, z2);

    if (ok && FF8Addresses::HasFieldStateArrays()) {
        float cx = (x0 + x1 + x2) / 3.0f;
        // v05.61: Use Y (screen-vertical) instead of Z (depth) for triangle center.
        float cy = (y0 + y1 + y2) / 3.0f;

        // Identify which entity just moved by comparing triIds against our
        // shadow copy (s_hookPrevTri).  Accept only if exactly ONE entity
        // changed — that entity's new triId receives the spatial centre
        // computed from the vertex arguments.  If multiple entities appear
        // to have changed simultaneously (e.g. after a hookPrevTri reset or
        // a threading edge case) we update the shadows but do not store a
        // centre, since we cannot reliably attribute which triId the vertices
        // belong to.  The key insight: storing centre keyed by triId (not
        // entity index) means the lookup in GetEntityPos is always spatially
        // correct — a triangle has exactly one world position regardless of
        // which entity is standing on it.
        __try {
            uint8_t* base = reinterpret_cast<uint8_t*>(
                *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateOthers));
            uint8_t count = *FF8Addresses::pFieldStateOtherCount;
            if (base && count > 0) {
                uint8_t  lim         = (count < MAX_ENTITIES) ? count : (uint8_t)MAX_ENTITIES;
                int      changeCount = 0;
                int      changedIdx  = -1;
                uint16_t newTriId    = 0;
                for (int i = 0; i < (int)lim; i++) {
                    int16_t  model  = *(int16_t*)(base + ENTITY_STRIDE * i + 0x218);
                    if (model < 0) continue;
                    uint16_t curTri = *(uint16_t*)(base + ENTITY_STRIDE * i + 0x1FA);
                    if (curTri != s_hookPrevTri[i]) {
                        changeCount++;
                        changedIdx      = i;
                        newTriId        = curTri;
                        s_hookPrevTri[i] = curTri;  // always keep shadow current
                    }
                }
                if (changeCount == 1 && newTriId > 0 && newTriId < MAX_TRI_ID) {
                    s_triCenter[newTriId].cx    = cx;
                    s_triCenter[newTriId].cz    = cy;
                    s_triCenter[newTriId].valid = true;
                    // Also update legacy per-entity cache used by AnnounceCurrentTarget
                    // (refreshed from triCenter map in Update() every 500ms).
                    s_entityCenters[changedIdx].cx    = cx;
                    s_entityCenters[changedIdx].cz    = cy;
                    s_entityCenters[changedIdx].valid = true;

                    // v06.13: CoordSample — Approach B: shared-edge midpoint.
                    // When the player crosses from prevTri to newTriId, they're
                    // at or near the shared edge between the two triangles.
                    // The 3D midpoint of that shared edge is a much tighter
                    // constraint than the triangle center (~30 unit error vs ~100).
                    // Also logs the triangle center as fallback (prevTri=0 at field load).
                    if (changedIdx == s_playerEntityIdx && s_walkmesh.valid &&
                        newTriId < (uint16_t)s_walkmesh.numTriangles) {
                        // 2D entity position from fixed-point coords.
                        uint8_t* pBlock = base + ENTITY_STRIDE * changedIdx;
                        int32_t fpX = *(int32_t*)(pBlock + 0x190);
                        int32_t fpY = *(int32_t*)(pBlock + 0x194);
                        float ent2dX = (float)(fpX / 4096);
                        float ent2dY = (float)(fpY / 4096);
                        const char* fld = FF8Addresses::pCurrentFieldName
                                          ? FF8Addresses::pCurrentFieldName : "?";
                        // Try to find the shared edge between prevTri and newTriId.
                        uint16_t prevTri = s_coordPrevPlayerTri;
                        float wx = 0, wy = 0, wz = 0;
                        bool usedEdge = false;
                        if (prevTri != 0 && prevTri != 0xFFFF &&
                            prevTri < (uint16_t)s_walkmesh.numTriangles) {
                            const auto& tOld = s_walkmesh.triangles[prevTri];
                            // Find which edge of prevTri is shared with newTriId.
                            for (int e = 0; e < 3; e++) {
                                if (tOld.neighbor[e] == newTriId) {
                                    // Shared edge: vertices (e+1)%3 and (e+2)%3 of prevTri.
                                    int ea = tOld.vertexIdx[(e + 1) % 3];
                                    int eb = tOld.vertexIdx[(e + 2) % 3];
                                    if (ea < s_walkmesh.numVertices &&
                                        eb < s_walkmesh.numVertices) {
                                        wx = (s_walkmesh.vertices[ea].x +
                                              s_walkmesh.vertices[eb].x) / 2.0f;
                                        wy = (s_walkmesh.vertices[ea].y +
                                              s_walkmesh.vertices[eb].y) / 2.0f;
                                        wz = (s_walkmesh.vertices[ea].z +
                                              s_walkmesh.vertices[eb].z) / 2.0f;
                                        usedEdge = true;
                                    }
                                    break;
                                }
                            }
                        }
                        if (!usedEdge) {
                            // Fallback: use triangle center (field load, first sample, etc.)
                            const auto& triNew = s_walkmesh.triangles[newTriId];
                            int vi0 = triNew.vertexIdx[0];
                            int vi1 = triNew.vertexIdx[1];
                            int vi2 = triNew.vertexIdx[2];
                            if (vi0 < s_walkmesh.numVertices &&
                                vi1 < s_walkmesh.numVertices &&
                                vi2 < s_walkmesh.numVertices) {
                                wx = (s_walkmesh.vertices[vi0].x +
                                      s_walkmesh.vertices[vi1].x +
                                      s_walkmesh.vertices[vi2].x) / 3.0f;
                                wy = (s_walkmesh.vertices[vi0].y +
                                      s_walkmesh.vertices[vi1].y +
                                      s_walkmesh.vertices[vi2].y) / 3.0f;
                                wz = (s_walkmesh.vertices[vi0].z +
                                      s_walkmesh.vertices[vi1].z +
                                      s_walkmesh.vertices[vi2].z) / 3.0f;
                            }
                        }
                        NavLog::CoordSample(fld, (int)newTriId,
                                            ent2dX, ent2dY,
                                            wx, wy, wz);
                        s_coordPrevPlayerTri = newTriId;
                    }
                }
                // changeCount > 1: shadows updated above; centre not stored
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { /* non-critical */ }
    }

    // Diagnostic logging — first 30 calls per field only.
    if (s_setTriCallCount < SET_TRI_LOG_MAX) {
        s_setTriCallCount++;
        if (ok) {
            Log::Write("FieldNavigation: [set_tri] #%d "
                       "v0=(%d,%d,%d) v1=(%d,%d,%d) v2=(%d,%d,%d) center=(%.0f,%.0f)",
                       s_setTriCallCount,
                       x0, y0, z0, x1, y1, z1, x2, y2, z2,
                       (x0+x1+x2)/3.0f, (z0+z1+z2)/3.0f);
        } else {
            Log::Write("FieldNavigation: [set_tri] #%d args=(0x%08X,0x%08X,0x%08X) BAD vertex ptrs",
                       s_setTriCallCount, (unsigned)a1, (unsigned)a2, (unsigned)a3);
        }
    }
}

// ============================================================================
// Hook: field_scripts_init
// ============================================================================

static int __cdecl HookedFieldScriptsInit(int unk1, int unk2, int unk3, int unk4)
{
    // Reset all per-field state BEFORE calling the original.
    // The original function calls set_current_triangle for every entity during
    // field load, which populates s_entityCenters via HookedSetCurrentTriangle.
    // If we reset AFTER the original call we wipe those freshly-seeded centers.
    memset(s_prevTriangles, 0, sizeof(s_prevTriangles));
    memset(s_changeScore,   0, sizeof(s_changeScore));
    memset(s_entityCenters, 0, sizeof(s_entityCenters));
    memset(s_triCenter,     0, sizeof(s_triCenter));
    memset(s_hookPrevTri,   0, sizeof(s_hookPrevTri));
    s_driveLastTriId    = 0xFFFF;
    s_driveStuckTicks  = 0;
    s_driveWiggleTicks = 0;
    s_driveWiggleDir   = DIR_LEFT;
    s_playerEntityIdx    = -1;
    s_symNameCount       = 0;
    s_symOthersOffset    = 0;
    s_jsmDoors           = 0;
    s_jsmLines           = 0;
    s_jsmBackgrounds     = 0;
    s_jsmOthers          = 0;
    s_gatewayCount       = 0;
    s_triggerCount       = 0;
    s_capturedLineCount  = 0;
    s_waypointCount      = 0;
    s_waypointIdx        = 0;
    s_usingFunnel        = false;  // v05.95
    // Free previous walkmesh before loading new one.
    FieldArchive::FreeWalkmesh(s_walkmesh);
    memset(s_symNames,   0, sizeof(s_symNames));
    memset(s_gateways,   0, sizeof(s_gateways));
    memset(s_triggers,   0, sizeof(s_triggers));
    memset(s_capturedLines, 0, sizeof(s_capturedLines));
    s_catalogCount       = 0;
    s_playerTri          = 0xFFFF;
    s_setTriCallCount    = 0;
    s_structFallbackLogged = 0;
    // v05.58: ENTDIAG/BGDIAG disabled — keep flags true to skip dumps.
    s_entDiagDumped      = true;
    s_bgDiagDumped       = true;
    s_coordDiagDumped    = false;  // v05.59: reset so COORDDIAG fires once per field
    s_coordPrevPlayerTri = 0;       // v06.13: reset for shared-edge CoordSample
    // v06.14: Reset heading calibration for new field.
    s_camRightX = 1.0f; s_camRightY = 0.0f;
    s_camDownX = 0.0f; s_camDownY = -1.0f;  // default: +lY = -Y world (screen down)
    s_camCalibrated = false;
    s_calibPhase = 0;
    s_calibPending = true;  // calibrate on first drive
    s_projDiagCount      = 0;       // v06.13: reset projection diagnostic
    s_projDiagPrevFpX    = 0;
    s_projDiagPrevFpY    = 0;
    s_cycleIdx           = 0;
    s_selectedCatalogIdx = 0;
    s_nonPlayerCount     = 0;
    // Stop auto-drive on field transition — releases held keys.
    if (s_driveActive) StopAutoDrive(nullptr);
    s_driveActive        = false;
    s_driveHeld          = 0;

    int ret = s_originalFieldScriptsInit(unk1, unk2, unk3, unk4);
    // s_entityCenters now contains centers for every entity that fired
    // set_current_triangle during load — including stationary NPCs.

    __try {
        uint16_t    fieldId   = FF8Addresses::pCurrentFieldId
                                ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        const char* fieldName = FF8Addresses::pCurrentFieldName
                                ? FF8Addresses::pCurrentFieldName : "(unknown)";
        uint8_t     entCount  = FF8Addresses::pFieldStateOtherCount
                                ? *FF8Addresses::pFieldStateOtherCount : 0;

        Log::Write("FieldNavigation: [fieldload] id=%u name='%s' entities=%u",
                   (unsigned)fieldId, fieldName, (unsigned)entCount);

        // v05.41: Detect player entity only. Catalog is built on-demand
        // when the user presses -/= to cycle, via RefreshCatalog().
        // This avoids picking up placeholder entities from opening sequences.
        if (FF8Addresses::pFieldStateOthers && entCount > 0) {
            uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base) {
                uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;
                for (int i = 0; i < (int)lim; i++) {
                    uint8_t  setpc = *(base + ENTITY_STRIDE * i + 0x255);
                    if (setpc == 0) {
                        s_playerEntityIdx = i;
                        break;
                    }
                }
                Log::Write("FieldNavigation: [fieldload] player=ent%d", s_playerEntityIdx);
            }
        }

        // v05.48: Load SYM entity names, JSM counts, and INF gateways from archive.
        if (FieldArchive::IsReady() && fieldName && fieldName[0] != '(') {
            FieldArchive::LoadSYMNames(fieldName, s_symNames, MAX_SYM_NAMES, s_symNameCount);
            FieldArchive::LoadINFGateways(fieldName, s_gateways, MAX_GATEWAYS, s_gatewayCount);

            // v05.48: Filter bogus gateways.
            // Remove gateways with destFieldId=0 (first FL entry, usually bogus),
            // out-of-range IDs, or sentinel vertex values.
            int filtered = 0;
            for (int g = 0; g < s_gatewayCount; ) {
                bool bogus = false;
                if (s_gateways[g].destFieldId == 0) bogus = true;
                if (s_gateways[g].destFieldId >= 800) bogus = true;  // FF8 has ~800 fields max
                if (s_gateways[g].centerX > 30000.0f || s_gateways[g].centerX < -30000.0f) bogus = true;
                if (s_gateways[g].centerZ > 30000.0f || s_gateways[g].centerZ < -30000.0f) bogus = true;
                if (bogus) {
                    Log::Write("FieldNavigation: [fieldload] filtered bogus gateway: dest=%u '%s' center=(%.0f,%.0f)",
                               (unsigned)s_gateways[g].destFieldId, s_gateways[g].destFieldName,
                               s_gateways[g].centerX, s_gateways[g].centerZ);
                    // Shift remaining gateways down.
                    for (int j = g; j < s_gatewayCount - 1; j++)
                        s_gateways[j] = s_gateways[j+1];
                    s_gatewayCount--;
                    filtered++;
                } else {
                    g++;
                }
            }
            if (filtered > 0)
                Log::Write("FieldNavigation: [fieldload] %d bogus gateways filtered, %d remain",
                           filtered, s_gatewayCount);

            // v05.49: SYM offset = 0.
            // CONFIRMED by ENTDIAG: the entity state array maps 1:1 to the
            // first N SYM names. SYM excludes doors, and the runtime entity
            // array corresponds directly to the non-door SYM entries in order.
            // Entity state index i = SYM[i].
            s_symOthersOffset = 0;

            // Still load JSM counts for diagnostic logging.
            FieldArchive::JSMCounts jsmCounts = {};
            FieldArchive::LoadJSMCounts(fieldName, jsmCounts);
            s_jsmDoors       = jsmCounts.doors;
            s_jsmLines       = jsmCounts.lines;
            s_jsmBackgrounds = jsmCounts.backgrounds;
            s_jsmOthers      = jsmCounts.others;

            // v05.54: Load INF trigger zones.
            FieldArchive::LoadINFTriggers(fieldName, s_triggers, MAX_TRIGGERS, s_triggerCount);

            // v05.62: Load walkmesh for A* pathfinding.
            FieldArchive::LoadWalkmesh(fieldName, s_walkmesh);

            Log::Write("FieldNavigation: [fieldload] SYM: %d names, %d entities, offset=0, %d gateways, %d triggers, walkmesh=%s",
                       s_symNameCount, (int)entCount, s_gatewayCount, s_triggerCount,
                       s_walkmesh.valid ? "OK" : "NONE");
            Log::Write("FieldNavigation: [fieldload] JSM: doors=%d lines=%d bg=%d others=%d",
                       s_jsmDoors, s_jsmLines, s_jsmBackgrounds, s_jsmOthers);

            // v06.08: NavLog field load
            NavLog::FieldLoad(fieldName, (int)fieldId,
                              s_walkmesh.valid ? s_walkmesh.numTriangles : 0,
                              (int)entCount, s_gatewayCount, 0);
        }

        s_cachedFieldId = fieldId;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: Exception in field_scripts_init hook (0x%08X)",
                   GetExceptionCode());
    }

    return ret;
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;

    if (!FF8Addresses::HasFieldStateArrays())
        Log::Write("FieldNavigation: WARNING - entity arrays not resolved; centre capture inactive.");

    // Install set_current_triangle hook.
    if (FF8Addresses::set_current_triangle_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::set_current_triangle_addr,
            (LPVOID)HookedSetCurrentTriangle,
            (LPVOID*)&s_originalSetCurrentTriangle);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::set_current_triangle_addr);
        Log::Write("FieldNavigation: set_current_triangle hook @ 0x%08X — %s",
                   FF8Addresses::set_current_triangle_addr, MH_StatusToString(st));
    } else {
        Log::Write("FieldNavigation: WARNING - set_current_triangle_addr=0, hook skipped.");
    }

    // Install field_scripts_init hook.
    if (FF8Addresses::field_scripts_init_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::field_scripts_init_addr,
            (LPVOID)HookedFieldScriptsInit,
            (LPVOID*)&s_originalFieldScriptsInit);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::field_scripts_init_addr);
        Log::Write("FieldNavigation: field_scripts_init hook @ 0x%08X — %s",
                   FF8Addresses::field_scripts_init_addr, MH_StatusToString(st));
    } else {
        Log::Write("FieldNavigation: WARNING - field_scripts_init_addr=0, hook skipped.");
    }

    // v05.56: Hook SETLINE/LINEON/LINEOFF for trigger line capture.
    if (FF8Addresses::opcode_setline != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_setline,
            (LPVOID)HookedSetline,
            (LPVOID*)&s_originalSetline);
        Log::Write("FieldNavigation: opcode_setline hook @ 0x%08X — %s",
                   FF8Addresses::opcode_setline, MH_StatusToString(st));
    }
    if (FF8Addresses::opcode_lineon != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_lineon,
            (LPVOID)HookedLineon,
            (LPVOID*)&s_originalLineon);
        Log::Write("FieldNavigation: opcode_lineon hook @ 0x%08X — %s",
                   FF8Addresses::opcode_lineon, MH_StatusToString(st));
    }
    if (FF8Addresses::opcode_lineoff != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_lineoff,
            (LPVOID)HookedLineoff,
            (LPVOID*)&s_originalLineoff);
        Log::Write("FieldNavigation: opcode_lineoff hook @ 0x%08X — %s",
                   FF8Addresses::opcode_lineoff, MH_StatusToString(st));
    }

    // v05.78: Hook TALKRADIUS/PUSHRADIUS for interaction distance detection.
    if (FF8Addresses::opcode_talkradius != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_talkradius,
            (LPVOID)HookedTalkradius,
            (LPVOID*)&s_originalTalkradius);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::opcode_talkradius);
        Log::Write("FieldNavigation: opcode_talkradius hook @ 0x%08X — %s",
                   FF8Addresses::opcode_talkradius, MH_StatusToString(st));
    }
    if (FF8Addresses::opcode_pushradius != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_pushradius,
            (LPVOID)HookedPushradius,
            (LPVOID*)&s_originalPushradius);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::opcode_pushradius);
        Log::Write("FieldNavigation: opcode_pushradius hook @ 0x%08X — %s",
                   FF8Addresses::opcode_pushradius, MH_StatusToString(st));
    }

    // v05.84: Hook dinput_update_gamepad_status to intercept gamepad polling.
    if (FF8Addresses::dinput_update_gamepad_status_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::dinput_update_gamepad_status_addr,
            (LPVOID)HookedDinputUpdateGamepad,
            (LPVOID*)&s_originalDinputUpdateGamepad);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::dinput_update_gamepad_status_addr);
        Log::Write("FieldNavigation: dinput_update_gamepad_status hook @ 0x%08X — %s",
                   FF8Addresses::dinput_update_gamepad_status_addr, MH_StatusToString(st));
    }

    // v05.82: Hook engine_eval_keyboard_gamepad_input for analog input diagnostic.
    if (FF8Addresses::engine_eval_keyboard_gamepad_input_addr != 0 && FF8Addresses::HasGamepadStates()) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::engine_eval_keyboard_gamepad_input_addr,
            (LPVOID)HookedEngineEvalInput,
            (LPVOID*)&s_originalEngineEvalInput);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::engine_eval_keyboard_gamepad_input_addr);
        Log::Write("FieldNavigation: engine_eval_keyboard_gamepad_input hook @ 0x%08X — %s",
                   FF8Addresses::engine_eval_keyboard_gamepad_input_addr, MH_StatusToString(st));
    } else {
        Log::Write("FieldNavigation: WARNING - engine_eval or gamepad_states not resolved, GPDIAG2 hook skipped.");
    }

    // v05.89: Hook get_key_state for arrow key suppression during auto-drive.
    if (FF8Addresses::HasGetKeyState()) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::get_key_state_addr,
            (LPVOID)HookedGetKeyState,
            (LPVOID*)&s_originalGetKeyState);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::get_key_state_addr);
        Log::Write("FieldNavigation: get_key_state hook @ 0x%08X — %s",
                   FF8Addresses::get_key_state_addr, MH_StatusToString(st));
    } else {
        Log::Write("FieldNavigation: WARNING - get_key_state_addr=0, arrow suppression hook skipped.");
    }

    // Pointer chain diagnostic.
    if (FF8Addresses::pFieldStateOthers) {
        uint32_t flatBase = *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateOthers);
        Log::Write("FieldNavigation:   pFieldStateOthers (ptr) = 0x%08X -> base = 0x%08X",
                   (uint32_t)(uintptr_t)FF8Addresses::pFieldStateOthers, flatBase);
    }

    // v05.50: Background entity array diagnostic.
    if (FF8Addresses::pFieldStateBackgrounds) {
        uint32_t bgFlatBase = *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds);
        Log::Write("FieldNavigation:   pFieldStateBackgrounds (ptr) = 0x%08X -> base = 0x%08X",
                   (uint32_t)(uintptr_t)FF8Addresses::pFieldStateBackgrounds, bgFlatBase);
    } else {
        Log::Write("FieldNavigation:   pFieldStateBackgrounds NOT RESOLVED");
    }
    if (FF8Addresses::pFieldStateBackgroundCount) {
        Log::Write("FieldNavigation:   pFieldStateBackgroundCount = 0x%08X (val=%u)",
                   (uint32_t)(uintptr_t)FF8Addresses::pFieldStateBackgroundCount,
                   (unsigned)*FF8Addresses::pFieldStateBackgroundCount);
    }

    // v05.47: Initialize the field archive reader for SYM/INF extraction.
    FieldArchive::Initialize();

    s_initialized = true;
    s_lastLogTime = GetTickCount();
    Log::Write("FieldNavigation: Initialized v0.06.22 — Corridor steering respects trigger lines + talk radius expansion.");
    Log::Write("FieldNavigation:   F9  = nearest character and compass direction (repeat to cycle)");
    Log::Write("FieldNavigation:   F10 = player field name and position");
}

// v05.70: Screen filtering — check if two points are separated by any active
// SETLINE trigger line. Uses the 2D cross product sign test: if the player
// and entity are on opposite sides of any active trigger line, the entity is
// on a different "screen" and should be excluded from the catalog.
// Returns true if separated (i.e. entity should be hidden).
static bool IsSeparatedByTriggerLine(float px, float py, float ex, float ey, int skipTriggerIdx)
{
    for (int t = 0; t < s_capturedLineCount; t++) {
        if (!s_capturedLines[t].active) continue;
        // v06.02: Skip the exempted trigger line (used when driving TO a screen transition).
        if (t == skipTriggerIdx) continue;
        float lx1 = (float)s_capturedLines[t].x1;
        float ly1 = (float)s_capturedLines[t].y1;
        float lx2 = (float)s_capturedLines[t].x2;
        float ly2 = (float)s_capturedLines[t].y2;
        // Cross product: (line_end - line_start) x (point - line_start)
        float ldx = lx2 - lx1;
        float ldy = ly2 - ly1;
        float crossPlayer = ldx * (py - ly1) - ldy * (px - lx1);
        float crossEntity = ldx * (ey - ly1) - ldy * (ex - lx1);
        // If signs differ, points are on opposite sides of the line.
        // Use a small deadzone to avoid filtering entities right on the line.
        if (crossPlayer * crossEntity < -1.0f) {
            return true;  // separated by this trigger line
        }
    }
    return false;  // same side of all trigger lines
}

// v06.05: Check if moving from (px,py) in direction (dx,dy) by RECOVERY_CHECK_DIST
// would cross any non-target active trigger line. Returns true if the projected
// endpoint is on the opposite side of any trigger line from the start point.
// This prevents recovery wiggle from accidentally pushing the player through
// screen transitions (e.g., into an elevator).
static bool WouldCrossTriggerLine(float px, float py, float dx, float dy, int skipTriggerIdx)
{
    // Normalize direction and project ahead by RECOVERY_CHECK_DIST.
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.001f) return false;
    float ex = px + (dx / len) * RECOVERY_CHECK_DIST;
    float ey = py + (dy / len) * RECOVERY_CHECK_DIST;
    return IsSeparatedByTriggerLine(px, py, ex, ey, skipTriggerIdx);
}

// v05.41: On-demand catalog refresh.  Builds the catalog from the live entity
// array every time the user presses -/= to cycle.  Ordering is stable:
//   1. Entities that were already in the catalog keep their relative order.
//   2. Entities no longer qualifying (model gone, flags cleared) are removed.
//   3. Newly-qualifying entities are appended at the end.
// The selected catalog index is adjusted so the user stays on the same entity
// (or the nearest valid one if theirs was removed).
static void RefreshCatalog()
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return;
    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        // Re-detect player.
        for (int i = 0; i < (int)lim; i++) {
            uint8_t setpc = *(base + ENTITY_STRIDE * i + 0x255);
            if (setpc == 0) { s_playerEntityIdx = i; break; }
        }

        // v05.48: Diagnostic dump of ALL entities at scan time.
        // This reveals which entities exist and why some might be filtered.
        // v05.49: Also try multiple SYM offsets to find correct mapping.
        if (!s_entDiagDumped) {
            Log::Write("FieldNavigation: [ENTDIAG] === Entity dump: %d entities, symCount=%d, curOffset=%d ===",
                       (int)lim, s_symNameCount, s_symOthersOffset);
            // Log ALL SYM names for cross-reference.
            for (int s = 0; s < s_symNameCount; s++) {
                Log::Write("FieldNavigation: [ENTDIAG] SYM[%d]='%s'", s, s_symNames[s]);
            }
            for (int i = 0; i < (int)lim; i++) {
                uint8_t* block = base + ENTITY_STRIDE * i;
                int16_t  modelId      = *(int16_t*)(block + 0x218);
                uint16_t triId        = *(uint16_t*)(block + 0x1FA);
                uint8_t  setpc        = *(block + 0x255);
                uint8_t  talkonoff    = *(block + 0x24B);
                uint8_t  pushonoff    = *(block + 0x249);
                uint8_t  throughonoff = *(block + 0x24C);
                uint32_t execFlags    = *(uint32_t*)(block + 0x160);
                int32_t  fpX          = *(int32_t*)(block + 0x190);
                int32_t  fpZ          = *(int32_t*)(block + 0x198);
                int16_t  simX         = *(int16_t*)(block + 0x20);
                int16_t  simZ         = *(int16_t*)(block + 0x28);
                // Try offset 0, lines+bg, and current offset to compare.
                const char* sym0 = (i < s_symNameCount) ? s_symNames[i] : "(none)";
                int symLB = s_symOthersOffset + i;
                const char* symLBName = (symLB >= 0 && symLB < s_symNameCount) ? s_symNames[symLB] : "(none)";
                Log::Write("FieldNavigation: [ENTDIAG] ent%d model=%d tri=0x%04X setpc=%d "
                           "talk=%d push=%d thru=%d exec=0x%X fp=(%d,%d) sim=(%d,%d) "
                           "@0='%s' @%d='%s'",
                           i, (int)modelId, (unsigned)triId, (int)setpc,
                           (int)talkonoff, (int)pushonoff, (int)throughonoff,
                           execFlags, fpX, fpZ, (int)simX, (int)simZ,
                           sym0, s_symOthersOffset, symLBName);
            }
            s_entDiagDumped = true;
        }

        // v05.50: Background entity diagnostic dump.
        // Logs the entire backgrounds array with execution_flags, bgstate,
        // and candidate SYM indices to determine the correct mapping.
        if (!s_bgDiagDumped && FF8Addresses::HasFieldStateBackgrounds()) {
            __try {
                uint8_t bgCount = *FF8Addresses::pFieldStateBackgroundCount;
                uint8_t* bgBase = reinterpret_cast<uint8_t*>(
                    *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                Log::Write("FieldNavigation: [BGDIAG] === Background entity dump: %d bg entities ===",
                           (int)bgCount);
                Log::Write("FieldNavigation: [BGDIAG] bgBase=0x%08X  otherCount=%d  symCount=%d  JSM(D=%d L=%d B=%d O=%d)",
                           (uint32_t)(uintptr_t)bgBase, (int)lim, s_symNameCount,
                           s_jsmDoors, s_jsmLines, s_jsmBackgrounds, s_jsmOthers);
                if (bgBase && bgCount > 0) {
                    int bgLim = (bgCount < MAX_BG_ENTITIES) ? bgCount : MAX_BG_ENTITIES;
                    for (int b = 0; b < bgLim; b++) {
                        uint8_t* block = bgBase + BG_STRIDE * b;
                        // ff8_field_state_common fields:
                        uint32_t execFlags = *(uint32_t*)(block + 0x160);
                        uint16_t instrPos  = *(uint16_t*)(block + 0x176);
                        // ff8_field_state_background fields (after common at 0x188):
                        uint16_t bgstate   = *(uint16_t*)(block + 0x188);
                        // SYM mapping hypothesis: backgrounds are at SYM[L .. L+B-1]
                        // where L = number of line entities from JSM header.
                        // But we also try offset=0 mapping to see if it makes sense.
                        // For now, log the raw index and let the human figure it out.
                        const char* symDirect = (b < s_symNameCount) ? s_symNames[b] : "(none)";
                        // Hypothesis A: offset = otherCount (bg entities AFTER others in SYM).
                        int symAfterOthers = (int)lim + b;
                        const char* symAfterO = (symAfterOthers < s_symNameCount)
                                                ? s_symNames[symAfterOthers] : "(none)";
                        // Hypothesis B: offset = lines (SYM order = lines, bg, others).
                        int symAfterLines = s_jsmLines + b;
                        const char* symAfterL = (symAfterLines >= 0 && symAfterLines < s_symNameCount)
                                                ? s_symNames[symAfterLines] : "(none)";
                        Log::Write("FieldNavigation: [BGDIAG] bg%d exec=0x%X bgstate=0x%04X ipos=%u "
                                   "@0='%s' @oth%d='%s' @lin%d='%s'",
                                   b, execFlags, (unsigned)bgstate, (unsigned)instrPos,
                                   symDirect, symAfterOthers, symAfterO,
                                   symAfterLines, symAfterL);
                    }
                } else {
                    Log::Write("FieldNavigation: [BGDIAG] bgBase is NULL or bgCount==0");
                }
                Log::Write("FieldNavigation: [BGDIAG] === End background dump ===");
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                Log::Write("FieldNavigation: [BGDIAG] Exception reading backgrounds array");
            }
            s_bgDiagDumped = true;
        }

        // v05.59: Coordinate diagnostic dump — log ALL coord sources once per field.
        // This helps identify coordinate space mismatches between entities,
        // triggers, and gateways.
        if (!s_coordDiagDumped) {
            s_coordDiagDumped = true;  // only dump once per field
            Log::Write("FieldNavigation: [COORDDIAG] === Coordinate space diagnostic ===");
            Log::Write("FieldNavigation: [COORDDIAG] Field: %s  player=ent%d",
                       FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
                       s_playerEntityIdx);
            // Entity positions (all strategies)
            for (int i = 0; i < (int)lim; i++) {
                uint8_t* block = base + ENTITY_STRIDE * i;
                int16_t  modelId = *(int16_t*)(block + 0x218);
                uint16_t triId   = *(uint16_t*)(block + 0x1FA);
                int32_t  fpX     = *(int32_t*)(block + 0x190);
                int32_t  fpY     = *(int32_t*)(block + 0x194);
                int32_t  fpZ     = *(int32_t*)(block + 0x198);
                int16_t  simX    = *(int16_t*)(block + 0x20);
                int16_t  simY    = *(int16_t*)(block + 0x24);
                int16_t  simZ    = *(int16_t*)(block + 0x28);
                Log::Write("FieldNavigation: [COORDDIAG] ent%d model=%d tri=0x%04X "
                           "fp=(%d,%d,%d)/4096=(%d,%d,%d) sim=(%d,%d,%d)%s",
                           i, (int)modelId, (unsigned)triId,
                           fpX, fpY, fpZ, fpX/4096, fpY/4096, fpZ/4096,
                           (int)simX, (int)simY, (int)simZ,
                           (i == s_playerEntityIdx) ? " [PLAYER]" : "");
            }
            // SETLINE trigger positions — show all 3 raw axes
            for (int t = 0; t < s_capturedLineCount; t++) {
                Log::Write("FieldNavigation: [COORDDIAG] trigger%d ent=0x%08X "
                           "raw=(%d,%d,%d)->(%d,%d,%d) "
                           "centerX=%.0f centerY=%.0f centerZ=%.0f active=%d",
                           t, s_capturedLines[t].entityAddr,
                           (int)s_capturedLines[t].x1, (int)s_capturedLines[t].y1, (int)s_capturedLines[t].z1,
                           (int)s_capturedLines[t].x2, (int)s_capturedLines[t].y2, (int)s_capturedLines[t].z2,
                           (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f,
                           (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f,
                           (float)(s_capturedLines[t].z1 + s_capturedLines[t].z2) / 2.0f,
                           (int)s_capturedLines[t].active);
            }
            // Gateway positions
            for (int g = 0; g < s_gatewayCount; g++) {
                Log::Write("FieldNavigation: [COORDDIAG] gateway%d dest='%s' "
                           "INF_center=(%.0f,%.0f)",
                           g, s_gateways[g].destFieldName,
                           s_gateways[g].centerX, s_gateways[g].centerZ);
            }
            Log::Write("FieldNavigation: [COORDDIAG] === End diagnostic ===");
        }

        // Build set of currently-qualifying entity indices.
        bool qualifies[MAX_ENTITIES] = {};
        EntityInfo fresh[MAX_ENTITIES] = {};
        for (int i = 0; i < (int)lim; i++) {
            uint8_t* block = base + ENTITY_STRIDE * i;
            int16_t  modelId      = *(int16_t*)(block + 0x218);
            uint16_t triId        = *(uint16_t*)(block + 0x1FA);
            uint8_t  setpc        = *(block + 0x255);
            uint8_t  talkonoff    = *(block + 0x24B);
            uint8_t  pushonoff    = *(block + 0x249);
            uint8_t  throughonoff = *(block + 0x24C);

            // v05.52: Classify entity type by interaction flags.
            // setpc==0 means this IS the player; setpc!=0 means it isn't.
            // Interaction flags determine what the player can do with it.
            EntityType etype = ENT_UNKNOWN;
            if (talkonoff > 0)        etype = ENT_NPC;
            else if (pushonoff > 0)   etype = ENT_OBJECT;
            else if (throughonoff > 0) etype = ENT_EXIT;
            else                      etype = ENT_NPC;  // visible character, default to NPC
            bool hasModel = (modelId >= 0);
            bool hasInteraction = (talkonoff > 0 || pushonoff > 0 || throughonoff > 0);
            // v05.52: Only include entities the player can meaningfully navigate to.
            // Must have a visible model on the walkmesh. Invisible controllers
            // (model=-1, e.g. 'Director') are skipped even if they have execution_flags.
            bool isPlaced = (triId > 0 || (hasModel && triId == 0));
            if (isPlaced && hasModel) {
                qualifies[i] = true;
                EntityInfo ei_info = {};
                ei_info.entityIdx  = i;
                ei_info.modelId    = modelId;
                ei_info.triangleId = triId;
                ei_info.type       = etype;
                ei_info.gatewayIdx = -1;
                ei_info.name[0]    = '\0';
                // v05.59: Simplified naming — all entities are just "NPC".
                // The announcement code adds the number ("NPC 1", "NPC 2", etc.)
                strncpy(ei_info.name, "NPC", sizeof(ei_info.name) - 1);
                ei_info.name[sizeof(ei_info.name) - 1] = '\0';
                fresh[i] = ei_info;
            }
        }

        // v05.70: Screen filtering — exclude entities on the other side of
        // any active SETLINE trigger line from the player. This hides NPCs
        // that are on a different camera screen (e.g. front vs back of
        // bgroom_1 classroom). Only applies when we have trigger lines and
        // can read the player position.
        // v05.71: Track which entities were screen-filtered so we can identify
        // which trigger lines are true screen transitions (they separate the
        // player from at least one filtered entity).
        bool screenFiltered[MAX_ENTITIES] = {};
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float playerX, playerY;
            if (GetEntityPos(s_playerEntityIdx, playerX, playerY)) {
                int filtered = 0;
                for (int i = 0; i < (int)lim; i++) {
                    if (!qualifies[i]) continue;
                    if (i == s_playerEntityIdx) continue;  // never filter the player
                    float entX, entY;
                    if (GetEntityPos(i, entX, entY)) {
                        if (IsSeparatedByTriggerLine(playerX, playerY, entX, entY)) {
                            qualifies[i] = false;
                            screenFiltered[i] = true;
                            filtered++;
                        }
                    }
                }
                if (filtered > 0) {
                    Log::Write("FieldNavigation: [screen] filtered %d entities on other side of trigger lines (player at %.0f,%.0f)",
                               filtered, playerX, playerY);
                }
            }
        }

        // Remember which entry the user had selected (entity or gateway).
        int prevSelectedEntity = -2;  // -2 = none, -1 = gateway, >=0 = entity
        int prevSelectedGateway = -1;
        if (s_selectedCatalogIdx >= 0 && s_selectedCatalogIdx < s_catalogCount) {
            prevSelectedEntity  = s_catalog[s_selectedCatalogIdx].entityIdx;
            prevSelectedGateway = s_catalog[s_selectedCatalogIdx].gatewayIdx;
        }

        // Rebuild: first, retain existing entity entries that still qualify (in order).
        // v05.51: Also retain background entities (entityIdx <= -100) — they'll be
        // re-evaluated below. Only retain "others" entities here.
        EntityInfo newCatalog[MAX_CATALOG] = {};
        int newCount = 0;
        for (int c = 0; c < s_catalogCount && newCount < MAX_CATALOG; c++) {
            int ei = s_catalog[c].entityIdx;
            if (ei >= 0 && ei < (int)lim && qualifies[ei]) {
                newCatalog[newCount++] = fresh[ei];
                qualifies[ei] = false;  // mark as placed
            }
            // Gateway and background entries are re-added below — skip them here.
        }
        // Then append any newly-qualifying entities at the end.
        int added = 0;
        for (int i = 0; i < (int)lim && newCount < MAX_CATALOG; i++) {
            if (qualifies[i]) {
                newCatalog[newCount++] = fresh[i];
                added++;
            }
        }

        // v05.71: Show reachable SETLINE trigger lines as "Screen transition" exits,
        // but ONLY if the line is a true screen transition. A trigger line qualifies
        // as a screen transition if it separates the player from at least one entity
        // that was screen-filtered above. Event-only triggers (Trepie movement,
        // Selphie spawn) don't separate any entities from the player.
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float scrPlayerX = 0, scrPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, scrPlayerX, scrPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;

                    // Reachability: trigger center must be on same side of all
                    // OTHER active trigger lines as the player.
                    bool reachable = true;
                    for (int o = 0; o < s_capturedLineCount; o++) {
                        if (o == t) continue;
                        if (!s_capturedLines[o].active) continue;
                        float olx1 = (float)s_capturedLines[o].x1;
                        float oly1 = (float)s_capturedLines[o].y1;
                        float olx2 = (float)s_capturedLines[o].x2;
                        float oly2 = (float)s_capturedLines[o].y2;
                        float odx = olx2 - olx1;
                        float ody = oly2 - oly1;
                        float crossP = odx * (scrPlayerY - oly1) - ody * (scrPlayerX - olx1);
                        float crossT = odx * (tcy - oly1) - ody * (tcx - olx1);
                        if (crossP * crossT < -1.0f) { reachable = false; break; }
                    }
                    if (!reachable) continue;

                    // Screen transition test: this trigger line must separate the
                    // player from at least one screen-filtered entity. This ensures
                    // we only show lines that actually divide the field into distinct
                    // camera zones, not event triggers near entities.
                    float tlx1 = (float)s_capturedLines[t].x1;
                    float tly1 = (float)s_capturedLines[t].y1;
                    float tlx2 = (float)s_capturedLines[t].x2;
                    float tly2 = (float)s_capturedLines[t].y2;
                    float tdx = tlx2 - tlx1;
                    float tdy = tly2 - tly1;
                    float crossPlayerSelf = tdx * (scrPlayerY - tly1) - tdy * (scrPlayerX - tlx1);
                    bool separatesFiltered = false;
                    for (int i = 0; i < (int)lim; i++) {
                        if (!screenFiltered[i]) continue;  // only check screen-filtered entities
                        float ex, ey;
                        if (!GetEntityPos(i, ex, ey)) continue;
                        float crossEnt = tdx * (ey - tly1) - tdy * (ex - tlx1);
                        if (crossPlayerSelf * crossEnt < -1.0f) {
                            separatesFiltered = true;
                            break;
                        }
                    }
                    // Also check screen-filtered gateways.
                    if (!separatesFiltered) {
                        for (int g = 0; g < s_gatewayCount; g++) {
                            float gx = s_gateways[g].centerX;
                            float gz = s_gateways[g].centerZ;
                            // Gateway is "screen-filtered" if it's separated from player.
                            if (!IsSeparatedByTriggerLine(scrPlayerX, scrPlayerY, gx, gz))
                                continue;  // gateway is on player's screen, not filtered
                            float crossGw = tdx * (gz - tly1) - tdy * (gx - tlx1);
                            if (crossPlayerSelf * crossGw < -1.0f) {
                                separatesFiltered = true;
                                break;
                            }
                        }
                    }
                    if (!separatesFiltered) continue;  // event trigger, not screen boundary

                    EntityInfo trigExit = {};
                    trigExit.entityIdx  = -200 - t;
                    trigExit.modelId    = -1;
                    trigExit.triangleId = 0;
                    trigExit.type       = ENT_EXIT;
                    trigExit.gatewayIdx = -1;
                    strncpy(trigExit.name, "Screen transition", sizeof(trigExit.name) - 1);
                    trigExit.name[sizeof(trigExit.name) - 1] = '\0';
                    newCatalog[newCount++] = trigExit;
                }
            }
        }

        // v05.72: Add reachable event triggers (non-screen-transition) as "Event".
        // These are active trigger lines on the player's screen that don't
        // separate screen-filtered entities. They fire script events when crossed.
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float evPlayerX = 0, evPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, evPlayerX, evPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    // Skip if already added as a screen transition.
                    bool alreadyAdded = false;
                    for (int c = 0; c < newCount; c++) {
                        if (newCatalog[c].entityIdx == (-200 - t)) { alreadyAdded = true; break; }
                    }
                    if (alreadyAdded) continue;
                    // Reachability check (same as screen transitions).
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    bool reachable = true;
                    for (int o = 0; o < s_capturedLineCount; o++) {
                        if (o == t) continue;
                        if (!s_capturedLines[o].active) continue;
                        float olx1 = (float)s_capturedLines[o].x1;
                        float oly1 = (float)s_capturedLines[o].y1;
                        float olx2 = (float)s_capturedLines[o].x2;
                        float oly2 = (float)s_capturedLines[o].y2;
                        float odx = olx2 - olx1;
                        float ody = oly2 - oly1;
                        float crossP = odx * (evPlayerY - oly1) - ody * (evPlayerX - olx1);
                        float crossT = odx * (tcy - oly1) - ody * (tcx - olx1);
                        if (crossP * crossT < -1.0f) { reachable = false; break; }
                    }
                    if (!reachable) continue;
                    EntityInfo evEntry = {};
                    evEntry.entityIdx  = -200 - t;
                    evEntry.modelId    = -1;
                    evEntry.triangleId = 0;
                    evEntry.type       = ENT_OBJECT;  // "Event" in announcement
                    evEntry.gatewayIdx = -1;
                    strncpy(evEntry.name, "Event", sizeof(evEntry.name) - 1);
                    evEntry.name[sizeof(evEntry.name) - 1] = '\0';
                    newCatalog[newCount++] = evEntry;
                }
            }
        }

        // v05.52: Background entities removed from cycling catalog.
        // They have no walkmesh position and can't be auto-driven to.
        // Active bg entities are still logged in BGDIAG for diagnostics.
        // Interactive objects (terminals, bulletin boards) are script-triggered
        // by walk-on zones — the player discovers them by exploring, not by
        // navigating to an entity position.

        // v05.47: Append INF gateway exits.
        // v05.71: Apply screen filtering to gateways — hide exits on other
        // side of trigger lines (e.g. feart2f1 exit when player is in back).
        {
            float gwPlayerX = 0, gwPlayerY = 0;
            bool gwHavePlayer = (s_capturedLineCount > 0 && s_playerEntityIdx >= 0
                                 && GetEntityPos(s_playerEntityIdx, gwPlayerX, gwPlayerY));
            for (int g = 0; g < s_gatewayCount && newCount < MAX_CATALOG; g++) {
                if (gwHavePlayer) {
                    float gwX = s_gateways[g].centerX;
                    float gwZ = s_gateways[g].centerZ;
                    if (IsSeparatedByTriggerLine(gwPlayerX, gwPlayerY, gwX, gwZ))
                        continue;  // skip — gateway is on a different screen
                }
                EntityInfo gwEntry = {};
                gwEntry.entityIdx  = -1;  // sentinel: not a real entity
                gwEntry.modelId    = -1;
                gwEntry.triangleId = 0;
                gwEntry.type       = ENT_EXIT;
                gwEntry.gatewayIdx = g;
                // Name = destination field name from INF.
                strncpy(gwEntry.name, s_gateways[g].destFieldName, 47);
                gwEntry.name[47] = '\0';
                newCatalog[newCount++] = gwEntry;
            }
        }

        // Detect changes and log.
        bool changed = (newCount != s_catalogCount || added > 0);
        if (!changed) {
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].entityIdx != s_catalog[c].entityIdx ||
                    newCatalog[c].gatewayIdx != s_catalog[c].gatewayIdx) {
                    changed = true; break;
                }
            }
        }

        // Commit.
        memcpy(s_catalog, newCatalog, sizeof(s_catalog));
        s_catalogCount = newCount;
        s_nonPlayerCount = 0;
        for (int c = 0; c < s_catalogCount; c++) {
            if (s_catalog[c].entityIdx != s_playerEntityIdx)
                s_nonPlayerCount++;
        }

        // Restore selection to the same entity/gateway/bg, or clamp.
        s_selectedCatalogIdx = 0;
        if (prevSelectedEntity != -2) {
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == prevSelectedEntity &&
                    s_catalog[c].gatewayIdx == prevSelectedGateway) {
                    s_selectedCatalogIdx = c; break;
                }
            }
        }

        if (changed) {
            Log::Write("FieldNavigation: [refresh] catalog: %d entries (%d navigable, %d new entities, %d gateways), player=ent%d",
                       s_catalogCount, s_nonPlayerCount, added, s_gatewayCount, s_playerEntityIdx);
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == s_playerEntityIdx) continue;
                if (s_catalog[c].gatewayIdx >= 0)
                    Log::Write("FieldNavigation: [refresh]   cat%d EXIT gw%d -> '%s'",
                               c, s_catalog[c].gatewayIdx, s_catalog[c].name);
                else if (s_catalog[c].entityIdx <= -200) {
                    int ti = -(s_catalog[c].entityIdx + 200);
                    float tcx = (ti < s_capturedLineCount) ? (float)(s_capturedLines[ti].x1 + s_capturedLines[ti].x2) / 2.0f : 0;
                    float tcz = (ti < s_capturedLineCount) ? (float)(s_capturedLines[ti].y1 + s_capturedLines[ti].y2) / 2.0f : 0;
                    Log::Write("FieldNavigation: [refresh]   cat%d TRIGGER line%d center=(%.0f,%.0f) name='%s'",
                               c, ti, tcx, tcz, s_catalog[c].name);
                }
                else
                    Log::Write("FieldNavigation: [refresh]   cat%d ent%d model=%d type=%s name='%s'",
                               c, s_catalog[c].entityIdx, (int)s_catalog[c].modelId,
                               EntityTypeName(s_catalog[c].type), s_catalog[c].name);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: Exception in RefreshCatalog()");
    }
}

void Update()
{
    if (!s_initialized) return;
    if (!FF8Addresses::HasFieldStateArrays()) return;
    if (!FF8Addresses::IsOnField()) return;

    // Key handling and auto-drive are unthrottled: runs every ~16ms.
    HandleKeys();
    UpdateAutoDrive();

    // Entity position polling is throttled to 500ms to reduce log spam.
    DWORD now = GetTickCount();
    if ((now - s_lastLogTime) < 500) return;
    s_lastLogTime = now;

    __try {
        uint8_t  entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;

        uint8_t* base = reinterpret_cast<uint8_t*>(
            *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateOthers));
        if (!base) return;

        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        // Player entity is identified at field load via setpc==0 — trust that,
        // don't override with heuristic scoring.
        if (s_playerEntityIdx < 0) return;
        s_playerTri = *(uint16_t*)(base + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);

        // Position system confirmed working (v05.45).  POSDIAG removed.
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldNavigation: Exception in Update() (0x%08X)", GetExceptionCode());
    }
}

void Shutdown()
{
    if (!s_initialized) return;

    // Release any held direction keys before unhooking.
    StopAutoDrive(nullptr);

    if (FF8Addresses::set_current_triangle_addr && s_originalSetCurrentTriangle) {
        MH_DisableHook((LPVOID)(uintptr_t)FF8Addresses::set_current_triangle_addr);
        MH_RemoveHook( (LPVOID)(uintptr_t)FF8Addresses::set_current_triangle_addr);
        s_originalSetCurrentTriangle = nullptr;
    }
    if (FF8Addresses::field_scripts_init_addr && s_originalFieldScriptsInit) {
        MH_DisableHook((LPVOID)(uintptr_t)FF8Addresses::field_scripts_init_addr);
        MH_RemoveHook( (LPVOID)(uintptr_t)FF8Addresses::field_scripts_init_addr);
        s_originalFieldScriptsInit = nullptr;
    }
    if (FF8Addresses::engine_eval_keyboard_gamepad_input_addr && s_originalEngineEvalInput) {
        MH_DisableHook((LPVOID)(uintptr_t)FF8Addresses::engine_eval_keyboard_gamepad_input_addr);
        MH_RemoveHook( (LPVOID)(uintptr_t)FF8Addresses::engine_eval_keyboard_gamepad_input_addr);
        s_originalEngineEvalInput = nullptr;
    }
    if (FF8Addresses::dinput_update_gamepad_status_addr && s_originalDinputUpdateGamepad) {
        MH_DisableHook((LPVOID)(uintptr_t)FF8Addresses::dinput_update_gamepad_status_addr);
        MH_RemoveHook( (LPVOID)(uintptr_t)FF8Addresses::dinput_update_gamepad_status_addr);
        s_originalDinputUpdateGamepad = nullptr;
    }
    if (FF8Addresses::get_key_state_addr && s_originalGetKeyState) {
        MH_DisableHook((LPVOID)(uintptr_t)FF8Addresses::get_key_state_addr);
        MH_RemoveHook( (LPVOID)(uintptr_t)FF8Addresses::get_key_state_addr);
        s_originalGetKeyState = nullptr;
    }
    // Ensure fake gamepad is removed even if StopAutoDrive wasn't called.
    if (s_fakeGamepadInstalled && FF8Addresses::HasDinputGamepadPtrs()) {
        *FF8Addresses::pDinputGamepadDevicePtr = s_savedDevicePtr;
        *FF8Addresses::pDinputGamepadStatePtr  = s_savedStatePtr;
        s_fakeGamepadInstalled = false;
    }

    FieldArchive::FreeWalkmesh(s_walkmesh);
    FieldArchive::Shutdown();

    s_initialized = false;
    Log::Write("FieldNavigation: Shutdown.");
}

bool IsActive() { return s_initialized; }

}  // namespace FieldNavigation
