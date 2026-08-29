// field_catalog.inl — consolidated FIELD entity-catalog assembly.
//
// v0.19.x CONSOLIDATION. The 8-file catalog assembly —
//   field_nav_catalog_diag.inl, field_nav_catalog_lateres.inl,
//   field_nav_catalog.inl, and the five mid-function fragments
//   (naming / triglines / mapexits / gateways / dedupe) plus the two inline
//   blocks (interaction injection, JSM special-entity injection) —
// merged into ONE file of real functions, defined before use, inside
// namespace FieldNavigation. This is CODE MOTION, byte-for-byte behavior-
// identical to the pre-consolidation assembly (proven over the 28-fixture
// cat_harness/run_diff battery). Every extracted body is verbatim; the five
// `#include`s and the two inline blocks became:
//   RefineEntityTypeAndName / InjectTriggerLineExits / InjectInteractionLines /
//   InjectJsmSpecials / InjectMapExits / InjectGatewayExits / DedupeCatalog,
// called by RefreshCatalog at the same points, in the same order (dedupe last).
// The shared newCatalog[]/newCount accumulator (plus base/lim/fresh where a
// block needs them) is threaded as parameters; all s_* state stays file-scope.
//
// Included from field_navigation.cpp inside namespace FieldNavigation, AFTER the
// headers and the .inl files that define the statics/helpers this uses
// (field_nav_helpers.inl, field_nav_names.inl, field_nav_pathfinding.inl, ...).
// Replaces the former #includes of _diag/_lateres/_catalog. Do not compile
// independently. Original per-fragment version-history headers are preserved
// verbatim inside each section below.


