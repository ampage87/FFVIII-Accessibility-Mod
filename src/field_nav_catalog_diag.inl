// field_nav_catalog_diag.inl — One-shot diagnostic dumps for catalog scan.
// Included from field_navigation.cpp inside the FieldNavigation namespace.
// Do not compile independently.
//
// v0.17.7.0: Extracted from field_nav_catalog.inl for size compliance.
//            Behavior byte-for-byte identical to pre-split source.
//
// Each helper is a no-op when its dump flag is already set. Flag reset is
// handled by HookedFieldScriptsInit on field load (party-state, coord) or
// the flag stays perma-true (entity, bg — diagnostics retired in v05.58).

// v05.48/49: Diagnostic dump of ALL entities at scan time.
// Reveals which entities exist and why some might be filtered. Also tries
// multiple SYM offsets to find correct mapping.
// v05.58: s_entDiagDumped initialized true — this dump no longer fires.
// Code retained for future re-enablement if the entity scan ever needs
// re-triage on a new field.
static void DumpEntityDiagOnce(uint8_t* base, uint8_t lim)
{
    if (s_entDiagDumped) return;
    Log::Field("FieldNavigation: [ENTDIAG] === Entity dump: %d entities, symCount=%d, curOffset=%d ===",
               (int)lim, s_symNameCount, s_symOthersOffset);
    // Log ALL SYM names for cross-reference.
    for (int s = 0; s < s_symNameCount; s++) {
        Log::Field("FieldNavigation: [ENTDIAG] SYM[%d]='%s'", s, s_symNames[s]);
    }
    for (int i = 0; i < (int)lim; i++) {
        uint8_t* block = base + ENTITY_STRIDE * i;
        int16_t  modelId      = *(int16_t*)(block + 0x218);
        uint16_t triId        = *(uint16_t*)(block + 0x1FA);
        uint8_t  setpc        = *(block + 0x255);
        uint8_t  talkonoff    = *(block + 0x24B);
        uint8_t  pushonoff    = *(block + 0x249);
        uint8_t  throughonoff = *(block + 0x24C);
        uint32_t execFlags    = *(uint32_t*)(block + 0x160);
        int32_t  fpX          = *(int32_t*)(block + 0x190);
        int32_t  fpZ          = *(int32_t*)(block + 0x198);
        int16_t  simX         = *(int16_t*)(block + 0x20);
        int16_t  simZ         = *(int16_t*)(block + 0x28);
        // Try offset 0, lines+bg, and current offset to compare.
        const char* sym0 = (i < s_symNameCount) ? s_symNames[i] : "(none)";
        int symLB = s_symOthersOffset + i;
        const char* symLBName = (symLB >= 0 && symLB < s_symNameCount) ? s_symNames[symLB] : "(none)";
        Log::Field("FieldNavigation: [ENTDIAG] ent%d model=%d tri=0x%04X setpc=%d "
                   "talk=%d push=%d thru=%d exec=0x%X fp=(%d,%d) sim=(%d,%d) "
                   "@0='%s' @%d='%s'",
                   i, (int)modelId, (unsigned)triId, (int)setpc,
                   (int)talkonoff, (int)pushonoff, (int)throughonoff,
                   execFlags, fpX, fpZ, (int)simX, (int)simZ,
                   sym0, s_symOthersOffset, symLBName);
    }
    s_entDiagDumped = true;
}

// v0.18.3.231 DIAG: Extended entity scan.
//
// ggsta1 reports otherCount=10 while its JSM declares O=13. The three entities
// the scan therefore never reaches are 'gsm3', 'director0' and 'traincont' —
// and the G-Garden Station train staff (absent from every array we have dumped)
// is very likely 'traincont'. This dump reads PAST the engine's reported count,
// up to MAX_ENTITIES, and prints what is actually in those slots. If slots 10-12
// hold real models/positions, the reported count is not the true array length
// and the catalog loop is simply stopping short.
//
// Reads are inside the caller's __try; each slot is a fixed ENTITY_STRIDE block,
// so over-reading stays within the allocation the engine sized for MAX entities.
static void DumpExtendedEntityScanOnce(uint8_t* base, uint8_t entCount)
{
    if (s_extScanDumped) return;
    s_extScanDumped = true;
    Log::Field("FieldNavigation: [EXTSCAN] === reported otherCount=%d, scanning 0..%d ===",
               (int)entCount, MAX_ENTITIES - 1);
    for (int i = 0; i < MAX_ENTITIES; i++) {
        uint8_t* block = base + ENTITY_STRIDE * i;
        int16_t  modelId = *(int16_t*)(block + 0x218);
        uint16_t triId   = *(uint16_t*)(block + 0x1FA);
        uint8_t  setpc   = *(block + 0x255);
        uint8_t  talk    = *(block + 0x24B);
        uint8_t  push    = *(block + 0x249);
        uint8_t  thru    = *(block + 0x24C);
        int32_t  fpX     = *(int32_t*)(block + 0x190);
        int32_t  fpY     = *(int32_t*)(block + 0x194);
        int symIdx = s_symOthersOffset + i;
        const char* sym = (symIdx >= 0 && symIdx < s_symNameCount) ? s_symNames[symIdx] : "(none)";
        Log::Field("FieldNavigation: [EXTSCAN] slot%-2d %s sym='%s' model=%d tri=%u setpc=%d "
                   "talk=%d push=%d thru=%d fp=(%d,%d)",
                   i, (i < (int)entCount) ? "IN " : "OUT", sym,
                   (int)modelId, (unsigned)triId, (int)setpc,
                   (int)talk, (int)push, (int)thru, fpX, fpY);
    }
    Log::Field("FieldNavigation: [EXTSCAN] === end ===");
}

