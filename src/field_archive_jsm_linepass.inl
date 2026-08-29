// field_archive_jsm_linepass.inl -- Line-entity REQ-following post-pass.
//
// A statement fragment #included at ONE point inside ScanJSMScripts, AFTER
// MapjumpResolver::Run -- it reads types the resolver may have changed, so the
// ordering is load-bearing. Extracted from field_archive_jsm_scan.inl in
// v0.62.0 for the 80 KB CI size gate. Do not compile independently; do not
// include anywhere else.

    // v0.62.0 (#123): REQ-following map exits, AFTER the mapjump resolver.
    //
    // Aaron, blocked at the end of the Lunar Base: "you have to go down a lift
    // to proceed but auto-drive failed to get me there. There was no specific
    // elevator or lift in the catalog." There was one -- "Exit to Lunar Base -
    // Pod 1" -- but it was attributed to `director1`, a script object with no
    // position, so the catalog held an exit at (0,0) and navigation answered
    // "Target not yet located."
    //
    // The thing the player steps on is `ele`, the red platform at (-30,30); its
    // script REQs director1's method 48, and THAT is where the MAPJUMP3 to field
    // 870 lives. Attributing the exit to the entity that triggers it gives the
    // exit a position for free, because that entity is a real object standing
    // somewhere.
    //
    // This rule existed since v0.07.84 and had never once fired as intended: it
    // ran inside the per-entity scan loop, so it could only see methods already
    // scanned, and directors are declared last. Add that s_entityReqs was empty
    // on every field (see the REQ recording in the scan) and the path was dead.
    //
    // v0.62.1: it runs HERE, after MapjumpResolver::Run, rather than before it.
    // The destination and its provenance both belong to the entity that owns the
    // MAPJUMP, and only the resolver knows them for certain -- on sscont2 it
    // resolves director1's destination through the variable block and marks it
    // INTERP. Copying the raw scan value instead left `ele` carrying a
    // destination the INF-gateway filter then rejected as probably-stale, and
    // the exit vanished from the catalog a second time: "Exit to the pod did not
    // show in the catalog in this test."
    for (int e2 = 0; e2 < outCount; e2++) {
        JSMEntityInfo& en = outEntities[e2];
        if (en.jsmCategory != 3) continue;
        // Only an entity the classifier had NOTHING to say about. The v0.07.84
        // rule also allowed NPC and BACKGROUND, but an entity that talks is an
        // NPC first and foremost -- retyping `Ghei1` on bcsaka1a from a person
        // you can speak to into a door loses more than it gains, and the exit it
        // triggers is a cutscene, not a way through.
        if (en.type != JSM_ENT_UNKNOWN) continue;
        int e3 = en.jsmIndex;
        if (e3 < 0 || e3 >= 128) continue;
        for (int r = 0; r < s_entityReqs[e3].count; r++) {
            int tgtEnt  = s_entityReqs[e3].calls[r].targetEntity;
            int tgtMeth = s_entityReqs[e3].calls[r].targetMethod;
            if (tgtEnt < 0 || tgtEnt >= totalEntities) continue;
            if (tgtMeth < groups[tgtEnt].startMethodIdx ||
                tgtMeth > groups[tgtEnt].startMethodIdx + groups[tgtEnt].methodCount) continue;
            if (tgtMeth < 0 || tgtMeth >= MAX_METHOD_MAPJUMPS) continue;
            if (!s_methodMapjumps[tgtMeth].found) continue;
            en.type  = JSM_ENT_MAP_EXIT;
            en.exitFromReqFollow = true;   // v0.63.0
            en.param = s_methodMapjumps[tgtMeth].destFieldId;
            // Take the OWNER's destination and provenance when it has them: the
            // resolver has already run and its answer is the authoritative one.
            for (int t3 = 0; t3 < outCount; t3++) {
                if (outEntities[t3].jsmIndex != tgtEnt) continue;
                if (outEntities[t3].type == JSM_ENT_MAP_EXIT) {
                    en.param           = outEntities[t3].param;
                    en.paramFromInterp = outEntities[t3].paramFromInterp;
                    // The script that CONTAINS the jump is the mechanism, not
                    // the door. With no position of its own it can only ever be
                    // catalogued at (0,0), which is what put an unreachable
                    // "Exit to Lunar Base - Pod 1" in front of Aaron. Now that
                    // the exit has a real position on the entity the player
                    // touches, retire the copy that has none.
                    if (!outEntities[t3].hasPosition) {
                        outEntities[t3].type = JSM_ENT_UNKNOWN;
                        Log::Field("FieldArchive: [JSMScan] ...and ent%d '%s', which holds the "
                                   "MAPJUMP but no position, is retired in its favour [v0.62.0]",
                                   tgtEnt, outEntities[t3].symName);
                    }
                }
                break;
            }
            // v0.62.2 (#123): the STORY GATE. Decode the guard the triggering
            // method opens with, if it has the canonical shape
            //   PSHM_B/W/L <addr> ; PSHN_L <literal> ; OP <cmp> ; JMPZ <n>
            // 55 of the REQ-follow triggers on the disc carry one, almost all of
            // them on var[256] -- FF8's story-progress word. The catalog reads the
            // live value and drops the exit while the guard is false, which is why
            // the Lunar Base pod stops being offered before you have spoken to
            // Ellone. Anything that is not exactly this shape gets no gate and the
            // exit behaves as it did.
            {
                int sm = s_entityReqs[e3].calls[r].srcMethod;
                if (!en.hasGate && sm >= 0 && sm < totalMethods) {
                    int ip0 = (int)(entryPoints[sm] & 0x7FFF);
                    if (ip0 >= 0 && ip0 < scriptDataDwords) {
                        JsmGate g = JsmDecodeGate(&scriptData[ip0],
                                                  (int)(scriptDataDwords - ip0));
                        if (g.ok && s_entityReqs[e3].calls[r].srcRel < g.skipTo) {
                            en.hasGate   = true;
                            en.gateAddr  = g.addr;
                            en.gateWidth = g.width;
                            en.gateOp    = g.op;
                            en.gateValue = g.value;
                            Log::Field("FieldArchive: [JSMScan] ...gated on var[%d] (%dB) "
                                       "op%d %d [v0.62.2]", en.gateAddr, (int)g.width,
                                       (int)en.gateOp, en.gateValue);
                        }
                    }
                }
            }
            Log::Field("FieldArchive: [JSMScan] REQ-follow: ent%d '%s' triggers ent%d "
                       "method%d, which MAPJUMPs to %d (interp=%d) -- it IS the exit [v0.62.0]",
                       e3, en.symName, tgtEnt, tgtMeth, en.param,
                       en.paramFromInterp ? 1 : 0);
            break;
        }
    }

    // v0.12.24 / v0.17.7.5.4: REQ-following for Line entity interaction detection.
    // If a Line entity REQs another entity that has dialog opcodes or ext dispatch,
    // the Line is dual-purpose (exit + interaction). Mark it with hasDialogReqTarget
    // so the catalog can distinguish this from the (much more common) case where
    // a Line uses extended dispatch in its OWN script for non-dialog purposes
    // (sound, particle effects, animation). v0.17.7.5.4 split the previous unified
    // hasExtDispatch flag into two: hasExtDispatch (own 0x1C usage, set in opcode
    // scan) and hasDialogReqTarget (dialog REQ target, set HERE). The catalog uses
    // hasDialogReqTarget for the dual-purpose check.
    //
    // The previous `if (outEntities[i].hasExtDispatch) continue;` early-exit was
    // dropped: we now run REQ-following for ALL Line entities regardless of own
    // ext-dispatch usage, so lines like bgroad_5 squalls (own 0x1C true, REQ
    // target dialog false) get correctly classified as pure exits.
    for (int i = 0; i < outCount; i++) {
        if (outEntities[i].jsmCategory != 1) continue;  // Line entities only
        int e = outEntities[i].jsmIndex;
        if (e >= 128) continue;
        // Check if this Line entity REQs any entity with dialog/ext dispatch.
        // First try resolved REQ targets.
        bool reqsInteractive = false;
        for (int r = 0; r < s_entityReqs[e].count && !reqsInteractive; r++) {
            int tgt = s_entityReqs[e].calls[r].targetEntity;
            if (tgt >= 0 && tgt < 128) {
                if (s_hasDialogAny[tgt] || s_hasExtDispatchArr[tgt])
                    reqsInteractive = true;
            }
        }
        // Fallback: if entity has unresolved REQ opcodes (stack lost track),
        // check if ANY Interactive Object entity exists on this field.
        // Interactive Objects are specifically the targets of dual-purpose Line
        // entity interactions (dormitory bed/desk/wardrobe, etc.).
        if (!reqsInteractive && s_reqOpcodeCount[e] > 0 && s_entityReqs[e].count == 0) {
            for (int ii = 0; ii < outCount && !reqsInteractive; ii++) {
                if (outEntities[ii].type == JSM_ENT_INTERACTIVE_OBJECT)
                    reqsInteractive = true;
            }
        }
        if (reqsInteractive) {
            outEntities[i].hasDialogReqTarget = true;
            Log::Field("FieldArchive: [JSMScan] REQ-interact: Line ent%d '%s' REQs interactive entity -> hasDialogReqTarget=1",
                       e, outEntities[i].symName);
            // v0.61.0: and that makes it an INTERACTION, not an event.
            //
            // The Line classification cascade sends a line with no dialog of its
            // own but with REQ opcodes to LINE_EVENT, which the catalog treats as
            // transparent and never surfaces. Until v0.59.0 that almost never
            // happened, because `foundExtDispatch` caught these first -- but that
            // flag fired on opcode 0x1C, which occurs in 92.8% of all entities, and
            // v0.59.0 removed it once the decode showed 0x1C is a no-op. Removing a
            // wrong signal without replacing what it was standing in for is a
            // regression, and this is the one it caused: on the Lunar Base control
            // room (sscont1) the moon-scope terminal is the line `moonscope`, whose
            // own script says nothing and whose touchOn method REQs six entities.
            // Aaron: "There is also a control panel you are supposed to look at /
            // operate and I couldn't find it."
            //
            // hasDialogReqTarget is the exact form of what extDispatch was guessing
            // at -- REQ-following found this line dispatches to an entity that
            // actually talks. LINE_CAMERA_PAN is deliberately left alone: a line
            // that drives BGDRAW/scroll is doing something visual.
            if (outEntities[i].type == JSM_ENT_LINE_EVENT) {
                outEntities[i].type = JSM_ENT_LINE_INTERACTIVE;
                Log::Field("FieldArchive: [JSMScan] Line ent%d '%s' promoted "
                           "LINE_EVENT -> LINE_INTERACTIVE: it dispatches to an entity "
                           "that talks [v0.61.0]", e, outEntities[i].symName);
            }
        }

        // v0.62.0 (#123): A LINE THE FIELD SWITCHES OFF AND SOMEBODY SWITCHES ON.
        //
        // v0.61.0 restored the moon-scope terminal by promoting on
        // hasDialogReqTarget -- but s_entityReqs was empty on every field then
        // (see the REQ recording above), so what actually fired was the "does
        // this field contain ANY Interactive Object" fallback beside it. With
        // REQ targets resolving properly that coincidence is gone, and
        // `moonscope` REQs six entities of which none has a single dialog
        // opcode. The promotion was right; the reason given for it was not.
        //
        // The real signal is in the line's own script. `moonscope` calls LINEOFF
        // in its init and LINEON in a method that `irvine`, `selphie` and
        // `quistis` each REQ from a method the SYM names `moon_lineon0` --
        // talking to any of them arms the terminal. Both halves are load-time
        // facts: the init disables the line, and another entity holds a
        // reference to it. A passive cutscene tripwire has neither. 86 lines on
        // the disc carry both, among them the Missile Base valves (bgmd1_4
        // 'Valve1'/'Valve2') and the Galbadia Garden hall lines.
        if (outEntities[i].type == JSM_ENT_LINE_EVENT &&
            e < 128 && s_lineInitOff[e] && s_isReqTarget[e]) {
            outEntities[i].type = JSM_ENT_LINE_INTERACTIVE;
            Log::Field("FieldArchive: [JSMScan] Line ent%d '%s' promoted "
                       "LINE_EVENT -> LINE_INTERACTIVE: disabled by its own init "
                       "and switched on from another script [v0.62.0]",
                       e, outEntities[i].symName);
        }

        // v0.17.8.8: Save-line detection, signal (b) -- REQ to a save point.
        // A Line that REQs an entity classified SAVE_POINT (or with a save*/svpt
        // SYM name, in case that entity was classified MAP_EXIT because its
        // script also contains a MAPJUMP -- e.g. bghall_1 'saveline0') is the
        // walk-on trigger that opens the save menu. Flag the Line so the catalog
        // labels it "Save Point". The Line already has a position (its SETLINE
        // center), so no save-point positioning is needed.
        if (!outEntities[i].isSaveLine) {
            for (int r = 0; r < s_entityReqs[e].count && !outEntities[i].isSaveLine; r++) {
                int tgt = s_entityReqs[e].calls[r].targetEntity;
                for (int t2 = 0; t2 < outCount; t2++) {
                    if (outEntities[t2].jsmIndex != tgt) continue;
                    bool tgtIsSave = (outEntities[t2].type == JSM_ENT_SAVE_POINT) ||
                                     (_strnicmp(outEntities[t2].symName, "save", 4) == 0) ||
                                     (_strnicmp(outEntities[t2].symName, "svpt", 4) == 0);
                    if (tgtIsSave) {
                        outEntities[i].isSaveLine = true;
                        outEntities[i].hasDialogReqTarget = true;  // ensure it surfaces
                        Log::Field("FieldArchive: [JSMScan] save-line(req): Line ent%d '%s' "
                                   "REQs save point ent%d '%s' -> isSaveLine=1 [v0.17.8.8]",
                                   e, outEntities[i].symName, tgt, outEntities[t2].symName);
                        break;
                    }
                }
            }
        }
    }


    // ------------------------------------------------------------------
    // v0.111.0 (#dsrc): THE SAME RULE, FOR A LINE YOU WALK ONTO.
    // ------------------------------------------------------------------
    // Aaron, descending the Deep Sea Research Center: "The hatchways down are
    // either not showing up in the catalog or auto-drive is not approaching them
    // correctly." They were not showing up -- not on ONE of the five tower
    // floors -- and the reason is that the v0.62.0 REQ-follow above only ever
    // looked at category 3.
    //
    // Every descent in that tower is one idiom. `Sitahe` -- shita-e, "downward"
    // -- is a LINE entity whose touch script is four instructions long:
    //
    //     PSHM_B var[614] ; PSHN_L 8 ; OPER >= ; JPF +6
    //     PSHN_L 1 ; PSHN_L 8 ; REQEW 0            ; -> Squall::test
    //
    // and `Squall::test` is where the MAPJUMP to the next floor lives. The
    // catalog found that MAPJUMP, attributed it to Squall, and dropped it under
    // the v0.62.0 party-member rule -- correctly on its own terms. A party
    // member is not a door. **The door is the line.** `Uehe` (ue-e, "upward")
    // is the same thing pointing the other way, and ddtower1 through ddtower5
    // and ddruins6 all use it.
    //
    // TWO THINGS MAKE THIS A SEPARATE LOOP RATHER THAN A WIDENED GUARD ABOVE.
    //
    // One: it must run AFTER the dialog-REQ pass. Folded into the loop above it
    // fired first, and the disc-wide scanner diff showed it stealing nine lines
    // that pass would have made INTERACTIVE -- `Eventline`, `mapjumpline`,
    // `Jumpline1`. A line the player talks to is a conversation first; that is
    // the same argument the category-3 rule makes about NPCs, and it is only
    // enforceable once that pass has had its say.
    //
    // Two: a category-3 promotion becomes a MAP_EXIT and then needs a position
    // of its own, which is what put an exit at (0,0) in front of Aaron on the
    // Lunar Base. A LINE already has one -- its own trigger line, which the
    // catalog turns into an ENT_EXIT at the line's centre. So the promotion here
    // is to SCREEN_BOUND, exactly the type the classifier gives a line that
    // carries its own MAPJUMP. Same destination, same catalog path, same
    // navigation, and nothing new to position.
    for (int e4 = 0; e4 < outCount; e4++) {
        JSMEntityInfo& en = outEntities[e4];
        // Only the two line types the catalog has nothing to say about, and
        // only once the passes above have had their say. See
        // jsm_line_exit_model.inl.
        if (!JsmLineExitEligible(en.jsmCategory, (int)en.type,
                                 (int)JSM_ENT_LINE_EVENT, (int)JSM_ENT_LINE_CAMERA_PAN,
                                 en.hasDialogReqTarget, en.isSaveLine)) continue;
        int e5 = en.jsmIndex;
        if (e5 < 0 || e5 >= 128) continue;
        for (int r = 0; r < s_entityReqs[e5].count; r++) {
            int tgtEnt  = s_entityReqs[e5].calls[r].targetEntity;
            int tgtMeth = s_entityReqs[e5].calls[r].targetMethod;
            if (tgtEnt < 0 || tgtEnt >= totalEntities) continue;
            if (tgtMeth < groups[tgtEnt].startMethodIdx ||
                tgtMeth > groups[tgtEnt].startMethodIdx + groups[tgtEnt].methodCount) continue;
            if (tgtMeth < 0 || tgtMeth >= MAX_METHOD_MAPJUMPS) continue;

            // ONE MORE HOP, AND ONLY ONE. `Uehe` -- ue-e, "upward" -- is
            // ddtower1's way back to the Research Center core, and it does not
            // reach the jump in a single step: its touch REQs `Squall::ue`,
            // which walks the party to the lift and REQs `Director::checker`,
            // and THAT is where the MAPJUMP3 to sdcore2 lives. ddtower1 is the
            // one floor in the tower with no INF gateway at all, so without
            // this the room has no way out in either direction.
            //
            // Depth two, hard-stopped. Every step widens what can be called a
            // door, and the disc-wide scanner golden is what says whether a step
            // was worth taking -- depth two moves four entities across 866
            // fields, all of them lifts and hatches.
            int jumpMeth = tgtMeth;
            int hopEnt   = tgtEnt;
            if (!s_methodMapjumps[jumpMeth].found) {
                bool hopped = false;
                if (tgtEnt >= 0 && tgtEnt < 128) {
                    for (int h = 0; h < s_entityReqs[tgtEnt].count && !hopped; h++) {
                        if (s_entityReqs[tgtEnt].calls[h].srcMethod != tgtMeth) continue;
                        int m2 = s_entityReqs[tgtEnt].calls[h].targetMethod;
                        int e2b = s_entityReqs[tgtEnt].calls[h].targetEntity;
                        if (m2 < 0 || m2 >= MAX_METHOD_MAPJUMPS) continue;
                        if (!s_methodMapjumps[m2].found) continue;
                        jumpMeth = m2;
                        hopEnt   = e2b;
                        hopped   = true;
                    }
                }
                if (!hopped) continue;
            }
            int dest = s_methodMapjumps[jumpMeth].destFieldId;
            bool interp = false;
            // The owner's resolved destination wins, for the reason v0.62.1
            // records: only MapjumpResolver::Run knows a destination that came
            // out of the variable block.
            for (int t4 = 0; t4 < outCount; t4++) {
                if (outEntities[t4].jsmIndex != hopEnt) continue;
                if ((outEntities[t4].type == JSM_ENT_MAP_EXIT ||
                     outEntities[t4].type == JSM_ENT_LINE_SCREEN_BOUND) &&
                    JsmOwnerDestWins(outEntities[t4].param, dest)) {
                    dest   = outEntities[t4].param;
                    interp = outEntities[t4].paramFromInterp;
                }
                break;
            }
            if (!JsmDestIsPlausibleField(dest)) continue;
            en.type             = JSM_ENT_LINE_SCREEN_BOUND;
            en.param            = dest;
            en.paramFromInterp  = interp;
            en.exitFromReqFollow = true;
            // The story gate, decoded from the guard the touch script opens with
            // -- `Sitahe` on every tower floor is gated on var[614], the DSRC's
            // own progress word, which is why the hatch is shut until the story
            // has opened it.
            {
                int sm = s_entityReqs[e5].calls[r].srcMethod;
                if (!en.hasGate && sm >= 0 && sm < totalMethods) {
                    int ip0 = (int)(entryPoints[sm] & 0x7FFF);
                    if (ip0 >= 0 && ip0 < scriptDataDwords) {
                        JsmGate g = JsmDecodeGate(&scriptData[ip0],
                                                  (int)(scriptDataDwords - ip0));
                        if (g.ok && s_entityReqs[e5].calls[r].srcRel < g.skipTo) {
                            en.hasGate   = true;
                            en.gateAddr  = g.addr;
                            en.gateWidth = g.width;
                            en.gateOp    = g.op;
                            en.gateValue = g.value;
                        }
                    }
                }
            }
            Log::Field("FieldArchive: [JSMScan] REQ-follow(line): line ent%d '%s' triggers "
                       "ent%d method%d, which MAPJUMPs to %d (interp=%d gate=%d) -- the LINE "
                       "is the door [v0.111.0]",
                       e5, en.symName, tgtEnt, tgtMeth, en.param,
                       en.paramFromInterp ? 1 : 0, en.hasGate ? 1 : 0);
            break;
        }
    }

    // ------------------------------------------------------------------
    // v0.114.0 (#dsrc): THE GATE ON AN INTERACTION LINE.
    // ------------------------------------------------------------------
    // Aaron, on Level 3: "when I tried to interact with the steam room terminal
    // nothing seemed to happen." Nothing did. `Tanme2::touch` opens
    //
    //     PSHM_B var[603] ; PSHN_L 2 ; OPER >= ; JPF +27
    //
    // and everything the terminal does is on the far side of that test, behind
    // two more (var[602] == 4 and var[615] == 12). With the guard false the
    // script runs four instructions and returns, which from the player's side
    // is silence -- and silence is indistinguishable from a broken mod.
    //
    // v0.62.2 already reads exactly this shape for an EXIT and drops the exit
    // while it is false, which is how the Lunar Base pod stops being offered
    // before you have spoken to Ellone. An interaction line got no such reading,
    // because `hasGate` was only ever filled in by the REQ-follow above.
    //
    // Dropping is the wrong answer here: a terminal that is shut is still a
    // terminal, and a blind player needs to know it EXISTS and is not ready --
    // not to have it vanish. So this only decodes the gate; field_catalog.inl
    // decides what to say about it.
    //
    // Which method carries the guard is not fixed, so every method is tried and
    // the first clean decode wins. In practice only the interaction method has
    // one: `default`, `talk`, `push` and `across` on these lines are two to five
    // instructions long and cannot match the shape.
    for (int e6 = 0; e6 < outCount; e6++) {
        JSMEntityInfo& en = outEntities[e6];
        if (en.jsmCategory != 1) continue;
        if (en.hasGate) continue;                       // the REQ-follow already spoke
        if (en.type == JSM_ENT_LINE_SCREEN_BOUND) continue;  // an exit, handled above
        int e7 = en.jsmIndex;
        if (e7 < 0 || e7 >= totalEntities) continue;
        for (int m2 = 0; m2 < groups[e7].methodCount; m2++) {
            int sm = groups[e7].startMethodIdx + m2;
            if (sm < 0 || sm >= totalMethods) continue;
            int ip0 = (int)(entryPoints[sm] & 0x7FFF);
            if (ip0 < 0 || ip0 >= scriptDataDwords) continue;
            JsmGate g = JsmDecodeGate(&scriptData[ip0], (int)(scriptDataDwords - ip0));
            if (!g.ok) continue;
            en.hasGate   = true;
            en.gateAddr  = g.addr;
            en.gateWidth = g.width;
            en.gateOp    = g.op;
            en.gateValue = g.value;
            Log::Field("FieldArchive: [JSMScan] line ent%d '%s' method%d is gated on "
                       "var[%d] (%dB) op%d %d [v0.114.0]",
                       e7, en.symName, m2, en.gateAddr, (int)g.width,
                       (int)en.gateOp, en.gateValue);
            break;
        }
    }