// ============================================================================
// 1. One-shot diagnostic dumps  (was field_nav_catalog_diag.inl)
// ============================================================================
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
    // v0.18.3.285 (#85): print TWO candidate sym-name lookups per slot, not
    // just one. s_symOthersOffset is hardcoded to 0 everywhere in this
    // codebase (never actually computed as doors+lines+backgrounds despite
    // its own comment saying it should be) so `sym0` below is really "flat
    // jsmIndex == this slot" -- i.e. the runtime Others array uses the SAME
    // numbering as the JSM's combined Door+Line+Bg+Other scan order, no
    // offset. `symC` is the OTHER candidate: "this slot is Other-entity-only
    // compact, offset by doors+lines+backgrounds" (the convention 3 existing
    // functions -- ResolveLatePositions, MatchSet3LateCaptures,
    // ResolveStructPositions -- already assume). Cross-field evidence
    // disagrees on which is right (glprein1's trapdoor position landed on a
    // real dead-end walkmesh cluster using sym0's convention; glwater3's
    // 'book'/'ladline5' and glclock1's 'rinoa'/'jumpline0' pairs read
    // byte-identical struct data using symC's convention despite being
    // different named entities) -- printing both side by side is how we
    // settle it instead of guessing again.
    int othStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
    Log::Field("FieldNavigation: [EXTSCAN] === reported otherCount=%d, scanning 0..%d, othStart=%d ===",
               (int)entCount, MAX_ENTITIES - 1, othStart);
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
        int symIdx0 = s_symOthersOffset + i;   // current (buggy offset=0) lookup, == flat jsmIndex==i
        int symIdxC = othStart + i;            // corrected: Other-compact, WITH othStart offset
        const char* sym0 = (symIdx0 >= 0 && symIdx0 < s_symNameCount) ? s_symNames[symIdx0] : "(none)";
        const char* symC = (symIdxC >= 0 && symIdxC < s_symNameCount) ? s_symNames[symIdxC] : "(none)";
        Log::Field("FieldNavigation: [EXTSCAN] slot%-2d %s sym0='%s' symC='%s' model=%d tri=%u setpc=%d "
                   "talk=%d push=%d thru=%d fp=(%d,%d)",
                   i, (i < (int)entCount) ? "IN " : "OUT", sym0, symC,
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
        // v0.18.3.293 (#85): 'saku1'/'ladline5' added so the Deling sewer rooms
        // qualify -- without them this whole function early-returns on glwater*
        // and the STATE-DIAG probe below would never fire. Re-armed per field
        // load (field_nav_fieldscripts.inl), so entering the room in each state
        // gives one log line per state, which is exactly the comparison needed.
        static const char* kWatch[] = { "cup", "megami", "te", "kakusi", "kidou",
                                        "saku1", "ladline5" };
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

        // v0.18.3.293 (#85) STATE-DIAG: sewer room state variables.
        //
        // Aaron backtracked to this room on a later save and screenshotted it:
        // the tall vertical ladder that stood right of the raised block in the
        // earlier shots is GONE, replaced by a diagonal plank -- the shortcut
        // ladder, already knocked down. Same room, two different world states.
        //
        // The init scripts explain the "duplicate objects" he keeps hearing.
        // glwater3's ladline5 and ladline6 both guard their SET3 on the SAME
        // varblock (0x0154 = 340), compared against DIFFERENT values:
        //     ladline5: PSHM_L 340 / PSHM_W 9  ... JPF ... SET3 tri=186
        //     ladline6: PSHM_L 340 / PSHM_W 0  ... SET3 tri=175
        // That is a state machine: one model is the standing ladder, the other
        // the fallen one, and only ONE exists in the world at a time. The
        // catalog is built from STATIC script data, so it injects both
        // unconditionally -- which is exactly Aaron's "two or three objects at
        // the location where I believe the shortcut ladder is". ladline7 (the
        // confirmed Gate) and saku1/saku2 read the same 340 plus 337/339.
        //
        // Before gating injection on these values, PROVE the mapping: log the
        // live varblock at catalog time, and have Aaron BAT the room in both
        // states (ladder standing vs knocked down). Read-only, no behavior
        // change -- deliberately a diagnostic first, because the opcode
        // simulator does not fully decode the JMP/JMPB/JPF compare semantics,
        // so the exact "which value means which state" is inferred, not proven.
        {
            static const uintptr_t VB_BASE293 = 0x01CFE9B8;  // EXIT_VARBLOCK_BASE
            const unsigned addrs293[] = { 0x0151, 0x0153, 0x0154 };  // 337, 339, 340
            for (int a = 0; a < 3; a++) {
                unsigned ad = addrs293[a];
                Log::Field("FieldNavigation: [STATE-DIAG] varblock[0x%04X] (%u) "
                           "word=%d byte=%d [v0.18.3.293 #85 room-state probe]",
                           ad, ad,
                           (int)*(const uint16_t*)(VB_BASE293 + ad),
                           (int)*(const uint8_t*)(VB_BASE293 + ad));
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

// ============================================================================
// 2. Late position resolution + state exclusion  (was field_nav_catalog_lateres.inl)
// ============================================================================
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

// ============================================================================
// 3a. Per-entity type refinement + display name  (was field_nav_catalog_naming.inl)
// ============================================================================
static const char* RefineEntityTypeAndName(int i, int16_t modelId, uint8_t setpc,
                                           const char* symName, bool talkable, EntityInfo& ei_info)
{
// field_nav_catalog_naming.inl — entity type-refinement + display naming.
//
// v0.18.3.235: Extracted from RefreshCatalog() in field_nav_catalog.inl to keep
// that file under the 80 KB source-size CI guard (it had reached 81 KB with only
// ~700 bytes of headroom, and the .235 fixes pushed it over).
//
// This is NOT a standalone function. It is a statement fragment #included at ONE
// point inside RefreshCatalog's per-entity loop — the same pattern as
// field_nav_catalog_dedupe.inl. It sees the loop locals (i, modelId, setpc,
// symName, talkable, ei_info) and the file-scope catalog state, and it DECLARES
// `entName`, which the including scope uses immediately afterwards to fill
// ei_info.name. Do not compile independently; do not include anywhere else.
//
// Behavior is identical to the pre-extraction source apart from the v0.18.3.235
// JSM-type guard documented below.

// v0.07.73: Look up JSM classification by SYM name.
// Overrides generic "NPC" with specific type (Save Point, Draw Point, etc.)
const char* entName = "NPC";
// v0.62.0: the SYM string reaches this fragment as the `symName` parameter,
// resolved by the model join in the scan loop. The old `s_symOthersOffset + i`
// index is gone -- it named the i-th script slot, and the live array this loop
// walks is not indexed by slot.
// v0.58.0: the script for runtime Others slot i is JSM group
// (nLines + nDoors + nBackgrounds + i) -- an identity, not a name match. The
// name lookup this replaced could land on any entity sharing the string, and
// on the fields whose SYM order the scanner used to get wrong it landed on a
// completely different one. Fall back to the name only if the scanner did not
// record a runtime slot (an old archive path, or a field with no JSM).
{
    const FieldArchive::JSMEntityInfo* jsm = FindJSMByLiveEntity(i);   // v0.62.0
    if (jsm) {
        EntityType jsmType = JSMTypeToCatalogType(jsm->type);
        // v0.18.3.235: a SYM-derived JSM type must NOT downgrade a VISIBLE
        // CHARACTER to a generic Object/Interaction.
        //
        // The SYM->slot map is unreliable on any field that instantiates only a
        // subset of its SYMs, so this lookup can be answering about a completely
        // different entity. On ggroom1 (G-Garden reception) a party member
        // standing in the scene (model 1) resolved to SYM 'zell' = a JSM
        // "Interactive Object", so a person was announced to the player as
        // "Object". A wrong name is survivable; a wrong TYPE is not.
        //
        // Specific, meaningful types (Save/Draw/Shop/Card) may still override --
        // they are what this lookup exists for, and the position-based dedupe in
        // field_nav_catalog_dedupe.inl cross-checks the save point independently.
        // Entities with no visible model (modelId < 0) are script objects rather
        // than characters and keep the original behavior.
        bool jsmSpecial = (jsmType == ENT_SAVE_POINT ||
                           jsmType == ENT_DRAW_POINT ||
                           jsmType == ENT_SHOP ||
                           jsmType == ENT_CARD_GAME);
        if (jsmType != ENT_UNKNOWN && (jsmSpecial || modelId < 0)) {
            ei_info.type = jsmType;
            entName = EntityTypeName(jsmType);
        }
    }
}
// v0.12.10: Comprehensive SYM-name entity type classification.
// Uses ENTITY_TYPE_TABLE from survey data, with pattern fallbacks.
if (ei_info.type == ENT_NPC || ei_info.type == ENT_OBJECT || ei_info.type == ENT_UNKNOWN) {
    if (symName && symName[0]) {
        const char* sym = symName;
        // First: check comprehensive type table from entity_classifications.h
        EntityClassificationType ecType = LookupEntityType(sym);
        if (ecType == EC_DRAW_POINT) {
            ei_info.type = ENT_DRAW_POINT;
            entName = "Draw Point";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Draw Point by type table", i, sym);
        } else if (ecType == EC_SAVE_POINT) {
            ei_info.type = ENT_SAVE_POINT;
            entName = "Save Point";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Save Point by type table", i, sym);
        } else if (ecType == EC_SHOP) {
            ei_info.type = ENT_SHOP;
            entName = "Shop";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Shop by type table", i, sym);
        } else if (ecType == EC_CARD_GAME) {
            ei_info.type = ENT_CARD_GAME;
            entName = "Card Player";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' classified as Card Game by type table", i, sym);
        } else {
            // Pattern-based fallback for names not in the table
            if ((sym[0] == 'd' || sym[0] == 'D') && (sym[1] == 'p' || sym[1] == 'P') &&
                sym[2] >= '0' && sym[2] <= '9') {
                ei_info.type = ENT_DRAW_POINT;
                entName = "Draw Point";
            } else if (_strnicmp(sym, "drpoint", 7) == 0 ||
                       _strnicmp(sym, "drawpoint", 9) == 0 ||
                       _strnicmp(sym, "draw_point", 10) == 0) {
                ei_info.type = ENT_DRAW_POINT;
                entName = "Draw Point";
            } else if (_strnicmp(sym, "save", 4) == 0 || _strnicmp(sym, "svpt", 4) == 0) {
                ei_info.type = ENT_SAVE_POINT;
                entName = "Save Point";
            }
        }
    }
}
// v0.12.09: Cross-entity draw point trigger detection.
// If this entity's JSM info shows it calls REQSW/REQEW to a draw point entity,
// classify it as Draw Point. This is deterministic — no proximity heuristics.
if (ei_info.type == ENT_NPC || ei_info.type == ENT_OBJECT || ei_info.type == ENT_UNKNOWN) {
    const FieldArchive::JSMEntityInfo* jsmDP = FindJSMByLiveEntity(i);   // v0.62.0
    if (jsmDP && jsmDP->drawPointTriggerOf >= 0) {
        ei_info.type = ENT_DRAW_POINT;
        entName = "Draw Point";
        Log::Field("FieldNavigation: [catalog] ent%d '%s' reclassified as Draw Point "
                   "(triggers JSM draw point ent%d)",
                   i, jsmDP->symName, jsmDP->drawPointTriggerOf);
    }
}
// v0.07.79: Model-based save point detection.
// Model 24 is the save point crystal across all FF8 fields. The visible save
// point entity often has a different SYM index than the save point script entity
// (e.g. bghall_1 ent6 vs JSM ent27), so SYM-based lookup misses it. Model ID is
// authoritative.
if (modelId == 24 && ei_info.type != ENT_SAVE_POINT) {
    ei_info.type = ENT_SAVE_POINT;
    entName = "Save Point";
}
// v0.18.3.227: Label interactable party members by proper name.
// v0.18.3.232: name party characters from setpc, NOT the SYM.
//
// A party character only reaches here if the party filter KEPT it — i.e. it is
// talkable — so announcing "Squall"/"Quistis" instead of a generic "NPC" is
// accurate and more useful. Deriving the name from setpc (the character ID)
// rather than the SYM is what makes it SAFE: the SYM list is shifted relative to
// the runtime slots, so a SYM-based label would happily announce the G-Garden
// train guard as "Rinoa". setpc is 0xFE on every genuine NPC, so no NPC, draw
// point or save point can be mislabeled as a party member.
if (ei_info.type == ENT_NPC) {
    const char* partyName = PartyCharacterNameById(setpc);
    if (partyName) {
        entName = partyName;
        Log::Field("FieldNavigation: [catalog] ent%d setpc=%d labeled as party "
                   "member '%s' (sym='%s' talkable=%d)",
                   i, (int)setpc, partyName, symName, talkable ? 1 : 0);
    }
}
    return entName;
}

// ============================================================================
// 3b. Trigger-line Exit / Event injection  (was field_nav_catalog_triglines.inl)
// ============================================================================
// v0.20.15: forward decl -- CarawayMansionSealed() is defined later (before
// InjectGatewayExits) but is used by InjectInteractionLines and InjectJsmSpecials
// (both earlier) to gate the Caraway's Mansion puzzle entities.
static bool CarawayMansionSealed();

// ============================================================================
// v0.20.19 (catalog revamp WS1 Step 1.1): OBSERVE-ONLY catalog audit.
// For every entry each injection path is about to KEEP, emit one uniform
// [CAT-AUDIT] line carrying the live-state signals available at that site, so a
// BAT across the reference fields shows -- per field -- exactly what the catalog
// announces and which signal would gate each bloat entry, BEFORE any behaviour
// change (WS1 Step 1.3). Pure logging: a scoped brace block before each push;
// no branch, no continue, no mutation. The existing per-path skip logs
// (controller/effect, mansion-sealed, gateway-filter) still cover the DROP side.
// One-line switch-off via s_catAudit. Reads only mod-side data (no live engine
// memory), so it is safe in the offline catalog harness.
// ============================================================================
static const bool s_catAudit = true;
static void CatAudit(const char* path, const EntityInfo& e, int x, int y,
                     const char* sym, const char* detail)
{
    if (!s_catAudit) return;
    Log::Field("FieldNavigation: [CAT-AUDIT] field=%s path=%s type=%d name='%s' "
               "pos=(%d,%d) sym='%s' ctrl=%d | %s",
               s_currentFieldName, path, (int)e.type, e.name, x, y,
               (sym && *sym) ? sym : "-",
               (sym && *sym && IsBgControllerName(sym)) ? 1 : 0,
               detail ? detail : "");
}
// ============================================================================
// v0.20.20 (catalog revamp WS1): camera-zone transition recognizer.
// A SCREEN_BOUND captured line whose resolved destination is a DIFFERENT, real,
// walkable field is a camera-zone transition -- walking across it moves the
// player to the adjacent section of the same logical space (e.g. B-Garden
// Classroom 1 -> Classroom 3 across the 'Selphie' line). Aaron's rule (v0.20.19
// BAT): announce it as "Exit to <dest field's display name>", NOT demote it to a
// nameless Interaction just because the line also REQs scene dialog (Classroom's
// 'Selphie' is the scene director AND the pan). Self-loop lines (dest == current
// field: dorm beds, prison stairs) and world-map staging dests are NOT zone
// transitions and keep their existing handling.
// ============================================================================
// v0.20.23: the v0.20.20 camera-zone-transition recognizer was REMOVED. It promoted a
// SCREEN_BOUND line with a different-field dest to an Exit even when it REQs dialog, which
// mis-labeled Squall's classroom desk (bgroom_1 line6 'Selphie': REQs Quistis, and walking
// into it MAPJUMPs to Classroom 3 as a scene consequence) as an exit. A dialog-REQ
// SCREEN_BOUND line is a DIALOG-GATED interaction (desk/bed), not a walk-across transition;
// the original rule (dialog-REQ -> Interaction) is correct and is restored below.

// ============================================================================
// v0.20.21 (catalog revamp WS1): topological camera-zone filter.
// The old far-zone filter tested whether the STRAIGHT line player->entity crosses
// a screen-bound segment -- fragile: from some positions that line slips past the
// segment's end and a different-zone entry leaks (classroom BAT: the far hallway
// exit was kept in 26 refreshes). This adds a TOPOLOGICAL test: flood-fill the
// walkmesh outward from the player's triangle, refusing to step across an active
// screen-bound segment (camera boundaries = walls); an entry is hidden only if it
// sits in a triangle flood-fill could NOT reach. FAIL-SAFE (#88): no walkmesh /
// unknown player triangle / unplaceable entry => s_zoneValid stays false or the
// entry is KEPT, so the worst case is a leftover leak for one more BAT, never a
// dropped reachable exit. Computed once per RefreshCatalog. Runs ALONGSIDE the
// existing straight-line filter (either may hide; neither hides a reachable entry).
// ============================================================================
static const int ZONE_MAX_TRI = 4096;   // matches field_navigation.cpp MAX_TRI_ID; self-contained for the harness
static bool s_zoneReachable[ZONE_MAX_TRI] = {};
static bool s_zoneValid = false;
static bool s_zoneBoundaryLine[MAX_CAPTURED_LINES] = {};  // v0.20.22: screen-bound lines that bound the player's zone -- their exits/transitions stay visible

static uint16_t NearestWalkTriangle(float x, float y)
{
    if (!s_walkmesh.valid || !s_walkmesh.triangles || s_walkmesh.numTriangles <= 0) return 0xFFFF;
    int best = -1; float bestD = 3.0e30f;
    int n = s_walkmesh.numTriangles; if (n > ZONE_MAX_TRI) n = ZONE_MAX_TRI;
    for (int i = 0; i < n; i++) {
        float dx = s_walkmesh.triangles[i].centerX - x;
        float dy = s_walkmesh.triangles[i].centerY - y;
        float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best < 0 ? (uint16_t)0xFFFF : (uint16_t)best;
}

static bool ZoneReachablePoint(float x, float y)
{
    if (!s_zoneValid) return true;                                   // filtering disabled -> KEEP
    uint16_t tri = NearestWalkTriangle(x, y);
    if (tri == 0xFFFF || tri >= (uint16_t)s_walkmesh.numTriangles) return true;  // can't place -> KEEP
    return s_zoneReachable[tri];
}

// A captured line straddles zones at a boundary, so KEEP it if ANY of its center or
// two endpoints is in the player's zone -- this keeps the forward camera-pan
// transition (whose far endpoint is across the line) while still hiding a line that
// lies entirely in another zone (the classroom's far hallway exit).
static bool ZoneReachableLine(int t)
{
    if (!s_zoneValid) return true;
    if (t < 0 || t >= s_capturedLineCount) return true;
    float cx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
    float cy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
    return ZoneReachablePoint(cx, cy)
        || ZoneReachablePoint((float)s_capturedLines[t].x1, (float)s_capturedLines[t].y1)
        || ZoneReachablePoint((float)s_capturedLines[t].x2, (float)s_capturedLines[t].y2);
}

static void ComputePlayerZoneReachability()
{
    s_zoneValid = false;
    for (int i = 0; i < ZONE_MAX_TRI; i++) s_zoneReachable[i] = false;
    for (int i = 0; i < MAX_CAPTURED_LINES; i++) s_zoneBoundaryLine[i] = false;
    if (!s_walkmesh.valid || !s_walkmesh.triangles || s_walkmesh.numTriangles <= 0) return;
    int nTri = s_walkmesh.numTriangles;
    if (nTri > ZONE_MAX_TRI) return;                                   // oversize -> disable (bias KEEP)

    // Player's current triangle: live entity field +0x1FA (same read auto-drive uses).
    uint16_t playerTri = 0xFFFF;
    if (FF8Addresses::pFieldStateOthers && s_playerEntityIdx >= 0) {
        uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (base2)
            playerTri = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
    }
    if (playerTri == 0xFFFF || playerTri >= (uint16_t)nTri) return;  // unknown -> disable (bias KEEP)

    static int bfsQueue[ZONE_MAX_TRI];
    int qh = 0, qt = 0;
    s_zoneReachable[playerTri] = true;
    bfsQueue[qt++] = playerTri;
    while (qh < qt) {
        int cur = bfsQueue[qh++];
        const auto& tc = s_walkmesh.triangles[cur];
        for (int e = 0; e < 3; e++) {
            uint16_t nb = tc.neighbor[e];
            if (nb == 0xFFFF || nb >= (uint16_t)nTri || s_zoneReachable[nb]) continue;
            const auto& tn = s_walkmesh.triangles[nb];
            bool blocked = false;
            for (int k = 0; k < s_capturedLineCount; k++) {
                if (!s_capturedLines[k].active) continue;
                if (s_capturedLines[k].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                if (SegmentsCross(tc.centerX, tc.centerY, tn.centerX, tn.centerY,
                                  (float)s_capturedLines[k].x1, (float)s_capturedLines[k].y1,
                                  (float)s_capturedLines[k].x2, (float)s_capturedLines[k].y2)) {
                    blocked = true;                              // this line walls the player's zone here
                    if (k < MAX_CAPTURED_LINES) s_zoneBoundaryLine[k] = true;
                }
            }
            if (blocked) continue;                                  // camera boundary between cur and nb
            s_zoneReachable[nb] = true;
            bfsQueue[qt++] = nb;
        }
    }
    s_zoneValid = true;
    Log::Field("FieldNavigation: [refresh] zone-filter active: player tri=%d, %d/%d triangles reachable [v0.20.21]",
               (int)playerTri, qt, nTri);
}

// ============================================================================
// v0.20.50 (WS1 Step 1.3) -- unified live-state gate, OBSERVE-ONLY (log-only).
// The catalog's live-state checks are scattered across 7 assembly paths; this is
// the single policy point every path reports through, so a BAT shows PER FIELD
// exactly what the gate WOULD decide and why, BEFORE any signal is enforced.
// Signals are tri-state: -1 unknown/not-applicable, 0 no, 1 yes. The verdict errs
// toward KEEP -- it says would-DROP only on a confidently not-live signal. NOTHING
// is dropped here: the existing per-path filters still own behavior; this emits a
// [LIVE-GATE] line only. Step 1.3 flips signals from log-only to enforcing one at a
// time in later builds, each validated against the reference expected-sets.
struct LiveSignals { int visible, active, talkable, lineActive, zoneReachable, gatewayEnabled; };
static LiveSignals LiveSignalsInit() { LiveSignals s = { -1, -1, -1, -1, -1, -1 }; return s; }
static const bool s_liveGate = true;   // one-switch off, like s_catAudit

// SEH-guarded read of a live "others" entity's flag word (+0x160: bit3=HIDE,
// bit1=USE/active, per exe RE) and talk-enable byte (+0x24B). Applies the slot-alias
// guard (v0.20.36/.43): the engine's others array tracks only a window, so a slot
// whose live triangle (+0x1FA) disagrees with the entity's own static SET3 triangle
// belongs to a DIFFERENT entity and its flags are not ours -- reject that read.
static bool ReadLiveEntityFlags(int otherIdx, uint16_t ownTri, uint32_t* outFlags, uint8_t* outTalk)
{
    __try {
        if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return false;
        uint8_t* ob = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        int ocnt = (int)(*FF8Addresses::pFieldStateOtherCount);
        if (!ob || otherIdx < 0 || otherIdx >= ocnt) return false;
        uint8_t* blk = ob + ENTITY_STRIDE * otherIdx;
        uint16_t ltri = *(uint16_t*)(blk + 0x1FA);
        if (ownTri != 0 && ltri != ownTri) return false;   // aliased slot -- flags not ours
        if (outFlags) *outFlags = *(uint32_t*)(blk + 0x160);
        if (outTalk)  *outTalk  = *(uint8_t*)(blk + 0x24B);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The unified live-state policy. OBSERVE-ONLY: logs [LIVE-GATE], returns the
// would-keep verdict (callers currently ignore it -- behavior unchanged).
static bool CatalogEntryIsLiveNow(const char* path, const EntityInfo& e, const LiveSignals& sg)
{
    bool keep = true; const char* reason = "live";
    if      (sg.visible        == 0) { keep = false; reason = "hidden (HIDE bit)"; }
    else if (sg.active         == 0) { keep = false; reason = "inactive (UNUSE bit)"; }
    else if (sg.lineActive     == 0) { keep = false; reason = "line off (LINEOFF)"; }
    else if (sg.gatewayEnabled == 0) { keep = false; reason = "gateway disabled"; }
    // v0.20.52: zone-reachability is POSITIONAL, not entity live-state, and the v0.20.51 BAT proved a
    // uniform zone-drop OVER-DROPS -- it flagged a talkable NPC, an exit, and a camera transition (all
    // reachable-to-USE across a camera-zone boundary) as would-DROP. The per-path code already applies
    // zone with the correct exemptions (gateways/exits exempt; talk-across-a-gap NPCs kept). The gate
    // owns the ENTITY-level spine only (visible / active / line / gateway); zone stays logged for context.
    if (s_liveGate)
        Log::Field("FieldNavigation: [LIVE-GATE] field=%s path=%s name='%s' "
                   "vis=%d act=%d talk=%d line=%d zone=%d gw=%d -> %s (%s) [v0.20.50 WS1-1.3 observe]",
                   s_currentFieldName, path, e.name,
                   sg.visible, sg.active, sg.talkable, sg.lineActive, sg.zoneReachable, sg.gatewayEnabled,
                   keep ? "KEEP" : "would-DROP", reason);
    return keep;
}

// Per-path resolvers: fill only the signals a path can see, then call the gate.
static void LiveGateLine(const char* path, const EntityInfo& e, int t)
{
    LiveSignals ls = LiveSignalsInit();
    if (t >= 0 && t < s_capturedLineCount) {
        ls.lineActive = s_capturedLines[t].active ? 1 : 0;
        if (s_zoneValid) ls.zoneReachable = ZoneReachableLine(t) ? 1 : 0;
    }
    CatalogEntryIsLiveNow(path, e, ls);
}
static void LiveGateObject(const char* path, const EntityInfo& e, int jsmIndex, uint16_t ownTri, int x, int y)
{
    LiveSignals ls = LiveSignalsInit();
    int oidx = jsmIndex - (s_jsmDoors + s_jsmLines + s_jsmBackgrounds);
    uint32_t fl = 0; uint8_t tk = 0;
    if (ReadLiveEntityFlags(oidx, ownTri, &fl, &tk)) {
        ls.visible  = ((fl & 0x08) == 0) ? 1 : 0;
        ls.active   = ((fl & 0x02) != 0) ? 1 : 0;
        ls.talkable = (tk != 0) ? 1 : 0;
    }
    if (s_zoneValid) ls.zoneReachable = ZoneReachablePoint((float)x, (float)y) ? 1 : 0;
    CatalogEntryIsLiveNow(path, e, ls);
}
static void LiveGatePos(const char* path, const EntityInfo& e, int x, int y, bool applyZone)
{
    LiveSignals ls = LiveSignalsInit();
    if (applyZone && s_zoneValid) ls.zoneReachable = ZoneReachablePoint((float)x, (float)y) ? 1 : 0;
    CatalogEntryIsLiveNow(path, e, ls);
}

// v0.20.51: runtime "others" entities (NPCs etc.). entityIdx directly indexes the live
// others array, so it IS this entity's own slot -- no alias risk, pass ownTri 0 to skip
// that guard. This is the path that actually carries the visible/active/talkable flags,
// which is why the v0.20.50 BAT saw them all -1 (only JSM objects were wired, and those
// were positionless Draw Points with no live block).
static void LiveGateRuntime(const char* path, const EntityInfo& e)
{
    LiveSignals ls = LiveSignalsInit();
    if (e.entityIdx >= 0) {
        uint32_t fl = 0; uint8_t tk = 0;
        if (ReadLiveEntityFlags(e.entityIdx, 0, &fl, &tk)) {
            ls.visible  = ((fl & 0x08) == 0) ? 1 : 0;
            ls.active   = ((fl & 0x02) != 0) ? 1 : 0;
            ls.talkable = (tk != 0) ? 1 : 0;
        }
    }
    if (s_zoneValid && e.triangleId != 0 && (int)e.triangleId < ZONE_MAX_TRI)
        ls.zoneReachable = s_zoneReachable[e.triangleId] ? 1 : 0;
    CatalogEntryIsLiveNow(path, e, ls);
}

static void InjectTriggerLineExits(EntityInfo* newCatalog, int& newCount)
{
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
                    // v0.62.3 (#123): its own script is gated shut -- crossing it does
                    // nothing, so it is not an exit yet.
                    if (s_capturedLines[t].gateClosed) continue;

                    // v0.20.29: camera-view transition line (routed to SCREEN_BOUND in
                    // linetypes so it walls the zone BFS). Emit as a "Camera transition"
                    // exit with no field destination, zone-filtered like any exit.
                    if (s_capturedLines[t].isCameraTransition) {
                        float ctcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                        float ctcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;

                        // v0.57.1 (#110): THE ONE LINE THE PLAYER MUST REACH.
                        //
                        // During the Lunatic Pandora run, six of these camera
                        // lines ARE the boarding triggers -- walking through one
                        // in its window is what takes you aboard. In the
                        // 2026-08-22 BAT the mod correctly told Aaron to cross
                        // eccway21's, and the catalog had already dropped it:
                        //
                        //   [refresh] camera-transition line0 filtered:
                        //             not a boundary of player's zone [v0.20.29]
                        //
                        // He spent eight minutes bouncing between four fields
                        // looking for an exit that was not in his catalog, and
                        // missed contact point 1 entirely. In eccway41 the same
                        // kind of line SURVIVED the filter, was listed (as
                        // "Camera transition"), and he reached contact point 2
                        // with the GPS on the first try. Same mechanism, and the
                        // only difference was whether it was in the list.
                        //
                        // The zone filter is right in general -- it stops the
                        // catalog offering camera lines you cannot walk to. It
                        // is wrong about the objective of a timed minigame. So a
                        // contact-point line is exempt while the run is live,
                        // and it is named for what it is rather than "Camera
                        // transition", which tells the player nothing.
                        const EstharSite* epLine =
                            EpContactLineAt(s_currentFieldName, (int)ctcx, (int)ctcy);
                        char camName[48];
                        if (epLine) {
                            snprintf(camName, sizeof(camName), "Contact point %d",
                                     epLine->cp + 1);
                        } else {
                            strncpy(camName, "Camera transition", sizeof(camName) - 1);
                            camName[sizeof(camName) - 1] = '\0';
                        }
                        if (!epLine && s_zoneValid && !ZoneReachableLine(t) &&
                            !(t >= 0 && t < MAX_CAPTURED_LINES && s_zoneBoundaryLine[t])) {
                            Log::Field("FieldNavigation: [refresh] camera-transition line%d filtered: not a boundary of player's zone [v0.20.29]", t);
                            continue;
                        }
                        if (epLine && s_zoneValid && !ZoneReachableLine(t) &&
                            !(t >= 0 && t < MAX_CAPTURED_LINES && s_zoneBoundaryLine[t])) {
                            Log::Field("FieldNavigation: [refresh] line%d is Esthar contact point %d "
                                       "-- KEPT despite the zone filter [v0.57.1 #110]",
                                       t, epLine->cp + 1);
                        }
                        EntityInfo camExit = {};
                        camExit.entityIdx  = -200 - t;
                        camExit.modelId    = -1;
                        camExit.triangleId = 0;
                        camExit.type       = ENT_EXIT;
                        camExit.gatewayIdx = -1;
                        strncpy(camExit.name, camName, sizeof(camExit.name) - 1);
                        camExit.name[sizeof(camExit.name) - 1] = '\0';
                        CatAudit("camera-transition", camExit, (int)ctcx, (int)ctcy, "-", s_capturedLines[t].name);
                        LiveGateLine("camera-transition", camExit, t);
                        newCatalog[newCount++] = camExit;
                        Log::Field("FieldNavigation: [refresh] camera-transition EXIT line%d '%s' center=(%.0f,%.0f) [v0.20.29]",
                                   t, s_capturedLines[t].name, ctcx, ctcy);
                        continue;
                    }
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
                    // v0.20.12: resolver flagged this line as a story-locked exit
                    // (Mansion 4/5 doors before the puzzle). Drop it whole -- NO
                    // gateway-recovery (that borrow is what mislabeled it "Exit to
                    // Mansion 6"), NO catalog entry. It re-appears when the gate opens
                    // on the next field load.
                    if ((uint32_t)destId == (uint32_t)FieldArchive::EXIT_LOCKED_MARKER) {
                        Log::Field("FieldNavigation: [refresh] SETLINE line%d center=(%.0f,%.0f) "
                                   "LOCKED (story gate false) -- suppressed [v0.20.12]", t, tcx, tcy);
                        continue;
                    }
                    // v0.20.16: Caraway's Mansion 3 (glfurin5) -- the door Quistis heads for
                    // to leave the mansion springs the "Rinoa runs back in" cutscene. Its
                    // MAPJUMP destination is a runtime var the interpreter could not resolve
                    // (reason=5 underflow), so it kept a VARBLOCK marker whose low word is
                    // 0x2D6 = 726 = Mansion 6 -- the real destination -- but glfurin5 has no
                    // local INF gateway for the recovery step to borrow a name from, so it
                    // fell through to a bare "Exit". Adopt the marker's addr as the dest so it
                    // reads as the Mansion 6 door it is: a blind player heads for it and gets
                    // the same cutscene surprise a sighted player does. Label-only -- the
                    // SETLINE-exit path applies no duplicated-room suppression, so the exit
                    // stays visible; only its name changes. Scoped to mansion + addr 726.
                    if (((uint32_t)destId & 0x80000000u) &&
                        ((uint32_t)destId & 0xFFFFu) == 726u &&
                        strncmp(s_currentFieldName, "glfurin", 7) == 0) {
                        Log::Field("FieldNavigation: [refresh] SETLINE line%d center=(%.0f,%.0f) "
                                   "marker=0x%08X addr=726 -> 'Exit to Mansion 6' (Rinoa-cutscene "
                                   "door) [v0.20.16]", t, tcx, tcy, (unsigned)destId);
                        destId = 726;
                    }
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

                    // v0.65.0: ...unless this line is one the table renames.
                    // Aaron, in the escape pod: "there is an empty capsule
                    // Squall has to enter and it is being identified as an exit
                    // to Desert. It should just read out as 'Capsule'." The
                    // destination is not wrong -- field 638 IS Desert 1 -- it is
                    // just not what he is walking towards. Line t pairs with
                    // s_jsmEntities[t] (see [LINE-PAIR]), which carries the SYM.
                    if (t >= 0 && t < s_jsmEntityCount) {
                        const FieldScopedEntity* fs =
                            FieldScopedFor(FF8Addresses::pCurrentFieldName,
                                           s_jsmEntities[t].symName);
                        if (fs && fs->display && fs->display[0]) {
                            Log::Field("FieldNavigation: [refresh] line%d '%s' renamed "
                                       "'%s' -> '%s' (field-scoped) [v0.65.0]",
                                       t, s_jsmEntities[t].symName, exitName, fs->display);
                            strncpy(exitName, fs->display, sizeof(exitName) - 1);
                            exitName[sizeof(exitName) - 1] = '\0';
                        }
                    }

                    EntityInfo trigExit = {};
                    trigExit.entityIdx  = -200 - t;
                    trigExit.modelId    = -1;
                    trigExit.triangleId = 0;
                    trigExit.type       = ENT_EXIT;
                    trigExit.gatewayIdx = -1;
                    strncpy(trigExit.name, exitName, sizeof(trigExit.name) - 1);
                    trigExit.name[sizeof(trigExit.name) - 1] = '\0';
                    { char caBuf[48]; snprintf(caBuf, sizeof caBuf, "destId=%d", destId);
                        CatAudit("setline-exit", trigExit, (int)tcx, (int)tcy, "-", caBuf); LiveGateLine("setline-exit", trigExit, t); }
                    if (s_zoneValid && !ZoneReachableLine(t) && !(t >= 0 && t < MAX_CAPTURED_LINES && s_zoneBoundaryLine[t])) { Log::Field("FieldNavigation: [refresh] '%s' filtered: exit lies wholly in another camera zone (not a boundary of the player's zone) [v0.20.22]", trigExit.name); continue; }
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
                    { CatAudit("trigger-event", evEntry, (int)tcx, (int)tcy, "-", "generic Event line"); LiveGateLine("trigger-event", evEntry, t); }
                    if (s_zoneValid && !ZoneReachableLine(t)) { Log::Field("FieldNavigation: [refresh] '%s' filtered: another camera zone (unreachable from player) [v0.20.21]", evEntry.name); continue; }
                    newCatalog[newCount++] = evEntry;
                }
            }
        }
}

// ============================================================================
// 3c. Interaction-line injection  (was inline block, field_nav_catalog.inl :535-715)
// ============================================================================
// v0.20.34 (Aaron): recognize signpost / notice-board LINE entities by SYM name
// so the catalog labels them "Notice Board" instead of a bare "Interaction N".
// Applies only to interaction lines the player reads as text (bgroom_1's
// 'BritinBoard' = the classroom bulletin board that cycles the Disciplinary /
// Garden Festival / cafeteria notices). The token set is deliberately TIGHT --
// distinctive substrings plus whole-name matches only -- so it never fires on
// unrelated words (bare "board"/"sign" would wrongly catch keyboard, design,
// signal, cardboard). Misses fall back to "Interaction N" (safe), never an
// over-label. Extend the lists as a field-SYM survey turns up more sign symbols;
// the robust long-term form keys on the script actually displaying a message.
static bool IsSignpostName(const char* sn)
{
    if (!sn || !sn[0]) return false;
    static const char* kParts[] = {
        "britin", "bulletin", "noticeboard", "notice_board",
        "signboard", "sign_board", "billboard", "signpost"
    };
    for (size_t i = 0; i < sizeof(kParts) / sizeof(kParts[0]); i++) {
        size_t nlen = strlen(kParts[i]);
        for (const char* h = sn; *h; ++h)
            if (_strnicmp(h, kParts[i], nlen) == 0) return true;
    }
    static const char* kExact[] = {
        "board", "sign", "notice", "signpost",
        "kanban", "keijiban", "keiji"
    };
    for (size_t i = 0; i < sizeof(kExact) / sizeof(kExact[0]); i++)
        if (_stricmp(sn, kExact[i]) == 0) return true;
    return false;
}

// v0.114.0 (#dsrc): defined below, beside the exit path that has used it since
// v0.62.2. Declared here because an interaction line now asks the same
// question an exit does, and it asks it earlier in the file.
static bool JsmGateOpen(const FieldArchive::JSMEntityInfo& je, int32_t* outLive);

static void InjectInteractionLines(EntityInfo* newCatalog, int& newCount)
{
        if (s_capturedLineCount > 0) {
            float intPlayerX = 0, intPlayerY = 0;
            const bool gotIntPlayer = (s_playerEntityIdx >= 0) &&
                                      GetEntityPos(s_playerEntityIdx, intPlayerX, intPlayerY);
            {
                int interactionNum = 0;
                // v0.18.3.268: solo-interaction naming. With exactly ONE active
                // interactive line, that line IS the field's puzzle object, so
                // name it rather than "Interaction 1". 'megami' outranks 'cup':
                // the statue screen holds both, but the interaction is the
                // statue. With 2+ lines the pairing is ambiguous -- keep generic.
                const char* soloName = nullptr;
                {
                    int nInter = 0;
                    for (int t2 = 0; t2 < s_capturedLineCount; t2++)
                        if (s_capturedLines[t2].active &&
                            s_capturedLines[t2].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE)
                            nInter++;
                    if (nInter == 1) {
                        bool hasStatue = false, hasGlass = false;
                        for (int j2 = 0; j2 < s_jsmEntityCount; j2++) {
                            if (s_jsmEntities[j2].type != FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) continue;
                            if (_stricmp(s_jsmEntities[j2].symName, "megami") == 0) hasStatue = true;
                            else if (_stricmp(s_jsmEntities[j2].symName, "cup") == 0) hasGlass = true;
                        }
                        if (hasStatue)      soloName = "Statue";
                        else if (hasGlass)  soloName = "Glass";
                    }
                }
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    // v0.20.30: camera-view transition lines are emitted as exits by
                    // InjectTriggerLineExits; never also list them here as interactions
                    // (that double-emission was the catalog instability Aaron saw).
                    if (s_capturedLines[t].isCameraTransition) continue;
                    // v0.116.0 (#centra): the ACTIVE test used to sit above this and
                    // took the line out before it could be named. It now sits just
                    // below the naming block, because a NAMED line the engine has
                    // switched off is worth listing as "not active" -- see
                    // line_camera_pan_surface_model.inl. Everything unnamed still
                    // leaves here exactly as it did.
                    // v0.20.31 (Aaron): the classroom desk trigger line ('Cliant') is a
                    // unique interaction -- label it "Desk" instead of a generic number.
                    const char* lineCurated = nullptr;
                    {
                        int wIdx2 = t;     // v0.62.2: lines are group 0..nLines-1
                        for (int j = 0; j < s_jsmEntityCount; j++) {
                            if (s_jsmEntities[j].jsmCategory == 1 && s_jsmEntities[j].jsmIndex == wIdx2) {
                                const char* csn = s_jsmEntities[j].symName;
                                if (_stricmp(csn, "Cliant") == 0) lineCurated = "Desk";      // v0.20.31: Squall's desk
                                else if (IsSignpostName(csn)) lineCurated = "Notice Board";  // v0.20.34: bulletin/notice signs
                                else {
                                    // v0.113.0 (#dsrc): ASK THE TABLES. Two hard-coded
                                    // specials were the only names a trigger line could
                                    // ever have, which is why v0.112.0's "Steam Room
                                    // Terminal" never appeared on Level 3 and Aaron
                                    // walked to "Interaction 1" instead. See
                                    // line_display_name_model.inl -- 18 lines disc-wide.
                                    const FieldScopedEntity* fsr =
                                        FieldScopedFor(s_currentFieldName, csn);
                                    const char* symName2 = nullptr;
                                    for (const EntityDisplayName* mL2 = ENTITY_DISPLAY_NAMES;
                                         mL2->sym != nullptr; mL2++) {
                                        if (_stricmp(csn, mL2->sym) == 0) { symName2 = mL2->display; break; }
                                    }
                                    lineCurated = LineDisplayName(
                                        (fsr && fsr->display) ? fsr->display : nullptr, symName2);
                                    if (lineCurated) {
                                        Log::Field("FieldNavigation: [refresh] line%d '%s' named "
                                                   "'%s' from the %s table [v0.113.0]",
                                                   t, csn, lineCurated,
                                                   (fsr && fsr->display) ? "field-scoped" : "sym");
                                    }
                                }
                                break;
                            }
                        }
                    }
                    const bool lineIsCamPan =
                        (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_CAMERA_PAN);
                    const bool lineEngineActive = s_capturedLines[t].active;
                    if (!lineEngineActive) {
                        if (!CameraPanLineSurfacesOffToo(lineIsCamPan, lineCurated)) continue;
                        Log::Field("FieldNavigation: [refresh] line%d '%s' is LINEOFF -- kept and "
                                   "labelled, the engine says crossing it does nothing [v0.116.0]",
                                   t, lineCurated);
                    }
                    // v0.17.7.1.2 / v0.17.7.5.4 / v0.17.7.5.5: Accept
                    // SCREEN_BOUND lines as Interactions in two cases:
                    //   1. hasDialogReqTarget=true (genuine dual-purpose,
                    //      e.g. dorm bed Line REQs dialog-bearing Background)
                    //   2. destFieldId == currentFieldId (self-loop sleep
                    //      transition, e.g. bgryo1_4 bed MAPJUMPs to field 240
                    //      which IS bgryo1_4) -- introduced v0.17.7.5.5 after
                    //      Aaron BAT'd the bed-as-exit mislabel.
                    //
                    // The SETLINE-Exit block above skips those same lines so
                    // they only appear here.
                    //
                    // Pure-exit SCREEN_BOUND lines (fepic1, bgroad_5 squalls)
                    // have hasDialogReqTarget=false AND destFieldId pointing
                    // to a different field -- they fall through this whole
                    // block and remain as Exits emitted by Block 1.
                    bool isInteractive =
                        (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE);
                    // v0.115.0 (#centra): A CAMERA_PAN line that the naming tables
                    // have named is not a camera pan -- it is a ladder, a statue or a
                    // switch whose only dispatch is PREQEW, which the classifier does
                    // not read, so it fell to the silent default. Surfacing is gated on
                    // the NAME, not on the opcode, so the blast radius is exactly the
                    // symbols a human has looked at; 95 camera-pan lines disc-wide use
                    // PREQEW and most of them are auto-firing story triggers a player
                    // must never be steered into. See line_camera_pan_surface_model.inl.
                    if (!isInteractive &&
                        CameraPanLineSurfaces(lineIsCamPan, lineCurated)) {
                        isInteractive = true;
                        Log::Field("FieldNavigation: [refresh] line%d camera-pan surfaced as "
                                   "'%s' (named in the tables) [v0.115.0]", t, lineCurated);
                    }
                    // v0.18.3.303 (#91 R1): the prison-shaft staircase exemption
                    // has to be applied to BOTH branches below, not just the
                    // self-loop one. .302 guarded only the second, so the down
                    // staircase -- which is dual-purpose AND self-destination --
                    // took the first branch and still surfaced as 'Interaction 1',
                    // the exact symptom the guard was meant to remove. Whenever a
                    // fix targets one of several passes writing the same field,
                    // check all of them (DEVNOTES rule); I missed that here once.
                    bool shaftStair = false;
                    if (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        uint16_t sfFid = FF8Addresses::pCurrentFieldId
                                         ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                        if (s_capturedLines[t].destFieldId == (int)sfFid &&
                            IsPrisonShaftFieldId(sfFid))
                            shaftStair = true;
                    }
                    if (!isInteractive && shaftStair) continue;   // named Exit only
                    if (!isInteractive &&
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        if (s_capturedLines[t].hasDialogReqTarget) {
                            isInteractive = true;
                        } else {
                            // v0.17.7.5.5: self-loop check.
                            uint16_t curFid = FF8Addresses::pCurrentFieldId
                                              ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                            if (s_capturedLines[t].destFieldId == (int)curFid) {
                                // v0.18.3.302 (#91 R1): in the D-District Prison
                                // shaft a self-destination line is a STAIRCASE,
                                // and the trigger-line block now emits it as a
                                // properly named Exit ("Stairs up to Floor 5").
                                // Promoting it here as well would list every
                                // staircase twice -- once with the real label and
                                // once as the meaningless "Interaction N" that
                                // sent Aaron looking for a staircase in the first
                                // place. Everywhere else (the dormitory bed and
                                // friends) is unchanged.
                                if (!IsPrisonShaftFieldId(curFid))
                                    isInteractive = true;
                            }
                        }
                    }
                    if (!isInteractive) continue;
                    // Don't check alreadyAdded -- Interactions use sentinel -600-t,
                    // distinct from exit sentinel -200-t, so both can coexist.
                    // Reachability: must be on same side of screen-boundary trigger lines.
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    // v0.20.33 (Aaron): a "door" interaction LINE co-located with a
                    // screen-bound exit is that exit's door-open animation trigger --
                    // crossing it plays the door opening automatically as the player
                    // heads for the exit, so listing it as a separate "Interaction" is
                    // redundant with the exit that's already catalogued. Drop it. BOTH
                    // conditions are required (a "door" name AND a nearby real exit line)
                    // so a genuine interaction that merely sits near a doorway is never
                    // suppressed. bgroom_1: 'door01' (1418,-3352) pairs with the
                    // 'to_corridor' exit (1418,-3444), 92 world units away.
                    bool doorTrigger = false;
                    {
                        const char* doorSym = nullptr;
                        // v0.62.2: captured line t is JSM GROUP t. field_scripts_init
                        // consumes the group array Lines, Doors, Backgrounds, Others --
                        // v0.58.0 established that and fixed the scanner, but these three
                        // consumers kept the old Doors-first base and so read the DOOR
                        // entity instead of the line on every field that has one. On
                        // ssmedi1 (1 door, 1 line) that made the save-line lookup ask the
                        // door whether it was a save line, the answer was no, and the save
                        // point's own activation zone was catalogued beside it as
                        // "Interaction 1". Aaron: "this build re-introduced a duplicate
                        // 'interaction' at the save point location. It should just have
                        // save point in the catalog."
                        int wIdxDoor = t;
                        for (int j = 0; j < s_jsmEntityCount; j++) {
                            if (s_jsmEntities[j].jsmCategory == 1 &&
                                s_jsmEntities[j].jsmIndex == wIdxDoor) {
                                doorSym = s_jsmEntities[j].symName; break;
                            }
                        }
                        bool nameHasDoor = false;
                        if (doorSym)
                            for (const char* p = doorSym; *p; ++p)
                                if (_strnicmp(p, "door", 4) == 0) { nameHasDoor = true; break; }
                        if (nameHasDoor) {
                            for (int dt = 0; dt < s_capturedLineCount && !doorTrigger; dt++) {
                                if (dt == t || !s_capturedLines[dt].active) continue;
                                if (s_capturedLines[dt].lineType !=
                                        FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                                if (s_capturedLines[dt].isCameraTransition) continue;
                                float ecx = (float)(s_capturedLines[dt].x1 + s_capturedLines[dt].x2) / 2.0f;
                                float ecy = (float)(s_capturedLines[dt].y1 + s_capturedLines[dt].y2) / 2.0f;
                                float ddx = tcx - ecx, ddy = tcy - ecy;
                                if (ddx*ddx + ddy*ddy <= 400.0f * 400.0f) {
                                    doorTrigger = true;
                                    Log::Field("FieldNavigation: [refresh] interaction line%d '%s' "
                                               "center=(%.0f,%.0f) suppressed: door-open trigger "
                                               "co-located with screen-bound exit line%d [v0.20.33]",
                                               t, doorSym, tcx, tcy, dt);
                                }
                            }
                        }
                    }
                    if (doorTrigger) continue;
                    // v0.18.3.268 BUG B: BOUNDED segment-crossing test, not the
                    // infinite-line side test. IsSeparatedByTriggerLine extends
                    // every screen-bound line to infinity, so a short doorway
                    // line elsewhere can falsely "separate" an interaction on the
                    // far side -- the same over-reach fixed for INF gateways in
                    // v0.17.8.10 via SegmentsCross, which this block never got.
                    // glfurin1: line0's infinite extension filtered the glass
                    // shelf's line1 while line2 (same side as player) survived.
                    if (gotIntPlayer) {
                        bool intCrossed = false;
                        for (int dt = 0; dt < s_capturedLineCount && !intCrossed; dt++) {
                            if (!s_capturedLines[dt].active) continue;
                            if (dt == t) continue;   // never test a line against itself
                            if (s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                                s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_UNKNOWN)
                                continue;
                            if (SegmentsCross(intPlayerX, intPlayerY, tcx, tcy,
                                              (float)s_capturedLines[dt].x1, (float)s_capturedLines[dt].y1,
                                              (float)s_capturedLines[dt].x2, (float)s_capturedLines[dt].y2)) {
                                intCrossed = true;
                                Log::Field("FieldNavigation: [refresh] interaction line%d center=(%.0f,%.0f) "
                                           "filtered: path crosses screen-bound line%d", t, tcx, tcy, dt);
                            }
                        }
                        if (intCrossed) continue;
                    }
                    // v0.17.8.8: If the scanner flagged this line's owning
                    // entity as a save line (own MENUSAVE, or REQ to a save
                    // point), surface it as "Save Point" not "Interaction N".
                    // This restores the bghall_1 Hall 1 save-point label: its
                    // 'savePoint' has PSHM-only X/Y, never injects standalone,
                    // and reaches the catalog only via this trigger line.
                    // Map: captured line t -> JSM line entity jsmIndex (doors+t).
                    bool lineIsSave = false;
                    {
                        int wantIdx = t;   // v0.62.2: lines are group 0..nLines-1
                        for (int j = 0; j < s_jsmEntityCount; j++) {
                            if (s_jsmEntities[j].jsmCategory == 1 &&
                                s_jsmEntities[j].jsmIndex == wantIdx &&
                                s_jsmEntities[j].isSaveLine) {
                                lineIsSave = true; break;
                            }
                        }
                    }
                    // v0.18.3.272: never add a SECOND Save Point. The v0.17.8.8
                    // trigger-line fallback was added because bghall_1's
                    // 'savePoint' has PSHM-only X/Y and "never injects
                    // standalone". It does now (it appears as runtime entity
                    // ent6), so both paths fired and Hall 1 announced
                    // "Save Point 1 of 2" / "2 of 2" for one physical save point.
                    // Keep the fallback for fields where the entity genuinely
                    // doesn't inject; skip it once one is already catalogued.
                    if (lineIsSave) {
                        bool haveSave = false;
                        for (int c = 0; c < newCount; c++)
                            if (newCatalog[c].type == ENT_SAVE_POINT) { haveSave = true; break; }
                        if (haveSave) {
                            Log::Field("FieldNavigation: [refresh] line%d save-line skipped: "
                                       "Save Point already in catalog", t);
                            continue;
                        }
                    }
                    // v0.114.0 (#dsrc): is this line's script shut right now?
                    // The gate is decoded at scan time (field_archive_jsm_linepass.inl)
                    // and evaluated live here, the same way v0.62.2 evaluates an
                    // exit's. An exit that is shut is dropped; a NAMED interaction
                    // that is shut is kept and says so -- see line_gate_name_model.inl.
                    bool lineGateOpen = true;
                    bool lineHasGate  = false;
                    {
                        for (int jg = 0; jg < s_jsmEntityCount; jg++) {
                            if (s_jsmEntities[jg].jsmCategory != 1) continue;
                            if (s_jsmEntities[jg].jsmIndex != t) continue;
                            if (s_jsmEntities[jg].hasGate) {
                                int32_t liveG = 0;
                                lineHasGate  = true;
                                lineGateOpen = JsmGateOpen(s_jsmEntities[jg], &liveG);
                                Log::Field("FieldNavigation: [LINE-GATE] line%d '%s' gated on "
                                           "var[%d]=%d op%d %d -> %s [v0.114.0]",
                                           t, s_jsmEntities[jg].symName,
                                           s_jsmEntities[jg].gateAddr, (int)liveG,
                                           (int)s_jsmEntities[jg].gateOp,
                                           s_jsmEntities[jg].gateValue,
                                           lineGateOpen ? "open" : "SHUT");
                            }
                            break;
                        }
                    }
                    EntityInfo intEntry = {};
                    intEntry.entityIdx  = -200 - t;  // same sentinel as exits -- position lookup works identically
                    intEntry.modelId    = -1;
                    intEntry.triangleId = 0;
                    intEntry.gatewayIdx = -1;
                    if (lineIsSave) {
                        intEntry.type = ENT_SAVE_POINT;
                        strncpy(intEntry.name, "Save Point", sizeof(intEntry.name) - 1);
                        intEntry.name[sizeof(intEntry.name) - 1] = '\0';
                        Log::Field("FieldNavigation: [refresh] line%d surfaced as "
                                   "Save Point (isSaveLine) [v0.17.8.8]", t);
                    } else {
                        intEntry.type = ENT_INTERACTION;
                        if (lineCurated) {
                            // v0.114.0 (#dsrc): the name does NOT yet carry the
                            // gate's state, and the reason is written down in
                            // line_gate_name_model.inl: on ddtower3's `Tanme2`
                            // the decoded guard is true on the branch that does
                            // NOTHING. Saying "not ready" from it would be
                            // exactly backwards. [LINE-GATE] logs the live value
                            // so one run settles the polarity; until then the
                            // name is the name.
                            // v0.116.0 (#centra): ", not active" when the engine has
                            // the line switched off. Unlike v0.114.0's decoded script
                            // guard, LINEOFF has no polarity to get wrong -- it is the
                            // engine saying this line does nothing right now.
                            LineOffDisplayName(intEntry.name, sizeof(intEntry.name),
                                               lineCurated, lineEngineActive);
                        } else if (soloName) {
                            snprintf(intEntry.name, sizeof(intEntry.name), "%s", soloName);
                        } else {
                            // v0.20.22: provisional label; the contiguous number is assigned below,
                            // AFTER filtering, so a dropped interaction never burns a number
                            // (fixes "Interaction 6 of 4").
                            strncpy(intEntry.name, "Interaction", sizeof(intEntry.name) - 1);
                            intEntry.name[sizeof(intEntry.name) - 1] = '\0';
                        }
                    }
                    // v0.20.15: the Caraway's Mansion glass-shelf interaction line (the
                    // 'Glass' trigger, line 1665's "glass shelf") is only interactive once
                    // the escape puzzle activates -- gate it on the same sealed flag as the
                    // puzzle objects. Save Points are never gated. glfurin* only; fires only
                    // on the not-sealed side, so it never hides the glass during the puzzle.
                    if (intEntry.type == ENT_INTERACTION &&
                        strncmp(s_currentFieldName, "glfurin", 7) == 0 && !CarawayMansionSealed()) {
                        Log::Field("FieldNavigation: [refresh] interaction line%d '%s' suppressed: "
                                   "Caraway's Mansion escape puzzle not yet active (progress<=376) [v0.20.15]",
                                   t, intEntry.name);
                        continue;
                    }
                    { CatAudit("interaction", intEntry, (int)tcx, (int)tcy, "-",
                        (intEntry.type == ENT_SAVE_POINT) ? "save point" : "interaction line"); LiveGateLine("interaction", intEntry, t); }
                    if (s_zoneValid && intEntry.type != ENT_SAVE_POINT && !ZoneReachableLine(t)) { Log::Field("FieldNavigation: [refresh] '%s' filtered: another camera zone (unreachable from player) [v0.20.22]", intEntry.name); continue; }
                    // v0.20.32: don't renumber a curated line name (e.g. "Desk") back
                    // into "Interaction N" -- exempt lineCurated the same way soloName is.
                    if (intEntry.type == ENT_INTERACTION && !soloName && !lineCurated) {
                        interactionNum++;
                        snprintf(intEntry.name, sizeof(intEntry.name), "Interaction %d", interactionNum);
                    }
                    newCatalog[newCount++] = intEntry;
                }
            }
        }
}

// ============================================================================
// v0.20.48 (#117): draw-point presence + TRUE position. v0.20.46/47 RE'd the visibility gate correctly
// (renderer 0x00475170 draws when state==2 || (state==1 && cfg==1); createDrawPoint 0x00474750 sets
// state=1 and cfg = word[0x01CDBFEA] = SETDRAWPOINT_param | Move-Find drawFlag, recomputed live -- so
// cfg==1 is exactly "a sighted player sees this sparkle, Move-Find included"), and the v0.20.47 BAT
// confirmed cfg (both tested points logged cfg=1 while the sparkle was on screen). What was WRONG was
// matching the sparkle against the drpoint entity's CATALOG position: that position is unreliable
// (otokun01 resolved 435/2422 world-units off the sparkle; zells resolved to (0,0)), so a VISIBLE draw
// point was dropped. FIX: the sparkle world position (0x01CDC620/622 = the drpoint entity's own pos >>12,
// written by createDrawPoint) IS the ground truth for where the draw point sits -- adopt it as the
// catalog position, and reject a STALE sparkle (left by a previous field whose SETDRAWPOINT did not run)
// by requiring it to lie on THIS field's walkmesh, not by matching a fragile entity position. state==2 is
// a transient draw-burst mode (separate particle routine 0x00474872), not the steady sparkle, so it is
// logged only. On success the sparkle position + its walkmesh triangle are returned for placement.
// SEH-guarded, fail-OPEN.
static bool IsDrawPointLivePresent(const char* sym, int16_t* outX, int16_t* outY, uint16_t* outTri)
{
    __try {
        int16_t sx  = *(const volatile int16_t*)(uintptr_t)0x01CDC620u;   // sparkle world X (entityX>>12)
        int16_t sy  = *(const volatile int16_t*)(uintptr_t)0x01CDC622u;   // sparkle world Y
        int16_t cfg = *(const volatile int16_t*)(uintptr_t)0x01CDBFEAu;   // renderer visibility gate (param|MoveFind)
        uint8_t st  = *(const volatile uint8_t*)(uintptr_t)0x01CE0750u;   // transient particle state (diagnostic)
        bool visible = (cfg == 1);
        bool onField = true;                 // fail-open: no walkmesh -> trust cfg alone
        const char* why = "keep(no-walkmesh)";
        float ndist = -1.0f;
        uint16_t tri = 0xFFFF;
        if (visible && s_walkmesh.valid && s_walkmesh.numTriangles > 0) {
            uint16_t nt = NearestWalkTriangle((float)sx, (float)sy);
            if (nt != 0xFFFF && nt < (uint16_t)s_walkmesh.numTriangles) {
                tri = nt;
                float ddx = s_walkmesh.triangles[nt].centerX - (float)sx;
                float ddy = s_walkmesh.triangles[nt].centerY - (float)sy;
                ndist = sqrtf(ddx*ddx + ddy*ddy);
            }
            if (IsInsideWalkmesh((float)sx, (float)sy))               { onField = true;  why = "on-mesh"; }
            else if (tri != 0xFFFF && ndist >= 0.0f && ndist < 600.0f) { onField = true;  why = "near-mesh"; }
            else                                                      { onField = false; why = "off-mesh(stale)"; }
        }
        bool present = visible && onField;
        if (present && outX && outY) { *outX = sx; *outY = sy; if (outTri) *outTri = tri; }
        Log::Field("FieldNavigation: [drawpt] '%s' live: state=%d cfg=%d sparkle=(%d,%d) nearTri=%d dist=%.0f mesh=%s -> %s [v0.20.48 #117]",
                   sym ? sym : "?", (int)st, (int)cfg, (int)sx, (int)sy, (int)(int16_t)tri, ndist, why,
                   present ? "PRESENT" : "absent");
        return present;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }  // fail-OPEN
}

// 3d. JSM special-entity injection  (was inline block, field_nav_catalog.inl :738-991)
// ============================================================================
static void InjectJsmSpecials(EntityInfo* newCatalog, int& newCount, const EntityInfo* fresh, uint8_t lim)
{
        for (int j = 0; j < s_jsmEntityCount && newCount < MAX_CATALOG; j++) {
            const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
            if (je.type != FieldArchive::JSM_ENT_SAVE_POINT &&
                je.type != FieldArchive::JSM_ENT_DRAW_POINT &&
                je.type != FieldArchive::JSM_ENT_SHOP &&
                je.type != FieldArchive::JSM_ENT_CARD_GAME &&
                je.type != FieldArchive::JSM_ENT_LADDER &&        // ITEM-1: surface ladders as navigable targets
                je.type != FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) continue;
                // v0.12.17: JSM_ENT_INTERACTIVE_OBJECT RE-ENABLED.
                // v0.12.12 removed it ("typically background visual effects"),
                // but that also blocked the Directory (dic) on bghall_1.
                // The paired-entity detection + shift-pattern position provides
                // adequate positions for genuine interactive objects.
            // v0.07.80: Check if this type exists as a runtime entity ANYWHERE on
            // the field, even if screen-filtered. JSM SET3 positions are unreliable
            // (bghall_1 saves at 135,588 instead of -700,-8593). Runtime entities
            // always have correct positions. If a runtime entity of matching type
            // exists, prefer it — don't inject JSM with wrong coordinates.
            EntityType jt = JSMTypeToCatalogType(je.type);
            // v0.20.48 (#117): draw-point gate. Presence = live sparkle (cfg==1) sitting on THIS field's
            // walkmesh; on success adopt the sparkle's own world position + triangle as the catalog
            // position (the drpoint entity's resolved position proved unreliable -- see the v0.20.47 BAT).
            if (jt == ENT_DRAW_POINT) {
                int16_t spx = je.posX, spy = je.posY; uint16_t sptri = je.posTriangle;
                if (!IsDrawPointLivePresent(je.symName, &spx, &spy, &sptri)) { continue; }
                s_jsmEntities[j].posX = spx;
                s_jsmEntities[j].posY = spy;
                s_jsmEntities[j].hasPosition = true;
                if (sptri != 0xFFFF) s_jsmEntities[j].posTriangle = sptri;
            }
            // v0.18.3.292 (#85): honour ENTITY_SKIP_NAMES for JSM-injected
            // objects too -- it was only consulted by IsBgControllerName() for
            // Background entities, so an Others entity with a controller name
            // walked straight in. glwater3's 'water' is in that list yet showed
            // as an "Object" ~100u from Gate 1 (Aaron: "another object was at
            // the same location as a gate"). Effect/lighting controllers are
            // never navigation targets.
            if (jt == ENT_OBJECT && IsBgControllerName(je.symName)) {
                Log::Field("FieldNavigation: [refresh] JSM object '%s' skipped: "
                           "controller/effect name (ENTITY_SKIP_NAMES) [v0.18.3.292]",
                           je.symName);
                continue;
            }
            // v0.19.1 (#97/#98): OBSERVE-ONLY -- drops NOTHING. The v0.19.0 drop was the
            // wrong layer: the DIRECTOR pattern feeds interaction from an INVISIBLE director
            // (bghall_1 'elelight' has 17 dialog targets) to promoted entities, so a REAL
            // object (a student, the Garden directory) has talk=setline=dialog=0 on ITSELF
            // and the v0.19.0 gate wrongly dropped it. The correct fix belongs in the director
            // promotion (only promote entities the director gives a real DIALOG to), plus
            // separate recognition for the real directory. This line just records the per-object
            // interaction flags across fields to design that fix; nothing is filtered.
            if (jt == ENT_OBJECT) {
                Log::Field("FieldNavigation: [refresh] JSM object '%s' interact-obs: "
                           "talk=%d setline=%d saveLine=%d extDisp=%d dlgReq=%d setmodel=%d [v0.19.1 #97/#98]",
                           je.symName, je.hasTalkSetup, je.hasSetline, je.isSaveLine,
                           je.hasExtDispatch, je.hasDialogReqTarget, je.hasSetmodelInit);
            }
            // v0.19.9 RE (#5): the REAL interactability signal lives on the LIVE entity, not
            // in the static script. The engine's interaction check needs a nonzero talk
            // radius (0x1F8) or push radius (0x1F6); those are gated by the runtime enable
            // flags talkonoff (0x24B) / pushonoff (0x249) that TALKON/PUSHON set. The mod's
            // static talk scan reads the wrong opcode (0x056) so it is 0 for every entity,
            // real ones included -- so read the flags straight from pFieldStateOthers. Log
            // BOTH index conventions (Other-compact = jsmIndex-othStart, per
            // ResolveLatePositions; and flat = jsmIndex) so one BAT settles which aligns.
            // LOG-ONLY.
            if (jt == ENT_OBJECT && je.jsmCategory == 3 && FF8Addresses::pFieldStateOthers) {
                int othStart = s_jsmDoors + s_jsmLines + s_jsmBackgrounds;
                int oirC = je.jsmIndex - othStart;   // Other-compact convention
                int oirF = je.jsmIndex;              // flat convention
                int tkC=-1,puC=-1,mdC=-999, tkF=-1,puF=-1,mdF=-999;
                __try {
                    uint8_t* ob = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    if (ob) {
                        if (oirC >= 0 && oirC < MAX_ENTITIES) {
                            uint8_t* b = ob + ENTITY_STRIDE * oirC;
                            tkC = *(b + 0x24B); puC = *(b + 0x249); mdC = *(int16_t*)(b + 0x218);
                        }
                        if (oirF >= 0 && oirF < MAX_ENTITIES) {
                            uint8_t* b = ob + ENTITY_STRIDE * oirF;
                            tkF = *(b + 0x24B); puF = *(b + 0x249); mdF = *(int16_t*)(b + 0x218);
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                Log::Field("FieldNavigation: [refresh] JSM object '%s' RT-INTERACT: "
                           "compact(oir=%d talkon=%d pushon=%d model=%d) flat(oir=%d talkon=%d pushon=%d model=%d) [v0.19.9]",
                           je.symName, oirC, tkC, puC, mdC, oirF, tkF, puF, mdF);
            }
            // v0.19.7 (#5): director "Object" junk gate. RunDirectorDetection promotes a
            // director's REQ targets to INTERACTIVE_OBJECT, but that promotion is
            // field-wide (dialog OR extDispatch OR model+req) and also sweeps in inert
            // effect entities the director never actually REQs (bghall_1 water/l5/
            // seito15). Such an entity has no interaction path of its own AND is not a
            // REQ target, so it is not a navigable object -- drop it. NARROWER than the
            // reverted v0.19.0 gate: requires wasDirectorPromoted (classify-promoted
            // directories/desks carry =false and are untouched), !isReqTarget (genuine
            // director targets survive; REQ target resolved statically from the opcode
            // inline param -- see field_archive_jsm_scan.inl), AND no own interaction
            // signal (talk/setline/saveline/dlgReq) so a player-interactable entity is
            // never dropped even if the director over-reached to promote it.
            // v0.20.0 (#5): SUPERSEDES the v0.19.7 gate comment above. RE'd from the
            // field engine -- the player can only interact with an entity that has a
            // talk/push radius (proximity) OR a walk-into trigger zone (SETLINE / INF
            // trigger, captured as hasNearbyInteractionZone). Drop a marker-positioned
            // Object (hasPshmCoords -- not placed at literal coords by the designer)
            // only when it has NO interaction zone AND no own interaction AND no curated
            // name. The bghall_5 lights (lr1/lr2/l4/s2) have none of these; the Balamb
            // directory carries a co-located SETLINE trigger AND a curated name, and
            // literal-placed objects (hasPshmCoords=false) are never in scope.
            bool jsmOwnInteraction = je.hasTalkSetup || je.hasSetline ||
                                     je.isSaveLine || je.hasDialogReqTarget;
            bool jsmNamedObject = false;
            bool jsmIsGate = false;  // v0.20.43: curated AND its display name is a "Gate" -- the
                                     // ALWAYS-PRESENT control mechanisms. STATE-DEPENDENT curated
                                     // objects (hasigomodel="Ladder", which appears only when the
                                     // shortcut ladder is down) must NOT get the out-of-window
                                     // exemption, or they phantom when absent (Aaron's phantom ladders).
            if (je.symName[0] != '\0') {
                for (const EntityDisplayName* mN = ENTITY_DISPLAY_NAMES; mN->sym != nullptr; mN++) {
                    if (_stricmp(je.symName, mN->sym) == 0) {
                        jsmNamedObject = true;
                        jsmIsGate = (mN->display && strncmp(mN->display, "Gate", 4) == 0);
                        break;
                    }
                }
            }
            if (jt == ENT_OBJECT && je.hasPshmCoords &&
                !je.hasNearbyInteractionZone && !jsmOwnInteraction && !jsmNamedObject) {
                Log::Field("FieldNavigation: [refresh] JSM object '%s' dropped: marker-positioned "
                           "phantom -- no interaction zone (SETLINE/trigger), no own interaction, "
                           "no curated name -- junk-gate [v0.20.0 #5]",
                           je.symName);
                continue;
            }

            // v0.20.29 (Aaron): drop opening-cutscene scene-actor phantoms. The
            // junk-gate above keeps an Object that has an interaction or curated
            // name -- but characters like Selphie carry a talk method yet are NOT
            // present during free-roam (their state-gated SET3 never ran). The live
            // "others" entity struct is the ground truth: a placed entity has a real
            // triangle/position; an unplaced one reads tri=0 pos=(0,0). Only applied
            // to marker-positioned (hasPshmCoords) Others so literally-placed and
            // live-present entities (Squall at his desk) are never touched.
            // v0.20.30: check ALL Others' live entity struct (v0.20.29 only checked
            // hasPshmCoords ones, so literally-positioned scene actors like Selphie
            // slipped through). No SEH (matches the sceneAssembly probe): base valid
            // on-field, index bounded by the engine's live other-count. Also logs the
            // flag word (+0x160) and model id (+0x218) so a visibility signal can be
            // found for any placed-but-hidden phantom the live-position test misses.
            if (jt == ENT_OBJECT && je.jsmCategory == 3 &&
                FF8Addresses::pFieldStateOthers && FF8Addresses::pFieldStateOtherCount) {
                uint8_t* obP  = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                int      oirP = je.jsmIndex - (s_jsmDoors + s_jsmLines + s_jsmBackgrounds);
                int      ocnt = (int)(*FF8Addresses::pFieldStateOtherCount);
                if (obP && oirP >= 0) {
                    // v0.20.31: the engine's live "others" array holds only the entities
                    // actually loaded in the current scene (ocnt of them). A JSM other
                    // whose slot index is >= ocnt has NO live entity -- it is a cutscene
                    // actor from a different scene (Selphie/Quistis/Irvine in the opening
                    // classroom) and is not present. Drop it.
                    // v0.20.42 (#85 gate maze): oirP>=ocnt means this entity sits BEYOND the engine
                    // active-tracking window (ocnt = only the nearest ~8-9 live others). For a sewer
                    // gate/mechanism that is the NORMAL state, not an absent cutscene actor -- and it is
                    // the drop that defeated v0.20.41: ocnt fluctuates 8<->9 across scenes, so a controller
                    // hits THIS branch first (Aaron's "hit or miss"). A curated object with a real static
                    // position is kept on it; there is NO live slot here, so blkP must NOT be dereferenced.
                    bool keptOOW = false;
                    if (oirP >= ocnt) {
                        if (jsmIsGate && je.hasPosition && je.posTriangle != 0) {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' KEPT despite oirP>=ocnt "
                                       "(%d>=%d): curated gate beyond active window, static tri=%u pos=(%d,%d) "
                                       "[v0.20.42]", je.symName, oirP, ocnt, (unsigned)je.posTriangle,
                                       (int)je.posX, (int)je.posY);
                            keptOOW = true;
                        } else {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' dropped: no live entity "
                                       "slot (index %d >= liveOthers %d) -- scene actor not present [v0.20.31]",
                                       je.symName, oirP, ocnt);
                            continue;
                        }
                    }
                    if (!keptOOW) {
                    uint8_t*  blkP  = obP + ENTITY_STRIDE * oirP;
                    uint16_t  ltri  = *(uint16_t*)(blkP + 0x1FA);
                    int32_t   lfx   = *(int32_t*)(blkP + 0x190);
                    int32_t   lfy   = *(int32_t*)(blkP + 0x194);
                    if (ltri == 0 && lfx == 0 && lfy == 0) {
                        // v0.20.41 (#85 gate maze): EXEMPT a curated named object that carries a real
                        // STATIC position (its own SET3 triangle / walkmesh centroid). The sewer gate
                        // controllers (ct_lf etc.) sit BEYOND the engine active-tracking window, so their
                        // live slot legitimately reads tri=0/pos=0 -- but the static SET3 triangle is
                        // authoritative (the #85 centroid-fallback case), and this live-0 drop is the
                        // regression that hid the openable gate. Scene actors this filter targets
                        // (Selphie/Quistis) are NOT curated gate names and have no static SET3, so they
                        // are still dropped; reachability (below) then keeps only the reachable gate.
                        if (jsmIsGate && je.hasPosition && je.posTriangle != 0) {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' KEPT despite live "
                                       "tri=0: curated object with static tri=%u pos=(%d,%d) -- "
                                       "out-of-window, centroid-positioned [v0.20.41]",
                                       je.symName, (unsigned)je.posTriangle,
                                       (int)je.posX, (int)je.posY);
                        } else {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' dropped: scene-actor "
                                       "phantom -- not placed in live state (tri=0 pos=0) [v0.20.31]", je.symName);
                            continue;
                        }
                    }
                    // v0.20.36 (glwater3 BAT): drop a JSM object whose live entity is HIDE-flagged
                    // (+0x160 bit3, set by opcode 0x61/HIDE, cleared by 0x60/SHOW). This is the SAME
                    // signal the entity-scan path's HIDDEN-ENTITY filter (v0.18.3.269) already trusts,
                    // but that filter never ran on this JSM-injection path -- so the sewer's
                    // not-yet-knocked-down ladder (hasigomodel, live hide=1) leaked into the catalog.
                    // Global: a hidden JSM object is not drawn / not present, never a real target. It
                    // reappears the moment the script SHOWs it (the ladder becomes a usable crossing).
                    {
                        // v0.20.43: duplicate-slot guard on the HIDE read -- the SAME guard LATE-RESOLVE
                        // and STRUCT-POS already apply to POSITION. The engine's live "others" array only
                        // tracks a window of entities; a slot whose live triangle disagrees with THIS
                        // entity's own static SET3 triangle is ALIASED to a different entity, so its
                        // +0x160 flags are not ours. glwater4: ct_lf_dw (own tri 59) and lf_up (own tri 73)
                        // read slots that alias tri 199 -- a HIDDEN party member -- so their HIDE bit read
                        // as set and BOTH gates the player needed were wrongly dropped. Trust the HIDE flag
                        // only when the slot's tri matches our own (the slot really belongs to us).
                        bool slotIsOurs = (je.posTriangle != 0) && (ltri == (uint16_t)je.posTriangle);
                        uint32_t lflags = *(uint32_t*)(blkP + 0x160);
                        // v0.75.0 (#112): AN INVISIBLE INTERACTION POINT IS NOT AN ABSENT
                        // OBJECT. This filter was written for a prop the script has not
                        // revealed yet -- the sewer's un-knocked ladder, a scene actor parked
                        // off-stage -- and those hide themselves and say nothing. An entity
                        // whose INIT calls HIDE and TALKON in the same breath is the opposite:
                        // its picture is in the background art and the hidden model exists only
                        // to carry the talk target. FF8 builds terminals, panels and signs this
                        // way. rgguest2's `comp` is one, and it is the terminal that teaches
                        // the Propagator pairing rule -- the single thing in that room worth
                        // finding. Aaron: "the terminal is not appearing in the catalog."
                        if (slotIsOurs && (lflags & 0x08) != 0 && je.invisibleTalkTarget) {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' KEPT despite the "
                                       "HIDE flag: its own init calls HIDE and TALKON together, so the "
                                       "model is invisible on purpose and the thing you talk to is in "
                                       "the background art [v0.75.0]", je.symName);
                        } else if (slotIsOurs && (lflags & 0x08) != 0) {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' dropped: HIDE flag set "
                                       "(flags@0x160=0x%08X bit3) -- hidden/not shown [v0.20.36]",
                                       je.symName, (unsigned)lflags);
                            continue;
                        }
                        if (!slotIsOurs && (lflags & 0x08) != 0) {
                            Log::Field("FieldNavigation: [refresh] JSM object '%s' HIDE bit IGNORED: live "
                                       "slot tri=%u != own SET3 tri=%u -- aliased slot, flag not ours [v0.20.43]",
                                       je.symName, (unsigned)ltri, (unsigned)je.posTriangle);
                        }
                    }
                    Log::Field("FieldNavigation: [refresh] JSM object '%s' present: tri=%d pos=(%d,%d) [v0.20.31 diag]",
                               je.symName, (int)ltri, (int)lfx, (int)lfy);
                    }  // end if(!keptOOW) -- v0.20.42
                }
            }
            // v0.18.3.286 (#85): ENT_OBJECT is not a singleton category -- a
            // field can hold several distinct Interactive Objects (the sewer
            // gate maze has up to 8). The two bare-type checks below were
            // written for genuinely singleton types (Save/Draw/Shop/Card), so
            // applying them to ENT_OBJECT meant only the FIRST one found per
            // refresh entered the catalog; the rest were dropped before the
            // proximity dedupe pass (field_nav_catalog_dedupe.inl) saw them.
            // Skip both guards for ENT_OBJECT; real duplicate removal still
            // happens downstream in that proximity pass.
            bool isSingletonType = (jt != ENT_OBJECT);
            bool runtimeEntityExists = false;
            if (isSingletonType) {
                for (int i2 = 0; i2 < (int)lim; i2++) {
                    if (fresh[i2].entityIdx >= 0 && fresh[i2].type == jt) {
                        runtimeEntityExists = true; break;
                    }
                }
            }
            if (runtimeEntityExists) continue;
            // Also check what's already in the catalog (from other sources).
            bool alreadyInCatalog = false;
            if (isSingletonType) {
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].type == jt) { alreadyInCatalog = true; break; }
                }
            }
            if (alreadyInCatalog) continue;

            // v0.12.09: Draw point consolidation.
            // The JSM draw point position often points to an invisible script entity,
            // not the actual interaction trigger. Check if any interactive catalog
            // entity (NPC/draw point/save point) exists near the JSM position.
            // If not, the real interaction entity is elsewhere — find the closest
            // non-party NPC in the catalog and reclassify it as Draw Point.
            if (jt == ENT_DRAW_POINT && je.hasPosition && s_walkmesh.valid && s_playerEntityIdx >= 0) {
                // Check if any catalog entity with interaction is near the JSM draw point.
                bool interactiveNearDP = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].entityIdx < 0) continue;
                    if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                    float nx = 0, ny = 0;
                    if (GetEntityPos(newCatalog[c].entityIdx, nx, ny)) {
                        float ddx = (float)je.posX - nx;
                        float ddy = (float)je.posY - ny;
                        float dd = sqrtf(ddx*ddx + ddy*ddy);
                        if (dd < 300.0f) { interactiveNearDP = true; break; }
                    }
                }
                if (!interactiveNearDP) {
                    // No interactive entity near the JSM draw point position.
                    // Find closest non-party NPC on the player's walkmesh and reclassify.
                    uint16_t pTriDP = 0xFFFF;
                    __try {
                        uint8_t* baseDP = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                        if (baseDP) pTriDP = *(uint16_t*)(baseDP + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    int bestCatIdx = -1;
                    float bestDist = 1e30f;
                    for (int c = 0; c < newCount; c++) {
                        if (newCatalog[c].entityIdx < 0) continue;
                        if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                        if (newCatalog[c].type != ENT_NPC) continue;
                        // Skip party character models (0-9)
                        if (newCatalog[c].modelId >= 0 && newCatalog[c].modelId < 10) continue;
                        float nx = 0, ny = 0;
                        if (GetEntityPos(newCatalog[c].entityIdx, nx, ny)) {
                            float ddx = (float)je.posX - nx;
                            float ddy = (float)je.posY - ny;
                            float dd = sqrtf(ddx*ddx + ddy*ddy);
                            if (dd < bestDist) { bestDist = dd; bestCatIdx = c; }
                        }
                    }
                    if (bestCatIdx >= 0 && bestDist < 300.0f) {   // v0.20.48: only adopt an NPC AT the sparkle; a standalone sparkle injects on its own below
                        newCatalog[bestCatIdx].type = ENT_DRAW_POINT;
                        strncpy(newCatalog[bestCatIdx].name, "Draw Point",
                                sizeof(newCatalog[bestCatIdx].name) - 1);
                        Log::Field("FieldNavigation: [catalog] Draw point consolidation: "
                                   "reclassified ent%d as Draw Point (JSM dp '%s' at %d,%d "
                                   "has no nearby interactive entity, NPC dist=%.0f)",
                                   newCatalog[bestCatIdx].entityIdx, je.symName,
                                   je.posX, je.posY, bestDist);
                        continue; // don't inject the JSM draw point
                    }
                }
            }

            // Not in catalog yet.
            // v0.12.12: No-position draw point fallback.
            // When a JSM draw point exists on the field but has no position
            // (entity beyond runtime window, e.g. Fire Cavern 'drpoint'),
            // reclassify the nearest non-player entity as Draw Point.
            // This is less precise than position-based consolidation but
            // ensures draw points get correct labels in the catalog.
            // v0.20.37 (glwater3): a cat=2 BACKGROUND false-typed as a no-position Draw Point is NOT a
            // real draw point. glwater3's gate 'saku4' gets DRAWPOINT-typed from a DRAWPOINT opcode in its
            // gate-controller script, has no position, and the fallback below then mislabeled a scene
            // character (ent3) as the Draw Point at a wrong, reachable-looking spot. Real no-position draw
            // points (Fire Cavern 'drpoint') are cat=3 "others". Drop the background outright; a genuinely
            // present draw point still comes through the position-based path above.
            if (jt == ENT_DRAW_POINT && !je.hasPosition && je.jsmCategory == 2) {
                Log::Field("FieldNavigation: [catalog] no-position draw-point '%s' dropped: cat=2 "
                           "background, not a real draw point (no fabricated fallback) [v0.20.37]", je.symName);
                continue;
            }
            if (jt == ENT_DRAW_POINT && !je.hasPosition) {
                int bestCatIdx2 = -1;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].entityIdx < 0) continue;
                    if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                    if (newCatalog[c].type != ENT_NPC) continue;
                    // v0.12.12: Don't filter by model<10 here.
                    // On Fire Cavern, the draw point entity uses model 9
                    // (party character range). Without position data,
                    // we accept any NPC as a candidate.
                    float nx2 = 0, ny2 = 0;
                    if (GetEntityPos(newCatalog[c].entityIdx, nx2, ny2)) {
                        // Without JSM position, we can't measure proximity.
                        // Pick the first (and likely only) non-player NPC.
                        bestCatIdx2 = c;
                        break;
                    }
                }
                if (bestCatIdx2 >= 0) {
                    newCatalog[bestCatIdx2].type = ENT_DRAW_POINT;
                    strncpy(newCatalog[bestCatIdx2].name, "Draw Point",
                            sizeof(newCatalog[bestCatIdx2].name) - 1);
                    Log::Field("FieldNavigation: [catalog] Draw point no-position fallback: "
                               "reclassified ent%d as Draw Point (JSM dp '%s' has no position)",
                               newCatalog[bestCatIdx2].entityIdx, je.symName);
                    continue;
                }
            }

            // Try to inject using JSM SET3 position.
            // v0.18.3.297 (#85): skip the wrong world-state of a multi-state object
            // (e.g. the fallen ladder while it is still standing). Position data is
            // left intact deliberately -- only catalog injection is skipped.
            if (j >= 0 && j < MAX_JSM_ENTITIES && s_jsmStateSuppressed[j]) {
                Log::Field("FieldNavigation: [refresh] '%s' skipped: wrong world state "
                           "(state-exclusion group) [v0.18.3.297]", je.symName);
                continue;
            }
            if (!je.hasPosition) continue;
            // Validate position is in plausible range.
            if (je.posX == 0 && je.posY == 0 && je.posZ == 0) continue;

            // v0.17.7.1: Walkmesh exclusion for off-mesh Interactive Objects
            // without TALKRADIUS/TALKON. Lights and decorative props get
            // incorrectly promoted to JSM_ENT_INTERACTIVE_OBJECT during the
            // JSM scan (foundDialogOp/foundExtDispatch + SET3 position make
            // them look like real interactive objects). Almost all of them
            // sit off-walkmesh, so excluding off-mesh + no-talk-setup catches
            // the bug without dropping real signs/desks (those land on the
            // walkmesh because the player has to stand on top of them or
            // adjacent to them to read).
            //
            // Save/Draw/Shop/Card points are NOT filtered here: they may
            // use proximity (PARTICLEON + MENUSAVE etc.) rather than
            // TALKRADIUS, and they're always valuable navigation targets.
            // MAP_EXIT injection runs in a separate block below; it's also
            // not subject to this filter -- exits are always valuable.
            if (jt == ENT_OBJECT && !je.hasTalkSetup &&
                !IsInsideWalkmesh((float)je.posX, (float)je.posY)) {
                Log::Field("FieldNavigation: [walkmesh-excl] JSM ent%d '%s' "
                           "INTERACTIVE_OBJECT pos=(%d,%d) off-mesh + no-talk-setup -- excluded",
                           je.jsmIndex, je.symName, (int)je.posX, (int)je.posY);
                continue;
            }
            // v0.20.14: Caraway's Mansion puzzle objects (glass/statue: sym 'cup',
            // 'kakusi', etc.) are catalogued in every mansion phase but only become
            // interactive once the escape puzzle activates (progress > 376, the same
            // gate that unlocks the Mansion 4/5 doors). Suppress the mansion's JSM
            // objects until then. The explicit glfurin* test is REQUIRED: off the
            // mansion CarawayMansionSealed() is false, so !CarawayMansionSealed() alone
            // would drop every object on every field. Fires only on the not-sealed
            // side, so it never hides them during the puzzle (when the player needs them).
            if (jt == ENT_OBJECT && strncmp(s_currentFieldName, "glfurin", 7) == 0 &&
                !CarawayMansionSealed()) {
                Log::Field("FieldNavigation: [refresh] JSM object '%s' suppressed: Caraway's "
                           "Mansion escape puzzle not yet active (progress<=376) [v0.20.14]",
                           je.symName);
                continue;
            }
            // v0.61.0: THE SAME ENTITY, TWICE.
            //
            // On the Lunar Base control room the catalog listed ten things for four
            // people and two doors. Three of the ten were `rinoa`, `selphie` and
            // `quistis` injected from here as objects, at their STATIC SET3
            // positions -- (43,30) and (194,130) twice, one position for two
            // different people -- while the very same entities were already in the
            // catalog from the runtime scan, at their real live positions. Aaron:
            // "it had quite an extensive list of NPCs but there seemed to be
            // phantoms and duplicates among them."
            //
            // The live entity running this script and the script itself are one
            // object, and the live one is the one with the true position.
            // v0.62.0: which live entity that is comes from the MODEL join, not
            // from the slot -- v0.61.0 compared entityIdx against runtimeSlot,
            // which is exactly how the curated name "Quistis" landed on Piet. Drop this copy and hand its NAME to the live entry
            // -- which is the other half of the fix, because the runtime scan calls
            // everything "NPC" and Aaron also said "I couldn't find the NPC to let
            // me talk to Quistis in this scene." The name only moves if it is a
            // curated one from ENTITY_DISPLAY_NAMES; a generic type name teaches the
            // live entry nothing.
            //
            // Specials are deliberately excluded: for a Save/Draw/Shop/Card point
            // the JSM entry is the one carrying the meaning, and the co-located
            // generic runtime entry is what the runtime-vs-special dedupe removes.
            const int jeLive = LiveIndexForJSM(je);   // v0.62.0
            if ((jt == ENT_OBJECT || jt == ENT_NPC) && jeLive >= 0) {
                int liveDup = -1;
                for (int c = 0; c < newCount; c++)
                    if (newCatalog[c].entityIdx == jeLive) { liveDup = c; break; }
                if (liveDup >= 0) {
                    const char* curated = nullptr;
                    if (je.symName[0]) {
                        for (const EntityDisplayName* mN = ENTITY_DISPLAY_NAMES; mN->sym; mN++)
                            if (_stricmp(je.symName, mN->sym) == 0) { curated = mN->display; break; }
                    }
                    if (curated && curated[0] &&
                        strncmp(newCatalog[liveDup].name, "NPC", 3) == 0) {
                        Log::Field("FieldNavigation: [refresh] JSM object '%s' is live ent%d, "
                                   "already catalogued -- dropping the duplicate and naming the live "
                                   "entry '%s' [v0.61.0/v0.62.0]", je.symName, jeLive, curated);
                        strncpy(newCatalog[liveDup].name, curated,
                                sizeof(newCatalog[liveDup].name) - 1);
                        newCatalog[liveDup].name[sizeof(newCatalog[liveDup].name) - 1] = '\0';
                    } else {
                        Log::Field("FieldNavigation: [refresh] JSM object '%s' is live ent%d, "
                                   "already catalogued -- dropping the duplicate [v0.61.0/v0.62.0]",
                                   je.symName, jeLive);
                    }
                    continue;
                }
            }

            // v0.61.0: a card player or a shopkeeper who is not in this scene.
            // `piet` on the Lunar Base is a card opponent in a LATER scene; here his
            // live entity reads model=-1 tri=0 pos=(0,0), exactly what the v0.20.31
            // scene-actor test already drops -- but that test is gated on
            // `jt == ENT_OBJECT`, so a special sailed past it and the catalog offered
            // a Card Game at a static position with nobody standing there. Save and
            // Draw points are NOT included: they are not people and are routinely
            // script-only with no live entity at all (the Lunar Base infirmary save
            // point is one), and the draw point has its own sparkle gate.
            if (jt == ENT_CARD_GAME || jt == ENT_SHOP) {
                bool absent = false;
                if (jeLive < 0) {
                    // v0.62.0: the script hands this person a model, and no live
                    // entity in the scene is carrying it. That is a stronger
                    // statement of "not here" than the unplaced-block test below,
                    // and it is the one that actually catches `piet` in the Lunar
                    // Base infirmary -- an absent actor's live block is not merely
                    // unplaced, it does not exist.
                    absent = (je.modelParam >= 0);
                } else if (FF8Addresses::pFieldStateOthers &&
                           FF8Addresses::pFieldStateOtherCount) {
                    uint8_t* obC = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    int      ocC = (int)(*FF8Addresses::pFieldStateOtherCount);
                    if (obC && jeLive < ocC) {
                        uint8_t*  blkC = obC + ENTITY_STRIDE * jeLive;
                        uint16_t  ltC  = *(uint16_t*)(blkC + 0x1FA);
                        int32_t   lxC  = *(int32_t*)(blkC + 0x190);
                        int32_t   lyC  = *(int32_t*)(blkC + 0x194);
                        if (ltC == 0 && lxC == 0 && lyC == 0) absent = true;
                    }
                }
                if (absent) {
                    Log::Field("FieldNavigation: [refresh] JSM %s '%s' dropped: not present in "
                               "this scene (live ent%d) [v0.61.0/v0.62.0]",
                               EntityTypeName(jt), je.symName, jeLive);
                    continue;
                }
            }

            EntityInfo jsmEntry = {};
            jsmEntry.entityIdx  = -300 - j;  // unique sentinel for JSM-injected entities
            jsmEntry.modelId    = -1;
            jsmEntry.triangleId = je.posTriangle;
            jsmEntry.type       = jt;
            jsmEntry.gatewayIdx = -1;
            // v0.12.17: Resolve friendly display name for interactive objects.
            // For save/draw points, use the type name. For interactive objects,
            // resolve the SYM name to a user-friendly name (e.g. "dic" -> "Directory").
            char friendlyBuf[48] = {};
            const char* jtName = EntityTypeName(jt);
            if (je.symName[0] != '\0') {
                // v0.18.3.291 (#85): require a REAL table hit, not the
                // raw-SYM-cleanup fallback. ResolveFriendlyName() never returns
                // empty -- on a table miss it capitalizes the SYM and returns
                // THAT, so this branch silently announced internal dev symbols.
                // The .290 BAT proved it: dropping the bogus ladline->"Ladder"
                // rows yielded "Ladline5"/"Ladline6"/"Ladline7" spoken verbatim,
                // not the generic "Object" expected -- violating the
                // never-expose-SYM rule. Check the table explicitly; on a miss
                // keep the generic type name, which says nothing false. Scoped to
                // this JSM-injection path; runtime-entity naming is untouched.
                bool symInTable291 = false;
                for (const EntityDisplayName* m291 = ENTITY_DISPLAY_NAMES;
                     m291->sym != nullptr; m291++) {
                    if (_stricmp(je.symName, m291->sym) == 0) { symInTable291 = true; break; }
                }
                if (symInTable291) {
                    ResolveFriendlyName(je.symName, friendlyBuf, sizeof(friendlyBuf));
                    if (friendlyBuf[0] != '\0' && jt != ENT_SAVE_POINT && jt != ENT_DRAW_POINT
                        && jt != ENT_SHOP && jt != ENT_CARD_GAME)
                        jtName = friendlyBuf;  // use friendly name for interactive objects
                } else {
                    Log::Field("FieldNavigation: [refresh] sym='%s' not in display-name table "
                               "-- using generic '%s' rather than exposing the SYM [v0.18.3.291]",
                               je.symName, jtName);
                }
            }
            // ITEM-1: a detected ladder always announces as "Ladder".
            if (je.type == FieldArchive::JSM_ENT_LADDER) jtName = "Ladder";
            // v0.18.3.292 (#85): " (approx.)" suffix dropped from the SPOKEN label
            // (still logged below). Centroid positions have proven accurate -- the
            // .291 BAT had Aaron at the gate with "Gate (approx.)" at 1 step and
            // "In range."; he asked for the tag to go. It also cost two extra
            // spoken words on every announcement. A wrong centroid is a bug to
            // fix, not something to hedge with a label.
            bool isApprox286 = (j < MAX_JSM_ENTITIES) && s_jsmTriangleApprox[j];
            strncpy(jsmEntry.name, jtName, sizeof(jsmEntry.name) - 1);
            jsmEntry.name[sizeof(jsmEntry.name) - 1] = '\0';
            {
                // v0.20.35 (WS1 Step 1.2): fold the live-state picture into the audit.
                // For a live "other" (jsmCategory 3), read its engine struct: flags word
                // +0x160 (bit3 = HIDE, cleared by SHOW), model id +0x218, triangle +0x1FA.
                // Bounds-checked, NO SEH -- same idiom as the phantom filter above (base
                // valid on-field, index < live other-count). Log-only; feeds the Step 1.3
                // unified live gate by showing, per surviving object, which signal gates it.
                char live[112] = "";
                if (je.jsmCategory == 3 &&
                    FF8Addresses::pFieldStateOthers && FF8Addresses::pFieldStateOtherCount) {
                    uint8_t* obP2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    int oirP2 = je.jsmIndex - (s_jsmDoors + s_jsmLines + s_jsmBackgrounds);
                    int ocnt2 = (int)(*FF8Addresses::pFieldStateOtherCount);
                    if (obP2 && oirP2 >= 0 && oirP2 < ocnt2) {
                        uint8_t* blk2 = obP2 + ENTITY_STRIDE * oirP2;
                        uint32_t fl2 = *(uint32_t*)(blk2 + 0x160);
                        int16_t  md2 = *(int16_t*)(blk2 + 0x218);
                        uint16_t tr2 = *(uint16_t*)(blk2 + 0x1FA);
                        snprintf(live, sizeof live,
                                 " live[slot=%d flags@160=0x%08X hide=%d model=%d tri=%u]",
                                 oirP2, (unsigned)fl2, (int)((fl2 >> 3) & 1), (int)md2, (unsigned)tr2);
                    } else if (obP2 && oirP2 >= 0) {
                        snprintf(live, sizeof live, " live[slot=%d/%d NO-SLOT]", oirP2, ocnt2);
                    }
                }
                char caBuf[192];
                snprintf(caBuf, sizeof caBuf, "talk=%d setline=%d jsmCat=%d%s",
                         (int)je.hasTalkSetup, (int)je.hasSetline, (int)je.jsmCategory, live);
                CatAudit("object", jsmEntry, (int)je.posX, (int)je.posY, je.symName, caBuf);
                LiveGateObject("object", jsmEntry, je.jsmIndex, (uint16_t)je.posTriangle, (int)je.posX, (int)je.posY);
            }
            if (s_zoneValid && jt != ENT_DRAW_POINT && !ZoneReachablePoint((float)je.posX, (float)je.posY)) { Log::Field("FieldNavigation: [refresh] '%s' filtered: another camera zone (unreachable from player) [v0.20.21]", jsmEntry.name); continue; }  // v0.20.48: draw points exempt -- cfg==1 + on-mesh already == sighted-player parity
            newCatalog[newCount++] = jsmEntry;
            Log::Field("FieldNavigation: [refresh] JSM-injected %s at (%d,%d) sym='%s'%s",
                       jtName, (int)je.posX, (int)je.posY, je.symName,
                       isApprox286 ? " [triangle-centroid approx]" : "");
        }
}

