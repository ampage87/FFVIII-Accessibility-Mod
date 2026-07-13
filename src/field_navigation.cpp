// field_navigation.cpp - Field navigation assistance for blind players
//
// See field_navigation.h for full architecture and phasing notes.
//
// ============================================================================
// CURRENT STATE: See FF8OPC_VERSION in ff8_accessibility.h
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
// Coordinate system (corrected v0.12.03 via gateway analysis):
//   FF8 walkmesh: +X = screen-right, +Y = screen-up.
//   Entity Y (offset 0x194) is the screen-vertical axis. +Y = screen-up.
//   Z (0x198) is always ~0 (depth).
//   (Old note said "-Y = screen-up" — WRONG. Gateway data proves
//   +Y = screen-up: bggate_4 forest exit Y=5353 is top, hallway Y=534 bottom.)
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
#include "field_display_names.h"
#include "entity_classifications.h"
// v0.17.8.15: chara.one cross-reference removed; the catalog dedupe pass now
// uses JSM behavior signals (jsmCategory + hasSetmodelInit) for the NPC vs
// Interaction discriminator. See field_nav_catalog_dedupe.inl.

// v0.15.9.11.3: chase_keyboard header included BEFORE the FieldNavigation
// namespace opens so its global-scope namespace (::ChaseKeyboard) is
// reachable from the .inl files included inside FieldNavigation below.
// The chase keyboard buffer substitution is called from InjectKey in
// field_nav_autodrive.inl during chase Auto, and only during chase Auto --
// F9 path-finding and world-map AD are unaffected (ChaseKeyboard::IsActive()
// returns false for them).
#include "chase_keyboard.h"

// Forward declarations for cross-module namespaces (restored in v0.14.28 build recovery).
namespace Log { void Field(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); }
namespace NavLog {
    void SessionStart();
    void FieldLoad(const char* fieldName, int fieldId, int numTris, int numEntities, int numExits, int numEvents);
    void DriveStart(const char* fieldName, const char* targetName, const char* targetType,
                    int startTri, float startX, float startY,
                    int goalTri, float goalX, float goalY, float talkRadius,
                    int astarTris, int waypointCount, bool usedFunnel);
    void DriveWaypoint(int wpIndex, int wpTotal, float playerX, float playerY, float distToTarget, int tick);
    void DriveSample(float playerX, float playerY, int playerTri, float distToTarget, int wpIndex, int wpTotal, int tick);
    void DriveRecovery(int phase, int playerTri, float playerX, float playerY, float distToTarget);
    void DriveEnd(const char* result, int totalTicks, float finalDist, int recoveryPhases, float startDist);
    void CoordSample(const char* fieldName, int triIdx, float posX, float posY, float wx, float wy, float wz);
}

