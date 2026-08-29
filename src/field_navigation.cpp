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
#include <string>
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
#include "countdown_timer.h"   // v0.63.1 (#111): the space rescue stops the clock
#include "field_overlay.h"     // v0.65.0 (#111): the mod's own Game Controls box
#include "battle_tts.h"        // v0.66.1 (#111): RequestScreenshotAsync / GetScreenshotDir
#include "ff8_text_decode.h"  // v0.120.0 (#centra): ButtonKeyName, for "press X"
                               // -- the space rescue takes one shot of its own box
                               // in the only window where a shot is possible

// Forward declarations for cross-module namespaces (restored in v0.14.28 build recovery).
namespace Log { void Field(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); bool IsSpeaking(); }
namespace FmvAudioDesc { void SetSuppressed(bool on); }  // #minigame-bgbtl briefing
namespace FmvSkip { std::string GetCurrentAviName(); bool RequestSkip();
                    bool IsMoviePlaying(); }  // #minigame-bgbtl
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

// v0.18.3.283 (#85): widened 16 -> 32. The [EXTSCAN] diagnostic (armed for
// glwater1) proved sakua/sakub/seigyo's runtime "others" slots (17-19) sit
// PAST this cap, not just past the reported otherCount -- even the
// "scan past the reported count" diagnostic couldn't reach them, because it
// was itself bounded by MAX_ENTITIES. glwater1 needs indices up to 19
// (Lines=5 + Backgrounds=4 + Others=11 = 20 total non-door SYM/state
// entries). 32 gives headroom for other fields without another bump; all
// uses elsewhere in the codebase are via this symbol (arrays sized by it,
// bounds checks against it) -- verified no raw "16" literal duplicates it.
static const int    MAX_ENTITIES  = 32;
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

// v0.20.7: duplicated-room phantom-exit suppression (field_nav_duproom.inl).
// Destinations that are phantoms on the CURRENT field -- a scripted MAP_EXIT to
// one of these is the cutscene twin of a real gateway exit in a sibling copy of
// this room. Recomputed each field load by ComputeDupRoomSuppression().
static uint16_t     s_dupSuppressDests[8] = {};
static int          s_dupSuppressCount = 0;
// v0.20.9 DIAGNOSTIC: current field name, for the [PUZZLE-GATE] var-bank dump.
static char         s_currentFieldName[64] = {};

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
// v0.18.3.286 (#85): set by ResolveTriangleCentroidPositions() when a
// position is a walkmesh-triangle-centroid approximation, not a real
// resolved one. Kept out of JSMEntityInfo (shared w/ offline tooling).
static bool s_jsmTriangleApprox[MAX_JSM_ENTITIES] = {};
// v0.18.3.297 (#85): entity is the WRONG world-state of a multi-state object
// (fallen ladder while it still stands). Injection skips these; positions stay
// intact. Rationale: field_nav_catalog_lateres.inl.
static bool s_jsmStateSuppressed[MAX_JSM_ENTITIES] = {};

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

// --- Navigation state declarations (extracted v0.55.0 for the 80 KB guard) ---
#include "field_nav_state.inl"


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
#include "field_minigame_bgbtl.inl"   // #minigame-bgbtl
#include "dragon_fight_model.inl"    // #105
#include "field_minigame_dragon.inl" // #105 -- after bgbtl: it rings that tone

// --- Disc 3 blockers (#110 Esthar Pandora run, #111 space rescue, #112
//     Propagator pairs). Models first (pure, probe-tested in
//     tests/disc3_models_compile.cpp), then the wiring layer. The wiring
//     layer uses GardenBattle::CopyKeyName, so it must follow bgbtl.
#include "field_pause.inl"          // v0.64.0 (#111): the one-byte field freeze
#include "esthar_pandora_model.inl"  // #110
#include "space_rescue_model.inl"    // #111
#include "propagator_model.inl"      // #112
#include "field_disc3.inl"           // #110/#111/#112 wiring

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
#include "field_nav_duproom.inl"      // v0.20.7: duplicated-room phantom-exit table + ComputeDupRoomSuppression()
// v0.101.0 (#derived-pos): positions the field files do not carry but the
// field's own script does. Must precede field_nav_fieldscripts.inl, which
// applies the table right after the INF trigger-zone pass.
#include "field_nav_derived_pos.inl"
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

    // v0.125.0 (#centra): hook the engine's step routine, so the ladder cue can
    // measure the game's own climbing cadence instead of guessing at it, and can
    // stay silent on the ladders the game already sounds.
    InstallLadderStepHook();

    // v0.130.0 (#centra): and the script's own PREQEW wait, which is the only
    // thing that says a ladder move is running when the move sets no movement
    // mode -- crroof1's descent.
    InstallLadderPreqewHook();

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
#include "field_nav_screenfilter.inl"

