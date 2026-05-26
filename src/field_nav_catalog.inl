// field_nav_catalog.inl — Entity catalog building (RefreshCatalog).
// Included from field_navigation.cpp inside the FieldNavigation namespace.
// Do not compile independently.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.
// v0.17.7.0: Two large blocks moved to dedicated helper files for size
//            compliance (catalog.inl was 75.77 KB, 4 KB under the 80 KB
//            hard fail). Helpers live in:
//              field_nav_catalog_diag.inl
//                — DumpEntityDiagOnce, DumpBgDiagOnce,
//                  DumpPartyStateOnce, DumpCoordDiagOnce
//              field_nav_catalog_lateres.inl
//                — ResolveLatePositions, MatchSet3LateCaptures,
//                  ResolveStructPositions
//            Both included BEFORE this file in field_navigation.cpp so
//            their static functions are visible to RefreshCatalog.
//            Behavior byte-for-byte identical to v0.17.6.2 source except
//            the v0.12.17 VARBLOCK-POS unreachable `if (false)` block is
//            dropped. Git history at v0.17.6.2 preserves it.

// v0.17.8.3: Known FF8 field SYM names for playable / party-swap characters.
// Field scripts name the party entities after the character (with optional
// shadow/duplicate suffixes like 'squalls', 'squallsd', 'zells'), so a prefix
// match against these bases identifies a party member regardless of which
// field-local model slot the engine assigned. Draw points ('drpoint'), save
// points ('savePoint'/'saveline'), and generic NPCs never match, so this is a
// safe discriminator for the party filter (see RefreshCatalog). Includes the
// Laguna dream party (laguna/kiros/ward) and the intro/tutorial playables
// (seifer/edea).
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

        // One-shot diagnostic dumps (extracted v0.17.7.0).
        // See field_nav_catalog_diag.inl. Each helper no-ops on subsequent calls.
        DumpEntityDiagOnce(base, lim);
        DumpBgDiagOnce(lim);
        DumpPartyStateOnce();
        DumpCoordDiagOnce(base, lim);

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

            // v0.17.8.3: Resolve this entity's SYM name now (needed by the
            // party filter below). Same offset mapping used elsewhere in this
            // function for JSM lookups.
            const char* symName = "";
            {
                int symIdxFilt = s_symOthersOffset + i;
                if (symIdxFilt >= 0 && symIdxFilt < s_symNameCount)
                    symName = s_symNames[symIdxFilt];
            }

            // v0.14.108 / v0.17.8.3: Party-member / non-interactive-character filter.
            //
            // Earlier (v0.14.107) attempt cross-referenced canonical model→charId
            // map against the savemap active formation. That failed on bggate_1
            // because field entity model IDs are field-local slot indices, not
            // canonical character IDs — the engine reuses model slots per-field.
            // Followers showed up as model 2 and 4 there even with a Squall +
            // Zell + Selphie party (formation [1,0,5,255]).
            //
            // The behavioral fingerprint is what defines a FOLLOWING party
            // member: a visible character (model 0-9) the player walks through
            // (throughonoff > 0) with no talk/push interaction. That holds
            // regardless of which model slot the field assigned them.
            //
            // v0.17.8.3 adds the STANDING / high-model party member cases. In
            // scripted scenes (dormitory, Laguna dream intro) party members are
            // placed as static or walk-through actors with no talk/push, and
            // they don't always use a low follower model:
            //   - bgryo2_1 ent1 'squalls' model 3, ent2 'squallsd' model 5,
            //     all interaction flags 0 (standing). Caught by model OR name.
            //   - bgryo2_1 ent5 'selphie' model 11, thru>0, no talk/push (a
            //     scene actor using a full NPC model). model >= 10 so the
            //     model-based follower rule misses it -- only the NAME catches it.
            // All showed as 'NPC' before this fix (Fire Cavern bug #4).
            //
            // We cannot simply drop the throughonoff>0 / model<10 requirements:
            // some draw points reuse a party-character model with all flags zero
            // (Fire Cavern 'drpoint' uses model 9), and filtering those would
            // delete the draw point before the JSM reclassification can find it.
            // The safe discriminator is the SYM NAME: party members are named
            // after the character (squall/zell/laguna/...), while draw points are
            // 'drpoint'/'dp*', save points 'savePoint'/'saveline', and exits
            // 'l1' etc. -- none match IsPartyCharacterSym(). So:
            //   - model 0-9 + no talk/push + thru>0   -> following party member
            //     (model-based; also catches followers whose SYM didn't resolve)
            //   - character SYM + no talk/push (any model, any thru) -> party
            //     member placed in the scene (standing or walk-through actor)
            //   - non-character SYM (e.g. 'drpoint') -> KEEP, so the draw-point
            //     reclassification downstream still works.
            //
            // A talkable character (talk>0, e.g. a scripted 'talk to Seifer')
            // is NOT filtered -- noInteract is false -- so it stays navigable.
            // Real exits are added via the trigger-line / gateway path, not the
            // runtime entity, so filtering a character-named runtime actor never
            // removes a real exit (verified on bgryo2_1: the 'squalls' screen-
            // boundary exit still appears as cat2 after ent1 'squalls' is filtered).
            //
            // Race risk: if TALKRADIUS sets talkonoff after this scan, a real NPC
            // could be transiently filtered; mitigated by the per-F9 refresh.
            {
                bool isVisibleChar = (modelId >= 0 && modelId < 10);
                bool noInteract    = (talkonoff == 0 && pushonoff == 0);
                // Model-based: an unnamed visible character the player walks
                // through is a following party member.
                bool isFollower    = isVisibleChar && noInteract && throughonoff > 0;
                // Name-based: a party-character SYM with no talk/push, regardless
                // of model or walk-through. Covers standing members (model 0-9,
                // thru=0) and high-model scene actors (model >= 10, thru>0).
                bool isNamedParty  = noInteract && IsPartyCharacterSym(symName);
                if (i != s_playerEntityIdx && (isFollower || isNamedParty)) {
                    Log::Field("FieldNavigation: [party-filter] ent%d model=%d sym='%s' "
                               "filtered (%s; thru=%d)",
                               i, (int)modelId, symName,
                               isFollower ? "following party member"
                                          : "named party member",
                               (int)throughonoff);
                    continue;
                }
            }

            // v0.17.8.3: the v0.17.8.2 [party-filter-miss] diagnostic was
            // removed here once the fix was BAT-confirmed on bgryo2_1 (all six
            // party entities -- squalls/squallsd/zell/zells/selphie/selphies,
            // including the model-11 selphie -- filtered as named party members,
            // zero misses, navigation intact). Draw-point safety holds by
            // construction: 'drpoint' is not a character name and has thru=0, so
            // neither the follower nor the named-party branch touches it.

            // v0.17.7.1: Walkmesh exclusion rule.
            //
            // Drop entities that are BOTH non-talkable AND non-pushable AND
            // positioned off the walkmesh. These are typically light sources,
            // particle emitters, decorative props, and other scenery the
            // player cannot reach or interact with. The OR-with-talkonoff /
            // pushonoff condition preserves entities like over-railing guards
            // (off-mesh but talkable) and walking NPCs whose model puts them
            // briefly off-mesh between steps (talk radius keeps them).
            //
            // Light sources entering via JSM_ENT_INTERACTIVE_OBJECT promotion
            // (bypassing the existing ENTITY_SKIP_NAMES BG filter) drop here
            // because lights have no talkradius. fepic1's three exit Lines
            // pass through unaffected because they're injected from the
            // SETLINE/JSM-MAP_EXIT block, not the runtime loop -- their
            // walkmesh check lives in those blocks (added separately).
            //
            // The v0.12.08 reachability filter (REMOVED in v0.12.09 because
            // bggate_6 has a guard on tri=87 while the player stands on
            // tri=22, disconnected islands within one screen) does not
            // recur here: the guard has talkonoff>0 so the OR keeps it.
            //
            // Skip player and entities without a readable position (fpX=fpY=0
            // covers the placeholder case where the engine hasn't placed the
            // entity yet -- treat that as on-mesh provisionally rather than
            // dropping prematurely).
            if (i != s_playerEntityIdx &&
                talkonoff == 0 && pushonoff == 0 &&
                (fpX != 0 || fpY != 0)) {
                float wmX = (float)(fpX / 4096);
                float wmY = (float)(fpY / 4096);
                if (!IsInsideWalkmesh(wmX, wmY)) {
                    Log::Field("FieldNavigation: [walkmesh-excl] ent%d model=%d "
                               "pos=(%.0f,%.0f) off-mesh + no-talk/push -- excluded",
                               i, (int)modelId, wmX, wmY);
                    continue;
                }
            }

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
        //
        // v0.17.7.1: Removed the v0.12.24 field-wide demote that converted
        // SCREEN_BOUND lines into Interactions whenever ANY entity on the
        // field was an Interactive Object. That rule fired on fepic1 (Front
        // Gate 5) and turned the three legitimate exit Lines into
        // 'Interaction 1/2/3'. Per-line discrimination now happens in the
        // JSM scanner via TALKRADIUS/TALKON detection -- if a Line really IS
        // dual-purpose (dormitory bed: MAPJUMP + dialog + TALK setup) the
        // scanner classifies it as JSM_ENT_LINE_INTERACTIVE and this exit
        // loop skips it on lineType alone (no field-wide lookup needed).
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float scrPlayerX = 0, scrPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, scrPlayerX, scrPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                    // v0.17.7.1.2 / v0.17.7.5.4: SCREEN_BOUND lines that
                    // genuinely REQ a dialog-bearing entity are dual-purpose
                    // (exit-via-interaction). The Line REQs a background
                    // entity that fires dialog (dorm bed: bed Line REQs the
                    // bed Background, which shows "Sleep?"; the MAPJUMP fires
                    // as a consequence of the player choosing yes, not as a
                    // walk-across event). These show only as Interactions
                    // below, not as Exits here -- showing both would be
                    // confusing and the Exit name (next-day field) is
                    // uninformative anyway.
                    //
                    // fepic1's three exit Lines, bgroad_5 squalls (Hallway 5
                    // -> Dormitory), and similar pure-exit Lines do NOT
                    // REQ dialog entities (they may still use 0x1C extended
                    // dispatch for sound/particle effects, but that's not
                    // a dual-purpose signal), so they pass through here as
                    // Exits.
                    //
                    // The check used to be `hasExtDispatch` which incorrectly
                    // suppressed bgroad_5 squalls because squalls' own script
                    // uses 0x1C for non-dialog purposes. v0.17.7.5.4 split
                    // hasExtDispatch into two signals: hasExtDispatch (own
                    // 0x1C usage, very common, not a dual-purpose indicator)
                    // and hasDialogReqTarget (REQ to dialog/ext-dispatch
                    // entity, only set by REQ-following post-pass). The
                    // catalog now uses hasDialogReqTarget, which only fires
                    // for genuine dual-purpose Lines.
                    if (s_capturedLines[t].hasDialogReqTarget) continue;

                    // v0.17.7.5.5: Self-loop detection. SCREEN_BOUND lines whose
                    // resolved destField equals the CURRENT field id are in-place
                    // state transitions, not navigational exits. The canonical
                    // case is a dormitory bed: walking onto it does a MAPJUMP to
                    // the same field id (which the engine treats as "reload this
                    // field, advancing some state like day/night"). The player
                    // doesn't move to a different room -- they wake up where
                    // they slept.
                    //
                    // BAT'd on bgryo1_4 (Dormitory Double 4, field 240): ent0
                    // 'squall' SCREEN_BOUND with destField=240 was labeled
                    // "Exit to B-Garden - Dormitory Double 4" -- nonsense
                    // because that IS the field the player is on. Aaron
                    // correctly identified the bed should be an Interaction.
                    //
                    // Block 2 below picks up self-loop SCREEN_BOUND lines as
                    // Interactions (same condition mirrored there). This is
                    // the same pattern as the hasDialogReqTarget split for
                    // genuinely dual-purpose Lines: Block 1 suppresses, Block 2
                    // emits the appropriate Interaction label.
                    //
                    // Safety: an in-place state-change Line that ISN'T a sleep
                    // transition (e.g. a script-driven looping animation Line)
                    // would also be treated as an Interaction here. That's
                    // mostly fine -- such a Line is still something the player
                    // CAN interact with, even if the meaning differs from
                    // "sleep here". A bare "Exit" label to the current field
                    // is unambiguously wrong; Interaction is at worst slightly
                    // imprecise.
                    {
                        uint16_t curFid = FF8Addresses::pCurrentFieldId
                                          ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                        if (s_capturedLines[t].destFieldId == (int)curFid) continue;
                    }
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;

                    // Reachability: trigger center must not be separated from player
                    // by any other active screen-boundary trigger line.
                    if (IsSeparatedByTriggerLine(scrPlayerX, scrPlayerY, tcx, tcy))
                        continue;

                    // v0.17.7.1.1: Robust destination recovery for PSHM_W-sourced
                    // MAPJUMPs. When the JSM static scan couldn't extract a usable
                    // destFieldId (the script pushed a memory-variable marker like
                    // 0x8000xxxx at MAPJUMP time, which the scanner treats as a
                    // marker and the marker survives into this code path as a
                    // negative int32 or an out-of-range positive), match the
                    // SETLINE center to the nearest INF gateway. INF gateway
                    // destFieldIds are static binary data in the .inf file and
                    // reliable when present. Threshold: 1000 world units --
                    // SETLINE trigger lines and INF gateway lines for the same
                    // physical exit are typically co-located (both at the screen
                    // boundary), often within ~200 units; 1000 gives generous
                    // margin without risking cross-matching to a different exit.
                    //
                    // World-map dest (-2) is preserved -- those resolve correctly
                    // through the WorldMapJump branch below.
                    //
                    // The dedup-against-existing-exit check in the v0.07.94 INF
                    // gateway block runs after this and catches the duplicate via
                    // displayName strcmp (same FIELD_DISPLAY_NAMES table on both
                    // paths), so the INF gateway won't be added as a separate
                    // entry once we've recovered its destId here.
                    int destId = s_capturedLines[t].destFieldId;
                    if ((destId < 0 || destId >= FIELD_DISPLAY_NAMES_COUNT) &&
                        destId != -2 && s_gatewayCount > 0) {
                        float bestDistSq = 1000.0f * 1000.0f;
                        int bestGw = -1;
                        for (int gi = 0; gi < s_gatewayCount; gi++) {
                            float gdx = s_gateways[gi].centerX - tcx;
                            float gdy = s_gateways[gi].centerZ - tcy;
                            float dsq = gdx*gdx + gdy*gdy;
                            if (dsq < bestDistSq) {
                                bestDistSq = dsq;
                                bestGw = gi;
                            }
                        }
                        if (bestGw >= 0) {
                            int recoveredId = (int)s_gateways[bestGw].destFieldId;
                            Log::Field("FieldNavigation: [refresh] SETLINE line%d "
                                       "center=(%.0f,%.0f) destId=%d unresolvable -> "
                                       "matched INF gateway %d destId=%d (dist=%.0f) "
                                       "-- recovering",
                                       t, tcx, tcy, destId, bestGw, recoveredId,
                                       sqrtf(bestDistSq));
                            destId = recoveredId;
                        } else {
                            Log::Field("FieldNavigation: [refresh] SETLINE line%d "
                                       "center=(%.0f,%.0f) destId=%d unresolvable, "
                                       "no INF gateway within 1000 units -- staying generic",
                                       t, tcx, tcy, destId);
                        }
                    }

                    // Resolve destination name from MAPJUMP field ID.
                    char exitName[48];
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
                    // v0.17.8.7: ALSO skip LINE_INTERACTIVE. With campan/event/screenbound/
                    // unknown all skipped, LINE_INTERACTIVE was the ONLY type this block still
                    // emitted -- and the Interaction block below ALSO emits it (same -200-t
                    // sentinel), so every interactive line was injected TWICE: once as "Event"
                    // (type ENT_OBJECT) and once as "Interaction N" (type ENT_INTERACTION). On
                    // bghall_1 line5 (a pathway sign) showed as both, and the F9 cursor appeared
                    // to "flicker" between Event and Interaction. Worse, the bogus ENT_OBJECT
                    // "Event" entry tripped the JSM-injection block's `alreadyInCatalog`
                    // (type==ENT_OBJECT) test, suppressing the real Directory (igyous1, also
                    // ENT_OBJECT). Skipping LINE_INTERACTIVE here makes this block emit nothing
                    // (its original UNKNOWN-only purpose was already removed in v0.12.12); genuine
                    // interactions still surface once, via the Interaction block.
                    if (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_CAMERA_PAN ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_EVENT ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE ||
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

        // v0.12.24 / v0.17.7.1: Add SETLINE-triggered interactive objects as
        // "Interaction N". Line entities classified as JSM_ENT_LINE_INTERACTIVE
        // by the JSM scanner have dialog opcodes (MES/ASK/AMES/AASK) AND a
        // TALKRADIUS/TALKON setup -- genuine player-facing interactions
        // (dormitory bed/desk/wardrobe, classroom desk/sign, etc.).
        //
        // v0.17.7.1: dropped the dual-purpose SCREEN_BOUND-promote-to-Interactive
        // path. JSM scanner now classifies dual-purpose lines (MAPJUMP + dialog
        // + talk setup, like dormitory beds) directly as LINE_INTERACTIVE
        // because TALKRADIUS/TALKON wins over MAPJUMP in the new priority
        // ordering. fepic1's three exit Lines (MAPJUMP only, no dialog, no
        // talk setup) stay LINE_SCREEN_BOUND and are added as Exits above
        // rather than mislabeled here.
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float intPlayerX = 0, intPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, intPlayerX, intPlayerY)) {
                int interactionNum = 0;
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    // v0.17.7.1.2 / v0.17.7.5.4 / v0.17.7.5.5: Accept
                    // SCREEN_BOUND lines as Interactions in two cases:
                    //   1. hasDialogReqTarget=true (genuine dual-purpose,
                    //      e.g. dorm bed Line REQs dialog-bearing Background)
                    //   2. destFieldId == currentFieldId (self-loop sleep
                    //      transition, e.g. bgryo1_4 bed MAPJUMPs to field 240
                    //      which IS bgryo1_4) -- introduced v0.17.7.5.5 after
                    //      Aaron BAT'd the bed-as-exit mislabel.
                    //
                    // The SETLINE-Exit block above skips those same lines so
                    // they only appear here.
                    //
                    // Pure-exit SCREEN_BOUND lines (fepic1, bgroad_5 squalls)
                    // have hasDialogReqTarget=false AND destFieldId pointing
                    // to a different field -- they fall through this whole
                    // block and remain as Exits emitted by Block 1.
                    bool isInteractive =
                        (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE);
                    if (!isInteractive &&
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        if (s_capturedLines[t].hasDialogReqTarget) {
                            isInteractive = true;
                        } else {
                            // v0.17.7.5.5: self-loop check.
                            uint16_t curFid = FF8Addresses::pCurrentFieldId
                                              ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                            if (s_capturedLines[t].destFieldId == (int)curFid) {
                                isInteractive = true;
                            }
                        }
                    }
                    if (!isInteractive) continue;
                    // Don't check alreadyAdded -- Interactions use sentinel -600-t,
                    // distinct from exit sentinel -200-t, so both can coexist.
                    // Reachability: must be on same side of screen-boundary trigger lines.
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    if (IsSeparatedByTriggerLine(intPlayerX, intPlayerY, tcx, tcy))
                        continue;
                    interactionNum++;
                    EntityInfo intEntry = {};
                    intEntry.entityIdx  = -200 - t;  // same sentinel as exits -- position lookup works identically
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

        // Late position resolution (extracted v0.17.7.0).
        // See field_nav_catalog_lateres.inl. Must run in this order — STRUCT-POS
        // depends on LATE-RESOLVE having populated hasPosition for entities that
        // had only hasPshmCoords on entry.
        ResolveLatePositions();
        MatchSet3LateCaptures();
        ResolveStructPositions();

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

            // v0.17.7.1: Walkmesh exclusion for off-mesh Interactive Objects
            // without TALKRADIUS/TALKON. Lights and decorative props get
            // incorrectly promoted to JSM_ENT_INTERACTIVE_OBJECT during the
            // JSM scan (foundDialogOp/foundExtDispatch + SET3 position make
            // them look like real interactive objects). Almost all of them
            // sit off-walkmesh, so excluding off-mesh + no-talk-setup catches
            // the bug without dropping real signs/desks (those land on the
            // walkmesh because the player has to stand on top of them or
            // adjacent to them to read).
            //
            // Save/Draw/Shop/Card points are NOT filtered here: they may
            // use proximity (PARTICLEON + MENUSAVE etc.) rather than
            // TALKRADIUS, and they're always valuable navigation targets.
            // MAP_EXIT injection runs in a separate block below; it's also
            // not subject to this filter -- exits are always valuable.
            if (jt == ENT_OBJECT && !je.hasTalkSetup &&
                !IsInsideWalkmesh((float)je.posX, (float)je.posY)) {
                Log::Field("FieldNavigation: [walkmesh-excl] JSM ent%d '%s' "
                           "INTERACTIVE_OBJECT pos=(%d,%d) off-mesh + no-talk-setup -- excluded",
                           je.jsmIndex, je.symName, (int)je.posX, (int)je.posY);
                continue;
            }
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
            // v0.17.8.6: Suppress dead positionless exits with unresolved
            // destinations. bgryo2_1 ent15 'l1' is a JSM_ENT_MAP_EXIT with no
            // SET3/SETLINE position and param=INT_MIN (0x80000000 -- a runtime-var
            // destination the static scan could not resolve). On a field with no
            // INF gateways the gateway-suppression check below never fires, so
            // without this the entity injects a bare second "Exit" with no
            // position -- the duplicate, useless exit Aaron reported. An exit that
            // has neither a navigable position nor a resolvable/world-map
            // destination cannot be driven to or named; drop it. (param==-2 is the
            // world-map sentinel and is kept.)
            if (!hasPos && je.param != -2 &&
                (je.param < 0 || je.param >= FIELD_DISPLAY_NAMES_COUNT)) {
                Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' dropped: "
                           "no position, unresolved dest (param=%d)",
                           je.symName, je.param);
                continue;
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
