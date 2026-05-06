// world_map.cpp - World map navigation TTS for blind players
//
// ============================================================================
// CURRENT STATE: v0.14.92 — Chapter 3 Stage 4 diagnostic build: hex-dump
//                wmsetus.obj Sections 7 and 8 to surface the FIELD
//                ENTRY trigger bytecode. v0.14.91 BAT'd cleanly and
//                proved the deep research's leading hypothesis wrong:
//                Sections 17/18 contain wm2field-style destination
//                data (field-walkmesh coords ±10000 range, 32-byte
//                records keyed on wmField IDs 0x07-0x10), not world-
//                map trigger geometry. A subsequent disassembly hunt
//                of FF8_EN.exe identified the real architecture:
//
//                The world-map tick at sub_53FAC0 calls sub_545EA0
//                each frame on foot. sub_545EA0 reads from Section 8
//                of wmsetus.obj (resolved at runtime as [0x2040070],
//                set up by sub_542DA0's 8th iteration). Section 8 is
//                a BYTECODE PROGRAM with ~56 different opcodes (range
//                0xFF02..0xFF38) dispatched via a jump table at
//                sub_546100. Opcodes include rectangle-bounds checks,
//                savemap story-flag comparisons, AND/OR combinators,
//                multi-stage matching states (modes 1-6), and a
//                follow-link instruction (0xFF0E) that lets locations
//                share sub-programs. When a program path evaluates to
//                'match', the wmField ID it carries gets written and
//                the field-entry transition fires. (sub_541C80 is the
//                separate ENCOUNTER trigger using Section 1 + Section
//                2's terrain map; not relevant for AD steering.)
//
//                Why this matters for AD: B-Garden's bytecode is a
//                wide-rectangle bounds check (entrable from any
//                direction). Balamb Town's bytecode is a tight
//                rectangle just covering the gate (entrable only from
//                the south). Catalog-center steering misses Balamb
//                Town because the catalog X/Y is the town center, not
//                the gate. Once we decode Section 8, we know each
//                location's exact entry rectangle and AD targets the
//                rectangle center directly.
//
//                THIS BUILD just hex-dumps Sections 7 (56 bytes) and
//                8 (2652 bytes) to ff8_world.log under [TRIGGER-DUMP]
//                — about 170 log rows total, trivial cost. Section 7
//                is the small adjacent section (set up next to 8 by
//                sub_542DA0); likely auxiliary metadata for the
//                bytecode walker. Sections 17/18 are no longer dumped
//                (we now know they're not relevant). After BAT, Claude
//                writes a Python disassembler in the bash sandbox
//                using the opcode dispatch table mapped from
//                sub_546100 + sub_545F10, runs it against the dumped
//                bytes, and emits per-location entry geometry. v0.14.93
//                hardcodes the decoded data as a static s_triggerData[]
//                array; v0.14.94 wires it into StartAutoDrive's
//                targeting + arrival check, demoting sweep-search to
//                fallback only for locations whose programs use
//                opcodes not yet decoded.
//
//                Pressing `\` while a drive is in progress cancels.
//
//   Prior baseline:
//   v0.11.16 — Deferred catalog build (position validity check)
//   v0.14.31 — Update()/Shutdown() restored after v0.14.24 build damage
// ============================================================================
//
// Architecture mirrors FieldNavigation but simpler:
// - Hardcoded 37-entry location catalog (26 main + 7 chocobo + 4 alien)
// - On world map entry: compute distances, sort, freeze list
// - -/= cycle through sorted list with TTS announcement
// - Backspace announces bearing + distance to selected location
// - \ reserved for auto-drive (future)
// - Continuous polling: vehicle changes, world map entry/exit
//
// All addresses confirmed static (no pointer chains needed):
//   Position: 0x0203EE80/84/88 (X/Y/Z DWORDs)
//   Heading:  0x0203ED02 (WORD, 0-4095, 0=North CW)
//   Vehicle:  0x02040A5E (BYTE, locomotion mode)
//   Scene:    0x0203ED2C (WORD, 0=worldmap 1=field/battle)

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "world_map.h"

// Forward declarations for namespaces
namespace Log { void World(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); }

