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
    uint16_t destFieldId;     // destination field ID (0xFFFF = unused)
    char    destFieldName[64]; // looked up from field.fl if possible
};

// Trigger zone info extracted from INF file (at offset 0x140).
// These are walkmesh line segments that activate JSM line entity scripts
// when the player crosses them.
struct TriggerInfo {
    float   centerX;          // midpoint X of the two vertices
    float   centerZ;          // midpoint Z of the two vertices
    int16_t x1, z1, x2, z2;  // raw vertex coordinates
    int     triggerIdx;       // INF trigger index (0-15)
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

// Load gateway exits from the INF file for the given field.
// gateways: array to receive gateway data.
// maxGateways: size of the gateways array (INF supports up to 12).
// outCount: number of active (non-0xFFFF) gateways found.
// Returns true if the INF file was found and parsed.
bool LoadINFGateways(const char* fieldName, GatewayInfo* gateways, int maxGateways, int& outCount);

// Load trigger zones from the INF file for the given field.
// Triggers are at offset 0x140, 16 entries of 24 bytes each.
// These map to JSM line entities and define walkmesh activation zones.
// triggers: array to receive trigger data.
// maxTriggers: size of the triggers array (INF supports up to 16).
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

// Shut down and release memory.
void Shutdown();

// Is the archive reader initialized?
bool IsReady();

}  // namespace FieldArchive