// v0.18.3.228: Catalog scan tracing. Every entity the scan sees is logged with
// the signals that decide its fate, and BOTH drop paths log their reason. The
// push-only skip and the unplaced-entity skip previously discarded entities with
// no log line at all, which is exactly why the missing ggsta1 NPCs (the 'ekiin'
// station attendant, the 'gsm*' students) were invisible to diagnosis.
// Kept here rather than inline in field_nav_catalog.inl to hold that file under
// the 80 KB source-size CI guard.
static void LogScanEntity(int i, const char* sym, int modelId, unsigned triId,
                          int talk, int push, int thru, bool jsmTalk,
                          unsigned rtRad, bool talkable, int fpX, int fpY)
{
    Log::Field("FieldNavigation: [SCAN] ent%d sym='%s' model=%d tri=%u "
               "talk=%d push=%d thru=%d jsmTalk=%d rtRad=%u talkable=%d fp=(%d,%d)",
               i, sym, modelId, triId, talk, push, thru,
               jsmTalk ? 1 : 0, rtRad, talkable ? 1 : 0, fpX, fpY);
}

static void LogScanKeep(int i, const char* sym, const char* typeName, const char* name)
{
    Log::Field("FieldNavigation: [SCAN-KEEP] ent%d sym='%s' type=%s name='%s'",
               i, sym, typeName, name);
}

static void LogScanDropPushOnly(int i, const char* sym, int modelId)
{
    Log::Field("FieldNavigation: [SCAN-DROP] ent%d sym='%s' model=%d "
               "push-only visible entity, not talkable -- skipped", i, sym, modelId);
}

static void LogScanDropUnplaced(int i, const char* sym, int modelId, unsigned triId,
                                int fpX, int fpY, bool hasModel, bool specialJSM)
{
    Log::Field("FieldNavigation: [SCAN-DROP] ent%d sym='%s' model=%d tri=%u fp=(%d,%d) "
               "not placed (hasModel=%d specialJSM=%d) -- skipped",
               i, sym, modelId, triId, fpX, fpY,
               hasModel ? 1 : 0, specialJSM ? 1 : 0);
}

// v05.50: Background entity diagnostic dump. Logs the entire backgrounds
// array with execution_flags, bgstate, and candidate SYM indices to
// determine the correct mapping.
// v05.58: s_bgDiagDumped initialized true — this dump no longer fires.
// Code retained for future re-enablement.
static void DumpBgDiagOnce(uint8_t lim)
{
    if (s_bgDiagDumped) return;
    if (!FF8Addresses::HasFieldStateBackgrounds()) return;
    __try {
        uint8_t bgCount = *FF8Addresses::pFieldStateBackgroundCount;
        uint8_t* bgBase = reinterpret_cast<uint8_t*>(
            *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateBackgrounds));
        Log::Field("FieldNavigation: [BGDIAG] === Background entity dump: %d bg entities ===",
                   (int)bgCount);
        Log::Field("FieldNavigation: [BGDIAG] bgBase=0x%08X  otherCount=%d  symCount=%d  JSM(D=%d L=%d B=%d O=%d)",
                   (uint32_t)(uintptr_t)bgBase, (int)lim, s_symNameCount,
                   s_jsmDoors, s_jsmLines, s_jsmBackgrounds, s_jsmOthers);
        if (bgBase && bgCount > 0) {
            int bgLim = (bgCount < MAX_BG_ENTITIES) ? bgCount : MAX_BG_ENTITIES;
            for (int b = 0; b < bgLim; b++) {
                uint8_t* block = bgBase + BG_STRIDE * b;
                // ff8_field_state_common fields:
                uint32_t execFlags = *(uint32_t*)(block + 0x160);
                uint16_t instrPos  = *(uint16_t*)(block + 0x176);
                // ff8_field_state_background fields (after common at 0x188):
                uint16_t bgstate   = *(uint16_t*)(block + 0x188);
                // SYM mapping hypothesis: backgrounds are at SYM[L .. L+B-1]
                // where L = number of line entities from JSM header.
                // But we also try offset=0 mapping to see if it makes sense.
                // For now, log the raw index and let the human figure it out.
                const char* symDirect = (b < s_symNameCount) ? s_symNames[b] : "(none)";
                // Hypothesis A: offset = otherCount (bg entities AFTER others in SYM).
                int symAfterOthers = (int)lim + b;
                const char* symAfterO = (symAfterOthers < s_symNameCount)
                                        ? s_symNames[symAfterOthers] : "(none)";
                // Hypothesis B: offset = lines (SYM order = lines, bg, others).
                int symAfterLines = s_jsmLines + b;
                const char* symAfterL = (symAfterLines >= 0 && symAfterLines < s_symNameCount)
                                        ? s_symNames[symAfterLines] : "(none)";
                Log::Field("FieldNavigation: [BGDIAG] bg%d exec=0x%X bgstate=0x%04X ipos=%u "
                           "@0='%s' @oth%d='%s' @lin%d='%s'",
                           b, execFlags, (unsigned)bgstate, (unsigned)instrPos,
                           symDirect, symAfterOthers, symAfterO,
                           symAfterLines, symAfterL);
            }
        } else {
            Log::Field("FieldNavigation: [BGDIAG] bgBase is NULL or bgCount==0");
        }
        Log::Field("FieldNavigation: [BGDIAG] === End background dump ===");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [BGDIAG] Exception reading backgrounds array");
    }
    s_bgDiagDumped = true;
}

