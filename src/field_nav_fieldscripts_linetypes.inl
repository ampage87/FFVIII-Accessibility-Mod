// ============================================================================
// field_nav_fieldscripts_linetypes.inl - captured trigger line classification
// ============================================================================
// v0.18.3.280: extracted verbatim from field_nav_fieldscripts.inl to bring that
// file back under the CI source-file size ceiling
// (.github/workflows/safety-checks.yml: soft warn > 60 KB, HARD FAIL > 80 KB).
// It had reached 81.7 KB across the #83/#84 work and CI refused the push.
//
// Same pattern as field_nav_catalog_mapexits.inl (v0.18.3.266) and
// field_nav_catalog_gateways.inl (v0.18.3.276): this is NOT a standalone
// function. It is a fragment of HookedFieldScriptsInit()'s body, #included
// inline where the block used to sit, so it operates directly on that
// function's locals and the file-scope nav state:
//
//   fieldName / fieldId          - field being loaded
//   s_capturedLines[] / s_capturedLineCount
//   s_jsmEntities[] / s_jsmEntityCount
//   s_jsmDoors / s_jsmLines / s_jsmBackgrounds / s_jsmOthers
//   s_initVarMaps[] / s_initVarMapCount
//
// Behaviour is byte-for-byte identical to the pre-extraction code; this was a
// pure textual move with no logic change. Verified balanced (braces 0,
// parens 0) before and after the move.
//
// What it does: assigns a lineType to every captured SETLINE trigger line from
// the JSM classification (the v0.18.3.277 #84 fix -- captured line t maps to
// JSM entity t, NOT jsmDoors + t), resolves each screen-bound line's
// destination field, and emits the [LINE-PAIR] / [LINEDIAG] / MAPJUMP-resolver
// diagnostics that made the Caraway's Mansion exit bugs traceable.
// ============================================================================

            // v0.07.82: Assign lineType to captured trigger lines from JSM classification.
            // SETLINE fires during field_scripts_init for each Line entity (and possibly
            // Door entities). Captured lines are ordered by lineOrder (SETLINE call order).
            // JSM Line entities are at indices [countDoors .. countDoors+countLines-1].
            // We match by assuming captured lines arrive in JSM entity order.
            // If captured count <= jsmLines, direct map. If more, excess are from doors.
            {
                // Sort captured lines by lineOrder to establish a stable mapping.
                // (They should already be in order, but be safe.)
                for (int a = 0; a < s_capturedLineCount - 1; a++) {
                    for (int b = a + 1; b < s_capturedLineCount; b++) {
                        if (s_capturedLines[b].lineOrder < s_capturedLines[a].lineOrder) {
                            CapturedTriggerLine tmp = s_capturedLines[a];
                            s_capturedLines[a] = s_capturedLines[b];
                            s_capturedLines[b] = tmp;
                        }
                    }
                }
                // v0.18.3.275: the v0.18.3.273/.274 "SETLINE-owner" mapping is
                // REMOVED -- it never ran. It keyed off JSMEntityInfo::hasSetline,
                // but the scanner's static SETLINE detection does not fire on any
                // field (zero "[JSMScan] ... SETLINE:" lines across a full session),
                // so every field logged "0 SETLINE owners" and silently fell back
                // to the legacy rule. Dead code that looks active is worse than no
                // code, so it's gone rather than left in place.
                //
                // Engine facts established while chasing it (kept so this isn't
                // re-derived): the SETLINE opcode handler is at 0x0051DC30
                // (opcode table 0x00B8DE94, entry [0x39]). It pops six values off
                // the script stack and writes them to its object at +0x188..+0x193,
                // then sets +0x194 = 1 as a "has line" flag. That layout collides
                // with the field-entity struct (which stores 32-bit position at
                // +0x190/+0x194), and the captured-line addresses are 0x1A0 apart
                // rather than the 0x264 entity stride -- so LINE objects live in
                // their own ~416-byte-stride array, separate from the "others"
                // entity array.
                //
                // Which means captured line t really does correspond to line-array
                // index t, and therefore to JSM Line entity doors+t: the legacy
                // rule below is structurally right, and the earlier "wrong entity
                // block" theory was wrong. The glfurin1 misassignment (line0 given
                // dest=724 while it actually goes to 725) must therefore originate
                // in the JSM header's category counts or in destination resolution
                // for those Line entities -- not in line indexing.
                //
                // The pairing dump below exists to settle exactly that in one run.

                int linesMapped = 0;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    s_capturedLines[t].lineType = FieldArchive::JSM_ENT_UNKNOWN;
                    s_capturedLines[t].destFieldId = -1;
                    s_capturedLines[t].hasExtDispatch = false;
                    s_capturedLines[t].hasDialogReqTarget = false;  // v0.17.7.5.4
                    s_capturedLines[t].isCameraTransition = false;  // v0.20.29
                    // v0.18.3.277 (#84): captured line t maps to JSM entity t --
                    // DOORS PARTICIPATE in the captured-line sequence, so the old
                    // "jsmDoors + t" over-shifted by the door count.
                    //
                    // Evidence on glfurin1 (jsmDoors=1), where every prediction of
                    // this rule matches what the player actually experiences:
                    //   line0 (-592,247)  -> jsm0 = DOOR, varblock 0x2D5 = 725  -> Mansion 5  (correct)
                    //   line1 (146,500)   -> jsm1, param 724                    -> Mansion 4  (correct)
                    //   line2 (-694,-254) -> jsm2, type 13 interactive          -> the Glass  (correct)
                    // Under jsmDoors+t, line0 got jsm1's 724 ("Mansion 4") while
                    // actually going to 725, and the line that really goes to 724
                    // got no destination and fell through to "Interaction".
                    //
                    // Why this never showed up before: every field BAT'd during the
                    // exit work (all of B-Garden) logs jsmDoors=0, where t and
                    // jsmDoors+t are identical. glfurin1 is the first door-bearing
                    // field examined, so only it exposed the shift.
                    //
                    // A Door entity carrying a MAPJUMP behaves as a screen-boundary
                    // exit line, so its MAP_EXIT type is normalised to
                    // LINE_SCREEN_BOUND; otherwise the catalog's line-exit path
                    // (which keys off LINE_SCREEN_BOUND) would skip it.
                    int jsmIdx = t;
                    if (jsmIdx < s_jsmEntityCount &&
                        (s_jsmEntities[jsmIdx].jsmCategory == 0 ||    // Door
                         s_jsmEntities[jsmIdx].jsmCategory == 1)) {   // Line
                        // v0.18.3.277: a Door carrying a MAPJUMP is, for catalog
                        // purposes, a screen-boundary exit line -- normalise its
                        // MAP_EXIT type so the line-exit path recognises it.
                        FieldArchive::JSMEntityType mappedType = s_jsmEntities[jsmIdx].type;
                        if (s_jsmEntities[jsmIdx].jsmCategory == 0 &&
                            mappedType == FieldArchive::JSM_ENT_MAP_EXIT)
                            mappedType = FieldArchive::JSM_ENT_LINE_SCREEN_BOUND;
                        s_capturedLines[t].lineType = mappedType;
                        s_capturedLines[t].hasExtDispatch = s_jsmEntities[jsmIdx].hasExtDispatch;
                        // v0.17.7.5.4: Copy the dialog-REQ-target signal too. This is
                        // what the catalog now uses to decide if a SCREEN_BOUND line
                        // is dual-purpose (exit-via-interaction) vs. a pure walk-across
                        // exit. hasExtDispatch alone is too noisy (fires on any 0x1C use).
                        s_capturedLines[t].hasDialogReqTarget = s_jsmEntities[jsmIdx].hasDialogReqTarget;
                        // v0.20.29: camera-view transition lines follow FF8's "*jump*"
                        // naming (validated across all 45 multi-camera fields). Route
                        // them as SCREEN_BOUND so they wall the zone BFS and reach the
                        // exit path, and tag them for a "Camera transition" label.
                        if (s_jsmEntities[jsmIdx].jsmCategory == 1 &&
                            (strstr(s_jsmEntities[jsmIdx].symName, "jump") ||
                             strstr(s_jsmEntities[jsmIdx].symName, "Jump"))) {
                            s_capturedLines[t].lineType = FieldArchive::JSM_ENT_LINE_SCREEN_BOUND;
                            s_capturedLines[t].isCameraTransition = true;
                        }
                        // v0.07.83 / v0.17.7.1.2: Capture MAPJUMP destination for screen boundary lines.
                        //
                        // The JSM scanner sets info.param to either:
                        //   * a literal field ID 0..981             (script pushed PSHN_L FieldID)
                        //   * a PSHM_W marker 0x80000000 | addr     (script pushed PSHM_W field-var)
                        //   * a negative literal e.g. -2 World Map  (PSHM_W passthrough, opcParam<0)
                        //   * a small negative on resolution failure
                        //
                        // v0.17.7.1.2 adds PSHM marker resolution: when bit 31
                        // is set, the low 16 bits encode the field-var address
                        // in the varblock. By the time this block runs, the
                        // engine has already executed s_originalFieldScriptsInit
                        // (above) so the varblock at 0x1CFE9B8 is populated.
                        // Read the 16-bit field ID from varblock[addr] and use
                        // that as the resolved destination.
                        //
                        // Why this matters for B-Garden hall fields: their exits
                        // (Cafeteria, Dormitories, Parking Lot) are emitted by
                        // the engine as MAPJUMP <PSHM_W varAddr>, with the
                        // varAddr indexing into a per-field destination table
                        // populated by the engine during init. Static JSM scan
                        // sees only the marker; INF gateways (v0.17.7.1.1's
                        // first attempted fix) hold vestigial PS1 placeholder
                        // destFieldIds for these fields; the runtime varblock
                        // is the only authoritative source.
                        //
                        // PSHSM_W (special memory, opcode 0x0C) also produces
                        // a marker via the same scanner branch but reads from
                        // a different base. Handled together here -- if the
                        // 0x1CFE9B8 read produces an out-of-range value, we
                        // leave info.param as-is and the catalog falls back to
                        // bare "Exit".
                        // v0.18.3.277: test the NORMALISED type, so a Door whose
                        // MAP_EXIT was mapped to LINE_SCREEN_BOUND above also has
                        // its destination captured (glfurin1 jsm0 -> 725).
                        if (mappedType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                            int rawParam = s_jsmEntities[jsmIdx].param;
                            if ((unsigned)rawParam & 0x80000000u) {
                                uint16_t pshmAddr = (uint16_t)(rawParam & 0xFFFF);
                                // v0.17.7.5.3: addr-as-literal. Empirically across
                                // 8 BAT fires on B-Garden hall fields, SCREEN_BOUND
                                // lines whose static resolver returns VARBLOCK <addr>
                                // have engine destField == addr (in decimal):
                                //   bghall_2 squallsd  0x00A5 -> 165 (Hall 1)
                                //   bghall_2 zell      0x00B9 -> 185 (Quad 4)
                                //   bghall_2 zells     0x00E3 -> 227 (Hallway 4)
                                //   bghall_5 selphie   0x00E0 -> 224 (Hallway 1)
                                //   bghall_5 irvine    0x00AA -> 170 (Hall 6)
                                //   bghall_5 zell      0x00E4 -> 228 (Hallway 5)  [predicted]
                                //   bghall_5 zells     0x00E1 -> 225 (Hallway 2)  [predicted]
                                //   bgroad_1 squall    0x009A -> 154 (Cafeteria 1)
                                //
                                // Mechanism (best-current-understanding): the B-Garden
                                // script authors chose pshmAddr = destField for ease
                                // of reading; the varblock at byte-offset addr holds
                                // value=addr at method-7 execution time (some setup
                                // we haven't located populates it between field-load
                                // and the line's MAPJUMP3 firing). We can't read it
                                // at field-load time because at that lifecycle point
                                // the varblock isn't yet populated -- prior v0.17.7.x
                                // builds read varblock here and got either 0 (kept
                                // marker, line stayed bare) or wrong values that
                                // didn't match the engine's actual destField (e.g.
                                // bghall_2 zell varblock[0xB9]=255 at field load but
                                // engine destField=185, mismatching the v0.17.7.4 BAT).
                                //
                                // The addr-as-literal interpretation works whether the
                                // pattern is intentional self-documenting bytecode
                                // (most likely) or coincidental (engine populates
                                // varblock[X] = X for some init range we haven't
                                // identified). Either way the labeling comes out
                                // correct on every BAT'd traversal.
                                //
                                // Caveats:
                                //  * If a future field uses PSHM_W with addr that
                                //    ISN'T the destField (a truly dynamic varblock-
                                //    driven destination), this will mislabel it.
                                //    No such case is known but it can't be ruled
                                //    out for non-B-Garden fields.
                                //  * bghall_1 has 3 SCREEN_BOUND lines all picking
                                //    addr=0x00AF (=175=Hall 11). After this fix
                                //    they all label as "Exit to Hall 11". If that's
                                //    wrong, we'll catch it in catalog testing.
                                // v0.18.3.290 (#85): the caveat documented above
                                // ("If a future field uses PSHM_W with addr that
                                // ISN'T the destField ... this will mislabel it")
                                // has now actually fired. glwater3 (Deling sewer)
                                // ent7 'hasigomodel' carries marker 0x8000011B;
                                // addr 0x011B = 283 happens to be a valid field id
                                // -- "Centra Ruins 8" -- so the catalog served
                                // Aaron a confident, completely fabricated 4th exit
                                // to a location on the other side of the world.
                                // He confirmed this field has only 3 real exits
                                // (two north, one east); the other three label
                                // correctly and are unaffected by this change.
                                //
                                // The whole addr-as-literal equivalence was derived
                                // from 8 BAT fires, ALL on B-Garden 'bg*' fields,
                                // and is explicitly a best-guess about that
                                // scripting convention -- there was never evidence
                                // it generalises. Scope it to the evidence:
                                // non-'bg' fields keep the marker and fall back to
                                // a bare "Exit" (honest and unlabeled) instead of
                                // inventing a destination. If a non-bg field later
                                // turns out to need this, widen it deliberately
                                // with its own BAT rather than by default.
                                bool isBgField291 = (fieldName &&
                                    (fieldName[0] == 'b' || fieldName[0] == 'B') &&
                                    (fieldName[1] == 'g' || fieldName[1] == 'G'));
                                // v0.18.3.291 (#85): the flat bg-only scoping above
                                // was too blunt and caused a REGRESSION in the .290
                                // BAT. glwater3 has TWO marker-bearing exit lines:
                                //   jsm2 'irvine'      addr 0x02FA = 762 = Sewer 2  <- CORRECT
                                //   jsm7 'hasigomodel' addr 0x011B = 283 = Centra Ruins 8 <- absurd
                                // Blocking both killed the bogus Centra Ruins label
                                // (the goal) but ALSO stripped a correct "Exit to
                                // Deling City - Sewer 2" label Aaron had been relying
                                // on, leaving him a bare "Exit".
                                //
                                // The discriminator is available statically: sibling
                                // lines on the SAME field that resolved to a LITERAL
                                // destination. On glwater3 those are 762 and 763
                                // (jsm3/4/5/6 'rinoa'/'selphie'/'quistis'/'book').
                                // irvine's 762 is corroborated by a sibling literal;
                                // hasigomodel's 283 matches nothing on the field and
                                // is the fabrication. So: accept addr-as-literal when
                                // the field is 'bg*' (the original 8-BAT evidence base,
                                // preserved as-is) OR when a sibling line independently
                                // resolved to that same destination id.
                                // NOTE: corroboration scans s_jsmEntities[], NOT
                                // s_capturedLines[]. This block runs inside the
                                // per-line loop that POPULATES destFieldId, so any
                                // sibling with a higher line index hasn't been
                                // assigned yet -- and on glwater3 every literal-
                                // bearing sibling (jsm3-jsm6) sits after irvine
                                // (jsm2), so a s_capturedLines[] scan would find
                                // nothing and silently fail to corroborate.
                                // s_jsmEntities[].param is fully populated by the
                                // JSM scan before any of this runs, so it's order-
                                // independent.
                                bool corroborated291 = false;
                                if (!isBgField291 && pshmAddr > 0) {
                                    for (int sib = 0; sib < s_jsmEntityCount; sib++) {
                                        if (sib == jsmIdx) continue;
                                        int sd = s_jsmEntities[sib].param;
                                        // positive literal only -- markers are negative
                                        // (bit 31 set) and must not corroborate each other
                                        if (sd > 0 && sd == (int)pshmAddr) {
                                            corroborated291 = true;
                                            Log::Field("FieldNavigation: [PSHM-DEST] line%d addr=0x%04X (%d) "
                                                       "CORROBORATED by jsm%d '%s' literal dest "
                                                       "-- addr-as-literal accepted [v0.18.3.291]",
                                                       t, pshmAddr, (int)pshmAddr,
                                                       sib, s_jsmEntities[sib].symName);
                                            break;
                                        }
                                    }
                                }
                                bool addrAsLiteralOk = isBgField291 || corroborated291;
                                if (!addrAsLiteralOk && pshmAddr > 0) {
                                    Log::Field("FieldNavigation: [PSHM-DEST] line%d (jsm%d '%s') "
                                               "marker=0x%08X addr=0x%04X: addr-as-literal NOT applied "
                                               "on non-bg field '%s' (would have fabricated field %d '%s') "
                                               "-- keeping marker, exit stays unlabeled [v0.18.3.290]",
                                               t, jsmIdx, s_jsmEntities[jsmIdx].symName,
                                               (unsigned)rawParam, pshmAddr, fieldName,
                                               (int)pshmAddr,
                                               (pshmAddr < FIELD_DISPLAY_NAMES_COUNT)
                                                   ? FIELD_DISPLAY_NAMES[pshmAddr] : "?");
                                }
                                if (addrAsLiteralOk && pshmAddr > 0 && pshmAddr < FIELD_DISPLAY_NAMES_COUNT) {
                                    Log::Field("FieldNavigation: [PSHM-DEST] line%d (jsm%d '%s') "
                                               "marker=0x%08X addr=0x%04X -> field %d (%s) [addr-as-literal]",
                                               t, jsmIdx, s_jsmEntities[jsmIdx].symName,
                                               (unsigned)rawParam, pshmAddr,
                                               (int)pshmAddr, FIELD_DISPLAY_NAMES[pshmAddr]);
                                    rawParam = (int)pshmAddr;
                                } else if (addrAsLiteralOk) {
                                    // v0.18.3.290: scoped to the bg-field case so the
                                    // non-bg skip above isn't double-logged with a
                                    // misleading "out of range" reason.
                                    Log::Field("FieldNavigation: [PSHM-DEST] line%d (jsm%d '%s') "
                                               "marker=0x%08X addr=0x%04X out of field-id range (0..%d), keeping marker",
                                               t, jsmIdx, s_jsmEntities[jsmIdx].symName,
                                               (unsigned)rawParam, pshmAddr,
                                               FIELD_DISPLAY_NAMES_COUNT - 1);
                                }
                            }
                            s_capturedLines[t].destFieldId = rawParam;
                        }
                        linesMapped++;
                    }
                }
                // v0.18.3.275 [LINE-PAIR]: dump what each captured line was paired
                // with, and what that pairing produced. This is the decisive data
                // for the glfurin1 misassignment (line0 labelled "Mansion 4"/724
                // but actually going to 725/"Mansion 5"): it shows the JSM header
                // counts, which JSM entity each line resolved to, that entity's
                // category/type/param, and the destination that landed on the line.
                // If doors/lines counts are wrong, or a Line entity carries a
                // destination that belongs to its neighbour, it is visible here
                // without another playthrough.
                Log::Field("FieldNavigation: [LINE-PAIR] jsmDoors=%d jsmLines=%d jsmEntities=%d "
                           "captured=%d", s_jsmDoors, s_jsmLines, s_jsmEntityCount,
                           s_capturedLineCount);
                for (int t = 0; t < s_capturedLineCount; t++) {
                    int ji = t;   // v0.18.3.277: doors participate in the sequence
                    if (ji >= 0 && ji < s_jsmEntityCount) {
                        const FieldArchive::JSMEntityInfo& LE = s_jsmEntities[ji];
                        Log::Field("FieldNavigation: [LINE-PAIR] line%d center=(%d,%d) -> jsm%d "
                                   "'%s' cat=%d type=%d param=%d | assigned lineType=%d dest=%d",
                                   t,
                                   (int)((s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2),
                                   (int)((s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2),
                                   ji, LE.symName, LE.jsmCategory, (int)LE.type, LE.param,
                                   (int)s_capturedLines[t].lineType,
                                   s_capturedLines[t].destFieldId);
                    } else {
                        Log::Field("FieldNavigation: [LINE-PAIR] line%d center=(%d,%d) -> jsm%d "
                                   "OUT OF RANGE (no pairing)",
                                   t,
                                   (int)((s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2),
                                   (int)((s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2), ji);
                    }
                }
                // Additionally list every JSM entity that carries a MAPJUMP-style
                // destination, so we can see which entity really owns 725 on
                // glfurin1 and whether it is inside the Line block at all.
                for (int j = 0; j < s_jsmEntityCount; j++) {
                    const FieldArchive::JSMEntityInfo& E = s_jsmEntities[j];
                    if (E.type != FieldArchive::JSM_ENT_MAP_EXIT &&
                        E.type != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                    Log::Field("FieldNavigation: [LINE-PAIR] exit-bearing jsm%d '%s' cat=%d "
                               "type=%d param=%d hasPos=%d pos=(%d,%d)",
                               j, E.symName, E.jsmCategory, (int)E.type, E.param,
                               E.hasPosition ? 1 : 0, (int)E.posX, (int)E.posY);
                }

                // Log the mapping results.
                int cameraPans = 0, screenBounds = 0, lineEvents = 0, lineUnknown = 0, lineInteractive = 0;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    switch (s_capturedLines[t].lineType) {
                        case FieldArchive::JSM_ENT_LINE_CAMERA_PAN:   cameraPans++; break;
                        case FieldArchive::JSM_ENT_LINE_SCREEN_BOUND: screenBounds++; break;
                        case FieldArchive::JSM_ENT_LINE_EVENT:        lineEvents++; break;
                        case FieldArchive::JSM_ENT_LINE_INTERACTIVE:  lineInteractive++; break;
                        default: lineUnknown++; break;
                    }
                }
                Log::Field("FieldNavigation: [fieldload] lineType assigned: %d captured, %d mapped "
                           "(camPan=%d screenBd=%d event=%d interact=%d unknown=%d)",
                           s_capturedLineCount, linesMapped,
                           cameraPans, screenBounds, lineEvents, lineInteractive, lineUnknown);

                // v0.17.9.14.1 DIAG: per-captured-line dump. Greppable
                // [LINEDIAG] anchor. Reports each SETLINE's assigned lineType +
                // destFieldId + center + JSM name. v0.17.9.17: gated behind
                // LINEDIAG_ENABLED (field_navigation.cpp); set 0 for push, flip
                // to 1 to verify a field's trigger-line classification / exits
                // (e.g. pending bgryo1_1 'squalls' / dotown_2 'Selphie' checks).
#if LINEDIAG_ENABLED
                for (int dt = 0; dt < s_capturedLineCount; dt++) {
                    int dji = s_jsmDoors + dt;
                    const char* dsym = (dji < s_jsmEntityCount) ? s_jsmEntities[dji].symName : "?";
                    int dcat = (dji < s_jsmEntityCount) ? s_jsmEntities[dji].jsmCategory : -1;
                    Log::Field("FieldNavigation: [LINEDIAG] field='%s' line%d order=%d center=(%d,%d) "
                               "type=%s destFieldId=%d extDisp=%d (jsm%d '%s' cat=%d)",
                               fieldName, dt, s_capturedLines[dt].lineOrder,
                               (int)(s_capturedLines[dt].x1 + s_capturedLines[dt].x2) / 2,
                               (int)(s_capturedLines[dt].y1 + s_capturedLines[dt].y2) / 2,
                               FieldArchive::JSMEntityTypeName(s_capturedLines[dt].lineType),
                               s_capturedLines[dt].destFieldId,
                               (int)s_capturedLines[dt].hasExtDispatch,
                               dji, dsym, dcat);
                }
#endif

                // v0.17.7.2: MAPJUMP destination resolver DIAGNOSTIC (observation only).
                //
                // For each SCREEN_BOUND line whose param is an unresolved bit31
                // PSHM marker (the runtime varblock read above failed because the
                // varblock isn't populated at this lifecycle point), enumerate
                // every init-method POPM_W write across all entities targeting
                // the SAME varblock address. If exactly one entity writes a
                // sensible field-ID value there, v0.17.7.3 will adopt it as the
                // resolved destination.
                //
                // This block makes NO data changes -- it only logs. The goal is
                // to confirm (or rule out) the hypothesis that field-exit
                // destinations live in init-method literal-PUSH + POPM_W pairs
                // captured by s_initVarMaps[]. If the BAT log shows writers
                // matching the unresolved addresses, the resolver in v0.17.7.3
                // is a 5-line cross-reference. If it shows zero writers, the
                // destinations live in story-dispatch methods (m != 0) and the
                // scanner needs to be widened first.
                //
                // The summary [INITVARS-SUMMARY] block at the end of this
                // diagnostic shows the full landscape of init-var writes for
                // this field so we can spot patterns even when the per-line
                // lookup misses.
                {
                    int unresolvedLines = 0;
                    int linesWithWriters = 0;
                    for (int t = 0; t < s_capturedLineCount; t++) {
                        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND)
                            continue;
                        int dfi = s_capturedLines[t].destFieldId;
                        // Filter to UNRESOLVED markers only (bit31 set, low 16 bits = varblock addr).
                        if (((unsigned)dfi & 0x80000000u) == 0) continue;
                        unresolvedLines++;
                        uint16_t pshmAddr = (uint16_t)(dfi & 0xFFFF);
                        int jsmIdx = s_jsmDoors + t;
                        const char* sym = (jsmIdx < s_jsmEntityCount)
                                            ? s_jsmEntities[jsmIdx].symName : "?";
                        // Look up all init-method writers to this address.
                        FieldArchive::InitVarWriter writers[16] = {};
                        int totalWriters = FieldArchive::LookupInitVarWrites(
                            (int16_t)pshmAddr, writers, 16);
                        if (totalWriters == 0) {
                            Log::Field("FieldNavigation: [MAPJUMP-RESOLVE] line%d (jsm%d '%s') "
                                       "addr=0x%04X (%d): NO init writers found",
                                       t, jsmIdx, sym, (unsigned)pshmAddr, (int)pshmAddr);
                        } else {
                            linesWithWriters++;
                            int logged = totalWriters < 16 ? totalWriters : 16;
                            Log::Field("FieldNavigation: [MAPJUMP-RESOLVE] line%d (jsm%d '%s') "
                                       "addr=0x%04X (%d): %d init writers%s",
                                       t, jsmIdx, sym, (unsigned)pshmAddr, (int)pshmAddr,
                                       totalWriters, totalWriters > 16 ? " (capped to 16)" : "");
                            for (int w = 0; w < logged; w++) {
                                int wEnt = writers[w].entityIdx;
                                int32_t wVal = writers[w].value;
                                // Look up writer's sym name in the JSM table.
                                const char* wSym = "?";
                                for (int q = 0; q < s_jsmEntityCount; q++) {
                                    if (s_jsmEntities[q].jsmIndex == wEnt) {
                                        wSym = s_jsmEntities[q].symName;
                                        break;
                                    }
                                }
                                // If the value is a plausible field ID, also resolve its name.
                                const char* destName = "";
                                if (wVal > 0 && wVal < FIELD_DISPLAY_NAMES_COUNT)
                                    destName = FIELD_DISPLAY_NAMES[wVal];
                                Log::Field("FieldNavigation: [MAPJUMP-RESOLVE]   writer ent%d '%s' "
                                           "value=%d %s%s",
                                           wEnt, wSym, (int)wVal,
                                           destName[0] ? "-> " : "", destName);
                            }
                        }
                    }
                    Log::Field("FieldNavigation: [MAPJUMP-RESOLVE] summary: %d unresolved SCREEN_BOUND lines, "
                               "%d found writers, %d had no writers",
                               unresolvedLines, linesWithWriters,
                               unresolvedLines - linesWithWriters);

                    // [INITVARS-SUMMARY]: full landscape of init writes for this field.
                    // Useful for spotting the destination values when LookupInitVarWrites()
                    // misses (e.g. if addresses are stored shifted, masked, or under a
                    // different convention than the markers).
                    FieldArchive::InitVarTuple allWrites[256] = {};
                    int totalAllWrites = FieldArchive::EnumerateInitVars(allWrites, 256);
                    int loggedAll = totalAllWrites < 256 ? totalAllWrites : 256;
                    Log::Field("FieldNavigation: [INITVARS-SUMMARY] field has %d init-method POPM_W writes%s",
                               totalAllWrites, totalAllWrites > 256 ? " (capped to 256)" : "");
                    for (int w = 0; w < loggedAll; w++) {
                        int wEnt = allWrites[w].entityIdx;
                        int32_t wAddr = allWrites[w].addr;
                        int32_t wVal = allWrites[w].value;
                        const char* wSym = "?";
                        for (int q = 0; q < s_jsmEntityCount; q++) {
                            if (s_jsmEntities[q].jsmIndex == wEnt) {
                                wSym = s_jsmEntities[q].symName;
                                break;
                            }
                        }
                        // Annotate if the value resembles a field ID.
                        const char* destName = "";
                        if (wVal > 0 && wVal < FIELD_DISPLAY_NAMES_COUNT)
                            destName = FIELD_DISPLAY_NAMES[wVal];
                        Log::Field("FieldNavigation: [INITVARS-SUMMARY]   ent%d '%s' addr=0x%04X (%d) "
                                   "value=%d %s%s",
                                   wEnt, wSym, (unsigned)(wAddr & 0xFFFF), (int)wAddr, (int)wVal,
                                   destName[0] ? "-> " : "", destName);
                    }
                }

                // v0.12.23: Dump scripts of Event Trigger and Unknown-type Line entities.
                // These are interaction mediators on shared dormitory/classroom fields.
                // Their scripts contain REQ opcodes targeting Others entities — revealing
                // which interactive object (bed/desk/wardrobe) each SETLINE zone controls.
                //
                // v0.17.7.2: Gated behind FF8OPC_VERBOSE_JSM. The same per-Director
                // log-explosion problem affected this loop on dormitory fields where
                // Lines reference long Background scripts.
#ifdef FF8OPC_VERBOSE_JSM
                for (int ld = 0; ld < s_capturedLineCount; ld++) {
                    int ldJsmIdx = s_jsmDoors + ld;
                    if (ldJsmIdx >= s_jsmEntityCount) continue;
                    int ldType = s_jsmEntities[ldJsmIdx].type;
                    if (ldType != FieldArchive::JSM_ENT_LINE_CAMERA_PAN &&
                        ldType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        Log::Field("FieldNavigation: [LINE-SCRIPT] Dumping Line entity %d '%s' (type=%s)",
                                   ldJsmIdx, s_jsmEntities[ldJsmIdx].symName,
                                   FieldArchive::JSMEntityTypeName((FieldArchive::JSMEntityType)ldType));
                        FieldArchive::DumpEntityScript(fieldName, ldJsmIdx);
                    }
                }
#endif
            }