namespace WorldMap {

// ============================================================================
// Confirmed static addresses (from deep research + ff8-speedruns + v0.11.02 diag)
// ============================================================================
static const uint32_t WM_POS_X      = 0x0203EE80;  // DWORD - player X
static const uint32_t WM_POS_Y      = 0x0203EE84;  // DWORD - player Y
static const uint32_t WM_POS_Z      = 0x0203EE88;  // DWORD - player Z
static const uint32_t WM_HEADING    = 0x0203ED02;   // WORD  - 0=North, 0-4095 CW
static const uint32_t WM_LOCOMOTION = 0x02040A5E;   // BYTE  - locomotion / vehicle mode. v0.14.83 whitelisted to canonical {0..4}; per `Plan & Research Documents/World Map Terrain and Locomotion Reference.md` legitimate values include 0=Squall foot, 6=Selphie foot, 10=train, 31=Chocobo, 32=invisible-car — our GetVehicleName uses 0/1/2/3/4 for foot/Car/Chocobo/Ship/Ragnarok which DISAGREES with the research-doc enum (research says Chocobo=31, we say 2). Empirical reconciliation needed; see v0.14.84 changelog.
static const uint32_t WM_SCENE_FLAG = 0x0203ED2C;   // WORD  - 0=worldmap, 1=field

// World map dimensions (wrapping torus)
static const double WM_WIDTH  = 262144.0;
static const double WM_HEIGHT = 196608.0;

// ============================================================================
// wmx.obj terrain grid constants (v0.14.85 restoration from v0.11.16 impl)
// ============================================================================
// world.fi entry 9 points at wmx.obj inside world.fs. The mesh contains 835
// segments. Of those, the first 768 form the playable 32x24 grid; the remainder
// are story variants. Each segment is exactly 36864 (0x9000) bytes and is
// structured as:
//   bytes 0..3   : group_id (uint32)
//   bytes 4..67  : 16 x uint32 block offsets (relative to segment base)
//   then 16 variable-length blocks at the offsets above
//   then padding to 36864.
// Each block starts with a 4-byte header:
//   byte 0: poly_count (1 byte)
//   byte 1: vert_count (1 byte)
//   byte 2: norm_count (1 byte)
//   byte 3: pad
// Followed by poly_count x 16-byte polygon records, vert_count x 8-byte
// vertices, norm_count x 8-byte normals, and a 4-byte end pad.
// Each polygon's terrain (ground type) byte lives at polygon offset 0x0D.
// Ocean values are 32 (shallow) / 33 (light) / 34 (dark); everything else
// is land. Source: `Plan & Research Documents/wmx.obj polygon format deep
// research findings.md`. Earlier reconstructed parsers that scanned a flat
// `segment[poly * 16 + 13]` stride from segment offset 0 (ignoring the
// per-block headers) read garbage bytes — v0.14.85 BAT classified all 768
// segments as land because the garbage-byte distribution rarely landed in
// the 32-34 range. The correct walker per the research doc is implemented
// in v0.14.85.1's LoadTerrainGrid.
static const int      WMX_FL_INDEX        = 9;
static const int      WMX_SEG_COLS        = 32;
static const int      WMX_SEG_ROWS        = 24;
static const int      WMX_PLAYABLE_SEGS   = 768;
static const int      WMX_TOTAL_SEGS      = 835;
static const uint32_t WMX_SEGMENT_SIZE    = 36864;
static const int      WMX_BLOCKS_PER_SEG  = 16;
static const int      WMX_SEG_HEADER_SIZE = 4 + 16 * 4;   // group_id + 16 block offsets = 68 bytes
static const int      WMX_BLOCK_HDR_SIZE  = 4;            // poly_count + vert_count + norm_count + pad
static const int      WMX_POLY_SIZE       = 16;
static const int      WMX_TERRAIN_OFFSET  = 0x0D;

// ============================================================================
// wmsetus.obj section constants (v0.14.92 — field-entry bytecode decoder)
// ============================================================================
// world.fi entry 10 points at wmsetus.obj inside world.fs (the EN-locale
// wmset; bare wmset.obj is unused leftover per the FF Inside wiki). The file
// begins with a 48-entry section-offset header: 48 little-endian uint32s
// (192 bytes total), each holding the byte offset within wmsetus.obj where
// that section's data starts.
//
// Per disassembly of FF8_EN.exe sub_542DA0 (the wmsetus section-pointer
// setup function, 1-indexed iteration order matches array index +1):
//   Section 1 (392 bytes)  → [0x2040068]  encounter table, sub_541C80
//   Section 2 (772 bytes)  → [0x2040330]  region/terrain map (32x24+hdr)
//   Section 3 (88 bytes)   → [0x2040090]
//   Section 4 (1348 bytes) → [0x2036be8]  encounter destinations
//   Section 5 (8 bytes)    → [0x203ed40]
//   Section 6 (68 bytes)   → [0x2040080]
//   Section 7 (56 bytes)   → [0x2040074]  adjacent to 8, possibly metadata
//   Section 8 (2652 bytes) → [0x2040070]  FIELD ENTRY BYTECODE, sub_545EA0
//   ...
//   (Section 17/18 turn out to be wm2field-style destination data, not
//   trigger geometry as the deep research had hypothesized.)
//
// THIS BUILD hex-dumps Sections 7 and 8 to ff8_world.log under [TRIGGER-DUMP].
// Section 8 is the field-entry bytecode (the data we need to decode for AD).
// Section 7 is small enough (56 bytes) to dump for free in case it turns out
// to be related metadata (entry-point index, region-to-bytecode-offset map,
// etc.). Earlier sections (encounters / region map / destinations) are NOT
// dumped here — they're future work for an 'encounter warning' feature, not
// needed for the v0.14.92 → v0.14.94 AD-steering plan. The 48-entry section
// table is still logged in full so we have a layout sanity-check.
static const int      WMSETUS_FL_INDEX            = 10;
static const int      WMSETUS_SECTION_COUNT       = 48;
static const int      WMSETUS_HEADER_BYTES        = WMSETUS_SECTION_COUNT * 4;   // 192
static const uint32_t WMSETUS_DUMP_CAP_BYTES      = 16384; // safety cap per section in the hex-dump

// Sections to hex-dump (1-indexed in user-facing logs; array index = N-1).
// Order is dump-order in the log; the table sanity-check log line lists all
// 48 sections regardless. Adding more sections to this list is a one-line
// change for any future build that needs a different slice of wmsetus.obj.
static const int      WMSETUS_DUMP_SECTIONS_1IDX[] = { 7, 8 };
static const int      WMSETUS_DUMP_COUNT           = sizeof(WMSETUS_DUMP_SECTIONS_1IDX) / sizeof(WMSETUS_DUMP_SECTIONS_1IDX[0]);

// Vehicle classification used by the BFS reachability rules.
// Locomotion enum values per `Plan & Research Documents/World Map Terrain
// and Locomotion Reference.md`:
//   0  = Squall on foot       6  = Selphie on foot
//   3  = Ship (BAT-validated v0.14.83)
//   31 = Chocobo             32-40 = Cars
//   48 = Garden (mobile)     50 = Ragnarok
// Mode 4 is NOT in the canonical list. Earlier builds (v0.14.83-v0.14.85.2)
// tagged it 'Ragnarok' based on a v0.14.82 BAT log where Claude assumed the
// 'Ragnarok session' label, but Aaron's v0.14.85.2 BAT clarified he doesn't
// have Ragnarok in the current save — mode 4 was a transient byte read at
// a Fire Cavern field-transition moment, not a vehicle. v0.14.85.3 drops
// the assumption: mode 4 (and any other non-canonical value) defaults to
// VEH_ON_FOOT for BFS purposes and is silently ignored by CheckVehicleChange.
enum VehicleType {
    VEH_ON_FOOT,
    VEH_CHOCOBO,
    VEH_CAR,
    VEH_GARDEN,
    VEH_RAGNAROK
};

// ============================================================================
// Location catalog — v0.14.85.1: rebuilt from canonical research doc
// ============================================================================
// Source: `Plan & Research Documents/World Map Location Coordinates Research
// Findings.md`. The doc enumerates the 26 numbered world-map markers from
// FinalFantasyKingdom + 7 chocobo forests + 4 alien encounters with X/Y/Z
// coords from the ff8-speedruns/ff8-memory dataset — the SAME coordinate
// system the player position address (`FF8_EN.exe+1C3EE80`) reports at
// runtime, so distances and BFS segment-mapping work without conversion.
// Plus Fire Cavern from the v0.11.11 wmx.obj analysis (it was missing from
// the canonical 26).
//
// v0.14.85 BAT exposed two flaws in the prior catalog: (a) it used a
// completely different coordinate system (positive-Y, magnitudes mismatched
// with the runtime player-position address by ~50000 units) so BFS
// reachability was effectively random, and (b) it included interior /
// event-only locations like 'Timber Maniacs Building', 'SeeD Graduation
// Ball', 'SeeD on Train', 'Balamb Garden MD Level', and 'Dr. Odines Lab'
// that aren't world-map entry points at all.
//
// This rebuild matches the canonical FF8 world-map entry-point set: every
// location here is a place you can walk/drive/fly TO from the overworld
// (not an interior accessed from another field).
// LocationEntry holds the catalog center coordinate — a stable Ragnarok-
// autopilot coordinate from `Plan & Research Documents/World Map Location
// Coordinates Research Findings.md`. v0.14.89 added EMPIRICAL entry-coord
// refinement in a parallel `s_refinedEntries[]` table (see below) that
// indexes by LOCATION_COUNT — not stored on this struct because it lives
// in const data. The s_refinedEntries table is mutable and is what gets
// updated when an auto-drive completes.
struct LocationEntry {
    const char* name;
    int32_t x;
    int32_t y;
};

static const LocationEntry s_locations[] = {
    // Numbered markers 1-26 (canonical FinalFantasyKingdom set)
    {"Balamb Garden",              24576,  -29406},
    {"Balamb Town",                13249,  -26779},   // canonical name 'Balamb'; kept 'Town' for clarity vs Garden
    {"Dollet",                    -15639,  -39437},
    {"Timber",                    -22564,   -4867},
    {"Galbadia Garden",           -37471,  -25062},
    {"Deling City",               -61806,  -28649},
    {"Tomb of the Unknown King",  -42471,  -36562},
    {"D-District Prison",         -55306,   -4841},
    {"Galbadia Missile Base",     -71695,  -15591},
    {"Fisherman's Horizon",        48811,   -1653},
    {"Trabia Garden",              48893,  -57979},
    {"Edea's House",              -23150,   62853},
    {"White SeeD Ship",             4887,   51285},
    {"Great Salt Lake",            49888,   -2683},
    {"Esthar City",                57011,   -2295},   // canonical 'Esthar'; kept 'City' for clarity
    {"Lunatic Pandora Lab",        79521,   -9135},
    {"Lunar Gate",                 88021,    7865},
    {"Sorceress Memorial",         81521,   11865},
    {"Shumi Village",              10362,  -76967},
    {"Winhill",                   -50285,    6320},
    {"Centra Ruins",                6887,   55285},
    {"Deep Sea Research Center", -119138,   86000},
    {"Cactuar Island",             54806,   62040},
    {"Tears' Point",               83021,   31865},
    {"Island Closest to Hell",   -105137,   -3802},
    {"Island Closest to Heaven",  102251,  -53082},

    // Chocobo Forests (7) — generic numbering; the in-game forests don't have
    // canon names. Geographic hints in the comments help during testing.
    {"Chocobo Forest 1",           11332,  -63659},   // Trabia Continent (south of Trabia Garden)
    {"Chocobo Forest 2",           10927,  -81010},   // Trabia Continent (north)
    {"Chocobo Forest 3",           51893,   -3959},   // Esthar coast / FH region
    {"Chocobo Forest 4",           97253,  -48250},   // Far East / Esthar mountains
    {"Chocobo Forest 5",           17383,   22013},   // South Galbadia
    {"Chocobo Forest 6",           44504,   76259},   // Centra Continent
    {"Chocobo Forest 7",          -20953,   68906},   // Centra / Edea's House region

    // Alien Encounter (UFO/PuPu) sites (4)
    {"Alien Ship 1",               79823,  -61212},   // Esthar / Trabia border
    {"Alien Ship 2",               40495,   54649},   // Centra Continent
    {"Alien Ship 3",              -12952,  -10202},   // Galbadia Continent
    {"Alien Ship 4",              -48806,    5808},   // West Galbadia

    // Fire Cavern — missing from the canonical 26 marker list because the
    // FinalFantasyKingdom set is Ragnarok-era; Fire Cavern is the early-game
    // dungeon on Balamb Island. Coords from the v0.11.11 wmx.obj polygon
    // analysis (segment(20,20), 6 terrain-29 polygons, small cave entrance).
    {"Fire Cavern",                36864,  -28672}
};
static const int LOCATION_COUNT = sizeof(s_locations) / sizeof(s_locations[0]);

// ============================================================================
// Refined entry coordinates table (v0.14.89, Option B — empirical capture)
// ============================================================================
// Parallel to s_locations[]. When a successful on-foot auto-drive ends with
// the world map exiting to MODE_FIELD inside the DRIVE_ARRIVED_ON_EXIT_DIST
// proximity to the catalog target, the field-entry handler captures the
// player's last-known world-map (X, Y) into this table at the same index.
// On subsequent drives to the same destination, StartAutoDrive prefers the
// refined coord over the catalog center for steering — making the second
// visit to a narrow-entrance location (e.g. Balamb Town, Dollet) direct,
// no sweep required.
//
// PERSISTENCE: this build keeps the table in-memory only. Refined coords
// are lost on game restart. Persistent storage (a JSON file alongside
// DEVNOTES, or in %APPDATA%) is queued for v0.14.90 alongside the
// already-deferred 'persistent accessibility settings' priority.
//
// CORRECTNESS: refined coords reflect WHERE THE PLAYER WAS at the moment
// the trigger fired — i.e. the actual on-the-ground entry point. Not the
// trigger-zone center, not the catalog center. Distance from this coord
// to the autopilot center is typically 300-1500 units (matches the prior
// deep research's 'triggers offset by ~300-800 units' estimate).
static int32_t s_refinedX[LOCATION_COUNT];
static int32_t s_refinedY[LOCATION_COUNT];
static bool    s_refinedHas[LOCATION_COUNT];

// Find the s_locations[] index of a location by its catalog center coords.
// The drive uses (s_driveTargetX, s_driveTargetY) which were captured from
// s_catalog[catIdx].(x, y) in StartAutoDrive — those values come from
// s_locations[] (or from the refined table if has_refined was true), so
// we need to search both sources to identify which s_locations[] slot a
// given target coord belongs to. Returns -1 if no match.
static int FindLocationIndexByTargetCoords(int32_t tx, int32_t ty)
{
    for (int i = 0; i < LOCATION_COUNT; i++) {
        if (s_locations[i].x == tx && s_locations[i].y == ty) return i;
        if (s_refinedHas[i] && s_refinedX[i] == tx && s_refinedY[i] == ty) return i;
    }
    return -1;
}

// ============================================================================
// Navigation state
// ============================================================================
static struct LocationEntry s_catalog[LOCATION_COUNT];  // distance-sorted working copy
static int  s_catalogCount = 0;        // v0.14.85: post-filter count (≤ LOCATION_COUNT)
static bool s_catalogBuilt = false;
static int s_catalogIndex = 0;       // current selected location (0 = nearest)
static int s_lastVehicle = -1;       // last known vehicle state
static bool s_onWorldMap = false;    // true when on world map
static DWORD s_lastMovementTick = 0; // last time position changed significantly

// ============================================================================
// Auto-drive state (v0.14.86, restored from v0.11.08-v0.11.10)
// ============================================================================
// World map auto-drive uses keyboard injection rather than the field-nav
// fake gamepad. Discovered v0.11.05-v0.11.07: the world map's input handler
// `worldmap_input_update_sub_559240` has its own input pipeline separate
// from the field's `engine_eval_keyboard_gamepad_input`, so the fake gamepad
// signal never reaches it. `keybd_event` injection at the OS level reaches
// both pipelines and works reliably on the world map. v0.11.08 BAT validated.
//
// Steering uses heading-relative bearing (0-4095 native units; 0=North CW)
// computed each tick. Three behaviors based on relative bearing magnitude:
//   ahead   (within ~18°)  : just walk forward
//   diag    (~18-45°)      : turn AND walk forward (saves time vs. turn-then-walk)
//   side    (>45°)         : turn only until aligned, then forward
// Final approach (<DRIVE_ARRIVE_DIST*2 ≈ 1000 units): just walk forward —
// most catalog coordinates are not exactly on entrance triggers, and walking
// straight through the area sweeps reliably onto the trigger zone.
//
// State target is captured by VALUE (X/Y/name) at StartAutoDrive time, not
// as a catalog index. The catalog rebuilds whenever the player crosses a
// BFS rule-class boundary, which would invalidate any stored index. Stable
// X/Y/name lets the drive survive arbitrary catalog rebuilds.
static const double DRIVE_ARRIVE_DIST           = 600.0;   // vehicle arrival proximity (on-foot uses world-map-exit detection)
static const double DRIVE_APPROACH_DIST         = 3000.0;  // one-shot "Approaching X" threshold
static const double DRIVE_FINAL_APPROACH_DIST   = 1000.0;  // walk-forward (no steering) below this
static const double DRIVE_ARRIVED_ON_EXIT_DIST  = 1500.0;  // v0.14.87: world-map exit while closer than this counts as arrival
static const DWORD  DRIVE_ANNOUNCE_INTERVAL_MS  = 5000;    // periodic distance announce
static const DWORD  DRIVE_STUCK_CHECK_INTERVAL_MS = 3000;  // stuck-detection sample window
static const double DRIVE_STUCK_THRESHOLD       = 100.0;   // movement floor in one window
static const int    DRIVE_STUCK_MAX             = 6;       // 6 windows × 3s = 18s no movement → give up

// v0.14.87 — sweep search constants. Activated when on-foot drive is stuck
// inside the final-approach zone (target within 1000 units but no entrance
// trigger has fired). Past chats v0.11.10 validated 6-phase alternating
// turn-then-walk as effective for narrow entrances like Balamb Town.
static const int    SWEEP_MAX_PHASES        = 6;     // give up after 6 attempts
static const DWORD  SWEEP_TURN_BASE_MS      = 800;   // phase 1 turn duration; +200ms per phase
static const DWORD  SWEEP_WALK_DURATION_MS  = 3000;  // walk forward 3s per phase
static const DWORD  FINAL_APPROACH_TIMEOUT_MS = 6000;// in final approach >6s without exit → sweep

static bool     s_driveActive            = false;
static int32_t  s_driveTargetX           = 0;
static int32_t  s_driveTargetY           = 0;
static char     s_driveTargetName[64]    = {};
static DWORD    s_driveStartTime         = 0;
static DWORD    s_driveLastAnnounce      = 0;
static double   s_driveLastDist          = 0.0;
static int32_t  s_driveLastPosX          = 0;     // v0.14.89: last known player X (for refined-entry capture on MODE_FIELD)
static int32_t  s_driveLastPosY          = 0;
static int32_t  s_driveStuckX            = 0;
static int32_t  s_driveStuckY            = 0;
static DWORD    s_driveStuckCheckTime    = 0;
static int      s_driveStuckCount        = 0;
static bool     s_driveApproachAnnounced = false;  // one-shot guard
static bool     s_driveOnFootAtStart     = true;   // v0.14.87: captured at StartAutoDrive; arrival semantics differ
static DWORD    s_finalApproachEnterTick = 0;      // v0.14.87: when player crossed below FINAL_APPROACH_DIST (0 = not yet)

// v0.14.87 sweep search state. Sweep alternates between TURNING and WALKING
// sub-states. Each phase: turn for SWEEP_TURN_BASE_MS + (phase-1)*200ms,
// then walk forward SWEEP_WALK_DURATION_MS. Odd phases turn right, even left.
// Field exit during any sub-state → arrival; sweep exhaustion → give up.
static bool     s_sweepActive  = false;
static int      s_sweepPhase   = 0;     // 1..SWEEP_MAX_PHASES (0 = not in sweep)
static bool     s_sweepTurning = true;  // sub-state: true = turn, false = walk
static DWORD    s_sweepStateEnd = 0;    // tick when current sub-state ends

// Held-key tracking for keybd_event injection. Press/release tracking lets
// SetDriveKeys() be idempotent — pressing UP twice doesn't double-press.
static bool s_keyUpHeld    = false;
static bool s_keyLeftHeld  = false;
static bool s_keyRightHeld = false;

// ============================================================================
// Terrain grid + BFS reachability state (v0.14.85)
// ============================================================================
// s_terrainGrid[row][col]: 0 = LAND, 1 = OCEAN. Loaded once at module init from
// wmx.obj inside world.fs. s_reachable[row][col] is rebuilt per catalog build
// via BFS flood-fill from the player's current segment.
static uint8_t s_terrainGrid[WMX_SEG_ROWS][WMX_SEG_COLS];
static uint8_t s_reachable  [WMX_SEG_ROWS][WMX_SEG_COLS];
static bool    s_terrainLoaded = false;

// ============================================================================
// Coordinate utility functions
// ============================================================================
static void GetWorldMapPosition(int32_t* x, int32_t* y, int32_t* z)
{
    __try {
        *x = *(int32_t*)WM_POS_X;
        *y = *(int32_t*)WM_POS_Y;
        *z = *(int32_t*)WM_POS_Z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *x = *y = *z = 0;
    }
}

static uint16_t GetWorldMapHeading()
{
    uint16_t heading = 0;
    __try {
        heading = *(uint16_t*)WM_HEADING;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        heading = 0;
    }
    return heading;
}

static uint8_t GetLocomotionMode()
{
    uint8_t mode = 0;
    __try {
        mode = *(uint8_t*)WM_LOCOMOTION;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode = 0;
    }
    return mode;
}

static bool IsOnWorldMap()
{
    uint16_t scene = 1;  // default to field (not worldmap)
    __try {
        scene = *(uint16_t*)WM_SCENE_FLAG;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        scene = 1;
    }
    return (scene == 0);
}

// Wrap-aware distance calculation on a torus
static double CalculateWrappedDistance(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    double dx = abs(x2 - x1);
    double dy = abs(y2 - y1);
    
    // Check if wrapping gives shorter distance
    if (dx > WM_WIDTH / 2)  dx = WM_WIDTH - dx;
    if (dy > WM_HEIGHT / 2) dy = WM_HEIGHT - dy;
    
    return sqrt(dx * dx + dy * dy);
}

// ============================================================================
// File I/O helpers (v0.14.85, restored from v0.11.12 impl)
// ============================================================================
// Mirrors the field_archive.cpp pattern but uses raw uint8_t* with malloc/free
// rather than std::vector to keep the world_map module independent of
// field_archive's internals. The duplicated LZSS function below has the same
// rationale; both could be lifted to a shared header in a future refactor.
static bool WM_ReadFileToBuffer(const char* path, uint8_t** outData, uint32_t* outSize)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    *outData = (uint8_t*)malloc((size_t)sz);
    if (!*outData) { fclose(f); return false; }
    size_t rd = fread(*outData, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(*outData); *outData = nullptr; return false; }
    *outSize = (uint32_t)sz;
    return true;
}

