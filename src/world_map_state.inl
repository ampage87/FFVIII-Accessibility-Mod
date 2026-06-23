// world_map_state.inl - Module-level statics, type definitions, constants
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// This file is included FIRST inside the WorldMap namespace by the slim
// world_map.cpp. Holds:
//   - Foundational type definitions (enums + structs) used by every other
//     .inl file: VehicleType, SegTerrainClass, LocationEntry.
//   - Address / dimension / sizing constants: WM_POS_*, WM_WIDTH/HEIGHT,
//     WMX_*, WMSETUS_*, MAX_LOCATIONS.
//   - All mutable module statics: refined-coord arrays, navigation state,
//     auto-drive state, terrain grid, segment-region map, drive path state,
//     s_destPlannerEligible[] (v0.16.0 planner-eligibility flag).
//
// Catalog data (s_locations[] + LOCATION_COUNT) lives in
// world_map_catalog.inl. To decouple state-array sizing from the catalog
// table size, this file defines MAX_LOCATIONS (a generous upper bound);
// catalog.inl asserts LOCATION_COUNT <= MAX_LOCATIONS at compile time.

// ============================================================================
// Sizing upper bound for state arrays (v0.16.0)
// ============================================================================
// State arrays indexed by catalog slot are sized to MAX_LOCATIONS, NOT to
// LOCATION_COUNT directly. This decouples them from the s_locations[] table
// definition (in catalog.inl, which is included later) so that state.inl
// can be processed first. catalog.inl static_asserts that the actual catalog
// count fits. Current catalog has 37 entries; 64 leaves comfortable headroom
// for adding new destinations without re-sizing state.
static const int MAX_LOCATIONS = 64;

// ============================================================================
// Confirmed static addresses (from deep research + ff8-speedruns + v0.11.02 diag)
// ============================================================================
static const uint32_t WM_POS_X      = 0x0203EE80;  // DWORD - player X
static const uint32_t WM_POS_Y      = 0x0203EE84;  // DWORD - player Y
static const uint32_t WM_POS_Z      = 0x0203EE88;  // DWORD - player Z
static const uint32_t WM_HEADING    = 0x0203ED02;   // WORD  - 0=North, 0-4095 CW
static const uint32_t WM_LOCOMOTION = 0x02040A5E;   // BYTE  - locomotion / vehicle mode. v0.14.83 whitelisted to canonical {0..4}; per `Plan & Research Documents/World Map Terrain and Locomotion Reference.md` legitimate values include 0=Squall foot, 6=Selphie foot, 10=train, 31=Chocobo, 32=invisible-car — our GetVehicleName uses 0/1/2/3/4 for foot/Car/Chocobo/Ship/Ragnarok which DISAGREES with the research-doc enum (research says Chocobo=31, we say 2). Empirical reconciliation needed; see v0.14.84 changelog.
static const uint32_t WM_SCENE_FLAG = 0x0203ED2C;   // WORD  - 0=worldmap, 1=field

// ============================================================================
// v0.14.103: Savemap WORLDMAP struct addresses (deep research validated)
// ============================================================================
// Per `Plan & Research Documents/Vehicle state and car position deep research
// results.md`, the savemap WORLDMAP struct IS maintained live in RAM during
// driving (refuting the prior "PC version may omit positions" caveat from
// the older deep-research doc). Hyne's WORLDMAP_PC 26-byte struct describes
// only what gets serialised to disk; the runtime engine keeps the full
// PSX-format struct in memory because the world-map driving code was ported
// expecting the position arrays to be there.
//
// SAVEMAP base = 0x01CFDC5C (76-byte header, NOT 96-byte — see SAVEMAP
// OFFSET CORRECTION in userMemories). The deep research recommended +0x125C
// for the WORLDMAP struct (= prior research's +0x1270 minus 0x14 for the
// header correction).
//
// Layout within the WORLDMAP struct (each *_pos[6] is uint16: X, Z, Y, unk,
// unk, rotation — 12 bytes total):
//   +0x00  char_pos[6]      — foot character mirror
//   +0x18  ragnarok_pos[6]
//   +0x24  bgu_pos[6]       — mobile Balamb Garden
//   +0x30  car_pos[6]       — rental car
//   +0x62  car_rent (1 byte) — boolean: rental car possessed
//
// VERIFICATION: addresses below are arithmetic derivations and must be
// verified by the v0.14.103 [VEH-VERIFY] diagnostic block at module init.
// Decision tree: char_pos X coord ~ foot DWORD X / 4096 at game start
// confirms +0x125C; car drive showing car_pos updating confirms address;
// values looking like ASCII or pointer-shaped (0x004xxxxx) indicate
// arithmetic is wrong and v0.14.103.1 patches with empirical offsets.
static const uintptr_t WM_SAVEMAP_BASE     = 0x01CFDC5C;
static const uint32_t  WM_WORLDMAP_OFFSET  = 0x125C;
static const uint32_t  WMS_CHAR_POS_OFFSET     = 0x00;   // foot mirror (uint16 x6)
static const uint32_t  WMS_RAGNAROK_POS_OFFSET = 0x18;
static const uint32_t  WMS_BGU_POS_OFFSET      = 0x24;
static const uint32_t  WMS_CAR_POS_OFFSET      = 0x30;   // rental car (uint16 x6)
static const uint32_t  WMS_CAR_RENT_OFFSET     = 0x62;

