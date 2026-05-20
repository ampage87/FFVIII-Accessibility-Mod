// field_archive_jsm_director.inl — Director-dispatched interaction detection.
// Included from field_archive_jsm.inl. Do not compile independently.
//
// v0.16.3 split: this code was previously inline inside ScanJSMScripts(),
// running after the draw-point trigger cross-reference and before the
// disabled-diagnostic / Line REQ-following blocks. It has been extracted
// verbatim into RunDirectorDetection() so the scan file fits comfortably
// under the 60 KB CI warn threshold. Behavior is unchanged.
//
// Static state read here (declared in field_archive_jsm_state.inl):
//   s_reqOpcodeCount, s_entityReqs, s_hasSetmodelInit,
//   s_hasDialogAny,   s_hasExtDispatchArr, s_initVarMaps

// ======================================================================
// v0.12.20: Director-dispatched interaction detection
// ======================================================================
//
// A Director is an invisible Others entity (no SETMODEL in init) that
// dispatches interactions via REQ calls to dialog target entities. The
// pattern shows up on dormitory and classroom fields (bed, desk, wardrobe,
// blackboard, etc.) where one parent script triggers many child entities.
//
// This function:
//   1. Emits a DIAGNOSTIC log line for every Others entity with REQ/dialog
//      flags set (regardless of whether it ends up flagged as Director).
//   2. Identifies Directors and promotes their Other dispatch targets to
//      JSM_ENT_INTERACTIVE_OBJECT, with party-character filtering.
//   3. Dumps Background and Director entity scripts for offline analysis.
static void RunDirectorDetection(const char* fieldName,
                                 JSMEntityInfo* outEntities,
                                 int outCount,
                                 int countDoors,
                                 int countLines,
                                 int countBg,
                                 char symNames[][32],
                                 int symCount)
{
    // DIAGNOSTIC: Log all Others entities' Director-relevant flags before filtering.
    {
        for (int dd = 0; dd < outCount && dd < 128; dd++) {
            const JSMEntityInfo& de2 = outEntities[dd];
            int di2 = de2.jsmIndex;
            if (di2 < 0 || di2 >= 128) continue;
            if (de2.jsmCategory != 3) continue;  // Others only
            if (s_reqOpcodeCount[di2] == 0 && s_entityReqs[di2].count == 0 &&
                !s_hasDialogAny[di2] && !s_hasExtDispatchArr[di2]) continue;
            // Log REQ targets for entities with REQs
            char reqBuf[256] = {};
            int rp = 0;
            for (int rr = 0; rr < s_entityReqs[di2].count && rp < 240; rr++)
                rp += snprintf(reqBuf + rp, 256 - rp, "ent%d.m%d ",
                               s_entityReqs[di2].calls[rr].targetEntity,
                               s_entityReqs[di2].calls[rr].targetMethod);
            Log::Field("FieldArchive: [DIR-DIAG] ent%d '%s' type=%s setmodelInit=%d "
                       "dialog=%d extDisp=%d reqOps=%d reqResolved=%d initVars=%d reqs=[%s]",
                       di2, de2.symName, JSMEntityTypeName(de2.type),
                       (int)s_hasSetmodelInit[di2],
                       (int)s_hasDialogAny[di2], (int)s_hasExtDispatchArr[di2],
                       s_reqOpcodeCount[di2], s_entityReqs[di2].count,
                       s_initVarMaps[di2].count, reqBuf);
        }
    }

    // Director detection post-pass
    // A Director is an invisible Others entity (no SETMODEL in init) that
    // dispatches interactions via REQ calls to dialog target entities.
    {
        int directorsFound = 0;
        int targetsPromoted = 0;
        for (int de = 0; de < outCount && de < 128; de++) {
            JSMEntityInfo& dirEnt = outEntities[de];
            int dIdx = dirEnt.jsmIndex;
            if (dIdx < 0 || dIdx >= 128) continue;

            // Director criteria:
            //   1. Cat=3 (Others)
            //   2. No SETMODEL in init (invisible)
            //   3. Still unclassified (UNKNOWN or NPC without specific role)
            //   4. Has >= 2 REQ calls to other entities
            //   5. At least 2 distinct REQ targets have dialog or EXT_DISPATCH
            if (dirEnt.jsmCategory != 3) continue;
            if (s_hasSetmodelInit[dIdx]) continue;
            if (dirEnt.type != JSM_ENT_UNKNOWN && dirEnt.type != JSM_ENT_NPC) continue;
            // v0.12.20: Use REQ opcode count (stack-independent) instead of
            // s_entityReqs which requires pushCount>=3 and often fails.
            if (s_reqOpcodeCount[dIdx] < 2) continue;

            // Count potential dispatch targets: Other entities on this field
            // with extDispatch or dialog, no SETMODEL in init, not this entity.
            // We can't rely on parsed REQ target IDs (stack simulation too weak),
            // so we use a heuristic: nearby entities with dialog capability.
            int dialogTargetCount = 0;
            for (int tc = 0; tc < outCount && tc < 128; tc++) {
                int tci = outEntities[tc].jsmIndex;
                if (tci == dIdx || tci < 0 || tci >= 128) continue;
                if (outEntities[tc].jsmCategory != 3) continue;
                if (!s_hasDialogAny[tci] && !s_hasExtDispatchArr[tci]) continue;
                dialogTargetCount++;
            }
            if (dialogTargetCount < 2) continue;

            // === This entity is a Director ===
            dirEnt.type = JSM_ENT_DIRECTOR;
            directorsFound++;

            // v0.12.23: Also dump Background entities on this field.
            // Deep research suggests interaction zones may be in Background
            // entity init scripts (SETLINE/SET3/TALKRADIUS), not Others.
            //
            // v0.17.7.2: Gated behind FF8OPC_VERBOSE_JSM. The unguarded dump
            // was generating thousands of log lines per field (bghall_1 has
            // 6 Background entities; each Director re-dumps them all, and
            // bghall_1 has 3 Directors -> 18 full BG dumps). In v0.17.7.1.2
            // BAT logs this caused field_scripts_init's post-init block to
            // be cut off entirely when the player moved to another field
            // before logging finished. Production builds skip the dump.
#ifdef FF8OPC_VERBOSE_JSM
            for (int bg = countDoors + countLines; bg < countDoors + countLines + countBg; bg++) {
                int bgSymIdx = bg - countDoors;
                const char* bgSym = (bgSymIdx >= 0 && bgSymIdx < symCount) ? symNames[bgSymIdx] : "?";
                Log::Field("FieldArchive: [DIRECTOR]   dumping Background entity %d '%s'", bg, bgSym);
                DumpEntityScript(fieldName, bg);
            }
#endif

            Log::Field("FieldArchive: [DIRECTOR] Detected: ent%d '%s' on '%s' — "
                       "%d REQ opcodes, %d dialog targets, %d init vars",
                       dIdx, dirEnt.symName, fieldName,
                       s_reqOpcodeCount[dIdx], dialogTargetCount,
                       s_initVarMaps[dIdx].count);

            // Log init variable map for diagnostics and future position extraction.
            // These are PUSH literal + POPM_W pairs from the Director's init method.
            // Interaction zone X/Y coordinates are stored at these addresses.
            for (int v = 0; v < s_initVarMaps[dIdx].count && v < 20; v++) {
                Log::Field("FieldArchive: [DIRECTOR]   initVar[%d] addr=%d value=%d",
                           v, (int)s_initVarMaps[dIdx].writes[v].addr,
                           (int)s_initVarMaps[dIdx].writes[v].value);
            }

            // v0.12.20: Dump Director's full decoded script for position pattern analysis.
            // v0.17.7.2: Gated -- displight on bghall_1 has 34 methods (~3000 dwords),
            // dumping it three times (one per Director) blew up the log past the
            // point where the field_scripts_init post-init block could complete
            // before the player changed fields.
#ifdef FF8OPC_VERBOSE_JSM
            DumpEntityScript(fieldName, dIdx);
#endif

            // Promote each potential dispatch target to INTERACTIVE_OBJECT.
            // Since REQ target IDs aren't reliably parsed, we promote all
            // non-SETMODEL Others with dialog/extDispatch on this field.
            for (int tc = 0; tc < outCount && tc < 128; tc++) {
                int tgt = outEntities[tc].jsmIndex;
                if (tgt == dIdx || tgt < 0 || tgt >= 128) continue;
                if (outEntities[tc].jsmCategory != 3) continue;
                if (!s_hasDialogAny[tgt] && !s_hasExtDispatchArr[tgt]) continue;

                // Get target SYM name
                int tgtSymIdx = tgt - countDoors;
                const char* tgtSym = (tgtSymIdx >= 0 && tgtSymIdx < symCount)
                                     ? symNames[tgtSymIdx] : "?";

                // v0.12.22: Filter out party character names to reduce false promotions.
                // Party characters are Director dispatch targets for party-related interactions
                // (e.g. "talk to Selphie") but are NOT background interactive objects.
                if (_strnicmp(tgtSym, "squall", 6) == 0 ||
                    _strnicmp(tgtSym, "zell", 4) == 0 ||
                    _strnicmp(tgtSym, "selphie", 7) == 0 ||
                    _strnicmp(tgtSym, "quistis", 7) == 0 ||
                    _strnicmp(tgtSym, "rinoa", 5) == 0 ||
                    _strnicmp(tgtSym, "irvine", 6) == 0 ||
                    _strnicmp(tgtSym, "seifer", 6) == 0 ||
                    _strnicmp(tgtSym, "edea", 4) == 0 ||
                    _strnicmp(tgtSym, "laguna", 6) == 0 ||
                    _strnicmp(tgtSym, "kiros", 5) == 0 ||
                    _strnicmp(tgtSym, "ward", 4) == 0) {
                    continue;  // skip party character
                }

                // Skip if already classified as something useful
                JSMEntityType tType = outEntities[tc].type;
                if (tType == JSM_ENT_INTERACTIVE_OBJECT ||
                    tType == JSM_ENT_DRAW_POINT ||
                    tType == JSM_ENT_SAVE_POINT ||
                    tType == JSM_ENT_SHOP ||
                    tType == JSM_ENT_MAP_EXIT ||
                    tType == JSM_ENT_DIRECTOR) {
                    continue;
                }

                // Promote target to INTERACTIVE_OBJECT
                const char* oldType = JSMEntityTypeName(outEntities[tc].type);
                outEntities[tc].type = JSM_ENT_INTERACTIVE_OBJECT;
                targetsPromoted++;
                Log::Field("FieldArchive: [DIRECTOR]   promoted ent%d '%s' %s -> Interactive Object "
                           "(pos=%s %d,%d)",
                           tgt, tgtSym, oldType,
                           outEntities[tc].hasPosition ? "YES" : "no",
                           (int)outEntities[tc].posX,
                           (int)outEntities[tc].posY);
                // v0.12.22: Dump init script for unpositioned targets to verify
                // whether they contain SETLINE/SET3/TALKRADIUS literals.
                // Deep research suggests coordinates should be here.
                // v0.17.7.2: Gated -- same log-explosion problem; dumps fire per
                // promoted target per Director, easily 18+ on hub fields. The
                // bghall_1 BAT showed seito4's dump appearing here even with the
                // other three gates in place.
#ifdef FF8OPC_VERBOSE_JSM
                if (!outEntities[tc].hasPosition) {
                    DumpEntityScript(fieldName, tgt);
                }
#endif
            }
        }
        if (directorsFound > 0) {
            Log::Field("FieldArchive: [DIRECTOR] '%s': %d Directors detected, %d targets promoted",
                       fieldName, directorsFound, targetsPromoted);
        }
    }
}