static bool WM_ReadFileChunk(const char* path, uint32_t offset, uint32_t size, uint8_t** outData)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return false; }
    *outData = (uint8_t*)malloc(size);
    if (!*outData) { fclose(f); return false; }
    size_t rd = fread(*outData, 1, size, f);
    fclose(f);
    if (rd != size) { free(*outData); *outData = nullptr; return false; }
    return true;
}

// ============================================================================
// LZSS decompression (v0.14.85, restored from v0.11.12 impl)
// ============================================================================
// Standard FF8/FF7 LZSS variant. 4096-byte ring buffer, ringPos starts at
// 0xFEE. Flag byte's bits read LSB-first: 1=literal byte, 0=back-reference
// (12-bit offset, 4-bit length+3). Identical to field_archive.cpp's
// DecompressLZSS but file-private here for module independence.
static bool WM_DecompressLZSS(const uint8_t* input, uint32_t inputSize,
                              uint8_t* output, uint32_t outputSize)
{
    uint8_t ring[4096];
    memset(ring, 0, sizeof(ring));
    int ringPos = 0xFEE;
    uint32_t inPos = 0, outPos = 0;

    while (outPos < outputSize && inPos < inputSize) {
        uint8_t flags = input[inPos++];
        for (int bit = 0; bit < 8 && outPos < outputSize; bit++) {
            if (flags & (1 << bit)) {
                if (inPos >= inputSize) return false;
                uint8_t b = input[inPos++];
                output[outPos++] = b;
                ring[ringPos] = b;
                ringPos = (ringPos + 1) & 0xFFF;
            } else {
                if (inPos + 1 >= inputSize) return false;
                uint8_t b1 = input[inPos++];
                uint8_t b2 = input[inPos++];
                int off = b1 | ((b2 & 0xF0) << 4);
                int len = (b2 & 0x0F) + 3;
                for (int i = 0; i < len && outPos < outputSize; i++) {
                    uint8_t b = ring[(off + i) & 0xFFF];
                    output[outPos++] = b;
                    ring[ringPos] = b;
                    ringPos = (ringPos + 1) & 0xFFF;
                }
            }
        }
    }
    return (outPos == outputSize);
}

// ============================================================================
// Coordinate conversion: game world coords → segment grid (v0.14.85)
// ============================================================================
// World map torus is 262144 x 196608, divided into a 32 x 24 grid of 8192-unit
// segments. The X axis has a non-zero origin offset: per wmx.obj analysis,
// game_X = seg_col * 8192 + 4096 - 131072. The +131072 below is the inverse
// of that offset — without it, BFS starts in an ocean cell and filters
// everything as unreachable. This was the v0.11.16 fix that finally made
// terrain BFS work end-to-end ("Driving worked as expected!" — Aaron's BAT).
// Y axis aligns naturally because the torus wrap absorbs any constant offset.
static int WorldXToSegCol(int32_t x)
{
    int32_t shifted = x + 131072;
    int32_t nx = ((shifted % 262144) + 262144) % 262144;
    return (nx / 8192) % WMX_SEG_COLS;
}

static int WorldYToSegRow(int32_t y)
{
    int32_t ny = ((y % 196608) + 196608) % 196608;
    return (ny / 8192) % WMX_SEG_ROWS;
}

// ============================================================================
// Auto-drive helpers (v0.14.86)
// ============================================================================
// Bearing in native FF8 heading units (0-4095, 0=North, CW). Wrap-aware on
// the world torus. Mirrors the math in AnnounceBearing but returns the raw
// angle for the steering decision; AnnounceBearing converts to compass
// directions for speech.
static int TorusBearing(int32_t fromX, int32_t fromY, int32_t toX, int32_t toY)
{
    int32_t dx = toX - fromX;
    int32_t dy = toY - fromY;
    if (abs(dx) > (int32_t)WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else        dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > (int32_t)WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else        dy += (int32_t)WM_HEIGHT;
    }
    // -dy because FF8 Y axis increases downward; atan2(dx, -dy) gives
    // angle from +Y (North) clockwise, matching the heading convention.
    double radians = atan2((double)dx, -(double)dy);
    if (radians < 0) radians += 2.0 * 3.14159265358979;
    int bearing = (int)(radians / (2.0 * 3.14159265358979) * 4096.0);
    return bearing & 0xFFF;  // wrap to 0-4095
}

// keybd_event-based key injection. Arrow keys use scan codes 0x48 (UP),
// 0x4B (LEFT), 0x4D (RIGHT), all extended-key scancodes (the high bit of
// the keyboard scancode set). KEYEVENTF_EXTENDEDKEY is required so the OS
// (and the game's input handler) treats these as the cursor arrows rather
// than the numpad equivalents.
static void PressKey(BYTE vk, BYTE scan)
{
    keybd_event(vk, scan, KEYEVENTF_EXTENDEDKEY, 0);
}

