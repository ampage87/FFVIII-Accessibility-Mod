// catalog_harness.cpp — committable regression guard for the FIELD "catalog".
//
// Compiles the REAL consolidated catalog assembly (src/field_catalog.inl:
// RefreshCatalog() + its mid-function fragments + the diag/lateres helpers) off
// the game, drives it with a battery of fully-specified fake field states
// (fixtures embedded below), dumps s_catalog for each in a stable, deterministic
// text format, and diffs that dump against a committed golden
// (tests/catalog_golden.txt). Any mismatch exits non-zero — so an unintended
// change to catalog LOGIC (classification, injection, naming, dedupe, filtering)
// breaks CI. The mere fact that it COMPILES is itself protection: it pins the
// real catalog's struct/static/seam surface, so an incompatible refactor breaks
// the build.
//
// This is a LOGIC-INTEGRITY SNAPSHOT, not a game-accuracy oracle: every engine
// input is a deterministic stub/fixture, so the golden is a pure function of the
// catalog code. It is NOT tied to any live game state.
//
// DESIGN — nothing is masked:
//   * Every engine helper whose RETURN VALUE gates catalog branching is either
//     (a) copied VERBATIM from the mod (geometry, party names, shaft, naming,
//         JSM/type table lookups) so it is a faithful function of fixture-set
//         state (s_walkmesh / s_capturedLines / s_jsmEntities / tables / varblock),
//     (b) an adaptation reading the fixture's own fake entity array
//         (GetEntityPos / GetEntitySetpc), or
//     (c) driven by a fixture-settable global (party leader / controlled roster /
//         background presence).
//   * The hardcoded absolute-address engine reads (party formation @0x01CFE74C /
//     0x01CFE990, the exit varblock @0x01CFE9B8) are backed by a real
//     mmap(MAP_FIXED) region, so ReadShaftFloor / ShaftCatalogDryRun /
//     DumpPartyStateOnce / DumpPuzzleDiagOnce / ResolveStateExclusionGroups all
//     run for real.
//
// Build (from repo root):
//   g++ -std=c++17 -O2 -w tests/catalog_harness.cpp -Isrc -o catalog_harness
// Run (from repo root):
//   ./catalog_harness              # diff against tests/catalog_golden.txt; nonzero on mismatch
//   ./catalog_harness --print      # emit the dump to stdout (does not diff)
//   ./catalog_harness --golden P   # diff against golden file P instead of the default
//
// REGENERATE the golden after an INTENTIONAL catalog change (review the diff!):
//   ./catalog_harness --print > tests/catalog_golden.txt

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <strings.h>
#include <sys/mman.h>

// ---- Windows-API shims ------------------------------------------------------
typedef uint32_t DWORD;
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#define __try try
#define __except(x) catch (...)
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#endif

// ---- Real mod headers (Linux-portable) --------------------------------------
#include "field_archive.h"
#include "entity_classifications.h"
#include "field_display_names.h"

// ---- Log stub ---------------------------------------------------------------
namespace Log { inline void Field(const char*, ...) {} }

// ============================================================================
// mmap-backed engine memory for the hardcoded absolute reads.
//   0x01CFE74C party savemap / 0x01CFE990 field party  (DumpPartyStateOnce)
//   0x01CFE9B8 EXIT_VARBLOCK_BASE (ReadShaftFloor / ShaftCatalogDryRun /
//              DumpPuzzleDiagOnce / ResolveStateExclusionGroups)
// One 16 KB region from a page-aligned base covers 0x01CFE9B8 + up to 0x2000.
// ============================================================================
static const uintptr_t VB_BASE = 0x01CFE9B8;
static bool g_mmapOk = false;
// (mapEngineMemory / clearEngineMemory / setVarByte are defined below, after the
//  backing-store globals they reference.)

// ============================================================================
// FF8Addresses globals + fixture backing store + fixture-settable controls
// ============================================================================
static const int   HB_MAX_ENT  = 32;
static const DWORD HB_STRIDE   = 0x264;
static const int   HB_MAX_BG   = 48;
static const DWORD HB_BGSTRIDE = 0x1B4;

// The mod is a Win32 (32-bit) DLL: several catalog paths read the entity/bg
// array base via *(uint32_t*)ptr and compare (uint32_t)base. On 64-bit that
// truncates a high pointer to garbage. So the fake arrays live in a LOW (<4 GB)
// mmap region — then the low 32 bits ARE the full address and the mod's
// 32-bit pointer arithmetic round-trips exactly.
static const uintptr_t LOW_ARENA = 0x02000000;   // 32 MB
static bool     g_lowOk      = false;
static uint8_t* g_entBase    = nullptr;          // LOW_ARENA + 0x00000
static uint8_t* g_bgBase     = nullptr;          // LOW_ARENA + 0x20000
static uint8_t* g_entBasePtr = nullptr;          // FF8Addresses::pFieldStateOthers -> &this
static uint8_t* g_bgBasePtr  = nullptr;
static uint8_t  g_entCount   = 0;
static uint8_t  g_bgCount    = 0;
static uint16_t g_fieldId    = 0;

static uint8_t  g_leaderChar      = 0xFF;   // GetFieldPartyLeaderChar()
static uint8_t  g_controlled[8]   = {};     // IsInFieldControlledParty() roster
static int      g_controlledCount = 0;
static bool     g_hasBg           = false;  // HasFieldStateBackgrounds()