// Convenience: absolute runtime addresses derived from the above.
static const uintptr_t WM_CAR_POS_ADDR      = WM_SAVEMAP_BASE + WM_WORLDMAP_OFFSET + WMS_CAR_POS_OFFSET;       // 0x01CFFEE8
static const uintptr_t WM_RAGNAROK_POS_ADDR = WM_SAVEMAP_BASE + WM_WORLDMAP_OFFSET + WMS_RAGNAROK_POS_OFFSET;  // 0x01CFFED0
static const uintptr_t WM_BGU_POS_ADDR      = WM_SAVEMAP_BASE + WM_WORLDMAP_OFFSET + WMS_BGU_POS_OFFSET;       // 0x01CFFEDC
static const uintptr_t WM_CHAR_POS_ADDR     = WM_SAVEMAP_BASE + WM_WORLDMAP_OFFSET + WMS_CHAR_POS_OFFSET;      // 0x01CFFEB8
static const uintptr_t WM_CAR_RENT_ADDR     = WM_SAVEMAP_BASE + WM_WORLDMAP_OFFSET + WMS_CAR_RENT_OFFSET;      // 0x01CFFF1A

// Foot DWORDs are 20.12 fixed-point (multiply savemap uint16 X by 4096 to
// align coordinate spaces). Savemap uint16 coords are signed when interpreted
// as int16 (the world map coordinate system has a non-zero origin offset
// per the wmx.obj documentation; the foot DWORDs at WM_POS_X/Y/Z carry that
// offset already, but the savemap uint16 coords store raw block-relative
// values that need to be sign-extended and scaled).
// v0.14.103.3: BAT empirically proved this should be 1, not 4096. The
// [VEH-VERIFY] dump at 22:11:38 showed: foot DWORDs X=16031, Y=-26948 versus
// car_pos[0]=16031, car_pos[2]=-26948 (exact 1:1 match). The savemap WORLDMAP
// struct stores positions at the SAME scale as foot DWORDs, NOT in 20.12
// fixed-point. The original assumption was wrong; correcting now.
static const int32_t WM_SAVEMAP_TO_DWORD_SCALE = 1;

static const uint32_t WM_STORY_FLAG = 0x02036BDE;   // WORD  - savemap story-flag word read by sub_545F10's 0xFF02 (GTE) / 0xFF03 (LT) opcodes; v0.14.94 reads this for s_triggerPrograms[] gate evaluation. Range observed in artifact: 0..5000 across the 38 programs.

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
// v0.14.93 added Section 2 (the 32x24 segment-region byte map): each byte
// is the region ID for one segment, addressed at file offset row * 32 + col.
// (A 4-byte trailer '00 00 00 00' lives at file offset 768 — not a header.)
// The 0xFF08 region operands in Section 8's bytecode reference these bytes.
// AD steering (v0.14.94+) uses Section 2 to translate catalog (X, Y) into
// a region byte and then matches that against s_triggerPrograms[]'s clauses.
static const int      WMSETUS_DUMP_SECTIONS_1IDX[] = { 2, 7, 8, 9, 19 };  // v0.14.95: +9,19 (also 772b each — hypothesis: per-area region maps; Section 2 alone has only 19 of the 45 region IDs s_triggerPrograms[] references, so the world is split across multiple 32x24 maps)
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
// LocationEntry — catalog row type (v0.14.85.1)
// ============================================================================
// One row per world-map entry destination. Holds display name + canonical
// (X, Y) icon-center coordinate. The full table (s_locations[]) and its
// derived LOCATION_COUNT live in world_map_catalog.inl. State that depends
// on a catalog slot (s_catalog, s_refined*, s_destPlannerEligible) is sized
// to MAX_LOCATIONS here so we don't need to see s_locations[] first.
struct LocationEntry {
    const char* name;
    int32_t x;
    int32_t y;
};

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
static int32_t s_refinedX[MAX_LOCATIONS];
static int32_t s_refinedY[MAX_LOCATIONS];
static bool    s_refinedHas[MAX_LOCATIONS];

// ============================================================================
// Planner-eligibility flag per catalog entry (v0.16.0 — Part C)
// ============================================================================
// Computed at Initialize() time by ComputePlannerEligibility() in
// world_map_planner.inl, walking s_triggerPrograms[] and checking whether
// each catalog's region byte appears in any clause with a foot vehicle code
// (0x80 = Squall foot, 0x84 = alt-leader foot). Destinations that fail this
// test are geometric-trigger destinations -- entry via terrain-29 polygon
// trigger, NOT wmsetus script event. They must use v0.11.11-era simple-coord
// steering toward catalog coordinates, NOT the A* planner. Without this
// distinction the v0.14.95 closest-active-region fallback misroutes drives
// toward unrelated destinations (e.g. Fire Cavern -> bggate_1 in v0.15.13.2).
//
// Catalog index aligned with s_locations[]. Sized to MAX_LOCATIONS to keep
// the array sizing decoupled from catalog.inl's data table.
static bool s_destPlannerEligible[MAX_LOCATIONS];