static void ReleaseKey(BYTE vk, BYTE scan)
{
    keybd_event(vk, scan, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
}

static void ReleaseAllDriveKeys()
{
    if (s_keyUpHeld)    { ReleaseKey(VK_UP,    0x48); s_keyUpHeld    = false; }
    if (s_keyLeftHeld)  { ReleaseKey(VK_LEFT,  0x4B); s_keyLeftHeld  = false; }
    if (s_keyRightHeld) { ReleaseKey(VK_RIGHT, 0x4D); s_keyRightHeld = false; }
}

// Idempotent press/release: only generates events on state changes. Called
// every Update() tick from UpdateAutoDrive with the desired key state for
// the next frame; the game sees a continuous press as long as the same key
// is held across calls.
static void SetDriveKeys(bool up, bool left, bool right)
{
    if (up    && !s_keyUpHeld)    { PressKey(VK_UP,    0x48); s_keyUpHeld    = true; }
    if (!up   &&  s_keyUpHeld)    { ReleaseKey(VK_UP,  0x48); s_keyUpHeld    = false; }
    if (left  && !s_keyLeftHeld)  { PressKey(VK_LEFT,  0x4B); s_keyLeftHeld  = true; }
    if (!left &&  s_keyLeftHeld)  { ReleaseKey(VK_LEFT, 0x4B); s_keyLeftHeld = false; }
    if (right && !s_keyRightHeld) { PressKey(VK_RIGHT, 0x4D); s_keyRightHeld = true; }
    if (!right&&  s_keyRightHeld) { ReleaseKey(VK_RIGHT,0x4D); s_keyRightHeld = false; }
}

// ============================================================================
// Vehicle classification (v0.14.85.3)
// ============================================================================
// Maps the raw locomotion byte to a coarse VehicleType used by reachability
// rules. Conservative: only modes from the canonical list get non-foot
// classifications. Unknown modes (including the transient mode 4 seen at
// field-transition moments) default to VEH_ON_FOOT — safest for filtering
// because the player IS likely on foot if they aren't in a known vehicle.
static VehicleType GetVehicleType(uint8_t mode)
{
    if (mode == 0 || mode == 6) return VEH_ON_FOOT;       // Squall / Selphie foot
    if (mode == 3)               return VEH_GARDEN;       // Ship: ocean access, BAT-validated v0.14.83
    if (mode == 31)              return VEH_CHOCOBO;
    if (mode >= 32 && mode <= 40) return VEH_CAR;
    if (mode == 48)              return VEH_GARDEN;       // Garden mobile (ocean access)
    if (mode == 50)              return VEH_RAGNAROK;     // No filter (flies anywhere)
    return VEH_ON_FOOT;                                    // safe default for unknown / transient values
}

// Three BFS rule classes used by the v0.14.85.3 type-change-triggered rebuild:
// 0 = land-only (foot, chocobo, car), 1 = ocean-allowed (Ship, Garden),
// 2 = no-filter (Ragnarok). A rebuild only fires when the rule class
// changes, so foot ↔ car ↔ chocobo transitions (all land-only) don't trigger
// gratuitous rebuilds.
static int GetBfsRuleClass(VehicleType v)
{
    if (v == VEH_RAGNAROK) return 2;
    if (v == VEH_GARDEN)   return 1;
    return 0;  // VEH_ON_FOOT, VEH_CHOCOBO, VEH_CAR all share land-only rules
}

// Whitelist of canonical locomotion-byte values. The byte at WM_LOCOMOTION
// drifts through transient values (animation phase counters, field-transition
// state, etc.) and announcing those would be noise. Only canonical values per
// the research doc are eligible for vehicle-change announcements.
static bool IsCanonicalLocomotion(uint8_t mode)
{
    return mode == 0 || mode == 3 || mode == 6 ||
           mode == 31 ||
           (mode >= 32 && mode <= 40) ||
           mode == 48 || mode == 50;
}

// True iff the segment is reachable for the given vehicle. For the binary
// land/ocean grid we have, foot/chocobo/car require land, Garden allows ocean
// too (and Ragnarok callers should skip the BFS entirely — it can fly
// anywhere, so calling this is moot).
static bool IsSegmentTraversable(int row, int col, VehicleType veh)
{
    if (row < 0 || row >= WMX_SEG_ROWS || col < 0 || col >= WMX_SEG_COLS) return false;
    uint8_t cell = s_terrainGrid[row][col];   // 0 = land, 1 = ocean
    if (veh == VEH_GARDEN || veh == VEH_RAGNAROK) return true;   // any segment
    return (cell == 0);                       // land-only for foot/chocobo/car
}

// ============================================================================
// LoadTerrainGrid — reads wmx.obj from world.fs once at module init,
// classifies each of 768 playable segments as LAND or OCEAN by polygon
// terrain types. Restored from v0.11.12 impl.
// ============================================================================
static bool LoadTerrainGrid()
{
    if (s_terrainLoaded) return true;

    // Auto-detect game install path via FF8_EN.exe location.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // ---- Read world.fi (small index file, 12 bytes per entry).
    uint8_t* fiData = nullptr;
    uint32_t fiSize = 0;
    if (!WM_ReadFileToBuffer(fiPath, &fiData, &fiSize)) {
        Log::World("WorldMap: [TERRAIN] Failed to read world.fi at '%s'", fiPath);
        return false;
    }
    if (fiSize < (uint32_t)(WMX_FL_INDEX + 1) * 12) {
        Log::World("WorldMap: [TERRAIN] world.fi too small (%u bytes)", fiSize);
        free(fiData);
        return false;
    }

    // ---- Parse wmx.obj's FI entry (index 9).
    const uint8_t* fiEntry = fiData + WMX_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);

    Log::World("WorldMap: [TERRAIN] wmx.obj FI entry: uncomp=%u offset=%u comp=%u",
               uncompSize, fsOffset, compression);

    // Validate expected size: 835 segments x 36864 = 30,801,540.
    uint32_t expectedSize = (uint32_t)835 * WMX_SEGMENT_SIZE;
    if (uncompSize != expectedSize) {
        Log::World("WorldMap: [TERRAIN] WARNING: wmx.obj size %u != expected %u",
                   uncompSize, expectedSize);
    }

    // ---- Read wmx.obj from world.fs (compressed or raw per FI compression flag).
    uint8_t* wmxData = nullptr;
    if (compression == 0) {
        free(fiData);
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmxData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read wmx.obj from world.fs");
            return false;
        }
    } else {
        // Compressed: derive compressed-size by reading the next FI entry's offset.
        uint32_t compSize = uncompSize; // fallback
        for (int j = WMX_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TERRAIN] Failed to read compressed wmx.obj");
            return false;
        }
        // FF8 LZSS storage: 4-byte uncompressed-size header precedes the bitstream.
        wmxData = (uint8_t*)malloc(uncompSize);
        if (!wmxData) { free(compData); return false; }
        bool ok = WM_DecompressLZSS(compData + 4, compSize - 4, wmxData, uncompSize);
        free(compData);
        if (!ok) {
            Log::World("WorldMap: [TERRAIN] LZSS decompression failed");
            free(wmxData);
            return false;
        }
    }

    Log::World("WorldMap: [TERRAIN] wmx.obj loaded (%u bytes), classifying %d segments...",
               uncompSize, WMX_PLAYABLE_SEGS);

    // ---- Classify each of the 768 playable segments by walking its proper
    // 68-byte header + 16 block headers + per-block polygon arrays.
    // v0.14.85.1: rewritten from the v0.14.85 flat-stride bug that read
    // 2304 "polygons" per segment and saw 0 oceans because the garbage
    // bytes between block boundaries rarely landed in 32-34.
    memset(s_terrainGrid, 0, sizeof(s_terrainGrid));
    int oceanSegs = 0, landSegs = 0;
    int totalRealPolys = 0, totalOceanPolys = 0;

    for (int seg = 0; seg < WMX_PLAYABLE_SEGS; seg++) {
        int row = seg / WMX_SEG_COLS;
        int col = seg % WMX_SEG_COLS;
        const uint8_t* segData = wmxData + (uint32_t)seg * WMX_SEGMENT_SIZE;

        int segPolyCount = 0, segOceanCount = 0;

        // Walk the 16 block offsets in the segment header. Skip the first 4
        // bytes (group_id), then read each uint32 little-endian offset.
        for (int b = 0; b < WMX_BLOCKS_PER_SEG; b++) {
            uint32_t blockOffset = *(const uint32_t*)(segData + 4 + b * 4);
            if (blockOffset == 0) continue;                       // unused slot
            if (blockOffset + WMX_BLOCK_HDR_SIZE > WMX_SEGMENT_SIZE) continue;  // out of range

            const uint8_t* blockBase = segData + blockOffset;
            uint8_t polyCount = blockBase[0];
            // (vert_count = blockBase[1], norm_count = blockBase[2], pad = blockBase[3]
            //  — not needed for terrain classification but kept for documentation.)

            // Bounds-guard: polygon array must fit within the segment.
            uint32_t polyArrayEnd = blockOffset + WMX_BLOCK_HDR_SIZE +
                                    (uint32_t)polyCount * WMX_POLY_SIZE;
            if (polyArrayEnd > WMX_SEGMENT_SIZE) continue;

            for (int p = 0; p < polyCount; p++) {
                uint8_t terrain = blockBase[WMX_BLOCK_HDR_SIZE + p * WMX_POLY_SIZE
                                            + WMX_TERRAIN_OFFSET];
                if (terrain >= 32 && terrain <= 34) segOceanCount++;
                segPolyCount++;
            }
        }

        totalRealPolys  += segPolyCount;
        totalOceanPolys += segOceanCount;

        // Majority-ocean polygons => OCEAN segment. Empty/degenerate segments
        // (segPolyCount == 0) default to LAND — the playable grid shouldn't
        // contain any, but if one exists, defaulting to land is the
        // conservative choice for accessibility filtering.
        if (segPolyCount > 0 && segOceanCount * 2 > segPolyCount) {
            s_terrainGrid[row][col] = 1;  // OCEAN
            oceanSegs++;
        } else {
            s_terrainGrid[row][col] = 0;  // LAND
            landSegs++;
        }
    }
    free(wmxData);
    s_terrainLoaded = true;

    Log::World("WorldMap: [TERRAIN] Grid built: %d land, %d ocean (of %d). Total real polys=%d (oceans=%d).",
               landSegs, oceanSegs, WMX_PLAYABLE_SEGS, totalRealPolys, totalOceanPolys);

    // Visual grid dump (# = land, ~ = ocean) — valuable for diagnosing
    // coordinate-mapping issues; cheap (24 lines, once per process).
    for (int r = 0; r < WMX_SEG_ROWS; r++) {
        char rowStr[WMX_SEG_COLS + 1];
        for (int c = 0; c < WMX_SEG_COLS; c++)
            rowStr[c] = s_terrainGrid[r][c] ? '~' : '#';
        rowStr[WMX_SEG_COLS] = '\0';
        Log::World("WorldMap: [TERRAIN] row%02d: %s", r, rowStr);
    }

    return true;
}

// ============================================================================
// LoadTriggerZones — hex-dumps a configurable list of wmsetus.obj sections
// (controlled by WMSETUS_DUMP_SECTIONS_1IDX) to ff8_world.log for trigger-
// system reverse engineering. Mirrors LoadTerrainGrid's archive-reader
// pattern (world.fi entry lookup + LZSS decompress) but reads world.fi entry
// 10 (wmsetus.obj) instead of entry 9 (wmx.obj). No game-side state is
// captured — the function is purely diagnostic; its output drives the
// decoder design in subsequent builds. v0.14.91 dumped Sections 17 and 18
// (deep research's leading hypothesis, since disproved). v0.14.92 dumps
// Sections 7 and 8 (the disassembly-confirmed field-entry bytecode plus
// its small adjacent section).
// ============================================================================