static void mapEngineMemory() {
    void* p = mmap((void*)0x01CFE000, 0x4000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    g_mmapOk = (p != MAP_FAILED);
    if (!g_mmapOk) fprintf(stderr, "WARNING: mmap(0x01CFE000) failed — shaft/state fixtures may misbehave\n");
    void* a = mmap((void*)LOW_ARENA, 0x40000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    g_lowOk = (a != MAP_FAILED);
    if (g_lowOk) { g_entBase = (uint8_t*)LOW_ARENA; g_bgBase = (uint8_t*)(LOW_ARENA + 0x20000); }
    else {
        fprintf(stderr, "WARNING: low-arena mmap failed — falling back to static arrays (bg lateres path disabled)\n");
        static uint8_t se[HB_MAX_ENT * 0x264], sb[HB_MAX_BG * 0x1B4];
        g_entBase = se; g_bgBase = sb;
    }
}
static void clearEngineMemory() { if (g_mmapOk) memset((void*)0x01CFE000, 0, 0x4000); }
static void setVarByte(unsigned off, uint8_t v) { if (g_mmapOk) *(uint8_t*)(VB_BASE + off) = v; }

namespace FF8Addresses {
    uint8_t** pFieldStateOthers          = nullptr;
    uint8_t*  pFieldStateOtherCount      = nullptr;
    uint8_t** pFieldStateBackgrounds     = nullptr;
    uint8_t*  pFieldStateBackgroundCount = nullptr;
    uint16_t* pCurrentFieldId            = nullptr;
    char*     pCurrentFieldName          = nullptr;
    bool HasFieldStateBackgrounds() { return g_hasBg; }
}

namespace FieldNavigation {

// ---- Constants (verbatim from field_navigation.cpp) ----
static const int   MAX_ENTITIES     = 32;
static const int   MAX_BG_ENTITIES  = 48;
static const int   MAX_CATALOG      = 64;
static const DWORD ENTITY_STRIDE    = 0x264;
static const DWORD BG_STRIDE        = 0x1B4;
static const int   MAX_SYM_NAMES    = 64;
static const int   MAX_JSM_ENTITIES = 128;
static const int   MAX_GATEWAYS     = 12;
static const int   MAX_DEDUP_GATEWAYS = 12;
static const int   MAX_CAPTURED_LINES = 32;
static const int   MAX_SET3_CAPTURES  = 64;

// ---- Types (verbatim) ----
enum EntityType { ENT_UNKNOWN = 0, ENT_NPC, ENT_OBJECT, ENT_EXIT, ENT_BG_NPC, ENT_BG_OBJECT,
                  ENT_SAVE_POINT, ENT_DRAW_POINT, ENT_SHOP, ENT_CARD_GAME, ENT_INTERACTION };
static const char* EntityTypeName(EntityType t) {
    switch (t) {
        case ENT_NPC: return "NPC"; case ENT_OBJECT: return "Object"; case ENT_EXIT: return "Exit";
        case ENT_BG_NPC: return "NPC"; case ENT_BG_OBJECT: return "Object";
        case ENT_SAVE_POINT: return "Save Point"; case ENT_DRAW_POINT: return "Draw Point";
        case ENT_SHOP: return "Shop"; case ENT_CARD_GAME: return "Card Game";
        case ENT_INTERACTION: return "Interaction"; default: return "Entity";
    }
}
struct EntityInfo { int entityIdx; int16_t modelId; uint16_t triangleId; EntityType type; char name[48]; int gatewayIdx; };
struct DedupGateway { float centerX, centerY; uint16_t destFieldId; char displayName[48]; int count; };
struct CapturedTriggerLine {
    uint32_t entityAddr; int lineOrder; int16_t x1,y1,z1,x2,y2,z2; bool active; char name[48];
    FieldArchive::JSMEntityType lineType; int destFieldId; bool hasExtDispatch; bool hasDialogReqTarget;
};
struct CapturedSET3 { uint32_t entityAddr; int16_t posX, posY, posZ; uint16_t triId; bool firstLogged; };

// ---- Statics (all catalog-touched file-scope state) ----
static int      s_playerEntityIdx = -1;
static char     s_symNames[MAX_SYM_NAMES][32] = {};
static int      s_symNameCount = 0, s_symOthersOffset = 0;
static int      s_jsmDoors = 0, s_jsmLines = 0, s_jsmBackgrounds = 0, s_jsmOthers = 0;
static FieldArchive::GatewayInfo s_gateways[MAX_GATEWAYS] = {};
static int      s_gatewayCount = 0;
// v0.20.7: duplicated-room phantom suppression (set by ComputeDupRoomSuppression in the
// real build; fixtures set it directly here to exercise the InjectMapExits drop).
static uint16_t s_dupSuppressDests[8] = {};
static int      s_dupSuppressCount = 0;
static char     s_currentFieldName[64] = {};  // v0.20.9: empty here -> [PUZZLE-GATE] self-gate never fires in the harness
static DedupGateway s_dedupGateways[MAX_DEDUP_GATEWAYS] = {};
static int      s_dedupGatewayCount = 0;
static FieldArchive::JSMEntityInfo s_jsmEntities[MAX_JSM_ENTITIES] = {};
static int      s_jsmEntityCount = 0;
static bool     s_jsmTriangleApprox[MAX_JSM_ENTITIES] = {};
static bool     s_jsmStateSuppressed[MAX_JSM_ENTITIES] = {};
static EntityInfo s_catalog[MAX_CATALOG] = {};
static int      s_catalogCount = 0, s_nonPlayerCount = 0, s_selectedCatalogIdx = 0;
static FieldArchive::WalkmeshData s_walkmesh = {};
static CapturedTriggerLine s_capturedLines[MAX_CAPTURED_LINES] = {};
static int      s_capturedLineCount = 0;
static CapturedSET3 s_set3Captures[MAX_SET3_CAPTURES] = {};
static int      s_set3CaptureCount = 0;
static uint16_t s_entTalkRadius[MAX_ENTITIES] = {};
static bool     s_entSeenTalkable[MAX_ENTITIES] = {};
static bool     s_entDiagDumped = true, s_bgDiagDumped = true, s_extScanDumped = true,
                s_coordDiagDumped = true, s_puzzleDiagDumped = false,
                s_partyDiagDumped = false, s_scanTraced = false;

// ============================================================================
// Helpers copied VERBATIM (faithful; inputs are all fixture-controlled)
// ============================================================================
static const char* PartyCharacterNameById(uint8_t setpc) {
    static const char* const kChars[] = { "Squall","Zell","Irvine","Quistis","Rinoa","Selphie","Seifer","Edea" };
    if (setpc < (uint8_t)(sizeof(kChars)/sizeof(kChars[0]))) return kChars[setpc];
    return nullptr;
}
static bool IsPartyCharacterSetpc(uint8_t setpc) { return PartyCharacterNameById(setpc) != nullptr; }
static bool IsPrisonShaftFieldId(uint16_t fid) { return (fid >= 0x0319 && fid <= 0x032E) || fid == 0x03C5; }
static const uintptr_t SHAFT_VB_BASE = 0x01CFE9B8;
static const unsigned  SHAFT_VB_FLOOR = 0x01B5;
static int ReadShaftFloor() {
    uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    if (!IsPrisonShaftFieldId(fid)) return -1;
    int v = -1;
    __try { v = (int)*(volatile uint8_t*)(SHAFT_VB_BASE + SHAFT_VB_FLOOR) + 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}
static int s_shaftDryRunLastFloor = -999;
static unsigned ShaftVarByte(unsigned off) {
    unsigned v = 0xFFFFu;
    __try { v = *(volatile uint8_t*)(uintptr_t)(SHAFT_VB_BASE + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0xFFFFu; }
    return v;
}
static void ShaftCatalogDryRun() {
    uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    if (!IsPrisonShaftFieldId(fid)) { s_shaftDryRunLastFloor = -999; return; }
    int floorNow = ReadShaftFloor();
    if (floorNow == s_shaftDryRunLastFloor) return;
    s_shaftDryRunLastFloor = floorNow;
    static const unsigned kWin[][2] = { {0x01A0u,0x01DFu}, {0x0700u,0x071Fu} };
    for (int w = 0; w < 2; w++) for (unsigned b = kWin[w][0]; b <= kWin[w][1]; b += 16) (void)ShaftVarByte(b);
}
static EntityClassificationType LookupEntityType(const char* symName) {
    if (!symName || symName[0] == '\0') return EC_NONE;
    for (const EntityTypeEntry* e = ENTITY_TYPE_TABLE; e->sym != nullptr; e++)
        if (_stricmp(symName, e->sym) == 0) return e->type;
    return EC_NONE;
}
static const FieldArchive::JSMEntityInfo* FindJSMBySym(const char* symName) {
    if (!symName || symName[0] == '\0') return nullptr;
    for (int j = 0; j < s_jsmEntityCount; j++)
        if (s_jsmEntities[j].symName[0] != '\0' && _stricmp(s_jsmEntities[j].symName, symName) == 0)
            return &s_jsmEntities[j];
    return nullptr;
}
static EntityType JSMTypeToCatalogType(FieldArchive::JSMEntityType jt) {
    switch (jt) {
        case FieldArchive::JSM_ENT_SAVE_POINT:         return ENT_SAVE_POINT;
        case FieldArchive::JSM_ENT_DRAW_POINT:         return ENT_DRAW_POINT;
        case FieldArchive::JSM_ENT_SHOP:               return ENT_SHOP;
        case FieldArchive::JSM_ENT_CARD_GAME:          return ENT_CARD_GAME;
        case FieldArchive::JSM_ENT_MAP_EXIT:           return ENT_EXIT;
        case FieldArchive::JSM_ENT_NPC:                return ENT_NPC;
        case FieldArchive::JSM_ENT_LADDER:             return ENT_OBJECT;   // ITEM-1 (mirrors the fix_ladder_nav helpers patch)
        case FieldArchive::JSM_ENT_INTERACTIVE_OBJECT: return ENT_OBJECT;
        default:                                       return ENT_UNKNOWN;
    }
}
static void ResolveFriendlyName(const char* symName, char* outBuf, int outBufSize) {
    if (!symName || symName[0] == '\0') { outBuf[0] = '\0'; return; }
    for (const EntityDisplayName* m = ENTITY_DISPLAY_NAMES; m->sym != nullptr; m++)
        if (_stricmp(symName, m->sym) == 0) { strncpy(outBuf, m->display, outBufSize-1); outBuf[outBufSize-1]='\0'; return; }
    char temp[48]; strncpy(temp, symName, 47); temp[47]='\0';
    int len = (int)strlen(temp);
    if (len>2 && (strcmp(temp+len-2,"_u")==0||strcmp(temp+len-2,"_n")==0||strcmp(temp+len-2,"_s")==0)) { temp[len-2]='\0'; len-=2; }
    if (len>5 && _stricmp(temp+len-5,"dummy")==0) { temp[len-5]='\0'; len-=5; }
    if (temp[0]>='a'&&temp[0]<='z') temp[0]=temp[0]-'a'+'A';
    for (int i=0;i<len;i++) if (temp[i]=='_') temp[i]=' ';
    strncpy(outBuf, temp, outBufSize-1); outBuf[outBufSize-1]='\0';
}
static const char* GetFieldDisplayName(uint16_t fieldId) {
    if (fieldId < FIELD_DISPLAY_NAMES_COUNT) return FIELD_DISPLAY_NAMES[fieldId];
    return nullptr;
}
static bool IsBgControllerName(const char* symName) {
    if (!symName || symName[0] == '\0') return true;
    for (const char** p = ENTITY_SKIP_NAMES; *p; p++) if (_stricmp(symName, *p) == 0) return true;
    if (strstr(symName,"jump")||strstr(symName,"Jump")) return true;
    if (strncmp(symName,"to_",3)==0||strncmp(symName,"To_",3)==0) return true;
    return false;
}
static bool IsSeparatedByTriggerLine(float px, float py, float ex, float ey, int skipTriggerIdx = -1) {
    for (int t = 0; t < s_capturedLineCount; t++) {
        if (!s_capturedLines[t].active) continue;
        if (t == skipTriggerIdx) continue;
        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
            s_capturedLines[t].lineType != FieldArchive::JSM_ENT_UNKNOWN) continue;
        float lx1=(float)s_capturedLines[t].x1, ly1=(float)s_capturedLines[t].y1;
        float lx2=(float)s_capturedLines[t].x2, ly2=(float)s_capturedLines[t].y2;
        float ldx=lx2-lx1, ldy=ly2-ly1;
        float cp = ldx*(py-ly1)-ldy*(px-lx1), ce = ldx*(ey-ly1)-ldy*(ex-lx1);
        if (cp*ce < -1.0f) return true;
    }
    return false;
}
static int Orient2D(float px, float py, float qx, float qy, float rx, float ry) {
    float v = (qx-px)*(ry-py)-(qy-py)*(rx-px);
    if (v>1.0f) return 1; if (v<-1.0f) return -1; return 0;
}
static bool SegmentsCross(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
    int o1=Orient2D(ax,ay,bx,by,cx,cy), o2=Orient2D(ax,ay,bx,by,dx,dy);
    int o3=Orient2D(cx,cy,dx,dy,ax,ay), o4=Orient2D(cx,cy,dx,dy,bx,by);
    return (o1!=o2 && o3!=o4 && o1!=0 && o2!=0 && o3!=0 && o4!=0);
}
static bool IsInsideWalkmesh(float x, float y) {
    if (!s_walkmesh.valid || s_walkmesh.numTriangles == 0) return false;
    for (int t = 0; t < s_walkmesh.numTriangles; t++) {
        int v0=s_walkmesh.triangles[t].vertexIdx[0], v1=s_walkmesh.triangles[t].vertexIdx[1], v2=s_walkmesh.triangles[t].vertexIdx[2];
        if (v0<0||v0>=s_walkmesh.numVertices||v1<0||v1>=s_walkmesh.numVertices||v2<0||v2>=s_walkmesh.numVertices) continue;
        float ax=s_walkmesh.vertices[v0].x, ay=s_walkmesh.vertices[v0].y;
        float bx=s_walkmesh.vertices[v1].x, by=s_walkmesh.vertices[v1].y;
        float cx=s_walkmesh.vertices[v2].x, cy=s_walkmesh.vertices[v2].y;
        float d1=(x-bx)*(ay-by)-(ax-bx)*(y-by);
        float d2=(x-cx)*(by-cy)-(bx-cx)*(y-cy);
        float d3=(x-ax)*(cy-ay)-(cx-ax)*(y-ay);
        bool hasNeg=(d1<0)||(d2<0)||(d3<0), hasPos=(d1>0)||(d2>0)||(d3>0);
        if (!(hasNeg && hasPos)) return true;
    }
    return false;
}

// ---- Helpers ADAPTED to the fixture's fake entity array ----
static bool GetEntityPos(int idx, float& cx, float& cy) {
    if (idx < 0 || idx >= MAX_ENTITIES || !FF8Addresses::pFieldStateOthers) return false;
    uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
    if (!base) return false;
    uint8_t* blk = base + ENTITY_STRIDE * idx;
    uint16_t tri = *(uint16_t*)(blk + 0x1FA);
    if (tri == 0) return false;
    cx = (float)(*(int32_t*)(blk + 0x190) / 4096);
    cy = (float)(*(int32_t*)(blk + 0x194) / 4096);
    return true;
}
static uint8_t GetEntitySetpc(int idx) {
    if (idx < 0 || idx >= MAX_ENTITIES || !FF8Addresses::pFieldStateOthers) return 0xFE;
    uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
    if (!base) return 0xFE;
    return *(base + ENTITY_STRIDE * idx + 0x255);
}

// ---- Helpers driven by fixture-settable globals ----
static uint8_t GetFieldPartyLeaderChar() { return g_leaderChar; }
static bool IsInFieldControlledParty(uint8_t charId) {
    for (int i = 0; i < g_controlledCount; i++) if (g_controlled[i] == charId) return true;
    return false;
}

// ============================================================================
// The REAL catalog assembly — the consolidated single-file catalog, compiled
// straight out of the shipped source tree (resolved via -Isrc). This is the
// exact code that runs in the mod; nothing here is a re-implementation.
// ============================================================================
// v0.20.8: real impl in field_nav_duproom.inl (not compiled here); stub keeps existing fixtures unaffected.
static bool IsWorldMapStaging(uint16_t) { return false; }

#include "field_catalog.inl"

}  // namespace FieldNavigation

namespace FieldArchive { const char* JSMEntityTypeName(JSMEntityType) { return "JSM"; } }

// ============================================================================
// Fake walkmesh: one big square (±4000) as two triangles. On-mesh = inside it.
// ============================================================================
static FieldArchive::WalkmeshVertex   g_wmVerts[4];
static FieldArchive::WalkmeshTriangle g_wmTris[2];
static void initWalkmesh() {
    g_wmVerts[0] = {(int16_t)-4000,(int16_t)-4000,0};
    g_wmVerts[1] = {(int16_t) 4000,(int16_t)-4000,0};
    g_wmVerts[2] = {(int16_t) 4000,(int16_t) 4000,0};
    g_wmVerts[3] = {(int16_t)-4000,(int16_t) 4000,0};
    g_wmTris[0] = {}; g_wmTris[0].vertexIdx[0]=0; g_wmTris[0].vertexIdx[1]=1; g_wmTris[0].vertexIdx[2]=2;
    g_wmTris[0].centerX = 1333.f; g_wmTris[0].centerY = -1333.f;
    g_wmTris[1] = {}; g_wmTris[1].vertexIdx[0]=0; g_wmTris[1].vertexIdx[1]=2; g_wmTris[1].vertexIdx[2]=3;
    g_wmTris[1].centerX = -1333.f; g_wmTris[1].centerY = 1333.f;
}

// ============================================================================
// Reset ALL catalog-touched state to a clean field-load baseline.
// ============================================================================
static void resetState() {
    using namespace FieldNavigation;
    memset(g_entBase, 0, HB_MAX_ENT * 0x264); g_entCount = 0; g_entBasePtr = g_entBase;
    memset(g_bgBase, 0, HB_MAX_BG * 0x1B4);   g_bgCount  = 0; g_bgBasePtr  = g_bgBase;
    g_fieldId = 0; g_leaderChar = 0xFF; g_controlledCount = 0; g_hasBg = false;
    memset(g_controlled, 0, sizeof(g_controlled));
    clearEngineMemory();

    s_playerEntityIdx = -1;
    memset(s_symNames, 0, sizeof(s_symNames)); s_symNameCount = 0; s_symOthersOffset = 0;
    s_jsmDoors = s_jsmLines = s_jsmBackgrounds = s_jsmOthers = 0;
    memset(s_gateways, 0, sizeof(s_gateways)); s_gatewayCount = 0;
    s_dupSuppressCount = 0;
    memset(s_dedupGateways, 0, sizeof(s_dedupGateways)); s_dedupGatewayCount = 0;
    memset(s_jsmEntities, 0, sizeof(s_jsmEntities)); s_jsmEntityCount = 0;
    memset(s_jsmTriangleApprox, 0, sizeof(s_jsmTriangleApprox));
    memset(s_jsmStateSuppressed, 0, sizeof(s_jsmStateSuppressed));
    memset(s_catalog, 0, sizeof(s_catalog)); s_catalogCount = 0; s_nonPlayerCount = 0; s_selectedCatalogIdx = 0;
    memset(s_capturedLines, 0, sizeof(s_capturedLines)); s_capturedLineCount = 0;
    memset(s_set3Captures, 0, sizeof(s_set3Captures)); s_set3CaptureCount = 0;
    memset(s_entTalkRadius, 0, sizeof(s_entTalkRadius));
    memset(s_entSeenTalkable, 0, sizeof(s_entSeenTalkable));
    s_entDiagDumped = true; s_bgDiagDumped = true; s_extScanDumped = true; s_coordDiagDumped = true;
    s_puzzleDiagDumped = false; s_partyDiagDumped = false; s_scanTraced = false;
    s_shaftDryRunLastFloor = -999;
    s_walkmesh.numVertices = 4; s_walkmesh.numTriangles = 2;
    s_walkmesh.vertices = g_wmVerts; s_walkmesh.triangles = g_wmTris; s_walkmesh.valid = true;

    FF8Addresses::pFieldStateOthers          = &g_entBasePtr;
    FF8Addresses::pFieldStateOtherCount      = &g_entCount;
    FF8Addresses::pFieldStateBackgrounds     = &g_bgBasePtr;
    FF8Addresses::pFieldStateBackgroundCount = &g_bgCount;
    FF8Addresses::pCurrentFieldId            = &g_fieldId;
    FF8Addresses::pCurrentFieldName          = (char*)"test_field";
}

// ============================================================================
// Fixture builders
// ============================================================================
static void ENT(int i, int16_t model, uint16_t tri, int32_t x, int32_t y, uint8_t setpc,
                uint8_t talk, uint8_t push, uint8_t thru, bool hide = false) {
    uint8_t* b = g_entBase + i * HB_STRIDE;
    *(uint32_t*)(b + 0x160) = hide ? 0x08u : 0u;
    *(int32_t*) (b + 0x190) = x * 4096;
    *(int32_t*) (b + 0x194) = y * 4096;
    *(int32_t*) (b + 0x198) = 0;
    *(uint16_t*)(b + 0x1FA) = tri;
    *(int16_t*) (b + 0x218) = model;
    *(b + 0x249) = push; *(b + 0x24B) = talk; *(b + 0x24C) = thru; *(b + 0x255) = setpc;
    if (i + 1 > g_entCount) g_entCount = (uint8_t)(i + 1);
}
static void SYM(int idx, const char* s) {
    strncpy(FieldNavigation::s_symNames[idx], s, 31);
    if (idx + 1 > FieldNavigation::s_symNameCount) FieldNavigation::s_symNameCount = idx + 1;
}
static void LINE(int t, FieldArchive::JSMEntityType type, int dest,
                 int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t z1 = 0, int16_t z2 = 0,
                 bool dialogReq = false, uint32_t entAddr = 0) {
    using namespace FieldNavigation;
    CapturedTriggerLine& L = s_capturedLines[t];
    L = {}; L.active = true; L.lineType = type; L.destFieldId = dest;
    L.x1=x1; L.y1=y1; L.x2=x2; L.y2=y2; L.z1=z1; L.z2=z2; L.hasDialogReqTarget = dialogReq; L.entityAddr = entAddr;
    if (t + 1 > s_capturedLineCount) s_capturedLineCount = t + 1;
}
static FieldArchive::JSMEntityInfo& JSM(int j, FieldArchive::JSMEntityType type, int cat, const char* sym) {
    using namespace FieldNavigation;
    FieldArchive::JSMEntityInfo& e = s_jsmEntities[j];
    e = {}; e.jsmIndex = j; e.jsmCategory = cat; e.type = type; e.param = 0; e.drawPointTriggerOf = -1;
    strncpy(e.symName, sym, 31);
    if (j + 1 > s_jsmEntityCount) s_jsmEntityCount = j + 1;
    return e;
}
static void JSMpos(FieldArchive::JSMEntityInfo& e, int16_t x, int16_t y, uint16_t tri) {
    e.hasPosition = true; e.posX = x; e.posY = y; e.posTriangle = tri;
}
static void GW(int g, uint16_t dest, float cx, float cy) {
    using namespace FieldNavigation;
    s_gateways[g] = {}; s_gateways[g].destFieldId = dest; s_gateways[g].centerX = cx; s_gateways[g].centerZ = cy;
    if (g + 1 > s_gatewayCount) s_gatewayCount = g + 1;
}

// ============================================================================
// Fixtures
// ============================================================================
static void fx_empty() { /* entCount stays 0 -> early-return path */ }

static void fx_players_and_npcs() {
    ENT(0,  1, 10,  100,  100, 0x00, 0,0,0);            // player/leader (setpc0) -> filtered
    ENT(1, 15, 11,  200,  150, 0xFE, 1,0,0);            // talkable NPC
    ENT(2, 12, 12, -100,  200, 0xFE, 0,1,0);            // push-radius visible NPC (model>=10)
    ENT(3, 20, 13,  300,  -50, 0xFE, 0,0,0);            // plain visible NPC (on-mesh)
    ENT(4,  8, 14,   50,  300, 0xFE, 0,0,1);            // walk-through visible NPC
}
static void fx_leader_not_squall() {
    g_leaderChar = 2;                                   // party led by Irvine
    ENT(0, 0, 10, 100, 100, 0x00, 0,0,0);               // Squall follower (setpc0)
    ENT(1, 2, 11, 200, 120, 0x02, 0,0,0);               // Irvine = leader -> player, filtered
    ENT(2,15, 12, -80, 200, 0xFE, 1,0,0);               // NPC kept
}
static void fx_assembly_and_partyfilter() {
    g_controlled[0]=0; g_controlled[1]=2; g_controlled[2]=3; g_controlledCount=3;
    ENT(0, 0, 10, 100, 100, 0x00, 0,0,0);               // leader(0) -> player, filtered
    ENT(1, 2, 11, 200, 120, 0x02, 0,0,0);               // roster member; assembly -> kept "Irvine"
    ENT(2, 4, 12, -80, 200, 0x04, 0,0,0);               // setpc4 NOT in roster, placed -> sceneAssembly; "Rinoa"
    ENT(3,15, 13,  40,-100, 0xFE, 1,0,0);               // plain talkable NPC
}
static void fx_walkmesh_exclusion() {
    ENT(0, 1, 10,  100, 100, 0x00, 0,0,0);              // player
    ENT(1,20, 11, 9999,   0, 0xFE, 0,0,0);              // off-mesh, no talk/push -> EXCLUDED
    ENT(2,20, 12, 9999, 100, 0xFE, 1,0,0);              // off-mesh but talkable   -> KEPT
    ENT(3,20, 13,  200, 200, 0xFE, 0,0,0);              // on-mesh, no talk/push    -> KEPT
}
static void fx_hidden_and_pushonly() {
    ENT(0, 1, 10,  100, 100, 0x00, 0,0,0);              // player
    ENT(1,15, 11,  200, 100, 0xFE, 0,0,0, /*hide*/true);// HIDE flag -> SCAN-DROP
    ENT(2, 5, 12,  -80, 120, 0xFE, 0,1,0);              // push-only visible model<10 -> push-only skip
    ENT(3,15, 13,   40,-100, 0xFE, 1,0,0);              // normal NPC kept
    ENT(4,-1, 14,  300, 300, 0xFE, 0,0,0); SYM(4,"drpoint"); // invisible special-JSM (draw pt) kept
    JSMpos(JSM(0, FieldArchive::JSM_ENT_DRAW_POINT, 3, "drpoint"), 300, 300, 14);
}
static void fx_naming_paths() {
    ENT(0, 1, 10, 100, 100, 0x00, 0,0,0);               // player
    ENT(1,15, 11, 200, 100, 0xFE, 1,0,0); SYM(1,"cardgamemaster"); // type table -> Card Player
    ENT(2,15, 12,-100, 120, 0xFE, 1,0,0); SYM(2,"dp5");            // pattern -> Draw Point
    ENT(3,24, 13,  40,-100, 0xFE, 0,0,0);                          // model 24 -> Save Point
    ENT(4,15, 14, 300, 300, 0xFE, 1,0,0); SYM(4,"savex");          // pattern -> Save Point
    ENT(5,15, 15,-300,-300, 0xFE, 1,0,0); SYM(5,"svp2");           // JSM override -> Save Point
    JSM(0, FieldArchive::JSM_ENT_SAVE_POINT, 3, "svp2");
}
static void fx_trigline_exits() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player at origin (tri!=0)
    LINE(0, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND, 100,  -400, 1000, -400, 1200); // normal exit
    LINE(1, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND,  -2,  1000, -400, 1200, -400); // world map
    LINE(2, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND, 0x7FFFFFF, 2000, 100, 2000, 300); // unresolved -> recover
    GW(0, 250, 2000.f, 200.f);                          // nearby gateway supplies recovered dest
}
static void fx_trigline_event() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    LINE(0, FieldArchive::JSM_ENT_LINE_EVENT,   -1, -800,-500, -800, 500);   // EVENT: transparent (not emitted)
    LINE(1, FieldArchive::JSM_ENT_LINE_TRIGGER, -1,  300, -50,  300,  50);   // generic trigger -> "Event"
}
static void fx_trigline_interaction() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    LINE(0, FieldArchive::JSM_ENT_LINE_INTERACTIVE, -1,  300, -50,  300, 50); // -> "Interaction 1"
    LINE(1, FieldArchive::JSM_ENT_LINE_INTERACTIVE, -1, -300, -50, -300, 50); // -> "Interaction 2"
}
static void fx_trigline_solo_statue() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    LINE(0, FieldArchive::JSM_ENT_LINE_INTERACTIVE, -1, 300, -50, 300, 50);   // lone interactive
    JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "megami");            // -> solo name "Statue"
}
static void fx_jsm_specials() {
    ENT(0, 1, 10, 100, 100, 0x00, 0,0,0);
    JSMpos(JSM(0, FieldArchive::JSM_ENT_SAVE_POINT, 3, "savePoint"), 300, 250, 14);
    JSMpos(JSM(1, FieldArchive::JSM_ENT_DRAW_POINT, 3, "dp01"),     -300, 250, 14);
    JSMpos(JSM(2, FieldArchive::JSM_ENT_SHOP,       3, "shop"),      300,-250, 14);
    JSMpos(JSM(3, FieldArchive::JSM_ENT_CARD_GAME,  3, "cardm"),    -300,-250, 14);
    { auto& e = JSM(4, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "dic"); e.hasTalkSetup = true; JSMpos(e, 50, 50, 14); }
}
static void fx_jsm_drawpoint_consolidation() {
    ENT(0, 1, 10, 100, 100, 0x00, 0,0,0);               // player
    ENT(1,15, 11, 500, 250, 0xFE, 1,0,0);               // NPC near JSM draw point -> reclassified Draw Point
    JSMpos(JSM(0, FieldArchive::JSM_ENT_DRAW_POINT, 3, "drpoint"), 500, 250, 14);
}
static void fx_mapexits() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player
    GW(0, 999, 3000.f, 0.f);                            // an INF gateway on the field
    { auto& e = JSM(0, FieldArchive::JSM_ENT_MAP_EXIT, 3, "door"); e.param = 120; e.paramFromInterp = true; JSMpos(e, 400, 0, 14); } // injected
    { auto& e = JSM(1, FieldArchive::JSM_ENT_MAP_EXIT, 3, "l1");   e.param = (int)0x80000000; }          // dropped (unpositioned, unresolved)
    { auto& e = JSM(2, FieldArchive::JSM_ENT_MAP_EXIT, 3, "elev"); e.param = 500; JSMpos(e, -400, 0, 14);} // filtered (no gateway match)
    { auto& e = JSM(3, FieldArchive::JSM_ENT_MAP_EXIT, 3, "warp"); e.param = 990; JSMpos(e, 0, 400, 14); } // filtered (param>982 w/ gateway)
}
static void fx_duproom_phantom() {
    // v0.20.7: the room (via ComputeDupRoomSuppression, stubbed here) says dest 726 is
    // served by a real INF gateway in a SIBLING copy of this room and this copy has no
    // local gateway there -> a scripted exit to 726 is a cutscene phantom and is dropped;
    // a scripted exit to a non-duplicated dest (717) is a unique exit and is kept.
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    FieldNavigation::s_dupSuppressDests[0] = 726; FieldNavigation::s_dupSuppressCount = 1;
    { auto& e = JSM(0, FieldArchive::JSM_ENT_MAP_EXIT, 3, "rinoa"); e.param = 726; e.paramFromInterp = true; JSMpos(e, 100, 100, 14); } // DROP
    { auto& e = JSM(1, FieldArchive::JSM_ENT_MAP_EXIT, 3, "trap");  e.param = 717; e.paramFromInterp = true; JSMpos(e, 200, 200, 14); } // KEEP
}
static void fx_gateways_merge() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    GW(0, 300, 100.f, 100.f);                           // two gateways same dest, near -> merge count=2
    GW(1, 300, 150.f, 120.f);
}
static void fx_gateways_ring_split() {
    g_fieldId = 0x031A;                                 // ring half -> Top/Bottom naming
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    GW(0, 795, 0.f,  2000.f);                           // same dest, far apart -> SPLIT into 2 groups
    GW(1, 795, 0.f, -2000.f);
}
static void fx_dedupe_object_line() {
    // Collisions are at ~50 units (nonzero, < DUP_DIST 128) so the dump is
    // sensitive to a dedupe-threshold change, not just an exact-overlap.
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);
    LINE(0, FieldArchive::JSM_ENT_LINE_INTERACTIVE, -1, 350, -20, 350, 20);   // line ctr (350,0); raw-sym object 50u away -> object dropped
    { auto& e = JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "kanban1"); JSMpos(e, 300, 0, 14); }
    LINE(1, FieldArchive::JSM_ENT_LINE_INTERACTIVE, -1, -350, -20, -350, 20);  // line ctr (-350,0); named object 50u away -> line dropped
    { auto& e = JSM(1, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "dic"); e.hasTalkSetup = true; JSMpos(e, -300, 0, 14); }
}
static void fx_dedupe_entity_special() {
    ENT(0, 1, 10,   0,  0, 0x00, 0,0,0);                // player
    ENT(1,15, 11, 100, 50, 0xFE, 0,0,0);                // generic NPC ~50u from the save line -> dropped (< ENT_DUP_DIST 96)
    LINE(0, FieldArchive::JSM_ENT_LINE_INTERACTIVE, -1, 50, 50, 50, 50);        // save line ctr (50,50) (isSaveLine)
    { auto& e = JSM(0, FieldArchive::JSM_ENT_SAVE_POINT, 1, "savePoint"); e.isSaveLine = true; }
    FieldNavigation::s_jsmDoors = 0;                    // captured line t -> jsm doors+t = 0
}
static void fx_sentinel_overlap() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player
    for (int j = 0; j < 100; j++) JSM(j, FieldArchive::JSM_ENT_UNKNOWN, 3, ""); // pad slots 0..99
    { auto& e = JSM(100, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "obj100"); e.hasTalkSetup = true; JSMpos(e, 200, 0, 14); } // -> sentinel -400
    GW(0, 260, 500.f, 0.f);                             // gateway -> sentinel -400 too (gwIdx=0)
}
static void fx_prison_stairs() {
    g_fieldId = 0x0320;                                 // in shaft range
    setVarByte(0x01B5, 5);                              // floor byte 5 -> Floor 6
    ENT(0, 1, 10, -100, -100, 0x00, 0,0,0);             // player (tri!=0)
    LINE(0, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND, 0x0320, -2150, -197, -2150,   0, -68, -55); // -> Stairs down
    LINE(1, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND, 0x0320, -2276,  269, -2276, 400, 352, 391); // -> Stairs up
}
static void fx_state_exclusion() {
    setVarByte(0x0154, 0);                              // live byte 0 anchors the group
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player
    auto& a = JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "ladline5"); // conditional wants 9 -> SUPPRESS
    a.hasStateGuard=true; a.stateVarAddr=0x0154; a.stateVarValue=9; a.stateGuardConditional=true; a.hasTalkSetup=true; JSMpos(a, 200, 0, 14);
    auto& b = JSM(1, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "ladline6"); // unconditional wants 0 -> KEEP
    b.hasStateGuard=true; b.stateVarAddr=0x0154; b.stateVarValue=0; b.stateGuardConditional=false; b.hasTalkSetup=true; JSMpos(b, -200, 0, 14);
    auto& c = JSM(2, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "saku3");    // unconditional wants 3 -> KEEP (Gate 3)
    c.hasStateGuard=true; c.stateVarAddr=0x0154; c.stateVarValue=3; c.stateGuardConditional=false; c.hasTalkSetup=true; JSMpos(c, 0, 200, 14);
}
static void fx_lateres_centroid() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player
    auto& e = JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "saku1");    // -> "Gate 1"
    e.hasPshmCoords = true; e.posTriangle = 1; e.hasTalkSetup = true;          // resolved to tri-1 centroid
}
static void fx_lateres_bg() {
    if (!g_lowOk) return;                               // needs the low arena for 32-bit bg-pointer round-trip
    g_hasBg = true;                                     // exercise the cat-2 background live-read path
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player
    // fake background entity slot 0 with a live position
    uint8_t* bb = g_bgBase + 0 * HB_BGSTRIDE;
    *(int32_t*)(bb + 0x190) = 700 * 4096; *(int32_t*)(bb + 0x194) = 350 * 4096; g_bgCount = 1;
    FieldNavigation::s_jsmDoors = 0; FieldNavigation::s_jsmLines = 0; FieldNavigation::s_jsmBackgrounds = 1;
    auto& e = JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "book");     // cat2 bg, PSHM coords
    e.hasPshmCoords = true; e.hasTalkSetup = true;
}
static void fx_crossing_filter() {
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player at origin
    LINE(0, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND, 100, 1000, -20, 1000, 20); // target exit (east)
    LINE(1, FieldArchive::JSM_ENT_UNKNOWN,           -1,  500,-500,  500, 500); // blocker crosses path -> filtered
    GW(0, 300, 1000.f, 0.f);                            // gateway behind the blocker -> filtered too
}
static void fx_rebuild_retain() {
    // Exercises the incremental-rebuild path: the "retain existing entries that
    // still qualify" loop + selection restore (only reached on a 2nd build over a
    // non-empty s_catalog). The fixture does the 1st build itself; the harness's
    // RefreshCatalog() is then the 2nd build.
    ENT(0,  1, 10, 100, 100, 0x00, 0,0,0);              // player
    ENT(1, 15, 11, 200, 150, 0xFE, 1,0,0);              // NPC
    ENT(2, 15, 12,-100, 200, 0xFE, 1,0,0);              // NPC
    FieldNavigation::RefreshCatalog();                  // 1st build populates s_catalog
    FieldNavigation::s_selectedCatalogIdx = 1;          // user had entry 1 selected -> restored on rebuild
}
static void fx_screen_filter() {
    // A qualifying NPC on the far side of a SCREEN_BOUND line -> dropped by the
    // entity screen-filter (IsSeparatedByTriggerLine).
    ENT(0, 1, 10,   0, 0, 0x00, 0,0,0);                 // player (west)
    ENT(1,15, 11, 600, 0, 0xFE, 1,0,0);                 // talkable NPC (east) -> screen-filtered out
    LINE(0, FieldArchive::JSM_ENT_LINE_SCREEN_BOUND, -1, 300, -500, 300, 500); // separator (also emits generic Exit)
}
static void fx_jsm_controller_skip() {
    // A JSM Interactive Object whose SYM is a controller/effect name (ENTITY_SKIP_NAMES)
    // -> skipped by IsBgControllerName.
    ENT(0, 1, 10, 0, 0, 0x00, 0,0,0);                   // player
    { auto& e = JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "water"); JSMpos(e, 300, 300, 14); }
}
static void fx_drawpoint_nopos() {
    // JSM Draw Point with NO position -> "no-position fallback" reclassifies the
    // nearest runtime NPC as Draw Point.
    ENT(0, 1, 10,   0,   0, 0x00, 0,0,0);               // player
    ENT(1,15, 11, 200, 100, 0xFE, 1,0,0);               // NPC -> reclassified Draw Point
    JSM(0, FieldArchive::JSM_ENT_DRAW_POINT, 3, "drpoint");  // hasPosition == false
}

