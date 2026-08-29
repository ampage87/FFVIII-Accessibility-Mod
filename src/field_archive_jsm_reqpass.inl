// field_archive_jsm_reqpass.inl -- REQ cross-reference post-passes.
//
// A statement fragment #included at ONE point inside ScanJSMScripts (the same
// pattern as field_archive_jsm_classify.inl). It runs after every entity has
// been scanned, so s_methodMapjumps / s_entityReqs / s_isReqTarget are complete
// for the whole field -- which is the entire point: these rules cross-reference
// entities against each other and cannot work entity-by-entity. Extracted from
// field_archive_jsm_scan.inl in v0.62.0 to keep that file under the 80 KB CI
// size gate. Do not compile independently; do not include anywhere else.
    // v0.12.09: Draw point trigger cross-reference.
    // For each entity that calls REQSW/REQEW to a draw point entity,
    // mark it as a draw point trigger. This deterministically links the
    // visible interaction entity (with talkonoff/model) to the invisible
    // draw point script entity (with DRAWPOINT opcode).
    for (int e2 = 0; e2 < outCount; e2++) {
        outEntities[e2].drawPointTriggerOf = -1;  // initialize
        int jsmIdx = outEntities[e2].jsmIndex;
        if (jsmIdx >= 128) continue;
        for (int r = 0; r < s_entityReqs[jsmIdx].count; r++) {
            int tgtEnt = s_entityReqs[jsmIdx].calls[r].targetEntity;
            // Find the target entity in our output array.
            for (int t = 0; t < outCount; t++) {
                if (outEntities[t].jsmIndex == tgtEnt &&
                    outEntities[t].type == JSM_ENT_DRAW_POINT) {
                    outEntities[e2].drawPointTriggerOf = tgtEnt;
                    Log::Field("FieldArchive: [JSMScan] Draw point trigger: ent%d '%s' "
                               "calls draw point ent%d '%s'",
                               jsmIdx, outEntities[e2].symName,
                               tgtEnt, outEntities[t].symName);
                    break;
                }
            }
            if (outEntities[e2].drawPointTriggerOf >= 0) break;
        }
    }

    // v0.19.7 (#5): propagate the field-wide REQ-target set onto each output
    // entity. s_isReqTarget[] was filled during the per-entity opcode scan from
    // every REQ/REQSW/REQEW inline param, so it is only complete now that the
    // whole field has been scanned. The director junk-gate (consumer) reads
    // je.isReqTarget to decide whether a director-promoted Object has any
    // interaction path at all.
    for (int ri = 0; ri < outCount; ri++) {
        int rji = outEntities[ri].jsmIndex;
        outEntities[ri].isReqTarget = (rji >= 0 && rji < 128) ? s_isReqTarget[rji] : false;
    }