// Hex-dump a byte range to ff8_world.log under [TRIGGER-DUMP] with section
// label and a printable-ASCII gutter. Caller passes already-validated bounds.
static void DumpTriggerSection(const char* sectLabel, const uint8_t* base, uint32_t bytes)
{
    const uint32_t cap = (bytes < WMSETUS_DUMP_CAP_BYTES) ? bytes : WMSETUS_DUMP_CAP_BYTES;
    Log::World("WorldMap: [TRIGGER-DUMP] %s begin (size=%u, dumping %u)", sectLabel, bytes, cap);
    for (uint32_t off = 0; off < cap; off += 16) {
        char hexpart[16 * 3 + 1] = {};
        char asciipart[17]       = {};
        uint32_t row = (cap - off >= 16) ? 16 : (cap - off);
        for (uint32_t i = 0; i < row; i++) {
            uint8_t b = base[off + i];
            snprintf(hexpart + i * 3, sizeof(hexpart) - i * 3, "%02X ", b);
            asciipart[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        // Pad short final row's hex column to keep the gutter aligned.
        for (uint32_t i = row; i < 16; i++) {
            snprintf(hexpart + i * 3, sizeof(hexpart) - i * 3, "   ");
        }
        asciipart[row] = '\0';
        Log::World("WorldMap: [TRIGGER-DUMP] %s +%04X: %s %s", sectLabel, off, hexpart, asciipart);
    }
    if (cap < bytes) {
        Log::World("WorldMap: [TRIGGER-DUMP] %s truncated at %u (full size %u)", sectLabel, cap, bytes);
    }
    Log::World("WorldMap: [TRIGGER-DUMP] %s end", sectLabel);
}

static bool LoadTriggerZones()
{
    // ---- Build paths exactly the way LoadTerrainGrid does. Auto-detect
    // game install via FF8_EN.exe location.
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fiPath[MAX_PATH], fsPath[MAX_PATH];
    snprintf(fiPath, MAX_PATH, "%sData\\lang-en\\world.fi", exePath);
    snprintf(fsPath, MAX_PATH, "%sData\\lang-en\\world.fs", exePath);

    // ---- Read world.fi (12 bytes per entry: uncompSize, fsOffset, compression).
    uint8_t* fiData = nullptr;
    uint32_t fiSize = 0;
    if (!WM_ReadFileToBuffer(fiPath, &fiData, &fiSize)) {
        Log::World("WorldMap: [TRIGGER-DUMP] Failed to read world.fi at '%s'", fiPath);
        return false;
    }
    if (fiSize < (uint32_t)(WMSETUS_FL_INDEX + 1) * 12) {
        Log::World("WorldMap: [TRIGGER-DUMP] world.fi too small (%u bytes) for entry %d",
                   fiSize, WMSETUS_FL_INDEX);
        free(fiData);
        return false;
    }

    const uint8_t* fiEntry = fiData + WMSETUS_FL_INDEX * 12;
    uint32_t uncompSize  = *(const uint32_t*)(fiEntry + 0);
    uint32_t fsOffset    = *(const uint32_t*)(fiEntry + 4);
    uint32_t compression = *(const uint32_t*)(fiEntry + 8);
    Log::World("WorldMap: [TRIGGER-DUMP] wmsetus.obj FI entry %d: uncomp=%u offset=%u comp=%u",
               WMSETUS_FL_INDEX, uncompSize, fsOffset, compression);

    // ---- Read wmsetus.obj from world.fs (compressed or raw per FI compression flag).
    uint8_t* wmsData = nullptr;
    if (compression == 0) {
        free(fiData);
        if (!WM_ReadFileChunk(fsPath, fsOffset, uncompSize, &wmsData)) {
            Log::World("WorldMap: [TRIGGER-DUMP] Failed to read wmsetus.obj raw");
            return false;
        }
    } else {
        // Compressed: derive compressed-size by reading the next FI entry's offset.
        uint32_t compSize = uncompSize;   // fallback if no later entry exists
        for (int j = WMSETUS_FL_INDEX + 1; (uint32_t)(j + 1) * 12 <= fiSize; j++) {
            uint32_t nextOff = *(const uint32_t*)(fiData + j * 12 + 4);
            if (nextOff > fsOffset) { compSize = nextOff - fsOffset; break; }
        }
        free(fiData);

        uint8_t* compData = nullptr;
        if (!WM_ReadFileChunk(fsPath, fsOffset, compSize, &compData)) {
            Log::World("WorldMap: [TRIGGER-DUMP] Failed to read compressed wmsetus.obj");
            return false;
        }
        // FF8 LZSS storage: 4-byte uncompressed-size header precedes the bitstream.
        wmsData = (uint8_t*)malloc(uncompSize);
        if (!wmsData) { free(compData); return false; }
        bool ok = WM_DecompressLZSS(compData + 4, compSize - 4, wmsData, uncompSize);
        free(compData);
        if (!ok) {
            Log::World("WorldMap: [TRIGGER-DUMP] LZSS decompression failed");
            free(wmsData);
            return false;
        }
    }

    // ---- Validate file size against the 48-entry header.
    if (uncompSize < (uint32_t)WMSETUS_HEADER_BYTES) {
        Log::World("WorldMap: [TRIGGER-DUMP] wmsetus.obj too small (%u bytes) for 48-entry header",
                   uncompSize);
        free(wmsData);
        return false;
    }

    // ---- Read the 48-entry section-offset table and log every offset for
    // diagnostic context. v0.14.93's decoder will reuse this header parse;
    // the full dump here makes the entire wmsetus layout visible at a glance
    // so we can sanity-check that section sizes match FF Inside wiki
    // annotations (region map at 2 = 772b, encounters at 1+4 = 392b+1348b,
    // textures at 38 = 257672b, etc.).
    uint32_t hdr[WMSETUS_SECTION_COUNT];
    for (int i = 0; i < WMSETUS_SECTION_COUNT; i++) {
        hdr[i] = *(const uint32_t*)(wmsData + i * 4);
    }
    for (int i = 0; i < WMSETUS_SECTION_COUNT; i++) {
        // Compute size as next-offset minus this-offset; for the last section,
        // size is uncompSize - hdr[last].
        uint32_t sectStart = hdr[i];
        uint32_t sectEnd   = (i + 1 < WMSETUS_SECTION_COUNT) ? hdr[i + 1] : uncompSize;
        uint32_t sectSize  = (sectEnd >= sectStart) ? (sectEnd - sectStart) : 0;
        Log::World("WorldMap: [TRIGGER-DUMP] section %02d (1-indexed): offset=0x%08X size=%u",
                   i + 1, sectStart, sectSize);
    }

    // ---- Hex-dump each section in WMSETUS_DUMP_SECTIONS_1IDX (1-indexed).
    // Bounds-check before dumping; if any section's offset/end pair looks
    // out of range, log and skip rather than reading past the buffer.
    auto safeDump = [&](int sectArrayIdx, const char* label) {
        if (sectArrayIdx < 0 || sectArrayIdx >= WMSETUS_SECTION_COUNT) return;
        uint32_t sectStart = hdr[sectArrayIdx];
        uint32_t sectEnd   = (sectArrayIdx + 1 < WMSETUS_SECTION_COUNT)
                              ? hdr[sectArrayIdx + 1]
                              : uncompSize;
        if (sectStart >= uncompSize || sectEnd > uncompSize || sectEnd <= sectStart) {
            Log::World("WorldMap: [TRIGGER-DUMP] %s out-of-range (start=0x%08X end=0x%08X file=%u) — skipping",
                       label, sectStart, sectEnd, uncompSize);
            return;
        }
        DumpTriggerSection(label, wmsData + sectStart, sectEnd - sectStart);
    };
    for (int k = 0; k < WMSETUS_DUMP_COUNT; k++) {
        int sect1Idx = WMSETUS_DUMP_SECTIONS_1IDX[k];
        char label[16];
        snprintf(label, sizeof(label), "sect%02d", sect1Idx);
        safeDump(sect1Idx - 1, label);   // 1-indexed in user log, 0-indexed in array
    }

    free(wmsData);
    return true;
}

// ============================================================================
// ComputeReachability — BFS flood-fill from player segment, populates
// s_reachable[][] for the given vehicle's traversal rules. Restored from
// v0.11.12 impl. 4-connected with torus wrapping (the world map wraps both
// axes, so segment col 31 borders col 0, and row 23 borders row 0).
// ============================================================================
static void ComputeReachability(int startCol, int startRow, VehicleType veh)
{
    memset(s_reachable, 0, sizeof(s_reachable));

    if (startRow < 0 || startRow >= WMX_SEG_ROWS ||
        startCol < 0 || startCol >= WMX_SEG_COLS) return;

    // Player's current cell is always reachable (they're standing there)
    // even if the cell classifies as ocean — e.g. transition frames where
    // position briefly snaps to a coastline edge classified as ocean.
    s_reachable[startRow][startCol] = 1;

    // BFS queue sized for 768 cells (the entire playable grid).
    static int qCol[WMX_PLAYABLE_SEGS];
    static int qRow[WMX_PLAYABLE_SEGS];
    int qHead = 0, qTail = 0;
    qCol[qTail] = startCol;
    qRow[qTail] = startRow;
    qTail++;

    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };

    while (qHead < qTail) {
        int cc = qCol[qHead];
        int cr = qRow[qHead];
        qHead++;

        for (int d = 0; d < 4; d++) {
            int nc = (cc + dx[d] + WMX_SEG_COLS) % WMX_SEG_COLS;
            int nr = (cr + dy[d] + WMX_SEG_ROWS) % WMX_SEG_ROWS;

            if (!s_reachable[nr][nc] && IsSegmentTraversable(nr, nc, veh)) {
                s_reachable[nr][nc] = 1;
                if (qTail < WMX_PLAYABLE_SEGS) {
                    qCol[qTail] = nc;
                    qRow[qTail] = nr;
                    qTail++;
                }
            }
        }
    }

    int reachCount = 0;
    for (int r = 0; r < WMX_SEG_ROWS; r++)
        for (int c = 0; c < WMX_SEG_COLS; c++)
            if (s_reachable[r][c]) reachCount++;

    Log::World("WorldMap: [BFS] From seg(%d,%d) veh=%d: %d/%d segments reachable",
               startCol, startRow, (int)veh, reachCount, WMX_PLAYABLE_SEGS);
}

// ============================================================================
// Catalog management
// ============================================================================
static void BuildDistanceCatalog()
{
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);

    // v0.14.85 (restored from v0.11.15): the engine reports player position as
    // (0,0) for several frames at world-map entry before populating the real
    // value. Building the catalog from (0,0) lands BFS in an arbitrary cell
    // (segment col 16, row 0) that is almost always wrong, so defer until we
    // see a non-zero position. s_catalogBuilt stays false, Poll() retries us
    // each frame until success.
    if (px == 0 && py == 0) {
        Log::World("WorldMap: [DEFER] Position is (0,0), retrying catalog build next poll");
        return;
    }

    // Copy all locations and compute distances
    for (int i = 0; i < LOCATION_COUNT; i++) {
        s_catalog[i] = s_locations[i];
        // Store distance in x field temporarily for sorting
        int32_t dist = (int32_t)CalculateWrappedDistance(px, py, s_locations[i].x, s_locations[i].y);
        s_catalog[i].x = dist;  // temporarily store distance here
    }

    // Sort by distance (ascending)
    std::sort(s_catalog, s_catalog + LOCATION_COUNT, [](const LocationEntry& a, const LocationEntry& b) {
        return a.x < b.x;  // x field contains distance
    });

    // Restore original coordinates
    for (int i = 0; i < LOCATION_COUNT; i++) {
        for (int j = 0; j < LOCATION_COUNT; j++) {
            if (strcmp(s_catalog[i].name, s_locations[j].name) == 0) {
                s_catalog[i].x = s_locations[j].x;
                s_catalog[i].y = s_locations[j].y;
                break;
            }
        }
    }

    // v0.14.85: apply reachability filter. If terrain failed to load (e.g.
    // missing world.fs at runtime), fall back to no-filter mode so cycling
    // still works — graceful degradation rather than empty catalog. If the
    // player is in Ragnarok, also skip the BFS — it can fly anywhere.
    if (!s_terrainLoaded) {
        s_catalogCount = LOCATION_COUNT;
        Log::World("WorldMap: [BFS] Terrain not loaded — catalog unfiltered (%d entries)",
                   s_catalogCount);
    } else {
        VehicleType veh   = GetVehicleType(GetLocomotionMode());
        int         pCol  = WorldXToSegCol(px);
        int         pRow  = WorldYToSegRow(py);
        Log::World("WorldMap: [BFS] Player at (%d,%d) -> seg(%d,%d), vehicle type %d",
                   px, py, pCol, pRow, (int)veh);

        if (veh == VEH_RAGNAROK) {
            // Ragnarok flies over everything — keep all entries, no BFS.
            s_catalogCount = LOCATION_COUNT;
            Log::World("WorldMap: [BFS] Ragnarok mode — catalog unfiltered (%d entries)",
                       s_catalogCount);
        } else {
            ComputeReachability(pCol, pRow, veh);

            // Compact in place: keep only entries on reachable segments.
            int kept = 0;
            for (int i = 0; i < LOCATION_COUNT; i++) {
                int locCol = WorldXToSegCol(s_catalog[i].x);
                int locRow = WorldYToSegRow(s_catalog[i].y);
                if (s_reachable[locRow][locCol]) {
                    if (kept != i) s_catalog[kept] = s_catalog[i];
                    kept++;
                }
            }
            s_catalogCount = kept;
            Log::World("WorldMap: [BFS] Filtered to %d reachable locations (vehicle type %d)",
                       s_catalogCount, (int)veh);
        }
    }

    // Pathological case: nothing reachable. Better than crashing on cycle
    // math — just leave catalog empty and surface in the log; Aaron will
    // notice immediately and we can investigate.
    if (s_catalogCount == 0) {
        Log::World("WorldMap: [BFS] WARNING — no reachable locations from current position");
    }

    s_catalogBuilt = true;
    s_catalogIndex = 0;
    if (s_catalogCount > 0) {
        Log::World("WorldMap: Catalog built (%d entries), nearest: %s",
                   s_catalogCount, s_catalog[0].name);
    }
}

// ============================================================================
// Navigation announcements
// ============================================================================
static void AnnounceLocation(int index)
{
    if (index < 0 || index >= s_catalogCount || !s_catalogBuilt) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    double distance = CalculateWrappedDistance(px, py, s_catalog[index].x, s_catalog[index].y);
    int distanceKm = (int)(distance / 1000.0);  // rough conversion to kilometers
    
    char buf[256];
    if (distanceKm < 1) {
        snprintf(buf, sizeof(buf), "%s. Very close.", s_catalog[index].name);
    } else {
        snprintf(buf, sizeof(buf), "%s. %d kilometers away.", s_catalog[index].name, distanceKm);
    }
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [LOCATION] %s", buf);
}

static void AnnounceBearing()
{
    if (!s_catalogBuilt || s_catalogIndex >= s_catalogCount) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    // Calculate bearing to selected location
    int32_t tx = s_catalog[s_catalogIndex].x;
    int32_t ty = s_catalog[s_catalogIndex].y;
    
    // Handle world wrapping for shortest path
    int32_t dx = tx - px;
    int32_t dy = ty - py;
    
    if (abs(dx) > WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else dy += (int32_t)WM_HEIGHT;
    }
    
    // Convert to bearing (0=North, clockwise)
    double radians = atan2(dx, -dy);  // -dy because FF8 Y increases downward
    double degrees = radians * 180.0 / 3.14159;
    if (degrees < 0) degrees += 360.0;
    
    const char* direction;
    if (degrees < 22.5 || degrees >= 337.5) direction = "North";
    else if (degrees < 67.5) direction = "Northeast";
    else if (degrees < 112.5) direction = "East";
    else if (degrees < 157.5) direction = "Southeast";
    else if (degrees < 202.5) direction = "South";
    else if (degrees < 247.5) direction = "Southwest";
    else if (degrees < 292.5) direction = "West";
    else direction = "Northwest";
    
    double distance = CalculateWrappedDistance(px, py, tx, ty);
    int distanceKm = (int)(distance / 1000.0);
    
    char buf[256];
    snprintf(buf, sizeof(buf), "%s. %s, %d kilometers.", 
             s_catalog[s_catalogIndex].name, direction, distanceKm);
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [BEARING] %s", buf);
}