// ladder-navigable feature lock (A): a positioned ladder JSM entity whose SYM
// ("hasigomodel") IS in the display-name table. With the ladder-nav feature
// present in field_catalog.inl (JSM_ENT_LADDER whitelisted for injection +
// JSMTypeToCatalogType -> ENT_OBJECT), this injects exactly ONE catalog entry
// typed "Object" named "Ladder". Removing the injection whitelist drops it to
// zero; reverting the type mapping flips the type column — either fails the golden.
static void fx_ladder() {
    ENT(0,1,10,0,0,0x00,0,0,0);                        // player (entCount>0)
    auto& e = JSM(0, FieldArchive::JSM_ENT_LADDER, 3, "hasigomodel"); JSMpos(e, 250, 175, 14); // on-mesh position
}
// ladder-navigable feature lock (B): same, but the SYM is NOT in the display
// table, so the "Ladder" label can ONLY come from the ITEM-1 naming hunk
// (`if (je.type == JSM_ENT_LADDER) jtName = "Ladder"`). This pins that hunk
// independently: revert it and the entry renders as the generic "Object",
// failing the golden. (Synthetic SYM avoids ENTITY_SKIP_NAMES / controller names.)
static void fx_ladder_unnamed() {
    ENT(0,1,10,0,0,0x00,0,0,0);                        // player (entCount>0)
    auto& e = JSM(0, FieldArchive::JSM_ENT_LADDER, 3, "hasigo_probe9"); JSMpos(e, 250, 175, 14); // on-mesh position
}