#include "field_nav_geometry.inl"

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

// --- Consolidated field entity-catalog assembly (v0.19.x) ---
// field_catalog.inl merges the former 8-file catalog assembly (diag +
// lateres + RefreshCatalog core + naming/triglines/mapexits/gateways/dedupe)
// into one file of real functions. Behavior-identical to the 8-file version,
// proven byte-identical offline (28-fixture catalog equivalence harness).
#include "field_catalog.inl"

// --- Diagnostic functions (extracted v0.12.18) ---
#include "field_nav_diagnostics.inl"

// --- v0.17.3: Passive arrow-key response observer (diagnostic only) ---
#include "field_nav_observe.inl"

// v0.18.3.236 (#72): battle pause/resume poll for F9 auto-drive. Split into
// its own .inl at creation time (this block pushed field_navigation.cpp over
// the 80 KB CI hard cap). See the .inl header for the full contract.
#include "field_nav_battlepause.inl"

// v0.18.3.260 (#83 diagnostic, LOCAL): per-frame talk-enable byte watcher for
// Caraway's Mansion scene actors. Logging only. See the .inl header.
#include "field_nav_caraway_diag.inl"

// v0.106.0 (#bahamut-light): the Game Controls box, published for modules
// outside this translation unit. Same sequence the space rescue runs inline --
// the game's own window first, the mod's overlay as the fallback.
bool OpenControlsBox(const char* text)
{
    if (text == nullptr || text[0] == '\0') return false;
    const bool gameBox = GardenBattle::OpenBriefDialog(text);
    if (!gameBox) FieldOverlay::Show(text);
    return gameBox;
}

void CloseControlsBox()
{
    GardenBattle::CloseBriefDialog();
    FieldOverlay::Hide();
}

void Update()
{
    if (!s_initialized) return;
    // v0.18.3.236 (#72): must run before the off-field early-returns so the
    // battle-entry pause fires while the game mode is battle.
    PollBattlePauseResume();
    if (!FF8Addresses::HasFieldStateArrays()) return;
    if (!FF8Addresses::IsOnField()) return;

    // v0.124.0 (#centra): the ladder climb sound. Cheap -- one guarded byte
    // read per frame -- and it must run before the early-returns below so a
    // climb that carries the player off the walkmesh keeps sounding.
    PollLadderClimbSound();

    // v0.18.3.260 (#83 diagnostic): per-frame, change-only entity-flag watcher
    // on glfurin4. Must run BEFORE the 500ms throttle below so a transient
    // talk-enable byte cannot slip between samples. No-op off glfurin4.
    CarawaySceneDiagTick();

    GardenBattle::Update();   // #minigame-bgbtl: reaction cue, unthrottled; no-op off bgbtl_1
    DragonFight::Update();    // #105: no-op off tvglen3
    Disc3::Update();          // #110/#111/#112: no-op off the disc-3 fields
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

    // v0.20.18: removed dead diagnostics (v0.08.23 descriptor-table poll probe + v0.08.24
    // PSHM_W dump stubs, DISABLED since v0.12.05/.11) to fit the 80 KB CI limit -- see git.

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

    // v0.64.0 (#111): and the field freeze, first of all. Everything else here
    // is a hook that stops mattering when the DLL goes; a 0xC3 left over
    // field_main's entry is a field that never advances again.
    FieldPause::Release("FieldNavigation::Shutdown");
    FieldOverlay::Shutdown();

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