// ============================================================================
// Vehicle state tracking (v0.14.85.3)
// ============================================================================
// Names match the canonical research-doc enum. Modes outside the canonical
// set never reach this function in normal operation — CheckVehicleChange's
// whitelist filters them — but the default returns 'Unknown vehicle' for
// safety. Cars are identified as a single 'Car' name regardless of the
// specific mode 32-40 value.
static const char* GetVehicleName(uint8_t mode)
{
    if (mode == 0 || mode == 6) return "On foot";        // Squall / Selphie
    if (mode == 3)               return "Ship";          // BAT-validated v0.14.83
    if (mode == 31)              return "Chocobo";
    if (mode >= 32 && mode <= 40) return "Car";
    if (mode == 48)              return "Garden";
    if (mode == 50)              return "Ragnarok";
    return "Unknown vehicle";
}

// v0.14.90: debounce window. The v0.14.89 BAT log demonstrated that even
// canonical locomotion values (3 = Ship, 32–40 = Car, 50 = Ragnarok) read
// TRANSIENTLY during normal on-foot travel, especially at coastlines and
// field-transition boundaries. The previous v0.14.85.3 IsCanonicalLocomotion
// whitelist passed these values straight through to s_lastVehicle, which
// then poisoned downstream arrival logic (the BAT log shows 'Arrived near
// Balamb Garden' firing at 11:24:28 because mode 50 = Ragnarok briefly read,
// vehicle proximity arrival path fired at <600 units before the player
// actually entered the Garden field). Real vehicle changes hold the canonical
// value steady for many frames; transients last 1–2 polls. Debounce: a new
// canonical value must read consistently across DEBOUNCE_POLLS consecutive
// polls before s_lastVehicle is updated and the announcement / catalog
// rebuild fires. Non-canonical values are still ignored at the IsCanonical
// gate; this debounce specifically targets transient CANONICAL values.
static const int DEBOUNCE_POLLS = 4;
static int     s_pendingVehicle      = -1;   // canonical value seen in the last N polls (or -1 if none pending)
static int     s_pendingVehicleCount = 0;    // consecutive polls reading s_pendingVehicle

// v0.14.90.3: world-map entry suppression window. The v0.14.90 debounce above
// handles 1–3-poll (~16–64 ms) transient byte excursions, but the v0.14.90.2
// BAT log exposed a more obstinate noise pattern: during the camera zoom-in
// animation that plays out for ~3 seconds after every world-map re-entry
// (post-battle return is the canonical case), the locomotion byte cycles
// through canonical values — e.g. mode 0 (Squall foot) → mode 3 (Ship) →
// mode 6 (Selphie foot) — each held for ~1 second. Every value sails through
// the 4-poll debounce as a 'real' transition and CheckVehicleChange announces
// each one + fires a BFS catalog rebuild. From the user's POV: three jarring
// vehicle announcements per battle exit and a catalog that briefly contains
// 38 ocean-allowed entries the player can't actually reach.
//
// The animation residue is indistinguishable from real vehicle changes purely
// on byte hold-time — they're all canonical values held for hundreds of ms.
// The only signal we have is 'we just entered the world map.' So time-gate
// CheckVehicleChange for the first WM_ENTRY_DEBOUNCE_MS after every entry.
// During the window every byte change is dropped silently. At expiry, snapshot
// whatever value the byte reads and commit it to s_lastVehicle as the new
// baseline — silently, no announce, no rebuild — because the player did NOT
// perform a vehicle action; the byte just settled to its steady state.
//
// 3000 ms covers the BAT's observed 3-cycle noise pattern with margin. The
// camera zoom-in is ~2 seconds and the player can't manually do anything
// during it, so we don't lose any genuine player-initiated vehicle change
// to the gate. Scripted story events that mount Garden / Ragnarok exactly
// at battle-victory return are the theoretical false-negative case; vanishingly
// rare in practice and the next legitimate transition restores the chain.
static const DWORD WM_ENTRY_DEBOUNCE_MS = 3000;
static DWORD s_wmEntryTick = 0;   // tick when world map was entered (0 = past the window or never)

static void CheckVehicleChange()
{
    uint8_t vehicle = GetLocomotionMode();

    // v0.14.90.3: suppress all vehicle-change processing during the world-map
    // re-entry animation window. See WM_ENTRY_DEBOUNCE_MS state declaration
    // above for full rationale.
    if (s_wmEntryTick != 0) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - s_wmEntryTick;
        if (elapsed < WM_ENTRY_DEBOUNCE_MS) {
            // Still inside the suppression window. Drop any pending state
            // and exit. The byte's animation cycling does not commit.
            s_pendingVehicle      = -1;
            s_pendingVehicleCount = 0;
            return;
        }
        // Window expired this tick. Snapshot the current byte as the new
        // baseline, silently. No announce (player didn't perform an action),
        // no catalog rebuild (the byte's settled value is what reachability
        // logic should have been using all along; if the rule class differs
        // from what's currently filtered we'd want a rebuild, but the v0.14.90
        // debounce on subsequent ticks will detect that on its own).
        if (IsCanonicalLocomotion(vehicle)) {
            int prev = s_lastVehicle;
            s_lastVehicle = vehicle;
            Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=%u (was %d, suppressed %lums of byte noise)",
                       vehicle, prev, (unsigned long)elapsed);
        } else {
            // Non-canonical at expiry — unusual but possible at story-script
            // boundaries. Leave s_lastVehicle untouched; the next tick's normal
            // CheckVehicleChange flow will start fresh once a canonical value
            // shows up. Log so we notice if this happens repeatedly.
            Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Window expired with non-canonical locomotion=%u; keeping s_lastVehicle=%d",
                       vehicle, s_lastVehicle);
        }
        s_wmEntryTick         = 0;
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;  // skip the rest of CheckVehicleChange this tick; normal flow resumes next tick
    }

    // v0.14.85.3 whitelist: only canonical locomotion values per the research
    // doc are eligible to trigger vehicle-change announcements or catalog
    // rebuilds. Transient bytes (animation phase counters, field-transition
    // state values like the mode 4 we observed at the Fire Cavern boundary in
    // the v0.14.85.2 BAT) are silently ignored — s_lastVehicle is not updated,
    // nothing is announced, no rebuild fires. Real vehicle changes still
    // register because the byte passes through a canonical value at the
    // mount/dismount moment, and that is the transition we capture.
    // v0.14.90 debounce: even canonical values can be transient (BAT showed
    // mode 50 / mode 3 reading for 1 poll during foot travel near coastlines).
    // Track a pending value across consecutive polls. Only commit to
    // s_lastVehicle (and fire the announcement / rebuild side effects) once
    // the same value has held for DEBOUNCE_POLLS in a row.
    if (!IsCanonicalLocomotion(vehicle)) {
        // Non-canonical → reset pending state. The byte's transient excursion
        // is over; whatever it tries next must build up its count from zero.
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    // Canonical value matches the already-committed s_lastVehicle — nothing
    // to do, ensure the pending state is also clear.
    if ((int)vehicle == s_lastVehicle) {
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    // Canonical value differs from current. Track / advance the pending
    // count. If it reaches DEBOUNCE_POLLS we commit. Otherwise we wait.
    if ((int)vehicle == s_pendingVehicle) {
        s_pendingVehicleCount++;
    } else {
        s_pendingVehicle      = vehicle;
        s_pendingVehicleCount = 1;
    }

    if (s_pendingVehicleCount < DEBOUNCE_POLLS) {
        // Not yet stable enough; don't commit, don't announce, don't rebuild.
        return;
    }

    // Stable for DEBOUNCE_POLLS polls in a row — treat as a real transition.
    s_pendingVehicle      = -1;
    s_pendingVehicleCount = 0;

    {
        if (s_lastVehicle != -1) {  // skip initial announcement
            const char* newVehicle = GetVehicleName(vehicle);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.", newVehicle);
            ScreenReader::Speak(buf, true);  // interrupt previous speech
            Log::World("WorldMap: Vehicle change: %s (mode %u)", newVehicle, vehicle);

            // v0.14.85.2 + .3: rebuild the catalog when the vehicle's BFS rule
            // CLASS changes (not just the VehicleType). Three rule classes:
            //   0 = land-only  (foot, chocobo, car)
            //   1 = ocean-allowed (Ship, Garden)
            //   2 = no-filter  (Ragnarok)
            // Foot ↔ car ↔ chocobo transitions stay within class 0, so they
            // don't trigger gratuitous rebuilds (the BFS result would be
            // identical). Class crossings (foot ↔ Ragnarok, car ↔ Ship)
            // do rebuild because the reachability set genuinely differs.
            VehicleType oldType = (s_lastVehicle >= 0)
                                  ? GetVehicleType((uint8_t)s_lastVehicle)
                                  : VEH_ON_FOOT;
            VehicleType newType = GetVehicleType(vehicle);
            int oldClass = GetBfsRuleClass(oldType);
            int newClass = GetBfsRuleClass(newType);
            if (oldClass != newClass && s_onWorldMap) {
                s_catalogBuilt = false;
                Log::World("WorldMap: [BFS] Vehicle rule class changed (%d -> %d), forcing catalog rebuild",
                           oldClass, newClass);
            }
        }
        s_lastVehicle = vehicle;
    }
}

// ============================================================================
// Auto-drive lifecycle (v0.14.86)
// ============================================================================
// StartAutoDrive captures the destination by VALUE (not by index) so the
// drive survives catalog rebuilds. UpdateAutoDrive does the per-tick work.
// StopAutoDrive is idempotent and centralizes key release. The drive
// pauses automatically on world-map exit (Poll() handles that path) and
// resumes on re-entry, allowing random encounters not to abort the route.
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    ReleaseAllDriveKeys();
    s_driveActive = false;
    // v0.14.87: clear sweep + final-approach state so a fresh drive starts clean.
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    if (reason && *reason) {
        ScreenReader::Speak(reason, true);
        Log::World("WorldMap: [DRIVE] Stopped: %s", reason);
    } else {
        Log::World("WorldMap: [DRIVE] Stopped (silent)");
    }
}

static void StartAutoDrive(int catIdx)
{
    if (s_driveActive) return;                       // toggle handler should have called Stop first
    if (!s_catalogBuilt || s_catalogCount == 0) {
        ScreenReader::Speak("No locations available.", true);
        return;
    }
    if (catIdx < 0 || catIdx >= s_catalogCount) {
        ScreenReader::Speak("Invalid destination.", true);
        return;
    }

    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    if (px == 0 && py == 0) {
        ScreenReader::Speak("Position unavailable. Try again.", true);
        return;
    }

    const LocationEntry& dest = s_catalog[catIdx];

    // v0.14.89: prefer refined entry coord when available. The s_catalog[]
    // working copy holds the catalog center (s_locations[].x/y); we look up
    // the matching s_locations[] index and check the parallel refined table
    // for an empirical entry coord captured on a prior successful drive.
    int locIdx = FindLocationIndexByTargetCoords(dest.x, dest.y);
    if (locIdx >= 0 && s_refinedHas[locIdx]) {
        s_driveTargetX = s_refinedX[locIdx];
        s_driveTargetY = s_refinedY[locIdx];
        Log::World("WorldMap: [DRIVE] Using refined entry for %s: (%d,%d) instead of catalog (%d,%d)",
                   dest.name, s_refinedX[locIdx], s_refinedY[locIdx], dest.x, dest.y);
    } else {
        s_driveTargetX = dest.x;
        s_driveTargetY = dest.y;
    }
    strncpy(s_driveTargetName, dest.name, sizeof(s_driveTargetName) - 1);
    s_driveTargetName[sizeof(s_driveTargetName) - 1] = '\0';

    double dist = CalculateWrappedDistance(px, py, dest.x, dest.y);
    DWORD now = GetTickCount();

    s_driveActive            = true;
    s_driveStartTime         = now;
    s_driveLastAnnounce      = now;
    s_driveLastDist          = dist;
    s_driveStuckX            = px;
    s_driveStuckY            = py;
    s_driveStuckCheckTime    = now;
    s_driveStuckCount        = 0;
    s_driveApproachAnnounced = (dist < DRIVE_APPROACH_DIST);  // suppress one-shot if already inside
    s_finalApproachEnterTick = 0;     // v0.14.87: not yet in final approach zone
    s_sweepActive            = false;
    s_sweepPhase             = 0;
    s_sweepTurning           = true;

    // v0.14.87: capture vehicle state at drive start. Arrival semantics differ:
    // on-foot waits for actual world-map exit (entering the location) to
    // announce arrival; vehicles announce arrival at proximity since they
    // can't enter most locations anyway. Re-checked dynamically each tick
    // via GetVehicleType(s_lastVehicle) so vehicle changes mid-drive are
    // honored (e.g. dismount car → walk into town).
    s_driveOnFootAtStart = (s_lastVehicle < 0) ||
                           (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    int distKm = (int)(dist / 1000.0);
    char buf[160];
    if (distKm < 1) {
        snprintf(buf, sizeof(buf), "Driving to %s. Very close.", s_driveTargetName);
    } else {
        snprintf(buf, sizeof(buf), "Driving to %s. %d kilometers.", s_driveTargetName, distKm);
    }
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [DRIVE] Start → %s at (%d,%d), dist=%.0f units (%d km)",
               s_driveTargetName, s_driveTargetX, s_driveTargetY, dist, distKm);
}