// v0.19.5 item-pickup relabel lock: a bare model NPC whose walkmesh triangle
// matches a JSM entity flagged isItemPickup is relabeled "NPC" -> "Item N". The
// runtime SYM ("shifted") is deliberately NOT the JSM SYM (mirrors the real
// runtime 'Zell2' vs JSM 'Urakata') -- the link is the SET3 triangle. ENT(2) is
// the CONTROL: same shape on a triangle whose JSM entity is NOT a pickup, so it
// stays "NPC". Revert the v0.19.5 relabel pass and ENT(1) renders "NPC",
// failing the golden. Positions are on-mesh (same coords as kept NPCs above).
static void fx_item_pickup() {
    ENT(0,  1, 10,  100, 100, 0x00, 0,0,0);   // player/leader -> filtered
    ENT(1, 10, 12,  200, 200, 0xFE, 0,0,0);   // bare model NPC, tri 12 -> pickup match -> "Item 1"
    ENT(2, 20, 13,  300, -50, 0xFE, 0,0,0);   // bare model NPC, tri 13 -> no pickup match -> stays "NPC"
    auto& p = JSM(0, FieldArchive::JSM_ENT_UNKNOWN, 3, "urakata_jsm");   // shifted SYM (never matched)
    JSMpos(p, 200, 200, 12); p.hasSetmodelInit = true; p.isItemPickup = true;
    auto& q = JSM(1, FieldArchive::JSM_ENT_UNKNOWN, 3, "npc_ctrl_jsm");  // control: not a pickup
    JSMpos(q, 300, -50, 13); q.hasSetmodelInit = true; q.isItemPickup = false;
}