// v0.62.2 (#123): read the live story variable this script is gated on and
// answer the engine's own comparison. No gate, or a variable we cannot read, is
// "open" -- absence of evidence never hides anything.
static bool JsmGateOpen(const FieldArchive::JSMEntityInfo& je, int32_t* outLive)
{
    if (!je.hasGate) return true;
    int32_t live = 0;
    bool read = false;
    __try {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(0x01CFE9B8u) + je.gateAddr;
        if      (je.gateWidth == 1) live = (int32_t)(*p);
        else if (je.gateWidth == 2) live = (int32_t)(*(const uint16_t*)p);
        else                        live = (int32_t)(*(const uint32_t*)p);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
    if (!read) return true;
    if (outLive) *outLive = live;
    return FieldArchive::JsmGateSatisfied(je.gateOp, je.gateValue, live);
}

// v0.62.2 (#123): JSM entities whose exit was suppressed because their trigger's
// story gate is currently false. The live entity running such a script is a thing
// that does nothing right now, so the dedupe drops it too -- otherwise closing the
// gate simply puts the platform back in the catalog as a nameless "NPC", which is
// the very thing Aaron walked to and reported.
static bool s_jsmGateClosed[MAX_JSM_ENTITIES];

// ============================================================================
// 3e. JSM_ENT_MAP_EXIT injection  (was field_nav_catalog_mapexits.inl)
// ============================================================================
static void InjectMapExits(EntityInfo* newCatalog, int& newCount, uint8_t* base, uint8_t lim)
{
// ============================================================================
// field_nav_catalog_mapexits.inl — JSM_ENT_MAP_EXIT catalog injection
// ============================================================================
// v0.18.3.266: extracted verbatim from field_nav_catalog.inl to keep that file
// under the CI source-file size ceiling (.github/workflows/safety-checks.yml:
// soft warn > 60 KB, HARD FAIL > 80 KB). field_nav_catalog.inl had reached
// 82 KB and the push utility's local mirror of the CI check refused the push.
//
// Same pattern as field_nav_catalog_dedupe.inl (v0.17.8.9) and
// field_nav_catalog_naming.inl: this is NOT a standalone function. It is a
// fragment of RefreshCatalog()'s body, #included inline at the point where the
// block used to sit, so it operates directly on that function's locals:
//
//   newCatalog[] / newCount   — catalog under construction
//   base / lim                — runtime "others" entity array + count
//   s_jsmEntities[] / s_jsmEntityCount
//   s_capturedLines[] / s_capturedLineCount
//   s_gateways[] / s_gatewayCount
//   s_symNames[] / s_symNameCount / s_symOthersOffset
//   s_playerEntityIdx
//
// Behaviour is byte-for-byte identical to the pre-extraction code; this was a
// pure textual move with no logic change.
//
// What it does: turns JSM "Others" entities classified JSM_ENT_MAP_EXIT
// (elevators, doors, trigger zones whose scripts contain MAPJUMP) into ENT_EXIT
// catalog entries, resolving the destination name and a position from SET3 or a
// captured SETLINE centre, then filtering dead/duplicate/off-screen exits.
// ============================================================================

        memset(s_jsmGateClosed, 0, sizeof(s_jsmGateClosed));   // v0.62.2
        // v0.07.83: Entity-based exits from JSM_ENT_MAP_EXIT "Other" entities.
        // These are interactive objects (elevators, doors, trigger zones) whose
        // scripts contain MAPJUMP. They have destination field IDs in param.
        // Position from SET3 extraction or runtime entity, or captured SETLINE.
        for (int j = 0; j < s_jsmEntityCount && newCount < MAX_CATALOG; j++) {
            const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
            if (je.type != FieldArchive::JSM_ENT_MAP_EXIT) continue;
            // v0.63.0 (#123): A PERSON YOU TALK TO IS NOT A DOOR.
            //
            // Aaron, in the escape pod: "there were some catalog glitches along
            // the way, most notably Ellone being identified as an exit." On
            // sspod2 `elone` carries the MAPJUMP to Outer Space 4 in her OWN
            // script -- talking to her is what starts the departure -- and she
            // is talkable, with a model, standing in the room. The catalog
            // listed "Exit to Outer Space 4" at her feet and (v0.62.1) dropped
            // the live Ellone in its favour, so the one person he needed was
            // announced as a door.
            //
            // The discriminator is provenance, not distance. A REQ-follow exit
            // is a mechanism the player steps on or operates -- the Lunar Base
            // pod lift, the Deling City buses -- and those keep working exactly
            // as they did. An exit whose MAPJUMP is in the entity's own script,
            // on an entity the player can talk to, is a conversation with a
            // consequence: the person is the thing to go to, and the catalog
            // already has them.
            if (!je.exitFromReqFollow && je.hasTalkSetup) {
                int jeTalk = LiveIndexForJSM(je);
                if (jeTalk >= 0 && GetEntityModelId(jeTalk) >= 0) {
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped: "
                               "its MAPJUMP is in its own talk script and live ent%d is a "
                               "character standing here -- go to the person, not a door "
                               "[v0.63.0]", je.symName, je.param, jeTalk);
                    continue;
                }
            }
            // v0.76.0 (#112): AND A MONSTER IS NOT A DOOR EITHER.
            //
            // rgguest2's alien01 is the passenger-compartment Propagator. Its
            // script ends in MAPJUMP3 to Aisle 2 -- because that is how the
            // forced-battle cutscene finishes, by ejecting the player from the
            // room -- so the scanner classified it JSM_ENT_MAP_EXIT and the
            // catalog offered "Exit to Ragnarok - Aisle 2" standing at (0,-500),
            // which is where the monster is. Worse, the v0.62.1 dedup then
            // dropped the live "Yellow Propagator" as a duplicate of its own
            // exit, so the room listed a door where the monster was and no
            // monster at all. A blind player navigating to that exit walks into
            // the thing the last four builds have been keeping him away from.
            //
            // The talk-script rule above does not catch it: that one needs
            // hasTalkSetup, and a Propagator has no TALKON -- there is nothing
            // to talk to. The provenance test is the same though. A MAPJUMP at
            // the end of a battle cutscene is a consequence, not a route, and
            // PG_LIST already knows exactly which eight entities those are.
            //
            // It self-corrects on death without this rule, which is what made it
            // hard to see: once the yellow one is dead its script's story gate
            // reads false and the exit is suppressed as LOCKED, the Propagator
            // survives dedup, and the room's real INF gateway is offered. The
            // room was only wrong while the monster was alive -- which is the
            // whole time the player needs it to be right.
            if (!je.exitFromReqFollow &&
                PgIsPropagator(FF8Addresses::pCurrentFieldName, je.symName)) {
                Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped: it is a "
                           "Propagator, and the MAPJUMP at the end of its script is how its "
                           "battle cutscene ejects him from the room -- not a way out of it "
                           "[v0.76.0]", je.symName, je.param);
                continue;
            }
            // v0.62.2 (#123): AN EXIT THE STORY HAS NOT OPENED YET.
            //
            // Aaron, standing on the Lunar Base pod lift: "it said I arrived at it,
            // but nothing happened even when I pressed the confirm button." Nothing
            // was wrong with the exit or with getting there. `ele`'s script opens
            //   PSHM_W var[256]; PSHN_L 2552; == ; JMPZ
            // and does nothing at all unless the story word already reads 2552 --
            // the value `elone`'s talk script writes to it when you speak to Ellone
            // in Control Room 1. The lift is not broken; it is not open yet.
            //
            // Read the live variable and evaluate the engine's own comparison
            // (operator table at 0x00B8DE4C). Scoped to REQ-follow exits, the only
            // ones that carry a gate, and to the exact guard shape the scanner
            // could decode -- everything else is unaffected.
            if (je.hasGate) {
                int32_t live = 0;
                if (!JsmGateOpen(je, &live)) {
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped: "
                               "its trigger is gated on var[%d]=%d op%d %d, which is false "
                               "right now -- not open yet [v0.62.2]",
                               je.symName, je.param, je.gateAddr, live,
                               (int)je.gateOp, je.gateValue);
                    if (j >= 0 && j < MAX_JSM_ENTITIES) s_jsmGateClosed[j] = true;
                    continue;
                }
            }
            // v0.62.0 (#123): a party member is never a door. The v0.62.0
            // REQ-follow attributes a scripted jump to whichever entity triggers
            // it, which is right for a lift or a bus but wrong when the trigger
            // is the leader's own script running a cutscene -- `Squall` on
            // bchtl1a, `squall` on bg2f_31 and bgkote_2. The live entity's setpc
            // says which characters those are, and the model join says which
            // live entity this script is.
            {
                int jePc = LiveIndexForJSM(je);
                if (jePc >= 0 && IsPartyCharacterSetpc(GetEntitySetpc(jePc))) {
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped: "
                               "its script belongs to party member ent%d, not to a door "
                               "[v0.62.0]", je.symName, je.param, jePc);
                    continue;
                }
            }
            // v0.20.8: drop a scripted map-exit to a WORLD-MAP STAGING field -- a wm* field
            // (id 0..79) that no INF gateway anywhere targets, so you only ever arrive there ON
            // the world map, never walk into it via a field exit. Real world-map view fields
            // (efview/edview, which ARE gatewayed) are excluded. See field_nav_duproom.inl.
            if (IsWorldMapStaging((uint16_t)je.param)) {
                Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped "
                           "(world-map staging field: scripted scene transition, not a walk-in exit)",
                           je.symName, je.param);
                continue;
            }
            // v0.20.7: duplicated-room phantom drop. If this scripted exit's destination
            // is one a real INF gateway serves in another copy of THIS room (same walkmesh)
            // and this copy has no local gateway there, it is the cutscene twin of that
            // gateway exit -- not a navigable exit. See field_nav_duproom.inl.
            {
                bool dupPhantom = false;
                for (int s = 0; s < s_dupSuppressCount; s++)
                    if (s_dupSuppressDests[s] == (uint16_t)je.param) { dupPhantom = true; break; }
                if (dupPhantom) {
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped "
                               "(duplicated-room phantom: the real exit is an INF gateway in another copy of this room)",
                               je.symName, je.param);
                    continue;
                }
            }
            // Skip if already in catalog as a runtime entity or trigger line exit.
            bool alreadyInCatalog = false;
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].type == ENT_EXIT) {
                    // Check if this JSM entity's destination matches an existing exit.
                    // Also check if the SYM name matches a runtime entity already added.
                    if (newCatalog[c].entityIdx <= -200) {
                        // Trigger line exit — check destination.
                        int ti = -(newCatalog[c].entityIdx + 200);
                        if (ti >= 0 && ti < s_capturedLineCount &&
                            s_capturedLines[ti].destFieldId == je.param) {
                            alreadyInCatalog = true; break;
                        }
                    }
                }
            }
            if (alreadyInCatalog) continue;
            // Resolve destination name.
            char exitName[48];
            int destId = je.param;
            if (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) {
                snprintf(exitName, sizeof(exitName), "Exit to %s", FIELD_DISPLAY_NAMES[destId]);
            } else if (destId == -2) {
                strncpy(exitName, "Exit to World Map", sizeof(exitName) - 1);
            } else {
                strncpy(exitName, "Exit", sizeof(exitName) - 1);
            }
            exitName[sizeof(exitName) - 1] = '\0';
            // v0.18.3.284 (#86 follow-up): true once exitName above resolved to a
            // real destination (a known field or the world map), not the generic
            // "Exit" fallback. Gates the live-entity-position fallback below so it
            // can never rescue an entity like 'tobi' (glprein1 ent9) whose param is
            // an unresolved runtime-var marker -- only a MAP_EXIT the interpreter/
            // scanner already trusts the destination of gets a fabricated position.
            bool destResolved = (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) || destId == -2;
            // Find position: try matching captured SETLINE by entity address range,
            // or use SET3 position from JSM scan.
            float exitX = 0, exitY = 0;
            bool hasPos = false;
            if (je.hasPosition) {
                exitX = (float)je.posX;
                exitY = (float)je.posY;
                hasPos = true;
            }
            // v0.07.84: If no SET3 position, try matching SYM name to a captured
            // SETLINE entity. "Other" entities that call SETLINE (e.g. saveline0
            // elevator trigger) have their SETLINE coordinates captured at runtime
            // with accurate positions, even when SET3 extraction fails.
            if (!hasPos && je.symName[0] != '\0' && base) {
                uint32_t baseAddr = (uint32_t)(uintptr_t)base;
                for (int t = 0; t < s_capturedLineCount; t++) {
                    uint32_t lineEntAddr = s_capturedLines[t].entityAddr;
                    // Check if this captured line belongs to an Others entity.
                    if (lineEntAddr >= baseAddr &&
                        lineEntAddr < baseAddr + ENTITY_STRIDE * lim) {
                        int entIdx = (int)((lineEntAddr - baseAddr) / ENTITY_STRIDE);
                        // v0.62.0: entIdx is a LIVE index, so it is compared
                        // against the live index the model join found for this
                        // script -- not against its slot, which v0.60.0 did and
                        // which only coincides when the scene instantiates every
                        // entity the field declares.
                        const FieldArchive::JSMEntityInfo* jl = FindJSMByLiveEntity(entIdx);
                        bool ownerMatches = (jl && jl->jsmIndex == je.jsmIndex);
                        if (ownerMatches) {
                            exitX = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                            exitY = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                            hasPos = true;
                            // v0.07.87: Write position back to JSM entity so
                            // AnnounceDirections can read it for compass.
                            s_jsmEntities[j].posX = (int16_t)exitX;
                            s_jsmEntities[j].posY = (int16_t)exitY;
                            s_jsmEntities[j].hasPosition = true;
                            Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' position from SETLINE center (%.0f,%.0f)",
                                       je.symName, exitX, exitY);
                            break;
                        }
                    }
                }
            }
            // v0.18.3.284 (#86 follow-up): Live runtime-position fallback for
            // scripted/hidden MAP_EXIT entities that have neither a SET3 position
            // nor a captured SETLINE. glprein1's trapdoor ('irvine') is triggered
            // by TALKRADIUS/interaction, not a walk-across line, so it never calls
            // SETLINE, and its init script never calls SET3 either -- both existing
            // position sources come up empty and the exit was injected at pos=(0,0),
            // unreachable by manual or auto navigation (BAT 2026-07-18: "trapdoor to
            // the clocktower still has no coordinates"). But the entity IS placed on
            // the walkmesh at runtime (confirmed via the [SCAN] pass, tri=38) --
            // it's only left out of the general NPC/interaction scan because a HIDE
            // flag marks it invisible. GetEntityPos() doesn't check that flag, only
            // walkmesh placement (triId != 0), so it can still read the entity's
            // live position directly. je.jsmIndex is the flat Door+Line+Bg+Other
            // scan index, and it maps 1:1 onto the runtime "Others" array index
            // used elsewhere by GetEntityPos/the [SCAN] loop with NO subtraction of
            // the doors+lines+backgrounds count -- confirmed both here (glprein1
            // 'irvine': jsmIndex=2, [SCAN] reports the same entity as ent2) and on
            // glwater1 (sakua/sakub/oku: jsmIndex 17/18/19, independently found in
            // an earlier BAT to sit past the old MAX_ENTITIES=16 cap, which only
            // holds if their runtime index is the unmodified jsmIndex, not
            // jsmIndex-9). Gated on destResolved so an entity like 'tobi' (glprein1
            // ent9, an unresolved runtime-var marker destination) can't be rescued
            // into a bogus positioned "Exit" entry by this fallback.
            //
            // SAFETY CHECK: the jsmIndex->runtime-slot mapping can coincidentally
            // land on a LIVE PARTY MEMBER's slot instead of the scripted exit
            // entity's own slot. glclock1's 'irvine' MAP_EXIT (the false exit
            // .283 just fixed) has jsmIndex=2, and glclock1's runtime slot 2 is
            // actually Rinoa (setpc=4) mid-scene -- without this check, this
            // fallback would silently reintroduce the false "Exit to wm05" with a
            // fabricated-but-plausible position (Rinoa's), defeating .283's veto
            // (which only fires when !hasPos). glprein1's trapdoor slot was
            // confirmed NOT a party-character slot: its [SCAN-DROP] hidden-filter
            // log line fires with no preceding [party-filter] line, meaning the
            // party-filter check (which runs first) already tested and rejected
            // isPartyChar for it. Refuse the fallback whenever the live slot's
            // setpc reads as a valid party character (0-7) -- a party member's
            // position is never a map exit's position.
            if (!hasPos && destResolved && je.jsmCategory == 3) {
                // v0.60.0: the runtime slot, not the group index. The comment above
                // reasons that je.jsmIndex "maps 1:1 onto the runtime Others array
                // index ... with NO subtraction", citing glprein1's 'irvine'
                // (jsmIndex 2, seen as ent2) and glwater1's sakua/sakub/oku
                // (jsmIndex 17/18/19). Both are coincidences of those fields:
                // glprein1 has 2 background entities, so its first Other IS group 2
                // and runtime slot 0 -- the 1:1 reading and the correct one give
                // different answers there and only the log line was checked. glwater1
                // has 5 lines and 4 backgrounds, so 17/18/19 are slots 8/9/10, which
                // also removes the reason the comment gives for believing the
                // unshifted reading (that they "sit past the old MAX_ENTITIES=16
                // cap"). Reading the wrong slot fabricates a plausible position for a
                // map exit out of some other entity's feet, which is a phantom exit
                // by any definition. JSMEntityInfo::runtimeSlot is the identity.
                // v0.62.0: runtimeSlot is a SLOT, and the live array is not
                // indexed by slot -- ask the model join which live entity is
                // running this script. -1 means it is not in the scene at all,
                // which is the honest answer and stops the position being
                // fabricated out of some other entity's feet.
                int liveIdx = LiveIndexForJSM(je);
                if (!IsPartyCharacterSetpc(GetEntitySetpc(liveIdx))) {
                    float rex = 0, rey = 0;
                    if (GetEntityPos(liveIdx, rex, rey)) {
                        exitX = rex;
                        exitY = rey;
                        hasPos = true;
                        s_jsmEntities[j].posX = (int16_t)exitX;
                        s_jsmEntities[j].posY = (int16_t)exitY;
                        s_jsmEntities[j].hasPosition = true;
                        Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' position from "
                                   "live entity idx=%d (%.0f,%.0f) [hidden/scripted fallback]",
                                   je.symName, liveIdx, exitX, exitY);
                    }
                }
            }
            // Screen filter: skip exits on the other side of trigger lines.
            if (hasPos && s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
                float plX = 0, plY = 0;
                if (GetEntityPos(s_playerEntityIdx, plX, plY)) {
                    if (IsSeparatedByTriggerLine(plX, plY, exitX, exitY))
                        continue;
                }
            }
            // v0.18.3.283 (#86 follow-up): don't trust an unpositioned MAP_EXIT
            // when a trigger-line (SCREEN_BOUND) Exit already covers this field.
            // glclock1 has exactly one real exit -- the SCREEN_BOUND 'squall'
            // trigger line back to Presidential Residence 7, confirmed against
            // the live [MAPJUMP-HOOK] oracle (destField=746) and against Aaron's
            // own play (2026-07-18 BAT: "just the one exit... back to Residence
            // 7"). Its 'irvine' MAP_EXIT entity ALSO resolves via INTERP to a
            // concrete literal (field 5, "wm05" -- an internal placeholder, not
            // a real destination) -- provenance-tainting (.282) doesn't catch
            // this because the value genuinely is a hardcoded literal on
            // whichever branch the interpreter reached; that branch is simply
            // not the one the live engine takes here (inactive/off-context
            // script path, not a live-variable problem). The dedup check above
            // only catches this when both destinations happen to MATCH, which
            // isn't the case for a wrong resolution. This field has 0 INF
            // gateways, so neither gateway-cross-check below ever runs either --
            // without this rule the wrong exit sails through uncontested.
            // Position-having MAP_EXITs are NOT touched by this rule: a real,
            // separately-walkable second door is still trusted (glprein1's
            // trapdoor has no competing trigger-line exit at all, so it's
            // unaffected either way).
            if (!hasPos) {
                bool triggerExitExists = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].type == ENT_EXIT &&
                        newCatalog[c].entityIdx <= -200 && newCatalog[c].entityIdx > -300) {
                        triggerExitExists = true;
                        break;
                    }
                }
                if (triggerExitExists) {
                    Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' dest=%d filtered "
                               "(unpositioned, and a trigger-line exit already covers this field)",
                               je.symName, je.param);
                    continue;
                }
            }

            // v0.17.8.6: Suppress dead positionless exits with unresolved
            // destinations. bgryo2_1 ent15 'l1' is a JSM_ENT_MAP_EXIT with no
            // SET3/SETLINE position and param=INT_MIN (0x80000000 -- a runtime-var
            // destination the static scan could not resolve). On a field with no
            // INF gateways the gateway-suppression check below never fires, so
            // without this the entity injects a bare second "Exit" with no
            // position -- the duplicate, useless exit Aaron reported. An exit that
            // has neither a navigable position nor a resolvable/world-map
            // destination cannot be driven to or named; drop it. (param==-2 is the
            // world-map sentinel and is kept.)
            // v0.20.12: resolver-flagged story-locked exit -> drop (any position).
            if ((uint32_t)je.param == (uint32_t)FieldArchive::EXIT_LOCKED_MARKER) {
                Log::Field("FieldNavigation: [catalog] JSM exit '%s' LOCKED (story gate false) "
                           "-- suppressed [v0.20.12]", je.symName);
                continue;
            }
            if (!hasPos && je.param != -2 &&
                (je.param < 0 || je.param >= FIELD_DISPLAY_NAMES_COUNT)) {
                Log::Field("FieldNavigation: [refresh] MAP_EXIT '%s' dropped: "
                           "no position, unresolved dest (param=%d)",
                           je.symName, je.param);
                continue;
            }
            EntityInfo mapExit = {};
            // v0.07.95: Suppress JSM exits with runtime-resolved destinations
            // when INF gateways exist on this field. The INF gateway system
            // handles those same physical exits with proper static destinations.
            // v0.08.01: Bit31 marker check. PSHM_W-sourced MAPJUMP destinations
            // have bit31 set (negative values). Also check param > 982 for
            // the 0x00FFxx markers that survived before the bit31 change.
            // v0.60.0: `param == -2` is the world map -- a fully resolved
            // destination, not an unresolved runtime marker. It is negative, so the
            // suppression below used to delete every world-map exit on any field
            // that also has INF gateways, which is most of the fields that have one
            // (a town gate, a garden entrance). INF gateways cannot cover it: a
            // gateway's destination is a field id and the world map has none.
            if (s_gatewayCount > 0 && je.param != -2 && (je.param < 0 || je.param > 982))
                continue;

            // v0.12.08 Fix A: Filter JSM exit destinations against INF gateways.
            // When a field has INF gateways, they define the known valid exits.
            // If a JSM MAP_EXIT destination doesn't match any INF gateway destination,
            // it's likely a stale runtime variable value (PSHM_W) and should be skipped.
            // (e.g., "Exit to Dollet Comms Tower" appearing in B-Garden hallways)
            //
            // v0.18.3.281 (#86): this "unmatched = stale" assumption doesn't hold
            // when the destination came from the authoritative interpreter
            // (info.paramFromInterp) rather than the abstract fallback resolver.
            // glprein1's trapdoor ('irvine', destField=716) is a real third exit
            // that this field's 2 INF gateways simply never registered -- scripted/
            // hidden mechanisms typically aren't INF walk-through triggers. An
            // INTERP result is the concrete destField the engine will actually use,
            // so it's trusted even without an INF match; a fallback-sourced (LITERAL/
            // VARBLOCK) destination that doesn't match is still treated as likely
            // stale, same as before (this is what the original bgryo2_1 'l1' fix
            // relied on, and stays intact).
            // v0.60.0: same reasoning -- a world-map destination can never match an
            // INF gateway's destination field id, so this filter would always reject
            // it. Exempt it rather than have it fail a test it cannot pass.
            if (s_gatewayCount > 0 && !je.paramFromInterp && je.param != -2) {
                bool destMatchesGateway = false;
                for (int gi = 0; gi < s_gatewayCount; gi++) {
                    if (s_gateways[gi].destFieldId == (uint16_t)je.param) {
                        destMatchesGateway = true;
                        break;
                    }
                }
                if (!destMatchesGateway) {
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d filtered "
                               "(no matching INF gateway on this field)",
                               je.symName, je.param);
                    continue;
                }
            }

            // v0.131.7 (#centra): a party member's script is not a doorway, and an
            // exit with no position cannot be walked to. See
            // jsm_exit_surface_model.inl -- crtower2 listed the same exit four
            // times and the fourth REFUSED the drive, four times over.
            if (!JsmExitShouldSurface(IsPartyCharacterSym(je.symName),
                                      (int)je.posTriangle, (int)je.posX, (int)je.posY)) {
                // v0.131.8: once per field load, not once per refresh. The rule
                // is a property of the field's script, so it produces exactly
                // the same verdict every rebuild -- on crtower2 that was seven
                // lines repeated four times in a few seconds.
                if (!s_mapExitTraced)
                    Log::Field("FieldNavigation: [catalog] JSM exit '%s' dest=%d dropped: "
                               "%s [v0.131.7]", je.symName, je.param,
                               IsPartyCharacterSym(je.symName)
                                   ? "it is a party character's own transition code, not a doorway"
                                   : "no position -- the drive would refuse it");
                continue;
            }

            mapExit.entityIdx  = -300 - j;  // JSM-injected sentinel
            mapExit.modelId    = -1;
            mapExit.triangleId = je.posTriangle;
            mapExit.type       = ENT_EXIT;
            mapExit.gatewayIdx = -1;
            strncpy(mapExit.name, exitName, sizeof(mapExit.name) - 1);
            mapExit.name[sizeof(mapExit.name) - 1] = '\0';
            { char caBuf[80]; snprintf(caBuf, sizeof caBuf, "dest=%d fromInterp=%d", (int)je.param, (int)je.paramFromInterp);
                CatAudit("mapexit", mapExit, (int)je.posX, (int)je.posY, je.symName, caBuf); LiveGatePos("mapexit", mapExit, (int)je.posX, (int)je.posY, true); }
            // v0.20.22: a map-exit is an EXIT -- never zone-filtered (exits are navigation aids; never hide one).
            newCatalog[newCount++] = mapExit;
        }
}

