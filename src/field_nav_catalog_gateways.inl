// ============================================================================
// field_nav_catalog_gateways.inl — INF gateway exit injection
// ============================================================================
// v0.18.3.276: extracted verbatim from field_nav_catalog.inl to bring that file
// back under the CI source-file size ceiling (.github/workflows/safety-checks.yml:
// soft warn > 60 KB, HARD FAIL > 80 KB). It had grown to 82.2 KB across the
// #83/#82/#71 work and the push utility refused the push.
//
// Same pattern as field_nav_catalog_mapexits.inl (v0.18.3.266),
// field_nav_catalog_dedupe.inl and field_nav_catalog_naming.inl: this is NOT a
// standalone function. It is a fragment of RefreshCatalog()'s body, #included
// inline where the block used to sit, so it operates directly on that
// function's locals:
//
//   newCatalog[] / newCount    — catalog under construction
//   s_gateways[] / s_gatewayCount
//   s_dedupGateways[] / s_dedupGatewayCount
//   s_capturedLines[] / s_capturedLineCount
//   s_playerEntityIdx
//
// Behaviour is byte-for-byte identical to the pre-extraction code; this was a
// pure textual move with no logic change.
//
// What it does: groups raw INF gateways by destination field, averages each
// group's centre into one catalog entry, filters groups whose path from the
// player actually crosses a screen-boundary segment, de-duplicates against
// exits already in the catalog, and injects the survivors as ENT_EXIT entries.
// ============================================================================

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
        // v0.18.3.279: does this field resolve its exits through script trigger
        // lines (SETLINE + MAPJUMP) rather than through static INF gateways?
        //
        // When it does, the INF gateway table is not the field's live exit list --
        // it is static map data that can describe exits belonging to a DIFFERENT
        // story state of the same room. glfurin1 (Caraway's Mansion 1) is the case
        // that exposed this: its script resolves two real line exits (725 Mansion 5,
        // 724 Mansion 4) while INF also carries gw[0] line=(-862,-360)->(-862,-497)
        // destId=726 "Mansion 6" -- a doorway that only opens on a later visit.
        // The mod catalogued it, the player walked to it, and nothing happened,
        // because the engine's own gateway check never fires in this story state.
        //
        // The destination-name dedupe below cannot catch it: 726 matches neither
        // 725 nor 724, so it is not a duplicate of anything -- it is a ghost.
        //
        // Evidence this is safe rather than a blunt instrument (2026-07-18 log,
        // full Deling + B-Garden sweep): every other field carrying INF gateways
        // resolves ZERO line exits, so the rule never fires there --
        //   glpreo1  1 gw, 7 lines all dest=-1      glpreo2  3 gw, 4 lines all dest=-1
        //   glprefr2 1 gw, 0 lines                  glstage1 3 gw, 0 lines
        //   glpreo3  8 gw, 0 lines
        // and glwitch1, the one field with both, has gateway destId=746 EQUAL to
        // its line exit dest=746, so the gateway is kept by the match below. Only
        // glfurin1 has a gateway whose destination no line agrees with.
        bool fieldHasLineExits = false;
        for (int lc = 0; lc < s_capturedLineCount; lc++) {
            if (!s_capturedLines[lc].active) continue;
            if (s_capturedLines[lc].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                s_capturedLines[lc].destFieldId > 0) {
                fieldHasLineExits = true;
                break;
            }
        }

        // Add to catalog.
        if (s_dedupGatewayCount > 0 && s_playerEntityIdx >= 0) {
            float gwPlayerX = 0, gwPlayerY = 0;
            bool gotPlayer = GetEntityPos(s_playerEntityIdx, gwPlayerX, gwPlayerY);
            for (int d = 0; d < s_dedupGatewayCount && newCount < MAX_CATALOG; d++) {
                // Stale-gateway filter (see fieldHasLineExits above): on a
                // script-exit field, keep a gateway only if some live trigger line
                // agrees on its destination.
                if (fieldHasLineExits) {
                    bool lineAgrees = false;
                    for (int lc = 0; lc < s_capturedLineCount && !lineAgrees; lc++) {
                        if (!s_capturedLines[lc].active) continue;
                        if (s_capturedLines[lc].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                            s_capturedLines[lc].destFieldId == (int)s_dedupGateways[d].destFieldId)
                            lineAgrees = true;
                    }
                    if (!lineAgrees) {
                        Log::Field("FieldNavigation: [refresh] INF-GW group %d '%s' (destId=%u) "
                                   "SUPPRESSED: field resolves exits via trigger lines and no live "
                                   "line targets this destination -- stale INF data for another story state",
                                   d, s_dedupGateways[d].displayName,
                                   (unsigned)s_dedupGateways[d].destFieldId);
                        continue;
                    }
                }
                // Screen filter: skip the gateway only if the player->gateway
                // SEGMENT actually crosses a screen-boundary line SEGMENT.
                // v0.17.8.10: replaced IsSeparatedByTriggerLine() here -- that
                // does an INFINITE-line side test, so a short SCREEN_BOUND line
                // on a far edge (bghall_5's Hall 6 doorway, x in [4206,5042])
                // wrongly "separated" the Hall 4 INF gateway on the opposite
                // (west) edge because the gateway's Y lay almost on that line's
                // infinite extension. A gateway is a real exit you walk to; it
                // is on another screen only if the path to it actually crosses a
                // boundary segment. Entity screen-filtering still uses the
                // infinite-line helper; only the gateway test changed.
                if (gotPlayer && s_capturedLineCount > 0) {
                    bool crossed = false;
                    for (int dt = 0; dt < s_capturedLineCount && !crossed; dt++) {
                        if (!s_capturedLines[dt].active) continue;
                        if (s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                            s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_UNKNOWN)
                            continue;
                        if (SegmentsCross(gwPlayerX, gwPlayerY,
                                          s_dedupGateways[d].centerX, s_dedupGateways[d].centerY,
                                          (float)s_capturedLines[dt].x1, (float)s_capturedLines[dt].y1,
                                          (float)s_capturedLines[dt].x2, (float)s_capturedLines[dt].y2)) {
                            crossed = true;
                            Log::Field("FieldNavigation: [refresh] INF-GW group %d '%s' "
                                       "center=(%.0f,%.0f) filtered: path crosses screen-bound line%d",
                                       d, s_dedupGateways[d].displayName,
                                       s_dedupGateways[d].centerX, s_dedupGateways[d].centerY, dt);
                        }
                    }
                    if (crossed) continue;
                }
                // Dedup against JSM exits already in catalog with same destination.
                //
                // v0.18.3.271: removed a substring test that read
                //   strstr(newCatalog[c].name, s_gateways[0].destFieldName)
                // It was broken two ways: it always consulted gateway **0**
                // rather than the gateway being tested (d), and it matched a
                // SUBSTRING rather than the whole destination.
                //
                // It was inert until v0.18.3.270 only because destFieldName held
                // an internal field name ('bghall_1'), which never appears inside
                // a catalog entry name ("Exit to B-Garden - Hall 1") -- so the
                // clause never fired and the exact-name comparison below did all
                // the real work. Fixing GetFieldNameById() to return the display
                // name made the substring suddenly match, so a single gateway-0
                // name suppressed unrelated exits field-wide: B-Garden Hall 1
                // dropped from four exits to one, and the Quad->Hall exit vanished.
                //
                // The exact displayName comparison is the correct dedupe and is
                // exactly what was effectively running before .270.
                bool dupExit = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].type != ENT_EXIT) continue;
                    if (strcmp(newCatalog[c].name, s_dedupGateways[d].displayName) == 0) {
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
