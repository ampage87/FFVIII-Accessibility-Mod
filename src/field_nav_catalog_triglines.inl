// ============================================================================
// field_nav_catalog_triglines.inl — trigger-line Exit / Event catalog injection
// ============================================================================
// v0.18.3.294: extracted VERBATIM from field_nav_catalog.inl to get that file
// back under the CI source-file size ceiling (.github/workflows/safety-checks.yml:
// soft warn > 60 KB, HARD FAIL > 80 KB). field_nav_catalog.inl had been sitting
// 100-600 bytes under the hard fail for several builds, and every change was
// paying for itself by deleting explanatory comments -- see GitHub #37.
//
// Same pattern as field_nav_catalog_mapexits.inl (v0.18.3.266),
// field_nav_catalog_gateways.inl, field_nav_catalog_dedupe.inl (v0.17.8.9) and
// field_nav_catalog_naming.inl: this is NOT a standalone function. It is a
// fragment of RefreshCatalog()'s body, #included inline at the point where the
// block used to sit, so it operates directly on that function's locals:
//
//   newCatalog[] / newCount   — catalog under construction
//   s_jsmEntities[] / s_jsmEntityCount
//   s_capturedLines[] / s_capturedLineCount
//   s_fieldId / s_currentFieldName
//   s_playerEntityIdx
//
// PURE TEXTUAL MOVE — no logic change whatsoever. Byte-for-byte the same
// statements in the same order; only the surrounding file changed. If a BAT
// after this split behaves differently from the one before it, the split is
// the suspect, not the game.
// ============================================================================

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
                    // v0.18.3.303 (#91 R1): the shaft-staircase flag is computed
                    // HERE, above the dual-purpose filter, because in the .302 BAT
                    // the down staircase was BOTH -- self-destination AND
                    // hasDialogReqTarget=1 -- and this `continue` ran first, so the
                    // stair path below was never reached for it. Confirmed from the
                    // .302 log: gpbig1a line1 maps to jsm1 'squall', and
                    // '[JSMScan] REQ-interact: Line ent1 squall ... hasDialogReqTarget=1'.
                    // line2 (jsm2 'zell') has no REQ target, which is exactly why
                    // the stairs UP listed and the stairs DOWN did not.
                    //
                    // Behaviourally this makes sense and is not an accident of one
                    // field: descending runs a scripted climb-down with dialogue
                    // ('No sense going back up.'), so the line legitimately REQs a
                    // dialog-bearing entity. The dual-purpose rule -- 'if it talks,
                    // it is an Interaction, not an Exit' -- is right for a dormitory
                    // bed and wrong for a staircase, which is the only way off the
                    // floor. The exemption is scoped to shaft self-destination
                    // lines, so beds and every other dual-purpose line are untouched.
                    bool isSelfLoopStair = false;
                    {
                        uint16_t sfFid = FF8Addresses::pCurrentFieldId
                                         ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                        if (s_capturedLines[t].destFieldId == (int)sfFid &&
                            IsPrisonShaftFieldId(sfFid))
                            isSelfLoopStair = true;
                    }

                    if (s_capturedLines[t].hasDialogReqTarget && !isSelfLoopStair) {
                        continue;
                    }
                    if (s_capturedLines[t].hasDialogReqTarget && isSelfLoopStair) {
                        Log::Field("FieldNavigation: [refresh] STAIRS line%d dual-purpose "
                                   "(hasDialogReqTarget=1) but shaft self-destination -- "
                                   "kept as Exit (#91 R1)", t);
                    }

                    // v0.17.7.5.5: Self-loop detection. SCREEN_BOUND lines whose
                    // resolved destField == the CURRENT field id are in-place state
                    // transitions, not exits -- canonically a dormitory bed, which
                    // MAPJUMPs to its own field id to advance day/night state; the
                    // player wakes where they slept. BAT'd on bgryo1_4 (field 240):
                    // ent0 'squall' was labeled "Exit to Dormitory Double 4", the
                    // field already occupied. Block 2 below emits these as
                    // Interactions (same condition mirrored) -- same suppress-here/
                    // emit-there split as hasDialogReqTarget above.
                    //
                    // Safety: an in-place state-change Line that ISN'T a sleep
                    // transition (e.g. a script-driven looping animation Line)
                    // would also be treated as an Interaction here. That's
                    // mostly fine -- such a Line is still something the player
                    // CAN interact with, even if the meaning differs from
                    // "sleep here". A bare "Exit" label to the current field
                    // is unambiguously wrong; Interaction is at worst slightly
                    // imprecise.
                    //
                    // v0.18.3.302 (#91 R1): ...EXCEPT in the D-District Prison
                    // shaft, where a self-destination line is not a bed at all --
                    // it is a STAIRCASE, and it is the only thing that changes
                    // floor. gpbig1a carries two and both were lost: line1
                    // surfaced as the meaningless "Interaction 1" and line2 did
                    // not surface at all, so the way up and the way down were
                    // between them invisible and unlabelled.
                    //
                    // WHICH IS WHICH, established 2026-08-01:
                    //   line1 centre (-2150,-197)  z = -68/-55   -> DOWN
                    //   line2 centre (-2276, 269)  z = +352/+391 -> UP
                    // Two independent confirmations. (a) All three floor changes
                    // in the .301 BAT fired from (-2400,-470), south of and just
                    // past line1, floor decreasing each time. (b) Aaron crossed
                    // the NORTH line and the game answered "(No sense going back
                    // up.)" -- so north is the up staircase, story-gated at that
                    // point in the plot.
                    //
                    // The rule is the Z one: of the self-destination lines on the
                    // field, the higher-Z one goes UP. That is the discriminator I
                    // first tried on INF gateways in .298 and had to withdraw --
                    // gateway lineZ is (0,0) everywhere. SETLINE data is the
                    // opposite: the heights are real and ~430 units apart here,
                    // about one floor. Same idea, correct data source.
                    //
                    // Deliberately NOT keyed on line index or SYM ('squall' and
                    // 'zell' here, which mean nothing) so it carries to gpbig2a
                    // and the other shaft screens on its own.
                    {
                        uint16_t curFid = FF8Addresses::pCurrentFieldId
                                          ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                        if (s_capturedLines[t].destFieldId == (int)curFid) {
                            if (!IsPrisonShaftFieldId(curFid)) continue;  // bed etc: unchanged
                            // isSelfLoopStair already set above (v0.18.3.303).
                        }
                    }
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;

                    // Reachability: trigger center must not be separated from player
                    // by any other active screen-boundary trigger line.
                    //
                    // v0.18.3.302 (#91 R1/R4): stairs are EXEMPT. gpbig1a's west
                    // wall carries five lines within ~500 units of each other, so
                    // whichever one the player is not next to reads as "separated"
                    // by its neighbours -- which is exactly how line2 disappeared
                    // while line1 survived. This separation test has now produced
                    // a false positive in four different blocks (v0.17.8.10
                    // gateways, v0.18.3.268 interactions, v0.18.3.278 exits, #94
                    // events); a cluster of parallel lines on one wall is its worst
                    // case. Losing the only way off a floor is far worse than
                    // listing a staircase the player has to walk around to, so the
                    // stairs skip the test and the would-be filter is logged.
                    //
                    // v0.18.3.278: BOUNDED segment test, matching the v0.17.8.10
                    // gateway fix and the v0.18.3.268 interaction fix. This block
                    // was the last user of the infinite-line side test, which
                    // extends every screen-bound line forever, so an exit could be
                    // "separated" by a short line that does not lie between it and
                    // the player. On glfurin1 that made the Mansion 4 exit
                    // (line1, centre 146,500) appear and vanish as the player moved
                    // -- present at 19:01:26, gone at 19:01:33 -- because line0's
                    // infinite extension flipped sides. Also skips testing a line
                    // against itself.
                    {
                        bool exitCrossed = false;
                        for (int dt = 0; dt < s_capturedLineCount && !exitCrossed; dt++) {
                            if (isSelfLoopStair) break;  // exempt -- see above
                            if (!s_capturedLines[dt].active) continue;
                            if (dt == t) continue;
                            if (s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                                s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_UNKNOWN)
                                continue;
                            if (SegmentsCross(scrPlayerX, scrPlayerY, tcx, tcy,
                                              (float)s_capturedLines[dt].x1, (float)s_capturedLines[dt].y1,
                                              (float)s_capturedLines[dt].x2, (float)s_capturedLines[dt].y2)) {
                                exitCrossed = true;
                                Log::Field("FieldNavigation: [refresh] exit line%d center=(%.0f,%.0f) "
                                           "filtered: path crosses screen-bound line%d", t, tcx, tcy, dt);
                            }
                        }
                        if (exitCrossed) continue;
                    }

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
                    if (isSelfLoopStair) {
                        // v0.18.3.302 (#91 R1): name the staircase by DIRECTION.
                        //
                        // Of the self-destination lines on this field, the one
                        // with the greater mean Z goes UP. Evidence and rationale
                        // are in the self-loop block above; the short version is
                        // that SETLINE carries real heights (unlike INF gateways,
                        // whose Z is zeroed) and gpbig1a's two stairs sit ~430
                        // units apart vertically, about one floor.
                        float myZ = (float)(s_capturedLines[t].z1 + s_capturedLines[t].z2) / 2.0f;
                        int   higher = 0, lower = 0;
                        for (int st = 0; st < s_capturedLineCount; st++) {
                            if (st == t || !s_capturedLines[st].active) continue;
                            if (s_capturedLines[st].lineType !=
                                FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                            if (s_capturedLines[st].destFieldId != s_capturedLines[t].destFieldId)
                                continue;
                            float oz = (float)(s_capturedLines[st].z1 + s_capturedLines[st].z2) / 2.0f;
                            if (oz > myZ) higher++; else if (oz < myZ) lower++;
                        }
                        // Only claim a direction when there IS a counterpart to be
                        // higher or lower than. A lone self-destination line, or a
                        // tie, gets the honest unqualified label rather than a
                        // coin-flip -- announcing "Stairs up" at the down stairs
                        // would be worse than saying nothing about direction.
                        const char* dir = nullptr;
                        if (higher > 0 && lower == 0)      dir = "down";
                        else if (lower > 0 && higher == 0) dir = "up";

                        int floorNow = ReadShaftFloor();
                        if (dir && floorNow > 0) {
                            int dest = (dir[0] == 'u') ? floorNow + 1 : floorNow - 1;
                            if (dest > 0)
                                snprintf(exitName, sizeof(exitName),
                                         "Stairs %s to Floor %d", dir, dest);
                            else
                                snprintf(exitName, sizeof(exitName), "Stairs %s", dir);
                        } else if (dir) {
                            snprintf(exitName, sizeof(exitName), "Stairs %s", dir);
                        } else {
                            strncpy(exitName, "Stairs", sizeof(exitName) - 1);
                        }
                        Log::Field("FieldNavigation: [refresh] STAIRS line%d center=(%.0f,%.0f) "
                                   "meanZ=%.0f higher=%d lower=%d floor=%d -> '%s' "
                                   "[v0.18.3.302 #91 R1]",
                                   t, tcx, tcy, myZ, higher, lower, floorNow, exitName);
                    } else if (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) {
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