// v0.14.107: Party-state diagnostic — log the active party formation bytes
// once per field. Useful for verifying that the party-member filter (in
// the main scan loop) is reading the correct savemap state. Resets via
// HookedFieldScriptsInit setting s_partyDiagDumped = false on each
// field load.
static void DumpPartyStateOnce()
{
    if (s_partyDiagDumped) return;
    __try {
        const uint8_t* f = (const uint8_t*)0x01CFE74C;
        Log::Field("FieldNavigation: [party-state] formation = [%u, %u, %u, %u]",
                   (unsigned)f[0], (unsigned)f[1], (unsigned)f[2], (unsigned)f[3]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [party-state] exception reading formation array");
    }
    s_partyDiagDumped = true;
}

// v05.59: Coordinate diagnostic dump — log ALL coord sources once per field.
// Helps identify coordinate space mismatches between entities, triggers,
// and gateways.
static void DumpCoordDiagOnce(uint8_t* base, uint8_t lim)
{
    if (s_coordDiagDumped) return;
    s_coordDiagDumped = true;  // only dump once per field
    Log::Field("FieldNavigation: [COORDDIAG] === Coordinate space diagnostic ===");
    Log::Field("FieldNavigation: [COORDDIAG] Field: %s  player=ent%d",
               FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?",
               s_playerEntityIdx);
    // Entity positions (all strategies)
    for (int i = 0; i < (int)lim; i++) {
        uint8_t* block = base + ENTITY_STRIDE * i;
        int16_t  modelId = *(int16_t*)(block + 0x218);
        uint16_t triId   = *(uint16_t*)(block + 0x1FA);
        int32_t  fpX     = *(int32_t*)(block + 0x190);
        int32_t  fpY     = *(int32_t*)(block + 0x194);
        int32_t  fpZ     = *(int32_t*)(block + 0x198);
        int16_t  simX    = *(int16_t*)(block + 0x20);
        int16_t  simY    = *(int16_t*)(block + 0x24);
        int16_t  simZ    = *(int16_t*)(block + 0x28);
        Log::Field("FieldNavigation: [COORDDIAG] ent%d model=%d tri=0x%04X "
                   "fp=(%d,%d,%d)/4096=(%d,%d,%d) sim=(%d,%d,%d)%s",
                   i, (int)modelId, (unsigned)triId,
                   fpX, fpY, fpZ, fpX/4096, fpY/4096, fpZ/4096,
                   (int)simX, (int)simY, (int)simZ,
                   (i == s_playerEntityIdx) ? " [PLAYER]" : "");
    }
    // SETLINE trigger positions — show all 3 raw axes
    for (int t = 0; t < s_capturedLineCount; t++) {
        Log::Field("FieldNavigation: [COORDDIAG] trigger%d ent=0x%08X "
                   "raw=(%d,%d,%d)->(%d,%d,%d) "
                   "centerX=%.0f centerY=%.0f centerZ=%.0f active=%d",
                   t, s_capturedLines[t].entityAddr,
                   (int)s_capturedLines[t].x1, (int)s_capturedLines[t].y1, (int)s_capturedLines[t].z1,
                   (int)s_capturedLines[t].x2, (int)s_capturedLines[t].y2, (int)s_capturedLines[t].z2,
                   (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f,
                   (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f,
                   (float)(s_capturedLines[t].z1 + s_capturedLines[t].z2) / 2.0f,
                   (int)s_capturedLines[t].active);
    }
    // v0.07.83: INF gateway logging removed (gateways replaced by JSM exits).
    Log::Field("FieldNavigation: [COORDDIAG] === End diagnostic ===");
}