namespace FieldNavigation {

// ============================================================================
// v0.17.9.12: bggate_6 (front gate) push-through gate diagnostic (Track A).
// LOCAL ONLY. When 1, a one-shot [GATEDIAG] walkmesh/reachability dump plus
// the [TTRACE] turnstile path tracer fire on field bggate_6 (id 0x00A3).
// Behaviour-neutral; only adds logging. See DumpGateDiagnostic() +
// TurnstileTrace() in field_nav_diagnostics.inl and the arm site in
// field_nav_fieldscripts.inl. v0.17.9.17: set to 0 for push -- Track A Step 3
// (the turnstile) is BAT-passed, so GATEDIAG/TTRACE have served their purpose.
// Flip back to 1 to re-probe the front gate.
// ============================================================================
#define FEPIC1_GATE_DIAG 0

// ============================================================================
// v0.17.9.17: per-field captured-trigger-line dump toggle. When 1, a
// [LINEDIAG] line fires on EVERY field load for each captured SETLINE,
// reporting its assigned lineType / destFieldId / center / JSM name (see the
// arm site in field_nav_fieldscripts.inl). Behaviour-neutral; only adds
// logging. Set to 0 for push. Flip to 1 when verifying a field's trigger-line
// classification or exit destinations -- e.g. the pending bgryo1_1 'squalls'
// and dotown_2 'Selphie' exit-label checks, or any future screen-bound / exit
// bug. (Was an always-on raw loop through v0.17.9.16.2; gated here so it's a
// one-line re-enable instead of a code edit.)
// ============================================================================
#define LINEDIAG_ENABLED 0

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

// v0.07.94: Deduplicated INF gateway groups for catalog.
// Multiple INF gateways with the same destFieldId are merged into one
// catalog exit with averaged center position. This prevents 3 gateway
// lines covering one wide exit from appearing as 3 separate exits.
struct DedupGateway {
    float   centerX, centerY;
    uint16_t destFieldId;
    char    displayName[48];
    int     count;  // number of raw gateways merged
};
static const int MAX_DEDUP_GATEWAYS = 12;
static DedupGateway s_dedupGateways[MAX_DEDUP_GATEWAYS] = {};
static int s_dedupGatewayCount = 0;

// --- INF trigger zones (loaded per field from archive, v05.54) ---
static const int    MAX_TRIGGERS  = 16;
static FieldArchive::TriggerInfo s_triggers[MAX_TRIGGERS] = {};
static int          s_triggerCount = 0;

// --- JSM entity classification results (loaded per field from ScanJSMScripts) ---
static const int MAX_JSM_ENTITIES = 128;
static FieldArchive::JSMEntityInfo s_jsmEntities[MAX_JSM_ENTITIES] = {};
static int s_jsmEntityCount = 0;

// --- NPC catalog (rebuilt at each field load) ---
enum EntityType { ENT_UNKNOWN = 0, ENT_NPC, ENT_OBJECT, ENT_EXIT, ENT_BG_NPC, ENT_BG_OBJECT,
                  ENT_SAVE_POINT, ENT_DRAW_POINT, ENT_SHOP, ENT_CARD_GAME, ENT_INTERACTION };

static const char* EntityTypeName(EntityType t) {
    switch (t) {
        case ENT_NPC:        return "NPC";
        case ENT_OBJECT:     return "Object";
        case ENT_EXIT:       return "Exit";
        case ENT_BG_NPC:     return "NPC";
        case ENT_BG_OBJECT:  return "Object";
        case ENT_SAVE_POINT: return "Save Point";
        case ENT_DRAW_POINT: return "Draw Point";
        case ENT_SHOP:       return "Shop";
        case ENT_CARD_GAME:  return "Card Game";
        case ENT_INTERACTION: return "Interaction";
        default:             return "Entity";
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

// v0.18.3.231: one-shot extended entity scan (reads past the engine's reported
// entity count). Retired — kept for re-arming if the entity array ever needs
// re-triage on a new field (this is what located the G-Garden train staff).
static bool      s_extScanDumped = true;

// v0.18.3.234: catalog scan trace, emitted ONCE per field load (reset by
// HookedFieldScriptsInit). RefreshCatalog runs ~1/sec, so tracing every rebuild
// would flood the log; once per field is enough to diagnose a missing entity.
static bool      s_scanTraced    = true;

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


// --- v0.15.9.2.1 / moved earlier in v0.17.8.19.2: Chase-drive state
// declared at file scope so all .inl files that need it can reference
// s_chaseDriveActive / s_chaseDriveTargetX/Y. Originally declared just
// before the autodrive.inl include, but v0.17.8.19.2 added a chase-drive
// gate inside PruneCollinearWaypoints (field_nav_pathfinding.inl) which is
// included earlier in this file, so the declarations had to move up.
// The chase-drive IMPLEMENTATION (StartChaseDrive / StopChaseDrive /
// IsChaseDriveActive) still lives in field_nav_directiondrive.inl which is
// included later, after autodrive.inl, so it can use SetHeldDirections /
// InjectKey from autodrive. File-scope statics in textual includes share one
// translation unit, so these definitions are visible to every .inl below.
static volatile bool s_chaseDriveActive = false;  // owned by directiondrive.inl, read by autodrive.inl + pathfinding.inl
static bool          s_chaseDriveWalk   = false;  // owned by directiondrive.inl
static int32_t       s_chaseDriveTargetX = 0;     // cached by StartChaseDrive, read by autodrive's UpdateAutoDrive
static int32_t       s_chaseDriveTargetY = 0;

// --- Engine input hooks (extracted v0.12.18) ---
#include "field_nav_input_hooks.inl"

// --- Helper functions (extracted v0.12.18) ---
#include "field_nav_helpers.inl"

// --- A* pathfinding, funnel smoothing, BFS (extracted v0.12.18) ---
#include "field_nav_pathfinding.inl"

// --- SYM name resolution, display names, BG classification (extracted v0.12.18) ---
#include "field_nav_names.inl"

// --- Opcode hooks: SETLINE, TALKRAD, PUSHRAD, SET3, PSHM_W (extracted v0.12.18) ---
#include "field_nav_opcode_hooks.inl"

// --- v0.17.7.4: MAPJUMP/MAPJUMP3/DISCJUMP/MAPJUMPO/WORLDMAPJUMP dispatch-table
//   diagnostic hooks. Logs varblock state at field-transition fire time so we
//   can identify the destination value for variable-dispatch MAPJUMPs that
//   static analysis couldn't resolve. Diagnostic-only, no catalog changes.
#include "field_nav_mapjump_diag.inl"

// --- Catalog announce: AnnounceCurrentTarget, AnnounceDirections, CycleEntity (extracted v0.12.18) ---
#include "field_nav_announce.inl"

// --- Auto-drive state machine, steering, recovery (extracted v0.12.18) ---
// v0.15.9.2.1 / v0.17.8.19.2: Chase-drive state (s_chaseDriveActive,
// s_chaseDriveWalk, s_chaseDriveTargetX/Y) was declared here originally but
// moved up above the field_nav_pathfinding.inl include in v0.17.8.19.2 so
// the prune chase-drive gate could reference it. See the new declaration
// block earlier in this file for the full rationale.

// v0.15.9.2.6: File-scope dead-end cluster state populated by the walkmesh
// dead-end scanner in HookedFieldScriptsInit (was a local variable before;
// results were logged then discarded). Promoted to file scope so the public
// API GetLargestClusterCenter() can read them, used by chase_auto_pilot to
// pick a default MODE_TARGET destination on chase fields that aren't in its
// explicit per-field config table.
struct DeadEndCluster {
    float centerX, centerY;
    int   triCount;
    int   seedTri;
};
static const int MAX_DEAD_CLUSTERS = 32;
static DeadEndCluster s_deadClusters[MAX_DEAD_CLUSTERS] = {};
static int            s_deadClusterCount = 0;

// --- Auto-drive helpers + CALIB (extracted v0.17.8.20 from field_nav_autodrive.inl
//     for size relief). helpers MUST precede calib (RunCalibration calls
//     SetHeldDirections); both MUST precede autodrive (UpdateAutoDrive calls
//     RunCalibration and the helpers). All three share the file-scope statics
//     declared above. See those files' headers + DEVNOTES. ---
#include "field_nav_autodrive_helpers.inl"
#include "field_nav_autodrive_calib.inl"
#include "field_nav_autodrive.inl"

// --- Direction-based auto-drive for chase scenes (v0.15.9.1) ---
// Included AFTER field_nav_autodrive.inl so SetHeldDirections / InjectKey /
// ReleaseAllDirections are visible. Included BEFORE field_nav_handlekeys.inl
// so the F9 drive handler can see s_directionDriveActive for mutex.
#include "field_nav_directiondrive.inl"

// --- GPS guided navigation (extracted v0.12.18) ---
#include "field_nav_gps.inl"

// --- Key dispatch (extracted v0.12.18) ---
#include "field_nav_handlekeys.inl"

// --- HookedSetCurrentTriangle (extracted v0.12.18) ---
#include "field_nav_settriangle.inl"

// --- HookedFieldScriptsInit (extracted v0.12.18) ---
#include "field_nav_fieldscripts.inl"

void Initialize()
{
    if (s_initialized) return;

    if (!FF8Addresses::HasFieldStateArrays())
        Log::Field("FieldNavigation: WARNING - entity arrays not resolved; centre capture inactive.");

    // Install set_current_triangle hook.
    if (FF8Addresses::set_current_triangle_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::set_current_triangle_addr,
            (LPVOID)HookedSetCurrentTriangle,
            (LPVOID*)&s_originalSetCurrentTriangle);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::set_current_triangle_addr);
        Log::Field("FieldNavigation: set_current_triangle hook @ 0x%08X — %s",
                   FF8Addresses::set_current_triangle_addr, MH_StatusToString(st));
    } else {
        Log::Field("FieldNavigation: WARNING - set_current_triangle_addr=0, hook skipped.");
    }

    // Install field_scripts_init hook.
    if (FF8Addresses::field_scripts_init_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::field_scripts_init_addr,
            (LPVOID)HookedFieldScriptsInit,
            (LPVOID*)&s_originalFieldScriptsInit);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::field_scripts_init_addr);
        Log::Field("FieldNavigation: field_scripts_init hook @ 0x%08X — %s",
                   FF8Addresses::field_scripts_init_addr, MH_StatusToString(st));
    } else {
        Log::Field("FieldNavigation: WARNING - field_scripts_init_addr=0, hook skipped.");
    }

    // v05.56: Hook SETLINE/LINEON/LINEOFF for trigger line capture.
    if (FF8Addresses::opcode_setline != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_setline,
            (LPVOID)HookedSetline,
            (LPVOID*)&s_originalSetline);
        Log::Field("FieldNavigation: opcode_setline hook @ 0x%08X — %s",
                   FF8Addresses::opcode_setline, MH_StatusToString(st));
    }
    if (FF8Addresses::opcode_lineon != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_lineon,
            (LPVOID)HookedLineon,
            (LPVOID*)&s_originalLineon);
        Log::Field("FieldNavigation: opcode_lineon hook @ 0x%08X — %s",
                   FF8Addresses::opcode_lineon, MH_StatusToString(st));
    }
    if (FF8Addresses::opcode_lineoff != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_lineoff,
            (LPVOID)HookedLineoff,
            (LPVOID*)&s_originalLineoff);
        Log::Field("FieldNavigation: opcode_lineoff hook @ 0x%08X — %s",
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
        Log::Field("FieldNavigation: opcode_talkradius hook @ 0x%08X — %s",
                   FF8Addresses::opcode_talkradius, MH_StatusToString(st));
    }
    if (FF8Addresses::opcode_pushradius != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_pushradius,
            (LPVOID)HookedPushradius,
            (LPVOID*)&s_originalPushradius);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::opcode_pushradius);
        Log::Field("FieldNavigation: opcode_pushradius hook @ 0x%08X — %s",
                   FF8Addresses::opcode_pushradius, MH_StatusToString(st));
    }

    // v0.08.03: SET3 opcode hook — PERMANENTLY DISABLED.
    // ANY interception of SET3 (MinHook or dispatch table) causes the infirmary scene
    // to hang (Dr. Kadowaki walk-to-phone never completes). Even a minimal wrapper
    // that only calls the original and returns triggers the bug. The FF8 script
    // interpreter is sensitive to SET3 handler replacement. See DEVNOTES.md.
    // SET3 capture was used for PSHM_W entity position investigation (exhausted).
    // Shift-pattern passthrough provides adequate fallback positions.
    // TODO: If SET3 capture is ever needed again, investigate naked/asm thunk.
    if (false && FF8Addresses::pExecuteOpcodeTable != nullptr) {
        s_originalSet3 = (OpcodeHandler_t)(uintptr_t)FF8Addresses::pExecuteOpcodeTable[0x1E];
        uint32_t* tableEntry = &FF8Addresses::pExecuteOpcodeTable[0x1E];
        DWORD oldProtect = 0;
        if (VirtualProtect(tableEntry, 4, PAGE_READWRITE, &oldProtect)) {
            *tableEntry = (uint32_t)(uintptr_t)HookedSet3;
            VirtualProtect(tableEntry, 4, oldProtect, &oldProtect);
            Log::Field("FieldNavigation: opcode_set3 dispatch table patch @ 0x%08X (was 0x%08X, now 0x%08X)",
                       (uint32_t)(uintptr_t)tableEntry,
                       (uint32_t)(uintptr_t)s_originalSet3,
                       (uint32_t)(uintptr_t)HookedSet3);
        } else {
            Log::Field("FieldNavigation: WARNING - VirtualProtect failed for SET3 dispatch table patch");
            s_originalSet3 = nullptr;
        }
    } else if (false && FF8Addresses::opcode_set3 != 0) {
        // OLD MinHook path — DISABLED (caused infirmary scene hang).
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::opcode_set3,
            (LPVOID)HookedSet3,
            (LPVOID*)&s_originalSet3);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::opcode_set3);
        Log::Field("FieldNavigation: opcode_set3 hook @ 0x%08X — %s",
                   FF8Addresses::opcode_set3, MH_StatusToString(st));
    } else {
        Log::Field("FieldNavigation: WARNING - opcode_set3=0, SET3 position capture hook skipped.");
    }

    // v0.08.07-10: PSHM_W dispatch table hook DISABLED for v0.08.11.
    // Replaced by direct varblock read diagnostic (see HookedFieldScriptsInit).
    // Hook infrastructure retained for future use if needed.
    if (FF8Addresses::opcode_pshm_w != 0) {
        Log::Field("FieldNavigation: PSHM_W at 0x%08X (hook disabled, using varblock diagnostic)",
                   FF8Addresses::opcode_pshm_w);
    }

    // v0.17.7.4: Install MAPJUMP-family dispatch table hooks for destination
    // diagnostics. See field_nav_mapjump_diag.inl for the full rationale.
    // Runs after the existing SET3/PSHM_W setup block so we know the opcode
    // table is resolved by the time we touch it.
    MapjumpDiag::Install();

    // v0.14.45: POPM_W/B/L shared memory write capture hooks removed (F12 diagnostic retired).

    // v05.84: Hook dinput_update_gamepad_status to intercept gamepad polling.
    if (FF8Addresses::dinput_update_gamepad_status_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::dinput_update_gamepad_status_addr,
            (LPVOID)HookedDinputUpdateGamepad,
            (LPVOID*)&s_originalDinputUpdateGamepad);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::dinput_update_gamepad_status_addr);
        Log::Field("FieldNavigation: dinput_update_gamepad_status hook @ 0x%08X — %s",
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
        Log::Field("FieldNavigation: engine_eval_keyboard_gamepad_input hook @ 0x%08X — %s",
                   FF8Addresses::engine_eval_keyboard_gamepad_input_addr, MH_StatusToString(st));
    } else {
        Log::Field("FieldNavigation: WARNING - engine_eval or gamepad_states not resolved, GPDIAG2 hook skipped.");
    }

    // v05.89: Hook get_key_state for arrow key suppression during auto-drive.
    if (FF8Addresses::HasGetKeyState()) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)(uintptr_t)FF8Addresses::get_key_state_addr,
            (LPVOID)HookedGetKeyState,
            (LPVOID*)&s_originalGetKeyState);
        if (st == MH_OK)
            st = MH_EnableHook((LPVOID)(uintptr_t)FF8Addresses::get_key_state_addr);
        Log::Field("FieldNavigation: get_key_state hook @ 0x%08X — %s",
                   FF8Addresses::get_key_state_addr, MH_StatusToString(st));
    } else {
        Log::Field("FieldNavigation: WARNING - get_key_state_addr=0, arrow suppression hook skipped.");
    }

    // Pointer chain diagnostic.
    if (FF8Addresses::pFieldStateOthers) {
        uint32_t flatBase = *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateOthers);
        Log::Field("FieldNavigation:   pFieldStateOthers (ptr) = 0x%08X -> base = 0x%08X",
                   (uint32_t)(uintptr_t)FF8Addresses::pFieldStateOthers, flatBase);
    }

    // v05.50: Background entity array diagnostic.
    if (FF8Addresses::pFieldStateBackgrounds) {
        uint32_t bgFlatBase = *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds);
        Log::Field("FieldNavigation:   pFieldStateBackgrounds (ptr) = 0x%08X -> base = 0x%08X",
                   (uint32_t)(uintptr_t)FF8Addresses::pFieldStateBackgrounds, bgFlatBase);
    } else {
        Log::Field("FieldNavigation:   pFieldStateBackgrounds NOT RESOLVED");
    }
    if (FF8Addresses::pFieldStateBackgroundCount) {
        Log::Field("FieldNavigation:   pFieldStateBackgroundCount = 0x%08X (val=%u)",
                   (uint32_t)(uintptr_t)FF8Addresses::pFieldStateBackgroundCount,
                   (unsigned)*FF8Addresses::pFieldStateBackgroundCount);
    }

    // v05.47: Initialize the field archive reader for SYM/INF extraction.
    FieldArchive::Initialize();

    s_initialized = true;
    s_lastLogTime = GetTickCount();
    Log::Field("FieldNavigation: Initialized v%s.", FF8OPC_VERSION);
    Log::Field("FieldNavigation:   F9  = nearest character and compass direction (repeat to cycle)");
    Log::Field("FieldNavigation:   F10 = player field name and position");
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
        // v0.07.82: Only screen-boundary lines act as separators.
        // Camera pans, event triggers, and unclassified lines are transparent.
        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
            s_capturedLines[t].lineType != FieldArchive::JSM_ENT_UNKNOWN)
            continue;
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

