// field_nav_catalog_lateres.inl — Late-resolution position fixups for catalog scan.
// Included from field_navigation.cpp inside the FieldNavigation namespace.
// Do not compile independently.
//
// v0.17.7.0: Extracted from field_nav_catalog.inl for size compliance.
//            Behavior byte-for-byte identical to pre-split source, except
//            the v0.12.17 VARBLOCK-POS block (gated `if (false)`, ~60 lines
//            of unreachable code) is dropped. Git history preserves it.
//
// Each helper writes resolved positions back into s_jsmEntities[] after the
// initial JSM-scanner pass and before catalog injection runs. They are
// stateless beyond the namespace-scope statics they read/write.
//
// Call order (must match RefreshCatalog's pre-split order):
//   1. ResolveLatePositions()          — runtime entity struct read for entities
//                                         with hasPshmCoords but no hasPosition
//   2. MatchSet3LateCaptures()         — overlay accumulated SET3 captures
//   3. ResolveStructPositions()        — direct entity struct read for PSHM entities
//                                         (catches entities beyond active window)
//   4. ResolveTriangleCentroidPositions() — walkmesh-triangle-centroid approximation,
//                                         LAST resort for entities the first three
//                                         (all live-memory reads) couldn't reach

// v0.08.05: Late PSHM resolution — retry direct struct reads for entities
// whose positions weren't available at field init time. By RefreshCatalog time,
// the field has been running and non-init scripts may have executed SET3.
static void ResolveLatePositions()
{
    if (!FF8Addresses::pFieldStateOthers) return;
    __try {
        uint8_t* othBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (othBase) {
            int othStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
            for (int jr = 0; jr < s_jsmEntityCount; jr++) {
                FieldArchive::JSMEntityInfo& jer = s_jsmEntities[jr];
                if (!jer.hasPshmCoords || jer.hasPosition) continue;
                // v0.08.15: Handle both Others (cat 3) and Background (cat 2) entities.
                uint8_t* blk4 = nullptr;
                int oir = 0;
                if (jer.jsmCategory == 3) {
                    oir = jer.jsmIndex - othStart;
                    if (oir < 0) continue;
                    blk4 = othBase + ENTITY_STRIDE * oir;
                } else if (jer.jsmCategory == 2) {
                    uint8_t* bgBase4 = nullptr;
                    if (FF8Addresses::HasFieldStateBackgrounds()) {
                        bgBase4 = reinterpret_cast<uint8_t*>(
                            *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
                    }
                    if (!bgBase4) continue;
                    int bgStart4 = s_jsmDoors + s_jsmLines;
                    oir = jer.jsmIndex - bgStart4;
                    if (oir < 0) continue;
                    blk4 = bgBase4 + BG_STRIDE * oir;
                } else {
                    continue;
                }
                int32_t fX = *(int32_t*)(blk4 + 0x190);
                int32_t fY = *(int32_t*)(blk4 + 0x194);
                uint16_t tr = 0;
                if (jer.jsmCategory == 3) {
                    tr = *(uint16_t*)(blk4 + 0x1FA);
                }
                // v0.18.3.290 (#85): duplicate-slot corruption guard.
                // The engine's live entity array only tracks a limited window
                // of entities; slots past it alias to other entities' data
                // (the confirmed .285 root cause). When that happens the read
                // SUCCEEDS and returns plausible, in-range values that actually
                // belong to a DIFFERENT entity -- silently, with nothing to
                // flag it. Confirmed on glwater3: 'ladline5' (own SET3 tri=186)
                // and 'ladline6' (own tri=175) both came back with byte-identical
                // fp values to 'saku1' (tri=147) and 'saku2' (tri=181), so the
                // catalog showed two phantom objects sitting exactly on top of
                // Gate 1 and Gate 2 -- real catalog bloat Aaron flagged, and two
                // more entries competing with the real gates.
                //
                // The entity's OWN statically-captured SET3 triangle is
                // authoritative for identity: a live read that disagrees with it
                // is reading someone else's slot. Reject those and let
                // ResolveTriangleCentroidPositions() fall back to the centroid of
                // the entity's own triangle instead. Entities whose live tri
                // MATCHES their static tri (saku1/saku2 here) are untouched and
                // keep their exact live positions -- so this costs precision
                // only where the value was already wrong.
                // Only applies when both triangles are known (cat-2 Backgrounds
                // don't read tr at all, so tr==0 skips the check).
                if (tr != 0 && jer.posTriangle != 0 && tr != jer.posTriangle) {
                    Log::Field("FieldNavigation: [LATE-RESOLVE] ent%d '%s' REJECTED: "
                               "live tri=%u disagrees with own SET3 tri=%u -- "
                               "duplicate/aliased entity slot (idx=%d), falling back to centroid",
                               jer.jsmIndex, jer.symName,
                               (unsigned)tr, (unsigned)jer.posTriangle, oir);
                    continue;
                }
                if (fX != 0 || fY != 0) {
                    jer.posX = (int16_t)(fX / 4096);
                    jer.posY = (int16_t)(fY / 4096);
                    jer.posTriangle = tr;
                    jer.hasPosition = true;
                    Log::Field("FieldNavigation: [LATE-RESOLVE] ent%d '%s' type=%s "
                               "cat=%d idx=%d pos=(%d,%d) tri=%u fp=(%d,%d)",
                               jer.jsmIndex, jer.symName,
                               FieldArchive::JSMEntityTypeName(jer.type),
                               jer.jsmCategory, oir,
                               (int)jer.posX, (int)jer.posY,
                               (unsigned)tr, fX, fY);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.08.16: SET3-LATE-MATCH — re-check accumulated SET3 captures against PSHM entities.
// The extended capture window (3s) catches per-frame SET3 calls from entities like
// dic (bghall_1 Directory) whose SET3 fires in method 1+, not during init.
// This overwrites shift-pattern approximations with engine-resolved positions.
static void MatchSet3LateCaptures()
{
    if (s_set3CaptureCount <= 0) return;
    if (!FF8Addresses::pFieldStateOthers) return;
    __try {
        uint8_t* set3OthBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        uint8_t* set3BgBase = nullptr;
        if (FF8Addresses::HasFieldStateBackgrounds()) {
            set3BgBase = reinterpret_cast<uint8_t*>(
                *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
        }
        if (set3OthBase) {
            int set3OthStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
            int set3BgStart = s_jsmDoors + s_jsmLines;
            int lateMatched = 0;
            for (int jl = 0; jl < s_jsmEntityCount; jl++) {
                FieldArchive::JSMEntityInfo& jel = s_jsmEntities[jl];
                if (!jel.hasPshmCoords) continue;
                // Compute expected entity address.
                uint32_t lateAddr = 0;
                if (jel.jsmCategory == 3) {
                    int oi = jel.jsmIndex - set3OthStart;
                    if (oi < 0) continue;
                    lateAddr = (uint32_t)(uintptr_t)(set3OthBase + ENTITY_STRIDE * oi);
                } else if (jel.jsmCategory == 2 && set3BgBase) {
                    int bi = jel.jsmIndex - set3BgStart;
                    if (bi < 0) continue;
                    lateAddr = (uint32_t)(uintptr_t)(set3BgBase + BG_STRIDE * bi);
                } else {
                    continue;
                }
                // Search SET3 captures for this entity address.
                for (int c = 0; c < s_set3CaptureCount; c++) {
                    if (s_set3Captures[c].entityAddr == lateAddr) {
                        int16_t newX = s_set3Captures[c].posX;
                        int16_t newY = s_set3Captures[c].posY;
                        if (newX == 0 && newY == 0) break;  // no useful position
                        // Only log + update if position actually changed.
                        if (!jel.hasPosition || jel.posX != newX || jel.posY != newY) {
                            Log::Field("FieldNavigation: [SET3-LATE-MATCH] ent%d '%s' type=%s "
                                       "cat=%d old=(%d,%d) new=(%d,%d) tri=%u addr=0x%08X",
                                       jel.jsmIndex, jel.symName,
                                       FieldArchive::JSMEntityTypeName(jel.type),
                                       jel.jsmCategory,
                                       jel.hasPosition ? (int)jel.posX : 0,
                                       jel.hasPosition ? (int)jel.posY : 0,
                                       (int)newX, (int)newY,
                                       (unsigned)s_set3Captures[c].triId, lateAddr);
                            jel.posX = newX;
                            jel.posY = newY;
                            jel.posTriangle = s_set3Captures[c].triId;
                            jel.hasPosition = true;
                            lateMatched++;
                        }
                        break;
                    }
                }
            }
            if (lateMatched > 0) {
                Log::Field("FieldNavigation: [SET3-LATE-MATCH] %d entities updated from %d captures",
                           lateMatched, s_set3CaptureCount);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.12.17: VARBLOCK-POS block (entity-scope PSHM varblock read) is permanently
// disabled — it CORRUPTED positions by replacing shift-pattern approximations
// with zeros for entities beyond the active window. The v0.17.7.0 split drops
// the unreachable `if (false) { ... }` body. Git history at v0.17.6.2 has the
// original code if anyone needs to revisit varblock as a future resolution path.

// v0.12.17: Direct entity struct position read for PSHM entities.
// The shift-pattern approximation discards the PSHM X value, giving
// ~200-unit error. But the engine allocates structs for ALL Others
// entities (not just the active window). If the entity's init script
// ran SET3 during field_scripts_init, the struct has the resolved
// position even though the entity isn't in the active window.
// LATE-RESOLVE skips entities with hasPosition=true (from shift-pattern),
// so we check here specifically for hasPshmCoords entities.
static void ResolveStructPositions()
{
    if (!FF8Addresses::pFieldStateOthers) return;
    __try {
        uint8_t* othBase2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        uint8_t* bgBase3 = nullptr;
        if (FF8Addresses::HasFieldStateBackgrounds()) {
            bgBase3 = reinterpret_cast<uint8_t*>(
                *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
        }
        if (othBase2) {
            int structOthStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
            int structBgStart = s_jsmDoors + s_jsmLines;
            int structFixed = 0;
            for (int js = 0; js < s_jsmEntityCount; js++) {
                FieldArchive::JSMEntityInfo& jes = s_jsmEntities[js];
                if (!jes.hasPshmCoords) continue;
                // Try reading the entity struct position directly.
                uint8_t* blk5 = nullptr;
                int sIdx = 0;
                if (jes.jsmCategory == 3) {
                    sIdx = jes.jsmIndex - structOthStart;
                    if (sIdx < 0 || sIdx >= 31) continue;  // safety: max 31 Others
                    blk5 = othBase2 + ENTITY_STRIDE * sIdx;
                } else if (jes.jsmCategory == 2 && bgBase3) {
                    sIdx = jes.jsmIndex - structBgStart;
                    if (sIdx < 0 || sIdx >= MAX_BG_ENTITIES) continue;
                    blk5 = bgBase3 + BG_STRIDE * sIdx;
                } else {
                    continue;
                }
                int32_t fX5 = *(int32_t*)(blk5 + 0x190);
                int32_t fY5 = *(int32_t*)(blk5 + 0x194);
                if (fX5 == 0 && fY5 == 0) continue;  // no position set
                // v0.18.3.291 (#85): SAME duplicate-slot guard as
                // ResolveLatePositions(). The v0.18.3.290 guard was INCOMPLETE --
                // it only covered ResolveLatePositions, and this pass runs after
                // it and re-applied the exact phantom positions it had just
                // rejected. Confirmed in the .290 BAT log: ladline5/ladline6 were
                // correctly REJECTED here...
                //   [LATE-RESOLVE] ent22 'ladline5' REJECTED: live tri=147 vs own 186
                // ...and then immediately re-corrupted by this pass...
                //   [STRUCT-POS] ent22 'ladline5' idx=6 struct=(268,671) old=(0,0)
                // ...landing them back on top of Gate 1 / Gate 2 in the catalog.
                // This pass reads the same aliased slot (idx 6/7) from the same
                // base pointer, so it needs the same test: the entity's own static
                // SET3 triangle is authoritative for identity, and a struct whose
                // triangle disagrees belongs to a different entity.
                uint16_t sTri = (jes.jsmCategory == 3) ? *(uint16_t*)(blk5 + 0x1FA) : 0;
                if (sTri != 0 && jes.posTriangle != 0 && sTri != jes.posTriangle) {
                    Log::Field("FieldNavigation: [STRUCT-POS] ent%d '%s' REJECTED: "
                               "struct tri=%u disagrees with own SET3 tri=%u -- "
                               "duplicate/aliased entity slot (idx=%d) [v0.18.3.291]",
                               jes.jsmIndex, jes.symName,
                               (unsigned)sTri, (unsigned)jes.posTriangle, sIdx);
                    continue;
                }
                int16_t sX = (int16_t)(fX5 / 4096);
                int16_t sY = (int16_t)(fY5 / 4096);
                // Only update if the struct position differs from current.
                if (sX != jes.posX || sY != jes.posY) {
                    Log::Field("FieldNavigation: [STRUCT-POS] ent%d '%s' type=%s "
                               "cat=%d idx=%d struct=(%d,%d) old=(%d,%d) fp=(%d,%d)",
                               jes.jsmIndex, jes.symName,
                               FieldArchive::JSMEntityTypeName(jes.type),
                               jes.jsmCategory, sIdx,
                               (int)sX, (int)sY,
                               (int)jes.posX, (int)jes.posY,
                               fX5, fY5);
                    jes.posX = sX;
                    jes.posY = sY;
                    jes.hasPosition = true;
                    structFixed++;
                }
            }
            if (structFixed > 0)
                Log::Field("FieldNavigation: [STRUCT-POS] %d PSHM positions updated from entity structs",
                           structFixed);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.18.3.295 (#85): mutual-exclusion state groups — DRY RUN, suppresses nothing.
//
// Aaron's two-state BAT proved glwater3's shortcut ladder is two entities gating
// their SET3 on the same field variable against different values (ladline6 == 0
// standing, ladline5 == 9 fallen; live varblock 340 read as a BYTE was 0 then 9).
// The catalog is built from static script data, so it injects both and the player
// hears the same ladder twice. The obvious fix is to keep only the member whose
// value matches the live byte.
//
// v0.18.3.296 UPDATE -- the .295 dry run RAN and refuted the naive rule on the
// very field it was designed for. glwater3, ladder standing (live byte 0):
//     ladline6 wants 0 -> KEEP      (correct)
//     ladline5 wants 9 -> SUPPRESS  (correct)
//     saku3    wants 3 -> SUPPRESS  (WRONG -- saku3 is Gate 3, a real gate)
// The discriminator it forced out: whether a JPF (jump-if-false) sits between
// the guard and the SET3. With a JPF the placement is genuinely conditional;
// without one the SET3 runs every time and the guard read is feeding some later
// branch instead. It is NOT a ladline/saku split --
//     conditional:   glwater3 ladline5, glwater2 saku2
//     unconditional: glwater3 ladline6 + saku3, glwater2 saku3 + saku4
// The refined rule (only a conditional entity may be suppressed) keeps saku3
// and both glwater2 gates while still dropping the wrong ladder state.
// Still logging only: both verdicts are printed side by side so the next BAT
// says whether refined matches reality before anything acts on it.
//
// Original .295 reasoning follows.
//
// I intended to ship exactly that here and backed off on the evidence. Capturing
// guards across all the sewer fields shows variable 340 is NOT a simple
// "which state" enum -- it is shared by entities that clearly coexist:
//   glwater3:  ladline5 == 9,  ladline6 == 0,  saku3 == 3
//   glwater2:  saku2 == 25,    saku3 == 13,    saku4 == 16
// Those three glwater2 entries are three REAL GATES the player has to find. The
// live byte can only equal one of 25/13/16, so a naive "keep only the match" rule
// would have silently hidden two working gates -- the precise failure mode that
// has already cost several BAT cycles on this issue. glwater3's saku3 (== 3, while
// the live byte is 0 or 9) would have been hidden too.
//
// LEAD for whoever picks this up: the constants look like BIT MASKS, not enum
// values -- 25=0b11001, 13=0b01101, 16=0b10000, 9=0b1001, 7=0b111, 3=0b11 -- and
// the live word moved 0x0900 -> 0x0909, i.e. one nibble set. A `value & mask`
// test would let several entities be simultaneously true, which is what a room
// full of independently-toggled gates actually needs. Unconfirmed; the operator
// is encoded in the JMP/JMPB/JPF opcodes this codebase does not decode.
//
// Worse, the script shapes don't separate them either. ladline5 has a real
// conditional (JPF) between its guard and its SET3; ladline6 and saku3 both run
// straight from guard to SET3 with no JPF at all -- structurally identical, yet
// we know ladline6 is state-dependent. Distinguishing "guard that controls
// existence" from "state read used for something else" needs the JMP/JMPB/JPF
// compare semantics, which this codebase does not decode.
//
// So: log the decision this rule WOULD make and change nothing. One BAT in each
// ladder state says whether the verdicts match reality, at zero risk of hiding a
// gate. If they hold up, .296 flips it from logging to acting.
static void ResolveStateExclusionGroups()
{
    if (s_jsmEntityCount <= 0) return;
    static const uintptr_t VB_BASE295 = 0x01CFE9B8;  // EXIT_VARBLOCK_BASE
    __try {
        for (int a = 0; a < s_jsmEntityCount; a++) {
            const FieldArchive::JSMEntityInfo& ea = s_jsmEntities[a];
            if (!ea.hasStateGuard) continue;
            // Only report each address once: skip if an earlier entity owns it.
            bool seenEarlier = false;
            for (int b = 0; b < a; b++)
                if (s_jsmEntities[b].hasStateGuard &&
                    s_jsmEntities[b].stateVarAddr == ea.stateVarAddr) { seenEarlier = true; break; }
            if (seenEarlier) continue;

            int members = 0, distinct = 0;
            int16_t seenVals[16] = {};
            for (int b = 0; b < s_jsmEntityCount; b++) {
                const FieldArchive::JSMEntityInfo& eb = s_jsmEntities[b];
                if (!eb.hasStateGuard || eb.stateVarAddr != ea.stateVarAddr) continue;
                members++;
                bool dup = false;
                for (int v = 0; v < distinct; v++) if (seenVals[v] == eb.stateVarValue) { dup = true; break; }
                if (!dup && distinct < 16) seenVals[distinct++] = eb.stateVarValue;
            }
            if (members < 2 || distinct < 2) continue;   // not a mutual-exclusion shape

            unsigned addr = (unsigned)(uint16_t)ea.stateVarAddr;
            if (addr >= 0x2000) continue;
            int liveByte = (int)*(const uint8_t*)(VB_BASE295 + addr);
            int liveWord = (int)*(const uint16_t*)(VB_BASE295 + addr);
            int matches = 0;
            for (int b = 0; b < s_jsmEntityCount; b++)
                if (s_jsmEntities[b].hasStateGuard &&
                    s_jsmEntities[b].stateVarAddr == ea.stateVarAddr &&
                    s_jsmEntities[b].stateVarValue == (int16_t)liveByte) matches++;

            // v0.18.3.297 ANCHOR GUARD -- the precondition that makes acting safe.
            // Only touch a group when at least one member's value actually equals
            // the live byte. If none does, the variable is not encoding this group's
            // state in a way we understand, so we must not draw conclusions from it.
            //
            // This is not caution for its own sake: every live read of varblock 340
            // across every BAT has been 0 or 9, while glwater2's three gates want
            // 25/13/16. Without this guard a future state could see zero matches and
            // suppress a conditional gate on no evidence; with it, glwater2 is
            // provably untouched -- no member can ever match, so nothing is acted on.
            bool anchored = (matches > 0);
            Log::Field("FieldNavigation: [STATE-GROUP] varblock[0x%04X] (%d) live byte=%d word=%d "
                       "-- %d entities, %d distinct values, %d match -- %s",
                       addr, (int)addr, liveByte, liveWord, members, distinct, matches,
                       anchored ? "ANCHORED, acting on refined verdicts"
                                : "NO ANCHOR (no member matches) -- nothing suppressed");
            for (int b = 0; b < s_jsmEntityCount; b++) {
                const FieldArchive::JSMEntityInfo& eb = s_jsmEntities[b];
                if (!eb.hasStateGuard || eb.stateVarAddr != ea.stateVarAddr) continue;
                // NAIVE (.295): equality with the live byte. Refuted -- it
                // suppressed glwater3's saku3, a real gate.
                bool keepNaive = (eb.stateVarValue == (int16_t)liveByte);
                // REFINED (.296): only a CONDITIONAL placement (JPF between the
                // guard and the SET3) can be state-dependent at all. An entity
                // whose SET3 is unconditional runs it every time and must always
                // be kept, whatever its guard read says.
                bool keepRefined = (!eb.stateGuardConditional) || keepNaive;
                // v0.18.3.297: ACT on the refined verdict, with the anchor guard
                // above as a hard precondition.
                bool acted = false;
                if (anchored && !keepRefined && eb.jsmIndex >= 0 && eb.jsmIndex < MAX_JSM_ENTITIES) {
                    s_jsmStateSuppressed[eb.jsmIndex] = true;
                    acted = true;
                }
                Log::Field("FieldNavigation: [STATE-GROUP]   ent%d '%s' wants %d cond=%d -> "
                           "naive=%s refined=%s%s%s (pos=%s %d,%d)",
                           eb.jsmIndex, eb.symName, (int)eb.stateVarValue,
                           eb.stateGuardConditional ? 1 : 0,
                           keepNaive   ? "KEEP" : "SUPPRESS",
                           keepRefined ? "KEEP" : "SUPPRESS",
                           (keepNaive != keepRefined) ? "  <-- RULES DISAGREE" : "",
                           acted ? "  [SUPPRESSED]" : "",
                           eb.hasPosition ? "YES" : "no",
                           (int)eb.posX, (int)eb.posY);
            }
            if (!anchored)
                Log::Field("FieldNavigation: [STATE-GROUP]   no member matches the live value -- "
                           "group left completely alone (this is the glwater2 case by design).");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.18.3.286 (#85): walkmesh-triangle-centroid fallback for entities beyond
// the engine's active-tracking window. GitHub #85 (Deling sewer gate maze):
// the `sakuN` gate entities in glwater2-5 have PSHM_W (runtime-only) X/Y/Z
// coordinates, but their own JSM script's SET3 call ALSO records a walkmesh
// triangle ID -- pure static, parse-time data, independent of whether the
// entity is currently tracked by the engine (proven via an offline static
// analysis of the field archive; see
// `Plan & Research Documents/2026-07-19_sewer_gates_offline_analysis.md`).
// ResolveLatePositions/MatchSet3LateCaptures/ResolveStructPositions above all
// require a live memory read of the entity's own struct, which only exists
// for entities within the active window (`pFieldStateOthers` only tracks a
// limited count) -- for the sewer gates that struct read comes back zero or
// a duplicate of an unrelated in-window entity, so those three passes can
// never resolve them, root-caused and confirmed in the v0.18.3.285 EXTSCAN
// dump. This pass runs LAST and only fills the gap: any entity a live read
// already resolved is left untouched (a real position is always preferred
// over an approximation), and entities with no captured triangle at all
// remain unpositioned exactly as before.
//
// The resulting position is the CENTROID of the triangle the entity's own
// SET3 call placed it on -- room/triangle-scale accuracy, not the exact
// model position. `s_jsmTriangleApprox[]` records which entities came from
// this pass so the catalog injection can label them as approximate (screen-
// reader users need that distinction: auto-drive will get them to the right
// triangle, not necessarily flush against the gate model).
static void ResolveTriangleCentroidPositions()
{
    if (!s_walkmesh.valid || !s_walkmesh.triangles || s_walkmesh.numTriangles <= 0) return;
    int centroidFixed = 0;
    for (int jc = 0; jc < s_jsmEntityCount; jc++) {
        FieldArchive::JSMEntityInfo& jec = s_jsmEntities[jc];
        if (jec.hasPosition) continue;              // a real position already won
        if (!jec.hasPshmCoords) continue;            // no PSHM SET3 was ever captured
        if (jec.posTriangle == 0) continue;          // no triangle captured either
        if (jec.posTriangle >= (uint16_t)s_walkmesh.numTriangles) continue;  // bounds
        const FieldArchive::WalkmeshTriangle& tri = s_walkmesh.triangles[jec.posTriangle];
        jec.posX = (int16_t)tri.centerX;
        jec.posY = (int16_t)tri.centerY;
        jec.hasPosition = true;
        if (jc < MAX_JSM_ENTITIES) s_jsmTriangleApprox[jc] = true;
        centroidFixed++;
        Log::Field("FieldNavigation: [TRI-CENTROID] ent%d '%s' type=%s "
                   "pos=(%d,%d) from walkmesh tri=%u centroid [approximate]",
                   jec.jsmIndex, jec.symName, FieldArchive::JSMEntityTypeName(jec.type),
                   (int)jec.posX, (int)jec.posY, (unsigned)jec.posTriangle);
    }
    if (centroidFixed > 0)
        Log::Field("FieldNavigation: [TRI-CENTROID] %d positions approximated from walkmesh triangle centroids",
                   centroidFixed);
}
