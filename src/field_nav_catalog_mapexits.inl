// ============================================================================
// field_nav_catalog_mapexits.inl — JSM_ENT_MAP_EXIT catalog injection
// ============================================================================
// v0.18.3.266: extracted verbatim from field_nav_catalog.inl to keep that file
// under the CI source-file size ceiling (.github/workflows/safety-checks.yml:
// soft warn > 60 KB, HARD FAIL > 80 KB). field_nav_catalog.inl had reached
// 82 KB and the push utility's local mirror of the CI check refused the push.
//
// Same pattern as field_nav_catalog_dedupe.inl (v0.17.8.9) and
// field_nav_catalog_naming.inl: this is NOT a standalone function. It is a
// fragment of RefreshCatalog()'s body, #included inline at the point where the
// block used to sit, so it operates directly on that function's locals:
//
//   newCatalog[] / newCount   — catalog under construction
//   base / lim                — runtime "others" entity array + count
//   s_jsmEntities[] / s_jsmEntityCount
//   s_capturedLines[] / s_capturedLineCount
//   s_gateways[] / s_gatewayCount
//   s_symNames[] / s_symNameCount / s_symOthersOffset
//   s_playerEntityIdx
//
// Behaviour is byte-for-byte identical to the pre-extraction code; this was a
// pure textual move with no logic change.
//
// What it does: turns JSM "Others" entities classified JSM_ENT_MAP_EXIT
// (elevators, doors, trigger zones whose scripts contain MAPJUMP) into ENT_EXIT
// catalog entries, resolving the destination name and a position from SET3 or a
// captured SETLINE centre, then filtering dead/duplicate/off-screen exits.
// ============================================================================

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
            // v0.18.3.284 (#86 follow-up): true once exitName above resolved to a
            // real destination (a known field or the world map), not the generic
            // "Exit" fallback. Gates the live-entity-position fallback below so it
            // can never rescue an entity like 'tobi' (glprein1 ent9) whose param is
            // an unresolved runtime-var marker -- only a MAP_EXIT the interpreter/
            // scanner already trusts the destination of gets a fabricated position.
            bool destResolved = (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) || destId == -2;
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
            // v0.18.3.284 (#86 follow-up): Live runtime-position fallback for
            // scripted/hidden MAP_EXIT entities that have neither a SET3 position
            // nor a captured SETLINE. glprein1's trapdoor ('irvine') is triggered
            // by TALKRADIUS/interaction, not a walk-across line, so it never calls
            // SETLINE, and its init script never calls SET3 either -- both existing
            // position sources come up empty and the exit was injected at pos=(0,0),
            // unreachable by manual or auto navigation (BAT 2026-07-18: "trapdoor to
            // the clocktower still has no coordinates"). But the entity IS placed on
            // the walkmesh at runtime (confirmed via the [SCAN] pass, tri=38) --
            // it's only left out of the general NPC/interaction scan because a HIDE
            // flag marks it invisible. GetEntityPos() doesn't check that flag, only
            // walkmesh placement (triId != 0), so it can still read the entity's
            // live position directly. je.jsmIndex is the flat Door+Line+Bg+Other
            // scan index, and it maps 1:1 onto the runtime "Others" array index
            // used elsewhere by GetEntityPos/the [SCAN] loop with NO subtraction of
            // the doors+lines+backgrounds count -- confirmed both here (glprein1
            // 'irvine': jsmIndex=2, [SCAN] reports the same entity as ent2) and on
            // glwater1 (sakua/sakub/oku: jsmIndex 17/18/19, independently found in
            // an earlier BAT to sit past the old MAX_ENTITIES=16 cap, which only
            // holds if their runtime index is the unmodified jsmIndex, not
            // jsmIndex-9). Gated on destResolved so an entity like 'tobi' (glprein1
            // ent9, an unresolved runtime-var marker destination) can't be rescued
            // into a bogus positioned "Exit" entry by this fallback.
            //
            // SAFETY CHECK: the jsmIndex->runtime-slot mapping can coincidentally
            // land on a LIVE PARTY MEMBER's slot instead of the scripted exit
            // entity's own slot. glclock1's 'irvine' MAP_EXIT (the false exit
            // .283 just fixed) has jsmIndex=2, and glclock1's runtime slot 2 is
            // actually Rinoa (setpc=4) mid-scene -- without this check, this
            // fallback would silently reintroduce the false "Exit to wm05" with a
            // fabricated-but-plausible position (Rinoa's), defeating .283's veto
            // (which only fires when !hasPos). glprein1's trapdoor slot was
            // confirmed NOT a party-character slot: its [SCAN-DROP] hidden-filter
            // log line fires with no preceding [party-filter] line, meaning the
            // party-filter check (which runs first) already tested and rejected
            // isPartyChar for it. Refuse the fallback whenever the live slot's
            // setpc reads as a valid party character (0-7) -- a party member's
            // position is never a map exit's position.
            if (!hasPos && destResolved && je.jsmCategory == 3) {
                int liveIdx = je.jsmIndex;
                if (!IsPartyCharacterSetpc(GetEntitySetpc(liveIdx))) {
                    float rex = 0, rey = 0;
                    if (GetEntityPos(liveIdx, rex, rey)) {
                        exitX = rex;
                        exitY = rey;
                        hasPos = true;
                        s_jsmEntities[j].posX = (int16_t)exitX;
                        s_jsmEntities[j].posY = (int16_t)exitY;
                        s_jsmEntities[j].hasPosition = true;
                        Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' position from "
                                   "live entity idx=%d (%.0f,%.0f) [hidden/scripted fallback]",
                                   je.symName, liveIdx, exitX, exitY);
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
            // v0.18.3.283 (#86 follow-up): don't trust an unpositioned MAP_EXIT
            // when a trigger-line (SCREEN_BOUND) Exit already covers this field.
            // glclock1 has exactly one real exit -- the SCREEN_BOUND 'squall'
            // trigger line back to Presidential Residence 7, confirmed against
            // the live [MAPJUMP-HOOK] oracle (destField=746) and against Aaron's
            // own play (2026-07-18 BAT: "just the one exit... back to Residence
            // 7"). Its 'irvine' MAP_EXIT entity ALSO resolves via INTERP to a
            // concrete literal (field 5, "wm05" -- an internal placeholder, not
            // a real destination) -- provenance-tainting (.282) doesn't catch
            // this because the value genuinely is a hardcoded literal on
            // whichever branch the interpreter reached; that branch is simply
            // not the one the live engine takes here (inactive/off-context
            // script path, not a live-variable problem). The dedup check above
            // only catches this when both destinations happen to MATCH, which
            // isn't the case for a wrong resolution. This field has 0 INF
            // gateways, so neither gateway-cross-check below ever runs either --
            // without this rule the wrong exit sails through uncontested.
            // Position-having MAP_EXITs are NOT touched by this rule: a real,
            // separately-walkable second door is still trusted (glprein1's
            // trapdoor has no competing trigger-line exit at all, so it's
            // unaffected either way).
            if (!hasPos) {
                bool triggerExitExists = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].type == ENT_EXIT &&
                        newCatalog[c].entityIdx <= -200 && newCatalog[c].entityIdx > -300) {
                        triggerExitExists = true;
                        break;
                    }
                }
                if (triggerExitExists) {
                    Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' dest=%d filtered "
                               "(unpositioned, and a trigger-line exit already covers this field)",
                               je.symName, je.param);
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
            //
            // v0.18.3.281 (#86): this "unmatched = stale" assumption doesn't hold
            // when the destination came from the authoritative interpreter
            // (info.paramFromInterp) rather than the abstract fallback resolver.
            // glprein1's trapdoor ('irvine', destField=716) is a real third exit
            // that this field's 2 INF gateways simply never registered -- scripted/
            // hidden mechanisms typically aren't INF walk-through triggers. An
            // INTERP result is the concrete destField the engine will actually use,
            // so it's trusted even without an INF match; a fallback-sourced (LITERAL/
            // VARBLOCK) destination that doesn't match is still treated as likely
            // stale, same as before (this is what the original bgryo2_1 'l1' fix
            // relied on, and stays intact).
            if (s_gatewayCount > 0 && !je.paramFromInterp) {
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
