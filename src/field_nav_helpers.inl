// field_nav_helpers.inl — GetEntityPos, ReadVertexCoords, FormatNavComponents, etc.
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// Helpers
// ============================================================================

// v0.14.107: Active party formation lookup.
//
// FF8 stores the active party as a 4-byte array at savemap+0xAF0 (= absolute
// 0x01CFE74C, given the BAT-confirmed savemap base 0x01CFDC5C). Each byte holds
// a character ID 0-7 (0=Squall, 1=Zell, 2=Irvine, 3=Quistis, 4=Rinoa,
// 5=Selphie, 6=Seifer, 7=Edea), or 0xFF for an empty slot. The array is NOT
// compacted: solo Squall reads as [0xFF, 0x00, 0xFF, 0xFF], two-member parties
// may interleave empty slots with active ones. Same address is used by Junction
// TTS and save block content TTS — confirmed reliable across many BATs.
//
// Returns true if charId appears anywhere in the four formation bytes.
// SEH-wrapped because the savemap may be transiently uninitialised during
// load transitions; we treat any read fault as 'no match'.
static bool IsCharacterInActiveParty(uint8_t charId)
{
    __try {
        const uint8_t* formation = (const uint8_t*)0x01CFE74C;
        for (int i = 0; i < 4; i++)
            if (formation[i] == charId) return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// v0.14.107: Map a field-entity model ID to its FF8 character ID, or -1 if
// the model isn't a party-character model.
//
// BAT-confirmed mapping (from ResolveNameByModelId in field_nav_names.inl,
// surveyed across many fields): models 0-7 correspond directly to character
// IDs 0-7. Model 8 is Quistis's uniform variant — same character (charId 3),
// different model slot. Models 10+ are generic NPCs and aren't covered by
// this mapping. Model 9 (occasionally Laguna) is excluded for now to avoid
// overfiltering during early-disc Laguna flashbacks where Laguna IS the
// player; if a later BAT exposes a Laguna-follower scenario this can be
// revisited.
//
// Returns -1 if modelId isn't a party-character slot. The caller is
// responsible for cross-referencing the result against IsCharacterInActiveParty
// before deciding to filter — the engine reuses model slots, so model 7 in an
// early-game field might be a generic background character, not Edea.
static int ModelIdToCharId(int16_t modelId)
{
    if (modelId < 0 || modelId > 8) return -1;
    if (modelId == 8) return 3;  // Quistis uniform variant
    return (int)modelId;
}

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
// v0.07.76: Uses camera-calibrated axes to convert world-space deltas to
// screen-space directions. Before calibration, falls back to raw world coords
// (+X = right, -Y = up). After calibration, projects world delta onto the
// measured camera right/down vectors so directions match the player's screen.
// Each axis is converted to steps independently at 250 world units/step.
// If both axes are tiny, says "right here".
static void FormatNavComponents(float dx, float dy, char* buf, int bufsz)
{
    // v0.07.76: Project world-space delta onto camera axes for screen-relative directions.
    // screenRight = dot(worldDelta, camRight), screenDown = dot(worldDelta, camDown).
    float screenRight = dx * s_camRightX + dy * s_camRightY;
    float screenDown  = dx * s_camDownX  + dy * s_camDownY;

    int hSteps = (int)(fabsf(screenRight) / 250.0f + 0.5f);
    int vSteps = (int)(fabsf(screenDown) / 250.0f + 0.5f);
    const char* hDir = (screenRight >= 0.0f) ? "right" : "left";
    const char* vDir = (screenDown >= 0.0f) ? "down" : "up";

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

// v0.12.10: Look up entity type from comprehensive ENTITY_TYPE_TABLE.
// Returns the EntityClassificationType, or EC_NONE if not found.
static EntityClassificationType LookupEntityType(const char* symName)
{
    if (!symName || symName[0] == '\0') return EC_NONE;
    for (const EntityTypeEntry* e = ENTITY_TYPE_TABLE; e->sym != nullptr; e++) {
        if (_stricmp(symName, e->sym) == 0)
            return e->type;
    }
    return EC_NONE;
}

// v0.07.73: Look up JSM entity classification by SYM name.
// Returns pointer to the matching JSMEntityInfo, or nullptr if not found.
static const FieldArchive::JSMEntityInfo* FindJSMBySym(const char* symName)
{
    if (!symName || symName[0] == '\0') return nullptr;
    for (int j = 0; j < s_jsmEntityCount; j++) {
        if (s_jsmEntities[j].symName[0] != '\0' &&
            _stricmp(s_jsmEntities[j].symName, symName) == 0) {
            return &s_jsmEntities[j];
        }
    }
    return nullptr;
}

// v0.07.73: Map JSM entity type to catalog EntityType.
static EntityType JSMTypeToCatalogType(FieldArchive::JSMEntityType jt)
{
    switch (jt) {
        case FieldArchive::JSM_ENT_SAVE_POINT:        return ENT_SAVE_POINT;
        case FieldArchive::JSM_ENT_DRAW_POINT:        return ENT_DRAW_POINT;
        case FieldArchive::JSM_ENT_SHOP:              return ENT_SHOP;
        case FieldArchive::JSM_ENT_CARD_GAME:         return ENT_CARD_GAME;
        case FieldArchive::JSM_ENT_MAP_EXIT:          return ENT_EXIT;
        case FieldArchive::JSM_ENT_NPC:               return ENT_NPC;
        case FieldArchive::JSM_ENT_INTERACTIVE_OBJECT: return ENT_OBJECT;  // v0.07.98
        default: return ENT_UNKNOWN;
    }
}

