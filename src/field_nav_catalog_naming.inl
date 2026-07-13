// field_nav_catalog_naming.inl — entity type-refinement + display naming.
//
// v0.18.3.235: Extracted from RefreshCatalog() in field_nav_catalog.inl to keep
// that file under the 80 KB source-size CI guard (it had reached 81 KB with only
// ~700 bytes of headroom, and the .235 fixes pushed it over).
//
// This is NOT a standalone function. It is a statement fragment #included at ONE
// point inside RefreshCatalog's per-entity loop — the same pattern as
// field_nav_catalog_dedupe.inl. It sees the loop locals (i, modelId, setpc,
// symName, talkable, ei_info) and the file-scope catalog state, and it DECLARES
// `entName`, which the including scope uses immediately afterwards to fill
// ei_info.name. Do not compile independently; do not include anywhere else.
//
// Behavior is identical to the pre-extraction source apart from the v0.18.3.235
// JSM-type guard documented below.

// v0.07.73: Look up JSM classification by SYM name.
// Overrides generic "NPC" with specific type (Save Point, Draw Point, etc.)
const char* entName = "NPC";
int symIdx = s_symOthersOffset + i;
if (symIdx >= 0 && symIdx < s_symNameCount) {
    const FieldArchive::JSMEntityInfo* jsm = FindJSMBySym(s_symNames[symIdx]);
    if (jsm) {
        EntityType jsmType = JSMTypeToCatalogType(jsm->type);
        // v0.18.3.235: a SYM-derived JSM type must NOT downgrade a VISIBLE
        // CHARACTER to a generic Object/Interaction.
        //
        // The SYM->slot map is unreliable on any field that instantiates only a
        // subset of its SYMs, so this lookup can be answering about a completely
        // different entity. On ggroom1 (G-Garden reception) a party member
        // standing in the scene (model 1) resolved to SYM 'zell' = a JSM
        // "Interactive Object", so a person was announced to the player as
        // "Object". A wrong name is survivable; a wrong TYPE is not.
        //
        // Specific, meaningful types (Save/Draw/Shop/Card) may still override --
        // they are what this lookup exists for, and the position-based dedupe in
        // field_nav_catalog_dedupe.inl cross-checks the save point independently.
        // Entities with no visible model (modelId < 0) are script objects rather
        // than characters and keep the original behavior.
        bool jsmSpecial = (jsmType == ENT_SAVE_POINT ||
                           jsmType == ENT_DRAW_POINT ||
                           jsmType == ENT_SHOP ||
                           jsmType == ENT_CARD_GAME);
        if (jsmType != ENT_UNKNOWN && (jsmSpecial || modelId < 0)) {
            ei_info.type = jsmType;
            entName = EntityTypeName(jsmType);
        }
    }
}
// v0.12.10: Comprehensive SYM-name entity type classification.
// Uses ENTITY_TYPE_TABLE from survey data, with pattern fallbacks.
if (ei_info.type == ENT_NPC || ei_info.type == ENT_OBJECT || ei_info.type == ENT_UNKNOWN) {
    if (symIdx >= 0 && symIdx < s_symNameCount) {
        const char* sym = s_symNames[symIdx];
        // First: check comprehensive type table from entity_classifications.h
        EntityClassificationType ecType = LookupEntityType(sym);
        if (ecType == EC_DRAW_POINT) {
            ei_info.type = ENT_DRAW_POINT;
            entName = "Draw Point";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Draw Point by type table", i, sym);
        } else if (ecType == EC_SAVE_POINT) {
            ei_info.type = ENT_SAVE_POINT;
            entName = "Save Point";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Save Point by type table", i, sym);
        } else if (ecType == EC_SHOP) {
            ei_info.type = ENT_SHOP;
            entName = "Shop";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Shop by type table", i, sym);
        } else if (ecType == EC_CARD_GAME) {
            ei_info.type = ENT_CARD_GAME;
            entName = "Card Player";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Card Game by type table", i, sym);
        } else {
            // Pattern-based fallback for names not in the table
            if ((sym[0] == 'd' || sym[0] == 'D') && (sym[1] == 'p' || sym[1] == 'P') &&
                sym[2] >= '0' && sym[2] <= '9') {
                ei_info.type = ENT_DRAW_POINT;
                entName = "Draw Point";
            } else if (_strnicmp(sym, "drpoint", 7) == 0 ||
                       _strnicmp(sym, "drawpoint", 9) == 0 ||
                       _strnicmp(sym, "draw_point", 10) == 0) {
                ei_info.type = ENT_DRAW_POINT;
                entName = "Draw Point";
            } else if (_strnicmp(sym, "save", 4) == 0 || _strnicmp(sym, "svpt", 4) == 0) {
                ei_info.type = ENT_SAVE_POINT;
                entName = "Save Point";
            }
        }
    }
}
// v0.12.09: Cross-entity draw point trigger detection.
// If this entity's JSM info shows it calls REQSW/REQEW to a draw point entity,
// classify it as Draw Point. This is deterministic — no proximity heuristics.
if (ei_info.type == ENT_NPC || ei_info.type == ENT_OBJECT || ei_info.type == ENT_UNKNOWN) {
    if (symIdx >= 0 && symIdx < s_symNameCount) {
        const FieldArchive::JSMEntityInfo* jsmDP = FindJSMBySym(s_symNames[symIdx]);
        if (jsmDP && jsmDP->drawPointTriggerOf >= 0) {
            ei_info.type = ENT_DRAW_POINT;
            entName = "Draw Point";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' reclassified as Draw Point "
                       "(triggers JSM draw point ent%d)",
                       i, s_symNames[symIdx], jsmDP->drawPointTriggerOf);
        }
    }
}
// v0.07.79: Model-based save point detection.
// Model 24 is the save point crystal across all FF8 fields. The visible save
// point entity often has a different SYM index than the save point script entity
// (e.g. bghall_1 ent6 vs JSM ent27), so SYM-based lookup misses it. Model ID is
// authoritative.
if (modelId == 24 && ei_info.type != ENT_SAVE_POINT) {
    ei_info.type = ENT_SAVE_POINT;
    entName = "Save Point";
}
// v0.18.3.227: Label interactable party members by proper name.
// v0.18.3.232: name party characters from setpc, NOT the SYM.
//
// A party character only reaches here if the party filter KEPT it — i.e. it is
// talkable — so announcing "Squall"/"Quistis" instead of a generic "NPC" is
// accurate and more useful. Deriving the name from setpc (the character ID)
// rather than the SYM is what makes it SAFE: the SYM list is shifted relative to
// the runtime slots, so a SYM-based label would happily announce the G-Garden
// train guard as "Rinoa". setpc is 0xFE on every genuine NPC, so no NPC, draw
// point or save point can be mislabeled as a party member.
if (ei_info.type == ENT_NPC) {
    const char* partyName = PartyCharacterNameById(setpc);
    if (partyName) {
        entName = partyName;
        Log::Field("FieldNavigation: [catalog] ent%d setpc=%d labeled as party "
                   "member '%s' (sym='%s' talkable=%d)",
                   i, (int)setpc, partyName, symName, talkable ? 1 : 0);
    }
}