// ============================================================================
// Navigation state
// ============================================================================
static struct LocationEntry s_catalog[MAX_LOCATIONS];  // distance-sorted working copy
static int  s_catalogCount = 0;        // v0.14.85: post-filter count (≤ LOCATION_COUNT, MAX_LOCATIONS)
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
static const double FINAL_APPROACH_FORWARD_DIST = 200.0;   // v0.14.100: below this, walk forward; 200..1000 uses bearing
static const double DRIVE_ARRIVED_ON_EXIT_DIST  = 1500.0;  // v0.14.87: world-map exit while closer than this counts as arrival
static const double DRIVE_NEAR_LOCATION_DIST    = 1500.0;  // v0.14.103: bounce-detection radius for non-car-friendly arrivals
static const DWORD  DRIVE_ANNOUNCE_INTERVAL_MS  = 5000;    // periodic distance announce
static const DWORD  DRIVE_STUCK_CHECK_INTERVAL_MS = 3000;  // stuck-detection sample window
static const double DRIVE_STUCK_THRESHOLD       = 100.0;   // movement floor in one window
static const int    DRIVE_STUCK_MAX             = 6;       // 6 windows × 3s = 18s no movement → give up

// #67 BAT 2 stage 2 (v0.18.3.59): planner-path follow + recovery tuning.
// DRIVE_PLAN_LOOKAHEAD_DIST: the drive steers at the first path cell at least
// this many world units ahead (straight-line) of the player. The v0.18.3.58
// BAT crawled because a one-cell (1024u) target sits inside the steering law's
// turn-in-place cone -- the bearing swings as the player inches, so it spins
// instead of driving. A target ~2400u out keeps the bearing stable (forward
// motion) while staying close enough to the clearance-centred route that the
// straight-line cut stays inside the corridor (offline follow sim: deviation
// ~720u at this range vs >1600u at a 4+ cell lookahead).
static const double DRIVE_PLAN_LOOKAHEAD_DIST   = 2400.0;
// Mid-route stuck recovery: after this many stuck windows on a planned drive,
// re-plan from the player's CURRENT position (a fresh clearance route from
// where he actually is) rather than giving up. Bounded per drive so a true
// hard-jam still terminates instead of looping.
static const int    DRIVE_REPLAN_TRIGGER        = 2;       // stuck windows before a recovery re-plan
static const int    DRIVE_MAX_REPLANS           = 8;       // recovery re-plans per drive before give-up

// #67 v0.18.3.60: mid-route arc-steering bands (relBearing units; 4096 = 360deg,
// so 1deg ~= 11.4 units). The old law walked forward only within ~17.5deg of the
// target and turned in place otherwise, with no damping -- so the heading
// overshot the target and rocked across it (v0.18.3.59 oscillated in place).
// New mid-route law: within STEER_DEADZONE of dead-ahead, drive STRAIGHT with no
// turn (this damps the overshoot -- the player stops correcting once roughly
// aimed); between deadzone and STEER_FWD_CONE, drive forward AND turn (arc);
// beyond the cone, turn in place toward the target (handles sharp path bends,
// like the old law). Tunable: widen the deadzone if it still oscillates, narrow
// it if it drifts into walls. Final-approach steering is unchanged.
static const int    STEER_DEADZONE              = 320;     // ~28deg: drive straight, no turning
static const int    STEER_FWD_CONE              = 576;     // ~50deg: forward while turning up to here

