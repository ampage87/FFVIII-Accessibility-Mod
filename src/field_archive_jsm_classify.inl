// ============================================================================
// field_archive_jsm_classify.inl — per-entity type classification + post-passes
// ============================================================================
// v0.18.3.294: extracted VERBATIM from field_archive_jsm_scan.inl to get that
// file back under the CI source-file size ceiling
// (.github/workflows/safety-checks.yml: soft warn > 60 KB, HARD FAIL > 80 KB).
// The scanner had been sitting ~700 bytes under the hard fail, which blocked
// the #85 state-gating work outright -- see GitHub #37.
//
// Same textual-fragment pattern used throughout this codebase
// (field_archive_jsm_director.inl, field_nav_catalog_mapexits.inl, ...):
// this is NOT a standalone function. It is a fragment of ScanJSMScripts()'s
// per-entity loop body, #included inline at the point where the block used to
// sit, so it operates directly on that loop's locals -- including `info`, `e`,
// the whole found*/has* opcode-signal set, and the s_* cross-pass arrays from
// field_archive_jsm_state.inl.
//
// It sits INSIDE the `for (int e = ...)` loop but is itself BRACE-BALANCED:
// it does not open or close the loop. `outCount++;` and the loop's closing
// brace remain in the parent, immediately after the #include.
//
// PURE TEXTUAL MOVE — no logic change whatsoever. Byte-for-byte the same
// statements in the same order. If a BAT after this split behaves differently
// from the one before it, the split is the suspect, not the game.
// ============================================================================

        // --- Classify entity type based on found opcodes ---
        // Priority: most specific first.
        if (foundSetDrawpoint || foundDrawpoint) {
            info.type = JSM_ENT_DRAW_POINT;
            info.param = drawpointId;
        } else if (foundMenusave || foundSaveenable) {
            info.type = JSM_ENT_SAVE_POINT;
        } else if (foundMenushop) {
            info.type = JSM_ENT_SHOP;
            info.param = shopId;
        } else if (foundCardgame) {
            info.type = JSM_ENT_CARD_GAME;
        } else if (foundLadder) {
            info.type = JSM_ENT_LADDER;
        } else if (foundMapjump) {
            info.type = JSM_ENT_MAP_EXIT;
            info.param = mapjumpDestField;
        } else if (foundSetmodel && foundTalkon) {
            info.type = JSM_ENT_NPC;
        } else if (foundDoorline && info.jsmCategory == 0) {
            info.type = JSM_ENT_DOOR;  // keep as door
        }
        // Otherwise, keep the default from JSM category assignment above.

        // v0.07.82 / v0.17.7.1 / v0.17.7.1.1: Classify Line entities by opcode signatures.
        //
        // v0.17.7.1.1 reverts the v0.17.7.1 TALK-setup gating that regressed
        // dorm bed Interactions. Background: dormitory beds use SETLINE + dialog
        // opcodes; the engine fires the "Sleep?" prompt when the player crosses
        // the line. They do NOT use TALKRADIUS/TALKON, so the v0.17.7.1 rule
        // "INTERACTIVE only if dialog + TALK setup" demoted them to EVENT or
        // SCREEN_BOUND. The actual fepic1 fix (which v0.17.7.1 was supposed to
        // ship) was the catalog's field-wide `fieldHasInteractiveObjects`
        // demote removal -- that's preserved here, so fepic1 still works.
        //
        // Priority restored to v0.12.24-era rule: dialog wins first.
        //   1. dialog opcodes  -> INTERACTIVE  (covers SETLINE+MES walk-through beds AND TALKRADIUS-press signs)
        //   2. MAPJUMP w/o dialog -> SCREEN_BOUND (pure screen exits like fepic1's three)
        //   3. battle/event w/o dialog -> EVENT
        //   4. BGDRAW/SCROLL only -> CAMERA_PAN
        //   5. nothing recognisable -> CAMERA_PAN  (silent default)
        //
        // The `hasTalkSetup` field on JSMEntityInfo is still populated below
        // and remains available for future fixes that need to distinguish
        // confirm-press interactions from walk-across triggers; we just don't
        // gate the LINE_INTERACTIVE classification on it any more.
        if (info.jsmCategory == 1) {
            if (foundDialogOp) {
                info.type = JSM_ENT_LINE_INTERACTIVE;
            } else if (foundMapjump) {
                info.type = JSM_ENT_LINE_SCREEN_BOUND;
            } else if (foundBattle) {
                info.type = JSM_ENT_LINE_EVENT;
            } else if (foundBgdraw || foundScroll) {
                info.type = JSM_ENT_LINE_CAMERA_PAN;
            } else if (foundExtDispatch) {
                // v0.17.8.6: Runtime-0x1C-dispatched interactive line.
                // The B-Garden dorm bed (bgryo2_1 ent0 'squall') reaches its
                // "I should get some rest" AASK prompt through a runtime-supplied
                // 0x1C dispatch (a bare EXT_DISPATCH whose sub-opcode index is
                // provided at runtime, logged "0x1C EMPTY STACK: ent=0 method=1").
                // The static scan provably cannot resolve that to a dialog opcode,
                // so foundDialogOp is false and the line would otherwise fall to
                // LINE_EVENT, which the catalog hides -- making the bed impossible
                // for a blind player to FIND before crossing it.
                //
                // extDisp (the entity's own 0x1C usage) is the SAME interactivity
                // proxy the cat2/3 JSM_ENT_INTERACTIVE_OBJECT promotion already
                // relies on (it surfaces the B-Garden Directory before you touch
                // it). Treating Line entities symmetrically pre-detects the bed at
                // field load. Surfaced at its SETLINE center by catalog Block 3.
                //
                // Ordering matters: this sits AFTER mapjump (screen exits),
                // battle (battle triggers), and bgdraw/scroll (camera-pan lines
                // whose 0x1C drives the scroll, not a dialog), so those keep
                // their existing classification. Only a Line whose 0x1C is NOT
                // any of those reaches here.
                //
                // Over-surfacing tradeoff: a Line whose 0x1C only fires a sound
                // or particle effect (no dialog) surfaces as a phantom
                // "Interaction". That is the unavoidable cost of pre-detecting
                // without being able to read the dialog statically; the runtime
                // dialog-confirmation + disk-persistence layer (v0.17.8.7) is
                // what prunes/labels these once the player reaches them.
                info.type = JSM_ENT_LINE_INTERACTIVE;
            } else if (foundEventOp) {
                info.type = JSM_ENT_LINE_EVENT;
            } else {
                info.type = JSM_ENT_LINE_CAMERA_PAN;
            }
        }

        // v0.17.8.8: Save-line detection, signal (a) -- own-script save.
        // The Line-classification block just above reclassifies EVERY Line
        // entity to a LINE_* type, which discards the JSM_ENT_SAVE_POINT the
        // type cascade would otherwise assign to a Line whose own script
        // invokes the save menu (MENUSAVE/SAVEENABLE/PHSENABLE). Preserve that
        // signal on a side flag the catalog can read, so the surfaced
        // Interaction can be labelled "Save Point" instead of "Interaction N".
        // (foundSaveenable also covers PHSENABLE -- see the opcode scan.)
        //
        // Two detection paths feed this:
        //   * foundMenusave/foundSaveenable -- save opcode resolved through a
        //     0x1C dispatch (works when the dispatch index is a readable
        //     literal/PSHM the scan can follow).
        //   * ownSaveConst -- the save opcode CONSTANTS appear as literal pushes
        //     in the line's own bytecode. bghall_1 'selphie' dispatches save via
        //     a runtime-supplied 0x1C (empty-stack, like the dorm bed) so the
        //     resolved flags never fire, but it literally pushes 0x12F
        //     (SAVEENABLE) and 0x130 (PHSENABLE). Requiring MENUSAVE alone, or
        //     SAVEENABLE+PHSENABLE together, keeps it tight -- the sibling
        //     control line 'zells' has neither and is unaffected.
        bool ownSaveConst = sawLitMenusave || (sawLitSaveenable && sawLitPhsenable);
        if (info.jsmCategory == 1 && (foundMenusave || foundSaveenable || ownSaveConst)) {
            info.isSaveLine = true;
            // Ensure the line actually surfaces in the catalog. A pure save
            // line might have no dialog/extDispatch and would otherwise fall to
            // CAMERA_PAN (hidden); force INTERACTIVE so it appears (and gets
            // relabelled to Save Point by the catalog).
            if (info.type != JSM_ENT_LINE_INTERACTIVE)
                info.type = JSM_ENT_LINE_INTERACTIVE;
            Log::Field("FieldArchive: [JSMScan] save-line(own): Line ent%d '%s' "
                       "invokes save menu -> isSaveLine=1 (resolved=%d const=%d) [v0.17.8.8]",
                       e, info.symName,
                       (foundMenusave || foundSaveenable) ? 1 : 0, ownSaveConst ? 1 : 0);
        }

        // v0.12.24: Store ext dispatch flag for dual-purpose Line detection.
        info.hasExtDispatch = foundExtDispatch;

        // v0.17.7.1: Talk-setup flag for catalog walkmesh exclusion rule.
        // True when the script uses TALKRADIUS or TALKON, indicating the player
        // can interact via confirm-press (vs. crossing a Line trigger).
        info.hasTalkSetup = foundTalkradius || foundTalkon;

        // v0.18.3.274: publish the SETLINE data onto the entity.
        //
        // The scanner has always detected SETLINE, captured its literal
        // coordinates, and LOGGED them ("[JSMScan] entN 'sym' SETLINE: ..."),
        // but never wrote any of it back to JSMEntityInfo. So
        // JSMEntityInfo::hasSetline / setlineX1..Z2 -- declared since v0.12.16 --
        // were permanently false/zero: dead fields that looked populated.
        //
        // Found while debugging the v0.18.3.273 captured-line -> JSM-entity
        // mapping, which keys off hasSetline: every field reported
        // "0 SETLINE owners", so that mapping silently never engaged and always
        // fell back to the legacy doors+t rule. Any other consumer of these
        // fields was equally reading zeros.
        info.hasSetline = foundSetline;
        info.setlineX1  = setlineX1;  info.setlineY1 = setlineY1;  info.setlineZ1 = setlineZ1;
        info.setlineX2  = setlineX2;  info.setlineY2 = setlineY2;  info.setlineZ2 = setlineZ2;

        // v0.17.8.15: Export the behavior signal the catalog dedupe pass uses
        // to distinguish raw-SYM Others-with-models (NPCs) from raw-SYM
        // walk-across Lines (Interactions). Replaces v0.17.8.11's setmodelSlot
        // + chara.one cross-reference. The persistent s_hasSetmodelInit[]
        // array below still tracks the same signal for the Director-detection
        // post-pass; this field exposes it on the per-entity export used by
        // field_nav_catalog_dedupe.inl.
        info.hasSetmodelInit = foundSetmodelInit;

        // v0.12.20: Store persistent flags for Director/interaction detection.
        if (e < 128) {
            s_hasSetmodelInit[e] = foundSetmodelInit;
            s_hasDialogAny[e] = foundDialogOp;
            s_hasExtDispatchArr[e] = foundExtDispatch;
        }

        // v0.07.84: REQ-following post-classification.
        // If this entity is still unclassified (or just "background/unknown")
        // and it calls REQ/REQSW/REQEW to a method that contains MAPJUMP,
        // classify it as MAP_EXIT with that destination.
        if ((info.type == JSM_ENT_UNKNOWN || info.type == JSM_ENT_BACKGROUND ||
             info.type == JSM_ENT_NPC) && e < 128 && info.jsmCategory == 3) {
            for (int r = 0; r < s_entityReqs[e].count; r++) {
                int tgtEnt  = s_entityReqs[e].calls[r].targetEntity;
                int tgtMeth = s_entityReqs[e].calls[r].targetMethod;
                if (tgtEnt < 0 || tgtEnt >= totalEntities) continue;
                // Convert entity-relative method index to global method index.
                // Method 0 = init, method 1 = first interaction, etc.
                int globalMethIdx = groups[tgtEnt].startMethodIdx + tgtMeth;
                if (globalMethIdx < 0 || globalMethIdx >= MAX_METHOD_MAPJUMPS) continue;
                if (s_methodMapjumps[globalMethIdx].found) {
                    info.type = JSM_ENT_MAP_EXIT;
                    info.param = s_methodMapjumps[globalMethIdx].destFieldId;
                    Log::Field("FieldArchive: [JSMScan] REQ-follow: ent%d '%s' -> ent%d method%d has MAPJUMP dest=%d",
                               e, info.symName, tgtEnt, tgtMeth, info.param);
                    break;
                }
            }
        }

        // v0.07.87: Variable-dispatch exit detection.
        // If this "Other" entity writes to a memory address (POPM_W) that a
        // MAPJUMP-containing method also reads (PSHM_W), this entity likely
        // sets a dispatch variable that triggers a map transition in the
        // Director entity's script loop. Classify as MAP_EXIT.
        // v0.07.88: Filter out very low memory addresses (0-7) — these are
        // scratch/temp variables used by virtually every entity (e.g. loop
        // counters, temp flags) and produce massive false positive rates.
        // Real dispatch variables use higher addresses.
        static const int32_t VAR_DISPATCH_MIN_ADDR = 8;
        if ((info.type == JSM_ENT_UNKNOWN || info.type == JSM_ENT_BACKGROUND ||
             info.type == JSM_ENT_NPC) && e < 128 && info.jsmCategory == 3 &&
            s_entityPopms[e].count > 0) {
            for (int p = 0; p < s_entityPopms[e].count; p++) {
                int32_t writeAddr = s_entityPopms[e].addrs[p];
                if (writeAddr < VAR_DISPATCH_MIN_ADDR) continue;  // skip scratch vars
                bool matched = false;
                int matchDest = -1;
                for (int mi = 0; mi < totalMethods && mi < MAX_METHOD_MAPJUMPS && !matched; mi++) {
                    if (!s_methodMapjumps[mi].found) continue;
                    for (int r = 0; r < s_methodMapjumps[mi].pshmCount; r++) {
                        if (s_methodMapjumps[mi].pshmAddrs[r] == writeAddr) {
                            matched = true;
                            matchDest = s_methodMapjumps[mi].destFieldId;
                            break;
                        }
                    }
                }
                if (matched) {
                    info.type = JSM_ENT_MAP_EXIT;
                    info.param = matchDest;
                    Log::Field("FieldArchive: [JSMScan] var-dispatch: ent%d '%s' writes addr %d "
                               "-> matches MAPJUMP method dest=%d",
                               e, info.symName, (int)writeAddr, matchDest);
                    break;
                }
            }
        }

        // v0.07.72: SYM-name fallback classification.
        // Extended opcodes (MENUSAVE, DRAWPOINT, etc.) are dispatched via 0x1C,
        // and save/draw point entities often push the dispatch index from a
        // runtime memory variable (PSHM_W), not a literal. Our scanner can't
        // know the runtime value, so opcode-based classification fails.
        // Fall back to SYM naming conventions for unclassified entities.
        if (info.type == JSM_ENT_UNKNOWN || info.type == JSM_ENT_BACKGROUND) {
            int symIdx2 = e - countDoors;
            if (symIdx2 >= 0 && symIdx2 < symCount) {
                const char* sn = symNames[symIdx2];
                // FF8 uses consistent SYM naming: "savePoint", "svpt", "dp01", etc.
                if (_strnicmp(sn, "save", 4) == 0 || _strnicmp(sn, "svpt", 4) == 0) {
                    info.type = JSM_ENT_SAVE_POINT;
                } else if ((_strnicmp(sn, "dp", 2) == 0 && (sn[2] >= '0' && sn[2] <= '9')) ||
                           _strnicmp(sn, "drpoint", 7) == 0 ||
                           _strnicmp(sn, "drawpoint", 9) == 0 ||
                           _strnicmp(sn, "draw_point", 10) == 0) {
                    info.type = JSM_ENT_DRAW_POINT;
                    // drawpointId remains -1 (unknown from static scan)
                } else if (_strnicmp(sn, "shop", 4) == 0) {
                    info.type = JSM_ENT_SHOP;
                }
            }
        }

        // v0.07.98: Interactive object detection for unclassified entities with dialog.
        // Entities (background OR invisible others) with dialog opcodes (MES/ASK/AMES/AASK)
        // and a position from SET3/SET are interactive objects the player can examine
        // (B-Garden Directory, classroom terminals, beds, desks, bulletin boards).
        // Promoted to JSM_ENT_INTERACTIVE_OBJECT for catalog injection.
        // Uses foundDialogOp (not foundEventOp) to avoid false positives on
        // lighting/animation controllers that use SHOW/HIDE but no dialog.
        // Covers both Background (cat=2) and Other (cat=3) entities that remain
        // unclassified after all prior classification passes.
        // foundDialogOp catches literal MES/ASK pushes; foundExtDispatch catches
        // runtime-dispatched extended opcodes (0x1C with PSHM_W or empty stack)
        // which commonly include MES/ASK for interactive objects like the Directory.
        // v0.07.99: Also accept hasPshmCoords — entity has SET3 but coordinates
        // are from runtime memory. Classification is correct; catalog injection
        // still requires hasPosition for navigable coordinates.
        if ((info.type == JSM_ENT_BACKGROUND || info.type == JSM_ENT_UNKNOWN) &&
            (foundDialogOp || foundExtDispatch) &&
            (info.hasPosition || info.hasPshmCoords) && !foundSetmodel) {
            // v0.17.8.7: skip debug leftovers (e.g. bgroad_5 / bghall_1
            // 'cardgamemaster') that would otherwise surface as phantoms.
            if (EntityIsDebugLeftover(e, info.symName)) {
                Log::Field("FieldArchive: [JSMScan] ent%d '%s' NOT promoted to "
                           "INTERACTIVE_OBJECT: debug leftover "
                           "(cardgamemaster/test-battle, v0.17.8.7)", e, info.symName);
            } else {
                info.type = JSM_ENT_INTERACTIVE_OBJECT;
            }
        }

        // v0.08.01: Paired entity position inheritance.
        // FF8 uses a pattern where a positioning entity (SET3 with PSHM_W coords)
        // is placed immediately before a dialog entity (0x1C extended dispatch with
        // MES/ASK) in the JSM entity table. Example: bghall_1 ent24 'dic' (position)
        // + ent25 'igyous1' (dialog). Neither passes interactive object detection
        // alone. When a dialog entity has no position at all, check if the
        // immediately preceding "Other" entity has PSHM_W coordinates and inherit them.
        // v0.08.04: Paired inheritance with targeted light-entity filter.
        // foundExtDispatch is needed because igyous1 (Directory dialog) uses 0x1C
        // dispatch, not literal MES/ASK. But lighting controllers (displight,
        // cornerlight, sidelight) also use 0x1C via stairlight inheritance.
        // Fix: allow foundExtDispatch but skip entities whose SYM name contains "light".
        if ((info.type == JSM_ENT_BACKGROUND || info.type == JSM_ENT_UNKNOWN) &&
            (foundDialogOp || foundExtDispatch) && !foundSetmodel &&
            !info.hasPosition && !info.hasPshmCoords &&
            outCount > 0 &&
            !strstr(info.symName, "light") &&
            !EntityIsDebugLeftover(e, info.symName)) {  // v0.17.8.7: skip debug leftovers here too
            JSMEntityInfo& prev = outEntities[outCount - 1];
            if (prev.hasPshmCoords && prev.jsmIndex == e - 1 &&
                (prev.jsmCategory == 3 || prev.jsmCategory == 2) &&
                prev.type != JSM_ENT_NPC && prev.type != JSM_ENT_MAP_EXIT) {
                // Inherit PSHM coordinates from the positioning entity.
                info.hasPshmCoords = true;
                info.pshmAddrX = prev.pshmAddrX;
                info.pshmAddrY = prev.pshmAddrY;
                info.pshmAddrZ = prev.pshmAddrZ;
                info.posTriangle = prev.posTriangle;
                info.posX = prev.posX;
                info.posY = prev.posY;
                info.posZ = prev.posZ;
                // v0.08.13: Also inherit hasPosition if the positioning entity
                // resolved its coordinates via the shift-pattern passthrough.
                if (prev.hasPosition) info.hasPosition = true;
                info.type = JSM_ENT_INTERACTIVE_OBJECT;
                Log::Field("FieldArchive: [JSMScan] paired-entity: ent%d '%s' inherits PSHM coords "
                           "from ent%d '%s' (addrX=%d addrY=%d addrZ=%d tri=%u)",
                           e, info.symName, prev.jsmIndex, prev.symName,
                           (int)info.pshmAddrX, (int)info.pshmAddrY, (int)info.pshmAddrZ,
                           (unsigned)info.posTriangle);
            }
        }

        // v0.12.20: Store persistent per-entity flags for Director detection post-pass.
        if (e < 128) {
            s_hasSetmodelInit[e] = foundSetmodelInit;
            s_hasDialogAny[e] = foundDialogOp;
            s_hasExtDispatchArr[e] = foundExtDispatch;
        }