// v0.17.8.10: Proper bounded segment-vs-segment intersection. Returns true only
// when segment AB actually crosses segment CD (orientation test). Unlike the
// infinite-line side test in IsSeparatedByTriggerLine, a short line that lies
// off to one side does NOT count as a crossing. Used by the INF-gateway screen
// filter so a far-edge screen-boundary doorway no longer falsely "separates" a
// gateway on the opposite edge (the bug behind the missing bghall_5 -> Hall 4
// exit). Collinear/endpoint-touch cases return false: a gateway grazing a
// boundary endpoint is not "behind" it.
static int Orient2D(float px, float py, float qx, float qy, float rx, float ry)
{
    float v = (qx - px) * (ry - py) - (qy - py) * (rx - px);
    if (v >  1.0f) return  1;
    if (v < -1.0f) return -1;
    return 0;
}
static bool SegmentsCross(float ax, float ay, float bx, float by,
                          float cx, float cy, float dx, float dy)
{
    int o1 = Orient2D(ax, ay, bx, by, cx, cy);
    int o2 = Orient2D(ax, ay, bx, by, dx, dy);
    int o3 = Orient2D(cx, cy, dx, dy, ax, ay);
    int o4 = Orient2D(cx, cy, dx, dy, bx, by);
    return (o1 != o2 && o3 != o4 && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0);
}