// #67 v0.18.3.62: motion-derived heading + self-calibrating turn sign. The
// v0.18.3.61 diagnostic proved GetWorldMapHeading() returns a frozen value (540)
// during a drive, so every relBearing was computed against a fake facing and the
// steering turned the wrong way into terrain. Instead we derive the player's
// facing from his ACTUAL motion (bearing of his recent position delta) and learn
// whether RIGHT raises or lowers that bearing by watching how it rotates when we
// turn -- so a wrong initial guess self-corrects within a sample. Heading -1 =
// not yet known (bootstrap: drive straight to establish it).
static int      s_driveMoveHeading    = -1;   // 0..4095, or -1 = unknown
static int32_t  s_driveHeadRefX       = 0;    // ref point for the next heading sample
static int32_t  s_driveHeadRefY       = 0;
static int      s_driveTurnSign       = 1;    // +1: RIGHT raises bearing; -1: RIGHT lowers it
static bool     s_driveTurnedSinceRef = false;
static bool     s_driveLastTurnRight  = false;
static DWORD    s_driveLastMoveTime   = 0;    // wall-slide: last time he actually moved
static int32_t  s_driveLastMovePosX   = 0;
static int32_t  s_driveLastMovePosY   = 0;
static DWORD    s_driveWedgeReverseUntil = 0;   // #67 v0.18.3.68: reverse-burst end tick (0 = not reversing)
static int      s_driveWedgeReverseCount = 0;   // reverse bursts used since the last genuine progress
static double   s_driveWedgeProgressDist = 0.0; // watermark: closest dist-to-target seen; refreshes the reverse budget
static int32_t  s_driveWedgeAnchorX      = 0;   // #67 v0.18.3.69: net-displacement wedge anchor (defeats wall-vibration)
static int32_t  s_driveWedgeAnchorY      = 0;
static DWORD    s_driveWedgeAnchorTime   = 0;
static const int   MOVE_HEADING_MIN_DELTA = 50;   // motion (units) needed to trust a heading sample
static const int   TURN_CAL_MIN_DELTA     = 80;   // heading change needed to trust a sign calibration
static const DWORD WALL_SLIDE_MS          = 600;  // stationary this long -> force a turn to slide off
static const int   WALL_SLIDE_EPS         = 40;   // movement under this (units) counts as "not moving"
// #67 v0.18.3.68: reverse un-wedge tuning.
static const DWORD  HARD_WEDGE_MS         = 1200;  // no real movement this long -> reverse off the wall
static const DWORD  REVERSE_BURST_MS      = 600;   // duration of one reverse burst
static const int    MAX_WEDGE_REVERSE     = 4;     // reverse bursts before falling through to stuck-detection
static const double WEDGE_PROGRESS_EPS    = 500.0; // got this much closer to target -> refresh the reverse budget
static const double WEDGE_NET_EPS         = 250.0; // #67 v0.18.3.69: net travel under this over HARD_WEDGE_MS = wedged (vibration-proof)
static const int   DRIVE_LOOKAHEAD_CELLS   = 1;    // #67 v0.18.3.64: aim at the NEXT route cell only (always walkable-adjacent, never across a wall)
static const DWORD TURN_DUTY_ON_MS         = 60;   // #67 v0.18.3.64: duty-cycle the turn to damp overshoot -- turn this long...
static const DWORD TURN_DUTY_OFF_MS        = 120;  // ...then go straight this long (forward frames let the heading catch up)

// ============================================================================
// #67 v0.18.3.74: SCREEN-RELATIVE self-calibrating on-foot steering
// ============================================================================
// The v0.18.3.73 camera diagnostic was decisive: WM_HEADING (0x0203ED02) is
// FROZEN on foot, G/H do NOT rotate the camera (nothing in memory responded to
// 5 taps each), and the UP-walk vector never changed -- on foot the arrows WALK
// screen-relative, and the screen->world mapping differs by region (UP was NE at
// the Dollet jam, NW near Galbadia Garden). So on-foot steering cannot use a
// heading or control the camera; instead it MEASURES the screen->world basis
// from the character's own motion (press an arrow, read the world delta) and
// presses the arrow combo whose screen direction points at the target. The
// basis is measured at drive start (a brief UP then RIGHT probe) and refreshed
// live from single-arrow motion, so it tracks the camera as it swings. Vehicle
// drives keep the existing heading-based steering (cars rotate-then-go).
enum DriveCalPhase { DCAL_NONE = 0, DCAL_PROBE_UP, DCAL_SETTLE_UR, DCAL_PROBE_RIGHT, DCAL_DONE };
static DriveCalPhase s_driveCalPhase = DCAL_DONE;
static DWORD    s_driveCalStart = 0;
static int32_t  s_driveCalX = 0, s_driveCalY = 0;   // current probe's start position
static int      s_driveCalTry = 0;
static double   s_camUx = 0.0, s_camUy = -1.0;      // screen-UP    -> world unit vector (default North)
static double   s_camRx = 1.0, s_camRy =  0.0;      // screen-RIGHT -> world unit vector (default East)
static bool     s_camBasisValid = false;
static bool     s_drivePrevUp = false, s_drivePrevDown = false, s_drivePrevLeft = false, s_drivePrevRight = false;
static int32_t  s_drivePrevX = 0, s_drivePrevY = 0;
static bool     s_drivePrevHadKeys = false;
static DWORD    s_driveSidestepUntil = 0;           // screen-relative un-wedge: slide laterally past an obstacle
static int      s_driveSidestepSign  = 1;           // alternates each stuck (right / left)

// #67 v0.18.3.77: GREEDY EMPIRICAL ARROW-PROBE state (replaces the basis decision).
// Hold ONE cardinal arrow; each window measure whether it moved the character
// toward the steer target; keep it if so, else rotate to the next cardinal.
// Trusts only MEASURED progress -- immune to camera swing / wall-slide that
// defeated the maintained screen->world basis across the .74/.75/.76 BATs.
static int      s_driveProbeArrow   = 0;     // 0=UP 1=RIGHT 2=DOWN 3=LEFT (committed cardinal)
static bool     s_driveProbeValid   = false; // false until the first window is armed
static int32_t  s_driveProbeAnchorX = 0, s_driveProbeAnchorY = 0;  // position at window start
static DWORD    s_driveProbeTime    = 0;     // window start tick
static int      s_driveProbeFails   = 0;     // consecutive non-progressing windows