// ============================================================================
// 3f. INF gateway exit injection  (was field_nav_catalog_gateways.inl)
// ============================================================================
// v0.20.14: Caraway's Mansion escape-puzzle gate. The Caraway corridor room is
// reused across field files (glfurin*). Its INF gateway to Mansion 6 (dest 726)
// stays ARMED the whole arc, but a physical door in front of it is OPEN in the
// explore/mission phase and LOCKED once the escape puzzle activates -- so the
// armed gateway is a phantom exit from then on. That same transition unlocks the
// Mansion 4/5 doors and makes the glass/statue puzzle objects interactive; it is
// the story-progress counter (0x01CFE9B8+0x100) crossing 376 (0x178) -- the exact
// gate the Mansion 4/5 door scripts test (see v0.20.12's interpreter suppression).
// (v0.20.13 used byte 0x0F2, which flips ~18 progress-units too early -- at mission
// start, not puzzle start -- and even flickered within one progress value; the
// monotonic progress word is the stable signal.) "Sealed" = escape puzzle active:
// M6 locked, M4/5 + glass live. glfurin* only. SEH-guarded live read.
static bool CarawayMansionSealed()
{
    if (strncmp(s_currentFieldName, "glfurin", 7) != 0) return false;
    uint16_t prog = 0; bool ok = true;
    __try {
        prog = *(const volatile uint16_t*)(uintptr_t)(0x01CFE9B8u + 0x100u);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok && (prog > 376);
}

static void InjectGatewayExits(EntityInfo* newCatalog, int& newCount)
{
// ============================================================================
// field_nav_catalog_gateways.inl — INF gateway exit injection
// ============================================================================
// v0.18.3.276: extracted verbatim from field_nav_catalog.inl to bring that file
// back under the CI source-file size ceiling (.github/workflows/safety-checks.yml:
// soft warn > 60 KB, HARD FAIL > 80 KB). It had grown to 82.2 KB across the
// #83/#82/#71 work and the push utility refused the push.
//
// Same pattern as field_nav_catalog_mapexits.inl (v0.18.3.266),
// field_nav_catalog_dedupe.inl and field_nav_catalog_naming.inl: this is NOT a
// standalone function. It is a fragment of RefreshCatalog()'s body, #included
// inline where the block used to sit, so it operates directly on that
// function's locals:
//
//   newCatalog[] / newCount    — catalog under construction
//   s_gateways[] / s_gatewayCount
//   s_dedupGateways[] / s_dedupGatewayCount
//   s_capturedLines[] / s_capturedLineCount
//   s_playerEntityIdx
//
// Behaviour is byte-for-byte identical to the pre-extraction code; this was a
// pure textual move with no logic change.
//
// What it does: groups raw INF gateways by destination field, averages each
// group's centre into one catalog entry, filters groups whose path from the
// player actually crosses a screen-boundary segment, de-duplicates against
// exits already in the catalog, and injects the survivors as ENT_EXIT entries.
// ============================================================================

        // v0.07.94: Add deduplicated INF gateway exits to catalog.
        // Group gateways by destFieldId, average their centers, create one
        // catalog entry per unique destination. Skip gateways whose center is
        // on the other side of a screen-boundary trigger line from the player.
        // Also skip gateways whose destination already has a JSM-detected exit.
        s_dedupGatewayCount = 0;
        memset(s_dedupGateways, 0, sizeof(s_dedupGateways));
        // v0.18.3.301 (#91 R3): two gateways sharing a destination are the SAME
        // exit only if they are also in the same place. Observed separations:
        // genuine duplicates < 200 units; the prison ring's two crossings 3,644.
        const float GATEWAY_CLUSTER_RADIUS = 800.0f;
        for (int g = 0; g < s_gatewayCount && s_dedupGatewayCount < MAX_DEDUP_GATEWAYS; g++) {
            uint16_t destId = s_gateways[g].destFieldId;
            // Find existing dedup group for this destination.
            //
            // v0.18.3.301 (#91 R3): matching the destination is NOT enough.
            // The D-District Prison shaft is a circular walkway around a
            // central hole, split across two screens -- gpbig1a is the left
            // half, gpbig2a the right -- with a crossing at the TOP of the
            // circle and another at the BOTTOM. Both crossings lead to the
            // other half, so both gateways carry the same destFieldId, and
            // grouping on that alone collapsed two exits **3,644 units apart**
            // into a single entry at their midpoint -- open floor belonging to
            // neither crossing, which auto-drive then obediently walked to.
            //
            // So a candidate only joins a group if it is also spatially near
            // it. Genuine duplicates (the case this dedupe exists for) sit
            // within ~200 units of each other; the shaft pair is 18x that.
            // 800 leaves a wide margin on both sides.
            //
            // centerX/centerY hold running SUMS at this point (they are
            // averaged after the loop), hence the divide by count.
            int groupIdx = -1;
            for (int d = 0; d < s_dedupGatewayCount; d++) {
                if (s_dedupGateways[d].destFieldId != destId) continue;
                if (s_dedupGateways[d].count > 0) {
                    float avgX = s_dedupGateways[d].centerX / (float)s_dedupGateways[d].count;
                    float avgY = s_dedupGateways[d].centerY / (float)s_dedupGateways[d].count;
                    float ddx  = s_gateways[g].centerX - avgX;
                    float ddy  = s_gateways[g].centerZ - avgY;  // centerZ = Y in our coords
                    if ((ddx * ddx + ddy * ddy) >
                        (GATEWAY_CLUSTER_RADIUS * GATEWAY_CLUSTER_RADIUS)) {
                        Log::Field("FieldNavigation: [refresh] INF-GW gw%d dest=%u at (%.0f,%.0f) "
                                   "SPLIT from group %d at (%.0f,%.0f) -- %.0f units apart "
                                   "[v0.18.3.301 #91 R3]",
                                   g, (unsigned)destId,
                                   s_gateways[g].centerX, s_gateways[g].centerZ,
                                   d, avgX, avgY,
                                   sqrtf(ddx * ddx + ddy * ddy));
                        continue;  // same destination, different place -- new group
                    }
                }
                groupIdx = d;
                break;
            }
            if (groupIdx < 0) {
                // New group.
                groupIdx = s_dedupGatewayCount++;
                s_dedupGateways[groupIdx].destFieldId = destId;
                s_dedupGateways[groupIdx].centerX = 0;
                s_dedupGateways[groupIdx].centerY = 0;
                s_dedupGateways[groupIdx].count = 0;
                // Resolve display name. Static INF destinations may be placeholders
                // (overwritten at runtime by MAPJUMPO), so show generic "Exit" if
                // the destination doesn't look like it belongs to this field's area.
                // v0.07.95: World map fields (IDs 0-71) all say "World Map" instead of "wm00" etc.
                const char* dispName = GetFieldDisplayName(destId);
                if (destId <= 71) {
                    strncpy(s_dedupGateways[groupIdx].displayName, "Exit to World Map", 47);
                } else if (dispName) {
                    snprintf(s_dedupGateways[groupIdx].displayName, 48, "Exit to %s", dispName);
                } else {
                    strncpy(s_dedupGateways[groupIdx].displayName, "Exit", 47);
                }
                s_dedupGateways[groupIdx].displayName[47] = '\0';
            }
            // Accumulate center (will average after all gateways processed).
            s_dedupGateways[groupIdx].centerX += s_gateways[g].centerX;
            s_dedupGateways[groupIdx].centerY += s_gateways[g].centerZ;  // centerZ = Y in our coords
            s_dedupGateways[groupIdx].count++;
        }
        // Average the centers.
        for (int d = 0; d < s_dedupGatewayCount; d++) {
            if (s_dedupGateways[d].count > 0) {
                s_dedupGateways[d].centerX /= (float)s_dedupGateways[d].count;
                s_dedupGateways[d].centerY /= (float)s_dedupGateways[d].count;
            }
        }

        // v0.18.3.301 (#91 R3): name the two ring crossings.
        //
        // Once R3 splits them, the shaft halves carry TWO exits with the same
        // destination and therefore the same display name -- and the exact-name
        // dedupe further down (v0.18.3.270) would drop the second one. They
        // need distinct names to survive, and "Exit to Galbadia D-District
        // Prison 5" twice would in any case tell the player nothing about
        // which way round the hole they are being sent.
        //
        // Both crossings land on the same floor (10 crossings, 0 floor changes
        // in the .299 BAT), so a floor label would be actively wrong here --
        // the floor belongs on the stairs, not on these. What distinguishes
        // them is position on the ring, which is exactly how Aaron describes
        // the room: "you can go from district 3 to district 5 across the top of
        // the circle or across the bottom of the circle." The destination is
        // dropped because both go to the same place; the only useful fact is
        // WHICH WAY ROUND.
        //
        // Derived from the gateway's own Y, not from the camera, so the label
        // is stable wherever the player is standing and on either half.
        //
        // ASSUMPTION, flagged for the BAT: larger Y = "top". This codebase's
        // convention is "X = screen-horizontal, Y = screen-vertical" but the
        // SIGN is not documented anywhere and I refuse to pretend I verified
        // it (the .298 gateway-Z hypothesis died of exactly this). Both centres
        // are logged below; if the two read swapped in play it is a one-line
        // flip and costs nothing but the rename.
        {
            uint16_t ringFid = FF8Addresses::pCurrentFieldId
                               ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
            bool onRingHalf = (ringFid == 0x031A || ringFid == 0x031B ||
                               ringFid == 0x031C || ringFid == 0x031D);
            if (onRingHalf) {
                for (int a = 0; a < s_dedupGatewayCount; a++) {
                    for (int b = a + 1; b < s_dedupGatewayCount; b++) {
                        if (s_dedupGateways[a].destFieldId !=
                            s_dedupGateways[b].destFieldId) continue;
                        int topIdx = (s_dedupGateways[a].centerY >=
                                      s_dedupGateways[b].centerY) ? a : b;
                        int botIdx = (topIdx == a) ? b : a;
                        strncpy(s_dedupGateways[topIdx].displayName, "Top crossing", 47);
                        s_dedupGateways[topIdx].displayName[47] = '\0';
                        strncpy(s_dedupGateways[botIdx].displayName, "Bottom crossing", 47);
                        s_dedupGateways[botIdx].displayName[47] = '\0';
                        Log::Field("FieldNavigation: [refresh] ring crossings named: "
                                   "group %d (%.0f,%.0f) = 'Top crossing', "
                                   "group %d (%.0f,%.0f) = 'Bottom crossing' "
                                   "[v0.18.3.301 #91 R3 -- larger Y assumed to be top]",
                                   topIdx, s_dedupGateways[topIdx].centerX,
                                   s_dedupGateways[topIdx].centerY,
                                   botIdx, s_dedupGateways[botIdx].centerX,
                                   s_dedupGateways[botIdx].centerY);
                    }
                }
            }
        }
        // v0.18.3.279: does this field resolve its exits through script trigger
        // lines (SETLINE + MAPJUMP) rather than through static INF gateways?
        //
        // When it does, the INF gateway table is not the field's live exit list --
        // it is static map data that can describe exits belonging to a DIFFERENT
        // story state of the same room. glfurin1 (Caraway's Mansion 1) is the case
        // that exposed this: its script resolves two real line exits (725 Mansion 5,
        // 724 Mansion 4) while INF also carries gw[0] line=(-862,-360)->(-862,-497)
        // destId=726 "Mansion 6" -- a doorway that only opens on a later visit.
        // The mod catalogued it, the player walked to it, and nothing happened,
        // because the engine's own gateway check never fires in this story state.
        //
        // The destination-name dedupe below cannot catch it: 726 matches neither
        // 725 nor 724, so it is not a duplicate of anything -- it is a ghost.
        //
        // Evidence this is safe rather than a blunt instrument (2026-07-18 log,
        // full Deling + B-Garden sweep): every other field carrying INF gateways
        // resolves ZERO line exits, so the rule never fires there --
        //   glpreo1  1 gw, 7 lines all dest=-1      glpreo2  3 gw, 4 lines all dest=-1
        //   glprefr2 1 gw, 0 lines                  glstage1 3 gw, 0 lines
        //   glpreo3  8 gw, 0 lines
        // and glwitch1, the one field with both, has gateway destId=746 EQUAL to
        // its line exit dest=746, so the gateway is kept by the match below. Only
        // glfurin1 has a gateway whose destination no line agrees with.
        bool fieldHasLineExits = false;
        for (int lc = 0; lc < s_capturedLineCount; lc++) {
            if (!s_capturedLines[lc].active) continue;
            if (s_capturedLines[lc].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                s_capturedLines[lc].destFieldId > 0) {
                fieldHasLineExits = true;
                break;
            }
        }

        // v0.18.3.300 (#91 R2): SCOPE THE RULE ABOVE TO ITS EVIDENCE BASE.
        //
        // The 2026-07-31 BAT caught this rule deleting a REAL exit and leaving
        // Aaron with no listed way off the screen. On gpbig2a (D-District
        // Prison, Prison 5) the catalog held exactly two entries -- a cell door
        // and the Directory -- on all five visits:
        //
        //   [LINE-PAIR] jsmDoors=1 jsmLines=1 jsmEntities=21 captured=1
        //     line0 center=(1459,2509) type=6 param=965 | lineType=11 dest=965
        //   [refresh] INF-GW group 0 'Exit to Prison 3' (destId=795) SUPPRESSED:
        //     field resolves exits via trigger lines and no live line targets
        //     this destination -- stale INF data for another story state
        //   [refresh] catalog: 2 entries (2 navigable, 0 new entities)
        //
        // The premise is inverted here. gpbig2a has ONE screen-bound line and it
        // points at a CELL (965). That single unrelated line arms
        // fieldHasLineExits, which then deletes the only real exit off the
        // screen -- the walkway back to gpbig1a, which Aaron then walked through
        // anyway, 30 seconds later, proving it live. "The field resolves exits
        // via trigger lines" is true only in the most literal and least useful
        // sense: the shaft screens use BOTH mechanisms, for DIFFERENT exits.
        //
        // The fix is to scope the heuristic back to the evidence it was actually
        // established on, exactly as .291 rescoped the addr-as-literal exit
        // heuristic to bg* fields after it fabricated a Centra Ruins exit.
        // Re-reading the evidence block above: the rule was derived from, and
        // has only ever been needed by, ONE field -- glfurin1 -- and glfurin1
        // carries a SINGLE gateway. Every other field listed there resolves zero
        // line exits (glpreo1/2/3, glprefr2, glstage1), so fieldHasLineExits is
        // false and the rule never fires on them at all; glwitch1 has both but
        // its gateway destination EQUALS its line destination, so it is kept by
        // the agreement test whether or not this gate exists. The change is
        // therefore provably inert on every field in the documented evidence
        // base, preserves glfurin1, and fixes both prison shaft screens
        // (INF parsed: 2 active gateways on gpbig1a AND gpbig2a).
        //
        // Note this MUST test s_gatewayCount (raw INF entries), not
        // s_dedupGatewayCount: the prison's two gateways share a destination and
        // merge into ONE group, so the dedup count is 1 there and would gate
        // nothing.
        //
        // DIRECTION OF RISK: this is a pure narrowing. It can only ever ADD
        // exits back to the catalog, never remove one. The worst case is a ghost
        // gateway reappearing on some multi-gateway field we have not visited --
        // an exit that does nothing when you walk to it. That is annoying.
        // Losing a real exit is this project's worst failure mode (#88,
        // ladline7 hidden for three BAT cycles, and now gpbig2a). Erring toward
        // showing too much is the correct side to be wrong on.
        // v0.20.2: stale-gateway rule RETIRED (RE-driven). FF8_EN.exe RE + a live
        // BAT (v0.20.1 [EXIT-DIAG]) proved the engine has no "present-but-stale
        // gateway" state: every loaded INF gateway is a real, crossable exit, and
        // disable/reroute is handled on trigger LINES instead (enable byte
        // entity+0x194 via LINEON/LINEOFF; destination resolved at runtime by
        // MAPJUMP). The rule deleted REAL exits -- confirmed bidirectionally on
        // bghall_2<->bghall_5 (B-Garden 4<->10): each reaches its OTHER neighbours
        // by script lines and the B4<->B10 doorway ONLY by its lone INF gateway, so
        // (fieldHasLineExits && gatewayCount<=1) fired and dropped it
        // (WOULD_SUPPRESS=1 for dest=174 and dest=168). Forcing false surfaces every
        // loaded gateway; proven additive on the 32-fixture catalog harness. Worst
        // case is glfurin1 showing one harmless ghost gateway -- the correct side to
        // err on (per the note below, losing a real exit is the worst failure mode).
        bool staleGatewayRuleActive = false;
        if (fieldHasLineExits && !staleGatewayRuleActive) {
            Log::Field("FieldNavigation: [refresh] stale-gateway rule NOT applied: "
                       "%d raw INF gateways (> 1) -- rule is scoped to its single-gateway "
                       "evidence base (glfurin1); this field uses lines AND gateways for "
                       "different exits [v0.18.3.300 #91]",
                       s_gatewayCount);
        }

        // Add to catalog.
        if (s_dedupGatewayCount > 0 && s_playerEntityIdx >= 0) {
            float gwPlayerX = 0, gwPlayerY = 0;
            bool gotPlayer = GetEntityPos(s_playerEntityIdx, gwPlayerX, gwPlayerY);
            for (int d = 0; d < s_dedupGatewayCount && newCount < MAX_CATALOG; d++) {
                // Stale-gateway filter (see fieldHasLineExits above): on a
                // script-exit field, keep a gateway only if some live trigger line
                // agrees on its destination.
                // v0.18.3.300 (#91 R2): gated on staleGatewayRuleActive so the rule
                // only fires on the single-gateway shape it was validated against.
                if (staleGatewayRuleActive) {
                    bool lineAgrees = false;
                    for (int lc = 0; lc < s_capturedLineCount && !lineAgrees; lc++) {
                        if (!s_capturedLines[lc].active) continue;
                        if (s_capturedLines[lc].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                            s_capturedLines[lc].destFieldId == (int)s_dedupGateways[d].destFieldId)
                            lineAgrees = true;
                    }
                    if (!lineAgrees) {
                        Log::Field("FieldNavigation: [refresh] INF-GW group %d '%s' (destId=%u) "
                                   "SUPPRESSED: field resolves exits via trigger lines and no live "
                                   "line targets this destination -- stale INF data for another story state",
                                   d, s_dedupGateways[d].displayName,
                                   (unsigned)s_dedupGateways[d].destFieldId);
                        continue;
                    }
                }
                // Screen filter: skip the gateway only if the player->gateway
                // SEGMENT actually crosses a screen-boundary line SEGMENT.
                // v0.17.8.10: replaced IsSeparatedByTriggerLine() here -- that
                // does an INFINITE-line side test, so a short SCREEN_BOUND line
                // on a far edge (bghall_5's Hall 6 doorway, x in [4206,5042])
                // wrongly "separated" the Hall 4 INF gateway on the opposite
                // (west) edge because the gateway's Y lay almost on that line's
                // infinite extension. A gateway is a real exit you walk to; it
                // is on another screen only if the path to it actually crosses a
                // boundary segment. Entity screen-filtering still uses the
                // infinite-line helper; only the gateway test changed.
                if (gotPlayer && s_capturedLineCount > 0) {
                    bool crossed = false;
                    for (int dt = 0; dt < s_capturedLineCount && !crossed; dt++) {
                        if (!s_capturedLines[dt].active) continue;
                        if (s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                            s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_UNKNOWN)
                            continue;
                        if (SegmentsCross(gwPlayerX, gwPlayerY,
                                          s_dedupGateways[d].centerX, s_dedupGateways[d].centerY,
                                          (float)s_capturedLines[dt].x1, (float)s_capturedLines[dt].y1,
                                          (float)s_capturedLines[dt].x2, (float)s_capturedLines[dt].y2)) {
                            crossed = true;
                            Log::Field("FieldNavigation: [refresh] INF-GW group %d '%s' "
                                       "center=(%.0f,%.0f) filtered: path crosses screen-bound line%d",
                                       d, s_dedupGateways[d].displayName,
                                       s_dedupGateways[d].centerX, s_dedupGateways[d].centerY, dt);
                        }
                    }
                    if (crossed) continue;
                }
                // Dedup against JSM exits already in catalog with same destination.
                //
                // v0.18.3.271: removed a substring test that read
                //   strstr(newCatalog[c].name, s_gateways[0].destFieldName)
                // It was broken two ways: it always consulted gateway **0**
                // rather than the gateway being tested (d), and it matched a
                // SUBSTRING rather than the whole destination.
                //
                // It was inert until v0.18.3.270 only because destFieldName held
                // an internal field name ('bghall_1'), which never appears inside
                // a catalog entry name ("Exit to B-Garden - Hall 1") -- so the
                // clause never fired and the exact-name comparison below did all
                // the real work. Fixing GetFieldNameById() to return the display
                // name made the substring suddenly match, so a single gateway-0
                // name suppressed unrelated exits field-wide: B-Garden Hall 1
                // dropped from four exits to one, and the Quad->Hall exit vanished.
                //
                // The exact displayName comparison is the correct dedupe and is
                // exactly what was effectively running before .270.
                bool dupExit = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].type != ENT_EXIT) continue;
                    if (strcmp(newCatalog[c].name, s_dedupGateways[d].displayName) == 0) {
                        dupExit = true; break;
                    }
                }
                if (dupExit) continue;
                // v0.20.13: Mansion 6's INF gateway stays armed even after its
                // door locks at the mission/trap phase -- drop the phantom then.
                if (s_dedupGateways[d].destFieldId == 726 && CarawayMansionSealed()) {
                    Log::Field("FieldNavigation: [refresh] INF-GW group %d '%s' SUPPRESSED: "
                               "Caraway's Mansion sealed (story var 0x0F2==0x16) -- Mansion 6 "
                               "door locked though gateway armed [v0.20.13]",
                               d, s_dedupGateways[d].displayName);
                    continue;
                }
                EntityInfo gwExit = {};
                gwExit.entityIdx  = -400 - d;  // sentinel for INF gateway exits
                gwExit.modelId    = -1;
                gwExit.triangleId = 0;
                gwExit.type       = ENT_EXIT;
                gwExit.gatewayIdx = d;  // index into s_dedupGateways
                strncpy(gwExit.name, s_dedupGateways[d].displayName, sizeof(gwExit.name) - 1);
                gwExit.name[sizeof(gwExit.name) - 1] = '\0';
                { char caBuf[80]; snprintf(caBuf, sizeof caBuf, "destField=%d gwMerged=%d", (int)s_dedupGateways[d].destFieldId, (int)s_dedupGateways[d].count);
                CatAudit("gateway", gwExit, (int)s_dedupGateways[d].centerX, (int)s_dedupGateways[d].centerY, "-", caBuf); LiveGatePos("gateway", gwExit, (int)s_dedupGateways[d].centerX, (int)s_dedupGateways[d].centerY, false); }
                // v0.20.22: a gateway is an EXIT -- never zone-filtered (exits are navigation aids; never hide one).
                newCatalog[newCount++] = gwExit;
                Log::Field("FieldNavigation: [refresh] INF-GW group %d: '%s' center=(%.0f,%.0f) %d gateways merged",
                           d, s_dedupGateways[d].displayName,
                           s_dedupGateways[d].centerX, s_dedupGateways[d].centerY,
                           s_dedupGateways[d].count);
            }
        }
}