// v0.17.9.15: Local bounded screen-bound crossing test for A* avoidance.
// Forward-declared in field_nav_pathfinding.inl. Unlike IsSeparatedByTriggerLine
// (which side-splits an INFINITE line from the start centre, so a short
// screen-bound segment near the spawn fences off the whole far side of the
// field via its extension), this returns true only when the actual traversal
// edge (ax,ay)->(bx,by) crosses a screen-bound line's FINITE segment, via the
// bounded SegmentsCross. ComputeAStarPath calls it per current->neighbor
// expansion so A* routes AROUND a boundary instead of being globally walled
// off by the boundary's infinite extension. Same filter as
// IsSeparatedByTriggerLine (only SCREEN_BOUND / UNKNOWN lines are barriers;
// camera-pans and interactive lines are transparent) and the same
// skipTriggerIdx exemption (used when driving TO a screen transition).
// Validated on bcsaka_1 (Balamb Hotel): the global test gives No path
// tri 13->196; this gives the 65-triangle route. nav-core shared with the
// Dollet chase -> gated on a chase-first BAT. See DEVNOTES "Track A Step 2".
static bool EdgeCrossesScreenBound(float ax, float ay, float bx, float by, int skipTriggerIdx)
{
    for (int t = 0; t < s_capturedLineCount; t++) {
        if (!s_capturedLines[t].active) continue;
        if (t == skipTriggerIdx) continue;
        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
            s_capturedLines[t].lineType != FieldArchive::JSM_ENT_UNKNOWN)
            continue;
        if (SegmentsCross(ax, ay, bx, by,
                          (float)s_capturedLines[t].x1, (float)s_capturedLines[t].y1,
                          (float)s_capturedLines[t].x2, (float)s_capturedLines[t].y2))
            return true;
    }
    return false;
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