// v0.20.0: interaction-zone junk gate, RE'd from the field engine. Interaction
// requires a talk/push radius OR a walk-into trigger zone (SETLINE/INF), captured
// as hasNearbyInteractionZone. Drop a marker-positioned Object (hasPshmCoords)
// ONLY when it has no zone AND no own interaction AND no curated name. All objects
// here get a real injected position; the gate scopes on hasPshmCoords. Five cases:
//   objphantom PSHM + no zone + no int + unnamed -> DROPPED (the bghall_5 light)
//   objzone    PSHM + has interaction zone       -> SURVIVES (walk-into trigger)
//   dic        PSHM + curated name ("Directory") -> SURVIVES (named)
//   objtalk    PSHM + own talk interaction       -> SURVIVES (own interaction)
//   objliteral literal position (not PSHM)       -> SURVIVES (out of gate scope)
static void fx_director_gate() {
    ENT(0, 1, 10, 100, 100, 0x00, 0,0,0);   // player/leader -> filtered
    { auto& e = JSM(0, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "objphantom");
      JSMpos(e, 200, 200, 11); e.hasPshmCoords = true; }
    { auto& e = JSM(1, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "objzone");
      JSMpos(e, 300, 300, 12); e.hasPshmCoords = true; e.hasNearbyInteractionZone = true; }
    { auto& e = JSM(2, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 2, "dic");
      JSMpos(e, -200, 200, 13); e.hasPshmCoords = true; }
    { auto& e = JSM(3, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "objtalk");
      JSMpos(e, 400, 100, 15); e.hasPshmCoords = true; e.hasTalkSetup = true; }
    { auto& e = JSM(4, FieldArchive::JSM_ENT_INTERACTIVE_OBJECT, 3, "objliteral");
      JSMpos(e, -300, -100, 16); /* hasPshmCoords stays false -> not in gate scope */ }
}