// ============================================================================
// 3g. Object/line dedupe + raw-SYM relabel  (was field_nav_catalog_dedupe.inl)
// ============================================================================
// ============================================================================
// v0.20.53 (WS1 Step 1.4) -- co-located dedup, OBSERVE-ONLY groundwork.
// Uniform position for ANY catalog entry (each source path stores position
// differently) + an interactability rank, so the [CO-LOCATED] pass at the end of
// DedupeCatalog can log the duplicate clusters the existing pairwise dedup misses
// (the plan's target: one thing surfaced as line + object + controller at a spot).
static bool CatalogEntryPos(const EntityInfo& e, float& x, float& y)
{
    if (e.gatewayIdx >= 0 && e.gatewayIdx < s_dedupGatewayCount) {   // INF gateway (by gatewayIdx; -300/-400 sentinels collide)
        x = (float)s_dedupGateways[e.gatewayIdx].centerX;
        y = (float)s_dedupGateways[e.gatewayIdx].centerY;
        return true;
    }
    int idx = e.entityIdx;
    if (idx >= 0) return GetEntityPos(idx, x, y);                    // runtime "others"
    if (idx <= -200 && idx > -300) {                                // trigger line (-200-t)
        int t = -(idx + 200);
        if (t >= 0 && t < s_capturedLineCount) {
            x = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
            y = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
            return true;
        }
    }
    if (idx <= -300) {                                              // JSM-injected (-300-j; gatewayIdx == -1 here)
        int j = -(idx + 300);
        if (j >= 0 && j < s_jsmEntityCount) {
            x = (float)s_jsmEntities[j].posX;
            y = (float)s_jsmEntities[j].posY;
            return true;
        }
    }
    return false;
}
// higher rank = more interactable = the representation to KEEP when a cluster collapses.
static int CoLoRank(const EntityInfo& e)
{
    switch (e.type) {
        case ENT_SAVE_POINT: case ENT_DRAW_POINT: case ENT_SHOP: case ENT_CARD_GAME: return 5;
        case ENT_EXIT:        return 4;
        case ENT_OBJECT:      return 3;
        case ENT_INTERACTION: return 2;
        case ENT_NPC:         return 2;
        default:              return 1;
    }
}