static const DWORD  DRIVE_CAL_PROBE_MS      = 320;   // hold each calibration probe arrow this long
static const DWORD  DRIVE_CAL_SETTLE_MS     = 150;   // settle between the UP and RIGHT probes
static const int    DRIVE_CAL_MIN_MOVE      = 60;    // min world units moved to accept a probe
static const int    DRIVE_CAL_MAX_TRY       = 3;     // probe retries before falling back
static const double STEER_AXIS_C            = 0.383; // cos(67.5deg): 8-way arrow thresholds
static const int    DRIVE_BASIS_REFRESH_MIN = 10;    // #67 v0.18.3.75: min single-arrow move (units) to refresh the basis. Was 40 -- ABOVE the per-tick walk delta (~15u), so the .74 BAT never refreshed and the basis went stale over a 15km drive as the camera swung. 10 tracks walking motion while still excluding wedge jitter (<1u/tick).
static const double DRIVE_BASIS_EMA         = 0.30;  // EMA weight pulling the basis toward fresh motion
static const double DRIVE_BASIS_AGREE_MIN   = 0.50;  // #67 v0.18.3.76: only refine the basis when observed motion agrees with the pressed arrow's predicted direction (dot > this, ~within 60deg). The .75 BAT tracked fine on open ground but a wall stall at the route corner fed perpendicular/reversed slide motion into the refresh, rotating uHat ~90deg off and driving him BACKWARD. Rejecting disagreeing motion blocks that while still allowing gradual camera tracking (which always agrees closely).
static const DWORD  DRIVE_SIDESTEP_MS       = 700;   // lateral slide burst on a mid-route stuck

// #67 v0.18.3.77: greedy arrow-probe tuning.
static const DWORD  DRIVE_PROBE_WINDOW_MS    = 250;   // hold one cardinal this long, then evaluate the window
static const double DRIVE_PROBE_MIN_MOVE     = 40.0;  // must actually move this far in the window (else the arrow is walled -> rotate)
static const double DRIVE_PROBE_ALIGN_MIN    = 0.55;  // window motion must align with the target dir within ~57deg to KEEP the arrow
static const int    DRIVE_PROBE_MAX_FAILS    = 8;     // this many non-progressing windows in a row -> re-plan (genuine pocket / dead end)

// #67 v0.18.3.61: per-tick steering DIAGNOSTIC. DRIVE_STEER_DIAG gates a
// throttled [DRIVE-DIAG] trace in UpdateAutoDrive (position + delta-moved,
// heading + delta, target bearing, relBearing/off, keys pressed, and which band
// the law took) so the stuck-follow behavior is finally visible tick-by-tick
// instead of inferred from 3-second stuck-checks. The delta-moved per line tells
// pivot-in-place (no forward key, heading turning) apart from forward-into-
// collision (UP pressed but position frozen). Per-session diagnostic -- set
// false (or remove this block + the trace) before the #67 push.
static const bool   DRIVE_STEER_DIAG            = false;  // #67 SHIPPED v0.18.3.87: Dollet on-foot drive arrived; trace retired
static const int    DRIVE_STEER_DIAG_INTERVAL_MS = 200;    // throttle (~5 lines/sec)

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
static int      s_driveReplanCount       = 0;      // #67 v0.18.3.59: mid-route recovery re-plans used this drive
static bool     s_driveApproachAnnounced = false;  // one-shot guard
static bool     s_driveOnFootAtStart     = true;   // v0.14.87: captured at StartAutoDrive; arrival semantics differ
static DWORD    s_finalApproachEnterTick = 0;      // v0.14.87: when player crossed below FINAL_APPROACH_DIST (0 = not yet)

// v0.14.103.7: per-destination foot-friendliness flag, computed once at
// AD start by IsLocationFootFriendly(). When TRUE, a foot trigger exists
// for the destination's region (engine will auto-dismount cars onto it):
// keep the standard DRIVE_BOUNCE_ABORT_THRESHOLD=2 retry budget. When
// FALSE, the destination's region is vehicle-only (Garden-style landing
// pad like Balamb Garden's region 0x0C, only program 20 = top_vehicle=
// Garden, no foot clause): cars will never trigger entry, so bounce-
// arrived fires on the FIRST sweep-abort (~13s instead of ~27s). The
// sweep-abort hook reads this flag via the inline ternary at the
// threshold check. Defaults to TRUE so any failure to compute the flag
// (region map not loaded, destination off-map, etc.) preserves
// v0.14.103.6 behavior rather than firing bounce-arrived prematurely.
static bool         s_destFootFriendly            = true;