struct Fixture { const char* name; void (*fn)(); };
static const Fixture kFixtures[] = {
    { "empty_field",                fx_empty },
    { "players_and_npcs",           fx_players_and_npcs },
    { "leader_not_squall",          fx_leader_not_squall },
    { "assembly_and_partyfilter",   fx_assembly_and_partyfilter },
    { "walkmesh_exclusion",         fx_walkmesh_exclusion },
    { "hidden_and_pushonly",        fx_hidden_and_pushonly },
    { "naming_paths",               fx_naming_paths },
    { "trigline_exits",             fx_trigline_exits },
    { "trigline_event",             fx_trigline_event },
    { "trigline_interaction",       fx_trigline_interaction },
    { "trigline_solo_statue",       fx_trigline_solo_statue },
    { "jsm_specials",               fx_jsm_specials },
    { "jsm_drawpoint_consolidation",fx_jsm_drawpoint_consolidation },
    { "mapexits",                   fx_mapexits },
    { "duproom_phantom",            fx_duproom_phantom },
    { "gateways_merge",             fx_gateways_merge },
    { "gateways_ring_split",        fx_gateways_ring_split },
    { "dedupe_object_line",         fx_dedupe_object_line },
    { "dedupe_entity_special",      fx_dedupe_entity_special },
    { "sentinel_overlap",           fx_sentinel_overlap },
    { "prison_stairs",              fx_prison_stairs },
    { "state_exclusion",            fx_state_exclusion },
    { "lateres_centroid",           fx_lateres_centroid },
    { "lateres_bg",                 fx_lateres_bg },
    { "crossing_filter",            fx_crossing_filter },
    { "rebuild_retain",             fx_rebuild_retain },
    { "screen_filter",              fx_screen_filter },
    { "jsm_controller_skip",        fx_jsm_controller_skip },
    { "drawpoint_nopos",            fx_drawpoint_nopos },
    // --- ladder-nav feature locks (29th/30th): exercise the ladder consumer path
    { "ladder_nav",                 fx_ladder },          // table-named -> "Ladder"
    { "ladder_unnamed",             fx_ladder_unnamed },  // pins the ITEM-1 naming hunk
    { "item_pickup",                fx_item_pickup },     // v0.19.5: NPC->Item relabel lock
    { "director_gate",              fx_director_gate },   // v0.19.7: director junk-gate drop lock
};
static const int kFixtureCount = (int)(sizeof(kFixtures)/sizeof(kFixtures[0]));