// v0.59.0: a pickup that is really a curated non-item (the glwater3 ladder) keeps
// its curated name. Deliberately a plain function, not a lambda inside
// DedupeCatalog: that function carries __try, and MSVC's C2712 forbids __try in
// a function that needs object unwinding. A captureless lambda is very likely
// fine, but "very likely" costs a build cycle to find out and Aaron is the only
// person who can run one.
static const char* CuratedNonItemName(const char* sym)
{
    if (!sym || !sym[0]) return nullptr;
    for (const EntityDisplayName* mN = ENTITY_DISPLAY_NAMES; mN->sym != nullptr; mN++)
        if (_stricmp(sym, mN->sym) == 0)
            return (mN->display && strncmp(mN->display, "Item", 4) != 0) ? mN->display : nullptr;
    return nullptr;
}

static void DedupeCatalog(EntityInfo* newCatalog, int& newCount)
{
// field_nav_catalog_dedupe.inl — v0.17.8.8 object/line dedupe + raw-SYM relabel.
// Extracted from RefreshCatalog() in field_nav_catalog.inl (v0.17.8.9) to keep
// that file under the size ceiling. This is NOT a standalone function: it is a
// statement fragment #included inline at ONE point inside RefreshCatalog's
// __try block, so it sees the local newCatalog[]/newCount and the file-scope
// catalog state (s_jsmEntities, s_jsmEntityCount, s_capturedLines,
// s_capturedLineCount, EntityTypeName, ENT_*, MAX_CATALOG, _stricmp, Log).
// Behavior is byte-identical to the previous inline block. Do not compile
// independently; do not include anywhere except that single call site.

// v0.17.8.8: General duplicate filter for object/line overlaps.
// A JSM-injected object (sentinel <= -300) coincident with an
// interactive trigger LINE (sentinel -200..-299, type ENT_INTERACTION)
// is the same physical interactable surfaced twice (entity + its walk-on
// line). Reported on bghall_2: the 'kanban' sign showed as both
// "Interaction 2" and "Kanban1". Rule: keep the more informative entry.
// A named object -- a special type (Save/Draw/Shop/Card) or a friendly
// name like "Directory" -- beats the generic line (drop the line); a
// raw-SYM object like "Kanban1" loses to "Interaction N" (drop the
// object). Exits are untouched (they dedupe by destination elsewhere).
{
    const float DUP_DIST = 128.0f;
    bool dupRemoved[MAX_CATALOG] = {};
    for (int a = 0; a < newCount; a++) {
        if (dupRemoved[a]) continue;
        if (newCatalog[a].entityIdx > -300) continue;   // A must be JSM-injected
        if (newCatalog[a].type == ENT_EXIT) continue;   // never dedupe exits here
        int ja = -(newCatalog[a].entityIdx + 300);
        if (ja < 0 || ja >= s_jsmEntityCount) continue;
        float ax = (float)s_jsmEntities[ja].posX;
        float ay = (float)s_jsmEntities[ja].posY;
        for (int b = 0; b < newCount; b++) {
            if (b == a || dupRemoved[b]) continue;
            if (newCatalog[b].type != ENT_INTERACTION) continue;
            if (newCatalog[b].entityIdx > -200 || newCatalog[b].entityIdx <= -300)
                continue;                                // B must be a trigger line
            int tb = -(newCatalog[b].entityIdx + 200);
            if (tb < 0 || tb >= s_capturedLineCount) continue;
            float bx = (float)(s_capturedLines[tb].x1 + s_capturedLines[tb].x2) / 2.0f;
            float by = (float)(s_capturedLines[tb].y1 + s_capturedLines[tb].y2) / 2.0f;
            float ddx = ax - bx, ddy = ay - by;
            if (ddx*ddx + ddy*ddy > DUP_DIST*DUP_DIST) continue;
            // Same spot. Is the object's name just its raw SYM?
            bool objIsRawSym = false;
            if (newCatalog[a].type == ENT_OBJECT) {
                const char* sym = s_jsmEntities[ja].symName;
                objIsRawSym = (sym[0] != '\0' &&
                               _stricmp(newCatalog[a].name, sym) == 0);
            }
            if (objIsRawSym) {
                dupRemoved[a] = true;   // keep the line ("Interaction N")
                Log::Field("FieldNavigation: [dedup] dropped JSM '%s' (%.0f,%.0f): "
                           "duplicate of Interaction line%d at (%.0f,%.0f) [v0.17.8.8]",
                           newCatalog[a].name, ax, ay, tb, bx, by);
                break;                  // A is gone; stop scanning lines for it
            } else {
                dupRemoved[b] = true;   // keep the named object
                Log::Field("FieldNavigation: [dedup] dropped Interaction line%d (%.0f,%.0f): "
                           "duplicate of JSM '%s' (%s) at (%.0f,%.0f) [v0.17.8.8]",
                           tb, bx, by, newCatalog[a].name,
                           EntityTypeName(newCatalog[a].type), ax, ay);
            }
        }
    }
    int dw = 0;
    for (int r = 0; r < newCount; r++) {
        if (!dupRemoved[r]) {
            if (dw != r) newCatalog[dw] = newCatalog[r];
            dw++;
        }
    }
    newCount = dw;

    // v0.17.8.8: Relabel any surviving raw-SYM interactive object as a
    // generic "Interaction N" or "NPC N". A standalone JSM object whose only
    // name is its internal SYM (e.g. bghall_3 'Kanban2') must not expose that
    // symbol to the player -- SYM names are unreliable internal identifiers
    // that don't always reflect what an entity actually is (kanban2 looks
    // like a signpost name but the entity IS Xu, a character, on bghall_3).
    // Friendly-named objects (Directory) and named specials (Save/Draw/Shop/
    // Card) are not ENT_OBJECT-with-raw-SYM, so they keep their labels.
    // Runs AFTER dedupe so the objIsRawSym test above still sees the raw
    // name (preserving the Hall 4 dedupe). Only positioned objects reach the
    // catalog, so every relabel stays navigable. Numbering for each label
    // type continues from existing entries of that type.
    //
    // v0.17.8.15: NPC vs Interaction discriminator. Replaces the v0.17.8.11
    // chara.one cross-reference, which was reverted after the bghall_3
    // screenshot proved kanban2 IS Xu standing in the world (not a signpost),
    // and the chara.one classifier had misclassified her model p048 as a
    // prop. The classifier was the wrong mechanism entirely -- what matters
    // for the player is whether the entity stands in the world and is talked
    // to (NPC) vs. is a walk-across line trigger (Interaction). The behavior
    // signal:
    //   jsmCategory == 3 (Other) AND hasSetmodelInit  -> "NPC N"
    //   everything else (Background, no SETMODEL, etc.) -> "Interaction N"
    //
    // The signal is grounded in observable game behavior, not file-level
    // classification: an Other-category entity that loads a 3D model in init
    // is by construction "someone standing somewhere" -- whether the model
    // file is conventionally a 'd'-prefix character or a 'p'-prefix prop
    // doesn't matter, because both are used for characters across the game.
    // Validated against bghall_3:
    //   line3 (cat 1, Line)         -> Interaction 1  (signpost, walk-across)
    //   line4 (cat 1, Line)         -> Interaction 2  (signpost, walk-across)
    //   ent25 kanban2 (cat 3, SETMODEL=1) -> NPC 1   (Xu, walk-up + Confirm)
    //
    // Per Aaron's directive, NPC labels are pure "NPC N" -- no SYM-derived
    // names ever exposed to the player. The nav-cycle code adds the
    // " X of Y" suffix at announce time based on how many NPCs are on the
    // current field.
    for (int a = 0; a < newCount; a++) {
        if (newCatalog[a].entityIdx > -300) continue;   // JSM-injected only
        if (newCatalog[a].type != ENT_OBJECT) continue;
        int ja = -(newCatalog[a].entityIdx + 300);
        if (ja < 0 || ja >= s_jsmEntityCount) continue;
        const char* sym = s_jsmEntities[ja].symName;
        if (sym[0] == '\0' || _stricmp(newCatalog[a].name, sym) != 0) continue;

        // v0.17.8.15: NPC discriminator. Other-category entity (cat 3) with
        // a SETMODEL in its init method is by definition a positioned, model-
        // bearing entity the player walks up to and Confirms -- i.e. an NPC.
        bool isNpcCandidate = (s_jsmEntities[ja].jsmCategory == 3 &&
                               s_jsmEntities[ja].hasSetmodelInit);

        if (isNpcCandidate) {
            // v0.17.8.15.1: Count entries already named "NPC N" (the generic
            // relabel sequence), NOT all ENT_NPC entries. Friendly-named NPCs
            // (Cid, Quistis, etc.) are also typed ENT_NPC but are announced
            // by their actual name -- counting them inflated kanban2's number
            // to "NPC 2" on bghall_3 even though it was the first/only raw-SYM
            // NPC relabel (Aaron never heard an "NPC 1" because the catalog's
            // other ENT_NPC announced as e.g. "Cid"). Match the "NPC %d"
            // prefix only.
            int n = 0;
            for (int c = 0; c < newCount; c++) {
                const char* nm = newCatalog[c].name;
                if (strncmp(nm, "NPC ", 4) == 0 && nm[4] >= '0' && nm[4] <= '9')
                    n++;
            }
            n++;
            snprintf(newCatalog[a].name, sizeof(newCatalog[a].name), "NPC %d", n);
            newCatalog[a].type = ENT_NPC;
            Log::Field("FieldNavigation: [dedup] relabeled raw-SYM object '%s' -> "
                       "NPC %d (Other + SETMODEL-init) [v0.17.8.15.1]", sym, n);
            continue;
        }

        // Fall-through: not an NPC by the behavior signal. Generic
        // "Interaction N" -- background script object, Other with no model,
        // etc. Numbering continues from existing ENT_INTERACTION entries.
        int n = 0;
        for (int c = 0; c < newCount; c++)
            if (newCatalog[c].type == ENT_INTERACTION) n++;
        n++;
        snprintf(newCatalog[a].name, sizeof(newCatalog[a].name), "Interaction %d", n);
        newCatalog[a].type = ENT_INTERACTION;
        Log::Field("FieldNavigation: [dedup] relabeled raw-SYM object '%s' -> "
                   "Interaction %d [v0.17.8.8]", sym, n);
    }

    // v0.19.5 [item-pickup]: relabel collectible pickups from "NPC N" to "Item N".
    // A catalogued generic-"NPC" MODEL entity whose walkmesh triangle matches a
    // JSM item-pickup entity's SET3 triangle is a collectible (Weapons Monthly,
    // Timber Maniacs, ...), not a character. The triangle is the link because the
    // runtime SYM is shifted (runtime 'Zell2' != JSM 'Urakata'), so name/index
    // matching is unreliable -- but the live triangle equals the JSM SET3 triangle
    // (BAT-confirmed: Urakata tri27, saveline0 tri36). The pickup signal itself is
    // exe-grounded (JSMEntityInfo.isItemPickup: a no-dialog model whose interaction
    // method writes its own savemap collected-flag and/or pushes ADDITEM 0x125).
    // PURE RELABEL -- nothing is dropped; a non-match simply leaves the entry as
    // "NPC" (current behavior), so this cannot regress the v0.19.0 over-drop.
    for (int a = 0; a < newCount; a++) {
        if (newCatalog[a].entityIdx < 0) continue;                 // runtime entries only
        if (newCatalog[a].type != ENT_NPC) continue;
        if (strncmp(newCatalog[a].name, "NPC", 3) != 0) continue;  // generic "NPC", not "Quistis" etc.
        if (newCatalog[a].triangleId == 0) continue;               // need a real triangle to match on
        bool isPickup = false;
        const char* curatedName = nullptr;   // v0.20.44 (ladder false-positive only)
        // v0.20.49 (Balamb Hotel BAT): ONLY an item-pickup JSM entity may claim a generic runtime "NPC"
        // here. The earlier code adopted the name of ANY JSM entity sharing the NPC's triangle and did so
        // BEFORE the item check, which caused BOTH bugs Aaron saw in the hotel: (1) a party member standing
        // on the save point (savePoint, tri 45) became a phantom SECOND "Save Point"; (2) the Timber
        // Maniacs magazine was suppressed -- tri 27 holds the curated 'Irvine' object AND the 'Buki1'
        // magazine (isItemPickup), 'Irvine' was matched first, so the runtime entity was relabeled 'Irvine'
        // and the item never surfaced. Gating on isItemPickup means a non-pickup entity (save/draw/shop/
        // card point, an Irvine object, a gate, ...) can no longer hijack the NPC; the curated-name
        // adoption is kept ONLY for a pickup that is itself a curated non-item -- the glwater3 'hasigomodel'
        // ladder false-positive, which must read "Ladder", never "Item".
        // v0.58.0: ask THIS entity's own script, not whatever script happens to
        // stand on the same triangle.
        //
        // The triangle scan below was written when the runtime->script join was
        // believed unreliable ("the runtime SYM is shifted"). It was -- but from a
        // fixable ordering bug, not from anything about the engine, and the
        // work-around had a failure mode of its own that Aaron has been hitting:
        // two entities on one triangle swap identities. A character standing where
        // a magazine lies gets announced as "Item", and the magazine, having been
        // claimed, disappears. Runtime Others slot i is JSM group
        // (nLines+nDoors+nBackgrounds+i) and nothing else, so ask that entity.
        // v0.62.0: entityIdx is a LIVE index, so the join is the model one.
        const FieldArchive::JSMEntityInfo* ownJsm =
            FindJSMByLiveEntity(newCatalog[a].entityIdx);
        if (ownJsm) {
            if (ownJsm->isItemPickup) {
                isPickup = true;
                curatedName = CuratedNonItemName(ownJsm->symName);
            }
            if (!isPickup)
                Log::Field("FieldNavigation: [dedup] ent%d tri=%d: own script "
                           "'%s' is not an item pickup -- left as NPC [v0.58.0]",
                           newCatalog[a].entityIdx, (int)newCatalog[a].triangleId,
                           ownJsm->symName);
        } else {
            // No slot recorded for this entity (no JSM for the field, or a scan
            // that failed): fall back to the pre-v0.58.0 triangle match.
            for (int j = 0; j < s_jsmEntityCount; j++) {
                if (s_jsmEntities[j].posTriangle != newCatalog[a].triangleId) continue;
                if (!s_jsmEntities[j].isItemPickup) continue;
                isPickup = true;
                curatedName = CuratedNonItemName(s_jsmEntities[j].symName);
                break;
            }
        }
        if (curatedName) {
            Log::Field("FieldNavigation: [dedup] curated-name relabel: ent%d tri=%d 'NPC'->'%s' "
                       "-- generic NPC on a curated entity, not an Item [v0.20.44]",
                       newCatalog[a].entityIdx, (int)newCatalog[a].triangleId, curatedName);
            snprintf(newCatalog[a].name, sizeof(newCatalog[a].name), "%s", curatedName);
            newCatalog[a].type = ENT_OBJECT;
            continue;
        }
        if (!isPickup) continue;
        // v0.20.38 (glwater3 BAT): an item pickup sitting on an UNREACHABLE walkmesh triangle
        // (across the sewer water / behind a closed gate) can't be collected right now, and a
        // sighted player can't reach it either -- drop it. It returns when the player's zone
        // grows to include its triangle. Items are must-WALK-to, so the walkmesh-reachability
        // test is exactly right (no talk-across-a-gap exception like a conversational NPC). The
        // runtime-entity path never applied the zone filter -- which is why 'Item 1' (ent3,
        // tri 175) survived while the far-side ladder, filtered on the object path, did not.
        if (s_zoneValid && newCatalog[a].triangleId >= 0 &&
            newCatalog[a].triangleId < (int)s_walkmesh.numTriangles &&
            newCatalog[a].triangleId < ZONE_MAX_TRI &&
            !s_zoneReachable[newCatalog[a].triangleId]) {
            Log::Field("FieldNavigation: [dedup] item-pickup ent%d tri=%d DROPPED: unreachable "
                       "(not in player's zone) [v0.20.38]",
                       newCatalog[a].entityIdx, (int)newCatalog[a].triangleId);
            for (int m = a; m < newCount - 1; m++) newCatalog[m] = newCatalog[m + 1];
            newCount--; a--;
            continue;
        }
        int n = 0;
        for (int c = 0; c < newCount; c++)
            if (strncmp(newCatalog[c].name, "Item ", 5) == 0) n++;
        n++;
        Log::Field("FieldNavigation: [dedup] item-pickup relabel: ent%d tri=%d 'NPC'->'Item %d' "
                   "(JSM isItemPickup match) [v0.19.5]",
                   newCatalog[a].entityIdx, (int)newCatalog[a].triangleId, n);
        snprintf(newCatalog[a].name, sizeof(newCatalog[a].name), "Item %d", n);
        newCatalog[a].type = ENT_OBJECT;
    }
}

// v0.18.3.233: RUNTIME-ENTITY vs SPECIAL-TRIGGER dedupe.
//
// The v0.17.8.8 pass above only dedupes JSM-injected objects (sentinel <= -300)
// against trigger lines. It never considers RUNTIME entities (entityIdx >= 0),
// so a save-point OBJECT standing on its own save line was surfaced twice: once
// correctly as "Save Point" (the line) and once as a bogus generic "NPC" (the
// entity). Confirmed on bgryo1_4 (B-Garden dormitory), where the save point is
// entity slot 3 sitting at (-171,283) -- the exact centre of the save line:
//
//   ent3   model=11 tri=26 setpc=254  fp/4096 = (-171,283)   -> "NPC"
//   line2  TRIGGER  center=(-171,283)                        -> "Save Point"
//
// The entity cannot be recognised as a save point by name: the SYM->slot mapping
// is unrecoverable on fields that instantiate only a subset of their SYMs (this
// field instantiates 4 of 16), so ent3 resolves to the SYM 'zell'. It is not a
// party character (setpc=254), so the party filter correctly keeps it -- which is
// what exposed the duplicate. Model IDs are field-local, so they cannot identify
// it either.
//
// Position is the reliable signal: a generic entity sitting ON a specifically-
// typed trigger IS that trigger's object. Keep the informative entry (the named
// Save/Draw/Shop/Card line) and drop the generic duplicate. Only untyped ENT_NPC
// entries are eligible, so a genuinely named NPC is never removed, and the player
// is explicitly exempt (they may simply be standing on the save point).
{
    const float ENT_DUP_DIST = 96.0f;
    bool entDupRemoved[MAX_CATALOG] = {};
    for (int a = 0; a < newCount; a++) {
        int ai = newCatalog[a].entityIdx;
        if (ai < 0) continue;                        // runtime entities only
        if (ai == s_playerEntityIdx) continue;       // never drop the player
        if (newCatalog[a].type != ENT_NPC) continue; // only generic NPC entries
        float ax, ay;
        if (!GetEntityPos(ai, ax, ay)) continue;
        for (int b = 0; b < newCount; b++) {
            if (b == a) continue;
            int bi = newCatalog[b].entityIdx;
            if (bi >= 0) continue;                   // B must not be a runtime entity
            if (!(newCatalog[b].type == ENT_SAVE_POINT ||
                  newCatalog[b].type == ENT_DRAW_POINT ||
                  newCatalog[b].type == ENT_SHOP ||
                  newCatalog[b].type == ENT_CARD_GAME))
                continue;                            // ...with a specific meaning
            // v0.61.0: B used to have to be a captured TRIGGER LINE (-200..-299).
            // On the Lunar Base infirmary the save point is JSM-INJECTED, not a
            // line -- the log even shows the save LINE being folded into the JSM
            // save point moments earlier -- so by the time this pass ran there was
            // no line left to match against and the model standing on the save
            // point stayed in the list as a third "NPC". Aaron, on the room his
            // save loads into: "it said there were 3 NPCs, but there were really
            // just two." Position now comes from CatalogEntryPos, which resolves
            // every source the same way, so a special is a special wherever the
            // catalog got it from.
            float bx = 0, by = 0;
            if (!CatalogEntryPos(newCatalog[b], bx, by)) continue;
            float ddx = ax - bx, ddy = ay - by;
            if (ddx*ddx + ddy*ddy > ENT_DUP_DIST*ENT_DUP_DIST) continue;
            entDupRemoved[a] = true;
            Log::Field("FieldNavigation: [dedup] dropped entity ent%d ('%s') at (%.0f,%.0f): "
                       "same object as %s '%s' (idx %d) at (%.0f,%.0f) [v0.18.3.233 / v0.61.0]",
                       ai, newCatalog[a].name, ax, ay,
                       EntityTypeName(newCatalog[b].type), newCatalog[b].name, bi, bx, by);
            break;
        }
    }
    // v0.62.1 (#123): a live entity whose OWN script was injected as a JSM
    // special or exit IS that object, not a second thing standing beside it.
    // On sscont2 the lift platform `ele` was catalogued twice -- once from the
    // runtime scan as a plain "NPC" and once as "Exit to Lunar Base - Pod 1" --
    // and Aaron walked to the NPC: "There is also an NPC that I arrived at but
    // didn't seem to do anything." This is not a distance test: the model join
    // says outright which live entity runs which script, so the two entries are
    // provably one object and the one that says what it does survives.
    for (int a = 0; a < newCount; a++) {
        int ai = newCatalog[a].entityIdx;
        if (ai < 0 || ai == s_playerEntityIdx || entDupRemoved[a]) continue;
        for (int b = 0; b < newCount; b++) {
            if (b == a || entDupRemoved[b]) continue;
            if (newCatalog[b].gatewayIdx >= 0) continue;      // INF gateway, not a JSM entry
            int bi = newCatalog[b].entityIdx;
            if (bi > -300) continue;                          // JSM-injected sentinel only
            int bj = -300 - bi;
            if (bj < 0 || bj >= s_jsmEntityCount) continue;
            if (LiveIndexForJSM(s_jsmEntities[bj]) != ai) continue;
            // (v0.63.0 considered exempting an own-script talk exit here too --
            // the Ellone case -- and then deleted the exemption because no
            // mutation of it could be made to fail: the injection guard above
            // means such an exit never reaches the catalog for this rule to
            // match against. A branch nothing can distinguish is not a
            // safeguard. The rule that does the work is the one in
            // InjectMapExits.)
            entDupRemoved[a] = true;
            Log::Field("FieldNavigation: [dedup] dropped entity ent%d ('%s'): it IS the %s "
                       "'%s' already catalogued from its own script [v0.62.1]",
                       ai, newCatalog[a].name, EntityTypeName(newCatalog[b].type),
                       newCatalog[b].name);
            break;
        }
    }

    // v0.62.2 (#123): and the live entity running a script whose gate is shut is
    // that same shut thing. Only entities the classifier had nothing else to say
    // about ever get a REQ-follow exit, so this can only remove script objects.
    for (int a = 0; a < newCount; a++) {
        int ai = newCatalog[a].entityIdx;
        if (ai < 0 || ai == s_playerEntityIdx || entDupRemoved[a]) continue;
        const FieldArchive::JSMEntityInfo* jl = FindJSMByLiveEntity(ai);
        if (!jl) continue;
        int jj = (int)(jl - s_jsmEntities);
        if (jj < 0 || jj >= MAX_JSM_ENTITIES || !s_jsmGateClosed[jj]) continue;
        entDupRemoved[a] = true;
        Log::Field("FieldNavigation: [dedup] dropped entity ent%d ('%s'): its script '%s' "
                   "is the exit the story has not opened yet [v0.62.2]",
                   ai, newCatalog[a].name, jl->symName);
    }

    int ew = 0;
    for (int r = 0; r < newCount; r++) {
        if (!entDupRemoved[r]) {
            if (ew != r) newCatalog[ew] = newCatalog[r];
            ew++;
        }
    }
    newCount = ew;
}

    // v0.20.53 (WS1 Step 1.4) OBSERVE-ONLY: log co-located clusters that survived the pairwise dedup
    // above. Positions are resolved uniformly (CatalogEntryPos); any two kept entries within COLO_DIST
    // are a candidate duplicate the current dedup does NOT collapse (3-way line+object+controller,
    // runtime+object, object+object, ...). Logs the pair + which representation would be kept (higher
    // interactability rank). NOTHING is removed -- a BAT tells us which pairs are truly one thing vs
    // distinct-but-close before Step 1.4 does any collapsing.
    if (s_liveGate) {
        const float COLO_DIST = 160.0f;
        for (int a = 0; a < newCount; a++) {
            float ax = 0, ay = 0;
            if (!CatalogEntryPos(newCatalog[a], ax, ay)) continue;
            for (int b = a + 1; b < newCount; b++) {
                float bx = 0, by = 0;
                if (!CatalogEntryPos(newCatalog[b], bx, by)) continue;
                float dx = ax - bx, dy = ay - by;
                float d2 = dx*dx + dy*dy;
                if (d2 > COLO_DIST * COLO_DIST) continue;
                const EntityInfo& keep = (CoLoRank(newCatalog[a]) >= CoLoRank(newCatalog[b]))
                                       ? newCatalog[a] : newCatalog[b];
                Log::Field("FieldNavigation: [CO-LOCATED] field=%s d=%.0f A={'%s' %s} B={'%s' %s} -> keep '%s' [v0.20.53 WS1-1.4 observe]",
                           s_currentFieldName, sqrtf(d2),
                           newCatalog[a].name, EntityTypeName(newCatalog[a].type),
                           newCatalog[b].name, EntityTypeName(newCatalog[b].type),
                           keep.name);
            }
        }
    }
}

