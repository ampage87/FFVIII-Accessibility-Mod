// field_nav_announce.inl — Catalog announce (AnnounceCurrentTarget, AnnounceDirections, CycleEntity)
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// Key handlers
// ============================================================================

// Forward declarations.
static bool AnnounceCurrentTarget();
static bool AnnounceDirections();  // v0.07.76: directions on Backspace
static void CycleEntity(int delta);
static void RefreshCatalog();

// Compute and speak the current distance/direction from the player to the
// entity at s_selectedCatalogIdx. Always reads live positions so Backspace
// gives a fresh reading after the player has moved.
// Returns false if player position is not yet known.
static bool AnnounceCurrentTarget()
{
    if (s_nonPlayerCount == 0) {
        ScreenReader::Speak("No entities in this area.");
        return false;
    }

    // Skip player entity when selected.
    if (s_selectedCatalogIdx < s_catalogCount &&
        s_catalog[s_selectedCatalogIdx].entityIdx == s_playerEntityIdx) {
        CycleEntity(+1);
        return true;
    }

    // Compute stable 1-based rank among non-player catalog entries.
    int rank = 0;
    for (int c = 0; c <= s_selectedCatalogIdx && c < s_catalogCount; c++) {
        if (s_catalog[c].entityIdx != s_playerEntityIdx)
            rank++;
    }
    if (rank == 0) rank = 1;

    const EntityInfo& catEnt = s_catalog[s_selectedCatalogIdx];
    int ei = catEnt.entityIdx;

    // v05.59: Simplified type+number labels.
    // v05.72: Trigger lines use their name field ("Screen transition" or "Event").
    // v0.07.74: Use EntityTypeName for all types, including new JSM-classified ones.
    const char* typeLabel = "Entity";
    if (catEnt.type == ENT_SAVE_POINT)      typeLabel = "Save Point";
    else if (catEnt.type == ENT_DRAW_POINT)  typeLabel = "Draw Point";
    else if (catEnt.type == ENT_SHOP)        typeLabel = "Shop";
    else if (catEnt.type == ENT_CARD_GAME)   typeLabel = "Card Game";
    // v0.12.17: Restrict trigger line labels to -200..-299 range only.
    // JSM-injected entities (-300..-399) and gateway exits (-400+) fall through
    // to the generic type checks below.
    else if (catEnt.entityIdx <= -200 && catEnt.entityIdx > -300 && catEnt.type == ENT_EXIT)
        typeLabel = "Exit";   // screen transition (trigger line)
    else if (catEnt.entityIdx <= -200 && catEnt.entityIdx > -300)
        typeLabel = "Event";  // event trigger (trigger line)
    else if (catEnt.type == ENT_EXIT)
        typeLabel = "Exit";
    else if (catEnt.type == ENT_NPC || catEnt.type == ENT_BG_NPC) typeLabel = "NPC";
    else if (catEnt.type == ENT_OBJECT || catEnt.type == ENT_BG_OBJECT) typeLabel = "Object";

    // Count entities of same type up to this one to get type-specific number.
    // v05.72: Events and Exits are separate categories even though both can
    // have entityIdx <= -200. Match by typeLabel string for trigger entries.
    int typeNum = 0;
    for (int c = 0; c < s_catalogCount; c++) {
        const EntityInfo& ce = s_catalog[c];
        if (ce.entityIdx == s_playerEntityIdx) continue;
        bool sameType = false;
        // Exits: all ENT_EXIT entries (gateway + screen transition) are one group.
        // Events: trigger lines with ENT_OBJECT are their own group.
        // NPCs: regular entities (entityIdx >= 0).
        if (strcmp(typeLabel, "Exit") == 0 && ce.type == ENT_EXIT)
            sameType = true;
        else if (strcmp(typeLabel, "Event") == 0 && ce.entityIdx <= -200 && ce.entityIdx > -300 && ce.type != ENT_EXIT
                 && ce.type != ENT_SAVE_POINT && ce.type != ENT_DRAW_POINT
                 && ce.type != ENT_SHOP && ce.type != ENT_CARD_GAME)
            sameType = true;
        else if (strcmp(typeLabel, "Save Point") == 0 && ce.type == ENT_SAVE_POINT)
            sameType = true;
        else if (strcmp(typeLabel, "Draw Point") == 0 && ce.type == ENT_DRAW_POINT)
            sameType = true;
        else if (strcmp(typeLabel, "Shop") == 0 && ce.type == ENT_SHOP)
            sameType = true;
        else if (strcmp(typeLabel, "Card Game") == 0 && ce.type == ENT_CARD_GAME)
            sameType = true;
        else if (strcmp(typeLabel, "NPC") == 0 && ce.entityIdx >= 0 && ce.entityIdx != s_playerEntityIdx)
            sameType = true;
        else if (strcmp(typeLabel, "Object") == 0 && (ce.type == ENT_OBJECT || ce.type == ENT_BG_OBJECT))
            sameType = true;
        if (sameType) typeNum++;
        if (c == s_selectedCatalogIdx) break;
    }
    if (typeNum == 0) typeNum = 1;

    // Count total of same type.
    int typeTotal = 0;
    for (int c = 0; c < s_catalogCount; c++) {
        const EntityInfo& ce = s_catalog[c];
        if (ce.entityIdx == s_playerEntityIdx) continue;
        bool sameType = false;
        if (strcmp(typeLabel, "Exit") == 0 && ce.type == ENT_EXIT)
            sameType = true;
        else if (strcmp(typeLabel, "Event") == 0 && ce.entityIdx <= -200 && ce.entityIdx > -300 && ce.type != ENT_EXIT
                 && ce.type != ENT_SAVE_POINT && ce.type != ENT_DRAW_POINT
                 && ce.type != ENT_SHOP && ce.type != ENT_CARD_GAME)
            sameType = true;
        else if (strcmp(typeLabel, "Save Point") == 0 && ce.type == ENT_SAVE_POINT)
            sameType = true;
        else if (strcmp(typeLabel, "Draw Point") == 0 && ce.type == ENT_DRAW_POINT)
            sameType = true;
        else if (strcmp(typeLabel, "Shop") == 0 && ce.type == ENT_SHOP)
            sameType = true;
        else if (strcmp(typeLabel, "Card Game") == 0 && ce.type == ENT_CARD_GAME)
            sameType = true;
        else if (strcmp(typeLabel, "NPC") == 0 && ce.entityIdx >= 0 && ce.entityIdx != s_playerEntityIdx)
            sameType = true;
        else if (strcmp(typeLabel, "Object") == 0 && (ce.type == ENT_OBJECT || ce.type == ENT_BG_OBJECT))
            sameType = true;
        if (sameType) typeTotal++;
    }

    // v0.07.76: Catalog cycling speaks only the type label, no directions.
    // Directions are spoken separately via Backspace (AnnounceDirections).
    // v0.07.95: Include destination name for exits so players can build mental maps.
    // Exit names are already in catEnt.name (e.g. "Exit to B-Garden - Front Gate 2").
    char label[160];
    if (catEnt.type == ENT_EXIT && catEnt.name[0] != '\0') {
        snprintf(label, sizeof(label), "%s, %d of %d", catEnt.name, typeNum, typeTotal);
    } else if (catEnt.name[0] != '\0' && strcmp(catEnt.name, typeLabel) != 0) {
        // v0.12.17: Named entity — use the resolved name (e.g. "Igyous1" instead of "Object").
        snprintf(label, sizeof(label), "%s %d of %d", catEnt.name, typeNum, typeTotal);
    } else {
        snprintf(label, sizeof(label), "%s %d of %d", typeLabel, typeNum, typeTotal);
    }
    ScreenReader::Speak(label);

    Log::Field("FieldNavigation: [nav] cat%d ent%d rank=%d/%d '%s'",
               s_selectedCatalogIdx, ei, rank, s_nonPlayerCount, label);
    return true;
}