static void StartSweep(int32_t px, int32_t py, DWORD now)
{
    s_sweepActive   = true;
    s_sweepPhase    = 1;
    s_sweepTurning  = true;
    s_sweepStateEnd = now + SWEEP_TURN_BASE_MS;
    // Reset stuck tracking so the sweep itself can't be classified as stuck
    s_driveStuckX = px;
    s_driveStuckY = py;
    s_driveStuckCheckTime = now;
    s_driveStuckCount = 0;
    ScreenReader::Speak("Searching for entrance.", true);
    Log::World("WorldMap: [DRIVE-SWEEP] Started (target=%s, phase 1 turning right %dms)",
               s_driveTargetName, SWEEP_TURN_BASE_MS);
}

static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    uint16_t heading = GetWorldMapHeading();

    // Position can briefly read (0,0) at world-map re-entry. Skip the tick
    // rather than treating it as a real location far from target.
    if (px == 0 && py == 0) return;

    double dist = CalculateWrappedDistance(px, py, s_driveTargetX, s_driveTargetY);
    DWORD now = GetTickCount();

    // v0.14.87: dynamic on-foot check each tick. If the player dismounts a
    // car or otherwise switches to foot mid-drive, the arrival semantics
    // switch immediately. The locomotion byte's transient noise is filtered
    // by the v0.14.85.3 IsCanonicalLocomotion whitelist before s_lastVehicle
    // updates, so this is stable.
    bool isOnFoot = (s_lastVehicle < 0) ||
                    (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    // ---- v0.14.90: removed vehicle proximity arrival ('Arrived near X').
    // The original v0.11.06 design announced arrival ONLY when the world
    // map exits to a field — same code path for foot and vehicle. Adding
    // a vehicle-only proximity branch in v0.14.87 was wrong because it
    // relied on s_lastVehicle which gets poisoned by transient locomotion-
    // byte values. The v0.14.89 BAT log showed 'Arrived near Balamb Garden'
    // firing at distance ~600 because the locomotion byte transiently read
    // mode 50 (Ragnarok) for one poll; the player wasn't actually in a
    // vehicle. Field-entry detection in Poll()'s exit handler is the
    // single source of truth for arrival; if the engine triggered a field
    // transition, the player arrived. Vehicles that can enter fields
    // (Garden → FH dock, Ship → docks) work the same way.


    // ---- One-shot approach announcement when crossing DRIVE_APPROACH_DIST. ----
    // Suppressed during sweep — the user already heard "Searching for entrance."
    if (!s_driveApproachAnnounced && dist < DRIVE_APPROACH_DIST && !s_sweepActive) {
        s_driveApproachAnnounced = true;
        int distKm = (int)(dist / 1000.0);
        char buf[128];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Approaching %s.", s_driveTargetName);
        } else {
            snprintf(buf, sizeof(buf), "Approaching %s. %d kilometers.", s_driveTargetName, distKm);
        }
        ScreenReader::Speak(buf, true);
        s_driveLastAnnounce = now;
    }
    s_driveLastDist = dist;
    s_driveLastPosX = px;       // v0.14.89: stash for refined-entry capture
    s_driveLastPosY = py;

    // ---- Periodic distance announce (suppressed during sweep). ----
    if (!s_sweepActive && now - s_driveLastAnnounce >= DRIVE_ANNOUNCE_INTERVAL_MS) {
        s_driveLastAnnounce = now;
        int distKm = (int)(dist / 1000.0);
        char buf[64];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Less than 1 kilometer.");
        } else {
            snprintf(buf, sizeof(buf), "%d kilometers.", distKm);
        }
        ScreenReader::Speak(buf, true);
    }

    // ---- Sweep state machine (on-foot, narrow-entrance recovery). ----
    // When stuck or timed-out in final approach, sweep alternately turns
    // and walks to scan the local area for the entrance trigger. Field
    // exit during any sub-state is detected by Poll()'s exit handler and
    // counts as arrival. Sweep exhaustion (phase > SWEEP_MAX_PHASES) gives
    // up with "Could not find entrance."
    if (s_sweepActive) {
        if (now >= s_sweepStateEnd) {
            if (s_sweepTurning) {
                // Turn done → start walk sub-state.
                s_sweepTurning = false;
                s_sweepStateEnd = now + SWEEP_WALK_DURATION_MS;
                Log::World("WorldMap: [DRIVE-SWEEP] Phase %d walk start (%dms)",
                           s_sweepPhase, SWEEP_WALK_DURATION_MS);
            } else {
                // Walk done → advance to next phase or give up.
                s_sweepPhase++;
                if (s_sweepPhase > SWEEP_MAX_PHASES) {
                    StopAutoDrive("Could not find entrance.");
                    return;
                }
                s_sweepTurning = true;
                DWORD turnDur = SWEEP_TURN_BASE_MS + (DWORD)(s_sweepPhase - 1) * 200;
                s_sweepStateEnd = now + turnDur;
                const char* dir = (s_sweepPhase % 2 == 1) ? "right" : "left";
                Log::World("WorldMap: [DRIVE-SWEEP] Phase %d turn start (%s, %dms)",
                           s_sweepPhase, dir, turnDur);
            }
        }
        // Output keys based on current sweep sub-state.
        bool wantUp    = !s_sweepTurning;
        bool wantRight = s_sweepTurning && (s_sweepPhase % 2 == 1);
        bool wantLeft  = s_sweepTurning && (s_sweepPhase % 2 == 0);
        SetDriveKeys(wantUp, wantLeft, wantRight);
        return;
    }

    // ---- Final-approach timeout (on-foot only). ----
    // Track when the player crossed below FINAL_APPROACH_DIST. If we've been
    // in final approach for more than FINAL_APPROACH_TIMEOUT_MS without the
    // world-map exiting, we likely walked past a narrow entrance. Start the
    // sweep search to scan local area for the trigger.
    if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST) {
        if (s_finalApproachEnterTick == 0) {
            s_finalApproachEnterTick = now;
            Log::World("WorldMap: [DRIVE] Entered final approach zone (dist=%.0f)", dist);
        }
        if (now - s_finalApproachEnterTick > FINAL_APPROACH_TIMEOUT_MS) {
            Log::World("WorldMap: [DRIVE] Final-approach timeout (%dms in zone, no entry)",
                       (int)(now - s_finalApproachEnterTick));
            StartSweep(px, py, now);
            return;
        }
    } else {
        // Out of final approach — reset so we re-arm next time we cross in.
        s_finalApproachEnterTick = 0;
    }

    // ---- Stuck detection. ----
    if (now - s_driveStuckCheckTime >= DRIVE_STUCK_CHECK_INTERVAL_MS) {
        double moved = CalculateWrappedDistance(s_driveStuckX, s_driveStuckY, px, py);
        if (moved < DRIVE_STUCK_THRESHOLD) {
            s_driveStuckCount++;
            Log::World("WorldMap: [DRIVE] Stuck check %d/%d (moved %.0f units in %dms window)",
                       s_driveStuckCount, DRIVE_STUCK_MAX, moved, DRIVE_STUCK_CHECK_INTERVAL_MS);
            // v0.14.87: on-foot stuck inside final approach → sweep instead
            // of giving up. Catches blocked entrances (e.g. trying to walk
            // into Balamb at the wrong angle and bumping a wall).
            if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount >= 2) {
                Log::World("WorldMap: [DRIVE] Stuck in final approach → sweep");
                StartSweep(px, py, now);
                return;
            }
            if (s_driveStuckCount >= DRIVE_STUCK_MAX) {
                StopAutoDrive("Stuck. Cannot reach destination.");
                return;
            }
        } else {
            s_driveStuckCount = 0;  // any meaningful movement resets the counter
        }
        s_driveStuckX         = px;
        s_driveStuckY         = py;
        s_driveStuckCheckTime = now;
    }

    // ---- Steering decision. ----
    int targetBearing = TorusBearing(px, py, s_driveTargetX, s_driveTargetY);
    int relBearing    = (targetBearing - (int)heading + 4096) & 0xFFF;
    // relBearing: 0=ahead, 1024=right 90°, 2048=behind, 3072=left 90°.

    bool wantUp = false, wantLeft = false, wantRight = false;

    if (dist < DRIVE_FINAL_APPROACH_DIST) {
        // Final approach: catalog coordinates aren't always exactly on the
        // entrance trigger zone. Walking forward through the area sweeps
        // through the trigger reliably without micro-corrections that can
        // overshoot the trigger band entirely. The final-approach timeout
        // (above) covers the case where this fails for narrow entrances.
        wantUp = true;
    } else if (relBearing < 200 || relBearing > 3896) {
        // Within ~17.6° of dead ahead — just go.
        wantUp = true;
    } else if (relBearing < 1800) {
        // Target is to the right (up to ~158°).
        wantRight = true;
        if (relBearing < 512) wantUp = true;  // within ~45°: turn AND walk
    } else {
        // Target is to the left (the remaining ~158-360° arc).
        wantLeft = true;
        if (relBearing > 3584) wantUp = true;  // within ~45° of ahead-left: turn AND walk
    }

    SetDriveKeys(wantUp, wantLeft, wantRight);
}