// ============================================================================
// 4. RefreshCatalog  (was field_nav_catalog.inl) — calls the above, same order
// ============================================================================
// field_nav_catalog.inl — Entity catalog building (RefreshCatalog).
// Included from field_navigation.cpp inside the FieldNavigation namespace.
// Do not compile independently.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.
// v0.17.7.0: Two large blocks moved to dedicated helper files for size
//            compliance (catalog.inl was 75.77 KB, 4 KB under the 80 KB
//            hard fail). Helpers live in:
//              field_nav_catalog_diag.inl
//                — DumpEntityDiagOnce, DumpBgDiagOnce,
//                  DumpPartyStateOnce, DumpCoordDiagOnce
//              field_nav_catalog_lateres.inl
//                — ResolveLatePositions, MatchSet3LateCaptures,
//                  ResolveStructPositions
//            Both included BEFORE this file in field_navigation.cpp so
//            their static functions are visible to RefreshCatalog.
//            Behavior byte-for-byte identical to v0.17.6.2 source except
//            the v0.12.17 VARBLOCK-POS unreachable `if (false)` block is
//            dropped. Git history at v0.17.6.2 preserves it.

// v0.18.3.228: IsPartyCharacterSym / PartyCharacterDisplayName moved to
// field_nav_helpers.inl (included earlier, so both stay visible here) to keep
// this file under the 80 KB source-size CI guard.

// ============================================================================
// v0.20.9 DIAGNOSTIC [PUZZLE-GATE]: on catalog refresh in Caraway's Mansion (glfurin*),
// snapshot the persistent story var-bank (0x01CFE9B8 +0x000..0x7FF) whenever it CHANGES
// since the last dump. Aaron cycles the exits before the glass/statue puzzle and after;
// diffing the two [PUZZLE-GATE] snapshots reveals the flag that turns the post-puzzle exit
// real, so the catalog can gate the "Exit" (party-member SETLINE) on it. Log-only, and
// self-gated to the mansion so it costs nothing elsewhere.
static void DumpPuzzleGateVars()
{
    if (strncmp(s_currentFieldName, "glfurin", 7) != 0) return;
    static uint8_t vb[0x800];
    bool ok = true;
    __try {
        const volatile uint8_t* pvb = (const volatile uint8_t*)(uintptr_t)0x01CFE9B8u;
        for (int i = 0; i < 0x800; i++) vb[i] = pvb[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) { Log::Field("FieldNavigation: [PUZZLE-GATE] '%s' varbank READ FAULT", s_currentFieldName); return; }
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 0x800; i++) { h ^= vb[i]; h *= 0x100000001b3ULL; }
    static uint64_t s_lastVbHash = 0;
    if (h == s_lastVbHash) return;
    s_lastVbHash = h;
    Log::Field("FieldNavigation: [PUZZLE-GATE] === '%s' varbank 0x01CFE9B8 +0x000..0x7FF (32 bytes/row) hash=%016llx ===",
               s_currentFieldName, (unsigned long long)h);
    for (int row = 0; row < 0x800; row += 32) {
        char line[80]; int lp = 0;
        for (int c = 0; c < 32; c++) lp += snprintf(line + lp, sizeof(line) - lp, "%02X", vb[row + c]);
        Log::Field("FieldNavigation: [PUZZLE-GATE] +%04X %s", row, line);
    }
}

// v0.20.10 DIAGNOSTIC [GW-LOCK]: read the LIVE in-memory INF gateway table (FDAT+0x64,
// 12 slots x 0x20) and log each slot's enable (+0x10) and fieldId (+0x12). The engine's
// crossing check skips a slot when enable==0xFFFF or fieldId==0x7FFF, so this reveals
// whether Caraway Mansion 1's "Exit to Mansion 6" gateway (dest 726) is LOCKED via the
// gateway-enable byte (then the catalog can mirror it) or via a story var (then we gate on
// that instead). FDAT-validated + SEH-guarded + self-gated to glfurin*, deduped on change.
static void DumpGatewayLock()
{
    if (strncmp(s_currentFieldName, "glfurin", 7) != 0) return;
    uint32_t fdat = 0;
    __try { fdat = *(const volatile uint32_t*)(uintptr_t)0x01CDC744u; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (fdat < 0x00400000u || fdat >= 0x40000000u) return;
    uint16_t en[12] = {}, fid[12] = {};
    bool ok = true;
    __try {
        const uint8_t* inf = (const uint8_t*)(uintptr_t)(fdat + 0x64u);
        for (int i = 0; i < 12; i++) {
            en[i]  = *(const uint16_t*)(inf + i * 0x20 + 0x10);
            fid[i] = *(const uint16_t*)(inf + i * 0x20 + 0x12);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) return;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 12; i++) { h ^= en[i]; h *= 0x100000001b3ULL; h ^= fid[i]; h *= 0x100000001b3ULL; }
    static uint64_t s_lastGwHash = 0;
    if (h == s_lastGwHash) return;
    s_lastGwHash = h;
    Log::Field("FieldNavigation: [GW-LOCK] '%s' FDAT=%08X live INF gateway slots (enable+0x10 / fieldId+0x12):",
               s_currentFieldName, fdat);
    for (int i = 0; i < 12; i++)
        Log::Field("FieldNavigation: [GW-LOCK]   slot%d enable=0x%04X fieldId=%u%s", i, en[i], fid[i],
                   (en[i] == 0xFFFF || fid[i] == 0x7FFF) ? " (DISABLED)" : "");
    for (int g = 0; g < s_gatewayCount; g++)
        Log::Field("FieldNavigation: [GW-LOCK]   static s_gateways[%d] dest=%u", g, (unsigned)s_gateways[g].destFieldId);
}

// v0.20.11 DIAGNOSTIC [M6-DOOR]: Caraway's Mansion 6 is a physical animated door in front of
// the (always-armed) INF gateway -- per Aaron you can walk up to it but it only opens when
// unlocked. The open/closed state is a story flag driven from the director cutscene scripts,
// NOT a gateway/DOORLINE lock. The full-playthrough [PUZZLE-GATE] snapshots could not isolate
// it: the M6-open (player left via 726) and M6-closed (player used Mansion 4/5) states were
// several story beats apart, so 88 varblock bytes differed. This logs JUST those 88 candidate
// offsets, compact + labeled, so a TIGHT before/after-trapped capture (mansion entry with M6
// still open, then again seconds later once the door has closed) narrows them to the single
// byte that flips = the door flag. Full [PUZZLE-GATE] dump stays active as the backstop.
// Log-only, glfurin*-gated, deduped on the candidate set. SEH-guarded (live varbank read).
static void DumpM6DoorCandidates()
{
    if (strncmp(s_currentFieldName, "glfurin", 7) != 0) return;
    static const uint16_t kCand[] = {
        0x006,0x050,0x051,0x054,0x079,0x0C2,0x0F2,0x66A,0x66B,0x66C,0x66E,0x66F,
        0x670,0x672,0x673,0x674,0x676,0x677,0x678,0x6CA,0x6CB,0x6CD,0x6CF,0x6D0,
        0x6D1,0x6D2,0x6D4,0x6D5,0x6D6,0x6D7,0x6D9,0x6DA,0x6DB,0x6DC,0x6DD,0x6DE,
        0x6DF,0x6E0,0x6E1,0x6E3,0x6E4,0x6E5,0x6E6,0x6E7,0x6E8,0x6E9,0x6EA,0x6EB,
        0x6EC,0x6ED,0x6EE,0x6EF,0x6F0,0x6F2,0x6F3,0x6F4,0x6F5,0x6F7,0x6F8,0x6F9,
        0x6FA,0x6FC,0x6FD,0x6FF,0x701,0x702,0x703,0x704,0x706,0x707,0x708,0x709,
        0x76A,0x76B,0x76C,0x76D,0x76F,0x770,0x771,0x772,0x7BA,0x7BB,0x7BC,0x7BD,
        0x7C0,0x7C1,0x7C4,0x7C5,
    };
    static const int N = (int)(sizeof(kCand) / sizeof(kCand[0]));
    uint8_t vals[128] = {};
    uint16_t prog = 0;
    bool ok = true;
    __try {
        const volatile uint8_t* pvb = (const volatile uint8_t*)(uintptr_t)0x01CFE9B8u;
        for (int i = 0; i < N; i++) vals[i] = pvb[kCand[i]];
        prog = *(const volatile uint16_t*)(uintptr_t)(0x01CFE9B8u + 0x100u);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) { Log::Field("FieldNavigation: [M6-DOOR] '%s' varbank READ FAULT", s_currentFieldName); return; }
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < N; i++) { h ^= vals[i]; h *= 0x100000001b3ULL; }
    // v0.20.11: per-FIELD dedup -- reset on field change so each mansion entry logs its state
    // even when two visits share candidate values (the initial global dedup suppressed
    // glfurin1's block as a duplicate of glfurin4's in the first BAT).
    static uint64_t s_lastM6Hash = 0;
    static char s_lastM6Field[64] = {};
    bool newField = (strncmp(s_currentFieldName, s_lastM6Field, 63) != 0);
    if (newField) { strncpy(s_lastM6Field, s_currentFieldName, 63); s_lastM6Field[63] = 0; }
    if (!newField && h == s_lastM6Hash) return;
    s_lastM6Hash = h;
    Log::Field("FieldNavigation: [M6-DOOR] '%s' progress[0x100]=%u  %d door-flag candidates (offset=value):",
               s_currentFieldName, (unsigned)prog, N);
    for (int i = 0; i < N; ) {
        char line[256]; int lp = 0;
        for (int c = 0; c < 22 && i < N; c++, i++)
            lp += snprintf(line + lp, sizeof(line) - lp, "%03X=%02X ", (unsigned)kCand[i], vals[i]);
        Log::Field("FieldNavigation: [M6-DOOR]   %s", line);
    }
}