// v0.16.0.2: per-drive planner-eligibility flag, captured once at
// StartAutoDrive from s_destPlannerEligible[locIdx] and consulted by the
// replan path in Poll() after world-map re-entry. The v0.16.0.1 BAT
// showed Fire Cavern's drive correctly skipping the planner at start
// (Part C), but the replan-on-world-map-re-entry path called
// PlanDrivePath() unconditionally, converting a clean simple-coord drive
// into a planner-routed drive toward the closest-active-region fallback
// (Balamb Town's seg(18,20)). Part B caught the off-target arrival, but
// Fire Cavern was never reached. Storing the decision once at
// drive-start lets Poll()'s replan honor the same gate. Defaults to TRUE
// so any future code path that doesn't set this flag preserves
// pre-v0.16.0.2 behavior (always call planner).
static bool         s_drivePlannerEligible        = true;

static const int    DRIVE_BOUNCE_ABORT_THRESHOLD = 2;
static int          s_sweepAbortCount             = 0;     // increments on each sweep-abort, reset on AD start

// v0.14.96: deferred arrival decision state. Use the game's settled game
// mode AFTER the world-map exit to disambiguate field-entry from random
// encounter. Decision table:
//   MODE_FIELD (1)        : entered a field — REAL ARRIVAL
//   MODE_SWIRL (3)        : pre-battle swirl — ENCOUNTER (paused)
//   MODE_BATTLE (999)     : in battle — ENCOUNTER (paused)
//   MODE_AFTER_BATTLE (4) : post-battle return — ENCOUNTER (paused)
//   anything else (incl. lingering MODE_WORLDMAP=2) : keep waiting
// Wait timeout: ARRIVAL_DECISION_TIMEOUT_MS. On timeout, fall back to the
// v0.14.95 segment-membership / distance heuristic as safety net.
//
// While awaiting decision: drive keys are released (no arrow-key injection
// during field/battle), s_driveActive stays true (so cancel works), Poll()
// runs ResolveDeferredArrival() each tick before its !s_onWorldMap early
// return so the decision can resolve.
static const DWORD ARRIVAL_DECISION_TIMEOUT_MS = 2000;
static bool  s_driveAwaitingArrivalDecision = false;
static DWORD s_driveExitTick                = 0;

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
// v0.14.102: Restore gas-pedal injection from v0.11.14 design. The car's
// forward/gas key is A (VK=0x41, scan=0x1E) — must be HELD continuously
// like a real gas pedal; arrow keys steer only and don't accelerate the
// car. ALWAYS injected alongside UP arrow because the locomotion byte at
// 0x02040A5E reads non-canonical values (e.g. 21 for rental car) when in
// vehicles, making reliable car detection impossible — the always-inject
// approach is harmless on foot (A is unbound for walking; the engine
// ignores it) and essential for cars. Critically, A is NOT an extended
// key, so PressKey/ReleaseKey are called with extended=false.
static bool s_keyGasHeld   = false;
// #67 v0.18.3.68: DOWN arrow (reverse), VK_DOWN (0x28), scan 0x50, extended.
// Drives the wedge-recovery reverse burst -- backing off a wall the drive
// cannot turn away from (on the world map the character only rotates WHILE
// moving forward, so a forward-wedge freezes the heading; reversing regains
// motion and room).
static bool s_keyDownHeld  = false;

// ============================================================================
// Terrain grid + BFS reachability state (v0.14.85, extended in v0.14.103)
// ============================================================================
// v0.14.103: extended from binary (0=land, 1=ocean) to 3-state classifier.
// Forest segments (terrain values 0-5 per `Plan & Research Documents/World
// Map Terrain and Locomotion Reference.md`) are walkable on foot/Chocobo but
// IMPASSABLE for cars (the engine's collision system rejects forest entry
// when the player is in a vehicle from the 32-40 locomotion family). Foot AD
// retains its existing land-only filter (forest counts as land for foot).
// Car AD adds forest as an impassable class.
//
// Loaded once at module init from wmx.obj inside world.fs. s_reachable[row]
// [col] is rebuilt per catalog build via BFS flood-fill from the player's
// current segment.
enum SegTerrainClass : uint8_t {
    SEG_LAND     = 0,   // walkable by all locomotion classes (default)
    SEG_FOREST   = 1,   // walkable on foot/Chocobo; impassable for cars
    SEG_OCEAN    = 2,   // walkable only by Garden/Ragnarok
    SEG_MOUNTAIN = 3    // #67: wmx terrain type 29. Impassable on foot (confirmed steep:
                        // avg face slope 57deg, the map's most common land type). BAT 1
                        // treats it as walkable land while the terrain logger calibrates
                        // it against real movement; BAT 2 flips it to blocked for foot.
                        // The coarse s_terrainGrid never emits this -- only the fine grid.
};
static uint8_t s_terrainGrid[WMX_SEG_ROWS][WMX_SEG_COLS];   // values from SegTerrainClass
static uint8_t s_reachable  [WMX_SEG_ROWS][WMX_SEG_COLS];
static bool    s_terrainLoaded = false;