// ============================================================================
// Deterministic dump of s_catalog identity fields, appended to a buffer.
// ============================================================================
static std::string g_out;
static void appendf(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_out += buf;
}
static void dumpCatalog(const char* name) {
    using namespace FieldNavigation;
    appendf("### FIXTURE %s\n", name);
    appendf("count=%d nonplayer=%d player=%d selected=%d\n",
            s_catalogCount, s_nonPlayerCount, s_playerEntityIdx, s_selectedCatalogIdx);
    for (int c = 0; c < s_catalogCount; c++) {
        const EntityInfo& e = s_catalog[c];
        appendf("  [%d] idx=%d type=%s model=%d tri=%u gw=%d name=\"%s\"\n",
                c, e.entityIdx, EntityTypeName(e.type), (int)e.modelId,
                (unsigned)e.triangleId, e.gatewayIdx, e.name);
    }
    appendf("\n");
}

// Build the full deterministic dump over every fixture into g_out.
static void runAllFixtures() {
    mapEngineMemory();
    initWalkmesh();
    appendf("# catalog_harness logic-integrity dump  (fixtures=%d)\n\n", kFixtureCount);
    for (int i = 0; i < kFixtureCount; i++) {
        fprintf(stderr, "[run] %s\n", kFixtures[i].name);
        resetState();
        kFixtures[i].fn();
        FieldNavigation::RefreshCatalog();
        dumpCatalog(kFixtures[i].name);
    }
}

