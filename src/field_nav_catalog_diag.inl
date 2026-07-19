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
// v0.18.3.267: one-shot PUZZLE-OBJECT diagnostic (Caraway's Mansion glass/statue).
//
// Why: on glfurin1 the wine glass ('cup') and on glfurin3 the statue ('megami')
// never reach the catalog. Both are JSM Interactive Objects with NO resolvable
// position (glfurin3 'megami' has no SET3 at all; glfurin1 'cup' only has PSHM
// coords inherited down a paired-entity chain, so they are not trustworthy), and
// the catalog drops unpositioned interactive objects. Their only real positional
// anchor is the captured SETLINE trigger zone.
//
// The 2026-07-18 log could not settle two things, because its only glfurin3
// catalog scan happened AFTER the puzzle was solved (the trigger line is
// switched off once the passage opens):
//   1. Is the glfurin3 interact line present/active BEFORE the puzzle is solved?
//      (It was classified interact=1 at field load, so it should be.)
//   2. glfurin1 captured TWO interact lines but surfaced only one, and Aaron
//      confirmed the surfaced one is a screen transition, not the glass. What is
//      the second line, and why is it dropped?
//
// So: dump the full captured-line inventory plus every JSM entity carrying a
// puzzle SYM, once per field load. Logging only — no behaviour change.
static void DumpPuzzleDiagOnce()
{
    if (s_puzzleDiagDumped) return;
    s_puzzleDiagDumped = true;
    __try {
        // Only fields that actually contain a watched puzzle object, to keep
        // this quiet everywhere else.
        static const char* kWatch[] = { "cup", "megami", "te", "kakusi", "kidou" };
        bool relevant = false;
        for (int j = 0; j < s_jsmEntityCount && !relevant; j++)
            for (int w = 0; w < (int)(sizeof(kWatch)/sizeof(kWatch[0])); w++)
                if (_stricmp(s_jsmEntities[j].symName, kWatch[w]) == 0) { relevant = true; break; }
        if (!relevant) return;

        Log::Field("FieldNavigation: [PUZZLE-DIAG] === captured lines: %d ===", s_capturedLineCount);
        for (int t = 0; t < s_capturedLineCount; t++) {
            const CapturedTriggerLine& L = s_capturedLines[t];
            Log::Field("FieldNavigation: [PUZZLE-DIAG] line%d active=%d type=%d dest=%d "
                       "extDisp=%d center=(%d,%d) (%d,%d)->(%d,%d) entAddr=0x%08X name='%s'",
                       t, L.active ? 1 : 0, (int)L.lineType, L.destFieldId,
                       L.hasExtDispatch ? 1 : 0,
                       (int)((L.x1 + L.x2) / 2), (int)((L.y1 + L.y2) / 2),
                       (int)L.x1, (int)L.y1, (int)L.x2, (int)L.y2,
                       (unsigned)L.entityAddr, L.name);
        }
        Log::Field("FieldNavigation: [PUZZLE-DIAG] === puzzle JSM entities ===");
        for (int j = 0; j < s_jsmEntityCount; j++) {
            const FieldArchive::JSMEntityInfo& E = s_jsmEntities[j];
            bool watched = false;
            for (int w = 0; w < (int)(sizeof(kWatch)/sizeof(kWatch[0])); w++)
                if (_stricmp(E.symName, kWatch[w]) == 0) { watched = true; break; }
            if (!watched) continue;
            Log::Field("FieldNavigation: [PUZZLE-DIAG] jsm%d sym='%s' type=%d cat=%d "
                       "hasPos=%d pos=(%d,%d) tri=%u pshm=%d pshmAddr=(%d,%d) param=%d",
                       E.jsmIndex, E.symName, (int)E.type, E.jsmCategory,
                       E.hasPosition ? 1 : 0, (int)E.posX, (int)E.posY,
                       (unsigned)E.posTriangle, E.hasPshmCoords ? 1 : 0,
                       (int)E.pshmAddrX, (int)E.pshmAddrY, E.param);
        }
        // v0.18.3.269 DIAG-A (missing Mansion 5 exit): resolve VARBLOCK exit
        // destinations LIVE. A cat=3 Map Exit whose destination comes from a
        // field variable carries param = 0x80000000 | addr (see
        // field_archive_jsm_mapjump_resolver.inl: EXIT_VARBLOCK_BASE / marker
        // encoding, duplicated here because that .inl belongs to field_archive).
        // glfurin1 ent12 'eventline2' resolves to marker 0x800002EB, and the
        // engine oracle shows a MAPJUMP on this field landing on destField=725
        // ("Caraway's Mansion 5") -- so if varblock[0x02EB] reads 725 at catalog
        // time, that entity IS the missing statue-alcove transition and we can
        // both name and surface it instead of dropping it as unresolved.
        {
            static const uintptr_t VB_BASE = 0x01CFE9B8;  // EXIT_VARBLOCK_BASE
            for (int j = 0; j < s_jsmEntityCount; j++) {
                const FieldArchive::JSMEntityInfo& E = s_jsmEntities[j];
                if (E.type != FieldArchive::JSM_ENT_MAP_EXIT && E.param >= 0) continue;
                unsigned addr = (unsigned)(E.param & 0xFFFF);
                int vbW = -1, vbB = -1;
                if ((E.param & 0x80000000) != 0 && addr < 0x2000) {
                    vbW = (int)*(const uint16_t*)(VB_BASE + addr);
                    vbB = (int)*(const uint8_t*)(VB_BASE + addr);
                }
                Log::Field("FieldNavigation: [PUZZLE-DIAG] exit jsm%d sym='%s' type=%d "
                           "param=0x%08X hasPos=%d pos=(%d,%d) vbAddr=0x%04X vbWord=%d vbByte=%d",
                           E.jsmIndex, E.symName, (int)E.type, (unsigned)E.param,
                           E.hasPosition ? 1 : 0, (int)E.posX, (int)E.posY,
                           addr, vbW, vbB);
            }
        }

        // v0.18.3.269 DIAG-C: raw INF gateway inventory. The statue-alcove
        // transition might be an INF gateway that the destination-match or
        // screen-side filters discard before it reaches the catalog, rather than
        // a JSM Map Exit at all. Dump them unfiltered so we can tell.
        {
            Log::Field("FieldNavigation: [PUZZLE-DIAG] === INF gateways: %d ===", s_gatewayCount);
            for (int g = 0; g < s_gatewayCount; g++) {
                Log::Field("FieldNavigation: [PUZZLE-DIAG] gw%d destField=%u '%s' "
                           "center=(%.0f,%.0f) line=(%d,%d)->(%d,%d)",
                           g, (unsigned)s_gateways[g].destFieldId,
                           s_gateways[g].destFieldName,
                           s_gateways[g].centerX, s_gateways[g].centerZ,
                           (int)s_gateways[g].lineX1, (int)s_gateways[g].lineY1,
                           (int)s_gateways[g].lineX2, (int)s_gateways[g].lineY2);
            }
        }

        // v0.18.3.269 DIAG-B (glass label / line naming): the SETLINE hook records
        // an entityAddr per captured line, but on these fields the spacing (0x1A0)
        // does not match the 0x264 field-entity stride, so the existing
        // "captured line t -> JSM entity doors+t" mapping is unreliable and
        // interactions fall back to "Interaction N". Dump each line's addr
        // relative to the live entity-array base: if delta/0x264 lands on a whole
        // entity index we can name lines by their owning entity's SYM (and label
        // glfurin1's line1 at (146,500) "Glass"); if not, the addr belongs to some
        // other per-line structure and we need a different hook.
        {
            uint8_t* entBase = nullptr;
            if (FF8Addresses::pFieldStateOthers)
                entBase = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            Log::Field("FieldNavigation: [PUZZLE-DIAG] entityArrayBase=0x%08X stride=0x%X",
                       (unsigned)(uintptr_t)entBase, (unsigned)ENTITY_STRIDE);
            for (int t = 0; t < s_capturedLineCount; t++) {
                uint32_t a = s_capturedLines[t].entityAddr;
                long d = entBase ? (long)((uintptr_t)a - (uintptr_t)entBase) : 0;
                Log::Field("FieldNavigation: [PUZZLE-DIAG] line%d entAddr=0x%08X "
                           "delta=%ld idx=%ld rem=%ld",
                           t, (unsigned)a, d,
                           entBase ? d / (long)ENTITY_STRIDE : -1,
                           entBase ? d % (long)ENTITY_STRIDE : -1);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [PUZZLE-DIAG] exception during dump");
    }
}

static void DumpPartyStateOnce()
{
    if (s_partyDiagDumped) return;
    __try {
        // v0.18.3.263 (#83): log BOTH the savemap party array (0x01CFE74C) and the
        // FIELD controlled-party array the engine uses (0x01CFE990). In single-
        // party play they match; in split-party scenes (Caraway) they diverge and
        // the FIELD array is the team that follows you. This confirms which array
        // holds the controlled/leader team (Quistis-led party 2 per Aaron).
        const uint8_t* fSave  = (const uint8_t*)0x01CFE74C;
        const uint8_t* fField = (const uint8_t*)0x01CFE990;
        Log::Field("FieldNavigation: [party-state] savemap[0x1CFE74C] = [%u, %u, %u, %u]  "
                   "FIELD[0x1CFE990] = [%u, %u, %u]",
                   (unsigned)fSave[0], (unsigned)fSave[1], (unsigned)fSave[2], (unsigned)fSave[3],
                   (unsigned)fField[0], (unsigned)fField[1], (unsigned)fField[2]);
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