// --- Catalog diagnostic + late-resolve helpers (extracted v0.17.7.0) ---
// MUST come before field_nav_catalog.inl so RefreshCatalog can call them.
#include "field_nav_catalog_diag.inl"
#include "field_nav_catalog_lateres.inl"

// --- RefreshCatalog (extracted v0.12.18) ---
#include "field_nav_catalog.inl"

// --- Diagnostic functions (extracted v0.12.18) ---
#include "field_nav_diagnostics.inl"

// --- v0.17.3: Passive arrow-key response observer (diagnostic only) ---
#include "field_nav_observe.inl"

void Update()
{
    if (!s_initialized) return;
    if (!FF8Addresses::HasFieldStateArrays()) return;
    if (!FF8Addresses::IsOnField()) return;

    // Key handling and auto-drive are unthrottled: runs every ~16ms.
    HandleKeys();
    UpdateAutoDrive();
    UpdateGPS();  // v0.12.02: GPS guided navigation polling
    ObserveArrowResponse();  // v0.17.3: log empirical arrow->world response vs CA prediction

#if FEPIC1_GATE_DIAG
    // v0.17.9.12: fire the one-shot fepic1 push-through gate dump once the
    // post-load settle delay elapses (armed in HookedFieldScriptsInit).
    if (s_gateDiagPending && (GetTickCount() - s_gateDiagArmTime) >= GATEDIAG_DELAY_MS) {
        s_gateDiagPending = false;
        DumpGateDiagnostic();
    }
    // v0.17.9.16.1: per-tick turnstile path tracer (bggate_6, auto-drive OFF).
    TurnstileTrace();
#endif

    // v0.14.45: POPM varblock write capture summary block removed (F12 diagnostic retired).

    // v0.08.23: Descriptor table polling probe — DISABLED v0.12.05.
    // Served its purpose: confirmed descriptor table is party-slot-only,
    // not PSHM_W entity-scope cache. See DEVNOTES v0.08.23.
    // Code retained but no longer activated on field change.
    if (false && FF8Addresses::pCurrentFieldId) {
        uint16_t curFieldId = *FF8Addresses::pCurrentFieldId;
        if (curFieldId != s_descriptorPollFieldId && curFieldId != 0xFFFF) {
            // New field loaded — start descriptor polling.
            s_descriptorPollFieldId = curFieldId;
            s_descriptorPollActive = true;
            s_descriptorPollStart = GetTickCount();
            s_descriptorPollCount = 0;
            s_descriptorPollLastCheck = 0;
            s_descriptorPollSummaryLogged = false;
            Log::Field("FieldNavigation: [DESCPOLL] Starting descriptor table poll for field %d (%s), "
                       "totalEnts=%d (D%d+L%d+B%d+O%d)",
                       curFieldId,
                       FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
                       s_jsmDoors + s_jsmLines + s_jsmBackgrounds + s_jsmOthers,
                       s_jsmDoors, s_jsmLines, s_jsmBackgrounds, s_jsmOthers);
        }
    }

    // v0.08.24: One-shot hex dump of PSHM_W functions — DISABLED v0.12.11.
    // DumpPshmFunctions();

    // v0.08.23: Run descriptor table polling probe — DISABLED v0.12.11.
    // PollDescriptorTable();

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
        Log::Field("FieldNavigation: Exception in Update() (0x%08X)", GetExceptionCode());
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
    // v0.09.38: SET3 uses dispatch table patch (not MinHook).
    if (FF8Addresses::pExecuteOpcodeTable != nullptr && s_originalSet3 != nullptr) {
        uint32_t* tableEntry = &FF8Addresses::pExecuteOpcodeTable[0x1E];
        DWORD oldProtect = 0;
        if (VirtualProtect(tableEntry, 4, PAGE_READWRITE, &oldProtect)) {
            *tableEntry = (uint32_t)(uintptr_t)s_originalSet3;
            VirtualProtect(tableEntry, 4, oldProtect, &oldProtect);
        }
        s_originalSet3 = nullptr;
    }
    // v0.08.07: Restore PSHM_W dispatch table entry (not MinHook).
    if (FF8Addresses::pExecuteOpcodeTable != nullptr && s_originalPshmW != nullptr) {
        uint32_t* tableEntry = &FF8Addresses::pExecuteOpcodeTable[0x06];
        DWORD oldProtect = 0;
        if (VirtualProtect(tableEntry, 4, PAGE_READWRITE, &oldProtect)) {
            *tableEntry = (uint32_t)(uintptr_t)s_originalPshmW;
            VirtualProtect(tableEntry, 4, oldProtect, &oldProtect);
        }
        s_originalPshmW = nullptr;
    }
    // v0.17.7.4: Restore MAPJUMP-family dispatch table entries before unload.
    MapjumpDiag::Restore();
    // v0.14.45: POPM_W/B/L shared memory write hook removal block removed (F12 diagnostic retired).
    // Ensure fake gamepad is removed even if StopAutoDrive wasn't called.
    if (s_fakeGamepadInstalled && FF8Addresses::HasDinputGamepadPtrs()) {
        *FF8Addresses::pDinputGamepadDevicePtr = s_savedDevicePtr;
        *FF8Addresses::pDinputGamepadStatePtr  = s_savedStatePtr;
        s_fakeGamepadInstalled = false;
    }

    FieldArchive::FreeWalkmesh(s_walkmesh);
    FieldArchive::Shutdown();

    s_initialized = false;
    Log::Field("FieldNavigation: Shutdown.");
}

