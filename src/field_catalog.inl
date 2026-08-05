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
int symIdx = s_symOthersOffset + i;
if (symIdx >= 0 && symIdx < s_symNameCount) {
    const FieldArchive::JSMEntityInfo* jsm = FindJSMBySym(s_symNames[symIdx]);
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
    if (symIdx >= 0 && symIdx < s_symNameCount) {
        const char* sym = s_symNames[symIdx];
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
    if (symIdx >= 0 && symIdx < s_symNameCount) {
        const FieldArchive::JSMEntityInfo* jsmDP = FindJSMBySym(s_symNames[symIdx]);
        if (jsmDP && jsmDP->drawPointTriggerOf >= 0) {
            ei_info.type = ENT_DRAW_POINT;
            entName = "Draw Point";
            Log::Field("FieldNavigation: [catalog] ent%d '%s' reclassified as Draw Point "
                       "(triggers JSM draw point ent%d)",
                       i, s_symNames[symIdx], jsmDP->drawPointTriggerOf);
        }
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

                    EntityInfo trigExit = {};
                    trigExit.entityIdx  = -200 - t;
                    trigExit.modelId    = -1;
                    trigExit.triangleId = 0;
                    trigExit.type       = ENT_EXIT;
                    trigExit.gatewayIdx = -1;
                    strncpy(trigExit.name, exitName, sizeof(trigExit.name) - 1);
                    trigExit.name[sizeof(trigExit.name) - 1] = '\0';
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
                    newCatalog[newCount++] = evEntry;
                }
            }
        }
}

// ============================================================================
// 3c. Interaction-line injection  (was inline block, field_nav_catalog.inl :535-715)
// ============================================================================
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
                    if (!s_capturedLines[t].active) continue;
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
                        int wantIdx = s_jsmDoors + t;
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
                        interactionNum++;
                        intEntry.type = ENT_INTERACTION;
                        if (soloName)
                            snprintf(intEntry.name, sizeof(intEntry.name), "%s", soloName);
                        else
                            snprintf(intEntry.name, sizeof(intEntry.name), "Interaction %d", interactionNum);
                    }
                    newCatalog[newCount++] = intEntry;
                }
            }
        }
}

// ============================================================================
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
                    if (bestCatIdx >= 0) {
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
            newCatalog[newCount++] = jsmEntry;
            Log::Field("FieldNavigation: [refresh] JSM-injected %s at (%d,%d) sym='%s'%s",
                       jtName, (int)je.posX, (int)je.posY, je.symName,
                       isApprox286 ? " [triangle-centroid approx]" : "");
        }
}

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

        // v0.07.83: Entity-based exits from JSM_ENT_MAP_EXIT "Other" entities.
        // These are interactive objects (elevators, doors, trigger zones) whose
        // scripts contain MAPJUMP. They have destination field IDs in param.
        // Position from SET3 extraction or runtime entity, or captured SETLINE.
        for (int j = 0; j < s_jsmEntityCount && newCount < MAX_CATALOG; j++) {
            const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
            if (je.type != FieldArchive::JSM_ENT_MAP_EXIT) continue;
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
                        int symIdx = s_symOthersOffset + entIdx;
                        if (symIdx >= 0 && symIdx < s_symNameCount &&
                            _stricmp(s_symNames[symIdx], je.symName) == 0) {
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
                int liveIdx = je.jsmIndex;
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
            if (s_gatewayCount > 0 && (je.param < 0 || je.param > 982))
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
            if (s_gatewayCount > 0 && !je.paramFromInterp) {
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

            mapExit.entityIdx  = -300 - j;  // JSM-injected sentinel
            mapExit.modelId    = -1;
            mapExit.triangleId = je.posTriangle;
            mapExit.type       = ENT_EXIT;
            mapExit.gatewayIdx = -1;
            strncpy(mapExit.name, exitName, sizeof(mapExit.name) - 1);
            mapExit.name[sizeof(mapExit.name) - 1] = '\0';
            newCatalog[newCount++] = mapExit;
        }
}

// ============================================================================
// 3f. INF gateway exit injection  (was field_nav_catalog_gateways.inl)
// ============================================================================
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
        bool staleGatewayRuleActive = fieldHasLineExits && (s_gatewayCount <= 1);
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
                EntityInfo gwExit = {};
                gwExit.entityIdx  = -400 - d;  // sentinel for INF gateway exits
                gwExit.modelId    = -1;
                gwExit.triangleId = 0;
                gwExit.type       = ENT_EXIT;
                gwExit.gatewayIdx = d;  // index into s_dedupGateways
                strncpy(gwExit.name, s_dedupGateways[d].displayName, sizeof(gwExit.name) - 1);
                gwExit.name[sizeof(gwExit.name) - 1] = '\0';
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
        for (int j = 0; j < s_jsmEntityCount; j++) {
            if (s_jsmEntities[j].isItemPickup &&
                s_jsmEntities[j].posTriangle == newCatalog[a].triangleId) {
                isPickup = true; break;
            }
        }
        if (!isPickup) continue;
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
            if (bi > -200 || bi <= -300) continue;   // B must be a trigger line
            if (!(newCatalog[b].type == ENT_SAVE_POINT ||
                  newCatalog[b].type == ENT_DRAW_POINT ||
                  newCatalog[b].type == ENT_SHOP ||
                  newCatalog[b].type == ENT_CARD_GAME))
                continue;                            // ...with a specific meaning
            int tb = -(bi + 200);
            if (tb < 0 || tb >= s_capturedLineCount) continue;
            float bx = (float)(s_capturedLines[tb].x1 + s_capturedLines[tb].x2) / 2.0f;
            float by = (float)(s_capturedLines[tb].y1 + s_capturedLines[tb].y2) / 2.0f;
            float ddx = ax - bx, ddy = ay - by;
            if (ddx*ddx + ddy*ddy > ENT_DUP_DIST*ENT_DUP_DIST) continue;
            entDupRemoved[a] = true;
            Log::Field("FieldNavigation: [dedup] dropped entity ent%d ('%s') at (%.0f,%.0f): "
                       "same object as %s line%d at (%.0f,%.0f) [v0.18.3.233]",
                       ai, newCatalog[a].name, ax, ay,
                       EntityTypeName(newCatalog[b].type), tb, bx, by);
            break;
        }
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