// v0.07.76: Speak compass directions for the currently selected entity.
// Called when Backspace is pressed. Reads live positions and applies
// camera-calibrated direction formatting.
static bool AnnounceDirections()
{
    if (s_nonPlayerCount == 0) {
        ScreenReader::Speak("No entities in this area.");
        return false;
    }
    if (s_selectedCatalogIdx >= s_catalogCount) return false;
    const EntityInfo& catEnt = s_catalog[s_selectedCatalogIdx];
    if (catEnt.entityIdx == s_playerEntityIdx) return false;
    int ei = catEnt.entityIdx;

    float px = 0, pz = 0;
    if (s_playerEntityIdx < 0 || !GetEntityPos(s_playerEntityIdx, px, pz)) {
        ScreenReader::Speak("Player position not yet known.");
        return true;
    }

    // Get target position.
    float tx = 0, tz = 0;
    bool targetLocated = false;
    if (ei <= -400) {
        // v0.07.94: INF gateway exit — position from deduplicated gateway center.
        int gwIdx = -(ei + 400);
        if (gwIdx >= 0 && gwIdx < s_dedupGatewayCount) {
            tx = s_dedupGateways[gwIdx].centerX;
            tz = s_dedupGateways[gwIdx].centerY;
            targetLocated = true;
        }
    } else if (ei <= -300) {
        int jsmIdx = -(ei + 300);
        if (jsmIdx >= 0 && jsmIdx < s_jsmEntityCount && s_jsmEntities[jsmIdx].hasPosition) {
            tx = (float)s_jsmEntities[jsmIdx].posX;
            tz = (float)s_jsmEntities[jsmIdx].posY;
            targetLocated = true;
        }
    } else if (ei <= -200) {
        int trigIdx = -(ei + 200);
        if (trigIdx >= 0 && trigIdx < s_capturedLineCount) {
            tx = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
            tz = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
            targetLocated = true;
        }
    } else if (ei >= 0 && ei < MAX_ENTITIES) {
        targetLocated = GetEntityPos(ei, tx, tz);
    }

    if (!targetLocated) {
        ScreenReader::Speak("Not yet located.");
        return true;
    }

    float dx   = tx - px;
    float dz   = tz - pz;
    float dist = sqrtf(dx*dx + dz*dz);

    if (dist > MAX_SANE_DIST) {
        ScreenReader::Speak("Position not yet reliable.");
        return true;
    }

    char dirBuf[128];
    FormatNavComponents(dx, dz, dirBuf, sizeof(dirBuf));

    char buf[256];
    if (dist < 250.0f) {
        snprintf(buf, sizeof(buf), "Right here.");
    } else {
        snprintf(buf, sizeof(buf), "%s.", dirBuf);
    }
    ScreenReader::Speak(buf);

    Log::Field("FieldNavigation: [dir] cat%d ent%d '%s' %s dist=%.0f "
               "player=(%.0f,%.0f) target=(%.0f,%.0f)",
               s_selectedCatalogIdx, ei, catEnt.name, dirBuf, dist, px, pz, tx, tz);
    return true;
}

// Step forward (+1) or backward (-1) through catalog[], skipping the player.
// Wraps around. Accepts entities with invalid/unknown centers — announce handles those.
static void CycleEntity(int delta)
{
    if (s_nonPlayerCount == 0) {
        ScreenReader::Speak("No entities in this area.");
        return;
    }

    int attempts = s_catalogCount;
    while (attempts-- > 0) {
        s_selectedCatalogIdx =
            ((s_selectedCatalogIdx + delta) % s_catalogCount + s_catalogCount) % s_catalogCount;
        const EntityInfo& entry = s_catalog[s_selectedCatalogIdx];
        if (entry.entityIdx == s_playerEntityIdx) continue;
        // v0.07.83: Allow runtime entities (>=0), trigger exits/events (<=-200), JSM-injected (<=-300), INF gateways (<=-400).
        if (entry.entityIdx < 0 && entry.entityIdx > -200) continue;
        if (entry.entityIdx >= MAX_ENTITIES) continue;
        break;
    }
    AnnounceCurrentTarget();
}