bool IsActive() { return s_initialized; }

// v0.15.9.2.6: Walkmesh cluster query for chase auto-pilot fallback.
// Returns the center of the cluster with the highest triCount across the
// dead-end scanner's results. Ties broken by first found (lowest seedTri).
// See header for full contract.
bool GetLargestClusterCenter(int32_t* outX, int32_t* outY)
{
    if (!outX || !outY) return false;
    if (s_deadClusterCount <= 0) return false;
    int bestIdx = -1;
    int bestCount = 0;
    for (int i = 0; i < s_deadClusterCount; ++i) {
        if (s_deadClusters[i].triCount > bestCount) {
            bestCount = s_deadClusters[i].triCount;
            bestIdx = i;
        }
    }
    if (bestIdx < 0) return false;
    *outX = (int32_t)s_deadClusters[bestIdx].centerX;
    *outY = (int32_t)s_deadClusters[bestIdx].centerY;
    return true;
}

// v0.15.9.2.14: Trigger-line lookup for chase auto-pilot fallback.
// See header for full contract. Implementation: find largest cluster, then
// pick the trigger line whose center is closest to that cluster.
bool GetTriggerLineNearestCluster(int32_t* outX, int32_t* outY, int* outTrigIdx)
{
    if (!outX || !outY || !outTrigIdx) return false;

    int32_t clusterX = 0, clusterY = 0;
    if (!GetLargestClusterCenter(&clusterX, &clusterY)) return false;

    int bestIdx = -1;
    float bestDistSq = 1e30f;
    for (int t = 0; t < s_capturedLineCount; ++t) {
        if (!s_capturedLines[t].active) continue;
        // Match the IsSeparatedByTriggerLine filter: SCREEN_BOUND for actual
        // screen transitions, UNKNOWN for unclassified lines that haven't been
        // labeled yet (common on early-game chase fields).
        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
            s_capturedLines[t].lineType != FieldArchive::JSM_ENT_UNKNOWN) continue;
        float cx = ((float)s_capturedLines[t].x1 + (float)s_capturedLines[t].x2) * 0.5f;
        float cy = ((float)s_capturedLines[t].y1 + (float)s_capturedLines[t].y2) * 0.5f;
        float dx = cx - (float)clusterX;
        float dy = cy - (float)clusterY;
        float distSq = dx*dx + dy*dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestIdx = t;
        }
    }

    if (bestIdx < 0) return false;

    *outX = (int32_t)(((float)s_capturedLines[bestIdx].x1 +
                       (float)s_capturedLines[bestIdx].x2) * 0.5f);
    *outY = (int32_t)(((float)s_capturedLines[bestIdx].y1 +
                       (float)s_capturedLines[bestIdx].y2) * 0.5f);
    *outTrigIdx = bestIdx;
    Log::Field("FieldNavigation: GetTriggerLineNearestCluster matched cluster(%d,%d) "
               "-> trigger[%d] center=(%d,%d) distFromCluster=%.0f",
               (int)clusterX, (int)clusterY, bestIdx, *outX, *outY, sqrtf(bestDistSq));
    return true;
}