// ============================================================================
// #67: Fine rasterized walkable grid + continuous-flood-fill reachability
// ============================================================================
// The 32x24 segment grid (s_terrainGrid) is too coarse for reachability: with
// the corrected coordinate mapping, NO land/ocean threshold both keeps same-
// continent coastal locations (e.g. Fire Cavern) AND separates continents
// across thin ocean straits (majority-rule drops coastal land; any-land bridges
// every continent into one blob). The faithful model rasterizes the wmx polygons
// into a fine grid (1024-unit cells, 256x192) and flood-fills in continuous
// space -- ocean straits block, mesh T-junctions don't. Validated offline 17/17
// (Balamb incl. Fire Cavern isolated; every other continent correctly unreachable
// on foot). See `Plan & Research Documents/World Map Reachability Rework -
// offline wmx analysis findings.md`.
//
// s_walkClassFine holds a SegTerrainClass per fine cell -- the class of the wmx
// polygon containing the cell centre, or SEG_OCEAN where no land polygon covers
// it. s_reachFine is the per-build flood-fill visited mask. s_walkGridLoaded
// gates the catalog onto the fine path; on load failure the catalog stays
// unfiltered (safer than a wrong filter).
static const int WM_FINE_CELL = 1024;                        // world units per fine cell
static const int WM_FINE_COLS = 256;                         // 262144 / 1024
static const int WM_FINE_ROWS = 192;                         // 196608 / 1024
static uint8_t s_walkClassFine[WM_FINE_ROWS][WM_FINE_COLS];  // SegTerrainClass per fine cell
static uint8_t s_reachFine     [WM_FINE_ROWS][WM_FINE_COLS]; // flood-fill visited (0/1)
static bool    s_walkGridLoaded = false;

// #67 BAT 2 (slope-aware mountains): per-fine-cell steepness = the elevation
// spread (max - min vertex elevation) of the wmx polygon whose centre is in
// the cell. A MOUNTAIN-class cell whose steepness exceeds WM_MTN_STEEP_BLOCK is
// an impassable steep face for foot/chocobo/car; gentler mountain cells are
// passes/plateaus and stay walkable, so the map doesn't over-fragment (blanket
// type-29 blocking isolated Esthar and split Edea/Centra -- real passes exist).
// Garden/Ragnarok ignore it (hover/fly). The threshold was calibrated offline
// against destination reachability -- at 256, every Galbadia and Balamb catalog
// destination stays reachable and the full reachable set is unchanged vs no
// blocking -- and is refined in-game via the [WM-CALIB] steepness trace (if the
// player is ever logged standing on a BLOCKED cell, the threshold is too low).
static uint16_t s_steepFine[WM_FINE_ROWS][WM_FINE_COLS];
static const uint16_t WM_MTN_STEEP_BLOCK = 256;

// #67 v0.18.3.81: Dollet false-coast no-walk patch bounds (world-coord AABB).
// The thin coastal cliff ledge SE of Dollet (between the mountain and the bay)
// reads as walkable LAND in wmx but is impassable on foot in-game; the route
// planner shortcut straight up it and wedged the on-foot drive (issue #67). No
// geometric rule -- terrain class, steepness, clearance, OR grid resolution --
// separates this ledge from genuine land (validated offline across 7 distinct
// rules: blanket cliff-blocking and coastal-ledge-blocking both DISCONNECT
// Dollet from the same-continent start; 512-res reconnects the road but breaks
// the Balamb Town->Fire Cavern regression; road-attraction reroutes only with
// the clearance penalty disabled and is parameter-fragile). The difference
// exists only in the engine's live collision, which offline geometry cannot
// recover. Dollet is a one-off (coast on one side, the Timber->Dollet canyon
// road on the other), so LoadTerrainGrid (segments.inl) marks this AABB
// impassable by coordinate -- the surgical fix that cannot disconnect anything
// elsewhere on the map. Bounds cover the ledge between Squall's wedge
// (-24252,-26310) and Dollet (-15639,-39437); the canyon road to the WEST
// stays open, so the clearance-weighted planner routes up the canyon and Dollet
// remains reachable (both offline-validated against the live planner cost).
// Expressed as world coords and converted via the same WorldX/YToFine* mapping
// as everything else, so a future coordinate-mapping change can't desync it.
static const int32_t DOLLET_COAST_X0 = -24576;   // -> fine col 104 (west edge)
static const int32_t DOLLET_COAST_X1 = -17408;   // -> fine col 111 (east edge)
static const int32_t DOLLET_COAST_Y0 = -37888;   // -> fine row 59 (north edge)
static const int32_t DOLLET_COAST_Y1 = -27648;   // -> fine row 69 (south edge)

