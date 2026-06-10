// field_archive.h - FF8 field archive reader (fi/fl/fs two-level system)
//
// Extracts per-field data files (SYM, INF, etc.) from the game's archive.
// The Steam 2013 PC version stores field data in a two-level fi/fl/fs system:
//
//   Outer archive: field.fi / field.fl / field.fs
//     - field.fl: newline-separated file paths (3 per field: .fi, .fl, .fs)
//     - field.fi: 12-byte index entries (size, offset, compression)
//     - field.fs: raw data blob
//
//   Inner archive: per-field fi/fl/fs extracted from the outer archive
//     - Same structure, contains .jsm, .sym, .inf, .msd, .id, etc.
//
// Usage:
//   FieldArchive::Initialize();  // auto-detects game path from DLL location
//   // Then per field load:
//   char names[16][32];
//   int nameCount;
//   FieldArchive::LoadSYMNames("bcroom_1", names, 16, nameCount);
//
// v05.47

#pragma once

#include <cstdint>
#include <vector>

namespace FieldArchive {

// JSM entity category counts (from JSM header).
struct JSMCounts {
    int doors;        // door entities
    int lines;        // walk-on trigger lines
    int backgrounds;  // background script entities
    int others;       // NPC/character entities (= pFieldStateOthers count)
};

// Gateway info extracted from INF file.
struct GatewayInfo {
    float   centerX;          // midpoint X of the two vertices
    float   centerZ;          // midpoint Z of the two vertices
    // v0.15.9.2.15: Exit line endpoints preserved for crossing detection.
    // The INF gateway is a 2-vertex line segment that the player physically
    // crosses to fire the screen transition. centerX/centerZ are the midpoint
    // (useful for compass / navigation targeting), but to detect crossing we
    // need the full line so we can do cross-product sign-flip detection.
    // Chase auto-pilot reads these via GetGatewayNearestCluster() and passes
    // them to StartChaseDrive.
    int16_t lineX1, lineY1;   // first endpoint (screen X, Y)
    int16_t lineX2, lineY2;   // second endpoint (screen X, Y)
    uint16_t destFieldId;     // destination field ID (0xFFFF = unused)
    char    destFieldName[64]; // looked up from field.fl if possible
};

// Trigger zone info extracted from INF file (at offset 0x1E4).
// v0.12.16: Corrected from disassembly of 0x47B610 (trigger zone scanner).
// 12 entries × 16 bytes each. These define proximity interaction zones
// that activate Background entity scripts when the player is within
// pushRadius of the line AND facing the right direction.
struct TriggerInfo {
    float   centerX;          // midpoint X of the two vertices
    float   centerZ;          // midpoint Y of the two vertices (legacy name)
    int16_t x1, y1, z1;      // line start (3D)
    int16_t x2, y2, z2;      // line end (3D)
    uint8_t entityIndex;      // JSM entity index activated by this zone (0xFF = empty)
    uint8_t interactionType;  // 0-5: even=enter, odd=leave
    int     triggerIdx;       // INF trigger slot index (0-11)
    char    symName[32];      // resolved SYM name for the entity (if available)
};

// Initialize the archive reader.  Auto-detects the game directory from the
// DLL's own file path (dinput8.dll sits in the game root).
// Returns true if the outer field.fl was parsed successfully.
bool Initialize();

// Load entity names from the SYM file for the given field.
// names: array of char[32] buffers to receive entity names.
// maxNames: size of the names array.
// outCount: number of names actually read.
// Returns true if the SYM file was found and parsed.
bool LoadSYMNames(const char* fieldName, char names[][32], int maxNames, int& outCount);

// Look up a field ID by its internal name (e.g. "bghall_1" → 165).
// Searches the FL file index loaded at init time.
// Returns -1 if not found.
int GetFieldIdByInternalName(const char* internalName);

// Load gateway exits from the INF file for the given field.
// gateways: array to receive gateway data.
// maxGateways: size of the gateways array (INF supports up to 12).
// outCount: number of active (non-0xFFFF) gateways found.
// Returns true if the INF file was found and parsed.
bool LoadINFGateways(const char* fieldName, GatewayInfo* gateways, int maxGateways, int& outCount);

// Load trigger zones from the INF file for the given field.
// Triggers are at offset 0x1E4, 12 entries of 16 bytes each.
// Each zone has a line segment, entity index, and interaction type.
// triggers: array to receive trigger data.
// maxTriggers: size of the triggers array (INF supports up to 12).
// outCount: number of active triggers found.
// Returns true if the INF file was found and parsed.
bool LoadINFTriggers(const char* fieldName, TriggerInfo* triggers, int maxTriggers, int& outCount);

// Build a mapping from field ID → field name using field.fl ordering.
// The field ID is the 0-based index of the field in the fl listing
// (each field occupies 3 consecutive entries: fi, fl, fs).
// Returns the field name for the given ID, or nullptr if out of range.
const char* GetFieldNameById(uint16_t fieldId);

// Load JSM entity counts (doors, lines, backgrounds, others).
// Returns true if the JSM header was parsed. The 'others' offset
// (doors + lines + backgrounds) tells you which SYM index corresponds
// to entity state array index 0.
bool LoadJSMCounts(const char* fieldName, JSMCounts& counts);

// ============================================================================
// JSM script scanning — entity classification by opcode signatures
// ============================================================================

// Entity type classified from JSM script content.
enum JSMEntityType {
    JSM_ENT_UNKNOWN = 0,
    JSM_ENT_DRAW_POINT,      // SETDRAWPOINT / DRAWPOINT
    JSM_ENT_SAVE_POINT,      // MENUSAVE / SAVEENABLE
    JSM_ENT_SHOP,            // MENUSHOP
    JSM_ENT_CARD_GAME,       // CARDGAME
    JSM_ENT_LADDER,          // LADDERUP / LADDERDOWN
    JSM_ENT_MAP_EXIT,        // MAPJUMP / MAPJUMP3
    JSM_ENT_NPC,             // SETMODEL + TALKON or other interaction
    JSM_ENT_DOOR,            // JSM Door category entity
    JSM_ENT_LINE_TRIGGER,    // JSM Line category (generic/unclassified trigger)
    JSM_ENT_LINE_CAMERA_PAN, // v0.07.82: Line with BGDRAW/BGOFF/scroll — transparent for screen filtering
    JSM_ENT_LINE_SCREEN_BOUND, // v0.07.82: Line with MAPJUMP — filters entities on other side
    JSM_ENT_LINE_EVENT,      // v0.07.82: Line with SHOW/HIDE/BATTLE — transparent (visual effects only)
    JSM_ENT_LINE_INTERACTIVE, // v0.12.24: Line with MES/ASK/AMES/AASK or ext dispatch — player-facing interaction
    JSM_ENT_BACKGROUND,      // JSM Background category (unclassified)
    JSM_ENT_INTERACTIVE_OBJECT, // v0.07.98: Background entity with dialog opcodes + position (Directory, desks, etc.)
    JSM_ENT_DIRECTOR             // v0.12.20: Invisible Others entity that dispatches interaction zones via REQ (dormitory bed/desk, classroom desk/sign)
};

// Classification result for a single JSM entity.
struct JSMEntityInfo {
    int            jsmIndex;      // index in JSM entity table (Door→Line→Bg→Other order)
    int            jsmCategory;   // 0=Door, 1=Line, 2=Background, 3=Other
    JSMEntityType  type;          // classified type from script scanning
    bool           hasPosition;   // true if SET3/SET position was found in init script
    int16_t        posX, posY, posZ;
    uint16_t       posTriangle;   // walkmesh triangle from SET3/SET inline param
    int            param;         // type-specific: drawPointId, shopId, destFieldId, etc.
    char           symName[32];   // from SYM (empty for doors)
    // v0.07.99: PSHM_W coordinate tracking for interactive objects.
    // When SET3 fires with PSHM_W markers instead of literal coordinates,
    // the memory addresses are stored here for runtime resolution.
    bool           hasPshmCoords; // true if SET3 found but coords are PSHM_W markers
    int16_t        pshmAddrX;     // PSHM_W memory address for X coordinate
    int16_t        pshmAddrY;     // PSHM_W memory address for Y coordinate
    int16_t        pshmAddrZ;     // PSHM_W memory address for Z coordinate
    // v0.12.09: Cross-entity draw point trigger detection.
    // If this entity's talk script calls REQSW/REQEW to a JSM_ENT_DRAW_POINT entity,
    // this field holds the JSM index of that draw point. -1 = not a trigger.
    int            drawPointTriggerOf;
    // v0.12.24: True if entity uses runtime 0x1C extended dispatch (PSHM_W-based).
    // Indicates potential runtime-dispatched dialog opcodes not detectable statically.
    bool           hasExtDispatch;
    // v0.17.7.5.4: True if REQ-following found this Line entity REQs a target
    // entity that has dialog opcodes (MES/ASK/AMES/AASK) or extended dispatch.
    // Distinguishes the genuine "dual-purpose dialog-mediated exit" pattern
    // (e.g. dormitory bed: bed Line REQs bed Background which shows "Sleep?")
    // from the "line uses extended dispatch for non-dialog purposes" pattern
    // (e.g. bgroad_5 squalls: uses 0x1C for sound/particle effects only).
    // Before this flag existed, the catalog used hasExtDispatch for both cases
    // and incorrectly suppressed pure-exit Lines from the catalog's Exit
    // labeling -- BAT'd on bgroad_5 (Hallway 5) where the dormitory exit
    // showed as "Interaction 1" instead of "Exit to Dormitory Double 1".
    // Only set by the scanner's REQ-following post-pass (never by own 0x1C use).
    bool           hasDialogReqTarget;
    // v0.12.16: SETLINE interaction zone from JSM script.
    // SETLINE defines the exact line segment where the player can interact.
    // The line center is the precise interaction position (better than SET3
    // which gives the entity graphic position, not the interaction zone).
    bool           hasSetline;    // true if SETLINE opcode found with literal coords
    int16_t        setlineX1, setlineY1, setlineZ1;
    int16_t        setlineX2, setlineY2, setlineZ2;
    // v0.17.7.1: True if entity's script uses TALKRADIUS or TALKON, meaning the
    // player must press confirm to interact (vs. crossing a Line trigger to
    // fire it). Used by catalog classification to distinguish Interactions
    // from Events for Line entities, and as a keep-condition for the walkmesh
    // exclusion filter (off-walkmesh entities with talk setup are kept because
    // the player can interact from on-walkmesh).
    bool           hasTalkSetup;
    // v0.17.8.8: True if this is a Line entity that the script ties to a save
    // point -- either its own script invokes the save menu (MENUSAVE/SAVEENABLE),
    // or it REQs an entity that is a Save Point (type SAVE_POINT or a save*/svpt
    // SYM name). The Line-classification block reclassifies all Line entities to
    // LINE_* types, which loses the save-ness the type cascade detected; this
    // flag preserves it so the catalog can label the surfaced Interaction as a
    // "Save Point" instead of a generic "Interaction N". Fixes the bghall_1 save
    // point, whose savePoint entity has PSHM-only X/Y and so never resolves a
    // position to inject as a standalone Save Point -- it only surfaces via its
    // co-located trigger line.
    bool           isSaveLine;
    // v0.17.8.15: True if this entity's init script (method 0) contains a
    // SETMODEL opcode -- i.e. the entity loads a 3D model at field load and
    // stands in the world (rather than being a script-only Background, an
    // invisible Director, or a walk-across Line trigger). Combined with
    // `jsmCategory == 3` (Other), this is the catalog's NPC discriminator:
    // a positioned Other-entity with a model that the player walks up to
    // and presses Confirm to interact with, vs. a Line walk-across trigger.
    // Replaces v0.17.8.11's `int setmodelSlot` + chara.one cross-reference,
    // which was reverted after the bghall_3 BAT screenshot showed the
    // model-classification approach was the wrong signal entirely (kanban2
    // IS Xu standing in the world; her chara.one slot p048 was misclassified
    // as a prop but that doesn't matter -- the behavior signal is enough).
    bool           hasSetmodelInit;
};

const char* JSMEntityTypeName(JSMEntityType t);

// Scan all JSM scripts for a field to classify entities by their opcodes.
// Detects draw points, save points, shops, card games, ladders, and map exits.
// Extracts positions from SET3/SET in init scripts.
// outEntities: array to receive classified entities.
// maxEntities: size of outEntities array.
// outCount: number of entities found.
// Returns true if the JSM was parsed successfully.
bool ScanJSMScripts(const char* fieldName, JSMEntityInfo* outEntities, int maxEntities, int& outCount);

// ============================================================================
// Camera axes (.ca file) for screen-space direction mapping
// ============================================================================

// FF8 field camera data extracted from the .ca section of the field archive.
// The camera defines how 3D walkmesh coordinates project to 2D entity/screen
// coordinates. Entity X,Y at offsets 0x190/0x194 are camera-projected:
//   entity_X = dot(camRight, walkmeshPoint3D)
//   entity_Y = dot(camDown,  walkmeshPoint3D)
// So deltas in entity space ARE screen-space deltas:
//   +dX = right on screen, +dY = down on screen.
//
// The .ca format (per Qhimm wiki FF7/FF8 camera section):
//   3 axis vectors as int16 (x,y,z), fixed-point /4096
//   Camera position as 3x int32
//   Zoom as int16
//   ~38 bytes per camera setting; file may contain multiple settings.

struct CameraAxes {
    // Raw axis vectors from .ca file (int16 fixed-point, /4096 for normalized)
    int16_t axis0[3];   // first axis vector (x,y,z)
    int16_t axis1[3];   // second axis vector (x,y,z)
    int16_t axis2[3];   // third axis vector (x,y,z)
    int32_t posX, posY, posZ;  // camera world position
    int16_t zoom;              // zoom/FOV factor
    int     numSettings;       // how many camera settings in the .ca file
    bool    valid;             // true if successfully parsed
};

// Load camera axes from the .ca file for the given field.
// Reads the first camera setting (index 0 = default camera at field load).
// Returns true if the .ca file was found and parsed.
bool LoadCameraAxes(const char* fieldName, CameraAxes& outAxes);

// ============================================================================
// Walkmesh (ID file) data for A* pathfinding
// ============================================================================

struct WalkmeshVertex {
    int16_t x, y, z;
};

struct WalkmeshTriangle {
    uint16_t vertexIdx[3];   // indices into vertex array
    uint16_t neighbor[3];    // adjacent triangle on each edge (0xFFFF = none)
    // Derived at load time:
    float    centerX, centerY; // center of triangle (X, Y screen-space)
};

struct WalkmeshData {
    int numVertices;
    int numTriangles;
    WalkmeshVertex*   vertices;   // heap-allocated
    WalkmeshTriangle* triangles;  // heap-allocated
    bool valid;
};

// Load the walkmesh (ID file) for the given field.
// Populates the output WalkmeshData struct with vertex/triangle/adjacency data.
// Caller owns the allocated arrays and must call FreeWalkmesh() when done.
// Returns true if the ID file was found and parsed.
bool LoadWalkmesh(const char* fieldName, WalkmeshData& outMesh);

// Free heap-allocated walkmesh data.
void FreeWalkmesh(WalkmeshData& mesh);

// v0.12.17: Dump decoded JSM script opcodes for a specific entity.
// Used to analyze interaction logic for Background entities like dic.
// jsmEntityIndex is the flat JSM entity index (Door+Line+BG+Other ordering).
// Logs all methods and decoded opcodes to the accessibility log.
bool DumpEntityScript(const char* fieldName, int jsmEntityIndex);

// v0.18.3.2: Dump the Timber-train code-apparatus entities' JSM scripts (#56).
// Dumps entities named ango*/key* (Angoyarukun code apparatus, Key* key
// supervisors), falling back to all 'Other' entities if no name match. Reuses
// DumpEntityScript; log-only -> ff8_field.log [SCRIPT-DUMP].
bool DumpTrainCodeScripts(const char* fieldName);

// v0.17.7.2: Look up init-method POPM_W writes to a specific varblock address.
// After ScanJSMScripts() has run, this exposes the static init-script writes
// captured per entity (the s_initVarMaps array). The diagnostic block in
// HookedFieldScriptsInit calls this for each unresolved MAPJUMP PSHM address
// to find out which entity writes the destination field ID at field load.
//
// addr: the PSHM varblock address (e.g. extracted from a 0x8000xxxx marker)
// outEntries: caller-provided buffer of (entityIdx, value) pairs filled in
// maxEntries: capacity of outEntries
// Returns: number of writers found (may exceed maxEntries; only the first
//          maxEntries are written to the buffer).
struct InitVarWriter { int entityIdx; int32_t value; };
int LookupInitVarWrites(int16_t addr, InitVarWriter* outEntries, int maxEntries);

// v0.17.7.2: Iterate all init-method writes across the field for summary logging.
// outEntries: caller buffer of (entityIdx, addr, value) tuples
// maxEntries: capacity
// Returns: number of writes found (may exceed maxEntries).
struct InitVarTuple { int entityIdx; int32_t addr; int32_t value; };
int EnumerateInitVars(InitVarTuple* outEntries, int maxEntries);

// Shut down and release memory.
void Shutdown();

// Is the archive reader initialized?
bool IsReady();

}  // namespace FieldArchive