// Emit a compact unified-style diff (line granularity) so a CI failure shows
// exactly which catalog lines moved. No external tools required.
static void printDiff(const std::string& golden, const std::string& got) {
    std::vector<std::string> a, b;
    { std::stringstream ss(golden); std::string ln; while (std::getline(ss, ln)) a.push_back(ln); }
    { std::stringstream ss(got);    std::string ln; while (std::getline(ss, ln)) b.push_back(ln); }
    size_t n = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++) {
        const std::string* ga = i < a.size() ? &a[i] : nullptr;
        const std::string* gb = i < b.size() ? &b[i] : nullptr;
        if (ga && gb && *ga == *gb) continue;
        if (ga) fprintf(stderr, "  -golden:%zu: %s\n", i + 1, ga->c_str());
        if (gb) fprintf(stderr, "  +actual:%zu: %s\n", i + 1, gb->c_str());
    }
}

static std::string slurp(const char* path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return {}; }
    std::stringstream ss; ss << f.rdbuf(); ok = true; return ss.str();
}

int main(int argc, char** argv) {
    bool printMode = false;
    const char* goldenPath = "tests/catalog_golden.txt";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--print") || !strcmp(argv[i], "--regen")) printMode = true;
        else if (!strcmp(argv[i], "--golden") && i + 1 < argc)          goldenPath = argv[++i];
        else { fprintf(stderr, "usage: %s [--print] [--golden PATH]\n", argv[0]); return 2; }
    }

    runAllFixtures();

    if (printMode) {                       // regenerate mode: emit the dump verbatim
        fwrite(g_out.data(), 1, g_out.size(), stdout);
        return 0;
    }

    bool ok = false;
    std::string golden = slurp(goldenPath, ok);
    if (!ok) {
        fprintf(stderr,
            "catalog_harness: FAIL — cannot open golden '%s'.\n"
            "  Run from the repo root, or regenerate with:\n"
            "    ./catalog_harness --print > tests/catalog_golden.txt\n", goldenPath);
        return 2;
    }
    if (golden == g_out) {
        fprintf(stderr, "catalog_harness: PASS — %d fixtures match %s\n", kFixtureCount, goldenPath);
        return 0;
    }
    fprintf(stderr,
        "catalog_harness: *** FAIL *** — catalog dump differs from golden (%s).\n"
        "  If this change is INTENTIONAL, review the diff and regenerate:\n"
        "    ./catalog_harness --print > tests/catalog_golden.txt\n"
        "  Divergent lines (-golden / +actual):\n", goldenPath);
    printDiff(golden, g_out);
    return 1;
}