// ============================================================================
// Keyboard input polling (v0.14.83)
// ============================================================================
// Replaces the orphaned v0.11.x HandleKeyPress dispatch path. HandleKeyPress
// was defined in this file but never declared in world_map.h and never
// invoked from anywhere — leftover collateral from the v0.14.24 build damage
// / v0.14.31 partial recovery (Update() and Shutdown() were restored, the
// keyboard dispatch was not). Result: world map nav keys had been silently
// dead since the recovery.
//
// New design mirrors FieldNavigation::HandleKeys: PollKeys() is called from
// inside Poll() and uses GetAsyncKeyState with edge-detected statics.
// Implicitly gated on s_onWorldMap because Poll() early-returns when off
// world map. No collision with FieldNavigation: it gates its identical key
// set on FF8Addresses::IsOnField() which is mutually exclusive with the
// world-map scene flag (0x0203ED2C == 0).
static void PollKeys()
{
    static bool s_minusWas = false;
    static bool s_plusWas  = false;
    static bool s_bkspWas  = false;
    static bool s_bslashWas = false;
    // v0.14.90.1: input-injection diagnostic. F12 fires a 200ms VK_UP pulse via
    // keybd_event (the current AD mechanism). F2 fires the same pulse via
    // SendInput. Aaron presses each while standing still on the world map and
    // listens for a footstep. If F12 moves the character, keybd_event works
    // and AD has a different bug (timing, state-machine race, etc.). If F12
    // does nothing but F2 works, switch AD to SendInput. If neither moves
    // the character, the world map reads input through a pipeline that
    // bypasses both — DirectInput direct read, raw input, or memory-mapped
    // game-pad state. v0.14.89 BAT showed AD said it was "driving" while the
    // distance-to-target never decreased — character was not moving — which
    // exposed that the entire keybd_event assumption (carried over from past
    // chats v0.11.05-v0.11.08 that allegedly BAT-validated it) had not been
    // re-verified on the current system. v0.14.90.1 verifies it directly.
    static bool s_diagF12Was = false;
    static bool s_diagF2Was  = false;

    bool minus  = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus   = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    bool bksp   = (GetAsyncKeyState(VK_BACK)      & 0x8000) != 0;
    bool bslash = (GetAsyncKeyState(VK_OEM_5)     & 0x8000) != 0;  // '\' key

    if (minus && !s_minusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex - 1 + s_catalogCount) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] minus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (plus && !s_plusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex + 1) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] plus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (bksp && !s_bkspWas) {
        AnnounceBearing();
        Log::World("WorldMap: [KEY] backspace bearing");
    }
    if (bslash && !s_bslashWas) {
        // v0.14.86: toggle auto-drive. If a drive is already running, cancel it;
        // otherwise start a drive toward the currently-selected catalog entry.
        // This replaces the v0.14.84 placeholder. The auto-drive system is
        // restored from past chats v0.11.05-v0.11.10.
        if (s_driveActive) {
            StopAutoDrive("Cancelled.");
            Log::World("WorldMap: [KEY] backslash → cancel");
        } else if (s_catalogBuilt && s_catalogCount > 0) {
            Log::World("WorldMap: [KEY] backslash → start drive to idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
            StartAutoDrive(s_catalogIndex);
        } else {
            ScreenReader::Speak("No locations available.", true);
            Log::World("WorldMap: [KEY] backslash → no catalog");
        }
    }

    s_minusWas  = minus;
    s_plusWas   = plus;
    s_bkspWas   = bksp;
    s_bslashWas = bslash;
}

// ============================================================================
// Main polling loop
// ============================================================================
void Poll()
{
    bool nowOnWorldMap = IsOnWorldMap();
    
    // Detect world map entry
    if (nowOnWorldMap && !s_onWorldMap) {
        s_onWorldMap = true;
        s_wmEntryTick = GetTickCount();   // v0.14.90.3: arm the locomotion-byte suppression window
        s_catalogBuilt = false;  // force rebuild on next poll
        Log::World("WorldMap: Entered world map");

        if (s_driveActive) {
            // v0.14.88: drive paused during a random encounter / battle, now
            // resuming. Re-arm timers so the stuck-detection window doesn't
            // trigger on the field/battle gap, and announce so the user
            // knows the drive is still going. Stable s_driveTargetX/Y/name
            // survive arbitrary catalog rebuilds. Note: drives that exited
            // world map FOR a field (location entry) were already terminated
            // by the MODE_FIELD detection path below, so reaching this branch
            // means the off-map state was a battle.
            DWORD now = GetTickCount();
            s_driveLastAnnounce      = now;
            s_driveStuckCheckTime    = now;
            s_driveStuckCount        = 0;
            s_finalApproachEnterTick = 0;  // re-arm final-approach timer
            int32_t rx, ry, rz;
            GetWorldMapPosition(&rx, &ry, &rz);
            if (rx != 0 || ry != 0) {
                s_driveStuckX = rx;
                s_driveStuckY = ry;
            }
            char buf[160];
            snprintf(buf, sizeof(buf), "Resuming drive to %s.", s_driveTargetName);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: [DRIVE] Resumed after world-map re-entry → %s",
                       s_driveTargetName);
        } else {
            ScreenReader::Speak("World map.", true);
        }

        // Check if we're in the right game mode for world map functionality
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

        // v0.14.90.2: distinguish arrival from battle/encounter by DISTANCE
        // at exit time, not by post-exit pGameMode. The v0.14.90 design read
        // pGameMode at exit and branched on MODE_FIELD vs anything-else, but
        // BAT logs show pGameMode is still MODE_WORLDMAP (2) at the moment
        // we detect IsOnWorldMap flipped false — the mode register hasn't
        // transitioned yet, so we always read 2 and never trip the arrival
        // branch. The post-exit MODE_FIELD read works in a deferred block
        // (v0.14.88 design) but is fragile because Poll's polling cadence
        // may miss the brief MODE_FIELD window before MODE_FIELD's own
        // game loop takes over.
        //
        // Simpler signal that works without timing assumptions: distance
        // to target. Battles and random encounters can fire ANYWHERE,
        // including far from any catalog destination. Field entries only
        // happen at a location's trigger zone, which by definition is
        // close to the target coord. So:
        //   dist < DRIVE_ARRIVED_ON_EXIT_DIST (1500) at exit → arrival
        //   dist >= 1500                                  → battle/encounter, pause
        //
        // Edge case: random encounter at the doorstep of a target. Would
        // mis-announce as arrival. In practice rare — random encounter
        // zones don't typically overlap location entrance triggers — and
        // the actual battle entry (mode swirl, battle music) is sensorily
        // unambiguous to the user. After the battle ends and they return
        // to world map, AD would normally have already terminated, but
        // since they're standing on the entrance, walking forward one
        // step enters the location naturally.
        if (s_driveActive) {
            char buf[160];
            if (s_driveLastDist > 0 && s_driveLastDist < DRIVE_ARRIVED_ON_EXIT_DIST) {
                snprintf(buf, sizeof(buf), "Arrived at %s.", s_driveTargetName);
                Log::World("WorldMap: [DRIVE] Arrival via exit-distance (target=%s, dist=%.0f)",
                           s_driveTargetName, s_driveLastDist);

                // v0.14.89 refined-coord capture, retained: the player's
                // last-known world-map (X, Y) before exit IS the actual
                // entry trigger position. Subsequent drives to this same
                // location will steer there directly, skipping sweep.
                int locIdx = FindLocationIndexByTargetCoords(s_driveTargetX, s_driveTargetY);
                if (locIdx >= 0 && (s_driveLastPosX != 0 || s_driveLastPosY != 0)) {
                    bool wasRefined = s_refinedHas[locIdx];
                    s_refinedX[locIdx]   = s_driveLastPosX;
                    s_refinedY[locIdx]   = s_driveLastPosY;
                    s_refinedHas[locIdx] = true;
                    Log::World("WorldMap: [DRIVE] %s refined entry for %s at (%d,%d) (was target=(%d,%d))",
                               wasRefined ? "Updated" : "Captured",
                               s_locations[locIdx].name,
                               s_driveLastPosX, s_driveLastPosY,
                               s_driveTargetX, s_driveTargetY);
                }

                StopAutoDrive(buf);
            } else {
                // Far from target → battle / encounter / vehicle dismount
                // / something else. Pause; drive resumes on world-map
                // re-entry. AD's resume logic (in the entry handler above)
                // re-arms the stuck-detection timer so the field/battle
                // gap doesn't immediately trigger 'Stuck'.
                ReleaseAllDriveKeys();
                Log::World("WorldMap: [DRIVE] Paused (target=%s, dist=%.0f) — will resume on re-entry",
                           s_driveTargetName, s_driveLastDist);
            }
        }
        Log::World("WorldMap: Exited world map");
    }

    
    if (!s_onWorldMap) return;
    
    // Build catalog on first poll while on world map
    if (!s_catalogBuilt) {
        BuildDistanceCatalog();
    }
    
    // Check for vehicle changes
    CheckVehicleChange();

    // v0.14.86: per-frame auto-drive update. Internally gated on s_driveActive
    // so this is a near-no-op when no drive is in progress. Placed AFTER
    // CheckVehicleChange so that any rule-class rebuild fires first — the
    // drive's stable target X/Y/name don't depend on the catalog state, but
    // the order keeps logs in a sensible sequence (vehicle change → catalog
    // rebuild → drive tick).
    UpdateAutoDrive();
    
    // Track significant position changes for future auto-drive features
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    static int32_t lastX = 0, lastY = 0;
    
    double movement = CalculateWrappedDistance(px, py, lastX, lastY);
    if (movement > 1000) {  // moved more than 1km
        s_lastMovementTick = GetTickCount();
        lastX = px;
        lastY = py;
    }

    // v0.14.83: Poll nav keys (-, =, Backspace) at the end of the world-map
    // poll cycle. Catalog is guaranteed built by this point; s_onWorldMap is
    // true (we early-returned above otherwise). See PollKeys comment for
    // ownership notes.
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

    // v0.14.90.3: reset entry-debounce state. s_wmEntryTick = 0 means 'not
    // currently in a suppression window'; the first world-map entry will set
    // it. s_pendingVehicle/Count are also reset for cleanliness, though the
    // existing v0.14.90 debounce logic re-initializes them on demand.
    s_wmEntryTick = 0;
    s_pendingVehicle = -1;
    s_pendingVehicleCount = 0;

    // v0.14.86: auto-drive state. Initialized to inactive; no keys held.
    // ReleaseAllDriveKeys() is a no-op given the bool flags are false, but
    // calling it makes the intent explicit if module re-init ever happens
    // mid-session.
    s_driveActive = false;
    s_driveTargetX = 0;
    s_driveTargetY = 0;
    s_driveTargetName[0] = '\0';
    s_driveStuckCount = 0;
    s_driveApproachAnnounced = false;
    // v0.14.87: sweep + final-approach state
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    s_driveOnFootAtStart = true;
    s_driveLastPosX = 0;
    s_driveLastPosY = 0;

    // v0.14.89: refined entry-coord table. Cleared on Initialize because
    // persistence is not yet implemented — a fresh game session starts
    // with the canonical catalog and accumulates refined entries through
    // gameplay. Persistence is queued for v0.14.90.
    memset(s_refinedX, 0, sizeof(s_refinedX));
    memset(s_refinedY, 0, sizeof(s_refinedY));
    memset(s_refinedHas, 0, sizeof(s_refinedHas));

    ReleaseAllDriveKeys();

    // v0.14.85: load terrain grid once at module init. Idempotent
    // (s_terrainLoaded short-circuits subsequent calls). If the load fails
    // — e.g. world.fs missing or corrupt — we log and continue; catalog
    // building will fall back to no-filter mode rather than blocking.
    if (LoadTerrainGrid()) {
        Log::World("WorldMap: [INIT] Terrain grid loaded successfully");
    } else {
        Log::World("WorldMap: [INIT] Terrain grid load failed — catalog will be unfiltered");
    }

    // v0.14.92: hex-dump wmsetus.obj Sections 7 and 8 (the field-entry
    // bytecode + its small adjacent section) to ff8_world.log. Diagnostic-
    // only (no game-side state captured); failure is non-fatal because
    // AD's existing catalog-center + sweep-search path continues to work
    // without the decoded trigger data. v0.14.93 will hardcode the decoded
    // s_triggerData[] from this dump; v0.14.94 wires it into AD targeting.
    if (LoadTriggerZones()) {
        Log::World("WorldMap: [INIT] Trigger-zone hex dump complete (see [TRIGGER-DUMP] entries above)");
    } else {
        Log::World("WorldMap: [INIT] Trigger-zone load failed — see preceding [TRIGGER-DUMP] entries");
    }

    Log::World("WorldMap: Module initialized (v%s)", FF8OPC_VERSION);
}

// ============================================================================
// Public Update entry — called every ~16ms from the accessibility thread.
// Restored v0.14.31 (was deleted from world_map.cpp during build damage).
// Thin wrapper: defers all polling/announcement work to internal Poll().
// ============================================================================
void Update()
{
    Poll();
}

// ============================================================================
// Public Shutdown entry — restored v0.14.31.
// Resets module state. No hooks installed by this module so nothing to unhook.
// ============================================================================
void Shutdown()
{
    // v0.14.86: cancel any in-flight drive and release injected keys before
    // module teardown. StopAutoDrive is idempotent and silent when reason
    // is null, which is correct here — shutdown shouldn't speak.
    if (s_driveActive) {
        StopAutoDrive(nullptr);
    } else {
        ReleaseAllDriveKeys();  // defensive; flags should already be false
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