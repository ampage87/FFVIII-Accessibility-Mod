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
//   1. ResolveLatePositions()    — runtime entity struct read for entities
//                                  with hasPshmCoords but no hasPosition
//   2. MatchSet3LateCaptures()   — overlay accumulated SET3 captures
//   3. ResolveStructPositions()  — direct entity struct read for PSHM entities
//                                  (catches entities beyond active window)

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