// #67 BAT 2 stage 2 (v0.18.3.59): per-fine-cell CLEARANCE = the Chebyshev
// distance (in cells, capped at 255) to the nearest BLOCKED cell (ocean OR
// steep-mountain). Computed once at grid-load by a multi-source BFS from every
// blocked cell. The drive planner uses it to route down the CENTRE of walkable
// corridors instead of the wall-hugging shortest path: a Dijkstra step into a
// low-clearance cell costs extra (WM_CLEAR_PENALTY per cell below
// WM_CLEAR_TARGET), so the route threads the canyon centre wherever any margin
// exists. Offline analysis of the Dollet canyon: the wall-hugging shortest path
// runs 21 cells within 1 cell of a wall; the clearance-weighted route cuts that
// to 15 and lifts average clearance 1.2 -> 1.7. Blocked cells have clearance 0;
// the field is computed against the foot/car blocker set (ocean + steep
// mountain), which is the relevant one for every planner-eligible drive.
static uint8_t s_clearFine[WM_FINE_ROWS][WM_FINE_COLS];
static const int WM_CLEAR_PENALTY = 20;  // #67 v0.18.3.70: extra Dijkstra cost per clearance-cell below target. Raised 6->20 to PREFER OPEN GROUND over short distance (Aaron: prioritize an open, clean path over minimizing distance). At 20 a wall/water-adjacent cell (clearance 1) costs ~81 vs 1 for open, so the route detours far to stay clear of edges -- the .69 BAT screenshot showed the drive sawing east-west along a shoreline with wide-open grass right beside it, because the old weight (6) let the route hug the coast.
static const int WM_CLEAR_TARGET  = 5;   // #67 v0.18.3.70: want >=5 cells of clearance (raised 3->5); below that the per-cell penalty applies, so the route stays well clear of water/mountain edges and only dips to low clearance at the unavoidable coastal destination itself

// ============================================================================
// Segment-region byte map (v0.14.94)
// ============================================================================
// 32x24 byte map from wmsetus.obj Section 2. Each byte is the region ID for
// one segment; the 0xFF08 region operands in s_triggerPrograms[]'s clauses
// reference these bytes. Loaded once at module init by LoadTriggerZones
// (which already reads the archive for diagnostic dumping — we extract
// Section 2's bytes into s_segmentRegionMap[] in the same pass to avoid
// duplicate I/O). Index as s_segmentRegionMap[row][col] where row=0..23 and
// col=0..31. The byte at file offset (row*32 + col) is the region for that
// segment; 0xFF means 'no region' (typically deep ocean cells); non-FF bytes
// are region IDs in the 0x00..0x45 range observed in the v0.14.93 BAT dump.
// (The 4-byte trailer at file offset 768, '00 00 00 00', is a section
// terminator and is not part of the data.)
//
// AD path planner uses this to construct goal sets: for a catalog target
// (X, Y), find the matching s_triggerPrograms[] entry, collect every cell
// in s_segmentRegionMap whose byte equals any matching clause's region
// operand — that's the equivalent trigger zone. Multi-target A* then runs
// from the player's current segment to the closest goal cell.
static uint8_t s_segmentRegionMap[WMX_SEG_ROWS][WMX_SEG_COLS];
static bool    s_segmentRegionLoaded = false;

// ============================================================================
// Path planner state (v0.14.94)
// ============================================================================
// A* on the 32x24 segment grid produces a sequence of segment cells from
// the player's start segment to a goal segment. The drive then follows the
// path one waypoint at a time, advancing the index when the player crosses
// into the current waypoint's segment. Replanned on world-map re-entry
// after a battle pause (random encounters drift the player off the path).
//
// Encoding: each waypoint is packed as (row << 8) | col into a uint16_t.
// 768 waypoints is a hard upper bound (visiting every segment exactly once);
// real paths are typically <40 waypoints across the longest reachable
// diagonal. s_drivePathPlanned distinguishes 'planner produced a path'
// (segment-membership arrival, waypoint steering) from 'planner declined or
// failed' (catalog-center steering, distance-based arrival fallback).
static const int DRIVE_PATH_MAX = WMX_PLAYABLE_SEGS;   // 768

static uint16_t s_drivePath[DRIVE_PATH_MAX];           // packed (row<<8)|col waypoints
static int      s_drivePathLen      = 0;
static int      s_drivePathIdx      = 0;
static bool     s_drivePathPlanned  = false;

// Goal-segment set for arrival detection. Populated when the planner picks
// a destination program: every cell in s_segmentRegionMap whose byte equals
// any matching clause's region operand. The player entering ANY of these
// via world-map exit counts as arrival. Stored as packed (row<<8)|col, max
// 768 entries (entire grid in the degenerate case).
static uint16_t s_driveGoalSegs[DRIVE_PATH_MAX];
static int      s_driveGoalSegCount = 0;

static inline uint16_t PackSeg(int row, int col)
{
    return (uint16_t)(((row & 0xFF) << 8) | (col & 0xFF));
}
static inline int UnpackRow(uint16_t s) { return (s >> 8) & 0xFF; }
static inline int UnpackCol(uint16_t s) { return  s       & 0xFF; }