static void RefreshCatalog()
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return;
    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        // v0.18.3.317 (#95/#98/#115): prison-shaft catalog floor-gating dry-run
        // (log-only; self-gates to shaft fields and fires once per floor change).
        // Gathers the per-floor varblock state needed to derive the Cycle-2 gate.
        ShaftCatalogDryRun();

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
            const char* symName = "";
            {
                int symIdxFilt = s_symOthersOffset + i;
                if (symIdxFilt >= 0 && symIdxFilt < s_symNameCount)
                    symName = s_symNames[symIdxFilt];
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
                int symIdxT = s_symOthersOffset + i;
                if (symIdxT >= 0 && symIdxT < s_symNameCount) {
                    const FieldArchive::JSMEntityInfo* jsmT = FindJSMBySym(s_symNames[symIdxT]);
                    if (jsmT && jsmT->hasTalkSetup) jsmTalk = true;
                }
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
                if ((entFlags & 0x08) != 0 && i != s_playerEntityIdx) {
                    if (!s_scanTraced)
                        Log::Field("FieldNavigation: [SCAN-DROP] ent%d sym='%s' hidden "
                                   "(flags@0x160=0x%08X bit3 set by HIDE) -- skipped",
                                   i, symName, (unsigned)entFlags);
                    continue;
                }
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
                int symIdx2 = s_symOthersOffset + i;
                if (symIdx2 >= 0 && symIdx2 < s_symNameCount) {
                    const FieldArchive::JSMEntityInfo* jsm2 = FindJSMBySym(s_symNames[symIdx2]);
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
                qualifies[ei] = false;  // mark as placed
            }
            // Gateway and background entries are re-added below — skip them here.
        }
        // Then append any newly-qualifying entities at the end.
        int added = 0;
        for (int i = 0; i < (int)lim && newCount < MAX_CATALOG; i++) {
            if (qualifies[i]) {
                newCatalog[newCount++] = fresh[i];
                added++;
            }
        }

        // v0.07.83 trigger-line Exit/Event injection. Extracted to
        // field_nav_catalog_triglines.inl (v0.18.3.294) to get this file back
        // under the CI size ceiling (see GitHub #37). The fragment runs inline
        // here and operates on the surrounding RefreshCatalog() locals.
        // Pure textual move -- no logic change.
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
