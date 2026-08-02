// field_nav_helpers.inl — GetEntityPos, ReadVertexCoords, FormatNavComponents, etc.
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// Helpers
// ============================================================================

// v0.17.8.3: Known FF8 field SYM names for playable / party-swap characters.
// Field scripts name the party entities after the character (with optional
// shadow/duplicate suffixes like 'squalls', 'squallsd', 'zells'), so a prefix
// match against these bases identifies a party member regardless of which
// field-local model slot the engine assigned. Draw points ('drpoint'), save
// points ('savePoint'/'saveline'), and generic NPCs never match, so this is a
// safe discriminator for the party filter (see RefreshCatalog). Includes the
// Laguna dream party (laguna/kiros/ward) and the intro/tutorial playables
// (seifer/edea).
// v0.18.3.228: moved here from field_nav_catalog.inl for size compliance.
static bool IsPartyCharacterSym(const char* sym)
{
    if (!sym || sym[0] == '\0') return false;
    static const char* const kBases[] = {
        "squall", "zell", "selphie", "quistis", "rinoa", "irvine",
        "laguna", "kiros", "ward", "seifer", "edea"
    };
    for (int b = 0; b < (int)(sizeof(kBases) / sizeof(kBases[0])); b++) {
        size_t n = strlen(kBases[b]);
        if (_strnicmp(sym, kBases[b], n) == 0) return true;
    }
    return false;
}

// v0.18.3.228: Proper display name for a party-character SYM. Returns the
// capitalized character name ("Squall", "Zell", ...) when the SYM matches a
// known party-character base, else nullptr. Bases are ordered longest-first so a
// longer name is tested before any shorter prefix of it. Used to label
// interactable party members — those the party filter now KEEPS because they
// have talk setup — instead of announcing them as a generic "NPC".
static const char* PartyCharacterDisplayName(const char* sym)
{
    if (!sym || sym[0] == '\0') return nullptr;
    struct NamePair { const char* base; const char* display; };
    static const NamePair kNames[] = {
        { "selphie", "Selphie" }, { "quistis", "Quistis" }, { "squall", "Squall" },
        { "seifer",  "Seifer"  }, { "irvine",  "Irvine"  }, { "laguna", "Laguna" },
        { "rinoa",   "Rinoa"   }, { "kiros",   "Kiros"   }, { "zell",   "Zell"   },
        { "ward",    "Ward"    }, { "edea",    "Edea"    },
    };
    for (int i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); i++) {
        size_t n = strlen(kNames[i].base);
        if (_strnicmp(sym, kNames[i].base, n) == 0) return kNames[i].display;
    }
    return nullptr;
}

// v0.18.3.232: Party-character identity from the entity's setpc byte (0x255).
//
// This is the AUTHORITATIVE party test — far more reliable than the SYM name.
// The engine writes the character ID (0-7) into setpc for an entity that is an
// actual party character, and 0xFE for anything that is not. The catalog already
// trusts this byte to find the player (setpc == 0).
//
// The SYM name cannot be trusted for this: the engine only instantiates the
// ACTIVE party members, while the JSM SYM list names all six playable
// characters, so every NPC slot is shifted. On ggsta1 the party is
// Squall+Zell+Quistis, yet slot2 (setpc=3 = Quistis) carries SYM 'irvine' and the
// station attendant in slot3 carries SYM 'rinoa' — which is exactly why the
// SYM-based party filter deleted the train staff.
static const char* PartyCharacterNameById(uint8_t setpc)
{
    static const char* const kChars[] = {
        "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea"
    };
    if (setpc < (uint8_t)(sizeof(kChars) / sizeof(kChars[0]))) return kChars[setpc];
    return nullptr;   // 0xFE (or anything else) = not a party character
}

// True when this entity IS one of the playable party characters.
static bool IsPartyCharacterSetpc(uint8_t setpc)
{
    return PartyCharacterNameById(setpc) != nullptr;
}

