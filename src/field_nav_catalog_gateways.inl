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
        // v0.18.3.301 (#91 R3): two gateways sharing a destination are the SAME
        // exit only if they are also in the same place. Observed separations:
        // genuine duplicates < 200 units; the prison ring's two crossings 3,644.
        const float GATEWAY_CLUSTER_RADIUS = 800.0f;
        for (int g = 0; g < s_gatewayCount && s_dedupGatewayCount < MAX_DEDUP_GATEWAYS; g++) {
            uint16_t destId = s_gateways[g].destFieldId;
            // Find existing dedup group for this destination.
            //
            // v0.18.3.301 (#91 R3): matching the destination is NOT enough.
            // The D-District Prison shaft is a circular walkway around a
            // central hole, split across two screens -- gpbig1a is the left
            // half, gpbig2a the right -- with a crossing at the TOP of the
            // circle and another at the BOTTOM. Both crossings lead to the
            // other half, so both gateways carry the same destFieldId, and
            // grouping on that alone collapsed two exits **3,644 units apart**
            // into a single entry at their midpoint -- open floor belonging to
            // neither crossing, which auto-drive then obediently walked to.
            //
            // So a candidate only joins a group if it is also spatially near
            // it. Genuine duplicates (the case this dedupe exists for) sit
            // within ~200 units of each other; the shaft pair is 18x that.
            // 800 leaves a wide margin on both sides.
            //
            // centerX/centerY hold running SUMS at this point (they are
            // averaged after the loop), hence the divide by count.
            int groupIdx = -1;
            for (int d = 0; d < s_dedupGatewayCount; d++) {
                if (s_dedupGateways[d].destFieldId != destId) continue;
                if (s_dedupGateways[d].count > 0) {
                    float avgX = s_dedupGateways[d].centerX / (float)s_dedupGateways[d].count;
                    float avgY = s_dedupGateways[d].centerY / (float)s_dedupGateways[d].count;
                    float ddx  = s_gateways[g].centerX - avgX;
                    float ddy  = s_gateways[g].centerZ - avgY;  // centerZ = Y in our coords
                    if ((ddx * ddx + ddy * ddy) >
                        (GATEWAY_CLUSTER_RADIUS * GATEWAY_CLUSTER_RADIUS)) {
                        Log::Field("FieldNavigation: [refresh] INF-GW gw%d dest=%u at (%.0f,%.0f) "
                                   "SPLIT from group %d at (%.0f,%.0f) -- %.0f units apart "
                                   "[v0.18.3.301 #91 R3]",
                                   g, (unsigned)destId,
                                   s_gateways[g].centerX, s_gateways[g].centerZ,
                                   d, avgX, avgY,
                                   sqrtf(ddx * ddx + ddy * ddy));
                        continue;  // same destination, different place -- new group
                    }
                }
                groupIdx = d;
                break;
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

        // v0.18.3.301 (#91 R3): name the two ring crossings.
        //
        // Once R3 splits them, the shaft halves carry TWO exits with the same
        // destination and therefore the same display name -- and the exact-name
        // dedupe further down (v0.18.3.270) would drop the second one. They
        // need distinct names to survive, and "Exit to Galbadia D-District
        // Prison 5" twice would in any case tell the player nothing about
        // which way round the hole they are being sent.
        //
        // Both crossings land on the same floor (10 crossings, 0 floor changes
        // in the .299 BAT), so a floor label would be actively wrong here --
        // the floor belongs on the stairs, not on these. What distinguishes
        // them is position on the ring, which is exactly how Aaron describes
        // the room: "you can go from district 3 to district 5 across the top of
        // the circle or across the bottom of the circle." The destination is
        // dropped because both go to the same place; the only useful fact is
        // WHICH WAY ROUND.
        //
        // Derived from the gateway's own Y, not from the camera, so the label
        // is stable wherever the player is standing and on either half.
        //
        // ASSUMPTION, flagged for the BAT: larger Y = "top". This codebase's
        // convention is "X = screen-horizontal, Y = screen-vertical" but the
        // SIGN is not documented anywhere and I refuse to pretend I verified
        // it (the .298 gateway-Z hypothesis died of exactly this). Both centres
        // are logged below; if the two read swapped in play it is a one-line
        // flip and costs nothing but the rename.
        {
            uint16_t ringFid = FF8Addresses::pCurrentFieldId
                               ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
            bool onRingHalf = (ringFid == 0x031A || ringFid == 0x031B ||
                               ringFid == 0x031C || ringFid == 0x031D);
            if (onRingHalf) {
                for (int a = 0; a < s_dedupGatewayCount; a++) {
                    for (int b = a + 1; b < s_dedupGatewayCount; b++) {
                        if (s_dedupGateways[a].destFieldId !=
                            s_dedupGateways[b].destFieldId) continue;
                        int topIdx = (s_dedupGateways[a].centerY >=
                                      s_dedupGateways[b].centerY) ? a : b;
                        int botIdx = (topIdx == a) ? b : a;
                        strncpy(s_dedupGateways[topIdx].displayName, "Top crossing", 47);
                        s_dedupGateways[topIdx].displayName[47] = '\0';
                        strncpy(s_dedupGateways[botIdx].displayName, "Bottom crossing", 47);
                        s_dedupGateways[botIdx].displayName[47] = '\0';
                        Log::Field("FieldNavigation: [refresh] ring crossings named: "
                                   "group %d (%.0f,%.0f) = 'Top crossing', "
                                   "group %d (%.0f,%.0f) = 'Bottom crossing' "
                                   "[v0.18.3.301 #91 R3 -- larger Y assumed to be top]",
                                   topIdx, s_dedupGateways[topIdx].centerX,
                                   s_dedupGateways[topIdx].centerY,
                                   botIdx, s_dedupGateways[botIdx].centerX,
                                   s_dedupGateways[botIdx].centerY);
                    }
                }
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

        // v0.18.3.300 (#91 R2): SCOPE THE RULE ABOVE TO ITS EVIDENCE BASE.
        //
        // The 2026-07-31 BAT caught this rule deleting a REAL exit and leaving
        // Aaron with no listed way off the screen. On gpbig2a (D-District
        // Prison, Prison 5) the catalog held exactly two entries -- a cell door
        // and the Directory -- on all five visits:
        //
        //   [LINE-PAIR] jsmDoors=1 jsmLines=1 jsmEntities=21 captured=1
        //     line0 center=(1459,2509) type=6 param=965 | lineType=11 dest=965
        //   [refresh] INF-GW group 0 'Exit to Prison 3' (destId=795) SUPPRESSED:
        //     field resolves exits via trigger lines and no live line targets
        //     this destination -- stale INF data for another story state
        //   [refresh] catalog: 2 entries (2 navigable, 0 new entities)
        //
        // The premise is inverted here. gpbig2a has ONE screen-bound line and it
        // points at a CELL (965). That single unrelated line arms
        // fieldHasLineExits, which then deletes the only real exit off the
        // screen -- the walkway back to gpbig1a, which Aaron then walked through
        // anyway, 30 seconds later, proving it live. "The field resolves exits
        // via trigger lines" is true only in the most literal and least useful
        // sense: the shaft screens use BOTH mechanisms, for DIFFERENT exits.
        //
        // The fix is to scope the heuristic back to the evidence it was actually
        // established on, exactly as .291 rescoped the addr-as-literal exit
        // heuristic to bg* fields after it fabricated a Centra Ruins exit.
        // Re-reading the evidence block above: the rule was derived from, and
        // has only ever been needed by, ONE field -- glfurin1 -- and glfurin1
        // carries a SINGLE gateway. Every other field listed there resolves zero
        // line exits (glpreo1/2/3, glprefr2, glstage1), so fieldHasLineExits is
        // false and the rule never fires on them at all; glwitch1 has both but
        // its gateway destination EQUALS its line destination, so it is kept by
        // the agreement test whether or not this gate exists. The change is
        // therefore provably inert on every field in the documented evidence
        // base, preserves glfurin1, and fixes both prison shaft screens
        // (INF parsed: 2 active gateways on gpbig1a AND gpbig2a).
        //
        // Note this MUST test s_gatewayCount (raw INF entries), not
        // s_dedupGatewayCount: the prison's two gateways share a destination and
        // merge into ONE group, so the dedup count is 1 there and would gate
        // nothing.
        //
        // DIRECTION OF RISK: this is a pure narrowing. It can only ever ADD
        // exits back to the catalog, never remove one. The worst case is a ghost
        // gateway reappearing on some multi-gateway field we have not visited --
        // an exit that does nothing when you walk to it. That is annoying.
        // Losing a real exit is this project's worst failure mode (#88,
        // ladline7 hidden for three BAT cycles, and now gpbig2a). Erring toward
        // showing too much is the correct side to be wrong on.
        bool staleGatewayRuleActive = fieldHasLineExits && (s_gatewayCount <= 1);
        if (fieldHasLineExits && !staleGatewayRuleActive) {
            Log::Field("FieldNavigation: [refresh] stale-gateway rule NOT applied: "
                       "%d raw INF gateways (> 1) -- rule is scoped to its single-gateway "
                       "evidence base (glfurin1); this field uses lines AND gateways for "
                       "different exits [v0.18.3.300 #91]",
                       s_gatewayCount);
        }

        // Add to catalog.
        if (s_dedupGatewayCount > 0 && s_playerEntityIdx >= 0) {
            float gwPlayerX = 0, gwPlayerY = 0;
            bool gotPlayer = GetEntityPos(s_playerEntityIdx, gwPlayerX, gwPlayerY);
            for (int d = 0; d < s_dedupGatewayCount && newCount < MAX_CATALOG; d++) {
                // Stale-gateway filter (see fieldHasLineExits above): on a
                // script-exit field, keep a gateway only if some live trigger line
                // agrees on its destination.
                // v0.18.3.300 (#91 R2): gated on staleGatewayRuleActive so the rule
                // only fires on the single-gateway shape it was validated against.
                if (staleGatewayRuleActive) {
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