// v0.15.9.2.15: INF gateway lookup for chase auto-pilot fallback.
// See header for full contract. Implementation: get largest cluster + player
// position, then pick the gateway whose direction-from-player has the largest
// positive dot product with the player->cluster vector. That selects forward-
// progress gateways and rejects entry-back gateways even when the entry-back
// gateway is geometrically closer to the cluster center.
bool GetGatewayNearestCluster(int32_t* outX, int32_t* outY,
                              int32_t* outLineX1, int32_t* outLineY1,
                              int32_t* outLineX2, int32_t* outLineY2)
{
    if (!outX || !outY || !outLineX1 || !outLineY1 || !outLineX2 || !outLineY2)
        return false;
    if (s_gatewayCount <= 0) return false;

    int32_t clusterX = 0, clusterY = 0;
    if (!GetLargestClusterCenter(&clusterX, &clusterY)) return false;

    float playerX = 0, playerY = 0;
    if (s_playerEntityIdx < 0) return false;
    if (!GetEntityPos(s_playerEntityIdx, playerX, playerY)) return false;

    // Vector from player to cluster -- defines "forward" direction.
    float clx = (float)clusterX - playerX;
    float cly = (float)clusterY - playerY;

    int bestIdx = -1;
    float bestScore = -1e30f;
    for (int g = 0; g < s_gatewayCount; ++g) {
        // Skip degenerate gateways (zero-length line).
        if (s_gateways[g].lineX1 == 0 && s_gateways[g].lineY1 == 0 &&
            s_gateways[g].lineX2 == 0 && s_gateways[g].lineY2 == 0)
            continue;
        float gwDx = s_gateways[g].centerX - playerX;
        float gwDy = s_gateways[g].centerZ - playerY;  // centerZ = Y in our coords
        float dot = gwDx * clx + gwDy * cly;
        if (dot > bestScore) {
            bestScore = dot;
            bestIdx = g;
        }
    }

    if (bestIdx < 0) return false;

    *outX = (int32_t)s_gateways[bestIdx].centerX;
    *outY = (int32_t)s_gateways[bestIdx].centerZ;
    *outLineX1 = (int32_t)s_gateways[bestIdx].lineX1;
    *outLineY1 = (int32_t)s_gateways[bestIdx].lineY1;
    *outLineX2 = (int32_t)s_gateways[bestIdx].lineX2;
    *outLineY2 = (int32_t)s_gateways[bestIdx].lineY2;
    Log::Field("FieldNavigation: GetGatewayNearestCluster matched cluster(%d,%d) "
               "player=(%.0f,%.0f) -> gateway[%d] center=(%d,%d) "
               "line=(%d,%d)->(%d,%d) destFieldId=%u score=%.0f",
               (int)clusterX, (int)clusterY, playerX, playerY,
               bestIdx, *outX, *outY,
               *outLineX1, *outLineY1, *outLineX2, *outLineY2,
               (unsigned)s_gateways[bestIdx].destFieldId, bestScore);
    return true;
}

}  // namespace FieldNavigation