// ============================================================================
// v0.18.3.302 (#91 R1): D-District Prison floor, for the stairs labels.
// ============================================================================
//
// Same source field_announce.cpp uses -- varblock 0x01B5 (437) holds floor - 1.
// Duplicated here rather than shared because that one is a file-static in a
// different translation unit and the value is two reads and an add; a header
// for it would be more machinery than the thing it carries.
//
// Pinned by the .299 auto-capture: two screenshots read "Floor 6" while the
// varblock held 5, and "Floor 4" while it held 3.
static const uintptr_t SHAFT_VB_BASE  = 0x01CFE9B8;  // EXIT_VARBLOCK_BASE
static const unsigned  SHAFT_VB_FLOOR = 0x01B5;      // holds floor - 1

static bool IsPrisonShaftFieldId(uint16_t fid)
{
    return (fid >= 0x0319 && fid <= 0x032E) || fid == 0x03C5;
}

// Returns the 1-based floor, or -1 outside the shaft / on a bad read.
// SEH-guarded, no C++ objects (C2712).
static int ReadShaftFloor()
{
    uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    if (!IsPrisonShaftFieldId(fid)) return -1;
    int v = -1;
    __try {
        v = (int)*(volatile uint8_t*)(SHAFT_VB_BASE + SHAFT_VB_FLOOR) + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = -1;
    }
    return v;
}

// v0.18.3.315 (#110 SOLVED): the field/zone movement basis, straight from the
// engine's OWN per-camera-zone movement-rotation offset. Static RE of
// FF8_EN.exe proved field walk heading = input + [0x1CE4908] (byte, a 256-unit
// angle, 90deg=64); the per-camera-zone handler @0x476B8D loads [0x1CE4908]
// from fielddata[9 + [0x1CE4906]] the instant the player crosses into a camera
// zone (identity zones store the screen-aligned baseline; rotated zones store
// the angle). BAT v0.18.3.314 confirmed the mapping and its sign against three
// independent anchors -- reference identity fields (off=128 -> camRight (1,0),
// camDown (0,-1)), the mod's own empirical bgroad_5 (off=64 -> (0,-1)/(-1,0)),
// and disassembly-proven bg2f_1 (off=192 -> (0,1)/(1,0)) -- and exposed the
// true NON-cardinal rotations the old 90deg quantizer got wrong (bghall_2 +17,
// dorm bgryo1_1 +79, bg2f_2 +101). Baseline 128 = screen identity; each unit is
// 360/256 deg:
//     phi      = (off4908 - 128) * 2*PI / 256
//     camRight = ( cos phi,  sin phi )
//     camDown  = ( sin phi, -cos phi )   [R(-90deg) of camRight; keeps det=-1]
// SEH-guarded, no C++ objects (C2712). Returns false (axes untouched) if the
// offset byte can't be read.
// v0.18.3.316: hardened against the field-entry / session-startup window. BAT
// .315 caught a startup [NAV-ENGOFF] off4908=0 -> +180deg, and the ZONEROT
// probe on the first real field showed WHY: at bghall_1 load, live [0x1CE4908]
// was still 0 while the source byte fielddata[9+cam] already held 128 (identity)
// -- the engine had not yet copied the per-zone byte into the live global. So:
// (1) require the field-data pointer to be plausible -- before the first field
//     of a session it is null/garbage; skipping there leaves the identity
//     default instead of applying a bogus 180deg basis; and
// (2) when the live byte reads 0 (the stale-entry case), fall back to the
//     per-zone SOURCE byte fielddata[9+[0x1CE4906]], which is correct the
//     instant the zone loads. In steady state live == fd[9+cam] (BAT .314), so
//     this is identical to before for every non-zero case that .315 confirmed
//     working; it only repairs the transient/startup zero. The live global stays
//     primary otherwise, so script-driven smooth basis rotations still register.
static bool ReadEngineMoveOffset(unsigned& outOff /* 0..255 */)
{
    unsigned live = 0xFFFFFFFFu, src = 0xFFFFFFFFu;
    uint32_t fdat = 0u;
    unsigned cam  = 0u;
    __try {
        fdat = *(volatile uint32_t*)0x01CDC744u;         // field-data heap ptr (const per field)
        live = *(volatile uint8_t*)0x01CE4908u;          // live applied offset
        cam  = *(volatile uint8_t*)0x01CE4906u;          // active camera-zone index
        if (fdat >= 0x00400000u && fdat < 0x40000000u && cam < 64u)
            src = *(volatile uint8_t*)(uintptr_t)(fdat + 9u + cam);   // per-zone source byte
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        live = 0xFFFFFFFFu; src = 0xFFFFFFFFu;
    }
    if (!(fdat >= 0x00400000u && fdat < 0x40000000u)) return false;  // not in a live field yet
    if (live > 255u) return false;
    if (live == 0u && src <= 255u) live = src;           // stale-entry: trust the source byte
    outOff = live;
    return true;
}
static void EngineOffsetToAxes(unsigned off, float& crx, float& cry,
                               float& cdx, float& cdy)
{
    float phi = ((float)off - 128.0f) * (2.0f * (float)NAV_PI / 256.0f);
    float c = cosf(phi), s = sinf(phi);
    if (fabsf(c) < 1e-6f) c = 0.0f;   // clean fp residue so logs/labels read clean
    if (fabsf(s) < 1e-6f) s = 0.0f;
    crx = c; cry = s;                 // camRight = ( cos phi,  sin phi )
    cdx = s; cdy = -c;                // camDown  = ( sin phi, -cos phi )
}

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

