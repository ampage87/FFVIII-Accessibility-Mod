// field_nav_catalog.inl — Entity catalog building (RefreshCatalog)
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.

static void RefreshCatalog()
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return;
    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        // Re-detect player.
        for (int i = 0; i < (int)lim; i++) {
            uint8_t setpc = *(base + ENTITY_STRIDE * i + 0x255);
            if (setpc == 0) { s_playerEntityIdx = i; break; }
        }

        // v05.48: Diagnostic dump of ALL entities at scan time.
        // This reveals which entities exist and why some might be filtered.
        // v05.49: Also try multiple SYM offsets to find correct mapping.
        if (!s_entDiagDumped) {
            Log::Field("FieldNavigation: [ENTDIAG] === Entity dump: %d entities, symCount=%d, curOffset=%d ===",
                       (int)lim, s_symNameCount, s_symOthersOffset);
            // Log ALL SYM names for cross-reference.
            for (int s = 0; s < s_symNameCount; s++) {
                Log::Field("FieldNavigation: [ENTDIAG] SYM[%d]='%s'", s, s_symNames[s]);
            }
            for (int i = 0; i < (int)lim; i++) {
                uint8_t* block = base + ENTITY_STRIDE * i;
                int16_t  modelId      = *(int16_t*)(block + 0x218);
                uint16_t triId        = *(uint16_t*)(block + 0x1FA);
                uint8_t  setpc        = *(block + 0x255);
                uint8_t  talkonoff    = *(block + 0x24B);
                uint8_t  pushonoff    = *(block + 0x249);
                uint8_t  throughonoff = *(block + 0x24C);
                uint32_t execFlags    = *(uint32_t*)(block + 0x160);
                int32_t  fpX          = *(int32_t*)(block + 0x190);
                int32_t  fpZ          = *(int32_t*)(block + 0x198);
                int16_t  simX         = *(int16_t*)(block + 0x20);
                int16_t  simZ         = *(int16_t*)(block + 0x28);
                // Try offset 0, lines+bg, and current offset to compare.
                const char* sym0 = (i < s_symNameCount) ? s_symNames[i] : "(none)";
                int symLB = s_symOthersOffset + i;
                const char* symLBName = (symLB >= 0 && symLB < s_symNameCount) ? s_symNames[symLB] : "(none)";
                Log::Field("FieldNavigation: [ENTDIAG] ent%d model=%d tri=0x%04X setpc=%d "
                           "talk=%d push=%d thru=%d exec=0x%X fp=(%d,%d) sim=(%d,%d) "
                           "@0='%s' @%d='%s'",
                           i, (int)modelId, (unsigned)triId, (int)setpc,
                           (int)talkonoff, (int)pushonoff, (int)throughonoff,
                           execFlags, fpX, fpZ, (int)simX, (int)simZ,
                           sym0, s_symOthersOffset, symLBName);
            }
            s_entDiagDumped = true;
        }

        // v05.50: Background entity diagnostic dump.
        // Logs the entire backgrounds array with execution_flags, bgstate,
        // and candidate SYM indices to determine the correct mapping.
        if (!s_bgDiagDumped && FF8Addresses::HasFieldStateBackgrounds()) {
            __try {
                uint8_t bgCount = *FF8Addresses::pFieldStateBackgroundCount;
                uint8_t* bgBase = reinterpret_cast<uint8_t*>(
                    *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                Log::Field("FieldNavigation: [BGDIAG] === Background entity dump: %d bg entities ===",
                           (int)bgCount);
                Log::Field("FieldNavigation: [BGDIAG] bgBase=0x%08X  otherCount=%d  symCount=%d  JSM(D=%d L=%d B=%d O=%d)",
                           (uint32_t)(uintptr_t)bgBase, (int)lim, s_symNameCount,
                           s_jsmDoors, s_jsmLines, s_jsmBackgrounds, s_jsmOthers);
                if (bgBase && bgCount > 0) {
                    int bgLim = (bgCount < MAX_BG_ENTITIES) ? bgCount : MAX_BG_ENTITIES;
                    for (int b = 0; b < bgLim; b++) {
                        uint8_t* block = bgBase + BG_STRIDE * b;
                        // ff8_field_state_common fields:
                        uint32_t execFlags = *(uint32_t*)(block + 0x160);
                        uint16_t instrPos  = *(uint16_t*)(block + 0x176);
                        // ff8_field_state_background fields (after common at 0x188):
                        uint16_t bgstate   = *(uint16_t*)(block + 0x188);
                        // SYM mapping hypothesis: backgrounds are at SYM[L .. L+B-1]
                        // where L = number of line entities from JSM header.
                        // But we also try offset=0 mapping to see if it makes sense.
                        // For now, log the raw index and let the human figure it out.
                        const char* symDirect = (b < s_symNameCount) ? s_symNames[b] : "(none)";
                        // Hypothesis A: offset = otherCount (bg entities AFTER others in SYM).
                        int symAfterOthers = (int)lim + b;
                        const char* symAfterO = (symAfterOthers < s_symNameCount)
                                                ? s_symNames[symAfterOthers] : "(none)";
                        // Hypothesis B: offset = lines (SYM order = lines, bg, others).
                        int symAfterLines = s_jsmLines + b;
                        const char* symAfterL = (symAfterLines >= 0 && symAfterLines < s_symNameCount)
                                                ? s_symNames[symAfterLines] : "(none)";
                        Log::Field("FieldNavigation: [BGDIAG] bg%d exec=0x%X bgstate=0x%04X ipos=%u "
                                   "@0='%s' @oth%d='%s' @lin%d='%s'",
                                   b, execFlags, (unsigned)bgstate, (unsigned)instrPos,
                                   symDirect, symAfterOthers, symAfterO,
                                   symAfterLines, symAfterL);
                    }
                } else {
                    Log::Field("FieldNavigation: [BGDIAG] bgBase is NULL or bgCount==0");
                }
                Log::Field("FieldNavigation: [BGDIAG] === End background dump ===");
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("FieldNavigation: [BGDIAG] Exception reading backgrounds array");
            }
            s_bgDiagDumped = true;
        }

        // v05.59: Coordinate diagnostic dump — log ALL coord sources once per field.
        // This helps identify coordinate space mismatches between entities,
        // triggers, and gateways.
        if (!s_coordDiagDumped) {
            s_coordDiagDumped = true;  // only dump once per field
            Log::Field("FieldNavigation: [COORDDIAG] === Coordinate space diagnostic ===");
            Log::Field("FieldNavigation: [COORDDIAG] Field: %s  player=ent%d",
                       FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
                       s_playerEntityIdx);
            // Entity positions (all strategies)
            for (int i = 0; i < (int)lim; i++) {
                uint8_t* block = base + ENTITY_STRIDE * i;
                int16_t  modelId = *(int16_t*)(block + 0x218);
                uint16_t triId   = *(uint16_t*)(block + 0x1FA);
                int32_t  fpX     = *(int32_t*)(block + 0x190);
                int32_t  fpY     = *(int32_t*)(block + 0x194);
                int32_t  fpZ     = *(int32_t*)(block + 0x198);
                int16_t  simX    = *(int16_t*)(block + 0x20);
                int16_t  simY    = *(int16_t*)(block + 0x24);
                int16_t  simZ    = *(int16_t*)(block + 0x28);
                Log::Field("FieldNavigation: [COORDDIAG] ent%d model=%d tri=0x%04X "
                           "fp=(%d,%d,%d)/4096=(%d,%d,%d) sim=(%d,%d,%d)%s",
                           i, (int)modelId, (unsigned)triId,
                           fpX, fpY, fpZ, fpX/4096, fpY/4096, fpZ/4096,
                           (int)simX, (int)simY, (int)simZ,
                           (i == s_playerEntityIdx) ? " [PLAYER]" : "");
            }
            // SETLINE trigger positions — show all 3 raw axes
            for (int t = 0; t < s_capturedLineCount; t++) {
                Log::Field("FieldNavigation: [COORDDIAG] trigger%d ent=0x%08X "
                           "raw=(%d,%d,%d)->(%d,%d,%d) "
                           "centerX=%.0f centerY=%.0f centerZ=%.0f active=%d",
                           t, s_capturedLines[t].entityAddr,
                           (int)s_capturedLines[t].x1, (int)s_capturedLines[t].y1, (int)s_capturedLines[t].z1,
                           (int)s_capturedLines[t].x2, (int)s_capturedLines[t].y2, (int)s_capturedLines[t].z2,
                           (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f,
                           (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f,
                           (float)(s_capturedLines[t].z1 + s_capturedLines[t].z2) / 2.0f,
                           (int)s_capturedLines[t].active);
            }
            // v0.07.83: INF gateway logging removed (gateways replaced by JSM exits).
            Log::Field("FieldNavigation: [COORDDIAG] === End diagnostic ===");
        }

        // Build set of currently-qualifying entity indices.
        bool qualifies[MAX_ENTITIES] = {};
        EntityInfo fresh[MAX_ENTITIES] = {};
        for (int i = 0; i < (int)lim; i++) {
            uint8_t* block = base + ENTITY_STRIDE * i;
            int16_t  modelId      = *(int16_t*)(block + 0x218);
            uint16_t triId        = *(uint16_t*)(block + 0x1FA);
            uint8_t  setpc        = *(block + 0x255);
            uint8_t  talkonoff    = *(block + 0x24B);
            uint8_t  pushonoff    = *(block + 0x249);
            uint8_t  throughonoff = *(block + 0x24C);
            // v0.12.08 Fix D: Read position for placement validation.
            int32_t  fpX          = *(int32_t*)(block + 0x190);
            int32_t  fpY          = *(int32_t*)(block + 0x194);

            // v05.52: Classify entity type by interaction flags.
            // setpc==0 means this IS the player; setpc!=0 means it isn't.
            // Interaction flags determine what the player can do with it.
            // v0.07.97: Entities with visible generic character models (modelId >= 10)
            // always classify as NPC. Walking NPCs get pushonoff before talkonoff
            // (PUSHRADIUS fires in init, TALKRADIUS fires later), so push-only
            // at catalog-build time doesn't mean "object" for visible characters.
            // The pushonoff → Object path only applies to invisible (model<0) entities.
            EntityType etype = ENT_UNKNOWN;
            if (talkonoff > 0)                    etype = ENT_NPC;
            else if (pushonoff > 0 && modelId >= 10) etype = ENT_NPC;   // v0.07.97: walking NPC, talk not yet set
            else if (pushonoff > 0 && modelId >= 0) continue;   // v0.12.12: visible push-only entity (walking student) — not interactable, skip
            else if (pushonoff > 0)               etype = ENT_OBJECT;
            else if (throughonoff > 0)             etype = ENT_EXIT;
            else                                  etype = ENT_NPC;  // visible character, default to NPC
            bool hasModel = (modelId >= 0);
            bool hasInteraction = (talkonoff > 0 || pushonoff > 0 || throughonoff > 0);
            // v0.12.08 Fix D: Entities at (0,0) with triId=0 are inactive placeholders
            // even if they have a model assigned. Must have either a valid walkmesh
            // triangle OR a non-zero position to be considered placed.
            bool isPlaced = (triId > 0) || (hasModel && (fpX != 0 || fpY != 0));
            bool isSpecialJSM = false;
            if (!hasModel) {
                int symIdx2 = s_symOthersOffset + i;
                if (symIdx2 >= 0 && symIdx2 < s_symNameCount) {
                    const FieldArchive::JSMEntityInfo* jsm2 = FindJSMBySym(s_symNames[symIdx2]);
                    if (jsm2 && (jsm2->type == FieldArchive::JSM_ENT_SAVE_POINT ||
                                 jsm2->type == FieldArchive::JSM_ENT_DRAW_POINT ||
                                 jsm2->type == FieldArchive::JSM_ENT_SHOP ||
                                 jsm2->type == FieldArchive::JSM_ENT_CARD_GAME)) {
                        // Check if the entity has a valid runtime position
                        int32_t fpX2 = *(int32_t*)(block + 0x190);
                        int32_t fpY2 = *(int32_t*)(block + 0x194);
                        if (fpX2 != 0 || fpY2 != 0 || triId > 0) {
                            isSpecialJSM = true;
                            isPlaced = true;
                        }
                    }
                }
            }
            if (isPlaced && (hasModel || isSpecialJSM)) {
                qualifies[i] = true;
                EntityInfo ei_info = {};
                ei_info.entityIdx  = i;
                ei_info.modelId    = modelId;
                ei_info.triangleId = triId;
                ei_info.type       = etype;
                ei_info.gatewayIdx = -1;
                ei_info.name[0]    = '\0';
                // v0.07.73: Look up JSM classification by SYM name.
                // Overrides generic "NPC" with specific type (Save Point, Draw Point, etc.)
                const char* entName = "NPC";
                int symIdx = s_symOthersOffset + i;
                if (symIdx >= 0 && symIdx < s_symNameCount) {
                    const FieldArchive::JSMEntityInfo* jsm = FindJSMBySym(s_symNames[symIdx]);
                    if (jsm) {
                        EntityType jsmType = JSMTypeToCatalogType(jsm->type);
                        if (jsmType != ENT_UNKNOWN) {
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
                // If this entity's JSM info shows it calls REQSW/REQEW to a
                // draw point entity, classify it as Draw Point. This is
                // deterministic — no proximity heuristics needed.
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
                // Model 24 is the save point crystal across all FF8 fields.
                // The visible save point entity often has a different SYM index
                // than the save point script entity (e.g. bghall_1 ent6 vs JSM ent27),
                // so SYM-based lookup misses it. Model ID is authoritative.
                if (modelId == 24 && ei_info.type != ENT_SAVE_POINT) {
                    ei_info.type = ENT_SAVE_POINT;
                    entName = "Save Point";
                }
                strncpy(ei_info.name, entName, sizeof(ei_info.name) - 1);
                ei_info.name[sizeof(ei_info.name) - 1] = '\0';
                fresh[i] = ei_info;
            }
        }

        // v05.70: Screen filtering — exclude entities on the other side of
        // any active SETLINE trigger line from the player. This hides NPCs
        // that are on a different camera screen (e.g. front vs back of
        // bgroom_1 classroom). Only applies when we have trigger lines and
        // can read the player position.
        // v05.71: Track which entities were screen-filtered so we can identify
        // which trigger lines are true screen transitions (they separate the
        // player from at least one filtered entity).
        bool screenFiltered[MAX_ENTITIES] = {};
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float playerX, playerY;
            if (GetEntityPos(s_playerEntityIdx, playerX, playerY)) {
                int filtered = 0;
                for (int i = 0; i < (int)lim; i++) {
                    if (!qualifies[i]) continue;
                    if (i == s_playerEntityIdx) continue;  // never filter the player
                    float entX, entY;
                    if (GetEntityPos(i, entX, entY)) {
                        if (IsSeparatedByTriggerLine(playerX, playerY, entX, entY)) {
                            qualifies[i] = false;
                            screenFiltered[i] = true;
                            filtered++;
                        }
                    }
                }
                if (filtered > 0) {
                    Log::Field("FieldNavigation: [screen] filtered %d entities on other side of trigger lines (player at %.0f,%.0f)",
                               filtered, playerX, playerY);
                }
            }
        }

        // v0.12.08 Fix B: Walkmesh reachability filter — REMOVED in v0.12.09.
        // Was filtering entities on disconnected walkmesh islands, but FF8 fields
        // often have multiple elevation layers that create disconnected islands
        // within the same playable screen (e.g., bggate_6 guard on tri=87 vs
        // player on tri=22). This caused false positives, removing valid NPCs.
        // The existing trigger-line-based screen filtering handles off-screen
        // entities adequately without walkmesh connectivity checks.

        // Remember which entry the user had selected (entity or gateway).
        int prevSelectedEntity = -2;  // -2 = none, -1 = gateway, >=0 = entity
        int prevSelectedGateway = -1;
        if (s_selectedCatalogIdx >= 0 && s_selectedCatalogIdx < s_catalogCount) {
            prevSelectedEntity  = s_catalog[s_selectedCatalogIdx].entityIdx;
            prevSelectedGateway = s_catalog[s_selectedCatalogIdx].gatewayIdx;
        }

        // Rebuild: first, retain existing entity entries that still qualify (in order).
        // v05.51: Also retain background entities (entityIdx <= -100) — they'll be
        // re-evaluated below. Only retain "others" entities here.
        EntityInfo newCatalog[MAX_CATALOG] = {};
        int newCount = 0;
        for (int c = 0; c < s_catalogCount && newCount < MAX_CATALOG; c++) {
            int ei = s_catalog[c].entityIdx;
            if (ei >= 0 && ei < (int)lim && qualifies[ei]) {
                newCatalog[newCount++] = fresh[ei];
                qualifies[ei] = false;  // mark as placed
            }
            // Gateway and background entries are re-added below — skip them here.
        }
        // Then append any newly-qualifying entities at the end.
        int added = 0;
        for (int i = 0; i < (int)lim && newCount < MAX_CATALOG; i++) {
            if (qualifies[i]) {
                newCatalog[newCount++] = fresh[i];
                added++;
            }
        }

        // v0.07.83: JSM-based exit detection for screen boundary trigger lines.
        // Each JSM_ENT_LINE_SCREEN_BOUND captured line becomes an ENT_EXIT entry
        // with the destination resolved from the MAPJUMP destination field ID.
        // Replaces INF gateway exits entirely (INF data is vestigial PS1 data).
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float scrPlayerX = 0, scrPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, scrPlayerX, scrPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    // v0.12.24: Check if this field has Interactive Objects.
                    // On such fields (dormitories), SETLINE screen boundaries serve
                    // dual purposes (exit + interaction) and their CENTER position is
                    // the interaction zone, not the exit. INF gateways handle exits.
                    // Convert these SETLINEs to Interactions instead of Exits.
                    bool fieldHasInteractiveObjects = false;
                    for (int ji = 0; ji < s_jsmEntityCount; ji++) {
                        if (s_jsmEntities[ji].type == FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) {
                            fieldHasInteractiveObjects = true; break;
                        }
                    }
                    if (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                        fieldHasInteractiveObjects) {
                        continue;  // skip — will be added as Interaction below
                    }
                    if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;

                    // Reachability: trigger center must not be separated from player
                    // by any other active screen-boundary trigger line.
                    if (IsSeparatedByTriggerLine(scrPlayerX, scrPlayerY, tcx, tcy))
                        continue;

                    // Resolve destination name from MAPJUMP field ID.
                    char exitName[48];
                    int destId = s_capturedLines[t].destFieldId;
                    if (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) {
                        snprintf(exitName, sizeof(exitName), "Exit to %s", FIELD_DISPLAY_NAMES[destId]);
                    } else if (destId == -2) {
                        strncpy(exitName, "Exit to World Map", sizeof(exitName) - 1);
                    } else {
                        strncpy(exitName, "Exit", sizeof(exitName) - 1);
                    }
                    exitName[sizeof(exitName) - 1] = '\0';

                    EntityInfo trigExit = {};
                    trigExit.entityIdx  = -200 - t;
                    trigExit.modelId    = -1;
                    trigExit.triangleId = 0;
                    trigExit.type       = ENT_EXIT;
                    trigExit.gatewayIdx = -1;
                    strncpy(trigExit.name, exitName, sizeof(trigExit.name) - 1);
                    trigExit.name[sizeof(trigExit.name) - 1] = '\0';
                    newCatalog[newCount++] = trigExit;
                }
            }
        }

        // v05.72: Add reachable event triggers (non-screen-transition) as "Event".
        // These are active trigger lines on the player's screen that don't
        // separate screen-filtered entities. They fire script events when crossed.
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float evPlayerX = 0, evPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, evPlayerX, evPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    // Skip if already added as a screen transition.
                    bool alreadyAdded = false;
                    for (int c = 0; c < newCount; c++) {
                        if (newCatalog[c].entityIdx == (-200 - t)) { alreadyAdded = true; break; }
                    }
                    if (alreadyAdded) continue;
                    // v0.07.84: Skip lines already classified by JSM as camera pans or events.
                    // Only unclassified (UNKNOWN) lines should appear as "Event" entries.
                    // Camera pan lines are transparent navigation markers, not interactable.
                    // v0.12.12: Also skip UNKNOWN lines — these are unclassified trigger lines
                    // that don't fire any player-visible event. Showing them as "Event"
                    // is confusing (player arrives and nothing happens).
                    if (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_CAMERA_PAN ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_EVENT ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_UNKNOWN)
                        continue;
                    // Reachability check (same as screen transitions).
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    bool reachable = true;
                    for (int o = 0; o < s_capturedLineCount; o++) {
                        if (o == t) continue;
                        if (!s_capturedLines[o].active) continue;
                        float olx1 = (float)s_capturedLines[o].x1;
                        float oly1 = (float)s_capturedLines[o].y1;
                        float olx2 = (float)s_capturedLines[o].x2;
                        float oly2 = (float)s_capturedLines[o].y2;
                        float odx = olx2 - olx1;
                        float ody = oly2 - oly1;
                        float crossP = odx * (evPlayerY - oly1) - ody * (evPlayerX - olx1);
                        float crossT = odx * (tcy - oly1) - ody * (tcx - olx1);
                        if (crossP * crossT < -1.0f) { reachable = false; break; }
                    }
                    if (!reachable) continue;
                    // v0.07.78: Skip event triggers near save/draw points already in catalog.
                    // Check both JSM positions AND runtime entity positions (JSM SET3 can be
                    // inaccurate, but runtime entities always have correct coordinates).
                    bool overlapsSaveDrawPt = false;
                    // Check JSM scan positions.
                    for (int j = 0; j < s_jsmEntityCount && !overlapsSaveDrawPt; j++) {
                        const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
                        if (!je.hasPosition) continue;
                        if (je.type != FieldArchive::JSM_ENT_SAVE_POINT &&
                            je.type != FieldArchive::JSM_ENT_DRAW_POINT) continue;
                        float jdx = tcx - (float)je.posX;
                        float jdy = tcy - (float)je.posY;
                        if (sqrtf(jdx*jdx + jdy*jdy) < 1000.0f)
                            overlapsSaveDrawPt = true;
                    }
                    // Check runtime catalog entries already classified as save/draw.
                    for (int c2 = 0; c2 < newCount && !overlapsSaveDrawPt; c2++) {
                        if (newCatalog[c2].type != ENT_SAVE_POINT &&
                            newCatalog[c2].type != ENT_DRAW_POINT) continue;
                        int cei = newCatalog[c2].entityIdx;
                        if (cei < 0 || cei >= MAX_ENTITIES) continue;
                        float ex2 = 0, ey2 = 0;
                        if (GetEntityPos(cei, ex2, ey2)) {
                            float edx = tcx - ex2;
                            float edy = tcy - ey2;
                            if (sqrtf(edx*edx + edy*edy) < 1000.0f)
                                overlapsSaveDrawPt = true;
                        }
                    }
                    if (overlapsSaveDrawPt) continue;

                    EntityInfo evEntry = {};
                    evEntry.entityIdx  = -200 - t;
                    evEntry.modelId    = -1;
                    evEntry.triangleId = 0;
                    evEntry.type       = ENT_OBJECT;  // "Event" in announcement
                    evEntry.gatewayIdx = -1;
                    strncpy(evEntry.name, "Event", sizeof(evEntry.name) - 1);
                    evEntry.name[sizeof(evEntry.name) - 1] = '\0';
                    newCatalog[newCount++] = evEntry;
                }
            }
        }

        // v0.12.24: Add SETLINE-triggered interactive objects as "Interaction N".
        // Line entities classified as JSM_ENT_LINE_INTERACTIVE have dialog opcodes
        // (MES/ASK/AMES/AASK) or runtime ext dispatch — genuine player-facing
        // interactions (dormitory bed/desk/wardrobe, classroom desk/sign, etc.).
        // These use SETLINE center as navigation position.
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float intPlayerX = 0, intPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, intPlayerX, intPlayerY)) {
                int interactionNum = 0;
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    bool isInteractive = (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE);
                    if (!isInteractive &&
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        // v0.12.24: On fields with Interactive Objects, SETLINE screen
                        // boundaries are dual-purpose (exit + interaction). Their center
                        // position is the interaction zone. Add as Interaction.
                        bool fhio = false;
                        for (int ji = 0; ji < s_jsmEntityCount; ji++) {
                            if (s_jsmEntities[ji].type == FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) {
                                fhio = true; break;
                            }
                        }
                        if (fhio) isInteractive = true;
                    }
                    if (!isInteractive) continue;
                    // Don't check alreadyAdded — Interactions use sentinel -600-t,
                    // distinct from exit sentinel -200-t, so both can coexist.
                    // Reachability: must be on same side of screen-boundary trigger lines.
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    if (IsSeparatedByTriggerLine(intPlayerX, intPlayerY, tcx, tcy))
                        continue;
                    interactionNum++;
                    EntityInfo intEntry = {};
                    intEntry.entityIdx  = -200 - t;  // same sentinel as exits — position lookup works identically
                    intEntry.modelId    = -1;
                    intEntry.triangleId = 0;
                    intEntry.type       = ENT_INTERACTION;
                    intEntry.gatewayIdx = -1;
                    snprintf(intEntry.name, sizeof(intEntry.name), "Interaction %d", interactionNum);
                    newCatalog[newCount++] = intEntry;
                }
            }
        }

        // v05.52: Background entities removed from cycling catalog.
        // They have no walkmesh position and can't be auto-driven to.
        // Active bg entities are still logged in BGDIAG for diagnostics.
        // Interactive objects (terminals, bulletin boards) are script-triggered
        // by walk-on zones — the player discovers them by exploring, not by
        // navigating to an entity position.

        // v0.08.05: Late PSHM resolution — retry direct struct reads for entities
        // whose positions weren't available at field init time. By RefreshCatalog time,
        // the field has been running and non-init scripts may have executed SET3.
        if (FF8Addresses::pFieldStateOthers) {
            __try {
                uint8_t* othBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                if (othBase) {
                    int othStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
                    for (int jr = 0; jr < s_jsmEntityCount; jr++) {
                        FieldArchive::JSMEntityInfo& jer = s_jsmEntities[jr];
                        if (!jer.hasPshmCoords || jer.hasPosition) continue;
                        // v0.08.15: Handle both Others (cat 3) and Background (cat 2) entities.
                        uint8_t* blk4 = nullptr;
                        int oir = 0;
                        if (jer.jsmCategory == 3) {
                            oir = jer.jsmIndex - othStart;
                            if (oir < 0) continue;
                            blk4 = othBase + ENTITY_STRIDE * oir;
                        } else if (jer.jsmCategory == 2) {
                            uint8_t* bgBase4 = nullptr;
                            if (FF8Addresses::HasFieldStateBackgrounds()) {
                                bgBase4 = reinterpret_cast<uint8_t*>(
                                    *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                            }
                            if (!bgBase4) continue;
                            int bgStart4 = s_jsmDoors + s_jsmLines;
                            oir = jer.jsmIndex - bgStart4;
                            if (oir < 0) continue;
                            blk4 = bgBase4 + BG_STRIDE * oir;
                        } else {
                            continue;
                        }
                        int32_t fX = *(int32_t*)(blk4 + 0x190);
                        int32_t fY = *(int32_t*)(blk4 + 0x194);
                        uint16_t tr = 0;
                        if (jer.jsmCategory == 3) {
                            tr = *(uint16_t*)(blk4 + 0x1FA);
                        }
                        if (fX != 0 || fY != 0) {
                            jer.posX = (int16_t)(fX / 4096);
                            jer.posY = (int16_t)(fY / 4096);
                            jer.posTriangle = tr;
                            jer.hasPosition = true;
                            Log::Field("FieldNavigation: [LATE-RESOLVE] ent%d '%s' type=%s "
                                       "cat=%d idx=%d pos=(%d,%d) tri=%u fp=(%d,%d)",
                                       jer.jsmIndex, jer.symName,
                                       FieldArchive::JSMEntityTypeName(jer.type),
                                       jer.jsmCategory, oir,
                                       (int)jer.posX, (int)jer.posY,
                                       (unsigned)tr, fX, fY);
                        }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        // v0.08.16: SET3-LATE-MATCH — re-check accumulated SET3 captures against PSHM entities.
        // The extended capture window (3s) catches per-frame SET3 calls from entities like
        // dic (bghall_1 Directory) whose SET3 fires in method 1+, not during init.
        // This overwrites shift-pattern approximations with engine-resolved positions.
        if (s_set3CaptureCount > 0 && FF8Addresses::pFieldStateOthers) {
            __try {
                uint8_t* set3OthBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                uint8_t* set3BgBase = nullptr;
                if (FF8Addresses::HasFieldStateBackgrounds()) {
                    set3BgBase = reinterpret_cast<uint8_t*>(
                        *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                }
                if (set3OthBase) {
                    int set3OthStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
                    int set3BgStart = s_jsmDoors + s_jsmLines;
                    int lateMatched = 0;
                    for (int jl = 0; jl < s_jsmEntityCount; jl++) {
                        FieldArchive::JSMEntityInfo& jel = s_jsmEntities[jl];
                        if (!jel.hasPshmCoords) continue;
                        // Compute expected entity address.
                        uint32_t lateAddr = 0;
                        if (jel.jsmCategory == 3) {
                            int oi = jel.jsmIndex - set3OthStart;
                            if (oi < 0) continue;
                            lateAddr = (uint32_t)(uintptr_t)(set3OthBase + ENTITY_STRIDE * oi);
                        } else if (jel.jsmCategory == 2 && set3BgBase) {
                            int bi = jel.jsmIndex - set3BgStart;
                            if (bi < 0) continue;
                            lateAddr = (uint32_t)(uintptr_t)(set3BgBase + BG_STRIDE * bi);
                        } else {
                            continue;
                        }
                        // Search SET3 captures for this entity address.
                        for (int c = 0; c < s_set3CaptureCount; c++) {
                            if (s_set3Captures[c].entityAddr == lateAddr) {
                                int16_t newX = s_set3Captures[c].posX;
                                int16_t newY = s_set3Captures[c].posY;
                                if (newX == 0 && newY == 0) break;  // no useful position
                                // Only log + update if position actually changed.
                                if (!jel.hasPosition || jel.posX != newX || jel.posY != newY) {
                                    Log::Field("FieldNavigation: [SET3-LATE-MATCH] ent%d '%s' type=%s "
                                               "cat=%d old=(%d,%d) new=(%d,%d) tri=%u addr=0x%08X",
                                               jel.jsmIndex, jel.symName,
                                               FieldArchive::JSMEntityTypeName(jel.type),
                                               jel.jsmCategory,
                                               jel.hasPosition ? (int)jel.posX : 0,
                                               jel.hasPosition ? (int)jel.posY : 0,
                                               (int)newX, (int)newY,
                                               (unsigned)s_set3Captures[c].triId, lateAddr);
                                    jel.posX = newX;
                                    jel.posY = newY;
                                    jel.posTriangle = s_set3Captures[c].triId;
                                    jel.hasPosition = true;
                                    lateMatched++;
                                }
                                break;
                            }
                        }
                    }
                    if (lateMatched > 0) {
                        Log::Field("FieldNavigation: [SET3-LATE-MATCH] %d entities updated from %d captures",
                                   lateMatched, s_set3CaptureCount);
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        // v0.12.17: Varblock PSHM position resolution — DISABLED.
        // Varblock[addr] returns 0 for entities beyond the active window
        // (their scripts never run to populate the PSHM variables).
        // This CORRUPTED positions by replacing shift-pattern approximations
        // with zeros. The STRUCT-POS block below handles the cases where
        // runtime struct data is available. Keep shift-pattern for the rest.
        if (false)
        {
            static const uint32_t VARBLOCK_BASE = 0x1CFE9B8;
            int vbResolved = 0;
            for (int jv = 0; jv < s_jsmEntityCount; jv++) {
                FieldArchive::JSMEntityInfo& jev = s_jsmEntities[jv];
                if (!jev.hasPshmCoords) continue;
                // Read varblock at each PSHM address.
                int16_t vbX = 0, vbY = 0, vbZ = 0;
                bool vbOk = true;
                __try {
                    // PSHM addresses are byte offsets into the varblock.
                    // addr=0 means "literal" (not a PSHM ref) — the JSM scanner
                    // stores the literal value in posX/posY/posZ already.
                    if (jev.pshmAddrX != 0)
                        vbX = *(int16_t*)(VARBLOCK_BASE + (uint16_t)jev.pshmAddrX);
                    else
                        vbX = jev.posX;  // literal value from shift-pattern
                    if (jev.pshmAddrY != 0)
                        vbY = *(int16_t*)(VARBLOCK_BASE + (uint16_t)jev.pshmAddrY);
                    else
                        vbY = jev.posY;
                    if (jev.pshmAddrZ != 0)
                        vbZ = *(int16_t*)(VARBLOCK_BASE + (uint16_t)jev.pshmAddrZ);
                    else
                        vbZ = jev.posZ;
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    vbOk = false;
                }
                if (vbOk && (vbX != 0 || vbY != 0)) {
                    // SET3 params map to: entity.X = param_X, entity.Y = param_Y.
                    // For 2D navigation: posX = SET3.X, posY = SET3.Y.
                    int16_t oldX = jev.posX;
                    int16_t oldY = jev.posY;
                    jev.posX = vbX;
                    jev.posY = vbY;
                    jev.posZ = vbZ;
                    jev.hasPosition = true;
                    vbResolved++;
                    Log::Field("FieldNavigation: [VARBLOCK-POS] ent%d '%s' type=%s "
                               "pshmAddr=(%d,%d,%d) varblock=(%d,%d,%d) "
                               "old=(%d,%d) new=(%d,%d)",
                               jev.jsmIndex, jev.symName,
                               FieldArchive::JSMEntityTypeName(jev.type),
                               (int)jev.pshmAddrX, (int)jev.pshmAddrY, (int)jev.pshmAddrZ,
                               (int)vbX, (int)vbY, (int)vbZ,
                               (int)oldX, (int)oldY,
                               (int)jev.posX, (int)jev.posY);
                }
            }
            if (vbResolved > 0)
                Log::Field("FieldNavigation: [VARBLOCK-POS] %d PSHM entity positions resolved from varblock",
                           vbResolved);
        }

        // v0.12.17: Direct entity struct position read for PSHM entities.
        // The shift-pattern approximation discards the PSHM X value, giving
        // ~200-unit error. But the engine allocates structs for ALL Others
        // entities (not just the active window). If the entity's init script
        // ran SET3 during field_scripts_init, the struct has the resolved
        // position even though the entity isn't in the active window.
        // LATE-RESOLVE skips entities with hasPosition=true (from shift-pattern),
        // so we check here specifically for hasPshmCoords entities.
        if (FF8Addresses::pFieldStateOthers) {
            __try {
                uint8_t* othBase2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                uint8_t* bgBase3 = nullptr;
                if (FF8Addresses::HasFieldStateBackgrounds()) {
                    bgBase3 = reinterpret_cast<uint8_t*>(
                        *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                }
                if (othBase2) {
                    int structOthStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
                    int structBgStart = s_jsmDoors + s_jsmLines;
                    int structFixed = 0;
                    for (int js = 0; js < s_jsmEntityCount; js++) {
                        FieldArchive::JSMEntityInfo& jes = s_jsmEntities[js];
                        if (!jes.hasPshmCoords) continue;
                        // Try reading the entity struct position directly.
                        uint8_t* blk5 = nullptr;
                        int sIdx = 0;
                        if (jes.jsmCategory == 3) {
                            sIdx = jes.jsmIndex - structOthStart;
                            if (sIdx < 0 || sIdx >= 31) continue;  // safety: max 31 Others
                            blk5 = othBase2 + ENTITY_STRIDE * sIdx;
                        } else if (jes.jsmCategory == 2 && bgBase3) {
                            sIdx = jes.jsmIndex - structBgStart;
                            if (sIdx < 0 || sIdx >= MAX_BG_ENTITIES) continue;
                            blk5 = bgBase3 + BG_STRIDE * sIdx;
                        } else {
                            continue;
                        }
                        int32_t fX5 = *(int32_t*)(blk5 + 0x190);
                        int32_t fY5 = *(int32_t*)(blk5 + 0x194);
                        if (fX5 == 0 && fY5 == 0) continue;  // no position set
                        int16_t sX = (int16_t)(fX5 / 4096);
                        int16_t sY = (int16_t)(fY5 / 4096);
                        // Only update if the struct position differs from current.
                        if (sX != jes.posX || sY != jes.posY) {
                            Log::Field("FieldNavigation: [STRUCT-POS] ent%d '%s' type=%s "
                                       "cat=%d idx=%d struct=(%d,%d) old=(%d,%d) fp=(%d,%d)",
                                       jes.jsmIndex, jes.symName,
                                       FieldArchive::JSMEntityTypeName(jes.type),
                                       jes.jsmCategory, sIdx,
                                       (int)sX, (int)sY,
                                       (int)jes.posX, (int)jes.posY,
                                       fX5, fY5);
                            jes.posX = sX;
                            jes.posY = sY;
                            jes.hasPosition = true;
                            structFixed++;
                        }
                    }
                    if (structFixed > 0)
                        Log::Field("FieldNavigation: [STRUCT-POS] %d PSHM positions updated from entity structs",
                                   structFixed);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        // v0.07.74: Inject JSM-classified special entities not already in the catalog.
        // These are entities beyond the runtime state array (SYM index >= entCount)
        // or entities in the array that weren't caught by approach A above.
        // Uses SET3 positions extracted by the JSM scanner when available.
        for (int j = 0; j < s_jsmEntityCount && newCount < MAX_CATALOG; j++) {
            const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
            if (je.type != FieldArchive::JSM_ENT_SAVE_POINT &&
                je.type != FieldArchive::JSM_ENT_DRAW_POINT &&
                je.type != FieldArchive::JSM_ENT_SHOP &&
                je.type != FieldArchive::JSM_ENT_CARD_GAME &&
                je.type != FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) continue;
                // v0.12.17: JSM_ENT_INTERACTIVE_OBJECT RE-ENABLED.
                // v0.12.12 removed it ("typically background visual effects"),
                // but that also blocked the Directory (dic) on bghall_1.
                // The paired-entity detection + shift-pattern position provides
                // adequate positions for genuine interactive objects.
            // v0.07.80: Check if this type exists as a runtime entity ANYWHERE on
            // the field, even if screen-filtered. JSM SET3 positions are unreliable
            // (bghall_1 saves at 135,588 instead of -700,-8593). Runtime entities
            // always have correct positions. If a runtime entity of matching type
            // exists, prefer it — don't inject JSM with wrong coordinates.
            EntityType jt = JSMTypeToCatalogType(je.type);
            bool runtimeEntityExists = false;
            for (int i2 = 0; i2 < (int)lim; i2++) {
                if (fresh[i2].entityIdx >= 0 && fresh[i2].type == jt) {
                    runtimeEntityExists = true; break;
                }
            }
            if (runtimeEntityExists) continue;
            // Also check what's already in the catalog (from other sources).
            bool alreadyInCatalog = false;
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].type == jt) { alreadyInCatalog = true; break; }
            }
            if (alreadyInCatalog) continue;

            // v0.12.09: Draw point consolidation.
            // The JSM draw point position often points to an invisible script entity,
            // not the actual interaction trigger. Check if any interactive catalog
            // entity (NPC/draw point/save point) exists near the JSM position.
            // If not, the real interaction entity is elsewhere — find the closest
            // non-party NPC in the catalog and reclassify it as Draw Point.
            if (jt == ENT_DRAW_POINT && je.hasPosition && s_walkmesh.valid && s_playerEntityIdx >= 0) {
                // Check if any catalog entity with interaction is near the JSM draw point.
                bool interactiveNearDP = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].entityIdx < 0) continue;
                    if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                    float nx = 0, ny = 0;
                    if (GetEntityPos(newCatalog[c].entityIdx, nx, ny)) {
                        float ddx = (float)je.posX - nx;
                        float ddy = (float)je.posY - ny;
                        float dd = sqrtf(ddx*ddx + ddy*ddy);
                        if (dd < 300.0f) { interactiveNearDP = true; break; }
                    }
                }
                if (!interactiveNearDP) {
                    // No interactive entity near the JSM draw point position.
                    // Find closest non-party NPC on the player's walkmesh and reclassify.
                    uint16_t pTriDP = 0xFFFF;
                    __try {
                        uint8_t* baseDP = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                        if (baseDP) pTriDP = *(uint16_t*)(baseDP + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    int bestCatIdx = -1;
                    float bestDist = 1e30f;
                    for (int c = 0; c < newCount; c++) {
                        if (newCatalog[c].entityIdx < 0) continue;
                        if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                        if (newCatalog[c].type != ENT_NPC) continue;
                        // Skip party character models (0-9)
                        if (newCatalog[c].modelId >= 0 && newCatalog[c].modelId < 10) continue;
                        float nx = 0, ny = 0;
                        if (GetEntityPos(newCatalog[c].entityIdx, nx, ny)) {
                            float ddx = (float)je.posX - nx;
                            float ddy = (float)je.posY - ny;
                            float dd = sqrtf(ddx*ddx + ddy*ddy);
                            if (dd < bestDist) { bestDist = dd; bestCatIdx = c; }
                        }
                    }
                    if (bestCatIdx >= 0) {
                        newCatalog[bestCatIdx].type = ENT_DRAW_POINT;
                        strncpy(newCatalog[bestCatIdx].name, "Draw Point",
                                sizeof(newCatalog[bestCatIdx].name) - 1);
                        Log::Field("FieldNavigation: [catalog] Draw point consolidation: "
                                   "reclassified ent%d as Draw Point (JSM dp '%s' at %d,%d "
                                   "has no nearby interactive entity, NPC dist=%.0f)",
                                   newCatalog[bestCatIdx].entityIdx, je.symName,
                                   je.posX, je.posY, bestDist);
                        continue; // don't inject the JSM draw point
                    }
                }
            }

            // Not in catalog yet.
            // v0.12.12: No-position draw point fallback.
            // When a JSM draw point exists on the field but has no position
            // (entity beyond runtime window, e.g. Fire Cavern 'drpoint'),
            // reclassify the nearest non-player entity as Draw Point.
            // This is less precise than position-based consolidation but
            // ensures draw points get correct labels in the catalog.
            if (jt == ENT_DRAW_POINT && !je.hasPosition) {
                int bestCatIdx2 = -1;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].entityIdx < 0) continue;
                    if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                    if (newCatalog[c].type != ENT_NPC) continue;
                    // v0.12.12: Don't filter by model<10 here.
                    // On Fire Cavern, the draw point entity uses model 9
                    // (party character range). Without position data,
                    // we accept any NPC as a candidate.
                    float nx2 = 0, ny2 = 0;
                    if (GetEntityPos(newCatalog[c].entityIdx, nx2, ny2)) {
                        // Without JSM position, we can't measure proximity.
                        // Pick the first (and likely only) non-player NPC.
                        bestCatIdx2 = c;
                        break;
                    }
                }
                if (bestCatIdx2 >= 0) {
                    newCatalog[bestCatIdx2].type = ENT_DRAW_POINT;
                    strncpy(newCatalog[bestCatIdx2].name, "Draw Point",
                            sizeof(newCatalog[bestCatIdx2].name) - 1);
                    Log::Field("FieldNavigation: [catalog] Draw point no-position fallback: "
                               "reclassified ent%d as Draw Point (JSM dp '%s' has no position)",
                               newCatalog[bestCatIdx2].entityIdx, je.symName);
                    continue;
                }
            }

            // Try to inject using JSM SET3 position.
            if (!je.hasPosition) continue;
            // Validate position is in plausible range.
            if (je.posX == 0 && je.posY == 0 && je.posZ == 0) continue;
            EntityInfo jsmEntry = {};
            jsmEntry.entityIdx  = -300 - j;  // unique sentinel for JSM-injected entities
            jsmEntry.modelId    = -1;
            jsmEntry.triangleId = je.posTriangle;
            jsmEntry.type       = jt;
            jsmEntry.gatewayIdx = -1;
            // v0.12.17: Resolve friendly display name for interactive objects.
            // For save/draw points, use the type name. For interactive objects,
            // resolve the SYM name to a user-friendly name (e.g. "dic" -> "Directory").
            char friendlyBuf[48] = {};
            const char* jtName = EntityTypeName(jt);
            if (je.symName[0] != '\0') {
                ResolveFriendlyName(je.symName, friendlyBuf, sizeof(friendlyBuf));
                if (friendlyBuf[0] != '\0' && jt != ENT_SAVE_POINT && jt != ENT_DRAW_POINT
                    && jt != ENT_SHOP && jt != ENT_CARD_GAME)
                    jtName = friendlyBuf;  // use friendly name for interactive objects
            }
            strncpy(jsmEntry.name, jtName, sizeof(jsmEntry.name) - 1);
            jsmEntry.name[sizeof(jsmEntry.name) - 1] = '\0';
            newCatalog[newCount++] = jsmEntry;
            Log::Field("FieldNavigation: [refresh] JSM-injected %s at (%d,%d) sym='%s'",
                       jtName, (int)je.posX, (int)je.posY, je.symName);
        }

        // v0.07.83: Entity-based exits from JSM_ENT_MAP_EXIT "Other" entities.
        // These are interactive objects (elevators, doors, trigger zones) whose
        // scripts contain MAPJUMP. They have destination field IDs in param.
        // Position from SET3 extraction or runtime entity, or captured SETLINE.
        for (int j = 0; j < s_jsmEntityCount && newCount < MAX_CATALOG; j++) {
            const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
            if (je.type != FieldArchive::JSM_ENT_MAP_EXIT) continue;
            // Skip if already in catalog as a runtime entity or trigger line exit.
            bool alreadyInCatalog = false;
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].type == ENT_EXIT) { 
                    // Check if this JSM entity's destination matches an existing exit.
                    // Also check if the SYM name matches a runtime entity already added.
                    if (newCatalog[c].entityIdx <= -200) {
                        // Trigger line exit — check destination.
                        int ti = -(newCatalog[c].entityIdx + 200);
                        if (ti >= 0 && ti < s_capturedLineCount &&
                            s_capturedLines[ti].destFieldId == je.param) {
                            alreadyInCatalog = true; break;
                        }
                    }
                }
            }
            if (alreadyInCatalog) continue;
            // Resolve destination name.
            char exitName[48];
            int destId = je.param;
            if (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) {
                snprintf(exitName, sizeof(exitName), "Exit to %s", FIELD_DISPLAY_NAMES[destId]);
            } else if (destId == -2) {
                strncpy(exitName, "Exit to World Map", sizeof(exitName) - 1);
            } else {
                strncpy(exitName, "Exit", sizeof(exitName) - 1);
            }
            exitName[sizeof(exitName) - 1] = '\0';
            // Find position: try matching captured SETLINE by entity address range,
            // or use SET3 position from JSM scan.
            float exitX = 0, exitY = 0;
            bool hasPos = false;
            if (je.hasPosition) {
                exitX = (float)je.posX;
                exitY = (float)je.posY;
                hasPos = true;
            }
            // v0.07.84: If no SET3 position, try matching SYM name to a captured
            // SETLINE entity. "Other" entities that call SETLINE (e.g. saveline0
            // elevator trigger) have their SETLINE coordinates captured at runtime
            // with accurate positions, even when SET3 extraction fails.
            if (!hasPos && je.symName[0] != '\0' && base) {
                uint32_t baseAddr = (uint32_t)(uintptr_t)base;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    uint32_t lineEntAddr = s_capturedLines[t].entityAddr;
                    // Check if this captured line belongs to an Others entity.
                    if (lineEntAddr >= baseAddr &&
                        lineEntAddr < baseAddr + ENTITY_STRIDE * lim) {
                        int entIdx = (int)((lineEntAddr - baseAddr) / ENTITY_STRIDE);
                        int symIdx = s_symOthersOffset + entIdx;
                        if (symIdx >= 0 && symIdx < s_symNameCount &&
                            _stricmp(s_symNames[symIdx], je.symName) == 0) {
                            exitX = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                            exitY = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                            hasPos = true;
                            // v0.07.87: Write position back to JSM entity so
                            // AnnounceDirections can read it for compass.
                            s_jsmEntities[j].posX = (int16_t)exitX;
                            s_jsmEntities[j].posY = (int16_t)exitY;
                            s_jsmEntities[j].hasPosition = true;
                            Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' position from SETLINE center (%.0f,%.0f)",
                                       je.symName, exitX, exitY);
                            break;
                        }
                    }
                }
            }
            // Screen filter: skip exits on the other side of trigger lines.
            if (hasPos && s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
                float plX = 0, plY = 0;
                if (GetEntityPos(s_playerEntityIdx, plX, plY)) {
                    if (IsSeparatedByTriggerLine(plX, plY, exitX, exitY))
                        continue;
                }
            }
            EntityInfo mapExit = {};
            // v0.07.95: Suppress JSM exits with runtime-resolved destinations
            // when INF gateways exist on this field. The INF gateway system
            // handles those same physical exits with proper static destinations.
            // v0.08.01: Bit31 marker check. PSHM_W-sourced MAPJUMP destinations
            // have bit31 set (negative values). Also check param > 982 for
            // the 0x00FFxx markers that survived before the bit31 change.
            if (s_gatewayCount > 0 && (je.param < 0 || je.param > 982))
                continue;

            // v0.12.08 Fix A: Filter JSM exit destinations against INF gateways.
            // When a field has INF gateways, they define the known valid exits.
            // If a JSM MAP_EXIT destination doesn't match any INF gateway destination,
            // it's likely a stale runtime variable value (PSHM_W) and should be skipped.
            // (e.g., "Exit to Dollet Comms Tower" appearing in B-Garden hallways)
            if (s_gatewayCount > 0) {
                bool destMatchesGateway = false;
                for (int gi = 0; gi < s_gatewayCount; gi++) {
                    if (s_gateways[gi].destFieldId == (uint16_t)je.param) {
                        destMatchesGateway = true;
                        break;
                    }
                }
                if (!destMatchesGateway) {
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d filtered "
                               "(no matching INF gateway on this field)",
                               je.symName, je.param);
                    continue;
                }
            }

            mapExit.entityIdx  = -300 - j;  // JSM-injected sentinel
            mapExit.modelId    = -1;
            mapExit.triangleId = je.posTriangle;
            mapExit.type       = ENT_EXIT;
            mapExit.gatewayIdx = -1;
            strncpy(mapExit.name, exitName, sizeof(mapExit.name) - 1);
            mapExit.name[sizeof(mapExit.name) - 1] = '\0';
            newCatalog[newCount++] = mapExit;
        }

        // v0.07.94: Add deduplicated INF gateway exits to catalog.
        // Group gateways by destFieldId, average their centers, create one
        // catalog entry per unique destination. Skip gateways whose center is
        // on the other side of a screen-boundary trigger line from the player.
        // Also skip gateways whose destination already has a JSM-detected exit.
        s_dedupGatewayCount = 0;
        memset(s_dedupGateways, 0, sizeof(s_dedupGateways));
        for (int g = 0; g < s_gatewayCount && s_dedupGatewayCount < MAX_DEDUP_GATEWAYS; g++) {
            uint16_t destId = s_gateways[g].destFieldId;
            // Find existing dedup group for this destination.
            int groupIdx = -1;
            for (int d = 0; d < s_dedupGatewayCount; d++) {
                if (s_dedupGateways[d].destFieldId == destId) { groupIdx = d; break; }
            }
            if (groupIdx < 0) {
                // New group.
                groupIdx = s_dedupGatewayCount++;
                s_dedupGateways[groupIdx].destFieldId = destId;
                s_dedupGateways[groupIdx].centerX = 0;
                s_dedupGateways[groupIdx].centerY = 0;
                s_dedupGateways[groupIdx].count = 0;
                // Resolve display name. Static INF destinations may be placeholders
                // (overwritten at runtime by MAPJUMPO), so show generic "Exit" if
                // the destination doesn't look like it belongs to this field's area.
                // v0.07.95: World map fields (IDs 0-71) all say "World Map" instead of "wm00" etc.
                const char* dispName = GetFieldDisplayName(destId);
                if (destId <= 71) {
                    strncpy(s_dedupGateways[groupIdx].displayName, "Exit to World Map", 47);
                } else if (dispName) {
                    snprintf(s_dedupGateways[groupIdx].displayName, 48, "Exit to %s", dispName);
                } else {
                    strncpy(s_dedupGateways[groupIdx].displayName, "Exit", 47);
                }
                s_dedupGateways[groupIdx].displayName[47] = '\0';
            }
            // Accumulate center (will average after all gateways processed).
            s_dedupGateways[groupIdx].centerX += s_gateways[g].centerX;
            s_dedupGateways[groupIdx].centerY += s_gateways[g].centerZ;  // centerZ = Y in our coords
            s_dedupGateways[groupIdx].count++;
        }
        // Average the centers.
        for (int d = 0; d < s_dedupGatewayCount; d++) {
            if (s_dedupGateways[d].count > 0) {
                s_dedupGateways[d].centerX /= (float)s_dedupGateways[d].count;
                s_dedupGateways[d].centerY /= (float)s_dedupGateways[d].count;
            }
        }
        // Add to catalog.
        if (s_dedupGatewayCount > 0 && s_playerEntityIdx >= 0) {
            float gwPlayerX = 0, gwPlayerY = 0;
            bool gotPlayer = GetEntityPos(s_playerEntityIdx, gwPlayerX, gwPlayerY);
            for (int d = 0; d < s_dedupGatewayCount && newCount < MAX_CATALOG; d++) {
                // Screen filter: skip if center is separated from player by trigger lines.
                if (gotPlayer && s_capturedLineCount > 0) {
                    if (IsSeparatedByTriggerLine(gwPlayerX, gwPlayerY,
                                                 s_dedupGateways[d].centerX,
                                                 s_dedupGateways[d].centerY))
                        continue;
                }
                // Dedup against JSM exits already in catalog with same destination.
                bool dupExit = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].type != ENT_EXIT) continue;
                    // Check if any existing exit names match this gateway's display name.
                    if (strstr(newCatalog[c].name, s_gateways[0].destFieldName) != nullptr ||
                        strcmp(newCatalog[c].name, s_dedupGateways[d].displayName) == 0) {
                        dupExit = true; break;
                    }
                }
                if (dupExit) continue;
                EntityInfo gwExit = {};
                gwExit.entityIdx  = -400 - d;  // sentinel for INF gateway exits
                gwExit.modelId    = -1;
                gwExit.triangleId = 0;
                gwExit.type       = ENT_EXIT;
                gwExit.gatewayIdx = d;  // index into s_dedupGateways
                strncpy(gwExit.name, s_dedupGateways[d].displayName, sizeof(gwExit.name) - 1);
                gwExit.name[sizeof(gwExit.name) - 1] = '\0';
                newCatalog[newCount++] = gwExit;
                Log::Field("FieldNavigation: [refresh] INF-GW group %d: '%s' center=(%.0f,%.0f) %d gateways merged",
                           d, s_dedupGateways[d].displayName,
                           s_dedupGateways[d].centerX, s_dedupGateways[d].centerY,
                           s_dedupGateways[d].count);
            }
        }

        // Detect changes and log.
        bool changed = (newCount != s_catalogCount || added > 0);
        if (!changed) {
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].entityIdx != s_catalog[c].entityIdx ||
                    newCatalog[c].gatewayIdx != s_catalog[c].gatewayIdx) {
                    changed = true; break;
                }
            }
        }

        // Commit.
        memcpy(s_catalog, newCatalog, sizeof(s_catalog));
        s_catalogCount = newCount;
        s_nonPlayerCount = 0;
        for (int c = 0; c < s_catalogCount; c++) {
            if (s_catalog[c].entityIdx != s_playerEntityIdx)
                s_nonPlayerCount++;
        }

        // Restore selection to the same entity/gateway/bg, or clamp.
        s_selectedCatalogIdx = 0;
        if (prevSelectedEntity != -2) {
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == prevSelectedEntity &&
                    s_catalog[c].gatewayIdx == prevSelectedGateway) {
                    s_selectedCatalogIdx = c; break;
                }
            }
        }

        if (changed) {
            Log::Field("FieldNavigation: [refresh] catalog: %d entries (%d navigable, %d new entities), player=ent%d",
                       s_catalogCount, s_nonPlayerCount, added, s_playerEntityIdx);
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == s_playerEntityIdx) continue;
                if (s_catalog[c].entityIdx <= -300) {
                    int ji = -(s_catalog[c].entityIdx + 300);
                    if (ji >= 0 && ji < s_jsmEntityCount)
                        Log::Field("FieldNavigation: [refresh]   cat%d JSM ent%d type=%s name='%s' pos=(%d,%d)",
                                   c, ji, EntityTypeName(s_catalog[c].type), s_catalog[c].name,
                                   (int)s_jsmEntities[ji].posX, (int)s_jsmEntities[ji].posY);
                }
                else if (s_catalog[c].entityIdx <= -200) {
                    int ti = -(s_catalog[c].entityIdx + 200);
                    float tcx = (ti < s_capturedLineCount) ? (float)(s_capturedLines[ti].x1 + s_capturedLines[ti].x2) / 2.0f : 0;
                    float tcz = (ti < s_capturedLineCount) ? (float)(s_capturedLines[ti].y1 + s_capturedLines[ti].y2) / 2.0f : 0;
                    Log::Field("FieldNavigation: [refresh]   cat%d TRIGGER line%d center=(%.0f,%.0f) name='%s'",
                               c, ti, tcx, tcz, s_catalog[c].name);
                }
                else
                    Log::Field("FieldNavigation: [refresh]   cat%d ent%d model=%d type=%s name='%s'",
                               c, s_catalog[c].entityIdx, (int)s_catalog[c].modelId,
                               EntityTypeName(s_catalog[c].type), s_catalog[c].name);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: Exception in RefreshCatalog()");
    }
}

// ============================================================================
// v0.08.24: One-shot hex dump of PSHM_W entity-scope functions
// ============================================================================
// Reads raw x86 instruction bytes from the game's .text segment at runtime.
// These are the same bytes the CPU executes — we just log them so we can
// disassemble the parametric curve formula offline.
//
// Target addresses (Steam 2013 en-US, no ASLR):
//   0x00532890 — entity-scope parametric curve subroutine (~300 insns)
//   0x0051C9C0 — type-clamping dispatch (caller of 0x00532890)