static void RefreshCatalog()
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return;
    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        // v0.20.45 (#118): post-battle trigger-line preservation. s_capturedLines is captured via
        // the SETLINE hook on field entry and cleared on every field-scripts re-init -- but SETLINE
        // does NOT re-fire when the engine returns from a battle (the [LINE-PAIR] count came back 0
        // for glwater3/glwater2 after fights), so every trigger-line EXIT vanished for the rest of the
        // visit. Field geometry is stable per field, so keep the last non-empty capture tagged by the
        // ENGINE field id (pCurrentFieldId is authoritative across a battle -- the battle-pause resume
        // relies on it too), and restore it if a SAME-field refresh finds the table empty. A real field
        // change has a different id, so it re-captures normally; if SETLINE does re-fire it dedupes by
        // entity address (stable per field), so the restore never doubles lines. Runs before
        // ComputePlayerZoneReachability (4274) and the trigger-line exit injection, which both read
        // s_capturedLines.
        // v0.58.0: the backup now lives at file scope (field_nav_pathfinding.inl)
        // and is dropped by HookedFieldScriptsInit on a real field change, so it
        // can no longer resurrect a previous visit's lines.
        {
            uint16_t curField = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
            if (s_capturedLineCount > 0) {
                int nb = (s_capturedLineCount <= MAX_CAPTURED_LINES) ? s_capturedLineCount : MAX_CAPTURED_LINES;
                for (int i = 0; i < nb; i++) s_capBackup[i] = s_capturedLines[i];
                s_capBackupCount = nb;
                s_capBackupField = curField;
            } else if (s_capBackupCount > 0 && s_capBackupField == curField && curField != 0xFFFF) {
                for (int i = 0; i < s_capBackupCount; i++) s_capturedLines[i] = s_capBackup[i];
                s_capturedLineCount = s_capBackupCount;
                Log::Field("FieldNavigation: [refresh] restored %d trigger lines for field 0x%04X after an "
                           "empty re-init (post-battle SETLINE did not re-fire) [v0.20.45 #118]",
                           s_capBackupCount, (unsigned)curField);
            }
        }

        // v0.18.3.317 (#95/#98/#115): prison-shaft catalog floor-gating dry-run
        // (log-only; self-gates to shaft fields and fires once per floor change).
        // Gathers the per-floor varblock state needed to derive the Cycle-2 gate.
        ShaftCatalogDryRun();
        // v0.20.17: Caraway's Mansion investigation diagnostics DISABLED -- the escape-puzzle
        // signal (progress[0x100] > 376) is found and shipped (v0.20.12-.16), so the heavy
        // per-refresh [PUZZLE-GATE] (full 0x000-0x7FF varbank dump) and [M6-DOOR] (88 candidate
        // bytes) are just log noise now. Behind a compile-time flag (functions still referenced,
        // so no unused-function warning) -- flip to true to re-probe the mansion later.
        static const bool s_mansionInvestigationDiag = false;
        if (s_mansionInvestigationDiag) { DumpPuzzleGateVars(); DumpM6DoorCandidates(); }
        DumpGatewayLock();     // v0.20.10 DIAGNOSTIC [GW-LOCK] -- KEPT as a live-gateway backstop (self-gated to glfurin*)

        // Re-detect player.
        // v0.18.3.262 (#83 follow-up): the player controls the party LEADER, which
        // is formation[0] -- NOT necessarily Squall (setpc 0). The old setpc==0
        // rule mis-identified the player whenever Squall was not the leader (e.g.
        // Caraway's Mansion party led by Irvine, formation [2,0,3]): it pointed
        // s_playerEntityIdx at Squall's follower entity, so the real controlled
        // character (Irvine) was treated as a catalog NPC and player-relative
        // navigation used the wrong entity's position. Match the leader's setpc;
        // fall back to setpc==0, then entity 0, if the formation is unreadable.
        {
            // v0.18.3.263: leader = first slot of the FIELD controlled-party array
            // (0x01CFE990, the one the engine uses), NOT the savemap array. In the
            // split-party Caraway arc these differ, so the old read pointed at the
            // wrong team's leader. Fall back to setpc==0, then entity 0.
            uint8_t leaderChar = GetFieldPartyLeaderChar();
            int found = -1, foundSquall = -1;
            for (int i = 0; i < (int)lim; i++) {
                uint8_t setpc = *(base + ENTITY_STRIDE * i + 0x255);
                if (leaderChar != 0xFF && setpc == leaderChar) { found = i; break; }
                if (setpc == 0 && foundSquall < 0) foundSquall = i;
            }
            if (found < 0) found = (foundSquall >= 0) ? foundSquall : 0;
            s_playerEntityIdx = found;
        }

        // v0.18.3.264 (#83 follow-up): detect an ASSEMBLY scene.
        //
        // The field controlled-party roster (0x01CFE990) tells us who the "party
        // train" is, but NOT whether they are currently FOLLOWING (walking field)
        // or STANDING as interactable scene actors (gather scene). In Caraway's
        // Mansion the roster [Squall,Irvine,Rinoa] is placed at distinct scripted
        // positions and every member is talkable; filtering them by roster hid the
        // interactable ones. Their per-entity flags are identical to a follower's
        // (talk=0 push=0 thru=0), so nothing on the entity distinguishes the two
        // modes.
        //
        // What DOES distinguish them: a walking field instantiates ONLY the
        // controlled party; an assembly scene also places party characters that
        // are NOT in the controlled roster (the other team / extra members). So if
        // any placed party-character entity's setpc is NOT in the field roster, the
        // party is assembled/standing, and the roster members are talkable too.
        // (glfurin4: Zell/Quistis/Selphie are placed and not in [0,2,4] -> assembly.
        //  glfury1: only the roster [2,0,3] is present -> following, filter them.)
        bool sceneAssembly = false;
        for (int i = 0; i < (int)lim; i++) {
            uint8_t setpc = *(base + ENTITY_STRIDE * i + 0x255);
            if (!IsPartyCharacterSetpc(setpc)) continue;
            if (IsInFieldControlledParty(setpc)) continue;   // in the roster
            uint16_t tri = *(uint16_t*)(base + ENTITY_STRIDE * i + 0x1FA);
            int32_t  fx  = *(int32_t*)(base + ENTITY_STRIDE * i + 0x190);
            int32_t  fy  = *(int32_t*)(base + ENTITY_STRIDE * i + 0x194);
            if (tri > 0 || fx != 0 || fy != 0) { sceneAssembly = true; break; }
        }
        if (!s_scanTraced)
            Log::Field("FieldNavigation: [party-state] sceneAssembly=%d (placed non-roster party char present)",
                       sceneAssembly ? 1 : 0);

        // One-shot diagnostic dumps (extracted v0.17.7.0).
        // See field_nav_catalog_diag.inl. Each helper no-ops on subsequent calls.
        DumpEntityDiagOnce(base, lim);
        DumpExtendedEntityScanOnce(base, entCount);   // v0.18.3.231 DIAG
        DumpBgDiagOnce(lim);
        DumpPartyStateOnce();
        DumpPuzzleDiagOnce();      // v0.18.3.267: glass/statue puzzle objects
        DumpCoordDiagOnce(base, lim);

        // v0.63.0 (#111): the space rescue owns the screen while it runs. On
        // ssspace3 the only thing in the catalog is Rinoa -- catalogued as
        // "Exit to Outer Space 5", because her script carries the MAPJUMP the
        // win path takes -- and Aaron's 14:54:22 log has auto-drive setting off
        // toward it in the middle of the attempt he was flying. There is
        // nothing to navigate to in open space.
        if (SpaceRescueActive()) {
            if (!s_scanTraced)
                Log::Field("FieldNavigation: [refresh] catalog suppressed: the space "
                           "rescue is running [v0.63.0]");
            s_catalogCount = 0;
            s_selectedCatalogIdx = -1;
            return;
        }

        // v0.62.0 (#123): resolve every live entity to its script BEFORE the
        // scan reads a single name or type. See BuildLiveJsmMap in
        // field_nav_helpers.inl for why the live index is not the script slot.
        BuildLiveJsmMap(base, (int)lim);
        // v0.62.3 (#123): a trigger line whose own touch script is gated shut is
        // inert -- it is not an exit and it is not a wall. On sspod2 the `pod`
        // line's script opens `var[256] == 2556` and ends in MAPJUMPO 638, so
        // before that point in the story the catalog was offering "Exit to
        // Desert 1" AND letting the screen filter treat the line as a boundary,
        // which put Ellone on the far side of it and removed her. Aaron: "only
        // two of the three seemed to navigate correctly... There was also an
        // unexpected exit to 'desert 1'." Re-evaluated every refresh, because
        // the variable moves while you play.
        for (int t = 0; t < s_capturedLineCount; t++) {
            bool closed = false;
            for (int jg = 0; jg < s_jsmEntityCount; jg++) {
                if (s_jsmEntities[jg].jsmCategory != 1) continue;
                if (s_jsmEntities[jg].jsmIndex != t) continue;
                if (s_jsmEntities[jg].hasGate) {
                    int32_t live = 0;
                    closed = !JsmGateOpen(s_jsmEntities[jg], &live);
                    if (closed && !s_scanTraced)
                        Log::Field("FieldNavigation: [catalog] line%d '%s' inert: gated on "
                                   "var[%d]=%d op%d %d [v0.62.3]", t,
                                   s_jsmEntities[jg].symName, s_jsmEntities[jg].gateAddr,
                                   live, (int)s_jsmEntities[jg].gateOp,
                                   s_jsmEntities[jg].gateValue);
                }
                break;
            }
            s_capturedLines[t].gateClosed = closed;
        }
        if (!s_scanTraced) {
            for (int i = 0; i < (int)lim && i < MAX_ENTITIES; i++) {
                const FieldArchive::JSMEntityInfo* jl = FindJSMByLiveEntity(i);
                Log::Field("FieldNavigation: [LIVE-JOIN] ent%d model=%d -> %s%s [v0.62.0]",
                           i, (int)*(int16_t*)(base + ENTITY_STRIDE * i + 0x218),
                           jl ? "sym=" : "UNRESOLVED", jl ? jl->symName : "");
            }
        }

        // Build set of currently-qualifying entity indices.
        bool qualifies[MAX_ENTITIES] = {};
        EntityInfo fresh[MAX_ENTITIES] = {};
        for (int i = 0; i < (int)lim; i++) {
            uint8_t* block = base + ENTITY_STRIDE * i;
            int16_t  modelId      = *(int16_t*)(block + 0x218);
            uint16_t triId        = *(uint16_t*)(block + 0x1FA);
            uint8_t  setpc        = *(block + 0x255);
            uint8_t  talkonoff    = *(block + 0x24B);
            uint8_t  pushonoff    = *(block + 0x249);
            uint8_t  throughonoff = *(block + 0x24C);
            // v0.12.08 Fix D: Read position for placement validation.
            int32_t  fpX          = *(int32_t*)(block + 0x190);
            int32_t  fpY          = *(int32_t*)(block + 0x194);

            // v0.17.8.3: Resolve this entity's SYM name now (needed by the
            // party filter below). Same offset mapping used elsewhere in this
            // function for JSM lookups.
            // v0.62.0: the SYM STRINGS were never the problem -- the index was.
            // s_symNames[s_symOthersOffset + i] reads the i-th script slot's
            // name, and the live array is not indexed by slot. Take the name
            // off the entity the model join proved, and take NO name when
            // nothing proved one.
            const char* symName = "";
            {
                const FieldArchive::JSMEntityInfo* jsmN = FindJSMByLiveEntity(i);
                if (jsmN && jsmN->symName[0]) symName = jsmN->symName;
            }

            // v0.18.3.228: Race-free TALKABILITY signal. Runtime flags (talk
            // @0x24B / push @0x249) are set by TALKRADIUS/TALKON during script
            // execution, so a talkable NPC can still read talk=0 at scan time.
            // The static JSM hasTalkSetup flag (script uses TALKRADIUS or TALKON)
            // is parsed at load and cannot race. On ggsta1 TALKRADIUS never fires
            // at all (scripts use TALKON), so runtime capture alone missed the
            // station attendant 'ekiin' and the 'gsm*' students.
            bool jsmTalk = false;
            {
                const FieldArchive::JSMEntityInfo* jsmT = FindJSMByLiveEntity(i);  // v0.62.0
                if (jsmT && jsmT->hasTalkSetup) jsmTalk = true;
            }
            // v0.18.3.235: talkability is STICKY per field. The talkonoff flag is
            // not just set late, it is TRANSIENT — on ggroom1 Quistis reads talk=1
            // on the first catalog build and 0 on every build after, so she was
            // kept on entry and then party-filtered a second later (a party member
            // WITH a talk radius must stay). Latch the observation instead.
            if (i < MAX_ENTITIES && talkonoff > 0) s_entSeenTalkable[i] = true;
            // Runtime capture (v0.18.3.227) is kept as a secondary signal: it
            // catches entities whose talk radius is enabled dynamically at
            // runtime rather than declared in the static script.
            bool talkable = (talkonoff > 0) || jsmTalk ||
                            (i < MAX_ENTITIES && (s_entSeenTalkable[i] ||
                                                  s_entTalkRadius[i] > 0));

            // v0.18.3.228: per-entity scan trace (see field_nav_catalog_diag.inl).
            // v0.18.3.234: once per field load, not on every rebuild.
            if (!s_scanTraced)
                LogScanEntity(i, symName, (int)modelId, (unsigned)triId,
                              (int)talkonoff, (int)pushonoff, (int)throughonoff, jsmTalk,
                              (unsigned)(i < MAX_ENTITIES ? s_entTalkRadius[i] : 0),
                              talkable, fpX, fpY);

            // v0.14.108 / v0.17.8.3: Party-member / non-interactive-character filter.
            // A FOLLOWING party member is identified by behavioral fingerprint:
            // a visible character (model 0-9) the player walks through
            // (throughonoff>0) with no talk/push -- model slots are field-local
            // so canonical-ID matching (the failed v0.14.107 approach) doesn't
            // work. v0.17.8.3 adds STANDING / high-model scene actors (dorm,
            // Laguna dream): party members placed static or walk-through with no
            // talk/push, sometimes on full NPC models (model>=10), caught by the
            // SYM NAME. The name is also the safe discriminator that protects
            // draw points reusing a party model (Fire Cavern 'drpoint' = model 9,
            // flags 0): 'drpoint'/'savePoint'/'l1' don't match IsPartyCharacterSym
            // so they're KEPT for downstream reclassification. Rules:
            //   - model 0-9 + no talk/push + thru>0       -> following member
            //   - character SYM + no talk/push (any model) -> scene-placed member
            //   - non-character SYM                        -> KEEP
            // Talkable characters (talk>0) are never filtered. Real exits come
            // from the trigger-line/gateway path, not runtime entities, so this
            // never drops an exit. Race: TALKRADIUS setting talkonoff after this
            // scan could transiently filter an NPC; mitigated by per-F9 refresh.
            {
                // v0.18.3.232: PARTY FILTER — driven by setpc, not the SYM name.
                //
                // setpc (0x255) holds the character ID (0-7) for an entity that is
                // an actual party character, and 0xFE for anything that is not. The
                // catalog already trusts this byte to identify the player.
                //
                // The previous SYM-based rules were built on a false premise. The
                // engine instantiates only the ACTIVE party members, while the JSM
                // SYM list names all six playable characters, so every NPC slot is
                // shifted. On ggsta1 (party = Squall+Zell+Quistis) slot2 is Quistis
                // but carries SYM 'irvine', and the station attendant in slot3
                // carries SYM 'rinoa' — so the "named party member" rule deleted the
                // train guard the player needs to buy a ticket, along with two
                // students. Only the two slots whose shifted SYMs happened to look
                // non-party survived, which is exactly the reported symptom.
                //
                // setpc has no such ambiguity: a real NPC is never a party character
                // no matter which SYM lands on it. Party members are still filtered
                // (preserving the v0.17.8.3 dormitory/classroom behavior), EXCEPT
                // when talkable — an interactable party member is kept and labeled by
                // proper name, per the interactable-party-member requirement.
                bool noInteract  = (talkonoff == 0 && pushonoff == 0 && !talkable);
                bool isPartyChar = IsPartyCharacterSetpc(setpc);
                // v0.18.3.236 (#71): IN-PARTY rule, from the bg2f_2/bg2f_1
                // Selphie evidence (2026-07-12 run). An entity whose setpc
                // character is currently in the ACTIVE party formation is the
                // follow entity (or a scene double of a recruited member) —
                // never a catalog target, even when talkable. Post-join
                // Selphie on bg2f_2 was talk=1 thru=0, so only the roster
                // identifies her.
                //
                // Deliberately NOT a walk-through (thru>0) rule: the .235
                // ggroom1 fix keeps Quistis (talk=1 push=1 thru=1, NOT in the
                // active party during that scene) as a named catalog entry —
                // flags alone cannot separate her from a follower. The roster
                // is the discriminator that preserves both behaviors.
                //
                // Known remaining gap (#71): a NOT-yet-recruited scene actor
                // parked invisible pre-scene (bg2f_2 Selphie before her run-in)
                // still lists — the entity SHOW/HIDE flag was never located
                // (v05.69 VISDIAG investigation closed without a result).
                // Needs a per-session flag-discovery diagnostic on bg2f_2.
                // v0.18.3.263 (#83 follow-up): use the FIELD controlled-party
                // array (0x01CFE990), not the savemap array. This is how the game
                // itself distinguishes a following party member from an
                // interactable one: the controlled team's setpc values are in
                // 0x01CFE990; the OTHER team standing in the room is not, so its
                // members fall through to the talkable-scene-actor path. Fixes both
                // the whole-party over-listing (the walking train is the field
                // party) and the split-party leader confusion.
                bool inActiveParty = isPartyChar &&
                                     IsInFieldControlledParty(setpc);
                // v0.18.3.261 (#83): CORRECTED talk-suppress polarity, scoped to
                // the party-filter keep decision. Disassembly of FF8_EN.exe proved
                // the talk-selection routine (0x004796E0) SKIPS an entity whose
                // 0x24B byte is nonzero and considers it only when 0x24B==0 --
                // TALKON writes 0, TALKOFF writes 1. So 0x24B==0 is TALK-ENABLED,
                // the opposite of the mod's historic `talkonoff>0` reading.
                //
                // v0.18.3.262 (#83): a talkable scene actor must be a NON-active-
                // party member. The active party (leader + followers, the "party
                // train") also reads talkonoff==0 (untouched default), so a
                // talkonoff-only rule listed the whole walking party as NPCs
                // (glfury1). The talk byte can't separate them, so gate on roster
                // membership: non-active + placed + talkonoff==0 -> keep as scene
                // actor (glfurin4 Quistis/Zell/Selphie, the #83 case); active
                // member -> filter as party train. Tradeoff: talkable active
                // members in gather scenes aren't surfaced -- better than listing
                // the party in every field. "Placed" (tri or fp nonzero) guards
                // unplaced ghost slots.
                //
                // v0.18.3.264 (#83): in an ASSEMBLY scene the roster is STANDING
                // and interactable, so keep them -- only the controlled player is
                // excluded. In a walking field the roster is the follow train and
                // is filtered. A non-roster placed party char is always an actor.
                bool isPlayer = (i == s_playerEntityIdx);
                bool placed = (triId > 0) || (fpX != 0 || fpY != 0);
                bool talkableActor = !isPlayer && placed && (talkonoff == 0) &&
                                     (modelId >= 0) &&
                                     (!inActiveParty || sceneAssembly);
                // The player (leader) is always filtered: never a catalog target.
                if (isPartyChar && !talkableActor &&
                    (noInteract || inActiveParty || isPlayer)) {
                    const char* pn = PartyCharacterNameById(setpc);
                    Log::Field("FieldNavigation: [party-filter] ent%d model=%d setpc=%d (%s) "
                               "sym='%s' filtered (party member; thru=%d inParty=%d noInteract=%d)",
                               i, (int)modelId, (int)setpc, pn ? pn : "?",
                               symName, (int)throughonoff, inActiveParty ? 1 : 0,
                               noInteract ? 1 : 0);
                    continue;
                }
            }

            // v0.17.8.3: the v0.17.8.2 [party-filter-miss] diagnostic was
            // removed here once the fix was BAT-confirmed on bgryo2_1 (all six
            // party entities -- squalls/squallsd/zell/zells/selphie/selphies,
            // including the model-11 selphie -- filtered as named party members,
            // zero misses, navigation intact). Draw-point safety holds by
            // construction: 'drpoint' is not a character name and has thru=0, so
            // neither the follower nor the named-party branch touches it.

            // v0.18.3.269 (#71): HIDDEN-ENTITY filter. The SHOW/HIDE flag is
            // entity flags dword @0x160, bit 3 (0x08): HIDE sets it, SHOW clears
            // it. Found by resolving the engine's opcode table (base 0x00B8DE94,
            // validated against [0x57]=TALKON/[0x58]=TALKOFF): SHOW (opcode 0x60)
            // @0x0051EAD0 does `and ecx,0xFFFFFFF7`, HIDE (opcode 0x61)
            // @0x0051EB40 does `or ecx,8`, both on [entity+0x160].
            //
            // This is the flag the v05.69 VISDIAG investigation failed to locate.
            // Without it the catalog announces actors that are scripted into the
            // scene but not yet drawn -- e.g. Zell and Selphie listed on the
            // Caraway statue screen (glfurin3) before they appear, which only
            // happens once the glass is placed. Same root cause as the #71
            // "not-yet-recruited scene actor parked invisible pre-scene" gap.
            {
                uint32_t entFlags = *(uint32_t*)(block + 0x160);
                // v0.75.0 (#112): the same exemption the JSM-injection path takes.
                // HIDE + TALKON in one init is an invisible interaction point, not
                // an object that is not there. See JSMEntityInfo::invisibleTalkTarget.
                const FieldArchive::JSMEntityInfo* jeHide = FindJSMByLiveEntity(i);
                const bool invisTalk = (jeHide && jeHide->invisibleTalkTarget);
                if ((entFlags & 0x08) != 0 && i != s_playerEntityIdx && !invisTalk) {
                    if (!s_scanTraced)
                        Log::Field("FieldNavigation: [SCAN-DROP] ent%d sym='%s' hidden "
                                   "(flags@0x160=0x%08X bit3 set by HIDE) -- skipped",
                                   i, symName, (unsigned)entFlags);
                    continue;
                }
                if ((entFlags & 0x08) != 0 && invisTalk && !s_scanTraced)
                    Log::Field("FieldNavigation: [SCAN-KEEP] ent%d sym='%s' hidden but its init "
                               "calls HIDE and TALKON together -- an invisible interaction point, "
                               "the picture is in the background art [v0.75.0]", i, symName);
            }

            // v0.17.7.1: Walkmesh exclusion rule.
            //
            // Drop entities that are BOTH non-talkable AND non-pushable AND
            // positioned off the walkmesh. These are typically light sources,
            // particle emitters, decorative props, and other scenery the
            // player cannot reach or interact with. The OR-with-talkonoff /
            // pushonoff condition preserves entities like over-railing guards
            // (off-mesh but talkable) and walking NPCs whose model puts them
            // briefly off-mesh between steps (talk radius keeps them).
            //
            // Light sources entering via JSM_ENT_INTERACTIVE_OBJECT promotion
            // (bypassing the existing ENTITY_SKIP_NAMES BG filter) drop here
            // because lights have no talkradius. fepic1's three exit Lines
            // pass through unaffected because they're injected from the
            // SETLINE/JSM-MAP_EXIT block, not the runtime loop -- their
            // walkmesh check lives in those blocks (added separately).
            //
            // The v0.12.08 reachability filter (REMOVED in v0.12.09 because
            // bggate_6 has a guard on tri=87 while the player stands on
            // tri=22, disconnected islands within one screen) does not
            // recur here: the guard has talkonoff>0 so the OR keeps it.
            //
            // Skip player and entities without a readable position (fpX=fpY=0
            // covers the placeholder case where the engine hasn't placed the
            // entity yet -- treat that as on-mesh provisionally rather than
            // dropping prematurely).
            if (i != s_playerEntityIdx &&
                talkonoff == 0 && pushonoff == 0 &&
                (fpX != 0 || fpY != 0)) {
                float wmX = (float)(fpX / 4096);
                float wmY = (float)(fpY / 4096);
                if (!IsInsideWalkmesh(wmX, wmY)) {
                    Log::Field("FieldNavigation: [walkmesh-excl] ent%d model=%d "
                               "pos=(%.0f,%.0f) off-mesh + no-talk/push -- excluded",
                               i, (int)modelId, wmX, wmY);
                    continue;
                }
            }

            // v05.52: Classify entity type by interaction flags.
            // setpc==0 means this IS the player; setpc!=0 means it isn't.
            // Interaction flags determine what the player can do with it.
            // v0.07.97: Entities with visible generic character models (modelId >= 10)
            // always classify as NPC. Walking NPCs get pushonoff before talkonoff
            // (PUSHRADIUS fires in init, TALKRADIUS fires later), so push-only
            // at catalog-build time doesn't mean "object" for visible characters.
            // The pushonoff → Object path only applies to invisible (model<0) entities.
            // v0.18.3.228: `talkable` (static JSM talk setup, or either runtime
            // signal) classifies the entity as an NPC up front, so a talkable NPC
            // whose talkonoff flag has not been set yet no longer falls through to
            // the push-only skip below and get discarded.
            EntityType etype = ENT_UNKNOWN;
            if (talkable)                         etype = ENT_NPC;
            else if (pushonoff > 0 && modelId >= 10) etype = ENT_NPC;   // v0.07.97: walking NPC, talk not yet set
            else if (pushonoff > 0 && modelId >= 0) {
                // v0.12.12: visible push-only entity — not interactable, skip.
                // v0.18.3.228: now logs its reason instead of dropping silently.
                if (!s_scanTraced) LogScanDropPushOnly(i, symName, (int)modelId);
                continue;
            }
            else if (pushonoff > 0)               etype = ENT_OBJECT;
            // v0.18.3.230: the EXIT branch now requires an INVISIBLE entity.
            // throughonoff just means "the player can walk through this"; on a
            // visible character that makes it an NPC, not an exit. ggsta1's
            // 'ekiin'/'gsl0' (model 8, thru=1) were surfacing as type=Exit, which
            // is both wrong to announce and wrong for navigation grouping. Real
            // exits are invisible trigger entities (model < 0) — and genuine map
            // exits come from the trigger-line/gateway path anyway, not from here.
            else if (throughonoff > 0 && modelId < 0) etype = ENT_EXIT;
            else                                  etype = ENT_NPC;  // visible character, default to NPC
            bool hasModel = (modelId >= 0);
            bool hasInteraction = (talkonoff > 0 || pushonoff > 0 || throughonoff > 0);
            // v0.12.08 Fix D: Entities at (0,0) with triId=0 are inactive placeholders
            // even if they have a model assigned. Must have either a valid walkmesh
            // triangle OR a non-zero position to be considered placed.
            bool isPlaced = (triId > 0) || (hasModel && (fpX != 0 || fpY != 0));
            bool isSpecialJSM = false;
            if (!hasModel) {
                {
                    // v0.62.0: model join. A model-less entity has no model key,
                    // so BuildLiveJsmMap's third pass matches it on its exact
                    // static SET3 position instead -- which is how the Lunar Base
                    // infirmary save point is recognised.
                    const FieldArchive::JSMEntityInfo* jsm2 = FindJSMByLiveEntity(i);
                    if (jsm2 && (jsm2->type == FieldArchive::JSM_ENT_SAVE_POINT ||
                                 jsm2->type == FieldArchive::JSM_ENT_DRAW_POINT ||
                                 jsm2->type == FieldArchive::JSM_ENT_SHOP ||
                                 jsm2->type == FieldArchive::JSM_ENT_CARD_GAME)) {
                        // Check if the entity has a valid runtime position
                        int32_t fpX2 = *(int32_t*)(block + 0x190);
                        int32_t fpY2 = *(int32_t*)(block + 0x194);
                        if (fpX2 != 0 || fpY2 != 0 || triId > 0) {
                            isSpecialJSM = true;
                            isPlaced = true;
                        }
                    }
                }
            }
            // v0.65.0: the field-scoped drop. `handle` is a valve wheel the
            // player turns in the Missile Base and a lever nothing touches in
            // the escape pod; the SYM alone cannot tell those apart, so the
            // table is keyed on the field too. Placed BEFORE qualifies[i] is
            // set, so a dropped entity is dropped rather than hidden later.
            {
                // v0.66.0 (#112): the Ragnarok's ninth alien. rgroad3 carries a
                // second `alien01` whose default method is three words long and
                // which can never be fought. Listing it would send the player
                // across the ship to a Propagator that is not there.
                if (PgCatalogDrop(FF8Addresses::pCurrentFieldName, symName)) {
                    if (!s_scanTraced)
                        Log::Field("FieldNavigation: [catalog] ent%d sym='%s' dropped: "
                                   "the cutscene-only Propagator decoy in '%s' -- the "
                                   "fightable one there is a different entity [v0.66.0]",
                                   i, symName, FF8Addresses::pCurrentFieldName);
                    continue;
                }
                const FieldScopedEntity* fs =
                    FieldScopedFor(FF8Addresses::pCurrentFieldName, symName);
                if (fs && !fs->display) {
                    if (!s_scanTraced)
                        Log::Field("FieldNavigation: [catalog] ent%d sym='%s' dropped: "
                                   "field-scoped exclusion for '%s' -- the scene works "
                                   "it, the player never does [v0.65.0]",
                                   i, symName, fs->field);
                    continue;
                }
            }
            if (isPlaced && (hasModel || isSpecialJSM)) {
                qualifies[i] = true;
                EntityInfo ei_info = {};
                ei_info.entityIdx  = i;
                ei_info.modelId    = modelId;
                ei_info.triangleId = triId;
                ei_info.type       = etype;
                ei_info.gatewayIdx = -1;
                ei_info.name[0]    = '\0';
                // v0.18.3.235: entity type refinement + display naming.
                // Extracted to field_nav_catalog_naming.inl — a statement fragment
                // included inline (same pattern as field_nav_catalog_dedupe.inl) to
                // hold this file under the 80 KB source-size CI guard. It declares
                // `entName`, which is consumed immediately below.
                const char* entName = RefineEntityTypeAndName(i, modelId, setpc, symName, talkable, ei_info);
                // v0.62.0 (#123): a NAMED character, named from the entity the
                // model join proved. Aaron: "I love the idea of the catalog
                // identifying NPCs when it can." Until now the only path to a
                // real name for a live entity was v0.61.0's duplicate-drop
                // transfer, which needed a second JSM copy of the same person to
                // exist -- and, joining on the slot, put "Quistis" on Piet.
                // Only a CURATED name from ENTITY_DISPLAY_NAMES is used: a SYM
                // that is not in the table is an internal dev symbol and must
                // never be spoken (v0.18.3.291).
                if (ei_info.type == ENT_NPC && symName[0]) {
                    for (const EntityDisplayName* mL = ENTITY_DISPLAY_NAMES; mL->sym; mL++) {
                        if (_stricmp(symName, mL->sym) != 0) continue;
                        if (mL->display && mL->display[0]) {
                            entName = mL->display;
                            Log::Field("FieldNavigation: [catalog] ent%d named '%s' from its own "
                                       "script '%s' (model join) [v0.62.0]", i, entName, symName);
                        }
                        break;
                    }
                }
                // v0.66.0 (#112): THE FIELD-SCOPED NAME. Some symbols mean
                // different things in different rooms, and `alien01` is the
                // extreme case -- eight Propagators across eight fields, four
                // colours, and the colour IS the puzzle. Aaron: "we want the NPC
                // in the catalog to say 'Red Propagator', 'Purple Propagator',
                // etc." This runs last so it outranks both the generic type name
                // and the curated SYM name above; the colours come from PG_LIST,
                // which is also what the pair logic reasons about, so the name
                // spoken and the name reasoned about cannot drift apart.
                // v0.76.0 (#112): and the things in those rooms that are not
                // monsters. rgguest2's `comp` was catalogued correctly from
                // v0.75.0 and announced as "NPC", which is a name for nothing.
                if (const char* pgObj = PgObjectName(FF8Addresses::pCurrentFieldName, symName)) {
                    entName = pgObj;
                    Log::Field("FieldNavigation: [catalog] ent%d named '%s' -- "
                               "field-scoped object, from the Propagator table [v0.76.0]",
                               i, entName);
                }
                char pgName[32];
                if (PgCatalogName(FF8Addresses::pCurrentFieldName, symName,
                                  pgName, sizeof pgName)) {
                    entName = pgName;
                    Log::Field("FieldNavigation: [catalog] ent%d named '%s' -- "
                               "field-scoped, from the Propagator table [v0.66.0]",
                               i, entName);
                }
                strncpy(ei_info.name, entName, sizeof(ei_info.name) - 1);
                ei_info.name[sizeof(ei_info.name) - 1] = '\0';
                fresh[i] = ei_info;
                if (!s_scanTraced)
                    LogScanKeep(i, symName, EntityTypeName(ei_info.type), ei_info.name);
            } else {
                // v0.18.3.228: the other formerly-silent drop path (unplaced
                // placeholder entity) now records its reason.
                if (!s_scanTraced)
                    LogScanDropUnplaced(i, symName, (int)modelId, (unsigned)triId,
                                        fpX, fpY, hasModel, isSpecialJSM);
            }
        }
        // v0.18.3.234: the scan trace has now emitted one full pass for this
        // field; suppress it on subsequent rebuilds (RefreshCatalog runs ~1/sec).
        s_scanTraced = true;

        // v05.70: Screen filtering — exclude entities on the other side of
        // any active SETLINE trigger line from the player. This hides NPCs
        // that are on a different camera screen (e.g. front vs back of
        // bgroom_1 classroom). Only applies when we have trigger lines and
        // can read the player position.
        // v05.71: Track which entities were screen-filtered so we can identify
        // which trigger lines are true screen transitions (they separate the
        // player from at least one filtered entity).
        bool screenFiltered[MAX_ENTITIES] = {};
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float playerX, playerY;
            if (GetEntityPos(s_playerEntityIdx, playerX, playerY)) {
                int filtered = 0;
                for (int i = 0; i < (int)lim; i++) {
                    if (!qualifies[i]) continue;
                    if (i == s_playerEntityIdx) continue;  // never filter the player
                    float entX, entY;
                    if (GetEntityPos(i, entX, entY)) {
                        if (IsSeparatedByTriggerLine(playerX, playerY, entX, entY)) {
                            qualifies[i] = false;
                            screenFiltered[i] = true;
                            filtered++;
                        }
                    }
                }
                if (filtered > 0) {
                    Log::Field("FieldNavigation: [screen] filtered %d entities on other side of trigger lines (player at %.0f,%.0f)",
                               filtered, playerX, playerY);
                }
            }
        }

        // v0.12.08 Fix B: Walkmesh reachability filter — REMOVED in v0.12.09.
        // Was filtering entities on disconnected walkmesh islands, but FF8 fields
        // often have multiple elevation layers that create disconnected islands
        // within the same playable screen (e.g., bggate_6 guard on tri=87 vs
        // player on tri=22). This caused false positives, removing valid NPCs.
        // The existing trigger-line-based screen filtering handles off-screen
        // entities adequately without walkmesh connectivity checks.

        // Remember which entry the user had selected (entity or gateway).
        int prevSelectedEntity = -2;  // -2 = none, -1 = gateway, >=0 = entity
        int prevSelectedGateway = -1;
        if (s_selectedCatalogIdx >= 0 && s_selectedCatalogIdx < s_catalogCount) {
            prevSelectedEntity  = s_catalog[s_selectedCatalogIdx].entityIdx;
            prevSelectedGateway = s_catalog[s_selectedCatalogIdx].gatewayIdx;
        }

        // Rebuild: first, retain existing entity entries that still qualify (in order).
        // v05.51: Also retain background entities (entityIdx <= -100) — they'll be
        // re-evaluated below. Only retain "others" entities here.
        EntityInfo newCatalog[MAX_CATALOG] = {};
        int newCount = 0;
        for (int c = 0; c < s_catalogCount && newCount < MAX_CATALOG; c++) {
            int ei = s_catalog[c].entityIdx;
            if (ei >= 0 && ei < (int)lim && qualifies[ei]) {
                newCatalog[newCount++] = fresh[ei];
                LiveGateRuntime("runtime", fresh[ei]);
                qualifies[ei] = false;  // mark as placed
            }
            // Gateway and background entries are re-added below — skip them here.
        }
        // Then append any newly-qualifying entities at the end.
        int added = 0;
        for (int i = 0; i < (int)lim && newCount < MAX_CATALOG; i++) {
            if (qualifies[i]) {
                newCatalog[newCount++] = fresh[i];
                LiveGateRuntime("runtime", fresh[i]);
                added++;
            }
        }

        // v0.07.83 trigger-line Exit/Event injection. Extracted to
        // field_nav_catalog_triglines.inl (v0.18.3.294) to get this file back
        // under the CI size ceiling (see GitHub #37). The fragment runs inline
        // here and operates on the surrounding RefreshCatalog() locals.
        // Pure textual move -- no logic change.
        ComputePlayerZoneReachability();

        // v0.20.40 (gate observe, WIDENED): the v0.20.39 window (vars 328..359) did NOT catch the
        // gate-open flip, so this gate writes a var outside 337/339/340. Diff the WHOLE field var
        // bank (0x800 bytes @ EXIT_VARBLOCK_BASE 0x01CFE9B8 -- a fixed engine global, always mapped)
        // and log EVERY changed var with the player triangle + world position. A BAT that opens a
        // gate now reveals the exact varN:before->after wherever it lives, plus the gate spot.
        // glwater* only, log-only, no catalog change.
        if (strncmp(s_currentFieldName, "glwater", 7) == 0) {
            static uint8_t s_gvLast[0x800] = {};
            static bool    s_gvSeen  = false;
            static char    s_gvField[64] = {};
            const volatile uint8_t* vbk = (const volatile uint8_t*)(uintptr_t)0x01CFE9B8u;
            bool fieldChg = (strncmp(s_gvField, s_currentFieldName, 63) != 0);
            bool baseline = fieldChg || !s_gvSeen;
            bool changed  = baseline;
            char delta[320]; int dp = 0; delta[0] = 0; int nch = 0;
            if (!baseline) {
                for (int k = 0; k < 0x800; k++) {
                    uint8_t v = vbk[k];
                    if (v != s_gvLast[k]) {
                        changed = true; nch++;
                        if (dp < (int)sizeof(delta) - 24)
                            dp += snprintf(delta + dp, sizeof(delta) - dp, "var%d:%d->%d ",
                                           k, (int)s_gvLast[k], (int)v);
                    }
                }
            }
            if (changed) {
                float pX = 0, pY = 0; GetEntityPos(s_playerEntityIdx, pX, pY);
                int pTri = -1;
                if (FF8Addresses::pFieldStateOthers && s_playerEntityIdx >= 0) {
                    uint8_t* pb = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                    if (pb) pTri = *(uint16_t*)(pb + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
                }
                Log::Field("FieldNavigation: [GATE-DIAG] '%s' playerTri=%d pos=(%.0f,%.0f) nchanged=%d "
                           "changed=[%s]%s [v0.20.40]",
                           s_currentFieldName, pTri, pX, pY, nch, delta,
                           baseline ? " (baseline)" : "");
                for (int k = 0; k < 0x800; k++) s_gvLast[k] = vbk[k];
                s_gvSeen = true;
                strncpy(s_gvField, s_currentFieldName, 63); s_gvField[63] = 0;
            }
        }
        InjectTriggerLineExits(newCatalog, newCount);

        // v0.12.24 / v0.17.7.1: Add SETLINE-triggered interactive objects as
        // "Interaction N". Line entities classified as JSM_ENT_LINE_INTERACTIVE
        // by the JSM scanner have dialog opcodes (MES/ASK/AMES/AASK) AND a
        // TALKRADIUS/TALKON setup -- genuine player-facing interactions
        // (dormitory bed/desk/wardrobe, classroom desk/sign, etc.).
        //
        // v0.17.7.1: dropped the dual-purpose SCREEN_BOUND-promote-to-Interactive
        // path. JSM scanner now classifies dual-purpose lines (MAPJUMP + dialog
        // + talk setup, like dormitory beds) directly as LINE_INTERACTIVE
        // because TALKRADIUS/TALKON wins over MAPJUMP in the new priority
        // ordering. fepic1's three exit Lines (MAPJUMP only, no dialog, no
        // talk setup) stay LINE_SCREEN_BOUND and are added as Exits above
        // rather than mislabeled here.
        // v0.18.3.268 BUG A: GetEntityPos() returns false when the player's tri
        // id is 0 ("not yet placed") -- normal on close-up screens where coords
        // are still valid. glfurin3 (statue) reads tri=0, so the old
        // `if (GetEntityPos(...))` wrapper skipped EVERY interaction there.
        // Position is only needed for the screen-side filter below, so degrade
        // to "no filtering" rather than dropping everything.
        InjectInteractionLines(newCatalog, newCount);

        // v05.52: Background entities removed from cycling catalog.
        // They have no walkmesh position and can't be auto-driven to.
        // Active bg entities are still logged in BGDIAG for diagnostics.
        // Interactive objects (terminals, bulletin boards) are script-triggered
        // by walk-on zones — the player discovers them by exploring, not by
        // navigating to an entity position.

        // Late position resolution (extracted v0.17.7.0).
        // See field_nav_catalog_lateres.inl. Must run in this order — STRUCT-POS
        // depends on LATE-RESOLVE having populated hasPosition for entities that
        // had only hasPshmCoords on entry.
        ResolveLatePositions();
        MatchSet3LateCaptures();
        ResolveStructPositions();
        ResolveTriangleCentroidPositions();  // v0.18.3.286 (#85): last-resort walkmesh approximation
        ResolveStateExclusionGroups();       // v0.18.3.297 (#85): now ACTS -- marks wrong-world-state entities

        // v0.07.74: Inject JSM-classified special entities not already in the catalog.
        // These are entities beyond the runtime state array (SYM index >= entCount)
        // or entities in the array that weren't caught by approach A above.
        // Uses SET3 positions extracted by the JSM scanner when available.
        InjectJsmSpecials(newCatalog, newCount, fresh, lim);

        // v0.07.83 JSM_ENT_MAP_EXIT catalog injection. Extracted to
        // field_nav_catalog_mapexits.inl (v0.18.3.266) to keep this file under
        // the CI source-file size ceiling (hard fail > 80 KB); the fragment runs
        // inline here (own braces) and operates on the local newCatalog[]/
        // newCount and the surrounding scan state. Pure textual move — no logic
        // change. See that file for the full logic.
        InjectMapExits(newCatalog, newCount, base, lim);
        s_mapExitTraced = true;   // v0.131.8: its drop lines are said once per field

        // v0.07.94 INF gateway exit injection. Extracted to
        // field_nav_catalog_gateways.inl (v0.18.3.276) to bring this file back
        // under the CI size ceiling (hard fail > 80 KB; it had reached 82.2 KB
        // and the push was refused). The fragment runs inline here (own braces)
        // and operates on the local newCatalog[]/newCount plus the gateway and
        // captured-line state. Pure textual move — no logic change. Same pattern
        // as _mapexits / _dedupe / _naming. See that file for the full logic.
        InjectGatewayExits(newCatalog, newCount);

        // v0.17.8.8 object/line dedupe + raw-SYM relabel. Extracted to
        // field_nav_catalog_dedupe.inl (v0.17.8.9) to keep this file under the
        // size ceiling; the fragment runs inline here (own braces) and operates
        // on the local newCatalog[]/newCount. See that file for the full logic.
        DedupeCatalog(newCatalog, newCount);

        // Detect changes and log.
        bool changed = (newCount != s_catalogCount || added > 0);
        if (!changed) {
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].entityIdx != s_catalog[c].entityIdx ||
                    newCatalog[c].gatewayIdx != s_catalog[c].gatewayIdx) {
                    changed = true; break;
                }
            }
        }

        // Commit.
        memcpy(s_catalog, newCatalog, sizeof(s_catalog));
        s_catalogCount = newCount;
        s_nonPlayerCount = 0;
        for (int c = 0; c < s_catalogCount; c++) {
            if (s_catalog[c].entityIdx != s_playerEntityIdx)
                s_nonPlayerCount++;
        }

        // Restore selection to the same entity/gateway/bg, or clamp.
        s_selectedCatalogIdx = 0;
        if (prevSelectedEntity != -2) {
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == prevSelectedEntity &&
                    s_catalog[c].gatewayIdx == prevSelectedGateway) {
                    s_selectedCatalogIdx = c; break;
                }
            }
        }

        if (changed) {
            Log::Field("FieldNavigation: [refresh] catalog: %d entries (%d navigable, %d new entities), player=ent%d",
                       s_catalogCount, s_nonPlayerCount, added, s_playerEntityIdx);
            // v0.18.3.298 (#92): the dump used to test `entityIdx <= -300`
            // BEFORE anything looked at the -400 range, so every INF gateway
            // exit (sentinel -400 - d) fell into the JSM branch, computed a
            // slot of 100 + d, failed the `ji < s_jsmEntityCount` bounds check
            // and printed NOTHING. Silently. Every "[refresh] catalog: N
            // entries" block in every log this project has ever collected
            // under-reports by the number of gateway exits -- the 2026-07-31
            // prison logs claim 6 entries and print 5, and the merged phantom
            // staircase that walked Aaron off the floor was invisible here.
            //
            // Fixed by testing the gateway case first, bounds-checking each
            // sentinel against its OWN table, and -- most importantly -- never
            // dropping a row on the floor again: an entry that matches no known
            // sentinel range now prints as UNKNOWN rather than vanishing.
            //
            // Gateways are identified by gatewayIdx, NOT by the -400 sentinel
            // arithmetic. The two sentinel ranges genuinely overlap -- JSM uses
            // -300 - slot with MAX_JSM_ENTITIES = 128, so slot 100 also lands
            // on -400 -- and gatewayIdx is exact: every non-gateway injection
            // site sets it to -1 (catalog.inl 409/664/898, triglines 204/304,
            // mapexits 277) and only the gateway block sets it >= 0.
            for (int c = 0; c < s_catalogCount; c++) {
                const int eidx    = s_catalog[c].entityIdx;
                if (eidx == s_playerEntityIdx) continue;
                const int gwSlot  = s_catalog[c].gatewayIdx;
                const int jsmSlot = -(eidx + 300);
                const int trgSlot = -(eidx + 200);

                if (gwSlot >= 0 && gwSlot < s_dedupGatewayCount) {
                    Log::Field("FieldNavigation: [refresh]   cat%d GATEWAY grp%d dest=%u "
                               "name='%s' center=(%.0f,%.0f) merged=%d",
                               c, gwSlot, (unsigned)s_dedupGateways[gwSlot].destFieldId,
                               s_catalog[c].name,
                               s_dedupGateways[gwSlot].centerX,
                               s_dedupGateways[gwSlot].centerY,
                               s_dedupGateways[gwSlot].count);
                }
                else if (eidx <= -300 && jsmSlot >= 0 && jsmSlot < s_jsmEntityCount) {
                    Log::Field("FieldNavigation: [refresh]   cat%d JSM ent%d type=%s name='%s' pos=(%d,%d)",
                               c, jsmSlot, EntityTypeName(s_catalog[c].type), s_catalog[c].name,
                               (int)s_jsmEntities[jsmSlot].posX, (int)s_jsmEntities[jsmSlot].posY);
                }
                else if (eidx <= -200 && eidx > -300) {
                    float tcx = (trgSlot >= 0 && trgSlot < s_capturedLineCount)
                                ? (float)(s_capturedLines[trgSlot].x1 + s_capturedLines[trgSlot].x2) / 2.0f : 0;
                    float tcz = (trgSlot >= 0 && trgSlot < s_capturedLineCount)
                                ? (float)(s_capturedLines[trgSlot].y1 + s_capturedLines[trgSlot].y2) / 2.0f : 0;
                    Log::Field("FieldNavigation: [refresh]   cat%d TRIGGER line%d center=(%.0f,%.0f) name='%s'",
                               c, trgSlot, tcx, tcz, s_catalog[c].name);
                }
                else if (eidx >= 0) {
                    Log::Field("FieldNavigation: [refresh]   cat%d ent%d model=%d type=%s name='%s'",
                               c, eidx, (int)s_catalog[c].modelId,
                               EntityTypeName(s_catalog[c].type), s_catalog[c].name);
                }
                else {
                    Log::Field("FieldNavigation: [refresh]   cat%d UNKNOWN sentinel ent%d "
                               "type=%s name='%s' gwIdx=%d (gwCount=%d jsmCount=%d lineCount=%d)",
                               c, eidx, EntityTypeName(s_catalog[c].type), s_catalog[c].name,
                               s_catalog[c].gatewayIdx, s_dedupGatewayCount,
                               s_jsmEntityCount, s_capturedLineCount);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: Exception in RefreshCatalog()");
    }
}

// ============================================================================
// v0.08.24: One-shot hex dump of PSHM_W entity-scope functions
// ============================================================================
// Reads raw x86 instruction bytes from the game's .text segment at runtime.
// These are the same bytes the CPU executes — we just log them so we can
// disassemble the parametric curve formula offline.
//
// Target addresses (Steam 2013 en-US, no ASLR):
//   0x00532890 — entity-scope parametric curve subroutine (~300 insns)
//   0x0051C9C0 — type-clamping dispatch (caller of 0x00532890)