// v0.18.3.263 (#83 follow-up): FIELD controlled-party formation.
//
// 0x01CFE74C (above) is the SAVEMAP party formation -- also used by battle/menu/
// junction TTS. But the FIELD engine decides which characters are the CONTROLLED
// party (the leader + followers that make up the "party train") using a DIFFERENT
// formation array at 0x01CFE990. Reverse-engineered from FF8_EN.exe: the SETPC /
// party-entity setup opcode handler at 0x0051EC30 configures a field entity as a
// party member iff its setpc byte matches one of the 3 slots of 0x01CFE990
// (`cmp byte ptr [edi + 0x1cfe990], al; ... cmp edi, 3; jl`). Same array is used
// by the sibling handlers at 0x0051ECF0 / 0x0051E8xx. In split-party scenes (the
// Deling assassination arc: Caraway's Mansion) the two arrays diverge -- one team
// follows you, the other stands in the room and is talkable. Reading the savemap
// array (0x01CFE74C) there identifies the wrong team as "your party."
//
// Char ids: 0=Squall 1=Zell 2=Irvine 3=Quistis 4=Rinoa 5=Selphie ... 0xFF=empty.
static bool IsInFieldControlledParty(uint8_t charId)
{
    if (charId == 0xFF) return false;
    __try {
        const uint8_t* formation = (const uint8_t*)0x01CFE990;
        for (int i = 0; i < 3; i++)
            if (formation[i] == charId) return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// v0.18.3.263 (#83 follow-up): character id of the FIELD party leader (the
// on-field controlled character) = first non-empty slot of 0x01CFE990. Returns
// 0xFF if the field party is unreadable / disbanded (e.g. a full-cutscene scene
// where nobody follows), letting the caller fall back to setpc==0.
static uint8_t GetFieldPartyLeaderChar()
{
    __try {
        const uint8_t* formation = (const uint8_t*)0x01CFE990;
        for (int i = 0; i < 3; i++)
            if (formation[i] != 0xFF) return formation[i];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0xFF;
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

// v0.18.3.284 (#86 follow-up): Read the setpc byte (0x255) for a runtime
// "Others" slot. 0-7 = an actual party-character entity; 0xFE = not one.
// Used by the MAP_EXIT live-position fallback to refuse trusting a runtime
// slot that's coincidentally occupied by a party member rather than the
// scripted exit entity itself (glclock1's 'irvine' MAP_EXIT shares jsmIndex=2
// with Rinoa's live party slot in that field -- reading her position there
// would silently reintroduce the false "Exit to wm05" with a fabricated-but-
// plausible-looking position instead of correctly staying filtered).
// Returns 0xFE (not-a-party-character) if the entity can't be read, matching
// the engine's own "no character" sentinel so callers don't need a separate
// error path.
static uint8_t GetEntitySetpc(int entityIdx)
{
    if (entityIdx < 0 || entityIdx >= MAX_ENTITIES) return 0xFE;
    if (!FF8Addresses::pFieldStateOthers) return 0xFE;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base) return *(base + ENTITY_STRIDE * entityIdx + 0x255);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0xFE;
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


// ============================================================================
// v0.18.3.305 (#109): per-fieldId camera-axes cache, surviving field loads.
//
// The .304 empirical correction works, but it is thrown away on every field
// load -- and the D-District Prison shaft reloads its field on every floor
// change. The .304 BAT log is unambiguous: gpbig1a loaded EIGHT times in four
// and a half minutes (plus three gpbig2a loads) while the calibration fired
// only TWICE. Correlating the two lists, every auto-drive that wedged in that
// log was running on uncalibrated ca-quantized axes, and the drives Aaron
// described as working ran inside a calibrated window:
//
//   12:27:14 load -> 12:27:22 calibrated -> 12:27:28/29/45/51 drives OK
//   12:28:10 load -> never recalibrated  -> 12:28:44 drive -> "Gave up" 12:28:53
//   12:29:07 load -> never recalibrated  -> 12:29:08 drive -> wedged on the stairs
//
// That is also the answer to "you have to approach the stairs from a
// particular direction": with the axes wrong, the drive presses the wrong
// arrow keys, so whether the player makes progress depends on how the axis
// error happens to line up with the approach. It is not the geometry being
// fussy, it is the steering being rotated.
//
// Relearning from scratch is not viable while walking a staircase, and it is
// unnecessary -- the camera for a given fieldId does not change between visits.
//
// 24 entries comfortably exceeds any single dungeon's field count; the table is
// ~600 bytes. On overflow the oldest entry is replaced round-robin, and a field
// that falls out simply relearns, which is exactly the pre-.305 behaviour.
//
// Lives here rather than in field_navigation.cpp because that file is at
// 81,792 of its 81,920-byte hard limit and DEVNOTES is explicit that it must be
// split BEFORE anything new lands there, not shaved to fit. This file is
// included ahead of both field_nav_fieldscripts.inl (which applies the cache on
// field load) and field_nav_observe.inl (which fills it).
// ============================================================================
static const int CAM_AXES_CACHE_MAX = 24;
struct CamAxesCacheEntry {
    uint16_t fieldId;
    float    rX, rY, dX, dY;
};
static CamAxesCacheEntry s_camAxesCache[CAM_AXES_CACHE_MAX] = {};
static int  s_camAxesCacheCount = 0;
static int  s_camAxesCacheNext  = 0;   // round-robin replacement cursor

// v0.18.3.305 (#109): corrections applied during THIS field load. Replaces the
// bare one-shot bool as the limiter.
//
// A cap rather than a lock, because the .304 BAT applied a correction that was
// 180 degrees WRONG: at 12:25:38 an arrow=DOWN consensus of (-0.872,0.490) --
// world angle 151 -- produced camRight=(0,-1), while at 12:27:22 an arrow=UP
// consensus of (-0.995,0.100) -- world angle 174 -- produced camRight=(0,+1).
// Opposite keys cannot both move the player the same way, so one sample was a
// wall slide, and the DOWN-derived answer was the wrong one. Under a hard
// one-shot lock that stood for the rest of the visit; caching it unchanged
// would have made it stand for the rest of the session. The cap lets the >= 45
// degree contradiction rule from .304 overwrite a bad correction (a 180-degree
// error diverges far past the threshold, so it self-repairs on the next clean
// sample) while still bounding oscillation.
static const int CAM_AXES_MAX_CORRECTIONS_PER_LOAD = 3;
static int  s_camAxesCorrectionCount = 0;

// Store (or refresh) this field's corrected axes.
static void CamAxesCacheStore(uint16_t fieldId, float rX, float rY, float dX, float dY)
{
    if (fieldId == 0xFFFF) return;
    for (int i = 0; i < s_camAxesCacheCount; i++) {
        if (s_camAxesCache[i].fieldId == fieldId) {
            s_camAxesCache[i].rX = rX; s_camAxesCache[i].rY = rY;
            s_camAxesCache[i].dX = dX; s_camAxesCache[i].dY = dY;
            return;
        }
    }
    int slot;
    if (s_camAxesCacheCount < CAM_AXES_CACHE_MAX) {
        slot = s_camAxesCacheCount++;
    } else {
        slot = s_camAxesCacheNext;
        s_camAxesCacheNext = (s_camAxesCacheNext + 1) % CAM_AXES_CACHE_MAX;
    }
    s_camAxesCache[slot].fieldId = fieldId;
    s_camAxesCache[slot].rX = rX; s_camAxesCache[slot].rY = rY;
    s_camAxesCache[slot].dX = dX; s_camAxesCache[slot].dY = dY;
}

// Look up cached axes for a field. Returns false when this field has never
// been calibrated, in which case the caller leaves the CA-derived axes alone.
static bool CamAxesCacheLookup(uint16_t fieldId, float& rX, float& rY, float& dX, float& dY)
{
    if (fieldId == 0xFFFF) return false;
    for (int i = 0; i < s_camAxesCacheCount; i++) {
        if (s_camAxesCache[i].fieldId == fieldId) {
            rX = s_camAxesCache[i].rX; rY = s_camAxesCache[i].rY;
            dX = s_camAxesCache[i].dX; dY = s_camAxesCache[i].dY;
            return true;
        }
    }
    return false;
}

// ============================================================================
// v0.18.3.305 (#110): CA snap-threshold probe. LOG-ONLY, no behaviour change.
//
// The offline survey of all 894 field .ca files (offline/CAMERA_ANGLES.csv,
// extracted straight from field.fs) established two things the v0.17.5
// quantizer was written without:
//
//   1. FF8 camera angles are NOT near-axis-aligned. Only 35% of fields sit
//      within 5 degrees of a world cardinal and 44% discard more than 20
//      degrees, so the 90-degree snap is making a large correction on most
//      of the game rather than tidying a rounding error.
//   2. The engine's snap threshold is NOT 45 degrees. gpbig1a's measured
//      response gives camRight ~= 90 (arrow RIGHT -> world 84 deg, DOWN ->
//      world -5.7 deg, two independent arrows agreeing), while its raw CA
//      angle is 38.32 and roundf picks 0. Against the fields known to work
//      (bghall_1 7.73, ggroom1 17.55, ggsta1 19.80, bghall_4 23.80 -> all
//      snap to 0 correctly; bgryo1_4 46.60, bg2f_1 65.38, bgroom_1 -62.49 ->
//      all snap up correctly) the true threshold is bracketed to
//      (23.8, 38.32].
//
// Moving the threshold to ~30 would flip 93 fields. EVERY field known to
// work today is outside that band and gpbig1a is inside it, which is the
// strongest argument the change is safe -- and 93 fields is still far too
// large a blast radius to bet on a single confirmed data point. So this
// build ships the measurement, not the fix.
//
// The probe pairs, per field, the raw CA angle against what the quantizer
// chose, what it rejected, and what the engine actually did once the
// empirical calibration lands. Every field Aaron walks that calibrates
// becomes a data point, not just the ones predicted in the survey.
// ============================================================================
static float s_caRawAngleDeg = 999.0f;   // raw CA camRight angle; 999 = no CA

// Both 90-degree candidates for a raw angle: the nearest (what the quantizer
// picks today) and the other neighbour (what a lower threshold would pick).
static void CamSnapCandidates(float rawDeg, float& nearestDeg, float& altDeg)
{
    const float q = 90.0f;
    float lo = floorf(rawDeg / q) * q;
    float hi = lo + q;
    if ((rawDeg - lo) <= (hi - rawDeg)) { nearestDeg = lo; altDeg = hi; }
    else                                { nearestDeg = hi; altDeg = lo; }
}

// Signed angular difference a-b folded into [-180,180].
static float CamAngleDelta(float a, float b)
{
    float d = a - b;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}
